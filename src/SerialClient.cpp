#include "SerialClient.h"

#include <QSerialPortInfo>

SerialClient::SerialClient(QObject* parent)
    : ILineTransport(parent) {
    connect(&serial_, &QSerialPort::readyRead, this, &SerialClient::handleReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this, &SerialClient::handleSerialError);
}

bool SerialClient::open(const QString& portName, int baudRate, QString* errorMessage) {
    if (serial_.isOpen()) {
        serial_.close();
    }

    rxBuffer_.clear();
    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        if (errorMessage != nullptr) {
            *errorMessage = serial_.errorString();
        }
        emit connectionChanged(false);
        return false;
    }

    emit connectionChanged(true);
    return true;
}

void SerialClient::close() {
    if (serial_.isOpen()) {
        serial_.close();
    }
    rxBuffer_.clear();
    emit connectionChanged(false);
}

bool SerialClient::isOpen() const {
    return serial_.isOpen();
}

QString SerialClient::portName() const {
    return serial_.portName();
}

bool SerialClient::sendLine(const QString& line, QString* errorMessage) {
    if (!serial_.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("串口未打开");
        }
        return false;
    }

    QByteArray data = line.toUtf8();
    if (!data.endsWith('\n')) {
        data.append("\r\n");
    }

    const qint64 written = serial_.write(data);
    if (written != data.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = serial_.errorString();
        }
        return false;
    }

    return true;
}

void SerialClient::handleReadyRead() {
    rxBuffer_.append(serial_.readAll());

    while (true) {
        const int lf = rxBuffer_.indexOf('\n');
        if (lf < 0) {
            break;
        }

        QByteArray line = rxBuffer_.left(lf);
        rxBuffer_.remove(0, lf + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        emit lineReceived(QString::fromUtf8(line));
    }
}

void SerialClient::handleSerialError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) {
        return;
    }
    emit errorOccurred(serial_.errorString());
}
