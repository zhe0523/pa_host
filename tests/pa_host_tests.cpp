#include "AppLogService.h"
#include "AppSettings.h"
#include "FramePresentationController.h"
#include "ILineTransport.h"
#include "ImageAlgorithms.h"
#include "ImageExportService.h"
#include "ImageSession.h"
#include "ImageSource.h"
#include "MtfAnalysis.h"
#include "PaDeviceController.h"
#include "PaProtocol.h"
#include "ReplayPresentationScheduler.h"
#include "TiRawImage.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <iterator>

namespace {
struct TestCase {
    const char* name;
    std::function<bool()> run;
};

class FakeLineTransport final : public ILineTransport {
public:
    explicit FakeLineTransport(QObject* parent = nullptr)
        : ILineTransport(parent) {
    }

    bool open(const QString& portName, int baudRate, QString* errorMessage) override {
        lastPortName = portName;
        lastBaudRate = baudRate;
        if (failOpen) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("模拟打开失败");
            }
            emit connectionChanged(false);
            return false;
        }
        openState = true;
        emit connectionChanged(true);
        return true;
    }

    void close() override {
        openState = false;
        emit connectionChanged(false);
    }

    bool isOpen() const override {
        return openState;
    }

    QString portName() const override {
        return lastPortName;
    }

    bool sendLine(const QString& line, QString* errorMessage) override {
        if (!openState || failSend) {
            if (errorMessage != nullptr) {
                *errorMessage = failSend
                    ? QStringLiteral("模拟发送失败")
                    : QStringLiteral("模拟传输未打开");
            }
            return false;
        }
        sentLines.push_back(line);
        return true;
    }

    void injectLine(const QString& line) {
        emit lineReceived(line);
    }

    void injectError(const QString& message) {
        emit errorOccurred(message);
    }

    bool openState = false;
    bool failOpen = false;
    bool failSend = false;
    QString lastPortName;
    int lastBaudRate = 0;
    QStringList sentLines;
};

void appendLe16(QByteArray* data, quint16 value) {
    data->append(static_cast<char>(value & 0xff));
    data->append(static_cast<char>((value >> 8) & 0xff));
}

bool writeTiraw(
    const QString& path,
    quint16 width,
    quint16 height,
    const QVector<quint16>& pixels,
    const QByteArray& magic = QByteArrayLiteral("TiRayRaw"),
    quint16 bytesPerPixel = 2) {
    QByteArray data;
    data.append(magic.leftJustified(8, '\0', true));
    appendLe16(&data, 1);
    appendLe16(&data, bytesPerPixel);
    appendLe16(&data, height);
    appendLe16(&data, width);
    for (const quint16 pixel : pixels) {
        appendLe16(&data, pixel);
    }

    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        qCritical("检查失败: %s (%s:%d)", expression, file, line);
    }
    return condition;
}

#define CHECK(expression) \
    do { \
        if (!check((expression), #expression, __FILE__, __LINE__)) { \
            return false; \
        } \
    } while (false)

bool fuzzyEqual(double actual, double expected, double tolerance = 1e-6) {
    return std::abs(actual - expected) <= tolerance;
}

bool testProtocolCommands() {
    CHECK(PaProtocol::commandText(PaProtocol::Command::Ping) == QStringLiteral("PING"));
    CHECK(PaProtocol::commandText(PaProtocol::Command::SendImage) == QStringLiteral("SEND_IMAGE"));
    CHECK(PaProtocol::commandName(PaProtocol::Command::SendImage) == QStringLiteral("手动上图"));
    CHECK(PaProtocol::commandNames().size() == 10);
    return true;
}

bool testProtocolResponses() {
    const auto status = PaProtocol::parseResponse(
        QStringLiteral("  OK   STATUS int=0x10 wr_state=2 corr_end=1  \r\n"));
    CHECK(status.ok);
    CHECK(!status.error);
    CHECK(status.keyword == QStringLiteral("STATUS"));
    CHECK(status.rawLine == QStringLiteral("OK   STATUS int=0x10 wr_state=2 corr_end=1"));
    CHECK(status.kv.value(QStringLiteral("int")) == QStringLiteral("0x10"));
    CHECK(status.kv.value(QStringLiteral("wr_state")) == QStringLiteral("2"));
    CHECK(status.kv.value(QStringLiteral("corr_end")) == QStringLiteral("1"));

    const auto error = PaProtocol::parseResponse(QStringLiteral("ERR UNKNOWN reason=bad"));
    CHECK(!error.ok);
    CHECK(error.error);
    CHECK(error.keyword == QStringLiteral("UNKNOWN"));
    CHECK(error.kv.value(QStringLiteral("reason")) == QStringLiteral("bad"));

    const auto empty = PaProtocol::parseResponse(QStringLiteral("   \r\n"));
    CHECK(!empty.ok);
    CHECK(!empty.error);
    CHECK(empty.keyword.isEmpty());
    return true;
}

bool testAppSettings() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));

    {
        AppSettings settings(settingsPath);
        CHECK(settings.serialBaudRate() == 115200);
        CHECK(settings.commandTimeoutMs() == 5000);
        settings.setLastImageDirectory(QStringLiteral("/tmp/images"));
        settings.setLastSaveDirectory(QStringLiteral("/tmp/save"));
        settings.setLastExportDirectory(QStringLiteral("/tmp/export"));
        settings.setLastDiagnosticDirectory(QStringLiteral("/tmp/diagnostics"));
        settings.setSerialPort(QStringLiteral("COM_TEST"));
        settings.setSerialBaudRate(921600);
        settings.setCommandTimeoutMs(12000);
        settings.sync();
    }

    {
        AppSettings settings(settingsPath);
        CHECK(settings.lastImageDirectory() == QStringLiteral("/tmp/images"));
        CHECK(settings.lastSaveDirectory() == QStringLiteral("/tmp/save"));
        CHECK(settings.lastExportDirectory() == QStringLiteral("/tmp/export"));
        CHECK(settings.lastDiagnosticDirectory() == QStringLiteral("/tmp/diagnostics"));
        CHECK(settings.serialPort() == QStringLiteral("COM_TEST"));
        CHECK(settings.serialBaudRate() == 921600);
        CHECK(settings.commandTimeoutMs() == 12000);

        settings.setSerialBaudRate(1);
        settings.setCommandTimeoutMs(999999);
        CHECK(settings.serialBaudRate() == 1200);
        CHECK(settings.commandTimeoutMs() == 300000);
    }
    return true;
}

bool testAppLogService() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    AppLogService service;
    service.setRotationPolicy(1024, 2);
    QVector<AppLogEntry> entries;
    QStringList persistenceErrors;
    QObject::connect(&service, &AppLogService::entryAdded,
        [&entries](const AppLogEntry& entry) {
            entries.push_back(entry);
        });
    QObject::connect(&service, &AppLogService::persistenceError,
        [&persistenceErrors](const QString& message) {
            persistenceErrors.push_back(message);
        });

    QString error;
    CHECK(service.start(directory.path(), &error));
    CHECK(service.isStarted());
    CHECK(service.logDirectory() == directory.path());
    CHECK(service.currentLogPath() == directory.filePath(QStringLiteral("pa_host.log")));

    for (int index = 0; index < 40; ++index) {
        service.info(QStringLiteral("test"),
            QStringLiteral("message-%1-%2")
                .arg(index)
                .arg(QString(80, QLatin1Char('x'))));
    }
    service.error(QStringLiteral("diagnostic"), QStringLiteral("final-marker"));

    CHECK(entries.size() == 41);
    CHECK(entries.first().formatted().contains(QStringLiteral("[INFO] [TEST]")));
    CHECK(entries.last().formatted().contains(QStringLiteral("[ERROR] [DIAGNOSTIC] final-marker")));
    CHECK(persistenceErrors.isEmpty());
    CHECK(QFileInfo::exists(directory.filePath(QStringLiteral("pa_host.log"))));
    CHECK(QFileInfo::exists(directory.filePath(QStringLiteral("pa_host.1.log"))));
    const QStringList logFiles = QDir(directory.path()).entryList(
        {QStringLiteral("pa_host.log"), QStringLiteral("pa_host.*.log")},
        QDir::Files);
    CHECK(logFiles.size() <= 3);

    QMap<QString, QString> metadata;
    metadata.insert(QStringLiteral("fixture"), QStringLiteral("app-log-service"));
    const QString diagnosticPath = directory.filePath(QStringLiteral("diagnostics.txt"));
    CHECK(service.exportDiagnostics(diagnosticPath, metadata, &error));
    QFile diagnosticFile(diagnosticPath);
    CHECK(diagnosticFile.open(QIODevice::ReadOnly));
    const QString diagnostic = QString::fromUtf8(diagnosticFile.readAll());
    CHECK(diagnostic.contains(QStringLiteral("PA Host Diagnostics")));
    CHECK(diagnostic.contains(QStringLiteral("fixture=app-log-service")));
    CHECK(diagnostic.contains(QStringLiteral("[logs]")));
    CHECK(diagnostic.contains(QStringLiteral("final-marker")));
    CHECK(!service.exportDiagnostics(service.currentLogPath(), metadata, &error));
    CHECK(error == QStringLiteral("诊断文件不能覆盖当前日志文件"));
    return true;
}

bool testPaDeviceController() {
    struct CommandResult {
        PaProtocol::Command command;
        bool success = false;
        QString detail;
    };

    FakeLineTransport transport;
    PaDeviceController controller(&transport);
    controller.setCommandTimeoutMs(15);
    QVector<CommandResult> results;
    QVector<PaDeviceStatus> statuses;
    QVector<quint64> interruptCounts;
    QStringList errors;
    QObject::connect(&controller, &PaDeviceController::commandFinished,
        [&results](PaProtocol::Command command, bool success, const QString& detail) {
            results.push_back({command, success, detail});
        });
    QObject::connect(&controller, &PaDeviceController::deviceStatusChanged,
        [&statuses](const PaDeviceStatus& status) {
            statuses.push_back(status);
        });
    QObject::connect(&controller, &PaDeviceController::interruptReceived,
        [&interruptCounts](quint64 count) {
            interruptCounts.push_back(count);
        });
    QObject::connect(&controller, &PaDeviceController::errorOccurred,
        [&errors](const QString& message) {
            errors.push_back(message);
        });

    QString error;
    CHECK(controller.state() == PaDeviceState::Disconnected);
    CHECK(!controller.sendCommand(PaProtocol::Command::Ping, &error));
    CHECK(error == QStringLiteral("串口未连接"));

    CHECK(controller.connectDevice(QStringLiteral("COM_TEST"), 115200, &error));
    CHECK(controller.isConnected());
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(transport.lastPortName == QStringLiteral("COM_TEST"));
    CHECK(transport.lastBaudRate == 115200);

    CHECK(controller.sendCommand(PaProtocol::Command::Status, &error));
    CHECK(controller.state() == PaDeviceState::Busy);
    CHECK(controller.hasPendingCommand());
    CHECK(transport.sentLines.last() == QStringLiteral("STATUS"));
    CHECK(!controller.sendCommand(PaProtocol::Command::Ping, &error));
    CHECK(error == QStringLiteral("上一条命令尚未完成"));
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Busy);

    transport.injectLine(QStringLiteral(
        "OK STATUS int=0x10 pa=0x20 com=3 rst=4 wr_state=2 wr_end=1 corr_state=5 corr_end=0"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(!controller.hasPendingCommand());
    CHECK(results.size() == 1);
    CHECK(results.last().command == PaProtocol::Command::Status);
    CHECK(results.last().success);
    CHECK(statuses.size() == 1);
    CHECK(statuses.last().valid);
    CHECK(statuses.last().interruptFlags == 0x10);
    CHECK(statuses.last().paFlags == 0x20);
    CHECK(statuses.last().writeState == 2);
    CHECK(statuses.last().correctionState == 5);

    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    transport.injectLine(QStringLiteral("OK IRQ count=7"));
    CHECK(interruptCounts == QVector<quint64>({7}));
    CHECK(controller.hasPendingCommand());
    CHECK(controller.state() == PaDeviceState::Busy);
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.size() == 2);
    CHECK(results.last().success);

    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    QEventLoop timeoutLoop;
    QTimer::singleShot(40, &timeoutLoop, &QEventLoop::quit);
    timeoutLoop.exec();
    CHECK(controller.state() == PaDeviceState::Error);
    CHECK(!controller.hasPendingCommand());
    CHECK(results.size() == 3);
    CHECK(!results.last().success);
    CHECK(results.last().detail.contains(QStringLiteral("超时")));

    // 错误状态下允许重试，成功响应后恢复 Ready。
    CHECK(controller.sendCommand(PaProtocol::Command::Ping, &error));
    transport.injectLine(QStringLiteral("OK PONG"));
    CHECK(controller.state() == PaDeviceState::Ready);
    CHECK(results.last().success);

    CHECK(controller.sendCommand(PaProtocol::Command::Status, &error));
    transport.injectError(QStringLiteral("模拟链路故障"));
    CHECK(controller.state() == PaDeviceState::Error);
    CHECK(!controller.hasPendingCommand());
    CHECK(!results.last().success);
    CHECK(results.last().detail.contains(QStringLiteral("模拟链路故障")));

    controller.disconnectDevice();
    CHECK(!controller.isConnected());
    CHECK(controller.state() == PaDeviceState::Disconnected);

    FakeLineTransport failedTransport;
    failedTransport.failOpen = true;
    PaDeviceController failedController(&failedTransport);
    CHECK(!failedController.connectDevice(QStringLiteral("BAD"), 9600, &error));
    CHECK(error == QStringLiteral("模拟打开失败"));
    CHECK(failedController.state() == PaDeviceState::Error);
    CHECK(!errors.isEmpty());
    return true;
}

bool testTirawParsingAndRoi() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    const QVector<quint16> pixels = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
    };
    const QString path = directory.filePath(QStringLiteral("basic.tiraw"));
    CHECK(writeTiraw(path, 4, 3, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));
    CHECK(error.isEmpty());
    CHECK(image.isValid());
    CHECK(image.version() == 1);
    CHECK(image.bytesPerPixel() == 2);
    CHECK(image.width() == 4);
    CHECK(image.height() == 3);
    CHECK(image.minValue() == 10);
    CHECK(image.maxValue() == 120);

    quint16 value = 0;
    CHECK(image.pixelValue(2, 1, &value));
    CHECK(value == 70);
    CHECK(!image.pixelValue(-1, 0, &value));
    CHECK(!image.pixelValue(4, 0, &value));

    TiRawImage::RoiStats stats;
    CHECK(image.roiStats(QRect(1, 0, 2, 2), &stats));
    CHECK(stats.rect == QRect(1, 0, 2, 2));
    CHECK(stats.pixelCount == 4);
    CHECK(stats.min == 20);
    CHECK(stats.max == 70);
    CHECK(fuzzyEqual(stats.mean, 45.0));
    CHECK(fuzzyEqual(stats.stddev, std::sqrt(425.0)));
    CHECK(fuzzyEqual(stats.rowNoise, 40.0));
    CHECK(fuzzyEqual(stats.rowNoiseStddev, 20.0));
    CHECK(fuzzyEqual(stats.rowNoiseRatio, 2.0));

    const QImage display = image.toDisplayImage(false, 65, 110);
    CHECK(!display.isNull());
    CHECK(display.width() == 4);
    CHECK(display.height() == 3);
    CHECK(display.constScanLine(0)[0] == 0);
    CHECK(display.constScanLine(2)[3] == 255);
    const QImage thumbnail = image.toDisplayImage(false, 65, 110, QSize(2, 2));
    CHECK(thumbnail.size() == QSize(2, 2));
    CHECK(thumbnail.constScanLine(1)[1] == 255);

    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    TiRawImage memoryImage;
    CHECK(memoryImage.loadData(file.readAll(), QStringLiteral("pcie-frame-1"), &error));
    CHECK(memoryImage.path() == QStringLiteral("pcie-frame-1"));
    CHECK(memoryImage.width() == 4);
    CHECK(memoryImage.height() == 3);
    CHECK(memoryImage.pixelValue(2, 1, &value));
    CHECK(value == 70);

    const QString exportedTirawPath = directory.filePath(QStringLiteral("exported.tiraw"));
    const QString exportedRawPath = directory.filePath(QStringLiteral("exported.raw"));
    CHECK(image.saveTiRaw(exportedTirawPath, &error));
    CHECK(image.saveRaw16(exportedRawPath, &error));
    CHECK(QFileInfo(exportedTirawPath).size() == 16 + pixels.size() * 2);
    CHECK(QFileInfo(exportedRawPath).size() == pixels.size() * 2);
    TiRawImage exportedImage;
    CHECK(exportedImage.load(exportedTirawPath, &error));
    CHECK(exportedImage.pixelValue(2, 1, &value));
    CHECK(value == 70);
    return true;
}

bool testAutoWindowLevel() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(1000);
    for (quint16 value = 0; value < 1000; ++value) {
        pixels.push_back(value);
    }

    const QString path = directory.filePath(QStringLiteral("percentile.tiraw"));
    CHECK(writeTiraw(path, 100, 10, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));
    CHECK(image.autoWindowLow() == 6);
    CHECK(image.autoWindowHigh() == 993);
    CHECK(image.autoWindowCenter() == 499);
    CHECK(image.autoWindowWidth() == 987);

    const QImage display = image.toDisplayImage(true, 0, 1);
    CHECK(display.constScanLine(0)[0] == 0);
    CHECK(display.constScanLine(9)[99] == 255);
    return true;
}

bool testImageAlgorithmBoundary() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(1000);
    for (quint16 value = 0; value < 1000; ++value) {
        pixels.push_back(value);
    }

    const QString path = directory.filePath(QStringLiteral("algorithm-boundary.tiraw"));
    CHECK(writeTiraw(path, 100, 10, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(path, &error));

    BuiltinImageAlgorithms algorithms;
    const WindowLevelResult automatic = algorithms.autoWindowLevel(image);
    CHECK(automatic.valid);
    CHECK(automatic.low == 6);
    CHECK(automatic.high == 993);
    CHECK(automatic.center == 499);
    CHECK(automatic.width == 987);

    const WindowLevelResult roi = algorithms.roiWindowLevel(image, QRect(0, 0, 10, 2));
    CHECK(roi.valid);
    CHECK(roi.low == 0);
    CHECK(roi.high == 109);
    CHECK(roi.center == 54);
    CHECK(roi.width == 109);

    CHECK(!algorithms.autoWindowLevel(TiRawImage()).valid);
    CHECK(!algorithms.roiWindowLevel(image, QRect(-10, -10, 2, 2)).valid);
    return true;
}

bool testImageSession() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.tiraw"));
    CHECK(writeTiraw(path, 2, 2, {10, 20, 30, 40}));

    ImageSession session(std::make_shared<BuiltinImageAlgorithms>());
    QString error;
    CHECK(session.loadFile(path, &error));
    CHECK(session.hasImage());
    CHECK(session.currentFrame().sourceName == QStringLiteral("session.tiraw"));
    CHECK(session.autoWindowLevel().valid);
    CHECK(!session.render(25, 30).isNull());
    TiRawImage::RoiStats stats;
    CHECK(session.roiStats(QRect(0, 0, 2, 2), &stats));
    CHECK(stats.max == 40);
    session.clear();
    CHECK(!session.hasImage());
    return true;
}

bool testImageExportService() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("export-source.tiraw"));
    CHECK(writeTiraw(sourcePath, 2, 2, {10, 20, 30, 40}));

    TiRawImage image;
    QString error;
    CHECK(image.load(sourcePath, &error));

    ImageExportFormat format;
    CHECK(ImageExportService::findFormat(QStringLiteral("tiraw"), &format));
    CHECK(format.suffix == QStringLiteral("tiraw"));
    CHECK(ImageExportService::isSupported(QStringLiteral("raw")));
    CHECK(ImageExportService::ensureFileSuffix(
              directory.filePath(QStringLiteral("raw-export")), QStringLiteral("raw"))
        .endsWith(QStringLiteral(".raw")));

    const QString rawPath = directory.filePath(QStringLiteral("service.raw"));
    CHECK(ImageExportService::exportImage(image, 25, 30, QStringLiteral("raw"), rawPath, &error));
    CHECK(QFileInfo(rawPath).size() == 8);

    if (ImageExportService::isSupported(QStringLiteral("png"))) {
        const QString pngPath = directory.filePath(QStringLiteral("service.png"));
        CHECK(ImageExportService::exportImage(image, 25, 30, QStringLiteral("png"), pngPath, &error));
        QImage exportedDisplay(pngPath);
        CHECK(exportedDisplay.size() == QSize(2, 2));
    }

    CHECK(!ImageExportService::exportImage(
        image, 25, 30, QStringLiteral("unknown"), directory.filePath(QStringLiteral("bad.bin")), &error));
    CHECK(!error.isEmpty());
    return true;
}

bool testReplayPresentationScheduler() {
    ReplayPresentationScheduler scheduler;
    scheduler.setTargetFps(60);
    scheduler.reset();
    CHECK(scheduler.targetFps() == 60);
    CHECK(scheduler.delayMs(0) == 0);

    scheduler.markPresented(0);
    CHECK(scheduler.delayMs(16000000) == 1);
    scheduler.markPresented(17000000);
    CHECK(scheduler.delayMs(32000000) == 2);
    scheduler.markPresented(34000000);
    CHECK(scheduler.delayMs(49000000) == 1);

    scheduler.markPresented(100000000);
    CHECK(scheduler.delayMs(100000000) == 17);
    scheduler.setTargetFps(0);
    CHECK(scheduler.targetFps() == 1);
    scheduler.setTargetFps(1000);
    CHECK(scheduler.targetFps() == 120);
    return true;
}

bool testFramePresentationController() {
    FramePresentationController controller;
    QVector<quint64> presentedSequences;
    QObject::connect(&controller, &FramePresentationController::framePresented,
        [&presentedSequences](const ImageFrame& frame) {
            presentedSequences.push_back(frame.sequence);
        });

    controller.start(30);
    for (quint64 sequence = 1; sequence <= 3; ++sequence) {
        ImageFrame frame;
        frame.sequence = sequence;
        controller.submitFrame(frame);
    }

    QEventLoop firstPresentationLoop;
    QTimer::singleShot(50, &firstPresentationLoop, &QEventLoop::quit);
    firstPresentationLoop.exec();

    FramePresentationStats stats = controller.stats();
    CHECK(presentedSequences == QVector<quint64>({3}));
    CHECK(stats.submittedFrames == 3);
    CHECK(stats.presentedFrames == 1);
    CHECK(stats.droppedFrames == 2);

    ImageFrame pendingFrame;
    pendingFrame.sequence = 4;
    controller.submitFrame(pendingFrame);
    controller.stop();
    stats = controller.stats();
    CHECK(!controller.isActive());
    CHECK(stats.submittedFrames == 4);
    CHECK(stats.presentedFrames == 1);
    CHECK(stats.droppedFrames == 3);

    // 每次开始回放都建立独立统计周期，避免上一次回放污染状态栏数据。
    controller.start(60);
    stats = controller.stats();
    CHECK(controller.isActive());
    CHECK(controller.targetFps() == 60);
    CHECK(stats.submittedFrames == 0);
    CHECK(stats.presentedFrames == 0);
    CHECK(stats.droppedFrames == 0);
    controller.stop();
    return true;
}

bool testLocalReplaySource() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.tiraw"));
    const QString second = directory.filePath(QStringLiteral("second.tiraw"));
    CHECK(writeTiraw(first, 2, 1, {1, 2}));
    CHECK(writeTiraw(second, 2, 1, {3, 4}));

    LocalReplaySource source;
    source.setPlaylist({first, second});
    source.setIntervalMs(1);
    source.setLoopEnabled(false);
    QVector<ImageFrame> frames;
    bool runningWhileDelivering = true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&source, &LocalReplaySource::frameReady, &loop, [&frames, &source, &runningWhileDelivering](const ImageFrame& frame) {
        runningWhileDelivering = runningWhileDelivering && source.isRunning();
        frames.push_back(frame);
    });
    QObject::connect(&source, &LocalReplaySource::runningChanged, &loop, [&loop](bool running) {
        if (!running) {
            loop.quit();
        }
    });

    QString error;
    CHECK(source.start(&error));
    CHECK(QFile::remove(second));
    timeout.start(1000);
    loop.exec();
    CHECK(frames.size() == 2);
    CHECK(frames.at(0).sequence == 0);
    CHECK(frames.at(1).sequence == 1);
    CHECK(frames.at(0).image.minValue() == 1);
    CHECK(runningWhileDelivering);
    CHECK(!source.isRunning());
    CHECK(source.stats().deliveredFrames == 2);
    CHECK(source.stats().failedFrames == 0);

    LocalReplaySource invalidSource;
    invalidSource.setPlaylist({directory.filePath(QStringLiteral("missing.tiraw")), first});
    invalidSource.setIntervalMs(1);
    invalidSource.setLoopEnabled(false);
    int validFrames = 0;
    QEventLoop invalidLoop;
    QTimer invalidTimeout;
    invalidTimeout.setSingleShot(true);
    QObject::connect(&invalidTimeout, &QTimer::timeout, &invalidLoop, &QEventLoop::quit);
    QObject::connect(&invalidSource, &LocalReplaySource::frameReady, &invalidLoop, [&validFrames](const ImageFrame&) {
        ++validFrames;
    });
    QObject::connect(&invalidSource, &LocalReplaySource::runningChanged, &invalidLoop, [&invalidLoop](bool running) {
        if (!running) {
            invalidLoop.quit();
        }
    });
    CHECK(invalidSource.start(&error));
    invalidTimeout.start(1000);
    invalidLoop.exec();
    CHECK(validFrames == 1);
    CHECK(invalidSource.stats().failedFrames == 1);

    LocalReplaySource restartedSource;
    restartedSource.setPlaylist({first});
    restartedSource.setIntervalMs(1000);
    restartedSource.setLoopEnabled(true);
    int restartedFrames = 0;
    QObject::connect(&restartedSource, &LocalReplaySource::frameReady, [&restartedFrames](const ImageFrame&) {
        ++restartedFrames;
    });
    CHECK(restartedSource.start(&error));
    restartedSource.stop();
    CHECK(restartedSource.start(&error));
    QEventLoop restartLoop;
    QTimer::singleShot(20, &restartLoop, &QEventLoop::quit);
    restartLoop.exec();
    restartedSource.stop();
    CHECK(restartedFrames == 1);
    return true;
}

bool testInvalidTirawFiles() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    const QVector<quint16> pixels = {1, 2, 3, 4};
    TiRawImage image;
    QString error;

    const QString badMagicPath = directory.filePath(QStringLiteral("bad-magic.tiraw"));
    CHECK(writeTiraw(badMagicPath, 2, 2, pixels, QByteArrayLiteral("BadMagic")));
    CHECK(!image.load(badMagicPath, &error));
    CHECK(!error.isEmpty());

    const QString badBppPath = directory.filePath(QStringLiteral("bad-bpp.tiraw"));
    CHECK(writeTiraw(badBppPath, 2, 2, pixels, QByteArrayLiteral("TiRayRaw"), 1));
    CHECK(!image.load(badBppPath, &error));

    const QString truncatedPath = directory.filePath(QStringLiteral("truncated.tiraw"));
    CHECK(writeTiraw(truncatedPath, 3, 2, pixels));
    CHECK(!image.load(truncatedPath, &error));

    const QString shortPath = directory.filePath(QStringLiteral("short.tiraw"));
    QFile shortFile(shortPath);
    CHECK(shortFile.open(QIODevice::WriteOnly));
    CHECK(shortFile.write("short") == 5);
    shortFile.close();
    CHECK(!image.load(shortPath, &error));
    return true;
}

bool testMtfAnalysisAndExport() {
    QTemporaryDir directory;
    CHECK(directory.isValid());

    QVector<quint16> pixels;
    pixels.reserve(200);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 100; ++x) {
            pixels.push_back(x < 50 ? 1000 : 4000);
        }
    }

    const QString imagePath = directory.filePath(QStringLiteral("edge.tiraw"));
    CHECK(writeTiraw(imagePath, 100, 2, pixels));

    TiRawImage image;
    QString error;
    CHECK(image.load(imagePath, &error));

    MtfAnalysisResult result;
    CHECK(MtfAnalysis::analyze(image, QRect(0, 0, 100, 2), &result));
    CHECK(result.esf.y.size() == 396);
    CHECK(result.lsf.y.size() == result.esf.y.size());
    CHECK(result.mtf.y.size() >= 100);
    CHECK(fuzzyEqual(result.esf.x.at(1), 0.025));
    CHECK(fuzzyEqual(result.mtf.y.first(), 1.0));

    const QString outputDirectory = directory.filePath(QStringLiteral("curves"));
    CHECK(MtfAnalysis::exportCsv(outputDirectory, result, &error));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/esf.csv")));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/lsf.csv")));
    CHECK(QFile::exists(outputDirectory + QStringLiteral("/mtf.csv")));
    return true;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const TestCase tests[] = {
        {"protocol_commands", testProtocolCommands},
        {"protocol_responses", testProtocolResponses},
        {"app_settings", testAppSettings},
        {"app_log_service", testAppLogService},
        {"pa_device_controller", testPaDeviceController},
        {"tiraw_parsing_and_roi", testTirawParsingAndRoi},
        {"auto_window_level", testAutoWindowLevel},
        {"image_algorithm_boundary", testImageAlgorithmBoundary},
        {"image_session", testImageSession},
        {"image_export_service", testImageExportService},
        {"replay_presentation_scheduler", testReplayPresentationScheduler},
        {"frame_presentation_controller", testFramePresentationController},
        {"local_replay_source", testLocalReplaySource},
        {"invalid_tiraw_files", testInvalidTirawFiles},
        {"mtf_analysis_and_export", testMtfAnalysisAndExport},
    };

    int failures = 0;
    for (const TestCase& test : tests) {
        if (test.run()) {
            qInfo("PASS %s", test.name);
        } else {
            qCritical("FAIL %s", test.name);
            ++failures;
        }
    }

    qInfo("测试完成: %d 通过, %d 失败", static_cast<int>(std::size(tests)) - failures, failures);
    return failures == 0 ? 0 : 1;
}
