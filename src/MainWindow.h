#pragma once

#include "DetectionConfig.h"

#include <QImage>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QThread;
class VideoWidget;
class VideoWorker;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void startVideoRequested(const QString &source);
    void stopVideoRequested();
    void configChanged(const DetectionConfig &config);

private slots:
    void browseVideo();
    void startVideo();
    void stopVideo();
    void updateConfig();
    void handleFrame(const QImage &image,
                     int detectionCount,
                     double processingMilliseconds,
                     double foregroundPercent,
                     bool globalMotionSuppressed);
    void handleState(bool running, const QString &message);
    void addIgnoredZone(const QRectF &zone);
    void toggleZoneDrawing();
    void clearIgnoredZones();

private:
    void buildInterface();
    QWidget *buildSettingsPanel();
    DetectionConfig collectConfig() const;
    void loadSettings();
    void saveSettings() const;
    void connectConfigControls();

    VideoWidget *videoWidget_ = nullptr;
    QLineEdit *sourceEdit_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *drawZoneButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *statsLabel_ = nullptr;

    QComboBox *backgroundModel_ = nullptr;
    QSpinBox *history_ = nullptr;
    QDoubleSpinBox *threshold_ = nullptr;
    QDoubleSpinBox *learningRate_ = nullptr;
    QCheckBox *detectShadows_ = nullptr;
    QSpinBox *blurKernel_ = nullptr;
    QSpinBox *openKernel_ = nullptr;
    QSpinBox *closeKernel_ = nullptr;
    QDoubleSpinBox *minAreaPercent_ = nullptr;
    QDoubleSpinBox *maxAreaPercent_ = nullptr;
    QSpinBox *minWidth_ = nullptr;
    QSpinBox *minHeight_ = nullptr;
    QDoubleSpinBox *minFillPercent_ = nullptr;
    QDoubleSpinBox *maxAspectRatio_ = nullptr;
    QCheckBox *useFlowCoherence_ = nullptr;
    QDoubleSpinBox *minFlowMagnitude_ = nullptr;
    QDoubleSpinBox *minFlowCoherence_ = nullptr;
    QSpinBox *confirmationFrames_ = nullptr;
    QSpinBox *maxMissedFrames_ = nullptr;
    QDoubleSpinBox *maxForegroundPercent_ = nullptr;
    QCheckBox *showForegroundMask_ = nullptr;

    QVector<QRectF> ignoredZones_;
    QThread *workerThread_ = nullptr;
    VideoWorker *worker_ = nullptr;
};
