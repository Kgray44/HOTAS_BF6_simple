#include "app_backend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));

    {
        hotas::AppBackend backend;
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
        if (application.exec() != 0) return 1;
    }
    return 0;
}
