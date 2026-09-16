#pragma once

#include "doctor_environment.h"

#include <QJsonObject>

namespace hotas::doctor {

class RepairPlanId final {
public:
    RepairPlanId() = default;
    explicit RepairPlanId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class RepairTransactionId final {
public:
    RepairTransactionId() = default;
    explicit RepairTransactionId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

class RepairRecipeId final {
public:
    RepairRecipeId() = default;
    explicit RepairRecipeId(QString value);
    bool isValid() const;
    const QString &value() const;
private:
    QString m_value;
};

enum class RepairOperationKind {
    Unknown,
    SetHidHideActive,
    SetHidHideInverse,
    AddWhitelistEntry,
    RemoveWhitelistEntry,
    AddBlacklistEntry,
    RemoveBlacklistEntry,
    RepairExactServiceConfiguration,
    RepairExactFilterRegistration,
    ValidateApprovedPackage,
    StageApprovedPackage,
    InstallApprovedHidHidePackage,
    RemoveSpecificInactiveHidHidePackage,
    ReconcileHidHideConfiguration,
    RequestSystemRestart,
    RestoreSnapshot,
};

enum class RepairTargetKind {
    None,
    HidHideActiveState,
    HidHideInverseState,
    WhitelistEntry,
    BlacklistEntry,
    HidHideService,
    HidHideFilterRegistration,
    ApprovedPackage,
    InactiveHidHidePackage,
    RebootBoundary,
    TransactionSnapshot,
};
enum class RepairAuthorization { NotAuthorized, UserAuthorized, OwnerLabAuthorized, HelperRevalidated };
enum class RepairTransactionState {
    Planned,
    AwaitingAuthorization,
    Authorized,
    CapturingBackup,
    Revalidating,
    AwaitingElevation,
    PreparingPackage,
    PackageValidated,
    StagingPackage,
    Installing,
    AwaitingReboot,
    ContinuingAfterReboot,
    ReconcilingConfiguration,
    Executing,
    Verifying,
    RollingBack,
    Completed,
    FailedSafely,
    RecoveryRequired,
    RecoveryPlanning,
    Recovering,
    Cancelled,
    StalePlan,
};

struct RepairPrecondition final {
    QString stableKey;
    QString expectedValue;
};

struct RepairVerification final {
    DoctorCheckId checkId;
    QString expectedObservation;
};

struct RepairRollbackDefinition final {
    QString rollbackId;
    QString expectedPriorValue;
};

struct RepairOperation final {
    DoctorOperationId id;
    RepairOperationKind kind = RepairOperationKind::Unknown;
    RepairTargetKind targetKind = RepairTargetKind::None;
    QString targetIdentity;
    QString requestedValue;
    QList<RepairPrecondition> preconditions;
    QList<RepairVerification> verification;
    std::optional<RepairRollbackDefinition> rollback;
    bool isWellFormed(QString *reason = nullptr) const;
};

struct RepairPlan final {
    RepairPlanId id;
    DoctorSessionId sessionId;
    RepairRecipeId recipeId;
    QString recipeVersion;
    QString title;
    QString description;
    RepairRiskClass riskClass = RepairRiskClass::R0Observe;
    RepairQualificationLevel qualification = RepairQualificationLevel::Experimental;
    RepairAuthorization authorization = RepairAuthorization::NotAuthorized;
    QList<DiagnosisId> diagnosesAddressed;
    QList<RepairOperation> operations;
    QList<RepairPrecondition> preconditions;
    QString preconditionFingerprint;
    QString expectedPreState;
    QString expectedPostState;
    QString expectedPostFingerprint;
    QStringList unchangedCollateral;
    bool elevationRequired = false;
    bool restartRequired = false;
    int maximumReboots = 0;
    int estimatedSeconds = 0;
    QDateTime createdAt;
    // Phase 4 keeps package identity, provenance, verification, reboot, and
    // recovery metadata in a sealed structured payload. It is data, never an
    // executable command line or an untrusted installer argument string.
    QJsonObject deepRepair;
    QString integrityDigest;
};

struct RepairOperationJournalEntry final {
    RepairTransactionId transactionId;
    RepairPlanId planId;
    DoctorOperationId operationId;
    RepairOperationKind kind = RepairOperationKind::Unknown;
    RepairTargetKind targetKind = RepairTargetKind::None;
    QString targetIdentity;
    QString priorValue;
    QString requestedValue;
    QString actualPostValue;
    DoctorOperationState state = DoctorOperationState::Waiting;
    std::optional<NativeError> nativeError;
    QDateTime startedAt;
    QDateTime completedAt;
    QString rollbackStatus;
};

struct BackupManifest final {
    int schemaVersion = 3;
    QDateTime capturedAt;
    QString scope;
    QString targetScope;
    QString serializedState;
    QString sha256;
    QString provider;
    QString repairIntent;
    QString capabilityEvidence;
    quint32 windowsBuild = 0;
    QString architecture;
    QString privacyClassification;
    bool restoreEligible = false;
    QJsonObject deepRecoverySnapshot;
};

// This is the durable transaction representation.  It is intentionally
// separate from a diagnosis session and serializes only through the repair
// engine's atomic journal store; a partial JSON file is never accepted as a
// valid repair record.
struct RepairTransaction final {
    int schemaVersion = 3;
    RepairTransactionId id;
    DoctorSessionId sessionId;
    RepairPlanId planId;
    RepairRecipeId recipeId;
    QString recipeVersion;
    RepairRiskClass riskClass = RepairRiskClass::R0Observe;
    RepairQualificationLevel qualification = RepairQualificationLevel::Experimental;
    QString provider;
    quint32 windowsBuild = 0;
    QString architecture;
    QDateTime startedAt;
    RepairTransactionState state = RepairTransactionState::Planned;
    QList<RepairPrecondition> preconditions;
    QString preconditionFingerprint;
    QString expectedPostFingerprint;
    BackupManifest backupManifest;
    QList<RepairOperationJournalEntry> operations;
    int currentOperation = -1;
    QString verificationPlan;
    QString rollbackPlan;
    QString rebootBoundary;
    int rebootCount = 0;
    int maximumReboots = 0;
    QJsonObject continuationState;
    QJsonObject deepRepair;
    bool separateRecoveryAuthorizationRequired = false;
    QString finalStatus;
    QString checksum;
};

class RepairHelperContract final {
public:
    static QString seal(const RepairPlan &plan);
    static bool validate(const RepairPlan &plan, const DoctorEnvironment &environment, QString *reason);
    static bool validate(const RepairPlan &plan, const DoctorEnvironment &environment,
        bool ownerLabMode = false, QString *reason = nullptr);
};

} // namespace hotas::doctor
