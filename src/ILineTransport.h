#pragma once

#include <QObject>
#include <QString>

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

signals:
    void lineReceived(const QString& line);
    void errorOccurred(const QString& message);
    void connectionChanged(bool connected);
};
