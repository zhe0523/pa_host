#include "MainWindow.h"

#include <QAction>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>

namespace {
QPushButton* makeCommandButton(const QString& text) {
    auto* button = new QPushButton(text);
    button->setMinimumHeight(30);
    return button;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("PA Host"));
    resize(1500, 860);

    createMenus();

    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    rootLayout->addWidget(createTopBar());

    auto* splitter = new QSplitter(Qt::Horizontal, root);
    splitter->addWidget(createImageListPanel());

    imageView_ = new ImageView(splitter);
    splitter->addWidget(imageView_);
    splitter->addWidget(createRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({170, 1080, 300});
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
}

void MainWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开 TiRaw 图像"),
        QStringLiteral("/home/zhe/sdk/app/windows/tidetector/CollectImage"),
        QStringLiteral("TiRayRaw (*.tiraw);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    TiRawImage image;
    if (!image.load(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        return;
    }

    currentRaw_ = image;
    imageList_->addItem(QFileInfo(path).fileName());
    imageList_->setCurrentRow(imageList_->count() - 1);
    imageLabel_->setText(QStringLiteral("%1 x %2").arg(currentRaw_.width()).arg(currentRaw_.height()));
    appendLog(QStringLiteral("打开图像: %1, %2x%3, min=%4 max=%5")
                  .arg(path)
                  .arg(currentRaw_.width())
                  .arg(currentRaw_.height())
                  .arg(currentRaw_.minValue())
                  .arg(currentRaw_.maxValue()));
    refreshImage();
}

void MainWindow::saveDisplayImage() {
    if (!imageView_->hasImage()) {
        QMessageBox::information(this, QStringLiteral("无图像"), QStringLiteral("当前没有可保存的显示图像"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存显示图像"),
        QStringLiteral("pa_host_display.png"),
        QStringLiteral("PNG Image (*.png)"));
    if (path.isEmpty()) {
        return;
    }

    if (!imageView_->savePng(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("图像保存失败"));
        return;
    }
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

void MainWindow::sendCustomCommand() {
    const QString text = customCommandEdit_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    QString error;
    if (!serial_.sendLine(text, &error)) {
        appendLog(QStringLiteral("发送失败: %1, %2").arg(text, error));
        return;
    }
    appendLog(QStringLiteral("TX: %1").arg(text));
    customCommandEdit_->clear();
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
    refreshImage();
}

QWidget* MainWindow::createTopBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* idleButton = new QPushButton(QStringLiteral("Idle"), bar);
    auto* continuousButton = new QPushButton(QStringLiteral("Continuous"), bar);
    auto* manualButton = new QPushButton(QStringLiteral("手动上图"), bar);
    auto* stopButton = new QPushButton(QStringLiteral("停止上图"), bar);

    idleButton->setCheckable(true);
    idleButton->setChecked(true);
    continuousButton->setCheckable(true);

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
    auto* layout = new QVBoxLayout(group);
    imageList_ = new QListWidget(group);
    imageList_->setMinimumWidth(150);
    layout->addWidget(imageList_);
    return group;
}

QWidget* MainWindow::createRightPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(createSerialPanel());
    layout->addWidget(createCommandPanel());
    layout->addWidget(createImageOpsPanel());
    layout->addWidget(createWindowLevelPanel());

    logView_ = new QTextEdit(panel);
    logView_->setReadOnly(true);
    logView_->setMinimumHeight(150);
    layout->addWidget(logView_, 1);

    return panel;
}

QWidget* MainWindow::createSerialPanel() {
    auto* group = new QGroupBox(QStringLiteral("RS422 通讯"), this);
    auto* layout = new QGridLayout(group);

    portCombo_ = new QComboBox(group);
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        portCombo_->addItem(info.systemLocation());
    }
    if (portCombo_->count() == 0) {
        portCombo_->addItems({QStringLiteral("/dev/ttyS0"), QStringLiteral("/dev/ttyS1"), QStringLiteral("/dev/ttyUSB0")});
    }

    baudSpin_ = new QSpinBox(group);
    baudSpin_->setRange(1200, 3000000);
    baudSpin_->setValue(115200);

    auto* connectButton = new QPushButton(QStringLiteral("连接"), group);
    auto* disconnectButton = new QPushButton(QStringLiteral("断开"), group);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectSerial);
    connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectSerial);

    layout->addWidget(new QLabel(QStringLiteral("端口")), 0, 0);
    layout->addWidget(portCombo_, 0, 1, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("波特率")), 1, 0);
    layout->addWidget(baudSpin_, 1, 1, 1, 2);
    layout->addWidget(connectButton, 2, 1);
    layout->addWidget(disconnectButton, 2, 2);

    return group;
}

QWidget* MainWindow::createCommandPanel() {
    auto* group = new QGroupBox(QStringLiteral("PA/FPGA 控制"), this);
    auto* layout = new QGridLayout(group);

    struct ButtonSpec {
        PaProtocol::Command command;
        const char* text;
    };

    const ButtonSpec buttons[] = {
        {PaProtocol::Command::Ping, "心跳"},
        {PaProtocol::Command::Status, "读取状态"},
        {PaProtocol::Command::LoadTemplate, "加载模板"},
        {PaProtocol::Command::ConfigTemplate, "配置模板"},
        {PaProtocol::Command::MakeOffset, "生成 Offset"},
        {PaProtocol::Command::MakeGain, "生成 Gain"},
        {PaProtocol::Command::StartCorrection, "启动校正"},
        {PaProtocol::Command::SendImage, "通知传图"},
        {PaProtocol::Command::WaitIrq, "等待中断"},
    };

    int row = 0;
    int col = 0;
    for (const ButtonSpec& spec : buttons) {
        auto* button = makeCommandButton(QString::fromUtf8(spec.text));
        connect(button, &QPushButton::clicked, this, [this, spec]() {
            sendCommand(spec.command);
        });
        layout->addWidget(button, row, col);
        col = (col + 1) % 2;
        if (col == 0) {
            ++row;
        }
    }

    customCommandEdit_ = new QLineEdit(group);
    customCommandEdit_->setPlaceholderText(QStringLiteral("手动命令，例如 STATUS"));
    auto* sendButton = new QPushButton(QStringLiteral("发送"), group);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendCustomCommand);
    connect(customCommandEdit_, &QLineEdit::returnPressed, this, &MainWindow::sendCustomCommand);

    layout->addWidget(customCommandEdit_, row + 1, 0);
    layout->addWidget(sendButton, row + 1, 1);

    return group;
}

QWidget* MainWindow::createImageOpsPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像操作"), this);
    auto* layout = new QGridLayout(group);

    auto* rotateLeftButton = makeCommandButton(QStringLiteral("左转"));
    auto* rotateRightButton = makeCommandButton(QStringLiteral("右转"));
    auto* flipHButton = makeCommandButton(QStringLiteral("水平翻转"));
    auto* flipVButton = makeCommandButton(QStringLiteral("垂直翻转"));
    auto* zoomOutButton = makeCommandButton(QStringLiteral("缩小"));
    auto* zoomInButton = makeCommandButton(QStringLiteral("放大"));
    auto* fitButton = makeCommandButton(QStringLiteral("适应"));
    auto* resetButton = makeCommandButton(QStringLiteral("重置"));
    auto* saveButton = makeCommandButton(QStringLiteral("保存"));

    connect(rotateLeftButton, &QPushButton::clicked, imageView_, &ImageView::rotateLeft);
    connect(rotateRightButton, &QPushButton::clicked, imageView_, &ImageView::rotateRight);
    connect(flipHButton, &QPushButton::clicked, imageView_, &ImageView::flipHorizontal);
    connect(flipVButton, &QPushButton::clicked, imageView_, &ImageView::flipVertical);
    connect(zoomOutButton, &QPushButton::clicked, imageView_, &ImageView::zoomOut);
    connect(zoomInButton, &QPushButton::clicked, imageView_, &ImageView::zoomIn);
    connect(fitButton, &QPushButton::clicked, imageView_, &ImageView::fitToWindow);
    connect(resetButton, &QPushButton::clicked, imageView_, &ImageView::resetView);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveDisplayImage);

    layout->addWidget(rotateLeftButton, 0, 0);
    layout->addWidget(rotateRightButton, 0, 1);
    layout->addWidget(flipHButton, 1, 0);
    layout->addWidget(flipVButton, 1, 1);
    layout->addWidget(zoomOutButton, 2, 0);
    layout->addWidget(zoomInButton, 2, 1);
    layout->addWidget(fitButton, 3, 0);
    layout->addWidget(resetButton, 3, 1);
    layout->addWidget(saveButton, 4, 0, 1, 2);

    return group;
}

QWidget* MainWindow::createWindowLevelPanel() {
    auto* group = new QGroupBox(QStringLiteral("窗宽窗位"), this);
    auto* layout = new QGridLayout(group);

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

    connect(autoWindowCheck_, &QCheckBox::toggled, this, &MainWindow::refreshImage);
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

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* openAction = fileMenu->addAction(QStringLiteral("打开 TiRaw 图像"));
    auto* saveAction = fileMenu->addAction(QStringLiteral("保存显示图像"));
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QStringLiteral("退出"));

    connect(openAction, &QAction::triggered, this, &MainWindow::openImage);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveDisplayImage);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    menuBar()->addMenu(QStringLiteral("校准"));
    menuBar()->addMenu(QStringLiteral("工具"));
    menuBar()->addMenu(QStringLiteral("开发者"));
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
    logView_->append(QStringLiteral("[%1] %2").arg(now, text));
}

void MainWindow::refreshImage() {
    if (!currentRaw_.isValid()) {
        return;
    }

    const QImage display = currentRaw_.toDisplayImage(
        autoWindowCheck_->isChecked(),
        centerSpin_->value(),
        widthSpin_->value());
    imageView_->setImage(display);
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
