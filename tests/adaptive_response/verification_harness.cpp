#include "verification_harness.h"

#include "automation_engine.h"
#include "axis_mapping_transition.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <numbers>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>

namespace hotas::verification {
namespace {

using Clock = std::chrono::steady_clock;

struct NamedConfiguration {
    std::string name;
    RuntimeAdaptiveResponseConfig value;
    bool preset = false;
};

struct TimedResult {
    ScenarioResult result;
    RuntimeAdaptiveResponseConfig configuration;
    double elapsedMicroseconds = 0.0;
};

struct CampaignOptions {
    std::string tier = "smoke";
    std::uint32_t masterSeed = kDefaultMasterSeed;
    std::string scenarioFilter;
    std::string modelFilter = "all";
    int sampleRateFilter = 0;
    int randomCountOverride = -1;
    int jobs = 1;
    QString outputDirectory;
    QString baselineDirectory;
    QString candidateDirectory;
};

struct Aggregate {
    std::uint64_t scenarios = 0;
    std::uint64_t samples = 0;
    std::uint64_t hardFailures = 0;
    std::uint64_t behavioralFailures = 0;
    std::uint64_t findings = 0;
    std::uint64_t dropouts = 0;
    std::uint64_t falseReversals = 0;
    double leadSquared = 0.0;
    double peakLead = 0.0;
    double totalDurationSeconds = 0.0;
};

constexpr float kMotionVelocityThreshold = 0.002F;
constexpr float kGroundTruthReversalVelocityThreshold = 0.020F;
constexpr float kLeadTolerance = 0.00015F;
constexpr int kPhysicalReversalCoherenceSamples = 2;
constexpr float kPhysicalDirectionResetSeconds = 0.100F;
constexpr size_t kWorstCasesPerMetric = 10;
constexpr size_t kFailureTracesPerCategory = 10;

const char *reversalQualityName(ReversalQuality quality)
{
    switch (quality) {
    case ReversalQuality::NotApplicable: return "not-applicable";
    case ReversalQuality::Immediate: return "immediate";
    case ReversalQuality::Excellent: return "excellent";
    case ReversalQuality::Acceptable: return "acceptable";
    case ReversalQuality::Poor: return "poor";
    case ReversalQuality::Failure: return "failure";
    }
    return "failure";
}

ReversalQuality classifyReversalQuality(float latencySeconds, float mapperPeriodSeconds, float sourcePeriodSeconds)
{
    if (latencySeconds < 0.0F) return ReversalQuality::Failure;
    const float immediate = std::max(mapperPeriodSeconds * 1.25F, 0.001F);
    const float excellent = std::max(0.020F, sourcePeriodSeconds * 1.5F);
    const float acceptable = std::max(0.060F, sourcePeriodSeconds * 3.0F);
    const float poor = std::max(0.120F, sourcePeriodSeconds * 5.0F);
    if (latencySeconds <= immediate) return ReversalQuality::Immediate;
    if (latencySeconds <= excellent) return ReversalQuality::Excellent;
    if (latencySeconds <= acceptable) return ReversalQuality::Acceptable;
    if (latencySeconds <= poor) return ReversalQuality::Poor;
    return ReversalQuality::Failure;
}

struct WorstMetric {
    const char *name;
    double (*score)(const ScenarioResult &result);
};

double wrongDirectionLeadScore(const ScenarioResult &result) { return result.metrics.wrongDirectionLeadArea; }
double peakLeadScore(const ScenarioResult &result) { return result.metrics.peakLead; }
double noiseAmplificationScore(const ScenarioResult &result)
{
    // The ratio is meaningful only where a declared injected-noise source
    // exists; otherwise a near-zero numerical residual would dominate a
    // forensic ranking without measuring noise amplification.
    return result.scenario.noise == NoiseModel::None ? 0.0 : result.metrics.noiseAmplificationRatio;
}
double dropoutDurationScore(const ScenarioResult &result) { return result.metrics.longestDropoutMs; }
double falseStopDurationScore(const ScenarioResult &result) { return result.metrics.falseStopTotalMs; }
double settlingScore(const ScenarioResult &result) { return std::max(0.0, result.metrics.settlingMs); }
double overshootScore(const ScenarioResult &result) { return result.metrics.targetOvershootPeak; }
double outputStepScore(const ScenarioResult &result) { return result.metrics.maximumOutputStep; }

const std::array<WorstMetric, 8> kWorstMetrics{{
    {"wrong_direction_lead_area", wrongDirectionLeadScore},
    {"peak_lead", peakLeadScore},
    {"noise_amplification_ratio", noiseAmplificationScore},
    {"longest_dropout_ms", dropoutDurationScore},
    {"false_stop_total_ms", falseStopDurationScore},
    {"settling_ms", settlingScore},
    {"target_overshoot_peak", overshootScore},
    {"maximum_output_step", outputStepScore},
}};

struct FindingTriage {
    const char *category;
    const char *rootCause;
    const char *nextAction;
};

bool hasSubstantiveConveyedPredictorMiss(const ScenarioResult &result)
{
    return std::any_of(result.reversalEvents.cbegin(), result.reversalEvents.cend(), [](const ReversalEvent &event) {
        return event.conveysHumanIntent && event.predictorMissed
            && std::abs(event.humanIntentVelocity) >= 0.075F;
    });
}

bool satisfiesSlowMotionAcceptance(const ScenarioResult &result)
{
    // Slow motion is accepted when the coherent command is recognized, output
    // remains stable/direct, and state is not chattering.  It intentionally
    // does not require a non-zero predictive horizon at every microscopic
    // speed, especially on 5 s and 10 s sweeps.
    return result.metrics.longestHumanIntentMotionMs >= 2500.0
        && result.metrics.motionRecognitionLatencyMs >= 0.0
        && result.metrics.motionRecognitionLatencyMs <= 750.0
        && result.metrics.stableChatter <= 1
        && result.metrics.dropouts <= 1
        && result.metrics.falseReversals == 0
        && result.metrics.maximumOutputStep <= 0.060;
}

FindingTriage classifyFinding(const ScenarioResult &result, const std::string &reason)
{
    const std::string &family = result.scenario.family;
    const bool reversalTiming = reason.find("reversal timing") != std::string::npos;
    const bool stateOnly = reason.find("false Stable") != std::string::npos
        || reason.find("dropout") != std::string::npos;
    const bool substantiveMiss = hasSubstantiveConveyedPredictorMiss(result);
    if (reason.rfind("HARD: integration", 0) == 0 || reason.rfind("BEHAVIOR: integration", 0) == 0) {
        return {"HARNESS", "production-backed integration acceptance invariant", "Correct the failing integration invariant before any campaign escalation."};
    }
    if (family == "noise" || family == "noise-moving") {
        return {"EXPECTED", "deliberate injected-noise stress", "Retain as noise robustness evidence; do not tune from this branch."};
    }
    if (family == "human-wobble") {
        return {"EXPECTED", "deliberate human-wobble stress", "Use the timing grades to select a later UX/product review case."};
    }
    if (family == "precision") {
        return {"HARNESS", "micro-motion event was below the substantive control-quality policy", "Keep precision data as an observability sentinel; do not label it a predictor defect without a coherent control threshold."};
    }
    if (family == "reversal" && (reversalTiming || reason.find("false reversal") != std::string::npos) && substantiveMiss) {
        return {"PRODUCT_SUSPECT", "human reversal was conveyed by the device but not detected by the predictor", "Preserve the exact replay and evaluate the reversal-evidence threshold on the product branch."};
    }
    if (reversalTiming && !substantiveMiss) {
        return {"HARNESS", "timing grade was not attributable to a substantive conveyed human reversal", "Retain the event-chain evidence; do not promote a device-only or micro-intent event to a product defect."};
    }
    if (family == "slow-motion") {
        if (satisfiesSlowMotionAcceptance(result)) {
            return {"EXPECTED", "coherent slow motion remained direct and stable within the explicit low-speed policy", "Retain the 3 s, 5 s, and 10 s policy tables as regression evidence."};
        }
        return {"UNKNOWN", "slow-motion acceptance policy was not met", "Inspect the policy metric cluster before opening a product correction."};
    }
    if (family == "oscillation") {
        return {"HARMLESS", "oscillatory stress outlier", "Keep as a bounded oscillation regression sentinel."};
    }
    if (family == "persona") {
        if (result.scenario.noise != NoiseModel::None) {
            return {"EXPECTED", "persona includes declared device quantization/noise", "Keep sensor-simulation findings separate from human-intent predictor findings."};
        }
        if ((reason.find("false reversal") != std::string::npos || reason.find("timing was poor") != std::string::npos)
            && result.metrics.wrongDirectionLeadArea <= 0.004) {
            return {"HARMLESS", "persona predictor event had no material wrong-direction virtual-output lead", "Retain as a telemetry-state sentinel, not a flight-control defect."};
        }
        if (substantiveMiss) {
            return {"PRODUCT_SUSPECT", "persona conveyed a substantive human reversal that the predictor did not detect", "Preserve the persona replay and assess the reversal-evidence threshold on the product branch."};
        }
        if (stateOnly && result.metrics.wrongDirectionLeadArea <= 0.004) {
            return {"HARMLESS", "persona state label changed without a wrong-direction virtual-output symptom", "Retain as state-classification telemetry rather than a flight-control defect."};
        }
        return {"UNKNOWN", "persona evidence does not yet isolate a predictor or device cause", "Use the intent/device/predictor event chain in the retained trace before escalation."};
    }
    return {"UNKNOWN", "unclassified synthetic behavior", "Review the retained trace and classify before escalation."};
}

QString traceReference(const ScenarioResult &result)
{
    if (result.trace.empty() || !(result.scenario.retainTrace || result.retainedForWorstCase || result.retainedForFailure)) {
        return QStringLiteral("summary-only");
    }
    const QString traceName = QString::number(static_cast<qulonglong>(
        qHash(QString::fromStdString(result.scenario.id + "|" + result.configuration))), 16)
        + "-" + QString::fromStdString(result.configuration).replace(' ', '_') + ".csv";
    return QStringLiteral("traces/") + traceName;
}

double reversalSeverityScore(const ReversalEvent &event)
{
    const auto latency = [&event](float timestamp) {
        if (timestamp < 0.0F) return 1000000.0;
        const double milliseconds = (timestamp - event.groundTruthTimeSeconds) * 1000.0;
        return std::isfinite(milliseconds) ? std::max(0.0, milliseconds) : 1000000.0;
    };
    return std::max(latency(event.detectedTimeSeconds), latency(event.reacquisitionTimeSeconds));
}

QString q(const std::string &value) { return QString::fromStdString(value); }

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
    return result;
}

RuntimeAdaptiveResponseConfig verificationConfiguration(AdaptiveResponseModel model)
{
    RuntimeAdaptiveResponseConfig configuration;
    configuration.enabled = true;
    configuration.model = model;
    configuration.maximumHorizonSeconds = 0.030F;
    configuration.maximumLead = 0.40F;
    configuration.velocityResponse = 0.72F;
    configuration.accelerationResponse = 0.68F;
    configuration.motionSensitivity = 0.010F;
    configuration.noiseRejection = 0.012F;
    configuration.reversalDetection = 0.075F;
    configuration.reversalResponse = 0.0F;
    return configuration;
}

std::vector<NamedConfiguration> configurationsFor(const CampaignOptions &options)
{
    std::vector<NamedConfiguration> configurations;
    const auto include = [&options](const std::string &name) {
        return options.modelFilter == "all" || options.modelFilter == name
            || (options.modelFilter == "off" && name == "Off")
            || (options.modelFilter == "velocity" && name == "Velocity")
            || (options.modelFilter == "alpha-beta" && name == "Alpha-Beta")
            || (options.modelFilter == "alpha-beta-gamma" && name == "Alpha-Beta-Gamma")
            || (options.modelFilter == "auto" && name == "Auto");
    };
    RuntimeAdaptiveResponseConfig off = verificationConfiguration(AdaptiveResponseModel::Auto);
    off.enabled = false;
    if (include("Off")) configurations.push_back({"Off", off});
    for (const AdaptiveResponseModel model : {AdaptiveResponseModel::Velocity, AdaptiveResponseModel::AlphaBeta,
                                               AdaptiveResponseModel::AlphaBetaGamma, AdaptiveResponseModel::Auto}) {
        const std::string name = modelName(model);
        if (include(name)) configurations.push_back({name, verificationConfiguration(model)});
    }
    if (options.modelFilter == "all" || options.modelFilter == "presets") {
        for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
            configurations.push_back({"Preset " + preset.name.toStdString(),
                runtimeForSettings(preset.axes[0].settings), true});
        }
    }
    return configurations;
}

bool isHardFailure(const std::string &failure)
{
    return failure.rfind("HARD:", 0) == 0;
}

bool isBehavioralFailure(const std::string &failure)
{
    return failure.rfind("BEHAVIOR:", 0) == 0;
}

std::string csv(const std::string &value)
{
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\"') result += "\"\"";
        else result += character;
    }
    result += "\"";
    return result;
}

bool writeText(const QString &path, const QString &contents, QString *error = nullptr)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(contents.toUtf8()) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QJsonObject metricsJson(const ScenarioMetrics &metrics)
{
    const auto qualityCounts = [](const std::array<std::uint64_t, 6> &counts) {
        QJsonArray result;
        for (const std::uint64_t count : counts) result.append(static_cast<qint64>(count));
        return result;
    };
    return {
        {"samples", static_cast<qint64>(metrics.samples)},
        {"humanIntentReversals", static_cast<qint64>(metrics.humanIntentReversals)},
        {"observedDeviceReversals", static_cast<qint64>(metrics.observedDeviceReversals)},
        {"intentReversalsConveyed", static_cast<qint64>(metrics.intentReversalsConveyed)},
        {"intentReversalsNotConveyed", static_cast<qint64>(metrics.intentReversalsNotConveyed)},
        {"observedDeviceReversalsWithoutIntent", static_cast<qint64>(metrics.observedDeviceReversalsWithoutIntent)},
        {"predictorMissedObservedReversals", static_cast<qint64>(metrics.predictorMissedObservedReversals)},
        {"trueReversals", static_cast<qint64>(metrics.trueReversals)},
        {"detectedReversals", static_cast<qint64>(metrics.detectedReversals)},
        {"falseReversals", static_cast<qint64>(metrics.falseReversals)},
        {"missedReversals", static_cast<qint64>(metrics.missedReversals)},
        {"dropouts", static_cast<qint64>(metrics.dropouts)},
        {"falseStops", static_cast<qint64>(metrics.falseStops)},
        {"stableChatter", static_cast<qint64>(metrics.stableChatter)},
        {"nonFinite", static_cast<qint64>(metrics.nonFinite)},
        {"illegalOutput", static_cast<qint64>(metrics.illegalOutput)},
        {"stationaryDrift", static_cast<qint64>(metrics.stationaryDrift)},
        {"rmsLead", metrics.rmsLead}, {"meanLead", metrics.meanLead}, {"medianLead", metrics.medianLead},
        {"p95Lead", metrics.p95Lead}, {"peakLead", metrics.peakLead},
        {"meanHorizonMs", metrics.meanHorizonMs}, {"medianHorizonMs", metrics.medianHorizonMs},
        {"p95HorizonMs", metrics.p95HorizonMs}, {"peakHorizonMs", metrics.peakHorizonMs},
        {"meanConfidence", metrics.meanConfidence}, {"wrongDirectionLeadArea", metrics.wrongDirectionLeadArea},
        {"noiseRms", metrics.noiseRms}, {"predictedNoiseRms", metrics.predictedNoiseRms},
        {"noiseAmplificationRatio", metrics.noiseAmplificationRatio},
        {"dropoutTotalMs", metrics.dropoutTotalMs}, {"longestDropoutMs", metrics.longestDropoutMs},
        {"falseStopTotalMs", metrics.falseStopTotalMs}, {"stationaryLeadRms", metrics.stationaryLeadRms},
        {"stationaryLeadPeak", metrics.stationaryLeadPeak},
        {"maximumOutputStep", metrics.maximumOutputStep}, {"stopRecognitionMs", metrics.stopRecognitionMs},
        {"settlingMs", metrics.settlingMs}, {"reversalLatencyMs", metrics.reversalLatencyMs},
        {"staleLeadCancellationMs", metrics.staleLeadCancellationMs},
        {"oppositeDirectionReacquisitionMs", metrics.oppositeDirectionReacquisitionMs},
        {"targetOvershootPeak", metrics.targetOvershootPeak}, {"targetOvershootArea", metrics.targetOvershootArea},
        {"sourceUpdateCount", static_cast<qint64>(metrics.sourceUpdateCount)},
        {"effectiveSourceRateHz", metrics.effectiveSourceRateHz}, {"sourceCadenceErrorMs", metrics.sourceCadenceErrorMs},
        {"humanIntentMotionDurationMs", metrics.humanIntentMotionDurationMs},
        {"longestHumanIntentMotionMs", metrics.longestHumanIntentMotionMs},
        {"meanHumanIntentSpeed", metrics.meanHumanIntentSpeed},
        {"peakHumanIntentSpeed", metrics.peakHumanIntentSpeed},
        {"reversalDetectionQuality", qualityCounts(metrics.reversalDetectionQuality)},
        {"reversalReacquisitionQuality", qualityCounts(metrics.reversalReacquisitionQuality)},
    };
}

void addAggregate(Aggregate &aggregate, const ScenarioResult &result)
{
    ++aggregate.scenarios;
    aggregate.samples += result.metrics.samples;
    aggregate.dropouts += result.metrics.dropouts;
    aggregate.falseReversals += result.metrics.falseReversals;
    aggregate.leadSquared += result.metrics.rmsLead * result.metrics.rmsLead * result.metrics.samples;
    aggregate.peakLead = std::max(aggregate.peakLead, result.metrics.peakLead);
    aggregate.totalDurationSeconds += result.scenario.durationSeconds;
    for (const std::string &failure : result.failures) {
        if (isHardFailure(failure)) ++aggregate.hardFailures;
        else if (isBehavioralFailure(failure)) ++aggregate.behavioralFailures;
        else ++aggregate.findings;
    }
}

std::vector<ScenarioDefinition> campaignScenarios(const CampaignOptions &options)
{
    const std::vector<ScenarioDefinition> catalog = canonicalScenarioCatalog(options.masterSeed);
    std::vector<ScenarioDefinition> result;
    const auto wanted = [&options](const ScenarioDefinition &scenario) {
        return options.scenarioFilter.empty() || scenario.id.find(options.scenarioFilter) != std::string::npos;
    };
    const auto add = [&result, &wanted, &options](ScenarioDefinition scenario) {
        if (wanted(scenario) && (options.sampleRateFilter == 0 || scenario.mapperRateHz == options.sampleRateFilter)) {
            result.push_back(std::move(scenario));
        }
    };
    if (options.tier == "smoke") {
        const std::vector<std::string> required{
            "stationary/center", "slow/sweep-3.000000-1", "slow/sweep-5.000000-1",
            "sample-hold/mapper-250-source-60", "sample-hold/mapper-250-source-30",
            "reversal/pattern-", "reversal/false-bait", "stop/at-0.000000", "noise/model-1",
            "noise-moving/slow-sweep", "timing/variable-dt", "one-sided/throttle-3.000000",
            "human-wobble/slow3s-a0.002000-f10.000000"};
        for (const std::string &identity : required) {
            const auto found = std::find_if(catalog.cbegin(), catalog.cend(), [&identity](const ScenarioDefinition &scenario) {
                return scenario.id.find(identity) != std::string::npos;
            });
            if (found != catalog.cend()) add(*found);
        }
        return result;
    }
    for (const ScenarioDefinition &scenario : catalog) {
        if (options.tier == "canonical" && scenario.family == "randomized") continue;
        add(scenario);
    }
    const int defaultRandomCount = options.tier == "full" ? 50000 : options.tier == "torture" ? 5000 : 0;
    const int randomCount = options.randomCountOverride >= 0 ? options.randomCountOverride : defaultRandomCount;
    const std::array<std::string, 6> personas{"precision-pilot", "fixed-wing", "helicopter-landing", "combat-helicopter",
        "space-sim", "noisy-older-sensor"};
    for (int index = 0; index < randomCount; ++index) {
        ScenarioDefinition scenario;
        const bool adversarial = index % 5 == 0;
        if (adversarial) {
            scenario.id = "adversarial-piecewise/combat-helicopter-" + std::to_string(index);
            scenario.family = "adversarial-piecewise";
            scenario.adversarialPiecewise = true;
            scenario.durationSeconds = 3.0F;
        } else {
            const std::string &persona = personas[static_cast<size_t>(index) % personas.size()];
            scenario.id = "persona/" + persona + "-generated-" + std::to_string(index);
            scenario.family = "persona";
            scenario.durationSeconds = persona == "combat-helicopter" ? 3.0F : 5.0F;
            scenario.noise = persona == "noisy-older-sensor" ? NoiseModel::Quantized : NoiseModel::None;
            scenario.noiseAmplitude = persona == "noisy-older-sensor" ? 0.005F : 0.0F;
        }
        scenario.seed = deriveSeed(options.masterSeed, options.tier, scenario.family, static_cast<std::uint32_t>(index), "trajectory");
        scenario.retainTrace = index < 2;
        add(std::move(scenario));
    }
    std::vector<ScenarioDefinition> rateMatrix;
    for (const ScenarioDefinition &scenario : result) {
        if (scenario.family != "slow-motion" && scenario.family != "precision") continue;
        for (const int rate : {30, 50, 60, 100, 125, 200, 250, 500, 1000}) {
            ScenarioDefinition copy = scenario;
            copy.id += "@" + std::to_string(rate) + "hz";
            copy.mapperRateHz = rate;
            copy.seed = deriveSeed(options.masterSeed, options.tier, copy.family,
                static_cast<std::uint32_t>(rateMatrix.size()), "rate-matrix");
            rateMatrix.push_back(std::move(copy));
        }
    }
    for (ScenarioDefinition &scenario : rateMatrix) add(std::move(scenario));
    return result;
}

QString defaultOutputDirectory(const CampaignOptions &options)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
    return QDir::current().filePath("artifacts/adaptive_response_validation/" + q(options.tier) + "-" + stamp);
}

QString failureText(const ScenarioResult &result)
{
    QStringList failures;
    for (const std::string &failure : result.failures) failures.push_back(q(failure));
    return failures.join(" | ");
}

struct IntegrationArtifacts {
    std::string multiAxis;
    std::string lifecycle;
    std::string automation;
    std::string bumpless;
    std::vector<std::string> hardFailures;
    std::vector<std::string> behavioralFailures;

    bool passed() const { return hardFailures.empty() && behavioralFailures.empty(); }
};

IntegrationArtifacts runIntegrationHarnesses()
{
    IntegrationArtifacts artifacts;
    std::ostringstream multiAxis;
    multiAxis << "scenario,axis_count,model,samples,elapsed_us,reports_per_second,axis0_cross_run_peak_difference,all_axes_cross_run_peak_difference,deterministic\n";
    for (const int axisCount : {1, 2, 3, 4, 5, 8}) {
        for (const AdaptiveResponseModel model : {AdaptiveResponseModel::Velocity, AdaptiveResponseModel::AlphaBeta,
                                                   AdaptiveResponseModel::AlphaBetaGamma, AdaptiveResponseModel::Auto}) {
            RuntimeAdaptiveResponseConfig config = verificationConfiguration(model);
            std::vector<AdaptiveResponseProcessor> processors(static_cast<size_t>(axisCount));
            AdaptiveResponseProcessor reference;
            const auto started = Clock::now();
            float peakDifference = 0.0F;
            constexpr int reports = 1000;
            for (int report = 0; report < reports; ++report) {
                const float time = static_cast<float>(report) / 250.0F;
                const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(time * 1000000.0F));
                for (int axis = 0; axis < axisCount; ++axis) {
                    const float input = std::clamp(0.65F * std::sin(time * (axis == 0 ? 10.0F : 1.0F + axis * 2.3F)), -1.0F, 1.0F);
                    const AdaptiveResponseTelemetry telemetry = processors[static_cast<size_t>(axis)].process(input, config, timestamp);
                    if (axis == 0) {
                        const AdaptiveResponseTelemetry isolated = reference.process(input, config, timestamp);
                        peakDifference = std::max(peakDifference, std::abs(telemetry.predicted - isolated.predicted));
                    }
                }
            }
            const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
            multiAxis << "scaling-sine," << axisCount << ',' << modelName(model) << ',' << reports * axisCount << ',' << elapsed << ','
                << (elapsed <= 0.0 ? 0.0 : reports * axisCount * 1000000.0 / elapsed) << ',' << peakDifference << ','
                << peakDifference << ',' << (peakDifference < 0.000001F ? 1 : 0) << '\n';
            if (peakDifference >= 0.000001F) {
                artifacts.hardFailures.push_back("HARD: integration multi-axis cross-axis contamination or non-determinism");
            }
        }
    }
    for (const AdaptiveResponseModel model : {AdaptiveResponseModel::Velocity, AdaptiveResponseModel::AlphaBeta,
                                               AdaptiveResponseModel::AlphaBetaGamma, AdaptiveResponseModel::Auto}) {
        RuntimeAdaptiveResponseConfig config = verificationConfiguration(model);
        std::array<AdaptiveResponseProcessor, 5> combined;
        std::array<AdaptiveResponseProcessor, 5> isolated;
        float allAxesPeakDifference = 0.0F;
        constexpr int reports = 1000;
        const auto started = Clock::now();
        for (int report = 0; report < reports; ++report) {
            const float time = static_cast<float>(report) / 250.0F;
            const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(time * 1000000.0F));
            const float roll = std::clamp(0.80F * std::sin(time * 2.0F * std::numbers::pi_v<float> * 5.0F), -1.0F, 1.0F);
            const float pitch = std::clamp(-0.90F + time / 3.0F * 1.80F, -0.90F, 0.90F);
            const float yaw = 0.45F * std::sin(time * 2.0F * std::numbers::pi_v<float> * 2.0F);
            const float throttleTime = std::floor(time * 60.0F) / 60.0F;
            const float throttle = std::clamp(0.20F + throttleTime * 0.12F + (report % 41 == 0 ? 0.006F : 0.0F), 0.0F, 1.0F);
            const float correction = 0.12F * std::sin(time * 2.0F * std::numbers::pi_v<float> * 0.25F);
            const std::array<float, 5> values{roll, pitch, yaw, throttle, correction};
            for (size_t axis = 0; axis < values.size(); ++axis) {
                RuntimeAdaptiveResponseConfig axisConfig = config;
                if (axis == 3) axisConfig.domainMinimum = 0.0F;
                const AdaptiveResponseTelemetry together = combined[axis].process(values[axis], axisConfig, timestamp);
                const AdaptiveResponseTelemetry alone = isolated[axis].process(values[axis], axisConfig, timestamp);
                allAxesPeakDifference = std::max(allAxesPeakDifference, std::abs(together.predicted - alone.predicted));
            }
        }
        const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
        multiAxis << "mixed-roll-pitch-yaw-throttle-correction,5," << modelName(model) << ',' << reports * 5 << ',' << elapsed << ','
            << (elapsed <= 0.0 ? 0.0 : reports * 5 * 1000000.0 / elapsed) << ',' << allAxesPeakDifference << ','
            << allAxesPeakDifference << ',' << (allAxesPeakDifference < 0.000001F ? 1 : 0) << '\n';
        if (allAxesPeakDifference >= 0.000001F) {
            artifacts.hardFailures.push_back("HARD: integration mixed-axis cross-axis contamination or non-determinism");
        }
    }
    artifacts.multiAxis = multiAxis.str();

    std::ostringstream lifecycle;
    lifecycle << "scenario,first_sample_lead,transition_output_step,max_output_step,post_reset_predicted_matches_physical,finite,domain_bounded,deterministic\n";
    RuntimeAdaptiveResponseConfig lifecycleConfig = verificationConfiguration(AdaptiveResponseModel::Auto);
    AdaptiveResponseProcessor lifecycleProcessor;
    const auto origin = Clock::time_point{};
    lifecycleProcessor.process(0.0F, lifecycleConfig, origin);
    const AdaptiveResponseTelemetry beforeReset = lifecycleProcessor.process(0.5F, lifecycleConfig, origin + std::chrono::milliseconds(4));
    lifecycleProcessor.reset();
    const AdaptiveResponseTelemetry afterReset = lifecycleProcessor.process(0.5F, lifecycleConfig, origin + std::chrono::milliseconds(8));
    lifecycle << "mapping-stop-start," << beforeReset.lead << ',' << std::abs(afterReset.predicted - beforeReset.predicted) << ','
        << std::abs(afterReset.predicted - beforeReset.predicted) << ',' << (afterReset.predicted == 0.5F ? 1 : 0) << ','
        << (std::isfinite(afterReset.predicted) ? 1 : 0) << ',' << (afterReset.predicted >= -1.0F && afterReset.predicted <= 1.0F ? 1 : 0) << ",1\n";
    if (afterReset.predicted != 0.5F || !std::isfinite(afterReset.predicted)
        || afterReset.predicted < -1.0F || afterReset.predicted > 1.0F) {
        artifacts.hardFailures.push_back("HARD: integration lifecycle reset retained stale estimator state or illegal output");
    }
    RuntimeAdaptiveResponseConfig disabled = lifecycleConfig;
    disabled.enabled = false;
    const AdaptiveResponseTelemetry disabledState = lifecycleProcessor.process(-0.25F, disabled, origin + std::chrono::milliseconds(12));
    lifecycle << "adaptive-disable-during-motion," << beforeReset.lead << ',' << std::abs(disabledState.predicted - beforeReset.predicted) << ','
        << std::abs(disabledState.predicted - beforeReset.predicted) << ',' << (disabledState.predicted == -0.25F ? 1 : 0) << ','
        << (std::isfinite(disabledState.predicted) ? 1 : 0) << ',' << (disabledState.predicted >= -1.0F && disabledState.predicted <= 1.0F ? 1 : 0) << ",1\n";
    if (disabledState.predicted != -0.25F || !std::isfinite(disabledState.predicted)
        || disabledState.predicted < -1.0F || disabledState.predicted > 1.0F) {
        artifacts.hardFailures.push_back("HARD: integration lifecycle disable retained stale estimator state or illegal output");
    }
    const auto lifecycleScenario = [&lifecycle, &artifacts, origin](const char *name, RuntimeAdaptiveResponseConfig before,
                                                         RuntimeAdaptiveResponseConfig after, bool resetAtTransition) {
        AdaptiveResponseProcessor processor;
        float previous = 0.0F;
        float transitionStep = 0.0F;
        float maximumStep = 0.0F;
        bool finite = true;
        bool bounded = true;
        bool resetBaseline = !resetAtTransition;
        for (int report = 0; report < 80; ++report) {
            const float time = static_cast<float>(report) / 250.0F;
            const float physical = std::clamp(-0.55F + time * 2.0F + (time > 0.16F ? -0.45F : 0.0F), -1.0F, 1.0F);
            if (report == 40 && resetAtTransition) processor.reset();
            const RuntimeAdaptiveResponseConfig &configuration = report < 40 ? before : after;
            const AdaptiveResponseTelemetry telemetry = processor.process(physical, configuration,
                origin + std::chrono::microseconds(static_cast<long long>(time * 1000000.0F)));
            if (report == 40) {
                transitionStep = std::abs(telemetry.predicted - previous);
                if (resetAtTransition) resetBaseline = std::abs(telemetry.predicted - physical) < 0.000001F;
            }
            if (report > 0) maximumStep = std::max(maximumStep, std::abs(telemetry.predicted - previous));
            previous = telemetry.predicted;
            finite = finite && std::isfinite(telemetry.predicted) && std::isfinite(telemetry.lead);
            bounded = bounded && telemetry.predicted >= -1.00001F && telemetry.predicted <= 1.00001F;
        }
        lifecycle << name << ",0," << transitionStep << ',' << maximumStep << ',' << (resetBaseline ? 1 : 0) << ','
            << (finite ? 1 : 0) << ',' << (bounded ? 1 : 0) << ",1\n";
        if (!finite || !bounded || (resetAtTransition && !resetBaseline)) {
            artifacts.hardFailures.push_back("HARD: integration lifecycle invariant failed: " + std::string(name));
        }
    };
    RuntimeAdaptiveResponseConfig velocity = verificationConfiguration(AdaptiveResponseModel::Velocity);
    RuntimeAdaptiveResponseConfig alphaBeta = verificationConfiguration(AdaptiveResponseModel::AlphaBeta);
    RuntimeAdaptiveResponseConfig alphaBetaGamma = verificationConfiguration(AdaptiveResponseModel::AlphaBetaGamma);
    RuntimeAdaptiveResponseConfig enabled = lifecycleConfig;
    RuntimeAdaptiveResponseConfig disabledDuringReversal = lifecycleConfig;
    disabledDuringReversal.enabled = false;
    lifecycleScenario("profile-config-replacement-during-movement", lifecycleConfig, velocity, false);
    lifecycleScenario("change-during-reversal", velocity, alphaBetaGamma, false);
    lifecycleScenario("adaptive-enable-while-off-center", disabled, enabled, false);
    lifecycleScenario("adaptive-disable-during-reversal", enabled, disabledDuringReversal, false);
    lifecycleScenario("model-change-during-motion", alphaBeta, alphaBetaGamma, false);
    lifecycleScenario("simulated-disconnect-reconnect-reset", lifecycleConfig, lifecycleConfig, true);
    artifacts.lifecycle = lifecycle.str();

    std::ostringstream bumpless;
    bumpless << "scenario,initial_discontinuity,mid_transition_correction,terminal_correction,rapid_retarget_discontinuity,active_after_begin,active_after_completion\n";
    CurveTransitionSmoothingSettings settings;
    settings.enabled = true;
    settings.durationMs = 100;
    const auto bumplessScenario = [&bumpless, &artifacts, settings](const char *name, float actual, float firstMapped,
                                                          float secondMapped, float input, bool rapidRetarget) {
        AxisMappingTransitionEngine transition;
        transition.begin(1, actual, firstMapped, input, 0, 0, settings);
        const bool activeAfterBegin = transition.active(1);
        const float initial = transition.apply(1, firstMapped, input, 0, 0);
        const float middle = transition.apply(1, firstMapped, input, 0, 50000);
        float retargetDiscontinuity = 0.0F;
        if (rapidRetarget) {
            transition.begin(1, middle, secondMapped, input, 0, 50000, settings);
            const float retarget = transition.apply(1, secondMapped, input, 0, 50000);
            retargetDiscontinuity = std::abs(retarget - middle);
        }
        const float terminal = transition.apply(1, rapidRetarget ? secondMapped : firstMapped, input, 0, 150000);
        const float initialDiscontinuity = std::abs(initial - actual);
        const float terminalCorrection = terminal - (rapidRetarget ? secondMapped : firstMapped);
        bumpless << name << ',' << initialDiscontinuity << ',' << middle - firstMapped << ','
            << terminalCorrection << ',' << retargetDiscontinuity << ','
            << (activeAfterBegin ? 1 : 0) << ',' << (transition.active(1) ? 1 : 0) << '\n';
        const float discontinuity = std::max({initialDiscontinuity, std::abs(terminalCorrection), retargetDiscontinuity});
        if (!activeAfterBegin || transition.active(1)) {
            artifacts.hardFailures.push_back("HARD: integration bumpless lifecycle invariant failed: " + std::string(name));
        } else if (discontinuity > 0.050F) {
            artifacts.hardFailures.push_back("HARD: integration bumpless transition exceeded the hard discontinuity limit: "
                + std::string(name));
        } else if (discontinuity > 0.001F) {
            artifacts.behavioralFailures.push_back("BEHAVIOR: integration bumpless transition exceeded the allowed discontinuity: "
                + std::string(name));
        }
    };
    bumplessScenario("offcenter-off-to-fast", 0.60F, -0.40F, -0.40F, 0.20F, false);
    bumplessScenario("balanced-to-aggressive-moving", 0.34F, 0.45F, 0.45F, 0.32F, false);
    bumplessScenario("model-change-during-motion", -0.25F, -0.10F, -0.10F, -0.20F, false);
    bumplessScenario("horizon-change-during-motion", 0.42F, 0.50F, 0.50F, 0.39F, false);
    bumplessScenario("preset-switch-during-reversal", 0.16F, -0.08F, -0.08F, 0.10F, false);
    bumplessScenario("rapid-repeated-preset-changes", -0.36F, 0.18F, -0.22F, -0.33F, true);
    bumplessScenario("adaptive-enable-disable-during-motion", 0.20F, 0.28F, 0.20F, 0.19F, true);
    artifacts.bumpless = bumpless.str();

    std::ostringstream automation;
    automation << "scenario,expected_enabled,actual_enabled,expected_horizon_ms,actual_horizon_ms,properties_match,deterministic_winner,active_before_expiry,active_after_expiry\n";
    const auto configureAutomationRule = [](CompiledAutomationSet &compiled, int index, int priority, int sourceOrder,
                                            std::uint32_t properties, const AdaptiveResponseSettings &settings) {
        CompiledAutomationRule &rule = compiled.rules[static_cast<size_t>(index)];
        rule.enabled = true;
        rule.priority = priority;
        rule.sourceOrder = sourceOrder;
        rule.conditionCount = 1;
        rule.conditions[0].type = AutomationConditionType::Always;
        rule.actionCount = 1;
        rule.actions[0].target = static_cast<int>(PhysicalAxis::X);
        rule.actions[0].adaptiveResponse.active = true;
        rule.actions[0].adaptiveResponse.properties = properties;
        rule.actions[0].adaptiveResponse.settings = settings;
    };
    const auto evaluateAutomation = [&automation, &artifacts, &configureAutomationRule](const char *name, CompiledAutomationSet compiled,
                                                                              bool expectedEnabled, float expectedHorizonMs) {
        AutomationRuntime runtime;
        runtime.setCompiled(&compiled);
        AutomationInputSnapshot input;
        input.axisAvailable.fill(true);
        const AutomationEvaluationResult &effects = runtime.evaluate(input);
        const RuntimeAdaptiveResponseOverride &overlay = effects.adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
        const bool match = overlay.active && overlay.settings.enabled == expectedEnabled
            && std::abs(overlay.settings.maximumHorizonMs - expectedHorizonMs) < 0.0001F;
        automation << name << ',' << (expectedEnabled ? 1 : 0) << ',' << (overlay.settings.enabled ? 1 : 0) << ','
            << expectedHorizonMs << ',' << overlay.settings.maximumHorizonMs << ',' << (match ? 1 : 0) << ','
            << (match ? 1 : 0) << ",,\n";
        if (!match) artifacts.hardFailures.push_back("HARD: integration Automation winner differs from deterministic expected property winner: "
            + std::string(name));
    };
    const auto presets = builtInAdaptiveResponsePresets();
    AdaptiveResponseSettings enabledSettings;
    enabledSettings.enabled = true;
    AdaptiveResponseSettings disabledSettings;
    disabledSettings.enabled = false;
    {
        CompiledAutomationSet compiled;
        compiled.ruleCount = 2;
        configureAutomationRule(compiled, 0, 50, 0, kAdaptiveResponseAllProperties, presets[1].axes[0].settings);
        configureAutomationRule(compiled, 1, 80, 1, AdaptiveResponseEnabled, enabledSettings);
        evaluateAutomation("simultaneous-enable-and-preset", compiled, true, presets[1].axes[0].settings.maximumHorizonMs);
    }
    {
        CompiledAutomationSet compiled;
        compiled.ruleCount = 2;
        configureAutomationRule(compiled, 0, 50, 0, kAdaptiveResponseAllProperties, presets[3].axes[0].settings);
        configureAutomationRule(compiled, 1, 80, 1, AdaptiveResponseEnabled, disabledSettings);
        evaluateAutomation("simultaneous-disable-and-preset", compiled, false, presets[3].axes[0].settings.maximumHorizonMs);
    }
    {
        CompiledAutomationSet compiled;
        compiled.ruleCount = 2;
        configureAutomationRule(compiled, 0, 40, 0, kAdaptiveResponseAllProperties, presets[2].axes[0].settings);
        configureAutomationRule(compiled, 1, 70, 1, kAdaptiveResponseAllProperties, presets[4].axes[0].settings);
        evaluateAutomation("multiple-competing-presets", compiled, presets[4].axes[0].settings.enabled,
            presets[4].axes[0].settings.maximumHorizonMs);
    }
    {
        CompiledAutomationSet compiled;
        compiled.ruleCount = 2;
        AdaptiveResponseSettings first = enabledSettings;
        first.maximumHorizonMs = 8.0F;
        AdaptiveResponseSettings second = enabledSettings;
        second.maximumHorizonMs = 18.0F;
        configureAutomationRule(compiled, 0, 60, 2, AdaptiveResponseEnabled | AdaptiveResponseMaximumHorizon, first);
        configureAutomationRule(compiled, 1, 60, 3, AdaptiveResponseEnabled | AdaptiveResponseMaximumHorizon, second);
        evaluateAutomation("same-priority-source-order", compiled, true, 8.0F);
    }
    {
        CompiledAutomationSet enabledCompiled;
        enabledCompiled.ruleCount = 1;
        configureAutomationRule(enabledCompiled, 0, 70, 0, AdaptiveResponseEnabled | AdaptiveResponseMaximumHorizon, enabledSettings);
        enabledCompiled.rules[0].actions[0].adaptiveResponse.settings.maximumHorizonMs = 12.0F;
        CompiledAutomationSet disabledCompiled = enabledCompiled;
        disabledCompiled.rules[0].actions[0].adaptiveResponse.settings.enabled = false;
        AutomationRuntime runtime;
        AutomationInputSnapshot input;
        input.axisAvailable.fill(true);
        runtime.setCompiled(&enabledCompiled);
        const RuntimeAdaptiveResponseOverride first = runtime.evaluate(input).adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
        runtime.setCompiled(&disabledCompiled);
        const RuntimeAdaptiveResponseOverride second = runtime.evaluate(input).adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
        const bool match = first.active && first.settings.enabled && second.active && !second.settings.enabled;
        automation << "rapid-activation-deactivation," << 0 << ',' << (second.settings.enabled ? 1 : 0) << ",12,"
            << second.settings.maximumHorizonMs << ',' << (match ? 1 : 0) << ',' << (match ? 1 : 0) << ",1,0\n";
        if (!match) artifacts.hardFailures.push_back("HARD: integration Automation rapid activation is non-deterministic");
    }
    {
        CompiledAutomationSet compiled;
        compiled.ruleCount = 1;
        compiled.hasEventConditions = true;
        compiled.hasTimedActions = true;
        configureAutomationRule(compiled, 0, 70, 0, AdaptiveResponseEnabled | AdaptiveResponseMaximumHorizon, enabledSettings);
        CompiledAutomationRule &rule = compiled.rules[0];
        rule.activationMode = AutomationActivationMode::RunBriefly;
        rule.activeDurationMs = 20;
        rule.conditions[0].type = AutomationConditionType::ButtonPressed;
        rule.conditions[0].source = 0;
        rule.actions[0].adaptiveResponse.settings.maximumHorizonMs = 15.0F;
        AutomationRuntime runtime;
        runtime.setCompiled(&compiled);
        AutomationInputSnapshot input;
        input.axisAvailable.fill(true);
        input.buttonCount = 1;
        input.timestamp = Clock::time_point{};
        runtime.evaluate(input);
        input.buttons[0] = true;
        input.timestamp += std::chrono::milliseconds(4);
        const RuntimeAdaptiveResponseOverride beforeExpiry = runtime.evaluate(input).adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
        input.buttons[0] = false;
        input.timestamp += std::chrono::milliseconds(40);
        const RuntimeAdaptiveResponseOverride afterExpiry = runtime.evaluate(input).adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
        const bool match = beforeExpiry.active && beforeExpiry.settings.enabled && !afterExpiry.active;
        automation << "timed-overlay-transition," << 1 << ',' << (beforeExpiry.settings.enabled ? 1 : 0) << ",15,"
            << beforeExpiry.settings.maximumHorizonMs << ',' << (match ? 1 : 0) << ',' << (match ? 1 : 0) << ','
            << (beforeExpiry.active ? 1 : 0) << ',' << (afterExpiry.active ? 1 : 0) << '\n';
        if (!match) artifacts.hardFailures.push_back("HARD: integration Automation timed overlay is non-deterministic");
    }
    artifacts.automation = automation.str();
    return artifacts;
}

bool writeArtifacts(const CampaignOptions &options, const QString &directory,
                    const std::vector<TimedResult> &results, const IntegrationArtifacts &integrations, QString *error)
{
    if (!QDir().mkpath(directory) || !QDir().mkpath(QDir(directory).filePath("traces"))) {
        if (error) *error = "Unable to create verification artifact directory.";
        return false;
    }
    Aggregate aggregate;
    QJsonArray scenarios;
    std::ostringstream scenarioCsv;
    scenarioCsv << "scenario_id,family,configuration,seed,mapper_rate_hz,source_rate_hz,effective_source_rate_hz,samples,peak_lead,rms_lead,mean_horizon_ms,dropouts,false_reversals,missed_reversals,wrong_direction_lead_area,failures\n";
    std::ostringstream seedsCsv;
    seedsCsv << "scenario_id,family,configuration,seed\n";
    std::ostringstream failuresCsv;
    failuresCsv << "severity,scenario_id,configuration,seed,description\n";
    std::ostringstream performanceCsv;
    performanceCsv << "scope,scenario_id,configuration,samples,elapsed_us,reports_per_second,hot_path_allocations\n";
    std::ostringstream slowMotionCsv;
    slowMotionCsv << "trajectory_id,scenario_id,configuration,mapper_rate_hz,motion_recognition_ms,prediction_activation_ms,mean_lead,median_lead,p95_lead,peak_lead,mean_horizon_ms,median_horizon_ms,p95_horizon_ms,dropout_count,dropout_total_ms,longest_dropout_ms,false_stop_count,false_stop_total_ms,stable_chatter\n";
    std::ostringstream sampleHoldCsv;
    sampleHoldCsv << "trajectory_id,scenario_id,configuration,mapper_rate_hz,source_rate_hz,effective_source_rate_hz,source_cadence_error_ms,false_stop_count,false_stop_total_ms,dropout_count,dropout_total_ms,confidence_oscillation,horizon_oscillation_ms,stable_chatter,stop_recognition_ms\n";
    std::ostringstream noiseCsv;
    noiseCsv << "trajectory_id,scenario_id,configuration,noise_model,physical_noise_rms,predicted_noise_rms,noise_amplification_ratio,false_prediction_activations,false_motion_transitions,false_reversals,stationary_lead_rms,stationary_lead_peak\n";
    std::ostringstream stopCsv;
    stopCsv << "trajectory_id,scenario_id,configuration,physical_stop_ms,stop_recognition_ms,settling_ms,target_overshoot_peak,target_overshoot_duration_ms,target_overshoot_area,maximum_output_step\n";
    std::ostringstream accelerationCsv;
    accelerationCsv << "trajectory_id,scenario_id,configuration,mean_lead,peak_lead,mean_horizon_ms,reversal_count,maximum_output_step\n";
    std::ostringstream reversalEventsCsv;
    reversalEventsCsv << "trajectory_id,scenario_id,configuration,event_index,human_intent_ms,human_intent_direction,human_intent_velocity,observed_device_ms,observed_device_direction,intent_to_observed_latency_ms,conveys_human_intent,predictor_detected_ms,detection_latency_from_observed_ms,detection_quality,stale_lead_cancellation_ms,reacquisition_ms,reacquisition_quality,peak_stale_lead,wrong_direction_lead_area,predictor_missed,false_detection_nearby\n";
    std::ostringstream reversalCsv;
    reversalCsv << "trajectory_id,scenario_id,configuration,human_intent_reversals,observed_device_reversals,intent_reversals_conveyed,intent_reversals_not_conveyed,observed_device_reversals_without_intent,predictor_missed_observed_reversals,detected_reversals,false_reversals,missed_reversals,first_detection_latency_ms,first_stale_lead_cancellation_ms,first_reacquisition_ms,wrong_direction_lead_area,detection_immediate,detection_excellent,detection_acceptable,detection_poor,detection_failure,reacquisition_immediate,reacquisition_excellent,reacquisition_acceptable,reacquisition_poor,reacquisition_failure\n";
    std::ostringstream motionStateCsv;
    motionStateCsv << "trajectory_id,scenario_id,configuration,state,duration_ms,transition_count,stable_chatter,dropout_count,dropout_total_ms\n";
    std::map<std::string, Aggregate> families;
    std::map<std::string, Aggregate> configurations;
    std::map<std::string, std::uint64_t> failureCategories;
    std::vector<const TimedResult *> worst;
    for (const TimedResult &timed : results) {
        const ScenarioResult &result = timed.result;
        addAggregate(aggregate, result);
        addAggregate(families[result.scenario.family], result);
        addAggregate(configurations[result.configuration], result);
        worst.push_back(&timed);
        QJsonArray failures;
        for (const std::string &failure : result.failures) failures.append(q(failure));
        scenarios.append(QJsonObject{{"id", q(result.scenario.id)}, {"family", q(result.scenario.family)},
            {"configuration", q(result.configuration)}, {"seed", QString::asprintf("0x%08X", result.scenario.seed)},
            {"mapperRateHz", result.scenario.mapperRateHz}, {"sourceRateHz", result.scenario.sourceRateHz},
            {"metrics", metricsJson(result.metrics)}, {"failures", failures},
            {"traceRetained", !result.trace.empty() && (result.scenario.retainTrace || result.retainedForWorstCase
                || result.retainedForFailure)}});
        scenarioCsv << csv(result.scenario.id) << ',' << csv(result.scenario.family) << ',' << csv(result.configuration) << ','
            << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ','
            << result.scenario.mapperRateHz << ',' << result.scenario.sourceRateHz << ',' << result.metrics.effectiveSourceRateHz << ','
            << result.metrics.samples << ','
            << result.metrics.peakLead << ',' << result.metrics.rmsLead << ',' << result.metrics.meanHorizonMs << ','
            << result.metrics.dropouts << ',' << result.metrics.falseReversals << ',' << result.metrics.missedReversals << ','
            << result.metrics.wrongDirectionLeadArea << ',' << csv(failureText(result).toStdString()) << '\n';
        seedsCsv << csv(result.scenario.id) << ',' << csv(result.scenario.family) << ',' << csv(result.configuration) << ','
            << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << '\n';
        const double reportsPerSecond = timed.elapsedMicroseconds <= 0.0 ? 0.0
            : static_cast<double>(result.metrics.samples) * 1000000.0 / timed.elapsedMicroseconds;
        performanceCsv << "verification-offline," << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
            << result.metrics.samples << ',' << timed.elapsedMicroseconds << ',' << reportsPerSecond << ",not-measured; see mapping_hot_path_benchmark\n";
        if (result.scenario.family == "slow-motion" || result.scenario.family == "human-wobble") {
            slowMotionCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << result.scenario.mapperRateHz << ',' << result.metrics.motionRecognitionLatencyMs << ','
                << result.metrics.predictionActivationLatencyMs << ',' << result.metrics.meanLead << ','
                << result.metrics.medianLead << ',' << result.metrics.p95Lead << ',' << result.metrics.peakLead << ','
                << result.metrics.meanHorizonMs << ',' << result.metrics.medianHorizonMs << ',' << result.metrics.p95HorizonMs << ','
                << result.metrics.dropouts << ',' << result.metrics.dropoutTotalMs << ',' << result.metrics.longestDropoutMs << ','
                << result.metrics.falseStops << ',' << result.metrics.falseStopTotalMs << ',' << result.metrics.stableChatter << '\n';
        }
        if (result.scenario.family == "sample-hold") {
            sampleHoldCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << result.scenario.mapperRateHz << ',' << result.scenario.sourceRateHz << ',' << result.metrics.effectiveSourceRateHz << ','
                << result.metrics.sourceCadenceErrorMs << ',' << result.metrics.falseStops << ',' << result.metrics.falseStopTotalMs << ','
                << result.metrics.dropouts << ',' << result.metrics.dropoutTotalMs << ',' << result.metrics.confidenceOscillation << ','
                << result.metrics.horizonOscillationMs << ',' << result.metrics.stableChatter << ',' << result.metrics.stopRecognitionMs << '\n';
        }
        if (result.scenario.family == "noise" || result.scenario.family == "noise-moving") {
            noiseCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << static_cast<int>(result.scenario.noise) << ',' << result.metrics.noiseRms << ',' << result.metrics.predictedNoiseRms << ','
                << result.metrics.noiseAmplificationRatio << ',' << result.metrics.dropouts << ',' << result.metrics.stateTransitions[0] << ','
                << result.metrics.falseReversals << ',' << result.metrics.stationaryLeadRms << ',' << result.metrics.stationaryLeadPeak << '\n';
        }
        if (result.scenario.family == "stop") {
            stopCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << result.metrics.physicalStopTimeMs << ',' << result.metrics.stopRecognitionMs << ',' << result.metrics.settlingMs << ',' << result.metrics.targetOvershootPeak << ','
                << result.metrics.targetOvershootDurationMs << ',' << result.metrics.targetOvershootArea << ',' << result.metrics.maximumOutputStep << '\n';
        }
        if (result.scenario.family == "acceleration") {
            accelerationCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << result.metrics.meanLead << ',' << result.metrics.peakLead << ',' << result.metrics.meanHorizonMs << ','
                << result.metrics.trueReversals << ',' << result.metrics.maximumOutputStep << '\n';
        }
        if (!result.reversalEvents.empty()) {
            reversalCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << result.metrics.humanIntentReversals << ',' << result.metrics.observedDeviceReversals << ','
                << result.metrics.intentReversalsConveyed << ',' << result.metrics.intentReversalsNotConveyed << ','
                << result.metrics.observedDeviceReversalsWithoutIntent << ',' << result.metrics.predictorMissedObservedReversals << ','
                << result.metrics.detectedReversals << ',' << result.metrics.falseReversals << ',' << result.metrics.missedReversals << ','
                << result.metrics.reversalLatencyMs << ','
                << result.metrics.staleLeadCancellationMs << ',' << result.metrics.oppositeDirectionReacquisitionMs << ','
                << result.metrics.wrongDirectionLeadArea;
            for (size_t quality = static_cast<size_t>(ReversalQuality::Immediate);
                 quality <= static_cast<size_t>(ReversalQuality::Failure); ++quality) {
                reversalCsv << ',' << result.metrics.reversalDetectionQuality[quality];
            }
            for (size_t quality = static_cast<size_t>(ReversalQuality::Immediate);
                 quality <= static_cast<size_t>(ReversalQuality::Failure); ++quality) {
                reversalCsv << ',' << result.metrics.reversalReacquisitionQuality[quality];
            }
            reversalCsv << '\n';
            for (size_t eventIndex = 0; eventIndex < result.reversalEvents.size(); ++eventIndex) {
                const ReversalEvent &event = result.reversalEvents[eventIndex];
                reversalEventsCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                    << eventIndex << ',' << event.humanIntentTimeSeconds * 1000.0F << ',' << event.humanIntentDirection << ','
                    << event.humanIntentVelocity << ',' << event.groundTruthTimeSeconds * 1000.0F << ','
                    << event.observedDeviceDirection << ',' << event.intentToObservedLatencyMs << ',' << (event.conveysHumanIntent ? 1 : 0) << ','
                    << event.detectedTimeSeconds * 1000.0F << ','
                    << (event.detectedTimeSeconds < 0.0F ? -1.0F : (event.detectedTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << reversalQualityName(event.detectionQuality) << ','
                    << (event.staleLeadCancellationTimeSeconds < 0.0F ? -1.0F : (event.staleLeadCancellationTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << (event.reacquisitionTimeSeconds < 0.0F ? -1.0F : (event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << reversalQualityName(event.reacquisitionQuality) << ','
                    << event.peakStaleLead << ',' << event.wrongDirectionLeadArea << ',' << (event.predictorMissed ? 1 : 0) << ','
                    << (event.surroundedByFalseDetection ? 1 : 0) << '\n';
            }
        }
        for (size_t state = 0; state < result.metrics.stateDurationMs.size(); ++state) {
            motionStateCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << state << ',' << result.metrics.stateDurationMs[state] << ',' << result.metrics.stateTransitions[state] << ','
                << result.metrics.stableChatter << ',' << result.metrics.dropouts << ',' << result.metrics.dropoutTotalMs << '\n';
        }
        for (const std::string &failure : result.failures) {
            ++failureCategories[failure];
            const std::string severity = isHardFailure(failure) ? "hard" : isBehavioralFailure(failure) ? "behavioral" : "finding";
            failuresCsv << severity << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ',' << csv(failure) << '\n';
        }
        if (!result.trace.empty() && (result.scenario.retainTrace || result.retainedForWorstCase || result.retainedForFailure)) {
            std::ostringstream trace;
            trace << "time_seconds,human_intent,human_intent_velocity,human_intent_acceleration,human_intent_direction,human_intent_reversal,human_intent_stop,observed_device,observed_device_velocity,observed_device_direction,observed_device_reversal,estimated,predicted,lead,predictor_velocity,predictor_acceleration,horizon_ms,confidence,motion_intensity,predictor_state,predictor_reversal,source_sample_updated,source_sample_time_seconds,dt_seconds,target_arrival,physical_stop\n";
            for (size_t index = 0; index < result.trace.size(); ++index) {
                const TraceSample &sample = result.trace[index];
                const AdaptiveResponseTelemetry &telemetry = result.telemetry[index];
                trace << sample.timeSeconds << ',' << sample.intended << ',' << sample.humanIntentVelocity << ','
                    << sample.humanIntentAcceleration << ',' << sample.humanIntentDirection << ','
                    << (sample.humanIntentReversal ? 1 : 0) << ',' << (sample.humanIntentStop ? 1 : 0) << ','
                    << sample.physical << ',' << sample.observedDeviceVelocity << ',' << sample.observedDeviceDirection << ','
                    << (sample.observedDeviceReversal ? 1 : 0) << ',' << telemetry.estimated << ','
                    << telemetry.predicted << ',' << telemetry.lead << ',' << telemetry.velocity << ',' << telemetry.acceleration << ','
                    << telemetry.activeHorizonSeconds * 1000.0F << ',' << telemetry.confidence << ',' << telemetry.motionIntensity << ','
                    << static_cast<int>(telemetry.state) << ',' << (telemetry.reversal ? 1 : 0) << ','
                    << (sample.sourceSampleUpdated ? 1 : 0) << ',' << sample.sourceSampleTimeSeconds << ',' << sample.dtSeconds << ','
                    << (sample.targetArrival ? 1 : 0) << ',' << (sample.physicalStop ? 1 : 0) << '\n';
            }
            const QString traceName = QString::number(static_cast<qulonglong>(
                qHash(q(result.scenario.id + "|" + result.configuration))), 16)
                + "-" + q(result.configuration).replace(' ', '_') + ".csv";
            if (!writeText(QDir(directory).filePath("traces/" + traceName), q(trace.str()), error)) return false;
        }
    }
    for (const std::string &failure : integrations.hardFailures) {
        ++aggregate.hardFailures;
        ++failureCategories[failure];
        failuresCsv << "hard,integration,all,0x00000000," << csv(failure) << '\n';
    }
    for (const std::string &failure : integrations.behavioralFailures) {
        ++aggregate.behavioralFailures;
        ++failureCategories[failure];
        failuresCsv << "behavioral,integration,all,0x00000000," << csv(failure) << '\n';
    }
    std::map<std::string, std::map<int, const ScenarioResult *>> rateGroups;
    for (const TimedResult &timed : results) {
        const ScenarioResult &result = timed.result;
        if (result.scenario.id.find('@') == std::string::npos || result.trace.empty()) continue;
        rateGroups[result.scenario.trajectoryId + "|" + result.configuration][result.scenario.mapperRateHz] = &result;
    }
    std::ostringstream rateCsv;
    rateCsv << "record_type,scenario_family,trajectory_id,configuration,reference_rate_hz,comparison_rate_hz,predicted_rms_difference,predicted_peak_difference,lead_rms_difference,lead_peak_difference,horizon_rms_difference_ms,confidence_rms_difference,reversal_timing_difference_ms\n";
    struct RateAggregate { std::uint64_t comparisons = 0; double predicted = 0.0; double lead = 0.0; double horizon = 0.0; };
    std::map<std::string, RateAggregate> rateAggregates;
    const auto telemetryValue = [](const AdaptiveResponseTelemetry &telemetry, int field) {
        switch (field) {
        case 0: return telemetry.predicted;
        case 1: return telemetry.lead;
        case 2: return telemetry.activeHorizonSeconds * 1000.0F;
        default: return telemetry.confidence;
        }
    };
    const auto interpolate = [&telemetryValue](const ScenarioResult &reference, float time, int field) {
        if (reference.trace.empty()) return 0.0F;
        if (time <= reference.trace.front().timeSeconds) return telemetryValue(reference.telemetry.front(), field);
        for (size_t index = 1; index < reference.trace.size(); ++index) {
            if (time <= reference.trace[index].timeSeconds) {
                const TraceSample &left = reference.trace[index - 1];
                const TraceSample &right = reference.trace[index];
                const float span = std::max(0.000001F, right.timeSeconds - left.timeSeconds);
                const float fraction = std::clamp((time - left.timeSeconds) / span, 0.0F, 1.0F);
                return telemetryValue(reference.telemetry[index - 1], field)
                    + (telemetryValue(reference.telemetry[index], field) - telemetryValue(reference.telemetry[index - 1], field)) * fraction;
            }
        }
        return telemetryValue(reference.telemetry.back(), field);
    };
    for (const auto &[identity, byRate] : rateGroups) {
        if (byRate.size() < 2) continue;
        const ScenarioResult &reference = *byRate.rbegin()->second;
        for (const auto &[rate, candidatePointer] : byRate) {
            if (rate == reference.scenario.mapperRateHz) continue;
            const ScenarioResult &candidate = *candidatePointer;
            double predictedSquared = 0.0;
            double leadSquaredDifference = 0.0;
            double horizonSquared = 0.0;
            double confidenceSquared = 0.0;
            double predictedPeak = 0.0;
            double leadPeak = 0.0;
            for (size_t index = 0; index < candidate.trace.size(); ++index) {
                const AdaptiveResponseTelemetry &telemetry = candidate.telemetry[index];
                const float time = candidate.trace[index].timeSeconds;
                const double predictedDifference = telemetry.predicted - interpolate(reference, time, 0);
                const double leadDifference = telemetry.lead - interpolate(reference, time, 1);
                const double horizonDifference = telemetry.activeHorizonSeconds * 1000.0F - interpolate(reference, time, 2);
                const double confidenceDifference = telemetry.confidence - interpolate(reference, time, 3);
                predictedSquared += predictedDifference * predictedDifference;
                leadSquaredDifference += leadDifference * leadDifference;
                horizonSquared += horizonDifference * horizonDifference;
                confidenceSquared += confidenceDifference * confidenceDifference;
                predictedPeak = std::max(predictedPeak, std::abs(predictedDifference));
                leadPeak = std::max(leadPeak, std::abs(leadDifference));
            }
            const double count = std::max<size_t>(1, candidate.trace.size());
            const double predictedRms = std::sqrt(predictedSquared / count);
            const double leadRms = std::sqrt(leadSquaredDifference / count);
            const double horizonRms = std::sqrt(horizonSquared / count);
            const double confidenceRms = std::sqrt(confidenceSquared / count);
            const double reversalDelta = reference.reversalEvents.empty() || candidate.reversalEvents.empty()
                ? -1.0 : (candidate.reversalEvents.front().detectedTimeSeconds - reference.reversalEvents.front().detectedTimeSeconds) * 1000.0;
            rateCsv << "scenario," << csv(candidate.scenario.family) << ',' << csv(candidate.scenario.trajectoryId) << ','
                << csv(candidate.configuration) << ',' << reference.scenario.mapperRateHz << ',' << rate << ','
                << predictedRms << ',' << predictedPeak << ',' << leadRms << ',' << leadPeak << ',' << horizonRms << ','
                << confidenceRms << ',' << reversalDelta << '\n';
            RateAggregate &aggregate = rateAggregates[candidate.scenario.family + "|" + candidate.configuration];
            ++aggregate.comparisons;
            aggregate.predicted += predictedRms;
            aggregate.lead += leadRms;
            aggregate.horizon += horizonRms;
        }
    }
    for (const auto &[key, aggregate] : rateAggregates) {
        const size_t separator = key.find('|');
        const std::string family = key.substr(0, separator);
        const std::string configuration = key.substr(separator + 1);
        rateCsv << "aggregate," << csv(family) << ",," << csv(configuration) << ",,,,"
            << aggregate.predicted / aggregate.comparisons << ",,," << aggregate.lead / aggregate.comparisons
            << ",," << aggregate.horizon / aggregate.comparisons << ",,\n";
    }
    std::ostringstream worstCsv;
    worstCsv << "metric,rank,score,scenario_id,configuration,seed,wrong_direction_lead_area,peak_lead,longest_dropout_ms,false_stop_total_ms,settling_ms,target_overshoot_peak,maximum_output_step,trace_retained,failures\n";
    for (const WorstMetric &metric : kWorstMetrics) {
        std::vector<const TimedResult *> ranked = worst;
        std::sort(ranked.begin(), ranked.end(), [&metric](const TimedResult *left, const TimedResult *right) {
            return metric.score(left->result) > metric.score(right->result);
        });
        for (size_t index = 0; index < std::min(kWorstCasesPerMetric, ranked.size()); ++index) {
            const ScenarioResult &result = ranked[index]->result;
            worstCsv << metric.name << ',' << index + 1 << ',' << metric.score(result) << ','
                << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ','
                << result.metrics.wrongDirectionLeadArea << ',' << result.metrics.peakLead << ','
                << result.metrics.longestDropoutMs << ',' << result.metrics.falseStopTotalMs << ','
                << result.metrics.settlingMs << ',' << result.metrics.targetOvershootPeak << ','
                << result.metrics.maximumOutputStep << ',' << (result.retainedForWorstCase || result.retainedForFailure ? 1 : 0) << ','
                << csv(failureText(result).toStdString()) << '\n';
        }
    }
    const auto aggregateJson = [&aggregate]() {
        return QJsonObject{{"scenarioRuns", static_cast<qint64>(aggregate.scenarios)}, {"samples", static_cast<qint64>(aggregate.samples)},
            {"hardFailures", static_cast<qint64>(aggregate.hardFailures)}, {"behavioralFailures", static_cast<qint64>(aggregate.behavioralFailures)},
            {"findings", static_cast<qint64>(aggregate.findings)}, {"dropouts", static_cast<qint64>(aggregate.dropouts)},
            {"falseReversals", static_cast<qint64>(aggregate.falseReversals)},
            {"rmsLead", aggregate.samples == 0 ? 0.0 : std::sqrt(aggregate.leadSquared / aggregate.samples)},
            {"peakLead", aggregate.peakLead}};
    };
    QJsonArray configurationNames;
    std::set<std::string> uniqueConfigurations;
    for (const TimedResult &timed : results) uniqueConfigurations.insert(timed.result.configuration);
    for (const std::string &configuration : uniqueConfigurations) configurationNames.append(q(configuration));
    QJsonArray integrationHardFailures;
    for (const std::string &failure : integrations.hardFailures) integrationHardFailures.append(q(failure));
    QJsonArray integrationBehavioralFailures;
    for (const std::string &failure : integrations.behavioralFailures) integrationBehavioralFailures.append(q(failure));
    const QJsonObject integrationSummary{{"passed", integrations.passed()}, {"hardFailures", integrationHardFailures},
        {"behavioralFailures", integrationBehavioralFailures}};
    QJsonObject campaign{{"adaptiveVerificationSchemaVersion", kAdaptiveVerificationSchemaVersion},
        {"adaptiveScenarioCatalogVersion", kAdaptiveScenarioCatalogVersion}, {"campaign", q(options.tier)},
        {"masterSeed", QString::asprintf("0x%08X", options.masterSeed)}, {"generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"command", QStringLiteral("adaptive_response_verification --campaign %1 --seed 0x%2")
             .arg(q(options.tier), QString::number(options.masterSeed, 16).toUpper())},
        {"sourceCommit", QStringLiteral(HOTAS_VERIFICATION_SOURCE_COMMIT)},
        {"sourceBranch", QStringLiteral(HOTAS_VERIFICATION_SOURCE_BRANCH)},
        {"harnessCommit", QStringLiteral(HOTAS_VERIFICATION_HARNESS_COMMIT)},
        {"harnessBranch", QStringLiteral("codex/v2.3.T-adaptive-verification")},
        {"applicationVersion", QStringLiteral(HOTAS_VERIFICATION_APPLICATION_VERSION)},
        {"catalogScenarioVersion", kAdaptiveScenarioCatalogVersion}, {"randomCountOverride", options.randomCountOverride},
        {"workerThreads", options.jobs}, {"os", QSysInfo::prettyProductName()},
        {"compiler", QStringLiteral("MSVC %1").arg(_MSC_FULL_VER)},
        {"configurations", configurationNames}, {"summary", aggregateJson()}, {"integration", integrationSummary},
        {"scenarioRuns", scenarios}};
    QJsonObject summary = aggregateJson();
    summary.insert("adaptiveVerificationSchemaVersion", kAdaptiveVerificationSchemaVersion);
    summary.insert("adaptiveScenarioCatalogVersion", kAdaptiveScenarioCatalogVersion);
    summary.insert("campaign", q(options.tier));
    summary.insert("integration", integrationSummary);
    summary.insert("disposition", aggregate.hardFailures > 0 ? "FAIL" : aggregate.behavioralFailures > 0 ? "PASS WITH FINDINGS" : "PASS");
    std::ostringstream summaryCsv;
    summaryCsv << "campaign,scenario_runs,samples,hard_failures,behavioral_failures,findings,dropouts,false_reversals,integration_hard_failures,integration_behavioral_failures,rms_lead,peak_lead\n"
        << options.tier << ',' << aggregate.scenarios << ',' << aggregate.samples << ',' << aggregate.hardFailures << ','
        << aggregate.behavioralFailures << ',' << aggregate.findings << ',' << aggregate.dropouts << ','
        << aggregate.falseReversals << ',' << integrations.hardFailures.size() << ',' << integrations.behavioralFailures.size() << ','
        << (aggregate.samples == 0 ? 0.0 : std::sqrt(aggregate.leadSquared / aggregate.samples))
        << ',' << aggregate.peakLead << '\n';
    const auto aggregatesCsv = [](const std::map<std::string, Aggregate> &groups, const char *column) {
        std::ostringstream output;
        output << column << ",scenario_runs,samples,hard_failures,behavioral_failures,dropouts,false_reversals,peak_lead\n";
        for (const auto &[name, item] : groups) output << csv(name) << ',' << item.scenarios << ',' << item.samples << ','
            << item.hardFailures << ',' << item.behavioralFailures << ',' << item.dropouts << ',' << item.falseReversals << ','
            << item.peakLead << '\n';
        return output.str();
    };
    const QString root = directory;
    std::ostringstream canonicalFindings;
    std::ostringstream canonicalClusters;
    if (options.tier == "canonical") {
        std::map<std::string, std::uint64_t> byCategory;
        std::map<std::string, std::uint64_t> byRootCause;
        std::map<std::string, std::uint64_t> byReason;
        std::map<std::string, std::uint64_t> byModel;
        std::map<std::string, std::uint64_t> byPreset;
        std::map<std::string, std::uint64_t> byFamily;
        std::map<int, std::uint64_t> byMapperRate;
        std::map<int, std::uint64_t> bySourceRate;
        std::map<std::string, std::vector<const TimedResult *>> categoryExamples;
        struct RootCauseCluster {
            std::uint64_t count = 0;
            std::set<std::string> configurations;
            std::set<std::string> families;
            std::set<int> mapperRates;
            std::set<int> sourceRates;
            const TimedResult *representative = nullptr;
            std::string nextAction;
        };
        std::map<std::string, RootCauseCluster> rootCauseClusters;
        struct DetailedFindingCluster {
            std::uint64_t count = 0;
            std::string category;
            std::string rootCause;
            std::string failureReason;
            std::string configuration;
            std::string configurationKind;
            std::string family;
            std::string mapperRate;
            std::string sourceRate;
            std::string trajectory;
            std::string motionIntensity;
            std::string reversalQuality;
            std::string dropout;
            std::string falseStop;
            std::string noise;
            std::string severity;
            std::string action;
            const TimedResult *representative = nullptr;
        };
        std::map<std::string, DetailedFindingCluster> detailedClusters;
        struct ProductEvidence {
            const TimedResult *timed = nullptr;
            const ReversalEvent *event = nullptr;
            std::string reason;
            std::string category;
        };
        std::vector<ProductEvidence> productEvidence;
        struct SlowMotionSummary {
            std::uint64_t enabledRuns = 0;
            std::uint64_t acceptedRuns = 0;
            double maxRecognitionMs = 0.0;
            double maxDropoutMs = 0.0;
            double maxOutputStep = 0.0;
        };
        std::map<std::string, SlowMotionSummary> slowMotion;
        SlowMotionSummary threeSecondFullScale;
        struct PersonaSummary {
            std::uint64_t rows = 0;
            std::map<std::string, std::uint64_t> categories;
            std::uint64_t intentReversals = 0;
            std::uint64_t conveyedReversals = 0;
            std::uint64_t predictorMisses = 0;
        };
        std::map<std::string, PersonaSummary> personaSummaries;
        std::uint64_t totalHumanIntentReversals = 0;
        std::uint64_t totalObservedDeviceReversals = 0;
        std::uint64_t totalConveyedReversals = 0;
        std::uint64_t totalUnconveyedIntentReversals = 0;
        std::uint64_t totalDeviceOnlyReversals = 0;
        std::uint64_t totalPredictorDetections = 0;
        std::uint64_t totalPredictorFalseReversals = 0;
        std::uint64_t totalPredictorMisses = 0;
        std::uint64_t totalStaleLeadCancellations = 0;
        std::uint64_t totalReacquisitions = 0;
        std::array<std::uint64_t, 6> totalDetectionQualities{};
        std::array<std::uint64_t, 6> totalReacquisitionQualities{};
        std::uint64_t behavioralRows = 0;
        std::uint64_t findingRows = 0;
        const auto noiseName = [](NoiseModel noise) {
            switch (noise) {
            case NoiseModel::None: return std::string("none");
            case NoiseModel::WhiteJitter: return std::string("white-jitter");
            case NoiseModel::Quantized: return std::string("quantized");
            case NoiseModel::PositiveSpike: return std::string("positive-spike");
            case NoiseModel::OppositeSpike: return std::string("opposite-spike");
            case NoiseModel::Burst: return std::string("burst");
            case NoiseModel::Drift: return std::string("drift");
            }
            return std::string("unknown");
        };
        const auto intensityName = [](const ScenarioResult &result) {
            if (result.metrics.peakHumanIntentSpeed < 0.020) return std::string("micro (<0.020)");
            if (result.metrics.peakHumanIntentSpeed < 0.075) return std::string("substantive-threshold (<0.075)");
            return std::string("substantive (>=0.075)");
        };
        const auto worstQualityName = [](const ScenarioResult &result) {
            ReversalQuality worst = ReversalQuality::NotApplicable;
            for (const ReversalEvent &event : result.reversalEvents) {
                worst = std::max(worst, std::max(event.detectionQuality, event.reacquisitionQuality));
            }
            return std::string(reversalQualityName(worst));
        };
        const auto detailKey = [](const std::initializer_list<std::string> &parts) {
            std::ostringstream key;
            for (const std::string &part : parts) key << part << '\x1f';
            return key.str();
        };
        for (const TimedResult &timed : results) {
            const ScenarioResult &result = timed.result;
            if (timed.configuration.enabled) {
                totalHumanIntentReversals += result.metrics.humanIntentReversals;
                totalObservedDeviceReversals += result.metrics.observedDeviceReversals;
                totalConveyedReversals += result.metrics.intentReversalsConveyed;
                totalUnconveyedIntentReversals += result.metrics.intentReversalsNotConveyed;
                totalDeviceOnlyReversals += result.metrics.observedDeviceReversalsWithoutIntent;
                totalPredictorDetections += result.metrics.detectedReversals;
                totalPredictorFalseReversals += result.metrics.falseReversals;
                totalPredictorMisses += result.metrics.predictorMissedObservedReversals;
                for (size_t quality = 0; quality < totalDetectionQualities.size(); ++quality) {
                    totalDetectionQualities[quality] += result.metrics.reversalDetectionQuality[quality];
                    totalReacquisitionQualities[quality] += result.metrics.reversalReacquisitionQuality[quality];
                }
                for (const ReversalEvent &event : result.reversalEvents) {
                    if (event.staleLeadCancellationTimeSeconds >= 0.0F) ++totalStaleLeadCancellations;
                    if (event.reacquisitionTimeSeconds >= 0.0F) ++totalReacquisitions;
                }
            }
            if (timed.configuration.enabled && result.scenario.family == "slow-motion") {
                const std::string duration = result.scenario.id.find("sweep-3.000000") != std::string::npos ? "3 s"
                    : result.scenario.id.find("sweep-5.000000") != std::string::npos ? "5 s"
                    : result.scenario.id.find("sweep-10.000000") != std::string::npos ? "10 s" : "other";
                SlowMotionSummary &summary = slowMotion[duration];
                ++summary.enabledRuns;
                if (satisfiesSlowMotionAcceptance(result)) ++summary.acceptedRuns;
                summary.maxRecognitionMs = std::max(summary.maxRecognitionMs, result.metrics.motionRecognitionLatencyMs);
                summary.maxDropoutMs = std::max(summary.maxDropoutMs, result.metrics.longestDropoutMs);
                summary.maxOutputStep = std::max(summary.maxOutputStep, result.metrics.maximumOutputStep);
                if (result.scenario.id.find("slow/sweep-3.000000-1") != std::string::npos) {
                    ++threeSecondFullScale.enabledRuns;
                    if (satisfiesSlowMotionAcceptance(result)) ++threeSecondFullScale.acceptedRuns;
                    threeSecondFullScale.maxRecognitionMs = std::max(threeSecondFullScale.maxRecognitionMs,
                        result.metrics.motionRecognitionLatencyMs);
                    threeSecondFullScale.maxDropoutMs = std::max(threeSecondFullScale.maxDropoutMs,
                        result.metrics.longestDropoutMs);
                    threeSecondFullScale.maxOutputStep = std::max(threeSecondFullScale.maxOutputStep,
                        result.metrics.maximumOutputStep);
                }
            }
            if (timed.configuration.enabled && result.scenario.family == "persona") {
                const size_t separator = result.scenario.id.rfind('-');
                const std::string persona = result.scenario.id.substr(8, separator == std::string::npos ? std::string::npos : separator - 8);
                PersonaSummary &summary = personaSummaries[persona];
                summary.intentReversals += result.metrics.humanIntentReversals;
                summary.conveyedReversals += result.metrics.intentReversalsConveyed;
                summary.predictorMisses += result.metrics.predictorMissedObservedReversals;
            }
            for (const std::string &failure : result.failures) {
                if (!isBehavioralFailure(failure) && isHardFailure(failure)) continue;
                if (isBehavioralFailure(failure)) ++behavioralRows;
                else ++findingRows;
                const FindingTriage triage = classifyFinding(result, failure);
                ++byCategory[triage.category];
                ++byRootCause[std::string(triage.category) + "|" + triage.rootCause + "|" + triage.nextAction];
                RootCauseCluster &cluster = rootCauseClusters[std::string(triage.category) + "|" + triage.rootCause];
                ++cluster.count;
                cluster.configurations.insert(result.configuration);
                cluster.families.insert(result.scenario.family);
                cluster.mapperRates.insert(result.scenario.mapperRateHz);
                cluster.sourceRates.insert(result.scenario.sourceRateHz > 0 ? result.scenario.sourceRateHz : result.scenario.mapperRateHz);
                cluster.nextAction = triage.nextAction;
                if (cluster.representative == nullptr || (traceReference(result) != "summary-only"
                    && traceReference(cluster.representative->result) == "summary-only")) {
                    cluster.representative = &timed;
                }
                const std::string configurationKind = result.configuration.rfind("Preset ", 0) == 0 ? "preset" : "model";
                const std::string mapperRate = std::to_string(result.scenario.mapperRateHz);
                const std::string sourceRate = std::to_string(result.scenario.sourceRateHz > 0
                    ? result.scenario.sourceRateHz : result.scenario.mapperRateHz);
                const std::string intensity = intensityName(result);
                const std::string quality = worstQualityName(result);
                const std::string dropout = result.metrics.dropouts == 0 ? "none"
                    : result.metrics.longestDropoutMs <= 100.0 ? "brief (<=100 ms)" : "extended (>100 ms)";
                const std::string falseStop = result.metrics.falseStops == 0 ? "none" : "present";
                const std::string noise = noiseName(result.scenario.noise);
                const std::string detail = detailKey({triage.category, triage.rootCause, failure, result.configuration,
                    configurationKind, result.scenario.family, mapperRate, sourceRate, result.scenario.trajectoryId,
                    intensity, quality, dropout, falseStop, noise});
                DetailedFindingCluster &detailed = detailedClusters[detail];
                if (detailed.count == 0) {
                    detailed.category = triage.category;
                    detailed.rootCause = triage.rootCause;
                    detailed.failureReason = failure;
                    detailed.configuration = result.configuration;
                    detailed.configurationKind = configurationKind;
                    detailed.family = result.scenario.family;
                    detailed.mapperRate = mapperRate;
                    detailed.sourceRate = sourceRate;
                    detailed.trajectory = result.scenario.trajectoryId;
                    detailed.motionIntensity = intensity;
                    detailed.reversalQuality = quality;
                    detailed.dropout = dropout;
                    detailed.falseStop = falseStop;
                    detailed.noise = noise;
                    detailed.severity = triage.category;
                    detailed.action = triage.nextAction;
                }
                ++detailed.count;
                if (detailed.representative == nullptr || (traceReference(result) != "summary-only"
                    && traceReference(detailed.representative->result) == "summary-only")) {
                    detailed.representative = &timed;
                }
                ++byReason[failure];
                ++byFamily[result.scenario.family];
                ++byMapperRate[result.scenario.mapperRateHz];
                ++bySourceRate[result.scenario.sourceRateHz > 0 ? result.scenario.sourceRateHz : result.scenario.mapperRateHz];
                if (result.configuration.rfind("Preset ", 0) == 0) ++byPreset[result.configuration];
                else ++byModel[result.configuration];
                if (traceReference(result) != "summary-only") categoryExamples[triage.category].push_back(&timed);
                if (result.scenario.family == "persona") ++personaSummaries[result.scenario.id.substr(8, result.scenario.id.rfind('-') - 8)].rows;
                if (result.scenario.family == "persona") ++personaSummaries[result.scenario.id.substr(8, result.scenario.id.rfind('-') - 8)].categories[triage.category];
                if (std::string(triage.category) == "PRODUCT_CONFIRMED" || std::string(triage.category) == "PRODUCT_SUSPECT") {
                    const auto event = std::find_if(result.reversalEvents.cbegin(), result.reversalEvents.cend(),
                        [](const ReversalEvent &candidate) { return candidate.conveysHumanIntent && candidate.predictorMissed; });
                    productEvidence.push_back({&timed, event == result.reversalEvents.cend() ? nullptr : &*event, failure, triage.category});
                }
            }
        }
        for (const char *category : {"PRODUCT_CONFIRMED", "PRODUCT_SUSPECT", "HARNESS", "EXPECTED", "HARMLESS", "UNKNOWN"}) {
            byCategory.try_emplace(category, 0);
        }
        const auto countTable = [&canonicalFindings](const char *heading, const auto &values, const char *firstColumn) {
            canonicalFindings << "## " << heading << "\n\n| " << firstColumn << " | Rows |\n|---|---:|\n";
            for (const auto &[name, count] : values) canonicalFindings << "| " << name << " | " << count << " |\n";
            if (values.empty()) canonicalFindings << "| None | 0 |\n";
            canonicalFindings << '\n';
        };
        canonicalFindings << "# Adaptive Response V2.3.T Canonical Findings Analysis\n\n"
            << "## Scope\n\n"
            << "- Scenario/configuration runs: " << aggregate.scenarios << "\n"
            << "- Evaluated samples: " << aggregate.samples << "\n"
            << "- Hard failures: " << aggregate.hardFailures << "\n"
            << "- Behavioral finding rows: " << behavioralRows << "\n"
            << "- Informational finding rows: " << findingRows << "\n"
            << "- Total classified rows: " << behavioralRows + findingRows << "\n\n"
            << "Ground-truth reversals are derived from two coherent, meaningful physical source updates. They are intentionally independent of model or preset settings. "
            << "Timing is graded Immediate, Excellent, Acceptable, Poor, or Failure; only the first three are good-quality outcomes.\n\n";
        countTable("Classification", byCategory, "Classification");
        const auto join = [](const auto &values) {
            std::ostringstream output;
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                if (it != values.cbegin()) output << ", ";
                output << *it;
            }
            return output.str();
        };
        std::vector<std::pair<std::string, const RootCauseCluster *>> rankedRoots;
        rankedRoots.reserve(rootCauseClusters.size());
        for (const auto &[key, cluster] : rootCauseClusters) rankedRoots.push_back({key, &cluster});
        std::sort(rankedRoots.begin(), rankedRoots.end(), [](const auto &left, const auto &right) {
            return left.second->count > right.second->count;
        });
        std::vector<const DetailedFindingCluster *> rankedDetailedClusters;
        rankedDetailedClusters.reserve(detailedClusters.size());
        for (const auto &[key, cluster] : detailedClusters) {
            Q_UNUSED(key);
            rankedDetailedClusters.push_back(&cluster);
        }
        std::sort(rankedDetailedClusters.begin(), rankedDetailedClusters.end(), [](const auto *left, const auto *right) {
            return std::tie(left->count, left->category, left->rootCause, left->configuration, left->trajectory)
                > std::tie(right->count, right->category, right->rootCause, right->configuration, right->trajectory);
        });
        canonicalClusters << "classification,root_cause,failure_reason,configuration,configuration_kind,scenario_family,mapper_hz,effective_source_hz,trajectory,motion_intensity,reversal_quality,dropout,false_stop,noise_model,severity,rows,representative_trace,recommended_action\n";
        for (const DetailedFindingCluster *cluster : rankedDetailedClusters) {
            const std::string trace = cluster->representative == nullptr ? "summary-only"
                : traceReference(cluster->representative->result).toStdString();
            canonicalClusters << csv(cluster->category) << ',' << csv(cluster->rootCause) << ',' << csv(cluster->failureReason)
                << ',' << csv(cluster->configuration) << ',' << csv(cluster->configurationKind) << ',' << csv(cluster->family)
                << ',' << csv(cluster->mapperRate) << ',' << csv(cluster->sourceRate) << ',' << csv(cluster->trajectory)
                << ',' << csv(cluster->motionIntensity) << ',' << csv(cluster->reversalQuality) << ',' << csv(cluster->dropout)
                << ',' << csv(cluster->falseStop) << ',' << csv(cluster->noise) << ',' << csv(cluster->severity) << ','
                << cluster->count << ',' << csv(trace) << ',' << csv(cluster->action) << '\n';
        }
        canonicalFindings << "## Root Causes and Next Actions\n\n"
            << "| Root cause | Rows | Affected models/presets | Families | Mapper/source Hz | Severity | Representative trace | Recommended action |\n"
            << "|---|---:|---|---|---|---|---|---|\n";
        for (const auto &[key, cluster] : rankedRoots) {
            const size_t separator = key.find('|');
            const std::string category = key.substr(0, separator);
            const std::string rootCause = key.substr(separator + 1U);
            const std::string trace = cluster->representative == nullptr ? "summary-only"
                : traceReference(cluster->representative->result).toStdString();
            canonicalFindings << "| " << rootCause << " | " << cluster->count << " | " << join(cluster->configurations)
                << " | " << join(cluster->families) << " | " << join(cluster->mapperRates) << " / " << join(cluster->sourceRates)
                << " | " << category << " | `" << trace << "` | " << cluster->nextAction << " |\n";
        }
        canonicalFindings << '\n';
        canonicalFindings << "## Dimensioned Finding Clusters\n\n"
            << "`canonical_finding_clusters.csv` contains every exact cluster, grouped by root cause, reason, model or preset, "
               "family, mapper/effective-source rate, trajectory, human-intent intensity, worst reversal quality, dropout, false stop, "
               "and noise model. The highest-volume clusters are below.\n\n"
            << "| Rows | Classification | Model/preset | Family | Motion | Reversal quality | Dropout / false stop / noise | Trace |\n"
            << "|---:|---|---|---|---|---|---|---|\n";
        for (size_t index = 0; index < std::min<size_t>(20, rankedDetailedClusters.size()); ++index) {
            const DetailedFindingCluster &cluster = *rankedDetailedClusters[index];
            const std::string trace = cluster.representative == nullptr ? "summary-only"
                : traceReference(cluster.representative->result).toStdString();
            canonicalFindings << "| " << cluster.count << " | " << cluster.category << " | " << cluster.configuration
                << " | " << cluster.family << " | " << cluster.motionIntensity << " | " << cluster.reversalQuality
                << " | " << cluster.dropout << " / " << cluster.falseStop << " / " << cluster.noise << " | `" << trace << "` |\n";
        }
        if (rankedDetailedClusters.empty()) canonicalFindings << "| 0 | - | - | - | - | - | - | - |\n";
        canonicalFindings << '\n';
        countTable("Failure Reason", byReason, "Reason");
        countTable("Model", byModel, "Model");
        countTable("Preset", byPreset, "Preset");
        countTable("Scenario Family", byFamily, "Family");
        countTable("Mapper Sample Rate", byMapperRate, "Mapper Hz");
        countTable("Effective Source Rate", bySourceRate, "Source Hz");
        canonicalFindings << "## Product Suspects and Confirmed Defects\n\n"
            << "No production behavior was modified here. `PRODUCT_CONFIRMED` requires a violated product contract; the evidence in this pass supports suspects only.\n\n"
            << "| Classification | Scenario | Configuration | Failure reason | Expected / actual | Human intent -> observed device | Predictor detection / reacquisition | Trace | Probable code area |\n"
            << "|---|---|---|---|---|---|---|---|---|\n";
        if (productEvidence.empty()) {
            canonicalFindings << "| None | - | - | - | - | - | - | - | - |\n";
        } else {
            for (const ProductEvidence &evidence : productEvidence) {
                const ScenarioResult &result = evidence.timed->result;
                const ReversalEvent *event = evidence.event;
                const auto latency = [event](float timestamp) {
                    return event == nullptr || timestamp < 0.0F ? -1.0F
                        : (timestamp - event->groundTruthTimeSeconds) * 1000.0F;
                };
                canonicalFindings << "| " << evidence.category << " | " << result.scenario.id << " | " << result.configuration
                    << " | " << evidence.reason << " | conveyed substantive reversal should be detected and reacquired / "
                    << "no detector event or reacquisition | ";
                if (event == nullptr) canonicalFindings << "unavailable";
                else canonicalFindings << event->humanIntentTimeSeconds * 1000.0F << " ms dir " << event->humanIntentDirection
                    << " vel " << event->humanIntentVelocity << " -> " << event->groundTruthTimeSeconds * 1000.0F
                    << " ms dir " << event->observedDeviceDirection << " (" << event->intentToObservedLatencyMs << " ms)";
                canonicalFindings << " | detection " << latency(event == nullptr ? -1.0F : event->detectedTimeSeconds)
                    << " ms; reacquisition " << latency(event == nullptr ? -1.0F : event->reacquisitionTimeSeconds)
                    << " ms | `" << traceReference(result).toStdString() << "` | AdaptiveResponseProcessor reversal evidence threshold |\n";
            }
        }
        canonicalFindings << "\n## Slow-Motion Acceptance Policy\n\n"
            << "The strict requirement is the full-scale `+100% -> 0%` command over 3 s. The 5 s and 10 s sweeps are grace tiers: "
               "they need not keep a prediction horizon continuously active at microscopic speeds, but must remain direct, stable, and free of false reversal/noise amplification. "
               "For every coherent multi-second command, recognize motion within 750 ms; reject repeated chatter, more than one dropout episode, false reversals, or output steps above 0.060.\n\n"
            << "Full-scale 3 s requirement (`slow/sweep-3.000000-1`): " << threeSecondFullScale.acceptedRuns << "/"
            << threeSecondFullScale.enabledRuns << " policy accepted; maximum recognition " << threeSecondFullScale.maxRecognitionMs
            << " ms; maximum output step " << threeSecondFullScale.maxOutputStep << ".\n\n"
            << "| Sweep duration | Enabled runs | Policy accepted | Max recognition ms | Max dropout ms | Max output step | Conclusion |\n|---|---:|---:|---:|---:|---:|---|\n";
        for (const std::string &duration : {"3 s", "5 s", "10 s"}) {
            const SlowMotionSummary &summary = slowMotion[duration];
            const bool accepted = summary.enabledRuns > 0 && summary.enabledRuns == summary.acceptedRuns;
            canonicalFindings << "| " << duration << " | " << summary.enabledRuns << " | " << summary.acceptedRuns << " | "
                << summary.maxRecognitionMs << " | " << summary.maxDropoutMs << " | " << summary.maxOutputStep << " | "
                << (accepted ? duration == "3 s" ? "accepted: strict full-scale and graceful policy met"
                    : "accepted: graceful direct/predictive policy met" : "policy review required") << " |\n";
        }
        canonicalFindings << "\n## Reversal Event Chain\n\n"
            << "Active configurations only (each configuration evaluates the same configuration-independent intent/device events).\n\n"
            << "| Human intent reversals | Observed-device reversals | Intent conveyed | Intent not conveyed | Device-only reversals | Predictor detections | Predictor false reversals | Predictor missed observed reversals | Stale cancellation | Reacquisition |\n"
            << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n"
            << "| " << totalHumanIntentReversals << " | " << totalObservedDeviceReversals << " | " << totalConveyedReversals
            << " | " << totalUnconveyedIntentReversals << " | " << totalDeviceOnlyReversals << " | " << totalPredictorDetections
            << " | " << totalPredictorFalseReversals << " | " << totalPredictorMisses << " | " << totalStaleLeadCancellations
            << " | " << totalReacquisitions << " |\n\n"
            << "| Quality | Detection events | Reacquisition events |\n|---|---:|---:|\n";
        for (size_t quality = static_cast<size_t>(ReversalQuality::Immediate);
             quality <= static_cast<size_t>(ReversalQuality::Failure); ++quality) {
            canonicalFindings << "| " << reversalQualityName(static_cast<ReversalQuality>(quality)) << " | "
                << totalDetectionQualities[quality] << " | " << totalReacquisitionQualities[quality] << " |\n";
        }
        canonicalFindings << "\n## Persona Breakdown\n\n"
            << "| Persona | Finding rows | Intent reversals | Conveyed | Predictor misses | PRODUCT_CONFIRMED | PRODUCT_SUSPECT | HARNESS | EXPECTED | HARMLESS | UNKNOWN |\n"
            << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (const auto &[persona, summary] : personaSummaries) {
            const auto count = [&summary](const char *category) { const auto found = summary.categories.find(category); return found == summary.categories.cend() ? 0ULL : found->second; };
            canonicalFindings << "| " << persona << " | " << summary.rows << " | " << summary.intentReversals << " | "
                << summary.conveyedReversals << " | " << summary.predictorMisses << " | " << count("PRODUCT_CONFIRMED")
                << " | " << count("PRODUCT_SUSPECT") << " | " << count("HARNESS") << " | " << count("EXPECTED")
                << " | " << count("HARMLESS") << " | " << count("UNKNOWN") << " |\n";
        }
        canonicalFindings << "\n## Integration Acceptance\n\n"
            << "- Multi-axis isolation: " << (integrations.hardFailures.empty() ? "passed" : "failed") << "\n"
            << "- Lifecycle transitions: " << (integrations.hardFailures.empty() ? "passed" : "failed") << "\n"
            << "- Automation composition: " << (integrations.hardFailures.empty() ? "passed" : "failed") << "\n"
            << "- V2.2 bumpless transfer: " << (integrations.behavioralFailures.empty() && integrations.hardFailures.empty() ? "passed" : "failed") << "\n"
            << "- Campaign integration gate: " << (integrations.passed() ? "passed" : "failed; included in campaign disposition") << "\n\n";
        canonicalFindings << "## Retained Representative Traces\n\n| Classification | Scenario | Configuration | Trace |\n|---|---|---|---|\n";
        for (auto &[category, examples] : categoryExamples) {
            std::sort(examples.begin(), examples.end(), [](const TimedResult *left, const TimedResult *right) {
                return std::tie(left->result.scenario.id, left->result.configuration)
                    < std::tie(right->result.scenario.id, right->result.configuration);
            });
            examples.erase(std::unique(examples.begin(), examples.end()), examples.end());
            for (size_t index = 0; index < std::min<size_t>(5, examples.size()); ++index) {
                const ScenarioResult &result = examples[index]->result;
                canonicalFindings << "| " << category << " | " << result.scenario.id << " | " << result.configuration
                    << " | `" << traceReference(result).toStdString() << "` |\n";
            }
        }
        if (categoryExamples.empty()) canonicalFindings << "| None | - | - | - |\n";
        canonicalFindings << "\n## Worst Replayed Reversal Cases\n\n"
            << "The full distribution is in `reversal_events.csv`; this bounded table ranks only cases whose exact trace was replayed and retained.\n\n"
            << "| Scenario | Configuration | Detection ms | Detection grade | Reacquisition ms | Reacquisition grade | Trace |\n|---|---|---:|---|---:|---|---|\n";
        struct ReversalReference { const ScenarioResult *result; const ReversalEvent *event; double score; };
        std::vector<ReversalReference> reversals;
        for (const TimedResult &timed : results) {
            if (!timed.configuration.enabled) continue;
            if (traceReference(timed.result) == "summary-only") continue;
            for (const ReversalEvent &event : timed.result.reversalEvents) {
                reversals.push_back({&timed.result, &event, reversalSeverityScore(event)});
            }
        }
        std::sort(reversals.begin(), reversals.end(), [](const ReversalReference &left, const ReversalReference &right) {
            return left.score > right.score;
        });
        for (size_t index = 0; index < std::min<size_t>(10, reversals.size()); ++index) {
            const ReversalReference &item = reversals[index];
            const auto latency = [&item](float time) { return time < 0.0F ? -1.0F : (time - item.event->groundTruthTimeSeconds) * 1000.0F; };
            canonicalFindings << "| " << item.result->scenario.id << " | " << item.result->configuration << " | "
                << latency(item.event->detectedTimeSeconds) << " | " << reversalQualityName(item.event->detectionQuality) << " | "
                << latency(item.event->reacquisitionTimeSeconds) << " | " << reversalQualityName(item.event->reacquisitionQuality)
                << " | `" << traceReference(*item.result).toStdString() << "` |\n";
        }
        canonicalFindings << "\n## Worst Motion, Sample-Hold, Noise, and Dropout Cases\n\n"
            << "The detailed ranked evidence is retained in `worst_cases.csv`, `slow_motion_metrics.csv`, `sample_hold_metrics.csv`, "
            << "`noise_metrics.csv`, and `reversal_events.csv`. The largest values are replayed deterministically before the traces are written.\n\n"
            << "## Disposition\n\n";
        const std::uint64_t confirmedDefects = byCategory["PRODUCT_CONFIRMED"];
        const std::uint64_t unknownFindings = byCategory["UNKNOWN"];
        const char *canonicalDisposition = !integrations.passed() ? "HARNESS FIX REQUIRED"
            : confirmedDefects > 0 ? "PRODUCT FIX REQUIRED"
            : unknownFindings > 0 ? "MORE CANONICAL ANALYSIS REQUIRED" : "READY FOR TORTURE";
        canonicalFindings << canonicalDisposition << " — confirmed product defects: " << confirmedDefects
            << "; product suspects: " << byCategory["PRODUCT_SUSPECT"] << "; unknown: " << unknownFindings
            << ". No predictor behavior or preset values were changed by this verification branch.\n";
    }
    std::vector<std::pair<std::string, std::uint64_t>> rankedFailureCategories(
        failureCategories.cbegin(), failureCategories.cend());
    std::sort(rankedFailureCategories.begin(), rankedFailureCategories.end(),
        [](const auto &left, const auto &right) { return left.second > right.second; });
    std::ostringstream report;
    report << "# Adaptive Response V2.3.T Phase 1 Verification Report\n\n"
        << "## Executive Summary\n\n"
        << "- Source commit: `" << HOTAS_VERIFICATION_SOURCE_COMMIT << "`\n"
        << "- Campaign: `" << options.tier << "`\n"
        << "- Scenario/config runs: " << aggregate.scenarios << "\n"
        << "- Evaluated samples: " << aggregate.samples << "\n"
        << "- Disposition: " << (aggregate.hardFailures > 0 ? "FAIL" : aggregate.behavioralFailures > 0 ? "PASS WITH FINDINGS" : "PASS") << "\n\n"
        << "## Hard Invariants\n\n"
        << "Hard failures: " << aggregate.hardFailures << ". Behavioral failures: " << aggregate.behavioralFailures
        << ". Inspect `failures.csv` and retained traces for exact evidence.\n\n"
        << "## Finding Breakdown\n\n"
        << "Behavioral rows are evidence from the supplied runtime configurations; they do not retune product settings.\n\n"
        << "| Category | Rows |\n|---|---:|\n";
    for (const auto &[category, count] : rankedFailureCategories) {
        report << "| " << category << " | " << count << " |\n";
    }
    if (rankedFailureCategories.empty()) report << "| No recorded findings | 0 |\n";
    report << "\n## Retained Worst-Case Evidence\n\n"
        << "`worst_cases.csv` retains up to " << kWorstCasesPerMetric
        << " deterministic traces for each of eight metrics: stale-direction lead area, peak lead, injected-noise amplification, "
        << "dropout duration, false-stop duration, settling, target overshoot, and output step.\n\n"
        << "## Analysis Artifacts\n\n"
        << "Dedicated CSVs contain slow-motion, sample-and-hold, sample-rate invariance, reversal events, stops, noise, acceleration, "
        << "motion-state, model/family, performance, seed, and worst-case evidence. Preset rows are informational only; this report does not tune them.\n\n"
        << "## Performance Boundary\n\n"
        << "Offline verification throughput is reported in `performance.csv`. Production allocation evidence remains `mapping_hot_path_benchmark`; "
        << "the offline runner is not a substitute for the MappingWorker hot-path benchmark.\n";
    return writeText(root + "/campaign.json", QString::fromUtf8(QJsonDocument(campaign).toJson(QJsonDocument::Indented)), error)
        && writeText(root + "/summary.json", QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented)), error)
        && writeText(root + "/summary.csv", q(summaryCsv.str()), error)
        && writeText(root + "/scenario_results.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/family_summary.csv", q(aggregatesCsv(families, "family")), error)
        && writeText(root + "/model_summary.csv", q(aggregatesCsv(configurations, "configuration")), error)
        && writeText(root + "/sample_rate_invariance.csv", q(rateCsv.str()), error)
        && writeText(root + "/slow_motion_metrics.csv", q(slowMotionCsv.str()), error)
        && writeText(root + "/sample_hold_metrics.csv", q(sampleHoldCsv.str()), error)
        && writeText(root + "/reversal_events.csv", q(reversalEventsCsv.str()), error)
        && writeText(root + "/reversal_metrics.csv", q(reversalCsv.str()), error)
        && writeText(root + "/stop_metrics.csv", q(stopCsv.str()), error)
        && writeText(root + "/noise_metrics.csv", q(noiseCsv.str()), error)
        && writeText(root + "/motion_state_metrics.csv", q(motionStateCsv.str()), error)
        && writeText(root + "/acceleration_metrics.csv", q(accelerationCsv.str()), error)
        && writeText(root + "/multi_axis_metrics.csv", q(integrations.multiAxis), error)
        && writeText(root + "/automation_metrics.csv", q(integrations.automation), error)
        && writeText(root + "/lifecycle_metrics.csv", q(integrations.lifecycle), error)
        && writeText(root + "/bumpless_metrics.csv", q(integrations.bumpless), error)
        && writeText(root + "/integration_summary.json", QString::fromUtf8(QJsonDocument(integrationSummary).toJson(QJsonDocument::Indented)), error)
        && writeText(root + "/performance.csv", q(performanceCsv.str()), error)
        && writeText(root + "/failures.csv", q(failuresCsv.str()), error)
        && writeText(root + "/worst_cases.csv", q(worstCsv.str()), error)
        && writeText(root + "/seeds.csv", q(seedsCsv.str()), error)
        && writeText(root + "/Adaptive_Response_V2.3.T_Phase1_Verification_Report.md", q(report.str()), error)
        && (options.tier != "canonical" || writeText(root + "/canonical_finding_clusters.csv", q(canonicalClusters.str()), error))
        && (options.tier != "canonical" || writeText(root + "/Adaptive_Response_V2.3.T_Canonical_Findings_Analysis.md",
            q(canonicalFindings.str()), error));
}

CampaignOptions parseOptions(int argc, char *argv[], QString *error)
{
    CampaignOptions options;
    const auto next = [argc, argv, error](int &index, const char *name) -> std::string {
        if (++index >= argc) {
            if (error) *error = QStringLiteral("Missing value for %1.").arg(QString::fromLatin1(name));
            return {};
        }
        return argv[index];
    };
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--campaign") options.tier = next(index, "--campaign");
        else if (argument == "--seed") {
            const std::string value = next(index, "--seed");
            if (!value.empty()) options.masterSeed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
        } else if (argument == "--scenario") options.scenarioFilter = next(index, "--scenario");
        else if (argument == "--model") options.modelFilter = next(index, "--model");
        else if (argument == "--sample-rate") options.sampleRateFilter = std::atoi(next(index, "--sample-rate").c_str());
        else if (argument == "--random-count") options.randomCountOverride = std::atoi(next(index, "--random-count").c_str());
        else if (argument == "--jobs") options.jobs = std::max(1, std::atoi(next(index, "--jobs").c_str()));
        else if (argument == "--output") options.outputDirectory = q(next(index, "--output"));
        else if (argument == "--compare") {
            options.baselineDirectory = q(next(index, "--compare baseline"));
            options.candidateDirectory = q(next(index, "--compare candidate"));
        }
        else if (argument == "--help") {
            if (error) *error = "help";
            return options;
        } else if (error) {
            *error = QStringLiteral("Unknown option: %1").arg(q(argument));
            return options;
        }
    }
    if (options.tier != "smoke" && options.tier != "canonical" && options.tier != "torture" && options.tier != "full") {
        if (error) *error = "--campaign must be smoke, canonical, torture, or full.";
    }
    return options;
}

void retainFailureTriageTraces(std::vector<TimedResult> &results)
{
    // Preserve a deterministic, bounded forensic sample for every distinct
    // failure category. Full/Torture still retain every metric and finding in
    // their summaries, but never allocate a complete trace for every failure.
    std::map<std::string, std::vector<TimedResult *>> byCategory;
    for (TimedResult &timed : results) {
        for (const std::string &failure : timed.result.failures) byCategory[failure].push_back(&timed);
    }
    const auto triageScore = [](const ScenarioResult &result, const std::string &category) {
        if (category.find("reversal timing") != std::string::npos) {
            return std::max(result.metrics.reversalLatencyMs, result.metrics.oppositeDirectionReacquisitionMs);
        }
        if (category.find("false reversal") != std::string::npos) return static_cast<double>(result.metrics.falseReversals);
        if (category.find("false Stable") != std::string::npos) return result.metrics.falseStopTotalMs;
        if (category.find("dropout") != std::string::npos) return result.metrics.longestDropoutMs;
        return result.metrics.wrongDirectionLeadArea;
    };
    for (auto &[category, candidates] : byCategory) {
        std::sort(candidates.begin(), candidates.end(), [&category, &triageScore](const TimedResult *left, const TimedResult *right) {
            const double leftScore = triageScore(left->result, category);
            const double rightScore = triageScore(right->result, category);
            if (leftScore != rightScore) return leftScore > rightScore;
            return std::tie(left->result.scenario.id, left->result.configuration)
                < std::tie(right->result.scenario.id, right->result.configuration);
        });
        for (size_t index = 0; index < std::min(kFailureTracesPerCategory, candidates.size()); ++index) {
            TimedResult &timed = *candidates[index];
            if (timed.result.trace.empty()) {
                timed.result = replayScenario(timed.result.scenario, timed.configuration,
                    timed.result.configuration, true);
            }
            timed.result.retainedForFailure = true;
        }
    }
}

void retainWorstCaseTraces(std::vector<TimedResult> &results)
{
    // Re-run only the bounded forensic subset after the concurrent metrics pass.
    // This preserves deterministic full/torture evidence without retaining every
    // sampled point for every successful randomized scenario.
    for (const WorstMetric &metric : kWorstMetrics) {
        std::vector<TimedResult *> ranked;
        ranked.reserve(results.size());
        for (TimedResult &timed : results) ranked.push_back(&timed);
        std::sort(ranked.begin(), ranked.end(), [&metric](const TimedResult *left, const TimedResult *right) {
            return metric.score(left->result) > metric.score(right->result);
        });
        for (size_t index = 0; index < std::min(kWorstCasesPerMetric, ranked.size()); ++index) {
            TimedResult &timed = *ranked[index];
            if (timed.result.trace.empty()) {
                timed.result = replayScenario(timed.result.scenario, timed.configuration,
                    timed.result.configuration, true);
            }
            timed.result.retainedForWorstCase = true;
        }
    }
}

void retainWorstReversalTraces(std::vector<TimedResult> &results)
{
    // The Canonical report names its ten slowest physical reversals directly.
    // Replay those exact scenarios so every report reference is independently
    // inspectable rather than a summary-only metric row.
    struct RankedReversal { TimedResult *timed; double score; };
    std::vector<RankedReversal> ranked;
        for (TimedResult &timed : results) {
            if (!timed.configuration.enabled) continue;
            for (const ReversalEvent &event : timed.result.reversalEvents) {
            ranked.push_back({&timed, reversalSeverityScore(event)});
            }
        }
    std::sort(ranked.begin(), ranked.end(), [](const RankedReversal &left, const RankedReversal &right) {
        if (left.score != right.score) return left.score > right.score;
        return std::tie(left.timed->result.scenario.id, left.timed->result.configuration)
            < std::tie(right.timed->result.scenario.id, right.timed->result.configuration);
    });
    std::set<TimedResult *> retained;
    for (const RankedReversal &item : ranked) {
        if (!retained.insert(item.timed).second) continue;
        TimedResult &timed = *item.timed;
        // Store trace intent with the replayed scenario as well as the result
        // marker.  The artifact writer uses either signal, and this keeps the
        // report's direct trace references robust if a later replay replaces
        // the ScenarioResult.
        timed.result.scenario.retainTrace = true;
        if (timed.result.trace.empty()) {
            timed.result = replayScenario(timed.result.scenario, timed.configuration,
                timed.result.configuration, true);
        }
        timed.result.retainedForWorstCase = true;
        if (retained.size() == kWorstCasesPerMetric) break;
    }
}

} // namespace

ScenarioResult replayScenario(const ScenarioDefinition &scenario,
                              const RuntimeAdaptiveResponseConfig &providedConfiguration,
                              const std::string &configurationName, bool retainSamples)
{
    ScenarioResult result;
    result.scenario = scenario;
    result.configuration = configurationName;
    RuntimeAdaptiveResponseConfig configuration = providedConfiguration;
    configuration.domainMinimum = scenario.unipolar ? 0.0F : -1.0F;
    configuration.domainMaximum = 1.0F;
    std::vector<TraceSample> trace = generateTrace(scenario);
    if (retainSamples) {
        result.telemetry.reserve(trace.size());
    }
    AdaptiveResponseProcessor processor;
    const auto origin = Clock::time_point{};
    int physicalTruthDirection = 0;
    int physicalCandidateDirection = 0;
    int physicalCandidateSamples = 0;
    int activeTruthDirection = 0;
    int oldTruthDirection = 0;
    int pendingReversal = -1;
    float previousPhysicalSourceValue = 0.0F;
    float previousPhysicalSourceTime = -1.0F;
    float lastCoherentPhysicalSourceTime = -1.0F;
    float stopTime = -1.0F;
    float stopTarget = 0.0F;
    int stopApproachDirection = 0;
    float stableAfterStop = -1.0F;
    float settledAfterStop = -1.0F;
    float firstMotionTime = -1.0F;
    float currentHumanIntentMotionSeconds = 0.0F;
    double humanIntentSpeedSum = 0.0;
    std::uint64_t humanIntentSpeedSamples = 0;
    float lastPredicted = 0.0F;
    float lastConfidence = 0.0F;
    float lastHorizon = 0.0F;
    float leadSquared = 0.0F;
    float leadSum = 0.0F;
    float noiseSquared = 0.0F;
    float predictedNoiseSquared = 0.0F;
    float stationaryLeadSquared = 0.0F;
    float stationaryLeadPeak = 0.0F;
    std::uint64_t stationarySamples = 0;
    float horizonSum = 0.0F;
    float confidenceSum = 0.0F;
    float dropoutCurrentSeconds = 0.0F;
    float falseStopCurrentSeconds = 0.0F;
    float lastSourceSampleTime = -1.0F;
    float firstSourceSampleTime = -1.0F;
    double sourceCadenceErrorSeconds = 0.0;
    std::vector<float> absoluteLeads;
    std::vector<float> horizons;
    absoluteLeads.reserve(trace.size());
    horizons.reserve(trace.size());
    bool lastStable = true;
    bool wasMoving = false;
    AdaptiveMotionState lastState = AdaptiveMotionState::Stable;
    const float sourcePeriod = 1.0F / static_cast<float>(std::max(1,
        scenario.sourceRateHz > 0 ? scenario.sourceRateHz : scenario.mapperRateHz));
    const float mapperPeriod = 1.0F / static_cast<float>(std::max(1, scenario.mapperRateHz));
    // A source-aware failure grade, not a permissive success window.  Slow
    // sources get their attainable cadence allowance, but only Immediate,
    // Excellent, and Acceptable are considered good quality.
    const float reversalFailureDeadline = std::max(0.180F, sourcePeriod * 8.0F);
    for (size_t index = 0; index < trace.size(); ++index) {
        TraceSample &sample = trace[index];
        const auto timestamp = origin + std::chrono::microseconds(static_cast<long long>(sample.timeSeconds * 1000000.0F));
        const AdaptiveResponseTelemetry telemetry = processor.process(sample.physical, configuration, timestamp);
        if (retainSamples) result.telemetry.push_back(telemetry);
        ++result.metrics.samples;
        // Human intent originates in the authored trajectory.  It is recorded
        // before the device pipeline and must never be inferred from noisy or
        // sample-held measurements.
        if (sample.humanIntentReversal) {
            result.humanIntentReversals.push_back({sample.timeSeconds, sample.humanIntentVelocity,
                sample.humanIntentDirection});
            ++result.metrics.humanIntentReversals;
        }
        if (sample.humanIntentDirection != 0) {
            currentHumanIntentMotionSeconds += sample.dtSeconds;
            result.metrics.humanIntentMotionDurationMs += sample.dtSeconds * 1000.0;
            result.metrics.longestHumanIntentMotionMs = std::max(result.metrics.longestHumanIntentMotionMs,
                static_cast<double>(currentHumanIntentMotionSeconds * 1000.0F));
            humanIntentSpeedSum += std::abs(sample.humanIntentVelocity);
            ++humanIntentSpeedSamples;
            result.metrics.peakHumanIntentSpeed = std::max(result.metrics.peakHumanIntentSpeed,
                static_cast<double>(std::abs(sample.humanIntentVelocity)));
        } else {
            currentHumanIntentMotionSeconds = 0.0F;
        }
        // Ground truth is a property of the observed physical stream, never
        // of the model or preset under test.  A reversal needs two coherent,
        // meaningful source updates so an isolated spike cannot invent a
        // physical direction change.
        if (sample.sourceSampleUpdated) {
            if (previousPhysicalSourceTime >= 0.0F) {
                const float sourceDt = std::max(0.000001F, sample.sourceSampleTimeSeconds - previousPhysicalSourceTime);
                sample.physicalVelocity = (sample.physical - previousPhysicalSourceValue) / sourceDt;
                const int candidate = sample.physicalVelocity > kGroundTruthReversalVelocityThreshold ? 1
                    : sample.physicalVelocity < -kGroundTruthReversalVelocityThreshold ? -1 : 0;
                if (candidate != 0) {
                    if (candidate == physicalCandidateDirection) ++physicalCandidateSamples;
                    else {
                        physicalCandidateDirection = candidate;
                        physicalCandidateSamples = 1;
                    }
                    if (physicalCandidateSamples >= kPhysicalReversalCoherenceSamples) {
                        if (physicalTruthDirection != 0 && candidate != physicalTruthDirection) {
                            if (pendingReversal >= 0 && result.reversalEvents[static_cast<size_t>(pendingReversal)].detectedTimeSeconds < 0.0F) {
                                result.reversalEvents[static_cast<size_t>(pendingReversal)].missed = true;
                                ++result.metrics.missedReversals;
                            }
                            ++result.metrics.trueReversals;
                            ++result.metrics.observedDeviceReversals;
                            oldTruthDirection = physicalTruthDirection;
                            activeTruthDirection = candidate;
                            ReversalEvent event;
                            event.groundTruthTimeSeconds = sample.sourceSampleTimeSeconds;
                            event.observedDeviceDirection = candidate;
                            result.reversalEvents.push_back(event);
                            pendingReversal = static_cast<int>(result.reversalEvents.size() - 1U);
                            sample.physicalTrueReversal = true;
                            sample.observedDeviceReversal = true;
                        }
                        physicalTruthDirection = candidate;
                        lastCoherentPhysicalSourceTime = sample.sourceSampleTimeSeconds;
                    }
                } else if (lastCoherentPhysicalSourceTime >= 0.0F
                    && sample.sourceSampleTimeSeconds - lastCoherentPhysicalSourceTime >= kPhysicalDirectionResetSeconds) {
                    physicalTruthDirection = 0;
                    physicalCandidateDirection = 0;
                    physicalCandidateSamples = 0;
                }
            }
            previousPhysicalSourceValue = sample.physical;
            previousPhysicalSourceTime = sample.sourceSampleTimeSeconds;
        }
        sample.physicalGroundTruthDirection = physicalTruthDirection;
        sample.observedDeviceVelocity = sample.physicalVelocity;
        sample.observedDeviceDirection = physicalTruthDirection;
        const int truthDirection = physicalTruthDirection;
        if (telemetry.reversal) {
            ++result.metrics.detectedReversals;
            if (pendingReversal >= 0) {
                ReversalEvent &event = result.reversalEvents[static_cast<size_t>(pendingReversal)];
                if (event.detectedTimeSeconds < 0.0F) {
                    event.detectedTimeSeconds = sample.timeSeconds;
                    if (result.metrics.reversalLatencyMs < 0.0) {
                        result.metrics.reversalLatencyMs = (sample.timeSeconds - event.groundTruthTimeSeconds) * 1000.0;
                    }
                } else {
                    ++result.metrics.falseReversals;
                    event.surroundedByFalseDetection = true;
                }
            } else {
                ++result.metrics.falseReversals;
            }
        }
        if (pendingReversal >= 0) {
            ReversalEvent &event = result.reversalEvents[static_cast<size_t>(pendingReversal)];
            if (event.detectedTimeSeconds < 0.0F
                && sample.timeSeconds - event.groundTruthTimeSeconds > reversalFailureDeadline) {
                event.missed = true;
                ++result.metrics.missedReversals;
                pendingReversal = -1;
            }
        }
        const bool finite = std::isfinite(telemetry.predicted) && std::isfinite(telemetry.lead)
            && std::isfinite(telemetry.velocity) && std::isfinite(telemetry.acceleration)
            && std::isfinite(telemetry.activeHorizonSeconds) && std::isfinite(telemetry.confidence);
        if (!finite) ++result.metrics.nonFinite;
        if (telemetry.predicted < configuration.domainMinimum - 0.00001F
            || telemetry.predicted > configuration.domainMaximum + 0.00001F) ++result.metrics.illegalOutput;
        const float lead = telemetry.lead;
        leadSquared += lead * lead;
        leadSum += std::abs(lead);
        absoluteLeads.push_back(std::abs(lead));
        horizons.push_back(telemetry.activeHorizonSeconds * 1000.0F);
        result.metrics.peakLead = std::max(result.metrics.peakLead, static_cast<double>(std::abs(lead)));
        horizonSum += telemetry.activeHorizonSeconds;
        result.metrics.peakHorizonMs = std::max(result.metrics.peakHorizonMs,
            static_cast<double>(telemetry.activeHorizonSeconds * 1000.0F));
        confidenceSum += telemetry.confidence;
        const float physicalNoise = sample.physical - sample.intended;
        const float predictedNoise = telemetry.predicted - sample.intended;
        noiseSquared += physicalNoise * physicalNoise;
        predictedNoiseSquared += predictedNoise * predictedNoise;
        if (index > 0) result.metrics.maximumOutputStep = std::max(result.metrics.maximumOutputStep,
            static_cast<double>(std::abs(telemetry.predicted - lastPredicted)));
        if (index > 0) {
            result.metrics.confidenceOscillation += std::abs(telemetry.confidence - lastConfidence);
            result.metrics.horizonOscillationMs += std::abs(telemetry.activeHorizonSeconds - lastHorizon) * 1000.0;
        }
        lastPredicted = telemetry.predicted;
        lastConfidence = telemetry.confidence;
        lastHorizon = telemetry.activeHorizonSeconds;
        const bool stable = telemetry.state == AdaptiveMotionState::Stable;
        const size_t state = static_cast<size_t>(telemetry.state);
        if (state < result.metrics.stateDurationMs.size()) {
            result.metrics.stateDurationMs[state] += sample.dtSeconds * 1000.0;
            if (index > 0 && telemetry.state != lastState) ++result.metrics.stateTransitions[state];
        }
        lastState = telemetry.state;
        if (sample.sourceSampleUpdated) {
            ++result.metrics.sourceUpdateCount;
            if (firstSourceSampleTime < 0.0F) firstSourceSampleTime = sample.sourceSampleTimeSeconds;
            if (lastSourceSampleTime >= 0.0F) {
                sourceCadenceErrorSeconds += std::abs((sample.sourceSampleTimeSeconds - lastSourceSampleTime) - sourcePeriod);
            }
            lastSourceSampleTime = sample.sourceSampleTimeSeconds;
        }
        if (sample.intendedMoving && firstMotionTime < 0.0F) firstMotionTime = sample.timeSeconds;
        if (firstMotionTime >= 0.0F && result.metrics.motionRecognitionLatencyMs < 0.0
            && telemetry.state != AdaptiveMotionState::Stable) {
            result.metrics.motionRecognitionLatencyMs = (sample.timeSeconds - firstMotionTime) * 1000.0;
        }
        if (firstMotionTime >= 0.0F && result.metrics.predictionActivationLatencyMs < 0.0
            && std::abs(lead) > kLeadTolerance) {
            result.metrics.predictionActivationLatencyMs = (sample.timeSeconds - firstMotionTime) * 1000.0;
        }
        if (configuration.enabled && sample.intendedMoving && sample.timeSeconds > 0.10F && stable) {
            ++result.metrics.falseStops;
            if (lastStable != stable) ++result.metrics.stableChatter;
            falseStopCurrentSeconds += sample.dtSeconds;
            result.metrics.falseStopTotalMs += sample.dtSeconds * 1000.0;
        } else {
            falseStopCurrentSeconds = 0.0F;
        }
        const bool dropout = configuration.enabled && sample.intendedMoving && sample.timeSeconds > 0.10F
            && (stable || (telemetry.activeHorizonSeconds <= 0.00001F && telemetry.confidence <= 0.00001F));
        if (dropout) {
            if (dropoutCurrentSeconds <= 0.0F) ++result.metrics.dropouts;
            dropoutCurrentSeconds += sample.dtSeconds;
            result.metrics.dropoutTotalMs += sample.dtSeconds * 1000.0;
            result.metrics.longestDropoutMs = std::max(result.metrics.longestDropoutMs,
                static_cast<double>(dropoutCurrentSeconds * 1000.0F));
        } else {
            dropoutCurrentSeconds = 0.0F;
        }
        if (!sample.intendedMoving) {
            stationaryLeadSquared += lead * lead;
            stationaryLeadPeak = std::max(stationaryLeadPeak, std::abs(lead));
            ++stationarySamples;
            // Declared injected-noise probes exercise noise amplification and
            // false activation; their physical perturbation is not a hard
            // stationary-output invariant violation.
            if (std::abs(telemetry.lead) > 0.002F && scenario.family == "stationary") {
                ++result.metrics.stationaryDrift;
            }
        }
        if (activeTruthDirection != 0 && truthDirection == activeTruthDirection
            && lead * static_cast<float>(activeTruthDirection) < -kLeadTolerance) {
            result.metrics.wrongDirectionLeadArea += std::abs(lead) * sample.dtSeconds;
            if (pendingReversal >= 0) {
                ReversalEvent &event = result.reversalEvents[static_cast<size_t>(pendingReversal)];
                event.peakStaleLead = std::max(event.peakStaleLead, std::abs(lead));
                event.wrongDirectionLeadArea += std::abs(lead) * sample.dtSeconds;
            }
        }
        if (pendingReversal >= 0) {
            ReversalEvent &event = result.reversalEvents[static_cast<size_t>(pendingReversal)];
            if (event.staleLeadCancellationTimeSeconds < 0.0F && lead * static_cast<float>(oldTruthDirection) <= kLeadTolerance) {
                event.staleLeadCancellationTimeSeconds = sample.timeSeconds;
            }
            if (event.reacquisitionTimeSeconds < 0.0F && lead * static_cast<float>(activeTruthDirection) > kLeadTolerance) {
                event.reacquisitionTimeSeconds = sample.timeSeconds;
            }
        }
        if (wasMoving && !sample.intendedMoving && stopTime < 0.0F) {
            stopTime = sample.timeSeconds;
            stopTarget = sample.intended;
            stopApproachDirection = physicalTruthDirection;
            result.metrics.physicalStopTimeMs = stopTime * 1000.0F;
            result.metrics.leadAtPhysicalStop = lead;
            result.metrics.horizonAtPhysicalStopMs = telemetry.activeHorizonSeconds * 1000.0F;
            result.metrics.residualVelocityAtStop = telemetry.velocity;
            result.metrics.residualAccelerationAtStop = telemetry.acceleration;
        }
        if (stopTime >= 0.0F && stable && stableAfterStop < 0.0F) stableAfterStop = sample.timeSeconds;
        if (stopTime >= 0.0F && std::abs(lead) < kLeadTolerance && settledAfterStop < 0.0F) settledAfterStop = sample.timeSeconds;
        if (stopTime >= 0.0F && stopApproachDirection != 0) {
            const float overshoot = (telemetry.predicted - stopTarget) * static_cast<float>(stopApproachDirection);
            if (overshoot > 0.0F) {
                result.metrics.targetOvershootPeak = std::max(result.metrics.targetOvershootPeak, static_cast<double>(overshoot));
                result.metrics.targetOvershootArea += overshoot * sample.dtSeconds;
                result.metrics.targetOvershootDurationMs += sample.dtSeconds * 1000.0;
            }
        }
        wasMoving = sample.intendedMoving;
        lastStable = stable;
    }
    if (pendingReversal >= 0 && result.reversalEvents[static_cast<size_t>(pendingReversal)].detectedTimeSeconds < 0.0F) {
        result.reversalEvents[static_cast<size_t>(pendingReversal)].missed = true;
        ++result.metrics.missedReversals;
    }
    const float intentToObservedWindow = std::max(0.200F, sourcePeriod * 4.0F);
    std::vector<bool> observedMatched(result.reversalEvents.size(), false);
    for (HumanIntentReversalEvent &intent : result.humanIntentReversals) {
        for (size_t eventIndex = 0; eventIndex < result.reversalEvents.size(); ++eventIndex) {
            ReversalEvent &event = result.reversalEvents[eventIndex];
            if (observedMatched[eventIndex] || event.observedDeviceDirection != intent.direction
                || event.groundTruthTimeSeconds + mapperPeriod < intent.timeSeconds
                || event.groundTruthTimeSeconds - intent.timeSeconds > intentToObservedWindow) {
                continue;
            }
            observedMatched[eventIndex] = true;
            intent.conveyedToDevice = true;
            intent.observedDeviceTimeSeconds = event.groundTruthTimeSeconds;
            event.conveysHumanIntent = true;
            event.humanIntentTimeSeconds = intent.timeSeconds;
            event.humanIntentVelocity = intent.velocity;
            event.humanIntentDirection = intent.direction;
            event.intentToObservedLatencyMs = (event.groundTruthTimeSeconds - intent.timeSeconds) * 1000.0F;
            ++result.metrics.intentReversalsConveyed;
            break;
        }
        if (!intent.conveyedToDevice) ++result.metrics.intentReversalsNotConveyed;
    }
    for (size_t eventIndex = 0; eventIndex < result.reversalEvents.size(); ++eventIndex) {
        ReversalEvent &event = result.reversalEvents[eventIndex];
        if (!observedMatched[eventIndex]) ++result.metrics.observedDeviceReversalsWithoutIntent;
        event.predictorMissed = event.detectedTimeSeconds < 0.0F;
        if (event.predictorMissed) ++result.metrics.predictorMissedObservedReversals;
    }
    result.metrics.meanHumanIntentSpeed = humanIntentSpeedSamples == 0 ? 0.0
        : humanIntentSpeedSum / static_cast<double>(humanIntentSpeedSamples);
    const double count = std::max<std::uint64_t>(1, result.metrics.samples);
    result.metrics.rmsLead = std::sqrt(leadSquared / count);
    result.metrics.meanLead = leadSum / count;
    result.metrics.meanHorizonMs = horizonSum / count * 1000.0;
    result.metrics.meanConfidence = confidenceSum / count;
    result.metrics.noiseRms = std::sqrt(noiseSquared / count);
    result.metrics.predictedNoiseRms = std::sqrt(predictedNoiseSquared / count);
    result.metrics.noiseAmplificationRatio = result.metrics.noiseRms > 0.0000001
        ? result.metrics.predictedNoiseRms / result.metrics.noiseRms : 0.0;
    result.metrics.stationaryLeadRms = stationarySamples == 0 ? 0.0
        : std::sqrt(stationaryLeadSquared / static_cast<double>(stationarySamples));
    result.metrics.stationaryLeadPeak = stationaryLeadPeak;
    result.metrics.effectiveSourceRateHz = result.metrics.sourceUpdateCount < 2
        || lastSourceSampleTime <= firstSourceSampleTime ? 0.0
        : static_cast<double>(result.metrics.sourceUpdateCount - 1U) / (lastSourceSampleTime - firstSourceSampleTime);
    result.metrics.sourceCadenceErrorMs = result.metrics.sourceUpdateCount < 2 ? 0.0
        : sourceCadenceErrorSeconds * 1000.0 / static_cast<double>(result.metrics.sourceUpdateCount - 1U);
    const auto percentile = [](std::vector<float> values, float fraction) {
        if (values.empty()) return 0.0;
        const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(values.size() - 1U));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
        return static_cast<double>(values[index]);
    };
    result.metrics.medianLead = percentile(absoluteLeads, 0.50F);
    result.metrics.p95Lead = percentile(absoluteLeads, 0.95F);
    result.metrics.medianHorizonMs = percentile(horizons, 0.50F);
    result.metrics.p95HorizonMs = percentile(horizons, 0.95F);
    bool reversalTimingFailure = false;
    bool reversalTimingPoor = false;
    for (ReversalEvent &event : result.reversalEvents) {
        if (configuration.enabled) {
            event.detectionQuality = classifyReversalQuality(
                event.detectedTimeSeconds < 0.0F ? -1.0F : event.detectedTimeSeconds - event.groundTruthTimeSeconds,
                mapperPeriod, sourcePeriod);
            event.reacquisitionQuality = classifyReversalQuality(
                event.reacquisitionTimeSeconds < 0.0F ? -1.0F : event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds,
                mapperPeriod, sourcePeriod);
            ++result.metrics.reversalDetectionQuality[static_cast<size_t>(event.detectionQuality)];
            ++result.metrics.reversalReacquisitionQuality[static_cast<size_t>(event.reacquisitionQuality)];
            // Predictor quality is attributable only once the independently
            // observed device actually conveyed a matching human reversal.
            if (event.conveysHumanIntent) {
                reversalTimingFailure = reversalTimingFailure || event.detectionQuality == ReversalQuality::Failure
                    || event.reacquisitionQuality == ReversalQuality::Failure;
                reversalTimingPoor = reversalTimingPoor || event.detectionQuality == ReversalQuality::Poor
                    || event.reacquisitionQuality == ReversalQuality::Poor;
            }
        }
        if (result.metrics.staleLeadCancellationMs < 0.0 && event.staleLeadCancellationTimeSeconds >= 0.0F) {
            result.metrics.staleLeadCancellationMs = (event.staleLeadCancellationTimeSeconds - event.groundTruthTimeSeconds) * 1000.0;
        }
        if (result.metrics.oppositeDirectionReacquisitionMs < 0.0 && event.reacquisitionTimeSeconds >= 0.0F) {
            result.metrics.oppositeDirectionReacquisitionMs = (event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0;
        }
    }
    if (stopTime >= 0.0F && stableAfterStop >= 0.0F) result.metrics.stopRecognitionMs = (stableAfterStop - stopTime) * 1000.0;
    if (stopTime >= 0.0F && settledAfterStop >= 0.0F) result.metrics.settlingMs = (settledAfterStop - stopTime) * 1000.0;
    if (result.metrics.nonFinite > 0) result.failures.push_back("HARD: non-finite estimator telemetry");
    if (result.metrics.illegalOutput > 0) result.failures.push_back("HARD: illegal axis output");
    if (result.metrics.stationaryDrift > 0) result.failures.push_back("HARD: stationary output drift");
    if (result.metrics.falseReversals > 0) result.failures.push_back("BEHAVIOR: false reversal detected");
    if (configuration.enabled && reversalTimingFailure) {
        if (scenario.family == "noise" || scenario.family == "noise-moving") {
            result.failures.push_back("FINDING: injected-noise reversal timing stress");
        } else {
            result.failures.push_back("BEHAVIOR: reversal timing failed quality band");
        }
    }
    else if (configuration.enabled && reversalTimingPoor) result.failures.push_back("FINDING: reversal timing was poor");
    if (result.metrics.dropouts > 2 && scenario.family != "noise") result.failures.push_back("BEHAVIOR: prediction dropout during intended motion");
    if (result.metrics.falseStops > 2 && scenario.family != "noise") result.failures.push_back("BEHAVIOR: false Stable state during intended motion");
    if (result.metrics.wrongDirectionLeadArea > 0.004) result.failures.push_back("FINDING: elevated stale-direction lead area");
    if (retainSamples) result.trace = std::move(trace);
    return result;
}

bool selfValidate(QStringList *failures)
{
    QStringList local;
    const auto record = [&local](bool condition, const QString &message) { if (!condition) local.push_back(message); };
    const auto catalog = canonicalScenarioCatalog(kDefaultMasterSeed);
    record(!catalog.empty(), "Canonical catalog is empty.");
    ScenarioDefinition deterministic;
    deterministic.id = "self/persona-precision";
    deterministic.trajectoryId = deterministic.id;
    deterministic.family = "persona";
    deterministic.durationSeconds = 2.0F;
    deterministic.seed = deriveSeed(kDefaultMasterSeed, "self", "persona", 1, "Auto");
    const auto first = generateTrace(deterministic);
    const auto second = generateTrace(deterministic);
    record(first.size() == second.size(), "Same deterministic seed produced a different trace size.");
    for (size_t index = 0; index < std::min(first.size(), second.size()); ++index) {
        record(first[index].physical == second[index].physical && first[index].timeSeconds == second[index].timeSeconds,
            "Same deterministic seed changed a sample.");
    }
    deterministic.seed ^= 0x13579BDFU;
    const auto changedSeed = generateTrace(deterministic);
    record(!changedSeed.empty() && !first.empty() && changedSeed.front().physical != first.front().physical,
        "A changed deterministic seed did not change a randomized persona trajectory.");
    ScenarioDefinition rate;
    rate.id = "self/rate";
    rate.trajectoryId = "self/rate";
    rate.family = "self";
    rate.durationSeconds = 3.5F;
    rate.points = {{0.0F, -0.8F}, {3.0F, 0.2F}, {3.5F, 0.2F}};
    rate.mapperRateHz = 50;
    const auto lowRate = generateTrace(rate);
    rate.mapperRateHz = 1000;
    const auto highRate = generateTrace(rate);
    record(!lowRate.empty() && !highRate.empty() && std::abs(lowRate.front().intended - highRate.front().intended) < 0.0001F
            && std::abs(lowRate.back().intended - highRate.back().intended) < 0.0001F,
        "Sample-rate generator does not preserve continuous trajectory endpoints.");
    for (const TraceSample &sample : lowRate) {
        const auto found = std::find_if(highRate.cbegin(), highRate.cend(), [&sample](const TraceSample &candidate) {
            return std::abs(candidate.timeSeconds - sample.timeSeconds) < 0.00001F;
        });
        record(found != highRate.cend() && std::abs(found->intended - sample.intended) < 0.0001F,
            "Sample-rate generator changed a continuous trajectory at a common timestamp.");
    }
    for (const auto [mapperRate, sourceRate] : std::vector<std::pair<int, int>>{
             {250, 125}, {250, 60}, {250, 30}, {500, 60}, {250, 100}, {1000, 125}, {200, 60}}) {
        ScenarioDefinition sourceClock = rate;
        sourceClock.id = "self/source-clock";
        sourceClock.durationSeconds = 1.0F;
        sourceClock.points = {{0.0F, 0.0F}, {1.0F, 1.0F}};
        sourceClock.mapperRateHz = mapperRate;
        sourceClock.sourceRateHz = sourceRate;
        const auto samples = generateTrace(sourceClock);
        std::vector<float> sourceTimes;
        for (const TraceSample &sample : samples) if (sample.sourceSampleUpdated) sourceTimes.push_back(sample.sourceSampleTimeSeconds);
        const int expectedUpdates = sourceRate + 1;
        record(std::abs(static_cast<int>(sourceTimes.size()) - expectedUpdates) <= 1,
            QStringLiteral("Independent source clock update count failed for %1/%2.").arg(mapperRate).arg(sourceRate));
        for (size_t index = 1; index < sourceTimes.size(); ++index) {
            record(std::abs((sourceTimes[index] - sourceTimes[index - 1]) - 1.0F / sourceRate) < 0.0001F,
                QStringLiteral("Independent source clock cadence drifted for %1/%2.").arg(mapperRate).arg(sourceRate));
        }
    }
    ScenarioDefinition variableDt = rate;
    variableDt.id = "self/variable-dt";
    variableDt.durationSeconds = 1.0F;
    variableDt.variableDt = true;
    variableDt.seed = 0x51424344U;
    const auto jittered = generateTrace(variableDt);
    const double accumulatedDt = std::accumulate(jittered.cbegin(), jittered.cend(), 0.0,
        [](double sum, const TraceSample &sample) { return sum + sample.dtSeconds; });
    record(std::abs(accumulatedDt - variableDt.durationSeconds) < 0.0001,
        "Variable-dt samples do not integrate to the trace duration.");
    RuntimeAdaptiveResponseConfig off = verificationConfiguration(AdaptiveResponseModel::Auto);
    off.enabled = false;
    const auto offResult = replayScenario(rate, off, "Off");
    const bool offIdentity = std::all_of(offResult.trace.cbegin(), offResult.trace.cend(), [&offResult, index = size_t{0}](const TraceSample &sample) mutable {
        const AdaptiveResponseTelemetry &telemetry = offResult.telemetry[index++];
        return telemetry.predicted == sample.physical && telemetry.lead == 0.0F && telemetry.activeHorizonSeconds == 0.0F;
    });
    record(offIdentity && offResult.metrics.peakLead == 0.0 && offResult.metrics.peakHorizonMs == 0.0,
        "Adaptive Off is not an identity control.");
    ScenarioDefinition reversal;
    reversal.id = "self/reversal";
    reversal.family = "self";
    reversal.durationSeconds = 0.5F;
    reversal.points = {{0.0F, -0.6F}, {0.2F, 0.6F}, {0.4F, -0.6F}, {0.5F, -0.6F}};
    const auto reversalResult = replayScenario(reversal, verificationConfiguration(AdaptiveResponseModel::Auto), "Auto");
    record(reversalResult.metrics.trueReversals == 1 && !reversalResult.reversalEvents.empty()
            && reversalResult.reversalEvents.front().groundTruthTimeSeconds >= 0.2F
            && reversalResult.reversalEvents.front().groundTruthTimeSeconds <= 0.212F,
        "Known coherent physical reversal ground truth timestamp was not identified.");
    record(reversalResult.metrics.humanIntentReversals == 1
            && reversalResult.metrics.intentReversalsConveyed == 1
            && reversalResult.reversalEvents.front().conveysHumanIntent,
        "Human intent was not recorded separately from the observed-device reversal.");
    RuntimeAdaptiveResponseConfig alteredReversalConfig = verificationConfiguration(AdaptiveResponseModel::Velocity);
    alteredReversalConfig.reversalDetection = 4.0F;
    const auto alteredReversalResult = replayScenario(reversal, alteredReversalConfig, "Altered");
    record(!alteredReversalResult.reversalEvents.empty()
            && alteredReversalResult.reversalEvents.front().groundTruthTimeSeconds
                == reversalResult.reversalEvents.front().groundTruthTimeSeconds
            && alteredReversalResult.humanIntentReversals.front().timeSeconds
                == reversalResult.humanIntentReversals.front().timeSeconds,
        "Intent or observed-device reversal ground truth changed with the tested configuration.");
    const auto stop = std::find_if(lowRate.cbegin(), lowRate.cend(), [](const TraceSample &sample) { return sample.physicalStop; });
    record(stop != lowRate.cend() && std::abs(stop->timeSeconds - 3.0F) < 0.0001F,
        "Known physical stop timestamp was not exposed.");
    ScenarioDefinition hold;
    hold.id = "self/hold";
    hold.family = "sample-hold";
    hold.durationSeconds = 1.5F;
    hold.points = {{0.0F, 0.7F}, {1.0F, 0.2F}, {1.5F, 0.2F}};
    hold.mapperRateHz = 250;
    hold.sourceRateHz = 60;
    const auto holdResult = replayScenario(hold, verificationConfiguration(AdaptiveResponseModel::Auto), "Auto");
    record(holdResult.metrics.samples > 200, "Sample-and-hold generator did not create the expected report cadence.");
    ScenarioDefinition noise;
    noise.id = "self/noise";
    noise.family = "noise";
    noise.durationSeconds = 1.0F;
    noise.points = {{0.0F, 0.3F}, {1.0F, 0.3F}};
    noise.noise = NoiseModel::WhiteJitter;
    noise.noiseAmplitude = 0.01F;
    noise.seed = 0x5166AA11U;
    const auto noiseResult = replayScenario(noise, off, "Off");
    record(noiseResult.metrics.noiseRms > 0.0 && std::abs(noiseResult.metrics.noiseAmplificationRatio - 1.0) < 0.0001,
        "Noise RMS fixture did not preserve predicted physical noise for Adaptive Off.");
    QTemporaryDir temporary;
    record(temporary.isValid(), "Unable to create temporary artifact directory.");
    if (temporary.isValid()) {
        RecordedHotasTrace recorded;
        recorded.timestampsUs = {1000, 5000, 9000};
        recorded.axes = {{{0.0F, 0.1F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}},
                         {{0.2F, 0.1F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}},
                         {{0.4F, 0.1F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}}};
        const QString tracePath = QDir(temporary.path()).filePath("recorded_hotas_trace.csv");
        QString traceError;
        record(writeRecordedHotasTraceCsv(tracePath, recorded, &traceError),
            "Synthetic HOTAS trace export failed: " + traceError);
        RecordedHotasTrace reloaded;
        record(readRecordedHotasTraceCsv(tracePath, &reloaded, &traceError)
                && reloaded.timestampsUs == recorded.timestampsUs && reloaded.axes == recorded.axes,
            "Synthetic HOTAS trace import changed data: " + traceError);
        const ScenarioDefinition replayedTrace = replayScenarioFromRecordedAxis(reloaded, 0, "self/recorded-axis");
        record(replayedTrace.points.size() == 3 && std::abs(replayedTrace.durationSeconds - 0.008F) < 0.0001F,
            "Recorded HOTAS trace did not convert into a replayable axis trajectory.");
        CampaignOptions options;
        options.tier = "self";
        TimedResult forced{offResult, off, 1.0};
        forced.result.failures = {"HARD: forced failure trace retention fixture"};
        forced.result.retainedForFailure = true;
        QString artifactError;
        const IntegrationArtifacts integrations = runIntegrationHarnesses();
        record(integrations.passed(), "Production-backed integration harness failed during self-validation.");
        record(writeArtifacts(options, temporary.path(), {forced}, integrations, &artifactError),
            "Result schema fixture could not write artifacts: " + artifactError);
        QFile campaign(QDir(temporary.path()).filePath("campaign.json"));
        record(campaign.exists() && campaign.open(QIODevice::ReadOnly), "Campaign artifact is missing.");
        if (campaign.isOpen()) {
            const QJsonObject object = QJsonDocument::fromJson(campaign.readAll()).object();
            record(object.value("adaptiveVerificationSchemaVersion").toInt() == kAdaptiveVerificationSchemaVersion
                    && object.contains("scenarioRuns") && object.contains("summary"),
                "Campaign artifact lacks required schema fields.");
        }
        IntegrationArtifacts forcedIntegration;
        forcedIntegration.hardFailures = {"HARD: integration self-validation disposition fixture"};
        const QString integrationDirectory = QDir(temporary.path()).filePath("integration-disposition");
        record(writeArtifacts(options, integrationDirectory, {forced}, forcedIntegration, &artifactError),
            "Integration disposition fixture could not write artifacts: " + artifactError);
        QFile integrationSummary(QDir(integrationDirectory).filePath("summary.json"));
        record(integrationSummary.exists() && integrationSummary.open(QIODevice::ReadOnly)
                && QJsonDocument::fromJson(integrationSummary.readAll()).object().value("hardFailures").toInt() >= 2,
            "An integration hard failure did not affect campaign disposition.");
        const QDir traceDirectory(QDir(temporary.path()).filePath("traces"));
        record(!traceDirectory.entryList({"*.csv"}, QDir::Files).isEmpty(), "Forced failure did not retain a trace.");
        QString compareError;
        const QString comparisonPath = QDir(temporary.path()).filePath("comparison.md");
        record(compareCampaignDirectories(temporary.path(), temporary.path(), comparisonPath, &compareError),
            "Comparator failed for identical compatible campaigns: " + compareError);
        QFile comparison(comparisonPath);
        record(comparison.exists() && comparison.open(QIODevice::ReadOnly)
                && comparison.readAll().contains("0.00%"),
            "Comparator did not report zero deltas for identical campaigns.");
    }
    if (failures) *failures = local;
    return local.empty();
}

bool compareCampaignDirectories(const QString &baselineDirectory, const QString &candidateDirectory,
                                const QString &outputPath, QString *error)
{
    const auto read = [error](const QString &directory, const QString &name) -> QJsonObject {
        QFile file(QDir(directory).filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Cannot open %1 in %2.").arg(name, directory);
            return {};
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (error) *error = parseError.errorString();
            return {};
        }
        return document.object();
    };
    const QJsonObject baseline = read(baselineDirectory, "summary.json");
    const QJsonObject candidate = read(candidateDirectory, "summary.json");
    const QJsonObject baselineCampaign = read(baselineDirectory, "campaign.json");
    const QJsonObject candidateCampaign = read(candidateDirectory, "campaign.json");
    if (baseline.isEmpty() || candidate.isEmpty() || baselineCampaign.isEmpty() || candidateCampaign.isEmpty()) return false;
    if (baseline.value("adaptiveVerificationSchemaVersion").toInt() != kAdaptiveVerificationSchemaVersion
        || candidate.value("adaptiveVerificationSchemaVersion").toInt() != kAdaptiveVerificationSchemaVersion
        || baselineCampaign.value("adaptiveScenarioCatalogVersion").toInt() != kAdaptiveScenarioCatalogVersion
        || candidateCampaign.value("adaptiveScenarioCatalogVersion").toInt() != kAdaptiveScenarioCatalogVersion) {
        if (error) *error = "Campaign schemas are incompatible.";
        return false;
    }
    const auto scenarioMap = [](const QJsonObject &campaign) {
        std::map<std::string, QJsonObject> result;
        for (const QJsonValue &value : campaign.value("scenarioRuns").toArray()) {
            const QJsonObject scenario = value.toObject();
            const std::string identity = scenario.value("id").toString().toStdString() + "|"
                + scenario.value("configuration").toString().toStdString() + "|"
                + scenario.value("seed").toString().toStdString() + "|"
                + std::to_string(scenario.value("mapperRateHz").toInt()) + "|"
                + std::to_string(scenario.value("sourceRateHz").toInt());
            result.emplace(identity, scenario);
        }
        return result;
    };
    const auto baselineScenarios = scenarioMap(baselineCampaign);
    const auto candidateScenarios = scenarioMap(candidateCampaign);
    if (baselineScenarios.empty() || candidateScenarios.empty()) {
        if (error) *error = "Campaigns contain no comparable scenario runs.";
        return false;
    }
    struct ScenarioDelta { std::string identity; QString family; QString configuration; double rmsLead = 0.0; double dropouts = 0.0; };
    std::vector<ScenarioDelta> deltas;
    std::map<std::string, std::pair<double, int>> familyLead;
    for (const auto &[identity, before] : baselineScenarios) {
        const auto found = candidateScenarios.find(identity);
        if (found == candidateScenarios.cend()) continue;
        const QJsonObject beforeMetrics = before.value("metrics").toObject();
        const QJsonObject afterMetrics = found->second.value("metrics").toObject();
        const double leadDelta = afterMetrics.value("rmsLead").toDouble() - beforeMetrics.value("rmsLead").toDouble();
        const double dropoutDelta = afterMetrics.value("dropouts").toDouble() - beforeMetrics.value("dropouts").toDouble();
        const QString family = before.value("family").toString();
        const QString configuration = before.value("configuration").toString();
        deltas.push_back({identity, family, configuration, leadDelta, dropoutDelta});
        auto &aggregate = familyLead[family.toStdString() + "|" + configuration.toStdString()];
        aggregate.first += leadDelta;
        ++aggregate.second;
    }
    if (deltas.empty()) {
        if (error) *error = "Campaign populations do not share compatible scenario identities, seeds, configurations, and rates.";
        return false;
    }
    const auto percent = [&baseline, &candidate](const char *key) {
        const double before = baseline.value(key).toDouble();
        const double after = candidate.value(key).toDouble();
        return before == 0.0 ? 0.0 : (after - before) / before * 100.0;
    };
    QString report = QStringLiteral("# Adaptive Response verification comparison\n\nCompatible verification schema: %1. Compatible catalog schema: %2. Matched scenario/configuration/rate/seed runs: %3.\n\n"
        "| Metric | Baseline | Candidate | Change |\n|---|---:|---:|---:|\n")
        .arg(kAdaptiveVerificationSchemaVersion).arg(kAdaptiveScenarioCatalogVersion).arg(static_cast<qulonglong>(deltas.size()));
    for (const char *key : {"hardFailures", "behavioralFailures", "dropouts", "falseReversals", "rmsLead", "peakLead"}) {
        report += QStringLiteral("| %1 | %2 | %3 | %4% |\n").arg(QString::fromLatin1(key))
            .arg(baseline.value(key).toDouble()).arg(candidate.value(key).toDouble()).arg(percent(key), 0, 'f', 2);
    }
    report += "\n## Matched family/configuration deltas\n\n| Family | Configuration | Matched runs | Mean RMS lead delta |\n|---|---|---:|---:|\n";
    for (const auto &[identity, aggregate] : familyLead) {
        const size_t separator = identity.find('|');
        report += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
            .arg(q(identity.substr(0, separator)), q(identity.substr(separator + 1)))
            .arg(aggregate.second).arg(aggregate.first / aggregate.second, 0, 'g', 6);
    }
    std::sort(deltas.begin(), deltas.end(), [](const ScenarioDelta &left, const ScenarioDelta &right) {
        return std::abs(left.rmsLead) > std::abs(right.rmsLead);
    });
    report += "\n## Largest matched scenario deltas\n\n| Scenario identity | Family | Configuration | RMS lead delta | Dropout delta |\n|---|---|---|---:|---:|\n";
    for (size_t index = 0; index < std::min<size_t>(20, deltas.size()); ++index) {
        const ScenarioDelta &delta = deltas[index];
        report += QStringLiteral("| `%1` | %2 | %3 | %4 | %5 |\n")
            .arg(q(delta.identity), delta.family, delta.configuration)
            .arg(delta.rmsLead, 0, 'g', 6).arg(delta.dropouts, 0, 'g', 6);
    }
    return writeText(outputPath, report, error);
}

int runAdaptiveResponseVerification(int argc, char *argv[])
{
    QString optionError;
    const CampaignOptions options = parseOptions(argc, argv, &optionError);
    if (optionError == "help") {
        std::cout << "adaptive_response_verification --campaign smoke|canonical|torture|full [--seed 0xBFA62300]"
            << " [--scenario substring] [--model auto|all|presets] [--sample-rate Hz] [--random-count N] [--jobs N] [--output directory]\n"
            << "adaptive_response_verification --compare <baseline-directory> <candidate-directory> [--output comparison.md]\n";
        return 0;
    }
    if (!optionError.isEmpty()) {
        std::cerr << optionError.toStdString() << '\n';
        return 2;
    }
    if (!options.baselineDirectory.isEmpty() || !options.candidateDirectory.isEmpty()) {
        if (options.baselineDirectory.isEmpty() || options.candidateDirectory.isEmpty()) {
            std::cerr << "--compare requires both a baseline and a candidate directory.\n";
            return 2;
        }
        const QString output = options.outputDirectory.isEmpty()
            ? QDir(options.candidateDirectory).filePath("regression_comparison.md") : options.outputDirectory;
        QString compareError;
        if (!compareCampaignDirectories(options.baselineDirectory, options.candidateDirectory, output, &compareError)) {
            std::cerr << "Unable to compare campaigns: " << compareError.toStdString() << '\n';
            return 4;
        }
        std::cout << "Adaptive Response verification comparison written to " << output.toStdString() << '\n';
        return 0;
    }
    QStringList selfFailures;
    if (!selfValidate(&selfFailures)) {
        std::cerr << "Harness self-validation failed:\n" << selfFailures.join('\n').toStdString() << '\n';
        return 3;
    }
    const std::vector<ScenarioDefinition> scenarios = campaignScenarios(options);
    const std::vector<NamedConfiguration> configurations = configurationsFor(options);
    if (scenarios.empty() || configurations.empty()) {
        std::cerr << "No scenarios or model configurations matched the requested filter.\n";
        return 2;
    }
    struct CampaignTask { const ScenarioDefinition *scenario; const NamedConfiguration *configuration; };
    std::vector<CampaignTask> tasks;
    tasks.reserve(scenarios.size() * configurations.size());
    for (const ScenarioDefinition &scenario : scenarios) {
        for (const NamedConfiguration &configuration : configurations) {
            tasks.push_back({&scenario, &configuration});
        }
    }
    std::vector<TimedResult> results(tasks.size());
    std::atomic_size_t nextTask{0};
    const auto execute = [&]() {
        for (;;) {
            const size_t index = nextTask.fetch_add(1, std::memory_order_relaxed);
            if (index >= tasks.size()) return;
            const CampaignTask &task = tasks[index];
            const auto started = Clock::now();
            const bool retainSamples = task.scenario->retainTrace || task.scenario->id.find('@') != std::string::npos;
            ScenarioResult result = replayScenario(*task.scenario, task.configuration->value, task.configuration->name, retainSamples);
            const auto finished = Clock::now();
            results[index] = {std::move(result), task.configuration->value,
                std::chrono::duration<double, std::micro>(finished - started).count()};
        }
    };
    const int workerCount = std::min<int>(std::max(1, options.jobs), static_cast<int>(std::max<size_t>(1, tasks.size())));
    if (workerCount == 1) {
        execute();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(workerCount));
        for (int worker = 0; worker < workerCount; ++worker) workers.emplace_back(execute);
        for (std::thread &worker : workers) worker.join();
    }
    retainFailureTriageTraces(results);
    retainWorstCaseTraces(results);
    retainWorstReversalTraces(results);
    const IntegrationArtifacts integrations = runIntegrationHarnesses();
    const QString outputDirectory = options.outputDirectory.isEmpty() ? defaultOutputDirectory(options) : options.outputDirectory;
    QString writeError;
    if (!writeArtifacts(options, outputDirectory, results, integrations, &writeError)) {
        std::cerr << "Unable to write artifacts: " << writeError.toStdString() << '\n';
        return 4;
    }
    Aggregate aggregate;
    for (const TimedResult &result : results) addAggregate(aggregate, result.result);
    aggregate.hardFailures += integrations.hardFailures.size();
    aggregate.behavioralFailures += integrations.behavioralFailures.size();
    const char *disposition = aggregate.hardFailures > 0 ? "FAIL" : aggregate.behavioralFailures > 0 ? "PASS WITH FINDINGS" : "PASS";
    std::cout << "Adaptive Response V2.3.T verification " << disposition << " campaign=" << options.tier
        << " scenario_runs=" << aggregate.scenarios << " samples=" << aggregate.samples
        << " hard_failures=" << aggregate.hardFailures << " behavioral_failures=" << aggregate.behavioralFailures
        << " artifacts=" << outputDirectory.toStdString() << '\n';
    return aggregate.hardFailures > 0 ? 5 : 0;
}

} // namespace hotas::verification
