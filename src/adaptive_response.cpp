#include "adaptive_response.h"

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {

constexpr float kMinimumPredictionDeltaSeconds = 0.0005F;
constexpr float kMaximumPredictionDeltaSeconds = 0.050F;
constexpr float kMaximumEstimatedVelocity = 64.0F;
constexpr float kMaximumEstimatedAcceleration = 4096.0F;

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
    m_estimatedPosition = 0.0F;
    m_velocity = 0.0F;
    m_acceleration = 0.0F;
    m_lastTimestamp = {};
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
        m_estimatedPosition = physical;
        m_lastTimestamp = timestamp;
        result.estimated = physical;
        result.predicted = physical;
        return result;
    }
    float dt = static_cast<float>(std::chrono::duration<double>(timestamp - m_lastTimestamp).count());
    dt = std::clamp(dt, kMinimumPredictionDeltaSeconds, kMaximumPredictionDeltaSeconds);
    const float measuredDelta = physical - m_lastPhysical;
    const float instantaneousVelocity = measuredDelta / dt;
    const bool stationaryMeasurement = std::abs(measuredDelta) <= configuration.noiseRejection;
    const float priorVelocity = m_velocity;
    const bool directionalChange = instantaneousVelocity * priorVelocity < 0.0F;
    const bool reversal = directionalChange
        && std::abs(instantaneousVelocity) >= configuration.reversalDetection
        && std::abs(priorVelocity) >= configuration.reversalDetection * 0.50F;

    // Keep small sensor noise out of the motion model, never out of the
    // physical baseline. The direct positional path below remains untouched.
    const float measurementVelocity = stationaryMeasurement
        ? 0.0F : instantaneousVelocity;
    if (reversal) {
        // A reversal is a session-local state boundary for prediction. Keep
        // the physical baseline direct and reacquire the new motion estimate
        // without carrying stale velocity or acceleration across it.
        m_estimatedPosition = physical;
        m_velocity = measurementVelocity * std::max(0.60F, configuration.reversalResponse);
        m_acceleration = 0.0F;
        ++m_reversalCount;
    } else if (configuration.model == AdaptiveResponseModel::Velocity) {
        // Velocity mode deliberately remains a lightweight derivative
        // predictor. Velocity Response controls its actual smoothing gain.
        const float gain = std::clamp(0.06F + configuration.velocityResponse * 0.90F,
                                      0.06F, 0.96F);
        m_velocity += (measurementVelocity - m_velocity) * gain;
        m_estimatedPosition = physical;
        m_acceleration = 0.0F;
    } else {
        // Proper Alpha-Beta / Alpha-Beta-Gamma estimators. Predict the state,
        // form an innovation against this physical measurement, then correct
        // x/v(/a). Auto keeps the ABG architecture and only adapts gains.
        const bool usesAcceleration = configuration.model == AdaptiveResponseModel::AlphaBetaGamma
            || configuration.model == AdaptiveResponseModel::Auto;
        const float predictedPosition = m_estimatedPosition + m_velocity * dt
            + (usesAcceleration ? 0.5F * m_acceleration * dt * dt : 0.0F);
        const float predictedVelocity = m_velocity + (usesAcceleration ? m_acceleration * dt : 0.0F);
        const float innovation = physical - predictedPosition;
        const float motion = std::clamp(std::max(std::abs(predictedVelocity),
                                                  std::abs(measurementVelocity))
                                             / std::max(0.02F, configuration.motionSensitivity * 18.0F),
                                         0.0F, 1.0F);
        const float adaptation = configuration.model == AdaptiveResponseModel::Auto
            ? 0.35F + motion * 0.65F : 1.0F;
        const float alpha = std::clamp((0.12F + configuration.velocityResponse * 0.68F)
                                           * adaptation, 0.08F, 0.92F);
        const float beta = std::clamp((0.006F + configuration.velocityResponse * 0.095F)
                                          * adaptation, 0.003F, 0.12F);
        m_estimatedPosition = predictedPosition + alpha * innovation;
        m_velocity = predictedVelocity + (beta / dt) * innovation;
        if (usesAcceleration) {
            const float gammaBase = 0.00025F + configuration.accelerationResponse * 0.0080F;
            const float gamma = std::clamp(gammaBase * adaptation, 0.0001F, 0.010F);
            m_acceleration += (2.0F * gamma / (dt * dt)) * innovation;
        } else {
            m_acceleration = 0.0F;
        }
    }
    m_velocity = std::clamp(m_velocity, -kMaximumEstimatedVelocity, kMaximumEstimatedVelocity);
    m_acceleration = std::clamp(m_acceleration, -kMaximumEstimatedAcceleration,
                                kMaximumEstimatedAcceleration);
    // A repeated physical sample is an explicit stop. The estimator may keep
    // its telemetry position, but no stale derivative is allowed to produce
    // an offset from the direct physical baseline on that report.
    if (stationaryMeasurement) {
        m_velocity = 0.0F;
        m_acceleration = 0.0F;
    }
    const float speed = std::abs(m_velocity);
    const bool decelerating = !reversal && (m_velocity * priorVelocity > 0.0F)
        && speed < std::abs(priorVelocity) * 0.88F;
    const float microCutoff = std::max(configuration.noiseRejection, configuration.motionSensitivity * 0.50F);
    const float intensity = std::clamp((speed - microCutoff) / (configuration.motionSensitivity * 18.0F),
                                       0.0F, 1.0F);
    const bool settling = !reversal && (decelerating || speed <= configuration.motionSensitivity);
    float confidence = intensity;
    if (decelerating) confidence *= 1.0F - configuration.decelerationResponse * 0.72F;
    if (settling) {
        const float settle = std::clamp(configuration.settlingResponse, 0.0F, 1.0F);
        confidence *= 1.0F - settle * 0.80F;
        m_velocity *= 1.0F - settle * 0.45F;
        m_acceleration *= 1.0F - settle * 0.65F;
    }
    if (reversal) confidence = std::max(confidence, 0.42F * configuration.reversalResponse);
    if (speed <= configuration.noiseRejection) {
        m_velocity = 0.0F;
        m_acceleration = 0.0F;
        confidence = 0.0F;
    }
    float horizon = configuration.maximumHorizonSeconds * intensity * confidence;
    if (decelerating) horizon *= 1.0F - configuration.decelerationResponse * 0.65F;
    if (settling) horizon *= 1.0F - configuration.settlingResponse * 0.88F;
    if (reversal) horizon *= std::max(0.55F, configuration.reversalResponse);
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
    result.safetyLimited = std::abs(unclampedLead - lead) > 0.00001F || bounded != predicted;
    if (result.safetyLimited) ++m_safetyClampCount;
    result.estimated = m_estimatedPosition;
    result.predicted = bounded;
    result.velocity = m_velocity;
    result.acceleration = m_acceleration;
    result.activeHorizonSeconds = horizon;
    result.lead = bounded - physical;
    result.confidence = confidence;
    result.motionIntensity = intensity;
    result.reversal = reversal;
    result.state = reversal ? AdaptiveMotionState::Reversing
        : speed <= configuration.noiseRejection ? AdaptiveMotionState::Stable
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

} // namespace hotas
