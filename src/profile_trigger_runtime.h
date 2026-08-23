#pragma once

#include "automation_engine.h"
#include "button_mapping.h"

#include <array>
#include <cstdint>

namespace hotas {

// The worker-owned, fixed-size state machine for global profile controls.
// It consumes only an already-compiled RuntimeProfileCache and physical input
// snapshots, so pressing a button or entering a POV direction cannot perform
// config I/O or curve work.
constexpr int kProfileTriggerPovSourceCount = kMaximumPhysicalPovs * kPovDirectionCount;
constexpr int kProfileTriggerSourceCount = kMaximumPhysicalButtons + kProfileTriggerPovSourceCount;

struct EffectiveProfileSelection {
    int profileIndex = 0;
    int sourceButton = 0; // One-based physical button; 0 means manual/base.
    int sourcePovHat = 0; // One-based physical POV hat; 0 means not a POV source.
    int sourcePovDirection = -1;
    ProfileTriggerMode sourceMode = ProfileTriggerMode::Disabled;
    int sourceAutomationRule = -1;
    int sourceAutomationAction = -1;
};

class ProfileTriggerRuntime final {
public:
    void reset();
    void initializeForMapping(const RuntimeProfileCache &cache,
                              const PhysicalButtonStates &buttons);
    void initializeForMapping(const RuntimeProfileCache &cache,
                              const PhysicalButtonStates &buttons,
                              const PhysicalPovValues &povs, int povCount);
    void reconcileConfiguration(const RuntimeProfileCache &cache,
                                const PhysicalButtonStates &buttons,
                                bool clearToggles);
    void reconcileConfiguration(const RuntimeProfileCache &cache,
                                const PhysicalButtonStates &buttons,
                                const PhysicalPovValues &povs, int povCount,
                                bool clearToggles);
    EffectiveProfileSelection processReport(const RuntimeProfileCache &cache,
                                            const PhysicalButtonStates &buttons);
    EffectiveProfileSelection processReport(const RuntimeProfileCache &cache,
                                            const PhysicalButtonStates &buttons,
                                            const PhysicalPovValues &povs, int povCount);
    EffectiveProfileSelection effectiveProfile(const RuntimeProfileCache &cache) const;
    void updateAutomationContributions(
        const std::array<AutomationProfileContribution, kMaximumAutomationProfileContributors> &contributions,
        int contributionCount, int profileCount);
    void clearAutomationContributions();

private:
    struct TriggerSignature {
        int targetProfileIndex = -1;
        ProfileTriggerMode mode = ProfileTriggerMode::Disabled;
        bool consumesInput = false;

        bool operator==(const TriggerSignature &) const = default;
    };

    using InputStates = std::array<bool, kProfileTriggerSourceCount>;

    static const RuntimeProfileTrigger &triggerFor(const RuntimeProfileCache &cache, int source);
    static InputStates inputStates(const PhysicalButtonStates &buttons,
                                   const PhysicalPovValues &povs, int povCount);
    static EffectiveProfileSelection selectionFor(int profileIndex, int source,
                                                  ProfileTriggerMode mode);
    static EffectiveProfileSelection automationSelectionFor(int profileIndex, int source,
                                                            ProfileTriggerMode mode);
    bool valid(const RuntimeProfileCache &cache, int source, ProfileTriggerMode expectedMode) const;
    void captureSignatures(const RuntimeProfileCache &cache);

    InputStates m_previousInputs{};
    std::array<std::uint64_t, kProfileTriggerSourceCount> m_holdOrder{};
    std::array<std::uint64_t, kProfileTriggerSourceCount> m_toggleOrder{};
    std::array<TriggerSignature, kProfileTriggerSourceCount> m_signatures{};
    struct AutomationSignature {
        int targetProfileIndex = -1;
        ProfileTriggerMode mode = ProfileTriggerMode::Disabled;

        bool operator==(const AutomationSignature &) const = default;
    };
    std::array<std::uint64_t, kMaximumAutomationProfileContributors> m_automationHoldOrder{};
    std::array<std::uint64_t, kMaximumAutomationProfileContributors> m_automationToggleOrder{};
    std::array<AutomationSignature, kMaximumAutomationProfileContributors> m_automationSignatures{};
    std::uint64_t m_activationSequence = 0;
};

} // namespace hotas
