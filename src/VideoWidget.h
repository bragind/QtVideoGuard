#pragma once

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <QWidget>

class VideoWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void setIgnoredZones(const QVector<QRectF> &zones);
    void setZoneDrawingEnabled(bool enabled);

    QSize minimumSizeHint() const override;

signals:
    void ignoredZoneCreated(const QRectF &normalizedZone);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect targetRect() const;
    QRectF normalizedRect(const QRect &widgetRect) const;

    QImage frame_;
    QVector<QRectF> ignoredZones_;
    bool zoneDrawingEnabled_ = false;
    bool dragging_ = false;
    QPoint dragStart_;
    QPoint dragEnd_;
};
