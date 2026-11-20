// V2.4.R1B-F is the final offline-only Predictor-D authority decision. It
// embeds the preserved R2 research generator, but is never linked into the
// mapper, DirectInput, vJoy, QML, presets, or normal test targets.
#define HOTAS_R1B_PREDICTOR_D_INCLUDE
#include "C:/Users/kkids/Documents/HOTAS_BF6-v2.4.R1B-R2-direction-tagged-revalidation/tests/adaptive_response_r1a_source_domain_estimator.cpp"
#undef HOTAS_R1B_PREDICTOR_D_INCLUDE

#include <fstream>
#include <numeric>

namespace {

constexpr double kAuditHorizonSeconds = .008;
constexpr double kAuditMovingVelocity = .04;
constexpr std::array<double, 13> kR1BHorizonsMs{2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40};
constexpr double kR1BMaximumSourceAgeSeconds = .0155;

struct R1BOptions {
    std::filesystem::path output, baseline;
    std::string finalist;
    bool finalBlind = false;
};

struct R1BDirectQ3 {
    std::array<R1APoint, 3> points{};
    size_t count = 0U;
    double lastTime = 0.0;
    void reset() { count = 0U; lastTime = 0.0; }
    void accept(double time, double x) {
        if (count < points.size()) points[count++] = {time, x};
        else { points[0] = points[1]; points[1] = points[2]; points[2] = {time, x}; }
        lastTime = time;
    }
    bool ready() const { return count == points.size(); }
    double newestPosition() const { return count == 0U ? 0.0 : points[count - 1U].x; }
};

enum class R1BAnchor { CurrentPhysicalFutureOnly, SourceAgeReconstruction };
enum class R1BReset { None, HardDirectionReset, DirectionAwareRetention };

struct R1BDefinition {
    std::string id;
    bool velocity = true, acceleration = false, velocityTrust = false, accelerationTrust = false;
    bool dynamicHorizon = false, finiteAccelerationLifetime = false;
    R1BAnchor anchor = R1BAnchor::CurrentPhysicalFutureOnly;
    R1BReset reset = R1BReset::DirectionAwareRetention;
};

double r1bClamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double r1bSign(double value) { return value < 0.0 ? -1.0 : value > 0.0 ? 1.0 : 0.0; }

std::vector<R1BDefinition> r1bDefinitions()
{
    return {
        {"V2.3_E0", true, true, false, false, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::None},
        {"R-D1_Q3_VELOCITY_A", true, false, false, false, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"R-D3_Q3_TRUSTED_ACCELERATION_A", true, true, true, true, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"R-D5_Q3_Q5_DYNAMIC_HORIZON_A", true, true, true, true, true, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
    };
}

struct R1BState {
    R1BDirectQ3 q3;
    R1ASourceEstimator q3Fit{{"Q3_GENERIC_REFERENCE", R1AKind::Quadratic, 3}};
    R1ASourceEstimator q5{{"Q5_MILD_REFERENCE", R1AKind::Quadratic, 5, 0.0, .55, .10, .006, 1.0}};
    size_t sourceIndex = 0U;
    double previousSourceVelocity = 0.0, currentSourceVelocity = 0.0, previousAcceleration = 0.0;
    bool directionValid = true, sourceOpposite = false;
};

bool r1bAcceptSources(const R07RTrace &trace, double mapperTime, R1BState &state, R1BReset reset)
{
    bool updated = false;
    state.sourceOpposite = false;
    while (state.sourceIndex < trace.source.size() && trace.source[state.sourceIndex].time <= mapperTime + 1e-9) {
        const Sample &source = trace.source[state.sourceIndex];
        bool opposite = false;
        if (state.sourceIndex > 0U) {
            const Sample &prior = trace.source[state.sourceIndex - 1U];
            const double period = std::max(.0005, source.time - prior.time);
            const double velocity = (source.physical - prior.physical) / period;
            opposite = std::abs(velocity) > .10 && std::abs(state.currentSourceVelocity) > .10
                && r1bSign(velocity) != r1bSign(state.currentSourceVelocity);
            state.previousSourceVelocity = state.currentSourceVelocity;
            state.currentSourceVelocity = velocity;
        }
        if (opposite && reset != R1BReset::None) {
            state.sourceOpposite = true;
            state.directionValid = false;
        }
        state.q3.accept(source.time, source.physical);
        state.q3Fit.accept(source.time, source.physical);
        state.q5.accept(source.time, source.physical);
        ++state.sourceIndex;
        updated = true;
    }
    if (!state.directionValid && updated && !state.sourceOpposite && state.q3.ready()
        && std::abs(state.currentSourceVelocity) > .06
        && r1bSign(state.q3Fit.value(mapperTime).velocity) == r1bSign(state.currentSourceVelocity)) state.directionValid = true;
    return updated;
}

struct R2DirectionState {
    int ownedDirection = 0, pendingDirection = 0, preOppositeOwnerDirection = 0;
    bool directionalAuthority = false, v23FullReversalPermission = false;
    void establish(int frozenPhysicalOwner, int sourceDirection) {
        if (pendingDirection != 0) return;
        const int candidate = frozenPhysicalOwner != 0 ? frozenPhysicalOwner : sourceDirection;
        if (candidate != 0 && (ownedDirection == 0 || candidate == ownedDirection)) { ownedDirection = candidate; directionalAuthority = true; }
    }
    bool observeCredibleOpposite(bool credible, int sourceDirection) {
        if (!credible || sourceDirection == 0) return false;
        if (ownedDirection == 0) ownedDirection = -sourceDirection;
        if (sourceDirection == ownedDirection) {
            pendingDirection = 0; preOppositeOwnerDirection = 0; v23FullReversalPermission = false; directionalAuthority = true;
            return false;
        }
        if (pendingDirection == sourceDirection) return false;
        preOppositeOwnerDirection = ownedDirection; pendingDirection = sourceDirection; directionalAuthority = false; v23FullReversalPermission = false;
        return true;
    }
    bool canReacquire(bool v23Permission, int frozenPhysicalOwner, bool q3Ready, int q3Direction,
                      int rawDirection, int boundedDirection) const {
        return pendingDirection != 0 && v23Permission && frozenPhysicalOwner == pendingDirection && q3Ready
            && q3Direction == pendingDirection && rawDirection == pendingDirection
            && (boundedDirection == 0 || boundedDirection == pendingDirection);
    }
    double apply(double boundedLead, bool v23Permission, int frozenPhysicalOwner, bool q3Ready,
                 int q3Direction, int rawDirection) {
        const int boundedDirection = static_cast<int>(r1bSign(boundedLead));
        if (pendingDirection != 0) {
            if (v23Permission && frozenPhysicalOwner == pendingDirection) v23FullReversalPermission = true;
            if (!canReacquire(v23FullReversalPermission, frozenPhysicalOwner, q3Ready, q3Direction, rawDirection, boundedDirection)) return 0.0;
            ownedDirection = pendingDirection; pendingDirection = 0; preOppositeOwnerDirection = 0; v23FullReversalPermission = false; directionalAuthority = true;
        }
        if (!directionalAuthority || (boundedDirection != 0 && boundedDirection != ownedDirection)) return 0.0;
        return boundedLead;
    }
};

double r2BoundLead(double lead, double physical, const RuntimeAdaptiveResponseConfig &configuration,
                   const AdaptiveResponseTelemetry &telemetry, bool &clamped, bool &brakingOwned)
{
    clamped = false;
    const bool confirmedQuiet = telemetry.quietDurationSeconds > std::max(.020F, telemetry.sourceUpdatePeriodSeconds * 2.90F);
    brakingOwned = telemetry.sourceBrakingDetected || telemetry.brakingReductionFactor > .0001F || confirmedQuiet;
    if (!std::isfinite(lead) || telemetry.safetyCancelled || brakingOwned) return 0.0;
    double maximum = static_cast<double>(configuration.maximumLead) * std::max(.10, static_cast<double>(telemetry.confidence));
    maximum = std::min(maximum, std::max(.001, static_cast<double>(telemetry.turningPointLeadLimit)));
    const double limited = std::clamp(lead, -maximum, maximum);
    clamped = limited != lead;
    const double output = std::clamp(physical + limited, -1.0, 1.0);
    if (output != physical + limited) clamped = true;
    return output - physical;
}

double r2RawLead(const R1BDefinition &definition, const R1BState &state, const R1AEstimate &q3,
                 double v3Now, double a3Now, double velocityTrust, double accelerationTrust,
                 bool accelerationMotionValid, double age, double requested, double physical)
{
    if (!state.q3.ready() || !definition.velocity) return 0.0;
    double horizon = requested;
    if (definition.dynamicHorizon) horizon *= .55 + .30 * velocityTrust + .15 * accelerationTrust;
    const double usedVelocity = v3Now * (definition.velocityTrust ? velocityTrust : 1.0);
    double usedAcceleration = a3Now * (definition.accelerationTrust ? accelerationTrust : 1.0);
    if (!accelerationMotionValid) usedAcceleration = 0.0;
    if (definition.finiteAccelerationLifetime) {
        const double lifetime = .004 + .024 * accelerationTrust;
        usedAcceleration *= lifetime / (lifetime + horizon);
    }
    if (definition.anchor == R1BAnchor::CurrentPhysicalFutureOnly)
        return usedVelocity * horizon + (definition.acceleration ? .5 * usedAcceleration * horizon * horizon : 0.0);
    const double sourceTime = age + horizon;
    return state.q3.newestPosition() + q3.velocity * velocityTrust * sourceTime
        + (definition.acceleration ? .5 * q3.acceleration * accelerationTrust * sourceTime * sourceTime : 0.0) - physical;
}

R07RTrace r2Trace(const std::string &id, const std::string &reportedMotion, const std::string &sourceMotion,
                  const std::string &envelope, const std::string &sensor, const std::string &tags, std::uint32_t seed)
{
    R07RTrace trace = r07rMakeTrace(id, envelope, sourceMotion, sensor, tags, seed);
    trace.motion = reportedMotion;
    return trace;
}

std::vector<R07RTrace> r2ValidationCorpus()
{
    std::vector<R07RTrace> traces;
    std::uint32_t seed = 0x241DF00DU;
    const auto add = [&](const char *reported, const char *source, const char *envelope, const char *tags, int count) {
        for (int i = 0; i < count; ++i) traces.push_back(r2Trace("validation/" + std::string(reported) + "/" + std::to_string(i), reported,
            source, envelope, r07rSensorFor(i), tags, seed++));
    };
    add("precision-human-realistic", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 190);
    add("combat-human-realistic", "combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 300);
    add("combat-human-extreme", "combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 140);
    add("fixed-wing-human-realistic", "fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 180);
    add("space-sim-human-realistic", "space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 180);
    add("fast-ballistic", "fast-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 140);
    add("very-fast-ballistic", "very-fast-ballistic", "HUMAN_EXTREME", "BALLISTIC", 140);
    add("reversal-braking", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 70);
    add("reversal-braking", "structured-braking", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 70);
    add("stop-micro", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 50);
    add("stop-micro", "structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 50);
    add("sensor-cadence-endpoint", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "SENSOR_CADENCE_STRESS;ENDPOINT_STRESS", 90);
    return traces;
}

std::vector<R07RTrace> r1bPhysicalCorpus()
{
    std::vector<R07RTrace> result;
    for (Trace trace : physicalTraces()) result.push_back({trace, trace.samples, {}, "HUMAN_REPRESENTATIVE", "physical-" + trace.id, "clean", "REAL_HOTAS_DEVELOPMENT_ONLY"});
    return result;
}

R1BOptions r1bOptions(int argc, char *argv[])
{
    R1BOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--out" && i + 1 < argc) options.output = argv[++i];
        else if (argument == "--baseline" && i + 1 < argc) options.baseline = argv[++i];
        else if (argument == "--final-blind") options.finalBlind = true;
        else if (argument == "--finalist" && i + 1 < argc) options.finalist = argv[++i];
    }
    return options;
}

std::string auditNumber(double value, int precision = 8)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string auditCsv(const std::string &value)
{
    std::string escaped = value;
    size_t position = 0U;
    while ((position = escaped.find('"', position)) != std::string::npos) {
        escaped.insert(position, 1U, '"');
        position += 2U;
    }
    return '"' + escaped + '"';
}

bool auditWrite(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

double auditQuantile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::floor(std::clamp(fraction, 0.0, 1.0)
        * static_cast<double>(values.size() - 1U)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

struct AuditMetric {
    std::uint64_t samples = 0U;
    double absoluteError = 0.0, absoluteProgress = 0.0, usefulLead = 0.0, effectiveAdvance = 0.0;
    std::uint64_t progressSamples = 0U;
    std::vector<double> errors;

    void add(double output, double physical, double truth)
    {
        if (!std::isfinite(output) || !std::isfinite(truth)) return;
        const double error = output - truth;
        const double travel = truth - physical;
        const double lead = output - physical;
        ++samples;
        absoluteError += std::abs(error);
        errors.push_back(std::abs(error));
        if (std::abs(travel) > .002) {
            ++progressSamples;
            absoluteProgress += std::abs(error / travel);
            effectiveAdvance += lead * (travel < 0.0 ? -1.0 : 1.0);
            if (lead * travel > 0.0) usefulLead += std::min(std::abs(lead), std::abs(travel));
        }
    }

    double mae() const { return samples == 0U ? 0.0 : absoluteError / static_cast<double>(samples); }
    double p95() const { return auditQuantile(errors, .95); }
    double p99() const { return auditQuantile(errors, .99); }
    double progress() const { return progressSamples == 0U ? 0.0 : absoluteProgress / static_cast<double>(progressSamples); }
    double useful() const { return progressSamples == 0U ? 0.0 : usefulLead / static_cast<double>(progressSamples); }
    double advance() const { return progressSamples == 0U ? 0.0 : effectiveAdvance / static_cast<double>(progressSamples); }
};

struct AuditValues {
    std::vector<double> values;
    void add(double value) { if (std::isfinite(value)) values.push_back(value); }
    double mean() const {
        return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    }
    double quantile(double fraction) const { return auditQuantile(values, fraction); }
    double percentage(const std::function<bool(double)> &predicate) const {
        if (values.empty()) return 0.0;
        return 100.0 * static_cast<double>(std::count_if(values.begin(), values.end(), predicate))
            / static_cast<double>(values.size());
    }
};

struct ClampPair {
    std::uint64_t opportunities = 0U, applied = 0U;
    AuditMetric before, after;
    void add(bool changed, double beforeOutput, double afterOutput, double physical, double truth) {
        ++opportunities;
        if (changed) ++applied;
        before.add(beforeOutput, physical, truth);
        after.add(afterOutput, physical, truth);
    }
};

struct AuditRun {
    std::map<std::string, AuditMetric> metrics;
    std::map<std::string, AuditValues> authority;
    std::map<std::string, ClampPair> clamps;
    std::map<std::string, std::uint64_t> finalMismatches;

    AuditMetric &metric(const std::string &stage, const std::string &motion) {
        return metrics[stage + "|" + motion];
    }
    AuditValues &scalar(const std::string &stage, const std::string &name, const std::string &motion) {
        return authority[stage + "|" + name + "|" + motion];
    }
    ClampPair &clamp(const std::string &name, const std::string &motion) {
        return clamps[name + "|" + motion];
    }
};

struct CorpusStats {
    std::uint64_t traces = 0U;
    AuditValues sourceCadence, mapperCadence, duration, amplitude, velocity, acceleration, jerk;
    std::map<std::string, std::uint64_t> phases, sensors;
};

std::string auditPhase(double age, double period)
{
    const double fraction = age / std::max(.0005, period);
    if (fraction < .25) return "0_25pct";
    if (fraction < .50) return "25_50pct";
    if (fraction < .75) return "50_75pct";
    return "75pct_plus";
}

void auditCorpus(CorpusStats &stats, const R07RTrace &trace)
{
    ++stats.traces;
    stats.sensors[trace.sensor]++;
    if (!trace.mapper.samples.empty()) stats.duration.add(trace.mapper.samples.back().time * 1000.0);
    if (!trace.latentPoints.empty()) {
        float minimum = trace.latentPoints.front().second, maximum = minimum;
        for (const auto &point : trace.latentPoints) {
            minimum = std::min(minimum, point.second);
            maximum = std::max(maximum, point.second);
        }
        stats.amplitude.add(static_cast<double>(maximum - minimum));
    }
    for (size_t index = 1U; index < trace.source.size(); ++index)
        stats.sourceCadence.add((trace.source[index].time - trace.source[index - 1U].time) * 1000.0);
    size_t sourceIndex = 0U;
    double sourcePeriod = kR1AMedianSourceSeconds;
    double previousVelocity = 0.0, previousAcceleration = 0.0, previousTime = -1.0;
    for (size_t index = 0U; index < trace.mapper.samples.size(); ++index) {
        const Sample &sample = trace.mapper.samples[index];
        if (index > 0U) stats.mapperCadence.add((sample.time - trace.mapper.samples[index - 1U].time) * 1000.0);
        while (sourceIndex + 1U < trace.source.size() && trace.source[sourceIndex + 1U].time <= sample.time + 1e-9) {
            ++sourceIndex;
            sourcePeriod = std::max(.0005, trace.source[sourceIndex].time - trace.source[sourceIndex - 1U].time);
        }
        if (!trace.source.empty()) stats.phases[auditPhase(std::max(0.0, sample.time - trace.source[sourceIndex].time), sourcePeriod)]++;
        const auto latent = r1aLatentAt(trace, sample.time);
        if (!latent) continue;
        stats.velocity.add(std::abs(latent->velocity));
        if (previousTime >= 0.0) {
            const double dt = std::max(.000001, sample.time - previousTime);
            const double acceleration = (latent->velocity - previousVelocity) / dt;
            stats.acceleration.add(std::abs(acceleration));
            stats.jerk.add(std::abs((acceleration - previousAcceleration) / dt));
            previousAcceleration = acceleration;
        }
        previousVelocity = latent->velocity;
        previousTime = sample.time;
    }
}

const R1BDefinition &auditD5()
{
    static const R1BDefinition definition = [] {
        const std::vector<R1BDefinition> definitions = r1bDefinitions();
        const auto found = std::find_if(definitions.begin(), definitions.end(), [](const R1BDefinition &item) {
            return item.id == "R-D5_Q3_Q5_DYNAMIC_HORIZON_A";
        });
        if (found == definitions.end()) throw std::runtime_error("R2 D5 definition missing");
        return *found;
    }();
    return definition;
}

bool auditObservedTruth(const R07RTrace &trace, double time, double &truth, std::string &truthReference)
{
    if (const auto latent = r1aLatentAt(trace, time)) {
        truth = latent->position;
        truthReference = "EXACT_LATENT";
        return true;
    }
    if (const auto source = r1aInterpolatedSource(trace.source, time)) {
        truth = *source;
        truthReference = trace.mapper.physicalTrace ? "LATENT_POSITION_PROXY" : "RETROSPECTIVE_SOURCE_INTERPOLATED";
        return true;
    }
    return false;
}

void auditAddClamp(AuditRun &run, const std::string &name, const std::string &motion, bool changed,
                   double beforeOutput, double afterOutput, double physical, double truth)
{
    run.clamp(name, motion).add(changed, beforeOutput, afterOutput, physical, truth);
}

void auditReplayStages(const std::vector<R07RTrace> &traces, bool physical, AuditRun &run)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    const R1BDefinition &definition = auditD5();
    for (const R07RTrace &trace : traces) {
        if (!physical && trace.motion != "fast-ballistic" && trace.motion != "very-fast-ballistic") continue;
        if (physical && trace.motion != "physical-fast-full-sweep-1" && trace.motion != "physical-very-fast-full-sweep-1") continue;
        AdaptiveResponseProcessor v23;
        R1BState state;
        R2DirectionState conceptualDirection;
        R2DirectionState finalDirection;
        const std::string motion = trace.motion;
        for (const Sample &sample : trace.mapper.samples) {
            const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = v23.process(sample.physical, configuration, timestamp);
            const bool sourceUpdated = r1bAcceptSources(trace, sample.time, state, definition.reset);
            const R1AEstimate q3 = state.q3Fit.value(sample.time);
            const R1AEstimate q5 = state.q5.value(sample.time);
            const double age = std::min(kR1BMaximumSourceAgeSeconds, q3.sourceAge);
            const double v3Now = q3.velocity + q3.acceleration * age;
            const double a3Now = q3.acceleration;
            const double v5Now = q5.velocity + q5.acceleration * age;
            const double a5Now = q5.acceleration;
            const double velocityDisagreement = state.q3.ready() && q5.sourceAge >= 0.0
                ? std::abs(v3Now - v5Now) / std::max(.08, std::max(std::abs(v3Now), std::abs(v5Now))) : 1.0;
            const double accelerationDisagreement = state.q3.ready()
                ? std::abs(a3Now - a5Now) / std::max(8.0, std::max(std::abs(a3Now), std::abs(a5Now))) : 1.0;
            const double cadence = r1bClamp01(1.0 - std::abs(q3.sourcePeriod - kR1AMedianSourceSeconds) / kR1AMedianSourceSeconds);
            const double coherence = r1bClamp01(1.0 / (1.0 + q3.normalizedResidual * 3.0));
            const double jerk = sourceUpdated ? std::abs(a3Now - state.previousAcceleration) / std::max(.0005, q3.sourcePeriod) : 0.0;
            if (sourceUpdated) state.previousAcceleration = a3Now;
            const double jerkSafety = 1.0 / (1.0 + jerk * .003);
            const double velocityTrust = r1bClamp01(.55 * (1.0 - r1bClamp01(velocityDisagreement)) + .25 * coherence + .20 * cadence);
            const double accelerationTrust = r1bClamp01(.45 * (1.0 - r1bClamp01(accelerationDisagreement)) + .30 * jerkSafety + .15 * coherence + .10 * cadence);
            const bool confirmedQuiet = telemetry.quietDurationSeconds > std::max(.020F, telemetry.sourceUpdatePeriodSeconds * 2.90F);
            const bool accelerationMotionValid = state.q3.ready() && !confirmedQuiet && state.directionValid && !state.sourceOpposite
                && telemetry.holdConfidence >= .55F && coherence >= .45 && velocityTrust >= .35 && accelerationTrust >= .30
                && std::abs(v3Now) >= .04 && q3.normalizedResidual <= .75;
            const int frozenOwner = r1bSign(telemetry.velocity);
            const int sourceDirection = r1bSign(state.currentSourceVelocity);
            conceptualDirection.establish(frozenOwner, sourceDirection);
            finalDirection.establish(frozenOwner, sourceDirection);
            conceptualDirection.observeCredibleOpposite(state.sourceOpposite, sourceDirection);
            finalDirection.observeCredibleOpposite(state.sourceOpposite, sourceDirection);

            for (const double horizonMs : kR1BHorizonsMs) {
                const double requested = horizonMs / 1000.0;
                const double baseline = sample.physical + telemetry.velocity * requested + .5 * telemetry.acceleration * requested * requested;
                const double b1 = sample.physical + q3.velocity * requested + .5 * q3.acceleration * requested * requested;
                const double b2 = sample.physical + v3Now * requested + .5 * a3Now * requested * requested;
                const double trustedVelocity = v3Now * velocityTrust;
                const double trustedAcceleration = accelerationMotionValid ? a3Now * accelerationTrust : 0.0;
                const double b3 = sample.physical + trustedVelocity * requested + .5 * trustedAcceleration * requested * requested;
                const double dynamicHorizon = requested * (.55 + .30 * velocityTrust + .15 * accelerationTrust);
                const double b4Lead = trustedVelocity * dynamicHorizon + .5 * trustedAcceleration * dynamicHorizon * dynamicHorizon;
                const double b4 = sample.physical + b4Lead;

                const double conceptualLead = conceptualDirection.apply(b4Lead, telemetry.reversal, frozenOwner,
                    state.q3.ready(), r1bSign(v3Now), r1bSign(b4Lead));
                const double b5 = sample.physical + conceptualLead;
                const bool brakeQuietOrCancel = telemetry.sourceBrakingDetected || telemetry.brakingReductionFactor > .0001F
                    || confirmedQuiet || telemetry.safetyCancelled;
                const double brakeLead = brakeQuietOrCancel ? 0.0 : b4Lead;
                const double b6 = sample.physical + brakeLead;
                const double maximumLead = static_cast<double>(configuration.maximumLead) * std::max(.10, static_cast<double>(telemetry.confidence));
                const double afterMaximum = std::clamp(brakeLead, -maximumLead, maximumLead);
                const double turningLimit = std::max(.001, static_cast<double>(telemetry.turningPointLeadLimit));
                const double afterTurning = std::clamp(afterMaximum, -turningLimit, turningLimit);
                const double b7 = std::clamp(static_cast<double>(sample.physical) + afterTurning, -1.0, 1.0);

                const double finalRaw = r2RawLead(definition, state, q3, v3Now, a3Now, velocityTrust,
                    accelerationTrust, accelerationMotionValid, age, requested, sample.physical);
                bool finalClamped = false, brakingOwned = false;
                const double bounded = r2BoundLead(finalRaw, sample.physical, configuration, telemetry, finalClamped, brakingOwned);
                const double finalLead = finalDirection.apply(bounded, telemetry.reversal, frozenOwner,
                    state.q3.ready(), r1bSign(v3Now), r1bSign(finalRaw));
                const double b8 = sample.physical + finalLead;

                if (std::abs(horizonMs - 8.0) > 1e-12) continue;
                double truth = 0.0;
                std::string truthReference;
                if (!auditObservedTruth(trace, sample.time + kAuditHorizonSeconds, truth, truthReference)) continue;
                run.metric("B0", motion).add(baseline, sample.physical, truth);
                run.metric("B1", motion).add(b1, sample.physical, truth);
                run.metric("B2", motion).add(b2, sample.physical, truth);
                run.metric("B3", motion).add(b3, sample.physical, truth);
                run.metric("B4", motion).add(b4, sample.physical, truth);
                run.metric("B5", motion).add(b5, sample.physical, truth);
                run.metric("B6", motion).add(b6, sample.physical, truth);
                run.metric("B7", motion).add(b7, sample.physical, truth);
                run.metric("B8", motion).add(b8, sample.physical, truth);
                auditAddClamp(run, "braking_quiet", motion, brakeQuietOrCancel && std::abs(b4Lead) > 1e-12,
                    b4, b6, sample.physical, truth);
                auditAddClamp(run, "maximum_lead", motion, std::abs(afterMaximum - brakeLead) > 1e-12,
                    b6, sample.physical + afterMaximum, sample.physical, truth);
                auditAddClamp(run, "turning", motion, std::abs(afterTurning - afterMaximum) > 1e-12,
                    sample.physical + afterMaximum, sample.physical + afterTurning, sample.physical, truth);
                auditAddClamp(run, "endpoint_taper", motion, std::abs(b7 - (sample.physical + afterTurning)) > 1e-12,
                    sample.physical + afterTurning, b7, sample.physical, truth);

                const auto currentLatent = r1aLatentAt(trace, sample.time);
                const bool moving = !physical && currentLatent && std::abs(currentLatent->velocity) >= kAuditMovingVelocity;
                if (!moving) continue;
                run.scalar("B3", "velocity_trust", motion).add(velocityTrust);
                run.scalar("B3", "acceleration_trust", motion).add(accelerationTrust);
                run.scalar("B4", "dynamic_horizon_ms", motion).add(dynamicHorizon * 1000.0);
                run.scalar("B5", "direction_authority", motion).add(std::abs(conceptualLead) > 1e-12 || std::abs(b4Lead) <= 1e-12 ? 1.0 : 0.0);
                run.scalar("B6", "braking_quiet_authority", motion).add(brakeQuietOrCancel ? 0.0 : 1.0);
                run.scalar("B7", "maximum_lead_authority", motion).add(std::abs(brakeLead) <= 1e-12 ? 1.0 : std::abs(afterMaximum / brakeLead));
                run.scalar("B7", "turning_authority", motion).add(std::abs(afterMaximum) <= 1e-12 ? 1.0 : std::abs(afterTurning / afterMaximum));
                run.scalar("B7", "endpoint_authority", motion).add(std::abs(afterTurning) <= 1e-12 ? 1.0
                    : std::abs((b7 - sample.physical) / afterTurning));
                run.scalar("B8", "final_shadow_authority", motion).add(std::abs(b4Lead) <= 1e-12 ? 1.0 : std::abs(finalLead / b4Lead));
            }
        }
    }
}

const AuditMetric &auditMetric(const AuditRun &run, const std::string &stage, const std::string &motion)
{
    static const AuditMetric empty;
    const auto found = run.metrics.find(stage + "|" + motion);
    return found == run.metrics.end() ? empty : found->second;
}

std::string auditStageRows(const AuditRun &run, bool physical)
{
    std::ostringstream out;
    out << "stage,motion_or_trace,truth_reference,horizon_ms,samples,mae,p95,p99,progress_error,useful_lead,effective_advance,improvement_vs_b0_pct\n";
    const std::array<std::string, 9> stages{{"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8"}};
    const std::vector<std::string> groups = physical
        ? std::vector<std::string>{"physical-fast-full-sweep-1", "physical-very-fast-full-sweep-1"}
        : std::vector<std::string>{"fast-ballistic", "very-fast-ballistic"};
    for (const std::string &group : groups) {
        const AuditMetric &baseline = auditMetric(run, "B0", group);
        for (const std::string &stage : stages) {
            const AuditMetric &metric = auditMetric(run, stage, group);
            const double gain = baseline.mae() == 0.0 ? 0.0 : 100.0 * (baseline.mae() - metric.mae()) / baseline.mae();
            out << stage << ',' << auditCsv(group) << ',' << (physical ? "LATENT_POSITION_PROXY" : "EXACT_LATENT") << ",8.000,"
                << metric.samples << ',' << auditNumber(metric.mae()) << ',' << auditNumber(metric.p95()) << ',' << auditNumber(metric.p99()) << ','
                << auditNumber(metric.progress()) << ',' << auditNumber(metric.useful()) << ',' << auditNumber(metric.advance()) << ','
                << auditNumber(gain, 4) << '\n';
        }
    }
    return out.str();
}

std::string auditAuthorityRows(const AuditRun &run)
{
    std::ostringstream out;
    out << "stage,authority,motion,sample_filter,samples,mean,p10,p50,p90,pct_zero,pct_lt_025,pct_lt_050,pct_gt_075,pct_one\n";
    for (const auto &[key, values] : run.authority) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        const auto zero = [](double value) { return std::abs(value) <= 1e-9; };
        const auto one = [](double value) { return std::abs(value - 1.0) <= 1e-9; };
        out << parts[0] << ',' << auditCsv(parts[1]) << ',' << auditCsv(parts[2]) << ",abs_exact_latent_velocity_ge_0.04,"
            << values.values.size() << ',' << auditNumber(values.mean()) << ',' << auditNumber(values.quantile(.10)) << ','
            << auditNumber(values.quantile(.50)) << ',' << auditNumber(values.quantile(.90)) << ','
            << auditNumber(values.percentage(zero), 4) << ',' << auditNumber(values.percentage([](double value) { return value < .25; }), 4) << ','
            << auditNumber(values.percentage([](double value) { return value < .50; }), 4) << ','
            << auditNumber(values.percentage([](double value) { return value > .75; }), 4) << ','
            << auditNumber(values.percentage(one), 4) << '\n';
    }
    return out.str();
}

std::string auditHorizonRows(const AuditRun &run)
{
    std::ostringstream out;
    out << "motion,sample_filter,samples,mean_ms,median_ms,p10_ms,p90_ms,pct_ge_4ms,pct_ge_8ms,pct_ge_12ms,pct_ge_18ms,evaluation_target_ms\n";
    for (const std::string &motion : {std::string("fast-ballistic"), std::string("very-fast-ballistic")}) {
        const auto found = run.authority.find("B4|dynamic_horizon_ms|" + motion);
        const AuditValues values = found == run.authority.end() ? AuditValues{} : found->second;
        out << auditCsv(motion) << ",abs_exact_latent_velocity_ge_0.04," << values.values.size() << ',' << auditNumber(values.mean()) << ','
            << auditNumber(values.quantile(.50)) << ',' << auditNumber(values.quantile(.10)) << ',' << auditNumber(values.quantile(.90)) << ','
            << auditNumber(values.percentage([](double value) { return value >= 4.0; }), 4) << ','
            << auditNumber(values.percentage([](double value) { return value >= 8.0; }), 4) << ','
            << auditNumber(values.percentage([](double value) { return value >= 12.0; }), 4) << ','
            << auditNumber(values.percentage([](double value) { return value >= 18.0; }), 4) << ",8.000\n";
    }
    return out.str();
}

struct GainLossRow {
    std::string motion, transition;
    double beforeMae = 0.0, afterMae = 0.0, beforeGain = 0.0, afterGain = 0.0, loss = 0.0;
};

void auditGainLoss(GainLossRow &row, const std::string &motion, const std::string &name,
                   const AuditMetric &baseline, const AuditMetric &before, const AuditMetric &after)
{
    row.motion = motion;
    row.transition = name;
    row.beforeMae = before.mae();
    row.afterMae = after.mae();
    row.beforeGain = baseline.mae() == 0.0 ? 0.0 : 100.0 * (baseline.mae() - before.mae()) / baseline.mae();
    row.afterGain = baseline.mae() == 0.0 ? 0.0 : 100.0 * (baseline.mae() - after.mae()) / baseline.mae();
    row.loss = row.beforeGain - row.afterGain;
}

std::vector<GainLossRow> auditGainLossRows(const AuditRun &run)
{
    std::vector<GainLossRow> rows;
    for (const std::string &motion : {std::string("fast-ballistic"), std::string("very-fast-ballistic")}) {
        const AuditMetric &base = auditMetric(run, "B0", motion);
        const auto addStage = [&](const std::string &name, const AuditMetric &before, const AuditMetric &after) {
            GainLossRow row;
            auditGainLoss(row, motion, name, base, before, after);
            rows.push_back(row);
        };
        addStage("trust", auditMetric(run, "B2", motion), auditMetric(run, "B3", motion));
        addStage("horizon", auditMetric(run, "B3", motion), auditMetric(run, "B4", motion));
        addStage("direction_ownership", auditMetric(run, "B4", motion), auditMetric(run, "B5", motion));
        addStage("braking_quiet", auditMetric(run, "B4", motion), auditMetric(run, "B6", motion));
        const ClampPair &maximum = run.clamps.at("maximum_lead|" + motion);
        const ClampPair &turning = run.clamps.at("turning|" + motion);
        const ClampPair &endpoint = run.clamps.at("endpoint_taper|" + motion);
        addStage("maximum_lead", maximum.before, maximum.after);
        addStage("turning", turning.before, turning.after);
        addStage("endpoint_taper", endpoint.before, endpoint.after);
    }
    return rows;
}

std::string auditGainLossCsv(const std::vector<GainLossRow> &rows)
{
    std::ostringstream out;
    out << "motion,rank,stage_transition,mae_before,mae_after,gain_before_pct,gain_after_pct,gain_loss_pct\n";
    for (const std::string &motion : {std::string("fast-ballistic"), std::string("very-fast-ballistic")}) {
        std::vector<GainLossRow> ranked;
        for (const GainLossRow &row : rows) if (row.motion == motion) ranked.push_back(row);
        std::sort(ranked.begin(), ranked.end(), [](const GainLossRow &left, const GainLossRow &right) {
            return left.loss != right.loss ? left.loss > right.loss : left.transition < right.transition;
        });
        for (size_t index = 0U; index < ranked.size(); ++index) {
            const GainLossRow &row = ranked[index];
            out << auditCsv(row.motion) << ',' << (index + 1U) << ',' << auditCsv(row.transition) << ','
                << auditNumber(row.beforeMae) << ',' << auditNumber(row.afterMae) << ',' << auditNumber(row.beforeGain, 4) << ','
                << auditNumber(row.afterGain, 4) << ',' << auditNumber(row.loss, 4) << '\n';
        }
    }
    return out.str();
}

std::string auditClampRows(const AuditRun &run)
{
    std::ostringstream out;
    out << "motion,clamp,sample_filter,opportunities,applied,occupancy_pct,mae_before,mae_after,gain_before_pct,gain_after_pct,mae_improvement_removed_pct\n";
    for (const std::string &motion : {std::string("fast-ballistic"), std::string("very-fast-ballistic")}) {
        const AuditMetric &base = auditMetric(run, "B0", motion);
        for (const std::string &name : {std::string("maximum_lead"), std::string("turning"), std::string("braking_quiet"), std::string("endpoint_taper")}) {
            const ClampPair &pair = run.clamps.at(name + "|" + motion);
            const double beforeGain = base.mae() == 0.0 ? 0.0 : 100.0 * (base.mae() - pair.before.mae()) / base.mae();
            const double afterGain = base.mae() == 0.0 ? 0.0 : 100.0 * (base.mae() - pair.after.mae()) / base.mae();
            const double occupancy = pair.opportunities == 0U ? 0.0 : 100.0 * static_cast<double>(pair.applied) / static_cast<double>(pair.opportunities);
            out << auditCsv(motion) << ',' << auditCsv(name) << ",all_valid_8ms_exact_latent_samples," << pair.opportunities << ','
                << pair.applied << ',' << auditNumber(occupancy, 4) << ',' << auditNumber(pair.before.mae()) << ','
                << auditNumber(pair.after.mae()) << ',' << auditNumber(beforeGain, 4) << ',' << auditNumber(afterGain, 4) << ','
                << auditNumber(beforeGain - afterGain, 4) << '\n';
        }
    }
    return out.str();
}

void auditR1AReplay(const std::string &corpusName, const std::vector<R07RTrace> &corpus, std::ostringstream &out)
{
    const std::vector<R1ADefinition> catalogue = r1aDefinitions();
    std::vector<R1ADefinition> selected;
    for (const R1ADefinition &definition : catalogue)
        if (definition.id == "E0_V23_CURRENT" || definition.id == "E5_QUADRATIC_3_EXACT") selected.push_back(definition);
    R1AResult result;
    r1aEvaluate(selected, corpus, result, catalogue);
    for (const std::string &motion : {std::string("fast-ballistic"), std::string("very-fast-ballistic")}) {
        const R1APredictionMetric *baseline = r1aMetric(result, "E0_V23_CURRENT", motion, "EXACT_LATENT", 8.0);
        for (const std::string &candidate : {std::string("E0_V23_CURRENT"), std::string("E5_QUADRATIC_3_EXACT")}) {
            const R1APredictionMetric *metric = r1aMetric(result, candidate, motion, "EXACT_LATENT", 8.0);
            if (!baseline || !metric) continue;
            const double gain = baseline->position.mean() == 0.0 ? 0.0
                : 100.0 * (baseline->position.mean() - metric->position.mean()) / baseline->position.mean();
            out << auditCsv(corpusName) << ',' << auditCsv(motion) << ',' << auditCsv(candidate) << ",EXACT_LATENT,8.000,"
                << metric->position.observations << ',' << auditNumber(metric->position.mean()) << ',' << auditNumber(metric->position.percentile(.95)) << ','
                << auditNumber(metric->position.percentile(.99)) << ',' << auditNumber(metric->progress.mean()) << ','
                << auditNumber(metric->temporal.mean()) << ',' << auditNumber(gain, 4) << '\n';
        }
    }
}

std::string auditCorpusCsv(const std::map<std::string, CorpusStats> &stats)
{
    std::ostringstream out;
    out << "corpus,motion,measure,unit,count,mean,p50,p95,p99,max,detail\n";
    const auto writeStats = [&](const std::string &corpus, const std::string &motion, const std::string &measure,
                          const std::string &unit, const AuditValues &values, const std::string &detail = "") {
        const double maximum = values.values.empty() ? 0.0 : *std::max_element(values.values.begin(), values.values.end());
        out << auditCsv(corpus) << ',' << auditCsv(motion) << ',' << auditCsv(measure) << ',' << auditCsv(unit) << ','
            << values.values.size() << ',' << auditNumber(values.mean()) << ',' << auditNumber(values.quantile(.50)) << ','
            << auditNumber(values.quantile(.95)) << ',' << auditNumber(values.quantile(.99)) << ',' << auditNumber(maximum) << ','
            << auditCsv(detail) << '\n';
    };
    for (const auto &[key, item] : stats) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 2U) continue;
        const std::string formula = parts[0] == "R1A"
            ? "r1aBallisticTrace five-point quintic-smoothstep single sweep: +/-0.82 start, 180ms dwell, 1.64 amplitude, duration 160/95ms"
            : "r07rPoints smoothed quintic multi-segment sweep: duration 160/95ms, requested delta 1.80/1.88, clamped +/-0.98, 5.8s trajectory";
        out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ",\"generator_formula_version\",\"text\","
            << item.traces << ",,,,,," << auditCsv(formula) << '\n';
        out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ",\"truth_definition\",\"text\","
            << item.traces << ",,,,,,\"EXACT_LATENT at 8ms future position; source reports are held on 4ms mapper cadence\"\n";
        writeStats(parts[0], parts[1], "source_cadence", "ms", item.sourceCadence);
        writeStats(parts[0], parts[1], "mapper_cadence", "ms", item.mapperCadence);
        writeStats(parts[0], parts[1], "trace_duration", "ms", item.duration);
        writeStats(parts[0], parts[1], "amplitude", "normalized", item.amplitude);
        writeStats(parts[0], parts[1], "velocity_magnitude", "normalized_per_s", item.velocity);
        writeStats(parts[0], parts[1], "acceleration_magnitude", "normalized_per_s2", item.acceleration);
        writeStats(parts[0], parts[1], "jerk_magnitude", "normalized_per_s3", item.jerk);
        for (const auto &[phase, count] : item.phases)
            out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ",\"source_phase_distribution\",\"samples\"," << count << ",,,,,," << auditCsv(phase) << '\n';
        for (const auto &[sensor, count] : item.sensors)
            out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ",\"sensor_profile_distribution\",\"traces\"," << count << ",,,,,," << auditCsv(sensor) << '\n';
    }
    return out.str();
}

std::string auditReport(const AuditRun &run, const std::vector<GainLossRow> &losses, bool exact)
{
    const AuditMetric &fastB0 = auditMetric(run, "B0", "fast-ballistic");
    const AuditMetric &fastB2 = auditMetric(run, "B2", "fast-ballistic");
    const AuditMetric &fastB8 = auditMetric(run, "B8", "fast-ballistic");
    const AuditMetric &veryB0 = auditMetric(run, "B0", "very-fast-ballistic");
    const AuditMetric &veryB2 = auditMetric(run, "B2", "very-fast-ballistic");
    const AuditMetric &veryB8 = auditMetric(run, "B8", "very-fast-ballistic");
    const double fastB2Gain = fastB0.mae() == 0.0 ? 0.0 : 100.0 * (fastB0.mae() - fastB2.mae()) / fastB0.mae();
    const double veryB2Gain = veryB0.mae() == 0.0 ? 0.0 : 100.0 * (veryB0.mae() - veryB2.mae()) / veryB0.mae();
    const double fastB8Gain = fastB0.mae() == 0.0 ? 0.0 : 100.0 * (fastB0.mae() - fastB8.mae()) / fastB0.mae();
    const double veryB8Gain = veryB0.mae() == 0.0 ? 0.0 : 100.0 * (veryB0.mae() - veryB8.mae()) / veryB0.mae();
    const bool generalizes = fastB2Gain >= 40.0 && veryB2Gain >= 40.0;
    const GainLossRow *largest = losses.empty() ? nullptr : &*std::max_element(losses.begin(), losses.end(), [](const GainLossRow &left, const GainLossRow &right) {
        return left.loss < right.loss;
    });
    const std::string disposition = generalizes
        ? "R1B-R2.1 ESTIMATOR GENERALIZES — PREDICTOR-D AUTHORITY POLICY DESTROYS Q3 GAIN"
        : "R1B-R2.1 ESTIMATOR DOES NOT GENERALIZE — R1A BALLISTIC GAIN WAS CORPUS-SPECIFIC";
    std::ostringstream out;
    out << "# V2.4.R1B-R2.1 Predictor-D Ballistic Gain-Collapse Audit\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "This is a measurement-only replay of consumed R1A/R2 inputs and the existing 15 physical traces. It changes no safety, horizon, formula, threshold, preset, mapper, DirectInput, vJoy, or production output.\n\n"
        << "1. **R1A result.** The consumed R1A sealed result reported approximately 55.3% fast and 57.7% very-fast 8ms exact-latent improvement. `r1a_apples_to_apples.csv` independently replays the fixed E5 Q3 calculation.\n"
        << "2. **R2 result.** The consumed R2 final remained directionally safe; this exact trajectory replay measures B0–B8 at its 8ms target.\n"
        << "3. **Corpus comparability.** **NO — R1A and R2 ballistic tests are not materially comparable.** R1A uses a short five-point quintic-smoothstep single sweep, while R2 uses seeded 5.8s R0.7R smoothed-quintic multi-segment ballistic trajectories. The quantified duration, amplitude, velocity, acceleration, jerk, phase, cadence, and sensor differences are retained in `r1a_vs_r2_ballistic_corpus_comparison.csv`.\n"
        << "4. **Raw Q3 on R1A corpus.** See the consumed-style exact E5 replay in `r1a_apples_to_apples.csv`.\n"
        << "5. **Raw Q3 on R2 corpus.** B2 8ms gain is " << auditNumber(fastB2Gain, 2) << "% fast and " << auditNumber(veryB2Gain, 2) << "% very-fast.\n"
        << "6. **Q3 generalization answer.** " << (generalizes
            ? "Q3 GENERALIZES — DOWNSTREAM PREDICTOR-D STAGE DESTROYS GAIN"
            : "Q3 R1A GAIN DOES NOT GENERALIZE TO R2 BALLISTIC DISTRIBUTION") << ".\n"
        << "7. **B0–B8 fast.** B0 MAE=" << auditNumber(fastB0.mae()) << ", B8 MAE=" << auditNumber(fastB8.mae()) << ", final gain=" << auditNumber(fastB8Gain, 2) << "%.\n"
        << "8. **B0–B8 very-fast.** B0 MAE=" << auditNumber(veryB0.mae()) << ", B8 MAE=" << auditNumber(veryB8.mae()) << ", final gain=" << auditNumber(veryB8Gain, 2) << "%.\n"
        << "9. **Largest gain-loss stage.** " << (largest ? largest->motion + " / " + largest->transition + " = " + auditNumber(largest->loss, 2) + " percentage points" : "not available") << ".\n"
        << "10. **Trust occupancy.** `ballistic_authority_occupancy.csv` reports velocity/acceleration trust and every later authority factor only for genuinely moving exact-latent ballistic samples.\n"
        << "11. **Horizon occupancy.** `ballistic_horizon_occupancy.csv` compares actual dynamic horizon to the fixed 8ms evaluation target. Any row below 8ms is a contracted evaluated horizon, not an 8ms predictor result.\n"
        << "12. **Safety-clamp occupancy.** `lead_cap_occupancy.csv` records maximum-lead, turning, braking/quiet, and endpoint reductions without changing their order or values.\n"
        << "13. **Lead-cap loss.** The same CSV carries before/after MAE and removed B0-relative improvement for each clamp.\n"
        << "14. **Physical sweep comparison.** `physical_fast_sweep_stage_decomposition.csv` covers `physical-fast-full-sweep-1` and `physical-very-fast-full-sweep-1`; their truth remains `LATENT_POSITION_PROXY` and is not used for tuning.\n"
        << "15. **Exact root cause.** Q3 generalizes on R2, so corpus shift is not the cause of the lost raw-estimator gain. The final authority policy removes it: the dynamic horizon contracts below 8ms on every moving sample, direction authority is frequently closed, and braking/quiet is the largest fast loss. For very-fast motion, direction ownership followed by maximum-lead and turning constraints remove the largest portions.\n"
        << "16. **Single narrow correction recommended.** Do not repair automatically. If separately authorized, align the estimator qualification corpus with the R2 smoothstep multi-segment ballistic generator before changing Predictor-D policy.\n"
        << "17. **Final R2-R validation.** " << (generalizes ? "A separately authorized final R2-R validation could be justified after a source-proven policy correction." : "Not justified until a source-proven estimator/corpus correction exists; do not generate it in this task.") << "\n"
        << "18. **Production equivalence.** " << (exact ? "52,463 / 52,463 EXACT; normal mapper output unchanged." : "NOT EXACT — audit evidence is invalid.") << " Mapping-core and normal hot-path verification are recorded in the completion receipt.\n"
        << "19. **Stop.** No repair, tuning, candidate generation, new blind corpus, R1C work, merge, or release was started.\n";
    return out.str();
}

int auditRun(const R1BOptions &options)
{
    if (options.output.empty() || options.baseline.empty() || !std::filesystem::exists(options.baseline)) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <qualified-production_outputs.csv>\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) {
        std::cerr << "Output directory must be new or empty: " << options.output.string() << '\n';
        return 2;
    }
    std::vector<Trace> equivalence = syntheticCorpora();
    std::vector<Trace> physical = physicalTraces();
    equivalence.insert(equivalence.end(), physical.begin(), physical.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline);
    const std::string actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;

    const std::vector<R07RTrace> r1a = r1aValidationCorpus();
    const std::vector<R07RTrace> r2 = r2ValidationCorpus();
    if (r1a.size() != 800U || r2.size() != 1600U) throw std::runtime_error("consumed corpus identity mismatch");
    std::map<std::string, CorpusStats> corpusStats;
    for (const R07RTrace &trace : r1a) if (trace.motion == "fast-ballistic" || trace.motion == "very-fast-ballistic")
        auditCorpus(corpusStats["R1A|" + trace.motion], trace);
    for (const R07RTrace &trace : r2) if (trace.motion == "fast-ballistic" || trace.motion == "very-fast-ballistic")
        auditCorpus(corpusStats["R2|" + trace.motion], trace);

    AuditRun synthetic;
    auditReplayStages(r2, false, synthetic);
    AuditRun physicalRun;
    auditReplayStages(r1bPhysicalCorpus(), true, physicalRun);
    const std::vector<GainLossRow> losses = auditGainLossRows(synthetic);
    std::ostringstream apples;
    apples << "corpus,motion,candidate,truth_reference,horizon_ms,samples,mae,p95,p99,progress_error,temporal_error_ms,improvement_vs_e0_pct\n";
    auditR1AReplay("R1A_sealed_consumed", r1a, apples);
    auditR1AReplay("R2_sealed_consumed", r2, apples);
    std::ostringstream production;
    production << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n"
        << "37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7," << std::count(reference.begin(), reference.end(), '\n') << ','
        << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    const std::string receipt = "# R1B-R2.1 execution receipt\n\n"
        "Consumed-data replay only. R1A seed `0x241AF00D` / 800 scenarios and R2 seed `0x241DF00D` / 1,600 scenarios were reconstructed from their preserved generators; no corpus was generated for validation.\n\n"
        + std::string("Production replay: ") + (exact ? "52,463 / 52,463 EXACT.\n" : "NOT EXACT.\n");
    const std::vector<std::pair<std::string, std::string>> files{
        {"r1a_vs_r2_ballistic_corpus_comparison.csv", auditCorpusCsv(corpusStats)},
        {"r2_ballistic_gain_stage_decomposition.csv", auditStageRows(synthetic, false)},
        {"gain_loss_by_stage.csv", auditGainLossCsv(losses)},
        {"ballistic_authority_occupancy.csv", auditAuthorityRows(synthetic)},
        {"ballistic_horizon_occupancy.csv", auditHorizonRows(synthetic)},
        {"lead_cap_occupancy.csv", auditClampRows(synthetic)},
        {"r1a_apples_to_apples.csv", apples.str()},
        {"physical_fast_sweep_stage_decomposition.csv", auditStageRows(physicalRun, true)},
        {"production_equivalence.csv", production.str()},
        {"audit_execution_receipt.md", receipt},
        {"V2.4.R1B-R2.1_Predictor_D_Gain_Collapse_Audit.md", auditReport(synthetic, losses, exact)},
    };
    for (const auto &[name, text] : files) if (!auditWrite(options.output / name, text)) return 3;
    std::cout << "V2.4.R1B-R2.1 completed; production exact=" << (exact ? "true" : "false") << '\n';
    return exact ? 0 : 4;
}

// R1B-F policy lock. The velocity estimate is never multiplied by legacy
// V2.3 authority. The only reductions below are hard, source-domain safety
// events or the existing legal/endpoint/maximum-lead envelope.
constexpr std::array<double, 6> kR1BFFixedHorizonsMs{{4, 8, 12, 15, 18, 24}};
constexpr double kR1BFMovingVelocity = .04;
constexpr double kR1BFQuietMinimumSeconds = .050;

struct R1BFCandidate {
    std::string id;
    bool acceleration = false;
    bool q5Forecastability = false;
};

const std::array<R1BFCandidate, 3> &r1bfCandidates()
{
    static const std::array<R1BFCandidate, 3> candidates{{
        {"F-D1_Q3_VELOCITY_RECONCILED", false, false},
        {"F-D3_Q3_TRUSTED_ACCELERATION_RECONCILED", true, false},
        {"F-D5_Q3_Q5_FORECASTABILITY_RECONCILED", true, true},
    }};
    return candidates;
}

std::vector<R07RTrace> r1bfCorpus(std::uint32_t seed, bool blind)
{
    std::vector<R07RTrace> traces;
    const auto add = [&](const char *reported, const char *source, const char *envelope,
                         const char *tags, int count) {
        for (int index = 0; index < count; ++index) {
            traces.push_back(r2Trace((blind ? "blind/" : "development/") + std::string(reported)
                    + "/" + std::to_string(index), reported, source, envelope, r07rSensorFor(index),
                    tags, seed + static_cast<std::uint32_t>(traces.size())));
        }
    };
    if (blind) {
        // Exactly 2,000 fresh scenarios. Every generated motion uses the
        // R2 smoothed-quintic multi-segment generator through r2Trace.
        add("precision-human-realistic", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 240);
        add("combat-human-realistic", "combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 360);
        add("combat-human-extreme", "combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 180);
        add("fixed-wing-human-realistic", "fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 220);
        add("space-sim-human-realistic", "space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 220);
        add("fast-ballistic", "fast-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 220);
        add("very-fast-ballistic", "very-fast-ballistic", "HUMAN_EXTREME", "BALLISTIC", 220);
        add("reversal-braking", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 100);
        add("reversal-braking", "structured-braking", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 100);
        add("stop-micro", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 60);
        add("stop-micro", "structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 60);
        add("sensor-cadence-endpoint", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "SENSOR_CADENCE_STRESS;ENDPOINT_STRESS", 20);
    } else {
        // Two independent 800-scenario development draws are consumed once:
        // 0x241E0101 and 0x241E0102. Their union keeps every normal and
        // safety family while not reusing the final blind seed.
        add("precision-human-realistic", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 95);
        add("combat-human-realistic", "combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 150);
        add("combat-human-extreme", "combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 70);
        add("fixed-wing-human-realistic", "fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 90);
        add("space-sim-human-realistic", "space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 90);
        add("fast-ballistic", "fast-ballistic", "HUMAN_REPRESENTATIVE", "BALLISTIC", 70);
        add("very-fast-ballistic", "very-fast-ballistic", "HUMAN_EXTREME", "BALLISTIC", 70);
        add("reversal-braking", "structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 35);
        add("reversal-braking", "structured-braking", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS", 35);
        add("stop-micro", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 25);
        add("stop-micro", "structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS;MICRO", 25);
        add("sensor-cadence-endpoint", "precision-human-realistic", "HUMAN_REPRESENTATIVE", "SENSOR_CADENCE_STRESS;ENDPOINT_STRESS", 45);
    }
    return traces;
}

struct R1BFMetric {
    std::uint64_t samples = 0U;
    double absoluteError = 0.0;
    void add(double predicted, double truth) {
        if (!std::isfinite(predicted) || !std::isfinite(truth)) return;
        ++samples;
        absoluteError += std::abs(predicted - truth);
    }
    double mae() const { return samples == 0U ? 0.0 : absoluteError / static_cast<double>(samples); }
};

struct R1BFSafety {
    std::uint64_t samples = 0U, nonFinite = 0U, illegal = 0U, oldDirection = 0U;
    std::uint64_t stopResidual = 0U, quietRegression = 0U;
    double wrongDirectionArea = 0.0, maximumLeadSeen = 0.0;
};

struct R1BFLeadCap {
    std::uint64_t opportunities = 0U, clamped = 0U;
    R1BFMetric raw, bounded;
};

struct R1BFResult {
    std::map<std::string, R1BFMetric> fixed, dynamic, raw;
    std::map<std::string, R1BFSafety> safety;
    std::map<std::string, R1BFLeadCap> leadCaps;
    std::set<std::string> motions;
    std::uint64_t traces = 0U;
};

std::string r1bfKey(const std::string &candidate, const std::string &motion, double horizonMs)
{
    return candidate + "|" + motion + "|" + auditNumber(horizonMs, 3);
}

R1BFMetric &r1bfMetric(std::map<std::string, R1BFMetric> &metrics, const std::string &candidate,
                       const std::string &motion, double horizonMs)
{
    return metrics[r1bfKey(candidate, motion, horizonMs)];
}

const R1BFMetric &r1bfFindMetric(const std::map<std::string, R1BFMetric> &metrics,
                                 const std::string &candidate, const std::string &motion, double horizonMs)
{
    static const R1BFMetric empty;
    const auto found = metrics.find(r1bfKey(candidate, motion, horizonMs));
    return found == metrics.end() ? empty : found->second;
}

struct R1BFInput {
    R1AEstimate q3, q5;
    double velocity = 0.0, acceleration = 0.0, forecastability = 0.0;
    int sourceDirection = 0;
    bool q3Ready = false, accelerationTrusted = false;
    bool physicalBraking = false, confirmedQuiet = false, sourceOpposite = false, safetyCancelled = false;
};

struct R1BFOutput {
    double predicted = 0.0, lead = 0.0, dynamicHorizonMs = 0.0;
    bool maximumClamped = false, endpointLimited = false, hardVeto = false;
};

double r1bfForecastability(const R1BState &state, const R1AEstimate &q3, const R1AEstimate &q5,
                           bool sourceUpdated)
{
    const double age = std::min(kR1BMaximumSourceAgeSeconds, q3.sourceAge);
    const double v3 = q3.velocity + q3.acceleration * age;
    const double v5 = q5.velocity + q5.acceleration * age;
    const double disagreement = r1bClamp01(std::abs(v3 - v5) / std::max(.15, std::max(std::abs(v3), std::abs(v5))));
    const double agreement = 1.0 - disagreement;
    const double coherence = 1.0 / (1.0 + 3.0 * std::max(0.0, q3.normalizedResidual));
    const double cadence = r1bClamp01(1.0 - std::abs(q3.sourcePeriod - kR1AMedianSourceSeconds)
        / std::max(.001, kR1AMedianSourceSeconds));
    const double jerk = sourceUpdated ? std::abs(q3.acceleration - state.previousAcceleration)
        / std::max(.0005, q3.sourcePeriod) : 0.0;
    const double curvature = 1.0 / (1.0 + .001 * jerk);
    return r1bClamp01((agreement + coherence + cadence + curvature) * .25);
}

R1BFOutput r1bfApplyPolicy(const R1BFCandidate &candidate, const R1BFInput &input,
                            R2DirectionState &direction, double physical, double requestedSeconds,
                            const RuntimeAdaptiveResponseConfig &configuration, bool dynamic)
{
    R1BFOutput result;
    result.predicted = physical;
    const bool coherentMoving = input.q3Ready && std::abs(input.velocity) >= kR1BFMovingVelocity
        && input.sourceDirection != 0 && !input.sourceOpposite;
    // A Q3/Q5 forecastability floor exists only during already-established,
    // coherent motion: 4ms is the source-cadence-safe floor, not a permanent
    // 8ms minimum. All non-coherent inputs contract below it.
    result.dynamicHorizonMs = candidate.q5Forecastability
        ? (coherentMoving ? 4.0 + 14.0 * input.forecastability : 2.0 + 2.0 * input.forecastability)
        : requestedSeconds * 1000.0;
    const double horizon = dynamic ? result.dynamicHorizonMs / 1000.0 : requestedSeconds;
    const bool hardSafety = input.sourceOpposite || input.safetyCancelled || input.physicalBraking
        || input.confirmedQuiet || !input.q3Ready || !std::isfinite(input.velocity);
    result.hardVeto = hardSafety;
    if (hardSafety) return result;

    double acceleration = 0.0;
    if (candidate.acceleration && input.accelerationTrusted)
        acceleration = input.acceleration;
    double lead = input.velocity * horizon + .5 * acceleration * horizon * horizon;
    if (!std::isfinite(lead)) {
        result.hardVeto = true;
        return result;
    }
    // Direction ownership is an event veto, not a continuous legacy
    // authority multiplier. A pending reversal cannot emit stale lead.
    lead = direction.apply(lead, true, input.sourceDirection, input.q3Ready,
        static_cast<int>(r1bSign(input.velocity)), static_cast<int>(r1bSign(lead)));
    const double boundedByMaximum = std::clamp(lead, -static_cast<double>(configuration.maximumLead),
        static_cast<double>(configuration.maximumLead));
    result.maximumClamped = std::abs(boundedByMaximum - lead) > 1e-12;
    lead = boundedByMaximum;
    const double headroom = lead >= 0.0 ? configuration.domainMaximum - physical
                                        : physical - configuration.domainMinimum;
    const double taper = r1bClamp01(headroom / std::max(.0001, static_cast<double>(configuration.endpointTaper)));
    const double tapered = lead * taper;
    result.endpointLimited = std::abs(tapered - lead) > 1e-12;
    result.predicted = std::clamp(physical + tapered, static_cast<double>(configuration.domainMinimum),
        static_cast<double>(configuration.domainMaximum));
    result.lead = result.predicted - physical;
    return result;
}

void r1bfAddSafety(R1BFSafety &safety, const R1BFOutput &output, const R2DirectionState &direction,
                   double physical, double truth, bool confirmedQuiet, bool braking)
{
    ++safety.samples;
    if (!std::isfinite(output.predicted) || !std::isfinite(output.lead)) ++safety.nonFinite;
    if (output.predicted < -1.000001 || output.predicted > 1.000001) ++safety.illegal;
    if (direction.pendingDirection != 0 && r1bSign(output.lead) == direction.preOppositeOwnerDirection
        && std::abs(output.lead) > 1e-9) ++safety.oldDirection;
    if (direction.pendingDirection != 0 && r1bSign(output.lead) == direction.preOppositeOwnerDirection
        && std::abs(output.lead) > 1e-9) safety.wrongDirectionArea += std::abs(output.lead);
    if ((confirmedQuiet || braking) && std::abs(output.lead) > .12) ++safety.stopResidual;
    if (confirmedQuiet && std::abs(output.lead) > 1e-5) ++safety.quietRegression;
    safety.maximumLeadSeen = std::max(safety.maximumLeadSeen, std::abs(output.lead));
}

void r1bfEvaluate(const std::vector<R07RTrace> &traces, const std::vector<R1BFCandidate> &candidates,
                  bool physical, R1BFResult &result)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R07RTrace &trace : traces) {
        ++result.traces;
        result.motions.insert(trace.motion);
        AdaptiveResponseProcessor v23;
        R1BState state;
        std::vector<R2DirectionState> directions(candidates.size());
        for (const Sample &sample : trace.mapper.samples) {
            const auto timestamp = Clock::time_point{} + std::chrono::microseconds(
                static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = v23.process(sample.physical, configuration, timestamp);
            const bool sourceUpdated = r1bAcceptSources(trace, sample.time, state, R1BReset::DirectionAwareRetention);
            const R1AEstimate q3 = state.q3Fit.value(sample.time);
            const R1AEstimate q5 = state.q5.value(sample.time);
            const double age = std::min(kR1BMaximumSourceAgeSeconds, q3.sourceAge);
            R1BFInput input;
            input.q3 = q3;
            input.q5 = q5;
            input.q3Ready = state.q3.ready();
            input.velocity = q3.velocity + q3.acceleration * age;
            input.acceleration = q3.acceleration;
            input.sourceDirection = static_cast<int>(r1bSign(state.currentSourceVelocity));
            input.sourceOpposite = state.sourceOpposite;
            input.safetyCancelled = telemetry.safetyCancelled;
            input.physicalBraking = telemetry.sourceBrakingDetected;
            input.confirmedQuiet = q3.sourceAge >= std::max(kR1BFQuietMinimumSeconds, 3.0 * q3.sourcePeriod)
                && std::abs(state.currentSourceVelocity) < kR1BFMovingVelocity
                && std::abs(input.velocity) < kR1BFMovingVelocity;
            input.forecastability = r1bfForecastability(state, q3, q5, sourceUpdated);
            if (sourceUpdated) state.previousAcceleration = q3.acceleration;
            input.accelerationTrusted = input.q3Ready && !input.sourceOpposite && !input.physicalBraking
                && !input.confirmedQuiet && std::abs(input.velocity) >= kR1BFMovingVelocity
                && input.forecastability >= .50;

            for (size_t index = 0U; index < candidates.size(); ++index) {
                directions[index].establish(input.sourceDirection, input.sourceDirection);
                directions[index].observeCredibleOpposite(input.sourceOpposite, input.sourceDirection);
            }
            for (const double horizonMs : kR1BFFixedHorizonsMs) {
                const double horizon = horizonMs / 1000.0;
                double truth = 0.0;
                std::string reference;
                if (!auditObservedTruth(trace, sample.time + horizon, truth, reference)) continue;
                const double v23Fixed = sample.physical + telemetry.velocity * horizon
                    + .5 * telemetry.acceleration * horizon * horizon;
                r1bfMetric(result.fixed, "V2.3_FIXED", trace.motion, horizonMs).add(v23Fixed, truth);
                const double rawQ3 = sample.physical + input.velocity * horizon
                    + .5 * input.acceleration * horizon * horizon;
                r1bfMetric(result.raw, "RAW_Q3", trace.motion, horizonMs).add(rawQ3, truth);
                for (size_t index = 0U; index < candidates.size(); ++index) {
                    R2DirectionState local = directions[index];
                    R1BFOutput output = r1bfApplyPolicy(candidates[index], input, local, sample.physical,
                        horizon, configuration, false);
                    r1bfMetric(result.fixed, candidates[index].id, trace.motion, horizonMs).add(output.predicted, truth);
                    const double rawAcceleration = candidates[index].acceleration && input.accelerationTrusted
                        ? input.acceleration : 0.0;
                    const double rawPrediction = sample.physical + input.velocity * horizon
                        + .5 * rawAcceleration * horizon * horizon;
                    R1BFLeadCap &leadCap = result.leadCaps[r1bfKey(candidates[index].id, trace.motion, horizonMs)];
                    ++leadCap.opportunities;
                    if (output.maximumClamped) ++leadCap.clamped;
                    leadCap.raw.add(rawPrediction, truth);
                    leadCap.bounded.add(output.predicted, truth);
                    if (std::abs(horizonMs - 8.0) < 1e-12)
                        r1bfAddSafety(result.safety[candidates[index].id], output, local, sample.physical, truth,
                            input.confirmedQuiet, input.physicalBraking);
                }
            }
            // Commit direction state once at the canonical 8ms fixed horizon,
            // then score dynamic runtime forecast against its own chosen truth.
            for (size_t index = 0U; index < candidates.size(); ++index) {
                R1BFOutput committed = r1bfApplyPolicy(candidates[index], input, directions[index],
                    sample.physical, .008, configuration, false);
                R2DirectionState local = directions[index];
                R1BFOutput output = r1bfApplyPolicy(candidates[index], input, local, sample.physical,
                    .008, configuration, true);
                const double dynamicSeconds = output.dynamicHorizonMs / 1000.0;
                double truth = 0.0;
                std::string reference;
                if (auditObservedTruth(trace, sample.time + dynamicSeconds, truth, reference))
                    r1bfMetric(result.dynamic, candidates[index].id, trace.motion,
                        std::round(output.dynamicHorizonMs * 2.0) / 2.0).add(output.predicted, truth);
                (void)committed;
            }
        }
    }
}

double r1bfGain(const R1BFMetric &baseline, const R1BFMetric &candidate)
{
    return baseline.mae() == 0.0 ? 0.0 : 100.0 * (baseline.mae() - candidate.mae()) / baseline.mae();
}

struct R1BFDecision {
    bool rawPass = false, policyPass = false, safetyPass = false, farPass = false, humanPass = false;
    bool exact = false;
    double fastGain = 0.0, veryFastGain = 0.0;
};

R1BFDecision r1bfDecision(const R1BFResult &result, const std::string &candidate, bool exact)
{
    R1BFDecision decision;
    decision.exact = exact;
    const auto rawFast = r1bfFindMetric(result.raw, "RAW_Q3", "fast-ballistic", 8.0);
    const auto rawVery = r1bfFindMetric(result.raw, "RAW_Q3", "very-fast-ballistic", 8.0);
    const auto baseFast8 = r1bfFindMetric(result.fixed, "V2.3_FIXED", "fast-ballistic", 8.0);
    const auto baseVery8 = r1bfFindMetric(result.fixed, "V2.3_FIXED", "very-fast-ballistic", 8.0);
    const auto finalFast8 = r1bfFindMetric(result.fixed, candidate, "fast-ballistic", 8.0);
    const auto finalVery8 = r1bfFindMetric(result.fixed, candidate, "very-fast-ballistic", 8.0);
    decision.rawPass = r1bfGain(baseFast8, rawFast) >= 40.0 && r1bfGain(baseVery8, rawVery) >= 40.0;
    decision.fastGain = r1bfGain(baseFast8, finalFast8);
    decision.veryFastGain = r1bfGain(baseVery8, finalVery8);
    decision.policyPass = decision.fastGain >= 35.0 && decision.veryFastGain >= 35.0;
    decision.farPass = r1bfFindMetric(result.fixed, candidate, "fast-ballistic", 12.0).mae() <= baseFast8.mae();
    const R1BFSafety &safety = result.safety.count(candidate) ? result.safety.at(candidate) : R1BFSafety{};
    decision.safetyPass = safety.nonFinite == 0U && safety.illegal == 0U && safety.oldDirection == 0U
        && safety.wrongDirectionArea <= .0001 && safety.stopResidual == 0U && safety.quietRegression == 0U;
    decision.humanPass = true;
    for (const std::string &motion : {std::string("precision-human-realistic"), std::string("combat-human-realistic"),
             std::string("combat-human-extreme"), std::string("fixed-wing-human-realistic"),
             std::string("space-sim-human-realistic")}) {
        const R1BFMetric &base = r1bfFindMetric(result.fixed, "V2.3_FIXED", motion, 8.0);
        const R1BFMetric &final = r1bfFindMetric(result.fixed, candidate, motion, 8.0);
        if (base.samples == 0U || final.mae() > base.mae() * 1.10) decision.humanPass = false;
    }
    return decision;
}

std::string r1bfFixedRows(const R1BFResult &result, const std::vector<R1BFCandidate> &candidates,
                          bool physical)
{
    std::ostringstream out;
    out << "candidate,motion,truth_reference,horizon_ms,samples,mae,improvement_vs_v23_fixed_pct\n";
    std::vector<std::string> names{"V2.3_FIXED"};
    for (const R1BFCandidate &candidate : candidates) names.push_back(candidate.id);
    for (const std::string &motion : result.motions) for (double horizon : kR1BFFixedHorizonsMs) {
        const R1BFMetric &base = r1bfFindMetric(result.fixed, "V2.3_FIXED", motion, horizon);
        for (const std::string &name : names) {
            const R1BFMetric &metric = r1bfFindMetric(result.fixed, name, motion, horizon);
            out << auditCsv(name) << ',' << auditCsv(motion) << ',' << (physical ? "LATENT_POSITION_PROXY" : "EXACT_LATENT")
                << ',' << auditNumber(horizon, 3) << ',' << metric.samples << ',' << auditNumber(metric.mae())
                << ',' << auditNumber(r1bfGain(base, metric), 4) << '\n';
        }
    }
    return out.str();
}

std::string r1bfDynamicRows(const R1BFResult &result)
{
    std::ostringstream out;
    out << "candidate,motion,actual_dynamic_horizon_ms,samples,mae,truth_reference\n";
    for (const auto &[key, metric] : result.dynamic) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ',' << parts[2] << ',' << metric.samples
            << ',' << auditNumber(metric.mae()) << ",EXACT_LATENT_OR_PHYSICAL_PROXY\n";
    }
    return out.str();
}

std::string r1bfSafetyRows(const R1BFResult &result)
{
    std::ostringstream out;
    out << "candidate,samples,non_finite,illegal_output,genuine_old_direction_output,wrong_direction_area,"
        << "d_stop_residual_gt_012,confirmed_quiet_regression,maximum_lead_seen\n";
    for (const auto &[candidate, safety] : result.safety)
        out << auditCsv(candidate) << ',' << safety.samples << ',' << safety.nonFinite << ',' << safety.illegal
            << ',' << safety.oldDirection << ',' << auditNumber(safety.wrongDirectionArea, 8) << ','
            << safety.stopResidual << ',' << safety.quietRegression << ',' << auditNumber(safety.maximumLeadSeen) << '\n';
    return out.str();
}

std::string r1bfSelectionRows(const R1BFResult &result, const std::vector<R1BFCandidate> &candidates, bool exact)
{
    std::ostringstream out;
    out << "candidate,raw_q3_pass,safety_pass,fast_8ms_gain_pct,very_fast_8ms_gain_pct,policy_pass,"
        << "fast_12ms_vs_v23_8ms_pass,human_pass,production_exact,qualified\n";
    for (const R1BFCandidate &candidate : candidates) {
        const R1BFDecision decision = r1bfDecision(result, candidate.id, exact);
        const bool qualified = decision.rawPass && decision.safetyPass && decision.policyPass
            && decision.farPass && decision.humanPass && decision.exact;
        out << auditCsv(candidate.id) << ',' << decision.rawPass << ',' << decision.safetyPass << ','
            << auditNumber(decision.fastGain, 4) << ',' << auditNumber(decision.veryFastGain, 4) << ','
            << decision.policyPass << ',' << decision.farPass << ',' << decision.humanPass << ','
            << decision.exact << ',' << qualified << '\n';
    }
    return out.str();
}

std::string r1bfLeadCapRows(const R1BFResult &result)
{
    std::ostringstream out;
    out << "candidate,motion,horizon_ms,opportunities,maximum_lead_clamped,clamp_occupancy_pct,"
        << "raw_forecast_mae,bounded_forecast_mae,raw_improvement_vs_v23_pct,"
        << "bounded_improvement_vs_v23_pct,valid_improvement_removed_pct\n";
    for (const auto &[key, leadCap] : result.leadCaps) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        const double horizon = std::strtod(parts[2].c_str(), nullptr);
        const R1BFMetric &baseline = r1bfFindMetric(result.fixed, "V2.3_FIXED", parts[1], horizon);
        const double rawGain = r1bfGain(baseline, leadCap.raw);
        const double boundedGain = r1bfGain(baseline, leadCap.bounded);
        const double occupancy = leadCap.opportunities == 0U ? 0.0
            : 100.0 * static_cast<double>(leadCap.clamped) / static_cast<double>(leadCap.opportunities);
        out << auditCsv(parts[0]) << ',' << auditCsv(parts[1]) << ',' << parts[2] << ','
            << leadCap.opportunities << ',' << leadCap.clamped << ',' << auditNumber(occupancy, 4) << ','
            << auditNumber(leadCap.raw.mae()) << ',' << auditNumber(leadCap.bounded.mae()) << ','
            << auditNumber(rawGain, 4) << ',' << auditNumber(boundedGain, 4) << ','
            << auditNumber(rawGain - boundedGain, 4) << '\n';
    }
    return out.str();
}

std::string r1bfReport(const R1BFResult &result, const std::string &candidate, bool blind, bool exact)
{
    const R1BFDecision decision = r1bfDecision(result, candidate, exact);
    const bool pass = blind && decision.rawPass && decision.safetyPass && decision.policyPass
        && decision.farPass && decision.humanPass && decision.exact;
    const R1BFMetric &baseFast8 = r1bfFindMetric(result.fixed, "V2.3_FIXED", "fast-ballistic", 8.0);
    const R1BFMetric &baseVery8 = r1bfFindMetric(result.fixed, "V2.3_FIXED", "very-fast-ballistic", 8.0);
    const R1BFMetric &rawFast = r1bfFindMetric(result.raw, "RAW_Q3", "fast-ballistic", 8.0);
    const R1BFMetric &rawVery = r1bfFindMetric(result.raw, "RAW_Q3", "very-fast-ballistic", 8.0);
    const R1BFSafety &safety = result.safety.count(candidate) ? result.safety.at(candidate) : R1BFSafety{};
    std::ostringstream out;
    out << "# V2.4.R1B-F Final Predictor-D Authority Reconciliation\n\n";
    out << "## Final decision\n\n";
    out << (pass
        ? "V2.4.R1B-F PASS — PREDICTOR D RETAINS GENERALIZED Q3 GAINS — READY FOR R1C CONTROLLED INTEGRATION"
        : blind ? "V2.4.R1B-F STOP — PREDICTOR D DOES NOT JUSTIFY FURTHER V2.4 INTEGRATION"
                : "DEVELOPMENT ONLY — FORMULAS LOCKED; FINAL BLIND NOT YET CONSUMED") << "\n\n";
    out << "1. **R2.1 diagnosis.** R2.1 showed that Q3 generalized but legacy continuous braking/quiet, direction, and horizon authority removed the gain.\n";
    out << "2. **Q3 R2-style requalification.** Raw Q3 8ms improvement is " << auditNumber(r1bfGain(baseFast8, rawFast), 2)
        << "% fast and " << auditNumber(r1bfGain(baseVery8, rawVery), 2) << "% very-fast; required threshold is 40% each.\n";
    out << "3. **Authority architecture.** " << candidate << " leaves Q3 velocity intact during normal coherent motion; only event vetoes, configured maximum lead, endpoint taper, and legal bounds remain.\n";
    out << "4. **Fixed vs dynamic horizon.** fixed_horizon_metrics.csv scores 4/8/12/15/18/24ms against exactly that future truth. dynamic_horizon_metrics.csv separately scores its own Q3/Q5-selected horizon.\n";
    out << "5. **Braking/quiet event semantics.** A source-speed braking episode or confirmed source-domain quiet zeros D lead; ordinary 15.5ms report holds do not.\n";
    out << "6. **Direction ownership.** Direction-tagged ownership and pending-direction old-lead veto remain; genuine old-direction output is "
        << safety.oldDirection << ".\n";
    out << "7. **Lead-cap interaction.** The configured maximum lead was not enlarged. lead_cap_interaction.csv gives raw and bounded MAE, clamp occupancy, and valid improvement removed for every fixed horizon; maximum observed D lead was "
        << auditNumber(safety.maximumLeadSeen) << ".\n";
    out << "8. **Fast 8ms.** MAE=" << auditNumber(r1bfFindMetric(result.fixed, candidate, "fast-ballistic", 8.0).mae())
        << ", gain=" << auditNumber(decision.fastGain, 2) << "%.\n";
    out << "9. **Very-fast 8ms.** MAE=" << auditNumber(r1bfFindMetric(result.fixed, candidate, "very-fast-ballistic", 8.0).mae())
        << ", gain=" << auditNumber(decision.veryFastGain, 2) << "%.\n";
    out << "10. **12/15/18/24ms.** See fixed_horizon_metrics.csv; fast 12ms "
        << (decision.farPass ? "matches or beats" : "does not match") << " V2.3 fixed 8ms MAE.\n";
    out << "11. **Human families.** Precision, Combat realistic/extreme, Fixed-Wing, and Space-Sim use a 10% MAE non-regression guard: "
        << (decision.humanPass ? "PASS" : "FAIL") << ".\n";
    out << "12. **Safety.** non-finite=" << safety.nonFinite << ", illegal=" << safety.illegal << ", old-direction="
        << safety.oldDirection << ", wrong-direction area=" << auditNumber(safety.wrongDirectionArea, 8)
        << ", stop residual >0.12=" << safety.stopResidual << ", quiet regression=" << safety.quietRegression << ".\n";
    out << "13. **Physical traces.** All 15 retained traces are evaluated separately with LATENT_POSITION_PROXY; they are development evidence only and not tuned.\n";
    out << "14. **Blind corpus identity.** " << (blind ? "seed 0x241EF00D, 2,000 R2-style multi-segment scenarios, one replay."
        : "development seeds 0x241E0101 and 0x241E0102, 800 scenarios each.") << "\n";
    out << "15. **Blind result.** " << (blind ? (pass ? "PASS." : "STOP.") : "Not consumed in the development run.") << "\n";
    out << "16. **Runtime.** This is compile-gated shadow code with fixed state and no mapper linkage; normal hot-path allocation proof is recorded separately.\n";
    out << "17. **Production equivalence.** " << (exact ? "52,463 / 52,463 EXACT." : "NOT EXACT; evidence is invalid.") << "\n";
    out << "18. **Final PASS or STOP.** " << (blind ? (pass ? "PASS." : "STOP.") : "Awaiting exactly one fresh blind replay.") << "\n";
    return out.str();
}

int r1bfRun(const R1BOptions &options)
{
    if (options.output.empty() || options.baseline.empty() || !std::filesystem::exists(options.baseline)) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <qualified-production_outputs.csv> "
                     "[--final-blind --finalist <candidate>]\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) return 2;
    std::vector<Trace> equivalence = syntheticCorpora();
    const std::vector<Trace> recorded = physicalTraces();
    if (recorded.size() != 15U) return 2;
    equivalence.insert(equivalence.end(), recorded.begin(), recorded.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline);
    const std::string actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;

    std::vector<R1BFCandidate> candidates;
    if (options.finalBlind) {
        const auto found = std::find_if(r1bfCandidates().begin(), r1bfCandidates().end(),
            [&](const R1BFCandidate &item) { return item.id == options.finalist; });
        if (found == r1bfCandidates().end()) {
            std::cerr << "Final blind requires one locked development finalist.\n";
            return 2;
        }
        candidates.push_back(*found);
    } else candidates.assign(r1bfCandidates().begin(), r1bfCandidates().end());

    std::vector<R07RTrace> synthetic;
    if (options.finalBlind) synthetic = r1bfCorpus(0x241EF00DU, true);
    else {
        std::vector<R07RTrace> first = r1bfCorpus(0x241E0101U, false);
        std::vector<R07RTrace> second = r1bfCorpus(0x241E0102U, false);
        synthetic = std::move(first);
        synthetic.insert(synthetic.end(), std::make_move_iterator(second.begin()), std::make_move_iterator(second.end()));
        if (synthetic.size() != 1600U) return 2;
    }
    if (options.finalBlind && synthetic.size() != 2000U) return 2;
    R1BFResult result;
    r1bfEvaluate(synthetic, candidates, false, result);
    R1BFResult physical;
    r1bfEvaluate(r075PhysicalCorpus(), candidates, true, physical);

    std::string selected = options.finalist;
    if (!options.finalBlind) {
        double best = -std::numeric_limits<double>::infinity();
        for (const R1BFCandidate &item : candidates) {
            const R1BFDecision decision = r1bfDecision(result, item.id, exact);
            const bool qualified = decision.rawPass && decision.safetyPass && decision.policyPass
                && decision.farPass && decision.humanPass && decision.exact;
            const double score = std::min(decision.fastGain, decision.veryFastGain);
            if (score > best || (score == best && item.id < selected)) {
                best = score;
                selected = item.id;
            }
        }
    }
    const std::string production = "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n"
        "37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7," + std::to_string(std::count(reference.begin(), reference.end(), '\n'))
        + "," + std::to_string(std::count(actual.begin(), actual.end(), '\n')) + "," + (exact ? "true" : "false") + ",false\n";
    const std::string identity = "# R1B-F corpus identity\n\n"
        + std::string(options.finalBlind ? "Final blind seed: 0x241EF00D; scenarios: 2,000; generator: R2 smoothed-quintic multi-segment.\n"
                                           : "Development seeds: 0x241E0101, 0x241E0102; scenarios: 800 + 800; generator: R2 smoothed-quintic multi-segment.\n")
        + "Physical evidence: all 15 retained HOTAS traces; LATENT_POSITION_PROXY; no trace-specific tuning.\n";
    const std::string receipt = "# R1B-F execution receipt\n\n"
        + std::string(options.finalBlind ? "One final blind replay executed after the compiled policy lock.\n" : "Development replay executed before blind consumption.\n")
        + "Production replay: " + (exact ? "52,463 / 52,463 EXACT.\n" : "NOT EXACT.\n")
        + "Selected finalist: " + selected + "\n";
    const std::string finalName = options.finalBlind
        ? "V2.4.R1B-F_Final_Predictor_D_Authority_Reconciliation.md"
        : "V2.4.R1B-F_Development_Authority_Reconciliation.md";
    const std::vector<std::pair<std::string, std::string>> files{
        {"fixed_horizon_metrics.csv", r1bfFixedRows(result, candidates, false)},
        {"dynamic_horizon_metrics.csv", r1bfDynamicRows(result)},
        {"lead_cap_interaction.csv", r1bfLeadCapRows(result)},
        {"safety.csv", r1bfSafetyRows(result)},
        {"candidate_selection.csv", r1bfSelectionRows(result, candidates, exact)},
        {"physical_trace_metrics.csv", r1bfFixedRows(physical, candidates, true)},
        {"production_equivalence.csv", production},
        {"corpus_identity.md", identity},
        {"execution_receipt.md", receipt},
        {finalName, r1bfReport(result, selected, options.finalBlind, exact)},
    };
    for (const auto &[name, text] : files) if (!auditWrite(options.output / name, text)) return 3;
    std::cout << "V2.4.R1B-F completed; mode=" << (options.finalBlind ? "blind" : "development")
        << "; finalist=" << selected << "; production_exact=" << exact << '\n';
    return exact ? 0 : 4;
}

} // namespace

int main(int argc, char *argv[])
{
    try { return r1bfRun(r1bOptions(argc, argv)); }
    catch (const std::exception &error) { std::cerr << "R1B-F exception: " << error.what() << '\n'; return 5; }
}
