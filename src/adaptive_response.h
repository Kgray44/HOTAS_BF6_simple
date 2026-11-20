#pragma once

#include "mapping_types.h"

#include <array>
#include <chrono>
#include <cstdint>

#ifndef HOTAS_SPEED_DECAY_GATE_PROFILE_MODE
// V2.3.T candidate default. Profiling builds override this with 0, 1, 2,
// or 3 to isolate the state bookkeeping, detector, and full envelope cost.
#define HOTAS_SPEED_DECAY_GATE_PROFILE_MODE 3
#endif

namespace hotas {

enum class AdaptiveMotionState : int {
    Stable = 0,
    Micro,
    Moving,
    Accelerating,
    Fast,
    Decelerating,
    Reversing,
    Settling,
};

struct AdaptiveResponseTelemetry {
    float physical = 0.0F;
    float estimated = 0.0F;
    float predicted = 0.0F;
    float velocity = 0.0F;
    float acceleration = 0.0F;
    float activeHorizonSeconds = 0.0F;
    float lead = 0.0F;
    float confidence = 0.0F;
    float motionIntensity = 0.0F;
    float motionCoherence = 0.0F;
    float sourceUpdatePeriodSeconds = 0.0F;
    float quietDurationSeconds = 0.0F;
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    float sourceStoppingSeconds = 0.0F;
    float brakingReductionFactor = 0.0F;
#endif
    AdaptiveMotionState state = AdaptiveMotionState::Stable;
    bool reversal = false;
    bool safetyLimited = false;
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    bool sourceBrakingDetected = false;
#endif
};

// This exists only in the standalone Pattern-73 investigation executable.
// The production targets do not define HOTAS_ADAPTIVE_DIAGNOSTIC_TRACE, so
// their estimator layout and per-report path are unchanged.
#if defined(HOTAS_ADAPTIVE_DIAGNOSTIC_TRACE)
struct AdaptiveResponseDiagnosticSnapshot {
    float normalizedMeasurement = 0.0F;
    float previousPhysical = 0.0F;
    float measuredDelta = 0.0F;
    float dtSeconds = 0.0F;
    float directionalTrend = 0.0F;
    float absoluteTrend = 0.0F;
    float trendAgeSeconds = 0.0F;
    float trendVelocity = 0.0F;
    float meaningfulDelta = 0.0F;
    float holdConfidence = 1.0F;
    float holdRatio = 1.0F;
    float expectedHoldSeconds = 0.0F;
    float graceSeconds = 0.0F;
    float settleSeconds = 0.0F;
    float sourceUpdatePeriodSeconds = 0.0F;
    float quietDurationSeconds = 0.0F;
    float minimumReversalSpeed = 0.0F;
    float softReversalMotion = 0.0F;
    float hardReversalDisplacement = 0.0F;
    float softReversalThreshold = 0.0F;
    float brakingReductionTarget = 0.0F;
    float brakingReductionFactor = 0.0F;
    float rawLead = 0.0F;
    float rawPredicted = 0.0F;
    int rawDirection = 0;
    int motionDirectionBefore = 0;
    int motionDirectionAfter = 0;
    std::uint8_t oppositeEvidenceCount = 0;
    std::uint8_t sourceDecayEvidence = 0;
    bool directMeaningful = false;
    bool accumulatedMeaningful = false;
    bool meaningfulMeasurement = false;
    bool sourceUpdated = false;
    bool oppositeRawDirection = false;
    bool hardReversal = false;
    bool softReversal = false;
    bool safetyCancellation = false;
    bool trustedMeasurement = false;
    bool decelerating = false;
    bool braking = false;
    bool sourceBrakingDetected = false;
};
#endif

// Runtime-only, allocation-free per-axis estimator state.  The Alpha-Beta
// variants own their own state estimate, but their position is telemetry and
// motion-estimation data only: the mapper always applies predictive lead to
// the current measured physical position.
class AdaptiveResponseProcessor final {
public:
    void reset();
    AdaptiveResponseTelemetry process(float physical,
                                      const RuntimeAdaptiveResponseConfig &configuration,
                                      std::chrono::steady_clock::time_point timestamp);
    std::uint64_t reversalCount() const { return m_reversalCount; }
    std::uint64_t safetyClampCount() const { return m_safetyClampCount; }
#if defined(HOTAS_ADAPTIVE_DIAGNOSTIC_TRACE)
    const AdaptiveResponseDiagnosticSnapshot &diagnosticSnapshot() const { return m_diagnosticSnapshot; }
#endif

private:
    bool m_initialized = false;
    float m_lastPhysical = 0.0F;
    float m_lastMeaningfulPhysical = 0.0F;
    float m_estimatedPosition = 0.0F;
    float m_velocity = 0.0F;
    float m_acceleration = 0.0F;
    float m_directionalTrend = 0.0F;
    float m_absoluteTrend = 0.0F;
    float m_trendAgeSeconds = 0.0F;
    float m_sourceUpdatePeriodSeconds = 0.008F;
    float m_quietDurationSeconds = 0.0F;
    float m_holdConfidence = 1.0F;
    float m_softReversalMotion = 0.0F;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
    float m_lastAcceptedSourcePhysical = 0.0F;
    float m_lastAcceptedSourceDeltaMagnitude = 0.0F;
    float m_lastAcceptedSourceInterval = 0.0F;
    int m_acceptedSourceDirection = 0;
#endif
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 2
    std::uint8_t m_sourceDecayEvidence = 0;
#endif
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
    float m_brakingReferenceSourceSpeed = 0.0F;
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    float m_sourceStoppingSeconds = 0.0F;
#endif
    float m_brakingReductionTarget = 0.0F;
    float m_brakingReductionFactor = 0.0F;
#endif
    std::uint8_t m_oppositeEvidenceCount = 0;
    int m_motionDirection = 0;
    std::chrono::steady_clock::time_point m_lastTimestamp{};
    std::chrono::steady_clock::time_point m_lastMeaningfulTimestamp{};
    std::chrono::steady_clock::time_point m_lastSourceTimestamp{};
    std::uint64_t m_reversalCount = 0;
    std::uint64_t m_safetyClampCount = 0;
#if defined(HOTAS_ADAPTIVE_DIAGNOSTIC_TRACE)
    AdaptiveResponseDiagnosticSnapshot m_diagnosticSnapshot{};
#endif
};

QString adaptiveResponseModelKey(AdaptiveResponseModel model);
AdaptiveResponseModel adaptiveResponseModelFromKey(const QString &value);
QString adaptiveMotionStateLabel(AdaptiveMotionState state);

AdaptiveResponseSettings sanitizedAdaptiveResponseSettings(AdaptiveResponseSettings settings);
void applyAdaptiveResponseOverride(AdaptiveResponseSettings &target,
                                   const AdaptiveResponseAxisOverride &override);
const AdaptiveResponsePreset *findAdaptiveResponsePreset(
    const MapperConfiguration &configuration, const QString &id);
const std::array<AdaptiveResponsePreset, 6> &builtInAdaptiveResponsePresets();
RuntimeAdaptiveResponseConfig resolveAdaptiveResponseConfiguration(
    const MapperConfiguration &configuration, const ControllerProfile &profile, int axis);
RuntimeAdaptiveResponseConfig applyAdaptiveResponseRuntimeOverride(
    RuntimeAdaptiveResponseConfig base, const RuntimeAdaptiveResponseOverride &override);

// Repeatable previews/Test Lab reuse the exact runtime estimator but never
// touch a live worker or telemetry buffer.
struct AdaptiveResponseSimulationSample {
    float timeSeconds = 0.0F;
    AdaptiveResponseTelemetry telemetry;
};
using AdaptiveResponseSimulation = std::vector<AdaptiveResponseSimulationSample>;
AdaptiveResponseSimulation simulateAdaptiveResponse(const RuntimeAdaptiveResponseConfig &configuration,
                                                     const std::vector<float> &physicalSamples,
                                                     float samplePeriodSeconds = 0.004F);

} // namespace hotas
