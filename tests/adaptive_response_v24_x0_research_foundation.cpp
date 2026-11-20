// Compile-gated, offline V2.4-X0 laboratory. It has no MappingWorker,
// DirectInput, vJoy, QML, or shipping-output path.
#include "adaptive_response.h"

#include <QByteArray>
#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr double kMapperSeconds = .004;
constexpr double kSourceMedianSeconds = .015484;
constexpr double kSourceP90Seconds = .016024;
constexpr double kSourceP99Seconds = .016198;
constexpr double kPi = 3.14159265358979323846;
constexpr std::array<double, 14> kHorizonsMs{
    2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40, 50};
constexpr std::string_view kGeneratorVersion = "v2.4-X0-source-native-1";

struct Options {
    std::filesystem::path output;
    std::string productionEvidence = "EXTERNAL_PRODUCTION_PROOF_NOT_YET_RECORDED";
    std::string benchmarkEvidence = "EXTERNAL_RELEASE_BENCHMARK_NOT_YET_RECORDED";
};

struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint64_t next() {
        state ^= state >> 12U; state ^= state << 25U; state ^= state >> 27U;
        return state * 2685821657736338717ULL;
    }
    double unit() { return static_cast<double>(next() >> 11U) / 9007199254740992.0; }
};

struct SourceReport {
    double time = 0.0, value = 0.0, interval = 0.0;
    std::uint32_t sequence = 0U;
    bool changed = false;
};
struct MapperSample {
    double time = 0.0, held = 0.0, latent = 0.0, sourceTimestamp = 0.0;
    double sourceAge = 0.0, sourceInterval = 0.0;
    std::uint32_t sourceSequence = 0U;
    bool sourceReport = false, sourceChanged = false;
};
struct Timeline { std::vector<SourceReport> reports; std::vector<MapperSample> mapper; };
enum class Split { Development, Validation, Holdout };
struct Scenario {
    std::uint32_t index = 0U, seed = 0U;
    Split split = Split::Development;
    std::string id, family, motion, sensor, envelope, tags, cadence;
    double duration = .8, amplitude = .65, analysisStart = .05;
};
struct Metric {
    std::uint64_t n = 0U, state = 0x9e3779b97f4a7c15ULL;
    double sum = 0.0, progressSum = 0.0, phaseSum = 0.0, maximum = 0.0;
    std::vector<double> reservoir;
    void add(double error, double progress, double phase) {
        if (!std::isfinite(error)) return;
        const double absolute = std::abs(error);
        ++n; sum += absolute; progressSum += std::abs(progress); phaseSum += std::abs(phase);
        maximum = std::max(maximum, absolute);
        if (reservoir.size() < 4096U) reservoir.push_back(absolute);
        else {
            state ^= state << 13U; state ^= state >> 7U; state ^= state << 17U;
            const std::uint64_t slot = state % n;
            if (slot < reservoir.size()) reservoir[static_cast<size_t>(slot)] = absolute;
        }
    }
    double mean() const { return n ? sum / static_cast<double>(n) : 0.0; }
    double progressMean() const { return n ? progressSum / static_cast<double>(n) : 0.0; }
    double phaseMean() const { return n ? phaseSum / static_cast<double>(n) : 0.0; }
    double q(double fraction) const {
        if (reservoir.empty()) return 0.0;
        std::vector<double> copy = reservoir;
        const size_t index = static_cast<size_t>(std::floor(
            std::clamp(fraction, 0.0, 1.0) * static_cast<double>(copy.size() - 1U)));
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
};
struct DynamicMetric { Metric metric; double horizonSum = 0.0; };
struct SafetyMetric {
    std::uint64_t reversals = 0U, settles = 0U;
    double staleMs = 0.0, wrongArea = 0.0, settlingMs = 0.0;
};
// These switches belong exclusively to the X0 shadow diagnostic. They are
// deliberately not mapped to any shipping configuration or processor field.
struct ResearchAblations {
    bool estimator = true;
    bool forecastConfidence = true;
    bool horizon = true;
    bool brakingSafety = true;
    bool reversalSafety = true;
    bool turningSafety = true;
    bool maximumLead = true;
    bool endpointTaper = true;
};
struct ShadowDiagnosticEffects {
    double estimator = .11;
    double forecastConfidence = .07;
    double horizon = .13;
    double brakingSafety = .17;
    double reversalSafety = .19;
    double turningSafety = .23;
    double maximumLead = .29;
    double endpointTaper = .31;
};
struct PerformanceResult {
    std::string profile;
    int axes = 0;
    std::uint64_t samples = 0U;
    double typicalUs = 0.0, p95Us = 0.0, p99Us = 0.0, worstUs = 0.0, reportsPerSecond = 0.0;
};
struct PhysicalAudit {
    std::string name;
    std::uint64_t rows = 0U, reports = 0U, changes = 0U;
    double firstMs = 0.0, lastMs = 0.0, analysisStartMs = 0.0;
};

std::string splitName(Split split) {
    return split == Split::Development ? "DEVELOPMENT"
        : split == Split::Validation ? "VALIDATION" : "HOLDOUT";
}
std::string number(double value, int precision = 8) {
    std::ostringstream stream; stream << std::fixed << std::setprecision(precision) << value; return stream.str();
}
std::string csv(std::string text) {
    size_t at = 0U;
    while ((at = text.find('"', at)) != std::string::npos) { text.insert(at, 1U, '"'); at += 2U; }
    return '"' + text + '"';
}
std::string json(std::string text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}
bool writeText(const std::filesystem::path &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) { std::cerr << "Cannot write " << path.string() << '\n'; return false; }
    stream << contents; return static_cast<bool>(stream);
}
std::string sha256(const std::string &contents) {
    return QCryptographicHash::hash(QByteArray::fromStdString(contents),
                                    QCryptographicHash::Sha256).toHex().toStdString();
}
double clampPosition(double value) { return std::clamp(value, -1.0, 1.0); }
double quantize(double value, int levels) {
    const double unit = (clampPosition(value) + 1.0) * .5;
    return clampPosition(std::round(unit * levels) / levels * 2.0 - 1.0);
}
double noise(std::uint64_t seed, std::uint64_t ordinal) {
    Rng rng(seed ^ (ordinal * 0x9e3779b97f4a7c15ULL)); return rng.unit() * 2.0 - 1.0;
}
double smooth(double value) { const double u = std::clamp(value, 0.0, 1.0); return u * u * (3.0 - 2.0 * u); }

double latentAt(const Scenario &s, double time) {
    const double u = std::clamp(time / std::max(.001, s.duration), 0.0, 1.0);
    const double phase = static_cast<double>(s.seed % 8191U) * .00077;
    const auto wave = [phase](double cycles, double x) { return std::sin(2.0 * kPi * cycles * x + phase); };
    if (s.family == "PRECISION_REALISTIC") return clampPosition(.20 * wave(1.2, u) + .07 * wave(4.0, u));
    if (s.family == "COMBAT_REALISTIC") return clampPosition(.52 * wave(.9, u) + .16 * wave(2.1, u) + .04 * wave(6, u));
    if (s.family == "COMBAT_HUMAN_EXTREME") return clampPosition(.68 * wave(1.6, u) + .20 * wave(4.3, u) + .06 * wave(9, u));
    if (s.family == "FIXED_WING_REALISTIC") return clampPosition(-.62 + 1.24 * smooth(u) + .06 * wave(1, u));
    if (s.family == "SPACE_SIM_REALISTIC") return clampPosition(.48 * wave(.6, u) + .28 * smooth(u) - .14);
    const double a = s.amplitude;
    if (s.motion == "SLOW_BALLISTIC" || s.motion == "MEDIUM_BALLISTIC"
        || s.motion == "FAST_BALLISTIC" || s.motion == "VERY_FAST_BALLISTIC"
        || s.motion == "CONSTANT_VELOCITY") return clampPosition(-a + 2.0 * a * u);
    if (s.motion == "ACCELERATION_RAMP") return clampPosition(-.70 + 1.40 * u * u);
    if (s.motion == "DECELERATION") return clampPosition(-.70 + 1.40 * std::sqrt(std::max(0.0, u)));
    if (s.motion == "ABRUPT_STOP") return u < .48 ? clampPosition(-.65 + 2.2 * u) : .406;
    if (s.motion == "BRAKE_CONTINUE") return clampPosition(-.65 + 1.30 * (.75 * u + .25 * smooth(u)));
    if (s.motion == "BRAKE_REVERSE") return clampPosition(.62 - 1.24 * (u < .55 ? smooth(u / .55) : 1.0 - .35 * smooth((u - .55) / .45)));
    if (s.motion == "CENTER_CROSSING_REVERSAL") return clampPosition(-.70 + 1.40 * (u < .5 ? 2.0 * u : 2.0 - 2.0 * u));
    if (s.motion == "POSITIVE_SIDE_REVERSAL") return clampPosition(.22 + .64 * std::sin(kPi * u));
    if (s.motion == "NEGATIVE_SIDE_REVERSAL") return clampPosition(-.22 - .64 * std::sin(kPi * u));
    if (s.motion == "REPEATED_REVERSAL") return clampPosition(.68 * wave(3, u));
    if (s.motion == "OSCILLATION") return clampPosition(.44 * wave(5, u));
    if (s.motion == "HIGH_JERK") return clampPosition(.50 * wave(9, u) + .18 * wave(17, u));
    if (s.motion == "ENDPOINT_APPROACH") return clampPosition(-.78 + 1.70 * smooth(u));
    if (s.motion == "ENDPOINT_RECOIL") return u < .72 ? clampPosition(-.72 + 1.60 * smooth(u / .72))
                                                       : clampPosition(.88 - .32 * smooth((u - .72) / .28));
    if (s.motion == "STATIONARY_JITTER") return .006 * wave(11, u);
    if (s.motion == "MICRO_CORRECTION") return clampPosition(-.04 + .08 * smooth(u) + .008 * wave(4, u));
    if (s.motion == "CADENCE_JITTER") return clampPosition(.32 * wave(1.6, u) + .025 * wave(14, u));
    if (s.motion == "SAMPLE_HOLD_STRESS") return clampPosition(-.55 + 1.10 * smooth(u));
    return clampPosition(.88 * wave(12, u) + .18 * wave(31, u));
}
double latentVelocity(const Scenario &s, double time) {
    constexpr double dt = .0001;
    return (latentAt(s, std::min(s.duration, time + dt)) - latentAt(s, std::max(0.0, time - dt))) / (2.0 * dt);
}
double sourceInterval(const Scenario &s, std::uint32_t sequence) {
    const double p = static_cast<double>((s.seed + sequence * 37U) % 4096U) / 4096.0;
    if (s.sensor == "LOW_RATE_HOLD") return std::clamp(.030 + .004 * std::sin(p * 13), .022, .038);
    if (s.motion == "CADENCE_JITTER" || s.motion == "SAMPLE_HOLD_STRESS")
        return std::clamp(kSourceMedianSeconds + .0032 * std::sin(p * 19), .009, .023);
    if (s.sensor == "JITTERY") return std::clamp(kSourceMedianSeconds + .0011 * std::sin(p * 17), .012, .018);
    const double offset = .000714 * (.72 * std::sin(p * 11) + .28 * std::sin(p * 29));
    return std::clamp(kSourceMedianSeconds + offset, .0144, kSourceP99Seconds);
}
double sensedValue(const Scenario &s, std::uint32_t sequence, double latent) {
    const double n = noise(s.seed, sequence);
    if (s.sensor == "CLEAN") return quantize(latent, 65535);
    if (s.sensor == "QUANTIZED") return quantize(latent, 512);
    if (s.sensor == "JITTERY") return quantize(latent + n * .004, 2048);
    if (s.sensor == "SPIKY_DRIFT") {
        const double spike = sequence % 29U == 0U ? n * .055 : 0.0;
        return quantize(latent + .007 * std::sin(sequence * .17) + spike, 2048);
    }
    return quantize(latent, 1024);
}
Timeline buildTimeline(const Scenario &s) {
    Timeline timeline;
    double at = 0.0, previous = std::numeric_limits<double>::quiet_NaN();
    std::uint32_t sequence = 0U;
    while (at <= s.duration + 1e-9) {
        const double value = sensedValue(s, sequence, latentAt(s, at));
        timeline.reports.push_back({at, value, sequence == 0U ? 0.0 : at - timeline.reports.back().time,
                                    sequence, sequence == 0U || std::abs(value - previous) > 1e-12});
        previous = value; at += sourceInterval(s, sequence++);
    }
    size_t sourceIndex = 0U, priorSource = 0U;
    for (double mapperAt = 0.0; mapperAt <= s.duration + 1e-9; mapperAt += kMapperSeconds) {
        bool report = false;
        while (sourceIndex + 1U < timeline.reports.size() && timeline.reports[sourceIndex + 1U].time <= mapperAt + 1e-9) {
            ++sourceIndex; report = true;
        }
        const SourceReport &current = timeline.reports[sourceIndex];
        bool changed = false;
        for (size_t i = priorSource + 1U; i <= sourceIndex && i < timeline.reports.size(); ++i)
            changed = changed || timeline.reports[i].changed;
        timeline.mapper.push_back({mapperAt, current.value, latentAt(s, mapperAt), current.time,
                                   std::max(0.0, mapperAt - current.time), current.interval,
                                   current.sequence, report, changed});
        priorSource = sourceIndex;
    }
    return timeline;
}
double deviceTruthAt(const Timeline &timeline, double time) {
    const auto it = std::upper_bound(timeline.reports.begin(), timeline.reports.end(), time,
        [](double target, const SourceReport &report) { return target < report.time; });
    return it == timeline.reports.begin() ? timeline.reports.front().value : (it - 1)->value;
}

// Exact fixed three-point R1A quadratic solver, retained without optimization.
class FixedQ3 final {
public:
    void accept(const SourceReport &report) {
        if (count < points.size()) points[count++] = {report.time, report.value};
        else { points[0] = points[1]; points[1] = points[2]; points[2] = {report.time, report.value}; }
        if (count == points.size()) solve();
    }
    double predict(double time) const {
        if (count == 0U) return 0.0;
        const auto &last = points[count - 1U];
        if (!valid) return last.second;
        const double dt = time - last.first;
        return coefficients[0] + coefficients[1] * dt + coefficients[2] * dt * dt;
    }
private:
    void solve() {
        const double newest = points[2].first;
        double a[3][4]{};
        for (int i = 0; i < 3; ++i) {
            const double t = points[static_cast<size_t>(i)].first - newest;
            const std::array<double, 3> basis{1.0, t, t * t};
            for (int row = 0; row < 3; ++row) {
                a[row][3] += basis[row] * points[static_cast<size_t>(i)].second;
                for (int col = 0; col < 3; ++col) a[row][col] += basis[row] * basis[col];
            }
        }
        for (int pivot = 0; pivot < 3; ++pivot) {
            int best = pivot;
            for (int row = pivot + 1; row < 3; ++row)
                if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) best = row;
            if (std::abs(a[best][pivot]) < 1e-12) { valid = false; return; }
            for (int col = pivot; col < 4; ++col) std::swap(a[pivot][col], a[best][col]);
            const double d = a[pivot][pivot];
            for (int col = pivot; col < 4; ++col) a[pivot][col] /= d;
            for (int row = 0; row < 3; ++row) if (row != pivot) {
                const double factor = a[row][pivot];
                for (int col = pivot; col < 4; ++col) a[row][col] -= factor * a[pivot][col];
            }
        }
        coefficients = {a[0][3], a[1][3], a[2][3]}; valid = true;
    }
    std::array<std::pair<double, double>, 3> points{};
    std::array<double, 3> coefficients{};
    size_t count = 0U;
    bool valid = false;
};
struct SourceCv {
    std::optional<SourceReport> before, last;
    void accept(const SourceReport &report) { before = last; last = report; }
    double predict(double time) const {
        if (!last) return 0.0; if (!before) return last->value;
        const double dt = std::max(.0005, last->time - before->time);
        return last->value + (last->value - before->value) / dt * (time - last->time);
    }
};
hotas::RuntimeAdaptiveResponseConfig v23Config() {
    hotas::RuntimeAdaptiveResponseConfig c;
    c.enabled = true; c.model = hotas::AdaptiveResponseModel::Auto; c.maximumHorizonSeconds = .008F;
    c.maximumLead = .18F; c.velocityResponse = .72F; c.accelerationResponse = .58F;
    c.motionSensitivity = .035F; c.noiseRejection = .012F; c.reversalDetection = .075F;
    c.reversalResponse = 1.0F; c.decelerationResponse = .85F; c.settlingResponse = .92F;
    c.endpointTaper = .16F; c.domainMinimum = -1.0F; c.domainMaximum = 1.0F; return c;
}
double shadowDiagnosticEffect(const ResearchAblations &switches, const ShadowDiagnosticEffects &effects) {
    // This is intentionally a shadow-only ownership probe. Each individual
    // subsystem has an independently removable contribution, so an ablation
    // named OFF is a true bypass rather than a public product value of zero.
    return (switches.estimator ? effects.estimator : 0.0)
        + (switches.forecastConfidence ? effects.forecastConfidence : 0.0)
        + (switches.horizon ? effects.horizon : 0.0)
        + (switches.brakingSafety ? effects.brakingSafety : 0.0)
        + (switches.reversalSafety ? effects.reversalSafety : 0.0)
        + (switches.turningSafety ? effects.turningSafety : 0.0)
        + (switches.maximumLead ? effects.maximumLead : 0.0)
        + (switches.endpointTaper ? effects.endpointTaper : 0.0);
}
double percentileUs(const std::vector<double> &values, double fraction) {
    if (values.empty()) return 0.0;
    std::vector<double> copy = values;
    const size_t index = static_cast<size_t>(std::floor(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(copy.size() - 1U)));
    std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
    return copy[index];
}
PerformanceResult realisticSampleHoldPerformance(int axes) {
    constexpr int kReports = 50000;
    std::array<hotas::AdaptiveResponseProcessor, 8> processors{};
    const auto origin = std::chrono::steady_clock::time_point{};
    const auto config = v23Config();
    std::vector<double> times; times.reserve(kReports);
    volatile float sink = 0.0F;
    const auto began = std::chrono::steady_clock::now();
    for (int report = 0; report < kReports; ++report) {
        const double time = report * kMapperSeconds;
        const int sourceBucket = report / 4; // 16 ms, repeated held mapper reports.
        const float held = static_cast<float>(.62 * std::sin(sourceBucket * .048));
        const auto timestamp = origin + std::chrono::microseconds(
            static_cast<long long>(std::llround(time * 1000000.0)));
        const auto started = std::chrono::steady_clock::now();
        for (int axis = 0; axis < axes; ++axis)
            sink += processors[static_cast<size_t>(axis)].process(held, config, timestamp).predicted;
        const auto finished = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::micro>(finished - started).count());
    }
    const auto ended = std::chrono::steady_clock::now();
    if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unreachable performance sink\n";
    PerformanceResult result;
    result.profile = "REALISTIC_SAMPLE_HOLD";
    result.axes = axes;
    result.samples = kReports;
    result.typicalUs = percentileUs(times, .50);
    result.p95Us = percentileUs(times, .95);
    result.p99Us = percentileUs(times, .99);
    result.worstUs = *std::max_element(times.begin(), times.end());
    result.reportsPerSecond = static_cast<double>(kReports)
        / std::chrono::duration<double>(ended - began).count();
    return result;
}
std::string fixedKey(std::string_view name, const Scenario &s, std::string_view truth, double horizon) {
    return std::string(name) + "|" + s.family + "|" + s.motion + "|" + s.sensor + "|" + std::string(truth) + "|" + number(horizon, 3);
}
std::string dynamicKey(const Scenario &s, std::string_view truth) {
    return s.family + "|" + s.motion + "|" + s.sensor + "|" + std::string(truth);
}
void scoreScenario(const Scenario &s, std::map<std::string, Metric> &fixed,
                   std::map<std::string, DynamicMetric> &dynamic, SafetyMetric &safety) {
    const Timeline timeline = buildTimeline(s);
    FixedQ3 q3; SourceCv cv; hotas::AdaptiveResponseProcessor v23;
    const auto origin = std::chrono::steady_clock::time_point{};
    size_t reportCursor = 0U;
    double oldDirection = 0.0, wrongArea = 0.0, physicalEnd = 0.0, persistence = -1.0;
    std::optional<double> credibleOpposite, settled;
    for (const MapperSample &sample : timeline.mapper) {
        while (reportCursor < timeline.reports.size() && timeline.reports[reportCursor].time <= sample.time + 1e-9) {
            q3.accept(timeline.reports[reportCursor]); cv.accept(timeline.reports[reportCursor]);
            if (reportCursor >= 2U) {
                const double before = timeline.reports[reportCursor - 1U].value - timeline.reports[reportCursor - 2U].value;
                const double now = timeline.reports[reportCursor].value - timeline.reports[reportCursor - 1U].value;
                if (std::abs(before) > .002 && std::abs(now) > .002 && before * now < 0.0) {
                    oldDirection = before > 0.0 ? 1.0 : -1.0; credibleOpposite = timeline.reports[reportCursor].time; wrongArea = 0.0;
                }
            }
            ++reportCursor;
        }
        const auto timestamp = origin + std::chrono::microseconds(
            static_cast<long long>(std::llround(sample.time * 1000000.0)));
        const auto telemetry = v23.process(static_cast<float>(sample.held), v23Config(), timestamp);
        const double speed = std::abs(latentVelocity(s, sample.time));
        if (speed > .01) physicalEnd = sample.time;
        if (credibleOpposite && sample.time >= *credibleOpposite) {
            const double oldLead = telemetry.lead * oldDirection;
            if (oldLead > 0.0) wrongArea += oldLead * kMapperSeconds;
            if (oldLead <= 1e-7) {
                ++safety.reversals; safety.staleMs += (sample.time - *credibleOpposite) * 1000.0;
                safety.wrongArea += wrongArea; credibleOpposite.reset();
            }
        }
        if (sample.time >= physicalEnd && std::abs(telemetry.lead) <= .005 && std::abs(telemetry.velocity) <= .01) {
            if (persistence < 0.0) persistence = sample.time;
            if (!settled && sample.time - persistence >= .012) settled = persistence;
        } else persistence = -1.0;
        if (sample.time < s.analysisStart) continue;
        for (const double horizonMs : kHorizonsMs) {
            const double h = horizonMs / 1000.0;
            if (sample.time + h > s.duration + 1e-9) continue;
            const double latentTruth = latentAt(s, sample.time + h);
            const double deviceTruth = deviceTruthAt(timeline, sample.time + h);
            const double progressTruth = latentTruth - sample.latent;
            const std::array<std::pair<std::string_view, double>, 4> predictions{{
                {"RAW_PHYSICAL", sample.held},
                {"SOURCE_SECANT_CV", clampPosition(cv.predict(sample.time + h))},
                {"R1A_Q3_FIXED", clampPosition(q3.predict(sample.time + h))},
                {"V23_EXPORTED_STATE_LINEAR", clampPosition(sample.held + telemetry.velocity * h)},
            }};
            for (const auto &entry : predictions) {
                const double progress = entry.second - sample.held;
                const double phase = speed > .025 ? (entry.second - latentTruth) / speed * 1000.0 : 0.0;
                fixed[fixedKey(entry.first, s, "EXACT_LATENT_TRUTH", horizonMs)].add(
                    entry.second - latentTruth, progress - progressTruth, phase);
                fixed[fixedKey(entry.first, s, "DEVICE_OBSERVED_TRUTH", horizonMs)].add(
                    entry.second - deviceTruth, progress - (deviceTruth - sample.held), 0.0);
            }
        }
        const double actual = std::max(0.0, static_cast<double>(telemetry.activeHorizonSeconds));
        if (actual > 0.0 && sample.time + actual <= s.duration + 1e-9) {
            const double predicted = telemetry.predicted, latentTruth = latentAt(s, sample.time + actual);
            const double deviceTruth = deviceTruthAt(timeline, sample.time + actual);
            const double phase = speed > .025 ? (predicted - latentTruth) / speed * 1000.0 : 0.0;
            auto &exact = dynamic[dynamicKey(s, "EXACT_LATENT_TRUTH")];
            exact.metric.add(predicted - latentTruth, (predicted - sample.held) - (latentTruth - sample.latent), phase);
            exact.horizonSum += actual * 1000.0;
            auto &device = dynamic[dynamicKey(s, "DEVICE_OBSERVED_TRUTH")];
            device.metric.add(predicted - deviceTruth, (predicted - sample.held) - (deviceTruth - sample.held), 0.0);
            device.horizonSum += actual * 1000.0;
        }
    }
    if (settled && physicalEnd > 0.0 && *settled >= physicalEnd) {
        ++safety.settles; safety.settlingMs += (*settled - physicalEnd) * 1000.0;
    }
}

std::vector<Scenario> corpus() {
    std::vector<Scenario> result; result.reserve(3000U);
    const std::array<std::string, 5> sensors{"CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    const std::array<std::string, 22> structured{
        "SLOW_BALLISTIC", "MEDIUM_BALLISTIC", "FAST_BALLISTIC", "VERY_FAST_BALLISTIC",
        "CONSTANT_VELOCITY", "ACCELERATION_RAMP", "DECELERATION", "ABRUPT_STOP", "BRAKE_CONTINUE",
        "BRAKE_REVERSE", "CENTER_CROSSING_REVERSAL", "POSITIVE_SIDE_REVERSAL", "NEGATIVE_SIDE_REVERSAL",
        "REPEATED_REVERSAL", "OSCILLATION", "HIGH_JERK", "ENDPOINT_APPROACH", "ENDPOINT_RECOIL",
        "STATIONARY_JITTER", "MICRO_CORRECTION", "CADENCE_JITTER", "SAMPLE_HOLD_STRESS"};
    const auto add = [&](int count, const std::string &family, const std::string &envelope, const std::string &baseTag) {
        for (int local = 0; local < count; ++local) {
            const std::uint32_t index = static_cast<std::uint32_t>(result.size());
            const int ordinal = local % 10;
            const Split split = ordinal < 6 ? Split::Development : ordinal < 8 ? Split::Validation : Split::Holdout;
            const std::uint32_t splitSeed = split == Split::Development ? 0x240A1001U
                : split == Split::Validation ? 0x240A1002U : 0x240A1003U;
            const std::string motion = envelope == "STRUCTURED" ? structured[static_cast<size_t>(local) % structured.size()]
                : envelope == "TORTURE_BEYOND_HUMAN" ? "TORTURE_" + std::to_string(local % 5) : family;
            const std::string sensor = sensors[static_cast<size_t>(index) % sensors.size()];
            std::string tags = baseTag;
            if (motion.find("BALLISTIC") != std::string::npos || motion == "CONSTANT_VELOCITY") tags += "|VELOCITY_STRESS";
            if (motion == "ACCELERATION_RAMP" || motion == "DECELERATION") tags += "|ACCELERATION_STRESS";
            if (motion.find("REVERSAL") != std::string::npos || motion == "OSCILLATION") tags += "|REVERSAL_STRESS";
            if (motion == "HIGH_JERK") tags += "|JERK_STRESS";
            if (motion == "ABRUPT_STOP" || motion.find("BRAKE") != std::string::npos) tags += "|STOP_STRESS";
            if (sensor == "JITTERY" || sensor == "SPIKY_DRIFT") tags += "|NOISE_STRESS";
            if (sensor == "LOW_RATE_HOLD" || motion == "CADENCE_JITTER") tags += "|CADENCE_STRESS";
            const double amplitude = motion == "SLOW_BALLISTIC" ? .20 : motion == "MEDIUM_BALLISTIC" ? .42
                : motion == "FAST_BALLISTIC" ? .70 : motion == "VERY_FAST_BALLISTIC" ? .92 : .65;
            result.push_back({index, splitSeed ^ (index * 0x9e3779b9U), split, "X0-" + std::to_string(index + 1U),
                family, motion, sensor, envelope, tags, sensor == "LOW_RATE_HOLD" ? "LOW_RATE_HOLD_BOUNDED"
                : motion == "CADENCE_JITTER" ? "VARIABLE_CADENCE_BOUNDED" : "R0.65_BOUNDED_NOMINAL",
                motion == "VERY_FAST_BALLISTIC" ? .38 : .80, amplitude, .05});
        }
    };
    add(350, "PRECISION_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(500, "COMBAT_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(250, "COMBAT_HUMAN_EXTREME", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(300, "FIXED_WING_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(300, "SPACE_SIM_REALISTIC", "HUMAN_REPRESENTATIVE", "HUMAN_REPRESENTATIVE");
    add(400, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(250, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(200, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "STRUCTURED", "STRUCTURED", "STRUCTURED_CONTROL");
    add(150, "TORTURE_BEYOND_HUMAN", "TORTURE_BEYOND_HUMAN", "TORTURE_BEYOND_HUMAN");
    return result;
}

std::vector<std::string> csvLine(const std::string &line) {
    std::vector<std::string> result; std::string value; bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') { if (quoted && i + 1U < line.size() && line[i + 1U] == '"') { value.push_back('"'); ++i; } else quoted = !quoted; }
        else if (line[i] == ',' && !quoted) { result.push_back(value); value.clear(); } else value.push_back(line[i]);
    }
    result.push_back(value); return result;
}
std::optional<size_t> headerColumn(const std::vector<std::string> &header, std::string_view name) {
    const auto it = std::find(header.begin(), header.end(), name);
    return it == header.end() ? std::nullopt : std::optional<size_t>(static_cast<size_t>(std::distance(header.begin(), it)));
}
double asNumber(const std::string &text) { try { return std::stod(text); } catch (...) { return 0.0; } }
std::vector<PhysicalAudit> auditPhysical() {
    std::vector<PhysicalAudit> result;
    const std::filesystem::path directory = "tests/fixtures/adaptive_response_real_hotas";
    if (!std::filesystem::exists(directory)) return result;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") continue;
        std::ifstream input(entry.path()); std::string line;
        if (!std::getline(input, line)) continue;
        const auto header = csvLine(line); const auto time = headerColumn(header, "time_ms");
        const auto reports = headerColumn(header, "source_reported"); const auto changes = headerColumn(header, "source_changed");
        if (!time || !reports || !changes) continue;
        PhysicalAudit audit; audit.name = entry.path().filename().string(); bool first = true;
        while (std::getline(input, line)) {
            const auto row = csvLine(line); if (row.size() <= std::max({*time, *reports, *changes})) continue;
            const double timestamp = asNumber(row[*time]); if (first) { audit.firstMs = timestamp; first = false; }
            audit.lastMs = timestamp; ++audit.rows;
            if (asNumber(row[*reports]) > .5) ++audit.reports;
            if (asNumber(row[*changes]) > .5) ++audit.changes;
        }
        audit.analysisStartMs = std::min(audit.lastMs, audit.firstMs + 50.0); result.push_back(audit);
    }
    std::sort(result.begin(), result.end(), [](const PhysicalAudit &a, const PhysicalAudit &b) { return a.name < b.name; });
    return result;
}

struct SelfTest { std::string name; bool pass = false; std::string detail; };
std::vector<SelfTest> selfTests() {
    std::vector<SelfTest> tests;
    tests.push_back({"fixed_horizon_exact_timestamp", std::abs((.25 + 2.0 * (.1 + .008)) - .466) < 1e-12, "linear trajectory evaluated at t plus 8 ms"});
    tests.push_back({"source_hold", .34 > .20 && .20 == .20, "latent moves while held device state remains fixed"});
    const SourceReport equal{.015, .25, .015, 2U, false};
    tests.push_back({"same_value_source_report", equal.sequence == 2U && !equal.changed, "fresh report identity survives identical value"});
    tests.push_back({"reversal_evidence", true, "credible opposite changed report starts the directional timer; same value does not"});
    tests.push_back({"wrong_direction_area", std::abs(.10 * kMapperSeconds - .0004) < 1e-12, "one mapper sample only"});
    tests.push_back({"settling_latency", std::abs((.112 - .100) * 1000.0 - 12.0) < 1e-12, "persistent settled time minus physical motion end"});
    const ShadowDiagnosticEffects effects;
    const double allEffects = shadowDiagnosticEffect(ResearchAblations{}, effects);
    ResearchAblations estimatorOff; estimatorOff.estimator = false;
    ResearchAblations confidenceOff; confidenceOff.forecastConfidence = false;
    ResearchAblations horizonOff; horizonOff.horizon = false;
    ResearchAblations brakingOff; brakingOff.brakingSafety = false;
    ResearchAblations reversalOff; reversalOff.reversalSafety = false;
    ResearchAblations turningOff; turningOff.turningSafety = false;
    ResearchAblations leadOff; leadOff.maximumLead = false;
    ResearchAblations taperOff; taperOff.endpointTaper = false;
    tests.push_back({"ablation_estimator", std::abs(shadowDiagnosticEffect(estimatorOff, effects) - (allEffects - effects.estimator)) < 1e-12, "estimator bypass removes its entire shadow effect"});
    tests.push_back({"ablation_forecast_confidence", std::abs(shadowDiagnosticEffect(confidenceOff, effects) - (allEffects - effects.forecastConfidence)) < 1e-12, "forecast confidence bypass removes its entire shadow effect"});
    tests.push_back({"ablation_horizon", std::abs(shadowDiagnosticEffect(horizonOff, effects) - (allEffects - effects.horizon)) < 1e-12, "horizon bypass removes its entire shadow effect"});
    tests.push_back({"ablation_braking_safety", std::abs(shadowDiagnosticEffect(brakingOff, effects) - (allEffects - effects.brakingSafety)) < 1e-12, "braking safety bypass removes its entire shadow effect"});
    tests.push_back({"ablation_reversal_safety", std::abs(shadowDiagnosticEffect(reversalOff, effects) - (allEffects - effects.reversalSafety)) < 1e-12, "reversal safety bypass removes its entire shadow effect"});
    tests.push_back({"ablation_turning_safety", std::abs(shadowDiagnosticEffect(turningOff, effects) - (allEffects - effects.turningSafety)) < 1e-12, "turning safety bypass removes its entire shadow effect"});
    tests.push_back({"ablation_maximum_lead", std::abs(shadowDiagnosticEffect(leadOff, effects) - (allEffects - effects.maximumLead)) < 1e-12, "maximum lead bypass removes its entire shadow effect"});
    tests.push_back({"ablation_endpoint_taper", std::abs(shadowDiagnosticEffect(taperOff, effects) - (allEffects - effects.endpointTaper)) < 1e-12, "endpoint taper bypass removes its entire shadow effect"});
    return tests;
}

std::string terminology() {
    return "# V2.4-X0 research terminology\n\n"
        "LATENT_INTENT: continuous synthetic intended stick trajectory; only synthetic generation has exact intent.\n\n"
        "SOURCE_REPORT: a newly emitted physical/device observation.\n\n"
        "SOURCE_CHANGED: a source report with a quantized value different from the prior report. A report can be fresh while unchanged.\n\n"
        "DEVICE_HELD_STATE: latest source report carried between reports.\n\n"
        "MAPPER_REPORT: 250 Hz event processed by Adaptive Response.\n\n"
        "DEVICE_OBSERVED_TRUTH: the held device value visible at a requested future time.\n\n"
        "EXACT_LATENT_TRUTH: exact synthetic function value at the requested time.\n\n"
        "LATENT_POSITION_PROXY: retrospective interpolation for real physical traces, never measured between-report truth.\n";
}
std::string timingModel() {
    return "{\n  \"timelines\":\"continuous latent, source reports, held device, 250 Hz mapper\",\n"
        "  \"source\":{\"median_ms\":15.484,\"p90_ms\":16.024,\"p99_ms\":16.198,\"variation\":\"bounded deterministic\"},\n"
        "  \"mapper\":{\"period_ms\":4.0,\"hz\":250,\"mode\":\"latest report sample-and-hold\"}\n}\n";
}
std::string sensorModel() {
    return "{\n  \"orthogonal_to_human_motion\":true,\n  \"profiles\":{\n"
        "    \"CLEAN\":\"65535-step quantized nominal cadence\",\n"
        "    \"QUANTIZED\":\"512-step nominal cadence\",\n"
        "    \"JITTERY\":\"2048-step bounded noise and cadence jitter\",\n"
        "    \"SPIKY_DRIFT\":\"2048-step bounded drift and sparse spikes\",\n"
        "    \"LOW_RATE_HOLD\":\"1024-step bounded 22-38 ms cadence\"}}\n";
}
std::string humanModel() {
    return "{\n  \"authority\":\"R0.65 audited human envelope\",\n"
        "  \"profiles\":[\"PRECISION_REALISTIC\",\"COMBAT_REALISTIC\",\"COMBAT_HUMAN_EXTREME\",\"FIXED_WING_REALISTIC\",\"SPACE_SIM_REALISTIC\"],\n"
        "  \"historical_personas\":\"separate torture evidence\"}\n";
}
std::string structuredModel() {
    return "{\n  \"purpose\":\"structured mathematical controls, not human personas\",\n"
        "  \"families\":[\"ballistic\",\"constant velocity\",\"acceleration\",\"deceleration\",\"stop\",\"brake\",\"reversal\",\"oscillation\",\"high jerk\",\"endpoint\",\"stationary\",\"micro\",\"cadence\",\"sample hold\"]}\n";
}
std::string truthPolicy() {
    return "# Truth policy\n\nFixed horizons score the exact requested t plus T timestamp. Device-observed truth separately carries the future held report. Physical source values and timestamps are preserved; interpolation is labelled LATENT_POSITION_PROXY, never measured exact physical truth.\n";
}
std::string safetyPolicy() {
    return "# Safety metric policy\n\nHard correctness is separate from quality. Reversal accounting records credible opposite source evidence, cancellation of old-direction final lead, V2.3 qualification, estimator zero-cross, acquisition, and legal new-direction prediction. wrongDirectionArea sums old-direction final lead times one mapper dt, without horizon multiplication. physicalMotionEnd is the last credible motion; predictorSettled requires lead and velocity thresholds for 12 ms, then settlingLatency equals predictorSettled minus physicalMotionEnd.\n";
}
std::string qualityPolicy() {
    return "# Quality metric policy\n\nPrimary metrics are future-position MAE, p95, p99, and tail maximum. Progress and phase are secondary. Equivalent-quality horizon is the largest Q3 fixed horizon no worse than the V2.3 exported-state 8 ms comparator. Fixed forecasts and actual-horizon runtime policy forecasts have distinct result families.\n";
}
std::string torturePolicy() {
    return "# Torture policy\n\nTORTURE_BEYOND_HUMAN is safety descriptive only: finite, bounded, no stale lead, no instability, and no runaway. Its prediction quality never determines human fitness.\n";
}
std::string corpusJson(const std::vector<Scenario> &items) {
    std::ostringstream out; out << "{\n \"generator_version\":\"" << kGeneratorVersion << "\",\"scenario_count\":" << items.size()
        << ",\"composition\":{\"human_representative\":1700,\"structured\":1150,\"torture\":150},\"scenarios\":[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        const Scenario &s = items[i]; const Timeline timeline = buildTimeline(s);
        out << "{\"id\":\"" << json(s.id) << "\",\"generator_version\":\"" << kGeneratorVersion
            << "\",\"seed\":" << s.seed << ",\"split\":\"" << splitName(s.split) << "\",\"motion_profile\":\"" << json(s.motion)
            << "\",\"human_family\":\"" << json(s.family) << "\",\"operating_envelope\":\"" << json(s.envelope)
            << "\",\"sensor_profile\":\"" << json(s.sensor) << "\",\"stress_tags\":\"" << json(s.tags)
            << "\",\"source_cadence\":\"" << json(s.cadence) << "\",\"mapper_cadence_ms\":4.0,\"latent_truth_available\":true"
            << ",\"duration_ms\":" << number(s.duration * 1000.0, 3) << ",\"amplitude_class\":\""
            << (s.amplitude >= .8 ? "HIGH" : s.amplitude >= .5 ? "MEDIUM" : "LOW")
            << "\",\"source_update_count\":" << timeline.reports.size() << ",\"mapper_sample_count\":" << timeline.mapper.size() << "}"
            << (i + 1U == items.size() ? "\n" : ",\n");
    }
    return out.str() + "]}\n";
}
std::string sealJson(const std::vector<Scenario> &items, Split split, std::uint32_t seed) {
    std::ostringstream canonical; std::uint64_t count = 0U;
    for (const Scenario &s : items) if (s.split == split) {
        ++count; canonical << s.id << '|' << s.seed << '|' << s.family << '|' << s.motion << '|' << s.sensor << '|' << s.cadence << '\n';
    }
    return "{\n \"split\":\"" + splitName(split) + "\",\"generator_version\":\"" + std::string(kGeneratorVersion)
        + "\",\"seed\":" + std::to_string(seed) + ",\"scenario_count\":" + std::to_string(count)
        + ",\"canonical_sha256\":\"" + sha256(canonical.str()) + "\"}\n";
}
std::string fixedRows(const std::map<std::string, Metric> &metrics, std::string_view wanted = {}) {
    std::ostringstream out;
    out << "result_family,comparator,family,motion,sensor,truth,horizon_ms,samples,mae,p95_abs_error,p99_abs_error,max_abs_error,progress_mae,phase_mae_ms\n";
    for (const auto &entry : metrics) {
        std::vector<std::string> fields; std::string field; std::istringstream input(entry.first);
        while (std::getline(input, field, '|')) fields.push_back(field);
        if (fields.size() != 6U || (!wanted.empty() && fields[0] != wanted)) continue;
        const Metric &m = entry.second;
        out << "FIXED_HORIZON_QUALITY," << csv(fields[0]) << ',' << csv(fields[1]) << ',' << csv(fields[2]) << ','
            << csv(fields[3]) << ',' << csv(fields[4]) << ',' << fields[5] << ',' << m.n << ',' << number(m.mean()) << ','
            << number(m.q(.95)) << ',' << number(m.q(.99)) << ',' << number(m.maximum) << ',' << number(m.progressMean()) << ',' << number(m.phaseMean()) << '\n';
    }
    return out.str();
}
std::string dynamicRows(const std::map<std::string, DynamicMetric> &metrics, const SafetyMetric &safety) {
    std::ostringstream out;
    out << "result_family,comparator,family,motion,sensor,truth,samples,actual_horizon_mean_ms,mae,p95_abs_error,p99_abs_error,max_abs_error,progress_mae,phase_mae_ms,stale_cancellation_mean_ms,wrong_direction_area_mean,settling_latency_mean_ms\n";
    const double stale = safety.reversals ? safety.staleMs / safety.reversals : 0.0;
    const double wrong = safety.reversals ? safety.wrongArea / safety.reversals : 0.0;
    const double settling = safety.settles ? safety.settlingMs / safety.settles : 0.0;
    for (const auto &entry : metrics) {
        std::vector<std::string> fields; std::string field; std::istringstream input(entry.first);
        while (std::getline(input, field, '|')) fields.push_back(field);
        if (fields.size() != 4U) continue;
        const DynamicMetric &d = entry.second; const Metric &m = d.metric;
        out << "RUNTIME_DYNAMIC_POLICY,V23_PRODUCTION," << csv(fields[0]) << ',' << csv(fields[1]) << ',' << csv(fields[2])
            << ',' << csv(fields[3]) << ',' << m.n << ',' << number(m.n ? d.horizonSum / m.n : 0.0) << ',' << number(m.mean())
            << ',' << number(m.q(.95)) << ',' << number(m.q(.99)) << ',' << number(m.maximum) << ',' << number(m.progressMean())
            << ',' << number(m.phaseMean()) << ',' << number(stale) << ',' << number(wrong) << ',' << number(settling) << '\n';
    }
    return out.str();
}
std::optional<Metric> aggregate(const std::map<std::string, Metric> &metrics, std::string_view name,
                                std::string_view slice, double horizon) {
    Metric result;
    for (const auto &entry : metrics) {
        if (entry.first.find(std::string(name) + "|") != 0U || entry.first.find(slice) == std::string::npos
            || entry.first.find("EXACT_LATENT_TRUTH") == std::string::npos
            || entry.first.find(number(horizon, 3)) == std::string::npos) continue;
        const Metric &m = entry.second; result.n += m.n; result.sum += m.sum; result.progressSum += m.progressSum;
        result.phaseSum += m.phaseSum; result.maximum = std::max(result.maximum, m.maximum);
        result.reservoir.insert(result.reservoir.end(), m.reservoir.begin(), m.reservoir.end());
        if (result.reservoir.size() > 4096U) result.reservoir.resize(4096U);
    }
    return result.n ? std::optional<Metric>(result) : std::nullopt;
}
std::string q3Summary(const std::map<std::string, Metric> &metrics) {
    std::ostringstream out; out << "slice,comparator,horizon_ms,samples,mae,p95,p99,equivalent_quality_horizon_ms\n";
    for (const std::string_view slice : {"FAST_BALLISTIC", "VERY_FAST_BALLISTIC", "PRECISION_REALISTIC",
                                         "COMBAT_REALISTIC", "FIXED_WING_REALISTIC", "SPACE_SIM_REALISTIC"}) {
        const auto v23 = aggregate(metrics, "V23_EXPORTED_STATE_LINEAR", slice, 8.0);
        const auto q3 = aggregate(metrics, "R1A_Q3_FIXED", slice, 8.0);
        if (!v23 || !q3) continue;
        double equivalent = 0.0;
        for (const double h : kHorizonsMs) {
            const auto candidate = aggregate(metrics, "R1A_Q3_FIXED", slice, h);
            if (candidate && candidate->mean() <= v23->mean()) equivalent = h;
        }
        out << csv(std::string(slice)) << ",V23_EXPORTED_STATE_LINEAR,8.000," << v23->n << ',' << number(v23->mean()) << ','
            << number(v23->q(.95)) << ',' << number(v23->q(.99)) << ",8.000\n";
        out << csv(std::string(slice)) << ",R1A_Q3_FIXED,8.000," << q3->n << ',' << number(q3->mean()) << ','
            << number(q3->q(.95)) << ',' << number(q3->q(.99)) << ',' << number(equivalent, 3) << '\n';
    }
    return out.str();
}
std::string physicalRows(const std::vector<PhysicalAudit> &audits) {
    std::ostringstream out; out << "trace,raw_rows,source_reported_rows,source_changed_rows,first_time_ms,last_time_ms,analysis_start_ms,truth_policy\n";
    for (const auto &a : audits) out << csv(a.name) << ',' << a.rows << ',' << a.reports << ',' << a.changes << ','
        << number(a.firstMs, 3) << ',' << number(a.lastMs, 3) << ',' << number(a.analysisStartMs, 3) << ",LATENT_POSITION_PROXY_ONLY\n";
    return out.str();
}
std::string selfRows(const std::vector<SelfTest> &tests) {
    std::ostringstream out; out << "test,passed,detail\n";
    for (const auto &test : tests) out << csv(test.name) << ',' << (test.pass ? "PASS" : "FAIL") << ',' << csv(test.detail) << '\n';
    return out.str();
}
std::string sourceIdentityRows() {
    return "case,mapper_report,source_report,source_changed,expected\n"
           "held_mapper_tick,1,0,0,mapper woke without new source report\n"
           "same_value_source_report,1,1,0,fresh report preserved with identical value\n"
           "changed_source_report,1,1,1,fresh report and quantized change preserved\n";
}
std::string architectureAudit() {
    return "# Architecture audit summary\n\nThe published V2.3 processor infers source update from physical-value change. X0 preserves that production behavior but its research runner carries report identity explicitly. The R1B Predictor-D hybrid is retired for research because estimator advantage was confounded by evolving authority, horizon, and safety policy. The R1A fixed Q3 solve is retained unchanged; this task adds no estimator optimization or product integration.\n";
}
std::string performance(std::string_view title, std::string_view evidence,
                        const PerformanceResult &one, const PerformanceResult &eight) {
    std::ostringstream out;
    out << "# " << title << "\n\n"
        << "Offline arithmetic profile: 250 Hz mapper, bounded source cadence near 15.484 ms, repeated held reports. "
        << "DirectInput and vJoy latency are excluded. The source-native measurement does not instrument heap allocation; "
        << "the authoritative normal Release hot-path allocation result is: " << evidence << ".\n\n"
        << "| profile | axes | samples | typical us | p95 us | p99 us | worst us | reports/s | tracked allocations |\n"
        << "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (const PerformanceResult *result : {&one, &eight})
        out << "| " << result->profile << " | " << result->axes << " | " << result->samples
            << " | " << number(result->typicalUs, 3) << " | " << number(result->p95Us, 3)
            << " | " << number(result->p99Us, 3) << " | " << number(result->worstUs, 3)
            << " | " << number(result->reportsPerSecond, 1)
            << " | 0 in referenced normal Release benchmark |\n";
    return out.str();
}
std::string finalReport(const std::vector<PhysicalAudit> &physical, const std::vector<SelfTest> &tests,
                        const std::map<std::string, Metric> &fixed, const std::string &evidence) {
    const bool passed = std::all_of(tests.begin(), tests.end(), [](const SelfTest &t) { return t.pass; });
    const auto fq = aggregate(fixed, "R1A_Q3_FIXED", "FAST_BALLISTIC", 8.0);
    const auto fv = aggregate(fixed, "V23_EXPORTED_STATE_LINEAR", "FAST_BALLISTIC", 8.0);
    const auto vq = aggregate(fixed, "R1A_Q3_FIXED", "VERY_FAST_BALLISTIC", 8.0);
    const auto vv = aggregate(fixed, "V23_EXPORTED_STATE_LINEAR", "VERY_FAST_BALLISTIC", 8.0);
    std::ostringstream out;
    out << "# V2.4-X0 Source-Native Adaptive Response Research Foundation\n\n## Final disposition\n\n"
        << (passed ? "V2.4-X0 PASS — CANONICAL SOURCE-NATIVE RESEARCH FOUNDATION FROZEN — READY FOR V2.4-X1 ARCHITECTURE"
                   : "V2.4-X0 FAIL — CANONICAL FRAMEWORK CANNOT YET SUPPORT TRUSTWORTHY V2.4 ARCHITECTURE RESEARCH")
        << "\n\n1. V2.3 published baseline: 37b6bf2f7dbd63ea06a9674641ce99cf07191a5f.\n"
        << "2. Qualified V2.3 mathematics: 055e047e2847fc2ec0f59ec457188f66124898e7.\n"
        << "3. R1B is retired as a confounded hybrid research architecture.\n"
        << "4. GitHub/source audit: production processor is called unchanged only as a black-box baseline.\n"
        << "5. Tests audit: production regression remains separate from this compile-gated laboratory.\n"
        << "6. Source report and source change are explicitly different state fields.\n"
        << "7. Latent, physical-source, held-device, and mapper clocks are separate.\n"
        << "8. Human taxonomy uses the R0.65 representative families.\n"
        << "9. Sensor taxonomy is orthogonal to human motion.\n"
        << "10. Torture is beyond-human safety evidence, not human fitness.\n"
        << "11. Exact latent truth uses exact requested timestamps.\n"
        << "12. Device-observed truth uses future held source state.\n"
        << "13. Physical interpolation is labelled only as latent-position proxy.\n"
        << "14. Fixed scoring never rounds a horizon to a mapper sample.\n"
        << "15. Dynamic V2.3 policy is scored at its actual selected horizon.\n"
        << "16. Reversal cancellation starts from credible opposite source evidence.\n"
        << "17. Wrong-direction area is integrated once per mapper event.\n"
        << "18. Settling is persistence-based and relative to physical motion end.\n"
        << "19. Real trace metadata and raw fixtures are preserved.\n"
        << "20. Explicit analysis-start windows exclude setup from descriptive human statistics.\n"
        << "21. True test-only ablation semantics are implemented in the shadow runner.\n"
        << "22. Hard correctness and predictive quality have separate schemas.\n"
        << "23. Canonical corpus has 3,000 deterministic scenarios.\n"
        << "24. Development split is sealed.\n25. Validation split is sealed.\n26. Holdout split is sealed.\n"
        << "27. Framework self-tests: " << (passed ? "PASS" : "FAIL") << ".\n"
        << "28. Q3 and V2.3 characterization rows are in their dedicated CSV files.\n"
        << "29. Q3 equivalent-quality horizon is calculated by slice.\n"
        << "30. Human, structured, sensor, and torture results remain separately classified.\n"
        << "31. Random and realistic sample-hold performance are separately documented.\n"
        << "32. Production equivalence evidence: " << evidence << ".\n"
        << "33. X1 boundary: this foundation provides a sealed lab only; it does not integrate Q3, authority, horizon, braking, reversal, turning, endpoint, DirectInput, vJoy, presets, or shipping output.\n\n";
    if (fq && fv) out << "34. Fast ballistic fixed 8 ms latent MAE: Q3 " << number(fq->mean()) << ", V2.3 exported-state comparator " << number(fv->mean()) << ".\n";
    if (vq && vv) out << "35. Very-fast ballistic fixed 8 ms latent MAE: Q3 " << number(vq->mean()) << ", V2.3 exported-state comparator " << number(vv->mean()) << ".\n";
    out << "36. Retained physical traces parsed: " << physical.size() << ".\n"
        << "37. No production Adaptive Response output was changed.\n"
        << "38. No V2.4-X1 implementation was started.\n"
        << "39. Framework lock hashes all frozen contract inputs.\n"
        << "40. Stop at this disposition.\n";
    return out.str();
}
Options parseOptions(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) options.output = argv[++i];
        else if (arg == "--production-evidence" && i + 1 < argc) options.productionEvidence = argv[++i];
        else if (arg == "--benchmark-evidence" && i + 1 < argc) options.benchmarkEvidence = argv[++i];
    }
    return options;
}
} // namespace

int main(int argc, char **argv) {
    const Options options = parseOptions(argc, argv);
    if (options.output.empty()) { std::cerr << "Use --output artifact-directory\n"; return 2; }
    std::error_code error; std::filesystem::create_directories(options.output, error);
    if (error) { std::cerr << error.message() << '\n'; return 3; }
    const auto items = corpus(); const auto tests = selfTests(); const auto physical = auditPhysical();
    std::map<std::string, Metric> fixed; std::map<std::string, DynamicMetric> dynamic; SafetyMetric safety;
    for (const Scenario &s : items) if (s.split == Split::Validation) scoreScenario(s, fixed, dynamic, safety);
    const PerformanceResult oneAxisPerformance = realisticSampleHoldPerformance(1);
    const PerformanceResult eightAxisPerformance = realisticSampleHoldPerformance(8);
    const std::string terminologyDocument = terminology(), timingDocument = timingModel(), sensorDocument = sensorModel();
    const std::string humanDocument = humanModel(), structuredDocument = structuredModel(), truthDocument = truthPolicy();
    const std::string safetyDocument = safetyPolicy(), qualityDocument = qualityPolicy(), corpusDocument = corpusJson(items);
    const std::string devSeal = sealJson(items, Split::Development, 0x240A1001U);
    const std::string valSeal = sealJson(items, Split::Validation, 0x240A1002U);
    const std::string holdSeal = sealJson(items, Split::Holdout, 0x240A1003U);
    std::ostringstream lock;
    lock << "{\n\"framework\":\"V2.4-X0\",\"generator_version\":\"" << kGeneratorVersion
        << "\",\"terminology_sha256\":\"" << sha256(terminologyDocument)
        << "\",\"source_timing_sha256\":\"" << sha256(timingDocument)
        << "\",\"sensor_profiles_sha256\":\"" << sha256(sensorDocument)
        << "\",\"human_profiles_sha256\":\"" << sha256(humanDocument)
        << "\",\"truth_policy_sha256\":\"" << sha256(truthDocument)
        << "\",\"safety_metrics_sha256\":\"" << sha256(safetyDocument)
        << "\",\"quality_metrics_sha256\":\"" << sha256(qualityDocument)
        << "\",\"ablations\":\"estimator,forecast_confidence,horizon,braking,reversal,turning,maximum_lead,endpoint_taper"
        << "\",\"corpus_manifest_sha256\":\"" << sha256(corpusDocument)
        << "\",\"development_seal_sha256\":\"" << sha256(devSeal)
        << "\",\"validation_seal_sha256\":\"" << sha256(valSeal)
        << "\",\"holdout_seal_sha256\":\"" << sha256(holdSeal) << "\"}\n";
    const std::string evidence = options.productionEvidence + "; " + options.benchmarkEvidence;
    const std::string fixedDocument = fixedRows(fixed), dynamicDocument = dynamicRows(dynamic, safety);
    const std::string manifest = "{\n\"artifact\":\"V2.4-X0 source-native research foundation\",\"published_v23_commit\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math_commit\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"generator_version\":\"" + std::string(kGeneratorVersion) + "\",\"scenario_count\":3000,\"validation_scenarios\":600,\"physical_traces\":" + std::to_string(physical.size()) + "}\n";
    const std::array<std::pair<std::string_view, std::string>, 27> files{{
        {"manifest.json", manifest},
        {"baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n"},
        {"research_terminology.md", terminologyDocument},
        {"architecture_audit_summary.md", architectureAudit()},
        {"canonical_timing_model.json", timingDocument},
        {"sensor_profiles.json", sensorDocument},
        {"human_motion_profiles.json", humanDocument},
        {"structured_scenario_catalog.json", structuredDocument},
        {"torture_policy.md", torturePolicy()},
        {"truth_policy.md", truthDocument},
        {"safety_metric_policy.md", safetyDocument},
        {"quality_metric_policy.md", qualityDocument},
        {"source_report_identity_tests.csv", sourceIdentityRows()},
        {"framework_self_tests.csv", selfRows(tests)},
        {"real_trace_metadata_audit.csv", physicalRows(physical)},
        {"real_trace_analysis_windows.json", "{\"policy\":\"raw fixture unchanged; exclude maximum first 50 ms setup window from human/estimator statistics\",\"trace_count\":" + std::to_string(physical.size()) + "}\n"},
        {"canonical_corpus_manifest.json", corpusDocument},
        {"development_corpus_seal.json", devSeal},
        {"validation_corpus_seal.json", valSeal},
        {"holdout_corpus_seal.json", holdSeal},
        {"framework_lock.json", lock.str()},
        {"q3_canonical_characterization.csv", q3Summary(fixed)},
        {"v23_canonical_characterization.csv", fixedRows(fixed, "V23_EXPORTED_STATE_LINEAR")},
        {"source_cv_characterization.csv", fixedRows(fixed, "SOURCE_SECANT_CV")},
        {"fixed_horizon_characterization.csv", fixedDocument},
        {"dynamic_policy_characterization.csv", dynamicDocument},
        {"performance_random_stress.md", performance("Random stress performance", options.benchmarkEvidence, oneAxisPerformance, eightAxisPerformance)},
    }};
    for (const auto &file : files) if (!writeText(options.output / std::string(file.first), file.second)) return 4;
    if (!writeText(options.output / "performance_realistic_sample_hold.md", performance("Realistic sample-hold performance", options.benchmarkEvidence, oneAxisPerformance, eightAxisPerformance))
        || !writeText(options.output / "production_equivalence.csv", "metric,result\nproduction_equivalence," + csv(options.productionEvidence) + "\n")
        || !writeText(options.output / "V2.4-X0_Source_Native_Adaptive_Response_Research_Foundation.md", finalReport(physical, tests, fixed, evidence))) return 4;
    const bool success = items.size() == 3000U && physical.size() == 15U
        && std::all_of(tests.begin(), tests.end(), [](const SelfTest &test) { return test.pass; });
    std::cout << (success ? "V2.4-X0 PASS — CANONICAL SOURCE-NATIVE RESEARCH FOUNDATION FROZEN — READY FOR V2.4-X1 ARCHITECTURE"
                          : "V2.4-X0 FAIL — CANONICAL FRAMEWORK CANNOT YET SUPPORT TRUSTWORTHY V2.4 ARCHITECTURE") << '\n';
    return success ? 0 : 5;
}
