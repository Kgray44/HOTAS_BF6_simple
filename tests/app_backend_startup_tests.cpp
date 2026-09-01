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
            && backend.presentationSnapshotIntervalMs() == 33
            && backend.controllerDiscoveryIntervalMs() == 2500;

        lifecycleWindow->showMinimized();
        QTimer::singleShot(0, &application, [&, lifecycleWindow, visibleLifecycle] {
            const bool minimizedLifecycle = backend.presentationState() == QStringLiteral("Minimized")
                && backend.presentationSnapshotActive()
                && backend.presentationSnapshotIntervalMs() == 250
                && backend.controllerDiscoveryIntervalMs() == 5000;
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
                    QTimer::singleShot(150, &application, [&, lifecycleWindow, visibleLifecycle,
                                                          minimizedLifecycle, trayLifecycle, mappingWasRequested] {
                        const QVariantMap restoredCounters = backend.uiPerformanceCounters();
                        const bool restoredLifecycle = backend.presentationState() == QStringLiteral("Visible")
                            && backend.presentationSnapshotActive()
                            && backend.presentationSnapshotIntervalMs() == 33
                            && backend.controllerDiscoveryIntervalMs() == 2500
                            && backend.mappingRequested() == mappingWasRequested
                            && restoredCounters.value(QStringLiteral("telemetryChanged")).toULongLong() >= 1
                            && restoredCounters.value(QStringLiteral("inputTelemetryChanged")).toULongLong() >= 1;
                        backend.setAutomaticGameDetection(false);
                        const bool gameDetectionStopsWhenDisabled = !backend.gameDetectionTimerActive();
                        backend.setAutomaticGameDetection(true);
                        const bool gameDetectionRunsWhenEnabled = backend.gameDetectionTimerActive();
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
                        const QVariantList steadyButtonModel = backend.buttons();
                        const QVariantList steadyProfiles = backend.profiles();
                        const QVariantList steadyCategories = backend.profileCategories();
                        Q_UNUSED(steadyControllerModel);
                        Q_UNUSED(steadyButtonModel);
                        Q_UNUSED(steadyProfiles);
                        Q_UNUSED(steadyCategories);
                        QTimer::singleShot(10'000, &application, [&] {
            const QVariantMap counters = backend.uiPerformanceCounters();
            const qulonglong controllerGetterCalls = counters.value(QStringLiteral("controllerGetterCalls")).toULongLong();
            const qulonglong controllerRebuilds = counters.value(QStringLiteral("controllerModelRebuilds")).toULongLong();
            const qulonglong controllerNotifications = counters.value(QStringLiteral("controllersChanged")).toULongLong();
            const qulonglong buttonGetterCalls = counters.value(QStringLiteral("buttonGetterCalls")).toULongLong();
            const qulonglong buttonRebuilds = counters.value(QStringLiteral("buttonModelRebuilds")).toULongLong();
            const qulonglong profileGetterCalls = counters.value(QStringLiteral("profileGetterCalls")).toULongLong();
            const qulonglong categoryGetterCalls = counters.value(QStringLiteral("categoryGetterCalls")).toULongLong();
            const qulonglong telemetryNotifications = counters.value(QStringLiteral("telemetryChanged")).toULongLong();
            const qulonglong inputNotifications = counters.value(QStringLiteral("inputTelemetryChanged")).toULongLong();
            const qulonglong buttonNotifications = counters.value(QStringLiteral("buttonTelemetryChanged")).toULongLong();
            const qulonglong stateNotifications = counters.value(QStringLiteral("stateChanged")).toULongLong();
            const qulonglong controllerBackgroundRuns = counters.value(QStringLiteral("controllerDiscoveryBackgroundRuns")).toULongLong();
            const bool controllerDiscoveryTimerActive = counters.value(QStringLiteral("controllerDiscoveryTimerActive")).toBool();
            const qulonglong gameBackgroundRuns = counters.value(QStringLiteral("gameDetectionBackgroundRuns")).toULongLong();
            const qulonglong uiStallsOver250Ms = counters.value(QStringLiteral("uiEventLoopDelayOver250Ms")).toULongLong();
            std::fprintf(stderr,
                         "ui_steady_state_seconds=10 controller_getter_calls=%llu button_getter_calls=%llu profile_getter_calls=%llu category_getter_calls=%llu controller_model_rebuilds=%llu button_model_rebuilds=%llu controllers_changed=%llu "
                         "telemetry_changed=%llu input_telemetry_changed=%llu button_telemetry_changed=%llu state_changed=%llu controller_background_runs=%llu controller_discovery_timer_active=%d game_background_runs=%llu ui_stalls_over_250ms=%llu\n",
                         static_cast<unsigned long long>(controllerGetterCalls),
                         static_cast<unsigned long long>(buttonGetterCalls),
                         static_cast<unsigned long long>(profileGetterCalls),
                         static_cast<unsigned long long>(categoryGetterCalls),
                         static_cast<unsigned long long>(controllerRebuilds),
                         static_cast<unsigned long long>(buttonRebuilds),
                         static_cast<unsigned long long>(controllerNotifications),
                         static_cast<unsigned long long>(telemetryNotifications),
                         static_cast<unsigned long long>(inputNotifications),
                         static_cast<unsigned long long>(buttonNotifications),
                         static_cast<unsigned long long>(stateNotifications),
                         static_cast<unsigned long long>(controllerBackgroundRuns),
                         controllerDiscoveryTimerActive ? 1 : 0,
                         static_cast<unsigned long long>(gameBackgroundRuns),
                         static_cast<unsigned long long>(uiStallsOver250Ms));
            // Live analog presentation is capped near 30 Hz and numeric
            // telemetry near 10 Hz. Neither cached controller nor 128-button
            // structure may rebuild during that activity. The controller
            // scheduler may have been phase-reset by the tray restore while a
            // DirectInput call remains external and isolated; it must either
            // have sampled or remain active without stalling the UI heartbeat.
            passed = controllerGetterCalls == 1 && buttonGetterCalls == 1
                && profileGetterCalls == 1 && categoryGetterCalls == 1
                && controllerRebuilds == 0 && buttonRebuilds == 0 && controllerNotifications == 0
                && telemetryNotifications >= 80 && telemetryNotifications <= 130
                && inputNotifications >= 100 && inputNotifications <= 340
                && stateNotifications < telemetryNotifications / 4
                && (controllerBackgroundRuns >= 1 || controllerDiscoveryTimerActive)
                && gameBackgroundRuns >= 1
                && uiStallsOver250Ms == 0;
            backend.setAutomaticGameDetection(false);
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
