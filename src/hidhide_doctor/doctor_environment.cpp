#include "doctor_environment.h"

namespace hotas::doctor {

bool PlatformQualification::matches(const DoctorEnvironment &environment) const
{
    const quint32 build = environment.platform.build;
    return environment.platform.nativeArchitecture == architecture
        && build >= minimumBuild && (maximumBuild == 0 || build <= maximumBuild);
}

FixturePlatformEnvironmentProvider::FixturePlatformEnvironmentProvider(DoctorEnvironment fixture)
    : m_fixture(std::move(fixture)) {}

DoctorEnvironment FixturePlatformEnvironmentProvider::collect()
{
    return m_fixture;
}

void FixtureHidHideProtocolProvider::setResult(QString operation, ProtocolProbeResult result)
{
    m_results.insert(operation.trimmed().toUpper(), std::move(result));
}

ProtocolProbeResult FixtureHidHideProtocolProvider::probe(const QString &operation)
{
    const QString key = operation.trimmed().toUpper();
    if (m_results.contains(key)) return m_results.value(key);
    return {DoctorCheckStatus::NotApplicable, key, {}, std::nullopt, 0};
}

namespace {
DoctorEnvironment baseEnvironment(quint32 build, CpuArchitecture architecture)
{
    DoctorEnvironment environment;
    environment.platform.windowsEdition = QStringLiteral("Windows 11 Pro");
    environment.platform.windowsVersion = QStringLiteral("10.0");
    environment.platform.build = build;
    environment.platform.revision = 1000;
    environment.platform.nativeArchitecture = architecture;
    environment.platform.processArchitecture = architecture;
    environment.platform.doctorBinaryArchitecture = architecture;
    environment.platform.helperBinaryArchitecture = architecture;
    environment.platform.localeName = QStringLiteral("en-US");
    environment.hidhide.present = true;
    environment.hidhide.provider = QStringLiteral("official-nefarius");
    environment.hidhide.clientVersion = QStringLiteral("1.5.230.0");
    environment.hidhide.driverVersion = QStringLiteral("1.5.230.0");
    environment.hidhide.packageVersion = QStringLiteral("1.5.230.0");
    environment.hidhide.packageArchitecture = architecture;
    environment.hidhide.protocolCapabilities = {QStringLiteral("GET_ACTIVE"), QStringLiteral("GET_INVERSE"),
        QStringLiteral("GET_WHITELIST"), QStringLiteral("GET_BLACKLIST")};
    environment.capabilities.directProtocolAvailable = true;
    environment.capabilities.helperArchitectureCompatible = true;
    environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::DiagnosisSupported;
    environment.devices.physicalControllerCount = 2;
    environment.devices.vendorLabels = {QStringLiteral("Vendor Alpha"), QStringLiteral("Vendor Beta")};
    return environment;
}
} // namespace

namespace fixtures {

DoctorEnvironment windows11X64Healthy() { return baseEnvironment(26100, CpuArchitecture::X64); }
DoctorEnvironment windows11DifferentBuild() { return baseEnvironment(22631, CpuArchitecture::X64); }
DoctorEnvironment windows10X64Baseline()
{
    DoctorEnvironment environment = baseEnvironment(19045, CpuArchitecture::X64);
    environment.platform.windowsEdition = QStringLiteral("Windows 10 Pro");
    return environment;
}
DoctorEnvironment windows11Arm64() { return baseEnvironment(26100, CpuArchitecture::Arm64); }
DoctorEnvironment unknownFutureWindows()
{
    DoctorEnvironment environment = baseEnvironment(99999, CpuArchitecture::X64);
    environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::DiagnosisSupported;
    return environment;
}
DoctorEnvironment hidHideAbsent()
{
    DoctorEnvironment environment = windows11X64Healthy();
    environment.hidhide = {};
    environment.capabilities.directProtocolAvailable = false;
    return environment;
}
DoctorEnvironment clientDriverMismatch()
{
    DoctorEnvironment environment = windows11X64Healthy();
    environment.hidhide.driverVersion = QStringLiteral("1.5.212.0");
    return environment;
}
DoctorEnvironment wrongArchitecturePackage()
{
    DoctorEnvironment environment = windows11Arm64();
    environment.hidhide.packageArchitecture = CpuArchitecture::X64;
    environment.capabilities.helperArchitectureCompatible = false;
    return environment;
}
DoctorEnvironment freshNonEnglishUser()
{
    DoctorEnvironment environment = windows11DifferentBuild();
    environment.platform.localeName = QStringLiteral("de-DE");
    environment.hotasBf6Present = false;
    environment.freshUser = true;
    return environment;
}
DoctorEnvironment hidHideCleanInstall()
{
    DoctorEnvironment environment = windows11X64Healthy();
    environment.hotasBf6Present = false;
    environment.freshUser = true;
    return environment;
}
DoctorEnvironment zeroControllers()
{
    DoctorEnvironment environment = hidHideCleanInstall();
    environment.devices.physicalControllerCount = 0;
    environment.devices.vendorLabels.clear();
    return environment;
}
DoctorEnvironment multipleControllersWithMalformedObservation()
{
    DoctorEnvironment environment = windows11X64Healthy();
    environment.devices.physicalControllerCount = 4;
    environment.devices.malformedObservationCount = 1;
    environment.devices.vendorLabels = {QStringLiteral("Vendor Alpha"), QStringLiteral("Vendor Beta"),
        QStringLiteral("Vendor Gamma"), QStringLiteral("Malformed fixture")};
    return environment;
}

} // namespace fixtures

QString displayName(CpuArchitecture architecture)
{
    switch (architecture) {
    case CpuArchitecture::X64: return QStringLiteral("x64");
    case CpuArchitecture::Arm64: return QStringLiteral("ARM64");
    case CpuArchitecture::X86: return QStringLiteral("x86");
    case CpuArchitecture::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString displayName(RepairCapabilityTier tier)
{
    switch (tier) {
    case RepairCapabilityTier::DiagnosisSupported: return QStringLiteral("Diagnosis Supported");
    case RepairCapabilityTier::ConfigurationRepairSupported: return QStringLiteral("Configuration Repair Supported");
    case RepairCapabilityTier::ComponentRepairSupported: return QStringLiteral("Component Repair Supported");
    case RepairCapabilityTier::PackageRepairSupported: return QStringLiteral("Package Repair Supported");
    case RepairCapabilityTier::UpgradeRepairSupported: return QStringLiteral("Upgrade Repair Supported");
    case RepairCapabilityTier::RecoverySupported: return QStringLiteral("Recovery Supported");
    }
    return QStringLiteral("Diagnosis Supported");
}

} // namespace hotas::doctor
