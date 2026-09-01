#include "axis_transform.h"
#include "axis_mapping_transition.h"
#include "adaptive_response.h"
#include "automation_engine.h"
#include "button_mapping.h"
#include "mapping_worker.h"
#include "physical_input_monitor.h"
#include "profile_model.h"
#include "profile_trigger_runtime.h"
#include "response_curve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
thread_local bool gTrackHotPathAllocations = false;
thread_local std::uint64_t gHotPathAllocations = 0;
}

void *operator new(std::size_t size)
{
    if (void *memory = std::malloc(size)) {
        if (gTrackHotPathAllocations) ++gHotPathAllocations;
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWarmupReports = 25'000;
constexpr int kMeasuredReports = 400'000;
constexpr int kMeasuredProfileSwitches = 120'000;
constexpr std::array<int, 4> kMappedAxes{
    static_cast<int>(hotas::PhysicalAxis::X),
    static_cast<int>(hotas::PhysicalAxis::Y),
    static_cast<int>(hotas::PhysicalAxis::Z),
    static_cast<int>(hotas::PhysicalAxis::Rz),
};
constexpr std::array<int, hotas::kPhysicalAxisCount> kAllPhysicalAxes{
    static_cast<int>(hotas::PhysicalAxis::X), static_cast<int>(hotas::PhysicalAxis::Y),
    static_cast<int>(hotas::PhysicalAxis::Z), static_cast<int>(hotas::PhysicalAxis::Rx),
    static_cast<int>(hotas::PhysicalAxis::Ry), static_cast<int>(hotas::PhysicalAxis::Rz),
    static_cast<int>(hotas::PhysicalAxis::Slider0), static_cast<int>(hotas::PhysicalAxis::Slider1),
};

struct SyntheticReport {
    std::array<float, hotas::kPhysicalAxisCount> axes{};
    hotas::PhysicalButtonStates buttons{};
    int pov = -1;
};

struct RuntimePublication {
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> raw{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> normalized{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> afterDeadzone{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> afterHysteresis{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> afterInversion{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> curveResponse{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> transformed{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveEstimated{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptivePredicted{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveVelocity{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveAcceleration{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveHorizonSeconds{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveLead{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveConfidence{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveMotionIntensity{};
    std::array<std::atomic<int>, hotas::kPhysicalAxisCount> adaptiveMotionState{};
    std::array<std::atomic<bool>, hotas::kPhysicalAxisCount> adaptiveReversing{};
    std::array<std::atomic<bool>, hotas::kPhysicalAxisCount> adaptiveSafetyLimited{};
    std::array<std::atomic<bool>, hotas::kPhysicalAxisCount> adaptiveRuntimeEnabled{};
    std::array<std::atomic<int>, hotas::kPhysicalAxisCount> adaptiveRuntimeModel{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveRuntimeMaximumHorizonSeconds{};
    std::array<std::atomic<float>, hotas::kPhysicalAxisCount> adaptiveRuntimeMaximumLead{};
    std::array<std::atomic<bool>, hotas::kMaximumPhysicalButtons> physicalButtons{};
    std::array<std::atomic<bool>, hotas::kMaximumPhysicalButtons> virtualButtons{};
};

struct HotPathState {
    hotas::PhysicalInputMonitor physicalMonitor;
    std::array<bool, hotas::kPhysicalAxisCount> availableAxes{};
    std::array<bool, hotas::kMaximumPhysicalButtons> availableButtons{};
    std::array<hotas::AxisHysteresisState, hotas::kPhysicalAxisCount> hysteresis{};
    std::array<hotas::AdaptiveResponseProcessor, hotas::kPhysicalAxisCount> adaptiveResponse{};
    std::array<float, hotas::kVirtualAxisSlotCount> lastVirtualValues{};
    hotas::AxisMappingTransitionEngine axisTransitions;
    std::array<int, hotas::kVirtualAxisSlotCount> virtualAxisSources{};
    hotas::RuntimeButtonTargets buttonTargets{};
    hotas::RuntimePovTargets povTargets{};
    std::array<hotas::NativePovBinding, hotas::kMaximumPhysicalPovs> nativePovBindings{};
    std::array<int, hotas::kMaximumPhysicalPovs> lastNativePovValues{};
    hotas::VirtualButtonStates lastVirtualButtons{};
    RuntimePublication publication;
    // Match the production worker's fixed-size latency telemetry write. The
    // GUI-side percentile sort intentionally remains outside this benchmarked
    // report path.
    std::array<std::atomic<std::uint64_t>, hotas::kLatencyTelemetrySamples> latencySamples{};
    std::atomic<std::uint64_t> latencySampleCount{0};
    std::uint64_t latencySampleSequence = 0;
    std::uint64_t outputWriteDecisions = 0;

    explicit HotPathState(const hotas::RuntimeMappingConfiguration &mapping,
                          const hotas::RuntimePovProfileTriggers &povProfileTriggers = {},
                          const std::array<hotas::NativePovBinding,
                                           hotas::kMaximumPhysicalPovs> &nativePovs = {},
                          bool allAxes = false)
    {
        if (allAxes) {
            for (const int index : kAllPhysicalAxes) availableAxes[static_cast<size_t>(index)] = true;
        } else {
            for (const int index : kMappedAxes) availableAxes[static_cast<size_t>(index)] = true;
        }
        for (int index = 0; index < 15; ++index) availableButtons[static_cast<size_t>(index)] = true;
        physicalMonitor.configure(availableAxes, availableButtons, 1);
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        lastNativePovValues.fill(-2); // -1 is a valid centered POV value.
        virtualAxisSources.fill(-1);
        buttonTargets = hotas::buildRuntimeButtonTargets(mapping.buttons, 32);
        povTargets = hotas::buildRuntimePovTargets(mapping.povs, 32, povProfileTriggers);
        nativePovBindings = nativePovs;
    }
};

struct Percentiles {
    double typicalUs = 0.0;
    double p95Us = 0.0;
    double p99Us = 0.0;
    double worstUs = 0.0;
};

struct BenchmarkResult {
    std::string_view name;
    double syntheticReportsPerSecond = 0.0;
    Percentiles latency;
    Percentiles compile;
    std::uint64_t outputWriteDecisions = 0;
    std::uint64_t hotPathAllocations = 0;
    double stationaryWritesPerReport = 0.0;
};

float percentileUs(std::vector<std::uint64_t> samples, double fraction)
{
    if (samples.empty()) return 0.0F;
    const size_t index = std::min(samples.size() - 1,
        static_cast<size_t>(std::ceil(fraction * static_cast<double>(samples.size()))) - 1);
    std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(index), samples.end());
    return static_cast<float>(samples[index]) / 1000.0F;
}

Percentiles summarize(const std::vector<std::uint64_t> &samples)
{
    Percentiles result;
    result.typicalUs = percentileUs(samples, 0.50);
    result.p95Us = percentileUs(samples, 0.95);
    result.p99Us = percentileUs(samples, 0.99);
    result.worstUs = static_cast<double>(*std::max_element(samples.begin(), samples.end())) / 1000.0;
    return result;
}

std::vector<SyntheticReport> makeReports()
{
    std::vector<SyntheticReport> reports;
    reports.reserve(4096);
    std::uint32_t state = 0xC0FFEEu;
    for (int reportIndex = 0; reportIndex < 4096; ++reportIndex) {
        SyntheticReport report;
        for (const int axis : kAllPhysicalAxes) {
            state = state * 1664525u + 1013904223u;
            report.axes[static_cast<size_t>(axis)]
                = static_cast<float>((state >> 8) & 0xFFFFu) / 32767.5F - 1.0F;
        }
        report.buttons[static_cast<size_t>(reportIndex % 15)] = (reportIndex / 31) % 2 == 0;
        report.pov = (reportIndex / 97) % 2 == 0 ? 9000 : -1;
        reports.push_back(report);
    }
    return reports;
}

void processReport(const SyntheticReport &report, const hotas::RuntimeMappingConfiguration &mapping,
                   HotPathState &state, volatile float &sink)
{
    // This is deliberately the same allocation-free report work as the
    // worker after DirectInput has supplied a physical report. It excludes
    // device Poll/GetDeviceState and the vJoy driver call, which require real
    // hardware and are reported separately rather than fabricated here.
    hotas::PhysicalInputReport physicalReport;
    physicalReport.axes = report.axes;
    physicalReport.buttons = report.buttons;
    physicalReport.povs[0] = report.pov;
    state.physicalMonitor.accept(physicalReport);
    const hotas::PhysicalInputSnapshot &snapshot = state.physicalMonitor.snapshot();

    std::array<float, hotas::kVirtualAxisSlotCount> output{};
    std::array<bool, hotas::kVirtualAxisSlotCount> targetUsed{};
    state.virtualAxisSources.fill(-1);
    const auto reportTimestamp = Clock::now();
    for (int index = 0; index < hotas::kPhysicalAxisCount; ++index) {
        if (!state.availableAxes[static_cast<size_t>(index)]) continue;
        const float raw = snapshot.axes[static_cast<size_t>(index)];
        const hotas::RuntimeAxisMapping &axis = mapping.axes[static_cast<size_t>(index)];
        const float physicalNormalized = hotas::normalizeCalibrated(raw, axis.calibration);
        const hotas::AdaptiveResponseTelemetry adaptive =
            state.adaptiveResponse[static_cast<size_t>(index)].process(
                physicalNormalized, axis.adaptiveResponse, reportTimestamp);
        float curveResponse = 0.0F;
        hotas::AxisSignalPath path;
        const float transformed = hotas::transformNormalizedAxisLive(adaptive.predicted, axis,
            state.hysteresis[static_cast<size_t>(index)], &curveResponse, &path);
        state.publication.raw[static_cast<size_t>(index)].store(raw, std::memory_order_relaxed);
        state.publication.normalized[static_cast<size_t>(index)].store(physicalNormalized,
                                                                        std::memory_order_relaxed);
        state.publication.afterDeadzone[static_cast<size_t>(index)].store(path.afterDeadzone, std::memory_order_relaxed);
        state.publication.afterHysteresis[static_cast<size_t>(index)].store(path.afterHysteresis, std::memory_order_relaxed);
        state.publication.afterInversion[static_cast<size_t>(index)].store(path.afterInversion, std::memory_order_relaxed);
        state.publication.curveResponse[static_cast<size_t>(index)].store(curveResponse, std::memory_order_relaxed);
        state.publication.transformed[static_cast<size_t>(index)].store(transformed, std::memory_order_relaxed);
        // Match the production full Adaptive Response latest-state snapshot.
        // These fixed atomics are intentionally published without a UI call.
        state.publication.adaptiveEstimated[static_cast<size_t>(index)].store(adaptive.estimated, std::memory_order_relaxed);
        state.publication.adaptivePredicted[static_cast<size_t>(index)].store(adaptive.predicted, std::memory_order_relaxed);
        state.publication.adaptiveVelocity[static_cast<size_t>(index)].store(adaptive.velocity, std::memory_order_relaxed);
        state.publication.adaptiveAcceleration[static_cast<size_t>(index)].store(adaptive.acceleration, std::memory_order_relaxed);
        state.publication.adaptiveHorizonSeconds[static_cast<size_t>(index)].store(adaptive.activeHorizonSeconds, std::memory_order_relaxed);
        state.publication.adaptiveLead[static_cast<size_t>(index)].store(adaptive.lead, std::memory_order_relaxed);
        state.publication.adaptiveConfidence[static_cast<size_t>(index)].store(adaptive.confidence, std::memory_order_relaxed);
        state.publication.adaptiveMotionIntensity[static_cast<size_t>(index)].store(adaptive.motionIntensity, std::memory_order_relaxed);
        state.publication.adaptiveMotionState[static_cast<size_t>(index)].store(static_cast<int>(adaptive.state), std::memory_order_relaxed);
        state.publication.adaptiveReversing[static_cast<size_t>(index)].store(adaptive.reversal, std::memory_order_relaxed);
        state.publication.adaptiveSafetyLimited[static_cast<size_t>(index)].store(adaptive.safetyLimited, std::memory_order_relaxed);
        state.publication.adaptiveRuntimeEnabled[static_cast<size_t>(index)].store(axis.adaptiveResponse.enabled, std::memory_order_relaxed);
        state.publication.adaptiveRuntimeModel[static_cast<size_t>(index)].store(static_cast<int>(axis.adaptiveResponse.model), std::memory_order_relaxed);
        state.publication.adaptiveRuntimeMaximumHorizonSeconds[static_cast<size_t>(index)].store(axis.adaptiveResponse.maximumHorizonSeconds, std::memory_order_relaxed);
        state.publication.adaptiveRuntimeMaximumLead[static_cast<size_t>(index)].store(axis.adaptiveResponse.maximumLead, std::memory_order_relaxed);
        const int target = static_cast<int>(axis.profile.target);
        if (target > 0 && target < static_cast<int>(output.size()) && !targetUsed[static_cast<size_t>(target)]) {
            output[static_cast<size_t>(target)] = transformed;
            targetUsed[static_cast<size_t>(target)] = true;
            state.virtualAxisSources[static_cast<size_t>(target)] = index;
        }
    }

    for (int source = 0; source < hotas::kMaximumPhysicalButtons; ++source) {
        state.publication.physicalButtons[static_cast<size_t>(source)].store(
            snapshot.buttons[static_cast<size_t>(source)], std::memory_order_relaxed);
    }
    for (int target = 1; target < static_cast<int>(output.size()); ++target) {
        const int source = state.virtualAxisSources[static_cast<size_t>(target)];
        const float physicalInput = source >= 0 ? snapshot.axes[static_cast<size_t>(source)] : 0.0F;
        // Production has the same inactive-state branch. No transition is
        // scheduled in the steady-state benchmark, so this measures the
        // persistent cost without fabricating a curve transition workload.
        const float desired = state.axisTransitions.apply(static_cast<size_t>(target),
            targetUsed[static_cast<size_t>(target)] ? output[static_cast<size_t>(target)] : 0.0F,
            physicalInput, source, state.latencySampleSequence);
        if (!std::isfinite(state.lastVirtualValues[static_cast<size_t>(target)])
            || std::abs(desired - state.lastVirtualValues[static_cast<size_t>(target)]) >= 0.00001F) {
            state.lastVirtualValues[static_cast<size_t>(target)] = desired;
            ++state.outputWriteDecisions;
        }
    }
    hotas::VirtualButtonStates desiredButtons = hotas::mapButtonStates(
        snapshot.buttons, state.buttonTargets, 32);
    hotas::mapPovStates(desiredButtons, snapshot.povs, state.physicalMonitor.povCount(),
                         state.povTargets, 32);
    for (int target = 1; target <= hotas::kMaximumVirtualButtons; ++target) {
        const bool desired = desiredButtons[static_cast<size_t>(target)];
        if (desired == state.lastVirtualButtons[static_cast<size_t>(target)]) continue;
        state.lastVirtualButtons[static_cast<size_t>(target)] = desired;
        state.publication.virtualButtons[static_cast<size_t>(target - 1)].store(desired,
                                                                                  std::memory_order_relaxed);
        ++state.outputWriteDecisions;
    }
    // This models the worker's native POV change detection. The actual vJoy
    // DLL call remains excluded from this synthetic benchmark, just like the
    // existing axis/button driver calls above.
    const int nativePovHats = std::min(state.physicalMonitor.povCount(), hotas::kMaximumPhysicalPovs);
    for (int hat = 0; hat < nativePovHats; ++hat) {
        const hotas::NativePovBinding &binding = state.nativePovBindings[static_cast<size_t>(hat)];
        if (!binding.enabled || binding.targetType == hotas::NativePovTargetType::Disabled) continue;
        const int desired = snapshot.povs[static_cast<size_t>(hat)];
        if (desired == state.lastNativePovValues[static_cast<size_t>(hat)]) continue;
        state.lastNativePovValues[static_cast<size_t>(hat)] = desired;
        ++state.outputWriteDecisions;
    }
    const size_t latencySlot = static_cast<size_t>(state.latencySampleSequence
        % hotas::kLatencyTelemetrySamples);
    state.latencySamples[latencySlot].store(state.latencySampleSequence,
        std::memory_order_release);
    ++state.latencySampleSequence;
    state.latencySampleCount.store(std::min<std::uint64_t>(
        state.latencySampleSequence, hotas::kLatencyTelemetrySamples), std::memory_order_release);
    sink += output[1] + output[2] + output[3] + output[4];
}

hotas::CurveDefinition maximumDensityCustom(bool unipolar)
{
    hotas::CurveDefinition custom = hotas::materializeCurveDefinition(
        hotas::standardCurveDefinition(hotas::CurveFamily::SCurve, 1.0F),
        unipolar, 25);
    // Make the definition genuinely non-linear and nonuniform without putting
    // any point editing work in the measured report path.
    if (!unipolar) {
        hotas::updateCurvePoint(custom, false, 15, 0.28F, 0.12F);
        hotas::updateCurvePoint(custom, false, 20, 0.70F, 0.58F);
    } else {
        hotas::updateCurvePoint(custom, true, 8, 0.31F, 0.15F);
        hotas::updateCurvePoint(custom, true, 17, 0.71F, 0.60F);
    }
    return custom;
}

hotas::MapperConfiguration configurationFor(std::string_view name)
{
    hotas::MapperConfiguration configuration = hotas::defaultConfiguration();
    hotas::ControllerProfile &profile = hotas::activeProfile(configuration);
    const bool oneSided = name == "One-Sided Linear";
    const bool adaptiveCase = name == "Adaptive Response" || name == "Adaptive Velocity"
        || name == "Adaptive Alpha-Beta" || name == "Adaptive Alpha-Beta-Gamma"
        || name == "Adaptive Auto" || name == "Adaptive Extreme" || name == "Adaptive All 8 Axes";
    if (adaptiveCase) {
        for (hotas::AdaptiveResponseAxisOverride &axis : configuration.adaptiveResponseGlobal.axes) {
            axis.presetId = name == "Adaptive Extreme" ? QStringLiteral("extreme")
                                                        : QStringLiteral("fast");
            if (name == "Adaptive Velocity") {
                axis.properties = hotas::AdaptiveResponseModelProperty;
                axis.settings.model = hotas::AdaptiveResponseModel::Velocity;
            } else if (name == "Adaptive Alpha-Beta") {
                axis.properties = hotas::AdaptiveResponseModelProperty;
                axis.settings.model = hotas::AdaptiveResponseModel::AlphaBeta;
            } else if (name == "Adaptive Alpha-Beta-Gamma") {
                axis.properties = hotas::AdaptiveResponseModelProperty;
                axis.settings.model = hotas::AdaptiveResponseModel::AlphaBetaGamma;
            } else if (name == "Adaptive Auto") {
                axis.properties = hotas::AdaptiveResponseModelProperty;
                axis.settings.model = hotas::AdaptiveResponseModel::Auto;
            }
        }
    }
    profile.buttons = hotas::defaultButtonMappings(15, 32);
    profile.povs.resize(1);
    profile.povs[0][static_cast<size_t>(hotas::povDirectionIndex(hotas::PovDirection::Right))] =
        {hotas::ButtonActionType::VirtualButton, 16, true};
    configuration.nativePovBindings.resize(1);
    configuration.nativePovBindings[0] = {true, hotas::NativePovTargetType::Continuous, 1};
    for (int index = 0; index < hotas::kPhysicalAxisCount; ++index) {
        const bool unipolar = hotas::isUnipolarAxis(static_cast<hotas::PhysicalAxis>(index));
        if (oneSided) {
            profile.axes[static_cast<size_t>(index)].rangeMode = hotas::AxisRangeMode::OneSided;
        }
        if (name == "J-Curve") {
            profile.axes[static_cast<size_t>(index)].curve = hotas::standardCurveDefinition(
                hotas::CurveFamily::JCurve, 1.0F);
        } else if (name == "S-Curve") {
            profile.axes[static_cast<size_t>(index)].curve = hotas::standardCurveDefinition(
                hotas::CurveFamily::SCurve, 1.0F);
        } else if (name == "Advanced") {
            profile.axes[static_cast<size_t>(index)].curve = hotas::advancedCurveDefinition(
                QStringLiteral("precision-tracking"));
        } else if (name == "Shooter-Flight") {
            profile.axes[static_cast<size_t>(index)].curve = hotas::advancedCurveDefinition(
                QStringLiteral("shooter-dynamic-inspired"));
        } else if (name == "Personal") {
            hotas::CurveDefinition personal = hotas::materializeCurveDefinition(
                hotas::advancedCurveDefinition(QStringLiteral("aircraft-gun-tracking")), unipolar, 13);
            personal.family = hotas::CurveFamily::Personal;
            personal.pointEditing = false;
            personal.baseLabel = QStringLiteral("Bench Personal");
            profile.axes[static_cast<size_t>(index)].curve = std::move(personal);
        } else if (name == "Custom-25") {
            profile.axes[static_cast<size_t>(index)].curve = maximumDensityCustom(unipolar);
        }
    }
    return configuration;
}

Percentiles measureCompile(const hotas::MapperConfiguration &configuration)
{
    std::vector<std::uint64_t> samples;
    samples.reserve(80);
    for (int iteration = 0; iteration < 80; ++iteration) {
        const auto started = Clock::now();
        const auto compiled = hotas::compileActiveProfile(configuration);
        const auto finished = Clock::now();
        volatile float sink = hotas::evaluateCompiledResponseCurve(0.42F, compiled.axes[0].responseCurve);
        (void)sink;
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    return summarize(samples);
}

BenchmarkResult benchmark(std::string_view name, const std::vector<SyntheticReport> &reports)
{
    const hotas::MapperConfiguration configuration = configurationFor(name);
    const hotas::RuntimeProfileCache profileCache = hotas::compileRuntimeProfileCache(configuration);
    const hotas::RuntimeMappingConfiguration &mapping = profileCache.profiles[
        static_cast<size_t>(profileCache.baseProfileIndex)];
    const bool allAxes = name == "Adaptive All 8 Axes";
    HotPathState state(mapping, profileCache.povProfileTriggers, profileCache.nativePovBindings, allAxes);
    volatile float sink = 0.0F;
    for (int index = 0; index < kWarmupReports; ++index) {
        processReport(reports[static_cast<size_t>(index) % reports.size()], mapping, state, sink);
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(kMeasuredReports);
    gHotPathAllocations = 0;
    gTrackHotPathAllocations = true;
    const auto throughputStarted = Clock::now();
    for (int index = 0; index < kMeasuredReports; ++index) {
        const auto started = Clock::now();
        processReport(reports[static_cast<size_t>(index) % reports.size()], mapping, state, sink);
        const auto finished = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    const auto throughputFinished = Clock::now();
    gTrackHotPathAllocations = false;

    SyntheticReport stationary;
    HotPathState stationaryState(mapping, profileCache.povProfileTriggers,
                                 profileCache.nativePovBindings, allAxes);
    processReport(stationary, mapping, stationaryState, sink);
    const std::uint64_t initialWrites = stationaryState.outputWriteDecisions;
    for (int index = 0; index < 10'000; ++index) processReport(stationary, mapping, stationaryState, sink);
    const std::uint64_t repeatedWrites = stationaryState.outputWriteDecisions - initialWrites;

    const double elapsedSeconds = std::chrono::duration<double>(throughputFinished - throughputStarted).count();
    BenchmarkResult result;
    result.name = name;
    result.syntheticReportsPerSecond = static_cast<double>(kMeasuredReports) / elapsedSeconds;
    result.latency = summarize(samples);
    result.compile = measureCompile(configuration);
    result.outputWriteDecisions = state.outputWriteDecisions;
    result.hotPathAllocations = gHotPathAllocations;
    result.stationaryWritesPerReport = static_cast<double>(repeatedWrites) / 10'000.0;
    if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unexpected sink\n";
    return result;
}

void printResult(std::string_view condition, const BenchmarkResult &result)
{
    std::cout << std::fixed << std::setprecision(3)
              << condition << ' ' << result.name
              << " reports/s=" << result.syntheticReportsPerSecond
              << " typical_us=" << result.latency.typicalUs
              << " p95_us=" << result.latency.p95Us
              << " p99_us=" << result.latency.p99Us
              << " worst_us=" << result.latency.worstUs
              << " compile_p95_us=" << result.compile.p95Us
              << " output_decisions=" << result.outputWriteDecisions
              << " hot_path_allocations=" << result.hotPathAllocations
              << " stationary_writes_per_report=" << result.stationaryWritesPerReport
              << '\n';
}

struct ProfileControlBenchmarkResult {
    std::string_view name;
    Percentiles latency;
    std::uint64_t hotPathAllocations = 0;
    std::uint64_t curveCompilesDuringTriggers = 0;
};

template <typename Setup, typename Action>
ProfileControlBenchmarkResult benchmarkProfileControl(
    std::string_view name, const hotas::RuntimeProfileCache &cache,
    const SyntheticReport &baseReport, Setup &&setup, Action &&action)
{
    hotas::ProfileTriggerRuntime controls;
    hotas::PhysicalButtonStates buttons{};
    hotas::PhysicalPovValues povs{};
    povs.fill(-1);
    controls.initializeForMapping(cache, buttons, povs, 1);
    int currentProfile = cache.baseProfileIndex;
    HotPathState state(cache.profiles[static_cast<size_t>(currentProfile)],
                        cache.povProfileTriggers, cache.nativePovBindings);
    volatile float sink = 0.0F;
    const auto processControlReport = [&](const hotas::PhysicalButtonStates &physicalButtons) {
        const hotas::EffectiveProfileSelection selection = controls.processReport(cache, physicalButtons, povs, 1);
        if (selection.profileIndex != currentProfile) {
            currentProfile = selection.profileIndex;
            state.buttonTargets = hotas::buildRuntimeButtonTargets(
                cache.profiles[static_cast<size_t>(currentProfile)].buttons, 32, cache.profileTriggers);
            state.povTargets = hotas::buildRuntimePovTargets(
                cache.profiles[static_cast<size_t>(currentProfile)].povs, 32, cache.povProfileTriggers);
            state.lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
            state.lastNativePovValues.fill(-2);
            state.hysteresis.fill({});
        }
        SyntheticReport report = baseReport;
        report.buttons = physicalButtons;
        report.pov = povs[0];
        processReport(report, cache.profiles[static_cast<size_t>(currentProfile)], state, sink);
    };

    std::vector<std::uint64_t> samples;
    samples.reserve(kMeasuredProfileSwitches);
    const std::uint64_t beforeCurves = hotas::responseCurveCompileCount();
    gHotPathAllocations = 0;
    gTrackHotPathAllocations = true;
    for (int iteration = 0; iteration < kMeasuredProfileSwitches; ++iteration) {
        buttons.fill(false);
        povs.fill(-1);
        controls.initializeForMapping(cache, buttons, povs, 1);
        currentProfile = cache.baseProfileIndex;
        state.buttonTargets = hotas::buildRuntimeButtonTargets(
            cache.profiles[static_cast<size_t>(currentProfile)].buttons, 32, cache.profileTriggers);
        state.povTargets = hotas::buildRuntimePovTargets(
            cache.profiles[static_cast<size_t>(currentProfile)].povs, 32, cache.povProfileTriggers);
        state.lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        state.lastNativePovValues.fill(-2);
        state.hysteresis.fill({});
        setup(processControlReport, buttons, povs);
        const auto started = Clock::now();
        action(processControlReport, buttons, povs);
        const auto finished = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    gTrackHotPathAllocations = false;
    if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unexpected sink\n";
    return {name, summarize(samples), gHotPathAllocations,
            hotas::responseCurveCompileCount() - beforeCurves};
}

void printProfileControlResult(const ProfileControlBenchmarkResult &result)
{
    std::cout << std::fixed << std::setprecision(3)
              << "profile-control " << result.name
              << " typical_us=" << result.latency.typicalUs
              << " p95_us=" << result.latency.p95Us
              << " p99_us=" << result.latency.p99Us
              << " worst_us=" << result.latency.worstUs
              << " hot_path_allocations=" << result.hotPathAllocations
              << " curve_compiles_during_triggers=" << result.curveCompilesDuringTriggers
              << '\n';
}

void runProfileControlBenchmarks()
{
    hotas::MapperConfiguration configuration = configurationFor("Custom-25");
    QString helicopterId;
    hotas::createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId);
    hotas::ControllerProfile *precision = hotas::findProfile(configuration, hotas::precisionProfileId());
    hotas::ControllerProfile *helicopter = hotas::findProfile(configuration, helicopterId);
    if (!precision || !helicopter) return;
    precision->axes[0].curve = hotas::advancedCurveDefinition(QStringLiteral("precision-tracking"));
    helicopter->axes[0].curve = maximumDensityCustom(false);
    configuration.profileTriggers.resize(7);
    configuration.profileTriggers[4] = {hotas::precisionProfileId(), hotas::ProfileTriggerMode::Hold};
    configuration.profileTriggers[5] = {helicopterId, hotas::ProfileTriggerMode::Hold};
    configuration.profileTriggers[6] = {helicopterId, hotas::ProfileTriggerMode::Toggle};
    configuration.povProfileTriggers.resize(1);
    configuration.povProfileTriggers[0][static_cast<size_t>(hotas::povDirectionIndex(hotas::PovDirection::Up))]
        = {hotas::precisionProfileId(), hotas::ProfileTriggerMode::Hold};
    const hotas::RuntimeProfileCache cache = hotas::compileRuntimeProfileCache(configuration);
    SyntheticReport report;
    report.axes[0] = 0.42F;
    report.axes[1] = -0.31F;
    report.axes[2] = 0.18F;
    report.axes[5] = 0.27F;

    const auto noSetup = [](auto &, auto &, auto &) {};
    printProfileControlResult(benchmarkProfileControl("hold-activation", cache, report, noSetup,
        [](auto &process, auto &buttons, auto &) { buttons[4] = true; process(buttons); }));
    printProfileControlResult(benchmarkProfileControl("hold-release", cache, report,
        [](auto &process, auto &buttons, auto &) { buttons[4] = true; process(buttons); },
        [](auto &process, auto &buttons, auto &) { buttons[4] = false; process(buttons); }));
    printProfileControlResult(benchmarkProfileControl("toggle-activation", cache, report, noSetup,
        [](auto &process, auto &buttons, auto &) { buttons[6] = true; process(buttons); }));
    printProfileControlResult(benchmarkProfileControl("toggle-release", cache, report,
        [](auto &process, auto &buttons, auto &) { buttons[6] = true; process(buttons); buttons[6] = false; process(buttons); },
        [](auto &process, auto &buttons, auto &) { buttons[6] = true; process(buttons); }));
    printProfileControlResult(benchmarkProfileControl("multiple-hold-precedence", cache, report,
        [](auto &process, auto &buttons, auto &) { buttons[4] = true; process(buttons); },
        [](auto &process, auto &buttons, auto &) { buttons[5] = true; process(buttons); }));
    printProfileControlResult(benchmarkProfileControl("pov-hold-activation", cache, report, noSetup,
        [](auto &process, auto &buttons, auto &povValues) { povValues[0] = 0; process(buttons); }));
}

hotas::MapperConfiguration automationConfiguration(int ruleCount, bool temporal = false,
                                                   bool adaptiveOverlay = false)
{
    hotas::MapperConfiguration configuration = hotas::defaultConfiguration();
    for (int index = 0; index < ruleCount; ++index) {
        hotas::AutomationDefinition rule;
        rule.id = QStringLiteral("bench-%1").arg(index);
        rule.name = QStringLiteral("Benchmark %1").arg(index);
        rule.matchMode = index % 3 == 0 ? hotas::AutomationMatchMode::Any : hotas::AutomationMatchMode::All;
        hotas::AutomationConditionDefinition condition;
        if (temporal) {
            switch (index % 3) {
            case 0:
                condition.type = hotas::AutomationConditionType::ButtonPressed;
                condition.button = index % 15 + 1;
                break;
            case 1:
                condition.type = hotas::AutomationConditionType::ButtonMultiPress;
                condition.button = index % 15 + 1;
                condition.pressCount = 2;
                condition.multiPressWindowMs = 350;
                break;
            default:
                condition.type = hotas::AutomationConditionType::AxisCrossesAbove;
                condition.axis = static_cast<int>(hotas::PhysicalAxis::Z);
                condition.minimum = 0.15F;
                condition.hysteresis = 0.03F;
                break;
            }
            rule.activationMode = index % 3 == 0
                ? hotas::AutomationActivationMode::ToggleOnTrigger
                : index % 3 == 1 ? hotas::AutomationActivationMode::RunBriefly
                                 : hotas::AutomationActivationMode::WhileTriggerActive;
            rule.activeDurationMs = 250;
        } else {
        switch (index % 4) {
        case 0:
            condition.type = hotas::AutomationConditionType::AxisAbove;
            condition.axis = static_cast<int>(hotas::PhysicalAxis::Z);
            condition.minimum = 0.15F;
            condition.hysteresis = 0.03F;
            break;
        case 1:
            condition.type = hotas::AutomationConditionType::ButtonHeld;
            condition.button = index % 15 + 1;
            break;
        case 2:
            condition.type = hotas::AutomationConditionType::PovActive;
            condition.povHat = 1;
            condition.povDirection = hotas::PovDirection::Right;
            break;
        default:
            condition.type = hotas::AutomationConditionType::Always;
            break;
        }
        }
        rule.conditions.push_back(condition);
        hotas::AutomationActionDefinition action;
        if (temporal) {
            action.type = index % 2 == 0 ? hotas::AutomationActionType::VJoyButtonTap
                                         : hotas::AutomationActionType::VJoyButtonHold;
            action.virtualButton = index % 32 + 1;
            action.tapDurationMs = 80;
        } else {
        switch (index % 5) {
        case 0:
            action.type = hotas::AutomationActionType::VJoyButtonHold;
            action.virtualButton = index % 32 + 1;
            break;
        case 1:
            action.type = hotas::AutomationActionType::AxisScale;
            action.targetAxis = static_cast<int>(hotas::PhysicalAxis::Y);
            action.value = 0.75F;
            break;
        case 2:
            action.type = hotas::AutomationActionType::AxisMix;
            action.sourceAxis = static_cast<int>(hotas::PhysicalAxis::X);
            action.targetAxis = static_cast<int>(hotas::PhysicalAxis::Rz);
            action.sourceStage = hotas::AutomationAxisSourceStage::Physical;
            action.value = 0.12F;
            break;
        case 3:
            action.type = hotas::AutomationActionType::AxisOffset;
            action.targetAxis = static_cast<int>(hotas::PhysicalAxis::Y);
            action.value = 0.03F;
            break;
        default:
            action.type = hotas::AutomationActionType::AxisClamp;
            action.targetAxis = static_cast<int>(hotas::PhysicalAxis::X);
            action.minimum = -0.92F;
            action.maximum = 0.92F;
            break;
        }
        }
        rule.actions.push_back(action);
        configuration.automations.push_back(std::move(rule));
    }
    if (adaptiveOverlay) {
        hotas::AutomationActionDefinition response;
        response.type = hotas::AutomationActionType::AdaptiveResponsePreset;
        response.targetAxis = static_cast<int>(hotas::PhysicalAxis::X);
        response.adaptiveResponsePresetId = QStringLiteral("fast");
        hotas::AutomationDefinition rule;
        rule.id = QStringLiteral("adaptive-overlay");
        rule.name = QStringLiteral("Adaptive overlay benchmark");
        rule.conditions = {{hotas::AutomationConditionType::Always}};
        rule.actions = {response};
        rule.priority = 75;
        configuration.automations.push_back(std::move(rule));
    }
    return configuration;
}

void runAutomationBenchmark(int ruleCount, const std::vector<SyntheticReport> &reports,
                            bool temporal = false, bool adaptiveOverlay = false)
{
    const hotas::RuntimeProfileCache cache = hotas::compileRuntimeProfileCache(
        automationConfiguration(ruleCount, temporal, adaptiveOverlay));
    hotas::AutomationRuntime runtime;
    runtime.setCompiled(cache.automation.get());
    const auto runOne = [&](const SyntheticReport &report, volatile float &sink) {
        hotas::AutomationInputSnapshot input;
        input.physicalAxes = report.axes;
        input.axisAvailable.fill(true);
        input.buttons = report.buttons;
        input.povs.fill(-1);
        input.povs[0] = report.pov;
        input.povCount = 1;
        input.buttonCount = 15;
        input.baseProfileIndex = cache.baseProfileIndex;
        input.preAutomationEffectiveProfileIndex = cache.baseProfileIndex;
        if (temporal) input.timestamp = Clock::now();
        const hotas::AutomationEvaluationResult &effects = runtime.evaluate(input);
        std::array<float, hotas::kPhysicalAxisCount> processed = report.axes;
        runtime.applyAxisActions(input, processed);
        sink += processed[0] + processed[1] + (effects.heldButtons[1] ? 1.0F : 0.0F);
    };
    volatile float sink = 0.0F;
    for (int index = 0; index < kWarmupReports; ++index) runOne(reports[static_cast<size_t>(index) % reports.size()], sink);
    std::vector<std::uint64_t> samples;
    samples.reserve(kMeasuredReports);
    gHotPathAllocations = 0;
    gTrackHotPathAllocations = true;
    const auto throughputStarted = Clock::now();
    for (int index = 0; index < kMeasuredReports; ++index) {
        const auto started = Clock::now();
        runOne(reports[static_cast<size_t>(index) % reports.size()], sink);
        const auto finished = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
    }
    const auto throughputFinished = Clock::now();
    gTrackHotPathAllocations = false;
    const Percentiles timing = summarize(samples);
    const double seconds = std::chrono::duration<double>(throughputFinished - throughputStarted).count();
    std::cout << std::fixed << std::setprecision(3)
              << (adaptiveOverlay ? "automation adaptive-overlay rules="
                                  : temporal ? "automation temporal rules=" : "automation rules=") << ruleCount
              << " typical_us=" << timing.typicalUs
              << " p95_us=" << timing.p95Us
              << " p99_us=" << timing.p99Us
              << " worst_us=" << timing.worstUs
              << " reports/s=" << static_cast<double>(kMeasuredReports) / seconds
              << " hot_path_allocations=" << gHotPathAllocations << '\n';
    if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unexpected sink\n";
}

void runAutomationBenchmarks(const std::vector<SyntheticReport> &reports)
{
    for (const int ruleCount : {0, 8, 32, 64}) runAutomationBenchmark(ruleCount, reports);
    runAutomationBenchmark(64, reports, true);
    runAutomationBenchmark(8, reports, false, true);
}

void runUiModelStress(std::atomic_bool &stop)
{
    // This is intentionally harsher than normal browsing: it continuously
    // exercises the same materialization, point edit, analysis, gain sampling,
    // and full-table compilation that a rapid Curve Editor drag requests. It
    // runs on a separate thread, like the application's GUI thread does.
    hotas::MapperConfiguration configuration = configurationFor("Custom-25");
    std::uint64_t tick = 0;
    while (!stop.load(std::memory_order_relaxed)) {
        hotas::ControllerProfile &profile = hotas::activeProfile(configuration);
        const bool unipolar = hotas::isUnipolarAxis(hotas::PhysicalAxis::X);
        profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)].curve = maximumDensityCustom(unipolar);
        hotas::updateCurvePoint(profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)].curve,
            false, 15, 0.28F + static_cast<float>(tick % 5) * 0.002F, 0.12F);
        const hotas::CurveAnalysis analysis = hotas::analyzeCurveDefinition(
            profile.axes[static_cast<size_t>(hotas::PhysicalAxis::X)].curve, false);
        const hotas::RuntimeMappingConfiguration compiled = hotas::compileActiveProfile(configuration);
        volatile float sink = analysis.peakGain;
        for (int sample = 0; sample <= 200; ++sample) {
            sink += hotas::evaluateCompiledResponseCurve(
                -1.0F + static_cast<float>(sample) * 0.01F, compiled.axes[0].responseCurve);
        }
        if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unexpected UI sink\n";
        ++tick;
    }
}

void runSuite(std::string_view condition, const std::vector<SyntheticReport> &reports,
              bool runUiStress)
{
    std::atomic_bool stop{false};
    std::thread uiThread;
    if (runUiStress) uiThread = std::thread(runUiModelStress, std::ref(stop));
    for (const std::string_view name : {"Linear", "Adaptive Off", "Adaptive Velocity",
                                        "Adaptive Alpha-Beta", "Adaptive Alpha-Beta-Gamma",
                                        "Adaptive Auto", "Adaptive Extreme", "Adaptive All 8 Axes",
                                        "One-Sided Linear", "J-Curve", "S-Curve", "Advanced",
                                        "Shooter-Flight", "Personal", "Custom-25"}) {
        printResult(condition, benchmark(name, reports));
    }
    if (runUiStress) {
        stop.store(true, std::memory_order_relaxed);
        uiThread.join();
    }
}

} // namespace

int runMappingHotPathBenchmark(int argc, char *argv[])
{
    const std::vector<SyntheticReport> reports = makeReports();
    std::cout << "Synthetic report-to-output-decision benchmark; no DirectInput device or vJoy driver call is included.\n";
    std::cout << "All curve evaluations use the production immutable 4097-sample LUT.\n";
    const bool uiModelStress = argc > 1 && std::string_view(argv[1]) == "--ui-stress";
    runSuite(uiModelStress ? "ui-model-stress" : "idle", reports, uiModelStress);
    runProfileControlBenchmarks();
    runAutomationBenchmarks(reports);
    return 0;
}

#ifndef HOTAS_MAPPING_BENCHMARK_EMBEDDED
int main(int argc, char *argv[])
{
    return runMappingHotPathBenchmark(argc, argv);
}
#endif
