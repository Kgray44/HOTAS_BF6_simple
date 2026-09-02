#include "adaptive_response.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hotas {
namespace {

constexpr float kMinimumPredictionDeltaSeconds = 0.0005F;
constexpr float kMaximumPredictionDeltaSeconds = 0.050F;
constexpr float kMaximumEstimatedVelocity = 64.0F;
constexpr float kMaximumEstimatedAcceleration = 4096.0F;
constexpr float kMotionEvidenceWindowSeconds = 0.060F;
constexpr float kMinimumMeaningfulDelta = 0.00015F;
constexpr float kMinimumExpectedHoldSeconds = 0.004F;
constexpr float kMaximumSourceUpdatePeriodSeconds = 0.050F;
constexpr float kBrakingAttackSeconds = 0.024F;
constexpr float kBrakingReleaseSeconds = 0.032F;

#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 2
constexpr float kSourceSpeedDecayRatio = 0.015F;
constexpr float kSourceBrakingCoherence = 0.62F;
#endif

#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
float smootherstep(float lower, float upper, float value)
{
    const float normalized = std::clamp((value - lower) / std::max(0.000001F, upper - lower), 0.0F, 1.0F);
    return normalized * normalized * normalized * (normalized * (normalized * 6.0F - 15.0F) + 10.0F);
}
#endif

int directionOf(float value, float threshold = 0.0F)
{
    return value > threshold ? 1 : value < -threshold ? -1 : 0;
}

bool hasProperty(const AdaptiveResponseAxisOverride &override, AdaptiveResponseProperty property)
{
    return (override.properties & static_cast<std::uint32_t>(property)) != 0;
}

void applyProperties(AdaptiveResponseSettings &target, const AdaptiveResponseSettings &source,
                     std::uint32_t properties)
{
    const auto enabled = static_cast<std::uint32_t>(AdaptiveResponseEnabled);
    const auto model = static_cast<std::uint32_t>(AdaptiveResponseModelProperty);
    const auto horizon = static_cast<std::uint32_t>(AdaptiveResponseMaximumHorizon);
    const auto lead = static_cast<std::uint32_t>(AdaptiveResponseMaximumLead);
    const auto velocity = static_cast<std::uint32_t>(AdaptiveResponseVelocityResponse);
    const auto acceleration = static_cast<std::uint32_t>(AdaptiveResponseAccelerationResponse);
    const auto sensitivity = static_cast<std::uint32_t>(AdaptiveResponseMotionSensitivity);
    const auto noise = static_cast<std::uint32_t>(AdaptiveResponseNoiseRejection);
    const auto reversalDetection = static_cast<std::uint32_t>(AdaptiveResponseReversalDetection);
    const auto reversalResponse = static_cast<std::uint32_t>(AdaptiveResponseReversalResponse);
    const auto deceleration = static_cast<std::uint32_t>(AdaptiveResponseDecelerationResponse);
    const auto settling = static_cast<std::uint32_t>(AdaptiveResponseSettlingResponse);
    const auto endpoint = static_cast<std::uint32_t>(AdaptiveResponseEndpointTaper);
    if (properties & enabled) target.enabled = source.enabled;
    if (properties & model) target.model = source.model;
    if (properties & horizon) target.maximumHorizonMs = source.maximumHorizonMs;
    if (properties & lead) target.maximumLead = source.maximumLead;
    if (properties & velocity) target.velocityResponse = source.velocityResponse;
    if (properties & acceleration) target.accelerationResponse = source.accelerationResponse;
    if (properties & sensitivity) target.motionSensitivity = source.motionSensitivity;
    if (properties & noise) target.noiseRejection = source.noiseRejection;
    if (properties & reversalDetection) target.reversalDetection = source.reversalDetection;
    if (properties & reversalResponse) target.reversalResponse = source.reversalResponse;
    if (properties & deceleration) target.decelerationResponse = source.decelerationResponse;
    if (properties & settling) target.settlingResponse = source.settlingResponse;
    if (properties & endpoint) target.endpointTaper = source.endpointTaper;
}

AdaptiveResponseAxisOverride completeOverride(const AdaptiveResponseSettings &settings)
{
    AdaptiveResponseAxisOverride result;
    result.properties = kAdaptiveResponseAllProperties;
    result.settings = settings;
    return result;
}

AdaptiveResponsePreset makeBuiltInPreset(const QString &id, const QString &name,
                                         const QString &description,
                                         AdaptiveResponseSettings settings)
{
    AdaptiveResponsePreset result;
    result.id = id;
    result.name = name;
    result.description = description;
    result.builtIn = true;
    result.axes.fill(completeOverride(sanitizedAdaptiveResponseSettings(settings)));
    return result;
}

const AdaptiveResponsePreset *findBuiltInPreset(const QString &id)
{
    const auto &presets = builtInAdaptiveResponsePresets();
    const auto found = std::find_if(presets.cbegin(), presets.cend(), [&id](const auto &preset) {
        return preset.id == id;
    });
    return found == presets.cend() ? nullptr : &*found;
}

void applyLayer(AdaptiveResponseSettings &target, const AdaptiveResponseLayer &layer,
                const MapperConfiguration &configuration, int axis)
{
    if (axis < 0 || axis >= kPhysicalAxisCount) return;
    const AdaptiveResponseAxisOverride &entry = layer.axes[static_cast<size_t>(axis)];
    if (!entry.presetId.trimmed().isEmpty()) {
        if (const AdaptiveResponsePreset *preset = findAdaptiveResponsePreset(configuration, entry.presetId)) {
            const AdaptiveResponseAxisOverride &presetAxis = preset->axes[static_cast<size_t>(axis)];
            applyProperties(target, presetAxis.settings, presetAxis.properties);
        }
    }
    applyProperties(target, entry.settings, entry.properties);
}

float clampFinite(float value, float minimum, float maximum, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

} // namespace

QString adaptiveResponseModelKey(AdaptiveResponseModel model)
{
    switch (model) {
    case AdaptiveResponseModel::Auto: return u"auto"_qs;
    case AdaptiveResponseModel::Velocity: return u"velocity"_qs;
    case AdaptiveResponseModel::AlphaBeta: return u"alpha-beta"_qs;
    case AdaptiveResponseModel::AlphaBetaGamma: return u"alpha-beta-gamma"_qs;
    }
    return u"auto"_qs;
}

AdaptiveResponseModel adaptiveResponseModelFromKey(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == u"velocity"_qs) return AdaptiveResponseModel::Velocity;
    if (normalized == u"alpha-beta"_qs || normalized == u"alphabeta"_qs) return AdaptiveResponseModel::AlphaBeta;
    if (normalized == u"alpha-beta-gamma"_qs || normalized == u"abg"_qs
        || normalized == u"alphabetagamma"_qs) return AdaptiveResponseModel::AlphaBetaGamma;
    return AdaptiveResponseModel::Auto;
}

QString adaptiveMotionStateLabel(AdaptiveMotionState state)
{
    switch (state) {
    case AdaptiveMotionState::Stable: return u"Stable"_qs;
    case AdaptiveMotionState::Micro: return u"Micro"_qs;
    case AdaptiveMotionState::Moving: return u"Moving"_qs;
    case AdaptiveMotionState::Accelerating: return u"Accelerating"_qs;
    case AdaptiveMotionState::Fast: return u"Fast"_qs;
    case AdaptiveMotionState::Decelerating: return u"Decelerating"_qs;
    case AdaptiveMotionState::Reversing: return u"Reversing"_qs;
    case AdaptiveMotionState::Settling: return u"Settling"_qs;
    }
    return u"Stable"_qs;
}

AdaptiveResponseSettings sanitizedAdaptiveResponseSettings(AdaptiveResponseSettings settings)
{
    settings.maximumHorizonMs = clampFinite(settings.maximumHorizonMs, 0.0F, 30.0F, 8.0F);
    settings.maximumLead = clampFinite(settings.maximumLead, 0.001F, 0.50F, 0.12F);
    settings.velocityResponse = clampFinite(settings.velocityResponse, 0.0F, 1.0F, 0.72F);
    settings.accelerationResponse = clampFinite(settings.accelerationResponse, 0.0F, 1.0F, 0.58F);
    settings.motionSensitivity = clampFinite(settings.motionSensitivity, 0.001F, 2.0F, 0.035F);
    settings.noiseRejection = clampFinite(settings.noiseRejection, 0.0F, 0.50F, 0.012F);
    settings.reversalDetection = clampFinite(settings.reversalDetection, 0.001F, 10.0F, 0.075F);
    settings.reversalResponse = clampFinite(settings.reversalResponse, 0.0F, 1.0F, 1.0F);
    settings.decelerationResponse = clampFinite(settings.decelerationResponse, 0.0F, 1.0F, 0.85F);
    settings.settlingResponse = clampFinite(settings.settlingResponse, 0.0F, 1.0F, 0.92F);
    settings.endpointTaper = clampFinite(settings.endpointTaper, 0.01F, 1.0F, 0.16F);
    return settings;
}

void applyAdaptiveResponseOverride(AdaptiveResponseSettings &target,
                                   const AdaptiveResponseAxisOverride &override)
{
    applyProperties(target, override.settings, override.properties);
    target = sanitizedAdaptiveResponseSettings(target);
}

const std::array<AdaptiveResponsePreset, 6> &builtInAdaptiveResponsePresets()
{
    static const std::array<AdaptiveResponsePreset, 6> presets = [] {
        AdaptiveResponseSettings off;
        off.enabled = false;
        AdaptiveResponseSettings light;
        light.enabled = true; light.maximumHorizonMs = 4.0F; light.maximumLead = 0.055F;
        light.velocityResponse = 0.56F; light.accelerationResponse = 0.35F;
        AdaptiveResponseSettings balanced;
        balanced.enabled = true; balanced.maximumHorizonMs = 8.0F; balanced.maximumLead = 0.12F;
        AdaptiveResponseSettings fast = balanced;
        fast.maximumHorizonMs = 12.0F; fast.maximumLead = 0.18F;
        fast.velocityResponse = 0.80F; fast.accelerationResponse = 0.68F;
        AdaptiveResponseSettings aggressive = fast;
        aggressive.maximumHorizonMs = 18.0F; aggressive.maximumLead = 0.27F;
        aggressive.velocityResponse = 0.91F; aggressive.accelerationResponse = 0.82F;
        AdaptiveResponseSettings extreme = aggressive;
        extreme.maximumHorizonMs = 30.0F; extreme.maximumLead = 0.40F;
        extreme.velocityResponse = 1.0F; extreme.accelerationResponse = 0.95F;
        return std::array<AdaptiveResponsePreset, 6>{
            makeBuiltInPreset(u"off"_qs, u"Off"_qs, u"Direct physical response with no prediction."_qs, off),
            makeBuiltInPreset(u"light"_qs, u"Light"_qs, u"Subtle, low-lead movement anticipation."_qs, light),
            makeBuiltInPreset(u"balanced"_qs, u"Balanced"_qs, u"Responsive default for general flight."_qs, balanced),
            makeBuiltInPreset(u"fast"_qs, u"Fast"_qs, u"More immediate lead for decisive movement."_qs, fast),
            makeBuiltInPreset(u"aggressive"_qs, u"Aggressive"_qs, u"High-response tuning for evasive control."_qs, aggressive),
            makeBuiltInPreset(u"extreme"_qs, u"Extreme"_qs,
                              u"Experimental — up to 30 ms adaptive prediction for advanced tuning."_qs, extreme),
        };
    }();
    return presets;
}

const AdaptiveResponsePreset *findAdaptiveResponsePreset(
    const MapperConfiguration &configuration, const QString &id)
{
    const QString sought = id.trimmed();
    if (sought.isEmpty()) return nullptr;
    if (const AdaptiveResponsePreset *builtIn = findBuiltInPreset(sought)) return builtIn;
    const auto found = std::find_if(configuration.adaptiveResponsePresets.cbegin(),
        configuration.adaptiveResponsePresets.cend(), [&sought](const auto &preset) {
            return preset.id == sought;
        });
    return found == configuration.adaptiveResponsePresets.cend() ? nullptr : &*found;
}

RuntimeAdaptiveResponseConfig resolveAdaptiveResponseConfiguration(
    const MapperConfiguration &configuration, const ControllerProfile &profile, int axis)
{
    AdaptiveResponseSettings settings;
    applyLayer(settings, configuration.adaptiveResponseGlobal, configuration, axis);
    if (const ProfileCategory *category = findProfileCategory(configuration, profile.categoryId)) {
        applyLayer(settings, category->adaptiveResponse, configuration, axis);
    }
    applyLayer(settings, profile.adaptiveResponse, configuration, axis);
    settings = sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig runtime;
    runtime.enabled = settings.enabled && settings.maximumHorizonMs > 0.0F;
    runtime.model = settings.model;
    runtime.maximumHorizonSeconds = settings.maximumHorizonMs / 1000.0F;
    runtime.maximumLead = settings.maximumLead;
    runtime.velocityResponse = settings.velocityResponse;
    runtime.accelerationResponse = settings.accelerationResponse;
    runtime.motionSensitivity = settings.motionSensitivity;
    runtime.noiseRejection = settings.noiseRejection;
    runtime.reversalDetection = settings.reversalDetection;
    runtime.reversalResponse = settings.reversalResponse;
    runtime.decelerationResponse = settings.decelerationResponse;
    runtime.settlingResponse = settings.settlingResponse;
    runtime.endpointTaper = settings.endpointTaper;
    const AxisMapping &mapping = profile.axes[static_cast<size_t>(std::clamp(axis, 0, kPhysicalAxisCount - 1))];
    runtime.domainMinimum = mapping.rangeMode == AxisRangeMode::OneSided ? 0.0F : -1.0F;
    runtime.domainMaximum = 1.0F;
    return runtime;
}

RuntimeAdaptiveResponseConfig applyAdaptiveResponseRuntimeOverride(
    RuntimeAdaptiveResponseConfig base, const RuntimeAdaptiveResponseOverride &override)
{
    if (!override.active || override.properties == 0) return base;
    AdaptiveResponseSettings settings;
    settings.enabled = base.enabled;
    settings.model = base.model;
    settings.maximumHorizonMs = base.maximumHorizonSeconds * 1000.0F;
    settings.maximumLead = base.maximumLead;
    settings.velocityResponse = base.velocityResponse;
    settings.accelerationResponse = base.accelerationResponse;
    settings.motionSensitivity = base.motionSensitivity;
    settings.noiseRejection = base.noiseRejection;
    settings.reversalDetection = base.reversalDetection;
    settings.reversalResponse = base.reversalResponse;
    settings.decelerationResponse = base.decelerationResponse;
    settings.settlingResponse = base.settlingResponse;
    settings.endpointTaper = base.endpointTaper;
    applyProperties(settings, override.settings, override.properties);
    settings = sanitizedAdaptiveResponseSettings(settings);
    base.enabled = settings.enabled && settings.maximumHorizonMs > 0.0F;
    base.model = settings.model;
    base.maximumHorizonSeconds = settings.maximumHorizonMs / 1000.0F;
    base.maximumLead = settings.maximumLead;
    base.velocityResponse = settings.velocityResponse;
    base.accelerationResponse = settings.accelerationResponse;
    base.motionSensitivity = settings.motionSensitivity;
    base.noiseRejection = settings.noiseRejection;
    base.reversalDetection = settings.reversalDetection;
    base.reversalResponse = settings.reversalResponse;
    base.decelerationResponse = settings.decelerationResponse;
    base.settlingResponse = settings.settlingResponse;
    base.endpointTaper = settings.endpointTaper;
    return base;
}

void AdaptiveResponseProcessor::reset()
{
    m_initialized = false;
    m_lastPhysical = 0.0F;
    m_lastMeaningfulPhysical = 0.0F;
    m_estimatedPosition = 0.0F;
    m_velocity = 0.0F;
    m_acceleration = 0.0F;
    m_directionalTrend = 0.0F;
    m_absoluteTrend = 0.0F;
    m_trendAgeSeconds = 0.0F;
    m_sourceUpdatePeriodSeconds = 0.008F;
    m_quietDurationSeconds = 0.0F;
    m_holdConfidence = 1.0F;
    m_softReversalMotion = 0.0F;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
    m_lastAcceptedSourcePhysical = 0.0F;
    m_lastAcceptedSourceDeltaMagnitude = 0.0F;
    m_lastAcceptedSourceInterval = 0.0F;
    m_acceptedSourceDirection = 0;
#endif
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 2
    m_sourceDecayEvidence = 0;
#endif
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
    m_brakingReferenceSourceSpeed = 0.0F;
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    m_sourceStoppingSeconds = 0.0F;
#endif
    m_brakingReductionTarget = 0.0F;
    m_brakingReductionFactor = 0.0F;
#endif
    m_oppositeEvidenceCount = 0;
    m_motionDirection = 0;
    m_lastTimestamp = {};
    m_lastMeaningfulTimestamp = {};
    m_lastSourceTimestamp = {};
    m_reversalCount = 0;
    m_safetyClampCount = 0;
}

AdaptiveResponseTelemetry AdaptiveResponseProcessor::process(
    float physical, const RuntimeAdaptiveResponseConfig &configuration,
    std::chrono::steady_clock::time_point timestamp)
{
    AdaptiveResponseTelemetry result;
    physical = std::isfinite(physical) ? std::clamp(physical, configuration.domainMinimum,
                                                      configuration.domainMaximum)
                                        : 0.0F;
    result.physical = physical;
    if (!configuration.enabled) {
        reset();
        result.estimated = physical;
        result.predicted = physical;
        return result;
    }
    if (!m_initialized) {
        m_initialized = true;
        m_lastPhysical = physical;
        m_lastMeaningfulPhysical = physical;
        m_estimatedPosition = physical;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
        m_lastAcceptedSourcePhysical = physical;
#endif
        m_lastTimestamp = timestamp;
        m_lastMeaningfulTimestamp = timestamp;
        m_lastSourceTimestamp = timestamp;
        result.estimated = physical;
        result.predicted = physical;
        result.sourceUpdatePeriodSeconds = m_sourceUpdatePeriodSeconds;
        return result;
    }
    float dt = static_cast<float>(std::chrono::duration<double>(timestamp - m_lastTimestamp).count());
    dt = std::clamp(dt, kMinimumPredictionDeltaSeconds, kMaximumPredictionDeltaSeconds);
    const float measuredDelta = physical - m_lastPhysical;
    const float absoluteDelta = std::abs(measuredDelta);
    const float instantaneousVelocity = measuredDelta / dt;

    // Fixed-size, time-normalized motion evidence. A short directional trend
    // lets coherent slow movement survive high mapper rates, while alternating
    // jitter cancels itself without a per-report history or allocation.
    const float evidenceDecay = std::clamp(1.0F - dt / kMotionEvidenceWindowSeconds, 0.0F, 1.0F);
    m_directionalTrend = m_directionalTrend * evidenceDecay + measuredDelta;
    m_absoluteTrend = m_absoluteTrend * evidenceDecay + absoluteDelta;
    m_trendAgeSeconds = std::min(kMotionEvidenceWindowSeconds, m_trendAgeSeconds + dt);
    const float coherence = std::clamp(std::abs(m_directionalTrend)
            / std::max(0.000001F, m_absoluteTrend), 0.0F, 1.0F);
    const float trendVelocity = m_directionalTrend / std::max(dt, m_trendAgeSeconds);
    const float meaningfulDelta = std::max(kMinimumMeaningfulDelta,
        std::max(configuration.noiseRejection * 0.50F, configuration.motionSensitivity * 0.015F));
    const bool directMeaningful = absoluteDelta > std::max(kMinimumMeaningfulDelta,
                                                            configuration.noiseRejection);
    const bool accumulatedMeaningful = std::abs(physical - m_lastMeaningfulPhysical)
            >= meaningfulDelta && coherence >= 0.58F;
    const bool meaningfulMeasurement = directMeaningful || accumulatedMeaningful;
    const float meaningfulDeltaSeconds = meaningfulMeasurement ? std::clamp(static_cast<float>(
        std::chrono::duration<double>(timestamp - m_lastMeaningfulTimestamp).count()),
        kMinimumPredictionDeltaSeconds, kMaximumPredictionDeltaSeconds) : dt;
    const bool sourceUpdated = absoluteDelta >= kMinimumMeaningfulDelta * 0.10F;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
    float acceptedSourceInterval = 0.0F;
#endif
    if (sourceUpdated) {
        const float sourceInterval = std::clamp(static_cast<float>(
            std::chrono::duration<double>(timestamp - m_lastSourceTimestamp).count()),
            kMinimumPredictionDeltaSeconds, kMaximumSourceUpdatePeriodSeconds);
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
        acceptedSourceInterval = sourceInterval;
#endif
        m_sourceUpdatePeriodSeconds += (sourceInterval - m_sourceUpdatePeriodSeconds) * 0.45F;
        m_sourceUpdatePeriodSeconds = std::clamp(m_sourceUpdatePeriodSeconds,
            kMinimumPredictionDeltaSeconds, kMaximumSourceUpdatePeriodSeconds);
        m_lastSourceTimestamp = timestamp;
        m_quietDurationSeconds = 0.0F;
        m_holdConfidence = 1.0F;
    } else {
        m_quietDurationSeconds += dt;
    }
    if (meaningfulMeasurement) {
        m_lastMeaningfulPhysical = physical;
        m_lastMeaningfulTimestamp = timestamp;
    }

    // Sample-and-hold sources are expected to repeat a physical value. Coast
    // through the learned cadence, decay only after a grace period, then
    // settle exactly to the physical baseline after confirmed quiet.
    const float expectedHoldSeconds = std::max(kMinimumExpectedHoldSeconds,
        m_sourceUpdatePeriodSeconds * 1.25F);
    const float graceSeconds = std::max(0.012F, m_sourceUpdatePeriodSeconds * 0.75F);
    const float settleSeconds = std::max(0.020F, m_sourceUpdatePeriodSeconds * 0.90F);
    float holdConfidence = 1.0F;
    if (!sourceUpdated && m_quietDurationSeconds > expectedHoldSeconds) {
        const float uncertain = std::clamp((m_quietDurationSeconds - expectedHoldSeconds)
            / graceSeconds, 0.0F, 1.0F);
        holdConfidence = 1.0F - uncertain * 0.55F;
        if (m_quietDurationSeconds > expectedHoldSeconds + graceSeconds) {
            holdConfidence *= std::clamp(1.0F - (m_quietDurationSeconds - expectedHoldSeconds
                - graceSeconds) / settleSeconds, 0.0F, 1.0F);
        }
    }
    const float holdRatio = sourceUpdated ? 1.0F
        : holdConfidence / std::max(0.0001F, m_holdConfidence);
    if (!sourceUpdated) {
        m_velocity *= holdRatio;
        m_acceleration *= holdRatio;
    }
    m_holdConfidence = holdConfidence;
    const bool confirmedQuiet = !sourceUpdated && m_quietDurationSeconds
        > expectedHoldSeconds + graceSeconds + settleSeconds;

    const float priorVelocity = m_velocity;
    const float velocityThreshold = std::max(0.002F, configuration.motionSensitivity * 0.10F);
    const int rawDirection = directionOf(measuredDelta, kMinimumMeaningfulDelta * 0.25F);
    if (m_motionDirection == 0) m_motionDirection = directionOf(priorVelocity, velocityThreshold);
    const bool oppositeRawDirection = rawDirection != 0 && m_motionDirection != 0
        && rawDirection != m_motionDirection;
    if (oppositeRawDirection && absoluteDelta >= kMinimumMeaningfulDelta) {
        m_softReversalMotion += absoluteDelta;
        m_oppositeEvidenceCount = std::min<std::uint8_t>(
            std::numeric_limits<std::uint8_t>::max(), m_oppositeEvidenceCount + 1);
    } else if (rawDirection != 0 && rawDirection == m_motionDirection) {
        m_softReversalMotion *= 0.15F;
        m_oppositeEvidenceCount = 0;
    } else if (rawDirection == 0) {
        m_softReversalMotion *= evidenceDecay;
        m_oppositeEvidenceCount = 0;
    }
    const float minimumReversalSpeed = std::max(configuration.reversalDetection,
                                                 configuration.motionSensitivity * 0.50F);
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 1
    // V2.3.T profiling levels: 1 records accepted-source state; 2 adds the
    // coherent two-update detector; 3 applies the proven smooth envelope.
    if (sourceUpdated) {
        const float acceptedSourceDelta = physical - m_lastAcceptedSourcePhysical;
        const float acceptedSourceDeltaMagnitude = std::abs(acceptedSourceDelta);
        const int observedSourceDirection = directionOf(acceptedSourceDelta, kMinimumMeaningfulDelta);
        // A tiny accepted source update is useful for cadence/quiet tracking,
        // but its sign is not trustworthy. It must not erase the coherent
        // direction and stop evidence that carried us into a turning point.
        const int acceptedSourceDirection = observedSourceDirection == 0
            ? m_acceptedSourceDirection : observedSourceDirection;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 2
        const float sourceSpeedFloor = minimumReversalSpeed;
        const bool sameDirection = observedSourceDirection != 0 && m_motionDirection != 0
            && acceptedSourceDirection == m_motionDirection
            && (m_acceptedSourceDirection == 0 || m_acceptedSourceDirection == acceptedSourceDirection);
        const bool sourceSpeedAboveFloor = acceptedSourceDeltaMagnitude
            >= sourceSpeedFloor * acceptedSourceInterval;
        const bool priorSourceSpeedAboveFloor = m_lastAcceptedSourceDeltaMagnitude
            >= sourceSpeedFloor * m_lastAcceptedSourceInterval;
        const float currentScaledSpeed = acceptedSourceDeltaMagnitude * m_lastAcceptedSourceInterval;
        const float priorScaledSpeed = m_lastAcceptedSourceDeltaMagnitude * acceptedSourceInterval;
        const bool meaningfulDecay = sameDirection && coherence >= kSourceBrakingCoherence
            && sourceSpeedAboveFloor && priorSourceSpeedAboveFloor
            && currentScaledSpeed <= priorScaledSpeed * (1.0F - kSourceSpeedDecayRatio);
        const bool completingConfirmedBrake = sameDirection && coherence >= kSourceBrakingCoherence
            && m_sourceDecayEvidence >= 2 && !sourceSpeedAboveFloor
            && currentScaledSpeed <= priorScaledSpeed;
        if (meaningfulDecay) {
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
            const float acceptedSourceSpeed = acceptedSourceDeltaMagnitude
                / std::max(acceptedSourceInterval, kMinimumPredictionDeltaSeconds);
            const float priorAcceptedSourceSpeed = m_lastAcceptedSourceDeltaMagnitude
                / std::max(m_lastAcceptedSourceInterval, kMinimumPredictionDeltaSeconds);
            if (m_sourceDecayEvidence == 0) m_brakingReferenceSourceSpeed = priorAcceptedSourceSpeed;
#endif
            m_sourceDecayEvidence = std::min<std::uint8_t>(4, m_sourceDecayEvidence + 1);
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
            if (m_sourceDecayEvidence >= 2) {
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
                const float sourceSpeedDrop = std::max(0.0F, priorAcceptedSourceSpeed - acceptedSourceSpeed);
                const float sourceSlope = sourceSpeedDrop / std::max(acceptedSourceInterval,
                                                                       kMinimumPredictionDeltaSeconds);
                m_sourceStoppingSeconds = std::clamp(acceptedSourceSpeed / std::max(sourceSlope, 0.001F),
                                                     0.0F, 0.75F);
#endif
                const float cumulativeDecay = std::clamp((m_brakingReferenceSourceSpeed - acceptedSourceSpeed)
                        / std::max(m_brakingReferenceSourceSpeed, sourceSpeedFloor), 0.0F, 1.0F);
                const float persistence = m_sourceDecayEvidence == 2 ? 0.50F
                    : m_sourceDecayEvidence == 3 ? 0.78F : 1.0F;
                // Reduction follows progress toward the actual physical stop,
                // not merely the first third of a speed decrease. A 50%
                // remaining source speed therefore remains a useful, broad
                // prediction gradient instead of an early guillotine.
                const float nearStopProgress = 0.97F - 0.12F * std::clamp(
                    configuration.settlingResponse, 0.0F, 1.0F);
                m_brakingReductionTarget = smootherstep(0.02F, nearStopProgress, cumulativeDecay)
                    * persistence * configuration.decelerationResponse;
            }
#endif
        } else if (completingConfirmedBrake) {
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
            m_sourceStoppingSeconds = 0.0F;
#endif
            m_brakingReductionTarget = configuration.decelerationResponse;
#endif
        } else if (observedSourceDirection == 0 && m_sourceDecayEvidence >= 2) {
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
            // The source is effectively still at the old-direction stop. Do
            // not create a detector dropout between the last coherent sample
            // and the first credible opposite sample.
            m_brakingReductionTarget = std::max(m_brakingReductionTarget,
                configuration.decelerationResponse);
#endif
        } else if (observedSourceDirection != 0 && acceptedSourceDirection != m_motionDirection) {
            // The first opposite report is safety-cancelled below. Retain the
            // stored envelope until the real reversal boundary resets it, so
            // a briefly indeterminate turn cannot reintroduce stale lead.
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
            m_brakingReductionTarget = 0.0F;
#endif
        } else {
            m_sourceDecayEvidence = 0;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
            m_brakingReferenceSourceSpeed = 0.0F;
#if defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
            m_sourceStoppingSeconds = 0.0F;
#endif
            m_brakingReductionTarget = 0.0F;
#endif
        }
#endif
        m_lastAcceptedSourcePhysical = physical;
        m_lastAcceptedSourceDeltaMagnitude = acceptedSourceDeltaMagnitude;
        m_lastAcceptedSourceInterval = acceptedSourceInterval;
        if (observedSourceDirection != 0) m_acceptedSourceDirection = acceptedSourceDirection;
    }
#endif
    const float hardReversalDisplacement = std::max(configuration.noiseRejection * 2.0F,
        minimumReversalSpeed * dt * 0.50F);
    const bool hardReversal = oppositeRawDirection && m_oppositeEvidenceCount >= 2
        && m_softReversalMotion >= std::max(hardReversalDisplacement,
                                             configuration.noiseRejection * 4.0F)
        && std::abs(priorVelocity) >= minimumReversalSpeed * 0.35F;
    const int trendDirection = directionOf(trendVelocity, minimumReversalSpeed * 0.30F);
    const float softReversalThreshold = std::max(configuration.noiseRejection * 2.5F,
        minimumReversalSpeed * std::max(dt, m_sourceUpdatePeriodSeconds) * 1.25F);
    const bool softReversal = oppositeRawDirection && trendDirection != 0
        && trendDirection != m_motionDirection && coherence >= 0.62F
        && m_softReversalMotion >= softReversalThreshold;
    const bool reversal = hardReversal || softReversal;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3 \
    && defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    bool sourceBrakingDetected = false;
    if (m_sourceDecayEvidence >= 2 && m_brakingReductionTarget > 0.0F && !reversal
        && coherence >= kSourceBrakingCoherence && m_acceptedSourceDirection != 0
        && m_acceptedSourceDirection == m_motionDirection) {
        sourceBrakingDetected = true;
    }
#endif
    // One accepted opposite report is enough to cancel stale lead safely, but
    // never enough to reverse the model. That keeps an isolated sensor spike
    // from commanding the wrong direction while the second coherent report
    // can reacquire a genuine hard reversal immediately.
    const bool safetyCancellation = oppositeRawDirection && !reversal;
    const bool trustedMeasurement = meaningfulMeasurement && (coherence >= 0.55F
        || absoluteDelta >= std::max(kMinimumMeaningfulDelta * 2.0F,
                                     configuration.noiseRejection * 2.0F));

    if (reversal) {
        // Stale-direction cancellation is never optional. Reversal Response
        // only controls how strongly the accepted new trend is reacquired.
        const float reacquiredVelocity = hardReversal ? instantaneousVelocity : trendVelocity;
        const float reacquisition = 0.35F + configuration.reversalResponse * 0.65F;
        m_estimatedPosition = physical;
        m_velocity = reacquiredVelocity * reacquisition;
        m_acceleration = 0.0F;
        m_motionDirection = directionOf(m_velocity, velocityThreshold);
        m_softReversalMotion = 0.0F;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
        // A confirmed new direction owns a new braking envelope. The old one
        // has already cancelled stale lead safely and must not delay the
        // opposite-direction acquisition.
        m_sourceDecayEvidence = 0;
        m_brakingReferenceSourceSpeed = 0.0F;
        m_brakingReductionTarget = 0.0F;
        m_brakingReductionFactor = 0.0F;
#endif
        ++m_reversalCount;
    } else if (trustedMeasurement && configuration.model == AdaptiveResponseModel::Velocity) {
        // Velocity mode deliberately remains a lightweight derivative
        // predictor. Velocity Response controls its actual smoothing gain.
        const float gain = std::clamp(0.06F + configuration.velocityResponse * 0.90F,
                                      0.06F, 0.96F);
        m_velocity += (trendVelocity - m_velocity) * gain;
        m_estimatedPosition = physical;
        m_acceleration = 0.0F;
    } else if (trustedMeasurement) {
        // Proper Alpha-Beta / Alpha-Beta-Gamma estimators. Predict the state,
        // form an innovation against this physical measurement, then correct
        // x/v(/a). Auto keeps the ABG architecture and only adapts gains.
        const bool usesAcceleration = configuration.model == AdaptiveResponseModel::AlphaBetaGamma
            || configuration.model == AdaptiveResponseModel::Auto;
        const float predictedPosition = m_estimatedPosition + m_velocity * meaningfulDeltaSeconds
            + (usesAcceleration ? 0.5F * m_acceleration * meaningfulDeltaSeconds * meaningfulDeltaSeconds : 0.0F);
        const float predictedVelocity = m_velocity
            + (usesAcceleration ? m_acceleration * meaningfulDeltaSeconds : 0.0F);
        const float innovation = physical - predictedPosition;
        const float motion = std::clamp(std::max(std::abs(predictedVelocity),
                                                  std::abs(trendVelocity))
                                             / std::max(0.02F, configuration.motionSensitivity * 18.0F),
                                         0.0F, 1.0F);
        const float adaptation = configuration.model == AdaptiveResponseModel::Auto
            ? 0.35F + motion * 0.65F : 1.0F;
        const float alpha = std::clamp((0.12F + configuration.velocityResponse * 0.68F)
                                           * adaptation, 0.08F, 0.92F);
        const float beta = std::clamp((0.006F + configuration.velocityResponse * 0.095F)
                                          * adaptation, 0.003F, 0.12F);
        m_estimatedPosition = predictedPosition + alpha * innovation;
        m_velocity = predictedVelocity + (beta / meaningfulDeltaSeconds) * innovation;
        m_velocity += (trendVelocity - m_velocity)
            * std::clamp(0.20F + configuration.velocityResponse * 0.30F, 0.20F, 0.50F);
        if (usesAcceleration) {
            const float gammaBase = 0.00025F + configuration.accelerationResponse * 0.0080F;
            const float gamma = std::clamp(gammaBase * adaptation, 0.0001F, 0.010F);
            m_acceleration += (2.0F * gamma / (meaningfulDeltaSeconds * meaningfulDeltaSeconds))
                * innovation;
        } else {
            m_acceleration = 0.0F;
        }
    } else {
        // An unchanged report is not a velocity measurement. Keep the
        // physical baseline direct while gently converging estimator telemetry
        // so a true stop cannot leave a residual reactivation spike.
        const float correction = 0.08F + (1.0F - holdConfidence) * 0.55F;
        m_estimatedPosition += (physical - m_estimatedPosition) * correction;
    }
    m_velocity = std::clamp(m_velocity, -kMaximumEstimatedVelocity, kMaximumEstimatedVelocity);
    m_acceleration = std::clamp(m_acceleration, -kMaximumEstimatedAcceleration,
                                kMaximumEstimatedAcceleration);
    if (confirmedQuiet) {
        m_velocity = 0.0F;
        m_acceleration = 0.0F;
        m_estimatedPosition = physical;
        m_motionDirection = 0;
    }
    const float speed = std::abs(m_velocity);
    const bool decelerating = !reversal && (m_velocity * priorVelocity > 0.0F)
        && speed < std::abs(priorVelocity) * 0.88F;
    const float microCutoff = std::max(configuration.noiseRejection * 0.25F,
                                       configuration.motionSensitivity * 0.50F);
    const float intensity = std::clamp((speed - microCutoff) / (configuration.motionSensitivity * 18.0F),
                                       0.0F, 1.0F);
    const bool settling = !reversal && (decelerating || confirmedQuiet
        || (holdConfidence < 0.45F && !meaningfulMeasurement));
    // Confidence is estimator trust, not a second copy of movement magnitude.
    // Motion intensity is applied exactly once when the horizon is formed.
    float confidence = (0.35F + coherence * 0.65F) * holdConfidence;
    if (reversal) confidence = std::max(confidence, 0.28F + 0.42F * configuration.reversalResponse);
    if (confirmedQuiet) {
        m_velocity = 0.0F;
        m_acceleration = 0.0F;
        confidence = 0.0F;
    }
    float horizon = configuration.maximumHorizonSeconds * intensity * confidence;
    if (reversal) horizon *= 0.35F + configuration.reversalResponse * 0.65F;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3
    // The envelope is a time-normalized first-order response. Its stored
    // value applies continuously, even when an instantaneous detector flickers
    // false between source reports.
    const float brakingTau = m_brakingReductionTarget > m_brakingReductionFactor
        ? kBrakingAttackSeconds : kBrakingReleaseSeconds;
    const float brakingAlpha = dt / (brakingTau + dt);
    m_brakingReductionFactor += (m_brakingReductionTarget - m_brakingReductionFactor) * brakingAlpha;
    m_brakingReductionFactor = std::clamp(m_brakingReductionFactor, 0.0F, 1.0F);
    if (m_brakingReductionFactor > 0.0001F) horizon *= 1.0F - m_brakingReductionFactor;
#endif
    if (safetyCancellation) horizon = 0.0F;
    float lead = m_velocity * horizon;
    if (configuration.model == AdaptiveResponseModel::AlphaBetaGamma
        || configuration.model == AdaptiveResponseModel::Auto) {
        lead += 0.5F * m_acceleration * horizon * horizon * configuration.accelerationResponse;
    }
    const float maximumLead = configuration.maximumLead * std::max(0.10F, confidence);
    const float unclampedLead = lead;
    lead = std::clamp(lead, -maximumLead, maximumLead);
    const float headroom = lead >= 0.0F ? configuration.domainMaximum - physical
                                        : physical - configuration.domainMinimum;
    const float taper = std::clamp(headroom / configuration.endpointTaper, 0.0F, 1.0F);
    lead *= taper;
    float predicted = physical + lead;
    const float bounded = std::clamp(predicted, configuration.domainMinimum, configuration.domainMaximum);
    result.safetyLimited = safetyCancellation || std::abs(unclampedLead - lead) > 0.00001F
        || bounded != predicted;
    if (result.safetyLimited) ++m_safetyClampCount;
    result.estimated = m_estimatedPosition;
    result.predicted = bounded;
    result.velocity = m_velocity;
    result.acceleration = m_acceleration;
    result.activeHorizonSeconds = horizon;
    result.lead = bounded - physical;
    result.confidence = confidence;
    result.motionIntensity = intensity;
    result.motionCoherence = coherence;
    result.sourceUpdatePeriodSeconds = m_sourceUpdatePeriodSeconds;
    result.quietDurationSeconds = m_quietDurationSeconds;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3 \
    && defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    result.sourceStoppingSeconds = m_sourceStoppingSeconds;
    result.brakingReductionFactor = m_brakingReductionFactor;
#endif
    result.reversal = reversal;
#if defined(HOTAS_SPEED_DECAY_GATE_PROFILE_MODE) && HOTAS_SPEED_DECAY_GATE_PROFILE_MODE >= 3 \
    && defined(HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY)
    result.sourceBrakingDetected = sourceBrakingDetected;
#endif
    result.state = reversal ? AdaptiveMotionState::Reversing
        : confirmedQuiet || speed <= velocityThreshold ? AdaptiveMotionState::Stable
        : intensity < 0.16F ? AdaptiveMotionState::Micro
        : settling && intensity < 0.28F ? AdaptiveMotionState::Settling
        : decelerating ? AdaptiveMotionState::Decelerating
        : std::abs(m_acceleration) > speed * 2.5F ? AdaptiveMotionState::Accelerating
        : intensity > 0.74F ? AdaptiveMotionState::Fast : AdaptiveMotionState::Moving;
    m_lastPhysical = physical;
    m_lastTimestamp = timestamp;
    return result;
}

AdaptiveResponseSimulation simulateAdaptiveResponse(const RuntimeAdaptiveResponseConfig &configuration,
                                                     const std::vector<float> &physicalSamples,
                                                     float samplePeriodSeconds)
{
    AdaptiveResponseSimulation samples;
    samples.reserve(physicalSamples.size());
    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    const float period = std::clamp(samplePeriodSeconds, 0.0005F, 0.100F);
    for (size_t index = 0; index < physicalSamples.size(); ++index) {
        const auto at = origin + std::chrono::microseconds(static_cast<long long>(
            static_cast<double>(index) * period * 1000000.0));
        samples.push_back({static_cast<float>(index) * period,
                           processor.process(physicalSamples[index], configuration, at)});
    }
    return samples;
}

std::vector<float> adaptiveResponseScenarioPhysicalSamples(const QString &scenario,
                                                           float domainMinimum, float domainMaximum)
{
    constexpr int kSamples = 211;
    constexpr float kSamplePeriodSeconds = 0.004F;
    const float minimum = std::min(domainMinimum, domainMaximum);
    const float maximum = std::max(domainMinimum, domainMaximum);
    const float span = maximum - minimum;
    const QString mode = scenario.trimmed().toCaseFolded();
    std::vector<float> physical;
    physical.reserve(kSamples);
    for (int index = 0; index < kSamples; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSamples - 1);
        float value = minimum + span * t;
        if (mode == u"rapid reversal"_qs || mode == u"instant reversal torture"_qs) value = t < 0.46F
            ? minimum + span * (t / 0.46F) : maximum - span * ((t - 0.46F) / 0.54F);
        else if (mode == u"human-like rapid reversal"_qs) {
            const float elapsed = static_cast<float>(index) * kSamplePeriodSeconds;
            const auto smootherStep = [](float progress) {
                const float u = std::clamp(progress, 0.0F, 1.0F);
                return u * u * u * (u * (u * 6.0F - 15.0F) + 10.0F);
            };
            if (elapsed < 0.080F) value = minimum + span * 0.20F;
            else if (elapsed < 0.340F) {
                value = minimum + span * (0.20F + 0.66F
                    * smootherStep((elapsed - 0.080F) / 0.260F));
            } else if (elapsed < 0.600F) {
                value = minimum + span * (0.86F - 0.56F
                    * smootherStep((elapsed - 0.340F) / 0.260F));
            } else value = minimum + span * 0.30F;
        } else if (mode == u"positive-side reversal"_qs) value = t < 0.46F
            ? minimum + span * (0.50F + 0.44F * (t / 0.46F))
            : minimum + span * (0.94F - 0.36F * ((t - 0.46F) / 0.54F));
        else if (mode == u"negative-side reversal"_qs) value = t < 0.46F
            ? minimum + span * (0.50F - 0.44F * (t / 0.46F))
            : minimum + span * (0.06F + 0.36F * ((t - 0.46F) / 0.54F));
        else if (mode == u"center-crossing reversal"_qs) value = t < 0.42F
            ? minimum + span * (0.18F + 0.64F * (t / 0.42F))
            : minimum + span * (0.82F - 0.64F * ((t - 0.42F) / 0.58F));
        else if (mode == u"micro adjustments"_qs) value = minimum + span * 0.5F
            + span * ((index % 16 < 8 ? 1.0F : -1.0F) * 0.018F);
        else if (mode == u"sudden stop"_qs) value = t < 0.48F ? minimum + span * (t / 0.48F) : maximum;
        else if (mode == u"center fighting"_qs) value = minimum + span * 0.5F
            + span * ((index % 24 < 12 ? 1.0F : -1.0F) * 0.22F);
        else if (mode == u"fast sweep"_qs) value = t < 0.28F ? minimum + span * (t / 0.28F) : maximum;
        physical.push_back(std::clamp(value, minimum, maximum));
    }
    return physical;
}

} // namespace hotas
