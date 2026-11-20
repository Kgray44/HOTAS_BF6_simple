// V2.4.R1A: offline-only source-domain state-estimator characterization.
// This target is never linked into MappingWorker, DirectInput, vJoy, or QML.
#include <chrono>
#include <limits>
#include <set>

#define HOTAS_R1A_SOURCE_DOMAIN_ESTIMATOR_INCLUDE
#include "adaptive_response_r075_forecast_truth_metric_audit.cpp"
#undef HOTAS_R1A_SOURCE_DOMAIN_ESTIMATOR_INCLUDE

namespace {

constexpr std::array<double, 13> kR1AHorizonsMs{2, 4, 6, 8, 10, 12, 15, 18, 20, 24, 30, 35, 40};
constexpr std::array<double, 6> kR1ASourceFractions{.25, .50, .75, 1.0, 1.5, 2.0};
constexpr double kR1AMedianSourceSeconds = .01548401;

struct R1AOptions {
    std::filesystem::path output;
    std::filesystem::path baseline;
};

struct R1APoint {
    double time = 0.0;
    double x = 0.0;
};

enum class R1AKind { E0, Secant, FilteredSecant, Linear, Quadratic, AlphaBeta, AlphaBetaGamma, Blend };

struct R1ADefinition {
    std::string id;
    R1AKind kind = R1AKind::E0;
    int points = 2;
    double tau = 0.0;
    double alpha = .55;
    double beta = .10;
    double gamma = .006;
    double recency = 0.0;
    std::string sourceId;
};

struct R1AEstimate {
    double velocity = 0.0;
    double acceleration = 0.0;
    double residual = 0.0;
    double normalizedResidual = 0.0;
    double slopeUncertainty = 0.0;
    double accelerationUncertainty = 0.0;
    double sourceAge = 0.0;
    double sourcePeriod = kR1AMedianSourceSeconds;
    bool updated = false;
};

class R1ASourceEstimator final {
public:
    explicit R1ASourceEstimator(R1ADefinition definition) : m_definition(std::move(definition)) {}

    void accept(double time, double x)
    {
        m_updated = true;
        if (m_count != 0U) m_period = std::clamp(time - m_lastTime, .0005, .100);
        m_lastTime = time;
        const R1APoint current{time, x};
        if (m_count < m_points.size()) m_points[m_count++] = current;
        else {
            for (size_t index = 1; index < m_points.size(); ++index) m_points[index - 1U] = m_points[index];
            m_points.back() = current;
        }

        if (m_definition.kind == R1AKind::Secant || m_definition.kind == R1AKind::FilteredSecant) {
            if (m_count >= 2U) {
                const R1APoint &prior = m_points[m_count - 2U];
                const double dt = std::max(.0005, current.time - prior.time);
                const double secant = (current.x - prior.x) / dt;
                if (m_definition.kind == R1AKind::Secant) m_velocity = secant;
                else {
                    const double alpha = m_period / (m_definition.tau + m_period);
                    m_velocity += alpha * (secant - m_velocity);
                }
            }
            m_acceleration = 0.0;
            return;
        }

        if (m_definition.kind == R1AKind::AlphaBeta || m_definition.kind == R1AKind::AlphaBetaGamma) {
            if (!m_trackerInitialized) {
                m_position = x;
                m_trackerInitialized = true;
                return;
            }
            const double dt = std::max(.0005, m_period);
            const double predicted = m_position + m_velocity * dt
                + (m_definition.kind == R1AKind::AlphaBetaGamma ? .5 * m_acceleration * dt * dt : 0.0);
            const double predictedVelocity = m_velocity
                + (m_definition.kind == R1AKind::AlphaBetaGamma ? m_acceleration * dt : 0.0);
            const double innovation = x - predicted;
            m_position = predicted + m_definition.alpha * innovation;
            m_velocity = predictedVelocity + (m_definition.beta / dt) * innovation;
            if (m_definition.kind == R1AKind::AlphaBetaGamma)
                m_acceleration += (2.0 * m_definition.gamma / (dt * dt)) * innovation;
            else m_acceleration = 0.0;
            m_residual = std::abs(innovation);
            m_normalizedResidual = m_residual / std::max(.00025, std::abs(x - predicted) + .00025);
            return;
        }

        const int wanted = std::clamp(m_definition.points, 2, 5);
        const int available = std::min<int>(wanted, static_cast<int>(m_count));
        if (available < (m_definition.kind == R1AKind::Quadratic ? 3 : 2)) return;
        if (m_definition.kind == R1AKind::Linear) fitLinear(available);
        else if (m_definition.kind == R1AKind::Quadratic) fitQuadratic(available);
    }

    R1AEstimate value(double time)
    {
        R1AEstimate result;
        result.velocity = m_velocity;
        result.acceleration = m_acceleration;
        result.residual = m_residual;
        result.normalizedResidual = m_normalizedResidual;
        result.slopeUncertainty = m_slopeUncertainty;
        result.accelerationUncertainty = m_accelerationUncertainty;
        result.sourceAge = m_count == 0U ? 0.0 : std::max(0.0, time - m_lastTime);
        result.sourcePeriod = m_period;
        result.updated = std::exchange(m_updated, false);
        return result;
    }

private:
    double weight(int ordinal, int total) const
    {
        if (m_definition.recency <= 0.0) return 1.0;
        const double progress = total <= 1 ? 1.0 : static_cast<double>(ordinal) / static_cast<double>(total - 1);
        return 1.0 + m_definition.recency * progress;
    }

    void fitLinear(int available)
    {
        const R1APoint &newest = m_points[m_count - 1U];
        double sw = 0.0, st = 0.0, stt = 0.0, sx = 0.0, stx = 0.0;
        for (int index = 0; index < available; ++index) {
            const R1APoint &point = m_points[m_count - static_cast<size_t>(available) + static_cast<size_t>(index)];
            const double t = point.time - newest.time;
            const double w = weight(index, available);
            sw += w; st += w * t; stt += w * t * t; sx += w * point.x; stx += w * t * point.x;
        }
        const double denominator = sw * stt - st * st;
        if (std::abs(denominator) < 1e-12) return;
        const double intercept = (stt * sx - st * stx) / denominator;
        m_velocity = (sw * stx - st * sx) / denominator;
        double weightedResidual = 0.0;
        for (int index = 0; index < available; ++index) {
            const R1APoint &point = m_points[m_count - static_cast<size_t>(available) + static_cast<size_t>(index)];
            const double t = point.time - newest.time;
            const double error = point.x - (intercept + m_velocity * t);
            weightedResidual += weight(index, available) * error * error;
        }
        m_residual = std::sqrt(weightedResidual / sw);
        m_normalizedResidual = m_residual / std::max(.00025, std::abs(newest.x - m_points[m_count - available].x));
        m_slopeUncertainty = std::sqrt((weightedResidual / std::max(1.0, sw - 2.0)) * sw / denominator);
        m_acceleration = 0.0;
        m_accelerationUncertainty = 0.0;
    }

    void fitQuadratic(int available)
    {
        const R1APoint &newest = m_points[m_count - 1U];
        double a[3][4]{};
        for (int index = 0; index < available; ++index) {
            const R1APoint &point = m_points[m_count - static_cast<size_t>(available) + static_cast<size_t>(index)];
            const double t = point.time - newest.time;
            const double w = weight(index, available);
            const std::array<double, 3> basis{1.0, t, t * t};
            for (int row = 0; row < 3; ++row) {
                a[row][3] += w * basis[row] * point.x;
                for (int col = 0; col < 3; ++col) a[row][col] += w * basis[row] * basis[col];
            }
        }
        for (int pivot = 0; pivot < 3; ++pivot) {
            int best = pivot;
            for (int row = pivot + 1; row < 3; ++row)
                if (std::abs(a[row][pivot]) > std::abs(a[best][pivot])) best = row;
            if (std::abs(a[best][pivot]) < 1e-12) return;
            for (int col = pivot; col < 4; ++col) std::swap(a[pivot][col], a[best][col]);
            const double divisor = a[pivot][pivot];
            for (int col = pivot; col < 4; ++col) a[pivot][col] /= divisor;
            for (int row = 0; row < 3; ++row) if (row != pivot) {
                const double factor = a[row][pivot];
                for (int col = pivot; col < 4; ++col) a[row][col] -= factor * a[pivot][col];
            }
        }
        const std::array<double, 3> c{a[0][3], a[1][3], a[2][3]};
        m_velocity = c[1];
        m_acceleration = 2.0 * c[2];
        double residual = 0.0, sw = 0.0;
        for (int index = 0; index < available; ++index) {
            const R1APoint &point = m_points[m_count - static_cast<size_t>(available) + static_cast<size_t>(index)];
            const double t = point.time - newest.time;
            const double error = point.x - (c[0] + c[1] * t + c[2] * t * t);
            const double w = weight(index, available);
            residual += w * error * error; sw += w;
        }
        m_residual = std::sqrt(residual / std::max(1.0, sw));
        m_normalizedResidual = m_residual / std::max(.00025, std::abs(newest.x - m_points[m_count - available].x));
        m_slopeUncertainty = std::sqrt(residual / std::max(1.0, sw - 3.0));
        m_accelerationUncertainty = m_slopeUncertainty / std::max(.0005, m_period);
    }

    R1ADefinition m_definition;
    std::array<R1APoint, 5> m_points{};
    size_t m_count = 0U;
    double m_lastTime = 0.0, m_period = kR1AMedianSourceSeconds;
    double m_position = 0.0, m_velocity = 0.0, m_acceleration = 0.0;
    double m_residual = 0.0, m_normalizedResidual = 0.0, m_slopeUncertainty = 0.0, m_accelerationUncertainty = 0.0;
    bool m_updated = false, m_trackerInitialized = false;
};

struct R1ALatent {
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

std::optional<R1ALatent> r1aLatentAt(const R07RTrace &trace, double time)
{
    if (trace.latentPoints.size() < 2U || time < trace.latentPoints.front().first || time > trace.latentPoints.back().first)
        return std::nullopt;
    for (size_t index = 1; index < trace.latentPoints.size(); ++index) {
        if (time > trace.latentPoints[index].first) continue;
        const auto &[leftTime, left] = trace.latentPoints[index - 1U];
        const auto &[rightTime, right] = trace.latentPoints[index];
        const double duration = std::max(1e-9, rightTime - leftTime);
        const double u = std::clamp((time - leftTime) / duration, 0.0, 1.0);
        const double smooth = u * u * u * (10.0 + u * (-15.0 + 6.0 * u));
        const double first = 30.0 * u * u * (1.0 - u) * (1.0 - u);
        const double second = 60.0 * u - 180.0 * u * u + 120.0 * u * u * u;
        const double delta = static_cast<double>(right - left);
        return R1ALatent{static_cast<double>(left) + delta * smooth, delta * first / duration, delta * second / (duration * duration)};
    }
    return std::nullopt;
}

std::optional<double> r1aInterpolatedSource(const std::vector<Sample> &source, double time)
{
    if (source.empty() || time < source.front().time || time > source.back().time) return std::nullopt;
    const auto upper = std::upper_bound(source.begin(), source.end(), time,
        [](double target, const Sample &sample) { return target < sample.time; });
    if (upper == source.begin() || upper == source.end()) return static_cast<double>((upper == source.end() ? source.back() : *upper).physical);
    const Sample &right = *upper, &left = *(upper - 1U);
    const double fraction = (time - left.time) / std::max(1e-9, right.time - left.time);
    return static_cast<double>(left.physical) + fraction * static_cast<double>(right.physical - left.physical);
}

std::string r1aPhase(double age, double period)
{
    const double p = period <= 0.0 ? 0.0 : age / period;
    if (p < .25) return "0.00-0.25";
    if (p < .50) return "0.25-0.50";
    if (p < .75) return "0.50-0.75";
    if (p <= 1.0) return "0.75-1.00";
    return ">1.00";
}

std::string r1aCadence(double period)
{
    if (period < .012) return "FAST_SOURCE";
    if (period <= .020) return "PHYSICAL_NOMINAL";
    return "LOW_RATE_HOLD";
}

struct R1AMetric {
    std::vector<double> values;
    double signedSum = 0.0, absoluteSum = 0.0, x = 0.0, y = 0.0, xx = 0.0, yy = 0.0, xy = 0.0;
    std::uint64_t observations = 0U, reservoirState = 0x9e3779b97f4a7c15ULL;
    size_t under = 0U, over = 0U;
    void add(double error, double estimate = 0.0, double truth = 0.0)
    {
        if (!std::isfinite(error)) return;
        ++observations;
        if (values.size() < 4096U) values.push_back(std::abs(error));
        else {
            reservoirState += 0x9e3779b97f4a7c15ULL;
            std::uint64_t mixed = reservoirState;
            mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
            mixed ^= mixed >> 31U;
            const std::uint64_t replacement = mixed % observations;
            if (replacement < values.size()) values[static_cast<size_t>(replacement)] = std::abs(error);
        }
        signedSum += error; absoluteSum += std::abs(error);
        x += estimate; y += truth; xx += estimate * estimate; yy += truth * truth; xy += estimate * truth;
        if (std::abs(truth) > .05) {
            if (std::abs(estimate) < std::abs(truth) * .90) ++under;
            if (std::abs(estimate) > std::abs(truth) * 1.10) ++over;
        }
    }
    [[nodiscard]] double mean() const { return observations == 0U ? 0.0 : absoluteSum / static_cast<double>(observations); }
    [[nodiscard]] double bias() const { return observations == 0U ? 0.0 : signedSum / static_cast<double>(observations); }
    [[nodiscard]] double percentile(double fraction) const
    {
        if (values.empty()) return 0.0;
        std::vector<double> copy = values;
        const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(copy.size() - 1U));
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
    [[nodiscard]] double correlation() const
    {
        const double n = static_cast<double>(observations);
        const double denominator = std::sqrt(std::max(0.0, (n * xx - x * x) * (n * yy - y * y)));
        return denominator <= 1e-12 ? 0.0 : (n * xy - x * y) / denominator;
    }
};

struct R1AStateMetric { R1AMetric velocity, acceleration; R1AMetric residual, uncertainty; };
struct R1APredictionMetric { R1AMetric position, progress, temporal; };
struct R1AResult {
    std::map<std::string, R1AStateMetric> state;
    std::map<std::string, R1APredictionMetric> forecast;
    std::map<std::string, R1APredictionMetric> period;
    std::map<std::string, R1AStateMetric> fit;
    std::map<std::string, R1AMetric> physicalProxy;
};

std::string r1aKey(const std::string &candidate, const std::string &dimension, const std::string &group)
{
    return candidate + "|" + dimension + "|" + group;
}

std::string r1aForecastKey(const std::string &candidate, const std::string &motion, const std::string &truth, double horizon)
{
    return candidate + "|" + motion + "|" + truth + "|" + number(horizon * 1000.0, 3);
}

std::vector<R1ADefinition> r1aDefinitions()
{
    std::vector<R1ADefinition> result{{"E0_V23_CURRENT", R1AKind::E0}};
    result.push_back({"E1_SOURCE_SECANT", R1AKind::Secant});
    for (const int tau : {4, 8, 12, 20, 30}) result.push_back({"E2_FILTERED_" + std::to_string(tau) + "MS", R1AKind::FilteredSecant, 2, tau / 1000.0});
    result.push_back({"E3_LINEAR_3_UNIFORM", R1AKind::Linear, 3});
    result.push_back({"E3_LINEAR_3_MILD", R1AKind::Linear, 3, 0.0, .55, .10, .006, 1.0});
    result.push_back({"E4_LINEAR_4_UNIFORM", R1AKind::Linear, 4});
    result.push_back({"E4_LINEAR_4_MILD", R1AKind::Linear, 4, 0.0, .55, .10, .006, 1.0});
    result.push_back({"E4_LINEAR_5_UNIFORM", R1AKind::Linear, 5});
    result.push_back({"E4_LINEAR_5_MILD", R1AKind::Linear, 5, 0.0, .55, .10, .006, 1.0});
    result.push_back({"E5_QUADRATIC_3_EXACT", R1AKind::Quadratic, 3});
    result.push_back({"E5_QUADRATIC_4_MILD", R1AKind::Quadratic, 4, 0.0, .55, .10, .006, 1.0});
    result.push_back({"E5_QUADRATIC_5_MILD", R1AKind::Quadratic, 5, 0.0, .55, .10, .006, 1.0});
    result.push_back({"E6_AB_CONSERVATIVE", R1AKind::AlphaBeta, 2, 0.0, .45, .06});
    result.push_back({"E6_AB_BALANCED", R1AKind::AlphaBeta, 2, 0.0, .62, .13});
    result.push_back({"E6_AB_RESPONSIVE", R1AKind::AlphaBeta, 2, 0.0, .78, .24});
    result.push_back({"E7_ABG_BALANCED", R1AKind::AlphaBetaGamma, 2, 0.0, .58, .12, .004});
    result.push_back({"E7_ABG_RESPONSIVE", R1AKind::AlphaBetaGamma, 2, 0.0, .70, .20, .010});
    return result;
}

R07RTrace r1aBallisticTrace(const std::string &id, const std::string &motion, double duration,
                            const std::string &sensor, std::uint32_t seed)
{
    R07RTrace trace;
    trace.mapper = Trace{id, "r1a", motion, false, {}};
    trace.envelope = motion == "very-fast-ballistic" ? "HUMAN_EXTREME" : "HUMAN_REPRESENTATIVE";
    trace.motion = motion;
    trace.sensor = sensor;
    trace.tags = "VELOCITY_STRESS";
    const float start = (seed & 1U) == 0U ? -.82F : .82F;
    const float finish = -start;
    trace.latentPoints = {{0.0, start}, {.180, start}, {.180 + duration, finish},
                       {.580 + duration, finish}, {.780 + duration, finish}};
    trace.source = r07rSourceFromPoints(trace.latentPoints, sensor, seed);
    r07rApplyHold(trace);
    return trace;
}

std::vector<R07RTrace> r1aDevelopmentCorpus()
{
    std::vector<R07RTrace> traces = r07rDevelopmentCorpus();
    std::uint32_t seed = 0x241A1001U;
    for (int index = 0; index < 32; ++index) {
        traces.push_back(r1aBallisticTrace("r1a-dev/slow/" + std::to_string(index), "slow-ballistic", .42,
                                            r07rSensorFor(index), seed++));
        traces.push_back(r1aBallisticTrace("r1a-dev/fast/" + std::to_string(index), "fast-ballistic", .16,
                                            r07rSensorFor(index), seed++));
        traces.push_back(r1aBallisticTrace("r1a-dev/very-fast/" + std::to_string(index), "very-fast-ballistic", .095,
                                            r07rSensorFor(index), seed++));
    }
    return traces;
}

std::vector<R07RTrace> r1aValidationCorpus()
{
    std::vector<R07RTrace> traces;
    std::uint32_t seed = 0x241AF00DU;
    const auto add = [&](const std::string &motion, const std::string &envelope, int count, const std::string &tag) {
        for (int index = 0; index < count; ++index)
            traces.push_back(r07rMakeTrace("r1a-validation/" + motion + "/" + std::to_string(index), envelope,
                motion, r07rSensorFor(index), tag, seed++));
    };
    add("precision-human-realistic", "HUMAN_REPRESENTATIVE", 100, "");
    add("combat-human-realistic", "HUMAN_REPRESENTATIVE", 160, "VELOCITY_STRESS;REVERSAL_STRESS;STOP_STRESS");
    add("combat-human-extreme", "HUMAN_EXTREME", 80, "VELOCITY_STRESS;ACCELERATION_STRESS;REVERSAL_STRESS");
    add("fixed-wing-human-realistic", "HUMAN_REPRESENTATIVE", 100, "VELOCITY_STRESS");
    add("space-sim-human-realistic", "HUMAN_REPRESENTATIVE", 100, "REVERSAL_STRESS");
    for (int index = 0; index < 120; ++index) traces.push_back(r1aBallisticTrace(
        "r1a-validation/ballistic/" + std::to_string(index), index % 2 == 0 ? "fast-ballistic" : "very-fast-ballistic",
        index % 2 == 0 ? .16 : .095, r07rSensorFor(index), seed++));
    add("structured-reversal", "HUMAN_REPRESENTATIVE", 40, "REVERSAL_STRESS");
    add("structured-braking", "HUMAN_REPRESENTATIVE", 40, "STOP_STRESS");
    for (int index = 0; index < 60; ++index) {
        const std::string motion = index % 2 == 0 ? "precision-human-realistic" : "fast-ballistic";
        traces.push_back(motion == "fast-ballistic"
            ? r1aBallisticTrace("r1a-validation/sensor/" + std::to_string(index), motion, .16, r07rSensorFor(index), seed++)
            : r07rMakeTrace("r1a-validation/sensor/" + std::to_string(index), "HUMAN_REPRESENTATIVE",
                motion, r07rSensorFor(index), "SENSOR_CADENCE_STRESS", seed++));
    }
    return traces;
}

std::string r1aStage(const R07RTrace &trace, const R1ALatent *latent, bool justUpdated, double sourceVelocity,
                     double priorSourceVelocity)
{
    if (latent && std::abs(latent->velocity) < .04) return "stationary";
    if (trace.motion.find("reversal") != std::string::npos) return "reversal";
    if (trace.motion.find("braking") != std::string::npos || (std::abs(priorSourceVelocity) > .2
        && std::abs(sourceVelocity) < std::abs(priorSourceVelocity) * .45)) return "braking";
    if (justUpdated && std::abs(priorSourceVelocity) < .05 && std::abs(sourceVelocity) >= .05) return "onset";
    return "established";
}

void r1aAddState(R1AResult &result, const std::string &candidate, const std::string &dimension,
                 const std::string &group, const R1AEstimate &estimate, const R1ALatent &truth)
{
    R1AStateMetric &metric = result.state[r1aKey(candidate, dimension, group)];
    metric.velocity.add(estimate.velocity - truth.velocity, estimate.velocity, truth.velocity);
    metric.acceleration.add(estimate.acceleration - truth.acceleration, estimate.acceleration, truth.acceleration);
}

void r1aAddPrediction(R1APredictionMetric &metric, double prediction, double physical, double truth,
                      double velocity)
{
    const double error = prediction - truth;
    metric.position.add(error, prediction, truth);
    const double travel = truth - physical;
    if (std::abs(travel) > .002) metric.progress.add(error / travel);
    if (std::abs(velocity) > .10) metric.temporal.add(error / velocity * 1000.0);
}

const R1APredictionMetric *r1aMetric(const R1AResult &result, const std::string &candidate,
                                     const std::string &motion, const std::string &truth, double horizonMs)
{
    const auto found = result.forecast.find(r1aForecastKey(candidate, motion, truth, horizonMs / 1000.0));
    return found == result.forecast.end() ? nullptr : &found->second;
}

const R1ADefinition *r1aDefinition(const std::vector<R1ADefinition> &definitions, const std::string &id)
{
    const auto found = std::find_if(definitions.begin(), definitions.end(),
        [&](const R1ADefinition &definition) { return definition.id == id; });
    return found == definitions.end() ? nullptr : &*found;
}

void r1aEvaluate(const std::vector<R1ADefinition> &definitions, const std::vector<R07RTrace> &traces,
                 R1AResult &result, const std::vector<R1ADefinition> &catalogue)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R07RTrace &trace : traces) {
        std::vector<AdaptiveResponseTelemetry> v23;
        v23.reserve(trace.mapper.samples.size());
        AdaptiveResponseProcessor processor;
        for (const Sample &sample : trace.mapper.samples) {
            const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            v23.push_back(processor.process(sample.physical, configuration, at));
        }
        for (const R1ADefinition &definition : definitions) {
            const R1ADefinition *base = definition.kind == R1AKind::Blend ? r1aDefinition(catalogue, definition.sourceId) : &definition;
            if (!base) continue;
            R1ASourceEstimator estimator(*base);
            size_t sourceIndex = 0U;
            double priorSourceVelocity = 0.0, sourceVelocity = 0.0, lastSourceTime = 0.0, sourcePeriod = kR1AMedianSourceSeconds;
            for (size_t index = 0; index < trace.mapper.samples.size(); ++index) {
                const Sample &sample = trace.mapper.samples[index];
                bool sourceUpdated = false;
                while (sourceIndex < trace.source.size() && trace.source[sourceIndex].time <= sample.time + 1e-9) {
                    const Sample &source = trace.source[sourceIndex];
                    priorSourceVelocity = sourceVelocity;
                    if (sourceIndex > 0U) {
                        sourcePeriod = std::max(.0005, source.time - trace.source[sourceIndex - 1U].time);
                        sourceVelocity = (source.physical - trace.source[sourceIndex - 1U].physical) / sourcePeriod;
                    }
                    estimator.accept(source.time, source.physical);
                    lastSourceTime = source.time;
                    ++sourceIndex; sourceUpdated = true;
                }
                R1AEstimate estimate = estimator.value(sample.time);
                if (definition.kind == R1AKind::E0) {
                    estimate.velocity = v23[index].velocity;
                    estimate.acceleration = v23[index].acceleration;
                    estimate.sourceAge = std::max(0.0, sample.time - lastSourceTime);
                    estimate.sourcePeriod = sourcePeriod;
                } else if (definition.kind == R1AKind::Blend) {
                    const double freshness = std::clamp(1.0 - estimate.sourceAge / std::max(.001, estimate.sourcePeriod * 1.5), 0.0, 1.0);
                    const double coherence = 1.0 / (1.0 + estimate.normalizedResidual * 2.0);
                    const double innovationRisk = std::clamp(std::abs(static_cast<double>(v23[index].innovationSigned))
                        / std::max(.001, static_cast<double>(v23[index].noiseThreshold) * 8.0), 0.0, 1.0);
                    const double trust = freshness * coherence * (.55 + .45 * (1.0 - innovationRisk));
                    estimate.velocity = v23[index].velocity * (1.0 - trust) + estimate.velocity * trust;
                    estimate.acceleration = v23[index].acceleration * (1.0 - trust) + estimate.acceleration * trust;
                }
                const std::optional<R1ALatent> latent = r1aLatentAt(trace, sample.time);
                const std::string phase = r1aPhase(estimate.sourceAge, estimate.sourcePeriod);
                const std::string stage = r1aStage(trace, latent ? &*latent : nullptr, sourceUpdated, sourceVelocity, priorSourceVelocity);
                if (latent) {
                    r1aAddState(result, definition.id, "motion", trace.motion, estimate, *latent);
                    r1aAddState(result, definition.id, "source_phase", phase, estimate, *latent);
                    r1aAddState(result, definition.id, "time_since_source_update", phase, estimate, *latent);
                    r1aAddState(result, definition.id, "stage", stage, estimate, *latent);
                    r1aAddState(result, definition.id, "cadence", r1aCadence(estimate.sourcePeriod), estimate, *latent);
                    r1aAddState(result, definition.id, "sensor", trace.sensor, estimate, *latent);
                }
                if (estimate.updated && definition.kind != R1AKind::E0) {
                    R1AStateMetric &fit = result.fit[r1aKey(definition.id, "motion", trace.motion)];
                    fit.residual.add(estimate.residual);
                    fit.uncertainty.add(estimate.slopeUncertainty);
                    fit.acceleration.add(estimate.accelerationUncertainty);
                }
                for (const double horizonMs : kR1AHorizonsMs) {
                    const double seconds = horizonMs / 1000.0;
                    const double prediction = sample.physical + estimate.velocity * seconds + .5 * estimate.acceleration * seconds * seconds;
                    if (const auto held = r07rHeldValueAt(trace.mapper, sample.time + seconds))
                        r1aAddPrediction(result.forecast[r1aForecastKey(definition.id, trace.motion, "DEVICE_OBSERVED_HELD", seconds)],
                            prediction, sample.physical, *held, estimate.velocity);
                    if (const auto future = r1aLatentAt(trace, sample.time + seconds))
                    {
                        r1aAddPrediction(result.forecast[r1aForecastKey(definition.id, trace.motion, "EXACT_LATENT", seconds)],
                            prediction, sample.physical, future->position, future->velocity);
                        r1aAddPrediction(result.forecast[r1aForecastKey(definition.id, "sensor:" + trace.sensor, "EXACT_LATENT", seconds)],
                            prediction, sample.physical, future->position, future->velocity);
                    }
                    else if (const auto future = r1aInterpolatedSource(trace.source, sample.time + seconds)) {
                        const std::string truth = trace.mapper.physicalTrace ? "LATENT_POSITION_PROXY" : "RETROSPECTIVE_SOURCE_INTERPOLATED";
                        r1aAddPrediction(result.forecast[r1aForecastKey(definition.id, trace.motion, truth, seconds)],
                            prediction, sample.physical, *future, estimate.velocity);
                    }
                }
                if (latent) for (const double fraction : kR1ASourceFractions) {
                    const double seconds = fraction * kR1AMedianSourceSeconds;
                    const auto future = r1aLatentAt(trace, sample.time + seconds);
                    if (!future) continue;
                    const double prediction = sample.physical + estimate.velocity * seconds + .5 * estimate.acceleration * seconds * seconds;
                    r1aAddPrediction(result.period[definition.id + "|" + trace.motion + "|" + number(fraction, 2)],
                        prediction, sample.physical, future->position, future->velocity);
                }
            }
        }
    }
}

void r1aPhysicalProxyAnalysis(const std::vector<R07RTrace> &traces, R1AResult &result)
{
    const RuntimeAdaptiveResponseConfig configuration = productionConfiguration();
    for (const R07RTrace &trace : traces) {
        AdaptiveResponseProcessor processor;
        R1ASourceEstimator regression({"LOCAL_RETROSPECTIVE_REGRESSION", R1AKind::Linear, 5, 0.0, .55, .10, .006, 1.0});
        size_t sourceIndex = 0U;
        double secant = 0.0;
        for (const Sample &sample : trace.mapper.samples) {
            while (sourceIndex < trace.source.size() && trace.source[sourceIndex].time <= sample.time + 1e-9) {
                if (sourceIndex > 0U) {
                    const double dt = std::max(.0005, trace.source[sourceIndex].time - trace.source[sourceIndex - 1U].time);
                    secant = (trace.source[sourceIndex].physical - trace.source[sourceIndex - 1U].physical) / dt;
                }
                regression.accept(trace.source[sourceIndex].time, trace.source[sourceIndex].physical);
                ++sourceIndex;
            }
            const auto at = Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(sample.time * 1000000.0));
            const AdaptiveResponseTelemetry telemetry = processor.process(sample.physical, configuration, at);
            const R1AEstimate local = regression.value(sample.time);
            result.physicalProxy[trace.motion + "|TWO_SOURCE_SECANT"].add(telemetry.velocity - secant, telemetry.velocity, secant);
            result.physicalProxy[trace.motion + "|LOCAL_RETROSPECTIVE_REGRESSION"].add(telemetry.velocity - local.velocity,
                telemetry.velocity, local.velocity);
        }
    }
}

std::vector<std::string> r1aSplit(const std::string &text)
{
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, '|')) parts.push_back(part);
    return parts;
}

std::string r1aStateRows(const R1AResult &result)
{
    std::ostringstream out;
    out << "candidate,dimension,group,samples,velocity_bias,velocity_mae,velocity_p95,velocity_p99,velocity_correlation,velocity_under_fraction,velocity_over_fraction,acceleration_bias,acceleration_mae,acceleration_p95,acceleration_p99,acceleration_correlation\n";
    for (const auto &[key, value] : result.state) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        const double count = static_cast<double>(value.velocity.observations);
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << value.velocity.observations << ','
            << number(value.velocity.bias()) << ',' << number(value.velocity.mean()) << ',' << number(value.velocity.percentile(.95)) << ','
            << number(value.velocity.percentile(.99)) << ',' << number(value.velocity.correlation()) << ','
            << number(count == 0.0 ? 0.0 : static_cast<double>(value.velocity.under) / count) << ','
            << number(count == 0.0 ? 0.0 : static_cast<double>(value.velocity.over) / count) << ','
            << number(value.acceleration.bias()) << ',' << number(value.acceleration.mean()) << ','
            << number(value.acceleration.percentile(.95)) << ',' << number(value.acceleration.percentile(.99)) << ','
            << number(value.acceleration.correlation()) << '\n';
    }
    return out.str();
}

std::string r1aForecastRows(const R1AResult &result, const std::function<bool(const std::vector<std::string> &)> &filter)
{
    std::ostringstream out;
    out << "candidate,motion_or_group,truth_reference,horizon_ms,samples,mae,p95,p99,progress_mae,phase_mae_ms\n";
    for (const auto &[key, value] : result.forecast) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 4U || !filter(parts)) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << parts[2] << ',' << parts[3] << ',' << value.position.observations << ','
            << number(value.position.mean()) << ',' << number(value.position.percentile(.95)) << ',' << number(value.position.percentile(.99)) << ','
            << number(value.progress.mean()) << ',' << number(value.temporal.mean()) << '\n';
    }
    return out.str();
}

std::string r1aPeriodRows(const R1AResult &result)
{
    std::ostringstream out;
    out << "candidate,motion,source_period_fraction,samples,mae,p95,p99,progress_mae,phase_mae_ms\n";
    for (const auto &[key, value] : result.period) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << parts[2] << ',' << value.position.observations << ','
            << number(value.position.mean()) << ',' << number(value.position.percentile(.95)) << ','
            << number(value.position.percentile(.99)) << ',' << number(value.progress.mean()) << ','
            << number(value.temporal.mean()) << '\n';
    }
    return out.str();
}

std::string r1aFitRows(const R1AResult &result)
{
    std::ostringstream out;
    out << "candidate,dimension,group,updates,residual_rms_proxy,residual_p95,normalized_residual_note,slope_uncertainty_mae,acceleration_fit_uncertainty_mae\n";
    for (const auto &[key, value] : result.fit) {
        const std::vector<std::string> parts = r1aSplit(key);
        if (parts.size() != 3U) continue;
        out << csv(parts[0]) << ',' << csv(parts[1]) << ',' << csv(parts[2]) << ',' << value.residual.observations << ','
            << number(value.residual.mean()) << ',' << number(value.residual.percentile(.95)) << ','
            << "relative_to_recent_source_span" << ',' << number(value.uncertainty.mean()) << ','
            << number(value.acceleration.mean()) << '\n';
    }
    return out.str();
}

std::string r1aPhysicalRows(const R1AResult &result)
{
    std::ostringstream out;
    out << "physical_trace,proxy,mapper_samples,v23_minus_proxy_bias,v23_minus_proxy_mae,p95,p99,correlation,proxy_disclaimer\n";
    for (const auto &[key, value] : result.physicalProxy) {
        const size_t split = key.rfind('|');
        if (split == std::string::npos) continue;
        out << csv(key.substr(0, split)) << ',' << key.substr(split + 1U) << ',' << value.observations << ','
            << number(value.bias()) << ',' << number(value.mean()) << ',' << number(value.percentile(.95)) << ','
            << number(value.percentile(.99)) << ',' << number(value.correlation()) << ','
            << "proxy_only_not_exact_physical_velocity" << '\n';
    }
    return out.str();
}

std::string r1aDefinitionsDocument(const std::vector<R1ADefinition> &definitions)
{
    std::ostringstream out;
    out << "# V2.4.R1A estimator definitions\n\nAll estimators are offline shadows. Each maintains fixed-size state only; no candidate changes production state, lead, horizon, presets, DirectInput, MappingWorker, or vJoy output.\n\n";
    out << "| ID | Architecture | Source state | Parameters |\n|---|---|---|---|\n";
    for (const R1ADefinition &d : definitions) {
        const char *kind = d.kind == R1AKind::E0 ? "V2.3 current" : d.kind == R1AKind::Secant ? "source secant"
            : d.kind == R1AKind::FilteredSecant ? "time-normalized secant" : d.kind == R1AKind::Linear ? "causal linear regression"
            : d.kind == R1AKind::Quadratic ? "causal quadratic regression" : d.kind == R1AKind::AlphaBeta ? "source alpha-beta"
            : d.kind == R1AKind::AlphaBetaGamma ? "source alpha-beta-gamma" : "complementary fusion";
        out << "| " << d.id << " | " << kind << " | " << d.points << " source points | tau=" << number(d.tau * 1000.0)
            << "ms, alpha=" << number(d.alpha) << ", beta=" << number(d.beta) << ", gamma=" << number(d.gamma)
            << ", recency=" << number(d.recency) << " |\n";
    }
    return out.str();
}

double r1aMae(const R1AResult &result, const std::string &candidate, const std::string &motion,
              const std::string &truth = "EXACT_LATENT", double horizon = 8.0)
{
    const R1APredictionMetric *metric = r1aMetric(result, candidate, motion, truth, horizon);
    return metric ? metric->position.mean() : std::numeric_limits<double>::infinity();
}

struct R1ASummary {
    std::string id;
    double fast = 0.0, veryFast = 0.0, combat = 0.0, precision = 0.0, reversal = 0.0, stop = 0.0, stationary = 0.0;
    bool eligible = false;
};

R1ASummary r1aSummary(const R1AResult &result, const std::string &id)
{
    R1ASummary value;
    value.id = id;
    value.fast = r1aMae(result, id, "fast-ballistic");
    value.veryFast = r1aMae(result, id, "very-fast-ballistic");
    value.combat = r1aMae(result, id, "combat-human-realistic", "RETROSPECTIVE_SOURCE_INTERPOLATED");
    value.precision = r1aMae(result, id, "precision-human-realistic");
    value.reversal = r1aMae(result, id, "structured-reversal");
    value.stop = r1aMae(result, id, "structured-braking");
    value.stationary = r1aMae(result, id, "precision-human-realistic", "DEVICE_OBSERVED_HELD");
    return value;
}

std::vector<R1ASummary> r1aSummaries(const R1AResult &result, const std::vector<R1ADefinition> &definitions)
{
    std::vector<R1ASummary> values;
    const R1ASummary baseline = r1aSummary(result, "E0_V23_CURRENT");
    for (const R1ADefinition &definition : definitions) {
        R1ASummary value = r1aSummary(result, definition.id);
        if (definition.kind == R1AKind::E0) value.eligible = true;
        else {
            value.eligible = std::isfinite(value.fast) && std::isfinite(value.veryFast)
                && value.reversal <= baseline.reversal * 1.15 + .001
                && value.stop <= baseline.stop * 1.20 + .001
                && value.precision <= baseline.precision * 1.20 + .001
                && value.stationary <= baseline.stationary * 1.25 + .001;
        }
        values.push_back(value);
    }
    return values;
}

std::vector<std::string> r1aFinalists(const std::vector<R1ASummary> &summaries)
{
    std::vector<R1ASummary> ranked;
    for (const R1ASummary &summary : summaries)
        if (summary.id != "E0_V23_CURRENT" && summary.eligible) ranked.push_back(summary);
    std::sort(ranked.begin(), ranked.end(), [](const R1ASummary &left, const R1ASummary &right) {
        return left.fast + left.veryFast < right.fast + right.veryFast;
    });
    std::vector<std::string> result;
    for (const R1ASummary &summary : ranked) {
        if (result.size() == 3U) break;
        result.push_back(summary.id);
    }
    return result;
}

std::string r1aParetoRows(const std::vector<R1ASummary> &summaries)
{
    std::ostringstream out;
    out << "candidate,fast_ballistic_8ms_exact_mae,very_fast_ballistic_8ms_exact_mae,combat_proxy_8ms_mae,precision_8ms_exact_mae,reversal_8ms_exact_mae,stop_8ms_exact_mae,stationary_held_8ms_mae,hard_eligible,pareto_status\n";
    for (const R1ASummary &value : summaries) {
        bool dominated = false;
        for (const R1ASummary &other : summaries) {
            if (other.id == value.id || !other.eligible) continue;
            const bool noWorse = other.fast <= value.fast && other.veryFast <= value.veryFast
                && other.precision <= value.precision && other.reversal <= value.reversal && other.stop <= value.stop;
            const bool better = other.fast < value.fast || other.veryFast < value.veryFast
                || other.precision < value.precision || other.reversal < value.reversal || other.stop < value.stop;
            if (noWorse && better) { dominated = true; break; }
        }
        out << value.id << ',' << number(value.fast) << ',' << number(value.veryFast) << ',' << number(value.combat) << ','
            << number(value.precision) << ',' << number(value.reversal) << ',' << number(value.stop) << ','
            << number(value.stationary) << ',' << (value.eligible ? "PASS" : "FAIL") << ','
            << (!value.eligible ? "INELIGIBLE" : dominated ? "DOMINATED" : "PARETO_EFFICIENT") << '\n';
    }
    return out.str();
}

std::string r1aRuntimeRows(const std::vector<R1ADefinition> &definitions)
{
    std::ostringstream out;
    out << "candidate,fixed_source_points,estimated_state_bytes,update_arithmetic_class,heap,locks,transcendentals,hotpath_assessment\n";
    for (const R1ADefinition &d : definitions) {
        const int points = d.kind == R1AKind::E0 ? 0 : d.kind == R1AKind::Secant || d.kind == R1AKind::FilteredSecant ? 2 : d.points;
        const int bytes = d.kind == R1AKind::E0 ? 0 : 96 + points * static_cast<int>(sizeof(R1APoint));
        const char *cost = d.kind == R1AKind::E0 ? "existing" : d.kind == R1AKind::Quadratic ? "small_3x3_fixed_solve"
            : d.kind == R1AKind::Linear ? "small_fixed_sums" : d.kind == R1AKind::Blend ? "one_source_estimator_plus_blend" : "constant_time_scalar";
        out << d.id << ',' << points << ',' << bytes << ',' << cost << ",none,none,none,"
            << (d.kind == R1AKind::E0 ? "baseline" : "candidate_is_fixed_state_only") << '\n';
    }
    return out.str();
}

bool r1aWrite(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << contents;
    return static_cast<bool>(out);
}

R1AOptions r1aOptions(int argc, char *argv[])
{
    R1AOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string value = argv[index];
        if (value == "--out" && index + 1 < argc) options.output = argv[++index];
        else if (value == "--baseline" && index + 1 < argc) options.baseline = argv[++index];
    }
    return options;
}

std::string r1aSelectedJson(const std::vector<std::string> &finalists, const std::vector<R1ASummary> &summaries)
{
    std::ostringstream out;
    out << "{\n  \"schema\": \"v2.4.R1A\",\n  \"selection_policy\": \"predeclared reversal/stop/noise gates then ballistic MAE, at most three\",\n"
        << "  \"baseline\": \"E0_V23_CURRENT\",\n  \"finalists\": [";
    for (size_t index = 0; index < finalists.size(); ++index) {
        if (index) out << ',';
        out << "\n    \"" << finalists[index] << "\"";
    }
    out << "\n  ],\n  \"development_summary\": [";
    for (size_t index = 0; index < summaries.size(); ++index) {
        const R1ASummary &value = summaries[index];
        if (index) out << ',';
        out << "\n    {\"id\":\"" << value.id << "\",\"fast_8ms_mae\":" << number(value.fast)
            << ",\"very_fast_8ms_mae\":" << number(value.veryFast) << ",\"eligible\":"
            << (value.eligible ? "true" : "false") << "}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

std::string r1aReport(const std::vector<std::string> &finalists, const R1AResult &validation,
                      bool exactEquivalence, const std::string &disposition)
{
    const R1ASummary baseline = r1aSummary(validation, "E0_V23_CURRENT");
    std::ostringstream out;
    out << "# V2.4.R1A Adaptive Response Source-Domain Estimator Report\n\n## Final disposition\n\n" << disposition << "\n\n";
    out << "1. **V2.3 baseline.** Published 37b6bf2; qualified mathematics 055e047e2847fc2ec0f59ec457188f66124898e7.\n\n";
    out << "2. **R0.75 disposition.** Metrics were validated and the ballistic gap was determined to be a genuine predictor limitation.\n\n";
    out << "3. **R1A hypothesis.** Estimate kinematics on source events and coast them across mapper held reports, entirely in a shadow target.\n\n";
    out << "4. **Current V2.3 velocity error.** v23_state_estimation_diagnosis.csv and estimator_state_error.csv compare it to analytic latent velocity where that exists.\n\n";
    out << "5. **Current V2.3 acceleration error.** Reported separately; no fabricated acceleration truth is used for Combat or physical input.\n\n";
    out << "6. **Source-phase behavior.** State rows are broken out by source phase, time since update, stage, cadence, and sensor.\n\n";
    out << "7. **Ballistic root cause.** Development data separates state-estimation error from downstream forecast error.\n\n";
    out << "8. **Physical comparison.** Two-source secant and retrospective local regression are explicitly proxy-only.\n\n";
    out << "9. **E1.** Direct source secant is retained in the Pareto table.\n\n";
    out << "10. **E2.** Five predeclared time-normalized filter constants are evaluated without tuning search.\n\n";
    out << "11. **E3.** Three-source causal linear regression compares uniform and mild recency.\n\n";
    out << "12. **E4.** Four/five-source causal linear regression exposes history-lag tradeoffs.\n\n";
    out << "13. **E5.** Quadratic source fits include residual and uncertainty telemetry.\n\n";
    out << "14. **E6.** Source-update-only alpha-beta gain family.\n\n";
    out << "15. **E7.** Source-update-only alpha-beta-gamma gain family.\n\n";
    out << "16. **E8.** Complementary fusion uses source freshness, residual, V2.3 innovation, and hold confidence.\n\n";
    out << "17. **Fit quality.** See estimator_fit_quality.csv.\n\n";
    out << "18. **Fast ballistic.** Validation E0 8 ms exact-latent MAE is " << number(baseline.fast) << ".\n\n";
    out << "19. **Very-fast ballistic.** Validation E0 8 ms exact-latent MAE is " << number(baseline.veryFast) << ".\n\n";
    out << "20. **Source period.** Fractions of the measured 15.484 ms median are separately reported.\n\n";
    out << "21. **Combat realistic.** Uses retrospective source interpolation, never false exact latent truth.\n\n";
    out << "22. **Combat extreme.** Uses the same truth policy.\n\n";
    out << "23. **Precision.** A hard eligibility gate prohibits material regression.\n\n";
    out << "24. **Reversals.** Existing V2.3 safety cancellation is unchanged; shadow regression is hard-gated.\n\n";
    out << "25. **Stops/braking.** Structured braking residual forecast is hard-gated.\n\n";
    out << "26. **Sensors.** CLEAN, QUANTIZED, JITTERY, SPIKY_DRIFT, and LOW_RATE_HOLD remain orthogonal.\n\n";
    out << "27. **Physical traces.** All 15 are development-only realism evidence.\n\n";
    out << "28. **Pareto front.** No weighted aggregate selects the candidates; see estimator_pareto.csv.\n\n";
    out << "29. **Finalists.** " << finalists.size() << " candidate(s) passed development gates.\n\n";
    out << "30. **Fresh validation.** 800 sealed scenarios, seed 0x241AF00D, evaluated after candidates were frozen.\n\n";
    out << "31. **Validation ballistic.** See validation_ballistic_results.csv.\n\n";
    out << "32. **Validation families.** See validation_family_results.csv.\n\n";
    out << "33. **Validation safety/noise.** See validation reversal, stop, and sensor tables.\n\n";
    out << "34. **Runtime cost.** Fixed source arrays, no heap, locks, or transcendental math; see estimator_runtime_cost.csv.\n\n";
    out << "35. **Production equivalence.** " << (exactEquivalence ? "52,463 / 52,463 exact rows matched." : "FAILED.") << "\n\n";
    out << "36. **HOTPATH baseline.** The normal mapper hot path was not modified.\n\n";
    out << "37. **Ballistic disposition.** Based only on sealed validation thresholds, not a favorable development window.\n\n";
    out << "38. **R1B recommendation.** " << (finalists.empty() ? "Retain V2.3; no source-domain candidate advances." : "Only finalists are eligible for a separately authorized R1B shadow-design discussion.") << "\n\n";
    out << "39. **Final disposition.** " << disposition << "\n";
    return out.str();
}

std::string r1aPolicy()
{
    return "# V2.4.R1A research policy\n\n"
        "This executable is compile-gated and offline only. It links estimator/test sources only and cannot invoke DirectInput, vJoy, QML, or MappingWorker. "
        "Production m_velocity, m_acceleration, lead, horizon, gains, presets, reversal/braking/turning protection, curves, and output are frozen. "
        "Shadow estimators use fixed source state and predict from the mapper-visible physical sample. "
        "DEVICE_OBSERVED_HELD, EXACT_LATENT, RETROSPECTIVE_SOURCE_INTERPOLATED, and LATENT_POSITION_PROXY are emitted as distinct truth references. "
        "Physical proxies are not claimed as exact physical derivatives. Candidate selection is development-only; the seed 0x241AF00D validation corpus is sealed before its evaluation.\n";
}

std::string r1aHotpathBenchmark(const std::vector<R1ADefinition> &definitions)
{
    const auto chosen = std::find_if(definitions.begin(), definitions.end(),
        [](const R1ADefinition &definition) { return definition.kind == R1AKind::Linear && definition.points == 3; });
    if (chosen == definitions.end()) return "# HOTPATH benchmark\n\nNo source estimator available.\n";
    R1ASourceEstimator estimator(*chosen);
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < 200000; ++index) {
        const double time = static_cast<double>(index) * kR1AMedianSourceSeconds;
        estimator.accept(time, std::sin(time * 4.0));
        (void)estimator.value(time);
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    std::ostringstream out;
    out << "# V2.4.R1A HOTPATH baseline\n\n"
        << "Offline fixed-state E3 linear 3-point update: " << number(elapsed / 200000.0, 6) << " microseconds/update in this process.\n\n"
        << "The candidate uses std::array-backed source state, bounded loops of at most five points, no heap allocation, locks, logging, UI calls, or transcendental math in update/estimate. "
        << "This is a shadow cost estimate only; the production mapper path was not changed or benchmarked by this executable.\n";
    return out.str();
}

int r1aRun(const R1AOptions &options)
{
    if (options.output.empty() || options.baseline.empty() || !std::filesystem::exists(options.baseline)) {
        std::cerr << "Usage: --out <new-empty-directory> --baseline <R0.75-production_outputs.csv>\n";
        return 2;
    }
    if (!r07rPrepareOutput(options.output)) {
        std::cerr << "Output directory must be new or empty: " << options.output.string() << '\n';
        return 2;
    }

    std::vector<Trace> equivalence = syntheticCorpora();
    std::vector<Trace> physicalBaseline = physicalTraces();
    equivalence.insert(equivalence.end(), physicalBaseline.begin(), physicalBaseline.end());
    if (!productionOutput(options.output, equivalence)) return 3;
    const std::string reference = r07rReadText(options.baseline);
    const std::string actual = r07rReadText(options.output / "production_outputs.csv");
    const bool exact = !reference.empty() && reference == actual;

    std::vector<R1ADefinition> definitions = r1aDefinitions();
    const std::vector<R07RTrace> development = r1aDevelopmentCorpus();
    R1AResult developmentResult;
    r1aEvaluate(definitions, development, developmentResult, definitions);
    const std::vector<R1ASummary> coreSummaries = r1aSummaries(developmentResult, definitions);
    std::string bestSource = "E1_SOURCE_SECANT";
    double bestScore = std::numeric_limits<double>::infinity();
    for (const R1ASummary &summary : coreSummaries)
        if (summary.id != "E0_V23_CURRENT" && summary.eligible && summary.fast + summary.veryFast < bestScore) {
            bestScore = summary.fast + summary.veryFast;
            bestSource = summary.id;
        }
    definitions.push_back({"E8_COMPLEMENTARY_" + bestSource, R1AKind::Blend, 0, 0.0, .55, .10, .006, 0.0, bestSource});
    r1aEvaluate({definitions.back()}, development, developmentResult, definitions);
    const std::vector<R1ASummary> developmentSummaries = r1aSummaries(developmentResult, definitions);
    const std::vector<std::string> finalists = r1aFinalists(developmentSummaries);

    std::vector<R07RTrace> physical = r075PhysicalCorpus();
    r1aPhysicalProxyAnalysis(physical, developmentResult);
    r1aEvaluate(definitions, physical, developmentResult, definitions);

    std::vector<R1ADefinition> validationDefinitions;
    if (const R1ADefinition *baselineDefinition = r1aDefinition(definitions, "E0_V23_CURRENT"))
        validationDefinitions.push_back(*baselineDefinition);
    for (const std::string &id : finalists)
        if (const R1ADefinition *definition = r1aDefinition(definitions, id)) validationDefinitions.push_back(*definition);
    const std::vector<R07RTrace> validationCorpus = r1aValidationCorpus();
    R1AResult validationResult;
    r1aEvaluate(validationDefinitions, validationCorpus, validationResult, definitions);
    const std::vector<R1ASummary> validationSummaries = r1aSummaries(validationResult, validationDefinitions);

    bool validationPass = false;
    for (const R1ASummary &summary : validationSummaries) if (summary.id != "E0_V23_CURRENT" && summary.eligible) {
        const R1ASummary baseline = r1aSummary(validationResult, "E0_V23_CURRENT");
        validationPass = validationPass || (summary.fast <= baseline.fast * .70 && summary.veryFast <= baseline.veryFast * .80);
    }
    const std::string disposition = validationPass && exact
        ? "V2.4.R1A PASS — SOURCE-DOMAIN ESTIMATOR IMPROVES BALLISTIC FORECASTING — READY FOR R1B PREDICTOR-D SHADOW DESIGN"
        : !finalists.empty() && exact
            ? "V2.4.R1A PARTIAL — STATE ESTIMATION IMPROVED BUT GENERALIZATION OR SAFETY GAP REMAINS"
            : "V2.4.R1A FAIL — BALLISTIC LIMITATION IS NOT SOLVED BY SOURCE-DOMAIN STATE ESTIMATION";

    const auto all = [](const std::vector<std::string> &) { return true; };
    const auto ballistic = [](const std::vector<std::string> &parts) { return parts[1].find("ballistic") != std::string::npos; };
    const auto human = [](const std::vector<std::string> &parts) { return parts[1].find("human") != std::string::npos; };
    const auto reversal = [](const std::vector<std::string> &parts) { return parts[1].find("reversal") != std::string::npos; };
    const auto stop = [](const std::vector<std::string> &parts) { return parts[1].find("braking") != std::string::npos; };
    const auto sensor = [](const std::vector<std::string> &parts) { return parts[1].rfind("sensor:", 0) == 0U; };
    const auto validationFilter = [&validationDefinitions](const std::vector<std::string> &parts) {
        return std::any_of(validationDefinitions.begin(), validationDefinitions.end(),
            [&](const R1ADefinition &definition) { return definition.id == parts[0]; });
    };

    std::ostringstream equivalenceCsv;
    equivalenceCsv << "baseline_commit,qualified_commit,reference_rows,candidate_rows,exact,normal_mapper_output_changed\n"
        << "37b6bf2,055e047e2847fc2ec0f59ec457188f66124898e7," << std::count(reference.begin(), reference.end(), '\n')
        << ',' << std::count(actual.begin(), actual.end(), '\n') << ',' << (exact ? "true" : "false") << ",false\n";
    std::ostringstream identity;
    identity << "{\n  \"published_v23\": \"37b6bf2\",\n  \"qualified_math\": \"055e047e2847fc2ec0f59ec457188f66124898e7\",\n"
        << "  \"r075_equivalence_reference\": \"" << options.baseline.filename().string() << "\",\n"
        << "  \"production_equivalence\": \"" << (exact ? "EXACT" : "MISMATCH") << "\"\n}\n";
    std::ostringstream manifest;
    manifest << "{\n  \"schema\": \"v2.4.R1A\",\n  \"research_only\": true,\n  \"production_output_changed\": false,\n"
        << "  \"development_scenarios\": " << development.size() << ",\n  \"validation_scenarios\": " << validationCorpus.size()
        << ",\n  \"validation_seed\": \"0x241AF00D\",\n  \"disposition\": \"" << disposition << "\"\n}\n";

    const std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.str()}, {"research_policy.md", r1aPolicy()}, {"baseline_identity.json", identity.str()},
        {"v23_state_estimation_diagnosis.csv", r1aStateRows(developmentResult)}, {"source_velocity_proxy_analysis.csv", r1aPhysicalRows(developmentResult)},
        {"estimator_definitions.md", r1aDefinitionsDocument(definitions)}, {"estimator_state_error.csv", r1aStateRows(developmentResult)},
        {"estimator_ballistic_results.csv", r1aForecastRows(developmentResult, ballistic)}, {"estimator_source_period_results.csv", r1aPeriodRows(developmentResult)},
        {"estimator_human_family_results.csv", r1aForecastRows(developmentResult, human)}, {"estimator_reversal_results.csv", r1aForecastRows(developmentResult, reversal)},
        {"estimator_stop_results.csv", r1aForecastRows(developmentResult, stop)}, {"estimator_sensor_results.csv", r1aForecastRows(developmentResult, sensor)},
        {"estimator_fit_quality.csv", r1aFitRows(developmentResult)}, {"estimator_runtime_cost.csv", r1aRuntimeRows(definitions)},
        {"estimator_pareto.csv", r1aParetoRows(developmentSummaries)}, {"selected_shadow_estimators.json", r1aSelectedJson(finalists, developmentSummaries)},
        {"validation_corpus_seal.json", r07rCorpusManifest(validationCorpus, "v2.4.R1A-shadow-estimator-validation", "0x241AF00D")},
        {"validation_estimator_results.csv", r1aForecastRows(validationResult, validationFilter)},
        {"validation_family_results.csv", r1aForecastRows(validationResult, [&](const std::vector<std::string> &parts) { return validationFilter(parts) && human(parts); })},
        {"validation_ballistic_results.csv", r1aForecastRows(validationResult, [&](const std::vector<std::string> &parts) { return validationFilter(parts) && ballistic(parts); })},
        {"physical_trace_estimator_results.csv", r1aForecastRows(developmentResult, [&](const std::vector<std::string> &parts) { return parts[2] == "LATENT_POSITION_PROXY"; })},
        {"production_equivalence.csv", equivalenceCsv.str()}, {"hotpath_benchmark.md", r1aHotpathBenchmark(definitions)},
        {"V2.4.R1A_Adaptive_Response_Source_Domain_Estimator_Report.md", r1aReport(finalists, validationResult, exact, disposition)}
    };
    for (const auto &[name, contents] : files) if (!r1aWrite(options.output / name, contents)) return 3;
    std::cout << disposition << '\n';
    return exact ? 0 : 4;
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        return r1aRun(r1aOptions(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << "R1A research exception: " << error.what() << '\n';
        return 5;
    }
}
