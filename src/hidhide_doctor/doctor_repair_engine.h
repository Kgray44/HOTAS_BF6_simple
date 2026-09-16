#pragma once

#include "doctor_diagnostics.h"
#include "doctor_repair_contract.h"

#include <QLockFile>

namespace hotas::doctor {

enum class ConfigurationOwnership { HotasOwned, DoctorOwned, UserOrUnrelated, Unknown };
enum class RepairProposalStatus { AvailableForOwnerLab, IdentifiedButNotFieldQualified, NotApplicable, Blocked };

struct HidHideConfigurationSnapshot final {
    QStringList whitelist;
    QStringList blacklist;
    bool whitelistKnown = false;
    bool blacklistKnown = false;
    std::optional<bool> active;
    std::optional<bool> inverse;
    QString provider;
    QString providerVersion;
    QDateTime observedAt;

    bool isComplete() const;
    QString stableJson() const;
    QString fingerprint() const;
};

struct ConfigurationDelta final {
    QStringList additions;
    QStringList removals;
    QStringList unchanged;
    QStringList resulting;
};

struct RepairRecipe final {
    RepairRecipeId id;
    QString version;
    QString title;
    QString description;
    RepairRiskClass riskClass = RepairRiskClass::R1Configuration;
    RepairQualificationLevel qualification = RepairQualificationLevel::LabQualified;
    QStringList supportedDiagnosisIds;
    DiagnosisConfidence requiredConfidence = DiagnosisConfidence::High;
    QStringList requiredCapabilities;
    QString requiredIntent;
    QString backupScope;
    QString verificationSummary;
    QString rollbackSummary;
    bool elevationRequired = true;
    bool restartRequired = false;
    int maxReboots = 0;
    int estimatedSeconds = 5;
};

struct RepairPlanProposal final {
    RepairProposalStatus status = RepairProposalStatus::NotApplicable;
    QString reason;
    RepairRecipe recipe;
    RepairPlan plan;
    HidHideConfigurationSnapshot before;
    HidHideConfigurationSnapshot after;
    ConfigurationDelta whitelistDelta;
    ConfigurationDelta blacklistDelta;
    QStringList collateralPreserved;
};

// Deliberately data-only registry.  Recipes are first-class, versioned
// records; planning behavior is switched by their stable ID rather than
// hidden in UI callbacks or anonymous repair lambdas.
class RepairRecipeRegistry final {
public:
    static QList<RepairRecipe> recipes();
    static std::optional<RepairRecipe> recipe(const RepairRecipeId &id);
};

class ConfigurationDeltaEngine final {
public:
    static QString semanticKey(const QString &value);
    static ConfigurationDelta addExact(const QStringList &current, const QString &entry);
    static ConfigurationDelta removeExact(const QStringList &current, const QString &entry);
};

class RepairPlanner final {
public:
    static std::optional<HidHideConfigurationSnapshot> configurationFrom(const ReadOnlyDiagnosticSnapshot &snapshot);
    RepairPlanProposal propose(const DoctorSession &session, const ReadOnlyDiagnosticSnapshot &snapshot,
        bool ownerLabMode, bool explicitApprovedUpgradeRequest = false) const;
};

class IRepairConfigurationMutator {
public:
    virtual ~IRepairConfigurationMutator() = default;
    virtual HidHideConfigurationSnapshot readConfiguration() = 0;
    virtual bool apply(const RepairOperation &operation, NativeError *error = nullptr) = 0;
};

// Tests and development demonstrations use this in-memory provider.  It is
// intentionally the only executable mutator in Phase 3 core; real HidHide
// SET operations remain behind the separate elevated helper target.
class MemoryRepairConfigurationMutator final : public IRepairConfigurationMutator {
public:
    explicit MemoryRepairConfigurationMutator(HidHideConfigurationSnapshot snapshot);
    HidHideConfigurationSnapshot readConfiguration() override;
    bool apply(const RepairOperation &operation, NativeError *error = nullptr) override;
    void failNextOperation(NativeError error);
    void replaceExternally(HidHideConfigurationSnapshot snapshot);
private:
    HidHideConfigurationSnapshot m_snapshot;
    std::optional<NativeError> m_nextError;
};

// The core never assumes that a successful SET operation repaired the
// diagnosis.  A caller may provide an independent read-only verifier (for
// example, the affected Doctor checks) after exact configuration read-back.
class IRepairPostconditionVerifier {
public:
    virtual ~IRepairPostconditionVerifier() = default;
    virtual bool verify(const RepairPlan &plan, const HidHideConfigurationSnapshot &after, QString *reason) = 0;
};

class RepairJournalStore final {
public:
    explicit RepairJournalStore(QString root = {});
    QString root() const;
    QString journalPath(const RepairTransactionId &id) const;
    bool persist(RepairTransaction transaction, QString *reason = nullptr) const;
    std::optional<RepairTransaction> load(const RepairTransactionId &id, QString *reason = nullptr) const;
    QList<RepairTransaction> history(QString *reason = nullptr) const;
private:
    QString m_root;
};

struct RepairExecutionResult final {
    RepairTransaction transaction;
    QString detail;
    bool mutated = false;
    bool rolledBack = false;
};

struct RepairRecoveryResult final {
    RepairTransaction transaction;
    QString detail;
    bool requiresOwnerReview = false;
};

class RepairTransactionCoordinator final {
public:
    // Dry runs produce the same plan/backup/journal representation but do
    // not receive a mutator and cannot change machine configuration.
    RepairExecutionResult dryRun(const RepairPlanProposal &proposal, const DoctorEnvironment &environment,
        const DoctorSessionId &sessionId, const RepairJournalStore &journal,
        const RepairTransactionId &transactionId = {}) const;
    RepairExecutionResult executeOwnerLab(const RepairPlanProposal &proposal, const DoctorEnvironment &environment,
        IRepairConfigurationMutator &mutator, const RepairJournalStore &journal,
        const RepairTransactionId &transactionId = {}, IRepairPostconditionVerifier *postconditionVerifier = nullptr) const;

    // Startup reconciliation is deliberately read-only.  It classifies an
    // incomplete durable journal and never replays, completes, or rolls back
    // a transaction automatically after a restart.
    RepairRecoveryResult reconcileIncomplete(const RepairTransaction &transaction,
        IRepairConfigurationMutator &mutator, const RepairJournalStore &journal) const;

    // Phase 4 continuation is read-only by construction. It consumes a new
    // independently collected Doctor snapshot and records whether the reboot
    // actually produced the expected package/driver/API/configuration state;
    // it never replays installation or rollback work on startup.
    RepairRecoveryResult reconcileAfterReboot(const RepairTransaction &transaction,
        const ReadOnlyDiagnosticSnapshot &snapshot, const RepairJournalStore &journal) const;
};

QString displayName(ConfigurationOwnership ownership);
QString displayName(RepairProposalStatus status);
QString displayName(RepairTransactionState state);

} // namespace hotas::doctor
