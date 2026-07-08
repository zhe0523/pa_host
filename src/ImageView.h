#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>

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

    void setImage(const QImage& image);
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

protected:
    void wheelEvent(QWheelEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void applyTransform();
    void updateZoomLabel();

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QImage image_;
    qreal zoom_ = 1.0;
    int rotation_ = 0;
    bool flipH_ = false;
    bool flipV_ = false;
};

