#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Settings are stored per user under this name; keep it stable.
    QCoreApplication::setOrganizationName(QStringLiteral("denon-remote"));
    QCoreApplication::setApplicationName(QStringLiteral("denon-remote"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DENON_REMOTE_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("denon-remote"));

    MainWindow window;
    window.show();

    return app.exec();
}
