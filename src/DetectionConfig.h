#pragma once

#include <QMetaType>
#include <QRectF>
#include <QVector>

struct DetectionConfig
{
    enum class BackgroundModel {
        Mog2 = 0,
        Knn = 1
    };

    BackgroundModel backgroundModel = BackgroundModel::Mog2;

    // Параметры модели фона.
    int history = 500;
    double threshold = 24.0;
    double learningRate = 0.003;
    bool detectShadows = true;

    // Предварительная обработка и морфология.
    int blurKernel = 5;
    int openKernel = 3;
    int closeKernel = 9;

    // Геометрические ограничения кандидата.
    double minAreaPercent = 0.35;
    double maxAreaPercent = 45.0;
    int minWidth = 45;
    int minHeight = 45;
    double minFillPercent = 18.0;
    double maxAspectRatio = 8.0;

    // Фильтр согласованности оптического потока.
    bool useFlowCoherence = true;
    double minFlowMagnitude = 0.65;
    double minFlowCoherence = 0.28;

    // Временная фильтрация.
    int confirmationFrames = 4;
    int maxMissedFrames = 6;

    // Подавление глобального движения/дрожания камеры.
    double maxForegroundPercent = 42.0;

    bool showForegroundMask = false;
    QVector<QRectF> ignoredZones;
};

Q_DECLARE_METATYPE(DetectionConfig)
