#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace hotas::doctor {

// These are value objects, not arbitrary UI labels.  Persisted evidence and
// future IPC refer to stable identifiers so labels can change without breaking
// reports, repair journals, or support tooling.
class DoctorSessionId final {
public:
    DoctorSessionId() = default;
    explicit DoctorSessionId(QString value);
    static DoctorSessionId create();
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class DoctorCheckId final {
public:
    DoctorCheckId() = default;
    explicit DoctorCheckId(QString value);
    bool isValid() const;
    const QString &value() const;
    friend bool operator==(const DoctorCheckId &, const DoctorCheckId &) = default;
private:
    QString m_value;
};

class DoctorStepId final {
public:
    DoctorStepId() = default;
    explicit DoctorStepId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class EvidenceId final {
public:
    EvidenceId() = default;
    explicit EvidenceId(QString value);
    static EvidenceId create();
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class FindingId final {
public:
    FindingId() = default;
    explicit FindingId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class DiagnosisId final {
public:
    DiagnosisId() = default;
    explicit DiagnosisId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class KnowledgeSignatureId final {
public:
    KnowledgeSignatureId() = default;
    explicit KnowledgeSignatureId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class DoctorOperationId final {
public:
    DoctorOperationId() = default;
    explicit DoctorOperationId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

enum class DoctorSessionState {
    Preparing,
    Diagnosing,
    Analyzing,
    DiagnosisComplete,
    AwaitingUser,
    RepairReady,
    Repairing,
    AwaitingElevation,
    AwaitingReboot,
    ResumingAfterReboot,
    Verifying,
    RepairComplete,
    DegradedComplete,
    Cancelled,
    FailedSafely,
};

enum class DoctorPhase {
    SystemEnvironment,
    InstallationDiscovery,
    PackageIntegrity,
    KernelDriverState,
    ProtocolApiHealth,
    ConfigurationIntegrity,
    DeviceEcosystem,
    IsolationVerification,
    WindowsEvidence,
    ConsistencyAnalysis,
    Diagnosis,
    RepairRecommendation,
    ExtendedInvestigation,
};

enum class DoctorCheckStatus {
    Waiting,
    Running,
    Healthy,
    Informational,
    Warning,
    Failed,
    PermissionLimited,
    Blocked,
    NotApplicable,
    Cancelled,
    TimedOut,
    Unknown,
    Inconclusive,
};

enum class DoctorOperationState { Waiting, Running, Completed, Failed, Cancelled, TimedOut };
enum class EvidenceKind { Observation, NativeResult, Capability, DerivedCorrelation, ReportProvenance };
enum class EvidenceProvenance { Direct, Derived, Fixture, ImportedLaunchContext };
enum class EvidenceSensitivity { SafeToExport, RequiresRedaction, SensitiveLocalOnly, PotentiallyIdentifying };
enum class NativeErrorDomain { None, Win32, NtStatus, HResult, Protocol, Process, Qt };
enum class FindingSeverity { Informational, Warning, Error, Critical };
enum class DiagnosisConfidence { Uncertain, Moderate, High, VeryHigh, Confirmed };
enum class UserActionState { NothingRequired, Required, Optional, Blocked };
// A session can change the current action as evidence arrives.  The ledger
// preserves those transitions for support reports without treating a current
// UI label as historical truth.
enum class UserActionLedgerState { NotRequired, Required, Waiting, Completed, Cancelled, Superseded, StillPending };
// Phase 2 deliberately describes what could be repaired without gaining any
// ability to perform that repair.  Keep this vocabulary distinct from the
// diagnosis itself and from the future RepairPlan risk/qualification model.
enum class Repairability {
    NotEvaluated,
    NoRepairRequired,
    AutomaticallyRepairable,
    AutomaticallyRepairableAfterRestart,
    PotentialRepairAvailableButUnqualified,
    ManualInterventionRequired,
    UpstreamOrComponentDefect,
    UnsupportedEnvironment,
    InsufficientEvidence,
    // Retained for report compatibility with Phase 0/1 records.
    NoQualifiedRepair,
    ConfigurationOnly,
    Repairable,
};
enum class RepairRiskClass { R0Observe, R1Configuration, R2Component, R3Package, R4ApprovedUpgrade, R5Recovery };
enum class RepairQualificationLevel { Experimental, LabQualified, FieldQualified, Retired };
enum class DiagnosisRole { Primary, Secondary, Contributing };

struct WorkWeight final {
    int units = 0;
    bool isValid() const { return units > 0; }
};

struct NativeError final {
    NativeErrorDomain domain = NativeErrorDomain::None;
    qint64 code = 0;
    QString symbolicName;
    QString message;
    bool isPresent() const { return domain != NativeErrorDomain::None; }
};

struct EvidenceRecord final {
    EvidenceId id;
    DoctorCheckId checkId;
    EvidenceKind kind = EvidenceKind::Observation;
    EvidenceProvenance provenance = EvidenceProvenance::Direct;
    EvidenceSensitivity sensitivity = EvidenceSensitivity::SafeToExport;
    QDateTime recordedAt;
    QString source;
    QString humanSummary;
    QString technicalDetails;
    QString structuredValue;
    std::optional<NativeError> nativeError;
    qint64 durationMs = 0;
    bool direct = true;
};

// A check result is deliberately not a finding or diagnosis.  It describes
// exactly one executed (or explicitly not-applicable) catalogue check and
// links it to raw evidence.  Later phases may reason over these records, but
// Phase 1 never turns a failed probe into an automatic repair conclusion.
struct DoctorCheckResult final {
    DoctorCheckId checkId;
    DoctorCheckStatus status = DoctorCheckStatus::Waiting;
    QString summary;
    QString technicalDetails;
    QList<EvidenceId> evidenceIds;
    std::optional<NativeError> nativeError;
    qint64 durationMs = 0;
    bool implementationConditional = false;
};

struct Finding final {
    FindingId id;
    FindingSeverity severity = FindingSeverity::Informational;
    QString title;
    QString explanation;
    QList<EvidenceId> evidenceIds;
    QString technicalExplanation;
    QString affectedObject;
    QString environmentalScope;
    DiagnosisConfidence confidence = DiagnosisConfidence::Uncertain;
    DoctorCheckStatus status = DoctorCheckStatus::Unknown;
    Repairability repairability = Repairability::NotEvaluated;
    QList<FindingId> relatedFindings;
    QDateTime observedAt;
};

struct ConfidenceExplanation final {
    int score = 0;
    QString bandReason;
    QStringList requiredEvidence;
    QStringList supportingEvidence;
    QStringList contradictingEvidence;
    QStringList missingExpectedEvidence;
};

struct Diagnosis final {
    DiagnosisId id;
    KnowledgeSignatureId signatureId;
    DiagnosisConfidence confidence = DiagnosisConfidence::Uncertain;
    FindingSeverity severity = FindingSeverity::Informational;
    Repairability repairability = Repairability::NotEvaluated;
    QList<EvidenceId> supportingEvidence;
    QList<EvidenceId> contradictingEvidence;
    QString title;
    QString problemFamily;
    QString humanExplanation;
    QString technicalExplanation;
    QString userImpact;
    QString usualResolution;
    QString provenance;
    QString knowledgeVersion;
    QStringList candidateRepairIds;
    DiagnosisRole role = DiagnosisRole::Secondary;
    ConfidenceExplanation confidenceExplanation;
};

struct DoctorActivityEvent final {
    QDateTime timestamp;
    DoctorCheckId checkId;
    DoctorCheckStatus status = DoctorCheckStatus::Waiting;
    QString title;
    QString detail;
    EvidenceId evidenceId;
};

struct UserAction final {
    UserActionState state = UserActionState::NothingRequired;
    FindingSeverity severity = FindingSeverity::Informational;
    QString title;
    QString explanation;
    QString why;
    QStringList instructions;
    QStringList availableActions;
};

struct UserActionLedgerEntry final {
    QString actionId;
    QString title;
    QString explanation;
    QDateTime firstRequiredAt;
    QDateTime completedAt;
    QDateTime cancelledAt;
    UserActionLedgerState state = UserActionLedgerState::NotRequired;
    QString reason;
    QString associatedCheckId;
    QString associatedDiagnosisId;
    QString userResponse;
};

struct DoctorOperation final {
    DoctorOperationId id;
    DoctorOperationState state = DoctorOperationState::Waiting;
    QString title;
    int progressPercent = 0;
    qint64 timeoutMs = 0;
};

QString displayName(DoctorPhase phase);
QString displayName(DoctorCheckStatus status);
QString displayName(FindingSeverity severity);
QString displayName(DiagnosisConfidence confidence);
QString displayName(Repairability repairability);
QString displayName(DiagnosisRole role);
bool isTerminal(DoctorCheckStatus status);
bool isExecutionFailure(DoctorCheckStatus status);
bool isTransitionAllowed(DoctorSessionState from, DoctorSessionState to);

} // namespace hotas::doctor
