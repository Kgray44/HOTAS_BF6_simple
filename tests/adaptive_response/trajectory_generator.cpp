#include "verification_harness.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hotas::verification {
namespace {

class DeterministicRng final {
public:
    explicit DeterministicRng(std::uint32_t seed) : m_state(seed == 0 ? 0xA341316CU : seed) {}

    std::uint32_t next()
    {
        std::uint32_t value = m_state;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        m_state = value;
        return value;
    }

    float unit() { return static_cast<float>(next()) / 4294967295.0F; }
    float between(float minimum, float maximum) { return minimum + (maximum - minimum) * unit(); }

private:
    std::uint32_t m_state;
};

std::uint32_t mix(std::uint32_t value, std::uint8_t byte)
{
    value ^= byte;
    value *= 16777619U;
    return value;
}

float clampDomain(float value, bool unipolar)
{
    return std::clamp(value, unipolar ? 0.0F : -1.0F, 1.0F);
}

float valueAt(const std::vector<ControlPoint> &points, float timeSeconds)
{
    if (points.empty()) return 0.0F;
    if (timeSeconds <= points.front().timeSeconds) return points.front().position;
    if (timeSeconds >= points.back().timeSeconds) return points.back().position;
    for (size_t index = 1; index < points.size(); ++index) {
        if (timeSeconds <= points[index].timeSeconds) {
            const ControlPoint &left = points[index - 1];
            const ControlPoint &right = points[index];
            const float span = std::max(0.000001F, right.timeSeconds - left.timeSeconds);
            const float fraction = (timeSeconds - left.timeSeconds) / span;
            return left.position + (right.position - left.position) * fraction;
        }
    }
    return points.back().position;
}

float velocityAt(const std::vector<ControlPoint> &points, float timeSeconds)
{
    if (points.size() < 2) return 0.0F;
    for (size_t index = 1; index < points.size(); ++index) {
        if (timeSeconds < points[index].timeSeconds - 0.000001F) {
            const ControlPoint &left = points[index - 1];
            const ControlPoint &right = points[index];
            return (right.position - left.position) / std::max(0.000001F, right.timeSeconds - left.timeSeconds);
        }
    }
    return 0.0F;
}

std::vector<ControlPoint> randomHumanPoints(const ScenarioDefinition &scenario)
{
    DeterministicRng rng(scenario.seed);
    std::vector<ControlPoint> points;
    const bool evasive = scenario.id.find("combat-helicopter") != std::string::npos;
    float time = 0.0F;
    float position = scenario.unipolar ? rng.between(0.15F, 0.75F) : rng.between(-0.45F, 0.45F);
    points.push_back({time, position});
    while (time < scenario.durationSeconds - 0.001F) {
        const float hold = evasive ? rng.between(0.020F, 0.120F) : rng.between(0.080F, 0.450F);
        time = std::min(scenario.durationSeconds, time + hold);
        float target = position;
        if (evasive) {
            target += rng.between(-0.95F, 0.95F);
        } else if (scenario.id.find("precision") != std::string::npos) {
            target += rng.between(-0.12F, 0.12F);
        } else {
            target += rng.between(-0.55F, 0.55F);
        }
        target = clampDomain(target, scenario.unipolar);
        points.push_back({time, target});
        position = target;
    }
    return points;
}

std::vector<ControlPoint> personaPoints(const ScenarioDefinition &scenario)
{
    struct PersonaLimits {
        float maximumVelocity;
        float maximumAcceleration;
        float maximumJerk;
        float targetRange;
        float holdMinimum;
        float holdMaximum;
        float centerBias;
    };
    const bool combat = scenario.id.find("combat-helicopter") != std::string::npos;
    const bool landing = scenario.id.find("helicopter-landing") != std::string::npos;
    const bool space = scenario.id.find("space-sim") != std::string::npos;
    const bool fixedWing = scenario.id.find("fixed-wing") != std::string::npos;
    const bool noisy = scenario.id.find("noisy-older-sensor") != std::string::npos;
    const PersonaLimits limits = combat ? PersonaLimits{4.5F, 24.0F, 260.0F, 1.0F, 0.020F, 0.160F, 0.10F}
        : landing ? PersonaLimits{0.55F, 2.2F, 18.0F, 0.38F, 0.050F, 0.250F, 0.75F}
        : space ? PersonaLimits{2.0F, 8.0F, 80.0F, 0.95F, 0.080F, 0.450F, 0.25F}
        : fixedWing ? PersonaLimits{1.55F, 5.0F, 42.0F, 0.90F, 0.100F, 0.700F, 0.20F}
        : noisy ? PersonaLimits{1.10F, 4.0F, 32.0F, 0.70F, 0.080F, 0.500F, 0.45F}
        : PersonaLimits{0.42F, 1.5F, 12.0F, 0.30F, 0.180F, 0.900F, 0.82F};
    DeterministicRng rng(scenario.seed);
    constexpr float dt = 0.010F;
    std::vector<ControlPoint> points;
    points.reserve(static_cast<size_t>(scenario.durationSeconds / dt) + 2U);
    float time = 0.0F;
    float position = scenario.unipolar ? 0.50F : rng.between(-0.10F, 0.10F);
    float velocity = 0.0F;
    float acceleration = 0.0F;
    float target = position;
    float targetChangeTime = 0.0F;
    while (time < scenario.durationSeconds + 0.000001F) {
        if (time + 0.000001F >= targetChangeTime || std::abs(target - position) < 0.015F) {
            const float centered = rng.between(-limits.targetRange, limits.targetRange);
            target = centered * (1.0F - limits.centerBias) + rng.between(-0.08F, 0.08F) * limits.centerBias;
            if (combat && rng.unit() < 0.45F) target = -position + rng.between(-0.20F, 0.20F);
            if (scenario.unipolar) target = rng.between(0.05F, 0.95F);
            target = clampDomain(target, scenario.unipolar);
            targetChangeTime = time + rng.between(limits.holdMinimum, limits.holdMaximum);
        }
        const float desiredVelocity = std::clamp((target - position) * 3.2F,
            -limits.maximumVelocity, limits.maximumVelocity);
        const float desiredAcceleration = std::clamp((desiredVelocity - velocity) * 7.0F,
            -limits.maximumAcceleration, limits.maximumAcceleration);
        acceleration += std::clamp(desiredAcceleration - acceleration,
            -limits.maximumJerk * dt, limits.maximumJerk * dt);
        velocity = std::clamp(velocity + acceleration * dt, -limits.maximumVelocity, limits.maximumVelocity);
        position = clampDomain(position + velocity * dt, scenario.unipolar);
        if ((position <= (scenario.unipolar ? 0.0F : -1.0F) || position >= 1.0F)
            && ((position <= (scenario.unipolar ? 0.0F : -1.0F) && velocity < 0.0F)
                || (position >= 1.0F && velocity > 0.0F))) {
            velocity *= -0.12F;
            acceleration = 0.0F;
        }
        points.push_back({std::min(time, scenario.durationSeconds), position});
        time += dt;
    }
    if (points.empty() || points.back().timeSeconds < scenario.durationSeconds) {
        points.push_back({scenario.durationSeconds, position});
    }
    return points;
}

void addScenario(std::vector<ScenarioDefinition> &catalog, std::uint32_t masterSeed,
                 ScenarioDefinition scenario)
{
    if (scenario.trajectoryId.empty()) scenario.trajectoryId = scenario.id;
    scenario.seed = deriveSeed(masterSeed, "canonical", scenario.family,
                               static_cast<std::uint32_t>(catalog.size()), "trajectory");
    catalog.push_back(std::move(scenario));
}

ScenarioDefinition linear(const std::string &id, const std::string &family, float start, float end,
                          float duration, bool unipolar = false)
{
    ScenarioDefinition scenario;
    scenario.id = id;
    scenario.family = family;
    scenario.durationSeconds = duration + 0.50F;
    scenario.points = {{0.0F, start}, {duration, end}, {duration + 0.50F, end}};
    scenario.unipolar = unipolar;
    return scenario;
}

} // namespace

std::uint32_t deriveSeed(std::uint32_t masterSeed, const std::string &campaign,
                         const std::string &family, std::uint32_t scenarioIndex,
                         const std::string &configuration)
{
    std::uint32_t value = 2166136261U ^ masterSeed;
    const auto append = [&value](const std::string &part) {
        for (const char character : part) value = mix(value, static_cast<std::uint8_t>(character));
    };
    append(campaign);
    append(family);
    append(configuration);
    for (int shift = 0; shift < 32; shift += 8) value = mix(value, static_cast<std::uint8_t>(scenarioIndex >> shift));
    return value == 0 ? 0x9E3779B9U : value;
}

std::vector<ScenarioDefinition> canonicalScenarioCatalog(std::uint32_t masterSeed)
{
    std::vector<ScenarioDefinition> catalog;
    for (const auto [name, value] : std::vector<std::pair<std::string, float>>{
             {"center", 0.0F}, {"plus25", 0.25F}, {"minus40", -0.40F},
             {"near-plus-endpoint", 0.98F}, {"near-minus-endpoint", -0.98F}}) {
        ScenarioDefinition scenario;
        scenario.id = "stationary/" + name;
        scenario.family = "stationary";
        scenario.durationSeconds = 2.0F;
        scenario.points = {{0.0F, value}, {2.0F, value}};
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (const float magnitude : {0.01F, 0.02F, 0.05F}) {
        ScenarioDefinition scenario;
        scenario.id = "precision/micro-" + std::to_string(static_cast<int>(magnitude * 100.0F));
        scenario.family = "precision";
        scenario.durationSeconds = 2.0F;
        scenario.points = {{0.0F, 0.0F}, {0.50F, magnitude}, {1.0F, -magnitude}, {1.50F, magnitude}, {2.0F, magnitude}};
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    const std::vector<std::pair<float, float>> directions{{0.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, -1.0F},
        {-1.0F, 0.0F}, {1.0F, -1.0F}, {-1.0F, 1.0F}, {0.80F, 0.20F}, {-0.70F, -0.15F}};
    for (const float duration : {0.25F, 0.50F, 1.0F, 2.0F, 3.0F, 5.0F, 10.0F}) {
        for (size_t index = 0; index < directions.size(); ++index) {
            addScenario(catalog, masterSeed, linear("slow/sweep-" + std::to_string(duration) + "-" + std::to_string(index),
                "slow-motion", directions[index].first, directions[index].second, duration));
        }
    }
    for (const float duration : {0.050F, 0.075F, 0.100F, 0.150F, 0.250F}) {
        addScenario(catalog, masterSeed, linear("fast/full-sweep-" + std::to_string(duration),
            "fast-motion", -0.95F, 0.95F, duration));
    }
    for (const std::vector<ControlPoint> &points : std::vector<std::vector<ControlPoint>>{
             {{0.0F, 0.80F}, {0.20F, 0.50F}, {0.40F, 0.20F}, {0.60F, -0.10F}, {0.80F, -0.40F}, {1.0F, -0.70F}, {1.5F, -0.70F}},
             {{0.0F, 0.90F}, {0.20F, 0.60F}, {0.40F, 0.30F}, {0.60F, 0.55F}, {0.80F, 0.80F}, {1.3F, 0.80F}},
             {{0.0F, -0.90F}, {0.20F, -0.60F}, {0.40F, -0.30F}, {0.60F, -0.55F}, {0.80F, -0.80F}, {1.3F, -0.80F}},
             {{0.0F, 0.70F}, {0.08F, -0.70F}, {0.16F, 0.70F}, {0.24F, -0.70F}, {0.70F, -0.70F}},
             {{0.0F, 0.0F}, {0.15F, 0.50F}, {0.30F, 0.80F}, {0.55F, 0.78F}, {0.80F, 0.65F}, {1.0F, 0.35F}, {1.2F, 0.0F}, {1.7F, 0.0F}}}) {
        ScenarioDefinition scenario;
        scenario.id = "reversal/pattern-" + std::to_string(catalog.size());
        scenario.family = "reversal";
        scenario.durationSeconds = points.back().timeSeconds;
        scenario.points = points;
        scenario.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    ScenarioDefinition falseReversalBait;
    falseReversalBait.id = "reversal/false-bait";
    falseReversalBait.family = "reversal";
    falseReversalBait.durationSeconds = 1.2F;
    falseReversalBait.points = {{0.0F, 0.30F}, {0.20F, 0.35F}, {0.40F, 0.40F}, {0.60F, 0.399F},
        {0.80F, 0.45F}, {1.0F, 0.50F}, {1.2F, 0.50F}};
    falseReversalBait.retainTrace = true;
    addScenario(catalog, masterSeed, std::move(falseReversalBait));
    for (const float stopAt : {-0.60F, 0.0F, 0.50F, 0.98F}) {
        ScenarioDefinition scenario = linear("stop/at-" + std::to_string(stopAt), "stop", -0.85F, stopAt, 0.35F);
        scenario.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (const float frequency : {1.0F, 5.0F, 10.0F, 20.0F, 30.0F}) {
        ScenarioDefinition scenario;
        scenario.id = "oscillation/sine-" + std::to_string(frequency) + "hz";
        scenario.family = "oscillation";
        scenario.durationSeconds = 1.5F;
        for (int index = 0; index <= 120; ++index) {
            const float time = scenario.durationSeconds * static_cast<float>(index) / 120.0F;
            scenario.points.push_back({time, 0.45F * std::sin(time * frequency * 2.0F * std::numbers::pi_v<float>)});
        }
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (const int mapperRate : {125, 200, 250, 500, 1000}) {
        for (const int sourceRate : {30, 60, 100, 125, 200, 250, 500}) {
            ScenarioDefinition scenario = linear("sample-hold/mapper-" + std::to_string(mapperRate)
                + "-source-" + std::to_string(sourceRate), "sample-hold", 0.70F, 0.20F, 1.0F);
            scenario.mapperRateHz = mapperRate;
            scenario.sourceRateHz = sourceRate;
            scenario.retainTrace = mapperRate == 250 && (sourceRate == 60 || sourceRate == 30);
            addScenario(catalog, masterSeed, std::move(scenario));
        }
    }
    for (const NoiseModel noise : {NoiseModel::WhiteJitter, NoiseModel::Quantized, NoiseModel::PositiveSpike,
                                   NoiseModel::OppositeSpike, NoiseModel::Burst, NoiseModel::Drift}) {
        ScenarioDefinition scenario;
        scenario.id = "noise/model-" + std::to_string(static_cast<int>(noise));
        scenario.family = "noise";
        scenario.durationSeconds = 2.0F;
        scenario.points = {{0.0F, 0.30F}, {2.0F, 0.30F}};
        scenario.noise = noise;
        scenario.noiseAmplitude = noise == NoiseModel::Quantized ? 0.01F : 0.005F;
        scenario.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (const NoiseModel noise : {NoiseModel::WhiteJitter, NoiseModel::Quantized, NoiseModel::OppositeSpike}) {
        ScenarioDefinition moving = linear("noise-moving/slow-sweep-" + std::to_string(static_cast<int>(noise)),
            "noise-moving", 1.0F, 0.0F, 3.0F);
        moving.noise = noise;
        moving.noiseAmplitude = noise == NoiseModel::Quantized ? 0.01F : 0.002F;
        moving.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(moving));
    }
    for (const float amplitude : {0.0005F, 0.001F, 0.002F, 0.005F, 0.010F}) {
        for (const float frequency : {3.0F, 5.0F, 10.0F, 15.0F, 20.0F, 30.0F}) {
            ScenarioDefinition wobble;
            wobble.id = "human-wobble/slow3s-a" + std::to_string(amplitude) + "-f" + std::to_string(frequency);
            wobble.family = "human-wobble";
            wobble.durationSeconds = 3.5F;
            for (int index = 0; index <= 350; ++index) {
                const float time = static_cast<float>(index) * 0.01F;
                const float base = time <= 3.0F ? 1.0F - time / 3.0F : 0.0F;
                const float variation = time <= 3.0F ? amplitude * std::sin(time * frequency
                    * 2.0F * std::numbers::pi_v<float>) : 0.0F;
                wobble.points.push_back({time, std::clamp(base + variation, -1.0F, 1.0F)});
            }
            wobble.retainTrace = amplitude == 0.002F && frequency == 10.0F;
            addScenario(catalog, masterSeed, std::move(wobble));
        }
    }
    for (const std::vector<ControlPoint> &points : std::vector<std::vector<ControlPoint>>{
             {{0.0F, -0.8F}, {0.5F, -0.6F}, {1.0F, 0.0F}, {1.5F, 0.6F}, {2.0F, 0.8F}, {2.5F, 0.8F}},
             {{0.0F, -0.8F}, {0.5F, -0.1F}, {1.0F, 0.5F}, {1.5F, 0.8F}, {2.0F, 0.8F}},
             {{0.0F, 0.8F}, {0.4F, 0.7F}, {0.8F, 0.3F}, {1.2F, -0.1F}, {1.6F, -0.6F}, {2.0F, -0.8F}, {2.5F, -0.8F}}}) {
        ScenarioDefinition acceleration;
        acceleration.id = "acceleration/fixture-" + std::to_string(catalog.size());
        acceleration.family = "acceleration";
        acceleration.durationSeconds = points.back().timeSeconds;
        acceleration.points = points;
        acceleration.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(acceleration));
    }
    for (const float duration : {1.0F, 3.0F}) {
        ScenarioDefinition scenario = linear("one-sided/throttle-" + std::to_string(duration),
            "one-sided", 0.0F, 1.0F, duration, true);
        scenario.retainTrace = true;
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (int index = 0; index < 16; ++index) {
        ScenarioDefinition scenario;
        scenario.id = index < 8 ? "randomized/precision-pilot-" + std::to_string(index)
                                : "randomized/combat-helicopter-" + std::to_string(index);
        scenario.family = "randomized";
        scenario.durationSeconds = index < 8 ? 5.0F : 3.0F;
        scenario.retainTrace = index == 8;
        addScenario(catalog, masterSeed, std::move(scenario));
    }
    for (const std::string &persona : {"precision-pilot", "fixed-wing", "helicopter-landing", "combat-helicopter",
                                       "space-sim", "noisy-older-sensor"}) {
        for (int index = 0; index < 4; ++index) {
            ScenarioDefinition scenario;
            scenario.id = "persona/" + persona + "-" + std::to_string(index);
            scenario.family = "persona";
            scenario.durationSeconds = persona == "combat-helicopter" ? 3.0F : 5.0F;
            scenario.noise = persona == "noisy-older-sensor" ? NoiseModel::Quantized : NoiseModel::None;
            scenario.noiseAmplitude = persona == "noisy-older-sensor" ? 0.005F : 0.0F;
            scenario.retainTrace = index == 0;
            addScenario(catalog, masterSeed, std::move(scenario));
        }
    }
    ScenarioDefinition jitter = linear("timing/variable-dt", "timing", -0.65F, 0.65F, 1.0F);
    jitter.variableDt = true;
    jitter.retainTrace = true;
    addScenario(catalog, masterSeed, std::move(jitter));
    return catalog;
}

std::vector<TraceSample> generateTrace(const ScenarioDefinition &scenario)
{
    const std::vector<ControlPoint> points = scenario.points.empty()
        ? (scenario.family == "persona" ? personaPoints(scenario) : randomHumanPoints(scenario)) : scenario.points;
    const int mapperRate = std::max(1, scenario.mapperRateHz);
    const float baseDt = 1.0F / static_cast<float>(mapperRate);
    const int sourceRate = scenario.sourceRateHz <= 0 ? mapperRate : scenario.sourceRateHz;
    const float sourcePeriod = 1.0F / static_cast<float>(std::max(1, sourceRate));
    DeterministicRng rng(scenario.seed);
    std::vector<TraceSample> trace;
    trace.reserve(static_cast<size_t>(scenario.durationSeconds / baseDt * 1.5F) + 3U);
    float time = 0.0F;
    std::uint64_t mapperTick = 0;
    float nextSourceSampleTime = 0.0F;
    float lastSourceSampleTime = 0.0F;
    float heldPhysical = clampDomain(valueAt(points, 0.0F), scenario.unipolar);
    float previousVelocity = 0.0F;
    int previousIntentDirection = 0;
    bool previousMoving = false;
    while (time <= scenario.durationSeconds + 0.000001F) {
        const float intended = clampDomain(valueAt(points, time), scenario.unipolar);
        const float velocity = velocityAt(points, time);
        bool sourceSampleUpdated = false;
        float latestSourceTime = lastSourceSampleTime;
        // The physical source has its own cadence.  Mapper ticks only observe
        // the latest source sample; they never retime that source clock.
        while (nextSourceSampleTime <= time + 0.0000001F) {
            latestSourceTime = nextSourceSampleTime;
            lastSourceSampleTime = nextSourceSampleTime;
            heldPhysical = clampDomain(valueAt(points, nextSourceSampleTime), scenario.unipolar);
            const float sourceVelocity = velocityAt(points, nextSourceSampleTime);
            switch (scenario.noise) {
            case NoiseModel::WhiteJitter: heldPhysical += rng.between(-scenario.noiseAmplitude, scenario.noiseAmplitude); break;
            case NoiseModel::Quantized:
                heldPhysical = std::round(heldPhysical / std::max(0.0001F, scenario.noiseAmplitude))
                    * std::max(0.0001F, scenario.noiseAmplitude);
                break;
            case NoiseModel::PositiveSpike:
                if (std::abs(nextSourceSampleTime - scenario.durationSeconds * 0.50F) < sourcePeriod * 0.5F) {
                    heldPhysical += scenario.noiseAmplitude * 40.0F;
                }
                break;
            case NoiseModel::OppositeSpike:
                if (std::abs(nextSourceSampleTime - scenario.durationSeconds * 0.50F) < sourcePeriod * 0.5F) {
                    heldPhysical -= sourceVelocity >= 0.0F ? scenario.noiseAmplitude * 40.0F : -scenario.noiseAmplitude * 40.0F;
                }
                break;
            case NoiseModel::Burst:
                if (nextSourceSampleTime > scenario.durationSeconds * 0.45F
                    && nextSourceSampleTime < scenario.durationSeconds * 0.55F) {
                    heldPhysical += rng.between(-scenario.noiseAmplitude * 8.0F, scenario.noiseAmplitude * 8.0F);
                }
                break;
            case NoiseModel::Drift: heldPhysical += scenario.noiseAmplitude * std::sin(nextSourceSampleTime * 1.2F); break;
            case NoiseModel::None: break;
            }
            heldPhysical = clampDomain(heldPhysical, scenario.unipolar);
            sourceSampleUpdated = true;
            nextSourceSampleTime += sourcePeriod;
        }
        const int intentDirection = velocity > 0.0005F ? 1 : velocity < -0.0005F ? -1 : 0;
        const bool moving = intentDirection != 0;
        float dt = baseDt;
        if (scenario.variableDt) {
            constexpr float factors[] = {0.50F, 1.75F, 0.75F, 1.25F, 1.0F, 0.95F, 1.075F};
            dt *= factors[rng.next() % (sizeof(factors) / sizeof(factors[0]))];
        }
        const float nextTime = std::min(scenario.durationSeconds, time + dt);
        const float actualDt = std::max(0.0F, nextTime - time);
        const bool reversal = intentDirection != 0 && previousIntentDirection != 0
            && intentDirection != previousIntentDirection;
        TraceSample sample;
        sample.timeSeconds = time;
        sample.intended = intended;
        sample.humanIntentVelocity = velocity;
        sample.humanIntentAcceleration = actualDt <= 0.0F ? 0.0F : (velocity - previousVelocity) / actualDt;
        sample.humanIntentDirection = intentDirection;
        sample.humanIntentReversal = reversal;
        sample.humanIntentStop = !moving && previousMoving;
        sample.physical = heldPhysical;
        sample.velocity = velocity;
        sample.dtSeconds = actualDt;
        sample.intendedMoving = moving;
        sample.sourceSampleUpdated = sourceSampleUpdated;
        sample.sourceSampleTimeSeconds = latestSourceTime;
        sample.targetArrival = !moving && previousMoving;
        sample.physicalStop = !moving && previousMoving;
        sample.trueReversal = reversal;
        trace.push_back(sample);
        if (actualDt <= 0.0F) break;
        previousVelocity = velocity;
        if (intentDirection != 0) previousIntentDirection = intentDirection;
        previousMoving = moving;
        if (scenario.variableDt) {
            time = nextTime;
        } else {
            ++mapperTick;
            time = std::min(scenario.durationSeconds, static_cast<float>(mapperTick) * baseDt);
        }
    }
    return trace;
}

} // namespace hotas::verification
