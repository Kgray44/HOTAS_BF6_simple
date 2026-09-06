#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

// Legacy non-centre transfer state retained for the curve editor and old
// configuration semantics.  The mapper's centred-axis report path instead
// uses AxisCenterResolverState below, before Adaptive Response.
struct AxisHysteresisState {
    float lastAcceptedInput = 0.0F;
    bool initialized = false;
};

// Fixed, per-axis report state for the canonical centre resolver.  A centred
// control is held at exact neutral only after it has actually settled there;
// an in-flight sweep keeps its continuous normalized trajectory through zero.
// No time source, allocation, lock, or UI dependency is involved.
struct AxisCenterResolverState {
    float previousInput = 0.0F;
    float previousDelta = 0.0F;
    unsigned char quietSamples = 0;
    bool initialized = false;
    bool centerHeld = false;
};

struct AxisSignalPath {
    float normalized = 0.0F;
    float afterDeadzone = 0.0F;
    float afterHysteresis = 0.0F;
    float afterInversion = 0.0F;
    float afterCurve = 0.0F;
    float afterLimits = 0.0F;
};

// The estimator continues to reason about physical stick motion. This small
// fixed-size result captures the separate, curve-aware authority decision in
// game-output space without introducing state, allocation, or UI work.
struct AdaptiveMappedAxisOutput {
    AxisSignalPath baselineSignalPath{};
    float baselineOutput = 0.0F;
    float predictedMappedOutput = 0.0F;
    float adaptiveOutput = 0.0F;
    float physicalLead = 0.0F;
    float mappedLead = 0.0F;
    float appliedLead = 0.0F;
    float localCurveGain = 0.0F;
    // Compatibility telemetry retained for existing snapshots. Centre hold
    // now occurs before prediction, so mapped-output authority is never
    // acquired or collapsed at a downstream deadzone edge.
    float deadzoneAuthority = 1.0F;
    bool deadzoneAuthorityBlocked = false;
    bool leadLimited = false;
    bool highLocalCurveGain = false;
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
// Resolves calibrated/normalized physical input into the one canonical signal
// consumed by the centred-axis mapper, predictor, diagnostics, and response
// previews.  mapping.profile.hysteresis provides the hold/release band.
float resolveNormalizedAxisCenter(float normalized, const RuntimeAxisMapping &mapping,
                                  AxisCenterResolverState &state);
float evaluateResponseCurve(float value, const RuntimeAxisMapping &mapping);
float applyOutputLimits(float value, const AxisMapping &mapping);
float applyAxisHysteresis(float value, float threshold, AxisHysteresisState &state);
// Evaluates the deterministic, non-hysteretic part of the per-axis pipeline
// for an already-normalized physical position. This is the F(x) used to
// compare current and predicted physical positions in mapped-output space.
float evaluateStaticNormalizedAxisTransfer(float normalized, const RuntimeAxisMapping &mapping,
                                           float *curveResponse = nullptr,
                                           AxisSignalPath *signalPath = nullptr);
// Maps an already centre-resolved normalized value.  It deliberately does not
// apply a second deadzone or hysteresis stage.
float evaluateResolvedNormalizedAxisTransfer(float resolvedNormalized,
                                             const RuntimeAxisMapping &mapping,
                                             float *curveResponse = nullptr,
                                             AxisSignalPath *signalPath = nullptr);
float evaluateStaticAxisTransfer(float raw, const RuntimeAxisMapping &mapping,
                                 float *curveResponse = nullptr,
                                 AxisSignalPath *signalPath = nullptr);
// Legacy live transfer used outside the resolved Adaptive Response pipeline.
float transformNormalizedAxisLive(float normalized, const RuntimeAxisMapping &mapping,
                                  AxisHysteresisState &hysteresisState,
                                  float *curveResponse = nullptr,
                                  AxisSignalPath *signalPath = nullptr);
float transformAxisLive(float raw, const RuntimeAxisMapping &mapping,
                        AxisHysteresisState &hysteresisState,
                        float *curveResponse = nullptr, AxisSignalPath *signalPath = nullptr);
float transformAxis(float raw, const RuntimeAxisMapping &mapping);
// Applies an already-computed prediction to the current resolved mapped
// baseline. The configured maximum lead is interpreted as a mapped-output
// limit; the predictor remains in normalized physical-axis space.
AdaptiveMappedAxisOutput applyCurveAwareAdaptiveResponse(
    float physicalCurrent, float physicalFuture, bool adaptiveEnabled,
    float maximumMappedLead, const RuntimeAxisMapping &mapping,
    AxisHysteresisState &hysteresisState);

// Restores safe, bounded profile values after configuration deserialization.
void normalizeAxisProcessing(AxisMapping &mapping);
// Control-plane range transition for the domain-specific output limits.
// This is never called by the DirectInput-to-vJoy report path.
void switchAxisOutputLimitDomain(AxisMapping &mapping, AxisRangeMode rangeMode);

// Keeps valid shared non-disabled targets intact during configuration load.
// The fixed-size output plan resolves a shared target deterministically by
// physical-axis row order; the user may explicitly opt in through the UI.
bool normalizeMappingConflicts(AxisMappings &mappings);
bool hasMappingConflict(const AxisMappings &mappings, int sourceIndex,
                        VirtualAxis candidateTarget);

} // namespace hotas
