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
        if (timeSeconds <= points[index].timeSeconds + 0.000001F) {
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

void addScenario(std::vector<ScenarioDefinition> &catalog, std::uint32_t masterSeed,
                 ScenarioDefinition scenario)
{
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
    ScenarioDefinition jitter = linear("timing/variable-dt", "timing", -0.65F, 0.65F, 1.0F);
    jitter.variableDt = true;
    jitter.retainTrace = true;
    addScenario(catalog, masterSeed, std::move(jitter));
    return catalog;
}

std::vector<TraceSample> generateTrace(const ScenarioDefinition &scenario)
{
    const std::vector<ControlPoint> points = scenario.points.empty() ? randomHumanPoints(scenario) : scenario.points;
    const int mapperRate = std::max(1, scenario.mapperRateHz);
    const float baseDt = 1.0F / static_cast<float>(mapperRate);
    const int sourceRate = scenario.sourceRateHz <= 0 ? mapperRate : scenario.sourceRateHz;
    const float sourcePeriod = 1.0F / static_cast<float>(std::max(1, sourceRate));
    DeterministicRng rng(scenario.seed);
    std::vector<TraceSample> trace;
    trace.reserve(static_cast<size_t>(scenario.durationSeconds / baseDt) + 3U);
    float time = 0.0F;
    float lastSourceTime = -sourcePeriod;
    float heldPhysical = clampDomain(valueAt(points, 0.0F), scenario.unipolar);
    while (time < scenario.durationSeconds + 0.000001F) {
        const float intended = clampDomain(valueAt(points, time), scenario.unipolar);
        const float velocity = velocityAt(points, time);
        if (time - lastSourceTime + 0.0000001F >= sourcePeriod) {
            lastSourceTime = time;
            heldPhysical = intended;
            switch (scenario.noise) {
            case NoiseModel::WhiteJitter: heldPhysical += rng.between(-scenario.noiseAmplitude, scenario.noiseAmplitude); break;
            case NoiseModel::Quantized:
                heldPhysical = std::round(heldPhysical / std::max(0.0001F, scenario.noiseAmplitude))
                    * std::max(0.0001F, scenario.noiseAmplitude);
                break;
            case NoiseModel::PositiveSpike:
                if (std::abs(time - scenario.durationSeconds * 0.50F) < sourcePeriod) heldPhysical += scenario.noiseAmplitude * 40.0F;
                break;
            case NoiseModel::OppositeSpike:
                if (std::abs(time - scenario.durationSeconds * 0.50F) < sourcePeriod) {
                    heldPhysical -= velocity >= 0.0F ? scenario.noiseAmplitude * 40.0F : -scenario.noiseAmplitude * 40.0F;
                }
                break;
            case NoiseModel::Burst:
                if (time > scenario.durationSeconds * 0.45F && time < scenario.durationSeconds * 0.55F) {
                    heldPhysical += rng.between(-scenario.noiseAmplitude * 8.0F, scenario.noiseAmplitude * 8.0F);
                }
                break;
            case NoiseModel::Drift: heldPhysical += scenario.noiseAmplitude * std::sin(time * 1.2F); break;
            case NoiseModel::None: break;
            }
            heldPhysical = clampDomain(heldPhysical, scenario.unipolar);
        }
        const bool moving = std::abs(velocity) > 0.0005F;
        trace.push_back({time, intended, heldPhysical, velocity, baseDt, moving});
        float dt = baseDt;
        if (scenario.variableDt) {
            constexpr float factors[] = {0.50F, 1.75F, 0.75F, 1.25F, 1.0F, 0.95F, 1.075F};
            dt *= factors[rng.next() % (sizeof(factors) / sizeof(factors[0]))];
        }
        time = std::min(scenario.durationSeconds + baseDt * 0.25F, time + dt);
    }
    return trace;
}

} // namespace hotas::verification
