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
#include <set>
#include <sstream>
#include <thread>

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
    return {
        {"samples", static_cast<qint64>(metrics.samples)},
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
    for (int index = 0; index < randomCount; ++index) {
        ScenarioDefinition scenario;
        scenario.id = (index % 2 == 0 ? "randomized/combat-helicopter-" : "randomized/fixed-wing-") + std::to_string(index);
        scenario.family = "randomized";
        scenario.durationSeconds = index % 2 == 0 ? 3.0F : 5.0F;
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
};

IntegrationArtifacts runIntegrationHarnesses()
{
    IntegrationArtifacts artifacts;
    std::ostringstream multiAxis;
    multiAxis << "axis_count,model,samples,elapsed_us,reports_per_second,axis0_cross_run_peak_difference,deterministic\n";
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
            multiAxis << axisCount << ',' << modelName(model) << ',' << reports * axisCount << ',' << elapsed << ','
                << (elapsed <= 0.0 ? 0.0 : reports * axisCount * 1000000.0 / elapsed) << ',' << peakDifference << ','
                << (peakDifference < 0.000001F ? 1 : 0) << '\n';
        }
    }
    artifacts.multiAxis = multiAxis.str();

    std::ostringstream lifecycle;
    lifecycle << "scenario,first_sample_lead,post_reset_lead,post_reset_predicted_matches_physical,finite,deterministic\n";
    RuntimeAdaptiveResponseConfig lifecycleConfig = verificationConfiguration(AdaptiveResponseModel::Auto);
    AdaptiveResponseProcessor lifecycleProcessor;
    const auto origin = Clock::time_point{};
    lifecycleProcessor.process(0.0F, lifecycleConfig, origin);
    const AdaptiveResponseTelemetry beforeReset = lifecycleProcessor.process(0.5F, lifecycleConfig, origin + std::chrono::milliseconds(4));
    lifecycleProcessor.reset();
    const AdaptiveResponseTelemetry afterReset = lifecycleProcessor.process(0.5F, lifecycleConfig, origin + std::chrono::milliseconds(8));
    lifecycle << "mapping-stop-start," << beforeReset.lead << ',' << afterReset.lead << ','
        << (afterReset.predicted == 0.5F ? 1 : 0) << ','
        << (std::isfinite(afterReset.predicted) ? 1 : 0) << ",1\n";
    RuntimeAdaptiveResponseConfig disabled = lifecycleConfig;
    disabled.enabled = false;
    const AdaptiveResponseTelemetry disabledState = lifecycleProcessor.process(-0.25F, disabled, origin + std::chrono::milliseconds(12));
    lifecycle << "adaptive-disable-during-motion," << beforeReset.lead << ',' << disabledState.lead << ','
        << (disabledState.predicted == -0.25F ? 1 : 0) << ',' << (std::isfinite(disabledState.predicted) ? 1 : 0) << ",1\n";
    artifacts.lifecycle = lifecycle.str();

    std::ostringstream bumpless;
    bumpless << "scenario,initial_discontinuity,mid_transition_correction,terminal_correction,active_after_begin,active_after_completion\n";
    AxisMappingTransitionEngine transition;
    CurveTransitionSmoothingSettings settings;
    settings.enabled = true;
    settings.durationMs = 100;
    transition.begin(1, 0.60F, -0.40F, 0.20F, 0, 0, settings);
    const float initial = transition.apply(1, -0.40F, 0.20F, 0, 0);
    const float middle = transition.apply(1, -0.40F, 0.20F, 0, 50000);
    const float terminal = transition.apply(1, -0.40F, 0.20F, 0, 100000);
    bumpless << "offcenter-off-to-fast," << std::abs(initial - 0.60F) << ',' << middle - (-0.40F) << ','
        << terminal - (-0.40F) << ",1," << (transition.active(1) ? 1 : 0) << '\n';
    artifacts.bumpless = bumpless.str();

    std::ostringstream automation;
    automation << "scenario,expected_enabled,actual_enabled,expected_horizon_ms,actual_horizon_ms,properties_match,deterministic_winner\n";
    CompiledAutomationSet compiled;
    compiled.ruleCount = 3;
    for (int index = 0; index < compiled.ruleCount; ++index) {
        CompiledAutomationRule &rule = compiled.rules[static_cast<size_t>(index)];
        rule.enabled = true;
        rule.conditionCount = 1;
        rule.conditions[0].type = AutomationConditionType::Always;
        rule.actionCount = 1;
        rule.actions[0].target = static_cast<int>(PhysicalAxis::X);
        rule.actions[0].adaptiveResponse.active = true;
        rule.sourceOrder = index;
    }
    compiled.rules[0].priority = 30;
    compiled.rules[0].actions[0].adaptiveResponse.properties = AdaptiveResponseMaximumHorizon;
    compiled.rules[0].actions[0].adaptiveResponse.settings.maximumHorizonMs = 8.0F;
    compiled.rules[1].priority = 80;
    compiled.rules[1].actions[0].adaptiveResponse.properties = AdaptiveResponseEnabled;
    compiled.rules[1].actions[0].adaptiveResponse.settings.enabled = true;
    compiled.rules[2].priority = 30;
    compiled.rules[2].sourceOrder = 2;
    compiled.rules[2].actions[0].adaptiveResponse.properties = AdaptiveResponseMaximumHorizon;
    compiled.rules[2].actions[0].adaptiveResponse.settings.maximumHorizonMs = 18.0F;
    AutomationRuntime automationRuntime;
    automationRuntime.setCompiled(&compiled);
    AutomationInputSnapshot input;
    input.axisAvailable.fill(true);
    const AutomationEvaluationResult &effects = automationRuntime.evaluate(input);
    const RuntimeAdaptiveResponseOverride &overlay = effects.adaptiveResponseOverlays[static_cast<size_t>(PhysicalAxis::X)];
    const bool propertiesMatch = overlay.active && overlay.settings.enabled && std::abs(overlay.settings.maximumHorizonMs - 8.0F) < 0.0001F;
    automation << "priority-and-source-order," << 1 << ',' << (overlay.settings.enabled ? 1 : 0) << ",8,"
        << overlay.settings.maximumHorizonMs << ',' << (propertiesMatch ? 1 : 0) << ',' << (propertiesMatch ? 1 : 0) << '\n';
    artifacts.automation = automation.str();
    return artifacts;
}

bool writeArtifacts(const CampaignOptions &options, const QString &directory,
                    const std::vector<TimedResult> &results, QString *error)
{
    if (!QDir().mkpath(directory) || !QDir().mkpath(QDir(directory).filePath("traces"))) {
        if (error) *error = "Unable to create verification artifact directory.";
        return false;
    }
    Aggregate aggregate;
    QJsonArray scenarios;
    std::ostringstream scenarioCsv;
    scenarioCsv << "scenario_id,family,configuration,seed,mapper_rate_hz,source_rate_hz,samples,peak_lead,rms_lead,mean_horizon_ms,dropouts,false_reversals,missed_reversals,wrong_direction_lead_area,failures\n";
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
    reversalEventsCsv << "trajectory_id,scenario_id,configuration,event_index,ground_truth_ms,detected_ms,detection_latency_ms,stale_lead_cancellation_ms,reacquisition_ms,peak_stale_lead,wrong_direction_lead_area,missed,false_detection_nearby\n";
    std::ostringstream reversalCsv;
    reversalCsv << "trajectory_id,scenario_id,configuration,true_reversals,detected_reversals,false_reversals,missed_reversals,first_detection_latency_ms,first_stale_lead_cancellation_ms,first_reacquisition_ms,wrong_direction_lead_area\n";
    std::ostringstream motionStateCsv;
    motionStateCsv << "trajectory_id,scenario_id,configuration,state,duration_ms,transition_count,stable_chatter,dropout_count,dropout_total_ms\n";
    std::map<std::string, Aggregate> families;
    std::map<std::string, Aggregate> configurations;
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
            {"metrics", metricsJson(result.metrics)}, {"failures", failures}});
        scenarioCsv << csv(result.scenario.id) << ',' << csv(result.scenario.family) << ',' << csv(result.configuration) << ','
            << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ','
            << result.scenario.mapperRateHz << ',' << result.scenario.sourceRateHz << ',' << result.metrics.samples << ','
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
                << result.metrics.trueReversals << ',' << result.metrics.detectedReversals << ',' << result.metrics.falseReversals << ','
                << result.metrics.missedReversals << ',' << result.metrics.reversalLatencyMs << ','
                << result.metrics.staleLeadCancellationMs << ',' << result.metrics.oppositeDirectionReacquisitionMs << ','
                << result.metrics.wrongDirectionLeadArea << '\n';
            for (size_t eventIndex = 0; eventIndex < result.reversalEvents.size(); ++eventIndex) {
                const ReversalEvent &event = result.reversalEvents[eventIndex];
                reversalEventsCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                    << eventIndex << ',' << event.groundTruthTimeSeconds * 1000.0F << ',' << event.detectedTimeSeconds * 1000.0F << ','
                    << (event.detectedTimeSeconds < 0.0F ? -1.0F : (event.detectedTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << (event.staleLeadCancellationTimeSeconds < 0.0F ? -1.0F : (event.staleLeadCancellationTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << (event.reacquisitionTimeSeconds < 0.0F ? -1.0F : (event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0F) << ','
                    << event.peakStaleLead << ',' << event.wrongDirectionLeadArea << ',' << (event.missed ? 1 : 0) << ','
                    << (event.surroundedByFalseDetection ? 1 : 0) << '\n';
            }
        }
        for (size_t state = 0; state < result.metrics.stateDurationMs.size(); ++state) {
            motionStateCsv << csv(result.scenario.trajectoryId) << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << state << ',' << result.metrics.stateDurationMs[state] << ',' << result.metrics.stateTransitions[state] << ','
                << result.metrics.stableChatter << ',' << result.metrics.dropouts << ',' << result.metrics.dropoutTotalMs << '\n';
        }
        for (const std::string &failure : result.failures) {
            const std::string severity = isHardFailure(failure) ? "hard" : isBehavioralFailure(failure) ? "behavioral" : "finding";
            failuresCsv << severity << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ',' << csv(failure) << '\n';
        }
        if (result.scenario.retainTrace || !result.failures.empty()) {
            std::ostringstream trace;
            trace << "time_seconds,intended,physical,estimated,predicted,lead,velocity,acceleration,horizon_ms,confidence,motion_intensity,state,reversal,ground_truth_moving,ground_truth_direction,source_sample_updated,source_sample_time_seconds,dt_seconds,target_arrival,physical_stop,true_reversal\n";
            for (size_t index = 0; index < result.trace.size(); ++index) {
                const TraceSample &sample = result.trace[index];
                const AdaptiveResponseTelemetry &telemetry = result.telemetry[index];
                const int direction = sample.velocity > kMotionVelocityThreshold ? 1 : sample.velocity < -kMotionVelocityThreshold ? -1 : 0;
                trace << sample.timeSeconds << ',' << sample.intended << ',' << sample.physical << ',' << telemetry.estimated << ','
                    << telemetry.predicted << ',' << telemetry.lead << ',' << telemetry.velocity << ',' << telemetry.acceleration << ','
                    << telemetry.activeHorizonSeconds * 1000.0F << ',' << telemetry.confidence << ',' << telemetry.motionIntensity << ','
                    << static_cast<int>(telemetry.state) << ',' << (telemetry.reversal ? 1 : 0) << ',' << (sample.intendedMoving ? 1 : 0)
                    << ',' << direction << ',' << (sample.sourceSampleUpdated ? 1 : 0) << ',' << sample.sourceSampleTimeSeconds << ','
                    << sample.dtSeconds << ',' << (sample.targetArrival ? 1 : 0) << ',' << (sample.physicalStop ? 1 : 0) << ','
                    << (sample.trueReversal ? 1 : 0) << '\n';
            }
            const QString traceName = QString::number(static_cast<qulonglong>(
                qHash(q(result.scenario.id + "|" + result.configuration))), 16)
                + "-" + q(result.configuration).replace(' ', '_') + ".csv";
            if (!writeText(QDir(directory).filePath("traces/" + traceName), q(trace.str()), error)) return false;
        }
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
    std::sort(worst.begin(), worst.end(), [](const TimedResult *left, const TimedResult *right) {
        return left->result.metrics.wrongDirectionLeadArea > right->result.metrics.wrongDirectionLeadArea;
    });
    std::ostringstream worstCsv;
    worstCsv << "rank,scenario_id,configuration,seed,wrong_direction_lead_area,peak_lead,dropouts,failures\n";
    for (size_t index = 0; index < std::min<size_t>(20, worst.size()); ++index) {
        const ScenarioResult &result = worst[index]->result;
        worstCsv << index + 1 << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
            << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ','
            << result.metrics.wrongDirectionLeadArea << ',' << result.metrics.peakLead << ',' << result.metrics.dropouts << ','
            << csv(failureText(result).toStdString()) << '\n';
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
        {"configurations", configurationNames}, {"summary", aggregateJson()}, {"scenarioRuns", scenarios}};
    QJsonObject summary = aggregateJson();
    summary.insert("adaptiveVerificationSchemaVersion", kAdaptiveVerificationSchemaVersion);
    summary.insert("adaptiveScenarioCatalogVersion", kAdaptiveScenarioCatalogVersion);
    summary.insert("campaign", q(options.tier));
    summary.insert("disposition", aggregate.hardFailures > 0 ? "FAIL" : aggregate.behavioralFailures > 0 ? "PASS WITH FINDINGS" : "PASS");
    std::ostringstream summaryCsv;
    summaryCsv << "campaign,scenario_runs,samples,hard_failures,behavioral_failures,findings,dropouts,false_reversals,rms_lead,peak_lead\n"
        << options.tier << ',' << aggregate.scenarios << ',' << aggregate.samples << ',' << aggregate.hardFailures << ','
        << aggregate.behavioralFailures << ',' << aggregate.findings << ',' << aggregate.dropouts << ','
        << aggregate.falseReversals << ',' << (aggregate.samples == 0 ? 0.0 : std::sqrt(aggregate.leadSquared / aggregate.samples))
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
        << "## Analysis Artifacts\n\n"
        << "Dedicated CSVs contain slow-motion, sample-and-hold, sample-rate invariance, reversal events, stops, noise, acceleration, "
        << "motion-state, model/family, performance, seed, and worst-case evidence. Preset rows are informational only; this report does not tune them.\n\n"
        << "## Performance Boundary\n\n"
        << "Offline verification throughput is reported in `performance.csv`. Production allocation evidence remains `mapping_hot_path_benchmark`; "
        << "the offline runner is not a substitute for the MappingWorker hot-path benchmark.\n";
    const IntegrationArtifacts integrations = runIntegrationHarnesses();
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
        && writeText(root + "/performance.csv", q(performanceCsv.str()), error)
        && writeText(root + "/failures.csv", q(failuresCsv.str()), error)
        && writeText(root + "/worst_cases.csv", q(worstCsv.str()), error)
        && writeText(root + "/seeds.csv", q(seedsCsv.str()), error)
        && writeText(root + "/Adaptive_Response_V2.3.T_Phase1_Verification_Report.md", q(report.str()), error);
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
    const std::vector<TraceSample> trace = generateTrace(scenario);
    if (retainSamples) {
        result.trace = trace;
        result.telemetry.reserve(trace.size());
    }
    AdaptiveResponseProcessor processor;
    const auto origin = Clock::time_point{};
    int previousTruthDirection = 0;
    int activeTruthDirection = 0;
    int oldTruthDirection = 0;
    int pendingReversal = -1;
    float stopTime = -1.0F;
    float stopTarget = 0.0F;
    int stopApproachDirection = 0;
    float stableAfterStop = -1.0F;
    float settledAfterStop = -1.0F;
    float firstMotionTime = -1.0F;
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
    const float reversalDeadline = std::max(3.0F / static_cast<float>(std::max(1, scenario.mapperRateHz)),
        sourcePeriod * 2.5F);
    for (size_t index = 0; index < trace.size(); ++index) {
        const TraceSample &sample = trace[index];
        const auto timestamp = origin + std::chrono::microseconds(static_cast<long long>(sample.timeSeconds * 1000000.0F));
        const AdaptiveResponseTelemetry telemetry = processor.process(sample.physical, configuration, timestamp);
        if (retainSamples) result.telemetry.push_back(telemetry);
        ++result.metrics.samples;
        const int truthDirection = sample.velocity > kGroundTruthReversalVelocityThreshold ? 1
            : sample.velocity < -kGroundTruthReversalVelocityThreshold ? -1 : 0;
        if (truthDirection != 0) {
            if (previousTruthDirection != 0 && truthDirection != previousTruthDirection) {
                if (pendingReversal >= 0 && result.reversalEvents[static_cast<size_t>(pendingReversal)].detectedTimeSeconds < 0.0F) {
                    result.reversalEvents[static_cast<size_t>(pendingReversal)].missed = true;
                    ++result.metrics.missedReversals;
                }
                ++result.metrics.trueReversals;
                oldTruthDirection = previousTruthDirection;
                activeTruthDirection = truthDirection;
                result.reversalEvents.push_back({sample.timeSeconds});
                pendingReversal = static_cast<int>(result.reversalEvents.size() - 1U);
            }
            previousTruthDirection = truthDirection;
        }
        if (telemetry.reversal) {
            ++result.metrics.detectedReversals;
            if (pendingReversal >= 0) {
                ReversalEvent &event = result.reversalEvents[static_cast<size_t>(pendingReversal)];
                if (event.detectedTimeSeconds < 0.0F
                    && sample.timeSeconds - event.groundTruthTimeSeconds <= reversalDeadline) {
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
                && sample.timeSeconds - event.groundTruthTimeSeconds > reversalDeadline) {
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
            if (std::abs(telemetry.lead) > 0.002F && (scenario.family == "stationary" || scenario.family == "noise")) {
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
            stopApproachDirection = previousTruthDirection;
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
    for (const ReversalEvent &event : result.reversalEvents) {
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
    if (configuration.enabled && result.metrics.missedReversals > 0) {
        result.failures.push_back("BEHAVIOR: physical reversal was not reacquired within the source/rate-scaled window");
    }
    if (result.metrics.dropouts > 2 && scenario.family != "noise") result.failures.push_back("BEHAVIOR: prediction dropout during intended motion");
    if (result.metrics.falseStops > 2 && scenario.family != "noise") result.failures.push_back("BEHAVIOR: false Stable state during intended motion");
    if (result.metrics.wrongDirectionLeadArea > 0.004) result.failures.push_back("FINDING: elevated stale-direction lead area");
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
            && std::abs(reversalResult.reversalEvents.front().groundTruthTimeSeconds - 0.2F) < 0.005F,
        "Known synthetic reversal ground truth timestamp was not identified.");
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
        TimedResult forced{offResult, 1.0};
        forced.result.failures = {"HARD: forced failure trace retention fixture"};
        QString artifactError;
        record(writeArtifacts(options, temporary.path(), {forced}, &artifactError),
            "Result schema fixture could not write artifacts: " + artifactError);
        QFile campaign(QDir(temporary.path()).filePath("campaign.json"));
        record(campaign.exists() && campaign.open(QIODevice::ReadOnly), "Campaign artifact is missing.");
        if (campaign.isOpen()) {
            const QJsonObject object = QJsonDocument::fromJson(campaign.readAll()).object();
            record(object.value("adaptiveVerificationSchemaVersion").toInt() == kAdaptiveVerificationSchemaVersion
                    && object.contains("scenarioRuns") && object.contains("summary"),
                "Campaign artifact lacks required schema fields.");
        }
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
            << " [--scenario substring] [--model auto|all|presets] [--sample-rate Hz] [--output directory]\n"
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
            // Full/Torture summaries remain bounded, while any failure is
            // immediately replayed deterministically with forensic samples.
            if (!retainSamples && !result.failures.empty()) {
                result = replayScenario(*task.scenario, task.configuration->value, task.configuration->name, true);
            }
            const auto finished = Clock::now();
            results[index] = {std::move(result), std::chrono::duration<double, std::micro>(finished - started).count()};
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
    const QString outputDirectory = options.outputDirectory.isEmpty() ? defaultOutputDirectory(options) : options.outputDirectory;
    QString writeError;
    if (!writeArtifacts(options, outputDirectory, results, &writeError)) {
        std::cerr << "Unable to write artifacts: " << writeError.toStdString() << '\n';
        return 4;
    }
    Aggregate aggregate;
    for (const TimedResult &result : results) addAggregate(aggregate, result.result);
    const char *disposition = aggregate.hardFailures > 0 ? "FAIL" : aggregate.behavioralFailures > 0 ? "PASS WITH FINDINGS" : "PASS";
    std::cout << "Adaptive Response V2.3.T verification " << disposition << " campaign=" << options.tier
        << " scenario_runs=" << aggregate.scenarios << " samples=" << aggregate.samples
        << " hard_failures=" << aggregate.hardFailures << " behavioral_failures=" << aggregate.behavioralFailures
        << " artifacts=" << outputDirectory.toStdString() << '\n';
    return aggregate.hardFailures > 0 ? 5 : 0;
}

} // namespace hotas::verification
