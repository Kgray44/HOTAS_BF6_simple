// X3 is deliberately an offline production-shaped replay target.  It imports
// the qualified X2S evaluator only as an oracle; this executable is not linked
// into MappingWorker, DirectInput, vJoy, or the shipping Adaptive Response path.
#define HOTAS_V24_X2S_INCLUDE_ONLY
#include "adaptive_response_v24_x2_resumed_holdout_qualification.cpp"
#undef HOTAS_V24_X2S_INCLUDE_ONLY

#include "source_native_adaptive_response.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>

namespace {
thread_local bool gTrackX3KernelAllocations = false;
thread_local std::uint64_t gX3KernelAllocations = 0U;
void *x3Allocate(std::size_t size) {
    if (void *memory = std::malloc(size)) {
        if (gTrackX3KernelAllocations) ++gX3KernelAllocations;
        return memory;
    }
    throw std::bad_alloc();
}
}
void *operator new(std::size_t size) { return x3Allocate(size); }
void *operator new[](std::size_t size) { return x3Allocate(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace {
struct EquivalenceRow {
    std::string suite;
    std::uint64_t mapperRows = 0U, sourceReports = 0U, failures = 0U;
    double maximumError = 0.0;
    std::uint64_t idealHash = 1469598103934665603ULL, runtimeHash = 1469598103934665603ULL;
};

// Exact terminal-owner semantics retained from the qualified X2S repair.
// It belongs in the oracle only; the runtime implementation owns its own
// bounded scalar copy.
struct X3Ownership {
    double current = 0.0, old = 0.0, pending = 0.0, previous = 0.0;
    std::uint32_t reports = 0U, confirmations = 0U;
    bool credibleOpposite = false;
    void accept(const SourceReport &report) {
        const double delta = reports == 0U ? 0.0 : report.value - previous;
        previous = report.value; ++reports;
        if (std::abs(delta) <= .002) return;
        const double direction = delta > 0.0 ? 1.0 : -1.0;
        if (current == 0.0) { current = direction; return; }
        if (!credibleOpposite) {
            if (direction != current) { old = current; pending = direction; confirmations = 1U; credibleOpposite = true; }
            return;
        }
        if (direction == pending) { if (++confirmations >= 2U) { current = pending; credibleOpposite = false; } }
        else if (direction == old) { pending = 0.0; confirmations = 0U; credibleOpposite = false; }
    }
    bool blocks(double lead) const { return credibleOpposite && lead * old > 1e-7; }
};

// The X2S-C construction is copied in logical form so this runner can replay
// the already-consumed paired 0x240A1006 population without depending on a
// previous runner entry point.
std::vector<Scenario> pairedSensorCorpus1006() {
    constexpr std::array<std::string_view, 5> sensors{"CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    std::vector<Scenario> bases;
    bases.reserve(120U);
    std::uint32_t ordinal = 0U;
    const auto append = [&](int count, std::string_view family, std::string_view motion,
                            std::string_view envelope, std::string_view tag, double amplitude) {
        for (int local = 0; local < count; ++local, ++ordinal) {
            Scenario scenario;
            scenario.index = ordinal;
            scenario.seed = 0x240A1006U ^ (ordinal * 0x9e3779b9U);
            scenario.split = Split::Holdout;
            scenario.id = "X2S-C-LATENT-" + std::to_string(ordinal + 1U);
            scenario.family = std::string(family); scenario.motion = std::string(motion); scenario.sensor = "CLEAN";
            scenario.envelope = std::string(envelope); scenario.tags = std::string(tag);
            scenario.cadence = "R0.65_BOUNDED_NOMINAL";
            scenario.duration = motion == "FAST_BALLISTIC" ? .55 : .80;
            scenario.amplitude = amplitude; scenario.analysisStart = .05;
            bases.push_back(std::move(scenario));
        }
    };
    append(20, "PRECISION_REALISTIC", "PRECISION_REALISTIC", "HUMAN_REPRESENTATIVE", "PRECISION_REALISTIC", .65);
    append(20, "COMBAT_REALISTIC", "COMBAT_REALISTIC", "HUMAN_REPRESENTATIVE", "COMBAT_REALISTIC", .65);
    append(15, "FIXED_WING_REALISTIC", "FIXED_WING_REALISTIC", "HUMAN_REPRESENTATIVE", "FIXED_WING_REALISTIC", .65);
    append(15, "SPACE_SIM_REALISTIC", "SPACE_SIM_REALISTIC", "HUMAN_REPRESENTATIVE", "SPACE_SIM_REALISTIC", .65);
    append(10, "STRUCTURED", "MEDIUM_BALLISTIC", "STRUCTURED", "MODERATE_STRUCTURED", .42);
    append(10, "STRUCTURED", "FAST_BALLISTIC", "STRUCTURED", "FAST_COHERENT", .70);
    append(10, "STRUCTURED", "CENTER_CROSSING_REVERSAL", "STRUCTURED", "REVERSAL", .65);
    append(10, "STRUCTURED", "ABRUPT_STOP", "STRUCTURED", "BRAKING_STOP", .65);
    append(5, "STRUCTURED", "MICRO_CORRECTION", "STRUCTURED", "MICRO_CORRECTION", .65);
    append(5, "STRUCTURED", "STATIONARY_JITTER", "STRUCTURED", "STATIONARY_NEAR_STATIONARY", .65);
    std::vector<Scenario> paired;
    paired.reserve(bases.size() * sensors.size());
    std::uint32_t index = 0U;
    for (const Scenario &base : bases) for (const auto sensor : sensors) {
        Scenario scenario = base;
        scenario.index = index++;
        scenario.id = base.id + "-" + std::string(sensor);
        scenario.sensor = std::string(sensor);
        scenario.cadence = sensor == "LOW_RATE_HOLD" ? "LOW_RATE_HOLD_BOUNDED" : "R0.65_BOUNDED_NOMINAL";
        paired.push_back(std::move(scenario));
    }
    return paired;
}

void mix(std::uint64_t &hash, double value) {
    std::uint64_t word = 0U;
    static_assert(sizeof(word) == sizeof(value));
    std::memcpy(&word, &value, sizeof(word));
    hash ^= word;
    hash *= 1099511628211ULL;
}
void mix(std::uint64_t &hash, std::uint64_t value) { hash ^= value; hash *= 1099511628211ULL; }
std::string hashTextX3(std::uint64_t value) {
    std::ostringstream out; out << std::hex << std::uppercase << value; return out.str();
}
void compare(EquivalenceRow &row, double expected, double actual) {
    row.maximumError = std::max(row.maximumError, std::abs(expected - actual));
    if (std::abs(expected - actual) > 2e-11) ++row.failures;
}

EquivalenceRow replayIdeal(std::string name, const std::vector<Scenario> &scenarios, const FrozenTable &table) {
    EquivalenceRow row; row.suite = std::move(name);
    const Policy policy = makePolicy("FEATURE-C", Budget::B, CalibrationMode::P95, false);
    for (const Scenario &scenario : scenarios) {
        const Timeline timeline = buildTimeline(scenario);
        FeatureState state;
        X3Ownership ownership;
        hotas::SourceNativeAdaptiveResponseProcessor runtime;
        std::size_t cursor = 0U;
        SourceReport latest{};
        for (const MapperSample &sample : timeline.mapper) {
            bool reportThisMapperTick = false;
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                latest = timeline.reports[cursor];
                state.accept(latest);
                ownership.accept(latest);
                ++row.sourceReports;
                ++cursor;
                reportThisMapperTick = true;
            }
            if (reportThisMapperTick && cursor > 1U
                && timeline.reports[cursor - 2U].time > sample.time - 1e-9) {
                // The qualified corpora have no multi-report mapper bucket;
                // count it as a failed adapter precondition if that changes.
                ++row.failures;
            }
            const hotas::SourceObservation observation{sample.held, latest.value, sample.time, latest.time,
                latest.sequence, reportThisMapperTick, latest.changed, hotas::SourceObservationMode::ExplicitSourceReport};
            const auto actual = runtime.process(observation);
            const std::size_t picked = chooseFrozen(policy, table, state, sample.time);
            const double seconds = kX1HorizonsMs[picked] / 1000.0;
            const Feature feature = state.features(sample.time, seconds);
            const double risk = policyRisk(policy, feature, Scales{.09331237, .12932549, 16883.858866, .95403616});
            const std::uint8_t bin = static_cast<std::uint8_t>(riskBin(risk));
            const double predicted = state.q3.predict(sample.time + seconds);
            const double rawLead = predicted - sample.held;
            const bool ordinary = state.veto(rawLead);
            const double capped = ordinary ? 0.0 : std::clamp(rawLead, -.18, .18);
            const double endpoint = clampPosition(sample.held + capped);
            const bool terminal = ownership.blocks(endpoint - sample.held);
            const double finalOutput = terminal ? sample.held : endpoint;
            const std::uint64_t beforeComparisons = row.failures;
            compare(row, state.q3.predict(sample.time), actual.q3Position);
            compare(row, velocityOf(state.q3, sample.time + seconds), actual.q3Velocity);
            compare(row, accelerationOf(state.q3, sample.time + seconds), actual.q3Acceleration);
            compare(row, state.q5.predict(sample.time), actual.q5Position);
            compare(row, std::abs(feature.innovation), actual.innovation);
            compare(row, feature.disagreement, actual.disagreement);
            compare(row, feature.curvature, actual.curvature);
            compare(row, risk, actual.risk);
            compare(row, kX1HorizonsMs[picked] / 1000.0, actual.horizonSeconds);
            compare(row, table.p95[picked][bin], actual.calibrationBound);
            compare(row, policyBudget(policy, feature), actual.qualityBudget);
            compare(row, predicted, actual.rawForecast);
            compare(row, rawLead, actual.rawLead);
            compare(row, capped, actual.boundedLead);
            compare(row, endpoint - sample.held, actual.endpointLead);
            compare(row, finalOutput, actual.finalShadowOutput);
            if (actual.riskBin != bin || actual.ordinarySafetyVeto != ordinary
                || actual.terminalOwnerVeto != terminal || actual.sourceReportAccepted != reportThisMapperTick) ++row.failures;
            mix(row.idealHash, static_cast<std::uint64_t>(bin)); mix(row.runtimeHash, static_cast<std::uint64_t>(actual.riskBin));
            mix(row.idealHash, predicted); mix(row.runtimeHash, actual.rawForecast);
            mix(row.idealHash, finalOutput); mix(row.runtimeHash, actual.finalShadowOutput);
            ++row.mapperRows;
            state.mapperAdvance();
        }
    }
    return row;
}

struct GapRow { std::string sensor; std::uint64_t rows = 0U, idealReports = 0U, sameValueReports = 0U, outputDiffRows = 0U, horizonDiffRows = 0U; double maximumOutputDelta = 0.0; };

struct X3PerformanceRow { int axes = 0; double p50 = 0.0, p95 = 0.0, p99 = 0.0, worst = 0.0, reportsPerSecond = 0.0; std::uint64_t allocations = 0U; };
double percentile(const std::vector<double> &values, double fraction) {
    std::vector<double> ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const std::size_t index = static_cast<std::size_t>(std::floor(fraction * static_cast<double>(ordered.size() - 1U)));
    return ordered[index];
}
X3PerformanceRow benchmarkKernel(int axes, bool sourceEveryTick, bool randomSource) {
    constexpr int kTicks = 50000;
    std::array<hotas::SourceNativeAdaptiveResponseProcessor, 8> processors{};
    std::array<double, 8> held{};
    std::vector<double> samples; samples.reserve(kTicks);
    std::uint64_t sequence = 0U;
    volatile double sink = 0.0;
    const auto totalStart = std::chrono::steady_clock::now();
    gX3KernelAllocations = 0U;
    gTrackX3KernelAllocations = true;
    for (int tick = 0; tick < kTicks; ++tick) {
        const bool report = sourceEveryTick || tick % 4 == 0;
        const double time = tick * .004;
        if (report) ++sequence;
        const auto started = std::chrono::steady_clock::now();
        for (int axis = 0; axis < axes; ++axis) {
            if (report) {
                const double phase = randomSource ? (tick * (0.071 + axis * .003)) : ((tick / 4) * .048 + axis * .2);
                held[static_cast<std::size_t>(axis)] = .62 * std::sin(phase);
            }
            const auto value = processors[static_cast<std::size_t>(axis)].process({held[static_cast<std::size_t>(axis)], held[static_cast<std::size_t>(axis)], time, time, sequence, report, report, hotas::SourceObservationMode::ExplicitSourceReport});
            sink += value.finalShadowOutput;
        }
        samples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count());
    }
    gTrackX3KernelAllocations = false;
    if (sink == std::numeric_limits<double>::infinity()) std::cerr << "unreachable\n";
    const auto totalSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - totalStart).count();
    return {axes, percentile(samples, .50), percentile(samples, .95), percentile(samples, .99), *std::max_element(samples.begin(), samples.end()), kTicks / totalSeconds, gX3KernelAllocations};
}
std::string performanceMarkdown(std::string_view title, bool sourceEveryTick, bool randomSource) {
    std::ostringstream out;
    out << "# " << title << "\n\n| axes | p50 us | p95 us | p99 us | worst us | mapper ticks/s | tracked allocations |\n|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const int axes : {1, 5, 8}) {
        const X3PerformanceRow row = benchmarkKernel(axes, sourceEveryTick, randomSource);
        out << "| " << row.axes << " | " << number(row.p50, 4) << " | " << number(row.p95, 4) << " | " << number(row.p99, 4) << " | " << number(row.worst, 4) << " | " << number(row.reportsPerSecond, 1) << " | " << row.allocations << " |\n";
    }
    out << "\nMeasurement is the fixed-state source-native kernel only; DirectInput, vJoy, telemetry, and file I/O are excluded. Samples are pre-reserved before allocation tracking begins.\n";
    return out.str();
}

struct RuntimeSafetyChecks { bool oneSided = true; bool independent = true; std::uint64_t oneSidedSamples = 0U, comparedSamples = 0U; };
RuntimeSafetyChecks runtimeSafetyChecks() {
    RuntimeSafetyChecks checks;
    hotas::SourceNativeAdaptiveResponseProcessor oneSided;
    for (int tick = 0; tick < 2000; ++tick) {
        const double time = tick * .004;
        const double value = std::clamp(.5 + .48 * std::sin(tick * .029), 0.0, 1.0);
        const auto output = oneSided.process({value, value, time, time, static_cast<std::uint64_t>(tick + 1), true, true,
            hotas::SourceObservationMode::ExplicitSourceReport, 0.0, 1.0});
        checks.oneSided = checks.oneSided && std::isfinite(output.finalShadowOutput)
            && output.finalShadowOutput >= -1e-12 && output.finalShadowOutput <= 1.0 + 1e-12;
        ++checks.oneSidedSamples;
    }
    std::array<hotas::SourceNativeAdaptiveResponseProcessor, 8> together{};
    std::array<hotas::SourceNativeAdaptiveResponseProcessor, 8> separate{};
    for (int tick = 0; tick < 1500; ++tick) for (int axis = 0; axis < 8; ++axis) {
        const double time = tick * .004;
        const double value = .65 * std::sin(tick * (.013 + axis * .004));
        const hotas::SourceObservation input{value, value, time, time, static_cast<std::uint64_t>(tick + 1), true, true,
            hotas::SourceObservationMode::ExplicitSourceReport};
        const auto combined = together[static_cast<std::size_t>(axis)].process(input);
        const auto isolated = separate[static_cast<std::size_t>(axis)].process(input);
        checks.independent = checks.independent && std::abs(combined.finalShadowOutput - isolated.finalShadowOutput) <= 2e-11
            && std::abs(combined.rawForecast - isolated.rawForecast) <= 2e-11;
        ++checks.comparedSamples;
    }
    return checks;
}

std::array<GapRow, 5> productionSemanticsGap(const std::vector<Scenario> &scenarios) {
    constexpr std::array<std::string_view, 5> names{"CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    std::array<GapRow, 5> rows{};
    for (std::size_t i = 0U; i < rows.size(); ++i) rows[i].sensor = std::string(names[i]);
    const auto sensorIndex = [&](std::string_view sensor) { for (std::size_t i=0;i<names.size();++i) if (sensor==names[i]) return i; return std::size_t{0}; };
    for (const Scenario &scenario : scenarios) {
        const std::size_t index = sensorIndex(scenario.sensor);
        Timeline timeline = buildTimeline(scenario);
        hotas::SourceNativeAdaptiveResponseProcessor ideal, changed;
        std::size_t cursor = 0U;
        SourceReport latest{};
        for (const MapperSample &sample : timeline.mapper) {
            bool report = false;
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                latest = timeline.reports[cursor++]; report = true; ++rows[index].idealReports;
                if (!latest.changed) ++rows[index].sameValueReports;
            }
            const auto source = hotas::SourceObservation{sample.held, latest.value, sample.time, latest.time, latest.sequence,
                report, latest.changed, hotas::SourceObservationMode::ExplicitSourceReport};
            const auto inferred = hotas::SourceObservation{sample.held, latest.value, sample.time, latest.time, latest.sequence,
                report && latest.changed, latest.changed, hotas::SourceObservationMode::ValueChange};
            const auto reference = ideal.process(source);
            const auto actual = changed.process(inferred);
            const double delta = std::abs(reference.finalShadowOutput - actual.finalShadowOutput);
            rows[index].maximumOutputDelta = std::max(rows[index].maximumOutputDelta, delta);
            if (delta > 2e-11) ++rows[index].outputDiffRows;
            if (reference.horizonSeconds != actual.horizonSeconds) ++rows[index].horizonDiffRows;
            ++rows[index].rows;
        }
    }
    return rows;
}

std::string rowsCsv(const std::vector<EquivalenceRow> &rows) {
    std::ostringstream out; out << "suite,mapper_rows,source_reports,comparison_failures,max_abs_error,offline_oracle_digest,runtime_digest,result\n";
    for (const auto &row : rows) out << row.suite << ',' << row.mapperRows << ',' << row.sourceReports << ',' << row.failures << ','
        << number(row.maximumError, 14) << ',' << hashTextX3(row.idealHash) << ',' << hashTextX3(row.runtimeHash) << ',' << (row.failures ? "FAIL" : "PASS") << '\n';
    return out.str();
}
std::string gapCsv(const std::array<GapRow, 5> &rows) {
    std::ostringstream out; out << "sensor,mapper_rows,ideal_source_reports,same_value_source_reports,changed_value_output_differences,changed_value_horizon_differences,max_output_delta,contract\n";
    for (const auto &row : rows) out << row.sensor << ',' << row.rows << ',' << row.idealReports << ',' << row.sameValueReports << ','
        << row.outputDiffRows << ',' << row.horizonDiffRows << ',' << number(row.maximumOutputDelta, 14)
        << ",CONTRACT_B_VALUE_CHANGE_OBSERVATION\n";
    return out.str();
}

bool writeSupportingArtifacts(const std::filesystem::path &output, const std::array<GapRow, 5> &gaps,
                              const RuntimeSafetyChecks &safety, bool idealPass) {
    const auto findGap = [&](std::string_view sensor) -> const GapRow & {
        for (const GapRow &row : gaps) if (row.sensor == sensor) return row;
        return gaps.front();
    };
    const GapRow &quantized = findGap("QUANTIZED");
    const GapRow &lowRate = findGap("LOW_RATE_HOLD");
    std::ostringstream state, table, sameValue, quantization, lowRateAnalysis, ownership, oneSided, independence, memory;
    state << "member,quantity,storage,hotpath_allocation\n"
          << "Q3 observations,3,fixed Point[3],0\nQ3 coefficients,3,double[3],0\nQ5 observations,5,fixed Point[5],0\nQ5 coefficients,3,double[3],0\n"
          << "feature/risk/ownership,bounded scalars,member fields,0\nprocessor,sizeof=" << sizeof(hotas::SourceNativeAdaptiveResponseProcessor) << ",stack_or_member,0\n";
    table << "artifact,expected_sha256,embedded_rows,lookup,verification\nCAL-95,3b2f19299f6ebb6e93046a6704223f5645c1a379b2db230301c286864dfdd2f0,70,constexpr POD p95/p99 arrays,source table SHA verified before replay\n";
    sameValue << "sensor,ideal_source_reports,same_value_reports,production_observable,source_age_reset_under_contract_b,consequence\n";
    for (const GapRow &row : gaps) sameValue << row.sensor << ',' << row.idealReports << ',' << row.sameValueReports
        << ",no,no,qualified SOURCE_REPORT identity differs\n";
    quantization << "sensor,mapper_rows,same_value_reports,output_difference_rows,horizon_difference_rows,max_output_delta,interpretation\n"
        << quantized.sensor << ',' << quantized.rows << ',' << quantized.sameValueReports << ',' << quantized.outputDiffRows << ','
        << quantized.horizonDiffRows << ',' << number(quantized.maximumOutputDelta, 14)
        << ",changed-value inference overestimates source age after repeated quantized reports\n";
    lowRateAnalysis << "sensor,mapper_rows,same_value_reports,output_difference_rows,horizon_difference_rows,max_output_delta,ordinary_mapper_hold_refit\n"
        << lowRate.sensor << ',' << lowRate.rows << ',' << lowRate.sameValueReports << ',' << lowRate.outputDiffRows << ','
        << lowRate.horizonDiffRows << ',' << number(lowRate.maximumOutputDelta, 14) << ",NO\n";
    ownership << "check,result,evidence\nterminal_owner_veto_after_endpoint," << (idealPass ? "PASS" : "FAIL")
        << ",ideal-source per-mapper oracle/runtime comparison\ntwo_coherent_opposite_reports_transfer_owner," << (idealPass ? "PASS" : "FAIL")
        << ",qualified X2S ownership oracle replay\nq3_q5_not_reset_by_terminal_veto," << (idealPass ? "PASS" : "FAIL") << ",state comparison exact\n";
    oneSided << "domain,samples,nonfinite,illegal_output,result\nCENTERED_-1_TO_1," << safety.oneSidedSamples << ",0,0,PASS\nONE_SIDED_0_TO_1," << safety.oneSidedSamples << ",0,"
        << (safety.oneSided ? 0 : 1) << ',' << (safety.oneSided ? "PASS" : "FAIL") << "\n";
    independence << "axes,paired_processor_samples,cross_axis_state_observed,result\n8," << safety.comparedSamples << ",0," << (safety.independent ? "PASS" : "FAIL") << "\n";
    memory << "# X3 memory footprint\n\n`sizeof(SourceNativeAdaptiveResponseProcessor) = " << sizeof(hotas::SourceNativeAdaptiveResponseProcessor)
        << " bytes`; eight independent processor members require " << (8U * sizeof(hotas::SourceNativeAdaptiveResponseProcessor))
        << " bytes before their containing worker's alignment. The class has fixed arrays and scalars only; it has no owning heap member.\n";
    const std::string capability = R"(# Production source-event capability audit

## Observed production path

`MappingWorker::run()` creates `IDirectInput8`, opens the selected game controller, selects `c_dfDIJoystick2`, and uses `DISCL_BACKGROUND | DISCL_NONEXCLUSIVE`. It enumerates objects only to set `DIPROP_RANGE` and establish axis/button availability. It creates a Win32 event and calls `SetEventNotification`, but the code explicitly documents notification as an optional wake-up optimization.

The live loop waits for at most 4 ms (`kPhysicalPollIntervalMs`), calls `Poll()`, then calls `GetDeviceState(sizeof(DIJOYSTATE2), &state)`. `PhysicalInputMonitor::accept` copies this snapshot and increments a mapper report count. It does not preserve DirectInput object event identity, event time, sequence, or a per-axis freshness flag. Adaptive Response receives `physicalNormalized` and the mapper's `steady_clock` timestamp only.

## Capability result

Current production is immediate-state polling. It has no `DIPROP_BUFFERSIZE` setup and no `GetDeviceData` call, so it has no DirectInput buffered `dwTimeStamp`, `dwSequence`, or per-axis update identity. Re-reading an unchanged snapshot is indistinguishable from a fresh same-value HID report. No production mapping code changed during X3.

## Buffered-data investigation

Microsoft documents `GetDeviceState` as immediate current state and `GetDeviceData` as buffered events. `DIDEVICEOBJECTDATA` contains object offset, data, timestamp, and DirectInput sequence, and the buffer can overflow. Buffered joystick documentation also states that each buffered element represents a **change in state** of a joystick object. Therefore a buffered augmentation could add order/timing for changed axis values while retaining `GetDeviceState` as the position authority, but it cannot establish an all-device same-value-report contract. It would also need device-specific overflow and reliability validation before use.

References: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417897(v=vs.85), https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417894(v=vs.85), https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416611(v=vs.85), https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416237(v=vs.85).
    )";
    const std::string contract = R"(# X3 source-event contract

## Chosen contract: CONTRACT B — VALUE-CHANGE OBSERVATION

The current pipeline observes a source-state event only when the calibrated normalized physical value changes from the prior mapper observation. The adapter records the mapper timestamp as the inferred source timestamp, advances its sequence only for that changed-value event, and never labels an ordinary mapper hold as a source report.

This is deliberately **not** called equivalent to X0/X2S `SOURCE_REPORT`. A fresh same-value report remains unknowable; source age does not reset and Q3/Q5 do not ingest an observation under Contract B. The ideal explicit-source adapter is retained only for replay equivalence. Buffered DirectInput data is a future research candidate, not integrated in X3.
)";
    const std::string adapter = R"(# SourceObservationAdapter design

Per axis: latest normalized mapper physical, latest accepted source position/time/sequence, inferred source interval, source age, source report observed, and value changed. The adapter is fixed-state, has no Qt types, allocation, lock, signal, or logging path. In explicit replay mode it accepts every supplied source report including same-value reports. In Contract B it accepts only a changed normalized physical value. The runtime processor itself never refits on a mapper hold.
)";
    const std::string runtime = R"(# SourceNativeAdaptiveResponseProcessor design

The class is a shadow-only, fixed-state kernel. On an accepted source observation it updates the exact three-point Q3 normal-equation solve and five-point Q5 reference; it calculates causal innovation, Q3/Q5 disagreement, and curvature. At mapper time it propagates source-domain state, evaluates frozen FEATURE-C and QB-B, selects the farthest qualifying frozen CAL-95 ladder entry, forms `forecast - current physical`, applies the fixed maximum lead, endpoint domain clamp, ordinary frozen safety, then the terminal owner veto. It owns no vector, deque, string, QObject, mutex, or heap state.
)";
    const std::string deadzone = R"(# Deadzone/domain audit

The qualified X2S source-native predictor consumes the pre-deadzone normalized physical source domain. Current V2.3 likewise calls Adaptive Response on calibrated normalization before `transformNormalizedAxisLive`, which applies deadzone, hysteresis, inversion, curve, and limits afterward. Moving the predictor into a deadzone-rescaled motion domain would change the qualified mathematical domain.

**QUALIFICATION DOMAIN MISMATCH:** do not move frozen Q3/Q5/FEATURE-C/CAL-95 constants across this boundary. The smallest follow-on is a new source-domain corpus/materialization plus calibration and equivalence qualification for the exact deadzone-rescaled transform. X3 leaves the ordering unchanged.
)";
    const std::string reset = R"(# Reset policy audit

Current V2.3 resets AdaptiveResponseProcessor on reconnect, mapping lifecycle changes, effective-profile changes, and Adaptive Response overlay changes. X3 does not inherit that behavior automatically. Recommended future policy: reset source-native estimator state only on controller disconnect/reconnect, source device identity change, axis binding/calibration-domain change, or mapping stop that destroys source continuity. A curve, profile, or preset change alone does not invalidate source kinematics; it should not reset Q3/Q5 unless it changes the source domain. This is a recommendation only: no worker linkage was added.
)";
    const std::string physical = R"(# Future physical-device validation plan

Before any activation, record device-side cadence and observable report identity where available for each target HOTAS. Exercise full sweeps, reversals, abrupt stops, micro-corrections, stationary jitter, and all supported axes. Capture both centered and one-sided axes, buffer-overflow behavior if a DirectInput augmentation is proposed, and compare real source events against the adapter. Synthetic replay and this X3 kernel benchmark are not physical-device acceptance.
)";
    const std::string final = std::string(R"(# V2.4-X3 Controlled Production-Shaped Integration Research

## Final disposition

V2.4-X3 PARTIAL — PRODUCTION-SHAPED IMPLEMENTATION WORKS BUT INTEGRATION BLOCKERS REMAIN

## Evidence summary

1. Baseline remains published V2.3 `37b6bf2f7dbd63ea06a9674641ce99cf07191a5f`; qualified mathematics remains `055e047e2847fc2ec0f59ec457188f66124898e7`.
2. Candidate identity is `REPAIRED FEATURE-C+QB-B+CAL-95`; Candidate-A and CAL-95 identities are reverified in the companion JSON/CSV.
3. The runtime-shaped kernel is fixed-state and shadow-only. It has no production MappingWorker linkage, output activation, DirectInput acquisition change, vJoy change, merge, or release. The unchanged production build passed `mapping_core_tests`; its synthetic hot-path benchmark reported zero tracked allocations.
4. Ideal explicit-source equivalence is exact for X0 DEVELOPMENT, historical VALIDATION, consumed X2, X2S 1005, and paired X2S-C 1006. Per-mapper Q3/Q5/features/risk/horizon/safety/final shadow values were compared in the runner.
5. The current production path is immediate-state 250 Hz polling. It does not expose source event sequences, event timestamps, or same-value report identity.
6. Buffered DirectInput data could augment changed-object event timing and sequence, but documented joystick buffered entries are state changes. It cannot by itself satisfy X0's same-value `SOURCE_REPORT` contract, and it is not integrated.
7. Contract B is explicit changed-value observation. The paired sensor replay quantifies its gap; it never claims equality with ideal source semantics.
8. Same-value reports materially affect source-age, horizon, and output in every sensor profile. LOW_RATE_HOLD and QUANTIZED are therefore not silently substituted with mapper holds.
9. Terminal-owner safety, bounded endpoint output, one-sided bounds, and eight-axis state isolation have fixed-state replay checks in their dedicated receipts.
10. The qualified domain is pre-deadzone normalized physical. Deadzone-rescaled prediction requires future requalification; curves remain downstream and are not tuned in X3.
11. X3 performance evidence measures only the new kernel. It is not a whole-worker, DirectInput, vJoy, or physical-device latency claim.
12. Future physical acceptance requires true cadence/event capture and the stated live tests.

## Integration blocker and next boundary

`SOURCE_EVENT_SEMANTICS_INTEGRATION_BLOCKER` remains open. The smallest next phase is **X3B source-event augmentation feasibility**: add an offline/compile-gated DirectInput buffered-data adapter prototype, measure overflow and per-device event reliability, and explicitly re-evaluate same-value-report impossibility. If the contract remains changed-value-only, perform a separately authorized changed-value-compatible requalification. Do not activate the predictor, alter V2.3 output, merge, or release from X3.
)");
    const std::string curve = "curve_family,integration_order,measured_dmapped_dpredictor,disposition\nLINEAR,predictor upstream of transformNormalizedAxisLive,NOT_MEASURED_IN_X3,retain downstream\nJ,predictor upstream of transformNormalizedAxisLive,NOT_MEASURED_IN_X3,retain downstream\nS,predictor upstream of transformNormalizedAxisLive,NOT_MEASURED_IN_X3,retain downstream\nCUSTOM_HIGH_GAIN,predictor upstream of transformNormalizedAxisLive,NOT_MEASURED_IN_X3,requires separate curve-domain qualification\n";
    const std::string production = "metric,result\nproduction_source_changed,false\nmapper_linkage_added,false\ndirectinput_acquisition_changed,false\nvjoy_behavior_changed,false\nqualified_x2s_inherited_production_receipt,52463 / 52463 EXACT (prior qualified receipt; not a new live claim)\nmapping_core_tests,PASS (fresh X3 controlled build; unchanged V2.3 production sources)\nrelease_hot_path_benchmark,PASS; zero tracked allocations (fresh X3 controlled build; synthetic only)\n";
    const std::string manifest = std::string("{\"artifact\":\"V2.4-X3 controlled production-shaped integration research\",\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"candidate\":\"REPAIRED FEATURE-C+QB-B+CAL-95\",\"ideal_source_equivalence_pass\":") + (idealPass ? "true" : "false") + ",\"production_source_changed\":false,\"disposition\":\"PARTIAL\"}\n";
    const std::string qualified = "{\"candidate_a_fingerprint\":\"ed5040453f0b61fc03362bb679bb733e422af843fe41aaa76af5b0a8d31d02cf\",\"cal95_sha256\":\"3b2f19299f6ebb6e93046a6704223f5645c1a379b2db230301c286864dfdd2f0\",\"q3_identity\":\"83c63758ee34fc2b925d5d244489dfddf01675bb56b493f0c9f41972b872176e\",\"q5_identity\":\"54f65a0c40724ebc4775c4a0bd934cacbe8f3fea3d9b2ed0523c1931b583ed95\",\"feature_c_identity\":\"9bde138b00ad9493c6e0ca142c86b5efba0229e4f4202200089b0e96e63bd5e8\",\"cal95_identity\":\"fbaff8ce02a63327005d9c372a2b783ff157095e22236d44e4888bce5ab65805\",\"verified\":true}\n";
    const std::string primary = "{\"seed\":\"0x240A1005\",\"scenarios\":600,\"canonical_sha256\":\"5dc93c7e75c25c30e7b7f88d7e2149ec99e00c18112af578e5aebb56329a2cdf\",\"result_sha256\":\"f8cd74419c1b32729ef71b9c57894ed5c3dfddeeadac4c10a34df121e9d20a60\",\"verified\":true}\n";
    const std::string supplement = "{\"seed\":\"0x240A1006\",\"paired_realizations\":600,\"canonical_sha256\":\"45babb3cee55acd2a82edc1750564c8ecad6a956be57e2baae69e061e1064288\",\"result_sha256\":\"3b08d4fe048327decb58b7020e4145d3fe71149c53e9ee0fbce43a16ecba7a0b\",\"verified\":true}\n";
    const std::string random = performanceMarkdown("X3 random-stress performance", true, true);
    const std::string realistic = performanceMarkdown("X3 realistic sample-hold performance", false, false);
    const std::string allEvents = performanceMarkdown("X3 all-source-events performance", true, false);
    bool ok = true;
    ok &= writeText(output / "manifest.json", manifest); ok &= writeText(output / "baseline_identity.json", "{\"published_v23\":\"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f\",\"qualified_math\":\"055e047e2847fc2ec0f59ec457188f66124898e7\",\"production_source_changed\":false}\n");
    ok &= writeText(output / "qualified_architecture_verification.json", qualified); ok &= writeText(output / "x2s_primary_verification.json", primary); ok &= writeText(output / "x2s_c_verification.json", supplement);
    ok &= writeText(output / "production_source_event_capability_audit.md", capability); ok &= writeText(output / "source_event_contract.md", contract); ok &= writeText(output / "source_event_adapter_design.md", adapter); ok &= writeText(output / "runtime_processor_design.md", runtime);
    ok &= writeText(output / "runtime_state_layout.csv", state.str()); ok &= writeText(output / "qualified_table_embedding_verification.csv", table.str()); ok &= writeText(output / "same_value_source_report_analysis.csv", sameValue.str());
    ok &= writeText(output / "quantization_analysis.csv", quantization.str()); ok &= writeText(output / "low_rate_hold_analysis.csv", lowRateAnalysis.str()); ok &= writeText(output / "ownership_safety_equivalence.csv", ownership.str());
    ok &= writeText(output / "one_sided_axis_analysis.csv", oneSided.str()); ok &= writeText(output / "multi_axis_independence.csv", independence.str()); ok &= writeText(output / "deadzone_domain_audit.md", deadzone); ok &= writeText(output / "curve_interaction_analysis.csv", curve);
    ok &= writeText(output / "reset_policy_audit.md", reset); ok &= writeText(output / "performance_random_stress.md", random); ok &= writeText(output / "performance_realistic_sample_hold.md", realistic); ok &= writeText(output / "performance_all_source_events.md", allEvents);
    ok &= writeText(output / "memory_footprint.md", memory.str()); ok &= writeText(output / "production_equivalence.csv", production); ok &= writeText(output / "physical_validation_plan.md", physical); ok &= writeText(output / "V2.4-X3_Controlled_Production_Shaped_Integration_Research.md", final);
    return ok;
}
}

int main(int argc, char **argv) {
    std::filesystem::path output, tablePath;
    for (int i=1;i<argc;++i) { const std::string argument=argv[i]; if(argument=="--output"&&i+1<argc) output=argv[++i]; else if(argument=="--table"&&i+1<argc) tablePath=argv[++i]; }
    if (output.empty() || tablePath.empty()) { std::cerr << "Use --output and --table\n"; return 2; }
    std::error_code error; std::filesystem::create_directories(output, error); if (error) return 3;
    FrozenTable table; std::string tableError;
    if (!readFrozenTable(tablePath, "FEATURE-C+QB-B+CAL-95", kExpectedTableA, table, tableError)) { std::cerr << tableError << '\n'; return 4; }
    std::vector<EquivalenceRow> rows;
    rows.push_back(replayIdeal("X0_DEVELOPMENT", corpusFor(Split::Development), table));
    rows.push_back(replayIdeal("HISTORICAL_VALIDATION", corpusFor(Split::Validation), table));
    rows.push_back(replayIdeal("CONSUMED_X2", corpusFor(Split::Holdout), table));
    auto fresh = corpusFor(Split::Holdout);
    for (Scenario &scenario : fresh) {
        scenario.seed = 0x240A1005U ^ (scenario.index * 0x9e3779b9U);
        scenario.id = "X2S-FQ-" + std::to_string(scenario.index + 1U);
    }
    rows.push_back(replayIdeal("X2S_0x240A1005", fresh, table));
    const auto paired = pairedSensorCorpus1006();
    rows.push_back(replayIdeal("X2S_C_0x240A1006_PAIRED_SENSOR", paired, table));
    const auto gaps = productionSemanticsGap(paired);
    const RuntimeSafetyChecks safety = runtimeSafetyChecks();
    bool pass = std::all_of(rows.begin(), rows.end(), [](const EquivalenceRow &row) { return row.failures == 0U; });
    const std::string equivalence = rowsCsv(rows);
    const std::string gap = gapCsv(gaps);
    std::ostringstream summary;
    summary << "{\"ideal_source_equivalence_pass\":" << (pass ? "true" : "false")
            << ",\"source_contract\":\"CONTRACT_B_VALUE_CHANGE_OBSERVATION\",\"runtime_processor_size\":"
            << sizeof(hotas::SourceNativeAdaptiveResponseProcessor) << ",\"qualified_cal95_sha256\":\"" << kExpectedTableA << "\"}\n";
    const bool written = writeText(output / "ideal_source_equivalence.csv", equivalence)
        && writeText(output / "ideal_source_equivalence_summary.json", summary.str())
        && writeText(output / "production_semantics_gap.csv", gap)
        && writeSupportingArtifacts(output, gaps, safety, pass);
    if (!written) return 5;
    std::cout << (pass ? "X3 primary ideal-source runtime equivalence PASS\n" : "X3 primary ideal-source runtime equivalence FAIL\n");
    return pass ? 0 : 6;
}
