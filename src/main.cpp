#include "app_backend.h"
#include "hotas_build_version.h"
#include "setup_repair_helper.h"
#include "theme_manager.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    if (const std::optional<int> repairExit = hotas::runElevatedRepairTransaction(argc, argv)) {
        return *repairExit;
    }
    QGuiApplication application(argc, argv);
    // The executable resource covers shell identity; this runtime icon covers
    // the Qt title bar, taskbar, Alt+Tab, and task-switching surfaces.
    application.setWindowIcon(QIcon(u":/assets/icons/png/hotas-bf6-256.png"_qs));
    // Keep the established QSettings identity so an installer upgrade retains
    // the existing profiles, curves, calibration, and button configuration.
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));
    application.setApplicationVersion(QString::fromLatin1(HOTAS_BF6_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    hotas::AppBackend backend;
    hotas::ThemeManager themeManager;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    QObject::connect(&engine, &QQmlEngine::warnings, &application,
        [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                std::fprintf(stderr, "%s\n", qPrintable(warning.toString()));
            }
        });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(u"HOTASMapper"_qs, u"Main"_qs);
    if (engine.rootObjects().isEmpty()) return -1;
    return application.exec();
}
