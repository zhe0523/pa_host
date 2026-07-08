#include "TiRawImage.h"

#include <QFile>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr int kHeaderSize = 16;
constexpr char kMagic[] = "TiRayRaw";
}

bool TiRawImage::load(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QByteArray data = file.readAll();
    if (data.size() < kHeaderSize) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("文件太小，不是有效 TiRayRaw 图像");
        }
        return false;
    }

    if (QByteArray::fromRawData(data.constData(), 8) != QByteArray(kMagic, 8)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("文件魔数不是 TiRayRaw");
        }
        return false;
    }

    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
    const quint16 version = readLe16(bytes + 8);
    const quint16 bytesPerPixel = readLe16(bytes + 10);
    const quint16 height = readLe16(bytes + 12);
    const quint16 width = readLe16(bytes + 14);

    if (bytesPerPixel != 2 || width == 0 || height == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前只支持 16-bit 灰度 TiRayRaw 图像");
        }
        return false;
    }

    const qint64 expectedSize = kHeaderSize + static_cast<qint64>(width) * height * bytesPerPixel;
    if (data.size() != expectedSize) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("文件大小与头部宽高不匹配");
        }
        return false;
    }

    QVector<quint16> pixels;
    pixels.resize(static_cast<int>(static_cast<qint64>(width) * height));

    const uchar* src = bytes + kHeaderSize;
    for (int i = 0; i < pixels.size(); ++i) {
        pixels[i] = readLe16(src + i * 2);
    }

    path_ = path;
    version_ = version;
    bytesPerPixel_ = bytesPerPixel;
    width_ = width;
    height_ = height;
    pixels_ = std::move(pixels);
    updateRange();

    return true;
}

bool TiRawImage::isValid() const {
    return !pixels_.isEmpty() && width_ > 0 && height_ > 0;
}

QString TiRawImage::path() const {
    return path_;
}

quint16 TiRawImage::version() const {
    return version_;
}

quint16 TiRawImage::bytesPerPixel() const {
    return bytesPerPixel_;
}

int TiRawImage::width() const {
    return width_;
}

int TiRawImage::height() const {
    return height_;
}

quint16 TiRawImage::minValue() const {
    return minValue_;
}

quint16 TiRawImage::maxValue() const {
    return maxValue_;
}

QImage TiRawImage::toDisplayImage(bool autoWindow, int windowCenter, int windowWidth) const {
    if (!isValid()) {
        return {};
    }

    int low = 0;
    int high = 65535;

    if (autoWindow) {
        low = minValue_;
        high = maxValue_;
    } else {
        const int width = std::max(1, windowWidth);
        low = windowCenter - width / 2;
        high = windowCenter + width / 2;
    }

    if (high <= low) {
        high = low + 1;
    }

    QImage image(width_, height_, QImage::Format_Grayscale8);
    for (int y = 0; y < height_; ++y) {
        uchar* dst = image.scanLine(y);
        const int row = y * width_;
        for (int x = 0; x < width_; ++x) {
            const int value = pixels_.at(row + x);
            const int clipped = std::max(low, std::min(high, value));
            dst[x] = static_cast<uchar>((clipped - low) * 255 / (high - low));
        }
    }

    return image;
}

quint16 TiRawImage::readLe16(const uchar* p) {
    return static_cast<quint16>(p[0] | (p[1] << 8));
}

void TiRawImage::updateRange() {
    if (pixels_.isEmpty()) {
        minValue_ = 0;
        maxValue_ = 0;
        return;
    }

    auto range = std::minmax_element(pixels_.begin(), pixels_.end());
    minValue_ = *range.first;
    maxValue_ = *range.second;
}
