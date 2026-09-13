#pragma once

#include "device_rig.h"

namespace hotas {

// Reasons are published with every decision so an automatic switch, a stable
// hold, and a refusal all have the same durable explanation contract.
enum class ActivationDecisionReason : int {
    NoMatchedApplication,
    AutomaticActivationDisabled,
    ManualOverrideRetained,
    ManualOverrideUnavailable,
    ManualOverrideExpired,
    CurrentPairRetained,
    PreferredCandidateSelected,
    FallbackCandidateSelected,
    ManualRigCandidateSelected,
    NoEligibleCandidate,
    IsolationBlocked,
    StaleDecisionDiscarded,
};

// The policy needs to distinguish a resolver-driven automatic response from
// a deliberate user command.  In particular, disabling game detection must
// never disable a user selecting a Category or a compatible Device Rig.
enum class ActivationIntent : int {
    Automatic,
    ManualProfile,
    ManualCategory,
    ManualRig,
    RecommendedCandidate,
};

struct ActivationCandidateEvaluation {
    QString profileId;
    QString profileName;
    QString deviceRigId;
    QString deviceRigName;
    QString outputLayoutId;
    QString outputLayoutName;
    QString resolvedOutputLayoutId;
    QString resolvedOutputLayoutName;
    ProfileAutomaticSelectionMode mode = ProfileAutomaticSelectionMode::Preferred;
    DeviceRigHealth rigHealth = DeviceRigHealth::Offline;
    bool eligible = false;
    bool current = false;
    bool selected = false;
    bool higherPreferenceAvailable = false;
    QStringList blockers;
    QStringList warnings;
};

struct ActivationContext {
    QString matchedCategoryId;
    QList<DeviceRigStatus> rigStatuses;
    QString activeCategoryId;
    QString activeProfileId;
    QString activeDeviceRigId;
    QString activeOutputLayoutId;
    bool automaticActivationEnabled = true;
    ActivationIntent intent = ActivationIntent::Automatic;
    QString requestedProfileId;
    QString requestedRigId;
    bool manualOverrideActive = false;
    QString manualOverrideProfileId;
    QString manualOverrideCategoryId;
    // A freshly disconnected required member of the current pair gets a
    // bounded grace window.  It never grants a new candidate eligibility.
    QString requiredDisconnectGraceRigId;
    QString requiredDisconnectGraceProfileId;
    // These are read-only HidHide snapshots supplied by AppBackend.  The
    // pure resolver uses them only as preflight facts and never mutates them.
    QStringList knownVisibleManagedRecordIds;
    QStringList unknownManagedIsolationRecordIds;
    quint64 configurationGeneration = 0;
    quint64 inventoryGeneration = 0;
    quint64 gameContextGeneration = 0;
    // The availability list is an injected control-plane snapshot. Process,
    // driver, and UI queries are intentionally kept out of this pure policy.
    QStringList unavailableOutputLayoutIds;
};

struct ActivationDecision {
    QString requestedProfileId;
    QString requestedRigId;
    ActivationIntent intent = ActivationIntent::Automatic;
    QString categoryId;
    QString profileId;
    QString deviceRigId;
    QString outputLayoutId;
    ActivationDecisionReason reason = ActivationDecisionReason::NoMatchedApplication;
    QString explanation;
    QStringList blockers;
    bool valid = false;
    bool changed = false;
    bool retainedCurrent = false;
    bool manualOverride = false;
    bool higherPreferenceAvailable = false;
    quint64 configurationGeneration = 0;
    quint64 inventoryGeneration = 0;
    quint64 gameContextGeneration = 0;
    QList<ActivationCandidateEvaluation> candidates;
};

QString activationDecisionReasonKey(ActivationDecisionReason reason);
QString activationIntentKey(ActivationIntent intent);
QString profileAutomaticSelectionModeKey(ProfileAutomaticSelectionMode mode);
QString profileAutomaticSelectionModeLabel(ProfileAutomaticSelectionMode mode);
bool profileAutomaticSelectionModeFromKey(const QString &key,
                                          ProfileAutomaticSelectionMode *mode);

// Pure, low-frequency control-plane policy. It does not enumerate processes,
// mutate HidHide, touch vJoy, persist data, or compile/report DirectInput.
ActivationDecision resolveActivation(const MapperConfiguration &configuration,
                                     const ActivationContext &context);

} // namespace hotas
