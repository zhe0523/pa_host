#include "ImageSource.h"

#include <QFileInfo>

#include <algorithm>

IImageSource::IImageSource(QObject* parent)
    : QObject(parent) {
}

LocalReplaySource::LocalReplaySource(QObject* parent)
    : IImageSource(parent) {
    qRegisterMetaType<ImageFrame>("ImageFrame");
    timer_.setSingleShot(false);
    connect(&timer_, &QTimer::timeout, this, &LocalReplaySource::deliverNext);
}

void LocalReplaySource::setPlaylist(const QStringList& paths) {
    stop();
    playlist_ = paths;
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

    running_ = true;
    timer_.start(intervalMs_);
    emit runningChanged(true);
    deliverNext();
    return true;
}

void LocalReplaySource::stop() {
    if (!running_) {
        return;
    }
    timer_.stop();
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
    if (!running_ || playlist_.isEmpty()) {
        return;
    }

    const QString path = playlist_.at(nextIndex_++);
    if (nextIndex_ >= playlist_.size()) {
        if (loopEnabled_) {
            nextIndex_ = 0;
        } else {
            timer_.stop();
            running_ = false;
        }
    }

    TiRawImage image;
    QString error;
    if (!image.load(path, &error)) {
        ++stats_.failedFrames;
        emit sourceError(QStringLiteral("回放图像失败: %1, %2").arg(path, error));
    } else {
        ImageFrame frame;
        frame.image = std::move(image);
        frame.sourceName = QFileInfo(path).fileName();
        frame.sequence = stats_.deliveredFrames;
        frame.receivedAt = QDateTime::currentDateTimeUtc();
        ++stats_.deliveredFrames;
        emit frameReady(frame);
    }

    if (!running_) {
        emit runningChanged(false);
    }
}
