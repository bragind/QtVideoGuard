#include "DetectionConfig.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("BraginLab"));
    QCoreApplication::setApplicationName(QStringLiteral("QtVideoGuard"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    qRegisterMetaType<DetectionConfig>("DetectionConfig");

    MainWindow window;
    window.show();

    return application.exec();
}
