#include "ImageSource.h"

#include <QFileInfo>

#include <algorithm>

IImageSource::IImageSource(QObject* parent)
    : QObject(parent) {
}

LocalReplaySource::LocalReplaySource(QObject* parent)
    : IImageSource(parent) {
    qRegisterMetaType<ImageFrame>("ImageFrame");
    initialDeliveryTimer_.setSingleShot(true);
    connect(&initialDeliveryTimer_, &QTimer::timeout, this, [this]() {
        const quint64 generation = runGeneration_;
        deliverNext();
        if (running_ && runGeneration_ == generation) {
            timer_.start(intervalMs_);
        }
    });
    timer_.setSingleShot(false);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &LocalReplaySource::deliverNext);
}

void LocalReplaySource::setPlaylist(const QStringList& paths) {
    stop();
    playlist_ = paths;
    cachedFrames_.clear();
    nextIndex_ = 0;
    stats_ = {};
}

void LocalReplaySource::setIntervalMs(int intervalMs) {
    intervalMs_ = std::max(1, intervalMs);
    if (timer_.isActive()) {
        timer_.setInterval(intervalMs_);
    }
}

void LocalReplaySource::setLoopEnabled(bool enabled) {
    loopEnabled_ = enabled;
}

bool LocalReplaySource::start(QString* errorMessage) {
    if (running_) {
        return true;
    }
    if (playlist_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未选择回放图像");
        }
        return false;
    }

    cachedFrames_.clear();
    nextIndex_ = 0;
    stats_ = {};
    cachedFrames_.reserve(playlist_.size());
    for (const QString& path : playlist_) {
        TiRawImage image;
        QString error;
        if (!image.load(path, &error)) {
            ++stats_.failedFrames;
            emit sourceError(QStringLiteral("回放图像预加载失败: %1, %2").arg(path, error));
            continue;
        }

        ImageFrame frame;
        frame.image = std::move(image);
        frame.sourceName = QFileInfo(path).fileName();
        cachedFrames_.push_back(std::move(frame));
    }
    if (cachedFrames_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("所选回放图像均无法加载");
        }
        return false;
    }

    ++runGeneration_;
    running_ = true;
    emit runningChanged(true);
    initialDeliveryTimer_.start(0);
    return true;
}

void LocalReplaySource::stop() {
    initialDeliveryTimer_.stop();
    timer_.stop();
    if (!running_) {
        return;
    }
    ++runGeneration_;
    running_ = false;
    emit runningChanged(false);
}

bool LocalReplaySource::isRunning() const {
    return running_;
}

ImageSourceStats LocalReplaySource::stats() const {
    return stats_;
}

void LocalReplaySource::deliverNext() {
    if (!running_ || cachedFrames_.isEmpty()) {
        return;
    }

    ImageFrame frame = cachedFrames_.at(nextIndex_++);
    bool stopAfterFrame = false;
    if (nextIndex_ >= cachedFrames_.size()) {
        if (loopEnabled_) {
            nextIndex_ = 0;
        } else {
            stopAfterFrame = true;
        }
    }

    frame.sequence = stats_.deliveredFrames;
    frame.receivedAt = QDateTime::currentDateTimeUtc();
    ++stats_.deliveredFrames;
    emit frameReady(frame);

    if (stopAfterFrame && running_) {
        initialDeliveryTimer_.stop();
        timer_.stop();
        ++runGeneration_;
        running_ = false;
        emit runningChanged(false);
    }
}
