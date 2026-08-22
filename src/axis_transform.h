#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

// The fixed v1.4 order is calibration -> normalization -> rescaled deadzone
// -> hysteresis -> inversion -> compiled response curve -> output limits.
// Static previews intentionally omit hysteresis because it is stateful and
// cannot be represented truthfully as a single-valued transfer trace.
struct AxisHysteresisState {
    float lastAcceptedInput = 0.0F;
    bool initialized = false;
};

struct AxisSignalPath {
    float normalized = 0.0F;
    float afterDeadzone = 0.0F;
    float afterHysteresis = 0.0F;
    float afterInversion = 0.0F;
    float afterCurve = 0.0F;
    float afterLimits = 0.0F;
};

float clampUnit(float value);
float normalizeCalibrated(float raw, const Calibration &calibration);
float applyRescaledDeadzone(float value, float deadzone);
float preprocessAxisInput(float raw, const RuntimeAxisMapping &mapping);
float evaluateResponseCurve(float value, const RuntimeAxisMapping &mapping);
float applyOutputLimits(float value, const AxisMapping &mapping);
float applyAxisHysteresis(float value, float threshold, AxisHysteresisState &state);
float evaluateStaticAxisTransfer(float raw, const RuntimeAxisMapping &mapping,
                                 float *curveResponse = nullptr,
                                 AxisSignalPath *signalPath = nullptr);
float transformAxisLive(float raw, const RuntimeAxisMapping &mapping,
                        AxisHysteresisState &hysteresisState,
                        float *curveResponse = nullptr, AxisSignalPath *signalPath = nullptr);
float transformAxis(float raw, const RuntimeAxisMapping &mapping);

// Restores safe, bounded profile values after configuration deserialization.
void normalizeAxisProcessing(AxisMapping &mapping);

// Rejects duplicate non-disabled targets, retaining the first assignment.
// Returns true when the configuration was already conflict-free.
bool normalizeMappingConflicts(AxisMappings &mappings);
bool hasMappingConflict(const AxisMappings &mappings, int sourceIndex,
                        VirtualAxis candidateTarget);

} // namespace hotas
