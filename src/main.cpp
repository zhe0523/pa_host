#include <QApplication>
#include <QStyleFactory>

#include "MainWindow.h"

namespace {
constexpr const char* kAppStyleSheet = R"(
QMainWindow {
    background: #f5f7fa;
}

QMenuBar {
    background: #ffffff;
    border-bottom: 1px solid #d6dde6;
    padding: 2px 8px;
}

QMenuBar::item {
    padding: 6px 10px;
    border-radius: 4px;
}

QMenuBar::item:selected {
    background: #e8eef6;
}

QMenu {
    background: #ffffff;
    border: 1px solid #cbd5df;
    padding: 6px;
}

QMenu::item {
    padding: 6px 24px 6px 16px;
    border-radius: 4px;
}

QMenu::item:selected {
    background: #e7f0ff;
    color: #123b6d;
}

QWidget#topBar {
    background: #ffffff;
    border: 1px solid #d9e1ea;
    border-radius: 6px;
}

QGraphicsView#imageCanvas {
    background: #f8fafc;
    border: 1px solid #d9e1ea;
    border-radius: 6px;
}

QWidget#rightPanel {
    background: transparent;
}

QGroupBox {
    background: #ffffff;
    border: 1px solid #d9e1ea;
    border-radius: 6px;
    margin-top: 16px;
    font-weight: 600;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #344054;
}

QPushButton {
    background: #ffffff;
    border: 1px solid #c8d2dc;
    border-radius: 5px;
    padding: 5px 10px;
    color: #26313d;
}

QPushButton:hover {
    background: #f0f5fb;
    border-color: #8fb2d9;
}

QPushButton:pressed {
    background: #dfeaf5;
}

QPushButton:checked,
QPushButton[role="mode"]:checked {
    background: #2f80ed;
    border-color: #2f80ed;
    color: #ffffff;
}

QPushButton[role="primary"] {
    background: #2f80ed;
    border-color: #2f80ed;
    color: #ffffff;
}

QPushButton[role="primary"]:hover {
    background: #1f6fd1;
}

QPushButton[role="stop"] {
    background: #2f9e44;
    border-color: #2f9e44;
    color: #ffffff;
}

QPushButton[role="stop"]:hover {
    background: #27883a;
}

QLineEdit,
QComboBox,
QSpinBox,
QTextEdit,
QListWidget {
    background: #ffffff;
    border: 1px solid #cbd5df;
    border-radius: 5px;
    padding: 4px 6px;
    selection-background-color: #2f80ed;
}

QListWidget::item {
    padding: 5px 6px;
    border-radius: 4px;
}

QListWidget::item:selected {
    background: #e7f0ff;
    color: #123b6d;
}

QSlider::groove:horizontal {
    height: 4px;
    background: #d8e0ea;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
    background: #2f80ed;
}

QCheckBox {
    color: #344054;
    spacing: 8px;
}

QSplitter::handle {
    background: #d9e1ea;
}

QSplitter::handle:horizontal {
    width: 4px;
}

QStatusBar {
    background: #ffffff;
    border-top: 1px solid #d6dde6;
}

QStatusBar QLabel#statusLabel {
    background: #f1f5f9;
    border: 1px solid #d8e0ea;
    border-radius: 4px;
    color: #344054;
}
)";
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setApplicationName("PA Host");
    QApplication::setOrganizationName("TiRay");
    app.setStyleSheet(QString::fromUtf8(kAppStyleSheet));

    MainWindow window;
    window.show();

    return app.exec();
}
