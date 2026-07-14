#include "MainWindow.h"

#include <QAction>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QInputDialog>
#include <QImageWriter>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kImagePathRole = Qt::UserRole + 1;
constexpr int kThumbnailReadyRole = Qt::UserRole + 2;
constexpr int kStaticImageRefreshIntervalMs = 33;
constexpr qint64 kNanosecondsPerSecond = 1000000000LL;
constexpr qint64 kNanosecondsPerMillisecond = 1000000LL;
constexpr QSize kThumbnailSize(134, 82);
constexpr QSize kImageListItemSize(146, 112);

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

QString defaultImageDirectory() {
    QSettings settings;
    const QString remembered = settings.value(QStringLiteral("paths/lastImageDirectory")).toString();
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

QIcon makeImageThumbnail(const TiRawImage& image) {
    QPixmap thumbnail(kThumbnailSize);
    thumbnail.fill(Qt::white);
    if (!image.isValid()) {
        return QIcon(thumbnail);
    }

    const QSize scaledSize = QSize(image.width(), image.height()).scaled(kThumbnailSize, Qt::KeepAspectRatio);
    const QImage scaled = image.toDisplayImage(
        false, image.autoWindowCenter(), image.autoWindowWidth(), scaledSize);
    QPainter painter(&thumbnail);
    painter.drawImage(
        QPoint((thumbnail.width() - scaled.width()) / 2, (thumbnail.height() - scaled.height()) / 2),
        scaled);
    painter.setPen(QColor(145, 156, 168));
    painter.drawRect(thumbnail.rect().adjusted(0, 0, -1, -1));
    return QIcon(thumbnail);
}

QString exportSuffix(const QString& formatId) {
    if (formatId == QStringLiteral("tiraw")) {
        return QStringLiteral("tiraw");
    }
    if (formatId == QStringLiteral("raw")) {
        return QStringLiteral("raw");
    }
    if (formatId == QStringLiteral("jpeg")) {
        return QStringLiteral("jpg");
    }
    if (formatId == QStringLiteral("tiff")) {
        return QStringLiteral("tif");
    }
    return formatId;
}

QString exportFilter(const QString& formatId) {
    if (formatId == QStringLiteral("tiraw")) {
        return QStringLiteral("TiRayRaw (*.tiraw)");
    }
    if (formatId == QStringLiteral("raw")) {
        return QStringLiteral("RAW 16-bit Little Endian (*.raw)");
    }
    if (formatId == QStringLiteral("png")) {
        return QStringLiteral("PNG Image (*.png)");
    }
    if (formatId == QStringLiteral("tiff")) {
        return QStringLiteral("TIFF Image (*.tif *.tiff)");
    }
    if (formatId == QStringLiteral("bmp")) {
        return QStringLiteral("BMP Image (*.bmp)");
    }
    return QStringLiteral("JPEG Image (*.jpg *.jpeg)");
}

QByteArray imageWriterFormat(const QString& formatId) {
    if (formatId == QStringLiteral("jpeg")) {
        return QByteArrayLiteral("jpeg");
    }
    return formatId.toLatin1();
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

MainWindow::MainWindow(std::shared_ptr<IImageAlgorithms> algorithms, QWidget* parent)
    : QMainWindow(parent) {
    imageSession_ = std::make_unique<ImageSession>(std::move(algorithms), this);
    replaySource_ = new LocalReplaySource(this);
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
    imageListPanel_ = createImageListPanel();
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
    createStatusBar();

    connect(&serial_, &SerialClient::lineReceived, this, &MainWindow::handleLineReceived);
    connect(&serial_, &SerialClient::errorOccurred, this, &MainWindow::handleSerialError);
    connect(&serial_, &SerialClient::connectionChanged, this, [this](bool connected) {
        connectionLabel_->setText(connected ? QStringLiteral("RS422: 已连接") : QStringLiteral("RS422: 未连接"));
    });
    connect(imageView_, &ImageView::zoomChanged, this, [this](int percent) {
        progressLabel_->setText(QStringLiteral("缩放: %1%").arg(percent));
    });
    connect(imageView_, &ImageView::pixelHovered, this, &MainWindow::updatePixelInfo);
    connect(imageView_, &ImageView::analysisRoiSelected, this, &MainWindow::handleAnalysisRoi);
    connect(imageView_, &ImageView::windowLevelRoiSelected, this, &MainWindow::applyWindowLevelFromRoi);
    connect(imageView_, &ImageView::roiCleared, this, &MainWindow::updateFullImageInfo);
    connect(replaySource_, &LocalReplaySource::frameReady, this, &MainWindow::handleImageFrame);
    connect(replaySource_, &LocalReplaySource::sourceError, this, [this](const QString& message) {
        appendLog(message);
    });
    connect(replaySource_, &LocalReplaySource::runningChanged, this, [this](bool running) {
        if (!running) {
            const ImageSourceStats stats = replaySource_->stats();
            appendLog(QStringLiteral("图像回放停止: 输入 %1 帧，显示 %2 帧，显示丢帧 %3，加载失败 %4")
                          .arg(stats.deliveredFrames)
                          .arg(replayDisplayedFrames_)
                          .arg(replayDroppedDisplayFrames_)
                          .arg(stats.failedFrames));
        }
    });
    imageRefreshTimer_.setSingleShot(true);
    imageRefreshTimer_.setInterval(kStaticImageRefreshIntervalMs);
    connect(&imageRefreshTimer_, &QTimer::timeout, this, [this]() {
        refreshImage(resetViewStateOnRefresh_);
        if (replaySource_->isRunning() && imagePresentationClock_.isValid()) {
            const qint64 framePeriodNs = kNanosecondsPerSecond / std::max(1, replayTargetFps_);
            const qint64 nowNs = imagePresentationClock_.nsecsElapsed();
            do {
                nextImagePresentationNs_ += framePeriodNs;
            } while (nextImagePresentationNs_ <= nowNs);
        }
        resetViewStateOnRefresh_ = false;
        if (replayFramePending_) {
            replayFramePending_ = false;
            ++replayDisplayedFrames_;
            ++replayFpsFrameCount_;
            const qint64 elapsedMs = replayFpsTimer_.elapsed();
            if (elapsedMs >= 500) {
                const double fps = replayFpsFrameCount_ * 1000.0 / elapsedMs;
                fpsLabel_->setText(QStringLiteral("显示 fps: %1 / 目标 %2")
                                       .arg(fps, 0, 'f', 2)
                                       .arg(replayTargetFps_));
                replayFpsTimer_.restart();
                replayFpsFrameCount_ = 0;
            }
        }
    });
    imageInfoRefreshTimer_.setSingleShot(true);
    connect(&imageInfoRefreshTimer_, &QTimer::timeout, this, &MainWindow::updateFullImageInfo);
}

void MainWindow::openImage() {
    stopImageReplay();
    const QString initialDirectory = defaultImageDirectory();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("打开 TiRaw 图像（可多选）"),
        initialDirectory,
        QStringLiteral("TiRayRaw (*.tiraw);;All Files (*)"));
    if (paths.isEmpty()) {
        return;
    }

    QSettings().setValue(QStringLiteral("paths/lastImageDirectory"), QFileInfo(paths.first()).absolutePath());

    TiRawImage lastImage;
    QString lastPath;
    QListWidgetItem* lastItem = nullptr;
    QStringList failures;
    const QSignalBlocker blockList(imageList_);
    for (const QString& selectedPath : paths) {
        const QString path = normalizedImagePath(selectedPath);
        TiRawImage image;
        QString error;
        if (!image.load(path, &error)) {
            failures.push_back(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
            continue;
        }

        lastItem = addOrUpdateImageListItem(path, &image);
        lastImage = std::move(image);
        lastPath = path;
    }

    if (lastItem != nullptr) {
        ImageFrame frame;
        frame.image = std::move(lastImage);
        frame.sourceName = QFileInfo(lastPath).fileName();
        frame.receivedAt = QDateTime::currentDateTimeUtc();
        QString error;
        if (imageSession_->setFrame(frame, &error)) {
            imageList_->setCurrentItem(lastItem);
            imageList_->scrollToItem(lastItem);
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
        defaultImageDirectory(),
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
    QSettings().setValue(QStringLiteral("paths/lastImageDirectory"), QFileInfo(paths.first()).absolutePath());
    replaySource_->setPlaylist(paths);
    replaySource_->setIntervalMs(std::max(1, 1000 / fps));
    replaySource_->setLoopEnabled(true);
    {
        const QSignalBlocker blockList(imageList_);
        imageList_->clear();
        for (const QString& path : paths) {
            addOrUpdateImageListItem(path);
        }
    }

    clearReplayDisplayCaches();
    replayFullImageStatsCache_.clear();
    imagePresentationClock_.invalidate();
    nextImagePresentationNs_ = 0;
    replayTargetFps_ = fps;
    imageInfoTimer_.invalidate();
    replayFpsFrameCount_ = 0;
    replayDisplayedFrames_ = 0;
    replayDroppedDisplayFrames_ = 0;
    replayFramePending_ = false;
    fpsLabel_->setText(QStringLiteral("显示 fps: 0.00 / 目标 %1").arg(replayTargetFps_));
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
    imagePresentationClock_.start();
    nextImagePresentationNs_ = 0;
    replayFpsTimer_.start();
    appendLog(QStringLiteral("开始图像回放: 预加载 %1/%2 帧，耗时 %3 ms，目标 %4 fps")
                  .arg(loadedFrames)
                  .arg(paths.size())
                  .arg(preloadMs)
                  .arg(fps));
}

void MainWindow::stopImageReplay() {
    imageRefreshTimer_.stop();
    imageInfoRefreshTimer_.stop();
    if (replayFramePending_) {
        ++replayDroppedDisplayFrames_;
    }
    replayFramePending_ = false;
    resetViewStateOnRefresh_ = false;
    imagePresentationClock_.invalidate();
    nextImagePresentationNs_ = 0;
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

    QSettings settings;
    QString saveDirectory = settings.value(QStringLiteral("paths/lastSaveDirectory")).toString();
    if (saveDirectory.isEmpty() || !QDir(saveDirectory).exists()) {
        saveDirectory = defaultImageDirectory();
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
    settings.setValue(QStringLiteral("paths/lastSaveDirectory"), QFileInfo(path).absolutePath());
    appendLog(QStringLiteral("保存显示图像: %1").arg(path));
}

void MainWindow::connectSerial() {
    QString error;
    if (!serial_.open(portCombo_->currentText(), baudSpin_->value(), &error)) {
        QMessageBox::warning(this, QStringLiteral("串口打开失败"), error);
        appendLog(QStringLiteral("串口打开失败: %1").arg(error));
        return;
    }
    appendLog(QStringLiteral("串口已打开: %1 @ %2").arg(portCombo_->currentText()).arg(baudSpin_->value()));
}

void MainWindow::disconnectSerial() {
    serial_.close();
    appendLog(QStringLiteral("串口已关闭"));
}

void MainWindow::sendCommand(PaProtocol::Command command) {
    const QString text = PaProtocol::commandText(command);
    QString error;
    if (!serial_.sendLine(text, &error)) {
        appendLog(QStringLiteral("发送失败: %1, %2").arg(text, error));
        return;
    }
    appendLog(QStringLiteral("TX: %1").arg(text));
}

void MainWindow::handleLineReceived(const QString& line) {
    appendLog(QStringLiteral("RX: %1").arg(line));
    updateStatusFromResponse(PaProtocol::parseResponse(line));
}

void MainWindow::handleSerialError(const QString& message) {
    appendLog(QStringLiteral("串口错误: %1").arg(message));
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
    auto* manualButton = new QPushButton(QStringLiteral("手动上图"), bar);
    auto* stopButton = new QPushButton(QStringLiteral("停止上图"), bar);

    idleButton->setCheckable(true);
    idleButton->setChecked(true);
    continuousButton->setCheckable(true);
    idleButton->setProperty("role", "mode");
    continuousButton->setProperty("role", "mode");
    manualButton->setProperty("role", "primary");
    stopButton->setProperty("role", "stop");

    auto* modeGroup = new QButtonGroup(bar);
    modeGroup->setExclusive(true);
    modeGroup->addButton(idleButton);
    modeGroup->addButton(continuousButton);

    connect(manualButton, &QPushButton::clicked, this, [this]() {
        sendCommand(PaProtocol::Command::SendImage);
    });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        sendCommand(PaProtocol::Command::Status);
    });

    layout->addWidget(idleButton);
    layout->addWidget(continuousButton);
    layout->addSpacing(18);
    layout->addWidget(manualButton);
    layout->addWidget(stopButton);
    layout->addStretch(1);

    return bar;
}

QWidget* MainWindow::createImageListPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像列表"), this);
    group->setObjectName(QStringLiteral("imageListPanel"));
    group->setMinimumWidth(180);
    group->setMaximumWidth(300);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(group);
    imageList_ = new QListWidget(group);
    imageList_->setObjectName(QStringLiteral("imageList"));
    imageList_->setViewMode(QListView::IconMode);
    imageList_->setFlow(QListView::TopToBottom);
    imageList_->setWrapping(false);
    imageList_->setMovement(QListView::Static);
    imageList_->setResizeMode(QListView::Adjust);
    imageList_->setIconSize(kThumbnailSize);
    imageList_->setGridSize(kImageListItemSize);
    imageList_->setTextElideMode(Qt::ElideMiddle);
    imageList_->setWordWrap(false);
    imageList_->setSpacing(3);
    imageList_->setUniformItemSizes(true);
    imageList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    imageList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(imageList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        handleImageListSelection(current);
    });

    auto* removeButton = new QPushButton(QStringLiteral("移除选中图像"), group);
    removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    removeButton->setToolTip(QStringLiteral("从列表移除选中图像，不删除源文件"));
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedImages);

    auto* removeAction = new QAction(QStringLiteral("移除选中图像"), imageList_);
    removeAction->setShortcut(QKeySequence::Delete);
    removeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    imageList_->addAction(removeAction);
    connect(removeAction, &QAction::triggered, this, &MainWindow::removeSelectedImages);
    connect(imageList_, &QListWidget::customContextMenuRequested, this, [this, removeAction](const QPoint& position) {
        QListWidgetItem* clickedItem = imageList_->itemAt(position);
        if (clickedItem == nullptr) {
            return;
        }
        imageList_->setCurrentItem(clickedItem, QItemSelectionModel::ClearAndSelect);

        QMenu menu(imageList_);
        QMenu* exportMenu = menu.addMenu(QStringLiteral("导出当前图像"));
        const struct {
            const char* id;
            const char* label;
        } formats[] = {
            {"tiraw", "TiRaw（原始数据）"},
            {"raw", "RAW16 LE（无文件头）"},
            {"png", "PNG（显示图像）"},
            {"tiff", "TIFF（显示图像）"},
            {"bmp", "BMP（显示图像）"},
            {"jpeg", "JPEG（显示图像）"},
        };
        const QList<QByteArray> supportedFormats = QImageWriter::supportedImageFormats();
        for (const auto& format : formats) {
            const QString formatId = QString::fromLatin1(format.id);
            QAction* action = exportMenu->addAction(QString::fromUtf8(format.label));
            if (formatId != QStringLiteral("tiraw") && formatId != QStringLiteral("raw")) {
                action->setEnabled(supportedFormats.contains(imageWriterFormat(formatId)));
            }
            connect(action, &QAction::triggered, this, [this, formatId]() {
                exportCurrentImage(formatId);
            });
        }
        menu.addSeparator();
        menu.addAction(removeAction);
        menu.exec(imageList_->viewport()->mapToGlobal(position));
    });

    layout->addWidget(imageList_);
    layout->addWidget(removeButton);
    return group;
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
    auto* portAction = new QWidgetAction(serialMenu);
    portAction->setDefaultWidget(portCombo_);
    serialMenu->addAction(portAction);

    auto* baudLabelAction = serialMenu->addAction(QStringLiteral("波特率"));
    baudLabelAction->setEnabled(false);
    baudSpin_ = new QSpinBox(serialMenu);
    baudSpin_->setRange(1200, 3000000);
    baudSpin_->setValue(115200);
    auto* baudAction = new QWidgetAction(serialMenu);
    baudAction->setDefaultWidget(baudSpin_);
    serialMenu->addAction(baudAction);

    serialMenu->addSeparator();
    serialMenu->addAction(QStringLiteral("刷新端口"), this, [this]() {
        populateSerialPorts(portCombo_);
    });
    serialMenu->addAction(QStringLiteral("连接"), this, &MainWindow::connectSerial);
    serialMenu->addAction(QStringLiteral("断开"), this, &MainWindow::disconnectSerial);

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
        {PaProtocol::Command::WaitIrq, "等待中断"},
        {PaProtocol::Command::Quit, "退出 ARM"},
    };
    for (const MenuCommand& item : commands) {
        commandMenu->addAction(QString::fromUtf8(item.text), this, [this, item]() {
            sendCommand(item.command);
        });
    }

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    imageMaximizeAction_ = viewMenu->addAction(QStringLiteral("图像最大化"));
    imageMaximizeAction_->setCheckable(true);
    imageMaximizeAction_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(imageMaximizeAction_, &QAction::triggered, this, &MainWindow::toggleImageMaximized);

    menuBar()->addMenu(QStringLiteral("校准"));
    menuBar()->addMenu(QStringLiteral("工具"));
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

void MainWindow::appendLog(const QString& text) {
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    const QString line = QStringLiteral("[%1] %2").arg(now, text);
    if (logView_ != nullptr) {
        logView_->append(line);
    } else {
        qInfo().noquote() << line;
    }
}

QListWidgetItem* MainWindow::addOrUpdateImageListItem(const QString& path, const TiRawImage* image) {
    const QString normalizedPath = normalizedImagePath(path);
    QListWidgetItem* item = findImageListItem(normalizedPath);
    if (item == nullptr) {
        item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon), QFileInfo(normalizedPath).fileName());
        item->setData(kImagePathRole, normalizedPath);
        item->setData(kThumbnailReadyRole, false);
        item->setToolTip(normalizedPath);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setSizeHint(kImageListItemSize);
        imageList_->addItem(item);
    }
    if (image != nullptr && image->isValid()) {
        item->setIcon(makeImageThumbnail(*image));
        item->setData(kThumbnailReadyRole, true);
    }
    return item;
}

QListWidgetItem* MainWindow::findImageListItem(const QString& path) const {
    const QString normalizedPath = normalizedImagePath(path);
    for (int row = 0; row < imageList_->count(); ++row) {
        QListWidgetItem* item = imageList_->item(row);
        if (item->data(kImagePathRole).toString() == normalizedPath) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::handleImageListSelection(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }

    stopImageReplay();
    const QString path = item->data(kImagePathRole).toString();
    QString error;
    if (!imageSession_->loadFile(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        appendLog(QStringLiteral("切换图像失败: %1, %2").arg(path, error));
        return;
    }
    if (!item->data(kThumbnailReadyRole).toBool()) {
        item->setIcon(makeImageThumbnail(imageSession_->image()));
        item->setData(kThumbnailReadyRole, true);
    }
    showCurrentSessionImage(path, true);
}

void MainWindow::removeSelectedImages() {
    QList<QListWidgetItem*> selected = imageList_->selectedItems();
    if (selected.isEmpty() && imageList_->currentItem() != nullptr) {
        selected.push_back(imageList_->currentItem());
    }
    if (selected.isEmpty()) {
        return;
    }

    stopImageReplay();
    int nextRow = imageList_->currentRow();
    {
        const QSignalBlocker blockList(imageList_);
        for (QListWidgetItem* item : selected) {
            delete imageList_->takeItem(imageList_->row(item));
        }
        nextRow = std::min(nextRow, imageList_->count() - 1);
        if (nextRow >= 0) {
            imageList_->setCurrentRow(nextRow);
        }
    }

    appendLog(QStringLiteral("从图像列表移除 %1 项（源文件未删除）").arg(selected.size()));
    if (imageList_->count() == 0) {
        clearCurrentImage();
    } else {
        handleImageListSelection(imageList_->currentItem());
    }
}

void MainWindow::exportCurrentImage(const QString& formatId) {
    QListWidgetItem* item = imageList_->currentItem();
    if (item == nullptr) {
        return;
    }

    const QString sourcePath = item->data(kImagePathRole).toString();
    if (!imageSession_->hasImage() || normalizedImagePath(imageSession_->image().path()) != sourcePath) {
        QString loadError;
        if (!imageSession_->loadFile(sourcePath, &loadError)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), loadError);
            return;
        }
    }

    QSettings settings;
    QString exportDirectory = settings.value(QStringLiteral("paths/lastExportDirectory")).toString();
    if (exportDirectory.isEmpty() || !QDir(exportDirectory).exists()) {
        exportDirectory = QFileInfo(sourcePath).absolutePath();
    }
    const QString suffix = exportSuffix(formatId);
    const QString defaultName = QFileInfo(sourcePath).completeBaseName()
        + QStringLiteral("_export.") + suffix;
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出当前图像"),
        QDir(exportDirectory).filePath(defaultName),
        exportFilter(formatId));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QLatin1Char('.') + suffix;
    }

    QString error;
    bool exported = false;
    if (formatId == QStringLiteral("tiraw")) {
        exported = imageSession_->image().saveTiRaw(outputPath, &error);
    } else if (formatId == QStringLiteral("raw")) {
        exported = imageSession_->image().saveRaw16(outputPath, &error);
    } else {
        QImageWriter writer(outputPath, imageWriterFormat(formatId));
        if (formatId == QStringLiteral("jpeg")) {
            writer.setQuality(95);
        }
        exported = writer.write(imageSession_->render(centerSpin_->value(), widthSpin_->value()));
        if (!exported) {
            error = writer.errorString();
        }
    }

    if (!exported) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    settings.setValue(QStringLiteral("paths/lastExportDirectory"), QFileInfo(outputPath).absolutePath());
    appendLog(QStringLiteral("导出图像: %1 -> %2").arg(sourcePath, outputPath));
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
    appendLog(QStringLiteral("打开图像: %1, %2x%3, min=%4 max=%5, auto center=%6 width=%7")
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
        int delayMs = kStaticImageRefreshIntervalMs;
        if (replaySource_->isRunning()) {
            if (!imagePresentationClock_.isValid()) {
                delayMs = 0;
            } else {
                const qint64 remainingNs = nextImagePresentationNs_
                    - imagePresentationClock_.nsecsElapsed();
                delayMs = remainingNs <= 0
                    ? 0
                    : static_cast<int>((remainingNs + kNanosecondsPerMillisecond - 1)
                        / kNanosecondsPerMillisecond);
            }
        }
        imageRefreshTimer_.start(delayMs);
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
            defaultImageDirectory());
        if (directory.isEmpty()) {
            return;
        }

        QString error;
        if (!imageSession_->exportMtf(directory, analysis, &error)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
            return;
        }
        appendLog(QStringLiteral("导出分析曲线: %1").arg(directory));
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

void MainWindow::handleImageFrame(const ImageFrame& frame) {
    const bool resetViewState = !imageSession_->hasImage()
        || imageSession_->image().width() != frame.image.width()
        || imageSession_->image().height() != frame.image.height();
    QString error;
    if (!imageSession_->setFrame(frame, &error)) {
        appendLog(QStringLiteral("接收图像帧失败: %1").arg(error));
        return;
    }

    if (replayFramePending_) {
        ++replayDroppedDisplayFrames_;
    }
    replayFramePending_ = true;

    imageLabel_->setText(QStringLiteral("%1 x %2").arg(frame.image.width()).arg(frame.image.height()));
    QListWidgetItem* item = findImageListItem(frame.image.path());
    if (item != nullptr) {
        if (!item->data(kThumbnailReadyRole).toBool()) {
            item->setIcon(makeImageThumbnail(frame.image));
            item->setData(kThumbnailReadyRole, true);
        }
        const QSignalBlocker blockList(imageList_);
        imageList_->setCurrentItem(item);
        imageList_->scrollToItem(item);
    }
    scheduleImageRefresh(resetViewState);
    if ((!imageInfoTimer_.isValid() || imageInfoTimer_.elapsed() >= 1000)
        && !imageInfoRefreshTimer_.isActive()) {
        imageInfoTimer_.restart();
        imageInfoRefreshTimer_.start(1);
    }
}

void MainWindow::updateStatusFromResponse(const PaProtocol::Response& response) {
    if (response.ok) {
        connectionLabel_->setText(QStringLiteral("RS422: 正常"));
    } else if (response.error) {
        connectionLabel_->setText(QStringLiteral("RS422: 错误"));
    }

    if (response.keyword == "STATUS") {
        const QString wrState = response.kv.value("wr_state", "--");
        const QString wrEnd = response.kv.value("wr_end", "--");
        const QString corrState = response.kv.value("corr_state", "--");
        const QString corrEnd = response.kv.value("corr_end", "--");
        modeLabel_->setText(QStringLiteral("wr:%1/%2 corr:%3/%4").arg(wrState, wrEnd, corrState, corrEnd));
    } else if (response.keyword == "IRQ") {
        modeLabel_->setText(QStringLiteral("IRQ count=%1").arg(response.kv.value("count", "--")));
    }
}
