#pragma once

#include "mapping_types.h"
#include "vjoy_ownership.h"

#include <QMutex>
#include <QThread>
#include <QVariantMap>

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

// Latest adaptive axis values for one physical input. The mapper overwrites
// these atomics; the UI samples them independently at display cadence.
struct AtomicAdaptiveTelemetry {
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
    // Publication is written last, after the scalar fields for this axis.
    // Consumers coalesce to the latest snapshot instead of queuing reports.
    std::array<std::atomic_uint64_t, kPhysicalAxisCount> adaptivePublicationSequence{};
    std::array<std::atomic_uint64_t, kPhysicalAxisCount> adaptivePublishedAtUs{};
    std::atomic_bool physicalConnected{false};
};

// Raw DirectInput candidate channels are captured only while the Ultra Nerd
// monitor is open.  This is a latest-snapshot diagnostic instrument: the
// mapper never queues samples, formats values, or notifies QML from a report.
struct AtomicAxisSourceTelemetry {
    std::array<std::atomic_int, kPhysicalAxisCount> value{};
    std::array<std::atomic_int, kPhysicalAxisCount> observedMinimum{};
    std::array<std::atomic_int, kPhysicalAxisCount> observedMaximum{};
    std::array<std::atomic_uint64_t, kPhysicalAxisCount> changeCount{};
    std::array<std::atomic_int, kPhysicalAxisCount> recentMovementMagnitude{};
    std::array<std::atomic_int64_t, kPhysicalAxisCount> lastChangeAgeMs{};
    std::atomic_bool available{false};
};

struct AtomicRuntimeState : AtomicAdaptiveTelemetry {
    // Selected Device reads this exact member snapshot in a Device Rig; it
    // never repurposes the aggregate (member-zero) presentation state.
    std::array<AtomicAdaptiveTelemetry, kMaximumDeviceRigMembers> deviceRigMemberAdaptive{};
    // These fixed-size snapshots carry the non-axis physical inputs for each
    // Rig member.  They are written by the mapping thread and sampled by the
    // UI timer, so choosing a controller never borrows member zero's buttons,
    // POVs, or connection state.
    std::array<std::atomic_bool, kMaximumDeviceRigMembers> deviceRigMemberPhysicalConnected{};
    std::array<std::array<std::atomic_bool, kMaximumPhysicalButtons>,
               kMaximumDeviceRigMembers> deviceRigMemberPhysicalButtonPressed{};
    std::array<std::array<std::atomic_int, kMaximumPhysicalPovs>,
               kMaximumDeviceRigMembers> deviceRigMemberPovValues{};
    // Per-member acquisition evidence follows the same fixed primitive rule
    // as adaptive telemetry so the Selected Device view never borrows
    // another Rig member's source or movement state.
    std::array<std::array<std::atomic_int, kPhysicalAxisCount>,
               kMaximumDeviceRigMembers> deviceRigMemberAxisAcquisitionSource{};
    // Method 3 is a handoff to the control plane.  This carries the unique
    // DIJOYSTATE2 field proven by a fresh buffered object event; it is not the
    // object's reported dwOfs, which may be contradictory metadata.
    std::array<std::array<std::atomic_int, kPhysicalAxisCount>,
               kMaximumDeviceRigMembers> deviceRigMemberAxisResolvedFormattedSource{};
    std::array<std::array<std::atomic_bool, kPhysicalAxisCount>,
               kMaximumDeviceRigMembers> deviceRigMemberAxisLiveMovementObserved{};
    std::array<std::array<std::atomic_int64_t, kPhysicalAxisCount>,
               kMaximumDeviceRigMembers> deviceRigMemberAxisLastMovementAgeMs{};
    std::array<AtomicAxisSourceTelemetry, kMaximumDeviceRigMembers>
        deviceRigMemberAxisSourceTelemetry{};
    std::array<std::atomic<float>, kPhysicalAxisCount> virtualValues{};
    std::array<std::atomic_bool, kVirtualAxisSlotCount> virtualAxisAvailable{};
    std::array<std::atomic<bool>, kPhysicalAxisCount> axisAvailable{};
    // Runtime-only axis acquisition evidence. These fixed primitives are
    // written by the mapper and sampled by the UI; no descriptor lookup,
    // QString, or allocation enters the DirectInput report path.
    std::array<std::atomic_int, kPhysicalAxisCount> axisAcquisitionSource{};
    std::array<std::atomic_int, kPhysicalAxisCount> axisResolvedFormattedSource{};
    std::array<std::atomic_bool, kPhysicalAxisCount> axisLiveMovementObserved{};
    std::array<std::atomic_int64_t, kPhysicalAxisCount> axisLastMovementAgeMs{};
    AtomicAxisSourceTelemetry axisSourceTelemetry{};
    // The UI/control plane enables this opt-in diagnostic capture. With the
    // monitor closed the report loop performs only this one relaxed branch.
    std::atomic_bool axisSourceMonitorRequested{false};
    std::array<std::atomic_int, kPhysicalAxisCount> axisActivity{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMinimum{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationCenter{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMaximum{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> physicalButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> virtualButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> buttonAvailable{};
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
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> axisDescriptors{};
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
    int unsupportedAxisCount = 0;
    QString diagnostic;
};

// A bounded, read-only characterization capture.  It never starts mapping,
// opens vJoy, persists configuration, or requests a device report range.
// The caller may run it alongside the normal non-exclusive mapper solely to
// establish which channel a physical object actually drives.
struct DirectInputAxisAcquisitionProbe {
    bool acquired = false;
    QString name;
    QString directInputId;
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> axisDescriptors{};
    std::array<qint32, kPhysicalAxisCount> standardMinimum{};
    std::array<qint32, kPhysicalAxisCount> standardMaximum{};
    // These sample every fixed c_dfDIJoystick2 field, not merely the field
    // named by an enumerated object.  They make descriptor/data-channel
    // disagreement visible in a real-hardware capture.
    std::array<qint32, kPhysicalAxisCount> stateFieldMinimum{};
    std::array<qint32, kPhysicalAxisCount> stateFieldMaximum{};
    std::array<qint32, kPhysicalAxisCount> bufferedMinimum{};
    std::array<qint32, kPhysicalAxisCount> bufferedMaximum{};
    std::array<quint64, kPhysicalAxisCount> standardChanges{};
    std::array<quint64, kPhysicalAxisCount> stateFieldChanges{};
    std::array<quint64, kPhysicalAxisCount> bufferedEvents{};
    qint32 bufferedConfigureResult = -1;
    qint32 lastBufferedReadResult = -1;
    int durationMs = 0;
    QString diagnostic;
};

class MappingWorker final : public QThread {
    Q_OBJECT

public:
    explicit MappingWorker(MapperConfiguration configuration, QObject *parent = nullptr);
    ~MappingWorker() override;

    void updateConfiguration(const MapperConfiguration &configuration);
    void setMappingEnabled(bool enabled);
    // Requests an immediate control-plane output availability pass. The
    // worker still owns every vJoy acquire; this merely bypasses its bounded
    // one-second polling cadence after a user says an external owner released
    // a device. It never runs from DirectInput report handling.
    void requestOutputAvailabilityRetry();
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
    static DirectInputAxisAcquisitionProbe captureExactPhysicalAxisAcquisition(
        const QString &expectedDirectInputId, int durationMs = 15000);
    void requestPhysicalControllerSelection() { m_reacquireInputRequested.fetch_add(1); }
    void setAxisSourceMonitorRequested(bool requested) {
        m_runtime.axisSourceMonitorRequested.store(requested, std::memory_order_relaxed);
    }
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
    // The explicit Axis Acquisition preview uses the same bounded snapshot
    // shape, but is driven only by the opt-in isolated presentation route.
    // It cannot acquire a device, invoke vJoy, or enter the report loop.
    void publishAxisAcquisitionPreviewSnapshot(int source, qint32 value,
                                                qint32 observedMinimum, qint32 observedMaximum,
                                                quint64 changeCount, int movementMagnitude,
                                                qint64 lastChangeAgeMs = 0);
    // Test-only descriptor fixture for route-editor coverage. This never
    // starts vJoy, changes a device, or runs from the report hot path.
    void publishVirtualAxisAvailabilityForTest(bool available);
    DeviceSnapshot deviceSnapshot() const;
    QString vjoyStatus() const;
    QVariantMap vjoyOwnershipTelemetry() const;
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
    void setVjoyOwnershipEvidence(const VJoyOwnershipEvidence &evidence, const QString &acquireAttempt);

    AtomicRuntimeState m_runtime;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_mappingRequested{false};
    std::atomic_uint64_t m_outputAvailabilityRetryGeneration{0};
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
    VJoyOwnershipEvidence m_vjoyOwnership;
    QString m_vjoyAcquireAttempt = u"No acquisition attempt yet"_qs;
    QString m_vjoyLastStatusTransition = u"Not observed"_qs;
};

} // namespace hotas
