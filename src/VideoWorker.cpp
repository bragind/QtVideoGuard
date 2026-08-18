#include "VideoWorker.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace {

QString safeSourceName(const QString &source)
{
    QUrl url(source);
    if (url.isValid() && !url.password().isEmpty()) {
        url.setPassword(QStringLiteral("***"));
        return url.toString();
    }
    return source;
}

} // namespace

VideoWorker::VideoWorker(QObject *parent)
    : QObject(parent)
{
}

VideoWorker::~VideoWorker()
{
    capture_.release();
}

void VideoWorker::start(const QString &source)
{
    stop();

    source_ = source.trimmed();
    if (source_.isEmpty()) {
        emit errorOccurred(
            QStringLiteral("Укажите адрес камеры или выберите видеофайл."));
        return;
    }

    detector_.configure(config_);
    detector_.reset();

    if (!openSource(source_)) {
        emit errorOccurred(
            QStringLiteral("Не удалось открыть источник: %1")
                .arg(safeSourceName(source_)));
        emit stateChanged(false, QStringLiteral("Источник не открыт"));
        return;
    }

    if (timer_ == nullptr) {
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);
        connect(timer_, &QTimer::timeout, this, &VideoWorker::readFrame);
    }

    int intervalMilliseconds = 1;
    if (isFile_) {
        const double fps = capture_.get(cv::CAP_PROP_FPS);
        if (fps > 1.0 && fps < 240.0) {
            intervalMilliseconds =
                std::clamp(static_cast<int>(1000.0 / fps), 4, 100);
        } else {
            intervalMilliseconds = 33;
        }
    }

    consecutiveReadErrors_ = 0;
    timer_->start(intervalMilliseconds);
    emit stateChanged(true, QStringLiteral("Видео запущено"));
}

void VideoWorker::stop()
{
    if (timer_ != nullptr) {
        timer_->stop();
    }
    capture_.release();
    detector_.reset();
    consecutiveReadErrors_ = 0;
    emit stateChanged(false, QStringLiteral("Остановлено"));
}

void VideoWorker::applyConfig(const DetectionConfig &config)
{
    config_ = config;
    detector_.configure(config_);
}

void VideoWorker::readFrame()
{
    if (!capture_.isOpened()) {
        return;
    }

    cv::Mat frame;
    if (!capture_.read(frame) || frame.empty()) {
        ++consecutiveReadErrors_;

        if (isFile_) {
            if (timer_ != nullptr) {
                timer_->stop();
            }
            capture_.release();
            emit stateChanged(false, QStringLiteral("Видеофайл завершён"));
            return;
        }

        if (consecutiveReadErrors_ >= 15) {
            if (timer_ != nullptr) {
                timer_->stop();
            }
            capture_.release();
            emit errorOccurred(QStringLiteral(
                "Поток камеры прерван. Проверьте сеть и RTSP-адрес."));
            emit stateChanged(false, QStringLiteral("Поток потерян"));
        }
        return;
    }

    consecutiveReadErrors_ = 0;
    QElapsedTimer elapsed;
    elapsed.start();

    const DetectionResult result = detector_.process(frame);

    cv::Mat display;
    if (config_.showForegroundMask) {
        cv::cvtColor(result.foregroundMask, display, cv::COLOR_GRAY2BGR);
    } else {
        display = frame.clone();
    }

    for (const cv::Rect &box : result.boxes) {
        cv::rectangle(display, box, cv::Scalar(40, 220, 40), 3);
        cv::putText(display,
                    "OBJECT",
                    cv::Point(box.x, std::max(22, box.y - 7)),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.65,
                    cv::Scalar(40, 220, 40),
                    2,
                    cv::LINE_AA);
    }

    if (result.globalMotionSuppressed) {
        cv::putText(display,
                    "GLOBAL MOTION SUPPRESSED",
                    cv::Point(18, 34),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.75,
                    cv::Scalar(20, 190, 255),
                    2,
                    cv::LINE_AA);
    }

    const double processingMilliseconds =
        static_cast<double>(elapsed.nsecsElapsed()) / 1'000'000.0;

    emit frameReady(matToQImage(display),
                    static_cast<int>(result.boxes.size()),
                    processingMilliseconds,
                    result.foregroundPercent,
                    result.globalMotionSuppressed);
}

bool VideoWorker::openSource(const QString &source)
{
    static const QRegularExpression numericSource(QStringLiteral("^\\d+$"));
    const bool isCameraIndex = numericSource.match(source).hasMatch();
    isFile_ = !isCameraIndex && QFileInfo::exists(source);

    bool opened = false;
    if (isCameraIndex) {
        opened = capture_.open(source.toInt(), cv::CAP_ANY);
    } else {
        opened = capture_.open(source.toStdString(), cv::CAP_ANY);
    }

    if (opened && !isFile_) {
        // Не все backend-ы поддерживают параметр, но там, где поддерживают,
        // это уменьшает задержку RTSP-потока.
        capture_.set(cv::CAP_PROP_BUFFERSIZE, 1.0);
    }

    return opened;
}

QImage VideoWorker::matToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return {};
    }

    switch (mat.type()) {
    case CV_8UC1: {
        const QImage image(mat.data,
                           mat.cols,
                           mat.rows,
                           static_cast<qsizetype>(mat.step),
                           QImage::Format_Grayscale8);
        return image.copy();
    }
    case CV_8UC3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        const QImage image(rgb.data,
                           rgb.cols,
                           rgb.rows,
                           static_cast<qsizetype>(rgb.step),
                           QImage::Format_RGB888);
        return image.copy();
    }
    case CV_8UC4: {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        const QImage image(rgba.data,
                           rgba.cols,
                           rgba.rows,
                           static_cast<qsizetype>(rgba.step),
                           QImage::Format_RGBA8888);
        return image.copy();
    }
    default:
        return {};
    }
}
