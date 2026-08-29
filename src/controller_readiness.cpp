#include "controller_readiness.h"

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

bool capabilityAxesSatisfy(const std::array<bool, kVirtualAxisSlotCount> &have,
                           const std::array<bool, kVirtualAxisSlotCount> &need)
{
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
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
        {QStringLiteral("controllerInstanceId"), m_journal.controllerInstanceId},
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
    recovered.controllerInstanceId = record.value(QStringLiteral("controllerInstanceId")).toString();
    recovered.available = !recovered.controllerInstanceId.isEmpty()
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
    for (const ControllerProfile &profile : configuration.profiles) {
        for (const AxisMapping &axis : profile.axes) {
            const int index = static_cast<int>(axis.target);
            if (index > 0 && index < kVirtualAxisSlotCount) requirements.axes[static_cast<size_t>(index)] = true;
        }
        requirements.buttons = std::max(requirements.buttons, highestButton(profile.buttons));
        requirements.buttons = std::max(requirements.buttons, highestPovButton(profile.povs));
    }
    for (const NativePovBinding &binding : configuration.nativePovBindings) {
        if (!binding.enabled) continue;
        if (binding.targetType == NativePovTargetType::Continuous) {
            requirements.continuousPovs = std::max(requirements.continuousPovs, binding.targetIndex);
        } else if (binding.targetType == NativePovTargetType::Discrete) {
            requirements.discretePovs = std::max(requirements.discretePovs, binding.targetIndex);
        }
    }
    for (const AutomationDefinition &automation : configuration.automations) {
        for (const AutomationActionDefinition &action : automation.actions) {
            if (action.type == AutomationActionType::VJoyButtonHold
                || action.type == AutomationActionType::VJoyButtonToggle
                || action.type == AutomationActionType::VJoyButtonTap) {
                requirements.buttons = std::max(requirements.buttons, action.virtualButton);
            }
        }
    }
    // A verified controller's stored requirement is a floor, not a competing
    // definition. Keep it when temporary profile edits happen to require less
    // than the output configuration that was successfully verified for this
    // selected device.
    const auto activeRecord = std::find_if(configuration.savedControllers.cbegin(),
        configuration.savedControllers.cend(), [&configuration](const SavedControllerRecord &record) {
            return record.id == configuration.activeControllerRecordId;
        });
    if (activeRecord != configuration.savedControllers.cend()) {
        requirements.buttons = std::max(requirements.buttons, activeRecord->vjoyRequirements.buttons);
        requirements.continuousPovs = std::max(requirements.continuousPovs,
                                                activeRecord->vjoyRequirements.continuousPovs);
        requirements.discretePovs = std::max(requirements.discretePovs,
                                              activeRecord->vjoyRequirements.discretePovs);
        for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
            requirements.axes[static_cast<size_t>(index)] =
                requirements.axes[static_cast<size_t>(index)]
                || activeRecord->vjoyRequirements.axes[static_cast<size_t>(index)];
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
    // An active mapper can safely own a BUSY device, but ownership and
    // successful reports never prove the device has enough axes, buttons, or
    // POV capacity for the selected controller.
    return vjoy.installed && vjoy.configurationUtilityAvailable && vjoy.driverReady
        && vjoy.devicePresent && (!vjoy.busy || vjoy.ownedByHotasBf6)
        && capabilityAxesSatisfy(vjoy.axes, requirements.axes)
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
        plan.vjoyStatus = VerificationSubsystemState::Ready;
        plan.vjoySummary = vjoy.ownedByHotasBf6
            ? QStringLiteral("vJoy Device %1 — Ready · HOTAS BF6 currently owns this device.").arg(vjoy.deviceId)
            : QStringLiteral("vJoy Device %1 — Ready · configured correctly.").arg(vjoy.deviceId);
        plan.findings.append(plan.vjoySummary);
    } else if (!vjoy.installed || !vjoy.configurationUtilityAvailable) {
        plan.vjoyStatus = VerificationSubsystemState::Error;
        plan.vjoySummary = QStringLiteral("vJoy is unavailable — install or repair the vJoy driver.");
        plan.findings.append(QStringLiteral("VJOY NOT DETECTED — Install vJoy before configuring virtual output."));
    } else if (vjoy.busy && !vjoy.ownedByHotasBf6) {
        plan.vjoyStatus = VerificationSubsystemState::Error;
        plan.vjoySummary = QStringLiteral("vJoy Device %1 — In use by another application.").arg(vjoy.deviceId);
        plan.findings.append(plan.vjoySummary);
    } else {
        plan.vjoyStatus = VerificationSubsystemState::Error;
        plan.vjoySummary = QStringLiteral("vJoy Device %1 needs the required output capabilities.").arg(vjoy.deviceId);
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
        plan.hidhideSummary = QStringLiteral("HidHide is blocking HOTAS BF6 from the selected physical device.");
        plan.findings.append(plan.hidhideSummary);
    } else {
        plan.hidhideStatus = VerificationSubsystemState::Attention;
        plan.hidhideSummary = QStringLiteral("HidHide needs configuration for the selected physical controller.");
        plan.hidhideCanApply = true;
        plan.findings.append(QStringLiteral("PHYSICAL CONTROLLER STILL VISIBLE TO GAMES — HOTAS BF6 can allow itself, hide only this controller, then enable cloaking."));
        if (!hidhide.mapperAllowlisted) plan.proposedChanges.append(QStringLiteral("Allow HOTAS BF6 through HidHide before changing device visibility."));
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
    result.busy = lower.contains(QStringLiteral("state:")) && lower.contains(QStringLiteral("busy"));
    result.devicePresent = lower.contains(QStringLiteral("device:"))
        && !lower.contains(QStringLiteral("does not exist")) && !lower.contains(QStringLiteral("not configured"));
    result.driverReady = result.devicePresent && !lower.contains(QStringLiteral("driver is disabled"))
        && !lower.contains(QStringLiteral("not enabled"));
    const QString buttons = firstRegexCapture(report, QRegularExpression(QStringLiteral("(?im)buttons\\s*:\\s*(\\d+)")));
    result.buttons = buttons.toInt();
    const QString continuous = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)contin(?:u|o)ous\\s+POVs?\\s*:\\s*(\\d+)")));
    result.continuousPovs = continuous.toInt();
    const QString discrete = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)desc(?:r|re)ete\\s+POVs?\\s*:\\s*(\\d+)")));
    result.discretePovs = discrete.toInt();
    const QString axes = firstRegexCapture(report, QRegularExpression(QStringLiteral("(?im)axes\\s*:\\s*([^\\r\\n]+)")));
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const QString name = virtualAxisLabel(static_cast<VirtualAxis>(index));
        result.axes[static_cast<size_t>(index)] = axes.contains(QRegularExpression(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(name)),
            QRegularExpression::CaseInsensitiveOption));
    }
    const QString ffb = firstRegexCapture(report, QRegularExpression(
        QStringLiteral("(?im)FFB\\s+Effects?\\s*:\\s*([^\\r\\n]+)")));
    if (!ffb.isEmpty()) {
        result.forceFeedbackKnown = true;
        if (ffb.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
            result.forceFeedbackEffects = ffb.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
        }
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
    result.installed = true;
    result.configurationUtilityAvailable = true;
    if (!report.succeeded() && result.diagnostic.isEmpty()) result.diagnostic = report.error;

    // -t -c emits an executable vJoyConfig command that can be used as a
    // narrow device-1 snapshot for rollback. Never use vJoyConfig -r.
    const SetupProcessResult command = runVJoy(false,
        {QStringLiteral("-t"), QStringLiteral("-c"), QString::number(deviceId)});
    if (command.succeeded()) {
        const QRegularExpression configurationLine(QStringLiteral("(?im)^\\s*vJoyConfig\\s+(.+)$"));
        const QRegularExpressionMatch match = configurationLine.match(command.output);
        if (match.hasMatch()) result.restoreCommand = match.captured(1).trimmed();
    }
    const SetupProcessResult devices = runVJoy(false, {QStringLiteral("-t")});
    if (devices.succeeded()) {
        const QRegularExpression deviceLine(QStringLiteral("(?im)^\\s*Device\\s*:\\s*(\\d+)"));
        QRegularExpressionMatchIterator iterator = deviceLine.globalMatch(devices.output);
        while (iterator.hasNext()) result.availableDeviceIds.append(iterator.next().captured(1).toInt());
        std::sort(result.availableDeviceIds.begin(), result.availableDeviceIds.end());
        result.availableDeviceIds.erase(
            std::unique(result.availableDeviceIds.begin(), result.availableDeviceIds.end()),
            result.availableDeviceIds.end());
    }
    if (!result.devicePresent) {
        // There is no descriptor to preserve. A deletion of this newly created
        // device is the exact rollback snapshot for a previously absent target.
        result.forceFeedbackKnown = true;
        return result;
    }
    if (result.forceFeedbackKnown) return result;
    // An absent FFB line is only safe when the report explicitly says no FFB.
    result.forceFeedbackKnown = report.output.contains(QStringLiteral("FFB Effects"), Qt::CaseInsensitive)
        && report.output.contains(QStringLiteral("None"), Qt::CaseInsensitive);
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
    if (cloak.succeeded()) {
        result.cloakKnown = true;
        result.cloaked = cloak.output.contains(QStringLiteral("--cloak-on"), Qt::CaseInsensitive);
    } else {
        result.diagnostic = cloak.error.isEmpty() ? cloak.output.trimmed() : cloak.error;
    }
    const SetupProcessResult apps = runHidHide(false, {QStringLiteral("--app-list")});
    if (apps.succeeded()) {
        result.allowlistedApplications = parseHidHideCommands(apps.output, QStringLiteral("app-reg"));
        const QString expected = QDir::toNativeSeparators(mapperExecutablePath());
        result.mapperAllowlisted = std::any_of(result.allowlistedApplications.cbegin(),
            result.allowlistedApplications.cend(), [&expected](const QString &entry) {
                return QDir::toNativeSeparators(entry).compare(expected, Qt::CaseInsensitive) == 0;
            });
    }
    const SetupProcessResult hidden = runHidHide(false, {QStringLiteral("--dev-list")});
    if (hidden.succeeded()) {
        result.hiddenDeviceInstanceIds = parseHidHideCommands(hidden.output, QStringLiteral("dev-hide"));
        const QString selected = normalizeDeviceInstanceId(physical.hidInstanceId);
        result.selectedControllerHidden = std::any_of(result.hiddenDeviceInstanceIds.cbegin(),
            result.hiddenDeviceInstanceIds.cend(), [&selected](const QString &entry) {
                return normalizeDeviceInstanceId(entry) == selected;
            });
    }
    // DIPROP_GUIDANDPATH is the exact PnP/HID path for the DirectInput device
    // the mapper already has open. That is a stable, per-instance identity;
    // do not downgrade it to an ambiguous warning merely because HidHide's
    // optional gaming-device listing is unavailable or formats it differently.
    if (!physical.hidInstanceId.isEmpty()) {
        result.selectedControllerResolved = true;
        const SetupProcessResult devices = runHidHide(false, {QStringLiteral("--dev-gaming")});
        if (devices.succeeded() && outputContainsDevice(devices.output, physical.hidInstanceId)) {
            result.selectedControllerResolved = true;
        }
    }
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
    return QCoreApplication::applicationFilePath();
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
            arguments.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
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
    journal->controllerInstanceId = normalizeDeviceInstanceId(plan.physical.hidInstanceId);
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
        RepairOperation operation;
        operation.name = QStringLiteral("Hide the selected physical controller");
        operation.program = hidhideCli;
        operation.arguments = {QStringLiteral("--dev-hide"), journal->controllerInstanceId};
        operation.rollbackName = QStringLiteral("Unhide the selected physical controller");
        operation.rollbackArguments = {QStringLiteral("--dev-unhide"), journal->controllerInstanceId};
        operation.failureSummary = QStringLiteral("HidHide device repair failed");
        operations.append(std::move(operation));
        journal->controllerWasHidden = true;
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
    const QString requestPath = QDir::temp().filePath(QStringLiteral("hotas-bf6-repair-%1-request.json").arg(nonce));
    const QString resultPath = QDir::temp().filePath(QStringLiteral("hotas-bf6-repair-%1-result.json").arg(nonce));
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
    const QJsonObject request{{QStringLiteral("schemaVersion"), 1},
                              {QStringLiteral("operations"), requestOperations}};
    requestFile.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    if (!requestFile.commit()) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = QStringLiteral("The approved repair request could not be saved.");
        return result;
    }

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
        result.operations.append({QStringLiteral("Administrator repair process"), false, false, false, false,
                                  helper.exitCode, helper.windowsErrorCode, result.message, helper.output, helper.errorOutput});
        return result;
    }
    if (!helper.started || !helper.finished) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = helper.error.isEmpty()
            ? QStringLiteral("Administrator repair process could not be started.")
            : QStringLiteral("Administrator repair process could not be started: %1").arg(helper.error);
        result.operations.append({QStringLiteral("Administrator repair process"), helper.started, helper.finished,
                                  false, false, helper.exitCode, helper.windowsErrorCode, result.message,
                                  helper.output, helper.errorOutput});
        return result;
    }
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        result.outcome = AutomaticRepairOutcome::Failed;
        result.message = helper.error.isEmpty()
            ? QStringLiteral("Administrator repair process did not return structured repair results.")
            : QStringLiteral("Administrator repair process failed: %1").arg(helper.error);
        return result;
    }

    const QJsonArray operationResults = response.object().value(QStringLiteral("operations")).toArray();
    for (const QJsonValue &value : operationResults) {
        const QJsonObject item = value.toObject();
        AutomaticRepairOperationResult operation;
        operation.operationName = item.value(QStringLiteral("name")).toString();
        operation.started = item.value(QStringLiteral("started")).toBool();
        operation.finished = item.value(QStringLiteral("finished")).toBool();
        operation.succeeded = item.value(QStringLiteral("succeeded")).toBool();
        operation.rollback = item.value(QStringLiteral("rollback")).toBool();
        operation.exitCode = item.value(QStringLiteral("exitCode")).toInt(-1);
        operation.windowsErrorCode = item.value(QStringLiteral("windowsErrorCode")).toInt();
        operation.message = item.value(QStringLiteral("message")).toString();
        operation.output = item.value(QStringLiteral("output")).toString();
        operation.errorOutput = item.value(QStringLiteral("errorOutput")).toString();
        result.operations.append(std::move(operation));
    }
    if (response.object().value(QStringLiteral("success")).toBool()) {
        result.outcome = AutomaticRepairOutcome::Ready;
        result.message = QStringLiteral("Approved repair operations completed.");
        return result;
    }

    const auto failed = std::find_if(result.operations.cbegin(), result.operations.cend(),
        [](const AutomaticRepairOperationResult &operation) { return !operation.rollback && !operation.succeeded; });
    const int operationIndex = failed == result.operations.cend() ? -1
        : static_cast<int>(std::distance(result.operations.cbegin(), failed));
    const RepairOperation *planned = operationIndex >= 0 && operationIndex < operations.size()
        ? &operations.at(operationIndex) : nullptr;
    result.outcome = AutomaticRepairOutcome::Failed;
    result.message = planned ? planned->failureSummary : QStringLiteral("The approved repair operation failed.");
    if (failed != result.operations.cend()) {
        const QString exitMessage = failed->exitCode >= 0
            ? QStringLiteral("Command exited with code %1").arg(failed->exitCode) : QString{};
        if (!exitMessage.isEmpty()) result.message += QStringLiteral(" %1.").arg(exitMessage);
        QString diagnostic = !failed->errorOutput.trimmed().isEmpty() ? failed->errorOutput.trimmed()
            : !failed->message.trimmed().isEmpty() ? failed->message.trimmed() : failed->output.trimmed();
        const int newline = diagnostic.indexOf(QRegularExpression(QStringLiteral("[\\r\\n]")));
        if (newline >= 0) diagnostic.truncate(newline);
        if (!diagnostic.isEmpty() && diagnostic.compare(exitMessage, Qt::CaseInsensitive) != 0) {
            result.message += QStringLiteral(" %1").arg(diagnostic.left(240));
        }
    }
    return result;
}

bool ControllerReadinessService::verifyAfterRepair()
{
    // HidHide service state can take a short moment to reflect a successful
    // transaction. This bounded control-plane retry is never run by the mapper.
    for (int attempt = 0; attempt != 3; ++attempt) {
        const VJoyCapabilities vjoy = inspectVJoy(m_configuration.vjoyDeviceId);
        const HidHideCapabilities hidhide = inspectHidHide(m_physical);
        m_plan = planFor(m_physical, requirementsFor(m_configuration), vjoy, hidhide, VerificationMode::Full);
        if (m_plan.state == ControllerReadinessState::Ready) return true;
        if (attempt != 2) QThread::msleep(static_cast<unsigned long>(150 * (attempt + 1)));
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
        record(QStringLiteral("Restore physical controller visibility"), runHidHide(true,
               {QStringLiteral("--dev-unhide"), journal->controllerInstanceId}),
               QStringLiteral("could not unhide the selected controller"));
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
    m_plan.hidhide.mapperAllowlisted = true;
    m_plan.hidhideStatus = VerificationSubsystemState::Attention;
    m_plan.hidhideSummary = QStringLiteral("HOTAS BF6 is now allowlisted. Re-enumerating physical controllers.");
    m_plan.status = QStringLiteral("HIDHIDE ACCESS REPAIRED — Connect or replug a physical controller to continue setup.");
    return true;
}

void ControllerReadinessService::completePhysicalAccessVerification(bool reacquired, bool reportsReceived,
                                                                    bool rollbackAttempted,
                                                                    bool rollbackSucceeded,
                                                                    bool reportsReceivedAfterRollback)
{
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

bool ControllerReadinessService::verifyReady()
{
    const VJoyCapabilities vjoy = inspectVJoy(m_configuration.vjoyDeviceId);
    const HidHideCapabilities hidhide = inspectHidHide(m_physical);
    m_plan = planFor(m_physical, requirementsFor(m_configuration), vjoy, hidhide);
    return m_plan.state == ControllerReadinessState::Ready;
}

bool ControllerReadinessService::applyAutomatically()
{
    if (m_transactionActive || !m_plan.canApplyAutomatically) return false;
    m_transactionActive = true;
    Journal journal;
    m_plan.state = ControllerReadinessState::AwaitingPermission;
    m_plan.status = QStringLiteral("WAITING FOR ADMINISTRATOR APPROVAL — HOTAS BF6 will run one approved repair transaction.");
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
