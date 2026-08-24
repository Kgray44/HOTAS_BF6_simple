#include "controller_readiness.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

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
    process.setProcessChannelMode(QProcess::MergedChannels);
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
    result.output = decodeProcessOutput(process.readAll());
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
    requirements.buttons = std::clamp(requirements.buttons, 0, kMaximumVirtualButtons);
    requirements.continuousPovs = std::clamp(requirements.continuousPovs, 0, 4);
    requirements.discretePovs = std::clamp(requirements.discretePovs, 0, 4);
    requirements.incompatiblePovMix = requirements.continuousPovs > 0 && requirements.discretePovs > 0;
    return requirements;
}

bool ControllerReadinessService::isVJoySufficient(const VJoyCapabilities &vjoy,
                                                   const MapperOutputRequirements &requirements)
{
    return vjoy.installed && vjoy.configurationUtilityAvailable && vjoy.driverReady
        && vjoy.devicePresent && !vjoy.busy && capabilityAxesSatisfy(vjoy.axes, requirements.axes)
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
                                                             const HidHideCapabilities &hidhide)
{
    MapperOutputRequirements effectiveRequirements = requirements;
    // Empty button routes are not equivalent to zero output capacity: the
    // established mapper safely creates its bounded 1:1 defaults when it sees
    // a controller. Plan for that current default rather than surprising a
    // new user with an eight-button virtual device for a fifteen-button HOTAS.
    if (effectiveRequirements.buttons == 0 && physical.connected) {
        effectiveRequirements.buttons = std::clamp(physical.buttons, 0, kMaximumVirtualButtons);
    }
    ControllerReadinessPlan plan;
    plan.physical = physical;
    plan.requirements = effectiveRequirements;
    plan.vjoy = vjoy;
    plan.hidhide = hidhide;

    if (!physical.connected) {
        plan.findings.append(QStringLiteral("No physical DirectInput controller is detected."));
    } else {
        plan.findings.append(QStringLiteral("CONTROLLER DETECTED — %1 · %2 axes · %3 buttons · %4 POV")
            .arg(physical.name).arg(std::count(physical.axes.begin(), physical.axes.end(), true))
            .arg(physical.buttons).arg(physical.povs));
    }

    if (effectiveRequirements.incompatiblePovMix) {
        plan.findings.append(QStringLiteral("Current profiles request both continuous and discrete native POV output. vJoyConfig configures one POV type at a time; resolve this in Advanced settings before automatic setup."));
    }
    plan.vjoyNeedsChanges = !isVJoySufficient(vjoy, effectiveRequirements);
    if (!plan.vjoyNeedsChanges) {
        plan.findings.append(QStringLiteral("VJOY READY — Device %1 already satisfies %2.")
            .arg(vjoy.deviceId).arg(describeVJoyRequirement(effectiveRequirements)));
    } else if (!vjoy.installed || !vjoy.configurationUtilityAvailable) {
        plan.findings.append(QStringLiteral("VJOY NOT DETECTED — Install vJoy before configuring virtual output."));
    } else if (vjoy.busy) {
        plan.findings.append(QStringLiteral("VJOY DEVICE %1 IS BUSY — close the application currently owning it before setup.").arg(vjoy.deviceId));
    } else {
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
    if (!hideRequired) {
        plan.findings.append(QStringLiteral("HIDHIDE WAITING — connect and select a physical controller before hiding an exact device instance."));
    } else if (!hidhide.installed || !hidhide.cliAvailable || !hidhide.serviceReady) {
        plan.findings.append(QStringLiteral("HIDHIDE NOT DETECTED — install HidHide for optional game-side physical-device hiding."));
    } else if (hidhide.cloakKnown && hidhide.cloaked && hidhide.mapperAllowlisted
               && hidhide.selectedControllerHidden) {
        plan.findings.append(QStringLiteral("HIDHIDE READY — HOTAS BF6 is allowed and the selected physical controller is hidden from ordinary applications."));
    } else if (!hidhide.selectedControllerResolved) {
        plan.findings.append(QStringLiteral("HIDHIDE NEEDS MANUAL REVIEW — the selected controller has no exact HID instance identity. HOTAS BF6 will not hide by friendly name."));
    } else if (!hidhide.cloakKnown) {
        plan.findings.append(QStringLiteral("HIDHIDE STATUS UNAVAILABLE — read the driver state in the HidHide client before changing it."));
    } else {
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
    if (!plan.vjoyNeedsChanges && !plan.hidhideNeedsChanges && physical.connected) {
        plan.state = ControllerReadinessState::Ready;
        plan.status = QStringLiteral("READY FOR BF6 BINDING — Mapping remains Off until you enable it.");
    } else {
        plan.state = ControllerReadinessState::NeedsChanges;
        plan.status = plan.canApplyAutomatically
            ? QStringLiteral("Review the proposed driver changes, then choose APPLY AUTOMATICALLY.")
            : QStringLiteral("Readiness needs attention. Use the guided details or the supported driver tools.");
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
    case ControllerReadinessState::Failed: return QStringLiteral("FAILED");
    case ControllerReadinessState::RollingBack: return QStringLiteral("ROLLING BACK");
    }
    return QStringLiteral("UNKNOWN");
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
    // Deliberately verify the exact instance path, never a friendly name. This
    // refuses automatic hiding when two identical controllers cannot be told apart.
    if (!physical.hidInstanceId.isEmpty()) {
        const SetupProcessResult devices = runHidHide(false, {QStringLiteral("--dev-gaming")});
        result.selectedControllerResolved = devices.succeeded()
            && outputContainsDevice(devices.output, physical.hidInstanceId);
    }
    return result;
}

const ControllerReadinessPlan &ControllerReadinessService::inspect(const MapperConfiguration &configuration,
                                                                     const PhysicalControllerCapabilities &physical)
{
    if (m_transactionActive) return m_plan;
    m_configuration = configuration;
    m_physical = physical;
    const MapperOutputRequirements requirements = requirementsFor(configuration);
    const VJoyCapabilities vjoy = inspectVJoy(configuration.vjoyDeviceId);
    const HidHideCapabilities hidhide = inspectHidHide(physical);
    m_plan = planFor(physical, requirements, vjoy, hidhide);
    return m_plan;
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

bool ControllerReadinessService::applyVJoy(const ControllerReadinessPlan &plan, Journal *journal,
                                            QString *failure)
{
    if (!plan.vjoyNeedsChanges) return true;
    if (!plan.vjoyCanApply || plan.vjoy.busy || !plan.vjoy.forceFeedbackKnown) {
        if (failure) *failure = QStringLiteral("vJoy is not safe to configure automatically.");
        return false;
    }
    journal->vjoyRestoreCommand = plan.vjoy.restoreCommand;
    journal->vjoyWasAbsent = !plan.vjoy.devicePresent;
    const SetupProcessResult applied = runVJoy(true, vjoyConfigurationArguments(plan.vjoy, plan.requirements));
    if (!applied.succeeded()) {
        if (failure) *failure = applied.error.isEmpty() ? QStringLiteral("vJoy configuration failed") : applied.error;
        return false;
    }
    journal->vjoyChanged = true;
    return true;
}

bool ControllerReadinessService::applyHidHide(const ControllerReadinessPlan &plan, Journal *journal,
                                              QString *failure)
{
    if (!plan.hidhideNeedsChanges) return true;
    if (!plan.hidhideCanApply || !plan.hidhide.selectedControllerResolved) {
        if (failure) *failure = QStringLiteral("HidHide cannot identify the exact selected controller safely.");
        return false;
    }
    journal->mapperExecutable = mapperExecutablePath();
    journal->controllerInstanceId = plan.physical.hidInstanceId;
    // Critical ordering: allow the mapper before the selected physical device
    // is hidden or cloaking changes. Existing lists and unrelated devices stay intact.
    if (!plan.hidhide.mapperAllowlisted) {
        const SetupProcessResult allow = runHidHide(true,
            {QStringLiteral("--app-reg"), journal->mapperExecutable});
        if (!allow.succeeded()) {
            if (failure) *failure = allow.error.isEmpty() ? QStringLiteral("Could not allow HOTAS BF6 through HidHide") : allow.error;
            return false;
        }
        journal->mapperWasAdded = true;
    }
    if (!plan.hidhide.selectedControllerHidden) {
        const SetupProcessResult hidden = runHidHide(true,
            {QStringLiteral("--dev-hide"), journal->controllerInstanceId});
        if (!hidden.succeeded()) {
            if (failure) *failure = hidden.error.isEmpty() ? QStringLiteral("Could not hide the selected controller") : hidden.error;
            return false;
        }
        journal->controllerWasHidden = true;
    }
    if (!plan.hidhide.cloaked) {
        const SetupProcessResult cloak = runHidHide(true, {QStringLiteral("--cloak-on")});
        if (!cloak.succeeded()) {
            if (failure) *failure = cloak.error.isEmpty() ? QStringLiteral("Could not enable HidHide cloaking") : cloak.error;
            return false;
        }
        journal->cloakWasEnabled = true;
    }
    return true;
}

bool ControllerReadinessService::rollback(Journal *journal, QString *failure)
{
    bool success = true;
    QStringList problems;
    if (journal->cloakWasEnabled && !runHidHide(true, {QStringLiteral("--cloak-off")}).succeeded()) {
        success = false; problems.append(QStringLiteral("could not disable newly enabled cloaking"));
    }
    if (journal->controllerWasHidden && !runHidHide(true,
        {QStringLiteral("--dev-unhide"), journal->controllerInstanceId}).succeeded()) {
        success = false; problems.append(QStringLiteral("could not unhide the selected controller"));
    }
    if (journal->mapperWasAdded && !runHidHide(true,
        {QStringLiteral("--app-unreg"), journal->mapperExecutable}).succeeded()) {
        success = false; problems.append(QStringLiteral("could not remove the newly added mapper allowlist entry"));
    }
    if (journal->vjoyChanged) {
        const QStringList restore = journal->vjoyWasAbsent
            ? QStringList{QStringLiteral("-d"), QString::number(m_configuration.vjoyDeviceId)}
            : QProcess::splitCommand(journal->vjoyRestoreCommand);
        if (restore.isEmpty() || !runVJoy(true, restore).succeeded()) {
            success = false; problems.append(QStringLiteral("could not restore vJoy Device 1"));
        }
    }
    if (failure && !success) *failure = problems.join(QStringLiteral("; "));
    return success;
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
    QString failure;
    bool completed = applyVJoy(m_plan, &journal, &failure) && applyHidHide(m_plan, &journal, &failure);
    if (completed) completed = verifyReady();
    if (!completed) {
        QString rollbackFailure;
        rollback(&journal, &rollbackFailure);
        m_plan.state = ControllerReadinessState::Failed;
        m_plan.status = rollbackFailure.isEmpty()
            ? QStringLiteral("Automatic setup failed: %1. Mapping remains Off.").arg(failure)
            : QStringLiteral("Automatic setup failed: %1. Rollback needs review: %2").arg(failure, rollbackFailure);
        m_transactionActive = false;
        return false;
    }
    journal.available = journal.vjoyChanged || journal.mapperWasAdded || journal.controllerWasHidden || journal.cloakWasEnabled;
    m_journal = std::move(journal);
    m_plan.status = QStringLiteral("READY FOR BF6 BINDING — automatic setup was verified. Mapping remains Off.");
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
        inspect(m_configuration, m_physical);
        m_plan.status = QStringLiteral("Automatic setup changes were undone. Mapping remains Off.");
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
