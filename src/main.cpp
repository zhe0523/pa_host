#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PA Host");
    QApplication::setOrganizationName("TiRay");

    MainWindow window;
    window.show();

    return app.exec();
}

