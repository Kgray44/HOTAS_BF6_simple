#include "app_backend.h"
#include "config_store.h"
#include "contention_resilience_controller.h"
#include "crash_diagnostics.h"
#include "hotas_build_version.h"
#include "interactive_scheduling_policy.h"
#include "native_qualification_driver.h"
#include "responsiveness_probe.h"
#include "setup_repair_helper.h"
#include "theme_manager.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>

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

QString argumentValue(int argc, char *argv[], const char *prefix)
{
    const size_t prefixLength = std::strlen(prefix);
    for (int index = 1; index < argc; ++index) {
        if (std::strncmp(argv[index], prefix, prefixLength) == 0) {
            return QString::fromLocal8Bit(argv[index] + prefixLength).trimmed();
        }
    }
    return {};
}

QString selectedDirectInputId(const hotas::MapperConfiguration &configuration)
{
    if (const hotas::DeviceRig *rig = hotas::findDeviceRig(configuration, configuration.activeDeviceRigId)) {
        for (const hotas::DeviceRigMember &member : rig->members) {
            if (!member.enabled) continue;
            for (const hotas::SavedControllerRecord &record : configuration.savedControllers) {
                if (record.id == member.controllerRecordId && !record.lastDirectInputId.trimmed().isEmpty()) {
                    return record.lastDirectInputId.trimmed();
                }
            }
        }
    }
    for (const hotas::SavedControllerRecord &record : configuration.savedControllers) {
        if (record.id == configuration.activeControllerRecordId) return record.lastDirectInputId.trimmed();
    }
    return configuration.preferredDeviceId.trimmed();
}

struct AxisHotPathTarget {
    QString directInputId;
    QString controllerRecordId;
    int memberIndex = -1;
    bool rzDescriptorPresent = false;
};

AxisHotPathTarget selectedAxisHotPathTarget(const hotas::MapperConfiguration &configuration)
{
    AxisHotPathTarget target;
    target.directInputId = selectedDirectInputId(configuration);
    if (target.directInputId.isEmpty()) return target;

    if (!configuration.activeDeviceRigId.isEmpty()) {
        const hotas::CompiledDeviceRigRuntime compiled = hotas::compileDeviceRigRuntime(
            configuration, configuration.activeDeviceRigId, configuration.activeProfileId);
        if (compiled.valid) {
            for (int index = 0; index < compiled.memberCount; ++index) {
                const hotas::CompiledDeviceRigMember &member = compiled.members[static_cast<size_t>(index)];
                if (member.directInputId.compare(target.directInputId, Qt::CaseInsensitive) != 0) continue;
                target.memberIndex = index;
                target.controllerRecordId = member.controllerRecordId;
                break;
            }
        }
    }

    for (const hotas::SavedControllerRecord &record : configuration.savedControllers) {
        if (record.id != target.controllerRecordId
            && record.lastDirectInputId.compare(target.directInputId, Qt::CaseInsensitive) != 0) continue;
        target.controllerRecordId = record.id;
        target.rzDescriptorPresent = record.axisDescriptors[static_cast<size_t>(hotas::PhysicalAxis::Rz)].present;
        break;
    }
    return target;
}

QString directInputStateFieldName(hotas::PhysicalAxis axis)
{
    switch (axis) {
    case hotas::PhysicalAxis::X: return QStringLiteral("lX");
    case hotas::PhysicalAxis::Y: return QStringLiteral("lY");
    case hotas::PhysicalAxis::Z: return QStringLiteral("lZ");
    case hotas::PhysicalAxis::Rx: return QStringLiteral("lRx");
    case hotas::PhysicalAxis::Ry: return QStringLiteral("lRy");
    case hotas::PhysicalAxis::Rz: return QStringLiteral("lRz");
    case hotas::PhysicalAxis::Slider0: return QStringLiteral("rglSlider[0]");
    case hotas::PhysicalAxis::Slider1: return QStringLiteral("rglSlider[1]");
    }
    return QStringLiteral("unknown");
}

int runAxisHotPathValidation()
{
    // This is a diagnostic-only worker run. Mapping stays off, so the real
    // report loop can be observed without acquiring, neutralizing, or writing
    // a virtual output. It neither persists configuration nor changes a
    // physical device.
    const hotas::MapperConfiguration configuration = hotas::ConfigStore::load();
    const AxisHotPathTarget target = selectedAxisHotPathTarget(configuration);
    if (target.directInputId.isEmpty() || (hotas::hasActiveDeviceRigRuntime(configuration) && target.memberIndex < 0)) {
        return -2;
    }
    hotas::MappingWorker worker(configuration);
    worker.setMappingEnabled(false);
    worker.start(QThread::HighPriority);

    constexpr size_t rz = static_cast<size_t>(hotas::PhysicalAxis::Rz);
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    float last = 0.0F;
    bool known = false;
    bool connected = false;
    quint64 changes = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 30000) {
        const hotas::AtomicRuntimeState &runtime = worker.runtime();
        const hotas::AtomicAdaptiveTelemetry &source = target.memberIndex <= 0
            ? static_cast<const hotas::AtomicAdaptiveTelemetry &>(runtime)
            : runtime.deviceRigMemberAdaptive[static_cast<size_t>(target.memberIndex)];
        connected = target.memberIndex < 0
            ? runtime.physicalConnected.load(std::memory_order_relaxed)
            : runtime.deviceRigMemberPhysicalConnected[static_cast<size_t>(target.memberIndex)]
                .load(std::memory_order_relaxed);
        if (connected) {
            const float value = source.raw[rz].load(std::memory_order_relaxed);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (known && !qFuzzyCompare(value + 1.0F, last + 1.0F)) ++changes;
            last = value;
            known = true;
        }
        QThread::msleep(4);
    }
    const hotas::AtomicRuntimeState &runtime = worker.runtime();
    const bool mappingActive = runtime.mappingActive.load(std::memory_order_relaxed);
    worker.requestStop();
    const bool stopped = worker.wait(2000);

    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("HOTAS-BF6-axis-hot-path-validation.txt"));
    QFile report(path);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return -4;
    QTextStream output(&report);
    output << "targetDirectInputId=" << target.directInputId << '\n'
           << "targetControllerRecordId=" << target.controllerRecordId << '\n'
           << "sourceMemberIndex=" << target.memberIndex << '\n'
           << "targetConnected=" << connected << '\n'
           << "rzDescriptorPresent=" << target.rzDescriptorPresent << '\n'
           << "mappingRequested=false\n"
           << "mappingActive=" << mappingActive << '\n'
           << "workerStopped=" << stopped << '\n'
           << "rzRawRange=" << minimum << ':' << maximum << '\n'
           << "rzRawChanges=" << changes << '\n'
           << "report=" << path << '\n';
    // A pre-fix saved layout can legitimately retain Rz metadata under its
    // former offset-derived slot.  This probe qualifies the real worker
    // acquisition path, not the age of that cached presentation metadata.
    return connected && changes > 0 && !mappingActive && stopped ? 0 : -3;
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
    const bool nativeQualification = hotas::NativeQualificationDriver::requested();
    // Manual qualification needs the real interactive QML surface without
    // attaching to the owner's active mapper or persisted settings.  This is
    // intentionally distinct from startup smoke: it isolates QSettings and
    // asks AppBackend to keep its smoke-safe hardware boundary, but still
    // enters the normal event loop for native pointer review.
    const bool isolatedPresentation = hasArgument(argc, argv, "--isolated-presentation") || nativeQualification;
    const bool startupSmoke = hasArgument(argc, argv, "--startup-smoke") || isolatedStartupSmoke;
    if (isolatedStartupSmoke || isolatedPresentation) {
        // Keep a local package smoke run away from the user's established
        // QSettings location. CI upgrade acceptance intentionally uses the
        // ordinary smoke argument so it can verify the seeded migration.
        QStandardPaths::setTestModeEnabled(true);
    }
    if (nativeQualification) {
        // Explicit, process-local qualification: retain the real native
        // window/render path while leaving owner configuration and external
        // setup inspection alone.
        qputenv("HOTAS_DISABLE_EXTERNAL_SETUP_INSPECTION", "1");
        qputenv("HOTAS_RESPONSIVENESS_INTERACTION_SOURCE", "native-window-synthetic");
    }
    // AppBackend owns a QSystemTrayIcon context QMenu. QMenu is a Qt Widgets
    // class, so the shipped application must use QApplication rather than
    // QGuiApplication whenever tray support is available.
    QApplication application(argc, argv);
    if (nativeQualification) hotas::InteractiveSchedulingPolicy::installForQualification(&application);
    else hotas::InteractiveSchedulingPolicy::installProduction(&application);
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
    if (hasArgument(argc, argv, "--axis-hot-path-validation")) {
        return runAxisHotPathValidation();
    }
    if (hasArgument(argc, argv, "--axis-acquisition-probe")) {
        const QString explicitDirectInputId = argumentValue(argc, argv, "--axis-acquisition-probe-id=");
        bool durationValid = false;
        const int requestedDuration = argumentValue(argc, argv, "--axis-acquisition-probe-duration=")
            .toInt(&durationValid);
        const hotas::DirectInputAxisAcquisitionProbe probe =
            hotas::MappingWorker::captureExactPhysicalAxisAcquisition(
                explicitDirectInputId.isEmpty()
                    ? selectedDirectInputId(hotas::ConfigStore::load())
                    : explicitDirectInputId,
                durationValid ? requestedDuration : 30000);
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("HOTAS-BF6-axis-acquisition-probe.txt"));
        QFile report(path);
        if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return -4;
        QTextStream output(&report);
        output << "diagnostic=" << probe.diagnostic << '\n'
               << "controller=" << probe.name << '\n'
               << "directInputId=" << probe.directInputId << '\n'
               << "durationMs=" << probe.durationMs << '\n'
               << "bufferedConfigureHRESULT=" << probe.bufferedConfigureResult << '\n'
               << "lastBufferedReadHRESULT=" << probe.lastBufferedReadResult << '\n';
        for (int axis = 0; axis < hotas::kPhysicalAxisCount; ++axis) {
            const auto physicalAxis = static_cast<hotas::PhysicalAxis>(axis);
            output << "stateField=" << directInputStateFieldName(physicalAxis)
                   << " canonicalSlot=" << hotas::physicalAxisKey(physicalAxis)
                   << " range=" << probe.stateFieldMinimum[static_cast<size_t>(axis)] << ':' << probe.stateFieldMaximum[static_cast<size_t>(axis)]
                   << " changes=" << probe.stateFieldChanges[static_cast<size_t>(axis)] << '\n';
        }
        for (int axis = 0; axis < hotas::kPhysicalAxisCount; ++axis) {
            const auto &descriptor = probe.axisDescriptors[static_cast<size_t>(axis)];
            if (!descriptor.present) continue;
            output << "axis=" << hotas::physicalAxisKey(static_cast<hotas::PhysicalAxis>(axis))
                   << " nativeName=" << descriptor.nativeName
                   << " enumerationIndex=" << descriptor.enumerationIndex
                   << " offset=" << descriptor.directInputOffset
                   << " type=" << descriptor.directInputType
                   << " guid=" << descriptor.directInputGuid
                   << " objectInstance=" << descriptor.directInputInstance
                   << " relative=" << descriptor.relative
                   << " nativeRange=" << descriptor.nativeMinimum << ':' << descriptor.nativeMaximum
                   << " rangeReadHRESULT=" << descriptor.rangeReadResult
                   << " rangeSetAttempted=" << descriptor.rangeSetAttempted
                   << " rangeSetHRESULT=" << descriptor.rangeSetResult
                   << " standardRange=" << probe.standardMinimum[static_cast<size_t>(axis)] << ':' << probe.standardMaximum[static_cast<size_t>(axis)]
                   << " standardChanges=" << probe.standardChanges[static_cast<size_t>(axis)]
                   << " bufferedRange=" << probe.bufferedMinimum[static_cast<size_t>(axis)] << ':' << probe.bufferedMaximum[static_cast<size_t>(axis)]
                   << " bufferedEvents=" << probe.bufferedEvents[static_cast<size_t>(axis)] << '\n';
        }
        output << "report=" << path << '\n';
        return probe.acquired ? 0 : -3;
    }
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
    // The Phase 0 probe is opt-in and all of its samples remain in memory
    // until this one bounded shutdown export.
    hotas::ResponsivenessProbe::installIfEnabled(&application);
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

    // Guidance classifies first use before AppBackend can write its normal
    // default configuration. It remains a presentation-only owner and never
    // receives a backend or mapping reference.
    hotas::ThemeManager themeManager;
    hotas::AppBackend backend;
    const bool recoveryNoticePending = !startupSmoke && !isolatedPresentation && !nativeQualification
        && hotas::CrashDiagnostics::previousRunWasAbnormal();
    // Connection order is intentional: shutdown waits only for the latest
    // asynchronous configuration generation before the final probe export.
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
        [&backend] { backend.flushPersistenceForShutdown(); });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
        [] { hotas::CrashDiagnostics::markCleanShutdown(); });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
        [] { hotas::ResponsivenessProbe::exportActive(); });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("contentionResilience"),
                                             backend.contentionResilienceController());
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themeManager);
    engine.rootContext()->setContextProperty(QStringLiteral("initialRecoveryNoticePending"), recoveryNoticePending);
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
    QObject *qmlRoot = engine.rootObjects().constFirst();
    if (auto *window = qobject_cast<QWindow *>(qmlRoot)) {
        backend.attachMainWindow(window);
        if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
            hotas::InteractiveSchedulingPolicy::attachWindow(quickWindow);
        }
        if (nativeQualification) {
            auto *driver = new hotas::NativeQualificationDriver(&application);
            driver->start(&application, &backend, &themeManager, qobject_cast<QQuickWindow *>(window));
        }
    }
    // A normal interactive launch must offer recovery after an abnormal exit.
    // The explicit startup-smoke route instead needs to initialize and close
    // deterministically in an off-screen package/upgrade acceptance run.
    if (recoveryNoticePending) {
        QTimer::singleShot(0, &application, [qmlRoot] {
            QMessageBox recovery;
            recovery.setWindowTitle(QStringLiteral("HOTAS BF6 recovery"));
            recovery.setIcon(QMessageBox::Warning);
            recovery.setText(QStringLiteral("HOTAS BF6 did not shut down normally last time."));
            recovery.setInformativeText(QStringLiteral("Crash reports are kept locally. You can continue normally or open the local diagnostics folder."));
            auto *open = recovery.addButton(QStringLiteral("Open Crash Reports"), QMessageBox::ActionRole);
            recovery.addButton(QStringLiteral("Continue"), QMessageBox::AcceptRole);
            recovery.exec();
            // First-use guidance is intentionally deferred until recovery is
            // acknowledged; a normal user choice must never cover an active
            // crash/recovery decision.
            if (qmlRoot) qmlRoot->setProperty("recoveryNoticePending", false);
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
