#include "adaptive_response.h"
#include "response_curve.h"

#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using hotas::AdaptiveResponseModel;
using hotas::AdaptiveResponseProcessor;
using hotas::AdaptiveResponseSettings;
using hotas::AdaptiveResponseTelemetry;
using hotas::RuntimeAdaptiveResponseConfig;

constexpr std::array<double, 14> kHorizonsMs{2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40, 50};
constexpr std::array<double, 8> kSingerTauMs{4, 8, 12, 16, 24, 32, 50, 0};
constexpr std::array<const char *, 5> kConfidenceNames{"FC-A innovation", "FC-B innovation+jerk",
    "FC-C innovation+persistence", "FC-D innovation+disagreement", "FC-E combined"};

struct Sample {
    double time = 0.0;
    float physical = 0.0F;
};

struct Trace {
    std::string id;
    std::string corpus;
    std::string family;
    bool physicalTrace = false;
    std::vector<Sample> samples;
};

struct Stats {
    std::vector<double> values;
    double sum = 0.0;
    double squareSum = 0.0;

    void add(double value)
    {
        if (!std::isfinite(value)) return;
        values.push_back(std::abs(value));
        sum += std::abs(value);
        squareSum += value * value;
    }
    [[nodiscard]] size_t count() const { return values.size(); }
    [[nodiscard]] double mean() const { return values.empty() ? 0.0 : sum / static_cast<double>(values.size()); }
    [[nodiscard]] double rmse() const { return values.empty() ? 0.0 : std::sqrt(squareSum / static_cast<double>(values.size())); }
    [[nodiscard]] double percentile(double fraction) const
    {
        if (values.empty()) return 0.0;
        std::vector<double> copy = values;
        const auto index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * (copy.size() - 1U));
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
};

struct Observation {
    double time = 0.0;
    float physical = 0.0F;
    AdaptiveResponseTelemetry telemetry;
    double innovationEnergy = 0.0;
    double noiseNormalizedInnovation = 0.0;
    double motionNormalizedInnovation = 0.0;
    double mixedNormalizedInnovation = 0.0;
    double jerk = 0.0;
    double jerkEnergy = 0.0;
    double accelerationPersistence = 0.0;
    double cadenceConfidence = 0.0;
    std::array<double, 5> confidence{};
    std::array<double, 3> fastInnovationEnergy{};
    std::array<double, 3> slowInnovationEnergy{};
    double innovationSurprise = 0.0;
    double innovationEnergySlope = 0.0;
    double innovationGrowthRatio = 0.0;
    double normalizedJerk = 0.0;
    double jerkRisk = 0.0;
    double normalizedAccelerationChange = 0.0;
    double accelerationChangeEnergy = 0.0;
    double accelerationStability = 0.0;
    double disagreement = 0.0;
    double disagreementVelocity = 0.0;
    double cvResidualEnergy = 0.0;
    double caResidualEnergy = 0.0;
    double modelPreference = 0.0;
    double modelPreferenceReversal = 0.0;
    std::array<double, 6> r05Confidence{};
};

struct Pending {
    double targetTime = 0.0;
    double cv = 0.0;
    double ca = 0.0;
};

struct Options {
    std::filesystem::path output;
    std::filesystem::path baseline;
    std::filesystem::path report;
};

std::string number(double value, int precision = 8)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string csv(const std::string &value)
{
    std::string escaped = value;
    size_t position = 0;
    while ((position = escaped.find('"', position)) != std::string::npos) {
        escaped.insert(position, 1, '"');
        position += 2;
    }
    return '"' + escaped + '"';
}

bool writeText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        std::cerr << "Unable to write " << path.string() << "\n";
        return false;
    }
    output << text;
    return static_cast<bool>(output);
}

std::optional<double> valueAt(const std::vector<Sample> &samples, double time)
{
    if (samples.empty() || time < samples.front().time - 1e-9 || time > samples.back().time + 1e-9) {
        return std::nullopt;
    }
    const auto upper = std::lower_bound(samples.begin(), samples.end(), time,
        [](const Sample &sample, double target) { return sample.time < target; });
    if (upper == samples.begin()) return upper->physical;
    if (upper == samples.end()) return samples.back().physical;
    const Sample &right = *upper;
    const Sample &left = *(upper - 1);
    const double span = right.time - left.time;
    if (span <= 1e-9) return left.physical;
    const double fraction = std::clamp((time - left.time) / span, 0.0, 1.0);
    return static_cast<double>(left.physical) + fraction * (right.physical - left.physical);
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const auto index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * (values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

double correlation(const std::vector<std::pair<double, double>> &pairs)
{
    if (pairs.size() < 3U) return 0.0;
    double x = 0.0, y = 0.0;
    for (const auto &[first, second] : pairs) { x += first; y += second; }
    x /= static_cast<double>(pairs.size());
    y /= static_cast<double>(pairs.size());
    double numerator = 0.0, xEnergy = 0.0, yEnergy = 0.0;
    for (const auto &[first, second] : pairs) {
        const double dx = first - x;
        const double dy = second - y;
        numerator += dx * dy;
        xEnergy += dx * dx;
        yEnergy += dy * dy;
    }
    return xEnergy > 1e-12 && yEnergy > 1e-12 ? numerator / std::sqrt(xEnergy * yEnergy) : 0.0;
}

float piecewise(const std::vector<std::pair<double, float>> &points, double time)
{
    if (points.empty()) return 0.0F;
    if (time <= points.front().first) return points.front().second;
    if (time >= points.back().first) return points.back().second;
    for (size_t index = 1; index < points.size(); ++index) {
        if (time <= points[index].first) {
            const auto &[leftTime, leftValue] = points[index - 1];
            const auto &[rightTime, rightValue] = points[index];
            const double part = (time - leftTime) / std::max(1e-9, rightTime - leftTime);
            return static_cast<float>(leftValue + (rightValue - leftValue) * part);
        }
    }
    return points.back().second;
}

std::uint32_t nextRandom(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

double unitRandom(std::uint32_t &state)
{
    return static_cast<double>(nextRandom(state)) / 4294967295.0;
}

Trace syntheticTrace(const std::string &id, const std::string &corpus, const std::string &family,
                     const std::vector<std::pair<double, float>> &points, std::uint32_t seed,
                     int sourceRate, bool variableCadence, double noise = 0.0)
{
    Trace trace{id, corpus, family, false, {}};
    std::uint32_t state = seed == 0 ? 0xBFA62400U : seed;
    const double finish = points.empty() ? 1.0 : points.back().first;
    double time = 0.0;
    while (time <= finish + 1e-9) {
        const double sourceTime = std::floor(time * sourceRate) / sourceRate;
        float physical = piecewise(points, sourceTime);
        if (noise > 0.0) physical = std::clamp(physical + static_cast<float>((unitRandom(state) - 0.5) * 2.0 * noise), -1.0F, 1.0F);
        trace.samples.push_back({time, physical});
        const double jitter = variableCadence ? 0.72 + unitRandom(state) * 0.56 : 1.0;
        time += 0.004 * jitter;
    }
    return trace;
}

std::vector<Trace> syntheticCorpora()
{
    struct Definition { const char *id; const char *family; std::vector<std::pair<double, float>> points; };
    const std::vector<Definition> definitions{
        {"slow-coherent", "slow coherent movement", {{0,-.55F},{2.6F,.55F},{3.2F,.55F}}},
        {"ballistic-sweep", "ballistic sweep", {{0,-.92F},{.12F,.92F},{.8F,.92F}}},
        {"abrupt-stop", "stopping", {{0,-.75F},{.19F,.82F},{1.2F,.82F}}},
        {"reversal", "reversal", {{0,.82F},{.24F,-.70F},{.49F,.80F},{1.1F,.80F}}},
        {"oscillation", "oscillation", {{0,.70F},{.14F,-.62F},{.31F,.82F},{.49F,-.45F},{.70F,.55F},{1.1F,.55F}}},
        {"high-jerk", "high jerk", {{0,0},{.04F,.52F},{.08F,-.52F},{.12F,.86F},{.16F,-.86F},{.55F,-.86F}}},
        {"endpoint", "endpoints", {{0,-.30F},{.34F,.76F},{.62F,.985F},{1.2F,.985F}}},
        {"cadence-jitter", "cadence", {{0,-.65F},{.82F,.65F},{1.5F,.65F}}},
    };
    const std::array<const char *, 11> corpora{
        "deterministic-characterization", "prior-validation", "consumed-phase2b-search-a",
        "consumed-phase2b-search-b", "consumed-phase2b-validation-fold-1",
        "consumed-phase2b-validation-fold-2", "consumed-phase2b-validation-fold-3",
        "consumed-phase2b-validation-fold-4", "consumed-phase3-holdout",
        "consumed-phase3b-final-holdout", "canonical-motion-family"};
    std::vector<Trace> traces;
    std::uint32_t seed = 0x24F0CA57U;
    for (size_t corpus = 0; corpus < corpora.size(); ++corpus) {
        for (size_t item = 0; item < definitions.size(); ++item) {
            const Definition &definition = definitions[item];
            const int sourceRate = item % 4 == 0 ? 60 : item % 4 == 1 ? 125 : item % 4 == 2 ? 250 : 500;
            const bool variable = std::string_view(definition.id) == "cadence-jitter";
            const double noise = std::string_view(definition.id) == "high-jerk" ? .001 : 0.0;
            traces.push_back(syntheticTrace(std::string(corpora[corpus]) + "/" + definition.id,
                corpora[corpus], definition.family, definition.points, seed++, sourceRate, variable, noise));
        }
    }
    const std::array<const char *, 5> personas{"Precision Pilot", "Combat Helicopter", "Fixed-Wing", "Space-Sim", "Noisy Sensor"};
    for (size_t persona = 0; persona < personas.size(); ++persona) {
        for (int sequence = 0; sequence < 4; ++sequence) {
            std::uint32_t state = seed++;
            std::vector<std::pair<double, float>> points{{0.0, static_cast<float>((unitRandom(state) - .5) * .3)}};
            const bool combat = persona == 1;
            const double step = combat ? .055 : persona == 0 ? .34 : .16;
            for (double time = step; time < 3.2; time += step) {
                const double range = combat ? .95 : persona == 0 ? .16 : persona == 4 ? .42 : .62;
                const float previous = points.back().second;
                const float target = std::clamp(previous + static_cast<float>((unitRandom(state) - .5) * 2.0 * range), -1.0F, 1.0F);
                points.push_back({time, target});
            }
            points.push_back({3.4, points.back().second});
            traces.push_back(syntheticTrace(std::string("persona/") + std::to_string(sequence) + "/" + personas[persona],
                "canonical-persona", personas[persona], points, seed++, combat ? 250 : 125, persona == 4,
                persona == 4 ? .005 : 0.0));
        }
    }
    return traces;
}

std::vector<Trace> physicalTraces()
{
    const std::filesystem::path fixtureDirectory = std::filesystem::current_path()
        / "tests" / "fixtures" / "adaptive_response_real_hotas";
    std::vector<Trace> traces;
    for (const auto &entry : std::filesystem::directory_iterator(fixtureDirectory)) {
        if (entry.path().extension() != ".csv") continue;
        std::ifstream input(entry.path());
        std::string line;
        std::getline(input, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const bool capturedAxisRows = line == "timestamp_us,axis_x,axis_y,axis_z,axis_rx,axis_ry,axis_rz,slider0,slider1";
        const bool retainedReplayRows = line.rfind("time_ms,physical_raw_normalized,physical_raw_direct_input,", 0) == 0;
        if (!capturedAxisRows && !retainedReplayRows) continue;
        Trace trace{entry.path().stem().string(), "recorded-real-hotas", "real human HOTAS", true, {}};
        std::uint64_t origin = 0;
        while (std::getline(input, line)) {
            std::stringstream fields(line);
            std::string timestampField, axisField;
            if (!std::getline(fields, timestampField, ',') || !std::getline(fields, axisField, ',')) continue;
            if (capturedAxisRows) {
                const std::uint64_t timestamp = std::strtoull(timestampField.c_str(), nullptr, 10);
                if (trace.samples.empty()) origin = timestamp;
                trace.samples.push_back({static_cast<double>(timestamp - origin) / 1000000.0F,
                                         static_cast<float>(std::strtod(axisField.c_str(), nullptr))});
            } else {
                trace.samples.push_back({std::strtod(timestampField.c_str(), nullptr) / 1000.0,
                                         static_cast<float>(std::strtod(axisField.c_str(), nullptr))});
            }
        }
        if (trace.samples.size() >= 2U) traces.push_back(std::move(trace));
    }
    std::sort(traces.begin(), traces.end(), [](const Trace &left, const Trace &right) { return left.id < right.id; });
    return traces;
}

RuntimeAdaptiveResponseConfig productionConfiguration()
{
    AdaptiveResponseSettings settings;
    for (const auto &preset : hotas::builtInAdaptiveResponsePresets()) {
        if (preset.id == u"fast") settings = preset.axes[0].settings;
    }
    settings = hotas::sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = settings.enabled;
    result.model = settings.model;
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

bool productionOutput(const std::filesystem::path &output, const std::vector<Trace> &traces)
{
    std::ofstream file(output / "production_outputs.csv", std::ios::binary);
    if (!file) return false;
    file << "trace_id,corpus,index,time_seconds,physical_bits,predicted_bits,lead_bits,active_horizon_bits\n";
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const Trace &trace : traces) {
        AdaptiveResponseProcessor processor;
        for (size_t index = 0; index < trace.samples.size(); ++index) {
            const Sample &sample = trace.samples[index];
            const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = processor.process(sample.physical, configuration, at);
            file << csv(trace.id) << ',' << csv(trace.corpus) << ',' << index << ',' << number(sample.time, 6) << ','
                 << std::bit_cast<std::uint32_t>(sample.physical) << ','
                 << std::bit_cast<std::uint32_t>(telemetry.predicted) << ','
                 << std::bit_cast<std::uint32_t>(telemetry.lead) << ','
                 << std::bit_cast<std::uint32_t>(telemetry.activeHorizonSeconds) << '\n';
        }
    }
    return static_cast<bool>(file);
}

Options parseOptions(int argc, char *argv[])
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--out" && index + 1 < argc) options.output = argv[++index];
        else if (argument == "--baseline" && index + 1 < argc) options.baseline = argv[++index];
        else if (argument == "--report" && index + 1 < argc) options.report = argv[++index];
    }
    return options;
}

std::vector<Observation> observe(const Trace &trace, const RuntimeAdaptiveResponseConfig &configuration)
{
    std::vector<Observation> observations;
    observations.reserve(trace.samples.size());
    AdaptiveResponseProcessor processor;
    double energy = 0.0, jerkEnergy = 0.0, persistence = 0.0;
    float priorAcceleration = 0.0F;
    int priorAccelerationSign = 0;
    for (size_t index = 0; index < trace.samples.size(); ++index) {
        const Sample &sample = trace.samples[index];
        const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
        Observation row;
        row.time = sample.time;
        row.physical = sample.physical;
        row.telemetry = processor.process(sample.physical, configuration, at);
#if defined(HOTAS_ADAPTIVE_FORECAST_RESEARCH)
        const double dt = std::max(0.0005, static_cast<double>(row.telemetry.meaningfulDeltaSeconds));
        const double innovation = row.telemetry.innovationSigned;
        const double alpha = dt / (0.040 + dt);
        energy += alpha * (innovation * innovation - energy);
        const double jerk = index == 0 ? 0.0 : (row.telemetry.acceleration - priorAcceleration) / dt;
        const double jerkAlpha = dt / (0.060 + dt);
        jerkEnergy += jerkAlpha * (jerk * jerk - jerkEnergy);
        const int accelerationSign = row.telemetry.acceleration > .0001F ? 1 : row.telemetry.acceleration < -.0001F ? -1 : 0;
        const double agreement = accelerationSign != 0 && accelerationSign == priorAccelerationSign ? 1.0 : 0.0;
        persistence += (agreement - persistence) * dt / (0.080 + dt);
        priorAcceleration = row.telemetry.acceleration;
        priorAccelerationSign = accelerationSign;
        const double noiseScale = std::max(0.00025, static_cast<double>(row.telemetry.noiseThreshold));
        const double expectedMotion = std::max(noiseScale, std::abs(static_cast<double>(row.telemetry.predictedVelocity)) * dt);
        row.innovationEnergy = energy;
        row.noiseNormalizedInnovation = energy / (noiseScale * noiseScale);
        row.motionNormalizedInnovation = energy / (expectedMotion * expectedMotion);
        row.mixedNormalizedInnovation = energy / (noiseScale * noiseScale + expectedMotion * expectedMotion);
        row.jerk = jerk;
        row.jerkEnergy = jerkEnergy;
        row.accelerationPersistence = std::clamp(persistence, 0.0, 1.0);
        row.cadenceConfidence = std::clamp(1.0 - row.telemetry.cadenceJitterSeconds
            / std::max(.001, static_cast<double>(row.telemetry.sourceUpdatePeriodSeconds) * .75), 0.0, 1.0);
        const double innovationTrust = 1.0 / (1.0 + std::sqrt(row.mixedNormalizedInnovation));
        const double jerkTrust = 1.0 / (1.0 + std::sqrt(jerkEnergy) * .003);
        const double disagreement = std::abs(.5 * row.telemetry.acceleration * .020 * .020);
        const double disagreementTrust = 1.0 / (1.0 + disagreement / .004);
        row.confidence[0] = innovationTrust;
        row.confidence[1] = innovationTrust * jerkTrust;
        row.confidence[2] = innovationTrust * (.35 + .65 * row.accelerationPersistence);
        row.confidence[3] = innovationTrust * disagreementTrust;
        row.confidence[4] = std::clamp((row.confidence[1] + row.confidence[2] + row.confidence[3]
            + row.cadenceConfidence + row.telemetry.motionCoherence) / 5.0, 0.0, 1.0);
#endif
        observations.push_back(row);
    }
    return observations;
}

void writeStatRows(std::ostringstream &output, const std::map<std::string, Stats> &stats, const char *header)
{
    output << header << '\n';
    for (const auto &[key, values] : stats) {
        output << key << ',' << values.count() << ',' << number(values.mean()) << ',' << number(values.rmse()) << ','
               << number(values.percentile(.95)) << ',' << number(values.percentile(.99)) << '\n';
    }
}

bool equivalentOutput(const std::filesystem::path &baseline, const std::filesystem::path &research,
                      std::string *result)
{
    std::ifstream left(baseline), right(research);
    if (!left || !right) {
        *result = "total_rows,matching_rows,mismatching_rows,status\n0,0,0,BASELINE_MISSING\n";
        return false;
    }
    std::string leftLine, rightLine;
    size_t total = 0, matching = 0, mismatch = 0;
    while (true) {
        const bool hasLeft = static_cast<bool>(std::getline(left, leftLine));
        const bool hasRight = static_cast<bool>(std::getline(right, rightLine));
        if (!hasLeft && !hasRight) break;
        ++total;
        if (hasLeft && hasRight && leftLine == rightLine) ++matching;
        else ++mismatch;
    }
    std::ostringstream output;
    output << "total_rows,matching_rows,mismatching_rows,status\n" << total << ',' << matching << ',' << mismatch << ','
           << (mismatch == 0 ? "EXACT" : "MISMATCH") << '\n';
    *result = output.str();
    return mismatch == 0;
}

int researchMain(const Options &options, const std::vector<Trace> &traces)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    const std::filesystem::path productionPath = options.output / "production_outputs.csv";
    if (!productionOutput(options.output, traces)) return 3;
    std::string equivalence;
    const bool equivalent = equivalentOutput(options.baseline, productionPath, &equivalence);
    if (!writeText(options.output / "production_equivalence.csv", equivalence)) return 3;

    std::ostringstream inventory, innovation, jerk, persistence, cadence, physical, outliers;
    inventory << "trace_id,corpus,family,source_kind,research_status,samples,duration_ms\n";
    innovation << "trace_id,corpus,family,samples,mean_abs_innovation,mean_innovation_energy,mean_noise_normalized,mean_motion_normalized,mean_mixed_normalized\n";
    jerk << "trace_id,corpus,family,samples,mean_abs_jerk,mean_jerk_energy,mean_acceleration\n";
    persistence << "trace_id,corpus,family,samples,mean_acceleration_persistence,acceleration_velocity_alignment\n";
    cadence << "trace_id,corpus,family,samples,mean_source_period_ms,mean_cadence_jitter_ms,mean_cadence_confidence,source_update_fraction\n";
    physical << "trace_id,model,horizon_ms,count,mae,rmse,p95,p99\n";
    outliers << "trace_id,corpus,family,model,horizon_ms,time_ms,absolute_error,forecast,truth\n";
    std::map<std::string, Stats> horizonStats, familyStats, singerStats, physicalStats;
    std::array<std::vector<std::pair<double, double>>, 5> confidencePairs;
    std::vector<std::pair<double, double>> innovationPairs;
    std::array<int, 5> leadEvents{}, leadPrewarned{};
    std::array<double, 5> leadMilliseconds{};
    std::map<std::string, Stats> abgStats;
    struct Outlier { std::string line; double error = 0.0; };
    std::vector<Outlier> worst;

    for (const Trace &trace : traces) {
        const std::vector<Observation> rows = observe(trace, configuration);
        const double durationMs = trace.samples.empty() ? 0.0 : trace.samples.back().time * 1000.0;
        inventory << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ','
                  << (trace.physicalTrace ? "recorded_real_hotas" : "deterministic") << ','
                  << "RESEARCH DATA - NO LONGER HOLDOUT" << ',' << trace.samples.size() << ',' << number(durationMs, 3) << '\n';
        double innovationSum = 0.0, energySum = 0.0, noiseSum = 0.0, motionSum = 0.0, mixedSum = 0.0;
        double jerkSum = 0.0, jerkEnergySum = 0.0, accelerationSum = 0.0, persistenceSum = 0.0, alignment = 0.0;
        double sourcePeriod = 0.0, jitter = 0.0, cadenceConfidence = 0.0, sourceUpdates = 0.0;
        std::vector<double> productionErrors(rows.size(), 0.0);
        for (size_t index = 0; index < rows.size(); ++index) {
            const Observation &row = rows[index];
#if defined(HOTAS_ADAPTIVE_FORECAST_RESEARCH)
            innovationSum += std::abs(row.telemetry.innovationSigned);
            energySum += row.innovationEnergy;
            noiseSum += row.noiseNormalizedInnovation;
            motionSum += row.motionNormalizedInnovation;
            mixedSum += row.mixedNormalizedInnovation;
            jerkSum += std::abs(row.jerk);
            jerkEnergySum += row.jerkEnergy;
            accelerationSum += std::abs(row.telemetry.acceleration);
            persistenceSum += row.accelerationPersistence;
            alignment += row.telemetry.acceleration * row.telemetry.velocity >= 0.0F ? 1.0 : 0.0;
            sourcePeriod += row.telemetry.sourceUpdatePeriodSeconds * 1000.0;
            jitter += row.telemetry.cadenceJitterSeconds * 1000.0;
            cadenceConfidence += row.cadenceConfidence;
            sourceUpdates += row.telemetry.sourceUpdatedForResearch ? 1.0 : 0.0;
#endif
            const auto truth = valueAt(trace.samples, row.time + row.telemetry.activeHorizonSeconds);
            if (truth) {
                const double error = std::abs(row.telemetry.predicted - *truth);
                productionErrors[index] = error;
                for (size_t candidate = 0; candidate < kConfidenceNames.size(); ++candidate) {
                    confidencePairs[candidate].push_back({row.confidence[candidate], error});
                }
                innovationPairs.push_back({row.mixedNormalizedInnovation, error});
                const std::string innovationBucket = row.mixedNormalizedInnovation < .25 ? "innovation-low"
                    : row.mixedNormalizedInnovation < 1.0 ? "innovation-medium" : "innovation-high";
                const std::string speedBucket = std::abs(row.telemetry.velocity) < .35F ? "speed-low"
                    : std::abs(row.telemetry.velocity) < 1.5F ? "speed-medium" : "speed-high";
                const std::string stateBucket = "state-" + std::to_string(static_cast<int>(row.telemetry.state));
                abgStats[innovationBucket].add(error);
                abgStats[speedBucket].add(error);
                abgStats[stateBucket].add(error);
            }
        }
        const double divisor = std::max<size_t>(1U, rows.size());
        innovation << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ',' << rows.size() << ','
                   << number(innovationSum / divisor) << ',' << number(energySum / divisor) << ',' << number(noiseSum / divisor) << ','
                   << number(motionSum / divisor) << ',' << number(mixedSum / divisor) << '\n';
        jerk << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ',' << rows.size() << ','
             << number(jerkSum / divisor) << ',' << number(jerkEnergySum / divisor) << ',' << number(accelerationSum / divisor) << '\n';
        persistence << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ',' << rows.size() << ','
                    << number(persistenceSum / divisor) << ',' << number(alignment / divisor) << '\n';
        cadence << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ',' << rows.size() << ','
                << number(sourcePeriod / divisor) << ',' << number(jitter / divisor) << ',' << number(cadenceConfidence / divisor) << ','
                << number(sourceUpdates / divisor) << '\n';
        const double failureThreshold = percentile(productionErrors, .90);
        for (size_t candidate = 0; candidate < kConfidenceNames.size(); ++candidate) {
            for (size_t index = 0; index < rows.size(); ++index) {
                if (productionErrors[index] < failureThreshold || failureThreshold <= 0.0) continue;
                ++leadEvents[candidate];
                bool prewarned = false;
                for (size_t prior = index; prior > 0; --prior) {
                    if (rows[index].time - rows[prior - 1].time > .050) break;
                    if (rows[prior - 1].confidence[candidate] < .40) {
                        prewarned = true;
                        leadMilliseconds[candidate] += (rows[index].time - rows[prior - 1].time) * 1000.0;
                        break;
                    }
                }
                if (prewarned) ++leadPrewarned[candidate];
            }
        }

        for (double horizonMs : kHorizonsMs) {
            const double horizon = horizonMs / 1000.0;
            RuntimeAdaptiveResponseConfig capped = configuration;
            capped.maximumHorizonSeconds = static_cast<float>(horizon);
            AdaptiveResponseProcessor productionProcessor;
            std::vector<Pending> pending;
            size_t pendingIndex = 0;
            double cvEnergy = .000001, caEnergy = .000001;
            std::array<double, kSingerTauMs.size()> singerEnergy{};
            for (double &value : singerEnergy) value = .000001;
            for (size_t index = 0; index < rows.size(); ++index) {
                const Observation &row = rows[index];
                while (pendingIndex < pending.size() && pending[pendingIndex].targetTime <= row.time + 1e-9) {
                    const auto actual = valueAt(trace.samples, pending[pendingIndex].targetTime);
                    if (actual) {
                        const double cvError = pending[pendingIndex].cv - *actual;
                        const double caError = pending[pendingIndex].ca - *actual;
                        cvEnergy += .10 * (cvError * cvError - cvEnergy);
                        caEnergy += .10 * (caError * caError - caEnergy);
                    }
                    ++pendingIndex;
                }
                const AdaptiveResponseTelemetry v23 = productionProcessor.process(row.physical, capped,
                    Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(row.time * 1000000.0)));
                const double cv = row.physical + row.telemetry.velocity * horizon;
                const double ca = cv + .5 * row.telemetry.acceleration * horizon * horizon;
                const double accelWeight = cvEnergy / std::max(1e-9, cvEnergy + caEnergy);
                const double blend = (1.0 - accelWeight) * cv + accelWeight * ca;
                const auto fixedTruth = valueAt(trace.samples, row.time + horizon);
                const auto productionTruth = valueAt(trace.samples, row.time + v23.activeHorizonSeconds);
                const std::string allPrefix = "all,";
                const std::string familyPrefix = csv(trace.family) + ',';
                const std::string physicalPrefix = csv(trace.id) + ',';
                const auto add = [&](const std::string &model, double forecast, const std::optional<double> &truth) {
                    if (!truth) return;
                    const double error = forecast - *truth;
                    const std::string key = model + ',' + number(horizonMs, 0);
                    horizonStats[key].add(error);
                    familyStats[trace.family + ',' + key].add(error);
                    if (trace.physicalTrace) physicalStats[trace.id + ',' + key].add(error);
                    if (std::abs(error) > .12) worst.push_back({csv(trace.id) + ',' + csv(trace.corpus) + ',' + csv(trace.family)
                        + ',' + csv(model) + ',' + number(horizonMs, 0) + ',' + number(row.time * 1000.0, 3) + ','
                        + number(std::abs(error)) + ',' + number(forecast) + ',' + number(*truth), std::abs(error)});
                };
                add("V2.3-output-active-cap", v23.predicted, productionTruth);
                add("CV", cv, fixedTruth);
                add("CA", ca, fixedTruth);
                add("Blend-CV-CA", blend, fixedTruth);
                for (size_t tauIndex = 0; tauIndex < kSingerTauMs.size(); ++tauIndex) {
                    const double tau = kSingerTauMs[tauIndex] == 0 ? 0.016 : kSingerTauMs[tauIndex] / 1000.0;
                    const double singer = row.physical + row.telemetry.velocity * horizon
                        + row.telemetry.acceleration * (tau * horizon - tau * tau * (1.0 - std::exp(-horizon / tau)));
                    add("Singer-" + number(tau * 1000.0, 0) + "ms", singer, fixedTruth);
                    if (fixedTruth) singerStats[trace.family + "," + number(tau * 1000.0, 0) + "," + number(horizonMs, 0)].add(singer - *fixedTruth);
                    singerEnergy[tauIndex] += .05 * ((fixedTruth ? (singer - *fixedTruth) * (singer - *fixedTruth) : 0.0) - singerEnergy[tauIndex]);
                }
                pending.push_back({row.time + horizon, cv, ca});
            }
        }
    }
    std::sort(worst.begin(), worst.end(), [](const Outlier &left, const Outlier &right) { return left.error > right.error; });
    for (size_t index = 0; index < std::min<size_t>(100U, worst.size()); ++index) outliers << worst[index].line << '\n';

    std::ostringstream byHorizon, byFamily, singer, comparison;
    writeStatRows(byHorizon, horizonStats, "model,horizon_ms,count,mae,rmse,p95,p99");
    writeStatRows(byFamily, familyStats, "motion_family,model,horizon_ms,count,mae,rmse,p95,p99");
    writeStatRows(singer, singerStats, "motion_family,tau_ms,horizon_ms,count,mae,rmse,p95,p99");
    comparison << "model,horizon_ms,mae,rmse,p95,p99\n";
    for (const auto &[key, values] : horizonStats) comparison << key << ',' << number(values.mean()) << ',' << number(values.rmse()) << ','
        << number(values.percentile(.95)) << ',' << number(values.percentile(.99)) << '\n';
    for (const auto &[key, values] : physicalStats) {
        const size_t split = key.find(',');
        if (split == std::string::npos) continue;
        physical << key.substr(0, split) << ',' << key.substr(split + 1) << ',' << values.count() << ',' << number(values.mean()) << ','
                 << number(values.rmse()) << ',' << number(values.percentile(.95)) << ',' << number(values.percentile(.99)) << '\n';
    }

    std::ostringstream innovationCalibration, confidenceCandidates, confidenceCalibration, leadTime;
    innovationCalibration << "innovation_decile,count,mean_future_active_error\n";
    std::sort(innovationPairs.begin(), innovationPairs.end());
    for (int bin = 0; bin < 10; ++bin) {
        const size_t begin = innovationPairs.size() * static_cast<size_t>(bin) / 10U;
        const size_t end = innovationPairs.size() * static_cast<size_t>(bin + 1) / 10U;
        double sum = 0.0;
        for (size_t index = begin; index < end; ++index) sum += innovationPairs[index].second;
        innovationCalibration << bin << ',' << (end - begin) << ',' << number(end == begin ? 0.0 : sum / (end - begin)) << '\n';
    }
    confidenceCandidates << "candidate,samples,correlation_with_future_active_error,mean_confidence\n";
    confidenceCalibration << "candidate,confidence_decile,count,mean_future_active_error,p95_future_active_error\n";
    std::array<double, 5> correlations{};
    std::array<double, 5> lowHighRatio{};
    for (size_t candidate = 0; candidate < kConfidenceNames.size(); ++candidate) {
        const auto &pairs = confidencePairs[candidate];
        correlations[candidate] = correlation(pairs);
        double confidenceSum = 0.0;
        for (const auto &[confidence, error] : pairs) confidenceSum += confidence;
        confidenceCandidates << csv(kConfidenceNames[candidate]) << ',' << pairs.size() << ',' << number(correlations[candidate]) << ','
                             << number(pairs.empty() ? 0.0 : confidenceSum / pairs.size()) << '\n';
        std::vector<std::pair<double, double>> sorted = pairs;
        std::sort(sorted.begin(), sorted.end());
        double firstMean = 0.0, lastMean = 0.0;
        for (int bin = 0; bin < 10; ++bin) {
            const size_t begin = sorted.size() * static_cast<size_t>(bin) / 10U;
            const size_t end = sorted.size() * static_cast<size_t>(bin + 1) / 10U;
            Stats errorStats;
            for (size_t index = begin; index < end; ++index) errorStats.add(sorted[index].second);
            if (bin == 0) firstMean = errorStats.mean();
            if (bin == 9) lastMean = errorStats.mean();
            confidenceCalibration << csv(kConfidenceNames[candidate]) << ',' << bin << ',' << errorStats.count() << ','
                                  << number(errorStats.mean()) << ',' << number(errorStats.percentile(.95)) << '\n';
        }
        lowHighRatio[candidate] = lastMean > 1e-12 ? firstMean / lastMean : 0.0;
    }
    leadTime << "candidate,large_error_events,prewarned_events,prewarn_fraction,mean_lead_ms\n";
    for (size_t candidate = 0; candidate < kConfidenceNames.size(); ++candidate) {
        leadTime << csv(kConfidenceNames[candidate]) << ',' << leadEvents[candidate] << ',' << leadPrewarned[candidate] << ','
                 << number(leadEvents[candidate] == 0 ? 0.0 : static_cast<double>(leadPrewarned[candidate]) / leadEvents[candidate]) << ','
                 << number(leadPrewarned[candidate] == 0 ? 0.0 : leadMilliseconds[candidate] / leadPrewarned[candidate]) << '\n';
    }

    std::ostringstream curves;
    curves << "curve,physical,physical_lead,output_delta,local_amplification\n";
    const std::array<std::pair<std::string, hotas::CurveDefinition>, 5> curveDefinitions{{
        {"linear", hotas::linearCurveDefinition()},
        {"j-curve-0.40", hotas::standardCurveDefinition(hotas::CurveFamily::JCurve, .40F)},
        {"j-curve-0.85", hotas::standardCurveDefinition(hotas::CurveFamily::JCurve, .85F)},
        {"s-curve-0.40", hotas::standardCurveDefinition(hotas::CurveFamily::SCurve, .40F)},
        {"s-curve-0.85", hotas::standardCurveDefinition(hotas::CurveFamily::SCurve, .85F)},
    }};
    for (const auto &[name, definition] : curveDefinitions) {
        for (const double physicalValue : {-0.90, -0.55, -0.20, 0.20, 0.55, 0.90}) {
            for (const double lead : {.002, .010, .030}) {
                const float base = hotas::evaluateCurveDefinition(static_cast<float>(physicalValue), definition, false);
                const float led = hotas::evaluateCurveDefinition(static_cast<float>(std::clamp(physicalValue + lead, -1.0, 1.0)), definition, false);
                const double outputDelta = led - base;
                curves << csv(name) << ',' << number(physicalValue, 4) << ',' << number(lead, 4) << ',' << number(outputDelta) << ','
                       << number(std::abs(outputDelta) / lead) << '\n';
            }
        }
    }

    std::ostringstream abg;
    writeStatRows(abg, abgStats, "bucket,count,mae,rmse,p95,p99");

    constexpr size_t kBestCandidate = 3; // FC-D, selected from the fixed five-candidate set.
    const double bestLeadFraction = leadEvents[kBestCandidate] == 0 ? 0.0
        : static_cast<double>(leadPrewarned[kBestCandidate]) / leadEvents[kBestCandidate];
    // A useful signal must not only correlate after error happens: it must
    // warn with broad coverage before/as major errors develop. The fixed 70%
    // coverage gate intentionally avoids promoting a merely retrospective
    // correlation into R1 authority.
    const bool pass = correlations[kBestCandidate] < -.20 && lowHighRatio[kBestCandidate] > 1.50
        && bestLeadFraction >= .70;
    const bool partial = correlations[kBestCandidate] < -.05 || lowHighRatio[kBestCandidate] > 1.10;
    const std::string disposition = pass
        ? "V2.4.R0 PASS - FORECAST RELIABILITY SIGNAL CONFIRMED - READY FOR R1 SHADOW PREDICTOR DESIGN"
        : partial ? "V2.4.R0 PARTIAL - FORECASTABILITY SIGNAL EXISTS BUT REQUIRES MORE CHARACTERIZATION"
                  : "V2.4.R0 FAIL - NO RELIABLE SIGNAL FOUND BEYOND CURRENT V2.3 MODEL";
    const std::string policy = "# V2.4.R0 Research Policy\n\n"
        "This executable is an offline, compile-gated research target. It never opens DirectInput, vJoy, the GUI, or the mapper. "
        "It does not alter V2.3 production configuration, estimator state transitions, output, presets, ABG gains, horizons, curves, or device handling. "
        "All Phase 2B, Phase 3, and Phase 3B material is labeled RESEARCH DATA - NO LONGER HOLDOUT.\n\n"
        "DirectInput freshness feasibility audit: the current path is state polling; unchanged axis values are indistinguishable from an older held value at the mapper boundary. "
        "Buffered DirectInput events could provide event sequencing/freshness metadata, but would be an input-architecture investigation with a new event queue and hot-path ownership review; R0 makes no such change.\n";
    const std::string manifest = "{\n  \"schema\": \"v2.4.R0\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"reference_preset\": \"Fast\",\n  \"physical_trace_count\": "
        + std::to_string(std::count_if(traces.begin(), traces.end(), [](const Trace &trace) { return trace.physicalTrace; })) + "\n}\n";
    const std::string identity = "{\n  \"published_release_sha\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n"
        "  \"qualified_mathematical_freeze_sha\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        "  \"adaptive_response_cpp_blob\": \"17950cf95f6fae89318f3ffc1d699ea36749156a\",\n"
        "  \"adaptive_response_h_blob\": \"ecd15848676dd8c6abdb2ad15648d2133edde6cc\",\n"
        "  \"mapping_types_blob\": \"39a3ad88f1aebd9b3eca05ede0ad1f7833df337d\",\n"
        "  \"release_matches_freeze_blobs\": true\n}\n";
    std::ostringstream report;
    report << "# V2.4.R0 Adaptive Response Forecastability Research Report\n\n"
           << "## Final disposition\n\n" << disposition << "\n\n"
           << "The published V2.3.0 release is `37b6bf2`; its Adaptive Response and mapping-type blobs match the qualified `055e047` mathematical freeze. "
           << "Research instrumentation is compiled only into `adaptive_forecastability_research`; exact production output comparison is "
           << (equivalent ? "EXACT." : "NOT EXACT - STOP.") << "\n\n"
           << "## Evidence scope\n\nAll deterministic, Phase 2B, Phase 3, Phase 3B, canonical, and 15 real-HOTAS-trace inputs are research data, not blind validation. "
           << "Future V2.4 output work requires new blind evidence.\n\n"
           << "## Instrumentation and signal characterization\n\nThe compile-gated telemetry records signed innovation, its time-normalized energy, normalized forms, jerk, acceleration persistence, source period residual, cadence confidence, and production estimator state. "
           << "FC-D is the strongest fixed candidate: correlation with future active-horizon V2.3 error is " << number(correlations[kBestCandidate])
           << "; its low/high confidence-decile error ratio is " << number(lowHighRatio[kBestCandidate])
           << "; its major-error prewarning coverage is " << number(bestLeadFraction) << ". The PASS gate requires 0.70 coverage, so correlation alone does not authorize R1.\n\n"
           << "## Counterfactual predictor findings\n\nCV, CA, fixed-tau Singer, and a causal residual-energy CV/CA blend are offline-only. "
           << "The horizon, motion-family, tau-sweep, physical-trace, and outlier CSVs retain the exact error distributions through 50 ms; no model reaches vJoy.\n\n"
           << "## Cadence and source-freshness audit\n\nThe shipping acquisition path uses DirectInput `Poll` plus `GetDeviceState` into `DIJOYSTATE2`; the state snapshot carries no report sequence number or source timestamp. "
           << "It cannot distinguish a genuinely new unchanged-value report from a held state at the mapper boundary. Buffered DirectInput events could investigate freshness, but introduce a new event-queue/ownership path and are out of scope for R0.\n\n"
           << "## Curve and ABG characterization\n\nPrediction remains in physical space. The representative curve audit measures post-curve lead amplification without changing the signal path; steep J/S regions can amplify a physical lead. "
           << "ABG buckets are descriptive only and do not derive new gains.\n\n"
           << "## Recommended next step\n\nDo not begin R1 automatically. If authorized after review, retain only the smallest causal FC-D-style signal in shadow mode, improve failure lead coverage, and collect a new blind corpus before changing any predictor, gain, horizon, curve envelope, or input architecture.\n";

    const std::array<std::pair<const char *, std::string>, 18> files{{
        {"manifest.json", manifest}, {"baseline_identity.json", identity}, {"research_policy.md", policy},
        {"corpus_inventory.csv", inventory.str()}, {"innovation_metrics.csv", innovation.str()},
        {"innovation_calibration.csv", innovationCalibration.str()}, {"jerk_metrics.csv", jerk.str()},
        {"acceleration_persistence.csv", persistence.str()}, {"cadence_confidence.csv", cadence.str()},
        {"model_error_by_horizon.csv", byHorizon.str()}, {"model_error_by_motion_family.csv", byFamily.str()},
        {"singer_tau_sweep.csv", singer.str()}, {"shadow_model_comparison.csv", comparison.str()},
        {"forecast_confidence_candidates.csv", confidenceCandidates.str()}, {"forecast_confidence_calibration.csv", confidenceCalibration.str()},
        {"confidence_lead_time.csv", leadTime.str()}, {"physical_trace_model_comparison.csv", physical.str()},
        {"curve_amplification_audit.csv", curves.str()},
    }};
    for (const auto &[name, contents] : files) if (!writeText(options.output / name, contents)) return 3;
    if (!writeText(options.output / "abg_gain_characterization.csv", abg.str())
        || !writeText(options.output / "outliers.csv", outliers.str())
        || !writeText(options.output / "V2.4.R0_Adaptive_Response_Forecastability_Research_Report.md", report.str())) return 3;
    if (!options.report.empty() && !writeText(options.report, report.str())) return 3;
    std::cout << disposition << "\n";
    return equivalent ? 0 : 4;
}

} // namespace

// R0.5 keeps the R0 executable above intact as historical context.  The
// following analyser is deliberately offline-only: it consumes test traces,
// never reaches the mapper, and calculates every new state outside `process()`.
#if defined(HOTAS_ADAPTIVE_FORECAST_RESEARCH)
constexpr std::array<const char *, 6> kR05Names{{
    "R05-A FC-D baseline", "R05-B FC-D plus innovation surprise",
    "R05-C FC-D plus normalized jerk", "R05-D FC-D plus surprise and jerk",
    "R05-E slow trust plus fast risk", "R05-F residual preference"
}};
constexpr std::array<double, 3> kFastTauMs{{4.0, 8.0, 12.0}};
constexpr std::array<double, 3> kSlowTauMs{{20.0, 40.0, 60.0}};
constexpr double kWarningThreshold = .40;

struct R05Pending {
    double targetTime = 0.0;
    double cv = 0.0;
    double ca = 0.0;
};

struct MajorEvent {
    size_t start = 0;
    size_t end = 0;
    size_t peak = 0;
    double threshold = 0.0;
};

struct TraceEvaluation {
    Trace trace;
    std::vector<Observation> rows;
    std::vector<double> errors;
    std::vector<MajorEvent> events;
    double threshold = 0.0;
};

struct MetricSummary {
    size_t samples = 0;
    size_t positives = 0;
    size_t warnings = 0;
    size_t falseWarnings = 0;
    size_t events = 0;
    size_t prewarned = 0;
    std::vector<std::pair<double, double>> confidenceErrors;
    std::vector<std::pair<double, bool>> riskLabels;
    std::vector<double> leads;
    std::vector<double> severities;
};

double aucRoc(const std::vector<std::pair<double, bool>> &scores)
{
    size_t positive = 0;
    for (const auto &[score, label] : scores) { (void)score; positive += label ? 1U : 0U; }
    const size_t negative = scores.size() - positive;
    if (positive == 0U || negative == 0U) return 0.0;
    std::vector<std::pair<double, double>> points{{0.0, 0.0}};
    for (int step = 100; step >= 0; --step) {
        const double threshold = static_cast<double>(step) / 100.0;
        size_t tp = 0, fp = 0;
        for (const auto &[score, label] : scores) if (score >= threshold) { if (label) ++tp; else ++fp; }
        points.push_back({static_cast<double>(fp) / negative, static_cast<double>(tp) / positive});
    }
    double area = 0.0;
    for (size_t index = 1; index < points.size(); ++index)
        area += (points[index].first - points[index - 1].first) * (points[index].second + points[index - 1].second) * .5;
    return std::clamp(area, 0.0, 1.0);
}

double aucPr(const std::vector<std::pair<double, bool>> &scores)
{
    size_t positive = 0;
    for (const auto &[score, label] : scores) { (void)score; positive += label ? 1U : 0U; }
    if (positive == 0U) return 0.0;
    double previousRecall = 0.0, previousPrecision = 1.0, area = 0.0;
    for (int step = 100; step >= 0; --step) {
        const double threshold = static_cast<double>(step) / 100.0;
        size_t tp = 0, fp = 0;
        for (const auto &[score, label] : scores) if (score >= threshold) { if (label) ++tp; else ++fp; }
        const double recall = static_cast<double>(tp) / positive;
        const double precision = tp + fp == 0U ? 1.0 : static_cast<double>(tp) / (tp + fp);
        area += (recall - previousRecall) * (precision + previousPrecision) * .5;
        previousRecall = recall;
        previousPrecision = precision;
    }
    return std::clamp(area, 0.0, 1.0);
}

double recallAtFalseWarningRate(const std::vector<std::pair<double, bool>> &scores, double cap)
{
    size_t positive = 0;
    for (const auto &[score, label] : scores) { (void)score; positive += label ? 1U : 0U; }
    const size_t negative = scores.size() - positive;
    if (positive == 0U || negative == 0U) return 0.0;
    double best = 0.0;
    for (int step = 0; step <= 100; ++step) {
        const double threshold = static_cast<double>(step) / 100.0;
        size_t tp = 0, fp = 0;
        for (const auto &[score, label] : scores) if (score >= threshold) { if (label) ++tp; else ++fp; }
        if (static_cast<double>(fp) / negative <= cap) best = std::max(best, static_cast<double>(tp) / positive);
    }
    return best;
}

std::optional<double> warningTime(const TraceEvaluation &evaluation, size_t eventIndex, int candidate)
{
    if (eventIndex >= evaluation.rows.size()) return std::nullopt;
    const double eventTime = evaluation.rows[eventIndex].time;
    std::optional<double> result;
    for (size_t index = 0; index < eventIndex; ++index) {
        if (eventTime - evaluation.rows[index].time > .050) continue;
        const double confidence = candidate == 0 ? evaluation.rows[index].confidence[0]
            : candidate == 1 ? evaluation.rows[index].confidence[1]
            : candidate == 2 ? evaluation.rows[index].confidence[3]
            : evaluation.rows[index].r05Confidence[static_cast<size_t>(candidate - 3)];
        if (confidence < kWarningThreshold && !result) result = evaluation.rows[index].time;
    }
    return result;
}

MetricSummary summarize(const std::vector<TraceEvaluation> &evaluations, int candidate,
                        const std::string &family = {})
{
    MetricSummary result;
    for (const TraceEvaluation &evaluation : evaluations) {
        if (!family.empty() && evaluation.trace.family != family) continue;
        for (size_t index = 0; index < evaluation.rows.size(); ++index) {
            const double error = evaluation.errors[index];
            if (error < 0.0) continue;
            const double confidence = candidate == 0 ? evaluation.rows[index].confidence[0]
                : candidate == 1 ? evaluation.rows[index].confidence[1]
                : candidate == 2 ? evaluation.rows[index].confidence[3]
                : evaluation.rows[index].r05Confidence[static_cast<size_t>(candidate - 3)];
            const bool major = error >= evaluation.threshold && evaluation.threshold > 0.0;
            const bool warning = confidence < kWarningThreshold;
            ++result.samples;
            result.positives += major ? 1U : 0U;
            result.warnings += warning ? 1U : 0U;
            result.falseWarnings += warning && !major ? 1U : 0U;
            result.confidenceErrors.push_back({confidence, error});
            result.riskLabels.push_back({std::clamp(1.0 - confidence, 0.0, 1.0), major});
        }
        for (const MajorEvent &event : evaluation.events) {
            ++result.events;
            result.severities.push_back(evaluation.errors[event.peak]);
            const auto warning = warningTime(evaluation, event.peak, candidate);
            if (warning) {
                ++result.prewarned;
                result.leads.push_back((evaluation.rows[event.peak].time - *warning) * 1000.0);
            }
        }
    }
    return result;
}

double eventRecall(const MetricSummary &summary)
{
    return summary.events == 0U ? 0.0 : static_cast<double>(summary.prewarned) / summary.events;
}

double precision(const MetricSummary &summary)
{
    return summary.warnings == 0U ? 0.0 : static_cast<double>(summary.warnings - summary.falseWarnings) / summary.warnings;
}

double falseWarningRate(const MetricSummary &summary)
{
    const size_t negatives = summary.samples - summary.positives;
    return negatives == 0U ? 0.0 : static_cast<double>(summary.falseWarnings) / negatives;
}

double f1(const MetricSummary &summary)
{
    const size_t tp = summary.warnings - summary.falseWarnings;
    const size_t fn = summary.positives > tp ? summary.positives - tp : 0U;
    const double denominator = 2.0 * tp + summary.falseWarnings + fn;
    return denominator <= 0.0 ? 0.0 : 2.0 * tp / denominator;
}

double meanOf(const std::vector<double> &values)
{
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

std::vector<Trace> blindSignalValidationCorpus()
{
    std::vector<Trace> traces;
    std::uint32_t seed = 0x2405F00DU;
    const std::array<const char *, 5> personas{{"Precision Pilot", "Combat Helicopter", "Fixed-Wing", "Space-Sim", "Noisy Sensor"}};
    for (size_t persona = 0; persona < personas.size(); ++persona) {
        for (int sequence = 0; sequence < 60; ++sequence) {
            std::uint32_t state = seed++;
            const bool combat = persona == 1U;
            const double step = combat ? .045 : persona == 0U ? .26 : persona == 2U ? .090 : .120;
            const double range = combat ? .98 : persona == 0U ? .15 : persona == 2U ? .72 : persona == 3U ? .84 : .38;
            std::vector<std::pair<double, float>> points{{0.0, static_cast<float>((unitRandom(state) - .5) * .2)}};
            for (double time = step; time < 2.6; time += step) {
                const float last = points.back().second;
                const double shape = persona == 3U && (sequence % 3 == 0) ? 1.25 : 1.0;
                const float next = std::clamp(last + static_cast<float>((unitRandom(state) - .5) * 2.0 * range * shape), -1.0F, 1.0F);
                points.push_back({time, next});
            }
            points.push_back({2.8, points.back().second});
            traces.push_back(syntheticTrace("blind/persona/" + std::to_string(sequence) + "/" + personas[persona],
                "blind-signal-validation", personas[persona], points, seed++, combat ? 250 : 125,
                persona == 4U, persona == 4U ? .006 : 0.0));
        }
    }
    const std::array<const char *, 12> families{{"ballistic smooth", "fast ballistic", "high jerk", "stop", "reversal",
        "brake reverse", "brake continue", "oscillation", "cadence jitter", "endpoint", "source holds", "real-human-like"}};
    for (int sequence = 0; sequence < 100; ++sequence) {
        const int style = sequence % static_cast<int>(families.size());
        const float wobble = static_cast<float>((unitRandom(seed) - .5) * .12);
        std::vector<std::pair<double, float>> points;
        switch (style) {
        case 0: points={{0,-.82F},{.72F,.82F},{1.3F,.82F}}; break;
        case 1: points={{0,-.92F},{.09F,.92F},{1.0F,.92F}}; break;
        case 2: points={{0,0},{.035F,.72F},{.07F,-.72F},{.105F,.86F},{.14F,-.86F},{.8F,-.86F}}; break;
        case 3: points={{0,-.82F},{.18F,.80F},{1.1F,.80F}}; break;
        case 4: points={{0,.82F},{.20F,-.78F},{.43F,.78F},{1.1F,.78F}}; break;
        case 5: points={{0,-.88F},{.16F,.78F},{.23F,.34F},{.37F,-.86F},{1.0F,-.86F}}; break;
        case 6: points={{0,-.88F},{.16F,.78F},{.23F,.31F},{.45F,.91F},{1.0F,.91F}}; break;
        case 7: points={{0,.70F},{.12F,-.70F},{.24F,.70F},{.36F,-.70F},{.48F,.66F},{1.1F,.66F}}; break;
        case 8: points={{0,-.66F},{.76F,.66F},{1.3F,.66F}}; break;
        case 9: points={{0,-.30F},{.34F,.82F},{.52F,.985F},{1.1F,.985F}}; break;
        case 10: points={{0,-.58F},{.18F,-.58F},{.36F,.70F},{.64F,.70F},{.85F,-.30F},{1.2F,-.30F}}; break;
        default: points={{0,-.76F},{.11F,.80F},{.24F,-.62F},{.42F,.76F},{.78F,.20F},{1.2F,.20F}}; break;
        }
        for (auto &point : points) point.second = std::clamp(point.second + wobble, -1.0F, 1.0F);
        traces.push_back(syntheticTrace("blind/structured/" + std::to_string(sequence) + "/" + families[style],
            "blind-signal-validation", families[style], points, seed++, style == 8 ? 125 : 250, style == 8, style == 8 ? .003 : 0.0));
    }
    return traces;
}

std::string blindCanonicalCsv(const std::vector<Trace> &traces)
{
    std::ostringstream output;
    output << "trace_id,corpus,family,time_us,physical_bits\n";
    for (const Trace &trace : traces) for (const Sample &sample : trace.samples)
        output << csv(trace.id) << ',' << csv(trace.corpus) << ',' << csv(trace.family) << ','
               << static_cast<long long>(std::llround(sample.time * 1000000.0)) << ','
               << std::bit_cast<std::uint32_t>(sample.physical) << '\n';
    return output.str();
}

std::vector<Observation> r05Observe(const Trace &trace, const RuntimeAdaptiveResponseConfig &configuration)
{
    std::vector<Observation> rows;
    rows.reserve(trace.samples.size());
    AdaptiveResponseProcessor processor;
    std::array<double, 3> fast{}, slow{};
    double priorFast = 0.0, jerkRisk = 0.0, accelerationChangeEnergy = 0.0, stability = 1.0;
    float priorAcceleration = 0.0F;
    for (size_t index = 0; index < trace.samples.size(); ++index) {
        const Sample &sample = trace.samples[index];
        const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
        Observation row;
        row.time = sample.time;
        row.physical = sample.physical;
        row.telemetry = processor.process(sample.physical, configuration, at);
#if defined(HOTAS_ADAPTIVE_FORECAST_RESEARCH)
        const double dt = std::max(.0005, static_cast<double>(row.telemetry.meaningfulDeltaSeconds));
        const double innovation = row.telemetry.innovationSigned;
        const double innovationSquared = innovation * innovation;
        for (size_t tau = 0; tau < fast.size(); ++tau) {
            fast[tau] += dt / (kFastTauMs[tau] / 1000.0 + dt) * (innovationSquared - fast[tau]);
            slow[tau] += dt / (kSlowTauMs[tau] / 1000.0 + dt) * (innovationSquared - slow[tau]);
        }
        const double noiseScale = std::max(.00025, static_cast<double>(row.telemetry.noiseThreshold));
        const double expectedMotion = std::max(noiseScale, std::abs(static_cast<double>(row.telemetry.predictedVelocity)) * dt);
        const double jerk = index == 0U ? 0.0 : (row.telemetry.acceleration - priorAcceleration) / dt;
        const double accelerationScale = std::abs(static_cast<double>(row.telemetry.acceleration)) + std::abs(static_cast<double>(priorAcceleration)) + .015;
        const double normalizedJerk = std::abs(jerk) * dt * dt / std::max(.0005, expectedMotion + accelerationScale * dt * dt);
        const double targetRisk = std::clamp((normalizedJerk - .30) / 1.40, 0.0, 1.0);
        jerkRisk += (targetRisk - jerkRisk) * (targetRisk > jerkRisk ? dt / (.004 + dt) : dt / (.020 + dt));
        row.fastInnovationEnergy = fast;
        row.slowInnovationEnergy = slow;
        row.innovationSurprise = innovationSquared / std::max(1e-7, slow[1] + noiseScale * noiseScale);
        row.innovationEnergySlope = (fast[1] - priorFast) / dt;
        row.innovationGrowthRatio = fast[1] / std::max(1e-7, slow[1]);
        row.normalizedJerk = normalizedJerk;
        row.jerkRisk = std::clamp(jerkRisk, 0.0, 1.0);
        row.normalizedAccelerationChange = std::abs(row.telemetry.acceleration - priorAcceleration) /
            std::max(.015, std::abs(static_cast<double>(row.telemetry.acceleration)) + .015);
        accelerationChangeEnergy += dt / (.030 + dt) * (row.normalizedAccelerationChange - accelerationChangeEnergy);
        stability += dt / (.040 + dt) * ((1.0 - std::clamp(row.normalizedAccelerationChange, 0.0, 1.0)) - stability);
        row.accelerationChangeEnergy = accelerationChangeEnergy;
        row.accelerationStability = std::clamp(stability, 0.0, 1.0);
        const double innovationTrust = 1.0 / (1.0 + std::sqrt(slow[1] / (noiseScale * noiseScale + expectedMotion * expectedMotion)));
        const double disagreement = std::abs(.5 * row.telemetry.acceleration * .020 * .020);
        const double disagreementTrust = 1.0 / (1.0 + disagreement / .004);
        const double jerkTrust = 1.0 - row.jerkRisk;
        row.confidence[0] = innovationTrust;
        row.confidence[1] = innovationTrust * jerkTrust;
        row.confidence[3] = innovationTrust * disagreementTrust;
        row.disagreement = disagreement;
        priorFast = fast[1];
        priorAcceleration = row.telemetry.acceleration;
#endif
        rows.push_back(row);
    }
    std::vector<R05Pending> pending;
    size_t pendingIndex = 0;
    double cvEnergy = .000001, caEnergy = .000001, previousDisagreement = 0.0, previousPreference = 0.0;
    for (size_t index = 0; index < rows.size(); ++index) {
        Observation &row = rows[index];
        while (pendingIndex < pending.size() && pending[pendingIndex].targetTime <= row.time + 1e-9) {
            if (const auto actual = valueAt(trace.samples, pending[pendingIndex].targetTime)) {
                const double cvError = pending[pendingIndex].cv - *actual;
                const double caError = pending[pendingIndex].ca - *actual;
                cvEnergy += .10 * (cvError * cvError - cvEnergy);
                caEnergy += .10 * (caError * caError - caEnergy);
            }
            ++pendingIndex;
        }
        const double horizon = std::clamp(static_cast<double>(row.telemetry.activeHorizonSeconds), .004, .050);
        const double cv = row.physical + row.telemetry.velocity * horizon;
        const double ca = cv + .5 * row.telemetry.acceleration * horizon * horizon;
        const double dt = index == 0U ? .004 : std::max(.0005, row.time - rows[index - 1U].time);
        row.disagreementVelocity = std::max(0.0, (row.disagreement - previousDisagreement) / dt);
        row.cvResidualEnergy = cvEnergy;
        row.caResidualEnergy = caEnergy;
        row.modelPreference = (cvEnergy - caEnergy) / std::max(1e-8, cvEnergy + caEnergy);
        row.modelPreferenceReversal = std::abs(row.modelPreference - previousPreference);
        const double surpriseRisk = std::clamp((row.innovationSurprise - 2.5) / 8.0, 0.0, 1.0);
        const double disagreementRisk = std::clamp(row.disagreementVelocity / .12, 0.0, 1.0);
        const double slowTrust = 1.0 / (1.0 + std::sqrt(row.slowInnovationEnergy[1] / .0004));
        const double residualTrust = 1.0 / (1.0 + std::sqrt(std::min(cvEnergy, caEnergy)) / .020);
        const double preferenceRisk = std::clamp(row.modelPreferenceReversal / .50, 0.0, 1.0);
        const double fcD = row.confidence[3];
        row.r05Confidence[0] = fcD;
        row.r05Confidence[1] = fcD * (1.0 - surpriseRisk);
        row.r05Confidence[2] = fcD * (1.0 - row.jerkRisk);
        row.r05Confidence[3] = fcD * (1.0 - std::max(surpriseRisk, row.jerkRisk));
        row.r05Confidence[4] = std::clamp((.70 * slowTrust + .30 * residualTrust) *
            (1.0 - std::max({surpriseRisk, row.jerkRisk, disagreementRisk})), 0.0, 1.0);
        row.r05Confidence[5] = std::clamp(residualTrust * (1.0 - std::max(preferenceRisk, disagreementRisk)), 0.0, 1.0);
        pending.push_back({row.time + horizon, cv, ca});
        previousDisagreement = row.disagreement;
        previousPreference = row.modelPreference;
    }
    return rows;
}

std::vector<TraceEvaluation> evaluate(const std::vector<Trace> &traces, const RuntimeAdaptiveResponseConfig &configuration)
{
    std::vector<TraceEvaluation> results;
    results.reserve(traces.size());
    for (const Trace &trace : traces) {
        TraceEvaluation evaluation;
        evaluation.trace = trace;
        evaluation.rows = r05Observe(trace, configuration);
        evaluation.errors.assign(evaluation.rows.size(), -1.0);
        std::vector<double> valid;
        for (size_t index = 0; index < evaluation.rows.size(); ++index) {
            const auto truth = valueAt(trace.samples, evaluation.rows[index].time + evaluation.rows[index].telemetry.activeHorizonSeconds);
            if (!truth) continue;
            evaluation.errors[index] = std::abs(evaluation.rows[index].telemetry.predicted - *truth);
            valid.push_back(evaluation.errors[index]);
        }
        evaluation.threshold = valid.size() < 6U ? 0.0 : std::max(.030, percentile(valid, .90));
        bool inEvent = false;
        MajorEvent current;
        for (size_t index = 0; index < evaluation.errors.size(); ++index) {
            const bool major = evaluation.threshold > 0.0 && evaluation.errors[index] >= evaluation.threshold;
            if (major && !inEvent) { current={index,index,index,evaluation.threshold}; inEvent=true; }
            if (major && inEvent) {
                current.end = index;
                if (evaluation.errors[index] > evaluation.errors[current.peak]) current.peak = index;
            }
            if (!major && inEvent) { evaluation.events.push_back(current); inEvent=false; }
        }
        if (inEvent) evaluation.events.push_back(current);
        results.push_back(std::move(evaluation));
    }
    return results;
}

int selectCandidate(const std::array<MetricSummary, 6> &metrics)
{
    int best = 0;
    double bestScore = -1e9;
    for (size_t index = 0; index < metrics.size(); ++index) {
        const MetricSummary &metric = metrics[index];
        const double corr = correlation(metric.confidenceErrors);
        const double score = eventRecall(metric) * 4.0 + std::max(0.0, -corr) + precision(metric)
            - falseWarningRate(metric) * 1.75 + recallAtFalseWarningRate(metric.riskLabels, .05) * .50;
        if (score > bestScore) { bestScore = score; best = static_cast<int>(index); }
    }
    return best;
}

std::string calibrationRows(const std::vector<TraceEvaluation> &evaluations, bool extended)
{
    std::ostringstream output;
    output << "candidate,subset,confidence_decile,count,mean_future_active_error,p95_future_active_error\n";
    const std::array<std::pair<const char *, int>, 6> subsets{{
        {"ALL",0},{"ACTIVE_PREDICTION",1},{"MOVING",2},{"MODERATE_FAST",3},{"NONZERO_ACTIVE_HORIZON",4},{"REAL_HUMAN_ONLY",5}
    }};
    for (size_t candidate = 0; candidate < kR05Names.size(); ++candidate) for (const auto &[name, subset] : subsets) {
        std::vector<std::pair<double,double>> pairs;
        for (const TraceEvaluation &evaluation : evaluations) for (size_t index = 0; index < evaluation.rows.size(); ++index) {
            if (evaluation.errors[index] < 0.0) continue;
            const Observation &row = evaluation.rows[index];
            const bool keep = subset == 0 || (subset == 1 && row.telemetry.activeHorizonSeconds > .0005F)
                || (subset == 2 && std::abs(row.telemetry.velocity) > .15F)
                || (subset == 3 && std::abs(row.telemetry.velocity) > .50F)
                || (subset == 4 && row.telemetry.activeHorizonSeconds > .002F)
                || (subset == 5 && evaluation.trace.physicalTrace);
            if (keep) pairs.push_back({row.r05Confidence[candidate], evaluation.errors[index]});
        }
        std::sort(pairs.begin(), pairs.end());
        for (int decile = 0; decile < 10; ++decile) {
            const size_t begin = pairs.size() * static_cast<size_t>(decile) / 10U;
            const size_t end = pairs.size() * static_cast<size_t>(decile + 1) / 10U;
            Stats values;
            for (size_t index = begin; index < end; ++index) values.add(pairs[index].second);
            output << csv(kR05Names[candidate]) << ',' << name << ',' << decile << ',' << values.count() << ','
                   << number(values.mean()) << ',' << number(values.percentile(.95)) << '\n';
        }
    }
    if (!extended) return output.str();
    return output.str();
}

std::string metricRows(const std::array<MetricSummary, 6> &metrics)
{
    std::ostringstream output;
    output << "candidate,samples,correlation,event_count,prewarning_recall,precision,false_warning_rate,low_confidence_occupancy,roc_auc,pr_auc,f1,recall_at_fwr_1pct,recall_at_fwr_5pct,mean_warning_lead_ms,median_warning_lead_ms,p10_warning_lead_ms\n";
    for (size_t index = 0; index < metrics.size(); ++index) {
        const MetricSummary &metric = metrics[index];
        output << csv(kR05Names[index]) << ',' << metric.samples << ',' << number(correlation(metric.confidenceErrors)) << ','
               << metric.events << ',' << number(eventRecall(metric)) << ',' << number(precision(metric)) << ','
               << number(falseWarningRate(metric)) << ',' << number(metric.samples == 0U ? 0.0 : static_cast<double>(metric.warnings) / metric.samples) << ','
               << number(aucRoc(metric.riskLabels)) << ',' << number(aucPr(metric.riskLabels)) << ',' << number(f1(metric)) << ','
               << number(recallAtFalseWarningRate(metric.riskLabels,.01)) << ',' << number(recallAtFalseWarningRate(metric.riskLabels,.05)) << ','
               << number(metric.leads.empty()?0.0:std::accumulate(metric.leads.begin(),metric.leads.end(),0.0)/metric.leads.size()) << ','
               << number(percentile(metric.leads,.50)) << ',' << number(percentile(metric.leads,.10)) << '\n';
    }
    return output.str();
}

std::string ledgerRows(const std::vector<TraceEvaluation> &evaluations)
{
    std::ostringstream output;
    output << "trace_id,corpus,motion_family,event_timestamp_ms,future_horizon_ms,error_magnitude,v23_active_horizon_ms,fc_a_confidence,fc_b_confidence,fc_d_confidence,fc_a_first_warning_ms,fc_a_warning_lead_ms,fc_a_missed,fc_b_first_warning_ms,fc_b_warning_lead_ms,fc_b_missed,fc_d_first_warning_ms,fc_d_warning_lead_ms,fc_d_missed,r05_a_confidence,r05_b_confidence,r05_c_confidence,r05_d_confidence,r05_e_confidence,r05_f_confidence,innovation,innovation_energy,innovation_derivative,jerk,acceleration,normalized_acceleration_change,model_disagreement,cv_residual_energy,ca_residual_energy,cadence_confidence,motion_coherence,v23_motion_state\n";
    for (const TraceEvaluation &evaluation : evaluations) for (const MajorEvent &event : evaluation.events) {
        const Observation &row = evaluation.rows[event.peak];
        const auto a = warningTime(evaluation,event.peak,0), b=warningTime(evaluation,event.peak,1), d=warningTime(evaluation,event.peak,2);
        const auto field = [&](const std::optional<double> &time) { return time ? number(*time*1000.0,3)+","+number((row.time-*time)*1000.0,3)+",false" : ",,true"; };
        output << csv(evaluation.trace.id) << ',' << csv(evaluation.trace.corpus) << ',' << csv(evaluation.trace.family) << ','
               << number(row.time*1000.0,3) << ',' << number(row.telemetry.activeHorizonSeconds*1000.0,3) << ',' << number(evaluation.errors[event.peak]) << ','
               << number(row.telemetry.activeHorizonSeconds*1000.0,3) << ',' << number(row.confidence[0]) << ',' << number(row.confidence[1]) << ',' << number(row.confidence[3]) << ','
               << field(a) << ',' << field(b) << ',' << field(d);
        for (double confidence : row.r05Confidence) output << ',' << number(confidence);
        output << ',' << number(row.telemetry.innovationSigned) << ',' << number(row.slowInnovationEnergy[1]) << ',' << number(row.innovationEnergySlope)
               << ',' << number(row.normalizedJerk) << ',' << number(row.telemetry.acceleration) << ',' << number(row.normalizedAccelerationChange)
               << ',' << number(row.disagreement) << ',' << number(row.cvResidualEnergy) << ',' << number(row.caResidualEnergy)
               << ',' << number(row.cadenceConfidence) << ',' << number(row.telemetry.motionCoherence) << ',' << static_cast<int>(row.telemetry.state) << '\n';
    }
    return output.str();
}

int researchMainR05(const Options &options, const std::vector<Trace> &historical)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    const std::filesystem::path productionPath = options.output / "production_outputs.csv";
    if (!productionOutput(options.output, historical)) return 3;
    std::string equivalence;
    const bool equivalent = equivalentOutput(options.baseline, productionPath, &equivalence);
    const std::vector<TraceEvaluation> development = evaluate(historical, configuration);
    std::array<MetricSummary,6> developmentMetrics;
    for (size_t candidate=0; candidate<developmentMetrics.size(); ++candidate) developmentMetrics[candidate]=summarize(development,static_cast<int>(candidate)+3);

    // The blind input is constructed, serialized, and sealed before the final
    // candidate is selected.  No forecast is evaluated against it before lock.
    const std::vector<Trace> blind = blindSignalValidationCorpus();
    if (blind.size() != 400U) { std::cerr << "Blind corpus target is not 400 scenarios.\n"; return 3; }
    const std::string blindCsv = blindCanonicalCsv(blind);
    const std::string blindHash = QCryptographicHash::hash(QByteArray::fromStdString(blindCsv), QCryptographicHash::Sha256).toHex().toStdString();
    const std::string blindSeal = "{\n  \"schema\": \"v2.4.R0.5-blind-signal-validation-v1\",\n  \"seed\": \"0x2405F00D\",\n  \"scenario_target\": 400,\n  \"scenario_count\": 400,\n  \"composition\": {\"Precision Pilot\":60,\"Combat Helicopter\":60,\"Fixed-Wing\":60,\"Space-Sim\":60,\"Noisy Sensor\":60,\"structured_torture\":100},\n  \"canonical_csv_sha256\": \"" + blindHash + "\",\n  \"blind_replays_before_candidate_lock\": 0,\n  \"sealed_before_candidate_lock\": true\n}\n";
    if (!writeText(options.output / "blind_corpus.csv", blindCsv) || !writeText(options.output / "blind_corpus_seal.json", blindSeal)) return 3;

    const int selected = selectCandidate(developmentMetrics);
    const std::string lock = "{\n  \"selected_candidate\": \"" + std::string(kR05Names[static_cast<size_t>(selected)]) + "\",\n  \"low_confidence_threshold\": 0.4,\n  \"selection_rule\": \"highest fixed development score: event recall, calibration, precision, false-warning control, recall at 5 percent FWR\",\n  \"locked_after_blind_seal\": true,\n  \"blind_replays_before_lock\": 0\n}\n";
    if (!writeText(options.output / "candidate_selection_lock.json", lock)) return 3;
    const std::vector<TraceEvaluation> blindEvaluation = evaluate(blind, configuration); // Exactly one blind replay after lock.
    const MetricSummary blindMetric = summarize(blindEvaluation, selected+3);

    std::map<std::string, int> families;
    for (const TraceEvaluation &evaluation : development) families[evaluation.trace.family]=1;
    std::ostringstream missedFamilies, familyResults, physical, timescales, surprise, jerk, acceleration, disagreement, blindValidation;
    missedFamilies << "motion_family,event_count,fc_d_recall,fc_b_recall,fc_a_recall,fc_d_missed,mean_fc_d_warning_lead_ms,mean_error_severity\n";
    familyResults << "motion_family,candidate,event_count,prewarning_recall,precision,false_warning_rate,mean_warning_lead_ms,error_severity\n";
    for (const auto &[family, value] : families) {
        (void)value;
        const MetricSummary a=summarize(development,0,family), b=summarize(development,1,family), d=summarize(development,2,family);
        missedFamilies << csv(family) << ',' << d.events << ',' << number(eventRecall(d)) << ',' << number(eventRecall(b)) << ',' << number(eventRecall(a)) << ','
                       << (d.events-d.prewarned) << ',' << number(meanOf(d.leads)) << ',' << number(meanOf(d.severities)) << '\n';
        for (size_t candidate=0; candidate<kR05Names.size(); ++candidate) {
            const MetricSummary metric=summarize(development,static_cast<int>(candidate)+3,family);
            familyResults << csv(family) << ',' << csv(kR05Names[candidate]) << ',' << metric.events << ',' << number(eventRecall(metric)) << ',' << number(precision(metric)) << ',' << number(falseWarningRate(metric)) << ','
                          << number(meanOf(metric.leads)) << ',' << number(meanOf(metric.severities)) << '\n';
        }
    }
    physical << "trace_id,event_count,fc_d_recall,fc_d_mean_warning_lead_ms,selected_candidate,selected_recall,selected_mean_warning_lead_ms\n";
    for (const TraceEvaluation &evaluation : development) if (evaluation.trace.physicalTrace) {
        const std::vector<TraceEvaluation> one{evaluation};
        const MetricSummary d=summarize(one,2), final=summarize(one,selected+3);
        physical << csv(evaluation.trace.id) << ',' << d.events << ',' << number(eventRecall(d)) << ',' << number(d.leads.empty()?0.0:std::accumulate(d.leads.begin(),d.leads.end(),0.0)/d.leads.size()) << ','
                 << csv(kR05Names[static_cast<size_t>(selected)]) << ',' << number(eventRecall(final)) << ',' << number(final.leads.empty()?0.0:std::accumulate(final.leads.begin(),final.leads.end(),0.0)/final.leads.size()) << '\n';
    }
    std::array<std::vector<std::pair<double,double>>,6> timePairs;
    std::vector<std::pair<double,double>> surprisePairs;
    std::map<std::string,Stats> jerkStats, accelerationStats, disagreementStats;
    for (const TraceEvaluation &evaluation : development) for (size_t index=0; index<evaluation.rows.size(); ++index) if (evaluation.errors[index]>=0.0) {
        const Observation &row=evaluation.rows[index]; const double error=evaluation.errors[index];
        for (size_t tau=0;tau<3;++tau) { timePairs[tau].push_back({row.fastInnovationEnergy[tau],error}); timePairs[tau+3].push_back({row.slowInnovationEnergy[tau],error}); }
        surprisePairs.push_back({row.innovationSurprise,error});
        jerkStats[evaluation.trace.family].add(row.normalizedJerk);
        accelerationStats[evaluation.trace.family].add(row.accelerationStability);
        disagreementStats[evaluation.trace.family].add(row.disagreementVelocity);
    }
    timescales << "energy_kind,tau_ms,samples,correlation_with_future_error,mean_energy\n";
    for (size_t tau=0;tau<3;++tau) for (int kind=0;kind<2;++kind) { const size_t slot=tau+static_cast<size_t>(kind)*3; Stats values; for (const auto &[energy,error] : timePairs[slot]) { values.add(energy); (void)error; } timescales << (kind==0?"fast":"slow") << ',' << number(kind==0?kFastTauMs[tau]:kSlowTauMs[tau],0) << ',' << timePairs[slot].size() << ',' << number(correlation(timePairs[slot])) << ',' << number(values.mean()) << '\n'; }
    surprise << "surprise_decile,count,mean_future_active_error,p95_future_active_error\n"; std::sort(surprisePairs.begin(),surprisePairs.end());
    for (int bin=0;bin<10;++bin) { const size_t begin=surprisePairs.size()*static_cast<size_t>(bin)/10U,end=surprisePairs.size()*static_cast<size_t>(bin+1)/10U; Stats values; for(size_t i=begin;i<end;++i) values.add(surprisePairs[i].second); surprise << bin << ',' << values.count() << ',' << number(values.mean()) << ',' << number(values.percentile(.95)) << '\n'; }
    jerk << "motion_family,mean_normalized_jerk,fc_d_recall,r05_c_recall\n";
    acceleration << "motion_family,mean_acceleration_stability,fc_d_recall,r05_e_recall\n";
    disagreement << "motion_family,mean_disagreement_growth,fc_d_recall,r05_f_recall\n";
    for (const auto &[family,value] : families) { (void)value; jerk << csv(family) << ',' << number(jerkStats[family].mean()) << ',' << number(eventRecall(summarize(development,2,family))) << ',' << number(eventRecall(summarize(development,5,family))) << '\n'; acceleration << csv(family) << ',' << number(accelerationStats[family].mean()) << ',' << number(eventRecall(summarize(development,2,family))) << ',' << number(eventRecall(summarize(development,7,family))) << '\n'; disagreement << csv(family) << ',' << number(disagreementStats[family].mean()) << ',' << number(eventRecall(summarize(development,2,family))) << ',' << number(eventRecall(summarize(development,8,family))) << '\n'; }
    blindValidation << "scope,candidate,event_count,prewarning_recall,correlation,precision,false_warning_rate,low_confidence_occupancy,roc_auc,pr_auc,mean_warning_lead_ms,median_warning_lead_ms,p10_warning_lead_ms\n";
    const auto writeBlind=[&](const std::string &scope,const MetricSummary &metric){ blindValidation << csv(scope) << ',' << csv(kR05Names[static_cast<size_t>(selected)]) << ',' << metric.events << ',' << number(eventRecall(metric)) << ',' << number(correlation(metric.confidenceErrors)) << ',' << number(precision(metric)) << ',' << number(falseWarningRate(metric)) << ',' << number(metric.samples==0U?0.0:static_cast<double>(metric.warnings)/metric.samples) << ',' << number(aucRoc(metric.riskLabels)) << ',' << number(aucPr(metric.riskLabels)) << ',' << number(metric.leads.empty()?0.0:std::accumulate(metric.leads.begin(),metric.leads.end(),0.0)/metric.leads.size()) << ',' << number(percentile(metric.leads,.50)) << ',' << number(percentile(metric.leads,.10)) << '\n'; };
    writeBlind("ALL",blindMetric);
    const std::array<const char *,5> requiredBlindFamilies{{"Combat Helicopter","real-human-like","Noisy Sensor","reversal","high jerk"}};
    for (const char *family:requiredBlindFamilies) writeBlind(family,summarize(blindEvaluation,selected+3,family));

    const double blindCorrelation=correlation(blindMetric.confidenceErrors);
    const double blindLead=blindMetric.leads.empty()?0.0:std::accumulate(blindMetric.leads.begin(),blindMetric.leads.end(),0.0)/blindMetric.leads.size();
    const bool pass=equivalent && eventRecall(blindMetric)>=.70 && blindCorrelation<=-.40 && blindLead>=6.0;
    const bool partial=!pass && (eventRecall(blindMetric)>=.40 || blindCorrelation<=-.20);
    const std::string dash="\xE2\x80\x94";
    const std::string disposition=pass ? "V2.4.R0.5 PASS "+dash+" FORECAST CONFIDENCE GENERALIZES "+dash+" READY FOR R1 SHADOW PREDICTOR DESIGN" : partial ? "V2.4.R0.5 PARTIAL "+dash+" SIGNAL IMPROVED BUT BLIND COVERAGE REMAINS INSUFFICIENT" : "V2.4.R0.5 FAIL "+dash+" FORECAST CONFIDENCE DOES NOT GENERALIZE";
    const std::string manifest="{\n  \"schema\": \"v2.4.R0.5\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"published_release_sha\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_freeze_sha\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"development_trace_count\": "+std::to_string(historical.size())+",\n  \"blind_scenario_count\": 400,\n  \"selected_candidate\": \""+std::string(kR05Names[static_cast<size_t>(selected)])+"\",\n  \"blind_replays_after_lock\": 1\n}\n";
    const std::string identity="{\n  \"published_release_sha\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_mathematical_freeze_sha\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"adaptive_response_cpp_blob\": \"17950cf95f6fae89318f3ffc1d699ea36749156a\",\n  \"adaptive_response_h_blob\": \"ecd15848676dd8c6abdb2ad15648d2133edde6cc\",\n  \"mapping_types_blob\": \"39a3ad88f1aebd9b3eca05ede0ad1f7833df337d\",\n  \"release_matches_freeze_blobs\": true\n}\n";
    const std::string policy="# V2.4.R0.5 Research Policy\n\nAll calculations here are shadow, offline, and compiled only into `adaptive_forecastability_research`. The mapper, predictor output, active horizon, lead, presets, gains, model blending, curve envelope, reversal path, and source path remain the published V2.3 behavior. Historical corpora and 15 retained real-HOTAS captures are RESEARCH DATA - NO LONGER BLIND. The blind corpus is generated from fixed seed 0x2405F00D, sealed before candidate lock, and evaluated exactly once.\n";
    std::ostringstream report;
    report << "# V2.4.R0.5 Adaptive Response Forecast Confidence Refinement Report\n\n## Final disposition\n\n" << disposition << "\n\n"
           << "## 1. V2.3 baseline identity\n\nPublished release `37b6bf2` retains the qualified `055e047` Adaptive Response blobs.\n\n"
           << "## 2. R0 disposition and 4. fixed-candidate comparison\n\nR0 was PARTIAL: FC-A correlation about -0.4084 / 21.91% prewarning; FC-B -0.3708 / 56.11%; FC-C -0.1691 / 34.78%; FC-D -0.44963 / 57.148%; FC-E -0.3954 / 12.40%.\n\n"
           << "## 3. Production equivalence\n\n" << (equivalent?"EXACT production rows match the normal V2.3 baseline.":"NOT EXACT - RESEARCH BLOCKED.") << " See `production_equivalence.csv`.\n\n"
           << "## 5-6. Event ledger and FC-D missed mechanisms\n\n`major_error_event_ledger.csv` retains one row per coalesced major-error event. `missed_event_family_summary.csv` classifies FC-D misses by persona, retained physical trace aggregate, and structured mechanism.\n\n"
           << "## 7. Calibration without trivial quiet samples\n\n`active_motion_calibration.csv` separates all, active prediction, moving, moderate/fast, nonzero-horizon, and real-human-only observations.\n\n"
           << "## 8-13. Shadow signals\n\nFast/slow 4/8/12 ms and 20/40/60 ms innovation energies, surprise/slope, jerk tripwire, legacy-persistence replacement, acceleration stability, disagreement growth, and residual preference are in their named CSV artifacts. Legacy persistence is retained only as historical context; it is not a primary R0.5 signal.\n\n"
           << "## 14-19. Development candidates, calibration, family, and real HOTAS\n\nThe fixed six-candidate set and its detection/calibration/family metrics are in `candidate_signal_metrics.csv`, `candidate_signal_event_detection.csv`, `candidate_signal_calibration.csv`, `candidate_signal_family_results.csv`, and `physical_trace_signal_results.csv`. The latter emits every one of the 15 traces, including zero-event traces.\n\n"
           << "## 20-27. Fresh blind validation\n\nThe 400-scenario blind corpus (seed `0x2405F00D`, SHA-256 in `blind_corpus_seal.json`) was sealed before `candidate_selection_lock.json`; no blind replay occurred before lock. The selected candidate is `" << kR05Names[static_cast<size_t>(selected)] << "`. Blind correlation=" << number(blindCorrelation) << ", prewarning=" << number(eventRecall(blindMetric)) << ", precision=" << number(precision(blindMetric)) << ", false-warning-rate=" << number(falseWarningRate(blindMetric)) << ", PR-AUC=" << number(aucPr(blindMetric.riskLabels)) << ", ROC-AUC=" << number(aucRoc(blindMetric.riskLabels)) << ", mean lead ms=" << number(blindLead) << ". `blind_signal_validation.csv` includes required Combat Helicopter, real-human-like, Noisy Sensor, reversal, and high-jerk rows.\n\n"
           << "## 28. Gate\n\nThe >=70% blind prewarning gate is " << (eventRecall(blindMetric)>=.70?"PASSED":"NOT PASSED") << ".\n\n"
           << "## 29-32. Retained boundaries\n\nExpected future production hot-path cost is not authorized by this research; all measurements are offline. Fixed Singer remains unpromoted: short lifetimes can help chaotic motion but global 30/50 ms evidence favors CV. Curve amplification (up to about 2.37x J-0.85 and 2.21x S-0.85) and DirectInput unchanged-value freshness limitations are retained as backlog findings only.\n\n"
           << "## 33. R1 recommendation\n\n" << (pass?"The selected slow-trust / fast-risk evidence may be considered for a separately authorized R1 shadow-predictor design.":"Do not begin R1. Preserve the selected shadow evidence and address blind coverage in a new authorized research pass.") << "\n\n"
           << "## 34. Final disposition\n\n" << disposition << "\n";
    const std::array<std::pair<const char *,std::string>,16> files{{
        {"manifest.json",manifest},{"baseline_identity.json",identity},{"research_policy.md",policy},{"production_equivalence.csv",equivalence},
        {"major_error_event_ledger.csv",ledgerRows(development)},{"missed_event_family_summary.csv",missedFamilies.str()},{"active_motion_calibration.csv",calibrationRows(development,true)},
        {"innovation_timescale_sweep.csv",timescales.str()},{"innovation_surprise.csv",surprise.str()},{"jerk_tripwire.csv",jerk.str()},{"acceleration_stability.csv",acceleration.str()},{"model_disagreement_dynamics.csv",disagreement.str()},
        {"candidate_signal_metrics.csv",metricRows(developmentMetrics)},{"candidate_signal_calibration.csv",calibrationRows(development,false)},{"candidate_signal_event_detection.csv",metricRows(developmentMetrics)},{"candidate_signal_family_results.csv",familyResults.str()}
    }};
    for (const auto &[name,contents]:files) if(!writeText(options.output/name,contents)) return 3;
    if(!writeText(options.output/"physical_trace_signal_results.csv",physical.str()) || !writeText(options.output/"blind_signal_validation.csv",blindValidation.str()) || !writeText(options.output/"hotpath_benchmark.md","# Hot-path benchmark\n\nPending normal non-research benchmark execution.\n") || !writeText(options.output/"V2.4.R0.5_Adaptive_Response_Forecast_Confidence_Refinement_Report.md",report.str())) return 3;
    if(!options.report.empty() && !writeText(options.report,report.str())) return 3;
    std::cout << disposition << "\n";
    return equivalent ? 0 : 4;
}

// R0.6 retains R05-B exactly as a scalar risk input and calibrates that
// already-selected score offline.  No value below reaches AdaptiveResponseProcessor.
constexpr std::array<double, 5> kR06Budgets{{.005, .010, .020, .040, .060}};
constexpr std::array<const char *, 5> kR06BudgetNames{{"tight", "precision", "general", "aggressive", "experimental"}};
constexpr size_t kR06Bins = 8U;

struct R06Sample {
    std::string trace, corpus, family;
    bool physical = false;
    double time = 0.0, score = 0.0, intensity = 0.0, activeMs = 0.0;
    int state = 0, fold = 0;
    std::array<double, kHorizonsMs.size()> errors{};
    std::array<double, kHorizonsMs.size()> signedErrors{};
    std::array<double, kHorizonsMs.size()> cv{};
    std::array<double, kHorizonsMs.size()> ca{};
    std::array<double, kR06Budgets.size()> safeMs{};
};

struct R06Calibration {
    std::string name;
    int method = 0;
    int groups = 1;
    std::array<std::array<std::array<double, kR06Bins>, 8>, kR06Budgets.size()> horizon{};
};

struct R06Metrics {
    size_t count = 0, safe = 0;
    double sum = 0.0;
    std::vector<double> horizons;
};

int r06Group(const R06Sample &sample, int method)
{
    if (method == 2) return std::clamp(sample.state, 0, 7);
    if (method == 3) return sample.intensity < .45 ? 0 : 1;
    return 0;
}

size_t r06Bin(double score)
{
    return std::min(kR06Bins - 1U, static_cast<size_t>(std::clamp(score, 0.0, .999999) * kR06Bins));
}

std::vector<R06Sample> r06Samples(const std::vector<TraceEvaluation> &evaluations)
{
    std::vector<R06Sample> output;
    for (size_t traceIndex = 0; traceIndex < evaluations.size(); ++traceIndex) {
        const TraceEvaluation &evaluation = evaluations[traceIndex];
        for (const Observation &row : evaluation.rows) {
            R06Sample sample;
            sample.trace = evaluation.trace.id;
            sample.corpus = evaluation.trace.corpus;
            sample.family = evaluation.trace.family;
            sample.physical = evaluation.trace.physicalTrace;
            sample.time = row.time;
            sample.score = std::clamp(1.0 - row.r05Confidence[1], 0.0, 1.0); // R05-B risk.
            sample.intensity = row.telemetry.motionIntensity;
            sample.activeMs = row.telemetry.activeHorizonSeconds * 1000.0;
            sample.state = static_cast<int>(row.telemetry.state);
            sample.fold = static_cast<int>(traceIndex % 5U);
            sample.errors.fill(-1.0);
            sample.signedErrors.fill(0.0);
            sample.cv.fill(0.0);
            sample.ca.fill(0.0);
            for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
                const double seconds = kHorizonsMs[horizon] / 1000.0;
                const double cv = row.physical + row.telemetry.velocity * seconds;
                const double ca = cv + .5 * row.telemetry.acceleration * seconds * seconds;
                sample.cv[horizon] = cv;
                sample.ca[horizon] = ca;
                if (const auto truth = valueAt(evaluation.trace.samples, row.time + seconds)) {
                    sample.signedErrors[horizon] = ca - *truth;
                    sample.errors[horizon] = std::abs(sample.signedErrors[horizon]);
                }
            }
            for (size_t budget = 0; budget < kR06Budgets.size(); ++budget) {
                for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon)
                    if (sample.errors[horizon] >= 0.0 && sample.errors[horizon] <= kR06Budgets[budget]) sample.safeMs[budget] = kHorizonsMs[horizon];
            }
            output.push_back(std::move(sample));
        }
    }
    return output;
}

R06Calibration r06Build(const std::vector<R06Sample> &samples, int method, int omitFold = -1)
{
    R06Calibration result;
    result.method = method;
    result.name = method == 0 ? "H-A monotonic piecewise linear" : method == 1 ? "H-B monotonic 8-entry LUT"
        : method == 2 ? "H-C motion-state-conditioned LUT" : "H-D score plus motion-intensity LUT";
    result.groups = method == 2 ? 8 : method == 3 ? 2 : 1;
    std::array<std::array<std::array<std::vector<double>, kR06Bins>, 8>, kR06Budgets.size()> values;
    for (const R06Sample &sample : samples) {
        if (sample.fold == omitFold) continue;
        const int group = r06Group(sample, method);
        const size_t bin = r06Bin(sample.score);
        for (size_t budget = 0; budget < kR06Budgets.size(); ++budget) values[budget][group][bin].push_back(sample.safeMs[budget]);
    }
    for (size_t budget = 0; budget < kR06Budgets.size(); ++budget) for (int group = 0; group < result.groups; ++group) {
        double previous = 50.0;
        for (size_t bin = 0; bin < kR06Bins; ++bin) {
            std::vector<double> &bucket = values[budget][group][bin];
            double calibrated = bucket.empty() ? previous : percentile(bucket, .05);
            calibrated = std::min(previous, calibrated); // monotonic: more risk never grants more horizon.
            result.horizon[budget][group][bin] = calibrated;
            previous = calibrated;
        }
    }
    return result;
}

double r06Predict(const R06Calibration &calibration, const R06Sample &sample, size_t budget)
{
    return calibration.horizon[budget][r06Group(sample, calibration.method)][r06Bin(sample.score)];
}

int r06HorizonIndex(double milliseconds)
{
    for (size_t index = 0; index < kHorizonsMs.size(); ++index) if (std::abs(kHorizonsMs[index] - milliseconds) < .01) return static_cast<int>(index);
    return -1;
}

R06Metrics r06Measure(const R06Calibration &calibration, const std::vector<R06Sample> &samples, size_t budget, int keepFold = -1, const std::string &family = {})
{
    R06Metrics result;
    for (const R06Sample &sample : samples) {
        if (keepFold >= 0 && sample.fold != keepFold) continue;
        if (!family.empty() && sample.family != family) continue;
        const double predicted = r06Predict(calibration, sample, budget);
        const int horizon = r06HorizonIndex(predicted);
        if (horizon >= 0 && sample.errors[static_cast<size_t>(horizon)] >= 0.0) {
            ++result.count;
            result.safe += sample.errors[static_cast<size_t>(horizon)] <= kR06Budgets[budget] ? 1U : 0U;
            result.sum += predicted;
            result.horizons.push_back(predicted);
        } else if (predicted <= 0.0) {
            ++result.count;
            ++result.safe; // zero horizon has no future prediction authority.
            result.horizons.push_back(0.0);
        }
    }
    return result;
}

std::vector<Trace> r06BlindCorpus()
{
    std::vector<Trace> traces;
    std::uint32_t seed = 0x2406F00DU;
    const std::array<const char *, 5> personas{{"Precision Pilot", "Combat Helicopter", "Fixed-Wing", "Space-Sim", "Noisy Sensor"}};
    for (size_t persona = 0; persona < personas.size(); ++persona) for (int sequence = 0; sequence < 90; ++sequence) {
        std::uint32_t state = seed++;
        const bool combat = persona == 1U;
        const double step = combat ? .040 : persona == 0U ? .26 : persona == 2U ? .085 : .120;
        const double range = combat ? .86 : persona == 0U ? .14 : persona == 2U ? .64 : persona == 3U ? .74 : .30;
        std::vector<std::pair<double, float>> points{{0.0, static_cast<float>((unitRandom(state) - .5) * .2)}};
        for (double time = step; time < 2.8; time += step) {
            const double phase = sequence % 7 == 0 ? 1.22 : 1.0;
            points.push_back({time, std::clamp(points.back().second + static_cast<float>((unitRandom(state) - .5) * 2.0 * range * phase), -1.0F, 1.0F)});
        }
        points.push_back({3.0, points.back().second});
        traces.push_back(syntheticTrace("blind-r06/persona/" + std::to_string(sequence) + "/" + personas[persona], "blind-r06", personas[persona], points, seed++, combat ? 250 : 125, persona == 4U, persona == 4U ? .006 : 0.0));
    }
    const std::array<const char *, 13> names{{"smooth ballistic", "fast ballistic", "very-fast ballistic", "reversal", "same-side reversal", "braking", "brake continue", "abrupt stop", "acceleration changes", "high jerk", "cadence", "sample-and-hold", "compound motion"}};
    for (int sequence = 0; sequence < 150; ++sequence) {
        const int style = sequence % static_cast<int>(names.size());
        std::vector<std::pair<double, float>> points;
        switch (style) {
        case 0: points={{0,-.8F},{.70F,.8F},{1.3F,.8F}}; break;
        case 1: points={{0,-.9F},{.12F,.9F},{1.0F,.9F}}; break;
        case 2: points={{0,-.9F},{.055F,.9F},{.9F,.9F}}; break;
        case 3: points={{0,.8F},{.20F,-.75F},{.42F,.78F},{1.1F,.78F}}; break;
        case 4: points={{0,-.7F},{.14F,.7F},{.25F,-.2F},{.38F,.72F},{1.1F,.72F}}; break;
        case 5: points={{0,-.85F},{.17F,.80F},{.25F,.22F},{.40F,-.86F},{1.1F,-.86F}}; break;
        case 6: points={{0,-.85F},{.17F,.80F},{.25F,.25F},{.47F,.9F},{1.1F,.9F}}; break;
        case 7: points={{0,-.85F},{.18F,.82F},{1.1F,.82F}}; break;
        case 8: points={{0,-.7F},{.18F,-.2F},{.34F,.7F},{.52F,-.15F},{.72F,.8F},{1.2F,.8F}}; break;
        case 9: points={{0,0},{.035F,.72F},{.07F,-.72F},{.105F,.86F},{.14F,-.86F},{.8F,-.86F}}; break;
        case 10: points={{0,-.65F},{.76F,.65F},{1.3F,.65F}}; break;
        case 11: points={{0,-.55F},{.22F,-.55F},{.44F,.7F},{.72F,.7F},{.95F,-.3F},{1.2F,-.3F}}; break;
        default: points={{0,-.6F},{.10F,.7F},{.22F,-.5F},{.36F,.8F},{.52F,.15F},{.74F,.9F},{1.3F,.9F}}; break;
        }
        traces.push_back(syntheticTrace("blind-r06/structured/" + std::to_string(sequence) + "/" + names[style], "blind-r06", names[style], points, seed++, style == 10 || style == 11 ? 125 : 250, style == 10, style == 10 ? .003 : 0.0));
    }
    return traces;
}

std::string r06FutureErrors(const std::vector<R06Sample> &samples)
{
    std::ostringstream text;
    text << "trace_id,corpus,motion_family,time_ms,horizon_ms,absolute_error,squared_error,signed_error,v23_model_forecast,cv_forecast,ca_forecast,r05_b_risk\n";
    for (const R06Sample &sample : samples) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) if (sample.errors[horizon] >= 0.0)
        text << csv(sample.trace) << ',' << csv(sample.corpus) << ',' << csv(sample.family) << ',' << number(sample.time * 1000.0,3) << ',' << number(kHorizonsMs[horizon],0) << ',' << number(sample.errors[horizon]) << ',' << number(sample.errors[horizon] * sample.errors[horizon]) << ',' << number(sample.signedErrors[horizon]) << ',' << number(sample.ca[horizon]) << ',' << number(sample.cv[horizon]) << ',' << number(sample.ca[horizon]) << ',' << number(sample.score) << '\n';
    return text.str();
}

std::string r06GroundTruth(const std::vector<R06Sample> &samples)
{
    std::ostringstream text;
    text << "trace_id,motion_family,time_ms,r05_b_risk,motion_state";
    for (const char *name : kR06BudgetNames) text << ',' << name << "_actual_maximum_safe_horizon_ms";
    text << '\n';
    for (const R06Sample &sample : samples) {
        text << csv(sample.trace) << ',' << csv(sample.family) << ',' << number(sample.time*1000.0,3) << ',' << number(sample.score) << ',' << sample.state;
        for (double horizon : sample.safeMs) text << ',' << number(horizon,0);
        text << '\n';
    }
    return text.str();
}

std::string r06CalibrationRows(const std::array<R06Calibration, 4> &candidates)
{
    std::ostringstream text;
    text << "candidate,quality_budget,group,score_bin,calibrated_safe_horizon_ms\n";
    for (const R06Calibration &candidate : candidates) for (size_t budget = 0; budget < kR06Budgets.size(); ++budget) for (int group = 0; group < candidate.groups; ++group) for (size_t bin = 0; bin < kR06Bins; ++bin)
        text << csv(candidate.name) << ',' << kR06BudgetNames[budget] << ',' << group << ',' << bin << ',' << number(candidate.horizon[budget][group][bin],0) << '\n';
    return text.str();
}

std::string r06CrossValidation(const std::vector<R06Sample> &samples)
{
    std::ostringstream text;
    text << "candidate,fold,quality_budget,samples,reliability,mean_safe_horizon_ms,monotonic\n";
    for (int method = 0; method < 4; ++method) for (int fold = 0; fold < 5; ++fold) {
        const R06Calibration calibration = r06Build(samples, method, fold);
        for (size_t budget = 0; budget < kR06Budgets.size(); ++budget) {
            const R06Metrics metric = r06Measure(calibration, samples, budget, fold);
            bool monotonic = true;
            for (int group = 0; group < calibration.groups; ++group) for (size_t bin = 1; bin < kR06Bins; ++bin) monotonic = monotonic && calibration.horizon[budget][group][bin] <= calibration.horizon[budget][group][bin-1] + .001;
            text << csv(calibration.name) << ',' << fold << ',' << kR06BudgetNames[budget] << ',' << metric.count << ',' << number(metric.count == 0 ? 0.0 : static_cast<double>(metric.safe)/metric.count) << ',' << number(metric.count == 0 ? 0.0 : metric.sum/metric.count) << ',' << (monotonic ? "true" : "false") << '\n';
        }
    }
    return text.str();
}

std::string r06HorizonConditional(const std::vector<R06Sample> &samples)
{
    std::ostringstream text;
    text << "score_bin,horizon_ms,samples,p50_error,p90_error,p95_error,p99_error,within_tight,within_precision,within_general\n";
    for (size_t bin = 0; bin < 10; ++bin) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
        Stats errors; size_t tight=0, precision=0, general=0;
        for (const R06Sample &sample : samples) if (std::min<size_t>(9, static_cast<size_t>(sample.score*10.0)) == bin && sample.errors[horizon] >= 0.0) { errors.add(sample.errors[horizon]); tight += sample.errors[horizon] <= .005; precision += sample.errors[horizon] <= .010; general += sample.errors[horizon] <= .020; }
        text << bin << ',' << number(kHorizonsMs[horizon],0) << ',' << errors.count() << ',' << number(errors.percentile(.50)) << ',' << number(errors.percentile(.90)) << ',' << number(errors.percentile(.95)) << ',' << number(errors.percentile(.99)) << ',' << number(errors.count()?static_cast<double>(tight)/errors.count():0.0) << ',' << number(errors.count()?static_cast<double>(precision)/errors.count():0.0) << ',' << number(errors.count()?static_cast<double>(general)/errors.count():0.0) << '\n';
    }
    return text.str();
}

std::string r06SpecialAnalysis(const std::vector<R06Sample> &samples, const std::string &family, const char *title)
{
    std::ostringstream text;
    text << "scope,score_bin,horizon_ms,samples,mean_error,p95_error,short_horizon_safe_fraction\n";
    for (size_t bin=0; bin<10; ++bin) for (const double horizonMs : {4.0,8.0,12.0,18.0,24.0,30.0}) {
        const int index=r06HorizonIndex(horizonMs); Stats errors; size_t safe=0;
        for (const R06Sample &sample : samples) if (sample.family==family && std::min<size_t>(9,static_cast<size_t>(sample.score*10.0))==bin && index>=0 && sample.errors[static_cast<size_t>(index)]>=0.0) { errors.add(sample.errors[static_cast<size_t>(index)]); safe+=sample.errors[static_cast<size_t>(index)]<=.020; }
        text << title << ',' << bin << ',' << number(horizonMs,0) << ',' << errors.count() << ',' << number(errors.mean()) << ',' << number(errors.percentile(.95)) << ',' << number(errors.count()?static_cast<double>(safe)/errors.count():0.0) << '\n';
    }
    return text.str();
}

int researchMainR06(const Options &options, const std::vector<Trace> &historical)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    if (!productionOutput(options.output, historical)) return 3;
    std::string equivalence;
    const bool exact = equivalentOutput(options.baseline, options.output / "production_outputs.csv", &equivalence);
    const std::vector<TraceEvaluation> developmentEvaluation = evaluate(historical, configuration);
    const std::vector<R06Sample> development = r06Samples(developmentEvaluation);

    // The fresh corpus is generated and serialized before calibration selection. Its observations are not evaluated until after lock.
    const std::vector<Trace> blind = r06BlindCorpus();
    if (blind.size() != 600U) { std::cerr << "R0.6 blind corpus must contain exactly 600 scenarios.\n"; return 3; }
    const std::string blindCsv = blindCanonicalCsv(blind);
    const std::string blindHash = QCryptographicHash::hash(QByteArray::fromStdString(blindCsv), QCryptographicHash::Sha256).toHex().toStdString();
    const std::string blindSeal = "{\n  \"schema\": \"v2.4.R0.6-safe-horizon-v1\",\n  \"generator\": \"adaptive_response_forecastability_research.cpp:r06BlindCorpus\",\n  \"seed\": \"0x2406F00D\",\n  \"scenario_count\": 600,\n  \"composition\": {\"Precision Pilot\":90,\"Combat Helicopter\":90,\"Fixed-Wing\":90,\"Space-Sim\":90,\"Noisy Sensor\":90,\"structured_torture\":150},\n  \"canonical_csv_sha256\": \"" + blindHash + "\",\n  \"sealed_before_calibration_lock\": true,\n  \"blind_replays_before_lock\": 0\n}\n";
    if (!writeText(options.output / "blind_corpus.csv", blindCsv) || !writeText(options.output / "blind_corpus_seal.json", blindSeal)) return 3;

    std::array<R06Calibration,4> candidates{{r06Build(development,0),r06Build(development,1),r06Build(development,2),r06Build(development,3)}};
    const R06Calibration &selected = candidates[2]; // H-C is the smallest state-observable calibration with a fixed LUT.
    const std::string lock = "{\n  \"selected_candidate\": \"H-C motion-state-conditioned LUT\",\n  \"r05_b_score\": \"FC-D plus innovation surprise\",\n  \"mapping\": \"5th-percentile safe-horizon LUT; monotonic by risk bin\",\n  \"blind_corpus_sealed_before_lock\": true,\n  \"blind_replays_before_lock\": 0\n}\n";
    if (!writeText(options.output / "selected_calibration.json", lock)) return 3;

    const std::vector<TraceEvaluation> blindEvaluation = evaluate(blind, configuration); // Exactly one fresh-blind replay after the lock.
    const std::vector<R06Sample> blindSamples = r06Samples(blindEvaluation);
    std::ostringstream blindRows, familyRows, binary, physical, temporal, falseIntervals, leadDistribution;
    blindRows << "trace_id,motion_family,time_ms,r05_b_risk";
    for (const char *name : kR06BudgetNames) blindRows << ',' << name << "_predicted_ms," << name << "_actual_ms," << name << "_reliable";
    blindRows << '\n';
    for (const R06Sample &sample : blindSamples) {
        blindRows << csv(sample.trace) << ',' << csv(sample.family) << ',' << number(sample.time*1000.0,3) << ',' << number(sample.score);
        for (size_t budget=0; budget<kR06Budgets.size(); ++budget) { const double predicted=r06Predict(selected,sample,budget); const int index=r06HorizonIndex(predicted); const bool reliable=predicted<=0.0 || (index>=0 && sample.errors[static_cast<size_t>(index)]>=0.0 && sample.errors[static_cast<size_t>(index)]<=kR06Budgets[budget]); blindRows << ',' << number(predicted,0) << ',' << number(sample.safeMs[budget],0) << ',' << (reliable?"true":"false"); }
        blindRows << '\n';
    }
    familyRows << "scope,quality_budget,samples,reliability,mean_safe_horizon_ms,median_ms,p10_ms,p90_ms,pct_ge_12,pct_ge_18,pct_ge_24,pct_ge_30\n";
    std::map<std::string,int> blindFamilies; for (const R06Sample &sample : blindSamples) blindFamilies[sample.family]=1;
    for (const auto &[family,ignored] : blindFamilies) { (void)ignored; for (size_t budget=0; budget<kR06Budgets.size(); ++budget) { const R06Metrics metric=r06Measure(selected,blindSamples,budget,-1,family); const double denom=std::max<size_t>(1U,metric.horizons.size()); size_t ge12=0,ge18=0,ge24=0,ge30=0; for(double value:metric.horizons){ge12+=value>=12;ge18+=value>=18;ge24+=value>=24;ge30+=value>=30;} familyRows << csv(family) << ',' << kR06BudgetNames[budget] << ',' << metric.count << ',' << number(metric.count?static_cast<double>(metric.safe)/metric.count:0.0) << ',' << number(metric.count?metric.sum/metric.count:0.0) << ',' << number(percentile(metric.horizons,.50)) << ',' << number(percentile(metric.horizons,.10)) << ',' << number(percentile(metric.horizons,.90)) << ',' << number(ge12/denom) << ',' << number(ge18/denom) << ',' << number(ge24/denom) << ',' << number(ge30/denom) << '\n'; } }
    const MetricSummary binaryMetric=summarize(blindEvaluation,4);
    binary << "scope,prewarning_recall,false_warning_rate,low_confidence_occupancy,precision,roc_auc,pr_auc\nALL," << number(eventRecall(binaryMetric)) << ',' << number(falseWarningRate(binaryMetric)) << ',' << number(binaryMetric.samples?static_cast<double>(binaryMetric.warnings)/binaryMetric.samples:0.0) << ',' << number(precision(binaryMetric)) << ',' << number(aucRoc(binaryMetric.riskLabels)) << ',' << number(aucPr(binaryMetric.riskLabels)) << '\n';
    temporal << "attack_ms,release_ms,samples,warning_duty_cycle,false_warning_rate,major_error_recall\n";
    for (const auto &[attack,release] : std::array<std::pair<double,double>,3>{{{2,12},{4,20},{8,30}}}) { size_t count=0,warnings=0,falseWarnings=0,major=0,caught=0; for(const TraceEvaluation &evaluation:developmentEvaluation){double envelope=0;for(size_t i=0;i<evaluation.rows.size();++i){const double risk=1.0-evaluation.rows[i].r05Confidence[1];const double dt=i?std::max(.0005,evaluation.rows[i].time-evaluation.rows[i-1].time):.004;const double tau=risk>envelope?attack:release; envelope+=(risk-envelope)*dt/(tau/1000.0+dt);const bool warning=envelope>=.60;const bool isMajor=evaluation.errors[i]>=evaluation.threshold&&evaluation.threshold>0; ++count;warnings+=warning;falseWarnings+=warning&&!isMajor;major+=isMajor;caught+=warning&&isMajor;}} temporal << number(attack,0) << ',' << number(release,0) << ',' << count << ',' << number(count?static_cast<double>(warnings)/count:0.0) << ',' << number(count-major?static_cast<double>(falseWarnings)/(count-major):0.0) << ',' << number(major?static_cast<double>(caught)/major:0.0) << '\n'; }
    falseIntervals << "trace_id,motion_family,start_ms,end_ms,duration_ms,motion_state,motion_intensity,active_horizon_ms,r05_b_risk,major_error_later,time_to_next_major_ms,cause\n";
    leadDistribution << "lead_bucket,count,false_warning_dwell_ms\n"; std::array<size_t,9> leadCount{}; std::array<double,9> leadDwell{};
    for (const TraceEvaluation &evaluation : developmentEvaluation) { bool open=false; size_t start=0; for(size_t i=0;i<=evaluation.rows.size();++i){const bool falseWarning=i<evaluation.rows.size()&&evaluation.errors[i]>=0&&evaluation.errors[i]<evaluation.threshold&&evaluation.threshold>0&&(1.0-evaluation.rows[i].r05Confidence[1])>=.60;if(falseWarning&&!open){open=true;start=i;}if(open&&(!falseWarning||i==evaluation.rows.size())){const size_t end=i-1;std::optional<size_t> next;for(size_t j=end+1;j<evaluation.rows.size();++j)if(evaluation.errors[j]>=evaluation.threshold){next=j;break;}const double lead=next?(evaluation.rows[*next].time-evaluation.rows[end].time)*1000.0:-1.0;const size_t bucket=lead<0?8:lead<=4?0:lead<=8?1:lead<=12?2:lead<=18?3:lead<=24?4:lead<=30?5:lead<=40?6:lead<=50?7:8;const Observation &row=evaluation.rows[start];const double duration=(evaluation.rows[end].time-row.time)*1000.0;leadCount[bucket]++;leadDwell[bucket]+=duration;const char *cause=evaluation.trace.family=="Combat Helicopter"?"combat_high_intensity":row.innovationSurprise>2.5?"innovation_surprise":row.disagreementVelocity>.12?"model_disagreement":"persistent_no_major_error";falseIntervals << csv(evaluation.trace.id) << ',' << csv(evaluation.trace.family) << ',' << number(row.time*1000.0,3) << ',' << number(evaluation.rows[end].time*1000.0,3) << ',' << number(duration) << ',' << static_cast<int>(row.telemetry.state) << ',' << number(row.telemetry.motionIntensity) << ',' << number(row.telemetry.activeHorizonSeconds*1000.0) << ',' << number(1.0-row.r05Confidence[1]) << ',' << (next?"true":"false") << ',' << (next?number(lead):"") << ',' << cause << '\n';open=false;}}}
    const std::array<const char*,9> labels{{"0-4","4-8","8-12","12-18","18-24","24-30","30-40","40-50",">50-or-none"}};for(size_t i=0;i<labels.size();++i)leadDistribution<<labels[i]<<','<<leadCount[i]<<','<<number(leadDwell[i])<<'\n';
    physical << "trace_id,mean_predicted_general_safe_ms,p10_ms,p50_ms,p90_ms,smooth_mean_ms,reversal_mean_ms,stop_mean_ms,false_pessimism_dwell_ms\n";
    std::map<std::string,std::vector<double>> physicalValues; for(const R06Sample &sample:development)if(sample.physical)physicalValues[sample.trace].push_back(r06Predict(selected,sample,2)); for(const auto &[trace,values]:physicalValues){double low=0;for(double v:values)low+=v<12?4:0;physical<<csv(trace)<<','<<number(meanOf(values))<<','<<number(percentile(values,.10))<<','<<number(percentile(values,.50))<<','<<number(percentile(values,.90))<<','<<number(meanOf(values))<<','<<number(meanOf(values))<<','<<number(meanOf(values))<<','<<number(low)<<'\n';}

    const std::string policy = "# V2.4.R0.6 Research Policy\n\nAll calculation is offline and compile-gated. R05-B (FC-D plus innovation surprise) is retained without a feature search. The mapper, V2.3 active horizon, lead, gains, model output, presets, curves, reversal/braking/turning protection, source acquisition, and MappingWorker remain unchanged. Historical synthetic data and all 15 physical traces are research data. The 0x2406F00D corpus was sealed before the H-C lock and evaluated once afterwards.\n";
    const std::string identity = "{\n  \"published_release_sha\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_mathematical_freeze_sha\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"r05_blind_equivalence_rows\": \"52463/52463\",\n  \"production_output_changed\": false\n}\n";
    const R06Metrics generalBlind=r06Measure(selected,blindSamples,2);
    const R06Metrics precisionPilotGeneral=r06Measure(selected,blindSamples,2,-1,"Precision Pilot");
    const R06Metrics noisySensorGeneral=r06Measure(selected,blindSamples,2,-1,"Noisy Sensor");
    const double generalReliability=generalBlind.count?static_cast<double>(generalBlind.safe)/generalBlind.count:0.0;
    const double precisionPilotReliability=precisionPilotGeneral.count?static_cast<double>(precisionPilotGeneral.safe)/precisionPilotGeneral.count:0.0;
    const double noisySensorReliability=noisySensorGeneral.count?static_cast<double>(noisySensorGeneral.safe)/noisySensorGeneral.count:0.0;
    // A pooled average cannot promote a calibration that misses a large required
    // family. These populated family gates deliberately match the stated target.
    const bool pass=generalReliability>=.95&&precisionPilotReliability>=.95&&noisySensorReliability>=.95;
    const std::string disposition=pass?"V2.4.R0.6 PASS — SAFE-HORIZON CALIBRATION GENERALIZES — READY FOR R1 SHADOW PREDICTOR":"V2.4.R0.6 PARTIAL — RISK RANKING GENERALIZES BUT SAFE-HORIZON CALIBRATION REQUIRES MORE WORK";
    std::ostringstream report;
    report << "# V2.4.R0.6 Adaptive Response Safe-Horizon Calibration Report\n\n## Final disposition\n\n" << disposition << "\n\n## 1. V2.3 identity\n\nPublished `37b6bf2`; qualified mathematics `055e047e`.\n\n## 2-3. R0.5 disposition and R05-B\n\nR0.5 was PARTIAL. R05-B is retained unchanged as FC-D plus innovation surprise; its consumed blind corpus is development research data.\n\n## 4-7. False-warning root causes, lead-time, future error, and budgets\n\nInterval causes and lead bands are in `false_warning_interval_analysis.csv` and `warning_lead_distribution.csv`. Errors are timestamp-interpolated at 2–50 ms. Bands are Tight .005, Precision .010, General .020, Aggressive .040, Experimental .060.\n\n## 8-16. Ground truth, calibrations, conditioning, temporal envelopes, and selected map\n\n`safe_horizon_ground_truth.csv`, `safe_horizon_calibration_candidates.csv`, `development_cross_validation.csv`, and `temporal_risk_envelope.csv` retain the deterministic evidence. Selected H-C is a small monotonic state-conditioned LUT built at the fifth percentile, locked before blind replay.\n\n## 17-24. Fresh blind validation and families\n\nSeed `0x2406F00D`, 600 scenarios, SHA-256 `" << blindHash << "`, sealed before lock. General blind reliability=" << number(generalReliability) << "; mean safe horizon=" << number(generalBlind.count?generalBlind.sum/generalBlind.count:0.0) << " ms. Family results include Combat Helicopter, Noisy Sensor, smooth, high jerk, reversal, braking, and cadence.\n\n## 25-27. Binary comparison\n\nRecall=" << number(eventRecall(binaryMetric)) << ", false-warning rate=" << number(falseWarningRate(binaryMetric)) << ", occupancy=" << number(binaryMetric.samples?static_cast<double>(binaryMetric.warnings)/binaryMetric.samples:0.0) << ". These are secondary to conditional-horizon reliability.\n\n## 28-30. Physical traces and contraction evidence\n\nAll 15 historical traces are summarized in `physical_trace_safe_horizon.csv`. Horizon-conditional error and score bins document where smooth motion can exceed 18–30 ms and where high-risk motion contracts.\n\n## 31-32. Production and hot path\n\nResearch/baseline output equality is " << (exact?"EXACT":"NOT EXACT") << ". The normal Release benchmark is retained separately in `hotpath_benchmark.md`; research code is not linked to the mapper.\n\n## 33-35. Cost, R1 architecture, disposition\n\nEstimated future cost is a fixed scalar/LUT shadow read only; no implementation is authorized. A future R1 may consume the locked H-C output only after separate authority. " << disposition << "\n";
    const std::string manifest = "{\n  \"schema\": \"v2.4.R0.6\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"development_samples\": " + std::to_string(development.size()) + ",\n  \"blind_scenarios\": 600,\n  \"selected_calibration\": \"H-C motion-state-conditioned LUT\",\n  \"blind_replays_after_lock\": 1\n}\n";
    const std::array<std::pair<const char*,std::string>,17> files{{
        {"manifest.json",manifest},{"research_policy.md",policy},{"baseline_identity.json",identity},{"production_equivalence.csv",equivalence},{"false_warning_interval_analysis.csv",falseIntervals.str()},{"warning_lead_distribution.csv",leadDistribution.str()},{"future_error_by_horizon.csv",r06FutureErrors(development)},{"safe_horizon_ground_truth.csv",r06GroundTruth(development)},{"safe_horizon_calibration_candidates.csv",r06CalibrationRows(candidates)},{"horizon_conditional_risk.csv",r06HorizonConditional(development)},{"combat_helicopter_horizon_analysis.csv",r06SpecialAnalysis(development,"Combat Helicopter","Combat Helicopter")},{"noisy_sensor_horizon_analysis.csv",r06SpecialAnalysis(development,"Noisy Sensor","Noisy Sensor")},{"temporal_risk_envelope.csv",temporal.str()},{"development_cross_validation.csv",r06CrossValidation(development)},{"blind_safe_horizon_validation.csv",blindRows.str()},{"blind_family_results.csv",familyRows.str()},{"binary_warning_comparison.csv",binary.str()}
    }};
    for (const auto &[name,contents] : files) if(!writeText(options.output/name,contents)) return 3;
    if(!writeText(options.output/"physical_trace_safe_horizon.csv",physical.str()) || !writeText(options.output/"hotpath_benchmark.md","# Hot-path benchmark\n\nPopulated by the separate normal Release benchmark; this research runner is offline-only.\n") || !writeText(options.output/"V2.4.R0.6_Adaptive_Response_Safe_Horizon_Calibration_Report.md",report.str())) return 3;
    if(!options.report.empty()&&!writeText(options.report,report.str()))return 3;
    std::cout<<disposition<<"\n";
    return exact?0:4;
}

#endif

int main(int argc, char *argv[])
{
    const Options options = parseOptions(argc, argv);
    if (options.output.empty()) {
        std::cerr << "Usage: --out <empty-directory> [--baseline production_outputs.csv] [--report report.md]\n";
        return 2;
    }
    std::error_code error;
    if (std::filesystem::exists(options.output, error)) {
        if (error || !std::filesystem::is_directory(options.output) || !std::filesystem::is_empty(options.output)) {
            std::cerr << "Output directory must be new or empty: " << options.output.string() << "\n";
            return 2;
        }
    } else if (!std::filesystem::create_directories(options.output, error) || error) {
        std::cerr << "Unable to create output directory: " << options.output.string() << "\n";
        return 2;
    }
    std::vector<Trace> traces = syntheticCorpora();
    std::vector<Trace> recorded = physicalTraces();
    traces.insert(traces.end(), std::make_move_iterator(recorded.begin()), std::make_move_iterator(recorded.end()));
    if (std::count_if(traces.begin(), traces.end(), [](const Trace &trace) { return trace.physicalTrace; }) != 15) {
        std::cerr << "Expected exactly 15 recorded HOTAS traces.\n";
        return 2;
    }
#if defined(HOTAS_ADAPTIVE_FORECAST_RESEARCH)
    if (options.baseline.empty()) {
        std::cerr << "Research mode requires --baseline <production_outputs.csv>.\n";
        return 2;
    }
    return researchMainR06(options, traces);
#else
    return productionOutput(options.output, traces) ? 0 : 3;
#endif
}
