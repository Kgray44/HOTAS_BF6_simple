#include "doctor_fixtures.h"

namespace hotas::doctor {
namespace {

ReadOnlyDiagnosticSnapshot healthySnapshot()
{
    ReadOnlyDiagnosticSnapshot snapshot;
    snapshot.environment = fixtures::windows11X64Healthy();
    snapshot.environment.hotasBf6Present = true;
    snapshot.environment.repairIntent.suppliedByHotas = true;
    snapshot.environment.repairIntent.expectedExecutable = QStringLiteral("C:\\Program Files\\HOTAS BF6\\HOTAS BF6.exe");
    snapshot.environment.repairIntent.expectedPhysicalDeviceIds = {QStringLiteral("HID\\VID_1234&PID_0001")};
    snapshot.environment.repairIntent.expectedVirtualOutputIds = {QStringLiteral("ROOT\\VJOY\\0000")};
    snapshot.service = {true, QStringLiteral("HidHide"), QStringLiteral("HidHide"), QStringLiteral("system32\\drivers\\HidHide.sys"), QStringLiteral("system"), QStringLiteral("running"), {}, std::nullopt};
    snapshot.artifacts = {
        {DoctorArtifactKind::ClientExecutable, QStringLiteral("client"), QStringLiteral("C:\\Program Files\\HidHide\\HidHideClient.exe"), true, 1, {}, QStringLiteral("1.5.230.0"), {}, CpuArchitecture::X64, QStringLiteral("fixture-client"), SignatureTrustState::Trusted, {}, std::nullopt},
        {DoctorArtifactKind::DriverBinary, QStringLiteral("driver"), QStringLiteral("C:\\Windows\\System32\\drivers\\HidHide.sys"), true, 1, {}, QStringLiteral("1.5.230.0"), {}, CpuArchitecture::X64, QStringLiteral("fixture-driver"), SignatureTrustState::Trusted, {}, std::nullopt}};
    snapshot.protocol = {
        {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Healthy, QStringLiteral("Control endpoint opened read-only"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_ACTIVE"), DoctorCheckStatus::Healthy, QStringLiteral("true"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_INVERSE"), DoctorCheckStatus::Healthy, QStringLiteral("false"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Healthy, QStringLiteral("48 bytes"), {}, std::nullopt, 1, true},
        {QStringLiteral("GET_WHITELIST"), DoctorCheckStatus::Healthy, QStringLiteral("1 entry"), {QStringLiteral("C:\\Program Files\\HOTAS BF6\\HOTAS BF6.exe")}, std::nullopt, 1, false},
        {QStringLiteral("GET_BLACKLIST_SIZE"), DoctorCheckStatus::Healthy, QStringLiteral("36 bytes"), {}, std::nullopt, 1, true},
        {QStringLiteral("GET_BLACKLIST"), DoctorCheckStatus::Healthy, QStringLiteral("1 entry"), {QStringLiteral("HID\\VID_1234&PID_0001")}, std::nullopt, 1, false}};
    snapshot.devices = {{QStringLiteral("HID\\VID_1234&PID_0001"), QStringLiteral("{fixture-controller}"), QStringLiteral("Fixture controller"), QStringLiteral("Fixture"), QStringLiteral("HIDClass"), {}, {}, {}, QStringLiteral("Fixture Driver"), QStringLiteral("1.0"), 0, 0, true, DeviceClassification::PhysicalGamingInput, {}, std::nullopt, 1, 4, {}}};
    return snapshot;
}

QString normalizedName(QString name)
{
    return name.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char(' ')).replace(QLatin1Char('-'), QLatin1Char(' '));
}

} // namespace

QStringList developmentFixtureNames()
{
    return {QStringLiteral("Healthy System"), QStringLiteral("GetWhitelist 0x57"), QStringLiteral("Broken HID Device"),
        QStringLiteral("Client Driver Mismatch"), QStringLiteral("Pending Restart"), QStringLiteral("Partial Install"),
        QStringLiteral("Contradictory Evidence"), QStringLiteral("Missing HOTAS Exemption"),
        QStringLiteral("Stale HOTAS Exemption"), QStringLiteral("Hidden Virtual Output"),
        QStringLiteral("Deep Incomplete Driver Replacement"), QStringLiteral("Same Version Reinstall"),
        QStringLiteral("Approved Upgrade"), QStringLiteral("Failed Install"), QStringLiteral("Reboot Required"),
        QStringLiteral("Post-Reboot Success"), QStringLiteral("Post-Reboot Failure"), QStringLiteral("Config Restoration Required"),
        QStringLiteral("Rollback"), QStringLiteral("Rollback Across Reboot"), QStringLiteral("Recovery"),
        QStringLiteral("Recovery Unavailable"), QStringLiteral("Missing HidHide Service Registration"),
        QStringLiteral("Incorrect HidHide Filter Registration"), QStringLiteral("Wrong Architecture Package"),
        QStringLiteral("Bad Package Signature"), QStringLiteral("Bad Package Hash"), QStringLiteral("Package Download Failure"),
        QStringLiteral("Insufficient Disk Space"), QStringLiteral("Maximum Reboot Count Exceeded"),
        QStringLiteral("Client Only"), QStringLiteral("Driver Only"), QStringLiteral("Restart Later"),
        QStringLiteral("Restart Now"), QStringLiteral("Doctor Restart Before OS Reboot"), QStringLiteral("Config Preserved"),
        QStringLiteral("Package Rollback Success"), QStringLiteral("Package Rollback Requires Reboot"),
        QStringLiteral("Helper Crash During Install"), QStringLiteral("Power Loss Reconciliation"),
        QStringLiteral("Unknown Windows Build"), QStringLiteral("ARM64 Architecture"), QStringLiteral("Stale Transaction")};
}

ReadOnlyDiagnosticSnapshot createDevelopmentFixture(const QString &name, QString *displayLabel)
{
    ReadOnlyDiagnosticSnapshot snapshot = healthySnapshot();
    const QString key = normalizedName(name);
    QString label = QStringLiteral("Healthy System");
    const auto configureDeepLab = [&snapshot] {
        snapshot.environment.hidhide.provider = QStringLiteral("fixture-official-nefarius");
        snapshot.environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::RecoverySupported;
    };
    if (key.contains(QStringLiteral("whitelist")) || key.contains(QStringLiteral("0x57"))) {
        label = QStringLiteral("GetWhitelist 0x57");
        snapshot.protocol[3] = {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Failed, QStringLiteral("GET_WHITELIST failed"), {},
            NativeError{NativeErrorDomain::Win32, 0x57, QStringLiteral("ERROR_INVALID_PARAMETER"), QStringLiteral("The parameter is incorrect.")}, 3, true};
    } else if (key.contains(QStringLiteral("broken")) || key.contains(QStringLiteral("hid device"))) {
        label = QStringLiteral("Broken HID Device");
        snapshot.devices.append({QStringLiteral("HID\\VID_FAULT&PID_0001"), QStringLiteral("{fixture-fault}"), QStringLiteral("Malformed fixture HID"), QStringLiteral("Fixture"), QStringLiteral("HIDClass"), {}, {}, {}, {}, {}, 0, 0, true,
            DeviceClassification::ProblemDevice, {QStringLiteral("SPDRP_HARDWAREID: ERROR_INVALID_DATA")}, std::nullopt, 0, 0, {}});
    } else if (key.contains(QStringLiteral("mismatch"))) {
        label = QStringLiteral("Client Driver Mismatch");
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.4.181.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.4.181.0");
    } else if (key.contains(QStringLiteral("pending")) || key.contains(QStringLiteral("restart"))) {
        label = QStringLiteral("Pending Restart");
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.4.181.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.4.181.0");
        snapshot.driverPackages.append({QStringLiteral("hidhide.inf_amd64"), QStringLiteral("Nefarius"), QStringLiteral("1.5.230.0"), QStringLiteral("fixture-driver-store"), CpuArchitecture::X64, true, false, std::nullopt});
        snapshot.pendingRestart.append({QStringLiteral("PendingFileRenameOperations"), QStringLiteral("fixture HidHide replacement"), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt});
    } else if (key.contains(QStringLiteral("partial"))) {
        label = QStringLiteral("Partial Install");
        snapshot.service = {};
        snapshot.artifacts.removeLast();
        snapshot.environment.hidhide.driverVersion.clear();
        snapshot.protocol.clear();
    } else if (key.contains(QStringLiteral("contradict"))) {
        label = QStringLiteral("Contradictory Evidence");
        snapshot.registryActive = false;
        snapshot.contradictions.append(QStringLiteral("Persistent registry active state is false while GET_ACTIVE returned true."));
    } else if (key.contains(QStringLiteral("missing hotas"))) {
        label = QStringLiteral("Missing HOTAS Exemption");
        snapshot.protocol[4].multiStringValues.clear();
        snapshot.protocol[4].value = QStringLiteral("0 entries");
        snapshot.registryWhitelist.clear();
    } else if (key.contains(QStringLiteral("stale hotas"))) {
        label = QStringLiteral("Stale HOTAS Exemption");
        snapshot.protocol[4].multiStringValues = {QStringLiteral("C:\\Stale\\HOTAS BF6.exe"), QStringLiteral("C:\\Program Files\\Steam\\Steam.exe")};
        snapshot.protocol[4].value = QStringLiteral("2 entries");
        snapshot.registryWhitelist = snapshot.protocol[4].multiStringValues;
    } else if (key.contains(QStringLiteral("hidden virtual"))) {
        label = QStringLiteral("Hidden Virtual Output");
        snapshot.protocol[6].multiStringValues.append(QStringLiteral("ROOT\\VJOY\\0000"));
        snapshot.protocol[6].value = QStringLiteral("2 entries");
        snapshot.registryBlacklist = snapshot.protocol[6].multiStringValues;
    } else if (key.contains(QStringLiteral("deep incomplete")) || key.contains(QStringLiteral("reboot required"))
        || key.contains(QStringLiteral("restart later")) || key.contains(QStringLiteral("restart now"))
        || key.contains(QStringLiteral("doctor restart")) || key.contains(QStringLiteral("config preserved"))) {
        if (key.contains(QStringLiteral("restart later")))
            label = QStringLiteral("Restart Later");
        else if (key.contains(QStringLiteral("restart now")))
            label = QStringLiteral("Restart Now");
        else if (key.contains(QStringLiteral("doctor restart")))
            label = QStringLiteral("Doctor Restart Before OS Reboot");
        else if (key.contains(QStringLiteral("config preserved")))
            label = QStringLiteral("Config Preserved");
        else if (key.contains(QStringLiteral("reboot")))
            label = QStringLiteral("Reboot Required");
        else
            label = QStringLiteral("Deep Incomplete Driver Replacement");
        configureDeepLab();
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.5.212.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.5.212.0");
        snapshot.driverPackages.append({QStringLiteral("hidhide.inf_amd64"), QStringLiteral("Nefarius"), QStringLiteral("1.5.230.0"), QStringLiteral("fixture-driver-store"), CpuArchitecture::X64, true, false, std::nullopt});
        snapshot.pendingRestart.append({QStringLiteral("PendingFileRenameOperations"), QStringLiteral("fixture HidHide replacement"), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt});
    } else if (key.contains(QStringLiteral("same version")) || key.contains(QStringLiteral("failed install"))
        || key.contains(QStringLiteral("client only")) || key.contains(QStringLiteral("driver only"))) {
        label = key.contains(QStringLiteral("failed")) ? QStringLiteral("Failed Install") : QStringLiteral("Same Version Reinstall");
        configureDeepLab();
        snapshot.service = {};
        snapshot.artifacts.removeLast();
        snapshot.environment.hidhide.driverVersion.clear();
    } else if (key.contains(QStringLiteral("approved upgrade"))) {
        label = QStringLiteral("Approved Upgrade");
        configureDeepLab();
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.5.212.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.5.212.0");
    } else if (key.contains(QStringLiteral("missing hidhide service"))) {
        label = QStringLiteral("Missing HidHide Service Registration");
        configureDeepLab();
        snapshot.catalogObservations.append({QStringLiteral("HD-PHASE4-SERVICE-REGISTRATION"), DoctorCheckStatus::Failed,
            QStringLiteral("Fixture exact HidHide service registration is absent."), {}, std::nullopt});
    } else if (key.contains(QStringLiteral("incorrect hidhide filter"))) {
        label = QStringLiteral("Incorrect HidHide Filter Registration");
        configureDeepLab();
        snapshot.catalogObservations.append({QStringLiteral("HD-PHASE4-FILTER-REGISTRATION"), DoctorCheckStatus::Failed,
            QStringLiteral("Fixture exact HidHide filter order is inconsistent."), {}, std::nullopt});
    } else if (key.contains(QStringLiteral("recovery")) || key.contains(QStringLiteral("rollback"))
        || key.contains(QStringLiteral("helper crash")) || key.contains(QStringLiteral("power loss"))
        || key.contains(QStringLiteral("stale transaction"))) {
        label = key.contains(QStringLiteral("unavailable")) ? QStringLiteral("Recovery Unavailable")
            : (key.contains(QStringLiteral("across")) ? QStringLiteral("Rollback Across Reboot")
            : (key.contains(QStringLiteral("rollback")) ? QStringLiteral("Rollback") : QStringLiteral("Recovery")));
        configureDeepLab();
        snapshot.catalogObservations.append({QStringLiteral("HD-PHASE4-RECOVERY-REQUIRED"), DoctorCheckStatus::Failed,
            QStringLiteral("Fixture durable repair transaction requires separately authorized recovery."), {}, std::nullopt});
        if (key.contains(QStringLiteral("unavailable"))) snapshot.environment.hidhide.provider = QStringLiteral("fixture-unapproved-provider");
    } else if (key.contains(QStringLiteral("post reboot success"))) {
        label = QStringLiteral("Post-Reboot Success");
        configureDeepLab();
    } else if (key.contains(QStringLiteral("post reboot failure")) || key.contains(QStringLiteral("maximum reboot"))) {
        label = key.contains(QStringLiteral("maximum")) ? QStringLiteral("Maximum Reboot Count Exceeded") : QStringLiteral("Post-Reboot Failure");
        configureDeepLab();
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.5.212.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.5.212.0");
    } else if (key.contains(QStringLiteral("config restoration"))) {
        label = QStringLiteral("Config Restoration Required");
        configureDeepLab();
        snapshot.registryWhitelist = {QStringLiteral("C:\\Conflicted\\External.exe")};
    } else if (key.contains(QStringLiteral("wrong architecture"))) {
        label = QStringLiteral("Wrong Architecture Package");
        snapshot.environment = fixtures::wrongArchitecturePackage();
    } else if (key.contains(QStringLiteral("unknown windows"))) {
        label = QStringLiteral("Unknown Windows Build");
        snapshot.environment = fixtures::unknownFutureWindows();
    } else if (key.contains(QStringLiteral("arm64"))) {
        label = QStringLiteral("ARM64 Architecture");
        snapshot.environment = fixtures::windows11Arm64();
    } else if (key.contains(QStringLiteral("bad package signature")) || key.contains(QStringLiteral("bad package hash"))
        || key.contains(QStringLiteral("download failure")) || key.contains(QStringLiteral("insufficient disk"))) {
        label = key.contains(QStringLiteral("signature")) ? QStringLiteral("Bad Package Signature")
            : (key.contains(QStringLiteral("hash")) ? QStringLiteral("Bad Package Hash")
            : (key.contains(QStringLiteral("download")) ? QStringLiteral("Package Download Failure") : QStringLiteral("Insufficient Disk Space")));
        configureDeepLab();
        snapshot.artifacts[1].fileVersion = QStringLiteral("1.5.212.0");
        snapshot.environment.hidhide.driverVersion = QStringLiteral("1.5.212.0");
        snapshot.driverPackages.append({QStringLiteral("hidhide.inf_amd64"), QStringLiteral("Nefarius"), QStringLiteral("1.5.230.0"), QStringLiteral("fixture-driver-store"), CpuArchitecture::X64, true, false, std::nullopt});
        snapshot.pendingRestart.append({QStringLiteral("PendingFileRenameOperations"), QStringLiteral("fixture HidHide replacement"), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt});
    }
    if (displayLabel) *displayLabel = label;
    return snapshot;
}

} // namespace hotas::doctor
