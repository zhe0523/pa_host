#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QRect>

/*
 * 图像显示控件。
 *
 * 当前只负责单帧 8-bit 显示图像的缩放、旋转、翻转和保存。
 * 原始 16-bit 数据由 TiRawImage 保存，窗宽窗位转换后再传入这里。
 */
class ImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image, bool resetViewState = false);
    bool hasImage() const;
    int zoomPercent() const;

public slots:
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void resetView();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    bool savePng(const QString& path);

signals:
    void zoomChanged(int percent);
    void pixelHovered(const QPoint& imagePoint);
    void analysisRoiSelected(const QRect& imageRect);
    void windowLevelRoiSelected(const QRect& imageRect);
    void roiCleared();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    QPoint imagePointAt(const QPoint& viewPoint) const;
    void clearRoiOverlay();
    void applyTransform();
    void updateZoomLabel();

    enum class SelectionMode {
        None,
        Analysis,
        WindowLevel,
    };

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QGraphicsRectItem* roiItem_ = nullptr;
    QImage image_;
    qreal zoom_ = 1.0;
    int rotation_ = 0;
    bool flipH_ = false;
    bool flipV_ = false;
    bool fitMode_ = true;
    SelectionMode selectionMode_ = SelectionMode::None;
    QPoint roiStart_;
};
