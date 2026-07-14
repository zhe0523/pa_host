#pragma once

#include <QLabel>
#include <QElapsedTimer>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPoint>
#include <QRect>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>

#include <memory>

#include "ImageSession.h"
#include "ImageSource.h"
#include "ImageView.h"
#include "PaProtocol.h"
#include "SerialClient.h"
#include "TiRawImage.h"

class QCheckBox;
class QComboBox;
class QSlider;
class QAction;

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
    explicit MainWindow(std::shared_ptr<IImageAlgorithms> algorithms, QWidget* parent = nullptr);

private slots:
    void openImage();
    void startImageReplay();
    void stopImageReplay();
    void saveDisplayImage();
    void connectSerial();
    void disconnectSerial();
    void sendCommand(PaProtocol::Command command);
    void handleLineReceived(const QString& line);
    void handleSerialError(const QString& message);
    void updateWindowLevel();
    void toggleImageMaximized();

private:
    QWidget* createTopBar();
    QWidget* createImageListPanel();
    QWidget* createRightPanel();
    QWidget* createImageOpsPanel();
    QWidget* createWindowLevelPanel();
    QWidget* createImageInfoPanel();
    void createMenus();
    void createStatusBar();
    void appendLog(const QString& text);
    void scheduleImageRefresh(bool resetViewState = false);
    void refreshImage(bool resetViewState = false);
    void updatePixelInfo(const QPoint& imagePoint);
    void updateRoiInfo(const QRect& imageRect);
    void updateFullImageInfo();
    void handleAnalysisRoi(const QRect& imageRect);
    void applyWindowLevelFromRoi(const QRect& imageRect);
    void handleImageFrame(const ImageFrame& frame);
    void updateStatusFromResponse(const PaProtocol::Response& response);

    SerialClient serial_;
    std::unique_ptr<ImageSession> imageSession_;
    LocalReplaySource* replaySource_ = nullptr;

    QWidget* topBar_ = nullptr;
    QWidget* imageListPanel_ = nullptr;
    QWidget* rightPanel_ = nullptr;
    QListWidget* imageList_ = nullptr;
    ImageView* imageView_ = nullptr;
    QTextEdit* logView_ = nullptr;
    QAction* imageMaximizeAction_ = nullptr;

    QComboBox* portCombo_ = nullptr;
    QSpinBox* baudSpin_ = nullptr;

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
    QLabel* pixelInfoLabel_ = nullptr;
    QLabel* roiInfoLabel_ = nullptr;

    QTimer imageRefreshTimer_;
    QElapsedTimer replayFpsTimer_;
    QElapsedTimer imageInfoTimer_;
    quint64 replayFpsFrameCount_ = 0;
    bool resetViewStateOnRefresh_ = false;
    bool imageMaximized_ = false;
};
