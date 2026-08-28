#include "app_backend.h"
#include "hotas_build_version.h"
#include "setup_repair_helper.h"
#include "theme_manager.h"

#include <QApplication>
#include <QIcon>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

bool hasArgument(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    if (const std::optional<int> repairExit = hotas::runElevatedRepairTransaction(argc, argv)) {
        return *repairExit;
    }
    const bool startupSmoke = hasArgument(argc, argv, "--startup-smoke");
    if (hasArgument(argc, argv, "--startup-smoke-isolated")) {
        // Keep a local package smoke run away from the user's established
        // QSettings location. CI upgrade acceptance intentionally uses the
        // ordinary smoke argument so it can verify the seeded migration.
        QStandardPaths::setTestModeEnabled(true);
    }
    // AppBackend owns a QSystemTrayIcon context QMenu. QMenu is a Qt Widgets
    // class, so the shipped application must use QApplication rather than
    // QGuiApplication whenever tray support is available.
    QApplication application(argc, argv);
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
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
        backend.attachMainWindow(window);
    }
    if (startupSmoke && hasArgument(argc, argv, "--require-tray") && !backend.trayAvailable()) {
        return -2;
    }
    // This is used only by the isolated package/startup acceptance run. It
    // reaches normal backend, QML, and tray initialization before exiting,
    // without introducing a second startup path for installed users.
    if (startupSmoke) QTimer::singleShot(1500, &application, &QCoreApplication::quit);
    return application.exec();
}
