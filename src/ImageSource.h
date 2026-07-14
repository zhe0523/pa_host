#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "TiRawImage.h"

struct ImageFrame {
    TiRawImage image;
    QString sourceName;
    quint64 sequence = 0;
    QDateTime receivedAt;
};

struct ImageSourceStats {
    quint64 deliveredFrames = 0;
    quint64 failedFrames = 0;
};

class IImageSource : public QObject {
    Q_OBJECT

public:
    explicit IImageSource(QObject* parent = nullptr);
    ~IImageSource() override = default;

    virtual bool start(QString* errorMessage) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual ImageSourceStats stats() const = 0;

signals:
    void frameReady(const ImageFrame& frame);
    void sourceError(const QString& message);
    void runningChanged(bool running);
};

/*
 * Local replay is the hardware-free implementation of the image source
 * contract. It is used to exercise the same display path that a future PCIe
 * source will use.
 */
class LocalReplaySource final : public IImageSource {
public:
    explicit LocalReplaySource(QObject* parent = nullptr);

    void setPlaylist(const QStringList& paths);
    void setIntervalMs(int intervalMs);
    void setLoopEnabled(bool enabled);

    bool start(QString* errorMessage) override;
    void stop() override;
    bool isRunning() const override;
    ImageSourceStats stats() const override;

private:
    void deliverNext();

    QStringList playlist_;
    QTimer timer_;
    int intervalMs_ = 33;
    int nextIndex_ = 0;
    bool loopEnabled_ = true;
    bool running_ = false;
    ImageSourceStats stats_;
};

Q_DECLARE_METATYPE(ImageFrame)
