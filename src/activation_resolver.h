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
    CurrentPairRetained,
    PreferredCandidateSelected,
    FallbackCandidateSelected,
    NoEligibleCandidate,
};

struct ActivationContext {
    QString matchedCategoryId;
    QList<DeviceRigStatus> rigStatuses;
    QString activeCategoryId;
    QString activeProfileId;
    QString activeDeviceRigId;
    QString activeOutputLayoutId;
    bool automaticActivationEnabled = true;
    bool manualOverrideActive = false;
    QString manualOverrideProfileId;
    // The availability list is an injected control-plane snapshot. Process,
    // driver, and UI queries are intentionally kept out of this pure policy.
    QStringList unavailableOutputLayoutIds;
};

struct ActivationDecision {
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
};

QString activationDecisionReasonKey(ActivationDecisionReason reason);
QString profileAutomaticSelectionModeKey(ProfileAutomaticSelectionMode mode);
QString profileAutomaticSelectionModeLabel(ProfileAutomaticSelectionMode mode);
bool profileAutomaticSelectionModeFromKey(const QString &key,
                                          ProfileAutomaticSelectionMode *mode);

// Pure, low-frequency control-plane policy. It does not enumerate processes,
// mutate HidHide, touch vJoy, persist data, or compile/report DirectInput.
ActivationDecision resolveActivation(const MapperConfiguration &configuration,
                                     const ActivationContext &context);

} // namespace hotas
