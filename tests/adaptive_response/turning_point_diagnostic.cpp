#include "adaptive_response.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hotas::AdaptiveResponsePreset;
using hotas::AdaptiveResponseProcessor;
using hotas::AdaptiveResponseSettings;
using hotas::AdaptiveResponseTelemetry;
using hotas::RuntimeAdaptiveResponseConfig;

constexpr double kWarmupSeconds = 0.20;
constexpr double kPostSettleSeconds = 0.40;
constexpr double kLeadEpsilon = 0.00010;

enum class Brake { Gentle, Aggressive };

struct Kinematics {
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

struct Trajectory {
    std::string id;
    std::string speed;
    std::string amplitudeLabel;
    std::string topology;
    std::string symmetry;
    std::string brake;
    double amplitude = 0.0;
    double start = 0.0;
    double peak = 0.0;
    double end = 0.0;
    double approachSeconds = 0.0;
    double departureSeconds = 0.0;
    Brake brakeMode = Brake::Gentle;

    double turnTime() const { return kWarmupSeconds + approachSeconds; }
    double departureEndTime() const { return turnTime() + departureSeconds; }
    double duration() const { return departureEndTime() + kPostSettleSeconds; }
};

struct Sample {
    double time = 0.0;
    Kinematics physical;
    AdaptiveResponseTelemetry telemetry;
    double futurePosition = 0.0;
    double futureError = 0.0;
};

struct MetricRow {
    std::string trajectory;
    std::string speed;
    std::string amplitude;
    std::string topology;
    std::string symmetry;
    std::string brake;
    std::string preset;
    int rate = 0;
    double physicalTurnMs = 0.0;
    double predictedTurnMs = 0.0;
    double timingAdvanceMs = 0.0;
    double physicalPeak = 0.0;
    double predictedPeak = 0.0;
    double peakError = 0.0;
    double leadBeforeTurn = 0.0;
    double leadAtPhysicalZero = 0.0;
    double horizonApproachingMs = 0.0;
    double estimatorVelocityAtTurn = 0.0;
    double estimatorAccelerationAtTurn = 0.0;
    double physicalVelocityAtTurn = 0.0;
    double physicalAccelerationAtTurn = 0.0;
    double futureErrorBeforeTurn = 0.0;
    double futureErrorAtTurn = 0.0;
    double futureMae = 0.0;
    double futureP95 = 0.0;
    double futureMax = 0.0;
    double turnWindowFutureMae = 0.0;
    double shiftFidelity = 0.0;
    double maxPhysicalStep = 0.0;
    double maxPredictedStep = 0.0;
    double maxArtificialStep = 0.0;
    double physicalBrakeStartMs = 0.0;
    double peakLeadBeforeBraking = 0.0;
    double usefulPreBrakingLeadMs = 0.0;
    double brakingDetectorActivationMs = -1.0;
    double brakingWarningBeforeZeroMs = -1.0;
    double leadAtBrakingDetector = 0.0;
    double leadReductionToPhysicalZero = 0.0;
    double maxBrakingReductionChange = 0.0;
    double maxBrakingReductionDerivative = 0.0;
    double reversalDetectionMs = -1.0;
    double staleCancellationMs = -1.0;
    double oppositeReacquisitionMs = -1.0;
    double settlingMs = -1.0;
    bool predictionReversal = false;
};

struct Run {
    MetricRow row;
    std::vector<Sample> samples;
    size_t beforeTurn = 0;
    size_t atTurn = 0;
    size_t afterTurn = 0;
};

std::string fixed(double value, int precision = 6)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string csv(std::string_view value)
{
    std::string escaped{"\""};
    for (const char character : value) escaped += character == '\"' ? "\"\"" : std::string(1, character);
    escaped += '"';
    return escaped;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::ceil(fraction * values.size())) - 1U;
    return values[std::min(index, values.size() - 1U)];
}

double mean(const std::vector<double> &values)
{
    if (values.empty()) return 0.0;
    double total = 0.0;
    for (const double value : values) total += value;
    return total / static_cast<double>(values.size());
}

RuntimeAdaptiveResponseConfig runtimeForSettings(const AdaptiveResponseSettings &settings)
{
    const AdaptiveResponseSettings clean = hotas::sanitizedAdaptiveResponseSettings(settings);
    RuntimeAdaptiveResponseConfig result;
    result.enabled = clean.enabled && clean.maximumHorizonMs > 0.0F;
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

std::vector<std::pair<std::string, RuntimeAdaptiveResponseConfig>> selectedPresets()
{
    std::vector<std::pair<std::string, RuntimeAdaptiveResponseConfig>> result;
    for (const AdaptiveResponsePreset &preset : hotas::builtInAdaptiveResponsePresets()) {
        const std::string name = preset.name.toStdString();
        if (name == "Balanced" || name == "Fast" || name == "Aggressive") {
            result.emplace_back(name, runtimeForSettings(preset.axes[0].settings));
        }
    }
    return result;
}

// Each incoming leg starts at cruise speed and decelerates continuously to
// zero.  Gentle braking spends 55% of displacement in the deceleration zone;
// aggressive braking postpones it to the final 20%.  The opposite leg is the
// exact time reverse, so a symmetric trajectory preserves the physical shape.
Kinematics incomingLeg(double time, double duration, Brake brake)
{
    const double brakeDistance = brake == Brake::Gentle ? 0.55 : 0.20;
    const double brakeSeconds = duration / (1.0 + (1.0 / brakeDistance - 1.0) / 2.0);
    const double cruiseSeconds = duration - brakeSeconds;
    const double cruiseVelocity = 2.0 * brakeDistance / brakeSeconds;
    if (time <= 0.0) return {0.0, cruiseVelocity, 0.0};
    if (time < cruiseSeconds) return {cruiseVelocity * time, cruiseVelocity, 0.0};
    if (time >= duration) return {1.0, 0.0, 0.0};
    const double unit = (time - cruiseSeconds) / brakeSeconds;
    const double position = (1.0 - brakeDistance)
        + cruiseVelocity * brakeSeconds * (unit / 2.0 + std::sin(std::numbers::pi * unit) / (2.0 * std::numbers::pi));
    const double velocity = cruiseVelocity * (1.0 + std::cos(std::numbers::pi * unit)) / 2.0;
    const double acceleration = -cruiseVelocity * std::numbers::pi * std::sin(std::numbers::pi * unit) / (2.0 * brakeSeconds);
    return {position, velocity, acceleration};
}

double brakingStartTime(const Trajectory &trajectory)
{
    const double brakeDistance = trajectory.brakeMode == Brake::Gentle ? 0.55 : 0.20;
    const double brakeSeconds = trajectory.approachSeconds
        / (1.0 + (1.0 / brakeDistance - 1.0) / 2.0);
    return kWarmupSeconds + trajectory.approachSeconds - brakeSeconds;
}

Kinematics physicalAt(const Trajectory &trajectory, double time)
{
    if (time <= kWarmupSeconds) return {trajectory.start, 0.0, 0.0};
    const double turn = trajectory.turnTime();
    if (time <= turn) {
        const Kinematics leg = incomingLeg(time - kWarmupSeconds, trajectory.approachSeconds, trajectory.brakeMode);
        const double delta = trajectory.peak - trajectory.start;
        return {trajectory.start + delta * leg.position, delta * leg.velocity, delta * leg.acceleration};
    }
    const double departureEnd = trajectory.departureEndTime();
    if (time <= departureEnd) {
        const double elapsed = time - turn;
        const Kinematics reversed = incomingLeg(trajectory.departureSeconds - elapsed,
                                                trajectory.departureSeconds, trajectory.brakeMode);
        const double progress = 1.0 - reversed.position;
        const double delta = trajectory.end - trajectory.peak;
        return {trajectory.peak + delta * progress, delta * reversed.velocity, -delta * reversed.acceleration};
    }
    return {trajectory.end, 0.0, 0.0};
}

std::vector<Trajectory> matrix()
{
    const std::array<std::pair<const char *, double>, 4> speeds{{
        {"slow", 1.20}, {"medium", 0.60}, {"fast", 0.30}, {"very-fast", 0.10},
    }};
    const std::array<double, 3> amplitudes{{0.25, 0.50, 1.00}};
    std::vector<Trajectory> result;
    result.reserve(24);
    for (const auto &[speed, duration] : speeds) {
        for (const double amplitude : amplitudes) {
            const std::string amplitudeLabel = std::to_string(static_cast<int>(amplitude * 100.0 + 0.5)) + "%";
            if (amplitude == 0.25) {
                result.push_back({std::string(speed) + "-plus-side-symmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "positive-side-only", "symmetric", "gentle", amplitude,
                    0.10 * amplitude, amplitude, 0.10 * amplitude, duration, duration, Brake::Gentle});
                result.push_back({std::string(speed) + "-minus-side-asymmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "negative-side-only", "asymmetric", "aggressive", amplitude,
                    -0.10 * amplitude, -amplitude, -0.45 * amplitude, duration, duration * 0.62, Brake::Aggressive});
            } else if (amplitude == 0.50) {
                result.push_back({std::string(speed) + "-minus-side-symmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "negative-side-only", "symmetric", "gentle", amplitude,
                    -0.10 * amplitude, -amplitude, -0.10 * amplitude, duration, duration, Brake::Gentle});
                result.push_back({std::string(speed) + "-plus-side-asymmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "positive-side-only", "asymmetric", "aggressive", amplitude,
                    0.10 * amplitude, amplitude, 0.45 * amplitude, duration, duration * 0.62, Brake::Aggressive});
            } else {
                result.push_back({std::string(speed) + "-center-symmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "center-crossing", "symmetric", "gentle", amplitude,
                    -amplitude, amplitude, -amplitude, duration, duration, Brake::Gentle});
                result.push_back({std::string(speed) + "-center-asymmetric-" + amplitudeLabel,
                    speed, amplitudeLabel, "center-crossing", "asymmetric", "aggressive", amplitude,
                    amplitude, -amplitude, 0.55 * amplitude, duration, duration * 0.62, Brake::Aggressive});
            }
        }
    }
    return result;
}

size_t closestSample(const std::vector<Sample> &samples, double timestamp)
{
    size_t best = 0;
    double distance = std::numeric_limits<double>::infinity();
    for (size_t index = 0; index < samples.size(); ++index) {
        const double candidate = std::abs(samples[index].time - timestamp);
        if (candidate < distance) { distance = candidate; best = index; }
    }
    return best;
}

Run run(const Trajectory &trajectory, const std::string &preset,
        const RuntimeAdaptiveResponseConfig &configuration, int rate)
{
    Run result;
    MetricRow &row = result.row;
    row.trajectory = trajectory.id;
    row.speed = trajectory.speed;
    row.amplitude = trajectory.amplitudeLabel;
    row.topology = trajectory.topology;
    row.symmetry = trajectory.symmetry;
    row.brake = trajectory.brake;
    row.preset = preset;
    row.rate = rate;
    const double dt = 1.0 / static_cast<double>(rate);
    const auto origin = std::chrono::steady_clock::time_point{};
    AdaptiveResponseProcessor processor;
    for (size_t index = 0;; ++index) {
        const double time = std::min(trajectory.duration(), static_cast<double>(index) * dt);
        const Kinematics physical = physicalAt(trajectory, time);
        const auto timestamp = origin + std::chrono::microseconds(static_cast<long long>(std::llround(time * 1000000.0)));
        const AdaptiveResponseTelemetry telemetry = processor.process(static_cast<float>(physical.position), configuration, timestamp);
        const double futureTime = std::min(trajectory.duration(), time + telemetry.activeHorizonSeconds);
        const double future = physicalAt(trajectory, futureTime).position;
        result.samples.push_back({time, physical, telemetry, future, static_cast<double>(telemetry.predicted) - future});
        if (time >= trajectory.duration()) break;
    }
    const double turn = trajectory.turnTime();
    const double brakingStart = brakingStartTime(trajectory);
    result.atTurn = closestSample(result.samples, turn);
    result.beforeTurn = result.atTurn == 0 ? 0 : result.atTurn - 1;
    result.afterTurn = std::min(result.samples.size() - 1U, result.atTurn + 2U);
    const int oldDirection = trajectory.peak > trajectory.start ? 1 : -1;
    const int peakDirection = trajectory.peak >= 0.0 ? 1 : -1;
    size_t predictedPeak = result.atTurn;
    const double peakWindowStart = turn - trajectory.approachSeconds * 0.55;
    const double peakWindowEnd = turn + trajectory.departureSeconds * 0.55;
    for (size_t index = 0; index < result.samples.size(); ++index) {
        if (result.samples[index].time < peakWindowStart || result.samples[index].time > peakWindowEnd) continue;
        if (peakDirection * result.samples[index].telemetry.predicted
            > peakDirection * result.samples[predictedPeak].telemetry.predicted) predictedPeak = index;
    }
    std::vector<double> futureErrors;
    std::vector<double> turnWindowErrors;
    double maxPhysicalStep = 0.0;
    double maxPredictedStep = 0.0;
    double maxArtificialStep = 0.0;
    double maxBrakingReductionChange = 0.0;
    double maxBrakingReductionDerivative = 0.0;
    for (size_t index = 0; index < result.samples.size(); ++index) {
        const Sample &sample = result.samples[index];
        const double error = std::abs(sample.futureError);
        futureErrors.push_back(error);
        if (sample.time >= peakWindowStart && sample.time <= peakWindowEnd) turnWindowErrors.push_back(error);
        if (index > 0) {
            const double physicalStep = sample.physical.position - result.samples[index - 1].physical.position;
            const double predictedStep = sample.telemetry.predicted - result.samples[index - 1].telemetry.predicted;
            maxPhysicalStep = std::max(maxPhysicalStep, std::abs(physicalStep));
            maxPredictedStep = std::max(maxPredictedStep, std::abs(predictedStep));
            maxArtificialStep = std::max(maxArtificialStep, std::abs(predictedStep - physicalStep));
            const double reductionChange = std::abs(sample.telemetry.brakingReductionFactor
                - result.samples[index - 1].telemetry.brakingReductionFactor);
            maxBrakingReductionChange = std::max(maxBrakingReductionChange, reductionChange);
            const double reductionDt = std::max(0.000001, sample.time - result.samples[index - 1].time);
            maxBrakingReductionDerivative = std::max(maxBrakingReductionDerivative, reductionChange / reductionDt);
        }
    }
    for (size_t index = result.atTurn; index < result.samples.size(); ++index) {
        const Sample &sample = result.samples[index];
        const double latency = (sample.time - turn) * 1000.0;
        if (!row.predictionReversal && sample.telemetry.reversal) {
            row.predictionReversal = true;
            row.reversalDetectionMs = latency;
        }
        if (row.staleCancellationMs < 0.0 && oldDirection * sample.telemetry.lead <= kLeadEpsilon) {
            row.staleCancellationMs = latency;
        }
        if (row.oppositeReacquisitionMs < 0.0 && oldDirection * sample.telemetry.lead < -kLeadEpsilon) {
            row.oppositeReacquisitionMs = latency;
        }
    }
    const double settleStart = trajectory.departureEndTime();
    for (size_t index = closestSample(result.samples, settleStart); index < result.samples.size(); ++index) {
        bool remainsSettled = true;
        const double end = std::min(trajectory.duration(), result.samples[index].time + 0.12);
        for (size_t probe = index; probe < result.samples.size() && result.samples[probe].time <= end; ++probe) {
            if (std::abs(result.samples[probe].telemetry.lead) > 0.001
                || std::abs(result.samples[probe].telemetry.velocity) > 0.010) {
                remainsSettled = false;
                break;
            }
        }
        if (remainsSettled) { row.settlingMs = (result.samples[index].time - settleStart) * 1000.0; break; }
    }
    const Sample &before = result.samples[result.beforeTurn];
    const Sample &at = result.samples[result.atTurn];
    const size_t brakingStartIndex = closestSample(result.samples, brakingStart);
    size_t preBrakingSample = brakingStartIndex;
    if (result.samples[preBrakingSample].time > brakingStart && preBrakingSample > 0) --preBrakingSample;
    const Sample &preBraking = result.samples[preBrakingSample];
    size_t detectorIndex = result.samples.size();
    for (size_t index = 0; index <= result.atTurn; ++index) {
        const Sample &sample = result.samples[index];
        if (sample.time >= brakingStart && sample.telemetry.sourceBrakingDetected) {
            detectorIndex = index;
            break;
        }
    }
    row.physicalTurnMs = turn * 1000.0;
    row.predictedTurnMs = result.samples[predictedPeak].time * 1000.0;
    row.timingAdvanceMs = row.physicalTurnMs - row.predictedTurnMs;
    row.physicalPeak = trajectory.peak;
    row.predictedPeak = result.samples[predictedPeak].telemetry.predicted;
    row.peakError = row.predictedPeak - row.physicalPeak;
    row.leadBeforeTurn = before.telemetry.lead;
    row.leadAtPhysicalZero = at.telemetry.lead;
    row.horizonApproachingMs = before.telemetry.activeHorizonSeconds * 1000.0;
    row.estimatorVelocityAtTurn = at.telemetry.velocity;
    row.estimatorAccelerationAtTurn = at.telemetry.acceleration;
    row.physicalVelocityAtTurn = at.physical.velocity;
    row.physicalAccelerationAtTurn = at.physical.acceleration;
    row.futureErrorBeforeTurn = before.futureError;
    row.futureErrorAtTurn = at.futureError;
    row.futureMae = mean(futureErrors);
    row.futureP95 = percentile(futureErrors, 0.95);
    row.futureMax = *std::max_element(futureErrors.begin(), futureErrors.end());
    row.turnWindowFutureMae = mean(turnWindowErrors);
    const double excursion = std::max(0.05, std::abs(trajectory.peak - trajectory.start));
    row.shiftFidelity = std::clamp(1.0 - row.turnWindowFutureMae / excursion, 0.0, 1.0);
    row.maxPhysicalStep = maxPhysicalStep;
    row.maxPredictedStep = maxPredictedStep;
    row.maxArtificialStep = maxArtificialStep;
    row.physicalBrakeStartMs = brakingStart * 1000.0;
    for (const Sample &sample : result.samples) {
        if (sample.time >= brakingStart) break;
        row.peakLeadBeforeBraking = std::max(row.peakLeadBeforeBraking,
            oldDirection * static_cast<double>(sample.telemetry.lead));
    }
    row.usefulPreBrakingLeadMs = oldDirection * static_cast<double>(preBraking.telemetry.lead)
        / std::max(0.001, std::abs(preBraking.physical.velocity)) * 1000.0;
    if (detectorIndex < result.samples.size()) {
        const Sample &detector = result.samples[detectorIndex];
        row.brakingDetectorActivationMs = detector.time * 1000.0;
        row.brakingWarningBeforeZeroMs = (turn - detector.time) * 1000.0;
        row.leadAtBrakingDetector = detector.telemetry.lead;
        row.leadReductionToPhysicalZero = oldDirection
            * (static_cast<double>(detector.telemetry.lead) - static_cast<double>(at.telemetry.lead));
    }
    row.maxBrakingReductionChange = maxBrakingReductionChange;
    row.maxBrakingReductionDerivative = maxBrakingReductionDerivative;
    return result;
}

void writeResultsCsv(const std::filesystem::path &path, const std::vector<Run> &runs)
{
    std::ofstream out(path);
    out << "trajectory,speed,amplitude,topology,symmetry,brake,preset,rate_hz,physical_turn_ms,predicted_turn_ms,timing_advance_ms,physical_peak,predicted_peak,peak_error,lead_before_turn,lead_at_physical_velocity_zero,horizon_approaching_ms,estimator_velocity_at_turn,estimator_acceleration_at_turn,physical_velocity_at_turn,physical_acceleration_at_turn,future_error_before_turn,future_error_at_turn,future_position_mae,future_position_p95,future_position_max,turn_window_future_mae,shape_time_shift_fidelity,max_physical_one_sample_step,max_predicted_one_sample_step,max_predictor_only_artificial_step,reversal_detection_latency_ms,stale_lead_cancellation_latency_ms,opposite_direction_reacquisition_latency_ms,settling_time_ms,physical_brake_start_ms,peak_lead_before_braking,useful_pre_braking_lead_ms,braking_detector_activation_ms,braking_warning_before_zero_ms,lead_at_braking_detector,lead_reduction_to_physical_zero,max_braking_reduction_change,max_braking_reduction_derivative\n";
    for (const Run &run : runs) {
        const MetricRow &r = run.row;
        out << csv(r.trajectory) << ',' << csv(r.speed) << ',' << csv(r.amplitude) << ',' << csv(r.topology) << ','
            << csv(r.symmetry) << ',' << csv(r.brake) << ',' << csv(r.preset) << ',' << r.rate;
        for (const double value : {r.physicalTurnMs, r.predictedTurnMs, r.timingAdvanceMs, r.physicalPeak, r.predictedPeak,
                                   r.peakError, r.leadBeforeTurn, r.leadAtPhysicalZero, r.horizonApproachingMs,
                                   r.estimatorVelocityAtTurn, r.estimatorAccelerationAtTurn, r.physicalVelocityAtTurn,
                                   r.physicalAccelerationAtTurn, r.futureErrorBeforeTurn, r.futureErrorAtTurn,
                                   r.futureMae, r.futureP95, r.futureMax, r.turnWindowFutureMae, r.shiftFidelity,
                                   r.maxPhysicalStep, r.maxPredictedStep, r.maxArtificialStep, r.reversalDetectionMs,
                                   r.staleCancellationMs, r.oppositeReacquisitionMs, r.settlingMs, r.physicalBrakeStartMs,
                                   r.peakLeadBeforeBraking, r.usefulPreBrakingLeadMs, r.brakingDetectorActivationMs,
                                   r.brakingWarningBeforeZeroMs, r.leadAtBrakingDetector, r.leadReductionToPhysicalZero,
                                   r.maxBrakingReductionChange, r.maxBrakingReductionDerivative}) out << ',' << fixed(value);
        out << '\n';
    }
}

void writeTraceCsv(const std::filesystem::path &path, const Run &run)
{
    std::ofstream out(path);
    out << "time_ms,physical,physical_velocity,physical_acceleration,predicted,lead,active_horizon_ms,estimator_velocity,estimator_acceleration,motion_coherence,confidence,source_update_period_ms,quiet_duration_ms,source_stopping_ms,braking_reduction_factor,source_braking_detected,predictor_state,reversal,safety_limited,source_sample_updated,source_cadence_hz,source_hold,future_physical_at_active_horizon,future_position_error\n";
    for (const Sample &sample : run.samples) {
        out << fixed(sample.time * 1000.0) << ',' << fixed(sample.physical.position) << ',' << fixed(sample.physical.velocity)
            << ',' << fixed(sample.physical.acceleration) << ',' << fixed(sample.telemetry.predicted) << ',' << fixed(sample.telemetry.lead)
            << ',' << fixed(sample.telemetry.activeHorizonSeconds * 1000.0) << ',' << fixed(sample.telemetry.velocity)
            << ',' << fixed(sample.telemetry.acceleration) << ',' << fixed(sample.telemetry.motionCoherence)
            << ',' << fixed(sample.telemetry.confidence) << ',' << fixed(sample.telemetry.sourceUpdatePeriodSeconds * 1000.0)
            << ',' << fixed(sample.telemetry.quietDurationSeconds * 1000.0)
            << ',' << fixed(sample.telemetry.sourceStoppingSeconds * 1000.0)
            << ',' << fixed(sample.telemetry.brakingReductionFactor)
            << ',' << (sample.telemetry.sourceBrakingDetected ? 1 : 0)
            << ',' << static_cast<int>(sample.telemetry.state) << ',' << (sample.telemetry.reversal ? 1 : 0)
            << ',' << (sample.telemetry.safetyLimited ? 1 : 0)
            << ",1," << run.row.rate << ",0," << fixed(sample.futurePosition) << ',' << fixed(sample.futureError) << '\n';
    }
}

double aggregate(const std::vector<const Run *> &runs, double MetricRow::*member)
{
    double total = 0.0;
    for (const Run *run : runs) total += run->row.*member;
    return runs.empty() ? 0.0 : total / static_cast<double>(runs.size());
}

void writeReport(const std::filesystem::path &path, const std::vector<Run> &runs)
{
    std::ofstream out(path);
    out << "# V2.3.T Turning-Point / Reversal Diagnostic\n\n";
    out << "Testing-only, deterministic offline evidence. The diagnostic invokes the current production `AdaptiveResponseProcessor`; it does not execute MappingWorker, DirectInput, vJoy, UI, or modify production behavior.\n\n";
    out << "## Scope\n\n24 continuous reversal trajectories: 4 speeds × 3 amplitudes × 2 topology/braking variants. Each runs at 30, 60, 125, and 250 Hz under the current Balanced, Fast, and Aggressive built-in configurations (288 runs). `future position` is the authored physical path evaluated at each sample's active horizon.\n\n";
    out << "| Preset | Runs | Mean shift-fidelity | Mean future MAE | Mean turn-window MAE | Mean timing advance ms | Mean peak error | Mean max artificial step | Detection observed |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const std::string preset : {"Balanced", "Fast", "Aggressive"}) {
        std::vector<const Run *> group;
        for (const Run &run : runs) if (run.row.preset == preset) group.push_back(&run);
        size_t detections = 0;
        for (const Run *run : group) detections += run->row.predictionReversal ? 1U : 0U;
        out << "| " << preset << " | " << group.size() << " | " << fixed(aggregate(group, &MetricRow::shiftFidelity), 4)
            << " | " << fixed(aggregate(group, &MetricRow::futureMae), 5) << " | " << fixed(aggregate(group, &MetricRow::turnWindowFutureMae), 5)
            << " | " << fixed(aggregate(group, &MetricRow::timingAdvanceMs), 2) << " | " << fixed(aggregate(group, &MetricRow::peakError), 5)
            << " | " << fixed(aggregate(group, &MetricRow::maxArtificialStep), 5) << " | " << detections << "/" << group.size() << " |\n";
    }
    std::vector<const Run *> fast;
    for (const Run &run : runs) if (run.row.preset == "Fast") fast.push_back(&run);
    std::sort(fast.begin(), fast.end(), [](const Run *a, const Run *b) {
        return a->row.turnWindowFutureMae > b->row.turnWindowFutureMae;
    });
    out << "\n## Fast preset: highest turning-window mismatch\n\n";
    out << "| Trajectory | Hz | Turn ms | Predicted peak ms | Advance ms | Peak error | Lead pre / at zero | Horizon pre ms | Future MAE / p95 / max | Turn MAE | Fidelity | Artificial step | Detect / cancel / reacquire ms | Settle ms |\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (size_t index = 0; index < std::min<size_t>(12, fast.size()); ++index) {
        const MetricRow &r = fast[index]->row;
        out << "| `" << r.trajectory << "` | " << r.rate << " | " << fixed(r.physicalTurnMs, 1) << " | " << fixed(r.predictedTurnMs, 1)
            << " | " << fixed(r.timingAdvanceMs, 1) << " | " << fixed(r.peakError, 5) << " | " << fixed(r.leadBeforeTurn, 5) << " / " << fixed(r.leadAtPhysicalZero, 5)
            << " | " << fixed(r.horizonApproachingMs, 2) << " | " << fixed(r.futureMae, 5) << " / " << fixed(r.futureP95, 5) << " / " << fixed(r.futureMax, 5)
            << " | " << fixed(r.turnWindowFutureMae, 5) << " | " << fixed(r.shiftFidelity, 4) << " | " << fixed(r.maxArtificialStep, 5)
            << " | " << fixed(r.reversalDetectionMs, 1) << " / " << fixed(r.staleCancellationMs, 1) << " / " << fixed(r.oppositeReacquisitionMs, 1)
            << " | " << fixed(r.settlingMs, 1) << " |\n";
    }
    out << "\n## Fast telemetry examples (before turn / physical zero / after turn)\n\n";
    out << "| Trajectory | Hz | Phase | Physical | Predicted | Lead | Horizon ms | Est. velocity | Est. acceleration | Future error | Reversal |\n|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (size_t rank = 0; rank < std::min<size_t>(3, fast.size()); ++rank) {
        const Run &run = *fast[rank];
        for (const auto &[label, sampleIndex] : std::array<std::pair<const char *, size_t>, 3>{{
                 {"before", run.beforeTurn}, {"physical zero", run.atTurn}, {"after", run.afterTurn},
             }}) {
            const Sample &s = run.samples[sampleIndex];
            out << "| `" << run.row.trajectory << "` | " << run.row.rate << " | " << label << " | " << fixed(s.physical.position, 5)
                << " | " << fixed(s.telemetry.predicted, 5) << " | " << fixed(s.telemetry.lead, 5)
                << " | " << fixed(s.telemetry.activeHorizonSeconds * 1000.0, 2) << " | " << fixed(s.telemetry.velocity, 5)
                << " | " << fixed(s.telemetry.acceleration, 5) << " | " << fixed(s.futureError, 5)
                << " | " << (s.telemetry.reversal ? "yes" : "no") << " |\n";
        }
    }
    out << "\n## Interpretation rule\n\nA predictor is acting mainly as a time advance when the shifted-path error remains small through the turn, predicted peak remains close to the physical peak, and predictor-only step stays below the physical step. A turning-point distortion appears as elevated turn-window shifted-path error, peak error, or artificial step despite otherwise positive timing advance. See `turning_point_results.csv` for every run and `fast_traces/` for all Fast-preset samples.\n";
}

} // namespace

int main(int argc, char *argv[])
{
    std::filesystem::path output = "artifacts/adaptive_response_validation/v2.3.T-turning-point";
    if (argc == 3 && std::string_view(argv[1]) == "--output") output = argv[2];
    if (argc != 1 && argc != 3) {
        std::cerr << "Usage: adaptive_turning_point_diagnostic [--output <directory>]\n";
        return 2;
    }
    const auto presets = selectedPresets();
    if (presets.size() != 3) {
        std::cerr << "Current Balanced, Fast, and Aggressive presets were not all available.\n";
        return 3;
    }
    std::filesystem::create_directories(output / "fast_traces");
    std::filesystem::create_directories(output / "braking_signal_traces");
    std::vector<Run> runs;
    runs.reserve(24U * 4U * presets.size());
    for (const Trajectory &trajectory : matrix()) {
        for (const int rate : {30, 60, 125, 250}) {
            for (const auto &[name, configuration] : presets) {
                runs.push_back(run(trajectory, name, configuration, rate));
                if (name == "Fast") {
                    const std::string file = trajectory.id + "-" + std::to_string(rate) + "hz.csv";
                    writeTraceCsv(output / "fast_traces" / file, runs.back());
                }
                if (name == "Balanced" || name == "Fast" || name == "Aggressive") {
                    const std::string file = trajectory.id + "-" + name + "-" + std::to_string(rate) + "hz.csv";
                    writeTraceCsv(output / "braking_signal_traces" / file, runs.back());
                }
            }
        }
    }
    writeResultsCsv(output / "turning_point_results.csv", runs);
    writeReport(output / "V2.3.T_Turning_Point_Report.md", runs);
    std::cout << "V2.3.T turning-point diagnostic completed: trajectories=24 runs=" << runs.size()
              << " output=" << output.string() << '\n';
    return 0;
}
