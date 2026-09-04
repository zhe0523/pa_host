#include "PaDeviceController.h"

#include "ILineTransport.h"

#include <initializer_list>
#include <limits>

namespace {
bool readUnsignedField(
    const QMap<QString, QString>& fields,
    const QString& key,
    quint32* value) {
    const auto it = fields.constFind(key);
    if (it == fields.cend()) {
        return false;
    }
    bool ok = false;
    const qulonglong parsed = it.value().toULongLong(&ok, 0);
    if (!ok || parsed > std::numeric_limits<quint32>::max()) {
        return false;
    }
    *value = static_cast<quint32>(parsed);
    return true;
}

bool readIntField(
    const QMap<QString, QString>& fields,
    const QString& key,
    int* value) {
    const auto it = fields.constFind(key);
    if (it == fields.cend()) {
        return false;
    }
    bool ok = false;
    const int parsed = it.value().toInt(&ok, 0);
    if (!ok) {
        return false;
    }
    *value = parsed;
    return true;
}

bool readUnsignedFieldAny(
    const QMap<QString, QString>& fields,
    std::initializer_list<QString> keys,
    quint32* value,
    bool required = true) {
    for (const QString& key : keys) {
        if (readUnsignedField(fields, key, value)) {
            return true;
        }
    }
    if (!required && value != nullptr) {
        *value = 0;
        return true;
    }
    return false;
}
}

PaDeviceController::PaDeviceController(ILineTransport* transport, QObject* parent)
    : QObject(parent)
    , transport_(transport) {
    qRegisterMetaType<PaDeviceState>("PaDeviceState");
    qRegisterMetaType<PaDeviceStatus>("PaDeviceStatus");
    qRegisterMetaType<PaProtocol::Command>("PaProtocol::Command");

    commandTimer_.setSingleShot(true);
    connect(&commandTimer_, &QTimer::timeout, this, &PaDeviceController::handleCommandTimeout);
    if (transport_ != nullptr) {
        connect(transport_, &ILineTransport::connectionChanged,
            this, &PaDeviceController::handleTransportConnectionChanged);
        connect(transport_, &ILineTransport::errorOccurred,
            this, &PaDeviceController::handleTransportError);
        connect(transport_, &ILineTransport::lineReceived,
            this, &PaDeviceController::handleLineReceived);
        state_ = transport_->isOpen() ? PaDeviceState::Ready : PaDeviceState::Disconnected;
    }
}

bool PaDeviceController::connectDevice(
    const QString& portName,
    int baudRate,
    QString* errorMessage) {
    if (transport_ == nullptr) {
        return rejectOperation(QStringLiteral("控制传输未配置"), errorMessage);
    }
    if (hasPendingCommand()) {
        return rejectOperation(QStringLiteral("设备正在执行命令，不能重新连接"), errorMessage);
    }
    if (transport_->isOpen()) {
        setState(PaDeviceState::Ready);
        return true;
    }

    QString transportError;
    if (!transport_->open(portName, baudRate, &transportError)) {
        const QString message = transportError.isEmpty()
            ? QStringLiteral("串口打开失败")
            : transportError;
        setState(PaDeviceState::Error);
        emit errorOccurred(message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    }

    // 正式串口和测试传输都应发 connectionChanged；这里保留兜底状态同步。
    setState(PaDeviceState::Ready);
    return true;
}

void PaDeviceController::disconnectDevice() {
    if (pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("设备连接已断开，命令取消"));
    }
    if (transport_ != nullptr) {
        transport_->close();
    }
    setState(PaDeviceState::Disconnected);
}

bool PaDeviceController::sendCommand(
    PaProtocol::Command command,
    QString* errorMessage) {
    if (transport_ == nullptr || !transport_->isOpen()) {
        setState(PaDeviceState::Disconnected);
        return rejectOperation(QStringLiteral("串口未连接"), errorMessage);
    }
    if (command == PaProtocol::Command::StopTransfer
        || command == PaProtocol::Command::StopDynamic) {
        return sendImmediateCommand(command, errorMessage);
    }
    if (pendingCommand_.has_value()) {
        return rejectOperation(QStringLiteral("上一条命令尚未完成"), errorMessage);
    }

    const QString line = PaProtocol::commandText(command);
    pendingCommand_ = command;
    setState(PaDeviceState::Busy);

    QString transportError;
    if (!transport_->sendLine(line, &transportError)) {
        pendingCommand_.reset();
        const QString message = transportError.isEmpty()
            ? QStringLiteral("命令发送失败")
            : transportError;
        setState(PaDeviceState::Error);
        emit errorOccurred(message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    }

    emit lineTransmitted(line);
    if (pendingCommand_.has_value()) {
        commandTimer_.start(commandTimeoutMs_);
    }
    return true;
}

bool PaDeviceController::sendImmediateCommand(
    PaProtocol::Command command,
    QString* errorMessage) {
    // 停止类命令只负责尽快写到 ARM，不等待响应，避免停止入口被在途命令卡住。
    commandTimer_.stop();
    pendingCommand_.reset();
    setState(PaDeviceState::Busy);

    const QString line = PaProtocol::commandText(command);
    QString transportError;
    if (!transport_->sendLine(line, &transportError)) {
        const QString message = transportError.isEmpty()
            ? QStringLiteral("即时命令发送失败")
            : transportError;
        setState(PaDeviceState::Error);
        emit errorOccurred(message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    }

    emit lineTransmitted(line);
    setState(PaDeviceState::Ready);
    emit commandFinished(command, true, QStringLiteral("命令已发送，无需等待响应"));
    return true;
}

bool PaDeviceController::isConnected() const {
    return transport_ != nullptr && transport_->isOpen();
}

bool PaDeviceController::hasPendingCommand() const {
    return pendingCommand_.has_value();
}

PaDeviceState PaDeviceController::state() const {
    return state_;
}

int PaDeviceController::commandTimeoutMs() const {
    return commandTimeoutMs_;
}

void PaDeviceController::setCommandTimeoutMs(int timeoutMs) {
    commandTimeoutMs_ = qMax(1, timeoutMs);
    if (commandTimer_.isActive()) {
        commandTimer_.start(commandTimeoutMs_);
    }
}

void PaDeviceController::handleTransportConnectionChanged(bool connected) {
    if (!connected && pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("设备连接意外断开"));
    }
    setState(connected ? PaDeviceState::Ready : PaDeviceState::Disconnected);
}

void PaDeviceController::handleTransportError(const QString& message) {
    if (pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("控制传输错误: %1").arg(message));
    }
    setState(isConnected() ? PaDeviceState::Error : PaDeviceState::Disconnected);
    emit errorOccurred(message);
}

void PaDeviceController::handleLineReceived(const QString& line) {
    emit lineReceived(line);
    const PaProtocol::Response response = PaProtocol::parseResponse(line);
    if (!response.ok && !response.error) {
        emit errorOccurred(QStringLiteral("无法识别设备响应: %1").arg(line));
        return;
    }

    bool payloadValid = true;
    QString payloadError;
    if (response.ok && response.keyword == QStringLiteral("STATUS")) {
        PaDeviceStatus status;
        payloadValid = parseDeviceStatus(response, &status, &payloadError);
        if (payloadValid) {
            emit deviceStatusChanged(status);
        } else {
            emit errorOccurred(payloadError);
        }
    }

    if (!pendingCommand_.has_value()) {
        if (response.ok && payloadValid && isConnected()) {
            setState(PaDeviceState::Ready);
        } else if (response.error || !payloadValid) {
            setState(PaDeviceState::Error);
            if (response.error) {
                emit errorOccurred(QStringLiteral("设备返回错误: %1").arg(response.rawLine));
            }
        }
        return;
    }

    if (!responseMatchesPendingCommand(response)) {
        return;
    }

    const bool success = response.ok && payloadValid;
    const QString detail = payloadValid ? response.rawLine : payloadError;
    finishPendingCommand(success, detail);
}

void PaDeviceController::handleCommandTimeout() {
    if (!pendingCommand_.has_value()) {
        return;
    }
    const QString commandName = PaProtocol::commandName(pendingCommand_.value());
    finishPendingCommand(false, QStringLiteral("等待“%1”响应超时").arg(commandName));
}

void PaDeviceController::setState(PaDeviceState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void PaDeviceController::finishPendingCommand(bool success, const QString& detail) {
    if (!pendingCommand_.has_value()) {
        return;
    }
    commandTimer_.stop();
    const PaProtocol::Command command = pendingCommand_.value();
    pendingCommand_.reset();
    setState(success
            ? (isConnected() ? PaDeviceState::Ready : PaDeviceState::Disconnected)
            : PaDeviceState::Error);
    emit commandFinished(command, success, detail);
}

bool PaDeviceController::responseMatchesPendingCommand(
    const PaProtocol::Response& response) const {
    if (!pendingCommand_.has_value() || response.error || response.keyword.isEmpty()) {
        return true;
    }

    // PONG 和 STATUS 可能迟到，必须与在途命令严格对应。
    switch (pendingCommand_.value()) {
    case PaProtocol::Command::Ping:
        return response.keyword == QStringLiteral("PONG");
    case PaProtocol::Command::Status:
        return response.keyword == QStringLiteral("STATUS");
    default:
        return response.keyword != QStringLiteral("PONG")
            && response.keyword != QStringLiteral("STATUS");
    }
}

bool PaDeviceController::parseDeviceStatus(
    const PaProtocol::Response& response,
    PaDeviceStatus* status,
    QString* errorMessage) const {
    if (status == nullptr) {
        return false;
    }

    PaDeviceStatus parsed;
    parsed.rawLine = response.rawLine;
    parsed.model = response.kv.value(QStringLiteral("model")).trimmed();
    parsed.serialNumber = response.kv.value(QStringLiteral("serial")).trimmed();
    parsed.armVersion = response.kv.value(QStringLiteral("arm_version")).trimmed();
    parsed.fpgaVersion = response.kv.value(QStringLiteral("fpga_version")).trimmed();
    const bool valid = readUnsignedFieldAny(response.kv, {QStringLiteral("int_vector"), QStringLiteral("int")}, &parsed.interruptVector, false)
        && readUnsignedFieldAny(response.kv, {QStringLiteral("pa_version"), QStringLiteral("pa")}, &parsed.paVersion)
        && readUnsignedFieldAny(response.kv, {QStringLiteral("com_version"), QStringLiteral("com")}, &parsed.communicationVersion)
        && readUnsignedFieldAny(response.kv, {QStringLiteral("rst_state"), QStringLiteral("rst")}, &parsed.resetState)
        && readIntField(response.kv, QStringLiteral("wr_state"), &parsed.writeState)
        && readIntField(response.kv, QStringLiteral("wr_end"), &parsed.writeEnd)
        && readIntField(response.kv, QStringLiteral("corr_state"), &parsed.correctionState)
        && readIntField(response.kv, QStringLiteral("corr_end"), &parsed.correctionEnd);
    if (!valid) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("STATUS 响应缺少字段或数值无效: %1").arg(response.rawLine);
        }
        return false;
    }

    parsed.valid = true;
    *status = parsed;
    return true;
}

bool PaDeviceController::rejectOperation(const QString& message, QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    emit errorOccurred(message);
    return false;
}
