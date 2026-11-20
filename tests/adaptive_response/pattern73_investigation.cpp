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
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

constexpr float kLeadTolerance = 0.00001F;

struct NamedConfiguration {
    std::string name;
    RuntimeAdaptiveResponseConfig value;
};

struct DiagnosticRun {
    std::vector<TraceSample> trace;
    std::vector<AdaptiveResponseTelemetry> telemetry;
    std::vector<AdaptiveResponseDiagnosticSnapshot> diagnostic;
};

struct EventTimes {
    float humanIntent = -1.0F;
    float firstSourceOpposite = -1.0F;
    float firstMapperOpposite = -1.0F;
    float observedDevice = -1.0F;
    float safetyCancellation = -1.0F;
    float fullReversal = -1.0F;
    float oppositeLead = -1.0F;
};

struct PhysicalSample {
    std::uint64_t timestampUs = 0;
    float physical = 0.0F;
};

struct SimilarSegment {
    QString traceId;
    QString sourceFile;
    int sampleIndex = -1;
    float timeSeconds = -1.0F;
    float priorVelocity = 0.0F;
    float firstOppositeVelocity = 0.0F;
    float ratio = 0.0F;
    float laterOppositeVelocity = 0.0F;
};

QString csv(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return u"\""_qs + escaped + u"\""_qs;
}

QString csv(const std::string &value) { return csv(QString::fromStdString(value)); }
QString csv(const char *value) { return csv(QString::fromUtf8(value)); }

bool writeText(const QString &path, const QString &contents, QString *error = nullptr)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream << contents;
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
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

std::vector<NamedConfiguration> activeConfigurations()
{
    std::vector<NamedConfiguration> configurations;
    configurations.push_back({"Velocity", verificationConfiguration(AdaptiveResponseModel::Velocity)});
    configurations.push_back({"Alpha-Beta", verificationConfiguration(AdaptiveResponseModel::AlphaBeta)});
    configurations.push_back({"Alpha-Beta-Gamma", verificationConfiguration(AdaptiveResponseModel::AlphaBetaGamma)});
    configurations.push_back({"Auto", verificationConfiguration(AdaptiveResponseModel::Auto)});
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        if (preset.name == u"Light"_qs || preset.name == u"Balanced"_qs || preset.name == u"Fast"_qs
            || preset.name == u"Aggressive"_qs || preset.name == u"Extreme"_qs) {
            configurations.push_back({"Preset " + preset.name.toStdString(), runtimeForSettings(preset.axes[0].settings)});
        }
    }
    return configurations;
}

const ScenarioDefinition *findScenario(const std::vector<ScenarioDefinition> &catalog, const std::string &id)
{
    const auto found = std::find_if(catalog.cbegin(), catalog.cend(), [&id](const ScenarioDefinition &scenario) {
        return scenario.id == id;
    });
    return found == catalog.cend() ? nullptr : &*found;
}

DiagnosticRun runDiagnostic(const ScenarioDefinition &scenario, const RuntimeAdaptiveResponseConfig &configuration)
{
    DiagnosticRun run;
    run.trace = generateTrace(scenario);
    run.telemetry.reserve(run.trace.size());
    run.diagnostic.reserve(run.trace.size());
    AdaptiveResponseProcessor processor;
    const auto origin = std::chrono::steady_clock::time_point{};
    for (const TraceSample &sample : run.trace) {
        const auto timestamp = origin + std::chrono::microseconds(
            static_cast<long long>(sample.timeSeconds * 1000000.0F));
        run.telemetry.push_back(processor.process(sample.physical, configuration, timestamp));
        run.diagnostic.push_back(processor.diagnosticSnapshot());
    }
    return run;
}

const ReversalEvent *firstConveyedEvent(const ScenarioResult &result)
{
    const auto found = std::find_if(result.reversalEvents.cbegin(), result.reversalEvents.cend(),
        [](const ReversalEvent &event) { return event.conveysHumanIntent; });
    return found == result.reversalEvents.cend() ? nullptr : &*found;
}

EventTimes findEventTimes(const DiagnosticRun &run, const ScenarioResult &result)
{
    EventTimes times;
    const ReversalEvent *event = firstConveyedEvent(result);
    if (event != nullptr) times.observedDevice = event->groundTruthTimeSeconds;
    for (size_t index = 1; index < run.trace.size(); ++index) {
        const TraceSample &sample = run.trace[index];
        const float delta = sample.physical - run.trace[index - 1U].physical;
        if (times.humanIntent < 0.0F && sample.humanIntentReversal) times.humanIntent = sample.timeSeconds;
        if (times.humanIntent >= 0.0F && sample.timeSeconds >= times.humanIntent && sample.sourceSampleUpdated
            && delta < -0.0000001F && times.firstSourceOpposite < 0.0F) {
            times.firstSourceOpposite = sample.timeSeconds;
        }
        if (times.humanIntent >= 0.0F && sample.timeSeconds >= times.humanIntent && delta < -0.0000001F
            && times.firstMapperOpposite < 0.0F) {
            times.firstMapperOpposite = sample.timeSeconds;
        }
        if (times.humanIntent >= 0.0F && sample.timeSeconds >= times.humanIntent
            && run.diagnostic[index].safetyCancellation && times.safetyCancellation < 0.0F) {
            times.safetyCancellation = sample.timeSeconds;
        }
        if (times.observedDevice >= 0.0F && sample.timeSeconds >= times.observedDevice
            && run.telemetry[index].reversal && times.fullReversal < 0.0F) {
            times.fullReversal = sample.timeSeconds;
        }
        if (times.observedDevice >= 0.0F && sample.timeSeconds >= times.observedDevice
            && run.telemetry[index].lead < -kLeadTolerance && times.oppositeLead < 0.0F) {
            times.oppositeLead = sample.timeSeconds;
        }
    }
    return times;
}

bool isSubstantiveMiss(const ScenarioResult &result)
{
    return std::any_of(result.reversalEvents.cbegin(), result.reversalEvents.cend(), [](const ReversalEvent &event) {
        return event.conveysHumanIntent && event.predictorMissed && std::abs(event.humanIntentVelocity) >= 0.075F;
    });
}

bool hasPromptStaleLeadCancellation(const ScenarioResult &result)
{
    const float mapperPeriod = 1.0F / static_cast<float>(std::max(1, result.scenario.mapperRateHz));
    const float sourcePeriod = 1.0F / static_cast<float>(std::max(1,
        result.scenario.sourceRateHz > 0 ? result.scenario.sourceRateHz : result.scenario.mapperRateHz));
    const float promptDeadline = std::max(mapperPeriod, sourcePeriod) + 0.0005F;
    return std::any_of(result.reversalEvents.cbegin(), result.reversalEvents.cend(),
        [promptDeadline](const ReversalEvent &event) {
            return event.conveysHumanIntent && event.predictorMissed
                && std::abs(event.humanIntentVelocity) >= 0.075F
                && event.staleLeadCancellationTimeSeconds >= 0.0F
                && event.staleLeadCancellationTimeSeconds - event.groundTruthTimeSeconds <= promptDeadline
                && event.wrongDirectionLeadArea <= 0.0001;
        });
}

QString oldClassification(const ScenarioResult &result)
{
    return isSubstantiveMiss(result) ? u"PRODUCT_SUSPECT"_qs : u"not-triggered"_qs;
}

QString correctedClassification(const ScenarioResult &result)
{
    if (!isSubstantiveMiss(result)) return u"not-triggered"_qs;
    return hasPromptStaleLeadCancellation(result) ? u"HARNESS"_qs : u"PRODUCT_SUSPECT"_qs;
}

QString scenarioFingerprint(const ScenarioDefinition &scenario)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(scenario.id.data(), static_cast<qsizetype>(scenario.id.size()));
    hash.addData(reinterpret_cast<const char *>(&scenario.mapperRateHz), sizeof(scenario.mapperRateHz));
    hash.addData(reinterpret_cast<const char *>(&scenario.sourceRateHz), sizeof(scenario.sourceRateHz));
    for (const ControlPoint &point : scenario.points) {
        hash.addData(reinterpret_cast<const char *>(&point.timeSeconds), sizeof(point.timeSeconds));
        hash.addData(reinterpret_cast<const char *>(&point.position), sizeof(point.position));
    }
    return QString::fromLatin1(hash.result().toHex());
}

ScenarioDefinition makeProbe(const ScenarioDefinition &base, const std::string &id, float turnTime,
                             float initialReverseSpeed, int sourceRate)
{
    ScenarioDefinition probe = base;
    probe.id = id;
    probe.trajectoryId = id;
    probe.sourceRateHz = sourceRate;
    const float initialEnd = 0.80F - initialReverseSpeed * (0.55F - turnTime);
    probe.points = {{0.0F, 0.0F}, {0.15F, 0.50F}, {turnTime, 0.80F}, {0.55F, initialEnd},
                    {0.80F, 0.65F}, {1.0F, 0.35F}, {1.2F, 0.0F}, {1.7F, 0.0F}};
    return probe;
}

std::optional<std::vector<PhysicalSample>> readPhysicalTrace(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return std::nullopt;
    QTextStream input(&file);
    bool headerSeen = false;
    std::vector<PhysicalSample> samples;
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (!headerSeen) {
            if (line != u"timestamp_us,raw_normalized,calibrated_normalized,source_reported,source_changed,elapsed_us,effective_cadence_hz,raw_direct_input"_qs) {
                return std::nullopt;
            }
            headerSeen = true;
            continue;
        }
        const QStringList fields = line.split(',');
        bool timeOk = false;
        bool physicalOk = false;
        const std::uint64_t timestamp = fields.value(0).toULongLong(&timeOk);
        const float physical = fields.value(2).toFloat(&physicalOk);
        if (fields.size() != 8 || !timeOk || !physicalOk || !std::isfinite(physical)
            || (!samples.empty() && timestamp <= samples.back().timestampUs)) return std::nullopt;
        samples.push_back({timestamp, physical});
    }
    if (!headerSeen || samples.size() < 4) return std::nullopt;
    return samples;
}

std::optional<SimilarSegment> findClosestSegment(const QString &traceId, const QString &sourceFile,
                                                 const std::vector<PhysicalSample> &samples)
{
    std::optional<SimilarSegment> best;
    const std::uint64_t origin = samples.front().timestampUs;
    // Pattern-73 has an established estimator trend before its reversal.
    // Exclude startup samples, where a direction flip cannot be structurally
    // comparable because the frozen processor is still initializing.
    for (size_t index = 10; index + 2 < samples.size(); ++index) {
        const float priorDt = static_cast<float>(samples[index - 1U].timestampUs - samples[index - 2U].timestampUs) / 1000000.0F;
        const float currentDt = static_cast<float>(samples[index].timestampUs - samples[index - 1U].timestampUs) / 1000000.0F;
        const float priorVelocity = (samples[index - 1U].physical - samples[index - 2U].physical) / std::max(0.000001F, priorDt);
        const float currentVelocity = (samples[index].physical - samples[index - 1U].physical) / std::max(0.000001F, currentDt);
        if (priorVelocity <= 0.25F || currentVelocity >= -0.02F) continue;
        const float ratio = std::abs(currentVelocity) / std::max(0.000001F, std::abs(priorVelocity));
        if (ratio > 0.50F) continue;
        float laterVelocity = 0.0F;
        for (size_t later = index + 1U; later < samples.size(); ++later) {
            if (samples[later].timestampUs - samples[index].timestampUs > 500000U) break;
            const float dt = static_cast<float>(samples[later].timestampUs - samples[later - 1U].timestampUs) / 1000000.0F;
            const float velocity = (samples[later].physical - samples[later - 1U].physical) / std::max(0.000001F, dt);
            if (velocity <= -0.25F) { laterVelocity = velocity; break; }
        }
        if (laterVelocity == 0.0F) continue;
        SimilarSegment candidate;
        candidate.traceId = traceId;
        candidate.sourceFile = sourceFile;
        candidate.sampleIndex = static_cast<int>(index);
        candidate.timeSeconds = static_cast<float>(samples[index].timestampUs - origin) / 1000000.0F;
        candidate.priorVelocity = priorVelocity;
        candidate.firstOppositeVelocity = currentVelocity;
        candidate.ratio = ratio;
        candidate.laterOppositeVelocity = laterVelocity;
        if (!best || std::abs(candidate.ratio - 0.04F) < std::abs(best->ratio - 0.04F)) best = candidate;
    }
    return best;
}

struct PhysicalReplaySummary {
    float safetyCancellationMs = -1.0F;
    float reversalMs = -1.0F;
    float oppositeLeadMs = -1.0F;
    float wrongDirectionLeadArea = 0.0F;
};

PhysicalReplaySummary replayPhysicalSegment(const std::vector<PhysicalSample> &samples, const SimilarSegment &segment,
                                            const RuntimeAdaptiveResponseConfig &configuration)
{
    PhysicalReplaySummary summary;
    AdaptiveResponseProcessor processor;
    const std::uint64_t origin = samples.front().timestampUs;
    const std::uint64_t eventTime = samples[static_cast<size_t>(segment.sampleIndex)].timestampUs;
    const auto zero = std::chrono::steady_clock::time_point{};
    for (size_t index = 0; index < samples.size(); ++index) {
        const auto telemetry = processor.process(samples[index].physical, configuration,
            zero + std::chrono::microseconds(samples[index].timestampUs - origin));
        const auto diagnostic = processor.diagnosticSnapshot();
        if (samples[index].timestampUs < eventTime) continue;
        const float elapsedMs = static_cast<float>(samples[index].timestampUs - eventTime) / 1000.0F;
        if (diagnostic.safetyCancellation && summary.safetyCancellationMs < 0.0F) summary.safetyCancellationMs = elapsedMs;
        if (telemetry.reversal && summary.reversalMs < 0.0F) summary.reversalMs = elapsedMs;
        if (telemetry.lead < -kLeadTolerance && summary.oppositeLeadMs < 0.0F) summary.oppositeLeadMs = elapsedMs;
        if (telemetry.lead > kLeadTolerance) {
            const float dt = index + 1U < samples.size()
                ? static_cast<float>(samples[index + 1U].timestampUs - samples[index].timestampUs) / 1000000.0F : 0.0F;
            summary.wrongDirectionLeadArea += telemetry.lead * dt;
        }
    }
    return summary;
}

void appendDiagnosticTrace(QString *output, const DiagnosticRun &run)
{
    *output += u"time_ms,dt_ms,physical,normalized_measurement,previous_physical,measured_delta,estimated,estimated_velocity,estimated_acceleration,velocity_sign,acceleration_sign,motion_intensity,motion_state,confidence,active_horizon_ms,raw_predicted,bounded_predicted,final_output,final_prediction_lead,lead_sign,braking,safety_cancellation,source_braking,braking_reduction,raw_direction,motion_direction_before,motion_direction_after,opposite_evidence,soft_reversal_motion,hard_reversal_threshold,soft_reversal_threshold,hard_reversal,soft_reversal,predictor_reversal,source_updated,source_update_period_ms,quiet_duration_ms,hold_confidence,hold_ratio\n"_qs;
    for (size_t index = 0; index < run.trace.size(); ++index) {
        const TraceSample &sample = run.trace[index];
        if (sample.timeSeconds < 0.260F || sample.timeSeconds > 0.640F) continue;
        const auto &telemetry = run.telemetry[index];
        const auto &diagnostic = run.diagnostic[index];
        const auto sign = [](float value) { return value > kLeadTolerance ? 1 : value < -kLeadTolerance ? -1 : 0; };
        QTextStream line(output);
        line.setRealNumberNotation(QTextStream::FixedNotation);
        line.setRealNumberPrecision(7);
        line << sample.timeSeconds * 1000.0F << ',' << diagnostic.dtSeconds * 1000.0F << ',' << sample.physical << ','
             << diagnostic.normalizedMeasurement << ',' << diagnostic.previousPhysical << ',' << diagnostic.measuredDelta << ','
             << telemetry.estimated << ',' << telemetry.velocity << ',' << telemetry.acceleration << ','
             << sign(telemetry.velocity) << ',' << sign(telemetry.acceleration) << ',' << telemetry.motionIntensity << ','
             << static_cast<int>(telemetry.state) << ',' << telemetry.confidence << ',' << telemetry.activeHorizonSeconds * 1000.0F << ','
             << diagnostic.rawPredicted << ',' << telemetry.predicted << ',' << telemetry.predicted << ',' << telemetry.lead << ','
             << sign(telemetry.lead) << ',' << (diagnostic.braking ? 1 : 0) << ',' << (diagnostic.safetyCancellation ? 1 : 0) << ','
             << (diagnostic.sourceBrakingDetected ? 1 : 0) << ',' << diagnostic.brakingReductionFactor << ','
             << diagnostic.rawDirection << ',' << diagnostic.motionDirectionBefore << ',' << diagnostic.motionDirectionAfter << ','
             << static_cast<int>(diagnostic.oppositeEvidenceCount) << ',' << diagnostic.softReversalMotion << ','
             << diagnostic.hardReversalDisplacement << ',' << diagnostic.softReversalThreshold << ','
             << (diagnostic.hardReversal ? 1 : 0) << ',' << (diagnostic.softReversal ? 1 : 0) << ','
             << (telemetry.reversal ? 1 : 0) << ',' << (diagnostic.sourceUpdated ? 1 : 0) << ','
             << diagnostic.sourceUpdatePeriodSeconds * 1000.0F << ',' << diagnostic.quietDurationSeconds * 1000.0F << ','
             << diagnostic.holdConfidence << ',' << diagnostic.holdRatio << '\n';
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Single-case Pattern-73 diagnostic. It is offline and does not open DirectInput, vJoy, mapper, or UI."_qs);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty artifact directory."_qs, u"directory"_qs);
    const QCommandLineOption physicalOption(u"physical-corpus"_qs, u"Required directory containing the 15 recorded .physical.csv traces."_qs, u"directory"_qs);
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
        QTextStream(stderr) << "Output directory must be empty to preserve diagnostic provenance.\n";
        return 2;
    }
    if (!QDir().mkpath(outputPath)) {
        QTextStream(stderr) << "Unable to create output directory.\n";
        return 3;
    }

    const auto catalog = canonicalScenarioCatalog(kDefaultMasterSeed);
    const ScenarioDefinition *exact = findScenario(catalog, "reversal/pattern-73");
    if (exact == nullptr || exact->seed != 0x2268D497U) {
        QTextStream(stderr) << "Pattern-73 identity was not found with its frozen seed.\n";
        return 4;
    }
    const auto configurations = activeConfigurations();
    if (configurations.size() != 9U) {
        QTextStream(stderr) << "Expected nine active model/preset configurations.\n";
        return 4;
    }

    const NamedConfiguration &velocity = configurations.front();
    const ScenarioResult exactVelocity = replayScenario(*exact, velocity.value, velocity.name);
    const DiagnosticRun exactDiagnostic = runDiagnostic(*exact, velocity.value);
    const EventTimes exactTimes = findEventTimes(exactDiagnostic, exactVelocity);
    if (exactTimes.humanIntent < 0.0F || exactTimes.observedDevice < 0.0F || exactTimes.safetyCancellation < 0.0F
        || exactTimes.fullReversal < 0.0F || exactTimes.oppositeLead < 0.0F) {
        QTextStream(stderr) << "Exact diagnostic event chain was incomplete.\n";
        return 5;
    }

    QString traceCsv;
    appendDiagnosticTrace(&traceCsv, exactDiagnostic);
    QString error;
    if (!writeText(output.filePath(u"pattern73_sample_trace.csv"_qs), traceCsv, &error)) {
        QTextStream(stderr) << "Unable to write sample trace: " << error << '\n';
        return 6;
    }

    QString modelsCsv = u"configuration,human_intent_ms,observed_device_ms,safety_cancellation_ms,full_reversal_ms,opposite_lead_ms,event_stale_cancellation_ms,event_reacquisition_ms,predictor_missed,wrong_direction_lead_area,peak_stale_lead,old_classification,corrected_classification\n"_qs;
    QString reclassificationCsv = u"configuration,failure,old_classification,corrected_classification,prompt_stale_cancellation,wrong_direction_lead_area\n"_qs;
    int oldSuspects = 0;
    int correctedSuspects = 0;
    int correctedHarness = 0;
    for (const NamedConfiguration &configuration : configurations) {
        const ScenarioResult result = replayScenario(*exact, configuration.value, configuration.name);
        const DiagnosticRun diagnostic = runDiagnostic(*exact, configuration.value);
        const EventTimes times = findEventTimes(diagnostic, result);
        const ReversalEvent *event = firstConveyedEvent(result);
        const QString oldClass = oldClassification(result);
        const QString correctedClass = correctedClassification(result);
        if (oldClass == u"PRODUCT_SUSPECT"_qs) ++oldSuspects;
        if (correctedClass == u"PRODUCT_SUSPECT"_qs) ++correctedSuspects;
        if (correctedClass == u"HARNESS"_qs) ++correctedHarness;
        QTextStream models(&modelsCsv);
        models.setRealNumberNotation(QTextStream::FixedNotation);
        models.setRealNumberPrecision(4);
        models << csv(configuration.name) << ',' << times.humanIntent * 1000.0F << ',' << times.observedDevice * 1000.0F << ','
               << times.safetyCancellation * 1000.0F << ',' << times.fullReversal * 1000.0F << ',' << times.oppositeLead * 1000.0F << ','
               << (event == nullptr ? -1.0F : (event->staleLeadCancellationTimeSeconds - event->groundTruthTimeSeconds) * 1000.0F) << ','
               << (event == nullptr ? -1.0F : (event->reacquisitionTimeSeconds - event->groundTruthTimeSeconds) * 1000.0F) << ','
               << (event != nullptr && event->predictorMissed ? 1 : 0) << ',' << (event == nullptr ? 0.0 : event->wrongDirectionLeadArea) << ','
               << (event == nullptr ? 0.0F : event->peakStaleLead) << ',' << csv(oldClass) << ',' << csv(correctedClass) << '\n';
        for (const std::string &failure : result.failures) {
            QTextStream row(&reclassificationCsv);
            row << csv(configuration.name) << ',' << csv(failure) << ',' << csv(oldClass) << ',' << csv(correctedClass) << ','
                << (hasPromptStaleLeadCancellation(result) ? 1 : 0) << ',' << (event == nullptr ? 0.0 : event->wrongDirectionLeadArea) << '\n';
        }
    }
    if (!writeText(output.filePath(u"model_comparison.csv"_qs), modelsCsv, &error)
        || !writeText(output.filePath(u"classifier_reclassification.csv"_qs), reclassificationCsv, &error)) {
        QTextStream(stderr) << "Unable to write model/classifier evidence: " << error << '\n';
        return 6;
    }

    QString neighborCsv = u"scenario,configuration,human_intent_ms,observed_device_ms,full_reversal_ms,opposite_lead_ms,predictor_missed,wrong_direction_lead_area,failures\n"_qs;
    for (const char *id : {"reversal/pattern-69", "reversal/pattern-70", "reversal/pattern-71", "reversal/pattern-72", "reversal/pattern-73", "reversal/false-bait"}) {
        const ScenarioDefinition *scenario = findScenario(catalog, id);
        if (scenario == nullptr) return 4;
        const ScenarioResult result = replayScenario(*scenario, velocity.value, velocity.name);
        const DiagnosticRun diagnostic = runDiagnostic(*scenario, velocity.value);
        const EventTimes times = findEventTimes(diagnostic, result);
        const ReversalEvent *event = firstConveyedEvent(result);
        QString failures;
        for (size_t index = 0; index < result.failures.size(); ++index) {
            if (index != 0U) failures += u" | "_qs;
            failures += QString::fromStdString(result.failures[index]);
        }
        QTextStream row(&neighborCsv);
        row << csv(scenario->id) << ',' << csv(velocity.name) << ',' << times.humanIntent * 1000.0F << ','
            << times.observedDevice * 1000.0F << ',' << times.fullReversal * 1000.0F << ',' << times.oppositeLead * 1000.0F << ','
            << (event != nullptr && event->predictorMissed ? 1 : 0) << ',' << (event == nullptr ? 0.0 : event->wrongDirectionLeadArea)
            << ',' << csv(failures) << '\n';
    }
    if (!writeText(output.filePath(u"neighbor_comparison.csv"_qs), neighborCsv, &error)) {
        QTextStream(stderr) << "Unable to write neighbor evidence: " << error << '\n';
        return 6;
    }

    QString boundaryCsv = u"probe,initial_reverse_speed,turn_time_ms,source_rate_hz,observed_device_ms,safety_cancellation_ms,full_reversal_ms,opposite_lead_ms,predictor_missed,wrong_direction_lead_area\n"_qs;
    struct ProbeSpec { const char *name; float speed; float turn; int sourceRate; };
    const std::array<ProbeSpec, 14> probes{{
        {"velocity-0.040", .040F, .300F, 250}, {"velocity-0.060", .060F, .300F, 250},
        {"velocity-0.075", .075F, .300F, 250}, {"velocity-0.080", .080F, .300F, 250},
        {"velocity-0.100", .100F, .300F, 250}, {"velocity-0.120", .120F, .300F, 250},
        {"velocity-0.160", .160F, .300F, 250}, {"velocity-0.200", .200F, .300F, 250},
        {"velocity-0.300", .300F, .300F, 250}, {"turn-minus-one-source-sample", .080F, .296F, 250},
        {"turn-base", .080F, .300F, 250}, {"turn-plus-one-source-sample", .080F, .304F, 250},
        {"sample-hold-125hz", .080F, .300F, 125}, {"sample-hold-60hz", .080F, .300F, 60},
    }};
    for (const ProbeSpec &probe : probes) {
        const ScenarioDefinition scenario = makeProbe(*exact, "reversal/pattern-73/probe/" + std::string(probe.name),
                                                      probe.turn, probe.speed, probe.sourceRate);
        const ScenarioResult result = replayScenario(scenario, velocity.value, velocity.name);
        const DiagnosticRun diagnostic = runDiagnostic(scenario, velocity.value);
        const EventTimes times = findEventTimes(diagnostic, result);
        const ReversalEvent *event = firstConveyedEvent(result);
        QTextStream row(&boundaryCsv);
        row << csv(probe.name) << ',' << probe.speed << ',' << probe.turn * 1000.0F << ',' << probe.sourceRate << ','
            << times.observedDevice * 1000.0F << ',' << times.safetyCancellation * 1000.0F << ','
            << times.fullReversal * 1000.0F << ',' << times.oppositeLead * 1000.0F << ','
            << (event != nullptr && event->predictorMissed ? 1 : 0) << ',' << (event == nullptr ? 0.0 : event->wrongDirectionLeadArea) << '\n';
    }
    if (!writeText(output.filePath(u"boundary_probes.csv"_qs), boundaryCsv, &error)) {
        QTextStream(stderr) << "Unable to write boundary evidence: " << error << '\n';
        return 6;
    }

    const QFileInfoList physicalFiles = QDir(physicalPath).entryInfoList({u"*.physical.csv"_qs}, QDir::Files, QDir::Name);
    if (physicalFiles.size() != 15) {
        QTextStream(stderr) << "Expected exactly 15 real-human trace files.\n";
        return 7;
    }
    QString realCsv = u"trace_id,source_file,sample_index,event_ms,prior_velocity,first_opposite_velocity,ratio,later_opposite_velocity,configuration,safety_cancellation_ms,full_reversal_ms,opposite_lead_ms,wrong_direction_lead_area\n"_qs;
    int similarSegments = 0;
    for (const QFileInfo &file : physicalFiles) {
        const auto samples = readPhysicalTrace(file.absoluteFilePath());
        if (!samples) {
            QTextStream(stderr) << "Unable to read real-human trace " << file.fileName() << '\n';
            return 7;
        }
        const auto segment = findClosestSegment(file.completeBaseName(), file.absoluteFilePath(), *samples);
        if (!segment) continue;
        ++similarSegments;
        for (const NamedConfiguration &configuration : configurations) {
            const PhysicalReplaySummary replay = replayPhysicalSegment(*samples, *segment, configuration.value);
            QTextStream row(&realCsv);
            row << csv(segment->traceId) << ',' << csv(segment->sourceFile) << ',' << segment->sampleIndex << ','
                << segment->timeSeconds * 1000.0F << ',' << segment->priorVelocity << ',' << segment->firstOppositeVelocity << ','
                << segment->ratio << ',' << segment->laterOppositeVelocity << ',' << csv(configuration.name) << ','
                << replay.safetyCancellationMs << ',' << replay.reversalMs << ',' << replay.oppositeLeadMs << ','
                << replay.wrongDirectionLeadArea << '\n';
        }
    }
    if (!writeText(output.filePath(u"real_trace_segments.csv"_qs), realCsv, &error)) {
        QTextStream(stderr) << "Unable to write real-human evidence: " << error << '\n';
        return 6;
    }

    QString summary;
    QTextStream report(&summary);
    report.setRealNumberNotation(QTextStream::FixedNotation);
    report.setRealNumberPrecision(4);
    report << "# Pattern-73 diagnostic machine summary\n\n"
           << "- Frozen source commit: `" << HOTAS_VERIFICATION_SOURCE_COMMIT << "`\n"
           << "- Harness base commit: `" << HOTAS_VERIFICATION_HARNESS_COMMIT << "`\n"
           << "- Scenario: `reversal/pattern-73`; seed `0x2268D497`; fingerprint `" << scenarioFingerprint(*exact) << "`\n"
           << "- Exact event chain (Velocity control): human intent " << exactTimes.humanIntent * 1000.0F
           << " ms; first source/mapper opposite sample " << exactTimes.firstSourceOpposite * 1000.0F
           << " ms; Canonical two-sample observed-device event " << exactTimes.observedDevice * 1000.0F
           << " ms; frozen safety cancellation " << exactTimes.safetyCancellation * 1000.0F
           << " ms; frozen full detector/opposite lead " << exactTimes.fullReversal * 1000.0F << " ms.\n"
           << "- Classification rows: old PRODUCT_SUSPECT configurations=" << oldSuspects
           << "; corrected PRODUCT_SUSPECT configurations=" << correctedSuspects
           << "; corrected HARNESS configurations=" << correctedHarness << ".\n"
           << "- Real-human structural candidates: " << similarSegments << " across 15 preserved traces.\n";
    if (!writeText(output.filePath(u"Pattern73_Diagnostic_Summary.md"_qs), summary, &error)) {
        QTextStream(stderr) << "Unable to write summary: " << error << '\n';
        return 6;
    }

    const QString manifest = QStringLiteral("{\n  \"schema\": 1,\n  \"frozen_source_commit\": \"")
        + QStringLiteral(HOTAS_VERIFICATION_SOURCE_COMMIT) + QStringLiteral("\",\n  \"harness_base_commit\": \"")
        + QStringLiteral(HOTAS_VERIFICATION_HARNESS_COMMIT) + QStringLiteral("\",\n  \"scenario_seed\": \"0x2268D497\",\n  \"scenario_fingerprint\": \"")
        + scenarioFingerprint(*exact) + QStringLiteral("\",\n  \"sample_trace_sha256\": \"") + sha256(traceCsv.toUtf8())
        + QStringLiteral("\",\n  \"physical_trace_count\": 15\n}\n");
    if (!writeText(output.filePath(u"manifest.json"_qs), manifest, &error)) {
        QTextStream(stderr) << "Unable to write manifest: " << error << '\n';
        return 6;
    }
    QTextStream(stdout) << "Pattern-73 diagnosis complete: old_suspects=" << oldSuspects
                        << " corrected_suspects=" << correctedSuspects << " corrected_harness=" << correctedHarness
                        << " similar_real_segments=" << similarSegments << " output=" << outputPath << '\n';
    return correctedSuspects == 0 && correctedHarness == 9 ? 0 : 8;
}
