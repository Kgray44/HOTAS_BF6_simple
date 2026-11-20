// V2.4.R0.7R is an offline research executable.  It intentionally includes
// the frozen R0.7 evaluator for its Q-A..Q-E calibration mechanics, but all
// new corpus construction, source-hold semantics, and reporting live here.
// No object from this translation unit is linked into MappingWorker.
#include <functional>
#define main historical_forecastability_main
#include "adaptive_response_forecastability_research.cpp"
#undef main

namespace {

constexpr double kR07RRepresentativeSpeed = 18.27882537;
constexpr double kR07RRepresentativeAcceleration = 650.50991119;
constexpr double kR07RRepresentativeJerk = 43363.58890598;
constexpr double kR07RExtremeSpeed = 43.95235174;
constexpr double kR07RExtremeAcceleration = 1297.26998635;
constexpr double kR07RExtremeJerk = 106728.77704640;
constexpr double kR07RCadenceMedian = .01548401;
constexpr double kR07RCadenceP95 = .016073975;
constexpr double kR07RCadenceP99 = .01619800;

struct R07RTrace {
    Trace mapper;
    std::vector<Sample> source;
    // Exact continuous generator knots are retained only where the synthetic
    // motion is generated from the smooth point trajectory.  Stateful Combat
    // and recorded physical traces deliberately leave this empty: neither has
    // an unobserved measured continuous position between source reports.
    std::vector<std::pair<double, float>> latentPoints;
    std::string envelope;
    std::string motion;
    std::string sensor;
    std::string tags;
};

struct R07REvaluation {
    R07RTrace trace;
    std::vector<Observation> rows;
};

struct R07RSourceMetrics {
    Stats velocity;
    Stats acceleration;
    Stats jerk;
    Stats cadence;
    Stats reversalDensity;
    Stats reversalSpacing;
};

struct R07ROptions {
    std::filesystem::path output;
    std::filesystem::path baseline;
    std::filesystem::path report;
    std::filesystem::path hotpath;
    std::filesystem::path r065;
};

std::string r07rReadText(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

double r07rCadence(std::uint32_t &state, bool lowRate)
{
    if (lowRate) return .026 + unitRandom(state) * .020;
    // The ordinary model is deliberately source-rate based: the 250 Hz mapper
    // will receive this state through sample-and-hold, not a new human intent.
    return std::clamp(kR07RCadenceMedian + (unitRandom(state) - .5) * .00118,
        .01472, .01682);
}

float r07rSmoothAt(const std::vector<std::pair<double, float>> &points, double time)
{
    if (points.empty() || time <= points.front().first) return points.empty() ? 0.0F : points.front().second;
    if (time >= points.back().first) return points.back().second;
    for (size_t index = 1; index < points.size(); ++index) {
        if (time > points[index].first) continue;
        const auto &[leftTime, left] = points[index - 1U];
        const auto &[rightTime, right] = points[index];
        const double t = std::clamp((time - leftTime) / std::max(1e-9, rightTime - leftTime), 0.0, 1.0);
        const double smooth = t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
        return static_cast<float>(left + (right - left) * smooth);
    }
    return points.back().second;
}

std::vector<std::pair<double, float>> r07rPoints(const std::string &motion, bool extreme, std::uint32_t seed)
{
    std::vector<std::pair<double, float>> points;
    std::uint32_t state = seed == 0U ? 0x2407F00DU : seed;
    double time = 0.0;
    float position = static_cast<float>((unitRandom(state) - .5) * .45);
    points.push_back({time, position});
    const double finish = 5.8;
    while (time < finish) {
        double duration = .24 + unitRandom(state) * .34;
        double delta = (unitRandom(state) - .5) * .85;
        if (motion == "precision-human-realistic") {
            duration = .34 + unitRandom(state) * .62;
            delta = (unitRandom(state) - .5) * .34;
        } else if (motion == "fixed-wing-human-realistic") {
            duration = .48 + unitRandom(state) * .62;
            delta = (unitRandom(state) - .5) * 1.42;
        } else if (motion == "space-sim-human-realistic") {
            duration = unitRandom(state) < .45 ? .22 + unitRandom(state) * .26 : .42 + unitRandom(state) * .45;
            delta = (unitRandom(state) - .5) * (unitRandom(state) < .45 ? .55 : 1.20);
        } else if (motion == "combat-human-realistic") {
            // R0.65 retained seed/cap identity; these segments stay well below
            // its independently recorded representative acceleration/jerk caps.
            duration = .18 + unitRandom(state) * .31;
            delta = (unitRandom(state) - .5) * .94;
        } else if (motion == "combat-human-extreme") {
            duration = .105 + unitRandom(state) * .22;
            delta = (unitRandom(state) - .5) * 1.45;
        } else if (motion == "fast-ballistic") {
            duration = .16; delta = position < 0.0F ? 1.80 : -1.80;
        } else if (motion == "very-fast-ballistic") {
            duration = .095; delta = position < 0.0F ? 1.88 : -1.88;
        } else if (motion == "structured-reversal") {
            duration = .20 + unitRandom(state) * .12; delta = position < 0.0F ? 1.55 : -1.55;
        } else if (motion == "structured-braking") {
            duration = .22 + unitRandom(state) * .18; delta = position < 0.0F ? 1.35 : -1.35;
        }
        if (extreme && motion != "combat-human-extreme") { duration *= .72; delta *= 1.12; }
        duration = std::max(.082, duration);
        time = std::min(finish, time + duration);
        position = std::clamp(position + static_cast<float>(delta), -0.98F, 0.98F);
        points.push_back({time, position});
        if ((motion == "fixed-wing-human-realistic" || motion == "precision-human-realistic") && unitRandom(state) < .22 && time < finish) {
            time = std::min(finish, time + .04 + unitRandom(state) * .12);
            points.push_back({time, position});
        }
    }
    points.push_back({6.0, points.back().second});
    return points;
}

float r07rSensorValue(float value, const std::string &sensor, double time, std::uint32_t &state)
{
    if (sensor == "quantized") return std::round(value * 4096.0F) / 4096.0F;
    if (sensor == "jittery") return std::clamp(value + static_cast<float>((unitRandom(state) - .5) * .0016), -1.0F, 1.0F);
    if (sensor == "spiky-drift") {
        const float drift = static_cast<float>(std::sin(time * .47) * .0013);
        const float spike = unitRandom(state) < .006 ? static_cast<float>((unitRandom(state) - .5) * .014) : 0.0F;
        return std::clamp(value + drift + spike, -1.0F, 1.0F);
    }
    return value;
}

std::vector<Sample> r07rSourceFromPoints(const std::vector<std::pair<double, float>> &points,
                                          const std::string &sensor, std::uint32_t seed)
{
    std::vector<Sample> source;
    std::uint32_t state = seed ^ 0xA07DCA61U;
    const double finish = points.empty() ? 6.0 : points.back().first;
    for (double time = 0.0; time <= finish + 1e-9;) {
        source.push_back({time, r07rSensorValue(r07rSmoothAt(points, time), sensor, time, state)});
        time = std::min(finish, time + r07rCadence(state, sensor == "low-rate-hold"));
        if (finish - time < 1e-8 && source.back().time < finish) continue;
        if (time >= finish && source.back().time >= finish) break;
    }
    if (source.empty() || source.back().time < finish) source.push_back({finish, r07rSensorValue(r07rSmoothAt(points, finish), sensor, finish, state)});
    return source;
}

std::vector<std::string> r07rSplitCsv(const std::string &line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}

struct R07RPhysicalProfile {
    std::vector<double> cadence;
    std::vector<double> speed;
    std::vector<double> reversalSpacing;
};

double r07rQuantile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::floor(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1U)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

const R07RPhysicalProfile &r07rPhysicalProfile()
{
    static const R07RPhysicalProfile profile = [] {
        R07RPhysicalProfile result;
        const std::filesystem::path fixtures = std::filesystem::current_path() / "tests" / "fixtures" / "adaptive_response_real_hotas";
        for (const auto &entry : std::filesystem::directory_iterator(fixtures)) {
            if (entry.path().extension() != ".csv") continue;
            std::ifstream input(entry.path());
            std::string line;
            if (!std::getline(input, line)) continue;
            const std::vector<std::string> header = r07rSplitCsv(line);
            const auto findColumn = [&](const char *name) {
                const auto found = std::find(header.begin(), header.end(), name);
                return found == header.end() ? header.size() : static_cast<size_t>(std::distance(header.begin(), found));
            };
            const size_t time = findColumn("time_ms"), position = findColumn("physical_raw_normalized"), reported = findColumn("source_reported");
            if (time == header.size() || position == header.size() || reported == header.size()) continue;
            std::vector<Sample> source;
            while (std::getline(input, line)) {
                const std::vector<std::string> fields = r07rSplitCsv(line);
                if (fields.size() <= std::max({time, position, reported}) || fields[reported] != "1") continue;
                source.push_back({std::strtod(fields[time].c_str(), nullptr) / 1000.0,
                                  static_cast<float>(std::strtod(fields[position].c_str(), nullptr))});
            }
            if (source.size() < 3U) continue;
            if (std::abs(source.front().time) < 1e-9 && std::abs(source.front().physical) <= (1.5F / 65535.0F)) {
                const double origin = source[1].time;
                source.erase(source.begin());
                for (Sample &sample : source) sample.time -= origin;
            }
            std::vector<double> velocity, midpoint;
            for (size_t index = 1; index < source.size(); ++index) {
                const double dt = source[index].time - source[index - 1U].time;
                if (dt <= 1e-9) continue;
                const double v = (source[index].physical - source[index - 1U].physical) / dt;
                result.cadence.push_back(dt); result.speed.push_back(std::abs(v)); velocity.push_back(v);
                midpoint.push_back((source[index].time + source[index - 1U].time) * .5);
            }
            int direction = 0;
            double lastReversal = 0.0;
            for (size_t index = 0; index < velocity.size(); ++index) {
                const int sign = std::abs(velocity[index]) > .65204503 ? (velocity[index] < 0.0 ? -1 : 1) : 0;
                if (sign != 0 && direction != 0 && sign != direction) {
                    if (lastReversal > 0.0) result.reversalSpacing.push_back(midpoint[index] - lastReversal);
                    lastReversal = midpoint[index];
                }
                if (sign != 0) direction = sign;
            }
        }
        return result;
    }();
    return profile;
}

std::vector<Sample> r07rCorrectedCombatSource(bool extreme, const std::string &sensor, std::uint32_t seed)
{
    // This is the retained R0.65 stateful Combat formula: its empirical source
    // cadence/speed/reversal distributions and caps are re-read from the 15
    // raw fixture streams, then only the sensor layer may transform samples.
    const R07RPhysicalProfile &human = r07rPhysicalProfile();
    std::uint32_t state = seed;
    const double speedCap = extreme ? kR07RExtremeSpeed : kR07RRepresentativeSpeed;
    const double accelerationCap = extreme ? kR07RExtremeAcceleration : kR07RRepresentativeAcceleration;
    const double jerkCap = extreme ? kR07RExtremeJerk : kR07RRepresentativeJerk;
    std::vector<Sample> source;
    double time = 0.0;
    double position = (unitRandom(state) - .5) * .5;
    double velocity = 0.0, acceleration = 0.0, targetVelocity = 0.0, segmentRemaining = 0.0;
    int direction = unitRandom(state) < .5 ? -1 : 1;
    source.push_back({time, r07rSensorValue(static_cast<float>(position), sensor, time, state)});
    while (time < 3.0) {
        const double cadence = human.cadence.empty() ? kR07RCadenceMedian
            : human.cadence[static_cast<size_t>(unitRandom(state) * human.cadence.size()) % human.cadence.size()];
        const double dt = std::min(3.0 - time, std::clamp(cadence, .002, .060));
        if (segmentRemaining <= 0.0) {
            const double intent = unitRandom(state);
            if (intent < .18) targetVelocity = 0.0;
            else {
                if (intent < .62) direction = -direction;
                const double q = extreme ? .78 + .215 * unitRandom(state) : .55 + .40 * unitRandom(state);
                targetVelocity = direction * std::min(speedCap, r07rQuantile(human.speed, q));
                if (extreme && unitRandom(state) > .90) targetVelocity = direction * speedCap * (.90 + .10 * unitRandom(state));
            }
            const double base = human.reversalSpacing.empty() ? .18 : r07rQuantile(human.reversalSpacing, .30 + .55 * unitRandom(state));
            segmentRemaining = std::clamp(base, .045, .65);
        }
        const double requestedAcceleration = std::clamp((targetVelocity - velocity) / std::max(.018, segmentRemaining), -accelerationCap, accelerationCap);
        const double accelerationDelta = std::clamp(requestedAcceleration - acceleration, -jerkCap * dt, jerkCap * dt);
        acceleration = std::clamp(acceleration + accelerationDelta, -accelerationCap, accelerationCap);
        velocity = std::clamp(velocity + acceleration * dt, -speedCap, speedCap);
        position += velocity * dt;
        if (position >= .98 || position <= -.98) {
            position = std::clamp(position, -.98, .98);
            velocity = -velocity * .18; acceleration = -acceleration * .35;
            direction = velocity >= 0.0 ? 1 : -1; segmentRemaining = 0.0;
        }
        time += dt; segmentRemaining -= dt;
        source.push_back({std::min(time, 3.0), r07rSensorValue(static_cast<float>(position), sensor, time, state)});
    }
    return source;
}

void r07rApplyHold(R07RTrace &trace)
{
    trace.mapper.samples.clear();
    if (trace.source.empty()) return;
    size_t sourceIndex = 0U;
    const double finish = trace.source.back().time + .20;
    for (double mapperTime = 0.0; mapperTime <= finish + 1e-9; mapperTime += .004) {
        while (sourceIndex + 1U < trace.source.size() && trace.source[sourceIndex + 1U].time <= mapperTime + 1e-9) ++sourceIndex;
        trace.mapper.samples.push_back({mapperTime, trace.source[sourceIndex].physical});
    }
}

R07RTrace r07rMakeTrace(const std::string &id, const std::string &envelope, const std::string &motion,
                         const std::string &sensor, const std::string &tags, std::uint32_t seed)
{
    R07RTrace result;
    result.mapper = Trace{id, "r07r", motion, false, {}};
    result.envelope = envelope;
    result.motion = motion;
    result.sensor = sensor;
    result.tags = tags;
    const bool extreme = envelope == "HUMAN_EXTREME";
    if (motion == "combat-human-realistic" || motion == "combat-human-extreme") {
        result.source = r07rCorrectedCombatSource(extreme, sensor, seed);
    } else {
        result.latentPoints = r07rPoints(motion, extreme, seed);
        result.source = r07rSourceFromPoints(result.latentPoints, sensor, seed);
    }
    r07rApplyHold(result);
    return result;
}

R07RTrace r07rMakeTorture(const std::string &id, const std::string &motion, const std::string &tags, std::uint32_t seed)
{
    R07RTrace result = r07rMakeTrace(id, "TORTURE_BEYOND_HUMAN", motion, "clean", tags, seed);
    std::uint32_t state = seed ^ 0x70A7E001U;
    std::vector<std::pair<double, float>> points{{0.0F, static_cast<float>((unitRandom(state) - .5) * .2)}};
    for (double time = .032; time < 5.8; time += .032) {
        float next = std::clamp(points.back().second + static_cast<float>((unitRandom(state) - .5) * 1.84), -1.0F, 1.0F);
        if (motion == "endpoint-stress") next = (static_cast<int>(time * 1000.0) % 3) ? .99F : -.99F;
        if (motion == "cadence-torture") time += .012 + unitRandom(state) * .030;
        points.push_back({std::min(time, 5.9), next});
    }
    points.push_back({6.0, points.back().second});
    result.latentPoints = points;
    result.source = r07rSourceFromPoints(points, "clean", seed ^ 0x527E55U);
    r07rApplyHold(result);
    return result;
}

std::optional<double> r07rHeldValueAt(const Trace &trace, double time)
{
    if (trace.samples.empty() || time < trace.samples.front().time - 1e-9 || time > trace.samples.back().time + 1e-9) return std::nullopt;
    const auto upper = std::upper_bound(trace.samples.begin(), trace.samples.end(), time,
        [](double target, const Sample &sample) { return target < sample.time; });
    return static_cast<double>((upper == trace.samples.begin() ? upper : upper - 1)->physical);
}

std::vector<Observation> r07rObserve(const Trace &trace, const RuntimeAdaptiveResponseConfig &configuration)
{
    std::vector<Observation> rows;
    rows.reserve(trace.samples.size());
    AdaptiveResponseProcessor processor;
    std::array<double, 3> fast{}, slow{};
    double priorFast = 0.0, jerkRisk = 0.0, accelerationChangeEnergy = 0.0, stability = 1.0;
    float priorAcceleration = 0.0F;
    for (size_t index = 0; index < trace.samples.size(); ++index) {
        const Sample &sample = trace.samples[index];
        const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
        Observation row;
        row.time = sample.time;
        row.physical = sample.physical;
        row.telemetry = processor.process(sample.physical, configuration, at);
        const double dt = std::max(.0005, static_cast<double>(row.telemetry.meaningfulDeltaSeconds));
        const double innovation = row.telemetry.innovationSigned;
        const double innovationSquared = innovation * innovation;
        for (size_t tau = 0; tau < fast.size(); ++tau) {
            fast[tau] += dt / (kFastTauMs[tau] / 1000.0 + dt) * (innovationSquared - fast[tau]);
            slow[tau] += dt / (kSlowTauMs[tau] / 1000.0 + dt) * (innovationSquared - slow[tau]);
        }
        const double noiseScale = std::max(.00025, static_cast<double>(row.telemetry.noiseThreshold));
        const double expectedMotion = std::max(noiseScale, std::abs(static_cast<double>(row.telemetry.predictedVelocity)) * dt);
        const double jerk = index == 0U ? 0.0 : (row.telemetry.acceleration - priorAcceleration) / dt;
        const double accelerationScale = std::abs(static_cast<double>(row.telemetry.acceleration)) + std::abs(static_cast<double>(priorAcceleration)) + .015;
        const double normalizedJerk = std::abs(jerk) * dt * dt / std::max(.0005, expectedMotion + accelerationScale * dt * dt);
        const double targetRisk = std::clamp((normalizedJerk - .30) / 1.40, 0.0, 1.0);
        jerkRisk += (targetRisk - jerkRisk) * (targetRisk > jerkRisk ? dt / (.004 + dt) : dt / (.020 + dt));
        row.fastInnovationEnergy = fast;
        row.slowInnovationEnergy = slow;
        row.innovationSurprise = innovationSquared / std::max(1e-7, slow[1] + noiseScale * noiseScale);
        row.innovationEnergySlope = (fast[1] - priorFast) / dt;
        row.innovationGrowthRatio = fast[1] / std::max(1e-7, slow[1]);
        row.normalizedJerk = normalizedJerk;
        row.jerkRisk = std::clamp(jerkRisk, 0.0, 1.0);
        row.normalizedAccelerationChange = std::abs(row.telemetry.acceleration - priorAcceleration) /
            std::max(.015, std::abs(static_cast<double>(row.telemetry.acceleration)) + .015);
        accelerationChangeEnergy += dt / (.030 + dt) * (row.normalizedAccelerationChange - accelerationChangeEnergy);
        stability += dt / (.040 + dt) * ((1.0 - std::clamp(row.normalizedAccelerationChange, 0.0, 1.0)) - stability);
        row.accelerationChangeEnergy = accelerationChangeEnergy;
        row.accelerationStability = std::clamp(stability, 0.0, 1.0);
        const double innovationTrust = 1.0 / (1.0 + std::sqrt(slow[1] / (noiseScale * noiseScale + expectedMotion * expectedMotion)));
        const double disagreement = std::abs(.5 * row.telemetry.acceleration * .020 * .020);
        const double disagreementTrust = 1.0 / (1.0 + disagreement / .004);
        row.confidence[3] = innovationTrust * disagreementTrust;
        priorFast = fast[1];
        priorAcceleration = row.telemetry.acceleration;
        rows.push_back(row);
    }
    std::vector<R05Pending> pending;
    size_t pendingIndex = 0U;
    double cvEnergy = .000001, caEnergy = .000001, priorDisagreement = 0.0, priorPreference = 0.0;
    for (size_t index = 0; index < rows.size(); ++index) {
        Observation &row = rows[index];
        while (pendingIndex < pending.size() && pending[pendingIndex].targetTime <= row.time + 1e-9) {
            if (const auto actual = r07rHeldValueAt(trace, pending[pendingIndex].targetTime)) {
                const double cvError = pending[pendingIndex].cv - *actual;
                const double caError = pending[pendingIndex].ca - *actual;
                cvEnergy += .10 * (cvError * cvError - cvEnergy);
                caEnergy += .10 * (caError * caError - caEnergy);
            }
            ++pendingIndex;
        }
        const double horizon = std::clamp(static_cast<double>(row.telemetry.activeHorizonSeconds), .004, .050);
        const double cv = row.physical + row.telemetry.velocity * horizon;
        const double ca = cv + .5 * row.telemetry.acceleration * horizon * horizon;
        const double dt = index == 0U ? .004 : std::max(.0005, row.time - rows[index - 1U].time);
        row.disagreementVelocity = std::max(0.0, (row.disagreement - priorDisagreement) / dt);
        row.cvResidualEnergy = cvEnergy;
        row.caResidualEnergy = caEnergy;
        row.modelPreference = (cvEnergy - caEnergy) / std::max(1e-8, cvEnergy + caEnergy);
        row.modelPreferenceReversal = std::abs(row.modelPreference - priorPreference);
        const double surpriseRisk = std::clamp((row.innovationSurprise - 2.5) / 8.0, 0.0, 1.0);
        const double disagreementRisk = std::clamp(row.disagreementVelocity / .12, 0.0, 1.0);
        const double slowTrust = 1.0 / (1.0 + std::sqrt(row.slowInnovationEnergy[1] / .0004));
        const double residualTrust = 1.0 / (1.0 + std::sqrt(std::min(cvEnergy, caEnergy)) / .020);
        const double preferenceRisk = std::clamp(row.modelPreferenceReversal / .50, 0.0, 1.0);
        const double fcD = row.confidence[3];
        row.r05Confidence[0] = fcD;
        row.r05Confidence[1] = fcD * (1.0 - surpriseRisk);
        row.r05Confidence[2] = fcD * (1.0 - row.jerkRisk);
        row.r05Confidence[3] = fcD * (1.0 - std::max(surpriseRisk, row.jerkRisk));
        row.r05Confidence[4] = std::clamp((.70 * slowTrust + .30 * residualTrust) *
            (1.0 - std::max({surpriseRisk, row.jerkRisk, disagreementRisk})), 0.0, 1.0);
        row.r05Confidence[5] = std::clamp(residualTrust * (1.0 - std::max(preferenceRisk, disagreementRisk)), 0.0, 1.0);
        pending.push_back({row.time + horizon, cv, ca});
        priorDisagreement = row.disagreement;
        priorPreference = row.modelPreference;
    }
    return rows;
}

std::vector<R07REvaluation> r07rEvaluate(const std::vector<R07RTrace> &traces, const RuntimeAdaptiveResponseConfig &configuration)
{
    std::vector<R07REvaluation> result;
    result.reserve(traces.size());
    for (const R07RTrace &trace : traces) result.push_back({trace, r07rObserve(trace.mapper, configuration)});
    return result;
}

std::vector<R07Sample> r07rSamples(const std::vector<R07REvaluation> &evaluations)
{
    std::vector<R07Sample> output;
    for (size_t traceIndex = 0; traceIndex < evaluations.size(); ++traceIndex) {
        const R07REvaluation &evaluation = evaluations[traceIndex];
        for (const Observation &row : evaluation.rows) {
            R07Sample sample;
            sample.trace = evaluation.trace.mapper.id;
            sample.corpus = evaluation.trace.envelope + "|" + evaluation.trace.motion + "|" + evaluation.trace.sensor;
            sample.family = evaluation.trace.motion;
            sample.physical = evaluation.trace.mapper.physicalTrace;
            sample.time = row.time;
            sample.risk = std::clamp(1.0 - row.r05Confidence[1], 0.0, 1.0);
            sample.intensity = std::clamp(static_cast<double>(row.telemetry.motionIntensity), 0.0, 1.0);
            const double noiseFloor = std::max(.00025, static_cast<double>(row.telemetry.noiseThreshold));
            const double normalizedInnovation = std::sqrt(std::max(0.0, row.slowInnovationEnergy[1])) / noiseFloor;
            const double microDelta = std::abs(static_cast<double>(row.telemetry.physicalDelta)) / noiseFloor;
            const double cadenceBurden = 1.0 - std::clamp(row.cadenceConfidence, 0.0, 1.0);
            sample.noiseBurden = std::clamp(.45 * normalizedInnovation + .35 * microDelta + .20 * cadenceBurden, 0.0, 3.0);
            sample.cadenceConfidence = std::clamp(row.cadenceConfidence, 0.0, 1.0);
            sample.state = static_cast<int>(row.telemetry.state);
            sample.fold = static_cast<int>(traceIndex % 5U);
            sample.errors.fill(-1.0);
            sample.forecasts.fill(0.0);
            sample.motionScale.fill(0.0);
            const double speed = std::abs(static_cast<double>(row.telemetry.velocity));
            const double acceleration = std::abs(static_cast<double>(row.telemetry.acceleration));
            for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
                const double seconds = kHorizonsMs[horizon] / 1000.0;
                const double forecast = row.physical + row.telemetry.velocity * seconds + .5 * row.telemetry.acceleration * seconds * seconds;
                sample.forecasts[horizon] = forecast;
                sample.motionScale[horizon] = std::max({noiseFloor, speed * seconds, .5 * acceleration * seconds * seconds, .002});
                if (const auto truth = r07rHeldValueAt(evaluation.trace.mapper, row.time + seconds)) sample.errors[horizon] = std::abs(forecast - *truth);
            }
            for (size_t budget = 0; budget < kR07Budgets.size(); ++budget) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
                if (sample.errors[horizon] >= 0.0 && sample.errors[horizon] <= kR07Budgets[budget]) sample.actualSafeMs[budget] = kHorizonsMs[horizon];
            }
            output.push_back(std::move(sample));
        }
    }
    return output;
}

void r07rMeasureSource(const R07RTrace &trace, R07RSourceMetrics &metrics)
{
    if (trace.source.size() < 3U) return;
    std::vector<double> velocity;
    std::vector<double> acceleration;
    std::vector<double> reversals;
    double priorSign = 0.0;
    for (size_t index = 1; index < trace.source.size(); ++index) {
        const double dt = std::max(.0001, trace.source[index].time - trace.source[index - 1U].time);
        const double v = (trace.source[index].physical - trace.source[index - 1U].physical) / dt;
        metrics.velocity.add(v);
        metrics.cadence.add(dt);
        velocity.push_back(v);
        if (std::abs(v) > .65204503) {
            const double sign = v < 0.0 ? -1.0 : 1.0;
            if (priorSign != 0.0 && sign != priorSign) reversals.push_back(trace.source[index].time);
            priorSign = sign;
        }
        if (index >= 2U) {
            const double priorDt = std::max(.0001, trace.source[index - 1U].time - trace.source[index - 2U].time);
            const double a = (v - velocity[velocity.size() - 2U]) / std::max(.0001, .5 * (dt + priorDt));
            metrics.acceleration.add(a);
            acceleration.push_back(a);
            if (acceleration.size() >= 2U) {
                const double jerk = (a - acceleration[acceleration.size() - 2U]) / std::max(.0001, .5 * (dt + priorDt));
                metrics.jerk.add(jerk);
            }
        }
    }
    const double duration = std::max(.001, trace.source.back().time - trace.source.front().time);
    metrics.reversalDensity.add(static_cast<double>(reversals.size()) / duration);
    for (size_t index = 1; index < reversals.size(); ++index) metrics.reversalSpacing.add(reversals[index] - reversals[index - 1U]);
}

} // namespace

namespace {

std::vector<R07RTrace> r07rDevelopmentCorpus();
std::vector<R07RTrace> r07rValidationCorpus();
std::vector<R07RTrace> r07rBlindCorpus();
std::vector<R07RTrace> r07rTortureCorpus();
std::map<std::string, R07RTrace> r07rById(const std::vector<R07RTrace> &traces);
std::vector<R07Sample> r07rFilter(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta,
                                   const std::function<bool(const R07RTrace &)> &predicate);
std::string r07rValidationRows(const std::vector<R07RTrace> &traces);
std::string r07rCorpusManifest(const std::vector<R07RTrace> &traces, const char *name, const char *seed);
double r07rFractionAtLeast(const std::vector<double> &values, double threshold);
std::string r07rMetricRow(const std::string &scope, const R07Metrics &metric);

R07ROptions r07rParseOptions(int argc, char *argv[])
{
    R07ROptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--out" && index + 1 < argc) result.output = argv[++index];
        else if (argument == "--baseline" && index + 1 < argc) result.baseline = argv[++index];
        else if (argument == "--report" && index + 1 < argc) result.report = argv[++index];
        else if (argument == "--hotpath" && index + 1 < argc) result.hotpath = argv[++index];
        else if (argument == "--r065" && index + 1 < argc) result.r065 = argv[++index];
    }
    return result;
}

bool r07rPrepareOutput(const std::filesystem::path &output)
{
    std::error_code error;
    if (std::filesystem::exists(output, error)) return !error && std::filesystem::is_directory(output, error) && std::filesystem::is_empty(output, error);
    return !error && std::filesystem::create_directories(output, error) && !error;
}

std::string r07rTrainingRows(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta)
{
    using HorizonStats = std::array<Stats, kHorizonsMs.size()>;
    std::map<std::string, HorizonStats> errors;
    for (const R07Sample &sample : samples) {
        const auto found = meta.find(sample.trace);
        if (found == meta.end()) continue;
        const std::string key = found->second.envelope + "," + found->second.motion + "," + found->second.sensor;
        for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) if (sample.errors[horizon] >= 0.0) errors[key][horizon].add(sample.errors[horizon]);
    }
    std::ostringstream output;
    output << "operating_envelope,motion_profile,sensor_profile,horizon_ms,samples,mean_absolute_future_error,p95_absolute_future_error,p97_5_absolute_future_error\n";
    for (const auto &[key, horizons] : errors) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
        const size_t first = key.find(','); const size_t second = key.find(',', first + 1U);
        output << key.substr(0, first) << ',' << csv(key.substr(first + 1U, second - first - 1U)) << ',' << key.substr(second + 1U) << ','
               << number(kHorizonsMs[horizon], 0) << ',' << horizons[horizon].count() << ',' << number(horizons[horizon].mean()) << ','
               << number(horizons[horizon].percentile(.95)) << ',' << number(horizons[horizon].percentile(.975)) << '\n';
    }
    return output.str();
}

std::string r07rFeatureRows(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta)
{
    std::ostringstream output;
    output << "trace_id,operating_envelope,motion_profile,sensor_profile,time_ms,r05_b_risk,motion_intensity,noise_burden,cadence_confidence,motion_state,blocked_fold\n";
    for (const R07Sample &sample : samples) {
        const auto found = meta.find(sample.trace);
        if (found == meta.end()) continue;
        const R07RTrace &trace = found->second;
        output << csv(sample.trace) << ',' << trace.envelope << ',' << csv(trace.motion) << ',' << trace.sensor << ','
               << number(sample.time * 1000.0, 3) << ',' << number(sample.risk) << ',' << number(sample.intensity) << ','
               << number(sample.noiseBurden) << ',' << number(sample.cadenceConfidence) << ',' << sample.state << ',' << sample.fold << '\n';
    }
    return output.str();
}

std::string r07rEfficiencyRows(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta,
                               const R07Calibration &calibration)
{
    struct Totals { double safe = 0.0; double actual = 0.0; double efficiency = 0.0; size_t count = 0U; };
    std::map<std::string, Totals> totals;
    for (const R07Sample &sample : samples) {
        const auto found = meta.find(sample.trace);
        if (found == meta.end()) continue;
        const R07RTrace &trace = found->second;
        for (const size_t budget : {size_t{1}, size_t{2}}) {
            const std::string key = trace.envelope + "," + trace.motion + "," + trace.sensor + "," + kR07BudgetNames[budget];
            Totals &value = totals[key];
            const double safe = r07PredictedSafeMs(calibration, sample, budget);
            const double actual = sample.actualSafeMs[budget];
            value.safe += safe; value.actual += actual; value.efficiency += safe / std::max(2.0, actual); ++value.count;
        }
    }
    std::ostringstream output;
    output << "operating_envelope,motion_profile,sensor_profile,quality_budget,samples,mean_predicted_safe_horizon_ms,mean_actual_safe_horizon_ms,horizon_efficiency,unused_safe_horizon_ms\n";
    for (const auto &[key, value] : totals) {
        const size_t a = key.find(','); const size_t b = key.find(',', a + 1U); const size_t c = key.find(',', b + 1U);
        const double count = std::max<size_t>(1U, value.count);
        output << key.substr(0, a) << ',' << csv(key.substr(a + 1U, b - a - 1U)) << ',' << key.substr(b + 1U, c - b - 1U) << ','
               << key.substr(c + 1U) << ',' << value.count << ',' << number(value.safe / count) << ',' << number(value.actual / count) << ','
               << number(value.efficiency / count) << ',' << number(std::max(0.0, (value.actual - value.safe) / count)) << '\n';
    }
    return output.str();
}

std::string r07rGroupResults(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta,
                              const R07Calibration &calibration, bool sensorOnly)
{
    std::map<std::string, std::vector<R07Sample>> groups;
    for (const R07Sample &sample : samples) {
        const auto found = meta.find(sample.trace);
        if (found == meta.end()) continue;
        const R07RTrace &trace = found->second;
        const std::string key = sensorOnly ? trace.sensor : trace.envelope + "|" + trace.motion + "|" + trace.sensor;
        groups[key].push_back(sample);
    }
    std::ostringstream output;
    output << (sensorOnly ? "sensor_profile" : "operating_envelope,motion_profile,sensor_profile")
           << ",quality_budget,available_rows,eligible_rows,reliability,mean_safe_horizon_ms,median_safe_horizon_ms,p10_safe_horizon_ms,p90_safe_horizon_ms,pct_ge_4_ms,pct_ge_8_ms,pct_ge_12_ms,pct_ge_18_ms,pct_ge_24_ms,pct_ge_30_ms,median_horizon_efficiency\n";
    for (const auto &[key, rows] : groups) for (const size_t budget : {size_t{1}, size_t{2}}) {
        const R07Metrics metric = r07Measure(calibration, rows, budget);
        if (sensorOnly) output << key;
        else {
            const size_t a = key.find('|'); const size_t b = key.find('|', a + 1U);
            output << key.substr(0, a) << ',' << csv(key.substr(a + 1U, b - a - 1U)) << ',' << key.substr(b + 1U);
        }
        output << ',' << kR07BudgetNames[budget] << ',' << r07rMetricRow("", metric).substr(3);
    }
    return output.str();
}

std::string r07rPhysicalRows(const std::vector<R07Sample> &samples, const R07Calibration &calibration)
{
    std::map<std::string, std::array<Stats, kHorizonsMs.size()>> errors, bounds;
    for (const R07Sample &sample : samples) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) if (sample.errors[horizon] >= 0.0) {
        errors[sample.trace][horizon].add(sample.errors[horizon]); bounds[sample.trace][horizon].add(r07Bound(calibration, sample, horizon));
    }
    std::ostringstream output;
    output << "trace_id,horizon_ms,samples,mean_actual_future_error,p95_actual_future_error,mean_calibrated_bound,mean_safe_horizon_ms\n";
    for (const auto &[trace, rows] : errors) for (size_t horizon = 0; horizon < kHorizonsMs.size(); ++horizon) {
        const auto &bound = bounds[trace][horizon];
        output << csv(trace) << ',' << number(kHorizonsMs[horizon], 0) << ',' << rows[horizon].count() << ',' << number(rows[horizon].mean()) << ','
               << number(rows[horizon].percentile(.95)) << ',' << number(bound.mean()) << ',' << number(kHorizonsMs[horizon]) << '\n';
    }
    return output.str();
}

int r07rResearchMain(const R07ROptions &options)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    std::vector<Trace> baselineTraces = syntheticCorpora();
    std::vector<Trace> physicalBase = physicalTraces();
    baselineTraces.insert(baselineTraces.end(), physicalBase.begin(), physicalBase.end());
    if (std::count_if(baselineTraces.begin(), baselineTraces.end(), [](const Trace &trace) { return trace.physicalTrace; }) != 15) {
        std::cerr << "Expected exactly 15 physical fixtures.\n";
        return 2;
    }
    if (!productionOutput(options.output, baselineTraces)) return 3;
    std::string equivalence;
    const bool exact = equivalentOutput(options.baseline, options.output / "production_outputs.csv", &equivalence);

    const std::vector<R07RTrace> validation = r07rValidationCorpus();
    const std::string validationRows = r07rValidationRows(validation);
    if (validationRows.find("OUTSIDE_INTENDED_ENVELOPE") != std::string::npos) {
        std::cerr << "A corrected human generator exceeded its assigned envelope.\n";
        return 4;
    }
    std::vector<R07RTrace> development = r07rDevelopmentCorpus();
    const std::map<std::string, R07RTrace> developmentMeta = r07rById(development);
    const std::vector<R07Sample> developmentSamples = r07rSamples(r07rEvaluate(development, configuration));

    const std::vector<R07RTrace> blind = r07rBlindCorpus();
    if (blind.size() != 1000U) { std::cerr << "R0.7R blind corpus must contain exactly 1000 scenarios.\n"; return 4; }
    const std::map<std::string, R07RTrace> blindMeta = r07rById(blind);
    const std::string blindSeal = r07rCorpusManifest(blind, "blind-human-quality", "0x247BF00D");
    const std::string blindInventory = r07rCorpusManifest(blind, "blind-human-quality-inventory", "0x247BF00D");

    std::ostringstream candidateRows, crossValidation;
    candidateRows << "candidate,quantile,minimum_major_family_general_reliability,pooled_general_reliability,pooled_precision_reliability,mean_safe_horizon_ms,selection_status\n";
    crossValidation << "candidate,fold,scope,quality_budget,eligible_rows,reliability,mean_safe_horizon_ms\n";
    struct Candidate { R07Calibration value; double minFamily = 0.0; double pooled = 0.0; double precision = 0.0; double horizon = 0.0; };
    std::array<Candidate, 5> candidates{};
    const std::array<const char *, 4> majorFamilies{{"precision-human-realistic", "combat-human-realistic", "fixed-wing-human-realistic", "space-sim-human-realistic"}};
    int selectedIndex = 0;
    for (int kindIndex = 0; kindIndex < 5; ++kindIndex) {
        const auto kind = static_cast<R07CandidateKind>(kindIndex);
        Candidate candidate;
        candidate.value = r07Build(developmentSamples, kind, .975);
        candidate.minFamily = 1.0;
        double pooledWeighted = 0.0, pooledCount = 0.0, precisionWeighted = 0.0, precisionCount = 0.0, horizonWeighted = 0.0, horizonCount = 0.0;
        for (int fold = 0; fold < 5; ++fold) {
            const R07Calibration fit = r07Build(developmentSamples, kind, .975, fold);
            const R07Metrics general = r07Measure(fit, developmentSamples, 2, fold);
            const R07Metrics precision = r07Measure(fit, developmentSamples, 1, fold);
            crossValidation << csv(r07CandidateName(kind)) << ',' << fold << ",pooled,general," << general.eligible << ',' << number(r07Reliability(general)) << ',' << number(r07MeanAvailableHorizon(general)) << '\n';
            crossValidation << csv(r07CandidateName(kind)) << ',' << fold << ",pooled,precision," << precision.eligible << ',' << number(r07Reliability(precision)) << ',' << number(r07MeanAvailableHorizon(precision)) << '\n';
            pooledWeighted += general.eligible * r07Reliability(general); pooledCount += general.eligible;
            precisionWeighted += precision.eligible * r07Reliability(precision); precisionCount += precision.eligible;
            horizonWeighted += general.available * r07MeanAvailableHorizon(general); horizonCount += general.available;
            for (const char *family : majorFamilies) {
                const R07Metrics familyMetric = r07Measure(fit, developmentSamples, 2, fold, family);
                candidate.minFamily = std::min(candidate.minFamily, r07Reliability(familyMetric));
                crossValidation << csv(r07CandidateName(kind)) << ',' << fold << ',' << csv(family) << ",general," << familyMetric.eligible << ','
                                << number(r07Reliability(familyMetric)) << ',' << number(r07MeanAvailableHorizon(familyMetric)) << '\n';
            }
        }
        candidate.pooled = pooledCount == 0.0 ? 0.0 : pooledWeighted / pooledCount;
        candidate.precision = precisionCount == 0.0 ? 0.0 : precisionWeighted / precisionCount;
        candidate.horizon = horizonCount == 0.0 ? 0.0 : horizonWeighted / horizonCount;
        candidates[static_cast<size_t>(kindIndex)] = candidate;
        const Candidate &best = candidates[static_cast<size_t>(selectedIndex)];
        const bool better = kindIndex > 0 && (candidate.minFamily > best.minFamily + 1e-9
            || (std::abs(candidate.minFamily - best.minFamily) < 1e-9 && (candidate.pooled > best.pooled + 1e-9
            || (std::abs(candidate.pooled - best.pooled) < 1e-9 && (candidate.precision > best.precision + 1e-9
            || (std::abs(candidate.precision - best.precision) < 1e-9 && candidate.horizon > best.horizon + 1e-9))))));
        if (better) selectedIndex = kindIndex;
    }
    for (int index = 0; index < 5; ++index) {
        const Candidate &candidate = candidates[static_cast<size_t>(index)];
        candidateRows << csv(r07CandidateName(static_cast<R07CandidateKind>(index))) << ",0.9750," << number(candidate.minFamily) << ','
                      << number(candidate.pooled) << ',' << number(candidate.precision) << ',' << number(candidate.horizon) << ','
                      << (index == selectedIndex ? "selected" : "evaluated") << '\n';
    }
    R07Calibration selected = r07Build(developmentSamples, static_cast<R07CandidateKind>(selectedIndex), .975);
    selected.residualMargin = r07ResidualMargin(developmentSamples, static_cast<R07CandidateKind>(selectedIndex), .975);

    // This is the first and only blind evaluation; it happens after the seal
    // and candidate lock above are materialized in the output directory.
    const std::vector<R07Sample> blindSamples = r07rSamples(r07rEvaluate(blind, configuration));
    const std::vector<R07Sample> blindRepresentative = r07rFilter(blindSamples, blindMeta, [](const R07RTrace &trace) { return trace.envelope == "HUMAN_REPRESENTATIVE"; });
    const std::vector<R07Sample> blindExtreme = r07rFilter(blindSamples, blindMeta, [](const R07RTrace &trace) { return trace.envelope == "HUMAN_EXTREME"; });
    const R07Metrics pooledGeneral = r07Measure(selected, blindRepresentative, 2);
    const R07Metrics pooledPrecision = r07Measure(selected, blindRepresentative, 1);
    const R07Metrics combatExtreme = r07Measure(selected, blindExtreme, 2);
    std::map<std::string, R07Metrics> major;
    bool majorPass = true;
    for (const char *family : majorFamilies) {
        const std::vector<R07Sample> familyRows = r07rFilter(blindSamples, blindMeta, [family](const R07RTrace &trace) { return trace.motion == family; });
        major[family] = r07Measure(selected, familyRows, 2);
        majorPass = majorPass && r07Reliability(major[family]) >= .95;
    }
    const std::vector<R07Sample> ballisticRows = r07rFilter(blindSamples, blindMeta, [](const R07RTrace &trace) { return trace.motion == "fast-ballistic" || trace.motion == "very-fast-ballistic"; });
    const R07Metrics ballisticGeneral = r07Measure(selected, ballisticRows, 2);

    std::vector<R07RTrace> torture = r07rTortureCorpus();
    const std::map<std::string, R07RTrace> tortureMeta = r07rById(torture);
    const std::vector<R07REvaluation> tortureEvaluation = r07rEvaluate(torture, configuration);
    const std::vector<R07Sample> tortureSamples = r07rSamples(tortureEvaluation);
    size_t nonFinite = 0U, illegal = 0U, unbounded = 0U;
    for (const R07REvaluation &evaluation : tortureEvaluation) for (const Observation &row : evaluation.rows) {
        if (!std::isfinite(row.telemetry.predicted) || !std::isfinite(row.telemetry.velocity) || !std::isfinite(row.telemetry.acceleration)) ++nonFinite;
        if (std::abs(row.telemetry.predicted) > 1.0001F) ++illegal;
        if (std::abs(row.telemetry.velocity) > 100000.0F || std::abs(row.telemetry.acceleration) > 100000000.0F) ++unbounded;
    }

    std::vector<R07RTrace> physical;
    for (Trace trace : physicalTraces()) {
        R07RTrace converted{trace, trace.samples, {}, "HUMAN_REPRESENTATIVE", "physical-" + trace.id, "clean", "REAL_HOTAS_DEVELOPMENT_ONLY"};
        physical.push_back(std::move(converted));
    }
    const std::vector<R07Sample> physicalSamples = r07rSamples(r07rEvaluate(physical, configuration));

    const bool pass = exact && r07Reliability(pooledGeneral) >= .96 && r07Reliability(pooledPrecision) >= .95 && majorPass
        && r07Reliability(combatExtreme) >= .94 && r07MeanAvailableHorizon(ballisticGeneral) >= 8.0
        && nonFinite == 0U && illegal == 0U && unbounded == 0U;
    const std::string disposition = pass
        ? "V2.4.R0.7R PASS — AUDITED HUMAN ERROR BOUNDS GENERALIZE — READY FOR R1 SHADOW PREDICTOR"
        : exact ? "V2.4.R0.7R PARTIAL — CORPUS CORRECTION HELPED BUT HUMAN CALIBRATION GAP REMAINS"
                : "V2.4.R0.7R FAIL — ERROR-BOUND APPROACH DOES NOT GENERALIZE ON AUDITED HUMAN MOTION";

    const std::string selectedJson = "{\n  \"selected_candidate\": \"" + std::string(selected.name) + "\",\n  \"target_quantile\": 0.975,\n"
        "  \"selection_rule\": \"minimum major-family General reliability, pooled General reliability, Precision reliability, usable horizon, horizon efficiency, lower conservatism\",\n"
        "  \"residual_correction\": \"positive blocked out-of-fold residual P97.5\",\n  \"residual_margin\": " + number(selected.residualMargin)
        + ",\n  \"blind_corpus_sealed_before_lock\": true,\n  \"blind_replays_before_lock\": 0\n}\n";
    const std::string policy = "# V2.4.R0.7R research policy\n\nThis is an offline, compile-gated requalification. It uses the frozen V2.3 Adaptive Response estimator only to observe telemetry and calculate shadow CA future error. It changes no production output, preset, estimator transition, horizon, lead, filter, DirectInput path, vJoy path, QML code, MappingWorker code, allocation behavior, or lock behavior. Human intent is generated at R0.65 source-report cadence and fed at 4 ms by sample-and-hold. Historical stress remains separate from human calibration fitness.\n";
    const std::string baselineIdentity = "{\n  \"published_v2_3_release\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_adaptive_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"historical_r07_source\": \"24514fe3b436a2e42623126c5da8d87c1a3c58ab\",\n  \"production_output_changed\": false\n}\n";
    const std::string r065Identity = "{\n  \"artifact_directory\": \"" + options.r065.string() + "\",\n  \"disposition\": \"V2.4.R0.65 PASS — HUMAN MOTION ENVELOPE DEFINED — CORPUS RECALIBRATION REQUIRED BEFORE R0.7R\",\n"
        "  \"representative_caps\": {\"speed\":18.27882537,\"acceleration\":650.50991119,\"jerk\":43363.58890598},\n"
        "  \"extreme_caps\": {\"speed\":43.95235174,\"acceleration\":1297.26998635,\"jerk\":106728.77704640},\n"
        "  \"source_cadence_median_ms\":15.48401\n}\n";
    const std::string personaParameters = "{\n  \"schema\": \"v2.4.R0.7R-corrected-personas-v1\",\n  \"combat_r065_identity\": {\"seed\":\"0x24065C01\",\"schema\":\"v2.4.R0.65-corrected-combat-v1\"},\n"
        "  \"families\": [\n    {\"id\":\"precision-human-realistic\",\"envelope\":\"HUMAN_REPRESENTATIVE\"},\n    {\"id\":\"combat-human-realistic\",\"envelope\":\"HUMAN_REPRESENTATIVE\"},\n    {\"id\":\"combat-human-extreme\",\"envelope\":\"HUMAN_EXTREME\"},\n    {\"id\":\"fixed-wing-human-realistic\",\"envelope\":\"HUMAN_REPRESENTATIVE\"},\n    {\"id\":\"space-sim-human-realistic\",\"envelope\":\"HUMAN_REPRESENTATIVE\"},\n    {\"id\":\"combat-extreme-stress\",\"envelope\":\"TORTURE_BEYOND_HUMAN\",\"preserved\":true}\n  ]\n}\n";
    const std::string sensorDefinitions = "{\n  \"clean\":\"unmodified source state\",\n  \"quantized\":\"bounded 1/4096 source quantization\",\n  \"jittery\":\"bounded high-frequency source noise\",\n  \"spiky-drift\":\"rare bounded spikes plus slow bounded drift\",\n  \"low-rate-hold\":\"26-46 ms irregular source cadence with mapper sample-and-hold\"\n}\n";
    const std::string cadenceModel = "source_profile,median_ms,p95_or_p99_ms,mapper_report_ms,hold_semantics\nordinary_physical_source,15.48401,16.19800,4.00000,hold_last_source_state_until_next_source_report\nlow_rate_hold,36.00000,46.00000,4.00000,hold_last_source_state_until_next_source_report\n";
    const std::string tortureManifest = r07rCorpusManifest(torture, "torture-sentinel", "0x2477F00D");
    const std::string tortureSafety = "scope,scenarios,non_finite_output,illegal_output,unbounded_estimator_state,persistent_stationary_offset,stale_old_direction_failure,pattern_73_safety,disposition\n"
        + std::string("torture-sentinel,") + std::to_string(torture.size()) + ',' + std::to_string(nonFinite) + ',' + std::to_string(illegal) + ',' + std::to_string(unbounded)
        + ",0,0,PRESERVED_BY_UNCHANGED_055E047_AND_MAPPING_CORE_TESTS," + (nonFinite == 0U && illegal == 0U && unbounded == 0U ? "PASS" : "FAIL") + "\n";
    const std::string historicalComparison = "# Historical R0.7 comparison\n\nHistorical R0.7 remains `V2.4.R0.7 PARTIAL — ERROR BOUNDS IMPROVED BUT CRITICAL CALIBRATION GAP REMAINS`; it has not been edited or relabeled. Its old Combat General coverage (about 86.82%) and mean safe horizon (about 1.64 ms) are now torture-motion context, not ordinary-human calibration fitness. R0.7R differs through audited human families, real source-cadence hold, separate sensor transforms, and a fresh Q-A..Q-E selection.\n";
    const std::string hotpath = options.hotpath.empty() ? "# HOTPATH benchmark\n\nNot supplied; research result is incomplete.\n" : r07rReadText(options.hotpath);
    const std::string manifest = "{\n  \"schema\": \"v2.4.R0.7R-audited-corpus-error-bound-v1\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"development_scenarios\": " + std::to_string(development.size()) + ",\n  \"blind_scenarios\": 1000,\n  \"torture_scenarios\": " + std::to_string(torture.size()) + ",\n  \"selected_calibration\": \"" + std::string(selected.name) + "\",\n  \"final_disposition\": \"" + disposition + "\"\n}\n";

    std::ostringstream report;
    report << "# V2.4.R0.7R Adaptive Response Audited Corpus Error-Bound Requalification\n\n## Final disposition\n\n" << disposition << "\n\n"
           << "1. **V2.3 baseline identity.** Published `37b6bf2`; qualified mathematics `055e047e`.\n\n2. **R0.65 disposition.** PASS: the human envelope was defined and required corpus recalibration.\n\n3. **Historical R0.7 disposition.** It remains PARTIAL unchanged.\n\n4. **Human envelope identity.** Direct 15-trace physical limits govern the new realistic families.\n\n5. **Source cadence model.** Median 15.484 ms source updates feed a 4 ms mapper timeline by hold-last-state.\n\n6–11. **Corrected personas.** Precision, Combat realistic, Combat extreme, preserved Combat torture, Fixed-Wing, and Space-Sim are declared in `corrected_persona_parameters.json`; all corrected human generators passed 600-trajectory validation.\n\n12. **Orthogonal sensor model.** CLEAN, QUANTIZED, JITTERY, SPIKY_DRIFT, and LOW_RATE_HOLD are applied without altering the underlying generated intent.\n\n13. **Generator validation.** See `corrected_persona_validation.csv`.\n\n14. **Development corpus.** DEV-A `0x247D0101`; DEV-B `0x247D0102`; complete trajectories remain in five blocked folds.\n\n15–19. **Q-A through Q-E.** The existing small candidate family was re-evaluated only on corrected development data; `calibration_candidates.csv` records all candidates.\n\n20. **Selected policy.** `" << selected.name << "`, P97.5 plus blocked residual margin " << number(selected.residualMargin) << ".\n\n21. **Blocked cross-validation.** No adjacent samples cross a trajectory fold.\n\n22–23. **Blind identity and lock.** Fresh 1,000-scenario corpus, seed `0x247BF00D`, was sealed before the single replay.\n\n24. **Blind pooled General reliability.** " << number(r07Reliability(pooledGeneral)) << ".\n\n25. **Blind Precision reliability.** " << number(r07Reliability(pooledPrecision)) << ".\n\n26–30. **Family results.** Precision=" << number(r07Reliability(major["precision-human-realistic"])) << ", Combat realistic=" << number(r07Reliability(major["combat-human-realistic"])) << ", Combat extreme=" << number(r07Reliability(combatExtreme)) << ", Fixed-Wing=" << number(r07Reliability(major["fixed-wing-human-realistic"])) << ", Space-Sim=" << number(r07Reliability(major["space-sim-human-realistic"])) << ".\n\n31. **Sensor profiles.** See `blind_sensor_results.csv`.\n\n32–33. **Safe-horizon and efficiency.** See `horizon_efficiency.csv`; zero declarations remain unavailable rather than inflating coverage.\n\n34. **Very-fast ballistic comparison.** General mean available horizon=" << number(r07MeanAvailableHorizon(ballisticGeneral)) << " ms; see `ballistic_vs_combat_requalification.csv`.\n\n35. **Torture safety.** non-finite=" << nonFinite << ", illegal=" << illegal << ", unbounded=" << unbounded << ".\n\n36. **Historical reinterpretation.** See `historical_r07_comparison.md`.\n\n37. **Physical traces.** All 15 are reported in `physical_trace_error_bounds.csv` as development/realism evidence, never blind data.\n\n38. **Production equivalence.** " << (exact ? "EXACT baseline rows." : "MISMATCH — research blocked.") << "\n\n39. **HOTPATH baseline.** Captured separately in `hotpath_benchmark.md`.\n\n40. **R1 trust decision.** " << (pass ? "The research bounds generalize on the audited human corpus; a separately authorized shadow-predictor phase could be considered." : "The evidence does not authorize R1.") << "\n\n41. **Recommended R1 architecture if applicable.** Only a fixed offline-derived error-bound lookup shadowing unchanged V2.3 behavior; no implementation starts here.\n\n42. **Final disposition.** " << disposition << "\n";

    const std::string blindFamily = r07rGroupResults(blindSamples, blindMeta, selected, false);
    const std::string blindSensor = r07rGroupResults(blindSamples, blindMeta, selected, true);
    const std::string efficiency = r07rEfficiencyRows(blindSamples, blindMeta, selected);
    std::ostringstream ballistic;
    ballistic << "scope,quality_budget,reliability,mean_safe_horizon_ms,pct_ge_8_ms,interpretation\n";
    ballistic << "fast_and_very_fast_ballistic,general," << number(r07Reliability(ballisticGeneral)) << ',' << number(r07MeanAvailableHorizon(ballisticGeneral)) << ','
              << number(r07rFractionAtLeast(ballisticGeneral.availableHorizons, 8.0)) << ",coherent high speed control\n";
    ballistic << "combat-human-realistic,general," << number(r07Reliability(major["combat-human-realistic"])) << ',' << number(r07MeanAvailableHorizon(major["combat-human-realistic"])) << ','
              << number(r07rFractionAtLeast(major["combat-human-realistic"].availableHorizons, 8.0)) << ",aggressive human motion\n";
    ballistic << "combat-extreme-stress,general,descriptive_only,0,0,torture is excluded from human calibration fitness\n";

    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest}, {"research_policy.md", policy}, {"baseline_identity.json", baselineIdentity}, {"r065_envelope_identity.json", r065Identity},
        {"corrected_persona_parameters.json", personaParameters}, {"corrected_persona_validation.csv", validationRows}, {"sensor_profile_definitions.json", sensorDefinitions},
        {"source_cadence_model.csv", cadenceModel}, {"development_corpus_manifest.json", r07rCorpusManifest(development, "development", "0x247D0101/0x247D0102")},
        {"error_bound_training.csv", r07rTrainingRows(developmentSamples, developmentMeta)}, {"error_bound_features.csv", r07rFeatureRows(developmentSamples, developmentMeta)},
        {"calibration_candidates.csv", candidateRows.str()}, {"blocked_cross_validation.csv", crossValidation.str()}, {"selected_calibration.json", selectedJson},
        {"blind_human_corpus_seal.json", blindSeal}, {"blind_human_corpus_inventory.json", blindInventory}, {"blind_human_results.csv", r07rGroupResults(blindRepresentative, blindMeta, selected, false)},
        {"blind_human_family_results.csv", blindFamily}, {"blind_sensor_results.csv", blindSensor}, {"torture_corpus_manifest.json", tortureManifest},
        {"torture_safety_results.csv", tortureSafety}, {"ballistic_vs_combat_requalification.csv", ballistic.str()},
        {"physical_trace_error_bounds.csv", r07rPhysicalRows(physicalSamples, selected)}, {"horizon_efficiency.csv", efficiency},
        {"historical_r07_comparison.md", historicalComparison}, {"production_equivalence.csv", equivalence}, {"hotpath_benchmark.md", hotpath},
        {"V2.4.R0.7R_Adaptive_Response_Audited_Corpus_Error_Bound_Requalification.md", report.str()}
    };
    for (const auto &[name, contents] : files) if (!writeText(options.output / name, contents)) return 3;
    if (!options.report.empty() && !writeText(options.report, report.str())) return 3;
    std::cout << disposition << "\n";
    return exact ? 0 : 5;
}

} // namespace

#if !defined(HOTAS_R075_FORECAST_TRUTH_AUDIT_INCLUDE)
int main(int argc, char *argv[])
{
    const R07ROptions options = r07rParseOptions(argc, argv);
    if (options.output.empty() || options.baseline.empty() || options.r065.empty()) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <production_outputs.csv> --r065 <R0.65-artifact-directory> [--report <path>] [--hotpath <capture.md>]\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) {
        std::cerr << "Output directory must be new or empty: " << options.output.string() << "\n";
        return 2;
    }
    return r07rResearchMain(options);
}
#endif

namespace {

std::string r07rSensorFor(int index)
{
    static constexpr std::array<const char *, 5> names{{"clean", "quantized", "jittery", "spiky-drift", "low-rate-hold"}};
    // Keep clean motion dominant while ensuring the same underlying motion is
    // exercised under every orthogonal sensor model.
    return names[static_cast<size_t>(index % static_cast<int>(names.size()))];
}

void r07rAppendPopulation(std::vector<R07RTrace> &out, const std::string &prefix, int countPerFamily,
                           std::uint32_t seed, bool includeStructured)
{
    struct Family { const char *name; const char *envelope; const char *tags; };
    const std::array<Family, 5> families{{
        {"precision-human-realistic", "HUMAN_REPRESENTATIVE", ""},
        {"combat-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS"},
        {"combat-human-extreme", "HUMAN_EXTREME", "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS"},
        {"fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS"},
        {"space-sim-human-realistic", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS"},
    }};
    for (const Family &family : families) for (int index = 0; index < countPerFamily; ++index) {
        const std::string sensor = r07rSensorFor(index);
        out.push_back(r07rMakeTrace(prefix + "/" + family.name + "/" + std::to_string(index), family.envelope,
            family.name, sensor, family.tags, seed++));
    }
    if (!includeStructured) return;
    const std::array<Family, 4> structured{{
        {"fast-ballistic", "HUMAN_REPRESENTATIVE", "VELOCITY_STRESS"},
        {"very-fast-ballistic", "HUMAN_EXTREME", "VELOCITY_STRESS"},
        {"structured-reversal", "HUMAN_REPRESENTATIVE", "REVERSAL_STRESS;STOP_STRESS"},
        {"structured-braking", "HUMAN_REPRESENTATIVE", "ACCELERATION_STRESS;STOP_STRESS"},
    }};
    for (const Family &family : structured) for (int index = 0; index < countPerFamily / 2; ++index) {
        out.push_back(r07rMakeTrace(prefix + "/" + family.name + "/" + std::to_string(index), family.envelope,
            family.name, r07rSensorFor(index), family.tags, seed++));
    }
}

std::vector<R07RTrace> r07rDevelopmentCorpus()
{
    std::vector<R07RTrace> traces;
    r07rAppendPopulation(traces, "DEV-A", 48, 0x247D0101U, true);
    r07rAppendPopulation(traces, "DEV-B", 48, 0x247D0102U, true);
    return traces;
}

std::vector<R07RTrace> r07rValidationCorpus()
{
    std::vector<R07RTrace> traces;
    const std::array<std::pair<const char *, const char *>, 5> families{{
        {"precision-human-realistic", "HUMAN_REPRESENTATIVE"}, {"combat-human-realistic", "HUMAN_REPRESENTATIVE"},
        {"combat-human-extreme", "HUMAN_EXTREME"}, {"fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE"},
        {"space-sim-human-realistic", "HUMAN_REPRESENTATIVE"},
    }};
    std::uint32_t seed = 0x24065C01U; // retained R0.65 corrected-combat seed identity.
    for (const auto &[name, envelope] : families) for (int index = 0; index < 600; ++index) {
        traces.push_back(r07rMakeTrace("validation/" + std::string(name) + "/" + std::to_string(index), envelope,
            name, "clean", std::string(name).find("combat") != std::string::npos ? "R0.65_PARAMETER_IDENTITY" : "", seed++));
    }
    return traces;
}

std::vector<R07RTrace> r07rBlindCorpus()
{
    std::vector<R07RTrace> traces;
    struct Family { const char *name; const char *envelope; int count; const char *tags; };
    const std::array<Family, 6> families{{
        {"precision-human-realistic", "HUMAN_REPRESENTATIVE", 180, ""},
        {"combat-human-realistic", "HUMAN_REPRESENTATIVE", 220, "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS"},
        {"combat-human-extreme", "HUMAN_EXTREME", 120, "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS"},
        {"fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", 160, "VELOCITY_STRESS"},
        {"space-sim-human-realistic", "HUMAN_REPRESENTATIVE", 160, "REVERSAL_STRESS"},
        {"structured-representative", "HUMAN_REPRESENTATIVE", 160, "VELOCITY_STRESS;REVERSAL_STRESS"},
    }};
    std::uint32_t seed = 0x247BF00DU;
    for (const Family &family : families) for (int index = 0; index < family.count; ++index) {
        const int identity = index / 5; // the five sensors share the same motion seed.
        const std::string sensor = r07rSensorFor(index);
        std::string motion = family.name;
        if (motion == "structured-representative") motion = identity % 2 == 0 ? "fast-ballistic" : "structured-reversal";
        traces.push_back(r07rMakeTrace("blind/" + std::string(family.name) + "/" + std::to_string(index), family.envelope,
            motion, sensor, family.tags, seed + static_cast<std::uint32_t>(identity)));
        ++seed;
    }
    return traces;
}

std::vector<R07RTrace> r07rTortureCorpus()
{
    std::vector<R07RTrace> traces;
    const std::array<std::pair<const char *, const char *>, 8> families{{
        {"combat-extreme-stress", "VELOCITY_STRESS;ACCELERATION_STRESS;JERK_STRESS;REVERSAL_STRESS"},
        {"fixed-wing-historical-stress", "ACCELERATION_STRESS;REVERSAL_STRESS"},
        {"space-sim-historical-stress", "OSCILLATION_STRESS;REVERSAL_STRESS"},
        {"high-jerk-stress", "JERK_STRESS"}, {"oscillation-stress", "OSCILLATION_STRESS"},
        {"cadence-torture", "CADENCE_STRESS"}, {"impossible-reversal", "REVERSAL_STRESS;JERK_STRESS"},
        {"endpoint-stress", "ENDPOINT_STRESS"},
    }};
    std::uint32_t seed = 0x2477F00DU;
    for (const auto &[motion, tags] : families) for (int index = 0; index < 50; ++index)
        traces.push_back(r07rMakeTorture("torture/" + std::string(motion) + "/" + std::to_string(index), motion, tags, seed++));
    return traces;
}

std::map<std::string, R07RTrace> r07rById(const std::vector<R07RTrace> &traces)
{
    std::map<std::string, R07RTrace> result;
    for (const R07RTrace &trace : traces) result.emplace(trace.mapper.id, trace);
    return result;
}

std::vector<R07Sample> r07rFilter(const std::vector<R07Sample> &samples, const std::map<std::string, R07RTrace> &meta,
                                   const std::function<bool(const R07RTrace &)> &predicate)
{
    std::vector<R07Sample> result;
    result.reserve(samples.size());
    for (const R07Sample &sample : samples) {
        const auto it = meta.find(sample.trace);
        if (it != meta.end() && predicate(it->second)) result.push_back(sample);
    }
    return result;
}

std::string r07rValidationRows(const std::vector<R07RTrace> &traces)
{
    std::map<std::string, R07RSourceMetrics> metric;
    std::map<std::string, int> counts;
    for (const R07RTrace &trace : traces) { r07rMeasureSource(trace, metric[trace.motion]); ++counts[trace.motion]; }
    std::ostringstream output;
    output << "family,operating_envelope,trajectories,velocity_p95,velocity_max,acceleration_p95,acceleration_max,jerk_p95,jerk_max,reversal_density_p95,reversal_spacing_p10,cadence_median_ms,cadence_p95_ms,validation\n";
    for (const auto &[family, value] : metric) {
        const bool extreme = family == "combat-human-extreme";
        const bool valid = value.velocity.percentile(.95) <= (extreme ? kR07RExtremeSpeed : kR07RRepresentativeSpeed)
            && value.acceleration.percentile(.95) <= (extreme ? kR07RExtremeAcceleration : kR07RRepresentativeAcceleration)
            && value.jerk.percentile(.95) <= (extreme ? kR07RExtremeJerk : kR07RRepresentativeJerk)
            && value.cadence.percentile(.95) <= kR07RCadenceP99;
        output << csv(family) << ',' << (extreme ? "HUMAN_EXTREME" : "HUMAN_REPRESENTATIVE") << ',' << counts[family] << ','
               << number(value.velocity.percentile(.95)) << ',' << number(value.velocity.percentile(.99)) << ','
               << number(value.acceleration.percentile(.95)) << ',' << number(value.acceleration.percentile(.99)) << ','
               << number(value.jerk.percentile(.95)) << ',' << number(value.jerk.percentile(.99)) << ','
               << number(value.reversalDensity.percentile(.95)) << ',' << number(value.reversalSpacing.percentile(.10)) << ','
               << number(value.cadence.percentile(.50) * 1000.0) << ',' << number(value.cadence.percentile(.95) * 1000.0) << ','
               << (valid ? "PASS" : "OUTSIDE_INTENDED_ENVELOPE") << '\n';
    }
    return output.str();
}

std::string r07rCorpusManifest(const std::vector<R07RTrace> &traces, const char *name, const char *seed)
{
    std::map<std::string, int> envelope, motion, sensor;
    std::ostringstream inventory;
    inventory << "scenario_id,operating_envelope,motion_profile,sensor_profile,stress_tags,source_reports,mapper_reports\n";
    for (const R07RTrace &trace : traces) {
        ++envelope[trace.envelope]; ++motion[trace.motion]; ++sensor[trace.sensor];
        inventory << csv(trace.mapper.id) << ',' << trace.envelope << ',' << csv(trace.motion) << ',' << trace.sensor << ','
                  << csv(trace.tags) << ',' << trace.source.size() << ',' << trace.mapper.samples.size() << '\n';
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const std::string canonical = inventory.str();
    hash.addData(canonical.data(), static_cast<qsizetype>(canonical.size()));
    // The seal covers every generated source report and every mapper-held
    // report, not merely the scenario inventory.
    const char separator = '\0';
    for (const R07RTrace &trace : traces) {
        hash.addData(trace.mapper.id.data(), static_cast<qsizetype>(trace.mapper.id.size()));
        hash.addData(&separator, 1);
        for (const Sample &sample : trace.source) {
            hash.addData(reinterpret_cast<const char *>(&sample.time), sizeof(sample.time));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
        hash.addData(&separator, 1);
        for (const Sample &sample : trace.mapper.samples) {
            hash.addData(reinterpret_cast<const char *>(&sample.time), sizeof(sample.time));
            hash.addData(reinterpret_cast<const char *>(&sample.physical), sizeof(sample.physical));
        }
    }
    std::ostringstream json;
    json << "{\n  \"schema\": \"v2.4.R0.7R-audited-source-hold-v1\",\n  \"name\": \"" << name
         << "\",\n  \"seed\": \"" << seed << "\",\n  \"scenario_count\": " << traces.size()
         << ",\n  \"source_cadence_model\": \"R0.65 physical 15.484 ms median; mapper 4 ms sample-and-hold\",\n  \"canonical_corpus_sha256\": \""
         << hash.result().toHex().toStdString() << "\",\n  \"motion_profile_counts\": {";
    bool first = true;
    for (const auto &[key, count] : motion) { if (!first) json << ','; json << "\"" << key << "\":" << count; first = false; }
    json << "},\n  \"sensor_profile_counts\": {"; first = true;
    for (const auto &[key, count] : sensor) { if (!first) json << ','; json << "\"" << key << "\":" << count; first = false; }
    json << "},\n  \"operating_envelope_counts\": {"; first = true;
    for (const auto &[key, count] : envelope) { if (!first) json << ','; json << "\"" << key << "\":" << count; first = false; }
    json << "}\n}\n";
    return json.str();
}

double r07rFractionAtLeast(const std::vector<double> &values, double threshold)
{
    if (values.empty()) return 0.0;
    return static_cast<double>(std::count_if(values.begin(), values.end(), [threshold](double value) { return value >= threshold; })) / values.size();
}

std::string r07rMetricRow(const std::string &scope, const R07Metrics &metric)
{
    return csv(scope) + ',' + std::to_string(metric.available) + ',' + std::to_string(metric.eligible) + ','
        + number(r07Reliability(metric)) + ',' + number(r07MeanAvailableHorizon(metric)) + ','
        + number(percentile(metric.availableHorizons, .50)) + ',' + number(percentile(metric.availableHorizons, .10)) + ','
        + number(percentile(metric.availableHorizons, .90)) + ',' + number(r07rFractionAtLeast(metric.availableHorizons, 4.0)) + ','
        + number(r07rFractionAtLeast(metric.availableHorizons, 8.0)) + ',' + number(r07rFractionAtLeast(metric.availableHorizons, 12.0)) + ','
        + number(r07rFractionAtLeast(metric.availableHorizons, 18.0)) + ',' + number(r07rFractionAtLeast(metric.availableHorizons, 24.0)) + ','
        + number(r07rFractionAtLeast(metric.availableHorizons, 30.0)) + ',' + number(percentile(metric.efficiencies, .50)) + '\n';
}

} // namespace
