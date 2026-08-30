#include "app_backend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQuickWindow>
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
    // Let startup control-plane work settle before exercising the real
    // presentation lifecycle and then taking the visible steady-state sample.
    QTimer::singleShot(1500, &application, [&] {
        auto *lifecycleWindow = new QQuickWindow();
        backend.attachMainWindow(lifecycleWindow);
        lifecycleWindow->show();
        QCoreApplication::processEvents();
        const bool visibleLifecycle = backend.presentationState() == QStringLiteral("Visible")
            && backend.presentationSnapshotActive()
            && backend.presentationSnapshotIntervalMs() == 16
            && backend.controllerDiscoveryIntervalMs() == 1000;

        lifecycleWindow->showMinimized();
        QTimer::singleShot(0, &application, [&, lifecycleWindow, visibleLifecycle] {
            const bool minimizedLifecycle = backend.presentationState() == QStringLiteral("Minimized")
                && backend.presentationSnapshotActive()
                && backend.presentationSnapshotIntervalMs() == 200
                && backend.controllerDiscoveryIntervalMs() == 3500;
            const bool mappingWasRequested = backend.mappingRequested();
            backend.hideToTray();
            QTimer::singleShot(50, &application, [&, lifecycleWindow, visibleLifecycle,
                                                    minimizedLifecycle, mappingWasRequested] {
                backend.resetUiPerformanceCounters();
                const QVariantList controllerModel = backend.controllers();
                const QVariantList profiles = backend.profiles();
                const QVariantList categories = backend.profileCategories();
                Q_UNUSED(controllerModel);
                Q_UNUSED(profiles);
                Q_UNUSED(categories);
                QTimer::singleShot(450, &application, [&, lifecycleWindow, visibleLifecycle,
                                                        minimizedLifecycle, mappingWasRequested] {
                    const QVariantMap trayCounters = backend.uiPerformanceCounters();
                    const bool trayLifecycle = backend.presentationState() == QStringLiteral("TrayHidden")
                        && !backend.presentationSnapshotActive()
                        && backend.presentationSnapshotIntervalMs() == 0
                        && backend.controllerDiscoveryIntervalMs() == 7500
                        && backend.mappingRequested() == mappingWasRequested
                        && trayCounters.value(QStringLiteral("controllerGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("profileGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("categoryGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("controllerModelRebuilds")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("controllersChanged")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("telemetryChanged")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("inputTelemetryChanged")).toULongLong() == 0;

                    backend.restoreFromTray();
                    QTimer::singleShot(50, &application, [&, lifecycleWindow, visibleLifecycle,
                                                          minimizedLifecycle, trayLifecycle, mappingWasRequested] {
                        const QVariantMap restoredCounters = backend.uiPerformanceCounters();
                        const bool restoredLifecycle = backend.presentationState() == QStringLiteral("Visible")
                            && backend.presentationSnapshotActive()
                            && backend.presentationSnapshotIntervalMs() == 16
                            && backend.controllerDiscoveryIntervalMs() == 1000
                            && backend.mappingRequested() == mappingWasRequested
                            && restoredCounters.value(QStringLiteral("telemetryChanged")).toULongLong() >= 1
                            && restoredCounters.value(QStringLiteral("inputTelemetryChanged")).toULongLong() >= 1;
                        backend.setAutomaticGameDetection(false);
                        const bool gameDetectionStopsWhenDisabled = !backend.gameDetectionTimerActive();
                        backend.setAutomaticGameDetection(true);
                        const bool gameDetectionRunsWhenEnabled = backend.gameDetectionTimerActive();
                        backend.setAutomaticGameDetection(false);
                        delete lifecycleWindow;

                        if (!(visibleLifecycle && minimizedLifecycle && trayLifecycle
                              && restoredLifecycle && gameDetectionStopsWhenDisabled
                              && gameDetectionRunsWhenEnabled)) {
                            std::fprintf(stderr,
                                "presentation_lifecycle_visible=%d minimized=%d tray=%d restored=%d game_disabled=%d game_enabled=%d\n",
                                visibleLifecycle ? 1 : 0, minimizedLifecycle ? 1 : 0,
                                trayLifecycle ? 1 : 0, restoredLifecycle ? 1 : 0,
                                gameDetectionStopsWhenDisabled ? 1 : 0,
                                gameDetectionRunsWhenEnabled ? 1 : 0);
                            QCoreApplication::quit();
                            return;
                        }

                        backend.resetUiPerformanceCounters();
        // One explicit cached read establishes the getter baseline. During the
        // following ten seconds no telemetry refresh may invoke it again.
                        const QVariantList steadyControllerModel = backend.controllers();
                        const QVariantList steadyProfiles = backend.profiles();
                        const QVariantList steadyCategories = backend.profileCategories();
                        Q_UNUSED(steadyControllerModel);
                        Q_UNUSED(steadyProfiles);
                        Q_UNUSED(steadyCategories);
                        QTimer::singleShot(10'000, &application, [&] {
            const QVariantMap counters = backend.uiPerformanceCounters();
            const qulonglong controllerGetterCalls = counters.value(QStringLiteral("controllerGetterCalls")).toULongLong();
            const qulonglong controllerRebuilds = counters.value(QStringLiteral("controllerModelRebuilds")).toULongLong();
            const qulonglong controllerNotifications = counters.value(QStringLiteral("controllersChanged")).toULongLong();
            const qulonglong profileGetterCalls = counters.value(QStringLiteral("profileGetterCalls")).toULongLong();
            const qulonglong categoryGetterCalls = counters.value(QStringLiteral("categoryGetterCalls")).toULongLong();
            const qulonglong telemetryNotifications = counters.value(QStringLiteral("telemetryChanged")).toULongLong();
            const qulonglong inputNotifications = counters.value(QStringLiteral("inputTelemetryChanged")).toULongLong();
            const qulonglong stateNotifications = counters.value(QStringLiteral("stateChanged")).toULongLong();
            std::fprintf(stderr,
                         "ui_steady_state_seconds=10 controller_getter_calls=%llu profile_getter_calls=%llu category_getter_calls=%llu controller_model_rebuilds=%llu controllers_changed=%llu "
                         "telemetry_changed=%llu input_telemetry_changed=%llu state_changed=%llu\n",
                         static_cast<unsigned long long>(controllerGetterCalls),
                         static_cast<unsigned long long>(profileGetterCalls),
                         static_cast<unsigned long long>(categoryGetterCalls),
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
            passed = controllerGetterCalls == 1 && profileGetterCalls == 1 && categoryGetterCalls == 1
                && controllerRebuilds == 0 && controllerNotifications == 0
                && telemetryNotifications >= 10 && inputNotifications >= 10
                && stateNotifications < telemetryNotifications / 4;
            QCoreApplication::quit();
        });
                    });
                });
            });
        });
    });
    const int eventLoopExitCode = application.exec();
    std::fprintf(stderr, "ui_steady_state_event_loop_exit=%d passed=%d\n", eventLoopExitCode, passed ? 1 : 0);
    return eventLoopExitCode == 0 && passed ? 0 : 1;
}
