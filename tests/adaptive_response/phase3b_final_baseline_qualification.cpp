// Test-only Phase 3B runner.  The Phase 2B source is included in this
// translation unit so the sealed corpus generator remains byte-identical.
#define main phase2bOptimizerLegacyMain
#include "phase2b_robust_preset_optimizer.cpp"
#undef main

#include <QJsonDocument>
#include <QJsonObject>

#include <set>

namespace {

constexpr const char *kFreezeSha = "055e047e2847fc2ec0f59ec457188f66124898e7";
constexpr const char *kExpectedHoldoutHash =
    "047f2ab3015bdd8a005ea877e59eb116ef6476c21821b6eb8eddb50d9b6a3ada";
constexpr const char *kExpectedPhysicalCombinedHash =
    "ff8bff8ac39651fb2e655ab618dfcb99f3e40931fcca2886f0a6202b9ffc4b40";

struct PresetExpectation {
    const char *name;
    const char *vector;
    double maxMae, maxP95, maxP99, minAdvance, maxAdvance;
    double minLead, maxLead, maxWrong, maxStop, maxStationary, maxNoise, minHorizon, maxHorizon;
};

constexpr std::array<PresetExpectation, 5> kPresets{{
    {"Light", "4.000000,0.055000,0.750000,0.150000,0.560000,0.350000,0.100000,4.000000,1.000000,0.850000,0.100000,0.060000,0.120000,0.060000,0.920000,0.012000,0.035000",
        .00304331,.01008450,.02145860,1.78006910,4.08436521,.00132325,.00309518,.00055678,.01287411,.00371283,2.81421077,1.07151482,2.72806937},
    {"Balanced", "8.000000,0.120000,0.820000,0.140000,0.720000,0.580000,0.220000,8.000000,1.000000,0.850000,0.180000,0.100000,0.220000,0.100000,0.920000,0.012000,0.035000",
        .00353717,.01095629,.02382861,3.67036998,8.31372198,.00270173,.00631733,.00111020,.02650911,.00751723,5.36394377,2.15120411,5.47464819},
    {"Fast", "12.000000,0.180000,0.880000,0.120000,0.800000,0.680000,0.350000,12.000000,1.000000,0.850000,0.280000,0.160000,0.320000,0.150000,0.920000,0.012000,0.035000",
        .00420116,.01269962,.02802102,5.63709028,12.57437189,.00412950,.00965790,.00167635,.04086369,.01141108,8.14809140,3.23245690,8.22902379},
    {"Aggressive", "18.000000,0.270000,0.940000,0.100000,0.910000,0.820000,0.480000,18.000000,1.000000,0.850000,0.380000,0.220000,0.420000,0.200000,0.920000,0.012000,0.035000",
        .00565712,.01716279,.03920556,8.66751261,19.04761791,.00643104,.01506903,.00254418,.06342306,.01748895,12.58791627,4.85912282,12.37640486},
    {"Extreme", "30.000000,0.400000,1.000000,0.080000,1.000000,0.950000,0.650000,24.000000,1.000000,0.850000,0.500000,0.300000,0.550000,0.280000,0.920000,0.012000,0.035000",
        .01038258,.03308961,.09332286,14.73920010,31.67025000,.01155427,.02708699,.00428568,.10873410,.03067483,22.14141677,8.11196866,20.66116368},
}};

struct ScenarioScore {
    ScenarioDefinition scenario;
    Metrics metrics;
    std::uint64_t unbounded = 0;
    std::vector<double> errors, advances, reacquisitions, horizons;
};

struct AggregateScore {
    Metrics metrics;
    std::uint64_t unbounded = 0;
    std::vector<ScenarioScore> rows;
};

struct Review { bool pass = false; std::string reasons; };

QString hashText(const std::string &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(text),
        QCryptographicHash::Sha256).toHex());
}

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(65536));
    return QString::fromLatin1(hash.result().toHex());
}

bool makeReadOnly(const QString &path)
{
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser
        | QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

bool finiteTelemetry(const AdaptiveResponseTelemetry &t)
{
    for (const float value : {t.physical,t.estimated,t.predicted,t.velocity,t.acceleration,t.activeHorizonSeconds,
             t.lead,t.confidence,t.motionIntensity,t.velocityAuthority,t.accelerationIntent,t.onsetAuthority,
             t.sustainedEvidence,t.sustainedAuthority,t.motionUrgency,t.horizonExtensionEligibility,
             t.normalMaximumHorizonSeconds,t.allowedMaximumHorizonSeconds,t.turningPointConfidence,
             t.estimatedTimeToTurnSeconds,t.estimatedRemainingTravel,t.turningPointHorizonLimitSeconds,
             t.turningPointLeadLimit,t.reacquisitionAuthority,t.motionCoherence,t.sourceUpdatePeriodSeconds,
             t.quietDurationSeconds,t.sourceStoppingSeconds,t.brakingReductionFactor}) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

ScenarioScore scoreOne(const ScenarioDefinition &scenario, const AdaptiveResponseSettings &settings)
{
    ScenarioScore score;
    score.scenario = scenario;
    const RuntimeAdaptiveResponseConfig runtime = toRuntime(settings);
    const ScenarioResult replay = replayScenario(scenario, runtime, "phase3b-production-baseline", true);
    Metrics &m = score.metrics;
    std::uint64_t moving = 0, inactive = 0, stationary = 0, clamped = 0, protectedSamples = 0, capped = 0;
    double errorSum = 0.0, leadSum = 0.0, horizonSum = 0.0, stationarySquares = 0.0;
    for (size_t i = 0; i < replay.trace.size() && i < replay.telemetry.size(); ++i) {
        const TraceSample &sample = replay.trace[i];
        const AdaptiveResponseTelemetry &t = replay.telemetry[i];
        ++m.samples;
        if (!std::isfinite(t.predicted) || !std::isfinite(t.lead)) ++m.nonFinite;
        if (t.predicted < -1.0001F || t.predicted > 1.0001F) ++m.illegalOutput;
        if (!finiteTelemetry(t) || std::abs(t.estimated) > 1.0001F || t.activeHorizonSeconds < -.000001F
            || t.activeHorizonSeconds > t.allowedMaximumHorizonSeconds + .0001F
            || t.allowedMaximumHorizonSeconds > runtime.maximumHorizonSeconds + runtime.horizonExtensionCapSeconds + .0001F
            || std::abs(t.lead) > runtime.maximumLead + .0001F) ++score.unbounded;
        const double error = std::abs(static_cast<double>(t.predicted)
            - intendedAt(replay.trace, sample.timeSeconds + t.activeHorizonSeconds));
        score.errors.push_back(error); errorSum += error;
        const double horizon = t.activeHorizonSeconds * 1000.0;
        score.horizons.push_back(horizon); horizonSum += horizon;
        m.maxExtensionMs = std::max(m.maxExtensionMs, std::max(0.0, horizon - settings.maximumHorizonMs));
        if (std::abs(sample.humanIntentVelocity) > .08F) {
            ++moving;
            if (std::abs(t.lead) < .00001F) ++inactive;
            const double advance = std::abs(t.lead / sample.humanIntentVelocity) * 1000.0;
            if (advance < 100.0) score.advances.push_back(advance);
            if (t.lead * sample.humanIntentVelocity < 0.0F) m.wrongArea += std::abs(t.lead) * sample.dtSeconds;
        }
        if (!sample.intendedMoving) { stationarySquares += t.lead * t.lead; ++stationary; }
        leadSum += std::abs(t.lead);
        if (t.safetyLimited) ++clamped;
        if (t.turningPointConfidence > .10F) ++protectedSamples;
        if (settings.horizonExtensionCapMs > 0.0F && horizon >= (settings.maximumHorizonMs + settings.horizonExtensionCapMs) * .98F) ++capped;
    }
    for (const ReversalEvent &event : replay.reversalEvents)
        if (event.reacquisitionTimeSeconds >= event.groundTruthTimeSeconds)
            score.reacquisitions.push_back((event.reacquisitionTimeSeconds - event.groundTruthTimeSeconds) * 1000.0);
    m.mae = m.samples ? errorSum / m.samples : 0.0;
    m.p95 = percentile(score.errors, .95); m.p99 = percentile(score.errors, .99);
    m.advanceMs = percentile(score.advances, .50); m.usefulLead = m.samples ? leadSum / m.samples : 0.0;
    m.meanHorizonMs = m.samples ? horizonSum / m.samples : 0.0; m.p95HorizonMs = percentile(score.horizons, .95);
    m.meanExtensionMs = std::max(0.0, m.meanHorizonMs - settings.maximumHorizonMs);
    m.stationaryRms = stationary ? std::sqrt(stationarySquares / stationary) : 0.0;
    m.noiseAmplification = replay.metrics.noiseAmplificationRatio;
    m.artificialStep = replay.metrics.maximumOutputStep;
    m.stationaryOffset = replay.metrics.stationaryDrift;
    m.stopResidual = std::abs(replay.metrics.leadAtPhysicalStop);
    m.clampFraction = m.samples ? static_cast<double>(clamped) / m.samples : 0.0;
    m.capFraction = m.samples ? static_cast<double>(capped) / m.samples : 0.0;
    m.inactiveFraction = moving ? static_cast<double>(inactive) / moving : 0.0;
    m.protectionFraction = m.samples ? static_cast<double>(protectedSamples) / m.samples : 0.0;
    return score;
}

AggregateScore aggregate(std::vector<ScenarioScore> rows)
{
    AggregateScore all;
    all.rows = std::move(rows);
    std::vector<double> errors, advances, reacquisitions, horizons;
    for (const ScenarioScore &row : all.rows) {
        const Metrics &m = row.metrics; Metrics &out = all.metrics;
        out.samples += m.samples; out.mae += m.mae; out.usefulLead += m.usefulLead; out.wrongArea += m.wrongArea;
        out.pattern73Area = std::max(out.pattern73Area, m.pattern73Area); out.stopResidual += m.stopResidual;
        out.stationaryRms += m.stationaryRms; out.noiseAmplification += m.noiseAmplification;
        out.artificialStep = std::max(out.artificialStep, m.artificialStep); out.clampFraction += m.clampFraction;
        out.capFraction += m.capFraction; out.inactiveFraction += m.inactiveFraction; out.protectionFraction += m.protectionFraction;
        out.meanHorizonMs += m.meanHorizonMs; out.meanExtensionMs += m.meanExtensionMs;
        out.maxExtensionMs = std::max(out.maxExtensionMs, m.maxExtensionMs); out.nonFinite += m.nonFinite;
        out.illegalOutput += m.illegalOutput; out.stationaryOffset += m.stationaryOffset; all.unbounded += row.unbounded;
        errors.insert(errors.end(), row.errors.cbegin(), row.errors.cend());
        advances.insert(advances.end(), row.advances.cbegin(), row.advances.cend());
        reacquisitions.insert(reacquisitions.end(), row.reacquisitions.cbegin(), row.reacquisitions.cend());
        horizons.insert(horizons.end(), row.horizons.cbegin(), row.horizons.cend());
    }
    const double n = std::max<size_t>(1, all.rows.size()); Metrics &out = all.metrics;
    out.mae /= n; out.usefulLead /= n; out.wrongArea /= n; out.stopResidual /= n; out.stationaryRms /= n;
    out.noiseAmplification /= n; out.clampFraction /= n; out.capFraction /= n; out.inactiveFraction /= n;
    out.protectionFraction /= n; out.meanHorizonMs /= n; out.meanExtensionMs /= n;
    out.p95 = percentile(errors,.95); out.p99 = percentile(errors,.99); out.advanceMs = percentile(advances,.50);
    out.reacquisitionMs = percentile(reacquisitions,.50); out.p95HorizonMs = percentile(horizons,.95);
    return all;
}

std::string familyName(const ScenarioDefinition &s)
{
    const std::string &id = s.id;
    if (id.find("precision-pilot") != std::string::npos) return "Precision Pilot";
    if (id.find("combat-helicopter") != std::string::npos) return "Combat Helicopter";
    if (id.find("fixed-wing") != std::string::npos) return "Fixed-Wing";
    if (id.find("space-sim") != std::string::npos) return "Space-Sim";
    if (id.find("noisy-older-sensor") != std::string::npos) return "Noisy Sensor";
    return "Structured motion";
}

std::string metricsHeader()
{
    return "future_mae,p95_error,p99_error,temporal_advance_ms,useful_lead,wrong_direction_area,pattern73_wrong_direction_area,"
        "reversal_reacquisition_ms,stop_residual,stationary_lead_rms,noise_amplification,mean_active_horizon_ms,"
        "p95_active_horizon_ms,non_finite,illegal_output,persistent_stationary_offset,unbounded_estimator_telemetry";
}

std::string metricsValues(const Metrics &m, std::uint64_t unbounded)
{
    std::ostringstream out; out << std::fixed << std::setprecision(8)
        << m.mae << ',' << m.p95 << ',' << m.p99 << ',' << m.advanceMs << ',' << m.usefulLead << ','
        << m.wrongArea << ',' << m.pattern73Area << ',' << m.reacquisitionMs << ',' << m.stopResidual << ','
        << m.stationaryRms << ',' << m.noiseAmplification << ',' << m.meanHorizonMs << ',' << m.p95HorizonMs << ','
        << m.nonFinite << ',' << m.illegalOutput << ',' << m.stationaryOffset << ',' << unbounded;
    return out.str();
}

std::string policyText()
{
    std::ostringstream out;
    out << "# V2.3.T Phase 3B locked qualification policy\n\n"
        << "Frozen production SHA: " << kFreezeSha << "\n\n"
        << "This is a baseline-only qualification. It executes the retained production presets only; no candidate selection, tuning, perturbation, or substitution is permitted.\n\n"
        << "Corpus: generator phase2b-persona-structured-v1; seed 0x2B10F00D; 200 scenarios; SHA-256 "
        << kExpectedHoldoutHash << ". Primary workload is five presets times 200 scenarios, or 1000 executions.\n\n"
        << "Metric definitions: future error compares prediction to intended position at the active horizon; temporal advance is median abs(lead/intended velocity) for moving samples below 100ms; useful lead is mean abs(lead); noise amplification, stop residual, reversal reacquisition, persistent stationary offset, and Pattern-73 use the established verification semantics. Unbounded estimator telemetry means non-finite telemetry, estimated position outside [-1,1], lead beyond the configured maximum, or active/allowed horizon beyond its frozen cap.\n\n"
        << "Absolute gates: each preset must have non-finite=0, illegal output=0, persistent stationary offset=0, unbounded estimator telemetry=0. Reversal reacquisition must not exceed 100ms and stop residual must not exceed 0.12. Post-holdout canonical verification must report hard failures=0, PRODUCT_CONFIRMED=0, PRODUCT_SUSPECT=0, and Pattern-73 wrong-direction lead area no greater than 0.0001.\n\n"
        << "The fixed numeric envelope is written in generalization_envelope.csv. It was derived before replay from the larger of Search A/B production-baseline variation and independent historical-baseline variation where available. Error, tail, and noise ceilings use a 1.5x allowance; temporal response uses a 0.7x to 1.5x interval. Generic wrong-direction area, stop residual, and stationary RMS use a 2.5x Search A/B ceiling plus the absolute gates. The purpose is to flag a substantial distribution shift, not require equality across corpora.\n\n"
        << "Preset personality requires strict aggregate ordering in median temporal advance, mean active horizon, and useful lead: Light < Balanced < Fast < Aggressive < Extreme. Light must remain the lowest/stablest response and Extreme the highest response.\n\n"
        << "Prepare mode locks and read-protects this policy, metric definitions, exact vectors, and corpus receipt before any scored replay. Execute mode writes a read-protected first-scored-replay receipt before it begins and refuses any second invocation.\n";
    return out.str();
}

std::string metricDefinitions()
{
    return "future_mae=mean absolute predicted-vs-intended-at-active-horizon error\n"
        "p95_error=95th percentile predicted-vs-intended-at-active-horizon error\n"
        "p99_error=99th percentile predicted-vs-intended-at-active-horizon error\n"
        "temporal_advance_ms=median abs(lead/intended_velocity) on moving samples below 100ms\n"
        "useful_lead=mean abs(lead)\nwrong_direction_area=mean scenario integral of opposed lead\n"
        "stop_residual=abs(lead at physical stop)\nreversal_reacquisition_ms=median replay event delay\n"
        "stationary_lead_rms=RMS lead while intended motion is stationary\n"
        "noise_amplification=verification harness noise ratio\n"
        "mean_active_horizon_ms=mean activeHorizonSeconds times 1000\n"
        "unbounded_estimator_telemetry=frozen telemetry-bounds sentinel\n";
}

std::string vectorsCsv(bool *valid)
{
    bool okay = true;
    std::ostringstream out;
    out << "preset,expected_vector,production_vector,axis_count,all_axes_exact\n";
    const auto &builtIns = builtInAdaptiveResponsePresets();
    for (const PresetExpectation &expected : kPresets) {
        const auto found = std::find_if(builtIns.cbegin(), builtIns.cend(), [&expected](const AdaptiveResponsePreset &preset) {
            return preset.name.toStdString() == expected.name;
        });
        const size_t axes = found == builtIns.cend() ? 0U : found->axes.size();
        std::string actual;
        bool match = found != builtIns.cend();
        if (found != builtIns.cend()) {
            actual = parameterCsv(found->axes[0].settings);
            match = actual == expected.vector;
            for (const auto &axis : found->axes) match = match && parameterCsv(axis.settings) == expected.vector;
        }
        okay = okay && match;
        out << expected.name << ',' << csv(expected.vector).toStdString() << ',' << csv(actual).toStdString()
            << ',' << axes << ',' << (match ? 1 : 0) << '\n';
    }
    if (valid) *valid = okay;
    return out.str();
}

bool verifyHoldout(QString *error)
{
    const auto holdout = freshCorpus(kFinalHoldoutSeed, "phase3b-final-holdout", 30, 50);
    const QString actual = corpusHash(holdout);
    if (holdout.size() != kFinalHoldoutScenarios || actual != QString::fromLatin1(kExpectedHoldoutHash)) {
        if (error) *error = "holdout identity mismatch: scenarios=" + QString::number(holdout.size()) + " sha256=" + actual;
        return false;
    }
    return true;
}

std::set<QString> namesIn(const QDir &dir)
{
    const QStringList names = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
    return {names.cbegin(), names.cend()};
}

bool prepare(const QString &path, const QString &characterization, QString *error)
{
    QDir output(path);
    if (output.exists() && !namesIn(output).empty()) {
        if (error) *error = "prepare requires an empty output directory";
        return false;
    }
    if (!output.exists() && !QDir().mkpath(path)) {
        if (error) *error = "unable to create output directory";
        return false;
    }
    if (!QFileInfo::exists(characterization)) {
        if (error) *error = "missing Phase 2B baseline characterization";
        return false;
    }
    if (!verifyHoldout(error)) return false;
    bool vectorsValid = false;
    const std::string vectors = vectorsCsv(&vectorsValid);
    if (!vectorsValid) {
        if (error) *error = "production preset vector differs from frozen selection";
        return false;
    }
    const std::string policy = policyText();
    const std::string definitions = metricDefinitions();
    const bool lockedInputsSaved = writeText(output.filePath("locked_acceptance_policy.md"), policy)
        && writeText(output.filePath("locked_metric_definitions.txt"), definitions)
        && writeText(output.filePath("production_preset_vectors.csv"), vectors);
    if (!lockedInputsSaved) {
        if (error) *error = "unable to write locked pre-replay inputs";
        return false;
    }
    const QString policyHash = hashFile(output.filePath("locked_acceptance_policy.md"));
    const QString definitionsHash = hashFile(output.filePath("locked_metric_definitions.txt"));
    const QString vectorHash = hashFile(output.filePath("production_preset_vectors.csv"));
    QString characterizationPath = characterization;
    characterizationPath.replace('\\','/');
    std::ostringstream lock;
    lock << "{\n  \"phase\": \"pre-replay-locked\",\n  \"freeze_sha\": \"" << kFreezeSha
         << "\",\n  \"corpus_generator\": \"" << kCorpusGeneratorVersion << "\",\n"
         << "  \"holdout_seed\": \"0x2B10F00D\",\n  \"holdout_scenarios\": 200,\n  \"holdout_sha256\": \""
         << kExpectedHoldoutHash << "\",\n  \"production_vectors_sha256\": \"" << vectorHash.toStdString()
         << "\",\n  \"acceptance_policy_sha256\": \"" << policyHash.toStdString()
         << "\",\n  \"metric_definitions_sha256\": \"" << definitionsHash.toStdString()
         << "\",\n  \"phase2b_characterization_path\": \"" << characterizationPath.toStdString()
         << "\",\n  \"phase2b_characterization_sha256\": \"" << hashFile(characterization).toStdString()
         << "\",\n  \"permitted_primary_executions\": 1000,\n  \"first_scored_replay\": false\n}\n";
    const bool saved = writeText(output.filePath("pre_replay_lock.json"), lock.str());
    if (!saved) {
        if (error) *error = "unable to write pre-replay lock";
        return false;
    }
    for (const QString &name : {"locked_acceptance_policy.md","locked_metric_definitions.txt",
             "production_preset_vectors.csv","pre_replay_lock.json"}) makeReadOnly(output.filePath(name));
    return true;
}

bool verifyLock(const QDir &output, QString *error)
{
    const std::set<QString> expected{"locked_acceptance_policy.md","locked_metric_definitions.txt",
        "production_preset_vectors.csv","pre_replay_lock.json"};
    if (namesIn(output) != expected) {
        if (error) *error = "execute requires exactly the unmodified pre-replay lock";
        return false;
    }
    QFile lockFile(output.filePath("pre_replay_lock.json"));
    lockFile.open(QIODevice::ReadOnly);
    const QString lock = QString::fromUtf8(lockFile.readAll());
    bool vectorsValid = false;
    const std::string vectors = vectorsCsv(&vectorsValid);
    const bool intact = vectorsValid
        && lock.contains(kExpectedHoldoutHash)
        && lock.contains(hashFile(output.filePath("locked_acceptance_policy.md")))
        && lock.contains(hashFile(output.filePath("locked_metric_definitions.txt")))
        && lock.contains(hashFile(output.filePath("production_preset_vectors.csv")))
        && lock.contains("\"first_scored_replay\": false");
    if (!intact) {
        if (error) *error = "pre-replay lock integrity failure";
        return false;
    }
    return verifyHoldout(error);
}

Review review(const Metrics &m, std::uint64_t unbounded, const PresetExpectation &e)
{
    std::vector<std::string> failures;
    const auto cap = [&failures](double actual, double maximum, const char *name) {
        if (actual > maximum) failures.emplace_back(name);
    };
    const auto interval = [&failures](double actual, double minimum, double maximum, const char *name) {
        if (actual < minimum || actual > maximum) failures.emplace_back(name);
    };
    if (m.nonFinite) failures.emplace_back("non-finite");
    if (m.illegalOutput) failures.emplace_back("illegal-output");
    if (m.stationaryOffset) failures.emplace_back("persistent-stationary-offset");
    if (unbounded) failures.emplace_back("unbounded-estimator-telemetry");
    cap(m.mae,e.maxMae,"future-mae"); cap(m.p95,e.maxP95,"p95"); cap(m.p99,e.maxP99,"p99");
    interval(m.advanceMs,e.minAdvance,e.maxAdvance,"temporal-advance");
    interval(m.usefulLead,e.minLead,e.maxLead,"useful-lead");
    cap(m.wrongArea,e.maxWrong,"wrong-direction-area"); cap(m.stopResidual,e.maxStop,"stop-residual");
    cap(m.stationaryRms,e.maxStationary,"stationary-lead-rms"); cap(m.noiseAmplification,e.maxNoise,"noise-amplification");
    interval(m.meanHorizonMs,e.minHorizon,e.maxHorizon,"mean-active-horizon"); cap(m.reacquisitionMs,100.0,"reversal-reacquisition");
    Review result; result.pass = failures.empty();
    for (size_t i = 0; i < failures.size(); ++i) { if (i) result.reasons += ';'; result.reasons += failures[i]; }
    return result;
}

bool execute(const QString &path, QString *error)
{
    const QDir output(path);
    if (!verifyLock(output,error)) return false;
    const std::string receipt = "{\n  \"first_scored_replay\": true,\n  \"primary_executions_planned\": 1000,\n"
        "  \"holdout_seed\": \"0x2B10F00D\",\n  \"holdout_sha256\": \""
        + std::string(kExpectedHoldoutHash) + "\"\n}\n";
    if (!writeText(output.filePath("first_scored_replay_receipt.json"),receipt)
        || !makeReadOnly(output.filePath("first_scored_replay_receipt.json"))) {
        if (error) *error = "unable to establish first-scored-replay receipt";
        return false;
    }
    const auto holdout = freshCorpus(kFinalHoldoutSeed,"phase3b-final-holdout",30,50);
    std::map<std::string,AggregateScore> totals;
    std::map<std::string,std::map<std::string,AggregateScore>> families;
    std::ostringstream raw;
    raw << "preset,scenario_index,scenario_id,family," << metricsHeader() << "\n";
    for (const PresetExpectation &preset : kPresets) {
        const auto settings = baselineFor(preset.name);
        if (!settings) {
            if (error) *error = "missing production preset " + QString::fromLatin1(preset.name);
            return false;
        }
        std::vector<ScenarioScore> scores;
        scores.reserve(holdout.size());
        for (size_t index = 0; index < holdout.size(); ++index) {
            ScenarioScore score = scoreOne(holdout[index],*settings);
            raw << csv(preset.name).toStdString() << ',' << index << ',' << csv(score.scenario.id).toStdString()
                << ',' << csv(familyName(score.scenario)).toStdString() << ','
                << metricsValues(score.metrics,score.unbounded) << '\n';
            scores.push_back(std::move(score));
        }
        totals.emplace(preset.name,aggregate(std::move(scores)));
        for (const ScenarioScore &score : totals.at(preset.name).rows)
            families[preset.name][familyName(score.scenario)].rows.push_back(score);
        for (auto &[family,value] : families[preset.name]) value = aggregate(std::move(value.rows));
    }

    std::map<std::string,Review> reviews;
    std::ostringstream aggregateCsv, familyCsv, outlierCsv, envelopeCsv, ladderCsv;
    aggregateCsv << "preset," << metricsHeader() << ",generalization_pass,generalization_reasons\n";
    familyCsv << "preset,family," << metricsHeader() << "\n";
    outlierCsv << "preset,metric,scenario_id,family,value\n";
    envelopeCsv << "preset,max_future_mae,max_p95,max_p99,min_temporal_advance_ms,max_temporal_advance_ms,"
        "min_useful_lead,max_useful_lead,max_wrong_direction_area,max_stop_residual,max_stationary_lead_rms,"
        "max_noise_amplification,min_mean_horizon_ms,max_mean_horizon_ms,actual_generalization_pass,actual_generalization_reasons\n";
    for (const PresetExpectation &preset : kPresets) {
        const AggregateScore &total = totals.at(preset.name);
        const Review result = review(total.metrics,total.unbounded,preset);
        reviews.emplace(preset.name,result);
        aggregateCsv << preset.name << ',' << metricsValues(total.metrics,total.unbounded) << ','
            << (result.pass ? 1 : 0) << ',' << csv(result.reasons).toStdString() << '\n';
        envelopeCsv << preset.name << ',' << std::fixed << std::setprecision(8)
            << preset.maxMae << ',' << preset.maxP95 << ',' << preset.maxP99 << ',' << preset.minAdvance << ','
            << preset.maxAdvance << ',' << preset.minLead << ',' << preset.maxLead << ',' << preset.maxWrong << ','
            << preset.maxStop << ',' << preset.maxStationary << ',' << preset.maxNoise << ',' << preset.minHorizon << ','
            << preset.maxHorizon << ',' << (result.pass ? 1 : 0) << ',' << csv(result.reasons).toStdString() << '\n';
        for (const auto &[family,value] : families.at(preset.name))
            familyCsv << preset.name << ',' << csv(family).toStdString() << ','
                << metricsValues(value.metrics,value.unbounded) << '\n';
        const std::array<std::pair<const char *,std::function<double(const ScenarioScore &)>>,7> metricSelectors{{
            {"future_mae",[](const ScenarioScore &s){ return s.metrics.mae; }},
            {"p95_error",[](const ScenarioScore &s){ return s.metrics.p95; }},
            {"p99_error",[](const ScenarioScore &s){ return s.metrics.p99; }},
            {"wrong_direction_area",[](const ScenarioScore &s){ return s.metrics.wrongArea; }},
            {"stop_residual",[](const ScenarioScore &s){ return s.metrics.stopResidual; }},
            {"reversal_reacquisition_ms",[](const ScenarioScore &s){ return s.metrics.reacquisitionMs; }},
            {"noise_amplification",[](const ScenarioScore &s){ return s.metrics.noiseAmplification; }},
        }};
        for (const auto &[metric,select] : metricSelectors) {
            const auto worst = std::max_element(total.rows.cbegin(),total.rows.cend(),
                [&select](const ScenarioScore &a,const ScenarioScore &b){ return select(a) < select(b); });
            outlierCsv << preset.name << ',' << metric << ',' << csv(worst->scenario.id).toStdString() << ','
                << csv(familyName(worst->scenario)).toStdString() << ',' << std::fixed << std::setprecision(8)
                << select(*worst) << '\n';
        }
    }
    const auto &light = totals.at("Light").metrics, &balanced = totals.at("Balanced").metrics;
    const auto &fast = totals.at("Fast").metrics, &aggressive = totals.at("Aggressive").metrics, &extreme = totals.at("Extreme").metrics;
    const bool ladder = light.advanceMs < balanced.advanceMs && balanced.advanceMs < fast.advanceMs
        && fast.advanceMs < aggressive.advanceMs && aggressive.advanceMs < extreme.advanceMs
        && light.meanHorizonMs < balanced.meanHorizonMs && balanced.meanHorizonMs < fast.meanHorizonMs
        && fast.meanHorizonMs < aggressive.meanHorizonMs && aggressive.meanHorizonMs < extreme.meanHorizonMs
        && light.usefulLead < balanced.usefulLead && balanced.usefulLead < fast.usefulLead
        && fast.usefulLead < aggressive.usefulLead && aggressive.usefulLead < extreme.usefulLead;
    ladderCsv << "preset,temporal_advance_ms,mean_active_horizon_ms,useful_lead,ordered_ladder_pass\n";
    for (const PresetExpectation &preset : kPresets) {
        const Metrics &m = totals.at(preset.name).metrics;
        ladderCsv << preset.name << ',' << std::fixed << std::setprecision(8) << m.advanceMs << ','
            << m.meanHorizonMs << ',' << m.usefulLead << ',' << (ladder ? 1 : 0) << '\n';
    }
    bool allPass = ladder;
    for (const auto &[name,result] : reviews) allPass = allPass && result.pass;
    const bool written = writeText(output.filePath("holdout_raw_results.csv"),raw.str())
        && writeText(output.filePath("holdout_aggregate_results.csv"),aggregateCsv.str())
        && writeText(output.filePath("holdout_family_results.csv"),familyCsv.str())
        && writeText(output.filePath("holdout_outliers.csv"),outlierCsv.str())
        && writeText(output.filePath("generalization_envelope.csv"),envelopeCsv.str())
        && writeText(output.filePath("preset_ladder.csv"),ladderCsv.str());
    if (!written) {
        if (error) *error = "scored replay completed but evidence output could not be written";
        return false;
    }
    makeReadOnly(output.filePath("holdout_raw_results.csv"));
    std::ostringstream manifest;
    manifest << "{\n  \"phase\": \"scored-holdout-complete\",\n  \"freeze_sha\": \"" << kFreezeSha
        << "\",\n  \"first_scored_replay\": true,\n  \"primary_executions\": 1000,\n  \"holdout_seed\": \"0x2B10F00D\",\n"
        << "  \"holdout_sha256\": \"" << kExpectedHoldoutHash << "\",\n  \"raw_results_sha256\": \""
        << hashFile(output.filePath("holdout_raw_results.csv")).toStdString() << "\",\n  \"all_presets_generalize\": "
        << (allPass ? "true" : "false") << ",\n  \"preset_ladder_pass\": " << (ladder ? "true" : "false") << "\n}\n";
    if (!writeText(output.filePath("scored_holdout_manifest.json"),manifest.str())
        || !makeReadOnly(output.filePath("scored_holdout_manifest.json"))) {
        if (error) *error = "unable to finish scored-holdout manifest";
        return false;
    }
    return allPass;
}

bool runCanonical(const QString &path, QString *error)
{
    QDir output(path);
    if (output.exists() && !namesIn(output).empty()) {
        if (error) *error = "canonical output must be empty";
        return false;
    }
    if (!output.exists() && !QDir().mkpath(path)) {
        if (error) *error = "unable to create canonical output";
        return false;
    }
    std::vector<QByteArray> arguments{{"adaptive_response_phase3b_qualification"},{"--campaign"},{"canonical"},
        {"--model"},{"presets"},{"--output"},path.toUtf8()};
    std::vector<char *> argv;
    for (QByteArray &arg : arguments) argv.push_back(arg.data());
    const int result = runAdaptiveResponseVerification(static_cast<int>(argv.size()),argv.data());
    if (result != 0 && error) *error = "canonical verification exited " + QString::number(result);
    return result == 0;
}

bool recordPhysicalIdentity(const QString &outputPath, const QString &fixturePath, QString *error)
{
    QDir output(outputPath);
    const QDir fixtures(fixturePath);
    if (!output.exists() && !QDir().mkpath(outputPath)) {
        if (error) *error = "could not create physical identity output directory";
        return false;
    }
    if (!fixtures.exists()) {
        if (error) *error = "physical fixture directory missing";
        return false;
    }
    const QFileInfoList files = fixtures.entryInfoList({"*.csv"},QDir::Files,QDir::Name);
    QCryptographicHash combined(QCryptographicHash::Sha256);
    std::uint64_t samples = 0;
    std::ostringstream rows;
    rows << "trace,source_samples,sha256\n";
    for (const QFileInfo &info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = "could not read " + info.fileName();
            return false;
        }
        const QByteArray bytes = file.readAll();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        const std::uint64_t sourceSamples = bytes.count('\n') > 0
            ? static_cast<std::uint64_t>(bytes.count('\n') - 1) : 0;
        samples += sourceSamples;
        combined.addData(info.fileName().toUtf8()); combined.addData(hash.toUtf8());
        rows << csv(info.fileName().toStdString()).toStdString() << ',' << sourceSamples << ',' << hash.toStdString() << '\n';
    }
    const QString actual = QString::fromLatin1(combined.result().toHex());
    const bool matched = files.size() == 15 && samples == 6076
        && actual == QString::fromLatin1(kExpectedPhysicalCombinedHash);
    std::ostringstream receipt;
    receipt << "traces=" << files.size() << "\nsource_samples=" << samples << "\ncombined_sha256=" << actual.toStdString()
        << "\nexpected_combined_sha256=" << kExpectedPhysicalCombinedHash << "\nidentity_match="
        << (matched ? "true" : "false") << "\nreplayed=false\n";
    if (!writeText(output.filePath("physical_trace_identity.csv"),rows.str())
        || !writeText(output.filePath("physical_trace_sentinel.txt"),receipt.str())) {
        if (error) *error = "could not write physical trace identity receipt";
        return false;
    }
    return matched;
}

int canonicalHardFailures(const QString &path)
{
    QFile file(QDir(path).filePath("summary.json"));
    if (!file.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(file.readAll()).object().value("hardFailures").toInt(-1);
}

std::uint64_t canonicalCount(const QString &path, const QString &category)
{
    QFile file(QDir(path).filePath("canonical_finding_clusters.csv"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return std::numeric_limits<std::uint64_t>::max();
    QTextStream input(&file);
    input.readLine();
    std::uint64_t total = 0;
    const QString prefix = QStringLiteral("\"") + category + QStringLiteral("\"");
    while (!input.atEnd()) {
        const QStringList fields = input.readLine().split(',');
        if (fields.value(0) != prefix) continue;
        bool okay = false;
        const std::uint64_t rows = fields.value(15).toULongLong(&okay);
        total += okay ? rows : 1;
    }
    return total;
}

double canonicalPattern73Area(const QString &path)
{
    QFile file(QDir(path).filePath("reversal_events.csv"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return std::numeric_limits<double>::infinity();
    QTextStream input(&file);
    input.readLine();
    double maximum = -1.0;
    while (!input.atEnd()) {
        const QStringList fields = input.readLine().split(',');
        if (!fields.value(1).contains("pattern-73")) continue;
        bool okay = false;
        const double area = fields.value(18).toDouble(&okay);
        if (!okay) return std::numeric_limits<double>::infinity();
        maximum = std::max(maximum,area);
    }
    return maximum;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return "unavailable\n";
    return QString::fromUtf8(file.readAll());
}

bool finalize(const QString &outputPath, const QString &canonicalPath, const QString &hotpathPath,
              const QString &reportPath, QString *error)
{
    const QDir output(outputPath);
    const QString manifestPath = output.filePath("scored_holdout_manifest.json");
    const QString correctedPhysicalPath = output.filePath("physical-sentinel-corrected/physical_trace_sentinel.txt");
    const QString physicalPath = QFileInfo::exists(correctedPhysicalPath)
        ? correctedPhysicalPath : output.filePath("physical_trace_sentinel.txt");
    if (!QFileInfo::exists(manifestPath) || !QFileInfo::exists(QDir(canonicalPath).filePath("summary.json"))
        || !QFileInfo::exists(hotpathPath) || !QFileInfo::exists(physicalPath)) {
        if (error) *error = "finalization requires scored holdout, canonical, hotpath, and physical identity evidence";
        return false;
    }
    const QString manifest = readText(manifestPath);
    const int hard = canonicalHardFailures(canonicalPath);
    const std::uint64_t confirmed = canonicalCount(canonicalPath,"PRODUCT_CONFIRMED");
    const std::uint64_t suspect = canonicalCount(canonicalPath,"PRODUCT_SUSPECT");
    const double pattern73 = canonicalPattern73Area(canonicalPath);
    const QString hotpath = readText(hotpathPath);
    const QString physical = readText(physicalPath);
    const bool held = manifest.contains("\"all_presets_generalize\": true") && manifest.contains("\"preset_ladder_pass\": true");
    const bool allocationFree = hotpath.contains("hot_path_allocations=0");
    const bool physicalMatch = physical.contains("identity_match=true");
    const bool patternPass = pattern73 >= 0.0 && pattern73 <= .0001;
    const bool pass = held && hard == 0 && confirmed == 0 && suspect == 0 && patternPass && allocationFree && physicalMatch;
    std::ostringstream report;
    report << "# V2.3.T Adaptive Response Phase 3B Final Baseline Qualification Report\n\n"
        << "## Freeze and sealed execution\n\n"
        << "1. Production freeze SHA: " << kFreezeSha << ".\n"
        << "2. Phase 2B disposition: PHASE 2B FAIL - NO ROBUST IMPROVEMENT OVER CURRENT PRESETS. The current production baseline is retained.\n"
        << "3. All five selected vectors exactly equal production across every preset axis. See production_preset_vectors.csv.\n"
        << "4. Sealed holdout: seed 0x2B10F00D; generator phase2b-persona-structured-v1; 200 scenarios; SHA-256 "
        << kExpectedHoldoutHash << ".\n"
        << "5. pre_replay_lock.json and first_scored_replay_receipt.json prove the lock preceded the first scored replay.\n"
        << "6. Locked policy SHA-256: " << hashFile(output.filePath("locked_acceptance_policy.md")).toStdString() << ".\n"
        << "7. Preset vector SHA-256: " << hashFile(output.filePath("production_preset_vectors.csv")).toStdString() << ".\n"
        << "8. Total primary executions: 1000.\n\n"
        << "## Per-preset results and generalization envelope\n\n"
        << "Items 9 through 14, including hard failures and Light, Balanced, Fast, Aggressive, and Extreme results:\n\n"
        << "~~~csv\n" << readText(output.filePath("holdout_aggregate_results.csv")).toStdString() << "~~~\n"
        << "The predeclared Search A/B plus independent-corpus envelope comparison is:\n\n~~~csv\n"
        << readText(output.filePath("generalization_envelope.csv")).toStdString() << "~~~\n\n"
        << "## Families, scrutiny groups, and outliers\n\n"
        << "Items 15 through 20 include separate Precision Pilot, Combat Helicopter, Fixed-Wing, Space-Sim, Noisy Sensor, and Structured motion rows. "
        << "Structured motion includes ballistic, reversal, stop, brake/continue, brake/reverse, off-center, acceleration, jerk, and cadence variants.\n\n~~~csv\n"
        << readText(output.filePath("holdout_family_results.csv")).toStdString() << "~~~\n"
        << "All required worst cases are retained, without discarding outliers:\n\n~~~csv\n"
        << readText(output.filePath("holdout_outliers.csv")).toStdString() << "~~~\n"
        << "Preset ladder and distinctness evidence:\n\n~~~csv\n" << readText(output.filePath("preset_ladder.csv")).toStdString() << "~~~\n\n"
        << "## Sentinel results\n\n"
        << "21. The ladder requires Light < Balanced < Fast < Aggressive < Extreme in temporal advance, mean active horizon, and useful lead; see the preceding table.\n"
        << "22. Distinctness follows the same ordered metrics and is independently required by the locked policy.\n"
        << "23. Pattern-73 canonical wrong-direction lead area: " << std::fixed << std::setprecision(8) << pattern73
        << " (limit 0.00010000).\n"
        << "24. Canonical classifications: hard failures=" << hard << ", PRODUCT_CONFIRMED=" << confirmed
        << ", PRODUCT_SUSPECT=" << suspect << ". Existing UNKNOWN and HARNESS evidence was retained.\n"
        << "25. Physical trace sentinel was identity-only: " << physical.toStdString()
        << "26. Clean Release HOTPATH benchmark:\n\n~~~text\n" << hotpath.toStdString() << "~~~\n"
        << "27. Allocation result: " << (allocationFree ? "zero hot-path allocations" : "allocation sentinel failed") << ".\n\n"
        << "## Final production vectors\n\n~~~csv\n" << readText(output.filePath("production_preset_vectors.csv")).toStdString() << "~~~\n\n"
        << "28. The listed vectors are empirically retained; no production change is required.\n"
        << "29. Production change required: no.\n"
        << "30. Final disposition:\n\n"
        << (pass ? "PHASE 3B PASS " "\xE2\x80\x94" " CURRENT PRODUCTION PRESETS VALIDATED " "\xE2\x80\x94" " SHIPPING PRESET FREEZE COMPLETE"
                 : "PHASE 3B FAIL " "\xE2\x80\x94" " ONE OR MORE CURRENT PRESETS REQUIRE NEW INVESTIGATION")
        << "\n\nNo production preset, estimator, mapper hot path, release, deployment, or installation was changed by this qualification.\n";
    if (!writeText(reportPath,report.str())) {
        if (error) *error = "could not write final report";
        return false;
    }
    return pass;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Offline one-shot V2.3.T Phase 3B baseline qualification. No UI, mapper, DirectInput, vJoy, or physical device path.");
    parser.addHelpOption();
    const QCommandLineOption mode{"mode","identity|prepare|execute|canonical|physical|finalize","mode"};
    const QCommandLineOption output{"output","Qualification evidence directory","directory"};
    const QCommandLineOption characterization{"characterization","Phase 2B baseline characterization CSV","path"};
    const QCommandLineOption canonical{"canonical-output","Canonical output directory","directory"};
    const QCommandLineOption fixtures{"physical-fixtures","Recorded physical fixture directory","directory"};
    const QCommandLineOption hotpath{"hotpath-report","Clean Release hotpath output text","path"};
    const QCommandLineOption report{"report","Required final report path","path"};
    parser.addOption(mode); parser.addOption(output); parser.addOption(characterization); parser.addOption(canonical);
    parser.addOption(fixtures); parser.addOption(hotpath); parser.addOption(report); parser.process(app);
    const QString selected = parser.value(mode); QString error;
    if (selected == "identity") {
        const bool okay = verifyHoldout(&error);
        if (okay) QTextStream(stdout) << "HOLDOUT_IDENTITY_VERIFIED seed=0x2B10F00D scenarios=200 sha256="
            << kExpectedHoldoutHash << " estimator_replays=0\n";
        else QTextStream(stderr) << "PHASE 3B BLOCKED - HOLDOUT IDENTITY FAILURE: " << error << '\n';
        return okay ? 0 : 3;
    }
    if (selected == "prepare") {
        const bool okay = parser.isSet(output) && parser.isSet(characterization)
            && prepare(parser.value(output),parser.value(characterization),&error);
        if (!okay) QTextStream(stderr) << "PHASE 3B BLOCKED - PRE-REPLAY LOCK FAILURE: " << error << '\n';
        else QTextStream(stdout) << "PHASE 3B PRE-REPLAY LOCKED\n";
        return okay ? 0 : 4;
    }
    if (selected == "execute") {
        const bool okay = parser.isSet(output) && execute(parser.value(output),&error);
        if (!okay) QTextStream(stderr) << "PHASE 3B SCORED REPLAY FAILED OR DID NOT PASS: " << error << '\n';
        else QTextStream(stdout) << "PHASE 3B SCORED HOLDOUT COMPLETE\n";
        return okay ? 0 : 5;
    }
    if (selected == "canonical") {
        const bool okay = parser.isSet(canonical) && runCanonical(parser.value(canonical),&error);
        if (!okay) QTextStream(stderr) << "PHASE 3B CANONICAL SENTINEL FAILED: " << error << '\n';
        return okay ? 0 : 6;
    }
    if (selected == "physical") {
        const bool okay = parser.isSet(output) && parser.isSet(fixtures)
            && recordPhysicalIdentity(parser.value(output),parser.value(fixtures),&error);
        if (!okay) QTextStream(stderr) << "PHASE 3B PHYSICAL TRACE IDENTITY FAILED: " << error << '\n';
        return okay ? 0 : 7;
    }
    if (selected == "finalize") {
        const bool okay = parser.isSet(output) && parser.isSet(canonical) && parser.isSet(hotpath) && parser.isSet(report)
            && finalize(parser.value(output),parser.value(canonical),parser.value(hotpath),parser.value(report),&error);
        if (!okay) QTextStream(stderr) << "PHASE 3B FINALIZATION FAILED OR DISPOSITION IS FAIL: " << error << '\n';
        return okay ? 0 : 8;
    }
    QTextStream(stderr) << "Specify --mode identity|prepare|execute|canonical|physical|finalize\n";
    return 2;
}
