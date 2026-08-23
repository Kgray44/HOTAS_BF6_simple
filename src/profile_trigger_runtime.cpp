#include "profile_trigger_runtime.h"

#include <algorithm>

namespace hotas {

const RuntimeProfileTrigger &ProfileTriggerRuntime::triggerFor(const RuntimeProfileCache &cache,
                                                                int source)
{
    if (source < kMaximumPhysicalButtons) {
        return cache.profileTriggers[static_cast<size_t>(source)];
    }
    const int povSource = source - kMaximumPhysicalButtons;
    return cache.povProfileTriggers[static_cast<size_t>(povSource / kPovDirectionCount)]
                                   [static_cast<size_t>(povSource % kPovDirectionCount)];
}

ProfileTriggerRuntime::InputStates ProfileTriggerRuntime::inputStates(
    const PhysicalButtonStates &buttons, const PhysicalPovValues &povs, int povCount)
{
    InputStates states{};
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        states[static_cast<size_t>(source)] = buttons[static_cast<size_t>(source)];
    }
    const int hats = std::clamp(povCount, 0, kMaximumPhysicalPovs);
    for (int hat = 0; hat < hats; ++hat) {
        const int direction = povDirectionIndex(povDirectionFromRaw(povs[static_cast<size_t>(hat)]));
        if (direction >= 0) {
            states[static_cast<size_t>(kMaximumPhysicalButtons + hat * kPovDirectionCount + direction)] = true;
        }
    }
    return states;
}

EffectiveProfileSelection ProfileTriggerRuntime::selectionFor(int profileIndex, int source,
                                                               ProfileTriggerMode mode)
{
    if (source < kMaximumPhysicalButtons) return {profileIndex, source + 1, 0, -1, mode};
    const int povSource = source - kMaximumPhysicalButtons;
    return {profileIndex, 0, povSource / kPovDirectionCount + 1,
            povSource % kPovDirectionCount, mode};
}

EffectiveProfileSelection ProfileTriggerRuntime::automationSelectionFor(int profileIndex, int source,
                                                                          ProfileTriggerMode mode)
{
    EffectiveProfileSelection selection;
    selection.profileIndex = profileIndex;
    selection.sourceMode = mode;
    selection.sourceAutomationRule = source / kMaximumAutomationActions;
    selection.sourceAutomationAction = source % kMaximumAutomationActions;
    return selection;
}

bool ProfileTriggerRuntime::valid(const RuntimeProfileCache &cache, int source,
                                  ProfileTriggerMode expectedMode) const
{
    const RuntimeProfileTrigger &trigger = triggerFor(cache, source);
    return trigger.consumesInput && trigger.mode == expectedMode
        && trigger.targetProfileIndex >= 0
        && trigger.targetProfileIndex < static_cast<int>(cache.profiles.size());
}

void ProfileTriggerRuntime::captureSignatures(const RuntimeProfileCache &cache)
{
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        const RuntimeProfileTrigger &trigger = triggerFor(cache, source);
        m_signatures[static_cast<size_t>(source)] = {
            trigger.targetProfileIndex, trigger.mode, trigger.consumesInput};
    }
}

void ProfileTriggerRuntime::reset()
{
    m_previousInputs.fill(false);
    m_holdOrder.fill(0);
    m_toggleOrder.fill(0);
    m_signatures.fill({});
    m_automationHoldOrder.fill(0);
    m_automationToggleOrder.fill(0);
    m_automationSignatures.fill({});
    m_activationSequence = 0;
}

void ProfileTriggerRuntime::clearAutomationContributions()
{
    m_automationHoldOrder.fill(0);
    m_automationToggleOrder.fill(0);
    m_automationSignatures.fill({});
}

void ProfileTriggerRuntime::updateAutomationContributions(
    const std::array<AutomationProfileContribution, kMaximumAutomationProfileContributors> &contributions,
    int contributionCount, int profileCount)
{
    std::array<bool, kMaximumAutomationProfileContributors> seen{};
    const int count = std::clamp(contributionCount, 0, kMaximumAutomationProfileContributors);
    for (int index = 0; index < count; ++index) {
        const AutomationProfileContribution &contribution = contributions[static_cast<size_t>(index)];
        if (contribution.source < 0 || contribution.source >= kMaximumAutomationProfileContributors) continue;
        const size_t source = static_cast<size_t>(contribution.source);
        seen[source] = true;
        const bool targetValid = contribution.targetProfileIndex >= 0
            && contribution.targetProfileIndex < profileCount;
        const AutomationSignature signature{contribution.targetProfileIndex, contribution.mode};
        const bool changed = m_automationSignatures[source] != signature;
        if (changed || !targetValid) {
            m_automationHoldOrder[source] = 0;
            m_automationToggleOrder[source] = 0;
        }
        m_automationSignatures[source] = signature;
        if (!targetValid) continue;
        if (contribution.mode == ProfileTriggerMode::Hold) {
            if (contribution.active && m_automationHoldOrder[source] == 0) {
                m_automationHoldOrder[source] = ++m_activationSequence;
            }
            if (!contribution.active) m_automationHoldOrder[source] = 0;
            m_automationToggleOrder[source] = 0;
        } else if (contribution.mode == ProfileTriggerMode::Toggle) {
            m_automationHoldOrder[source] = 0;
            if (contribution.rising) {
                m_automationToggleOrder[source] = m_automationToggleOrder[source] == 0
                    ? ++m_activationSequence : 0;
            }
        }
    }
    for (int source = 0; source < kMaximumAutomationProfileContributors; ++source) {
        if (seen[static_cast<size_t>(source)]) continue;
        m_automationHoldOrder[static_cast<size_t>(source)] = 0;
        m_automationToggleOrder[static_cast<size_t>(source)] = 0;
        m_automationSignatures[static_cast<size_t>(source)] = {};
    }
}

void ProfileTriggerRuntime::initializeForMapping(const RuntimeProfileCache &cache,
                                                 const PhysicalButtonStates &buttons)
{
    PhysicalPovValues povs{};
    povs.fill(-1);
    initializeForMapping(cache, buttons, povs, 0);
}

void ProfileTriggerRuntime::initializeForMapping(const RuntimeProfileCache &cache,
                                                 const PhysicalButtonStates &buttons,
                                                 const PhysicalPovValues &povs, int povCount)
{
    reset();
    captureSignatures(cache);
    const InputStates inputs = inputStates(buttons, povs, povCount);
    m_previousInputs = inputs; // Toggles require a future logical entry edge.
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        if (inputs[static_cast<size_t>(source)] && valid(cache, source, ProfileTriggerMode::Hold)) {
            m_holdOrder[static_cast<size_t>(source)] = ++m_activationSequence;
        }
    }
}

void ProfileTriggerRuntime::reconcileConfiguration(const RuntimeProfileCache &cache,
                                                   const PhysicalButtonStates &buttons,
                                                   bool clearToggles)
{
    PhysicalPovValues povs{};
    povs.fill(-1);
    reconcileConfiguration(cache, buttons, povs, 0, clearToggles);
}

void ProfileTriggerRuntime::reconcileConfiguration(const RuntimeProfileCache &cache,
                                                   const PhysicalButtonStates &buttons,
                                                   const PhysicalPovValues &povs, int povCount,
                                                   bool clearToggles)
{
    const InputStates inputs = inputStates(buttons, povs, povCount);
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        const RuntimeProfileTrigger &trigger = triggerFor(cache, source);
        const TriggerSignature desired{trigger.targetProfileIndex, trigger.mode, trigger.consumesInput};
        const size_t index = static_cast<size_t>(source);
        const bool changed = m_signatures[index] != desired;
        if (changed || !valid(cache, source, ProfileTriggerMode::Hold)) {
            m_holdOrder[index] = 0;
        }
        if (changed || !valid(cache, source, ProfileTriggerMode::Toggle)) {
            m_toggleOrder[index] = 0;
        }
        // A newly configured/retargeted Hold control reflects the actual
        // physical state immediately. Toggle controls still need a new edge.
        if (changed && inputs[index] && valid(cache, source, ProfileTriggerMode::Hold)) {
            m_holdOrder[index] = ++m_activationSequence;
        }
        m_signatures[index] = desired;
    }
    if (clearToggles) m_toggleOrder.fill(0);
    m_previousInputs = inputs;
}

EffectiveProfileSelection ProfileTriggerRuntime::processReport(
    const RuntimeProfileCache &cache, const PhysicalButtonStates &buttons)
{
    PhysicalPovValues povs{};
    povs.fill(-1);
    return processReport(cache, buttons, povs, 0);
}

EffectiveProfileSelection ProfileTriggerRuntime::processReport(
    const RuntimeProfileCache &cache, const PhysicalButtonStates &buttons,
    const PhysicalPovValues &povs, int povCount)
{
    const InputStates inputs = inputStates(buttons, povs, povCount);
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        const size_t index = static_cast<size_t>(source);
        const bool pressed = inputs[index];
        const bool rising = pressed && !m_previousInputs[index];
        if (valid(cache, source, ProfileTriggerMode::Hold)) {
            if (pressed && m_holdOrder[index] == 0) m_holdOrder[index] = ++m_activationSequence;
            if (!pressed) m_holdOrder[index] = 0;
        } else {
            m_holdOrder[index] = 0;
        }
        if (valid(cache, source, ProfileTriggerMode::Toggle)) {
            if (rising) {
                m_toggleOrder[index] = m_toggleOrder[index] == 0 ? ++m_activationSequence : 0;
            }
        } else {
            m_toggleOrder[index] = 0;
        }
        m_previousInputs[index] = pressed;
    }
    return effectiveProfile(cache);
}

EffectiveProfileSelection ProfileTriggerRuntime::effectiveProfile(const RuntimeProfileCache &cache) const
{
    EffectiveProfileSelection result;
    result.profileIndex = std::clamp(cache.baseProfileIndex, 0,
                                     std::max(0, static_cast<int>(cache.profiles.size()) - 1));
    std::uint64_t newest = 0;
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        const size_t index = static_cast<size_t>(source);
        if (m_holdOrder[index] > newest && valid(cache, source, ProfileTriggerMode::Hold)) {
            newest = m_holdOrder[index];
            result = selectionFor(triggerFor(cache, source).targetProfileIndex, source,
                                  ProfileTriggerMode::Hold);
        }
    }
    for (int source = 0; source < kMaximumAutomationProfileContributors; ++source) {
        const size_t index = static_cast<size_t>(source);
        const AutomationSignature &signature = m_automationSignatures[index];
        if (m_automationHoldOrder[index] > newest && signature.mode == ProfileTriggerMode::Hold
            && signature.targetProfileIndex >= 0
            && signature.targetProfileIndex < static_cast<int>(cache.profiles.size())) {
            newest = m_automationHoldOrder[index];
            result = automationSelectionFor(signature.targetProfileIndex, source, ProfileTriggerMode::Hold);
        }
    }
    if (newest != 0) return result;
    for (int source = 0; source < kProfileTriggerSourceCount; ++source) {
        const size_t index = static_cast<size_t>(source);
        if (m_toggleOrder[index] > newest && valid(cache, source, ProfileTriggerMode::Toggle)) {
            newest = m_toggleOrder[index];
            result = selectionFor(triggerFor(cache, source).targetProfileIndex, source,
                                  ProfileTriggerMode::Toggle);
        }
    }
    for (int source = 0; source < kMaximumAutomationProfileContributors; ++source) {
        const size_t index = static_cast<size_t>(source);
        const AutomationSignature &signature = m_automationSignatures[index];
        if (m_automationToggleOrder[index] > newest && signature.mode == ProfileTriggerMode::Toggle
            && signature.targetProfileIndex >= 0
            && signature.targetProfileIndex < static_cast<int>(cache.profiles.size())) {
            newest = m_automationToggleOrder[index];
            result = automationSelectionFor(signature.targetProfileIndex, source, ProfileTriggerMode::Toggle);
        }
    }
    return result;
}

} // namespace hotas
