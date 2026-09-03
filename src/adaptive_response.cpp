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

constexpr float kSourceSpeedDecayRatio = 0.015F;
constexpr float kSourceBrakingCoherence = 0.62F;

float smootherstep(float lower, float upper, float value)
{
    const float normalized = std::clamp((value - lower) / std::max(0.000001F, upper - lower), 0.0F, 1.0F);
    return normalized * normalized * normalized * (normalized * (normalized * 6.0F - 15.0F) + 10.0F);
}

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
    const auto onsetAssist = static_cast<std::uint32_t>(AdaptiveResponseOnsetAssist);
    const auto onsetCap = static_cast<std::uint32_t>(AdaptiveResponseOnsetCap);
    const auto sustainedAssist = static_cast<std::uint32_t>(AdaptiveResponseSustainedAssist);
    const auto sustainedCap = static_cast<std::uint32_t>(AdaptiveResponseSustainedCap);
    const auto horizonExtension = static_cast<std::uint32_t>(AdaptiveResponseHorizonExtension);
    const auto horizonExtensionCap = static_cast<std::uint32_t>(AdaptiveResponseHorizonExtensionCap);
    const auto turningPointProtection = static_cast<std::uint32_t>(AdaptiveResponseTurningPointProtection);
    const auto turningPointMargin = static_cast<std::uint32_t>(AdaptiveResponseTurningPointMargin);
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
    if (properties & onsetAssist) target.onsetAssist = source.onsetAssist;
    if (properties & onsetCap) target.onsetCap = source.onsetCap;
    if (properties & sustainedAssist) target.sustainedAssist = source.sustainedAssist;
    if (properties & sustainedCap) target.sustainedCap = source.sustainedCap;
    if (properties & horizonExtension) target.horizonExtension = source.horizonExtension;
    if (properties & horizonExtensionCap) target.horizonExtensionCapMs = source.horizonExtensionCapMs;
    if (properties & turningPointProtection) target.turningPointProtection = source.turningPointProtection;
    if (properties & turningPointMargin) target.turningPointMargin = source.turningPointMargin;
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
    settings.onsetAssist = clampFinite(settings.onsetAssist, 0.0F, 1.0F, 0.0F);
    settings.onsetCap = clampFinite(settings.onsetCap, 0.0F, 0.40F, 0.0F);
    settings.sustainedAssist = clampFinite(settings.sustainedAssist, 0.0F, 1.0F, 0.0F);
    settings.sustainedCap = clampFinite(settings.sustainedCap, 0.0F, 0.35F, 0.0F);
    settings.horizonExtension = clampFinite(settings.horizonExtension, 0.0F, 1.0F, 0.0F);
    settings.horizonExtensionCapMs = clampFinite(settings.horizonExtensionCapMs, 0.0F, 30.0F, 0.0F);
    settings.turningPointProtection = clampFinite(settings.turningPointProtection, 0.0F, 1.0F, 0.0F);
    settings.turningPointMargin = clampFinite(settings.turningPointMargin, 0.0F, 0.30F, 0.0F);
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
        light.onsetAssist = 0.10F; light.onsetCap = 0.06F;
        light.sustainedAssist = 0.12F; light.sustainedCap = 0.06F;
        light.horizonExtension = 0.10F; light.horizonExtensionCapMs = 4.0F;
        light.turningPointProtection = 0.75F; light.turningPointMargin = 0.15F;
        AdaptiveResponseSettings balanced;
        balanced.enabled = true; balanced.maximumHorizonMs = 8.0F; balanced.maximumLead = 0.12F;
        balanced.onsetAssist = 0.18F; balanced.onsetCap = 0.10F;
        balanced.sustainedAssist = 0.22F; balanced.sustainedCap = 0.10F;
        balanced.horizonExtension = 0.22F; balanced.horizonExtensionCapMs = 8.0F;
        balanced.turningPointProtection = 0.82F; balanced.turningPointMargin = 0.14F;
        AdaptiveResponseSettings fast = balanced;
        fast.maximumHorizonMs = 12.0F; fast.maximumLead = 0.18F;
        fast.velocityResponse = 0.80F; fast.accelerationResponse = 0.68F;
        fast.onsetAssist = 0.28F; fast.onsetCap = 0.16F;
        fast.sustainedAssist = 0.32F; fast.sustainedCap = 0.15F;
        fast.horizonExtension = 0.35F; fast.horizonExtensionCapMs = 12.0F;
        fast.turningPointProtection = 0.88F; fast.turningPointMargin = 0.12F;
        AdaptiveResponseSettings aggressive = fast;
        aggressive.maximumHorizonMs = 18.0F; aggressive.maximumLead = 0.27F;
        aggressive.velocityResponse = 0.91F; aggressive.accelerationResponse = 0.82F;
        aggressive.onsetAssist = 0.38F; aggressive.onsetCap = 0.22F;
        aggressive.sustainedAssist = 0.42F; aggressive.sustainedCap = 0.20F;
        aggressive.horizonExtension = 0.48F; aggressive.horizonExtensionCapMs = 18.0F;
        aggressive.turningPointProtection = 0.94F; aggressive.turningPointMargin = 0.10F;
        AdaptiveResponseSettings extreme = aggressive;
        extreme.maximumHorizonMs = 30.0F; extreme.maximumLead = 0.40F;
        extreme.velocityResponse = 1.0F; extreme.accelerationResponse = 0.95F;
        extreme.onsetAssist = 0.50F; extreme.onsetCap = 0.30F;
        extreme.sustainedAssist = 0.55F; extreme.sustainedCap = 0.28F;
        extreme.horizonExtension = 0.65F; extreme.horizonExtensionCapMs = 24.0F;
        extreme.turningPointProtection = 1.0F; extreme.turningPointMargin = 0.08F;
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
    runtime.onsetAssist = settings.onsetAssist;
    runtime.onsetCap = settings.onsetCap;
    runtime.sustainedAssist = settings.sustainedAssist;
    runtime.sustainedCap = settings.sustainedCap;
    runtime.horizonExtension = settings.horizonExtension;
    runtime.horizonExtensionCapSeconds = settings.horizonExtensionCapMs / 1000.0F;
    runtime.turningPointProtection = settings.turningPointProtection;
    runtime.turningPointMargin = settings.turningPointMargin;
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
    settings.onsetAssist = base.onsetAssist;
    settings.onsetCap = base.onsetCap;
    settings.sustainedAssist = base.sustainedAssist;
    settings.sustainedCap = base.sustainedCap;
    settings.horizonExtension = base.horizonExtension;
    settings.horizonExtensionCapMs = base.horizonExtensionCapSeconds * 1000.0F;
    settings.turningPointProtection = base.turningPointProtection;
    settings.turningPointMargin = base.turningPointMargin;
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
    base.onsetAssist = settings.onsetAssist;
    base.onsetCap = settings.onsetCap;
    base.sustainedAssist = settings.sustainedAssist;
    base.sustainedCap = settings.sustainedCap;
    base.horizonExtension = settings.horizonExtension;
    base.horizonExtensionCapSeconds = settings.horizonExtensionCapMs / 1000.0F;
    base.turningPointProtection = settings.turningPointProtection;
    base.turningPointMargin = settings.turningPointMargin;
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
    m_lastAcceptedSourcePhysical = 0.0F;
    m_lastAcceptedSourceDeltaMagnitude = 0.0F;
    m_lastAcceptedSourceInterval = 0.0F;
    m_acceptedSourceDirection = 0;
    m_sourceDecayEvidence = 0;
    m_brakingReferenceSourceSpeed = 0.0F;
    m_sourceStoppingSeconds = 0.0F;
    m_brakingReductionTarget = 0.0F;
    m_brakingReductionFactor = 0.0F;
    m_oppositeEvidenceCount = 0;
    m_motionDirection = 0;
    m_reacquisitionAuthority = 1.0F;
    m_launchEvidence = 0.0F;
    m_launchEvidenceDirection = 0;
    m_launchEvidenceCount = 0;
    m_onsetAuthority = 0.0F;
    m_sustainedEvidence = 0.0F;
    m_horizonExtensionAuthority = 0.0F;
    m_turningPointAuthority = 0.0F;
    m_turningPointHorizonLimitSeconds = 0.060F;
    m_turningPointLeadLimit = 0.50F;
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
        m_lastAcceptedSourcePhysical = physical;
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
    float acceptedSourceInterval = 0.0F;
    if (sourceUpdated) {
        const float sourceInterval = std::clamp(static_cast<float>(
            std::chrono::duration<double>(timestamp - m_lastSourceTimestamp).count()),
            kMinimumPredictionDeltaSeconds, kMaximumSourceUpdatePeriodSeconds);
        acceptedSourceInterval = sourceInterval;
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
    float sourceSpeedGrowth = 0.0F;
    bool sourceSpeedGrowingInMotionDirection = false;
    int sourceGrowthDirection = 0;
    // Accepted-source speed is a fixed-state physical braking detector. It
    // requires two coherent decays; individual report jitter cannot create
    // braking or a turning constraint.
    if (sourceUpdated) {
        const float acceptedSourceDelta = physical - m_lastAcceptedSourcePhysical;
        const float acceptedSourceDeltaMagnitude = std::abs(acceptedSourceDelta);
        const int observedSourceDirection = directionOf(acceptedSourceDelta, kMinimumMeaningfulDelta);
        // Launch preparation may use a finer, still bounded source report
        // threshold than the braking detector. It is never sufficient on its
        // own: two coherent speed-growth reports are required below.
        sourceGrowthDirection = directionOf(acceptedSourceDelta,
            kMinimumMeaningfulDelta * 0.10F);
        // A tiny accepted source update is useful for cadence/quiet tracking,
        // but its sign is not trustworthy. It must not erase the coherent
        // direction and stop evidence that carried us into a turning point.
        const int acceptedSourceDirection = observedSourceDirection == 0
            ? m_acceptedSourceDirection : observedSourceDirection;
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
        const float acceptedSourceSpeed = acceptedSourceDeltaMagnitude
            / std::max(acceptedSourceInterval, kMinimumPredictionDeltaSeconds);
        const float priorAcceptedSourceSpeed = m_lastAcceptedSourceDeltaMagnitude
            / std::max(m_lastAcceptedSourceInterval, kMinimumPredictionDeltaSeconds);
        sourceSpeedGrowth = std::max(0.0F, (acceptedSourceSpeed - priorAcceptedSourceSpeed)
            / std::max(acceptedSourceInterval, kMinimumPredictionDeltaSeconds));
        sourceSpeedGrowingInMotionDirection = sameDirection && sourceSpeedGrowth > 0.0F;
        if (meaningfulDecay) {
            if (m_sourceDecayEvidence == 0) m_brakingReferenceSourceSpeed = priorAcceptedSourceSpeed;
            m_sourceDecayEvidence = std::min<std::uint8_t>(4, m_sourceDecayEvidence + 1);
            if (m_sourceDecayEvidence >= 2) {
                const float sourceSpeedDrop = std::max(0.0F, priorAcceptedSourceSpeed - acceptedSourceSpeed);
                const float sourceSlope = sourceSpeedDrop / std::max(acceptedSourceInterval,
                                                                       kMinimumPredictionDeltaSeconds);
                m_sourceStoppingSeconds = std::clamp(acceptedSourceSpeed / std::max(sourceSlope, 0.001F),
                                                     0.0F, 0.75F);
                const float cumulativeDecay = std::clamp((m_brakingReferenceSourceSpeed - acceptedSourceSpeed)
                        / std::max(m_brakingReferenceSourceSpeed, sourceSpeedFloor), 0.0F, 1.0F);
                const float persistence = smootherstep(1.0F, 4.0F,
                    static_cast<float>(m_sourceDecayEvidence));
                // Reduction follows progress toward the actual physical stop,
                // not merely the first third of a speed decrease. A 50%
                // remaining source speed therefore remains a useful, broad
                // prediction gradient instead of an early guillotine.
                const float nearStopProgress = 0.97F - 0.12F * std::clamp(
                    configuration.settlingResponse, 0.0F, 1.0F);
                m_brakingReductionTarget = smootherstep(0.02F, nearStopProgress, cumulativeDecay)
                    * persistence * configuration.decelerationResponse;
            }
        } else if (completingConfirmedBrake) {
            m_sourceStoppingSeconds = 0.0F;
            m_brakingReductionTarget = configuration.decelerationResponse;
        } else if (observedSourceDirection == 0 && m_sourceDecayEvidence >= 2) {
            // The source is effectively still at the old-direction stop. Do
            // not create a detector dropout between the last coherent sample
            // and the first credible opposite sample.
            m_brakingReductionTarget = std::max(m_brakingReductionTarget,
                configuration.decelerationResponse);
        } else if (observedSourceDirection != 0 && acceptedSourceDirection != m_motionDirection) {
            // The first opposite report is safety-cancelled below. Retain the
            // stored envelope until the real reversal boundary resets it, so
            // a briefly indeterminate turn cannot reintroduce stale lead.
            m_brakingReductionTarget = 0.0F;
        } else {
            m_sourceDecayEvidence = 0;
            m_brakingReferenceSourceSpeed = 0.0F;
            m_sourceStoppingSeconds = 0.0F;
            m_brakingReductionTarget = 0.0F;
        }
        m_lastAcceptedSourcePhysical = physical;
        m_lastAcceptedSourceDeltaMagnitude = acceptedSourceDeltaMagnitude;
        m_lastAcceptedSourceInterval = acceptedSourceInterval;
        if (observedSourceDirection != 0) m_acceptedSourceDirection = acceptedSourceDirection;
    }
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
    bool sourceBrakingDetected = false;
    if (m_sourceDecayEvidence >= 2 && m_brakingReductionTarget > 0.0F && !reversal
        && coherence >= kSourceBrakingCoherence && m_acceptedSourceDirection != 0
        && m_acceptedSourceDirection == m_motionDirection) {
        sourceBrakingDetected = true;
    }
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
        // A credible opposite sample has already zeroed stale old-direction
        // lead. Start only the new direction's predictive authority from a
        // bounded low value, rather than making the reset boundary a jump.
        m_reacquisitionAuthority = 0.0F;
        // A confirmed new direction owns a new braking envelope. The old one
        // has already cancelled stale lead safely and must not delay the
        // opposite-direction acquisition.
        m_sourceDecayEvidence = 0;
        m_brakingReferenceSourceSpeed = 0.0F;
        m_brakingReductionTarget = 0.0F;
        m_brakingReductionFactor = 0.0F;
        if (m_launchEvidenceDirection != m_motionDirection) {
            m_launchEvidence = 0.0F;
            m_launchEvidenceDirection = 0;
            m_launchEvidenceCount = 0;
        }
        m_onsetAuthority = 0.0F;
        m_sustainedEvidence = 0.0F;
        m_horizonExtensionAuthority = 0.0F;
        m_turningPointAuthority = 0.0F;
        m_turningPointHorizonLimitSeconds = 0.060F;
        m_turningPointLeadLimit = 0.50F;
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
    // Ordinary coherent motion owns a direction too. This is deliberately
    // updated after estimator correction so source-braking and sustained
    // evidence use the last trusted direction rather than a raw jitter sign.
    if (!reversal && trustedMeasurement) {
        const int trustedDirection = directionOf(m_velocity, velocityThreshold);
        if (trustedDirection != 0) m_motionDirection = trustedDirection;
    }
    if (confirmedQuiet) {
        m_velocity = 0.0F;
        m_acceleration = 0.0F;
        m_estimatedPosition = physical;
        m_motionDirection = 0;
        m_reacquisitionAuthority = 1.0F;
        m_launchEvidence = 0.0F;
        m_launchEvidenceDirection = 0;
        m_launchEvidenceCount = 0;
        m_onsetAuthority = 0.0F;
        m_sustainedEvidence = 0.0F;
        m_horizonExtensionAuthority = 0.0F;
        m_turningPointAuthority = 0.0F;
        m_turningPointHorizonLimitSeconds = 0.060F;
        m_turningPointLeadLimit = 0.50F;
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
    // The envelope is a time-normalized first-order response. Its stored
    // value applies continuously, even when an instantaneous detector flickers
    // false between source reports.
    const float brakingTau = m_brakingReductionTarget > m_brakingReductionFactor
        ? kBrakingAttackSeconds : kBrakingReleaseSeconds;
    const float brakingAlpha = dt / (brakingTau + dt);
    m_brakingReductionFactor += (m_brakingReductionTarget - m_brakingReductionFactor) * brakingAlpha;
    m_brakingReductionFactor = std::clamp(m_brakingReductionFactor, 0.0F, 1.0F);
    const bool braking = decelerating
        || m_brakingReductionTarget > 0.0001F || m_brakingReductionFactor > 0.0001F
        ;
    // The authority pipeline has three bounded inputs. Velocity owns normal
    // fast motion; onset helps only while a coherent motion is gaining speed;
    // sustained evidence can fill what remains for deliberate slow/moderate
    // motion. None of these are output smoothing or extra axis gain.
    // Normalize acceleration evidence to the configured deliberate-motion
    // scale. The band is deliberately low enough for a small fast correction,
    // while coherence and direction gates still reject derivative noise.
    const float accelerationLow = std::max(0.10F, configuration.motionSensitivity * 20.0F);
    const float accelerationHigh = std::max(accelerationLow * 3.0F,
                                            configuration.motionSensitivity * 120.0F);
    const float rawAccelerationIntent = smootherstep(accelerationLow, accelerationHigh,
        std::abs(m_acceleration));
    const int accelerationDirection = directionOf(m_acceleration, accelerationLow * 0.25F);
    const int velocityDirection = directionOf(m_velocity, velocityThreshold * 0.25F);
    const int trendDirectionForOnset = directionOf(trendVelocity, velocityThreshold * 0.25F);
    const int intendedDirection = velocityDirection != 0 ? velocityDirection : trendDirectionForOnset;
    const bool accelerationAligned = accelerationDirection != 0 && intendedDirection != 0
        && accelerationDirection == intendedDirection;
    // Launch intent deliberately combines the estimator's acceleration with
    // independent accepted-source/trend speed growth. A low-lag ABG estimate
    // may be momentarily stale at launch, but it cannot by itself create
    // authority without coherent directional motion.
    const float trendSpeedGrowth = std::max(0.0F, (speed - std::abs(priorVelocity))
        / std::max(dt, kMinimumPredictionDeltaSeconds));
    const float sourceLaunchIntent = smootherstep(accelerationLow, accelerationHigh,
        sourceSpeedGrowingInMotionDirection ? sourceSpeedGrowth : 0.0F);
    const float trendLaunchIntent = smootherstep(accelerationLow, accelerationHigh,
        trendSpeedGrowth);
    const float establishedLaunchIntent = std::max(rawAccelerationIntent,
        std::max(sourceLaunchIntent, trendLaunchIntent));
    // Prearm state is relevant only before broad measurement validity, or
    // while an opposite report is safety-cancelled. Established motion keeps
    // the normal onset path and does not pay for preparatory authority work.
    const bool launchNeedsPrearm = !meaningfulMeasurement || safetyCancellation;
    const bool sourceGrowthCanPrearm = launchNeedsPrearm && sourceGrowthDirection != 0
        && (m_motionDirection == 0 || sourceGrowthDirection == m_motionDirection)
        && (m_launchEvidenceDirection == 0
            || sourceGrowthDirection == m_launchEvidenceDirection)
        && sourceSpeedGrowth > accelerationLow * 0.65F;
    // Prearming contributes only before the broad measurement gate opens;
    // established movement retains precisely the existing intent scale and
    // therefore cannot gain a larger onset peak from this path.
    float launchIntent = establishedLaunchIntent;
    if (!meaningfulMeasurement && sourceGrowthCanPrearm) {
        launchIntent = std::max(launchIntent, smootherstep(accelerationLow,
            accelerationHigh, sourceSpeedGrowth));
    }
    const bool launchAligned = accelerationAligned || sourceSpeedGrowingInMotionDirection
        || sourceGrowthCanPrearm
        || (trendSpeedGrowth > accelerationLow && intendedDirection == m_motionDirection);
    // Launch evidence is intentionally separate from applied onset authority.
    // A few fresh, same-direction source-speed increases can prepare a small
    // contribution before the broad meaningful-measurement gate opens, but a
    // lone sample, braking, or an old-direction prediction cannot command it.
    const int launchEvidenceDirection = rawDirection != 0 ? rawDirection : intendedDirection;
    const float launchTravelThreshold = std::max(kMinimumMeaningfulDelta * 1.5F,
        meaningfulDelta * 0.45F);
    const bool ordinaryPrearm = !meaningfulMeasurement && sourceUpdated && launchEvidenceDirection != 0
        && (m_motionDirection == 0 || launchEvidenceDirection == m_motionDirection)
        && coherence >= 0.84F && std::abs(physical - m_lastMeaningfulPhysical) >= launchTravelThreshold
        && (sourceGrowthCanPrearm
            || sourceSpeedGrowingInMotionDirection
            || (trendSpeedGrowth > accelerationLow * 0.65F && launchAligned));
    // Safety cancellation always keeps old-direction lead at zero. It may,
    // however, collect bounded evidence for repeated credible opposite input
    // so confirmed reversal reacquisition does not start from nothing.
    const bool oppositePrearm = safetyCancellation && sourceUpdated && rawDirection != 0
        && m_oppositeEvidenceCount >= 1 && absoluteDelta >= launchTravelThreshold
        && coherence >= 0.72F;
    const bool prearmCandidate = (!braking && !settling && ordinaryPrearm) || oppositePrearm;
    if (confirmedQuiet) {
        m_launchEvidence = 0.0F;
        m_launchEvidenceDirection = 0;
        m_launchEvidenceCount = 0;
    } else if (prearmCandidate) {
        if (m_launchEvidenceDirection != 0 && m_launchEvidenceDirection != launchEvidenceDirection) {
            m_launchEvidence = 0.0F;
            m_launchEvidenceCount = 0;
        }
        m_launchEvidenceDirection = launchEvidenceDirection;
        m_launchEvidenceCount = std::min<std::uint8_t>(3, m_launchEvidenceCount + 1);
        constexpr float kLaunchEvidenceAttackTauSeconds = 0.012F;
        const float attackAlpha = dt / (kLaunchEvidenceAttackTauSeconds + dt);
        m_launchEvidence += (1.0F - m_launchEvidence) * attackAlpha;
    } else if (launchNeedsPrearm) {
        const float releaseTau = (braking || settling) ? 0.006F : 0.020F;
        const float releaseAlpha = dt / (releaseTau + dt);
        m_launchEvidence += (0.0F - m_launchEvidence) * releaseAlpha;
        if (m_launchEvidence < 0.0001F) {
            m_launchEvidence = 0.0F;
            m_launchEvidenceDirection = 0;
            m_launchEvidenceCount = 0;
        }
    } else {
        m_launchEvidence = 0.0F;
        m_launchEvidenceDirection = 0;
        m_launchEvidenceCount = 0;
    }
    m_launchEvidence = std::clamp(m_launchEvidence, 0.0F, 1.0F);
    const int onsetDirection = intendedDirection != 0 ? intendedDirection : launchEvidenceDirection;
    const bool prearmedDirectionMatches = m_launchEvidenceDirection != 0
        && m_launchEvidenceDirection == onsetDirection;
    const float prearmedReadiness = !meaningfulMeasurement && m_launchEvidenceCount >= 2
        && prearmedDirectionMatches
        ? 0.32F * smootherstep(0.15F, 0.70F, m_launchEvidence) : 0.0F;
    const float onsetReadiness = meaningfulMeasurement ? 1.0F : prearmedReadiness;
    const bool onsetValid = onsetReadiness > 0.0F && coherence >= 0.70F && launchAligned
        && !braking && !settling && !safetyCancellation && !reversal && !confirmedQuiet
        && m_reacquisitionAuthority > 0.0F;
    const float accelerationIntent = onsetValid
        ? launchIntent * configuration.accelerationResponse * onsetReadiness : 0.0F;
    const float onsetTarget = accelerationIntent * configuration.onsetAssist * configuration.onsetCap
        * coherence * m_reacquisitionAuthority;
    // This is a predictor-authority envelope, not an axis/output filter. It
    // removes the last discrete eligibility knee while keeping braking and
    // reversal shutdown immediate and safety-first.
    if (braking || safetyCancellation || reversal || confirmedQuiet) {
        m_onsetAuthority = 0.0F;
    } else {
        constexpr float kOnsetAttackTauSeconds = 0.006F;
        constexpr float kOnsetReleaseTauSeconds = 0.016F;
        const float onsetTau = onsetTarget > m_onsetAuthority ? kOnsetAttackTauSeconds
                                                               : kOnsetReleaseTauSeconds;
        const float onsetAlpha = dt / (onsetTau + dt);
        m_onsetAuthority += (onsetTarget - m_onsetAuthority) * onsetAlpha;
        m_onsetAuthority = std::clamp(m_onsetAuthority, 0.0F, 0.40F);
    }
    const float onsetAuthority = m_onsetAuthority;

    const bool sameDirection = rawDirection != 0 && m_motionDirection != 0
        && rawDirection == m_motionDirection;
    const bool sustainedEligible = sourceUpdated && meaningfulMeasurement && sameDirection
        && coherence >= 0.82F && speed > microCutoff * 0.65F
        && !braking && !settling && !safetyCancellation && !reversal && !confirmedQuiet
        && m_reacquisitionAuthority > 0.0F;
    if (safetyCancellation || reversal || confirmedQuiet) {
        // A completed quiet, an ambiguous turn, or coherent braking must never
        // carry slow-motion authority into an invalid old direction.
        m_sustainedEvidence = 0.0F;
    } else if (braking || settling) {
        constexpr float kSustainedBrakeReleaseTauSeconds = 0.040F;
        const float releaseAlpha = dt / (kSustainedBrakeReleaseTauSeconds + dt);
        m_sustainedEvidence += (0.0F - m_sustainedEvidence) * releaseAlpha;
    } else if (sustainedEligible) {
        constexpr float kSustainedEvidenceTauSeconds = 0.080F;
        const float sustainedAlpha = dt / (kSustainedEvidenceTauSeconds + dt);
        m_sustainedEvidence += (1.0F - m_sustainedEvidence) * sustainedAlpha;
    } else if (!sourceUpdated || coherence < 0.82F || !sameDirection) {
        constexpr float kSustainedReleaseTauSeconds = 0.040F;
        const float releaseAlpha = dt / (kSustainedReleaseTauSeconds + dt);
        m_sustainedEvidence += (0.0F - m_sustainedEvidence) * releaseAlpha;
    }
    m_sustainedEvidence = std::clamp(m_sustainedEvidence, 0.0F, 1.0F);
    // Moderate deliberate travel has not exhausted velocity authority yet.
    // Preserve that room for sustained fill; genuinely fast motion still
    // reaches zero assistance as intensity approaches one.
    const float slowModerateAuthority = 1.0F - smootherstep(0.78F, 0.98F, intensity);
    // Evidence already releases smoothly on ordinary braking/settling.  The
    // applied authority uses the same continuous braking envelope instead of
    // a binary handoff, while safety/reversal/quiet above still invalidate it
    // immediately.
    const float sustainedBrakeSuppression = 1.0F - smootherstep(0.03F, 0.72F,
        m_brakingReductionFactor);
    const float sustainedAuthority = m_sustainedEvidence * coherence * slowModerateAuthority
        * configuration.sustainedAssist * configuration.sustainedCap
        * m_reacquisitionAuthority * sustainedBrakeSuppression;
    const float velocityAuthority = intensity;
    const float motionUrgency = std::clamp(1.0F - (1.0F - velocityAuthority)
        * (1.0F - std::clamp(onsetAuthority, 0.0F, 1.0F))
        * (1.0F - std::clamp(sustainedAuthority, 0.0F, 1.0F)), 0.0F, 1.0F);

    // Horizon extension is intentionally a separate eligibility from motion
    // authority. It cannot grow from repeated sample-and-hold values: only
    // real source updates build sustained evidence, and any braking/turning
    // path collapses the eligibility immediately.
    const float lowSpeedForExtension = 1.0F - smootherstep(0.65F, 0.98F, intensity);
    // Estimator acceleration is intentionally responsive. Treat only a much
    // larger sustained derivative as trajectory-dynamic here; otherwise a
    // smooth low-speed sweep would be rejected simply because it began with
    // finite jerk.
    const float lowAccelerationForExtension = 1.0F - smootherstep(
        accelerationHigh, accelerationHigh * 6.0F, std::abs(m_acceleration));
    const float highCoherenceForExtension = smootherstep(0.80F, 0.98F, coherence);
    // Only a fresh physical source change can build extension. The stored
    // envelope may coast smoothly over an ordinary held report, but repeated
    // sample-and-hold values cannot manufacture new horizon authority.
    const float horizonExtensionTarget = (sourceUpdated && !braking && !settling && !safetyCancellation
        && !reversal && !confirmedQuiet && m_reacquisitionAuthority > 0.0F)
        ? std::clamp(m_sustainedEvidence * lowSpeedForExtension * lowAccelerationForExtension
            * highCoherenceForExtension * holdConfidence * configuration.horizonExtension
            * m_reacquisitionAuthority, 0.0F, 1.0F)
        : 0.0F;
    if (safetyCancellation || reversal || confirmedQuiet) {
        m_horizonExtensionAuthority = 0.0F;
    } else {
        constexpr float kExtensionAttackTauSeconds = 0.045F;
        constexpr float kExtensionReleaseTauSeconds = 0.020F;
        constexpr float kExtensionBrakeReleaseTauSeconds = 0.008F;
        const float extensionTau = horizonExtensionTarget > m_horizonExtensionAuthority
            ? kExtensionAttackTauSeconds : kExtensionReleaseTauSeconds;
        const float releaseTau = braking ? kExtensionBrakeReleaseTauSeconds : extensionTau;
        const float extensionAlpha = dt / (releaseTau + dt);
        m_horizonExtensionAuthority += (horizonExtensionTarget - m_horizonExtensionAuthority)
            * extensionAlpha;
        m_horizonExtensionAuthority = std::clamp(m_horizonExtensionAuthority, 0.0F, 1.0F);
    }
    const float horizonExtensionEligibility = m_horizonExtensionAuthority;
    const float normalMaximumHorizon = std::clamp(configuration.maximumHorizonSeconds, 0.0F, 0.030F);
    const float allowedMaximumHorizon = std::min(0.060F, normalMaximumHorizon
        + horizonExtensionEligibility * std::clamp(configuration.horizonExtensionCapSeconds, 0.0F, 0.030F));

    // Strong, coherent reversals regain their justified new-direction lead in
    // 8–16 ms. This touches only prediction authority, never input/mapping.
    const float reacquisitionTau = 0.016F - 0.008F * coherence * configuration.reversalResponse;
    const float reacquisitionAlpha = dt / (std::max(0.008F, reacquisitionTau) + dt);
    m_reacquisitionAuthority += (1.0F - m_reacquisitionAuthority) * reacquisitionAlpha;
    m_reacquisitionAuthority = std::clamp(m_reacquisitionAuthority, 0.0F, 1.0F);
    float horizon = allowedMaximumHorizon * motionUrgency * confidence
        * m_reacquisitionAuthority;
    if (reversal) horizon *= 0.35F + configuration.reversalResponse * 0.65F;
    if (m_brakingReductionFactor > 0.0001F) horizon *= 1.0F - m_brakingReductionFactor;
    if (safetyCancellation) horizon = 0.0F;

    // The source-speed-decay envelope is stronger evidence than a raw ABG
    // derivative near a real turn. Derive a conservative stop time from the
    // same accepted-source state, then use estimator deceleration only when
    // it agrees. This is scalar state only and remains rate-normalized.
    float sourceBrakingEvidence = 0.0F;
    float sourceTimeToTurnSeconds = 0.0F;
    float sourceRemainingTravel = 0.0F;
    if (m_sourceDecayEvidence >= 2 && m_brakingReductionFactor > 0.0001F
        && m_acceptedSourceDirection != 0 && m_acceptedSourceDirection == m_motionDirection) {
        const float sourceSpeed = m_lastAcceptedSourceDeltaMagnitude
            / std::max(m_lastAcceptedSourceInterval, kMinimumPredictionDeltaSeconds);
        const float evidenceDuration = std::max(m_sourceUpdatePeriodSeconds
            * static_cast<float>(m_sourceDecayEvidence), kMinimumPredictionDeltaSeconds);
        const float sourceDeceleration = std::max(0.0F,
            (m_brakingReferenceSourceSpeed - sourceSpeed) / evidenceDuration);
        if (sourceSpeed > velocityThreshold && sourceDeceleration > accelerationLow * 0.20F) {
            sourceTimeToTurnSeconds = std::clamp(sourceSpeed / sourceDeceleration, 0.0F, 0.120F);
            sourceRemainingTravel = 0.5F * sourceSpeed * sourceTimeToTurnSeconds;
            // Two coherent accepted source-speed decays are already a strong
            // directional fact. The envelope then refines how near the stop
            // is; it must not postpone turn protection until the lead has
            // already crossed the physical apex.
            sourceBrakingEvidence = std::clamp((0.70F + 0.30F * m_brakingReductionFactor)
                * coherence, 0.0F, 1.0F);
        }
    }
    const bool accelerationOpposesMotion = m_velocity * m_acceleration
        < -accelerationLow * std::max(speed, velocityThreshold);
    const float accelerationTurnConfidence = accelerationOpposesMotion
        ? smootherstep(accelerationLow, accelerationHigh, std::abs(m_acceleration)) * coherence : 0.0F;
    const float estimatorTimeToTurnSeconds = accelerationOpposesMotion
        ? std::clamp(speed / std::max(std::abs(m_acceleration), accelerationLow), 0.0F, 0.120F) : 0.0F;
    const float estimatorRemainingTravel = accelerationOpposesMotion
        ? 0.5F * speed * estimatorTimeToTurnSeconds : 0.0F;
    const float rawTurningPointConfidence = std::clamp(std::max(sourceBrakingEvidence,
        sourceBrakingEvidence > 0.0F ? accelerationTurnConfidence * sourceBrakingEvidence
                                     : accelerationTurnConfidence * 0.45F), 0.0F, 1.0F);
    const float estimatedTimeToTurnSeconds = sourceTimeToTurnSeconds > 0.0F
        ? sourceTimeToTurnSeconds : estimatorTimeToTurnSeconds;
    const float estimatedRemainingTravel = sourceRemainingTravel > 0.0F
        ? sourceRemainingTravel : estimatorRemainingTravel;
    const float rawTurningPointHorizonLimit = estimatedTimeToTurnSeconds > 0.0F
        ? std::min(0.060F, estimatedTimeToTurnSeconds
            * (0.78F + 0.22F * configuration.turningPointMargin)) : 0.0F;
    const float rawTurningPointLeadLimit = estimatedRemainingTravel > 0.0F
        ? estimatedRemainingTravel * (1.0F + configuration.turningPointMargin) : 0.0F;
    // The estimator may suggest deceleration, but persistent turning
    // protection requires the same two accepted source-speed decays as the
    // braking envelope. That prevents a responsive ABG derivative from
    // constraining normal high-speed travel before physical braking is real.
    const bool credibleTurningConstraint = sourceBrakingEvidence > 0.0001F
        && (rawTurningPointHorizonLimit > 0.0F || rawTurningPointLeadLimit > 0.0F);
    const bool renewedAcceleration = !sourceBrakingDetected
        && (sourceSpeedGrowingInMotionDirection
            || m_velocity * m_acceleration > accelerationLow * std::max(speed, velocityThreshold));
    if (safetyCancellation || reversal || confirmedQuiet) {
        m_turningPointAuthority = 0.0F;
        m_turningPointHorizonLimitSeconds = 0.060F;
        m_turningPointLeadLimit = 0.50F;
    } else if (credibleTurningConstraint) {
        // A coherent braking episode accepts tighter estimates quickly.  A
        // looser raw estimate is held back unless acceleration is genuinely
        // renewed, which removes source-cadence saw-toothing without making
        // a credible physical apex less safe.
        constexpr float kTurningConstraintTightenTauSeconds = 0.010F;
        constexpr float kTurningConstraintSlowReleaseTauSeconds = 0.090F;
        constexpr float kTurningConstraintRenewedReleaseTauSeconds = 0.014F;
        const float tightenAlpha = dt / (kTurningConstraintTightenTauSeconds + dt);
        if (rawTurningPointConfidence > m_turningPointAuthority) {
            m_turningPointAuthority += (rawTurningPointConfidence - m_turningPointAuthority) * tightenAlpha;
        }
        if (rawTurningPointHorizonLimit > 0.0F) {
            const float releaseTau = renewedAcceleration ? kTurningConstraintRenewedReleaseTauSeconds
                                                         : kTurningConstraintSlowReleaseTauSeconds;
            const float alpha = rawTurningPointHorizonLimit < m_turningPointHorizonLimitSeconds
                ? tightenAlpha : dt / (releaseTau + dt);
            m_turningPointHorizonLimitSeconds += (rawTurningPointHorizonLimit
                - m_turningPointHorizonLimitSeconds) * alpha;
        }
        if (rawTurningPointLeadLimit > 0.0F) {
            const float releaseTau = renewedAcceleration ? kTurningConstraintRenewedReleaseTauSeconds
                                                         : kTurningConstraintSlowReleaseTauSeconds;
            const float alpha = rawTurningPointLeadLimit < m_turningPointLeadLimit
                ? tightenAlpha : dt / (releaseTau + dt);
            m_turningPointLeadLimit += (rawTurningPointLeadLimit - m_turningPointLeadLimit) * alpha;
        }
    } else {
        const float releaseTau = renewedAcceleration ? 0.012F : 0.055F;
        const float releaseAlpha = dt / (releaseTau + dt);
        m_turningPointAuthority += (0.0F - m_turningPointAuthority) * releaseAlpha;
        m_turningPointHorizonLimitSeconds += (0.060F - m_turningPointHorizonLimitSeconds)
            * releaseAlpha;
        m_turningPointLeadLimit += (0.50F - m_turningPointLeadLimit) * releaseAlpha;
    }
    m_turningPointAuthority = std::clamp(m_turningPointAuthority, 0.0F, 1.0F);
    m_turningPointHorizonLimitSeconds = std::clamp(m_turningPointHorizonLimitSeconds, 0.0F, 0.060F);
    m_turningPointLeadLimit = std::clamp(m_turningPointLeadLimit, 0.0F, 0.50F);
    const float turningPointConfidence = m_turningPointAuthority;
    // A raw tighter limit is a correctness floor and applies immediately.
    // The stored constraint then prevents a one-report looser estimate from
    // reopening horizon/lead during the same braking episode.  Once evidence
    // flickers out, its scalar release remains rate-normalized.
    const bool persistentTurningConstraint = m_turningPointAuthority > 0.0001F;
    const float turningPointHorizonLimit = rawTurningPointHorizonLimit > 0.0F
        ? std::min(rawTurningPointHorizonLimit, m_turningPointHorizonLimitSeconds)
        : persistentTurningConstraint ? m_turningPointHorizonLimitSeconds : 0.0F;
    const float turningPointLeadLimit = rawTurningPointLeadLimit > 0.0F
        ? std::min(rawTurningPointLeadLimit, m_turningPointLeadLimit)
        : persistentTurningConstraint ? m_turningPointLeadLimit : 0.0F;
    // Correctness never falls to zero at the UI's 0% setting; the user may
    // tune how proactively we constrain a credible turn, not re-enable a
    // prediction which source braking says cannot occur.
    const float turnProtection = 0.25F + 0.75F * configuration.turningPointProtection;
    const float turnBlend = turningPointConfidence * turnProtection;
    if (turningPointHorizonLimit > 0.0F) {
        const float limitedHorizon = std::min(horizon, turningPointHorizonLimit);
        horizon += (limitedHorizon - horizon) * turnBlend;
    }

    float lead = m_velocity * horizon;
    if (configuration.model == AdaptiveResponseModel::AlphaBetaGamma
        || configuration.model == AdaptiveResponseModel::Auto) {
        // If coherent source braking contradicts an old-direction ABG
        // acceleration estimate, do not let that stale term add lead. The
        // telemetry remains the unmodified estimator value for diagnostics.
        const float accelerationReinforcement = smootherstep(0.0F,
            accelerationHigh * std::max(speed, velocityThreshold),
            std::max(0.0F, m_velocity * m_acceleration));
        const float staleAccelerationSuppression = std::clamp(sourceBrakingEvidence
            * turningPointConfidence * accelerationReinforcement, 0.0F, 1.0F);
        const float accelerationLeadAuthority = configuration.accelerationResponse
            * (1.0F - staleAccelerationSuppression);
        lead += 0.5F * m_acceleration * horizon * horizon * accelerationLeadAuthority;
    }
    const float maximumLead = configuration.maximumLead * std::max(0.10F, confidence);
    if (turningPointLeadLimit > 0.0F) {
        const float credibleLeadLimit = std::min(maximumLead, turningPointLeadLimit);
        const float blendedLeadLimit = maximumLead + (credibleLeadLimit - maximumLead) * turnBlend;
        lead = std::clamp(lead, -blendedLeadLimit, blendedLeadLimit);
    }
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
    result.velocityAuthority = velocityAuthority;
    result.accelerationIntent = accelerationIntent;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.launchIntent = launchIntent;
    result.onsetTarget = onsetTarget;
#endif
    result.onsetAuthority = onsetAuthority;
    result.sustainedEvidence = m_sustainedEvidence;
    result.sustainedAuthority = sustainedAuthority;
    result.motionUrgency = motionUrgency;
    result.horizonExtensionEligibility = horizonExtensionEligibility;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.horizonExtensionTarget = horizonExtensionTarget;
#endif
    result.normalMaximumHorizonSeconds = normalMaximumHorizon;
    result.allowedMaximumHorizonSeconds = allowedMaximumHorizon;
    result.turningPointConfidence = turningPointConfidence;
    result.estimatedTimeToTurnSeconds = estimatedTimeToTurnSeconds;
    result.estimatedRemainingTravel = estimatedRemainingTravel;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.rawTurningPointHorizonLimitSeconds = rawTurningPointHorizonLimit;
    result.rawTurningPointLeadLimit = rawTurningPointLeadLimit;
#endif
    result.turningPointHorizonLimitSeconds = turningPointHorizonLimit;
    result.turningPointLeadLimit = turningPointLeadLimit;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.appliedTurningPointHorizonLimitSeconds = m_turningPointHorizonLimitSeconds;
    result.appliedTurningPointLeadLimit = m_turningPointLeadLimit;
#endif
    result.reacquisitionAuthority = m_reacquisitionAuthority;
    result.motionCoherence = coherence;
    result.sourceUpdatePeriodSeconds = m_sourceUpdatePeriodSeconds;
    result.quietDurationSeconds = m_quietDurationSeconds;
    result.sourceStoppingSeconds = m_sourceStoppingSeconds;
    result.brakingReductionFactor = m_brakingReductionFactor;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.sourceDecayEvidence = static_cast<float>(m_sourceDecayEvidence);
#endif
    result.reversal = reversal;
    result.safetyCancelled = safetyCancellation;
    result.sourceBrakingDetected = sourceBrakingDetected;
#if defined(HOTAS_ADAPTIVE_TEST_DIAGNOSTICS)
    result.meaningfulMeasurement = meaningfulMeasurement;
    result.sourceUpdated = sourceUpdated;
    result.braking = braking;
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
        else if (mode == u"slow coherent waggle"_qs) {
            const float elapsed = static_cast<float>(index) * kSamplePeriodSeconds;
            const float segmentProgress = std::max(0.0F, elapsed - 0.040F) / 0.120F;
            const int segment = static_cast<int>(segmentProgress);
            const float local = segmentProgress - static_cast<float>(segment);
            const float start = segment == 0 ? 0.50F : (segment % 2 == 0 ? 0.26F : 0.74F);
            const float endpoint = segment % 2 == 0 ? 0.74F : 0.26F;
            value = minimum + span * (elapsed < 0.040F ? 0.50F
                : start + (endpoint - start) * smootherstep(0.0F, 1.0F, local));
        } else if (mode == u"slow one-way sweep"_qs) {
            const float elapsed = static_cast<float>(index) * kSamplePeriodSeconds;
            const float progress = smootherstep(0.0F, 1.0F, (elapsed - 0.080F) / 0.560F);
            value = minimum + span * (elapsed < 0.080F ? 0.18F
                : elapsed < 0.640F ? 0.18F + 0.64F * progress : 0.82F);
        } else if (mode == u"small slow correction"_qs) {
            const float elapsed = static_cast<float>(index) * kSamplePeriodSeconds;
            const float progress = smootherstep(0.0F, 1.0F, (elapsed - 0.120F) / 0.360F);
            value = minimum + span * (elapsed < 0.120F ? 0.50F
                : elapsed < 0.480F ? 0.50F + 0.018F * progress : 0.518F);
        } else if (mode == u"extreme turning-point torture"_qs) {
            const float elapsed = static_cast<float>(index) * kSamplePeriodSeconds;
            if (elapsed < 0.060F) value = minimum + span * 0.12F;
            else if (elapsed < 0.400F) value = minimum + span * (0.12F + 0.82F
                * smootherstep(0.0F, 1.0F, (elapsed - 0.060F) / 0.340F));
            else if (elapsed < 0.700F) value = minimum + span * (0.94F - 0.64F
                * smootherstep(0.0F, 1.0F, (elapsed - 0.400F) / 0.300F));
            else value = minimum + span * 0.30F;
        }
        physical.push_back(std::clamp(value, minimum, maximum));
    }
    return physical;
}

} // namespace hotas
