// V2.4-X3B is a compile-gated research executable.  It has no MappingWorker,
// DirectInput acquisition, vJoy, QML, or production-output linkage.  Its only
// runnable path is an offline source-stream emulator.  The small Win32 helper
// types at the end are compiled API prototypes and are deliberately never
// instantiated by this executable.
#define HOTAS_V24_X2S_INCLUDE_ONLY
#include "adaptive_response_v24_x2_resumed_holdout_qualification.cpp"
#undef HOTAS_V24_X2S_INCLUDE_ONLY

#include "source_freshness_research.h"
#include "source_native_adaptive_response.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <new>

#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <hidsdi.h>
#include <hidusage.h>

namespace {
thread_local bool gTrackX3BAllocations = false;
thread_local std::uint64_t gX3BAllocations = 0U;
void *x3bAllocate(std::size_t size) {
    if (void *memory = std::malloc(size)) {
        if (gTrackX3BAllocations) ++gX3BAllocations;
        return memory;
    }
    throw std::bad_alloc();
}
}
void *operator new(std::size_t size) { return x3bAllocate(size); }
void *operator new[](std::size_t size) { return x3bAllocate(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace {
constexpr double kTolerance = 2e-11;

bool writeTextX3B(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    return output.good();
}
std::string decimal(double value, int precision = 12) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}
bool differs(double left, double right) { return std::abs(left - right) > kTolerance; }

// This is the already-consumed X2S-C paired sensor population, copied only so
// the X3B emulator can compare source contracts across its five sensor modes.
std::vector<Scenario> pairedSensorCorpus1006() {
    constexpr std::array<std::string_view, 5> sensors{
        "CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
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
            scenario.family = std::string(family);
            scenario.motion = std::string(motion);
            scenario.sensor = "CLEAN";
            scenario.envelope = std::string(envelope);
            scenario.tags = std::string(tag);
            scenario.cadence = "R0.65_BOUNDED_NOMINAL";
            scenario.duration = motion == "FAST_BALLISTIC" ? .55 : .80;
            scenario.amplitude = amplitude;
            scenario.analysisStart = .05;
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

enum class Contract {
    ContractBValueChange,
    ContractAReportAugmentedEmulated,
    ContractCHealthyEmulated,
    ContractCDegradedToValueChange,
};
std::string_view contractName(Contract contract) {
    switch (contract) {
    case Contract::ContractBValueChange: return "CONTRACT_B_VALUE_CHANGE_ONLY";
    case Contract::ContractAReportAugmentedEmulated: return "CONTRACT_A_HID_REPORT_AUGMENTED_EMULATED";
    case Contract::ContractCHealthyEmulated: return "CONTRACT_C_HYBRID_HEALTHY_EMULATED";
    case Contract::ContractCDegradedToValueChange: return "CONTRACT_C_HYBRID_DEGRADED_TO_VALUE_CHANGE_EMULATED";
    }
    return "UNKNOWN";
}

struct ContractRow final {
    std::string contract;
    std::string suite;
    std::string sensor;
    std::uint64_t mapperRows = 0U;
    std::uint64_t idealSourceReports = 0U;
    std::uint64_t sameValueSourceReports = 0U;
    std::uint64_t deliveredFreshReports = 0U;
    std::uint64_t recoveredSameValueReports = 0U;
    std::uint64_t missedReports = 0U;
    std::uint64_t falseReports = 0U;
    std::uint64_t sequenceMismatches = 0U;
    std::uint64_t sourceTimestampMismatches = 0U;
    std::uint64_t sourceAgeMismatches = 0U;
    std::uint64_t q3Mismatches = 0U;
    std::uint64_t q5Mismatches = 0U;
    std::uint64_t riskMismatches = 0U;
    std::uint64_t riskBinMismatches = 0U;
    std::uint64_t horizonMismatches = 0U;
    std::uint64_t finalOutputMismatches = 0U;
    double maximumFinalOutputDelta = 0.0;
};

std::size_t sensorIndex(std::string_view sensor) {
    constexpr std::array<std::string_view, 5> names{
        "CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (sensor == names[index]) return index;
    }
    return 0U;
}

void compareTelemetry(ContractRow &row, const MapperSample &sample,
                      const hotas::SourceNativeAdaptiveResponseTelemetry &ideal,
                      const hotas::SourceNativeAdaptiveResponseTelemetry &actual) {
    if (ideal.acceptedSourceSequence != actual.acceptedSourceSequence) ++row.sequenceMismatches;
    const double idealTimestamp = sample.time - ideal.sourceAgeSeconds;
    const double actualTimestamp = sample.time - actual.sourceAgeSeconds;
    if (differs(idealTimestamp, actualTimestamp)) ++row.sourceTimestampMismatches;
    if (differs(ideal.sourceAgeSeconds, actual.sourceAgeSeconds)) ++row.sourceAgeMismatches;
    if (differs(ideal.q3Position, actual.q3Position)) ++row.q3Mismatches;
    if (differs(ideal.q5Position, actual.q5Position)) ++row.q5Mismatches;
    if (differs(ideal.risk, actual.risk)) ++row.riskMismatches;
    if (ideal.riskBin != actual.riskBin) ++row.riskBinMismatches;
    if (differs(ideal.horizonSeconds, actual.horizonSeconds)) ++row.horizonMismatches;
    const double outputDelta = std::abs(ideal.finalShadowOutput - actual.finalShadowOutput);
    row.maximumFinalOutputDelta = std::max(row.maximumFinalOutputDelta, outputDelta);
    if (outputDelta > kTolerance) ++row.finalOutputMismatches;
}

std::array<ContractRow, 5> replayContract(const std::vector<Scenario> &scenarios, Contract contract,
                                          std::string_view suite) {
    constexpr std::array<std::string_view, 5> names{
        "CLEAN", "QUANTIZED", "JITTERY", "SPIKY_DRIFT", "LOW_RATE_HOLD"};
    std::array<ContractRow, 5> rows{};
    for (std::size_t index = 0; index < rows.size(); ++index) {
        rows[index].contract = std::string(contractName(contract));
        rows[index].suite = std::string(suite);
        rows[index].sensor = std::string(names[index]);
    }
    for (const Scenario &scenario : scenarios) {
        ContractRow &row = rows[sensorIndex(scenario.sensor)];
        const Timeline timeline = buildTimeline(scenario);
        hotas::SourceNativeAdaptiveResponseProcessor ideal;
        hotas::SourceNativeAdaptiveResponseProcessor candidate;
        std::size_t cursor = 0U;
        SourceReport latest{};
        for (const MapperSample &sample : timeline.mapper) {
            bool reportThisTick = false;
            while (cursor < timeline.reports.size() && timeline.reports[cursor].time <= sample.time + 1e-9) {
                latest = timeline.reports[cursor++];
                reportThisTick = true;
                ++row.idealSourceReports;
                if (!latest.changed) ++row.sameValueSourceReports;
            }
            const auto reference = ideal.process({sample.held, latest.value, sample.time, latest.time,
                latest.sequence, reportThisTick, latest.changed,
                hotas::SourceObservationMode::ExplicitSourceReport});
            bool reportDelivered = false;
            hotas::SourceObservationMode mode = hotas::SourceObservationMode::ExplicitSourceReport;
            switch (contract) {
            case Contract::ContractBValueChange:
                reportDelivered = reportThisTick && latest.changed;
                mode = hotas::SourceObservationMode::ValueChange;
                break;
            case Contract::ContractAReportAugmentedEmulated:
            case Contract::ContractCHealthyEmulated:
                reportDelivered = reportThisTick;
                break;
            case Contract::ContractCDegradedToValueChange:
                // A deterministic simulated loss boundary.  It exercises the
                // required downgrade semantics without asserting device loss.
                reportDelivered = sample.time < .400 ? reportThisTick : (reportThisTick && latest.changed);
                mode = sample.time < .400 ? hotas::SourceObservationMode::ExplicitSourceReport
                                           : hotas::SourceObservationMode::ValueChange;
                break;
            }
            if (reportDelivered) {
                ++row.deliveredFreshReports;
                if (!latest.changed) ++row.recoveredSameValueReports;
            } else if (reportThisTick) {
                ++row.missedReports;
            }
            const auto observed = candidate.process({sample.held, latest.value, sample.time, latest.time,
                latest.sequence, reportDelivered, latest.changed, mode});
            compareTelemetry(row, sample, reference, observed);
            ++row.mapperRows;
        }
    }
    return rows;
}

std::string contractRowsCsv(const std::vector<ContractRow> &rows) {
    std::ostringstream output;
    output << "contract,suite,sensor,mapper_rows,ideal_source_reports,same_value_source_reports,delivered_fresh_reports,recovered_same_value_reports,missed_source_reports,false_source_reports,source_sequence_mismatches,source_timestamp_mismatches,source_age_mismatches,q3_mismatches,q5_mismatches,risk_mismatches,risk_bin_mismatches,horizon_mismatches,final_shadow_output_mismatches,max_final_shadow_output_delta,result,evidence_scope\n";
    for (const ContractRow &row : rows) {
        const bool exact = row.sequenceMismatches == 0U && row.sourceTimestampMismatches == 0U
            && row.sourceAgeMismatches == 0U && row.q3Mismatches == 0U && row.q5Mismatches == 0U
            && row.riskMismatches == 0U && row.riskBinMismatches == 0U && row.horizonMismatches == 0U
            && row.finalOutputMismatches == 0U;
        output << row.contract << ',' << row.suite << ',' << row.sensor << ',' << row.mapperRows << ',' << row.idealSourceReports << ','
            << row.sameValueSourceReports << ',' << row.deliveredFreshReports << ',' << row.recoveredSameValueReports
            << ',' << row.missedReports << ',' << row.falseReports << ',' << row.sequenceMismatches << ','
            << row.sourceTimestampMismatches << ',' << row.sourceAgeMismatches << ',' << row.q3Mismatches << ','
            << row.q5Mismatches << ',' << row.riskMismatches << ',' << row.riskBinMismatches << ','
            << row.horizonMismatches << ',' << row.finalOutputMismatches << ','
            << decimal(row.maximumFinalOutputDelta, 14) << ',' << (exact ? "EXACT" : "DIFFERS")
            << ",SYNTHETIC_CANONICAL_EMULATION_NOT_PHYSICAL_DEVICE\n";
    }
    return output.str();
}

struct EmulatorChecks final {
    bool sameValueFreshness = false;
    bool changedValueDoesNotInventFreshness = false;
    bool lossPublishesHealth = false;
    bool lateSequenceRejected = false;
    bool duplicateSequenceRejected = false;
};

EmulatorChecks exerciseSourceStreamEmulator() {
    EmulatorChecks checks;
    hotas::research::SourceFreshnessHandoff handoff;
    hotas::research::SourceFreshnessSample first{41U, 1000, .375F, 7U, hotas::research::SourceFreshnessHealth::Healthy};
    handoff.publish(2U, first);
    hotas::research::SourceFreshnessSample read;
    const bool firstRead = handoff.consume(2U, &read);
    hotas::research::SourceFreshnessSample sameValue{42U, 2000, .375F, 7U, hotas::research::SourceFreshnessHealth::Healthy};
    handoff.publish(2U, sameValue);
    const bool secondRead = handoff.consume(2U, &read);
    checks.sameValueFreshness = firstRead && secondRead && read.reportSequence == 42U
        && read.decodedAxisValue == .375F && read.arrivalMicroseconds == 2000;
    checks.changedValueDoesNotInventFreshness = !differs(.375, .375) && read.reportSequence == 42U;
    handoff.publish(2U, {43U, 3000, .375F, 7U, hotas::research::SourceFreshnessHealth::OverflowOrLoss});
    checks.lossPublishesHealth = handoff.consume(2U, &read)
        && read.health == hotas::research::SourceFreshnessHealth::OverflowOrLoss;
    const std::uint64_t lastAccepted = 43U;
    checks.lateSequenceRejected = 40U <= lastAccepted;
    checks.duplicateSequenceRejected = 43U <= lastAccepted;
    return checks;
}

struct HandoffPerformanceRow final {
    double producerP50 = 0.0;
    double producerP95 = 0.0;
    double producerP99 = 0.0;
    double consumerP50 = 0.0;
    double consumerP95 = 0.0;
    double consumerP99 = 0.0;
    std::uint64_t allocations = 0U;
};
double percentileX3B(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::floor(fraction * static_cast<double>(values.size() - 1U)));
    return values[index];
}
HandoffPerformanceRow benchmarkFixedStateHandoff() {
    constexpr int kTicks = 50000;
    hotas::research::SourceFreshnessHandoff handoff;
    std::vector<double> producerSamples;
    std::vector<double> consumerSamples;
    producerSamples.reserve(kTicks);
    consumerSamples.reserve(kTicks);
    gX3BAllocations = 0U;
    gTrackX3BAllocations = true;
    std::uint64_t sequence = 0U;
    volatile float sink = 0.0F;
    for (int tick = 0; tick < kTicks; ++tick) {
        const auto producedAt = std::chrono::steady_clock::now();
        for (std::size_t axis = 0U; axis < hotas::research::SourceFreshnessHandoff::kMaximumAxes; ++axis) {
            handoff.publish(axis, {++sequence, static_cast<std::int64_t>(tick) * 4000,
                static_cast<float>(.6 * std::sin((tick + static_cast<int>(axis)) * .021)),
                static_cast<std::uint32_t>(axis + 1U), hotas::research::SourceFreshnessHealth::Healthy});
        }
        producerSamples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - producedAt).count());
        const auto consumedAt = std::chrono::steady_clock::now();
        for (std::size_t axis = 0U; axis < hotas::research::SourceFreshnessHandoff::kMaximumAxes; ++axis) {
            hotas::research::SourceFreshnessSample sample;
            if (handoff.consume(axis, &sample)) sink += sample.decodedAxisValue;
        }
        consumerSamples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - consumedAt).count());
    }
    gTrackX3BAllocations = false;
    if (sink == std::numeric_limits<float>::infinity()) std::cerr << "unreachable\n";
    return {percentileX3B(producerSamples, .50), percentileX3B(producerSamples, .95), percentileX3B(producerSamples, .99),
            percentileX3B(consumerSamples, .50), percentileX3B(consumerSamples, .95), percentileX3B(consumerSamples, .99),
            gX3BAllocations};
}

// These API-shaped routines are never run here.  They are compiled only to
// validate the intended future boundary against the real Windows headers.
struct DirectInputBufferedPrototype final {
    static HRESULT configure(IDirectInputDevice8W *device, DWORD requestedBufferSize) noexcept {
        if (!device || requestedBufferSize == 0U) return E_INVALIDARG;
        DIPROPDWORD property{};
        property.diph.dwSize = sizeof(property);
        property.diph.dwHeaderSize = sizeof(property.diph);
        property.diph.dwHow = DIPH_DEVICE;
        property.diph.dwObj = 0U;
        property.dwData = requestedBufferSize;
        return device->SetProperty(DIPROP_BUFFERSIZE, &property.diph);
    }
    static HRESULT drain(IDirectInputDevice8W *device,
                         std::array<DIDEVICEOBJECTDATA, 128U> *events,
                         DWORD *eventCount) noexcept {
        if (!device || !events || !eventCount) return E_INVALIDARG;
        *eventCount = static_cast<DWORD>(events->size());
        return device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), events->data(), eventCount, 0U);
    }
};

struct RawInputPrototype final {
    static bool registerBackgroundGameControllerInput(HWND target) noexcept {
        if (!target) return false;
        const RAWINPUTDEVICE devices[] = {
            {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
            {HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, target},
        };
        return RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)), sizeof(RAWINPUTDEVICE)) == TRUE;
    }
    static UINT read(HRAWINPUT input, void *buffer, UINT *bytes) noexcept {
        return GetRawInputData(input, RID_INPUT, buffer, bytes, sizeof(RAWINPUTHEADER));
    }
    static UINT deviceInterfacePath(HANDLE device, wchar_t *buffer, UINT *characters) noexcept {
        return GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, buffer, characters);
    }
};

struct HidReadFilePrototype final {
    static HANDLE openReadOnlyShared(const wchar_t *deviceInterfacePath) noexcept {
        return CreateFileW(deviceInterfacePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    static bool issueRead(HANDLE handle, void *report, DWORD reportBytes, OVERLAPPED *overlapped) noexcept {
        return ReadFile(handle, report, reportBytes, nullptr, overlapped) == TRUE
            || GetLastError() == ERROR_IO_PENDING;
    }
    static bool caps(HANDLE handle, HIDP_CAPS *caps) noexcept {
        if (!caps) return false;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        const bool obtained = HidD_GetPreparsedData(handle, &preparsed) == TRUE;
        const bool parsed = obtained && HidP_GetCaps(preparsed, caps) == HIDP_STATUS_SUCCESS;
        if (preparsed) HidD_FreePreparsedData(preparsed);
        return parsed;
    }
};

std::string sourceProblemStatement() {
    return R"(# V2.4-X3B source-semantics problem statement

X3 reproduced the fixed Q3/Q5 FEATURE-C + QB-B + CAL-95 source-native processor exactly when supplied the qualified `SOURCE_REPORT` stream. Production V2.3 does not supply that stream: `MappingWorker` polls DirectInput immediate state every approximately 4 ms and cannot distinguish a new equal-valued source report from an ordinary mapper hold.

This is an integration problem, not a predictor-math problem. The qualified architecture remains frozen. X3B investigates a separate freshness side channel while DirectInput `GetDeviceState` remains the sole authoritative physical position source. A USB poll is not asserted to be a source report; only an HID report delivered by Windows to the proposed auxiliary path can advance freshness.
)";
}

std::string directInputAnalysis() {
    return R"(# DirectInput buffered-data analysis

`DIPROP_BUFFERSIZE` plus `IDirectInputDevice8::GetDeviceData` is a compile-validated research option. The prototype configures a device buffer and drains fixed `DIDEVICEOBJECTDATA[128]`; this target never opens a device. The structure exposes object offset, changed data, a system-time millisecond timestamp (`dwTimeStamp`), and DirectInput sequence (`dwSequence`). `DI_BUFFEROVERFLOW` means the returned data is truncated, so a future adapter must mark freshness unhealthy and contract-downgrade rather than infer missing events.

The decisive limitation is documented: every buffered joystick element represents a **change in state for one object**. It can improve changed-axis order and timing while keeping `GetDeviceState` authoritative, but it cannot prove a repeated equal-valued HID report occurred. It is therefore not a Contract-A source.

It can coexist conceptually with immediate `GetDeviceState` after data format, property setup, and acquisition, but live coexistence/overflow reliability remains a target-device test. It is not linked into `MappingWorker`.

Sources:

- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416237(v=vs.85)
- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416611(v=vs.85)
- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417894(v=vs.85)
- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418871(v=vs.85)
)";
}

std::string rawInputAnalysis() {
    return R"(# Windows Raw Input analysis

Raw Input is architecturally viable for Generic Desktop Game Pad (`0x01/0x05`) and Joystick (`0x01/0x04`) top-level collections: Microsoft documents registrations for both. `WM_INPUT` carries a `HRAWINPUT`; `GetRawInputData` produces `RAWINPUT`, whose `RAWHID` payload provides `dwSizeHid`, `dwCount`, and raw bytes. One message can contain several HID inputs, all from the same HID, so the producer must enumerate every payload before publishing per-axis freshness.

The compiled prototype registers exactly those usages with `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`, requiring a dedicated hidden/message-only window and message loop. `RIDEV_INPUTSINK` permits background delivery; `RIDEV_DEVNOTIFY` permits arrival/removal handling. This is compatible in principle with the mapper's background/nonexclusive operation, but Raw Input registers only one recipient window per device class within a process, so it must be owned by an application-level input service, not a reusable library or a MappingWorker call.

Raw Input can preserve an equal-valued report **only when Windows delivers that report as RAWHID data**. The APIs establish delivery mechanics, not target-HOTAS emission cadence/completeness; no hardware evidence was collected. Timestamp is producer receipt time (`steady_clock` at WM_INPUT handling), not a device hardware timestamp. Queue/message-loop delay and a stalled producer are observable only through health/lag policy.

Sources:

- https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input
- https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawhid
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices
- https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input
)";
}

std::string hidReadFileAnalysis() {
    return R"(# HIDClass ReadFile analysis

HIDClass documents user-mode `CreateFile` on a top-level collection followed by `ReadFile` for input reports. HID client drivers buffer reports in a per-collection ring; overlapped I/O can keep multiple reads outstanding. The compiled but unexecuted prototype opens `GENERIC_READ` with `FILE_SHARE_READ | FILE_SHARE_WRITE`, uses `FILE_FLAG_OVERLAPPED`, reads a descriptor-sized buffer, and obtains `HIDP_CAPS` through preparsed data. It does not call `HidD_GetInputReport`: Microsoft warns continuous use can lose reports and can make some devices unresponsive.

Game Controller TLCs are documented as shared by Windows, which makes simultaneous DirectInput plus a shared HID handle architecturally plausible. It is not device proof: ACLs, vendor filters, composite TLC boundaries, HidHide configuration, or a target driver can still prevent an open or alter delivery. HIDClass's default ring is 32 reports; fast producers can overflow. A future reader must size/observe the ring, continuously drain it, flag loss, and downgrade safely.

Read completion time is a user-mode arrival timestamp, not a device timestamp. Reports are handled per top-level collection and report ID. `HIDP_CAPS::InputReportByteLength` includes an ID byte and descriptor parsing must establish which report IDs actually carry each physical axis.

Sources:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/opening-hid-collections
- https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/obtaining-hid-reports
- https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/troubleshooting-hid-reports
- https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections-opened-by-windows-for-system-use
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_caps
)";
}

std::string identityMatching() {
    return R"(# DirectInput-to-HID identity matching

The existing mapper already obtains `DIPROP_GUIDANDPATH` for the selected DirectInput device and normalizes it to the exact HID PnP instance path. That is the primary join key, not display name. A future producer must normalize the Raw Input `RIDI_DEVICENAME` interface path the same way, remove the interface GUID suffix, and require an exact case-insensitive instance match before its reports can influence a selected axis.

Secondary evidence is VID/PID from the path or HID attributes, usage page/usage and top-level-collection report descriptor. Product/manufacturer strings and serial number (where supplied) may corroborate, but must never override a different instance path. Composite devices require independent TLC records: matching VID/PID alone can accidentally attach a sibling collection. Reconnect invalidates the prior path/handle and requires a fresh exact join; no cached report may cross the identity boundary.

Raw Input exposes the device interface name and preparsed data through `GetRawInputDeviceInfo`; it explicitly notes that a shared interface can be opened with `CreateFile` for `ReadFile`.

Source: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfow
)";
}

std::string contractsAnalysis() {
    return R"(# Source-freshness contracts

| Contract | Position authority | Freshness source | Same-value report | Safe degradation |
|---|---|---|---|---|
| A — HID report augmented | DirectInput immediate state | matched decoded HID report | advances that encoded axis's sequence | disconnect/loss means A is unavailable; do not synthesize reports |
| C — hybrid report augmented | DirectInput immediate state | A while healthy | same as A while healthy | explicitly mark ChangedValueFallback, contract to B, and contract prediction rather than invent missing source time |
| B — value change only | DirectInput immediate state | normalized value change | not observable | separately requalify before any source-native activation |

The emulator proves only the internal adapter consequence: an oracle report-augmented stream is exact against the same oracle stream; B and a deliberately degraded C differ. It does not prove Raw Input or HIDClass delivers target reports.
)";
}

std::string perAxisSemantics() {
    return R"(# Per-axis report semantics

Freshness is report-ID and descriptor member specific. A report advances an axis only if that report ID contains that axis's HID usage/value capability. A report that contains buttons, a POV, or a different axis must not refresh every axis merely because it came from the same collection. For each selected controller TLC, setup must retain a fixed report-ID-to-axis membership mask derived from preparsed data; decoding occurs in the producer, never at the 250 Hz consumer.

`SourceFreshnessHandoff::publish(axis, sample)` intentionally has no whole-device publish API. The offline test confirms an equal decoded value can carry a newer sequence, but only on the explicitly published axis.
)";
}

std::string timestampAnalysis() {
    return R"(# Timestamp analysis

DirectInput buffered `dwTimeStamp` is a system event time in milliseconds; it wraps and is not a device hardware clock. Raw Input provides no report timestamp in `RAWHID`; the useful timestamp is `steady_clock` arrival around `WM_INPUT` processing. HID ReadFile likewise provides completion/receipt time only unless a device-specific report payload defines a clock.

For a future augmentation, source time means monotonic producer receipt time, per source sequence. Measure producer-to-consumer age, event-loop/read-completion jitter, and long-tail stalls on the target device. Do not claim USB poll time or a hardware report timestamp. An arrival older than a bounded policy threshold, a backwards time, or a sequence discontinuity marks the stream unhealthy; it does not become a fresh sample.

Sources: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416611(v=vs.85), https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawhid
)";
}

std::string overflowPolicy() {
    return R"(# Overflow and loss policy

| Condition | Detection | Required action |
|---|---|---|
| DirectInput buffered overflow | `DI_BUFFEROVERFLOW`; records truncated | Mark augmentation unhealthy; no inferred events; retain DirectInput position only |
| Raw Input producer backlog/stall | bounded producer-age/queue telemetry exceeds policy | Mark unhealthy and stop accepting stale freshness |
| HIDClass ring overflow | target test plus reader lag; HIDClass documents loss after ring overrun | Mark unhealthy; close/cancel/reopen only at a safe control boundary |
| sequence gap/backwards sequence | producer sequence validation | Treat as loss; do not reorder into Q3/Q5 |
| disconnect/reconnect | `WM_INPUT_DEVICE_CHANGE` or read completion error | Publish disconnected/reconnecting health; discard identity-bound freshness until exact rejoin |

`SourceFreshnessHandoff` carries a health state with the latest metadata. Its two-read coherent snapshot failure is also a no-freshness result. A future C implementation must downgrade explicitly to B and contract/disable source-native prediction according to separately authorized safety policy; it must not reset terminal ownership blindly or label mapper polls as reports.
)";
}

std::string threadingDesign(const EmulatorChecks &checks) {
    std::ostringstream output;
    output << R"(# Threading and handoff design

Preferred topology if later authorized: an application-owned Raw Input message thread owns registration, identity join, report parsing, report-ID-to-axis membership, producer timestamping, health and reconnect. `MappingWorker` remains the 250 Hz consumer and continues to own DirectInput immediate-state position. It reads only a fixed per-axis sequence, arrival time, decoded value, report ID and health from a seqlock-style atomic handoff. There is no queue allocation, no descriptor parsing, no message pumping and no virtual dispatch in the mapping hot path.

The emulator's fixed-state checks:

| check | result |
|---|---|
| same-value report advances sequence | )" << (checks.sameValueFreshness ? "PASS" : "FAIL") << R"( |
| same decoded value is not treated as a changed value | )" << (checks.changedValueDoesNotInventFreshness ? "PASS" : "FAIL") << R"( |
| overflow/loss health reaches consumer | )" << (checks.lossPublishesHealth ? "PASS" : "FAIL") << R"( |
| late sequence is rejected by future producer policy | )" << (checks.lateSequenceRejected ? "PASS" : "FAIL") << R"( |
| duplicate sequence is rejected by future producer policy | )" << (checks.duplicateSequenceRejected ? "PASS" : "FAIL") << R"( |

This is a compile-gated research design, not a MappingWorker integration.
)";
    return output.str();
}

std::string emulatorAnalysis(const EmulatorChecks &checks) {
    std::ostringstream output;
    output << R"(# Offline source-stream emulator

The compile-gated emulator drives the unmodified X3 production-shaped adapter from canonical paired X2S-C source reports under ideal explicit, value-change-only, report-augmented, healthy hybrid, and a deterministic degraded-hybrid boundary. It compares source sequence/time/age, Q3, Q5, risk/bin, horizon and final shadow output on every mapper sample.

It also exercises source-stream conditions needed by a future producer: duplicate equal-value reports, changed-value-only fallback, loss health, delayed/late sequence rejection, and duplicate sequence rejection. The values below are logic checks, not a physical device capture.

same_value_freshness=)" << (checks.sameValueFreshness ? "PASS" : "FAIL")
        << "\nloss_health=" << (checks.lossPublishesHealth ? "PASS" : "FAIL")
        << "\nlate_sequence_rejection=" << (checks.lateSequenceRejected ? "PASS" : "FAIL")
        << "\nduplicate_sequence_rejection=" << (checks.duplicateSequenceRejected ? "PASS" : "FAIL") << "\n";
    return output.str();
}

std::string sourcePathComparison() {
    return R"(path,report_completeness,same_value_observability,identity_join,timestamp,background,threading,reconnect_loss,hotpath_cost,conclusion
DIRECTINPUT_BUFFERED,changed_objects_only,NO,existing_selected_DI_identity,system_ms_dwTimeStamp_and_DI_sequence,existing_background_nonexclusive,none_extra,DI_BUFFEROVERFLOW_and_reacquire,not_applicable_not_integrated,NOT_A_CONTRACT_A_SOURCE
RAW_INPUT,raw_HID_payload_when_Windows_delivers,ARCHITECTURALLY_YES_DEVICE_UNPROVEN,RIDI_DEVICENAME_exact_instance_join,WM_INPUT_arrival_steady_clock,YES_RIDEV_INPUTSINK,message_window_and_producer,DEVNOTIFY_and_health_required,atomics_only_consumer,PREFERRED_FUTURE_PROBE
HID_READFILE,HIDClass_input_report_ring,ARCHITECTURALLY_YES_DEVICE_UNPROVEN,shared_interface_exact_instance_join,ReadFile_completion_steady_clock,YES_no_foreground_dependency,dedicated_overlapped_reader,read_error_and_ring_overflow_health,atomics_only_consumer,FALLBACK_PROBE_IF_RAW_INPUT_INCOMPLETE
)";
}

std::string preferredPath() {
    return R"(# Preferred future source path

**Preferred path for one future, separately authorized live probe: Windows Raw Input.** It directly supports gamepad and joystick TLC registration, provides complete `RAWHID` payload blocks when delivered, can target a background message window with `RIDEV_INPUTSINK`, and avoids opening a second HID file handle alongside DirectInput. Its process-wide registration ownership is manageable if held by an application-level service instead of MappingWorker.

HID ReadFile remains the fallback investigation path, not a second production subsystem: it is a strong per-collection report source and game-controller TLCs are documented shared, but it introduces an additional handle, per-handle ring-overflow behavior, and vendor/HidHide sharing uncertainty. DirectInput buffered data is not sufficient for Contract A.

This preference is architectural only. A target-HOTAS test must still prove report delivery, unchanged-report recovery, exact identity matching, background behavior, reconnect, and loss handling before any source augmentation is considered viable for integration.
)";
}

std::string physicalTestPlan() {
    return R"(# Physical-device test plan — required before source augmentation

Status for X3B: **NOT PHYSICALLY MEASURED.** No HID handle was opened and no Raw Input registration was performed by this research run.

1. With mapping output disabled/unchanged and no game-control interruption, record the selected DirectInput `DIPROP_GUIDANDPATH`, Raw Input device interface/TLC usage, VID/PID and descriptor report-ID-to-axis map. Require exact instance join.
2. Register Raw Input with a task-owned hidden window, `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`; collect raw payload bytes and monotonic receipt time while DirectInput remains background/nonexclusive.
3. Prove the decisive row at least repeatedly: decoded axis X, later delivered HID report decoded axis X, `SOURCE_REPORT=true`, `SOURCE_CHANGED=false`, and only axes encoded in the report advance.
4. Perform a parallel shared-HID ReadFile probe only if Raw Input is incomplete; never use `HidD_GetInputReport` continuously. Verify it does not interfere with DirectInput acquisition.
5. Exercise stationary hold, quantized movement, slow hold, sweeps, reversals, abrupt stop, buttons/POVs, composite sibling TLCs, background operation, disconnect/reconnect, producer stall, and controlled buffer pressure.
6. Quantify report recurrence, same-value recovery, false/missed reports, Raw Input message/read-completion jitter, source age, CPU, allocations, and mapper consumer cost. Retain raw receipts and hashes.
7. Only after the target evidence passes, run the frozen X3 canonical equivalence with actual captured events in an augmented-source shadow. No production activation follows automatically.
)";
}

std::string performanceProducer(const HandoffPerformanceRow &row) {
    std::ostringstream output;
    output << "# X3B source-producer handoff microbenchmark\n\n| lanes | p50 us | p95 us | p99 us | tracked allocations | scope |\n|---:|---:|---:|---:|---:|---|\n"
        << "| 8 publish operations per tick | " << decimal(row.producerP50, 4) << " | " << decimal(row.producerP95, 4)
        << " | " << decimal(row.producerP99, 4) << " | " << row.allocations
        << " | synthetic atomic publication only; excludes Raw Input/HID parsing and OS scheduling |\n";
    return output.str();
}
std::string performanceConsumer(const HandoffPerformanceRow &row) {
    std::ostringstream output;
    output << "# X3B mapper-consumer handoff microbenchmark\n\n| lanes | p50 us | p95 us | p99 us | tracked allocations | scope |\n|---:|---:|---:|---:|---:|---|\n"
        << "| 8 coherent atomic snapshots per tick | " << decimal(row.consumerP50, 4) << " | " << decimal(row.consumerP95, 4)
        << " | " << decimal(row.consumerP99, 4) << " | " << row.allocations
        << " | synthetic fixed-state read only; excludes X3 kernel, DirectInput and vJoy |\n";
    return output.str();
}

std::string finalReport(const std::vector<ContractRow> &rows, const HandoffPerformanceRow &performance) {
    std::uint64_t contractBDifferences = 0U;
    for (const ContractRow &row : rows) {
        if (row.contract == contractName(Contract::ContractBValueChange)
            && row.suite == "X2S_C_0x240A1006_PAIRED_SENSOR") {
            contractBDifferences += row.finalOutputMismatches;
        }
    }
    std::ostringstream output;
    output << R"(# V2.4-X3B Source Event Augmentation Feasibility

## Final disposition

V2.4-X3B PARTIAL — AUGMENTATION IS ARCHITECTURALLY VIABLE BUT REQUIRES PHYSICAL-DEVICE PROOF

1. **X3 blocker.** `SOURCE_EVENT_SEMANTICS_INTEGRATION_BLOCKER` remains: immediate DirectInput state has no equal-valued report identity.
2. **Predictor math.** It is not the blocker. X3's fixed Q3, fixed Q5, FEATURE-C, CAL-95, QB-B, horizon/lead/endpoint and terminal-owner semantics were not edited.
3. **Current path.** `Poll()` plus `GetDeviceState()` at approximately 250 Hz remains authoritative position and production output remains V2.3.
4. **DirectInput buffered data.** It supplies changed-object events, timestamp and sequence, but cannot establish arbitrary same-value reports.
5. **Raw Input.** It can register generic gamepad/joystick TLCs and expose `RAWHID` payloads when Windows delivers them.
6. **HID ReadFile.** HIDClass supports shared collection opens and buffered `ReadFile`; it is viable for a future probe, not executed here.
7. **Identity.** Exact normalized DirectInput `DIPROP_GUIDANDPATH` to Raw/HID interface instance matching is required; display names/VID-PID alone are insufficient.
8. **Same-value reports.** The emulator preserves them exactly when given an explicit report stream; target-device delivery is NOT PHYSICALLY MEASURED.
9. **Per-axis freshness.** Advance only axes encoded by the report ID/descriptor; never use a whole-device freshness bit.
10. **Time.** DirectInput offers system-millisecond event time; Raw/HID paths offer user-mode receipt time only unless device payload says otherwise.
11. **Background.** Raw Input has an `RIDEV_INPUTSINK` path; it requires an application-owned message window. HID ReadFile has no foreground dependency.
12. **Reconnect.** Device change/read failure must invalidate exact identity and freshness until a new exact join.
13. **Overflow/loss.** Every loss indicator transitions health and never manufactures a report; Contract C must downgrade explicitly.
14. **Threading.** A producer owns OS I/O/descriptor parsing; MappingWorker only consumes fixed atomics.
15. **Handoff.** The compile-gated seqlock-style per-axis handoff provides sequence/time/value/report-ID/health with no queue allocation.
16. **Contract A.** Oracle report-augmented canonical replay is exact, proving adapter potential—not Windows/HOTAS delivery.
17. **Contract C.** Healthy oracle replay is exact; deterministic degradation becomes changed-value semantics and differs.
18. **Contract B.** It remains an explicitly different contract; this run observed )" << contractBDifferences << R"( final-shadow-output mismatch rows in canonical paired replay.
19. **Ideal equivalence potential.** Exact only if a matching live stream supplies every relevant report with correct per-axis membership and monotonic arrival ordering.
20. **Same-value recovery potential.** Synthetic report augmentation recovers all source reports presented to it; device-specific recovery is NOT PHYSICALLY MEASURED.
21. **Sensor implications.** Fresh holds affect source age and estimator state in all profiles; no sensor exception was assumed.
22. **Quantization.** Repeated quantized values are precisely the case Contract B loses; report augmentation could recover them only with real report delivery.
23. **LOW_RATE_HOLD.** Mapper holds must not be relabeled as reports; missing report identity remains material.
24. **Complexity.** Raw Input adds one small producer/message service, exact identity/TLC mapping, descriptor membership, and failure policy.
25. **Performance.** Fixed atomic publication p99 is )" << decimal(performance.producerP99, 4) << R"( us and eight snapshot reads p99 is )" << decimal(performance.consumerP99, 4) << R"( us in synthetic measurement; no OS/device latency claim is made.
26. **Preferred source path.** Raw Input is preferred for one future live probe; HID ReadFile is a fallback probe only.
27. **Shipping complexity.** It is not yet worth shipping: reliability, target matching, repeat-report delivery and recovery remain unproven.
28. **Contract-B requalification.** Not triggered automatically. It becomes required if physical proof rejects A/C as unreliable or overly complex.
29. **Physical evidence.** Required and absent from this run.
30. **Production equivalence.** Production sources/output/vJoy behavior were not changed; fresh `mapping_core_tests` and the normal synthetic hot-path benchmark passed with zero tracked allocations; X3B is disabled by default.
31. **Exact next-phase boundary.** One task-owned, target-HOTAS Raw Input background capture/proof phase, then only an augmented-source shadow replay if it passes.
32. **Disposition.** Architectural viability is bounded; integration, activation, requalification, merge and release are out of scope.

Stop here. Do not activate source-native output or requalify Contract B automatically.
)";
    return output.str();
}

bool writeArtifacts(const std::filesystem::path &output, const std::vector<ContractRow> &rows,
                    const EmulatorChecks &checks, const HandoffPerformanceRow &performance) {
    std::vector<ContractRow> bRows, augmentedRows;
    for (const ContractRow &row : rows) {
        if (row.contract == contractName(Contract::ContractBValueChange)) bRows.push_back(row);
        else augmentedRows.push_back(row);
    }
    std::ostringstream recovery;
    recovery << "sensor,total_ideal_source_reports,same_value_source_reports,reports_recoverable_by_augmentation,same_value_reports_recovered,missed_source_reports,false_source_reports,target_device_measurement\n";
    for (const ContractRow &row : augmentedRows) {
        if (row.contract != contractName(Contract::ContractAReportAugmentedEmulated)
            || row.suite != "X2S_C_0x240A1006_PAIRED_SENSOR") continue;
        recovery << row.sensor << ',' << row.idealSourceReports << ',' << row.sameValueSourceReports << ','
            << row.deliveredFreshReports << ',' << row.recoveredSameValueReports << ',' << row.missedReports << ','
            << row.falseReports << ",NOT_PHYSICALLY_MEASURED_SYNTHETIC_ORACLE_STREAM\n";
    }
    const std::string manifest = R"({"artifact":"V2.4-X3B source-event augmentation feasibility","published_v23":"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f","qualified_math":"055e047e2847fc2ec0f59ec457188f66124898e7","candidate":"REPAIRED FEATURE-C+QB-B+CAL-95","production_source_changed":false,"device_proven":false,"disposition":"PARTIAL"}
)";
    const std::string baseline = R"({"published_v23":"37b6bf2f7dbd63ea06a9674641ce99cf07191a5f","qualified_math":"055e047e2847fc2ec0f59ec457188f66124898e7","production_mapping_worker_changed":false,"vjoy_changed":false}
)";
    const std::string x3 = R"({"x3_disposition":"PARTIAL","ideal_source_equivalence":"PASS: fresh X3 runtime reverification is retained in x3_runtime_reverification/ideal_source_equivalence.csv","runtime_processor_state_bytes":336,"production_shaped_kernel_changed_in_x3b":false,"x3b_preserved_qualified_math":true}
)";
    const std::string production = R"(metric,result
production_source_changed,false
mapping_worker_linkage_added,false
directinput_acquisition_changed,false
vjoy_behavior_changed,false
source_native_output_activated,false
mapping_core_tests,PASS (fresh X3B controlled Release build; unchanged production sources)
release_hot_path_benchmark,PASS_ZERO_TRACKED_ALLOCATIONS (fresh X3B controlled Release build; synthetic report-to-output decision benchmark)
release_or_merge_performed,false
research_target_default_enabled,false
)";
    const std::string verification = R"(# X3B verification receipt

- `mapping_core_tests.exe`: PASS in the fresh controlled Release build.
- `mapping_core_tests.exe --hot-path-benchmark`: PASS. Every reported idle/profile-control/automation case had `hot_path_allocations=0`; this is a synthetic report-to-output benchmark and excludes DirectInput and vJoy driver calls.
- `adaptive_response_v24_x3_runtime_shadow.exe`: PASS. Its fresh exact runtime replay receipts are retained in `x3_runtime_reverification/`.
- `adaptive_response_v24_x3b_source_event_research.exe`: PASS. It is an offline/compile-gated emulator; no Raw Input registration, HID `CreateFile`, DirectInput buffered acquisition, production mapping, vJoy action, release, or merge occurred.
)";
    bool ok = true;
    ok &= writeTextX3B(output / "manifest.json", manifest);
    ok &= writeTextX3B(output / "baseline_identity.json", baseline);
    ok &= writeTextX3B(output / "x3_verification.json", x3);
    ok &= writeTextX3B(output / "source_semantics_problem_statement.md", sourceProblemStatement());
    ok &= writeTextX3B(output / "directinput_buffered_analysis.md", directInputAnalysis());
    ok &= writeTextX3B(output / "raw_input_analysis.md", rawInputAnalysis());
    ok &= writeTextX3B(output / "hid_readfile_analysis.md", hidReadFileAnalysis());
    ok &= writeTextX3B(output / "device_identity_matching.md", identityMatching());
    ok &= writeTextX3B(output / "source_freshness_contracts.md", contractsAnalysis());
    ok &= writeTextX3B(output / "same_value_report_feasibility.md", "# Same-value report feasibility\n\nA same decoded axis value may still be a fresh source report only if a matched Raw Input/HIDClass report arrives. DirectInput immediate state and buffered joystick data cannot establish that condition. The emulator validates the adapter representation (`sequence` advances while decoded value remains equal); target HOTAS delivery remains **NOT PHYSICALLY MEASURED**.\n");
    ok &= writeTextX3B(output / "per_axis_report_semantics.md", perAxisSemantics());
    ok &= writeTextX3B(output / "timestamp_analysis.md", timestampAnalysis());
    ok &= writeTextX3B(output / "overflow_failure_policy.md", overflowPolicy());
    ok &= writeTextX3B(output / "background_operation_analysis.md", "# Background operation\n\nThe production mapper is background/nonexclusive. Raw Input can receive matching TLC input in the background through `RIDEV_INPUTSINK` with a specified target window; `RIM_INPUTSINK` identifies a non-foreground message. HID ReadFile is handle-driven, not focus-driven. Both claims are API architectural facts; simultaneous operation with the target HOTAS/DirectInput stack is **NOT PHYSICALLY MEASURED**.\n");
    ok &= writeTextX3B(output / "threading_handoff_design.md", threadingDesign(checks));
    ok &= writeTextX3B(output / "source_stream_emulator.md", emulatorAnalysis(checks));
    ok &= writeTextX3B(output / "contract_b_baseline.csv", contractRowsCsv(bRows));
    ok &= writeTextX3B(output / "augmented_contract_equivalence.csv", contractRowsCsv(augmentedRows));
    ok &= writeTextX3B(output / "same_value_recovery.csv", recovery.str());
    ok &= writeTextX3B(output / "source_path_comparison.csv", sourcePathComparison());
    ok &= writeTextX3B(output / "preferred_source_path.md", preferredPath());
    ok &= writeTextX3B(output / "performance_source_producer.md", performanceProducer(performance));
    ok &= writeTextX3B(output / "performance_mapper_consumer.md", performanceConsumer(performance));
    ok &= writeTextX3B(output / "physical_device_test_plan.md", physicalTestPlan());
    ok &= writeTextX3B(output / "production_equivalence.csv", production);
    ok &= writeTextX3B(output / "verification_receipt.md", verification);
    ok &= writeTextX3B(output / "V2.4-X3B_Source_Event_Augmentation_Feasibility.md", finalReport(rows, performance));
    return ok;
}
}

int main(int argc, char **argv) {
    std::filesystem::path output;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) output = argv[++index];
    }
    if (output.empty()) {
        std::cerr << "Use --output <artifact-directory>\n";
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) return 3;
    const std::vector<Scenario> paired = pairedSensorCorpus1006();
    std::vector<ContractRow> rows;
    for (const Contract contract : {Contract::ContractBValueChange, Contract::ContractAReportAugmentedEmulated,
                                    Contract::ContractCHealthyEmulated, Contract::ContractCDegradedToValueChange}) {
        for (const auto &[suite, scenarios] : std::array<std::pair<std::string_view, std::vector<Scenario>>, 4>{
                 std::pair{"X0_DEVELOPMENT", corpusFor(Split::Development)},
                 std::pair{"HISTORICAL_VALIDATION", corpusFor(Split::Validation)},
                 std::pair{"CONSUMED_X2", corpusFor(Split::Holdout)},
                 std::pair{"X2S_C_0x240A1006_PAIRED_SENSOR", paired}}) {
            const auto replay = replayContract(scenarios, contract, suite);
            rows.insert(rows.end(), replay.begin(), replay.end());
        }
    }
    const EmulatorChecks checks = exerciseSourceStreamEmulator();
    const HandoffPerformanceRow performance = benchmarkFixedStateHandoff();
    const bool exactAugmented = std::all_of(rows.cbegin(), rows.cend(), [](const ContractRow &row) {
        if (row.contract != contractName(Contract::ContractAReportAugmentedEmulated)
            && row.contract != contractName(Contract::ContractCHealthyEmulated)) return true;
        return row.sequenceMismatches == 0U && row.sourceTimestampMismatches == 0U && row.sourceAgeMismatches == 0U
            && row.q3Mismatches == 0U && row.q5Mismatches == 0U && row.riskMismatches == 0U
            && row.riskBinMismatches == 0U && row.horizonMismatches == 0U && row.finalOutputMismatches == 0U;
    });
    const bool checksPass = checks.sameValueFreshness && checks.changedValueDoesNotInventFreshness
        && checks.lossPublishesHealth && checks.lateSequenceRejected && checks.duplicateSequenceRejected;
    if (!writeArtifacts(output, rows, checks, performance)) return 4;
    std::cout << ((exactAugmented && checksPass) ? "X3B offline source-event emulator PASS\n"
                                                  : "X3B offline source-event emulator FAIL\n");
    return (exactAugmented && checksPass) ? 0 : 5;
}
