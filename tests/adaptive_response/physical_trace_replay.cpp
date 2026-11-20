#include "physical_trace_io.h"

#include "adaptive_response.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

namespace {

using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

struct TurnPoint {
    size_t index = 0;
    int previousDirection = 0;
    int nextDirection = 0;
};

struct PredictedPeak {
    size_t index = 0;
    float value = 0.0F;
};

RuntimeAdaptiveResponseConfig runtimeForSettings(AdaptiveResponseSettings settings)
{
    const AdaptiveResponseSettings clean = sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = clean.enabled;
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

std::optional<RuntimeAdaptiveResponseConfig> configurationForPreset(const QString &requested, QString *canonicalName)
{
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        if (preset.name.compare(requested.trimmed(), Qt::CaseInsensitive) != 0) continue;
        if (preset.name != u"Balanced"_qs && preset.name != u"Fast"_qs && preset.name != u"Aggressive"_qs) return std::nullopt;
        if (canonicalName) *canonicalName = preset.name;
        return runtimeForSettings(preset.axes[0].settings);
    }
    return std::nullopt;
}

int directionOf(float delta)
{
    constexpr float kMeaningfulDelta = 0.00015F;
    return delta > kMeaningfulDelta ? 1 : delta < -kMeaningfulDelta ? -1 : 0;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0)
        * static_cast<double>(values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

double medianCadenceHz(const PhysicalAxisTrace &trace)
{
    std::vector<double> rates;
    rates.reserve(trace.samples.size());
    for (const PhysicalAxisTraceSample &sample : trace.samples) {
        if (sample.effectiveCadenceHz > 0.0) rates.push_back(sample.effectiveCadenceHz);
    }
    return percentile(std::move(rates), 0.50);
}

std::vector<TurnPoint> findTurns(const std::vector<TraceSample> &trace)
{
    std::vector<TurnPoint> turns;
    int establishedDirection = 0;
    int candidateDirection = 0;
    int candidateCount = 0;
    size_t candidateStart = 0;
    for (size_t index = 1; index < trace.size(); ++index) {
        const int direction = directionOf(trace[index].physical - trace[index - 1U].physical);
        if (direction == 0) continue;
        if (establishedDirection == 0) {
            establishedDirection = direction;
            continue;
        }
        if (direction == establishedDirection) {
            candidateDirection = 0;
            candidateCount = 0;
            continue;
        }
        if (direction != candidateDirection) {
            candidateDirection = direction;
            candidateCount = 1;
            candidateStart = index;
            continue;
        }
        ++candidateCount;
        if (candidateCount < 2) continue;
        turns.push_back({candidateStart > 0U ? candidateStart - 1U : 0U, establishedDirection, candidateDirection});
        establishedDirection = candidateDirection;
        candidateDirection = 0;
        candidateCount = 0;
    }
    return turns;
}

// The physical local extremum is the recorded turn itself.  The matching
// predictor peak may occur earlier, so measure it over the lobe bounded by
// adjacent recorded turns instead of only sampling it at the physical turn.
PredictedPeak predictedPeakForLobe(const std::vector<AdaptiveResponseTelemetry> &telemetry,
                                   size_t first, size_t last, int incomingDirection)
{
    PredictedPeak result{first, telemetry[first].predicted};
    for (size_t index = first + 1U; index <= last; ++index) {
        const float value = telemetry[index].predicted;
        if ((incomingDirection > 0 && value > result.value)
            || (incomingDirection < 0 && value < result.value)) {
            result = {index, value};
        }
    }
    return result;
}

std::optional<float> futurePhysical(const std::vector<TraceSample> &trace, float timeSeconds)
{
    if (trace.empty() || timeSeconds < trace.front().timeSeconds || timeSeconds > trace.back().timeSeconds) return std::nullopt;
    const auto upper = std::lower_bound(trace.cbegin(), trace.cend(), timeSeconds,
        [](const TraceSample &sample, float target) { return sample.timeSeconds < target; });
    if (upper == trace.cbegin() || upper == trace.cend()) return upper == trace.cend()
        ? std::optional<float>(trace.back().physical) : std::optional<float>(upper->physical);
    const TraceSample &right = *upper;
    const TraceSample &left = *(upper - 1);
    const float span = right.timeSeconds - left.timeSeconds;
    if (span <= 0.0F) return left.physical;
    const float fraction = std::clamp((timeSeconds - left.timeSeconds) / span, 0.0F, 1.0F);
    return left.physical + (right.physical - left.physical) * fraction;
}

bool writeText(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << text;
    if (out.status() != QTextStream::Ok || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(u"HOTAS BF6 Physical Trace Replay"_qs);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Offline physical HOTAS A/B replay. It never reads DirectInput or writes vJoy."_qs);
    parser.addHelpOption();
    const QCommandLineOption inputOption(u"input"_qs, u"Required captured physical-axis CSV."_qs, u"file"_qs);
    const QCommandLineOption presetOption(u"preset"_qs, u"Required built-in preset: Balanced, Fast, or Aggressive."_qs, u"name"_qs);
    const QCommandLineOption variantOption(u"variant"_qs, u"Required replay identity: baseline or candidate."_qs, u"name"_qs);
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty output directory for this trace/preset/variant."_qs, u"directory"_qs);
    parser.addOptions({inputOption, presetOption, variantOption, outputOption});
    parser.process(application);
    const QString inputPath = parser.value(inputOption).trimmed();
    const QString variant = parser.value(variantOption).trimmed().toCaseFolded();
    const QString outputDirectory = parser.value(outputOption).trimmed();
    QString preset;
    const std::optional<RuntimeAdaptiveResponseConfig> configuration = configurationForPreset(parser.value(presetOption), &preset);
    if (inputPath.isEmpty() || outputDirectory.isEmpty() || !configuration
        || (variant != u"baseline"_qs && variant != u"candidate"_qs)) {
        QTextStream(stderr) << "--input, --preset (Balanced/Fast/Aggressive), --variant (baseline/candidate), and --output are required.\n";
        return 2;
    }
    PhysicalAxisTrace physicalTrace;
    QString readError;
    if (!readPhysicalAxisTraceCsv(inputPath, &physicalTrace, &readError)) {
        QTextStream(stderr) << "Unable to read physical trace: " << readError << '\n';
        return 3;
    }
    std::vector<TraceSample> trace = traceSamplesFromPhysicalAxisTrace(physicalTrace);
    ScenarioDefinition scenario;
    scenario.id = physicalTrace.captureLabel.toStdString();
    scenario.trajectoryId = scenario.id;
    scenario.family = "recorded-physical";
    scenario.durationSeconds = trace.empty() ? 0.0F : trace.back().timeSeconds;
    const double cadenceHz = medianCadenceHz(physicalTrace);
    scenario.mapperRateHz = std::clamp(static_cast<int>(std::lround(cadenceHz)), 1, 1000);
    scenario.sourceRateHz = scenario.mapperRateHz;
    const ScenarioResult replay = replayTraceSamples(scenario, trace, *configuration,
                                                      preset.toStdString(), true);
    if (replay.trace.size() != replay.telemetry.size() || replay.trace.size() != physicalTrace.samples.size()) {
        QTextStream(stderr) << "Replay telemetry is incomplete.\n";
        return 4;
    }

    QDir().mkpath(outputDirectory);
    double physicalMinimum = std::numeric_limits<double>::infinity();
    double physicalMaximum = -std::numeric_limits<double>::infinity();
    double futureErrorSum = 0.0;
    double futureErrorMaximum = 0.0;
    std::vector<double> futureErrors;
    double maximumPhysicalStep = 0.0;
    double maximumPredictedStep = 0.0;
    double maximumArtificialStep = 0.0;
    int detectorSamples = 0;
    int detectorEvents = 0;
    bool detectorWasActive = false;
    QString detectorEventsCsv;
    QTextStream detectorOut(&detectorEventsCsv);
    detectorOut.setRealNumberNotation(QTextStream::FixedNotation);
    detectorOut.setRealNumberPrecision(6);
    detectorOut << "time_ms,physical_raw,physical_calibrated,source_reported,source_changed,velocity,acceleration,lead,active_horizon_ms,braking_reduction_factor,previous_sample_hold\n";
    QString samplesCsv;
    QTextStream sampleOut(&samplesCsv);
    sampleOut.setRealNumberNotation(QTextStream::FixedNotation);
    sampleOut.setRealNumberPrecision(6);
    sampleOut << "time_ms,physical_raw_normalized,physical_raw_direct_input,physical_calibrated,source_reported,source_changed,elapsed_us,effective_cadence_hz,predicted,lead,active_horizon_ms,velocity,acceleration,source_update_period_ms,braking_detector,braking_reduction_factor,reversal,future_ground_truth_available,future_physical,future_position_error\n";
    for (size_t index = 0; index < replay.trace.size(); ++index) {
        const TraceSample &sample = replay.trace[index];
        const AdaptiveResponseTelemetry &telemetry = replay.telemetry[index];
        const PhysicalAxisTraceSample &raw = physicalTrace.samples[index];
        physicalMinimum = std::min(physicalMinimum, static_cast<double>(sample.physical));
        physicalMaximum = std::max(physicalMaximum, static_cast<double>(sample.physical));
        const std::optional<float> future = futurePhysical(replay.trace,
            sample.timeSeconds + telemetry.activeHorizonSeconds);
        const double futureError = future ? static_cast<double>(telemetry.predicted - *future) : 0.0;
        if (future) {
            futureErrors.push_back(std::abs(futureError));
            futureErrorSum += std::abs(futureError);
            futureErrorMaximum = std::max(futureErrorMaximum, std::abs(futureError));
        }
        const bool detector = telemetry.sourceBrakingDetected;
        if (detector) ++detectorSamples;
        if (detector && !detectorWasActive) {
            ++detectorEvents;
            const bool priorHold = index > 0U && !physicalTrace.samples[index - 1U].sourceChanged;
            detectorOut << sample.timeSeconds * 1000.0F << ',' << raw.rawNormalized << ','
                        << sample.physical << ',' << (raw.sourceReported ? 1 : 0) << ','
                        << (raw.sourceChanged ? 1 : 0) << ',' << telemetry.velocity << ','
                        << telemetry.acceleration << ',' << telemetry.lead << ','
                        << telemetry.activeHorizonSeconds * 1000.0F << ','
                        << telemetry.brakingReductionFactor << ',' << (priorHold ? 1 : 0) << '\n';
        }
        detectorWasActive = detector;
        if (index > 0U) {
            const double physicalStep = sample.physical - replay.trace[index - 1U].physical;
            const double predictedStep = telemetry.predicted - replay.telemetry[index - 1U].predicted;
            maximumPhysicalStep = std::max(maximumPhysicalStep, std::abs(physicalStep));
            maximumPredictedStep = std::max(maximumPredictedStep, std::abs(predictedStep));
            maximumArtificialStep = std::max(maximumArtificialStep, std::abs(predictedStep - physicalStep));
        }
        sampleOut << sample.timeSeconds * 1000.0F << ',' << raw.rawNormalized << ','
                  << raw.rawDirectInput << ',' << sample.physical << ','
                  << (raw.sourceReported ? 1 : 0) << ',' << (raw.sourceChanged ? 1 : 0) << ','
                  << raw.elapsedUs << ',' << raw.effectiveCadenceHz << ',' << telemetry.predicted << ','
                  << telemetry.lead << ',' << telemetry.activeHorizonSeconds * 1000.0F << ','
                  << telemetry.velocity << ',' << telemetry.acceleration << ','
                  << telemetry.sourceUpdatePeriodSeconds * 1000.0F << ',' << (detector ? 1 : 0) << ','
                  << telemetry.brakingReductionFactor << ',' << (telemetry.reversal ? 1 : 0) << ','
                  << (future ? 1 : 0) << ',';
        if (future) sampleOut << *future << ',' << futureError;
        else sampleOut << ",";
        sampleOut << '\n';
    }
    const double meanFutureError = futureErrors.empty() ? 0.0 : futureErrorSum / static_cast<double>(futureErrors.size());
    const double physicalRange = std::max(0.0001, physicalMaximum - physicalMinimum);
    const double shapeFidelity = std::max(-1.0, 1.0 - meanFutureError / physicalRange);
    const std::vector<TurnPoint> turns = findTurns(replay.trace);
    QString turnsCsv;
    QTextStream turnOut(&turnsCsv);
    turnOut.setRealNumberNotation(QTextStream::FixedNotation);
    turnOut.setRealNumberPrecision(6);
    turnOut << "trace_label,variant,preset,turn_index,physical_turn_ms,previous_direction,next_direction,physical_peak_at_turn,predicted_at_turn,predicted_peak_value,predicted_peak_ms,peak_amplitude_error,peak_timing_advance_ms,lead_at_turn,detector_activation_ms,warning_before_turn_ms,stale_lead_collapse_ms,opposite_lead_reacquisition_ms\n";
    size_t previousTurnIndex = 0;
    for (size_t turnNumber = 0; turnNumber < turns.size(); ++turnNumber) {
        const TurnPoint &turn = turns[turnNumber];
        const TraceSample &sample = replay.trace[turn.index];
        const AdaptiveResponseTelemetry &telemetry = replay.telemetry[turn.index];
        const size_t lobeFirst = turnNumber == 0U ? 0U : turns[turnNumber - 1U].index;
        const size_t lobeLast = turnNumber + 1U < turns.size()
            ? turns[turnNumber + 1U].index : replay.trace.size() - 1U;
        const PredictedPeak peak = predictedPeakForLobe(replay.telemetry, lobeFirst, lobeLast,
                                                         turn.previousDirection);
        int activationIndex = -1;
        for (size_t index = previousTurnIndex; index <= turn.index; ++index) {
            const bool active = replay.telemetry[index].sourceBrakingDetected;
            const bool prior = index > 0U && replay.telemetry[index - 1U].sourceBrakingDetected;
            if (active && !prior) activationIndex = static_cast<int>(index);
        }
        int collapseIndex = -1;
        int reacquisitionIndex = -1;
        for (size_t index = turn.index; index < replay.trace.size(); ++index) {
            if (collapseIndex < 0 && std::abs(replay.telemetry[index].lead) <= 0.00015F) collapseIndex = static_cast<int>(index);
            if (reacquisitionIndex < 0 && replay.telemetry[index].lead * static_cast<float>(turn.nextDirection) > 0.00015F) {
                reacquisitionIndex = static_cast<int>(index);
            }
            if (collapseIndex >= 0 && reacquisitionIndex >= 0) break;
        }
        const auto elapsedFromTurn = [&replay, &turn](int index) {
            return index < 0 ? -1.0 : static_cast<double>(replay.trace[static_cast<size_t>(index)].timeSeconds - replay.trace[turn.index].timeSeconds) * 1000.0;
        };
        const double activationMs = activationIndex < 0 ? -1.0 : replay.trace[static_cast<size_t>(activationIndex)].timeSeconds * 1000.0;
        const double warningMs = activationIndex < 0 ? -1.0
            : (sample.timeSeconds - replay.trace[static_cast<size_t>(activationIndex)].timeSeconds) * 1000.0;
        turnOut << physicalTrace.captureLabel << ',' << variant << ',' << preset << ',' << turnNumber + 1U << ','
                << sample.timeSeconds * 1000.0F << ',' << turn.previousDirection << ',' << turn.nextDirection << ','
                << sample.physical << ',' << telemetry.predicted << ',' << peak.value << ','
                << replay.trace[peak.index].timeSeconds * 1000.0F << ','
                << std::abs(peak.value - sample.physical) << ','
                << (sample.timeSeconds - replay.trace[peak.index].timeSeconds) * 1000.0F << ','
                << telemetry.lead << ',' << activationMs << ',' << warningMs << ','
                << elapsedFromTurn(collapseIndex) << ',' << elapsedFromTurn(reacquisitionIndex) << '\n';
        previousTurnIndex = turn.index;
    }
    QString summaryCsv;
    QTextStream summaryOut(&summaryCsv);
    summaryOut.setRealNumberNotation(QTextStream::FixedNotation);
    summaryOut.setRealNumberPrecision(6);
    summaryOut << "trace_label,variant,preset,selected_axis,calibration_enabled,samples,duration_ms,median_source_cadence_hz,source_changed_samples,detector_activation_samples,detector_activation_events,local_physical_turns,mean_absolute_future_position_error,p95_future_position_error,max_future_position_error,shape_time_shift_fidelity,max_physical_one_sample_step,max_predicted_one_sample_step,max_predictor_only_artificial_step,settling_ms,false_reversals,non_finite\n";
    const int sourceChanges = static_cast<int>(std::count_if(physicalTrace.samples.cbegin(), physicalTrace.samples.cend(),
        [](const PhysicalAxisTraceSample &sample) { return sample.sourceChanged; }));
    summaryOut << physicalTrace.captureLabel << ',' << variant << ',' << preset << ',' << physicalTrace.selectedAxis << ','
               << (physicalTrace.calibrationEnabled ? 1 : 0) << ',' << replay.trace.size() << ','
               << (replay.trace.empty() ? 0.0F : replay.trace.back().timeSeconds * 1000.0F) << ',' << cadenceHz << ','
               << sourceChanges << ',' << detectorSamples << ',' << detectorEvents << ',' << turns.size() << ','
               << meanFutureError << ',' << percentile(futureErrors, 0.95) << ',' << futureErrorMaximum << ','
               << shapeFidelity << ',' << maximumPhysicalStep << ',' << maximumPredictedStep << ','
               << maximumArtificialStep << ',' << replay.metrics.settlingMs << ',' << replay.metrics.falseReversals << ','
               << replay.metrics.nonFinite << '\n';
    QString writeError;
    if (!writeText(QDir(outputDirectory).filePath(u"replay_samples.csv"_qs), samplesCsv, &writeError)
        || !writeText(QDir(outputDirectory).filePath(u"replay_turns.csv"_qs), turnsCsv, &writeError)
        || !writeText(QDir(outputDirectory).filePath(u"replay_detector_events.csv"_qs), detectorEventsCsv, &writeError)
        || !writeText(QDir(outputDirectory).filePath(u"replay_summary.csv"_qs), summaryCsv, &writeError)) {
        QTextStream(stderr) << "Unable to write replay output: " << writeError << '\n';
        return 5;
    }
    QTextStream(stdout) << "Offline " << variant << " replay complete: samples=" << replay.trace.size()
                        << " turns=" << turns.size() << " output=" << outputDirectory << '\n';
    return 0;
}
