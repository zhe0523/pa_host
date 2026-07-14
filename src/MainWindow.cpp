#include "MainWindow.h"

#include "AppLogService.h"
#include "AppSettings.h"
#include "FramePresentationController.h"
#include "ImageExportService.h"
#include "ImageListPanel.h"
#include "PaDeviceController.h"

#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextDocument>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kStaticImageRefreshIntervalMs = 33;

QSize initialWindowSize() {
    auto* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return {1180, 720};
    }

    const QSize available = screen->availableGeometry().size();
    return {
        std::max(1180, std::min(1320, static_cast<int>(available.width() * 0.92))),
        std::max(820, std::min(900, static_cast<int>(available.height() * 0.92))),
    };
}

QPushButton* makeCommandButton(const QString& text) {
    auto* button = new QPushButton(text);
    button->setMinimumHeight(26);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return button;
}

void populateSerialPorts(QComboBox* combo) {
    combo->clear();
    combo->setEditable(true);
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_WIN
        combo->addItem(info.portName());
#else
        combo->addItem(info.systemLocation());
#endif
    }
    if (combo->count() == 0) {
#ifdef Q_OS_WIN
        combo->addItems({QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4")});
#else
        combo->addItems({QStringLiteral("/dev/ttyS0"), QStringLiteral("/dev/ttyS1"), QStringLiteral("/dev/ttyUSB0")});
#endif
    }
}

QString defaultImageDirectory(const AppSettings& settings) {
    const QString remembered = settings.lastImageDirectory();
    if (!remembered.isEmpty() && QDir(remembered).exists()) {
        return remembered;
    }

#ifndef Q_OS_WIN
    const QString sampleDirectory = QStringLiteral("/home/zhe/app/windows/tidetector/CollectImage");
    if (QDir(sampleDirectory).exists()) {
        return sampleDirectory;
    }
#endif

    const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return picturesDirectory.isEmpty() ? QDir::homePath() : picturesDirectory;
}

QString normalizedImagePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString deviceStateName(PaDeviceState state) {
    switch (state) {
    case PaDeviceState::Disconnected:
        return QStringLiteral("Disconnected");
    case PaDeviceState::Ready:
        return QStringLiteral("Ready");
    case PaDeviceState::Busy:
        return QStringLiteral("Busy");
    case PaDeviceState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QPixmap drawAnalysisPreview(const QImage& image, const QRect& roi, const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(QColor(8, 9, 10));

    if (image.isNull()) {
        return pixmap;
    }

    const QSize scaledSize = image.size().scaled(size, Qt::KeepAspectRatio);
    const QRect targetRect(
        (size.width() - scaledSize.width()) / 2,
        (size.height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(targetRect, image);

    const qreal scaleX = static_cast<qreal>(targetRect.width()) / image.width();
    const qreal scaleY = static_cast<qreal>(targetRect.height()) / image.height();
    const QRectF roiRect(
        targetRect.left() + roi.left() * scaleX,
        targetRect.top() + roi.top() * scaleY,
        roi.width() * scaleX,
        roi.height() * scaleY);
    painter.setPen(QPen(QColor(255, 110, 0), 1.2));
    painter.drawRect(roiRect);
    return pixmap;
}

QPixmap drawLineChart(
    const QString& title,
    const QString& yLabel,
    const QString& xLabel,
    const QVector<double>& values,
    const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect plotRect(56, 28, size.width() - 72, size.height() - 62);
    painter.setPen(QColor(25, 31, 37));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRect(0, 4, size.width(), 20), Qt::AlignCenter, title);

    painter.setFont(QFont());
    painter.setPen(QColor(230, 235, 240));
    for (int i = 0; i <= 5; ++i) {
        const int x = plotRect.left() + plotRect.width() * i / 5;
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
        const int y = plotRect.top() + plotRect.height() * i / 5;
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
    }

    painter.setPen(QColor(30, 35, 40));
    painter.drawRect(plotRect);

    if (!values.isEmpty()) {
        auto range = std::minmax_element(values.begin(), values.end());
        double minValue = *range.first;
        double maxValue = *range.second;
        if (maxValue <= minValue) {
            maxValue = minValue + 1.0;
        }

        QPolygonF polyline;
        polyline.reserve(values.size());
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = plotRect.left() + (values.size() == 1 ? 0.0 : plotRect.width() * static_cast<qreal>(i) / (values.size() - 1));
            const qreal normalized = (values.at(i) - minValue) / (maxValue - minValue);
            const qreal y = plotRect.bottom() - normalized * plotRect.height();
            polyline << QPointF(x, y);
        }
        painter.setPen(QPen(QColor(31, 119, 180), 1.0));
        painter.drawPolyline(polyline);

        painter.setPen(QColor(60, 67, 75));
        painter.drawText(4, plotRect.top() + 4, QString::number(maxValue, 'f', 0));
        painter.drawText(4, plotRect.bottom(), QString::number(minValue, 'f', 0));
    }

    painter.setPen(QColor(25, 31, 37));
    painter.drawText(QRect(plotRect.left(), size.height() - 24, plotRect.width(), 18), Qt::AlignCenter, xLabel);

    painter.save();
    painter.translate(14, plotRect.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-plotRect.height() / 2, 0, plotRect.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();

    return pixmap;
}
}

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(std::make_shared<BuiltinImageAlgorithms>(), parent) {
}

MainWindow::~MainWindow() {
    if (logService_ != nullptr) {
        logService_->info(QStringLiteral("SYSTEM"), QStringLiteral("应用正常退出"));
    }
    if (settings_ != nullptr) {
        settings_->sync();
    }
}

MainWindow::MainWindow(std::shared_ptr<IImageAlgorithms> algorithms, QWidget* parent)
    : QMainWindow(parent) {
    settings_ = std::make_unique<AppSettings>();
    logService_ = new AppLogService(this);
    QString logError;
    if (!logService_->start(QString(), &logError)) {
        qWarning().noquote() << logError;
    }
    imageSession_ = std::make_unique<ImageSession>(std::move(algorithms), this);
    replaySource_ = new LocalReplaySource(this);
    presentationController_ = new FramePresentationController(this);
    deviceController_ = new PaDeviceController(&serial_, this);
    deviceController_->setCommandTimeoutMs(settings_->commandTimeoutMs());
    replayDisplayCache_.setMaxCost(4);
    setWindowTitle(QStringLiteral("PA Host"));
    setMinimumSize(1180, 820);
    resize(initialWindowSize());

    createMenus();

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("mainRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    topBar_ = createTopBar();
    rootLayout->addWidget(topBar_);

    auto* splitter = new QSplitter(Qt::Horizontal, root);
    imageListPanel_ = new ImageListPanel(splitter);
    splitter->addWidget(imageListPanel_);

    imageView_ = new ImageView(splitter);
    imageView_->setObjectName(QStringLiteral("imageCanvas"));
    splitter->addWidget(imageView_);
    rightPanel_ = createRightPanel();
    splitter->addWidget(rightPanel_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({205, 920, 285});
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(root);
    createLogDock();
    createStatusBar();

    connect(logService_, &AppLogService::entryAdded, this, [this](const AppLogEntry& entry) {
        if (logView_ != nullptr) {
            logView_->appendPlainText(entry.formatted());
        }
    });
    connect(logService_, &AppLogService::persistenceError, this, [](const QString& message) {
        qWarning().noquote() << message;
    });

    connect(deviceController_, &PaDeviceController::stateChanged,
        this, &MainWindow::updateDeviceState);
    connect(deviceController_, &PaDeviceController::deviceStatusChanged,
        this, &MainWindow::updateDeviceStatus);
    connect(deviceController_, &PaDeviceController::lineTransmitted, this, [this](const QString& line) {
        logService_->info(QStringLiteral("RS422"), QStringLiteral("TX: %1").arg(line));
    });
    connect(deviceController_, &PaDeviceController::lineReceived, this, [this](const QString& line) {
        logService_->info(QStringLiteral("RS422"), QStringLiteral("RX: %1").arg(line));
    });
    connect(deviceController_, &PaDeviceController::errorOccurred, this, [this](const QString& message) {
        logService_->error(QStringLiteral("RS422"), QStringLiteral("控制错误: %1").arg(message));
    });
    connect(deviceController_, &PaDeviceController::commandFinished, this,
        [this](PaProtocol::Command command, bool success, const QString& detail) {
            const QString message = QStringLiteral("命令%1: %2, %3")
                                        .arg(success ? QStringLiteral("完成") : QStringLiteral("失败"))
                                        .arg(PaProtocol::commandName(command), detail);
            logService_->log(success ? AppLogLevel::Info : AppLogLevel::Warning,
                QStringLiteral("RS422"), message);
        });
    connect(imageView_, &ImageView::zoomChanged, this, [this](int percent) {
        progressLabel_->setText(QStringLiteral("缩放: %1%").arg(percent));
    });
    connect(imageView_, &ImageView::pixelHovered, this, &MainWindow::updatePixelInfo);
    connect(imageView_, &ImageView::analysisRoiSelected, this, &MainWindow::handleAnalysisRoi);
    connect(imageView_, &ImageView::windowLevelRoiSelected, this, &MainWindow::applyWindowLevelFromRoi);
    connect(imageView_, &ImageView::roiCleared, this, &MainWindow::updateFullImageInfo);
    connect(imageListPanel_, &ImageListPanel::imageActivated, this, &MainWindow::handleImageListSelection);
    connect(imageListPanel_, &ImageListPanel::imagesRemoved, this, &MainWindow::handleImagesRemoved);
    connect(imageListPanel_, &ImageListPanel::exportRequested, this, &MainWindow::exportImage);
    connect(replaySource_, &LocalReplaySource::frameReady,
        presentationController_, &FramePresentationController::submitFrame);
    connect(presentationController_, &FramePresentationController::framePresented,
        this, &MainWindow::handlePresentedFrame);
    connect(presentationController_, &FramePresentationController::fpsUpdated,
        this, [this](double actualFps, int targetFps) {
            fpsLabel_->setText(QStringLiteral("显示 fps: %1 / 目标 %2")
                                   .arg(actualFps, 0, 'f', 2)
                                   .arg(targetFps));
        });
    connect(replaySource_, &LocalReplaySource::sourceError, this, [this](const QString& message) {
        logService_->warning(QStringLiteral("IMAGE"), message);
    });
    connect(replaySource_, &LocalReplaySource::runningChanged, this, [this](bool running) {
        if (!running) {
            const ImageSourceStats stats = replaySource_->stats();
            presentationController_->stop();
            const FramePresentationStats presentationStats = presentationController_->stats();
            logService_->info(QStringLiteral("IMAGE"),
                QStringLiteral("图像回放停止: 输入 %1 帧，显示 %2 帧，显示丢帧 %3，加载失败 %4")
                    .arg(stats.deliveredFrames)
                    .arg(presentationStats.presentedFrames)
                    .arg(presentationStats.droppedFrames)
                    .arg(stats.failedFrames));
        }
    });
    imageRefreshTimer_.setSingleShot(true);
    imageRefreshTimer_.setInterval(kStaticImageRefreshIntervalMs);
    connect(&imageRefreshTimer_, &QTimer::timeout, this, [this]() {
        refreshImage(resetViewStateOnRefresh_);
        resetViewStateOnRefresh_ = false;
    });
    imageInfoRefreshTimer_.setSingleShot(true);
    connect(&imageInfoRefreshTimer_, &QTimer::timeout, this, &MainWindow::updateFullImageInfo);
    updateDeviceState(deviceController_->state());
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("应用启动，日志目录: %1").arg(logService_->logDirectory()));
}

void MainWindow::openImage() {
    stopImageReplay();
    const QString initialDirectory = defaultImageDirectory(*settings_);
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("打开 TiRaw 图像（可多选）"),
        initialDirectory,
        QStringLiteral("TiRayRaw (*.tiraw);;All Files (*)"));
    if (paths.isEmpty()) {
        return;
    }

    settings_->setLastImageDirectory(QFileInfo(paths.first()).absolutePath());

    TiRawImage lastImage;
    QString lastPath;
    QStringList failures;
    for (const QString& selectedPath : paths) {
        const QString path = normalizedImagePath(selectedPath);
        TiRawImage image;
        QString error;
        if (!image.load(path, &error)) {
            failures.push_back(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
            continue;
        }

        imageListPanel_->addOrUpdateImage(path, &image);
        lastImage = std::move(image);
        lastPath = path;
    }

    if (!lastPath.isEmpty()) {
        ImageFrame frame;
        frame.image = std::move(lastImage);
        frame.sourceName = QFileInfo(lastPath).fileName();
        frame.receivedAt = QDateTime::currentDateTimeUtc();
        QString error;
        if (imageSession_->setFrame(frame, &error)) {
            imageListPanel_->setCurrentPath(lastPath);
            showCurrentSessionImage(lastPath, true);
        }
    }

    if (!failures.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("部分图像打开失败"),
            failures.join(QLatin1Char('\n')));
    }
}

void MainWindow::startImageReplay() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择回放 TiRaw 序列"),
        defaultImageDirectory(*settings_),
        QStringLiteral("TiRayRaw (*.tiraw);;All Files (*)"));
    if (paths.isEmpty()) {
        return;
    }

    bool accepted = false;
    const int fps = QInputDialog::getInt(
        this, QStringLiteral("回放帧率"), QStringLiteral("帧率 (fps)"), 30, 1, 120, 1, &accepted);
    if (!accepted) {
        return;
    }

    stopImageReplay();
    settings_->setLastImageDirectory(QFileInfo(paths.first()).absolutePath());
    replaySource_->setPlaylist(paths);
    replaySource_->setIntervalMs(std::max(1, 1000 / fps));
    replaySource_->setLoopEnabled(true);
    imageListPanel_->clearImages();
    for (const QString& path : paths) {
        imageListPanel_->addOrUpdateImage(path);
    }

    clearReplayDisplayCaches();
    replayFullImageStatsCache_.clear();
    imageInfoTimer_.invalidate();
    QString error;
    QElapsedTimer preloadTimer;
    preloadTimer.start();
    if (!replaySource_->start(&error)) {
        imageView_->setPixmapCacheEnabled(false);
        QMessageBox::warning(this, QStringLiteral("回放失败"), error);
        return;
    }
    const qint64 preloadMs = preloadTimer.elapsed();
    const ImageSourceStats sourceStats = replaySource_->stats();
    const quint64 selectedFrames = static_cast<quint64>(paths.size());
    const quint64 loadedFrames = selectedFrames > sourceStats.failedFrames
        ? selectedFrames - sourceStats.failedFrames
        : 0;
    imageView_->setPixmapCacheEnabled(true);
    // 预加载完成后再启动计时，实际 FPS 不包含文件读取耗时。
    presentationController_->start(fps);
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("开始图像回放: 预加载 %1/%2 帧，耗时 %3 ms，目标 %4 fps")
            .arg(loadedFrames)
            .arg(paths.size())
            .arg(preloadMs)
            .arg(fps));
}

void MainWindow::stopImageReplay() {
    imageRefreshTimer_.stop();
    imageInfoRefreshTimer_.stop();
    // 先停止呈现控制器，将尚未显示的最新帧记入丢帧统计。
    presentationController_->stop();
    resetViewStateOnRefresh_ = false;
    clearReplayDisplayCaches();
    replayFullImageStatsCache_.clear();
    imageView_->setPixmapCacheEnabled(false);
    if (replaySource_ != nullptr && replaySource_->isRunning()) {
        replaySource_->stop();
    }
}

void MainWindow::saveDisplayImage() {
    if (!imageView_->hasImage()) {
        QMessageBox::information(this, QStringLiteral("无图像"), QStringLiteral("当前没有可保存的显示图像"));
        return;
    }

    QString saveDirectory = settings_->lastSaveDirectory();
    if (saveDirectory.isEmpty() || !QDir(saveDirectory).exists()) {
        saveDirectory = defaultImageDirectory(*settings_);
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存显示图像"),
        QDir(saveDirectory).filePath(QStringLiteral("pa_host_display.png")),
        QStringLiteral("PNG Image (*.png)"));
    if (path.isEmpty()) {
        return;
    }

    if (!imageView_->savePng(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("图像保存失败"));
        return;
    }
    settings_->setLastSaveDirectory(QFileInfo(path).absolutePath());
    logService_->info(QStringLiteral("IMAGE"), QStringLiteral("保存显示图像: %1").arg(path));
}

void MainWindow::connectSerial() {
    QString error;
    if (!deviceController_->connectDevice(portCombo_->currentText(), baudSpin_->value(), &error)) {
        QMessageBox::warning(this, QStringLiteral("串口打开失败"), error);
        return;
    }
    settings_->setSerialPort(portCombo_->currentText());
    settings_->setSerialBaudRate(baudSpin_->value());
    logService_->info(QStringLiteral("RS422"),
        QStringLiteral("串口已打开: %1 @ %2")
            .arg(portCombo_->currentText())
            .arg(baudSpin_->value()));
}

void MainWindow::disconnectSerial() {
    deviceController_->disconnectDevice();
    logService_->info(QStringLiteral("RS422"), QStringLiteral("串口已关闭"));
}

void MainWindow::sendCommand(PaProtocol::Command command) {
    QString error;
    deviceController_->sendCommand(command, &error);
}

void MainWindow::exportDiagnostics() {
    QString directory = settings_->lastDiagnosticDirectory();
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }

    const QString defaultName = QStringLiteral("pa_host_diagnostics_%1.txt")
                                    .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd-HHmmss")));
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出诊断信息"),
        QDir(directory).filePath(defaultName),
        QStringLiteral("Text File (*.txt)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QStringLiteral(".txt");
    }

    QMap<QString, QString> metadata;
    metadata.insert(QStringLiteral("serial.port"), portCombo_->currentText());
    metadata.insert(QStringLiteral("serial.baud"), QString::number(baudSpin_->value()));
    metadata.insert(QStringLiteral("control.command_timeout_ms"),
        QString::number(deviceController_->commandTimeoutMs()));
    metadata.insert(QStringLiteral("control.device_state"),
        deviceStateName(deviceController_->state()));
    metadata.insert(QStringLiteral("log.directory"), logService_->logDirectory());

    QString error;
    if (!logService_->exportDiagnostics(outputPath, metadata, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        logService_->error(QStringLiteral("SYSTEM"),
            QStringLiteral("导出诊断信息失败: %1").arg(error));
        return;
    }
    settings_->setLastDiagnosticDirectory(QFileInfo(outputPath).absolutePath());
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("导出诊断信息: %1").arg(outputPath));
}

void MainWindow::configureCommandTimeout() {
    bool accepted = false;
    const int timeoutMs = QInputDialog::getInt(
        this,
        QStringLiteral("命令超时设置"),
        QStringLiteral("响应超时 (ms)"),
        deviceController_->commandTimeoutMs(),
        100,
        300000,
        100,
        &accepted);
    if (!accepted) {
        return;
    }

    deviceController_->setCommandTimeoutMs(timeoutMs);
    settings_->setCommandTimeoutMs(timeoutMs);
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("命令响应超时设置为 %1 ms").arg(timeoutMs));
}

void MainWindow::updateWindowLevel() {
    if (centerSlider_->value() != centerSpin_->value()) {
        centerSpin_->setValue(centerSlider_->value());
    }
    if (widthSlider_->value() != widthSpin_->value()) {
        widthSpin_->setValue(widthSlider_->value());
    }
    if (!autoWindowCheck_->isChecked()) {
        clearReplayDisplayCaches();
        scheduleImageRefresh(false);
    }
}

void MainWindow::toggleImageMaximized() {
    imageMaximized_ = !imageMaximized_;

    if (topBar_ != nullptr) {
        topBar_->setVisible(!imageMaximized_);
    }
    if (imageListPanel_ != nullptr) {
        imageListPanel_->setVisible(!imageMaximized_);
    }
    if (rightPanel_ != nullptr) {
        rightPanel_->setVisible(!imageMaximized_);
    }
    if (imageMaximizeAction_ != nullptr) {
        imageMaximizeAction_->setChecked(imageMaximized_);
        imageMaximizeAction_->setText(imageMaximized_
                                          ? QStringLiteral("退出图像最大化")
                                          : QStringLiteral("图像最大化"));
    }
    if (imageView_ != nullptr && imageView_->hasImage()) {
        imageView_->fitToWindow();
    }
}

QWidget* MainWindow::createTopBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    auto* idleButton = new QPushButton(QStringLiteral("Idle"), bar);
    auto* continuousButton = new QPushButton(QStringLiteral("Continuous"), bar);
    manualImageButton_ = new QPushButton(QStringLiteral("手动上图"), bar);
    statusButton_ = new QPushButton(QStringLiteral("停止上图"), bar);

    idleButton->setCheckable(true);
    idleButton->setChecked(true);
    continuousButton->setCheckable(true);
    idleButton->setProperty("role", "mode");
    continuousButton->setProperty("role", "mode");
    manualImageButton_->setProperty("role", "primary");
    statusButton_->setProperty("role", "stop");

    auto* modeGroup = new QButtonGroup(bar);
    modeGroup->setExclusive(true);
    modeGroup->addButton(idleButton);
    modeGroup->addButton(continuousButton);

    connect(manualImageButton_, &QPushButton::clicked, this, [this]() {
        sendCommand(PaProtocol::Command::SendImage);
    });
    connect(statusButton_, &QPushButton::clicked, this, [this]() {
        sendCommand(PaProtocol::Command::Status);
    });

    layout->addWidget(idleButton);
    layout->addWidget(continuousButton);
    layout->addSpacing(18);
    layout->addWidget(manualImageButton_);
    layout->addWidget(statusButton_);
    layout->addStretch(1);

    return bar;
}

QWidget* MainWindow::createRightPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("rightPanel"));
    panel->setMinimumWidth(270);
    panel->setMaximumWidth(340);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(createImageOpsPanel());
    layout->addWidget(createWindowLevelPanel());
    layout->addWidget(createImageInfoPanel());
    layout->addStretch(1);

    return panel;
}

QWidget* MainWindow::createImageOpsPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像操作"), this);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);

    auto* rotateLeftButton = makeCommandButton(QStringLiteral("左转"));
    auto* rotateRightButton = makeCommandButton(QStringLiteral("右转"));
    auto* flipHButton = makeCommandButton(QStringLiteral("水平翻转"));
    auto* flipVButton = makeCommandButton(QStringLiteral("垂直翻转"));
    auto* zoomOutButton = makeCommandButton(QStringLiteral("缩小"));
    auto* zoomInButton = makeCommandButton(QStringLiteral("放大"));
    auto* fitButton = makeCommandButton(QStringLiteral("适应"));
    auto* resetButton = makeCommandButton(QStringLiteral("重置"));
    auto* saveButton = makeCommandButton(QStringLiteral("保存"));
    auto* maximizeButton = makeCommandButton(QStringLiteral("最大化图像"));

    connect(rotateLeftButton, &QPushButton::clicked, imageView_, &ImageView::rotateLeft);
    connect(rotateRightButton, &QPushButton::clicked, imageView_, &ImageView::rotateRight);
    connect(flipHButton, &QPushButton::clicked, imageView_, &ImageView::flipHorizontal);
    connect(flipVButton, &QPushButton::clicked, imageView_, &ImageView::flipVertical);
    connect(zoomOutButton, &QPushButton::clicked, imageView_, &ImageView::zoomOut);
    connect(zoomInButton, &QPushButton::clicked, imageView_, &ImageView::zoomIn);
    connect(fitButton, &QPushButton::clicked, imageView_, &ImageView::fitToWindow);
    connect(resetButton, &QPushButton::clicked, imageView_, &ImageView::resetView);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveDisplayImage);
    connect(maximizeButton, &QPushButton::clicked, this, &MainWindow::toggleImageMaximized);

    layout->addWidget(rotateLeftButton, 0, 0);
    layout->addWidget(rotateRightButton, 0, 1);
    layout->addWidget(flipHButton, 1, 0);
    layout->addWidget(flipVButton, 1, 1);
    layout->addWidget(zoomOutButton, 2, 0);
    layout->addWidget(zoomInButton, 2, 1);
    layout->addWidget(fitButton, 3, 0);
    layout->addWidget(resetButton, 3, 1);
    layout->addWidget(saveButton, 4, 0, 1, 2);
    layout->addWidget(maximizeButton, 5, 0, 1, 2);

    return group;
}

QWidget* MainWindow::createWindowLevelPanel() {
    auto* group = new QGroupBox(QStringLiteral("窗宽窗位"), this);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);

    autoWindowCheck_ = new QCheckBox(QStringLiteral("自动窗宽窗位"), group);
    autoWindowCheck_->setChecked(true);

    centerSlider_ = new QSlider(Qt::Horizontal, group);
    centerSlider_->setRange(0, 65535);
    centerSlider_->setValue(4096);
    widthSlider_ = new QSlider(Qt::Horizontal, group);
    widthSlider_->setRange(1, 65535);
    widthSlider_->setValue(4096);

    centerSpin_ = new QSpinBox(group);
    centerSpin_->setRange(0, 65535);
    centerSpin_->setValue(4096);
    widthSpin_ = new QSpinBox(group);
    widthSpin_->setRange(1, 65535);
    widthSpin_->setValue(4096);

    connect(autoWindowCheck_, &QCheckBox::toggled, this, [this]() {
        clearReplayDisplayCaches();
        scheduleImageRefresh(false);
    });
    connect(centerSlider_, &QSlider::valueChanged, this, &MainWindow::updateWindowLevel);
    connect(widthSlider_, &QSlider::valueChanged, this, &MainWindow::updateWindowLevel);
    connect(centerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), centerSlider_, &QSlider::setValue);
    connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), widthSlider_, &QSlider::setValue);

    layout->addWidget(autoWindowCheck_, 0, 0, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("窗位")), 1, 0);
    layout->addWidget(centerSlider_, 1, 1);
    layout->addWidget(centerSpin_, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("窗宽")), 2, 0);
    layout->addWidget(widthSlider_, 2, 1);
    layout->addWidget(widthSpin_, 2, 2);

    return group;
}

QWidget* MainWindow::createImageInfoPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像信息"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    pixelInfoLabel_ = new QLabel(QStringLiteral("像素值: --"), group);
    pixelInfoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    roiInfoLabel_ = new QLabel(QStringLiteral("ROI 信息: Ctrl+左键分析，Shift+左键重算窗宽窗位"), group);
    roiInfoLabel_->setWordWrap(true);
    roiInfoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(pixelInfoLabel_);
    layout->addWidget(roiInfoLabel_);
    return group;
}

void MainWindow::createLogDock() {
    logDock_ = new QDockWidget(QStringLiteral("运行日志"), this);
    logDock_->setObjectName(QStringLiteral("runtimeLogDock"));
    logDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    logView_ = new QPlainTextEdit(logDock_);
    logView_->setObjectName(QStringLiteral("runtimeLogView"));
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView_->document()->setMaximumBlockCount(2000);
    logDock_->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    logDock_->hide();

    if (viewMenu_ != nullptr) {
        QAction* toggleAction = logDock_->toggleViewAction();
        toggleAction->setText(QStringLiteral("运行日志"));
        viewMenu_->addAction(toggleAction);
    }
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* openAction = fileMenu->addAction(QStringLiteral("打开 TiRaw 图像"));
    auto* replayAction = fileMenu->addAction(QStringLiteral("回放 TiRaw 序列"));
    auto* stopReplayAction = fileMenu->addAction(QStringLiteral("停止图像回放"));
    auto* saveAction = fileMenu->addAction(QStringLiteral("保存显示图像"));
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QStringLiteral("退出"));

    connect(openAction, &QAction::triggered, this, &MainWindow::openImage);
    connect(replayAction, &QAction::triggered, this, &MainWindow::startImageReplay);
    connect(stopReplayAction, &QAction::triggered, this, &MainWindow::stopImageReplay);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveDisplayImage);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* serialMenu = menuBar()->addMenu(QStringLiteral("RS422"));
    auto* portLabelAction = serialMenu->addAction(QStringLiteral("端口"));
    portLabelAction->setEnabled(false);
    portCombo_ = new QComboBox(serialMenu);
    populateSerialPorts(portCombo_);
    if (!settings_->serialPort().isEmpty()) {
        portCombo_->setCurrentText(settings_->serialPort());
    }
    auto* portAction = new QWidgetAction(serialMenu);
    portAction->setDefaultWidget(portCombo_);
    serialMenu->addAction(portAction);

    auto* baudLabelAction = serialMenu->addAction(QStringLiteral("波特率"));
    baudLabelAction->setEnabled(false);
    baudSpin_ = new QSpinBox(serialMenu);
    baudSpin_->setRange(1200, 3000000);
    baudSpin_->setValue(settings_->serialBaudRate());
    auto* baudAction = new QWidgetAction(serialMenu);
    baudAction->setDefaultWidget(baudSpin_);
    serialMenu->addAction(baudAction);

    serialMenu->addSeparator();
    refreshPortsAction_ = serialMenu->addAction(QStringLiteral("刷新端口"), this, [this]() {
        populateSerialPorts(portCombo_);
    });
    connectSerialAction_ = serialMenu->addAction(
        QStringLiteral("连接"), this, &MainWindow::connectSerial);
    disconnectSerialAction_ = serialMenu->addAction(
        QStringLiteral("断开"), this, &MainWindow::disconnectSerial);

    auto* commandMenu = menuBar()->addMenu(QStringLiteral("PA/FPGA"));
    struct MenuCommand {
        PaProtocol::Command command;
        const char* text;
    };
    const MenuCommand commands[] = {
        {PaProtocol::Command::Ping, "心跳"},
        {PaProtocol::Command::Status, "读取状态"},
        {PaProtocol::Command::LoadTemplate, "加载模板"},
        {PaProtocol::Command::ConfigTemplate, "配置模板"},
        {PaProtocol::Command::MakeOffset, "生成 Offset"},
        {PaProtocol::Command::MakeGain, "生成 Gain"},
        {PaProtocol::Command::StartCorrection, "启动校正"},
        {PaProtocol::Command::SendImage, "手动上图"},
        {PaProtocol::Command::Quit, "退出 ARM"},
    };
    for (const MenuCommand& item : commands) {
        QAction* action = commandMenu->addAction(QString::fromUtf8(item.text), this, [this, item]() {
            sendCommand(item.command);
        });
        deviceCommandActions_.push_back(action);
    }

    viewMenu_ = menuBar()->addMenu(QStringLiteral("视图"));
    imageMaximizeAction_ = viewMenu_->addAction(QStringLiteral("图像最大化"));
    imageMaximizeAction_->setCheckable(true);
    imageMaximizeAction_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(imageMaximizeAction_, &QAction::triggered, this, &MainWindow::toggleImageMaximized);

    menuBar()->addMenu(QStringLiteral("校准"));
    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("工具"));
    toolsMenu->addAction(QStringLiteral("命令超时设置"), this, &MainWindow::configureCommandTimeout);
    toolsMenu->addAction(QStringLiteral("导出诊断信息"), this, &MainWindow::exportDiagnostics);
    menuBar()->addMenu(QStringLiteral("帮助"));
}

void MainWindow::createStatusBar() {
    modelLabel_ = new QLabel(QStringLiteral("新型号 PA 专用"));
    serialLabel_ = new QLabel(QStringLiteral("SN: --"));
    connectionLabel_ = new QLabel(QStringLiteral("RS422: 未连接"));
    modeLabel_ = new QLabel(QStringLiteral("工作模式: Idle"));
    imageLabel_ = new QLabel(QStringLiteral("当前图像: --"));
    progressLabel_ = new QLabel(QStringLiteral("缩放: --"));
    fpsLabel_ = new QLabel(QStringLiteral("fps: 0.00"));

    const auto configureStatusLabel = [](QLabel* label) {
        label->setObjectName(QStringLiteral("statusLabel"));
        label->setContentsMargins(6, 2, 6, 2);
    };
    configureStatusLabel(modelLabel_);
    configureStatusLabel(serialLabel_);
    configureStatusLabel(connectionLabel_);
    configureStatusLabel(modeLabel_);
    configureStatusLabel(imageLabel_);
    configureStatusLabel(progressLabel_);
    configureStatusLabel(fpsLabel_);

    statusBar()->addWidget(modelLabel_);
    statusBar()->addWidget(serialLabel_);
    statusBar()->addWidget(connectionLabel_);
    statusBar()->addWidget(modeLabel_);
    statusBar()->addWidget(imageLabel_, 1);
    statusBar()->addWidget(progressLabel_);
    statusBar()->addWidget(fpsLabel_);
}

void MainWindow::handleImageListSelection(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    stopImageReplay();
    QString error;
    if (!imageSession_->loadFile(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        logService_->error(QStringLiteral("IMAGE"),
            QStringLiteral("切换图像失败: %1, %2").arg(path, error));
        return;
    }
    imageListPanel_->ensureThumbnail(path, imageSession_->image());
    showCurrentSessionImage(path, true);
}

void MainWindow::handleImagesRemoved(int count, const QString& nextPath) {
    stopImageReplay();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("从图像列表移除 %1 项（源文件未删除）").arg(count));
    if (nextPath.isEmpty()) {
        clearCurrentImage();
    } else {
        handleImageListSelection(nextPath);
    }
}

void MainWindow::exportImage(const QString& sourcePath, const QString& formatId) {
    if (sourcePath.isEmpty()) {
        return;
    }

    if (!imageSession_->hasImage() || normalizedImagePath(imageSession_->image().path()) != sourcePath) {
        QString loadError;
        if (!imageSession_->loadFile(sourcePath, &loadError)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), loadError);
            return;
        }
    }

    QString exportDirectory = settings_->lastExportDirectory();
    if (exportDirectory.isEmpty() || !QDir(exportDirectory).exists()) {
        exportDirectory = QFileInfo(sourcePath).absolutePath();
    }

    ImageExportFormat format;
    if (!ImageExportService::findFormat(formatId, &format)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("不支持的导出格式"));
        return;
    }
    const QString defaultName = QFileInfo(sourcePath).completeBaseName()
        + QStringLiteral("_export.") + format.suffix;
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出当前图像"),
        QDir(exportDirectory).filePath(defaultName),
        format.fileFilter);
    if (outputPath.isEmpty()) {
        return;
    }
    outputPath = ImageExportService::ensureFileSuffix(outputPath, formatId);

    QString error;
    if (!ImageExportService::exportImage(
            imageSession_->image(),
            centerSpin_->value(),
            widthSpin_->value(),
            formatId,
            outputPath,
            &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    settings_->setLastExportDirectory(QFileInfo(outputPath).absolutePath());
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("导出图像: %1 -> %2").arg(sourcePath, outputPath));
}

void MainWindow::showCurrentSessionImage(const QString& source, bool resetViewState) {
    if (!imageSession_->hasImage()) {
        return;
    }

    const TiRawImage& image = imageSession_->image();
    clearReplayDisplayCaches();
    imageLabel_->setText(QStringLiteral("%1 x %2").arg(image.width()).arg(image.height()));
    fpsLabel_->setText(QStringLiteral("fps: 0.00"));
    pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
    updateFullImageInfo();
    const WindowLevelResult autoWindow = imageSession_->autoWindowLevel();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("打开图像: %1, %2x%3, min=%4 max=%5, auto center=%6 width=%7")
            .arg(source)
            .arg(image.width())
            .arg(image.height())
            .arg(image.minValue())
            .arg(image.maxValue())
            .arg(autoWindow.center)
            .arg(autoWindow.width));
    refreshImage(resetViewState);
}

void MainWindow::clearCurrentImage() {
    imageRefreshTimer_.stop();
    imageInfoRefreshTimer_.stop();
    imageSession_->clear();
    imageView_->setImage(QImage());
    imageLabel_->setText(QStringLiteral("当前图像: --"));
    progressLabel_->setText(QStringLiteral("缩放: --"));
    fpsLabel_->setText(QStringLiteral("fps: 0.00"));
    pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
    roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
}

void MainWindow::clearReplayDisplayCaches() {
    replayDisplayCache_.clear();
    if (imageView_ != nullptr) {
        imageView_->clearPixmapCache();
    }
}

void MainWindow::scheduleImageRefresh(bool resetViewState) {
    resetViewStateOnRefresh_ = resetViewStateOnRefresh_ || resetViewState;
    if (!imageRefreshTimer_.isActive()) {
        imageRefreshTimer_.start(kStaticImageRefreshIntervalMs);
    }
}

void MainWindow::refreshImage(bool resetViewState) {
    if (!imageSession_->hasImage()) {
        return;
    }

    if (autoWindowCheck_->isChecked()) {
        const WindowLevelResult autoWindow = imageSession_->autoWindowLevel();
        if (!autoWindow.valid) {
            return;
        }
        const QSignalBlocker blockCenterSlider(centerSlider_);
        const QSignalBlocker blockWidthSlider(widthSlider_);
        const QSignalBlocker blockCenterSpin(centerSpin_);
        const QSignalBlocker blockWidthSpin(widthSpin_);

        centerSlider_->setValue(autoWindow.center);
        centerSpin_->setValue(autoWindow.center);
        widthSlider_->setValue(autoWindow.width);
        widthSpin_->setValue(autoWindow.width);
    }

    QImage display;
    if (replaySource_->isRunning()) {
        const QString cacheKey = imageSession_->image().path()
            + QStringLiteral("\n%1\n%2").arg(centerSpin_->value()).arg(widthSpin_->value());
        if (const QImage* cached = replayDisplayCache_.object(cacheKey)) {
            display = *cached;
        } else {
            display = imageSession_->render(centerSpin_->value(), widthSpin_->value());
            replayDisplayCache_.insert(cacheKey, new QImage(display));
        }
    } else {
        display = imageSession_->render(centerSpin_->value(), widthSpin_->value());
    }
    imageView_->setImage(display, resetViewState);
}

void MainWindow::updatePixelInfo(const QPoint& imagePoint) {
    if (pixelInfoLabel_ == nullptr) {
        return;
    }

    quint16 value = 0;
    if (!imageSession_->image().pixelValue(imagePoint.x(), imagePoint.y(), &value)) {
        pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
        return;
    }

    pixelInfoLabel_->setText(QStringLiteral("像素值(%1,%2):%3")
                                 .arg(imagePoint.x())
                                 .arg(imagePoint.y())
                                 .arg(value));
}

void MainWindow::showRoiInfo(const TiRawImage::RoiStats& stats) {
    if (roiInfoLabel_ == nullptr) {
        return;
    }

    const TiRawImage& image = imageSession_->image();
    const bool fullImage = stats.rect == QRect(0, 0, image.width(), image.height());
    roiInfoLabel_->setText(QStringLiteral(
                               "区域 %1\n"
                               "范围 (%2,%3)-(%4,%5)\n"
                               "像素 %6\n"
                               "均值 %7\n"
                               "最小/最大 %8 / %9\n"
                               "标准差 %10\n"
                               "行噪声 %11")
                               .arg(fullImage ? QStringLiteral("全图") : QStringLiteral("ROI"))
                               .arg(stats.rect.left())
                               .arg(stats.rect.top())
                               .arg(stats.rect.right())
                               .arg(stats.rect.bottom())
                               .arg(stats.pixelCount)
                               .arg(QString::number(stats.mean, 'f', 4))
                               .arg(stats.min)
                               .arg(stats.max)
                               .arg(QString::number(stats.stddev, 'f', 4))
                               .arg(QString::number(stats.rowNoise, 'f', 4)));
}

void MainWindow::updateRoiInfo(const QRect& imageRect) {
    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        if (roiInfoLabel_ != nullptr) {
            roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
        }
        return;
    }
    showRoiInfo(stats);
}

void MainWindow::updateFullImageInfo() {
    if (!imageSession_->hasImage()) {
        if (roiInfoLabel_ != nullptr) {
            roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
        }
        return;
    }

    const TiRawImage& image = imageSession_->image();
    if (replaySource_->isRunning()) {
        const QString cacheKey = image.path();
        auto cached = replayFullImageStatsCache_.constFind(cacheKey);
        if (cached == replayFullImageStatsCache_.constEnd()) {
            TiRawImage::RoiStats stats;
            if (!imageSession_->roiStats(QRect(0, 0, image.width(), image.height()), &stats)) {
                return;
            }
            cached = replayFullImageStatsCache_.insert(cacheKey, stats);
        }
        showRoiInfo(cached.value());
        return;
    }
    updateRoiInfo(QRect(0, 0, image.width(), image.height()));
}

void MainWindow::handleAnalysisRoi(const QRect& imageRect) {
    updateRoiInfo(imageRect);

    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("分析测试"));
    dialog.resize(1280, 720);

    auto* root = new QHBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 12, 12);
    root->setSpacing(12);

    auto* previewLabel = new QLabel(&dialog);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumSize(700, 640);
    previewLabel->setStyleSheet(QStringLiteral("background:#08090a;"));

    const QImage display = imageSession_->render(centerSpin_->value(), widthSpin_->value());
    previewLabel->setPixmap(drawAnalysisPreview(display, stats.rect, previewLabel->minimumSize()));

    auto* rightPanel = new QWidget(&dialog);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 24, 0, 0);
    rightLayout->setSpacing(18);

    MtfAnalysisResult analysis;
    if (!imageSession_->analyzeMtf(stats.rect, &analysis)) {
        QMessageBox::warning(this, QStringLiteral("分析失败"), QStringLiteral("当前 ROI 无法生成 MTF 分析结果"));
        return;
    }

    constexpr int chartWidth = 430;
    constexpr int chartHeight = 185;
    auto* esfLabel = new QLabel(&dialog);
    auto* lsfLabel = new QLabel(&dialog);
    auto* mtfLabel = new QLabel(&dialog);
    esfLabel->setPixmap(drawLineChart(QStringLiteral("Edge Spread Function"), QStringLiteral("ESF (ADC/mm)"), QStringLiteral("Distance (mm)"), analysis.esf.y, {chartWidth, chartHeight}));
    lsfLabel->setPixmap(drawLineChart(QStringLiteral("Line Spread Function"), QStringLiteral("LSF (ADC/mm)"), QStringLiteral("Distance (mm)"), analysis.lsf.y, {chartWidth, chartHeight}));
    mtfLabel->setPixmap(drawLineChart(QStringLiteral("Modulation Transfer Function (Pixel Size:100um)"), QStringLiteral("MTF"), QStringLiteral("Spatial Frequency (lp/mm)"), analysis.mtf.y, {chartWidth, chartHeight}));

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* exportButton = closeButtons->addButton(QStringLiteral("导出 CSV"), QDialogButtonBox::ActionRole);
    connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, &dialog, [this, analysis]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("导出分析曲线"),
            defaultImageDirectory(*settings_));
        if (directory.isEmpty()) {
            return;
        }

        QString error;
        if (!imageSession_->exportMtf(directory, analysis, &error)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
            return;
        }
        logService_->info(QStringLiteral("ANALYSIS"),
            QStringLiteral("导出分析曲线: %1").arg(directory));
    });

    rightLayout->addWidget(esfLabel);
    rightLayout->addWidget(lsfLabel);
    rightLayout->addWidget(mtfLabel);
    rightLayout->addStretch(1);
    rightLayout->addWidget(closeButtons);

    root->addWidget(previewLabel, 1);
    root->addWidget(rightPanel);

    dialog.exec();
}

void MainWindow::applyWindowLevelFromRoi(const QRect& imageRect) {
    updateRoiInfo(imageRect);

    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        return;
    }

    const WindowLevelResult windowLevel = imageSession_->roiWindowLevel(stats.rect);
    if (!windowLevel.valid) {
        return;
    }

    {
        const QSignalBlocker blockAuto(autoWindowCheck_);
        const QSignalBlocker blockCenterSlider(centerSlider_);
        const QSignalBlocker blockWidthSlider(widthSlider_);
        const QSignalBlocker blockCenterSpin(centerSpin_);
        const QSignalBlocker blockWidthSpin(widthSpin_);

        autoWindowCheck_->setChecked(false);
        centerSlider_->setValue(windowLevel.center);
        centerSpin_->setValue(windowLevel.center);
        widthSlider_->setValue(windowLevel.width);
        widthSpin_->setValue(windowLevel.width);
    }

    clearReplayDisplayCaches();
    refreshImage(false);
}

void MainWindow::handlePresentedFrame(const ImageFrame& frame) {
    const bool resetViewState = !imageSession_->hasImage()
        || imageSession_->image().width() != frame.image.width()
        || imageSession_->image().height() != frame.image.height();
    QString error;
    if (!imageSession_->setFrame(frame, &error)) {
        logService_->error(QStringLiteral("IMAGE"),
            QStringLiteral("接收图像帧失败: %1").arg(error));
        return;
    }

    imageLabel_->setText(QStringLiteral("%1 x %2").arg(frame.image.width()).arg(frame.image.height()));
    imageListPanel_->ensureThumbnail(frame.image.path(), frame.image);
    imageListPanel_->setCurrentPath(frame.image.path());
    // 呈现控制器已经完成限速和丢帧处理，这里只负责同步界面状态。
    imageRefreshTimer_.stop();
    const bool shouldResetView = resetViewStateOnRefresh_ || resetViewState;
    resetViewStateOnRefresh_ = false;
    refreshImage(shouldResetView);
    if ((!imageInfoTimer_.isValid() || imageInfoTimer_.elapsed() >= 1000)
        && !imageInfoRefreshTimer_.isActive()) {
        imageInfoTimer_.restart();
        imageInfoRefreshTimer_.start(1);
    }
}

void MainWindow::updateDeviceState(PaDeviceState state) {
    const bool connected = deviceController_->isConnected();
    const bool commandEnabled = connected && state != PaDeviceState::Busy;

    portCombo_->setEnabled(!connected);
    baudSpin_->setEnabled(!connected);
    refreshPortsAction_->setEnabled(!connected);
    connectSerialAction_->setEnabled(!connected);
    disconnectSerialAction_->setEnabled(connected);
    for (QAction* action : deviceCommandActions_) {
        action->setEnabled(commandEnabled);
    }
    manualImageButton_->setEnabled(commandEnabled);
    statusButton_->setEnabled(commandEnabled);

    switch (state) {
    case PaDeviceState::Disconnected:
        connectionLabel_->setText(QStringLiteral("RS422: 未连接"));
        break;
    case PaDeviceState::Ready:
        connectionLabel_->setText(QStringLiteral("RS422: 已连接"));
        break;
    case PaDeviceState::Busy:
        connectionLabel_->setText(QStringLiteral("RS422: 执行中"));
        break;
    case PaDeviceState::Error:
        connectionLabel_->setText(QStringLiteral("RS422: 错误"));
        break;
    }
}

void MainWindow::updateDeviceStatus(const PaDeviceStatus& status) {
    if (!status.valid) {
        return;
    }
    modeLabel_->setText(QStringLiteral("wr:%1/%2 corr:%3/%4")
                            .arg(status.writeState)
                            .arg(status.writeEnd)
                            .arg(status.correctionState)
                            .arg(status.correctionEnd));
}
