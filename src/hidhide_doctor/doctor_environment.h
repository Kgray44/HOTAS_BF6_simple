#pragma once

#include "doctor_types.h"

#include <QHash>
#include <QList>

namespace hotas::doctor {

enum class CpuArchitecture { Unknown, X64, Arm64, X86 };
enum class PrivilegeState { Unelevated, Elevated, PermissionLimited };
enum class RepairCapabilityTier { DiagnosisSupported, ConfigurationRepairSupported, ComponentRepairSupported, PackageRepairSupported, UpgradeRepairSupported, RecoverySupported };

struct PlatformFingerprint final {
    QString windowsEdition;
    QString windowsVersion;
    quint32 build = 0;
    quint32 revision = 0;
    CpuArchitecture nativeArchitecture = CpuArchitecture::Unknown;
    CpuArchitecture processArchitecture = CpuArchitecture::Unknown;
    bool wow64OrEmulated = false;
    QString localeName;
};

struct HidHideComponentEvidence final {
    bool present = false;
    QString provider;
    QString clientVersion;
    QString driverVersion;
    QString packageVersion;
    CpuArchitecture packageArchitecture = CpuArchitecture::Unknown;
    QStringList protocolCapabilities;
};

struct DoctorCapabilitySet final {
    bool diagnosisSupported = true;
    RepairCapabilityTier highestQualifiedRepairTier = RepairCapabilityTier::DiagnosisSupported;
    bool directProtocolAvailable = false;
    bool helperArchitectureCompatible = false;
};

// Fixture-only device data gives the Phase 0 harness a stable way to cover
// device cardinality and malformed observations without touching SetupAPI,
// HidHide, DirectInput, Raw Input, or real controllers.
struct DeviceEcosystemFixture final {
    int physicalControllerCount = 0;
    int malformedObservationCount = 0;
    QStringList vendorLabels;
};

// Context from a HOTAS BF6 launch is a hint, not a configuration command.
// Phase 3 uses it only after the executable/device identity was independently
// observed.  Standalone Doctor sessions leave this empty and therefore never
// invent an expected HOTAS configuration.
struct HotasRepairIntent final {
    bool suppliedByHotas = false;
    QString expectedExecutable;
    QStringList expectedPhysicalDeviceIds;
    QStringList expectedVirtualOutputIds;
    std::optional<bool> expectedCloakState;
    std::optional<bool> expectedInverseState;
};

struct DoctorEnvironment final {
    PlatformFingerprint platform;
    HidHideComponentEvidence hidhide;
    DoctorCapabilitySet capabilities;
    PrivilegeState privilege = PrivilegeState::Unelevated;
    bool hotasBf6Present = false;
    bool freshUser = true;
    DeviceEcosystemFixture devices;
    HotasRepairIntent repairIntent;
};

struct PlatformQualification final {
    QString qualificationId;
    quint32 minimumBuild = 0;
    quint32 maximumBuild = 0;
    CpuArchitecture architecture = CpuArchitecture::Unknown;
    RepairCapabilityTier tier = RepairCapabilityTier::DiagnosisSupported;
    QString testEvidenceReference;
    bool matches(const DoctorEnvironment &environment) const;
};

struct ProtocolProbeResult final {
    DoctorCheckStatus status = DoctorCheckStatus::Unknown;
    QString operation;
    QString value;
    std::optional<NativeError> nativeError;
    qint64 durationMs = 0;
};

class IPlatformEnvironmentProvider {
public:
    virtual ~IPlatformEnvironmentProvider() = default;
    virtual DoctorEnvironment collect() = 0;
};

class IHidHideProtocolProvider {
public:
    virtual ~IHidHideProtocolProvider() = default;
    virtual ProtocolProbeResult probe(const QString &operation) = 0;
};

class FixturePlatformEnvironmentProvider final : public IPlatformEnvironmentProvider {
public:
    explicit FixturePlatformEnvironmentProvider(DoctorEnvironment fixture);
    DoctorEnvironment collect() override;
private:
    DoctorEnvironment m_fixture;
};

class FixtureHidHideProtocolProvider final : public IHidHideProtocolProvider {
public:
    void setResult(QString operation, ProtocolProbeResult result);
    ProtocolProbeResult probe(const QString &operation) override;
private:
    QHash<QString, ProtocolProbeResult> m_results;
};

namespace fixtures {
DoctorEnvironment windows11X64Healthy();
DoctorEnvironment windows11DifferentBuild();
DoctorEnvironment windows10X64Baseline();
DoctorEnvironment windows11Arm64();
DoctorEnvironment unknownFutureWindows();
DoctorEnvironment hidHideAbsent();
DoctorEnvironment clientDriverMismatch();
DoctorEnvironment wrongArchitecturePackage();
DoctorEnvironment freshNonEnglishUser();
DoctorEnvironment hidHideCleanInstall();
DoctorEnvironment zeroControllers();
DoctorEnvironment multipleControllersWithMalformedObservation();
} // namespace fixtures

QString displayName(CpuArchitecture architecture);
QString displayName(RepairCapabilityTier tier);

} // namespace hotas::doctor
