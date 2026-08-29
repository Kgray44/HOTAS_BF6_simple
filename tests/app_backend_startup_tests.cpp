#include "app_backend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("HOTAS_ENABLE_UI_PERFORMANCE_INSTRUMENTATION", "1");
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));

    hotas::AppBackend backend;
    bool passed = false;
    // Let the startup discovery and bounded update check settle before taking
    // the steady-state sample. This exercises the real 16 ms presentation
    // timer while no controller inventory change is requested.
    QTimer::singleShot(1500, &application, [&] {
        backend.resetUiPerformanceCounters();
        // One explicit cached read establishes the getter baseline. During the
        // following ten seconds no telemetry refresh may invoke it again.
        const QVariantList controllerModel = backend.controllers();
        Q_UNUSED(controllerModel);
        QTimer::singleShot(10'000, &application, [&] {
            const QVariantMap counters = backend.uiPerformanceCounters();
            const qulonglong controllerGetterCalls = counters.value(QStringLiteral("controllerGetterCalls")).toULongLong();
            const qulonglong controllerRebuilds = counters.value(QStringLiteral("controllerModelRebuilds")).toULongLong();
            const qulonglong controllerNotifications = counters.value(QStringLiteral("controllersChanged")).toULongLong();
            const qulonglong telemetryNotifications = counters.value(QStringLiteral("telemetryChanged")).toULongLong();
            const qulonglong inputNotifications = counters.value(QStringLiteral("inputTelemetryChanged")).toULongLong();
            const qulonglong stateNotifications = counters.value(QStringLiteral("stateChanged")).toULongLong();
            std::fprintf(stderr,
                         "ui_steady_state_seconds=10 controller_getter_calls=%llu controller_model_rebuilds=%llu controllers_changed=%llu "
                         "telemetry_changed=%llu input_telemetry_changed=%llu state_changed=%llu\n",
                         static_cast<unsigned long long>(controllerGetterCalls),
                         static_cast<unsigned long long>(controllerRebuilds),
                         static_cast<unsigned long long>(controllerNotifications),
                         static_cast<unsigned long long>(telemetryNotifications),
                         static_cast<unsigned long long>(inputNotifications),
                         static_cast<unsigned long long>(stateNotifications));
            // Telemetry remains live while the cached controller model stays
            // completely quiet. A few state notifications are permitted for
            // normal asynchronous startup/control-plane completion, but never
            // presentation-cadence churn. The offscreen test host is not a
            // frame-rate benchmark, so it asserts live notifications rather
            // than a machine-specific callback count.
            passed = controllerGetterCalls == 1 && controllerRebuilds == 0 && controllerNotifications == 0
                && telemetryNotifications >= 10 && inputNotifications >= 10
                && stateNotifications < telemetryNotifications / 4;
            QCoreApplication::quit();
        });
    });
    const int eventLoopExitCode = application.exec();
    std::fprintf(stderr, "ui_steady_state_event_loop_exit=%d passed=%d\n", eventLoopExitCode, passed ? 1 : 0);
    return eventLoopExitCode == 0 && passed ? 0 : 1;
}
