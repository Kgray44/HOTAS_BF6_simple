#include "app_backend.h"
#include "crash_diagnostics.h"

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
#include "signal_flow_model.h"
#include "setup_truth.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
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
// Foreground matching is control-plane work.  A short debounce keeps focus
// changes from flapping profiles while never entering the input-report path.
constexpr int kForegroundGameProbeIntervalMs = 250;
constexpr int kForegroundGameStableMs = 450;
constexpr int kRequiredDeviceDisconnectGraceMs = 3500;

bool startupSmokeRequested()
{
    const QStringList arguments = QCoreApplication::arguments();
    return arguments.contains(u"--startup-smoke"_qs)
        || arguments.contains(u"--startup-smoke-isolated"_qs)
        || arguments.contains(u"--isolated-presentation"_qs);
}

bool sameControllerInventory(const QList<DiscoveredController> &left,
                             const QList<DiscoveredController> &right)
{
    if (left.size() != right.size()) return false;
    return std::equal(left.cbegin(), left.cend(), right.cbegin(),
                      [](const DiscoveredController &first, const DiscoveredController &second) {
        return first.name == second.name && first.directInputId == second.directInputId
            && first.productGuid == second.productGuid && first.hidInstanceId == second.hidInstanceId
            && first.hidContainerId == second.hidContainerId
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

struct ForegroundApplicationSnapshot {
    QString executable;
    quint64 processId = 0;
};

ForegroundApplicationSnapshot foregroundApplicationSnapshot()
{
#ifdef Q_OS_WIN
    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;
    if (!foreground || GetWindowThreadProcessId(foreground, &processId) == 0 || processId == 0) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried || length == 0) return {};
    return {QFileInfo(QString::fromWCharArray(path.data(), static_cast<qsizetype>(length))).fileName(), processId};
#else
    return {};
#endif
}

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

int adaptiveOverrideAxisCount(const AdaptiveResponseLayer &layer)
{
    return static_cast<int>(std::count_if(layer.axes.cbegin(), layer.axes.cend(),
        [](const AdaptiveResponseAxisOverride &entry) {
            return entry.properties != 0 || !entry.presetId.isEmpty();
        }));
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

QVariantMap issueNavigationTarget(const QString &code, const QString &objectType,
                                  const QString &objectId)
{
    int page = 3; // Diagnostics is the safe technical fallback.
    if (objectType == u"signalFlowRoute"_qs || objectType == u"signalFlowProcessor"_qs
        || code == u"RoutingConflict"_qs || code == u"NoMappedControl"_qs) {
        page = 11;
    } else if (objectType == u"axis"_qs) {
        page = 0;
    } else if (objectType == u"physicalDevice"_qs || objectType == u"deviceRig"_qs
        || objectType == u"virtualOutput"_qs || objectType == u"gameVisibility"_qs) {
        page = 10;
    } else if (objectType == u"profile"_qs) {
        page = 5;
    } else if (objectType == u"automation"_qs) {
        page = 7;
    } else if (objectType == u"calibration"_qs || code == u"CalibrationRequired"_qs) {
        page = 2;
    }
    return {{u"page"_qs, page}, {u"objectType"_qs, objectType},
            {u"objectId"_qs, objectId}, {u"issueCode"_qs, code}};
}

QVariantMap actionResult(bool success, const QString &title, const QString &message,
                         const QString &objectType = {}, const QString &objectId = {},
                         const QString &nextAction = {}, const QString &nextActionLabel = {},
                         const QString &technicalDetails = {}, bool inProgress = false)
{
    AppActionResult result;
    result.success = success;
    result.severity = success ? (inProgress ? u"in-progress"_qs : u"success"_qs) : u"error"_qs;
    result.title = title;
    result.message = message;
    result.affectedObjectType = objectType;
    result.affectedObjectId = objectId;
    result.nextAction = nextAction;
    result.nextActionLabel = nextActionLabel;
    result.technicalDetails = technicalDetails;
    result.inProgress = inProgress;
    return result.toVariantMap();
}

bool hasMappedControl(const CompiledDeviceRigMember &member)
{
    if (std::any_of(member.mapping.axes.cbegin(), member.mapping.axes.cend(),
            [](const RuntimeAxisMapping &axis) {
                return axis.profile.target != VirtualAxis::Disabled;
            })) return true;
    if (std::any_of(member.mapping.buttons.cbegin(), member.mapping.buttons.cend(),
            [](const ButtonBinding &binding) {
                return binding.type == ButtonActionType::VirtualButton && binding.target > 0;
            })) return true;
    for (const PovDirectionBindings &pov : member.mapping.povs) {
        if (std::any_of(pov.cbegin(), pov.cend(), [](const ButtonBinding &binding) {
                return binding.type == ButtonActionType::VirtualButton && binding.target > 0;
            })) return true;
    }
    return std::any_of(member.nativePovBindings.cbegin(), member.nativePovBindings.cend(),
        [](const NativePovBinding &binding) {
            return binding.enabled && binding.targetType != NativePovTargetType::Disabled
                && binding.targetIndex > 0;
        });
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
        condition.controllerRecordId = input.value(u"controllerRecordId"_qs).toString().trimmed();
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
        action.sourceControllerRecordId = input.value(u"sourceControllerRecordId"_qs)
            .toString().trimmed();
        action.outputLayoutId = input.value(u"outputLayoutId"_qs).toString().trimmed();
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
    // Package/installer startup acceptance needs the real QML shell, backend
    // models, and tray construction, but it must not acquire DirectInput,
    // vJoy, or foreground-game state from a machine that may be in use. The
    // flags are explicit automation entry points handled by main.cpp; normal
    // launches retain the unchanged hardware startup path below.
    const bool startupSmoke = startupSmokeRequested();
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
        // This is intentionally not an inspection result. In particular, the
        // default HidHide fields must never be projected as "unavailable"
        // simply because a previous process ended during a repair.
        m_readiness.adoptPlan(std::move(pending));
    }
    connect(&m_snapshotTimer, &QTimer::timeout, this, &AppBackend::refreshUiSnapshot);
    connect(&m_numericTelemetryTimer, &QTimer::timeout, this, &AppBackend::refreshNumericTelemetry);
    connect(&m_controllerDiscoveryTimer, &QTimer::timeout, this, &AppBackend::refreshControllerInventory);
    connect(&m_gameDetectionTimer, &QTimer::timeout, this, &AppBackend::evaluateGameDetection);
    connect(&m_foregroundGameTimer, &QTimer::timeout, this, &AppBackend::sampleForegroundGameContext);
    m_requiredDisconnectGraceTimer.setSingleShot(true);
    connect(&m_requiredDisconnectGraceTimer, &QTimer::timeout, this, [this] {
        updateRequiredDeviceDisconnectGrace();
        scheduleActivationResolution(u"required Device Rig disconnect grace expired"_qs);
    });
    m_activationResolveTimer.setSingleShot(true);
    m_activationResolveTimer.setInterval(120);
    connect(&m_activationResolveTimer, &QTimer::timeout, this, &AppBackend::resolveActivationNow);
    connect(&m_worker, &MappingWorker::workerEvent, this, &AppBackend::appendEvent, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::hardwareStateChanged, this, [this] {
        // A generic hardware change also covers vJoy readiness and status.
        // Rebuild controller presentation only when the worker's active
        // DirectInput identity changed; inventory changes use their dedicated
        // low-frequency path below.
        if (deviceId() != m_controllerUiModelLiveDeviceId) rebuildControllerUiModel();
        scheduleActivationResolution(u"runtime output state changed"_qs);
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
    // The 83 Hz sampler is started only while a controller is connected and
    // the application surface is visible. It reads the worker's latest fixed
    // snapshot, so observing physical movement never depends on vJoy or
    // active output mapping.
    m_numericTelemetryTimer.setInterval(kVisibleNumericTelemetryIntervalMs);
    m_numericTelemetryTimer.start();
    // DirectInput enumeration is an independent, low-frequency control-plane
    // snapshot.  The report loop neither waits for it nor reads its results.
    m_controllerDiscoveryTimer.setInterval(kVisibleControllerDiscoveryIntervalMs);
    if (!startupSmoke) m_controllerDiscoveryTimer.start();
    // Foreground-process sampling is low-frequency control-plane work. It is
    // intentionally independent from the presentation snapshot and
    // the DirectInput worker's report loop.
    m_gameDetectionTimer.setInterval(kVisibleGameDetectionIntervalMs);
    if (!startupSmoke && m_configuration.automaticGameDetection) m_gameDetectionTimer.start();
    m_foregroundExecutableClock.start();
    m_activationControlPlaneClock.start();
    m_foregroundGameTimer.setInterval(kForegroundGameProbeIntervalMs);
    if (!startupSmoke && m_configuration.automaticGameDetection) m_foregroundGameTimer.start();
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
    m_presentedEffectiveProfileName = effectiveProfileName();
    m_presentedEffectiveProfileDisplayName = effectiveProfileDisplayName();
    m_presentedProfileSourceLabel = profileSourceLabel();
    // Profile presentation is a derived control-plane projection.  Keep the
    // shell's text bindings asleep during ordinary 30 Hz input telemetry and
    // notify them only after a relevant state boundary actually changes it.
    connect(this, &AppBackend::stateChanged, this,
            &AppBackend::publishProfilePresentationIfChanged);
    appendEvent(u"HOTAS Mapper ready"_qs);
    // The mapping thread consumes physical reports while the GUI may be
    // rebuilding editor data. HighPriority is intentionally below
    // TimeCriticalPriority: it favors real input responsiveness without
    // starving normal system or rendering work on a constrained CPU.
    if (!startupSmoke) {
        m_worker.start(QThread::HighPriority);
        m_mappingDesired = m_configuration.startMappingOnLaunch;
        if (m_mappingDesired) {
            m_worker.setMappingEnabled(true);
        }
    }
    // Startup verification is passive and runs on its own short-lived worker.
    // It never stops mapping, releases either device, or opens a modal.
    if (!startupSmoke) {
        QTimer::singleShot(750, this, &AppBackend::startQuickVerification);
        QTimer::singleShot(100, this, &AppBackend::refreshControllerInventory);
        if (m_configuration.automaticGameDetection) {
            QTimer::singleShot(kVisibleGameDetectionIntervalMs, this, &AppBackend::evaluateGameDetection);
            QTimer::singleShot(kForegroundGameProbeIntervalMs, this, &AppBackend::sampleForegroundGameContext);
        }
    }
    // Update network activity is intentionally scheduled on the UI event loop
    // after startup. It never enters the DirectInput/vJoy worker or its hot
    // path, and a bounded timeout leaves mapper startup fully independent.
    if (!startupSmoke) QTimer::singleShot(500, this, &AppBackend::checkForUpdates);
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

QVariantList AppBackend::axisConfiguration() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : profile.axes;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const auto axis = static_cast<PhysicalAxis>(index);
        const AxisMapping &mapping = axes[index];
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
        item.insert(u"target"_qs, virtualAxisLabel(mapping.target));
        const int targetIndex = static_cast<int>(mapping.target);
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
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::axisTelemetry() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &mappings = deviceMapping ? deviceMapping->axes : currentProfile().axes;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const AxisMapping &mapping = mappings[index];
        const bool fixed = static_cast<PhysicalAxisActivity>(runtime.axisActivity[index].load())
            == PhysicalAxisActivity::Fixed;
        const int targetIndex = static_cast<int>(mapping.target);
        const bool virtualRouted = !fixed && targetIndex > 0 && targetIndex < kVirtualAxisSlotCount
            && runtime.virtualAxisAvailable[static_cast<size_t>(targetIndex)].load();
        const float virtualValue = virtualRouted ? runtime.virtualValues[index].load()
                                                 : std::numeric_limits<float>::quiet_NaN();
        QVariantMap item;
        item.insert(u"index"_qs, index);
        item.insert(u"raw"_qs, runtime.raw[index].load());
        item.insert(u"calibrated"_qs, runtime.normalized[index].load());
        item.insert(u"curveResponse"_qs, runtime.curveResponse[index].load());
        item.insert(u"transformed"_qs, runtime.transformed[index].load());
        item.insert(u"virtualValue"_qs, virtualValue);
        item.insert(u"virtualRouted"_qs, virtualRouted);
        item.insert(u"virtualValid"_qs, virtualRouted && std::isfinite(virtualValue));
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::axes() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : profile.axes;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const auto axis = static_cast<PhysicalAxis>(index);
        const AxisMapping &mapping = axes[index];
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

QVariantList AppBackend::curveCustomProfileChoices() const
{
    QVariantList result;
    const int axis = m_configuration.selectedAxisIndex;
    if (!validAxis(axis)) return result;
    const bool targetUnipolar = axisIsOneSided(axis);
    for (const ControllerProfile &profile : m_configuration.profiles) {
        const AxisMapping &mapping = profile.axes[static_cast<size_t>(axis)];
        if (mapping.curve.family != CurveFamily::Custom) continue;
        const bool sourceUnipolar = mapping.rangeMode == AxisRangeMode::OneSided;
        if (sourceUnipolar != targetUnipolar) continue;
        result.append(QVariantMap{{u"id"_qs, profile.id}, {u"name"_qs, profile.name},
                                  {u"active"_qs, profile.id == m_configuration.activeProfileId},
                                  {u"summary"_qs, curveDefinitionSummary(mapping.curve)}});
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
    if (normalized == u"normalmovementresponse"_qs) return AdaptiveResponseNormalMovementResponse;
    if (normalized == u"rapidmovementresponse"_qs) return AdaptiveResponseRapidMovementResponse;
    if (normalized == u"engagementsensitivity"_qs) return AdaptiveResponseEngagementSensitivity;
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
    settings.normalMovementResponse = runtime.normalMovementResponse;
    settings.rapidMovementResponse = runtime.rapidMovementResponse;
    settings.engagementSensitivity = runtime.engagementSensitivity;
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
            {u"turningPointMargin"_qs, runtime.turningPointMargin},
            {u"normalMovementResponse"_qs, runtime.normalMovementResponse},
            {u"rapidMovementResponse"_qs, runtime.rapidMovementResponse},
            {u"engagementSensitivity"_qs, runtime.engagementSensitivity}};
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
            {u"turningPointMargin"_qs, runtime.adaptiveRuntimeTurningPointMargin[index].load()},
            {u"normalMovementResponse"_qs, runtime.adaptiveRuntimeNormalMovementResponse[index].load()},
            {u"rapidMovementResponse"_qs, runtime.adaptiveRuntimeRapidMovementResponse[index].load()},
            {u"engagementSensitivity"_qs, runtime.adaptiveRuntimeEngagementSensitivity[index].load()}};
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
    static constexpr std::array<PropertyLabel, 24> labels{{
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
        {AdaptiveResponseNormalMovementResponse, u"Normal Movement Response"},
        {AdaptiveResponseRapidMovementResponse, u"Rapid Movement Response"},
        {AdaptiveResponseEngagementSensitivity, u"Engagement Sensitivity"},
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
            {u"predicted"_qs, load(runtime.adaptivePredicted)},
            {u"baselineOutput"_qs, load(runtime.adaptiveBaselineMapped)},
            {u"predictedMappedOutput"_qs, load(runtime.adaptivePredictedMapped)},
            {u"adaptiveOutput"_qs, load(runtime.adaptiveOutput)},
            {u"mappedLead"_qs, load(runtime.adaptiveMappedLead)},
            {u"appliedLead"_qs, load(runtime.adaptiveAppliedLead)},
            {u"localCurveGain"_qs, load(runtime.adaptiveLocalCurveGain)},
            {u"virtualOutput"_qs, load(runtime.adaptiveOutput)},
            {u"velocity"_qs, load(runtime.adaptiveVelocity)}, {u"acceleration"_qs, load(runtime.adaptiveAcceleration)},
            {u"activeHorizonMs"_qs, load(runtime.adaptiveHorizonSeconds) * 1000.0F},
            {u"maximumHorizonMs"_qs, maximumHorizonSeconds * 1000.0F},
            {u"requestedLead"_qs, load(runtime.adaptiveRequestedLead)},
            {u"cappedLead"_qs, load(runtime.adaptiveCappedLead)},
            {u"endpointTaper"_qs, load(runtime.adaptiveEndpointTaper)},
            {u"lead"_qs, load(runtime.adaptiveLead)}, {u"maximumLead"_qs, maximumLead},
            {u"confidence"_qs, load(runtime.adaptiveConfidence)},
            {u"motionIntensity"_qs, load(runtime.adaptiveMotionIntensity)},
            {u"velocityAuthority"_qs, load(runtime.adaptiveVelocityAuthority)},
            {u"deliberateMotionEvidence"_qs, load(runtime.adaptiveDeliberateMotionEvidence)},
            {u"normalMotionAuthority"_qs, load(runtime.adaptiveNormalMotionAuthority)},
            {u"rapidMotionAuthority"_qs, load(runtime.adaptiveRapidMotionAuthority)},
            {u"rapidMotionBlend"_qs, load(runtime.adaptiveRapidMotionBlend)},
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
            {u"deadzoneAuthorityBlocked"_qs, load(runtime.adaptiveDeadzoneAuthorityBlocked)},
            {u"leadLimited"_qs, load(runtime.adaptiveLeadLimited)},
            {u"highLocalCurveGain"_qs, load(runtime.adaptiveHighLocalCurveGain)},
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
                                  {u"baselineOutput"_qs, sample.baselineMappedOutput},
                                  {u"predictedMappedOutput"_qs, sample.predictedMappedOutput},
                                  {u"adaptiveOutput"_qs, sample.adaptiveOutput},
                                  {u"mappedLead"_qs, sample.mappedLead},
                                  {u"appliedLead"_qs, sample.appliedLead},
                                  {u"localCurveGain"_qs, sample.localCurveGain},
                                  {u"virtualOutput"_qs, sample.virtualOutput},
                                  {u"velocity"_qs, sample.velocity},
                                  {u"acceleration"_qs, sample.acceleration},
                                  {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
                                  {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
                                  {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                                      ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
                                   {u"requestedLead"_qs, sample.requestedLead},
                                   {u"cappedLead"_qs, sample.cappedLead},
                                   {u"endpointTaper"_qs, sample.endpointTaper},
                                   {u"lead"_qs, sample.lead},
                                   {u"confidence"_qs, sample.confidence},
                                   {u"motionIntensity"_qs, sample.motionIntensity},
                                   {u"deliberateMotionEvidence"_qs, sample.deliberateMotionEvidence},
                                   {u"normalMotionAuthority"_qs, sample.normalMotionAuthority},
                                   {u"rapidMotionAuthority"_qs, sample.rapidMotionAuthority},
                                   {u"rapidMotionBlend"_qs, sample.rapidMotionBlend},
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
            {u"baselineOutput"_qs, sample.baselineMappedOutput},
            {u"predictedMappedOutput"_qs, sample.predictedMappedOutput},
            {u"adaptiveOutput"_qs, sample.adaptiveOutput},
            {u"mappedLead"_qs, sample.mappedLead}, {u"appliedLead"_qs, sample.appliedLead},
            {u"localCurveGain"_qs, sample.localCurveGain},
            {u"virtualOutput"_qs, sample.virtualOutput}, {u"velocity"_qs, sample.velocity},
            {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"requestedLead"_qs, sample.requestedLead},
            {u"cappedLead"_qs, sample.cappedLead},
            {u"endpointTaper"_qs, sample.endpointTaper},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"deliberateMotionEvidence"_qs, sample.deliberateMotionEvidence},
            {u"normalMotionAuthority"_qs, sample.normalMotionAuthority},
            {u"rapidMotionAuthority"_qs, sample.rapidMotionAuthority},
            {u"rapidMotionBlend"_qs, sample.rapidMotionBlend},
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

void AppBackend::injectAdaptiveResponseLiveSampleForTest(int physicalAxis, float normalized)
{
    // Keep this at the control-plane/UI boundary: production DirectInput owns
    // the same snapshot in MappingWorker, and QML still polls it at display
    // cadence. The test request intentionally models a suspended mapper with
    // no vJoy output capability.
    m_mappingDesired = true;
    m_liveInputTestSuspended = true;
    m_worker.publishPhysicalAxisSnapshotForTest(physicalAxis, normalized);
    sampleAdaptiveResponseHistory();
    emit inputTelemetryChanged();
    emit stateChanged();
}

void AppBackend::setVirtualAxisAvailabilityForTest(bool available)
{
    m_worker.publishVirtualAxisAvailabilityForTest(available);
    emit stateChanged();
}

QVariantList AppBackend::runtimeAxisRoutesForTest() const
{
    QVariantList routes;
    const std::shared_ptr<const RuntimeProfileCache> compiled = m_worker.runtimeProfileCache();
    if (!compiled || compiled->profiles.empty()) return routes;
    const int profile = std::clamp(m_worker.runtime().effectiveProfileIndex.load(), 0,
        static_cast<int>(compiled->profiles.size()) - 1);
    const RuntimeMappingConfiguration &mapping = compiled->profiles[static_cast<size_t>(profile)];
    routes.reserve(kPhysicalAxisCount);
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        routes.append(QVariantMap{{u"index"_qs, axis},
            {u"target"_qs, virtualAxisLabel(
                mapping.axes[static_cast<size_t>(axis)].profile.target)}});
    }
    return routes;
}

void AppBackend::setSetupAssistantFactsForTest(const QVariantMap &facts)
{
    m_setupAssistantTestFacts = facts;
    // Synthetic scenarios describe an app-wide readiness snapshot. Keep a
    // preceding Device Rig action from leaking a stale presentation scope
    // into the test-only seam.
    if (!facts.isEmpty()) {
        const QString requestedScope = facts.value(u"scopeType"_qs).toString().trimmed();
        m_setupAssistantScopeType = requestedScope == u"device"_qs
                || requestedScope == u"deviceRig"_qs || requestedScope == u"virtualOutput"_qs
            ? requestedScope : u"application"_qs;
        m_setupAssistantScopeId = facts.value(u"scopeId"_qs).toString().trimmed();
        m_setupAssistantLiveTestActive = false;
    } else {
        m_setupAssistantScopeType = u"application"_qs;
        m_setupAssistantScopeId.clear();
        m_setupAssistantLiveTestActive = false;
    }
    emit stateChanged();
}
#ifdef HOTAS_STARTUP_TESTING
void AppBackend::setButtonUiFixtureForTest(int physicalButtonCount, int vjoyButtonCapacity,
                                           int physicalPovCount, int continuousPovCapacity)
{
    AtomicRuntimeState &runtime = m_worker.runtimeForTest();
    const int buttonCount = std::clamp(physicalButtonCount, 0, kMaximumPhysicalButtons);
    runtime.physicalConnected = buttonCount > 0 || physicalPovCount > 0;
    runtime.buttonCount = buttonCount;
    runtime.vjoyButtonCount = std::clamp(vjoyButtonCapacity, 0, kMaximumVirtualButtons);
    runtime.povCount = std::clamp(physicalPovCount, 0, kMaximumPhysicalPovs);
    runtime.vjoyContinuousPovCount = std::max(0, continuousPovCapacity);
    runtime.vjoyDiscretePovCount = 0;
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        runtime.buttonAvailable[static_cast<size_t>(index)] = index < buttonCount;
        runtime.physicalButtonPressed[static_cast<size_t>(index)] = false;
        runtime.virtualButtonPressed[static_cast<size_t>(index)] = false;
    }
    for (int index = 0; index < kMaximumPhysicalPovs; ++index) {
        runtime.povValues[static_cast<size_t>(index)] = -1;
    }
    rebuildButtonUiModel();
    emit inputTelemetryChanged();
    emit stateChanged();
}

bool AppBackend::configureActivationTransactionFixtureForTest()
{
    MapperConfiguration fixture = defaultConfiguration();
    ControllerProfile *normal = findProfile(fixture, normalProfileId());
    ControllerProfile *precision = findProfile(fixture, precisionProfileId());
    if (!normal || !precision || fixture.outputLayouts.empty()) return false;

    SavedControllerRecord record;
    record.id = u"activation-transaction-controller"_qs;
    record.displayName = u"Activation Transaction Fixture"_qs;
    record.lastDirectInputId = u"{activation-transaction-controller}"_qs;
    record.productGuid = u"{activation-transaction-product}"_qs;
    record.hidInstanceId = u"HID\\ACTIVATION_TRANSACTION\\1"_qs;
    record.axisCount = 3;
    record.buttonCount = 16;
    record.povCount = 1;
    record.axes[0] = true;
    record.axes[1] = true;
    record.axes[2] = true;
    record.lastVerified = u"2026-09-10T00:00:00Z"_qs;
    fixture.savedControllers = {record};

    VirtualOutputLayout alternate = fixture.outputLayouts.front();
    alternate.id = u"activation-transaction-output"_qs;
    alternate.name = u"Activation Transaction Output"_qs;
    alternate.requirements.deviceId = 2;
    alternate.hidhideManaged = false;
    alternate.hidHideDeviceInstanceId.clear();
    fixture.outputLayouts.push_back(alternate);

    DeviceRig rig;
    rig.id = u"activation-transaction-rig"_qs;
    rig.name = u"Activation Transaction Rig"_qs;
    rig.members = {{record.id, true, true, fixture.outputLayouts.front().id}};
    rig.outputs = {{fixture.outputLayouts.front().id, true}, {alternate.id, true}};
    fixture.deviceRigs = {rig};

    DeviceProfileMapping mapping;
    mapping.controllerRecordId = record.id;
    mapping.enabled = true;
    normal->deviceRigId = rig.id;
    normal->outputLayoutId = fixture.outputLayouts.front().id;
    normal->deviceMappings = {mapping};
    normal->automaticSelectionMode = ProfileAutomaticSelectionMode::Preferred;
    precision->deviceRigId = rig.id;
    precision->outputLayoutId = alternate.id;
    precision->deviceMappings = {mapping};
    precision->automaticSelectionMode = ProfileAutomaticSelectionMode::Fallback;
    fixture.activeProfileId = normal->id;
    fixture.activeDeviceRigId = rig.id;

    if (!compileDeviceRigRuntime(fixture, rig.id, normal->id).valid
        || !compileDeviceRigRuntime(fixture, rig.id, precision->id).valid
        || !ConfigStore::save(fixture)) return false;
    m_configuration = std::move(fixture);
    ++m_configurationGeneration;
    DeviceRigStatus status;
    status.rigId = rig.id;
    status.health = DeviceRigHealth::Ready;
    status.complete = true;
    m_deviceRigStatuses = {status};
    ++m_inventoryGeneration;
    ++m_gameContextGeneration;
    m_initialInventoryResolved = true;
    m_initialGameContextResolved = true;
    m_manualActivationOverride = false;
    m_manualOverrideProfileId.clear();
    m_manualOverrideCategoryId.clear();
    m_activationDegraded = false;
    m_activationFaultInjections.clear();
    m_activationTransactionTestBypassDriverConfiguration = true;
    m_worker.updateConfiguration(m_configuration);
    emit deviceRigsChanged();
    emit stateChanged();
    return true;
}

void AppBackend::setActivationFaultInjectionsForTest(const QStringList &stages)
{
    m_activationFaultInjections.clear();
    for (const QString &stage : stages) {
        const QString normalized = stage.trimmed().toCaseFolded();
        if (!normalized.isEmpty()) m_activationFaultInjections.insert(normalized);
    }
}
#endif

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
    propagateProfileAdaptiveResponseIfShared(scope, targetId, physicalAxis);
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
        propagateProfileAdaptiveResponseIfShared(scope, targetId, physicalAxis);
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
    else if (key == u"normalmovementresponse"_qs) override.settings.normalMovementResponse = static_cast<float>(value.toDouble());
    else if (key == u"rapidmovementresponse"_qs) override.settings.rapidMovementResponse = static_cast<float>(value.toDouble());
    else if (key == u"engagementsensitivity"_qs) override.settings.engagementSensitivity = static_cast<float>(value.toDouble());
    else return false;
    override.settings = sanitizedAdaptiveResponseSettings(override.settings);
    override.properties |= bit;
    propagateProfileAdaptiveResponseIfShared(scope, targetId, physicalAxis);
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
    propagateProfileAdaptiveResponseIfShared(scope, targetId, physicalAxis);
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
    RuntimeAdaptiveResponseConfig physicalPrediction = runtime;
    physicalPrediction.maximumLead = 0.50F;
    // Static Response Preview runs the same calibrated-normalized-centre
    // semantics as the mapper rather than displaying a single-valued static
    // deadzone transfer. A coherent centre transit therefore stays coherent.
    AxisCenterResolverState previewCenterResolver;
    AxisHysteresisState previewHysteresis;
    AdaptiveResponseProcessor previewProcessor;
    const auto previewOrigin = std::chrono::steady_clock::time_point{};
    QVariantList result;
    result.reserve(static_cast<qsizetype>(physical.size()));
    for (size_t index = 0; index < physical.size(); ++index) {
        const float resolved = resolveNormalizedAxisCenter(physical[index], mapping,
                                                            previewCenterResolver);
        const AdaptiveResponseTelemetry telemetry = previewProcessor.process(resolved, physicalPrediction,
            previewOrigin + std::chrono::milliseconds(static_cast<qint64>(index) * 4));
        const AdaptiveMappedAxisOutput mapped = applyCurveAwareAdaptiveResponse(
            telemetry.physical, telemetry.predicted, runtime.enabled,
            runtime.maximumLead, mapping, previewHysteresis);
        result.append(QVariantMap{{u"time"_qs, static_cast<double>(index) * 0.004},
            {u"physical"_qs, telemetry.physical}, {u"estimated"_qs, telemetry.estimated},
            {u"predicted"_qs, telemetry.predicted},
            {u"baselineOutput"_qs, mapped.baselineOutput},
            {u"predictedMappedOutput"_qs, mapped.predictedMappedOutput},
            {u"adaptiveOutput"_qs, mapped.adaptiveOutput},
            {u"virtualOutput"_qs, mapped.adaptiveOutput},
            {u"physicalLead"_qs, mapped.physicalLead}, {u"mappedLead"_qs, mapped.mappedLead},
            {u"appliedLead"_qs, mapped.appliedLead}, {u"localCurveGain"_qs, mapped.localCurveGain},
            {u"deadzoneAuthorityBlocked"_qs, mapped.deadzoneAuthorityBlocked},
            {u"leadLimited"_qs, mapped.leadLimited},
            {u"highLocalCurveGain"_qs, mapped.highLocalCurveGain},
            {u"lead"_qs, mapped.physicalLead}, {u"horizonMs"_qs, telemetry.activeHorizonSeconds * 1000.0F},
            {u"confidence"_qs, telemetry.confidence}, {u"velocity"_qs, telemetry.velocity},
            {u"acceleration"_qs, telemetry.acceleration}, {u"accelerationIntent"_qs, telemetry.accelerationIntent},
            {u"deliberateMotionEvidence"_qs, telemetry.deliberateMotionEvidence},
            {u"normalMotionAuthority"_qs, telemetry.normalMotionAuthority},
            {u"rapidMotionAuthority"_qs, telemetry.rapidMotionAuthority},
            {u"rapidMotionBlend"_qs, telemetry.rapidMotionBlend},
            {u"onsetAuthority"_qs, telemetry.onsetAuthority}, {u"sustainedEvidence"_qs, telemetry.sustainedEvidence},
            {u"sustainedAuthority"_qs, telemetry.sustainedAuthority}, {u"motionUrgency"_qs, telemetry.motionUrgency},
            {u"horizonExtensionEligibility"_qs, telemetry.horizonExtensionEligibility},
            {u"normalMaximumHorizonMs"_qs, telemetry.normalMaximumHorizonSeconds * 1000.0F},
            {u"allowedMaximumHorizonMs"_qs, telemetry.allowedMaximumHorizonSeconds * 1000.0F},
            {u"turningPointConfidence"_qs, telemetry.turningPointConfidence},
            {u"estimatedTimeToTurnMs"_qs, telemetry.estimatedTimeToTurnSeconds * 1000.0F},
            {u"estimatedRemainingTravel"_qs, telemetry.estimatedRemainingTravel},
            {u"turningPointHorizonLimitMs"_qs, telemetry.turningPointHorizonLimitSeconds * 1000.0F},
            {u"turningPointLeadLimit"_qs, telemetry.turningPointLeadLimit},
            {u"reacquisitionAuthority"_qs, telemetry.reacquisitionAuthority},
            {u"velocityAuthority"_qs, telemetry.velocityAuthority},
            {u"motionCoherence"_qs, telemetry.motionCoherence},
            {u"sourceUpdatePeriodMs"_qs, telemetry.sourceUpdatePeriodSeconds * 1000.0F},
            {u"quietDurationMs"_qs, telemetry.quietDurationSeconds * 1000.0F},
            {u"reversal"_qs, telemetry.reversal}, {u"state"_qs, adaptiveMotionStateLabel(telemetry.state)}});
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
            m_adaptiveResponseSimulatorResolvedInput = resolveNormalizedAxisCenter(
                m_adaptiveResponseSimulatorHeldInput, mapping,
                m_adaptiveResponseSimulatorCenterResolver);
        }
        const auto timestamp = std::chrono::steady_clock::time_point{}
            + std::chrono::milliseconds(m_adaptiveResponseSimulatorLastTickMs);
        RuntimeAdaptiveResponseConfig physicalPrediction = configuration;
        physicalPrediction.maximumLead = 0.50F;
        const AdaptiveResponseTelemetry telemetry = m_adaptiveResponseSimulator.process(
            m_adaptiveResponseSimulatorResolvedInput, physicalPrediction, timestamp);
        const AdaptiveMappedAxisOutput mapped = applyCurveAwareAdaptiveResponse(
            telemetry.physical, telemetry.predicted, configuration.enabled, configuration.maximumLead,
            mapping, m_adaptiveResponseSimulatorHysteresis);
        AdaptiveResponseSimulatorSample sample;
        sample.elapsedMs = m_adaptiveResponseSimulatorLastTickMs;
        sample.physical = telemetry.physical;
        sample.estimated = telemetry.estimated;
        sample.predicted = telemetry.predicted;
        sample.baselineMappedOutput = mapped.baselineOutput;
        sample.predictedMappedOutput = mapped.predictedMappedOutput;
        sample.adaptiveOutput = mapped.adaptiveOutput;
        sample.mappedLead = mapped.mappedLead;
        sample.appliedLead = mapped.appliedLead;
        sample.localCurveGain = mapped.localCurveGain;
        sample.deadzoneAuthorityBlocked = mapped.deadzoneAuthorityBlocked;
        sample.leadLimited = mapped.leadLimited;
        sample.highLocalCurveGain = mapped.highLocalCurveGain;
        sample.virtualOutput = mapped.adaptiveOutput;
        sample.velocity = telemetry.velocity;
        sample.acceleration = telemetry.acceleration;
        sample.activeHorizonSeconds = telemetry.activeHorizonSeconds;
        sample.maximumHorizonSeconds = configuration.maximumHorizonSeconds;
        sample.maximumLead = configuration.maximumLead;
        sample.requestedLead = telemetry.requestedLead;
        sample.cappedLead = telemetry.cappedLead;
        sample.endpointTaper = telemetry.endpointTaper;
        sample.lead = telemetry.lead;
        sample.confidence = telemetry.confidence;
        sample.motionIntensity = telemetry.motionIntensity;
        sample.velocityAuthority = telemetry.velocityAuthority;
        sample.deliberateMotionEvidence = telemetry.deliberateMotionEvidence;
        sample.normalMotionAuthority = telemetry.normalMotionAuthority;
        sample.rapidMotionAuthority = telemetry.rapidMotionAuthority;
        sample.rapidMotionBlend = telemetry.rapidMotionBlend;
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
            {u"predicted"_qs, sample.predicted},
            {u"baselineOutput"_qs, sample.baselineMappedOutput},
            {u"predictedMappedOutput"_qs, sample.predictedMappedOutput},
            {u"adaptiveOutput"_qs, sample.adaptiveOutput},
            {u"mappedLead"_qs, sample.mappedLead}, {u"appliedLead"_qs, sample.appliedLead},
            {u"localCurveGain"_qs, sample.localCurveGain},
            {u"deadzoneAuthorityBlocked"_qs, sample.deadzoneAuthorityBlocked},
            {u"leadLimited"_qs, sample.leadLimited},
            {u"highLocalCurveGain"_qs, sample.highLocalCurveGain},
            {u"virtualOutput"_qs, sample.virtualOutput},
            {u"velocity"_qs, sample.velocity}, {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"requestedLead"_qs, sample.requestedLead},
            {u"cappedLead"_qs, sample.cappedLead},
            {u"endpointTaper"_qs, sample.endpointTaper},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"deliberateMotionEvidence"_qs, sample.deliberateMotionEvidence},
            {u"normalMotionAuthority"_qs, sample.normalMotionAuthority},
            {u"rapidMotionAuthority"_qs, sample.rapidMotionAuthority},
            {u"rapidMotionBlend"_qs, sample.rapidMotionBlend},
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
            {u"baselineOutput"_qs, sample.baselineMappedOutput},
            {u"predictedMappedOutput"_qs, sample.predictedMappedOutput},
            {u"adaptiveOutput"_qs, sample.adaptiveOutput},
            {u"mappedLead"_qs, sample.mappedLead}, {u"appliedLead"_qs, sample.appliedLead},
            {u"localCurveGain"_qs, sample.localCurveGain},
            {u"deadzoneAuthorityBlocked"_qs, sample.deadzoneAuthorityBlocked},
            {u"leadLimited"_qs, sample.leadLimited},
            {u"highLocalCurveGain"_qs, sample.highLocalCurveGain},
            {u"virtualOutput"_qs, sample.virtualOutput}, {u"velocity"_qs, sample.velocity},
            {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"requestedLead"_qs, sample.requestedLead},
            {u"cappedLead"_qs, sample.cappedLead},
            {u"endpointTaper"_qs, sample.endpointTaper},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"deliberateMotionEvidence"_qs, sample.deliberateMotionEvidence},
            {u"normalMotionAuthority"_qs, sample.normalMotionAuthority},
            {u"rapidMotionAuthority"_qs, sample.rapidMotionAuthority},
            {u"rapidMotionBlend"_qs, sample.rapidMotionBlend},
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
    m_adaptiveResponseSimulatorHysteresis = {};
    m_adaptiveResponseSimulatorCenterResolver = {};
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
    m_adaptiveResponseSimulatorResolvedInput = 0.0F;
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
            {u"predicted"_qs, sample.predicted},
            {u"baselineOutput"_qs, sample.baselineMappedOutput},
            {u"predictedMappedOutput"_qs, sample.predictedMappedOutput},
            {u"adaptiveOutput"_qs, sample.adaptiveOutput},
            {u"mappedLead"_qs, sample.mappedLead}, {u"appliedLead"_qs, sample.appliedLead},
            {u"localCurveGain"_qs, sample.localCurveGain},
            {u"deadzoneAuthorityBlocked"_qs, sample.deadzoneAuthorityBlocked},
            {u"leadLimited"_qs, sample.leadLimited},
            {u"highLocalCurveGain"_qs, sample.highLocalCurveGain},
            {u"virtualOutput"_qs, sample.virtualOutput},
            {u"velocity"_qs, sample.velocity}, {u"acceleration"_qs, sample.acceleration},
            {u"activeHorizonMs"_qs, sample.activeHorizonSeconds * 1000.0F},
            {u"maximumHorizonMs"_qs, sample.maximumHorizonSeconds * 1000.0F},
            {u"maximumLead"_qs, sample.maximumLead},
            {u"horizonRatio"_qs, sample.maximumHorizonSeconds > 0.0001F
                ? sample.activeHorizonSeconds / sample.maximumHorizonSeconds : 0.0F},
            {u"lead"_qs, sample.lead}, {u"confidence"_qs, sample.confidence},
            {u"motionIntensity"_qs, sample.motionIntensity},
            {u"deliberateMotionEvidence"_qs, sample.deliberateMotionEvidence},
            {u"normalMotionAuthority"_qs, sample.normalMotionAuthority},
            {u"rapidMotionAuthority"_qs, sample.rapidMotionAuthority},
            {u"rapidMotionBlend"_qs, sample.rapidMotionBlend},
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const ButtonBindings &activeBindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (!runtime.buttonAvailable[source].load()) continue;
        const ButtonBinding binding = source < static_cast<int>(activeBindings.size())
            ? activeBindings[static_cast<size_t>(source)] : ButtonBinding{};
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const NativePovBindings &nativeBindings = deviceMapping ? deviceMapping->nativePovBindings
                                                             : m_configuration.nativePovBindings;
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
        const NativePovBinding binding = hat < static_cast<int>(nativeBindings.size())
            ? nativeBindings[static_cast<size_t>(hat)] : NativePovBinding{};
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const PovBindings &bindings = deviceMapping ? deviceMapping->povs : currentProfile().povs;
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
        item.insert(u"automaticSelectionMode"_qs,
                    profileAutomaticSelectionModeKey(profile.automaticSelectionMode));
        item.insert(u"automaticSelectionLabel"_qs,
                    profileAutomaticSelectionModeLabel(profile.automaticSelectionMode));
        item.insert(u"effective"_qs, profile.id == effectiveProfileId());
        item.insert(u"effectiveSource"_qs, profile.id == effectiveProfileId()
            ? profileSourceLabel() : QString{});
        item.insert(u"protected"_qs, profile.id == normalProfileId());
        item.insert(u"mappedAxes"_qs, mappedAxes);
        item.insert(u"mappedButtons"_qs, mappedButtons);
        item.insert(u"mappedPovs"_qs, mappedPovs);
        item.insert(u"customCurves"_qs, customCurves);
        item.insert(u"automationCount"_qs, automationCount);
        const int profileAdaptiveOverrides = adaptiveOverrideAxisCount(profile.adaptiveResponse);
        const int categoryAdaptiveOverrides = category
            ? adaptiveOverrideAxisCount(category->adaptiveResponse) : 0;
        item.insert(u"adaptiveOverrideAxes"_qs, profileAdaptiveOverrides);
        item.insert(u"adaptiveSource"_qs, profileAdaptiveOverrides > 0
            ? u"Custom profile response"_qs
            : categoryAdaptiveOverrides > 0 ? u"Category response defaults"_qs
                                        : u"Global response defaults"_qs);
        item.insert(u"curveTransitionInherited"_qs, !profile.curveTransitionSmoothingOverride);
        const VirtualOutputLayout *layout = findOutputLayout(m_configuration, profile.outputLayoutId);
        const DeviceRig *rig = findDeviceRig(m_configuration, profile.deviceRigId);
        const auto rigStatus = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [&profile](const DeviceRigStatus &status) { return status.rigId == profile.deviceRigId; });
        item.insert(u"deviceRigId"_qs, profile.deviceRigId);
        item.insert(u"deviceRigName"_qs, rig ? rig->name : u"Device Rig assignment required"_qs);
        item.insert(u"deviceRigReady"_qs, rigStatus != m_deviceRigStatuses.cend() && rigStatus->complete
            && rigStatus->ambiguousRequiredMemberIds.isEmpty()
            && rigStatus->needsVerificationRequiredMemberIds.isEmpty());
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
        QStringList profileIds;
        profileIds.reserve(static_cast<qsizetype>(category.profileIds.size()));
        for (const QString &profileId : category.profileIds) profileIds.append(profileId);
        item.insert(u"profileIds"_qs, profileIds);
        item.insert(u"executableRules"_qs, category.executableRules);
        item.insert(u"adaptiveOverrideAxes"_qs, adaptiveOverrideAxisCount(category.adaptiveResponse));
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

void AppBackend::publishProfilePresentationIfChanged()
{
    const QString effectiveName = effectiveProfileName();
    const QString effectiveDisplayName = effectiveProfileDisplayName();
    const QString source = profileSourceLabel();
    if (effectiveName == m_presentedEffectiveProfileName
        && effectiveDisplayName == m_presentedEffectiveProfileDisplayName
        && source == m_presentedProfileSourceLabel) {
        return;
    }
    m_presentedEffectiveProfileName = effectiveName;
    m_presentedEffectiveProfileDisplayName = effectiveDisplayName;
    m_presentedProfileSourceLabel = source;
    emit profilePresentationChanged();
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
        const SavedControllerRecord *record = match.recordId.isEmpty() || match.ambiguous
            ? nullptr : savedControllerRecord(match.recordId);
        const bool known = record != nullptr;
        const bool verified = known && !record->lastVerified.isEmpty();
        const bool selected = known && match.recordId == m_configuration.activeControllerRecordId;
        const bool active = controller.connected && controller.directInputId == liveId;
        QVariantMap item{{u"id"_qs, match.recordId}, {u"directInputId"_qs, controller.directInputId},
                         {u"name"_qs, controller.name}, {u"connected"_qs, controller.connected},
                         {u"verified"_qs, verified}, {u"selected"_qs, selected},
                         {u"ambiguous"_qs, match.ambiguous}, {u"active"_qs, active},
                         {u"axisCount"_qs, controller.axisCount}, {u"buttonCount"_qs, controller.buttonCount},
                         {u"povCount"_qs, controller.povCount}};
        item.insert(u"state"_qs, match.ambiguous ? u"Connected · Selection required"_qs
            : match.recordId.isEmpty() ? u"Connected · New device"_qs
            : !verified ? u"Connected · Needs verification"_qs
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
            {u"name"_qs, record.displayName}, {u"connected"_qs, false},
            {u"verified"_qs, !record.lastVerified.isEmpty()},
            {u"selected"_qs, record.id == m_configuration.activeControllerRecordId},
            {u"ambiguous"_qs, false}, {u"active"_qs, false},
            {u"axisCount"_qs, record.axisCount}, {u"buttonCount"_qs, record.buttonCount},
            {u"povCount"_qs, record.povCount},
            {u"state"_qs, record.id == m_configuration.activeControllerRecordId
                ? (!record.lastVerified.isEmpty() ? u"Selected · Offline · Verified"_qs
                                                   : u"Selected · Offline · Needs verification"_qs)
                : (!record.lastVerified.isEmpty() ? u"Offline · Verified"_qs
                                                   : u"Offline · Needs verification"_qs)}});
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : profile.axes;
    choices.reserve(kPhysicalAxisCount);
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const PhysicalAxis axis = static_cast<PhysicalAxis>(index);
        const QString customName = axes[static_cast<size_t>(index)].customName.trimmed();
        choices.append(QVariantMap{{u"index"_qs, index},
            {u"label"_qs, customName.isEmpty() ? physicalAxisLabel(axis) : customName}});
    }
    m_curveAxisChoices = std::move(choices);
}

QString AppBackend::activeControllerRecordId() const { return m_configuration.activeControllerRecordId; }

const SavedControllerRecord *AppBackend::savedControllerRecord(const QString &recordId) const
{
    const auto found = std::find_if(m_configuration.savedControllers.cbegin(),
        m_configuration.savedControllers.cend(), [&recordId](const SavedControllerRecord &record) {
            return record.id == recordId;
        });
    return found == m_configuration.savedControllers.cend() ? nullptr : &*found;
}

DeviceRig *AppBackend::activeDeviceRig()
{
    return findDeviceRig(m_configuration, m_configuration.activeDeviceRigId);
}

const DeviceRig *AppBackend::activeDeviceRig() const
{
    return findDeviceRig(m_configuration, m_configuration.activeDeviceRigId);
}

const DeviceRig *AppBackend::setupAssistantDeviceRig(const QString &scopeType,
                                                      const QString &scopeId) const
{
    if (scopeType == u"deviceRig"_qs) return findDeviceRig(m_configuration, scopeId);
    if (scopeType == u"device"_qs) {
        const auto found = std::find_if(m_configuration.deviceRigs.cbegin(),
            m_configuration.deviceRigs.cend(), [&scopeId](const DeviceRig &rig) {
                return std::any_of(rig.members.cbegin(), rig.members.cend(), [&scopeId](const DeviceRigMember &member) {
                    return member.controllerRecordId == scopeId;
                });
            });
        return found == m_configuration.deviceRigs.cend() ? nullptr : &*found;
    }
    if (scopeType == u"virtualOutput"_qs) {
        const auto found = std::find_if(m_configuration.deviceRigs.cbegin(),
            m_configuration.deviceRigs.cend(), [&scopeId](const DeviceRig &rig) {
                return std::any_of(rig.outputs.cbegin(), rig.outputs.cend(), [&scopeId](const DeviceRigOutputTarget &output) {
                    return output.outputLayoutId == scopeId;
                });
            });
        return found == m_configuration.deviceRigs.cend() ? nullptr : &*found;
    }
    return activeDeviceRig();
}

QVariantList AppBackend::deviceRigs() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_configuration.deviceRigs.size()));
    for (const DeviceRig &rig : m_configuration.deviceRigs) {
        const auto status = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [&rig](const DeviceRigStatus &candidate) { return candidate.rigId == rig.id; });
        const DeviceRigStatus fallback{rig.id, rig.enabled ? DeviceRigHealth::Offline : DeviceRigHealth::Disabled};
        const DeviceRigStatus &health = status == m_deviceRigStatuses.cend() ? fallback : *status;
        // This is intentionally a UI/control-plane compilation. "In use" is
        // not Rig membership: it means that the current effective Profile has
        // a compiled, meaningful route for this member.
        const CompiledDeviceRigRuntime currentRoutes = compileDeviceRigRuntime(
            m_configuration, rig.id, effectiveProfileId());
        const auto compiledMemberHasRoute = [](const CompiledDeviceRigMember &compiled) {
            if (std::any_of(compiled.mapping.axes.cbegin(), compiled.mapping.axes.cend(),
                            [](const RuntimeAxisMapping &axis) {
                                return axis.profile.target != VirtualAxis::Disabled;
                            })) return true;
            if (std::any_of(compiled.mapping.buttons.cbegin(), compiled.mapping.buttons.cend(),
                            [](const ButtonBinding &button) {
                                return button.type == ButtonActionType::VirtualButton;
                            })) return true;
            return std::any_of(compiled.mapping.povs.cbegin(), compiled.mapping.povs.cend(),
                               [](const auto &hat) {
                                   return std::any_of(hat.cbegin(), hat.cend(),
                                                      [](const ButtonBinding &button) {
                                                          return button.type == ButtonActionType::VirtualButton;
                                                      });
                               });
        };
        QVariantList members;
        for (const DeviceRigMember &member : rig.members) {
            const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
            const QVariantMap detail = physicalDeviceDetail(member.controllerRecordId);
            const bool connected = health.connectedMemberIds.contains(member.controllerRecordId);
            QString seenIdentity;
            for (const DiscoveredController &candidate : m_discoveredControllers) {
                const ControllerMatch match = ControllerManager::match(candidate,
                                                                         m_configuration.savedControllers);
                if (!match.ambiguous && match.recordId == member.controllerRecordId
                    && candidate.connected) {
                    seenIdentity = candidate.directInputId;
                    break;
                }
            }
            const bool inUse = currentRoutes.valid && std::any_of(
                currentRoutes.members.cbegin(),
                currentRoutes.members.cbegin() + currentRoutes.memberCount,
                [&member, &compiledMemberHasRoute](const CompiledDeviceRigMember &compiled) {
                    return compiled.controllerRecordId == member.controllerRecordId
                        && compiledMemberHasRoute(compiled);
                });
            const bool visibilityManaged = detail.value(u"managedVisibility"_qs, false).toBool();
            const bool visibilityKnown = detail.value(u"visibilityKnown"_qs, false).toBool();
            members.append(QVariantMap{{u"id"_qs, member.controllerRecordId},
                {u"name"_qs, record ? record->displayName : u"Unknown device"_qs},
                {u"enabled"_qs, member.enabled}, {u"required"_qs, member.required},
                {u"connected"_qs, connected}, {u"inUse"_qs, inUse},
                {u"unused"_qs, !inUse},
                {u"expectedIdentity"_qs, record ? record->lastDirectInputId : QString{}},
                {u"seenIdentity"_qs, seenIdentity},
                {u"ambiguous"_qs, health.ambiguousMemberIds.contains(member.controllerRecordId)},
                {u"verified"_qs, record && !record->lastVerified.isEmpty()},
                // The Devices page and the persistent Device Context use this
                // one configuration-owned scope. It deliberately says
                // nothing about the active runtime rig.
                {u"editing"_qs, rig.id == m_configuration.editingDeviceRigId
                    && m_configuration.editingDeviceRecordIds.contains(member.controllerRecordId)},
                {u"needsVerification"_qs, health.needsVerificationMemberIds.contains(member.controllerRecordId)},
                {u"preferredOutputLayoutId"_qs, member.preferredOutputLayoutId},
                {u"visibilityManaged"_qs, visibilityManaged},
                {u"hiddenFromGames"_qs, detail.value(u"hiddenFromGames"_qs, false)},
                {u"visibilityKnown"_qs, visibilityKnown},
                {u"isolationNeedsAttention"_qs, !visibilityManaged || !visibilityKnown}});
        }
        QVariantList outputs;
        for (const DeviceRigOutputTarget &target : rig.outputs) {
            const VirtualOutputLayout *layout = findOutputLayout(m_configuration, target.outputLayoutId);
            // The landing-page projection needs the same compact readiness
            // summary as the output detail view.  This is a UI/control-plane
            // snapshot, never a mapper/report-path query.
            const QVariantMap detail = virtualOutputDetail(target.outputLayoutId);
            outputs.append(QVariantMap{{u"id"_qs, target.outputLayoutId},
                {u"name"_qs, layout ? layout->name : u"Unavailable output"_qs},
                {u"deviceId"_qs, layout ? layout->requirements.deviceId : 0},
                {u"enabled"_qs, target.enabled},
                {u"ready"_qs, detail.value(u"ready"_qs, false)},
                {u"status"_qs, detail.value(u"status"_qs, u"Output unavailable"_qs)},
                {u"routeCount"_qs, detail.value(u"routeCount"_qs, 0)},
                {u"visibilityManaged"_qs, detail.value(u"managedVisibility"_qs, false)},
                {u"hiddenFromGames"_qs, detail.value(u"hiddenFromGames"_qs, false)},
                {u"visibilityKnown"_qs, detail.value(u"visibilityKnown"_qs, false)}});
        }
        const bool configured = rig.id == m_configuration.activeDeviceRigId;
        // A configured pair is not described as active until MappingWorker is
        // actually routing it.  This keeps selection, readiness, and live
        // mapper use visibly distinct in Devices and Flight Deck.
        const bool inUse = configured && m_worker.runtime().mappingActive.load();
        result.append(QVariantMap{{u"id"_qs, rig.id}, {u"name"_qs, rig.name},
            {u"enabled"_qs, rig.enabled}, {u"default"_qs, rig.isDefault},
            {u"autoActivate"_qs, rig.autoActivate}, {u"activationPriority"_qs, rig.activationPriority},
            {u"fallbackRigId"_qs, rig.fallbackRigId},
            {u"disconnectBehavior"_qs, static_cast<int>(rig.disconnectBehavior)},
            {u"health"_qs, deviceRigHealthKey(health.health)},
            {u"healthLabel"_qs, deviceRigHealthLabel(health.health)},
            {u"complete"_qs, health.complete}, {u"configured"_qs, configured},
            {u"inUse"_qs, inUse}, {u"active"_qs, inUse},
            {u"editing"_qs, rig.id == m_configuration.editingDeviceRigId},
            {u"members"_qs, members}, {u"outputs"_qs, outputs}});
    }
    return result;
}

QString AppBackend::activeDeviceRigId() const { return m_configuration.activeDeviceRigId; }

QString AppBackend::activeDeviceRigName() const
{
    if (const DeviceRig *rig = activeDeviceRig()) return rig->name;
    return u"No active Device Rig"_qs;
}

QString AppBackend::editingDeviceRigId() const { return m_configuration.editingDeviceRigId; }

QString AppBackend::editingDeviceRigName() const
{
    if (const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId)) {
        return rig->name;
    }
    return u"Device Rig"_qs;
}

QString AppBackend::editingScopeLabel() const
{
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    if (!rig || m_configuration.editingDeviceRecordIds.isEmpty()) return u"All Devices"_qs;
    if (m_configuration.editingDeviceRecordIds.size() == 1) {
        if (const SavedControllerRecord *record = savedControllerRecord(
                m_configuration.editingDeviceRecordIds.front())) return record->displayName;
        return u"One Device"_qs;
    }
    return QString(u"%1 Devices"_qs).arg(m_configuration.editingDeviceRecordIds.size());
}

QVariantList AppBackend::editingDevices() const
{
    QVariantList result;
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    if (!rig) return result;
    for (const DeviceRigMember &member : rig->members) {
        const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
        result.append(QVariantMap{{u"id"_qs, member.controllerRecordId},
            {u"name"_qs, record ? record->displayName : u"Unknown device"_qs},
            {u"selected"_qs, m_configuration.editingDeviceRecordIds.contains(member.controllerRecordId)},
            {u"required"_qs, member.required}, {u"enabled"_qs, member.enabled}});
    }
    return result;
}

QString AppBackend::selectedDeviceRigId() const { return editingDeviceRigId(); }
QString AppBackend::selectedDeviceRigName() const { return editingDeviceRigName(); }
QString AppBackend::selectedDeviceLabel() const { return editingScopeLabel(); }
QVariantList AppBackend::selectedDevices() const { return editingDevices(); }

QString AppBackend::deviceRigMigrationWarning() const { return m_configuration.deviceRigMigrationWarning; }
QString AppBackend::deviceRigDetectionMessage() const { return m_deviceRigDetectionMessage; }

QVariantMap AppBackend::physicalDeviceDetail(const QString &recordId) const
{
    const SavedControllerRecord *record = savedControllerRecord(recordId.trimmed());
    if (!record) return {};
    const DiscoveredController *discovered = nullptr;
    for (const DiscoveredController &candidate : m_discoveredControllers) {
        const ControllerMatch match = ControllerManager::match(candidate, m_configuration.savedControllers);
        if (!match.ambiguous && match.recordId == record->id) {
            discovered = &candidate;
            break;
        }
    }
    QStringList rigNames;
    int mappedAxes = 0;
    int mappedButtons = 0;
    int mappedPovs = 0;
    for (const DeviceRig &rig : m_configuration.deviceRigs) {
        if (std::any_of(rig.members.cbegin(), rig.members.cend(), [record](const DeviceRigMember &member) {
                return member.controllerRecordId == record->id;
            })) rigNames.append(rig.name);
    }
    for (const ControllerProfile &profile : m_configuration.profiles) {
        const DeviceProfileMapping *mapping = findDeviceProfileMapping(profile, record->id);
        if (!mapping) continue;
        for (const AxisMapping &axis : mapping->axes) {
            if (axis.target != VirtualAxis::Disabled) ++mappedAxes;
        }
        for (const ButtonBinding &button : mapping->buttons) {
            if (button.type == ButtonActionType::VirtualButton) ++mappedButtons;
        }
        for (const auto &hat : mapping->povs) {
            for (const ButtonBinding &binding : hat) {
                if (binding.type == ButtonActionType::VirtualButton) ++mappedPovs;
            }
        }
    }
    int calibratedAxes = 0;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (record->axes[static_cast<size_t>(axis)] && record->calibration[static_cast<size_t>(axis)].enabled) {
            ++calibratedAxes;
        }
    }
    const HidHideCapabilities &hidhide = m_readiness.plan().hidhide;
    QStringList managedInstances = record->ownedHidHideDeviceInstances;
    for (QString &instance : managedInstances) {
        instance = ControllerReadinessService::normalizeDeviceInstanceId(instance);
    }
    managedInstances.removeAll(QString{});
    const bool managed = !managedInstances.isEmpty();
    const bool hidden = managed && hidhide.cloakKnown && std::all_of(
        managedInstances.cbegin(), managedInstances.cend(), [&hidhide](const QString &instance) {
            return std::any_of(hidhide.hiddenDeviceInstanceIds.cbegin(), hidhide.hiddenDeviceInstanceIds.cend(),
                [&instance](const QString &entry) {
                    return ControllerReadinessService::normalizeDeviceInstanceId(entry) == instance;
                });
        });
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    const bool currentConnection = discovered && discovered->connected
        && record->lastDirectInputId == physical.directInputId;
    // This counter is reset by a physical acquisition/disconnect, never when
    // the assistant opens. Reading it here stays on the GUI control plane.
    const bool inputDetected = currentConnection
        && m_worker.runtime().meaningfulInputSequence.load(std::memory_order_relaxed) > 0;
    return {{u"id"_qs, record->id}, {u"name"_qs, record->displayName},
            {u"connected"_qs, discovered && discovered->connected},
            {u"verified"_qs, !record->lastVerified.isEmpty()},
            {u"lastVerified"_qs, record->lastVerified}, {u"lastSeen"_qs, record->lastSeen},
            {u"axisCount"_qs, record->axisCount}, {u"buttonCount"_qs, record->buttonCount},
            {u"povCount"_qs, record->povCount}, {u"calibratedAxes"_qs, calibratedAxes},
            {u"rigs"_qs, rigNames.join(u" · "_qs)}, {u"mappedAxes"_qs, mappedAxes},
            {u"mappedButtons"_qs, mappedButtons}, {u"mappedPovs"_qs, mappedPovs},
            {u"hidhideManaged"_qs, managed}, {u"managedVisibility"_qs, managed},
            {u"visibilityKnown"_qs, managed && hidhide.cloakKnown},
            {u"hiddenFromGames"_qs, hidden},
            {u"inputDetected"_qs, inputDetected},
            {u"activityStatus"_qs, !discovered || !discovered->connected ? u"Offline"_qs
                : inputDetected ? u"Input detected"_qs : u"Listening for controller input…"_qs},
            {u"calibrationStatus"_qs, calibratedAxes > 0
                ? QString(u"Custom calibration · %1 axes"_qs).arg(calibratedAxes)
                : u"Using default controller range"_qs},
            {u"hidInstanceId"_qs, record->hidInstanceId},
            {u"hidContainerId"_qs, record->hidContainerId},
            {u"directInputId"_qs, record->lastDirectInputId},
            {u"capabilityFingerprint"_qs, record->capabilityFingerprint}};
}

QVariantMap AppBackend::virtualOutputDetail(const QString &layoutId) const
{
    const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId.trimmed());
    if (!layout) return {};
    QStringList axes;
    QStringList rigNames;
    int routeCount = 0;
    for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
        if (layout->requirements.axes[static_cast<size_t>(axis)]) {
            axes.append(virtualAxisLabel(static_cast<VirtualAxis>(axis)));
        }
    }
    for (const DeviceRig &rig : m_configuration.deviceRigs) {
        const bool used = std::any_of(rig.outputs.cbegin(), rig.outputs.cend(), [layout](const auto &output) {
            return output.outputLayoutId == layout->id;
        });
        if (used) rigNames.append(rig.name);
    }
    const auto countMappingRoutes = [&routeCount](const DeviceProfileMapping &mapping) {
        for (const AxisMapping &axis : mapping.axes) {
            if (axis.target != VirtualAxis::Disabled) ++routeCount;
        }
        for (const ButtonBinding &button : mapping.buttons) {
            if (button.type == ButtonActionType::VirtualButton) ++routeCount;
        }
        for (const auto &pov : mapping.povs) {
            for (const ButtonBinding &binding : pov) {
                if (binding.type == ButtonActionType::VirtualButton) ++routeCount;
            }
        }
    };
    for (const ControllerProfile &profile : m_configuration.profiles) {
        const DeviceRig *profileRig = findDeviceRig(m_configuration, profile.deviceRigId);
        if (!profileRig) {
            if (profile.outputLayoutId == layout->id) {
                for (const DeviceProfileMapping &mapping : profile.deviceMappings) countMappingRoutes(mapping);
            }
            continue;
        }
        for (const DeviceProfileMapping &mapping : profile.deviceMappings) {
            const auto member = std::find_if(profileRig->members.cbegin(), profileRig->members.cend(),
                [&mapping](const DeviceRigMember &candidate) {
                    return candidate.controllerRecordId == mapping.controllerRecordId;
                });
            if (member == profileRig->members.cend()) continue;
            const QString target = member->preferredOutputLayoutId.isEmpty()
                ? profile.outputLayoutId : member->preferredOutputLayoutId;
            if (target == layout->id) countMappingRoutes(mapping);
        }
    }
    const HidHideCapabilities &hidhide = m_readiness.plan().hidhide;
    const QString normalizedOutputInstance = ControllerReadinessService::normalizeDeviceInstanceId(
        layout->hidHideDeviceInstanceId);
    const bool hidden = layout->hidhideManaged && hidhide.cloakKnown
        && !normalizedOutputInstance.isEmpty()
        && std::any_of(hidhide.hiddenDeviceInstanceIds.cbegin(), hidhide.hiddenDeviceInstanceIds.cend(),
            [&normalizedOutputInstance](const QString &entry) {
                return ControllerReadinessService::normalizeDeviceInstanceId(entry) == normalizedOutputInstance;
            });
    const ControllerReadinessPlan *readiness = virtualOutputReadinessPlan(layout->id);
    const bool inspected = readiness != nullptr;
    const bool outputReady = inspected && readiness->vjoy.installed
        && readiness->vjoy.configurationUtilityAvailable && readiness->vjoy.driverReady
        && readiness->vjoy.devicePresent && (!readiness->vjoy.busy || readiness->vjoy.ownedByHotasBf6)
        && !readiness->vjoyNeedsChanges && !hidden;
    const QString readinessState = !inspected ? u"SAVED"_qs
        : !readiness->vjoy.installed || !readiness->vjoy.devicePresent ? u"OFFLINE"_qs
        : readiness->vjoy.busy && !readiness->vjoy.ownedByHotasBf6 ? u"BUSY"_qs
        : readiness->vjoyNeedsChanges ? u"SETUP NEEDED"_qs
        : hidden ? u"SETUP NEEDED"_qs : u"READY"_qs;
    const QString readinessStatus = !inspected
        ? u"Saved output — choose Check Output to inspect this vJoy device."_qs
        : hidden ? u"This virtual output is hidden from games."_qs
        : readiness->vjoySummary;
    return {{u"id"_qs, layout->id}, {u"name"_qs, layout->name},
            {u"deviceId"_qs, layout->requirements.deviceId}, {u"axes"_qs, axes.join(u" · "_qs)},
            {u"buttons"_qs, layout->requirements.buttons},
            {u"continuousPovs"_qs, layout->requirements.continuousPovs},
            {u"discretePovs"_qs, layout->requirements.discretePovs},
            {u"rigs"_qs, rigNames.join(u" · "_qs)}, {u"routeCount"_qs, routeCount},
            {u"managedVisibility"_qs, layout->hidhideManaged},
            {u"visibilityPrepared"_qs, !layout->hidHideDeviceInstanceId.isEmpty()},
            {u"visibilityKnown"_qs, layout->hidhideManaged && hidhide.cloakKnown},
            {u"hiddenFromGames"_qs, hidden},
            {u"ready"_qs, outputReady}, {u"inspected"_qs, inspected},
            {u"readinessState"_qs, readinessState}, {u"status"_qs, readinessStatus}};
}

const ControllerReadinessPlan *AppBackend::virtualOutputReadinessPlan(const QString &layoutId) const
{
    const auto cached = m_virtualOutputReadinessPlans.constFind(layoutId);
    if (cached != m_virtualOutputReadinessPlans.cend()) return &cached.value();
    const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
    const ControllerReadinessPlan &plan = m_readiness.plan();
    if (layout && plan.vjoy.deviceId == layout->requirements.deviceId && plan.lastChecked.isValid()) {
        return &plan;
    }
    return nullptr;
}

void AppBackend::refreshVirtualOutputReadiness(const QString &layoutId)
{
    const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
    if (!layout) return;
    MapperConfiguration scoped = m_configuration;
    scoped.vjoyDeviceId = layout->requirements.deviceId;
    ControllerReadinessService inspector;
    const ControllerReadinessPlan &plan = inspector.inspectForRequirements(
        scoped, currentPhysicalCapabilities(),
        ControllerReadinessService::requirementsFor(layout->requirements));
    m_virtualOutputReadinessPlans.insert(layout->id, plan);
}

void AppBackend::refreshSelectedRigOutputReadiness()
{
    const DeviceRig *rig = activeDeviceRig();
    if (!rig && !m_configuration.editingDeviceRigId.isEmpty()) {
        rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    }
    if (!rig) return;

    QSet<QString> inspected;
    for (const DeviceRigOutputTarget &target : rig->outputs) {
        if (!target.enabled || target.outputLayoutId.isEmpty() || inspected.contains(target.outputLayoutId)) continue;
        inspected.insert(target.outputLayoutId);
        // The full verifier has already inspected the active output on its
        // worker thread. Preserve that exact read-back rather than spawning a
        // redundant utility process. Other enabled rig outputs are inspected
        // independently so one ready Device 1 cannot mask Device 2.
        if (const VirtualOutputLayout *active = activeOutputLayout(); active
            && active->id == target.outputLayoutId
            && m_readiness.plan().vjoy.deviceId == active->requirements.deviceId) {
            m_virtualOutputReadinessPlans.insert(active->id, m_readiness.plan());
        } else {
            refreshVirtualOutputReadiness(target.outputLayoutId);
        }
    }
}

QString AppBackend::createDeviceRig(const QString &name, const QStringList &controllerRecordIds,
                                    const QString &outputLayoutId)
{
    const QString trimmedName = name.trimmed().left(64);
    if (trimmedName.isEmpty() || controllerRecordIds.isEmpty()
        || controllerRecordIds.size() > kMaximumDeviceRigMembers) return {};
    if (std::any_of(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
                    [&trimmedName](const DeviceRig &rig) {
                        return rig.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
                    })) return {};
    const QString selectedOutput = outputLayoutId.trimmed().isEmpty()
        ? currentProfile().outputLayoutId : outputLayoutId.trimmed();
    if (!findOutputLayout(m_configuration, selectedOutput)) return {};
    QSet<QString> uniqueIds;
    DeviceRig rig;
    rig.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rig.name = trimmedName;
    rig.isDefault = m_configuration.deviceRigs.empty();
    rig.presentationOrder = static_cast<int>(m_configuration.deviceRigs.size());
    for (const QString &recordId : controllerRecordIds) {
        const QString id = recordId.trimmed();
        if (!savedControllerRecord(id) || uniqueIds.contains(id)) return {};
        uniqueIds.insert(id);
        rig.members.push_back({id, true, true, selectedOutput});
    }
    rig.outputs.push_back({selectedOutput, true});
    m_configuration.deviceRigs.push_back(rig);
    if (m_configuration.editingDeviceRigId.isEmpty()) {
        m_configuration.editingDeviceRigId = rig.id;
        m_configuration.editingDeviceRecordIds = controllerRecordIds;
    }
    for (ControllerProfile &profile : m_configuration.profiles) {
        if (profile.deviceRigId.isEmpty()) profile.deviceRigId = rig.id;
        for (const DeviceRigMember &member : rig.members) {
            DeviceProfileMapping &mapping = ensureDeviceProfileMapping(profile, member.controllerRecordId);
            if (mapping.nativePovBindings.empty()) mapping.nativePovBindings = m_configuration.nativePovBindings;
        }
    }
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    appendEvent(QString(u"Created Device Rig: %1"_qs).arg(rig.name));
    emit deviceRigsChanged();
    return rig.id;
}

QVariantMap AppBackend::createDeviceRigResult(const QString &name,
                                              const QStringList &controllerRecordIds,
                                              const QString &outputLayoutId)
{
    const auto result = [](bool success, const QString &title, const QString &message,
                           const QString &objectId = {}, const QString &nextAction = {}) {
        return actionResult(success, title, message, u"deviceRig"_qs, objectId, nextAction);
    };
    const QString trimmedName = name.trimmed().left(64);
    if (trimmedName.isEmpty()) {
        return result(false, u"Device Rig was not created"_qs,
                      u"Enter a name for the Device Rig."_qs);
    }
    if (controllerRecordIds.isEmpty()) {
        return result(false, u"Device Rig needs a physical controller"_qs,
                      u"Connect a controller to create your first Device Rig."_qs);
    }
    if (controllerRecordIds.size() > kMaximumDeviceRigMembers) {
        return result(false, u"Too many physical controllers"_qs,
                      QString(u"A Device Rig can contain at most %1 physical controllers."_qs)
                          .arg(kMaximumDeviceRigMembers));
    }
    const QString selectedOutput = outputLayoutId.trimmed().isEmpty()
        ? currentProfile().outputLayoutId : outputLayoutId.trimmed();
    if (!findOutputLayout(m_configuration, selectedOutput)) {
        return result(false, u"Device Rig needs a virtual output"_qs,
                      u"Choose a saved virtual output before creating this Device Rig."_qs);
    }
    if (std::any_of(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
                    [&trimmedName](const DeviceRig &rig) {
                        return rig.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
                    })) {
        return result(false, u"Device Rig name is already used"_qs,
                      u"Choose a different name for this Device Rig."_qs);
    }

    QStringList recordIds;
    QSet<QString> seen;
    QSet<QString> createdRecordIds;
    std::vector<SavedControllerRecord> newRecords;
    bool needsSetup = false;
    for (const QString &entry : controllerRecordIds) {
        const QString value = entry.trimmed();
        if (value.isEmpty() || seen.contains(value)) {
            return result(false, u"Physical controller selection is invalid"_qs,
                          u"Choose each physical controller only once."_qs);
        }
        seen.insert(value);
        if (const SavedControllerRecord *saved = savedControllerRecord(value)) {
            recordIds.append(saved->id);
            needsSetup = needsSetup || saved->lastVerified.isEmpty();
            continue;
        }
        const DiscoveredController *discovered = discoveredController(value);
        if (!discovered || !discovered->connected || discovered->virtualDevice) {
            return result(false, u"Physical controller is unavailable"_qs,
                          u"Reconnect the selected physical controller, then try again."_qs);
        }
        const ControllerMatch match = ControllerManager::match(*discovered, m_configuration.savedControllers);
        if (match.ambiguous) {
            return result(false, u"Physical controller identity is ambiguous"_qs,
                          u"Resolve the matching controller in setup before creating this Device Rig."_qs);
        }
        if (!match.recordId.isEmpty()) {
            const SavedControllerRecord *saved = savedControllerRecord(match.recordId);
            if (!saved) return result(false, u"Physical controller is unavailable"_qs,
                                      u"Refresh the device inventory, then try again."_qs);
            recordIds.append(saved->id);
            needsSetup = needsSetup || saved->lastVerified.isEmpty();
            continue;
        }
        SavedControllerRecord record = ControllerManager::verifiedRecord(*discovered, {},
            currentVjoyRequirements());
        record.lastVerified.clear();
        record.axisActivity = {};
        recordIds.append(record.id);
        createdRecordIds.insert(record.id);
        newRecords.push_back(std::move(record));
        needsSetup = true;
    }
    if (recordIds.isEmpty()) {
        return result(false, u"Device Rig needs a physical controller"_qs,
                      u"Connect a controller to create your first Device Rig."_qs);
    }
    m_configuration.savedControllers.insert(m_configuration.savedControllers.end(),
        std::make_move_iterator(newRecords.begin()), std::make_move_iterator(newRecords.end()));
    const QString rigId = createDeviceRig(trimmedName, recordIds, selectedOutput);
    if (rigId.isEmpty()) {
        m_configuration.savedControllers.erase(std::remove_if(m_configuration.savedControllers.begin(),
            m_configuration.savedControllers.end(), [&createdRecordIds](const SavedControllerRecord &record) {
                return createdRecordIds.contains(record.id);
            }), m_configuration.savedControllers.end());
        return result(false, u"Device Rig was not created"_qs,
                      u"The selected devices or virtual output changed. Refresh and try again."_qs);
    }
    rebuildControllerUiModel();
    return result(true, u"Device Rig created"_qs,
                  needsSetup ? u"The rig was saved. Set up its controller and output before using it in a game."_qs
                             : u"The rig was saved and is ready to review."_qs,
                  rigId, needsSetup ? u"setup"_qs : u"details"_qs);
}

QString AppBackend::createDeviceRigFromDetected(const QString &name,
                                                const QStringList &directInputIds,
                                                const QString &outputLayoutId)
{
    const QString trimmedName = name.trimmed().left(64);
    if (trimmedName.isEmpty() || directInputIds.isEmpty()
        || directInputIds.size() > kMaximumDeviceRigMembers
        || !findOutputLayout(m_configuration, outputLayoutId.trimmed().isEmpty()
            ? currentProfile().outputLayoutId : outputLayoutId.trimmed())) return {};
    if (std::any_of(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
                    [&trimmedName](const DeviceRig &rig) {
                        return rig.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
                    })) return {};

    QStringList recordIds;
    QSet<QString> seenInputs;
    QSet<QString> createdRecordIds;
    std::vector<SavedControllerRecord> newRecords;
    for (const QString &directInputId : directInputIds) {
        const QString inputId = directInputId.trimmed();
        const DiscoveredController *discovered = discoveredController(inputId);
        if (inputId.isEmpty() || !discovered || !discovered->connected || discovered->virtualDevice
            || seenInputs.contains(inputId)) return {};
        seenInputs.insert(inputId);
        const ControllerMatch match = ControllerManager::match(*discovered, m_configuration.savedControllers);
        if (match.ambiguous) return {};
        if (!match.recordId.isEmpty()) {
            recordIds.append(match.recordId);
            continue;
        }
        // Discovery gives us an exact DirectInput/HID identity and immutable
        // capabilities, so it is safe to save an unverified record as the
        // starting point for a grouped setup.  Do not claim setup or
        // calibration completion: the unified rig verifier supplies that.
        SavedControllerRecord record = ControllerManager::verifiedRecord(*discovered, {},
            currentVjoyRequirements());
        record.lastVerified.clear();
        record.axisActivity = {};
        recordIds.append(record.id);
        createdRecordIds.insert(record.id);
        newRecords.push_back(std::move(record));
    }
    if (recordIds.isEmpty()) return {};
    m_configuration.savedControllers.insert(m_configuration.savedControllers.end(),
        std::make_move_iterator(newRecords.begin()), std::make_move_iterator(newRecords.end()));
    const QString rigId = createDeviceRig(trimmedName, recordIds, outputLayoutId);
    if (rigId.isEmpty()) {
        // All preconditions above were checked before adding records, but keep
        // the operation transactional if a future create validation changes.
        m_configuration.savedControllers.erase(std::remove_if(m_configuration.savedControllers.begin(),
            m_configuration.savedControllers.end(), [&createdRecordIds](const SavedControllerRecord &record) {
                return createdRecordIds.contains(record.id);
            }), m_configuration.savedControllers.end());
        return {};
    }
    rebuildControllerUiModel();
    appendEvent(QString(u"Created an unverified Device Rig from %1 detected controller(s); verify the rig before use."_qs)
        .arg(recordIds.size()));
    return rigId;
}

bool AppBackend::addDetectedDeviceToRig(const QString &rigId, const QString &directInputId,
                                        bool required)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const DiscoveredController *discovered = discoveredController(directInputId.trimmed());
    if (!rig || !discovered || !discovered->connected || discovered->virtualDevice) return false;
    const ControllerMatch match = ControllerManager::match(*discovered, m_configuration.savedControllers);
    if (match.ambiguous) return false;
    QString recordId = match.recordId;
    bool createdRecord = false;
    if (recordId.isEmpty()) {
        SavedControllerRecord record = ControllerManager::verifiedRecord(*discovered, {},
            currentVjoyRequirements());
        record.lastVerified.clear();
        record.axisActivity = {};
        recordId = record.id;
        m_configuration.savedControllers.push_back(std::move(record));
        createdRecord = true;
    }
    if (!addDeviceRigMember(rig->id, recordId, required)) {
        if (createdRecord) {
            m_configuration.savedControllers.erase(std::remove_if(m_configuration.savedControllers.begin(),
                m_configuration.savedControllers.end(), [&recordId](const SavedControllerRecord &record) {
                    return record.id == recordId;
                }), m_configuration.savedControllers.end());
        }
        return false;
    }
    rebuildControllerUiModel();
    appendEvent(QString(u"New controller added to %1; complete Device Rig verification before its routes are used."_qs)
        .arg(rig->name));
    return true;
}

bool AppBackend::activateDeviceRig(const QString &rigId)
{
    const ActivationDecision decision = activationDecision({}, ActivationIntent::ManualRig, {}, rigId);
    if (!decision.valid) {
        appendEvent(decision.explanation + u" "_qs + decision.blockers.join(u"; "_qs));
        return false;
    }
    return applyActivationDecision(decision, ActivationIntent::ManualRig);
}

bool AppBackend::deactivateDeviceRig(const QString &rigId)
{
    const DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || rig->id != m_configuration.activeDeviceRigId) return false;
    appendEvent(u"Active Device Rig cannot be removed independently of its Profile and Output route. Select a compatible Profile or Rig instead."_qs);
    return false;
}

bool AppBackend::setDefaultDeviceRig(const QString &rigId)
{
    DeviceRig *target = findDeviceRig(m_configuration, rigId.trimmed());
    if (!target) return false;
    for (DeviceRig &rig : m_configuration.deviceRigs) rig.isDefault = rig.id == target->id;
    persistAndApply();
    appendEvent(QString(u"Default Device Rig set to %1"_qs).arg(target->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::clearDefaultDeviceRig(const QString &rigId)
{
    DeviceRig *target = findDeviceRig(m_configuration, rigId.trimmed());
    if (!target || !target->isDefault) return false;
    target->isDefault = false;
    persistAndApply();
    appendEvent(QString(u"Cleared default Device Rig: %1"_qs).arg(target->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigAutoActivate(const QString &rigId, bool enabled)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || rig->autoActivate == enabled) return false;
    rig->autoActivate = enabled;
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigActivationPriority(const QString &rigId, int priority)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const int normalized = std::clamp(priority, 0, 100);
    if (!rig || rig->activationPriority == normalized) return false;
    rig->activationPriority = normalized;
    persistAndApply();
    appendEvent(QString(u"Device Rig activation priority set to %1: %2"_qs)
        .arg(normalized).arg(rig->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigMemberRequired(const QString &rigId, const QString &controllerRecordId,
                                            bool required)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig) return false;
    const auto member = std::find_if(rig->members.begin(), rig->members.end(),
        [&controllerRecordId](const DeviceRigMember &item) { return item.controllerRecordId == controllerRecordId; });
    if (member == rig->members.end() || member->required == required) return false;
    member->required = required;
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigMemberEnabled(const QString &rigId, const QString &controllerRecordId,
                                           bool enabled)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig) return false;
    const auto member = std::find_if(rig->members.begin(), rig->members.end(),
        [&controllerRecordId](const DeviceRigMember &item) { return item.controllerRecordId == controllerRecordId; });
    if (member == rig->members.end() || member->enabled == enabled) return false;
    member->enabled = enabled;
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigMemberOutput(const QString &rigId, const QString &controllerRecordId,
                                          const QString &outputLayoutId)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || !findOutputLayout(m_configuration, outputLayoutId.trimmed())) return false;
    const bool ownedOutput = std::any_of(rig->outputs.cbegin(), rig->outputs.cend(),
        [&outputLayoutId](const DeviceRigOutputTarget &output) {
            return output.enabled && output.outputLayoutId == outputLayoutId.trimmed();
        });
    if (!ownedOutput) return false;
    const auto member = std::find_if(rig->members.begin(), rig->members.end(),
        [&controllerRecordId](const DeviceRigMember &item) { return item.controllerRecordId == controllerRecordId; });
    if (member == rig->members.end() || member->preferredOutputLayoutId == outputLayoutId.trimmed()) return false;
    member->preferredOutputLayoutId = outputLayoutId.trimmed();
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::addDeviceRigOutput(const QString &rigId, const QString &outputLayoutId)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString outputId = outputLayoutId.trimmed();
    if (!rig || !findOutputLayout(m_configuration, outputId)
        || rig->outputs.size() >= kMaximumDeviceRigOutputs
        || std::any_of(rig->outputs.cbegin(), rig->outputs.cend(), [&outputId](const auto &output) {
               return output.outputLayoutId == outputId;
           })) return false;
    rig->outputs.push_back({outputId, true});
    persistAndApply();
    appendEvent(QString(u"Added virtual output to %1; assign an input before it receives routes."_qs)
        .arg(rig->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::removeDeviceRigOutput(const QString &rigId, const QString &outputLayoutId)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString outputId = outputLayoutId.trimmed();
    if (!rig || rig->outputs.size() <= 1) return false;
    const auto found = std::find_if(rig->outputs.cbegin(), rig->outputs.cend(), [&outputId](const auto &output) {
        return output.outputLayoutId == outputId;
    });
    if (found == rig->outputs.cend()
        || std::any_of(rig->members.cbegin(), rig->members.cend(), [&outputId](const DeviceRigMember &member) {
               return member.preferredOutputLayoutId == outputId;
           })) return false;
    rig->outputs.erase(rig->outputs.begin() + static_cast<std::ptrdiff_t>(
        std::distance(rig->outputs.cbegin(), found)));
    persistAndApply();
    appendEvent(QString(u"Removed virtual output from %1."_qs).arg(rig->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigOutputEnabled(const QString &rigId, const QString &outputLayoutId,
                                            bool enabled)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString outputId = outputLayoutId.trimmed();
    if (!rig) return false;
    const auto target = std::find_if(rig->outputs.begin(), rig->outputs.end(), [&outputId](const auto &output) {
        return output.outputLayoutId == outputId;
    });
    if (target == rig->outputs.end() || target->enabled == enabled) return false;
    if (!enabled) {
        const int enabledCount = static_cast<int>(std::count_if(rig->outputs.cbegin(), rig->outputs.cend(),
            [](const auto &output) { return output.enabled; }));
        const bool assigned = std::any_of(rig->members.cbegin(), rig->members.cend(),
            [&outputId](const DeviceRigMember &member) { return member.preferredOutputLayoutId == outputId; });
        if (enabledCount <= 1 || assigned) return false;
    }
    target->enabled = enabled;
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigInputVisibility(const QString &rigId,
                                             const QStringList &controllerRecordIds, bool hidden)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || controllerRecordIds.isEmpty()) return false;

    QStringList instances;
    QList<SavedControllerRecord *> adoptedRecords;
    QSet<QString> seenRecords;
    for (const QString &value : controllerRecordIds) {
        const QString recordId = value.trimmed();
        if (recordId.isEmpty() || seenRecords.contains(recordId)) continue;
        const auto member = std::find_if(rig->members.cbegin(), rig->members.cend(), [&recordId](const DeviceRigMember &item) {
            return item.controllerRecordId == recordId;
        });
        const auto recordFound = std::find_if(m_configuration.savedControllers.begin(),
            m_configuration.savedControllers.end(), [&recordId](const SavedControllerRecord &item) {
                return item.id == recordId;
            });
        if (member == rig->members.cend() || recordFound == m_configuration.savedControllers.end()) {
            appendEvent(u"Visibility actions can only target saved inputs in the selected Device Rig."_qs);
            return false;
        }
        SavedControllerRecord *record = &*recordFound;
        QStringList owned = record->ownedHidHideDeviceInstances;
        if (owned.isEmpty()) {
            // Selecting Hide in Devices is an explicit adoption choice. Show
            // never adopts an unmanaged input, so upgraded users are not
            // silently brought under isolation management.
            if (!hidden || record->hidInstanceId.trimmed().isEmpty()) {
                appendEvent(QString(u"%1 has not been explicitly adopted for managed game visibility."_qs)
                    .arg(record->displayName));
                return false;
            }
            owned = {record->hidInstanceId};
        }
        for (const QString &instance : owned) instances.append(instance);
        adoptedRecords.append(record);
        seenRecords.insert(recordId);
    }
    if (instances.isEmpty()) return false;

    ControllerReadinessService visibility;
    QStringList normalized;
    QString validation;
    if (!visibility.validateManagedPhysicalInputIdentities(instances, &normalized, &validation)) {
        appendEvent(validation);
        return false;
    }
    const ManagedVisibilityTransactionResult result = visibility.applyManagedPhysicalInputVisibility(normalized, hidden);
    appendEvent(result.status);
    if (!result.succeeded) return false;

    if (hidden) {
        // Store only exact identities just proved by HidHide. No broad
        // friendly-name, VID, or unrelated-device rule enters this model.
        for (SavedControllerRecord *record : adoptedRecords) {
            if (!record->ownedHidHideDeviceInstances.isEmpty()) continue;
            const QString identity = ControllerReadinessService::normalizeDeviceInstanceId(record->hidInstanceId);
            if (!identity.isEmpty()) record->ownedHidHideDeviceInstances = {identity};
        }
    }
    persistAndApply();
    emit deviceRigsChanged();
    // Command exit is not acceptance: re-inspect live HidHide state through
    // the existing low-frequency verification path.
    startQuickVerification();
    return true;
}

bool AppBackend::setDeviceRigOutputVisibility(const QString &rigId,
                                              const QStringList &outputLayoutIds, bool visible)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || outputLayoutIds.isEmpty()) return false;

    QStringList instances;
    QSet<QString> seenLayouts;
    for (const QString &value : outputLayoutIds) {
        const QString layoutId = value.trimmed();
        if (layoutId.isEmpty() || seenLayouts.contains(layoutId)) continue;
        const auto target = std::find_if(rig->outputs.cbegin(), rig->outputs.cend(), [&layoutId](const DeviceRigOutputTarget &item) {
            return item.outputLayoutId == layoutId;
        });
        VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
        if (target == rig->outputs.cend() || !layout || !layout->hidhideManaged
            || layout->hidHideDeviceInstanceId.trimmed().isEmpty()) {
            appendEvent(u"Adopt an exact vJoy HID identity before changing virtual-output visibility."_qs);
            return false;
        }
        if (!visible && rig->id == m_configuration.activeDeviceRigId && target->enabled) {
            appendEvent(u"An enabled active-rig output must stay visible to games. Disable it or select another active output first."_qs);
            return false;
        }
        instances.append(layout->hidHideDeviceInstanceId);
        seenLayouts.insert(layoutId);
    }
    if (instances.isEmpty()) return false;

    ControllerReadinessService visibility;
    const ManagedVisibilityTransactionResult result = visibility.applyManagedVirtualOutputVisibility(instances, !visible);
    appendEvent(result.status);
    if (!result.succeeded) return false;
    emit deviceRigsChanged();
    startQuickVerification();
    return true;
}

bool AppBackend::addDeviceRigMember(const QString &rigId, const QString &controllerRecordId, bool required)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString id = controllerRecordId.trimmed();
    if (!rig || !savedControllerRecord(id) || rig->members.size() >= kMaximumDeviceRigMembers
        || std::any_of(rig->members.cbegin(), rig->members.cend(), [&id](const DeviceRigMember &member) {
               return member.controllerRecordId == id;
           })) return false;
    const auto output = std::find_if(rig->outputs.cbegin(), rig->outputs.cend(),
        [](const DeviceRigOutputTarget &candidate) { return candidate.enabled; });
    if (output == rig->outputs.cend()) return false;
    rig->members.push_back({id, true, required, output->outputLayoutId});
    for (ControllerProfile &profile : m_configuration.profiles) {
        if (profile.deviceRigId == rig->id) {
            DeviceProfileMapping &mapping = ensureDeviceProfileMapping(profile, id);
            if (mapping.nativePovBindings.empty()) mapping.nativePovBindings = m_configuration.nativePovBindings;
        }
    }
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    appendEvent(QString(u"Added device to %1; review its routes before activation."_qs).arg(rig->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::removeDeviceRigMember(const QString &rigId, const QString &controllerRecordId)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    // A zero-member rig is not a recoverable topology. The explicit Delete
    // Rig action preserves a clear transaction boundary for that case.
    if (!rig || rig->members.size() <= 1) return false;
    const auto found = std::find_if(rig->members.cbegin(), rig->members.cend(),
        [&controllerRecordId](const DeviceRigMember &member) { return member.controllerRecordId == controllerRecordId; });
    if (found == rig->members.cend()) return false;
    const QString removed = found->controllerRecordId;
    rig->members.erase(rig->members.begin() + static_cast<std::ptrdiff_t>(
        std::distance(rig->members.cbegin(), found)));
    for (ControllerProfile &profile : m_configuration.profiles) {
        if (profile.deviceRigId != rig->id) continue;
        profile.deviceMappings.erase(std::remove_if(profile.deviceMappings.begin(), profile.deviceMappings.end(),
            [&removed](const DeviceProfileMapping &mapping) { return mapping.controllerRecordId == removed; }),
            profile.deviceMappings.end());
    }
    m_configuration.editingDeviceRecordIds.removeAll(removed);
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    appendEvent(QString(u"Removed device from %1; its mapping payload was retained only in unrelated rigs/profiles."_qs)
        .arg(rig->name));
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::renameDeviceRig(const QString &rigId, const QString &name)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString trimmed = name.trimmed().left(64);
    if (!rig || trimmed.isEmpty() || rig->name == trimmed
        || std::any_of(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
                       [&rig, &trimmed](const DeviceRig &candidate) {
                           return candidate.id != rig->id
                               && candidate.name.compare(trimmed, Qt::CaseInsensitive) == 0;
                       })) return false;
    rig->name = trimmed;
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigEnabled(const QString &rigId, bool enabled)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || rig->enabled == enabled) return false;
    rig->enabled = enabled;
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigFallback(const QString &rigId, const QString &fallbackRigId)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    const QString fallback = fallbackRigId.trimmed();
    if (!rig || (!fallback.isEmpty() && (!findDeviceRig(m_configuration, fallback) || fallback == rig->id))
        || rig->fallbackRigId == fallback) return false;
    rig->fallbackRigId = fallback;
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setDeviceRigDisconnectBehavior(const QString &rigId, int behavior)
{
    DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig || behavior < static_cast<int>(DeviceRigDisconnectBehavior::SuspendAffectedRoutes)
        || behavior > static_cast<int>(DeviceRigDisconnectBehavior::UseFallback)) return false;
    const auto desired = static_cast<DeviceRigDisconnectBehavior>(behavior);
    if (rig->disconnectBehavior == desired) return false;
    rig->disconnectBehavior = desired;
    persistAndApply();
    emit deviceRigsChanged();
    return true;
}

void AppBackend::verifyDeviceRig(const QString &rigId)
{
    const DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed().isEmpty()
        ? m_configuration.activeDeviceRigId : rigId.trimmed());
    if (!rig) return;
    // The existing themed readiness dialog remains the sole verification UI.
    // It now projects the selected rig's inputs, outputs, and compiled routes
    // through controllerReadinessChecks while the established transaction
    // retains the driver's repair/reacquire safeguards.
    if (rig->id != m_configuration.activeDeviceRigId) {
        appendEvent(u"Open Devices and activate the desired rig before a live verification transaction."_qs);
        return;
    }
    verifyHotasSetup();
}

bool AppBackend::setEditingDeviceContext(const QString &rigId, const QStringList &controllerRecordIds)
{
    const DeviceRig *rig = findDeviceRig(m_configuration, rigId.trimmed());
    if (!rig) return false;
    QSet<QString> validMembers;
    for (const DeviceRigMember &member : rig->members) validMembers.insert(member.controllerRecordId);
    QSet<QString> unique;
    QStringList selected;
    for (const QString &value : controllerRecordIds) {
        const QString id = value.trimmed();
        if (!validMembers.contains(id) || unique.contains(id)) return false;
        unique.insert(id);
        selected.append(id);
    }
    m_configuration.editingDeviceRigId = rig->id;
    m_configuration.editingDeviceRecordIds = selected;
    persistAndApply();
    // Editing context is a low-frequency control-plane action, but axes,
    // buttons, POVs, curves, and Adaptive Response all consume the selected
    // device mapping. Notify their QML properties immediately rather than
    // waiting for the next telemetry snapshot.
    emit inputTelemetryChanged();
    emit buttonTelemetryChanged();
    emit deviceRigsChanged();
    return true;
}

bool AppBackend::setSelectedDeviceContext(const QString &rigId,
                                          const QStringList &controllerRecordIds)
{
    return setEditingDeviceContext(rigId, controllerRecordIds);
}

bool AppBackend::focusIssueTarget(const QString &objectType, const QString &objectId)
{
    const QString type = objectType.trimmed();
    const QString id = objectId.trimmed();
    if (type == u"deviceRig"_qs) return setEditingDeviceContext(id, {});
    if (id.isEmpty()) return false;

    // Signal Flow uses durable identities in the same AppIssue contract as
    // Devices. Resolve the affected route's canonical context before the
    // shell loads page 11, then retain only the opaque identity for the QML
    // presentation to select. This is deliberately not a graph-local route
    // cache and does not mutate any mapping semantics.
    const auto focusSignalFlowRoute = [this](const SignalFlowRoute &route,
                                              const QString &focusId) {
        if (route.profileId.isEmpty() || !findProfile(m_configuration, route.profileId)) return false;
        if (route.profileId != m_configuration.activeProfileId
            && !activateProfile(route.profileId)) return false;
        if (!route.controllerRecordId.isEmpty()) {
            for (const DeviceRig &rig : m_configuration.deviceRigs) {
                const bool containsController = std::any_of(rig.members.cbegin(), rig.members.cend(),
                    [&route](const DeviceRigMember &member) {
                        return member.controllerRecordId == route.controllerRecordId;
                    });
                if (!containsController) continue;
                const QStringList exactScope{route.controllerRecordId};
                if ((m_configuration.editingDeviceRigId != rig.id
                     || m_configuration.editingDeviceRecordIds != exactScope)
                    && !setEditingDeviceContext(rig.id, exactScope)) return false;
                break;
            }
        }
        m_signalFlowFocusObjectId = focusId;
        m_signalFlowActionFeedback = u"Opened the exact Signal Flow item reported by App Health."_qs;
        emit signalFlowChanged();
        return true;
    };
    if (type == u"signalFlowRoute"_qs) {
        const SignalFlowRoute *route = findSignalFlowRouteById(m_configuration.signalFlow, id);
        return route ? focusSignalFlowRoute(*route, id) : false;
    }
    if (type == u"signalFlowProcessor"_qs) {
        const auto route = std::find_if(m_configuration.signalFlow.routes.cbegin(),
            m_configuration.signalFlow.routes.cend(), [&id](const SignalFlowRoute &candidate) {
                return candidate.processorPath.contains(id);
            });
        return route != m_configuration.signalFlow.routes.cend()
            ? focusSignalFlowRoute(*route, id) : false;
    }

    for (const DeviceRig &rig : m_configuration.deviceRigs) {
        const auto member = std::find_if(rig.members.cbegin(), rig.members.cend(), [&id](const DeviceRigMember &candidate) {
            return candidate.controllerRecordId == id;
        });
        const auto output = std::find_if(rig.outputs.cbegin(), rig.outputs.cend(), [&id](const DeviceRigOutputTarget &candidate) {
            return candidate.outputLayoutId == id;
        });
        if (type == u"physicalDevice"_qs && member != rig.members.cend()) {
            return setEditingDeviceContext(rig.id, {id});
        }
        if (type == u"virtualOutput"_qs && output != rig.outputs.cend()) {
            return setEditingDeviceContext(rig.id, {});
        }
        if (type == u"gameVisibility"_qs) {
            if (member != rig.members.cend()) return setEditingDeviceContext(rig.id, {id});
            if (output != rig.outputs.cend()) return setEditingDeviceContext(rig.id, {});
        }
    }
    return false;
}

void AppBackend::recordCrashPresentationState(int page, const QString &theme)
{
    m_crashPresentationPage = page;
    m_crashPresentationTheme = theme.left(32);
    // This updates the fixed crash-context snapshot only. It deliberately
    // does not append an event for every page/property binding evaluation.
    CrashDiagnostics::recordControlPlaneEvent(QString(), crashPresentationContext());
}

QVariantMap AppBackend::editingAxisBatchPreview(int physicalAxis, const QString &property,
                                                const QVariant &value) const
{
    QVariantMap result{{u"property"_qs, property}, {u"value"_qs, value}};
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    const ControllerProfile &profile = currentProfile();
    if (!validAxis(physicalAxis) || !rig || (!profile.deviceRigId.isEmpty()
        && profile.deviceRigId != rig->id)
        || (property != u"inverted"_qs && property != u"deadzone"_qs)) {
        result.insert(u"valid"_qs, false);
        result.insert(u"summary"_qs, u"This edit is unavailable for the current context."_qs);
        return result;
    }
    QStringList ids = m_configuration.editingDeviceRecordIds;
    if (ids.isEmpty()) {
        for (const DeviceRigMember &member : rig->members) {
            if (member.enabled) ids.append(member.controllerRecordId);
        }
    }
    QVariantList targets;
    for (const QString &id : ids) {
        const SavedControllerRecord *record = savedControllerRecord(id);
        if (!record) continue;
        const DeviceProfileMapping *mapping = findDeviceProfileMapping(profile, id);
        const AxisMapping &axis = mapping ? mapping->axes[static_cast<size_t>(physicalAxis)]
                                          : profile.axes[static_cast<size_t>(physicalAxis)];
        targets.append(QVariantMap{{u"id"_qs, id}, {u"name"_qs, record->displayName},
            {u"current"_qs, property == u"inverted"_qs ? QVariant(axis.inverted)
                                                        : QVariant(axis.deadzone)},
            {u"compatible"_qs, true}});
    }
    result.insert(u"valid"_qs, targets.size() > 1);
    result.insert(u"targets"_qs, targets);
    result.insert(u"compatibleCount"_qs, targets.size());
    result.insert(u"summary"_qs, targets.size() > 1
        ? QString(u"%1 selected physical inputs are compatible. Review before applying."_qs)
              .arg(targets.size())
        : u"Choose two or more physical inputs to make a batch edit."_qs);
    return result;
}

bool AppBackend::applyEditingAxisBatch(int physicalAxis, const QString &property,
                                       const QVariant &value, const QString &mode)
{
    if (mode != u"apply-compatible-only"_qs) return false;
    const QVariantMap preview = editingAxisBatchPreview(physicalAxis, property, value);
    if (!preview.value(u"valid"_qs).toBool()) return false;
    DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    if (!rig) return false;
    QStringList ids = m_configuration.editingDeviceRecordIds;
    if (ids.isEmpty()) {
        for (const DeviceRigMember &member : rig->members) {
            if (member.enabled) ids.append(member.controllerRecordId);
        }
    }
    ControllerProfile &profile = currentProfile();
    int applied = 0;
    for (const QString &id : ids) {
        if (!savedControllerRecord(id)) continue;
        DeviceProfileMapping &mapping = ensureDeviceProfileMapping(profile, id);
        AxisMapping &axis = mapping.axes[static_cast<size_t>(physicalAxis)];
        if (property == u"inverted"_qs) {
            axis.inverted = value.toBool();
        } else {
            axis.deadzone = std::clamp(static_cast<float>(value.toDouble()), 0.0F, 0.95F);
        }
        ++applied;
    }
    if (applied < 2) return false;
    persistAndApply();
    appendEvent(QString(u"Applied compatible %1 axis edit to %2 physical inputs."_qs)
        .arg(property).arg(applied));
    return true;
}

bool AppBackend::deleteDeviceRig(const QString &rigId)
{
    const auto found = std::find_if(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
        [&rigId](const DeviceRig &rig) { return rig.id == rigId.trimmed(); });
    if (found == m_configuration.deviceRigs.cend()) return false;
    const QString name = found->name;
    for (ControllerProfile &profile : m_configuration.profiles) {
        if (profile.deviceRigId == found->id) profile.deviceRigId.clear();
    }
    const bool wasActive = found->id == m_configuration.activeDeviceRigId;
    const bool wasEditing = found->id == m_configuration.editingDeviceRigId;
    m_configuration.deviceRigs.erase(m_configuration.deviceRigs.begin()
        + static_cast<std::ptrdiff_t>(std::distance(m_configuration.deviceRigs.cbegin(), found)));
    if (wasActive) m_configuration.activeDeviceRigId.clear();
    if (wasEditing) {
        m_configuration.editingDeviceRigId.clear();
        m_configuration.editingDeviceRecordIds.clear();
    }
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    persistAndApply();
    appendEvent(QString(u"Deleted Device Rig: %1. Saved devices and profile mappings were retained."_qs).arg(name));
    emit deviceRigsChanged();
    return true;
}
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
    if (m_liveInputTestSuspended) return u"MAPPING SUSPENDED"_qs;
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
bool AppBackend::vjoyReady() const
{
    // The deterministic Live Controller UI seam represents a deliberately
    // suspended mapper with no usable output.  Keep that presentation state
    // self-contained rather than letting a real, unrelated vJoy driver on a
    // developer machine make the test outcome depend on external hardware.
    // Production code never sets this flag.
    return !m_liveInputTestSuspended && m_worker.runtime().vjoyReady.load();
}
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
    // The existing readiness panel is the shared setup surface.  A one-input
    // rig therefore remains compact, while a multi-input rig contributes one
    // clear row per physical member/output plus a compiled-routing result.
    // This projection is control-plane only; no mapper report constructs it.
    if (const DeviceRig *rig = setupAssistantDeviceRig(m_setupAssistantScopeType,
                                                       m_setupAssistantScopeId)) {
        const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [rig](const DeviceRigStatus &status) { return status.rigId == rig->id; });
        const DeviceRigStatus status = found == m_deviceRigStatuses.cend()
            ? DeviceRigStatus{rig->id, rig->enabled ? DeviceRigHealth::Offline : DeviceRigHealth::Disabled}
            : *found;
        const auto appendRigCheck = [](QVariantList &checks, const QString &name, const QString &state,
                                       const QString &message, const QString &severity) {
            checks.append(QVariantMap{{u"name"_qs, name}, {u"state"_qs, state},
                                      {u"message"_qs, message}, {u"severity"_qs, severity}});
        };
        QVariantList checks;
        const HidHideCapabilities &hidhide = m_readiness.plan().hidhide;
        // This is compiled once on the UI/control plane.  The worker only
        // updates the fixed member/output counters below; it never produces
        // readiness strings or traverses this durable model per report.
        const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(m_configuration, rig->id);
        appendRigCheck(checks, u"GAME VISIBILITY · HOTAS BF6"_qs,
            hidhide.mapperAllowlisted ? u"READY"_qs : u"SETUP NEEDED"_qs,
            hidhide.mapperAllowlisted
                ? u"HOTAS BF6 can keep selected physical controllers hidden from games."_qs
                : hidhide.installed
                    ? u"Game visibility needs one setup change before HOTAS BF6 can hide physical controllers safely."_qs
                    : u"Set up game visibility so your game uses the virtual controller instead of the physical controllers."_qs,
            hidhide.mapperAllowlisted ? u"ready"_qs : u"warning"_qs);
        for (const DeviceRigMember &member : rig->members) {
            const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
            const QString name = record ? record->displayName : u"Unknown device"_qs;
            if (!member.enabled) {
                appendRigCheck(checks, u"INPUT · "_qs + name, u"DISABLED"_qs,
                    u"Excluded from this Device Rig."_qs, u"info"_qs);
            } else if (status.ambiguousMemberIds.contains(member.controllerRecordId)) {
                appendRigCheck(checks, u"INPUT · "_qs + name, u"SELECTION REQUIRED"_qs,
                    u"Windows exposed more than one plausible saved identity. Choose the exact device in Devices."_qs,
                    u"error"_qs);
            } else if (!status.connectedMemberIds.contains(member.controllerRecordId)) {
                appendRigCheck(checks, u"INPUT · "_qs + name,
                    member.required ? u"OFFLINE"_qs : u"OPTIONAL · OFFLINE"_qs,
                    member.required ? u"Required device is not connected."_qs
                                    : u"Optional device is offline; the rig may continue without its routes."_qs,
                    member.required ? u"error"_qs : u"info"_qs);
            } else if (status.needsVerificationMemberIds.contains(member.controllerRecordId)) {
                appendRigCheck(checks, u"INPUT · "_qs + name, u"NEEDS VERIFICATION"_qs,
                    u"Identity is saved and connected; run setup verification before automatic activation."_qs,
                    u"warning"_qs);
            } else {
                appendRigCheck(checks, u"INPUT · "_qs + name, u"READY"_qs,
                    member.required ? u"Connected, identity matched, and verified."_qs
                                    : u"Optional device connected, identity matched, and verified."_qs,
                    u"ready"_qs);
            }
            if (!member.enabled || (!member.required
                                    && !status.connectedMemberIds.contains(member.controllerRecordId))) {
                continue;
            }
            const bool managed = record && !record->ownedHidHideDeviceInstances.isEmpty();
            const QString persistedIdentity = record ? record->hidInstanceId : QString{};
            const QString normalizedIdentity = ControllerReadinessService::normalizeDeviceInstanceId(
                persistedIdentity);
            const bool hidden = managed && std::any_of(hidhide.hiddenDeviceInstanceIds.cbegin(),
                hidhide.hiddenDeviceInstanceIds.cend(), [&normalizedIdentity](const QString &candidate) {
                    return !normalizedIdentity.isEmpty()
                        && ControllerReadinessService::normalizeDeviceInstanceId(candidate) == normalizedIdentity;
                });
            appendRigCheck(checks, u"GAME VISIBILITY · "_qs + name,
                !managed || !hidhide.cloakKnown || !hidden ? u"SETUP NEEDED"_qs : u"READY"_qs,
                !managed ? u"Game visibility has not been set up for this physical controller."_qs
                    : !hidhide.cloakKnown ? u"HOTAS BF6 needs to check whether this physical controller is hidden from games."_qs
                    : hidden ? u"This physical controller is hidden from games."_qs
                             : u"This physical controller is visible to games and may cause duplicate controls."_qs,
                !managed || !hidhide.cloakKnown || !hidden ? u"warning"_qs : u"ready"_qs);

            const auto compiledMember = std::find_if(compiled.members.cbegin(),
                compiled.members.cbegin() + compiled.memberCount,
                [&member](const CompiledDeviceRigMember &candidate) {
                    return candidate.controllerRecordId == member.controllerRecordId;
                });
            if (compiledMember != compiled.members.cbegin() + compiled.memberCount) {
                const int memberIndex = static_cast<int>(std::distance(compiled.members.cbegin(), compiledMember));
                const bool reports = m_worker.runtime().deviceRigMeaningfulInputSequence[
                    static_cast<size_t>(memberIndex)].load(std::memory_order_relaxed) > 0;
                appendRigCheck(checks, u"LIVE INPUT · "_qs + name,
                    reports ? u"PASSED"_qs : u"WAITING"_qs,
                    reports ? u"Meaningful physical control movement has arrived from this Device Rig input."_qs
                            : u"Move an axis about 2% or press and release a button to prove this live input path."_qs,
                    reports ? u"ready"_qs : u"warning"_qs);
            }
        }
        int enabledOutputIndex = 0;
        bool everyConnectedInputReported = true;
        bool everyEnabledOutputChanged = true;
        for (int memberIndex = 0; memberIndex < compiled.memberCount; ++memberIndex) {
            const CompiledDeviceRigMember &member = compiled.members[static_cast<size_t>(memberIndex)];
            if (!status.connectedMemberIds.contains(member.controllerRecordId)) continue;
            everyConnectedInputReported = everyConnectedInputReported
                && m_worker.runtime().deviceRigMeaningfulInputSequence[static_cast<size_t>(memberIndex)]
                       .load(std::memory_order_relaxed) > 0;
        }
        for (const DeviceRigOutputTarget &target : rig->outputs) {
            const VirtualOutputLayout *layout = findOutputLayout(m_configuration, target.outputLayoutId);
            const QString outputName = layout ? layout->name : u"Unavailable output"_qs;
            // The rig worker publishes vjoyReady only after every enabled
            // output has been checked. That makes this a truthful compact
            // per-output readiness view without probing vJoy from QML.
            const bool ready = target.enabled && m_worker.runtime().vjoyReady.load();
            appendRigCheck(checks, u"OUTPUT · "_qs + outputName,
                !target.enabled ? u"DISABLED"_qs : ready ? u"READY"_qs : u"VERIFY OUTPUT"_qs,
                !target.enabled ? u"Excluded from this Device Rig."_qs
                    : ready ? m_worker.vjoyStatus()
                            : QString(u"Verify vJoy Device %1 and its required capabilities."_qs)
                                  .arg(layout ? layout->requirements.deviceId : 0),
                !target.enabled ? u"info"_qs : ready ? u"ready"_qs : u"warning"_qs);
            const bool outputHidden = layout && layout->hidhideManaged
                && std::any_of(hidhide.hiddenDeviceInstanceIds.cbegin(), hidhide.hiddenDeviceInstanceIds.cend(),
                    [layout](const QString &candidate) {
                        return !layout->hidHideDeviceInstanceId.isEmpty()
                            && ControllerReadinessService::normalizeDeviceInstanceId(candidate)
                                == ControllerReadinessService::normalizeDeviceInstanceId(
                                    layout->hidHideDeviceInstanceId);
                    });
            appendRigCheck(checks, u"GAME VISIBILITY · "_qs + outputName,
                !layout || !layout->hidhideManaged || outputHidden ? u"SETUP NEEDED"_qs : u"READY"_qs,
                !layout || !layout->hidhideManaged
                    ? u"Game visibility has not been set up for this virtual controller."_qs
                    : outputHidden ? u"This virtual controller is hidden from games. Games need to see it."_qs
                                   : u"This virtual controller is visible to games."_qs,
                !layout || !layout->hidhideManaged || outputHidden ? u"warning"_qs : u"ready"_qs);
            if (target.enabled) {
                const bool writes = enabledOutputIndex < compiled.outputCount
                    && m_worker.runtime().deviceRigMeaningfulOutputSequence[static_cast<size_t>(enabledOutputIndex)]
                           .load(std::memory_order_relaxed) > 0;
                appendRigCheck(checks, u"LIVE OUTPUT · "_qs + outputName,
                    writes ? u"PASSED"_qs : u"WAITING"_qs,
                    writes ? u"A mapped Device Rig route has changed this virtual output."_qs
                           : u"Move a mapped control for this output to prove the live route."_qs,
                    writes ? u"ready"_qs : u"warning"_qs);
                everyEnabledOutputChanged = everyEnabledOutputChanged && writes;
                ++enabledOutputIndex;
            }
        }
        appendRigCheck(checks, u"ROUTING"_qs, compiled.valid ? u"READY"_qs : u"ACTION REQUIRED"_qs,
            compiled.valid ? u"Device-qualified routes and output destinations are unambiguous."_qs
                           : compiled.issue,
            compiled.valid ? u"ready"_qs : u"error"_qs);
        const bool liveInput = compiled.valid && everyConnectedInputReported;
        const bool liveOutput = compiled.valid && m_worker.runtime().mappingActive.load()
            && everyEnabledOutputChanged;
        appendRigCheck(checks, u"LIVE MAPPING"_qs,
            liveInput && liveOutput ? u"PASSED"_qs : liveInput ? u"INPUT ACTIVE"_qs : u"WAITING"_qs,
            liveInput && liveOutput ? u"Meaningful input movement has reached the active Device Rig output."_qs
                : liveInput ? u"Meaningful input is arriving; move a mapped control to confirm output activity."_qs
                            : u"Move each required connected input during verification to prove the live chain."_qs,
            liveInput && liveOutput ? u"ready"_qs : u"warning"_qs);
        return checks;
    }
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
    const VerificationSubsystemState gameVisibilityState = plan.hidhideStatus == VerificationSubsystemState::Ready
        ? VerificationSubsystemState::Ready : VerificationSubsystemState::Attention;
    append(u"GAME VISIBILITY"_qs, gameVisibilityState,
           plan.hidhideStatus == VerificationSubsystemState::Ready
               ? u"HOTAS BF6 can keep the selected physical controller hidden from games."_qs
               : u"Set up game visibility so games use the virtual controller instead of the physical controller."_qs);
    return checks;
}

QVariantList AppBackend::setupAssistantIssues() const
{
    return setupAssistantIssuesForScope(m_setupAssistantScopeType, m_setupAssistantScopeId);
}

QString AppBackend::setupAssistantScopeType() const
{
    return m_setupAssistantScopeType;
}

QString AppBackend::setupAssistantScopeId() const
{
    return m_setupAssistantScopeId;
}

QVariantList AppBackend::setupAssistantIssuesForScope(const QString &scopeType,
                                                       const QString &scopeId) const
{
    QVariantList issues;
    // Setup decisions are derived from durable records and typed readiness
    // facts.  controllerReadinessChecks() remains a concise dashboard
    // projection, but the assistant must never infer product behavior by
    // parsing that projection's display strings.
    const bool testFacts = !m_setupAssistantTestFacts.isEmpty();
    const auto boolFact = [this](const QString &key, bool fallback) {
        return m_setupAssistantTestFacts.value(key, fallback).toBool();
    };
    const auto stringFact = [this](const QString &key, const QString &fallback) {
        return m_setupAssistantTestFacts.value(key, fallback).toString();
    };

    QVariantList inputs;
    QVariantList outputs;
    bool hidhideInstalled = false;
    bool hidhideReady = false;
    bool physicalVisible = false;
    QStringList visiblePhysicalInputIds;
    bool outputHidden = false;
    bool routingConflict = false;
    QString routingDetails;
    bool noMappedControl = false;
    bool vjoyInstalled = false;
    bool vjoyPresent = false;
    bool vjoyBusy = false;
    bool vjoyOwned = false;
    bool vjoySufficient = false;
    QString missingCapabilities;

    if (testFacts) {
        inputs = m_setupAssistantTestFacts.value(u"inputs"_qs).toList();
        outputs = m_setupAssistantTestFacts.value(u"outputs"_qs).toList();
        hidhideInstalled = boolFact(u"hidhideInstalled"_qs, false);
        hidhideReady = boolFact(u"hidhideReady"_qs, false);
        physicalVisible = boolFact(u"physicalVisible"_qs, false);
        outputHidden = boolFact(u"outputHidden"_qs, false);
        routingConflict = boolFact(u"routingConflict"_qs, false);
        routingDetails = stringFact(u"routingDetails"_qs, u"Two controls need the same output assignment."_qs);
        noMappedControl = boolFact(u"noMappedControl"_qs, false);
        vjoyInstalled = boolFact(u"vjoyInstalled"_qs, true);
        vjoyPresent = boolFact(u"vjoyPresent"_qs, true);
        vjoyBusy = boolFact(u"vjoyBusy"_qs, false);
        vjoyOwned = boolFact(u"vjoyOwned"_qs, false);
        vjoySufficient = boolFact(u"vjoySufficient"_qs, true);
        missingCapabilities = stringFact(u"missingCapabilities"_qs, QString{});
        if (physicalVisible) {
            for (const QVariant &entry : inputs) {
                const QVariantMap input = entry.toMap();
                if (input.value(u"connected"_qs).toBool() && input.value(u"required"_qs, true).toBool()) {
                    visiblePhysicalInputIds.append(input.value(u"id"_qs).toString());
                }
            }
        }
    } else {
        const DeviceRig *rig = setupAssistantDeviceRig(scopeType, scopeId);
        const DeviceRigStatus *status = nullptr;
        if (rig) {
            const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
                [rig](const DeviceRigStatus &candidate) { return candidate.rigId == rig->id; });
            if (found != m_deviceRigStatuses.cend()) status = &*found;
            int inputIndex = 0;
            for (const DeviceRigMember &member : rig->members) {
                if (!member.enabled) continue;
                const int runtimeIndex = inputIndex++;
                if (scopeType == u"device"_qs && member.controllerRecordId != scopeId) continue;
                const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
                const bool connected = status && status->connectedMemberIds.contains(member.controllerRecordId);
                const bool current = record && (record->lastDirectInputId == deviceId()
                    || record->id == m_configuration.activeControllerRecordId);
                inputs.append(QVariantMap{{u"id"_qs, member.controllerRecordId},
                    {u"name"_qs, record ? record->displayName : u"Physical controller"_qs},
                    {u"saved"_qs, record != nullptr}, {u"connected"_qs, connected},
                    {u"required"_qs, member.required},
                    {u"verified"_qs, record && !record->lastVerified.isEmpty()},
                    {u"ambiguous"_qs, status && status->ambiguousMemberIds.contains(member.controllerRecordId)},
                    {u"calibrationRequired"_qs, current && calibrationNeedsSetup(currentPhysicalCapabilities())},
                    {u"runtimeIndex"_qs, runtimeIndex}});
            }
            int outputIndex = 0;
            for (const DeviceRigOutputTarget &target : rig->outputs) {
                if (!target.enabled) continue;
                const int runtimeIndex = outputIndex++;
                if (scopeType == u"virtualOutput"_qs && target.outputLayoutId != scopeId) continue;
                const VirtualOutputLayout *layout = findOutputLayout(m_configuration, target.outputLayoutId);
                if (!layout) continue;
                outputs.append(QVariantMap{{u"id"_qs, layout->id}, {u"name"_qs, layout->name},
                    {u"deviceId"_qs, layout->requirements.deviceId},
                    {u"buttons"_qs, layout->requirements.buttons},
                    {u"continuousPovs"_qs, layout->requirements.continuousPovs},
                    {u"discretePovs"_qs, layout->requirements.discretePovs},
                    {u"runtimeIndex"_qs, runtimeIndex}});
            }
            const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(m_configuration, rig->id);
            if (scopeType == u"application"_qs || scopeType == u"deviceRig"_qs) {
                routingConflict = !compiled.valid;
                routingDetails = routingConflict ? compiled.issue : QString{};
            }
            if (compiled.valid) {
                bool mappedControlFound = false;
                for (int memberIndex = 0; memberIndex < compiled.memberCount; ++memberIndex) {
                    const CompiledDeviceRigMember &member = compiled.members[static_cast<size_t>(memberIndex)];
                    if (scopeType == u"device"_qs && member.controllerRecordId != scopeId) continue;
                    if (scopeType == u"virtualOutput"_qs && member.outputLayoutId != scopeId) continue;
                    mappedControlFound = mappedControlFound || hasMappedControl(member);
                }
                noMappedControl = !mappedControlFound;
            }
        } else {
            QSet<QString> represented;
            for (const DiscoveredController &controller : m_discoveredControllers) {
                if (controller.virtualDevice) continue;
                const ControllerMatch match = ControllerManager::match(controller, m_configuration.savedControllers);
                const SavedControllerRecord *record = match.recordId.isEmpty() || match.ambiguous
                    ? nullptr : savedControllerRecord(match.recordId);
                const QString inputId = record ? record->id : controller.directInputId;
                if (scopeType == u"device"_qs && inputId != scopeId) continue;
                inputs.append(QVariantMap{{u"id"_qs, inputId},
                    {u"name"_qs, controller.name}, {u"saved"_qs, record != nullptr},
                    {u"connected"_qs, controller.connected}, {u"required"_qs, true},
                    {u"verified"_qs, record && !record->lastVerified.isEmpty()},
                    {u"ambiguous"_qs, match.ambiguous}, {u"calibrationRequired"_qs, false}});
                if (record) represented.insert(record->id);
            }
            for (const SavedControllerRecord &record : m_configuration.savedControllers) {
                if (represented.contains(record.id) || (scopeType == u"device"_qs && record.id != scopeId)) continue;
                inputs.append(QVariantMap{{u"id"_qs, record.id}, {u"name"_qs, record.displayName},
                    {u"saved"_qs, true}, {u"connected"_qs, false}, {u"required"_qs, true},
                    {u"verified"_qs, !record.lastVerified.isEmpty()}, {u"ambiguous"_qs, false},
                    {u"calibrationRequired"_qs, false}});
            }
            const VirtualOutputLayout *scopeOutput = scopeType == u"virtualOutput"_qs
                ? findOutputLayout(m_configuration, scopeId) : activeOutputLayout();
            if (scopeOutput) {
                const VirtualOutputLayout *layout = scopeOutput;
                outputs.append(QVariantMap{{u"id"_qs, layout->id}, {u"name"_qs, layout->name},
                    {u"deviceId"_qs, layout->requirements.deviceId},
                    {u"buttons"_qs, layout->requirements.buttons},
                    {u"continuousPovs"_qs, layout->requirements.continuousPovs},
                    {u"discretePovs"_qs, layout->requirements.discretePovs}});
            }
        }
        const ControllerReadinessPlan *scopedOutputPlan = scopeType == u"virtualOutput"_qs
            ? virtualOutputReadinessPlan(scopeId) : nullptr;
        const ControllerReadinessPlan &plan = scopedOutputPlan ? *scopedOutputPlan : m_readiness.plan();
        hidhideInstalled = plan.hidhide.installed;
        hidhideReady = plan.hidhide.installed && plan.hidhide.cliAvailable
            && plan.hidhide.serviceReady && plan.hidhide.mapperAllowlisted;
        vjoyInstalled = plan.vjoy.installed && plan.vjoy.configurationUtilityAvailable;
        vjoyPresent = plan.vjoy.devicePresent;
        vjoyBusy = plan.vjoy.busy;
        vjoyOwned = plan.vjoy.ownedByHotasBf6;
        vjoySufficient = !plan.vjoyNeedsChanges;
        missingCapabilities = plan.vjoyNeedsChanges ? plan.vjoySummary : QString{};
        for (const QVariant &entry : inputs) {
            const QVariantMap input = entry.toMap();
            if (!input.value(u"connected"_qs).toBool() || !input.value(u"required"_qs).toBool()) continue;
            const SavedControllerRecord *record = savedControllerRecord(input.value(u"id"_qs).toString());
            const QStringList identities = record && !record->ownedHidHideDeviceInstances.isEmpty()
                ? record->ownedHidHideDeviceInstances
                : record && !record->hidInstanceId.isEmpty() ? QStringList{record->hidInstanceId} : QStringList{};
            const bool hidden = !identities.isEmpty() && plan.hidhide.cloakKnown
                && std::all_of(identities.cbegin(), identities.cend(), [&plan](const QString &identity) {
                    return std::any_of(plan.hidhide.hiddenDeviceInstanceIds.cbegin(),
                                       plan.hidhide.hiddenDeviceInstanceIds.cend(), [&identity](const QString &candidate) {
                        return ControllerReadinessService::normalizeDeviceInstanceId(candidate)
                            == ControllerReadinessService::normalizeDeviceInstanceId(identity);
                    });
                });
            if (!hidden) {
                physicalVisible = true;
                visiblePhysicalInputIds.append(input.value(u"id"_qs).toString());
            }
        }
        for (const QVariant &entry : outputs) {
            const VirtualOutputLayout *layout = findOutputLayout(m_configuration,
                entry.toMap().value(u"id"_qs).toString());
            if (!layout) continue;
            outputHidden = outputHidden || (layout->hidhideManaged && !layout->hidHideDeviceInstanceId.isEmpty()
                && std::any_of(plan.hidhide.hiddenDeviceInstanceIds.cbegin(), plan.hidhide.hiddenDeviceInstanceIds.cend(),
                    [layout](const QString &candidate) {
                        return ControllerReadinessService::normalizeDeviceInstanceId(candidate)
                            == ControllerReadinessService::normalizeDeviceInstanceId(layout->hidHideDeviceInstanceId);
                    }));
        }
    }

    int sequence = 0;
    const auto append = [&issues, &sequence, &scopeType, &scopeId](const QString &category, const QString &code,
                                              const QString &severity, const QString &objectType,
                                              const QString &objectId, const QString &title,
                                              const QString &explanation, const QString &action,
                                              const QString &actionLabel, bool automaticallyFixable,
                                              bool requiresLiveHardware, int priority,
                                              const QString &technicalDetails = {},
                                              const QStringList &objectIds = {}) {
        AppIssue issue;
        issue.id = QString(u"%1-%2"_qs).arg(code).arg(sequence++);
        issue.category = category;
        issue.code = code;
        issue.severity = severity;
        issue.scopeType = scopeType;
        issue.scopeId = scopeId;
        issue.affectedObjectType = objectType;
        issue.affectedObjectId = objectId;
        issue.affectedObjectIds = objectIds.isEmpty()
            ? (objectId.isEmpty() ? QStringList{} : QStringList{objectId}) : objectIds;
        issue.title = title;
        issue.explanation = explanation;
        issue.recommendedAction = action;
        issue.recommendedActionLabel = actionLabel;
        issue.alternativeActions = QVariantList{u"view-all-details"_qs};
        issue.automaticallyFixable = automaticallyFixable;
        issue.requiresLiveHardware = requiresLiveHardware;
        issue.priority = priority;
        issue.technicalDetails = technicalDetails;
        issue.navigationTarget = issueNavigationTarget(code, objectType, objectId);
        issues.append(issue.toVariantMap());
    };

    const bool deviceScope = scopeType == u"device"_qs;
    const bool outputScope = scopeType == u"virtualOutput"_qs;
    const bool needsPhysicalInput = !outputScope;
    const bool needsVirtualOutput = !deviceScope;
    if (!testFacts && deviceScope) {
        const QString acquisitionFailure = m_setupAssistantDeviceAcquisitionFailures.value(scopeId);
        if (!acquisitionFailure.isEmpty()) {
            const SavedControllerRecord *record = savedControllerRecord(scopeId);
            const QString name = record ? record->displayName : u"selected controller"_qs;
            append(u"PhysicalInput"_qs, u"PhysicalDeviceAcquisitionFailed"_qs, u"error"_qs,
                u"physicalDevice"_qs, scopeId,
                u"HOTAS BF6 could not acquire your "_qs + name,
                acquisitionFailure, u"set-up-device"_qs, u"RETRY ACQUISITION"_qs,
                false, true, 5,
                QString(u"ISSUE\nPhysicalDeviceAcquisitionFailed\n\nTARGET\n%1\n\nRESULT\nThe saved controller matched the discovery inventory, but HOTAS BF6 did not receive a fresh DirectInput report during the 3.5-second acquisition window.\n\nNEXT STEP\nClose other HOTAS BF6 sessions or any program that owns the controller or vJoy Device 1, then retry acquisition. If that does not help, check the controller's HidHide allowlist and Windows driver state.\n\nCURRENT READINESS\n%2"_qs)
                    .arg(name, m_readiness.plan().status));
        }
    }
    if (needsPhysicalInput && inputs.isEmpty()) {
        append(u"PhysicalInput"_qs, u"PhysicalDeviceMissing"_qs, u"setup-needed"_qs,
            u"physicalDevice"_qs, {}, u"Let's set up your controller"_qs,
            u"Connect a physical controller to Windows, then choose Check Setup to continue."_qs,
            u"check-again"_qs, u"CHECK AGAIN"_qs, false, true, 10);
    }

    bool requiredOffline = false;
    bool hasConnectedInput = false;
    bool needsDeviceSetup = false;
    for (const QVariant &entry : needsPhysicalInput ? inputs : QVariantList{}) {
        const QVariantMap input = entry.toMap();
        const QString id = input.value(u"id"_qs).toString();
        const QString name = input.value(u"name"_qs, u"Physical controller"_qs).toString();
        const bool connected = input.value(u"connected"_qs).toBool();
        const bool required = input.value(u"required"_qs, true).toBool();
        if (input.value(u"ambiguous"_qs).toBool()) {
            append(u"Identity"_qs, u"PhysicalDeviceAmbiguous"_qs, u"setup-needed"_qs,
                u"physicalDevice"_qs, id, u"Choose the correct physical controller"_qs,
                u"Windows found more than one possible match for "_qs + name + u". Choose the correct controller before continuing."_qs,
                u"check-again"_qs, u"CHECK AGAIN"_qs, false, true, 15);
            needsDeviceSetup = true;
        } else if (!connected) {
            if (required) {
                append(u"PhysicalInput"_qs, u"PhysicalDeviceOffline"_qs, u"offline"_qs,
                    u"physicalDevice"_qs, id, u"Reconnect your "_qs + name,
                    u"The controller is saved and configured, but it is not currently connected."_qs,
                    u"check-again"_qs, u"CHECK AGAIN"_qs, false, true, 20);
                requiredOffline = true;
            } else {
                append(u"PhysicalInput"_qs, u"OptionalDeviceOffline"_qs, u"note"_qs,
                    u"physicalDevice"_qs, id, name + u" is optional and currently offline"_qs,
                    u"This optional controller remains available for planning and saved mapping edits."_qs,
                    u"none"_qs, {}, false, false, 900);
            }
        } else {
            hasConnectedInput = true;
            if (!input.value(u"verified"_qs).toBool()) {
                append(u"PhysicalInput"_qs, u"PhysicalDeviceUnverified"_qs, u"setup-needed"_qs,
                    u"physicalDevice"_qs, id, u"Finish setting up your "_qs + name,
                    u"HOTAS BF6 found this controller, but it still needs setup before it can be used safely in a game."_qs,
                    u"set-up-device"_qs, u"SET UP DEVICE"_qs, false, true, 30);
                needsDeviceSetup = true;
            } else if (input.value(u"calibrationRequired"_qs).toBool()) {
                append(u"Calibration"_qs, u"CalibrationRequired"_qs, u"setup-needed"_qs,
                    u"physicalDevice"_qs, id, u"Calibrate your controller"_qs,
                    u"HOTAS BF6 needs to learn the full travel of its axes."_qs,
                    u"start-calibration"_qs, u"START CALIBRATION"_qs, false, true, 35);
                needsDeviceSetup = true;
            }
        }
    }
    const bool physicalSetupComplete = !needsPhysicalInput
        || (!inputs.isEmpty() && !requiredOffline && hasConnectedInput && !needsDeviceSetup);

    const QVariantMap output = outputs.isEmpty() ? QVariantMap{} : outputs.front().toMap();
    const QString outputId = output.value(u"id"_qs).toString();
    const QString outputName = output.value(u"name"_qs, u"Virtual controller"_qs).toString();
    bool outputSetupComplete = !needsVirtualOutput;
    if (needsVirtualOutput && (outputs.isEmpty() || !vjoyInstalled || !vjoyPresent)) {
        append(u"Driver"_qs, u"VirtualOutputMissing"_qs, u"setup-needed"_qs,
            u"virtualOutput"_qs, outputId, u"Virtual controller driver needed"_qs,
            u"HOTAS BF6 uses vJoy to present one clean controller to your game."_qs,
            u"setup-vjoy"_qs, u"SET UP VJOY"_qs, false, false, 40);
    } else if (needsVirtualOutput && vjoyBusy && !vjoyOwned) {
        append(u"VirtualOutput"_qs, u"VirtualOutputBusy"_qs, u"busy"_qs,
            u"virtualOutput"_qs, outputId, outputName + u" is already in use"_qs,
            u"Another application currently owns this vJoy device. Close that application, then check setup again."_qs,
            u"check-again"_qs, u"CHECK AGAIN"_qs, false, false, 45);
    } else if (needsVirtualOutput && !vjoySufficient) {
        append(u"VirtualOutput"_qs, u"VirtualOutputMisconfigured"_qs, u"setup-needed"_qs,
            u"virtualOutput"_qs, outputId, outputName + u" needs different capabilities"_qs,
            missingCapabilities.isEmpty()
                ? u"This virtual controller does not have all of the axes, buttons, or POVs required by the current setup."_qs
                : missingCapabilities,
            u"reconfigure-output"_qs, u"RECONFIGURE OUTPUT"_qs, false, false, 50);
    } else {
        outputSetupComplete = true;
    }
    // Visibility, routing, and live proof are consequences of the first
    // unresolved physical/output prerequisite. Do not present those
    // consequences as separate repair jobs while their root cause is open.
    bool visibilitySetupComplete = false;
    if (physicalSetupComplete && outputSetupComplete) {
    visibilitySetupComplete = hidhideInstalled && hidhideReady;
    if (!hidhideInstalled || !hidhideReady) {
        append(u"Driver"_qs, u"HidHideUnavailable"_qs, u"setup-needed"_qs,
            u"gameVisibility"_qs, {}, u"Game visibility protection needs setup"_qs,
            u"HOTAS BF6 needs HidHide so games see the virtual controller instead of duplicate physical controls."_qs,
            u"setup-hidhide"_qs, u"SET UP HIDHIDE"_qs, false, false, 60);
    }
    if (physicalVisible && !visiblePhysicalInputIds.isEmpty()) {
        QStringList targetNames;
        QStringList directInputIds;
        QStringList hidInstances;
        bool exactIdentityAvailable = true;
        for (const QString &recordId : visiblePhysicalInputIds) {
            const SavedControllerRecord *record = savedControllerRecord(recordId);
            const auto inputFound = std::find_if(inputs.cbegin(), inputs.cend(), [&recordId](const QVariant &entry) {
                return entry.toMap().value(u"id"_qs).toString() == recordId;
            });
            const QVariantMap testInput = inputFound == inputs.cend() ? QVariantMap{} : inputFound->toMap();
            targetNames.append(record ? record->displayName
                                      : testInput.value(u"name"_qs, u"Physical controller"_qs).toString());
            if (record) {
                directInputIds.append(record->lastDirectInputId);
                if (!record->ownedHidHideDeviceInstances.isEmpty()) {
                    hidInstances.append(record->ownedHidHideDeviceInstances);
                } else {
                    hidInstances.append(record->hidInstanceId);
                }
                exactIdentityAvailable = exactIdentityAvailable && !record->hidInstanceId.trimmed().isEmpty();
            } else if (!testFacts) {
                exactIdentityAvailable = false;
            }
        }
        directInputIds.removeAll(QString{});
        hidInstances.removeAll(QString{});
        const bool visibilityFixable = testFacts
            ? boolFact(u"visibilityActionAvailable"_qs, hidhideReady)
            : exactIdentityAvailable && m_readiness.plan().hidhide.installed
                && m_readiness.plan().hidhide.cliAvailable && m_readiness.plan().hidhide.serviceReady
                && m_readiness.plan().hidhide.mapperAllowlisted && m_readiness.plan().hidhide.cloakKnown
                && m_readiness.plan().hidhide.cloaked;
        const QString technical = QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nDIRECTINPUT ID\n%3\n\nHID INSTANCE(S)\n%4\n\nHIDHIDE\nInstalled: %5\nCLI available: %6\nService ready: %7\nMapper allowlisted: %8\nCloaking enabled: %9\nCurrent hidden state: visible\n\nREQUESTED ACTION\nHide from games\n\nCOMMAND/TRANSACTION RESULT\nNot attempted\n\nEXIT CODE\nNot run\n\nREADBACK RESULT\nPending\n\nROLLBACK\nNot needed\n\nLAST CHECK\n%10"_qs)
            .arg(targetNames.join(u", "_qs), visiblePhysicalInputIds.join(u", "_qs),
                 directInputIds.join(u", "_qs), hidInstances.join(u", "_qs),
                 hidhideInstalled ? u"yes"_qs : u"no"_qs,
                 testFacts ? (hidhideReady ? u"yes"_qs : u"no"_qs)
                           : (m_readiness.plan().hidhide.cliAvailable ? u"yes"_qs : u"no"_qs),
                 testFacts ? (hidhideReady ? u"yes"_qs : u"no"_qs)
                           : (m_readiness.plan().hidhide.serviceReady ? u"yes"_qs : u"no"_qs),
                 testFacts ? (hidhideReady ? u"yes"_qs : u"no"_qs)
                           : (m_readiness.plan().hidhide.mapperAllowlisted ? u"yes"_qs : u"no"_qs),
                 testFacts ? (hidhideReady ? u"yes"_qs : u"no"_qs)
                           : (m_readiness.plan().hidhide.cloaked ? u"yes"_qs : u"no"_qs),
                 m_readiness.plan().lastChecked.isValid()
                     ? m_readiness.plan().lastChecked.toString(Qt::ISODate) : u"Not recorded"_qs);
        append(u"Visibility"_qs, u"PhysicalInputVisible"_qs, u"setup-needed"_qs,
            u"physicalDevice"_qs, visiblePhysicalInputIds.front(),
            u"Hide "_qs + targetNames.front() + u" from games"_qs,
            u"Your game may detect both "_qs + targetNames.front()
                + u" and the HOTAS BF6 virtual controller. Hiding the physical controller avoids duplicate controls."_qs,
            u"hide-from-games"_qs, u"HIDE FROM GAMES"_qs, visibilityFixable,
            false, 70, technical, visiblePhysicalInputIds);
        visibilitySetupComplete = false;
    }
    if (outputHidden) {
        append(u"Visibility"_qs, u"VirtualOutputHidden"_qs, u"setup-needed"_qs,
            u"virtualOutput"_qs, outputId, u"Show your virtual controller to games"_qs,
            u"Games need to see "_qs + outputName + u" to use your HOTAS BF6 mappings."_qs,
            u"check-again"_qs, u"CHECK VISIBILITY"_qs, false, false, 75);
        visibilitySetupComplete = false;
    }
    }
    const bool prerequisitesReadyForRouting = physicalSetupComplete && outputSetupComplete
        && visibilitySetupComplete;
    if (prerequisitesReadyForRouting && routingConflict) {
        append(u"Routing"_qs, u"RoutingConflict"_qs, u"setup-needed"_qs,
            u"deviceRig"_qs, activeDeviceRigId(), u"Review routing"_qs,
            routingDetails.isEmpty() ? u"Two controls need the same output. Review the conflicting routes before testing."_qs
                                     : routingDetails,
            u"review-routing"_qs, u"REVIEW ROUTING"_qs, false, false, 80);
    }
    if (prerequisitesReadyForRouting && noMappedControl && !deviceScope) {
        const QString affectedType = scopeType == u"device"_qs ? u"physicalDevice"_qs
            : scopeType == u"virtualOutput"_qs ? u"virtualOutput"_qs : u"deviceRig"_qs;
        const QString affectedId = scopeType == u"application"_qs ? activeDeviceRigId() : scopeId;
        append(u"Routing"_qs, u"NoMappedControl"_qs, u"setup-needed"_qs,
            affectedType, affectedId, u"No mapped control to test"_qs,
            u"This setup has no enabled axis, button, or POV route for the selected scope. Assign a control before running live output proof."_qs,
            u"review-routing"_qs, u"OPEN MAPPING"_qs, false, false, 85);
    }
    // Meaningful input/output evidence is continuously observed by existing
    // fixed atomics and rendered as optional activity. It is deliberately not
    // emitted as a setup issue or a blocking prerequisite.
    return issues;
}

QVariantList AppBackend::setupAssistantSteps() const
{
    struct StepDefinition {
        QString id;
        QString title;
        QString message;
        bool required = true;
        bool informational = false;
    };

    const QString scopeType = m_setupAssistantScopeType;
    const bool deviceScope = scopeType == u"device"_qs;
    const bool outputScope = scopeType == u"virtualOutput"_qs;
    const QList<StepDefinition> definitions = deviceScope
        ? QList<StepDefinition>{{u"device"_qs, u"DEVICE"_qs,
                                 u"Confirm this physical controller is identified, connected, and verified."_qs},
                                {u"calibration"_qs, u"CALIBRATION"_qs,
                                 u"Using the default controller range. Calibration is optional."_qs, false},
                                {u"visibility"_qs, u"GAME VISIBILITY"_qs,
                                 u"Keep this physical controller out of games to avoid duplicate controls."_qs}}
        : outputScope
            ? QList<StepDefinition>{{u"output"_qs, u"OUTPUT EXISTS"_qs,
                                     u"Confirm the selected virtual output is available."_qs},
                                    {u"capabilities"_qs, u"CAPABILITIES"_qs,
                                     u"Confirm its axes, buttons, and POVs satisfy the selected output."_qs},
                                    {u"visibility"_qs, u"GAME VISIBILITY"_qs,
                                     u"Games must be able to see the selected virtual output."_qs}}
            : QList<StepDefinition>{{u"physical"_qs, u"PHYSICAL INPUTS"_qs,
                                     u"Confirm every required physical controller is identified and ready."_qs},
                                    {u"output"_qs, u"VIRTUAL CONTROLLER"_qs,
                                     u"Confirm the selected vJoy output exists and has the needed capabilities."_qs},
                                    {u"visibility"_qs, u"GAME VISIBILITY"_qs,
                                     u"Keep physical inputs out of games while keeping the virtual controller visible."_qs}};

    const auto stepIdForIssue = [deviceScope, outputScope](const QVariantMap &issue) {
        const QString code = issue.value(u"code"_qs).toString();
        const QString category = issue.value(u"category"_qs).toString();
        if (deviceScope) {
            if (category == u"Calibration"_qs) return u"calibration"_qs;
            if (category == u"Visibility"_qs || code == u"HidHideUnavailable"_qs) return u"visibility"_qs;
            if (category == u"PhysicalInput"_qs || category == u"Identity"_qs) return u"device"_qs;
            return u"device"_qs;
        }
        if (outputScope) {
            if (code == u"VirtualOutputMissing"_qs) return u"output"_qs;
            if (category == u"Visibility"_qs || code == u"HidHideUnavailable"_qs) return u"visibility"_qs;
            if (code == u"VirtualOutputMisconfigured"_qs || category == u"VirtualOutput"_qs) return u"capabilities"_qs;
            return u"capabilities"_qs;
        }
        if (category == u"PhysicalInput"_qs || category == u"Identity"_qs || category == u"Calibration"_qs) {
            return u"physical"_qs;
        }
        if (category == u"Visibility"_qs || code == u"HidHideUnavailable"_qs) return u"visibility"_qs;
        if (category == u"VirtualOutput"_qs || (category == u"Driver"_qs && code != u"HidHideUnavailable"_qs)) {
            return u"output"_qs;
        }
        return u"output"_qs;
    };

    QHash<QString, QVariantMap> blockingIssues;
    QHash<QString, QVariantMap> optionalIssues;
    for (const QVariant &entry : setupAssistantIssues()) {
        const QVariantMap issue = entry.toMap();
        const QString stepId = stepIdForIssue(issue);
        const bool optional = issue.value(u"severity"_qs).toString() == u"note"_qs
            || issue.value(u"code"_qs).toString() == u"OptionalDeviceOffline"_qs;
        QHash<QString, QVariantMap> &destination = optional ? optionalIssues : blockingIssues;
        if (!destination.contains(stepId)
            || issue.value(u"priority"_qs).toInt() < destination.value(stepId).value(u"priority"_qs).toInt()) {
            destination.insert(stepId, issue);
        }
    }

    int currentIndex = -1;
    for (int index = 0; index < definitions.size(); ++index) {
        if (blockingIssues.contains(definitions.at(index).id)) {
            currentIndex = index;
            break;
        }
    }

    QVariantList steps;
    steps.reserve(definitions.size());
    for (int index = 0; index < definitions.size(); ++index) {
        const StepDefinition &definition = definitions.at(index);
        const QVariantMap issue = blockingIssues.value(definition.id);
        const QVariantMap optionalIssue = optionalIssues.value(definition.id);
        const bool required = definition.required || !issue.isEmpty();
        const bool isOptional = !required;
        const bool isCurrent = currentIndex == index;
        // Optional guidance remains visibly optional while a required step is
        // unresolved; it is never a yellow prerequisite in disguise.
        const bool isBlocked = !isOptional && currentIndex >= 0 && index > currentIndex;
        const QString state = isOptional ? u"optional"_qs : isCurrent ? u"current"_qs
            : isBlocked ? u"blocked"_qs : u"complete"_qs;
        QVariantMap presentationIssue = !issue.isEmpty() ? issue : optionalIssue;
        if (presentationIssue.isEmpty() && definition.id == u"calibration"_qs) {
            presentationIssue = {{u"explanation"_qs, definition.message},
                                 {u"recommendedAction"_qs, u"start-calibration"_qs},
                                 {u"recommendedActionLabel"_qs, u"CALIBRATE"_qs},
                                 {u"requiresLiveHardware"_qs, true}};
        }
        const QString prerequisiteMessage = currentIndex >= 0 && index > currentIndex
            ? QString(u"Complete Step %1 first."_qs).arg(currentIndex + 1) : QString{};
        const auto completedMessage = [&definition] {
            if (definition.id == u"device"_qs || definition.id == u"physical"_qs) {
                return u"Connected controller identity and required setup are verified."_qs;
            }
            if (definition.id == u"output"_qs || definition.id == u"capabilities"_qs) {
                return u"The selected virtual output has the required capabilities."_qs;
            }
            if (definition.id == u"visibility"_qs) return u"Game visibility is configured for this scope."_qs;
            return definition.message;
        };
        const QString message = isBlocked ? prerequisiteMessage
            : state == u"complete"_qs ? completedMessage()
            : presentationIssue.value(u"explanation"_qs, definition.message).toString();
        steps.append(QVariantMap{{u"id"_qs, definition.id}, {u"order"_qs, index + 1},
            {u"state"_qs, state}, {u"current"_qs, isCurrent}, {u"blocked"_qs, isBlocked},
            {u"required"_qs, required}, {u"optional"_qs, isOptional},
            {u"informational"_qs, definition.informational}, {u"skipped"_qs, false},
            {u"complete"_qs, state == u"complete"_qs}, {u"title"_qs, definition.title},
            {u"message"_qs, message},
            {u"blockingIssueCode"_qs, issue.value(u"code"_qs).toString()},
            {u"action"_qs, presentationIssue.value(u"recommendedAction"_qs).toString()},
            {u"actionLabel"_qs, presentationIssue.value(u"recommendedActionLabel"_qs).toString()},
            {u"requiresLiveHardware"_qs, presentationIssue.value(u"requiresLiveHardware"_qs).toBool()},
            {u"technicalDetails"_qs, presentationIssue.value(u"technicalDetails"_qs).toString()},
            {u"issue"_qs, presentationIssue}});
    }
    return steps;
}

QVariantMap AppBackend::setupAssistantSummary() const
{
    const QVariantList issues = setupAssistantIssues();
    const QVariantList steps = setupAssistantSteps();
    QVariantMap primaryIssue;
    QStringList notes;
    for (const QVariant &entry : issues) {
        const QVariantMap issue = entry.toMap();
        const QString severity = issue.value(u"severity"_qs).toString();
        if (severity == u"note"_qs) {
            notes.append(issue.value(u"title"_qs).toString());
        }
    }
    for (const QVariant &entry : steps) {
        const QVariantMap step = entry.toMap();
        if (step.value(u"state"_qs).toString() != u"current"_qs) continue;
        primaryIssue = step.value(u"issue"_qs).toMap();
        break;
    }
    if (primaryIssue.isEmpty()) {
        for (const QVariant &entry : issues) {
            const QVariantMap issue = entry.toMap();
            if (issue.value(u"severity"_qs).toString() == u"note"_qs) continue;
            if (primaryIssue.isEmpty()
                || issue.value(u"priority"_qs).toInt() < primaryIssue.value(u"priority"_qs).toInt()) {
                primaryIssue = issue;
            }
        }
    }
    const QString scope = m_setupAssistantScopeType == u"deviceRig"_qs
        ? (setupAssistantDeviceRig(m_setupAssistantScopeType, m_setupAssistantScopeId)
               ? setupAssistantDeviceRig(m_setupAssistantScopeType, m_setupAssistantScopeId)->name
               : u"Device Rig"_qs)
        : m_setupAssistantScopeType == u"device"_qs
            ? (savedControllerRecord(m_setupAssistantScopeId)
                   ? savedControllerRecord(m_setupAssistantScopeId)->displayName
                   : m_setupAssistantTestFacts.value(u"scopeLabel"_qs, u"Physical controller"_qs).toString())
        : m_setupAssistantScopeType == u"virtualOutput"_qs
            ? (findOutputLayout(m_configuration, m_setupAssistantScopeId)
                   ? findOutputLayout(m_configuration, m_setupAssistantScopeId)->name
                   : m_setupAssistantTestFacts.value(u"scopeLabel"_qs, u"Virtual Output"_qs).toString())
        : !m_configuration.activeDeviceRigId.isEmpty()
            ? activeDeviceRig() ? activeDeviceRig()->name : u"Device Rig"_qs
        : activeOutputLayout() ? activeOutputLayout()->name
            : m_setupAssistantTestFacts.value(u"scopeLabel"_qs, deviceName()).toString();
    bool requiredStepIncomplete = false;
    for (const QVariant &entry : steps) {
        const QVariantMap step = entry.toMap();
        if (step.value(u"required"_qs).toBool()
            && step.value(u"state"_qs).toString() != u"complete"_qs) {
            requiredStepIncomplete = true;
            break;
        }
    }
    QVariantList readyItems;
    if (m_setupAssistantScopeType == u"device"_qs) {
        readyItems = {u"Device recognized"_qs, u"Connected and verified"_qs,
                      u"Game visibility configured"_qs};
    } else if (m_setupAssistantScopeType == u"virtualOutput"_qs) {
        readyItems = {u"vJoy device available"_qs, u"Required capabilities present"_qs,
                      u"Game visibility configured"_qs};
    } else {
        readyItems = {u"Required physical inputs ready"_qs, u"Virtual output ready"_qs,
                      u"Game visibility configured"_qs};
    }
    QString state = u"READY"_qs;
    QString title = u"Your setup is ready"_qs;
    QString message = scope + u" is ready to use."_qs;
    if (m_verificationInProgress || m_readiness.plan().isChecking) {
        state = u"WAITING"_qs;
        title = u"Checking your setup"_qs;
        message = u"HOTAS BF6 is checking your controller, virtual controller, and game visibility."_qs;
    } else if (!primaryIssue.isEmpty() || requiredStepIncomplete) {
        const QString severity = primaryIssue.value(u"severity"_qs).toString();
        state = severity == u"offline"_qs ? u"OFFLINE"_qs
            : severity == u"busy"_qs ? u"BUSY"_qs
            : severity == u"waiting"_qs ? u"WAITING"_qs : u"SETUP NEEDED"_qs;
        title = primaryIssue.value(u"title"_qs, u"Complete the required setup steps"_qs).toString();
        message = primaryIssue.value(u"explanation"_qs,
            u"HOTAS BF6 is still waiting for a required setup condition."_qs).toString();
    }
    return QVariantMap{{u"state"_qs, state}, {u"title"_qs, title}, {u"message"_qs, message},
        {u"scope"_qs, scope}, {u"issueCount"_qs, issues.size()},
        {u"primaryIssue"_qs, primaryIssue}, {u"steps"_qs, steps}, {u"visibleSteps"_qs, steps},
        {u"readyItems"_qs, readyItems},
        {u"secondaryMessage"_qs, notes.join(u"\n"_qs)},
        {u"scopeType"_qs, m_setupAssistantScopeType}, {u"scopeId"_qs, m_setupAssistantScopeId},
        {u"primaryAction"_qs, primaryIssue.value(u"recommendedAction"_qs, u"done"_qs)},
        {u"primaryActionLabel"_qs, primaryIssue.value(u"recommendedActionLabel"_qs, u"DONE"_qs)}};
}

QVariantMap AppBackend::buildSetupTruthSnapshot() const
{
    const QDateTime now = QDateTime::currentDateTime();
    const ControllerReadinessPlan &plan = m_readiness.plan();
    const bool checking = m_verificationInProgress || plan.isChecking;
    const bool inspected = plan.lastChecked.isValid()
        && plan.lastChecked.secsTo(now) <= 20;
    const auto statusValue = [](SetupTruthStatus status) {
        return setupTruthStatusLabel(status);
    };
    const auto severity = [](SetupTruthStatus status) {
        return status == SetupTruthStatus::Ready ? u"ready"_qs
            : status == SetupTruthStatus::Checking ? u"checking"_qs
            : status == SetupTruthStatus::Unknown ? u"unknown"_qs
            : status == SetupTruthStatus::Failed || status == SetupTruthStatus::Unavailable ? u"error"_qs
            : status == SetupTruthStatus::WaitingForUser ? u"waiting"_qs : u"attention"_qs;
    };
    QVariantList groups;
    QVariantList issues;
    QVariantList repairPlan;
    const auto addGroup = [&](const QString &id, const QString &title, SetupTruthStatus status,
                              const QString &detail, const QVariantMap &evidence = {}) {
        groups.append(QVariantMap{{u"id"_qs, id}, {u"title"_qs, title},
            {u"status"_qs, statusValue(status)}, {u"severity"_qs, severity(status)},
            {u"detail"_qs, detail}, {u"evidence"_qs, evidence}});
    };
    const auto addIssue = [&](const QString &code, const QString &subsystem, SetupTruthStatus status,
                              const QString &title, const QString &detail, bool automatic,
                              bool elevated, const QString &action, const QVariantMap &evidence = {}) {
        const QString id = code + u":"_qs + QString::number(issues.size() + 1);
        const QString recordId = evidence.value(u"recordId"_qs).toString();
        const QString layoutId = evidence.value(u"layoutId"_qs).toString();
        const QString affectedId = !recordId.isEmpty() ? recordId : layoutId;
        issues.append(QVariantMap{{u"id"_qs, id}, {u"code"_qs, code}, {u"subsystem"_qs, subsystem},
            {u"severity"_qs, statusValue(status)}, {u"title"_qs, title}, {u"explanation"_qs, detail},
            {u"repairable"_qs, automatic}, {u"automatic"_qs, automatic},
            {u"requiresElevation"_qs, elevated}, {u"proposedRepair"_qs, action},
            {u"requiresReconnect"_qs, code == u"HidHideMismatch"_qs},
            {u"risk"_qs, elevated ? u"Scoped driver configuration with mandatory read-back."_qs
                                     : u"No driver configuration is required."_qs},
            {u"manualFallback"_qs, automatic ? u"Review diagnostics if the scoped repair cannot complete."_qs
                                                 : u"Review technical diagnostics, then run another read-only check."_qs},
            {u"navigationTarget"_qs, subsystem}, {u"affectedObjectIds"_qs, QStringList{affectedId}},
            {u"evidence"_qs, evidence}});
        if (automatic) repairPlan.append(QVariantMap{{u"id"_qs, id}, {u"subsystem"_qs, subsystem},
            {u"title"_qs, action}, {u"requiresElevation"_qs, elevated}, {u"status"_qs, u"WAITING"_qs}});
    };

    const DeviceRig *rig = activeDeviceRig();
    if (!rig && !m_configuration.editingDeviceRigId.isEmpty())
        rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    if (!rig) {
        const auto enabled = std::find_if(m_configuration.deviceRigs.cbegin(), m_configuration.deviceRigs.cend(),
            [](const DeviceRig &candidate) { return candidate.enabled; });
        if (enabled != m_configuration.deviceRigs.cend()) rig = &*enabled;
    }

    const DeviceRigStatus *rigStatus = nullptr;
    if (rig) {
        const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [rig](const DeviceRigStatus &candidate) { return candidate.rigId == rig->id; });
        if (found != m_deviceRigStatuses.cend()) rigStatus = &*found;
    }
    QVariantList physicalMembers;
    bool requiredOffline = false;
    bool requiredAmbiguous = false;
    bool requiredUnverified = false;
    bool optionalUnverified = false;
    QString connectedRecoveryRecordId;
    QString connectedRecoveryName;
    if (!rig) {
        const SetupTruthStatus state = checking ? SetupTruthStatus::Checking
            : SetupTruthStatus::WaitingForUser;
        addGroup(u"physical"_qs, u"Physical input"_qs, state,
            checking ? u"Reading physical controller inventory."_qs : u"Select or create a Device Rig to inspect its physical inputs."_qs);
        addGroup(u"verification"_qs, u"Controller verification"_qs, state,
            checking ? u"Waiting for the Device Rig inspection."_qs : u"A selected Device Rig is required."_qs);
    } else {
        for (const DeviceRigMember &member : rig->members) {
            if (!member.enabled) continue;
            const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
            const bool ambiguous = rigStatus && rigStatus->ambiguousMemberIds.contains(member.controllerRecordId);
            const auto discovered = std::find_if(m_discoveredControllers.cbegin(), m_discoveredControllers.cend(),
                [this, &member](const DiscoveredController &candidate) {
                    if (!candidate.connected || candidate.virtualDevice) return false;
                    const ControllerMatch match = ControllerManager::match(candidate, m_configuration.savedControllers);
                    return !match.ambiguous && match.recordId == member.controllerRecordId;
                });
            const bool connected = !ambiguous && discovered != m_discoveredControllers.cend();
            const bool reports = connected && discovered->directInputId == deviceId()
                && currentPhysicalCapabilities().inputReportsReceived;
            const bool verified = record && !record->lastVerified.isEmpty();
            const QString name = record ? record->displayName : u"Unknown saved controller"_qs;
            const QString identity = connected ? discovered->hidInstanceId
                : record ? record->hidInstanceId : QString{};
            physicalMembers.append(QVariantMap{{u"recordId"_qs, member.controllerRecordId}, {u"name"_qs, name},
                {u"required"_qs, member.required}, {u"connected"_qs, connected}, {u"directInputAcquired"_qs, connected && discovered->directInputId == deviceId()},
                {u"inputReportsReceived"_qs, reports}, {u"identityVerified"_qs, verified},
                {u"lastVerified"_qs, record ? record->lastVerified : QString{}},
                {u"savedHidIdentity"_qs, record ? record->hidInstanceId : QString{}},
                {u"observedHidIdentity"_qs, identity},
                {u"identityMatch"_qs, connected}, {u"ambiguous"_qs, ambiguous},
                {u"axisCount"_qs, record ? record->axisCount : 0},
                {u"buttons"_qs, record ? record->buttonCount : 0},
                {u"povs"_qs, record ? record->povCount : 0},
                {u"meaningfulInputObserved"_qs, connected && currentPhysicalCapabilities().directInputId == discovered->directInputId
                    && m_worker.runtime().meaningfulInputSequence.load(std::memory_order_relaxed) > 0},
                {u"calibrationRequired"_qs, connected && currentPhysicalCapabilities().directInputId == discovered->directInputId
                    && calibrationNeedsSetup(currentPhysicalCapabilities())}});
            if (member.required && !connected) requiredOffline = true;
            if (member.required && ambiguous) requiredAmbiguous = true;
            if (connected && !verified && member.required) requiredUnverified = true;
            if (connected && !verified && !member.required) optionalUnverified = true;
            if (connected && member.required && connectedRecoveryRecordId.isEmpty()) {
                connectedRecoveryRecordId = member.controllerRecordId;
                connectedRecoveryName = name;
            }
            if (connected && !verified) {
                addIssue(u"PhysicalDeviceUnverified"_qs, u"Controller verification"_qs,
                    member.required ? SetupTruthStatus::Repairable : SetupTruthStatus::Attention,
                    u"Controller identity has not been committed"_qs,
                    name + u" is connected with an exact saved identity, but its verification timestamp is empty."_qs,
                    member.required, false, u"Verify and save this exact controller identity"_qs,
                    QVariantMap{{u"recordId"_qs, member.controllerRecordId}, {u"observedIdentity"_qs, identity},
                                {u"lastVerified"_qs, record ? record->lastVerified : QString{}}});
            }
        }
        SetupTruthStatus physicalState = SetupTruthStatus::Ready;
        QString physicalDetail = u"Every required controller is connected."_qs;
        if (checking) { physicalState = SetupTruthStatus::Checking; physicalDetail = u"Resolving exact DirectInput and HID identities."_qs; }
        else if (!m_controllerInventoryInitialized) { physicalState = SetupTruthStatus::Unknown; physicalDetail = u"Controller inventory has not completed a fresh inspection."_qs; }
        else if (requiredAmbiguous) { physicalState = SetupTruthStatus::WaitingForUser; physicalDetail = u"More than one discovered controller matches a saved Device Rig member."_qs; }
        else if (requiredOffline) { physicalState = SetupTruthStatus::WaitingForUser; physicalDetail = u"Reconnect every required controller, then run Check & Repair Setup."_qs; }
        addGroup(u"physical"_qs, u"Physical input"_qs, physicalState, physicalDetail,
            QVariantMap{{u"members"_qs, physicalMembers}});
        const bool recoveryProofPending = m_readiness.hasPendingRecovery();
        SetupTruthStatus verificationState = checking ? SetupTruthStatus::Checking
            : requiredUnverified ? SetupTruthStatus::Repairable
            : recoveryProofPending && !connectedRecoveryRecordId.isEmpty() ? SetupTruthStatus::Repairable
            : optionalUnverified ? SetupTruthStatus::Attention
            : requiredOffline ? SetupTruthStatus::WaitingForUser : SetupTruthStatus::Ready;
        addGroup(u"verification"_qs, u"Controller verification"_qs, verificationState,
            checking ? u"Checking persisted identity against the live controller."_qs
            : requiredUnverified ? u"A required connected controller can be verified without changing vJoy or HidHide."_qs
            : recoveryProofPending && !connectedRecoveryRecordId.isEmpty()
                ? u"A prior automatic repair needs one fresh exact-controller proof; HidHide and vJoy state are shown separately."_qs
            : optionalUnverified ? u"An optional connected controller has not yet been committed as verified."_qs
            : requiredOffline ? u"Verification requires the selected controller to be connected."_qs
            : u"Every required connected controller has a saved verification record."_qs,
            QVariantMap{{u"members"_qs, physicalMembers}});
        if (recoveryProofPending && !requiredUnverified && !connectedRecoveryRecordId.isEmpty()) {
            addIssue(u"RecoveryVerificationPending"_qs, u"Controller verification"_qs,
                SetupTruthStatus::Repairable, u"Finish post-repair controller verification"_qs,
                connectedRecoveryName + u" needs a fresh exact identity and live-input proof before HOTAS BF6 can retire its prior repair recovery record."_qs,
                true, false, u"Verify the exact controller and complete recovery proof"_qs,
                QVariantMap{{u"recordId"_qs, connectedRecoveryRecordId}});
        }
    }

    // A rig may have multiple enabled outputs. Keep an independent evidence
    // record for each descriptor; never let the currently active profile's
    // vJoy device make another selected-rig output look healthy.
    QVariantList outputEvidence;
    SetupTruthStatus vjoyState = SetupTruthStatus::Ready;
    QString vjoyDetail = u"Every enabled Device Rig output has fresh descriptor evidence."_qs;
    bool anyOutput = false;
    QSet<QString> outputIds;
    const auto inspectOutput = [&](const QString &layoutId, bool enabled) {
        if (!enabled || layoutId.isEmpty() || outputIds.contains(layoutId)) return;
        outputIds.insert(layoutId);
        anyOutput = true;
        const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
        const ControllerReadinessPlan *outputPlan = virtualOutputReadinessPlan(layoutId);
        SetupTruthStatus state = SetupTruthStatus::Unknown;
        QString detail = u"No fresh inspection has completed for this virtual output."_qs;
        if (!layout) {
            state = SetupTruthStatus::Unavailable;
            detail = u"The Device Rig references a virtual output layout that no longer exists."_qs;
        } else if (checking) {
            state = SetupTruthStatus::Checking;
            detail = QString(u"Reading vJoy Device %1 descriptor."_qs).arg(layout->requirements.deviceId);
        } else if (!outputPlan || !outputPlan->lastChecked.isValid()
                   || outputPlan->lastChecked.secsTo(now) > 20) {
            state = SetupTruthStatus::Unknown;
        } else if (!outputPlan->vjoy.installed) {
            state = SetupTruthStatus::Unavailable;
            detail = outputPlan->vjoy.diagnostic;
        } else if (!outputPlan->vjoy.inspectionComplete) {
            state = SetupTruthStatus::Unknown;
            detail = u"vJoy inspection did not complete every required supported read."_qs;
        } else if (outputPlan->vjoyNeedsChanges && outputPlan->vjoy.busy && !outputPlan->vjoy.ownedByHotasBf6) {
            state = SetupTruthStatus::WaitingForUser;
            detail = outputPlan->vjoySummary;
        } else if (outputPlan->vjoyNeedsChanges && outputPlan->vjoyCanApply) {
            state = SetupTruthStatus::Repairable;
            detail = outputPlan->vjoySummary;
        } else if (outputPlan->vjoyNeedsChanges) {
            state = SetupTruthStatus::Attention;
            detail = outputPlan->vjoySummary;
        } else {
            state = SetupTruthStatus::Ready;
            detail = outputPlan->vjoySummary;
        }
        if (setupTruthStatusPriority(state) < setupTruthStatusPriority(vjoyState)) {
            vjoyState = state;
            vjoyDetail = detail;
        }
        const VJoyCapabilities capabilities = outputPlan ? outputPlan->vjoy : VJoyCapabilities{};
        // A Device Rig can retain a reference to a deleted layout.  That is
        // an inspectable, user-repairable configuration defect, not a reason
        // for setup diagnostics to dereference a null layout and crash.
        const MapperOutputRequirements requirements = outputPlan ? outputPlan->requirements
            : layout ? ControllerReadinessService::requirementsFor(layout->requirements)
                     : MapperOutputRequirements{};
        outputEvidence.append(QVariantMap{{u"layoutId"_qs, layoutId}, {u"layoutName"_qs, layout ? layout->name : u"Missing output"_qs},
            {u"status"_qs, statusValue(state)}, {u"detail"_qs, detail}, {u"deviceId"_qs, layout ? layout->requirements.deviceId : 0},
            {u"installed"_qs, capabilities.installed}, {u"devicePresent"_qs, capabilities.devicePresent},
            {u"busy"_qs, capabilities.busy}, {u"ownedByHotasBf6"_qs, capabilities.ownedByHotasBf6},
            {u"outputReportsSucceeding"_qs, capabilities.outputReportsSucceeding}, {u"buttons"_qs, capabilities.buttons},
            {u"continuousPovs"_qs, capabilities.continuousPovs}, {u"discretePovs"_qs, capabilities.discretePovs},
            {u"requiredButtons"_qs, requirements.buttons}, {u"requiredContinuousPovs"_qs, requirements.continuousPovs},
            {u"requiredDiscretePovs"_qs, requirements.discretePovs}, {u"forceFeedbackKnown"_qs, capabilities.forceFeedbackKnown},
            {u"rollbackSnapshotAvailable"_qs, !capabilities.devicePresent || !capabilities.restoreCommand.isEmpty()},
            {u"rawDescriptor"_qs, capabilities.descriptorReport}, {u"rawConfiguration"_qs, capabilities.configurationReport},
            {u"rawDeviceList"_qs, capabilities.deviceListReport}, {u"rawInspection"_qs, capabilities.diagnostic}});
        if (state == SetupTruthStatus::Repairable) {
            addIssue(u"VJoyDescriptorMismatch"_qs, u"Virtual output"_qs, state,
                QString(u"%1 needs a safe vJoy descriptor repair"_qs).arg(layout->name), detail, true, true,
                u"Configure this vJoy descriptor and prove it by read-back"_qs,
                QVariantMap{{u"layoutId"_qs, layoutId}, {u"deviceId"_qs, layout->requirements.deviceId}});
        } else if (state == SetupTruthStatus::Unknown) {
            addIssue(u"VJoyInspectionFailed"_qs, u"Virtual output"_qs, state,
                QString(u"%1 vJoy inspection is incomplete"_qs).arg(layout ? layout->name : u"Virtual output"_qs), detail,
                false, false, u"Review vJoy diagnostics and run Check again"_qs,
                QVariantMap{{u"layoutId"_qs, layoutId}});
        }
    };
    if (rig) {
        for (const DeviceRigOutputTarget &target : rig->outputs) inspectOutput(target.outputLayoutId, target.enabled);
    } else if (const VirtualOutputLayout *active = activeOutputLayout()) {
        inspectOutput(active->id, true);
    }
    if (!anyOutput) {
        vjoyState = checking ? SetupTruthStatus::Checking : SetupTruthStatus::WaitingForUser;
        vjoyDetail = checking ? u"Waiting for Device Rig output selection."_qs
                              : u"Select an enabled Virtual Output in the Device Rig before setup can inspect vJoy."_qs;
    }
    addGroup(u"vjoy"_qs, u"Virtual output"_qs, vjoyState, vjoyDetail,
        QVariantMap{{u"outputs"_qs, outputEvidence}});

    SetupTruthStatus hidState = SetupTruthStatus::Unknown;
    QString hidDetail = u"No fresh HidHide inspection has completed."_qs;
    if (checking) { hidState = SetupTruthStatus::Checking; hidDetail = u"Reading HidHide service, allowlist, cloak state, and device collections."_qs; }
    else if (inspected && !plan.hidhide.installed) { hidState = SetupTruthStatus::Unavailable; hidDetail = plan.hidhide.diagnostic; }
    else if (inspected && !plan.hidhide.inspectionComplete) { hidState = SetupTruthStatus::Unknown; hidDetail = plan.hidhide.diagnostic.isEmpty() ? u"HidHide inspection did not complete every required supported read."_qs : plan.hidhide.diagnostic; }
    else if (inspected && !plan.hidhide.cloakKnown) { hidState = SetupTruthStatus::Unknown; hidDetail = plan.hidhide.diagnostic.isEmpty() ? u"HidHide did not return a readable cloak state."_qs : plan.hidhide.diagnostic; }
    else if (inspected && plan.hidhideNeedsChanges && plan.hidhideCanApply) { hidState = SetupTruthStatus::Repairable; hidDetail = plan.hidhideSummary; }
    else if (inspected && plan.hidhideNeedsChanges) { hidState = SetupTruthStatus::Attention; hidDetail = plan.hidhideSummary; }
    else if (inspected) { hidState = SetupTruthStatus::Ready; hidDetail = plan.hidhideSummary; }
    addGroup(u"isolation"_qs, u"Device isolation"_qs, hidState, hidDetail,
        QVariantMap{{u"cloakKnown"_qs, plan.hidhide.cloakKnown}, {u"cloaked"_qs, plan.hidhide.cloaked},
            {u"mapperExecutable"_qs, plan.hidhide.mapperExecutable}, {u"mapperAllowlisted"_qs, plan.hidhide.mapperAllowlisted},
            {u"resolvedCollections"_qs, plan.hidhide.selectedControllerInstanceIds},
            {u"hiddenCollections"_qs, plan.hidhide.hiddenDeviceInstanceIds}, {u"rawCloakState"_qs, plan.hidhide.cloakReport},
            {u"rawAppList"_qs, plan.hidhide.appListReport}, {u"rawGamingDevices"_qs, plan.hidhide.gamingDevicesReport},
            {u"rawHiddenDevices"_qs, plan.hidhide.deviceListReport}, {u"rawInspection"_qs, plan.hidhide.diagnostic}});
    if (hidState == SetupTruthStatus::Repairable) addIssue(u"HidHideMismatch"_qs, u"Device isolation"_qs,
        hidState, u"HidHide needs a scoped repair"_qs, hidDetail, true, true,
        u"Allowlist HOTAS BF6, hide only the resolved physical collections, and read everything back"_qs);
    else if (hidState == SetupTruthStatus::Unknown) addIssue(u"HidHideInspectionFailed"_qs, u"Device isolation"_qs,
        hidState, u"HidHide inspection is incomplete"_qs, hidDetail, false, false, u"Review the failed inspection and run Check again"_qs);

    SetupTruthStatus mappingState = checking ? SetupTruthStatus::Checking : SetupTruthStatus::Ready;
    QString mappingDetail = checking ? u"Checking mapping and Device Rig routing."_qs : u"The selected Device Rig routing compiles."_qs;
    if (rig) {
        const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(m_configuration, rig->id);
        if (!checking && !compiled.valid) { mappingState = SetupTruthStatus::Attention; mappingDetail = compiled.issue; }
    }
    addGroup(u"mapping"_qs, u"Mapping"_qs, mappingState, mappingDetail,
        QVariantMap{{u"requested"_qs, m_worker.mappingRequested()}, {u"active"_qs, m_worker.runtime().mappingActive.load()},
                    {u"vjoyOwned"_qs, plan.vjoy.ownedByHotasBf6}, {u"outputReports"_qs, plan.vjoy.outputReportsSucceeding}});

    SetupTruthStatus overall = SetupTruthStatus::Ready;
    for (const QVariant &value : groups) {
        const QString label = value.toMap().value(u"status"_qs).toString();
        const SetupTruthStatus current = label == u"FAILED"_qs ? SetupTruthStatus::Failed
            : label == u"UNKNOWN / INSPECTION FAILED"_qs ? SetupTruthStatus::Unknown
            : label == u"UNAVAILABLE"_qs ? SetupTruthStatus::Unavailable
            : label == u"ACTION NEEDED"_qs ? SetupTruthStatus::Repairable
            : label == u"WAITING FOR USER"_qs ? SetupTruthStatus::WaitingForUser
            : label == u"ATTENTION"_qs ? SetupTruthStatus::Attention
            : label == u"CHECKING"_qs ? SetupTruthStatus::Checking : SetupTruthStatus::Ready;
        if (setupTruthStatusPriority(current) < setupTruthStatusPriority(overall)) overall = current;
    }
    QString diagnostics = QString(u"HOTAS BF6 SETUP DIAGNOSTICS\nGenerated: %1\nVersion: %2\nWindows: %3\nSnapshot: %4\nExecutable: %5\nRig: %6\nProfile: %7\nMapping requested: %8\nMapping active: %9\n\n"_qs)
        .arg(now.toString(Qt::ISODate), QString::fromLatin1(HOTAS_BF6_VERSION),
             QSysInfo::prettyProductName(),
             m_setupConvergenceSessionId.isEmpty() ? u"ad-hoc-check"_qs : m_setupConvergenceSessionId,
             QCoreApplication::applicationFilePath(), rig ? rig->name : u"None"_qs,
             activeProfileName(), m_worker.mappingRequested() ? u"yes"_qs : u"no"_qs,
             m_worker.runtime().mappingActive.load() ? u"yes"_qs : u"no"_qs);
    for (const QVariant &value : groups) {
        const QVariantMap group = value.toMap();
        diagnostics += QString(u"%1\n%2\n%3\n\n"_qs).arg(group.value(u"title"_qs).toString(),
            group.value(u"status"_qs).toString(), group.value(u"detail"_qs).toString());
    }
    for (const QVariant &value : issues) {
        const QVariantMap issue = value.toMap();
        diagnostics += QString(u"ISSUE %1\n%2\n%3\n\n"_qs).arg(issue.value(u"code"_qs).toString(),
            issue.value(u"title"_qs).toString(), issue.value(u"explanation"_qs).toString());
    }
    diagnostics += u"RAW VJOY / HIDHIDE EVIDENCE\n"_qs;
    for (const QVariant &value : outputEvidence) {
        const QVariantMap output = value.toMap();
        diagnostics += QString(u"\nVJOY %1 (Device %2)\nDescriptor:\n%3\nConfiguration snapshot:\n%4\nDevice list:\n%5\n"_qs)
            .arg(output.value(u"layoutName"_qs).toString(), output.value(u"deviceId"_qs).toString(),
                 output.value(u"rawDescriptor"_qs).toString(), output.value(u"rawConfiguration"_qs).toString(),
                 output.value(u"rawDeviceList"_qs).toString());
    }
    diagnostics += QString(u"\nHIDHIDE\nCloak state:\n%1\nAllowlist:\n%2\nGaming devices:\n%3\nHidden devices:\n%4\nInspection failures:\n%5\n"_qs)
        .arg(plan.hidhide.cloakReport, plan.hidhide.appListReport, plan.hidhide.gamingDevicesReport,
             plan.hidhide.deviceListReport, plan.hidhide.inspectionFailures.join(u"\n"_qs));
    return QVariantMap{{u"snapshotId"_qs, m_setupConvergenceSessionId.isEmpty() ? u"ad-hoc-check"_qs : m_setupConvergenceSessionId},
        {u"timestamp"_qs, now.toString(Qt::ISODate)}, {u"version"_qs, QString::fromLatin1(HOTAS_BF6_VERSION)},
        {u"executable"_qs, QCoreApplication::applicationFilePath()},
        {u"rigId"_qs, rig ? rig->id : QString{}}, {u"rigName"_qs, rig ? rig->name : QString{}},
        {u"profileId"_qs, m_configuration.activeProfileId}, {u"profileName"_qs, activeProfileName()},
        {u"overallStatus"_qs, statusValue(overall)}, {u"groups"_qs, groups}, {u"issues"_qs, issues},
        {u"repairPlan"_qs, repairPlan}, {u"diagnostics"_qs, diagnostics}, {u"fresh"_qs, inspected}};
}

QVariantMap AppBackend::setupTruthSnapshot() const
{
    return m_setupTruthSnapshot.isEmpty() ? buildSetupTruthSnapshot() : m_setupTruthSnapshot;
}

QVariantMap AppBackend::setupRepairSession() const
{
    const auto mode = [this] {
        switch (m_setupConvergenceStage) {
        case SetupConvergenceStage::Idle: return u"IDLE"_qs;
        case SetupConvergenceStage::Checking: return u"CHECKING"_qs;
        case SetupConvergenceStage::Results: return u"RESULTS"_qs;
        case SetupConvergenceStage::Repairing:
        case SetupConvergenceStage::VerifyingIdentity:
        case SetupConvergenceStage::RepairingVJoy: return u"REPAIRING"_qs;
        case SetupConvergenceStage::WaitingForUser: return u"WAITING FOR USER"_qs;
        case SetupConvergenceStage::FinalChecking: return u"CHECKING FINAL STATE"_qs;
        case SetupConvergenceStage::Complete: return u"COMPLETE"_qs;
        case SetupConvergenceStage::Failed: return u"FAILED"_qs;
        case SetupConvergenceStage::Cancelled: return u"CANCELLED"_qs;
        }
        return u"IDLE"_qs;
    }();
    const bool active = m_setupConvergenceStage == SetupConvergenceStage::Checking
        || m_setupConvergenceStage == SetupConvergenceStage::Repairing
        || m_setupConvergenceStage == SetupConvergenceStage::VerifyingIdentity
        || m_setupConvergenceStage == SetupConvergenceStage::RepairingVJoy
        || m_setupConvergenceStage == SetupConvergenceStage::WaitingForUser
        || m_setupConvergenceStage == SetupConvergenceStage::FinalChecking;
    const auto isTerminalStep = [](const QString &status) {
        return status == u"SUCCEEDED"_qs || status == u"FAILED"_qs || status == u"CANCELLED"_qs;
    };
    const int totalStepCount = m_setupRepairProgress.size();
    int completedStepCount = 0;
    for (const QVariant &entry : m_setupRepairProgress) {
        if (isTerminalStep(entry.toMap().value(u"status"_qs).toString())) ++completedStepCount;
    }
    const QVariantMap current = currentSetupRepairStep();
    int currentStepNumber = current.value(u"order"_qs).toInt();
    if (currentStepNumber <= 0 && totalStepCount > 0) {
        for (const QVariant &entry : m_setupRepairProgress) {
            const QVariantMap step = entry.toMap();
            if (isTerminalStep(step.value(u"status"_qs).toString())) {
                currentStepNumber = std::max(currentStepNumber, step.value(u"order"_qs).toInt());
            }
        }
        currentStepNumber = std::min(totalStepCount, std::max(1, currentStepNumber));
    }
    const int progressPercent = totalStepCount == 0 ? 0
        : (100 * completedStepCount) / totalStepCount;
    const QString progressLabel = totalStepCount == 0 ? u"No repair stages have started"_qs
        : QString(u"Stage %1 of %2 · %3%"_qs).arg(currentStepNumber).arg(totalStepCount).arg(progressPercent);
    QVariantMap result{{u"sessionId"_qs, m_setupConvergenceSessionId}, {u"mode"_qs, mode},
        {u"active"_qs, active}, {u"startedAt"_qs, m_setupConvergenceStarted.toString(Qt::ISODate)},
        {u"steps"_qs, m_setupRepairProgress}, {u"stepCount"_qs, totalStepCount},
        {u"totalStepCount"_qs, totalStepCount}, {u"completedStepCount"_qs, completedStepCount},
        {u"currentStepNumber"_qs, currentStepNumber}, {u"progressPercent"_qs, progressPercent},
        {u"progressLabel"_qs, progressLabel}};
    if (!current.isEmpty()) result.insert(u"currentStep"_qs, current);
    if (m_setupConvergenceStage == SetupConvergenceStage::Complete
        || m_setupConvergenceStage == SetupConvergenceStage::Failed
        || m_setupConvergenceStage == SetupConvergenceStage::Cancelled) {
        result.insert(u"finishedAt"_qs, m_setupConvergenceFinished.toString(Qt::ISODate));
        result.insert(u"result"_qs, mode);
    }
    return result;
}

QVariantList AppBackend::setupRepairProgress() const { return m_setupRepairProgress; }
QString AppBackend::setupRepairSessionReport() const { return m_setupRepairSessionReport; }
bool AppBackend::setupRepairSessionActive() const { return setupRepairSession().value(u"active"_qs).toBool(); }

void AppBackend::setSetupConvergenceStage(SetupConvergenceStage stage)
{
    m_setupConvergenceStage = stage;
}

QVariantMap AppBackend::currentSetupRepairStep() const
{
    for (const QVariant &entry : m_setupRepairProgress) {
        const QVariantMap step = entry.toMap();
        if (step.value(u"current"_qs).toBool()) return step;
    }
    return {};
}

void AppBackend::appendSetupRepairProgress(const QString &id, const QString &subsystem, const QString &title,
                                           const QString &status, const QString &detail,
                                           bool requiresElevation, bool requiresReconnect)
{
    const bool current = status == u"RUNNING"_qs || status == u"WAITING FOR USER"_qs;
    const bool terminal = status == u"SUCCEEDED"_qs || status == u"FAILED"_qs || status == u"CANCELLED"_qs;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (current) {
        for (QVariant &entry : m_setupRepairProgress) {
            QVariantMap step = entry.toMap();
            step.insert(u"current"_qs, false);
            entry = step;
        }
    }
    m_setupRepairProgress.append(QVariantMap{{u"id"_qs, id}, {u"subsystem"_qs, subsystem}, {u"title"_qs, title},
        {u"state"_qs, status}, {u"status"_qs, status}, {u"current"_qs, current},
        {u"detail"_qs, detail}, {u"requiresElevation"_qs, requiresElevation},
        {u"requiresReconnect"_qs, requiresReconnect}, {u"order"_qs, m_setupRepairProgress.size() + 1},
        {u"total"_qs, 0}, {u"startedAt"_qs, now},
        {u"finishedAt"_qs, terminal ? now : QString{}}, {u"result"_qs, terminal ? status : QString{}},
        {u"evidence"_qs, QVariantMap{}}, {u"timestamp"_qs, now}});
    const int total = m_setupRepairProgress.size();
    for (QVariant &entry : m_setupRepairProgress) {
        QVariantMap step = entry.toMap();
        step.insert(u"total"_qs, total);
        entry = step;
    }
}

void AppBackend::updateSetupRepairProgress(const QString &id, const QString &state, const QString &detail,
                                           const QString &result, const QVariantMap &evidence, bool current)
{
    const bool active = current && (state == u"RUNNING"_qs || state == u"WAITING FOR USER"_qs);
    const bool terminal = state == u"SUCCEEDED"_qs || state == u"FAILED"_qs || state == u"CANCELLED"_qs;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (active) {
        for (QVariant &entry : m_setupRepairProgress) {
            QVariantMap step = entry.toMap();
            step.insert(u"current"_qs, false);
            entry = step;
        }
    }
    for (QVariant &entry : m_setupRepairProgress) {
        QVariantMap step = entry.toMap();
        if (step.value(u"id"_qs).toString() != id) continue;
        step.insert(u"state"_qs, state);
        step.insert(u"status"_qs, state);
        step.insert(u"current"_qs, active);
        if (!detail.isEmpty()) step.insert(u"detail"_qs, detail);
        if (!result.isEmpty()) step.insert(u"result"_qs, result);
        if (!evidence.isEmpty()) step.insert(u"evidence"_qs, evidence);
        if (terminal) step.insert(u"finishedAt"_qs, now);
        else if (!active) step.insert(u"finishedAt"_qs, QString{});
        step.insert(u"timestamp"_qs, now);
        entry = step;
        break;
    }
}

void AppBackend::captureSetupTruthSnapshot(bool finalSnapshot)
{
    m_setupTruthSnapshot = buildSetupTruthSnapshot();
    if (finalSnapshot) m_setupTruthAfterSnapshot = m_setupTruthSnapshot;
}

QVariantMap AppBackend::checkSetupHealth()
{
    if (m_verificationInProgress) return actionResult(false, u"Setup check is already running"_qs,
        u"HOTAS BF6 is still inspecting the selected setup."_qs, u"application"_qs, {}, u"wait"_qs, {}, {}, true);
    setSetupConvergenceStage(SetupConvergenceStage::Checking);
    m_setupConvergenceSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_setupConvergenceStarted = QDateTime::currentDateTime();
    m_setupConvergenceFinished = {};
    m_setupRepairProgress.clear();
    m_setupConvergenceAttemptedIssues.clear();
    m_setupConvergenceIdentityVerificationFailed = false;
    m_setupConvergenceIdentityVerificationFailure.clear();
    m_setupConvergenceVJoyRepairFailed = false;
    m_setupConvergenceHidHideRepairFailed = false;
    m_setupConvergenceCancelled = false;
    m_setupConvergenceCurrentIssueId.clear();
    m_setupTruthBeforeSnapshot.clear();
    m_setupTruthAfterSnapshot.clear();
    m_setupRepairSessionReport.clear();
    m_virtualOutputReadinessPlans.clear();
    appendSetupRepairProgress(u"inspect"_qs, u"Setup"_qs, u"Inspecting complete setup"_qs, u"RUNNING"_qs,
        u"No configuration changes are made during this check."_qs);
    verifyHotasSetup();
    // Publish CHECKING only after the verifier has actually entered its
    // read-only worker phase. A previous READY plan must never remain visible
    // as current during a new check.
    m_setupTruthSnapshot = buildSetupTruthSnapshot();
    emit stateChanged();
    return actionResult(true, u"Checking complete setup"_qs,
        u"HOTAS BF6 is reading physical input, saved verification, vJoy, HidHide, and mapping without changing configuration."_qs,
        u"application"_qs, {}, u"wait"_qs, {}, {}, true);
}

QVariantMap AppBackend::repairSetupHealth()
{
    if (m_verificationInProgress) return actionResult(false, u"Setup is still checking"_qs,
        u"Wait for the read-only check to finish before approving repair."_qs, u"application"_qs, {}, u"wait"_qs, {}, {}, true);
    if (m_setupTruthSnapshot.isEmpty()) return checkSetupHealth();
    if (setupRepairSessionActive() && m_setupConvergenceStage != SetupConvergenceStage::Repairing) return actionResult(false, u"Setup repair is already running"_qs,
        u"HOTAS BF6 is completing the approved repair session."_qs, u"application"_qs, {}, u"wait"_qs, {}, {}, true);
    if (m_setupConvergenceSessionId.isEmpty()) {
        m_setupConvergenceSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_setupConvergenceStarted = QDateTime::currentDateTime();
        m_setupConvergenceFinished = {};
        m_setupConvergenceAttemptedIssues.clear();
        m_setupConvergenceIdentityVerificationFailed = false;
        m_setupConvergenceIdentityVerificationFailure.clear();
        m_setupConvergenceVJoyRepairFailed = false;
        m_setupConvergenceHidHideRepairFailed = false;
        m_setupConvergenceCancelled = false;
    }
    if (m_setupTruthBeforeSnapshot.isEmpty()) m_setupTruthBeforeSnapshot = m_setupTruthSnapshot;
    const QVariantList issues = m_setupTruthSnapshot.value(u"issues"_qs).toList();
    const QVariantList approvedPlan = m_setupTruthSnapshot.value(u"repairPlan"_qs).toList();
    const auto approved = [&approvedPlan](const QString &issueId) {
        return std::any_of(approvedPlan.cbegin(), approvedPlan.cend(), [&issueId](const QVariant &value) {
            return value.toMap().value(u"id"_qs).toString() == issueId;
        });
    };
    const auto stepIdFor = [](const QString &issueId) { return u"repair:"_qs + issueId; };
    for (const QVariant &value : issues) {
        const QVariantMap issue = value.toMap();
        const QString issueId = issue.value(u"id"_qs).toString();
        if (!issue.value(u"repairable"_qs).toBool() || !approved(issueId)) continue;
        const QString stepId = stepIdFor(issueId);
        const bool alreadyListed = std::any_of(m_setupRepairProgress.cbegin(), m_setupRepairProgress.cend(),
            [&stepId](const QVariant &entry) { return entry.toMap().value(u"id"_qs).toString() == stepId; });
        if (!alreadyListed) appendSetupRepairProgress(stepId, issue.value(u"subsystem"_qs).toString(),
            issue.value(u"proposedRepair"_qs).toString(), u"PENDING"_qs,
            issue.value(u"explanation"_qs).toString(), issue.value(u"requiresElevation"_qs).toBool(),
            issue.value(u"requiresReconnect"_qs).toBool());
    }
    // Freeze the full stage count before the first repair starts. The progress
    // percentage is stage-based (not a time estimate), so it must never jump
    // backwards when the mandatory final read-back is reached.
    const bool finalAlreadyListed = std::any_of(m_setupRepairProgress.cbegin(), m_setupRepairProgress.cend(),
        [](const QVariant &entry) { return entry.toMap().value(u"id"_qs).toString() == u"final-inspect"_qs; });
    if (!finalAlreadyListed) appendSetupRepairProgress(u"final-inspect"_qs, u"Setup"_qs,
        u"Performing final full inspection"_qs, u"PENDING"_qs,
        u"Fresh driver and controller read-back will determine the final setup truth."_qs);
    for (const QVariant &value : issues) {
        const QVariantMap issue = value.toMap();
        const QString code = issue.value(u"code"_qs).toString();
        if ((code != u"PhysicalDeviceUnverified"_qs && code != u"RecoveryVerificationPending"_qs)
            || !issue.value(u"repairable"_qs).toBool() || !approved(issue.value(u"id"_qs).toString())) continue;
        const QString recordId = issue.value(u"evidence"_qs).toMap().value(u"recordId"_qs).toString();
        const QString attemptKey = u"ControllerIdentityProof:"_qs + recordId;
        if (m_setupConvergenceAttemptedIssues.contains(attemptKey)) continue;
        m_setupConvergenceAttemptedIssues.insert(attemptKey);
        m_setupConvergenceCurrentIssueId = issue.value(u"id"_qs).toString();
        setSetupConvergenceStage(SetupConvergenceStage::VerifyingIdentity);
        updateSetupRepairProgress(stepIdFor(m_setupConvergenceCurrentIssueId), u"RUNNING"_qs,
            u"Acquiring the matching DirectInput controller and committing verification only after exact identity proof."_qs,
            {}, QVariantMap{{u"recordId"_qs, recordId}}, true);
        const QVariantMap verification = completeSetupAssistantDevice(recordId);
        // A disconnected or unavailable saved target is an immediate, honest
        // outcome from the legacy exact-acquisition entry point. Do not leave
        // the central convergence session showing an endless verification
        // spinner when no asynchronous worker was started.
        if (!verification.value(u"ok"_qs).toBool()) {
            m_setupConvergenceIdentityVerificationFailed = true;
            QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
        }
        return verification;
    }
    for (const QVariant &value : issues) {
        const QVariantMap issue = value.toMap();
        if (issue.value(u"code"_qs).toString() != u"VJoyDescriptorMismatch"_qs
            || !issue.value(u"repairable"_qs).toBool() || !approved(issue.value(u"id"_qs).toString())) continue;
        const QString layoutId = issue.value(u"evidence"_qs).toMap().value(u"layoutId"_qs).toString();
        const VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId);
        const QString attemptKey = u"VJoyDescriptorMismatch:"_qs + layoutId;
        if (!layout || m_setupConvergenceAttemptedIssues.contains(attemptKey)) continue;
        m_setupConvergenceAttemptedIssues.insert(attemptKey);
        MapperConfiguration outputConfiguration = m_configuration;
        outputConfiguration.vjoyDeviceId = layout->requirements.deviceId;
        m_setupConvergenceCurrentIssueId = issue.value(u"id"_qs).toString();
        setSetupConvergenceStage(SetupConvergenceStage::WaitingForUser);
        updateSetupRepairProgress(stepIdFor(m_setupConvergenceCurrentIssueId), u"WAITING FOR USER"_qs,
            QString(u"Waiting for administrator approval to repair vJoy Device %1 for %2 only; Device 1 and HidHide are not part of this operation."_qs)
                .arg(layout->requirements.deviceId).arg(layout->name), {},
            QVariantMap{{u"layoutId"_qs, layoutId}, {u"deviceId"_qs, layout->requirements.deviceId}}, true);
        if (applyScopedVJoyRepair(layoutId, outputConfiguration,
                                  ControllerReadinessService::requirementsFor(layout->requirements))) {
            return actionResult(true, u"Repairing virtual output"_qs,
                u"HOTAS BF6 is applying the approved vJoy repair and will re-inspect the complete Device Rig."_qs,
                u"virtualOutput"_qs, layoutId, u"wait"_qs, {}, {}, true);
        }
        m_setupConvergenceVJoyRepairFailed = true;
        updateSetupRepairProgress(stepIdFor(m_setupConvergenceCurrentIssueId), u"FAILED"_qs,
            u"The scoped vJoy repair could not start; no HidHide change was attempted."_qs);
        setSetupConvergenceStage(SetupConvergenceStage::Repairing);
        QTimer::singleShot(0, this, &AppBackend::repairSetupHealth);
        return actionResult(false, u"Virtual output repair could not start"_qs,
            u"No driver change was applied. HOTAS BF6 will still complete a final inspection."_qs,
            u"virtualOutput"_qs, layoutId, u"wait"_qs, {}, {}, true);
    }
    for (const QVariant &value : issues) {
        const QVariantMap issue = value.toMap();
        if (issue.value(u"code"_qs).toString() != u"HidHideMismatch"_qs
            || !issue.value(u"repairable"_qs).toBool() || !approved(issue.value(u"id"_qs).toString())) continue;
        const QString attemptKey = u"HidHideMismatch:"_qs + issue.value(u"id"_qs).toString();
        if (m_setupConvergenceAttemptedIssues.contains(attemptKey)) continue;
        m_setupConvergenceAttemptedIssues.insert(attemptKey);
        m_setupConvergenceCurrentIssueId = issue.value(u"id"_qs).toString();
        setSetupConvergenceStage(SetupConvergenceStage::WaitingForUser);
        updateSetupRepairProgress(stepIdFor(m_setupConvergenceCurrentIssueId), u"WAITING FOR USER"_qs,
            u"Waiting for administrator approval to allow HOTAS BF6 and hide only the freshly resolved physical-controller interfaces."_qs,
            {}, issue.value(u"evidence"_qs).toMap(), true);
        if (applyScopedHidHideRepair(m_configuration, currentPhysicalCapabilities())) {
            return actionResult(true, u"Repairing device isolation"_qs,
                u"HOTAS BF6 is applying the approved exact physical-controller HidHide repair and will re-inspect the complete Device Rig."_qs,
                u"deviceIsolation"_qs, {}, u"wait"_qs, {}, {}, true);
        }
        m_setupConvergenceHidHideRepairFailed = true;
        updateSetupRepairProgress(stepIdFor(m_setupConvergenceCurrentIssueId), u"FAILED"_qs,
            u"The approved HidHide repair could not start; final inspection will preserve the actual device-isolation truth."_qs);
        setSetupConvergenceStage(SetupConvergenceStage::Repairing);
        QTimer::singleShot(0, this, &AppBackend::repairSetupHealth);
        return actionResult(false, u"Device isolation repair could not start"_qs,
            u"No unplanned driver change was applied. HOTAS BF6 will still complete a final inspection."_qs,
            u"deviceIsolation"_qs, {}, u"wait"_qs, {}, {}, true);
    }
    setSetupConvergenceStage(SetupConvergenceStage::FinalChecking);
    updateSetupRepairProgress(u"final-inspect"_qs, u"RUNNING"_qs,
        u"Performing final full inspection from fresh driver and controller read-back evidence."_qs, {}, {}, true);
    verifyHotasSetup();
    return actionResult(true, u"Verifying final setup state"_qs,
        u"No additional automatic mutation is needed; HOTAS BF6 is recomputing setup health from scratch."_qs,
        u"application"_qs, {}, u"wait"_qs, {}, {}, true);
}

bool AppBackend::copySetupHealthDiagnostics()
{
    const QString report = m_setupRepairSessionReport.isEmpty()
        ? setupTruthSnapshot().value(u"diagnostics"_qs).toString() : m_setupRepairSessionReport;
    if (report.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(report);
    appendEvent(u"Setup Truth diagnostics copied to the clipboard"_qs);
    return true;
}

void AppBackend::completeSetupConvergence(const QString &finalState)
{
    if (m_setupTruthAfterSnapshot.isEmpty()) captureSetupTruthSnapshot(true);
    const QString state = finalState.isEmpty() ? m_setupTruthAfterSnapshot.value(u"overallStatus"_qs).toString() : finalState;
    const QString finalStepState = state == u"READY"_qs ? u"SUCCEEDED"_qs
        : state == u"CANCELLED"_qs ? u"CANCELLED"_qs : u"FAILED"_qs;
    updateSetupRepairProgress(u"final-inspect"_qs, finalStepState,
        state == u"READY"_qs ? u"Fresh read-back verified the final setup state."_qs
        : state == u"CANCELLED"_qs ? u"The approved repair was cancelled; final read-back preserved the unresolved setup truth."_qs
        : u"Fresh read-back found remaining setup work; review the exact issue below."_qs,
        state, QVariantMap{{u"overallStatus"_qs, m_setupTruthAfterSnapshot.value(u"overallStatus"_qs)}});
    QString operations;
    for (const QVariant &value : m_setupRepairProgress) {
        const QVariantMap operation = value.toMap();
        operations += QString(u"[%1] %2 — %3\n%4\n\n"_qs)
            .arg(operation.value(u"timestamp"_qs).toString(), operation.value(u"status"_qs).toString(),
                 operation.value(u"title"_qs).toString(), operation.value(u"detail"_qs).toString());
    }
    m_setupRepairSessionReport = QString(u"HOTAS BF6 SETUP REPAIR SESSION\nSession ID: %1\nStarted: %2\nFinished: %3\n\nBEFORE\n%4\n\nOPERATIONS\n%5\n\nAFTER\n%6\n\nFINAL\n%7"_qs)
        .arg(m_setupConvergenceSessionId, m_setupConvergenceStarted.toString(Qt::ISODate),
             QDateTime::currentDateTime().toString(Qt::ISODate),
             m_setupTruthBeforeSnapshot.value(u"diagnostics"_qs).toString(),
             operations, m_setupTruthAfterSnapshot.value(u"diagnostics"_qs).toString(), state);
    m_setupConvergenceFinished = QDateTime::currentDateTime();
    setSetupConvergenceStage(state == u"READY"_qs ? SetupConvergenceStage::Complete
        : state == u"CANCELLED"_qs ? SetupConvergenceStage::Cancelled : SetupConvergenceStage::Failed);
    emit stateChanged();
}

void AppBackend::continueSetupConvergence()
{
    if (m_verificationInProgress) return;
    if (m_setupConvergenceStage == SetupConvergenceStage::Checking) {
        refreshSelectedRigOutputReadiness();
        captureSetupTruthSnapshot();
        updateSetupRepairProgress(u"inspect"_qs, u"SUCCEEDED"_qs,
            u"Read-only inspection completed. These frozen results are the only authority for a later repair."_qs,
            u"RESULTS FROZEN"_qs, QVariantMap{{u"snapshotId"_qs, m_setupTruthSnapshot.value(u"snapshotId"_qs)}});
        setSetupConvergenceStage(SetupConvergenceStage::Results);
        emit stateChanged();
        return;
    }
    if (m_setupConvergenceStage == SetupConvergenceStage::VerifyingIdentity) {
        QVariantMap issue;
        for (const QVariant &entry : m_setupTruthSnapshot.value(u"issues"_qs).toList()) {
            if (entry.toMap().value(u"id"_qs).toString() == m_setupConvergenceCurrentIssueId) {
                issue = entry.toMap();
                break;
            }
        }
        const QString recordId = issue.value(u"evidence"_qs).toMap().value(u"recordId"_qs).toString();
        const SavedControllerRecord *record = savedControllerRecord(recordId);
        const bool verified = record && !record->lastVerified.isEmpty();
        const QString stepId = u"repair:"_qs + m_setupConvergenceCurrentIssueId;
        if (m_setupConvergenceIdentityVerificationFailed || !verified) {
            m_setupConvergenceIdentityVerificationFailed = true;
            const QString detail = m_setupConvergenceIdentityVerificationFailure.isEmpty()
                ? u"The exact saved controller did not complete identity verification. The remaining approved repairs will still be processed."_qs
                : m_setupConvergenceIdentityVerificationFailure;
            updateSetupRepairProgress(stepId, u"FAILED"_qs,
                detail,
                u"IDENTITY NOT COMMITTED"_qs, QVariantMap{{u"recordId"_qs, recordId}});
        } else {
            updateSetupRepairProgress(stepId, u"SUCCEEDED"_qs,
                u"The exact saved controller identity was committed and its verification timestamp was persisted."_qs,
                u"IDENTITY VERIFIED"_qs,
                QVariantMap{{u"recordId"_qs, recordId}, {u"lastVerified"_qs, record->lastVerified}});
        }
        // Do not let a failure in one approved repair silently cancel the next
        // one. The frozen repair plan remains the transaction authority.
        setSetupConvergenceStage(SetupConvergenceStage::Repairing);
        repairSetupHealth();
        return;
    }
    if (m_setupConvergenceStage == SetupConvergenceStage::RepairingVJoy) return;
    if (m_setupConvergenceStage == SetupConvergenceStage::Repairing) {
        if (m_readiness.reconnectVerificationPending()) {
            setSetupConvergenceStage(SetupConvergenceStage::WaitingForUser);
            appendSetupRepairProgress(u"reconnect"_qs, u"Device isolation"_qs, u"Reconnect controller"_qs,
                u"WAITING FOR USER"_qs, u"Unplug and reconnect the exact selected controller, then move a control."_qs,
                false, true);
            emit stateChanged();
            return;
        }
        repairSetupHealth();
        return;
    }
    if (m_setupConvergenceStage == SetupConvergenceStage::FinalChecking) {
        refreshSelectedRigOutputReadiness();
        captureSetupTruthSnapshot(true);
        const QString finalState = m_setupConvergenceCancelled ? u"CANCELLED"_qs
            : (m_setupConvergenceIdentityVerificationFailed || m_setupConvergenceVJoyRepairFailed
                || m_setupConvergenceHidHideRepairFailed
                || m_setupTruthAfterSnapshot.value(u"overallStatus"_qs).toString() != u"READY"_qs)
                ? u"FAILED"_qs : u"READY"_qs;
        completeSetupConvergence(finalState);
    }
}

bool AppBackend::applyScopedVJoyRepair(const QString &layoutId, const MapperConfiguration &configuration,
                                       const MapperOutputRequirements &requirements)
{
    if (m_verificationInProgress || layoutId.isEmpty()) return false;
    const bool mappingWasRequested = m_worker.mappingRequested();
    const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
    const QString issueId = m_setupConvergenceCurrentIssueId;
    m_verificationInProgress = true;
    emit stateChanged();

    auto repair = std::make_shared<ControllerReadinessService>();
    QThread *thread = QThread::create([this, repair, layoutId, configuration, requirements, physical,
                                       mappingWasRequested, issueId] {
        bool prepared = m_worker.prepareForDriverConfiguration();
        bool completed = false;
        bool cancelled = false;
        bool restored = false;
        if (prepared) {
            repair->inspectForRequirements(configuration, physical, requirements);
            const ControllerReadinessPlan before = repair->plan();
            if (before.vjoyNeedsChanges && before.vjoyCanApply) {
                completed = repair->applyVJoyConfiguration();
                cancelled = repair->plan().state == ControllerReadinessState::Cancelled;
            }
            restored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
        }
        QMetaObject::invokeMethod(this, [this, repair, layoutId, issueId, prepared, completed, cancelled, restored] {
            m_virtualOutputReadinessPlans.insert(layoutId, repair->plan());
            m_verificationInProgress = false;
            const QString stepId = u"repair:"_qs + issueId;
            if (cancelled) {
                m_setupConvergenceCancelled = true;
                updateSetupRepairProgress(stepId, u"CANCELLED"_qs,
                    u"The administrator repair request was cancelled. HidHide and other vJoy devices were left untouched."_qs,
                    u"CANCELLED"_qs, QVariantMap{{u"layoutId"_qs, layoutId},
                        {u"deviceId"_qs, repair->plan().vjoy.deviceId}});
            } else if (completed && restored && !repair->plan().vjoyNeedsChanges) {
                updateSetupRepairProgress(stepId, u"SUCCEEDED"_qs,
                    QString(u"vJoy Device %1 now matches this output layout; fresh descriptor read-back succeeded."_qs)
                        .arg(repair->plan().vjoy.deviceId), u"READ-BACK VERIFIED"_qs,
                    QVariantMap{{u"layoutId"_qs, layoutId}, {u"deviceId"_qs, repair->plan().vjoy.deviceId},
                        {u"descriptor"_qs, repair->plan().vjoy.descriptorReport}});
            } else {
                m_setupConvergenceVJoyRepairFailed = true;
                const QString detail = !prepared
                    ? u"HOTAS BF6 could not safely release its current mapping output for this Device 2-only repair."_qs
                    : !restored ? u"vJoy repair did not restore the prior mapping state; final inspection will show the actual result."_qs
                    : repair->plan().status.isEmpty() ? u"vJoy Device 2 did not accept the scoped descriptor repair."_qs
                    : repair->plan().status;
                updateSetupRepairProgress(stepId, u"FAILED"_qs, detail, u"READ-BACK FAILED"_qs,
                    QVariantMap{{u"layoutId"_qs, layoutId}, {u"deviceId"_qs, repair->plan().vjoy.deviceId},
                        {u"descriptor"_qs, repair->plan().vjoy.descriptorReport}});
            }
            if (m_setupConvergenceCancelled) {
                setSetupConvergenceStage(SetupConvergenceStage::FinalChecking);
                updateSetupRepairProgress(u"final-inspect"_qs, u"RUNNING"_qs,
                    u"Reading final setup truth after the cancelled repair request."_qs, {}, {}, true);
                verifyHotasSetup();
            } else {
                setSetupConvergenceStage(SetupConvergenceStage::Repairing);
                QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
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

bool AppBackend::applyScopedHidHideRepair(const MapperConfiguration &configuration,
                                          const PhysicalControllerCapabilities &physical)
{
    if (m_verificationInProgress) return false;
    const bool mappingWasRequested = m_worker.mappingRequested();
    const QString issueId = m_setupConvergenceCurrentIssueId;
    m_verificationInProgress = true;
    emit stateChanged();

    auto repair = std::make_shared<ControllerReadinessService>();
    QThread *thread = QThread::create([this, repair, configuration, physical, mappingWasRequested, issueId] {
        bool prepared = m_worker.prepareForDriverConfiguration();
        bool completed = false;
        bool cancelled = false;
        bool physicalReacquired = false;
        bool recoveryAttempted = false;
        bool recoverySucceeded = false;
        bool restored = false;
        if (prepared) {
            repair->inspect(configuration, physical, VerificationMode::Full);
            const ControllerReadinessPlan before = repair->plan();
            if (before.hidhideNeedsChanges && before.hidhideCanApply) {
                completed = repair->applyHidHideConfiguration();
                cancelled = repair->plan().state == ControllerReadinessState::Cancelled;
                if (completed) {
                    physicalReacquired = m_worker.reacquirePhysicalController(physical.hidInstanceId);
                    if (!physicalReacquired) {
                        recoveryAttempted = true;
                        recoverySucceeded = repair->recoverFromPhysicalAccessFailure();
                        if (recoverySucceeded) physicalReacquired = m_worker.reacquirePhysicalController(physical.hidInstanceId);
                    }
                }
            }
            restored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
        }
        QMetaObject::invokeMethod(this, [this, repair, issueId, prepared, completed, cancelled, physicalReacquired,
                                         recoveryAttempted, recoverySucceeded, restored] {
            m_verificationInProgress = false;
            const QString stepId = u"repair:"_qs + issueId;
            if (cancelled) {
                m_setupConvergenceCancelled = true;
                updateSetupRepairProgress(stepId, u"CANCELLED"_qs,
                    u"The administrator HidHide request was cancelled. Existing allowlist, cloak, and device rules were left unchanged."_qs,
                    u"CANCELLED"_qs);
            } else if (completed && restored && physicalReacquired && !repair->plan().hidhideNeedsChanges) {
                updateSetupRepairProgress(stepId, u"SUCCEEDED"_qs,
                    u"The exact current physical-controller interfaces are hidden, the candidate remains allowlisted, and fresh HidHide read-back succeeded."_qs,
                    u"READ-BACK VERIFIED"_qs,
                    QVariantMap{{u"hiddenCollections"_qs, repair->plan().hidhide.hiddenDeviceInstanceIds},
                                {u"resolvedCollections"_qs, repair->plan().hidhide.selectedControllerInstanceIds}});
            } else {
                m_setupConvergenceHidHideRepairFailed = true;
                const QString detail = !prepared
                    ? u"HOTAS BF6 could not safely release its mapping output for the exact physical-controller HidHide repair."_qs
                    : !restored ? u"HidHide repair did not restore the prior mapping state; final inspection will show the actual result."_qs
                    : recoveryAttempted
                        ? (recoverySucceeded
                            ? u"HidHide changes were rolled back after physical-controller reacquisition failed."_qs
                            : u"HidHide repair could not reacquire the physical controller and its rollback also needs attention."_qs)
                        : !physicalReacquired ? u"HidHide repair completed but HOTAS BF6 could not reacquire the exact physical controller."_qs
                        : repair->plan().hidhideSummary;
                updateSetupRepairProgress(stepId, u"FAILED"_qs, detail, u"READ-BACK FAILED"_qs,
                    QVariantMap{{u"hiddenCollections"_qs, repair->plan().hidhide.hiddenDeviceInstanceIds},
                                {u"resolvedCollections"_qs, repair->plan().hidhide.selectedControllerInstanceIds}});
            }
            if (m_setupConvergenceCancelled) {
                setSetupConvergenceStage(SetupConvergenceStage::FinalChecking);
                updateSetupRepairProgress(u"final-inspect"_qs, u"RUNNING"_qs,
                    u"Reading final setup truth after the cancelled repair request."_qs, {}, {}, true);
                verifyHotasSetup();
            } else {
                setSetupConvergenceStage(SetupConvergenceStage::Repairing);
                QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
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

QVariantList AppBackend::appIssues() const
{
    // The Setup Assistant is the canonical source for device/output/visibility
    // findings. App Health deliberately omits its user-requested live-test
    // reminders: the header surfaces structural problems, not an invitation
    // to run an optional proof session.
    QVariantList issues;
    for (const QVariant &entry : setupAssistantIssuesForScope(u"application"_qs, {})) {
        const QVariantMap issue = entry.toMap();
        const QString code = issue.value(u"code"_qs).toString();
        if (code == u"LiveInputNotTested"_qs || code == u"LiveOutputNotTested"_qs) continue;
        issues.append(issue);
    }

    const auto append = [&issues](const QString &code, const QString &category,
                                  const QString &severity, const QString &objectType,
                                  const QString &objectId, const QString &title,
                                  const QString &explanation, const QString &action,
                                  const QString &actionLabel, int priority,
                                  const QString &technicalDetails = {}) {
        AppIssue issue;
        issue.id = u"app-"_qs + code + u"-"_qs + objectId;
        issue.code = code;
        issue.category = category;
        issue.severity = severity;
        issue.scopeType = objectType;
        issue.scopeId = objectId;
        issue.affectedObjectType = objectType;
        issue.affectedObjectId = objectId;
        issue.title = title;
        issue.explanation = explanation;
        issue.recommendedAction = action;
        issue.recommendedActionLabel = actionLabel;
        issue.alternativeActions = QVariantList{u"view-technical-details"_qs};
        issue.priority = priority;
        issue.technicalDetails = technicalDetails;
        issue.navigationTarget = issueNavigationTarget(code, objectType, objectId);
        issues.append(issue.toVariantMap());
    };

    if (m_mappingDesired && !m_worker.runtime().mappingActive.load(std::memory_order_relaxed)
        && static_cast<MappingEffectiveState>(m_worker.runtime().mappingEffectiveState.load(
               std::memory_order_relaxed)) == MappingEffectiveState::Suspended) {
        const QString rigId = activeDeviceRigId();
        append(u"MappingSuspended"_qs, u"Mapping"_qs, u"warning"_qs, u"deviceRig"_qs, rigId,
               u"Mapping is suspended"_qs,
               activeDeviceRig() ? activeDeviceRig()->name + u" needs attention before its routes can run."_qs
                                 : u"Choose a ready Device Rig before mapping can run."_qs,
               u"open-devices"_qs, u"OPEN DEVICES"_qs, 5, mappingStatus());
    }

    for (const ControllerProfile &profile : m_configuration.profiles) {
        if (!profile.enabled) continue;
        if (!profile.deviceRigId.isEmpty() && !findDeviceRig(m_configuration, profile.deviceRigId)) {
            append(u"ProfileRigMissing"_qs, u"Profile"_qs, u"warning"_qs, u"profile"_qs, profile.id,
                   u"Profile needs a Device Rig"_qs,
                   profile.name + u" references a Device Rig that is no longer available."_qs,
                   u"choose-rig"_qs, u"CHOOSE RIG"_qs, 110, profile.deviceRigId);
        }
        if (!profile.outputLayoutId.isEmpty() && !findOutputLayout(m_configuration, profile.outputLayoutId)) {
            append(u"ProfileOutputMissing"_qs, u"Profile"_qs, u"warning"_qs, u"profile"_qs, profile.id,
                   u"Profile needs a Virtual Output"_qs,
                   profile.name + u" references a virtual output that is no longer available."_qs,
                   u"choose-output"_qs, u"CHOOSE OUTPUT"_qs, 115, profile.outputLayoutId);
        }
    }

    for (const AutomationDefinition &automation : m_configuration.automations) {
        if (!automation.enabled || (!automation.conditions.empty() && !automation.actions.empty())) continue;
        append(u"AutomationInvalid"_qs, u"Automation"_qs, u"warning"_qs, u"automation"_qs,
               automation.id, u"Automation needs a complete rule"_qs,
               automation.name + (automation.conditions.empty()
                   ? u" has no trigger. Choose when it should run."_qs
                   : u" has no action. Choose what HOTAS BF6 should do."_qs),
               u"edit-automation"_qs,
               automation.conditions.empty() ? u"ADD CONDITION"_qs : u"ADD ACTION"_qs, 120);
    }
    for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
        if (route.enabled || route.id.isEmpty()
            || route.sourceKind != SignalFlowPortKind::Axis
            || route.destinationKind != SignalFlowPortKind::Axis) continue;
        append(u"RoutingConflict"_qs, u"Signal Flow"_qs, u"warning"_qs,
               u"signalFlowRoute"_qs, route.id,
               u"An analog route needs an explicit mixer decision"_qs,
               QString(u"A saved route into virtual axis %1 is disabled so it cannot silently merge with another analog source."_qs)
                   .arg(route.destinationIndex),
               u"review-routing"_qs, u"REVIEW ROUTING"_qs, 118,
               QString(u"ROUTE ID\n%1\n\nDESTINATION AXIS\n%2\n\nSTATUS\nDisabled until an explicit Mixer is created or the competing route is replaced."_qs)
                   .arg(route.id).arg(route.destinationIndex));
    }
    if (m_updateCheckFailed) {
        append(u"UpdateCheckFailed"_qs, u"Update"_qs, u"note"_qs, u"application"_qs, {},
               u"Could not check for updates"_qs,
               u"The update server could not be reached. Your installed version is unchanged."_qs,
               u"retry-update"_qs, u"TRY AGAIN"_qs, 900, m_updateStatusText);
    }
    return issues;
}

QVariantMap AppBackend::appHealthSummary() const
{
    return AppHealthService::summarize(appIssues());
}

QVariantMap AppBackend::setupAssistantLiveTest() const
{
    const AtomicRuntimeState &runtime = m_worker.runtime();
    QVariantList activity;
    bool allObserved = true;
    if (const DeviceRig *rig = activeDeviceRig()) {
        const DeviceRigStatus *status = nullptr;
        const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [rig](const DeviceRigStatus &candidate) { return candidate.rigId == rig->id; });
        if (found != m_deviceRigStatuses.cend()) status = &*found;
        int memberIndex = 0;
        for (const DeviceRigMember &member : rig->members) {
            if (!member.enabled) continue;
            const int runtimeIndex = memberIndex++;
            if (m_setupAssistantScopeType == u"device"_qs
                && member.controllerRecordId != m_setupAssistantScopeId) continue;
            const SavedControllerRecord *record = savedControllerRecord(member.controllerRecordId);
            const QString name = record ? record->displayName : u"Physical controller"_qs;
            const bool connected = status && status->connectedMemberIds.contains(member.controllerRecordId);
            const bool observed = runtimeIndex < kMaximumDeviceRigMembers
                && runtime.deviceRigMeaningfulInputSequence[static_cast<size_t>(runtimeIndex)].load(std::memory_order_relaxed)
                    > 0;
            const QString activityState = !connected ? u"offline"_qs : observed ? u"ready"_qs : u"listening"_qs;
            activity.append(QVariantMap{{u"id"_qs, member.controllerRecordId}, {u"kind"_qs, u"input"_qs},
                {u"title"_qs, name}, {u"optional"_qs, true}, {u"informational"_qs, true},
                {u"state"_qs, activityState},
                {u"message"_qs, !connected ? u"Calibration and live activity are available when connected."_qs
                                     : observed ? u"Input detected during this connection session."_qs
                                                : u"Listening for controller input…"_qs}});
            if (connected) allObserved = allObserved && observed;
        }
        int outputIndex = 0;
        for (const DeviceRigOutputTarget &target : rig->outputs) {
            if (!target.enabled) continue;
            const int runtimeIndex = outputIndex++;
            if (m_setupAssistantScopeType == u"virtualOutput"_qs
                && target.outputLayoutId != m_setupAssistantScopeId) continue;
            const VirtualOutputLayout *layout = findOutputLayout(m_configuration, target.outputLayoutId);
            const bool observed = runtimeIndex < kMaximumDeviceRigOutputs
                && runtime.deviceRigMeaningfulOutputSequence[static_cast<size_t>(runtimeIndex)].load(std::memory_order_relaxed)
                    > 0;
            activity.append(QVariantMap{{u"id"_qs, target.outputLayoutId}, {u"kind"_qs, u"output"_qs},
                {u"title"_qs, layout ? layout->name : u"Virtual controller"_qs},
                {u"optional"_qs, true}, {u"informational"_qs, true},
                {u"state"_qs, observed ? u"ready"_qs : u"listening"_qs},
                {u"message"_qs, observed ? u"Mapped output activity detected."_qs
                                           : u"Listening for mapped output activity…"_qs}});
            allObserved = allObserved && observed;
        }
    } else {
        const PhysicalControllerCapabilities physical = currentPhysicalCapabilities();
        const bool observed = physical.connected
            && runtime.meaningfulInputSequence.load(std::memory_order_relaxed) > 0;
        const QString title = m_setupAssistantScopeType == u"device"_qs
            && savedControllerRecord(m_setupAssistantScopeId)
            ? savedControllerRecord(m_setupAssistantScopeId)->displayName : u"Physical controller"_qs;
        activity.append(QVariantMap{{u"id"_qs, u"physical"_qs}, {u"kind"_qs, u"input"_qs},
            {u"title"_qs, title}, {u"optional"_qs, true}, {u"informational"_qs, true},
            {u"state"_qs, !physical.connected ? u"offline"_qs : observed ? u"ready"_qs : u"listening"_qs},
            {u"message"_qs, !physical.connected ? u"Activity is available when the controller is connected."_qs
                                      : observed ? u"Input detected during this connection session."_qs
                                                 : u"Listening for controller input…"_qs}});
        allObserved = !physical.connected || observed;
    }
    return QVariantMap{{u"active"_qs, m_setupAssistantLiveTestActive},
        {u"complete"_qs, allObserved}, {u"steps"_qs, activity}};
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
    if (const DeviceRig *rig = activeDeviceRig()) {
        const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [rig](const DeviceRigStatus &status) { return status.rigId == rig->id; });
        const DeviceRigHealth health = found == m_deviceRigStatuses.cend()
            ? DeviceRigHealth::Offline : found->health;
        return health == DeviceRigHealth::Ready ? u"READY"_qs
            : health == DeviceRigHealth::Partial ? u"PARTIAL"_qs
            : health == DeviceRigHealth::Conflict ? u"ACTION REQUIRED"_qs
            : health == DeviceRigHealth::NeedsAttention ? u"NEEDS VERIFICATION"_qs
            : u"OFFLINE"_qs;
    }
    return ControllerReadinessService::stateLabel(m_readiness.plan().state);
}

QString AppBackend::controllerReadinessStatus() const
{
    if (const DeviceRig *rig = activeDeviceRig()) {
        const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(m_configuration, rig->id);
        if (!compiled.valid) return u"ROUTING ACTION REQUIRED — "_qs + compiled.issue;
        const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
            [rig](const DeviceRigStatus &status) { return status.rigId == rig->id; });
        if (found == m_deviceRigStatuses.cend()) return u"Device Rig has not yet been discovered."_qs;
        return QString(u"%1 — %2/%3 physical inputs connected."_qs).arg(rig->name)
            .arg(found->connectedMemberIds.size()).arg(rig->members.size());
    }
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
bool AppBackend::controllerReconnectRequired() const
{
    return m_readiness.reconnectVerificationPending() || m_readiness.reconnectReconciliationPending();
}
bool AppBackend::controllerDisconnectObserved() const
{
    return m_readiness.reconnectDisconnectObserved();
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
        const bool active = currentProfile().outputLayoutId == layout.id;
        const QVariantMap detail = virtualOutputDetail(layout.id);
        result.append(QVariantMap{{u"id"_qs, layout.id}, {u"name"_qs, layout.name},
            {u"deviceId"_qs, layout.requirements.deviceId}, {u"axes"_qs, axes.join(u" · "_qs)},
            {u"buttons"_qs, layout.requirements.buttons},
            {u"continuousPovs"_qs, layout.requirements.continuousPovs},
            {u"discretePovs"_qs, layout.requirements.discretePovs},
            {u"profileCount"_qs, profileCount}, {u"active"_qs, active},
            {u"ready"_qs, detail.value(u"ready"_qs, false)},
            {u"inspected"_qs, detail.value(u"inspected"_qs, false)},
            {u"readinessState"_qs, detail.value(u"readinessState"_qs, u"SAVED"_qs)},
            {u"status"_qs, detail.value(u"status"_qs, u"Saved output"_qs)},
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
            QString label = automationConditionSummary(condition, m_configuration);
            if (!condition.controllerRecordId.isEmpty()) {
                if (const SavedControllerRecord *record = savedControllerRecord(condition.controllerRecordId)) {
                    label = record->displayName + u" · "_qs + label;
                } else {
                    label = u"Missing device · "_qs + label;
                }
            }
            conditionLabels.append(label);
            conditions.append(QVariantMap{{u"type"_qs, static_cast<int>(condition.type)},
                {u"axis"_qs, condition.axis}, {u"minimum"_qs, condition.minimum},
                {u"maximum"_qs, condition.maximum}, {u"hysteresis"_qs, condition.hysteresis},
                {u"button"_qs, condition.button}, {u"povHat"_qs, condition.povHat},
                {u"povDirection"_qs, static_cast<int>(condition.povDirection)},
                {u"profileId"_qs, condition.profileId}, {u"pressCount"_qs, condition.pressCount},
                {u"multiPressWindowMs"_qs, condition.multiPressWindowMs},
                {u"longPressDurationMs"_qs, condition.longPressDurationMs},
                {u"controllerRecordId"_qs, condition.controllerRecordId}});
        }
        QVariantList actions;
        QStringList actionLabels;
        for (const AutomationActionDefinition &action : definition.actions) {
            QString label = automationActionSummary(action, m_configuration);
            if (!action.sourceControllerRecordId.isEmpty()) {
                if (const SavedControllerRecord *record = savedControllerRecord(action.sourceControllerRecordId)) {
                    label = record->displayName + u" · "_qs + label;
                }
            }
            if (!action.outputLayoutId.isEmpty()) {
                if (const VirtualOutputLayout *layout = findOutputLayout(m_configuration, action.outputLayoutId)) {
                    label += u" → "_qs + layout->name;
                }
            }
            actionLabels.append(label);
            actions.append(QVariantMap{{u"type"_qs, static_cast<int>(action.type)},
                {u"virtualButton"_qs, action.virtualButton}, {u"profileId"_qs, action.profileId},
                {u"adaptiveResponsePresetId"_qs, action.adaptiveResponsePresetId},
                {u"targetAxis"_qs, action.targetAxis}, {u"sourceAxis"_qs, action.sourceAxis},
                {u"sourceStage"_qs, static_cast<int>(action.sourceStage)}, {u"value"_qs, action.value},
                {u"offset"_qs, action.offset}, {u"minimum"_qs, action.minimum}, {u"maximum"_qs, action.maximum},
                {u"tapDurationMs"_qs, action.tapDurationMs},
                {u"sourceControllerRecordId"_qs, action.sourceControllerRecordId},
                {u"outputLayoutId"_qs, action.outputLayoutId}});
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : currentProfile().axes;
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const VirtualAxis axis = static_cast<VirtualAxis>(index);
        const bool available = runtime.virtualAxisAvailable[static_cast<size_t>(index)].load();
        bool configured = false;
        for (const AxisMapping &mapping : axes) {
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
    const ControllerProfile &profile = currentProfile();
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (!runtime.virtualAxisAvailable[static_cast<size_t>(index)].load()) continue;
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
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    for (int source = 0; source < std::min(physicalCount, static_cast<int>(bindings.size())); ++source) {
        const ButtonBinding &binding = bindings[static_cast<size_t>(source)];
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

QString AppBackend::signalFlowWorkspaceKey() const
{
    const DeviceProfileMapping *mapping = editingDeviceMapping();
    const QString source = mapping ? mapping->controllerRecordId : u"legacy-source"_qs;
    return QString(u"signal-flow:%1:%2:%3"_qs).arg(currentProfile().id,
        m_configuration.editingDeviceRigId.isEmpty() ? u"no-rig"_qs : m_configuration.editingDeviceRigId,
        source.isEmpty() ? u"legacy-source"_qs : source);
}

bool AppBackend::signalFlowCanUndo() const
{
    return !m_signalFlowUndo.empty()
        && m_signalFlowUndo.back().undoRevision == m_configurationGeneration;
}

bool AppBackend::signalFlowCanRedo() const
{
    return !m_signalFlowRedo.empty()
        && m_signalFlowRedo.back().redoRevision == m_configurationGeneration;
}

QVariantMap AppBackend::signalFlowGraph() const
{
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *editingMapping = editingDeviceMapping();
    const QString graphDeviceRigId = !m_configuration.editingDeviceRigId.trimmed().isEmpty()
        ? m_configuration.editingDeviceRigId : profile.deviceRigId;
    const DeviceRig *graphDeviceRig = findDeviceRig(m_configuration, graphDeviceRigId);
    const QString graphDeviceRigName = graphDeviceRig ? graphDeviceRig->name
        : graphDeviceRigId.isEmpty() ? u"Profile-local input scope"_qs : u"Missing Device Rig"_qs;
    // A multi-member rig can intentionally have no current *editing* member.
    // The graph must still be a truthful read-only projection in that state,
    // rather than falling back to legacy profile fields and hiding the rig.
    // Pick a stable visible primary member for presentation only; mutation
    // remains disabled until the user chooses an explicit input scope.
    const DeviceProfileMapping *mapping = editingMapping;
    QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    if (!mapping && graphDeviceRig) {
        for (const DeviceRigMember &member : graphDeviceRig->members) {
            if (!member.enabled) continue;
            if (const DeviceProfileMapping *candidate = findDeviceProfileMapping(profile, member.controllerRecordId)) {
                mapping = candidate;
                controllerId = candidate->controllerRecordId;
                break;
            }
        }
    }
    if (!mapping && !profile.deviceMappings.empty()) {
        mapping = &profile.deviceMappings.front();
        controllerId = mapping->controllerRecordId;
    }
    const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
    const ButtonBindings &buttons = mapping ? mapping->buttons : profile.buttons;
    const PovBindings &povs = mapping ? mapping->povs : profile.povs;
    const NativePovBindings &nativePovs = mapping ? mapping->nativePovBindings
                                                   : m_configuration.nativePovBindings;
    const VirtualOutputLayout *layout = activeOutputLayout();
    const QString workspaceKey = signalFlowWorkspaceKey();
    const QString inputNodeId = mapping
        ? QString(u"input:%1:%2"_qs).arg(profile.id, controllerId)
        : QString(u"input:%1:legacy"_qs).arg(profile.id);
    const QString outputNodeId = layout ? QString(u"output:%1"_qs).arg(layout->id)
                                        : u"output:missing"_qs;

    const auto saved = savedControllerRecord(controllerId);
    const QString inputLabel = saved ? saved->displayName
        : controllerId.isEmpty() ? u"Profile input"_qs : u"Saved controller"_qs;
    // Persisted `lastSeen` is historical telemetry, not a current connection
    // claim. The graph remains inspectable offline, while Effective mode only
    // turns green for the present authoritative monitor state.
    // A Signal Flow scope is allowed to describe a saved member that is not
    // the currently acquired DirectInput device.  Do not borrow the global
    // worker connection flag in that case: it would make a different live
    // controller paint this card as ready.  `physicalDeviceDetail` resolves
    // the durable record against the discovery inventory on the UI/control
    // plane and preserves the card's identity when it is offline.
    const QVariantMap inputDeviceDetail = controllerId.isEmpty()
        ? QVariantMap{} : physicalDeviceDetail(controllerId);
    const bool inputConnected = controllerId.isEmpty()
        ? physicalConnected() : inputDeviceDetail.value(u"connected"_qs).toBool();
    const bool inputMissing = !controllerId.isEmpty() && !saved;
    const auto layoutFor = [this, &workspaceKey](const QString &id, float fallbackX, float fallbackY) {
        for (const SignalFlowNodeLayout &savedLayout : m_configuration.signalFlow.nodeLayouts) {
            if (savedLayout.workspaceKey == workspaceKey && savedLayout.objectId == id) {
                return QVariantMap{{u"x"_qs, savedLayout.x}, {u"y"_qs, savedLayout.y},
                                   {u"pinned"_qs, savedLayout.pinned}};
            }
        }
        return QVariantMap{{u"x"_qs, fallbackX}, {u"y"_qs, fallbackY}, {u"pinned"_qs, false}};
    };
    const auto applyLayout = [](QVariantMap *node, const QVariantMap &placement) {
        if (!node) return;
        for (auto entry = placement.cbegin(); entry != placement.cend(); ++entry) {
            node->insert(entry.key(), entry.value());
        }
    };
    const auto portGroupsFor = [this, &workspaceKey](const QString &cardId, bool output) {
        const QStringList groups = output
            ? QStringList{u"Virtual Axes"_qs, u"Virtual Buttons"_qs, u"Virtual POV"_qs}
            : QStringList{u"Axes"_qs, u"Buttons"_qs, u"POV Directions"_qs, u"Native POV"_qs};
        QVariantList result;
        for (const QString &group : groups) {
            bool collapsed = false;
            for (const SignalFlowPortGroupState &state : m_configuration.signalFlow.portGroups) {
                if (state.workspaceKey == workspaceKey && state.cardId == cardId
                    && state.group == group) {
                    collapsed = state.collapsed;
                    break;
                }
            }
            result.append(QVariantMap{{u"group"_qs, group}, {u"collapsed"_qs, collapsed}});
        }
        return result;
    };

    QVariantList inputPorts;
    QVariantList outputPorts;
    QVariantList nodes;
    QVariantList routes;
    const auto inScope = [&profile, &controllerId](const SignalFlowRoute &route) {
        return route.profileId == profile.id && route.controllerRecordId == controllerId;
    };
    const auto sourcePortId = [](SignalFlowPortKind kind, int index, int subIndex) {
        switch (kind) {
        case SignalFlowPortKind::Axis: return QString(u"axis:%1"_qs).arg(index);
        case SignalFlowPortKind::Button: return QString(u"button:%1"_qs).arg(index);
        case SignalFlowPortKind::PovDirection:
            return QString(u"pov:%1:%2"_qs).arg(index).arg(subIndex);
        case SignalFlowPortKind::NativePov: return QString(u"native-pov:%1"_qs).arg(index);
        }
        return QString{};
    };
    const auto destinationPortId = [](SignalFlowPortKind kind, int index, int subIndex) {
        switch (kind) {
        case SignalFlowPortKind::Axis: return QString(u"axis:%1"_qs).arg(index);
        case SignalFlowPortKind::Button: return QString(u"button:%1"_qs).arg(index);
        case SignalFlowPortKind::PovDirection:
            return QString(u"pov:%1:%2"_qs).arg(index).arg(subIndex);
        case SignalFlowPortKind::NativePov:
            return QString(u"native-pov:%1:%2"_qs).arg(subIndex).arg(index);
        }
        return QString{};
    };
    const auto sourceIsMapped = [&inScope, this](SignalFlowPortKind kind, int index, int subIndex) {
        return std::any_of(m_configuration.signalFlow.routes.cbegin(),
            m_configuration.signalFlow.routes.cend(), [&inScope, kind, index, subIndex](const SignalFlowRoute &route) {
                return route.enabled && inScope(route) && route.sourceKind == kind
                    && route.sourceIndex == index && route.sourceSubIndex == subIndex;
            });
    };
    const auto destinationIsMapped = [&profile, this](SignalFlowPortKind kind, int index, int subIndex) {
        return std::any_of(m_configuration.signalFlow.routes.cbegin(),
            m_configuration.signalFlow.routes.cend(), [&profile, kind, index, subIndex](const SignalFlowRoute &route) {
                return route.enabled && route.profileId == profile.id && route.destinationKind == kind
                    && route.destinationIndex == index && route.destinationSubIndex == subIndex;
            });
    };
    const auto adaptiveSummaryFor = [this, &profile](const DeviceProfileMapping *scopeMapping, int axis) {
        const int sourceAxis = std::clamp(axis, 0, kPhysicalAxisCount - 1);
        const RuntimeAdaptiveResponseConfig adaptive = scopeMapping
            ? resolveAdaptiveResponseConfiguration(m_configuration, profile, *scopeMapping, sourceAxis)
            : resolveAdaptiveResponseConfiguration(m_configuration, profile, sourceAxis);
        QVariantMap result = adaptiveSettingsMap(adaptive);
        result.insert(u"summary"_qs, QString(u"%1 model · %2 ms horizon · ±%3 max lead"_qs)
            .arg(result.value(u"model"_qs).toString().toUpper())
            .arg(qRound(result.value(u"maximumHorizonMs"_qs).toDouble()))
            .arg(QString::number(result.value(u"maximumLead"_qs).toDouble(), 'f', 2)));
        return result;
    };
    const auto annotateProcessorSettings = [&adaptiveSummaryFor](QVariantMap *detail,
                                                                   const QString &semantic,
                                                                   const DeviceProfileMapping *scopeMapping,
                                                                   int axis) {
        if (!detail || semantic != u"adaptive-response"_qs) return;
        const QVariantMap adaptive = adaptiveSummaryFor(scopeMapping, axis);
        detail->insert(u"adaptiveSettings"_qs, adaptive);
        detail->insert(u"settingsSummary"_qs, adaptive.value(u"summary"_qs));
    };
    QSet<QString> emittedProcessors;
    const auto addProcessor = [&](const QString &processorId, int axis, float order) {
        if (processorId.isEmpty() || emittedProcessors.contains(processorId)) {
            return processorId.isEmpty() ? QString{} : QString(u"processor:%1"_qs).arg(processorId);
        }
        const auto identityIt = std::find_if(m_configuration.signalFlow.processorIdentities.cbegin(),
            m_configuration.signalFlow.processorIdentities.cend(),
            [&processorId](const SignalFlowIdentityRecord &candidate) { return candidate.id == processorId; });
        if (identityIt == m_configuration.signalFlow.processorIdentities.cend() || !identityIt->active) {
            return QString{};
        }
        const SignalFlowIdentityRecord *identity = &*identityIt;
        emittedProcessors.insert(processorId);
        const QString key = identity->key;
        const bool shared = key.startsWith(u"processor:shared:"_qs);
        QString semantic = key.section(u':', -1);
        QString label = u"Signal conditioner"_qs;
        QString detail = u"Active route processor."_qs;
        if (key.startsWith(u"processor:mixer:"_qs)) {
            semantic = u"mixer"_qs;
            label = u"Explicit mixer"_qs;
            detail = u"Combines multiple analog inputs only when configured."_qs;
        } else if (semantic == u"domain"_qs) {
            label = u"One-sided domain"_qs;
            detail = u"Uses 0–100% output semantics."_qs;
        } else if (semantic == u"deadzone"_qs) {
            label = u"Deadzone"_qs;
            detail = u"Suppresses calibrated center noise."_qs;
        } else if (semantic == u"center-hold"_qs) {
            label = u"Center hold"_qs;
            detail = u"Applies the configured release band."_qs;
        } else if (semantic == u"invert"_qs) {
            label = u"Invert"_qs;
            detail = u"Reverses output direction."_qs;
        } else if (semantic == u"curve"_qs) {
            label = u"Response curve"_qs;
            detail = u"Uses this source's focused curve setting."_qs;
        } else if (semantic == u"limits"_qs) {
            label = u"Output limits"_qs;
            detail = u"Uses this source's focused output limits."_qs;
        } else if (semantic == u"adaptive-response"_qs) {
            label = u"Adaptive Response"_qs;
            detail = adaptiveSummaryFor(mapping, axis).value(u"summary"_qs).toString();
        }
        int sharedChannelCount = 0;
        int sharedOwnerAxis = -1;
        if (shared) {
            const auto sharedProcessor = std::find_if(
                m_configuration.signalFlow.sharedProcessors.cbegin(),
                m_configuration.signalFlow.sharedProcessors.cend(),
                [&processorId](const SignalFlowSharedProcessor &candidate) {
                    return candidate.enabled && candidate.id == processorId;
                });
            if (sharedProcessor != m_configuration.signalFlow.sharedProcessors.cend()) {
                sharedChannelCount = static_cast<int>(sharedProcessor->sourceAxes.size());
                sharedOwnerAxis = sharedProcessor->ownerAxis;
                label = u"Shared "_qs + label;
                detail = QString(u"One durable %1 setting mirrored to %2 routed source axes."_qs)
                    .arg(semantic).arg(sharedChannelCount);
            }
        }
        const QString nodeId = QString(u"processor:%1"_qs).arg(processorId);
        QVariantMap node{{u"id"_qs, nodeId}, {u"objectId"_qs, processorId},
                         {u"kind"_qs, u"processor"_qs}, {u"label"_qs, label},
                         {u"detail"_qs, detail}, {u"axis"_qs, axis},
                         {u"semantic"_qs, semantic}, {u"shared"_qs, shared},
                         {u"sourceAxis"_qs, axis},
                         {u"settingsPage"_qs, semantic == u"curve"_qs ? 6
                             : semantic == u"adaptive-response"_qs ? 9 : -1},
                         {u"canOpenFullSettings"_qs, semantic == u"curve"_qs
                             || semantic == u"adaptive-response"_qs},
                         {u"sharedChannelCount"_qs, sharedChannelCount},
                         {u"sharedOwnerAxis"_qs, sharedOwnerAxis},
                         {u"connected"_qs, true}};
        applyLayout(&node, layoutFor(processorId, 340.0F + order * 162.0F, 115.0F + std::max(axis, 0) * 92.0F));
        nodes.append(node);
        return nodeId;
    };

    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const AxisMapping &axisMapping = axes[static_cast<size_t>(axis)];
        const QString axisLabel = axisMapping.customName.trimmed().isEmpty()
            ? physicalAxisLabel(static_cast<PhysicalAxis>(axis)) : axisMapping.customName.trimmed();
        const bool mapped = sourceIsMapped(SignalFlowPortKind::Axis, axis, -1);
        inputPorts.append(QVariantMap{{u"id"_qs, QString(u"axis:%1"_qs).arg(axis)},
                                      {u"kind"_qs, u"axis"_qs}, {u"index"_qs, axis},
                                      {u"group"_qs, u"Axes"_qs},
                                      {u"label"_qs, axisLabel}, {u"technicalLabel"_qs,
                                      physicalAxisLabel(static_cast<PhysicalAxis>(axis))},
                                      {u"mapped"_qs, mapped},
                                      {u"available"_qs, m_configuration.axisActivity[static_cast<size_t>(axis)]
                                          != PhysicalAxisActivity::Fixed}});
    }

    const int inputButtonCount = std::max(static_cast<int>(buttons.size()), kMaximumPhysicalButtons);
    for (int button = 0; button < inputButtonCount; ++button) {
        const ButtonBinding binding = button < static_cast<int>(buttons.size())
            ? buttons[static_cast<size_t>(button)] : ButtonBinding{};
        const QString label = binding.customName.trimmed().isEmpty()
            ? QString(u"Button %1"_qs).arg(button + 1) : binding.customName.trimmed();
        const bool mapped = sourceIsMapped(SignalFlowPortKind::Button, button, -1);
        inputPorts.append(QVariantMap{{u"id"_qs, QString(u"button:%1"_qs).arg(button)},
                                      {u"kind"_qs, u"button"_qs}, {u"index"_qs, button},
                                      {u"group"_qs, u"Buttons"_qs},
                                      {u"label"_qs, label}, {u"technicalLabel"_qs,
                                      QString(u"Button %1"_qs).arg(button + 1)},
                                      {u"mapped"_qs, mapped}, {u"available"_qs, true}});
    }

    const int inputPovCount = std::max(static_cast<int>(povs.size()), kMaximumPhysicalPovs);
    for (int hat = 0; hat < inputPovCount; ++hat) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const QString label = QString(u"POV %1 %2"_qs).arg(hat + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)));
            const bool mapped = sourceIsMapped(SignalFlowPortKind::PovDirection, hat, direction);
            inputPorts.append(QVariantMap{{u"id"_qs, QString(u"pov:%1:%2"_qs).arg(hat).arg(direction)},
                                          {u"kind"_qs, u"pov"_qs}, {u"index"_qs, hat},
                                          {u"group"_qs, u"POV Directions"_qs},
                                          {u"subIndex"_qs, direction}, {u"label"_qs, label},
                                          {u"technicalLabel"_qs, label}, {u"mapped"_qs, mapped},
                                          {u"available"_qs, true}});
        }
    }
    for (int hat = 0; hat < std::max(static_cast<int>(nativePovs.size()), kMaximumPhysicalPovs); ++hat) {
        inputPorts.append(QVariantMap{{u"id"_qs, sourcePortId(SignalFlowPortKind::NativePov, hat, -1)},
                                      {u"kind"_qs, u"native-pov"_qs}, {u"index"_qs, hat},
                                      {u"group"_qs, u"Native POV"_qs},
                                      {u"label"_qs, QString(u"Native POV %1"_qs).arg(hat + 1)},
                                      {u"technicalLabel"_qs, QString(u"Native POV %1"_qs).arg(hat + 1)},
                                      {u"mapped"_qs, sourceIsMapped(SignalFlowPortKind::NativePov, hat, -1)},
                                      {u"available"_qs, true}});
    }

    for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
        const QString alias = profile.virtualAxisAliases[static_cast<size_t>(axis)].trimmed();
        outputPorts.append(QVariantMap{{u"id"_qs, QString(u"axis:%1"_qs).arg(axis)},
                                       {u"kind"_qs, u"axis"_qs}, {u"index"_qs, axis},
                                       {u"group"_qs, u"Virtual Axes"_qs},
                                       {u"label"_qs, alias.isEmpty() ? virtualAxisLabel(static_cast<VirtualAxis>(axis)) : alias},
                                       {u"technicalLabel"_qs, virtualAxisLabel(static_cast<VirtualAxis>(axis))},
                                       {u"mapped"_qs, destinationIsMapped(SignalFlowPortKind::Axis, axis, -1)},
                                       {u"available"_qs, !layout || layout->requirements.axes[static_cast<size_t>(axis)]}});
    }
    const int buttonCapacity = layout ? layout->requirements.buttons : 0;
    for (int button = 1; button <= std::max(buttonCapacity, 16); ++button) {
        outputPorts.append(QVariantMap{{u"id"_qs, QString(u"button:%1"_qs).arg(button)},
                                       {u"kind"_qs, u"button"_qs}, {u"index"_qs, button},
                                       {u"group"_qs, u"Virtual Buttons"_qs},
                                       {u"label"_qs, QString(u"vJoy Button %1"_qs).arg(button)},
                                       {u"technicalLabel"_qs, QString(u"Button %1"_qs).arg(button)},
                                       {u"mapped"_qs, destinationIsMapped(SignalFlowPortKind::Button, button, -1)},
                                       {u"available"_qs, !layout || button <= layout->requirements.buttons}});
    }
    for (int targetType = static_cast<int>(NativePovTargetType::Continuous);
         targetType <= static_cast<int>(NativePovTargetType::Discrete); ++targetType) {
        const int capacity = !layout ? 0 : targetType == static_cast<int>(NativePovTargetType::Continuous)
            ? layout->requirements.continuousPovs : layout->requirements.discretePovs;
        for (int target = 1; target <= capacity; ++target) {
            const QString label = targetType == static_cast<int>(NativePovTargetType::Continuous)
                ? QString(u"vJoy Continuous POV %1"_qs).arg(target)
                : QString(u"vJoy Discrete POV %1"_qs).arg(target);
            outputPorts.append(QVariantMap{{u"id"_qs, destinationPortId(SignalFlowPortKind::NativePov, target, targetType)},
                                           {u"kind"_qs, u"native-pov"_qs}, {u"index"_qs, target},
                                           {u"group"_qs, u"Virtual POV"_qs},
                                           {u"subIndex"_qs, targetType}, {u"label"_qs, label},
                                           {u"technicalLabel"_qs, label},
                                           {u"mapped"_qs, destinationIsMapped(SignalFlowPortKind::NativePov, target, targetType)},
                                           {u"available"_qs, true}});
        }
    }

    const auto mixerModeLabel = [](SignalFlowMixerMode mode) {
        switch (mode) {
        case SignalFlowMixerMode::Average: return u"Average"_qs;
        case SignalFlowMixerMode::SumClamped: return u"Sum Clamped"_qs;
        case SignalFlowMixerMode::HighestMagnitude: return u"Highest Magnitude"_qs;
        case SignalFlowMixerMode::Disabled: return u"Disabled"_qs;
        }
        return u"Disabled"_qs;
    };
    for (const SignalFlowMixer &mixer : m_configuration.signalFlow.mixers) {
        if (mixer.profileId != profile.id || mixer.controllerRecordId != controllerId
            || !mixer.enabled || mixer.mode == SignalFlowMixerMode::Disabled || mixer.id.isEmpty()) continue;
        const QString nodeId = addProcessor(mixer.id, -1, 5.0F + mixer.destinationAxis * 0.2F);
        if (nodeId.isEmpty()) continue;
        for (QVariant &value : nodes) {
            QVariantMap node = value.toMap();
            if (node.value(u"id"_qs).toString() != nodeId) continue;
            node.insert(u"label"_qs, QString(u"Mixer · %1"_qs).arg(mixerModeLabel(mixer.mode)));
            node.insert(u"detail"_qs, QString(u"Explicit analog merge into %1."_qs)
                .arg(virtualAxisLabel(static_cast<VirtualAxis>(mixer.destinationAxis))));
            node.insert(u"semantic"_qs, u"mixer"_qs);
            node.insert(u"mixerMode"_qs, mixerModeLabel(mixer.mode));
            value = node;
            break;
        }
    }

    for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
        if (!inScope(route)) continue;
        QString sourceLabel;
        switch (route.sourceKind) {
        case SignalFlowPortKind::Axis:
            sourceLabel = route.sourceIndex >= 0 && route.sourceIndex < kPhysicalAxisCount
                ? (axes[static_cast<size_t>(route.sourceIndex)].customName.trimmed().isEmpty()
                    ? physicalAxisLabel(static_cast<PhysicalAxis>(route.sourceIndex))
                    : axes[static_cast<size_t>(route.sourceIndex)].customName.trimmed())
                : u"Unknown axis"_qs;
            break;
        case SignalFlowPortKind::Button:
            sourceLabel = route.sourceIndex >= 0 && route.sourceIndex < static_cast<int>(buttons.size())
                && !buttons[static_cast<size_t>(route.sourceIndex)].customName.trimmed().isEmpty()
                ? buttons[static_cast<size_t>(route.sourceIndex)].customName.trimmed()
                : QString(u"Button %1"_qs).arg(route.sourceIndex + 1);
            break;
        case SignalFlowPortKind::PovDirection:
            sourceLabel = QString(u"POV %1 %2"_qs).arg(route.sourceIndex + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(route.sourceSubIndex + 1)));
            break;
        case SignalFlowPortKind::NativePov:
            sourceLabel = QString(u"Native POV %1"_qs).arg(route.sourceIndex + 1);
            break;
        }
        QString destinationLabel;
        switch (route.destinationKind) {
        case SignalFlowPortKind::Axis: {
            const QString alias = route.destinationIndex > 0 && route.destinationIndex < kVirtualAxisSlotCount
                ? profile.virtualAxisAliases[static_cast<size_t>(route.destinationIndex)].trimmed() : QString{};
            destinationLabel = alias.isEmpty() ? virtualAxisLabel(static_cast<VirtualAxis>(route.destinationIndex)) : alias;
            break;
        }
        case SignalFlowPortKind::Button:
            destinationLabel = QString(u"vJoy Button %1"_qs).arg(route.destinationIndex);
            break;
        case SignalFlowPortKind::PovDirection:
            destinationLabel = QString(u"vJoy POV %1 %2"_qs).arg(route.destinationIndex)
                .arg(route.destinationSubIndex + 1);
            break;
        case SignalFlowPortKind::NativePov:
            destinationLabel = route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                ? QString(u"vJoy Continuous POV %1"_qs).arg(route.destinationIndex)
                : QString(u"vJoy Discrete POV %1"_qs).arg(route.destinationIndex);
            break;
        }
        QVariantList processors;
        QVariantList processorDetails;
        QString viaNodeId;
        float processorOrder = 0.0F;
        for (const QString &processorId : route.processorPath) {
            const QString nodeId = addProcessor(processorId, route.sourceIndex, processorOrder++);
            if (nodeId.isEmpty()) continue;
            processors.append(nodeId);
            const auto identityIt = std::find_if(m_configuration.signalFlow.processorIdentities.cbegin(),
                m_configuration.signalFlow.processorIdentities.cend(),
                [&processorId](const SignalFlowIdentityRecord &candidate) { return candidate.id == processorId; });
            if (identityIt != m_configuration.signalFlow.processorIdentities.cend()
                && identityIt->key.startsWith(u"processor:mixer:"_qs)) {
                viaNodeId = nodeId;
            }
            if (identityIt != m_configuration.signalFlow.processorIdentities.cend()) {
                const QString semantic = identityIt->key.startsWith(u"processor:mixer:"_qs)
                    ? u"mixer"_qs : identityIt->key.section(u':', -1);
                QVariantMap detail{{u"id"_qs, processorId}, {u"semantic"_qs, semantic},
                                   {u"sourceAxis"_qs, route.sourceIndex},
                                   {u"settingsPage"_qs, semantic == u"curve"_qs ? 6
                                       : semantic == u"adaptive-response"_qs ? 9 : -1},
                                   {u"canOpenFullSettings"_qs, semantic == u"curve"_qs
                                       || semantic == u"adaptive-response"_qs}};
                annotateProcessorSettings(&detail, semantic, editingMapping, route.sourceIndex);
                if (identityIt->key.startsWith(u"processor:shared:"_qs)) {
                    const auto shared = std::find_if(
                        m_configuration.signalFlow.sharedProcessors.cbegin(),
                        m_configuration.signalFlow.sharedProcessors.cend(),
                        [&processorId](const SignalFlowSharedProcessor &candidate) {
                            return candidate.enabled && candidate.id == processorId;
                        });
                    if (shared != m_configuration.signalFlow.sharedProcessors.cend()) {
                        detail.insert(u"shared"_qs, true);
                        detail.insert(u"sharedChannelCount"_qs,
                                      static_cast<int>(shared->sourceAxes.size()));
                        detail.insert(u"sharedOwnerAxis"_qs, shared->ownerAxis);
                    }
                }
                processorDetails.append(std::move(detail));
            }
        }
        bool destinationAvailable = layout != nullptr;
        if (layout && route.destinationKind == SignalFlowPortKind::Axis) {
            destinationAvailable = route.destinationIndex > 0
                && route.destinationIndex < kVirtualAxisSlotCount
                && layout->requirements.axes[static_cast<size_t>(route.destinationIndex)];
        } else if (layout && route.destinationKind == SignalFlowPortKind::Button) {
            destinationAvailable = route.destinationIndex > 0
                && route.destinationIndex <= layout->requirements.buttons;
        } else if (layout && route.destinationKind == SignalFlowPortKind::NativePov) {
            const int capacity = route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                ? layout->requirements.continuousPovs : layout->requirements.discretePovs;
            destinationAvailable = route.destinationIndex > 0 && route.destinationIndex <= capacity;
        }
        QString health = u"ready"_qs;
        QString healthDetail = u"Ready when Mapping Active is enabled."_qs;
        if (!route.enabled) {
            health = route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                ? u"conflict"_qs : u"disabled"_qs;
            healthDetail = health == u"conflict"_qs
                ? u"This analog route is disabled until its destination has an explicit Mixer or a single source."_qs
                : u"This configured route is disabled and does not drive the virtual output."_qs;
        } else if (!inputConnected) {
            health = u"source-offline"_qs;
            healthDetail = u"The saved physical input is offline; the route remains editable."_qs;
        } else if (!destinationAvailable) {
            health = u"destination-unavailable"_qs;
            healthDetail = u"The selected virtual output does not currently provide this destination."_qs;
        } else if (!mappingActive()) {
            health = u"mapping-off"_qs;
            healthDetail = u"The route is configured and ready; turn on Mapping Active to send output."_qs;
        }
        const bool effective = route.enabled && mappingActive() && inputConnected && destinationAvailable && vjoyReady();
        routes.append(QVariantMap{{u"id"_qs, route.id.isEmpty() ? route.identityKey : route.id},
                                  {u"kind"_qs, route.sourceKind == SignalFlowPortKind::Axis ? u"axis"_qs
                                      : route.sourceKind == SignalFlowPortKind::Button ? u"button"_qs
                                      : route.sourceKind == SignalFlowPortKind::PovDirection ? u"pov"_qs : u"native-pov"_qs},
                                  {u"sourceNodeId"_qs, inputNodeId},
                                  {u"sourcePortId"_qs, sourcePortId(route.sourceKind, route.sourceIndex, route.sourceSubIndex)},
                                  {u"sourceIndex"_qs, route.sourceIndex},
                                  {u"sourceSubIndex"_qs, route.sourceSubIndex},
                                  {u"sourceLabel"_qs, sourceLabel}, {u"destinationNodeId"_qs, outputNodeId},
                                  {u"destinationPortId"_qs, destinationPortId(route.destinationKind, route.destinationIndex,
                                                                            route.destinationSubIndex)},
                                  {u"destinationIndex"_qs, route.destinationIndex},
                                  {u"destinationSubIndex"_qs, route.destinationSubIndex},
                                  {u"destinationLabel"_qs, destinationLabel},
                                  {u"processors"_qs, processors}, {u"processorDetails"_qs, processorDetails},
                                  {u"viaNodeId"_qs, viaNodeId},
                                  {u"configured"_qs, true}, {u"enabled"_qs, route.enabled},
                                  {u"effective"_qs, effective}, {u"primaryProjection"_qs, route.primaryProjection},
                                  {u"health"_qs, health}, {u"healthDetail"_qs, healthDetail},
                                  {u"editable"_qs, editingMapping != nullptr || graphDeviceRig == nullptr},
                                  {u"controllerRecordId"_qs, controllerId}});
    }

    // The focused editors deliberately edit one physical member at a time,
    // but Signal Flow is a Device Rig viewer as well as an editor. Project
    // every saved member (including imported/missing references) into the
    // same output topology. Secondary cards are read-only until their card
    // explicitly becomes the editing scope, so no pointer gesture can write
    // a route against the wrong controller.
    QStringList secondaryControllerIds;
    const auto appendSecondaryController = [&secondaryControllerIds, &controllerId](const QString &id) {
        const QString normalized = id.trimmed();
        if (!normalized.isEmpty() && normalized != controllerId && !secondaryControllerIds.contains(normalized)) {
            secondaryControllerIds.append(normalized);
        }
    };
    if (graphDeviceRig) {
        for (const DeviceRigMember &member : graphDeviceRig->members) {
            if (member.enabled) appendSecondaryController(member.controllerRecordId);
        }
    }
    for (const DeviceProfileMapping &candidate : profile.deviceMappings) {
        appendSecondaryController(candidate.controllerRecordId);
    }
    for (const SignalFlowRoute &candidate : m_configuration.signalFlow.routes) {
        if (candidate.profileId == profile.id) appendSecondaryController(candidate.controllerRecordId);
    }
    const auto scopedSourcePortId = [&sourcePortId](const QString &scopeId, SignalFlowPortKind kind,
                                                     int index, int subIndex) {
        return QString(u"%1|%2"_qs).arg(scopeId, sourcePortId(kind, index, subIndex));
    };
    const auto routeSourceLabel = [](const SignalFlowRoute &route, const DeviceProfileMapping *scopeMapping) {
        switch (route.sourceKind) {
        case SignalFlowPortKind::Axis:
            if (scopeMapping && route.sourceIndex >= 0 && route.sourceIndex < kPhysicalAxisCount) {
                const AxisMapping &axis = scopeMapping->axes[static_cast<size_t>(route.sourceIndex)];
                return axis.customName.trimmed().isEmpty()
                    ? physicalAxisLabel(static_cast<PhysicalAxis>(route.sourceIndex)) : axis.customName.trimmed();
            }
            return route.sourceIndex >= 0 && route.sourceIndex < kPhysicalAxisCount
                ? physicalAxisLabel(static_cast<PhysicalAxis>(route.sourceIndex)) : u"Unknown axis"_qs;
        case SignalFlowPortKind::Button:
            if (scopeMapping && route.sourceIndex >= 0
                && route.sourceIndex < static_cast<int>(scopeMapping->buttons.size())
                && !scopeMapping->buttons[static_cast<size_t>(route.sourceIndex)].customName.trimmed().isEmpty()) {
                return scopeMapping->buttons[static_cast<size_t>(route.sourceIndex)].customName.trimmed();
            }
            return QString(u"Button %1"_qs).arg(route.sourceIndex + 1);
        case SignalFlowPortKind::PovDirection:
            return QString(u"POV %1 %2"_qs).arg(route.sourceIndex + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(route.sourceSubIndex + 1)));
        case SignalFlowPortKind::NativePov:
            return QString(u"Native POV %1"_qs).arg(route.sourceIndex + 1);
        }
        return QString{};
    };
    const auto routeDestinationLabel = [&profile](const SignalFlowRoute &route) {
        switch (route.destinationKind) {
        case SignalFlowPortKind::Axis: {
            const QString alias = route.destinationIndex > 0 && route.destinationIndex < kVirtualAxisSlotCount
                ? profile.virtualAxisAliases[static_cast<size_t>(route.destinationIndex)].trimmed() : QString{};
            return alias.isEmpty() ? virtualAxisLabel(static_cast<VirtualAxis>(route.destinationIndex)) : alias;
        }
        case SignalFlowPortKind::Button:
            return QString(u"vJoy Button %1"_qs).arg(route.destinationIndex);
        case SignalFlowPortKind::PovDirection:
            return QString(u"vJoy POV %1 %2"_qs).arg(route.destinationIndex).arg(route.destinationSubIndex + 1);
        case SignalFlowPortKind::NativePov:
            return route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                ? QString(u"vJoy Continuous POV %1"_qs).arg(route.destinationIndex)
                : QString(u"vJoy Discrete POV %1"_qs).arg(route.destinationIndex);
        }
        return QString{};
    };
    for (int scopeIndex = 0; scopeIndex < secondaryControllerIds.size(); ++scopeIndex) {
        const QString scopeId = secondaryControllerIds.at(scopeIndex);
        const DeviceProfileMapping *scopeMapping = findDeviceProfileMapping(profile, scopeId);
        const SavedControllerRecord *scopeRecord = savedControllerRecord(scopeId);
        const QVariantMap scopeDetail = physicalDeviceDetail(scopeId);
        const bool scopeConnected = scopeDetail.value(u"connected"_qs).toBool();
        const bool scopeMissing = scopeRecord == nullptr;
        const QString scopeNodeId = QString(u"input:%1:%2"_qs).arg(profile.id, scopeId);
        QVariantList scopePorts;
        const auto scopeMapped = [this, &profile, &scopeId](SignalFlowPortKind kind, int index, int subIndex) {
            return std::any_of(m_configuration.signalFlow.routes.cbegin(), m_configuration.signalFlow.routes.cend(),
                [&profile, &scopeId, kind, index, subIndex](const SignalFlowRoute &route) {
                    return route.profileId == profile.id && route.controllerRecordId == scopeId && route.enabled
                        && route.sourceKind == kind && route.sourceIndex == index && route.sourceSubIndex == subIndex;
                });
        };
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            const AxisMapping *axisMapping = scopeMapping ? &scopeMapping->axes[static_cast<size_t>(axis)] : nullptr;
            const QString label = axisMapping && !axisMapping->customName.trimmed().isEmpty()
                ? axisMapping->customName.trimmed() : physicalAxisLabel(static_cast<PhysicalAxis>(axis));
            scopePorts.append(QVariantMap{{u"id"_qs, scopedSourcePortId(scopeId, SignalFlowPortKind::Axis, axis, -1)},
                {u"kind"_qs, u"axis"_qs}, {u"index"_qs, axis}, {u"group"_qs, u"Axes"_qs},
                {u"label"_qs, label}, {u"technicalLabel"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(axis))},
                {u"mapped"_qs, scopeMapped(SignalFlowPortKind::Axis, axis, -1)}, {u"available"_qs, true},
                {u"scopeEditable"_qs, false}});
        }
        const int scopeButtonCount = std::max(scopeMapping ? static_cast<int>(scopeMapping->buttons.size()) : 0,
                                              scopeRecord ? scopeRecord->buttonCount : kMaximumPhysicalButtons);
        for (int button = 0; button < std::max(scopeButtonCount, 1); ++button) {
            const ButtonBinding *binding = scopeMapping && button < static_cast<int>(scopeMapping->buttons.size())
                ? &scopeMapping->buttons[static_cast<size_t>(button)] : nullptr;
            const QString label = binding && !binding->customName.trimmed().isEmpty()
                ? binding->customName.trimmed() : QString(u"Button %1"_qs).arg(button + 1);
            scopePorts.append(QVariantMap{{u"id"_qs, scopedSourcePortId(scopeId, SignalFlowPortKind::Button, button, -1)},
                {u"kind"_qs, u"button"_qs}, {u"index"_qs, button}, {u"group"_qs, u"Buttons"_qs},
                {u"label"_qs, label}, {u"technicalLabel"_qs, QString(u"Button %1"_qs).arg(button + 1)},
                {u"mapped"_qs, scopeMapped(SignalFlowPortKind::Button, button, -1)}, {u"available"_qs, true},
                {u"scopeEditable"_qs, false}});
        }
        const int scopePovCount = std::max(scopeMapping ? static_cast<int>(scopeMapping->povs.size()) : 0,
                                           scopeRecord ? scopeRecord->povCount : 1);
        for (int hat = 0; hat < std::max(scopePovCount, 1); ++hat) {
            for (int direction = 0; direction < kPovDirectionCount; ++direction) {
                const QString label = QString(u"POV %1 %2"_qs).arg(hat + 1)
                    .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)));
                scopePorts.append(QVariantMap{{u"id"_qs, scopedSourcePortId(scopeId, SignalFlowPortKind::PovDirection, hat, direction)},
                    {u"kind"_qs, u"pov"_qs}, {u"index"_qs, hat}, {u"subIndex"_qs, direction},
                    {u"group"_qs, u"POV Directions"_qs}, {u"label"_qs, label}, {u"technicalLabel"_qs, label},
                    {u"mapped"_qs, scopeMapped(SignalFlowPortKind::PovDirection, hat, direction)},
                    {u"available"_qs, true}, {u"scopeEditable"_qs, false}});
            }
            scopePorts.append(QVariantMap{{u"id"_qs, scopedSourcePortId(scopeId, SignalFlowPortKind::NativePov, hat, -1)},
                {u"kind"_qs, u"native-pov"_qs}, {u"index"_qs, hat}, {u"group"_qs, u"Native POV"_qs},
                {u"label"_qs, QString(u"Native POV %1"_qs).arg(hat + 1)},
                {u"technicalLabel"_qs, QString(u"Native POV %1"_qs).arg(hat + 1)},
                {u"mapped"_qs, scopeMapped(SignalFlowPortKind::NativePov, hat, -1)}, {u"available"_qs, true},
                {u"scopeEditable"_qs, false}});
        }
        int scopeRouteCount = 0;
        for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
            if (route.profileId != profile.id || route.controllerRecordId != scopeId) continue;
            ++scopeRouteCount;
            QVariantList processors;
            QVariantList processorDetails;
            QString viaNodeId;
            float processorOrder = 0.0F;
            for (const QString &processorId : route.processorPath) {
                const QString nodeId = addProcessor(processorId, route.sourceIndex,
                    static_cast<float>(scopeIndex * 4) + processorOrder++);
                if (nodeId.isEmpty()) continue;
                processors.append(nodeId);
                const auto identityIt = std::find_if(m_configuration.signalFlow.processorIdentities.cbegin(),
                    m_configuration.signalFlow.processorIdentities.cend(), [&processorId](const SignalFlowIdentityRecord &candidate) {
                        return candidate.id == processorId;
                    });
                if (identityIt == m_configuration.signalFlow.processorIdentities.cend()) continue;
                const bool mixer = identityIt->key.startsWith(u"processor:mixer:"_qs);
                const QString semantic = mixer ? u"mixer"_qs : identityIt->key.section(u':', -1);
                if (mixer) viaNodeId = nodeId;
                QVariantMap detail{{u"id"_qs, processorId}, {u"semantic"_qs, semantic},
                    {u"sourceAxis"_qs, route.sourceIndex},
                    {u"settingsPage"_qs, semantic == u"curve"_qs ? 6
                        : semantic == u"adaptive-response"_qs ? 9 : -1},
                    {u"canOpenFullSettings"_qs, semantic == u"curve"_qs
                        || semantic == u"adaptive-response"_qs}};
                annotateProcessorSettings(&detail, semantic, scopeMapping, route.sourceIndex);
                if (identityIt->key.startsWith(u"processor:shared:"_qs)) {
                    const auto shared = std::find_if(m_configuration.signalFlow.sharedProcessors.cbegin(),
                        m_configuration.signalFlow.sharedProcessors.cend(), [&processorId](const SignalFlowSharedProcessor &candidate) {
                            return candidate.enabled && candidate.id == processorId;
                        });
                    if (shared != m_configuration.signalFlow.sharedProcessors.cend()) {
                        detail.insert(u"shared"_qs, true);
                        detail.insert(u"sharedChannelCount"_qs, static_cast<int>(shared->sourceAxes.size()));
                        detail.insert(u"sharedOwnerAxis"_qs, shared->ownerAxis);
                    }
                }
                processorDetails.append(std::move(detail));
            }
            bool destinationAvailable = layout != nullptr;
            if (layout && route.destinationKind == SignalFlowPortKind::Axis) {
                destinationAvailable = route.destinationIndex > 0 && route.destinationIndex < kVirtualAxisSlotCount
                    && layout->requirements.axes[static_cast<size_t>(route.destinationIndex)];
            } else if (layout && route.destinationKind == SignalFlowPortKind::Button) {
                destinationAvailable = route.destinationIndex > 0 && route.destinationIndex <= layout->requirements.buttons;
            } else if (layout && route.destinationKind == SignalFlowPortKind::NativePov) {
                const int capacity = route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                    ? layout->requirements.continuousPovs : layout->requirements.discretePovs;
                destinationAvailable = route.destinationIndex > 0 && route.destinationIndex <= capacity;
            }
            QString health = u"ready"_qs;
            QString healthDetail = u"Ready when Mapping Active is enabled."_qs;
            if (!route.enabled) {
                health = route.sourceKind == SignalFlowPortKind::Axis && route.destinationKind == SignalFlowPortKind::Axis
                    ? u"conflict"_qs : u"disabled"_qs;
                healthDetail = health == u"conflict"_qs
                    ? u"This analog route is disabled until its destination has an explicit Mixer or a single source."_qs
                    : u"This configured route is disabled and does not drive the virtual output."_qs;
            } else if (!scopeConnected) {
                health = u"source-offline"_qs;
                healthDetail = scopeMissing
                    ? u"The saved controller reference is missing; its route is preserved for resolution."_qs
                    : u"The saved physical input is offline; select this card to edit it without losing topology."_qs;
            } else if (!destinationAvailable) {
                health = u"destination-unavailable"_qs;
                healthDetail = u"The selected virtual output does not currently provide this destination."_qs;
            } else if (!mappingActive()) {
                health = u"mapping-off"_qs;
                healthDetail = u"The route is configured and ready; turn on Mapping Active to send output."_qs;
            }
            routes.append(QVariantMap{{u"id"_qs, route.id.isEmpty() ? route.identityKey : route.id},
                {u"kind"_qs, route.sourceKind == SignalFlowPortKind::Axis ? u"axis"_qs
                    : route.sourceKind == SignalFlowPortKind::Button ? u"button"_qs
                    : route.sourceKind == SignalFlowPortKind::PovDirection ? u"pov"_qs : u"native-pov"_qs},
                {u"sourceNodeId"_qs, scopeNodeId},
                {u"sourcePortId"_qs, scopedSourcePortId(scopeId, route.sourceKind, route.sourceIndex, route.sourceSubIndex)},
                {u"sourceIndex"_qs, route.sourceIndex}, {u"sourceSubIndex"_qs, route.sourceSubIndex},
                {u"sourceLabel"_qs, routeSourceLabel(route, scopeMapping)}, {u"destinationNodeId"_qs, outputNodeId},
                {u"destinationPortId"_qs, destinationPortId(route.destinationKind, route.destinationIndex, route.destinationSubIndex)},
                {u"destinationIndex"_qs, route.destinationIndex},
                {u"destinationSubIndex"_qs, route.destinationSubIndex},
                {u"destinationLabel"_qs, routeDestinationLabel(route)},
                {u"processors"_qs, processors}, {u"processorDetails"_qs, processorDetails}, {u"viaNodeId"_qs, viaNodeId},
                {u"configured"_qs, true}, {u"enabled"_qs, route.enabled},
                {u"effective"_qs, route.enabled && mappingActive() && scopeConnected && destinationAvailable && vjoyReady()},
                {u"primaryProjection"_qs, route.primaryProjection}, {u"health"_qs, health},
                {u"healthDetail"_qs, healthDetail}, {u"editable"_qs, false}, {u"controllerRecordId"_qs, scopeId}});
        }
        const QString scopeLabel = scopeRecord ? scopeRecord->displayName : QString(u"Missing controller · %1"_qs).arg(scopeId.left(20));
        const QString scopeDetailText = scopeMissing
            ? u"Missing imported/saved controller reference — Resolve in Devices; routes are preserved."_qs
            : scopeConnected ? QString(u"Connected · %1 routed endpoint%2 · select to edit this input"_qs)
                .arg(scopeRouteCount).arg(scopeRouteCount == 1 ? QString{} : u"s"_qs)
            : QString(u"Offline · %1 saved routed endpoint%2 · select to edit this input"_qs)
                .arg(scopeRouteCount).arg(scopeRouteCount == 1 ? QString{} : u"s"_qs);
        QVariantMap scopeNode{{u"id"_qs, scopeNodeId}, {u"objectId"_qs, scopeNodeId}, {u"kind"_qs, u"input"_qs},
            {u"label"_qs, scopeLabel}, {u"detail"_qs, scopeDetailText}, {u"missingReference"_qs, scopeMissing},
            {u"routeCount"_qs, scopeRouteCount}, {u"capabilitySummary"_qs, QString(u"%1 axes · %2 buttons · %3 POVs"_qs)
                .arg(kPhysicalAxisCount).arg(std::max(scopeButtonCount, 1)).arg(std::max(scopePovCount, 1))},
            {u"connected"_qs, scopeConnected}, {u"ports"_qs, scopePorts},
            {u"portGroups"_qs, portGroupsFor(scopeNodeId, false)}, {u"controllerRecordId"_qs, scopeId},
            {u"scopeEditable"_qs, false}};
        applyLayout(&scopeNode, layoutFor(scopeNodeId, 80.0F, 160.0F + (scopeIndex + 1) * 238.0F));
        nodes.append(scopeNode);
    }

    const int inputRouteCount = static_cast<int>(std::count_if(routes.cbegin(), routes.cend(),
        [&inputNodeId](const QVariant &entry) {
            const QVariantMap route = entry.toMap();
            return route.value(u"sourceNodeId"_qs).toString() == inputNodeId && route.value(u"enabled"_qs).toBool();
        }));
    const int outputRouteCount = static_cast<int>(std::count_if(routes.cbegin(), routes.cend(),
        [](const QVariant &entry) { return entry.toMap().value(u"enabled"_qs).toBool(); }));
    const QString inputDetail = inputMissing
        ? u"Missing saved controller reference — resolve it in Devices; preserved routes remain editable."_qs
        : inputConnected ? QString(u"Connected · %1 routed endpoint%2"_qs)
              .arg(inputRouteCount).arg(inputRouteCount == 1 ? QString{} : u"s"_qs)
        : QString(u"Offline · %1 saved routed endpoint%2 remain editable"_qs)
              .arg(inputRouteCount).arg(inputRouteCount == 1 ? QString{} : u"s"_qs);
    QVariantMap inputNode{{u"id"_qs, inputNodeId}, {u"objectId"_qs, inputNodeId},
                          {u"kind"_qs, u"input"_qs}, {u"label"_qs, inputLabel},
                          {u"detail"_qs, inputDetail}, {u"missingReference"_qs, inputMissing},
                          {u"routeCount"_qs, inputRouteCount},
                          {u"capabilitySummary"_qs, QString(u"%1 axes · %2 buttons · %3 POVs"_qs)
                              .arg(kPhysicalAxisCount).arg(inputButtonCount).arg(inputPovCount)},
                          {u"connected"_qs, inputConnected}, {u"ports"_qs, inputPorts},
                          {u"portGroups"_qs, portGroupsFor(inputNodeId, false)},
                          {u"controllerRecordId"_qs, controllerId},
                          {u"scopeEditable"_qs, editingMapping != nullptr || graphDeviceRig == nullptr}};
    applyLayout(&inputNode, layoutFor(inputNodeId, 80.0F, 160.0F));
    nodes.prepend(inputNode);
    QVariantMap outputNode{{u"id"_qs, outputNodeId}, {u"objectId"_qs, outputNodeId},
                           {u"kind"_qs, u"output"_qs},
                           {u"label"_qs, layout ? layout->name : u"Missing virtual output"_qs},
                           {u"detail"_qs, layout ? QString(u"vJoy Device %1 · %2 incoming route%3"_qs)
                                                   .arg(layout->requirements.deviceId).arg(outputRouteCount)
                                                   .arg(outputRouteCount == 1 ? QString{} : u"s"_qs)
                                                   : u"Missing virtual output reference — assign one in Profiles."_qs},
                           {u"missingReference"_qs, layout == nullptr},
                           {u"routeCount"_qs, outputRouteCount},
                           {u"capabilitySummary"_qs, QString(u"%1 virtual axes · %2 buttons · %3 POV targets"_qs)
                               .arg(kVirtualAxisSlotCount - 1).arg(buttonCapacity)
                               .arg((layout ? layout->requirements.continuousPovs : 0)
                                    + (layout ? layout->requirements.discretePovs : 0))},
                           {u"connected"_qs, vjoyReady()}, {u"ports"_qs, outputPorts},
                           {u"portGroups"_qs, portGroupsFor(outputNodeId, true)}};
    applyLayout(&outputNode, layoutFor(outputNodeId, 1380.0F, 160.0F));
    nodes.append(outputNode);

    QVariantMap workspace{{u"key"_qs, workspaceKey}, {u"panX"_qs, 0.0}, {u"panY"_qs, 0.0},
                          {u"zoom"_qs, 1.0}, {u"wireStyle"_qs, u"smooth"_qs},
                          {u"densityMode"_qs, u"detailed"_qs}, {u"inspectorWidth"_qs, 360},
                          {u"layoutLocked"_qs, false}};
    for (const SignalFlowWorkspaceState &savedWorkspace : m_configuration.signalFlow.workspaces) {
        if (savedWorkspace.key != workspaceKey) continue;
        workspace = {{u"key"_qs, savedWorkspace.key}, {u"panX"_qs, savedWorkspace.panX},
                     {u"panY"_qs, savedWorkspace.panY}, {u"zoom"_qs, savedWorkspace.zoom},
                     {u"wireStyle"_qs, savedWorkspace.wireStyle}, {u"densityMode"_qs, savedWorkspace.densityMode},
                     {u"inspectorWidth"_qs, savedWorkspace.inspectorWidth},
                     {u"layoutLocked"_qs, savedWorkspace.layoutLocked}};
        break;
    }
    return {{u"revision"_qs, QVariant::fromValue(m_configurationGeneration)},
            {u"profileId"_qs, profile.id}, {u"profileName"_qs, profile.name},
            {u"effectiveProfileId"_qs, effectiveProfileId()},
            {u"effectiveProfileName"_qs, effectiveProfileDisplayName()},
            {u"editingDiffersFromEffective"_qs, profile.id != effectiveProfileId()},
            {u"deviceRigId"_qs, graphDeviceRigId}, {u"deviceRigName"_qs, graphDeviceRigName},
            {u"controllerRecordId"_qs, controllerId}, {u"controllerLabel"_qs, inputLabel},
            {u"inputNodeId"_qs, inputNodeId}, {u"outputNodeId"_qs, outputNodeId},
            {u"nodes"_qs, nodes}, {u"routes"_qs, routes}, {u"workspace"_qs, workspace},
            {u"topologyVersion"_qs, m_configuration.signalFlow.topologyVersion},
            {u"configuredRouteCount"_qs, routes.size()},
            {u"effectiveSummary"_qs, mappingActive() && inputConnected && vjoyReady()
                ? u"Effective mapping is active for this scope."_qs
                : u"Configured routes are preserved; runtime output is currently inactive or unavailable."_qs},
            {u"editable"_qs, !findDeviceRig(m_configuration, m_configuration.editingDeviceRigId)
                || editingMapping != nullptr}};
}

QVariantMap AppBackend::signalFlowExplainRoute(const QString &routeId) const
{
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *mapping = editingDeviceMapping();
    const QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    const SignalFlowRoute *route = findSignalFlowRouteById(m_configuration.signalFlow, routeId.trimmed());
    if (!route || !signalFlowRouteMatchesScope(*route, profile, controllerId)) {
        return signalFlowActionResult(false, u"Route is no longer available"_qs,
            u"The selected route changed in another editor. Refresh Signal Flow and choose it again."_qs,
            routeId.trimmed());
    }

    const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
    const ButtonBindings &buttons = mapping ? mapping->buttons : profile.buttons;
    const auto sourceLabel = [&axes, &buttons, route] {
        switch (route->sourceKind) {
        case SignalFlowPortKind::Axis:
            if (route->sourceIndex >= 0 && route->sourceIndex < kPhysicalAxisCount) {
                const AxisMapping &axis = axes[static_cast<size_t>(route->sourceIndex)];
                return axis.customName.trimmed().isEmpty()
                    ? physicalAxisLabel(static_cast<PhysicalAxis>(route->sourceIndex))
                    : axis.customName.trimmed();
            }
            return u"Unknown axis"_qs;
        case SignalFlowPortKind::Button:
            if (route->sourceIndex >= 0 && route->sourceIndex < static_cast<int>(buttons.size())
                && !buttons[static_cast<size_t>(route->sourceIndex)].customName.trimmed().isEmpty()) {
                return buttons[static_cast<size_t>(route->sourceIndex)].customName.trimmed();
            }
            return QString(u"Button %1"_qs).arg(route->sourceIndex + 1);
        case SignalFlowPortKind::PovDirection:
            return QString(u"POV %1 %2"_qs).arg(route->sourceIndex + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(route->sourceSubIndex + 1)));
        case SignalFlowPortKind::NativePov:
            return QString(u"Native POV %1"_qs).arg(route->sourceIndex + 1);
        }
        return QString{};
    };
    const auto destinationLabel = [&profile, route] {
        switch (route->destinationKind) {
        case SignalFlowPortKind::Axis: {
            const QString alias = route->destinationIndex > 0
                    && route->destinationIndex < kVirtualAxisSlotCount
                ? profile.virtualAxisAliases[static_cast<size_t>(route->destinationIndex)].trimmed()
                : QString{};
            return alias.isEmpty() ? virtualAxisLabel(static_cast<VirtualAxis>(route->destinationIndex)) : alias;
        }
        case SignalFlowPortKind::Button:
            return QString(u"vJoy Button %1"_qs).arg(route->destinationIndex);
        case SignalFlowPortKind::PovDirection:
            return QString(u"vJoy POV %1 %2"_qs).arg(route->destinationIndex)
                .arg(route->destinationSubIndex + 1);
        case SignalFlowPortKind::NativePov:
            return route->destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                ? QString(u"vJoy Continuous POV %1"_qs).arg(route->destinationIndex)
                : QString(u"vJoy Discrete POV %1"_qs).arg(route->destinationIndex);
        }
        return QString{};
    };
    const auto processorLabel = [](const QString &semantic) {
        if (semantic == u"domain"_qs) return u"One-sided domain"_qs;
        if (semantic == u"deadzone"_qs) return u"Deadzone"_qs;
        if (semantic == u"center-hold"_qs) return u"Center hold"_qs;
        if (semantic == u"invert"_qs) return u"Invert"_qs;
        if (semantic == u"curve"_qs) return u"Response curve"_qs;
        if (semantic == u"limits"_qs) return u"Output limits"_qs;
        if (semantic == u"adaptive-response"_qs) return u"Adaptive Response"_qs;
        if (semantic == u"mixer"_qs) return u"Explicit mixer"_qs;
        return u"Signal conditioner"_qs;
    };

    QVariantList steps;
    steps.append(QVariantMap{{u"kind"_qs, u"source"_qs}, {u"label"_qs, sourceLabel()},
                             {u"detail"_qs, u"Physical input sampled by the mapper."_qs}});
    for (const QString &processorId : route->processorPath) {
        const auto identity = std::find_if(m_configuration.signalFlow.processorIdentities.cbegin(),
            m_configuration.signalFlow.processorIdentities.cend(),
            [&processorId](const SignalFlowIdentityRecord &candidate) { return candidate.id == processorId; });
        if (identity == m_configuration.signalFlow.processorIdentities.cend() || !identity->active) continue;
        const QString semantic = identity->key.startsWith(u"processor:mixer:"_qs)
            ? u"mixer"_qs : identity->key.section(u':', -1);
        QString detail = u"Uses the same persisted setting shown in the focused editor."_qs;
        if (semantic == u"mixer"_qs) {
            const auto mixer = std::find_if(m_configuration.signalFlow.mixers.cbegin(),
                m_configuration.signalFlow.mixers.cend(), [route](const SignalFlowMixer &candidate) {
                    return candidate.profileId == route->profileId
                        && candidate.controllerRecordId == route->controllerRecordId
                        && candidate.destinationAxis == route->destinationIndex;
                });
            if (mixer != m_configuration.signalFlow.mixers.cend()) {
                switch (mixer->mode) {
                case SignalFlowMixerMode::Average: detail = u"Average combines the participating analog inputs."_qs; break;
                case SignalFlowMixerMode::SumClamped: detail = u"Sum Clamped adds the inputs, then clamps to the output range."_qs; break;
                case SignalFlowMixerMode::HighestMagnitude: detail = u"Highest Magnitude forwards the input furthest from neutral."_qs; break;
                case SignalFlowMixerMode::Disabled: break;
                }
            }
        } else if (identity->key.startsWith(u"processor:shared:"_qs)) {
            const auto shared = std::find_if(m_configuration.signalFlow.sharedProcessors.cbegin(),
                m_configuration.signalFlow.sharedProcessors.cend(),
                [&processorId](const SignalFlowSharedProcessor &candidate) {
                    return candidate.enabled && candidate.id == processorId;
                });
            if (shared != m_configuration.signalFlow.sharedProcessors.cend()) {
                detail = QString(u"One durable %1 setting is mirrored to %2 routed source axes; split this route before editing it locally."_qs)
                    .arg(processorLabel(semantic)).arg(shared->sourceAxes.size());
            }
        }
        steps.append(QVariantMap{{u"kind"_qs, semantic}, {u"id"_qs, processorId},
                                 {u"label"_qs, processorLabel(semantic)}, {u"detail"_qs, detail}});
    }
    steps.append(QVariantMap{{u"kind"_qs, u"destination"_qs}, {u"label"_qs, destinationLabel()},
                             {u"detail"_qs, u"Virtual output endpoint."_qs}});

    const bool inputOnline = controllerId.isEmpty() ? physicalConnected()
        : physicalDeviceDetail(controllerId).value(u"connected"_qs).toBool();
    const bool outputReady = vjoyReady();
    const bool runtimeActive = mappingActive();
    const bool effective = route->enabled && inputOnline && outputReady && runtimeActive;
    QString runtimeDetail;
    if (!route->enabled) runtimeDetail = u"Configured route is disabled."_qs;
    else if (!inputOnline) runtimeDetail = u"Configured route is waiting for its saved physical input."_qs;
    else if (!outputReady) runtimeDetail = u"Configured route is waiting for its virtual output."_qs;
    else if (!runtimeActive) runtimeDetail = u"Configured route is ready but Mapping Active is off."_qs;
    else runtimeDetail = u"Configured route is currently effective at runtime."_qs;
    const int processorCount = std::max(0, static_cast<int>(steps.size()) - 2);

    return {{u"success"_qs, true}, {u"routeId"_qs, route->id},
            {u"source"_qs, sourceLabel()}, {u"destination"_qs, destinationLabel()},
            {u"steps"_qs, steps}, {u"configured"_qs, route->enabled},
            {u"effective"_qs, effective}, {u"summary"_qs,
                QString(u"%1 flows to %2 through %3 visible processor%4."_qs)
                    .arg(sourceLabel(), destinationLabel())
                    .arg(processorCount)
                    .arg(processorCount == 1 ? QString{} : u"s"_qs)},
            {u"runtimeDetail"_qs, runtimeDetail},
            {u"technicalDetails"_qs,
                QString(u"ROUTE ID\n%1\n\nSOURCE\n%2\n\nDESTINATION\n%3\n\nCONFIGURED\n%4\nEFFECTIVE\n%5"_qs)
                    .arg(route->id, sourceLabel(), destinationLabel(), route->enabled ? u"yes"_qs : u"no"_qs,
                         effective ? u"yes"_qs : u"no"_qs)},
            {u"revision"_qs, QVariant::fromValue(m_configurationGeneration)}};
}

QVariantMap AppBackend::signalFlowLiveTelemetry() const
{
    // A deliberately bounded UI-side sample. MappingWorker only writes the
    // fixed atomics below; it never traverses graph state, allocates QVariant
    // containers, signals QML, or logs a Signal Flow update per report.
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *mapping = editingDeviceMapping();
    const QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    const AtomicRuntimeState &runtime = m_worker.runtime();
    QVariantList axes;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const size_t index = static_cast<size_t>(axis);
        const float transformed = runtime.transformed[index].load(std::memory_order_relaxed);
        const float output = runtime.virtualValues[index].load(std::memory_order_relaxed);
        axes.append(QVariantMap{{u"sourcePortId"_qs, QString(u"axis:%1"_qs).arg(axis)},
                                {u"axis"_qs, axis},
                                {u"available"_qs, runtime.axisAvailable[index].load(std::memory_order_relaxed)},
                                {u"raw"_qs, runtime.raw[index].load(std::memory_order_relaxed)},
                                {u"normalized"_qs, runtime.normalized[index].load(std::memory_order_relaxed)},
                                {u"afterDeadzone"_qs, runtime.afterDeadzone[index].load(std::memory_order_relaxed)},
                                {u"afterHysteresis"_qs, runtime.afterHysteresis[index].load(std::memory_order_relaxed)},
                                {u"afterInversion"_qs, runtime.afterInversion[index].load(std::memory_order_relaxed)},
                                {u"curve"_qs, runtime.curveResponse[index].load(std::memory_order_relaxed)},
                                {u"value"_qs, transformed}, {u"output"_qs, output},
                                {u"active"_qs, std::isfinite(transformed) && std::abs(transformed) >= 0.015F}});
    }
    QVariantList routes;
    for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
        if (!signalFlowRouteMatchesScope(route, profile, controllerId)) continue;
        float value = 0.0F;
        bool active = false;
        if (route.sourceKind == SignalFlowPortKind::Axis && route.sourceIndex >= 0
            && route.sourceIndex < kPhysicalAxisCount) {
            value = runtime.transformed[static_cast<size_t>(route.sourceIndex)].load(std::memory_order_relaxed);
            active = std::isfinite(value) && std::abs(value) >= 0.015F;
        } else if (route.sourceKind == SignalFlowPortKind::Button && route.sourceIndex >= 0
                   && route.sourceIndex < kMaximumPhysicalButtons) {
            active = runtime.physicalButtonPressed[static_cast<size_t>(route.sourceIndex)].load(std::memory_order_relaxed);
            value = active ? 1.0F : 0.0F;
        } else if ((route.sourceKind == SignalFlowPortKind::PovDirection
                    || route.sourceKind == SignalFlowPortKind::NativePov)
                   && route.sourceIndex >= 0 && route.sourceIndex < kMaximumPhysicalPovs) {
            const int raw = runtime.povValues[static_cast<size_t>(route.sourceIndex)].load(std::memory_order_relaxed);
            if (route.sourceKind == SignalFlowPortKind::PovDirection) {
                active = povDirectionFromRaw(raw) == static_cast<PovDirection>(route.sourceSubIndex + 1);
            } else {
                active = povDirectionFromRaw(raw) != PovDirection::Centered;
            }
            value = active ? 1.0F : 0.0F;
        }
        routes.append(QVariantMap{{u"id"_qs, route.id}, {u"sourcePortId"_qs,
                                   route.sourceKind == SignalFlowPortKind::Axis
                                       ? QString(u"axis:%1"_qs).arg(route.sourceIndex)
                                       : route.sourceKind == SignalFlowPortKind::Button
                                           ? QString(u"button:%1"_qs).arg(route.sourceIndex)
                                           : route.sourceKind == SignalFlowPortKind::PovDirection
                                               ? QString(u"pov:%1:%2"_qs).arg(route.sourceIndex).arg(route.sourceSubIndex)
                                               : QString(u"native-pov:%1"_qs).arg(route.sourceIndex)},
                                   {u"value"_qs, value}, {u"active"_qs, active && route.enabled}});
    }
    const bool inputOnline = controllerId.isEmpty() ? physicalConnected()
        : physicalDeviceDetail(controllerId).value(u"connected"_qs).toBool();
    return {{u"sampledAtMs"_qs, QDateTime::currentMSecsSinceEpoch()}, {u"axes"_qs, axes},
            {u"routes"_qs, routes}, {u"inputOnline"_qs, inputOnline},
            {u"outputReady"_qs, vjoyReady()}, {u"mappingActive"_qs, mappingActive()}};
}

QVariantMap AppBackend::signalFlowActionResult(bool success, const QString &title,
                                               const QString &message, const QString &objectId) const
{
    QVariantMap result = actionResult(success, title, message, u"signalFlow"_qs, objectId);
    result.insert(u"revision"_qs, QVariant::fromValue(m_configurationGeneration));
    result.insert(u"canUndo"_qs, signalFlowCanUndo());
    result.insert(u"canRedo"_qs, signalFlowCanRedo());
    return result;
}

bool AppBackend::commitSignalFlowCommand(MapperConfiguration before, const QString &description)
{
    // Graph commands change canonical topology directly. Rebuild the focused
    // axis/button/POV projection before normal persistence so the universal
    // reconciliation path observes the graph's primary legs rather than
    // treating stale compatibility fields as a later user edit.
    projectSignalFlowTopologyToFocusedEditors(&m_configuration);
    m_signalFlowCommandInFlight = true;
    persistAndApply();
    m_signalFlowCommandInFlight = false;
    SignalFlowCommand command;
    command.before = std::move(before);
    command.after = m_configuration;
    command.undoRevision = m_configurationGeneration;
    command.description = description;
    m_signalFlowUndo.push_back(std::move(command));
    if (m_signalFlowUndo.size() > 64) m_signalFlowUndo.erase(m_signalFlowUndo.begin());
    m_signalFlowRedo.clear();
    m_signalFlowActionFeedback = description + u" — Undo is available."_qs;
    emit signalFlowChanged();
    return true;
}

QVariantMap AppBackend::signalFlowConnect(const QString &sourceKind, int sourceIndex,
                                          int sourceSubIndex, const QString &destination,
                                          bool replaceConflicts, qulonglong expectedRevision)
{
    return signalFlowConnectInternal(sourceKind, sourceIndex, sourceSubIndex, destination,
                                     replaceConflicts, {}, expectedRevision);
}

QVariantMap AppBackend::signalFlowConnectWithMixer(const QString &sourceKind, int sourceIndex,
                                                   int sourceSubIndex, const QString &destination,
                                                   const QString &mixerMode,
                                                   qulonglong expectedRevision)
{
    return signalFlowConnectInternal(sourceKind, sourceIndex, sourceSubIndex, destination,
                                     false, mixerMode, expectedRevision);
}

QVariantMap AppBackend::signalFlowConnectInternal(const QString &sourceKind, int sourceIndex,
                                                  int sourceSubIndex, const QString &destination,
                                                  bool replaceConflicts, const QString &mixerMode,
                                                  qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        m_signalFlowActionFeedback = u"The graph changed while this connection was being prepared. Review the current route and retry."_qs;
        emit signalFlowChanged();
        return signalFlowActionResult(false, u"Connection was not applied"_qs,
            m_signalFlowActionFeedback);
    }
    const QString kind = sourceKind.trimmed().toLower();
    // The canonical runtime has physical DirectInput endpoints only. Virtual
    // outputs are sinks, never sources, so reject every attempted vJoy/virtual
    // feedback leg before the transaction can mutate topology. This makes the
    // current source-domain graph structurally acyclic and leaves no override
    // path for direct or indirect virtual input/output loops.
    const bool virtualFeedbackSource = kind == u"virtual-output"_qs
        || kind.startsWith(u"virtual-"_qs) || kind.startsWith(u"virtual"_qs)
        || kind.startsWith(u"vjoy-"_qs) || kind.startsWith(u"vjoy"_qs)
        || kind == u"vjoy"_qs;
    if (virtualFeedbackSource) {
        return signalFlowActionResult(false, u"Virtual feedback is blocked"_qs,
            u"Signal Flow accepts physical DirectInput sources only. Virtual vJoy input/output feedback cannot form a direct or indirect signal cycle."_qs);
    }
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"This Device Rig contains multiple selected inputs. Select one source before creating a route."_qs);
    }
    ControllerProfile &profile = currentProfile();
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    VirtualOutputLayout *layout = activeOutputLayout();
    if (!layout) {
        return signalFlowActionResult(false, u"Virtual output is unavailable"_qs,
            u"Assign a Virtual Output to this profile before creating a Signal Flow connection."_qs);
    }
    // The graph always mutates durable topology first.  A pre-topology
    // in-memory fixture can still arrive here from a test or legacy import;
    // import its focused fields once before taking the undo snapshot.
    reconcileSignalFlowState(&m_configuration);
    MapperConfiguration before = m_configuration;
    QString description;
    SignalFlowState &topology = m_configuration.signalFlow;
    const auto inScope = [&profile, &controllerId](const SignalFlowRoute &route) {
        return route.profileId == profile.id && route.controllerRecordId == controllerId;
    };
    const auto sameSource = [&inScope](const SignalFlowRoute &route, SignalFlowPortKind source,
                                       int index, int subIndex) {
        return inScope(route) && route.sourceKind == source && route.sourceIndex == index
            && route.sourceSubIndex == subIndex;
    };
    const auto eraseSource = [&topology, &sameSource](SignalFlowPortKind source, int index, int subIndex) {
        topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
            [&sameSource, source, index, subIndex](const SignalFlowRoute &route) {
                return sameSource(route, source, index, subIndex);
            }), topology.routes.end());
    };
    const auto hasPrimary = [&topology, &sameSource](SignalFlowPortKind source, int index, int subIndex) {
        return std::any_of(topology.routes.cbegin(), topology.routes.cend(),
            [&sameSource, source, index, subIndex](const SignalFlowRoute &route) {
                return route.primaryProjection && sameSource(route, source, index, subIndex);
            });
    };

    if (kind == u"axis"_qs) {
        if (sourceIndex < 0 || sourceIndex >= kPhysicalAxisCount) {
            return signalFlowActionResult(false, u"Axis source is unavailable"_qs,
                u"The source axis no longer exists in this mapping context."_qs);
        }
        const VirtualAxis target = virtualAxisFromString(destination);
        const int targetIndex = static_cast<int>(target);
        if (target == VirtualAxis::Disabled || targetIndex < 1 || targetIndex >= kVirtualAxisSlotCount) {
            return signalFlowActionResult(false, u"Choose a virtual axis"_qs,
                u"Axis routes can only connect to a valid vJoy axis."_qs);
        }
        if (m_configuration.axisActivity[static_cast<size_t>(sourceIndex)] == PhysicalAxisActivity::Fixed) {
            return signalFlowActionResult(false, u"This axis is marked inactive"_qs,
                u"Complete a calibration with meaningful travel before routing this fixed descriptor axis."_qs);
        }
        SignalFlowMixerMode requestedMixer = SignalFlowMixerMode::Disabled;
        const QString normalizedMixer = mixerMode.trimmed().toLower();
        if (!normalizedMixer.isEmpty()) {
            if (normalizedMixer == u"average"_qs) requestedMixer = SignalFlowMixerMode::Average;
            else if (normalizedMixer == u"sum-clamped"_qs) requestedMixer = SignalFlowMixerMode::SumClamped;
            else if (normalizedMixer == u"highest-magnitude"_qs) requestedMixer = SignalFlowMixerMode::HighestMagnitude;
            else return signalFlowActionResult(false, u"Choose a valid mixer mode"_qs,
                u"Use Average, Sum Clamped, or Highest Magnitude for an explicit analog merge."_qs);
        }
        const auto exact = std::find_if(topology.routes.cbegin(), topology.routes.cend(),
            [&sameSource, sourceIndex, targetIndex](const SignalFlowRoute &route) {
                return route.enabled && sameSource(route, SignalFlowPortKind::Axis, sourceIndex, -1)
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.destinationIndex == targetIndex;
            });
        if (exact != topology.routes.cend()) {
            return signalFlowActionResult(true, u"Connection already present"_qs,
                u"This axis route is already part of the canonical Signal Flow topology."_qs, exact->id);
        }
        const auto existingMixer = std::find_if(topology.mixers.begin(), topology.mixers.end(),
            [&profile, &controllerId, targetIndex](const SignalFlowMixer &mixer) {
                return mixer.profileId == profile.id && mixer.controllerRecordId == controllerId
                    && mixer.destinationAxis == targetIndex;
            });
        const bool mixerActive = existingMixer != topology.mixers.end() && existingMixer->enabled
            && existingMixer->mode != SignalFlowMixerMode::Disabled;
        const bool anotherAnalogInput = std::any_of(topology.routes.cbegin(), topology.routes.cend(),
            [&inScope, sourceIndex, targetIndex](const SignalFlowRoute &route) {
                return route.enabled && inScope(route) && route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.destinationIndex == targetIndex && route.sourceIndex != sourceIndex;
            });
        if (anotherAnalogInput && requestedMixer == SignalFlowMixerMode::Disabled && !mixerActive
            && !replaceConflicts) {
            return signalFlowActionResult(false, u"Analog merge needs an explicit decision"_qs,
                u"That virtual axis already has another source. Choose Replace or Mixer; Signal Flow never creates an implicit analog merge."_qs);
        }
        if (replaceConflicts) {
            topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
                [&inScope, sourceIndex, targetIndex](const SignalFlowRoute &route) {
                    return inScope(route) && route.sourceKind == SignalFlowPortKind::Axis
                        && route.destinationKind == SignalFlowPortKind::Axis
                        && route.destinationIndex == targetIndex && route.sourceIndex != sourceIndex;
                }), topology.routes.end());
            topology.mixers.erase(std::remove_if(topology.mixers.begin(), topology.mixers.end(),
                [&profile, &controllerId, targetIndex](const SignalFlowMixer &mixer) {
                    return mixer.profileId == profile.id && mixer.controllerRecordId == controllerId
                        && mixer.destinationAxis == targetIndex;
                }), topology.mixers.end());
        } else if (requestedMixer != SignalFlowMixerMode::Disabled) {
            if (existingMixer != topology.mixers.end()) {
                existingMixer->mode = requestedMixer;
                existingMixer->enabled = true;
            } else {
                SignalFlowMixer mixer;
                mixer.profileId = profile.id;
                mixer.controllerRecordId = controllerId;
                mixer.destinationAxis = targetIndex;
                mixer.mode = requestedMixer;
                mixer.enabled = true;
                mixer.identityKey = signalFlowMixerIdentityKey(profile, controllerId, targetIndex);
                topology.mixers.push_back(std::move(mixer));
            }
        }
        layout->requirements.axes[static_cast<size_t>(targetIndex)] = true;
        SignalFlowRoute route;
        route.profileId = profile.id;
        route.controllerRecordId = controllerId;
        route.sourceKind = SignalFlowPortKind::Axis;
        route.sourceIndex = sourceIndex;
        route.sourceSubIndex = -1;
        route.destinationKind = SignalFlowPortKind::Axis;
        route.destinationIndex = targetIndex;
        route.destinationSubIndex = -1;
        route.primaryProjection = !hasPrimary(SignalFlowPortKind::Axis, sourceIndex, -1);
        route.enabled = true;
        route.identityKey = route.primaryProjection
            ? signalFlowRouteIdentityKey(profile, controllerId, u"axis"_qs, sourceIndex)
            : signalFlowFanoutRouteIdentityKey(profile, controllerId, u"axis"_qs, sourceIndex, -1,
                                                SignalFlowPortKind::Axis, targetIndex);
        topology.routes.push_back(std::move(route));
        description = requestedMixer == SignalFlowMixerMode::Disabled
            ? QString(u"Connected %1 to %2"_qs)
                  .arg(physicalAxisLabel(static_cast<PhysicalAxis>(sourceIndex)), virtualAxisLabel(target))
            : QString(u"Connected %1 to %2 through an explicit mixer"_qs)
                  .arg(physicalAxisLabel(static_cast<PhysicalAxis>(sourceIndex)), virtualAxisLabel(target));
    } else if (kind == u"button"_qs || kind == u"pov"_qs) {
        bool parsed = false;
        const int target = destination.trimmed().toInt(&parsed);
        if (!parsed || target < 1 || target > kMaximumVirtualButtons) {
            return signalFlowActionResult(false, u"Choose a virtual button"_qs,
                u"Button and POV routes must target a valid vJoy button number."_qs);
        }
        SignalFlowPortKind sourcePort = SignalFlowPortKind::Button;
        int normalizedSubIndex = -1;
        QString routeKind = u"button"_qs;
        if (kind == u"button"_qs) {
            if (sourceIndex < 0 || sourceIndex >= kMaximumPhysicalButtons) {
                return signalFlowActionResult(false, u"Button source is unavailable"_qs,
                    u"The selected physical button is outside the supported input range."_qs);
            }
        } else {
            if (sourceIndex < 0 || sourceIndex >= kMaximumPhysicalPovs
                || sourceSubIndex < 0 || sourceSubIndex >= kPovDirectionCount) {
                return signalFlowActionResult(false, u"POV source is unavailable"_qs,
                    u"Select a valid POV direction before creating a route."_qs);
            }
            sourcePort = SignalFlowPortKind::PovDirection;
            normalizedSubIndex = sourceSubIndex;
            routeKind = u"pov"_qs;
        }
        layout->requirements.buttons = std::max(layout->requirements.buttons, target);
        const auto exact = std::find_if(topology.routes.cbegin(), topology.routes.cend(),
            [&sameSource, sourcePort, sourceIndex, normalizedSubIndex, target](const SignalFlowRoute &route) {
                return route.enabled && sameSource(route, sourcePort, sourceIndex, normalizedSubIndex)
                    && route.destinationKind == SignalFlowPortKind::Button
                    && route.destinationIndex == target;
            });
        if (exact != topology.routes.cend()) {
            return signalFlowActionResult(true, u"Connection already present"_qs,
                u"This button route is already part of the canonical Signal Flow topology."_qs, exact->id);
        }
        // Digital fan-in and fan-out are both canonical runtime behaviors.
        // The first leg remains the focused-editor compatibility projection;
        // additional legs receive durable fan-out identities and compile into
        // the fixed report-path source buckets.
        SignalFlowRoute route;
        route.profileId = profile.id;
        route.controllerRecordId = controllerId;
        route.sourceKind = sourcePort;
        route.sourceIndex = sourceIndex;
        route.sourceSubIndex = normalizedSubIndex;
        route.destinationKind = SignalFlowPortKind::Button;
        route.destinationIndex = target;
        route.destinationSubIndex = -1;
        route.primaryProjection = !hasPrimary(sourcePort, sourceIndex, normalizedSubIndex);
        route.enabled = true;
        route.identityKey = route.primaryProjection
            ? signalFlowRouteIdentityKey(profile, controllerId, routeKind, sourceIndex, normalizedSubIndex)
            : signalFlowFanoutRouteIdentityKey(profile, controllerId, routeKind, sourceIndex,
                                                normalizedSubIndex, SignalFlowPortKind::Button, target);
        topology.routes.push_back(std::move(route));
        if (kind == u"button"_qs) {
            description = QString(u"Connected Button %1 to vJoy Button %2"_qs).arg(sourceIndex + 1).arg(target);
        } else {
            description = QString(u"Connected POV %1 %2 to vJoy Button %3"_qs).arg(sourceIndex + 1)
                .arg(povDirectionLabel(static_cast<PovDirection>(sourceSubIndex + 1))).arg(target);
        }
    } else if (kind == u"native-pov"_qs) {
        if (sourceIndex < 0 || sourceIndex >= kMaximumPhysicalPovs) {
            return signalFlowActionResult(false, u"Native POV source is unavailable"_qs,
                u"The selected physical POV is outside the supported input range."_qs);
        }
        const QStringList parts = destination.trimmed().split(u':');
        bool typeParsed = false;
        bool targetParsed = false;
        const int targetType = parts.size() == 3 && parts.front() == u"native-pov"_qs
            ? parts.at(1).toInt(&typeParsed) : 0;
        const int target = parts.size() == 3 ? parts.at(2).toInt(&targetParsed) : 0;
        if (!typeParsed || !targetParsed
            || (targetType != static_cast<int>(NativePovTargetType::Continuous)
                && targetType != static_cast<int>(NativePovTargetType::Discrete)) || target < 1) {
            return signalFlowActionResult(false, u"Choose a virtual POV destination"_qs,
                u"Native POV routes must target a valid continuous or discrete vJoy POV."_qs);
        }
        const int capacity = std::min(targetType == static_cast<int>(NativePovTargetType::Continuous)
            ? layout->requirements.continuousPovs : layout->requirements.discretePovs,
            kMaximumPhysicalPovs);
        if (target > capacity) {
            return signalFlowActionResult(false, u"Virtual POV destination is unavailable"_qs,
                u"Create or assign a virtual output with that POV capacity before connecting it."_qs);
        }
        const auto exact = std::find_if(topology.routes.cbegin(), topology.routes.cend(),
            [&sameSource, sourceIndex, target, targetType](const SignalFlowRoute &route) {
                return route.enabled && sameSource(route, SignalFlowPortKind::NativePov, sourceIndex, -1)
                    && route.destinationKind == SignalFlowPortKind::NativePov
                    && route.destinationIndex == target && route.destinationSubIndex == targetType;
            });
        if (exact != topology.routes.cend()) {
            return signalFlowActionResult(true, u"Connection already present"_qs,
                u"This native POV route is already part of the canonical Signal Flow topology."_qs, exact->id);
        }
        const auto destinationConflict = std::find_if(topology.routes.cbegin(), topology.routes.cend(),
            [&inScope, sourceIndex, target, targetType](const SignalFlowRoute &route) {
                return route.enabled && inScope(route) && route.sourceKind == SignalFlowPortKind::NativePov
                    && route.sourceIndex != sourceIndex && route.destinationKind == SignalFlowPortKind::NativePov
                    && route.destinationIndex == target && route.destinationSubIndex == targetType;
            });
        if (destinationConflict != topology.routes.cend() && !replaceConflicts) {
            return signalFlowActionResult(false, u"Virtual POV already has a source"_qs,
                u"That virtual POV already receives a physical POV stream. Replace it or choose a different destination; Signal Flow does not silently merge raw POV angles."_qs);
        }
        if (destinationConflict != topology.routes.cend()) {
            topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
                [&inScope, sourceIndex, target, targetType](const SignalFlowRoute &route) {
                    return inScope(route) && route.sourceKind == SignalFlowPortKind::NativePov
                        && route.sourceIndex != sourceIndex && route.destinationKind == SignalFlowPortKind::NativePov
                        && route.destinationIndex == target && route.destinationSubIndex == targetType;
                }), topology.routes.end());
        }
        SignalFlowRoute route;
        route.profileId = profile.id;
        route.controllerRecordId = controllerId;
        route.sourceKind = SignalFlowPortKind::NativePov;
        route.sourceIndex = sourceIndex;
        route.sourceSubIndex = -1;
        route.destinationKind = SignalFlowPortKind::NativePov;
        route.destinationIndex = target;
        route.destinationSubIndex = targetType;
        route.primaryProjection = !hasPrimary(SignalFlowPortKind::NativePov, sourceIndex, -1);
        route.enabled = true;
        route.identityKey = route.primaryProjection
            ? signalFlowRouteIdentityKey(profile, controllerId, u"native-pov"_qs, sourceIndex)
            : signalFlowFanoutRouteIdentityKey(profile, controllerId, u"native-pov"_qs, sourceIndex, -1,
                                                SignalFlowPortKind::NativePov, target, targetType);
        topology.routes.push_back(std::move(route));
        description = QString(u"Connected Native POV %1 to vJoy %2 POV %3"_qs).arg(sourceIndex + 1)
            .arg(targetType == static_cast<int>(NativePovTargetType::Continuous)
                ? u"Continuous"_qs : u"Discrete"_qs)
            .arg(target);
    } else {
        return signalFlowActionResult(false, u"Unsupported Signal Flow source"_qs,
            u"This source type is visible for inspection but cannot be connected by the current route command."_qs);
    }
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Connection applied"_qs, description);
}

QVariantMap AppBackend::signalFlowDisconnect(const QString &routeId, qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        m_signalFlowActionFeedback = u"The graph changed while this disconnect was pending. No route was changed."_qs;
        emit signalFlowChanged();
        return signalFlowActionResult(false, u"Disconnect was not applied"_qs, m_signalFlowActionFeedback, routeId);
    }
    const QString id = routeId.trimmed();
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"Select one source before changing a route in this multi-device Device Rig."_qs, id);
    }
    ControllerProfile &profile = currentProfile();
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    SignalFlowRoute *selected = findSignalFlowRouteById(&m_configuration.signalFlow, id);
    if (!selected || !signalFlowRouteMatchesScope(*selected, profile, controllerId)) {
        return signalFlowActionResult(false, u"Route is no longer available"_qs,
            u"The selected route changed in another editor. Refresh the graph and try again."_qs, id);
    }
    const SignalFlowPortKind sourceKind = selected->sourceKind;
    const int sourceIndex = selected->sourceIndex;
    const int sourceSubIndex = selected->sourceSubIndex;
    const SignalFlowPortKind destinationKind = selected->destinationKind;
    const int destinationIndex = selected->destinationIndex;
    MapperConfiguration before = m_configuration;
    SignalFlowState &topology = m_configuration.signalFlow;
    topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
        [&id](const SignalFlowRoute &route) { return route.id == id; }), topology.routes.end());

    // Preserve a usable one-target focused projection if a legacy-inexpressible
    // fan-out leg remains after the selected primary route was removed.
    SignalFlowRoute *replacement = nullptr;
    for (SignalFlowRoute &route : topology.routes) {
        if (!signalFlowRouteMatchesScope(route, profile, controllerId)
            || route.sourceKind != sourceKind || route.sourceIndex != sourceIndex
            || route.sourceSubIndex != sourceSubIndex) continue;
        if (!replacement || (!replacement->enabled && route.enabled)) replacement = &route;
        route.primaryProjection = false;
    }
    if (replacement) replacement->primaryProjection = true;

    // Button/POV absence means legacy default behavior, so explicitly clear a
    // just-disconnected source before the projection routine preserves it.
    if (!replacement) {
        if (sourceKind == SignalFlowPortKind::Button && sourceIndex >= 0) {
            ButtonBindings &buttons = deviceMapping ? deviceMapping->buttons : profile.buttons;
            if (buttons.size() <= static_cast<size_t>(sourceIndex)) {
                buttons.resize(static_cast<size_t>(sourceIndex + 1));
            }
            const QString customName = buttons[static_cast<size_t>(sourceIndex)].customName;
            buttons[static_cast<size_t>(sourceIndex)] = {};
            buttons[static_cast<size_t>(sourceIndex)].customName = customName;
            buttons[static_cast<size_t>(sourceIndex)].explicitlyConfigured = true;
        } else if (sourceKind == SignalFlowPortKind::PovDirection && sourceIndex >= 0
                   && sourceSubIndex >= 0 && sourceSubIndex < kPovDirectionCount) {
            PovBindings &povs = deviceMapping ? deviceMapping->povs : profile.povs;
            if (povs.size() <= static_cast<size_t>(sourceIndex)) {
                povs.resize(static_cast<size_t>(sourceIndex + 1));
            }
            ButtonBinding &binding = povs[static_cast<size_t>(sourceIndex)]
                [static_cast<size_t>(sourceSubIndex)];
            const QString customName = binding.customName;
            binding = {};
            binding.customName = customName;
            binding.explicitlyConfigured = true;
        } else if (sourceKind == SignalFlowPortKind::NativePov && sourceIndex >= 0) {
            NativePovBindings &nativePovs = deviceMapping ? deviceMapping->nativePovBindings
                                                           : m_configuration.nativePovBindings;
            if (nativePovs.size() <= static_cast<size_t>(sourceIndex)) {
                nativePovs.resize(static_cast<size_t>(sourceIndex + 1));
            }
            nativePovs[static_cast<size_t>(sourceIndex)] = {};
        }
    }
    if (destinationKind == SignalFlowPortKind::Axis) {
        const int remainingInputs = static_cast<int>(std::count_if(topology.routes.cbegin(),
            topology.routes.cend(), [&profile, &controllerId, destinationIndex](const SignalFlowRoute &route) {
                return route.enabled && route.profileId == profile.id
                    && route.controllerRecordId == controllerId
                    && route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.destinationIndex == destinationIndex;
            }));
        if (remainingInputs <= 1) {
            topology.mixers.erase(std::remove_if(topology.mixers.begin(), topology.mixers.end(),
                [&profile, &controllerId, destinationIndex](const SignalFlowMixer &mixer) {
                    return mixer.profileId == profile.id && mixer.controllerRecordId == controllerId
                        && mixer.destinationAxis == destinationIndex;
                }), topology.mixers.end());
        }
    }
    QString description;
    switch (sourceKind) {
    case SignalFlowPortKind::Axis:
        description = sourceIndex >= 0 && sourceIndex < kPhysicalAxisCount
            ? QString(u"Disconnected %1"_qs).arg(physicalAxisLabel(static_cast<PhysicalAxis>(sourceIndex)))
            : u"Disconnected axis route"_qs;
        break;
    case SignalFlowPortKind::Button:
        description = QString(u"Disconnected Button %1"_qs).arg(sourceIndex + 1);
        break;
    case SignalFlowPortKind::PovDirection:
        description = QString(u"Disconnected POV %1 %2"_qs).arg(sourceIndex + 1)
            .arg(povDirectionLabel(static_cast<PovDirection>(sourceSubIndex + 1)));
        break;
    case SignalFlowPortKind::NativePov:
        description = QString(u"Disconnected native POV %1"_qs).arg(sourceIndex + 1);
        break;
    }
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Route disconnected"_qs, description, id);
}

QVariantMap AppBackend::signalFlowToggleProcessor(const QString &routeId,
                                                   const QString &processorKind, bool enabled,
                                                   qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        return signalFlowActionResult(false, u"Processor change was not applied"_qs,
            u"The graph changed while this processor was being prepared. Review the current route and retry."_qs,
            routeId.trimmed());
    }
    const QString kind = processorKind.trimmed().toLower();
    const QSet<QString> supported{u"curve"_qs, u"deadzone"_qs, u"center-hold"_qs,
                                  u"invert"_qs, u"limits"_qs, u"adaptive-response"_qs};
    if (!supported.contains(kind)) {
        return signalFlowActionResult(false, u"Choose a supported processor"_qs,
            u"Signal Flow can add Curve, Deadzone, Center Hold, Invert, Output Limits, or Adaptive Response to an axis route."_qs,
            routeId.trimmed());
    }
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"Select one source before editing a processor in this multi-device Device Rig."_qs,
            routeId.trimmed());
    }
    reconcileSignalFlowState(&m_configuration);
    ControllerProfile &profile = currentProfile();
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    const SignalFlowRoute *route = findSignalFlowRouteById(m_configuration.signalFlow, routeId.trimmed());
    if (!route || !signalFlowRouteMatchesScope(*route, profile, controllerId)
        || route->sourceKind != SignalFlowPortKind::Axis || route->sourceIndex < 0
        || route->sourceIndex >= kPhysicalAxisCount) {
        return signalFlowActionResult(false, u"Processor target is unavailable"_qs,
            u"Choose a current axis route. Button and POV routes do not have an axis conditioning chain."_qs,
            routeId.trimmed());
    }
    const auto shared = std::find_if(m_configuration.signalFlow.sharedProcessors.cbegin(),
        m_configuration.signalFlow.sharedProcessors.cend(), [&profile, &controllerId, &kind, route](
            const SignalFlowSharedProcessor &processor) {
            return processor.enabled && processor.profileId == profile.id
                && processor.controllerRecordId == controllerId && processor.kind == kind
                && std::find(processor.sourceAxes.cbegin(), processor.sourceAxes.cend(),
                             route->sourceIndex) != processor.sourceAxes.cend();
        });
    if (shared != m_configuration.signalFlow.sharedProcessors.cend()) {
        return signalFlowActionResult(false, u"Shared processor needs an explicit split"_qs,
            u"This source uses a shared conditioner. Split it first to keep the other selected channels unchanged, then edit the local processor."_qs,
            routeId.trimmed());
    }
    MapperConfiguration before = m_configuration;
    AxisMapping &axis = (deviceMapping ? deviceMapping->axes : profile.axes)[static_cast<size_t>(route->sourceIndex)];
    AdaptiveResponseAxisOverride &adaptive = (deviceMapping ? deviceMapping->adaptiveResponse
                                                              : profile.adaptiveResponse)
        .axes[static_cast<size_t>(route->sourceIndex)];
    if (kind == u"curve"_qs) {
        axis.curve = enabled ? standardCurveDefinition(CurveFamily::SCurve, 0.50F)
                             : linearCurveDefinition();
    } else if (kind == u"deadzone"_qs) {
        axis.deadzone = enabled ? 0.08F : 0.03F;
    } else if (kind == u"center-hold"_qs) {
        axis.hysteresis = enabled ? 0.01F : 0.002F;
    } else if (kind == u"invert"_qs) {
        axis.inverted = enabled;
    } else if (kind == u"limits"_qs) {
        const bool oneSided = axis.rangeMode == AxisRangeMode::OneSided;
        const float minimum = enabled ? (oneSided ? 0.08F : -0.85F) : (oneSided ? 0.0F : -1.0F);
        const float maximum = enabled ? (oneSided ? 0.92F : 0.85F) : 1.0F;
        axis.outputMinimum = minimum;
        axis.outputMaximum = maximum;
        if (oneSided) {
            axis.oneSidedOutputMinimum = minimum;
            axis.oneSidedOutputMaximum = maximum;
        } else {
            axis.centeredOutputMinimum = minimum;
            axis.centeredOutputMaximum = maximum;
        }
    } else if (kind == u"adaptive-response"_qs) {
        adaptive.settings.enabled = enabled;
        adaptive.properties |= AdaptiveResponseEnabled;
    }
    const QString label = kind == u"center-hold"_qs ? u"Center Hold"_qs
        : kind == u"adaptive-response"_qs ? u"Adaptive Response"_qs
        : kind == u"limits"_qs ? u"Output Limits"_qs
        : kind.left(1).toUpper() + kind.mid(1);
    const QString description = QString(u"%1 %2 on %3"_qs).arg(enabled ? u"Added"_qs : u"Removed"_qs,
        label, physicalAxisLabel(static_cast<PhysicalAxis>(route->sourceIndex)));
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, enabled ? u"Processor added"_qs : u"Processor removed"_qs,
                                  description, routeId.trimmed());
}

QVariantMap AppBackend::signalFlowShareProcessor(const QStringList &routeIds,
                                                  const QString &processorKind,
                                                  qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        return signalFlowActionResult(false, u"Shared processor was not created"_qs,
            u"The graph changed while this shared processor was being prepared. Review the latest routes and retry."_qs);
    }
    const QString kind = processorKind.trimmed().toLower();
    const QSet<QString> supported{u"curve"_qs, u"deadzone"_qs, u"center-hold"_qs,
                                  u"invert"_qs, u"limits"_qs, u"adaptive-response"_qs};
    if (!supported.contains(kind)) {
        return signalFlowActionResult(false, u"Choose a supported processor"_qs,
            u"Only Curve, Deadzone, Center Hold, Invert, Output Limits, and Adaptive Response can be shared between axis sources."_qs);
    }
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"Select one source before sharing a processor in this multi-device Device Rig."_qs);
    }
    reconcileSignalFlowState(&m_configuration);
    ControllerProfile &profile = currentProfile();
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    if (routeIds.size() < 2 || routeIds.size() > kPhysicalAxisCount) {
        return signalFlowActionResult(false, u"Choose two or more axis routes"_qs,
            u"A shared processor needs two to eight distinct axis sources in the current Signal Flow scope."_qs);
    }
    QSet<QString> seenRouteIds;
    QSet<int> seenAxes;
    std::vector<int> sourceAxes;
    sourceAxes.reserve(static_cast<size_t>(routeIds.size()));
    int ownerAxis = -1;
    for (const QString &rawId : routeIds) {
        const QString id = rawId.trimmed();
        const SignalFlowRoute *route = findSignalFlowRouteById(m_configuration.signalFlow, id);
        if (id.isEmpty() || seenRouteIds.contains(id) || !route
            || !signalFlowRouteMatchesScope(*route, profile, controllerId)
            || !route->enabled || route->sourceKind != SignalFlowPortKind::Axis
            || route->sourceIndex < 0 || route->sourceIndex >= kPhysicalAxisCount) {
            return signalFlowActionResult(false, u"Selected route is unavailable"_qs,
                u"Choose distinct enabled axis routes from the current Signal Flow scope before sharing a processor."_qs, id);
        }
        if (seenAxes.contains(route->sourceIndex)) {
            return signalFlowActionResult(false, u"Choose one route per axis source"_qs,
                u"Fan-out legs from the same physical axis already share that source setting; select a different physical axis."_qs, id);
        }
        seenRouteIds.insert(id);
        seenAxes.insert(route->sourceIndex);
        if (ownerAxis < 0) ownerAxis = route->sourceIndex;
        sourceAxes.push_back(route->sourceIndex);
    }
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : profile.axes;
    const AxisMapping &owner = axes[static_cast<size_t>(ownerAxis)];
    const bool processorActive = kind == u"curve"_qs
        ? owner.curve.family != CurveFamily::Linear || owner.curve.pointEditing
            || std::abs(owner.curve.strength) > 0.0001F
        : kind == u"deadzone"_qs ? std::abs(owner.deadzone - 0.03F) > 0.0001F
        : kind == u"center-hold"_qs ? std::abs(owner.hysteresis - 0.002F) > 0.0001F
        : kind == u"invert"_qs ? owner.inverted
        : kind == u"limits"_qs
            ? std::abs(owner.outputMinimum - (owner.rangeMode == AxisRangeMode::OneSided ? 0.0F : -1.0F)) > 0.0001F
                || std::abs(owner.outputMaximum - 1.0F) > 0.0001F
        : m_configuration.adaptiveResponseGlobal.axes[static_cast<size_t>(ownerAxis)].settings.enabled
            || profile.adaptiveResponse.axes[static_cast<size_t>(ownerAxis)].settings.enabled
            || (deviceMapping && deviceMapping->adaptiveResponse.axes[static_cast<size_t>(ownerAxis)].settings.enabled);
    if (!processorActive) {
        const QString label = kind == u"center-hold"_qs ? u"Center Hold"_qs
            : kind == u"adaptive-response"_qs ? u"Adaptive Response"_qs
            : kind == u"limits"_qs ? u"Output Limits"_qs
            : kind.left(1).toUpper() + kind.mid(1);
        return signalFlowActionResult(false, u"Add the owner processor first"_qs,
            QString(u"Add or configure %1 on the first selected axis, then share that exact setting with the other routes."_qs)
                .arg(label));
    }
    MapperConfiguration before = m_configuration;
    SignalFlowState &topology = m_configuration.signalFlow;
    for (auto iterator = topology.sharedProcessors.begin();
         iterator != topology.sharedProcessors.end();) {
        if (iterator->profileId != profile.id || iterator->controllerRecordId != controllerId
            || iterator->kind != kind) {
            ++iterator;
            continue;
        }
        iterator->sourceAxes.erase(std::remove_if(iterator->sourceAxes.begin(), iterator->sourceAxes.end(),
            [&seenAxes](int axis) { return seenAxes.contains(axis); }), iterator->sourceAxes.end());
        if (iterator->sourceAxes.size() < 2) {
            iterator = topology.sharedProcessors.erase(iterator);
            continue;
        }
        if (std::find(iterator->sourceAxes.cbegin(), iterator->sourceAxes.cend(), iterator->ownerAxis)
            == iterator->sourceAxes.cend()) {
            iterator->ownerAxis = iterator->sourceAxes.front();
            iterator->identityKey = signalFlowSharedProcessorIdentityKey(
                profile, controllerId, kind, iterator->ownerAxis);
        }
        ++iterator;
    }
    if (topology.sharedProcessors.size() >= kMaximumSignalFlowSharedProcessors) {
        return signalFlowActionResult(false, u"Shared processor limit reached"_qs,
            u"This configuration has reached its bounded Signal Flow shared-processor limit."_qs);
    }
    SignalFlowSharedProcessor shared;
    shared.profileId = profile.id;
    shared.controllerRecordId = controllerId;
    shared.kind = kind;
    shared.ownerAxis = ownerAxis;
    shared.sourceAxes = std::move(sourceAxes);
    shared.enabled = true;
    shared.identityKey = signalFlowSharedProcessorIdentityKey(profile, controllerId, kind, ownerAxis);
    topology.sharedProcessors.push_back(std::move(shared));
    const QString label = kind == u"center-hold"_qs ? u"Center Hold"_qs
        : kind == u"adaptive-response"_qs ? u"Adaptive Response"_qs
        : kind == u"limits"_qs ? u"Output Limits"_qs
        : kind.left(1).toUpper() + kind.mid(1);
    const QString description = QString(u"Shared %1 across %2 axis sources"_qs)
        .arg(label).arg(seenAxes.size());
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Shared processor created"_qs, description,
                                  routeIds.front().trimmed());
}

QVariantMap AppBackend::signalFlowSplitSharedProcessor(const QString &routeId,
                                                       const QString &processorKind,
                                                       qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        return signalFlowActionResult(false, u"Shared processor was not split"_qs,
            u"The graph changed while this split was being prepared. Review the latest route and retry."_qs,
            routeId.trimmed());
    }
    const QString kind = processorKind.trimmed().toLower();
    const QSet<QString> supported{u"curve"_qs, u"deadzone"_qs, u"center-hold"_qs,
                                  u"invert"_qs, u"limits"_qs, u"adaptive-response"_qs};
    if (!supported.contains(kind)) {
        return signalFlowActionResult(false, u"Choose a supported processor"_qs,
            u"Only source-owned axis processors can be split from a shared Signal Flow object."_qs,
            routeId.trimmed());
    }
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"Select one source before changing a shared processor in this multi-device Device Rig."_qs,
            routeId.trimmed());
    }
    reconcileSignalFlowState(&m_configuration);
    ControllerProfile &profile = currentProfile();
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    const SignalFlowRoute *route = findSignalFlowRouteById(m_configuration.signalFlow, routeId.trimmed());
    if (!route || !signalFlowRouteMatchesScope(*route, profile, controllerId)
        || route->sourceKind != SignalFlowPortKind::Axis || route->sourceIndex < 0
        || route->sourceIndex >= kPhysicalAxisCount) {
        return signalFlowActionResult(false, u"Route is unavailable"_qs,
            u"Choose a current axis route before splitting a shared processor."_qs, routeId.trimmed());
    }
    SignalFlowState &topology = m_configuration.signalFlow;
    const auto shared = std::find_if(topology.sharedProcessors.cbegin(), topology.sharedProcessors.cend(),
        [&profile, &controllerId, &kind, route](const SignalFlowSharedProcessor &processor) {
            return processor.enabled && processor.profileId == profile.id
                && processor.controllerRecordId == controllerId && processor.kind == kind
                && std::find(processor.sourceAxes.cbegin(), processor.sourceAxes.cend(), route->sourceIndex)
                    != processor.sourceAxes.cend();
        });
    if (shared == topology.sharedProcessors.cend()) {
        return signalFlowActionResult(false, u"Processor is already local"_qs,
            u"This route does not currently use a shared processor of that kind."_qs, routeId.trimmed());
    }
    MapperConfiguration before = m_configuration;
    const int sourceAxis = route->sourceIndex;
    for (auto iterator = topology.sharedProcessors.begin();
         iterator != topology.sharedProcessors.end(); ++iterator) {
        if (iterator->identityKey != shared->identityKey) continue;
        iterator->sourceAxes.erase(std::remove(iterator->sourceAxes.begin(), iterator->sourceAxes.end(), sourceAxis),
                                   iterator->sourceAxes.end());
        if (iterator->sourceAxes.size() < 2) {
            topology.sharedProcessors.erase(iterator);
        } else if (iterator->ownerAxis == sourceAxis) {
            iterator->ownerAxis = iterator->sourceAxes.front();
            iterator->identityKey = signalFlowSharedProcessorIdentityKey(
                profile, controllerId, kind, iterator->ownerAxis);
        }
        break;
    }
    const QString label = kind == u"center-hold"_qs ? u"Center Hold"_qs
        : kind == u"adaptive-response"_qs ? u"Adaptive Response"_qs
        : kind == u"limits"_qs ? u"Output Limits"_qs
        : kind.left(1).toUpper() + kind.mid(1);
    const QString description = QString(u"Split %1 from %2"_qs)
        .arg(label, physicalAxisLabel(static_cast<PhysicalAxis>(sourceAxis)));
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Shared processor split"_qs,
                                  u"This axis retains its current settings as an independent local processor."_qs,
                                  routeId.trimmed());
}

QVariantMap AppBackend::signalFlowDefaultPreview(const QString &mode) const
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized != u"unassigned"_qs && normalized != u"replace-all"_qs) {
        return signalFlowActionResult(false, u"Unknown default mode"_qs,
            u"Choose Connect Unassigned Only or Replace All With Defaults."_qs);
    }
    const DeviceProfileMapping *mapping = editingDeviceMapping();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !mapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"Default Connections needs one explicit source in this multi-device Device Rig."_qs);
    }
    const ControllerProfile &profile = currentProfile();
    const QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    QVariantList changes;
    int actionableChanges = 0;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const VirtualAxis desired = static_cast<VirtualAxis>(axis + 1);
        QStringList existing;
        bool hasDesiredOnly = true;
        for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
            if (!route.enabled || route.profileId != profile.id || route.controllerRecordId != controllerId
                || route.sourceKind != SignalFlowPortKind::Axis || route.sourceIndex != axis
                || route.destinationKind != SignalFlowPortKind::Axis) continue;
            existing.append(virtualAxisLabel(static_cast<VirtualAxis>(route.destinationIndex)));
            hasDesiredOnly = hasDesiredOnly && route.destinationIndex == static_cast<int>(desired);
        }
        if (normalized == u"unassigned"_qs && !existing.isEmpty()) continue;
        if (normalized == u"replace-all"_qs && existing.size() == 1 && hasDesiredOnly) continue;
        const bool destinationBusy = normalized == u"unassigned"_qs && std::any_of(
            m_configuration.signalFlow.routes.cbegin(), m_configuration.signalFlow.routes.cend(),
            [&profile, &controllerId, axis](const SignalFlowRoute &route) {
                return route.enabled && route.profileId == profile.id
                    && route.controllerRecordId == controllerId
                    && route.sourceKind == SignalFlowPortKind::Axis && route.sourceIndex != axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.destinationIndex == axis + 1;
            });
        QVariantMap change{{u"source"_qs, physicalAxisLabel(static_cast<PhysicalAxis>(axis))},
                           {u"from"_qs, existing.isEmpty() ? u"Unassigned"_qs
                               : existing.join(u", "_qs)},
                           {u"to"_qs, virtualAxisLabel(desired)},
                           {u"blocked"_qs, destinationBusy}};
        if (destinationBusy) {
            change.insert(u"reason"_qs, u"Destination already has an analog source; create a mixer or use Replace All."_qs);
        } else {
            ++actionableChanges;
        }
        changes.append(std::move(change));
    }
    return {{u"success"_qs, true}, {u"mode"_qs, normalized}, {u"changes"_qs, changes},
            {u"count"_qs, actionableChanges}, {u"revision"_qs, QVariant::fromValue(m_configurationGeneration)},
            {u"message"_qs, normalized == u"unassigned"_qs
                ? u"Only unassigned axes with an unoccupied default destination will be connected 1:1."_qs
                : u"All axis destinations in this scope will be replaced by the 1:1 default map."_qs}};
}

QVariantMap AppBackend::signalFlowApplyDefaults(const QString &mode, qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        return signalFlowActionResult(false, u"Defaults were not applied"_qs,
            u"The graph changed after its preview. Reopen Default Connections to review the latest result."_qs);
    }
    const QVariantMap preview = signalFlowDefaultPreview(mode);
    if (!preview.value(u"success"_qs).toBool()) return preview;
    const QString normalized = preview.value(u"mode"_qs).toString();
    DeviceProfileMapping *mapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !mapping) return preview;
    VirtualOutputLayout *layout = activeOutputLayout();
    if (!layout) return signalFlowActionResult(false, u"Virtual output is unavailable"_qs,
        u"Assign a Virtual Output before applying default connections."_qs);
    ControllerProfile &profile = currentProfile();
    const QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    MapperConfiguration before = m_configuration;
    SignalFlowState &topology = m_configuration.signalFlow;
    if (normalized == u"replace-all"_qs) {
        topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
            [&profile, &controllerId](const SignalFlowRoute &route) {
                return route.profileId == profile.id && route.controllerRecordId == controllerId
                    && route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis;
            }), topology.routes.end());
        topology.mixers.erase(std::remove_if(topology.mixers.begin(), topology.mixers.end(),
            [&profile, &controllerId](const SignalFlowMixer &mixer) {
                return mixer.profileId == profile.id && mixer.controllerRecordId == controllerId;
            }), topology.mixers.end());
    }
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const bool assigned = std::any_of(topology.routes.cbegin(), topology.routes.cend(),
            [&profile, &controllerId, axis](const SignalFlowRoute &route) {
                return route.enabled && route.profileId == profile.id
                    && route.controllerRecordId == controllerId
                    && route.sourceKind == SignalFlowPortKind::Axis && route.sourceIndex == axis
                    && route.destinationKind == SignalFlowPortKind::Axis;
            });
        if (normalized == u"unassigned"_qs && assigned) continue;
        if (normalized == u"unassigned"_qs) {
            const bool destinationBusy = std::any_of(topology.routes.cbegin(), topology.routes.cend(),
                [&profile, &controllerId, axis](const SignalFlowRoute &route) {
                    return route.enabled && route.profileId == profile.id
                        && route.controllerRecordId == controllerId
                        && route.sourceKind == SignalFlowPortKind::Axis
                        && route.sourceIndex != axis && route.destinationKind == SignalFlowPortKind::Axis
                        && route.destinationIndex == axis + 1;
                });
            if (destinationBusy) continue;
        }
        SignalFlowRoute route;
        route.profileId = profile.id;
        route.controllerRecordId = controllerId;
        route.sourceKind = SignalFlowPortKind::Axis;
        route.sourceIndex = axis;
        route.sourceSubIndex = -1;
        route.destinationKind = SignalFlowPortKind::Axis;
        route.destinationIndex = axis + 1;
        route.destinationSubIndex = -1;
        route.primaryProjection = true;
        route.enabled = true;
        route.identityKey = signalFlowRouteIdentityKey(profile, controllerId, u"axis"_qs, axis);
        topology.routes.push_back(std::move(route));
        layout->requirements.axes[static_cast<size_t>(axis + 1)] = true;
    }
    if (preview.value(u"count"_qs).toInt() == 0) {
        return signalFlowActionResult(true, u"Defaults already match"_qs,
            u"No route changed in this Signal Flow scope."_qs);
    }
    const QString description = normalized == u"unassigned"_qs
        ? u"Connected unassigned axes using the default map"_qs
        : u"Replaced axis routes with the default map"_qs;
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Defaults applied"_qs, description);
}

QVariantMap AppBackend::signalFlowUndo(qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration || !signalFlowCanUndo()) {
        return signalFlowActionResult(false, u"Undo is no longer safe"_qs,
            u"A focused editor, profile change, import, or other canonical edit changed this graph. Signal Flow did not overwrite the newer state."_qs);
    }
    SignalFlowCommand command = std::move(m_signalFlowUndo.back());
    m_signalFlowUndo.pop_back();
    m_configuration = command.before;
    m_signalFlowCommandInFlight = true;
    persistAndApply();
    m_signalFlowCommandInFlight = false;
    command.redoRevision = m_configurationGeneration;
    m_signalFlowRedo.push_back(std::move(command));
    // We are now exactly at the `after` snapshot of the preceding undo
    // command. Persisting the reverted state legitimately advances the
    // global configuration generation, so carry that fresh precondition to
    // the next undo rather than treating our own successful undo as an
    // external edit conflict.
    if (!m_signalFlowUndo.empty()) {
        m_signalFlowUndo.back().undoRevision = m_configurationGeneration;
    }
    m_signalFlowActionFeedback = u"Signal Flow change undone."_qs;
    emit signalFlowChanged();
    return signalFlowActionResult(true, u"Undo applied"_qs, m_signalFlowActionFeedback);
}

QVariantMap AppBackend::signalFlowRedo(qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration || !signalFlowCanRedo()) {
        return signalFlowActionResult(false, u"Redo is no longer safe"_qs,
            u"Canonical routing changed after the undo, so Signal Flow did not replay an obsolete route change."_qs);
    }
    SignalFlowCommand command = std::move(m_signalFlowRedo.back());
    m_signalFlowRedo.pop_back();
    m_configuration = command.after;
    m_signalFlowCommandInFlight = true;
    persistAndApply();
    m_signalFlowCommandInFlight = false;
    command.undoRevision = m_configurationGeneration;
    m_signalFlowUndo.push_back(std::move(command));
    // Symmetric redo-chain rule: after restoring this command we are at the
    // `before` snapshot of the next redo command, with a new durable
    // generation caused by persistence. Keep that command usable while any
    // unrelated canonical edit still invalidates both stacks normally.
    if (!m_signalFlowRedo.empty()) {
        m_signalFlowRedo.back().redoRevision = m_configurationGeneration;
    }
    m_signalFlowActionFeedback = u"Signal Flow change restored."_qs;
    emit signalFlowChanged();
    return signalFlowActionResult(true, u"Redo applied"_qs, m_signalFlowActionFeedback);
}

bool AppBackend::saveSignalFlowPresentation()
{
    if (!ConfigStore::save(m_configuration)) {
        m_signalFlowActionFeedback = u"Signal Flow workspace could not be saved."_qs;
        emit signalFlowChanged();
        return false;
    }
    emit signalFlowChanged();
    return true;
}

bool AppBackend::signalFlowSaveWorkspace(const QVariantMap &workspace)
{
    SignalFlowWorkspaceState state;
    state.key = signalFlowWorkspaceKey();
    state.panX = std::clamp(static_cast<float>(workspace.value(u"panX"_qs, 0.0).toDouble()), -100000.0F, 100000.0F);
    state.panY = std::clamp(static_cast<float>(workspace.value(u"panY"_qs, 0.0).toDouble()), -100000.0F, 100000.0F);
    state.zoom = std::clamp(static_cast<float>(workspace.value(u"zoom"_qs, 1.0).toDouble()), 0.25F, 4.0F);
    state.wireStyle = workspace.value(u"wireStyle"_qs, u"smooth"_qs).toString().trimmed();
    state.densityMode = workspace.value(u"densityMode"_qs, u"detailed"_qs).toString().trimmed();
    state.inspectorWidth = std::clamp(workspace.value(u"inspectorWidth"_qs, 360).toInt(), 240, 720);
    state.layoutLocked = workspace.value(u"layoutLocked"_qs, false).toBool();
    if ((state.wireStyle != u"smooth"_qs && state.wireStyle != u"orthogonal"_qs)
        || (state.densityMode != u"detailed"_qs && state.densityMode != u"compact"_qs
            && state.densityMode != u"overview"_qs)) return false;
    for (SignalFlowWorkspaceState &existing : m_configuration.signalFlow.workspaces) {
        if (existing.key != state.key) continue;
        existing = std::move(state);
        return saveSignalFlowPresentation();
    }
    if (m_configuration.signalFlow.workspaces.size() >= kMaximumSignalFlowWorkspaces) return false;
    m_configuration.signalFlow.workspaces.push_back(std::move(state));
    return saveSignalFlowPresentation();
}

bool AppBackend::signalFlowSaveNodeLayout(const QString &objectId, double x, double y, bool pinned)
{
    const QString normalizedId = objectId.trimmed().left(96);
    if (normalizedId.isEmpty() || !std::isfinite(x) || !std::isfinite(y)) return false;
    const QString workspaceKey = signalFlowWorkspaceKey();
    SignalFlowNodeLayout state;
    state.workspaceKey = workspaceKey;
    state.objectId = normalizedId;
    state.x = std::clamp(static_cast<float>(x), -100000.0F, 100000.0F);
    state.y = std::clamp(static_cast<float>(y), -100000.0F, 100000.0F);
    state.pinned = pinned;
    for (SignalFlowNodeLayout &existing : m_configuration.signalFlow.nodeLayouts) {
        if (existing.workspaceKey != workspaceKey || existing.objectId != normalizedId) continue;
        existing = std::move(state);
        return saveSignalFlowPresentation();
    }
    if (m_configuration.signalFlow.nodeLayouts.size() >= kMaximumSignalFlowNodeLayouts) return false;
    m_configuration.signalFlow.nodeLayouts.push_back(std::move(state));
    return saveSignalFlowPresentation();
}

QVariantMap AppBackend::signalFlowSetPortGroupCollapsed(const QString &cardId,
                                                        const QString &group, bool collapsed)
{
    const QString card = cardId.trimmed().left(96);
    const QString label = group.trimmed().left(96);
    const QSet<QString> inputGroups{u"Axes"_qs, u"Buttons"_qs, u"POV Directions"_qs, u"Native POV"_qs};
    const QSet<QString> outputGroups{u"Virtual Axes"_qs, u"Virtual Buttons"_qs, u"Virtual POV"_qs};
    const bool inputCard = card.startsWith(u"input:"_qs);
    const bool outputCard = card.startsWith(u"output:"_qs);
    if (card.isEmpty() || (!inputCard && !outputCard)
        || (inputCard && !inputGroups.contains(label))
        || (outputCard && !outputGroups.contains(label))) {
        return signalFlowActionResult(false, u"Port group was not changed"_qs,
            u"That port group is no longer part of the current Signal Flow workspace."_qs, card);
    }
    const QString workspaceKey = signalFlowWorkspaceKey();
    for (SignalFlowPortGroupState &state : m_configuration.signalFlow.portGroups) {
        if (state.workspaceKey != workspaceKey || state.cardId != card || state.group != label) continue;
        state.collapsed = collapsed;
        m_signalFlowActionFeedback = QString(u"%1 is now %2."_qs).arg(label,
            collapsed ? u"collapsed"_qs : u"expanded"_qs);
        const bool saved = saveSignalFlowPresentation();
        return signalFlowActionResult(saved, saved ? u"Port group updated"_qs : u"Port group was not saved"_qs,
            saved ? m_signalFlowActionFeedback : u"Signal Flow could not save this port group state."_qs, card);
    }
    if (m_configuration.signalFlow.portGroups.size() >= kMaximumSignalFlowPortGroupStates) {
        return signalFlowActionResult(false, u"Port group was not saved"_qs,
            u"This configuration has reached its bounded Signal Flow port-group limit."_qs, card);
    }
    m_configuration.signalFlow.portGroups.push_back({workspaceKey, card, label, collapsed});
    m_signalFlowActionFeedback = QString(u"%1 is now %2."_qs).arg(label,
        collapsed ? u"collapsed"_qs : u"expanded"_qs);
    const bool saved = saveSignalFlowPresentation();
    return signalFlowActionResult(saved, saved ? u"Port group updated"_qs : u"Port group was not saved"_qs,
        saved ? m_signalFlowActionFeedback : u"Signal Flow could not save this port group state."_qs, card);
}

QVariantMap AppBackend::signalFlowAutoLayout()
{
    const ControllerProfile &profile = currentProfile();
    const DeviceProfileMapping *mapping = editingDeviceMapping();
    const QString controllerId = mapping ? mapping->controllerRecordId : QString{};
    const QString workspaceKey = signalFlowWorkspaceKey();
    const QString inputId = mapping ? QString(u"input:%1:%2"_qs).arg(profile.id, controllerId)
                                    : QString(u"input:%1:legacy"_qs).arg(profile.id);
    const VirtualOutputLayout *layout = activeOutputLayout();
    const QString outputId = layout ? QString(u"output:%1"_qs).arg(layout->id) : u"output:missing"_qs;
    const auto upsert = [this, &workspaceKey](const QString &objectId, float x, float y) {
        for (SignalFlowNodeLayout &entry : m_configuration.signalFlow.nodeLayouts) {
            if (entry.workspaceKey != workspaceKey || entry.objectId != objectId) continue;
            if (entry.pinned) return true;
            entry.x = x;
            entry.y = y;
            return true;
        }
        if (m_configuration.signalFlow.nodeLayouts.size() >= kMaximumSignalFlowNodeLayouts) return false;
        m_configuration.signalFlow.nodeLayouts.push_back({workspaceKey, objectId, x, y, false});
        return true;
    };
    if (!upsert(inputId, 80.0F, 160.0F) || !upsert(outputId, 1380.0F, 160.0F)) {
        return signalFlowActionResult(false, u"Auto-layout was not saved"_qs,
            u"This configuration has reached its bounded Signal Flow layout limit."_qs);
    }
    QSet<QString> uniqueProcessors;
    for (const SignalFlowRoute &route : m_configuration.signalFlow.routes) {
        if (!signalFlowRouteMatchesScope(route, profile, controllerId)) continue;
        for (const QString &processorId : route.processorPath) uniqueProcessors.insert(processorId);
    }
    QStringList processors = uniqueProcessors.values();
    processors.sort(Qt::CaseSensitive);
    for (int index = 0; index < processors.size(); ++index) {
        const int column = index % 4;
        const int row = index / 4;
        if (!upsert(processors.at(index), 380.0F + column * 220.0F, 105.0F + row * 126.0F)) {
            return signalFlowActionResult(false, u"Auto-layout was not saved"_qs,
                u"This configuration has reached its bounded Signal Flow layout limit."_qs);
        }
    }
    m_signalFlowActionFeedback = u"Signal Flow arranged the current scope with a bounded, stable layout."_qs;
    const bool saved = saveSignalFlowPresentation();
    return signalFlowActionResult(saved, saved ? u"Auto-layout applied"_qs : u"Auto-layout was not saved"_qs,
        saved ? m_signalFlowActionFeedback : u"Signal Flow could not save the requested layout."_qs);
}

QVariantMap AppBackend::inputLearning() const
{
    const auto kindName = [this] {
        switch (m_inputLearning.kind) {
        case InputLearningKind::Axis: return u"axis"_qs;
        case InputLearningKind::Button: return u"button"_qs;
        case InputLearningKind::Pov: return u"pov"_qs;
        case InputLearningKind::SignalFlowSource: return u"signal-flow"_qs;
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
    QString sourceKind;
    int sourceIndex = -1;
    int sourceSubIndex = -1;
    if (m_inputLearning.sourceAxis >= 0) {
        sourceKind = u"axis"_qs;
        sourceIndex = m_inputLearning.sourceAxis;
    } else if (m_inputLearning.sourceButton > 0) {
        sourceKind = u"button"_qs;
        sourceIndex = m_inputLearning.sourceButton - 1;
    } else if (m_inputLearning.sourcePovHat > 0
               && m_inputLearning.sourcePovDirection != PovDirection::Centered) {
        sourceKind = u"pov"_qs;
        sourceIndex = m_inputLearning.sourcePovHat - 1;
        sourceSubIndex = povDirectionIndex(m_inputLearning.sourcePovDirection);
    }
    QString sourcePortId;
    if (sourceKind == u"axis"_qs || sourceKind == u"button"_qs) {
        sourcePortId = QString(u"%1:%2"_qs).arg(sourceKind).arg(sourceIndex);
    } else if (sourceKind == u"pov"_qs && sourceSubIndex >= 0) {
        sourcePortId = QString(u"pov:%1:%2"_qs).arg(sourceIndex).arg(sourceSubIndex);
    }
    return {{u"active"_qs, m_inputLearning.kind != InputLearningKind::None},
        {u"kind"_qs, kindName()}, {u"phase"_qs, phaseName()},
        {u"target"_qs, m_inputLearning.target},
        {u"targetLabel"_qs, m_inputLearning.kind == InputLearningKind::Axis
            ? QString(u"vJoy %1"_qs).arg(m_inputLearning.target)
            : m_inputLearning.kind == InputLearningKind::SignalFlowSource
                ? u"Physical Signal Flow source"_qs
            : QString(u"vJoy Button %1"_qs).arg(m_inputLearning.virtualButton)},
        {u"sourceLabel"_qs, m_inputLearning.sourceLabel},
        {u"sourceKind"_qs, sourceKind}, {u"sourceIndex"_qs, sourceIndex},
        {u"sourceSubIndex"_qs, sourceSubIndex}, {u"sourcePortId"_qs, sourcePortId},
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

QVariantMap AppBackend::resolveAxisMappingConflict(int physicalAxis, const QString &target,
                                                   const QString &decision,
                                                   qulonglong expectedRevision)
{
    if (expectedRevision != m_configurationGeneration) {
        return signalFlowActionResult(false, u"Route decision was not applied"_qs,
            u"The routing changed while this decision was open. Review the current Signal Flow and choose again."_qs);
    }
    if (!validAxis(physicalAxis)) {
        return signalFlowActionResult(false, u"Axis route is unavailable"_qs,
            u"The selected physical axis is no longer part of this mapping context."_qs);
    }

    // Bring a focused-editor projection to its canonical form before taking
    // the undo snapshot. This keeps a decision made from Axes, Standard, or
    // Flight Deck equivalent to the same decision made from Signal Flow.
    reconcileSignalFlowState(&m_configuration);
    ControllerProfile &profile = currentProfile();
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        return signalFlowActionResult(false, u"Choose one physical input first"_qs,
            u"This Device Rig contains multiple selected inputs. Select one source before changing an axis route."_qs);
    }
    const VirtualAxis virtualAxis = virtualAxisFromString(target);
    const int targetIndex = static_cast<int>(virtualAxis);
    if (virtualAxis == VirtualAxis::Disabled || targetIndex < 1
        || targetIndex >= kVirtualAxisSlotCount) {
        return signalFlowActionResult(false, u"Choose a virtual axis"_qs,
            u"A conflict decision needs a valid exposed vJoy axis destination."_qs);
    }
    if (m_configuration.axisActivity[static_cast<size_t>(physicalAxis)] == PhysicalAxisActivity::Fixed) {
        return signalFlowActionResult(false, u"This axis is marked inactive"_qs,
            u"Complete a calibration with meaningful travel before routing this fixed descriptor axis."_qs);
    }
    if (!m_worker.runtime().virtualAxisAvailable[static_cast<size_t>(targetIndex)].load()) {
        return signalFlowActionResult(false, u"Virtual axis is unavailable"_qs,
            u"The requested vJoy axis is not exposed by the active virtual-output descriptor."_qs);
    }
    VirtualOutputLayout *layout = activeOutputLayout();
    if (!layout) {
        return signalFlowActionResult(false, u"Virtual output is unavailable"_qs,
            u"Assign a Virtual Output to this profile before changing this route."_qs);
    }

    const QString normalizedDecision = decision.trimmed().toLower();
    SignalFlowMixerMode mixerMode = SignalFlowMixerMode::Disabled;
    if (normalizedDecision == u"average"_qs) mixerMode = SignalFlowMixerMode::Average;
    else if (normalizedDecision == u"sum-clamped"_qs) mixerMode = SignalFlowMixerMode::SumClamped;
    else if (normalizedDecision == u"highest-magnitude"_qs) mixerMode = SignalFlowMixerMode::HighestMagnitude;
    else if (normalizedDecision != u"replace"_qs) {
        return signalFlowActionResult(false, u"Choose a valid route decision"_qs,
            u"Choose Replace, Average, Sum Clamped, or Highest Magnitude for this analog conflict."_qs);
    }

    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    const auto inScope = [&profile, &controllerId](const SignalFlowRoute &route) {
        return route.profileId == profile.id && route.controllerRecordId == controllerId;
    };
    const bool anotherAnalogInput = std::any_of(m_configuration.signalFlow.routes.cbegin(),
        m_configuration.signalFlow.routes.cend(), [&inScope, physicalAxis, targetIndex](const SignalFlowRoute &route) {
            return route.enabled && inScope(route) && route.sourceKind == SignalFlowPortKind::Axis
                && route.destinationKind == SignalFlowPortKind::Axis
                && route.sourceIndex != physicalAxis && route.destinationIndex == targetIndex;
        });
    if (!anotherAnalogInput) {
        return signalFlowActionResult(false, u"No active analog conflict"_qs,
            u"The occupied destination changed before this decision was applied. Review the current Signal Flow."_qs);
    }

    MapperConfiguration before = m_configuration;
    SignalFlowState &topology = m_configuration.signalFlow;
    // A focused axis selector can express one primary target only. Replacing
    // its source leg intentionally removes any graph-only fan-out from that
    // source rather than leaving a hidden secondary path behind.
    topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
        [&inScope, physicalAxis](const SignalFlowRoute &route) {
            return inScope(route) && route.sourceKind == SignalFlowPortKind::Axis
                && route.sourceIndex == physicalAxis;
        }), topology.routes.end());

    if (mixerMode == SignalFlowMixerMode::Disabled) {
        topology.routes.erase(std::remove_if(topology.routes.begin(), topology.routes.end(),
            [&inScope, physicalAxis, targetIndex](const SignalFlowRoute &route) {
                return inScope(route) && route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.sourceIndex != physicalAxis && route.destinationIndex == targetIndex;
            }), topology.routes.end());
        topology.mixers.erase(std::remove_if(topology.mixers.begin(), topology.mixers.end(),
            [&profile, &controllerId, targetIndex](const SignalFlowMixer &mixer) {
                return mixer.profileId == profile.id && mixer.controllerRecordId == controllerId
                    && mixer.destinationAxis == targetIndex;
            }), topology.mixers.end());
    } else {
        const auto mixer = std::find_if(topology.mixers.begin(), topology.mixers.end(),
            [&profile, &controllerId, targetIndex](const SignalFlowMixer &candidate) {
                return candidate.profileId == profile.id && candidate.controllerRecordId == controllerId
                    && candidate.destinationAxis == targetIndex;
            });
        if (mixer != topology.mixers.end()) {
            mixer->mode = mixerMode;
            mixer->enabled = true;
        } else {
            SignalFlowMixer created;
            created.profileId = profile.id;
            created.controllerRecordId = controllerId;
            created.destinationAxis = targetIndex;
            created.mode = mixerMode;
            created.enabled = true;
            created.identityKey = signalFlowMixerIdentityKey(profile, controllerId, targetIndex);
            topology.mixers.push_back(std::move(created));
        }
    }

    SignalFlowRoute route;
    route.profileId = profile.id;
    route.controllerRecordId = controllerId;
    route.sourceKind = SignalFlowPortKind::Axis;
    route.sourceIndex = physicalAxis;
    route.sourceSubIndex = -1;
    route.destinationKind = SignalFlowPortKind::Axis;
    route.destinationIndex = targetIndex;
    route.destinationSubIndex = -1;
    route.primaryProjection = true;
    route.enabled = true;
    route.identityKey = signalFlowRouteIdentityKey(profile, controllerId, u"axis"_qs, physicalAxis);
    topology.routes.push_back(std::move(route));
    layout->requirements.axes[static_cast<size_t>(targetIndex)] = true;

    const QString description = mixerMode == SignalFlowMixerMode::Disabled
        ? QString(u"Replaced competing routes into %1"_qs).arg(virtualAxisLabel(virtualAxis))
        : QString(u"Connected %1 to %2 through a visible %3 mixer"_qs)
              .arg(physicalAxisLabel(static_cast<PhysicalAxis>(physicalAxis)),
                   virtualAxisLabel(virtualAxis),
                   normalizedDecision == u"sum-clamped"_qs ? u"Sum Clamped"_qs
                   : normalizedDecision == u"highest-magnitude"_qs ? u"Highest Magnitude"_qs
                   : u"Average"_qs);
    commitSignalFlowCommand(std::move(before), description);
    return signalFlowActionResult(true, u"Axis route decision applied"_qs, description);
}

bool AppBackend::setMapping(int physicalAxis, const QString &target, bool explicitOverride)
{
    if (!validAxis(physicalAxis)) return false;
    // The axis selector is a focused projection of Signal Flow. Reconcile
    // before checking a collision so a graph-originated fan-out cannot be
    // bypassed merely because it has no legacy selector row.
    reconcileSignalFlowState(&m_configuration);
    ControllerProfile &profile = currentProfile();
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing an axis route. Multi-device routes require an explicit source."_qs);
        return false;
    }
    AxisMappings &axes = deviceMapping ? deviceMapping->axes : profile.axes;
    const VirtualAxis virtualAxis = virtualAxisFromString(target);
    const int targetIndex = static_cast<int>(virtualAxis);
    if (virtualAxis != VirtualAxis::Disabled
        && m_configuration.axisActivity[static_cast<size_t>(physicalAxis)] == PhysicalAxisActivity::Fixed) {
        appendEvent(u"Inactive descriptor axes cannot be routed; complete a new calibration to revise activity"_qs);
        return false;
    }
    if (virtualAxis != VirtualAxis::Disabled
        && (targetIndex < 1 || targetIndex >= kVirtualAxisSlotCount
            || !m_worker.runtime().virtualAxisAvailable[static_cast<size_t>(targetIndex)].load())) {
        appendEvent(u"Selected vJoy axis is not exposed by the active device"_qs);
        return false;
    }
    // Do not mutate any persistent state until the conflict decision has
    // completed. In particular, Cancel must leave both routes and output
    // layout metadata exactly as they were before the attempted selection.
    const QString controllerId = deviceMapping ? deviceMapping->controllerRecordId : QString{};
    const bool topologyConflict = virtualAxis != VirtualAxis::Disabled
        && std::any_of(m_configuration.signalFlow.routes.cbegin(), m_configuration.signalFlow.routes.cend(),
            [&profile, &controllerId, physicalAxis, targetIndex](const SignalFlowRoute &route) {
                return route.enabled && route.profileId == profile.id
                    && route.controllerRecordId == controllerId
                    && route.sourceKind == SignalFlowPortKind::Axis
                    && route.destinationKind == SignalFlowPortKind::Axis
                    && route.sourceIndex != physicalAxis && route.destinationIndex == targetIndex;
            });
    if (hasMappingConflict(axes, physicalAxis, virtualAxis) || topologyConflict) {
        if (!explicitOverride) return false;
        // Compatibility callers that already confirmed a conflict retain a
        // safe, explicit meaning: Replace. The visual focused editors use
        // resolveAxisMappingConflict directly when the user chooses a mixer.
        return resolveAxisMappingConflict(physicalAxis, target, u"replace"_qs,
                                          m_configurationGeneration)
            .value(u"success"_qs).toBool();
    }
    // The device descriptor is authoritative for the editor. Persist an
    // exposed target into this profile's layout before compiling the next
    // configuration so the output worker continues to route it safely.
    if (virtualAxis != VirtualAxis::Disabled) {
        VirtualOutputLayout *layout = activeOutputLayout();
        if (!layout) {
            appendEvent(u"The active profile has no virtual output layout"_qs);
            return false;
        }
        layout->requirements.axes[static_cast<size_t>(targetIndex)] = true;
    }
    axes[physicalAxis].target = virtualAxis;
    persistAndApply();
    return true;
}

void AppBackend::setAxisCustomName(int physicalAxis, const QString &name)
{
    if (!validAxis(physicalAxis)) return;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before renaming its axis."_qs);
        return;
    }
    AxisMapping &mapping = deviceMapping ? deviceMapping->axes[static_cast<size_t>(physicalAxis)]
                                         : currentProfile().axes[static_cast<size_t>(physicalAxis)];
    const QString normalized = name.trimmed().left(48);
    if (mapping.customName == normalized) return;
    mapping.customName = normalized;
    persistAndApply();
}

void AppBackend::setAxisRangeMode(int physicalAxis, const QString &mode)
{
    if (!validAxis(physicalAxis)) return;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing its axis domain."_qs);
        return;
    }
    AxisMapping &mapping = deviceMapping ? deviceMapping->axes[static_cast<size_t>(physicalAxis)]
                                         : currentProfile().axes[static_cast<size_t>(physicalAxis)];
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

bool AppBackend::startSignalFlowInputLearning()
{
    if (!physicalConnected()) return false;
    m_inputLearning = {};
    m_inputLearning.kind = InputLearningKind::SignalFlowSource;
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
    m_inputLearning.sourcePovDirection = PovDirection::Centered;
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
    case InputLearningKind::SignalFlowSource:
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing inversion."_qs);
        return;
    }
    (deviceMapping ? deviceMapping->axes : currentProfile().axes)[physicalAxis].inverted = inverted;
    persistAxisProcessorEdit(u"invert"_qs, physicalAxis);
}

void AppBackend::setAxisDeadzone(int physicalAxis, double deadzone)
{
    if (!validAxis(physicalAxis)) return;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing deadzone."_qs);
        return;
    }
    (deviceMapping ? deviceMapping->axes : currentProfile().axes)[physicalAxis].deadzone =
        std::clamp(static_cast<float>(deadzone), 0.0F, 0.95F);
    persistAxisProcessorEdit(u"deadzone"_qs, physicalAxis);
}

void AppBackend::setAxisHysteresis(int physicalAxis, double hysteresis)
{
    if (!validAxis(physicalAxis)) return;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing hysteresis."_qs);
        return;
    }
    (deviceMapping ? deviceMapping->axes : currentProfile().axes)[physicalAxis].hysteresis = std::clamp(
        static_cast<float>(hysteresis), 0.0F, 0.25F);
    persistAxisProcessorEdit(u"center-hold"_qs, physicalAxis);
}

bool AppBackend::setAxisOutputLimits(int physicalAxis, double minimum, double maximum)
{
    if (!validAxis(physicalAxis)) return false;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing output limits."_qs);
        return false;
    }
    AxisMapping &mapping = (deviceMapping ? deviceMapping->axes : currentProfile().axes)[physicalAxis];
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
    persistAxisProcessorEdit(u"limits"_qs, physicalAxis);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::setCurveStrength(double strength)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || mapping->curve.family == CurveFamily::Linear) return;
    const float bounded = std::clamp(static_cast<float>(strength), 0.0F, 1.0F);
    if (std::abs(mapping->curve.strength - bounded) < 0.0001F) return;
    mapping->curve.strength = bounded;
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::setCurveStandardPreset(const QString &presetId)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    const CurveFamily family = mapping->curve.family == CurveFamily::SCurve
        ? CurveFamily::SCurve : CurveFamily::JCurve;
    mapping->curve = standardCurveDefinition(family, presetId);
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::applyAdvancedCurvePreset(const QString &presetId)
{
    if (!advancedCurvePreset(presetId)) return;
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = advancedCurveDefinition(presetId);
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::setCurveInterpolation(const QString &interpolation)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing) return;
    mapping->curve.interpolation = interpolation.trimmed().compare(
        u"linear"_qs, Qt::CaseInsensitive) == 0 ? CurveInterpolation::Linear : CurveInterpolation::Smooth;
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::setCurvePointDensity(int density)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing || !supportedCurvePointDensity(density)) return;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    mapping->curve = resampleCurveDefinition(mapping->curve, unipolar, density);
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

void AppBackend::setCurveSymmetry(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing
        || axisIsOneSided(m_configuration.selectedAxisIndex)) return;
    mapping->curve.symmetry = enabled;
    normalizeCurveDefinition(mapping->curve, false);
    persistSelectedAxisProcessorEdit(u"curve"_qs);
}

bool AppBackend::setCurvePoint(int index, double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = updateCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index,
        static_cast<float>(input), static_cast<float>(output));
    if (changed) persistSelectedAxisProcessorEdit(u"curve"_qs);
    return changed;
}

bool AppBackend::setCurvePointLocked(int index, bool locked)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::setCurvePointLocked(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index, locked);
    if (changed) persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
    return selected;
}

bool AppBackend::removeCurvePoint(int index)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::removeCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index);
    if (changed) persistSelectedAxisProcessorEdit(u"curve"_qs);
    return changed;
}

void AppBackend::resetCurveLinear()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = linearCurveDefinition();
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
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
    persistSelectedAxisProcessorEdit(u"curve"_qs);
    return true;
}

bool AppBackend::setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride)
{
    if (!validPhysicalButton(physicalButton) || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    const int source = physicalButton - 1;
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing a button route."_qs);
        return false;
    }
    ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    PovBindings &povs = deviceMapping ? deviceMapping->povs : currentProfile().povs;
    const ButtonRouteChange change = analyzeButtonRouteChange(bindings, source, virtualButton,
                                                               vjoyButtonCount());
    const bool povConflict = hasButtonMappingConflict(bindings, povs, source,
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before resolving a button route."_qs);
        return false;
    }
    ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    PovBindings &povs = deviceMapping ? deviceMapping->povs : currentProfile().povs;
    const ButtonRouteChange change = analyzeButtonRouteChange(bindings, source, virtualButton,
                                                               vjoyButtonCount());
    if (!change.valid) return false;

    // POV routes retain their existing exclusive contract.  They are never
    // silently displaced, and physical-button Ignore is intentionally limited
    // to the documented many-physical-sources fan-in case.
    bool povConflict = false;
    for (const PovDirectionBindings &hat : povs) {
        for (const ButtonBinding &binding : hat) {
            povConflict = povConflict || (virtualButton > 0
                && binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton);
        }
    }
    if (povConflict && decision != ButtonRouteResolution::Replace) return false;
    if (povConflict) {
        for (PovDirectionBindings &hat : povs) {
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before naming a button."_qs);
        return;
    }
    ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
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
    if (const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
        rig && std::count_if(rig->members.cbegin(), rig->members.cend(),
            [](const DeviceRigMember &member) { return member.enabled; }) > 1) {
        appendEvent(u"Mapping controls are rig-global. Use a one-input rig or a qualified Automation rule for this control."_qs);
        return false;
    }
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing a POV route."_qs);
        return false;
    }
    PovBindings &povs = deviceMapping ? deviceMapping->povs : currentProfile().povs;
    ButtonBindings &buttons = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    if (povs.size() <= static_cast<size_t>(hat)) povs.resize(static_cast<size_t>(hat + 1));
    if (hasPovMappingConflict(buttons, povs, hat, direction, virtualButton,
                              vjoyButtonCount())) {
        if (!explicitOverride) return false;
        for (ButtonBinding &binding : buttons) {
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
    if (const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
        rig && std::count_if(rig->members.cbegin(), rig->members.cend(),
            [](const DeviceRigMember &member) { return member.enabled; }) > 1) {
        appendEvent(u"Profile triggers are rig-global. Use a qualified Automation rule for a multi-device input."_qs);
        return false;
    }
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
    if (const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
        rig && std::count_if(rig->members.cbegin(), rig->members.cend(),
            [](const DeviceRigMember &member) { return member.enabled; }) > 1) {
        appendEvent(u"POV profile triggers are rig-global. Use a qualified Automation rule for a multi-device input."_qs);
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before changing a native POV route."_qs);
        return false;
    }
    NativePovBindings &nativeBindings = deviceMapping ? deviceMapping->nativePovBindings
                                                       : m_configuration.nativePovBindings;
    if (nativeBindings.size() <= static_cast<size_t>(hat)) nativeBindings.resize(static_cast<size_t>(hat + 1));
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
        for (int otherHat = 0; otherHat < static_cast<int>(nativeBindings.size()); ++otherHat) {
            if (otherHat == hat) continue;
            const NativePovBinding &existing = nativeBindings[static_cast<size_t>(otherHat)];
            if (existing.enabled && existing.targetType == binding.targetType
                && existing.targetIndex == binding.targetIndex) {
                appendEvent(u"Each native vJoy POV target can have only one physical POV owner"_qs);
                return false;
            }
        }
        binding.enabled = true;
    }
    nativeBindings[static_cast<size_t>(hat)] = binding;
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
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) {
        appendEvent(u"Select one physical input before resetting its button routes."_qs);
        return;
    }
    (deviceMapping ? deviceMapping->buttons : currentProfile().buttons) =
        defaultButtonMappings(physicalCount, virtualCount);
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
    if (profile && profile->enabled && profile->deviceRigId.isEmpty()) {
        // Preserve pre-rig manual profiles without permitting a mismatched
        // Profile/Rig pair. They are never automatic candidates, and once a
        // Device Rig is active the user must select an explicitly compatible
        // routed profile instead.
        if (!m_configuration.activeDeviceRigId.isEmpty()) {
            appendEvent(u"Manual activation refused: this legacy Profile has no Device Rig while a routed Device Rig is active."_qs);
            return false;
        }
        const QString legacyProfileId = profile->id;
        const QString legacyCategoryId = profile->categoryId;
        const QString legacyOutputLayoutId = profile->outputLayoutId;
        MapperConfiguration candidate = m_configuration;
        if (!hotas::activateProfile(candidate, profileId)) return false;
        candidate.activationManualOverride = false;
        candidate.manualOverrideProfileId.clear();
        if (const VirtualOutputLayout *layout = findOutputLayout(candidate, legacyOutputLayoutId)) {
            candidate.vjoyDeviceId = layout->requirements.deviceId;
        }
        if (!commitActivationConfiguration(candidate)) {
            appendEvent(u"Manual activation could not save the legacy Profile selection."_qs);
            return false;
        }
        m_manualActivationOverride = true;
        m_manualOverrideProfileId = legacyProfileId;
        m_manualOverrideCategoryId = legacyCategoryId;
        appendEvent(u"Manual activation retained for a legacy Profile without a Device Rig."_qs);
        return true;
    }
    const ActivationDecision decision = activationDecision({}, ActivationIntent::ManualProfile, profileId);
    if (!decision.valid) {
        appendEvent(decision.explanation + u" "_qs + decision.blockers.join(u"; "_qs));
        return false;
    }
    return applyActivationDecision(decision, ActivationIntent::ManualProfile);
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
    const ActivationDecision decision = activationDecision(categoryId, ActivationIntent::ManualCategory);
    if (!decision.valid) {
        appendEvent(decision.explanation);
        return false;
    }
    return applyActivationDecision(decision, ActivationIntent::ManualCategory);
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
    scheduleActivationResolution(u"Game / Application association changed"_qs);
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
        m_foregroundGameTimer.start(kForegroundGameProbeIntervalMs);
        startRunningApplicationSnapshot(false);
    } else {
        m_gameDetectionTimer.stop();
        m_foregroundGameTimer.stop();
    }
    persistAndApply();
    appendEvent(enabled ? u"Automatic game category detection enabled"_qs
                        : u"Automatic game category detection disabled"_qs);
    if (enabled) scheduleActivationResolution(u"Automatic Activation resumed"_qs);
}

ActivationContext AppBackend::activationContext(const QString &categoryId, ActivationIntent intent,
                                                const QString &requestedProfileId,
                                                const QString &requestedRigId) const
{
    ActivationContext context;
    context.activeCategoryId = activeCategoryId();
    context.activeProfileId = m_configuration.activeProfileId;
    context.activeDeviceRigId = m_configuration.activeDeviceRigId;
    context.activeOutputLayoutId = currentProfile().outputLayoutId;
    context.rigStatuses = m_deviceRigStatuses;
    context.automaticActivationEnabled = m_configuration.automaticGameDetection;
    context.intent = intent;
    context.requestedProfileId = requestedProfileId.trimmed();
    context.requestedRigId = requestedRigId.trimmed();
    context.manualOverrideActive = m_manualActivationOverride;
    context.manualOverrideProfileId = m_manualOverrideProfileId;
    context.manualOverrideCategoryId = m_manualOverrideCategoryId;
    context.requiredDisconnectGraceRigId = m_requiredDisconnectGraceRigId;
    context.requiredDisconnectGraceProfileId = m_requiredDisconnectGraceProfileId;
    context.configurationGeneration = m_configurationGeneration;
    context.inventoryGeneration = m_inventoryGeneration;
    context.gameContextGeneration = m_gameContextGeneration;
    const HidHideCapabilities &hidhide = m_readiness.plan().hidhide;
    for (const SavedControllerRecord &record : m_configuration.savedControllers) {
        if (record.ownedHidHideDeviceInstances.isEmpty()) continue;
        if (!hidhide.cloakKnown) {
            context.unknownManagedIsolationRecordIds.append(record.id);
            continue;
        }
        const bool hidden = std::all_of(record.ownedHidHideDeviceInstances.cbegin(),
                                        record.ownedHidHideDeviceInstances.cend(), [&hidhide](const QString &identity) {
            const QString normalized = ControllerReadinessService::normalizeDeviceInstanceId(identity);
            return !normalized.isEmpty() && std::any_of(hidhide.hiddenDeviceInstanceIds.cbegin(),
                hidhide.hiddenDeviceInstanceIds.cend(), [&normalized](const QString &candidate) {
                    return ControllerReadinessService::normalizeDeviceInstanceId(candidate) == normalized;
                });
        });
        if (!hidden) context.knownVisibleManagedRecordIds.append(record.id);
    }
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        const QVariantMap detail = virtualOutputDetail(layout.id);
        // An uninspected output is not assumed broken. Once the existing
        // readiness service has inspected it, however, a known unavailable
        // virtual output is an explicit resolver blocker.
        if (detail.value(u"inspected"_qs, false).toBool()
            && !detail.value(u"ready"_qs, false).toBool()) {
            context.unavailableOutputLayoutIds.append(layout.id);
        }
    }
    if (!categoryId.trimmed().isEmpty()) {
        context.matchedCategoryId = categoryId.trimmed();
    } else {
        const GameCategoryMatch foreground = categoryForForegroundExecutable(m_configuration,
            m_stableForegroundExecutable);
        context.matchedCategoryId = !foreground.categoryId.isEmpty()
            ? foreground.categoryId
            : categoryForRunningExecutables(m_configuration, m_lastDetectedExecutables,
                                            context.activeCategoryId).categoryId;
    }
    return context;
}

ActivationDecision AppBackend::activationDecision(const QString &categoryId, ActivationIntent intent,
                                                  const QString &requestedProfileId,
                                                  const QString &requestedRigId) const
{
    return resolveActivation(m_configuration, activationContext(categoryId, intent, requestedProfileId,
                                                                 requestedRigId));
}

QVariantMap AppBackend::activationDecisionVariant(const ActivationDecision &decision) const
{
    const ProfileCategory *category = findProfileCategory(m_configuration, decision.categoryId);
    const ControllerProfile *profile = findProfile(m_configuration, decision.profileId);
    const DeviceRig *rig = findDeviceRig(m_configuration, decision.deviceRigId);
    const VirtualOutputLayout *output = findOutputLayout(m_configuration, decision.outputLayoutId);
    QVariantList blockers;
    for (const QString &blocker : decision.blockers) blockers.append(blocker);
    QVariantList candidates;
    for (const ActivationCandidateEvaluation &candidate : decision.candidates) {
        QVariantList candidateBlockers;
        QVariantList candidateWarnings;
        for (const QString &blocker : candidate.blockers) candidateBlockers.append(blocker);
        for (const QString &warning : candidate.warnings) candidateWarnings.append(warning);
        candidates.append(QVariantMap{{u"profileId"_qs, candidate.profileId},
            {u"profileName"_qs, candidate.profileName},
            {u"deviceRigId"_qs, candidate.deviceRigId},
            {u"deviceRigName"_qs, candidate.deviceRigName},
            {u"outputLayoutId"_qs, candidate.outputLayoutId},
            {u"outputLayoutName"_qs, candidate.outputLayoutName},
            {u"mode"_qs, profileAutomaticSelectionModeKey(candidate.mode)},
            {u"rigHealth"_qs, deviceRigHealthKey(candidate.rigHealth)},
            {u"eligible"_qs, candidate.eligible}, {u"current"_qs, candidate.current},
            {u"selected"_qs, candidate.selected},
            {u"higherPreferenceAvailable"_qs, candidate.higherPreferenceAvailable},
            {u"blockers"_qs, candidateBlockers}, {u"warnings"_qs, candidateWarnings}});
    }
    return {{u"valid"_qs, decision.valid},
            {u"changed"_qs, decision.changed},
            {u"retainedCurrent"_qs, decision.retainedCurrent},
            {u"manualOverride"_qs, decision.manualOverride || m_manualActivationOverride},
            {u"higherPreferenceAvailable"_qs, decision.higherPreferenceAvailable},
            {u"configurationGeneration"_qs, QVariant::fromValue(decision.configurationGeneration)},
            {u"inventoryGeneration"_qs, QVariant::fromValue(decision.inventoryGeneration)},
            {u"gameContextGeneration"_qs, QVariant::fromValue(decision.gameContextGeneration)},
            {u"degraded"_qs, m_activationDegraded},
            {u"reason"_qs, activationDecisionReasonKey(decision.reason)},
            {u"explanation"_qs, decision.explanation},
            {u"blockers"_qs, blockers},
            {u"categoryId"_qs, decision.categoryId},
            {u"categoryName"_qs, category ? category->name : QString{}},
            {u"profileId"_qs, decision.profileId},
            {u"profileName"_qs, profile ? profile->name : QString{}},
            {u"deviceRigId"_qs, decision.deviceRigId},
            {u"deviceRigName"_qs, rig ? rig->name : QString{}},
            {u"outputLayoutId"_qs, decision.outputLayoutId},
            {u"outputLayoutName"_qs, output ? output->name : QString{}},
            {u"candidates"_qs, candidates}};
}

QVariantMap AppBackend::activationResolverState() const
{
    return activationDecisionVariant(activationDecision());
}

QVariantMap AppBackend::activationPreview(const QString &categoryId) const
{
    return activationDecisionVariant(activationDecision(categoryId));
}

QVariantMap AppBackend::explainActivation(const QString &categoryId) const
{
    return activationPreview(categoryId);
}

void AppBackend::scheduleActivationResolution(const QString &reason)
{
    if (!reason.trimmed().isEmpty()) m_pendingActivationReasons.insert(reason.trimmed());
    if (!m_activationResolveTimer.isActive()) m_activationResolveTimer.start();
}

void AppBackend::clearManualActivationOverride(const QString &reason)
{
    if (!m_manualActivationOverride && m_manualOverrideProfileId.isEmpty()) return;
    m_manualActivationOverride = false;
    m_manualOverrideProfileId.clear();
    m_manualOverrideCategoryId.clear();
    if (!reason.isEmpty()) appendEvent(reason);
}

bool AppBackend::commitActivationConfiguration(const MapperConfiguration &candidate)
{
    if (consumeActivationFaultForTest(u"persist"_qs)) return false;
    MapperConfiguration persisted = candidate;
    if (!ConfigStore::save(persisted)) return false;
    m_configuration = std::move(persisted);
    ++m_configurationGeneration;
    m_worker.updateConfiguration(m_configuration);
    rebuildSelectedAxisCurve();
    rebuildCurveAxisChoices();
    rebuildButtonUiModel();
    emit selectedAxisCurveChanged();
    emit stateChanged();
    return true;
}

bool AppBackend::consumeActivationFaultForTest(const QString &stage)
{
#ifdef HOTAS_STARTUP_TESTING
    const QString normalized = stage.trimmed().toCaseFolded();
    if (m_activationFaultInjections.contains(normalized)) {
        m_activationFaultInjections.remove(normalized);
        return true;
    }
#else
    Q_UNUSED(stage);
#endif
    return false;
}

bool AppBackend::applyActivationDecision(const ActivationDecision &decision, ActivationIntent intent)
{
    if (!decision.valid) return false;
    if (decision.configurationGeneration != m_configurationGeneration
        || decision.inventoryGeneration != m_inventoryGeneration
        || decision.gameContextGeneration != m_gameContextGeneration) {
        appendEvent(u"Activation decision discarded because control-plane state changed; it will be resolved again."_qs);
        scheduleActivationResolution(u"stale activation decision"_qs);
        return false;
    }
    const ControllerProfile *target = findProfile(m_configuration, decision.profileId);
    const DeviceRig *targetRig = findDeviceRig(m_configuration, decision.deviceRigId);
    if (!target || !targetRig || !target->enabled || !targetRig->enabled) return false;
    if (!findOutputLayout(m_configuration, decision.outputLayoutId)) return false;
    const QString targetProfileId = target->id;
    const QString targetProfileName = target->name;
    const QString targetCategoryId = target->categoryId;
    const QString targetRigId = targetRig->id;
    const QString targetRigName = targetRig->name;
    // Revalidate the complete profile/rig/output route immediately before
    // changing runtime state. This is a configuration-boundary check only.
    const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(m_configuration, targetRigId, targetProfileId);
    if (!compiled.valid) {
        appendEvent(u"Activation preflight refused: "_qs + compiled.issue);
        return false;
    }

    const MapperConfiguration previous = m_configuration;
    const QString previousOutputLayoutId = currentProfile().outputLayoutId;
    const bool outputChanges = target->outputLayoutId != previousOutputLayoutId;
    const bool mappingWasRequested = outputChanges && m_worker.mappingRequested();
    const auto prepareOutput = [this]() {
#ifdef HOTAS_STARTUP_TESTING
        if (consumeActivationFaultForTest(u"prepare"_qs)) return false;
        if (m_activationTransactionTestBypassDriverConfiguration) return true;
#endif
        return m_worker.prepareForDriverConfiguration();
    };
    const auto restoreOutput = [this, mappingWasRequested]() {
#ifdef HOTAS_STARTUP_TESTING
        if (m_activationTransactionTestBypassDriverConfiguration) return true;
#endif
        return m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
    };
    if (outputChanges && !prepareOutput()) {
        appendEvent(u"Activation preflight could not safely release the current virtual output."_qs);
        return false;
    }
    if (outputChanges) {
        // This is the established virtual-output visibility transaction. The
        // resolver never mutates physical-controller HidHide membership.
        ControllerReadinessService visibility;
        OutputVisibilitySwitchResult result = visibility.applyManagedOutputVisibility(m_configuration,
                                                                                        target->outputLayoutId);
#ifdef HOTAS_STARTUP_TESTING
        if (consumeActivationFaultForTest(u"visibility"_qs)) {
            result.succeeded = false;
            result.status = u"Injected activation visibility failure."_qs;
        }
#endif
        if (!result.succeeded) {
            restoreOutput();
            appendEvent(result.status);
            return false;
        }
        appendEvent(result.status);
    }

    MapperConfiguration candidate = m_configuration;
    if (!hotas::activateProfile(candidate, target->id)) {
        if (outputChanges) restoreOutput();
        return false;
    }
    candidate.activeDeviceRigId = targetRigId;
    candidate.activationManualOverride = false;
    candidate.manualOverrideProfileId.clear();
    if (const VirtualOutputLayout *layout = findOutputLayout(candidate, target->outputLayoutId)) {
        candidate.vjoyDeviceId = layout->requirements.deviceId;
    }
    if (decision.configurationGeneration != m_configurationGeneration
        || decision.inventoryGeneration != m_inventoryGeneration
        || decision.gameContextGeneration != m_gameContextGeneration) {
        if (outputChanges) {
            ControllerReadinessService visibility;
            visibility.applyManagedOutputVisibility(previous, previousOutputLayoutId);
            restoreOutput();
        }
        appendEvent(u"Activation decision became stale during preflight; previous configuration was retained."_qs);
        scheduleActivationResolution(u"activation state changed during preflight"_qs);
        return false;
    }
    if (!commitActivationConfiguration(candidate)) {
        bool rolledBack = true;
        if (outputChanges) {
            ControllerReadinessService visibility;
            rolledBack = visibility.applyManagedOutputVisibility(previous, previousOutputLayoutId).succeeded;
            rolledBack = restoreOutput() && rolledBack;
            if (consumeActivationFaultForTest(u"rollback"_qs)) rolledBack = false;
        }
        m_activationDegraded = !rolledBack;
        appendEvent(rolledBack
            ? u"Activation persistence failed; previous configuration was retained."_qs
            : u"Activation persistence failed and output rollback needs attention. Mapping remains disabled for safety."_qs);
        return false;
    }
    if (outputChanges && (consumeActivationFaultForTest(u"reacquire"_qs) || !restoreOutput())) {
        ControllerReadinessService visibility;
        bool rolledBack = visibility.applyManagedOutputVisibility(previous, previousOutputLayoutId).succeeded;
        rolledBack = commitActivationConfiguration(previous) && rolledBack;
        rolledBack = restoreOutput() && rolledBack;
        if (consumeActivationFaultForTest(u"rollback"_qs)) rolledBack = false;
        m_activationDegraded = !rolledBack;
        appendEvent(rolledBack
            ? u"Activation reacquisition failed; the previous configuration was restored."_qs
            : u"Activation reacquisition and rollback failed; mapping remains disabled pending recovery."_qs);
        return false;
    }
    if (intent == ActivationIntent::ManualProfile || intent == ActivationIntent::ManualCategory
        || intent == ActivationIntent::ManualRig) {
        m_manualActivationOverride = true;
        m_manualOverrideProfileId = targetProfileId;
        m_manualOverrideCategoryId = targetCategoryId;
    } else if (intent == ActivationIntent::RecommendedCandidate) {
        clearManualActivationOverride();
    }
    m_activationDegraded = false;
    appendEvent(QString(u"%1 activation: %2 → %3 → %4"_qs)
        .arg(intent == ActivationIntent::Automatic ? u"Automatic"_qs : u"Manual"_qs,
             activeCategoryName(), targetProfileName, targetRigName));
    emit deviceRigsChanged();
    return true;
}

void AppBackend::resolveActivationNow()
{
    if (m_configuration.automaticGameDetection
        && (!m_initialInventoryResolved || !m_initialGameContextResolved)) {
        // Startup is a barrier, not a guessed fallback: automatic switching
        // begins only after the first inventory and game snapshots agree.
        emit stateChanged();
        return;
    }
    const ActivationContext currentContext = activationContext();
    if (m_manualActivationOverride
        && (!findProfile(m_configuration, m_manualOverrideProfileId)
            || currentContext.matchedCategoryId != m_manualOverrideCategoryId)) {
        clearManualActivationOverride(u"Manual Override ended because its target or game/application context changed."_qs);
    }
    const ActivationDecision decision = resolveActivation(m_configuration, activationContext());
    QStringList triggerReasons = m_pendingActivationReasons.values();
    triggerReasons.sort(Qt::CaseInsensitive);
    const QString trigger = triggerReasons.join(u"; "_qs);
    m_pendingActivationReasons.clear();
    if (m_manualActivationOverride && !decision.manualOverride
        && decision.reason == ActivationDecisionReason::ManualOverrideExpired) {
        clearManualActivationOverride(u"Manual Override ended because the game/application context changed."_qs);
    }
    if (!decision.valid || !decision.changed) {
        emit stateChanged();
        return;
    }
    if (applyActivationDecision(decision, ActivationIntent::Automatic)) {
        appendEvent(QString(u"Activation Resolver (%1): %2"_qs)
            .arg(trigger.isEmpty() ? u"state change"_qs : trigger, decision.explanation));
    }
    emit stateChanged();
}

bool AppBackend::resumeAutomaticActivation()
{
    if (!m_manualActivationOverride) return true;
    clearManualActivationOverride(u"Manual Override ended; Automatic Activation resumed."_qs);
    scheduleActivationResolution(u"Manual Override ended"_qs);
    return true;
}

bool AppBackend::activateRecommendedConfiguration(const QString &categoryId)
{
    const ActivationDecision decision = activationDecision(categoryId, ActivationIntent::RecommendedCandidate);
    if (!decision.valid) {
        appendEvent(decision.explanation + u" "_qs + decision.blockers.join(u"; "_qs));
        return false;
    }
    return applyActivationDecision(decision, ActivationIntent::RecommendedCandidate);
}

bool AppBackend::setProfileAutomaticSelectionMode(const QString &profileId, const QString &mode)
{
    ProfileAutomaticSelectionMode parsed;
    if (!profileAutomaticSelectionModeFromKey(mode, &parsed)
        || !hotas::setProfileAutomaticSelectionMode(m_configuration, profileId, parsed)) return false;
    persistAndApply();
    scheduleActivationResolution(u"profile automatic-selection policy changed"_qs);
    return true;
}

bool AppBackend::reorderCategoryAutomaticProfiles(const QString &categoryId,
                                                   const QStringList &profileIds)
{
    if (!hotas::reorderCategoryProfiles(m_configuration, categoryId, profileIds)) return false;
    persistAndApply();
    scheduleActivationResolution(u"automatic selection order changed"_qs);
    return true;
}

bool AppBackend::assignProfileDeviceRig(const QString &profileId, const QString &rigId)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    const DeviceRig *rig = findDeviceRig(m_configuration, rigId);
    if (!profile || !rig) return false;
    profile->deviceRigId = rig->id;
    persistAndApply();
    scheduleActivationResolution(u"Profile Device Rig changed"_qs);
    return true;
}

void AppBackend::sampleForegroundGameContext()
{
    const ForegroundApplicationSnapshot observed = foregroundApplicationSnapshot();
    if (observed.executable.compare(m_foregroundExecutableCandidate, Qt::CaseInsensitive) != 0
        || observed.processId != m_foregroundExecutableCandidateProcessId) {
        m_foregroundExecutableCandidate = observed.executable;
        m_foregroundExecutableCandidateProcessId = observed.processId;
        m_foregroundExecutableClock.restart();
        return;
    }
    if (m_foregroundExecutableClock.elapsed() < kForegroundGameStableMs
        || (observed.executable.compare(m_stableForegroundExecutable, Qt::CaseInsensitive) == 0
            && observed.processId == m_stableForegroundExecutableProcessId)) return;
    const bool restarted = !m_stableForegroundExecutable.isEmpty()
        && observed.executable.compare(m_stableForegroundExecutable, Qt::CaseInsensitive) == 0
        && observed.processId != 0 && m_stableForegroundExecutableProcessId != 0
        && observed.processId != m_stableForegroundExecutableProcessId;
    m_stableForegroundExecutable = observed.executable;
    m_stableForegroundExecutableProcessId = observed.processId;
    if (restarted) {
        clearManualActivationOverride(u"Manual Override ended because the foreground game/application process restarted."_qs);
    }
    ++m_gameContextGeneration;
    scheduleActivationResolution(u"stable foreground application changed"_qs);
}

void AppBackend::updateRequiredDeviceDisconnectGrace()
{
    const DeviceRig *activeRig = findDeviceRig(m_configuration, m_configuration.activeDeviceRigId);
    const ControllerProfile *activeProfile = findProfile(m_configuration, m_configuration.activeProfileId);
    if (!activeRig || !activeProfile || activeProfile->deviceRigId != activeRig->id) {
        m_requiredDeviceDisconnectStartedMs.clear();
        m_requiredDisconnectGraceRigId.clear();
        m_requiredDisconnectGraceProfileId.clear();
        return;
    }
    const auto found = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
        [activeRig](const DeviceRigStatus &status) { return status.rigId == activeRig->id; });
    if (found == m_deviceRigStatuses.cend() || !found->ambiguousRequiredMemberIds.isEmpty()
        || !found->needsVerificationRequiredMemberIds.isEmpty()
        || found->missingRequiredMemberIds.isEmpty()) {
        m_requiredDeviceDisconnectStartedMs.clear();
        m_requiredDisconnectGraceRigId.clear();
        m_requiredDisconnectGraceProfileId.clear();
        return;
    }
    const qint64 now = m_activationControlPlaneClock.elapsed();
    qint64 earliest = now;
    for (const QString &recordId : found->missingRequiredMemberIds) {
        if (!m_requiredDeviceDisconnectStartedMs.contains(recordId)) {
            m_requiredDeviceDisconnectStartedMs.insert(recordId, now);
        }
        earliest = std::min(earliest, m_requiredDeviceDisconnectStartedMs.value(recordId));
    }
    for (auto it = m_requiredDeviceDisconnectStartedMs.begin(); it != m_requiredDeviceDisconnectStartedMs.end();) {
        if (!found->missingRequiredMemberIds.contains(it.key())) it = m_requiredDeviceDisconnectStartedMs.erase(it);
        else ++it;
    }
    const qint64 elapsed = now - earliest;
    if (elapsed >= kRequiredDeviceDisconnectGraceMs) {
        m_requiredDisconnectGraceRigId.clear();
        m_requiredDisconnectGraceProfileId.clear();
        return;
    }
    m_requiredDisconnectGraceRigId = activeRig->id;
    m_requiredDisconnectGraceProfileId = activeProfile->id;
    m_requiredDisconnectGraceTimer.start(static_cast<int>(kRequiredDeviceDisconnectGraceMs - elapsed));
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
            m_initialGameContextResolved = true;
            if (runningExecutables == m_lastDetectedExecutables) return;
            m_lastDetectedExecutables = runningExecutables;
            ++m_gameContextGeneration;
            if (!m_configuration.automaticGameDetection) return;
            // Category matching produces facts only. The single resolver then
            // owns profile/rig/output policy and its atomic apply, including
            // explaining the no-match case after an application exits.
            scheduleActivationResolution(u"matching Game / Application changed"_qs);
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
    detail.insert(u"categoryActivationBehavior"_qs,
                  u"Resolver order: Preferred profiles, then Fallback profiles; legacy defaults are compatibility-only."_qs);
    detail.insert(u"displayName"_qs, profileDisplayName(profileId));
    detail.insert(u"active"_qs, profile->id == m_configuration.activeProfileId);
    detail.insert(u"enabled"_qs, profile->enabled);
    detail.insert(u"protected"_qs, profile->id == normalProfileId());
    detail.insert(u"automaticSelectionMode"_qs,
                  profileAutomaticSelectionModeKey(profile->automaticSelectionMode));
    detail.insert(u"automaticSelectionLabel"_qs,
                  profileAutomaticSelectionModeLabel(profile->automaticSelectionMode));
    const DeviceRig *profileRig = findDeviceRig(m_configuration, profile->deviceRigId);
    const auto profileRigStatus = std::find_if(m_deviceRigStatuses.cbegin(), m_deviceRigStatuses.cend(),
        [profile](const DeviceRigStatus &status) { return status.rigId == profile->deviceRigId; });
    int requiredCount = 0;
    int optionalCount = 0;
    if (profileRig) {
        for (const DeviceRigMember &member : profileRig->members) {
            if (!member.enabled) continue;
            if (member.required) ++requiredCount;
            else ++optionalCount;
        }
    }
    const int requiredMissing = profileRigStatus == m_deviceRigStatuses.cend() ? requiredCount
        : profileRigStatus->missingRequiredMemberIds.size();
    const int optionalMissing = profileRigStatus == m_deviceRigStatuses.cend() ? optionalCount
        : profileRigStatus->missingOptionalMemberIds.size();
    const bool rigReady = profileRig && profileRigStatus != m_deviceRigStatuses.cend()
        && profileRigStatus->complete && profileRigStatus->ambiguousRequiredMemberIds.isEmpty()
        && profileRigStatus->needsVerificationRequiredMemberIds.isEmpty();
    detail.insert(u"deviceRigId"_qs, profile->deviceRigId);
    detail.insert(u"deviceRigName"_qs, profileRig ? profileRig->name
                                                   : u"Device Rig assignment required"_qs);
    detail.insert(u"deviceRigReady"_qs, rigReady);
    detail.insert(u"requiredDevices"_qs, requiredCount);
    detail.insert(u"requiredConnected"_qs, std::max(0, requiredCount - requiredMissing));
    detail.insert(u"optionalDevices"_qs, optionalCount);
    detail.insert(u"optionalConnected"_qs, std::max(0, optionalCount - optionalMissing));
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
    const int profileAdaptiveOverrides = adaptiveOverrideAxisCount(profile->adaptiveResponse);
    const int categoryAdaptiveOverrides = category
        ? adaptiveOverrideAxisCount(category->adaptiveResponse) : 0;
    const int globalAdaptiveOverrides = adaptiveOverrideAxisCount(m_configuration.adaptiveResponseGlobal);
    detail.insert(u"adaptiveProfileOverrideAxes"_qs, profileAdaptiveOverrides);
    detail.insert(u"adaptiveCategoryOverrideAxes"_qs, categoryAdaptiveOverrides);
    detail.insert(u"adaptiveGlobalOverrideAxes"_qs, globalAdaptiveOverrides);
    detail.insert(u"adaptiveSource"_qs, profileAdaptiveOverrides > 0
        ? u"Custom response overrides in this profile"_qs
        : categoryAdaptiveOverrides > 0 ? u"Inherited from this category"_qs
        : globalAdaptiveOverrides > 0 ? u"Inherited from global defaults"_qs
                                    : u"Built-in response defaults"_qs);
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

bool AppBackend::beginCalibrationForDevice(const QString &recordId)
{
    const SavedControllerRecord *record = savedControllerRecord(recordId.trimmed());
    if (!record) return false;
    const auto discovered = std::find_if(m_discoveredControllers.cbegin(), m_discoveredControllers.cend(),
        [this, record](const DiscoveredController &controller) {
            const ControllerMatch match = ControllerManager::match(controller, m_configuration.savedControllers);
            return !match.ambiguous && match.recordId == record->id && controller.connected;
        });
    if (discovered == m_discoveredControllers.cend()) {
        m_calibrationStatus = QString(u"Connect %1 to calibrate it."_qs).arg(record->displayName);
        appendEvent(m_calibrationStatus);
        emit stateChanged();
        return false;
    }
    if (m_configuration.activeControllerRecordId != record->id) {
        m_pendingCalibrationRecordId = record->id;
        if (!setActiveController(record->id)) {
            m_pendingCalibrationRecordId.clear();
            return false;
        }
        return true;
    }
    beginCalibration();
    return m_calibrationStage != CalibrationStageState::Idle;
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

int AppBackend::suggestedVirtualOutputDeviceId() const
{
    for (int deviceId = 1; deviceId <= 16; ++deviceId) {
        const bool used = std::any_of(m_configuration.outputLayouts.cbegin(),
            m_configuration.outputLayouts.cend(), [deviceId](const VirtualOutputLayout &layout) {
                return layout.requirements.deviceId == deviceId;
            });
        if (!used) return deviceId;
    }
    return 0;
}

QString AppBackend::createVirtualOutputLayout(const QString &name, int deviceId, const QString &preset)
{
    const QString normalizedPreset = preset.trimmed().toLower();
    QVariantList axes;
    if (normalizedPreset == u"full-8-axis"_qs) {
        for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) axes.append(axis);
    } else if (normalizedPreset == u"bf6-4-axis"_qs || normalizedPreset == u"custom"_qs) {
        for (const VirtualAxis axis : {VirtualAxis::X, VirtualAxis::Y, VirtualAxis::Z,
                                       VirtualAxis::Rz}) axes.append(static_cast<int>(axis));
    } else {
        return {};
    }
    const QVariantMap created = createVirtualOutputLayoutResult(name, deviceId, u"custom"_qs,
        {}, axes, normalizedPreset == u"custom"_qs ? 16 : 32, 0, 0);
    return created.value(u"success"_qs).toBool() ? created.value(u"objectId"_qs).toString() : QString{};
}

QVariantMap AppBackend::createVirtualOutputLayoutResult(const QString &name, int deviceId,
                                                         const QString &mode, const QString &sourceId,
                                                         const QVariantList &customAxes, int buttons,
                                                         int continuousPovs, int discretePovs)
{
    const auto result = [](bool success, const QString &title, const QString &message,
                           const QString &objectId = {}, const QString &nextStep = {},
                           const QString &technicalDetails = {}) {
        return actionResult(success, title, message, u"virtualOutput"_qs, objectId, nextStep,
                            {}, technicalDetails);
    };
    const QString trimmed = name.trimmed().left(64);
    const QString normalizedMode = mode.trimmed().toLower();
    if (trimmed.isEmpty()) {
        return result(false, u"Virtual Output needs a name"_qs,
                      u"Enter a name that will help you recognize this virtual controller."_qs);
    }
    if (deviceId < 1 || deviceId > 16 || static_cast<int>(m_configuration.outputLayouts.size()) >= 16) {
        return result(false, u"Virtual Output needs an available vJoy Device ID"_qs,
                      u"Choose an unused vJoy Device ID from 1 through 16."_qs);
    }
    for (const VirtualOutputLayout &layout : m_configuration.outputLayouts) {
        if (layout.name.compare(trimmed, Qt::CaseInsensitive) == 0
            || layout.requirements.deviceId == deviceId) {
            return result(false, u"Virtual Output already exists"_qs,
                          u"That output name or vJoy Device ID is already in use."_qs);
        }
    }

    ControllerVJoyRequirements requirements;
    requirements.deviceId = deviceId;
    QString sourceName;
    if (normalizedMode == u"match-physical"_qs) {
        const SavedControllerRecord *saved = savedControllerRecord(sourceId.trimmed());
        const DiscoveredController *discovered = saved ? nullptr : discoveredController(sourceId.trimmed());
        if (!saved && (!discovered || discovered->virtualDevice)) {
            return result(false, u"Choose a physical controller"_qs,
                          u"Select a saved or connected physical controller to match."_qs);
        }
        const auto &axes = saved ? saved->axes : discovered->axes;
        const int axisCount = saved ? saved->axisCount : discovered->axisCount;
        const int buttonCount = saved ? saved->buttonCount : discovered->buttonCount;
        const int povCount = saved ? saved->povCount : discovered->povCount;
        sourceName = saved ? saved->displayName : discovered->name;
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            requirements.axes[static_cast<size_t>(axis + 1)] = axes[static_cast<size_t>(axis)];
        }
        if (axisCount <= 0 || std::none_of(requirements.axes.cbegin() + 1,
                                            requirements.axes.cend(), [](bool enabled) { return enabled; })) {
            return result(false, u"Physical controller has no usable axes"_qs,
                          u"Choose a physical controller with at least one reported axis."_qs);
        }
        requirements.buttons = std::clamp(buttonCount, 0, kMaximumVirtualButtons);
        requirements.continuousPovs = std::clamp(povCount, 0, kMaximumPhysicalPovs);
        requirements.discretePovs = 0;
    } else if (normalizedMode == u"copy-output"_qs) {
        const VirtualOutputLayout *source = findOutputLayout(m_configuration, sourceId.trimmed());
        if (!source) {
            return result(false, u"Choose a Virtual Output to copy"_qs,
                          u"Select an existing saved Virtual Output before creating a copy."_qs);
        }
        requirements = source->requirements;
        requirements.deviceId = deviceId;
        sourceName = source->name;
    } else if (normalizedMode == u"custom"_qs) {
        QSet<int> selectedAxes;
        for (const QVariant &axis : customAxes) {
            bool valid = false;
            const int index = axis.toInt(&valid);
            if (!valid || index < 1 || index >= kVirtualAxisSlotCount || selectedAxes.contains(index)) {
                return result(false, u"Custom axes are invalid"_qs,
                              u"Choose each supported virtual axis at most once."_qs);
            }
            selectedAxes.insert(index);
            requirements.axes[static_cast<size_t>(index)] = true;
        }
        if (selectedAxes.isEmpty()) {
            return result(false, u"Choose at least one axis"_qs,
                          u"A Virtual Output needs at least one enabled axis."_qs);
        }
        if (buttons < 0 || buttons > kMaximumVirtualButtons) {
            return result(false, u"Button count is invalid"_qs,
                          QString(u"Choose a button count from 0 through %1."_qs).arg(kMaximumVirtualButtons));
        }
        if (continuousPovs < 0 || continuousPovs > kMaximumPhysicalPovs
            || discretePovs < 0 || discretePovs > kMaximumPhysicalPovs) {
            return result(false, u"POV count is invalid"_qs,
                          QString(u"Choose up to %1 continuous or discrete POVs."_qs).arg(kMaximumPhysicalPovs));
        }
        if (continuousPovs > 0 && discretePovs > 0) {
            return result(false, u"Choose one POV type"_qs,
                          u"This vJoy configuration supports continuous POVs or discrete POVs, not both together."_qs);
        }
        requirements.buttons = buttons;
        requirements.continuousPovs = continuousPovs;
        requirements.discretePovs = discretePovs;
    } else {
        return result(false, u"Choose how to configure this Virtual Output"_qs,
                      u"Choose Match a Physical Device, Copy an Existing vJoy Output, or Custom."_qs);
    }

    VirtualOutputLayout layout;
    layout.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layout.name = trimmed;
    layout.requirements = requirements;
    const QString id = layout.id;
    m_configuration.outputLayouts.push_back(std::move(layout));
    persistAndApply();
    appendEvent(QString(u"Created Virtual Output %1 on vJoy Device %2."_qs).arg(trimmed).arg(deviceId));
    emit deviceRigsChanged();
    const QString description = normalizedMode == u"copy-output"_qs
        ? QString(u"Virtual Output created. %1 was copied to vJoy Device %2."_qs).arg(sourceName).arg(deviceId)
        : normalizedMode == u"match-physical"_qs
            ? QString(u"Virtual Output created. Its capabilities match %1."_qs).arg(sourceName)
            : u"Virtual Output created. Its custom capability configuration was saved."_qs;
    return result(true, u"Virtual Output created"_qs,
                  description + u" Check Setup before using it in a game."_qs,
                  id, u"check-output"_qs,
                  QString(u"vJoy Device %1; %2 buttons; %3 continuous POVs; %4 discrete POVs."_qs)
                      .arg(deviceId).arg(requirements.buttons).arg(requirements.continuousPovs)
                      .arg(requirements.discretePovs));
}

bool AppBackend::renameVirtualOutputLayout(const QString &layoutId, const QString &name)
{
    VirtualOutputLayout *layout = findOutputLayout(m_configuration, layoutId.trimmed());
    const QString trimmed = name.trimmed().left(64);
    if (!layout || trimmed.isEmpty() || layout->name == trimmed
        || std::any_of(m_configuration.outputLayouts.cbegin(), m_configuration.outputLayouts.cend(),
            [&layout, &trimmed](const VirtualOutputLayout &candidate) {
                return candidate.id != layout->id
                    && candidate.name.compare(trimmed, Qt::CaseInsensitive) == 0;
            })) return false;
    layout->name = trimmed;
    persistAndApply();
    appendEvent(QString(u"Renamed virtual output: %1"_qs).arg(trimmed));
    emit deviceRigsChanged();
    return true;
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
    QString defaultSourceId;
    if (m_configuration.editingDeviceRecordIds.size() == 1) {
        defaultSourceId = m_configuration.editingDeviceRecordIds.front();
    } else if (const DeviceRig *rig = activeDeviceRig(); rig && rig->members.size() == 1) {
        defaultSourceId = rig->members.front().controllerRecordId;
    } else {
        defaultSourceId = m_configuration.activeControllerRecordId;
    }
    const auto physicalCondition = [](AutomationConditionType type) {
        return type != AutomationConditionType::Always && type != AutomationConditionType::BaseProfileIs
            && type != AutomationConditionType::EffectiveProfileIs;
    };
    const auto deviceAction = [](AutomationActionType type) {
        return type == AutomationActionType::AxisScale || type == AutomationActionType::AxisOffset
            || type == AutomationActionType::AxisClamp || type == AutomationActionType::AxisOverride
            || type == AutomationActionType::AxisMix || type == AutomationActionType::AxisFollow
            || type == AutomationActionType::AdaptiveResponseEnable
            || type == AutomationActionType::AdaptiveResponseDisable
            || type == AutomationActionType::AdaptiveResponsePreset;
    };
    for (AutomationConditionDefinition &condition : candidate.conditions) {
        if (!physicalCondition(condition.type) || !condition.controllerRecordId.isEmpty()) continue;
        if (defaultSourceId.isEmpty()) {
            m_automationValidationMessage = u"Choose one physical device before saving a physical Automation condition."_qs;
            emit stateChanged();
            return false;
        }
        condition.controllerRecordId = defaultSourceId;
    }
    for (AutomationActionDefinition &action : candidate.actions) {
        if (deviceAction(action.type) && action.sourceControllerRecordId.isEmpty()) {
            if (defaultSourceId.isEmpty()) {
                m_automationValidationMessage = u"Choose one physical device before saving a device-scoped Automation action."_qs;
                emit stateChanged();
                return false;
            }
            action.sourceControllerRecordId = defaultSourceId;
        }
        if ((action.type == AutomationActionType::VJoyButtonHold
             || action.type == AutomationActionType::VJoyButtonToggle
             || action.type == AutomationActionType::VJoyButtonTap)
            && action.outputLayoutId.isEmpty()) {
            action.outputLayoutId = currentProfile().outputLayoutId;
        }
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

QVariantMap AppBackend::startSetupAssistantCheck()
{
    if (m_verificationInProgress) {
        return actionResult(false, u"Setup check is already running"_qs,
                            u"HOTAS BF6 is still checking the current setup."_qs,
                            u"application"_qs, {}, u"wait"_qs, {}, {}, true);
    }
    verifyHotasSetup();
    return actionResult(true, u"Checking setup"_qs,
                        u"HOTAS BF6 is checking your physical controller, virtual controller, and game visibility."_qs,
                        u"application"_qs, {}, u"wait"_qs, {}, {}, true);
}

QVariantMap AppBackend::startSetupAssistantCheckForScope(const QString &scopeType,
                                                          const QString &scopeId)
{
    const QString normalizedType = scopeType.trimmed().isEmpty()
        ? u"application"_qs : scopeType.trimmed();
    const QString normalizedId = scopeId.trimmed();
    const bool validType = normalizedType == u"application"_qs || normalizedType == u"device"_qs
        || normalizedType == u"deviceRig"_qs || normalizedType == u"virtualOutput"_qs;
    const bool validTarget = normalizedType == u"application"_qs
        || (normalizedType == u"device"_qs && savedControllerRecord(normalizedId))
        || (normalizedType == u"deviceRig"_qs && findDeviceRig(m_configuration, normalizedId))
        || (normalizedType == u"virtualOutput"_qs && findOutputLayout(m_configuration, normalizedId));
    if (!validType || !validTarget) {
        return actionResult(false, u"Setup target is no longer available"_qs,
                            u"Refresh Devices, choose the saved controller, Device Rig, or Virtual Output again, then retry."_qs,
                            u"application"_qs, normalizedId, u"open-devices"_qs, u"OPEN DEVICES"_qs);
    }
    m_setupAssistantScopeType = normalizedType;
    m_setupAssistantScopeId = normalizedType == u"application"_qs ? QString{} : normalizedId;
    m_setupAssistantLiveTestActive = false;
    if (normalizedType == u"virtualOutput"_qs && m_setupAssistantTestFacts.isEmpty()) {
        refreshVirtualOutputReadiness(normalizedId);
    }
    emit stateChanged();
    return startSetupAssistantCheck();
}

QVariantMap AppBackend::completeSetupAssistantDevice(const QString &recordId)
{
    const QString targetId = recordId.trimmed().isEmpty() ? m_setupAssistantScopeId : recordId.trimmed();
    const SavedControllerRecord *saved = savedControllerRecord(targetId);
    if (!saved) {
        return actionResult(false, u"Selected controller is no longer available"_qs,
            u"Refresh Devices, select the saved controller again, then retry setup."_qs,
            u"physicalDevice"_qs, targetId, u"open-devices"_qs, u"OPEN DEVICES"_qs);
    }
    const auto discovered = std::find_if(m_discoveredControllers.cbegin(),
        m_discoveredControllers.cend(), [this, &targetId](const DiscoveredController &controller) {
            if (!controller.connected || controller.virtualDevice || controller.directInputId.trimmed().isEmpty()) {
                return false;
            }
            const ControllerMatch match = ControllerManager::match(controller,
                m_configuration.savedControllers);
            return !match.ambiguous && match.recordId == targetId;
        });
    if (discovered == m_discoveredControllers.cend()) {
        return actionResult(false, u"Connect the selected controller"_qs,
            u"HOTAS BF6 will verify only the saved controller you selected. Connect that exact controller, then choose Set Up Device again."_qs,
            u"physicalDevice"_qs, targetId, u"check-again"_qs, u"CHECK AGAIN"_qs);
    }
    if (m_verificationInProgress || m_controllerSelectionInProgress) {
        return actionResult(false, u"Setup check is already running"_qs,
            u"HOTAS BF6 is still verifying the selected controller."_qs,
            u"physicalDevice"_qs, targetId, u"wait"_qs, {}, {}, true);
    }
    m_setupAssistantScopeType = u"device"_qs;
    m_setupAssistantScopeId = targetId;
    m_setupAssistantDeviceAcquisitionFailures.remove(targetId);
    // The discovery match establishes the saved target. Acquire that exact
    // DirectInput device before verification, rather than requiring an
    // already-active worker snapshot that setup itself is meant to create.
    // Completion is still committed only after the full verifier returns with
    // the same physical identity.
    m_pendingSetupVerificationRecordId = targetId;
    startExplicitNewControllerVerification(discovered->directInputId, discovered->name);
    return actionResult(true, u"Acquiring selected controller"_qs,
        u"HOTAS BF6 found this exact saved controller and is acquiring it before verification."_qs,
        u"physicalDevice"_qs, targetId, u"wait"_qs, {}, {}, true);
}

QVariantMap AppBackend::applyPhysicalDeviceGameVisibility(const QStringList &controllerRecordIds,
                                                          bool hidden)
{
    QStringList targetIds;
    QStringList targetNames;
    QStringList directInputIds;
    QStringList instances;
    QList<SavedControllerRecord *> records;
    QSet<QString> seen;
    for (const QString &value : controllerRecordIds) {
        const QString recordId = value.trimmed();
        if (recordId.isEmpty() || seen.contains(recordId)) continue;
        const auto found = std::find_if(m_configuration.savedControllers.begin(),
            m_configuration.savedControllers.end(), [&recordId](const SavedControllerRecord &record) {
                return record.id == recordId;
            });
        if (found == m_configuration.savedControllers.end()) {
            if (!m_setupAssistantTestFacts.isEmpty()) {
                const QVariantList testInputs = m_setupAssistantTestFacts.value(u"inputs"_qs).toList();
                const auto testInput = std::find_if(testInputs.cbegin(), testInputs.cend(), [&recordId](const QVariant &entry) {
                    return entry.toMap().value(u"id"_qs).toString() == recordId;
                });
                if (testInput != testInputs.cend()) {
                    const QVariantMap input = testInput->toMap();
                    targetIds.append(recordId);
                    targetNames.append(input.value(u"name"_qs, u"Physical controller"_qs).toString());
                    directInputIds.append(input.value(u"directInputId"_qs).toString());
                    seen.insert(recordId);
                    continue;
                }
            }
            return actionResult(false, u"Could not change game visibility"_qs,
                u"The saved physical controller is no longer available. Refresh Devices and try again."_qs,
                u"physicalDevice"_qs, recordId, u"check-again"_qs, u"TRY AGAIN"_qs,
                u"REQUESTED ACTION\nHide from games\n\nCOMMAND/TRANSACTION RESULT\nSaved device target was not found."_qs);
        }
        SavedControllerRecord *record = &*found;
        const QStringList identities = !record->ownedHidHideDeviceInstances.isEmpty()
            ? record->ownedHidHideDeviceInstances
            : record->hidInstanceId.isEmpty() ? QStringList{} : QStringList{record->hidInstanceId};
        if (identities.isEmpty()) {
            return actionResult(false, u"Could not hide "_qs + record->displayName,
                u"HOTAS BF6 does not have an exact HID identity for this saved controller, so it did not change HidHide."_qs,
                u"physicalDevice"_qs, record->id, u"details"_qs, u"VIEW TECHNICAL DETAILS"_qs,
                QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nDIRECTINPUT ID\n%3\n\nHID INSTANCE(S)\nUnavailable\n\nREQUESTED ACTION\nHide from games\n\nCOMMAND/TRANSACTION RESULT\nExact HID identity missing."_qs)
                    .arg(record->displayName, record->id, record->lastDirectInputId));
        }
        targetIds.append(record->id);
        targetNames.append(record->displayName);
        directInputIds.append(record->lastDirectInputId);
        instances.append(identities);
        records.append(record);
        seen.insert(recordId);
    }
    if (targetIds.isEmpty()) {
        return actionResult(false, u"Could not change game visibility"_qs,
            u"Select a saved physical controller before changing game visibility."_qs,
            u"physicalDevice"_qs, {}, u"check-again"_qs, u"TRY AGAIN"_qs);
    }

    if (!m_setupAssistantTestFacts.isEmpty()) {
        const bool succeeds = m_setupAssistantTestFacts.value(u"visibilityRepairSucceeds"_qs, true).toBool();
        const QString failure = m_setupAssistantTestFacts.value(u"visibilityRepairFailure"_qs,
            u"Fixture denied the requested HidHide repair."_qs).toString();
        const QString technical = QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nDIRECTINPUT ID\n%3\n\nHID INSTANCE(S)\nFixture identity\n\nHIDHIDE\nInstalled: yes\nCLI available: yes\nService ready: yes\nMapper allowlisted: yes\nCloaking enabled: yes\n\nREQUESTED ACTION\nHide from games\n\nCOMMAND/TRANSACTION RESULT\n%4\n\nEXIT CODE\n%5\n\nREADBACK RESULT\n%6\n\nROLLBACK\n%7\n\nLAST CHECK\nFixture"_qs)
            .arg(targetNames.join(u", "_qs), targetIds.join(u", "_qs), directInputIds.join(u", "_qs),
                 succeeds ? u"Targeted fixture repair completed."_qs : failure,
                 succeeds ? u"0"_qs : u"1"_qs,
                 succeeds ? u"Target is hidden."_qs : u"Target remains visible."_qs,
                 succeeds ? u"Not needed"_qs : u"Not needed"_qs);
        if (!succeeds) {
            return actionResult(false, u"Could not hide "_qs + targetNames.front(), failure,
                u"physicalDevice"_qs, targetIds.front(), u"check-again"_qs, u"TRY AGAIN"_qs, technical);
        }
        m_setupAssistantTestFacts.insert(u"physicalVisible"_qs, false);
        emit stateChanged();
        return actionResult(true, u"✓ "_qs + targetNames.front() + u" is hidden from games."_qs,
            u"Visibility changed. HOTAS BF6 reassessed this setup and advanced to the next blocking step."_qs,
            u"physicalDevice"_qs, targetIds.front(), u"check-again"_qs, u"CHECK SETUP"_qs, technical);
    }

    ControllerReadinessService visibility;
    QStringList normalized;
    QString validation;
    if (!visibility.validateManagedPhysicalInputIdentities(instances, &normalized, &validation)) {
        return actionResult(false, u"Could not hide "_qs + targetNames.front(), validation,
            u"physicalDevice"_qs, targetIds.front(), u"check-again"_qs, u"TRY AGAIN"_qs,
            QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nHID INSTANCE(S)\n%3\n\nREQUESTED ACTION\nHide from games\n\nCOMMAND/TRANSACTION RESULT\n%4\n\nREADBACK RESULT\nNo change was made."_qs)
                .arg(targetNames.join(u", "_qs), targetIds.join(u", "_qs), instances.join(u", "_qs), validation));
    }
    const ManagedVisibilityTransactionResult result = visibility.applyManagedPhysicalInputVisibility(normalized, hidden);
    if (!result.succeeded) {
        return actionResult(false, u"Could not hide "_qs + targetNames.front(), result.status,
            u"physicalDevice"_qs, targetIds.front(), u"check-again"_qs, u"TRY AGAIN"_qs,
            QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nDIRECTINPUT ID\n%3\n\nHID INSTANCE(S)\n%4\n\nHIDHIDE\nInstalled: yes\nCLI available: yes\nService ready: yes\nMapper allowlisted: verified before transaction\nCloaking enabled: verified before transaction\nCurrent hidden state: not confirmed\n\n%5\n\nLAST CHECK\n%6"_qs)
                .arg(targetNames.join(u", "_qs), targetIds.join(u", "_qs), directInputIds.join(u", "_qs),
                     normalized.join(u", "_qs), result.technicalDetails,
                     QDateTime::currentDateTime().toString(Qt::ISODate)));
    }
    if (hidden) {
        for (SavedControllerRecord *record : records) {
            if (!record->ownedHidHideDeviceInstances.isEmpty()) continue;
            const QString normalizedIdentity = ControllerReadinessService::normalizeDeviceInstanceId(record->hidInstanceId);
            if (!normalizedIdentity.isEmpty()) record->ownedHidHideDeviceInstances = {normalizedIdentity};
        }
    }
    persistAndApply();
    appendEvent(result.status);
    startQuickVerification();
    emit stateChanged();
    return actionResult(true, QString(u"✓ %1 is hidden from games."_qs).arg(targetNames.front()),
        result.changed ? u"Visibility changed. HOTAS BF6 is reassessing this scoped setup now."_qs
                       : u"This controller was already hidden. HOTAS BF6 is reassessing this scoped setup now."_qs,
        u"physicalDevice"_qs, targetIds.front(), u"check-again"_qs, u"CHECK SETUP"_qs,
        QString(u"ISSUE\nPhysicalInputVisible\n\nTARGET\n%1\n\nSAVED DEVICE ID\n%2\n\nDIRECTINPUT ID\n%3\n\nHID INSTANCE(S)\n%4\n\nHIDHIDE\nInstalled: yes\nCLI available: yes\nService ready: yes\nMapper allowlisted: verified before transaction\nCloaking enabled: verified before transaction\nCurrent hidden state: hidden\n\n%5\n\nLAST CHECK\n%6"_qs)
            .arg(targetNames.join(u", "_qs), targetIds.join(u", "_qs), directInputIds.join(u", "_qs),
                 normalized.join(u", "_qs), result.technicalDetails,
                 QDateTime::currentDateTime().toString(Qt::ISODate)));
}

QVariantMap AppBackend::applySetupAssistantIssueAction(const QString &issueId)
{
    const QVariantList issues = setupAssistantIssues();
    const auto found = std::find_if(issues.cbegin(), issues.cend(),
        [&issueId](const QVariant &entry) { return entry.toMap().value(u"id"_qs).toString() == issueId; });
    if (found == issues.cend()) {
        return actionResult(false, u"Setup action is no longer available"_qs,
            u"Run Check Setup again so HOTAS BF6 can rebuild the current scoped action."_qs,
            u"application"_qs, {}, u"check-again"_qs, u"CHECK SETUP"_qs);
    }
    const QVariantMap issue = found->toMap();
    if (issue.value(u"recommendedAction"_qs).toString() == u"hide-from-games"_qs) {
        return applyPhysicalDeviceGameVisibility(issue.value(u"affectedObjectIds"_qs).toStringList(), true);
    }
    return actionResult(false, issue.value(u"title"_qs).toString(),
        u"This setup action does not have an automatic repair transaction."_qs,
        issue.value(u"affectedObjectType"_qs).toString(), issue.value(u"affectedObjectId"_qs).toString(),
        u"details"_qs, u"VIEW TECHNICAL DETAILS"_qs, issue.value(u"technicalDetails"_qs).toString());
}

QVariantMap AppBackend::applySetupAssistantFix()
{
    const QVariantMap primary = setupAssistantSummary().value(u"primaryIssue"_qs).toMap();
    if (primary.value(u"recommendedAction"_qs).toString() == u"hide-from-games"_qs) {
        return applySetupAssistantIssueAction(primary.value(u"id"_qs).toString());
    }
    if (!m_readiness.plan().canApplyAutomatically) {
        const QString title = primary.value(u"title"_qs,
            u"This setup step needs manual attention"_qs).toString();
        const QString explanation = primary.value(u"explanation"_qs,
            u"HOTAS BF6 cannot safely apply this change automatically. Review Technical Details for the exact requirement."_qs).toString();
        const QString technical = primary.value(u"technicalDetails"_qs).toString();
        return actionResult(false, title, explanation,
                            primary.value(u"affectedObjectType"_qs, u"application"_qs).toString(),
                            primary.value(u"affectedObjectId"_qs).toString(), u"details"_qs,
                            u"VIEW TECHNICAL DETAILS"_qs,
                            technical.isEmpty()
                                ? QString(u"ISSUE: %1\nCURRENT STATE: %2"_qs)
                                      .arg(primary.value(u"code"_qs, u"Unknown"_qs).toString(),
                                           m_readiness.plan().status)
                                : technical);
    }
    if (!applyControllerReadiness()) {
        return actionResult(false, u"HOTAS BF6 could not start the fix"_qs,
                            m_readiness.plan().status.isEmpty()
                                ? u"No changes were applied. Check the setup status and try again."_qs
                                : m_readiness.plan().status,
                            u"application"_qs, {}, u"check"_qs, u"CHECK AGAIN"_qs,
                            QString(u"ATTEMPTED ACTION: automatic setup repair\nRESULT: %1"_qs)
                                .arg(m_readiness.plan().status));
    }
    return actionResult(true, u"Fix started"_qs,
                        u"HOTAS BF6 is applying the recommended setup change and will check the result."_qs,
                        u"application"_qs, {}, u"wait"_qs, {}, {}, true);
}

QVariantMap AppBackend::startSetupAssistantLiveTest()
{
    const AtomicRuntimeState &runtime = m_worker.runtime();
    m_setupAssistantLiveTestActive = true;
    m_setupAssistantInputBaseline = runtime.meaningfulInputSequence.load(std::memory_order_relaxed);
    m_setupAssistantOutputBaseline = runtime.deviceRigMeaningfulOutputSequence[0].load(std::memory_order_relaxed);
    for (int index = 0; index < kMaximumDeviceRigMembers; ++index) {
        m_setupAssistantMemberBaselines[static_cast<size_t>(index)] = runtime.deviceRigMeaningfulInputSequence[
            static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }
    for (int index = 0; index < kMaximumDeviceRigOutputs; ++index) {
        m_setupAssistantOutputBaselines[static_cast<size_t>(index)] = runtime.deviceRigMeaningfulOutputSequence[
            static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }
    emit inputTelemetryChanged();
    return actionResult(true, u"Live control test started"_qs,
                        u"Move an axis about 2% or press and release a mapped button on the highlighted physical controller."_qs,
                        m_setupAssistantScopeType, m_setupAssistantScopeId,
                        u"live-test"_qs);
}

QVariantMap AppBackend::skipCalibrationForSetup(const QString &recordId)
{
    const QString target = recordId.trimmed().isEmpty() ? m_setupAssistantScopeId : recordId.trimmed();
    const SavedControllerRecord *record = savedControllerRecord(target);
    const QString name = record ? record->displayName : u"This controller"_qs;
    // Default DirectInput normalization is already the safe normal path, so
    // accepting this choice cannot mask an invalid saved calibration. Invalid
    // data continues to produce CalibrationRequired and a blocking repair.
    appendEvent(QString(u"Calibration deferred for %1; using the default controller range"_qs).arg(name));
    emit stateChanged();
    return actionResult(true, u"Using default controller range"_qs,
                        name + u" remains ready to use. You can calibrate it any time from Device Details."_qs,
                        u"physicalDevice"_qs, target, u"done"_qs);
}

void AppBackend::verifyHotasSetup()
{
    if (m_readiness.reconnectVerificationPending() || m_readiness.reconnectReconciliationPending()) {
        observeControllerReconnect();
        return;
    }
    startVerification(VerificationMode::Full);
}

void AppBackend::observeControllerReconnect()
{
    if (!m_readiness.reconnectVerificationPending()) return;
    const PhysicalControllerCapabilities observed = currentPhysicalCapabilities();
    const PhysicalControllerCapabilities expected = m_readiness.plan().physical;
    const bool matchingController = observed.connected
        && ControllerReadinessService::samePhysicalController(expected, observed);
    if (!m_readiness.observePhysicalReconnect(observed.connected, matchingController,
                                               observed.inputReportsReceived)) {
        return;
    }
    appendEvent(m_readiness.plan().status);
    if (m_readiness.reconnectReconciliationPending()) {
        reconcileControllerReconnect(observed);
        emit stateChanged();
        return;
    }
    if (!m_readiness.reconnectVerificationPending()
        && m_readiness.plan().state == ControllerReadinessState::Ready) {
        rememberCurrentController();
    }
    emit stateChanged();
}

void AppBackend::reconcileControllerReconnect(const PhysicalControllerCapabilities &physical)
{
    if (!m_readiness.reconnectReconciliationPending() || m_verificationInProgress) return;

    const MapperConfiguration configuration = m_configuration;
    const bool mappingWasRequested = m_worker.mappingRequested();
    ControllerReadinessPlan waiting = m_readiness.plan();
    waiting.state = ControllerReadinessState::Verifying;
    waiting.isChecking = true;
    waiting.hidhideStatus = VerificationSubsystemState::Checking;
    waiting.hidhideSummary = QStringLiteral("Reading HidHide state for the controller's current re-enumerated interfaces.");
    waiting.status = QStringLiteral("RECONCILING HIDHIDE — Checking current controller interfaces after reconnect.");
    m_readiness.adoptPlan(std::move(waiting));
    m_verificationInProgress = true;
    emit stateChanged();
    appendEvent(u"Controller reconnected; reconciling HidHide against its current interfaces"_qs);

    auto reconciler = std::make_shared<ControllerReadinessService>();
    QThread *thread = QThread::create([this, reconciler, configuration, physical, mappingWasRequested] {
        const bool prepared = m_worker.prepareForDriverConfiguration();
        bool repaired = false;
        bool reacquired = false;
        bool restored = true;
        if (prepared) {
            reconciler->inspect(configuration, physical, VerificationMode::Full);
            if (reconciler->plan().state != ControllerReadinessState::Ready
                && reconciler->plan().canApplyAutomatically) {
                repaired = reconciler->applyAutomatically();
            }

            if (reconciler->plan().state == ControllerReadinessState::Ready
                || (repaired && reconciler->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Ready)) {
                const auto visibilityChanged = [](const AutomaticRepairOperationResult &operation) {
                    return operation.succeeded && !operation.rollback
                        && (operation.operationName.startsWith(QStringLiteral("Hide the selected physical controller"))
                            || operation.operationName == QStringLiteral("Enable HidHide cloaking"));
                };
                const bool anotherReconnectRequired = repaired && std::any_of(
                    reconciler->lastAutomaticRepairResult().operations.cbegin(),
                    reconciler->lastAutomaticRepairResult().operations.cend(), visibilityChanged);
                if (anotherReconnectRequired) {
                    // A newly discovered collection was cloaked during the
                    // post-reconnect reconciliation. Require its own actual
                    // re-enumeration rather than declaring read-back READY.
                    reconciler->beginPhysicalReconnectVerification();
                } else {
                    reacquired = m_worker.reacquirePhysicalController(physical.hidInstanceId);
                    if (reacquired) {
                        reconciler->completePhysicalAccessVerification(true, true);
                    } else if (repaired) {
                        const bool rolledBack = reconciler->recoverFromPhysicalAccessFailure();
                        reconciler->completePhysicalAccessVerification(false, false, true, rolledBack, false);
                    } else {
                        ControllerReadinessPlan failed = reconciler->plan();
                        failed.state = ControllerReadinessState::Attention;
                        failed.physicalStatus = VerificationSubsystemState::Attention;
                        failed.physicalSummary = QStringLiteral("The reconnected controller did not provide a fresh DirectInput report during final verification.");
                        failed.status = QStringLiteral("RECONNECT VERIFICATION INCOMPLETE — Move a control, then verify setup again.");
                        reconciler->adoptPlan(std::move(failed));
                    }
                }
            }
        }
        restored = m_worker.restoreAfterDriverConfiguration(mappingWasRequested);
        if (!prepared) {
            ControllerReadinessPlan failed = ControllerReadinessService::checkingPlan(physical, VerificationMode::Full);
            failed.state = ControllerReadinessState::Failed;
            failed.isChecking = false;
            failed.status = QStringLiteral("RECONNECT VERIFICATION FAILED — HOTAS BF6 could not safely prepare the controller stack.");
            reconciler->adoptPlan(std::move(failed));
        } else if (!restored) {
            ControllerReadinessPlan failed = reconciler->plan();
            failed.state = ControllerReadinessState::Failed;
            failed.isChecking = false;
            failed.vjoyStatus = VerificationSubsystemState::Error;
            failed.vjoySummary = QStringLiteral("Reconciliation completed, but HOTAS BF6 could not restore vJoy ownership.");
            failed.status = QStringLiteral("RECONNECT VERIFICATION FAILED — Mapping did not resume after HidHide reconciliation.");
            reconciler->adoptPlan(std::move(failed));
        }

        QMetaObject::invokeMethod(this, [this, reconciler, reacquired, restored] {
            m_readiness = std::move(*reconciler);
            m_verificationInProgress = false;
            appendEvent(m_readiness.plan().status);
            if (restored && reacquired && m_readiness.plan().state == ControllerReadinessState::Ready
                && currentPhysicalCapabilities().connected) {
                rememberCurrentController();
            }
            emit stateChanged();
            if (m_setupConvergenceStage == SetupConvergenceStage::WaitingForUser) {
                setSetupConvergenceStage(SetupConvergenceStage::FinalChecking);
                QTimer::singleShot(0, this, &AppBackend::verifyHotasSetup);
            }
        }, Qt::QueuedConnection);
    });
    m_verificationThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_verificationThread == thread) m_verificationThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
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

    const QString setupVerificationRecordId = mode == VerificationMode::Full
        ? m_pendingSetupVerificationRecordId : QString{};
    QThread *thread = QThread::create([this, configuration, physical, mode, mappingWasRequested,
                                       mapperOwnsVjoy, outputReportsSucceeding, arrivalId,
                                       setupVerificationRecordId] {
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

        QMetaObject::invokeMethod(this, [this, plan = std::move(plan), mode, restored, arrivalId,
                                         setupVerificationRecordId] () mutable {
            m_readiness.adoptPlan(std::move(plan));
            const PhysicalControllerCapabilities observedPhysical = currentPhysicalCapabilities();
            if (m_readiness.reconcilePendingRecoveryAfterVerifiedReadback(observedPhysical)) {
                appendEvent(u"Prior automatic setup recovery was reconciled after fresh controller and driver read-back proof"_qs);
            }
            if (m_readiness.hasPendingRecovery()) {
                // Preserve the fresh plan's physical, HidHide, and vJoy facts.
                // A recovery journal records an unfinished safety proof, not a
                // failed driver inspection. buildSetupTruthSnapshot projects
                // it as a separate controller-verification operation.
                appendEvent(u"Prior automatic setup still awaits a fresh exact-controller and live-input proof"_qs);
            }
            m_verificationInProgress = false;
            appendEvent(restored
                ? QString(u"Controller verification complete: %1"_qs).arg(m_readiness.plan().status)
                : u"Controller verification complete, but mapping restoration failed"_qs);
            if (mode == VerificationMode::Full && restored && observedPhysical.connected) {
                if (!setupVerificationRecordId.isEmpty()) {
                    if (m_pendingSetupVerificationRecordId == setupVerificationRecordId)
                        m_pendingSetupVerificationRecordId.clear();
                    const SavedControllerRecord *saved = savedControllerRecord(setupVerificationRecordId);
                    PhysicalControllerCapabilities expected;
                    if (saved) {
                        expected.directInputId = saved->lastDirectInputId;
                        expected.hidInstanceId = saved->hidInstanceId;
                        expected.hidContainerId = saved->hidContainerId;
                    }
                    const bool physicalReady = m_readiness.plan().physicalStatus == VerificationSubsystemState::Ready;
                    const bool identityMatches = saved && ControllerReadinessService::samePhysicalController(
                        expected, observedPhysical);
                    if (saved && physicalReady && identityMatches && !m_readiness.hasPendingRecovery()
                        && rememberCurrentController(setupVerificationRecordId)) {
                        appendEvent(QString(u"Selected controller setup completed: %1"_qs).arg(saved->displayName));
                    } else {
                        m_setupConvergenceIdentityVerificationFailed = true;
                        if (!saved) {
                            m_setupConvergenceIdentityVerificationFailure =
                                u"The saved controller record is no longer available to persist."_qs;
                        } else if (!physicalReady) {
                            m_setupConvergenceIdentityVerificationFailure = m_readiness.plan().physicalSummary;
                        } else if (!identityMatches) {
                            m_setupConvergenceIdentityVerificationFailure =
                                u"The active controller did not match the selected saved physical identity."_qs;
                        } else if (m_readiness.hasPendingRecovery()) {
                            m_setupConvergenceIdentityVerificationFailure =
                                u"The controller identity matched, but the required fresh DirectInput report after the earlier repair was not observed. Move a control and retry; HidHide and vJoy were not changed."_qs;
                        } else {
                            m_setupConvergenceIdentityVerificationFailure =
                                u"The matching controller was proven, but HOTAS BF6 could not persist its verification record."_qs;
                        }
                        appendEvent(QString(u"Selected controller setup did not commit identity: %1"_qs)
                            .arg(m_setupConvergenceIdentityVerificationFailure));
                    }
                } else if (m_readiness.plan().state == ControllerReadinessState::Ready) {
                    rememberCurrentController();
                }
            }
            if (mode == VerificationMode::Quick && !arrivalId.isEmpty()
                && arrivalId == m_pendingControllerArrivalId) {
                m_pendingControllerArrivalId.clear();
                const PhysicalControllerCapabilities current = currentPhysicalCapabilities();
                const bool known = ControllerReadinessService::isKnownPhysicalController(
                    current, m_configuration.savedControllers);
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
            if (m_setupConvergenceStage == SetupConvergenceStage::Checking
                || m_setupConvergenceStage == SetupConvergenceStage::VerifyingIdentity
                || m_setupConvergenceStage == SetupConvergenceStage::FinalChecking) {
                QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
            }
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
    // An absent custom calibration is a safe, supported default: DirectInput
    // still supplies its normalized range. Only malformed persisted custom
    // calibration may block use of a controller.
    if (!physical.connected || m_configuration.preferredDeviceId != physical.directInputId) return false;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!physical.axes[static_cast<size_t>(index)]) continue;
        const Calibration &calibration = m_configuration.calibration[static_cast<size_t>(index)];
        if (!calibration.enabled) continue;
        const bool finite = std::isfinite(calibration.minimum) && std::isfinite(calibration.maximum)
            && std::isfinite(calibration.center);
        const bool rangeValid = calibration.minimum < calibration.maximum;
        const bool centerValid = !calibration.centered
            || (calibration.minimum < calibration.center && calibration.center < calibration.maximum);
        if (!finite || !rangeValid || !centerValid) {
            return true;
        }
    }
    return false;
}

bool AppBackend::applyControllerReadiness()
{
    return applyControllerReadinessForConfiguration(m_configuration);
}

bool AppBackend::applyControllerReadinessForConfiguration(const MapperConfiguration &configuration)
{
    if (m_verificationInProgress) return false;
    // The worker owns vJoy while mapping normally. Release it before the
    // privileged control-plane transaction, then restore the prior user choice.
    const bool mappingWasRequested = m_worker.mappingRequested();
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
        bool visibilityReconnectRequired = false;
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
                const auto changedVisibility = [](const AutomaticRepairOperationResult &operation) {
                    return operation.succeeded && !operation.rollback
                        && (operation.operationName == QStringLiteral("Hide the selected physical controller")
                            || operation.operationName == QStringLiteral("Enable HidHide cloaking"));
                };
                visibilityReconnectRequired = std::any_of(
                    repair->lastAutomaticRepairResult().operations.cbegin(),
                    repair->lastAutomaticRepairResult().operations.cend(), changedVisibility);
                if (visibilityReconnectRequired) {
                    repair->beginPhysicalReconnectVerification();
                } else {
                repair->completePhysicalAccessVerification(true, true);
                }
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
                || repair->lastAutomaticRepairResult().outcome == AutomaticRepairOutcome::Failed
                || repair->reconnectVerificationPending()) {
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
                                          visibilityReconnectRequired,
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
                if (visibilityReconnectRequired) {
                    appendEvent(u"HidHide changed device visibility; waiting for the required unplug and reconnect"_qs);
                }
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
            if (m_setupConvergenceStage == SetupConvergenceStage::Repairing) {
                QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
            }
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

bool AppBackend::rememberCurrentController(const QString &expectedRecordId)
{
    const DiscoveredController *controller = discoveredController(deviceId());
    if (!controller || controller->virtualDevice) return false;
    const ControllerMatch match = ControllerManager::match(*controller, m_configuration.savedControllers);
    QString existingId = match.ambiguous ? QString{} : match.recordId;
    if (!expectedRecordId.isEmpty()) {
        const SavedControllerRecord *expected = savedControllerRecord(expectedRecordId);
        PhysicalControllerCapabilities remembered;
        if (!expected) return false;
        remembered.directInputId = expected->lastDirectInputId;
        remembered.hidInstanceId = expected->hidInstanceId;
        remembered.hidContainerId = expected->hidContainerId;
        PhysicalControllerCapabilities observed;
        observed.directInputId = controller->directInputId;
        observed.hidInstanceId = controller->hidInstanceId;
        observed.hidContainerId = controller->hidContainerId;
        observed.connected = controller->connected;
        if (!ControllerReadinessService::samePhysicalController(remembered, observed)) return false;
        existingId = expectedRecordId;
    }
    ControllerVJoyRequirements verifiedRequirements = currentVjoyRequirements();
    const ControllerReadinessPlan &verifiedPlan = m_readiness.plan();
    if (verifiedPlan.physicalStatus == VerificationSubsystemState::Ready
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
    MapperConfiguration candidate = m_configuration;
    if (!existingId.isEmpty()) {
        bool updated = false;
        for (SavedControllerRecord &existing : candidate.savedControllers) {
            if (existing.id != existingId) continue;
            record.ownedHidHideDeviceInstances = existing.ownedHidHideDeviceInstances;
            existing = std::move(record);
            candidate.activeControllerRecordId = existingId;
            updated = true;
            break;
        }
        if (!updated) return false;
    } else {
        candidate.savedControllers.push_back(std::move(record));
        candidate.activeControllerRecordId = candidate.savedControllers.back().id;
    }
    candidate.preferredDeviceId = controller->directInputId;
    if (!ConfigStore::save(candidate)) return false;
    m_configuration = std::move(candidate);
    ++m_configurationGeneration;
    m_worker.updateConfiguration(m_configuration);
    rebuildControllerUiModel();
    appendEvent(QString(u"Verified controller remembered: %1"_qs).arg(controller->name));
    return true;
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
                    ? QString(u"Active controller switched to %1; the selected vJoy device meets the required capabilities"_qs).arg(selectedTarget.name)
                    : QString(u"Active controller switched to %1; vJoy was configured and verified before mapping resumed"_qs).arg(selectedTarget.name));
                if (m_pendingCalibrationRecordId == m_configuration.activeControllerRecordId) {
                    m_pendingCalibrationRecordId.clear();
                    beginCalibration();
                }
            } else {
                m_pendingCalibrationRecordId.clear();
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
                m_setupAssistantDeviceAcquisitionFailures.remove(m_pendingSetupVerificationRecordId);
                appendEvent(QString(u"Selected controller acquired for setup: %1; starting explicit verification"_qs)
                    .arg(displayName));
                verifyHotasSetup();
            } else {
                const QString failedRecordId = m_pendingSetupVerificationRecordId;
                m_pendingSetupVerificationRecordId.clear();
                if (!failedRecordId.isEmpty()) {
                    m_setupAssistantDeviceAcquisitionFailures.insert(failedRecordId,
                        QString(u"HOTAS BF6 found the saved controller in discovery, but it did not receive a fresh DirectInput report while acquiring it. Close other HOTAS BF6 sessions or any controller software using the device, then retry acquisition."_qs));
                }
                appendEvent(QString(u"Could not acquire %1 for setup; no fresh DirectInput report arrived"_qs)
                    .arg(displayName));
                if (m_setupConvergenceStage == SetupConvergenceStage::VerifyingIdentity) {
                    m_setupConvergenceIdentityVerificationFailed = true;
                    QTimer::singleShot(0, this, &AppBackend::continueSetupConvergence);
                }
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
    } else if (normalized == u"flight deck light"_qs) {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #f9fbfd; color: #152534; border: 1px solid #cfdae5; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Segoe UI'; }"
            "QMenu::item:selected { background: #d7eff7; color: #152534; border: 1px solid #167b9f; }"
            "QMenu::item:disabled { color: #a9b7c2; }"
            "QMenu::separator { height: 1px; background: #dde5ed; margin: 5px 9px; }"));
    } else if (normalized == u"flight deck"_qs || normalized == u"flight deck dark"_qs) {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #132331; color: #eef7fb; border: 1px solid #315064; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Segoe UI'; }"
            "QMenu::item:selected { background: #174656; color: #eef7fb; border: 1px solid #4dc5df; }"
            "QMenu::item:disabled { color: #49626f; }"
            "QMenu::separator { height: 1px; background: #284353; margin: 5px 9px; }"));
    } else if (normalized == u"day ops"_qs) {
        m_trayMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #d9ddda; color: #142a38; border: 1px solid #2e5268; padding: 6px; }"
            "QMenu::item { background: transparent; padding: 9px 38px 9px 15px; min-height: 22px; font: 600 9pt 'Segoe UI Variable'; }"
            "QMenu::item:selected { background: #b9d0d1; color: #0e2534; border: 1px solid #c56622; }"
            "QMenu::item:disabled { color: #71828a; }"
            "QMenu::separator { height: 1px; background: #78909a; margin: 5px 9px; }"));
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
    reconcileDeviceRigInventory();
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

void AppBackend::reconcileDeviceRigInventory()
{
    m_deviceRigStatuses = evaluateDeviceRigs(m_configuration, m_discoveredControllers);
    ++m_inventoryGeneration;
    m_initialInventoryResolved = true;
    updateRequiredDeviceDisconnectGrace();
    // V2.5.4 intentionally retires the independent rig auto-selector here.
    // Inventory changes feed the one Category → Profile → Rig → Output
    // resolver, so a ready rig for another game cannot win on its own.
    m_deviceRigDetectionMessage.clear();
    scheduleActivationResolution(u"Device Rig readiness changed"_qs);
    emit deviceRigsChanged();
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
    physical.hidContainerId = snapshot.hidContainerId;
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
    case InputLearningKind::SignalFlowSource:
        m_inputLearning.message = u"RELEASE BUTTONS, CENTER HATS, AND HOLD AXES STEADY…"_qs;
        break;
    case InputLearningKind::None:
        break;
    }
}

QString AppBackend::learnedAxisLabel(int physicalAxis) const
{
    if (!validAxis(physicalAxis)) return {};
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : currentProfile().axes;
    const QString custom = axes[static_cast<size_t>(physicalAxis)].customName.trimmed();
    return custom.isEmpty() ? physicalAxisLabel(static_cast<PhysicalAxis>(physicalAxis)) : custom;
}

QString AppBackend::learnedButtonLabel(int physicalButton) const
{
    if (!validPhysicalButton(physicalButton)) return {};
    const int source = physicalButton - 1;
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
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
    case InputLearningKind::SignalFlowSource:
        return false;
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
        case InputLearningKind::SignalFlowSource: {
            constexpr float stabilityTolerance = 0.03F;
            bool axesStable = true;
            for (int index = 0; index < kPhysicalAxisCount; ++index) {
                const size_t slot = static_cast<size_t>(index);
                if (!m_inputLearning.axisAvailable[slot]
                    || m_inputLearning.axisActivity[slot] == PhysicalAxisActivity::Fixed) {
                    continue;
                }
                const float current = runtime.normalized[slot].load();
                if (std::abs(current - m_inputLearning.axisBaseline[slot]) > stabilityTolerance) {
                    m_inputLearning.axisBaseline[slot] = current;
                    axesStable = false;
                }
            }
            bool buttonsReleased = true;
            for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
                const size_t slot = static_cast<size_t>(index);
                if (runtime.buttonAvailable[slot].load() && runtime.physicalButtonPressed[slot].load()) {
                    buttonsReleased = false;
                    break;
                }
            }
            bool povsCentered = true;
            for (int index = 0; index < std::min(povCount(), kMaximumPhysicalPovs); ++index) {
                if (povDirectionFromRaw(runtime.povValues[static_cast<size_t>(index)].load())
                    != PovDirection::Centered) {
                    povsCentered = false;
                    break;
                }
            }
            if (!axesStable || !buttonsReleased || !povsCentered) {
                m_inputLearning.armingStableSinceMs = 0;
                return;
            }
            if (m_inputLearning.armingStableSinceMs == 0) {
                m_inputLearning.armingStableSinceMs = now;
                return;
            }
            if (now - m_inputLearning.armingStableSinceMs < 300) return;
            captureInputLearningBaseline();
            m_inputLearning.phase = InputLearningPhase::Waiting;
            m_inputLearning.message = u"MOVE OR PRESS THE PHYSICAL CONTROL YOU WANT TO ROUTE."_qs;
            emit inputLearningChanged();
            return;
        }
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
    case InputLearningKind::SignalFlowSource: {
        std::array<float, kPhysicalAxisCount> axisCurrent{};
        std::array<bool, kMaximumPhysicalButtons> buttonCurrent{};
        std::array<bool, kMaximumPhysicalButtons> buttonAvailable{};
        std::array<int, kMaximumPhysicalPovs> povCurrent{};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            axisCurrent[static_cast<size_t>(index)] = runtime.normalized[static_cast<size_t>(index)].load();
        }
        for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
            const size_t slot = static_cast<size_t>(index);
            buttonCurrent[slot] = runtime.physicalButtonPressed[slot].load();
            buttonAvailable[slot] = runtime.buttonAvailable[slot].load();
        }
        for (int index = 0; index < kMaximumPhysicalPovs; ++index) {
            povCurrent[static_cast<size_t>(index)] = runtime.povValues[static_cast<size_t>(index)].load();
        }
        const SignalFlowInputSelection selection = selectSignalFlowInput(
            m_inputLearning.axisBaseline, axisCurrent, m_inputLearning.axisAvailable,
            m_inputLearning.axisActivity, m_inputLearning.buttonBaseline, buttonCurrent,
            buttonAvailable, m_inputLearning.povBaseline, povCurrent, povCount());
        if (selection.result == SignalFlowInputSelectionResult::Waiting) return;
        if (selection.result == SignalFlowInputSelectionResult::Ambiguous) {
            m_inputLearning.phase = InputLearningPhase::Ambiguous;
            m_inputLearning.message = u"MULTIPLE INPUTS DETECTED — Move or press only the control you want to route."_qs;
            emit inputLearningChanged();
            return;
        }
        switch (selection.kind) {
        case SignalFlowInputSourceKind::Axis:
            m_inputLearning.sourceAxis = selection.index;
            m_inputLearning.sourceLabel = learnedAxisLabel(selection.index);
            break;
        case SignalFlowInputSourceKind::Button:
            m_inputLearning.sourceButton = selection.index + 1;
            m_inputLearning.sourceLabel = learnedButtonLabel(m_inputLearning.sourceButton);
            break;
        case SignalFlowInputSourceKind::Pov:
            m_inputLearning.sourcePovHat = selection.index + 1;
            m_inputLearning.sourcePovDirection = static_cast<PovDirection>(selection.subIndex + 1);
            m_inputLearning.sourceLabel = QString(u"POV %1 %2"_qs).arg(m_inputLearning.sourcePovHat)
                .arg(povDirectionLabel(m_inputLearning.sourcePovDirection));
            break;
        case SignalFlowInputSourceKind::None:
            return;
        }
        m_inputLearning.phase = InputLearningPhase::Assigned;
        m_inputLearning.message = QString(u"Found %1. Choose a compatible virtual destination."_qs)
            .arg(m_inputLearning.sourceLabel);
        emit inputLearningChanged();
        return;
    }
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
    // Preserve the explicit HidHide reconnect proof through this UI tick. A
    // newly reconnected controller must not start the ordinary arrival probe
    // and replace the reconnect plan before its selected-device/live-report
    // observation completes.
    const bool reconnectWasPending = m_readiness.reconnectVerificationPending()
        || m_readiness.reconnectReconciliationPending();
    observeControllerReconnect();
    const bool connectionChanged = connected != m_physicalControllerWasConnected;
    if (ControllerReadinessService::isNewPhysicalControllerArrival(
            m_physicalControllerWasConnected, connected) && !m_verificationInProgress
        && !reconnectWasPending) {
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
    const bool captureAdaptiveHistory = connected
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
    // Physical DirectInput telemetry remains useful when mapping is off,
    // suspended, or vJoy is unavailable. Only a disconnected controller gets
    // sparse samples, preserving a responsive Live Controller inspection feed.
    if (!runtime.physicalConnected.load()
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
    sample.baselineMappedOutput = runtime.adaptiveBaselineMapped[index].load();
    sample.predictedMappedOutput = runtime.adaptivePredictedMapped[index].load();
    sample.adaptiveOutput = runtime.adaptiveOutput[index].load();
    sample.mappedLead = runtime.adaptiveMappedLead[index].load();
    sample.appliedLead = runtime.adaptiveAppliedLead[index].load();
    sample.localCurveGain = runtime.adaptiveLocalCurveGain[index].load();
    sample.virtualOutput = sample.adaptiveOutput;
    sample.velocity = runtime.adaptiveVelocity[index].load();
    sample.acceleration = runtime.adaptiveAcceleration[index].load();
    sample.activeHorizonSeconds = runtime.adaptiveHorizonSeconds[index].load();
    sample.maximumHorizonSeconds = runtime.adaptiveRuntimeMaximumHorizonSeconds[index].load();
    sample.requestedLead = runtime.adaptiveRequestedLead[index].load();
    sample.cappedLead = runtime.adaptiveCappedLead[index].load();
    sample.endpointTaper = runtime.adaptiveEndpointTaper[index].load();
    sample.lead = runtime.adaptiveLead[index].load();
    sample.confidence = runtime.adaptiveConfidence[index].load();
    sample.motionIntensity = runtime.adaptiveMotionIntensity[index].load();
    sample.velocityAuthority = runtime.adaptiveVelocityAuthority[index].load();
    sample.deliberateMotionEvidence = runtime.adaptiveDeliberateMotionEvidence[index].load();
    sample.normalMotionAuthority = runtime.adaptiveNormalMotionAuthority[index].load();
    sample.rapidMotionAuthority = runtime.adaptiveRapidMotionAuthority[index].load();
    sample.rapidMotionBlend = runtime.adaptiveRapidMotionBlend[index].load();
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
    const QString entry = timestamp + u"  "_qs + event;
    m_events.append(entry);
    CrashDiagnostics::recordControlPlaneEvent(entry, crashPresentationContext());
    emit eventLogChanged();
}

QString AppBackend::crashPresentationContext() const
{
    static constexpr std::array<const char *, 11> pages{
        "Axes", "Buttons", "Calibration", "Diagnostics", "Settings", "Profiles", "Curves", "Automation", "Overview", "Adaptive Response", "Devices"};
    const QString page = m_crashPresentationPage >= 0 && m_crashPresentationPage < static_cast<int>(pages.size())
        ? QString::fromLatin1(pages[static_cast<size_t>(m_crashPresentationPage)]) : u"Unknown"_qs;
    const auto safe = [](QString value) {
        return value.replace(u'\n', u' ').replace(u'\r', u' ').replace(u'"', u'\'').left(128);
    };
    return QString(u"page=%1\\ntheme=%2\\nactiveProfile=%3\\nactiveRig=%4\\neditingRig=%5\\neditingScope=%6\\nphysicalDevices=%7/%8 connected\\nmappingRequested=%9\\nmappingEffective=%10\\nvJoy=%11\\nHidHide=%12"_qs)
        .arg(safe(page), safe(m_crashPresentationTheme), safe(activeProfileName()), safe(activeDeviceRigName()), safe(editingDeviceRigName()), safe(editingScopeLabel()))
        .arg(m_connectedControllerCount).arg(m_discoveredControllers.size())
        .arg(mappingRequested() ? u"true"_qs : u"false"_qs, mappingActive() ? u"true"_qs : u"false"_qs,
             vjoyReady() ? u"ready"_qs : u"unavailable"_qs,
             hidhideAvailable() && hidhideCloaked() && hidhideMapperAllowed() ? u"ready"_qs : u"needs attention"_qs);
}

void AppBackend::initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity)
{
    if (physicalButtonCount <= 0 || vjoyButtonCapacity <= 0) {
        return;
    }
    DeviceProfileMapping *deviceMapping = editingDeviceMappingForWrite();
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) && !deviceMapping) return;
    ButtonBindings &bindings = deviceMapping ? deviceMapping->buttons : currentProfile().buttons;
    if (!ensureDefaultButtonMappings(bindings, physicalButtonCount, vjoyButtonCapacity)) {
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

bool AppBackend::editingScopeHasSinglePhysicalSource() const
{
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    if (!rig) return true;
    if (m_configuration.editingDeviceRecordIds.size() == 1) return true;
    return m_configuration.editingDeviceRecordIds.isEmpty()
        && std::count_if(rig->members.cbegin(), rig->members.cend(),
                         [](const DeviceRigMember &member) { return member.enabled; }) == 1;
}

const DeviceProfileMapping *AppBackend::editingDeviceMapping() const
{
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    const ControllerProfile &profile = currentProfile();
    if (!rig || (!profile.deviceRigId.isEmpty() && profile.deviceRigId != rig->id)) return nullptr;
    QString controllerId;
    if (m_configuration.editingDeviceRecordIds.size() == 1) {
        controllerId = m_configuration.editingDeviceRecordIds.front();
    } else if (m_configuration.editingDeviceRecordIds.isEmpty()) {
        const auto member = std::find_if(rig->members.cbegin(), rig->members.cend(),
            [](const DeviceRigMember &candidate) { return candidate.enabled; });
        if (member == rig->members.cend()
            || std::count_if(rig->members.cbegin(), rig->members.cend(),
                [](const DeviceRigMember &candidate) { return candidate.enabled; }) != 1) return nullptr;
        controllerId = member->controllerRecordId;
    } else {
        return nullptr;
    }
    return findDeviceProfileMapping(profile, controllerId);
}

DeviceProfileMapping *AppBackend::editingDeviceMappingForWrite()
{
    const DeviceRig *rig = findDeviceRig(m_configuration, m_configuration.editingDeviceRigId);
    ControllerProfile &profile = currentProfile();
    if (!rig || (!profile.deviceRigId.isEmpty() && profile.deviceRigId != rig->id)) return nullptr;
    QString controllerId;
    if (m_configuration.editingDeviceRecordIds.size() == 1) {
        controllerId = m_configuration.editingDeviceRecordIds.front();
    } else if (m_configuration.editingDeviceRecordIds.isEmpty()) {
        const auto member = std::find_if(rig->members.cbegin(), rig->members.cend(),
            [](const DeviceRigMember &candidate) { return candidate.enabled; });
        if (member == rig->members.cend()
            || std::count_if(rig->members.cbegin(), rig->members.cend(),
                [](const DeviceRigMember &candidate) { return candidate.enabled; }) != 1) return nullptr;
        controllerId = member->controllerRecordId;
    } else {
        return nullptr;
    }
    return &ensureDeviceProfileMapping(profile, controllerId);
}

AxisMapping *AppBackend::selectedAxisMapping()
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    if (DeviceProfileMapping *mapping = editingDeviceMappingForWrite()) {
        return &mapping->axes[m_configuration.selectedAxisIndex];
    }
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId)
        && !editingScopeHasSinglePhysicalSource()) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

const AxisMapping *AppBackend::selectedAxisMapping() const
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    if (const DeviceProfileMapping *mapping = editingDeviceMapping()) {
        return &mapping->axes[m_configuration.selectedAxisIndex];
    }
    if (findDeviceRig(m_configuration, m_configuration.editingDeviceRigId)
        && !editingScopeHasSinglePhysicalSource()) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

void AppBackend::persistAndApply()
{
    // These fields existed only in unreleased candidate configurations.  A
    // manual selection is runtime/session state and must never re-enter disk.
    m_configuration.activationManualOverride = false;
    m_configuration.manualOverrideProfileId.clear();
    // This is a bounded UI/control-plane reconciliation. It runs only when a
    // configuration is committed, never from the DirectInput report thread.
    // Focused editors therefore update the same durable Signal Flow IDs on
    // their normal immediate-persistence path.
    reconcileSignalFlowState(&m_configuration);
    if (SavedControllerRecord *record = activeControllerRecord()) {
        record->calibration = m_configuration.calibration;
        record->axisActivity = m_configuration.axisActivity;
        record->vjoyRequirements = currentVjoyRequirements();
    }
    ConfigStore::save(m_configuration);
    ++m_configurationGeneration;
    m_worker.updateConfiguration(m_configuration);
    rebuildSelectedAxisCurve();
    rebuildCurveAxisChoices();
    rebuildButtonUiModel();
    emit selectedAxisCurveChanged();
    emit stateChanged();
    emit signalFlowChanged();
    // Configuration commits happen on the UI/control plane. Coalescing here
    // covers topology, profile, and output edits without adding work to the
    // DirectInput → MappingWorker → vJoy report path.
    scheduleActivationResolution(u"configuration changed"_qs);
}

void AppBackend::persistAxisProcessorEdit(const QString &kind, int physicalAxis)
{
    if (validAxis(physicalAxis)) {
        const DeviceProfileMapping *mapping = editingDeviceMapping();
        if (!findDeviceRig(m_configuration, m_configuration.editingDeviceRigId) || mapping) {
            signalFlowPropagateSharedProcessorSettings(&m_configuration, currentProfile().id,
                mapping ? mapping->controllerRecordId : QString{}, kind, physicalAxis);
        }
    }
    persistAndApply();
}

void AppBackend::persistSelectedAxisProcessorEdit(const QString &kind)
{
    persistAxisProcessorEdit(kind, m_configuration.selectedAxisIndex);
}

void AppBackend::propagateProfileAdaptiveResponseIfShared(const QString &scope,
                                                          const QString &targetId,
                                                          int physicalAxis)
{
    if (scope.trimmed().compare(u"profile"_qs, Qt::CaseInsensitive) != 0
        || !validAxis(physicalAxis)) {
        return;
    }
    const QString profileId = targetId.trimmed().isEmpty() ? currentProfile().id : targetId.trimmed();
    signalFlowPropagateSharedProcessorSettings(&m_configuration, profileId, {},
                                                u"adaptive-response"_qs, physicalAxis);
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
    const AxisMapping *selected = selectedAxisMapping();
    if (!selected) return;
    const AxisMapping &axis = *selected;
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
    if (!validAxis(physicalAxis)) return false;
    const DeviceProfileMapping *deviceMapping = editingDeviceMapping();
    const AxisMappings &axes = deviceMapping ? deviceMapping->axes : currentProfile().axes;
    return axes[static_cast<size_t>(physicalAxis)].rangeMode == AxisRangeMode::OneSided;
}

} // namespace hotas
