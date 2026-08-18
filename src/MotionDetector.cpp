#include "MotionDetector.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>

MotionDetector::MotionDetector()
{
    rebuildBackgroundModel();
}

void MotionDetector::configure(const DetectionConfig &config)
{
    const bool modelChanged =
        config.backgroundModel != config_.backgroundModel ||
        config.history != config_.history ||
        std::abs(config.threshold - config_.threshold) > 0.001 ||
        config.detectShadows != config_.detectShadows;

    config_ = config;

    if (modelChanged || backgroundSubtractor_.empty()) {
        rebuildBackgroundModel();
        previousGray_.release();
        tracks_.clear();
    }
}

void MotionDetector::reset()
{
    previousGray_.release();
    tracks_.clear();
    nextTrackId_ = 1;
    rebuildBackgroundModel();
}

DetectionResult MotionDetector::process(const cv::Mat &frame)
{
    DetectionResult result;
    if (frame.empty()) {
        return result;
    }

    cv::Mat blurred;
    const int blurSize = oddKernel(config_.blurKernel);
    if (blurSize > 1) {
        cv::GaussianBlur(frame, blurred, cv::Size(blurSize, blurSize), 0.0);
    } else {
        blurred = frame;
    }

    cv::Mat rawMask;
    backgroundSubtractor_->apply(blurred, rawMask, config_.learningRate);

    // Значения MOG2: 0 — фон, 127 — тень, 255 — передний план.
    // Порог 200 гарантированно удаляет тени.
    cv::threshold(rawMask,
                  result.foregroundMask,
                  config_.detectShadows ? 200.0 : 1.0,
                  255.0,
                  cv::THRESH_BINARY);

    const cv::Mat allowedMask = buildAllowedMask(frame.size());
    cv::bitwise_and(result.foregroundMask,
                    allowedMask,
                    result.foregroundMask);

    const int openSize = oddKernel(config_.openKernel);
    if (openSize > 1) {
        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(openSize, openSize));
        cv::morphologyEx(result.foregroundMask,
                         result.foregroundMask,
                         cv::MORPH_OPEN,
                         kernel);
    }

    const int closeSize = oddKernel(config_.closeKernel);
    if (closeSize > 1) {
        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(closeSize, closeSize));
        cv::morphologyEx(result.foregroundMask,
                         result.foregroundMask,
                         cv::MORPH_CLOSE,
                         kernel);
    }

    const double frameArea =
        static_cast<double>(frame.cols) * static_cast<double>(frame.rows);
    result.foregroundPercent =
        100.0 * static_cast<double>(cv::countNonZero(result.foregroundMask)) /
        std::max(1.0, frameArea);

    cv::Mat gray;
    cv::cvtColor(blurred, gray, cv::COLOR_BGR2GRAY);

    cv::Mat flow;
    if (config_.useFlowCoherence && !previousGray_.empty() &&
        previousGray_.size() == gray.size()) {
        cv::calcOpticalFlowFarneback(previousGray_,
                                     gray,
                                     flow,
                                     0.5,
                                     3,
                                     15,
                                     3,
                                     5,
                                     1.2,
                                     0);
    }
    gray.copyTo(previousGray_);

    if (result.foregroundPercent > config_.maxForegroundPercent) {
        result.globalMotionSuppressed = true;
        tracks_.clear();
        return result;
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::Mat contourMask = result.foregroundMask.clone();
    cv::findContours(contourMask,
                     contours,
                     cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Rect> candidates;
    candidates.reserve(contours.size());

    for (const auto &contour : contours) {
        const double contourArea = cv::contourArea(contour);
        const double areaPercent = 100.0 * contourArea / frameArea;
        if (areaPercent < config_.minAreaPercent ||
            areaPercent > config_.maxAreaPercent) {
            continue;
        }

        const cv::Rect box = cv::boundingRect(contour);
        if (box.width < config_.minWidth || box.height < config_.minHeight) {
            continue;
        }

        const double boxArea =
            static_cast<double>(box.width) * static_cast<double>(box.height);
        const double fillPercent = 100.0 * contourArea / std::max(1.0, boxArea);
        if (fillPercent < config_.minFillPercent) {
            continue;
        }

        const double aspect = std::max(
            static_cast<double>(box.width) / std::max(1, box.height),
            static_cast<double>(box.height) / std::max(1, box.width));
        if (aspect > config_.maxAspectRatio) {
            continue;
        }

        if (config_.useFlowCoherence && !flow.empty()) {
            const double coherence =
                flowCoherence(flow, result.foregroundMask, box);
            if (coherence < config_.minFlowCoherence) {
                continue;
            }
        }

        candidates.push_back(box);
    }

    result.boxes = updateTracks(candidates);
    return result;
}

void MotionDetector::rebuildBackgroundModel()
{
    if (config_.backgroundModel == DetectionConfig::BackgroundModel::Knn) {
        const double distanceThreshold =
            std::max(1.0, config_.threshold * config_.threshold);
        backgroundSubtractor_ = cv::createBackgroundSubtractorKNN(
            config_.history, distanceThreshold, config_.detectShadows);
    } else {
        backgroundSubtractor_ = cv::createBackgroundSubtractorMOG2(
            config_.history, config_.threshold, config_.detectShadows);
    }
}

cv::Mat MotionDetector::buildAllowedMask(const cv::Size &size) const
{
    cv::Mat allowed(size, CV_8UC1, cv::Scalar(255));

    for (const QRectF &zone : config_.ignoredZones) {
        const int left =
            std::clamp(static_cast<int>(std::round(zone.left() * size.width)),
                       0,
                       size.width - 1);
        const int top =
            std::clamp(static_cast<int>(std::round(zone.top() * size.height)),
                       0,
                       size.height - 1);
        const int right =
            std::clamp(static_cast<int>(std::round(zone.right() * size.width)),
                       0,
                       size.width);
        const int bottom =
            std::clamp(static_cast<int>(std::round(zone.bottom() * size.height)),
                       0,
                       size.height);

        if (right > left && bottom > top) {
            cv::rectangle(allowed,
                          cv::Rect(left, top, right - left, bottom - top),
                          cv::Scalar(0),
                          cv::FILLED);
        }
    }

    return allowed;
}

double MotionDetector::flowCoherence(const cv::Mat &flow,
                                     const cv::Mat &foregroundMask,
                                     const cv::Rect &box) const
{
    const cv::Rect imageBounds(0, 0, flow.cols, flow.rows);
    const cv::Rect bounded = box & imageBounds;
    if (bounded.empty()) {
        return 0.0;
    }

    double sumX = 0.0;
    double sumY = 0.0;
    double sumMagnitude = 0.0;
    int samples = 0;

    // Шаг 2 снижает стоимость расчёта на больших объектах.
    for (int y = bounded.y; y < bounded.y + bounded.height; y += 2) {
        const auto *flowRow = flow.ptr<cv::Point2f>(y);
        const auto *maskRow = foregroundMask.ptr<uchar>(y);
        for (int x = bounded.x; x < bounded.x + bounded.width; x += 2) {
            if (maskRow[x] == 0) {
                continue;
            }

            const cv::Point2f vector = flowRow[x];
            const double magnitude =
                std::hypot(static_cast<double>(vector.x),
                           static_cast<double>(vector.y));
            if (magnitude < config_.minFlowMagnitude) {
                continue;
            }

            sumX += vector.x;
            sumY += vector.y;
            sumMagnitude += magnitude;
            ++samples;
        }
    }

    if (samples < 12 || sumMagnitude < 1e-6) {
        return 0.0;
    }

    // 1.0 — все векторы направлены одинаково (твёрдый движущийся объект);
    // около 0.0 — хаотичное движение травы, листвы или бликов на воде.
    return std::hypot(sumX, sumY) / sumMagnitude;
}

std::vector<cv::Rect>
MotionDetector::updateTracks(const std::vector<cv::Rect> &detections)
{
    std::vector<bool> trackMatched(tracks_.size(), false);

    for (const cv::Rect &detection : detections) {
        int bestIndex = -1;
        double bestScore = 0.08;

        for (std::size_t index = 0; index < tracks_.size(); ++index) {
            if (trackMatched[index]) {
                continue;
            }

            const double score =
                intersectionOverUnion(tracks_[index].box, detection);
            if (score > bestScore) {
                bestScore = score;
                bestIndex = static_cast<int>(index);
            }
        }

        if (bestIndex >= 0) {
            Track &track = tracks_[static_cast<std::size_t>(bestIndex)];
            constexpr float smoothing = 0.35F;
            track.box.x =
                (1.0F - smoothing) * track.box.x + smoothing * detection.x;
            track.box.y =
                (1.0F - smoothing) * track.box.y + smoothing * detection.y;
            track.box.width = (1.0F - smoothing) * track.box.width +
                              smoothing * detection.width;
            track.box.height = (1.0F - smoothing) * track.box.height +
                               smoothing * detection.height;
            ++track.hits;
            track.missed = 0;
            trackMatched[static_cast<std::size_t>(bestIndex)] = true;
        } else {
            Track track;
            track.id = nextTrackId_++;
            track.box = detection;
            track.hits = 1;
            tracks_.push_back(track);
            trackMatched.push_back(true);
        }
    }

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        if (!trackMatched[index]) {
            ++tracks_[index].missed;
        }
    }

    std::erase_if(tracks_, [this](const Track &track) {
        return track.missed > config_.maxMissedFrames;
    });

    std::vector<cv::Rect> confirmed;
    for (const Track &track : tracks_) {
        if (track.hits >= config_.confirmationFrames && track.missed <= 2) {
            confirmed.emplace_back(
                cv::Rect(cvRound(track.box.x),
                         cvRound(track.box.y),
                         cvRound(track.box.width),
                         cvRound(track.box.height)));
        }
    }

    return confirmed;
}

double MotionDetector::intersectionOverUnion(const cv::Rect2f &a,
                                             const cv::Rect2f &b)
{
    const cv::Rect2f intersection = a & b;
    const float intersectionArea = intersection.area();
    const float unionArea = a.area() + b.area() - intersectionArea;
    return unionArea > 0.0F ? intersectionArea / unionArea : 0.0;
}

int MotionDetector::oddKernel(int value)
{
    if (value <= 1) {
        return 1;
    }
    return value % 2 == 0 ? value + 1 : value;
}
