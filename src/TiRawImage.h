#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <QtGlobal>

/*
 * TiRayRaw 图像文件读取。
 *
 * 从 Windows 上位机样例文件观察到的格式：
 *   0x00: "TiRayRaw" 8 字节魔数
 *   0x08: uint16 版本，样例为 1
 *   0x0A: uint16 每像素字节数，样例为 2
 *   0x0C: uint16 高度
 *   0x0E: uint16 宽度
 *   0x10: uint16 little-endian 灰度像素数据
 *
 * 注意：这是根据当前样例反推的格式，后续拿到正式 SDK 文档后需要再校对。
 */
class TiRawImage {
public:
    bool load(const QString& path, QString* errorMessage);
    bool isValid() const;

    QString path() const;
    quint16 version() const;
    quint16 bytesPerPixel() const;
    int width() const;
    int height() const;
    quint16 minValue() const;
    quint16 maxValue() const;

    QImage toDisplayImage(bool autoWindow, int windowCenter, int windowWidth) const;

private:
    static quint16 readLe16(const uchar* p);
    void updateRange();

    QString path_;
    quint16 version_ = 0;
    quint16 bytesPerPixel_ = 0;
    int width_ = 0;
    int height_ = 0;
    quint16 minValue_ = 0;
    quint16 maxValue_ = 0;
    QVector<quint16> pixels_;
};

