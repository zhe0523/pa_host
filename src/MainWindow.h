#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextEdit>

#include "ImageView.h"
#include "PaProtocol.h"
#include "SerialClient.h"
#include "TiRawImage.h"

class QCheckBox;
class QComboBox;
class QSlider;

/*
 * 主窗口。
 *
 * 当前界面目标不是复刻旧 Windows 软件，而是保留同类操作习惯：
 *   左侧图像列表，中间图像显示，右侧控制/图像操作，底部状态栏。
 * 新项目只面向单一 RS422 型号，所以不引入旧软件里的 WiFi、TCP、多型号切换。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openImage();
    void saveDisplayImage();
    void connectSerial();
    void disconnectSerial();
    void sendCommand(PaProtocol::Command command);
    void sendCustomCommand();
    void handleLineReceived(const QString& line);
    void handleSerialError(const QString& message);
    void updateWindowLevel();

private:
    QWidget* createTopBar();
    QWidget* createImageListPanel();
    QWidget* createRightPanel();
    QWidget* createSerialPanel();
    QWidget* createCommandPanel();
    QWidget* createImageOpsPanel();
    QWidget* createWindowLevelPanel();
    void createMenus();
    void createStatusBar();
    void appendLog(const QString& text);
    void refreshImage();
    void updateStatusFromResponse(const PaProtocol::Response& response);

    SerialClient serial_;
    TiRawImage currentRaw_;

    QListWidget* imageList_ = nullptr;
    ImageView* imageView_ = nullptr;
    QTextEdit* logView_ = nullptr;

    QComboBox* portCombo_ = nullptr;
    QSpinBox* baudSpin_ = nullptr;
    QLineEdit* customCommandEdit_ = nullptr;

    QCheckBox* autoWindowCheck_ = nullptr;
    QSlider* centerSlider_ = nullptr;
    QSlider* widthSlider_ = nullptr;
    QSpinBox* centerSpin_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;

    QLabel* modelLabel_ = nullptr;
    QLabel* serialLabel_ = nullptr;
    QLabel* connectionLabel_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
};

