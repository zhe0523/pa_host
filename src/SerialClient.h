#pragma once

#include <QObject>
#include <QSerialPort>
#include <QString>

/*
 * RS422 串口客户端。
 *
 * RS422 在 Linux 上通常表现为普通 tty 设备，例如 /dev/ttyS1、/dev/ttyUSB0。
 * 这个类只处理串口打开、关闭、按行收发；协议含义交给 PaProtocol。
 */
class SerialClient : public QObject {
    Q_OBJECT

public:
    explicit SerialClient(QObject* parent = nullptr);

    bool open(const QString& portName, int baudRate, QString* errorMessage);
    void close();
    bool isOpen() const;
    QString portName() const;

    bool sendLine(const QString& line, QString* errorMessage);

signals:
    void lineReceived(const QString& line);
    void errorOccurred(const QString& message);
    void connectionChanged(bool connected);

private slots:
    void handleReadyRead();
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort serial_;
    QByteArray rxBuffer_; // 未凑齐一整行的接收缓存
};

