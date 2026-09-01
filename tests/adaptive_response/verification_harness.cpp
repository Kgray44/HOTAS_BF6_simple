#include "verification_harness.h"

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
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
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
        {"rmsLead", metrics.rmsLead}, {"peakLead", metrics.peakLead},
        {"meanHorizonMs", metrics.meanHorizonMs}, {"peakHorizonMs", metrics.peakHorizonMs},
        {"meanConfidence", metrics.meanConfidence}, {"wrongDirectionLeadArea", metrics.wrongDirectionLeadArea},
        {"noiseRms", metrics.noiseRms}, {"predictedNoiseRms", metrics.predictedNoiseRms},
        {"maximumOutputStep", metrics.maximumOutputStep}, {"stopRecognitionMs", metrics.stopRecognitionMs},
        {"settlingMs", metrics.settlingMs}, {"reversalLatencyMs", metrics.reversalLatencyMs},
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
        const std::set<std::string> families{"stationary", "slow-motion", "reversal", "stop", "sample-hold", "noise", "timing"};
        std::set<std::string> included;
        for (const ScenarioDefinition &scenario : catalog) {
            if (families.contains(scenario.family) && included.insert(scenario.family).second) add(scenario);
        }
        return result;
    }
    for (const ScenarioDefinition &scenario : catalog) {
        if (options.tier == "canonical" && scenario.family == "randomized") continue;
        add(scenario);
    }
    const int randomCount = options.tier == "full" ? 5000 : options.tier == "torture" ? 512 : 0;
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
        for (const std::string &failure : result.failures) {
            const std::string severity = isHardFailure(failure) ? "hard" : isBehavioralFailure(failure) ? "behavioral" : "finding";
            failuresCsv << severity << ',' << csv(result.scenario.id) << ',' << csv(result.configuration) << ','
                << "0x" << std::hex << std::uppercase << result.scenario.seed << std::dec << ',' << csv(failure) << '\n';
        }
        if (result.scenario.retainTrace || !result.failures.empty()) {
            std::ostringstream trace;
            trace << "time_seconds,intended,physical,predicted,lead,horizon_ms,confidence,state,reversal\n";
            for (size_t index = 0; index < result.trace.size(); ++index) {
                const TraceSample &sample = result.trace[index];
                const AdaptiveResponseTelemetry &telemetry = result.telemetry[index];
                trace << sample.timeSeconds << ',' << sample.intended << ',' << sample.physical << ',' << telemetry.predicted << ','
                    << telemetry.lead << ',' << telemetry.activeHorizonSeconds * 1000.0F << ',' << telemetry.confidence << ','
                    << static_cast<int>(telemetry.state) << ',' << (telemetry.reversal ? 1 : 0) << '\n';
            }
            const QString traceName = QString::number(static_cast<qulonglong>(
                qHash(q(result.scenario.id + "|" + result.configuration))), 16)
                + "-" + q(result.configuration).replace(' ', '_') + ".csv";
            if (!writeText(QDir(directory).filePath("traces/" + traceName), q(trace.str()), error)) return false;
        }
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
    QJsonObject campaign{{"adaptiveVerificationSchemaVersion", kAdaptiveVerificationSchemaVersion}, {"campaign", q(options.tier)},
        {"masterSeed", QString::asprintf("0x%08X", options.masterSeed)}, {"generatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"command", QStringLiteral("adaptive_response_verification --campaign %1 --seed 0x%2")
             .arg(q(options.tier), QString::number(options.masterSeed, 16).toUpper())},
        {"sourceCommit", QStringLiteral(HOTAS_VERIFICATION_SOURCE_COMMIT)},
        {"sourceBranch", QStringLiteral(HOTAS_VERIFICATION_SOURCE_BRANCH)},
        {"applicationVersion", QStringLiteral(HOTAS_VERIFICATION_APPLICATION_VERSION)},
        {"summary", aggregateJson()}, {"scenarioRuns", scenarios}};
    QJsonObject summary = aggregateJson();
    summary.insert("adaptiveVerificationSchemaVersion", kAdaptiveVerificationSchemaVersion);
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
    return writeText(root + "/campaign.json", QString::fromUtf8(QJsonDocument(campaign).toJson(QJsonDocument::Indented)), error)
        && writeText(root + "/summary.json", QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented)), error)
        && writeText(root + "/summary.csv", q(summaryCsv.str()), error)
        && writeText(root + "/scenario_results.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/family_summary.csv", q(aggregatesCsv(families, "family")), error)
        && writeText(root + "/model_summary.csv", q(aggregatesCsv(configurations, "configuration")), error)
        && writeText(root + "/sample_rate_invariance.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/slow_motion_metrics.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/sample_hold_metrics.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/reversal_metrics.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/stop_metrics.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/noise_metrics.csv", q(scenarioCsv.str()), error)
        && writeText(root + "/lifecycle_metrics.csv", q("scope,status\nlifecycle,planned-control-plane-integration\n"), error)
        && writeText(root + "/bumpless_metrics.csv", q("scope,status\nbumpless,covered-by-mapping-core-tests\n"), error)
        && writeText(root + "/performance.csv", q(performanceCsv.str()), error)
        && writeText(root + "/failures.csv", q(failuresCsv.str()), error)
        && writeText(root + "/worst_cases.csv", q(worstCsv.str()), error)
        && writeText(root + "/seeds.csv", q(seedsCsv.str()), error);
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
                              const std::string &configurationName)
{
    ScenarioResult result;
    result.scenario = scenario;
    result.configuration = configurationName;
    RuntimeAdaptiveResponseConfig configuration = providedConfiguration;
    configuration.domainMinimum = scenario.unipolar ? 0.0F : -1.0F;
    configuration.domainMaximum = 1.0F;
    result.trace = generateTrace(scenario);
    result.telemetry.reserve(result.trace.size());
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
    double sourceCadenceErrorSeconds = 0.0;
    std::vector<float> absoluteLeads;
    std::vector<float> horizons;
    absoluteLeads.reserve(result.trace.size());
    horizons.reserve(result.trace.size());
    bool lastStable = true;
    bool wasMoving = false;
    AdaptiveMotionState lastState = AdaptiveMotionState::Stable;
    const float sourcePeriod = 1.0F / static_cast<float>(std::max(1,
        scenario.sourceRateHz > 0 ? scenario.sourceRateHz : scenario.mapperRateHz));
    const float reversalDeadline = std::max(3.0F / static_cast<float>(std::max(1, scenario.mapperRateHz)),
        sourcePeriod * 2.5F);
    for (size_t index = 0; index < result.trace.size(); ++index) {
        const TraceSample &sample = result.trace[index];
        const auto timestamp = origin + std::chrono::microseconds(static_cast<long long>(sample.timeSeconds * 1000000.0F));
        const AdaptiveResponseTelemetry telemetry = processor.process(sample.physical, configuration, timestamp);
        result.telemetry.push_back(telemetry);
        ++result.metrics.samples;
        const int truthDirection = sample.velocity > kMotionVelocityThreshold ? 1
            : sample.velocity < -kMotionVelocityThreshold ? -1 : 0;
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
    result.metrics.effectiveSourceRateHz = scenario.durationSeconds <= 0.0F ? 0.0
        : result.metrics.sourceUpdateCount / scenario.durationSeconds;
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
    if (result.metrics.missedReversals > 0) result.failures.push_back("BEHAVIOR: physical reversal was not reacquired within the source/rate-scaled window");
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
    const auto read = [error](const QString &directory) -> QJsonObject {
        QFile file(QDir(directory).filePath("summary.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Cannot open summary.json in %1.").arg(directory);
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
    const QJsonObject baseline = read(baselineDirectory);
    const QJsonObject candidate = read(candidateDirectory);
    if (baseline.isEmpty() || candidate.isEmpty()) return false;
    if (baseline.value("adaptiveVerificationSchemaVersion").toInt() != kAdaptiveVerificationSchemaVersion
        || candidate.value("adaptiveVerificationSchemaVersion").toInt() != kAdaptiveVerificationSchemaVersion) {
        if (error) *error = "Campaign schemas are incompatible.";
        return false;
    }
    const auto percent = [&baseline, &candidate](const char *key) {
        const double before = baseline.value(key).toDouble();
        const double after = candidate.value(key).toDouble();
        return before == 0.0 ? 0.0 : (after - before) / before * 100.0;
    };
    QString report = QStringLiteral("# Adaptive Response verification comparison\n\nCompatible schema: %1\n\n"
        "| Metric | Baseline | Candidate | Change |\n|---|---:|---:|---:|\n")
        .arg(kAdaptiveVerificationSchemaVersion);
    for (const char *key : {"hardFailures", "behavioralFailures", "dropouts", "falseReversals", "rmsLead", "peakLead"}) {
        report += QStringLiteral("| %1 | %2 | %3 | %4% |\n").arg(QString::fromLatin1(key))
            .arg(baseline.value(key).toDouble()).arg(candidate.value(key).toDouble()).arg(percent(key), 0, 'f', 2);
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
    std::vector<TimedResult> results;
    results.reserve(scenarios.size() * configurations.size());
    for (const ScenarioDefinition &scenario : scenarios) {
        for (const NamedConfiguration &configuration : configurations) {
            const auto started = Clock::now();
            ScenarioResult result = replayScenario(scenario, configuration.value, configuration.name);
            const auto finished = Clock::now();
            results.push_back({std::move(result), std::chrono::duration<double, std::micro>(finished - started).count()});
        }
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
