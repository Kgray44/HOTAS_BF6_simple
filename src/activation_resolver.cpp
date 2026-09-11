#include "activation_resolver.h"

#include <algorithm>
#include <limits>

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
    QStringList warnings;
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
    const bool retainedDuringDisconnectGrace = rig->id == context.requiredDisconnectGraceRigId
        && profile->id == context.requiredDisconnectGraceProfileId
        && profile->id == context.activeProfileId && rig->id == context.activeDeviceRigId;
    // Hand-built legacy test/extension statuses may only populate the older
    // aggregate field. Treat that conservative representation as required.
    const bool ambiguousRequired = !status->ambiguousRequiredMemberIds.isEmpty()
        || (!status->ambiguousMemberIds.isEmpty()
            && status->ambiguousOptionalMemberIds.isEmpty());
    const bool unverifiedRequired = !status->needsVerificationRequiredMemberIds.isEmpty()
        || (!status->needsVerificationMemberIds.isEmpty()
            && status->needsVerificationOptionalMemberIds.isEmpty());
    if (ambiguousRequired) {
        result.blocker = rig->name + QStringLiteral(" has an ambiguous required device identity.");
        return result;
    }
    if (unverifiedRequired) {
        result.blocker = rig->name + QStringLiteral(" has a required device that needs verification.");
        return result;
    }
    if (!status->complete && !retainedDuringDisconnectGrace) {
        result.blocker = rig->name + QStringLiteral(" is missing a required device.");
        return result;
    }
    if (retainedDuringDisconnectGrace) {
        result.warnings.append(QStringLiteral("A required input is within the reconnect grace period; the current configuration is retained."));
    }
    for (const DeviceRigMember &member : rig->members) {
        if (!member.enabled || !member.required) continue;
        if (context.knownVisibleManagedRecordIds.contains(member.controllerRecordId)) {
            result.blocker = rig->name + QStringLiteral(" has a required managed physical input visible directly to games.");
            return result;
        }
        if (context.unknownManagedIsolationRecordIds.contains(member.controllerRecordId)) {
            result.warnings.append(rig->name + QStringLiteral(" has a managed physical input whose HidHide state is not currently known."));
        }
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
    decision.configurationGeneration = context.configurationGeneration;
    decision.inventoryGeneration = context.inventoryGeneration;
    decision.gameContextGeneration = context.gameContextGeneration;
    return decision;
}

int automaticPolicyRank(ProfileAutomaticSelectionMode mode)
{
    switch (mode) {
    case ProfileAutomaticSelectionMode::Preferred: return 0;
    case ProfileAutomaticSelectionMode::Fallback: return 1;
    case ProfileAutomaticSelectionMode::ManualOnly: return 2;
    }
    return 2;
}

void appendCandidateEvaluations(const MapperConfiguration &configuration,
                                const ActivationContext &context,
                                ActivationDecision *decision)
{
    if (!decision) return;
    const ProfileCategory *category = findProfileCategory(configuration, decision->categoryId);
    if (!category) return;
    int selectedOrder = std::numeric_limits<int>::max();
    int selectedRank = std::numeric_limits<int>::max();
    for (int index = 0; index < category->profileIds.size(); ++index) {
        const ControllerProfile *profile = findProfile(configuration, category->profileIds.at(index));
        if (profile && profile->id == decision->profileId) {
            selectedOrder = index;
            selectedRank = automaticPolicyRank(profile->automaticSelectionMode);
            break;
        }
    }
    for (int index = 0; index < category->profileIds.size(); ++index) {
        const QString &profileId = category->profileIds.at(index);
        const ControllerProfile *profile = findProfile(configuration, profileId);
        if (!profile) continue;
        const bool explicitManual = context.intent == ActivationIntent::ManualProfile
            && context.requestedProfileId == profileId;
        const CandidateResult candidate = candidateFor(configuration, context, *category, profileId, explicitManual);
        ActivationCandidateEvaluation evaluation;
        evaluation.profileId = profileId;
        evaluation.profileName = profile->name;
        evaluation.deviceRigId = profile->deviceRigId;
        if (const DeviceRig *rig = findDeviceRig(configuration, profile->deviceRigId)) {
            evaluation.deviceRigName = rig->name;
        }
        evaluation.outputLayoutId = profile->outputLayoutId;
        if (const VirtualOutputLayout *output = findOutputLayout(configuration, profile->outputLayoutId)) {
            evaluation.outputLayoutName = output->name;
        }
        evaluation.mode = profile->automaticSelectionMode;
        if (const DeviceRigStatus *status = statusFor(context.rigStatuses, profile->deviceRigId)) {
            evaluation.rigHealth = status->health;
        }
        evaluation.eligible = candidate.profile != nullptr;
        evaluation.current = profileId == context.activeProfileId
            && profile->deviceRigId == context.activeDeviceRigId;
        evaluation.selected = profileId == decision->profileId;
        evaluation.blockers = candidate.blocker.isEmpty() ? QStringList{} : QStringList{candidate.blocker};
        evaluation.warnings = candidate.warnings;
        const int rank = automaticPolicyRank(profile->automaticSelectionMode);
        evaluation.higherPreferenceAvailable = evaluation.eligible && !evaluation.selected
            && (rank < selectedRank || (rank == selectedRank && index < selectedOrder));
        if (evaluation.higherPreferenceAvailable) decision->higherPreferenceAvailable = true;
        decision->candidates.append(std::move(evaluation));
    }
}

} // namespace

QString activationDecisionReasonKey(ActivationDecisionReason reason)
{
    switch (reason) {
    case ActivationDecisionReason::NoMatchedApplication: return QStringLiteral("no-matched-application");
    case ActivationDecisionReason::AutomaticActivationDisabled: return QStringLiteral("automatic-disabled");
    case ActivationDecisionReason::ManualOverrideRetained: return QStringLiteral("manual-override-retained");
    case ActivationDecisionReason::ManualOverrideUnavailable: return QStringLiteral("manual-override-unavailable");
    case ActivationDecisionReason::ManualOverrideExpired: return QStringLiteral("manual-override-expired");
    case ActivationDecisionReason::CurrentPairRetained: return QStringLiteral("current-pair-retained");
    case ActivationDecisionReason::PreferredCandidateSelected: return QStringLiteral("preferred-selected");
    case ActivationDecisionReason::FallbackCandidateSelected: return QStringLiteral("fallback-selected");
    case ActivationDecisionReason::NoEligibleCandidate: return QStringLiteral("no-eligible-candidate");
    case ActivationDecisionReason::IsolationBlocked: return QStringLiteral("isolation-blocked");
    case ActivationDecisionReason::StaleDecisionDiscarded: return QStringLiteral("stale-decision-discarded");
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
    const auto finish = [&](ActivationDecision result) {
        result.configurationGeneration = context.configurationGeneration;
        result.inventoryGeneration = context.inventoryGeneration;
        result.gameContextGeneration = context.gameContextGeneration;
        appendCandidateEvaluations(configuration, context, &result);
        return result;
    };
    const bool isAutomatic = context.intent == ActivationIntent::Automatic;
    const bool manualCommand = context.intent == ActivationIntent::ManualProfile
        || context.intent == ActivationIntent::ManualCategory
        || context.intent == ActivationIntent::ManualRig;

    // A user command is still useful when automatic game detection is paused.
    // Only background, resolver-driven switching honors this kill switch.
    if (isAutomatic && !context.automaticActivationEnabled) {
        decision.reason = ActivationDecisionReason::AutomaticActivationDisabled;
        decision.explanation = QStringLiteral("Automatic Activation is paused.");
        return finish(decision);
    }

    const ControllerProfile *requestedProfile = context.requestedProfileId.isEmpty()
        ? nullptr : findProfile(configuration, context.requestedProfileId);
    QString categoryId = context.matchedCategoryId;
    if (context.intent == ActivationIntent::ManualProfile && requestedProfile) {
        categoryId = requestedProfile->categoryId;
    } else if (context.intent == ActivationIntent::ManualRig && categoryId.isEmpty()) {
        categoryId = context.activeCategoryId;
        if (categoryId.isEmpty()) {
            if (const ControllerProfile *active = findProfile(configuration, context.activeProfileId)) {
                categoryId = active->categoryId;
            }
        }
    }

    QString manualUnavailableBlocker;
    if (isAutomatic && context.manualOverrideActive && !context.manualOverrideProfileId.isEmpty()) {
        const bool sameContext = !context.manualOverrideCategoryId.isEmpty()
            && context.manualOverrideCategoryId == categoryId;
        if (!sameContext) {
            decision.reason = ActivationDecisionReason::ManualOverrideExpired;
            manualUnavailableBlocker = QStringLiteral("Manual Override ended because the active game/application context changed.");
            decision.blockers.append(manualUnavailableBlocker);
        } else {
            const ControllerProfile *manualProfile = findProfile(configuration, context.manualOverrideProfileId);
            const ProfileCategory *manualCategory = manualProfile
                ? findProfileCategory(configuration, manualProfile->categoryId) : nullptr;
            const CandidateResult manual = manualCategory
                ? candidateFor(configuration, context, *manualCategory, context.manualOverrideProfileId, true)
                : CandidateResult{nullptr, nullptr,
                    QStringLiteral("The selected manual configuration no longer exists.")};
            if (manual.profile) {
                decision = selectedDecision(manual, context, ActivationDecisionReason::ManualOverrideRetained,
                    QStringLiteral("Manual Override is retaining %1 for this application context.").arg(manual.profile->name));
                decision.manualOverride = true;
                return finish(decision);
            }
            decision.reason = ActivationDecisionReason::ManualOverrideUnavailable;
            manualUnavailableBlocker = manual.blocker.isEmpty()
                ? QStringLiteral("The selected manual configuration is unavailable.") : manual.blocker;
            decision.blockers.append(manualUnavailableBlocker);
        }
    }

    const ProfileCategory *category = findProfileCategory(configuration, categoryId);
    if (!category || !category->enabled) {
        decision.categoryId = categoryId;
        decision.reason = manualUnavailableBlocker.isEmpty()
            ? ActivationDecisionReason::NoMatchedApplication : decision.reason;
        decision.explanation = manualCommand
            ? QStringLiteral("The requested Game / Application category is not available.")
            : QStringLiteral("No enabled Game / Application category currently matches.");
        return finish(decision);
    }
    decision.categoryId = category->id;

    if (context.intent == ActivationIntent::ManualProfile) {
        const CandidateResult explicitProfile = requestedProfile
            ? candidateFor(configuration, context, *category, requestedProfile->id, true)
            : CandidateResult{nullptr, nullptr, QStringLiteral("The selected Profile no longer exists.")};
        if (explicitProfile.profile) {
            ActivationDecision selected = selectedDecision(explicitProfile, context,
                ActivationDecisionReason::ManualOverrideRetained,
                QStringLiteral("User selected %1.").arg(explicitProfile.profile->name));
            selected.manualOverride = true;
            return finish(selected);
        }
        decision.reason = ActivationDecisionReason::ManualOverrideUnavailable;
        decision.explanation = QStringLiteral("The requested Profile cannot be activated safely.");
        decision.blockers.append(explicitProfile.blocker);
        return finish(decision);
    }

    const auto findByMode = [&](ProfileAutomaticSelectionMode mode,
                                const QString &rigConstraint = {}) -> CandidateResult {
        CandidateResult firstFailure;
        for (const QString &profileId : category->profileIds) {
            const ControllerProfile *profile = findProfile(configuration, profileId);
            if (!profile || profile->automaticSelectionMode != mode) continue;
            if (!rigConstraint.isEmpty() && profile->deviceRigId != rigConstraint) continue;
            CandidateResult candidate = candidateFor(configuration, context, *category, profileId, false);
            if (candidate.profile) return candidate;
            if (firstFailure.blocker.isEmpty()) firstFailure = std::move(candidate);
        }
        return firstFailure;
    };

    if (context.intent == ActivationIntent::ManualRig) {
        if (context.requestedRigId.isEmpty() || !findDeviceRig(configuration, context.requestedRigId)) {
            decision.reason = ActivationDecisionReason::ManualOverrideUnavailable;
            decision.explanation = QStringLiteral("The requested Device Rig no longer exists.");
            return finish(decision);
        }
        CandidateResult preferredForRig = findByMode(ProfileAutomaticSelectionMode::Preferred,
                                                     context.requestedRigId);
        CandidateResult fallbackForRig = findByMode(ProfileAutomaticSelectionMode::Fallback,
                                                    context.requestedRigId);
        CandidateResult selected = preferredForRig.profile ? preferredForRig : fallbackForRig;
        if (selected.profile) {
            ActivationDecision applied = selectedDecision(selected, context,
                selected.profile->automaticSelectionMode == ProfileAutomaticSelectionMode::Preferred
                    ? ActivationDecisionReason::PreferredCandidateSelected
                    : ActivationDecisionReason::FallbackCandidateSelected,
                QStringLiteral("User selected compatible Device Rig %1 with Profile %2.")
                    .arg(selected.rig->name, selected.profile->name));
            applied.manualOverride = true;
            return finish(applied);
        }
        decision.reason = ActivationDecisionReason::ManualOverrideUnavailable;
        decision.explanation = QStringLiteral("No compatible Profile can safely activate the requested Device Rig.");
        if (!preferredForRig.blocker.isEmpty()) decision.blockers.append(preferredForRig.blocker);
        if (!fallbackForRig.blocker.isEmpty() && fallbackForRig.blocker != preferredForRig.blocker) {
            decision.blockers.append(fallbackForRig.blocker);
        }
        return finish(decision);
    }

    // A current pair is deliberately sticky only during automatic resolution
    // of the same category.  Category changes and Switch Now always re-rank.
    if (isAutomatic && context.activeCategoryId == category->id && !context.activeProfileId.isEmpty()) {
        const CandidateResult current = candidateFor(configuration, context, *category,
                                                     context.activeProfileId, false);
        if (current.profile) {
            ActivationDecision retained = selectedDecision(current, context,
                ActivationDecisionReason::CurrentPairRetained,
                QStringLiteral("Current configuration retained because it remains valid."));
            retained.retainedCurrent = true;
            return finish(retained);
        }
    }

    CandidateResult preferred = findByMode(ProfileAutomaticSelectionMode::Preferred);
    if (preferred.profile) {
        ActivationDecision selected = selectedDecision(preferred, context,
            ActivationDecisionReason::PreferredCandidateSelected,
            QStringLiteral("Selected preferred configuration: %1.").arg(preferred.profile->name));
        if (!manualUnavailableBlocker.isEmpty()) selected.blockers.append(manualUnavailableBlocker);
        return finish(selected);
    }
    CandidateResult fallback = findByMode(ProfileAutomaticSelectionMode::Fallback);
    if (fallback.profile) {
        ActivationDecision selected = selectedDecision(fallback, context,
            ActivationDecisionReason::FallbackCandidateSelected,
            QStringLiteral("Selected fallback configuration: %1.").arg(fallback.profile->name));
        if (!manualUnavailableBlocker.isEmpty()) selected.blockers.append(manualUnavailableBlocker);
        if (!preferred.blocker.isEmpty()) selected.blockers.append(preferred.blocker);
        return finish(selected);
    }

    decision.reason = (preferred.blocker.contains(QStringLiteral("visible directly"))
                       || fallback.blocker.contains(QStringLiteral("visible directly")))
        ? ActivationDecisionReason::IsolationBlocked : ActivationDecisionReason::NoEligibleCandidate;
    decision.explanation = QStringLiteral("No valid automatic configuration is available for %1.").arg(category->name);
    if (!preferred.blocker.isEmpty()) decision.blockers.append(preferred.blocker);
    if (!fallback.blocker.isEmpty() && fallback.blocker != preferred.blocker) decision.blockers.append(fallback.blocker);
    if (!manualUnavailableBlocker.isEmpty()) decision.blockers.append(manualUnavailableBlocker);
    if (decision.blockers.isEmpty()) decision.blockers.append(QStringLiteral("No Preferred or Fallback profile is eligible."));
    return finish(decision);
}

} // namespace hotas
