#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

// The fixed v1.3 order is calibration -> normalization -> rescaled deadzone
// -> hysteresis -> inversion -> identity response curve -> output limits.
// Static previews intentionally omit hysteresis because it is stateful and
// cannot be represented truthfully as a single-valued transfer trace.
struct AxisHysteresisState {
    float lastAcceptedInput = 0.0F;
    bool initialized = false;
};

float clampUnit(float value);
float normalizeCalibrated(float raw, const Calibration &calibration);
float applyRescaledDeadzone(float value, float deadzone);
float preprocessAxisInput(float raw, const RuntimeAxisMapping &mapping);
float evaluateResponseCurve(float value, const AxisMapping &mapping);
float applyOutputLimits(float value, const AxisMapping &mapping);
float applyAxisHysteresis(float value, float threshold, AxisHysteresisState &state);
float evaluateStaticAxisTransfer(float raw, const RuntimeAxisMapping &mapping);
float transformAxisLive(float raw, const RuntimeAxisMapping &mapping,
                        AxisHysteresisState &hysteresisState);
float transformAxis(float raw, const RuntimeAxisMapping &mapping);

// Restores safe, bounded profile values after configuration deserialization.
void normalizeAxisProcessing(AxisMapping &mapping);

// Rejects duplicate non-disabled targets, retaining the first assignment.
// Returns true when the configuration was already conflict-free.
bool normalizeMappingConflicts(AxisMappings &mappings);
bool hasMappingConflict(const AxisMappings &mappings, int sourceIndex,
                        VirtualAxis candidateTarget);

} // namespace hotas
