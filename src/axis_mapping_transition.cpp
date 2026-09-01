#include "axis_mapping_transition.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hotas {

namespace {

bool validAxis(std::size_t virtualAxis)
{
    return virtualAxis > 0 && virtualAxis < kVirtualAxisSlotCount;
}

float clampOutput(float value)
{
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

float smoothstep(float value)
{
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

} // namespace

void AxisMappingTransitionEngine::clear()
{
    m_states.fill({});
}

void AxisMappingTransitionEngine::clear(std::size_t virtualAxis)
{
    if (virtualAxis < m_states.size()) m_states[virtualAxis] = {};
}

void AxisMappingTransitionEngine::begin(std::size_t virtualAxis, float actualOutput,
                                        float newMappedOutput, float currentInput,
                                        int sourceAxis, std::uint64_t nowUs,
                                        CurveTransitionSmoothingSettings settings)
{
    if (!validAxis(virtualAxis)) return;
    settings = sanitizedCurveTransitionSmoothing(settings);
    AxisMappingTransitionState &state = m_states[virtualAxis];
    if (!settings.enabled || settings.durationMs == 0 || !std::isfinite(actualOutput)
        || !std::isfinite(newMappedOutput)) {
        state = {};
        return;
    }

    const float offset = clampOutput(actualOutput) - clampOutput(newMappedOutput);
    if (std::abs(offset) < kCurveTransitionMeaningfulJump) {
        state = {};
        return;
    }

    state.active = true;
    state.offset = offset;
    state.inputAtSwitch = currentInput;
    state.sourceAxis = sourceAxis;
    state.startedUs = nowUs;
    state.durationUs = static_cast<std::uint32_t>(settings.durationMs) * 1000U;
}

float AxisMappingTransitionEngine::apply(std::size_t virtualAxis, float newMappedOutput,
                                         float currentInput, int sourceAxis,
                                         std::uint64_t nowUs)
{
    const float mapped = clampOutput(newMappedOutput);
    if (!validAxis(virtualAxis)) return mapped;
    AxisMappingTransitionState &state = m_states[virtualAxis];
    if (!state.active) return mapped;

    // A mapping reassignment can change the contributing physical source; a
    // new begin() normally accompanies it, but never retain correction across
    // an unmatched source change.
    if (state.sourceAxis != sourceAxis
        || (sourceAxis >= 0 && std::abs(currentInput - state.inputAtSwitch)
            >= kCurveTransitionUserIntentDelta)) {
        state = {};
        return mapped;
    }

    const std::uint64_t elapsedUs = nowUs >= state.startedUs ? nowUs - state.startedUs : 0;
    if (state.durationUs == 0 || elapsedUs >= state.durationUs) {
        state = {};
        return mapped;
    }

    const float progress = static_cast<float>(elapsedUs) / static_cast<float>(state.durationUs);
    const float decay = 1.0F - smoothstep(progress);
    return clampOutput(mapped + state.offset * decay);
}

bool AxisMappingTransitionEngine::active(std::size_t virtualAxis) const
{
    return validAxis(virtualAxis) && m_states[virtualAxis].active;
}

} // namespace hotas
