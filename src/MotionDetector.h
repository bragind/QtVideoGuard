#pragma once

#include "DetectionConfig.h"

#include <opencv2/core.hpp>
#include <opencv2/video/background_segm.hpp>

#include <vector>

struct DetectionResult
{
    std::vector<cv::Rect> boxes;
    cv::Mat foregroundMask;
    double foregroundPercent = 0.0;
    bool globalMotionSuppressed = false;
};

class MotionDetector
{
public:
    MotionDetector();

    void configure(const DetectionConfig &config);
    void reset();
    DetectionResult process(const cv::Mat &frame);

private:
    struct Track
    {
        int id = 0;
        cv::Rect2f box;
        int hits = 0;
        int missed = 0;
    };

    void rebuildBackgroundModel();
    cv::Mat buildAllowedMask(const cv::Size &size) const;
    double flowCoherence(const cv::Mat &flow,
                         const cv::Mat &foregroundMask,
                         const cv::Rect &box) const;
    std::vector<cv::Rect> updateTracks(const std::vector<cv::Rect> &detections);
    static double intersectionOverUnion(const cv::Rect2f &a,
                                        const cv::Rect2f &b);
    static int oddKernel(int value);

    DetectionConfig config_;
    cv::Ptr<cv::BackgroundSubtractor> backgroundSubtractor_;
    cv::Mat previousGray_;
    std::vector<Track> tracks_;
    int nextTrackId_ = 1;
};
