#pragma once

#include <QObject>
#include <QString>

#include "PaBinaryProtocol.h"

/*
 * ASCII 行传输边界。
 * 上层控制器只依赖完整文本行，不感知 QSerialPort，测试可注入内存模拟传输。
 */
class ILineTransport : public QObject {
    Q_OBJECT

public:
    explicit ILineTransport(QObject* parent = nullptr);
    ~ILineTransport() override = default;

    virtual bool open(const QString& portName, int baudRate, QString* errorMessage) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString portName() const = 0;
    virtual bool sendLine(const QString& line, QString* errorMessage) = 0;

    /* 二进制模式的默认实现；ASCII 测试传输无需实现即可继续工作。 */
    virtual bool sendBinaryFrame(const PaBinaryProtocol::Frame& frame, QString* errorMessage);
    virtual void setBinaryMode(bool enabled);

signals:
    void lineReceived(const QString& line);
    void errorOccurred(const QString& message);
    void connectionChanged(bool connected);
    void binaryFrameReceived(const PaBinaryProtocol::Frame& frame);
};
