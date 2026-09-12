#include "controller_readiness.h"
#include "hid_device_identity.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QUuid>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winsvc.h>

#include <algorithm>
#include <chrono>

namespace hotas {
namespace {

constexpr int kInspectionTimeoutMs = 2500;
constexpr int kApplyTimeoutMs = 30000;
constexpr auto kPendingRecoveryKey = "readiness/pendingAutomaticRepairRecovery";

QStringList installRoots()
{
    QStringList roots{qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
                      QStringLiteral("C:/Program Files")};
    roots.removeDuplicates();
    return roots;
}

QString quotedArguments(const QStringList &arguments)
{
    QStringList escaped;
    escaped.reserve(arguments.size());
    for (QString argument : arguments) {
        argument.replace(u'"', QStringLiteral("\\\""));
        escaped.append(QStringLiteral("\"") + argument + QStringLiteral("\""));
    }
    return escaped.join(u' ');
}

bool capabilityAxesMatch(const std::array<bool, kVirtualAxisSlotCount> &have,
                           const std::array<bool, kVirtualAxisSlotCount> &need)
{
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        // A configured vJoy device may expose useful axes beyond the active
        // profile's current demand. Only a missing required capability makes
        // the descriptor insufficient; a capability superset is valid.
        if (need[static_cast<size_t>(index)] && !have[static_cast<size_t>(index)]) return false;
    }
    return true;
}

QString axisList(const std::array<bool, kVirtualAxisSlotCount> &axes)
{
    QStringList result;
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (axes[static_cast<size_t>(index)]) {
            result.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
        }
    }
    return result.isEmpty() ? QStringLiteral("none") : result.join(QStringLiteral(", "));
}

QString extraAxisList(const std::array<bool, kVirtualAxisSlotCount> &actual,
                      const std::array<bool, kVirtualAxisSlotCount> &expected)
{
    std::array<bool, kVirtualAxisSlotCount> extras{};
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        extras[static_cast<size_t>(index)] = actual[static_cast<size_t>(index)]
            && !expected[static_cast<size_t>(index)];
    }
    return axisList(extras);
}

template <typename Bindings>
int highestButton(const Bindings &bindings)
{
    int highest = 0;
    for (const ButtonBinding &binding : bindings) {
        if (binding.type == ButtonActionType::VirtualButton) highest = std::max(highest, binding.target);
    }
    return highest;
}

int highestPovButton(const PovBindings &bindings)
{
    int highest = 0;
    for (const auto &hat : bindings) highest = std::max(highest, highestButton(hat));
    return highest;
}

QString firstRegexCapture(const QString &input, const QRegularExpression &expression)
{
    const QRegularExpressionMatch match = expression.match(input);
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}

QString stateName(const VJoyCapabilities &vjoy)
{
    if (vjoy.ownedByHotasBf6) return QStringLiteral("owned by HOTAS BF6");
    if (vjoy.busy) return QStringLiteral("busy in another application");
    if (!vjoy.devicePresent) return QStringLiteral("not configured");
    return QStringLiteral("available");
}

QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::toNativeSeparators(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool samePath(const QString &left, const QString &right)
{
    return !left.isEmpty() && !right.isEmpty()
        && canonicalPath(left).compare(canonicalPath(right), Qt::CaseInsensitive) == 0;
}

bool isVJoyHidInstance(const QString &instanceId)
{
    return ControllerReadinessService::normalizeDeviceInstanceId(instanceId).startsWith(
        QStringLiteral("HID\\VID_1234&PID_BEAD\\"), Qt::CaseInsensitive);
}

QStringList selectedPhysicalHidInstances(const PhysicalControllerCapabilities &physical,
                                         const QStringList &gamingInstances)
{
    QStringList result;
    const auto append = [&result](const QString &value) {
        const QString normalized = ControllerReadinessService::normalizeDeviceInstanceId(value);
        if (!normalized.isEmpty() && !result.contains(normalized, Qt::CaseInsensitive)) result.append(normalized);
    };

    for (const QString &instance : physical.hidHideDeviceInstanceIds) {
        if (!isVJoyHidInstance(instance)) append(instance);
    }
    if (result.isEmpty() && !physical.hidContainerId.isEmpty()) {
        for (const QString &instance : gamingInstances) {
            if (isVJoyHidInstance(instance)) continue;
            const QString container = hidDeviceContainerId(instance);
            if (!container.isEmpty()
                && container.compare(physical.hidContainerId, Qt::CaseInsensitive) == 0) {
                append(instance);
            }
        }
    }
    // Exact current identity is a safe fallback only when Windows could not
    // provide a container-aware enumeration. It never broadens to VID/PID.
    if (result.isEmpty() && !physical.hidInstanceId.isEmpty() && !isVJoyHidInstance(physical.hidInstanceId)) {
        append(physical.hidInstanceId);
    }
    return result;
}

QString decodeProcessOutput(const QByteArray &bytes)
{
    if (bytes.size() >= 2 && bytes[0] == '\xff' && bytes[1] == '\xfe') {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2),
                                  (bytes.size() - 2) / 2);
    }
    if (bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes.contains('\0')) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    }
    return QString::fromLocal8Bit(bytes);
}

QString vJoyConfigurationAxisToken(VirtualAxis axis)
{
    // vJoyConfig's display spelling ("Slider 0") is not its command-line
    // spelling. Passing the display label makes an elevated repair appear to
    // run, but the driver rejects its required slider axes. Keep this adapter
    // at the driver boundary; UI labels remain human-readable everywhere else.
    switch (axis) {
    case VirtualAxis::Slider0: return QStringLiteral("Sl0");
    case VirtualAxis::Slider1: return QStringLiteral("Sl1");
    default: return virtualAxisLabel(axis);
    }
}

} // namespace

SetupProcessResult WindowsSetupProcessRunner::run(const QString &program, const QStringList &arguments,
                                                   int timeoutMs)
{
    SetupProcessResult result;
    if (!QFileInfo::exists(program)) {
        result.error = QStringLiteral("Utility was not found: %1").arg(program);
        return result;
    }
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    result.started = process.waitForStarted(std::min(timeoutMs, 3000));
    if (!result.started) {
        result.error = process.errorString();
        return result;
    }
    result.finished = process.waitForFinished(timeoutMs);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("Timed out after %1 ms").arg(timeoutMs);
        return result;
    }
    result.exitCode = process.exitCode();
    result.output = decodeProcessOutput(process.readAllStandardOutput());
    result.errorOutput = decodeProcessOutput(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit) result.error = QStringLiteral("Utility terminated unexpectedly");
    return result;
}

SetupProcessResult WindowsSetupProcessRunner::runElevated(const QString &program,
                                                           const QStringList &arguments,
                                                           int timeoutMs)
{
    SetupProcessResult result;
    if (!QFileInfo::exists(program)) {
        result.error = QStringLiteral("Utility was not found: %1").arg(program);
        return result;
    }

    const std::wstring file = QDir::toNativeSeparators(program).toStdWString();
    const std::wstring parameters = quotedArguments(arguments).toStdWString();
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = file.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError();
        result.windowsErrorCode = static_cast<int>(error);
        result.cancelled = error == ERROR_CANCELLED;
        result.error = error == ERROR_CANCELLED
            ? QStringLiteral("Administrator approval was cancelled")
            : QStringLiteral("Could not start elevated utility (Windows error %1)").arg(error);
        return result;
    }
    result.started = true;
    const DWORD wait = WaitForSingleObject(execute.hProcess, static_cast<DWORD>(timeoutMs));
    if (wait != WAIT_OBJECT_0) {
        result.error = wait == WAIT_TIMEOUT ? QStringLiteral("Elevated utility timed out")
                                          : QStringLiteral("Could not wait for elevated utility");
        CloseHandle(execute.hProcess);
        return result;
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(execute.hProcess, &exitCode);
    CloseHandle(execute.hProcess);
    result.finished = true;
    result.exitCode = static_cast<int>(exitCode);
    return result;
}

ControllerReadinessService::ControllerReadinessService(std::unique_ptr<SetupProcessRunner> runner,
                                                       SetupUtilityPaths utilityPaths)
    : m_runner(std::move(runner)), m_utilityPaths(std::move(utilityPaths))
{
    if (!m_runner) m_runner = std::make_unique<WindowsSetupProcessRunner>();
    // Test runners are intentionally ephemeral. Production keeps only this
    // narrow record until physical-controller proof or rollback completes.
    if (!m_utilityPaths.supplied) loadRecoveryJournal();
}

void ControllerReadinessService::persistRecoveryJournal() const
{
    if (m_utilityPaths.supplied || !m_journal.available) return;
    QJsonObject record{
        {QStringLiteral("vjoyChanged"), m_journal.vjoyChanged},
        {QStringLiteral("vjoyWasAbsent"), m_journal.vjoyWasAbsent},
        {QStringLiteral("mapperWasAdded"), m_journal.mapperWasAdded},
        {QStringLiteral("controllerWasHidden"), m_journal.controllerWasHidden},
        {QStringLiteral("cloakWasEnabled"), m_journal.cloakWasEnabled},
        {QStringLiteral("vjoyRestoreCommand"), m_journal.vjoyRestoreCommand},
        {QStringLiteral("mapperExecutable"), m_journal.mapperExecutable},
        {QStringLiteral("controllerInstanceIds"), QJsonArray::fromStringList(m_journal.controllerInstanceIds)},
    };
    QSettings settings;
    settings.setValue(QLatin1String(kPendingRecoveryKey), QJsonDocument(record).toJson(QJsonDocument::Compact));
    settings.sync();
}

void ControllerReadinessService::loadRecoveryJournal()
{
    QSettings settings;
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(QLatin1String(kPendingRecoveryKey)).toByteArray());
    if (!document.isObject()) return;
    const QJsonObject record = document.object();
    Journal recovered;
    recovered.vjoyChanged = record.value(QStringLiteral("vjoyChanged")).toBool();
    recovered.vjoyWasAbsent = record.value(QStringLiteral("vjoyWasAbsent")).toBool();
    recovered.mapperWasAdded = record.value(QStringLiteral("mapperWasAdded")).toBool();
    recovered.controllerWasHidden = record.value(QStringLiteral("controllerWasHidden")).toBool();
    recovered.cloakWasEnabled = record.value(QStringLiteral("cloakWasEnabled")).toBool();
    recovered.vjoyRestoreCommand = record.value(QStringLiteral("vjoyRestoreCommand")).toString();
    recovered.mapperExecutable = record.value(QStringLiteral("mapperExecutable")).toString();
    for (const QJsonValue &value : record.value(QStringLiteral("controllerInstanceIds")).toArray()) {
        const QString instance = normalizeDeviceInstanceId(value.toString());
        if (!instance.isEmpty() && !recovered.controllerInstanceIds.contains(instance, Qt::CaseInsensitive)) {
            recovered.controllerInstanceIds.append(instance);
        }
    }
    // Read the prior single-target journal once so an interrupted older
    // candidate remains safely reversible after upgrading.
    const QString legacyInstance = normalizeDeviceInstanceId(
        record.value(QStringLiteral("controllerInstanceId")).toString());
    if (!legacyInstance.isEmpty() && !recovered.controllerInstanceIds.contains(legacyInstance, Qt::CaseInsensitive)) {
        recovered.controllerInstanceIds.append(legacyInstance);
    }
    recovered.available = !recovered.controllerInstanceIds.isEmpty()
        && (recovered.vjoyChanged || recovered.mapperWasAdded || recovered.controllerWasHidden || recovered.cloakWasEnabled);
    if (recovered.available) m_journal = std::move(recovered);
    else clearRecoveryJournal();
}

void ControllerReadinessService::clearRecoveryJournal() const
{
    if (m_utilityPaths.supplied) return;
    QSettings settings;
    settings.remove(QLatin1String(kPendingRecoveryKey));
    settings.sync();
}

MapperOutputRequirements ControllerReadinessService::requirementsFor(const MapperConfiguration &configuration)
{
    MapperOutputRequirements requirements;
    const ControllerProfile *active = findProfile(configuration, configuration.activeProfileId);
    const VirtualOutputLayout *layout = active
        ? findOutputLayout(configuration, active->outputLayoutId) : nullptr;
    if (layout) {
        requirements = requirementsFor(layout->requirements);
        // Output-layout button counts are provisioned capacity, not a live
        // mapping requirement. Keep the exact layout axes, then derive the
        // minimum button floor from every profile that can switch onto this
        // same descriptor without a driver reconfiguration.
        requirements.buttons = 0;
        for (const ControllerProfile &profile : configuration.profiles) {
            if (profile.outputLayoutId != layout->id) continue;
            requirements.buttons = std::max(requirements.buttons, highestButton(profile.buttons));
            requirements.buttons = std::max(requirements.buttons, highestPovButton(profile.povs));
        }
    } else {
        // Pre-migration/malformed callers retain the old conservative route
        // union, but valid v2.0.10 configurations always take the exact
        // descriptor path above.
        for (const ControllerProfile &profile : configuration.profiles) {
            for (const AxisMapping &axis : profile.axes) {
                const int index = static_cast<int>(axis.target);
                if (index > 0 && index < kVirtualAxisSlotCount) requirements.axes[static_cast<size_t>(index)] = true;
            }
            requirements.buttons = std::max(requirements.buttons, highestButton(profile.buttons));
            requirements.buttons = std::max(requirements.buttons, highestPovButton(profile.povs));
        }
    }
    for (const NativePovBinding &binding : configuration.nativePovBindings) {
        if (!binding.enabled) continue;
        if (binding.targetType == NativePovTargetType::Continuous) {
            requirements.continuousPovs = std::max(requirements.continuousPovs, binding.targetIndex);
        } else if (binding.targetType == NativePovTargetType::Discrete) {
            requirements.discretePovs = std::max(requirements.discretePovs, binding.targetIndex);
        }
    }
    // Canonical Signal Flow can retain fan-out legs which intentionally do
    // not fit in the historical one-target compatibility projections.  The
    // active output contract must therefore derive its capability floor from
    // every enabled canonical sink in profiles that share this output layout.
    // This also makes imported topology safe when an older layout descriptor
    // did not yet record a secondary button, axis, or native POV endpoint.
    if (configuration.signalFlow.topologyVersion >= 1) {
        for (const SignalFlowRoute &route : configuration.signalFlow.routes) {
            if (!route.enabled) continue;
            const ControllerProfile *routeProfile = findProfile(configuration, route.profileId);
            if (!routeProfile || (layout && routeProfile->outputLayoutId != layout->id)) continue;
            if (route.destinationKind == SignalFlowPortKind::Axis
                && route.destinationIndex > 0 && route.destinationIndex < kVirtualAxisSlotCount) {
                requirements.axes[static_cast<size_t>(route.destinationIndex)] = true;
            } else if (route.destinationKind == SignalFlowPortKind::Button
                       && route.destinationIndex > 0) {
                requirements.buttons = std::max(requirements.buttons, route.destinationIndex);
            } else if (route.destinationKind == SignalFlowPortKind::NativePov
                       && route.destinationIndex > 0) {
                if (route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)) {
                    requirements.continuousPovs = std::max(requirements.continuousPovs,
                                                           route.destinationIndex);
                } else if (route.destinationSubIndex == static_cast<int>(NativePovTargetType::Discrete)) {
                    requirements.discretePovs = std::max(requirements.discretePovs,
                                                         route.destinationIndex);
                }
            }
        }
    }
    for (const AutomationDefinition &automation : configuration.automations) {
        if (!automation.enabled) continue;
        for (const AutomationActionDefinition &action : automation.actions) {
            if (action.type == AutomationActionType::VJoyButtonHold
                || action.type == AutomationActionType::VJoyButtonToggle
                || action.type == AutomationActionType::VJoyButtonTap) {
                requirements.buttons = std::max(requirements.buttons, action.virtualButton);
            }
        }
    }
    requirements.buttons = std::clamp(requirements.buttons, 0, kMaximumVirtualButtons);
    requirements.continuousPovs = std::clamp(requirements.continuousPovs, 0, 4);
    requirements.discretePovs = std::clamp(requirements.discretePovs, 0, 4);
    requirements.incompatiblePovMix = requirements.continuousPovs > 0 && requirements.discretePovs > 0;
    return requirements;
}

MapperOutputRequirements ControllerReadinessService::requirementsFor(
    const ControllerVJoyRequirements &requirements)
{
    MapperOutputRequirements result;
    result.axes = requirements.axes;
    result.buttons = std::clamp(requirements.buttons, 0, kMaximumVirtualButtons);
    result.continuousPovs = std::max(0, requirements.continuousPovs);
    result.discretePovs = std::max(0, requirements.discretePovs);
    result.incompatiblePovMix = result.continuousPovs > 0 && result.discretePovs > 0;
    return result;
}

bool ControllerReadinessService::isVJoySufficient(const VJoyCapabilities &vjoy,
                                                   const MapperOutputRequirements &requirements)
{
    // This is the configured-device/capability dimension only. Temporary
    // ownership is reported separately by planFor(): an otherwise-correct
    // vJoy device held by another process must never be diagnosed as needing
    // a destructive descriptor reconfiguration.
    return vjoy.installed && vjoy.configurationUtilityAvailable && vjoy.driverReady
        && vjoy.devicePresent
        && capabilityAxesMatch(vjoy.axes, requirements.axes)
        && vjoy.buttons >= requirements.buttons
        && vjoy.continuousPovs >= requirements.continuousPovs
        && vjoy.discretePovs >= requirements.discretePovs;
}

QString ControllerReadinessService::describeVJoyRequirement(const MapperOutputRequirements &requirements)
{
    return QStringLiteral("%1 axes; %2 buttons; %3 continuous / %4 discrete POV")
        .arg(axisList(requirements.axes)).arg(requirements.buttons)
        .arg(requirements.continuousPovs).arg(requirements.discretePovs);
}

ControllerReadinessPlan ControllerReadinessService::planFor(const PhysicalControllerCapabilities &physical,
                                                              const MapperOutputRequirements &requirements,
                                                              const VJoyCapabilities &vjoy,
                                                              const HidHideCapabilities &hidhide,
                                                              VerificationMode mode)
{
    MapperOutputRequirements effectiveRequirements = requirements;
    // The physical controller's exposed buttons are always part of the output
    // contract. Applying this only to an empty mapping let Verify Setup accept
    // an eight-button vJoy device while runtime correctly rejected it for a
    // fifteen-button HOTAS.
    if (physical.connected) {
        effectiveRequirements.buttons = std::max(effectiveRequirements.buttons,
            std::clamp(physical.buttons, 0, kMaximumVirtualButtons));
    }
    ControllerReadinessPlan plan;
    plan.physical = physical;
    plan.requirements = effectiveRequirements;
    plan.vjoy = vjoy;
    plan.hidhide = hidhide;
    plan.verificationMode = mode;
    plan.lastChecked = QDateTime::currentDateTime();

    if (!physical.connected) {
        plan.physicalStatus = VerificationSubsystemState::Error;
        plan.physicalSummary = QStringLiteral("Selected physical controller is not connected.");
        plan.findings.append(QStringLiteral("No physical DirectInput controller is detected."));
    } else {
        plan.physicalStatus = VerificationSubsystemState::Ready;
        plan.physicalSummary = physical.inputReportsReceived
            ? QStringLiteral("%1 — Connected · input reports received.").arg(physical.name)
            : QStringLiteral("%1 — Connected · mapper has the selected device open.").arg(physical.name);
        plan.findings.append(QStringLiteral("PHYSICAL CONTROLLER READY — %1 · %2 axes · %3 buttons · %4 POV%5")
            .arg(physical.name).arg(std::count(physical.axes.begin(), physical.axes.end(), true))
            .arg(physical.buttons).arg(physical.povs)
            .arg(physical.inputReportsReceived ? QStringLiteral(" · reports received") : QString{}));
    }

    if (effectiveRequirements.incompatiblePovMix) {
        plan.findings.append(QStringLiteral("Current profiles request both continuous and discrete native POV output. vJoyConfig configures one POV type at a time; resolve this in Advanced settings before automatic setup."));
    }
    plan.vjoyNeedsChanges = !isVJoySufficient(vjoy, effectiveRequirements);
    if (!plan.vjoyNeedsChanges) {
        const QString extras = extraAxisList(vjoy.axes, effectiveRequirements.axes);
        const QString capabilitySuffix = extras == QStringLiteral("none")
            ? QString{}
            : QStringLiteral(" Extra available axes: %1.").arg(extras);
        if (vjoy.busy && !vjoy.ownedByHotasBf6) {
            plan.vjoyStatus = VerificationSubsystemState::Attention;
            plan.vjoySummary = QStringLiteral("vJoy Device %1 — Busy in another application; its configured capabilities are correct.")
                .arg(vjoy.deviceId) + capabilitySuffix;
            plan.findings.append(plan.vjoySummary);
        } else {
            plan.vjoyStatus = VerificationSubsystemState::Ready;
            plan.vjoySummary = (vjoy.ownedByHotasBf6
            ? QStringLiteral("vJoy Device %1 — Ready · HOTAS BF6 currently owns this device.")
            : QStringLiteral("vJoy Device %1 — Ready · required capabilities present."))
                .arg(vjoy.deviceId) + capabilitySuffix;
            plan.findings.append(plan.vjoySummary);
        }
    } else if (!vjoy.installed || !vjoy.configurationUtilityAvailable) {
        plan.vjoyStatus = VerificationSubsystemState::Error;
        plan.vjoySummary = QStringLiteral("vJoy is unavailable — install or repair the vJoy driver.");
        plan.findings.append(QStringLiteral("VJOY NOT DETECTED — Install vJoy before configuring virtual output."));
    } else if (vjoy.busy && !vjoy.ownedByHotasBf6) {
        plan.vjoyStatus = VerificationSubsystemState::Attention;
        plan.vjoySummary = QStringLiteral("vJoy Device %1 — Busy in another application; release it before correcting its capabilities.")
            .arg(vjoy.deviceId);
        plan.findings.append(plan.vjoySummary);
    } else {
        plan.vjoyStatus = VerificationSubsystemState::Error;
        plan.vjoySummary = QStringLiteral("vJoy Device %1 needs the required output capabilities.")
            .arg(vjoy.deviceId);
        plan.findings.append(QStringLiteral("VJOY NEEDS CONFIGURATION — Device %1 is %2; HOTAS BF6 requires %3.")
            .arg(vjoy.deviceId).arg(stateName(vjoy)).arg(describeVJoyRequirement(effectiveRequirements)));
        if (vjoy.forceFeedbackKnown && (!vjoy.devicePresent || !vjoy.restoreCommand.isEmpty())
            && !effectiveRequirements.incompatiblePovMix) {
            plan.vjoyCanApply = true;
            plan.proposedChanges.append(QStringLiteral("Configure vJoy Device %1 for %2 while preserving its current Force Feedback setting.")
                .arg(vjoy.deviceId).arg(describeVJoyRequirement(effectiveRequirements)));
        } else if (!vjoy.forceFeedbackKnown || (vjoy.devicePresent && vjoy.restoreCommand.isEmpty())) {
            plan.findings.append(QStringLiteral("vJoy did not provide a complete restorable configuration snapshot, so automatic reconfiguration is withheld."));
        }
    }

    const bool hideRequired = physical.connected;
    plan.hidhideNeedsChanges = hideRequired && (!hidhide.cloakKnown || !hidhide.cloaked
        || !hidhide.mapperAllowlisted || !hidhide.selectedControllerHidden);
    if (!hideRequired && hidhide.installed && hidhide.cloakKnown && hidhide.cloaked
        && !hidhide.mapperAllowlisted) {
        plan.hidhideStatus = VerificationSubsystemState::Error;
        plan.hidhideSummary = QStringLiteral("HOTAS BF6 may be blocked by HidHide; its executable is not allowlisted.");
        plan.findings.append(QStringLiteral("HIDHIDE SELF-ACCESS BLOCKED — safely allowlist HOTAS BF6, then re-enumerate controllers."));
    } else if (!hideRequired) {
        plan.hidhideStatus = VerificationSubsystemState::Unknown;
        plan.hidhideSummary = QStringLiteral("No physical DirectInput devices detected. HidHide will be checked again when one is connected.");
        plan.findings.append(QStringLiteral("HIDHIDE WAITING — connect and select a physical controller before hiding an exact device instance."));
    } else if (!hidhide.installed || !hidhide.cliAvailable || !hidhide.serviceReady) {
        plan.hidhideStatus = VerificationSubsystemState::Attention;
        plan.hidhideSummary = QStringLiteral("HidHide is not available for optional physical-device isolation.");
        plan.findings.append(QStringLiteral("HIDHIDE NOT DETECTED — install HidHide for optional game-side physical-device hiding."));
    } else if (hidhide.cloakKnown && hidhide.cloaked && hidhide.mapperAllowlisted
               && hidhide.selectedControllerHidden) {
        plan.hidhideStatus = VerificationSubsystemState::Ready;
        plan.hidhideSummary = QStringLiteral("HidHide — Configured · physical controller is hidden and HOTAS BF6 is permitted.");
        plan.findings.append(QStringLiteral("HIDHIDE READY — HOTAS BF6 is allowed and the selected physical controller is hidden from ordinary applications."));
    } else if (!hidhide.selectedControllerResolved) {
        plan.hidhideStatus = VerificationSubsystemState::Attention;
        plan.hidhideSummary = QStringLiteral("HidHide — Verification incomplete · run full verification for the exact device check.");
        plan.findings.append(plan.hidhideSummary);
    } else if (!hidhide.cloakKnown) {
        plan.hidhideStatus = VerificationSubsystemState::Attention;
        plan.hidhideSummary = QStringLiteral("HidHide — Verification incomplete · driver state could not be read.");
        plan.findings.append(plan.hidhideSummary);
    } else if (hidhide.cloaked && !hidhide.mapperAllowlisted) {
        plan.hidhideStatus = VerificationSubsystemState::Error;
        plan.hidhideSummary = QStringLiteral("HidHide is blocking this running HOTAS BF6 executable from the selected physical device: %1")
            .arg(hidhide.mapperExecutable);
        plan.findings.append(plan.hidhideSummary);
        // This is the primary safe repair case: the exact running executable
        // can be allowlisted without touching any unrelated HidHide rule.
        plan.hidhideCanApply = true;
        plan.proposedChanges.append(QStringLiteral("Allow this running HOTAS BF6 executable through HidHide: %1")
            .arg(hidhide.mapperExecutable));
    } else {
        plan.hidhideStatus = VerificationSubsystemState::Attention;
        plan.hidhideSummary = QStringLiteral("HidHide needs configuration for the selected physical controller.");
        plan.hidhideCanApply = true;
        plan.findings.append(QStringLiteral("PHYSICAL CONTROLLER STILL VISIBLE TO GAMES — HOTAS BF6 can allow itself, hide only this controller, then enable cloaking."));
        if (!hidhide.mapperAllowlisted) {
            plan.proposedChanges.append(QStringLiteral("Allow this running HOTAS BF6 executable through HidHide: %1")
                .arg(hidhide.mapperExecutable));
        }
        if (!hidhide.selectedControllerHidden) plan.proposedChanges.append(QStringLiteral("Hide only %1 from ordinary applications.").arg(physical.name));
        if (!hidhide.cloaked) plan.proposedChanges.append(QStringLiteral("Enable HidHide cloaking after the mapper is allowlisted."));
    }

    if (physical.connected && physical.povs == 0
        && (effectiveRequirements.continuousPovs > 0 || effectiveRequirements.discretePovs > 0)) {
        plan.findings.append(QStringLiteral("The selected controller has no physical POV, so configured native POV routes will remain inactive until a capable controller is selected."));
    }
    plan.canApplyAutomatically = physical.connected && !effectiveRequirements.incompatiblePovMix
        && (!plan.vjoyNeedsChanges || plan.vjoyCanApply)
        && (!plan.hidhideNeedsChanges || plan.hidhideCanApply)
        && (plan.vjoyNeedsChanges || plan.hidhideNeedsChanges);
    const bool hasActionRequired = plan.physicalStatus == VerificationSubsystemState::Error
        || plan.vjoyStatus == VerificationSubsystemState::Error
        || plan.hidhideStatus == VerificationSubsystemState::Error;
    const bool hasAttention = plan.physicalStatus == VerificationSubsystemState::Attention
        || plan.vjoyStatus == VerificationSubsystemState::Attention
        || plan.hidhideStatus == VerificationSubsystemState::Attention;
    if (!hasActionRequired && !hasAttention && physical.connected) {
        plan.state = ControllerReadinessState::Ready;
        plan.status = QStringLiteral("READY — Your controller configuration is working correctly.");
    } else if (!hasActionRequired) {
        plan.state = ControllerReadinessState::Attention;
        plan.status = mode == VerificationMode::Quick
            ? QStringLiteral("ATTENTION — Mapping is functional, but one or more setup details need a full check.")
            : QStringLiteral("ATTENTION — Mapping is functional, but one or more setup details could not be confirmed.");
    } else {
        plan.state = ControllerReadinessState::NeedsChanges;
        plan.status = plan.canApplyAutomatically
            ? QStringLiteral("ACTION REQUIRED — A confirmed issue has a safe, scoped repair available.")
            : QStringLiteral("ACTION REQUIRED — Follow the setup instructions for the confirmed issue.");
    }
    return plan;
}

QString ControllerReadinessService::stateLabel(ControllerReadinessState state)
{
    switch (state) {
    case ControllerReadinessState::Idle: return QStringLiteral("IDLE");
    case ControllerReadinessState::Inspecting: return QStringLiteral("INSPECTING");
    case ControllerReadinessState::NeedsChanges: return QStringLiteral("NEEDS CHANGES");
    case ControllerReadinessState::AwaitingPermission: return QStringLiteral("AWAITING PERMISSION");
    case ControllerReadinessState::Applying: return QStringLiteral("APPLYING");
    case ControllerReadinessState::Verifying: return QStringLiteral("VERIFYING");
    case ControllerReadinessState::Ready: return QStringLiteral("READY");
    case ControllerReadinessState::Attention: return QStringLiteral("ATTENTION");
    case ControllerReadinessState::Failed: return QStringLiteral("FAILED");
    case ControllerReadinessState::Cancelled: return QStringLiteral("CANCELLED");
    case ControllerReadinessState::RollingBack: return QStringLiteral("ROLLING BACK");
    }
    return QStringLiteral("UNKNOWN");
}

QString ControllerReadinessService::subsystemStateLabel(VerificationSubsystemState state)
{
    switch (state) {
    case VerificationSubsystemState::Unknown: return QStringLiteral("UNKNOWN");
    case VerificationSubsystemState::Checking: return QStringLiteral("CHECKING");
    case VerificationSubsystemState::Ready: return QStringLiteral("READY");
    case VerificationSubsystemState::Attention: return QStringLiteral("ATTENTION");
    case VerificationSubsystemState::Error: return QStringLiteral("ACTION REQUIRED");
    }
    return QStringLiteral("UNKNOWN");
}

ControllerReadinessPlan ControllerReadinessService::checkingPlan(const PhysicalControllerCapabilities &physical,
                                                                  VerificationMode mode)
{
    ControllerReadinessPlan plan;
    plan.state = ControllerReadinessState::Inspecting;
    plan.verificationMode = mode;
    plan.isChecking = true;
    plan.physical = physical;
    plan.physicalStatus = VerificationSubsystemState::Checking;
    plan.vjoyStatus = VerificationSubsystemState::Checking;
    plan.hidhideStatus = VerificationSubsystemState::Checking;
    plan.physicalSummary = QStringLiteral("Checking physical controller…");
    plan.vjoySummary = QStringLiteral("Checking vJoy Device 1…");
    plan.hidhideSummary = QStringLiteral("Checking HidHide…");
    plan.status = mode == VerificationMode::Full
        ? QStringLiteral("VERIFYING — HOTAS BF6 is checking the complete controller chain.")
        : QStringLiteral("VERIFYING — Performing a passive startup check.");
    return plan;
}

bool ControllerReadinessService::needsSetupAfterControllerArrival(bool isNewPhysicalArrival,
                                                                   const ControllerReadinessPlan &plan)
{
    if (!isNewPhysicalArrival || !plan.physical.connected || plan.isChecking) return false;
    return plan.state == ControllerReadinessState::NeedsChanges
        || plan.state == ControllerReadinessState::Attention
        || plan.state == ControllerReadinessState::Failed;
}

bool ControllerReadinessService::isNewPhysicalControllerArrival(bool wasConnected, bool isConnected)
{
    return !wasConnected && isConnected;
}

QString ControllerReadinessService::normalizeDeviceInstanceId(QString value)
{
    value = value.trimmed().replace(u'/', u'\\');
    value.remove(QStringLiteral("\\\\?\\"), Qt::CaseInsensitive);
    const int classSeparator = value.indexOf(QStringLiteral("#{"));
    if (classSeparator >= 0) value.truncate(classSeparator);
    value = value.replace(u'#', u'\\').toUpper();
    while (value.startsWith(u'\\')) value.remove(0, 1);
    return value;
}

bool ControllerReadinessService::samePhysicalController(const PhysicalControllerCapabilities &expected,
                                                          const PhysicalControllerCapabilities &observed)
{
    if (!expected.hidContainerId.isEmpty() && !observed.hidContainerId.isEmpty()) {
        return expected.hidContainerId.compare(observed.hidContainerId, Qt::CaseInsensitive) == 0;
    }
    if (!expected.hidInstanceId.isEmpty() && !observed.hidInstanceId.isEmpty()) {
        return normalizeDeviceInstanceId(expected.hidInstanceId)
            == normalizeDeviceInstanceId(observed.hidInstanceId);
    }
    return !expected.directInputId.isEmpty() && !observed.directInputId.isEmpty()
        && expected.directInputId.compare(observed.directInputId, Qt::CaseInsensitive) == 0;
}

bool ControllerReadinessService::isKnownPhysicalController(
    const PhysicalControllerCapabilities &physical,
    const std::vector<SavedControllerRecord> &records)
{
    if (!physical.connected) return false;
    return std::any_of(records.cbegin(), records.cend(), [&physical](const SavedControllerRecord &record) {
        PhysicalControllerCapabilities remembered;
        remembered.directInputId = record.lastDirectInputId;
        remembered.hidInstanceId = record.hidInstanceId;
        remembered.hidContainerId = record.hidContainerId;
        return samePhysicalController(remembered, physical);
    });
}

QString ControllerReadinessService::decodeOutput(const QByteArray &bytes)
{
    if (bytes.size() >= 2 && bytes[0] == '\xff' && bytes[1] == '\xfe') {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2),
                                  (bytes.size() - 2) / 2);
    }
    // HidHideCLI writes UTF-16 through std::wcout when its output is redirected.
    if (bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes.contains('\0')) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    }
    return QString::fromLocal8Bit(bytes);
}

QString ControllerReadinessService::findVJoyConfig()
{
    for (const QString &root : installRoots()) {
        const QString candidate = QDir(root).filePath(QStringLiteral("vJoy/x64/vJoyConfig.exe"));
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return {};
}

QString ControllerReadinessService::findVJoyConf()
{
    for (const QString &root : installRoots()) {
        const QString candidate = QDir(root).filePath(QStringLiteral("vJoy/x64/vJoyConf.exe"));
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return {};
}

QString ControllerReadinessService::findHidHideCli()
{
    for (const QString &root : installRoots()) {
        const QString candidate = QDir(root).filePath(
            QStringLiteral("Nefarius Software Solutions/HidHide/x64/HidHideCLI.exe"));
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return {};
}

QString ControllerReadinessService::findHidHideClient()
{
    for (const QString &root : installRoots()) {
        const QString candidate = QDir(root).filePath(
            QStringLiteral("Nefarius Software Solutions/HidHide/x64/HidHideClient.exe"));
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return {};
}

bool ControllerReadinessService::hasHidHideService()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, L"HidHide", SERVICE_QUERY_STATUS);
    const bool available = service != nullptr;
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return available;
}

VJoyCapabilities ControllerReadinessService::parseVJoyReport(const QString &report, int deviceId)
{
    VJoyCapabilities result;
    result.deviceId = deviceId;
    const QString lower = report.toLower();
    result.reportValid = !report.trimmed().isEmpty();
    result.installed = result.reportValid;
    result.configurationUtilityAvailable = result.reportValid;
    // vJoy 2.1.9 emits the supported human-readable form "Device 1 FREE"
    // (without colons), while earlier releases used "Device: 1". Treat both
    // formats as descriptors; otherwise a healthy native installation is
    // falsely read as an absent zero-button device.
    const QRegularExpression deviceLine(QStringLiteral("(?im)\\bdevice\\s*:?[\\t ]*%1\\b")
        .arg(deviceId));
    result.busy = deviceLine.match(report).hasMatch() && lower.contains(QStringLiteral("busy"));
    result.devicePresent = deviceLine.match(report).hasMatch()
        && !lower.contains(QStringLiteral("does not exist")) && !lower.contains(QStringLiteral("not configured"));
    result.driverReady = result.devicePresent && !lower.contains(QStringLiteral("driver is disabled"))
        && !lower.contains(QStringLiteral("not enabled"));
    const QString buttons = firstRegexCapture(report, QRegularExpression(QStringLiteral("(?im)buttons\\s*:?[\\t ]*(\\d+)")));
    result.buttons = buttons.toInt();
    const QString continuous = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)contin(?:u|o)ous\\s+POVs?\\s*:?[\\t ]*(\\d+)")));
    result.continuousPovs = continuous.toInt();
    const QString discrete = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)desc(?:r|re)ete\\s+POVs?\\s*:?[\\t ]*(\\d+)")));
    result.discretePovs = discrete.toInt();
    const QString axes = firstRegexCapture(report, QRegularExpression(QStringLiteral("(?im)axes\\s*:?[\\t ]*([^\\r\\n]+)")));
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const VirtualAxis axis = static_cast<VirtualAxis>(index);
        const QString name = virtualAxisLabel(axis);
        QString compactName = name;
        compactName.remove(u' ');
        // vJoyConfig's own report spells its two sliders "Sl0" and "Sl1"
        // on some releases, while configuration arguments and the UI use
        // "Slider 0" / "Slider 1". Treat those as the same authoritative
        // capability; otherwise a valid descriptor gets reconfigured and
        // repeatedly fails convergence against its unchanged report.
        QStringList aliases{name, compactName};
        if (axis == VirtualAxis::Slider0) aliases.append(QStringLiteral("Sl0"));
        if (axis == VirtualAxis::Slider1) aliases.append(QStringLiteral("Sl1"));
        result.axes[static_cast<size_t>(index)] = std::any_of(aliases.cbegin(), aliases.cend(), [&axes](const QString &alias) {
            return axes.contains(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(alias)),
                QRegularExpression::CaseInsensitiveOption));
        });
    }
    const QString ffb = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)FFB(?:\\s+All)?\\s+Effects?\\s*:?[\\t ]*([^\\r\\n]*)")));
    if (!ffb.isEmpty()) {
        result.forceFeedbackKnown = true;
        if (ffb.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
            result.forceFeedbackEffects = ffb.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
        }
    } else if (lower.contains(QStringLiteral("ffb all effects"))) {
        // This exact spelling has no delimiter or payload in vJoy 2.1.9.
        result.forceFeedbackKnown = true;
        result.forceFeedbackEffects = {QStringLiteral("all")};
    }
    result.diagnostic = report.trimmed();
    return result;
}

QStringList ControllerReadinessService::parseHidHideCommands(const QString &output, const QString &command)
{
    QStringList result;
    const QRegularExpression expression(QStringLiteral("(?im)^\\s*--%1\\s+\\\"([^\\\"]+)\\\"")
        .arg(QRegularExpression::escape(command)));
    QRegularExpressionMatchIterator iterator = expression.globalMatch(output);
    while (iterator.hasNext()) result.append(iterator.next().captured(1));
    return result;
}

QStringList ControllerReadinessService::parseHidHideGamingDevices(const QString &output)
{
    QStringList result;
    const QRegularExpression expression(QStringLiteral("(?im)(HID\\\\[^\\r\\n\\\"]+)"));
    QRegularExpressionMatchIterator iterator = expression.globalMatch(output);
    while (iterator.hasNext()) {
        const QString instance = normalizeDeviceInstanceId(iterator.next().captured(1));
        if (!instance.isEmpty() && !result.contains(instance, Qt::CaseInsensitive)) result.append(instance);
    }
    return result;
}

bool ControllerReadinessService::outputContainsDevice(const QString &output, const QString &instanceId)
{
    if (instanceId.isEmpty()) return false;
    const QString normalizedNeedle = normalizeDeviceInstanceId(instanceId);
    const QString normalizedOutput = normalizeDeviceInstanceId(output);
    return normalizedOutput.contains(normalizedNeedle, Qt::CaseInsensitive);
}

VJoyCapabilities ControllerReadinessService::inspectVJoy(int deviceId) const
{
    VJoyCapabilities result;
    result.deviceId = deviceId;
    const QString utility = vjoyConfigPath();
    if (utility.isEmpty()) {
        result.diagnostic = QStringLiteral("vJoyConfig.exe was not found");
        return result;
    }
    result.installed = true;
    result.configurationUtilityAvailable = true;
    const SetupProcessResult report = runVJoy(false, {QStringLiteral("-t"), QString::number(deviceId)});
    if (!report.started) {
        result.diagnostic = report.error;
        return result;
    }
    result = parseVJoyReport(report.output, deviceId);
    result.descriptorReport = report.output;
    result.installed = true;
    result.configurationUtilityAvailable = true;
    if (!report.succeeded() && result.diagnostic.isEmpty()) result.diagnostic = report.error;

    // -t -c emits an executable vJoyConfig command that can be used as a
    // narrow device-1 snapshot for rollback. Never use vJoyConfig -r.
    const SetupProcessResult command = runVJoy(false,
        {QStringLiteral("-t"), QStringLiteral("-c"), QString::number(deviceId)});
    result.configurationReport = command.output;
    if (command.succeeded()) {
        const QRegularExpression configurationLine(QStringLiteral("(?im)^\\s*vJoyConfig\\s+(.+)$"));
        const QRegularExpressionMatch match = configurationLine.match(command.output);
        if (match.hasMatch()) result.restoreCommand = match.captured(1).trimmed();
    } else {
        result.diagnostic += QStringLiteral("\nvJoyConfig -t -c %1 failed: %2").arg(deviceId).arg(
            command.error.isEmpty() ? command.output.trimmed() : command.error);
    }
    const SetupProcessResult devices = runVJoy(false, {QStringLiteral("-t")});
    result.deviceListReport = devices.output;
    if (devices.succeeded()) {
        const QRegularExpression deviceLine(QStringLiteral("(?im)^\\s*Device\\s*:?[\\t ]*(\\d+)"));
        QRegularExpressionMatchIterator iterator = deviceLine.globalMatch(devices.output);
        while (iterator.hasNext()) result.availableDeviceIds.append(iterator.next().captured(1).toInt());
        std::sort(result.availableDeviceIds.begin(), result.availableDeviceIds.end());
        result.availableDeviceIds.erase(
            std::unique(result.availableDeviceIds.begin(), result.availableDeviceIds.end()),
            result.availableDeviceIds.end());
    } else {
        result.diagnostic += QStringLiteral("\nvJoyConfig -t failed: %1").arg(
            devices.error.isEmpty() ? devices.output.trimmed() : devices.error);
    }
    if (!result.devicePresent) {
        // There is no descriptor to preserve. A deletion of this newly created
        // device is the exact rollback snapshot for a previously absent target.
        result.forceFeedbackKnown = true;
        result.inspectionComplete = report.succeeded() && command.succeeded() && devices.succeeded();
        return result;
    }
    if (result.forceFeedbackKnown) {
        result.inspectionComplete = report.succeeded() && command.succeeded() && devices.succeeded();
        return result;
    }
    // An absent FFB line is only safe when the report explicitly says no FFB.
    result.forceFeedbackKnown = report.output.contains(QStringLiteral("FFB Effects"), Qt::CaseInsensitive)
        && report.output.contains(QStringLiteral("None"), Qt::CaseInsensitive);
    result.inspectionComplete = report.succeeded() && command.succeeded() && devices.succeeded();
    return result;
}

HidHideCapabilities ControllerReadinessService::inspectHidHide(const PhysicalControllerCapabilities &physical) const
{
    HidHideCapabilities result;
    const QString cli = hidhideCliPath();
    result.cliAvailable = !cli.isEmpty();
    result.serviceReady = hidhideServiceReady();
    result.installed = result.cliAvailable && result.serviceReady;
    if (!result.installed) {
        result.diagnostic = result.cliAvailable ? QStringLiteral("HidHide service is unavailable")
                                                : QStringLiteral("HidHideCLI.exe was not found");
        return result;
    }
    const SetupProcessResult cloak = runHidHide(false, {QStringLiteral("--cloak-state")});
    result.cloakReport = cloak.output;
    if (cloak.succeeded()) {
        result.cloakKnown = true;
        result.cloaked = cloak.output.contains(QStringLiteral("--cloak-on"), Qt::CaseInsensitive);
    } else {
        result.diagnostic = cloak.error.isEmpty() ? cloak.output.trimmed() : cloak.error;
        result.inspectionFailures.append(QStringLiteral("--cloak-state: %1").arg(result.diagnostic));
    }
    result.mapperExecutable = mapperExecutablePath();
    const SetupProcessResult apps = runHidHide(false, {QStringLiteral("--app-list")});
    result.appListReport = apps.output;
    if (apps.succeeded()) {
        result.allowlistedApplications = parseHidHideCommands(apps.output, QStringLiteral("app-reg"));
        result.mapperAllowlisted = std::any_of(result.allowlistedApplications.cbegin(),
            result.allowlistedApplications.cend(), [&result](const QString &entry) {
                return samePath(entry, result.mapperExecutable);
            });
    } else {
        result.inspectionFailures.append(QStringLiteral("--app-list: %1").arg(
            apps.error.isEmpty() ? apps.output.trimmed() : apps.error));
    }
    QStringList gamingDevices;
    const SetupProcessResult devices = runHidHide(false, {QStringLiteral("--dev-gaming")});
    result.gamingDevicesReport = devices.output;
    if (devices.succeeded()) gamingDevices = parseHidHideGamingDevices(devices.output);
    else result.inspectionFailures.append(QStringLiteral("--dev-gaming: %1").arg(
        devices.error.isEmpty() ? devices.output.trimmed() : devices.error));
    result.selectedControllerInstanceIds = selectedPhysicalHidInstances(physical, gamingDevices);
    result.selectedControllerResolved = !result.selectedControllerInstanceIds.isEmpty();
    const SetupProcessResult hidden = runHidHide(false, {QStringLiteral("--dev-list")});
    result.deviceListReport = hidden.output;
    if (hidden.succeeded()) {
        result.hiddenDeviceInstanceIds = parseHidHideCommands(hidden.output, QStringLiteral("dev-hide"));
        result.selectedControllerHidden = !result.selectedControllerInstanceIds.isEmpty()
            && std::all_of(result.selectedControllerInstanceIds.cbegin(),
                           result.selectedControllerInstanceIds.cend(), [&result](const QString &selected) {
                return std::any_of(result.hiddenDeviceInstanceIds.cbegin(), result.hiddenDeviceInstanceIds.cend(),
                    [&selected](const QString &entry) {
                        return normalizeDeviceInstanceId(entry) == normalizeDeviceInstanceId(selected);
                    });
            });
        if (!result.selectedControllerHidden && result.selectedControllerResolved) {
            result.diagnostic = QStringLiteral("One or more current HID collections for the selected controller are not cloaked.");
        }
    } else {
        result.inspectionFailures.append(QStringLiteral("--dev-list: %1").arg(
            hidden.error.isEmpty() ? hidden.output.trimmed() : hidden.error));
    }
    result.inspectionComplete = cloak.succeeded() && apps.succeeded() && devices.succeeded() && hidden.succeeded();
    if (!result.inspectionFailures.isEmpty()) result.diagnostic = result.inspectionFailures.join(QStringLiteral("\n"));
    return result;
}

OutputVisibilitySwitchResult ControllerReadinessService::applyManagedOutputVisibility(
    const MapperConfiguration &configuration, const QString &activeLayoutId) const
{
    OutputVisibilitySwitchResult result;
    const QString cli = hidhideCliPath();
    if (cli.isEmpty() || !hidhideServiceReady()) {
        result.status = QStringLiteral("HidHide runtime visibility switching is unavailable on this installation; managed outputs were left unchanged.");
        return result;
    }

    std::vector<const VirtualOutputLayout *> managed;
    for (const VirtualOutputLayout &layout : configuration.outputLayouts) {
        if (layout.hidhideManaged && !layout.hidHideDeviceInstanceId.trimmed().isEmpty()) {
            managed.push_back(&layout);
        }
    }
    if (managed.empty()) {
        result.status = QStringLiteral("No virtual outputs have explicit HidHide device identities; visibility was left unchanged.");
        return result;
    }

    const SetupProcessResult cloak = runHidHide(false, {QStringLiteral("--cloak-state")});
    const SetupProcessResult apps = runHidHide(false, {QStringLiteral("--app-list")});
    const SetupProcessResult hidden = runHidHide(false, {QStringLiteral("--dev-list")});
    if (!cloak.succeeded() || !apps.succeeded() || !hidden.succeeded()) {
        result.status = QStringLiteral("HidHide did not provide a readable runtime configuration; visibility was left unchanged.");
        return result;
    }
    const QString mapperPath = mapperExecutablePath();
    const QStringList appEntries = parseHidHideCommands(apps.output, QStringLiteral("app-reg"));
    const bool mapperAllowed = std::any_of(appEntries.cbegin(), appEntries.cend(), [&mapperPath](const QString &entry) {
            return samePath(entry, mapperPath);
        });
    if (!cloak.output.contains(QStringLiteral("--cloak-on"), Qt::CaseInsensitive) || !mapperAllowed) {
        result.status = QStringLiteral("HidHide visibility switching requires existing cloaking and a HOTAS BF6 allowlist entry; no runtime change was made.");
        return result;
    }

    const QStringList hiddenDevices = parseHidHideCommands(hidden.output, QStringLiteral("dev-hide"));
    struct Change { QString instance; bool wasHidden = false; bool hide = false; };
    std::vector<Change> changes;
    changes.reserve(managed.size());
    for (const VirtualOutputLayout *layout : managed) {
        const QString normalized = normalizeDeviceInstanceId(layout->hidHideDeviceInstanceId);
        const bool wasHidden = std::any_of(hiddenDevices.cbegin(), hiddenDevices.cend(),
            [&normalized](const QString &entry) { return normalizeDeviceInstanceId(entry) == normalized; });
        const bool shouldHide = layout->id != activeLayoutId;
        if (wasHidden != shouldHide) changes.push_back({layout->hidHideDeviceInstanceId, wasHidden, shouldHide});
    }
    result.available = true;
    if (changes.empty()) {
        result.status = QStringLiteral("Managed virtual-output visibility already matches the selected layout.");
        return result;
    }

    std::vector<Change> completed;
    completed.reserve(changes.size());
    // Hide inactive outputs before exposing the selected one, so normal games
    // never see two incompatible managed descriptors during this transition.
    std::stable_sort(changes.begin(), changes.end(), [](const Change &left, const Change &right) {
        return left.hide && !right.hide;
    });
    for (const Change &change : changes) {
        const SetupProcessResult operation = runHidHide(false, {change.hide
            ? QStringLiteral("--dev-hide") : QStringLiteral("--dev-unhide"), change.instance});
        if (operation.succeeded()) {
            completed.push_back(change);
            continue;
        }
        for (auto rollback = completed.crbegin(); rollback != completed.crend(); ++rollback) {
            runHidHide(false, {rollback->wasHidden ? QStringLiteral("--dev-hide")
                                                   : QStringLiteral("--dev-unhide"), rollback->instance});
        }
        result.succeeded = false;
        result.status = QStringLiteral("HidHide could not switch managed virtual-output visibility; completed changes were rolled back and mapping remains on the previous output.");
        return result;
    }
    result.changed = true;
    result.status = QStringLiteral("Managed virtual-output visibility switched without administrator elevation.");
    return result;
}

bool ControllerReadinessService::validateManagedVirtualOutputIdentity(const QString &instanceId,
                                                                         QString *normalizedInstanceId,
                                                                         QString *status) const
{
    if (normalizedInstanceId) normalizedInstanceId->clear();
    const QString normalized = normalizeDeviceInstanceId(instanceId);
    if (!normalized.startsWith(QStringLiteral("HID\\VID_1234&PID_BEAD\\"), Qt::CaseInsensitive)) {
        if (status) *status = QStringLiteral("Enter the exact HID instance for a vJoy device; display names are not accepted.");
        return false;
    }
    if (hidhideCliPath().isEmpty() || !hidhideServiceReady()) {
        if (status) *status = QStringLiteral("HidHide is not ready, so this virtual output cannot be prepared for visibility management.");
        return false;
    }
    const SetupProcessResult gaming = runHidHide(false, {QStringLiteral("--dev-gaming")});
    if (!gaming.succeeded()) {
        if (status) *status = QStringLiteral("HidHide could not read currently enumerated gaming devices; no visibility identity was recorded.");
        return false;
    }
    const QRegularExpression vjoyInstancePattern(
        QStringLiteral("(?im)HID\\\\VID_1234&PID_BEAD\\\\[^\\r\\n\\\"]+"));
    bool exactEnumeratedIdentity = false;
    QRegularExpressionMatchIterator matches = vjoyInstancePattern.globalMatch(gaming.output);
    while (matches.hasNext()) {
        if (normalizeDeviceInstanceId(matches.next().captured()) == normalized) {
            exactEnumeratedIdentity = true;
            break;
        }
    }
    if (!exactEnumeratedIdentity) {
        if (status) *status = QStringLiteral("The supplied vJoy HID instance is not currently enumerated by HidHide; no visibility identity was recorded.");
        return false;
    }
    if (normalizedInstanceId) *normalizedInstanceId = normalized;
    if (status) *status = QStringLiteral("Exact vJoy HID identity verified for non-elevated visibility switching.");
    return true;
}

bool ControllerReadinessService::validateManagedPhysicalInputIdentities(const QStringList &instanceIds,
                                                                         QStringList *normalizedInstanceIds,
                                                                         QString *status) const
{
    if (normalizedInstanceIds) normalizedInstanceIds->clear();
    if (instanceIds.isEmpty()) {
        if (status) *status = QStringLiteral("Select at least one saved physical input before changing game visibility.");
        return false;
    }
    if (hidhideCliPath().isEmpty() || !hidhideServiceReady()) {
        if (status) *status = QStringLiteral("HidHide is not ready, so physical-input visibility was left unchanged.");
        return false;
    }
    const SetupProcessResult gaming = runHidHide(false, {QStringLiteral("--dev-gaming")});
    if (!gaming.succeeded()) {
        if (status) *status = QStringLiteral("HidHide could not read currently enumerated gaming devices; physical-input visibility was left unchanged.");
        return false;
    }
    const QStringList enumerated = parseHidHideGamingDevices(gaming.output);
    QStringList normalized;
    for (const QString &value : instanceIds) {
        const QString instance = normalizeDeviceInstanceId(value);
        // Physical input actions may never infer an identity from a friendly
        // name, nor may they touch vJoy's known virtual HID namespace.
        if (!instance.startsWith(QStringLiteral("HID\\"), Qt::CaseInsensitive)
            || instance.startsWith(QStringLiteral("HID\\VID_1234&PID_BEAD\\"), Qt::CaseInsensitive)) {
            if (status) *status = QStringLiteral("Only exact, non-vJoy HID instances saved for this Device Rig may be changed.");
            return false;
        }
        const bool found = std::any_of(enumerated.cbegin(), enumerated.cend(), [&instance](const QString &entry) {
            return normalizeDeviceInstanceId(entry) == instance;
        });
        if (!found) {
            if (status) *status = QStringLiteral("A saved physical HID identity is not currently enumerated by HidHide; no visibility change was made.");
            return false;
        }
        if (!normalized.contains(instance, Qt::CaseInsensitive)) normalized.append(instance);
    }
    if (normalized.isEmpty()) {
        if (status) *status = QStringLiteral("No exact physical HID identity was available for this Device Rig.");
        return false;
    }
    if (normalizedInstanceIds) *normalizedInstanceIds = normalized;
    if (status) *status = QStringLiteral("Exact physical HID identities verified for managed visibility switching.");
    return true;
}

ManagedVisibilityTransactionResult ControllerReadinessService::applyManagedPhysicalInputVisibility(
    const QStringList &instanceIds, bool hidden) const
{
    ManagedVisibilityTransactionResult result;
    const auto details = [hidden](const QString &transaction, int exitCode,
                                  const QString &readback, bool rollback) {
        return QStringLiteral("REQUESTED ACTION\n%1\n\nCOMMAND/TRANSACTION RESULT\n%2\n\nEXIT CODE\n%3\n\nREADBACK RESULT\n%4\n\nROLLBACK\n%5")
            .arg(hidden ? QStringLiteral("Hide from games") : QStringLiteral("Show to games"),
                 transaction, exitCode < 0 ? QStringLiteral("Not run") : QString::number(exitCode),
                 readback, rollback ? QStringLiteral("Completed changes were rolled back")
                           : QStringLiteral("Not needed"));
    };
    QStringList normalized;
    QString validation;
    if (!validateManagedPhysicalInputIdentities(instanceIds, &normalized, &validation)) {
        result.status = validation;
        result.technicalDetails = details(validation, -1, QStringLiteral("Exact identity validation failed."), false);
        return result;
    }

    const SetupProcessResult cloak = runHidHide(false, {QStringLiteral("--cloak-state")});
    const SetupProcessResult apps = runHidHide(false, {QStringLiteral("--app-list")});
    const SetupProcessResult current = runHidHide(false, {QStringLiteral("--dev-list")});
    if (!cloak.succeeded() || !apps.succeeded() || !current.succeeded()) {
        result.status = QStringLiteral("HidHide did not provide a readable runtime configuration; physical-input visibility was left unchanged.");
        result.exitCode = !cloak.succeeded() ? cloak.exitCode : !apps.succeeded() ? apps.exitCode : current.exitCode;
        result.technicalDetails = details(result.status, result.exitCode,
            QStringLiteral("HidHide preflight read-back was unavailable."), false);
        return result;
    }
    const QString mapperPath = mapperExecutablePath();
    const QStringList appEntries = parseHidHideCommands(apps.output, QStringLiteral("app-reg"));
    const bool mapperAllowed = std::any_of(appEntries.cbegin(), appEntries.cend(), [&mapperPath](const QString &entry) {
        return samePath(entry, mapperPath);
    });
    if (!cloak.output.contains(QStringLiteral("--cloak-on"), Qt::CaseInsensitive) || !mapperAllowed) {
        result.status = QStringLiteral("Physical-input visibility requires enabled HidHide cloaking and a HOTAS BF6 allowlist entry; no runtime change was made.");
        result.technicalDetails = details(result.status, 0,
            QStringLiteral("Cloaking or mapper allowlist prerequisite was not satisfied."), false);
        return result;
    }

    const QStringList hiddenDevices = parseHidHideCommands(current.output, QStringLiteral("dev-hide"));
    struct Change { QString instance; bool wasHidden = false; };
    std::vector<Change> changes;
    changes.reserve(static_cast<size_t>(normalized.size()));
    for (const QString &instance : normalized) {
        const bool wasHidden = std::any_of(hiddenDevices.cbegin(), hiddenDevices.cend(), [&instance](const QString &entry) {
            return normalizeDeviceInstanceId(entry) == instance;
        });
        if (wasHidden != hidden) changes.push_back({instance, wasHidden});
    }
    result.available = true;
    if (changes.empty()) {
        result.status = hidden ? QStringLiteral("Selected managed physical inputs are already hidden from games.")
                               : QStringLiteral("Selected managed physical inputs are already visible to games.");
        result.technicalDetails = details(result.status, 0, result.status, false);
        return result;
    }

    std::vector<Change> completed;
    completed.reserve(changes.size());
    for (const Change &change : changes) {
        const SetupProcessResult operation = runHidHide(false, {hidden
            ? QStringLiteral("--dev-hide") : QStringLiteral("--dev-unhide"), change.instance});
        if (operation.succeeded()) {
            completed.push_back(change);
            continue;
        }
        for (auto rollback = completed.crbegin(); rollback != completed.crend(); ++rollback) {
            runHidHide(false, {rollback->wasHidden ? QStringLiteral("--dev-hide")
                                                   : QStringLiteral("--dev-unhide"), rollback->instance});
        }
        result.succeeded = false;
        result.rollbackOccurred = !completed.empty();
        result.exitCode = operation.exitCode;
        result.status = QStringLiteral("HidHide could not change the selected physical-input visibility; completed changes were rolled back.");
        result.technicalDetails = details(result.status, operation.exitCode,
            QStringLiteral("The requested device state was not accepted."), result.rollbackOccurred);
        return result;
    }
    const SetupProcessResult readback = runHidHide(false, {QStringLiteral("--dev-list")});
    const QStringList observedHidden = readback.succeeded()
        ? parseHidHideCommands(readback.output, QStringLiteral("dev-hide")) : QStringList{};
    const bool readbackMatches = readback.succeeded() && std::all_of(normalized.cbegin(), normalized.cend(),
        [&observedHidden, hidden](const QString &instance) {
            const bool observed = std::any_of(observedHidden.cbegin(), observedHidden.cend(), [&instance](const QString &entry) {
                return ControllerReadinessService::normalizeDeviceInstanceId(entry) == instance;
            });
            return observed == hidden;
        });
    if (!readbackMatches) {
        for (auto rollback = completed.crbegin(); rollback != completed.crend(); ++rollback) {
            runHidHide(false, {rollback->wasHidden ? QStringLiteral("--dev-hide")
                                                   : QStringLiteral("--dev-unhide"), rollback->instance});
        }
        result.succeeded = false;
        result.rollbackOccurred = !completed.empty();
        result.exitCode = readback.exitCode;
        result.status = QStringLiteral("HidHide did not confirm the requested physical-input visibility; completed changes were rolled back.");
        result.technicalDetails = details(result.status, readback.exitCode,
            readback.succeeded() ? QStringLiteral("Read-back did not match the requested hidden state.")
                               : QStringLiteral("HidHide read-back command failed."), result.rollbackOccurred);
        return result;
    }
    result.changed = true;
    result.status = hidden ? QStringLiteral("Selected managed physical inputs are now hidden from games; rechecking HidHide state.")
                           : QStringLiteral("Selected managed physical inputs are now visible to games; rechecking HidHide state.");
    result.exitCode = 0;
    result.technicalDetails = details(result.status, 0,
        QStringLiteral("Exact HID identities match the requested visibility state."), false);
    return result;
}

ManagedVisibilityTransactionResult ControllerReadinessService::applyManagedVirtualOutputVisibility(
    const QStringList &instanceIds, bool hidden) const
{
    ManagedVisibilityTransactionResult result;
    if (instanceIds.isEmpty()) {
        result.status = QStringLiteral("Select at least one adopted virtual output before changing game visibility.");
        return result;
    }
    QStringList normalized;
    for (const QString &instance : instanceIds) {
        QString value;
        QString validation;
        if (!validateManagedVirtualOutputIdentity(instance, &value, &validation)) {
            result.status = validation;
            return result;
        }
        if (!normalized.contains(value, Qt::CaseInsensitive)) normalized.append(value);
    }
    const SetupProcessResult cloak = runHidHide(false, {QStringLiteral("--cloak-state")});
    const SetupProcessResult apps = runHidHide(false, {QStringLiteral("--app-list")});
    const SetupProcessResult current = runHidHide(false, {QStringLiteral("--dev-list")});
    if (!cloak.succeeded() || !apps.succeeded() || !current.succeeded()) {
        result.status = QStringLiteral("HidHide did not provide a readable runtime configuration; virtual-output visibility was left unchanged.");
        return result;
    }
    const QString mapperPath = mapperExecutablePath();
    const QStringList appEntries = parseHidHideCommands(apps.output, QStringLiteral("app-reg"));
    const bool mapperAllowed = std::any_of(appEntries.cbegin(), appEntries.cend(), [&mapperPath](const QString &entry) {
        return samePath(entry, mapperPath);
    });
    if (!cloak.output.contains(QStringLiteral("--cloak-on"), Qt::CaseInsensitive) || !mapperAllowed) {
        result.status = QStringLiteral("Virtual-output visibility requires enabled HidHide cloaking and a HOTAS BF6 allowlist entry; no runtime change was made.");
        return result;
    }
    const QStringList hiddenDevices = parseHidHideCommands(current.output, QStringLiteral("dev-hide"));
    struct Change { QString instance; bool wasHidden = false; };
    std::vector<Change> changes;
    changes.reserve(static_cast<size_t>(normalized.size()));
    for (const QString &instance : normalized) {
        const bool wasHidden = std::any_of(hiddenDevices.cbegin(), hiddenDevices.cend(), [&instance](const QString &entry) {
            return normalizeDeviceInstanceId(entry) == instance;
        });
        if (wasHidden != hidden) changes.push_back({instance, wasHidden});
    }
    result.available = true;
    if (changes.empty()) {
        result.status = hidden ? QStringLiteral("Selected managed virtual outputs are already hidden from games.")
                               : QStringLiteral("Selected managed virtual outputs are already visible to games.");
        return result;
    }
    std::vector<Change> completed;
    completed.reserve(changes.size());
    for (const Change &change : changes) {
        const SetupProcessResult operation = runHidHide(false, {hidden
            ? QStringLiteral("--dev-hide") : QStringLiteral("--dev-unhide"), change.instance});
        if (operation.succeeded()) {
            completed.push_back(change);
            continue;
        }
        for (auto rollback = completed.crbegin(); rollback != completed.crend(); ++rollback) {
            runHidHide(false, {rollback->wasHidden ? QStringLiteral("--dev-hide")
                                                   : QStringLiteral("--dev-unhide"), rollback->instance});
        }
        result.succeeded = false;
        result.status = QStringLiteral("HidHide could not change the selected virtual-output visibility; completed changes were rolled back.");
        return result;
    }
    result.changed = true;
    result.status = hidden ? QStringLiteral("Selected managed virtual outputs are now hidden from games; rechecking HidHide state.")
                           : QStringLiteral("Selected managed virtual outputs are now visible to games; rechecking HidHide state.");
    return result;
}

const ControllerReadinessPlan &ControllerReadinessService::inspect(const MapperConfiguration &configuration,
                                                                     const PhysicalControllerCapabilities &physical,
                                                                     VerificationMode mode,
                                                                     bool mapperOwnsVjoy,
                                                                     bool outputReportsSucceeding)
{
    if (m_transactionActive) return m_plan;
    m_configuration = configuration;
    m_physical = physical;
    const MapperOutputRequirements requirements = requirementsFor(configuration);
    m_inspectedRequirements = requirements;
    VJoyCapabilities vjoy = inspectVJoy(configuration.vjoyDeviceId);
    vjoy.ownedByHotasBf6 = mapperOwnsVjoy;
    vjoy.outputReportsSucceeding = outputReportsSucceeding;
    const HidHideCapabilities hidhide = inspectHidHide(physical);
    m_plan = planFor(physical, requirements, vjoy, hidhide, mode);
    return m_plan;
}

const ControllerReadinessPlan &ControllerReadinessService::inspectForRequirements(
    const MapperConfiguration &configuration, const PhysicalControllerCapabilities &physical,
    const MapperOutputRequirements &requirements)
{
    if (m_transactionActive) return m_plan;
    m_configuration = configuration;
    m_physical = physical;
    m_inspectedRequirements = requirements;
    const VJoyCapabilities vjoy = inspectVJoy(configuration.vjoyDeviceId);
    const HidHideCapabilities hidhide = inspectHidHide(physical);
    m_plan = planFor(physical, requirements, vjoy, hidhide, VerificationMode::Full);
    return m_plan;
}

bool ControllerReadinessService::applyVJoyConfiguration()
{
    if (m_transactionActive || !m_plan.vjoyNeedsChanges || !m_plan.vjoyCanApply) return false;
    m_transactionActive = true;
    m_plan.state = ControllerReadinessState::Applying;
    m_plan.status = QStringLiteral("CONFIGURING VJOY — Applying the selected controller's output requirements.");
    const SetupProcessResult result = runVJoy(true, vjoyConfigurationArguments(m_plan.vjoy, m_plan.requirements));
    if (!result.succeeded()) {
        m_plan.state = result.cancelled ? ControllerReadinessState::Cancelled : ControllerReadinessState::Failed;
        m_plan.status = result.cancelled
            ? QStringLiteral("vJoy configuration was cancelled.")
            : QStringLiteral("vJoy configuration failed: %1").arg(result.error);
        m_transactionActive = false;
        return false;
    }
    const VJoyCapabilities after = inspectVJoy(m_configuration.vjoyDeviceId);
    m_plan = planFor(m_physical, m_inspectedRequirements, after, m_plan.hidhide, VerificationMode::Full);
    if (m_plan.vjoyNeedsChanges) {
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = QStringLiteral("vJoy did not expose the selected controller's required capabilities after configuration.");
        m_transactionActive = false;
        return false;
    }
    m_transactionActive = false;
    return true;
}

SetupProcessResult ControllerReadinessService::runHidHide(bool elevated, const QStringList &arguments) const
{
    const QString cli = hidhideCliPath();
    return elevated ? m_runner->runElevated(cli, arguments, kApplyTimeoutMs)
                    : m_runner->run(cli, arguments, kInspectionTimeoutMs);
}

SetupProcessResult ControllerReadinessService::runVJoy(bool elevated, const QStringList &arguments) const
{
    const QString utility = vjoyConfigPath();
    return elevated ? m_runner->runElevated(utility, arguments, kApplyTimeoutMs)
                    : m_runner->run(utility, arguments, kInspectionTimeoutMs);
}

QString ControllerReadinessService::mapperExecutablePath() const
{
    return canonicalPath(QCoreApplication::applicationFilePath());
}

QString ControllerReadinessService::vjoyConfigPath() const
{
    return m_utilityPaths.supplied ? m_utilityPaths.vjoyConfig : findVJoyConfig();
}

QString ControllerReadinessService::vjoyConfPath() const
{
    return m_utilityPaths.supplied ? m_utilityPaths.vjoyConf : findVJoyConf();
}

QString ControllerReadinessService::hidhideCliPath() const
{
    return m_utilityPaths.supplied ? m_utilityPaths.hidhideCli : findHidHideCli();
}

QString ControllerReadinessService::hidhideClientPath() const
{
    return m_utilityPaths.supplied ? m_utilityPaths.hidhideClient : findHidHideClient();
}

bool ControllerReadinessService::hidhideServiceReady() const
{
    return m_utilityPaths.supplied ? m_utilityPaths.hidhideServiceReady : hasHidHideService();
}

QStringList ControllerReadinessService::vjoyConfigurationArguments(const VJoyCapabilities &before,
                                                                    const MapperOutputRequirements &requirements)
{
    QStringList arguments{QString::number(before.deviceId), QStringLiteral("-f"), QStringLiteral("-a")};
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (requirements.axes[static_cast<size_t>(index)]) {
            arguments.append(vJoyConfigurationAxisToken(static_cast<VirtualAxis>(index)));
        }
    }
    arguments << QStringLiteral("-b") << QString::number(requirements.buttons);
    if (requirements.continuousPovs > 0) arguments << QStringLiteral("-p") << QString::number(requirements.continuousPovs);
    if (requirements.discretePovs > 0) arguments << QStringLiteral("-s") << QString::number(requirements.discretePovs);
    if (!before.forceFeedbackEffects.isEmpty()) {
        arguments << QStringLiteral("-e");
        arguments.append(before.forceFeedbackEffects);
    }
    return arguments;
}

QList<ControllerReadinessService::RepairOperation> ControllerReadinessService::repairOperationsFor(
    const ControllerReadinessPlan &plan, Journal *journal) const
{
    QList<RepairOperation> operations;
    if (plan.vjoyNeedsChanges) {
        journal->vjoyRestoreCommand = plan.vjoy.restoreCommand;
        journal->vjoyWasAbsent = !plan.vjoy.devicePresent;
        RepairOperation operation;
        operation.name = QStringLiteral("Configure vJoy Device %1").arg(plan.vjoy.deviceId);
        operation.program = vjoyConfigPath();
        operation.arguments = vjoyConfigurationArguments(plan.vjoy, plan.requirements);
        operation.rollbackName = QStringLiteral("Restore vJoy Device %1").arg(plan.vjoy.deviceId);
        operation.rollbackArguments = journal->vjoyWasAbsent
            ? QStringList{QStringLiteral("-d"), QString::number(plan.vjoy.deviceId)}
            : QProcess::splitCommand(journal->vjoyRestoreCommand);
        operation.failureSummary = QStringLiteral("vJoy configuration failed");
        operations.append(std::move(operation));
        journal->vjoyChanged = true;
    }
    if (!plan.hidhideNeedsChanges) return operations;

    journal->mapperExecutable = mapperExecutablePath();
    journal->controllerInstanceIds = plan.hidhide.selectedControllerInstanceIds;
    const QString hidhideCli = hidhideCliPath();
    // Critical ordering: allow the mapper before hiding only the selected
    // physical device, then enable cloaking. Existing lists are never cleared.
    if (!plan.hidhide.mapperAllowlisted) {
        RepairOperation operation;
        operation.name = QStringLiteral("Allow HOTAS BF6 through HidHide");
        operation.program = hidhideCli;
        operation.arguments = {QStringLiteral("--app-reg"), journal->mapperExecutable};
        operation.rollbackName = QStringLiteral("Remove the new HOTAS BF6 HidHide allowlist entry");
        operation.rollbackArguments = {QStringLiteral("--app-unreg"), journal->mapperExecutable};
        operation.failureSummary = QStringLiteral("HidHide allowlist repair failed");
        operations.append(std::move(operation));
        journal->mapperWasAdded = true;
    }
    if (plan.physical.connected && !plan.hidhide.selectedControllerHidden) {
        for (int index = 0; index < journal->controllerInstanceIds.size(); ++index) {
            const QString &instance = journal->controllerInstanceIds.at(index);
            RepairOperation operation;
            operation.name = index == 0 ? QStringLiteral("Hide the selected physical controller")
                                      : QStringLiteral("Hide selected physical controller interface %1").arg(index + 1);
            operation.program = hidhideCli;
            operation.arguments = {QStringLiteral("--dev-hide"), instance};
            operation.rollbackName = index == 0 ? QStringLiteral("Unhide the selected physical controller")
                                              : QStringLiteral("Unhide selected physical controller interface %1").arg(index + 1);
            operation.rollbackArguments = {QStringLiteral("--dev-unhide"), instance};
            operation.failureSummary = QStringLiteral("HidHide device repair failed");
            operations.append(std::move(operation));
        }
        journal->controllerWasHidden = !journal->controllerInstanceIds.isEmpty();
    }
    if (plan.physical.connected && !plan.hidhide.cloaked) {
        RepairOperation operation;
        operation.name = QStringLiteral("Enable HidHide cloaking");
        operation.program = hidhideCli;
        operation.arguments = {QStringLiteral("--cloak-on")};
        operation.rollbackName = QStringLiteral("Disable newly enabled HidHide cloaking");
        operation.rollbackArguments = {QStringLiteral("--cloak-off")};
        operation.failureSummary = QStringLiteral("HidHide cloaking repair failed");
        operations.append(std::move(operation));
        journal->cloakWasEnabled = true;
    }
    return operations;
}

AutomaticRepairResult ControllerReadinessService::runRepairTransaction(
    const QList<RepairOperation> &operations) const
{
    AutomaticRepairResult result;
    if (operations.isEmpty()) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = QStringLiteral("No approved repair operations were available.");
        return result;
    }

    const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString requestPath = QDir::temp().filePath(
        QStringLiteral("hotas-bf6-repair-%1-request.json").arg(nonce));
    const QString resultPath = QDir::temp().filePath(
        QStringLiteral("hotas-bf6-repair-%1-result.json").arg(nonce));
    QJsonArray requestOperations;
    for (const RepairOperation &operation : operations) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), operation.name);
        item.insert(QStringLiteral("program"), operation.program);
        item.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(operation.arguments));
        item.insert(QStringLiteral("rollbackName"), operation.rollbackName);
        item.insert(QStringLiteral("rollbackArguments"), QJsonArray::fromStringList(operation.rollbackArguments));
        requestOperations.append(item);
    }
    QSaveFile requestFile(requestPath);
    if (!requestFile.open(QIODevice::WriteOnly)) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = QStringLiteral("The approved repair request could not be prepared.");
        return result;
    }
    requestFile.write(QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("operations"), requestOperations}}).toJson(QJsonDocument::Compact));
    if (!requestFile.commit()) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = QStringLiteral("The approved repair request could not be saved.");
        return result;
    }

    // One UAC consent belongs to HOTAS BF6.  The elevated app runs no UI and
    // accepts only this short, structured, local operation list.
    const SetupProcessResult helper = m_runner->runElevated(mapperExecutablePath(),
        {QStringLiteral("--hotas-repair-transaction"), QStringLiteral("--request"), requestPath,
         QStringLiteral("--result"), resultPath},
        kApplyTimeoutMs * operations.size() + kInspectionTimeoutMs);

    QFile resultFile(resultPath);
    QJsonParseError parseError;
    QJsonDocument response;
    if (resultFile.open(QIODevice::ReadOnly)) response = QJsonDocument::fromJson(resultFile.readAll(), &parseError);
    QFile::remove(requestPath);
    QFile::remove(resultPath);
    if (helper.cancelled) {
        result.outcome = AutomaticRepairOutcome::Cancelled;
        result.message = QStringLiteral("Automatic repair was cancelled because administrator approval was not granted.");
        return result;
    }
    if (!helper.started || !helper.finished) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = helper.error.isEmpty()
            ? QStringLiteral("Administrator repair process could not be started.")
            : QStringLiteral("Administrator repair process could not be started: %1").arg(helper.error);
        return result;
    }
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = QStringLiteral("Administrator repair process did not return structured repair results.");
        return result;
    }
    for (const QJsonValue &value : response.object().value(QStringLiteral("operations")).toArray()) {
        const QJsonObject item = value.toObject();
        result.operations.append({item.value(QStringLiteral("name")).toString(),
            item.value(QStringLiteral("started")).toBool(), item.value(QStringLiteral("finished")).toBool(),
            item.value(QStringLiteral("succeeded")).toBool(), item.value(QStringLiteral("rollback")).toBool(),
            item.value(QStringLiteral("exitCode")).toInt(-1), item.value(QStringLiteral("windowsErrorCode")).toInt(),
            item.value(QStringLiteral("message")).toString(), item.value(QStringLiteral("output")).toString(),
            item.value(QStringLiteral("errorOutput")).toString()});
    }
    if (response.object().value(QStringLiteral("success")).toBool()) {
        result.outcome = AutomaticRepairOutcome::Ready;
        result.message = QStringLiteral("Approved repair operations completed.");
        return result;
    }
    result.outcome = AutomaticRepairOutcome::Failed;
    const auto failed = std::find_if(result.operations.cbegin(), result.operations.cend(),
        [](const AutomaticRepairOperationResult &operation) { return !operation.rollback && !operation.succeeded; });
    const int failedIndex = failed == result.operations.cend() ? -1
        : static_cast<int>(std::distance(result.operations.cbegin(), failed));
    const RepairOperation *planned = failedIndex >= 0 && failedIndex < operations.size()
        ? &operations.at(failedIndex) : nullptr;
    result.message = planned ? planned->failureSummary : QStringLiteral("An approved repair operation failed.");
    if (failed != result.operations.cend()) {
        if (failed->exitCode >= 0) result.message += QStringLiteral(" Command exited with code %1.").arg(failed->exitCode);
        const QString diagnostic = !failed->errorOutput.trimmed().isEmpty() ? failed->errorOutput.trimmed()
            : !failed->message.trimmed().isEmpty() ? failed->message.trimmed() : failed->output.trimmed();
        if (!diagnostic.isEmpty()) result.message += QStringLiteral(" %1").arg(diagnostic.left(240));
    }
    return result;
}

bool ControllerReadinessService::verifyAfterRepair()
{
    // vJoyConfig can return before existing consumers observe its new
    // descriptor. Reinspect the actual device at a bounded control-plane
    // cadence; never make process exit alone the proof of repair.
    constexpr int kRepairReinspectionAttempts = 8;
    constexpr unsigned long kRepairReinspectionIntervalMs = 150;
    for (int attempt = 0; attempt != kRepairReinspectionAttempts; ++attempt) {
        const VJoyCapabilities vjoy = inspectVJoy(m_configuration.vjoyDeviceId);
        const HidHideCapabilities hidhide = inspectHidHide(m_physical);
        m_plan = planFor(m_physical, requirementsFor(m_configuration), vjoy, hidhide, VerificationMode::Full);
        if (m_plan.state == ControllerReadinessState::Ready) return true;
        if (attempt + 1 != kRepairReinspectionAttempts) QThread::msleep(kRepairReinspectionIntervalMs);
    }
    if (m_plan.vjoyNeedsChanges) {
        m_plan.vjoySummary = QStringLiteral("vJoy Device %1 still reports %2 buttons; %3 are required.")
            .arg(m_plan.vjoy.deviceId).arg(m_plan.vjoy.buttons).arg(m_plan.requirements.buttons);
        m_plan.status = QStringLiteral("VJOY CONVERGENCE TIMEOUT — Required %1 buttons; observed %2.")
            .arg(m_plan.requirements.buttons).arg(m_plan.vjoy.buttons);
    }
    return false;
}

bool ControllerReadinessService::rollback(Journal *journal, QString *failure)
{
    bool success = true;
    QStringList problems;
    const auto record = [this, &success, &problems](const QString &name, const SetupProcessResult &operation,
                                                      const QString &problem) {
        m_lastRepairResult.operations.append({name, operation.started, operation.finished,
            operation.succeeded(), true, operation.exitCode, operation.windowsErrorCode,
            operation.error, operation.output, operation.errorOutput});
        if (!operation.succeeded()) {
            success = false;
            problems.append(problem);
        }
    };
    if (journal->cloakWasEnabled) {
        record(QStringLiteral("Restore prior HidHide cloaking"),
               runHidHide(true, {QStringLiteral("--cloak-off")}),
               QStringLiteral("could not disable newly enabled cloaking"));
    }
    if (journal->controllerWasHidden) {
        for (int index = journal->controllerInstanceIds.size() - 1; index >= 0; --index) {
            record(index == 0 ? QStringLiteral("Restore physical controller visibility")
                              : QStringLiteral("Restore physical controller interface %1 visibility").arg(index + 1),
                   runHidHide(true, {QStringLiteral("--dev-unhide"), journal->controllerInstanceIds.at(index)}),
                   QStringLiteral("could not unhide the selected controller"));
        }
    }
    if (journal->mapperWasAdded) {
        record(QStringLiteral("Restore prior HidHide allowlist"), runHidHide(true,
               {QStringLiteral("--app-unreg"), journal->mapperExecutable}),
               QStringLiteral("could not remove the newly added mapper allowlist entry"));
    }
    if (journal->vjoyChanged) {
        const QStringList restore = journal->vjoyWasAbsent
            ? QStringList{QStringLiteral("-d"), QString::number(m_configuration.vjoyDeviceId)}
            : QProcess::splitCommand(journal->vjoyRestoreCommand);
        const SetupProcessResult operation = restore.isEmpty()
            ? SetupProcessResult{false, false, -1, {}, QStringLiteral("No vJoy restore command was available")}
            : runVJoy(true, restore);
        record(QStringLiteral("Restore prior vJoy Device %1").arg(m_configuration.vjoyDeviceId), operation,
               QStringLiteral("could not restore vJoy Device 1"));
    }
    if (failure && !success) *failure = problems.join(QStringLiteral("; "));
    return success;
}

bool ControllerReadinessService::recoverFromPhysicalAccessFailure()
{
    if (m_transactionActive || !m_journal.available) return false;
    m_transactionActive = true;
    m_lastRepairResult.rollbackAttempted = true;
    m_plan.state = ControllerReadinessState::RollingBack;
    m_plan.isChecking = true;
    m_plan.status = QStringLiteral("RESTORING PHYSICAL CONTROLLER VISIBILITY — HOTAS BF6 could not safely reopen the controller.");
    QString failure;
    const bool restored = rollback(&m_journal, &failure);
    m_lastRepairResult.rollbackSucceeded = restored;
    if (restored) {
        m_journal = {};
        clearRecoveryJournal();
    } else {
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = QStringLiteral("ROLLBACK FAILURE — %1").arg(failure);
    }
    m_transactionActive = false;
    return restored;
}

bool ControllerReadinessService::allowlistMapperOnly()
{
    if (m_transactionActive || !m_plan.hidhide.installed || m_plan.hidhide.mapperAllowlisted) return false;
    const SetupProcessResult operation = runHidHide(true,
        {QStringLiteral("--app-reg"), mapperExecutablePath()});
    if (!operation.succeeded()) return false;
    const HidHideCapabilities observed = inspectHidHide(m_plan.physical);
    m_plan = planFor(m_plan.physical, m_plan.requirements, m_plan.vjoy, observed,
                     m_plan.verificationMode == VerificationMode::None
                         ? VerificationMode::Quick : m_plan.verificationMode);
    if (!m_plan.hidhide.mapperAllowlisted) {
        m_plan.hidhideStatus = VerificationSubsystemState::Error;
        m_plan.hidhideSummary = QStringLiteral("HidHide accepted the allowlist command, but this running executable was not present on read-back: %1")
            .arg(mapperExecutablePath());
        m_plan.status = QStringLiteral("HIDHIDE ACCESS REPAIR FAILED — Read-back did not allowlist this running executable.");
        return false;
    }
    m_plan.hidhideStatus = VerificationSubsystemState::Attention;
    m_plan.hidhideSummary = QStringLiteral("This running HOTAS BF6 executable is allowlisted; re-enumerating physical controllers.");
    m_plan.status = QStringLiteral("HIDHIDE ACCESS REPAIRED — Connect or replug a physical controller to continue setup.");
    return true;
}

void ControllerReadinessService::completePhysicalAccessVerification(bool reacquired, bool reportsReceived,
                                                                     bool rollbackAttempted,
                                                                     bool rollbackSucceeded,
                                                                     bool reportsReceivedAfterRollback)
{
    m_reconnectVerificationPending = false;
    m_reconnectDisconnectObserved = false;
    m_reconnectReconciliationPending = false;
    m_lastRepairResult.physicalReacquisitionAttempted = true;
    m_lastRepairResult.physicalReacquisitionSucceeded = reacquired;
    m_lastRepairResult.physicalReportsReceivedAfterRepair = reportsReceived;
    m_lastRepairResult.rollbackAttempted = rollbackAttempted;
    m_lastRepairResult.rollbackSucceeded = rollbackSucceeded;
    m_lastRepairResult.physicalReportsReceivedAfterRollback = reportsReceivedAfterRollback;
    m_plan.isChecking = false;
    m_plan.lastChecked = QDateTime::currentDateTime();
    if (reacquired && reportsReceived) {
        m_plan.physicalStatus = VerificationSubsystemState::Ready;
        m_plan.physicalSummary = QStringLiteral("%1 — reacquired after HidHide changes; live reports confirmed.")
            .arg(m_plan.physical.name);
        m_plan.state = ControllerReadinessState::Ready;
        m_plan.status = QStringLiteral("READY — Controller setup repaired successfully and physical input was reacquired.");
        m_lastRepairResult.outcome = AutomaticRepairOutcome::Ready;
        m_lastRepairResult.message = QStringLiteral("Controller setup repaired successfully after physical-controller verification.");
        m_journal = {};
        clearRecoveryJournal();
        return;
    }

    m_lastRepairResult.outcome = AutomaticRepairOutcome::Failed;
    m_plan.physicalStatus = VerificationSubsystemState::Error;
    m_plan.hidhideStatus = VerificationSubsystemState::Error;
    if (rollbackSucceeded && reportsReceivedAfterRollback) {
        m_plan.physicalSummary = QStringLiteral("Physical controller visibility was restored and live reports resumed after automatic setup was reverted.");
        m_plan.hidhideSummary = QStringLiteral("Automatic HidHide configuration was reverted because HOTAS BF6 could not safely retain controller access.");
        m_plan.state = ControllerReadinessState::Attention;
        m_plan.status = QStringLiteral("HIDHIDE SELF-ACCESS FAILURE — Automatic setup was reverted; physical input is available again.");
        m_lastRepairResult.message = QStringLiteral("Automatic HidHide setup was reverted after HOTAS BF6 could not reacquire the physical controller.");
    } else {
        m_plan.physicalSummary = QStringLiteral("HOTAS BF6 could not reacquire the selected physical controller after HidHide changes.");
        m_plan.hidhideSummary = rollbackSucceeded
            ? QStringLiteral("Visibility was restored, but reconnect the controller and use Copy Diagnostics if reports do not return.")
            : QStringLiteral("Automatic rollback could not fully restore controller visibility; use Copy Diagnostics before manual recovery.");
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = rollbackSucceeded
            ? QStringLiteral("PHYSICAL CONTROLLER LOST — Visibility was restored; reconnect your controller to complete recovery.")
            : QStringLiteral("ROLLBACK FAILURE — HOTAS BF6 could not safely restore physical controller access.");
        m_lastRepairResult.message = m_plan.status;
    }
}

void ControllerReadinessService::beginPhysicalReconnectVerification()
{
    m_reconnectVerificationPending = true;
    m_reconnectDisconnectObserved = false;
    m_reconnectReconciliationPending = false;
    m_plan.state = ControllerReadinessState::Verifying;
    m_plan.isChecking = false;
    m_plan.physicalStatus = VerificationSubsystemState::Attention;
    m_plan.physicalSummary = QStringLiteral("HidHide is configured. Unplug %1 to verify the required device re-enumeration.")
        .arg(m_plan.physical.name);
    m_plan.status = QStringLiteral("RECONNECT CONTROLLER — HidHide is configured. Unplug %1, then reconnect it when prompted.")
        .arg(m_plan.physical.name);
    m_plan.lastChecked = QDateTime::currentDateTime();
    m_lastRepairResult.message = QStringLiteral("HidHide configuration is verified; waiting for an observed controller reconnect.");
}

bool ControllerReadinessService::observePhysicalReconnect(bool connected,
                                                           bool selectedControllerDetected,
                                                           bool reportsReceived)
{
    if (!m_reconnectVerificationPending) return false;
    if (!m_reconnectDisconnectObserved) {
        if (connected) return false;
        m_reconnectDisconnectObserved = true;
        m_plan.physicalStatus = VerificationSubsystemState::Checking;
        m_plan.physicalSummary = QStringLiteral("Controller disconnected ✓  Reconnect %1 now.").arg(m_plan.physical.name);
        m_plan.status = QStringLiteral("RECONNECT CONTROLLER — Controller disconnected ✓  Reconnect %1 and move a control.")
            .arg(m_plan.physical.name);
        m_plan.lastChecked = QDateTime::currentDateTime();
        return true;
    }
    if (!connected) return false;
    if (!selectedControllerDetected) {
        const QString summary = QStringLiteral("A controller was detected, but it is not the selected %1.")
            .arg(m_plan.physical.name);
        const QString status = QStringLiteral("RECONNECT CONTROLLER — Waiting for the selected controller to return.");
        if (m_plan.physicalSummary == summary && m_plan.status == status) return false;
        m_plan.physicalStatus = VerificationSubsystemState::Attention;
        m_plan.physicalSummary = summary;
        m_plan.status = status;
        m_plan.lastChecked = QDateTime::currentDateTime();
        return true;
    }
    if (!reportsReceived) {
        const QString summary = QStringLiteral("%1 reconnected ✓  Waiting for a live input report.")
            .arg(m_plan.physical.name);
        const QString status = QStringLiteral("RECONNECT CONTROLLER — Move a control to confirm live input.");
        if (m_plan.physicalSummary == summary && m_plan.status == status) return false;
        m_plan.physicalStatus = VerificationSubsystemState::Checking;
        m_plan.physicalSummary = summary;
        m_plan.status = status;
        m_plan.lastChecked = QDateTime::currentDateTime();
        return true;
    }

    m_reconnectVerificationPending = false;
    m_reconnectReconciliationPending = true;
    m_plan.physicalStatus = VerificationSubsystemState::Ready;
    m_plan.physicalSummary = QStringLiteral("%1 reconnected ✓  Live input reports received ✓.")
        .arg(m_plan.physical.name);
    m_plan.hidhideStatus = VerificationSubsystemState::Checking;
    m_plan.hidhideSummary = QStringLiteral("Reconciling current HID interfaces for the reconnected controller.");
    m_plan.state = ControllerReadinessState::Verifying;
    m_plan.status = QStringLiteral("RECONCILING HIDHIDE — Re-reading current controller interfaces after reconnect.");
    m_plan.lastChecked = QDateTime::currentDateTime();
    m_lastRepairResult.physicalReacquisitionAttempted = true;
    m_lastRepairResult.physicalReacquisitionSucceeded = true;
    m_lastRepairResult.physicalReportsReceivedAfterRepair = true;
    m_lastRepairResult.message = QStringLiteral("Controller reconnected with live input; reconciling HidHide against the current interfaces.");
    return true;
}

bool ControllerReadinessService::verifyReady()
{
    if (m_reconnectVerificationPending || m_reconnectReconciliationPending) return false;
    const VJoyCapabilities vjoy = inspectVJoy(m_configuration.vjoyDeviceId);
    const HidHideCapabilities hidhide = inspectHidHide(m_physical);
    m_plan = planFor(m_physical, requirementsFor(m_configuration), vjoy, hidhide);
    return m_plan.state == ControllerReadinessState::Ready;
}

bool ControllerReadinessService::applyAutomatically()
{
    if (m_transactionActive || !m_plan.canApplyAutomatically) return false;
    m_reconnectVerificationPending = false;
    m_reconnectDisconnectObserved = false;
    m_reconnectReconciliationPending = false;
    m_transactionActive = true;
    Journal journal;
    m_plan.state = ControllerReadinessState::AwaitingPermission;
    m_plan.status = QStringLiteral("WAITING FOR ADMINISTRATOR APPROVAL — HOTAS BF6 will run only the approved vJoy and HidHide repair utilities.");
    const QList<RepairOperation> operations = repairOperationsFor(m_plan, &journal);
    m_lastRepairResult = runRepairTransaction(operations);
    if (m_lastRepairResult.outcome == AutomaticRepairOutcome::Cancelled) {
        m_plan.state = ControllerReadinessState::Cancelled;
        m_plan.status = m_lastRepairResult.message;
        m_transactionActive = false;
        return false;
    }
    if (m_lastRepairResult.outcome != AutomaticRepairOutcome::Ready) {
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = QStringLiteral("REPAIR FAILED — %1").arg(m_lastRepairResult.message);
        m_transactionActive = false;
        return false;
    }

    // Persist the exact narrow transaction immediately after privileged work
    // completes. A crash during subsequent re-enumeration can therefore be
    // surfaced as a recoverable Undo action on the next launch.
    journal.available = journal.vjoyChanged || journal.mapperWasAdded || journal.controllerWasHidden || journal.cloakWasEnabled;
    m_journal = journal;
    persistRecoveryJournal();

    m_plan.state = ControllerReadinessState::Verifying;
    m_plan.status = QStringLiteral("VERIFYING REPAIR — HOTAS BF6 is checking the complete controller chain.");
    if (!verifyAfterRepair()) {
        m_lastRepairResult.outcome = AutomaticRepairOutcome::Attention;
        const QString unresolved = !m_plan.hidhideSummary.isEmpty() ? m_plan.hidhideSummary
            : !m_plan.vjoySummary.isEmpty() ? m_plan.vjoySummary : m_plan.physicalSummary;
        m_lastRepairResult.message = QStringLiteral("Repair completed, but verification still needs attention: %1").arg(unresolved);
        m_plan.state = ControllerReadinessState::Attention;
        m_plan.status = QStringLiteral("REPAIR COMPLETED — VERIFICATION INCOMPLETE — %1").arg(unresolved);
    } else {
        // Do not publish READY from configuration read-back. AppBackend now
        // forces a brand-new DirectInput open and reports proof before this
        // transition may become user-visible.
        m_lastRepairResult.outcome = AutomaticRepairOutcome::Ready;
        m_lastRepairResult.message = QStringLiteral("Configuration applied; waiting for physical-controller verification.");
        m_plan.state = ControllerReadinessState::Verifying;
        m_plan.status = QStringLiteral("VERIFYING PHYSICAL CONTROLLER — Reacquiring the controller after HidHide changes.");
    }
    m_transactionActive = false;
    return true;
}

bool ControllerReadinessService::undoLastAutomaticSetup()
{
    if (m_transactionActive || !m_journal.available) return false;
    m_transactionActive = true;
    QString failure;
    const bool restored = rollback(&m_journal, &failure);
    if (restored) {
        m_reconnectVerificationPending = false;
        m_reconnectDisconnectObserved = false;
        m_reconnectReconciliationPending = false;
        m_journal = {};
        clearRecoveryJournal();
        inspect(m_configuration, m_physical);
        m_plan.status = QStringLiteral("Automatic controller repair changes were undone.");
    } else {
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = QStringLiteral("Undo needs manual review: %1").arg(failure);
    }
    m_transactionActive = false;
    return restored;
}

bool ControllerReadinessService::canUndo() const
{
    return m_journal.available && !m_transactionActive;
}

} // namespace hotas
