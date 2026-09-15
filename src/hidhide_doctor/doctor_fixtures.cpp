#include "doctor_fixtures.h"

namespace hotas::doctor {
namespace {

ReadOnlyDiagnosticSnapshot healthySnapshot()
{
    ReadOnlyDiagnosticSnapshot snapshot;
    snapshot.environment = fixtures::windows11X64Healthy();
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
        QStringLiteral("Contradictory Evidence")};
}

ReadOnlyDiagnosticSnapshot createDevelopmentFixture(const QString &name, QString *displayLabel)
{
    ReadOnlyDiagnosticSnapshot snapshot = healthySnapshot();
    const QString key = normalizedName(name);
    QString label = QStringLiteral("Healthy System");
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
    }
    if (displayLabel) *displayLabel = label;
    return snapshot;
}

} // namespace hotas::doctor
