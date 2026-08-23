#include "profile_trigger_runtime.h"

#include <algorithm>

namespace hotas {

bool ProfileTriggerRuntime::valid(const RuntimeProfileCache &cache, int source,
                                  ProfileTriggerMode expectedMode) const
{
    const RuntimeProfileTrigger &trigger = cache.profileTriggers[static_cast<size_t>(source)];
    return trigger.consumesButton && trigger.mode == expectedMode
        && trigger.targetProfileIndex >= 0
        && trigger.targetProfileIndex < static_cast<int>(cache.profiles.size());
}

void ProfileTriggerRuntime::captureSignatures(const RuntimeProfileCache &cache)
{
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const RuntimeProfileTrigger &trigger = cache.profileTriggers[static_cast<size_t>(source)];
        m_signatures[static_cast<size_t>(source)] = {
            trigger.targetProfileIndex, trigger.mode, trigger.consumesButton};
    }
}

void ProfileTriggerRuntime::reset()
{
    m_previousButtons.fill(false);
    m_holdOrder.fill(0);
    m_toggleOrder.fill(0);
    m_signatures.fill({});
    m_activationSequence = 0;
}

void ProfileTriggerRuntime::initializeForMapping(const RuntimeProfileCache &cache,
                                                 const PhysicalButtonStates &buttons)
{
    reset();
    captureSignatures(cache);
    m_previousButtons = buttons; // Toggles require a future rising edge.
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (buttons[static_cast<size_t>(source)] && valid(cache, source, ProfileTriggerMode::Hold)) {
            m_holdOrder[static_cast<size_t>(source)] = ++m_activationSequence;
        }
    }
}

void ProfileTriggerRuntime::reconcileConfiguration(const RuntimeProfileCache &cache,
                                                   const PhysicalButtonStates &buttons,
                                                   bool clearToggles)
{
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const RuntimeProfileTrigger &trigger = cache.profileTriggers[static_cast<size_t>(source)];
        const TriggerSignature desired{trigger.targetProfileIndex, trigger.mode, trigger.consumesButton};
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
        if (changed && buttons[index] && valid(cache, source, ProfileTriggerMode::Hold)) {
            m_holdOrder[index] = ++m_activationSequence;
        }
        m_signatures[index] = desired;
    }
    if (clearToggles) m_toggleOrder.fill(0);
    m_previousButtons = buttons;
}

EffectiveProfileSelection ProfileTriggerRuntime::processReport(
    const RuntimeProfileCache &cache, const PhysicalButtonStates &buttons)
{
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const size_t index = static_cast<size_t>(source);
        const bool pressed = buttons[index];
        const bool rising = pressed && !m_previousButtons[index];
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
        m_previousButtons[index] = pressed;
    }
    return effectiveProfile(cache);
}

EffectiveProfileSelection ProfileTriggerRuntime::effectiveProfile(const RuntimeProfileCache &cache) const
{
    EffectiveProfileSelection result;
    result.profileIndex = std::clamp(cache.baseProfileIndex, 0,
                                     std::max(0, static_cast<int>(cache.profiles.size()) - 1));
    std::uint64_t newest = 0;
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const size_t index = static_cast<size_t>(source);
        if (m_holdOrder[index] > newest && valid(cache, source, ProfileTriggerMode::Hold)) {
            newest = m_holdOrder[index];
            result = {cache.profileTriggers[index].targetProfileIndex, source + 1, ProfileTriggerMode::Hold};
        }
    }
    if (newest != 0) return result;
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const size_t index = static_cast<size_t>(source);
        if (m_toggleOrder[index] > newest && valid(cache, source, ProfileTriggerMode::Toggle)) {
            newest = m_toggleOrder[index];
            result = {cache.profileTriggers[index].targetProfileIndex, source + 1, ProfileTriggerMode::Toggle};
        }
    }
    return result;
}

} // namespace hotas
