#include "verification_harness.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

constexpr std::uint32_t kCharacterizationSeed = 0x23A10001U;
constexpr std::uint32_t kValidationSeed = 0x23A10002U;
constexpr std::uint32_t kHoldoutSeed = 0x23A10003U;

struct NamedConfiguration {
    std::string name;
    RuntimeAdaptiveResponseConfig value;
    std::string kind;
};

struct PhysicalSample {
    std::uint64_t timestampUs = 0;
    float physical = 0.0F;
    bool sourceReported = true;
    bool sourceChanged = false;
    double cadenceHz = 0.0;
};

struct PhysicalTrace {
    QString id;
    QString sourcePath;
    QString sha256;
    int axis = -1;
    QString axisLabel;
    std::vector<PhysicalSample> samples;
};

struct RunRow {
    std::string stage;
    std::string partition;
    std::string scenarioId;
    std::string family;
    std::string configuration;
    int configuredHorizonMs = 0;
    double configuredLead = 0.0;
    int sourceRateHz = 0;
    std::uint64_t samples = 0;
    double durationMs = 0.0;
    double mae = 0.0;
    double rmse = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double peak = 0.0;
    double fidelity = 1.0;
    double meanHorizonMs = 0.0;
    double p95HorizonMs = 0.0;
    double peakHorizonMs = 0.0;
    double meanLead = 0.0;
    double p95Lead = 0.0;
    double peakLead = 0.0;
    double meanOnsetAuthority = 0.0;
    double peakOnsetAuthority = 0.0;
    double meanSustainedEvidence = 0.0;
    double meanSustainedAuthority = 0.0;
    double peakSustainedAuthority = 0.0;
    double meanExtensionMs = 0.0;
    double peakExtensionMs = 0.0;
    double extensionDwellMs = 0.0;
    double maximumContinuousExtensionDwellMs = 0.0;
    double extensionDuringBrakingMs = 0.0;
    double peakTurningPointConfidence = 0.0;
    double minimumTurningHorizonLimitMs = 0.0;
    double minimumTurningLeadLimit = 0.0;
    double leadClampDwellMs = 0.0;
    double maximumContinuousLeadClampDwellMs = 0.0;
    double wrongDirectionLeadArea = 0.0;
    double reversalReacquisitionMs = -1.0;
    double leadAtStop = 0.0;
    double settlingMs = -1.0;
    double stationaryLeadRms = 0.0;
    double noiseAmplification = 0.0;
    double maximumOutputStep = 0.0;
    std::uint64_t falseReversals = 0;
    std::uint64_t nonFinite = 0;
    std::uint64_t illegalOutput = 0;
    std::uint64_t stationaryDrift = 0;
    std::string consequence;
};

struct Aggregate {
    std::uint64_t rows = 0;
    std::uint64_t samples = 0;
    std::uint64_t beneficial = 0;
    std::uint64_t benign = 0;
    std::uint64_t harmful = 0;
    std::uint64_t hard = 0;
    double mae = 0.0;
    double fidelity = 0.0;
    double p95Horizon = 0.0;
};

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0)
        * static_cast<double>(values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

QString csv(const std::string &value)
{
    QString escaped = QString::fromStdString(value);
    escaped.replace('"', "\"\"");
    return u"\""_qs + escaped + u"\""_qs;
}

bool writeText(const QString &path, const QString &contents, QString *error = nullptr)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream output(&file);
    output << contents;
    if (output.status() != QTextStream::Ok || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

RuntimeAdaptiveResponseConfig runtimeForSettings(const AdaptiveResponseSettings &settings)
{
    const AdaptiveResponseSettings clean = sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = clean.enabled && clean.maximumHorizonMs > 0.0F;
    result.model = clean.model;
    result.maximumHorizonSeconds = clean.maximumHorizonMs / 1000.0F;
    result.maximumLead = clean.maximumLead;
    result.velocityResponse = clean.velocityResponse;
    result.accelerationResponse = clean.accelerationResponse;
    result.motionSensitivity = clean.motionSensitivity;
    result.noiseRejection = clean.noiseRejection;
    result.reversalDetection = clean.reversalDetection;
    result.reversalResponse = clean.reversalResponse;
    result.decelerationResponse = clean.decelerationResponse;
    result.settlingResponse = clean.settlingResponse;
    result.endpointTaper = clean.endpointTaper;
    result.onsetAssist = clean.onsetAssist;
    result.onsetCap = clean.onsetCap;
    result.sustainedAssist = clean.sustainedAssist;
    result.sustainedCap = clean.sustainedCap;
    result.horizonExtension = clean.horizonExtension;
    result.horizonExtensionCapSeconds = clean.horizonExtensionCapMs / 1000.0F;
    result.turningPointProtection = clean.turningPointProtection;
    result.turningPointMargin = clean.turningPointMargin;
    return result;
}

RuntimeAdaptiveResponseConfig verificationConfiguration(AdaptiveResponseModel model)
{
    RuntimeAdaptiveResponseConfig config;
    config.enabled = true;
    config.model = model;
    config.maximumHorizonSeconds = 0.030F;
    config.maximumLead = 0.20F;
    config.velocityResponse = 0.72F;
    config.accelerationResponse = 0.68F;
    config.motionSensitivity = 0.010F;
    config.noiseRejection = 0.012F;
    config.reversalDetection = 0.075F;
    config.reversalResponse = 1.0F;
    config.decelerationResponse = 0.85F;
    config.settlingResponse = 0.92F;
    return config;
}

std::string modelName(AdaptiveResponseModel model)
{
    switch (model) {
    case AdaptiveResponseModel::Velocity: return "Velocity";
    case AdaptiveResponseModel::AlphaBeta: return "Alpha-Beta";
    case AdaptiveResponseModel::AlphaBetaGamma: return "Alpha-Beta-Gamma";
    case AdaptiveResponseModel::Auto: return "Auto";
    }
    return "Auto";
}

std::vector<NamedConfiguration> productionConfigurations()
{
    std::vector<NamedConfiguration> result;
    RuntimeAdaptiveResponseConfig off = verificationConfiguration(AdaptiveResponseModel::Auto);
    off.enabled = false;
    result.push_back({"Off", off, "production"});
    RuntimeAdaptiveResponseConfig reference = verificationConfiguration(AdaptiveResponseModel::Auto);
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        const RuntimeAdaptiveResponseConfig runtime = runtimeForSettings(preset.axes[0].settings);
        if (!runtime.enabled) continue;
        if (preset.name == u"Light"_qs || preset.name == u"Balanced"_qs || preset.name == u"Fast"_qs
            || preset.name == u"Aggressive"_qs || preset.name == u"Extreme"_qs) {
            result.push_back({preset.name.toStdString(), runtime, "production"});
            if (preset.name == u"Balanced"_qs) reference = runtime;
        }
    }
    for (const AdaptiveResponseModel model : {AdaptiveResponseModel::Velocity, AdaptiveResponseModel::AlphaBeta,
                                               AdaptiveResponseModel::AlphaBetaGamma, AdaptiveResponseModel::Auto}) {
        RuntimeAdaptiveResponseConfig config = reference;
        config.enabled = true;
        config.model = model;
        result.push_back({modelName(model), config, "model-control"});
    }
    return result;
}

NamedConfiguration experimentalConfiguration(int horizonMs, double maximumLead)
{
    RuntimeAdaptiveResponseConfig config = verificationConfiguration(AdaptiveResponseModel::Auto);
    config.maximumHorizonSeconds = static_cast<float>(horizonMs) / 1000.0F;
    config.maximumLead = static_cast<float>(maximumLead);
    return {"Auto-H" + std::to_string(horizonMs) + "-L" + std::to_string(static_cast<int>(maximumLead * 100.0)), config,
            "experimental"};
}

NamedConfiguration extensionConfiguration(const NamedConfiguration &reference, int baseHorizonMs,
                                          int extensionCapMs)
{
    RuntimeAdaptiveResponseConfig config = reference.value;
    config.maximumHorizonSeconds = static_cast<float>(baseHorizonMs) / 1000.0F;
    config.horizonExtensionCapSeconds = static_cast<float>(extensionCapMs) / 1000.0F;
    return {"Balanced-BaseH" + std::to_string(baseHorizonMs) + "-ExtCap" + std::to_string(extensionCapMs),
            config, "phase1r-extension"};
}

ScenarioDefinition scenario(std::string id, std::string family, std::vector<ControlPoint> points,
                            int rate, std::uint32_t seed)
{
    ScenarioDefinition result;
    result.id = std::move(id);
    result.trajectoryId = result.id;
    result.family = std::move(family);
    result.points = std::move(points);
    result.durationSeconds = result.points.empty() ? 2.0F : result.points.back().timeSeconds;
    result.mapperRateHz = rate;
    result.sourceRateHz = rate;
    result.seed = seed;
    return result;
}

std::vector<ScenarioDefinition> coreScenarios(std::uint32_t seed, bool validation)
{
    const std::string tag = validation ? "validation/" : "characterization/";
    const std::vector<std::pair<std::string, std::vector<ControlPoint>>> templates{
        {"stationary-center", {{0,0},{2,0}}},
        {"stationary-off-center", {{0,.45F},{2,.45F}}},
        {"micro-corrections", {{0,.45F},{.25F,.46F},{.5F,.44F},{.75F,.47F},{1,.43F},{1.5F,.45F}}},
        {"slow-sweep", {{0,0},{3,.9F},{3.5F,.9F}}},
        {"moderate-sweep", {{0,-.7F},{.8F,.7F},{1.3F,.7F}}},
        {"ballistic-sweep", {{0,-.95F},{.12F,.95F},{.65F,.95F}}},
        {"sudden-stop", {{0,-.7F},{.20F,.8F},{1,.8F}}},
        {"same-side-reversal", {{0,.8F},{.25F,.2F},{.5F,.7F},{1,.7F}}},
        {"center-reversal", {{0,.9F},{.22F,-.9F},{.44F,.85F},{1,.85F}}},
        {"combat-oscillation", {{0,.7F},{.15F,-.6F},{.32F,.8F},{.50F,-.4F},{.72F,.5F},{1,.5F}}},
        {"endpoint-approach", {{0,.1F},{.5F,.8F},{.8F,.98F},{1.5F,.98F}}},
        {"compound-human", {{0,-.6F},{.25F,.7F},{.5F,.5F},{.7F,.5F},{.9F,.55F},{1.1F,.2F},{1.35F,.6F},{1.8F,.6F}}},
        {"timing-jitter", {{0,-.65F},{.8F,.65F},{1.4F,.65F}}},
    };
    std::vector<ScenarioDefinition> result;
    size_t index = 0;
    for (const int rate : {60, 125, 250, 500, 1000}) {
        for (const auto &[name, points] : templates) {
            ScenarioDefinition item = scenario(tag + name + "-" + std::to_string(rate), name == "timing-jitter" ? "cadence" : name,
                                               points, rate, deriveSeed(seed, tag, name, static_cast<std::uint32_t>(index++), "trajectory"));
            item.variableDt = name == "timing-jitter";
            result.push_back(std::move(item));
        }
    }
    for (const auto &[name, noise, amplitude] : std::vector<std::tuple<std::string, NoiseModel, float>>{
             {"stationary-white-noise", NoiseModel::WhiteJitter, .002F},
             {"stationary-quantized", NoiseModel::Quantized, .004F},
             {"stationary-drift", NoiseModel::Drift, .003F},
             {"moving-noise", NoiseModel::WhiteJitter, .002F},
             {"moving-spike", NoiseModel::OppositeSpike, .003F}}) {
        const bool moving = name.starts_with("moving");
        ScenarioDefinition item = scenario(tag + name, moving ? "noise-moving" : "noise",
            moving ? std::vector<ControlPoint>{{0,-.8F},{2,.8F},{2.5F,.8F}} : std::vector<ControlPoint>{{0,.3F},{2,.3F}},
            250, deriveSeed(seed, tag, name, static_cast<std::uint32_t>(index++), "trajectory"));
        item.noise = noise;
        item.noiseAmplitude = amplitude;
        result.push_back(std::move(item));
    }
    return result;
}

std::vector<ScenarioDefinition> randomizedScenarios(std::uint32_t seed, int countPerPersona)
{
    std::vector<ScenarioDefinition> result;
    size_t index = 0;
    for (const std::string &persona : {"precision-pilot", "combat-helicopter", "fixed-wing", "space-sim", "noisy-older-sensor"}) {
        for (int sample = 0; sample < countPerPersona; ++sample) {
            ScenarioDefinition item;
            item.id = "randomized/" + persona + "-" + std::to_string(sample);
            item.trajectoryId = item.id;
            item.family = "persona";
            item.durationSeconds = persona == "combat-helicopter" ? 3.0F : 5.0F;
            item.mapperRateHz = 250;
            item.sourceRateHz = 250;
            item.seed = deriveSeed(seed, "randomized", persona, static_cast<std::uint32_t>(index++), "trajectory");
            if (persona == "noisy-older-sensor") {
                item.noise = NoiseModel::Quantized;
                item.noiseAmplitude = .005F;
                item.variableDt = sample % 2 == 0;
            }
            result.push_back(std::move(item));
        }
    }
    return result;
}

QString corpusHash(const std::vector<ScenarioDefinition> &scenarios)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ScenarioDefinition &item : scenarios) {
        const std::vector<TraceSample> trace = generateTrace(item);
        hash.addData(item.id.data(), static_cast<qsizetype>(item.id.size()));
        for (const TraceSample &sample : trace) {
            hash.addData(reinterpret_cast<const char *>(&sample.timeSeconds), sizeof(sample.timeSeconds));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

std::optional<float> futureValue(const std::vector<TraceSample> &trace, float time)
{
    if (trace.empty() || time < trace.front().timeSeconds || time > trace.back().timeSeconds) return std::nullopt;
    const auto upper = std::lower_bound(trace.cbegin(), trace.cend(), time,
        [](const TraceSample &sample, float target) { return sample.timeSeconds < target; });
    if (upper == trace.cbegin()) return upper->physical;
    if (upper == trace.cend()) return trace.back().physical;
    const TraceSample &right = *upper;
    const TraceSample &left = *(upper - 1);
    const float span = right.timeSeconds - left.timeSeconds;
    if (span <= 0.0F) return left.physical;
    const float fraction = std::clamp((time - left.timeSeconds) / span, 0.0F, 1.0F);
    return left.physical + (right.physical - left.physical) * fraction;
}

RunRow analyze(const std::string &stage, const std::string &partition, const std::string &scenarioId,
               const std::string &family, int sourceRate, const NamedConfiguration &configuration,
               const std::vector<TraceSample> &trace, const std::vector<AdaptiveResponseTelemetry> &telemetry)
{
    RunRow row;
    row.stage = stage;
    row.partition = partition;
    row.scenarioId = scenarioId;
    row.family = family;
    row.configuration = configuration.name;
    row.configuredHorizonMs = static_cast<int>(std::lround(configuration.value.maximumHorizonSeconds * 1000.0F));
    row.configuredLead = configuration.value.maximumLead;
    row.sourceRateHz = sourceRate;
    row.samples = std::min(trace.size(), telemetry.size());
    row.durationMs = trace.empty() ? 0.0 : trace.back().timeSeconds * 1000.0;
    std::vector<double> errors, horizons, leads, stationaryLeads, onsetAuthorities, sustainedEvidence,
        sustainedAuthorities, extensions;
    double squared = 0.0, covariance = 0.0, predictedEnergy = 0.0, futureEnergy = 0.0;
    double continuousExtensionDwellMs = 0.0;
    double continuousClampDwellMs = 0.0;
    bool turningConstraintObserved = false;
    for (size_t i = 0; i < row.samples; ++i) {
        const auto future = futureValue(trace, trace[i].timeSeconds + telemetry[i].activeHorizonSeconds);
        if (future) {
            const double error = std::abs(static_cast<double>(telemetry[i].predicted - *future));
            errors.push_back(error);
            squared += error * error;
            covariance += static_cast<double>(telemetry[i].predicted) * *future;
            predictedEnergy += static_cast<double>(telemetry[i].predicted) * telemetry[i].predicted;
            futureEnergy += static_cast<double>(*future) * *future;
        }
        horizons.push_back(telemetry[i].activeHorizonSeconds * 1000.0);
        leads.push_back(std::abs(telemetry[i].lead));
        onsetAuthorities.push_back(telemetry[i].onsetAuthority);
        sustainedEvidence.push_back(telemetry[i].sustainedEvidence);
        sustainedAuthorities.push_back(telemetry[i].sustainedAuthority);
        const double extensionMs = std::max(0.0, static_cast<double>(telemetry[i].activeHorizonSeconds
            - telemetry[i].normalMaximumHorizonSeconds) * 1000.0);
        extensions.push_back(extensionMs);
        const double dtMs = std::max(0.0, static_cast<double>(trace[i].dtSeconds) * 1000.0);
        if (extensionMs > 0.001) {
            row.extensionDwellMs += dtMs;
            continuousExtensionDwellMs += dtMs;
            row.maximumContinuousExtensionDwellMs = std::max(row.maximumContinuousExtensionDwellMs,
                continuousExtensionDwellMs);
            if (telemetry[i].sourceBrakingDetected) row.extensionDuringBrakingMs += dtMs;
        } else {
            continuousExtensionDwellMs = 0.0;
        }
        const bool leadClamped = configuration.value.maximumLead > 0.0F
            && std::abs(telemetry[i].lead) >= configuration.value.maximumLead - 0.00001F;
        if (leadClamped) {
            row.leadClampDwellMs += dtMs;
            continuousClampDwellMs += dtMs;
            row.maximumContinuousLeadClampDwellMs = std::max(row.maximumContinuousLeadClampDwellMs,
                continuousClampDwellMs);
        } else {
            continuousClampDwellMs = 0.0;
        }
        row.peakTurningPointConfidence = std::max(row.peakTurningPointConfidence,
            static_cast<double>(telemetry[i].turningPointConfidence));
        if (telemetry[i].turningPointConfidence > 0.0001F) {
            const double horizonLimitMs = telemetry[i].turningPointHorizonLimitSeconds * 1000.0;
            if (!turningConstraintObserved) {
                row.minimumTurningHorizonLimitMs = horizonLimitMs;
                row.minimumTurningLeadLimit = telemetry[i].turningPointLeadLimit;
                turningConstraintObserved = true;
            } else {
                row.minimumTurningHorizonLimitMs = std::min(row.minimumTurningHorizonLimitMs, horizonLimitMs);
                row.minimumTurningLeadLimit = std::min(row.minimumTurningLeadLimit,
                    static_cast<double>(telemetry[i].turningPointLeadLimit));
            }
        }
        if (family == "noise" || family == "stationary-center" || family == "stationary-off-center") stationaryLeads.push_back(telemetry[i].lead);
        if (!std::isfinite(telemetry[i].predicted) || !std::isfinite(telemetry[i].lead)) ++row.nonFinite;
        if (telemetry[i].predicted < -1.0001F || telemetry[i].predicted > 1.0001F) ++row.illegalOutput;
        if (i > 0U) row.maximumOutputStep = std::max(row.maximumOutputStep,
            std::abs(static_cast<double>(telemetry[i].predicted - telemetry[i - 1U].predicted)
                     - static_cast<double>(trace[i].physical - trace[i - 1U].physical)));
    }
    if (!errors.empty()) {
        row.mae = std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();
        row.rmse = std::sqrt(squared / errors.size());
        row.p95 = percentile(errors, .95);
        row.p99 = percentile(errors, .99);
        row.peak = *std::max_element(errors.begin(), errors.end());
    }
    row.fidelity = predictedEnergy > 0.0 && futureEnergy > 0.0 ? covariance / std::sqrt(predictedEnergy * futureEnergy) : 1.0;
    row.meanHorizonMs = horizons.empty() ? 0.0 : std::accumulate(horizons.begin(), horizons.end(), 0.0) / horizons.size();
    row.p95HorizonMs = percentile(horizons, .95);
    row.peakHorizonMs = horizons.empty() ? 0.0 : *std::max_element(horizons.begin(), horizons.end());
    row.meanLead = leads.empty() ? 0.0 : std::accumulate(leads.begin(), leads.end(), 0.0) / leads.size();
    row.p95Lead = percentile(leads, .95);
    row.peakLead = leads.empty() ? 0.0 : *std::max_element(leads.begin(), leads.end());
    row.meanOnsetAuthority = onsetAuthorities.empty() ? 0.0
        : std::accumulate(onsetAuthorities.begin(), onsetAuthorities.end(), 0.0) / onsetAuthorities.size();
    row.peakOnsetAuthority = onsetAuthorities.empty() ? 0.0
        : *std::max_element(onsetAuthorities.begin(), onsetAuthorities.end());
    row.meanSustainedEvidence = sustainedEvidence.empty() ? 0.0
        : std::accumulate(sustainedEvidence.begin(), sustainedEvidence.end(), 0.0) / sustainedEvidence.size();
    row.meanSustainedAuthority = sustainedAuthorities.empty() ? 0.0
        : std::accumulate(sustainedAuthorities.begin(), sustainedAuthorities.end(), 0.0) / sustainedAuthorities.size();
    row.peakSustainedAuthority = sustainedAuthorities.empty() ? 0.0
        : *std::max_element(sustainedAuthorities.begin(), sustainedAuthorities.end());
    row.meanExtensionMs = extensions.empty() ? 0.0
        : std::accumulate(extensions.begin(), extensions.end(), 0.0) / extensions.size();
    row.peakExtensionMs = extensions.empty() ? 0.0 : *std::max_element(extensions.begin(), extensions.end());
    if (!stationaryLeads.empty()) {
        double sum = 0.0;
        for (const double lead : stationaryLeads) sum += lead * lead;
        row.stationaryLeadRms = std::sqrt(sum / stationaryLeads.size());
    }
    if (row.nonFinite || row.illegalOutput || row.stationaryDrift) row.consequence = "HARMFUL";
    else if (row.wrongDirectionLeadArea > .004 || (family == "noise" && row.stationaryLeadRms > .015)
             || row.maximumOutputStep > .12 || row.p95 > .12) row.consequence = "HARMFUL";
    else if (configuration.value.enabled && row.meanHorizonMs > .25 && row.mae < .035 && row.fidelity > .96) row.consequence = "BENEFICIAL";
    else row.consequence = "BENIGN";
    return row;
}

RunRow runSynthetic(const std::string &stage, const std::string &partition, const ScenarioDefinition &scenario,
                    const NamedConfiguration &configuration)
{
    const ScenarioResult replay = replayScenario(scenario, configuration.value, configuration.name, true);
    RunRow row = analyze(stage, partition, scenario.id, scenario.family, scenario.sourceRateHz, configuration,
                         replay.trace, replay.telemetry);
    row.wrongDirectionLeadArea = replay.metrics.wrongDirectionLeadArea;
    row.reversalReacquisitionMs = replay.metrics.oppositeDirectionReacquisitionMs;
    row.leadAtStop = replay.metrics.leadAtPhysicalStop;
    row.settlingMs = replay.metrics.settlingMs;
    row.falseReversals = replay.metrics.falseReversals;
    row.nonFinite += replay.metrics.nonFinite;
    row.illegalOutput += replay.metrics.illegalOutput;
    row.stationaryDrift += replay.metrics.stationaryDrift;
    row.noiseAmplification = replay.metrics.noiseAmplificationRatio;
    if (row.nonFinite || row.illegalOutput || row.stationaryDrift) row.consequence = "HARMFUL";
    return row;
}

bool parseFlag(const QString &field, bool *value)
{
    if (!value || (field != u"0"_qs && field != u"1"_qs)) return false;
    *value = field == u"1"_qs;
    return true;
}

std::optional<PhysicalTrace> readPhysicalTrace(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    PhysicalTrace trace;
    trace.id = QFileInfo(path).completeBaseName();
    trace.sourcePath = QFileInfo(path).absoluteFilePath();
    trace.sha256 = sha256File(path);
    QTextStream input(&file);
    bool headerSeen = false;
    std::uint64_t previous = 0;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            const int separator = line.indexOf('=');
            if (separator > 2) {
                const QString key = line.mid(2, separator - 2);
                const QString value = line.mid(separator + 1);
                if (key == u"capture_label"_qs) trace.id = value;
                else if (key == u"selected_axis_index"_qs) trace.axis = value.toInt();
                else if (key == u"selected_axis_label"_qs) trace.axisLabel = value;
            }
            continue;
        }
        if (!headerSeen) {
            if (line != u"timestamp_us,raw_normalized,calibrated_normalized,source_reported,source_changed,elapsed_us,effective_cadence_hz,raw_direct_input"_qs) {
                if (error) *error = "Unsupported physical trace header.";
                return std::nullopt;
            }
            headerSeen = true;
            continue;
        }
        const QStringList fields = line.split(',');
        bool timestampOk = false, physicalOk = false, cadenceOk = false;
        PhysicalSample sample;
        sample.timestampUs = fields.value(0).toULongLong(&timestampOk);
        sample.physical = fields.value(2).toFloat(&physicalOk);
        sample.cadenceHz = fields.value(6).toDouble(&cadenceOk);
        if (fields.size() != 8 || !timestampOk || !physicalOk || !cadenceOk || !parseFlag(fields[3], &sample.sourceReported)
            || !parseFlag(fields[4], &sample.sourceChanged) || sample.timestampUs <= previous
            || !std::isfinite(sample.physical) || std::abs(sample.physical) > 1.0001F) {
            if (error) *error = "Invalid physical trace sample.";
            return std::nullopt;
        }
        previous = sample.timestampUs;
        trace.samples.push_back(sample);
    }
    if (!headerSeen || trace.axis < 0 || trace.samples.size() < 2 || trace.sha256.isEmpty()) {
        if (error) *error = "Incomplete physical trace metadata or samples.";
        return std::nullopt;
    }
    return trace;
}

RunRow runPhysical(const PhysicalTrace &trace, const NamedConfiguration &configuration)
{
    std::vector<TraceSample> samples;
    std::vector<AdaptiveResponseTelemetry> telemetry;
    samples.reserve(trace.samples.size());
    telemetry.reserve(trace.samples.size());
    const std::uint64_t origin = trace.samples.front().timestampUs;
    AdaptiveResponseProcessor processor;
    const auto zero = std::chrono::steady_clock::time_point{};
    int previousDirection = 0;
    bool previousMoving = false;
    for (size_t index = 0; index < trace.samples.size(); ++index) {
        const PhysicalSample &raw = trace.samples[index];
        const float time = static_cast<float>(raw.timestampUs - origin) / 1000000.0F;
        const float dt = index + 1U < trace.samples.size()
            ? static_cast<float>(trace.samples[index + 1U].timestampUs - raw.timestampUs) / 1000000.0F : 0.0F;
        const float velocity = index == 0 ? 0.0F : (raw.physical - trace.samples[index - 1U].physical)
            / std::max(.000001F, static_cast<float>(raw.timestampUs - trace.samples[index - 1U].timestampUs) / 1000000.0F);
        const int direction = velocity > .00015F ? 1 : velocity < -.00015F ? -1 : 0;
        TraceSample sample;
        sample.timeSeconds = time;
        sample.intended = raw.physical;
        sample.physical = raw.physical;
        sample.velocity = velocity;
        sample.humanIntentVelocity = velocity;
        sample.humanIntentDirection = direction;
        sample.humanIntentReversal = direction != 0 && previousDirection != 0 && direction != previousDirection;
        sample.humanIntentStop = direction == 0 && previousMoving;
        sample.dtSeconds = dt;
        sample.intendedMoving = direction != 0;
        sample.sourceSampleUpdated = raw.sourceReported;
        sample.sourceSampleTimeSeconds = time;
        sample.physicalStop = sample.humanIntentStop;
        sample.trueReversal = sample.humanIntentReversal;
        samples.push_back(sample);
        telemetry.push_back(processor.process(raw.physical, configuration.value,
            zero + std::chrono::microseconds(raw.timestampUs - origin)));
        if (direction != 0) previousDirection = direction;
        previousMoving = direction != 0;
    }
    RunRow row = analyze("physical-replay", "physical", trace.id.toStdString(), "recorded-human", 0, configuration, samples, telemetry);
    row.sourceRateHz = static_cast<int>(std::lround(percentile([&trace] {
        std::vector<double> values; for (const PhysicalSample &item : trace.samples) if (item.cadenceHz > 0.0) values.push_back(item.cadenceHz); return values; }(), .50)));
    for (size_t index = 1; index < samples.size(); ++index) {
        const int prior = samples[index - 1].humanIntentDirection;
        const int current = samples[index].humanIntentDirection;
        if (prior != 0 && current != 0 && prior != current) {
            const float turnTime = samples[index].timeSeconds;
            bool reacquired = false;
            double staleArea = 0.0;
            for (size_t later = index; later < telemetry.size(); ++later) {
                const double interval = later + 1U < samples.size()
                    ? std::max(0.0F, samples[later + 1U].timeSeconds - samples[later].timeSeconds) : 0.0;
                if (telemetry[later].lead * current < -.00015F) staleArea += std::abs(telemetry[later].lead) * interval;
                if (telemetry[later].lead * current > .00015F) {
                    const double value = (samples[later].timeSeconds - turnTime) * 1000.0;
                    row.reversalReacquisitionMs = row.reversalReacquisitionMs < 0.0 ? value : std::max(row.reversalReacquisitionMs, value);
                    reacquired = true;
                    break;
                }
            }
            row.wrongDirectionLeadArea += staleArea;
        }
        if (samples[index].physicalStop) row.leadAtStop = std::max(row.leadAtStop, std::abs(static_cast<double>(telemetry[index].lead)));
    }
    if (row.nonFinite || row.illegalOutput || row.wrongDirectionLeadArea > .004 || row.maximumOutputStep > .12 || row.p95 > .12) row.consequence = "HARMFUL";
    else if (configuration.value.enabled && row.meanHorizonMs > .25 && row.mae < .035 && row.fidelity > .96) row.consequence = "BENEFICIAL";
    else row.consequence = "BENIGN";
    return row;
}

void writeRows(const QString &path, const std::vector<RunRow> &rows)
{
    QString contents;
    QTextStream out(&contents);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);
    out << "stage,partition,scenario_id,family,configuration,configured_horizon_ms,configured_maximum_lead,source_rate_hz,samples,duration_ms,mae,rmse,p95_error,p99_error,peak_error,fidelity,mean_active_horizon_ms,p95_active_horizon_ms,max_active_horizon_ms,mean_lead,p95_lead,max_lead,mean_onset_authority,peak_onset_authority,mean_sustained_evidence,mean_sustained_authority,peak_sustained_authority,mean_extension_ms,peak_extension_ms,extension_dwell_ms,max_continuous_extension_dwell_ms,extension_during_braking_ms,peak_turning_point_confidence,min_turning_horizon_limit_ms,min_turning_lead_limit,lead_clamp_dwell_ms,max_continuous_lead_clamp_dwell_ms,wrong_direction_lead_area,reversal_reacquisition_ms,lead_at_stop,settling_ms,stationary_lead_rms,noise_amplification,max_predictor_only_step,false_reversals,non_finite,illegal_output,stationary_drift,consequence\n";
    for (const RunRow &row : rows) {
        out << csv(row.stage) << ',' << csv(row.partition) << ',' << csv(row.scenarioId) << ',' << csv(row.family) << ','
            << csv(row.configuration) << ',' << row.configuredHorizonMs << ',' << row.configuredLead << ',' << row.sourceRateHz << ','
            << row.samples << ',' << row.durationMs << ',' << row.mae << ',' << row.rmse << ',' << row.p95 << ',' << row.p99 << ','
            << row.peak << ',' << row.fidelity << ',' << row.meanHorizonMs << ',' << row.p95HorizonMs << ',' << row.peakHorizonMs << ','
            << row.meanLead << ',' << row.p95Lead << ',' << row.peakLead << ','
            << row.meanOnsetAuthority << ',' << row.peakOnsetAuthority << ','
            << row.meanSustainedEvidence << ',' << row.meanSustainedAuthority << ',' << row.peakSustainedAuthority << ','
            << row.meanExtensionMs << ',' << row.peakExtensionMs << ',' << row.extensionDwellMs << ','
            << row.maximumContinuousExtensionDwellMs << ',' << row.extensionDuringBrakingMs << ','
            << row.peakTurningPointConfidence << ',' << row.minimumTurningHorizonLimitMs << ',' << row.minimumTurningLeadLimit << ','
            << row.leadClampDwellMs << ',' << row.maximumContinuousLeadClampDwellMs << ','
            << row.wrongDirectionLeadArea << ','
            << row.reversalReacquisitionMs << ',' << row.leadAtStop << ',' << row.settlingMs << ',' << row.stationaryLeadRms << ','
            << row.noiseAmplification << ',' << row.maximumOutputStep << ',' << row.falseReversals << ',' << row.nonFinite << ','
            << row.illegalOutput << ',' << row.stationaryDrift << ',' << csv(row.consequence) << '\n';
    }
    QString error;
    if (!writeText(path, contents, &error)) qFatal("Unable to write rows: %s", qPrintable(error));
}

Aggregate aggregateRows(const std::vector<RunRow> &rows)
{
    Aggregate result;
    for (const RunRow &row : rows) {
        ++result.rows;
        result.samples += row.samples;
        result.mae += row.mae;
        result.fidelity += row.fidelity;
        result.p95Horizon += row.p95HorizonMs;
        if (row.consequence == "BENEFICIAL") ++result.beneficial;
        else if (row.consequence == "HARMFUL") ++result.harmful;
        else ++result.benign;
        if (row.nonFinite || row.illegalOutput || row.stationaryDrift) ++result.hard;
    }
    if (result.rows) {
        result.mae /= result.rows;
        result.fidelity /= result.rows;
        result.p95Horizon /= result.rows;
    }
    return result;
}

void appendGrid(std::vector<RunRow> &destination, const std::string &stage, const std::string &partition,
                const std::vector<ScenarioDefinition> &scenarios, const std::vector<NamedConfiguration> &configurations)
{
    for (const ScenarioDefinition &item : scenarios) {
        for (const NamedConfiguration &config : configurations) destination.push_back(runSynthetic(stage, partition, item, config));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Offline V2.3.T Phase 1 frozen-model characterization. No DirectInput, vJoy, UI, or mapper process is opened."_qs);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty artifact directory."_qs, u"directory"_qs);
    const QCommandLineOption physicalOption(u"physical-corpus"_qs, u"Required directory containing exactly the 15 final real-human .physical.csv files."_qs, u"directory"_qs);
    parser.addOptions({outputOption, physicalOption});
    parser.process(application);
    const QString outputPath = parser.value(outputOption).trimmed();
    const QString physicalPath = parser.value(physicalOption).trimmed();
    if (outputPath.isEmpty() || physicalPath.isEmpty()) {
        QTextStream(stderr) << "--output and --physical-corpus are required.\n";
        return 2;
    }
    QDir output(outputPath);
    if (output.exists() && !output.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        QTextStream(stderr) << "Output directory must be empty to prevent evidence overwrite.\n";
        return 2;
    }
    if (!QDir().mkpath(outputPath)) {
        QTextStream(stderr) << "Unable to create output directory.\n";
        return 3;
    }
    const QFileInfoList physicalFiles = QDir(physicalPath).entryInfoList({u"*.physical.csv"_qs}, QDir::Files, QDir::Name);
    if (physicalFiles.size() != 15) {
        QTextStream(stderr) << "Physical corpus must contain exactly 15 final recorded source traces; found " << physicalFiles.size() << ".\n";
        return 3;
    }
    std::vector<PhysicalTrace> physical;
    for (const QFileInfo &file : physicalFiles) {
        QString error;
        const auto trace = readPhysicalTrace(file.absoluteFilePath(), &error);
        if (!trace) {
            QTextStream(stderr) << "Unable to read " << file.fileName() << ": " << error << '\n';
            return 3;
        }
        physical.push_back(*trace);
    }
    const std::vector<ScenarioDefinition> characterization = coreScenarios(kCharacterizationSeed, false);
    const std::vector<ScenarioDefinition> validation = coreScenarios(kValidationSeed, true);
    const std::vector<ScenarioDefinition> random = randomizedScenarios(kCharacterizationSeed, 20);
    const std::vector<ScenarioDefinition> holdout = randomizedScenarios(kHoldoutSeed, 20);
    const QString characterizationHash = corpusHash(characterization);
    const QString validationHash = corpusHash(validation);
    const QString holdoutHash = corpusHash(holdout);
    const std::vector<NamedConfiguration> production = productionConfigurations();
    if (production.size() != 10) {
        QTextStream(stderr) << "Expected Off, five production presets, and four model controls; got " << production.size() << ".\n";
        return 3;
    }
    const auto balanced = std::find_if(production.cbegin(), production.cend(), [](const NamedConfiguration &item) {
        return item.name == "Balanced";
    });
    if (balanced == production.cend()) {
        QTextStream(stderr) << "Balanced production preset is unavailable for the Phase 1R extension matrix.\n";
        return 3;
    }
    std::vector<RunRow> coarse, focused, randomized, physicalRows, authorityRows;
    std::vector<NamedConfiguration> coarseConfigurations;
    for (const int horizon : {0, 2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40, 50}) coarseConfigurations.push_back(experimentalConfiguration(horizon, .20));
    appendGrid(coarse, "coarse-horizon-rate", "characterization", characterization, coarseConfigurations);
    appendGrid(focused, "production-preset-baseline", "characterization", characterization, production);
    std::vector<ScenarioDefinition> focusedScenarios;
    for (const ScenarioDefinition &item : characterization) {
        if (item.sourceRateHz == 250 && (item.family.find("reversal") != std::string::npos || item.family == "sudden-stop"
            || item.family == "noise" || item.family == "micro-corrections" || item.family == "ballistic-sweep")) focusedScenarios.push_back(item);
    }
    std::vector<NamedConfiguration> leadConfigurations;
    for (const int horizon : {12, 24, 30, 35, 40}) for (const double lead : {.02, .04, .06, .08, .10, .12, .15, .20}) leadConfigurations.push_back(experimentalConfiguration(horizon, lead));
    appendGrid(focused, "focused-horizon-lead", "characterization", focusedScenarios, leadConfigurations);
    std::vector<NamedConfiguration> modelConfigurations;
    for (const int horizon : {8, 18, 30}) {
        for (const AdaptiveResponseModel model : {AdaptiveResponseModel::Velocity, AdaptiveResponseModel::AlphaBeta,
                                                   AdaptiveResponseModel::AlphaBetaGamma, AdaptiveResponseModel::Auto}) {
            NamedConfiguration config = experimentalConfiguration(horizon, .12);
            config.name = modelName(model) + "-H" + std::to_string(horizon);
            config.value.model = model;
            modelConfigurations.push_back(std::move(config));
        }
    }
    appendGrid(focused, "predictor-model-comparison", "characterization", focusedScenarios, modelConfigurations);
    appendGrid(randomized, "randomized-human", "characterization", random, production);
    std::vector<NamedConfiguration> validationConfigurations;
    for (const int horizon : {8, 18, 24, 30, 35, 40}) validationConfigurations.push_back(experimentalConfiguration(horizon, .20));
    appendGrid(focused, "independent-validation", "validation", validation, validationConfigurations);
    std::vector<NamedConfiguration> extensionConfigurations;
    for (const int baseHorizon : {0, 4, 12, 20, 30}) {
        for (const int extensionCap : {0, 4, 8, 12, 18, 24, 30}) {
            extensionConfigurations.push_back(extensionConfiguration(*balanced, baseHorizon, extensionCap));
        }
    }
    appendGrid(authorityRows, "phase1r-extension-matrix", "characterization", characterization,
        extensionConfigurations);
    std::vector<NamedConfiguration> physicalConfigurations = production;
    for (const int horizon : {0, 2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40, 50}) physicalConfigurations.push_back(experimentalConfiguration(horizon, .20));
    for (const int horizon : {18, 30}) for (const double lead : {.02, .04, .06, .08, .10, .12, .15, .20}) physicalConfigurations.push_back(experimentalConfiguration(horizon, lead));
    for (const PhysicalTrace &trace : physical) for (const NamedConfiguration &config : physicalConfigurations) physicalRows.push_back(runPhysical(trace, config));
    for (const PhysicalTrace &trace : physical) {
        for (const NamedConfiguration &config : extensionConfigurations) authorityRows.push_back(runPhysical(trace, config));
    }
    writeRows(output.filePath(u"coarse_scan.csv"_qs), coarse);
    writeRows(output.filePath(u"focused_scan.csv"_qs), focused);
    writeRows(output.filePath(u"randomized_corpus_summary.csv"_qs), randomized);
    writeRows(output.filePath(u"physical_per_trace_results.csv"_qs), physicalRows);
    writeRows(output.filePath(u"new_math_authority_metrics.csv"_qs), authorityRows);
    std::vector<RunRow> all = coarse;
    all.insert(all.end(), focused.begin(), focused.end());
    all.insert(all.end(), randomized.begin(), randomized.end());
    all.insert(all.end(), physicalRows.begin(), physicalRows.end());
    all.insert(all.end(), authorityRows.begin(), authorityRows.end());
    writeRows(output.filePath(u"all_results.csv"_qs), all);
    QString traceManifest;
    QTextStream traces(&traceManifest);
    traces << "trace_id,source_path,sha256,axis_index,axis_label,samples,duration_ms,median_cadence_hz,min_position,max_position\n";
    std::uint64_t physicalSamples = 0;
    for (const PhysicalTrace &trace : physical) {
        std::vector<double> cadence;
        float minimum = std::numeric_limits<float>::infinity(), maximum = -std::numeric_limits<float>::infinity();
        for (const PhysicalSample &sample : trace.samples) { if (sample.cadenceHz > 0.0) cadence.push_back(sample.cadenceHz); minimum = std::min(minimum, sample.physical); maximum = std::max(maximum, sample.physical); }
        const double duration = static_cast<double>(trace.samples.back().timestampUs - trace.samples.front().timestampUs) / 1000.0;
        traces << csv(trace.id.toStdString()) << ',' << csv(trace.sourcePath.toStdString()) << ',' << trace.sha256 << ',' << trace.axis << ','
               << csv(trace.axisLabel.toStdString()) << ',' << trace.samples.size() << ',' << duration << ',' << percentile(cadence, .50) << ',' << minimum << ',' << maximum << '\n';
        physicalSamples += trace.samples.size();
    }
    QString error;
    if (!writeText(output.filePath(u"physical_trace_manifest.csv"_qs), traceManifest, &error)) qFatal("Unable to write physical manifest: %s", qPrintable(error));
    const Aggregate total = aggregateRows(all);
    const Aggregate physicalAggregate = aggregateRows(physicalRows);
    const QString manifest = QStringLiteral("{\n  \"schema\": 1,\n  \"freeze_sha\": \"") + QStringLiteral(HOTAS_VERIFICATION_SOURCE_COMMIT)
        + QStringLiteral("\",\n  \"harness_commit\": \"") + QStringLiteral(HOTAS_VERIFICATION_HARNESS_COMMIT)
        + QStringLiteral("\",\n  \"characterization_seed\": \"0x23A10001\",\n  \"validation_seed\": \"0x23A10002\",\n  \"holdout_seed\": \"0x23A10003\",\n  \"characterization_hash\": \"") + characterizationHash
        + QStringLiteral("\",\n  \"validation_hash\": \"") + validationHash + QStringLiteral("\",\n  \"holdout_hash\": \"") + holdoutHash
        + QStringLiteral("\",\n  \"physical_unique_source_traces\": 15,\n  \"physical_source_samples\": ") + QString::number(physicalSamples)
        + QStringLiteral(",\n  \"physical_replay_executions\": ") + QString::number(physicalRows.size()) + QStringLiteral("\n}\n");
    if (!writeText(output.filePath(u"manifest.json"_qs), manifest, &error)) qFatal("Unable to write manifest: %s", qPrintable(error));
    const QString environment = QStringLiteral("{\n  \"compiler\": \"MSVC 19.44.35228\",\n  \"qt\": \"6.8.3 msvc2022_64\",\n  \"mode\": \"Release\",\n  \"physical_hardware_required\": false\n}\n");
    if (!writeText(output.filePath(u"environment.json"_qs), environment, &error)) qFatal("Unable to write environment: %s", qPrintable(error));
    QString report;
    QTextStream summary(&report);
    summary.setRealNumberNotation(QTextStream::FixedNotation);
    summary.setRealNumberPrecision(6);
    summary << "# V2.3.T Monster Phase 1 — Machine Summary\n\n"
            << "- Frozen source SHA: `" << HOTAS_VERIFICATION_SOURCE_COMMIT << "`\n"
            << "- Characterization corpus: " << characterization.size() << " scenarios, hash `" << characterizationHash << "`\n"
            << "- Independent validation corpus: " << validation.size() << " scenarios, hash `" << validationHash << "`\n"
            << "- Untouched holdout corpus: " << holdout.size() << " scenarios, hash `" << holdoutHash << "` (generated only; not replayed)\n"
            << "- Physical corpus: **15 unique recorded real-human HOTAS source traces**, " << physicalSamples << " source samples, " << physicalRows.size() << " replay executions.\n"
            << "- Total rows: " << total.rows << "; total processed samples: " << total.samples << "; hard correctness rows: " << total.hard << ".\n"
            << "- Consequences: " << total.beneficial << " BENEFICIAL, " << total.benign << " BENIGN, " << total.harmful << " HARMFUL.\n"
            << "- Physical aggregate: " << physicalAggregate.beneficial << " BENEFICIAL, " << physicalAggregate.benign << " BENIGN, " << physicalAggregate.harmful << " HARMFUL.\n\n"
            << "The CSV evidence retains every configuration/trace identity. HARMFUL is an evidence classification, not a product repair authorization.\n";
    if (!writeText(output.filePath(u"Monster_Phase1_Machine_Summary.md"_qs), report, &error)) qFatal("Unable to write summary: %s", qPrintable(error));
    QTextStream(stdout) << "Phase 1 offline characterization complete: rows=" << total.rows << " samples=" << total.samples
                        << " physical_sources=15 physical_replays=" << physicalRows.size() << " hard_rows=" << total.hard
                        << " output=" << outputPath << '\n';
    return total.hard == 0 ? 0 : 5;
}
