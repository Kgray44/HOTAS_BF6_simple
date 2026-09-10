#include "activation_resolver.h"

#include <algorithm>

namespace hotas {
namespace {

const DeviceRigStatus *statusFor(const QList<DeviceRigStatus> &statuses, const QString &rigId)
{
    const auto found = std::find_if(statuses.cbegin(), statuses.cend(), [&rigId](const DeviceRigStatus &status) {
        return status.rigId == rigId;
    });
    return found == statuses.cend() ? nullptr : &*found;
}

struct CandidateResult {
    const ControllerProfile *profile = nullptr;
    const DeviceRig *rig = nullptr;
    QString blocker;
};

CandidateResult candidateFor(const MapperConfiguration &configuration,
                             const ActivationContext &context,
                             const ProfileCategory &category,
                             const QString &profileId,
                             bool allowManualOnly)
{
    CandidateResult result;
    const ControllerProfile *profile = findProfile(configuration, profileId);
    if (!profile || profile->categoryId != category.id) {
        result.blocker = QStringLiteral("The category contains an invalid profile reference.");
        return result;
    }
    if (!profile->enabled) {
        result.blocker = profile->name + QStringLiteral(" is not enabled.");
        return result;
    }
    if (!allowManualOnly && profile->automaticSelectionMode == ProfileAutomaticSelectionMode::ManualOnly) {
        result.blocker = profile->name + QStringLiteral(" is Manual Only.");
        return result;
    }
    const DeviceRig *rig = findDeviceRig(configuration, profile->deviceRigId);
    if (!rig) {
        result.blocker = profile->name + QStringLiteral(" has no valid Device Rig.");
        return result;
    }
    if (!rig->enabled) {
        result.blocker = rig->name + QStringLiteral(" is not enabled.");
        return result;
    }
    const DeviceRigStatus *status = statusFor(context.rigStatuses, rig->id);
    if (!status) {
        result.blocker = rig->name + QStringLiteral(" readiness is not known yet.");
        return result;
    }
    if (!status->ambiguousMemberIds.isEmpty()) {
        result.blocker = rig->name + QStringLiteral(" has an ambiguous required device identity.");
        return result;
    }
    if (!status->needsVerificationMemberIds.isEmpty()) {
        result.blocker = rig->name + QStringLiteral(" has a required device that needs verification.");
        return result;
    }
    if (!status->complete) {
        result.blocker = rig->name + QStringLiteral(" is missing a required device.");
        return result;
    }
    if (!findOutputLayout(configuration, profile->outputLayoutId)) {
        result.blocker = profile->name + QStringLiteral(" references an unavailable virtual output.");
        return result;
    }
    if (context.unavailableOutputLayoutIds.contains(profile->outputLayoutId)) {
        result.blocker = profile->name + QStringLiteral(" requires a virtual output that is unavailable.");
        return result;
    }
    const bool rigUsesOutput = std::any_of(rig->outputs.cbegin(), rig->outputs.cend(),
        [&profile](const DeviceRigOutputTarget &output) {
            return output.enabled && output.outputLayoutId == profile->outputLayoutId;
        });
    if (!rigUsesOutput) {
        result.blocker = rig->name + QStringLiteral(" does not enable the Profile's virtual output.");
        return result;
    }
    const CompiledDeviceRigRuntime compiled = compileDeviceRigRuntime(configuration, rig->id, profile->id);
    if (!compiled.valid) {
        result.blocker = profile->name + QStringLiteral(" cannot compile safely: ") + compiled.issue;
        return result;
    }
    result.profile = profile;
    result.rig = rig;
    return result;
}

ActivationDecision selectedDecision(const CandidateResult &candidate,
                                   const ActivationContext &context,
                                   ActivationDecisionReason reason,
                                   const QString &explanation)
{
    ActivationDecision decision;
    decision.categoryId = candidate.profile->categoryId;
    decision.profileId = candidate.profile->id;
    decision.deviceRigId = candidate.rig->id;
    decision.outputLayoutId = candidate.profile->outputLayoutId;
    decision.reason = reason;
    decision.explanation = explanation;
    decision.valid = true;
    decision.changed = decision.profileId != context.activeProfileId
        || decision.deviceRigId != context.activeDeviceRigId
        || decision.outputLayoutId != context.activeOutputLayoutId;
    return decision;
}

} // namespace

QString activationDecisionReasonKey(ActivationDecisionReason reason)
{
    switch (reason) {
    case ActivationDecisionReason::NoMatchedApplication: return QStringLiteral("no-matched-application");
    case ActivationDecisionReason::AutomaticActivationDisabled: return QStringLiteral("automatic-disabled");
    case ActivationDecisionReason::ManualOverrideRetained: return QStringLiteral("manual-override-retained");
    case ActivationDecisionReason::ManualOverrideUnavailable: return QStringLiteral("manual-override-unavailable");
    case ActivationDecisionReason::CurrentPairRetained: return QStringLiteral("current-pair-retained");
    case ActivationDecisionReason::PreferredCandidateSelected: return QStringLiteral("preferred-selected");
    case ActivationDecisionReason::FallbackCandidateSelected: return QStringLiteral("fallback-selected");
    case ActivationDecisionReason::NoEligibleCandidate: return QStringLiteral("no-eligible-candidate");
    }
    return QStringLiteral("unknown");
}

QString profileAutomaticSelectionModeKey(ProfileAutomaticSelectionMode mode)
{
    switch (mode) {
    case ProfileAutomaticSelectionMode::Preferred: return QStringLiteral("preferred");
    case ProfileAutomaticSelectionMode::Fallback: return QStringLiteral("fallback");
    case ProfileAutomaticSelectionMode::ManualOnly: return QStringLiteral("manual-only");
    }
    return QStringLiteral("preferred");
}

QString profileAutomaticSelectionModeLabel(ProfileAutomaticSelectionMode mode)
{
    switch (mode) {
    case ProfileAutomaticSelectionMode::Preferred: return QStringLiteral("Preferred");
    case ProfileAutomaticSelectionMode::Fallback: return QStringLiteral("Fallback");
    case ProfileAutomaticSelectionMode::ManualOnly: return QStringLiteral("Manual Only");
    }
    return QStringLiteral("Preferred");
}

bool profileAutomaticSelectionModeFromKey(const QString &key, ProfileAutomaticSelectionMode *mode)
{
    if (!mode) return false;
    const QString normalized = key.trimmed().toCaseFolded();
    if (normalized == QStringLiteral("preferred")) *mode = ProfileAutomaticSelectionMode::Preferred;
    else if (normalized == QStringLiteral("fallback")) *mode = ProfileAutomaticSelectionMode::Fallback;
    else if (normalized == QStringLiteral("manual-only") || normalized == QStringLiteral("manual only")) {
        *mode = ProfileAutomaticSelectionMode::ManualOnly;
    } else return false;
    return true;
}

ActivationDecision resolveActivation(const MapperConfiguration &configuration,
                                     const ActivationContext &context)
{
    ActivationDecision decision;
    if (!context.automaticActivationEnabled) {
        decision.reason = ActivationDecisionReason::AutomaticActivationDisabled;
        decision.explanation = QStringLiteral("Automatic Activation is paused.");
        return decision;
    }
    QString manualUnavailableBlocker;
    if (context.manualOverrideActive && !context.manualOverrideProfileId.isEmpty()) {
        // A direct user choice deliberately survives a game-category change.
        // Validate it in its own category rather than silently discarding it
        // merely because another game's executable is now detected.
        const ControllerProfile *manualProfile = findProfile(configuration,
            context.manualOverrideProfileId);
        const ProfileCategory *manualCategory = manualProfile
            ? findProfileCategory(configuration, manualProfile->categoryId) : nullptr;
        const CandidateResult manual = manualCategory
            ? candidateFor(configuration, context, *manualCategory, context.manualOverrideProfileId, true)
            : CandidateResult{nullptr, nullptr,
                QStringLiteral("The selected manual configuration no longer exists.")};
        if (manual.profile) {
            decision = selectedDecision(manual, context, ActivationDecisionReason::ManualOverrideRetained,
                QStringLiteral("Manual Override is retaining %1.").arg(manual.profile->name));
            decision.manualOverride = true;
            return decision;
        }
        decision.reason = ActivationDecisionReason::ManualOverrideUnavailable;
        manualUnavailableBlocker = manual.blocker.isEmpty()
            ? QStringLiteral("The selected manual configuration is unavailable.") : manual.blocker;
        decision.blockers.append(manualUnavailableBlocker);
    }

    const ProfileCategory *category = findProfileCategory(configuration, context.matchedCategoryId);
    if (!category || !category->enabled) {
        decision.reason = manualUnavailableBlocker.isEmpty()
            ? ActivationDecisionReason::NoMatchedApplication
            : ActivationDecisionReason::ManualOverrideUnavailable;
        decision.explanation = manualUnavailableBlocker.isEmpty()
            ? QStringLiteral("No enabled Game / Application category currently matches.")
            : QStringLiteral("Manual Override is unavailable; no enabled Game / Application category currently matches.");
        return decision;
    }
    decision.categoryId = category->id;

    // Stable-current-pair policy: once the matched category has a safe active
    // target, a newly available higher-preference rig is shown as available,
    // not switched in the middle of a session.
    if (context.activeCategoryId == category->id && !context.activeProfileId.isEmpty()) {
        const CandidateResult current = candidateFor(configuration, context, *category,
                                                     context.activeProfileId, false);
        if (current.profile) {
            decision = selectedDecision(current, context, ActivationDecisionReason::CurrentPairRetained,
                QStringLiteral("Current configuration retained because it remains valid."));
            decision.retainedCurrent = true;
            return decision;
        }
    }

    const auto findByMode = [&](ProfileAutomaticSelectionMode mode) -> CandidateResult {
        CandidateResult firstFailure;
        for (const QString &profileId : category->profileIds) {
            const ControllerProfile *profile = findProfile(configuration, profileId);
            if (!profile || profile->automaticSelectionMode != mode) continue;
            CandidateResult candidate = candidateFor(configuration, context, *category, profileId, false);
            if (candidate.profile) return candidate;
            if (firstFailure.blocker.isEmpty()) firstFailure = std::move(candidate);
        }
        return firstFailure;
    };

    CandidateResult preferred = findByMode(ProfileAutomaticSelectionMode::Preferred);
    if (preferred.profile) {
        ActivationDecision selected = selectedDecision(preferred, context,
            ActivationDecisionReason::PreferredCandidateSelected,
            QStringLiteral("Selected preferred configuration: %1.").arg(preferred.profile->name));
        if (!manualUnavailableBlocker.isEmpty()) selected.blockers.append(manualUnavailableBlocker);
        return selected;
    }
    CandidateResult fallback = findByMode(ProfileAutomaticSelectionMode::Fallback);
    if (fallback.profile) {
        ActivationDecision selected = selectedDecision(fallback, context,
            ActivationDecisionReason::FallbackCandidateSelected,
            QStringLiteral("Selected fallback configuration: %1.").arg(fallback.profile->name));
        if (!manualUnavailableBlocker.isEmpty()) selected.blockers.append(manualUnavailableBlocker);
        if (!preferred.blocker.isEmpty()) selected.blockers.append(preferred.blocker);
        return selected;
    }

    decision.reason = ActivationDecisionReason::NoEligibleCandidate;
    decision.explanation = QStringLiteral("No valid automatic configuration is available for %1.").arg(category->name);
    if (!preferred.blocker.isEmpty()) decision.blockers.append(preferred.blocker);
    if (!fallback.blocker.isEmpty() && fallback.blocker != preferred.blocker) decision.blockers.append(fallback.blocker);
    if (!manualUnavailableBlocker.isEmpty()) decision.blockers.append(manualUnavailableBlocker);
    if (decision.blockers.isEmpty()) decision.blockers.append(QStringLiteral("No Preferred or Fallback profile is eligible."));
    return decision;
}

} // namespace hotas
