#include "app_backend.h"
#include "crash_diagnostics.h"
#include "hotas_build_version.h"
#include "setup_repair_helper.h"
#include "theme_manager.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QDesktopServices>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstring>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace {

bool hasArgument(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return true;
    }
    return false;
}

void crashMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtFatalMsg) hotas::CrashDiagnostics::recordQtFatal(message);
    std::fprintf(stderr, "%s\n", qPrintable(message));
}

} // namespace

int main(int argc, char *argv[])
{
    // This intentionally happens before QApplication/Qt Quick initialization.
    // A single elevated HOTAS BF6 process performs only the approved repair
    // operations and returns structured read-back data to the normal UI.
    if (const std::optional<int> repairExit = hotas::runElevatedRepairTransaction(argc, argv)) {
        return *repairExit;
    }
    const bool isolatedStartupSmoke = hasArgument(argc, argv, "--startup-smoke-isolated");
    // Manual qualification needs the real interactive QML surface without
    // attaching to the owner's active mapper or persisted settings.  This is
    // intentionally distinct from startup smoke: it isolates QSettings and
    // asks AppBackend to keep its smoke-safe hardware boundary, but still
    // enters the normal event loop for native pointer review.
    const bool isolatedPresentation = hasArgument(argc, argv, "--isolated-presentation")
        || hasArgument(argc, argv, "--isolated-presentation-signal-flow");
    const bool startupSmoke = hasArgument(argc, argv, "--startup-smoke") || isolatedStartupSmoke;
    if (isolatedStartupSmoke || isolatedPresentation) {
        // Keep a local package smoke run away from the user's established
        // QSettings location. CI upgrade acceptance intentionally uses the
        // ordinary smoke argument so it can verify the seeded migration.
        QStandardPaths::setTestModeEnabled(true);
    }
    // AppBackend owns a QSystemTrayIcon context QMenu. QMenu is a Qt Widgets
    // class, so the shipped application must use QApplication rather than
    // QGuiApplication whenever tray support is available.
    QApplication application(argc, argv);
    // Flight Deck also contains regular Qt Quick Text items, which resolve
    // from QApplication's default rather than a Controls font inheritance
    // chain.  Pin the supported Windows UI face before any QML loads.
    application.setFont(QFont(QStringLiteral("Segoe UI")));
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
    hotas::CrashDiagnostics::initialize(QCoreApplication::applicationFilePath(),
        QString::fromLatin1(HOTAS_BF6_VERSION), QStringLiteral(HOTAS_BF6_BUILD_ID));
    qInstallMessageHandler(crashMessageHandler);
    std::set_terminate([] {
        hotas::CrashDiagnostics::recordTerminate();
#ifdef Q_OS_WIN
        TerminateProcess(GetCurrentProcess(), 3);
#else
        std::abort();
#endif
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
        [] { hotas::CrashDiagnostics::markCleanShutdown(); });
    // Development/test-only fatal-path exercise. It is not presented in QML
    // or Settings and never runs unless a caller supplies the explicit flag.
    if (hasArgument(argc, argv, "--crash-reporter-test")) {
        hotas::CrashDiagnostics::recordControlPlaneEvent(
            QStringLiteral("Controlled crash reporter test started"),
            QStringLiteral("page=Controlled crash test\ntheme=Standard\nactiveRig=Test Flight Rig\neditingRig=Test Flight Rig\neditingScope=All Devices\nmappingRequested=false\nmappingEffective=false\nvJoy=unavailable\nHidHide=not evaluated"));
#ifdef Q_OS_WIN
        RaiseException(0xE0424F53UL, 0, 0, nullptr);
#endif
        return 3;
    }

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
    // A normal interactive launch must offer recovery after an abnormal exit.
    // The explicit startup-smoke route instead needs to initialize and close
    // deterministically in an off-screen package/upgrade acceptance run.
    if (!startupSmoke && !isolatedPresentation && hotas::CrashDiagnostics::previousRunWasAbnormal()) {
        QTimer::singleShot(0, &application, [] {
            QMessageBox recovery;
            recovery.setWindowTitle(QStringLiteral("HOTAS BF6 recovery"));
            recovery.setIcon(QMessageBox::Warning);
            recovery.setText(QStringLiteral("HOTAS BF6 did not shut down normally last time."));
            recovery.setInformativeText(QStringLiteral("Crash reports are kept locally. You can continue normally or open the local diagnostics folder."));
            auto *open = recovery.addButton(QStringLiteral("Open Crash Reports"), QMessageBox::ActionRole);
            recovery.addButton(QStringLiteral("Continue"), QMessageBox::AcceptRole);
            recovery.exec();
            if (recovery.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(hotas::CrashDiagnostics::crashReportsDirectory()));
        });
    }
    if (startupSmoke && hasArgument(argc, argv, "--require-tray") && !backend.trayAvailable()) {
        return -2;
    }
    // This is used only by the explicit package/startup acceptance route. At
    // this point the real backend, tray, and QML root have all initialized;
    // return directly so a platform event-loop or an active controller cannot
    // make a smoke check linger or take focus. Interactive launches always
    // enter the ordinary event loop below.
    if (startupSmoke) {
        // `aboutToQuit` is normally responsible for this record, but this
        // explicit route intentionally does not enter the event loop.
        hotas::CrashDiagnostics::markCleanShutdown();
        return 0;
    }
    return application.exec();
}
