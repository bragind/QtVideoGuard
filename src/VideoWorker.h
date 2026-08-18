#pragma once

#include "DetectionConfig.h"
#include "MotionDetector.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <opencv2/videoio.hpp>

class QTimer;

class VideoWorker final : public QObject
{
    Q_OBJECT

public:
    explicit VideoWorker(QObject *parent = nullptr);
    ~VideoWorker() override;

public slots:
    void start(const QString &source);
    void stop();
    void applyConfig(const DetectionConfig &config);

signals:
    void frameReady(const QImage &image,
                    int detectionCount,
                    double processingMilliseconds,
                    double foregroundPercent,
                    bool globalMotionSuppressed);
    void stateChanged(bool running, const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void readFrame();

private:
    bool openSource(const QString &source);
    static QImage matToQImage(const cv::Mat &mat);

    QTimer *timer_ = nullptr;
    cv::VideoCapture capture_;
    MotionDetector detector_;
    DetectionConfig config_;
    QString source_;
    bool isFile_ = false;
    int consecutiveReadErrors_ = 0;
};
