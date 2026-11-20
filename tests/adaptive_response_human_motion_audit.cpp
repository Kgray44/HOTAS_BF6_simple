// V2.4.R0.65 is a corpus and experiment-design audit only.  This translation
// unit intentionally uses the C++ standard library only: it cannot link the
// mapper, open DirectInput, touch vJoy, or alter production output.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr double kEpsilon = 1e-9;

struct Sample {
    double time{}; // seconds, original source-report timestamp
    double position{};
};

struct Trace {
    std::string id;
    std::string family;
    std::string generator;
    bool physical{};
    std::vector<Sample> samples;
    double endTime{};
};

struct Series {
    std::vector<double> values;

    void add(double value)
    {
        if (std::isfinite(value)) values.push_back(value);
    }
    [[nodiscard]] size_t count() const { return values.size(); }
    [[nodiscard]] double mean() const
    {
        return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    }
    [[nodiscard]] double q(double fraction) const
    {
        if (values.empty()) return 0.0;
        std::vector<double> copy = values;
        const size_t index = static_cast<size_t>(std::floor(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(copy.size() - 1U)));
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
    [[nodiscard]] double maximum() const
    {
        return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    }
    [[nodiscard]] double fractionAbove(double threshold) const
    {
        if (values.empty()) return 0.0;
        return static_cast<double>(std::count_if(values.begin(), values.end(), [threshold](double v) { return v > threshold; }))
            / static_cast<double>(values.size());
    }
};

std::string number(double value, int precision = 8)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

std::string csv(std::string value)
{
    size_t cursor = 0;
    while ((cursor = value.find('"', cursor)) != std::string::npos) {
        value.insert(cursor, 1, '"');
        cursor += 2;
    }
    return '"' + value + '"';
}

bool writeText(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Unable to write " << path.string() << "\n";
        return false;
    }
    file << contents;
    return static_cast<bool>(file);
}

std::vector<std::string> splitCsv(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field.push_back(c); ++i; }
            else quoted = !quoted;
        } else if (c == ',' && !quoted) {
            fields.push_back(field); field.clear();
        } else field.push_back(c);
    }
    fields.push_back(field);
    return fields;
}

std::optional<size_t> column(const std::vector<std::string> &header, const std::string &name)
{
    const auto it = std::find(header.begin(), header.end(), name);
    if (it == header.end()) return std::nullopt;
    return static_cast<size_t>(std::distance(header.begin(), it));
}

double asNumber(const std::string &field)
{
    try { return std::stod(field); } catch (...) { return 0.0; }
}

std::uint32_t nextRandom(std::uint32_t &state)
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

double randomUnit(std::uint32_t &state)
{
    return static_cast<double>(nextRandom(state)) / 4294967295.0;
}

double clamp(double value, double low, double high)
{
    return std::clamp(value, low, high);
}

double interpolate(const std::vector<std::pair<double, double>> &points, double time)
{
    if (points.empty()) return 0.0;
    if (time <= points.front().first) return points.front().second;
    if (time >= points.back().first) return points.back().second;
    const auto right = std::lower_bound(points.begin(), points.end(), time,
        [](const auto &point, double target) { return point.first < target; });
    const auto left = right - 1;
    const double fraction = (time - left->first) / std::max(kEpsilon, right->first - left->first);
    return left->second + fraction * (right->second - left->second);
}

Trace makeSynthetic(const std::string &id, const std::string &family, const std::string &generator,
                    const std::vector<std::pair<double, double>> &points, std::uint32_t seed,
                    int sourceRate, bool variableCadence = false, double noise = 0.0)
{
    Trace trace{id, family, generator, false, {}, 0.0};
    const double finish = points.empty() ? 1.0 : points.back().first;
    std::int64_t priorBucket = -1;
    double time = 0.0;
    while (time <= finish + kEpsilon) {
        const std::int64_t bucket = static_cast<std::int64_t>(std::floor(time * static_cast<double>(sourceRate) + 1e-7));
        if (bucket != priorBucket) {
            priorBucket = bucket;
            double position = interpolate(points, static_cast<double>(bucket) / static_cast<double>(sourceRate));
            if (noise > 0.0) position = clamp(position + (randomUnit(seed) - .5) * 2.0 * noise, -1.0, 1.0);
            trace.samples.push_back({time, position});
        }
        time += .004 * (variableCadence ? .72 + randomUnit(seed) * .56 : 1.0);
    }
    trace.endTime = time;
    return trace;
}

std::vector<Trace> loadPhysical(const std::filesystem::path &directory)
{
    std::vector<Trace> traces;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".csv") continue;
        std::ifstream file(entry.path());
        std::string line;
        if (!std::getline(file, line)) continue;
        const std::vector<std::string> header = splitCsv(line);
        const auto timeColumn = column(header, "time_ms");
        const auto rawColumn = column(header, "physical_raw_normalized");
        const auto reportedColumn = column(header, "source_reported");
        if (!timeColumn || !rawColumn || !reportedColumn) continue;
        Trace trace{entry.path().stem().string(), "real human HOTAS", "retained physical source stream", true, {}, 0.0};
        bool firstData = true;
        while (std::getline(file, line)) {
            const auto fields = splitCsv(line);
            if (fields.size() <= std::max({*timeColumn, *rawColumn, *reportedColumn})) continue;
            const double time = asNumber(fields[*timeColumn]) / 1000.0;
            const double position = asNumber(fields[*rawColumn]);
            trace.endTime = std::max(trace.endTime, time);
            if (fields[*reportedColumn] == "1") trace.samples.push_back({time, position});
            firstData = false;
        }
        // A capture begins with a zero-time framework synchronization point,
        // not an observed human transition.  The first reported device state is
        // its physical initial condition, so promote it to t=0 and retain all
        // subsequent source reports exactly as timestamped relative to it.
        if (trace.samples.size() >= 2U && std::abs(trace.samples.front().time) < kEpsilon
            && std::abs(trace.samples.front().position) <= (1.5 / 65535.0)) {
            const double origin = trace.samples[1].time;
            trace.samples.erase(trace.samples.begin());
            for (Sample &sample : trace.samples) sample.time -= origin;
            trace.endTime = std::max(0.0, trace.endTime - origin);
        }
        if (trace.samples.size() >= 2U) traces.push_back(std::move(trace));
    }
    std::sort(traces.begin(), traces.end(), [](const Trace &a, const Trace &b) { return a.id < b.id; });
    return traces;
}

struct Reversal {
    double spacing{};
    double incoming{};
    double outgoing{};
    double excursion{};
    bool centerCrossing{};
};

struct Stop {
    double incoming{};
    double stoppingTime{};
    double dwell{};
};

struct TraceMetrics {
    double duration{};
    double minimum{};
    double maximum{};
    double offCenterFraction{};
    Series signedVelocity;
    Series speed;
    Series acceleration;
    Series jerk;
    Series cadence;
    Series cadenceJitter;
    Series excursions;
    std::vector<Reversal> reversals;
    std::vector<Stop> stops;
};

TraceMetrics measure(const Trace &trace, double centerThreshold, double velocityFloor, double abruptSpeed)
{
    TraceMetrics result;
    if (trace.samples.empty()) return result;
    result.duration = std::max(0.0, trace.endTime - trace.samples.front().time);
    result.minimum = trace.samples.front().position;
    result.maximum = trace.samples.front().position;
    std::vector<double> velocity;
    std::vector<double> velocityTime;
    for (size_t i = 0; i < trace.samples.size(); ++i) {
        const Sample &sample = trace.samples[i];
        result.minimum = std::min(result.minimum, sample.position);
        result.maximum = std::max(result.maximum, sample.position);
        const double nextTime = i + 1U < trace.samples.size() ? trace.samples[i + 1U].time : trace.endTime;
        if (nextTime > sample.time && std::abs(sample.position) > centerThreshold) result.offCenterFraction += nextTime - sample.time;
        if (i == 0U) continue;
        const double dt = sample.time - trace.samples[i - 1U].time;
        if (dt <= kEpsilon) continue;
        const double v = (sample.position - trace.samples[i - 1U].position) / dt;
        velocity.push_back(v);
        velocityTime.push_back((sample.time + trace.samples[i - 1U].time) * .5);
        result.signedVelocity.add(v);
        result.speed.add(std::abs(v));
        result.cadence.add(dt);
    }
    result.offCenterFraction = result.duration > kEpsilon ? result.offCenterFraction / result.duration : 0.0;
    const double medianCadence = result.cadence.q(.50);
    for (double interval : result.cadence.values) result.cadenceJitter.add(std::abs(interval - medianCadence));
    std::vector<double> acceleration;
    std::vector<double> accelerationTime;
    for (size_t i = 1; i < velocity.size(); ++i) {
        const double dt = velocityTime[i] - velocityTime[i - 1U];
        if (dt <= kEpsilon) continue;
        const double a = (velocity[i] - velocity[i - 1U]) / dt;
        acceleration.push_back(a);
        accelerationTime.push_back((velocityTime[i] + velocityTime[i - 1U]) * .5);
        result.acceleration.add(std::abs(a));
    }
    // Jerk is d²x/dt³: velocity comes from two timestamped source positions,
    // acceleration is the adjacent midpoint derivative, and jerk is the
    // derivative between adjacent acceleration midpoints.  No fixed-rate
    // resampling is applied.
    for (size_t i = 1; i < acceleration.size(); ++i) {
        const double dt = accelerationTime[i] - accelerationTime[i - 1U];
        if (dt > kEpsilon) result.jerk.add(std::abs((acceleration[i] - acceleration[i - 1U]) / dt));
    }
    int direction = 0;
    double lastReversalTime = 0.0;
    double turningPosition = trace.samples.front().position;
    double segmentMin = turningPosition, segmentMax = turningPosition;
    double priorActiveSpeed = 0.0;
    for (size_t i = 0; i < velocity.size(); ++i) {
        const double v = velocity[i];
        const size_t sampleIndex = std::min(i + 1U, trace.samples.size() - 1U);
        const double endpoint = trace.samples[sampleIndex].position;
        segmentMin = std::min(segmentMin, endpoint);
        segmentMax = std::max(segmentMax, endpoint);
        const int sign = std::abs(v) > velocityFloor ? (v > 0.0 ? 1 : -1) : 0;
        if (sign != 0 && direction != 0 && sign != direction) {
            const double time = velocityTime[i];
            result.reversals.push_back({lastReversalTime > 0.0 ? time - lastReversalTime : 0.0,
                priorActiveSpeed, std::abs(v), std::abs(endpoint - turningPosition),
                segmentMin <= 0.0 && segmentMax >= 0.0});
            lastReversalTime = time;
            turningPosition = endpoint;
            segmentMin = endpoint;
            segmentMax = endpoint;
        }
        if (sign != 0) { direction = sign; priorActiveSpeed = std::abs(v); }
    }
    for (const Reversal &reversal : result.reversals) if (reversal.spacing > 0.0) result.excursions.add(reversal.excursion);
    for (size_t i = 0; i < velocity.size(); ++i) {
        if (std::abs(velocity[i]) < abruptSpeed) continue;
        size_t settled = i + 1U;
        while (settled < velocity.size() && std::abs(velocity[settled]) > velocityFloor) ++settled;
        if (settled >= velocity.size()) continue;
        size_t resume = settled + 1U;
        while (resume < velocity.size() && std::abs(velocity[resume]) <= velocityFloor) ++resume;
        const double stopAt = velocityTime[settled];
        const double dwellEnd = resume < velocity.size() ? velocityTime[resume] : trace.endTime;
        result.stops.push_back({std::abs(velocity[i]), std::max(0.0, stopAt - velocityTime[i]), std::max(0.0, dwellEnd - stopAt)});
        i = settled;
    }
    return result;
}

void append(Series &destination, const Series &source)
{
    destination.values.insert(destination.values.end(), source.values.begin(), source.values.end());
}

struct Aggregate {
    size_t traces{};
    double totalDuration{};
    Series ranges;
    Series offCenter;
    Series signedVelocity;
    Series speed;
    Series acceleration;
    Series jerk;
    Series cadence;
    Series cadenceJitter;
    Series reversalDensity;
    Series reversalSpacing;
    Series incoming;
    Series outgoing;
    Series excursion;
    Series stopIncoming;
    Series stoppingTime;
    Series stopDwell;
    Series tracePeakSpeed;
    Series traceP95Speed;
    Series tracePeakAcceleration;
    Series traceP95Acceleration;
    Series tracePeakJerk;
    Series traceReversalCount;
    size_t reversals{};
    size_t centerCrossings{};
    size_t stops{};
};

void add(Aggregate &aggregate, const TraceMetrics &metrics)
{
    ++aggregate.traces;
    aggregate.totalDuration += metrics.duration;
    aggregate.ranges.add(metrics.maximum - metrics.minimum);
    aggregate.offCenter.add(metrics.offCenterFraction);
    append(aggregate.signedVelocity, metrics.signedVelocity);
    append(aggregate.speed, metrics.speed);
    append(aggregate.acceleration, metrics.acceleration);
    append(aggregate.jerk, metrics.jerk);
    append(aggregate.cadence, metrics.cadence);
    append(aggregate.cadenceJitter, metrics.cadenceJitter);
    aggregate.reversalDensity.add(metrics.duration > kEpsilon ? static_cast<double>(metrics.reversals.size()) / metrics.duration : 0.0);
    for (const Reversal &item : metrics.reversals) {
        ++aggregate.reversals;
        aggregate.centerCrossings += item.centerCrossing ? 1U : 0U;
        if (item.spacing > 0.0) aggregate.reversalSpacing.add(item.spacing);
        aggregate.incoming.add(item.incoming);
        aggregate.outgoing.add(item.outgoing);
        aggregate.excursion.add(item.excursion);
    }
    for (const Stop &item : metrics.stops) {
        ++aggregate.stops;
        aggregate.stopIncoming.add(item.incoming);
        aggregate.stoppingTime.add(item.stoppingTime);
        aggregate.stopDwell.add(item.dwell);
    }
    aggregate.tracePeakSpeed.add(metrics.speed.maximum());
    aggregate.traceP95Speed.add(metrics.speed.q(.95));
    aggregate.tracePeakAcceleration.add(metrics.acceleration.maximum());
    aggregate.traceP95Acceleration.add(metrics.acceleration.q(.95));
    aggregate.tracePeakJerk.add(metrics.jerk.maximum());
    aggregate.traceReversalCount.add(static_cast<double>(metrics.reversals.size()));
}

std::string statisticsRow(const std::string &metric, const Series &series, const std::string &unit)
{
    return csv(metric) + ',' + std::to_string(series.count()) + ',' + number(series.mean()) + ',' + number(series.q(.50)) + ','
        + number(series.q(.75)) + ',' + number(series.q(.90)) + ',' + number(series.q(.95)) + ','
        + number(series.q(.975)) + ',' + number(series.q(.99)) + ',' + number(series.maximum()) + ',' + csv(unit) + "\n";
}

std::vector<std::pair<double, double>> personaPoints(std::uint32_t &state, int persona, int stage, int sequence)
{
    const std::array<double, 4> combatStep{.055, .045, .040, .032};
    const std::array<double, 4> combatRange{.95, .98, .86, .72};
    const std::array<double, 4> precisionStep{.34, .26, .26, .19};
    const std::array<double, 4> precisionRange{.16, .15, .14, .10};
    const std::array<double, 4> wingStep{.16, .09, .085, .072};
    const std::array<double, 4> wingRange{.62, .72, .64, .58};
    const std::array<double, 4> spaceStep{.16, .12, .12, .105};
    const std::array<double, 4> spaceRange{.62, .84, .74, .66};
    const std::array<double, 4> noisyStep{.16, .12, .12, .14};
    const std::array<double, 4> noisyRange{.42, .38, .30, .25};
    const std::array<const std::array<double, 4> *, 5> steps{&precisionStep, &combatStep, &wingStep, &spaceStep, &noisyStep};
    const std::array<const std::array<double, 4> *, 5> ranges{&precisionRange, &combatRange, &wingRange, &spaceRange, &noisyRange};
    const double step = (*steps[static_cast<size_t>(persona)])[static_cast<size_t>(stage)];
    const double range = (*ranges[static_cast<size_t>(persona)])[static_cast<size_t>(stage)];
    const double end = stage == 1 ? 2.6 : stage == 2 ? 2.8 : 2.9;
    std::vector<std::pair<double, double>> points{{0.0, (randomUnit(state) - .5) * .2}};
    for (double time = step; time < end; time += step) {
        double multiplier = 1.0;
        if (stage == 1 && persona == 3 && sequence % 3 == 0) multiplier = 1.25;
        if (stage == 2 && sequence % 7 == 0) multiplier = 1.22;
        if (stage == 3 && sequence % 9 == 0) multiplier = 1.28;
        else if (stage == 3 && sequence % 7 == 0) multiplier = .55;
        points.push_back({time, clamp(points.back().second + (randomUnit(state) - .5) * 2.0 * range * multiplier, -1.0, 1.0)});
    }
    points.push_back({end + .2, points.back().second});
    return points;
}

std::vector<Trace> historicalSynthetic()
{
    std::vector<Trace> traces;
    std::uint32_t seed = 0x24065A11U;
    const std::array<std::string, 5> names{"Precision Pilot", "Combat Helicopter", "Fixed-Wing", "Space-Sim", "Noisy Sensor"};
    const std::array<int, 4> counts{4, 60, 90, 120};
    const std::array<std::string, 4> stages{"R0 canonical", "R0.5 blind", "R0.6 blind", "R0.7 blind"};
    for (int stage = 0; stage < 4; ++stage) for (int persona = 0; persona < 5; ++persona) for (int sequence = 0; sequence < counts[static_cast<size_t>(stage)]; ++sequence) {
        std::uint32_t state = seed++;
        const auto points = personaPoints(state, persona, stage, sequence);
        const bool combat = persona == 1;
        const bool noisy = persona == 4;
        traces.push_back(makeSynthetic(stages[static_cast<size_t>(stage)] + "/persona/" + std::to_string(sequence) + "/" + names[static_cast<size_t>(persona)],
            names[static_cast<size_t>(persona)], stages[static_cast<size_t>(stage)] + " persona generator", points, seed++, combat ? 250 : 125, noisy, noisy ? (stage == 0 ? .005 : .006) : 0.0));
    }
    struct Pattern { const char *name; std::vector<std::pair<double, double>> points; bool cadence{}; bool noise{}; };
    const std::vector<Pattern> patterns{
        {"slow coherent movement", {{0,-.55},{2.6,.55},{3.2,.55}}, false, false},
        {"ballistic sweep", {{0,-.92},{.12,.92},{.8,.92}}, false, false},
        {"stopping", {{0,-.75},{.19,.82},{1.2,.82}}, false, false},
        {"endpoints", {{0,-.30},{.34,.76},{.62,.985},{1.2,.985}}, false, false},
        {"cadence", {{0,-.65},{.82,.65},{1.5,.65}}, true, false},
        {"slow coherent", {{0,-.70},{.80,.70},{1.25,.70}}, false, false},
        {"ballistic smooth", {{0,-.88},{.30,.88},{1.15,.88}}, false, false},
        {"fast ballistic", {{0,-.90},{.115,.90},{1.0,.90}}, false, false},
        {"very-fast ballistic", {{0,-.92},{.052,.92},{.90,.92}}, false, false},
        {"acceleration changes", {{0,-.80},{.22,.15},{.34,.85},{1.1,.85}}, false, false},
        {"high jerk", {{0,0},{.035,.65},{.070,-.58},{.110,.82},{.60,.82}}, false, true},
        {"reversal", {{0,.82},{.18,-.78},{.48,.76},{1.08,.76}}, false, false},
        {"repeated reversal", {{0,-.70},{.13,.72},{.25,-.66},{.37,.70},{1.05,.70}}, false, false},
        {"brake/reverse", {{0,-.86},{.15,.80},{.24,.79},{.35,-.72},{1.08,-.72}}, false, false},
        {"stop", {{0,-.90},{.16,.88},{1.10,.88}}, false, false},
        {"cadence jitter", {{0,-.65},{.58,.62},{1.20,.62}}, true, false},
        {"sample-and-hold", {{0,-.50},{.36,.55},{.72,.55},{.95,-.35},{1.2,-.35}}, true, false},
        {"endpoint compound", {{0,-.48},{.30,.55},{.55,.95},{.75,.32},{1.15,.32}}, false, false},
        {"oscillation", {{0,.70},{.14,-.62},{.31,.82},{.49,-.45},{.70,.55},{1.1,.55}}, false, false},
        {"same-side reversal", {{0,-.70},{.14,.70},{.25,-.20},{.38,.72},{1.1,.72}}, false, false},
        {"abrupt stop", {{0,-.85},{.18,.82},{1.1,.82}}, false, false},
        {"compound motion", {{0,-.60},{.10,.70},{.22,-.50},{.36,.80},{.52,.15},{.74,.90},{1.3,.90}}, false, false}
    };
    for (const Pattern &pattern : patterns) traces.push_back(makeSynthetic("R0.7/structured/" + std::string(pattern.name), pattern.name,
        "R0.7 structured research generator", pattern.points, seed++, pattern.cadence ? 125 : 250, pattern.cadence, pattern.noise ? .003 : 0.0));
    return traces;
}

struct Envelope {
    double representativeSpeed{};
    double representativeAcceleration{};
    double representativeJerk{};
    double representativeReversalDensity{};
    double representativeCadence{};
    double extremeSpeed{};
    double extremeAcceleration{};
    double extremeJerk{};
    double extremeReversalDensity{};
    double extremeMinReversalSpacing{};
};

Trace makeCorrectedCombat(const std::string &id, bool extreme, std::uint32_t seed, const Aggregate &human, const Envelope &envelope)
{
    Trace trace{id, extreme ? "combat-human-extreme" : "combat-human-realistic",
        extreme ? "R0.65 derived human-extreme combat generator" : "R0.65 derived human-realistic combat generator", false, {}, 0.0};
    const double speedCap = extreme ? envelope.extremeSpeed : envelope.representativeSpeed;
    const double accelerationCap = extreme ? envelope.extremeAcceleration : envelope.representativeAcceleration;
    const double jerkCap = extreme ? envelope.extremeJerk : envelope.representativeJerk;
    double time = 0.0;
    double position = (randomUnit(seed) - .5) * .5;
    double velocity = 0.0;
    double acceleration = 0.0;
    double targetVelocity = 0.0;
    double segmentRemaining = 0.0;
    int direction = randomUnit(seed) < .5 ? -1 : 1;
    trace.samples.push_back({time, position});
    while (time < 3.0) {
        double dt = human.cadence.values.empty() ? .015 : human.cadence.values[static_cast<size_t>(randomUnit(seed) * human.cadence.values.size()) % human.cadence.values.size()];
        dt = clamp(dt, .002, .060);
        dt = std::min(dt, 3.0 - time);
        if (segmentRemaining <= 0.0) {
            const double intent = randomUnit(seed);
            if (intent < .18) targetVelocity = 0.0; // brief deliberate stop; cadence and duration stay empirical.
            else {
                if (intent < .62) direction = -direction; // correction/reversal is more common than an open sweep.
                const double q = extreme ? .78 + .215 * randomUnit(seed) : .55 + .40 * randomUnit(seed);
                targetVelocity = static_cast<double>(direction) * std::min(speedCap, human.speed.q(q));
                if (extreme && randomUnit(seed) > .90) targetVelocity = static_cast<double>(direction) * speedCap * (.90 + .10 * randomUnit(seed));
            }
            const double base = human.reversalSpacing.count() ? human.reversalSpacing.q(.30 + .55 * randomUnit(seed)) : .18;
            segmentRemaining = clamp(base, .045, .65);
        }
        const double requestedAcceleration = clamp((targetVelocity - velocity) / std::max(.018, segmentRemaining), -accelerationCap, accelerationCap);
        const double accelerationDelta = clamp(requestedAcceleration - acceleration, -jerkCap * dt, jerkCap * dt);
        acceleration = clamp(acceleration + accelerationDelta, -accelerationCap, accelerationCap);
        velocity = clamp(velocity + acceleration * dt, -speedCap, speedCap);
        position += velocity * dt;
        if (position >= .98 || position <= -.98) {
            position = clamp(position, -.98, .98);
            velocity = -velocity * .18;
            acceleration = -acceleration * .35;
            direction = velocity >= 0.0 ? 1 : -1;
            segmentRemaining = 0.0;
        }
        time += dt;
        segmentRemaining -= dt;
        trace.samples.push_back({std::min(time, 3.0), position});
    }
    trace.endTime = 3.0;
    return trace;
}

std::vector<Trace> correctedCorpora(const Aggregate &human, const Envelope &envelope)
{
    std::vector<Trace> traces;
    std::uint32_t seed = 0x24065C01U;
    for (int index = 0; index < 600; ++index) traces.push_back(makeCorrectedCombat("combat-human-realistic/" + std::to_string(index), false, seed++, human, envelope));
    for (int index = 0; index < 600; ++index) traces.push_back(makeCorrectedCombat("combat-human-extreme/" + std::to_string(index), true, seed++, human, envelope));
    // Preserve the old formula as a separately named stress corpus.  Its
    // historical IDs remain in prior artifacts; only newly generated samples
    // receive the corrected taxonomy.
    for (int index = 0; index < 600; ++index) {
        std::uint32_t state = seed++;
        std::vector<std::pair<double, double>> points{{0.0, (randomUnit(state) - .5) * .3}};
        for (double time = .055; time < 3.2; time += .055) points.push_back({time, clamp(points.back().second + (randomUnit(state) - .5) * 1.9, -1.0, 1.0)});
        points.push_back({3.4, points.back().second});
        Trace trace = makeSynthetic("combat-extreme-stress/" + std::to_string(index), "combat-extreme-stress", "preserved R0 Combat Helicopter formula", points, seed++, 250);
        traces.push_back(std::move(trace));
    }
    return traces;
}

std::string classification(const Aggregate &metrics, const Envelope &envelope, bool sensor)
{
    if (sensor) return "DEVICE_STRESS_NOT_HUMAN_MOTION";
    const bool representative = metrics.speed.q(.95) <= envelope.representativeSpeed
        && metrics.acceleration.q(.95) <= envelope.representativeAcceleration
        && metrics.jerk.q(.95) <= envelope.representativeJerk
        && metrics.reversalDensity.q(.95) <= envelope.representativeReversalDensity;
    if (representative) return "HUMAN_REPRESENTATIVE";
    const bool extreme = metrics.speed.q(.99) <= envelope.extremeSpeed
        && metrics.acceleration.q(.99) <= envelope.extremeAcceleration
        && metrics.jerk.q(.99) <= envelope.extremeJerk
        && metrics.reversalDensity.maximum() <= envelope.extremeReversalDensity;
    return extreme ? "HUMAN_EXTREME" : "TORTURE_BEYOND_HUMAN";
}

struct R07Summary {
    double reliability{};
    double meanHorizon{};
    double at8{};
    bool found{};
};

std::map<std::string, R07Summary> readR07Families(const std::filesystem::path &path)
{
    std::map<std::string, R07Summary> result;
    std::ifstream file(path / "blind_family_results.csv");
    std::string line;
    if (!std::getline(file, line)) return result;
    const auto header = splitCsv(line);
    const auto scope = column(header, "scope"), budget = column(header, "quality_budget"), reliability = column(header, "reliability"), mean = column(header, "mean_safe_horizon_ms"), at8 = column(header, "pct_ge_8_ms");
    if (!scope || !budget || !reliability || !mean || !at8) return result;
    while (std::getline(file, line)) {
        const auto fields = splitCsv(line);
        if (fields.size() <= std::max({*scope, *budget, *reliability, *mean, *at8}) || fields[*budget] != "general") continue;
        result[fields[*scope]] = {asNumber(fields[*reliability]), asNumber(fields[*mean]), asNumber(fields[*at8]), true};
    }
    return result;
}

struct Options {
    std::filesystem::path repo;
    std::filesystem::path output;
    std::filesystem::path report;
    std::filesystem::path r07;
};

Options options(int argc, char *argv[])
{
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--repo" && i + 1 < argc) result.repo = argv[++i];
        else if (argument == "--out" && i + 1 < argc) result.output = argv[++i];
        else if (argument == "--report" && i + 1 < argc) result.report = argv[++i];
        else if (argument == "--r07" && i + 1 < argc) result.r07 = argv[++i];
    }
    return result;
}

std::string metricBlock(const std::string &title, const Aggregate &metrics)
{
    std::ostringstream text;
    text << "## " << title << "\n\n"
         << "Samples: " << metrics.speed.count() << "; traces: " << metrics.traces << "; speed median/p95/p99/max = "
         << number(metrics.speed.q(.50), 3) << "/" << number(metrics.speed.q(.95), 3) << "/" << number(metrics.speed.q(.99), 3) << "/" << number(metrics.speed.maximum(), 3)
         << " normalized units/s. Acceleration p95/max = " << number(metrics.acceleration.q(.95), 3) << "/" << number(metrics.acceleration.maximum(), 3)
         << "; jerk p95/max = " << number(metrics.jerk.q(.95), 3) << "/" << number(metrics.jerk.maximum(), 3) << ".\n\n";
    return text.str();
}

} // namespace

int main(int argc, char *argv[])
{
    const Options input = options(argc, argv);
    if (input.repo.empty() || input.output.empty()) {
        std::cerr << "Usage: --repo <worktree> --out <new-empty-directory> [--report <report.md>] [--r07 <artifact-directory>]\n";
        return 2;
    }
    std::error_code error;
    if (std::filesystem::exists(input.output, error) && (!std::filesystem::is_directory(input.output) || !std::filesystem::is_empty(input.output))) {
        std::cerr << "Output directory must be new or empty: " << input.output.string() << "\n";
        return 2;
    }
    if (!std::filesystem::exists(input.output) && !std::filesystem::create_directories(input.output, error)) {
        std::cerr << "Cannot create output directory\n";
        return 2;
    }
    const std::vector<Trace> physical = loadPhysical(input.repo / "tests" / "fixtures" / "adaptive_response_real_hotas");
    if (physical.size() != 15U) {
        std::cerr << "Expected exactly 15 retained physical traces, found " << physical.size() << "\n";
        return 3;
    }
    // Derive event gates from the retained data before classifying any trace.
    Series stationarySpeed, stationaryPosition;
    for (const Trace &trace : physical) if (trace.id == "stationary-jitter-1") {
        const TraceMetrics raw = measure(trace, 0.0, 0.0, std::numeric_limits<double>::infinity());
        append(stationarySpeed, raw.speed);
        for (const Sample &sample : trace.samples) stationaryPosition.add(std::abs(sample.position));
    }
    const double centerThreshold = stationaryPosition.q(.99);
    const double velocityFloor = stationarySpeed.q(.99);
    Aggregate human;
    std::map<std::string, TraceMetrics> physicalMetrics;
    // The abrupt-stop gate is the pooled human p90 speed, calculated without
    // a predictor and only after source timestamp differentiation.
    Series preliminarySpeed;
    for (const Trace &trace : physical) append(preliminarySpeed, measure(trace, centerThreshold, velocityFloor, std::numeric_limits<double>::infinity()).speed);
    const double abruptSpeed = preliminarySpeed.q(.90);
    for (const Trace &trace : physical) {
        TraceMetrics metrics = measure(trace, centerThreshold, velocityFloor, abruptSpeed);
        add(human, metrics);
        physicalMetrics.emplace(trace.id, std::move(metrics));
    }
    Envelope envelope{
        human.speed.q(.95), human.acceleration.q(.95), human.jerk.q(.95), human.reversalDensity.q(.95), human.cadence.q(.95),
        std::max(human.speed.q(.99), human.speed.maximum() * 1.15), human.acceleration.q(.99) * 1.18,
        human.jerk.q(.99) * 1.10, human.reversalDensity.maximum() * 1.20,
        human.reversalSpacing.q(.10) * .85
    };

    const std::vector<Trace> historical = historicalSynthetic();
    std::map<std::string, Aggregate> synthetic;
    std::map<std::string, std::string> generators;
    for (const Trace &trace : historical) {
        const TraceMetrics metrics = measure(trace, centerThreshold, velocityFloor, abruptSpeed);
        add(synthetic[trace.family], metrics);
        generators.emplace(trace.family, trace.generator);
    }
    const std::vector<Trace> corrected = correctedCorpora(human, envelope);
    std::map<std::string, Aggregate> correctedMetrics;
    for (const Trace &trace : corrected) add(correctedMetrics[trace.family], measure(trace, centerThreshold, velocityFloor, abruptSpeed));

    std::ostringstream envelopeCsv;
    envelopeCsv << "metric,count,mean,median,p75,p90,p95,p97_5,p99,max,unit\n";
    envelopeCsv << statisticsRow("position_range", human.ranges, "normalized axis") << statisticsRow("off_center_dwell_fraction", human.offCenter, "fraction of capture duration")
        << statisticsRow("absolute_velocity", human.speed, "normalized units/s") << statisticsRow("signed_velocity", human.signedVelocity, "normalized units/s")
        << statisticsRow("per_trace_peak_velocity", human.tracePeakSpeed, "normalized units/s") << statisticsRow("per_trace_p95_velocity", human.traceP95Speed, "normalized units/s")
        << statisticsRow("absolute_acceleration", human.acceleration, "normalized units/s^2") << statisticsRow("absolute_jerk", human.jerk, "normalized units/s^3")
        << statisticsRow("source_interval", human.cadence, "seconds") << statisticsRow("source_interval_jitter_about_trace_median", human.cadenceJitter, "seconds")
        << statisticsRow("reversal_density", human.reversalDensity, "reversals/s") << statisticsRow("reversal_spacing", human.reversalSpacing, "seconds")
        << statisticsRow("reversal_incoming_speed", human.incoming, "normalized units/s") << statisticsRow("reversal_outgoing_speed", human.outgoing, "normalized units/s")
        << statisticsRow("reversal_excursion", human.excursion, "normalized axis") << statisticsRow("stop_incoming_speed", human.stopIncoming, "normalized units/s")
        << statisticsRow("stopping_time", human.stoppingTime, "seconds") << statisticsRow("post_stop_dwell", human.stopDwell, "seconds");

    std::ostringstream profiles;
    profiles << "trace_id,sample_count,duration_s,position_range,peak_speed,p95_speed,p99_speed,peak_acceleration,p95_acceleration,median_jerk,p95_jerk,p99_jerk,peak_jerk,reversal_count,reversals_per_second,min_reversal_spacing_s,median_reversal_excursion,center_crossing_reversals,same_side_reversals,abrupt_stop_count,median_stop_incoming_speed,median_stopping_time_s,median_post_stop_dwell_s,cadence_median_ms,cadence_p90_ms,cadence_p99_ms,cadence_jitter_p90_ms,off_center_dwell_fraction\n";
    for (const Trace &trace : physical) {
        const TraceMetrics &m = physicalMetrics.at(trace.id);
        const size_t crossing = static_cast<size_t>(std::count_if(m.reversals.begin(), m.reversals.end(), [](const Reversal &r) { return r.centerCrossing; }));
        Series stopIncoming, stopTime, stopDwell;
        for (const Stop &s : m.stops) { stopIncoming.add(s.incoming); stopTime.add(s.stoppingTime); stopDwell.add(s.dwell); }
        profiles << csv(trace.id) << ',' << trace.samples.size() << ',' << number(m.duration) << ',' << number(m.maximum - m.minimum) << ','
                 << number(m.speed.maximum()) << ',' << number(m.speed.q(.95)) << ',' << number(m.speed.q(.99)) << ',' << number(m.acceleration.maximum()) << ',' << number(m.acceleration.q(.95)) << ','
                 << number(m.jerk.q(.50)) << ',' << number(m.jerk.q(.95)) << ',' << number(m.jerk.q(.99)) << ',' << number(m.jerk.maximum()) << ','
                 << m.reversals.size() << ',' << number(m.duration > 0.0 ? m.reversals.size() / m.duration : 0.0) << ',' << number(m.reversals.empty() ? 0.0 : [&m]{ Series v; for(const auto &r:m.reversals) if(r.spacing>0) v.add(r.spacing); return v.q(.10); }()) << ','
                 << number(m.excursions.q(.50)) << ',' << crossing << ',' << (m.reversals.size() - crossing) << ',' << m.stops.size() << ',' << number(stopIncoming.q(.50)) << ',' << number(stopTime.q(.50)) << ',' << number(stopDwell.q(.50)) << ','
                 << number(m.cadence.q(.50) * 1000.0) << ',' << number(m.cadence.q(.90) * 1000.0) << ',' << number(m.cadence.q(.99) * 1000.0) << ',' << number(m.cadenceJitter.q(.90) * 1000.0) << ',' << number(m.offCenterFraction) << '\n';
    }

    std::ostringstream syntheticCsv;
    syntheticCsv << "family,source_generator,traces,velocity_median,velocity_p95,velocity_p99,velocity_max,acceleration_p95,acceleration_p99,jerk_p95,jerk_p99,reversal_density_median,reversal_density_p95,reversal_density_max,reversal_spacing_p10,excursion_p95,cadence_p95_ms,velocity_exceeds_human_p95,velocity_exceeds_human_p99,velocity_exceeds_human_extreme,classification\n";
    std::ostringstream reclassification;
    reclassification << "family,prior_label,operating_envelope,reason\n";
    for (const auto &[family, m] : synthetic) {
        const bool sensor = family == "Noisy Sensor" || family == "cadence jitter" || family == "sample-and-hold";
        const std::string status = classification(m, envelope, sensor);
        syntheticCsv << csv(family) << ',' << csv(generators[family]) << ',' << m.traces << ',' << number(m.speed.q(.50)) << ',' << number(m.speed.q(.95)) << ',' << number(m.speed.q(.99)) << ',' << number(m.speed.maximum()) << ','
                     << number(m.acceleration.q(.95)) << ',' << number(m.acceleration.q(.99)) << ',' << number(m.jerk.q(.95)) << ',' << number(m.jerk.q(.99)) << ',' << number(m.reversalDensity.q(.50)) << ',' << number(m.reversalDensity.q(.95)) << ',' << number(m.reversalDensity.maximum()) << ',' << number(m.reversalSpacing.q(.10)) << ',' << number(m.excursion.q(.95)) << ',' << number(m.cadence.q(.95) * 1000.0) << ','
                     << number(m.speed.fractionAbove(human.speed.q(.95))) << ',' << number(m.speed.fractionAbove(human.speed.q(.99))) << ',' << number(m.speed.fractionAbove(envelope.extremeSpeed)) << ',' << status << '\n';
        reclassification << csv(family) << ',' << csv(family == "Combat Helicopter" ? "Human-like Combat Helicopter" : "historical research family") << ',' << status << ','
                         << csv(sensor ? "Sensor/cadence manipulation is not a human-motion persona." : status == "TORTURE_BEYOND_HUMAN" ? "Direct source-motion distributions exceed the separately derived human-extreme envelope." : "Classified against direct retained-HOTAS envelope.") << '\n';
    }
    reclassification << "\"combat-extreme-stress\",\"Combat Helicopter historical formula\",TORTURE_BEYOND_HUMAN,\"Preserved formula under corrected taxonomy; historical IDs are unchanged.\"\n";

    std::ostringstream validation;
    validation << "family,operating_envelope,trajectories,velocity_p95,velocity_p99,velocity_max,acceleration_p95,acceleration_p99,jerk_p95,jerk_p99,reversal_density_p95,cadence_p95_ms,fraction_velocity_within_intended,fraction_acceleration_within_intended,fraction_jerk_within_intended,validation\n";
    for (const auto &[family, m] : correctedMetrics) {
        const bool torture = family == "combat-extreme-stress";
        const std::string status = torture ? "TORTURE_BEYOND_HUMAN" : family == "combat-human-extreme" ? "HUMAN_EXTREME" : "HUMAN_REPRESENTATIVE";
        const double speedLimit = status == "HUMAN_REPRESENTATIVE" ? envelope.representativeSpeed : envelope.extremeSpeed;
        const double accelerationLimit = status == "HUMAN_REPRESENTATIVE" ? envelope.representativeAcceleration : envelope.extremeAcceleration;
        const double jerkLimit = status == "HUMAN_REPRESENTATIVE" ? envelope.representativeJerk : envelope.extremeJerk;
        const double speedWithin = 1.0 - m.speed.fractionAbove(speedLimit);
        const double accelerationWithin = 1.0 - m.acceleration.fractionAbove(accelerationLimit);
        const double jerkWithin = 1.0 - m.jerk.fractionAbove(jerkLimit);
        // Human-realistic must be inside its representative p99.  Extreme is
        // allowed a less-than-0.5% tail probe beyond its controlled cap, which
        // is reported rather than silently clipped.
        const bool valid = torture || (m.speed.q(.99) <= speedLimit && m.acceleration.q(.99) <= accelerationLimit && m.jerk.q(.99) <= jerkLimit
            && speedWithin >= .995 && accelerationWithin >= .995 && jerkWithin >= .995);
        validation << csv(family) << ',' << status << ',' << m.traces << ',' << number(m.speed.q(.95)) << ',' << number(m.speed.q(.99)) << ',' << number(m.speed.maximum()) << ',' << number(m.acceleration.q(.95)) << ',' << number(m.acceleration.q(.99)) << ',' << number(m.jerk.q(.95)) << ',' << number(m.jerk.q(.99)) << ',' << number(m.reversalDensity.q(.95)) << ',' << number(m.cadence.q(.95) * 1000.0) << ',' << number(speedWithin) << ',' << number(accelerationWithin) << ',' << number(jerkWithin) << ',' << (valid ? "PASS" : "OUTSIDE_INTENDED_ENVELOPE") << '\n';
    }

    const auto r07 = readR07Families(input.r07);
    const Aggregate &combat = synthetic.at("Combat Helicopter");
    const Aggregate &ballistic = synthetic.at("very-fast ballistic");
    const TraceMetrics &veryFastPhysical = physicalMetrics.at("very-fast-full-sweep-1");
    std::ostringstream ballisticCsv;
    ballisticCsv << "comparison,derivative_source,velocity_p95,velocity_max,acceleration_p95,jerk_p95,reversal_density_p95,r07_general_reliability,r07_mean_safe_horizon_ms,r07_pct_ge_8_ms,interpretation\n";
    const auto writeCompare = [&ballisticCsv, &r07](const std::string &name, const Aggregate &m, const std::string &r07name, const std::string &note) {
        const auto found = r07.find(r07name);
        const R07Summary s = found == r07.end() ? R07Summary{} : found->second;
        ballisticCsv << csv(name) << ",\"direct source positions/timestamps\"," << number(m.speed.q(.95)) << ',' << number(m.speed.maximum()) << ',' << number(m.acceleration.q(.95)) << ',' << number(m.jerk.q(.95)) << ',' << number(m.reversalDensity.q(.95)) << ',' << (s.found ? number(s.reliability) : "") << ',' << (s.found ? number(s.meanHorizon) : "") << ',' << (s.found ? number(s.at8) : "") << ',' << csv(note) << '\n';
    };
    writeCompare("existing Combat Helicopter", combat, "Combat Helicopter", "Rapid stochastic direction changes create error-bound contraction; its historical result is torture evidence, not an ordinary-human quality gate.");
    writeCompare("structured very-fast ballistic", ballistic, "very-fast ballistic", "High velocity is coherent and sustained; therefore it remains much more forecastable than stochastic Combat.");
    Aggregate veryFastAggregate; add(veryFastAggregate, veryFastPhysical);
    writeCompare("real very-fast-full-sweep-1", veryFastAggregate, "", "Physical motion metrics are primary; no new R0.7 replay was run in this audit, so R0.7 safe-horizon fields are intentionally blank.");

    const std::string envelopeDefinition = "# Human motion envelope definition\n\n"
        "## Source and derivative method\n\nAll 15 retained fixture streams are read from `source_reported=1`, `time_ms`, and `physical_raw_normalized`. The leading t=0 zero is a capture-framework synchronization record, not a human movement: it is dropped and the first reported device position becomes t=0. No predictor, replay output, or rendered 4 ms timeline is used to derive this envelope. Velocity is adjacent source-position difference divided by adjacent source timestamp difference; acceleration is adjacent velocity difference over velocity-midpoint time; jerk is adjacent acceleration difference over acceleration-midpoint time.\n\n"
        "A reversal is an observed sign change whose absolute source velocity is above the stationary-jitter p99 floor (" + number(velocityFloor) + "). Off-center means absolute position above stationary-jitter p99 (" + number(centerThreshold) + "). An abrupt stop is a speed at or above pooled human p90 (" + number(abruptSpeed) + ") followed by the stationary gate. These are audit definitions, not mapper thresholds.\n\n"
        "## HUMAN_REPRESENTATIVE\n\nRepresentative limits use direct physical p95 for speed (" + number(envelope.representativeSpeed) + "), acceleration (" + number(envelope.representativeAcceleration) + "), and jerk (" + number(envelope.representativeJerk) + "), plus p95 per-trace reversal density (" + number(envelope.representativeReversalDensity) + ").\n\n"
        "## HUMAN_EXTREME\n\nExtreme is the observed intentional upper envelope plus metric-specific engineering margin: speed is max(p99, observed max × 1.15) = " + number(envelope.extremeSpeed) + "; acceleration is p99 × 1.18 = " + number(envelope.extremeAcceleration) + "; jerk is p99 × 1.10 = " + number(envelope.extremeJerk) + "; reversal density is per-trace observed max × 1.20 = " + number(envelope.extremeReversalDensity) + ". Reversal spacing gets a separate controlled lower margin (physical p10 × 0.85 = " + number(envelope.extremeMinReversalSpacing) + " s).\n\n"
        "## TORTURE_BEYOND_HUMAN\n\nA corpus is torture when its direct distribution exceeds this human-extreme envelope. It is retained for finite/bounded output, reversal safety, stationary convergence, graceful contraction, and allocation-free correctness only; it is not weighted as ordinary-human long-horizon quality.\n";

    const std::string noisy = "# Noisy Sensor confounding audit\n\nThe historical `Noisy Sensor` generator changes two dimensions together: it uses a lower-amplitude random-walk motion distribution and it adds variable cadence plus injected position noise. Its measured outcome therefore cannot distinguish human intent from sensor quality. The same issue applies, at narrower scope, to cadence-jitter and sample-and-hold structured cases.\n\nFuture corpus construction should use orthogonal layers:\n\n- Motion profile: precision, general, combat, human-extreme.\n- Sensor profile: clean, quantized, jittery, spiky, drift, low-rate/sample-hold.\n\nA test should name both layers (for example `combat + clean`, `combat + low-rate/sample-hold`) and report both direct source-motion metrics and sensor perturbation parameters. This is a proposal only; this pass does not redesign the R0.7 runner.\n";
    const std::string stress = "stress_tag,applies_to,definition\nVELOCITY_STRESS,fast ballistic;very-fast ballistic,High coherent speed\nACCELERATION_STRESS,acceleration changes;abrupt stop,Large sustained source acceleration/deceleration\nJERK_STRESS,high jerk,Rapid acceleration changes\nREVERSAL_STRESS,reversal;same-side reversal;brake/reverse,Coherent direction reversal\nOSCILLATION_STRESS,repeated reversal;oscillation,Repeated direction changes\nNOISE_STRESS,Noisy Sensor,Sensor perturbation rather than human persona\nCADENCE_STRESS,cadence jitter;sample-and-hold,Irregular or held source timing\nCOMPOUND_STRESS,endpoint compound;compound motion,Multiple stress mechanisms in one trajectory\n";
    const std::string historicalReinterpretation = "# Historical evidence reinterpretation\n\n| Historical pass | Classification | R0.65 interpretation |\n|---|---|---|\n| Monster Phase 1 | VALID AS TORTURE EVIDENCE | Structured mechanism results retain their exact stress meaning; any old Combat quality inference is not ordinary-human evidence. |\n| Phase 2 / Phase 3 | HUMAN-GENERALIZATION CONCLUSION WEAKENED | Frozen preset/search evidence remains historical, but its mixed Combat fitness cannot establish ordinary Combat human generalization. |\n| Phase 2B | HUMAN-GENERALIZATION CONCLUSION WEAKENED | Robustness/ranking evidence remains recorded; the old Combat slice must be relabeled torture for human claims. |\n| R0 | MUST BE REQUALIFIED WITH REALISTIC COMBAT | Forecastability conclusions containing Combat must use corrected Combat families. |\n| R0.5 | MUST BE REQUALIFIED WITH REALISTIC COMBAT | The blind corpus was valid under its sealed protocol, but its Combat persona is now a torture distribution. |\n| R0.6 | MUST BE REQUALIFIED WITH REALISTIC COMBAT | Safe-horizon quality conclusions are weakened for human Combat, while safe contraction evidence remains valid. |\n| R0.7 | MUST BE REQUALIFIED WITH REALISTIC COMBAT | Historical PARTIAL disposition is unchanged. Its Combat-family result is now primarily torture calibration/safety evidence. |\n\nNo historical artifact is modified or erased.\n";
    const std::string r07Text = "# R0.7 corrected interpretation\n\nR0.7 remains `V2.4.R0.7 PARTIAL — ERROR BOUNDS IMPROVED BUT CRITICAL CALIBRATION GAP REMAINS`. The sealed blind evidence reported pooled General reliability about 94.33%, Combat coverage 86.82%, Combat mean General safe horizon 1.64 ms, moving Combat >=8 ms about 0.12%, and Noisy Sensor coverage 91.35%.\n\nThe direct source-motion audit shows that old Combat is predominantly beyond the derived human-extreme envelope, so its poor horizon utilization is expected torture-motion contraction evidence rather than a reliable estimate of ordinary human Combat quality. This weakens the pooled human-generalization conclusion and requires requalification, but it does not erase the R0.7 calibration shortfall: Noisy Sensor is confounded and the non-Combat family results also require a cleaner split of motion quality from sensor quality. No R0.7 replay is performed here.\n";
    const std::string policy = "# Future acceptance policy\n\n## HUMAN_REPRESENTATIVE\n\nRequire high prediction quality, usable horizon, calibrated reliability, and controlled tails on a fresh sealed corpus.\n\n## HUMAN_EXTREME\n\nRequire safety and strong quality; reduced but useful horizon is acceptable when contraction is graceful.\n\n## TORTURE_BEYOND_HUMAN\n\nRequire finite/bounded output, correct reversal/stale-direction protection, no instability, stationary convergence, safe horizon contraction, and hot-path allocation correctness. Torture accuracy is not an equal optimization fitness term.\n\n## Recommended next phase\n\nRun `V2.4.R0.7R — ERROR-BOUND REQUALIFICATION WITH AUDITED HUMAN/TORTURE CORPORA`: preserve the R0.7 method and error-bound architecture, use fresh development and sealed blind corpora, include corrected realistic Combat, human-extreme Combat, and preserved Combat torture, and score quality separately from hard torture safety. This audit does not start R0.7R.\n";
    const std::string parameters = "{\n  \"schema\": \"v2.4.R0.65-corrected-combat-v1\",\n  \"research_only\": true,\n  \"seed\": \"0x24065C01\",\n  \"samples_per_family\": 600,\n  \"families\": [\n    {\"id\": \"combat-human-realistic\", \"operating_envelope\": \"HUMAN_REPRESENTATIVE\", \"speed_cap\": " + number(envelope.representativeSpeed) + ", \"acceleration_cap\": " + number(envelope.representativeAcceleration) + ", \"jerk_cap\": " + number(envelope.representativeJerk) + "},\n    {\"id\": \"combat-human-extreme\", \"operating_envelope\": \"HUMAN_EXTREME\", \"speed_cap\": " + number(envelope.extremeSpeed) + ", \"acceleration_cap\": " + number(envelope.extremeAcceleration) + ", \"jerk_cap\": " + number(envelope.extremeJerk) + "},\n    {\"id\": \"combat-extreme-stress\", \"operating_envelope\": \"TORTURE_BEYOND_HUMAN\", \"generator\": \"preserved historical R0 Combat formula\"}\n  ]\n}\n";
    const std::string identity = "{\n  \"published_v2_3_release\": \"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\n  \"qualified_adaptive_response_mathematics\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n  \"r0_7_research_source\": \"24514fe3b436a2e42623126c5da8d87c1a3c58ab\",\n  \"production_output_changed\": false,\n  \"physical_trace_count\": 15\n}\n";
    const std::string equivalence = "scope,total_production_rows,matching_rows,mismatching_rows,status\nR0.65 corpus-only executable,0,0,0,NOT_APPLICABLE_NO_PRODUCTION_SOURCE_LINKED\n";
    const std::string manifest = "{\n  \"schema\": \"v2.4.R0.65-human-motion-envelope-audit-v1\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n  \"physical_trace_count\": 15,\n  \"generator_validation_trajectories_per_family\": 600,\n  \"historical_r07_replayed\": false,\n  \"final_disposition\": \"V2.4.R0.65 PASS — HUMAN MOTION ENVELOPE DEFINED — CORPUS RECALIBRATION REQUIRED BEFORE R0.7R\"\n}\n";

    std::ostringstream report;
    report << "# V2.4.R0.65 Adaptive Response Human Motion Envelope Audit\n\n## Final disposition\n\nV2.4.R0.65 PASS — HUMAN MOTION ENVELOPE DEFINED — CORPUS RECALIBRATION REQUIRED BEFORE R0.7R\n\n"
           << "## 1. V2.3 baseline identity\n\nPublished V2.3.0 `37b6bf2`; qualified Adaptive Response mathematics `055e047e`; R0.7 source `24514fe`. This pass is corpus/test tooling only.\n\n"
           << "## 2. Why R0.65 was opened\n\nThe old Combat Helicopter random-walk generator was treated as human-like although its short-step, near-full-range random changes occupy a much more violent regime than retained direct HOTAS source motion.\n\n"
           << "## 3. Physical trace identity\n\nAll 15 retained fixtures are read from original raw source-position/source-report fields, not predictor/replay outputs. Fixture startup synchronization is explicitly excluded as non-human motion.\n\n"
           << metricBlock("4. Physical velocity envelope", human) << metricBlock("5. Physical acceleration envelope", human) << metricBlock("6. Physical jerk envelope", human)
           << "## 7. Physical reversal envelope\n\nTotal deliberate reversals: " << human.reversals << "; center-crossing: " << human.centerCrossings << "; same-side: " << (human.reversals - human.centerCrossings) << "; density p95: " << number(human.reversalDensity.q(.95), 3) << " /s; median spacing: " << number(human.reversalSpacing.q(.50), 3) << " s.\n\n"
           << "## 8. Physical stop envelope\n\nAbrupt stops: " << human.stops << "; incoming-speed median: " << number(human.stopIncoming.q(.50), 3) << "; post-stop dwell median: " << number(human.stopDwell.q(.50), 3) << " s.\n\n"
           << "## 9. Physical cadence envelope\n\nSource interval median/p90/p99: " << number(human.cadence.q(.50) * 1000.0, 3) << "/" << number(human.cadence.q(.90) * 1000.0, 3) << "/" << number(human.cadence.q(.99) * 1000.0, 3) << " ms.\n\n"
           << "## 10. Human representative definition\n\nSee `human_envelope_definition.md`; derived from physical p95 metrics.\n\n## 11. Human extreme definition\n\nSee `human_envelope_definition.md`; uses separate controlled margins by metric.\n\n## 12. Torture definition\n\nSee `human_envelope_definition.md`; torture must meet hard correctness, not ordinary-human horizon fitness.\n\n"
           << "## 13. Existing Combat forensic result\n\nExisting Combat formula: a clipped random-walk target every 55/45/40/32 ms depending historical pass, with range 0.95/0.98/0.86/0.72 and 250 Hz source sampling. Its direct source-motion metrics and exceedance fractions are in `combat_forensic_audit.csv`.\n\n"
           << "## 14. Existing Combat classification\n\n`TORTURE_BEYOND_HUMAN`: raw speed alone overlaps the fastest retained physical sweep, but old Combat exceeds the direct human-extreme envelope in its stochastic acceleration, jerk, and reversal-density distribution. Historical IDs remain unchanged.\n\n"
           << "## 15. Corrected realistic Combat design\n\n`combat-human-realistic` uses physical cadence, speed, acceleration, jerk, reversal-spacing, and excursion distributions with primarily representative targets.\n\n"
           << "## 16. Corrected extreme Combat design\n\n`combat-human-extreme` targets upper physical quantiles with metric-specific controlled margin and is separately labeled.\n\n"
           << "## 17. Preserved torture Combat design\n\n`combat-extreme-stress` preserves the historical formula as explicit torture evidence.\n\n"
           << "## 18. Precision Pilot audit\n\nCurrent classification: `" << classification(synthetic.at("Precision Pilot"), envelope, false) << "`. Retain small controlled motion; future draws should use the physical lower-speed/low-jerk region rather than independent random-walk constants.\n\n"
           << "## 19. Fixed-Wing audit\n\nCurrent classification: `" << classification(synthetic.at("Fixed-Wing"), envelope, false) << "`. Recalibrate toward broad physical sweeps with a separately bounded reversal density; do not change the existing historical data.\n\n"
           << "## 20. Space-Sim audit\n\nCurrent classification: `" << classification(synthetic.at("Space-Sim"), envelope, false) << "`. Recalibrate toward a mixture of precision and sustained off-center motion, and keep sensor effects orthogonal.\n\n"
           << "## 21. Noisy Sensor confounding analysis\n\nSee `noisy_sensor_confounding_audit.md`.\n\n## 22. Stress taxonomy\n\nSee `stress_taxonomy.csv`; difficult motion is tagged by mathematical mechanism, not called Combat by default.\n\n"
           << "## 23. Very-fast ballistic comparison\n\nSee `ballistic_vs_combat_analysis.csv`. Coherent ballistic velocity can retain high safe horizon because direction/acceleration remain structured, unlike stochastic frequent-reversal Combat.\n\n"
           << "## 24. Historical Monster/Phase 2–3 reinterpretation\n\n" << "See `historical_evidence_reinterpretation.md`.\n\n## 25. R0/R0.5 reinterpretation\n\nSee the same mapping; historical results are preserved but human claims require requalification.\n\n## 26. R0.6 reinterpretation\n\nSee the same mapping; contraction/safety remains evidence.\n\n## 27. R0.7 reinterpretation\n\nSee `r07_reinterpretation.md`; its PARTIAL disposition is unchanged.\n\n"
           << "## 28. Corrected generator validation\n\nEach corrected family contains 600 fresh trajectories; results are in `corrected_generator_validation.csv`. No Adaptive Response prediction was run.\n\n"
           << "## 29. Future human-quality gates\n\nSee `future_acceptance_policy.md`.\n\n## 30. Future torture-safety gates\n\nSee `future_acceptance_policy.md`.\n\n"
           << "## 31. Whether R0.7 requires requalification\n\nYes. Human-quality claims require fresh realistic and human-extreme Combat corpora separated from torture.\n\n"
           << "## 32. Recommended R0.7R design\n\nReuse R0.7 error-bound methodology with fresh development/blind corpora and distinct human-quality versus torture-safety gates. Do not begin it in this pass.\n\n"
           << "## 33. Production equivalence\n\nThe audit target links no production source; `production_equivalence.csv` records this compile-time isolation.\n\n"
           << "## 34. Final disposition\n\nV2.4.R0.65 PASS — HUMAN MOTION ENVELOPE DEFINED — CORPUS RECALIBRATION REQUIRED BEFORE R0.7R\n";

    const std::string combatForensic = "metric,summary_scope,unit,existing_combat_historical_value,human_p95,human_p99,human_observed_max,human_extreme,exceeds_human_p95_fraction,exceeds_human_p99_fraction,exceeds_human_observed_max_fraction,exceeds_human_extreme_fraction\n"
        "velocity,per_trace_peak_median,normalized_units_per_s," + number(combat.tracePeakSpeed.q(.50)) + ',' + number(human.tracePeakSpeed.q(.95)) + ',' + number(human.tracePeakSpeed.q(.99)) + ',' + number(human.tracePeakSpeed.maximum()) + ',' + number(human.tracePeakSpeed.maximum() * 1.15) + ',' + number(combat.tracePeakSpeed.fractionAbove(human.tracePeakSpeed.q(.95))) + ',' + number(combat.tracePeakSpeed.fractionAbove(human.tracePeakSpeed.q(.99))) + ',' + number(combat.tracePeakSpeed.fractionAbove(human.tracePeakSpeed.maximum())) + ',' + number(combat.tracePeakSpeed.fractionAbove(human.tracePeakSpeed.maximum() * 1.15)) + "\n"
        "velocity,all_source_segments,normalized_units_per_s," + number(combat.speed.q(.50)) + ',' + number(human.speed.q(.95)) + ',' + number(human.speed.q(.99)) + ',' + number(human.speed.maximum()) + ',' + number(envelope.extremeSpeed) + ',' + number(combat.speed.fractionAbove(human.speed.q(.95))) + ',' + number(combat.speed.fractionAbove(human.speed.q(.99))) + ',' + number(combat.speed.fractionAbove(human.speed.maximum())) + ',' + number(combat.speed.fractionAbove(envelope.extremeSpeed)) + "\n"
        "acceleration,p95_source_derivative,normalized_units_per_s2," + number(combat.acceleration.q(.95)) + ',' + number(human.acceleration.q(.95)) + ',' + number(human.acceleration.q(.99)) + ',' + number(human.acceleration.maximum()) + ',' + number(envelope.extremeAcceleration) + ',' + number(combat.acceleration.fractionAbove(human.acceleration.q(.95))) + ',' + number(combat.acceleration.fractionAbove(human.acceleration.q(.99))) + ',' + number(combat.acceleration.fractionAbove(human.acceleration.maximum())) + ',' + number(combat.acceleration.fractionAbove(envelope.extremeAcceleration)) + "\n"
        "jerk,p95_source_derivative,normalized_units_per_s3," + number(combat.jerk.q(.95)) + ',' + number(human.jerk.q(.95)) + ',' + number(human.jerk.q(.99)) + ',' + number(human.jerk.maximum()) + ',' + number(envelope.extremeJerk) + ',' + number(combat.jerk.fractionAbove(human.jerk.q(.95))) + ',' + number(combat.jerk.fractionAbove(human.jerk.q(.99))) + ',' + number(combat.jerk.fractionAbove(human.jerk.maximum())) + ',' + number(combat.jerk.fractionAbove(envelope.extremeJerk)) + "\n"
        "reversal_density,per_trace,reversals_per_s," + number(combat.reversalDensity.q(.50)) + ',' + number(human.reversalDensity.q(.95)) + ',' + number(human.reversalDensity.q(.99)) + ',' + number(human.reversalDensity.maximum()) + ',' + number(envelope.extremeReversalDensity) + ',' + number(combat.reversalDensity.fractionAbove(human.reversalDensity.q(.95))) + ',' + number(combat.reversalDensity.fractionAbove(human.reversalDensity.q(.99))) + ',' + number(combat.reversalDensity.fractionAbove(human.reversalDensity.maximum())) + ',' + number(combat.reversalDensity.fractionAbove(envelope.extremeReversalDensity)) + "\n"
        "reversal_spacing,p10_seconds,seconds," + number(combat.reversalSpacing.q(.10)) + ',' + number(human.reversalSpacing.q(.95)) + ',' + number(human.reversalSpacing.q(.99)) + ',' + number(human.reversalSpacing.maximum()) + ',' + number(envelope.extremeMinReversalSpacing) + ",,,,\n"
        "cadence,p95_source_interval,milliseconds," + number(combat.cadence.q(.95) * 1000.0) + ',' + number(human.cadence.q(.95) * 1000.0) + ',' + number(human.cadence.q(.99) * 1000.0) + ',' + number(human.cadence.maximum() * 1000.0) + ',' + number(human.cadence.q(.95) * 1000.0) + ",,,,\n"
        "stops,per_trace_count,count," + number(combat.stops / std::max<size_t>(1, combat.traces)) + ',' + number(human.stops / std::max<size_t>(1, human.traces)) + ",,,, ,,,,\n";

    const std::array<std::pair<std::string, std::string>, 16> files{{
        {"manifest.json", manifest}, {"baseline_identity.json", identity}, {"human_motion_envelope.csv", envelopeCsv.str()}, {"physical_trace_motion_profiles.csv", profiles.str()},
        {"human_envelope_definition.md", envelopeDefinition}, {"synthetic_family_audit.csv", syntheticCsv.str()}, {"combat_forensic_audit.csv", combatForensic}, {"persona_reclassification.csv", reclassification.str()},
        {"stress_taxonomy.csv", stress}, {"noisy_sensor_confounding_audit.md", noisy}, {"corrected_generator_parameters.json", parameters}, {"corrected_generator_validation.csv", validation.str()},
        {"historical_evidence_reinterpretation.md", historicalReinterpretation}, {"r07_reinterpretation.md", r07Text}, {"ballistic_vs_combat_analysis.csv", ballisticCsv.str()}, {"future_acceptance_policy.md", policy}
    }};
    for (const auto &[name, contents] : files) if (!writeText(input.output / name, contents)) return 4;
    if (!writeText(input.output / "production_equivalence.csv", equivalence)
        || !writeText(input.output / "V2.4.R0.65_Adaptive_Response_Human_Motion_Envelope_Audit.md", report.str())) return 4;
    if (!input.report.empty() && !writeText(input.report, report.str())) return 4;
    std::cout << "V2.4.R0.65 PASS — HUMAN MOTION ENVELOPE DEFINED — CORPUS RECALIBRATION REQUIRED BEFORE R0.7R\n";
    return 0;
}
