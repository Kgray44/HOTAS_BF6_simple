#include "app_backend.h"

#include "adaptive_response.h"
#include "axis_transform.h"
#include "automation_engine.h"
#include "button_mapping.h"
#include "config_store.h"
#include "controller_discovery.h"
#include "controller_diagnostics.h"
#include "controller_manager.h"
#include "hotas_build_version.h"
#include "input_learning.h"
#include "launcher_core.h"
#include "profile_model.h"
#include "profile_portability.h"
#include "response_curve.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QSystemTrayIcon>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>
#include <QAction>
#include <QIcon>
#include <QMenu>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

constexpr int kVisibleSnapshotIntervalMs = 33;
// Capture more diagnostic detail than the renderer consumes. This remains a
// GUI-thread read of existing atomics, never a MappingWorker callback.
constexpr int kAdaptiveResponseHistoryIntervalMs = 12;
constexpr int kMinimizedSnapshotIntervalMs = 250;
constexpr int kVisibleNumericTelemetryIntervalMs = 100;
constexpr int kMinimizedNumericTelemetryIntervalMs = 500;
constexpr int kVisibleControllerDiscoveryIntervalMs = 2500;
constexpr int kMinimizedControllerDiscoveryIntervalMs = 5000;
constexpr int kTrayHiddenControllerDiscoveryIntervalMs = 7500;
constexpr int kVisibleGameDetectionIntervalMs = 2500;
constexpr int kMinimizedGameDetectionIntervalMs = 5000;
constexpr int kTrayHiddenGameDetectionIntervalMs = 7500;

bool sameControllerInventory(const QList<DiscoveredController> &left,
                             const QList<DiscoveredController> &right)
{
    if (left.size() != right.size()) return false;
    return std::equal(left.cbegin(), left.cend(), right.cbegin(),
                      [](const DiscoveredController &first, const DiscoveredController &second) {
        return first.name == second.name && first.directInputId == second.directInputId
            && first.productGuid == second.productGuid && first.hidInstanceId == second.hidInstanceId
            && first.vendorId == second.vendorId && first.productId == second.productId
            && first.axes == second.axes && first.axisCount == second.axisCount
            && first.buttonCount == second.buttonCount && first.povCount == second.povCount
            && first.connected == second.connected && first.virtualDevice == second.virtualDevice;
    });
}

QString automationProfileName(const MapperConfiguration &configuration, const QString &id)
{
    if (findProfile(configuration, id)) return categoryProfileLabel(configuration, id);
    return id.isEmpty() ? u"a profile"_qs : u"missing profile"_qs;
}

struct RunningApplication {
    QString name;
    QString executable;
    QString path;
};

QString friendlyApplicationName(const QString &executable)
{
    const QString stem = QFileInfo(executable).completeBaseName();
    if (stem.compare(u"bf6"_qs, Qt::CaseInsensitive) == 0) return u"Battlefield 6"_qs;
    if (stem.compare(u"starcitizen"_qs, Qt::CaseInsensitive) == 0) return u"Star Citizen"_qs;
    QString spaced = stem;
    spaced.replace(QRegularExpression(u"([a-z])([A-Z])"_qs), u"\\1 \\2"_qs);
    spaced.replace(QRegularExpression(u"[_-]+"_qs), u" "_qs);
    return spaced.isEmpty() ? executable : spaced;
}

bool isUsefulRunningApplication(const QString &executable)
{
    static const QSet<QString> excluded = {
        u"applicationframehost.exe"_qs, u"audiodg.exe"_qs, u"conhost.exe"_qs,
        u"csrss.exe"_qs, u"ctfmon.exe"_qs, u"dwm.exe"_qs, u"explorer.exe"_qs,
        u"fontdrvhost.exe"_qs, u"idle.exe"_qs, u"lsass.exe"_qs, u"memory compression"_qs,
        u"msedgewebview2.exe"_qs, u"registry"_qs, u"runtimebroker.exe"_qs,
        u"searchhost.exe"_qs, u"services.exe"_qs, u"shellexperiencehost.exe"_qs,
        u"sihost.exe"_qs, u"smss.exe"_qs, u"spoolsv.exe"_qs, u"startmenuexperiencehost.exe"_qs,
        u"svchost.exe"_qs, u"system"_qs, u"taskhostw.exe"_qs, u"textinputhost.exe"_qs,
        u"wininit.exe"_qs, u"winlogon.exe"_qs
    };
    return executable.endsWith(u".exe"_qs, Qt::CaseInsensitive)
        && !excluded.contains(executable.toCaseFolded());
}

QList<RunningApplication> runningApplicationSnapshot(bool resolvePaths,
                                                      QHash<QString, QString> *pathCache = nullptr)
{
    QList<RunningApplication> result;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    QSet<QString> seen;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString executable = QString::fromWCharArray(entry.szExeFile).trimmed();
            const QString key = executable.toCaseFolded();
            if (!isUsefulRunningApplication(executable) || seen.contains(key)) continue;
            seen.insert(key);
            QString path = pathCache ? pathCache->value(key) : QString{};
            // Automatic category matching needs only the basename. Avoiding
            // OpenProcess and path resolution there removes the heaviest
            // per-process work; the explicit Add Game view resolves a path
            // only once per known executable identity.
            if (resolvePaths && path.isEmpty()) {
                const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process) {
                    std::array<wchar_t, 32768> buffer{};
                    DWORD length = static_cast<DWORD>(buffer.size());
                    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) {
                        path = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length));
                        if (pathCache && !path.isEmpty()) pathCache->insert(key, path);
                    }
                    CloseHandle(process);
                }
            }
            result.append({friendlyApplicationName(executable), executable, path});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
#endif
    std::sort(result.begin(), result.end(), [](const RunningApplication &left, const RunningApplication &right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QString automationBehaviorLabel(AutomationActivationMode mode)
{
    switch (mode) {
    case AutomationActivationMode::WhileTriggerActive: return u"MOMENTARY"_qs;
    case AutomationActivationMode::ToggleOnTrigger: return u"TOGGLE"_qs;
    case AutomationActivationMode::RunBriefly: return u"TIMED"_qs;
    }
    return u"UNKNOWN"_qs;
}

QString automationConditionSummary(const AutomationConditionDefinition &condition,
                                   const MapperConfiguration &configuration)
{
    const auto percent = [&condition](float value) {
        // DirectInput throttle is internally normalized to -1..+1, but the
        // flight-control UI exposes it as its natural 0..100 percent travel.
        const float display = condition.axis == static_cast<int>(PhysicalAxis::Z)
            ? (value + 1.0F) * 50.0F : value * 100.0F;
        return QString::number(display, 'f', 0) + u"%"_qs;
    };
    switch (condition.type) {
    case AutomationConditionType::Always: return u"All the time"_qs;
    case AutomationConditionType::AxisAbove: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is above "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisBelow: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is below "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisBetween: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is between "_qs + percent(condition.minimum) + u" and "_qs + percent(condition.maximum);
    case AutomationConditionType::AxisOutsideRange: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is outside "_qs + percent(condition.minimum) + u" to "_qs + percent(condition.maximum);
    case AutomationConditionType::ButtonHeld: return QString(u"Button %1 is held"_qs).arg(condition.button);
    case AutomationConditionType::ButtonReleased: return QString(u"Button %1 is not held"_qs).arg(condition.button);
    case AutomationConditionType::ButtonPressed: return QString(u"Button %1 is pressed"_qs).arg(condition.button);
    case AutomationConditionType::ButtonReleaseEvent: return QString(u"Button %1 is released"_qs).arg(condition.button);
    case AutomationConditionType::ButtonMultiPress: return QString(u"Button %1 is pressed %2 times"_qs)
        .arg(condition.button).arg(condition.pressCount);
    case AutomationConditionType::ButtonLongPress: return QString(u"Button %1 is held for %2 ms"_qs)
        .arg(condition.button).arg(condition.longPressDurationMs);
    case AutomationConditionType::AxisCrossesAbove: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" crosses above "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisCrossesBelow: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" crosses below "_qs + percent(condition.minimum);
    case AutomationConditionType::PovActive: return QString(u"POV %1 points %2"_qs).arg(condition.povHat)
        .arg(povDirectionLabel(condition.povDirection));
    case AutomationConditionType::PovInactive: return QString(u"POV %1 is not pointing %2"_qs).arg(condition.povHat)
        .arg(povDirectionLabel(condition.povDirection));
    case AutomationConditionType::BaseProfileIs: return u"Selected profile is "_qs
        + automationProfileName(configuration, condition.profileId);
    case AutomationConditionType::EffectiveProfileIs: return u"Active profile is "_qs
        + automationProfileName(configuration, condition.profileId);
    }
    return u"Invalid condition"_qs;
}

QString automationActionSummary(const AutomationActionDefinition &action,
                                const MapperConfiguration &configuration)
{
    const auto percent = [](float value) { return QString::number(value * 100.0F, 'f', 0) + u"%"_qs; };
    switch (action.type) {
    case AutomationActionType::VJoyButtonHold: return QString(u"Press and hold virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::VJoyButtonToggle: return QString(u"Toggle virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::VJoyButtonTap: return QString(u"Tap virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::ProfileHold: return u"Use "_qs
        + automationProfileName(configuration, action.profileId) + u" while active"_qs;
    case AutomationActionType::ProfileToggle: return u"Switch to "_qs
        + automationProfileName(configuration, action.profileId);
    case AutomationActionType::AxisScale: return u"Change "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" sensitivity to "_qs
        + percent(action.value);
    case AutomationActionType::AxisOffset: return u"Adjust "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" output by "_qs
        + percent(action.value);
    case AutomationActionType::AxisClamp: return u"Limit "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" output to "_qs
        + percent(action.minimum) + u"–"_qs + percent(action.maximum);
    case AutomationActionType::AxisOverride: return u"Force "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" to "_qs
        + percent(action.value);
    case AutomationActionType::AxisMix: return u"Mix "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.sourceAxis))
        + (action.sourceStage == AutomationAxisSourceStage::Physical ? u" from controller input into "_qs
                                                                     : u" from current mapped output into "_qs)
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" at "_qs + percent(action.value);
    case AutomationActionType::AxisFollow: {
        QString summary = u"Make "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis))
            + u" follow "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.sourceAxis))
            + (action.sourceStage == AutomationAxisSourceStage::Physical ? u" from controller input at "_qs
                                                                         : u" from current mapped output at "_qs)
            + percent(action.value);
        if (std::abs(action.offset) > 0.00001F) summary += u" with "_qs + percent(action.offset) + u" offset"_qs;
        return summary;
    }
    case AutomationActionType::MappingOn: return u"Turn mapping on"_qs;
    case AutomationActionType::MappingOff: return u"Turn mapping off"_qs;
    case AutomationActionType::ToggleMapping: return u"Toggle mapping on or off"_qs;
    case AutomationActionType::AdaptiveResponseEnable: return u"Temporarily enable Adaptive Response on "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis));
    case AutomationActionType::AdaptiveResponseDisable: return u"Temporarily disable Adaptive Response on "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis));
    case AutomationActionType::AdaptiveResponsePreset: return u"Temporarily apply Adaptive Response preset "_qs
        + action.adaptiveResponsePresetId + u" to "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis));
    }
    return u"Invalid action"_qs;
}

bool automationDefinitionFromVariant(const QVariantMap &map, AutomationDefinition *definition,
                                     QString *reason)
{
    if (!definition) return false;
    AutomationDefinition restored;
    restored.id = map.value(u"id"_qs).toString().trimmed();
    restored.name = map.value(u"name"_qs).toString().trimmed();
    restored.enabled = map.value(u"enabled"_qs, true).toBool();
    restored.matchMode = static_cast<AutomationMatchMode>(map.value(u"matchMode"_qs, 0).toInt());
    restored.activationMode = static_cast<AutomationActivationMode>(map.value(u"activationMode"_qs, 0).toInt());
    restored.activeDurationMs = map.value(u"activeDurationMs"_qs, 250).toInt();
    restored.priority = std::clamp(map.value(u"priority"_qs, 50).toInt(), 0, 100);
    if (restored.id.isEmpty() || restored.name.isEmpty() || restored.name.size() > 64
        || (restored.matchMode != AutomationMatchMode::All && restored.matchMode != AutomationMatchMode::Any)
        || (restored.activationMode != AutomationActivationMode::WhileTriggerActive
            && restored.activationMode != AutomationActivationMode::ToggleOnTrigger
            && restored.activationMode != AutomationActivationMode::RunBriefly)
        || restored.activeDurationMs < kAutomationMinimumRuleActiveDurationMs
        || restored.activeDurationMs > kAutomationMaximumRuleActiveDurationMs) {
        if (reason) *reason = u"Name and match mode are required."_qs;
        return false;
    }
    const QVariantList conditions = map.value(u"conditions"_qs).toList();
    const QVariantList actions = map.value(u"actions"_qs).toList();
    if (conditions.empty() || conditions.size() > kMaximumAutomationConditions
        || actions.empty() || actions.size() > kMaximumAutomationActions) {
        if (reason) *reason = u"Rules support one to four conditions and actions."_qs;
        return false;
    }
    for (const QVariant &value : conditions) {
        const QVariantMap input = value.toMap();
        AutomationConditionDefinition condition;
        condition.type = static_cast<AutomationConditionType>(input.value(u"type"_qs).toInt());
        condition.axis = input.value(u"axis"_qs, 0).toInt();
        condition.minimum = static_cast<float>(input.value(u"minimum"_qs, 0.0).toDouble());
        condition.maximum = static_cast<float>(input.value(u"maximum"_qs, 0.0).toDouble());
        condition.hysteresis = static_cast<float>(input.value(u"hysteresis"_qs, 0.0).toDouble());
        condition.button = input.value(u"button"_qs, 1).toInt();
        condition.povHat = input.value(u"povHat"_qs, 1).toInt();
        condition.povDirection = static_cast<PovDirection>(input.value(u"povDirection"_qs,
            static_cast<int>(PovDirection::Up)).toInt());
        condition.profileId = input.value(u"profileId"_qs).toString().trimmed();
        condition.pressCount = input.value(u"pressCount"_qs, 2).toInt();
        condition.multiPressWindowMs = input.value(u"multiPressWindowMs"_qs, 350).toInt();
        condition.longPressDurationMs = input.value(u"longPressDurationMs"_qs, 600).toInt();
        if (static_cast<int>(condition.type) < static_cast<int>(AutomationConditionType::Always)
            || static_cast<int>(condition.type) > static_cast<int>(AutomationConditionType::AxisCrossesBelow)) {
            if (reason) *reason = u"Condition type is invalid."_qs;
            return false;
        }
        restored.conditions.push_back(std::move(condition));
    }
    for (const QVariant &value : actions) {
        const QVariantMap input = value.toMap();
        AutomationActionDefinition action;
        action.type = static_cast<AutomationActionType>(input.value(u"type"_qs).toInt());
        action.virtualButton = input.value(u"virtualButton"_qs, 1).toInt();
        action.profileId = input.value(u"profileId"_qs).toString().trimmed();
        action.adaptiveResponsePresetId = input.value(u"adaptiveResponsePresetId"_qs)
            .toString().trimmed();
        action.targetAxis = input.value(u"targetAxis"_qs, 0).toInt();
        action.sourceAxis = input.value(u"sourceAxis"_qs, 0).toInt();
        action.sourceStage = static_cast<AutomationAxisSourceStage>(input.value(u"sourceStage"_qs,
            static_cast<int>(AutomationAxisSourceStage::Processed)).toInt());
        action.value = static_cast<float>(input.value(u"value"_qs, 0.0).toDouble());
        action.offset = static_cast<float>(input.value(u"offset"_qs, 0.0).toDouble());
        action.minimum = static_cast<float>(input.value(u"minimum"_qs, -1.0).toDouble());
        action.maximum = static_cast<float>(input.value(u"maximum"_qs, 1.0).toDouble());
        action.tapDurationMs = input.value(u"tapDurationMs"_qs, 80).toInt();
        if (static_cast<int>(action.type) < static_cast<int>(AutomationActionType::VJoyButtonHold)
            || static_cast<int>(action.type) > static_cast<int>(AutomationActionType::AdaptiveResponsePreset)) {
            if (reason) *reason = u"Action type is invalid."_qs;
            return false;
        }
        restored.actions.push_back(std::move(action));
    }
    *definition = std::move(restored);
    return true;
}

} // namespace

AppBackend::AppBackend(QObject *parent)
    : QObject(parent), m_configuration(ConfigStore::load()), m_worker(m_configuration)
{
    m_adaptiveResponseHistoryClock.start();
    m_adaptiveResponseSimulatorClock.start();
    m_adaptiveResponseSimulatorHistory.resize(1800);
    m_adaptiveResponseSimulatorRecording.resize(3000);
    QSettings settings;
    m_controllerSetupSuggested = !settings.value(u"readiness/controllerSetupIntroSeen"_qs, false).toBool();
    if (m_readiness.hasPendingRecovery()) {
        ControllerReadinessPlan pending;
        pending.state = ControllerReadinessState::Attention;
        pending.physicalStatus = VerificationSubsystemState::Attention;
        pending.hidhideStatus = VerificationSubsystemState::Attention;
        pending.physicalSummary = QStringLiteral("A prior automatic setup transaction needs physical-controller recovery verification.");
        pending.hidhideSummary = QStringLiteral("HOTAS BF6 retained a narrow recovery record; connect the controller and use Undo Automatic Repair if visibility was not restored.");
        pending.status = QStringLiteral("RECOVERY PENDING — A prior automatic setup did not reach physical-controller verification.");
        pending.lastChecked = QDateTime::currentDateTime();
        m_readiness.adoptPlan(std::move(pending));
    }
    connect(&m_snapshotTimer, &QTimer::timeout, this, &AppBackend::refreshUiSnapshot);
    connect(&m_numericTelemetryTimer, &QTimer::timeout, this, &AppBackend::refreshNumericTelemetry);
    connect(&m_controllerDiscoveryTimer, &QTimer::timeout, this, &AppBackend::refreshControllerInventory);
    connect(&m_gameDetectionTimer, &QTimer::timeout, this, &AppBackend::evaluateGameDetection);
    connect(&m_worker, &MappingWorker::workerEvent, this, &AppBackend::appendEvent, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::hardwareStateChanged, this, [this] {
        // A generic hardware change also covers vJoy readiness and status.
        // Rebuild controller presentation only when the worker's active
        // DirectInput identity changed; inventory changes use their dedicated
        // low-frequency path below.
        if (deviceId() != m_controllerUiModelLiveDeviceId) rebuildControllerUiModel();
        emit stateChanged();
    }, Qt::QueuedConnection);
    m_uiPerformanceInstrumentationEnabled = qEnvironmentVariableIntValue("HOTAS_ENABLE_UI_PERFORMANCE_INSTRUMENTATION") != 0;
    if (m_uiPerformanceInstrumentationEnabled) {
        connect(this, &AppBackend::stateChanged, this, [this] { ++m_stateChangedNotifications; });
        connect(this, &AppBackend::telemetryChanged, this, [this] { ++m_telemetryChangedNotifications; });
        connect(this, &AppBackend::inputTelemetryChanged, this, [this] { ++m_inputTelemetryChangedNotifications; });
        connect(this, &AppBackend::buttonTelemetryChanged, this, [this] { ++m_buttonTelemetryChangedNotifications; });
        connect(this, &AppBackend::controllersChanged, this, [this] { ++m_controllersChangedNotifications; });
        m_uiEventLoopHeartbeatClock.start();
        m_uiEventLoopHeartbeatTimer.setInterval(16);
        connect(&m_uiEventLoopHeartbeatTimer, &QTimer::timeout, this, [this] {
            const qint64 elapsed = m_uiEventLoopHeartbeatClock.restart();
            m_uiEventLoopMaxDelayMs = std::max(m_uiEventLoopMaxDelayMs, elapsed);
            if (elapsed > 16) ++m_uiEventLoopDelayOver16Ms;
            if (elapsed > 50) ++m_uiEventLoopDelayOver50Ms;
            if (elapsed > 100) ++m_uiEventLoopDelayOver100Ms;
            if (elapsed > 250) ++m_uiEventLoopDelayOver250Ms;
        });
        m_uiEventLoopHeartbeatTimer.start();
    }
    connect(&m_worker, &MappingWorker::buttonConfigurationSuggested, this,
            &AppBackend::initializeDefaultButtonMappings, Qt::QueuedConnection);
    m_snapshotTimer.setInterval(kVisibleSnapshotIntervalMs);
    m_snapshotTimer.start();
    m_adaptiveResponseHistoryTimer.setInterval(kAdaptiveResponseHistoryIntervalMs);
    connect(&m_adaptiveResponseHistoryTimer, &QTimer::timeout, this,
            &AppBackend::sampleAdaptiveResponseHistory);
    m_adaptiveResponseHistoryClock.start();
    // The 83 Hz sampler is started only for connected, requested mapping from
    // refreshUiSnapshot(). An inactive mapper must not keep the GUI event
    // queue busy merely to record a flat line.
    m_numericTelemetryTimer.setInterval(kVisibleNumericTelemetryIntervalMs);
    m_numericTelemetryTimer.start();
    // DirectInput enumeration is an independent, low-frequency control-plane
    // snapshot.  The report loop neither waits for it nor reads its results.
    m_controllerDiscoveryTimer.setInterval(kVisibleControllerDiscoveryIntervalMs);
    m_controllerDiscoveryTimer.start();
    // Foreground-process sampling is low-frequency control-plane work. It is
    // intentionally independent from the presentation snapshot and
    // the DirectInput worker's report loop.
    m_gameDetectionTimer.setInterval(kVisibleGameDetectionIntervalMs);
    if (m_configuration.automaticGameDetection) m_gameDetectionTimer.start();
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(QIcon(u":/assets/icons/png/hotas-bf6-256.png"_qs), this);
        m_trayMenu = new QMenu();
        m_trayStatusAction = m_trayMenu->addAction(u"HOTAS BF6 — Starting"_qs);
        m_trayStatusAction->setEnabled(false);
        m_trayMenu->addSeparator();
        QAction *open = m_trayMenu->addAction(u"Open HOTAS BF6"_qs);
        connect(open, &QAction::triggered, this, &AppBackend::restoreFromTray);
        m_trayToggleAction = m_trayMenu->addAction(u"Start Mapping"_qs);
        connect(m_trayToggleAction, &QAction::triggered, this, &AppBackend::toggleMapping);
        m_trayMenu->addSeparator();
        QAction *exit = m_trayMenu->addAction(u"Exit HOTAS BF6"_qs);
        connect(exit, &QAction::triggered, this, &AppBackend::exitApplication);
        setTrayTheme(u"Standard"_qs);
        m_trayIcon->setContextMenu(m_trayMenu);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger) restoreFromTray();
                });
        m_trayIcon->show();
    }
    m_rateClock.start();
    m_physicalUpdateClock.start();
    m_latencyPercentileClock.start();
    m_overviewMetricsClock.start();
    m_updateTimeout.setSingleShot(true);
    connect(&m_updateTimeout, &QTimer::timeout, this, [this] {
        if (!m_updateReply) return;
        m_updateTimedOut = true;
        m_updateReply->abort();
    });
    rebuildSelectedAxisCurve();
    rebuildCurveAxisChoices();
    rebuildControllerUiModel();
    rebuildButtonUiModel();
    appendEvent(u"HOTAS Mapper ready"_qs);
    // The mapping thread consumes physical reports while the GUI may be
    // rebuilding editor data. HighPriority is intentionally below
    // TimeCriticalPriority: it favors real input responsiveness without
    // starving normal system or rendering work on a constrained CPU.
    m_worker.start(QThread::HighPriority);
    m_mappingDesired = m_configuration.startMappingOnLaunch;
    if (m_mappingDesired) {
        m_worker.setMappingEnabled(true);
    }
    // Startup verification is passive and runs on its own short-lived worker.
    // It never stops mapping, releases either device, or opens a modal.
    QTimer::singleShot(750, this, &AppBackend::startQuickVerification);
    QTimer::singleShot(100, this, &AppBackend::refreshControllerInventory);
    if (m_configuration.automaticGameDetection) {
        QTimer::singleShot(kVisibleGameDetectionIntervalMs, this, &AppBackend::evaluateGameDetection);
    }
    // Update network activity is intentionally scheduled on the UI event loop
    // after startup. It never enters the DirectInput/vJoy worker or its hot
    // path, and a bounded timeout leaves mapper startup fully independent.
    QTimer::singleShot(500, this, &AppBackend::checkForUpdates);
}

AppBackend::~AppBackend()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
        // QSystemTrayIcon does not own the QMenu, so detach it before the
        // backend releases the menu during application shutdown.
        m_trayIcon->setContextMenu(nullptr);
    }
    delete m_trayMenu;
    if (m_verificationThread) {
        QThread *thread = m_verificationThread;
        thread->disconnect(this);
        thread->wait(5000);
        delete thread;
    }
    if (m_controllerSelectionThread) {
        QThread *thread = m_controllerSelectionThread;
        thread->disconnect(this);
        thread->wait(5000);
        delete thread;
    }
    if (m_controllerDiscoveryThread) {
        QThread *thread = m_controllerDiscoveryThread;
        thread->disconnect(this);
        thread->wait(5000);
        delete thread;
    }
    if (m_gameDetectionThread) {
        QThread *thread = m_gameDetectionThread;
        thread->disconnect(this);
        thread->wait(5000);
        delete thread;
    }
    m_worker.requestStop();
    // The mapper owns DirectInput and vJoy handles. Releasing the backend
    // while its report thread is still unwinding destroys a live QThread and
    // can abort application shutdown. Stop is observed at the next bounded
    // poll boundary, so join before member destruction.
    m_worker.wait();
}

QVariantList AppBackend::axes() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const ControllerProfile &profile = currentProfile();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const auto axis = static_cast<PhysicalAxis>(index);
        const AxisMapping &mapping = profile.axes[index];
        QVariantMap item;
        item.insert(u"index"_qs, index);
        item.insert(u"key"_qs, physicalAxisKey(axis));
        const QString hardwareLabel = physicalAxisLabel(axis);
        const QString customLabel = mapping.customName.trimmed();
        item.insert(u"label"_qs, customLabel.isEmpty() ? hardwareLabel : customLabel);
        item.insert(u"hardwareLabel"_qs, hardwareLabel);
        item.insert(u"customName"_qs, customLabel);
        item.insert(u"detail"_qs, physicalAxisDetail(axis));
        item.insert(u"available"_qs, runtime.axisAvailable[index].load());
        const PhysicalAxisActivity activity = static_cast<PhysicalAxisActivity>(
            runtime.axisActivity[index].load());
        const bool fixed = activity == PhysicalAxisActivity::Fixed;
        item.insert(u"activity"_qs, physicalAxisActivityKey(activity));
        item.insert(u"activityLabel"_qs, physicalAxisActivityLabel(activity));
        item.insert(u"activityDetail"_qs, fixed
            ? u"No meaningful movement observed during completed calibration"_qs : QString{});
        item.insert(u"fixed"_qs, fixed);
        item.insert(u"raw"_qs, runtime.raw[index].load());
        // Normal screens deliberately use the calibrated coordinate system.
        // Raw remains exposed separately for calibration and support work.
        item.insert(u"calibrated"_qs, runtime.normalized[index].load());
        item.insert(u"curveResponse"_qs, runtime.curveResponse[index].load());
        item.insert(u"transformed"_qs, runtime.transformed[index].load());
        item.insert(u"target"_qs, virtualAxisLabel(mapping.target));
        const int targetIndex = static_cast<int>(mapping.target);
        const bool virtualRouted = !fixed && targetIndex > 0 && targetIndex < kVirtualAxisSlotCount
            && runtime.virtualAxisAvailable[static_cast<size_t>(targetIndex)].load();
        const float virtualValue = virtualRouted ? runtime.virtualValues[index].load()
                                                 : std::numeric_limits<float>::quiet_NaN();
        item.insert(u"virtualValue"_qs, virtualValue);
        item.insert(u"virtualRouted"_qs, virtualRouted);
        item.insert(u"virtualValid"_qs, virtualRouted && std::isfinite(virtualValue));
        const QString alias = targetIndex > 0 && targetIndex < kVirtualAxisSlotCount
            ? profile.virtualAxisAliases[static_cast<size_t>(targetIndex)].trimmed() : QString{};
        item.insert(u"outputAlias"_qs, alias);
        item.insert(u"targetAvailable"_qs, targetIndex == 0 || runtime.virtualAxisAvailable[
            static_cast<size_t>(targetIndex)].load());
        item.insert(u"rangeMode"_qs, axisRangeModeKey(mapping.rangeMode));
        item.insert(u"rangeModeLabel"_qs, axisRangeModeLabel(mapping.rangeMode));
        item.insert(u"inverted"_qs, mapping.inverted);
        item.insert(u"deadzone"_qs, mapping.deadzone);
        item.insert(u"hysteresis"_qs, mapping.hysteresis);
        item.insert(u"outputMinimum"_qs, mapping.outputMinimum);
        item.insert(u"outputMaximum"_qs, mapping.outputMaximum);
        item.insert(u"curveSummary"_qs, curveDefinitionSummary(mapping.curve));
        item.insert(u"curvePointEditing"_qs, mapping.curve.pointEditing);
        item.insert(u"unipolar"_qs, mapping.rangeMode == AxisRangeMode::OneSided);
        item.insert(u"calibrationEnabled"_qs, m_configuration.calibration[index].enabled);
        item.insert(u"calibrationCentered"_qs, m_configuration.calibration[index].centered);
        const CalibrationCaptureAxis &capture = m_calibrationCapture[static_cast<size_t>(index)];
        const bool showingCapture = m_calibrationStage != CalibrationStageState::Idle && capture.available;
        item.insert(u"calibrationMinimum"_qs, showingCapture ? capture.minimum
            : runtime.calibrationMinimum[index].load());
        item.insert(u"calibrationCenter"_qs, showingCapture && capture.centerSampleCount > 0
            ? robustCalibrationCenter(capture.centerSamples, capture.centerSampleCount)
            : runtime.calibrationCenter[index].load());
        item.insert(u"calibrationMaximum"_qs, showingCapture ? capture.maximum
            : runtime.calibrationMaximum[index].load());
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::curveAxisChoices() const
{
    return m_curveAxisChoices;
}

int AppBackend::selectedAxisIndex() const
{
    return m_configuration.selectedAxisIndex;
}

QVariantList AppBackend::selectedAxisCurve() const
{
    return m_selectedAxisCurve;
}

QVariantList AppBackend::curveEditorResponseCurve() const
{
    return m_curveEditorResponseCurve;
}

QVariantList AppBackend::curveGainSamples() const
{
    return m_curveGainSamples;
}

QVariantList AppBackend::curveComparisonCurve() const
{
    return m_curveComparisonCurve;
}

QVariantList AppBackend::curvePreviewCurve() const
{
    return m_curvePreviewCurve;
}

QVariantList AppBackend::selectedCurvePoints() const
{
    QVariantList points;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing) return points;
    for (int index = 0; index < static_cast<int>(mapping->curve.points.size()); ++index) {
        const CurvePoint &point = mapping->curve.points[static_cast<size_t>(index)];
        QVariantMap item;
        item.insert(u"index"_qs, index);
        item.insert(u"input"_qs, point.input);
        item.insert(u"output"_qs, point.output);
        item.insert(u"locked"_qs, point.locked);
        points.append(item);
    }
    return points;
}

QVariantMap AppBackend::curveEditorState() const
{
    QVariantMap state;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return state;
    const bool unipolar = mapping->rangeMode == AxisRangeMode::OneSided;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const float raw = runtime.raw[m_configuration.selectedAxisIndex].load();
    const float domainInput = runtime.normalized[m_configuration.selectedAxisIndex].load();
    const float afterInversion = runtime.afterInversion[m_configuration.selectedAxisIndex].load();
    const float curveInput = afterInversion;
    state.insert(u"family"_qs, curveFamilyLabel(mapping->curve.family));
    state.insert(u"familyId"_qs, static_cast<int>(mapping->curve.family));
    state.insert(u"presetId"_qs, mapping->curve.presetId);
    state.insert(u"sourceFamilyId"_qs, static_cast<int>(mapping->curve.sourceFamily));
    state.insert(u"sourcePresetId"_qs, mapping->curve.sourcePresetId);
    state.insert(u"summary"_qs, curveDefinitionSummary(mapping->curve));
    state.insert(u"baseLabel"_qs, mapping->curve.baseLabel);
    state.insert(u"strength"_qs, mapping->curve.family == CurveFamily::Linear ? 0.0 : mapping->curve.strength);
    state.insert(u"pointEditing"_qs, mapping->curve.pointEditing);
    state.insert(u"symmetry"_qs, mapping->curve.symmetry);
    state.insert(u"interpolation"_qs, curveInterpolationLabel(mapping->curve.interpolation));
    state.insert(u"pointDensity"_qs, mapping->curve.pointDensity);
    state.insert(u"pointCount"_qs, static_cast<int>(mapping->curve.points.size()));
    state.insert(u"unipolar"_qs, unipolar);
    state.insert(u"rawPhysicalInput"_qs, raw);
    state.insert(u"physicalInput"_qs, domainInput);
    state.insert(u"normalized"_qs, domainInput);
    state.insert(u"afterDeadzone"_qs, runtime.afterDeadzone[m_configuration.selectedAxisIndex].load());
    state.insert(u"afterHysteresis"_qs, runtime.afterHysteresis[m_configuration.selectedAxisIndex].load());
    state.insert(u"afterInversion"_qs, afterInversion);
    state.insert(u"curveInput"_qs, afterInversion);
    state.insert(u"curveResponse"_qs, runtime.curveResponse[m_configuration.selectedAxisIndex].load());
    state.insert(u"finalOutput"_qs, runtime.transformed[m_configuration.selectedAxisIndex].load());
    state.insert(u"localGain"_qs, evaluateCurveGain(curveInput, mapping->curve, unipolar));
    state.insert(u"runtimeLutSamples"_qs, kResponseCurveLutSamples);
    state.insert(u"lastCurveCompileUs"_qs, static_cast<qulonglong>(m_worker.runtime().lastCurveCompileUs.load()));
    state.insert(u"previewLabel"_qs, m_curvePreviewLabel);
    const CurveAnalysis health = analyzeCurveDefinition(mapping->curve, unipolar);
    state.insert(u"neutralOffset"_qs, health.neutralOffset);
    state.insert(u"neutralMapsToNeutral"_qs, health.neutralMapsToNeutral);
    if (const AdvancedCurvePresetInfo *advanced = mapping->curve.family == CurveFamily::Advanced
            ? advancedCurvePreset(mapping->curve.presetId) : nullptr) {
        state.insert(u"advancedBestFor"_qs, advanced->bestFor);
        state.insert(u"advancedCategory"_qs, advanced->category);
        state.insert(u"advancedBehavior"_qs, advanced->behavior);
        state.insert(u"advancedSourceBasis"_qs, advanced->provenance);
    }
    return state;
}

QVariantMap AppBackend::curveAnalysis() const
{
    return m_curveAnalysis;
}

QVariantMap AppBackend::curveEditorTelemetry() const
{
    QVariantMap telemetry;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return telemetry;
    const int index = m_configuration.selectedAxisIndex;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const bool oneSided = mapping->rangeMode == AxisRangeMode::OneSided;
    const float afterInversion = runtime.afterInversion[index].load();
    telemetry.insert(u"physicalInput"_qs, runtime.normalized[index].load());
    telemetry.insert(u"afterDeadzone"_qs, runtime.afterDeadzone[index].load());
    telemetry.insert(u"afterHysteresis"_qs, runtime.afterHysteresis[index].load());
    telemetry.insert(u"afterInversion"_qs, afterInversion);
    telemetry.insert(u"curveResponse"_qs, runtime.curveResponse[index].load());
    telemetry.insert(u"finalOutput"_qs, runtime.transformed[index].load());
    telemetry.insert(u"localGain"_qs, evaluateCurveGain(afterInversion, mapping->curve, oneSided));
    return telemetry;
}

QVariantMap AppBackend::curveComparisonState() const
{
    QVariantMap state;
    if (m_curveComparisonId.isEmpty()) return state;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return state;
    const bool unipolar = mapping->rangeMode == AxisRangeMode::OneSided;
    const CurveDefinition comparison = comparisonCurveDefinition();
    const float input = m_worker.runtime().normalized[m_configuration.selectedAxisIndex].load();
    const float current = evaluateCurveDefinition(input, mapping->curve, unipolar);
    const float reference = evaluateCurveDefinition(input, comparison, unipolar);
    state.insert(u"label"_qs, m_curveComparisonLabel);
    state.insert(u"currentOutput"_qs, current);
    state.insert(u"referenceOutput"_qs, reference);
    state.insert(u"difference"_qs, current - reference);
    state.insert(u"currentGain"_qs, evaluateCurveGain(input, mapping->curve, unipolar));
    state.insert(u"referenceGain"_qs, evaluateCurveGain(input, comparison, unipolar));
    return state;
}

QVariantList AppBackend::curveStandardPresets() const
{
    QVariantList result;
    for (const CurvePresetInfo &preset : standardCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
                                  {u"strength"_qs, preset.strength}});
    }
    return result;
}

QVariantList AppBackend::curveAdvancedPresets() const
{
    QVariantList result;
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        const CurveDefinition definition = advancedCurveDefinition(preset.id);
        const CurveAnalysis analysis = analyzeCurveDefinition(definition, false);
        const float edgeGain = evaluateCurveGain(0.90F, definition, false);
        const auto responseBand = [](float gain) {
            if (gain < 0.35F) return u"Very Soft"_qs;
            if (gain < 0.75F) return u"Soft"_qs;
            if (gain <= 1.25F) return u"Moderate"_qs;
            return u"Strong"_qs;
        };
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
            {u"category"_qs, preset.category}, {u"bestFor"_qs, preset.bestFor}, {u"behavior"_qs, preset.behavior},
            {u"sourceBasis"_qs, preset.provenance},
            {u"centerResponse"_qs, responseBand(analysis.centerGain)},
            {u"midrangeResponse"_qs, responseBand(analysis.halfGain)},
            {u"edgeResponse"_qs, responseBand(edgeGain)},
            {u"centerGain"_qs, analysis.centerGain}, {u"quarterGain"_qs, analysis.quarterGain},
            {u"midrangeGain"_qs, analysis.halfGain}, {u"threeQuarterGain"_qs, analysis.threeQuarterGain},
            {u"peakGain"_qs, analysis.peakGain}, {u"symmetric"_qs, true},
            {u"fullAuthority"_qs, analysis.fullAuthority},
            {u"strengthBehavior"_qs, u"0% Linear · 100% full researched response"_qs}});
    }
    return result;
}

QVariantList AppBackend::personalCurvePresets() const
{
    QVariantList result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != unipolar) continue;
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
                                  {u"description"_qs, preset.description},
                                  {u"summary"_qs, curveDefinitionSummary(preset.definition)}});
    }
    return result;
}

QVariantList AppBackend::curveComparisonChoices() const
{
    QVariantList result;
    result.append(QVariantMap{{u"id"_qs, QString{}}, {u"label"_qs, u"None"_qs}});
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const AxisMapping *current = selectedAxisMapping();
    if (current && current->curve.family == CurveFamily::Custom
        && current->curve.sourceFamily != CurveFamily::Linear) {
        result.append(QVariantMap{{u"id"_qs, u"source"_qs},
            {u"label"_qs, u"Source · "_qs + current->curve.baseLabel}});
    }
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if ((profile.axes[static_cast<size_t>(axis)].rangeMode == AxisRangeMode::OneSided) != unipolar) continue;
            result.append(QVariantMap{{u"id"_qs, QString(u"profile:%1:%2"_qs).arg(profile.id).arg(axis)},
                {u"label"_qs, profile.name + u" / "_qs
                    + physicalAxisLabel(static_cast<PhysicalAxis>(axis))}});
        }
    }
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, u"advanced:"_qs + preset.id},
            {u"label"_qs, u"Advanced · "_qs + preset.name}});
    }
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != unipolar) continue;
        result.append(QVariantMap{{u"id"_qs, u"personal:"_qs + preset.id},
            {u"label"_qs, u"Personal · "_qs + preset.name}});
    }
    return result;
}

QVariantList AppBackend::curvePreviewChoices() const
{
    QVariantList result;
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, u"advanced:"_qs + preset.id},
            {u"label"_qs, u"Advanced · "_qs + preset.name}});
    }
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != axisIsOneSided(m_configuration.selectedAxisIndex)) continue;
        result.append(QVariantMap{{u"id"_qs, u"personal:"_qs + preset.id},
            {u"label"_qs, u"Personal · "_qs + preset.name}});
    }
    return result;
}

QVariantList AppBackend::curveCopyChoices() const
{
    QVariantList result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if ((profile.axes[static_cast<size_t>(axis)].rangeMode == AxisRangeMode::OneSided) != unipolar) continue;
            result.append(QVariantMap{{u"id"_qs, QString(u"%1:%2"_qs).arg(profile.id).arg(axis)},
                {u"label"_qs, profile.name + u" / "_qs
                    + physicalAxisLabel(static_cast<PhysicalAxis>(axis)) + u" · "_qs
                    + curveDefinitionSummary(profile.axes[axis].curve)}});
        }
    }
    return result;
}

namespace {

std::uint32_t adaptivePropertyForKey(const QString &key)
{
    const QString normalized = key.trimmed().toCaseFolded();
    if (normalized == u"enabled"_qs) return AdaptiveResponseEnabled;
    if (normalized == u"model"_qs) return AdaptiveResponseModelProperty;
    if (normalized == u"maximumhorizonms"_qs) return AdaptiveResponseMaximumHorizon;
    if (normalized == u"maximumlead"_qs) return AdaptiveResponseMaximumLead;
    if (normalized == u"velocityresponse"_qs) return AdaptiveResponseVelocityResponse;
    if (normalized == u"accelerationresponse"_qs) return AdaptiveResponseAccelerationResponse;
    if (normalized == u"motionsensitivity"_qs) return AdaptiveResponseMotionSensitivity;
    if (normalized == u"noiserejection"_qs) return AdaptiveResponseNoiseRejection;
    if (normalized == u"reversaldetection"_qs) return AdaptiveResponseReversalDetection;
    if (normalized == u"reversalresponse"_qs) return AdaptiveResponseReversalResponse;
    if (normalized == u"decelerationresponse"_qs) return AdaptiveResponseDecelerationResponse;
    if (normalized == u"settlingresponse"_qs) return AdaptiveResponseSettlingResponse;
    if (normalized == u"endpointtaper"_qs) return AdaptiveResponseEndpointTaper;
    if (normalized == u"onsetassist"_qs) return AdaptiveResponseOnsetAssist;
    if (normalized == u"onsetcap"_qs) return AdaptiveResponseOnsetCap;
    if (normalized == u"sustainedassist"_qs) return AdaptiveResponseSustainedAssist;
    if (normalized == u"sustainedcap"_qs) return AdaptiveResponseSustainedCap;
    if (normalized == u"horizonextension"_qs) return AdaptiveResponseHorizonExtension;
    if (normalized == u"horizonextensioncap"_qs || normalized == u"horizonextensioncapms"_qs) return AdaptiveResponseHorizonExtensionCap;
    if (normalized == u"turningpointprotection"_qs) return AdaptiveResponseTurningPointProtection;
    if (normalized == u"turningpointmargin"_qs) return AdaptiveResponseTurningPointMargin;
    return 0;
}

AdaptiveResponseSettings settingsFromRuntime(const RuntimeAdaptiveResponseConfig &runtime)
{
    AdaptiveResponseSettings settings;
    settings.enabled = runtime.enabled;
    settings.model = runtime.model;
    settings.maximumHorizonMs = runtime.maximumHorizonSeconds * 1000.0F;
    settings.maximumLead = runtime.maximumLead;
    settings.velocityResponse = runtime.velocityResponse;
    settings.accelerationResponse = runtime.accelerationResponse;
    settings.motionSensitivity = runtime.motionSensitivity;
    settings.noiseRejection = runtime.noiseRejection;
    settings.reversalDetection = runtime.reversalDetection;
    settings.reversalResponse = runtime.reversalResponse;
    settings.decelerationResponse = runtime.decelerationResponse;
    settings.settlingResponse = runtime.settlingResponse;
    settings.endpointTaper = runtime.endpointTaper;
    settings.onsetAssist = runtime.onsetAssist;
    settings.onsetCap = runtime.onsetCap;
    settings.sustainedAssist = runtime.sustainedAssist;
    settings.sustainedCap = runtime.sustainedCap;
    settings.horizonExtension = runtime.horizonExtension;
    settings.horizonExtensionCapMs = runtime.horizonExtensionCapSeconds * 1000.0F;
    settings.turningPointProtection = runtime.turningPointProtection;
    settings.turningPointMargin = runtime.turningPointMargin;
    return sanitizedAdaptiveResponseSettings(settings);
}

QVariantMap adaptiveSettingsMap(const RuntimeAdaptiveResponseConfig &runtime)
{
    return {{u"enabled"_qs, runtime.enabled}, {u"model"_qs, adaptiveResponseModelKey(runtime.model)},
            {u"maximumHorizonMs"_qs, runtime.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, runtime.maximumLead}, {u"velocityResponse"_qs, runtime.velocityResponse},
            {u"accelerationResponse"_qs, runtime.accelerationResponse},
            {u"motionSensitivity"_qs, runtime.motionSensitivity}, {u"noiseRejection"_qs, runtime.noiseRejection},
            {u"reversalDetection"_qs, runtime.reversalDetection}, {u"reversalResponse"_qs, runtime.reversalResponse},
            {u"decelerationResponse"_qs, runtime.decelerationResponse}, {u"settlingResponse"_qs, runtime.settlingResponse},
            {u"endpointTaper"_qs, runtime.endpointTaper}, {u"onsetAssist"_qs, runtime.onsetAssist},
            {u"onsetCap"_qs, runtime.onsetCap}, {u"sustainedAssist"_qs, runtime.sustainedAssist},
            {u"sustainedCap"_qs, runtime.sustainedCap}, {u"horizonExtension"_qs, runtime.horizonExtension},
            {u"horizonExtensionCapMs"_qs, runtime.horizonExtensionCapSeconds * 1000.0F},
            {u"turningPointProtection"_qs, runtime.turningPointProtection},
            {u"turningPointMargin"_qs, runtime.turningPointMargin}};
}

QVariantMap adaptiveRuntimeSettingsMap(const AtomicRuntimeState &runtime, int axis,
                                       const RuntimeAdaptiveResponseConfig &fallback)
{
    const size_t index = static_cast<size_t>(axis);
    const bool published = runtime.physicalConnected.load()
        && runtime.adaptiveRuntimeMaximumHorizonSeconds[index].load() > 0.0F;
    if (!published) return adaptiveSettingsMap(fallback);
    return {{u"enabled"_qs, runtime.adaptiveRuntimeEnabled[index].load()},
            {u"model"_qs, adaptiveResponseModelKey(static_cast<AdaptiveResponseModel>(
                runtime.adaptiveRuntimeModel[index].load()))},
            {u"maximumHorizonMs"_qs, runtime.adaptiveRuntimeMaximumHorizonSeconds[index].load() * 1000.0F},
            {u"maximumLead"_qs, runtime.adaptiveRuntimeMaximumLead[index].load()},
            {u"velocityResponse"_qs, runtime.adaptiveRuntimeVelocityResponse[index].load()},
            {u"accelerationResponse"_qs, runtime.adaptiveRuntimeAccelerationResponse[index].load()},
            {u"motionSensitivity"_qs, runtime.adaptiveRuntimeMotionSensitivity[index].load()},
            {u"noiseRejection"_qs, runtime.adaptiveRuntimeNoiseRejection[index].load()},
            {u"reversalDetection"_qs, runtime.adaptiveRuntimeReversalDetection[index].load()},
            {u"reversalResponse"_qs, runtime.adaptiveRuntimeReversalResponse[index].load()},
            {u"decelerationResponse"_qs, runtime.adaptiveRuntimeDecelerationResponse[index].load()},
            {u"settlingResponse"_qs, runtime.adaptiveRuntimeSettlingResponse[index].load()},
            {u"endpointTaper"_qs, runtime.adaptiveRuntimeEndpointTaper[index].load()},
            {u"onsetAssist"_qs, runtime.adaptiveRuntimeOnsetAssist[index].load()},
            {u"onsetCap"_qs, runtime.adaptiveRuntimeOnsetCap[index].load()},
            {u"sustainedAssist"_qs, runtime.adaptiveRuntimeSustainedAssist[index].load()},
            {u"sustainedCap"_qs, runtime.adaptiveRuntimeSustainedCap[index].load()},
            {u"horizonExtension"_qs, runtime.adaptiveRuntimeHorizonExtension[index].load()},
            {u"horizonExtensionCapMs"_qs, runtime.adaptiveRuntimeHorizonExtensionCapSeconds[index].load() * 1000.0F},
            {u"turningPointProtection"_qs, runtime.adaptiveRuntimeTurningPointProtection[index].load()},
            {u"turningPointMargin"_qs, runtime.adaptiveRuntimeTurningPointMargin[index].load()}};
}

QString adaptiveSourceLabel(const AdaptiveResponseAxisOverride &override, const QString &fallback)
{
    if (override.properties != 0) return override.presetId.isEmpty() ? u"Custom"_qs
                                                                       : u"Custom / "_qs + override.presetId;
    return override.presetId.isEmpty() ? fallback : override.presetId;
}

QStringList adaptivePropertyLabels(std::uint32_t properties)
{
    struct PropertyLabel {
        AdaptiveResponseProperty property;
        QStringView label;
    };
    static constexpr std::array<PropertyLabel, 21> labels{{
        {AdaptiveResponseEnabled, u"Enabled"},
        {AdaptiveResponseModelProperty, u"Predictor"},
        {AdaptiveResponseMaximumHorizon, u"Maximum horizon"},
        {AdaptiveResponseMaximumLead, u"Maximum lead"},
        {AdaptiveResponseVelocityResponse, u"Velocity response"},
        {AdaptiveResponseAccelerationResponse, u"Acceleration response"},
        {AdaptiveResponseMotionSensitivity, u"Motion sensitivity"},
        {AdaptiveResponseNoiseRejection, u"Noise rejection"},
        {AdaptiveResponseReversalDetection, u"Reversal detection"},
        {AdaptiveResponseReversalResponse, u"Reversal response"},
        {AdaptiveResponseDecelerationResponse, u"Deceleration response"},
        {AdaptiveResponseSettlingResponse, u"Settling response"},
        {AdaptiveResponseEndpointTaper, u"Endpoint taper"},
        {AdaptiveResponseOnsetAssist, u"Onset Assist"},
        {AdaptiveResponseOnsetCap, u"Onset Cap"},
        {AdaptiveResponseSustainedAssist, u"Sustained Assist"},
        {AdaptiveResponseSustainedCap, u"Sustained Cap"},
        {AdaptiveResponseHorizonExtension, u"Adaptive Horizon Extension"},
        {AdaptiveResponseHorizonExtensionCap, u"Horizon Extension Cap"},
        {AdaptiveResponseTurningPointProtection, u"Turning-Point Protection"},
        {AdaptiveResponseTurningPointMargin, u"Turning-Point Margin"},
    }};
    QStringList result;
    for (const PropertyLabel &entry : labels) {
        if ((properties & static_cast<std::uint32_t>(entry.property)) != 0U) {
            result.append(entry.label.toString());
        }
    }
    return result;
}

} // namespace

RuntimeAdaptiveResponseConfig AppBackend::adaptiveResponseConfigurationAtContext(
    const QString &scope, const QString &targetId, int physicalAxis,
    AdaptiveResponseAxisOverride *contextOverride, QString *source,
    RuntimeAxisMapping *staticMapping) const
{
    const int axis = std::clamp(physicalAxis, 0, kPhysicalAxisCount - 1);
    const QString normalized = scope.trimmed().toCaseFolded();
    MapperConfiguration contextConfiguration = m_configuration;
    ControllerProfile contextProfile = currentProfile();
    AdaptiveResponseAxisOverride selected;
    QString label;

    if (normalized == u"global"_qs) {
        // Resolve only the global layer. A copied profile preserves the axis
        // domain while removing any downstream Category/Profile overrides.
        contextConfiguration.profileCategories.clear();
        contextProfile.categoryId.clear();
        contextProfile.adaptiveResponse = {};
        selected = contextConfiguration.adaptiveResponseGlobal.axes[static_cast<size_t>(axis)];
        label = adaptiveSourceLabel(selected, u"Application default"_qs);
    } else if (normalized == u"category"_qs) {
        const ProfileCategory *category = findProfileCategory(contextConfiguration, targetId.trimmed());
        if (!category) return {};
        contextProfile.categoryId = category->id;
        contextProfile.adaptiveResponse = {};
        selected = category->adaptiveResponse.axes[static_cast<size_t>(axis)];
        label = adaptiveSourceLabel(selected, u"Inherited"_qs);
    } else if (normalized == u"preset"_qs) {
        const AdaptiveResponsePreset *preset = findAdaptiveResponsePreset(contextConfiguration,
                                                                            targetId.trimmed());
        if (!preset) return {};
        // A preset is an independently editable all-property template; it is
        // intentionally previewed without any currently selected profile
        // override layered on top of it.
        contextConfiguration.profileCategories.clear();
        contextConfiguration.adaptiveResponseGlobal = {};
        contextConfiguration.adaptiveResponseGlobal.axes[static_cast<size_t>(axis)] =
            preset->axes[static_cast<size_t>(axis)];
        contextProfile.categoryId.clear();
        contextProfile.adaptiveResponse = {};
        selected = preset->axes[static_cast<size_t>(axis)];
        label = u"Response Preset"_qs;
    } else {
        const ControllerProfile *profile = findProfile(contextConfiguration, targetId.trimmed());
        if (!profile) return {};
        contextProfile = *profile;
        selected = contextProfile.adaptiveResponse.axes[static_cast<size_t>(axis)];
        label = adaptiveSourceLabel(selected, u"Inherited"_qs);
    }
    if (contextOverride) *contextOverride = selected;
    if (source) *source = label;
    const RuntimeAdaptiveResponseConfig effective = resolveAdaptiveResponseConfiguration(
        contextConfiguration, contextProfile, axis);
    if (staticMapping) {
        staticMapping->profile = contextProfile.axes[static_cast<size_t>(axis)];
        staticMapping->calibration = contextConfiguration.calibration[static_cast<size_t>(axis)];
        staticMapping->responseCurve = compileResponseCurve(staticMapping->profile.curve,
            staticMapping->profile.rangeMode == AxisRangeMode::OneSided);
        staticMapping->adaptiveResponse = effective;
    }
    return effective;
}

QVariantMap AppBackend::adaptiveResponseState() const
{
    const int axis = std::clamp(m_configuration.selectedAxisIndex, 0, kPhysicalAxisCount - 1);
    const ControllerProfile &profile = currentProfile();
    const ProfileCategory *category = findProfileCategory(m_configuration, profile.categoryId);
    const RuntimeAdaptiveResponseConfig effective =
        resolveAdaptiveResponseConfiguration(m_configuration, profile, axis);
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const AdaptiveResponseAxisOverride &global = m_configuration.adaptiveResponseGlobal.axes[axis];
    const AdaptiveResponseAxisOverride categoryEntry = category ? category->adaptiveResponse.axes[axis]
                                                                : AdaptiveResponseAxisOverride{};
    const AdaptiveResponseAxisOverride &profileEntry = profile.adaptiveResponse.axes[axis];
    return {{u"axis"_qs, axis}, {u"axisLabel"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(axis))},
            {u"profileId"_qs, profile.id}, {u"profile"_qs, profile.name},
            {u"categoryId"_qs, category ? category->id : QString{}},
            {u"category"_qs, category ? category->name : u"General"_qs},
            {u"effective"_qs, adaptiveSettingsMap(effective)},
            {u"runtimeEffective"_qs, adaptiveRuntimeSettingsMap(runtime, axis, effective)},
            {u"automation"_qs, QVariantMap{
                {u"active"_qs, runtime.adaptiveAutomationOverlayActive[static_cast<size_t>(axis)].load()},
                {u"properties"_qs, static_cast<qulonglong>(runtime.adaptiveAutomationOverlayProperties[
                    static_cast<size_t>(axis)].load())},
                {u"affectedProperties"_qs, adaptivePropertyLabels(
                    runtime.adaptiveAutomationOverlayProperties[static_cast<size_t>(axis)].load())}}},
            {u"global"_qs, QVariantMap{{u"presetId"_qs, global.presetId},
                {u"source"_qs, adaptiveSourceLabel(global, u"Application default"_qs)},
                {u"properties"_qs, static_cast<int>(global.properties)}}},
            {u"categoryLayer"_qs, QVariantMap{{u"presetId"_qs, categoryEntry.presetId},
                {u"source"_qs, adaptiveSourceLabel(categoryEntry, u"Inherited"_qs)},
                {u"properties"_qs, static_cast<int>(categoryEntry.properties)}}},
            {u"profileLayer"_qs, QVariantMap{{u"presetId"_qs, profileEntry.presetId},
                {u"source"_qs, adaptiveSourceLabel(profileEntry, u"Inherited"_qs)},
                {u"properties"_qs, static_cast<int>(profileEntry.properties)}}}};
}

QVariantList AppBackend::adaptiveResponsePresets() const
{
    QVariantList result;
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
            {u"description"_qs, preset.description}, {u"builtIn"_qs, true}});
    }
    for (const AdaptiveResponsePreset &preset : m_configuration.adaptiveResponsePresets) {
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
            {u"description"_qs, preset.description}, {u"builtIn"_qs, false}});
    }
    return result;
}

QVariantMap AppBackend::adaptiveResponseTelemetry() const
{
    const int axis = std::clamp(m_configuration.selectedAxisIndex, 0, kPhysicalAxisCount - 1);
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const RuntimeAdaptiveResponseConfig persistent =
        resolveAdaptiveResponseConfiguration(m_configuration, currentProfile(), axis);
    const auto load = [&runtime, axis](const auto &values) { return values[static_cast<size_t>(axis)].load(); };
    const bool runtimePublished = runtime.physicalConnected.load()
        && load(runtime.adaptiveRuntimeMaximumHorizonSeconds) > 0.0F;
    const float maximumHorizonSeconds = runtimePublished ? load(runtime.adaptiveRuntimeMaximumHorizonSeconds)
                                                        : persistent.maximumHorizonSeconds;
    const float maximumLead = runtimePublished ? load(runtime.adaptiveRuntimeMaximumLead)
                                                : persistent.maximumLead;
    const AdaptiveResponseModel model = runtimePublished
        ? static_cast<AdaptiveResponseModel>(load(runtime.adaptiveRuntimeModel)) : persistent.model;
    return {{u"physical"_qs, load(runtime.normalized)}, {u"estimated"_qs, load(runtime.adaptiveEstimated)},
            {u"predicted"_qs, load(runtime.adaptivePredicted)}, {u"virtualOutput"_qs, load(runtime.virtualValues)},
            {u"velocity"_qs, load(runtime.adaptiveVelocity)}, {u"acceleration"_qs, load(runtime.adaptiveAcceleration)},
            {u"activeHorizonMs"_qs, load(runtime.adaptiveHorizonSeconds) * 1000.0F},
            {u"maximumHorizonMs"_qs, maximumHorizonSeconds * 1000.0F},
            {u"lead"_qs, load(runtime.adaptiveLead)}, {u"maximumLead"_qs, maximumLead},
            {u"confidence"_qs, load(runtime.adaptiveConfidence)},
            {u"motionIntensity"_qs, load(runtime.adaptiveMotionIntensity)},
            {u"velocityAuthority"_qs, load(runtime.adaptiveVelocityAuthority)},
            {u"accelerationIntent"_qs, load(runtime.adaptiveAccelerationIntent)},
            {u"onsetAuthority"_qs, load(runtime.adaptiveOnsetAuthority)},
            {u"sustainedEvidence"_qs, load(runtime.adaptiveSustainedEvidence)},
            {u"sustainedAuthority"_qs, load(runtime.adaptiveSustainedAuthority)},
            {u"motionUrgency"_qs, load(runtime.adaptiveMotionUrgency)},
            {u"horizonExtensionEligibility"_qs, load(runtime.adaptiveHorizonExtensionEligibility)},
            {u"normalMaximumHorizonMs"_qs, load(runtime.adaptiveNormalMaximumHorizonSeconds) * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, load(runtime.adaptiveAllowedMaximumHorizonSeconds) * 1000.0F},
            {u"turningPointConfidence"_qs, load(runtime.adaptiveTurningPointConfidence)},
            {u"estimatedTimeToTurnMs"_qs, load(runtime.adaptiveEstimatedTimeToTurnSeconds) * 1000.0F},
            {u"estimatedRemainingTravel"_qs, load(runtime.adaptiveEstimatedRemainingTravel)},
            {u"turningPointHorizonLimitMs"_qs, load(runtime.adaptiveTurningPointHorizonLimitSeconds) * 1000.0F},
            {u"turningPointLeadLimit"_qs, load(runtime.adaptiveTurningPointLeadLimit)},
            {u"reacquisitionAuthority"_qs, load(runtime.adaptiveReacquisitionAuthority)},
            {u"state"_qs, adaptiveMotionStateLabel(static_cast<AdaptiveMotionState>(load(runtime.adaptiveMotionState)))},
            {u"model"_qs, adaptiveResponseModelKey(model)},
            {u"enabled"_qs, runtimePublished ? load(runtime.adaptiveRuntimeEnabled) : persistent.enabled},
            {u"automationOverlayActive"_qs, load(runtime.adaptiveAutomationOverlayActive)},
            {u"automationOverlayProperties"_qs, QVariant::fromValue(load(runtime.adaptiveAutomationOverlayProperties))},
            {u"reversing"_qs, load(runtime.adaptiveReversing)}, {u"safetyLimited"_qs, load(runtime.adaptiveSafetyLimited)},
            {u"reversalCount"_qs, QVariant::fromValue(load(runtime.adaptiveReversalCount))},
            {u"safetyClampCount"_qs, QVariant::fromValue(load(runtime.adaptiveSafetyClampCount))}};
}

QVariantList AppBackend::adaptiveResponseHistory(int seconds) const
{
    const int windowSeconds = std::clamp(seconds, 2, 30);
    const qint64 newestMs = m_adaptiveResponseHistoryCount > 0
        ? m_adaptiveResponseHistory[(m_adaptiveResponseHistoryNext + static_cast<int>(m_adaptiveResponseHistory.size()) - 1)
                                      % static_cast<int>(m_adaptiveResponseHistory.size())].elapsedMs
        : 0;
    const qint64 minimumMs = newestMs - static_cast<qint64>(windowSeconds) * 1000;
    const int selectedAxis = std::clamp(m_configuration.selectedAxisIndex, 0, kPhysicalAxisCount - 1);
    QVariantList result;
    result.reserve(m_adaptiveResponseHistoryCount);
    const int capacity = static_cast<int>(m_adaptiveResponseHistory.size());
    const int first = (m_adaptiveResponseHistoryNext - m_adaptiveResponseHistoryCount + capacity) % capacity;
    for (int offset = 0; offset < m_adaptiveResponseHistoryCount; ++offset) {
        const AdaptiveResponseHistorySample &sample = m_adaptiveResponseHistory[(first + offset) % capacity];
        if (sample.axis != selectedAxis || sample.elapsedMs < minimumMs) continue;
        result.append(QVariantMap{{u"sequence"_qs, sample.sequence},
                                  {u"timeMs"_qs, sample.elapsedMs - newestMs},
                                  {u"physical"_qs, sample.physical},
                                  {u"estimated"_qs, sample.estimated},
                                  {u"predicted"_qs, sample.predicted},
                                  {u"virtualOutput"_qs, sample.virtualOutput},
                                  {u"velocity"_qs, sample.velocity},
                                  {u"acceleration"_qs, sample.acceleration},
                                  {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
                                  {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
                                  {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                                      ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
                                   {u"lead"_qs, sample.lead},
                                   {u"confidence"_qs, sample.confidence},
                                   {u"motionIntensity"_qs, sample.motionIntensity},
                                   {u"accelerationIntent"_qs, sample.accelerationIntent},
                                   {u"onsetAuthority"_qs, sample.onsetAuthority},
                                   {u"sustainedEvidence"_qs, sample.sustainedEvidence},
                                   {u"sustainedAuthority"_qs, sample.sustainedAuthority},
                                   {u"motionUrgency"_qs, sample.motionUrgency},
                                   {u"horizonExtensionEligibility"_qs, sample.horizonExtensionEligibility},
                                   {u"normalMaximumHorizonMs"_qs, sample.normalMaximumHorizonSeconds * 1000.0F},
                                   {u"allowedMaximumHorizonMs"_qs, sample.allowedMaximumHorizonSeconds * 1000.0F},
                                   {u"turningPointConfidence"_qs, sample.turningPointConfidence},
                                   {u"estimatedTimeToTurnMs"_qs, sample.estimatedTimeToTurnSeconds * 1000.0F},
                                   {u"estimatedRemainingTravel"_qs, sample.estimatedRemainingTravel},
                                   {u"turningPointHorizonLimitMs"_qs, sample.turningPointHorizonLimitSeconds * 1000.0F},
                                   {u"turningPointLeadLimit"_qs, sample.turningPointLeadLimit},
                                   {u"reacquisitionAuthority"_qs, sample.reacquisitionAuthority},
                                   {u"state"_qs, adaptiveMotionStateLabel(
                                      static_cast<AdaptiveMotionState>(sample.motionState))}});
    }
    return result;
}

QVariantMap AppBackend::adaptiveResponseHistorySince(qint64 lastSequence, int seconds) const
{
    const int windowSeconds = std::clamp(seconds, 2, 30);
    const int capacity = static_cast<int>(m_adaptiveResponseHistory.size());
    const int selectedAxis = std::clamp(m_configuration.selectedAxisIndex, 0, kPhysicalAxisCount - 1);
    const int first = (m_adaptiveResponseHistoryNext - m_adaptiveResponseHistoryCount + capacity) % capacity;
    const qint64 oldestSequence = m_adaptiveResponseHistoryCount > 0
        ? m_adaptiveResponseHistory[static_cast<size_t>(first)].sequence : 0;
    const qint64 newestMs = m_adaptiveResponseHistoryCount > 0
        ? m_adaptiveResponseHistory[static_cast<size_t>((m_adaptiveResponseHistoryNext + capacity - 1) % capacity)].elapsedMs
        : 0;
    const qint64 minimumMs = newestMs - static_cast<qint64>(windowSeconds) * 1000;
    const bool reset = lastSequence > m_adaptiveResponseHistorySequence
        || (lastSequence > 0 && lastSequence < oldestSequence - 1);
    const qint64 effectiveSequence = reset ? 0 : std::max<qint64>(0, lastSequence);
    QVariantList samples;
    samples.reserve(m_adaptiveResponseHistoryCount);
    for (int offset = 0; offset < m_adaptiveResponseHistoryCount; ++offset) {
        const AdaptiveResponseHistorySample &sample = m_adaptiveResponseHistory[
            static_cast<size_t>((first + offset) % capacity)];
        if (sample.sequence <= effectiveSequence || sample.axis != selectedAxis
            || sample.elapsedMs < minimumMs) continue;
        samples.append(QVariantMap{{u"sequence"_qs, sample.sequence},
            {u"timeMs"_qs, sample.elapsedMs - newestMs}, {u"physical"_qs, sample.physical},
            {u"estimated"_qs, sample.estimated}, {u"predicted"_qs, sample.predicted},
            {u"virtualOutput"_qs, sample.virtualOutput}, {u"velocity"_qs, sample.velocity},
            {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"accelerationIntent"_qs, sample.accelerationIntent},
            {u"onsetAuthority"_qs, sample.onsetAuthority},
            {u"sustainedEvidence"_qs, sample.sustainedEvidence},
            {u"sustainedAuthority"_qs, sample.sustainedAuthority},
            {u"motionUrgency"_qs, sample.motionUrgency},
            {u"horizonExtensionEligibility"_qs, sample.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, sample.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, sample.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, sample.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, sample.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, sample.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, sample.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, sample.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, sample.reacquisitionAuthority},
            {u"state"_qs, adaptiveMotionStateLabel(static_cast<AdaptiveMotionState>(sample.motionState))}});
    }
    return {{u"samples"_qs, samples}, {u"newestSequence"_qs, m_adaptiveResponseHistorySequence},
            {u"reset"_qs, reset}};
}

QVariantMap AppBackend::adaptiveResponseContextState(const QString &scope, const QString &targetId,
                                                     int physicalAxis) const
{
    AdaptiveResponseAxisOverride contextOverride;
    QString source;
    const RuntimeAdaptiveResponseConfig effective = adaptiveResponseConfigurationAtContext(
        scope, targetId, physicalAxis, &contextOverride, &source);
    if (!validAxis(physicalAxis)) return {};
    const int axis = std::clamp(physicalAxis, 0, kPhysicalAxisCount - 1);
    return {{u"axis"_qs, axis},
            {u"axisLabel"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(axis))},
            {u"effective"_qs, adaptiveSettingsMap(effective)},
            // Context editing must never claim that a selected inactive target
            // is the live mapper configuration. This is the resolved state for
            // the target that is being edited, not a worker publication.
            {u"runtimeEffective"_qs, adaptiveSettingsMap(effective)},
            {u"source"_qs, source},
            {u"presetId"_qs, contextOverride.presetId},
            {u"properties"_qs, static_cast<qulonglong>(contextOverride.properties)}};
}

AdaptiveResponseLayer *AppBackend::adaptiveResponseLayer(const QString &scope, const QString &targetId)
{
    const QString normalized = scope.trimmed().toCaseFolded();
    if (normalized == u"global"_qs) return &m_configuration.adaptiveResponseGlobal;
    if (normalized == u"category"_qs) {
        const QString categoryId = targetId.trimmed().isEmpty() ? currentProfile().categoryId
                                                                : targetId.trimmed();
        if (ProfileCategory *category = findProfileCategory(m_configuration, categoryId)) {
            return &category->adaptiveResponse;
        }
    }
    if (normalized == u"profile"_qs) {
        const QString profileId = targetId.trimmed().isEmpty() ? currentProfile().id : targetId.trimmed();
        if (ControllerProfile *profile = findProfile(m_configuration, profileId)) {
            return &profile->adaptiveResponse;
        }
    }
    return nullptr;
}

const AdaptiveResponseLayer *AppBackend::adaptiveResponseLayer(const QString &scope,
                                                               const QString &targetId) const
{
    return const_cast<AppBackend *>(this)->adaptiveResponseLayer(scope, targetId);
}

bool AppBackend::setAdaptiveResponsePreset(const QString &scope, int physicalAxis,
                                           const QString &presetId)
{
    return setAdaptiveResponsePresetAtContext(scope, {}, physicalAxis, presetId);
}

bool AppBackend::setAdaptiveResponsePresetAtContext(const QString &scope, const QString &targetId,
                                                    int physicalAxis, const QString &presetId)
{
    AdaptiveResponseLayer *layer = adaptiveResponseLayer(scope, targetId);
    if (!layer || !validAxis(physicalAxis) || !findAdaptiveResponsePreset(m_configuration, presetId)) return false;
    AdaptiveResponseAxisOverride &override = layer->axes[static_cast<size_t>(physicalAxis)];
    override = {};
    override.presetId = presetId;
    persistAndApply();
    return true;
}

bool AppBackend::setAdaptiveResponseProperty(const QString &scope, int physicalAxis,
                                             const QString &property, const QVariant &value, bool inherit)
{
    return setAdaptiveResponsePropertyAtContext(scope, {}, physicalAxis, property, value, inherit);
}

bool AppBackend::setAdaptiveResponsePropertyAtContext(const QString &scope, const QString &targetId,
                                                      int physicalAxis, const QString &property,
                                                      const QVariant &value, bool inherit)
{
    const std::uint32_t bit = adaptivePropertyForKey(property);
    if (!validAxis(physicalAxis) || bit == 0) return false;
    AdaptiveResponseAxisOverride *entry = nullptr;
    if (scope.trimmed().compare(u"preset"_qs, Qt::CaseInsensitive) == 0) {
        const auto preset = std::find_if(m_configuration.adaptiveResponsePresets.begin(),
            m_configuration.adaptiveResponsePresets.end(), [&targetId](const AdaptiveResponsePreset &item) {
                return item.id == targetId;
            });
        if (preset == m_configuration.adaptiveResponsePresets.end()) return false;
        entry = &preset->axes[static_cast<size_t>(physicalAxis)];
    } else {
        AdaptiveResponseLayer *layer = adaptiveResponseLayer(scope, targetId);
        if (!layer) return false;
        entry = &layer->axes[static_cast<size_t>(physicalAxis)];
    }
    AdaptiveResponseAxisOverride &override = *entry;
    if (inherit) {
        override.properties &= ~bit;
        persistAndApply();
        return true;
    }
    const QString key = property.trimmed().toCaseFolded();
    if (key == u"enabled"_qs) override.settings.enabled = value.toBool();
    else if (key == u"model"_qs) override.settings.model = adaptiveResponseModelFromKey(value.toString());
    else if (key == u"maximumhorizonms"_qs) override.settings.maximumHorizonMs = static_cast<float>(value.toDouble());
    else if (key == u"maximumlead"_qs) override.settings.maximumLead = static_cast<float>(value.toDouble());
    else if (key == u"velocityresponse"_qs) override.settings.velocityResponse = static_cast<float>(value.toDouble());
    else if (key == u"accelerationresponse"_qs) override.settings.accelerationResponse = static_cast<float>(value.toDouble());
    else if (key == u"motionsensitivity"_qs) override.settings.motionSensitivity = static_cast<float>(value.toDouble());
    else if (key == u"noiserejection"_qs) override.settings.noiseRejection = static_cast<float>(value.toDouble());
    else if (key == u"reversaldetection"_qs) override.settings.reversalDetection = static_cast<float>(value.toDouble());
    else if (key == u"reversalresponse"_qs) override.settings.reversalResponse = static_cast<float>(value.toDouble());
    else if (key == u"decelerationresponse"_qs) override.settings.decelerationResponse = static_cast<float>(value.toDouble());
    else if (key == u"settlingresponse"_qs) override.settings.settlingResponse = static_cast<float>(value.toDouble());
    else if (key == u"endpointtaper"_qs) override.settings.endpointTaper = static_cast<float>(value.toDouble());
    else if (key == u"onsetassist"_qs) override.settings.onsetAssist = static_cast<float>(value.toDouble());
    else if (key == u"onsetcap"_qs) override.settings.onsetCap = static_cast<float>(value.toDouble());
    else if (key == u"sustainedassist"_qs) override.settings.sustainedAssist = static_cast<float>(value.toDouble());
    else if (key == u"sustainedcap"_qs) override.settings.sustainedCap = static_cast<float>(value.toDouble());
    else if (key == u"horizonextension"_qs) override.settings.horizonExtension = static_cast<float>(value.toDouble());
    else if (key == u"horizonextensioncap"_qs || key == u"horizonextensioncapms"_qs) override.settings.horizonExtensionCapMs = static_cast<float>(value.toDouble());
    else if (key == u"turningpointprotection"_qs) override.settings.turningPointProtection = static_cast<float>(value.toDouble());
    else if (key == u"turningpointmargin"_qs) override.settings.turningPointMargin = static_cast<float>(value.toDouble());
    else return false;
    override.settings = sanitizedAdaptiveResponseSettings(override.settings);
    override.properties |= bit;
    persistAndApply();
    return true;
}

bool AppBackend::resetAdaptiveResponseAxis(const QString &scope, int physicalAxis)
{
    return resetAdaptiveResponseAxisAtContext(scope, {}, physicalAxis);
}

bool AppBackend::resetAdaptiveResponseAxisAtContext(const QString &scope, const QString &targetId,
                                                    int physicalAxis)
{
    if (!validAxis(physicalAxis)) return false;
    if (scope.trimmed().compare(u"preset"_qs, Qt::CaseInsensitive) == 0) {
        const auto preset = std::find_if(m_configuration.adaptiveResponsePresets.begin(),
            m_configuration.adaptiveResponsePresets.end(), [&targetId](const AdaptiveResponsePreset &item) {
                return item.id == targetId;
            });
        if (preset == m_configuration.adaptiveResponsePresets.end()) return false;
        preset->axes[static_cast<size_t>(physicalAxis)] = {};
    } else {
        AdaptiveResponseLayer *layer = adaptiveResponseLayer(scope, targetId);
        if (!layer) return false;
        layer->axes[static_cast<size_t>(physicalAxis)] = {};
    }
    persistAndApply();
    return true;
}

bool AppBackend::saveAdaptiveResponsePreset(const QString &name, const QString &description)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 64) return false;
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        if (preset.name.compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    }
    if (std::any_of(m_configuration.adaptiveResponsePresets.cbegin(), m_configuration.adaptiveResponsePresets.cend(),
                    [&trimmed](const auto &preset) { return preset.name.compare(trimmed, Qt::CaseInsensitive) == 0; })
        || m_configuration.adaptiveResponsePresets.size() >= 64) return false;
    AdaptiveResponsePreset preset;
    preset.id = u"adaptive-"_qs + QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = trimmed;
    preset.description = description.trimmed().left(160);
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        preset.axes[static_cast<size_t>(axis)].properties = kAdaptiveResponseAllProperties;
        preset.axes[static_cast<size_t>(axis)].settings = settingsFromRuntime(
            resolveAdaptiveResponseConfiguration(m_configuration, currentProfile(), axis));
    }
    m_configuration.adaptiveResponsePresets.push_back(std::move(preset));
    persistAndApply();
    return true;
}

bool AppBackend::duplicateAdaptiveResponsePreset(const QString &presetId, const QString &name)
{
    const AdaptiveResponsePreset *source = findAdaptiveResponsePreset(m_configuration, presetId);
    const QString trimmed = name.trimmed();
    if (!source || trimmed.isEmpty() || trimmed.size() > 64 || m_configuration.adaptiveResponsePresets.size() >= 64) return false;
    for (const AdaptiveResponsePreset &builtIn : builtInAdaptiveResponsePresets()) {
        if (builtIn.name.compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    }
    if (std::any_of(m_configuration.adaptiveResponsePresets.cbegin(), m_configuration.adaptiveResponsePresets.cend(),
                    [&trimmed](const auto &preset) { return preset.name.compare(trimmed, Qt::CaseInsensitive) == 0; })) return false;
    AdaptiveResponsePreset copy = *source;
    copy.id = u"adaptive-"_qs + QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = trimmed;
    copy.builtIn = false;
    m_configuration.adaptiveResponsePresets.push_back(std::move(copy));
    persistAndApply();
    return true;
}

bool AppBackend::renameAdaptiveResponsePreset(const QString &presetId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 64) return false;
    for (const AdaptiveResponsePreset &builtIn : builtInAdaptiveResponsePresets()) {
        if (builtIn.name.compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    }
    for (AdaptiveResponsePreset &preset : m_configuration.adaptiveResponsePresets) {
        if (preset.id != presetId) continue;
        if (std::any_of(m_configuration.adaptiveResponsePresets.cbegin(), m_configuration.adaptiveResponsePresets.cend(),
                        [&presetId, &trimmed](const auto &item) { return item.id != presetId && item.name.compare(trimmed, Qt::CaseInsensitive) == 0; })) return false;
        preset.name = trimmed;
        persistAndApply();
        return true;
    }
    return false;
}

QVariantList AppBackend::adaptiveResponsePresetDependencies(const QString &presetId) const
{
    QVariantList result;
    const QString sought = presetId.trimmed();
    if (sought.isEmpty()) return result;
    const auto appendLayer = [&result, &sought](const AdaptiveResponseLayer &layer,
                                                 const QString &owner) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (layer.axes[static_cast<size_t>(axis)].presetId == sought) {
                result.append(QString(u"%1 / %2"_qs).arg(owner,
                    physicalAxisLabel(static_cast<PhysicalAxis>(axis))));
            }
        }
    };
    appendLayer(m_configuration.adaptiveResponseGlobal, u"Global Defaults"_qs);
    for (const ProfileCategory &category : m_configuration.profileCategories) {
        appendLayer(category.adaptiveResponse, u"Category: "_qs + category.name);
    }
    for (const ControllerProfile &profile : m_configuration.profiles) {
        appendLayer(profile.adaptiveResponse, u"Profile: "_qs + profile.name);
    }
    for (const AutomationDefinition &automation : m_configuration.automations) {
        for (const AutomationActionDefinition &action : automation.actions) {
            if (action.type == AutomationActionType::AdaptiveResponsePreset
                && action.adaptiveResponsePresetId == sought) {
                result.append(u"Automation: "_qs + automation.name);
                break;
            }
        }
    }
    return result;
}

bool AppBackend::deleteAdaptiveResponsePreset(const QString &presetId)
{
    const auto found = std::find_if(m_configuration.adaptiveResponsePresets.begin(),
                                    m_configuration.adaptiveResponsePresets.end(),
                                    [&presetId](const auto &preset) { return preset.id == presetId; });
    if (found == m_configuration.adaptiveResponsePresets.end()) return false;
    // Deletion is intentionally blocked rather than silently clearing a
    // profile/category/Automation reference. The UI can list every owner via
    // adaptiveResponsePresetDependencies() and ask the user to remap it.
    if (!adaptiveResponsePresetDependencies(presetId).isEmpty()) return false;
    m_configuration.adaptiveResponsePresets.erase(found);
    persistAndApply();
    return true;
}

QVariantList AppBackend::adaptiveResponsePreview(const QString &scenario) const
{
    return adaptiveResponsePreviewAtContext(scenario, u"profile"_qs, currentProfile().id,
                                            m_configuration.selectedAxisIndex);
}

QVariantList AppBackend::adaptiveResponsePreviewAtContext(const QString &scenario,
                                                          const QString &scope,
                                                          const QString &targetId,
                                                          int physicalAxis) const
{
    const int axis = std::clamp(physicalAxis, 0, kPhysicalAxisCount - 1);
    RuntimeAxisMapping mapping;
    const RuntimeAdaptiveResponseConfig runtime = adaptiveResponseConfigurationAtContext(
        scope, targetId, axis, nullptr, nullptr, &mapping);
    const std::vector<float> physical = adaptiveResponseScenarioPhysicalSamples(
        scenario, runtime.domainMinimum, runtime.domainMaximum);
    const AdaptiveResponseSimulation simulated = simulateAdaptiveResponse(runtime, physical, 0.004F);
    QVariantList result;
    result.reserve(static_cast<qsizetype>(simulated.size()));
    for (const AdaptiveResponseSimulationSample &sample : simulated) {
        result.append(QVariantMap{{u"time"_qs, sample.timeSeconds}, {u"physical"_qs, sample.telemetry.physical},
            {u"estimated"_qs, sample.telemetry.estimated}, {u"predicted"_qs, sample.telemetry.predicted},
            {u"virtualOutput"_qs, evaluateStaticAxisTransfer(sample.telemetry.predicted, mapping)},
            {u"lead"_qs, sample.telemetry.lead}, {u"horizonMs"_qs, sample.telemetry.activeHorizonSeconds * 1000.0F},
            {u"confidence"_qs, sample.telemetry.confidence},
            {u"velocity"_qs, sample.telemetry.velocity},
            {u"acceleration"_qs, sample.telemetry.acceleration},
            {u"accelerationIntent"_qs, sample.telemetry.accelerationIntent},
            {u"onsetAuthority"_qs, sample.telemetry.onsetAuthority},
            {u"sustainedEvidence"_qs, sample.telemetry.sustainedEvidence},
            {u"sustainedAuthority"_qs, sample.telemetry.sustainedAuthority},
            {u"motionUrgency"_qs, sample.telemetry.motionUrgency},
            {u"horizonExtensionEligibility"_qs, sample.telemetry.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, sample.telemetry.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, sample.telemetry.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, sample.telemetry.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, sample.telemetry.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, sample.telemetry.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, sample.telemetry.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, sample.telemetry.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, sample.telemetry.reacquisitionAuthority},
            {u"velocityAuthority"_qs, sample.telemetry.velocityAuthority},
            {u"motionCoherence"_qs, sample.telemetry.motionCoherence},
            {u"sourceUpdatePeriodMs"_qs, sample.telemetry.sourceUpdatePeriodSeconds * 1000.0F},
            {u"quietDurationMs"_qs, sample.telemetry.quietDurationSeconds * 1000.0F},
            {u"reversal"_qs, sample.telemetry.reversal},
            {u"state"_qs, adaptiveMotionStateLabel(sample.telemetry.state)}});
    }
    return result;
}

QVariantMap AppBackend::adaptiveResponseTestLab(const QString &scenario) const
{
    return adaptiveResponseTestLabAtContext(scenario, u"profile"_qs, currentProfile().id,
                                            m_configuration.selectedAxisIndex);
}

QVariantMap AppBackend::adaptiveResponseTestLabAtContext(const QString &scenario,
                                                         const QString &scope,
                                                         const QString &targetId,
                                                         int physicalAxis) const
{
    // This control-plane simulation intentionally builds no mapper state. It
    // reuses the preview's production estimator samples, then derives compact
    // scenario metrics for inspection before a user maps a live controller.
    const QVariantList samples = adaptiveResponsePreviewAtContext(scenario, scope, targetId,
                                                                    physicalAxis);
    struct TestSample {
        double timeMs = 0.0;
        float physical = 0.0F;
        float predicted = 0.0F;
        float virtualOutput = 0.0F;
        float lead = 0.0F;
        float horizonMs = 0.0F;
        float normalMaximumHorizonMs = 0.0F;
        float allowedMaximumHorizonMs = 0.0F;
        float sustainedAuthority = 0.0F;
        float horizonExtensionEligibility = 0.0F;
        float turningPointConfidence = 0.0F;
        float estimatedTimeToTurnMs = 0.0F;
        float turningPointHorizonLimitMs = 0.0F;
        float turningPointLeadLimit = 0.0F;
        QString state;
    };
    std::vector<TestSample> trace;
    trace.reserve(static_cast<size_t>(samples.size()));
    for (const QVariant &entry : samples) {
        const QVariantMap sample = entry.toMap();
        trace.push_back({sample.value(u"time"_qs).toDouble() * 1000.0,
            static_cast<float>(sample.value(u"physical"_qs).toDouble()),
            static_cast<float>(sample.value(u"predicted"_qs).toDouble()),
            static_cast<float>(sample.value(u"virtualOutput"_qs).toDouble()),
            static_cast<float>(sample.value(u"lead"_qs).toDouble()),
            static_cast<float>(sample.value(u"horizonMs"_qs).toDouble()),
            static_cast<float>(sample.value(u"normalMaximumHorizonMs"_qs).toDouble()),
            static_cast<float>(sample.value(u"allowedMaximumHorizonMs"_qs).toDouble()),
            static_cast<float>(sample.value(u"sustainedAuthority"_qs).toDouble()),
            static_cast<float>(sample.value(u"horizonExtensionEligibility"_qs).toDouble()),
            static_cast<float>(sample.value(u"turningPointConfidence"_qs).toDouble()),
            static_cast<float>(sample.value(u"estimatedTimeToTurnMs"_qs).toDouble()),
            static_cast<float>(sample.value(u"turningPointHorizonLimitMs"_qs).toDouble()),
            static_cast<float>(sample.value(u"turningPointLeadLimit"_qs).toDouble()),
            sample.value(u"state"_qs).toString()});
    }
    const auto directionOf = [](float value, float tolerance = 0.0002F) {
        return value > tolerance ? 1 : value < -tolerance ? -1 : 0;
    };
    const auto physicalAt = [&trace](double timeMs) {
        if (trace.empty() || timeMs <= trace.front().timeMs) return trace.empty() ? 0.0F : trace.front().physical;
        if (timeMs >= trace.back().timeMs) return trace.back().physical;
        const auto right = std::lower_bound(trace.cbegin(), trace.cend(), timeMs,
            [](const TestSample &sample, double value) { return sample.timeMs < value; });
        const auto left = std::prev(right);
        const double span = std::max(0.0001, right->timeMs - left->timeMs);
        const float fraction = static_cast<float>((timeMs - left->timeMs) / span);
        return left->physical + (right->physical - left->physical) * fraction;
    };
    float peakLead = 0.0F;
    float maximumPredictionError = 0.0F;
    float targetOvershoot = 0.0F;
    float stationaryLead = 0.0F;
    float preReversalLead = 0.0F;
    float postReversalLead = 0.0F;
    float maximumPhysicalDelta = 0.0F;
    float maximumPredictedDelta = 0.0F;
    float maximumArtificialPredictorStep = 0.0F;
    float maximumVirtualOutputStep = 0.0F;
    float maximumSustainedAuthority = 0.0F;
    float maximumExtensionEligibility = 0.0F;
    float maximumAllowedHorizonMs = 0.0F;
    float maximumHorizonExtensionMs = 0.0F;
    float maximumTurningPointConfidence = 0.0F;
    float maximumEstimatedTimeToTurnMs = 0.0F;
    float minimumTurningPointHorizonLimitMs = 0.0F;
    float minimumTurningPointLeadLimit = 0.0F;
    int turningPointProtectionActivations = 0;
    std::vector<float> leadMagnitudes;
    std::vector<float> predictionErrors;
    leadMagnitudes.reserve(static_cast<size_t>(samples.size()));
    predictionErrors.reserve(static_cast<size_t>(samples.size()));
    double physicalReversalMs = -1.0;
    double predictorDetectedMs = -1.0;
    double reversalDetectionLatencyMs = -1.0;
    double motionRecognitionDelayMs = -1.0;
    double settledMs = -1.0;
    double staleLeadCancellationMs = -1.0;
    double oppositeDirectionReacquisitionMs = -1.0;
    double finalStableStartMs = -1.0;
    bool havePrevious = false;
    float previousPhysical = 0.0F;
    float previousLead = 0.0F;
    float previousPredicted = 0.0F;
    float previousVirtualOutput = 0.0F;
    int previousDirection = 0;
    int reversalDirection = 0;
    int staleLeadDirection = 0;
    int falseReversalCount = 0;
    int trueReversalCount = 0;
    double motionStartMs = -1.0;
    bool predictorWasReversing = false;
    bool reversalBaselineCaptured = false;
    int trajectoryDirection = 0;
    for (size_t index = 1; index < trace.size(); ++index) {
        const int direction = directionOf(trace[index].physical - trace[index - 1].physical);
        if (direction == 0) continue;
        if (trajectoryDirection != 0 && direction != trajectoryDirection) {
            physicalReversalMs = trace[index].timeMs;
            reversalDirection = direction;
            break;
        }
        trajectoryDirection = direction;
    }
    for (size_t index = 0; index < trace.size(); ++index) {
        const TestSample &sample = trace[index];
        const float physical = sample.physical;
        const float predicted = sample.predicted;
        const float virtualOutput = sample.virtualOutput;
        const float lead = sample.lead;
        const double timeMs = sample.timeMs;
        maximumSustainedAuthority = std::max(maximumSustainedAuthority, sample.sustainedAuthority);
        maximumExtensionEligibility = std::max(maximumExtensionEligibility, sample.horizonExtensionEligibility);
        maximumAllowedHorizonMs = std::max(maximumAllowedHorizonMs, sample.allowedMaximumHorizonMs);
        maximumHorizonExtensionMs = std::max(maximumHorizonExtensionMs,
            std::max(0.0F, sample.allowedMaximumHorizonMs - sample.normalMaximumHorizonMs));
        maximumTurningPointConfidence = std::max(maximumTurningPointConfidence, sample.turningPointConfidence);
        maximumEstimatedTimeToTurnMs = std::max(maximumEstimatedTimeToTurnMs, sample.estimatedTimeToTurnMs);
        if (sample.turningPointConfidence > 0.01F && sample.turningPointHorizonLimitMs > 0.0F) {
            ++turningPointProtectionActivations;
            if (minimumTurningPointHorizonLimitMs <= 0.0F) {
                minimumTurningPointHorizonLimitMs = sample.turningPointHorizonLimitMs;
                minimumTurningPointLeadLimit = sample.turningPointLeadLimit;
            } else {
                minimumTurningPointHorizonLimitMs = std::min(minimumTurningPointHorizonLimitMs,
                    sample.turningPointHorizonLimitMs);
                minimumTurningPointLeadLimit = std::min(minimumTurningPointLeadLimit,
                    sample.turningPointLeadLimit);
            }
        }
        peakLead = std::max(peakLead, std::abs(lead));
        leadMagnitudes.push_back(std::abs(lead));
        const float predictionError = predicted - physicalAt(timeMs + sample.horizonMs);
        predictionErrors.push_back(std::abs(predictionError));
        maximumPredictionError = std::max(maximumPredictionError, std::abs(predictionError));
        const float movement = physical - previousPhysical;
        const int direction = directionOf(movement);
        if (direction != 0 && motionStartMs < 0.0) motionStartMs = timeMs;
        if (motionStartMs >= 0.0 && motionRecognitionDelayMs < 0.0 && std::abs(lead) > 0.0001F) {
            motionRecognitionDelayMs = timeMs - motionStartMs;
        }
        if (havePrevious && physicalReversalMs >= 0.0 && !reversalBaselineCaptured
            && timeMs >= physicalReversalMs) {
            staleLeadDirection = previousLead > 0.0001F ? 1 : previousLead < -0.0001F ? -1 : 0;
            preReversalLead = std::abs(previousLead);
            postReversalLead = std::abs(lead);
            reversalBaselineCaptured = true;
        }
        const bool reversingNow = sample.state == u"Reversing"_qs;
        const bool insideReversalWindow = physicalReversalMs >= 0.0
            && timeMs >= physicalReversalMs - 60.0 && timeMs <= physicalReversalMs + 120.0;
        if (reversingNow && !predictorWasReversing) {
            if (insideReversalWindow) ++trueReversalCount;
            else ++falseReversalCount;
        }
        predictorWasReversing = reversingNow;
        if (physicalReversalMs >= 0.0) {
            if (predictorDetectedMs < 0.0 && reversingNow && timeMs >= physicalReversalMs) {
                predictorDetectedMs = timeMs;
                reversalDetectionLatencyMs = predictorDetectedMs - physicalReversalMs;
            }
            postReversalLead = std::min(postReversalLead, std::abs(lead));
            const int leadDirection = lead > 0.0001F ? 1 : lead < -0.0001F ? -1 : 0;
            if (staleLeadCancellationMs < 0.0
                && (staleLeadDirection == 0 || leadDirection != staleLeadDirection)) {
                staleLeadCancellationMs = timeMs - physicalReversalMs;
            }
            if (oppositeDirectionReacquisitionMs < 0.0 && reversalDirection != 0 && index + 2 < trace.size()
                && leadDirection == reversalDirection) {
                const int nextDirection = directionOf(trace[index + 1].lead, 0.0001F);
                const int laterDirection = directionOf(trace[index + 2].lead, 0.0001F);
                if (nextDirection == reversalDirection && laterDirection == reversalDirection) {
                    oppositeDirectionReacquisitionMs = timeMs - physicalReversalMs;
                }
            }
        }
        if (direction != 0) {
            previousDirection = direction;
            finalStableStartMs = -1.0;
        } else if (finalStableStartMs < 0.0 && previousDirection != 0) {
            finalStableStartMs = timeMs;
        }
        if (finalStableStartMs >= 0.0) stationaryLead = std::max(stationaryLead, std::abs(lead));
        if (havePrevious) {
            const float physicalDelta = physical - previousPhysical;
            const float predictedDelta = predicted - previousPredicted;
            maximumPhysicalDelta = std::max(maximumPhysicalDelta, std::abs(physicalDelta));
            maximumPredictedDelta = std::max(maximumPredictedDelta, std::abs(predictedDelta));
            // The predictor's own additional discontinuity, after subtracting
            // the hand/source movement. This is diagnostic only.
            maximumArtificialPredictorStep = std::max(maximumArtificialPredictorStep,
                std::abs(predictedDelta - physicalDelta));
            maximumVirtualOutputStep = std::max(maximumVirtualOutputStep,
                std::abs(virtualOutput - previousVirtualOutput));
        }
        previousPhysical = physical;
        previousLead = lead;
        previousPredicted = predicted;
        previousVirtualOutput = virtualOutput;
        havePrevious = true;
    }
    // Settling is a persistent state after the final physical target is held,
    // not merely the first moment at which an estimator happens to say Stable.
    constexpr double settlingPersistenceMs = 48.0;
    constexpr float settlingLeadTolerance = 0.002F;
    constexpr float settlingHorizonToleranceMs = 0.25F;
    if (finalStableStartMs >= 0.0) {
        for (size_t start = 0; start < trace.size(); ++start) {
            if (trace[start].timeMs < finalStableStartMs) continue;
            bool persistent = true;
            for (size_t candidate = start; candidate < trace.size()
                 && trace[candidate].timeMs < trace[start].timeMs + settlingPersistenceMs; ++candidate) {
                if (std::abs(trace[candidate].lead) >= settlingLeadTolerance
                    || trace[candidate].horizonMs >= settlingHorizonToleranceMs
                    || std::abs(trace[candidate].predicted - trace[candidate].physical) >= settlingLeadTolerance
                    || trace[candidate].state != u"Stable"_qs) {
                    persistent = false;
                    break;
                }
            }
            if (persistent && trace.back().timeMs >= trace[start].timeMs + settlingPersistenceMs) {
                settledMs = trace[start].timeMs - finalStableStartMs;
                break;
            }
        }
    }
    const bool hasStaticTarget = scenario.trimmed().compare(u"Sudden Stop"_qs, Qt::CaseInsensitive) == 0;
    if (hasStaticTarget && !trace.empty()) {
        const float target = trace.back().physical;
        const int approach = trace.size() > 1 ? directionOf(trace.back().physical - trace.front().physical) : 0;
        for (const TestSample &sample : trace) {
            const float overshoot = approach >= 0 ? sample.predicted - target : target - sample.predicted;
            targetOvershoot = std::max(targetOvershoot, std::max(0.0F, overshoot));
        }
    }
    // Where the authored trace supplies an imminent physical local extremum,
    // measure whether a prediction inside its own active horizon crosses it.
    // This is a test-lab truth metric, not a special-case predictor input.
    float maximumTurningPointOvershoot = 0.0F;
    for (size_t index = 1; index + 1 < trace.size(); ++index) {
        const int direction = directionOf(trace[index].physical - trace[index - 1].physical);
        if (direction == 0 || trace[index].horizonMs <= 0.0F) continue;
        size_t apex = index;
        while (apex + 1 < trace.size()) {
            const int nextDirection = directionOf(trace[apex + 1].physical - trace[apex].physical);
            if (nextDirection != 0 && nextDirection != direction) break;
            ++apex;
        }
        if (apex == index || trace[apex].timeMs - trace[index].timeMs > trace[index].horizonMs + 0.001) continue;
        const float overshoot = direction > 0 ? trace[index].predicted - trace[apex].physical
                                               : trace[apex].physical - trace[index].predicted;
        maximumTurningPointOvershoot = std::max(maximumTurningPointOvershoot, std::max(0.0F, overshoot));
    }
    const float medianLead = leadMagnitudes.empty() ? 0.0F : [&leadMagnitudes]() {
        const size_t middle = leadMagnitudes.size() / 2;
        std::nth_element(leadMagnitudes.begin(), leadMagnitudes.begin() + middle, leadMagnitudes.end());
        return leadMagnitudes[middle];
    }();
    const double meanAbsolutePredictionError = predictionErrors.empty() ? 0.0
        : std::accumulate(predictionErrors.cbegin(), predictionErrors.cend(), 0.0)
            / static_cast<double>(predictionErrors.size());
    const double rmsPredictionError = predictionErrors.empty() ? 0.0 : std::sqrt(
        std::accumulate(predictionErrors.cbegin(), predictionErrors.cend(), 0.0,
            [](double total, float error) { return total + static_cast<double>(error) * error; })
        / static_cast<double>(predictionErrors.size()));
    float p95PredictionError = 0.0F;
    if (!predictionErrors.empty()) {
        std::sort(predictionErrors.begin(), predictionErrors.end());
        const size_t p95Index = std::min(predictionErrors.size() - 1,
            static_cast<size_t>(std::ceil(static_cast<double>(predictionErrors.size()) * 0.95)) - 1);
        p95PredictionError = predictionErrors[p95Index];
    }
    return {{u"sampleCount"_qs, samples.size()}, {u"peakLead"_qs, peakLead},
            {u"medianLead"_qs, medianLead}, {u"maximumPredictionError"_qs, maximumPredictionError},
            {u"meanAbsolutePredictionError"_qs, meanAbsolutePredictionError},
            {u"rmsPredictionError"_qs, rmsPredictionError}, {u"p95PredictionError"_qs, p95PredictionError},
            {u"hasStaticTarget"_qs, hasStaticTarget}, {u"targetOvershoot"_qs, targetOvershoot},
            {u"stationaryLead"_qs, stationaryLead}, {u"falseReversalCount"_qs, falseReversalCount},
            {u"trueReversalCount"_qs, trueReversalCount}, {u"physicalReversalMs"_qs, physicalReversalMs},
            {u"predictorDetectedMs"_qs, predictorDetectedMs},
            {u"reversalDetectionLatencyMs"_qs, reversalDetectionLatencyMs},
            {u"motionRecognitionDelayMs"_qs, motionRecognitionDelayMs},
            {u"settlingTimeMs"_qs, settledMs}, {u"settlingPersistenceMs"_qs, settlingPersistenceMs},
            {u"staleLeadCancellationMs"_qs, staleLeadCancellationMs},
            {u"oppositeDirectionReacquisitionMs"_qs, oppositeDirectionReacquisitionMs},
            {u"preReversalLead"_qs, preReversalLead},
            {u"postReversalLead"_qs, postReversalLead},
            {u"leadCollapseMagnitude"_qs, std::max(0.0F, preReversalLead - postReversalLead)},
            {u"maximumPhysicalDelta"_qs, maximumPhysicalDelta},
            {u"maximumPredictedDelta"_qs, maximumPredictedDelta},
            {u"maximumArtificialPredictorStep"_qs, maximumArtificialPredictorStep},
            {u"maximumVirtualOutputStep"_qs, maximumVirtualOutputStep},
            {u"maximumSustainedAuthority"_qs, maximumSustainedAuthority},
            {u"maximumExtensionEligibility"_qs, maximumExtensionEligibility},
            {u"maximumAllowedHorizonMs"_qs, maximumAllowedHorizonMs},
            {u"maximumHorizonExtensionMs"_qs, maximumHorizonExtensionMs},
            {u"maximumTurningPointConfidence"_qs, maximumTurningPointConfidence},
            {u"maximumEstimatedTimeToTurnMs"_qs, maximumEstimatedTimeToTurnMs},
            {u"minimumTurningPointHorizonLimitMs"_qs, minimumTurningPointHorizonLimitMs},
            {u"minimumTurningPointLeadLimit"_qs, minimumTurningPointLeadLimit},
            {u"turningPointProtectionActivations"_qs, turningPointProtectionActivations},
            {u"maximumTurningPointOvershoot"_qs, maximumTurningPointOvershoot}};
}

void AppBackend::adaptiveResponseSimulatorStepAtContext(double physical, const QString &scope,
                                                         const QString &targetId, int physicalAxis,
                                                         int sourceRateHz)
{
    advanceAdaptiveResponseSimulator(static_cast<float>(physical), scope, targetId, physicalAxis,
                                     sourceRateHz, m_adaptiveResponseSimulatorClock.elapsed());
}

void AppBackend::advanceAdaptiveResponseSimulator(float manualInput, const QString &scope,
                                                  const QString &targetId, int physicalAxis,
                                                  int sourceRateHz, qint64 nowMs)
{
    // This intentionally does not touch MappingWorker. It reconstructs the
    // physical gesture between QML pointer events, then samples that gesture
    // at the selected device rate and holds reports through a fixed 250 Hz
    // estimator cadence. QML's paint cadence is never a physical input rate.
    const int axis = std::clamp(physicalAxis, 0, kPhysicalAxisCount - 1);
    const RuntimeAdaptiveResponseConfig configuration =
        adaptiveResponseConfigurationAtContext(scope, targetId, axis);
    const int supportedRate = sourceRateHz <= 45 ? 30 : sourceRateHz <= 90 ? 60
        : sourceRateHz <= 180 ? 125 : 250;
    // Keep fractional report periods (especially 60 Hz) rather than rounding
    // them to a 16 ms / 62.5 Hz source. Estimator samples remain on their
    // separate fixed 250 Hz cadence below.
    const double sourcePeriodMs = 1000.0 / static_cast<double>(supportedRate);
    constexpr qint64 processingPeriodMs = 4;
    const float sourceValue = std::clamp(manualInput, configuration.domainMinimum,
                                         configuration.domainMaximum);
    if (!m_adaptiveResponseSimulatorHasManualInput) {
        m_adaptiveResponseSimulatorHasManualInput = true;
        m_adaptiveResponseSimulatorLastManualInput = sourceValue;
        m_adaptiveResponseSimulatorHeldInput = sourceValue;
        m_adaptiveResponseSimulatorLastManualInputMs = nowMs;
        m_adaptiveResponseSimulatorLastTickMs = nowMs - processingPeriodMs;
        m_adaptiveResponseSimulatorLastSourceMs = static_cast<double>(nowMs) - sourcePeriodMs;
        m_adaptiveResponseSimulatorSourceRate = supportedRate;
    } else if (m_adaptiveResponseSimulatorSourceRate != supportedRate) {
        // Preserve estimator state while restarting only the synthetic device
        // clock when the operator changes source-rate emulation.
        m_adaptiveResponseSimulatorSourceRate = supportedRate;
        m_adaptiveResponseSimulatorLastSourceMs = static_cast<double>(nowMs) - sourcePeriodMs;
    }

    const qint64 gestureStartMs = m_adaptiveResponseSimulatorLastManualInputMs;
    const qint64 gestureDurationMs = std::max<qint64>(1, nowMs - gestureStartMs);
    const float gestureStartValue = m_adaptiveResponseSimulatorLastManualInput;
    const auto reconstructedGestureAt = [=](double sampleMs) {
        const float fraction = std::clamp(static_cast<float>(sampleMs - static_cast<double>(gestureStartMs))
            / static_cast<float>(gestureDurationMs), 0.0F, 1.0F);
        return gestureStartValue + (sourceValue - gestureStartValue) * fraction;
    };
    const auto runtimeProfiles = m_worker.runtimeProfileCache();
    const int profile = std::clamp(m_worker.runtime().effectiveProfileIndex.load(), 0,
        static_cast<int>(runtimeProfiles->profiles.size()) - 1);
    const RuntimeAxisMapping &mapping = runtimeProfiles->profiles[static_cast<size_t>(profile)]
        .axes[static_cast<size_t>(axis)];

    // Generate original 4 ms estimator timestamps. Device reports update the
    // held physical value only at their emulated rate; a 60 Hz device therefore
    // has reconstructed 60 Hz reports held over intervening estimator ticks.
    int generated = 0;
    while (m_adaptiveResponseSimulatorLastTickMs + processingPeriodMs <= nowMs && generated < 256) {
        m_adaptiveResponseSimulatorLastTickMs += processingPeriodMs;
        while (m_adaptiveResponseSimulatorLastSourceMs + sourcePeriodMs
               <= static_cast<double>(m_adaptiveResponseSimulatorLastTickMs)) {
            m_adaptiveResponseSimulatorLastSourceMs += sourcePeriodMs;
            m_adaptiveResponseSimulatorHeldInput = reconstructedGestureAt(
                m_adaptiveResponseSimulatorLastSourceMs);
        }
        const auto timestamp = std::chrono::steady_clock::time_point{}
            + std::chrono::milliseconds(m_adaptiveResponseSimulatorLastTickMs);
        const AdaptiveResponseTelemetry telemetry = m_adaptiveResponseSimulator.process(
            m_adaptiveResponseSimulatorHeldInput, configuration, timestamp);
        AdaptiveResponseSimulatorSample sample;
        sample.elapsedMs = m_adaptiveResponseSimulatorLastTickMs;
        sample.physical = telemetry.physical;
        sample.estimated = telemetry.estimated;
        sample.predicted = telemetry.predicted;
        sample.virtualOutput = evaluateStaticAxisTransfer(telemetry.predicted, mapping);
        sample.velocity = telemetry.velocity;
        sample.acceleration = telemetry.acceleration;
        sample.activeHorizonSeconds = telemetry.activeHorizonSeconds;
        sample.maximumHorizonSeconds = configuration.maximumHorizonSeconds;
        sample.maximumLead = configuration.maximumLead;
        sample.lead = telemetry.lead;
        sample.confidence = telemetry.confidence;
        sample.motionIntensity = telemetry.motionIntensity;
        sample.velocityAuthority = telemetry.velocityAuthority;
        sample.accelerationIntent = telemetry.accelerationIntent;
        sample.onsetAuthority = telemetry.onsetAuthority;
        sample.sustainedEvidence = telemetry.sustainedEvidence;
        sample.sustainedAuthority = telemetry.sustainedAuthority;
        sample.motionUrgency = telemetry.motionUrgency;
        sample.horizonExtensionEligibility = telemetry.horizonExtensionEligibility;
        sample.normalMaximumHorizonSeconds = telemetry.normalMaximumHorizonSeconds;
        sample.allowedMaximumHorizonSeconds = telemetry.allowedMaximumHorizonSeconds;
        sample.turningPointConfidence = telemetry.turningPointConfidence;
        sample.estimatedTimeToTurnSeconds = telemetry.estimatedTimeToTurnSeconds;
        sample.estimatedRemainingTravel = telemetry.estimatedRemainingTravel;
        sample.turningPointHorizonLimitSeconds = telemetry.turningPointHorizonLimitSeconds;
        sample.turningPointLeadLimit = telemetry.turningPointLeadLimit;
        sample.reacquisitionAuthority = telemetry.reacquisitionAuthority;
        sample.motionState = static_cast<int>(telemetry.state);
        appendAdaptiveResponseSimulatorSample(sample);
        ++generated;
    }
    m_adaptiveResponseSimulatorLastManualInput = sourceValue;
    m_adaptiveResponseSimulatorLastManualInputMs = nowMs;
}

void AppBackend::appendAdaptiveResponseSimulatorSample(const AdaptiveResponseSimulatorSample &sample)
{
    AdaptiveResponseSimulatorSample stored = sample;
    stored.sequence = ++m_adaptiveResponseSimulatorSequence;
    m_adaptiveResponseSimulatorHistory[static_cast<size_t>(m_adaptiveResponseSimulatorHistoryNext)] = stored;
    m_adaptiveResponseSimulatorHistoryNext = (m_adaptiveResponseSimulatorHistoryNext + 1)
        % static_cast<int>(m_adaptiveResponseSimulatorHistory.size());
    m_adaptiveResponseSimulatorHistoryCount = std::min(m_adaptiveResponseSimulatorHistoryCount + 1,
        static_cast<int>(m_adaptiveResponseSimulatorHistory.size()));
    if (!m_adaptiveResponseSimulatorRecordingActive) return;
    m_adaptiveResponseSimulatorRecording[static_cast<size_t>(m_adaptiveResponseSimulatorRecordingNext)] = stored;
    m_adaptiveResponseSimulatorRecordingNext = (m_adaptiveResponseSimulatorRecordingNext + 1)
        % static_cast<int>(m_adaptiveResponseSimulatorRecording.size());
    m_adaptiveResponseSimulatorRecordingCount = std::min(m_adaptiveResponseSimulatorRecordingCount + 1,
        static_cast<int>(m_adaptiveResponseSimulatorRecording.size()));
}

QVariantList AppBackend::adaptiveResponseSimulatorHistory() const
{
    QVariantList result;
    result.reserve(m_adaptiveResponseSimulatorHistoryCount);
    if (m_adaptiveResponseSimulatorHistoryCount == 0) return result;
    const int capacity = static_cast<int>(m_adaptiveResponseSimulatorHistory.size());
    const int first = (m_adaptiveResponseSimulatorHistoryNext - m_adaptiveResponseSimulatorHistoryCount
        + capacity) % capacity;
    const qint64 newest = m_adaptiveResponseSimulatorHistory[
        static_cast<size_t>((m_adaptiveResponseSimulatorHistoryNext + capacity - 1) % capacity)].elapsedMs;
    for (int offset = 0; offset < m_adaptiveResponseSimulatorHistoryCount; ++offset) {
        const AdaptiveResponseSimulatorSample &sample = m_adaptiveResponseSimulatorHistory[
            static_cast<size_t>((first + offset) % capacity)];
        result.append(QVariantMap{{u"sequence"_qs, sample.sequence},
            {u"timeMs"_qs, sample.elapsedMs - newest},
            {u"physical"_qs, sample.physical}, {u"estimated"_qs, sample.estimated},
            {u"predicted"_qs, sample.predicted}, {u"virtualOutput"_qs, sample.virtualOutput},
            {u"velocity"_qs, sample.velocity}, {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"accelerationIntent"_qs, sample.accelerationIntent},
            {u"onsetAuthority"_qs, sample.onsetAuthority},
            {u"sustainedEvidence"_qs, sample.sustainedEvidence},
            {u"sustainedAuthority"_qs, sample.sustainedAuthority},
            {u"motionUrgency"_qs, sample.motionUrgency},
            {u"horizonExtensionEligibility"_qs, sample.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, sample.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, sample.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, sample.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, sample.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, sample.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, sample.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, sample.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, sample.reacquisitionAuthority},
            {u"state"_qs, adaptiveMotionStateLabel(static_cast<AdaptiveMotionState>(sample.motionState))}});
    }
    return result;
}

QVariantMap AppBackend::adaptiveResponseSimulatorHistorySince(qint64 lastSequence) const
{
    const int capacity = static_cast<int>(m_adaptiveResponseSimulatorHistory.size());
    const int first = (m_adaptiveResponseSimulatorHistoryNext - m_adaptiveResponseSimulatorHistoryCount
        + capacity) % capacity;
    const qint64 oldestSequence = m_adaptiveResponseSimulatorHistoryCount > 0
        ? m_adaptiveResponseSimulatorHistory[static_cast<size_t>(first)].sequence : 0;
    const bool reset = lastSequence > m_adaptiveResponseSimulatorSequence
        || (lastSequence > 0 && lastSequence < oldestSequence - 1);
    const qint64 effectiveSequence = reset ? 0 : std::max<qint64>(0, lastSequence);
    const qint64 newest = m_adaptiveResponseSimulatorHistoryCount > 0
        ? m_adaptiveResponseSimulatorHistory[static_cast<size_t>((m_adaptiveResponseSimulatorHistoryNext
            + capacity - 1) % capacity)].elapsedMs : 0;
    QVariantList samples;
    samples.reserve(m_adaptiveResponseSimulatorHistoryCount);
    for (int offset = 0; offset < m_adaptiveResponseSimulatorHistoryCount; ++offset) {
        const AdaptiveResponseSimulatorSample &sample = m_adaptiveResponseSimulatorHistory[
            static_cast<size_t>((first + offset) % capacity)];
        if (sample.sequence <= effectiveSequence) continue;
        samples.append(QVariantMap{{u"sequence"_qs, sample.sequence},
            {u"timeMs"_qs, sample.elapsedMs - newest}, {u"physical"_qs, sample.physical},
            {u"estimated"_qs, sample.estimated}, {u"predicted"_qs, sample.predicted},
            {u"virtualOutput"_qs, sample.virtualOutput}, {u"velocity"_qs, sample.velocity},
            {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"accelerationIntent"_qs, sample.accelerationIntent},
            {u"onsetAuthority"_qs, sample.onsetAuthority},
            {u"sustainedEvidence"_qs, sample.sustainedEvidence},
            {u"sustainedAuthority"_qs, sample.sustainedAuthority},
            {u"motionUrgency"_qs, sample.motionUrgency},
            {u"horizonExtensionEligibility"_qs, sample.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, sample.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, sample.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, sample.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, sample.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, sample.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, sample.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, sample.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, sample.reacquisitionAuthority},
            {u"state"_qs, adaptiveMotionStateLabel(static_cast<AdaptiveMotionState>(sample.motionState))}});
    }
    return {{u"samples"_qs, samples}, {u"newestSequence"_qs, m_adaptiveResponseSimulatorSequence},
            {u"reset"_qs, reset}};
}

void AppBackend::adaptiveResponseSimulatorClear()
{
    m_adaptiveResponseSimulator.reset();
    m_adaptiveResponseSimulatorHistoryNext = 0;
    m_adaptiveResponseSimulatorHistoryCount = 0;
    m_adaptiveResponseSimulatorRecordingNext = 0;
    m_adaptiveResponseSimulatorRecordingCount = 0;
    m_adaptiveResponseSimulatorLastSourceMs = -1.0;
    m_adaptiveResponseSimulatorLastTickMs = -1;
    m_adaptiveResponseSimulatorLastManualInputMs = -1;
    m_adaptiveResponseSimulatorSequence = 0;
    m_adaptiveResponseSimulatorSourceRate = 250;
    m_adaptiveResponseSimulatorLastManualInput = 0.0F;
    m_adaptiveResponseSimulatorHeldInput = 0.0F;
    m_adaptiveResponseSimulatorHasManualInput = false;
    m_adaptiveResponseSimulatorRecordingActive = false;
    m_adaptiveResponseSimulatorClock.restart();
}

void AppBackend::adaptiveResponseSimulatorStartRecording()
{
    m_adaptiveResponseSimulatorRecordingNext = 0;
    m_adaptiveResponseSimulatorRecordingCount = 0;
    m_adaptiveResponseSimulatorRecordingActive = true;
}

void AppBackend::adaptiveResponseSimulatorStopRecording()
{
    m_adaptiveResponseSimulatorRecordingActive = false;
}

bool AppBackend::adaptiveResponseSimulatorRecordingActive() const
{
    return m_adaptiveResponseSimulatorRecordingActive;
}

QVariantList AppBackend::adaptiveResponseSimulatorRecording() const
{
    QVariantList result;
    result.reserve(m_adaptiveResponseSimulatorRecordingCount);
    if (m_adaptiveResponseSimulatorRecordingCount == 0) return result;
    const int capacity = static_cast<int>(m_adaptiveResponseSimulatorRecording.size());
    const int first = (m_adaptiveResponseSimulatorRecordingNext - m_adaptiveResponseSimulatorRecordingCount
        + capacity) % capacity;
    const qint64 firstTime = m_adaptiveResponseSimulatorRecording[static_cast<size_t>(first)].elapsedMs;
    for (int offset = 0; offset < m_adaptiveResponseSimulatorRecordingCount; ++offset) {
        const AdaptiveResponseSimulatorSample &sample = m_adaptiveResponseSimulatorRecording[
            static_cast<size_t>((first + offset) % capacity)];
        result.append(QVariantMap{{u"sequence"_qs, sample.sequence},
            {u"recordedElapsedMs"_qs, sample.elapsedMs - firstTime},
            {u"physical"_qs, sample.physical}, {u"estimated"_qs, sample.estimated},
            {u"predicted"_qs, sample.predicted}, {u"virtualOutput"_qs, sample.virtualOutput},
            {u"velocity"_qs, sample.velocity}, {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"accelerationIntent"_qs, sample.accelerationIntent},
            {u"onsetAuthority"_qs, sample.onsetAuthority},
            {u"sustainedEvidence"_qs, sample.sustainedEvidence},
            {u"sustainedAuthority"_qs, sample.sustainedAuthority},
            {u"motionUrgency"_qs, sample.motionUrgency},
            {u"horizonExtensionEligibility"_qs, sample.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, sample.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, sample.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, sample.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, sample.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, sample.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, sample.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, sample.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, sample.reacquisitionAuthority},
            {u"state"_qs, adaptiveMotionStateLabel(static_cast<AdaptiveMotionState>(sample.motionState))}});
    }
    return result;
}

QVariantList AppBackend::buttons() const
{
    if (m_uiPerformanceInstrumentationEnabled) ++m_buttonGetterCalls;
    return m_buttonUiModel;
}

void AppBackend::rebuildButtonUiModel()
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const int capacity = vjoyButtonCount();
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (!runtime.buttonAvailable[source].load()) continue;
        const ButtonBindings &bindings = currentProfile().buttons;
        const ButtonBinding binding = source < static_cast<int>(bindings.size())
            ? bindings[static_cast<size_t>(source)] : ButtonBinding{};
        const int target = binding.type == ButtonActionType::VirtualButton
            && isButtonBindingValid(binding, capacity) ? binding.target : 0;
        const ProfileTriggerBinding trigger = source < static_cast<int>(m_configuration.profileTriggers.size())
            ? m_configuration.profileTriggers[static_cast<size_t>(source)] : ProfileTriggerBinding{};
        const bool profileControlEnabled = profileTriggerBindingEnabled(trigger);
        const ControllerProfile *triggerTarget = profileControlEnabled
            ? findProfile(m_configuration, trigger.targetProfileId) : nullptr;
        const ProfileTriggerMode activeMode = static_cast<ProfileTriggerMode>(
            runtime.profileOverrideMode.load());
        QVariantMap item;
        item.insert(u"index"_qs, source + 1);
        const QString hardwareLabel = QString(u"Button %1"_qs).arg(source + 1);
        const QString customLabel = binding.customName.trimmed();
        const MappingControlAction mappingControl = source < static_cast<int>(m_configuration.mappingControls.size())
            ? m_configuration.mappingControls[static_cast<size_t>(source)] : MappingControlAction::None;
        item.insert(u"label"_qs, customLabel.isEmpty() ? hardwareLabel : customLabel);
        item.insert(u"hardwareLabel"_qs, hardwareLabel);
        item.insert(u"customName"_qs, customLabel);
        item.insert(u"pressed"_qs, runtime.physicalButtonPressed[source].load());
        item.insert(u"target"_qs, target);
        item.insert(u"targetLabel"_qs, target > 0
            ? QString(u"vJoy Button %1"_qs).arg(target) : u"Disabled"_qs);
        item.insert(u"virtualPressed"_qs, target > 0
            && runtime.virtualButtonPressed[target - 1].load());
        item.insert(u"profileControlEnabled"_qs, profileControlEnabled);
        item.insert(u"profileControlTargetId"_qs, trigger.targetProfileId);
        item.insert(u"profileControlTargetName"_qs, triggerTarget
            ? triggerTarget->name : (profileControlEnabled ? u"Target profile unavailable"_qs : QString{}));
        item.insert(u"profileControlTargetAvailable"_qs, triggerTarget != nullptr);
        item.insert(u"profileControlMode"_qs, profileTriggerModeLabel(trigger.mode));
        item.insert(u"profileControlActive"_qs, runtime.profileOverrideButton.load() == source + 1
            && activeMode == trigger.mode && profileControlEnabled);
        item.insert(u"mappingControl"_qs, mappingControlActionLabel(mappingControl));
        item.insert(u"mappingControlKey"_qs, mappingControlActionKey(mappingControl));
        result.append(item);
    }
    if (m_buttonUiModel == result) return;
    m_buttonUiModel = std::move(result);
    if (m_uiPerformanceInstrumentationEnabled) ++m_buttonUiModelRebuilds;
    emit buttonTelemetryChanged();
}

bool AppBackend::refreshButtonUiModelRuntimeState()
{
    bool changed = false;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const ProfileTriggerMode activeMode = static_cast<ProfileTriggerMode>(runtime.profileOverrideMode.load());
    for (qsizetype index = 0; index < m_buttonUiModel.size(); ++index) {
        QVariantMap item = m_buttonUiModel[index].toMap();
        const int source = item.value(u"index"_qs).toInt() - 1;
        if (source < 0 || source >= kMaximumPhysicalButtons) continue;
        const int target = item.value(u"target"_qs).toInt();
        const bool pressed = runtime.physicalButtonPressed[static_cast<size_t>(source)].load();
        const bool virtualPressed = target > 0
            && runtime.virtualButtonPressed[static_cast<size_t>(target - 1)].load();
        const ProfileTriggerBinding trigger = source < static_cast<int>(m_configuration.profileTriggers.size())
            ? m_configuration.profileTriggers[static_cast<size_t>(source)] : ProfileTriggerBinding{};
        const bool profileControlActive = profileTriggerBindingEnabled(trigger)
            && runtime.profileOverrideButton.load() == source + 1
            && activeMode == trigger.mode;
        if (item.value(u"pressed"_qs).toBool() == pressed
            && item.value(u"virtualPressed"_qs).toBool() == virtualPressed
            && item.value(u"profileControlActive"_qs).toBool() == profileControlActive) {
            continue;
        }
        item.insert(u"pressed"_qs, pressed);
        item.insert(u"virtualPressed"_qs, virtualPressed);
        item.insert(u"profileControlActive"_qs, profileControlActive);
        m_buttonUiModel[index] = std::move(item);
        changed = true;
    }
    return changed;
}

QVariantList AppBackend::povs() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const int count = std::clamp(povCount(), 0, kMaximumPhysicalPovs);
    for (int hat = 0; hat < count; ++hat) {
        const int raw = runtime.povValues[static_cast<size_t>(hat)].load();
        const PovDirection direction = povDirectionFromRaw(raw);
        QVariantMap item;
        item.insert(u"index"_qs, hat + 1);
        item.insert(u"raw"_qs, raw);
        item.insert(u"centered"_qs, direction == PovDirection::Centered);
        item.insert(u"direction"_qs, povDirectionLabel(direction));
        item.insert(u"angle"_qs, direction == PovDirection::Centered ? -1 : raw / 100);
        const NativePovBinding binding = hat < static_cast<int>(m_configuration.nativePovBindings.size())
            ? m_configuration.nativePovBindings[static_cast<size_t>(hat)] : NativePovBinding{};
        const bool targetAvailable = binding.targetType == NativePovTargetType::Continuous
            ? binding.targetIndex <= vjoyContinuousPovCount()
            : binding.targetType == NativePovTargetType::Discrete
                && binding.targetIndex <= vjoyDiscretePovCount();
        const QString targetKind = binding.targetType == NativePovTargetType::Continuous
            ? u"Continuous"_qs : binding.targetType == NativePovTargetType::Discrete
                ? u"Discrete"_qs : QString{};
        item.insert(u"nativeEnabled"_qs, binding.enabled);
        item.insert(u"nativeTargetKey"_qs, binding.targetType == NativePovTargetType::Continuous
            ? QString(u"continuous:%1"_qs).arg(binding.targetIndex)
            : binding.targetType == NativePovTargetType::Discrete
                ? QString(u"discrete:%1"_qs).arg(binding.targetIndex) : QString{});
        item.insert(u"nativeTargetLabel"_qs, binding.enabled
            ? QString(u"vJoy %1 POV %2"_qs).arg(targetKind).arg(binding.targetIndex)
            : u"Off"_qs);
        item.insert(u"nativeAvailable"_qs, targetAvailable);
        item.insert(u"nativeStatus"_qs, !binding.enabled ? u"OFF"_qs
            : targetAvailable ? u"READY"_qs : u"UNAVAILABLE"_qs);
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::povInputs() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const PovBindings &bindings = currentProfile().povs;
    const int capacity = vjoyButtonCount();
    const int hats = std::clamp(povCount(), 0, kMaximumPhysicalPovs);
    for (int hat = 0; hat < hats; ++hat) {
        const PovDirection active = povDirectionFromRaw(
            runtime.povValues[static_cast<size_t>(hat)].load());
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding binding = hat < static_cast<int>(bindings.size())
                ? bindings[static_cast<size_t>(hat)][static_cast<size_t>(direction)] : ButtonBinding{};
            const int target = binding.type == ButtonActionType::VirtualButton
                && isButtonBindingValid(binding, capacity) ? binding.target : 0;
            const PovDirection logicalDirection = static_cast<PovDirection>(direction + 1);
            const ProfileTriggerBinding trigger = hat < static_cast<int>(m_configuration.povProfileTriggers.size())
                ? m_configuration.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)]
                : ProfileTriggerBinding{};
            const bool profileControlEnabled = profileTriggerBindingEnabled(trigger);
            const ControllerProfile *triggerTarget = profileControlEnabled
                ? findProfile(m_configuration, trigger.targetProfileId) : nullptr;
            const ProfileTriggerMode activeMode = static_cast<ProfileTriggerMode>(
                runtime.profileOverrideMode.load());
            QVariantMap item;
            item.insert(u"hat"_qs, hat + 1);
            item.insert(u"direction"_qs, direction);
            item.insert(u"label"_qs, povDirectionLabel(logicalDirection));
            item.insert(u"active"_qs, active == logicalDirection);
            item.insert(u"target"_qs, target);
            item.insert(u"targetLabel"_qs, target > 0
                ? QString(u"vJoy Button %1"_qs).arg(target) : u"Disabled"_qs);
            item.insert(u"virtualPressed"_qs, target > 0
                && runtime.virtualButtonPressed[static_cast<size_t>(target - 1)].load());
            item.insert(u"profileControlEnabled"_qs, profileControlEnabled);
            item.insert(u"profileControlTargetId"_qs, trigger.targetProfileId);
            item.insert(u"profileControlTargetName"_qs, triggerTarget
                ? triggerTarget->name : (profileControlEnabled ? u"Target profile unavailable"_qs : QString{}));
            item.insert(u"profileControlTargetAvailable"_qs, triggerTarget != nullptr);
            item.insert(u"profileControlMode"_qs, profileTriggerModeLabel(trigger.mode));
            item.insert(u"profileControlActive"_qs, runtime.profileOverridePovHat.load() == hat + 1
                && runtime.profileOverridePovDirection.load() == direction
                && activeMode == trigger.mode && profileControlEnabled);
            result.append(item);
        }
    }
    return result;
}

QVariantList AppBackend::profiles() const
{
    if (m_uiPerformanceInstrumentationEnabled) ++m_profileGetterCalls;
    QVariantList result;
    for (const ControllerProfile &profile : m_configuration.profiles) {
        int mappedAxes = 0;
        for (const AxisMapping &axis : profile.axes) {
            if (axis.target != VirtualAxis::Disabled) ++mappedAxes;
        }
        int mappedButtons = 0;
        for (const ButtonBinding &binding : profile.buttons) {
            if (binding.type == ButtonActionType::VirtualButton) ++mappedButtons;
        }
        int mappedPovs = 0;
        for (const auto &hat : profile.povs) {
            for (const ButtonBinding &binding : hat) {
                if (binding.type == ButtonActionType::VirtualButton) ++mappedPovs;
            }
        }
        int customCurves = 0;
        for (const AxisMapping &axis : profile.axes) {
            if (axis.curve.family != CurveFamily::Linear) ++customCurves;
        }
        int automationCount = 0;
        for (const AutomationDefinition &automation : m_configuration.automations) {
            bool associated = false;
            for (const AutomationConditionDefinition &condition : automation.conditions) {
                if (condition.profileId == profile.id) { associated = true; break; }
            }
            if (!associated) for (const AutomationActionDefinition &action : automation.actions) {
                if (action.profileId == profile.id) { associated = true; break; }
            }
            if (associated) ++automationCount;
        }
        QVariantMap item;
        item.insert(u"id"_qs, profile.id);
        item.insert(u"name"_qs, profile.name);
        const ProfileCategory *category = findProfileCategory(m_configuration, profile.categoryId);
        item.insert(u"categoryId"_qs, profile.categoryId);
        item.insert(u"categoryName"_qs, category ? category->name : u"General"_qs);
        item.insert(u"displayName"_qs, category ? QString(u"%1 / %2"_qs).arg(category->name, profile.name) : profile.name);
        item.insert(u"active"_qs, profile.id == m_configuration.activeProfileId);
        item.insert(u"enabled"_qs, profile.enabled);
        item.insert(u"effective"_qs, profile.id == effectiveProfileId());
        item.insert(u"effectiveSource"_qs, profile.id == effectiveProfileId()
            ? profileSourceLabel() : QString{});
        item.insert(u"protected"_qs, profile.id == normalProfileId());
        item.insert(u"mappedAxes"_qs, mappedAxes);
        item.insert(u"mappedButtons"_qs, mappedButtons);
        item.insert(u"mappedPovs"_qs, mappedPovs);
        item.insert(u"customCurves"_qs, customCurves);
        item.insert(u"automationCount"_qs, automationCount);
        const VirtualOutputLayout *layout = findOutputLayout(m_configuration, profile.outputLayoutId);
        item.insert(u"outputLayoutId"_qs, profile.outputLayoutId);
        item.insert(u"outputLayoutName"_qs, layout ? layout->name : u"Output unavailable"_qs);
        item.insert(u"outputDeviceId"_qs, layout ? layout->requirements.deviceId : 0);
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::profileCategories() const
{
    if (m_uiPerformanceInstrumentationEnabled) ++m_categoryGetterCalls;
    QVariantList result;
    for (const ProfileCategory &category : m_configuration.profileCategories) {
        QVariantMap item;
        item.insert(u"id"_qs, category.id);
        item.insert(u"name"_qs, category.name);
        item.insert(u"icon"_qs, category.icon);
        item.insert(u"profileCount"_qs, static_cast<int>(category.profileIds.size()));
        item.insert(u"defaultProfileId"_qs, category.defaultProfileId);
        item.insert(u"lastActiveProfileId"_qs, category.lastActiveProfileId);
        item.insert(u"defaultProfileName"_qs, categoryProfileLabel(m_configuration, category.defaultProfileId));
        item.insert(u"lastActiveProfileName"_qs, categoryProfileLabel(m_configuration, category.lastActiveProfileId));
        item.insert(u"active"_qs, category.id == activeCategoryId());
        item.insert(u"enabled"_qs, category.enabled);
        item.insert(u"restoreLastProfile"_qs, category.restoreLastProfile);
        item.insert(u"executableRules"_qs, category.executableRules);
        result.append(item);
    }
    return result;
}

QString AppBackend::activeProfileId() const { return m_configuration.activeProfileId; }
QString AppBackend::activeProfileName() const { return currentProfile().name; }
QString AppBackend::profileDisplayName(const QString &profileId) const
{
    return categoryProfileLabel(m_configuration, profileId);
}
QString AppBackend::activeProfileDisplayName() const { return profileDisplayName(activeProfileId()); }
QString AppBackend::activeCategoryId() const { return currentProfile().categoryId; }
QString AppBackend::activeCategoryName() const
{
    if (const ProfileCategory *category = findProfileCategory(m_configuration, activeCategoryId())) return category->name;
    return u"General"_qs;
}
QString AppBackend::effectiveProfileName() const
{
    const int index = m_worker.runtime().effectiveProfileIndex.load();
    if (index >= 0 && index < static_cast<int>(m_configuration.profiles.size())) {
        return m_configuration.profiles[static_cast<size_t>(index)].name;
    }
    return currentProfile().name;
}
QString AppBackend::effectiveProfileDisplayName() const { return profileDisplayName(effectiveProfileId()); }

QVariantMap AppBackend::portableImportPreview() const
{
    return m_portableImportPreview;
}

QString AppBackend::effectiveProfileId() const
{
    const int index = m_worker.runtime().effectiveProfileIndex.load();
    if (index >= 0 && index < static_cast<int>(m_configuration.profiles.size())) {
        return m_configuration.profiles[static_cast<size_t>(index)].id;
    }
    return m_configuration.activeProfileId;
}

QString AppBackend::profileSourceLabel() const
{
    const int button = m_worker.runtime().profileOverrideButton.load();
    const int povHat = m_worker.runtime().profileOverridePovHat.load();
    const int povDirection = m_worker.runtime().profileOverridePovDirection.load();
    const ProfileTriggerMode mode = static_cast<ProfileTriggerMode>(
        m_worker.runtime().profileOverrideMode.load());
    const int automationRule = m_worker.runtime().profileOverrideAutomationRule.load();
    if (mode == ProfileTriggerMode::Disabled) return u"Manual base profile"_qs;
    if (button > 0) {
        return QString(u"Button %1 · %2"_qs).arg(button).arg(profileTriggerModeLabel(mode));
    }
    if (povHat > 0 && povDirection >= 0 && povDirection < kPovDirectionCount) {
        return QString(u"POV %1 %2 · %3"_qs).arg(povHat)
            .arg(povDirectionLabel(static_cast<PovDirection>(povDirection + 1)))
            .arg(profileTriggerModeLabel(mode));
    }
    if (automationRule >= 0 && automationRule < static_cast<int>(m_configuration.automations.size())) {
        const QString name = m_configuration.automations[static_cast<size_t>(automationRule)].name;
        return QString(u"Automation %1 · %2"_qs).arg(name)
            .arg(profileTriggerModeLabel(mode));
    }
    return u"Manual base profile"_qs;
}
int AppBackend::activeProfileIndex() const
{
    for (int index = 0; index < static_cast<int>(m_configuration.profiles.size()); ++index) {
        if (m_configuration.profiles[static_cast<size_t>(index)].id == m_configuration.activeProfileId) {
            return index;
        }
    }
    return 0;
}

QString AppBackend::deviceName() const { return m_worker.deviceSnapshot().name; }
QString AppBackend::deviceId() const { return m_worker.deviceSnapshot().id; }
QVariantList AppBackend::controllers() const
{
    if (m_uiPerformanceInstrumentationEnabled) ++m_controllerGetterCalls;
    return m_controllerUiModel;
}

bool AppBackend::rebuildControllerUiModel()
{
    QVariantList result;
    QSet<QString> represented;
    const QString liveId = deviceId();
    int connectedCount = 0;
    for (const DiscoveredController &controller : m_discoveredControllers) {
        if (controller.virtualDevice) continue;
        const ControllerMatch match = ControllerManager::match(controller, m_configuration.savedControllers);
        const bool verified = !match.recordId.isEmpty() && !match.ambiguous;
        const bool selected = verified && match.recordId == m_configuration.activeControllerRecordId;
        const bool active = controller.connected && controller.directInputId == liveId;
        QVariantMap item{{u"id"_qs, match.recordId}, {u"directInputId"_qs, controller.directInputId},
                         {u"name"_qs, controller.name}, {u"connected"_qs, controller.connected},
                         {u"verified"_qs, verified}, {u"selected"_qs, selected},
                         {u"ambiguous"_qs, match.ambiguous}, {u"active"_qs, active},
                         {u"axisCount"_qs, controller.axisCount}, {u"buttonCount"_qs, controller.buttonCount},
                         {u"povCount"_qs, controller.povCount}};
        item.insert(u"state"_qs, match.ambiguous ? u"Connected · Selection required"_qs
            : match.recordId.isEmpty() ? u"Connected · New device"_qs
            : active ? u"Connected · Verified · Active"_qs
            : selected ? u"Connected · Verified · Selected"_qs
                       : u"Connected · Verified"_qs);
        represented.insert(match.recordId);
        result.append(item);
        if (controller.connected) ++connectedCount;
    }
    for (const SavedControllerRecord &record : m_configuration.savedControllers) {
        if (represented.contains(record.id)) continue;
        result.append(QVariantMap{{u"id"_qs, record.id}, {u"directInputId"_qs, record.lastDirectInputId},
            {u"name"_qs, record.displayName}, {u"connected"_qs, false}, {u"verified"_qs, true},
            {u"selected"_qs, record.id == m_configuration.activeControllerRecordId},
            {u"ambiguous"_qs, false}, {u"active"_qs, false},
            {u"axisCount"_qs, record.axisCount}, {u"buttonCount"_qs, record.buttonCount},
            {u"povCount"_qs, record.povCount},
            {u"state"_qs, record.id == m_configuration.activeControllerRecordId
                ? u"Selected · Offline · Verified"_qs : u"Offline · Verified"_qs}});
    }
    m_controllerUiModelLiveDeviceId = liveId;
    if (m_controllerUiModel == result) return false;
    m_controllerUiModel = std::move(result);
    m_connectedControllerCount = connectedCount;
    if (m_uiPerformanceInstrumentationEnabled) ++m_controllerUiModelRebuilds;
    emit controllersChanged();
    return true;
}

void AppBackend::rebuildCurveAxisChoices()
{
    QVariantList choices;
    const ControllerProfile &profile = currentProfile();
    choices.reserve(kPhysicalAxisCount);
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const PhysicalAxis axis = static_cast<PhysicalAxis>(index);
        const QString customName = profile.axes[static_cast<size_t>(index)].customName.trimmed();
        choices.append(QVariantMap{{u"index"_qs, index},
            {u"label"_qs, customName.isEmpty() ? physicalAxisLabel(axis) : customName}});
    }
    m_curveAxisChoices = std::move(choices);
}

QString AppBackend::activeControllerRecordId() const { return m_configuration.activeControllerRecordId; }
bool AppBackend::autoSwitchVerifiedController() const { return m_configuration.autoSwitchVerifiedController; }
bool AppBackend::keepRunningInTray() const { return m_configuration.keepRunningInTray; }
bool AppBackend::trayAvailable() const { return m_trayIcon && m_trayIcon->isVisible(); }
QString AppBackend::presentationState() const
{
    switch (m_presentationLifecycle) {
    case PresentationLifecycleState::Visible: return u"Visible"_qs;
    case PresentationLifecycleState::Minimized: return u"Minimized"_qs;
    case PresentationLifecycleState::TrayHidden: return u"TrayHidden"_qs;
    }
    return u"Visible"_qs;
}

int AppBackend::presentationSnapshotIntervalMs() const
{
    return m_snapshotTimer.isActive() ? m_snapshotTimer.interval() : 0;
}

int AppBackend::controllerDiscoveryIntervalMs() const
{
    return m_controllerDiscoveryTimer.interval();
}

bool AppBackend::presentationSnapshotActive() const { return m_snapshotTimer.isActive(); }
bool AppBackend::gameDetectionTimerActive() const { return m_gameDetectionTimer.isActive(); }
bool AppBackend::physicalConnected() const { return m_worker.runtime().physicalConnected.load(); }
int AppBackend::axisCount() const { return m_worker.runtime().axisCount.load(); }

QString AppBackend::physicalAxisCapabilitySummary() const
{
    const AtomicRuntimeState &runtime = m_worker.runtime();
    int advertised = 0;
    int active = 0;
    bool classified = false;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!runtime.axisAvailable[static_cast<size_t>(index)].load()) continue;
        ++advertised;
        const PhysicalAxisActivity activity = static_cast<PhysicalAxisActivity>(
            runtime.axisActivity[static_cast<size_t>(index)].load());
        classified = classified || activity != PhysicalAxisActivity::Unknown;
        active += activity == PhysicalAxisActivity::Active ? 1 : 0;
    }
    if (!classified) return advertised > 0
        ? QString(u"%1 ADVERTISED · CALIBRATE TO VERIFY ACTIVITY"_qs).arg(advertised)
        : u"WAITING"_qs;
    return QString(u"%1 ACTIVE · %2 ADVERTISED"_qs).arg(active).arg(advertised);
}
int AppBackend::buttonCount() const { return m_worker.runtime().buttonCount.load(); }
int AppBackend::povCount() const { return m_worker.runtime().povCount.load(); }
int AppBackend::povValue() const { return m_worker.runtime().povValues[0].load(); }
int AppBackend::vjoyButtonCount() const { return m_worker.runtime().vjoyButtonCount.load(); }
int AppBackend::vjoyContinuousPovCount() const
{
    return m_worker.runtime().vjoyContinuousPovCount.load();
}
int AppBackend::vjoyDiscretePovCount() const
{
    return m_worker.runtime().vjoyDiscretePovCount.load();
}
int AppBackend::vjoyRequiredButtonCount() const
{
    return currentVjoyRequirements().buttons;
}
bool AppBackend::vjoyCapacitySufficient() const
{
    return vjoyButtonCount() >= vjoyRequiredButtonCount();
}
int AppBackend::lastPhysicalButton() const { return m_worker.runtime().lastPhysicalButton.load(); }
int AppBackend::lastPhysicalButtonTarget() const { return m_worker.runtime().lastPhysicalButtonTarget.load(); }
bool AppBackend::mappingActive() const { return m_worker.runtime().mappingActive.load(); }
bool AppBackend::mappingRequested() const { return m_mappingDesired; }
QString AppBackend::mappingStatus() const
{
    const bool active = m_worker.runtime().mappingActive.load();
    const MappingEffectiveState effective =
        static_cast<MappingEffectiveState>(m_worker.runtime().mappingEffectiveState.load());
    if (!m_mappingDesired) return active ? u"STOPPING MAPPING"_qs : u"MAPPING OFF"_qs;
    if (active) return u"MAPPING ACTIVE"_qs;
    if (effective == MappingEffectiveState::Suspended) {
        return u"MAPPING SUSPENDED"_qs;
    }
    return u"STARTING MAPPING"_qs;
}
bool AppBackend::vjoyReady() const { return m_worker.runtime().vjoyReady.load(); }
QString AppBackend::vjoyStatus() const { return m_worker.vjoyStatus(); }
QString AppBackend::vjoyStatusSeverity() const
{
    if (!vjoyReady()) return u"error"_qs;
    return vjoyCapacitySufficient() ? u"ready"_qs : u"warning"_qs;
}
bool AppBackend::hidhideAvailable() const { return m_readiness.plan().hidhide.installed; }
bool AppBackend::hidhideCloakStateKnown() const
{
    return m_readiness.plan().hidhide.cloakKnown;
}
bool AppBackend::hidhideCloaked() const { return m_readiness.plan().hidhide.cloaked; }
bool AppBackend::hidhideMapperAllowed() const
{
    return m_readiness.plan().hidhide.mapperAllowlisted;
}

QVariantList AppBackend::controllerReadinessChecks() const
{
    QVariantList checks;
    const ControllerReadinessPlan &plan = m_readiness.plan();
    const auto append = [&checks](const QString &name, VerificationSubsystemState state,
                                  const QString &summary) {
        const QString severity = state == VerificationSubsystemState::Ready ? u"ready"_qs
            : state == VerificationSubsystemState::Error ? u"error"_qs
            : state == VerificationSubsystemState::Attention ? u"warning"_qs : u"info"_qs;
        checks.append(QVariantMap{{u"name"_qs, name},
                                  {u"state"_qs, ControllerReadinessService::subsystemStateLabel(state)},
                                  {u"message"_qs, summary},
                                  {u"severity"_qs, severity}});
    };
    append(u"PHYSICAL CONTROLLER"_qs, plan.physicalStatus, plan.physicalSummary);
    append(u"VJOY OUTPUT"_qs, plan.vjoyStatus, plan.vjoySummary);
    append(u"HIDHIDE ISOLATION"_qs, plan.hidhideStatus, plan.hidhideSummary);
    return checks;
}

QVariantList AppBackend::controllerReadinessProposedChanges() const
{
    QVariantList changes;
    for (const QString &change : m_readiness.plan().proposedChanges) {
        changes.append(QVariantMap{{u"message"_qs, change}});
    }
    return changes;
}

QVariantList AppBackend::controllerRepairOperationResults() const
{
    QVariantList operations;
    for (const AutomaticRepairOperationResult &operation : m_readiness.lastAutomaticRepairResult().operations) {
        QVariantMap item;
        item.insert(u"name"_qs, operation.operationName);
        item.insert(u"state"_qs, operation.rollback ? u"ROLLED BACK"_qs
            : operation.succeeded ? u"COMPLETED"_qs : u"FAILED"_qs);
        item.insert(u"severity"_qs, operation.rollback ? u"warning"_qs
            : operation.succeeded ? u"ready"_qs : u"error"_qs);
        QString message = operation.message.trimmed();
        if (message.isEmpty()) message = operation.succeeded ? u"Completed."_qs : u"The operation did not complete."_qs;
        if (!operation.succeeded && operation.exitCode >= 0) {
            message += QString(u" Exit code %1."_qs).arg(operation.exitCode);
        }
        item.insert(u"message"_qs, message);
        operations.append(std::move(item));
    }
    return operations;
}

QString AppBackend::controllerReadinessState() const
{
    return ControllerReadinessService::stateLabel(m_readiness.plan().state);
}

QString AppBackend::controllerReadinessStatus() const
{
    const QString status = m_readiness.plan().status;
    return status.isEmpty() ? QStringLiteral("Verify your controller setup from Settings.") : status;
}
QString AppBackend::controllerReadinessLastChecked() const
{
    const QDateTime checked = m_readiness.plan().lastChecked;
    return checked.isValid() ? checked.toString(QLocale().timeFormat(QLocale::ShortFormat))
                             : QStringLiteral("Not yet verified");
}
QString AppBackend::controllerReadinessRecommendedAction() const
{
    const ControllerReadinessPlan &plan = m_readiness.plan();
    if (plan.isChecking) return {};
    if (plan.canApplyAutomatically) return QStringLiteral("FIX AUTOMATICALLY");
    if (plan.state == ControllerReadinessState::Attention
        && plan.verificationMode == VerificationMode::Quick) {
        return QStringLiteral("RUN FULL VERIFICATION");
    }
    if (plan.state == ControllerReadinessState::NeedsChanges) return QStringLiteral("SHOW INSTRUCTIONS");
    return {};
}
bool AppBackend::controllerSetupCanApply() const
{
    return m_readiness.plan().canApplyAutomatically && !m_verificationInProgress;
}
bool AppBackend::controllerSetupInProgress() const
{
    return m_verificationInProgress || m_controllerSelectionInProgress || m_readiness.transactionActive();
}
bool AppBackend::controllerSetupCanUndo() const { return m_readiness.canUndo(); }
bool AppBackend::controllerDiagnosticsAvailable() const
{
    return isControllerDiagnosticsAvailable(m_readiness.plan().state);
}
bool AppBackend::calibrationActive() const { return m_calibrationStage != CalibrationStageState::Idle; }
QString AppBackend::calibrationStage() const
{
    switch (m_calibrationStage) {
    case CalibrationStageState::Idle: return QStringLiteral("IDLE");
    case CalibrationStageState::Range: return QStringLiteral("RANGE");
    case CalibrationStageState::Center: return QStringLiteral("CENTER");
    case CalibrationStageState::Finalizing: return QStringLiteral("FINALIZING");
    }
    return QStringLiteral("IDLE");
}
bool AppBackend::startMappingOnLaunch() const { return m_configuration.startMappingOnLaunch; }
int AppBackend::vjoyDeviceId() const { return m_configuration.vjoyDeviceId; }

const VirtualOutputLayout *AppBackend::activeOutputLayout() const
{
    return findOutputLayout(m_configuration, currentProfile().outputLayoutId);
}

VirtualOutputLayout *AppBackend::activeOutputLayout()
{
    return findOutputLayout(m_configuration, currentProfile().outputLayoutId);
}

void AppBackend::synchronizeActiveOutputLayout()
{
    if (const VirtualOutputLayout *layout = activeOutputLayout()) {
        m_configuration.vjoyDeviceId = layout->requirements.deviceId;
    }
}

QString AppBackend::activeOutputLayoutName() const
{
    const VirtualOutputLayout *layout = activeOutputLayout();
    return layout ? layout->name : u"Output unavailable"_qs;
}

QString AppBackend::activeOutputLayoutDescriptor() const
{
    const VirtualOutputLayout *layout = activeOutputLayout();
    if (!layout) return {};
    QStringList axes;
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (layout->requirements.axes[static_cast<size_t>(index)]) {
            axes.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
        }
    }
    return axes.join(u" · "_qs);
}

QVariantList AppBackend::virtualOutputLayouts() const
{
    QVariantList result;
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        QStringList axes;
        for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
            if (layout.requirements.axes[static_cast<size_t>(index)]) {
                axes.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
            }
        }
        int profileCount = 0;
        for (const ControllerProfile &profile : m_configuration.profiles) {
            profileCount += profile.outputLayoutId == layout.id ? 1 : 0;
        }
        result.append(QVariantMap{{u"id"_qs, layout.id}, {u"name"_qs, layout.name},
            {u"deviceId"_qs, layout.requirements.deviceId}, {u"axes"_qs, axes.join(u" · "_qs)},
            {u"profileCount"_qs, profileCount}, {u"active"_qs, currentProfile().outputLayoutId == layout.id},
            {u"managedVisibility"_qs, layout.hidhideManaged},
            {u"visibilityPrepared"_qs, !layout.hidHideDeviceInstanceId.isEmpty()}});
    }
    return result;
}

double AppBackend::disabledAxisValue() const
{
    return static_cast<double>(sanitizedDisabledAxisValue(m_configuration.disabledAxisValue)) * 100.0;
}

bool AppBackend::curveTransitionSmoothingEnabled() const
{
    return sanitizedCurveTransitionSmoothing(m_configuration.curveTransitionSmoothing).enabled;
}

int AppBackend::curveTransitionDurationMs() const
{
    return sanitizedCurveTransitionSmoothing(m_configuration.curveTransitionSmoothing).durationMs;
}
qulonglong AppBackend::latencyCurrentUs() const { return m_worker.runtime().latencyCurrentUs.load(); }
qulonglong AppBackend::latencyAverageUs() const { return m_worker.runtime().latencyAverageUs.load(); }
qulonglong AppBackend::latencyPeakUs() const { return m_worker.runtime().latencyPeakUs.load(); }
qulonglong AppBackend::profileSwitchCount() const { return m_worker.runtime().profileSwitchCount.load(); }
qulonglong AppBackend::lastProfileSwapUs() const { return m_worker.runtime().lastProfileSwapUs.load(); }
qulonglong AppBackend::lastCurveCompileUs() const { return m_worker.runtime().lastCurveCompileUs.load(); }
bool AppBackend::automationEngineEnabled() const { return m_configuration.automationEnabled; }
int AppBackend::automationRuleCount() const { return static_cast<int>(m_configuration.automations.size()); }
int AppBackend::automationActiveRuleCount() const { return m_worker.runtime().automationActiveRuleCount.load(); }
qulonglong AppBackend::automationEvaluationUs() const { return m_worker.runtime().automationEvaluationUs.load(); }
QString AppBackend::automationValidationMessage() const { return m_automationValidationMessage; }

QVariantList AppBackend::automationRules() const
{
    QVariantList rules;
    const std::shared_ptr<const RuntimeProfileCache> compiled = m_worker.runtimeProfileCache();
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < static_cast<int>(m_configuration.automations.size()); ++index) {
        const AutomationDefinition &definition = m_configuration.automations[static_cast<size_t>(index)];
        QVariantList conditions;
        QStringList conditionLabels;
        for (const AutomationConditionDefinition &condition : definition.conditions) {
            conditionLabels.append(automationConditionSummary(condition, m_configuration));
            conditions.append(QVariantMap{{u"type"_qs, static_cast<int>(condition.type)},
                {u"axis"_qs, condition.axis}, {u"minimum"_qs, condition.minimum},
                {u"maximum"_qs, condition.maximum}, {u"hysteresis"_qs, condition.hysteresis},
                {u"button"_qs, condition.button}, {u"povHat"_qs, condition.povHat},
                {u"povDirection"_qs, static_cast<int>(condition.povDirection)},
                {u"profileId"_qs, condition.profileId}, {u"pressCount"_qs, condition.pressCount},
                {u"multiPressWindowMs"_qs, condition.multiPressWindowMs},
                {u"longPressDurationMs"_qs, condition.longPressDurationMs}});
        }
        QVariantList actions;
        QStringList actionLabels;
        for (const AutomationActionDefinition &action : definition.actions) {
            actionLabels.append(automationActionSummary(action, m_configuration));
            actions.append(QVariantMap{{u"type"_qs, static_cast<int>(action.type)},
                {u"virtualButton"_qs, action.virtualButton}, {u"profileId"_qs, action.profileId},
                {u"adaptiveResponsePresetId"_qs, action.adaptiveResponsePresetId},
                {u"targetAxis"_qs, action.targetAxis}, {u"sourceAxis"_qs, action.sourceAxis},
                {u"sourceStage"_qs, static_cast<int>(action.sourceStage)}, {u"value"_qs, action.value},
                {u"offset"_qs, action.offset}, {u"minimum"_qs, action.minimum}, {u"maximum"_qs, action.maximum},
                {u"tapDurationMs"_qs, action.tapDurationMs}});
        }
        AutomationHealth health = AutomationHealth::Valid;
        QString healthMessage;
        if (compiled && compiled->automation && index < compiled->automation->ruleCount) {
            health = compiled->automation->ruleHealth[static_cast<size_t>(index)];
            healthMessage = compiled->automation->ruleMessages[static_cast<size_t>(index)];
        }
        // Availability is inherently dynamic, so the compiler cannot decide
        // it. Surface a connected device's missing route as repairable
        // attention in the Automation UI without publishing unsafe output.
        QString availabilityMessage;
        if (runtime.physicalConnected.load()) {
            for (const AutomationConditionDefinition &condition : definition.conditions) {
                const bool axisCondition = condition.type == AutomationConditionType::AxisAbove
                    || condition.type == AutomationConditionType::AxisBelow
                    || condition.type == AutomationConditionType::AxisBetween
                    || condition.type == AutomationConditionType::AxisOutsideRange
                    || condition.type == AutomationConditionType::AxisCrossesAbove
                    || condition.type == AutomationConditionType::AxisCrossesBelow;
                if (axisCondition && (condition.axis < 0 || condition.axis >= kPhysicalAxisCount
                    || !runtime.axisAvailable[static_cast<size_t>(condition.axis)].load())) {
                    availabilityMessage = u"Required physical axis is unavailable on the connected controller."_qs;
                    break;
                }
                const bool buttonCondition = condition.type == AutomationConditionType::ButtonHeld
                    || condition.type == AutomationConditionType::ButtonReleased
                    || condition.type == AutomationConditionType::ButtonPressed
                    || condition.type == AutomationConditionType::ButtonReleaseEvent
                    || condition.type == AutomationConditionType::ButtonMultiPress
                    || condition.type == AutomationConditionType::ButtonLongPress;
                if (buttonCondition && (condition.button < 1 || condition.button > kMaximumPhysicalButtons
                    || !runtime.buttonAvailable[static_cast<size_t>(condition.button - 1)].load())) {
                    availabilityMessage = u"Required physical button is unavailable on the connected controller."_qs;
                    break;
                }
                const bool povCondition = condition.type == AutomationConditionType::PovActive
                    || condition.type == AutomationConditionType::PovInactive;
                if (povCondition && (condition.povHat < 1 || condition.povHat > runtime.povCount.load())) {
                    availabilityMessage = u"Required POV hat is unavailable on the connected controller."_qs;
                    break;
                }
            }
            if (availabilityMessage.isEmpty()) {
                for (const AutomationActionDefinition &action : definition.actions) {
                    const bool axisTarget = action.type == AutomationActionType::AxisScale
                        || action.type == AutomationActionType::AxisOffset
                        || action.type == AutomationActionType::AxisClamp
                        || action.type == AutomationActionType::AxisOverride
                        || action.type == AutomationActionType::AxisMix
                        || action.type == AutomationActionType::AxisFollow;
                    const bool adaptiveAxisTarget = action.type == AutomationActionType::AdaptiveResponseEnable
                        || action.type == AutomationActionType::AdaptiveResponseDisable
                        || action.type == AutomationActionType::AdaptiveResponsePreset;
                    const bool usesAxisSource = action.type == AutomationActionType::AxisMix
                        || action.type == AutomationActionType::AxisFollow;
                    if (((axisTarget || adaptiveAxisTarget)
                         && (action.targetAxis < 0 || action.targetAxis >= kPhysicalAxisCount
                         || !runtime.axisAvailable[static_cast<size_t>(action.targetAxis)].load()))
                        || (usesAxisSource && (action.sourceAxis < 0 || action.sourceAxis >= kPhysicalAxisCount
                            || !runtime.axisAvailable[static_cast<size_t>(action.sourceAxis)].load()))) {
                        availabilityMessage = u"An Automation axis action references an unavailable controller axis."_qs;
                        break;
                    }
                    if ((action.type == AutomationActionType::VJoyButtonHold
                         || action.type == AutomationActionType::VJoyButtonToggle
                         || action.type == AutomationActionType::VJoyButtonTap)
                        && runtime.vjoyButtonCount.load() > 0
                        && action.virtualButton > runtime.vjoyButtonCount.load()) {
                        availabilityMessage = u"Automation targets a vJoy button not exposed by the active device."_qs;
                        break;
                    }
                }
            }
        }
        if (!availabilityMessage.isEmpty()) {
            health = AutomationHealth::Invalid;
            healthMessage = availabilityMessage;
        }
        const auto always = [](const AutomationConditionDefinition &condition) {
            return condition.type == AutomationConditionType::Always;
        };
        const bool containsAlways = std::any_of(definition.conditions.cbegin(), definition.conditions.cend(), always);
        if (containsAlways) {
            // A single Always condition is a friendly rule-level mode. It is
            // redundant under ALL with other requirements and dominates under
            // ANY, so never force people to decode it on overview cards.
            if (definition.matchMode == AutomationMatchMode::Any || definition.conditions.size() == 1) {
                conditionLabels = {u"All the time"_qs};
            } else {
                QStringList filtered;
                for (int labelIndex = 0; labelIndex < static_cast<int>(definition.conditions.size()); ++labelIndex) {
                    if (!always(definition.conditions[static_cast<size_t>(labelIndex)])) {
                        filtered.append(conditionLabels[labelIndex]);
                    }
                }
                conditionLabels = std::move(filtered);
            }
        }
        const QString connector = definition.matchMode == AutomationMatchMode::Any ? u" OR "_qs : u" AND "_qs;
        rules.append(QVariantMap{{u"id"_qs, definition.id}, {u"name"_qs, definition.name},
            {u"enabled"_qs, definition.enabled}, {u"matchMode"_qs, static_cast<int>(definition.matchMode)},
            {u"activationMode"_qs, static_cast<int>(definition.activationMode)},
            {u"activeDurationMs"_qs, definition.activeDurationMs},
            {u"behaviorLabel"_qs, automationBehaviorLabel(definition.activationMode)},
            {u"priority"_qs, definition.priority}, {u"conditions"_qs, conditions}, {u"actions"_qs, actions},
            {u"conditionSummary"_qs, conditionLabels.join(connector)},
            {u"actionSummary"_qs, actionLabels.join(u" · "_qs)},
            {u"active"_qs, m_worker.runtime().automationRuleActive[static_cast<size_t>(index)].load()},
            {u"health"_qs, static_cast<int>(health)}, {u"healthMessage"_qs, healthMessage}});
    }
    return rules;
}

QStringList AppBackend::buttonOutputChoices() const
{
    QStringList choices{u"Disabled"_qs};
    for (int button = 1; button <= vjoyButtonCount(); ++button) {
        choices.append(QString(u"vJoy Button %1"_qs).arg(button));
    }
    return choices;
}

QStringList AppBackend::virtualAxisChoices() const
{
    QStringList choices{u"Disabled"_qs};
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const VirtualAxis axis = static_cast<VirtualAxis>(index);
        const bool available = runtime.virtualAxisAvailable[static_cast<size_t>(index)].load();
        bool configured = false;
        for (const AxisMapping &mapping : currentProfile().axes) {
            configured = configured || mapping.target == axis;
        }
        if (available || configured) choices.append(virtualAxisLabel(axis));
    }
    return choices;
}

QString AppBackend::virtualAxisStatus() const
{
    QStringList available;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (runtime.virtualAxisAvailable[static_cast<size_t>(index)].load()) {
            available.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
        }
    }
    return available.isEmpty() ? u"No vJoy axis reported"_qs
        : u"Axes: "_qs + available.join(u" / "_qs);
}

QVariantList AppBackend::quickAssignAxisTargets() const
{
    QVariantList targets;
    const VirtualOutputLayout *layout = activeOutputLayout();
    if (!layout) return targets;
    const ControllerProfile &profile = currentProfile();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (!layout->requirements.axes[static_cast<size_t>(index)]) continue;
        const VirtualAxis axis = static_cast<VirtualAxis>(index);
        const QString target = virtualAxisLabel(axis);
        const QString alias = profile.virtualAxisAliases[static_cast<size_t>(index)].trimmed();
        targets.append(QVariantMap{{u"target"_qs, target},
            {u"label"_qs, alias.isEmpty() ? target : alias},
            {u"technicalLabel"_qs, QString(u"vJoy %1"_qs).arg(target)}});
    }
    return targets;
}

QVariantList AppBackend::quickMapButtonTargets() const
{
    QSet<int> destinations;
    const int physicalCount = std::min(buttonCount(), kMaximumPhysicalButtons);
    const ControllerProfile &profile = currentProfile();
    for (int source = 0; source < std::min(physicalCount, static_cast<int>(profile.buttons.size())); ++source) {
        const ButtonBinding &binding = profile.buttons[static_cast<size_t>(source)];
        if (binding.type == ButtonActionType::VirtualButton && binding.target > 0
            && binding.target <= vjoyButtonCount()) {
            destinations.insert(binding.target);
        }
    }
    // A fresh profile has no explicit bindings yet; its meaningful intent is
    // the existing one-to-one default floor, not every available vJoy button.
    if (destinations.isEmpty()) {
        for (int button = 1; button <= std::min(physicalCount, vjoyButtonCount()); ++button) {
            destinations.insert(button);
        }
    }
    QList<int> ordered = destinations.values();
    std::sort(ordered.begin(), ordered.end());
    QVariantList targets;
    for (const int destination : ordered) {
        targets.append(QVariantMap{{u"virtualButton"_qs, destination},
            {u"label"_qs, QString(u"vJoy Button %1"_qs).arg(destination)}});
    }
    return targets;
}

QVariantMap AppBackend::inputLearning() const
{
    const auto kindName = [this] {
        switch (m_inputLearning.kind) {
        case InputLearningKind::Axis: return u"axis"_qs;
        case InputLearningKind::Button: return u"button"_qs;
        case InputLearningKind::Pov: return u"pov"_qs;
        case InputLearningKind::None: return u"none"_qs;
        }
        return u"none"_qs;
    };
    const auto phaseName = [this] {
        switch (m_inputLearning.phase) {
        case InputLearningPhase::Arming: return u"arming"_qs;
        case InputLearningPhase::Waiting: return u"waiting"_qs;
        case InputLearningPhase::Ambiguous: return u"ambiguous"_qs;
        case InputLearningPhase::Conflict: return u"conflict"_qs;
        case InputLearningPhase::Assigned: return u"assigned"_qs;
        case InputLearningPhase::Idle: return u"idle"_qs;
        }
        return u"idle"_qs;
    };
    return {{u"active"_qs, m_inputLearning.kind != InputLearningKind::None},
        {u"kind"_qs, kindName()}, {u"phase"_qs, phaseName()},
        {u"target"_qs, m_inputLearning.target},
        {u"targetLabel"_qs, m_inputLearning.kind == InputLearningKind::Axis
            ? QString(u"vJoy %1"_qs).arg(m_inputLearning.target)
            : QString(u"vJoy Button %1"_qs).arg(m_inputLearning.virtualButton)},
        {u"sourceLabel"_qs, m_inputLearning.sourceLabel},
        {u"message"_qs, m_inputLearning.message}};
}

QStringList AppBackend::mappingControlActionChoices() const
{
    return {u"None"_qs, u"Mapping On"_qs, u"Mapping Off"_qs, u"Toggle Mapping"_qs};
}

QVariantList AppBackend::profileTriggerChoices() const
{
    QVariantList choices;
    choices.append(QVariantMap{{u"id"_qs, QString{}}, {u"label"_qs, u"None"_qs}});
    for (const ControllerProfile &profile : m_configuration.profiles) {
        choices.append(QVariantMap{{u"id"_qs, profile.id},
            {u"label"_qs, categoryProfileLabel(m_configuration, profile.id)}});
    }
    return choices;
}

QVariantList AppBackend::nativePovTargetChoices() const
{
    QVariantList choices;
    for (int index = 1; index <= vjoyContinuousPovCount(); ++index) {
        choices.append(QVariantMap{{u"key"_qs, QString(u"continuous:%1"_qs).arg(index)},
            {u"label"_qs, QString(u"vJoy POV %1 · Continuous"_qs).arg(index)}});
    }
    for (int index = 1; index <= vjoyDiscretePovCount(); ++index) {
        choices.append(QVariantMap{{u"key"_qs, QString(u"discrete:%1"_qs).arg(index)},
            {u"label"_qs, QString(u"vJoy POV %1 · Discrete"_qs).arg(index)}});
    }
    return choices;
}

QStringList AppBackend::profileTriggerBehaviorChoices() const
{
    return {u"Hold"_qs, u"Toggle"_qs};
}

void AppBackend::toggleMapping()
{
    setMappingActive(!m_mappingDesired);
}

void AppBackend::setMappingActive(bool active)
{
    if (m_mappingDesired == active && m_worker.mappingRequested() == active) return;
    m_mappingDesired = active;
    m_worker.setMappingEnabled(active);
    // The 16 ms presentation projection is deliberately stopped in Deep Tray
    // Sleep, so keep the native tray control accurate through this direct,
    // user-driven control-plane path.
    refreshTrayStatus();
    appendEvent(active ? u"Starting mapping…"_qs : u"Stopping mapping…"_qs);
    emit stateChanged();
}

bool AppBackend::setMapping(int physicalAxis, const QString &target, bool explicitOverride)
{
    if (!validAxis(physicalAxis)) return false;
    ControllerProfile &profile = currentProfile();
    const VirtualAxis virtualAxis = virtualAxisFromString(target);
    const int targetIndex = static_cast<int>(virtualAxis);
    if (virtualAxis != VirtualAxis::Disabled
        && m_configuration.axisActivity[static_cast<size_t>(physicalAxis)] == PhysicalAxisActivity::Fixed) {
        appendEvent(u"Inactive descriptor axes cannot be routed; complete a new calibration to revise activity"_qs);
        return false;
    }
    const VirtualOutputLayout *layout = activeOutputLayout();
    if (virtualAxis != VirtualAxis::Disabled && layout
        && !layout->requirements.axes[static_cast<size_t>(targetIndex)]) {
        appendEvent(u"Selected virtual axis is not part of this profile's output layout"_qs);
        return false;
    }
    if (virtualAxis != VirtualAxis::Disabled
        && (targetIndex < 1 || targetIndex >= kVirtualAxisSlotCount
            || !m_worker.runtime().virtualAxisAvailable[static_cast<size_t>(targetIndex)].load())) {
        appendEvent(u"Selected vJoy axis is not exposed by the active device"_qs);
        return false;
    }
    if (hasMappingConflict(profile.axes, physicalAxis, virtualAxis)) {
        if (!explicitOverride) return false;
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (index != physicalAxis && profile.axes[index].target == virtualAxis) {
                profile.axes[index].target = VirtualAxis::Disabled;
            }
        }
    }
    profile.axes[physicalAxis].target = virtualAxis;
    persistAndApply();
    return true;
}

void AppBackend::setAxisCustomName(int physicalAxis, const QString &name)
{
    if (!validAxis(physicalAxis)) return;
    AxisMapping &mapping = currentProfile().axes[static_cast<size_t>(physicalAxis)];
    const QString normalized = name.trimmed().left(48);
    if (mapping.customName == normalized) return;
    mapping.customName = normalized;
    persistAndApply();
}

void AppBackend::setAxisRangeMode(int physicalAxis, const QString &mode)
{
    if (!validAxis(physicalAxis)) return;
    AxisMapping &mapping = currentProfile().axes[static_cast<size_t>(physicalAxis)];
    const AxisRangeMode normalized = axisRangeModeFromString(mode, mapping.rangeMode);
    if (mapping.rangeMode == normalized) return;
    const bool wasOneSided = mapping.rangeMode == AxisRangeMode::OneSided;
    const bool willBeOneSided = normalized == AxisRangeMode::OneSided;
    if (wasOneSided) {
        mapping.oneSidedCurveBackup = mapping.curve;
        mapping.hasOneSidedCurveBackup = true;
        mapping.curve = mapping.hasCenteredCurveBackup
            ? mapping.centeredCurveBackup
            : convertCurveDefinitionDomain(mapping.curve, true, false);
    } else {
        mapping.centeredCurveBackup = mapping.curve;
        mapping.hasCenteredCurveBackup = true;
        mapping.curve = mapping.hasOneSidedCurveBackup
            ? mapping.oneSidedCurveBackup
            : convertCurveDefinitionDomain(mapping.curve, false, true);
    }
    switchAxisOutputLimitDomain(mapping, normalized);
    normalizeCurveDefinition(mapping.curve, willBeOneSided);
    persistAndApply();
}

void AppBackend::setVirtualAxisAlias(const QString &target, const QString &alias)
{
    const VirtualAxis axis = virtualAxisFromString(target);
    const int index = static_cast<int>(axis);
    if (index <= 0 || index >= kVirtualAxisSlotCount) return;
    QString normalized = alias.trimmed().left(48);
    currentProfile().virtualAxisAliases[static_cast<size_t>(index)] = normalized;
    persistAndApply();
}

bool AppBackend::startAxisLearning(const QString &target)
{
    const VirtualAxis axis = virtualAxisFromString(target);
    const int index = static_cast<int>(axis);
    const VirtualOutputLayout *layout = activeOutputLayout();
    if (!physicalConnected() || index <= 0 || index >= kVirtualAxisSlotCount || !layout
        || !layout->requirements.axes[static_cast<size_t>(index)]) {
        return false;
    }
    m_inputLearning = {};
    m_inputLearning.kind = InputLearningKind::Axis;
    m_inputLearning.target = virtualAxisLabel(axis);
    enterInputLearningArming();
    emit inputLearningChanged();
    return true;
}

bool AppBackend::startButtonLearning(int virtualButton)
{
    if (!physicalConnected() || virtualButton <= 0 || virtualButton > vjoyButtonCount()) return false;
    m_inputLearning = {};
    m_inputLearning.kind = InputLearningKind::Button;
    m_inputLearning.virtualButton = virtualButton;
    enterInputLearningArming();
    emit inputLearningChanged();
    return true;
}

bool AppBackend::startPovLearning(int virtualButton)
{
    if (!physicalConnected() || povCount() <= 0 || virtualButton <= 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    m_inputLearning = {};
    m_inputLearning.kind = InputLearningKind::Pov;
    m_inputLearning.virtualButton = virtualButton;
    enterInputLearningArming();
    emit inputLearningChanged();
    return true;
}

void AppBackend::retryInputLearning()
{
    if (m_inputLearning.kind == InputLearningKind::None) return;
    m_inputLearning.sourceAxis = -1;
    m_inputLearning.sourceButton = 0;
    m_inputLearning.sourcePovHat = 0;
    m_inputLearning.sourceLabel.clear();
    enterInputLearningArming();
    emit inputLearningChanged();
}

void AppBackend::cancelInputLearning()
{
    if (m_inputLearning.kind == InputLearningKind::None) return;
    m_inputLearning = {};
    emit inputLearningChanged();
}

bool AppBackend::resolveInputLearningConflict(const QString &resolution)
{
    if (m_inputLearning.phase != InputLearningPhase::Conflict) return false;
    if (resolution == u"cancel"_qs) {
        cancelInputLearning();
        return true;
    }
    bool assigned = false;
    switch (m_inputLearning.kind) {
    case InputLearningKind::Axis:
        assigned = setMapping(m_inputLearning.sourceAxis, m_inputLearning.target, true);
        break;
    case InputLearningKind::Button:
        assigned = resolveButtonRouteChange(m_inputLearning.sourceButton,
            m_inputLearning.virtualButton, resolution);
        break;
    case InputLearningKind::Pov:
        assigned = resolution == u"replace"_qs && setPovMapping(m_inputLearning.sourcePovHat,
            povDirectionIndex(m_inputLearning.sourcePovDirection), m_inputLearning.virtualButton, true);
        break;
    case InputLearningKind::None:
        break;
    }
    if (assigned) {
        m_inputLearning.phase = InputLearningPhase::Assigned;
        m_inputLearning.message = QString(u"%1 assigned."_qs).arg(m_inputLearning.sourceLabel);
    } else {
        m_inputLearning.message = u"That assignment is not available for the current output layout."_qs;
    }
    emit inputLearningChanged();
    return assigned;
}

void AppBackend::setSelectedAxis(int physicalAxis)
{
    if (!validAxis(physicalAxis) || m_configuration.selectedAxisIndex == physicalAxis) return;
    m_configuration.selectedAxisIndex = physicalAxis;
    ConfigStore::save(m_configuration);
    rebuildSelectedAxisCurve();
    emit stateChanged();
}

void AppBackend::setAxisInverted(int physicalAxis, bool inverted)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].inverted = inverted;
    persistAndApply();
}

void AppBackend::setAxisDeadzone(int physicalAxis, double deadzone)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].deadzone = std::clamp(static_cast<float>(deadzone), 0.0F, 0.95F);
    persistAndApply();
}

void AppBackend::setAxisHysteresis(int physicalAxis, double hysteresis)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].hysteresis = std::clamp(
        static_cast<float>(hysteresis), 0.0F, 0.25F);
    persistAndApply();
}

bool AppBackend::setAxisOutputLimits(int physicalAxis, double minimum, double maximum)
{
    if (!validAxis(physicalAxis)) return false;
    AxisMapping &mapping = currentProfile().axes[physicalAxis];
    const float domainMinimum = mapping.rangeMode == AxisRangeMode::OneSided ? 0.0F : -1.0F;
    const float boundedMinimum = std::clamp(static_cast<float>(minimum), domainMinimum, 1.0F);
    const float boundedMaximum = std::clamp(static_cast<float>(maximum), domainMinimum, 1.0F);
    if (boundedMinimum >= boundedMaximum) {
        appendEvent(u"Output minimum must remain below output maximum"_qs);
        return false;
    }
    mapping.outputMinimum = boundedMinimum;
    mapping.outputMaximum = boundedMaximum;
    if (mapping.rangeMode == AxisRangeMode::OneSided) {
        mapping.oneSidedOutputMinimum = boundedMinimum;
        mapping.oneSidedOutputMaximum = boundedMaximum;
    } else {
        mapping.centeredOutputMinimum = boundedMinimum;
        mapping.centeredOutputMaximum = boundedMaximum;
    }
    persistAndApply();
    return true;
}

void AppBackend::setCurveFamily(const QString &family)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    const QString normalized = family.trimmed().toCaseFolded();
    if (normalized == u"linear"_qs) {
        mapping->curve = linearCurveDefinition();
    } else if (normalized == u"j-curve"_qs || normalized == u"j"_qs) {
        mapping->curve = standardCurveDefinition(CurveFamily::JCurve, 0.50F);
    } else if (normalized == u"s-curve"_qs || normalized == u"s"_qs) {
        mapping->curve = standardCurveDefinition(CurveFamily::SCurve, 0.50F);
    } else if (normalized == u"advanced"_qs) {
        mapping->curve = advancedCurveDefinition(advancedCurvePresets().front().id);
    } else if (normalized == u"custom"_qs) {
        mapping->curve = materializeCurveDefinition(
            mapping->curve, axisIsOneSided(m_configuration.selectedAxisIndex));
    } else if (normalized == u"personal"_qs) {
        const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
        const auto preset = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [unipolar](const PersonalCurvePreset &entry) {
                return entry.unipolar == unipolar;
            });
        if (preset != m_configuration.personalCurvePresets.cend()) {
            applyPersonalCurvePreset(preset->id);
            return;
        }
        CurveDefinition personal = materializeCurveDefinition(mapping->curve, unipolar);
        personal.family = CurveFamily::Personal;
        personal.sourceFamily = CurveFamily::Personal;
        personal.presetId.clear();
        personal.sourcePresetId.clear();
        personal.baseLabel = u"Unsaved Personal Response"_qs;
        personal.pointEditing = false;
        mapping->curve = std::move(personal);
    } else {
        return;
    }
    persistAndApply();
}

void AppBackend::setCurveStrength(double strength)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || mapping->curve.family == CurveFamily::Linear) return;
    const float bounded = std::clamp(static_cast<float>(strength), 0.0F, 1.0F);
    if (std::abs(mapping->curve.strength - bounded) < 0.0001F) return;
    mapping->curve.strength = bounded;
    persistAndApply();
}

void AppBackend::setCurveStandardPreset(const QString &presetId)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    const CurveFamily family = mapping->curve.family == CurveFamily::SCurve
        ? CurveFamily::SCurve : CurveFamily::JCurve;
    mapping->curve = standardCurveDefinition(family, presetId);
    persistAndApply();
}

void AppBackend::applyAdvancedCurvePreset(const QString &presetId)
{
    if (!advancedCurvePreset(presetId)) return;
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = advancedCurveDefinition(presetId);
    persistAndApply();
}

bool AppBackend::applyPersonalCurvePreset(const QString &presetId)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
        m_configuration.personalCurvePresets.cend(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.cend() || found->unipolar != unipolar) return false;
    mapping->curve = found->definition;
    mapping->curve.family = CurveFamily::Personal;
    mapping->curve.sourceFamily = CurveFamily::Personal;
    mapping->curve.presetId = found->id;
    mapping->curve.sourcePresetId = found->id;
    mapping->curve.baseLabel = found->name;
    persistAndApply();
    appendEvent(u"Applied personal curve preset: "_qs + found->name);
    return true;
}

void AppBackend::setCurvePointEditing(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || mapping->curve.pointEditing == enabled) return;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    if (enabled) {
        mapping->curve = materializeCurveDefinition(mapping->curve, unipolar);
    } else {
        mapping->curve.pointEditing = false;
    }
    persistAndApply();
}

void AppBackend::setCurveInterpolation(const QString &interpolation)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing) return;
    mapping->curve.interpolation = interpolation.trimmed().compare(
        u"linear"_qs, Qt::CaseInsensitive) == 0 ? CurveInterpolation::Linear : CurveInterpolation::Smooth;
    persistAndApply();
}

void AppBackend::setCurvePointDensity(int density)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing || !supportedCurvePointDensity(density)) return;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    mapping->curve = resampleCurveDefinition(mapping->curve, unipolar, density);
    persistAndApply();
}

void AppBackend::setCurveSymmetry(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing
        || axisIsOneSided(m_configuration.selectedAxisIndex)) return;
    mapping->curve.symmetry = enabled;
    normalizeCurveDefinition(mapping->curve, false);
    persistAndApply();
}

bool AppBackend::setCurvePoint(int index, double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = updateCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index,
        static_cast<float>(input), static_cast<float>(output));
    if (changed) persistAndApply();
    return changed;
}

bool AppBackend::setCurvePointLocked(int index, bool locked)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::setCurvePointLocked(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index, locked);
    if (changed) persistAndApply();
    return changed;
}

int AppBackend::addCurvePoint(double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return -1;
    int selected = -1;
    if (!hotas::addCurvePoint(mapping->curve,
            axisIsOneSided(m_configuration.selectedAxisIndex),
            static_cast<float>(input), static_cast<float>(output), &selected)) return -1;
    persistAndApply();
    return selected;
}

bool AppBackend::removeCurvePoint(int index)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::removeCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index);
    if (changed) persistAndApply();
    return changed;
}

void AppBackend::resetCurveLinear()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = linearCurveDefinition();
    persistAndApply();
}

bool AppBackend::resetCurveToSource()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const CurveDefinition current = mapping->curve;
    if (current.sourceFamily == CurveFamily::JCurve || current.sourceFamily == CurveFamily::SCurve) {
        mapping->curve = standardCurveDefinition(current.sourceFamily,
            current.sourcePresetId.isEmpty() ? u"medium"_qs : current.sourcePresetId);
    } else if (current.sourceFamily == CurveFamily::Advanced) {
        mapping->curve = advancedCurveDefinition(current.sourcePresetId);
    } else if (current.sourceFamily == CurveFamily::Personal) {
        if (!applyPersonalCurvePreset(current.sourcePresetId)) return false;
        return true;
    } else {
        mapping->curve = linearCurveDefinition();
    }
    persistAndApply();
    return true;
}

bool AppBackend::copyCurveFrom(const QString &profileId, int axisIndex)
{
    if (!validAxis(axisIndex)) return false;
    const ControllerProfile *source = findProfile(m_configuration, profileId);
    AxisMapping *target = selectedAxisMapping();
    if (!source || !target) return false;
    const bool targetUnipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const bool sourceUnipolar = source->axes[static_cast<size_t>(axisIndex)].rangeMode
        == AxisRangeMode::OneSided;
    const CurveDefinition &curve = source->axes[axisIndex].curve;
    if (targetUnipolar != sourceUnipolar && (curve.pointEditing || curve.family == CurveFamily::Custom)) {
        appendEvent(u"Point-edited curves can only copy to a compatible axis domain"_qs);
        return false;
    }
    target->curve = curve;
    persistAndApply();
    appendEvent(u"Copied response curve from "_qs + source->name);
    return true;
}

bool AppBackend::copyCurveFromSelection(const QString &selectionId)
{
    const int separator = selectionId.lastIndexOf(u':');
    if (separator <= 0) return false;
    bool valid = false;
    const int axis = selectionId.mid(separator + 1).toInt(&valid);
    return valid && copyCurveFrom(selectionId.left(separator), axis);
}

bool AppBackend::saveCurrentCurveAsPersonalPreset(const QString &name)
{
    const QString trimmed = name.trimmed();
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !personalCurvePresetNameAvailable(m_configuration.personalCurvePresets, trimmed)) {
        appendEvent(u"Personal preset names must be unique and between 1 and 48 characters"_qs);
        return false;
    }
    PersonalCurvePreset preset;
    preset.id = u"curve-"_qs + QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = trimmed;
    preset.unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    preset.definition = mapping->curve;
    // A library entry owns a concrete curve shape. Materialize generated
    // families once so applying it later is always a copy, never a reference
    // back to a mutable source preset.
    if (preset.definition.points.empty()) {
        preset.definition = materializeCurveDefinition(preset.definition, preset.unipolar);
        preset.definition.pointEditing = false;
    }
    m_configuration.personalCurvePresets.push_back(std::move(preset));
    persistAndApply();
    appendEvent(u"Saved personal curve preset: "_qs + trimmed);
    return true;
}

bool AppBackend::renamePersonalCurvePreset(const QString &presetId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!personalCurvePresetNameAvailable(m_configuration.personalCurvePresets, trimmed, presetId)) return false;
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end()) return false;
    found->name = trimmed;
    persistAndApply();
    return true;
}

bool AppBackend::deletePersonalCurvePreset(const QString &presetId)
{
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end()) return false;
    const QString name = found->name;
    m_configuration.personalCurvePresets.erase(found);
    persistAndApply();
    appendEvent(u"Deleted personal curve preset: "_qs + name);
    return true;
}

bool AppBackend::updatePersonalCurvePreset(const QString &presetId)
{
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end() || found->unipolar != unipolar) return false;
    found->definition = mapping->curve;
    if (found->definition.points.empty()) {
        found->definition = materializeCurveDefinition(found->definition, unipolar);
        found->definition.pointEditing = false;
    }
    found->definition.sourceFamily = CurveFamily::Personal;
    found->definition.sourcePresetId = found->id;
    persistAndApply();
    appendEvent(u"Updated personal curve preset: "_qs + found->name);
    return true;
}

void AppBackend::setCurveComparison(const QString &comparisonId)
{
    m_curveComparisonId.clear();
    m_curveComparisonLabel.clear();
    if (!comparisonId.isEmpty()) {
        const QVariantList choices = curveComparisonChoices();
        for (const QVariant &choiceValue : choices) {
            const QVariantMap choice = choiceValue.toMap();
            if (choice.value(u"id"_qs).toString() != comparisonId) continue;
            m_curveComparisonId = comparisonId;
            m_curveComparisonLabel = choice.value(u"label"_qs).toString();
            break;
        }
    }
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

QVariantMap AppBackend::inspectCurve(double domainInput) const
{
    QVariantMap result;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const float input = std::clamp(static_cast<float>(domainInput), unipolar ? 0.0F : -1.0F, 1.0F);
    result.insert(u"input"_qs, input);
    result.insert(u"output"_qs, evaluateCurveDefinition(input, mapping->curve, unipolar));
    result.insert(u"gain"_qs, evaluateCurveGain(input, mapping->curve, unipolar));
    return result;
}

QString AppBackend::curveEditorSnapshot() const
{
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return {};
    return QString::fromUtf8(QJsonDocument(curveDefinitionToJson(mapping->curve))
        .toJson(QJsonDocument::Compact));
}

bool AppBackend::restoreCurveEditorSnapshot(const QString &snapshot)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const QJsonDocument document = QJsonDocument::fromJson(snapshot.toUtf8());
    if (!document.isObject()) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    CurveDefinition restored = curveDefinitionFromJson(document.object(), unipolar);
    if (!curveDefinitionIsValid(restored, unipolar)) return false;
    mapping->curve = std::move(restored);
    persistAndApply();
    return true;
}

void AppBackend::previewCurvePreset(const QString &presetId)
{
    const QStringList parts = presetId.split(u':');
    CurveDefinition definition;
    if (parts.size() == 3 && parts[0] == u"standard"_qs) {
        definition = standardCurveDefinition(parts[1] == u"s"_qs ? CurveFamily::SCurve
                                                                   : CurveFamily::JCurve,
                                           parts[2]);
    } else if (parts.size() == 2 && parts[0] == u"advanced"_qs) {
        definition = advancedCurveDefinition(parts[1]);
    } else if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found == m_configuration.personalCurvePresets.cend()) return;
        definition = found->definition;
    } else {
        return;
    }
    m_curvePreviewId = presetId;
    m_curvePreviewDefinition = std::move(definition);
    for (const QVariant &choice : curvePreviewChoices()) {
        const QVariantMap item = choice.toMap();
        if (item.value(u"id"_qs).toString() == presetId) {
            m_curvePreviewLabel = item.value(u"label"_qs).toString();
            break;
        }
    }
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

void AppBackend::clearCurvePreview()
{
    if (m_curvePreviewId.isEmpty()) return;
    m_curvePreviewId.clear();
    m_curvePreviewLabel.clear();
    m_curvePreviewDefinition = linearCurveDefinition();
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

bool AppBackend::applyCurvePreview()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || m_curvePreviewId.isEmpty()) return false;
    mapping->curve = m_curvePreviewDefinition;
    const QStringList parts = m_curvePreviewId.split(u':');
    if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found != m_configuration.personalCurvePresets.cend()) {
            mapping->curve.family = CurveFamily::Personal;
            mapping->curve.presetId = found->id;
            mapping->curve.baseLabel = found->name;
            mapping->curve.sourcePresetId = found->id;
        }
    }
    if (mapping->curve.family == CurveFamily::Personal) {
        mapping->curve.sourceFamily = CurveFamily::Personal;
    }
    m_curvePreviewId.clear();
    m_curvePreviewLabel.clear();
    m_curvePreviewDefinition = linearCurveDefinition();
    persistAndApply();
    return true;
}

bool AppBackend::setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride)
{
    if (!validPhysicalButton(physicalButton) || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    const int source = physicalButton - 1;
    ButtonBindings &bindings = currentProfile().buttons;
    const ButtonRouteChange change = analyzeButtonRouteChange(bindings, source, virtualButton,
                                                               vjoyButtonCount());
    const bool povConflict = hasButtonMappingConflict(bindings, currentProfile().povs, source,
                                                       virtualButton, vjoyButtonCount())
        && !change.requiresResolution;
    if (change.requiresResolution || povConflict) {
        return explicitOverride && resolveButtonRouteChange(physicalButton, virtualButton, u"replace"_qs);
    }
    return resolveButtonRouteChange(physicalButton, virtualButton, u"replace"_qs);
}

bool AppBackend::resolveButtonRouteChange(int physicalButton, int virtualButton,
                                          const QString &resolution)
{
    if (!validPhysicalButton(physicalButton) || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    ButtonRouteResolution decision = ButtonRouteResolution::Cancel;
    if (resolution == u"replace"_qs) decision = ButtonRouteResolution::Replace;
    else if (resolution == u"ignore"_qs) decision = ButtonRouteResolution::Ignore;
    if (decision == ButtonRouteResolution::Cancel) return false;

    const int source = physicalButton - 1;
    ButtonBindings &bindings = currentProfile().buttons;
    const ButtonRouteChange change = analyzeButtonRouteChange(bindings, source, virtualButton,
                                                               vjoyButtonCount());
    if (!change.valid) return false;

    // POV routes retain their existing exclusive contract.  They are never
    // silently displaced, and physical-button Ignore is intentionally limited
    // to the documented many-physical-sources fan-in case.
    bool povConflict = false;
    for (const PovDirectionBindings &hat : currentProfile().povs) {
        for (const ButtonBinding &binding : hat) {
            povConflict = povConflict || (virtualButton > 0
                && binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton);
        }
    }
    if (povConflict && decision != ButtonRouteResolution::Replace) return false;
    if (povConflict) {
        for (PovDirectionBindings &hat : currentProfile().povs) {
            for (ButtonBinding &binding : hat) {
                if (binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton) {
                    binding.type = ButtonActionType::Disabled;
                    binding.target = 0;
                    binding.explicitlyConfigured = true;
                }
            }
        }
    }
    if (!applyButtonRouteChange(bindings, change, decision)) return false;
    persistAndApply();
    const QString action = decision == ButtonRouteResolution::Ignore
        ? u"shared with"_qs : (change.canSwap && change.requiresResolution ? u"swapped with"_qs : u"routed to"_qs);
    appendEvent(QString(u"Button %1 %2 %3"_qs).arg(physicalButton).arg(action).arg(
        virtualButton > 0 ? QString(u"vJoy %1"_qs).arg(virtualButton) : u"Disabled"_qs));
    return true;
}

void AppBackend::setButtonCustomName(int physicalButton, const QString &name)
{
    if (!validPhysicalButton(physicalButton)) return;
    const int source = physicalButton - 1;
    ButtonBindings &bindings = currentProfile().buttons;
    if (bindings.size() <= static_cast<size_t>(source)) {
        bindings.resize(static_cast<size_t>(source + 1));
    }
    ButtonBinding &binding = bindings[static_cast<size_t>(source)];
    const QString normalized = name.trimmed().left(48);
    if (binding.customName == normalized) return;
    binding.customName = normalized;
    persistAndApply();
}

bool AppBackend::setMappingControl(int physicalButton, const QString &action)
{
    if (!validPhysicalButton(physicalButton)) return false;
    const int source = physicalButton - 1;
    const MappingControlAction normalized = mappingControlActionFromString(action);
    if (m_configuration.mappingControls.size() <= static_cast<size_t>(source)) {
        m_configuration.mappingControls.resize(static_cast<size_t>(source + 1));
    }
    if (m_configuration.mappingControls[static_cast<size_t>(source)] == normalized) return true;
    m_configuration.mappingControls[static_cast<size_t>(source)] = normalized;
    persistAndApply();
    appendEvent(QString(u"Button %1 mapping control: %2"_qs).arg(physicalButton)
        .arg(mappingControlActionLabel(normalized)));
    return true;
}

bool AppBackend::setPovMapping(int povHat, int direction, int virtualButton, bool explicitOverride)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= povCount() || hat >= kMaximumPhysicalPovs
        || direction < 0 || direction >= kPovDirectionCount || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    PovBindings &povs = currentProfile().povs;
    if (povs.size() <= static_cast<size_t>(hat)) povs.resize(static_cast<size_t>(hat + 1));
    if (hasPovMappingConflict(currentProfile().buttons, povs, hat, direction, virtualButton,
                              vjoyButtonCount())) {
        if (!explicitOverride) return false;
        for (ButtonBinding &binding : currentProfile().buttons) {
            if (binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton) {
                binding = {};
                binding.explicitlyConfigured = true;
            }
        }
        for (int otherHat = 0; otherHat < static_cast<int>(povs.size()); ++otherHat) {
            for (int otherDirection = 0; otherDirection < kPovDirectionCount; ++otherDirection) {
                if (otherHat == hat && otherDirection == direction) continue;
                ButtonBinding &binding = povs[static_cast<size_t>(otherHat)][static_cast<size_t>(otherDirection)];
                if (binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton) {
                    binding = {};
                    binding.explicitlyConfigured = true;
                }
            }
        }
    }
    ButtonBinding &binding = povs[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
    binding = virtualButton > 0 ? ButtonBinding{ButtonActionType::VirtualButton, virtualButton}
                                : ButtonBinding{};
    binding.explicitlyConfigured = true;
    persistAndApply();
    appendEvent(QString(u"POV %1 %2 → %3"_qs).arg(povHat)
        .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)))
        .arg(virtualButton > 0 ? QString(u"vJoy %1"_qs).arg(virtualButton) : u"Disabled"_qs));
    return true;
}

bool AppBackend::setProfileTrigger(int physicalButton, const QString &targetProfileId,
                                   const QString &behavior)
{
    const int source = physicalButton - 1;
    if (source < 0 || source >= kMaximumPhysicalButtons) return false;
    if (m_configuration.profileTriggers.size() <= static_cast<size_t>(source)) {
        m_configuration.profileTriggers.resize(static_cast<size_t>(source + 1));
    }
    ProfileTriggerBinding &trigger = m_configuration.profileTriggers[static_cast<size_t>(source)];
    const QString target = targetProfileId.trimmed();
    if (target.isEmpty()) {
        trigger = {};
        persistAndApply();
        appendEvent(QString(u"Button %1 profile control cleared; game route restored"_qs).arg(physicalButton));
        return true;
    }
    const ControllerProfile *profile = findProfile(m_configuration, target);
    if (!profile) return false;
    const ProfileTriggerMode mode = profileTriggerModeFromString(behavior);
    if (mode == ProfileTriggerMode::Disabled) return false;
    trigger = {target, mode};
    persistAndApply();
    appendEvent(QString(u"Button %1 → profile %2 · %3 (game route consumed)"_qs)
        .arg(physicalButton).arg(profile->name).arg(profileTriggerModeLabel(mode)));
    return true;
}

bool AppBackend::setPovProfileTrigger(int povHat, int direction,
                                      const QString &targetProfileId, const QString &behavior)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= kMaximumPhysicalPovs || direction < 0 || direction >= kPovDirectionCount) {
        return false;
    }
    if (m_configuration.povProfileTriggers.size() <= static_cast<size_t>(hat)) {
        m_configuration.povProfileTriggers.resize(static_cast<size_t>(hat + 1));
    }
    ProfileTriggerBinding &trigger = m_configuration.povProfileTriggers[static_cast<size_t>(hat)]
        [static_cast<size_t>(direction)];
    const QString target = targetProfileId.trimmed();
    const QString inputLabel = QString(u"POV %1 %2"_qs).arg(povHat)
        .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)));
    if (target.isEmpty()) {
        trigger = {};
        persistAndApply();
        appendEvent(inputLabel + u" profile control cleared; game route restored"_qs);
        return true;
    }
    const ControllerProfile *profile = findProfile(m_configuration, target);
    if (!profile) return false;
    const ProfileTriggerMode mode = profileTriggerModeFromString(behavior);
    if (mode == ProfileTriggerMode::Disabled) return false;
    trigger = {target, mode};
    persistAndApply();
    appendEvent(QString(u"%1 → profile %2 · %3 (direction route consumed)"_qs)
        .arg(inputLabel).arg(profile->name).arg(profileTriggerModeLabel(mode)));
    return true;
}

bool AppBackend::setNativePovOutput(int povHat, bool enabled, const QString &targetKey)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= kMaximumPhysicalPovs) return false;
    if (m_configuration.nativePovBindings.size() <= static_cast<size_t>(hat)) {
        m_configuration.nativePovBindings.resize(static_cast<size_t>(hat + 1));
    }
    NativePovBinding binding;
    if (enabled) {
        const QStringList parts = targetKey.split(u":"_qs);
        if (parts.size() != 2) return false;
        if (parts[0] == u"continuous"_qs) binding.targetType = NativePovTargetType::Continuous;
        else if (parts[0] == u"discrete"_qs) binding.targetType = NativePovTargetType::Discrete;
        else return false;
        bool ok = false;
        binding.targetIndex = parts[1].toInt(&ok);
        if (!ok || binding.targetIndex < 1) return false;
        const bool targetAvailable = binding.targetType == NativePovTargetType::Continuous
            ? binding.targetIndex <= vjoyContinuousPovCount()
            : binding.targetIndex <= vjoyDiscretePovCount();
        if (!targetAvailable) return false;
        for (int otherHat = 0; otherHat < static_cast<int>(m_configuration.nativePovBindings.size()); ++otherHat) {
            if (otherHat == hat) continue;
            const NativePovBinding &existing = m_configuration.nativePovBindings[static_cast<size_t>(otherHat)];
            if (existing.enabled && existing.targetType == binding.targetType
                && existing.targetIndex == binding.targetIndex) {
                appendEvent(u"Each native vJoy POV target can have only one physical POV owner"_qs);
                return false;
            }
        }
        binding.enabled = true;
    }
    m_configuration.nativePovBindings[static_cast<size_t>(hat)] = binding;
    persistAndApply();
    appendEvent(enabled ? QString(u"POV %1 native vJoy output enabled"_qs).arg(povHat)
                        : QString(u"POV %1 native vJoy output disabled"_qs).arg(povHat));
    return true;
}

void AppBackend::resetButtonMappings()
{
    const int physicalCount = buttonCount();
    const int virtualCount = vjoyButtonCount();
    if (physicalCount <= 0) {
        appendEvent(u"Connect a controller before resetting button mappings"_qs);
        return;
    }
    currentProfile().buttons = defaultButtonMappings(physicalCount, virtualCount);
    persistAndApply();
    appendEvent(u"Button mappings reset to passthrough defaults"_qs);
}

bool AppBackend::createProfile(const QString &name, const QString &startFromId)
{
    const QString trimmedName = name.trimmed();
    if (!hotas::createProfile(m_configuration, trimmedName, startFromId)) {
        appendEvent(u"Profile name must be unique within its category and between 1 and 48 characters"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Created profile: "_qs + trimmedName);
    return true;
}

bool AppBackend::createProfileInCategory(const QString &name, const QString &categoryId,
                                         const QString &startFromId)
{
    if (!hotas::createProfileInCategory(m_configuration, name.trimmed(), categoryId, startFromId)) {
        appendEvent(u"Choose a category and a profile name unique within that category"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(QString(u"Created profile: %1"_qs).arg(name.trimmed()));
    return true;
}

bool AppBackend::cloneProfile(const QString &profileId)
{
    const ControllerProfile *source = findProfile(m_configuration, profileId);
    if (!source) return false;
    const QString sourceName = source->name;
    if (!hotas::cloneProfile(m_configuration, profileId)) return false;
    persistAndApply();
    appendEvent(u"Cloned profile: "_qs + sourceName);
    return true;
}

bool AppBackend::duplicateProfileToCategory(const QString &profileId, const QString &name,
                                            const QString &categoryId)
{
    if (!hotas::duplicateProfileToCategory(m_configuration, profileId, name, categoryId)) {
        appendEvent(u"Could not duplicate the profile into the selected category"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(QString(u"Duplicated profile as %1"_qs).arg(name.trimmed()));
    return true;
}

bool AppBackend::renameProfile(const QString &profileId, const QString &name)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    const QString trimmedName = name.trimmed();
    if (!profile) {
        return false;
    }
    const QString previousName = profile->name;
    if (!hotas::renameProfile(m_configuration, profileId, trimmedName)) {
        appendEvent(u"Profile name must be unique and between 1 and 48 characters"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Renamed profile: "_qs + previousName + u" → "_qs + trimmedName);
    return true;
}

bool AppBackend::moveProfileToCategory(const QString &profileId, const QString &categoryId)
{
    const QString label = profileDisplayName(profileId);
    if (!hotas::moveProfileToCategory(m_configuration, profileId, categoryId)) {
        appendEvent(u"Could not move the profile; names must be unique within the destination category"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Moved profile: "_qs + label + u" → "_qs + profileDisplayName(profileId));
    return true;
}

bool AppBackend::setProfileEnabled(const QString &profileId, bool enabled)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile || profile->enabled == enabled) return profile != nullptr;
    if (!enabled && profileId == m_configuration.activeProfileId) {
        appendEvent(u"Select another profile before disabling the active one"_qs);
        return false;
    }
    profile->enabled = enabled;
    persistAndApply();
    appendEvent(QString(u"Profile %1: %2"_qs).arg(profile->name,
        enabled ? u"enabled"_qs : u"disabled"_qs));
    return true;
}

bool AppBackend::deleteProfile(const QString &profileId)
{
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile) return false;
    const QString name = profile->name;
    if (!hotas::deleteProfile(m_configuration, profileId)) {
        appendEvent(u"Activate another profile before deleting this one"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Deleted profile: "_qs + name);
    return true;
}

bool AppBackend::activateProfile(const QString &profileId)
{
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile || !profile->enabled) return false;
    if (profileId == m_configuration.activeProfileId) return true;
    const bool outputChanges = profile->outputLayoutId != currentProfile().outputLayoutId;
    const bool mappingWasRequested = outputChanges && m_worker.mappingRequested();
    if (outputChanges && !m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not safely release the current virtual output for profile switching"_qs);
        return false;
    }
    if (outputChanges) {
        ControllerReadinessService visibility;
        const OutputVisibilitySwitchResult visibilityResult =
            visibility.applyManagedOutputVisibility(m_configuration, profile->outputLayoutId);
        if (!visibilityResult.succeeded) {
            m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
            appendEvent(visibilityResult.status);
            return false;
        }
        appendEvent(visibilityResult.status);
    }
    const QString name = profile->name;
    if (!hotas::activateProfile(m_configuration, profileId)) return false;
    synchronizeActiveOutputLayout();
    persistAndApply();
    if (outputChanges && !m_worker.restoreAfterDriverConfiguration(mappingWasRequested)) {
        appendEvent(u"Profile was selected, but mapping could not reacquire its virtual output"_qs);
    }
    appendEvent(u"Activated profile: "_qs + name + u" · "_qs + activeOutputLayoutName());
    return true;
}

bool AppBackend::createProfileCategory(const QString &name)
{
    QString id;
    if (!hotas::createProfileCategory(m_configuration, name, &id)) {
        appendEvent(u"Category names must be unique and between 1 and 64 characters"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Created category: "_qs + name.trimmed());
    return true;
}

bool AppBackend::renameProfileCategory(const QString &categoryId, const QString &name)
{
    const ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    const QString previous = category ? category->name : QString{};
    if (!hotas::renameProfileCategory(m_configuration, categoryId, name)) return false;
    persistAndApply();
    appendEvent(QString(u"Renamed category: %1 → %2"_qs).arg(previous, name.trimmed()));
    return true;
}

bool AppBackend::deleteProfileCategory(const QString &categoryId)
{
    const ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    const QString name = category ? category->name : QString{};
    if (!hotas::deleteProfileCategory(m_configuration, categoryId)) {
        appendEvent(u"Move or delete all profiles before deleting a category"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Deleted empty category: "_qs + name);
    return true;
}

bool AppBackend::activateProfileCategory(const QString &categoryId)
{
    QString profileId;
    MapperConfiguration candidate = m_configuration;
    if (!hotas::activateCategoryProfile(candidate, categoryId, &profileId)) return false;
    // Route category selection through the existing safe profile activation;
    // it retains vJoy layout release/reacquire behavior.
    return activateProfile(profileId);
}

bool AppBackend::setProfileCategoryEnabled(const QString &categoryId, bool enabled)
{
    ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    if (!category || category->enabled == enabled) return category != nullptr;
    if (!enabled && categoryId == activeCategoryId()) {
        appendEvent(u"Select another category before disabling the active category"_qs);
        return false;
    }
    category->enabled = enabled;
    persistAndApply();
    appendEvent(QString(u"Category %1: %2"_qs).arg(category->name,
        enabled ? u"enabled"_qs : u"disabled"_qs));
    return true;
}

bool AppBackend::setCategoryDefaultProfile(const QString &categoryId, const QString &profileId)
{
    ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!category || !profile || profile->categoryId != categoryId) return false;
    category->defaultProfileId = profileId;
    persistAndApply();
    return true;
}

bool AppBackend::setCategoryRestoreLastProfile(const QString &categoryId, bool restoreLastProfile)
{
    ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    if (!category || category->restoreLastProfile == restoreLastProfile) return category != nullptr;
    category->restoreLastProfile = restoreLastProfile;
    persistAndApply();
    return true;
}

bool AppBackend::setCategoryGameDetectionRules(const QString &categoryId, const QStringList &rules)
{
    ProfileCategory *category = findProfileCategory(m_configuration, categoryId);
    if (!category || rules.size() > 32) return false;
    QStringList normalized;
    for (const QString &raw : rules) {
        const QString rule = QFileInfo(raw.trimmed()).fileName().left(260);
        if (rule.isEmpty()) continue;
        if (!normalized.contains(rule, Qt::CaseInsensitive)) normalized.append(rule);
    }
    category->executableRules = normalized;
    persistAndApply();
    return true;
}

QVariantList AppBackend::runningApplications() const
{
    return m_runningApplications;
}

void AppBackend::refreshRunningApplications()
{
    startRunningApplicationSnapshot(true);
}

void AppBackend::setAutomaticGameDetection(bool enabled)
{
    if (m_configuration.automaticGameDetection == enabled) return;
    m_configuration.automaticGameDetection = enabled;
    m_lastDetectedExecutables.clear();
    if (enabled) {
        const int interval = m_presentationLifecycle == PresentationLifecycleState::Visible
            ? kVisibleGameDetectionIntervalMs
            : m_presentationLifecycle == PresentationLifecycleState::Minimized
                ? kMinimizedGameDetectionIntervalMs : kTrayHiddenGameDetectionIntervalMs;
        m_gameDetectionTimer.start(interval);
        startRunningApplicationSnapshot(false);
    } else {
        m_gameDetectionTimer.stop();
    }
    persistAndApply();
    appendEvent(enabled ? u"Automatic game category detection enabled"_qs
                        : u"Automatic game category detection disabled"_qs);
}

void AppBackend::startRunningApplicationSnapshot(bool resolvePaths)
{
    if (m_gameDetectionInProgress) return;
    m_gameDetectionInProgress = true;
    if (m_uiPerformanceInstrumentationEnabled) ++m_gameDetectionBackgroundRuns;
    QHash<QString, QString> pathCache = m_runningApplicationPathCache;
    QThread *thread = QThread::create([this, resolvePaths, pathCache]() mutable {
        QList<RunningApplication> snapshot = runningApplicationSnapshot(resolvePaths, &pathCache);
        QVariantList applications;
        QStringList runningExecutables;
        applications.reserve(snapshot.size());
        runningExecutables.reserve(snapshot.size());
        for (const RunningApplication &application : snapshot) {
            applications.append(QVariantMap{{u"name"_qs, application.name},
                                            {u"executable"_qs, application.executable},
                                            {u"path"_qs, application.path}});
            runningExecutables.append(application.executable);
        }
        runningExecutables.sort(Qt::CaseInsensitive);
        QMetaObject::invokeMethod(this, [this, applications = std::move(applications),
                                         runningExecutables = std::move(runningExecutables),
                                         pathCache = std::move(pathCache)] () mutable {
            m_gameDetectionInProgress = false;
            m_runningApplicationPathCache = std::move(pathCache);
            if (m_runningApplications != applications) {
                m_runningApplications = std::move(applications);
                emit runningApplicationsChanged();
            }
            if (!m_configuration.automaticGameDetection
                || runningExecutables == m_lastDetectedExecutables) return;
            m_lastDetectedExecutables = runningExecutables;
            const GameCategoryMatch match = categoryForRunningExecutables(
                m_configuration, runningExecutables, activeCategoryId());
            if (match.categoryId.isEmpty() || match.categoryId == activeCategoryId()) return;
            const ProfileCategory *category = findProfileCategory(m_configuration, match.categoryId);
            if (category && activateProfileCategory(match.categoryId)) {
                appendEvent(QString(u"Game detection selected category: %1"_qs).arg(category->name));
            }
        }, Qt::QueuedConnection);
    });
    m_gameDetectionThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_gameDetectionThread == thread) m_gameDetectionThread = nullptr;
        thread->deleteLater();
    });
    thread->start(QThread::LowPriority);
}

QVariantMap AppBackend::profileDetail(const QString &profileId) const
{
    QVariantMap detail;
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile) return detail;
    const ProfileCategory *category = findProfileCategory(m_configuration, profile->categoryId);
    detail.insert(u"id"_qs, profile->id);
    detail.insert(u"name"_qs, profile->name);
    detail.insert(u"categoryId"_qs, profile->categoryId);
    detail.insert(u"category"_qs, category ? category->name : u"General"_qs);
    detail.insert(u"categoryGames"_qs, category ? category->executableRules : QStringList{});
    detail.insert(u"categoryActivationBehavior"_qs, category && !category->restoreLastProfile
        ? u"Always use the selected profile"_qs : u"Restore the last-used profile"_qs);
    detail.insert(u"displayName"_qs, profileDisplayName(profileId));
    detail.insert(u"active"_qs, profile->id == m_configuration.activeProfileId);
    detail.insert(u"enabled"_qs, profile->enabled);
    const CurveTransitionSmoothingSettings transitionSettings = sanitizedCurveTransitionSmoothing(
        profile->curveTransitionSmoothingOverride ? profile->curveTransitionSmoothing
                                                  : m_configuration.curveTransitionSmoothing);
    detail.insert(u"curveTransitionSmoothingOverride"_qs,
                  profile->curveTransitionSmoothingOverride);
    detail.insert(u"curveTransitionSmoothingEnabled"_qs, transitionSettings.enabled);
    detail.insert(u"curveTransitionDurationMs"_qs, transitionSettings.durationMs);
    detail.insert(u"globalCurveTransitionSmoothingEnabled"_qs,
                  curveTransitionSmoothingEnabled());
    detail.insert(u"globalCurveTransitionDurationMs"_qs,
                  curveTransitionDurationMs());
    QVariantList axes;
    QVariantList curves;
    int mappedAxes = 0;
    int customCurves = 0;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const AxisMapping &axis = profile->axes[static_cast<size_t>(index)];
        if (axis.target == VirtualAxis::Disabled) continue;
        ++mappedAxes;
        QVariantMap item;
        item.insert(u"physical"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(index)));
        item.insert(u"virtual"_qs, virtualAxisLabel(axis.target));
        item.insert(u"inverted"_qs, axis.inverted);
        item.insert(u"deadzone"_qs, axis.deadzone * 100.0);
        item.insert(u"minimum"_qs, axis.outputMinimum * 100.0);
        item.insert(u"maximum"_qs, axis.outputMaximum * 100.0);
        item.insert(u"curve"_qs, curveDefinitionSummary(axis.curve));
        axes.append(item);
        if (axis.curve.family != CurveFamily::Linear) {
            ++customCurves;
            curves.append(QVariantMap{{u"axis"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(index))},
                {u"summary"_qs, curveDefinitionSummary(axis.curve)},
                {u"points"_qs, static_cast<int>(axis.curve.points.size())}});
        }
    }
    QVariantList buttons;
    int mappedButtons = 0;
    for (int index = 0; index < static_cast<int>(profile->buttons.size()); ++index) {
        const ButtonBinding &binding = profile->buttons[static_cast<size_t>(index)];
        if (binding.type != ButtonActionType::VirtualButton) continue;
        ++mappedButtons;
        buttons.append(QVariantMap{{u"input"_qs, QString(u"Button %1"_qs).arg(index + 1)},
            {u"output"_qs, QString(u"vJoy Button %1"_qs).arg(binding.target)}});
    }
    QVariantList povs;
    int mappedPovs = 0;
    int mappedPovHats = 0;
    for (int hat = 0; hat < static_cast<int>(profile->povs.size()); ++hat) {
        bool hatMapped = false;
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding &binding = profile->povs[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (binding.type != ButtonActionType::VirtualButton) continue;
            ++mappedPovs;
            hatMapped = true;
            povs.append(QVariantMap{{u"input"_qs, QString(u"POV %1 %2"_qs).arg(hat + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)))},
                {u"output"_qs, QString(u"vJoy Button %1"_qs).arg(binding.target)}});
        }
        if (hatMapped) ++mappedPovHats;
    }
    const int profileControlButtons = static_cast<int>(std::count_if(m_configuration.profileTriggers.cbegin(),
        m_configuration.profileTriggers.cend(), [&profileId](const ProfileTriggerBinding &binding) {
            return binding.targetProfileId == profileId && profileTriggerBindingEnabled(binding);
        }));
    QVariantList automations;
    for (const AutomationDefinition &automation : m_configuration.automations) {
        bool related = false;
        for (const AutomationConditionDefinition &condition : automation.conditions) {
            related = related || condition.profileId == profileId;
        }
        for (const AutomationActionDefinition &action : automation.actions) {
            related = related || action.profileId == profileId;
        }
        if (related) automations.append(QVariantMap{{u"id"_qs, automation.id}, {u"name"_qs, automation.name},
            {u"enabled"_qs, automation.enabled}});
    }
    const VirtualOutputLayout *layout = findOutputLayout(m_configuration, profile->outputLayoutId);
    const int outputAxes = layout ? static_cast<int>(std::count(layout->requirements.axes.cbegin(),
        layout->requirements.axes.cend(), true)) : 0;
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    const int availableAxes = static_cast<int>(std::count(physical.axes.cbegin(), physical.axes.cend(), true));
    const bool compatible = !physical.connected || (availableAxes >= mappedAxes
        && physical.buttons >= mappedButtons && physical.povs >= (mappedPovs > 0 ? 1 : 0));
    detail.insert(u"mappedAxes"_qs, mappedAxes);
    detail.insert(u"mappedButtons"_qs, mappedButtons);
    detail.insert(u"mappedPovs"_qs, mappedPovs);
    detail.insert(u"mappedPovHats"_qs, mappedPovHats);
    detail.insert(u"profileControlButtons"_qs, profileControlButtons);
    detail.insert(u"directPovOutputs"_qs, 0);
    detail.insert(u"customCurves"_qs, customCurves);
    detail.insert(u"automationCount"_qs, static_cast<int>(automations.size()));
    detail.insert(u"axes"_qs, axes);
    detail.insert(u"buttons"_qs, buttons);
    detail.insert(u"povs"_qs, povs);
    detail.insert(u"curves"_qs, curves);
    detail.insert(u"automations"_qs, automations);
    detail.insert(u"outputName"_qs, layout ? layout->name : u"Output unavailable"_qs);
    detail.insert(u"vjoyDevice"_qs, layout ? layout->requirements.deviceId : 0);
    detail.insert(u"vjoyReady"_qs, layout && m_worker.runtime().vjoyReady.load());
    detail.insert(u"outputAxes"_qs, outputAxes);
    detail.insert(u"unmappedOutputAxes"_qs, std::max(0, outputAxes - mappedAxes));
    detail.insert(u"compatibility"_qs, !physical.connected ? u"Review recommended — no current controller"_qs
        : compatible ? u"Fully compatible"_qs : u"Partial compatibility — review missing controls"_qs);
    detail.insert(u"controllerName"_qs, physical.name);
    detail.insert(u"relationships"_qs, profileRelationships(profileId));
    return detail;
}

QVariantMap AppBackend::profileRelationships(const QString &profileId) const
{
    QVariantMap result;
    QVariantList references;
    QVariantList referencedBy;
    for (int index = 0; index < static_cast<int>(m_configuration.profileTriggers.size()); ++index) {
        const ProfileTriggerBinding &binding = m_configuration.profileTriggers[static_cast<size_t>(index)];
        if (binding.targetProfileId == profileId) {
            referencedBy.append(QVariantMap{{u"profile"_qs, u"Global profile control"_qs},
                {u"via"_qs, QString(u"Button %1 · %2"_qs).arg(index + 1)
                    .arg(profileTriggerModeLabel(binding.mode))}});
        }
    }
    for (int hat = 0; hat < static_cast<int>(m_configuration.povProfileTriggers.size()); ++hat) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ProfileTriggerBinding &binding =
                m_configuration.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (binding.targetProfileId != profileId) continue;
            referencedBy.append(QVariantMap{{u"profile"_qs, u"Global profile control"_qs},
                {u"via"_qs, QString(u"POV %1 · %2 · %3"_qs).arg(hat + 1)
                    .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)))
                    .arg(profileTriggerModeLabel(binding.mode))}});
        }
    }
    for (const AutomationDefinition &automation : m_configuration.automations) {
        bool source = false;
        for (const AutomationConditionDefinition &condition : automation.conditions) source = source || condition.profileId == profileId;
        for (const AutomationActionDefinition &action : automation.actions) {
            if (action.profileId == profileId) {
                referencedBy.append(QVariantMap{{u"profile"_qs, automation.name}, {u"via"_qs, u"Automation target"_qs}});
            } else if (source && !action.profileId.isEmpty()) {
                references.append(QVariantMap{{u"profile"_qs, profileDisplayName(action.profileId)},
                    {u"via"_qs, QString(u"Automation: %1"_qs).arg(automation.name)}});
            }
        }
    }
    result.insert(u"references"_qs, references);
    result.insert(u"referencedBy"_qs, referencedBy);
    return result;
}

bool AppBackend::exportPortableProfile(const QString &profileId, const QString &fileName)
{
    QString error;
    if (!ProfilePortability::exportProfile(m_configuration, profileId, fileName, &error)) {
        m_portableImportStatus = error;
        emit stateChanged();
        return false;
    }
    m_portableImportStatus = u"Profile exported successfully"_qs;
    appendEvent(u"Exported portable profile: "_qs + profileDisplayName(profileId));
    emit stateChanged();
    return true;
}

bool AppBackend::exportPortablePack(const QStringList &categoryIds, const QStringList &profileIds,
                                    const QString &name, const QString &description, bool includeDevices,
                                    bool includeCalibration, bool includeAutomations,
                                    bool includeProfileRelationships, bool includeGameDetection,
                                    const QString &fileName)
{
    QString error;
    if (!ProfilePortability::exportPack(m_configuration, categoryIds, profileIds, name, description,
                                        includeDevices, includeCalibration, includeAutomations,
                                        includeProfileRelationships, includeGameDetection, fileName, &error)) {
        m_portableImportStatus = error;
        emit stateChanged();
        return false;
    }
    m_portableImportStatus = u"Pack exported successfully"_qs;
    appendEvent(u"Exported portable Pack: "_qs + name.trimmed());
    emit stateChanged();
    return true;
}

bool AppBackend::loadPortableImportPreview(const QString &fileName)
{
    auto bundle = std::make_unique<PortableConfigurationBundle>();
    QString error;
    if (!ProfilePortability::inspect(fileName, bundle.get(), &error)) {
        m_pendingPortableImport.reset();
        m_portableImportPreview.clear();
        m_portableImportDeviceSelections.clear();
        m_portableImportStatus = error;
        emit stateChanged();
        return false;
    }
    m_portableImportDeviceSelections.clear();
    m_portableImportPreview = ProfilePortability::preview(*bundle, m_configuration);
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    m_portableImportPreview.insert(u"currentControllerName"_qs, physical.connected
        ? physical.name : u"No current controller"_qs);
    QVariantList profiles = m_portableImportPreview.value(u"profiles"_qs).toList();
    for (QVariant &value : profiles) {
        QVariantMap profile = value.toMap();
        const int mappedAxes = profile.value(u"mappedAxes"_qs).toInt();
        const int mappedButtons = profile.value(u"mappedButtons"_qs).toInt();
        const int mappedPovs = profile.value(u"povMappings"_qs).toInt();
        const int availableAxes = static_cast<int>(std::count(physical.axes.cbegin(), physical.axes.cend(), true));
        profile.insert(u"compatibility"_qs, !physical.connected ? u"REVIEW RECOMMENDED — no current controller"_qs
            : (availableAxes >= mappedAxes && physical.buttons >= mappedButtons
               && physical.povs >= (mappedPovs > 0 ? 1 : 0)) ? u"FULLY COMPATIBLE"_qs
                                                       : u"PARTIAL COMPATIBILITY — review missing controls"_qs);
        value = profile;
    }
    m_portableImportPreview.insert(u"profiles"_qs, profiles);
    m_pendingPortableImport = std::move(bundle);
    m_portableImportStatus = u"Review the import preview before applying changes"_qs;
    emit stateChanged();
    return true;
}

bool AppBackend::applyPortableImport(const QString &destinationCategoryId, bool replaceMatchingProfiles,
                                     const QString &categoryConflictMode,
                                     bool applyImportedCalibration,
                                     const QString &adaptivePresetConflictMode)
{
    if (!m_pendingPortableImport) return false;
    PortableImportOptions options;
    options.destinationCategoryId = destinationCategoryId;
    options.replaceMatchingProfiles = replaceMatchingProfiles;
    const QString mode = categoryConflictMode.trimmed().toCaseFolded();
    if (mode == u"new"_qs || mode == u"importasnew"_qs) {
        options.categoryConflictMode = PortableCategoryConflictMode::ImportAsNew;
    } else if (mode == u"replace"_qs) {
        options.categoryConflictMode = PortableCategoryConflictMode::Replace;
    } else if (mode != u"merge"_qs) {
        m_portableImportStatus = u"Choose Merge, Import as New, or Replace for existing Categories"_qs;
        emit stateChanged();
        return false;
    }
    const QString presetMode = adaptivePresetConflictMode.trimmed().toCaseFolded();
    if (presetMode == u"keep"_qs || presetMode == u"keep-local"_qs) {
        options.adaptiveResponsePresetConflictMode =
            PortableAdaptiveResponsePresetConflictMode::KeepLocal;
    } else if (presetMode == u"replace"_qs) {
        options.adaptiveResponsePresetConflictMode =
            PortableAdaptiveResponsePresetConflictMode::Replace;
    } else if (presetMode == u"copy"_qs || presetMode == u"import-as-copy"_qs) {
        options.adaptiveResponsePresetConflictMode =
            PortableAdaptiveResponsePresetConflictMode::ImportAsCopy;
    } else {
        m_portableImportStatus = u"Choose Keep Local, Import as Copy, or Replace for conflicting Response Presets"_qs;
        emit stateChanged();
        return false;
    }
    options.deviceSelections = m_portableImportDeviceSelections;
    options.applyImportedCalibration = applyImportedCalibration;
    QStringList warnings;
    QString error;
    if (!ProfilePortability::apply(&m_configuration, *m_pendingPortableImport, options, &warnings, &error)) {
        m_portableImportStatus = error;
        emit stateChanged();
        return false;
    }
    persistAndApply();
    m_portableImportStatus = warnings.isEmpty() ? u"Import completed successfully"_qs
                                                 : warnings.join(u"\n"_qs);
    m_pendingPortableImport.reset();
    m_portableImportPreview.clear();
    m_portableImportDeviceSelections.clear();
    appendEvent(u"Imported portable "_qs + (m_portableImportStatus.isEmpty() ? u"configuration"_qs : u"configuration"_qs));
    emit stateChanged();
    return true;
}

bool AppBackend::selectPortableImportDevice(int descriptorIndex, const QString &savedControllerId)
{
    if (!m_pendingPortableImport || descriptorIndex < 0 || savedControllerId.trimmed().isEmpty()) return false;
    QVariantList devices = m_portableImportPreview.value(u"devices"_qs).toList();
    if (descriptorIndex >= devices.size()) return false;
    QVariantMap device = devices.at(descriptorIndex).toMap();
    bool validSelection = false;
    QString selectedName;
    for (const QVariant &choiceValue : device.value(u"choices"_qs).toList()) {
        const QVariantMap choice = choiceValue.toMap();
        if (choice.value(u"id"_qs).toString() == savedControllerId) {
            validSelection = true;
            selectedName = choice.value(u"name"_qs).toString();
            break;
        }
    }
    if (!validSelection) return false;
    m_portableImportDeviceSelections.insert(descriptorIndex, savedControllerId);
    device.insert(u"selectedControllerName"_qs, selectedName);
    device.insert(u"state"_qs, u"USER-SELECTED LOCAL CONTROLLER"_qs);
    devices[descriptorIndex] = device;
    m_portableImportPreview.insert(u"devices"_qs, devices);
    emit stateChanged();
    return true;
}

void AppBackend::beginCalibration()
{
    if (m_calibrationStage != CalibrationStageState::Idle) return;
    m_calibrationSuccess = false;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        CalibrationCaptureAxis &capture = m_calibrationCapture[static_cast<size_t>(index)];
        capture = {};
        capture.available = runtime.axisAvailable[index].load();
        const float raw = runtime.raw[index].load();
        capture.minimum = raw;
        capture.maximum = raw;
    }
    m_calibrationStage = CalibrationStageState::Range;
    m_calibrationStatus = u"STEP 1 OF 2 — Move every control through its complete range several times."_qs;
    appendEvent(u"Calibration range capture started"_qs);
    emit stateChanged();
}

bool AppBackend::beginCalibrationCenterCapture()
{
    if (m_calibrationStage != CalibrationStageState::Range) return false;
    m_calibrationStage = CalibrationStageState::Center;
    m_calibrationStatus = u"STEP 2 OF 2 — Release self-centering controls. Throttles and sliders do not need a center."_qs;
    appendEvent(u"Calibration range captured; ready to capture centered controls"_qs);
    emit stateChanged();
    return true;
}

bool AppBackend::saveCalibration()
{
    if (m_calibrationStage != CalibrationStageState::Center) return false;
    for (CalibrationCaptureAxis &capture : m_calibrationCapture) capture.centerSampleCount = 0;
    m_calibrationStage = CalibrationStageState::Finalizing;
    m_calibrationFinalizationClock.restart();
    m_calibrationStatus = u"Measuring a short, stable center sample. Keep self-centering controls released."_qs;
    appendEvent(u"Calibration center sampling started"_qs);
    emit stateChanged();
    return true;
}

void AppBackend::resetCalibration()
{
    m_calibrationStage = CalibrationStageState::Idle;
    m_calibrationCapture = {};
    m_calibrationSuccess = false;
    for (Calibration &calibration : m_configuration.calibration) calibration = Calibration{};
    m_configuration.axisActivity.fill(PhysicalAxisActivity::Unknown);
    persistAndApply();
    m_calibrationStatus = u"Calibration reset. Raw input is shown until you calibrate again."_qs;
    appendEvent(u"Calibration reset"_qs);
}

void AppBackend::sampleCalibrationControlPlane()
{
    if (m_calibrationStage == CalibrationStageState::Idle) return;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        CalibrationCaptureAxis &capture = m_calibrationCapture[static_cast<size_t>(index)];
        if (!capture.available || !runtime.axisAvailable[index].load()) continue;
        const float raw = runtime.raw[index].load();
        if (m_calibrationStage == CalibrationStageState::Range) {
            capture.minimum = std::min(capture.minimum, raw);
            capture.maximum = std::max(capture.maximum, raw);
        } else if (m_calibrationStage == CalibrationStageState::Finalizing
                   && capture.centerSampleCount < static_cast<int>(capture.centerSamples.size())) {
            capture.centerSamples[static_cast<size_t>(capture.centerSampleCount++)] = raw;
        }
    }
    if (m_calibrationStage == CalibrationStageState::Finalizing
        && m_calibrationFinalizationClock.elapsed() >= 400) {
        finishCalibration();
    }
}

void AppBackend::finishCalibration()
{
    constexpr float kMinimumCenteredMargin = 0.01F;
    constexpr float kMaximumCenterSpread = 0.12F;
    QStringList problems;
    std::array<Calibration, kPhysicalAxisCount> captured = m_configuration.calibration;
    std::array<PhysicalAxisActivity, kPhysicalAxisCount> capturedActivity{};
    bool observedAny = false;
    bool savedAny = false;
    int calibratedAxisCount = 0;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const CalibrationCaptureAxis &capture = m_calibrationCapture[static_cast<size_t>(index)];
        if (!capture.available) continue;
        const PhysicalAxisActivity activity = physicalAxisActivityForObservedSpan(
            capture.minimum, capture.maximum, true);
        if (activity == PhysicalAxisActivity::Fixed) {
            // DirectInput may expose a slot that has no physical control. It
            // is not calibration data and must not block real controls.
            captured[static_cast<size_t>(index)] = Calibration{};
            capturedActivity[static_cast<size_t>(index)] = activity;
            continue;
        }
        capturedActivity[static_cast<size_t>(index)] = activity;
        observedAny = true;
        Calibration calibration;
        calibration.enabled = true;
        calibration.minimum = capture.minimum;
        calibration.maximum = capture.maximum;
        if (capture.centerSampleCount < 10) {
            problems.append(physicalAxisLabel(static_cast<PhysicalAxis>(index)) + u" did not receive enough resting samples"_qs);
            continue;
        }
        const float center = robustCalibrationCenter(capture.centerSamples, capture.centerSampleCount);
        const auto samples = capture.centerSamples;
        const auto bounds = std::minmax_element(samples.cbegin(), samples.cbegin() + capture.centerSampleCount);
        if (*bounds.second - *bounds.first > kMaximumCenterSpread) {
            problems.append(physicalAxisLabel(static_cast<PhysicalAxis>(index)) + u" resting position was too unstable; release it and try again"_qs);
            continue;
        }
        const bool interiorCenter = capture.minimum + kMinimumCenteredMargin < center
            && center < capture.maximum - kMinimumCenteredMargin;
        calibration.centered = interiorCenter;
        if (interiorCenter) {
            calibration.center = center;
        } else {
            // A stable endpoint is a valid physical range-only control. This
            // describes the device itself, independent of profile Range.
            calibration.center = 0.0F;
        }
        captured[static_cast<size_t>(index)] = calibration;
        savedAny = true;
        ++calibratedAxisCount;
    }
    if (!observedAny) problems.append(u"No meaningful axis travel was observed"_qs);
    if (savedAny && problems.isEmpty()) {
        m_configuration.calibration = captured;
        m_configuration.axisActivity = capturedActivity;
        for (ControllerProfile &profile : m_configuration.profiles) {
            for (int index = 0; index < kPhysicalAxisCount; ++index) {
                if (capturedActivity[static_cast<size_t>(index)] == PhysicalAxisActivity::Fixed) {
                    profile.axes[static_cast<size_t>(index)].target = VirtualAxis::Disabled;
                }
            }
        }
        m_calibrationStage = CalibrationStageState::Idle;
        m_calibrationCapture = {};
        m_calibrationSuccess = true;
        appendCalibrationHistory(captured, calibratedAxisCount);
        persistAndApply();
        m_calibrationStatus = u"CALIBRATION SUCCESSFUL — Calibration saved. Centered axes now use measured neutral as 0.0%."_qs;
        appendEvent(u"Two-stage calibration saved"_qs);
    } else {
        m_calibrationStage = CalibrationStageState::Center;
        m_calibrationSuccess = false;
        m_calibrationStatus = QStringLiteral("Calibration needs attention: %1.").arg(problems.join(QStringLiteral("; ")));
        appendEvent(m_calibrationStatus);
    }
    emit stateChanged();
}

void AppBackend::appendCalibrationHistory(
    const std::array<Calibration, kPhysicalAxisCount> &calibration, int calibratedAxisCount)
{
    const DeviceSnapshot snapshot = m_worker.deviceSnapshot();
    const SavedControllerRecord *record = activeControllerRecord();
    CalibrationHistoryEntry entry;
    entry.controllerRecordId = record ? record->id : m_configuration.activeControllerRecordId;
    entry.controllerDisplayName = record ? record->displayName : snapshot.name;
    if (entry.controllerDisplayName.isEmpty()) entry.controllerDisplayName = u"Connected controller"_qs;
    entry.controllerIdentity = record ? (!record->hidInstanceId.isEmpty() ? record->hidInstanceId
                                                                           : record->lastDirectInputId)
                                      : (!snapshot.hidInstanceId.isEmpty() ? snapshot.hidInstanceId : snapshot.id);
    entry.completedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry.applicationVersion = QString::fromLatin1(HOTAS_BF6_VERSION);
    entry.calibratedAxisCount = std::clamp(calibratedAxisCount, 0, kPhysicalAxisCount);
    entry.calibration = calibration;
    m_configuration.calibrationHistory.insert(m_configuration.calibrationHistory.begin(), std::move(entry));
    if (static_cast<int>(m_configuration.calibrationHistory.size()) > kMaximumCalibrationHistoryEntries) {
        m_configuration.calibrationHistory.resize(kMaximumCalibrationHistoryEntries);
    }
}

QVariantList AppBackend::calibrationHistory() const
{
    QVariantList history;
    history.reserve(static_cast<qsizetype>(m_configuration.calibrationHistory.size()));
    for (const CalibrationHistoryEntry &entry : m_configuration.calibrationHistory) {
        const QDateTime utc = QDateTime::fromString(entry.completedAtUtc, Qt::ISODateWithMs);
        const QString localTime = utc.isValid() ? utc.toLocalTime().toString(u"MMM d, yyyy · h:mm AP"_qs)
                                                : entry.completedAtUtc;
        history.append(QVariantMap{{u"controllerRecordId"_qs, entry.controllerRecordId},
            {u"name"_qs, entry.controllerDisplayName}, {u"when"_qs, localTime},
            {u"axes"_qs, entry.calibratedAxisCount},
            {u"currentDevice"_qs, !entry.controllerRecordId.isEmpty()
                && entry.controllerRecordId == m_configuration.activeControllerRecordId}});
    }
    return history;
}

void AppBackend::setStartMappingOnLaunch(bool enabled)
{
    m_configuration.startMappingOnLaunch = enabled;
    persistAndApply();
}

void AppBackend::setVjoyDeviceId(int deviceId)
{
    const int normalized = std::clamp(deviceId, 1, 16);
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        if (layout.id != currentProfile().outputLayoutId
            && layout.requirements.deviceId == normalized) {
            appendEvent(u"Each virtual output layout needs its own vJoy device ID"_qs);
            return;
        }
    }
    if (VirtualOutputLayout *layout = activeOutputLayout()) {
        layout->requirements.deviceId = normalized;
    }
    m_configuration.vjoyDeviceId = normalized;
    persistAndApply();
}

bool AppBackend::assignProfileOutputLayout(const QString &profileId, const QString &layoutId)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
    if (!profile || !layout) return false;
    if (profile->outputLayoutId == layoutId) return true;
    const bool active = profileId == m_configuration.activeProfileId;
    const bool mappingWasRequested = active && m_worker.mappingRequested();
    if (active && !m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not safely release the current virtual output for layout switching"_qs);
        return false;
    }
    if (active) {
        ControllerReadinessService visibility;
        const OutputVisibilitySwitchResult visibilityResult =
            visibility.applyManagedOutputVisibility(m_configuration, layoutId);
        if (!visibilityResult.succeeded) {
            m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
            appendEvent(visibilityResult.status);
            return false;
        }
        appendEvent(visibilityResult.status);
    }
    profile->outputLayoutId = layoutId;
    if (active) synchronizeActiveOutputLayout();
    persistAndApply();
    if (active && !m_worker.restoreAfterDriverConfiguration(mappingWasRequested)) {
        appendEvent(u"Output layout was selected, but mapping could not reacquire the target vJoy device"_qs);
    }
    appendEvent(QString(u"Profile %1 now uses %2"_qs).arg(profile->name, layout->name));
    return true;
}

QString AppBackend::createFiveAxisOutputLayout(const QString &name, int deviceId)
{
    const QString trimmed = name.trimmed().left(64);
    const int normalizedDeviceId = std::clamp(deviceId, 1, 16);
    if (trimmed.isEmpty() || static_cast<int>(m_configuration.outputLayouts.size()) >= 16) return {};
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        if (layout.name.compare(trimmed, Qt::CaseInsensitive) == 0
            || layout.requirements.deviceId == normalizedDeviceId) return {};
    }
    VirtualOutputLayout layout;
    layout.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layout.name = trimmed;
    layout.requirements.deviceId = normalizedDeviceId;
    layout.requirements.buttons = 32;
    for (const VirtualAxis axis : {VirtualAxis::X, VirtualAxis::Y, VirtualAxis::Z,
                                   VirtualAxis::Rz, VirtualAxis::Slider0}) {
        layout.requirements.axes[static_cast<size_t>(axis)] = true;
    }
    const QString id = layout.id;
    m_configuration.outputLayouts.push_back(std::move(layout));
    persistAndApply();
    appendEvent(QString(u"Created 5-axis virtual output %1 on vJoy Device %2; provision it once in vJoy setup before use"_qs)
        .arg(trimmed).arg(normalizedDeviceId));
    return id;
}

bool AppBackend::adoptVirtualOutputVisibility(const QString &layoutId,
                                               const QString &deviceInstanceId)
{
    VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
    if (!layout) return false;

    ControllerReadinessService visibility;
    QString normalizedInstanceId;
    QString status;
    if (!visibility.validateManagedVirtualOutputIdentity(deviceInstanceId, &normalizedInstanceId, &status)) {
        appendEvent(status);
        return false;
    }
    for (const VirtualOutputLayout &other : m_configuration.outputLayouts) {
        if (other.id == layout->id || other.hidHideDeviceInstanceId.isEmpty()) continue;
        if (ControllerReadinessService::normalizeDeviceInstanceId(other.hidHideDeviceInstanceId)
            == normalizedInstanceId) {
            appendEvent(u"That exact vJoy HID instance is already adopted by another virtual output layout"_qs);
            return false;
        }
    }
    layout->hidHideDeviceInstanceId = normalizedInstanceId;
    layout->hidhideManaged = true;
    persistAndApply();
    appendEvent(QString(u"Virtual-output visibility prepared for %1. Normal layout switches use HidHide without administrator elevation; restart a running game if it retains an older device handle."_qs)
        .arg(layout->name));
    return true;
}

void AppBackend::setAutomationEngineEnabled(bool enabled)
{
    if (m_configuration.automationEnabled == enabled) return;
    m_configuration.automationEnabled = enabled;
    persistAndApply();
    appendEvent(enabled ? u"Automation engine enabled"_qs : u"Automation engine disabled"_qs);
}

QString AppBackend::createAutomation()
{
    if (static_cast<int>(m_configuration.automations.size()) >= kMaximumAutomationRules) {
        m_automationValidationMessage = u"Automation limit is 64 rules."_qs;
        emit stateChanged();
        return {};
    }
    AutomationDefinition automation;
    automation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    automation.name = u"New Automation"_qs;
    int suffix = 2;
    const auto nameTaken = [this, &automation] {
        return std::any_of(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
            [&automation](const AutomationDefinition &existing) {
                return existing.name.compare(automation.name, Qt::CaseInsensitive) == 0;
            });
    };
    while (nameTaken()) automation.name = QString(u"New Automation %1"_qs).arg(suffix++);
    // A new rule is an intentionally incomplete, disabled draft. It is
    // persisted so a user can safely leave and resume editing, but cannot
    // enter the compiled runtime set until Save supplies valid conditions and
    // actions. Never invent a default action that can alter flight controls.
    automation.enabled = false;
    m_configuration.automations.push_back(std::move(automation));
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation draft created"_qs);
    return m_configuration.automations.back().id;
}

QString AppBackend::duplicateAutomation(const QString &id)
{
    if (static_cast<int>(m_configuration.automations.size()) >= kMaximumAutomationRules) return {};
    const auto found = std::find_if(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.cend()) return {};
    AutomationDefinition copy = *found;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.enabled = false;
    const QString sourceName = copy.name.trimmed();
    int suffix = 2;
    QString suffixText = u" Copy"_qs;
    do {
        copy.name = sourceName.left(64 - suffixText.size()).trimmed() + suffixText;
        suffixText = QString(u" Copy %1"_qs).arg(suffix++);
    } while (std::any_of(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&copy](const AutomationDefinition &automation) {
            return automation.name.compare(copy.name, Qt::CaseInsensitive) == 0;
        }));
    m_configuration.automations.push_back(std::move(copy));
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation duplicated"_qs);
    return m_configuration.automations.back().id;
}

bool AppBackend::deleteAutomation(const QString &id)
{
    const auto found = std::find_if(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.cend()) return false;
    const QString name = found->name;
    m_configuration.automations.erase(found);
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation deleted: "_qs + name);
    return true;
}

bool AppBackend::setAutomationEnabled(const QString &id, bool enabled)
{
    const auto found = std::find_if(m_configuration.automations.begin(), m_configuration.automations.end(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.end() || found->enabled == enabled) return false;
    if (enabled) {
        MapperConfiguration proposed = m_configuration;
        const auto proposedRule = std::find_if(proposed.automations.begin(), proposed.automations.end(),
            [&id](const AutomationDefinition &automation) { return automation.id == id; });
        proposedRule->enabled = true;
        const RuntimeProfileCache compiled = compileRuntimeProfileCache(proposed);
        const int index = static_cast<int>(std::distance(proposed.automations.begin(), proposedRule));
        if (!compiled.automation || !compiled.automation->publishable
            || compiled.automation->ruleHealth[static_cast<size_t>(index)] == AutomationHealth::Invalid) {
            m_automationValidationMessage = compiled.automation && !compiled.automation->ruleMessages[
                static_cast<size_t>(index)].isEmpty() ? compiled.automation->ruleMessages[static_cast<size_t>(index)]
                : (compiled.automation ? compiled.automation->message : u"Automation validation failed."_qs);
            emit stateChanged();
            return false;
        }
    }
    found->enabled = enabled;
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent((enabled ? u"Automation enabled: "_qs : u"Automation disabled: "_qs) + found->name);
    return true;
}

bool AppBackend::saveAutomation(const QVariantMap &automation)
{
    AutomationDefinition candidate;
    QString reason;
    if (!automationDefinitionFromVariant(automation, &candidate, &reason)) {
        m_automationValidationMessage = reason;
        emit stateChanged();
        return false;
    }
    const auto found = std::find_if(m_configuration.automations.begin(), m_configuration.automations.end(),
        [&candidate](const AutomationDefinition &existing) { return existing.id == candidate.id; });
    if (found == m_configuration.automations.end()) {
        m_automationValidationMessage = u"Automation no longer exists."_qs;
        emit stateChanged();
        return false;
    }
    MapperConfiguration proposed = m_configuration;
    const auto proposedRule = std::find_if(proposed.automations.begin(), proposed.automations.end(),
        [&candidate](const AutomationDefinition &existing) { return existing.id == candidate.id; });
    *proposedRule = candidate;
    const RuntimeProfileCache compiled = compileRuntimeProfileCache(proposed);
    if (!compiled.automation || !compiled.automation->publishable
        || compiled.automation->ruleHealth[static_cast<size_t>(std::distance(
            proposed.automations.begin(), proposedRule))] == AutomationHealth::Invalid) {
        const int index = static_cast<int>(std::distance(proposed.automations.begin(), proposedRule));
        m_automationValidationMessage = compiled.automation && !compiled.automation->ruleMessages[
            static_cast<size_t>(index)].isEmpty() ? compiled.automation->ruleMessages[static_cast<size_t>(index)]
            : (compiled.automation ? compiled.automation->message : u"Automation validation failed."_qs);
        emit stateChanged();
        return false;
    }
    *found = std::move(candidate);
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation saved: "_qs + found->name);
    return true;
}

void AppBackend::setDisabledAxisValue(double percent)
{
    const float normalized = std::isfinite(percent)
        ? sanitizedDisabledAxisValue(static_cast<float>(std::clamp(percent, -100.0, 100.0) / 100.0))
        : 0.0F;
    if (std::abs(m_configuration.disabledAxisValue - normalized) < 0.00001F) return;
    m_configuration.disabledAxisValue = normalized;
    persistAndApply();
    appendEvent(QString(u"Disabled Axis Value set to %1%"_qs)
        .arg(static_cast<double>(normalized) * 100.0, 0, 'f', 1));
}

void AppBackend::setCurveTransitionSmoothingEnabled(bool enabled)
{
    CurveTransitionSmoothingSettings settings = sanitizedCurveTransitionSmoothing(
        m_configuration.curveTransitionSmoothing);
    if (settings.enabled == enabled) return;
    settings.enabled = enabled;
    m_configuration.curveTransitionSmoothing = settings;
    persistAndApply();
    appendEvent(enabled ? u"Curve Transition Smoothing enabled"_qs
                        : u"Curve Transition Smoothing disabled"_qs);
}

void AppBackend::setCurveTransitionDurationMs(int durationMs)
{
    CurveTransitionSmoothingSettings settings = m_configuration.curveTransitionSmoothing;
    settings.durationMs = durationMs;
    settings = sanitizedCurveTransitionSmoothing(settings);
    if (settings.durationMs == curveTransitionDurationMs()) return;
    m_configuration.curveTransitionSmoothing = settings;
    persistAndApply();
    appendEvent(QString(u"Curve Transition Smoothing duration set to %1 ms"_qs)
        .arg(settings.durationMs));
}

bool AppBackend::setProfileCurveTransitionSmoothingOverride(const QString &profileId, bool enabled)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile) return false;
    if (profile->curveTransitionSmoothingOverride == enabled) return true;
    profile->curveTransitionSmoothingOverride = enabled;
    profile->curveTransitionSmoothing = sanitizedCurveTransitionSmoothing(
        profile->curveTransitionSmoothing);
    persistAndApply();
    appendEvent(QString(u"%1 Curve Transition Smoothing override for %2"_qs)
        .arg(enabled ? u"Enabled"_qs : u"Cleared"_qs, profile->name));
    return true;
}

bool AppBackend::setProfileCurveTransitionSmoothingEnabled(const QString &profileId, bool enabled)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile || !profile->curveTransitionSmoothingOverride) return false;
    CurveTransitionSmoothingSettings settings = sanitizedCurveTransitionSmoothing(
        profile->curveTransitionSmoothing);
    if (settings.enabled == enabled) return true;
    settings.enabled = enabled;
    profile->curveTransitionSmoothing = settings;
    persistAndApply();
    appendEvent(QString(u"Curve Transition Smoothing %1 for profile %2"_qs)
        .arg(enabled ? u"enabled"_qs : u"disabled"_qs, profile->name));
    return true;
}

bool AppBackend::setProfileCurveTransitionDurationMs(const QString &profileId, int durationMs)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile || !profile->curveTransitionSmoothingOverride) return false;
    CurveTransitionSmoothingSettings settings = profile->curveTransitionSmoothing;
    settings.durationMs = durationMs;
    settings = sanitizedCurveTransitionSmoothing(settings);
    if (settings.durationMs == sanitizedCurveTransitionSmoothing(
            profile->curveTransitionSmoothing).durationMs) return true;
    profile->curveTransitionSmoothing = settings;
    persistAndApply();
    appendEvent(QString(u"Curve Transition Smoothing duration for %1 set to %2 ms"_qs)
        .arg(profile->name).arg(settings.durationMs));
    return true;
}

void AppBackend::checkForUpdates()
{
    if (m_updateChecking) return;
    m_updateChecking = true;
    m_updateTimedOut = false;
    m_updateCheckFailed = false;
    m_updateStatusText = u"Checking for updates…"_qs;
    appendEvent(u"Update check started"_qs);

    QNetworkRequest request(QUrl(QString::fromUtf8(hotas::launcher::updateManifestUrl().data(),
                                                     static_cast<qsizetype>(hotas::launcher::updateManifestUrl().size()))));
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QString(u"HOTAS-BF6/%1"_qs).arg(QString::fromLatin1(HOTAS_BF6_VERSION)));
    QNetworkReply *reply = m_updateNetworkManager.get(request);
    m_updateReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        finishUpdateCheck(reply);
    });
    m_updateTimeout.start(3000);
    emit stateChanged();
}

void AppBackend::finishUpdateCheck(QNetworkReply *reply)
{
    if (!reply || reply != m_updateReply) {
        if (reply) reply->deleteLater();
        return;
    }
    m_updateTimeout.stop();
    m_updateReply = nullptr;
    m_updateChecking = false;
    const bool timedOut = m_updateTimedOut;
    m_updateTimedOut = false;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray manifestJson = networkError == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
    const QString networkReason = timedOut ? u"request timed out"_qs : reply->errorString();
    reply->deleteLater();

    if (timedOut || networkError != QNetworkReply::NoError) {
        failUpdateCheck(networkReason);
        return;
    }
    if (manifestJson.isEmpty() || manifestJson.size() > 64 * 1024) {
        failUpdateCheck(u"release metadata was empty or too large"_qs);
        return;
    }

    hotas::launcher::SemanticVersion localVersion{};
    std::string reason;
    if (!hotas::launcher::parseSemanticVersion(HOTAS_BF6_VERSION, localVersion, &reason)) {
        failUpdateCheck(u"installed application version is invalid"_qs);
        return;
    }
    hotas::launcher::UpdateManifest manifest;
    const std::string response(manifestJson.constData(), static_cast<size_t>(manifestJson.size()));
    const hotas::launcher::UpdateAction action = hotas::launcher::decideUpdate(
        true, response, localVersion, &manifest, &reason);
    if (action == hotas::launcher::UpdateAction::InstallUpdate) {
        m_updateAvailable = true;
        m_updateCheckFailed = false;
        m_updateAvailableVersion = QString(u"v%1"_qs).arg(QString::fromStdString(manifest.versionText));
        m_updateStatusText = QString(u"%1 is available"_qs).arg(m_updateAvailableVersion);
        appendEvent(QString(u"Update available: %1"_qs).arg(m_updateAvailableVersion));
    } else if (reason == "installed version is current or newer") {
        m_updateAvailable = false;
        m_updateCheckFailed = false;
        m_updateAvailableVersion.clear();
        m_updateStatusText = u"You're running the latest version."_qs;
        appendEvent(u"Update check completed: application is current"_qs);
    } else {
        failUpdateCheck(QString::fromStdString(reason));
        return;
    }
    emit stateChanged();
}

void AppBackend::failUpdateCheck(const QString &reason)
{
    m_updateChecking = false;
    m_updateAvailable = false;
    m_updateCheckFailed = true;
    m_updateAvailableVersion.clear();
    m_updateStatusText = u"Update status unavailable"_qs;
    appendEvent(QString(u"Update check error: %1"_qs).arg(reason));
    emit stateChanged();
}

bool AppBackend::handoffToLauncher()
{
    if (!m_updateAvailable) return false;
    // Ensure the existing QSettings record is durable before a separate
    // launcher process takes over. A launcher start is confirmed first; only
    // then do we begin stopping controller I/O and exit this application.
    ConfigStore::save(m_configuration);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QString launcherPath = QDir(applicationDirectory).filePath(u"HOTAS BF6 Launcher.exe"_qs);
    if (!QFileInfo(launcherPath).isExecutable()) {
        m_updateStatusText = u"Update available, but the HOTAS BF6 Launcher was not found."_qs;
        appendEvent(QString(u"Launcher-start failure: %1"_qs).arg(launcherPath));
        emit stateChanged();
        return false;
    }
    const QStringList arguments{u"--wait-for-pid"_qs,
                                QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(launcherPath, arguments, applicationDirectory)) {
        m_updateStatusText = u"Update available, but the launcher could not be started."_qs;
        appendEvent(QString(u"Launcher-start failure: %1"_qs).arg(launcherPath));
        emit stateChanged();
        return false;
    }
    appendEvent(QString(u"Launcher started for update handoff: %1"_qs).arg(launcherPath));
    m_worker.setMappingEnabled(false);
    QTimer::singleShot(100, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
    return true;
}

bool AppBackend::openVjoyConfiguration()
{
    if (!m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not release vJoy for manual configuration; Mapping remains Off"_qs);
        return false;
    }
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
        u"C:/Program Files"_qs,
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString executable = QDir(root).filePath(u"vJoy/x64/vJoyConf.exe"_qs);
        if (QFileInfo(executable).isExecutable() && QProcess::startDetached(executable)) {
            appendEvent(u"Mapping stopped; opened the supported vJoy configuration utility"_qs);
            return true;
        }
    }
    appendEvent(u"vJoy configuration utility was not found"_qs);
    return false;
}

void AppBackend::refreshHidHideStatus()
{
    startQuickVerification();
    appendEvent(hidhideAvailable()
        ? u"HidHide status refreshed"_qs
        : u"HidHide is not installed or its service is unavailable"_qs);
}

bool AppBackend::repairHidHideAccess()
{
    const bool repaired = m_readiness.allowlistMapperOnly();
    appendEvent(repaired ? u"HOTAS BF6 HidHide self-access was repaired; re-enumerating controllers"_qs
                         : u"HidHide self-access repair was unavailable or not approved"_qs);
    if (repaired) {
        m_worker.requestPhysicalControllerSelection();
        QTimer::singleShot(250, this, &AppBackend::refreshControllerInventory);
    }
    emit stateChanged();
    return repaired;
}

bool AppBackend::openHidHideConfiguration()
{
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
        u"C:/Program Files"_qs,
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString executable = QDir(root).filePath(
            u"Nefarius Software Solutions/HidHide/x64/HidHideClient.exe"_qs);
        if (QFileInfo(executable).isExecutable() && QProcess::startDetached(executable)) {
            appendEvent(u"Opened the HidHide Configuration Client"_qs);
            return true;
        }
    }
    appendEvent(u"HidHide Configuration Client was not found"_qs);
    return false;
}

void AppBackend::inspectControllerReadiness()
{
    verifyHotasSetup();
}

void AppBackend::verifyHotasSetup()
{
    startVerification(VerificationMode::Full);
}

void AppBackend::startQuickVerification()
{
    startVerification(VerificationMode::Quick);
}

void AppBackend::startVerification(VerificationMode mode)
{
    if (m_verificationInProgress) return;

    const MapperConfiguration configuration = m_configuration;
    const bool mappingWasRequested = m_worker.mappingRequested();
    const bool mapperOwnsVjoy = m_worker.runtime().mappingActive.load();
    const bool outputReportsSucceeding = mapperOwnsVjoy && m_worker.runtime().vjoyReady.load();
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    const QString arrivalId = mode == VerificationMode::Quick ? m_pendingControllerArrivalId : QString{};

    m_verificationInProgress = true;
    m_readiness.adoptPlan(ControllerReadinessService::checkingPlan(physical, mode));
    emit stateChanged();
    appendEvent(mode == VerificationMode::Full
        ? u"Full controller verification started"_qs
        : u"Quick controller verification started"_qs);

    QThread *thread = QThread::create([this, configuration, physical, mode, mappingWasRequested,
                                       mapperOwnsVjoy, outputReportsSucceeding, arrivalId] {
        ControllerReadinessPlan plan;
        bool prepared = true;
        bool restored = true;
        if (mode == VerificationMode::Full) {
            prepared = m_worker.prepareForDriverConfiguration();
        }

        if (prepared) {
            ControllerReadinessService verifier;
            plan = verifier.inspect(configuration, physical, mode,
                                    mode == VerificationMode::Quick && mapperOwnsVjoy,
                                    mode == VerificationMode::Quick && outputReportsSucceeding);
        } else {
            plan = ControllerReadinessService::checkingPlan(physical, mode);
            plan.state = ControllerReadinessState::Failed;
            plan.isChecking = false;
            plan.vjoyStatus = VerificationSubsystemState::Error;
            plan.vjoySummary = QStringLiteral("HOTAS BF6 could not safely release vJoy Device 1 for verification.");
            plan.status = QStringLiteral("ACTION REQUIRED — Full verification could not safely prepare vJoy Device 1.");
            plan.lastChecked = QDateTime::currentDateTime();
        }

        if (mode == VerificationMode::Full) {
            restored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
            if (prepared && restored && mappingWasRequested && m_worker.runtime().mappingActive.load()) {
                plan.vjoy.ownedByHotasBf6 = true;
                plan.vjoy.outputReportsSucceeding = m_worker.runtime().vjoyReady.load();
                plan = ControllerReadinessService::planFor(plan.physical, plan.requirements,
                                                           plan.vjoy, plan.hidhide, mode);
            } else if (!restored) {
                plan.state = ControllerReadinessState::Failed;
                plan.vjoyStatus = VerificationSubsystemState::Error;
                plan.vjoySummary = QStringLiteral("Verification completed, but HOTAS BF6 could not restore vJoy ownership.");
                plan.status = QStringLiteral("ACTION REQUIRED — Mapping did not resume after verification.");
            }
        }

        QMetaObject::invokeMethod(this, [this, plan = std::move(plan), mode, restored, arrivalId] () mutable {
            m_readiness.adoptPlan(std::move(plan));
            if (m_readiness.hasPendingRecovery()) {
                // A process restart between the privileged change and fresh
                // DirectInput proof must stay visible even when the current
                // device inspection happens to look healthy. The retained
                // journal exposes only this app's own reversible entries.
                ControllerReadinessPlan pending = m_readiness.plan();
                pending.state = ControllerReadinessState::Attention;
                pending.isChecking = false;
                pending.physicalStatus = VerificationSubsystemState::Attention;
                pending.hidhideStatus = VerificationSubsystemState::Attention;
                pending.physicalSummary = QStringLiteral("A prior automatic setup did not complete fresh physical-controller verification.");
                pending.hidhideSummary = QStringLiteral("A narrow recovery record is available. Use Undo Automatic Repair after confirming the selected controller is connected.");
                pending.status = QStringLiteral("RECOVERY PENDING — Review the previous automatic setup and Undo Automatic Repair if needed.");
                pending.lastChecked = QDateTime::currentDateTime();
                m_readiness.adoptPlan(std::move(pending));
            }
            m_verificationInProgress = false;
            appendEvent(restored
                ? QString(u"Controller verification complete: %1"_qs).arg(m_readiness.plan().status)
                : u"Controller verification complete, but mapping restoration failed"_qs);
            if (mode == VerificationMode::Full && restored
                && m_readiness.plan().state == ControllerReadinessState::Ready
                && currentPhysicalCapabilities().connected) {
                rememberCurrentController();
            }
            if (mode == VerificationMode::Quick && !arrivalId.isEmpty()
                && arrivalId == m_pendingControllerArrivalId) {
                m_pendingControllerArrivalId.clear();
                const PhysicalControllerCapabilities current = currentPhysicalCapabilities();
                const bool known = std::any_of(m_configuration.savedControllers.cbegin(),
                    m_configuration.savedControllers.cend(), [&current](const SavedControllerRecord &record) {
                        return (!current.hidInstanceId.isEmpty()
                                && record.hidInstanceId.compare(current.hidInstanceId, Qt::CaseInsensitive) == 0)
                            || (!current.directInputId.isEmpty()
                                && record.lastDirectInputId.compare(current.directInputId, Qt::CaseInsensitive) == 0);
                    });
                const bool actionable = ControllerReadinessService::needsSetupAfterControllerArrival(
                    true, m_readiness.plan());
                if (current.connected && current.directInputId == arrivalId
                    && (!known || actionable || calibrationNeedsSetup(current) || m_readiness.hasPendingRecovery())) {
                    appendEvent(known ? u"Controller setup needs attention after controller arrival"_qs
                                      : u"New controller detected; Verify Setup is required"_qs);
                    emit controllerSetupRequested({arrivalId});
                }
            }
            emit stateChanged();
        }, Qt::QueuedConnection);
    });
    m_verificationThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_verificationThread == thread) m_verificationThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

bool AppBackend::calibrationNeedsSetup(const PhysicalControllerCapabilities &physical) const
{
    if (!physical.connected || m_configuration.preferredDeviceId != physical.directInputId) return true;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!physical.axes[static_cast<size_t>(index)]) continue;
        if (currentProfile().axes[static_cast<size_t>(index)].rangeMode == AxisRangeMode::Centered
            && !m_configuration.calibration[static_cast<size_t>(index)].enabled) {
            return true;
        }
    }
    return false;
}

bool AppBackend::applyControllerReadiness()
{
    if (m_verificationInProgress) return false;
    // The worker owns vJoy while mapping normally. Release it before the
    // privileged control-plane transaction, then restore the prior user choice.
    const bool mappingWasRequested = m_worker.mappingRequested();
    const MapperConfiguration configuration = m_configuration;
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    ControllerReadinessPlan waiting = m_readiness.plan();
    waiting.state = ControllerReadinessState::AwaitingPermission;
    waiting.isChecking = true;
    waiting.status = QStringLiteral("WAITING FOR ADMINISTRATOR APPROVAL — Preparing the approved repair transaction.");
    m_readiness.adoptPlan(std::move(waiting));
    m_verificationInProgress = true;
    appendEvent(u"Automatic controller repair requested"_qs);
    appendEvent(u"Repair plan will preserve unrelated HidHide rules and the previous Mapping state"_qs);
    emit stateChanged();

    auto repair = std::make_shared<ControllerReadinessService>();
    QThread *thread = QThread::create([this, repair, configuration, physical, mappingWasRequested] {
        bool prepared = m_worker.prepareForDriverConfiguration();
        bool completed = false;
        bool physicalReacquired = false;
        bool recoveryAttempted = false;
        bool recoverySucceeded = false;
        bool recoveredPhysicalReports = false;
        if (prepared) {
            repair->inspect(configuration, physical, VerificationMode::Full);
            if (repair->plan().canApplyAutomatically) {
                QMetaObject::invokeMethod(this, [this] {
                    ControllerReadinessPlan applying = m_readiness.plan();
                    applying.state = ControllerReadinessState::Applying;
                    applying.status = QStringLiteral("APPLYING HIDHIDE CONFIGURATION — Waiting for the approved administrator repair transaction.");
                    m_readiness.adoptPlan(std::move(applying));
                    appendEvent(u"Administrator repair helper started"_qs);
                    emit stateChanged();
                }, Qt::QueuedConnection);
                completed = repair->applyAutomatically();
            }
        }

        if (completed) {
            // This is the safety invariant v1.9.2 was missing. Config read-
            // back cannot prove HidHide still lets this process open the
            // selected device, so force a fresh DirectInput acquisition first.
            physicalReacquired = m_worker.reacquirePhysicalController(physical.hidInstanceId);
            if (!physicalReacquired) {
                recoveryAttempted = true;
                recoverySucceeded = repair->recoverFromPhysicalAccessFailure();
                if (recoverySucceeded) {
                    recoveredPhysicalReports = m_worker.reacquirePhysicalController(physical.hidInstanceId);
                }
                repair->completePhysicalAccessVerification(false, false, recoveryAttempted,
                                                           recoverySucceeded, recoveredPhysicalReports);
            } else if (repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Ready) {
                repair->completePhysicalAccessVerification(true, true);
            }
        }

        bool restored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
        if (!prepared) {
            ControllerReadinessPlan failed = ControllerReadinessService::checkingPlan(physical, VerificationMode::Full);
            failed.state = ControllerReadinessState::Failed;
            failed.isChecking = false;
            failed.vjoyStatus = VerificationSubsystemState::Error;
            failed.vjoySummary = QStringLiteral("HOTAS BF6 could not safely release vJoy Device 1 for repair.");
            failed.status = QStringLiteral("REPAIR FAILED — HOTAS BF6 could not safely prepare vJoy Device 1.");
            failed.lastChecked = QDateTime::currentDateTime();
            repair->adoptPlan(std::move(failed));
        } else if (!repair->plan().canApplyAutomatically && !completed
                   && repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::None) {
            // The repair plan was rebuilt immediately before elevation; a
            // changed device state means no privileged action was performed.
            repair->adoptPlan(repair->plan());
        }
        if (!restored) {
            ControllerReadinessPlan failed = repair->plan();
            failed.state = ControllerReadinessState::Failed;
            failed.isChecking = false;
            failed.vjoyStatus = VerificationSubsystemState::Error;
            failed.vjoySummary = QStringLiteral("Repair completed, but HOTAS BF6 could not restore vJoy ownership.");
            failed.status = QStringLiteral("REPAIR COMPLETED, BUT MAPPING COULD NOT BE RESTORED.");
            repair->adoptPlan(std::move(failed));
        } else if (completed && mappingWasRequested && m_worker.runtime().mappingActive.load()) {
            ControllerReadinessPlan restoredPlan = repair->plan();
            const QString repairStatus = restoredPlan.status;
            const ControllerReadinessState repairState = restoredPlan.state;
            const VerificationSubsystemState repairPhysicalStatus = restoredPlan.physicalStatus;
            const QString repairPhysicalSummary = restoredPlan.physicalSummary;
            const VerificationSubsystemState repairHidHideStatus = restoredPlan.hidhideStatus;
            const QString repairHidHideSummary = restoredPlan.hidhideSummary;
            restoredPlan.vjoy.ownedByHotasBf6 = true;
            restoredPlan.vjoy.outputReportsSucceeding = m_worker.runtime().vjoyReady.load();
            restoredPlan = ControllerReadinessService::planFor(restoredPlan.physical, restoredPlan.requirements,
                                                               restoredPlan.vjoy, restoredPlan.hidhide,
                                                               VerificationMode::Full);
            if (repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Attention
                || repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Failed) {
                restoredPlan.state = repairState;
                restoredPlan.status = repairStatus;
                restoredPlan.physicalStatus = repairPhysicalStatus;
                restoredPlan.physicalSummary = repairPhysicalSummary;
                restoredPlan.hidhideStatus = repairHidHideStatus;
                restoredPlan.hidhideSummary = repairHidHideSummary;
            } else if (repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Ready) {
                restoredPlan.status = repairStatus;
            }
            repair->adoptPlan(std::move(restoredPlan));
        }

        QMetaObject::invokeMethod(this, [this, repair, completed, restored, physicalReacquired,
                                         recoveryAttempted, recoverySucceeded, recoveredPhysicalReports] {
            m_readiness = std::move(*repair);
            m_verificationInProgress = false;
            if (completed && restored && physicalReacquired
                && m_readiness.plan().state == ControllerReadinessState::Ready
                && currentPhysicalCapabilities().connected) {
                // A completed automatic repair is a full verification result;
                // commit the controller now rather than requiring Verify Again.
                rememberCurrentController();
            }
            if (!restored) {
                appendEvent(u"Automatic controller repair completed, but mapping restoration failed"_qs);
            } else if (!completed && m_readiness.lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::None) {
                appendEvent(u"Controller setup requires a manual repair; no changes were applied"_qs);
            } else {
                appendEvent(m_readiness.plan().status);
                if (recoveryAttempted) {
                    appendEvent(recoverySucceeded && recoveredPhysicalReports
                        ? u"Automatic HidHide setup was reverted and physical reports resumed"_qs
                        : u"Automatic HidHide recovery needs a physical reconnect; diagnostics are available"_qs);
                } else if (completed && !physicalReacquired) {
                    appendEvent(u"Physical controller reacquisition did not complete; diagnostics are available"_qs);
                }
                for (const AutomaticRepairOperationResult &operation : m_readiness.lastAutomaticRepairResult().operations) {
                    if (!operation.succeeded && !operation.rollback) {
                        appendEvent(QString(u"Repair operation failed: %1 (exit code %2)"_qs)
                            .arg(operation.operationName).arg(operation.exitCode));
                    }
                }
            }
            emit stateChanged();
        }, Qt::QueuedConnection);
    });
    m_verificationThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_verificationThread == thread) m_verificationThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
    return true;
}

bool AppBackend::undoControllerReadiness()
{
    const bool mappingWasRequested = m_worker.mappingRequested();
    const QString expectedHidInstanceId = currentPhysicalCapabilities().hidInstanceId;
    if (!m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not release vJoy for controller setup rollback"_qs);
        emit stateChanged();
        return false;
    }
    const bool restored = m_readiness.undoLastAutomaticSetup();
    const bool physicalRestored = restored && m_worker.reacquirePhysicalController(expectedHidInstanceId);
    const bool mappingStateRestored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
    ControllerReadinessPlan plan = m_readiness.plan();
    if (restored && physicalRestored) {
        plan.physicalStatus = VerificationSubsystemState::Ready;
        plan.physicalSummary = QStringLiteral("Physical controller reacquired and live reports confirmed after undo.");
        plan.status = QStringLiteral("Automatic controller repair was undone; physical input was verified.");
        m_readiness.adoptPlan(std::move(plan));
    } else if (restored) {
        plan.state = ControllerReadinessState::Attention;
        plan.physicalStatus = VerificationSubsystemState::Error;
        plan.physicalSummary = QStringLiteral("Undo commands completed, but HOTAS BF6 has not reacquired physical reports.");
        plan.status = QStringLiteral("UNDO VERIFICATION INCOMPLETE — Reconnect your controller, then use Verify Again or Copy Diagnostics.");
        m_readiness.adoptPlan(std::move(plan));
    }
    appendEvent(restored && physicalRestored && mappingStateRestored
        ? u"Automatic controller repair was undone; physical input and previous mapping state restored"_qs
        : mappingStateRestored ? m_readiness.plan().status
                               : u"Controller setup rollback completed, but mapping restoration failed"_qs);
    emit stateChanged();
    return restored && physicalRestored && mappingStateRestored;
}

ControllerDiagnosticsSnapshot AppBackend::controllerDiagnosticsSnapshot() const
{
    ControllerDiagnosticsSnapshot diagnostics;
    diagnostics.version = QString::fromLatin1(HOTAS_BF6_VERSION);
    diagnostics.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    diagnostics.windowsVersion = QSysInfo::prettyProductName();
    diagnostics.physical = currentPhysicalCapabilities();
    diagnostics.vjoy = m_readiness.plan().vjoy;
    diagnostics.hidhide = m_readiness.plan().hidhide;
    diagnostics.repair = m_readiness.lastAutomaticRepairResult();
    diagnostics.activeProfileName = currentProfile().name;
    diagnostics.privatePaths = {QDir::homePath(), QCoreApplication::applicationDirPath()};
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const Calibration &calibration = m_configuration.calibration[static_cast<size_t>(index)];
        diagnostics.axes.append({physicalAxisLabel(static_cast<PhysicalAxis>(index)), calibration.minimum,
            calibration.center, calibration.maximum, runtime.normalized[index].load(),
            runtime.transformed[index].load(), m_configuration.axisActivity[static_cast<size_t>(index)]});
    }
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        QStringList axes;
        for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
            if (layout.requirements.axes[static_cast<size_t>(index)]) {
                axes.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
            }
        }
        const QString normalized = ControllerReadinessService::normalizeDeviceInstanceId(
            layout.hidHideDeviceInstanceId);
        const bool hidden = layout.hidhideManaged && std::any_of(
            diagnostics.hidhide.hiddenDeviceInstanceIds.cbegin(),
            diagnostics.hidhide.hiddenDeviceInstanceIds.cend(), [&normalized](const QString &entry) {
                return !normalized.isEmpty()
                    && ControllerReadinessService::normalizeDeviceInstanceId(entry) == normalized;
            });
        diagnostics.virtualOutputs.append({layout.name, axes.join(u" · "_qs), layout.requirements.deviceId,
            layout.id == currentProfile().outputLayoutId, layout.hidhideManaged, hidden});
    }
    diagnostics.selectedHidInstance = diagnostics.physical.hidInstanceId;
    return diagnostics;
}

bool AppBackend::copyControllerDiagnostics()
{
    if (!controllerDiagnosticsAvailable()
        || !copyControllerDiagnosticsToClipboard(controllerDiagnosticsSnapshot())) return false;
    appendEvent(u"Controller setup diagnostics copied to the clipboard"_qs);
    emit stateChanged();
    return true;
}

void AppBackend::acknowledgeControllerSetup()
{
    if (!m_controllerSetupSuggested) return;
    QSettings settings;
    settings.setValue(u"readiness/controllerSetupIntroSeen"_qs, true);
    m_controllerSetupSuggested = false;
    emit stateChanged();
}

void AppBackend::useConnectedDevice()
{
    const DiscoveredController *controller = discoveredController(deviceId());
    if (!controller) {
        appendEvent(u"Connect a controller before selecting it"_qs);
        return;
    }
    const ControllerMatch match = ControllerManager::match(*controller, m_configuration.savedControllers);
    if (!match.recordId.isEmpty() && !match.ambiguous) {
        setActiveController(match.recordId);
        return;
    }
    m_configuration.preferredDeviceId = controller->directInputId;
    persistAndApply();
    appendEvent(u"Connected controller selected; complete Verify Setup to remember it"_qs);
}

void AppBackend::refreshControllers()
{
    refreshControllerInventory();
}

const DiscoveredController *AppBackend::discoveredController(const QString &directInputId) const
{
    const auto found = std::find_if(m_discoveredControllers.cbegin(), m_discoveredControllers.cend(),
        [&directInputId](const DiscoveredController &controller) {
            return controller.directInputId.compare(directInputId, Qt::CaseInsensitive) == 0;
        });
    return found == m_discoveredControllers.cend() ? nullptr : &*found;
}

SavedControllerRecord *AppBackend::activeControllerRecord()
{
    const auto found = std::find_if(m_configuration.savedControllers.begin(), m_configuration.savedControllers.end(),
        [this](const SavedControllerRecord &record) { return record.id == m_configuration.activeControllerRecordId; });
    return found == m_configuration.savedControllers.end() ? nullptr : &*found;
}

const SavedControllerRecord *AppBackend::activeControllerRecord() const
{
    const auto found = std::find_if(m_configuration.savedControllers.cbegin(), m_configuration.savedControllers.cend(),
        [this](const SavedControllerRecord &record) { return record.id == m_configuration.activeControllerRecordId; });
    return found == m_configuration.savedControllers.cend() ? nullptr : &*found;
}

ControllerVJoyRequirements AppBackend::currentVjoyRequirements() const
{
    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(m_configuration);
    ControllerVJoyRequirements result;
    result.axes = requirements.axes;
    result.buttons = std::max(requirements.buttons,
        std::clamp(buttonCount(), 0, kMaximumVirtualButtons));
    result.continuousPovs = requirements.continuousPovs;
    result.discretePovs = requirements.discretePovs;
    result.deviceId = m_configuration.vjoyDeviceId;
    return result;
}

void AppBackend::rememberCurrentController()
{
    const DiscoveredController *controller = discoveredController(deviceId());
    if (!controller || controller->virtualDevice) return;
    const ControllerMatch match = ControllerManager::match(*controller, m_configuration.savedControllers);
    const QString existingId = match.ambiguous ? QString{} : match.recordId;
    ControllerVJoyRequirements verifiedRequirements = currentVjoyRequirements();
    const ControllerReadinessPlan &verifiedPlan = m_readiness.plan();
    if (verifiedPlan.state == ControllerReadinessState::Ready
        && verifiedPlan.physical.directInputId == controller->directInputId) {
        verifiedRequirements.axes = verifiedPlan.requirements.axes;
        verifiedRequirements.buttons = verifiedPlan.requirements.buttons;
        verifiedRequirements.continuousPovs = verifiedPlan.requirements.continuousPovs;
        verifiedRequirements.discretePovs = verifiedPlan.requirements.discretePovs;
    }
    verifiedRequirements.deviceId = m_configuration.vjoyDeviceId;
    SavedControllerRecord record = ControllerManager::verifiedRecord(*controller, m_configuration.calibration,
                                                                      verifiedRequirements, existingId);
    record.axisActivity = m_configuration.axisActivity;
    if (!existingId.isEmpty()) {
        for (SavedControllerRecord &existing : m_configuration.savedControllers) {
            if (existing.id != existingId) continue;
            record.ownedHidHideDeviceInstances = existing.ownedHidHideDeviceInstances;
            existing = std::move(record);
            m_configuration.activeControllerRecordId = existingId;
            break;
        }
    } else {
        m_configuration.savedControllers.push_back(std::move(record));
        m_configuration.activeControllerRecordId = m_configuration.savedControllers.back().id;
    }
    m_configuration.preferredDeviceId = controller->directInputId;
    ConfigStore::save(m_configuration);
    m_worker.updateConfiguration(m_configuration);
    rebuildControllerUiModel();
    appendEvent(QString(u"Verified controller remembered: %1"_qs).arg(controller->name));
}

bool AppBackend::setActiveController(const QString &recordId)
{
    const auto record = std::find_if(m_configuration.savedControllers.cbegin(), m_configuration.savedControllers.cend(),
        [&recordId](const SavedControllerRecord &candidate) { return candidate.id == recordId; });
    if (record == m_configuration.savedControllers.cend()) return false;
    const DiscoveredController *target = nullptr;
    for (const DiscoveredController &controller : m_discoveredControllers) {
        const ControllerMatch match = ControllerManager::match(controller, m_configuration.savedControllers);
        if (!match.ambiguous && match.recordId == recordId) {
            target = &controller;
            break;
        }
    }
    if (!target || !target->connected || m_verificationInProgress || m_controllerSelectionInProgress) {
        appendEvent(u"Selected controller is offline; connect it before making it active"_qs);
        return false;
    }
    const SavedControllerRecord targetRecord = *record;
    const DiscoveredController selectedTarget = *target;
    const MapperConfiguration previousConfiguration = m_configuration;
    MapperConfiguration targetConfiguration = m_configuration;
    targetConfiguration.activeControllerRecordId = recordId;
    targetConfiguration.preferredDeviceId = selectedTarget.directInputId;
    targetConfiguration.calibration = targetRecord.calibration;
    targetConfiguration.axisActivity = targetRecord.axisActivity;
    PhysicalControllerCapabilities targetPhysical;
    targetPhysical.name = selectedTarget.name;
    targetPhysical.directInputId = selectedTarget.directInputId;
    targetPhysical.hidInstanceId = selectedTarget.hidInstanceId;
    targetPhysical.connected = selectedTarget.connected;
    targetPhysical.axes = selectedTarget.axes;
    targetPhysical.buttons = selectedTarget.buttonCount;
    targetPhysical.povs = selectedTarget.povCount;
    // Output selection belongs to the active profile, not to a physical
    // controller record. A controller change therefore cannot silently move a
    // game-facing virtual descriptor to a stale saved device ID.
    const MapperOutputRequirements targetRequirements =
        ControllerReadinessService::requirementsFor(targetConfiguration);
    const bool mappingWasRequested = m_worker.mappingRequested();
    m_verificationInProgress = true;
    m_readiness.adoptPlan(ControllerReadinessService::checkingPlan(targetPhysical, VerificationMode::Full));
    appendEvent(QString(u"Switching active controller to %1; validating the selected profile output layout"_qs)
        .arg(selectedTarget.name));
    emit stateChanged();

    QThread *thread = QThread::create([this, previousConfiguration, targetConfiguration, selectedTarget,
                                        targetPhysical, targetRequirements, mappingWasRequested] {
        ControllerReadinessService verifier;
        ControllerReadinessPlan plan = ControllerReadinessService::checkingPlan(targetPhysical, VerificationMode::Full);
        bool prepared = m_worker.prepareForDriverConfiguration();
        bool outputValid = false;
        bool reusedExistingVjoy = false;
        bool selected = false;
        bool restored = false;
        if (prepared) {
            verifier.inspectForRequirements(targetConfiguration, targetPhysical, targetRequirements);
            plan = verifier.plan();
            reusedExistingVjoy = !plan.vjoyNeedsChanges;
            outputValid = reusedExistingVjoy || verifier.applyVJoyConfiguration();
            plan = verifier.plan();
            if (outputValid) {
                // The new configuration is compiled before the worker may
                // acquire the target device. This remains a control-plane
                // configuration boundary, never a per-report lookup.
                m_worker.updateConfiguration(targetConfiguration);
                selected = m_worker.selectPhysicalController(selectedTarget.directInputId);
                restored = selected && m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
            }
        }
        if (!prepared || !outputValid || !selected || !restored) {
            m_worker.updateConfiguration(previousConfiguration);
            m_worker.requestPhysicalControllerSelection();
            if (prepared) m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
        }
        QMetaObject::invokeMethod(this, [this, plan = std::move(plan), targetConfiguration, selectedTarget,
                                         prepared, outputValid, reusedExistingVjoy, selected, restored] () mutable {
            m_readiness.adoptPlan(std::move(plan));
            m_verificationInProgress = false;
            if (prepared && outputValid && selected && restored) {
                m_configuration = targetConfiguration;
                ConfigStore::save(m_configuration);
                rebuildSelectedAxisCurve();
                rebuildControllerUiModel();
                emit selectedAxisCurveChanged();
                appendEvent(reusedExistingVjoy
                    ? QString(u"Active controller switched to %1; the selected vJoy descriptor matched exactly"_qs).arg(selectedTarget.name)
                    : QString(u"Active controller switched to %1; vJoy was configured and verified before mapping resumed"_qs).arg(selectedTarget.name));
            } else {
                appendEvent(QString(u"Active controller switch to %1 was not completed; prior mapping configuration was restored"_qs)
                    .arg(selectedTarget.name));
            }
            emit stateChanged();
        }, Qt::QueuedConnection);
    });
    m_verificationThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_verificationThread == thread) m_verificationThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
    return true;
}

bool AppBackend::selectNewController(const QString &directInputId)
{
    const DiscoveredController *target = discoveredController(directInputId);
    if (!target || !target->connected || target->virtualDevice) return false;
    const ControllerMatch match = ControllerManager::match(*target, m_configuration.savedControllers);
    if (!match.recordId.isEmpty() && !match.ambiguous) return setActiveController(match.recordId);
    m_configuration.activeControllerRecordId.clear();
    m_configuration.preferredDeviceId = target->directInputId;
    persistAndApply();
    rebuildControllerUiModel();
    appendEvent(QString(u"Selected new controller for explicit verification: %1"_qs).arg(target->name));
    startExplicitNewControllerVerification(target->directInputId, target->name);
    return true;
}

void AppBackend::startExplicitNewControllerVerification(const QString &directInputId,
                                                        const QString &displayName)
{
    if (m_verificationInProgress || m_controllerSelectionInProgress) return;
    m_controllerSelectionInProgress = true;
    emit stateChanged();
    QThread *thread = QThread::create([this, directInputId, displayName] {
        // A user selected SET UP, so acquire that exact device and wait for a
        // fresh report before beginning full verification. This never relies
        // on a global disconnected-to-connected edge.
        const bool selected = m_worker.selectPhysicalController(directInputId);
        QMetaObject::invokeMethod(this, [this, selected, displayName] {
            m_controllerSelectionInProgress = false;
            if (selected) {
                appendEvent(QString(u"New controller acquired for setup: %1; starting explicit verification"_qs)
                    .arg(displayName));
                verifyHotasSetup();
            } else {
                appendEvent(QString(u"Could not acquire %1 for setup; connect it and try Set Up again"_qs)
                    .arg(displayName));
            }
            emit stateChanged();
        }, Qt::QueuedConnection);
    });
    m_controllerSelectionThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_controllerSelectionThread == thread) m_controllerSelectionThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

bool AppBackend::forgetController(const QString &recordId)
{
    const auto found = std::find_if(m_configuration.savedControllers.cbegin(), m_configuration.savedControllers.cend(),
        [&recordId](const SavedControllerRecord &record) { return record.id == recordId; });
    if (found == m_configuration.savedControllers.cend()) return false;
    const bool active = found->id == m_configuration.activeControllerRecordId;
    const QString name = found->displayName;
    m_configuration.savedControllers.erase(m_configuration.savedControllers.begin()
        + static_cast<std::ptrdiff_t>(std::distance(m_configuration.savedControllers.cbegin(), found)));
    if (active) {
        m_configuration.activeControllerRecordId.clear();
        m_configuration.preferredDeviceId.clear();
    }
    persistAndApply();
    rebuildControllerUiModel();
    if (active) m_worker.requestPhysicalControllerSelection();
    appendEvent(QString(u"Forgot saved controller: %1"_qs).arg(name));
    return true;
}

void AppBackend::setAutoSwitchVerifiedController(bool enabled)
{
    if (m_configuration.autoSwitchVerifiedController == enabled) return;
    m_configuration.autoSwitchVerifiedController = enabled;
    persistAndApply();
}

void AppBackend::setKeepRunningInTray(bool enabled)
{
    if (m_configuration.keepRunningInTray == enabled) return;
    m_configuration.keepRunningInTray = enabled;
    persistAndApply();
}

void AppBackend::setTrayTheme(const QString &themeName)
{
    if (!m_trayMenu) return;
    const QString normalized = themeName.trimmed().toLower();
    if (normalized == u"top gun"_qs) {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #09151a; color: #ead7a3; border: 1px solid #c29a5b; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Arial Narrow'; }"
            "QMenu::item:selected { background: #3b241c; color: #ff7b31; border: 1px solid #df6428; }"
            "QMenu::item:disabled { color: #816f55; }"
            "QMenu::separator { height: 1px; background: #765d37; margin: 5px 9px; }"));
    } else if (normalized == u"legacy"_qs) {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #182126; color: #eef5f5; border: 1px solid #52717c; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Segoe UI'; }"
            "QMenu::item:selected { background: #345864; color: #f0f4f5; border: 1px solid #78aab9; }"
            "QMenu::item:disabled { color: #8da0a5; }"
            "QMenu::separator { height: 1px; background: #3c5660; margin: 5px 9px; }"));
    } else {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #14191d; color: #f3f7f7; border: 1px solid #78aab9; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Segoe UI Variable'; }"
            "QMenu::item:selected { background: #244550; color: #dbe7e8; border: 1px solid #a8d1dc; }"
            "QMenu::item:disabled { color: #77919a; }"
            "QMenu::separator { height: 1px; background: #335268; margin: 5px 9px; }"));
    }
}

void AppBackend::forgetAllSavedControllers()
{
    m_configuration.savedControllers.clear();
    m_configuration.activeControllerRecordId.clear();
    m_configuration.preferredDeviceId.clear();
    persistAndApply();
    rebuildControllerUiModel();
    m_worker.requestPhysicalControllerSelection();
    appendEvent(u"All saved controllers were forgotten; profiles and automation were preserved"_qs);
}

void AppBackend::resetDeviceCalibration()
{
    for (Calibration &calibration : m_configuration.calibration) calibration = Calibration{};
    if (SavedControllerRecord *record = activeControllerRecord()) {
        for (Calibration &calibration : record->calibration) calibration = Calibration{};
    }
    persistAndApply();
    appendEvent(u"Active controller calibration reset; profiles and curves were preserved"_qs);
}

bool AppBackend::launchUninstaller()
{
    const QString uninstaller = QDir(QCoreApplication::applicationDirPath()).filePath(u"unins000.exe"_qs);
    if (!QFileInfo(uninstaller).isExecutable()) {
        appendEvent(u"Installed HOTAS BF6 uninstaller was not found"_qs);
        return false;
    }
    if (!QProcess::startDetached(uninstaller)) {
        appendEvent(u"Could not start the HOTAS BF6 uninstaller"_qs);
        return false;
    }
    appendEvent(u"Started the HOTAS BF6 uninstaller; shared vJoy and HidHide components are retained by default"_qs);
    exitApplication();
    return true;
}

void AppBackend::evaluateGameDetection()
{
    if (m_configuration.automaticGameDetection) startRunningApplicationSnapshot(false);
}

void AppBackend::refreshControllerInventory()
{
    if (m_controllerDiscoveryInProgress) return;
    m_controllerDiscoveryInProgress = true;
    if (m_uiPerformanceInstrumentationEnabled) ++m_controllerDiscoveryBackgroundRuns;
    QThread *thread = QThread::create([this] {
        QList<DiscoveredController> latestInventory = ControllerDiscovery::enumerate();
        QMetaObject::invokeMethod(this, [this, latestInventory = std::move(latestInventory)] () mutable {
            m_controllerDiscoveryInProgress = false;
            applyControllerInventory(std::move(latestInventory));
        }, Qt::QueuedConnection);
    });
    m_controllerDiscoveryThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_controllerDiscoveryThread == thread) m_controllerDiscoveryThread = nullptr;
        thread->deleteLater();
    });
    thread->start(QThread::LowPriority);
}

void AppBackend::applyControllerInventory(QList<DiscoveredController> latestInventory)
{
    const bool inventoryChanged = !sameControllerInventory(m_discoveredControllers, latestInventory);
    if (inventoryChanged) m_discoveredControllers = latestInventory;
    QStringList newlyDiscoveredUnverifiedIds;
    for (const DiscoveredController &controller : m_discoveredControllers) {
        if (controller.virtualDevice || !controller.connected || controller.directInputId.isEmpty()) continue;
        const bool firstSeen = !m_observedControllerIds.contains(controller.directInputId);
        m_observedControllerIds.insert(controller.directInputId);
        const ControllerMatch match = ControllerManager::match(controller, m_configuration.savedControllers);
        if (m_controllerInventoryInitialized && firstSeen && (match.recordId.isEmpty() || match.ambiguous)) {
            newlyDiscoveredUnverifiedIds.append(controller.directInputId);
        }
    }
    m_controllerInventoryInitialized = true;
    tryAutoSwitchVerifiedController();
    if (!newlyDiscoveredUnverifiedIds.isEmpty() && !m_verificationInProgress && !m_controllerSelectionInProgress) {
        appendEvent(newlyDiscoveredUnverifiedIds.size() == 1
            ? QString(u"New controller detected: %1. Select Set Up to explicitly verify it."_qs)
                .arg(discoveredController(newlyDiscoveredUnverifiedIds.front())->name)
            : QString(u"%1 new controllers detected. Select one for explicit setup."_qs)
                .arg(newlyDiscoveredUnverifiedIds.size()));
        emit controllerSetupRequested(newlyDiscoveredUnverifiedIds);
    }
    if (inventoryChanged && rebuildControllerUiModel()) emit stateChanged();
}

void AppBackend::tryAutoSwitchVerifiedController()
{
    if (!m_configuration.autoSwitchVerifiedController || m_worker.runtime().physicalConnected.load()
        || m_configuration.activeControllerRecordId.isEmpty()) return;
    const QString candidate = ControllerManager::autoSelect(m_discoveredControllers,
        m_configuration.savedControllers, m_configuration.activeControllerRecordId);
    if (candidate.isEmpty() || candidate.compare(m_configuration.preferredDeviceId, Qt::CaseInsensitive) == 0) return;
    const DiscoveredController *controller = discoveredController(candidate);
    if (!controller) return;
    const ControllerMatch match = ControllerManager::match(*controller, m_configuration.savedControllers);
    if (match.recordId.isEmpty() || match.ambiguous) return;
    const SavedControllerRecord *previous = activeControllerRecord();
    const QString previousName = previous ? previous->displayName : u"Selected controller"_qs;
    if (setActiveController(match.recordId)) {
        appendEvent(QString(u"%1 disconnected; switched to verified controller: %2"_qs)
            .arg(previousName, controller->name));
    }
}

void AppBackend::attachMainWindow(QWindow *window)
{
    if (m_mainWindow == window) return;
    m_mainWindow = window;
    if (!m_mainWindow) return;
    connect(m_mainWindow, &QWindow::visibilityChanged, this,
            [this](QWindow::Visibility) { updatePresentationLifecycle(); });
    connect(m_mainWindow, &QWindow::windowStateChanged, this,
            [this](Qt::WindowState) { updatePresentationLifecycle(); });
    connect(m_mainWindow, &QObject::destroyed, this, [this] {
        m_mainWindow = nullptr;
        m_trayHidden = false;
        setPresentationLifecycle(PresentationLifecycleState::Visible);
    });
    updatePresentationLifecycle();
}

void AppBackend::hideToTray()
{
    if (!m_mainWindow) return;
    m_trayHidden = true;
    m_mainWindow->hide();
    updatePresentationLifecycle();
}

void AppBackend::restoreFromTray()
{
    m_trayHidden = false;
    if (!m_mainWindow) {
        setPresentationLifecycle(PresentationLifecycleState::Visible);
        return;
    }
    restorePresentationResources();
    m_mainWindow->showNormal();
    m_mainWindow->raise();
    m_mainWindow->requestActivate();
    updatePresentationLifecycle();
}

void AppBackend::exitApplication()
{
    m_worker.setMappingEnabled(false);
    QTimer::singleShot(100, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
}

void AppBackend::refreshTrayStatus()
{
    if (!m_trayStatusAction || !m_trayToggleAction) return;
    const QString controller = physicalConnected() ? deviceName() : u"Controller disconnected"_qs;
    const QString mapping = mappingRequested() ? (mappingActive() ? u"Mapping active"_qs : u"Mapping suspended"_qs)
                                             : u"Mapping off"_qs;
    m_trayStatusAction->setText(controller + u" · "_qs + mapping);
    m_trayToggleAction->setText(mappingRequested() ? u"Stop Mapping"_qs : u"Start Mapping"_qs);
}

void AppBackend::updatePresentationLifecycle()
{
    if (m_trayHidden) {
        setPresentationLifecycle(PresentationLifecycleState::TrayHidden);
        return;
    }
    if (!m_mainWindow) {
        setPresentationLifecycle(PresentationLifecycleState::Visible);
        return;
    }
    if ((m_mainWindow->windowState() & Qt::WindowMinimized)
        || m_mainWindow->visibility() == QWindow::Minimized
        || !m_mainWindow->isVisible()) {
        setPresentationLifecycle(PresentationLifecycleState::Minimized);
        return;
    }
    setPresentationLifecycle(PresentationLifecycleState::Visible);
}

void AppBackend::setPresentationLifecycle(PresentationLifecycleState state)
{
    if (m_presentationLifecycle == state) return;
    m_presentationLifecycle = state;
    switch (state) {
    case PresentationLifecycleState::Visible:
        restorePresentationResources();
        m_snapshotTimer.start(kVisibleSnapshotIntervalMs);
        m_numericTelemetryTimer.start(kVisibleNumericTelemetryIntervalMs);
        m_controllerDiscoveryTimer.start(kVisibleControllerDiscoveryIntervalMs);
        if (m_configuration.automaticGameDetection) {
            m_gameDetectionTimer.start(kVisibleGameDetectionIntervalMs);
        }
        // Project the latest worker atomics before the visible QML tree has a
        // chance to render. This is presentation work only; MappingWorker has
        // remained awake and independent throughout the transition.
        refreshUiSnapshot();
        break;
    case PresentationLifecycleState::Minimized:
        m_snapshotTimer.start(kMinimizedSnapshotIntervalMs);
        m_numericTelemetryTimer.start(kMinimizedNumericTelemetryIntervalMs);
        m_controllerDiscoveryTimer.start(kMinimizedControllerDiscoveryIntervalMs);
        if (m_configuration.automaticGameDetection) {
            m_gameDetectionTimer.start(kMinimizedGameDetectionIntervalMs);
        }
        break;
    case PresentationLifecycleState::TrayHidden:
        m_snapshotTimer.stop();
        m_numericTelemetryTimer.stop();
        m_controllerDiscoveryTimer.start(kTrayHiddenControllerDiscoveryIntervalMs);
        if (m_configuration.automaticGameDetection) {
            m_gameDetectionTimer.start(kTrayHiddenGameDetectionIntervalMs);
        }
        releasePresentationResources();
        break;
    }
    emit presentationStateChanged();
}

void AppBackend::releasePresentationResources()
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(m_mainWindow.data());
    if (!quickWindow) return;
    // These are GUI-thread QQuick lifecycle controls. They release only scene
    // graph/graphics resources after the window has gone to the tray; the
    // backend and MappingWorker remain owned by the running application.
    quickWindow->setPersistentSceneGraph(false);
    quickWindow->setPersistentGraphics(false);
    quickWindow->releaseResources();
}

void AppBackend::restorePresentationResources()
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(m_mainWindow.data());
    if (!quickWindow) return;
    quickWindow->setPersistentSceneGraph(true);
    quickWindow->setPersistentGraphics(true);
}

void AppBackend::resetApplicationConfiguration()
{
    m_worker.setMappingEnabled(false);
    m_configuration = defaultConfiguration();
    persistAndApply();
    rebuildControllerUiModel();
    appendEvent(u"Application settings, saved controllers, and calibration reset to safe defaults"_qs);
}

PhysicalControllerCapabilities AppBackend::currentPhysicalCapabilities() const
{
    PhysicalControllerCapabilities physical;
    const DeviceSnapshot snapshot = m_worker.deviceSnapshot();
    physical.name = snapshot.name;
    physical.directInputId = snapshot.id;
    physical.hidInstanceId = snapshot.hidInstanceId;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    physical.connected = runtime.physicalConnected.load();
    physical.inputReportsReceived = runtime.physicalReportsSinceAcquisition.load() > 0;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        physical.axes[static_cast<size_t>(index)] = runtime.axisAvailable[index].load();
    }
    physical.buttons = runtime.buttonCount.load();
    physical.povs = runtime.povCount.load();
    return physical;
}

void AppBackend::captureInputLearningBaseline()
{
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const size_t slot = static_cast<size_t>(index);
        m_inputLearning.axisBaseline[slot] = runtime.normalized[slot].load();
        m_inputLearning.axisAvailable[slot] = runtime.axisAvailable[slot].load();
        m_inputLearning.axisActivity[slot] = static_cast<PhysicalAxisActivity>(
            runtime.axisActivity[slot].load());
    }
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        m_inputLearning.buttonBaseline[static_cast<size_t>(index)] =
            runtime.physicalButtonPressed[static_cast<size_t>(index)].load();
    }
    for (int index = 0; index < kMaximumPhysicalPovs; ++index) {
        m_inputLearning.povBaseline[static_cast<size_t>(index)] =
            runtime.povValues[static_cast<size_t>(index)].load();
    }
}

void AppBackend::enterInputLearningArming()
{
    captureInputLearningBaseline();
    m_inputLearning.phase = InputLearningPhase::Arming;
    m_inputLearning.armingStableSinceMs = 0;
    switch (m_inputLearning.kind) {
    case InputLearningKind::Axis:
        m_inputLearning.message = u"HOLD CONTROLS STEADY…"_qs;
        break;
    case InputLearningKind::Button:
        m_inputLearning.message = u"RELEASE HELD BUTTONS…"_qs;
        break;
    case InputLearningKind::Pov:
        m_inputLearning.message = u"RETURN THE HAT TO NEUTRAL…"_qs;
        break;
    case InputLearningKind::None:
        break;
    }
}

QString AppBackend::learnedAxisLabel(int physicalAxis) const
{
    if (!validAxis(physicalAxis)) return {};
    const QString custom = currentProfile().axes[static_cast<size_t>(physicalAxis)].customName.trimmed();
    return custom.isEmpty() ? physicalAxisLabel(static_cast<PhysicalAxis>(physicalAxis)) : custom;
}

QString AppBackend::learnedButtonLabel(int physicalButton) const
{
    if (!validPhysicalButton(physicalButton)) return {};
    const int source = physicalButton - 1;
    const ButtonBindings &bindings = currentProfile().buttons;
    const QString custom = source < static_cast<int>(bindings.size())
        ? bindings[static_cast<size_t>(source)].customName.trimmed() : QString{};
    return custom.isEmpty() ? QString(u"Button %1"_qs).arg(physicalButton) : custom;
}

bool AppBackend::applyLearnedInput()
{
    bool assigned = false;
    switch (m_inputLearning.kind) {
    case InputLearningKind::Axis:
        assigned = setMapping(m_inputLearning.sourceAxis, m_inputLearning.target, false);
        break;
    case InputLearningKind::Button:
        assigned = setButtonMapping(m_inputLearning.sourceButton, m_inputLearning.virtualButton, false);
        break;
    case InputLearningKind::Pov:
        assigned = setPovMapping(m_inputLearning.sourcePovHat,
            povDirectionIndex(m_inputLearning.sourcePovDirection), m_inputLearning.virtualButton, false);
        break;
    case InputLearningKind::None:
        return false;
    }
    m_inputLearning.phase = assigned ? InputLearningPhase::Assigned : InputLearningPhase::Conflict;
    m_inputLearning.message = assigned
        ? QString(u"%1 assigned."_qs).arg(m_inputLearning.sourceLabel)
        : QString(u"%1 conflicts with an existing route."_qs).arg(m_inputLearning.sourceLabel);
    emit inputLearningChanged();
    return assigned;
}

void AppBackend::processInputLearning()
{
    if (m_inputLearning.phase == InputLearningPhase::Arming) {
        const AtomicRuntimeState &runtime = m_worker.runtime();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        switch (m_inputLearning.kind) {
        case InputLearningKind::Axis: {
            constexpr float stabilityTolerance = 0.03F;
            bool stable = true;
            for (int index = 0; index < kPhysicalAxisCount; ++index) {
                const size_t slot = static_cast<size_t>(index);
                if (!m_inputLearning.axisAvailable[slot]
                    || m_inputLearning.axisActivity[slot] == PhysicalAxisActivity::Fixed) {
                    continue;
                }
                const float current = runtime.normalized[slot].load();
                if (std::abs(current - m_inputLearning.axisBaseline[slot]) > stabilityTolerance) {
                    m_inputLearning.axisBaseline[slot] = current;
                    stable = false;
                }
            }
            if (!stable || m_inputLearning.armingStableSinceMs == 0) {
                m_inputLearning.armingStableSinceMs = now;
                return;
            }
            if (now - m_inputLearning.armingStableSinceMs < 300) return;
            captureInputLearningBaseline();
            m_inputLearning.phase = InputLearningPhase::Waiting;
            m_inputLearning.message = u"MOVE THE PHYSICAL AXIS YOU WANT TO USE."_qs;
            emit inputLearningChanged();
            return;
        }
        case InputLearningKind::Button: {
            bool allReleased = true;
            for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
                const size_t slot = static_cast<size_t>(index);
                if (runtime.buttonAvailable[slot].load() && runtime.physicalButtonPressed[slot].load()) {
                    allReleased = false;
                    break;
                }
            }
            if (!allReleased) return;
            captureInputLearningBaseline();
            m_inputLearning.phase = InputLearningPhase::Waiting;
            m_inputLearning.message = u"PRESS THE PHYSICAL BUTTON YOU WANT TO USE."_qs;
            emit inputLearningChanged();
            return;
        }
        case InputLearningKind::Pov:
            for (int index = 0; index < std::min(povCount(), kMaximumPhysicalPovs); ++index) {
                if (povDirectionFromRaw(runtime.povValues[static_cast<size_t>(index)].load())
                    != PovDirection::Centered) {
                    return;
                }
            }
            captureInputLearningBaseline();
            m_inputLearning.phase = InputLearningPhase::Waiting;
            m_inputLearning.message = u"MOVE THE HAT IN THE DESIRED DIRECTION."_qs;
            emit inputLearningChanged();
            return;
        case InputLearningKind::None:
            return;
        }
    }
    if (m_inputLearning.phase != InputLearningPhase::Waiting) return;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    switch (m_inputLearning.kind) {
    case InputLearningKind::Axis: {
        std::array<float, kPhysicalAxisCount> current{};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            current[static_cast<size_t>(index)] = runtime.normalized[static_cast<size_t>(index)].load();
        }
        const AxisLearningSelection selection = selectLearnedAxis(m_inputLearning.axisBaseline,
            current, m_inputLearning.axisAvailable, m_inputLearning.axisActivity);
        if (selection.result == AxisLearningResult::Waiting) return;
        if (selection.result == AxisLearningResult::Ambiguous) {
            m_inputLearning.phase = InputLearningPhase::Ambiguous;
            m_inputLearning.message = u"MULTIPLE AXES DETECTED — Move only the control you want to assign."_qs;
            emit inputLearningChanged();
            return;
        }
        m_inputLearning.sourceAxis = selection.axis;
        m_inputLearning.sourceLabel = learnedAxisLabel(selection.axis);
        applyLearnedInput();
        return;
    }
    case InputLearningKind::Button:
        {
            std::array<bool, kMaximumPhysicalButtons> current{};
            std::array<bool, kMaximumPhysicalButtons> available{};
            for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
                const size_t slot = static_cast<size_t>(index);
                current[slot] = runtime.physicalButtonPressed[slot].load();
                available[slot] = runtime.buttonAvailable[slot].load();
            }
            const int button = selectLearnedButton(m_inputLearning.buttonBaseline, current, available);
            if (button == 0) return;
            m_inputLearning.sourceButton = button;
            m_inputLearning.sourceLabel = learnedButtonLabel(button);
            applyLearnedInput();
            return;
        }
    case InputLearningKind::Pov:
        for (int index = 0; index < std::min(povCount(), kMaximumPhysicalPovs); ++index) {
            const int raw = runtime.povValues[static_cast<size_t>(index)].load();
            const PovDirection direction = povDirectionFromRaw(raw);
            if (direction == PovDirection::Centered
                || raw == m_inputLearning.povBaseline[static_cast<size_t>(index)]) {
                continue;
            }
            m_inputLearning.sourcePovHat = index + 1;
            m_inputLearning.sourcePovDirection = direction;
            m_inputLearning.sourceLabel = QString(u"POV %1 %2"_qs).arg(index + 1)
                .arg(povDirectionLabel(direction));
            applyLearnedInput();
            return;
        }
        return;
    case InputLearningKind::None:
        return;
    }
}

void AppBackend::refreshUiSnapshot()
{
    // The worker publishes raw atomics only; no calibration calculation or
    // presentation allocation is performed during DirectInput-to-vJoy work.
    sampleCalibrationControlPlane();
    processInputLearning();
    const bool selectedAxisChanged = fallBackToAvailableAxis();
    if (selectedAxisChanged) emit selectedAxisCurveChanged();
    const bool connected = m_worker.runtime().physicalConnected.load();
    const bool connectionChanged = connected != m_physicalControllerWasConnected;
    if (ControllerReadinessService::isNewPhysicalControllerArrival(
            m_physicalControllerWasConnected, connected) && !m_verificationInProgress) {
        m_pendingControllerArrivalId = deviceId();
        appendEvent(u"Physical controller arrived; evaluating setup readiness"_qs);
        startQuickVerification();
    } else if (!connected) {
        m_pendingControllerArrivalId.clear();
    }
    m_physicalControllerWasConnected = connected;
    if (connectionChanged) {
        rebuildControllerUiModel();
        rebuildButtonUiModel();
    }
    if (refreshButtonUiModelRuntimeState()) emit buttonTelemetryChanged();
    const bool workerRequested = m_worker.mappingRequested();
    const bool mappingIntentChanged = workerRequested != m_mappingDesired;
    if (mappingIntentChanged) m_mappingDesired = workerRequested;
    const bool captureAdaptiveHistory = connected && workerRequested
        && m_presentationLifecycle != PresentationLifecycleState::TrayHidden;
    if (captureAdaptiveHistory && !m_adaptiveResponseHistoryTimer.isActive()) {
        // Record diagnostic history at 83 Hz; QML renders independently at
        // roughly 30 Hz and only while its section is near the viewport.
        m_adaptiveResponseHistoryTimer.start(kAdaptiveResponseHistoryIntervalMs);
    } else if (!captureAdaptiveHistory && m_adaptiveResponseHistoryTimer.isActive()) {
        m_adaptiveResponseHistoryTimer.stop();
    }
    const int effectiveMappingState = m_worker.runtime().mappingEffectiveState.load();
    const bool mappingEffectiveChanged = effectiveMappingState != m_presentedMappingEffectiveState;
    if (mappingEffectiveChanged) m_presentedMappingEffectiveState = effectiveMappingState;
    if (selectedAxisChanged || connectionChanged || mappingIntentChanged || mappingEffectiveChanged) emit stateChanged();
    // Analog and POV presentation is useful at 30 Hz; this broad property no
    // longer wakes QML at the former 62.5 Hz snapshot rate.
    emit inputTelemetryChanged();
}

void AppBackend::sampleAdaptiveResponseHistory()
{
    const int axis = std::clamp(m_configuration.selectedAxisIndex, 0, kPhysicalAxisCount - 1);
    const size_t index = static_cast<size_t>(axis);
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const qint64 elapsedMs = m_adaptiveResponseHistoryClock.elapsed();
    // A disconnected or suspended mapper has no live trace to animate. Keep a
    // sparse baseline for inspection, but do not manufacture 83 Hz flat data
    // that would wake QML graphs without meaningful telemetry movement.
    if ((!runtime.physicalConnected.load() || !mappingRequested())
        && m_adaptiveResponseHistoryCount > 0) {
        const int capacity = static_cast<int>(m_adaptiveResponseHistory.size());
        const AdaptiveResponseHistorySample &previous = m_adaptiveResponseHistory[static_cast<size_t>(
            (m_adaptiveResponseHistoryNext + capacity - 1) % capacity)];
        if (previous.axis == axis && elapsedMs - previous.elapsedMs < 1000) return;
    }
    AdaptiveResponseHistorySample &sample = m_adaptiveResponseHistory[
        static_cast<size_t>(m_adaptiveResponseHistoryNext)];
    sample.sequence = ++m_adaptiveResponseHistorySequence;
    sample.elapsedMs = elapsedMs;
    sample.axis = axis;
    sample.physical = runtime.normalized[index].load();
    sample.estimated = runtime.adaptiveEstimated[index].load();
    sample.predicted = runtime.adaptivePredicted[index].load();
    sample.virtualOutput = runtime.virtualValues[index].load();
    sample.velocity = runtime.adaptiveVelocity[index].load();
    sample.acceleration = runtime.adaptiveAcceleration[index].load();
    sample.activeHorizonSeconds = runtime.adaptiveHorizonSeconds[index].load();
    sample.maximumHorizonSeconds = runtime.adaptiveRuntimeMaximumHorizonSeconds[index].load();
    sample.lead = runtime.adaptiveLead[index].load();
    sample.confidence = runtime.adaptiveConfidence[index].load();
    sample.motionIntensity = runtime.adaptiveMotionIntensity[index].load();
    sample.velocityAuthority = runtime.adaptiveVelocityAuthority[index].load();
    sample.accelerationIntent = runtime.adaptiveAccelerationIntent[index].load();
    sample.onsetAuthority = runtime.adaptiveOnsetAuthority[index].load();
    sample.sustainedEvidence = runtime.adaptiveSustainedEvidence[index].load();
    sample.sustainedAuthority = runtime.adaptiveSustainedAuthority[index].load();
    sample.motionUrgency = runtime.adaptiveMotionUrgency[index].load();
    sample.horizonExtensionEligibility = runtime.adaptiveHorizonExtensionEligibility[index].load();
    sample.normalMaximumHorizonSeconds = runtime.adaptiveNormalMaximumHorizonSeconds[index].load();
    sample.allowedMaximumHorizonSeconds = runtime.adaptiveAllowedMaximumHorizonSeconds[index].load();
    sample.turningPointConfidence = runtime.adaptiveTurningPointConfidence[index].load();
    sample.estimatedTimeToTurnSeconds = runtime.adaptiveEstimatedTimeToTurnSeconds[index].load();
    sample.estimatedRemainingTravel = runtime.adaptiveEstimatedRemainingTravel[index].load();
    sample.turningPointHorizonLimitSeconds = runtime.adaptiveTurningPointHorizonLimitSeconds[index].load();
    sample.turningPointLeadLimit = runtime.adaptiveTurningPointLeadLimit[index].load();
    sample.reacquisitionAuthority = runtime.adaptiveReacquisitionAuthority[index].load();
    sample.motionState = runtime.adaptiveMotionState[index].load();
    m_adaptiveResponseHistoryNext = (m_adaptiveResponseHistoryNext + 1)
        % static_cast<int>(m_adaptiveResponseHistory.size());
    m_adaptiveResponseHistoryCount = std::min(m_adaptiveResponseHistoryCount + 1,
        static_cast<int>(m_adaptiveResponseHistory.size()));
}

void AppBackend::refreshNumericTelemetry()
{
    // Percentiles are diagnostic telemetry. Sampling them four times per
    // second keeps statistics fresh without making them a render workload.
    if (m_latencyPercentileClock.elapsed() >= 250) {
        const MappingLatencyPercentiles percentiles = m_worker.latencyPercentiles();
        m_latencyP95Us = percentiles.p95Us;
        m_latencyP99Us = percentiles.p99Us;
        m_latencyPercentileClock.restart();
    }
    const qint64 elapsed = m_rateClock.restart();
    if (elapsed > 0) {
        const quint64 reports = m_worker.runtime().inputReports.load();
        const quint64 writes = m_worker.runtime().vjoyWrites.load();
        const bool receivedPhysicalUpdate = reports != m_previousInputReports;
        m_inputReportsPerSecond = (reports - m_previousInputReports) * 1000.0 / elapsed;
        m_vjoyWritesPerSecond = (writes - m_previousVjoyWrites) * 1000.0 / elapsed;
        m_previousInputReports = reports;
        m_previousVjoyWrites = writes;
        if (reports > 0) {
            if (!m_havePhysicalReport || receivedPhysicalUpdate) {
                m_physicalUpdateClock.restart();
                m_havePhysicalReport = true;
            }
            m_lastPhysicalUpdateAgeMs = m_physicalUpdateClock.elapsed();
        } else {
            m_lastPhysicalUpdateAgeMs = -1;
        }
    }
    constexpr double kSmoothingTimeConstantMs = 325.0;
    const double elapsedMs = std::max(1.0, static_cast<double>(m_overviewMetricsClock.restart()));
    const double alpha = 1.0 - std::exp(-elapsedMs / kSmoothingTimeConstantMs);
    const double rawLatency = static_cast<double>(m_worker.runtime().latencyAverageUs.load());
    if (!m_worker.runtime().physicalConnected.load() || !mappingRequested()) {
        m_overviewInputRate = 0.0;
        m_overviewOutputRate = 0.0;
    } else {
        m_overviewInputRate += alpha * (m_inputReportsPerSecond - m_overviewInputRate);
        m_overviewOutputRate += alpha * (m_vjoyWritesPerSecond - m_overviewOutputRate);
    }
    m_overviewMapperLatencyUs += alpha * (rawLatency - m_overviewMapperLatencyUs);
    refreshTrayStatus();
    emit telemetryChanged();
}

QVariantMap AppBackend::uiPerformanceCounters() const
{
    if (!m_uiPerformanceInstrumentationEnabled) return {};
    return {{u"controllerGetterCalls"_qs, QVariant::fromValue(m_controllerGetterCalls)},
            {u"controllerModelRebuilds"_qs, QVariant::fromValue(m_controllerUiModelRebuilds)},
            {u"buttonGetterCalls"_qs, QVariant::fromValue(m_buttonGetterCalls)},
            {u"buttonModelRebuilds"_qs, QVariant::fromValue(m_buttonUiModelRebuilds)},
            {u"profileGetterCalls"_qs, QVariant::fromValue(m_profileGetterCalls)},
            {u"categoryGetterCalls"_qs, QVariant::fromValue(m_categoryGetterCalls)},
            {u"stateChanged"_qs, QVariant::fromValue(m_stateChangedNotifications)},
            {u"telemetryChanged"_qs, QVariant::fromValue(m_telemetryChangedNotifications)},
            {u"inputTelemetryChanged"_qs, QVariant::fromValue(m_inputTelemetryChangedNotifications)},
            {u"buttonTelemetryChanged"_qs, QVariant::fromValue(m_buttonTelemetryChangedNotifications)},
            {u"controllersChanged"_qs, QVariant::fromValue(m_controllersChangedNotifications)},
            {u"controllerDiscoveryBackgroundRuns"_qs, QVariant::fromValue(m_controllerDiscoveryBackgroundRuns)},
            {u"controllerDiscoveryTimerActive"_qs, m_controllerDiscoveryTimer.isActive()},
            {u"gameDetectionBackgroundRuns"_qs, QVariant::fromValue(m_gameDetectionBackgroundRuns)},
            {u"uiEventLoopMaxDelayMs"_qs, m_uiEventLoopMaxDelayMs},
            {u"uiEventLoopDelayOver16Ms"_qs, QVariant::fromValue(m_uiEventLoopDelayOver16Ms)},
            {u"uiEventLoopDelayOver50Ms"_qs, QVariant::fromValue(m_uiEventLoopDelayOver50Ms)},
            {u"uiEventLoopDelayOver100Ms"_qs, QVariant::fromValue(m_uiEventLoopDelayOver100Ms)},
            {u"uiEventLoopDelayOver250Ms"_qs, QVariant::fromValue(m_uiEventLoopDelayOver250Ms)}};
}

void AppBackend::resetUiPerformanceCounters()
{
    if (!m_uiPerformanceInstrumentationEnabled) return;
    m_controllerGetterCalls = 0;
    m_controllerUiModelRebuilds = 0;
    m_buttonGetterCalls = 0;
    m_buttonUiModelRebuilds = 0;
    m_profileGetterCalls = 0;
    m_categoryGetterCalls = 0;
    m_stateChangedNotifications = 0;
    m_telemetryChangedNotifications = 0;
    m_inputTelemetryChangedNotifications = 0;
    m_buttonTelemetryChangedNotifications = 0;
    m_controllersChangedNotifications = 0;
    m_controllerDiscoveryBackgroundRuns = 0;
    m_gameDetectionBackgroundRuns = 0;
    m_uiEventLoopMaxDelayMs = 0;
    m_uiEventLoopDelayOver16Ms = 0;
    m_uiEventLoopDelayOver50Ms = 0;
    m_uiEventLoopDelayOver100Ms = 0;
    m_uiEventLoopDelayOver250Ms = 0;
    m_uiEventLoopHeartbeatClock.restart();
}

void AppBackend::appendEvent(const QString &event)
{
    const QString timestamp = QDateTime::currentDateTime().toString(u"HH:mm:ss"_qs);
    m_events.append(timestamp + u"  "_qs + event);
    emit eventLogChanged();
}

void AppBackend::initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity)
{
    if (physicalButtonCount <= 0 || vjoyButtonCapacity <= 0) {
        return;
    }
    if (!ensureDefaultButtonMappings(currentProfile().buttons, physicalButtonCount, vjoyButtonCapacity)) {
        return;
    }
    persistAndApply();
    appendEvent(u"Implicit button routes initialized as 1:1 passthrough"_qs);
}

const ControllerProfile &AppBackend::currentProfile() const
{
    return activeProfile(m_configuration);
}

ControllerProfile &AppBackend::currentProfile()
{
    return activeProfile(m_configuration);
}

AxisMapping *AppBackend::selectedAxisMapping()
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

const AxisMapping *AppBackend::selectedAxisMapping() const
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

void AppBackend::persistAndApply()
{
    if (SavedControllerRecord *record = activeControllerRecord()) {
        record->calibration = m_configuration.calibration;
        record->axisActivity = m_configuration.axisActivity;
        record->vjoyRequirements = currentVjoyRequirements();
    }
    ConfigStore::save(m_configuration);
    m_worker.updateConfiguration(m_configuration);
    rebuildSelectedAxisCurve();
    rebuildCurveAxisChoices();
    rebuildButtonUiModel();
    emit selectedAxisCurveChanged();
    emit stateChanged();
}

void AppBackend::rebuildSelectedAxisCurve()
{
    m_selectedAxisCurve.clear();
    m_curveEditorResponseCurve.clear();
    m_curveGainSamples.clear();
    m_curveComparisonCurve.clear();
    m_curvePreviewCurve.clear();
    m_curveAnalysis.clear();
    if (!validAxis(m_configuration.selectedAxisIndex)) return;
    const int axisIndex = m_configuration.selectedAxisIndex;
    const AxisMapping &axis = currentProfile().axes[axisIndex];
    const bool unipolar = axis.rangeMode == AxisRangeMode::OneSided;
    RuntimeAxisMapping mapping;
    mapping.profile = axis;
    // The chart domain is the calibrated coordinate system. Applying raw
    // calibration again would bend a linear curve around an electrical sensor
    // offset, even though the user-facing neutral is exactly zero.
    mapping.calibration = {};
    mapping.responseCurve = compileResponseCurve(axis.curve, unipolar);
    constexpr int kSamples = 101;
    const float graphMinimum = unipolar ? 0.0F : -1.0F;
    m_selectedAxisCurve.reserve(kSamples);
    for (int sample = 0; sample < kSamples; ++sample) {
        const float input = graphMinimum + (1.0F - graphMinimum) * static_cast<float>(sample)
            / static_cast<float>(kSamples - 1);
        QVariantMap point;
        point.insert(u"input"_qs, input);
        // Reuse the mapping engine's static evaluator so the graph cannot
        // drift from deadzone, inversion, calibration, or output limits.
        point.insert(u"output"_qs, evaluateStaticAxisTransfer(input, mapping));
        m_selectedAxisCurve.append(point);
    }

    constexpr int kEditorSamples = 201;
    const float minimum = unipolar ? 0.0F : -1.0F;
    m_curveEditorResponseCurve.reserve(kEditorSamples);
    m_curveGainSamples.reserve(kEditorSamples);
    if (!m_curveComparisonId.isEmpty()) m_curveComparisonCurve.reserve(kEditorSamples);
    if (!m_curvePreviewId.isEmpty()) m_curvePreviewCurve.reserve(kEditorSamples);
    const CurveDefinition comparison = comparisonCurveDefinition();
    for (int sample = 0; sample < kEditorSamples; ++sample) {
        const float input = minimum + (1.0F - minimum) * static_cast<float>(sample)
            / static_cast<float>(kEditorSamples - 1);
        m_curveEditorResponseCurve.append(QVariantMap{{u"input"_qs, input},
            {u"output"_qs, evaluateCurveDefinition(input, axis.curve, unipolar)}});
        m_curveGainSamples.append(QVariantMap{{u"input"_qs, input},
            {u"gain"_qs, evaluateCurveGain(input, axis.curve, unipolar)}});
        if (!m_curveComparisonId.isEmpty()) {
            m_curveComparisonCurve.append(QVariantMap{{u"input"_qs, input},
                {u"output"_qs, evaluateCurveDefinition(input, comparison, unipolar)}});
        }
        if (!m_curvePreviewId.isEmpty()) {
            m_curvePreviewCurve.append(QVariantMap{{u"input"_qs, input},
                {u"output"_qs, evaluateCurveDefinition(input, m_curvePreviewDefinition, unipolar)}});
        }
    }
    const CurveAnalysis analysis = analyzeCurveDefinition(axis.curve, unipolar);
    m_curveAnalysis.insert(u"valid"_qs, analysis.valid);
    m_curveAnalysis.insert(u"monotonic"_qs, analysis.monotonic);
    m_curveAnalysis.insert(u"continuous"_qs, analysis.continuous);
    m_curveAnalysis.insert(u"fullAuthority"_qs, analysis.fullAuthority);
    m_curveAnalysis.insert(u"noOvershoot"_qs, analysis.noOvershoot);
    m_curveAnalysis.insert(u"centerGain"_qs, analysis.centerGain);
    m_curveAnalysis.insert(u"quarterGain"_qs, analysis.quarterGain);
    m_curveAnalysis.insert(u"halfGain"_qs, analysis.halfGain);
    m_curveAnalysis.insert(u"threeQuarterGain"_qs, analysis.threeQuarterGain);
    m_curveAnalysis.insert(u"peakGain"_qs, analysis.peakGain);
    m_curveAnalysis.insert(u"largestGainTransition"_qs, analysis.largestGainTransition);
}

CurveDefinition AppBackend::comparisonCurveDefinition() const
{
    if (m_curveComparisonId.isEmpty()) return linearCurveDefinition();
    if (m_curveComparisonId == u"source"_qs) {
        const AxisMapping *current = selectedAxisMapping();
        if (!current) return linearCurveDefinition();
        const CurveDefinition &curve = current->curve;
        if (curve.sourceFamily == CurveFamily::JCurve || curve.sourceFamily == CurveFamily::SCurve) {
            return standardCurveDefinition(curve.sourceFamily, curve.sourcePresetId);
        }
        if (curve.sourceFamily == CurveFamily::Advanced) {
            return advancedCurveDefinition(curve.sourcePresetId);
        }
        if (curve.sourceFamily == CurveFamily::Personal) {
            const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
                m_configuration.personalCurvePresets.cend(), [&curve](const PersonalCurvePreset &preset) {
                    return preset.id == curve.sourcePresetId;
                });
            if (found != m_configuration.personalCurvePresets.cend()) return found->definition;
        }
        return linearCurveDefinition();
    }
    const QStringList parts = m_curveComparisonId.split(u':');
    if (parts.size() == 3 && parts[0] == u"profile"_qs) {
        const ControllerProfile *profile = findProfile(m_configuration, parts[1]);
        const int axis = parts[2].toInt();
        if (profile && validAxis(axis)) return profile->axes[axis].curve;
    }
    if (parts.size() == 3 && parts[0] == u"standard"_qs) {
        return standardCurveDefinition(parts[1] == u"s"_qs ? CurveFamily::SCurve : CurveFamily::JCurve,
                                       parts[2]);
    }
    if (parts.size() == 2 && parts[0] == u"advanced"_qs) return advancedCurveDefinition(parts[1]);
    if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found != m_configuration.personalCurvePresets.cend()) return found->definition;
    }
    return linearCurveDefinition();
}

bool AppBackend::fallBackToAvailableAxis()
{
    // Curve selection is a durable editor context, not a live-device routing
    // decision. Earlier code silently jumped back to the first discovered
    // axis when a selected DirectInput object was absent, making Rotation and
    // slider editing appear broken. All eight logical axes remain selectable;
    // availability is shown by the axes UI without rewriting this selection.
    return false;
}

bool AppBackend::validAxis(int physicalAxis) const
{
    return physicalAxis >= 0 && physicalAxis < kPhysicalAxisCount;
}

bool AppBackend::validPhysicalButton(int physicalButton) const
{
    const int source = physicalButton - 1;
    return source >= 0 && source < kMaximumPhysicalButtons
        && m_worker.runtime().buttonAvailable[source].load();
}

bool AppBackend::axisIsOneSided(int physicalAxis) const
{
    return validAxis(physicalAxis)
        && currentProfile().axes[static_cast<size_t>(physicalAxis)].rangeMode
            == AxisRangeMode::OneSided;
}

} // namespace hotas
