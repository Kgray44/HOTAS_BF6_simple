#include "axis_transform.h"
#include "button_mapping.h"
#include "mapping_worker.h"
#include "physical_input_monitor.h"
#include "profile_model.h"
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
constexpr std::array<int, 4> kMappedAxes{
    static_cast<int>(hotas::PhysicalAxis::X),
    static_cast<int>(hotas::PhysicalAxis::Y),
    static_cast<int>(hotas::PhysicalAxis::Z),
    static_cast<int>(hotas::PhysicalAxis::Rz),
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
    std::array<std::atomic<bool>, hotas::kMaximumPhysicalButtons> physicalButtons{};
    std::array<std::atomic<bool>, hotas::kMaximumPhysicalButtons> virtualButtons{};
};

struct HotPathState {
    hotas::PhysicalInputMonitor physicalMonitor;
    std::array<bool, hotas::kPhysicalAxisCount> availableAxes{};
    std::array<bool, hotas::kMaximumPhysicalButtons> availableButtons{};
    std::array<hotas::AxisHysteresisState, hotas::kPhysicalAxisCount> hysteresis{};
    std::array<float, 5> lastVirtualValues{};
    std::array<int, 5> virtualAxisSources{};
    hotas::RuntimeButtonTargets buttonTargets{};
    hotas::VirtualButtonStates lastVirtualButtons{};
    RuntimePublication publication;
    // Match the production worker's fixed-size latency telemetry write. The
    // GUI-side percentile sort intentionally remains outside this benchmarked
    // report path.
    std::array<std::atomic<std::uint64_t>, hotas::kLatencyTelemetrySamples> latencySamples{};
    std::atomic<std::uint64_t> latencySampleCount{0};
    std::uint64_t latencySampleSequence = 0;
    std::uint64_t outputWriteDecisions = 0;

    explicit HotPathState(const hotas::RuntimeMappingConfiguration &mapping)
    {
        for (const int index : kMappedAxes) availableAxes[static_cast<size_t>(index)] = true;
        for (int index = 0; index < 15; ++index) availableButtons[static_cast<size_t>(index)] = true;
        physicalMonitor.configure(availableAxes, availableButtons, 1);
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        virtualAxisSources.fill(-1);
        buttonTargets = hotas::buildRuntimeButtonTargets(mapping.buttons, 32);
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
        for (const int axis : kMappedAxes) {
            state = state * 1664525u + 1013904223u;
            report.axes[static_cast<size_t>(axis)]
                = static_cast<float>((state >> 8) & 0xFFFFu) / 32767.5F - 1.0F;
        }
        report.axes[static_cast<size_t>(hotas::PhysicalAxis::Z)]
            = (report.axes[static_cast<size_t>(hotas::PhysicalAxis::Z)] + 1.0F) * 0.5F;
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
    physicalReport.pov = report.pov;
    state.physicalMonitor.accept(physicalReport);
    const hotas::PhysicalInputSnapshot &snapshot = state.physicalMonitor.snapshot();

    std::array<float, 5> output{};
    std::array<bool, 5> targetUsed{};
    state.virtualAxisSources.fill(-1);
    for (int index = 0; index < hotas::kPhysicalAxisCount; ++index) {
        if (!state.availableAxes[static_cast<size_t>(index)]) continue;
        const float raw = snapshot.axes[static_cast<size_t>(index)];
        const hotas::RuntimeAxisMapping &axis = mapping.axes[static_cast<size_t>(index)];
        float curveResponse = 0.0F;
        hotas::AxisSignalPath path;
        const float transformed = hotas::transformAxisLive(raw, axis,
            state.hysteresis[static_cast<size_t>(index)], &curveResponse, &path);
        state.publication.raw[static_cast<size_t>(index)].store(raw, std::memory_order_relaxed);
        state.publication.normalized[static_cast<size_t>(index)].store(path.normalized, std::memory_order_relaxed);
        state.publication.afterDeadzone[static_cast<size_t>(index)].store(path.afterDeadzone, std::memory_order_relaxed);
        state.publication.afterHysteresis[static_cast<size_t>(index)].store(path.afterHysteresis, std::memory_order_relaxed);
        state.publication.afterInversion[static_cast<size_t>(index)].store(path.afterInversion, std::memory_order_relaxed);
        state.publication.curveResponse[static_cast<size_t>(index)].store(curveResponse, std::memory_order_relaxed);
        state.publication.transformed[static_cast<size_t>(index)].store(transformed, std::memory_order_relaxed);
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
        const float desired = targetUsed[static_cast<size_t>(target)] ? output[static_cast<size_t>(target)] : 0.0F;
        if (!std::isfinite(state.lastVirtualValues[static_cast<size_t>(target)])
            || std::abs(desired - state.lastVirtualValues[static_cast<size_t>(target)]) >= 0.00001F) {
            state.lastVirtualValues[static_cast<size_t>(target)] = desired;
            ++state.outputWriteDecisions;
        }
    }
    const hotas::VirtualButtonStates desiredButtons = hotas::mapButtonStates(
        snapshot.buttons, state.buttonTargets, 32);
    for (int target = 1; target <= hotas::kMaximumVirtualButtons; ++target) {
        const bool desired = desiredButtons[static_cast<size_t>(target)];
        if (desired == state.lastVirtualButtons[static_cast<size_t>(target)]) continue;
        state.lastVirtualButtons[static_cast<size_t>(target)] = desired;
        state.publication.virtualButtons[static_cast<size_t>(target - 1)].store(desired,
                                                                                  std::memory_order_relaxed);
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
    profile.buttons = hotas::defaultButtonMappings(15, 32);
    for (int index = 0; index < hotas::kPhysicalAxisCount; ++index) {
        const bool unipolar = hotas::isUnipolarAxis(static_cast<hotas::PhysicalAxis>(index));
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
    const hotas::RuntimeMappingConfiguration mapping = hotas::compileActiveProfile(configuration);
    HotPathState state(mapping);
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
    HotPathState stationaryState(mapping);
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
    for (const std::string_view name : {"Linear", "J-Curve", "S-Curve", "Advanced", "Shooter-Flight", "Personal", "Custom-25"}) {
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
    return 0;
}

#ifndef HOTAS_MAPPING_BENCHMARK_EMBEDDED
int main(int argc, char *argv[])
{
    return runMappingHotPathBenchmark(argc, argv);
}
#endif
