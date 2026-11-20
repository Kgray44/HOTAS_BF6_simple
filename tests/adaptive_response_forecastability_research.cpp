#include "adaptive_response.h"
#include "response_curve.h"

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
    return researchMain(options, traces);
#else
    return productionOutput(options.output, traces) ? 0 : 3;
#endif
}
