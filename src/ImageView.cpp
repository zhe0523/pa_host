#include "ImageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
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
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState);
    setCacheMode(QGraphicsView::CacheBackground);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

void ImageView::setImage(const QImage& image, bool resetViewState) {
    const bool sizeChanged = image_.size() != image.size();
    image_ = image;

    if (image_.isNull()) {
        scene_.clear();
        pixmapItem_ = nullptr;
        roiItem_ = nullptr;
        return;
    }

    if (pixmapItem_ == nullptr || sizeChanged) {
        scene_.clear();
        roiItem_ = nullptr;
        pixmapItem_ = scene_.addPixmap(QPixmap::fromImage(image_));
        pixmapItem_->setTransformationMode(Qt::FastTransformation);
        scene_.setSceneRect(pixmapItem_->boundingRect());
    } else {
        pixmapItem_->setPixmap(QPixmap::fromImage(image_));
    }

    if (resetViewState || sizeChanged) {
        rotation_ = 0;
        flipH_ = false;
        flipV_ = false;
        fitMode_ = true;
    }

    if (fitMode_) {
        fitToWindow();
    } else {
        applyTransform();
    }
}

bool ImageView::hasImage() const {
    return !image_.isNull();
}

int ImageView::zoomPercent() const {
    return static_cast<int>(zoom_ * 100.0 + 0.5);
}

void ImageView::zoomIn() {
    fitMode_ = false;
    zoom_ = std::min<qreal>(zoom_ * 1.25, 32.0);
    applyTransform();
}

void ImageView::zoomOut() {
    fitMode_ = false;
    zoom_ = std::max<qreal>(zoom_ / 1.25, 0.02);
    applyTransform();
}

void ImageView::fitToWindow() {
    if (pixmapItem_ == nullptr) {
        return;
    }

    const QSizeF imageSize = pixmapItem_->boundingRect().size();
    qreal targetWidth = imageSize.width();
    qreal targetHeight = imageSize.height();
    if (rotation_ == 90 || rotation_ == 270) {
        std::swap(targetWidth, targetHeight);
    }

    const QSize viewSize = viewport()->size();
    if (targetWidth <= 0 || targetHeight <= 0 || viewSize.isEmpty()) {
        return;
    }

    constexpr qreal margin = 0.96;
    zoom_ = std::max<qreal>(
        0.02,
        std::min(viewSize.width() / targetWidth, viewSize.height() / targetHeight) * margin);
    fitMode_ = true;
    applyTransform();
    centerOn(pixmapItem_);
}

void ImageView::resetView() {
    zoom_ = 1.0;
    rotation_ = 0;
    flipH_ = false;
    flipV_ = false;
    fitMode_ = false;
    applyTransform();
}

void ImageView::rotateLeft() {
    rotation_ = (rotation_ + 270) % 360;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::rotateRight() {
    rotation_ = (rotation_ + 90) % 360;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::flipHorizontal() {
    flipH_ = !flipH_;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::flipVertical() {
    flipV_ = !flipV_;
    fitMode_ ? fitToWindow() : applyTransform();
}

bool ImageView::savePng(const QString& path) {
    if (image_.isNull()) {
        return false;
    }
    return image_.save(path, "PNG");
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    const bool analysisSelection = (event->modifiers() & Qt::ControlModifier) != 0;
    const bool windowLevelSelection = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (event->button() == Qt::LeftButton && hasImage() && (analysisSelection || windowLevelSelection)) {
        const QPoint imagePoint = imagePointAt(event->pos());
        if (imagePoint.x() >= 0) {
            selectionMode_ = analysisSelection ? SelectionMode::Analysis : SelectionMode::WindowLevel;
            roiStart_ = imagePoint;
            setDragMode(QGraphicsView::NoDrag);
            if (roiItem_ == nullptr) {
                roiItem_ = scene_.addRect(QRectF(), QPen(QColor(255, 230, 0), 1.4), Qt::NoBrush);
                roiItem_->setZValue(10.0);
            }
            roiItem_->setPen(QPen(selectionMode_ == SelectionMode::Analysis ? QColor(255, 125, 0) : QColor(36, 170, 84), 1.4));
            roiItem_->setRect(QRectF(roiStart_, QSizeF(1, 1)));
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && hasImage() && roiItem_ != nullptr) {
        clearRoiOverlay();
        emit roiCleared();
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint imagePoint = imagePointAt(event->pos());
    emit pixelHovered(imagePoint);

    if (selectionMode_ != SelectionMode::None) {
        if (imagePoint.x() >= 0 && roiItem_ != nullptr) {
            roiItem_->setRect(QRectF(roiStart_, imagePoint).normalized());
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (selectionMode_ != SelectionMode::None && event->button() == Qt::LeftButton) {
        const SelectionMode mode = selectionMode_;
        selectionMode_ = SelectionMode::None;
        setDragMode(QGraphicsView::ScrollHandDrag);
        const QPoint imagePoint = imagePointAt(event->pos());
        if (imagePoint.x() >= 0) {
            const QRect imageRect(roiStart_, imagePoint);
            const QRect normalized = imageRect.normalized();
            if (normalized.width() > 1 && normalized.height() > 1) {
                if (mode == SelectionMode::Analysis) {
                    emit analysisRoiSelected(normalized);
                } else if (mode == SelectionMode::WindowLevel) {
                    emit windowLevelRoiSelected(normalized);
                }
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!hasImage()) {
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

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (fitMode_ && pixmapItem_ != nullptr) {
        fitToWindow();
    }
}

void ImageView::drawBackground(QPainter* painter, const QRectF& rect) {
    /*
     * 模仿旧 Windows 上位机的透明棋盘背景。
     * 这只是图像查看辅助，不代表图像实际存在透明通道。
     */
    constexpr int cell = 18;
    QPixmap pattern(cell * 2, cell * 2);
    pattern.fill(QColor(255, 255, 255));

    QPainter patternPainter(&pattern);
    patternPainter.fillRect(0, 0, cell, cell, QColor(224, 224, 224));
    patternPainter.fillRect(cell, cell, cell, cell, QColor(224, 224, 224));

    painter->fillRect(rect, QBrush(pattern));
}

QPoint ImageView::imagePointAt(const QPoint& viewPoint) const {
    if (image_.isNull()) {
        return {-1, -1};
    }

    const QPointF scenePoint = mapToScene(viewPoint);
    const int x = static_cast<int>(std::floor(scenePoint.x()));
    const int y = static_cast<int>(std::floor(scenePoint.y()));
    if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height()) {
        return {-1, -1};
    }
    return {x, y};
}

void ImageView::clearRoiOverlay() {
    if (roiItem_ != nullptr) {
        scene_.removeItem(roiItem_);
        delete roiItem_;
        roiItem_ = nullptr;
    }
    selectionMode_ = SelectionMode::None;
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
