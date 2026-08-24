#include "axis_transform.h"

#include "response_curve.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hotas {

float clampUnit(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

float normalizeCalibrated(float raw, const Calibration &calibration)
{
    raw = clampUnit(raw);
    if (!calibration.enabled) {
        return raw;
    }

    const float minimum = clampUnit(calibration.minimum);
    const float center = clampUnit(calibration.center);
    const float maximum = clampUnit(calibration.maximum);
    if (!(minimum < center && center < maximum)) {
        return raw;
    }

    if (raw >= center) {
        return clampUnit((raw - center) / (maximum - center));
    }
    return clampUnit((raw - center) / (center - minimum));
}

float applyRescaledDeadzone(float value, float deadzone)
{
    value = clampUnit(value);
    deadzone = std::clamp(deadzone, 0.0F, 0.95F);
    const float magnitude = std::abs(value);
    if (magnitude <= deadzone) {
        return 0.0F;
    }

    const float rescaled = (magnitude - deadzone) / (1.0F - deadzone);
    return std::copysign(std::min(rescaled, 1.0F), value);
}

void normalizeAxisProcessing(AxisMapping &mapping)
{
    mapping.deadzone = std::clamp(mapping.deadzone, 0.0F, 0.95F);
    mapping.hysteresis = std::clamp(mapping.hysteresis, 0.0F, 0.25F);
    mapping.outputMinimum = clampUnit(mapping.outputMinimum);
    mapping.outputMaximum = clampUnit(mapping.outputMaximum);
    if (mapping.outputMinimum >= mapping.outputMaximum) {
        mapping.outputMinimum = -1.0F;
        mapping.outputMaximum = 1.0F;
    }
}

float preprocessAxisInput(float raw, const RuntimeAxisMapping &mapping)
{
    return applyRescaledDeadzone(normalizeCalibrated(raw, mapping.calibration),
                                 mapping.profile.deadzone);
}

float evaluateResponseCurve(float value, const RuntimeAxisMapping &mapping)
{
    return evaluateCompiledResponseCurve(value, mapping.responseCurve);
}

float applyOutputLimits(float value, const AxisMapping &mapping)
{
    return std::clamp(value, mapping.outputMinimum, mapping.outputMaximum);
}

float applyAxisHysteresis(float value, float threshold, AxisHysteresisState &state)
{
    threshold = std::max(0.0F, threshold);
    if (!state.initialized || threshold == 0.0F
        || std::abs(value - state.lastAcceptedInput) >= threshold) {
        state.lastAcceptedInput = clampUnit(value);
        state.initialized = true;
    }
    return state.lastAcceptedInput;
}

float evaluateStaticAxisTransfer(float raw, const RuntimeAxisMapping &mapping, float *curveResponse,
                                 AxisSignalPath *signalPath)
{
    const float normalized = normalizeCalibrated(raw, mapping.calibration);
    float transformed = applyRescaledDeadzone(normalized, mapping.profile.deadzone);
    if (signalPath) {
        signalPath->normalized = normalized;
        signalPath->afterDeadzone = transformed;
        signalPath->afterHysteresis = transformed;
    }
    if (mapping.profile.inverted) {
        transformed = -transformed;
    }
    if (signalPath) signalPath->afterInversion = transformed;
    transformed = evaluateResponseCurve(transformed, mapping);
    if (curveResponse) *curveResponse = transformed;
    if (signalPath) signalPath->afterCurve = transformed;
    transformed = applyOutputLimits(transformed, mapping.profile);
    if (signalPath) signalPath->afterLimits = transformed;
    return transformed;
}

float transformAxisLive(float raw, const RuntimeAxisMapping &mapping,
                        AxisHysteresisState &hysteresisState, float *curveResponse,
                        AxisSignalPath *signalPath)
{
    const float normalized = normalizeCalibrated(raw, mapping.calibration);
    const float afterDeadzone = applyRescaledDeadzone(normalized, mapping.profile.deadzone);
    float transformed = applyAxisHysteresis(afterDeadzone, mapping.profile.hysteresis, hysteresisState);
    if (signalPath) {
        signalPath->normalized = normalized;
        signalPath->afterDeadzone = afterDeadzone;
        signalPath->afterHysteresis = transformed;
    }
    if (mapping.profile.inverted) {
        transformed = -transformed;
    }
    if (signalPath) signalPath->afterInversion = transformed;
    transformed = evaluateResponseCurve(transformed, mapping);
    if (curveResponse) *curveResponse = transformed;
    if (signalPath) signalPath->afterCurve = transformed;
    transformed = applyOutputLimits(transformed, mapping.profile);
    if (signalPath) signalPath->afterLimits = transformed;
    return transformed;
}

float transformAxis(float raw, const RuntimeAxisMapping &mapping)
{
    return evaluateStaticAxisTransfer(raw, mapping);
}

bool normalizeMappingConflicts(AxisMappings &mappings)
{
    std::array<bool, kVirtualAxisSlotCount> occupied{};
    bool clean = true;
    for (auto &mapping : mappings) {
        const int target = static_cast<int>(mapping.target);
        if (mapping.target == VirtualAxis::Disabled) {
            continue;
        }
        if (target < 1 || target >= static_cast<int>(occupied.size()) || occupied[target]) {
            mapping.target = VirtualAxis::Disabled;
            clean = false;
            continue;
        }
        occupied[target] = true;
    }
    return clean;
}

bool hasMappingConflict(const AxisMappings &mappings, int sourceIndex,
                        VirtualAxis candidateTarget)
{
    if (candidateTarget == VirtualAxis::Disabled) {
        return false;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (index != sourceIndex && mappings[index].target == candidateTarget) {
            return true;
        }
    }
    return false;
}

} // namespace hotas
