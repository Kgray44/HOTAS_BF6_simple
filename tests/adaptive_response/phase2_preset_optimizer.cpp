#include "verification_harness.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
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

constexpr std::uint32_t kTrainingSeed = 0x23A10001U;
constexpr std::uint32_t kValidationSeed = 0x23A10002U;
constexpr std::uint32_t kHoldoutSeed = 0x23A10003U;
constexpr const char *kTrainingHash = "7a462261e36420f63a92babe93ba20bd34014308a8e0230407df8f8c1e9b1d62";
constexpr const char *kValidationHash = "ae4fd487dfb594ac43f729422592178a0fabf6cbc8e96134141809b4eed7c1f7";
constexpr const char *kHoldoutHash = "6c4e742eb17771d3310d25e336b7267fe1d80b54bb638ea2a448a30c803f63db";

struct Weight {
    double response;
    double accuracy;
    double reversal;
    double stop;
    double stability;
    double noise;
    double precision;
    double turning;
    double targetAdvanceMs;
};

struct Spec {
    std::string name;
    float horizonLow;
    float horizonHigh;
    float leadLow;
    float leadHigh;
    Weight weight;
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
    Metrics metrics;
    double score = -1.0;
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

double calculateScore(const Metrics &m, const Weight &w)
{
    const double response = clipped(m.advanceMs / w.targetAdvanceMs);
    const double accuracy = clipped(1.0 - m.mae / 0.12);
    const double reversal = clipped(1.0 - m.reacquisitionMs / 90.0)
        * clipped(1.0 - m.wrongArea / 0.0012);
    const double stop = clipped(1.0 - m.stopResidual / 0.10);
    const double stability = clipped(1.0 - m.artificialStep / 0.18);
    const double noise = clipped(1.0 - m.noiseAmplification / 1.6);
    const double precision = clipped(1.0 - m.stationaryRms / 0.035);
    const double turning = clipped(1.0 - m.pattern73Area / 0.0001);
    return w.response * response + w.accuracy * accuracy + w.reversal * reversal + w.stop * stop
        + w.stability * stability + w.noise * noise + w.precision * precision + w.turning * turning;
}

void qualify(Candidate *candidate, const Weight &weight)
{
    candidate->score = calculateScore(candidate->metrics, weight);
    const Metrics &m = candidate->metrics;
    if (m.nonFinite || m.illegalOutput) candidate->reason = "non-finite-or-illegal-output";
    else if (m.stationaryOffset) candidate->reason = "persistent-stationary-offset";
    else if (m.pattern73Area > 0.0001) candidate->reason = "pattern73-wrong-direction-area";
    else if (m.stopResidual > 0.12) candidate->reason = "stop-residual";
    else if (m.reacquisitionMs > 100.0) candidate->reason = "reversal-reacquisition";
    candidate->eligible = candidate->reason.empty();
}

bool dominates(const Candidate &a, const Candidate &b)
{
    const Metrics &left = a.metrics, &right = b.metrics;
    const bool noWorse = left.advanceMs >= right.advanceMs && left.mae <= right.mae
        && left.wrongArea <= right.wrongArea && left.stopResidual <= right.stopResidual
        && left.artificialStep <= right.artificialStep && left.stationaryRms <= right.stationaryRms;
    const bool better = left.advanceMs > right.advanceMs || left.mae < right.mae
        || left.wrongArea < right.wrongArea || left.stopResidual < right.stopResidual
        || left.artificialStep < right.artificialStep || left.stationaryRms < right.stationaryRms;
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
    std::sort(result.begin(), result.end(), [](const Candidate *a, const Candidate *b) { return a->score > b->score; });
    return result;
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
    std::vector<double> errors, advances, horizons;
    double errorSum = 0.0, leadSum = 0.0, horizonSum = 0.0, stationarySquares = 0.0;
    std::uint64_t stationary = 0, moving = 0, inactive = 0, clamped = 0;
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
        if (telemetry.safetyLimited) ++clamped;
        if (!std::isfinite(telemetry.predicted)) ++metrics.nonFinite;
        if (telemetry.predicted < -1.0001F || telemetry.predicted > 1.0001F) ++metrics.illegalOutput;
    }
    const double count = std::max<std::uint64_t>(1, metrics.samples);
    metrics.mae = errorSum / count; metrics.p95 = percentile(errors, 0.95); metrics.p99 = percentile(errors, 0.99);
    metrics.advanceMs = percentile(advances, 0.50); metrics.usefulLead = leadSum / count;
    metrics.stationaryRms = stationary ? std::sqrt(stationarySquares / stationary) : 0.0;
    metrics.clampFraction = static_cast<double>(clamped) / count;
    metrics.inactiveFraction = moving ? static_cast<double>(inactive) / moving : 0.0;
    metrics.meanHorizonMs = horizonSum / count; metrics.p95HorizonMs = percentile(horizons, 0.95);
    metrics.meanExtensionMs = std::max(0.0, metrics.meanHorizonMs - settings.maximumHorizonMs);
    return metrics;
}
}

int main(int argc, char *argv[])
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
