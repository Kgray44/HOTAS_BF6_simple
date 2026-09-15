#pragma once

#include "doctor_environment.h"
#include "doctor_session.h"

#include <QDateTime>
#include <QList>

#include <atomic>
#include <functional>

namespace hotas::doctor {

enum class DoctorArtifactKind { ClientExecutable, CliExecutable, DriverBinary, DriverStorePackage, InstallerMetadata, Unknown };
enum class SignatureTrustState { NotChecked, Trusted, Untrusted, NotSigned, Error, NotApplicable };
enum class DeviceClassification { Unknown, PhysicalGamingInput, VirtualGamingDevice, VJoyVirtualOutput, HidHideManagedLooking, StaleOrPhantom, ProblemDevice };

struct DoctorBuildProvenance final {
    QString version;
    QString sourceRevision;
    QString channel;
    QString processArchitecture;
};

struct FileArtifactObservation final {
    DoctorArtifactKind kind = DoctorArtifactKind::Unknown;
    QString role;
    QString path;
    bool exists = false;
    quint64 size = 0;
    QDateTime lastModified;
    QString fileVersion;
    QString productVersion;
    CpuArchitecture architecture = CpuArchitecture::Unknown;
    QString sha256;
    SignatureTrustState signatureTrust = SignatureTrustState::NotChecked;
    QString signer;
    std::optional<NativeError> nativeError;
};

struct DriverPackageObservation final {
    QString infName;
    QString provider;
    QString version;
    QString path;
    CpuArchitecture architecture = CpuArchitecture::Unknown;
    bool activeCandidate = false;
    bool staleCandidate = false;
    std::optional<NativeError> nativeError;
};

struct ServiceObservation final {
    bool present = false;
    QString serviceName;
    QString displayName;
    QString binaryPath;
    QString startType;
    QString currentState;
    QStringList dependencies;
    std::optional<NativeError> nativeError;
};

struct DeviceObservation final {
    QString instanceId;
    QString containerId;
    QString friendlyName;
    QString manufacturer;
    QString className;
    QStringList hardwareIds;
    QStringList compatibleIds;
    QString location;
    QString driverProvider;
    QString driverVersion;
    quint32 statusFlags = 0;
    quint32 problemCode = 0;
    bool present = false;
    DeviceClassification classification = DeviceClassification::Unknown;
    QList<QString> propertyFailures;
    std::optional<NativeError> nativeError;
};

struct EventObservation final {
    QString channel;
    QString provider;
    quint32 eventId = 0;
    QString level;
    QDateTime timestamp;
    QString summary;
    EvidenceSensitivity sensitivity = EvidenceSensitivity::RequiresRedaction;
    std::optional<NativeError> nativeError;
};

struct ProtocolObservation final {
    QString operation;
    DoctorCheckStatus status = DoctorCheckStatus::Unknown;
    QString value;
    QStringList multiStringValues;
    std::optional<NativeError> nativeError;
    qint64 durationMs = 0;
    bool sizeNegotiation = false;
};

struct PendingRestartObservation final {
    QString source;
    QString value;
    EvidenceSensitivity sensitivity = EvidenceSensitivity::RequiresRedaction;
    std::optional<NativeError> nativeError;
};

struct ProcessObservation final {
    QString imageName;
    quint32 processId = 0;
    QString executablePath;
    std::optional<NativeError> nativeError;
};

// A provider can record an exact catalog observation when it has a direct
// Windows source for that requirement.  A missing entry never means healthy:
// the engine emits an explicit Unknown/NotApplicable result instead.
struct CatalogObservation final {
    QString checkId;
    DoctorCheckStatus status = DoctorCheckStatus::Unknown;
    QString summary;
    QString technicalDetails;
    std::optional<NativeError> nativeError;
};

struct ReadOnlyDiagnosticSnapshot final {
    DoctorBuildProvenance build;
    DoctorEnvironment environment;
    QList<FileArtifactObservation> artifacts;
    QList<DriverPackageObservation> driverPackages;
    ServiceObservation service;
    QList<DeviceObservation> devices;
    QList<EventObservation> events;
    QList<EventObservation> werReports;
    QList<EventObservation> setupApiEvidence;
    QList<ProtocolObservation> protocol;
    QStringList registryWhitelist;
    QStringList registryBlacklist;
    std::optional<bool> registryActive;
    std::optional<bool> registryInverse;
    QList<PendingRestartObservation> pendingRestart;
    QList<ProcessObservation> processes;
    QStringList hidHideInterfacePaths;
    QStringList hidHideFilterRegistrations;
    std::optional<NativeError> hidHideFilterEnumerationError;
    QList<CatalogObservation> catalogObservations;
    QStringList contradictions;
    QStringList operationalLimits;
};

// This interface intentionally has no repair method and exposes no mutating
// HidHide operation.  Implementations may only take observational snapshots.
class IReadOnlyDiagnosticProvider {
public:
    virtual ~IReadOnlyDiagnosticProvider() = default;
    using ObservationProgress = std::function<void(const DoctorCheckId &, int)>;
    virtual ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *cancelled = nullptr,
        ObservationProgress onProgress = {}) = 0;
};

class ReadOnlyWindowsDiagnosticProvider final : public IReadOnlyDiagnosticProvider {
public:
    ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *cancelled = nullptr,
        ObservationProgress onProgress = {}) override;
};

struct DiagnosticRunOutcome final {
    DoctorSession session;
    ReadOnlyDiagnosticSnapshot snapshot;
    CatalogCoverageReport catalogCoverage;
    QDateTime startedAt;
    QDateTime completedAt;
    qint64 durationMs = 0;
    bool cancelled = false;
};

class DoctorDiagnosticEngine final {
public:
    using ProgressCallback = std::function<void(const DoctorSession &)>;

    DiagnosticRunOutcome run(IReadOnlyDiagnosticProvider &provider,
        std::atomic_bool *cancelled = nullptr, ProgressCallback onProgress = {}) const;
    DoctorSession createPreparedSession() const;
    static QByteArray serializeJson(const DiagnosticRunOutcome &outcome, bool redactSensitive);

private:
    static DoctorCatalog registeredCatalog();
    static DoctorPhase phaseFor(const DoctorCheckId &id);
    static DoctorCheckResult evaluate(const DoctorCheckDefinition &definition,
        const ReadOnlyDiagnosticSnapshot &snapshot);
};

QString displayName(DoctorArtifactKind kind);
QString displayName(SignatureTrustState state);
QString displayName(DeviceClassification classification);

} // namespace hotas::doctor
