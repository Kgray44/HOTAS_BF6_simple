#pragma once

#include "mapping_types.h"

#include <array>
#include <chrono>

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
    float velocityAuthority = 0.0F;
    // Bounded decomposition of predictive authority. These fixed scalars are
    // published for preview/diagnostics only; they do not allocate or notify.
    float accelerationIntent = 0.0F;
    float onsetAuthority = 0.0F;
    float sustainedEvidence = 0.0F;
    float sustainedAuthority = 0.0F;
    float motionUrgency = 0.0F;
    float horizonExtensionEligibility = 0.0F;
    float normalMaximumHorizonSeconds = 0.0F;
    float allowedMaximumHorizonSeconds = 0.0F;
    float turningPointConfidence = 0.0F;
    float estimatedTimeToTurnSeconds = 0.0F;
    float estimatedRemainingTravel = 0.0F;
    float turningPointHorizonLimitSeconds = 0.0F;
    float turningPointLeadLimit = 0.0F;
    float reacquisitionAuthority = 1.0F;
    float motionCoherence = 0.0F;
    float sourceUpdatePeriodSeconds = 0.0F;
    float quietDurationSeconds = 0.0F;
    // Source-speed braking is production behavior. These remain scalar
    // telemetry for preview and deterministic verification; they do not
    // allocate, notify, or change mapping-thread ownership.
    float sourceStoppingSeconds = 0.0F;
    float brakingReductionFactor = 0.0F;
    AdaptiveMotionState state = AdaptiveMotionState::Stable;
    bool reversal = false;
    bool safetyCancelled = false;
    bool safetyLimited = false;
    bool sourceBrakingDetected = false;
};

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
    float m_lastAcceptedSourcePhysical = 0.0F;
    float m_lastAcceptedSourceDeltaMagnitude = 0.0F;
    float m_lastAcceptedSourceInterval = 0.0F;
    int m_acceptedSourceDirection = 0;
    std::uint8_t m_sourceDecayEvidence = 0;
    float m_brakingReferenceSourceSpeed = 0.0F;
    float m_sourceStoppingSeconds = 0.0F;
    float m_brakingReductionTarget = 0.0F;
    float m_brakingReductionFactor = 0.0F;
    // Confirmed reversals retain immediate stale-lead cancellation, while the
    // new direction's predictive authority rises over a very short envelope.
    float m_reacquisitionAuthority = 1.0F;
    float m_onsetAuthority = 0.0F;
    float m_sustainedEvidence = 0.0F;
    float m_horizonExtensionAuthority = 0.0F;
    // Turning protection has an authority envelope of its own. It preserves
    // a credible constraint across ordinary detector flicker, but releases
    // only after a real renewed acceleration, reversal, or quiet state.
    float m_turningPointAuthority = 0.0F;
    float m_turningPointHorizonLimitSeconds = 0.060F;
    float m_turningPointLeadLimit = 0.50F;
    std::uint8_t m_oppositeEvidenceCount = 0;
    int m_motionDirection = 0;
    std::chrono::steady_clock::time_point m_lastTimestamp{};
    std::chrono::steady_clock::time_point m_lastMeaningfulTimestamp{};
    std::chrono::steady_clock::time_point m_lastSourceTimestamp{};
    std::uint64_t m_reversalCount = 0;
    std::uint64_t m_safetyClampCount = 0;
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

// Static Preview and Test Lab input is generated in C++, never by the QML
// renderer. Keeping the authored trace beside the production simulator makes
// every diagnostic view consume the same deterministic source samples.
std::vector<float> adaptiveResponseScenarioPhysicalSamples(const QString &scenario,
                                                            float domainMinimum,
                                                            float domainMaximum);

} // namespace hotas
