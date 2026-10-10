#pragma once

#include "doctor_environment.h"

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
    StageApprovedPackage,
    RestoreSnapshot,
};

enum class RepairTargetKind { None, HidHideActiveState, HidHideInverseState, WhitelistEntry, BlacklistEntry, ApprovedPackage, TransactionSnapshot };
enum class RepairAuthorization { NotAuthorized, UserAuthorized, HelperRevalidated };

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
    RepairRiskClass riskClass = RepairRiskClass::R0Observe;
    RepairQualificationLevel qualification = RepairQualificationLevel::Experimental;
    RepairAuthorization authorization = RepairAuthorization::NotAuthorized;
    QList<DiagnosisId> diagnosesAddressed;
    QList<RepairOperation> operations;
    QList<RepairPrecondition> preconditions;
    QString integrityDigest;
};

struct RepairTransactionJournalEntry final {
    RepairTransactionId transactionId;
    RepairPlanId planId;
    DoctorOperationId operationId;
    QString priorValue;
    QString requestedValue;
    QString actualPostValue;
    DoctorOperationState state = DoctorOperationState::Waiting;
    std::optional<NativeError> nativeError;
};

class RepairHelperContract final {
public:
    static QString seal(const RepairPlan &plan);
    static bool validate(const RepairPlan &plan, const DoctorEnvironment &environment, QString *reason = nullptr);
};

} // namespace hotas::doctor
