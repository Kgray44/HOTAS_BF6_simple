#pragma once

#include "mapping_types.h"

#include <QMutex>
#include <QThread>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

struct IDirectInput8W;

namespace hotas {

constexpr size_t kLatencyTelemetrySamples = 2048;

struct MappingLatencyPercentiles {
    std::uint64_t sampleCount = 0;
    std::uint64_t p95Us = 0;
    std::uint64_t p99Us = 0;
};

struct AtomicRuntimeState {
    std::array<std::atomic<float>, kPhysicalAxisCount> raw{};
    std::array<std::atomic<float>, kPhysicalAxisCount> normalized{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterDeadzone{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterHysteresis{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterInversion{};
    std::array<std::atomic<float>, kPhysicalAxisCount> curveResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> transformed{};
    // Adaptive Response publishes only this latest fixed-size snapshot. The
    // QML timer samples it independently; no report fires a UI event.
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveEstimated{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptivePredicted{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveBaselineMapped{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptivePredictedMapped{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveOutput{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveMappedLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveAppliedLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveLocalCurveGain{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveVelocity{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveAcceleration{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveHorizonSeconds{};
    // Fixed scalar diagnostics for the Flight Deck's lead pipeline. Like the
    // adjacent telemetry, these are published without allocating or waking QML.
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRequestedLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveCappedLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveEndpointTaper{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveConfidence{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveMotionIntensity{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveVelocityAuthority{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveDeliberateMotionEvidence{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveNormalMotionAuthority{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRapidMotionAuthority{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRapidMotionBlend{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveAccelerationIntent{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveOnsetAuthority{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveSustainedEvidence{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveSustainedAuthority{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveMotionUrgency{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveHorizonExtensionEligibility{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveNormalMaximumHorizonSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveAllowedMaximumHorizonSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveTurningPointConfidence{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveEstimatedTimeToTurnSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveEstimatedRemainingTravel{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveTurningPointHorizonLimitSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveTurningPointLeadLimit{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveReacquisitionAuthority{};
    std::array<std::atomic_int, kPhysicalAxisCount> adaptiveMotionState{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveReversing{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveSafetyLimited{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveDeadzoneAuthorityBlocked{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveLeadLimited{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveHighLocalCurveGain{};
    std::array<std::atomic_uint64_t, kPhysicalAxisCount> adaptiveReversalCount{};
    std::array<std::atomic_uint64_t, kPhysicalAxisCount> adaptiveSafetyClampCount{};
    // The worker's already-flattened config is published alongside live
    // telemetry. This lets presentation show the true Automation-adjusted
    // runtime instead of re-resolving only persistent configuration.
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveRuntimeEnabled{};
    std::array<std::atomic_int, kPhysicalAxisCount> adaptiveRuntimeModel{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeMaximumHorizonSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeMaximumLead{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeVelocityResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeAccelerationResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeMotionSensitivity{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeNoiseRejection{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeReversalDetection{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeReversalResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeDecelerationResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeSettlingResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeEndpointTaper{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeOnsetAssist{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeOnsetCap{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeSustainedAssist{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeSustainedCap{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeHorizonExtension{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeHorizonExtensionCapSeconds{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeTurningPointProtection{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeTurningPointMargin{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeNormalMovementResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeRapidMovementResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> adaptiveRuntimeEngagementSensitivity{};
    std::array<std::atomic_bool, kPhysicalAxisCount> adaptiveAutomationOverlayActive{};
    std::array<std::atomic_uint32_t, kPhysicalAxisCount> adaptiveAutomationOverlayProperties{};
    std::array<std::atomic<float>, kPhysicalAxisCount> virtualValues{};
    std::array<std::atomic_bool, kVirtualAxisSlotCount> virtualAxisAvailable{};
    std::array<std::atomic<bool>, kPhysicalAxisCount> axisAvailable{};
    std::array<std::atomic_int, kPhysicalAxisCount> axisActivity{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMinimum{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationCenter{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMaximum{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> physicalButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> virtualButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> buttonAvailable{};
    std::atomic_bool physicalConnected{false};
    std::atomic_int axisCount{0};
    std::atomic_int buttonCount{0};
    std::atomic_int povCount{0};
    // DirectInput reports up to four POVs in hundredths of a degree; -1 is
    // centered. The UI reads this fixed snapshot without entering the worker.
    std::array<std::atomic_int, kMaximumPhysicalPovs> povValues{};
    std::atomic_int vjoyButtonCount{0};
    std::atomic_int vjoyContinuousPovCount{0};
    std::atomic_int vjoyDiscretePovCount{0};
    std::atomic_int lastPhysicalButton{0};
    std::atomic_int lastPhysicalButtonTarget{0};
    std::atomic_bool mappingActive{false};
    std::atomic_int mappingEffectiveState{static_cast<int>(MappingEffectiveState::Off)};
    std::atomic_bool outputNeutralized{true};
    std::atomic_bool vjoyReady{false};
    // Runtime-output proof is written only as fixed-size atomics from the
    // mapper thread. QML and setup diagnostics sample it later; no report
    // allocates, logs, or wakes the GUI.
    std::atomic_int activeOutputVjoyDeviceId{0};
    std::atomic_bool outputReportsSucceeding{false};
    std::atomic_uint64_t successfulOutputReportSequence{0};
    std::atomic_uint64_t lastSuccessfulOutputReportMs{0};
    std::atomic_uint64_t outputWriteFailures{0};
    std::array<std::atomic<float>, kVirtualAxisSlotCount> mappedOutputAxes{};
    std::array<std::atomic_bool, kMaximumVirtualButtons> mappedOutputButtons{};
    std::atomic_bool hidhideAvailable{false};
    std::atomic_bool hidhideCloakStateKnown{false};
    std::atomic_bool hidhideCloaked{false};
    std::atomic_bool hidhideMapperAllowed{false};
    std::atomic_uint64_t inputReports{0};
    // A Device Rig is verified as a collection of independent physical
    // sources. These fixed sequence values carry meaningful changes only:
    // an axis crosses the configured evidence threshold, a button changes,
    // or a POV changes. They never build strings, allocate, lock, or signal
    // QML from MappingWorker's report loop.
    std::atomic_uint64_t meaningfulInputSequence{0};
    std::array<std::atomic_uint64_t, kMaximumDeviceRigMembers> deviceRigMeaningfulInputSequence{};
    std::array<std::atomic_uint64_t, kMaximumDeviceRigOutputs> deviceRigMeaningfulOutputSequence{};
    // Report/write counters remain diagnostics, not user-movement proof.
    std::array<std::atomic_uint64_t, kMaximumDeviceRigMembers> deviceRigInputReports{};
    std::array<std::atomic_uint64_t, kMaximumDeviceRigOutputs> deviceRigOutputWrites{};
    // This resets at a DirectInput acquisition boundary. It lets setup prove
    // that a freshly reopened physical device is still delivering reports,
    // without confusing historical report totals for current access.
    std::atomic_uint64_t physicalReportsSinceAcquisition{0};
    std::atomic_uint64_t vjoyWrites{0};
    std::atomic_uint64_t latencyCurrentUs{0};
    std::atomic_uint64_t latencyAverageUs{0};
    std::atomic_uint64_t latencyPeakUs{0};
    // The worker writes one bounded sample per report. Percentile sorting is
    // deliberately performed by the UI-side snapshot timer, never here.
    std::array<std::atomic_uint64_t, kLatencyTelemetrySamples> latencySamples{};
    std::atomic_uint64_t latencySampleCount{0};
    std::atomic_uint64_t profileSwitchCount{0};
    std::atomic_uint64_t lastProfileSwapUs{0};
    // Index/source are small worker-owned values; names stay on the UI side.
    std::atomic_int effectiveProfileIndex{0};
    std::atomic_int profileOverrideButton{0};
    std::atomic_int profileOverridePovHat{0};
    std::atomic_int profileOverridePovDirection{-1};
    std::atomic_int profileOverrideMode{static_cast<int>(ProfileTriggerMode::Disabled)};
    std::atomic_uint64_t lastCurveCompileUs{0};
    std::atomic_bool automationEngineEnabled{true};
    std::atomic_int automationRuleCount{0};
    std::atomic_int automationActiveRuleCount{0};
    std::atomic_uint64_t automationEvaluationUs{0};
    std::array<std::atomic_bool, kMaximumAutomationRules> automationRuleActive{};
    std::atomic_int profileOverrideAutomationRule{-1};
    std::atomic_int profileOverrideAutomationAction{-1};
};

struct DeviceSnapshot {
    QString name = u"No controller detected"_qs;
    QString id;
    // Captured once at DirectInput acquisition. Readiness uses this exact HID
    // instance identity; it never infers a HidHide target from a friendly name.
    QString hidInstanceId;
    QString hidContainerId;
};

// A bounded control-plane proof for setup.  It opens exactly one persisted
// DirectInput controller, reads one state report, and immediately releases
// it.  It never starts mapping, selects an active Device Rig, or opens vJoy.
struct DirectInputControllerProbe {
    bool acquired = false;
    QString name;
    QString directInputId;
    QString hidInstanceId;
    QString hidContainerId;
    std::array<bool, kPhysicalAxisCount> axes{};
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
    QString diagnostic;
};

class MappingWorker final : public QThread {
    Q_OBJECT

public:
    explicit MappingWorker(MapperConfiguration configuration, QObject *parent = nullptr);
    ~MappingWorker() override;

    void updateConfiguration(const MapperConfiguration &configuration);
    void setMappingEnabled(bool enabled);
    bool mappingRequested() const;
    // A setup transaction uses this bounded control-plane handoff before
    // touching vJoy. It is never called from a report and it never changes a
    // mapping profile.
    bool prepareForDriverConfiguration(int timeoutMs = 1500);
    // Restores exactly the Mapping On/Off choice that preceded a deliberate
    // verification or repair operation. This is control-plane work only.
    bool restoreAfterDriverConfiguration(bool mappingWasRequested, int timeoutMs = 1500);
    // Forces a fresh DirectInput acquisition and waits only on the control
    // plane for the exact expected HID instance plus a newly received report.
    // It is never called by the report path.
    bool reacquirePhysicalController(const QString &expectedHidInstanceId, int timeoutMs = 3500);
    // Changes the already-persisted preferred DirectInput device at a safe
    // acquisition boundary.  It clears old input state before discovery and
    // never performs device enumeration from a report callback.
    bool selectPhysicalController(const QString &expectedDirectInputId, int timeoutMs = 3500);
    // Setup can inspect an editing Rig without activating it.  This direct
    // proof is deliberately independent from the long-lived mapping runtime.
    static DirectInputControllerProbe probeExactPhysicalController(const QString &expectedDirectInputId);
    void requestPhysicalControllerSelection() { m_reacquireInputRequested.fetch_add(1); }
    void requestStop();
    const AtomicRuntimeState &runtime() const { return m_runtime; }
#ifdef HOTAS_STARTUP_TESTING
    // Startup-test-only mutation of the bounded published UI snapshot. The
    // production worker exposes only the const observer above.
    AtomicRuntimeState &runtimeForTest() { return m_runtime; }
#endif
    // Deterministic test seam for the UI-side live-input contract. It writes
    // the same fixed latest-snapshot atomics DirectInput normally publishes;
    // it never enters the mapper report path or opens vJoy.
    void publishPhysicalAxisSnapshotForTest(int physicalAxis, float normalized);
    // Test-only descriptor fixture for route-editor coverage. This never
    // starts vJoy, changes a device, or runs from the report hot path.
    void publishVirtualAxisAvailabilityForTest(bool available);
    DeviceSnapshot deviceSnapshot() const;
    QString vjoyStatus() const;
    MappingLatencyPercentiles latencyPercentiles() const;
    std::shared_ptr<const RuntimeProfileCache> runtimeProfileCache() const;

signals:
    void workerEvent(const QString &message);
    void hardwareStateChanged();
    void buttonConfigurationSuggested(int physicalButtonCount, int vjoyButtonCapacity);

protected:
    void run() override;

private:
    void runSingleDevice(IDirectInput8W *directInput);
    void runDeviceRig(IDirectInput8W *directInput);
    MapperConfiguration configurationCopy();
    std::pair<MapperConfiguration, std::shared_ptr<const RuntimeProfileCache>>
    preparedConfigurationCopy();
    void setDeviceSnapshot(const DeviceSnapshot &snapshot);
    void setVjoyStatus(const QString &status);

    AtomicRuntimeState m_runtime;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_mappingRequested{false};
    std::atomic_bool m_releaseVjoyRequested{false};
    std::atomic_bool m_vjoyReleasedForControlPlane{false};
    // This fixture is only written through the explicit UI-test seam below.
    // A normal process keeps -1 and publishes the real vJoy descriptor.
    std::atomic_int m_testVirtualAxisAvailability{-1};
    std::atomic_uint64_t m_reacquireInputRequested{0};
    std::atomic_uint64_t m_reacquireInputAcknowledged{0};
    std::atomic_uint64_t m_configurationVersion{0};
    std::atomic_bool m_runtimeTopologyChangeRequested{false};
    mutable QMutex m_configurationMutex;
    MapperConfiguration m_configuration;
    // Built by the caller before it acquires the configuration mutex. The
    // worker only swaps this fully prepared immutable table between reports.
    std::shared_ptr<const RuntimeProfileCache> m_preparedProfileCache;
    mutable QMutex m_deviceMutex;
    DeviceSnapshot m_device;
    mutable QMutex m_statusMutex;
    QString m_vjoyStatus = u"Not checked"_qs;
};

} // namespace hotas
