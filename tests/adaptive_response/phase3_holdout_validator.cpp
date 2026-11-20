#include "verification_harness.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

constexpr std::uint32_t kHoldoutSeed = 0x23A10003U;
constexpr const char *kHoldoutHash = "6c4e742eb17771d3310d25e336b7267fe1d80b54bb638ea2a448a30c803f63db";
constexpr const char *kFreezeSha = "055e047e2847fc2ec0f59ec457188f66124898e7";

struct Metrics {
    std::uint64_t samples = 0;
    double futureMae = 0.0, rms = 0.0, p95 = 0.0, p99 = 0.0, advanceMs = 0.0, usefulLead = 0.0;
    double wrongArea = 0.0, pattern73Area = 0.0, reacquisitionMs = 0.0, stopResidual = 0.0;
    double stationaryRms = 0.0, noiseAmplification = 0.0, artificialStep = 0.0, clampFraction = 0.0;
    double capFraction = 0.0, inactiveFraction = 0.0, protectionFraction = 0.0, meanHorizonMs = 0.0;
    double p95HorizonMs = 0.0, meanExtensionMs = 0.0, maxExtensionMs = 0.0, maxClampDwellMs = 0.0;
    std::uint64_t nonFinite = 0, illegalOutput = 0, stationaryOffset = 0, unboundedState = 0, hardFailures = 0;
    std::vector<double> errors, advances, horizons, reacquisitions;
};

struct ScenarioRun {
    std::string id;
    std::vector<std::string> families;
    Metrics metrics;
};

struct Config {
    std::string preset, role, id;
    AdaptiveResponseSettings settings;
};

struct ConfigRun {
    Config config;
    std::vector<ScenarioRun> scenarios;
    Metrics aggregate;
};

struct Threshold {
    const char *preset;
    double minAdvance, maxMae, maxP95, maxP99, maxNoise, maxReacq, maxStop, maxStep, maxWrong;
};

QString csv(const std::string &input)
{
    QString result = QString::fromStdString(input);
    const QChar quote(34);
    result.replace(quote, QString(quote) + QString(quote));
    return QString(quote) + result + QString(quote);
}

bool writeText(const QString &path, const std::string &contents)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    return file.write(contents.data(), static_cast<qint64>(contents.size())) >= 0 && file.commit();
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

QString sha256(const QByteArray &input)
{
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * (values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

RuntimeAdaptiveResponseConfig runtimeFor(const AdaptiveResponseSettings &original)
{
    const AdaptiveResponseSettings settings = sanitizedAdaptiveResponseSettings(original);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = settings.enabled && settings.maximumHorizonMs > 0.0F;
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

float intendedAt(const std::vector<TraceSample> &trace, float time)
{
    if (trace.empty()) return 0.0F;
    if (time <= trace.front().timeSeconds) return trace.front().intended;
    if (time >= trace.back().timeSeconds) return trace.back().intended;
    const auto upper = std::lower_bound(trace.cbegin(), trace.cend(), time,
        [](const TraceSample &sample, float target) { return sample.timeSeconds < target; });
    const TraceSample &right = *upper;
    const TraceSample &left = *(upper - 1);
    const float span = std::max(0.000001F, right.timeSeconds - left.timeSeconds);
    return left.intended + (right.intended - left.intended) * (time - left.timeSeconds) / span;
}

std::vector<ScenarioDefinition> holdoutScenarios()
{
    std::vector<ScenarioDefinition> result;
    size_t index = 0;
    for (const std::string &persona : {"precision-pilot", "combat-helicopter", "fixed-wing", "space-sim", "noisy-older-sensor"}) {
        for (int sample = 0; sample < 20; ++sample) {
            ScenarioDefinition item;
            item.id = "randomized/" + persona + "-" + std::to_string(sample);
            item.trajectoryId = item.id;
            item.family = "persona";
            item.durationSeconds = persona == "combat-helicopter" ? 3.0F : 5.0F;
            item.mapperRateHz = 250;
            item.sourceRateHz = 250;
            item.seed = deriveSeed(kHoldoutSeed, "randomized", persona, static_cast<std::uint32_t>(index++), "trajectory");
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

QString holdoutIdentity(const std::vector<ScenarioDefinition> &scenarios)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ScenarioDefinition &item : scenarios) {
        const auto trace = generateTrace(item);
        hash.addData(item.id.data(), static_cast<qsizetype>(item.id.size()));
        for (const auto &sample : trace) {
            hash.addData(reinterpret_cast<const char *>(&sample.timeSeconds), sizeof(sample.timeSeconds));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

std::vector<std::string> tagsFor(const ScenarioDefinition &scenario, const ScenarioResult &result)
{
    bool stop = false, reversal = false, endpoint = false;
    double peakVelocity = 0.0, velocitySum = 0.0;
    for (const TraceSample &sample : result.trace) {
        stop = stop || sample.humanIntentStop;
        reversal = reversal || sample.humanIntentReversal;
        endpoint = endpoint || sample.targetArrival;
        peakVelocity = std::max(peakVelocity, std::abs(static_cast<double>(sample.humanIntentVelocity)));
        velocitySum += std::abs(static_cast<double>(sample.humanIntentVelocity));
    }
    const double meanVelocity = result.trace.empty() ? 0.0 : velocitySum / result.trace.size();
    std::vector<std::string> tags;
    if (scenario.id.find("precision-pilot") != std::string::npos) tags.push_back("precision");
    if (scenario.noise != NoiseModel::None) tags.push_back("stationary_noise");
    if (meanVelocity <= .20) tags.push_back("slow_movement");
    if (peakVelocity >= 1.0) tags.push_back("fast_ballistic");
    if (stop) tags.push_back("stops");
    if (reversal) tags.push_back("reversals");
    if (scenario.id.find("combat-helicopter") != std::string::npos || result.metrics.humanIntentReversals > 1) tags.push_back("oscillatory_combat");
    if (endpoint) tags.push_back("endpoints");
    if ((stop && reversal) || result.metrics.humanIntentReversals > 1) tags.push_back("compound_motion");
    if (tags.empty()) tags.push_back("unclassified_persona");
    return tags;
}

Metrics evaluate(const ScenarioDefinition &scenario, const AdaptiveResponseSettings &settings, const std::string &configuration,
                 std::vector<std::string> *tags)
{
    const ScenarioResult result = replayScenario(scenario, runtimeFor(settings), configuration, true);
    if (tags) *tags = tagsFor(scenario, result);
    Metrics metrics;
    std::uint64_t moving = 0, inactive = 0, clamped = 0, capped = 0, protectedSamples = 0, stationary = 0, dwell = 0;
    double errorSum = 0.0, errorSquared = 0.0, leadSum = 0.0, horizonSum = 0.0, stationarySquares = 0.0, maximumDwell = 0.0;
    for (size_t index = 0; index < result.trace.size() && index < result.telemetry.size(); ++index) {
        const TraceSample &sample = result.trace[index];
        const AdaptiveResponseTelemetry &telemetry = result.telemetry[index];
        ++metrics.samples;
        if (!std::isfinite(telemetry.predicted) || !std::isfinite(telemetry.lead)) ++metrics.nonFinite;
        if (telemetry.predicted < -1.0001F || telemetry.predicted > 1.0001F) ++metrics.illegalOutput;
        const double error = std::abs(static_cast<double>(telemetry.predicted) - intendedAt(result.trace, sample.timeSeconds + telemetry.activeHorizonSeconds));
        metrics.errors.push_back(error);
        errorSum += error;
        errorSquared += error * error;
        const double horizon = telemetry.activeHorizonSeconds * 1000.0;
        metrics.horizons.push_back(horizon);
        horizonSum += horizon;
        metrics.maxExtensionMs = std::max(metrics.maxExtensionMs, std::max(0.0, horizon - settings.maximumHorizonMs));
        if (horizon > settings.maximumHorizonMs + settings.horizonExtensionCapMs + .001) ++metrics.unboundedState;
        if (std::abs(sample.humanIntentVelocity) > .08F) {
            ++moving;
            if (std::abs(telemetry.lead) < .00001F) ++inactive;
            const double advance = std::abs(telemetry.lead / sample.humanIntentVelocity) * 1000.0;
            if (advance < 100.0) metrics.advances.push_back(advance);
            if (telemetry.lead * sample.humanIntentVelocity < 0.0F) metrics.wrongArea += std::abs(telemetry.lead) * sample.dtSeconds;
        }
        if (!sample.intendedMoving) {
            stationarySquares += telemetry.lead * telemetry.lead;
            ++stationary;
        }
        leadSum += std::abs(telemetry.lead);
        if (telemetry.safetyLimited) {
            ++clamped;
            ++dwell;
        } else {
            maximumDwell = std::max(maximumDwell, dwell * sample.dtSeconds * 1000.0);
            dwell = 0;
        }
        if (telemetry.turningPointConfidence > .10F) ++protectedSamples;
        if (settings.horizonExtensionCapMs > 0.0F && horizon >= (settings.maximumHorizonMs + settings.horizonExtensionCapMs) * .98) ++capped;
    }
    for (const ReversalEvent &event : result.reversalEvents) {
        if (event.reacquisitionTimeSeconds >= event.groundTruthTimeSeconds)
            metrics.reacquisitions.push_back((event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0);
    }
    for (const std::string &failure : result.failures) if (failure.rfind("HARD:", 0) == 0) ++metrics.hardFailures;
    const double count = std::max<std::uint64_t>(1, metrics.samples);
    metrics.futureMae = errorSum / count;
    metrics.rms = std::sqrt(errorSquared / count);
    metrics.p95 = percentile(metrics.errors, .95);
    metrics.p99 = percentile(metrics.errors, .99);
    metrics.advanceMs = percentile(metrics.advances, .50);
    metrics.usefulLead = leadSum / count;
    metrics.reacquisitionMs = percentile(metrics.reacquisitions, .50);
    metrics.stopResidual = std::abs(result.metrics.leadAtPhysicalStop);
    metrics.stationaryRms = stationary ? std::sqrt(stationarySquares / stationary) : 0.0;
    metrics.noiseAmplification = result.metrics.noiseAmplificationRatio;
    metrics.artificialStep = result.metrics.maximumOutputStep;
    metrics.clampFraction = clamped / count;
    metrics.capFraction = capped / count;
    metrics.inactiveFraction = moving ? static_cast<double>(inactive) / moving : 0.0;
    metrics.protectionFraction = protectedSamples / count;
    metrics.meanHorizonMs = horizonSum / count;
    metrics.p95HorizonMs = percentile(metrics.horizons, .95);
    metrics.meanExtensionMs = std::max(0.0, metrics.meanHorizonMs - settings.maximumHorizonMs);
    metrics.maxClampDwellMs = maximumDwell;
    metrics.stationaryOffset = result.metrics.stationaryDrift;
    if (scenario.id.find("pattern-73") != std::string::npos) metrics.pattern73Area = result.metrics.wrongDirectionLeadArea;
    return metrics;
}

Metrics aggregate(const std::vector<ScenarioRun> &runs)
{
    Metrics result;
    if (runs.empty()) return result;
    double mae = 0.0, useful = 0.0, wrong = 0.0, stop = 0.0, stationary = 0.0, noise = 0.0;
    double clamp = 0.0, cap = 0.0, inactive = 0.0, protection = 0.0, horizon = 0.0, extension = 0.0, rmsSquared = 0.0;
    for (const ScenarioRun &run : runs) {
        const Metrics &m = run.metrics;
        result.samples += m.samples;
        mae += m.futureMae;
        rmsSquared += m.rms * m.rms * m.samples;
        useful += m.usefulLead;
        wrong += m.wrongArea;
        result.pattern73Area = std::max(result.pattern73Area, m.pattern73Area);
        stop += m.stopResidual;
        stationary += m.stationaryRms;
        noise += m.noiseAmplification;
        result.artificialStep = std::max(result.artificialStep, m.artificialStep);
        clamp += m.clampFraction;
        cap += m.capFraction;
        inactive += m.inactiveFraction;
        protection += m.protectionFraction;
        horizon += m.meanHorizonMs;
        extension += m.meanExtensionMs;
        result.maxExtensionMs = std::max(result.maxExtensionMs, m.maxExtensionMs);
        result.maxClampDwellMs = std::max(result.maxClampDwellMs, m.maxClampDwellMs);
        result.nonFinite += m.nonFinite;
        result.illegalOutput += m.illegalOutput;
        result.stationaryOffset += m.stationaryOffset;
        result.unboundedState += m.unboundedState;
        result.hardFailures += m.hardFailures;
        result.errors.insert(result.errors.end(), m.errors.begin(), m.errors.end());
        result.advances.insert(result.advances.end(), m.advances.begin(), m.advances.end());
        result.horizons.insert(result.horizons.end(), m.horizons.begin(), m.horizons.end());
        result.reacquisitions.insert(result.reacquisitions.end(), m.reacquisitions.begin(), m.reacquisitions.end());
    }
    const double count = static_cast<double>(runs.size());
    result.futureMae = mae / count;
    result.rms = result.samples ? std::sqrt(rmsSquared / result.samples) : 0.0;
    result.p95 = percentile(result.errors, .95);
    result.p99 = percentile(result.errors, .99);
    result.advanceMs = percentile(result.advances, .50);
    result.usefulLead = useful / count;
    result.wrongArea = wrong / count;
    result.reacquisitionMs = percentile(result.reacquisitions, .50);
    result.stopResidual = stop / count;
    result.stationaryRms = stationary / count;
    result.noiseAmplification = noise / count;
    result.clampFraction = clamp / count;
    result.capFraction = cap / count;
    result.inactiveFraction = inactive / count;
    result.protectionFraction = protection / count;
    result.meanHorizonMs = horizon / count;
    result.p95HorizonMs = percentile(result.horizons, .95);
    result.meanExtensionMs = extension / count;
    return result;
}

std::optional<AdaptiveResponseSettings> baselineFor(const std::string &name)
{
    for (const AdaptiveResponsePreset &preset : builtInAdaptiveResponsePresets()) {
        if (preset.name.toStdString() == name) return preset.axes[0].settings;
    }
    return std::nullopt;
}

AdaptiveResponseSettings recommended(const std::string &preset, AdaptiveResponseSettings base)
{
    if (preset == "Light") {
        base.maximumHorizonMs=7.022789F; base.maximumLead=.019900F; base.turningPointProtection=.594576F; base.turningPointMargin=.070129F;
        base.velocityResponse=.542857F; base.accelerationResponse=.320027F; base.horizonExtension=.189202F; base.horizonExtensionCapMs=3.333617F;
        base.reversalResponse=.948840F; base.decelerationResponse=.906122F; base.onsetAssist=.10F; base.onsetCap=.06F; base.sustainedAssist=.12F; base.sustainedCap=.06F;
    } else if (preset == "Balanced") {
        base.maximumHorizonMs=12.016726F; base.maximumLead=.055514F; base.turningPointProtection=.810111F; base.turningPointMargin=.161458F;
        base.velocityResponse=.622800F; base.accelerationResponse=.620906F; base.horizonExtension=.216395F; base.horizonExtensionCapMs=8.265409F;
        base.reversalResponse=.935054F; base.decelerationResponse=.818625F; base.onsetAssist=.18F; base.onsetCap=.10F; base.sustainedAssist=.22F; base.sustainedCap=.10F;
    } else if (preset == "Fast") {
        base.maximumHorizonMs=15.717801F; base.maximumLead=.082755F; base.turningPointProtection=.570320F; base.turningPointMargin=.069699F;
        base.velocityResponse=.728501F; base.accelerationResponse=.757727F; base.horizonExtension=.456006F; base.horizonExtensionCapMs=14.068745F;
        base.reversalResponse=.952815F; base.decelerationResponse=.754898F; base.onsetAssist=.28F; base.onsetCap=.16F; base.sustainedAssist=.32F; base.sustainedCap=.15F;
    } else if (preset == "Aggressive") {
        base.maximumHorizonMs=23.823053F; base.maximumLead=.138797F; base.turningPointProtection=.799286F; base.turningPointMargin=.160922F;
        base.velocityResponse=.954385F; base.accelerationResponse=.830786F; base.horizonExtension=.477132F; base.horizonExtensionCapMs=15.628583F;
        base.reversalResponse=.929662F; base.decelerationResponse=.773903F; base.onsetAssist=.38F; base.onsetCap=.22F; base.sustainedAssist=.42F; base.sustainedCap=.20F;
    } else {
        base.maximumHorizonMs=27.840208F; base.maximumLead=.242355F; base.turningPointProtection=1.0F; base.turningPointMargin=.125216F;
        base.velocityResponse=1.0F; base.accelerationResponse=1.0F; base.horizonExtension=.758739F; base.horizonExtensionCapMs=25.139339F;
        base.reversalResponse=.930832F; base.decelerationResponse=.759074F; base.onsetAssist=.50F; base.onsetCap=.30F; base.sustainedAssist=.55F; base.sustainedCap=.28F;
    }
    base.settlingResponse=.92F;
    base.noiseRejection=.012F;
    base.motionSensitivity=.035F;
    return sanitizedAdaptiveResponseSettings(base);
}

std::string parameterCsv(const AdaptiveResponseSettings &s)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << s.maximumHorizonMs << ',' << s.maximumLead << ',' << s.turningPointProtection << ',' << s.turningPointMargin << ','
        << s.velocityResponse << ',' << s.accelerationResponse << ',' << s.horizonExtension << ',' << s.horizonExtensionCapMs << ','
        << s.reversalResponse << ',' << s.decelerationResponse << ',' << s.onsetAssist << ',' << s.onsetCap << ','
        << s.sustainedAssist << ',' << s.sustainedCap << ',' << s.settlingResponse << ',' << s.noiseRejection << ',' << s.motionSensitivity;
    return out.str();
}

std::string metricsCsv(const Metrics &m)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << m.futureMae << ',' << m.rms << ',' << m.p95 << ',' << m.p99 << ',' << m.advanceMs << ',' << m.usefulLead << ','
        << m.wrongArea << ',' << m.pattern73Area << ',' << m.reacquisitionMs << ',' << m.stopResidual << ',' << m.stationaryRms << ','
        << m.noiseAmplification << ',' << m.artificialStep << ',' << m.clampFraction << ',' << m.capFraction << ',' << m.inactiveFraction << ','
        << m.protectionFraction << ',' << m.meanHorizonMs << ',' << m.p95HorizonMs << ',' << m.meanExtensionMs << ',' << m.maxExtensionMs << ','
        << m.maxClampDwellMs << ',' << m.nonFinite << ',' << m.illegalOutput << ',' << m.stationaryOffset << ',' << m.unboundedState << ',' << m.hardFailures;
    return out.str();
}

std::string familyText(const std::vector<std::string> &families)
{
    std::ostringstream out;
    for (size_t index = 0; index < families.size(); ++index) {
        if (index) out << ';';
        out << families[index];
    }
    return out.str();
}

const Threshold *thresholdFor(const std::string &preset)
{
    static constexpr std::array<Threshold, 5> all{{
        {"Light", .25, 1.07, 1.10, 1.10, 1.10, 5.0, .010, .010, .00020},
        {"Balanced", .50, 1.05, 1.08, 1.10, 1.10, 5.0, .010, .010, .00020},
        {"Fast", 1.00, 1.15, 1.20, 1.25, 1.20, 10.0, .020, .020, .00035},
        {"Aggressive", 1.50, 1.30, 1.35, 1.40, 1.35, 15.0, .030, .030, .00050},
        {"Extreme", 2.00, 1.50, 1.50, 1.60, 1.50, 20.0, .040, .040, .00075},
    }};
    for (const Threshold &item : all) if (preset == item.preset) return &item;
    return nullptr;
}

bool hardFailure(const Metrics &m)
{
    return m.nonFinite || m.illegalOutput || m.stationaryOffset || m.unboundedState || m.hardFailures
        || m.pattern73Area > .0001 || m.stopResidual > .12 || m.reacquisitionMs > 100.0;
}

const ConfigRun *findRun(const std::vector<ConfigRun> &runs, const std::string &preset, const std::string &role)
{
    for (const ConfigRun &run : runs) if (run.config.preset == preset && run.config.role == role) return &run;
    return nullptr;
}

const ScenarioRun *findScenario(const ConfigRun &run, const std::string &id)
{
    for (const ScenarioRun &row : run.scenarios) if (row.id == id) return &row;
    return nullptr;
}

double metricValue(const Metrics &m, const std::string &name)
{
    if (name == "future_mae") return m.futureMae;
    if (name == "p95_error") return m.p95;
    if (name == "p99_error") return m.p99;
    if (name == "wrong_direction_area") return m.wrongArea;
    if (name == "stop_residual") return m.stopResidual;
    if (name == "clamp_dwell_ms") return m.maxClampDwellMs;
    return m.noiseAmplification;
}

bool runProgram(const QString &program, const QStringList &arguments, int *exitCode, QString *output)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(30000) || !process.waitForFinished(600000)) {
        if (exitCode) *exitCode = -1;
        if (output) *output = process.errorString();
        return false;
    }
    if (exitCode) *exitCode = process.exitCode();
    if (output) *output = QString::fromUtf8(process.readAll());
    return process.exitStatus() == QProcess::NormalExit;
}

int extractInt(const QString &text, const QRegularExpression &expression)
{
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"One-shot offline V2.3.T Phase 3 holdout validator; no hardware, UI, mapper, DirectInput, or vJoy."_qs);
    parser.addHelpOption();
    const QCommandLineOption outputOption(u"output"_qs, u"Required empty artifact directory."_qs, u"directory"_qs);
    const QCommandLineOption reportOption(u"report"_qs, u"Required final report path."_qs, u"path"_qs);
    const QCommandLineOption sentinelOption(u"sentinel-exe"_qs, u"Required canonical sentinel executable."_qs, u"path"_qs);
    const QCommandLineOption benchmarkOption(u"benchmark-exe"_qs, u"Required Release benchmark executable."_qs, u"path"_qs);
    parser.addOptions({outputOption, reportOption, sentinelOption, benchmarkOption});
    parser.process(app);
    const QString outputPath = parser.value(outputOption).trimmed();
    const QString reportPath = parser.value(reportOption).trimmed();
    const QString sentinelPath = parser.value(sentinelOption).trimmed();
    const QString benchmarkPath = parser.value(benchmarkOption).trimmed();
    if (outputPath.isEmpty() || reportPath.isEmpty() || sentinelPath.isEmpty() || benchmarkPath.isEmpty()) return 2;
    QDir output(outputPath);
    if (output.exists() && !output.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) return 2;

    const std::vector<ScenarioDefinition> holdout = holdoutScenarios();
    const QString actualHoldoutHash = holdoutIdentity(holdout);
    if (holdout.size() != 100U || actualHoldoutHash != QString::fromLatin1(kHoldoutHash)) {
        QTextStream(stderr) << "PHASE 3 BLOCKED - HOLDOUT IDENTITY FAILURE scenarios=" << holdout.size()
                            << " sha256=" << actualHoldoutHash << '\n';
        return 3;
    }

    const std::array<std::string, 5> presets{{"Light", "Balanced", "Fast", "Aggressive", "Extreme"}};
    std::vector<Config> configs;
    for (const std::string &preset : presets) {
        const auto baseline = baselineFor(preset);
        if (!baseline) return 4;
        configs.push_back({preset, "baseline", preset + "-baseline", *baseline});
        const std::string candidate = preset == "Light" ? "Light-secondary-1-3"
            : preset == "Balanced" ? "Balanced-secondary-0-2"
            : preset == "Fast" ? "Fast-secondary-3-1"
            : preset == "Aggressive" ? "Aggressive-secondary-0-1" : "Extreme-secondary-2-1";
        configs.push_back({preset, "recommended", candidate, recommended(preset, *baseline)});
    }

    const std::string vectorColumns = "candidate_id,preset,role,maximum_horizon_ms,maximum_lead,turning_protection,turning_margin,velocity_response,acceleration_response,horizon_extension,horizon_extension_cap_ms,reversal_response,deceleration_response,onset_assist,onset_cap,sustained_assist,sustained_cap,settling_response,noise_rejection,motion_sensitivity\n";
    std::ostringstream candidates, baselines;
    candidates << vectorColumns;
    baselines << vectorColumns;
    for (const Config &config : configs) {
        std::ostream &stream = config.role == "recommended" ? candidates : baselines;
        stream << csv(config.id).toStdString() << ',' << config.preset << ',' << config.role << ',' << parameterCsv(config.settings) << '\n';
    }
    const std::string policy =
        "# V2.3.T Phase 3 locked acceptance policy\n\n"
        "This policy, all five shipping baselines, and one selected Phase 2 recommendation per preset are locked before replay. "
        "No alternate, perturbation, retuning, regenerated scenario, metric weight, threshold, or production preset change is allowed.\n\n"
        "Hard failure means a non-finite or illegal output, persistent stationary offset, unbounded horizon state, hard correctness failure, "
        "Pattern-73 wrong-direction area above 0.0001, stop residual above 0.12, or reversal reacquisition above 100 ms. "
        "The canonical sentinel must separately give PRODUCT_CONFIRMED=0, PRODUCT_SUSPECT=0, and hard failures=0.\n\n"
        "Phase 2 metric semantics are retained: future error is predictor versus intended value at active horizon; RMS/p95/p99 use that distribution; "
        "effective advance is median absolute lead divided by intended velocity where magnitude exceeds 0.08; useful lead is mean absolute lead; "
        "stop, stationary, noise, and artificial-step values come from the verification harness. Clamp occupancy is safetyLimited fraction.\n\n"
        "Light: advance plus 0.25 ms and MAE/P95/P99/noise no worse than 1.07/1.10/1.10/1.10x baseline. "
        "Balanced: plus 0.50 ms and 1.05/1.08/1.10/1.10x. Fast: plus 1.00 ms and 1.15/1.20/1.25/1.20x. "
        "Aggressive: plus 1.50 ms and 1.30/1.35/1.40/1.35x. Extreme: plus 2.00 ms and 1.50/1.50/1.60/1.50x. "
        "The validator also locks absolute maximum regressions for reversal, stop, step, and wrong-direction area, and requires at least 0.25 ms separation between adjacent recommendations.\n";
    const QString candidateHash = sha256(QByteArray::fromStdString(candidates.str()));
    const QString baselineHash = sha256(QByteArray::fromStdString(baselines.str()));
    const QString policyHash = sha256(QByteArray::fromStdString(policy));
    if (!QDir().mkpath(outputPath)
        || !writeText(output.filePath(u"locked_acceptance_policy.md"_qs), policy)
        || !writeText(output.filePath(u"candidate_vectors.csv"_qs), candidates.str())
        || !writeText(output.filePath(u"baseline_vectors.csv"_qs), baselines.str())) return 5;
    std::ostringstream preparedManifest;
    preparedManifest << "{\n  \"phase\": \"pre-replay-locked\",\n  \"freeze_sha\": \"" << kFreezeSha
        << "\",\n  \"holdout_seed\": \"0x23A10003\",\n  \"holdout_scenarios\": 100,\n  \"holdout_sha256\": \""
        << actualHoldoutHash.toStdString() << "\",\n  \"candidate_vectors_sha256\": \"" << candidateHash.toStdString()
        << "\",\n  \"baseline_vectors_sha256\": \"" << baselineHash.toStdString()
        << "\",\n  \"acceptance_policy_sha256\": \"" << policyHash.toStdString()
        << "\",\n  \"permitted_configurations\": 10,\n  \"permitted_holdout_executions\": 1000\n}\n";
    if (!writeText(output.filePath(u"manifest.json"_qs), preparedManifest.str())) return 5;

    std::vector<ConfigRun> runs;
    for (const Config &config : configs) {
        ConfigRun run;
        run.config = config;
        for (const ScenarioDefinition &scenario : holdout) {
            ScenarioRun row;
            row.id = scenario.id;
            row.metrics = evaluate(scenario, config.settings, config.id, &row.families);
            run.scenarios.push_back(std::move(row));
        }
        run.aggregate = aggregate(run.scenarios);
        runs.push_back(std::move(run));
    }

    const std::string metricColumns = "future_mae,rms_error,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,artificial_step,lead_clamp_fraction,horizon_cap_fraction,prediction_inactive_fraction,turning_protection_active_fraction,mean_active_horizon_ms,p95_active_horizon_ms,mean_extension_ms,max_extension_ms,max_clamp_dwell_ms,non_finite,illegal_output,stationary_offset,unbounded_state,hard_correctness_failures";
    std::ostringstream raw, aggregates;
    raw << "preset,role,candidate_id,scenario_id,families,samples," << metricColumns << '\n';
    aggregates << "preset,role,candidate_id,scenario_count,samples," << metricColumns << '\n';
    for (const ConfigRun &run : runs) {
        aggregates << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ',' << run.scenarios.size()
                   << ',' << run.aggregate.samples << ',' << metricsCsv(run.aggregate) << '\n';
        for (const ScenarioRun &row : run.scenarios) {
            raw << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ',' << csv(row.id).toStdString()
                << ',' << csv(familyText(row.families)).toStdString() << ',' << row.metrics.samples << ',' << metricsCsv(row.metrics) << '\n';
        }
    }

    const std::array<std::string, 9> familyNames{{"precision", "stationary_noise", "slow_movement", "fast_ballistic",
        "stops", "reversals", "oscillatory_combat", "endpoints", "compound_motion"}};
    std::ostringstream familyResults;
    familyResults << "preset,role,candidate_id,family,scenario_count,samples," << metricColumns << '\n';
    for (const ConfigRun &run : runs) for (const std::string &family : familyNames) {
        std::vector<ScenarioRun> selected;
        for (const ScenarioRun &row : run.scenarios)
            if (std::find(row.families.cbegin(), row.families.cend(), family) != row.families.cend()) selected.push_back(row);
        const Metrics combined = aggregate(selected);
        familyResults << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ','
                      << family << ',' << selected.size() << ',' << combined.samples << ',' << metricsCsv(combined) << '\n';
    }
    std::ostringstream clampResults;
    clampResults << "preset,role,candidate_id,mean_clamp_fraction,p95_clamp_fraction,max_clamp_fraction,max_continuous_clamp_dwell_ms,scenarios_over_50_percent,scenarios_over_80_percent\n";
    for (const ConfigRun &run : runs) {
        std::vector<double> fractions;
        size_t over50 = 0, over80 = 0;
        for (const ScenarioRun &row : run.scenarios) {
            fractions.push_back(row.metrics.clampFraction);
            if (row.metrics.clampFraction > .50) ++over50;
            if (row.metrics.clampFraction > .80) ++over80;
        }
        clampResults << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ','
                     << std::fixed << std::setprecision(8) << run.aggregate.clampFraction << ',' << percentile(fractions,.95)
                     << ',' << percentile(fractions,1.0) << ',' << run.aggregate.maxClampDwellMs << ',' << over50 << ',' << over80 << '\n';
    }
    std::map<std::string, std::vector<std::string>> decisionReasons;
    std::ostringstream comparisons;
    comparisons << "preset,baseline_id,recommended_id,advance_delta_ms,future_mae_ratio,p95_ratio,p99_ratio,useful_lead_delta,wrong_direction_area_delta,reacquisition_delta_ms,stop_residual_delta,noise_ratio,artificial_step_delta,clamp_fraction_delta,mean_horizon_delta_ms,maximum_lead_delta\n";
    for (const std::string &preset : presets) {
        const ConfigRun *baseline = findRun(runs, preset, "baseline");
        const ConfigRun *candidate = findRun(runs, preset, "recommended");
        const Threshold *threshold = thresholdFor(preset);
        if (!baseline || !candidate || !threshold) return 6;
        auto &reasons = decisionReasons[preset];
        if (hardFailure(candidate->aggregate)) reasons.push_back("hard-safety-or-correctness-gate");
        if (candidate->aggregate.advanceMs < baseline->aggregate.advanceMs + threshold->minAdvance) reasons.push_back("insufficient-temporal-advance");
        if (candidate->aggregate.futureMae > baseline->aggregate.futureMae * threshold->maxMae + .000001) reasons.push_back("future-mae-regression");
        if (candidate->aggregate.p95 > baseline->aggregate.p95 * threshold->maxP95 + .000001) reasons.push_back("p95-tail-regression");
        if (candidate->aggregate.p99 > baseline->aggregate.p99 * threshold->maxP99 + .000001) reasons.push_back("p99-tail-regression");
        if (candidate->aggregate.noiseAmplification > baseline->aggregate.noiseAmplification * threshold->maxNoise + .000001) reasons.push_back("noise-regression");
        if (candidate->aggregate.reacquisitionMs > baseline->aggregate.reacquisitionMs + threshold->maxReacq) reasons.push_back("reversal-regression");
        if (candidate->aggregate.stopResidual > baseline->aggregate.stopResidual + threshold->maxStop) reasons.push_back("stop-regression");
        if (candidate->aggregate.artificialStep > baseline->aggregate.artificialStep + threshold->maxStep) reasons.push_back("artificial-step-regression");
        if (candidate->aggregate.wrongArea > baseline->aggregate.wrongArea + threshold->maxWrong) reasons.push_back("wrong-direction-area-regression");
        comparisons << preset << ',' << csv(baseline->config.id).toStdString() << ',' << csv(candidate->config.id).toStdString() << ','
                    << std::fixed << std::setprecision(8) << candidate->aggregate.advanceMs - baseline->aggregate.advanceMs << ','
                    << (baseline->aggregate.futureMae == 0.0 ? 0.0 : candidate->aggregate.futureMae / baseline->aggregate.futureMae) << ','
                    << (baseline->aggregate.p95 == 0.0 ? 0.0 : candidate->aggregate.p95 / baseline->aggregate.p95) << ','
                    << (baseline->aggregate.p99 == 0.0 ? 0.0 : candidate->aggregate.p99 / baseline->aggregate.p99) << ','
                    << candidate->aggregate.usefulLead - baseline->aggregate.usefulLead << ','
                    << candidate->aggregate.wrongArea - baseline->aggregate.wrongArea << ','
                    << candidate->aggregate.reacquisitionMs - baseline->aggregate.reacquisitionMs << ','
                    << candidate->aggregate.stopResidual - baseline->aggregate.stopResidual << ','
                    << (baseline->aggregate.noiseAmplification == 0.0 ? 0.0 : candidate->aggregate.noiseAmplification / baseline->aggregate.noiseAmplification) << ','
                    << candidate->aggregate.artificialStep - baseline->aggregate.artificialStep << ','
                    << candidate->aggregate.clampFraction - baseline->aggregate.clampFraction << ','
                    << candidate->aggregate.meanHorizonMs - baseline->aggregate.meanHorizonMs << ','
                    << candidate->config.settings.maximumLead - baseline->config.settings.maximumLead << '\n';
    }
    std::ostringstream ladder;
    ladder << "lower_preset,upper_preset,lower_advance_ms,upper_advance_ms,advance_separation_ms,required_minimum_ms,passes\n";
    bool ladderPass = true;
    for (size_t index = 1; index < presets.size(); ++index) {
        const Metrics &lower = findRun(runs, presets[index - 1], "recommended")->aggregate;
        const Metrics &upper = findRun(runs, presets[index], "recommended")->aggregate;
        const bool passes = upper.advanceMs >= lower.advanceMs + .25;
        ladderPass = ladderPass && passes;
        ladder << presets[index - 1] << ',' << presets[index] << ',' << lower.advanceMs << ',' << upper.advanceMs << ','
               << upper.advanceMs - lower.advanceMs << ",0.250000," << (passes ? "true" : "false") << '\n';
        if (!passes) {
            decisionReasons[presets[index - 1]].push_back("adjacent-preset-ladder-collapse");
            decisionReasons[presets[index]].push_back("adjacent-preset-ladder-collapse");
        }
    }
    const std::array<std::string, 7> outlierNames{{"future_mae", "p95_error", "p99_error", "wrong_direction_area",
        "stop_residual", "clamp_dwell_ms", "noise_amplification"}};
    std::ostringstream outliers;
    outliers << "preset,role,metric,scenario_id,value,paired_baseline_value\n";
    for (const std::string &preset : presets) {
        const ConfigRun *baseline = findRun(runs, preset, "baseline");
        const ConfigRun *candidate = findRun(runs, preset, "recommended");
        for (const std::string &metric : outlierNames) {
            const auto worst = std::max_element(candidate->scenarios.cbegin(), candidate->scenarios.cend(), [&metric](const ScenarioRun &a, const ScenarioRun &b) {
                return metricValue(a.metrics, metric) < metricValue(b.metrics, metric);
            });
            const ScenarioRun *paired = findScenario(*baseline, worst->id);
            outliers << preset << ",recommended," << metric << ',' << csv(worst->id).toStdString() << ',' << metricValue(worst->metrics, metric)
                     << ',' << (paired ? metricValue(paired->metrics, metric) : 0.0) << '\n';
        }
    }

    const QString sentinelRaw = output.filePath(u"architecture-sentinel-raw"_qs);
    int sentinelExit = -1;
    QString sentinelOutput;
    const bool sentinelStarted = runProgram(sentinelPath, {u"--campaign"_qs, u"canonical"_qs, u"--output"_qs, sentinelRaw},
                                            &sentinelExit, &sentinelOutput);
    const QString canonicalAnalysis = readText(QDir(sentinelRaw).filePath(u"Adaptive_Response_V2.3.T_Canonical_Findings_Analysis.md"_qs));
    const int productConfirmed = extractInt(canonicalAnalysis, QRegularExpression(u"confirmed product defects:\\s*(\\d+)"_qs));
    const int productSuspect = extractInt(canonicalAnalysis, QRegularExpression(u"product suspects:\\s*(\\d+)"_qs));
    const QString summary = readText(QDir(sentinelRaw).filePath(u"summary.csv"_qs));
    const QStringList summaryRows = summary.split('\n', Qt::SkipEmptyParts);
    const int sentinelHard = summaryRows.size() > 1 ? summaryRows[1].split(',').value(3).toInt() : -1;
    const bool sentinelPass = sentinelStarted && sentinelExit == 0 && sentinelHard == 0 && productConfirmed == 0 && productSuspect == 0;

    int benchmarkExit = -1;
    QString benchmarkOutput;
    const bool benchmarkStarted = runProgram(benchmarkPath, {}, &benchmarkExit, &benchmarkOutput);
    QRegularExpression allocationExpression(u"hot_path_allocations=(\\d+)"_qs);
    QRegularExpression p99Expression(u"p99_us=([0-9]+(?:\\.[0-9]+)?)"_qs);
    int allocationFailures = 0;
    double maximumP99 = 0.0;
    QRegularExpressionMatchIterator allocationMatches = allocationExpression.globalMatch(benchmarkOutput);
    while (allocationMatches.hasNext()) if (allocationMatches.next().captured(1).toInt() != 0) ++allocationFailures;
    QRegularExpressionMatchIterator p99Matches = p99Expression.globalMatch(benchmarkOutput);
    while (p99Matches.hasNext()) maximumP99 = std::max(maximumP99, p99Matches.next().captured(1).toDouble());
    const bool benchmarkPass = benchmarkStarted && benchmarkExit == 0 && allocationFailures == 0 && maximumP99 > 0.0 && maximumP99 < 100.0;
    if (!sentinelPass || !benchmarkPass) {
        for (const std::string &preset : presets) {
            if (!sentinelPass) decisionReasons[preset].push_back("canonical-architecture-sentinel");
            if (!benchmarkPass) decisionReasons[preset].push_back("hot-path-performance-sentinel");
        }
    }

    size_t accepted = 0;
    std::ostringstream decisions;
    decisions << "# Phase 3 final preset decisions\n\n";
    for (const std::string &preset : presets) {
        const bool passes = decisionReasons[preset].empty();
        if (passes) ++accepted;
        decisions << "## " << preset << "\n\n" << (passes ? "RECOMMENDED CANDIDATE ACCEPTED" : "RECOMMENDED CANDIDATE REJECTED") << "\n\n";
        if (!passes) {
            decisions << "Reasons: ";
            for (size_t index = 0; index < decisionReasons[preset].size(); ++index) {
                if (index) decisions << "; ";
                decisions << decisionReasons[preset][index];
            }
            decisions << "\n\n";
        }
    }
    const std::string disposition = accepted == 5
        ? "PHASE 3 PASS - ALL OPTIMIZED PRESETS VALIDATED ON UNTOUCHED HOLDOUT - READY FOR FINAL PRODUCTION INTEGRATION"
        : accepted == 0 ? "PHASE 3 FAIL - OPTIMIZED PRESET FAMILY DOES NOT GENERALIZE"
        : "PHASE 3 PARTIAL - ONE OR MORE OPTIMIZED PRESETS FAILED HOLDOUT";
    decisions << "## Final disposition\n\n" << disposition << '\n';

    std::ostringstream architecture;
    architecture << "freeze_sha,canonical_exit_code,canonical_hard_failures,product_confirmed,product_suspect,canonical_pass,raw_directory\n"
                 << kFreezeSha << ',' << sentinelExit << ',' << sentinelHard << ',' << productConfirmed << ',' << productSuspect << ','
                 << (sentinelPass ? "true" : "false") << ',' << csv(sentinelRaw.toStdString()).toStdString() << '\n';
    std::ostringstream benchmark;
    benchmark << "# Phase 3 Release hot-path benchmark\n\n"
              << "Executable: " << benchmarkPath.toStdString() << "\n\n"
              << "Exit code: " << benchmarkExit << "\n\n"
              << "Non-zero allocation observations: " << allocationFailures << "\n\n"
              << "Maximum observed p99: " << maximumP99 << " us\n\n"
              << "Sentinel result: " << (benchmarkPass ? "PASS" : "FAIL") << "\n\n"
              << "Output follows.\n\n" << benchmarkOutput.toStdString();

    std::ostringstream report;
    report << "# V2.3.T Adaptive Response Phase 3 Holdout Validation Report\n\n"
           << "Production freeze SHA: " << kFreezeSha << "\n\n"
           << "Phase 2 evidence: training 0x23A10001 / 7a462261e36420f63a92babe93ba20bd34014308a8e0230407df8f8c1e9b1d62; "
           << "validation 0x23A10002 / ae4fd487dfb594ac43f729422592178a0fabf6cbc8e96134141809b4eed7c1f7.\n\n"
           << "First scored replay: holdout 0x23A10003, 100 scenarios, hash " << actualHoldoutHash.toStdString() << ".\n\n"
           << "Locked policy hash: " << policyHash.toStdString() << ". Candidate vector hash: " << candidateHash.toStdString()
           << ". Baseline vector hash: " << baselineHash.toStdString() << ".\n\n"
           << "Total primary executions: 1000.\n\n"
           << "Hard safety failures are detailed in holdout_aggregate_results.csv. Canonical sentinel: hard failures="
           << sentinelHard << ", PRODUCT_CONFIRMED=" << productConfirmed << ", PRODUCT_SUSPECT=" << productSuspect
           << ", pass=" << (sentinelPass ? "true" : "false") << ".\n\n"
           << "Family, outlier, tail, advance, wrong-direction, stop, noise, horizon/lead, ladder, and clamp analyses are in their named CSV artifacts. "
           << "The physical corpus was not replayed or used for tuning.\n\n"
           << "Adjacent behavioral ladder: " << (ladderPass ? "PASS" : "FAIL") << ".\n\n"
           << "Release hot-path benchmark: allocations=" << allocationFailures << ", maximum p99=" << maximumP99
           << " us, pass=" << (benchmarkPass ? "true" : "false") << ".\n\n"
           << "## Decisions\n\n";
    for (const std::string &preset : presets) {
        report << "- " << preset << ": " << (decisionReasons[preset].empty() ? "RECOMMENDED CANDIDATE ACCEPTED" : "RECOMMENDED CANDIDATE REJECTED") << "\n";
    }
    report << "\n## Final disposition\n\n" << disposition << '\n';

    if (!writeText(output.filePath(u"holdout_raw_results.csv"_qs), raw.str())
        || !writeText(output.filePath(u"holdout_aggregate_results.csv"_qs), aggregates.str())
        || !writeText(output.filePath(u"holdout_family_results.csv"_qs), familyResults.str())
        || !writeText(output.filePath(u"holdout_outliers.csv"_qs), outliers.str())
        || !writeText(output.filePath(u"baseline_vs_recommended.csv"_qs), comparisons.str())
        || !writeText(output.filePath(u"preset_ladder.csv"_qs), ladder.str())
        || !writeText(output.filePath(u"clamp_analysis.csv"_qs), clampResults.str())
        || !writeText(output.filePath(u"architecture_sentinel.csv"_qs), architecture.str())
        || !writeText(output.filePath(u"benchmark_results.md"_qs), benchmark.str())
        || !writeText(output.filePath(u"final_preset_decisions.md"_qs), decisions.str())
        || !writeText(reportPath, report.str())) return 7;

    const QString rawPath = output.filePath(u"holdout_raw_results.csv"_qs);
    const bool writeProtected = QFile::setPermissions(rawPath, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    std::ostringstream finalManifest;
    finalManifest << "{\n  \"phase\": \"complete\",\n  \"freeze_sha\": \"" << kFreezeSha
                  << "\",\n  \"holdout_seed\": \"0x23A10003\",\n  \"holdout_scenarios\": 100,\n  \"holdout_sha256\": \""
                  << actualHoldoutHash.toStdString() << "\",\n  \"first_scored_replay\": true,\n  \"candidate_vectors_sha256\": \""
                  << candidateHash.toStdString() << "\",\n  \"baseline_vectors_sha256\": \"" << baselineHash.toStdString()
                  << "\",\n  \"acceptance_policy_sha256\": \"" << policyHash.toStdString() << "\",\n  \"primary_executions\": 1000,\n"
                  << "  \"holdout_raw_results_sha256\": \"" << sha256File(rawPath).toStdString()
                  << "\",\n  \"aggregate_results_sha256\": \"" << sha256File(output.filePath(u"holdout_aggregate_results.csv"_qs)).toStdString()
                  << "\",\n  \"raw_results_write_protected\": " << (writeProtected ? "true" : "false")
                  << ",\n  \"canonical_sentinel_pass\": " << (sentinelPass ? "true" : "false")
                  << ",\n  \"benchmark_pass\": " << (benchmarkPass ? "true" : "false")
                  << ",\n  \"disposition\": \"" << disposition << "\"\n}\n";
    if (!writeText(output.filePath(u"manifest.json"_qs), finalManifest.str())) return 7;
    QTextStream(stdout) << QString::fromStdString(disposition) << "\nexecutions=1000 accepted=" << accepted << " artifact=" << outputPath << '\n';
    return accepted == 5 ? 0 : 8;
}

#if 0
    const std::array<std::string, 9> familyNames{{"precision", "stationary_noise", "slow_movement", "fast_ballistic",
        "stops", "reversals", "oscillatory_combat", "endpoints", "compound_motion"}};
    std::ostringstream familyResults;
    familyResults << "preset,role,candidate_id,family,scenario_count,samples," << metricColumns << '\n';
    for (const ConfigRun &run : runs) for (const std::string &family : familyNames) {
        std::vector<ScenarioRun> selected;
        for (const ScenarioRun &row : run.scenarios) {
            if (std::find(row.families.cbegin(), row.families.cend(), family) != row.families.cend()) selected.push_back(row);
        }
        const Metrics combined = aggregate(selected);
        familyResults << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ','
                      << family << ',' << selected.size() << ',' << combined.samples << ',' << metricsCsv(combined) << '\n';
    }

    std::ostringstream clampResults;
    clampResults << "preset,role,candidate_id,mean_clamp_fraction,p95_clamp_fraction,max_clamp_fraction,max_continuous_clamp_dwell_ms,scenarios_over_50_percent,scenarios_over_80_percent\n";
    for (const ConfigRun &run : runs) {
        std::vector<double> fractions;
        size_t over50 = 0, over80 = 0;
        for (const ScenarioRun &row : run.scenarios) {
            fractions.push_back(row.metrics.clampFraction);
            if (row.metrics.clampFraction > .50) ++over50;
            if (row.metrics.clampFraction > .80) ++over80;
        }
        clampResults << run.config.preset << ',' << run.config.role << ',' << csv(run.config.id).toStdString() << ','
                     << std::fixed << std::setprecision(8) << run.aggregate.clampFraction << ',' << percentile(fractions,.95)
                     << ',' << percentile(fractions,1.0) << ',' << run.aggregate.maxClampDwellMs << ',' << over50 << ',' << over80 << '\n';
    }
    std::map<std::string, std::vector<std::string>> decisionReasons;
    std::ostringstream comparisons;
    comparisons << "preset,baseline_id,recommended_id,advance_delta_ms,future_mae_ratio,p95_ratio,p99_ratio,useful_lead_delta,wrong_direction_area_delta,reacquisition_delta_ms,stop_residual_delta,noise_ratio,artificial_step_delta,clamp_fraction_delta,mean_horizon_delta_ms,maximum_lead_delta\n";
    for (const std::string &preset : presets) {
        const ConfigRun *baseline = findRun(runs, preset, "baseline");
        const ConfigRun *candidate = findRun(runs, preset, "recommended");
        const Threshold *threshold = thresholdFor(preset);
        if (!baseline || !candidate || !threshold) return 6;
        auto &reasons = decisionReasons[preset];
        if (hardFailure(candidate->aggregate)) reasons.push_back("hard-safety-or-correctness-gate");
        if (candidate->aggregate.advanceMs < baseline->aggregate.advanceMs + threshold->minAdvance) reasons.push_back("insufficient-temporal-advance");
        if (candidate->aggregate.futureMae > baseline->aggregate.futureMae * threshold->maxMae + .000001) reasons.push_back("future-mae-regression");
        if (candidate->aggregate.p95 > baseline->aggregate.p95 * threshold->maxP95 + .000001) reasons.push_back("p95-tail-regression");
        if (candidate->aggregate.p99 > baseline->aggregate.p99 * threshold->maxP99 + .000001) reasons.push_back("p99-tail-regression");
        if (candidate->aggregate.noiseAmplification > baseline->aggregate.noiseAmplification * threshold->maxNoise + .000001) reasons.push_back("noise-regression");
        if (candidate->aggregate.reacquisitionMs > baseline->aggregate.reacquisitionMs + threshold->maxReacq) reasons.push_back("reversal-regression");
        if (candidate->aggregate.stopResidual > baseline->aggregate.stopResidual + threshold->maxStop) reasons.push_back("stop-regression");
        if (candidate->aggregate.artificialStep > baseline->aggregate.artificialStep + threshold->maxStep) reasons.push_back("artificial-step-regression");
        if (candidate->aggregate.wrongArea > baseline->aggregate.wrongArea + threshold->maxWrong) reasons.push_back("wrong-direction-area-regression");
        comparisons << preset << ',' << csv(baseline->config.id).toStdString() << ',' << csv(candidate->config.id).toStdString() << ','
                    << std::fixed << std::setprecision(8) << candidate->aggregate.advanceMs - baseline->aggregate.advanceMs << ','
                    << (baseline->aggregate.futureMae == 0.0 ? 0.0 : candidate->aggregate.futureMae / baseline->aggregate.futureMae) << ','
                    << (baseline->aggregate.p95 == 0.0 ? 0.0 : candidate->aggregate.p95 / baseline->aggregate.p95) << ','
                    << (baseline->aggregate.p99 == 0.0 ? 0.0 : candidate->aggregate.p99 / baseline->aggregate.p99) << ','
                    << candidate->aggregate.usefulLead - baseline->aggregate.usefulLead << ','
                    << candidate->aggregate.wrongArea - baseline->aggregate.wrongArea << ','
                    << candidate->aggregate.reacquisitionMs - baseline->aggregate.reacquisitionMs << ','
                    << candidate->aggregate.stopResidual - baseline->aggregate.stopResidual << ','
                    << (baseline->aggregate.noiseAmplification == 0.0 ? 0.0 : candidate->aggregate.noiseAmplification / baseline->aggregate.noiseAmplification) << ','
                    << candidate->aggregate.artificialStep - baseline->aggregate.artificialStep << ','
                    << candidate->aggregate.clampFraction - baseline->aggregate.clampFraction << ','
                    << candidate->aggregate.meanHorizonMs - baseline->aggregate.meanHorizonMs << ','
                    << candidate->config.settings.maximumLead - baseline->config.settings.maximumLead << '\n';
    }

    std::ostringstream ladder;
    ladder << "lower_preset,upper_preset,lower_advance_ms,upper_advance_ms,advance_separation_ms,required_minimum_ms,passes\n";
    bool ladderPass = true;
    for (size_t index = 1; index < presets.size(); ++index) {
        const Metrics &lower = findRun(runs, presets[index - 1], "recommended")->aggregate;
        const Metrics &upper = findRun(runs, presets[index], "recommended")->aggregate;
        const bool passes = upper.advanceMs >= lower.advanceMs + .25;
        ladderPass = ladderPass && passes;
        ladder << presets[index - 1] << ',' << presets[index] << ',' << lower.advanceMs << ',' << upper.advanceMs << ','
               << upper.advanceMs - lower.advanceMs << ",0.250000," << (passes ? "true" : "false") << '\n';
        if (!passes) {
            decisionReasons[presets[index - 1]].push_back("adjacent-preset-ladder-collapse");
            decisionReasons[presets[index]].push_back("adjacent-preset-ladder-collapse");
        }
    }

    const std::array<std::string, 7> outlierNames{{"future_mae", "p95_error", "p99_error", "wrong_direction_area",
        "stop_residual", "clamp_dwell_ms", "noise_amplification"}};
    std::ostringstream outliers;
    outliers << "preset,role,metric,scenario_id,value,paired_baseline_value\n";
    for (const std::string &preset : presets) {
        const ConfigRun *baseline = findRun(runs, preset, "baseline");
        const ConfigRun *candidate = findRun(runs, preset, "recommended");
        for (const std::string &metric : outlierNames) {
            const auto worst = std::max_element(candidate->scenarios.cbegin(), candidate->scenarios.cend(), [&metric](const ScenarioRun &a, const ScenarioRun &b) {
                return metricValue(a.metrics, metric) < metricValue(b.metrics, metric);
            });
            const ScenarioRun *paired = findScenario(*baseline, worst->id);
            outliers << preset << ",recommended," << metric << ',' << csv(worst->id).toStdString() << ',' << metricValue(worst->metrics, metric)
                     << ',' << (paired ? metricValue(paired->metrics, metric) : 0.0) << '\n';
        }
    }
#endif
