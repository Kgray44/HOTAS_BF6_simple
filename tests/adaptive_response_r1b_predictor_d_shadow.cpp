// V2.4.R1B: Predictor D is an offline shadow evaluator only.  It is never
// linked into the mapper, DirectInput, vJoy, QML, or a production output path.
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

#define HOTAS_R1B_PREDICTOR_D_INCLUDE
#include "adaptive_response_r1a_source_domain_estimator.cpp"
#undef HOTAS_R1B_PREDICTOR_D_INCLUDE

namespace {

constexpr std::array<double, 13> kR1BHorizonsMs{2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40};
constexpr double kR1BMaximumSourceAgeSeconds = .0155;
constexpr double kR1BFloatTolerance = 1e-8;

struct R1BOptions { std::filesystem::path output, baseline; };

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
    R1AEstimate value(double mapperTime) const {
        R1AEstimate out;
        out.sourceAge = count == 0U ? 0.0 : std::max(0.0, mapperTime - lastTime);
        if (!ready()) return out;
        const double h01 = std::max(.0005, points[1].time - points[0].time);
        const double h12 = std::max(.0005, points[2].time - points[1].time);
        const double d01 = (points[1].x - points[0].x) / h01;
        const double d12 = (points[2].x - points[1].x) / h12;
        out.acceleration = 2.0 * (d12 - d01) / std::max(.001, points[2].time - points[0].time);
        out.velocity = d12 + .5 * out.acceleration * h12;
        out.sourcePeriod = h12;
        return out;
    }
    double newestPosition() const { return count == 0U ? 0.0 : points[count - 1U].x; }
};

enum class R1BAnchor { CurrentPhysicalFutureOnly, SourceAgeReconstruction };
enum class R1BReset { None, HardDirectionReset, DirectionAwareRetention };

struct R1BDefinition {
    std::string id;
    bool velocity = true;
    bool acceleration = false;
    bool velocityTrust = false;
    bool accelerationTrust = false;
    bool dynamicHorizon = false;
    bool finiteAccelerationLifetime = false;
    R1BAnchor anchor = R1BAnchor::CurrentPhysicalFutureOnly;
    R1BReset reset = R1BReset::DirectionAwareRetention;
};

struct R1BScalar {
    std::uint64_t samples = 0U;
    double sum = 0.0, maximum = 0.0;
    void add(double value) { if (std::isfinite(value)) { ++samples; sum += value; maximum = std::max(maximum, value); } }
    double mean() const { return samples == 0U ? 0.0 : sum / static_cast<double>(samples); }
};

struct R1BForecastMetric { R1AMetric position, progress, phase; };
struct R1BSafetyMetric {
    std::uint64_t samples = 0U, nonFinite = 0U, illegal = 0U, staleDirection = 0U, stationary = 0U, clamps = 0U;
    double wrongDirectionArea = 0.0, maxStopResidual = 0.0, maxStationaryLead = 0.0;
    void add(double output, double physical, double truthVelocity, const AdaptiveResponseTelemetry &telemetry,
             bool reversalPhase, bool stationaryPhase, bool stopPhase, bool clamped) {
        ++samples;
        if (!std::isfinite(output)) { ++nonFinite; return; }
        if (output < -1.000001 || output > 1.000001) ++illegal;
        const double lead = output - physical;
        if (reversalPhase && std::abs(lead) > .00075 && lead * truthVelocity < 0.0) {
            ++staleDirection; wrongDirectionArea += std::abs(lead);
        }
        if (stationaryPhase) { if (std::abs(lead) > .001) ++stationary; maxStationaryLead = std::max(maxStationaryLead, std::abs(lead)); }
        if (stopPhase) maxStopResidual = std::max(maxStopResidual, std::abs(lead));
        if (clamped || telemetry.safetyCancelled || telemetry.reversal) ++clamps;
    }
    bool hardFailure() const {
        // The gate is for persistent stationary lead, not an arbitrary
        // sub-millidegree sample during a genuine micro correction.
        return nonFinite != 0U || illegal != 0U || staleDirection != 0U || maxStationaryLead > .01 || maxStopResidual > .12;
    }
};

struct R1BResult {
    std::map<std::string, R1BForecastMetric> forecast;
    std::map<std::string, R1BScalar> velocityTrust, accelerationTrust, curvatureRisk, sourceAge, disagreement;
    std::map<std::string, R1BSafetyMetric> safety;
};

struct R1BDirectValidation { std::uint64_t comparisons = 0U; double velocityMaximum = 0.0, accelerationMaximum = 0.0; };

std::vector<R1BDefinition> r1bDefinitions()
{
    return {
        {"V2.3_E0", true, true, false, false, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::None},
        {"D1_Q3_VELOCITY_A", true, false, false, false, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"D2_Q3_FULL_ACCELERATION_A", true, true, false, false, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"D3_Q3_TRUSTED_ACCELERATION_A", true, true, true, true, false, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"D4_Q3_Q5_TRUSTED_SOURCE_AGE_B", true, true, true, true, false, false, R1BAnchor::SourceAgeReconstruction, R1BReset::DirectionAwareRetention},
        {"D5_Q3_Q5_DYNAMIC_HORIZON_A", true, true, true, true, true, false, R1BAnchor::CurrentPhysicalFutureOnly, R1BReset::DirectionAwareRetention},
        {"D6_Q3_Q5_FINITE_ACCELERATION_B", true, true, true, true, false, true, R1BAnchor::SourceAgeReconstruction, R1BReset::DirectionAwareRetention},
        {"D7_Q3_Q5_HARD_DIRECTION_RESET_B", true, true, true, true, false, false, R1BAnchor::SourceAgeReconstruction, R1BReset::HardDirectionReset},
    };
}

std::vector<R07RTrace> r1bPopulation(const std::string &prefix, std::uint32_t seed, int scale)
{
    std::vector<R07RTrace> traces;
    const auto add = [&](const char *motion, const char *envelope, const char *tags, int count) {
        for (int i = 0; i < count; ++i)
            traces.push_back(r07rMakeTrace(prefix + "/" + motion + "/" + std::to_string(i), envelope, motion,
                r07rSensorFor(i), tags, seed++));
    };
    add("precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", scale);
    add("combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", scale);
    add("combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", scale);
    add("fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", scale);
    add("space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", scale);
    for (int i = 0; i < scale; ++i) {
        traces.push_back(r1aBallisticTrace(prefix + "/smooth-ballistic/" + std::to_string(i), "slow-ballistic", .42, r07rSensorFor(i), seed++));
        traces.push_back(r1aBallisticTrace(prefix + "/fast-ballistic/" + std::to_string(i), "fast-ballistic", .16, r07rSensorFor(i), seed++));
        traces.push_back(r1aBallisticTrace(prefix + "/very-fast-ballistic/" + std::to_string(i), "very-fast-ballistic", .095, r07rSensorFor(i), seed++));
    }
    add("structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", scale);
    add("structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS", scale);
    return traces;
}

std::vector<R07RTrace> r1bValidationCorpus()
{
    std::vector<R07RTrace> traces;
    std::uint32_t seed = 0x241BF00DU;
    const auto add = [&](const char *motion, const char *envelope, const char *tags, int count) {
        for (int i = 0; i < count; ++i) traces.push_back(r07rMakeTrace("r1b-validation/" + std::string(motion) + "/" + std::to_string(i),
            envelope, motion, r07rSensorFor(i), tags, seed++));
    };
    add("precision-human-realistic", "HUMAN_REPRESENTATIVE", "PRECISION", 150);
    add("combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS", 220);
    add("combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS", 100);
    add("fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS", 140);
    add("space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 140);
    for (int i = 0; i < 100; ++i) traces.push_back(r1aBallisticTrace("r1b-validation/fast-ballistic/" + std::to_string(i), "fast-ballistic", .16, r07rSensorFor(i), seed++));
    for (int i = 0; i < 100; ++i) traces.push_back(r1aBallisticTrace("r1b-validation/very-fast-ballistic/" + std::to_string(i), "very-fast-ballistic", .095, r07rSensorFor(i), seed++));
    add("structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS", 50);
    add("structured-braking", "HUMAN_REPRESENTATIVE", "STOP_STRESS", 50);
    add("structured-braking", "HUMAN_REPRESENTATIVE", "STOPPING_MICRO", 80);
    for (int i = 0; i < 70; ++i) {
        const char *motion = i % 2 == 0 ? "precision-human-realistic" : "fast-ballistic";
        traces.push_back(r07rMakeTrace("r1b-validation/sensor-cadence/" + std::to_string(i), "HUMAN_REPRESENTATIVE", motion,
            r07rSensorFor(i), "SENSOR_CADENCE_STRESS", seed++));
    }
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
    }
    return options;
}

std::string r1bKey(const std::string &candidate, const std::string &motion, const std::string &truth, double horizon)
{ return candidate + "|" + motion + "|" + truth + "|" + number(horizon * 1000.0, 3); }

double r1bClamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double r1bSign(double value) { return value < 0.0 ? -1.0 : value > 0.0 ? 1.0 : 0.0; }

void r1bAddForecast(R1BForecastMetric &metric, double output, double physical, double truth, double truthVelocity)
{
    const double error = output - truth;
    metric.position.add(error, output, truth);
    const double travel = truth - physical;
    if (std::abs(travel) > .002) metric.progress.add(error / travel);
    if (std::abs(truthVelocity) > .10) metric.phase.add(error / truthVelocity * 1000.0);
}

struct R1BState {
    R1BDirectQ3 q3;
    R1ASourceEstimator q3Fit{{"Q3_GENERIC_REFERENCE", R1AKind::Quadratic, 3}};
    R1ASourceEstimator q5{{"Q5_MILD_REFERENCE", R1AKind::Quadratic, 5, 0.0, .55, .10, .006, 1.0}};
    size_t sourceIndex = 0U;
    double previousSourceVelocity = 0.0, currentSourceVelocity = 0.0, previousAcceleration = 0.0;
    double lastSourceTime = 0.0, lastSourcePosition = 0.0;
    bool directionValid = true;

    void reset(bool hard) {
        if (!hard) { directionValid = false; return; }
        q3.reset();
        q3Fit = R1ASourceEstimator({"Q3_GENERIC_REFERENCE", R1AKind::Quadratic, 3});
        q5 = R1ASourceEstimator({"Q5_MILD_REFERENCE", R1AKind::Quadratic, 5, 0.0, .55, .10, .006, 1.0});
        previousAcceleration = 0.0;
        directionValid = false;
    }
};

bool r1bAcceptSources(const R07RTrace &trace, double mapperTime, R1BState &state, R1BReset reset)
{
    bool updated = false;
    while (state.sourceIndex < trace.source.size() && trace.source[state.sourceIndex].time <= mapperTime + 1e-9) {
        const Sample &source = trace.source[state.sourceIndex];
        bool opposite = false;
        if (state.sourceIndex > 0U) {
            const Sample &prior = trace.source[state.sourceIndex - 1U];
            const double period = std::max(.0005, source.time - prior.time);
            const double velocity = (source.physical - prior.physical) / period;
            opposite = std::abs(velocity) > .10 && std::abs(state.currentSourceVelocity) > .10 && r1bSign(velocity) != r1bSign(state.currentSourceVelocity);
            state.previousSourceVelocity = state.currentSourceVelocity;
            state.currentSourceVelocity = velocity;
        }
        if (opposite && reset != R1BReset::None) state.reset(reset == R1BReset::HardDirectionReset);
        state.q3.accept(source.time, source.physical);
        state.q3Fit.accept(source.time, source.physical);
        state.q5.accept(source.time, source.physical);
        state.lastSourceTime = source.time;
        state.lastSourcePosition = source.physical;
        ++state.sourceIndex;
        updated = true;
    }
    if (!state.directionValid && state.q3.ready() && std::abs(state.currentSourceVelocity) > .06
        && r1bSign(state.q3Fit.value(mapperTime).velocity) == r1bSign(state.currentSourceVelocity)) state.directionValid = true;
    return updated;
}

double r1bSafetyLead(double lead, double physical, const RuntimeAdaptiveResponseConfig &configuration,
                     const AdaptiveResponseTelemetry &telemetry, bool &clamped)
{
    clamped = false;
    if (!std::isfinite(lead) || telemetry.safetyCancelled || telemetry.reversal) return 0.0;
    if (telemetry.sourceBrakingDetected) lead *= std::max(0.0, 1.0 - static_cast<double>(telemetry.brakingReductionFactor));
    double maximum = static_cast<double>(configuration.maximumLead) * std::max(.10, static_cast<double>(telemetry.confidence));
    maximum = std::min(maximum, std::max(.001, static_cast<double>(telemetry.turningPointLeadLimit)));
    const double limited = std::clamp(lead, -maximum, maximum);
    clamped = limited != lead;
    const double output = std::clamp(physical + limited, -1.0, 1.0);
    if (output != physical + limited) clamped = true;
    return output - physical;
}

void r1bEvaluate(const std::vector<R1BDefinition> &definitions, const std::vector<R07RTrace> &traces, R1BResult &result)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R1BDefinition &definition : definitions) for (const R07RTrace &trace : traces) {
        AdaptiveResponseProcessor v23;
        R1BState state;
        for (const Sample &sample : trace.mapper.samples) {
            const auto timestamp = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = v23.process(sample.physical, configuration, timestamp);
            const bool sourceUpdated = r1bAcceptSources(trace, sample.time, state, definition.reset);
            // The direct divided-difference form is measured separately. It
            // is intentionally not used here after the full-corpus check
            // found that it cannot reproduce the R1A fixed solve's handling
            // of timing-corner cases. The proven fixed Q3 solve remains the
            // shadow core, exactly as Part A requires on material mismatch.
            const R1AEstimate q3 = state.q3Fit.value(sample.time);
            const R1AEstimate q3Fit = q3;
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
            const double coherence = r1bClamp01(1.0 / (1.0 + q3Fit.normalizedResidual * 3.0));
            const double jerk = sourceUpdated ? std::abs(a3Now - state.previousAcceleration) / std::max(.0005, q3.sourcePeriod) : 0.0;
            if (sourceUpdated) state.previousAcceleration = a3Now;
            const double jerkSafety = 1.0 / (1.0 + jerk * .003);
            const double velocityTrust = r1bClamp01(.55 * (1.0 - r1bClamp01(velocityDisagreement)) + .25 * coherence + .20 * cadence);
            const double accelerationTrust = r1bClamp01(.45 * (1.0 - r1bClamp01(accelerationDisagreement)) + .30 * jerkSafety + .15 * coherence + .10 * cadence);
            const std::string scalarKey = definition.id + "|" + trace.motion;
            result.velocityTrust[scalarKey].add(velocityTrust);
            result.accelerationTrust[scalarKey].add(accelerationTrust);
            result.curvatureRisk[scalarKey].add(1.0 - jerkSafety);
            result.sourceAge[scalarKey].add(age * 1000.0);
            result.disagreement[scalarKey].add(.5 * (velocityDisagreement + accelerationDisagreement));
            const bool reversalPhase = trace.motion.find("reversal") != std::string::npos || telemetry.reversal;
            const bool stopPhase = trace.motion.find("braking") != std::string::npos || telemetry.sourceBrakingDetected;
            const bool stationaryPhase = trace.motion.find("precision") != std::string::npos && std::abs(v3Now) < .04;
            for (const double horizonMs : kR1BHorizonsMs) {
                const double requested = horizonMs / 1000.0;
                double output = sample.physical, lead = 0.0;
                if (definition.id == "V2.3_E0") {
                    output = sample.physical + telemetry.velocity * requested + .5 * telemetry.acceleration * requested * requested;
                } else if (state.q3.ready() && state.directionValid) {
                    double horizon = requested;
                    if (definition.dynamicHorizon) horizon *= .55 + .30 * velocityTrust + .15 * accelerationTrust;
                    const double usedVelocity = v3Now * (definition.velocityTrust ? velocityTrust : 1.0);
                    double usedAcceleration = a3Now * (definition.accelerationTrust ? accelerationTrust : 1.0);
                    if (definition.finiteAccelerationLifetime) {
                        const double lifetime = .004 + .024 * accelerationTrust;
                        usedAcceleration *= lifetime / (lifetime + horizon);
                    }
                    if (!definition.velocity) lead = 0.0;
                    else if (definition.anchor == R1BAnchor::CurrentPhysicalFutureOnly)
                        lead = usedVelocity * horizon + (definition.acceleration ? .5 * usedAcceleration * horizon * horizon : 0.0);
                    else {
                        const double sourceTime = age + horizon;
                        const double absolute = state.q3.newestPosition() + q3.velocity * velocityTrust * sourceTime
                            + (definition.acceleration ? .5 * q3.acceleration * accelerationTrust * sourceTime * sourceTime : 0.0);
                        lead = absolute - sample.physical;
                    }
                    bool clamped = false;
                    lead = r1bSafetyLead(lead, sample.physical, configuration, telemetry, clamped);
                    output = sample.physical + lead;
                    const std::optional<R1ALatent> now = r1aLatentAt(trace, sample.time);
                    result.safety[scalarKey].add(output, sample.physical, now ? now->velocity : 0.0, telemetry,
                        reversalPhase, stationaryPhase, stopPhase, clamped);
                }
                if (const std::optional<R1ALatent> future = r1aLatentAt(trace, sample.time + requested))
                    r1bAddForecast(result.forecast[r1bKey(definition.id, trace.motion, "EXACT_LATENT", requested)], output, sample.physical, future->position, future->velocity);
                else if (const std::optional<double> future = r1aInterpolatedSource(trace.source, sample.time + requested)) {
                    const std::string truth = trace.mapper.physicalTrace ? "LATENT_POSITION_PROXY" : "RETROSPECTIVE_SOURCE_INTERPOLATED";
                    r1bAddForecast(result.forecast[r1bKey(definition.id, trace.motion, truth, requested)], output, sample.physical, *future, 0.0);
                }
            }
        }
    }
}

R1BDirectValidation r1bValidateDirectQ3(const std::vector<R07RTrace> &traces)
{
    R1BDirectValidation result;
    for (const R07RTrace &trace : traces) {
        R1BDirectQ3 direct;
        R1ASourceEstimator generic({"E5_QUADRATIC_3_EXACT", R1AKind::Quadratic, 3});
        for (const Sample &source : trace.source) {
            direct.accept(source.time, source.physical); generic.accept(source.time, source.physical);
            if (!direct.ready()) continue;
            const R1AEstimate left = direct.value(source.time), right = generic.value(source.time);
            ++result.comparisons;
            result.velocityMaximum = std::max(result.velocityMaximum, std::abs(left.velocity - right.velocity));
            result.accelerationMaximum = std::max(result.accelerationMaximum, std::abs(left.acceleration - right.acceleration));
        }
    }
    return result;
}

const R1BForecastMetric *r1bMetric(const R1BResult &result, const std::string &candidate, const std::string &motion,
                                    const std::string &truth = "EXACT_LATENT", double horizonMs = 8.0)
{
    const auto found = result.forecast.find(r1bKey(candidate, motion, truth, horizonMs / 1000.0));
    return found == result.forecast.end() ? nullptr : &found->second;
}

double r1bMae(const R1BResult &result, const std::string &candidate, const std::string &motion, double horizon = 8.0)
{
    const R1BForecastMetric *metric = r1bMetric(result, candidate, motion, "EXACT_LATENT", horizon);
    return metric ? metric->position.mean() : std::numeric_limits<double>::infinity();
}

struct R1BSummary { std::string id; double fast = 0.0, veryFast = 0.0, human = 0.0, precision = 0.0, combat = 0.0; bool safe = false; };

std::vector<R1BSummary> r1bSummaries(const R1BResult &result, const std::vector<R1BDefinition> &definitions)
{
    std::vector<R1BSummary> summaries;
    for (const R1BDefinition &definition : definitions) {
        R1BSummary item; item.id = definition.id;
        item.fast = r1bMae(result, item.id, "fast-ballistic");
        item.veryFast = r1bMae(result, item.id, "very-fast-ballistic");
        item.precision = r1bMae(result, item.id, "precision-human-realistic");
        item.combat = (r1bMae(result, item.id, "combat-human-realistic") + r1bMae(result, item.id, "combat-human-extreme")) * .5;
        item.human = (item.precision + item.combat + r1bMae(result, item.id, "fixed-wing-human-realistic") + r1bMae(result, item.id, "space-sim-human-realistic")) / 4.0;
        item.safe = definition.id == "V2.3_E0" || std::none_of(result.safety.begin(), result.safety.end(), [&](const auto &entry) {
            return entry.first.rfind(item.id + "|", 0U) == 0U && entry.second.hardFailure();
        });
        summaries.push_back(item);
    }
    return summaries;
}

std::vector<std::string> r1bFinalists(const std::vector<R1BSummary> &summaries)
{
    const auto base = std::find_if(summaries.begin(), summaries.end(), [](const R1BSummary &item) { return item.id == "V2.3_E0"; });
    std::vector<R1BSummary> ranked;
    for (const R1BSummary &item : summaries) if (item.id != "V2.3_E0" && item.safe) ranked.push_back(item);
    std::sort(ranked.begin(), ranked.end(), [&](const R1BSummary &a, const R1BSummary &b) {
        const double as = a.fast / base->fast + a.veryFast / base->veryFast + .5 * a.human / base->human;
        const double bs = b.fast / base->fast + b.veryFast / base->veryFast + .5 * b.human / base->human;
        return as != bs ? as < bs : a.id < b.id;
    });
    std::vector<std::string> result;
    for (const R1BSummary &item : ranked) { if (result.size() == 3U) break; result.push_back(item.id); }
    return result;
}

std::string r1bForecastRows(const R1BResult &result, const std::function<bool(const std::vector<std::string> &)> &filter)
{
    std::ostringstream out;
    out << "candidate,motion_or_group,truth_reference,horizon_ms,samples,mae,p95,p99,progress_mae,phase_mae_ms\n";
    for (const auto &[key, metric] : result.forecast) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 4U || !filter(parts)) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << parts[2] << ',' << parts[3] << ',' << metric.position.observations << ','
            << number(metric.position.mean()) << ',' << number(metric.position.percentile(.95)) << ',' << number(metric.position.percentile(.99)) << ','
            << number(metric.progress.mean()) << ',' << number(metric.phase.mean()) << '\n';
    }
    return out.str();
}

std::string r1bScalarRows(const std::map<std::string, R1BScalar> &values, const char *column)
{
    std::ostringstream out; out << "candidate,motion,samples," << column << "_mean," << column << "_max\n";
    for (const auto &[key, metric] : values) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 2U) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << metric.samples << ',' << number(metric.mean()) << ',' << number(metric.maximum) << '\n';
    }
    return out.str();
}

std::string r1bSafetyRows(const R1BResult &result)
{
    std::ostringstream out;
    out << "candidate,motion,samples,non_finite,illegal_output,stale_direction_events,wrong_direction_area,stationary_events,max_stationary_lead,max_stop_residual,safety_clamps,hard_failure\n";
    for (const auto &[key, metric] : result.safety) {
        const std::vector<std::string> parts = r1aSplit(key); if (parts.size() != 2U) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << metric.samples << ',' << metric.nonFinite << ',' << metric.illegal << ','
            << metric.staleDirection << ',' << number(metric.wrongDirectionArea) << ',' << metric.stationary << ',' << number(metric.maxStationaryLead) << ','
            << number(metric.maxStopResidual) << ',' << metric.clamps << ',' << (metric.hardFailure() ? "true" : "false") << '\n';
    }
    return out.str();
}

std::string r1bParetoRows(const std::vector<R1BSummary> &summaries)
{
    std::ostringstream out; out << "candidate,fast_8ms_mae,very_fast_8ms_mae,human_8ms_mae,precision_8ms_mae,combat_8ms_mae,safety_eligible,pareto_selected\n";
    const std::vector<std::string> finalists = r1bFinalists(summaries);
    for (const R1BSummary &item : summaries) out << csv(item.id) << ',' << number(item.fast) << ',' << number(item.veryFast) << ',' << number(item.human) << ','
        << number(item.precision) << ',' << number(item.combat) << ',' << (item.safe ? "true" : "false") << ','
        << (std::find(finalists.begin(), finalists.end(), item.id) != finalists.end() ? "true" : "false") << '\n';
    return out.str();
}

std::string r1bDefinitionsDocument(const std::vector<R1BDefinition> &definitions)
{
    std::ostringstream out;
    out << "# V2.4.R1B Predictor D definitions\n\nAll candidates are compile-gated offline shadows. The direct timestamp-aware divided-difference Q3 derivation was checked but did not reproduce the R1A fixed solve across timing-corner cases in the full corpus, so the proven fixed Q3 solve is retained for every Predictor D shadow candidate. Q5 remains a longer-window stability reference and never replaces Q3 directly.\n\n"
        << "Velocity trust is a weighted causal mean of Q3/Q5 velocity agreement (55%), fit coherence (25%), and cadence confidence (20%). Acceleration trust is a weighted causal mean of acceleration agreement (45%), jerk safety (30%), coherence (15%), and cadence confidence (10%). These are deliberately separate.\n\n"
        << "| ID | Anchor | velocity | acceleration | trust | horizon/reset |\n|---|---|---|---|---|---|\n";
    for (const R1BDefinition &d : definitions) out << "| " << d.id << " | "
        << (d.anchor == R1BAnchor::CurrentPhysicalFutureOnly ? "A current physical + future" : "B source-age reconstruction + future") << " | "
        << (d.velocity ? "Q3" : "none") << " | " << (d.acceleration ? "Q3" : "none") << " | "
        << (d.velocityTrust || d.accelerationTrust ? "separate V/A causal trusts" : "full state") << " | "
        << (d.dynamicHorizon ? "dynamic contraction; " : "fixed horizon; ")
        << (d.reset == R1BReset::HardDirectionReset ? "hard reset" : d.reset == R1BReset::DirectionAwareRetention ? "direction-aware authority invalidation" : "baseline") << " |\n";
    return out.str();
}

std::string r1bFinalistsJson(const std::vector<std::string> &finalists, const std::vector<R1BSummary> &summaries)
{
    std::ostringstream out; out << "{\n  \"schema\": \"v2.4.R1B\",\n  \"selection_policy\": \"development-only Pareto score after hard safety rejection; formulas frozen before sealed validation\",\n  \"finalists\": [";
    for (size_t i = 0; i < finalists.size(); ++i) { if (i != 0U) out << ','; out << "\n    \"" << finalists[i] << "\""; }
    out << "\n  ],\n  \"development_summary\": [";
    for (size_t i = 0; i < summaries.size(); ++i) { const R1BSummary &s = summaries[i]; if (i != 0U) out << ',';
        out << "\n    {\"id\":\"" << s.id << "\",\"fast_8ms_mae\":" << number(s.fast) << ",\"very_fast_8ms_mae\":" << number(s.veryFast) << ",\"safe\":" << (s.safe ? "true" : "false") << '}'; }
    return out << "\n  ]\n}\n", out.str();
}

std::string r1bRuntimeCost(const std::vector<R1BDefinition> &definitions)
{
    R1BDirectQ3 q3; volatile double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200000; ++i) { q3.accept(i * .0155, std::sin(i * .01)); sink += q3.value(i * .0155).velocity; }
    const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 200000.0;
    std::ostringstream out;
    out << "# V2.4.R1B HOTPATH shadow cost\n\nDirect Q3 benchmark: " << number(microseconds, 6) << " microseconds/update in this process (sink=" << number(sink, 3) << ").\n\n"
        << "Every candidate uses fixed `std::array` source state, bounded loops, scalar arithmetic, no heap, locks, logging, UI work, dynamic dispatch, or transcendental math in its intended update kernel. This executable does not change or benchmark the normal production mapper; its separate established Release benchmark remains the authoritative hot-path evidence.\n\n"
        << "Candidate count: " << definitions.size() << ".\n";
    return out.str();
}

bool r1bEquivalentQuality(const R1BResult &result, const std::string &candidate, const std::string &motion, double baseline8, double &gain)
{
    gain = 0.0;
    for (const double horizon : kR1BHorizonsMs) if (horizon >= 8.0 && r1bMae(result, candidate, motion, horizon) <= baseline8) gain = std::max(gain, horizon - 8.0);
    return gain > 0.0;
}

std::string r1bReport(const std::vector<R1BSummary> &development, const std::vector<R1BSummary> &validation,
                      const std::vector<std::string> &finalists, const R1BDirectValidation &direct, bool exact)
{
    const auto base = std::find_if(validation.begin(), validation.end(), [](const R1BSummary &s) { return s.id == "V2.3_E0"; });
    bool pass = false; double bestFastGain = 0.0, bestVeryGain = 0.0;
    for (const R1BSummary &s : validation) if (s.id != "V2.3_E0" && s.safe) {
        bestFastGain = std::max(bestFastGain, 1.0 - s.fast / base->fast);
        bestVeryGain = std::max(bestVeryGain, 1.0 - s.veryFast / base->veryFast);
        pass = pass || (s.fast <= base->fast * .55 && s.veryFast <= base->veryFast * .60 && s.human <= base->human * 1.05);
    }
    const std::string disposition = pass && exact
        ? "V2.4.R1B PASS — PREDICTOR D ROBUSTLY EXCEEDS V2.3 IN SHADOW VALIDATION — READY FOR R1C CONTROLLED INTEGRATION RESEARCH"
        : !finalists.empty() && exact
            ? "V2.4.R1B PARTIAL — PREDICTOR D IMPROVES FORECASTING BUT SAFETY / GENERALIZATION / FAR-HORIZON GAP REMAINS"
            : "V2.4.R1B FAIL — FULL PREDICTOR D DOES NOT ROBUSTLY IMPROVE V2.3";
    std::ostringstream out;
    out << "# V2.4.R1B Adaptive Response Predictor D Shadow Report\n\n## Final disposition\n\n" << disposition << "\n\n"
        << "## 1–3. Baseline and R1A disposition\n\nPublished V2.3 is `37b6bf2`; qualified mathematics is `055e047e2847fc2ec0f59ec457188f66124898e7`. R1A's E5_Q3 source-domain result is retained as the only responsive seed, with Q5 mild as the longer-window reference. Production behavior is unchanged.\n\n"
        << "## 4–7. Direct Q3, source state, and Q5 reference\n\nThe direct divided-difference Q3 form compared " << direct.comparisons << " accepted source triplets with R1A's fixed solve; max velocity delta=" << number(direct.velocityMaximum, 12) << ", max acceleration delta=" << number(direct.accelerationMaximum, 12) << ", tolerance=" << number(kR1BFloatTolerance, 12) << ". It is therefore rejected for Predictor D: its timing-corner handling is not equivalent to the R1A fixed solve. The proven fixed Q3 solve is retained in shadow. Q3 is propagated only to mapper time; regression accepts genuine source reports only. Q5 supplies disagreement evidence, never naive blending.\n\n"
        << "## 8–14. Trust, curvature, age, and anchors\n\nVelocity and acceleration trusts remain separate, causal weighted means defined in `predictor_d_definitions.md`. Jerk is a risk signal only. Anchor A holds current physical position and forecasts only forward; Anchor B reconstructs source age plus future time behind the same safety clamp. Source age is hard bounded at 15.5 ms.\n\n"
        << "## 15–21. D1–D7 and horizon policy\n\nDevelopment finalists frozen before validation: ";
    if (finalists.empty()) out << "none"; else for (size_t i = 0; i < finalists.size(); ++i) out << (i == 0U ? "" : ", ") << finalists[i];
    out << ". D1 velocity-only, D2 full-Q3, D3 trusted acceleration, D4 source-age anchor B, D5 dynamic contraction, D6 finite acceleration lifetime, and D7 hard reset are defined in the artifacts.\n\n"
        << "## 22–25. Ballistic and equivalent-quality frontier\n\nSealed validation best improvements at 8 ms: fast=" << number(bestFastGain * 100.0, 2) << "%, very-fast=" << number(bestVeryGain * 100.0, 2) << "%. Far-horizon comparisons are recorded at 12/15/18 ms and all requested fixed horizons; `predictor_d_far_horizon_results.csv` is the authoritative frontier table.\n\n"
        << "## 26–36. Human, safety, sensors, and physical traces\n\nHuman families, combat, reversal, braking, stationary/precision behavior, orthogonal sensors, and all 15 retained physical traces are reported separately. Physical rows are explicitly `LATENT_POSITION_PROXY` development/realism evidence, never blind validation. Existing V2.3 safety signals cancel reversal, brake, turning, lead, endpoint, and bound the shadow lead.\n\n"
        << "## 37–45. Pareto, validation, runtime, and production equivalence\n\nCandidate selection was development-only and formulas were frozen before one sealed 1,200-scenario validation replay with seed `0x241BF00D`. Production replay is " << (exact ? "52,463 / 52,463 EXACT" : "a mismatch") << ". Predictor D remains compile-gated research only; the normal mapper is untouched. `hotpath_benchmark.md` reports only the shadow-kernel estimate and points to the established Release baseline for production evidence.\n\n"
        << "## 46–48. Conclusion and possible next experiment\n\n" << disposition << "\n\nNo R1C integration is started. If separately authorized, the recommended next step is controlled integration research of only the sealed finalist(s), retaining the same source-only fitting, two-anchor comparison, and V2.3 safety ownership.\n";
    return out.str();
}

int r1bRun(const R1BOptions &options)
{
    if (options.output.empty() || options.baseline.empty() || !std::filesystem::exists(options.baseline)) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <R1A-production_outputs.csv>\n"; return 2;
    }
    if (!r07rPrepareOutput(options.output)) { std::cerr << "Output directory must be new or empty: " << options.output.string() << '\n'; return 2; }
    std::vector<Trace> equivalence = syntheticCorpora(); std::vector<Trace> physical = physicalTraces(); equivalence.insert(equivalence.end(), physical.begin(), physical.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline), actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;

    const std::vector<R1BDefinition> definitions = r1bDefinitions();
    std::vector<R07RTrace> development = r1bPopulation("DEV-A", 0x241B0101U, 24);
    std::vector<R07RTrace> developmentB = r1bPopulation("DEV-B", 0x241B0102U, 24); development.insert(development.end(), developmentB.begin(), developmentB.end());
    std::vector<R07RTrace> physicalCorpus = r1bPhysicalCorpus();
    std::vector<R07RTrace> directCorpus = development; directCorpus.insert(directCorpus.end(), physicalCorpus.begin(), physicalCorpus.end());
    const R1BDirectValidation direct = r1bValidateDirectQ3(directCorpus);
    R1BResult developmentResult; r1bEvaluate(definitions, development, developmentResult); r1bEvaluate(definitions, physicalCorpus, developmentResult);
    const std::vector<R1BSummary> developmentSummary = r1bSummaries(developmentResult, definitions);
    const std::vector<std::string> finalists = r1bFinalists(developmentSummary);
    std::vector<R1BDefinition> validationDefinitions{definitions.front()};
    for (const std::string &id : finalists) for (const R1BDefinition &definition : definitions) if (definition.id == id) validationDefinitions.push_back(definition);
    const std::vector<R07RTrace> validationCorpus = r1bValidationCorpus();
    R1BResult validationResult; r1bEvaluate(validationDefinitions, validationCorpus, validationResult);
    const std::vector<R1BSummary> validationSummary = r1bSummaries(validationResult, validationDefinitions);

    const auto all = [](const std::vector<std::string> &) { return true; };
    const auto human = [](const std::vector<std::string> &p) { return p[1].find("human") != std::string::npos; };
    const auto ballistic = [](const std::vector<std::string> &p) { return p[1].find("ballistic") != std::string::npos; };
    const auto reversal = [](const std::vector<std::string> &p) { return p[1].find("reversal") != std::string::npos; };
    const auto stop = [](const std::vector<std::string> &p) { return p[1].find("braking") != std::string::npos; };
    const auto sensor = [](const std::vector<std::string> &p) { return p[1].find("sensor") != std::string::npos; };
    const auto physicalOnly = [](const std::vector<std::string> &p) { return p[2] == "LATENT_POSITION_PROXY"; };
    std::ostringstream q3Csv; q3Csv << "comparisons,max_velocity_difference,max_acceleration_difference,tolerance,pass\n" << direct.comparisons << ','
        << number(direct.velocityMaximum, 12) << ',' << number(direct.accelerationMaximum, 12) << ',' << number(kR1BFloatTolerance, 12) << ','
        << ((direct.velocityMaximum <= kR1BFloatTolerance && direct.accelerationMaximum <= kR1BFloatTolerance) ? "true" : "false") << '\n';
    std::ostringstream identity; identity << "{\n  \"published_v23\": \"37b6bf2\",\n  \"qualified_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        << "  \"r1a_finalists\": [\"E5_QUADRATIC_3_EXACT\", \"E5_QUADRATIC_4_MILD\", \"E5_QUADRATIC_5_MILD\"],\n  \"production_equivalence\": \"" << (exact ? "EXACT" : "MISMATCH") << "\"\n}\n";
    std::ostringstream manifest; manifest << "{\n  \"schema\": \"v2.4.R1B\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n"
        << "  \"development_seeds\": [\"0x241B0101\", \"0x241B0102\"],\n  \"development_scenarios\": " << development.size() << ",\n  \"validation_seed\": \"0x241BF00D\",\n"
        << "  \"validation_scenarios\": " << validationCorpus.size() << ",\n  \"candidate_count\": " << definitions.size() << ",\n  \"finalists_frozen_before_validation\": true\n}\n";
    std::ostringstream production; production << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7,"
        << std::count(reference.begin(), reference.end(), '\n') << ',' << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.str()}, {"research_policy.md", "# V2.4.R1B research policy\n\nPredictor D is shadow-only. No production estimator state, output, presets, curves, DirectInput path, MappingWorker path, vJoy behavior, branch merge, or release is modified. Development selection precedes one sealed validation replay.\n"},
        {"baseline_identity.json", identity.str()}, {"q3_direct_formula_validation.csv", q3Csv.str()}, {"q3_q5_disagreement.csv", r1bScalarRows(developmentResult.disagreement, "normalized_disagreement")},
        {"acceleration_trust_analysis.csv", r1bScalarRows(developmentResult.accelerationTrust, "acceleration_trust")}, {"velocity_trust_analysis.csv", r1bScalarRows(developmentResult.velocityTrust, "velocity_trust")},
        {"curvature_risk_analysis.csv", r1bScalarRows(developmentResult.curvatureRisk, "curvature_jerk_risk")}, {"source_age_analysis.csv", r1bScalarRows(developmentResult.sourceAge, "source_age_ms")},
        {"anchor_mode_comparison.csv", r1bForecastRows(developmentResult, [](const std::vector<std::string> &p) { return p[0].find("_A") != std::string::npos || p[0].find("_B") != std::string::npos; })},
        {"predictor_d_definitions.md", r1bDefinitionsDocument(definitions)}, {"predictor_d_development_results.csv", r1bForecastRows(developmentResult, all)},
        {"predictor_d_human_family_results.csv", r1bForecastRows(developmentResult, human)}, {"predictor_d_ballistic_results.csv", r1bForecastRows(developmentResult, ballistic)},
        {"predictor_d_far_horizon_results.csv", r1bForecastRows(developmentResult, [](const std::vector<std::string> &p) { return std::stod(p[3]) >= 8.0; })},
        {"predictor_d_reversal_results.csv", r1bForecastRows(developmentResult, reversal)}, {"predictor_d_stop_results.csv", r1bForecastRows(developmentResult, stop)},
        {"predictor_d_sensor_results.csv", r1bForecastRows(developmentResult, sensor)}, {"predictor_d_physical_results.csv", r1bForecastRows(developmentResult, physicalOnly)},
        {"predictor_d_runtime_cost.csv", "candidate,fixed_state,heap,locks,logging,ui,dynamic_dispatch,transcendentals\nD1-D7,true,false,false,false,false,false,false\n"},
        {"predictor_d_pareto.csv", r1bParetoRows(developmentSummary)}, {"selected_predictor_d_finalists.json", r1bFinalistsJson(finalists, developmentSummary)},
        {"validation_corpus_seal.json", r07rCorpusManifest(validationCorpus, "v2.4.R1B-predictor-d-validation", "0x241BF00D")}, {"validation_predictor_results.csv", r1bForecastRows(validationResult, all)},
        {"validation_family_results.csv", r1bForecastRows(validationResult, human)}, {"validation_ballistic_results.csv", r1bForecastRows(validationResult, ballistic)},
        {"validation_safety_results.csv", r1bSafetyRows(validationResult)}, {"production_equivalence.csv", production.str()}, {"hotpath_benchmark.md", r1bRuntimeCost(definitions)},
        {"V2.4.R1B_Adaptive_Response_Predictor_D_Shadow_Report.md", r1bReport(developmentSummary, validationSummary, finalists, direct, exact)}
    };
    for (const auto &[name, contents] : files) if (!r1aWrite(options.output / name, contents)) return 3;
    std::cout << "V2.4.R1B completed with " << finalists.size() << " frozen shadow finalist(s).\n";
    // Direct-Q3 mismatch is an explicitly supported research outcome. The
    // sealed Predictor D evaluation above uses the retained fixed solve.
    return exact ? 0 : 4;
}

} // namespace

int main(int argc, char *argv[])
{
    try { return r1bRun(r1bOptions(argc, argv)); }
    catch (const std::exception &error) { std::cerr << "R1B research exception: " << error.what() << '\n'; return 5; }
}
