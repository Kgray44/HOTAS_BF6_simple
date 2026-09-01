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
    AdaptiveMotionState state = AdaptiveMotionState::Stable;
    bool reversal = false;
    bool safetyLimited = false;
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
    float m_estimatedPosition = 0.0F;
    float m_velocity = 0.0F;
    float m_acceleration = 0.0F;
    std::chrono::steady_clock::time_point m_lastTimestamp{};
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

} // namespace hotas
