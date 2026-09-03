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

float robustCalibrationCenter(const std::array<float, 32> &values, int count)
{
    count = std::clamp(count, 0, static_cast<int>(values.size()));
    if (count == 0) return 0.0F;

    std::array<float, 32> ordered = values;
    std::sort(ordered.begin(), ordered.begin() + count);
    const int middle = count / 2;
    return count % 2 == 0
        ? (ordered[middle - 1] + ordered[middle]) * 0.5F
        : ordered[middle];
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
    if (!calibration.centered) {
        if (!(minimum < maximum)) return raw;
        return clampUnit(-1.0F + 2.0F * (raw - minimum) / (maximum - minimum));
    }
    if (!(minimum < center && center < maximum)) return raw;

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

float applyRescaledUnipolarDeadzone(float value, float deadzone)
{
    value = std::clamp(value, 0.0F, 1.0F);
    deadzone = std::clamp(deadzone, 0.0F, 0.95F);
    if (value <= deadzone) return 0.0F;
    return std::min((value - deadzone) / (1.0F - deadzone), 1.0F);
}

void normalizeAxisProcessing(AxisMapping &mapping)
{
    mapping.deadzone = std::clamp(mapping.deadzone, 0.0F, 0.95F);
    mapping.hysteresis = std::clamp(mapping.hysteresis, 0.0F, 0.25F);
    const float domainMinimum = mapping.rangeMode == AxisRangeMode::OneSided ? 0.0F : -1.0F;
    mapping.outputMinimum = std::clamp(mapping.outputMinimum, domainMinimum, 1.0F);
    mapping.outputMaximum = std::clamp(mapping.outputMaximum, domainMinimum, 1.0F);
    if (mapping.outputMinimum >= mapping.outputMaximum) {
        mapping.outputMinimum = domainMinimum;
        mapping.outputMaximum = 1.0F;
    }
}

void switchAxisOutputLimitDomain(AxisMapping &mapping, AxisRangeMode rangeMode)
{
    if (mapping.rangeMode == rangeMode) return;
    if (mapping.rangeMode == AxisRangeMode::OneSided) {
        mapping.oneSidedOutputMinimum = mapping.outputMinimum;
        mapping.oneSidedOutputMaximum = mapping.outputMaximum;
        mapping.outputMinimum = mapping.centeredOutputMinimum;
        mapping.outputMaximum = mapping.centeredOutputMaximum;
    } else {
        mapping.centeredOutputMinimum = mapping.outputMinimum;
        mapping.centeredOutputMaximum = mapping.outputMaximum;
        mapping.outputMinimum = mapping.oneSidedOutputMinimum;
        mapping.outputMaximum = mapping.oneSidedOutputMaximum;
    }
    mapping.rangeMode = rangeMode;
    normalizeAxisProcessing(mapping);
    if (rangeMode == AxisRangeMode::OneSided) {
        mapping.oneSidedOutputMinimum = mapping.outputMinimum;
        mapping.oneSidedOutputMaximum = mapping.outputMaximum;
    } else {
        mapping.centeredOutputMinimum = mapping.outputMinimum;
        mapping.centeredOutputMaximum = mapping.outputMaximum;
    }
}

float preprocessAxisInput(float raw, const RuntimeAxisMapping &mapping)
{
    const float calibrated = normalizeCalibrated(raw, mapping.calibration);
    if (mapping.profile.rangeMode == AxisRangeMode::OneSided) {
        return applyRescaledUnipolarDeadzone(std::max(0.0F, calibrated),
                                             mapping.profile.deadzone);
    }
    return applyRescaledDeadzone(calibrated, mapping.profile.deadzone);
}

float evaluateResponseCurve(float value, const RuntimeAxisMapping &mapping)
{
    if (mapping.profile.rangeMode == AxisRangeMode::OneSided) {
        // The immutable LUT retains a compact -1..1 storage encoding. The
        // report path deliberately crosses that representation only at the
        // curve boundary; every surrounding transform remains 0..1.
        const float encoded = evaluateCompiledResponseCurve(value * 2.0F - 1.0F,
                                                             mapping.responseCurve);
        return std::clamp((encoded + 1.0F) * 0.5F, 0.0F, 1.0F);
    }
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
    const float calibrated = normalizeCalibrated(raw, mapping.calibration);
    const bool oneSided = mapping.profile.rangeMode == AxisRangeMode::OneSided;
    const float normalized = oneSided ? std::max(0.0F, calibrated) : calibrated;
    float transformed = oneSided
        ? applyRescaledUnipolarDeadzone(normalized, mapping.profile.deadzone)
        : applyRescaledDeadzone(normalized, mapping.profile.deadzone);
    if (signalPath) {
        signalPath->normalized = normalized;
        signalPath->afterDeadzone = transformed;
        signalPath->afterHysteresis = transformed;
    }
    if (mapping.profile.inverted) transformed = oneSided ? 1.0F - transformed : -transformed;
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
    const float calibrated = normalizeCalibrated(raw, mapping.calibration);
    return transformNormalizedAxisLive(calibrated, mapping, hysteresisState, curveResponse, signalPath);
}

float transformNormalizedAxisLive(float normalized, const RuntimeAxisMapping &mapping,
                                  AxisHysteresisState &hysteresisState, float *curveResponse,
                                  AxisSignalPath *signalPath)
{
    const bool oneSided = mapping.profile.rangeMode == AxisRangeMode::OneSided;
    normalized = oneSided ? std::clamp(normalized, 0.0F, 1.0F) : clampUnit(normalized);
    const float afterDeadzone = oneSided
        ? applyRescaledUnipolarDeadzone(normalized, mapping.profile.deadzone)
        : applyRescaledDeadzone(normalized, mapping.profile.deadzone);
    float transformed = applyAxisHysteresis(afterDeadzone, mapping.profile.hysteresis, hysteresisState);
    if (signalPath) {
        signalPath->normalized = normalized;
        signalPath->afterDeadzone = afterDeadzone;
        signalPath->afterHysteresis = transformed;
    }
    if (mapping.profile.inverted) transformed = oneSided ? 1.0F - transformed : -transformed;
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
