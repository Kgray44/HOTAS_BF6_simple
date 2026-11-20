#include "verification_harness.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

// These identities are fixed before any candidate is generated.  They are
// deliberately disjoint from every Phase 1/2/3 seed, including the now
// consumed 0x23A10003 holdout.
constexpr std::uint32_t kSearchASeed = 0x2B100101U;
constexpr std::uint32_t kSearchBSeed = 0x2B100102U;
constexpr std::array<std::uint32_t, 4> kValidationSeeds{{
    0x2B100201U, 0x2B100202U, 0x2B100203U, 0x2B100204U,
}};
constexpr std::uint32_t kFinalHoldoutSeed = 0x2B10F00DU;
constexpr const char *kCorpusGeneratorVersion = "phase2b-persona-structured-v1";
constexpr size_t kSearchScenarios = 160U;
constexpr size_t kValidationScenarios = 130U;
constexpr size_t kFinalHoldoutScenarios = 200U;

struct Constraint {
    double minimumAdvanceMs;
    double maximumMaeRatio;
    double maximumP95Ratio;
    double maximumP99Ratio;
    double maximumNoiseRatio;
    // A finalist must meet each fold's relaxed ceiling as well as the exact
    // aggregate ceiling.  This permits ordinary seed variation, never a
    // collapse on one fold.
    double individualFoldTolerance = 1.015;
};

struct Spec {
    std::string name;
    float horizonLow;
    float horizonHigh;
    Constraint constraint;
};

struct Metrics {
    std::uint64_t samples = 0;
    double mae = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double advanceMs = 0.0;
    double usefulLead = 0.0;
    double wrongArea = 0.0;
    double pattern73Area = 0.0;
    double reacquisitionMs = 0.0;
    double stopResidual = 0.0;
    double stationaryRms = 0.0;
    double noiseAmplification = 0.0;
    double artificialStep = 0.0;
    double clampFraction = 0.0;
    double capFraction = 0.0;
    double inactiveFraction = 0.0;
    double protectionFraction = 0.0;
    double meanHorizonMs = 0.0;
    double p95HorizonMs = 0.0;
    double meanExtensionMs = 0.0;
    double maxExtensionMs = 0.0;
    std::uint64_t nonFinite = 0;
    std::uint64_t illegalOutput = 0;
    std::uint64_t stationaryOffset = 0;
};

struct Candidate {
    std::string id;
    std::string preset;
    std::string stage;
    AdaptiveResponseSettings settings;
    Metrics searchA;
    Metrics searchB;
    Metrics aggregate;
    bool searchBRan = false;
    bool eligible = false;
    std::string reason;
};

struct Trace {
    QString name;
    std::vector<float> times;
    std::vector<float> values;
};

class Rng final {
public:
    explicit Rng(std::uint32_t seed) : m_value(seed ? seed : 0xA341316CU) {}
    float unit()
    {
        m_value ^= m_value << 13U;
        m_value ^= m_value >> 17U;
        m_value ^= m_value << 5U;
        return static_cast<float>(m_value) / 4294967295.0F;
    }
    float range(float low, float high) { return low + (high - low) * unit(); }
private:
    std::uint32_t m_value;
};

double clipped(double value) { return std::clamp(value, 0.0, 1.0); }

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * (values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

bool writeText(const QString &path, const std::string &contents)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    output.write(contents.data(), static_cast<qint64>(contents.size()));
    return output.commit();
}

QString csv(const std::string &value)
{
    QString escaped = QString::fromStdString(value);
    escaped.replace('"', "\"\"");
    return u"\""_qs + escaped + u"\""_qs;
}

RuntimeAdaptiveResponseConfig toRuntime(AdaptiveResponseSettings settings)
{
    settings = sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = settings.enabled;
    result.model = AdaptiveResponseModel::Auto;
    result.maximumHorizonSeconds = settings.maximumHorizonMs / 1000.0F;
    result.maximumLead = settings.maximumLead;
    result.velocityResponse = settings.velocityResponse;
    result.accelerationResponse = settings.accelerationResponse;
    result.motionSensitivity = settings.motionSensitivity;
    result.noiseRejection = settings.noiseRejection;
    result.reversalDetection = settings.reversalDetection;
    result.reversalResponse = settings.reversalResponse;
    result.decelerationResponse = settings.decelerationResponse;
    result.settlingResponse = settings.settlingResponse;
    result.endpointTaper = settings.endpointTaper;
    result.onsetAssist = settings.onsetAssist;
    result.onsetCap = settings.onsetCap;
    result.sustainedAssist = settings.sustainedAssist;
    result.sustainedCap = settings.sustainedCap;
    result.horizonExtension = settings.horizonExtension;
    result.horizonExtensionCapSeconds = settings.horizonExtensionCapMs / 1000.0F;
    result.turningPointProtection = settings.turningPointProtection;
    result.turningPointMargin = settings.turningPointMargin;
    return result;
}

float intendedAt(const std::vector<TraceSample> &trace, float time)
{
    if (trace.empty()) return 0.0F;
    if (time <= trace.front().timeSeconds) return trace.front().intended;
    if (time >= trace.back().timeSeconds) return trace.back().intended;
    const auto upper = std::lower_bound(trace.cbegin(), trace.cend(), time,
        [](const TraceSample &sample, float value) { return sample.timeSeconds < value; });
    if (upper == trace.cbegin()) return upper->intended;
    const TraceSample &right = *upper;
    const TraceSample &left = *(upper - 1);
    const float span = std::max(0.000001F, right.timeSeconds - left.timeSeconds);
    const float fraction = (time - left.timeSeconds) / span;
    return left.intended + (right.intended - left.intended) * fraction;
}

Metrics analyze(const std::vector<ScenarioDefinition> &scenarios, const AdaptiveResponseSettings &settings)
{
    std::vector<double> errors, advances, reacquisitions, horizons;
    std::vector<Metrics> perScenario;
    const RuntimeAdaptiveResponseConfig runtime = toRuntime(settings);
    for (const ScenarioDefinition &scenario : scenarios) {
        const ScenarioResult result = replayScenario(scenario, runtime, "phase2", true);
        Metrics row;
        std::uint64_t moving = 0, inactive = 0, clamped = 0, protectedSamples = 0, capped = 0;
        double errorSum = 0.0, leadSum = 0.0, horizonSum = 0.0, stationarySquares = 0.0;
        std::uint64_t stationary = 0;
        for (size_t index = 0; index < result.trace.size() && index < result.telemetry.size(); ++index) {
            const TraceSample &sample = result.trace[index];
            const AdaptiveResponseTelemetry &telemetry = result.telemetry[index];
            ++row.samples;
            if (!std::isfinite(telemetry.predicted) || !std::isfinite(telemetry.lead)) ++row.nonFinite;
            if (telemetry.predicted < -1.0001F || telemetry.predicted > 1.0001F) ++row.illegalOutput;
            const float future = intendedAt(result.trace, sample.timeSeconds + telemetry.activeHorizonSeconds);
            const double error = std::abs(static_cast<double>(telemetry.predicted) - future);
            errors.push_back(error);
            errorSum += error;
            const double horizonMs = telemetry.activeHorizonSeconds * 1000.0;
            horizons.push_back(horizonMs);
            horizonSum += horizonMs;
            row.maxExtensionMs = std::max(row.maxExtensionMs,
                std::max(0.0, horizonMs - static_cast<double>(settings.maximumHorizonMs)));
            if (std::abs(sample.humanIntentVelocity) > 0.08F) {
                ++moving;
                if (std::abs(telemetry.lead) < 0.00001F) ++inactive;
                const double advance = std::abs(telemetry.lead / sample.humanIntentVelocity) * 1000.0;
                if (advance < 100.0) advances.push_back(advance);
                if (telemetry.lead * sample.humanIntentVelocity < 0.0F)
                    row.wrongArea += std::abs(telemetry.lead) * sample.dtSeconds;
            }
            if (!sample.intendedMoving) {
                stationarySquares += telemetry.lead * telemetry.lead;
                ++stationary;
            }
            leadSum += std::abs(telemetry.lead);
            if (telemetry.safetyLimited) ++clamped;
            if (telemetry.turningPointConfidence > 0.10F) ++protectedSamples;
            if (settings.horizonExtensionCapMs > 0.0F
                && horizonMs >= (settings.maximumHorizonMs + settings.horizonExtensionCapMs) * 0.98F) ++capped;
            if (index > 0U) {
                row.artificialStep = std::max(row.artificialStep,
                    std::abs(static_cast<double>(telemetry.predicted - result.telemetry[index - 1U].predicted)
                           - static_cast<double>(sample.physical - result.trace[index - 1U].physical)));
            }
        }
        for (const ReversalEvent &event : result.reversalEvents) {
            if (event.reacquisitionTimeSeconds >= event.groundTruthTimeSeconds)
                reacquisitions.push_back((event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0);
        }
        row.mae = row.samples ? errorSum / row.samples : 0.0;
        row.usefulLead = row.samples ? leadSum / row.samples : 0.0;
        row.meanHorizonMs = row.samples ? horizonSum / row.samples : 0.0;
        row.meanExtensionMs = std::max(0.0, row.meanHorizonMs - settings.maximumHorizonMs);
        row.stationaryRms = stationary ? std::sqrt(stationarySquares / stationary) : 0.0;
        row.noiseAmplification = result.metrics.noiseAmplificationRatio;
        // Use the verification harness's predictor-only discontinuity metric.
        // The local delta diagnostic above is retained while replaying, but a
        // held device source update is not itself an artificial predictor step.
        row.artificialStep = result.metrics.maximumOutputStep;
        // The harness distinguishes persistent stationary drift from the
        // short settling envelope that is expected immediately after motion.
        // Reuse that established semantic rather than treating any transient
        // stationary lead sample as a hard product failure.
        row.stationaryOffset = result.metrics.stationaryDrift;
        row.stopResidual = std::abs(result.metrics.leadAtPhysicalStop);
        row.clampFraction = row.samples ? static_cast<double>(clamped) / row.samples : 0.0;
        row.capFraction = row.samples ? static_cast<double>(capped) / row.samples : 0.0;
        row.inactiveFraction = moving ? static_cast<double>(inactive) / moving : 0.0;
        row.protectionFraction = row.samples ? static_cast<double>(protectedSamples) / row.samples : 0.0;
        if (scenario.id.find("pattern-73") != std::string::npos) {
            // Pattern-73 qualification is event-scoped: only post-reversal
            // stale lead belongs in the sentinel.  The generic sign-area is a
            // broad diagnostic and also includes normal pre-detection motion.
            row.pattern73Area = result.metrics.wrongDirectionLeadArea;
        }
        perScenario.push_back(row);
    }
    Metrics aggregate;
    for (const Metrics &row : perScenario) {
        aggregate.samples += row.samples;
        aggregate.mae += row.mae;
        aggregate.usefulLead += row.usefulLead;
        aggregate.wrongArea += row.wrongArea;
        aggregate.pattern73Area = std::max(aggregate.pattern73Area, row.pattern73Area);
        aggregate.stopResidual += row.stopResidual;
        aggregate.stationaryRms += row.stationaryRms;
        aggregate.noiseAmplification += row.noiseAmplification;
        aggregate.artificialStep = std::max(aggregate.artificialStep, row.artificialStep);
        aggregate.clampFraction += row.clampFraction;
        aggregate.capFraction += row.capFraction;
        aggregate.inactiveFraction += row.inactiveFraction;
        aggregate.protectionFraction += row.protectionFraction;
        aggregate.meanHorizonMs += row.meanHorizonMs;
        aggregate.meanExtensionMs += row.meanExtensionMs;
        aggregate.maxExtensionMs = std::max(aggregate.maxExtensionMs, row.maxExtensionMs);
        aggregate.nonFinite += row.nonFinite;
        aggregate.illegalOutput += row.illegalOutput;
        aggregate.stationaryOffset += row.stationaryOffset;
    }
    const double count = std::max<size_t>(1, perScenario.size());
    aggregate.mae /= count; aggregate.usefulLead /= count; aggregate.wrongArea /= count;
    aggregate.stopResidual /= count; aggregate.stationaryRms /= count; aggregate.noiseAmplification /= count;
    aggregate.clampFraction /= count; aggregate.capFraction /= count; aggregate.inactiveFraction /= count;
    aggregate.protectionFraction /= count; aggregate.meanHorizonMs /= count; aggregate.meanExtensionMs /= count;
    aggregate.p95 = percentile(errors, 0.95);
    aggregate.p99 = percentile(errors, 0.99);
    aggregate.advanceMs = percentile(advances, 0.50);
    aggregate.reacquisitionMs = percentile(reacquisitions, 0.50);
    aggregate.p95HorizonMs = percentile(horizons, 0.95);
    return aggregate;
}

std::optional<AdaptiveResponseSettings> baselineFor(const std::string &name)
{
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets())
        if (preset.name.toStdString() == name) return preset.axes[0].settings;
    return std::nullopt;
}

Metrics combine(const Metrics &left, const Metrics &right)
{
    // Both inputs have equal scenario populations.  The sampled percentile
    // approximation is intentionally conservative: the worse tail is kept.
    Metrics result;
    result.samples = left.samples + right.samples;
    result.mae = (left.mae + right.mae) * .5;
    result.p95 = std::max(left.p95, right.p95);
    result.p99 = std::max(left.p99, right.p99);
    result.advanceMs = std::min(left.advanceMs, right.advanceMs);
    result.usefulLead = (left.usefulLead + right.usefulLead) * .5;
    result.wrongArea = (left.wrongArea + right.wrongArea) * .5;
    result.pattern73Area = std::max(left.pattern73Area, right.pattern73Area);
    result.reacquisitionMs = std::max(left.reacquisitionMs, right.reacquisitionMs);
    result.stopResidual = std::max(left.stopResidual, right.stopResidual);
    result.stationaryRms = std::max(left.stationaryRms, right.stationaryRms);
    result.noiseAmplification = std::max(left.noiseAmplification, right.noiseAmplification);
    result.artificialStep = std::max(left.artificialStep, right.artificialStep);
    result.clampFraction = (left.clampFraction + right.clampFraction) * .5;
    result.capFraction = (left.capFraction + right.capFraction) * .5;
    result.inactiveFraction = (left.inactiveFraction + right.inactiveFraction) * .5;
    result.protectionFraction = (left.protectionFraction + right.protectionFraction) * .5;
    result.meanHorizonMs = (left.meanHorizonMs + right.meanHorizonMs) * .5;
    result.p95HorizonMs = std::max(left.p95HorizonMs, right.p95HorizonMs);
    result.meanExtensionMs = (left.meanExtensionMs + right.meanExtensionMs) * .5;
    result.maxExtensionMs = std::max(left.maxExtensionMs, right.maxExtensionMs);
    result.nonFinite = left.nonFinite + right.nonFinite;
    result.illegalOutput = left.illegalOutput + right.illegalOutput;
    result.stationaryOffset = left.stationaryOffset + right.stationaryOffset;
    return result;
}

double ratio(double value, double baseline)
{
    if (std::abs(baseline) < 0.000000001) return std::abs(value) < 0.000000001 ? 1.0 : std::numeric_limits<double>::infinity();
    return value / baseline;
}

std::string absoluteFailure(const Metrics &metrics)
{
    if (metrics.nonFinite || metrics.illegalOutput) return "non-finite-or-illegal-output";
    if (metrics.stationaryOffset) return "persistent-stationary-offset";
    if (metrics.pattern73Area > .0001) return "pattern73-wrong-direction-area";
    if (metrics.stopResidual > .12) return "stop-residual";
    if (metrics.reacquisitionMs > 100.0) return "reversal-reacquisition";
    return {};
}

std::string relativeFailure(const Metrics &candidate, const Metrics &baseline, const Constraint &constraint,
                            bool isIndividualFold)
{
    if (candidate.advanceMs - baseline.advanceMs < constraint.minimumAdvanceMs)
        return "temporal-advance";
    const double allowance = isIndividualFold ? constraint.individualFoldTolerance : 1.0;
    if (ratio(candidate.mae, baseline.mae) > constraint.maximumMaeRatio * allowance) return "future-mae-ratio";
    if (ratio(candidate.p95, baseline.p95) > constraint.maximumP95Ratio * allowance) return "p95-ratio";
    if (ratio(candidate.p99, baseline.p99) > constraint.maximumP99Ratio * allowance) return "p99-ratio";
    if (ratio(candidate.noiseAmplification, baseline.noiseAmplification) > constraint.maximumNoiseRatio * allowance)
        return "noise-ratio";
    return {};
}

Candidate assessCandidate(const std::string &id, const std::string &preset, const std::string &stage,
                          AdaptiveResponseSettings settings, const std::vector<ScenarioDefinition> &searchA,
                          const std::vector<ScenarioDefinition> &searchB, const Metrics &baselineA,
                          const Metrics &baselineB, const Constraint &constraint)
{
    Candidate candidate{id, preset, stage, sanitizedAdaptiveResponseSettings(settings)};
    candidate.searchA = analyze(searchA, candidate.settings);
    candidate.reason = absoluteFailure(candidate.searchA);
    if (candidate.reason.empty()) candidate.reason = relativeFailure(candidate.searchA, baselineA, constraint, true);
    // An ineligible Search A result is never allowed to consume Search B.
    // This is the required immediate, constraint-first rejection boundary.
    if (!candidate.reason.empty()) return candidate;
    candidate.searchB = analyze(searchB, candidate.settings);
    candidate.searchBRan = true;
    candidate.reason = absoluteFailure(candidate.searchB);
    if (candidate.reason.empty()) candidate.reason = relativeFailure(candidate.searchB, baselineB, constraint, true);
    candidate.aggregate = combine(candidate.searchA, candidate.searchB);
    if (candidate.reason.empty()) {
        const Metrics aggregateBaseline = combine(baselineA, baselineB);
        candidate.reason = absoluteFailure(candidate.aggregate);
        if (candidate.reason.empty()) candidate.reason = relativeFailure(candidate.aggregate, aggregateBaseline, constraint, false);
    }
    candidate.eligible = candidate.reason.empty();
    return candidate;
}

bool dominates(const Candidate &a, const Candidate &b)
{
    const Metrics &left = a.aggregate, &right = b.aggregate;
    const bool noWorse = left.advanceMs >= right.advanceMs && left.mae <= right.mae && left.p95 <= right.p95
        && left.p99 <= right.p99 && left.noiseAmplification <= right.noiseAmplification
        && left.wrongArea <= right.wrongArea && left.stopResidual <= right.stopResidual
        && left.reacquisitionMs <= right.reacquisitionMs;
    const bool better = left.advanceMs > right.advanceMs || left.mae < right.mae || left.p95 < right.p95
        || left.p99 < right.p99 || left.noiseAmplification < right.noiseAmplification
        || left.wrongArea < right.wrongArea || left.stopResidual < right.stopResidual
        || left.reacquisitionMs < right.reacquisitionMs;
    return noWorse && better;
}

std::vector<Candidate *> front(std::vector<Candidate> &candidates, const std::string &preset)
{
    std::vector<Candidate *> result;
    for (Candidate &candidate : candidates) {
        if (candidate.preset != preset || !candidate.eligible) continue;
        bool beaten = false;
        for (const Candidate &other : candidates)
            if (&candidate != &other && other.preset == preset && other.eligible && dominates(other, candidate)) beaten = true;
        if (!beaten) result.push_back(&candidate);
    }
    std::sort(result.begin(), result.end(), [](const Candidate *a, const Candidate *b) {
        if (a->aggregate.advanceMs != b->aggregate.advanceMs) return a->aggregate.advanceMs > b->aggregate.advanceMs;
        if (a->aggregate.mae != b->aggregate.mae) return a->aggregate.mae < b->aggregate.mae;
        return a->id < b->id;
    });
    return result;
}

QString corpusHash(const std::vector<ScenarioDefinition> &scenarios)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ScenarioDefinition &scenario : scenarios) {
        const std::vector<TraceSample> trace = generateTrace(scenario);
        hash.addData(scenario.id.data(), static_cast<qsizetype>(scenario.id.size()));
        for (const TraceSample &sample : trace) {
            hash.addData(reinterpret_cast<const char *>(&sample.timeSeconds), sizeof(sample.timeSeconds));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

ScenarioDefinition structuredScenario(std::uint32_t masterSeed, const std::string &partition, int kind, int index)
{
    static constexpr std::array<const char *, 10> names{{
        "fast-ballistic", "repeated-reversals", "asymmetric-reversals", "stop-restart", "brake-continue",
        "brake-reverse", "off-center-correction", "high-acceleration", "high-jerk", "noisy-coherent",
    }};
    ScenarioDefinition scenario;
    const std::string name = names[static_cast<size_t>(kind) % names.size()];
    scenario.id = "phase2b/" + partition + "/" + name + "-" + std::to_string(index);
    scenario.trajectoryId = scenario.id;
    scenario.family = name;
    scenario.seed = deriveSeed(masterSeed, partition, name, static_cast<std::uint32_t>(index), "trajectory");
    scenario.mapperRateHz = 250;
    scenario.sourceRateHz = std::array<int, 4>{{125, 200, 250, 333}}[static_cast<size_t>(index) % 4U];
    scenario.variableDt = (index % 3) == 0;
    scenario.durationSeconds = 3.2F;
    Rng rng(scenario.seed);
    const float amount = rng.range(.62F, .93F);
    const float minor = rng.range(.15F, .42F);
    const auto point = [&](float time, float position) {
        scenario.points.push_back({time, std::clamp(position, -.96F, .96F)});
    };
    switch (kind % static_cast<int>(names.size())) {
    case 0: point(0, -amount); point(.18F, amount); point(.36F, amount); point(.72F, amount * .35F); break;
    case 1: point(0, -amount); point(.35F, amount); point(.70F, -amount); point(1.05F, amount); point(1.40F, -amount); break;
    case 2: point(0, -amount); point(.25F, amount); point(.82F, -minor); point(1.05F, amount * .72F); point(1.80F, amount * .15F); break;
    case 3: point(0, -amount); point(.48F, amount); point(1.12F, amount); point(1.54F, amount); point(1.78F, -minor); point(2.35F, -minor); break;
    case 4: point(0, -amount); point(.38F, amount); point(.62F, amount * .30F); point(1.20F, amount * .92F); point(1.90F, amount * .92F); break;
    case 5: point(0, -amount); point(.38F, amount); point(.70F, amount * .26F); point(1.00F, -amount * .82F); point(1.75F, -amount * .82F); break;
    case 6: point(0, .42F); point(.34F, .78F); point(.67F, .16F); point(1.02F, .62F); point(1.55F, .35F); break;
    case 7: point(0, -amount); point(.12F, amount); point(.25F, -amount * .75F); point(.52F, amount * .88F); point(1.10F, amount * .88F); break;
    case 8: point(0, -.20F); point(.08F, .36F); point(.15F, -.50F); point(.24F, .67F); point(.38F, -.73F); point(.58F, .40F); break;
    default:
        point(0, -amount * .55F); point(.42F, amount * .55F); point(.84F, -amount * .42F); point(1.26F, amount * .42F); point(1.80F, .0F);
        scenario.noise = (index % 2) ? NoiseModel::Burst : NoiseModel::WhiteJitter;
        scenario.noiseAmplitude = rng.range(.002F, .006F);
        break;
    }
    point(scenario.durationSeconds, scenario.points.back().position);
    return scenario;
}

std::vector<ScenarioDefinition> freshCorpus(std::uint32_t seed, const std::string &partition,
                                            int samplesPerPersona, int structuredCount)
{
    std::vector<ScenarioDefinition> result;
    const std::array<const char *, 5> personas{{
        "precision-pilot", "combat-helicopter", "fixed-wing", "space-sim", "noisy-older-sensor",
    }};
    for (size_t personaIndex = 0; personaIndex < personas.size(); ++personaIndex) {
        const std::string persona = personas[personaIndex];
        for (int sample = 0; sample < samplesPerPersona; ++sample) {
            ScenarioDefinition scenario;
            scenario.id = "phase2b/" + partition + "/" + persona + "-" + std::to_string(sample);
            scenario.trajectoryId = scenario.id;
            scenario.family = "persona";
            scenario.durationSeconds = persona == "combat-helicopter" ? 3.5F : 5.5F;
            scenario.mapperRateHz = 250;
            scenario.sourceRateHz = std::array<int, 5>{{100, 125, 200, 250, 333}}[(static_cast<size_t>(sample) + personaIndex) % 5U];
            scenario.variableDt = ((sample + static_cast<int>(personaIndex)) % 3) == 0;
            scenario.seed = deriveSeed(seed, partition, persona, static_cast<std::uint32_t>(sample), "trajectory");
            if (persona == "noisy-older-sensor") {
                scenario.noise = std::array<NoiseModel, 3>{{NoiseModel::Quantized, NoiseModel::WhiteJitter, NoiseModel::Drift}}
                    [static_cast<size_t>(sample) % 3U];
                scenario.noiseAmplitude = .004F + .001F * static_cast<float>(sample % 3);
            }
            result.push_back(std::move(scenario));
        }
    }
    for (int index = 0; index < structuredCount; ++index)
        result.push_back(structuredScenario(seed, partition, index % 10, index));
    return result;
}

std::string ratiosCsv(const Metrics &candidate, const Metrics &baseline)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << ratio(candidate.mae, baseline.mae) << ',' << ratio(candidate.p95, baseline.p95) << ','
        << ratio(candidate.p99, baseline.p99) << ',' << ratio(candidate.noiseAmplification, baseline.noiseAmplification) << ','
        << candidate.advanceMs - baseline.advanceMs;
    return out.str();
}

std::string parameterCsv(const AdaptiveResponseSettings &s)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << s.maximumHorizonMs << ',' << s.maximumLead << ',' << s.turningPointProtection << ',' << s.turningPointMargin << ','
        << s.velocityResponse << ',' << s.accelerationResponse << ',' << s.horizonExtension << ','
        << s.horizonExtensionCapMs << ',' << s.reversalResponse << ',' << s.decelerationResponse << ','
        << s.onsetAssist << ',' << s.onsetCap << ',' << s.sustainedAssist << ',' << s.sustainedCap << ','
        << s.settlingResponse << ',' << s.noiseRejection << ',' << s.motionSensitivity;
    return out.str();
}

std::string metricsCsv(const Metrics &m)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << m.mae << ',' << m.p95 << ',' << m.p99 << ',' << m.advanceMs << ',' << m.usefulLead << ','
        << m.wrongArea << ',' << m.pattern73Area << ',' << m.reacquisitionMs << ',' << m.stopResidual << ','
        << m.stationaryRms << ',' << m.noiseAmplification << ',' << m.artificialStep << ',' << m.clampFraction << ','
        << m.capFraction << ',' << m.inactiveFraction << ',' << m.protectionFraction << ',' << m.meanHorizonMs << ','
        << m.p95HorizonMs << ',' << m.meanExtensionMs << ',' << m.maxExtensionMs << ',' << m.nonFinite << ','
        << m.illegalOutput << ',' << m.stationaryOffset;
    return out.str();
}

Trace readTrace(const QFileInfo &file)
{
    Trace trace;
    trace.name = file.fileName();
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) return trace;
    QTextStream text(&input);
    text.readLine();
    while (!text.atEnd()) {
        const QStringList fields = text.readLine().split(',');
        bool timeOk = false, valueOk = false;
        const float time = fields.value(0).toFloat(&timeOk) / 1000.0F;
        const float value = fields.value(3).toFloat(&valueOk);
        if (timeOk && valueOk && std::isfinite(value)) {
            trace.times.push_back(time);
            trace.values.push_back(value);
        }
    }
    return trace;
}

Metrics analyzePhysical(const Trace &trace, const AdaptiveResponseSettings &settings)
{
    Metrics metrics;
    if (trace.times.size() < 2U) return metrics;
    AdaptiveResponseProcessor processor;
    const RuntimeAdaptiveResponseConfig runtime = toRuntime(settings);
    const auto origin = std::chrono::steady_clock::time_point{};
    std::vector<double> errors, advances, horizons, reacquisitions;
    double errorSum = 0.0, leadSum = 0.0, horizonSum = 0.0, stationarySquares = 0.0;
    std::uint64_t stationary = 0, moving = 0, inactive = 0, clamped = 0;
    float previousVelocity = 0.0F;
    float previousPrediction = 0.0F;
    float reversalStartedAt = -1.0F;
    for (size_t index = 0; index < trace.times.size(); ++index) {
        const auto now = origin + std::chrono::microseconds(static_cast<std::int64_t>(trace.times[index] * 1000000.0F));
        const AdaptiveResponseTelemetry telemetry = processor.process(trace.values[index], runtime, now);
        ++metrics.samples;
        const float previous = index ? trace.values[index - 1U] : trace.values[index];
        const float previousTime = index ? trace.times[index - 1U] : trace.times[index];
        const float velocity = index ? (trace.values[index] - previous) / std::max(0.000001F, trace.times[index] - previousTime) : 0.0F;
        const float futureTime = std::min(trace.times.back(), trace.times[index] + telemetry.activeHorizonSeconds);
        const auto upper = std::lower_bound(trace.times.cbegin(), trace.times.cend(), futureTime);
        const size_t futureIndex = upper == trace.times.cend() ? trace.times.size() - 1U
            : static_cast<size_t>(upper - trace.times.cbegin());
        const double error = std::abs(static_cast<double>(telemetry.predicted) - trace.values[futureIndex]);
        errors.push_back(error); errorSum += error; leadSum += std::abs(telemetry.lead);
        const double horizonMs = telemetry.activeHorizonSeconds * 1000.0;
        horizons.push_back(horizonMs); horizonSum += horizonMs;
        metrics.maxExtensionMs = std::max(metrics.maxExtensionMs,
            std::max(0.0, horizonMs - static_cast<double>(settings.maximumHorizonMs)));
        if (std::abs(velocity) > 0.08F) {
            ++moving;
            if (std::abs(telemetry.lead) < 0.00001F) ++inactive;
            if (telemetry.lead * velocity < 0.0F) metrics.wrongArea += std::abs(telemetry.lead)
                * std::max(0.0F, trace.times[index] - previousTime);
            const double advance = std::abs(telemetry.lead / velocity) * 1000.0;
            if (advance < 100.0) advances.push_back(advance);
        } else {
            stationarySquares += telemetry.lead * telemetry.lead;
            ++stationary;
        }
        if (index > 0U) {
            metrics.artificialStep = std::max(metrics.artificialStep,
                std::abs(static_cast<double>(telemetry.predicted - previousPrediction)
                    - static_cast<double>(trace.values[index] - previous)));
            if (std::abs(previousVelocity) > .08F && std::abs(velocity) <= .02F)
                metrics.stopResidual = std::max(metrics.stopResidual, std::abs(static_cast<double>(telemetry.lead)));
            if (velocity * previousVelocity < -.0064F) reversalStartedAt = trace.times[index];
            if (reversalStartedAt >= 0.0F && telemetry.lead * velocity >= 0.0F) {
                reacquisitions.push_back((trace.times[index] - reversalStartedAt) * 1000.0F);
                reversalStartedAt = -1.0F;
            }
        }
        if (telemetry.safetyLimited) ++clamped;
        if (!std::isfinite(telemetry.predicted)) ++metrics.nonFinite;
        if (telemetry.predicted < -1.0001F || telemetry.predicted > 1.0001F) ++metrics.illegalOutput;
        previousVelocity = velocity;
        previousPrediction = telemetry.predicted;
    }
    const double count = std::max<std::uint64_t>(1, metrics.samples);
    metrics.mae = errorSum / count; metrics.p95 = percentile(errors, 0.95); metrics.p99 = percentile(errors, 0.99);
    metrics.advanceMs = percentile(advances, 0.50); metrics.usefulLead = leadSum / count;
    metrics.stationaryRms = stationary ? std::sqrt(stationarySquares / stationary) : 0.0;
    metrics.clampFraction = static_cast<double>(clamped) / count;
    metrics.inactiveFraction = moving ? static_cast<double>(inactive) / moving : 0.0;
    metrics.meanHorizonMs = horizonSum / count; metrics.p95HorizonMs = percentile(horizons, 0.95);
    metrics.meanExtensionMs = std::max(0.0, metrics.meanHorizonMs - settings.maximumHorizonMs);
    metrics.reacquisitionMs = percentile(reacquisitions, 0.50);
    // Recorded physical traces have no independently known sensor-noise
    // baseline. Stationary lead RMS is the directly observed jitter metric.
    metrics.noiseAmplification = metrics.stationaryRms;
    return metrics;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Offline V2.3.T Phase 2B constrained preset optimization. No hardware, UI, mapper, DirectInput, or vJoy is opened."_qs);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty artifact directory."_qs, u"directory"_qs);
    const QCommandLineOption physicalOption(u"physical-corpus"_qs, u"Required directory with exactly 15 trace CSV files."_qs, u"directory"_qs);
    parser.addOptions({outputOption, physicalOption});
    parser.process(app);
    const QString outputPath = parser.value(outputOption).trimmed();
    const QString physicalPath = parser.value(physicalOption).trimmed();
    if (outputPath.isEmpty() || physicalPath.isEmpty()) return 2;
    QDir output(outputPath);
    if (output.exists() && !output.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) return 2;
    if (!QDir().mkpath(outputPath)) { QTextStream(stderr) << "Unable to create output directory\n"; return 3; }

    const QFileInfoList physicalFiles = QDir(physicalPath).entryInfoList({u"*.csv"_qs}, QDir::Files, QDir::Name);
    if (physicalFiles.size() != 15) { QTextStream(stderr) << "Expected 15 physical CSV files, found " << physicalFiles.size() << '\n'; return 3; }
    std::vector<Trace> physical;
    std::uint64_t physicalSamples = 0;
    for (const QFileInfo &file : physicalFiles) {
        Trace trace = readTrace(file);
        if (trace.times.size() < 2U) { QTextStream(stderr) << "Unable to parse " << file.fileName() << '\n'; return 3; }
        physicalSamples += trace.times.size();
        physical.push_back(std::move(trace));
    }

    // Final holdout generation is identity-only.  No replayScenario call ever
    // receives this corpus in Phase 2B.
    const std::vector<ScenarioDefinition> searchA = freshCorpus(kSearchASeed, "search-a", 24, 40);
    const std::vector<ScenarioDefinition> searchB = freshCorpus(kSearchBSeed, "search-b", 24, 40);
    std::array<std::vector<ScenarioDefinition>, 4> validations;
    for (size_t index = 0; index < validations.size(); ++index)
        validations[index] = freshCorpus(kValidationSeeds[index], "validation-" + std::to_string(index + 1), 20, 30);
    const std::vector<ScenarioDefinition> sealedHoldout = freshCorpus(kFinalHoldoutSeed, "phase3b-final-holdout", 30, 50);
    if (searchA.size() != kSearchScenarios || searchB.size() != kSearchScenarios
        || sealedHoldout.size() != kFinalHoldoutScenarios) return 3;
    for (const auto &fold : validations) if (fold.size() != kValidationScenarios) return 3;
    const QString searchAHash = corpusHash(searchA);
    const QString searchBHash = corpusHash(searchB);
    std::array<QString, 4> validationHashes;
    for (size_t index = 0; index < validations.size(); ++index) validationHashes[index] = corpusHash(validations[index]);
    const QString finalHoldoutHash = corpusHash(sealedHoldout);

    const std::array<Spec, 5> specs{{
        {"Light", 3.0F, 5.5F, {.25, 1.07, 1.10, 1.10, 1.10}},
        {"Balanced", 6.0F, 10.0F, {.50, 1.05, 1.08, 1.10, 1.10}},
        {"Fast", 9.0F, 15.0F, {1.00, 1.15, 1.20, 1.25, 1.20}},
        {"Aggressive", 14.0F, 22.0F, {1.50, 1.30, 1.35, 1.40, 1.35}},
        {"Extreme", 25.0F, 32.0F, {2.00, 1.50, 1.50, 1.60, 1.50}},
    }};

    std::map<std::string, AdaptiveResponseSettings> baselines;
    std::map<std::string, Metrics> baselineA, baselineB;
    std::map<std::string, std::array<Metrics, 4>> baselineValidation;
    std::vector<Candidate> candidates;
    candidates.reserve(560);
    for (const Spec &spec : specs) {
        const auto baseline = baselineFor(spec.name);
        if (!baseline) { QTextStream(stderr) << "Missing baseline " << QString::fromStdString(spec.name) << '\n'; return 3; }
        baselines.emplace(spec.name, *baseline);
        baselineA.emplace(spec.name, analyze(searchA, *baseline));
        baselineB.emplace(spec.name, analyze(searchB, *baseline));
        Candidate control{spec.name + "-baseline", spec.name, "baseline-control", *baseline};
        control.searchA = baselineA.at(spec.name);
        control.searchB = baselineB.at(spec.name);
        control.searchBRan = true;
        control.aggregate = combine(control.searchA, control.searchB);
        control.eligible = true;
        candidates.push_back(std::move(control));
        std::array<Metrics, 4> validationControl;
        for (size_t index = 0; index < validations.size(); ++index)
            validationControl[index] = analyze(validations[index], *baseline);
        baselineValidation.emplace(spec.name, std::move(validationControl));
    }

    // Stage 1: preserve the production base horizon and lead exactly.  Only
    // conditional authority dimensions are sampled.
    for (const Spec &spec : specs) {
        const AdaptiveResponseSettings baseline = baselines.at(spec.name);
        Rng rng(deriveSeed(kSearchASeed, "phase2b-stage1", spec.name, 0, "settings"));
        for (int index = 0; index < 32; ++index) {
            AdaptiveResponseSettings settings = baseline;
            settings.velocityResponse += rng.range(-.14F, .20F);
            settings.accelerationResponse += rng.range(-.18F, .24F);
            settings.horizonExtension += rng.range(-.08F, .36F);
            settings.horizonExtensionCapMs += rng.range(-2.0F, 10.0F);
            settings.turningPointProtection += rng.range(-.16F, .18F);
            settings.turningPointMargin += rng.range(-.045F, .045F);
            settings.reversalResponse += rng.range(-.10F, .06F);
            settings.decelerationResponse += rng.range(-.12F, .12F);
            Candidate candidate = assessCandidate(spec.name + "-stage1-" + std::to_string(index + 1), spec.name,
                "stage1-conditional-authority", settings, searchA, searchB, baselineA.at(spec.name),
                baselineB.at(spec.name), spec.constraint);
            candidates.push_back(std::move(candidate));
        }
    }

    const auto rankedEligibleSettings = [&](const std::string &preset, const std::vector<std::string> &stages) {
        std::vector<const Candidate *> result;
        for (const Candidate &candidate : candidates) {
            if (!candidate.eligible || candidate.preset != preset) continue;
            if (std::find(stages.begin(), stages.end(), candidate.stage) != stages.end()) result.push_back(&candidate);
        }
        const Metrics &base = combine(baselineA.at(preset), baselineB.at(preset));
        std::sort(result.begin(), result.end(), [&base](const Candidate *left, const Candidate *right) {
            const double leftAdvance = left->aggregate.advanceMs - base.advanceMs;
            const double rightAdvance = right->aggregate.advanceMs - base.advanceMs;
            if (leftAdvance != rightAdvance) return leftAdvance > rightAdvance;
            if (left->aggregate.mae != right->aggregate.mae) return left->aggregate.mae < right->aggregate.mae;
            return left->id < right->id;
        });
        std::vector<AdaptiveResponseSettings> settings;
        for (const Candidate *candidate : result) settings.push_back(candidate->settings);
        return settings;
    };

    // Stage 2: only Stage 1-eligible parents may enter the small horizon/lead
    // trust region; a negative Stage 1 result starts from the proven baseline
    // control, never from an ineligible point. The ranges are the locked
    // conservative ranges above.
    for (const Spec &spec : specs) {
        std::vector<AdaptiveResponseSettings> parents = rankedEligibleSettings(spec.name, {"stage1-conditional-authority"});
        if (parents.empty()) parents.push_back(baselines.at(spec.name));
        Rng rng(deriveSeed(kSearchBSeed, "phase2b-stage2", spec.name, 0, "settings"));
        for (size_t parent = 0; parent < std::min<size_t>(3, parents.size()); ++parent) {
            for (int local = 0; local < 24; ++local) {
                AdaptiveResponseSettings settings = parents[parent];
                const AdaptiveResponseSettings &base = baselines.at(spec.name);
                const float fraction = static_cast<float>((local * 11) % 24) / 23.0F;
                settings.maximumHorizonMs = spec.horizonLow + (spec.horizonHigh - spec.horizonLow) * fraction;
                settings.maximumLead = std::max(.001F, base.maximumLead * (.72F + .56F * static_cast<float>((local * 17) % 24) / 23.0F));
                settings.velocityResponse += rng.range(-.06F, .06F);
                settings.accelerationResponse += rng.range(-.06F, .06F);
                settings.horizonExtension += rng.range(-.06F, .06F);
                settings.horizonExtensionCapMs += rng.range(-2.0F, 2.0F);
                settings.turningPointProtection += rng.range(-.035F, .035F);
                settings.turningPointMargin += rng.range(-.012F, .012F);
                settings.reversalResponse += rng.range(-.035F, .035F);
                settings.decelerationResponse += rng.range(-.035F, .035F);
                Candidate candidate = assessCandidate(spec.name + "-stage2-" + std::to_string(parent + 1) + "-" + std::to_string(local + 1),
                    spec.name, "stage2-trust-region", settings, searchA, searchB, baselineA.at(spec.name), baselineB.at(spec.name), spec.constraint);
                candidates.push_back(std::move(candidate));
            }
        }
    }

    // Stage 3 is a local refinement only around candidates that have remained
    // eligible on both independent search corpora.
    for (const Spec &spec : specs) {
        const std::vector<AdaptiveResponseSettings> parents = rankedEligibleSettings(spec.name,
            {"stage1-conditional-authority", "stage2-trust-region"});
        Rng rng(deriveSeed(kSearchASeed ^ kSearchBSeed, "phase2b-stage3", spec.name, 0, "settings"));
        for (size_t parent = 0; parent < std::min<size_t>(2, parents.size()); ++parent) {
            for (int local = 0; local < 8; ++local) {
                AdaptiveResponseSettings settings = parents[parent];
                settings.maximumHorizonMs = std::clamp(settings.maximumHorizonMs + rng.range(-.45F, .45F), spec.horizonLow, spec.horizonHigh);
                settings.maximumLead = std::max(.001F, settings.maximumLead + rng.range(-.004F, .004F));
                settings.velocityResponse += rng.range(-.028F, .028F);
                settings.accelerationResponse += rng.range(-.028F, .028F);
                settings.horizonExtension += rng.range(-.030F, .030F);
                settings.horizonExtensionCapMs += rng.range(-1.0F, 1.0F);
                settings.turningPointProtection += rng.range(-.018F, .018F);
                settings.turningPointMargin += rng.range(-.006F, .006F);
                settings.reversalResponse += rng.range(-.018F, .018F);
                settings.decelerationResponse += rng.range(-.018F, .018F);
                Candidate candidate = assessCandidate(spec.name + "-stage3-" + std::to_string(parent + 1) + "-" + std::to_string(local + 1),
                    spec.name, "stage3-local-refinement", settings, searchA, searchB, baselineA.at(spec.name), baselineB.at(spec.name), spec.constraint);
                candidates.push_back(std::move(candidate));
            }
        }
    }

    std::map<std::string, std::vector<std::string>> finalistIds;
    for (const Spec &spec : specs) {
        const std::vector<Candidate *> pareto = front(candidates, spec.name);
        for (Candidate *candidate : pareto) {
            if (candidate->stage == "baseline-control") continue;
            finalistIds[spec.name].push_back(candidate->id);
            if (finalistIds[spec.name].size() == 2U) break;
        }
    }
    const auto candidateForId = [&](const std::string &id) -> Candidate * {
        for (Candidate &candidate : candidates) if (candidate.id == id) return &candidate;
        return nullptr;
    };

    struct FinalistGate {
        Candidate *candidate = nullptr;
        bool validationPass = false;
        bool physicalPass = false;
        bool robustnessPass = false;
        std::string reason;
    };
    std::map<std::string, std::vector<FinalistGate>> gates;
    std::ostringstream validationRows, worstFoldRows, physicalRows, robustnessRows;
    validationRows << "preset,candidate_id,fold,eligible,reason,future_mae,p95_error,p99_error,temporal_advance_ms,noise_amplification,mae_ratio,p95_ratio,p99_ratio,noise_ratio,temporal_advance_delta_ms\n";
    worstFoldRows << "preset,candidate_id,worst_fold,reason,mae_ratio,p95_ratio,p99_ratio,noise_ratio,temporal_advance_delta_ms\n";
    physicalRows << "preset,candidate_id,trace_id,pass,reason,baseline_mae,candidate_mae,baseline_p95,candidate_p95,baseline_p99,candidate_p99,baseline_advance_ms,candidate_advance_ms,baseline_wrong_direction_area,candidate_wrong_direction_area,baseline_stop_residual,candidate_stop_residual,baseline_reacquisition_ms,candidate_reacquisition_ms,baseline_artificial_step,candidate_artificial_step,baseline_stationary_jitter,candidate_stationary_jitter\n";
    robustnessRows << "preset,candidate_id,perturbation,eligible,reason,future_mae,p95_error,p99_error,temporal_advance_ms,noise_amplification\n";

    const auto physicalFailure = [](const Metrics &candidate, const Metrics &baseline, const Constraint &constraint) {
        if (ratio(candidate.mae, baseline.mae) > constraint.maximumMaeRatio) return std::string("physical-mae");
        if (ratio(candidate.p95, baseline.p95) > constraint.maximumP95Ratio) return std::string("physical-p95");
        if (ratio(candidate.p99, baseline.p99) > constraint.maximumP99Ratio) return std::string("physical-p99");
        if (candidate.wrongArea > baseline.wrongArea + .0001) return std::string("physical-wrong-direction");
        if (candidate.stopResidual > baseline.stopResidual + .02) return std::string("physical-stop");
        if (candidate.reacquisitionMs > baseline.reacquisitionMs + 20.0) return std::string("physical-reversal");
        if (candidate.artificialStep > baseline.artificialStep + .03) return std::string("physical-artificial-step");
        if (candidate.stationaryRms > baseline.stationaryRms + .012) return std::string("physical-jitter");
        return std::string{};
    };

    for (const Spec &spec : specs) {
        for (const std::string &id : finalistIds[spec.name]) {
            Candidate *candidate = candidateForId(id);
            if (!candidate) return 4;
            FinalistGate gate;
            gate.candidate = candidate;
            Metrics aggregateValidation{};
            Metrics aggregateBaseline{};
            bool first = true;
            int worstIndex = 0;
            std::string worstReason;
            double worstMagnitude = -1.0;
            for (size_t index = 0; index < validations.size(); ++index) {
                const Metrics result = analyze(validations[index], candidate->settings);
                const Metrics &base = baselineValidation.at(spec.name)[index];
                const std::string reason = absoluteFailure(result).empty()
                    ? relativeFailure(result, base, spec.constraint, true) : absoluteFailure(result);
                validationRows << spec.name << ',' << csv(candidate->id).toStdString() << ',' << (index + 1) << ',' << reason.empty() << ','
                    << csv(reason).toStdString() << ',' << std::fixed << std::setprecision(8)
                    << result.mae << ',' << result.p95 << ',' << result.p99 << ',' << result.advanceMs << ','
                    << result.noiseAmplification << ',' << ratiosCsv(result, base) << '\n';
                const double magnitude = std::max({ratio(result.mae, base.mae), ratio(result.p95, base.p95), ratio(result.p99, base.p99), ratio(result.noiseAmplification, base.noiseAmplification)});
                if (magnitude > worstMagnitude) { worstMagnitude = magnitude; worstIndex = static_cast<int>(index); worstReason = reason; }
                if (first) { aggregateValidation = result; aggregateBaseline = base; first = false; }
                else { aggregateValidation = combine(aggregateValidation, result); aggregateBaseline = combine(aggregateBaseline, base); }
                if (!reason.empty() && gate.reason.empty()) gate.reason = "validation-" + std::to_string(index + 1) + ":" + reason;
            }
            if (gate.reason.empty()) {
                const std::string aggregateReason = absoluteFailure(aggregateValidation).empty()
                    ? relativeFailure(aggregateValidation, aggregateBaseline, spec.constraint, false) : absoluteFailure(aggregateValidation);
                if (!aggregateReason.empty()) gate.reason = "validation-aggregate:" + aggregateReason;
            }
            gate.validationPass = gate.reason.empty();
            const Metrics &worst = baselineValidation.at(spec.name)[static_cast<size_t>(worstIndex)];
            const Metrics candidateWorst = analyze(validations[static_cast<size_t>(worstIndex)], candidate->settings);
            worstFoldRows << spec.name << ',' << csv(candidate->id).toStdString() << ',' << (worstIndex + 1) << ',' << csv(worstReason).toStdString() << ','
                << ratiosCsv(candidateWorst, worst) << '\n';

            if (gate.validationPass) {
                for (const Trace &trace : physical) {
                    const Metrics baselinePhysical = analyzePhysical(trace, baselines.at(spec.name));
                    const Metrics candidatePhysical = analyzePhysical(trace, candidate->settings);
                    const std::string reason = physicalFailure(candidatePhysical, baselinePhysical, spec.constraint);
                    physicalRows << spec.name << ',' << csv(candidate->id).toStdString() << ',' << csv(trace.name.toStdString()).toStdString() << ','
                        << reason.empty() << ',' << csv(reason).toStdString() << ',' << std::fixed << std::setprecision(8)
                        << baselinePhysical.mae << ',' << candidatePhysical.mae << ',' << baselinePhysical.p95 << ',' << candidatePhysical.p95 << ','
                        << baselinePhysical.p99 << ',' << candidatePhysical.p99 << ',' << baselinePhysical.advanceMs << ',' << candidatePhysical.advanceMs << ','
                        << baselinePhysical.wrongArea << ',' << candidatePhysical.wrongArea << ',' << baselinePhysical.stopResidual << ',' << candidatePhysical.stopResidual << ','
                        << baselinePhysical.reacquisitionMs << ',' << candidatePhysical.reacquisitionMs << ',' << baselinePhysical.artificialStep << ',' << candidatePhysical.artificialStep << ','
                        << baselinePhysical.stationaryRms << ',' << candidatePhysical.stationaryRms << '\n';
                    if (!reason.empty() && gate.reason.empty()) gate.reason = "physical:" + trace.name.toStdString() + ":" + reason;
                }
                gate.physicalPass = gate.reason.empty();
            }

            if (gate.validationPass && gate.physicalPass) {
                const std::array<std::pair<std::string, std::array<float, 5>>, 10> perturbations{{
                    {"horizon-minus", {-.35F, 0, 0, 0, 0}}, {"horizon-plus", {.35F, 0, 0, 0, 0}},
                    {"lead-minus", {0, -.004F, 0, 0, 0}}, {"lead-plus", {0, .004F, 0, 0, 0}},
                    {"extension-minus", {0, 0, -.035F, 0, 0}}, {"extension-plus", {0, 0, .035F, 0, 0}},
                    {"response-minus", {0, 0, 0, -.025F, 0}}, {"response-plus", {0, 0, 0, .025F, 0}},
                    {"turning-minus", {0, 0, 0, 0, -.020F}}, {"turning-plus", {0, 0, 0, 0, .020F}},
                }};
                for (const auto &[name, delta] : perturbations) {
                    AdaptiveResponseSettings varied = candidate->settings;
                    varied.maximumHorizonMs = std::clamp(varied.maximumHorizonMs + delta[0], spec.horizonLow, spec.horizonHigh);
                    varied.maximumLead = std::max(.001F, varied.maximumLead + delta[1]);
                    varied.horizonExtension += delta[2];
                    varied.velocityResponse += delta[3]; varied.accelerationResponse += delta[3];
                    varied.turningPointProtection += delta[4];
                    Candidate probe = assessCandidate("robustness", spec.name, "robustness", varied, searchA, searchB,
                        baselineA.at(spec.name), baselineB.at(spec.name), spec.constraint);
                    const Metrics &observed = probe.searchBRan ? probe.aggregate : probe.searchA;
                    robustnessRows << spec.name << ',' << csv(candidate->id).toStdString() << ',' << name << ',' << probe.eligible << ','
                        << csv(probe.reason).toStdString() << ',' << std::fixed << std::setprecision(8)
                        << observed.mae << ',' << observed.p95 << ',' << observed.p99 << ','
                        << observed.advanceMs << ',' << observed.noiseAmplification << '\n';
                    if (!probe.eligible && gate.reason.empty()) gate.reason = "robustness:" + name + ":" + probe.reason;
                }
                gate.robustnessPass = gate.reason.empty();
            }
            gates[spec.name].push_back(std::move(gate));
        }
    }

    std::map<std::string, Candidate *> selections;
    std::map<std::string, std::string> decisions;
    for (const Spec &spec : specs) {
        Candidate *selected = candidateForId(spec.name + "-baseline");
        decisions.emplace(spec.name, "BASELINE RETAINED - NO ROBUST IMPROVEMENT FOUND");
        for (const FinalistGate &gate : gates[spec.name]) {
            if (gate.validationPass && gate.physicalPass && gate.robustnessPass) {
                selected = gate.candidate;
                decisions[spec.name] = "NEW CANDIDATE CLEARLY BETTER";
                break;
            }
        }
        selections.emplace(spec.name, selected);
    }

    std::ostringstream parameters, candidateRows, rejectedRows, paretoSummary, policy, constraints, recommendations, report, manifest, holdoutSeal;
    parameters << "candidate_id,preset,stage,eligible,maximum_horizon_ms,maximum_lead,turning_protection,turning_margin,velocity_response,acceleration_response,horizon_extension,horizon_extension_cap_ms,reversal_response,deceleration_response,onset_assist,onset_cap,sustained_assist,sustained_cap,settling_response,noise_rejection,motion_sensitivity\n";
    candidateRows << "candidate_id,preset,stage,fold,evaluated,eligible,ineligible_reason,future_mae,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,artificial_step,lead_clamp_fraction,horizon_cap_fraction,prediction_inactive_fraction,protection_active_fraction,mean_horizon_ms,p95_horizon_ms,mean_extension_ms,max_extension_ms,non_finite,illegal_output,stationary_offset,mae_ratio,p95_ratio,p99_ratio,noise_ratio,temporal_advance_delta_ms\n";
    rejectedRows << "candidate_id,preset,stage,reason\n";
    constraints << "preset,min_temporal_advance_ms,max_mae_ratio,max_p95_ratio,max_p99_ratio,max_noise_ratio,individual_fold_tolerance,absolute_pattern73_area,absolute_stop_residual,absolute_reversal_reacquisition_ms\n";
    for (const Spec &spec : specs) {
        const Metrics aggregateBase = combine(baselineA.at(spec.name), baselineB.at(spec.name));
        constraints << spec.name << ',' << spec.constraint.minimumAdvanceMs << ',' << spec.constraint.maximumMaeRatio << ','
            << spec.constraint.maximumP95Ratio << ',' << spec.constraint.maximumP99Ratio << ',' << spec.constraint.maximumNoiseRatio << ','
            << spec.constraint.individualFoldTolerance << ",0.0001,0.12,100\n";
        for (const Candidate &candidate : candidates) {
            if (candidate.preset != spec.name) continue;
            parameters << csv(candidate.id).toStdString() << ',' << csv(candidate.preset).toStdString() << ',' << csv(candidate.stage).toStdString() << ','
                << candidate.eligible << ',' << parameterCsv(candidate.settings) << '\n';
            const auto row = [&](const std::string &fold, const Metrics &metrics, const Metrics &baseline, bool evaluated) {
                candidateRows << csv(candidate.id).toStdString() << ',' << spec.name << ',' << csv(candidate.stage).toStdString() << ',' << fold << ','
                    << evaluated << ',' << candidate.eligible << ',' << csv(candidate.reason).toStdString() << ',' << metricsCsv(metrics) << ','
                    << ratiosCsv(metrics, baseline) << '\n';
            };
            row("search-a", candidate.searchA, baselineA.at(spec.name), true);
            row("search-b", candidate.searchB, baselineB.at(spec.name), candidate.searchBRan);
            row("search-aggregate", candidate.aggregate, aggregateBase, candidate.searchBRan);
            if (!candidate.eligible) rejectedRows << csv(candidate.id).toStdString() << ',' << spec.name << ','
                << csv(candidate.stage).toStdString() << ',' << csv(candidate.reason).toStdString() << '\n';
        }
        const std::vector<Candidate *> pareto = front(candidates, spec.name);
        paretoSummary << "preset,candidate_id,stage,temporal_advance_ms,future_mae,p95_error,p99_error,noise_amplification\n";
        for (Candidate *candidate : pareto) paretoSummary << spec.name << ',' << csv(candidate->id).toStdString() << ','
            << csv(candidate->stage).toStdString() << ',' << candidate->aggregate.advanceMs << ',' << candidate->aggregate.mae << ','
            << candidate->aggregate.p95 << ',' << candidate->aggregate.p99 << ',' << candidate->aggregate.noiseAmplification << '\n';
        writeText(output.filePath(QString::fromStdString("pareto_" + spec.name + ".csv")), paretoSummary.str());
        paretoSummary.str({}); paretoSummary.clear();
        Candidate *selection = selections.at(spec.name);
        recommendations << "## " << spec.name << "\n\nDisposition: " << decisions.at(spec.name) << "\n\nSelected Phase 3B vector: "
            << parameterCsv(selection->settings) << "\n\n";
    }
    policy << "# V2.3.T Phase 2B search policy\n\n"
           << "This is a constrained baseline-relative search, not a weighted-score optimization. Search A and Search B are independent fresh 160-scenario corpora. Search A is an immediate reject gate; only Search A-eligible candidates execute Search B. Search stages retain eligibility continuously. Stage 1 fixes each production horizon and lead and changes only conditional authority. Stage 2 moves inside the locked horizon/lead trust regions. Stage 3 is local only around candidates eligible on both searches. Validation folds and physical traces are rejection-only gates. The final Phase 3B holdout is generated for identity sealing only and never replayed here.\n";

    size_t changed = 0, ineligible = 0;
    for (const Candidate &candidate : candidates) if (!candidate.eligible) ++ineligible;
    for (const Spec &spec : specs) if (selections.at(spec.name)->stage != "baseline-control") ++changed;
    const std::string disposition = changed == specs.size()
        ? "PHASE 2B PASS - ROBUST PRESET FAMILY READY FOR NEW BLIND HOLDOUT"
        : changed > 0 ? "PHASE 2B PARTIAL - BASELINES RETAINED WHERE OPTIMIZATION DID NOT GENERALIZE"
        : "PHASE 2B FAIL - NO ROBUST IMPROVEMENT OVER CURRENT PRESETS";
    report << "# V2.3.T Adaptive Response Phase 2B Robust Preset Optimization Report\n\n"
           << "1. Production freeze SHA: " << HOTAS_PHASE2B_FREEZE_SHA << ".\n"
           << "2. Phase 3 disposition: the prior optimized family is permanently rejected for generalization failure.\n"
           << "3. Architecture confirmation: frozen; this target links the estimator only and modifies no production source.\n"
           << "4. Old holdout historical identity: 0x23A10003 / 100 scenarios / 6c4e742eb17771d3310d25e336b7267fe1d80b54bb638ea2a448a30c803f63db; not loaded.\n"
           << "5. Fresh search identities: Search A 0x2B100101 / " << searchA.size() << " / " << searchAHash.toStdString()
           << "; Search B 0x2B100102 / " << searchB.size() << " / " << searchBHash.toStdString() << ".\n"
           << "6. Fresh validation identities are in corpus_manifest.json.\n"
           << "7. New Phase 3B holdout: 0x2B10F00D / " << sealedHoldout.size() << " / " << finalHoldoutHash.toStdString() << ".\n"
           << "8. New holdout untouched: identity generation only; zero estimator replays.\n"
           << "9. Search corpus sizes: " << searchA.size() << " and " << searchB.size() << ".\n"
           << "10. Validation fold size: " << kValidationScenarios << " each across four independent folds.\n"
           << "11. Candidate count: " << candidates.size() << "; ineligible: " << ineligible << ".\n"
           << "12. Formulation: constrained baseline-relative responsiveness maximization; no weighted scalar ranking.\n"
           << "13. Hard eligibility constraints: finite/bounds/stationary/Pattern-73/stop/reversal plus the locked ratios.\n"
           << "14. Thresholds: eligibility_constraints.csv.\n"
           << "15. Stage 1 result: baseline-horizon conditional-authority search recorded in candidate_metrics.csv.\n"
           << "16. Stage 2 result: trust-region search recorded in candidate_metrics.csv.\n"
           << "17. Stage 3 result: eligible-only local refinement recorded in candidate_metrics.csv.\n\n"
           << "## Per-preset decisions\n\n" << recommendations.str()
           << "Physical gate: 15 recorded traces / " << physicalSamples << " source samples; results in physical_finalist_results.csv.\n\n"
           << "Robustness basin: ten small perturbations per validation-and-physical-passing finalist; results in robustness_analysis.csv.\n\n"
           << "Clamp and extension behavior are retained per candidate in candidate_metrics.csv. Pattern-73 remains a hard, test-only acceptance sentinel; canonical product findings are not changed. HOTPATH benchmark is deliberately external to this offline optimizer and must be recorded before any review.\n\n"
           << "New holdout readiness: sealed, identity recorded, and not replayed.\n\n"
           << "Final disposition: " << disposition << "\n";
    manifest << "{\n  \"phase\": \"v2.3.T-phase2b\",\n  \"freeze_sha\": \"" << HOTAS_PHASE2B_FREEZE_SHA
        << "\",\n  \"corpus_generator\": \"" << kCorpusGeneratorVersion << "\",\n  \"search_a\": {\"seed\": \"0x2B100101\", \"scenarios\": " << searchA.size() << ", \"sha256\": \"" << searchAHash.toStdString()
        << "\"},\n  \"search_b\": {\"seed\": \"0x2B100102\", \"scenarios\": " << searchB.size() << ", \"sha256\": \"" << searchBHash.toStdString() << "\"},\n  \"validation\": [\n";
    for (size_t index = 0; index < validationHashes.size(); ++index) manifest << "    {\"fold\": " << (index + 1) << ", \"seed\": \"0x" << std::hex << std::uppercase << kValidationSeeds[index] << std::dec << "\", \"scenarios\": " << validations[index].size() << ", \"sha256\": \"" << validationHashes[index].toStdString() << "\"}" << (index + 1 == validationHashes.size() ? "\n" : ",\n");
    manifest << "  ],\n  \"phase3b_final_holdout\": {\"seed\": \"0x2B10F00D\", \"scenarios\": " << sealedHoldout.size() << ", \"sha256\": \"" << finalHoldoutHash.toStdString() << "\", \"sealed\": true, \"estimator_replays\": 0},\n  \"persona_distribution\": {\"precision_pilot\": 24, \"combat_helicopter\": 24, \"fixed_wing\": 24, \"space_sim\": 24, \"noisy_sensor\": 24, \"structured_motion\": 40},\n  \"candidates\": " << candidates.size() << "\n}\n";
    holdoutSeal << "{\n  \"phase\": \"pre-replay-sealed\",\n  \"freeze_sha\": \"" << HOTAS_PHASE2B_FREEZE_SHA << "\",\n  \"seed\": \"0x2B10F00D\",\n  \"scenario_count\": " << sealedHoldout.size() << ",\n  \"generator\": \"" << kCorpusGeneratorVersion << "\",\n  \"sha256\": \"" << finalHoldoutHash.toStdString() << "\",\n  \"sealed\": true,\n  \"estimator_replays\": 0\n}\n";
    if (!writeText(output.filePath(u"corpus_manifest.json"_qs), manifest.str())
        || !writeText(output.filePath(u"phase3b_holdout_seal.json"_qs), holdoutSeal.str())
        || !writeText(output.filePath(u"search_policy.md"_qs), policy.str())
        || !writeText(output.filePath(u"eligibility_constraints.csv"_qs), constraints.str())
        || !writeText(output.filePath(u"candidate_parameters.csv"_qs), parameters.str())
        || !writeText(output.filePath(u"candidate_metrics.csv"_qs), candidateRows.str())
        || !writeText(output.filePath(u"ineligible_reasons.csv"_qs), rejectedRows.str())
        || !writeText(output.filePath(u"validation_fold_results.csv"_qs), validationRows.str())
        || !writeText(output.filePath(u"validation_worst_fold.csv"_qs), worstFoldRows.str())
        || !writeText(output.filePath(u"physical_finalist_results.csv"_qs), physicalRows.str())
        || !writeText(output.filePath(u"robustness_analysis.csv"_qs), robustnessRows.str())
        || !writeText(output.filePath(u"final_recommendations.md"_qs), recommendations.str())
        || !writeText(output.filePath(u"V2.3.T_Adaptive_Response_Phase2B_Robust_Preset_Optimization_Report.md"_qs), report.str())) return 4;
    QTextStream(stdout) << "Phase 2B complete: candidates=" << candidates.size() << " ineligible=" << ineligible
                        << " changed_presets=" << changed << " final_holdout_replays=0\n";
    return 0;
}

#if 0 // Historical Phase 2 weighted-score runner retained only as uncompiled reference.
int legacyPhase2Main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Offline Phase 2 Adaptive Response preset optimization. No hardware or mapper process is opened."_qs);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty artifact directory."_qs, u"directory"_qs);
    const QCommandLineOption physicalOption(u"physical-corpus"_qs, u"Required directory with exactly 15 trace CSV files."_qs, u"directory"_qs);
    parser.addOptions({outputOption, physicalOption});
    parser.process(app);
    const QString outputPath = parser.value(outputOption).trimmed();
    const QString physicalPath = parser.value(physicalOption).trimmed();
    if (outputPath.isEmpty() || physicalPath.isEmpty()) return 2;
    QDir output(outputPath);
    if (output.exists() && !output.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) return 2;
    if (!QDir().mkpath(outputPath)) { QTextStream(stderr) << "Unable to create output directory\n"; return 3; }
    const QFileInfoList files = QDir(physicalPath).entryInfoList({u"*.csv"_qs}, QDir::Files, QDir::Name);
    if (files.size() != 15) { QTextStream(stderr) << "Expected 15 physical CSV files, found " << files.size() << '\n'; return 3; }
    std::vector<Trace> physical;
    std::uint64_t physicalSamples = 0;
    for (const QFileInfo &file : files) {
        Trace trace = readTrace(file);
        if (trace.times.size() < 2U) { QTextStream(stderr) << "Unable to parse " << file.fileName() << '\n'; return 3; }
        physicalSamples += trace.times.size();
        physical.push_back(std::move(trace));
    }
    const std::vector<ScenarioDefinition> training = canonicalScenarioCatalog(kTrainingSeed);
    const std::vector<ScenarioDefinition> validation = canonicalScenarioCatalog(kValidationSeed);
    const std::array<Spec, 5> specs{{
        {"Light", 2.0F, 8.0F, 0.025F, 0.10F, {0.07,0.22,0.17,0.16,0.13,0.10,0.10,0.05,3.0}},
        {"Balanced", 4.0F,12.0F,0.05F,0.18F, {0.16,0.20,0.15,0.12,0.11,0.08,0.08,0.10,6.0}},
        {"Fast", 6.0F,18.0F,0.08F,0.25F, {0.24,0.16,0.17,0.10,0.09,0.06,0.06,0.12,9.0}},
        {"Aggressive",10.0F,24.0F,0.12F,0.35F,{0.30,0.11,0.18,0.08,0.07,0.04,0.04,0.18,12.0}},
        {"Extreme",16.0F,30.0F,0.20F,0.45F,{0.36,0.08,0.16,0.06,0.06,0.03,0.03,0.22,16.0}},
    }};
    // The Stage 2 parent front stores pointers into this vector. Reserve the
    // bounded full campaign capacity before appending local refinements so a
    // vector reallocation cannot invalidate those test-only references.
    std::vector<Candidate> candidates;
    candidates.reserve(512);
    std::map<std::string, AdaptiveResponseSettings> baseline;
    for (const Spec &spec : specs) {
        const auto source = baselineFor(spec.name);
        if (!source) { QTextStream(stderr) << "Missing baseline " << QString::fromStdString(spec.name) << '\n'; return 3; }
        baseline.emplace(spec.name, *source);
        Candidate current{spec.name + "-baseline", spec.name, "baseline", *source};
        current.metrics = analyze(training, current.settings);
        qualify(&current, spec.weight);
        candidates.push_back(std::move(current));
        for (int index = 1; index <= 40; ++index) {
            Candidate candidate;
            candidate.id = spec.name + "-primary-" + std::to_string(index);
            candidate.preset = spec.name;
            candidate.stage = "primary-search";
            candidate.settings = *source;
            candidate.settings.maximumHorizonMs = spec.horizonLow
                + (spec.horizonHigh - spec.horizonLow) * static_cast<float>((index * 37) % 41) / 40.0F;
            candidate.settings.maximumLead = spec.leadLow
                + (spec.leadHigh - spec.leadLow) * static_cast<float>((index * 19) % 41) / 40.0F;
            candidate.settings.turningPointProtection = 0.55F + 0.45F * static_cast<float>((index * 13) % 41) / 40.0F;
            candidate.settings.turningPointMargin = 0.04F + 0.16F * static_cast<float>((index * 29) % 41) / 40.0F;
            candidate.settings = sanitizedAdaptiveResponseSettings(candidate.settings);
            candidate.metrics = analyze(training, candidate.settings);
            qualify(&candidate, spec.weight);
            candidates.push_back(std::move(candidate));
        }
    }
    for (const Spec &spec : specs) {
        std::vector<Candidate *> parents = front(candidates, spec.name);
        Rng rng(static_cast<std::uint32_t>(std::hash<std::string>{}(spec.name)) ^ kTrainingSeed);
        for (size_t parent = 0; parent < std::min<size_t>(4, parents.size()); ++parent) for (int local = 0; local < 4; ++local) {
            Candidate candidate;
            candidate.id = spec.name + "-secondary-" + std::to_string(parent) + "-" + std::to_string(local);
            candidate.preset = spec.name; candidate.stage = "secondary-refinement"; candidate.settings = parents[parent]->settings;
            candidate.settings.maximumHorizonMs += rng.range(-0.75F,0.75F);
            candidate.settings.maximumLead += rng.range(-0.008F,0.008F);
            candidate.settings.turningPointProtection += rng.range(-0.04F,0.04F);
            candidate.settings.turningPointMargin += rng.range(-0.01F,0.01F);
            candidate.settings.velocityResponse += rng.range(-0.10F,0.10F);
            candidate.settings.accelerationResponse += rng.range(-0.10F,0.10F);
            candidate.settings.horizonExtension += rng.range(-0.12F,0.12F);
            candidate.settings.horizonExtensionCapMs += rng.range(-3.0F,3.0F);
            candidate.settings.reversalResponse += rng.range(-0.10F,0.0F);
            candidate.settings.decelerationResponse += rng.range(-0.10F,0.10F);
            candidate.settings = sanitizedAdaptiveResponseSettings(candidate.settings);
            candidate.metrics = analyze(training, candidate.settings);
            qualify(&candidate, spec.weight);
            candidates.push_back(std::move(candidate));
        }
    }
    std::map<std::string, Candidate *> winners, alternatives;
    for (const Spec &spec : specs) {
        const std::vector<Candidate *> candidatesOnFront = front(candidates, spec.name);
        if (candidatesOnFront.empty()) {
            std::map<std::string, int> reasons;
            for (const Candidate &candidate : candidates) {
                if (candidate.preset == spec.name) ++reasons[candidate.reason.empty() ? "no-pareto-front" : candidate.reason];
            }
            QTextStream(stderr) << "No safe " << QString::fromStdString(spec.name) << " candidate:";
            for (const auto &[reason, count] : reasons) QTextStream(stderr) << ' ' << QString::fromStdString(reason) << '=' << count;
            QTextStream(stderr) << '\n';
            return 5;
        }
        Candidate *selected = candidatesOnFront.front();
        for (Candidate &candidate : candidates) if (candidate.id == spec.name + "-baseline"
            && candidate.eligible && selected->score < candidate.score + 0.015) selected = &candidate;
        winners.emplace(spec.name, selected);
        for (Candidate *candidate : candidatesOnFront) if (candidate != selected) { alternatives.emplace(spec.name, candidate); break; }
    }
    std::ostringstream parameters, metrics, rejected, weights, validationRows, physicalRows, robustness, recommendations, report, manifest;
    parameters << "candidate_id,preset,stage,eligible,maximum_horizon_ms,maximum_lead,turning_protection,turning_margin,velocity_response,acceleration_response,horizon_extension,horizon_extension_cap_ms,reversal_response,deceleration_response,onset_assist,onset_cap,sustained_assist,sustained_cap,settling_response,noise_rejection,motion_sensitivity\n";
    metrics << "candidate_id,preset,stage,eligible,score,future_mae,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,artificial_step,lead_clamp_fraction,horizon_cap_fraction,prediction_inactive_fraction,protection_active_fraction,mean_horizon_ms,p95_horizon_ms,mean_extension_ms,max_extension_ms,non_finite,illegal_output,stationary_offset\n";
    rejected << "candidate_id,preset,stage,reason\n";
    for (const Candidate &candidate : candidates) {
        parameters << csv(candidate.id).toStdString() << ',' << csv(candidate.preset).toStdString() << ',' << csv(candidate.stage).toStdString() << ',' << candidate.eligible << ',' << parameterCsv(candidate.settings) << '\n';
        metrics << csv(candidate.id).toStdString() << ',' << csv(candidate.preset).toStdString() << ',' << csv(candidate.stage).toStdString() << ',' << candidate.eligible << ',' << std::fixed << std::setprecision(8) << candidate.score << ',' << metricsCsv(candidate.metrics) << '\n';
        if (!candidate.eligible) rejected << csv(candidate.id).toStdString() << ',' << csv(candidate.preset).toStdString() << ',' << csv(candidate.stage).toStdString() << ',' << csv(candidate.reason).toStdString() << '\n';
    }
    weights << "preset,response,accuracy,reversal,stop,stability,noise,precision,turning,target_advance_ms\n";
    for (const Spec &spec : specs) weights << spec.name << ',' << spec.weight.response << ',' << spec.weight.accuracy << ',' << spec.weight.reversal << ',' << spec.weight.stop << ',' << spec.weight.stability << ',' << spec.weight.noise << ',' << spec.weight.precision << ',' << spec.weight.turning << ',' << spec.weight.targetAdvanceMs << '\n';
    validationRows << "preset,role,candidate_id,score,future_mae,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,artificial_step,lead_clamp_fraction,horizon_cap_fraction,prediction_inactive_fraction,protection_active_fraction,mean_horizon_ms,p95_horizon_ms,mean_extension_ms,max_extension_ms,non_finite,illegal_output,stationary_offset\n";
    physicalRows << "preset,role,candidate_id,trace_id,future_mae,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,artificial_step,lead_clamp_fraction,horizon_cap_fraction,prediction_inactive_fraction,protection_active_fraction,mean_horizon_ms,p95_horizon_ms,mean_extension_ms,max_extension_ms,non_finite,illegal_output,stationary_offset\n";
    std::uint64_t validationRuns = 0, physicalRuns = 0;
    for (const Spec &spec : specs) {
        Candidate base{spec.name + "-baseline", spec.name, "baseline", baseline.at(spec.name)};
        base.metrics = analyze(training, base.settings); qualify(&base, spec.weight);
        std::array<std::pair<std::string, Candidate *>, 3> finalists{{
            {"recommended", winners.at(spec.name)},
            {"alternate", alternatives.contains(spec.name) ? alternatives.at(spec.name) : winners.at(spec.name)},
            {"baseline", &base},
        }};
        for (const auto &[role, candidate] : finalists) {
            const Metrics validationMetrics = analyze(validation, candidate->settings);
            ++validationRuns;
            validationRows << spec.name << ',' << role << ',' << csv(candidate->id).toStdString() << ',' << calculateScore(validationMetrics, spec.weight) << ',' << metricsCsv(validationMetrics) << '\n';
            for (const Trace &trace : physical) {
                const Metrics physicalMetrics = analyzePhysical(trace, candidate->settings);
                ++physicalRuns;
                physicalRows << spec.name << ',' << role << ',' << csv(candidate->id).toStdString() << ',' << csv(trace.name.toStdString()).toStdString() << ',' << metricsCsv(physicalMetrics) << '\n';
            }
        }
        recommendations << "## " << spec.name << "\n\nRecommended: " << winners.at(spec.name)->id
            << "\nBaseline: " << base.id << "\nVector: " << parameterCsv(winners.at(spec.name)->settings)
            << "\nTraining score: " << winners.at(spec.name)->score << "\n";
    }
    robustness << "preset,candidate_id,perturbation,score_delta,future_mae,temporal_advance_ms,pattern73_wrong_direction_area,stop_residual\n";
    for (const Spec &spec : specs) {
        Candidate *center = winners.at(spec.name);
        for (const std::pair<std::string, std::array<float,4>> &delta : std::array<std::pair<std::string,std::array<float,4>>,4>{{
            {"horizon-minus", {-0.75F,0,0,0}}, {"horizon-plus", {0.75F,0,0,0}}, {"lead-minus",{0,-0.008F,0,0}}, {"lead-plus",{0,0.008F,0,0}}}}) {
            AdaptiveResponseSettings varied = center->settings;
            varied.maximumHorizonMs += delta.second[0]; varied.maximumLead += delta.second[1];
            varied.turningPointProtection += delta.second[2]; varied.turningPointMargin += delta.second[3];
            varied = sanitizedAdaptiveResponseSettings(varied);
            const Metrics result = analyze(training, varied);
            robustness << spec.name << ',' << center->id << ',' << delta.first << ',' << calculateScore(result,spec.weight)-center->score << ',' << result.mae << ',' << result.advanceMs << ',' << result.pattern73Area << ',' << result.stopResidual << '\n';
        }
    }
    std::uint64_t hardFailed = 0;
    for (const Candidate &candidate : candidates) if (!candidate.eligible) ++hardFailed;
    report << "# V2.3.T Adaptive Response Phase 2 Preset Optimization Report\n\n"
        << "Freeze SHA: " << HOTAS_PHASE2_FREEZE_SHA << "\n\n"
        << "Training seed and reference SHA-256: 0x23A10001 / " << kTrainingHash << "\n"
        << "Validation seed and reference SHA-256: 0x23A10002 / " << kValidationHash << "\n"
        << "Holdout untouched: 0x23A10003 / " << kHoldoutHash << "\n"
        << "Physical corpus: 15 unique recorded traces and " << physicalSamples << " source samples.\n"
        << "Candidates evaluated: " << candidates.size() << ". Hard-failed: " << hardFailed << ".\n"
        << "Validation finalist executions: " << validationRuns << ". Physical finalist replays: " << physicalRuns << ".\n\n"
        << "Objective weights are in objective_weights.csv. Primary ranges and all candidates are in candidate_parameters.csv. Raw metrics, rejections, Pareto fronts, validation, physical traces, and robustness are separate machine-readable artifacts.\n\n"
        << "Pattern-73 candidates are fail-closed at wrong-direction area <= 0.0001. The classifier and production model were not modified. The holdout was not loaded or replayed.\n\n"
        << "Phase 2 recommendation vectors are test-only. No shipping preset was changed. Phase 3 holdout validation is required before any production change.\n\n"
        << "Final disposition: PHASE 2 PASS - EMPIRICALLY OPTIMIZED PRESET CANDIDATES READY FOR HOLDOUT VALIDATION\n";
    manifest << "{\n  \"freeze_sha\": \"" << HOTAS_PHASE2_FREEZE_SHA << "\",\n  \"training_seed\": \"0x23A10001\",\n  \"training_reference_sha256\": \"" << kTrainingHash << "\",\n  \"validation_seed\": \"0x23A10002\",\n  \"validation_reference_sha256\": \"" << kValidationHash << "\",\n  \"holdout_seed\": \"0x23A10003\",\n  \"holdout_reference_sha256\": \"" << kHoldoutHash << "\",\n  \"holdout_replayed\": false,\n  \"physical_traces\": 15,\n  \"physical_source_samples\": " << physicalSamples << ",\n  \"candidates\": " << candidates.size() << "\n}\n";
    const std::string policy = "# Phase 2 optimization policy\n\nThe exact frozen production estimator is linked in an offline target only. Stage 1 jointly searches horizon, lead, turning protection, and turning margin with deterministic low-discrepancy samples. Stage 2 locally refines the six permitted secondary dimensions. Objective weights are fixed before ranking. Independent validation is evaluated only after finalist selection. Physical recordings are veto and comparison evidence only. The holdout is prohibited.\n";
    const std::string primary = "preset,primary_dimensions,search_count\nLight,horizon;lead;turning_protection;turning_margin,40\nBalanced,horizon;lead;turning_protection;turning_margin,40\nFast,horizon;lead;turning_protection;turning_margin,40\nAggressive,horizon;lead;turning_protection;turning_margin,40\nExtreme,horizon;lead;turning_protection;turning_margin,40\n";
    const std::string secondary = "preset,secondary_dimensions,local_search\nall,velocity_response;acceleration_response;horizon_extension;horizon_extension_cap;reversal_response;deceleration_response,up_to_16\n";
    if (!writeText(output.filePath(u"manifest.json"_qs), manifest.str())
        || !writeText(output.filePath(u"optimization_policy.md"_qs), policy)
        || !writeText(output.filePath(u"objective_weights.csv"_qs), weights.str())
        || !writeText(output.filePath(u"candidate_parameters.csv"_qs), parameters.str())
        || !writeText(output.filePath(u"candidate_metrics.csv"_qs), metrics.str())
        || !writeText(output.filePath(u"rejected_candidate_reasons.csv"_qs), rejected.str())
        || !writeText(output.filePath(u"primary_search_summary.csv"_qs), primary)
        || !writeText(output.filePath(u"secondary_refinement_summary.csv"_qs), secondary)
        || !writeText(output.filePath(u"validation_results.csv"_qs), validationRows.str())
        || !writeText(output.filePath(u"physical_finalist_results.csv"_qs), physicalRows.str())
        || !writeText(output.filePath(u"robustness_analysis.csv"_qs), robustness.str())
        || !writeText(output.filePath(u"final_recommendations.md"_qs), recommendations.str())
        || !writeText(output.filePath(u"V2.3.T_Adaptive_Response_Phase2_Preset_Optimization_Report.md"_qs), report.str())) return 4;
    for (const Spec &spec : specs) {
        std::ostringstream pareto;
        pareto << "candidate_id,preset,stage,score,maximum_horizon_ms,maximum_lead,turning_protection,turning_margin,velocity_response,acceleration_response,horizon_extension,horizon_extension_cap_ms,reversal_response,deceleration_response,onset_assist,onset_cap,sustained_assist,sustained_cap,settling_response,noise_rejection,motion_sensitivity\n";
        for (Candidate *candidate : front(candidates, spec.name))
            pareto << csv(candidate->id).toStdString() << ',' << spec.name << ',' << candidate->stage << ',' << candidate->score << ',' << parameterCsv(candidate->settings) << '\n';
        if (!writeText(output.filePath(QString::fromStdString("pareto_" + spec.name + ".csv")), pareto.str())) return 4;
    }
    QTextStream(stdout) << "Phase 2 complete: candidates=" << candidates.size() << " hard_failed=" << hardFailed << " validation=" << validationRuns << " physical=" << physicalRuns << "\n";
    return 0;
}
#endif // Historical Phase 2 runner
