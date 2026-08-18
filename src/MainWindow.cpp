#include "MainWindow.h"

#include "VideoWidget.h"
#include "VideoWorker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QSpinBox *integerControl(int minimum,
                         int maximum,
                         int value,
                         const QString &suffix = {})
{
    auto *control = new QSpinBox;
    control->setRange(minimum, maximum);
    control->setValue(value);
    control->setSuffix(suffix);
    control->setKeyboardTracking(false);
    return control;
}

QDoubleSpinBox *realControl(double minimum,
                            double maximum,
                            double value,
                            double step,
                            int decimals,
                            const QString &suffix = {})
{
    auto *control = new QDoubleSpinBox;
    control->setRange(minimum, maximum);
    control->setValue(value);
    control->setSingleStep(step);
    control->setDecimals(decimals);
    control->setSuffix(suffix);
    control->setKeyboardTracking(false);
    return control;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildInterface();
    loadSettings();
    videoWidget_->setIgnoredZones(ignoredZones_);

    workerThread_ = new QThread(this);
    worker_ = new VideoWorker;
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this,
            &MainWindow::startVideoRequested,
            worker_,
            &VideoWorker::start,
            Qt::QueuedConnection);
    connect(this,
            &MainWindow::stopVideoRequested,
            worker_,
            &VideoWorker::stop,
            Qt::QueuedConnection);
    connect(this,
            &MainWindow::configChanged,
            worker_,
            &VideoWorker::applyConfig,
            Qt::QueuedConnection);
    connect(worker_,
            &VideoWorker::frameReady,
            this,
            &MainWindow::handleFrame);
    connect(worker_,
            &VideoWorker::stateChanged,
            this,
            &MainWindow::handleState);
    connect(worker_,
            &VideoWorker::errorOccurred,
            this,
            [this](const QString &message) {
                statusLabel_->setText(message);
                QMessageBox::warning(this,
                                     QStringLiteral("Ошибка видео"),
                                     message);
            });

    connectConfigControls();
    workerThread_->start();
    updateConfig();
}

MainWindow::~MainWindow()
{
    saveSettings();

    if (workerThread_ != nullptr && workerThread_->isRunning()) {
        QMetaObject::invokeMethod(worker_,
                                  &VideoWorker::stop,
                                  Qt::BlockingQueuedConnection);
        workerThread_->quit();
        workerThread_->wait();
    }
}

void MainWindow::browseVideo()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Выберите видео"),
        {},
        QStringLiteral(
            "Видео (*.mp4 *.avi *.mkv *.mov *.m4v *.wmv);;Все файлы (*.*)"));
    if (!path.isEmpty()) {
        sourceEdit_->setText(path);
    }
}

void MainWindow::startVideo()
{
    updateConfig();
    emit startVideoRequested(sourceEdit_->text());
}

void MainWindow::stopVideo()
{
    emit stopVideoRequested();
}

void MainWindow::updateConfig()
{
    emit configChanged(collectConfig());
}

void MainWindow::handleFrame(const QImage &image,
                             int detectionCount,
                             double processingMilliseconds,
                             double foregroundPercent,
                             bool globalMotionSuppressed)
{
    videoWidget_->setFrame(image);

    QString suppression;
    if (globalMotionSuppressed) {
        suppression = QStringLiteral(" · глобальное движение подавлено");
    }

    statsLabel_->setText(
        QStringLiteral("Объекты: %1 · обработка: %2 мс · передний план: %3%%")
            .arg(detectionCount)
            .arg(processingMilliseconds, 0, 'f', 1)
            .arg(foregroundPercent, 0, 'f', 1) +
        suppression);
}

void MainWindow::handleState(bool running, const QString &message)
{
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
    statusLabel_->setText(message);
}

void MainWindow::addIgnoredZone(const QRectF &zone)
{
    ignoredZones_.append(zone.normalized());
    videoWidget_->setIgnoredZones(ignoredZones_);
    updateConfig();
}

void MainWindow::toggleZoneDrawing()
{
    const bool enabled = drawZoneButton_->isChecked();
    drawZoneButton_->setText(
        enabled ? QStringLiteral("Завершить выделение")
                : QStringLiteral("Добавить зону мышью"));
    videoWidget_->setZoneDrawingEnabled(enabled);
}

void MainWindow::clearIgnoredZones()
{
    ignoredZones_.clear();
    videoWidget_->setIgnoredZones(ignoredZones_);
    updateConfig();
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("Qt Video Guard — фильтрация крупных объектов"));
    resize(1280, 760);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    auto *sourceLayout = new QHBoxLayout;
    sourceLayout->addWidget(new QLabel(QStringLiteral("Источник:")));

    sourceEdit_ = new QLineEdit;
    sourceEdit_->setPlaceholderText(
        QStringLiteral("rtsp://user:password@192.168.1.10:554/stream или файл"));
    sourceLayout->addWidget(sourceEdit_, 1);

    auto *browseButton = new QPushButton(QStringLiteral("Файл…"));
    startButton_ = new QPushButton(QStringLiteral("Запустить"));
    stopButton_ = new QPushButton(QStringLiteral("Остановить"));
    stopButton_->setEnabled(false);

    sourceLayout->addWidget(browseButton);
    sourceLayout->addWidget(startButton_);
    sourceLayout->addWidget(stopButton_);
    rootLayout->addLayout(sourceLayout);

    auto *contentLayout = new QHBoxLayout;
    videoWidget_ = new VideoWidget;
    contentLayout->addWidget(videoWidget_, 1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(buildSettingsPanel());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(355);
    scroll->setMaximumWidth(420);
    scroll->setFrameShape(QFrame::NoFrame);
    contentLayout->addWidget(scroll);
    rootLayout->addLayout(contentLayout, 1);

    statsLabel_ =
        new QLabel(QStringLiteral("Объекты: 0 · обработка: — · передний план: —"));
    statusLabel_ = new QLabel(QStringLiteral("Остановлено"));

    auto *footer = new QHBoxLayout;
    footer->addWidget(statsLabel_, 1);
    footer->addWidget(statusLabel_);
    rootLayout->addLayout(footer);

    setCentralWidget(central);

    connect(browseButton,
            &QPushButton::clicked,
            this,
            &MainWindow::browseVideo);
    connect(startButton_,
            &QPushButton::clicked,
            this,
            &MainWindow::startVideo);
    connect(stopButton_,
            &QPushButton::clicked,
            this,
            &MainWindow::stopVideo);
    connect(sourceEdit_,
            &QLineEdit::returnPressed,
            this,
            &MainWindow::startVideo);
    connect(videoWidget_,
            &VideoWidget::ignoredZoneCreated,
            this,
            &MainWindow::addIgnoredZone);

    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #20262d; color: #e8edf2; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
        "  background: #11161c; border: 1px solid #46515d;"
        "  border-radius: 4px; padding: 5px; }"
        "QPushButton { background: #315b80; border: 0; border-radius: 4px;"
        "  padding: 7px 12px; }"
        "QPushButton:hover { background: #3d709d; }"
        "QPushButton:disabled { background: #3b4249; color: #89929a; }"
        "QPushButton:checked { background: #9a5f25; }"
        "QGroupBox { border: 1px solid #3b4651; border-radius: 5px;"
        "  margin-top: 12px; padding-top: 8px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 9px;"
        "  padding: 0 4px; }"));
}

QWidget *MainWindow::buildSettingsPanel()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *backgroundGroup =
        new QGroupBox(QStringLiteral("Модель переднего плана"));
    auto *backgroundForm = new QFormLayout(backgroundGroup);
    backgroundModel_ = new QComboBox;
    backgroundModel_->addItem(QStringLiteral("MOG2 — универсальный"));
    backgroundModel_->addItem(QStringLiteral("KNN — сложный фон"));
    history_ = integerControl(30, 3000, 500, QStringLiteral(" кадров"));
    threshold_ = realControl(4.0, 100.0, 24.0, 1.0, 1);
    learningRate_ = realControl(0.0001, 0.1, 0.003, 0.001, 4);
    detectShadows_ = new QCheckBox(QStringLiteral("Отсекать тени"));
    detectShadows_->setChecked(true);
    backgroundForm->addRow(QStringLiteral("Алгоритм"), backgroundModel_);
    backgroundForm->addRow(QStringLiteral("История"), history_);
    backgroundForm->addRow(QStringLiteral("Порог модели"), threshold_);
    backgroundForm->addRow(QStringLiteral("Скорость обучения"), learningRate_);
    backgroundForm->addRow({}, detectShadows_);
    layout->addWidget(backgroundGroup);

    auto *morphologyGroup =
        new QGroupBox(QStringLiteral("Шум и разрывы маски"));
    auto *morphologyForm = new QFormLayout(morphologyGroup);
    blurKernel_ = integerControl(1, 31, 5, QStringLiteral(" px"));
    openKernel_ = integerControl(1, 31, 3, QStringLiteral(" px"));
    closeKernel_ = integerControl(1, 51, 9, QStringLiteral(" px"));
    blurKernel_->setSingleStep(2);
    openKernel_->setSingleStep(2);
    closeKernel_->setSingleStep(2);
    morphologyForm->addRow(QStringLiteral("Размытие"), blurKernel_);
    morphologyForm->addRow(QStringLiteral("Удаление точек"), openKernel_);
    morphologyForm->addRow(QStringLiteral("Склейка объекта"), closeKernel_);
    layout->addWidget(morphologyGroup);

    auto *geometryGroup =
        new QGroupBox(QStringLiteral("Размер и форма объекта"));
    auto *geometryForm = new QFormLayout(geometryGroup);
    minAreaPercent_ =
        realControl(0.01, 50.0, 0.35, 0.05, 2, QStringLiteral(" % кадра"));
    maxAreaPercent_ =
        realControl(1.0, 100.0, 45.0, 1.0, 1, QStringLiteral(" % кадра"));
    minWidth_ = integerControl(2, 4000, 45, QStringLiteral(" px"));
    minHeight_ = integerControl(2, 4000, 45, QStringLiteral(" px"));
    minFillPercent_ =
        realControl(1.0, 100.0, 18.0, 1.0, 1, QStringLiteral(" %"));
    maxAspectRatio_ = realControl(1.0, 30.0, 8.0, 0.5, 1);
    geometryForm->addRow(QStringLiteral("Мин. площадь"), minAreaPercent_);
    geometryForm->addRow(QStringLiteral("Макс. площадь"), maxAreaPercent_);
    geometryForm->addRow(QStringLiteral("Мин. ширина"), minWidth_);
    geometryForm->addRow(QStringLiteral("Мин. высота"), minHeight_);
    geometryForm->addRow(QStringLiteral("Заполнение рамки"), minFillPercent_);
    geometryForm->addRow(QStringLiteral("Макс. вытянутость"), maxAspectRatio_);
    layout->addWidget(geometryGroup);

    auto *dynamicsGroup =
        new QGroupBox(QStringLiteral("Трава, ветки, волны и дрожание"));
    auto *dynamicsForm = new QFormLayout(dynamicsGroup);
    useFlowCoherence_ =
        new QCheckBox(QStringLiteral("Проверять направление движения"));
    useFlowCoherence_->setChecked(true);
    minFlowMagnitude_ =
        realControl(0.05, 10.0, 0.65, 0.05, 2, QStringLiteral(" px"));
    minFlowCoherence_ =
        realControl(0.0, 1.0, 0.28, 0.05, 2);
    confirmationFrames_ =
        integerControl(1, 60, 4, QStringLiteral(" кадров"));
    maxMissedFrames_ =
        integerControl(0, 60, 6, QStringLiteral(" кадров"));
    maxForegroundPercent_ =
        realControl(1.0, 100.0, 42.0, 1.0, 1, QStringLiteral(" %"));
    dynamicsForm->addRow({}, useFlowCoherence_);
    dynamicsForm->addRow(QStringLiteral("Мин. скорость"), minFlowMagnitude_);
    dynamicsForm->addRow(QStringLiteral("Согласованность"), minFlowCoherence_);
    dynamicsForm->addRow(QStringLiteral("Подтверждение"), confirmationFrames_);
    dynamicsForm->addRow(QStringLiteral("Допуск пропусков"), maxMissedFrames_);
    dynamicsForm->addRow(QStringLiteral("Глобальное движение"),
                         maxForegroundPercent_);
    layout->addWidget(dynamicsGroup);

    auto *zonesGroup =
        new QGroupBox(QStringLiteral("Зоны и диагностика"));
    auto *zonesLayout = new QVBoxLayout(zonesGroup);
    drawZoneButton_ = new QPushButton(QStringLiteral("Добавить зону мышью"));
    drawZoneButton_->setCheckable(true);
    auto *clearZonesButton =
        new QPushButton(QStringLiteral("Очистить зоны исключения"));
    showForegroundMask_ =
        new QCheckBox(QStringLiteral("Показывать бинарную маску"));
    zonesLayout->addWidget(drawZoneButton_);
    zonesLayout->addWidget(clearZonesButton);
    zonesLayout->addWidget(showForegroundMask_);
    layout->addWidget(zonesGroup);
    layout->addStretch(1);

    connect(drawZoneButton_,
            &QPushButton::clicked,
            this,
            &MainWindow::toggleZoneDrawing);
    connect(clearZonesButton,
            &QPushButton::clicked,
            this,
            &MainWindow::clearIgnoredZones);

    return panel;
}

DetectionConfig MainWindow::collectConfig() const
{
    DetectionConfig config;
    config.backgroundModel =
        backgroundModel_->currentIndex() == 1
            ? DetectionConfig::BackgroundModel::Knn
            : DetectionConfig::BackgroundModel::Mog2;
    config.history = history_->value();
    config.threshold = threshold_->value();
    config.learningRate = learningRate_->value();
    config.detectShadows = detectShadows_->isChecked();
    config.blurKernel = blurKernel_->value();
    config.openKernel = openKernel_->value();
    config.closeKernel = closeKernel_->value();
    config.minAreaPercent = minAreaPercent_->value();
    config.maxAreaPercent = maxAreaPercent_->value();
    config.minWidth = minWidth_->value();
    config.minHeight = minHeight_->value();
    config.minFillPercent = minFillPercent_->value();
    config.maxAspectRatio = maxAspectRatio_->value();
    config.useFlowCoherence = useFlowCoherence_->isChecked();
    config.minFlowMagnitude = minFlowMagnitude_->value();
    config.minFlowCoherence = minFlowCoherence_->value();
    config.confirmationFrames = confirmationFrames_->value();
    config.maxMissedFrames = maxMissedFrames_->value();
    config.maxForegroundPercent = maxForegroundPercent_->value();
    config.showForegroundMask = showForegroundMask_->isChecked();
    config.ignoredZones = ignoredZones_;
    return config;
}

void MainWindow::loadSettings()
{
    QSettings settings;
    sourceEdit_->setText(settings.value(QStringLiteral("source")).toString());
    backgroundModel_->setCurrentIndex(
        settings.value(QStringLiteral("detector/model"), 0).toInt());
    history_->setValue(
        settings.value(QStringLiteral("detector/history"), 500).toInt());
    threshold_->setValue(
        settings.value(QStringLiteral("detector/threshold"), 24.0).toDouble());
    learningRate_->setValue(
        settings.value(QStringLiteral("detector/learningRate"), 0.003)
            .toDouble());
    detectShadows_->setChecked(
        settings.value(QStringLiteral("detector/shadows"), true).toBool());
    blurKernel_->setValue(
        settings.value(QStringLiteral("filter/blur"), 5).toInt());
    openKernel_->setValue(
        settings.value(QStringLiteral("filter/open"), 3).toInt());
    closeKernel_->setValue(
        settings.value(QStringLiteral("filter/close"), 9).toInt());
    minAreaPercent_->setValue(
        settings.value(QStringLiteral("filter/minArea"), 0.35).toDouble());
    maxAreaPercent_->setValue(
        settings.value(QStringLiteral("filter/maxArea"), 45.0).toDouble());
    minWidth_->setValue(
        settings.value(QStringLiteral("filter/minWidth"), 45).toInt());
    minHeight_->setValue(
        settings.value(QStringLiteral("filter/minHeight"), 45).toInt());
    minFillPercent_->setValue(
        settings.value(QStringLiteral("filter/minFill"), 18.0).toDouble());
    maxAspectRatio_->setValue(
        settings.value(QStringLiteral("filter/maxAspect"), 8.0).toDouble());
    useFlowCoherence_->setChecked(
        settings.value(QStringLiteral("flow/enabled"), true).toBool());
    minFlowMagnitude_->setValue(
        settings.value(QStringLiteral("flow/minMagnitude"), 0.65).toDouble());
    minFlowCoherence_->setValue(
        settings.value(QStringLiteral("flow/coherence"), 0.28).toDouble());
    confirmationFrames_->setValue(
        settings.value(QStringLiteral("tracking/confirmation"), 4).toInt());
    maxMissedFrames_->setValue(
        settings.value(QStringLiteral("tracking/missed"), 6).toInt());
    maxForegroundPercent_->setValue(
        settings.value(QStringLiteral("filter/globalMotion"), 42.0).toDouble());
    showForegroundMask_->setChecked(
        settings.value(QStringLiteral("view/showMask"), false).toBool());

    const int zoneCount = settings.beginReadArray(QStringLiteral("zones"));
    for (int index = 0; index < zoneCount; ++index) {
        settings.setArrayIndex(index);
        const QRectF zone = settings.value(QStringLiteral("rect")).toRectF();
        if (zone.isValid()) {
            ignoredZones_.append(zone);
        }
    }
    settings.endArray();
}

void MainWindow::saveSettings() const
{
    QSettings settings;
    const DetectionConfig config = collectConfig();
    const QUrl sourceUrl(sourceEdit_->text());
    if (sourceUrl.isValid() && !sourceUrl.password().isEmpty()) {
        // Пароль камеры не должен попадать в QSettings в открытом виде.
        settings.remove(QStringLiteral("source"));
    } else {
        settings.setValue(QStringLiteral("source"), sourceEdit_->text());
    }
    settings.setValue(QStringLiteral("detector/model"),
                      backgroundModel_->currentIndex());
    settings.setValue(QStringLiteral("detector/history"), config.history);
    settings.setValue(QStringLiteral("detector/threshold"), config.threshold);
    settings.setValue(QStringLiteral("detector/learningRate"),
                      config.learningRate);
    settings.setValue(QStringLiteral("detector/shadows"),
                      config.detectShadows);
    settings.setValue(QStringLiteral("filter/blur"), config.blurKernel);
    settings.setValue(QStringLiteral("filter/open"), config.openKernel);
    settings.setValue(QStringLiteral("filter/close"), config.closeKernel);
    settings.setValue(QStringLiteral("filter/minArea"),
                      config.minAreaPercent);
    settings.setValue(QStringLiteral("filter/maxArea"),
                      config.maxAreaPercent);
    settings.setValue(QStringLiteral("filter/minWidth"), config.minWidth);
    settings.setValue(QStringLiteral("filter/minHeight"), config.minHeight);
    settings.setValue(QStringLiteral("filter/minFill"),
                      config.minFillPercent);
    settings.setValue(QStringLiteral("filter/maxAspect"),
                      config.maxAspectRatio);
    settings.setValue(QStringLiteral("flow/enabled"),
                      config.useFlowCoherence);
    settings.setValue(QStringLiteral("flow/minMagnitude"),
                      config.minFlowMagnitude);
    settings.setValue(QStringLiteral("flow/coherence"),
                      config.minFlowCoherence);
    settings.setValue(QStringLiteral("tracking/confirmation"),
                      config.confirmationFrames);
    settings.setValue(QStringLiteral("tracking/missed"),
                      config.maxMissedFrames);
    settings.setValue(QStringLiteral("filter/globalMotion"),
                      config.maxForegroundPercent);
    settings.setValue(QStringLiteral("view/showMask"),
                      config.showForegroundMask);

    settings.beginWriteArray(QStringLiteral("zones"));
    for (int index = 0; index < ignoredZones_.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("rect"), ignoredZones_[index]);
    }
    settings.endArray();
}

void MainWindow::connectConfigControls()
{
    const auto changed = [this] { updateConfig(); };

    connect(backgroundModel_,
            &QComboBox::currentIndexChanged,
            this,
            changed);
    connect(history_, &QSpinBox::valueChanged, this, changed);
    connect(threshold_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(learningRate_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(detectShadows_, &QCheckBox::toggled, this, changed);
    connect(blurKernel_, &QSpinBox::valueChanged, this, changed);
    connect(openKernel_, &QSpinBox::valueChanged, this, changed);
    connect(closeKernel_, &QSpinBox::valueChanged, this, changed);
    connect(minAreaPercent_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(maxAreaPercent_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(minWidth_, &QSpinBox::valueChanged, this, changed);
    connect(minHeight_, &QSpinBox::valueChanged, this, changed);
    connect(minFillPercent_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(maxAspectRatio_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(useFlowCoherence_, &QCheckBox::toggled, this, changed);
    connect(minFlowMagnitude_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(minFlowCoherence_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(confirmationFrames_,
            &QSpinBox::valueChanged,
            this,
            changed);
    connect(maxMissedFrames_,
            &QSpinBox::valueChanged,
            this,
            changed);
    connect(maxForegroundPercent_,
            &QDoubleSpinBox::valueChanged,
            this,
            changed);
    connect(showForegroundMask_, &QCheckBox::toggled, this, changed);
}
