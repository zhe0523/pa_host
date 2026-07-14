#include "ImageAlgorithms.h"
#include "ImageSession.h"
#include "ImageSource.h"
#include "MtfAnalysis.h"
#include "PaProtocol.h"
#include "TiRawImage.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <iterator>

namespace {
struct TestCase {
    const char* name;
    std::function<bool()> run;
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
        {"tiraw_parsing_and_roi", testTirawParsingAndRoi},
        {"auto_window_level", testAutoWindowLevel},
        {"image_algorithm_boundary", testImageAlgorithmBoundary},
        {"image_session", testImageSession},
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
