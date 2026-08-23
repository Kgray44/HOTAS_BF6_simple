#pragma once

#include "button_mapping.h"

#include <array>
#include <cstdint>

namespace hotas {

// The worker-owned, fixed-size state machine for global profile controls.
// It consumes only an already-compiled RuntimeProfileCache and physical button
// snapshots, so pressing a trigger cannot perform config I/O or curve work.
struct EffectiveProfileSelection {
    int profileIndex = 0;
    int sourceButton = 0; // One-based physical button; 0 means manual/base.
    ProfileTriggerMode sourceMode = ProfileTriggerMode::Disabled;
};

class ProfileTriggerRuntime final {
public:
    void reset();
    void initializeForMapping(const RuntimeProfileCache &cache,
                              const PhysicalButtonStates &buttons);
    void reconcileConfiguration(const RuntimeProfileCache &cache,
                                const PhysicalButtonStates &buttons,
                                bool clearToggles);
    EffectiveProfileSelection processReport(const RuntimeProfileCache &cache,
                                            const PhysicalButtonStates &buttons);
    EffectiveProfileSelection effectiveProfile(const RuntimeProfileCache &cache) const;

private:
    struct TriggerSignature {
        int targetProfileIndex = -1;
        ProfileTriggerMode mode = ProfileTriggerMode::Disabled;
        bool consumesButton = false;

        bool operator==(const TriggerSignature &) const = default;
    };

    bool valid(const RuntimeProfileCache &cache, int source,
               ProfileTriggerMode expectedMode) const;
    void captureSignatures(const RuntimeProfileCache &cache);

    PhysicalButtonStates m_previousButtons{};
    std::array<std::uint64_t, kMaximumPhysicalButtons> m_holdOrder{};
    std::array<std::uint64_t, kMaximumPhysicalButtons> m_toggleOrder{};
    std::array<TriggerSignature, kMaximumPhysicalButtons> m_signatures{};
    std::uint64_t m_activationSequence = 0;
};

} // namespace hotas
