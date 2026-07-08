#include "ImageView.h"

#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent) {
    setScene(&scene_);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFrameShape(QFrame::NoFrame);
}

void ImageView::setImage(const QImage& image) {
    image_ = image;
    scene_.clear();
    pixmapItem_ = nullptr;

    if (!image_.isNull()) {
        pixmapItem_ = scene_.addPixmap(QPixmap::fromImage(image_));
        scene_.setSceneRect(pixmapItem_->boundingRect());
        resetView();
    }
}

bool ImageView::hasImage() const {
    return !image_.isNull();
}

int ImageView::zoomPercent() const {
    return static_cast<int>(zoom_ * 100.0 + 0.5);
}

void ImageView::zoomIn() {
    zoom_ = std::min<qreal>(zoom_ * 1.25, 16.0);
    applyTransform();
}

void ImageView::zoomOut() {
    zoom_ = std::max<qreal>(zoom_ / 1.25, 0.02);
    applyTransform();
}

void ImageView::fitToWindow() {
    if (pixmapItem_ == nullptr) {
        return;
    }
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
    zoom_ = transform().m11();
    updateZoomLabel();
}

void ImageView::resetView() {
    zoom_ = 1.0;
    rotation_ = 0;
    flipH_ = false;
    flipV_ = false;
    applyTransform();
}

void ImageView::rotateLeft() {
    rotation_ = (rotation_ + 270) % 360;
    applyTransform();
}

void ImageView::rotateRight() {
    rotation_ = (rotation_ + 90) % 360;
    applyTransform();
}

void ImageView::flipHorizontal() {
    flipH_ = !flipH_;
    applyTransform();
}

void ImageView::flipVertical() {
    flipV_ = !flipV_;
    applyTransform();
}

bool ImageView::savePng(const QString& path) {
    if (image_.isNull()) {
        return false;
    }
    return image_.save(path, "PNG");
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if ((event->modifiers() & Qt::ControlModifier) == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
    event->accept();
}

void ImageView::drawBackground(QPainter* painter, const QRectF& rect) {
    /*
     * 模仿旧 Windows 上位机的透明棋盘背景。
     * 这只是图像查看辅助，不代表图像实际存在透明通道。
     */
    constexpr int cell = 18;
    const int left = static_cast<int>(std::floor(rect.left() / cell)) * cell;
    const int top = static_cast<int>(std::floor(rect.top() / cell)) * cell;

    painter->fillRect(rect, QColor(248, 248, 248));
    for (int y = top; y < rect.bottom(); y += cell) {
        for (int x = left; x < rect.right(); x += cell) {
            const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
            painter->fillRect(QRectF(x, y, cell, cell), dark ? QColor(224, 224, 224) : QColor(255, 255, 255));
        }
    }
}

void ImageView::applyTransform() {
    QTransform t;
    t.scale(zoom_ * (flipH_ ? -1.0 : 1.0), zoom_ * (flipV_ ? -1.0 : 1.0));
    t.rotate(rotation_);
    setTransform(t);
    updateZoomLabel();
}

void ImageView::updateZoomLabel() {
    emit zoomChanged(zoomPercent());
}
