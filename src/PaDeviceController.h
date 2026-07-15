#pragma once

#include <QObject>
#include <QTimer>

#include <optional>

#include "PaProtocol.h"

class ILineTransport;

enum class PaDeviceState {
    Disconnected,
    Ready,
    Busy,
    Error
};

struct PaDeviceStatus {
    bool valid = false;
    quint32 paFlags = 0;
    quint32 communicationFlags = 0;
    quint32 resetFlags = 0;
    int writeState = -1;
    int writeEnd = -1;
    int correctionState = -1;
    int correctionEnd = -1;
    QString model;
    QString serialNumber;
    QString armVersion;
    QString fpgaVersion;
    QString rawLine;
};

/*
 * PA 设备控制层：管理串口连接、单条在途命令、响应解析和超时。
 * 界面只消费结构化状态，不直接处理 ASCII 行或 QSerialPort。
 */
class PaDeviceController final : public QObject {
    Q_OBJECT

public:
    explicit PaDeviceController(ILineTransport* transport, QObject* parent = nullptr);

    bool connectDevice(const QString& portName, int baudRate, QString* errorMessage = nullptr);
    void disconnectDevice();
    bool sendCommand(PaProtocol::Command command, QString* errorMessage = nullptr);

    bool isConnected() const;
    bool hasPendingCommand() const;
    PaDeviceState state() const;
    int commandTimeoutMs() const;
    void setCommandTimeoutMs(int timeoutMs);

signals:
    void stateChanged(PaDeviceState state);
    void deviceStatusChanged(const PaDeviceStatus& status);
    void commandFinished(PaProtocol::Command command, bool success, const QString& detail);
    void lineTransmitted(const QString& line);
    void lineReceived(const QString& line);
    void errorOccurred(const QString& message);

private:
    bool sendImmediateCommand(PaProtocol::Command command, QString* errorMessage);
    void handleTransportConnectionChanged(bool connected);
    void handleTransportError(const QString& message);
    void handleLineReceived(const QString& line);
    void handleCommandTimeout();
    void setState(PaDeviceState state);
    void finishPendingCommand(bool success, const QString& detail);
    bool responseMatchesPendingCommand(const PaProtocol::Response& response) const;
    bool parseDeviceStatus(
        const PaProtocol::Response& response,
        PaDeviceStatus* status,
        QString* errorMessage) const;
    bool rejectOperation(const QString& message, QString* errorMessage);

    ILineTransport* transport_ = nullptr;
    QTimer commandTimer_;
    std::optional<PaProtocol::Command> pendingCommand_;
    PaDeviceState state_ = PaDeviceState::Disconnected;
    int commandTimeoutMs_ = 5000;
};

Q_DECLARE_METATYPE(PaDeviceState)
Q_DECLARE_METATYPE(PaDeviceStatus)
