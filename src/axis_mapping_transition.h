#pragma once

#include "mapping_types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace hotas {

// A compact, allocation-free bumpless-transfer state machine. It is indexed
// by virtual axis because that is the output the game actually observes.
constexpr float kCurveTransitionMeaningfulJump = 0.005F;
constexpr float kCurveTransitionUserIntentDelta = 0.15F;

struct AxisMappingTransitionState {
    bool active = false;
    float offset = 0.0F;
    float inputAtSwitch = 0.0F;
    int sourceAxis = -1;
    std::uint64_t startedUs = 0;
    std::uint32_t durationUs = 0;
};

class AxisMappingTransitionEngine final {
public:
    void clear();
    void clear(std::size_t virtualAxis);

    // Starts from the actual currently published virtual value, never a
    // theoretical old-map result. A no-op setting or microscopic difference
    // leaves the new mapping direct.
    void begin(std::size_t virtualAxis, float actualOutput, float newMappedOutput,
               float currentInput, int sourceAxis, std::uint64_t nowUs,
               CurveTransitionSmoothingSettings settings);

    // Applies the decaying continuity offset to the new mapping. Strong pilot
    // movement immediately removes the correction rather than fighting input.
    float apply(std::size_t virtualAxis, float newMappedOutput, float currentInput,
                int sourceAxis, std::uint64_t nowUs);

    bool active(std::size_t virtualAxis) const;

private:
    std::array<AxisMappingTransitionState, kVirtualAxisSlotCount> m_states{};
};

} // namespace hotas
