#include "VideoWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral("background: #11151a;"));
}

void VideoWidget::setFrame(const QImage &frame)
{
    frame_ = frame;
    update();
}

void VideoWidget::setIgnoredZones(const QVector<QRectF> &zones)
{
    ignoredZones_ = zones;
    update();
}

void VideoWidget::setZoneDrawingEnabled(bool enabled)
{
    zoneDrawingEnabled_ = enabled;
    dragging_ = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

QSize VideoWidget::minimumSizeHint() const
{
    return {640, 360};
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 21, 26));

    if (frame_.isNull()) {
        painter.setPen(QColor(150, 160, 170));
        painter.drawText(rect(),
                         Qt::AlignCenter,
                         QStringLiteral("Видео не запущено"));
        return;
    }

    const QRect destination = targetRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(destination, frame_);

    painter.setPen(QPen(QColor(255, 90, 70), 2, Qt::DashLine));
    painter.setBrush(QColor(255, 70, 50, 55));
    for (const QRectF &zone : ignoredZones_) {
        const QRectF displayed(
            destination.left() + zone.left() * destination.width(),
            destination.top() + zone.top() * destination.height(),
            zone.width() * destination.width(),
            zone.height() * destination.height());
        painter.drawRect(displayed);
    }

    if (dragging_) {
        painter.setPen(QPen(QColor(255, 185, 40), 2));
        painter.setBrush(QColor(255, 185, 40, 45));
        painter.drawRect(QRect(dragStart_, dragEnd_).normalized());
    }

    if (zoneDrawingEnabled_) {
        painter.setPen(QColor(245, 205, 90));
        painter.drawText(destination.adjusted(12, 10, -12, -10),
                         Qt::AlignTop | Qt::AlignLeft,
                         QStringLiteral(
                             "Выделите мышью область, которую нужно игнорировать"));
    }
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (zoneDrawingEnabled_ && event->button() == Qt::LeftButton &&
        targetRect().contains(event->position().toPoint())) {
        dragging_ = true;
        dragStart_ = event->position().toPoint();
        dragEnd_ = dragStart_;
        update();
    }
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        const QRect videoRect = targetRect();
        dragEnd_.setX(
            std::clamp(event->position().toPoint().x(),
                       videoRect.left(),
                       videoRect.right()));
        dragEnd_.setY(
            std::clamp(event->position().toPoint().y(),
                       videoRect.top(),
                       videoRect.bottom()));
        update();
    }
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        return;
    }

    dragging_ = false;
    const QRect selection = QRect(dragStart_, dragEnd_).normalized();
    const QRectF zone = normalizedRect(selection);
    if (zone.width() >= 0.01 && zone.height() >= 0.01) {
        emit ignoredZoneCreated(zone);
    }
    update();
}

QRect VideoWidget::targetRect() const
{
    if (frame_.isNull()) {
        return rect();
    }

    QSize scaled = frame_.size();
    scaled.scale(size(), Qt::KeepAspectRatio);
    const int left = (width() - scaled.width()) / 2;
    const int top = (height() - scaled.height()) / 2;
    return {left, top, scaled.width(), scaled.height()};
}

QRectF VideoWidget::normalizedRect(const QRect &widgetRect) const
{
    const QRect videoRect = targetRect();
    const QRect bounded = widgetRect.intersected(videoRect);
    if (bounded.isEmpty() || videoRect.width() <= 0 ||
        videoRect.height() <= 0) {
        return {};
    }

    return {
        static_cast<double>(bounded.left() - videoRect.left()) /
            videoRect.width(),
        static_cast<double>(bounded.top() - videoRect.top()) /
            videoRect.height(),
        static_cast<double>(bounded.width()) / videoRect.width(),
        static_cast<double>(bounded.height()) / videoRect.height()};
}
