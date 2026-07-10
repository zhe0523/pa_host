#include "TiRawImage.h"

#include <QFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int kHeaderSize = 16;
constexpr char kMagic[] = "TiRayRaw";
constexpr double kAutoWindowTailPercent = 0.006;
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

int TiRawImage::autoWindowCenter() const {
    return autoWindowCenter_;
}

int TiRawImage::autoWindowWidth() const {
    return autoWindowWidth_;
}

int TiRawImage::autoWindowLow() const {
    return autoWindowLow_;
}

int TiRawImage::autoWindowHigh() const {
    return autoWindowHigh_;
}

bool TiRawImage::pixelValue(int x, int y, quint16* value) const {
    if (!isValid() || x < 0 || y < 0 || x >= width_ || y >= height_) {
        return false;
    }
    if (value != nullptr) {
        *value = pixels_.at(y * width_ + x);
    }
    return true;
}

bool TiRawImage::roiStats(const QRect& rect, RoiStats* stats) const {
    if (!isValid() || stats == nullptr) {
        return false;
    }

    const QRect imageRect(0, 0, width_, height_);
    const QRect roi = rect.normalized().intersected(imageRect);
    if (roi.isEmpty()) {
        return false;
    }

    RoiStats result;
    result.rect = roi;
    result.pixelCount = roi.width() * roi.height();
    result.min = std::numeric_limits<quint16>::max();
    result.max = std::numeric_limits<quint16>::min();

    double sum = 0.0;
    double sumSquares = 0.0;
    QVector<double> rowMeans;
    rowMeans.reserve(roi.height());

    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        double rowSum = 0.0;
        const int row = y * width_;
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const quint16 value = pixels_.at(row + x);
            result.min = std::min(result.min, value);
            result.max = std::max(result.max, value);
            sum += value;
            sumSquares += static_cast<double>(value) * value;
            rowSum += value;
        }
        rowMeans.push_back(rowSum / roi.width());
    }

    result.mean = sum / result.pixelCount;
    const double variance = std::max(0.0, sumSquares / result.pixelCount - result.mean * result.mean);
    result.stddev = std::sqrt(variance);
    result.noiseLevel = result.stddev;

    if (rowMeans.size() > 1) {
        double diffSum = 0.0;
        for (int i = 1; i < rowMeans.size(); ++i) {
            diffSum += std::abs(rowMeans.at(i) - rowMeans.at(i - 1));
        }
        result.rowNoise = diffSum / (rowMeans.size() - 1);

        double rowMeanSum = 0.0;
        for (const double rowMean : rowMeans) {
            rowMeanSum += rowMean;
        }
        const double rowMeanAverage = rowMeanSum / rowMeans.size();
        double rowVarianceSum = 0.0;
        for (const double rowMean : rowMeans) {
            const double delta = rowMean - rowMeanAverage;
            rowVarianceSum += delta * delta;
        }
        result.rowNoiseStddev = std::sqrt(rowVarianceSum / rowMeans.size());
        if (result.rowNoiseStddev > 0.0) {
            result.rowNoiseRatio = result.rowNoise / result.rowNoiseStddev;
        }
    }

    *stats = result;
    return true;
}

QImage TiRawImage::toDisplayImage(bool autoWindow, int windowCenter, int windowWidth) const {
    if (!isValid()) {
        return {};
    }

    int low = 0;
    int high = 65535;

    if (autoWindow) {
        low = autoWindowLow_;
        high = autoWindowHigh_;
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
    updateAutoWindowLevel();
}

void TiRawImage::updateAutoWindowLevel() {
    if (pixels_.isEmpty()) {
        autoWindowLow_ = 0;
        autoWindowHigh_ = 65535;
        autoWindowCenter_ = 32767;
        autoWindowWidth_ = 65535;
        return;
    }

    QVector<int> histogram(65536);
    for (const quint16 value : pixels_) {
        ++histogram[value];
    }

    const int total = pixels_.size();
    const int lowRank = static_cast<int>(std::round(kAutoWindowTailPercent * (total - 1)));
    const int highRank = static_cast<int>(std::round((1.0 - kAutoWindowTailPercent) * (total - 1)));

    auto valueAtRank = [&histogram](int rank) {
        int cumulative = 0;
        for (int value = 0; value < histogram.size(); ++value) {
            cumulative += histogram.at(value);
            if (cumulative > rank) {
                return value;
            }
        }
        return histogram.size() - 1;
    };

    autoWindowLow_ = valueAtRank(lowRank);
    autoWindowHigh_ = valueAtRank(highRank);
    if (autoWindowHigh_ <= autoWindowLow_) {
        if (autoWindowLow_ >= 65535) {
            autoWindowLow_ = 65534;
            autoWindowHigh_ = 65535;
        } else {
            autoWindowHigh_ = autoWindowLow_ + 1;
        }
    }
    autoWindowCenter_ = (autoWindowLow_ + autoWindowHigh_) / 2;
    autoWindowWidth_ = autoWindowHigh_ - autoWindowLow_;
}
