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

// The output plan is rebuilt from fixed-size arrays once per physical report.
// It is deliberately free of UI, allocation, and driver calls so the mapper
// can publish a configurable safe value for unused vJoy axes without adding
// work outside its existing change-driven output loop.
struct VirtualAxisOutputPlan {
    std::array<float, kVirtualAxisSlotCount> values{};
    std::array<int, kVirtualAxisSlotCount> sourceIndexes{};
};

inline VirtualAxisOutputPlan buildVirtualAxisOutputPlan(
    const RuntimeMappingConfiguration &mapping,
    const std::array<bool, kPhysicalAxisCount> &availableAxes,
    const std::array<float, kPhysicalAxisCount> &transformedAxes,
    float disabledAxisValue)
{
    VirtualAxisOutputPlan plan;
    plan.values.fill(sanitizedDisabledAxisValue(disabledAxisValue));
    plan.sourceIndexes.fill(-1);
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const int target = static_cast<int>(mapping.axes[static_cast<size_t>(index)].profile.target);
        if (!availableAxes[static_cast<size_t>(index)] || target <= 0
            || target >= static_cast<int>(plan.values.size()) || plan.sourceIndexes[target] >= 0) {
            continue;
        }
        plan.values[target] = transformedAxes[static_cast<size_t>(index)];
        plan.sourceIndexes[target] = index;
    }
    return plan;
}

float clampUnit(float value);
// Bounded control-plane estimator used only when calibration is finalized.
// It is intentionally not called from the DirectInput report path.
float robustCalibrationCenter(const std::array<float, 32> &values, int count);
float normalizeCalibrated(float raw, const Calibration &calibration);
float applyRescaledDeadzone(float value, float deadzone);
float applyRescaledUnipolarDeadzone(float value, float deadzone);
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
