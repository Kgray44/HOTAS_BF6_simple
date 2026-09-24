#include "mapping_worker.h"

#include "interactive_scheduling_policy.h"
#include "crash_diagnostics.h"
#include "direct_input_axis.h"
#include "device_rig.h"
#include "hid_device_identity.h"

#include "adaptive_response.h"
#include "axis_transform.h"
#include "axis_mapping_transition.h"
#include "automation_engine.h"
#include "button_mapping.h"
#include "physical_input_monitor.h"
#include "profile_trigger_runtime.h"
#include "vjoy_ownership.h"

#include <dinput.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

constexpr DWORD kVjoyUsageX = 0x30;
constexpr DWORD kVjoyUsageY = 0x31;
constexpr DWORD kVjoyUsageZ = 0x32;
constexpr DWORD kVjoyUsageRx = 0x33;
constexpr DWORD kVjoyUsageRy = 0x34;
constexpr DWORD kVjoyUsageRz = 0x35;
constexpr DWORD kVjoyUsageSlider0 = 0x36;
constexpr DWORD kVjoyUsageSlider1 = 0x37;
constexpr LONG kVjoyMinimum = 0;
constexpr LONG kVjoyMaximum = 32767;
constexpr DWORD kVjoyPovCentered = 0xFFFFFFFFUL;
// VjdStat from vJoyInterface.h: OWN = 0, FREE = 1, BUSY = 2,
// MISSING = 3, UNKNOWN = 4. Keep these values explicit because the DLL is
// loaded dynamically and its enum is not available at compile time.
constexpr int kVjoyStatusOwn = 0;
constexpr int kVjoyStatusFree = 1;
constexpr int kVjoyStatusBusy = 2;
constexpr int kVjoyStatusMissing = 3;
constexpr int kVjoyStatusUnknown = 4;
constexpr DWORD kPhysicalPollIntervalMs = 4; // 250 Hz bounded worker cadence.
constexpr float kMeaningfulInputAxisDelta = 0.02F;

struct MeaningfulInputEvidence {
    std::array<float, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    std::array<int, kMaximumPhysicalPovs> povs{};
    bool initialized = false;

    MeaningfulInputEvidence() { povs.fill(-1); }
};

bool observeMeaningfulInput(MeaningfulInputEvidence &evidence,
                            const PhysicalInputSnapshot &snapshot,
                            const std::array<bool, kPhysicalAxisCount> &availableAxes,
                            const std::array<bool, kMaximumPhysicalButtons> &availableButtons,
                            int povCount)
{
    if (!evidence.initialized) {
        evidence.axes = snapshot.axes;
        evidence.buttons = snapshot.buttons;
        evidence.povs = snapshot.povs;
        evidence.initialized = true;
        return false;
    }
    bool changed = false;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const size_t index = static_cast<size_t>(axis);
        if (!availableAxes[index]) continue;
        if (std::abs(snapshot.axes[index] - evidence.axes[index]) >= kMeaningfulInputAxisDelta) {
            evidence.axes[index] = snapshot.axes[index];
            changed = true;
        }
    }
    for (int button = 0; button < kMaximumPhysicalButtons; ++button) {
        const size_t index = static_cast<size_t>(button);
        if (!availableButtons[index]) continue;
        if (snapshot.buttons[index] != evidence.buttons[index]) {
            evidence.buttons[index] = snapshot.buttons[index];
            changed = true;
        }
    }
    for (int pov = 0; pov < std::min(povCount, kMaximumPhysicalPovs); ++pov) {
        const size_t index = static_cast<size_t>(pov);
        if (snapshot.povs[index] != evidence.povs[index]) {
            evidence.povs[index] = snapshot.povs[index];
            changed = true;
        }
    }
    return changed;
}

bool sameAdaptiveResponseOverlay(const RuntimeAdaptiveResponseOverride &left,
                                 const RuntimeAdaptiveResponseOverride &right)
{
    if (left.active != right.active || left.properties != right.properties) return false;
    if (!left.active && !right.active) return true;
    const AdaptiveResponseSettings &a = left.settings;
    const AdaptiveResponseSettings &b = right.settings;
    return a.enabled == b.enabled && a.model == b.model
        && a.maximumHorizonMs == b.maximumHorizonMs && a.maximumLead == b.maximumLead
        && a.velocityResponse == b.velocityResponse && a.accelerationResponse == b.accelerationResponse
        && a.motionSensitivity == b.motionSensitivity && a.noiseRejection == b.noiseRejection
        && a.reversalDetection == b.reversalDetection && a.reversalResponse == b.reversalResponse
        && a.decelerationResponse == b.decelerationResponse && a.settlingResponse == b.settlingResponse
        && a.endpointTaper == b.endpointTaper && a.onsetAssist == b.onsetAssist
        && a.onsetCap == b.onsetCap && a.sustainedAssist == b.sustainedAssist
        && a.sustainedCap == b.sustainedCap && a.horizonExtension == b.horizonExtension
        && a.horizonExtensionCapMs == b.horizonExtensionCapMs
        && a.turningPointProtection == b.turningPointProtection
        && a.turningPointMargin == b.turningPointMargin;
}

// Publish the full latest snapshot for one physical source. This remains a
// bounded atomics-only write in the DirectInput worker; presentation samples
// it later and therefore cannot make a backlog in the mapper hot path.
void publishAdaptiveAxisTelemetry(AtomicAdaptiveTelemetry &target, int axis, float raw,
                                  float resolved, const AdaptiveMappedAxisOutput &mapped,
                                  const AdaptiveResponseTelemetry &adaptive,
                                  const RuntimeAdaptiveResponseConfig &configuration,
                                  quint64 reversalCount, quint64 safetyClampCount,
                                  const RuntimeAdaptiveResponseOverride &overlay)
{
    const size_t index = static_cast<size_t>(axis);
    target.physicalConnected.store(true, std::memory_order_relaxed);
    target.raw[index] = raw;
    target.normalized[index] = resolved;
    target.afterDeadzone[index] = mapped.baselineSignalPath.afterDeadzone;
    target.afterHysteresis[index] = mapped.baselineSignalPath.afterHysteresis;
    target.afterInversion[index] = mapped.baselineSignalPath.afterInversion;
    target.curveResponse[index] = mapped.baselineSignalPath.afterCurve;
    target.transformed[index] = mapped.adaptiveOutput;
    target.adaptiveEstimated[index] = adaptive.estimated;
    target.adaptivePredicted[index] = adaptive.predicted;
    target.adaptiveBaselineMapped[index] = mapped.baselineOutput;
    target.adaptivePredictedMapped[index] = mapped.predictedMappedOutput;
    target.adaptiveOutput[index] = mapped.adaptiveOutput;
    target.adaptiveMappedLead[index] = mapped.mappedLead;
    target.adaptiveAppliedLead[index] = mapped.appliedLead;
    target.adaptiveLocalCurveGain[index] = mapped.localCurveGain;
    target.adaptiveVelocity[index] = adaptive.velocity;
    target.adaptiveAcceleration[index] = adaptive.acceleration;
    target.adaptiveHorizonSeconds[index] = adaptive.activeHorizonSeconds;
    target.adaptiveRequestedLead[index] = adaptive.requestedLead;
    target.adaptiveCappedLead[index] = adaptive.cappedLead;
    target.adaptiveEndpointTaper[index] = adaptive.endpointTaper;
    target.adaptiveLead[index] = adaptive.lead;
    target.adaptiveConfidence[index] = adaptive.confidence;
    target.adaptiveMotionIntensity[index] = adaptive.motionIntensity;
    target.adaptiveVelocityAuthority[index] = adaptive.velocityAuthority;
    target.adaptiveDeliberateMotionEvidence[index] = adaptive.deliberateMotionEvidence;
    target.adaptiveNormalMotionAuthority[index] = adaptive.normalMotionAuthority;
    target.adaptiveRapidMotionAuthority[index] = adaptive.rapidMotionAuthority;
    target.adaptiveRapidMotionBlend[index] = adaptive.rapidMotionBlend;
    target.adaptiveAccelerationIntent[index] = adaptive.accelerationIntent;
    target.adaptiveOnsetAuthority[index] = adaptive.onsetAuthority;
    target.adaptiveSustainedEvidence[index] = adaptive.sustainedEvidence;
    target.adaptiveSustainedAuthority[index] = adaptive.sustainedAuthority;
    target.adaptiveMotionUrgency[index] = adaptive.motionUrgency;
    target.adaptiveHorizonExtensionEligibility[index] = adaptive.horizonExtensionEligibility;
    target.adaptiveNormalMaximumHorizonSeconds[index] = adaptive.normalMaximumHorizonSeconds;
    target.adaptiveAllowedMaximumHorizonSeconds[index] = adaptive.allowedMaximumHorizonSeconds;
    target.adaptiveTurningPointConfidence[index] = adaptive.turningPointConfidence;
    target.adaptiveEstimatedTimeToTurnSeconds[index] = adaptive.estimatedTimeToTurnSeconds;
    target.adaptiveEstimatedRemainingTravel[index] = adaptive.estimatedRemainingTravel;
    target.adaptiveTurningPointHorizonLimitSeconds[index] = adaptive.turningPointHorizonLimitSeconds;
    target.adaptiveTurningPointLeadLimit[index] = adaptive.turningPointLeadLimit;
    target.adaptiveReacquisitionAuthority[index] = adaptive.reacquisitionAuthority;
    target.adaptiveMotionState[index] = static_cast<int>(adaptive.state);
    target.adaptiveReversing[index] = adaptive.reversal;
    target.adaptiveSafetyLimited[index] = adaptive.safetyLimited || mapped.leadLimited;
    target.adaptiveDeadzoneAuthorityBlocked[index] = mapped.deadzoneAuthorityBlocked;
    target.adaptiveLeadLimited[index] = mapped.leadLimited;
    target.adaptiveHighLocalCurveGain[index] = mapped.highLocalCurveGain;
    target.adaptiveReversalCount[index] = reversalCount;
    target.adaptiveSafetyClampCount[index] = safetyClampCount;
    target.adaptiveRuntimeEnabled[index] = configuration.enabled;
    target.adaptiveRuntimeModel[index] = static_cast<int>(configuration.model);
    target.adaptiveRuntimeMaximumHorizonSeconds[index] = configuration.maximumHorizonSeconds;
    target.adaptiveRuntimeMaximumLead[index] = configuration.maximumLead;
    target.adaptiveRuntimeVelocityResponse[index] = configuration.velocityResponse;
    target.adaptiveRuntimeAccelerationResponse[index] = configuration.accelerationResponse;
    target.adaptiveRuntimeMotionSensitivity[index] = configuration.motionSensitivity;
    target.adaptiveRuntimeNoiseRejection[index] = configuration.noiseRejection;
    target.adaptiveRuntimeReversalDetection[index] = configuration.reversalDetection;
    target.adaptiveRuntimeReversalResponse[index] = configuration.reversalResponse;
    target.adaptiveRuntimeDecelerationResponse[index] = configuration.decelerationResponse;
    target.adaptiveRuntimeSettlingResponse[index] = configuration.settlingResponse;
    target.adaptiveRuntimeEndpointTaper[index] = configuration.endpointTaper;
    target.adaptiveRuntimeOnsetAssist[index] = configuration.onsetAssist;
    target.adaptiveRuntimeOnsetCap[index] = configuration.onsetCap;
    target.adaptiveRuntimeSustainedAssist[index] = configuration.sustainedAssist;
    target.adaptiveRuntimeSustainedCap[index] = configuration.sustainedCap;
    target.adaptiveRuntimeHorizonExtension[index] = configuration.horizonExtension;
    target.adaptiveRuntimeHorizonExtensionCapSeconds[index] = configuration.horizonExtensionCapSeconds;
    target.adaptiveRuntimeTurningPointProtection[index] = configuration.turningPointProtection;
    target.adaptiveRuntimeTurningPointMargin[index] = configuration.turningPointMargin;
    target.adaptiveRuntimeNormalMovementResponse[index] = configuration.normalMovementResponse;
    target.adaptiveRuntimeRapidMovementResponse[index] = configuration.rapidMovementResponse;
    target.adaptiveRuntimeEngagementSensitivity[index] = configuration.engagementSensitivity;
    target.adaptiveAutomationOverlayActive[index] = overlay.active;
    target.adaptiveAutomationOverlayProperties[index] = overlay.properties;
    const auto now = std::chrono::steady_clock::now();
    target.adaptivePublishedAtUs[index].store(static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()),
        std::memory_order_relaxed);
    target.adaptivePublicationSequence[index].fetch_add(1, std::memory_order_release);
}

int buttonIndexForOffset(DWORD offset)
{
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        if (offset == DIJOFS_BUTTON(index)) return index;
    }
    return -1;
}

float normalizedFromDirectInput(LONG value)
{
    return std::clamp(static_cast<float>(value) / 10000.0F, -1.0F, 1.0F);
}

// Read only the DirectInput event records that Windows has already queued for
// this acquired device. The caller owns fixed-size storage; this performs no
// identity lookup, allocation, or GUI notification in the report loop.
bool readBufferedAxisEvents(LPDIRECTINPUTDEVICE8W device,
                            const std::array<NativeAxisDescriptor, kPhysicalAxisCount> &descriptors,
                            const std::array<bool, kPhysicalAxisCount> &available,
                            std::array<LONG, kPhysicalAxisCount> *values,
                            std::array<bool, kPhysicalAxisCount> *known,
                            std::array<bool, kPhysicalAxisCount> *changed)
{
    if (!device || !values || !known || !changed) return false;
    changed->fill(false);
    std::array<DIDEVICEOBJECTDATA, 32> events{};
    DWORD count = static_cast<DWORD>(events.size());
    const HRESULT result = device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), events.data(), &count, 0);
    if (FAILED(result) || count == 0) return false;
    bool axisEvent = false;
    for (DWORD eventIndex = 0; eventIndex < count; ++eventIndex) {
        // Match the buffered object against the object descriptor itself,
        // never by treating its dwOfs as the canonical logical axis. A
        // GUID_RzAxis / DIJOFS_Z device must still keep its Rz identity.
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (!available[static_cast<size_t>(axis)]
                || descriptors[static_cast<size_t>(axis)].directInputOffset != events[eventIndex].dwOfs) {
                continue;
            }
            (*values)[static_cast<size_t>(axis)] = static_cast<LONG>(events[eventIndex].dwData);
            (*known)[static_cast<size_t>(axis)] = true;
            (*changed)[static_cast<size_t>(axis)] = true;
            axisEvent = true;
            break;
        }
    }
    return axisEvent;
}

DWORD vjoyUsage(VirtualAxis axis)
{
    switch (axis) {
    case VirtualAxis::X: return kVjoyUsageX;
    case VirtualAxis::Y: return kVjoyUsageY;
    case VirtualAxis::Z: return kVjoyUsageZ;
    case VirtualAxis::Rx: return kVjoyUsageRx;
    case VirtualAxis::Ry: return kVjoyUsageRy;
    case VirtualAxis::Rz: return kVjoyUsageRz;
    case VirtualAxis::Slider0: return kVjoyUsageSlider0;
    case VirtualAxis::Slider1: return kVjoyUsageSlider1;
    case VirtualAxis::Disabled: return 0;
    }
    return 0;
}

LONG vjoyValue(float value)
{
    const float mapped = (std::clamp(value, -1.0F, 1.0F) + 1.0F) * 0.5F;
    return static_cast<LONG>(std::lround(mapped * kVjoyMaximum));
}

QString guidToString(const GUID &guid)
{
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return QString::fromWCharArray(text);
}

QString inputErrorMessage(HRESULT result)
{
    return QString(u"DirectInput error 0x%1"_qs)
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

class VJoyAdapter final {
public:
    // The vJoy interface is process-global inside the vendor DLL, even though
    // individual virtual devices are acquired and relinquished by this
    // worker.  Releasing the module for every rig topology change allowed
    // callbacks/window state in the DLL to outlive its code mapping.  Keep
    // one worker-only interface loaded until process shutdown; adapters still
    // relinquish only their owned device when the output itself is retired.
    ~VJoyAdapter() { unload(); }

    bool checkDevice(int deviceId, QString *status)
    {
        if (!load(status)) {
            m_lastAcquireAttempt = QStringLiteral("Not attempted: vJoy interface could not be loaded");
            return false;
        }
        m_lastOwnership = ownershipEvidence(deviceId);
        if (m_lastOwnership.state == VJoyOwnershipState::BusyOtherProcess) {
            if (status) *status = QString(u"Device %1 is owned by %2"_qs)
                .arg(deviceId).arg(vjoyOwnershipOwnerLabel(m_lastOwnership));
            return false;
        }
        if (m_lastOwnership.state == VJoyOwnershipState::StaleOwnership) {
            if (status) *status = QString(u"Device %1 has STALE OWNERSHIP / DRIVER STATE: %2"_qs)
                .arg(deviceId).arg(m_lastOwnership.diagnostic);
            return false;
        }
        if (m_lastOwnership.state == VJoyOwnershipState::Missing
            || m_lastOwnership.state == VJoyOwnershipState::Unknown) {
            if (status) *status = QString(u"Device %1 is unavailable: %2"_qs)
                .arg(deviceId).arg(m_lastOwnership.diagnostic);
            return false;
        }
        const std::array<bool, kVirtualAxisSlotCount> axes = axisCapabilities(deviceId);
        const int axisCount = static_cast<int>(std::count(axes.begin() + 1, axes.end(), true));
        if (axisCount == 0) {
            if (status) *status = QString(u"Device %1 exposes no usable vJoy axes"_qs).arg(deviceId);
            return false;
        }
        const int buttons = buttonCapacity(deviceId, nullptr);
        const PovCapabilities povs = povCapabilities(deviceId, nullptr);
        if (status) {
            *status = QString(m_lastOwnership.state == VJoyOwnershipState::OwnedByCurrentProcess
                ? u"Device %1 CONFIGURED · ACQUIRED · OWNED BY HOTAS BF6 · %2 axes · %3 buttons · %4 continuous / %5 discrete POV"_qs
                : u"Device %1 Ready · %2 axes · %3 buttons · %4 continuous / %5 discrete POV"_qs)
                .arg(deviceId).arg(axisCount).arg(buttons).arg(povs.continuous).arg(povs.discrete);
        }
        return true;
    }

    bool acquire(int deviceId, QString *status)
    {
        if (!checkDevice(deviceId, status)) {
            m_lastAcquireAttempt = QStringLiteral("Not attempted: %1").arg(m_lastOwnership.diagnostic);
            return false;
        }
        if (m_acquired && m_deviceId == deviceId) {
            m_lastAcquireAttempt = QStringLiteral("Already owned by this HOTAS BF6 worker");
            return true;
        }
        if (vjoyAcquireActionFor(m_lastOwnership) == VJoyAcquireAction::AlreadyOwned) {
            m_acquired = true;
            m_deviceId = deviceId;
            m_lastAcquireAttempt = QStringLiteral("Already owned by this HOTAS BF6 process; AcquireVJD was not called");
            return true;
        }
        if (vjoyAcquireActionFor(m_lastOwnership) != VJoyAcquireAction::Acquire) {
            m_lastAcquireAttempt = QStringLiteral("Not attempted: %1").arg(m_lastOwnership.diagnostic);
            return false;
        }
        release();
        if (!m_acquire(static_cast<UINT>(deviceId))) {
            m_lastAcquireAttempt = QStringLiteral("AcquireVJD returned failure");
            if (status) *status = QString(u"Could not acquire vJoy device %1"_qs).arg(deviceId);
            return false;
        }
        // The post-acquire query is mandatory.  vJoy may report raw BUSY to a
        // second interface in this process, so GetOwnerPid is authoritative.
        m_lastOwnership = ownershipEvidence(deviceId);
        if (m_lastOwnership.state != VJoyOwnershipState::OwnedByCurrentProcess) {
            m_relinquish(static_cast<UINT>(deviceId));
            m_lastAcquireAttempt = QStringLiteral("AcquireVJD returned success, but post-acquire ownership was %1")
                .arg(vjoyOwnershipStateName(m_lastOwnership.state));
            if (status) *status = QString(u"Device %1 did not become owned by this HOTAS BF6 process: %2"_qs)
                .arg(deviceId).arg(m_lastOwnership.diagnostic);
            return false;
        }
        m_acquired = true;
        m_deviceId = deviceId;
        m_lastAcquireAttempt = QStringLiteral("AcquireVJD succeeded; post-acquire ownership confirmed");
        if (status) {
            const std::array<bool, kVirtualAxisSlotCount> axes = axisCapabilities(deviceId);
            const int axisCount = static_cast<int>(std::count(axes.begin() + 1, axes.end(), true));
            const PovCapabilities povs = povCapabilities(deviceId, nullptr);
            *status = QString(u"Device %1 CONFIGURED · ACQUIRED · OWNED BY HOTAS BF6 · %2 axes · %3 buttons · %4 continuous / %5 discrete POV"_qs)
                .arg(deviceId).arg(axisCount).arg(buttonCapacity(deviceId, nullptr))
                .arg(povs.continuous).arg(povs.discrete);
        }
        return true;
    }

    const VJoyOwnershipEvidence &lastOwnership() const { return m_lastOwnership; }
    const QString &lastAcquireAttempt() const { return m_lastAcquireAttempt; }

    bool setAxis(VirtualAxis axis, float value)
    {
        if (!m_acquired || axis == VirtualAxis::Disabled) {
            return false;
        }
        return m_setAxis(vjoyValue(value), static_cast<UINT>(m_deviceId), vjoyUsage(axis));
    }

    std::array<bool, kVirtualAxisSlotCount> axisCapabilities(int deviceId, QString *status = nullptr)
    {
        std::array<bool, kVirtualAxisSlotCount> result{};
        if (!load(status)) return result;
        for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
            result[static_cast<size_t>(index)] = m_axisExists(static_cast<UINT>(deviceId),
                vjoyUsage(static_cast<VirtualAxis>(index))) != FALSE;
        }
        return result;
    }

    bool acquired() const { return m_acquired; }

    int buttonCapacity(int deviceId, QString *status)
    {
        if (!load(status) || !m_getButtonNumber) {
            return 0;
        }
        const int reported = m_getButtonNumber(static_cast<UINT>(deviceId));
        return std::clamp(reported, 0, kMaximumVirtualButtons);
    }

    struct PovCapabilities {
        int continuous = 0;
        int discrete = 0;
    };

    PovCapabilities povCapabilities(int deviceId, QString *status)
    {
        if (!load(status)) return {};
        PovCapabilities result;
        if (m_getContinuousPovNumber) {
            result.continuous = std::clamp(m_getContinuousPovNumber(static_cast<UINT>(deviceId)), 0, 32);
        }
        if (m_getDiscretePovNumber) {
            result.discrete = std::clamp(m_getDiscretePovNumber(static_cast<UINT>(deviceId)), 0, 32);
        }
        return result;
    }

    bool setPov(const NativePovBinding &binding, int physicalRawAngle)
    {
        if (!m_acquired || !binding.enabled || binding.targetIndex < 1) return false;
        if (binding.targetType == NativePovTargetType::Continuous
            && m_setContinuousPov) {
            const DWORD value = physicalRawAngle >= 0 && physicalRawAngle < 36000
                ? static_cast<DWORD>(physicalRawAngle) : kVjoyPovCentered;
            return m_setContinuousPov(value, static_cast<UINT>(m_deviceId),
                                      static_cast<UCHAR>(binding.targetIndex));
        }
        if (binding.targetType == NativePovTargetType::Discrete
            && m_setDiscretePov) {
            // A vJoy discrete POV is cardinal only. Diagonal physical angles
            // resolve clockwise at the 45-degree boundary: UR/DR -> Right,
            // DL -> Left, UL -> Up. Direction-to-button routes remain fully
            // eight-way and are not affected by this hardware fallback.
            const int value = physicalRawAngle >= 0 && physicalRawAngle < 36000
                ? ((physicalRawAngle + 4500) / 9000) % 4 : -1;
            return m_setDiscretePov(value, static_cast<UINT>(m_deviceId),
                                    static_cast<UCHAR>(binding.targetIndex));
        }
        return false;
    }

    bool centerContinuousPov(int index)
    {
        return m_acquired && m_setContinuousPov && index > 0
            && m_setContinuousPov(kVjoyPovCentered, static_cast<UINT>(m_deviceId),
                                  static_cast<UCHAR>(index));
    }

    bool centerDiscretePov(int index)
    {
        return m_acquired && m_setDiscretePov && index > 0
            && m_setDiscretePov(-1, static_cast<UINT>(m_deviceId), static_cast<UCHAR>(index));
    }

    bool setButton(int button, bool pressed)
    {
        if (!m_acquired || !m_setButton || button < 1 || button > kMaximumVirtualButtons) {
            return false;
        }
        return m_setButton(pressed ? TRUE : FALSE, static_cast<UINT>(m_deviceId),
                           static_cast<UCHAR>(button));
    }

    void release()
    {
        if (m_acquired && m_relinquish) {
            m_relinquish(static_cast<UINT>(m_deviceId));
        }
        m_acquired = false;
        m_deviceId = 0;
    }

private:
    using GetVJDStatusFn = int(__cdecl *)(UINT);
    using GetOwnerPidFn = DWORD(__cdecl *)(UINT);
    using GetVJDAxisExistFn = BOOL(__cdecl *)(UINT, UINT);
    using AcquireVJDFn = BOOL(__cdecl *)(UINT);
    using RelinquishVJDFn = void(__cdecl *)(UINT);
    using SetAxisFn = BOOL(__cdecl *)(LONG, UINT, UINT);
    using GetVJDButtonNumberFn = int(__cdecl *)(UINT);
    using SetBtnFn = BOOL(__cdecl *)(BOOL, UINT, UCHAR);
    using GetPovNumberFn = int(__cdecl *)(UINT);
    using SetContinuousPovFn = BOOL(__cdecl *)(DWORD, UINT, UCHAR);
    using SetDiscretePovFn = BOOL(__cdecl *)(int, UINT, UCHAR);

    struct PersistentInterface {
        HMODULE library = nullptr;
        GetVJDStatusFn getStatus = nullptr;
        GetOwnerPidFn getOwnerPid = nullptr;
        GetVJDAxisExistFn axisExists = nullptr;
        AcquireVJDFn acquire = nullptr;
        RelinquishVJDFn relinquish = nullptr;
        SetAxisFn setAxis = nullptr;
        GetVJDButtonNumberFn getButtonNumber = nullptr;
        SetBtnFn setButton = nullptr;
        GetPovNumberFn getContinuousPovNumber = nullptr;
        GetPovNumberFn getDiscretePovNumber = nullptr;
        SetContinuousPovFn setContinuousPov = nullptr;
        SetDiscretePovFn setDiscretePov = nullptr;
    };

    static PersistentInterface &persistentWorkerInterface()
    {
        // VJoyAdapter is instantiated only from MappingWorker's DirectInput
        // thread.  This function-local state therefore gives that worker one
        // DLL interface for its whole process lifetime without introducing
        // GUI-thread driver ownership or report-path synchronization.
        static PersistentInterface api;
        return api;
    }

    void attachPersistentInterface(const PersistentInterface &api)
    {
        m_library = api.library;
        m_getStatus = api.getStatus;
        m_getOwnerPid = api.getOwnerPid;
        m_axisExists = api.axisExists;
        m_acquire = api.acquire;
        m_relinquish = api.relinquish;
        m_setAxis = api.setAxis;
        m_getButtonNumber = api.getButtonNumber;
        m_setButton = api.setButton;
        m_getContinuousPovNumber = api.getContinuousPovNumber;
        m_getDiscretePovNumber = api.getDiscretePovNumber;
        m_setContinuousPov = api.setContinuousPov;
        m_setDiscretePov = api.setDiscretePov;
    }

    void detachInterface()
    {
        m_library = nullptr;
        m_getStatus = nullptr;
        m_getOwnerPid = nullptr;
        m_axisExists = nullptr;
        m_acquire = nullptr;
        m_relinquish = nullptr;
        m_setAxis = nullptr;
        m_getButtonNumber = nullptr;
        m_setButton = nullptr;
        m_getContinuousPovNumber = nullptr;
        m_getDiscretePovNumber = nullptr;
        m_setContinuousPov = nullptr;
        m_setDiscretePov = nullptr;
    }

    VJoyOwnershipEvidence ownershipEvidence(int deviceId) const
    {
        const int rawStatus = m_getStatus ? m_getStatus(static_cast<UINT>(deviceId)) : kVJoyStatusUnknown;
        const bool ownerPidAvailable = m_getOwnerPid != nullptr;
        const quint64 ownerPid = ownerPidAvailable
            ? static_cast<quint64>(m_getOwnerPid(static_cast<UINT>(deviceId))) : 0;
        const VJoyOwnerProcessEvidence process = ownerPidAvailable
            ? inspectVJoyOwnerProcess(ownerPid) : VJoyOwnerProcessEvidence{};
        return classifyVJoyOwnership(deviceId, rawStatus, ownerPidAvailable, ownerPid,
                                     static_cast<quint64>(GetCurrentProcessId()), process);
    }

    bool load(QString *status)
    {
        if (m_library) {
            return true;
        }
        PersistentInterface &persistent = persistentWorkerInterface();
        if (persistent.library) {
            attachPersistentInterface(persistent);
            return true;
        }
        QStringList candidates{u"vJoyInterface.dll"_qs};
        const QString programFiles = qEnvironmentVariable("ProgramW6432");
        const QString fallbackProgramFiles = qEnvironmentVariable("ProgramFiles");
        for (const QString &root : {programFiles, fallbackProgramFiles, u"C:/Program Files"_qs}) {
            if (!root.isEmpty()) candidates.append(root + u"/vJoy/x64/vJoyInterface.dll"_qs);
        }
        for (const QString &candidate : candidates) {
            const std::wstring nativePath = candidate.toStdWString();
            m_library = LoadLibraryW(nativePath.c_str());
            if (m_library) break;
        }
        if (!m_library) {
            if (status) *status = u"vJoyInterface.dll not found (install vJoy)"_qs;
            return false;
        }
        m_getStatus = reinterpret_cast<GetVJDStatusFn>(GetProcAddress(m_library, "GetVJDStatus"));
        m_getOwnerPid = reinterpret_cast<GetOwnerPidFn>(GetProcAddress(m_library, "GetOwnerPid"));
        m_axisExists = reinterpret_cast<GetVJDAxisExistFn>(GetProcAddress(m_library, "GetVJDAxisExist"));
        m_acquire = reinterpret_cast<AcquireVJDFn>(GetProcAddress(m_library, "AcquireVJD"));
        m_relinquish = reinterpret_cast<RelinquishVJDFn>(GetProcAddress(m_library, "RelinquishVJD"));
        m_setAxis = reinterpret_cast<SetAxisFn>(GetProcAddress(m_library, "SetAxis"));
        m_getButtonNumber = reinterpret_cast<GetVJDButtonNumberFn>(GetProcAddress(m_library, "GetVJDButtonNumber"));
        m_setButton = reinterpret_cast<SetBtnFn>(GetProcAddress(m_library, "SetBtn"));
        // POV APIs are optional because a valid vJoy configuration may expose
        // no hats. Their absence makes the target unavailable, never fatal.
        m_getContinuousPovNumber = reinterpret_cast<GetPovNumberFn>(GetProcAddress(m_library, "GetVJDContPovNumber"));
        m_getDiscretePovNumber = reinterpret_cast<GetPovNumberFn>(GetProcAddress(m_library, "GetVJDDiscPovNumber"));
        m_setContinuousPov = reinterpret_cast<SetContinuousPovFn>(GetProcAddress(m_library, "SetContPov"));
        m_setDiscretePov = reinterpret_cast<SetDiscretePovFn>(GetProcAddress(m_library, "SetDiscPov"));
        if (!m_getStatus || !m_axisExists || !m_acquire || !m_relinquish || !m_setAxis) {
            if (status) *status = u"vJoyInterface.dll is missing a required API"_qs;
            // No device was acquired yet, so it is safe to discard this
            // failed initial load.  Successful loads are intentionally kept
            // by persistentWorkerInterface() until the process exits.
            FreeLibrary(m_library);
            detachInterface();
            return false;
        }
        persistent.library = m_library;
        persistent.getStatus = m_getStatus;
        persistent.getOwnerPid = m_getOwnerPid;
        persistent.axisExists = m_axisExists;
        persistent.acquire = m_acquire;
        persistent.relinquish = m_relinquish;
        persistent.setAxis = m_setAxis;
        persistent.getButtonNumber = m_getButtonNumber;
        persistent.setButton = m_setButton;
        persistent.getContinuousPovNumber = m_getContinuousPovNumber;
        persistent.getDiscretePovNumber = m_getDiscretePovNumber;
        persistent.setContinuousPov = m_setContinuousPov;
        persistent.setDiscretePov = m_setDiscretePov;
        return true;
    }

    void unload()
    {
        release();
        // Do not call FreeLibrary here.  A topology transaction may destroy
        // this small adapter while the vendor interface still owns internal
        // window/callback state.  The persistent worker interface owns the
        // single module reference through orderly process shutdown.
        detachInterface();
    }

    HMODULE m_library = nullptr;
    GetVJDStatusFn m_getStatus = nullptr;
    GetOwnerPidFn m_getOwnerPid = nullptr;
    GetVJDAxisExistFn m_axisExists = nullptr;
    AcquireVJDFn m_acquire = nullptr;
    RelinquishVJDFn m_relinquish = nullptr;
    SetAxisFn m_setAxis = nullptr;
    GetVJDButtonNumberFn m_getButtonNumber = nullptr;
    SetBtnFn m_setButton = nullptr;
    GetPovNumberFn m_getContinuousPovNumber = nullptr;
    GetPovNumberFn m_getDiscretePovNumber = nullptr;
    SetContinuousPovFn m_setContinuousPov = nullptr;
    SetDiscretePovFn m_setDiscretePov = nullptr;
    VJoyOwnershipEvidence m_lastOwnership;
    QString m_lastAcquireAttempt = QStringLiteral("No acquisition attempt yet");
    bool m_acquired = false;
    int m_deviceId = 0;
};

struct DirectInputDevice {
    GUID guid{};
    QString name;
};

struct EnumerationContext {
    std::vector<DirectInputDevice> devices;
};

BOOL CALLBACK enumDeviceCallback(const DIDEVICEINSTANCEW *instance, VOID *context)
{
    auto *devices = static_cast<EnumerationContext *>(context);
    devices->devices.push_back({instance->guidInstance, QString::fromWCharArray(instance->tszProductName)});
    return DIENUM_CONTINUE;
}

struct ObjectEnumerationContext {
    LPDIRECTINPUTDEVICE8W device = nullptr;
    std::array<bool, kPhysicalAxisCount> *axes = nullptr;
    std::array<bool, kMaximumPhysicalButtons> *buttons = nullptr;
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> *axisDescriptors = nullptr;
    // Verification/probe enumeration must not alter a physical device. The
    // live mapper alone may request a DirectInput report range.
    bool configureAxisRanges = true;
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
    int unsupportedAxisCount = 0;
};

BOOL CALLBACK enumObjectCallback(const DIDEVICEOBJECTINSTANCEW *instance, VOID *context)
{
    auto *objects = static_cast<ObjectEnumerationContext *>(context);
    const DWORD objectType = DIDFT_GETTYPE(instance->dwType);
    if ((objectType & DIDFT_AXIS) != 0) {
        const int enumerationIndex = objects->axisCount;
        NativeAxisDescriptor discovered = describeDirectInputAxisObject(objects->device, *instance);
        const int index = objects->axisDescriptors
            ? resolveUniqueDirectInputAxisSlot(&discovered, objects->axisDescriptors, objects->axes)
            : discovered.canonicalAxis;
        if (index >= 0) {
            (*objects->axes)[index] = true;
            if (objects->axisDescriptors) {
                NativeAxisDescriptor &descriptor = (*objects->axisDescriptors)[static_cast<size_t>(index)];
                discovered.enumerationIndex = enumerationIndex;
                if (objects->configureAxisRanges) {
                    configureDirectInputAxisRange(objects->device, *instance, &discovered);
                } else {
                    discovered.acquisitionSourceResolved = discovered.present;
                }
                if (!descriptor.present
                    || (discovered.resolutionSource == AxisResolutionSource::StandardSemanticGuid
                        && descriptor.resolutionSource != AxisResolutionSource::StandardSemanticGuid)) {
                    descriptor = std::move(discovered);
                }
            } else {
                if (objects->configureAxisRanges) {
                    configureDirectInputAxisRange(objects->device, *instance);
                }
            }
        } else {
            ++objects->unsupportedAxisCount;
        }
        ++objects->axisCount;
    } else if ((objectType & DIDFT_BUTTON) != 0) {
        const int index = buttonIndexForOffset(instance->dwOfs);
        if (index >= 0) {
            (*objects->buttons)[static_cast<size_t>(index)] = true;
        }
        ++objects->buttonCount;
    } else if ((objectType & DIDFT_POV) != 0) {
        objects->povCount = std::min(objects->povCount + 1, kMaximumPhysicalPovs);
    }
    return DIENUM_CONTINUE;
}

std::optional<DirectInputDevice> selectDevice(LPDIRECTINPUT8W directInput, const MapperConfiguration &configuration)
{
    EnumerationContext context;
    directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumDeviceCallback, &context, DIEDFL_ATTACHEDONLY);
    if (context.devices.empty()) {
        return std::nullopt;
    }
    if (!configuration.preferredDeviceId.isEmpty()) {
        for (const auto &device : context.devices) {
            if (guidToString(device.guid) == configuration.preferredDeviceId
                && !isVirtualControllerName(device.name)) {
                return device;
            }
        }
    }
    if (!configuration.activeControllerRecordId.isEmpty()) {
        for (const SavedControllerRecord &record : configuration.savedControllers) {
            if (record.id != configuration.activeControllerRecordId) continue;
            for (const auto &device : context.devices) {
                if (guidToString(device.guid) == record.lastDirectInputId
                    && !isVirtualControllerName(device.name)) return device;
            }
        }
    }
    std::vector<DirectInputDevice> physical;
    for (const auto &device : context.devices) {
        if (!isVirtualControllerName(device.name)) physical.push_back(device);
    }
    // First-run auto selection is only safe when there is one candidate.  A
    // user must explicitly choose between multiple physical controllers.
    if (physical.size() == 1) return physical.front();
    // A mapper must never consume the vJoy controller it produces. Wait for a
    // real DirectInput device rather than creating a feedback loop.
    return std::nullopt;
}

std::optional<DirectInputDevice> selectDeviceByPersistedId(LPDIRECTINPUT8W directInput,
                                                           const QString &directInputId)
{
    if (directInputId.isEmpty()) return std::nullopt;
    EnumerationContext context;
    directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumDeviceCallback, &context, DIEDFL_ATTACHEDONLY);
    for (const DirectInputDevice &candidate : context.devices) {
        if (!isVirtualControllerName(candidate.name)
            && guidToString(candidate.guid).compare(directInputId, Qt::CaseInsensitive) == 0) {
            return candidate;
        }
    }
    return std::nullopt;
}

QString hidInstanceIdForDevice(LPDIRECTINPUTDEVICE8W device)
{
    if (!device) return {};
    DIPROPGUIDANDPATH property{};
    property.diph.dwSize = sizeof(property);
    property.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    property.diph.dwHow = DIPH_DEVICE;
    property.diph.dwObj = 0;
    if (FAILED(device->GetProperty(DIPROP_GUIDANDPATH, &property.diph))) return {};

    QString path = QString::fromWCharArray(property.wszPath);
    path.remove(QStringLiteral("\\\\?\\"), Qt::CaseInsensitive);
    const int classSeparator = path.indexOf(QStringLiteral("#{"));
    if (classSeparator >= 0) path.truncate(classSeparator);
    return path.replace(u'#', u'\\').toUpper();
}

} // namespace

MappingWorker::MappingWorker(MapperConfiguration configuration, QObject *parent)
    : QThread(parent), m_configuration(std::move(configuration))
{
    const auto resetAxisSourceTelemetry = [](AtomicAxisSourceTelemetry &snapshot) {
        snapshot.available.store(false, std::memory_order_relaxed);
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            snapshot.value[static_cast<size_t>(index)].store(0, std::memory_order_relaxed);
            snapshot.observedMinimum[static_cast<size_t>(index)].store(0, std::memory_order_relaxed);
            snapshot.observedMaximum[static_cast<size_t>(index)].store(0, std::memory_order_relaxed);
            snapshot.changeCount[static_cast<size_t>(index)].store(0, std::memory_order_relaxed);
            snapshot.recentMovementMagnitude[static_cast<size_t>(index)].store(0, std::memory_order_relaxed);
            snapshot.lastChangeAgeMs[static_cast<size_t>(index)].store(-1, std::memory_order_relaxed);
        }
    };
    resetAxisSourceTelemetry(m_runtime.axisSourceTelemetry);
    for (AtomicAxisSourceTelemetry &snapshot : m_runtime.deviceRigMemberAxisSourceTelemetry) {
        resetAxisSourceTelemetry(snapshot);
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        m_runtime.raw[index] = 0.0F;
        m_runtime.normalized[index] = 0.0F;
        m_runtime.afterDeadzone[index] = 0.0F;
        m_runtime.afterHysteresis[index] = 0.0F;
        m_runtime.afterInversion[index] = 0.0F;
        m_runtime.curveResponse[index] = 0.0F;
        m_runtime.transformed[index] = 0.0F;
        m_runtime.adaptiveEstimated[index] = 0.0F;
        m_runtime.adaptivePredicted[index] = 0.0F;
        m_runtime.adaptiveBaselineMapped[index] = 0.0F;
        m_runtime.adaptivePredictedMapped[index] = 0.0F;
        m_runtime.adaptiveOutput[index] = 0.0F;
        m_runtime.adaptiveMappedLead[index] = 0.0F;
        m_runtime.adaptiveAppliedLead[index] = 0.0F;
        m_runtime.adaptiveLocalCurveGain[index] = 0.0F;
        m_runtime.adaptiveVelocity[index] = 0.0F;
        m_runtime.adaptiveAcceleration[index] = 0.0F;
        m_runtime.adaptiveHorizonSeconds[index] = 0.0F;
        m_runtime.adaptiveRequestedLead[index] = 0.0F;
        m_runtime.adaptiveCappedLead[index] = 0.0F;
        m_runtime.adaptiveEndpointTaper[index] = 1.0F;
        m_runtime.adaptiveLead[index] = 0.0F;
        m_runtime.adaptiveConfidence[index] = 0.0F;
        m_runtime.adaptiveMotionIntensity[index] = 0.0F;
        m_runtime.adaptiveVelocityAuthority[index] = 0.0F;
        m_runtime.adaptiveDeliberateMotionEvidence[index] = 0.0F;
        m_runtime.adaptiveNormalMotionAuthority[index] = 0.0F;
        m_runtime.adaptiveRapidMotionAuthority[index] = 0.0F;
        m_runtime.adaptiveRapidMotionBlend[index] = 0.0F;
        m_runtime.adaptiveAccelerationIntent[index] = 0.0F;
        m_runtime.adaptiveOnsetAuthority[index] = 0.0F;
        m_runtime.adaptiveSustainedEvidence[index] = 0.0F;
        m_runtime.adaptiveSustainedAuthority[index] = 0.0F;
        m_runtime.adaptiveMotionUrgency[index] = 0.0F;
        m_runtime.adaptiveHorizonExtensionEligibility[index] = 0.0F;
        m_runtime.adaptiveNormalMaximumHorizonSeconds[index] = 0.0F;
        m_runtime.adaptiveAllowedMaximumHorizonSeconds[index] = 0.0F;
        m_runtime.adaptiveTurningPointConfidence[index] = 0.0F;
        m_runtime.adaptiveEstimatedTimeToTurnSeconds[index] = 0.0F;
        m_runtime.adaptiveEstimatedRemainingTravel[index] = 0.0F;
        m_runtime.adaptiveTurningPointHorizonLimitSeconds[index] = 0.0F;
        m_runtime.adaptiveTurningPointLeadLimit[index] = 0.0F;
        m_runtime.adaptiveReacquisitionAuthority[index] = 0.0F;
        m_runtime.adaptiveMotionState[index] = static_cast<int>(AdaptiveMotionState::Stable);
        m_runtime.adaptiveReversing[index] = false;
        m_runtime.adaptiveSafetyLimited[index] = false;
        m_runtime.adaptiveDeadzoneAuthorityBlocked[index] = false;
        m_runtime.adaptiveLeadLimited[index] = false;
        m_runtime.adaptiveHighLocalCurveGain[index] = false;
        m_runtime.adaptiveReversalCount[index] = 0;
        m_runtime.adaptiveSafetyClampCount[index] = 0;
        m_runtime.virtualValues[index] = std::numeric_limits<float>::quiet_NaN();
        m_runtime.axisAvailable[index] = false;
        m_runtime.axisAcquisitionSource[index] = -1;
        m_runtime.axisResolvedFormattedSource[index] = -1;
        m_runtime.axisLiveMovementObserved[index] = false;
        m_runtime.axisLastMovementAgeMs[index] = -1;
        m_runtime.axisActivity[index] = static_cast<int>(m_configuration.axisActivity[index]);
        m_runtime.calibrationMinimum[index] = m_configuration.calibration[index].minimum;
        m_runtime.calibrationCenter[index] = m_configuration.calibration[index].center;
        m_runtime.calibrationMaximum[index] = m_configuration.calibration[index].maximum;
    }
    for (std::atomic_bool &available : m_runtime.virtualAxisAvailable) available = false;
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        m_runtime.physicalButtonPressed[index] = false;
        m_runtime.virtualButtonPressed[index] = false;
        m_runtime.buttonAvailable[index] = false;
    }
    for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
    for (std::atomic_uint64_t &sample : m_runtime.latencySamples) sample = 0;
    const auto compileStarted = std::chrono::steady_clock::now();
    m_preparedProfileCache = std::make_shared<RuntimeProfileCache>(
        compileRuntimeProfileCache(m_configuration));
    m_runtime.effectiveProfileIndex = m_preparedProfileCache->baseProfileIndex;
    m_runtime.automationEngineEnabled = m_preparedProfileCache->automation
        && m_preparedProfileCache->automation->engineEnabled;
    m_runtime.automationRuleCount = m_preparedProfileCache->automation
        ? m_preparedProfileCache->automation->ruleCount : 0;
    m_runtime.lastCurveCompileUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - compileStarted).count());
}

MappingWorker::~MappingWorker()
{
    requestStop();
    // QThread must never reach its base destructor while run() still owns
    // DirectInput/vJoy state. The report loop wakes on a bounded interval.
    wait();
}

void MappingWorker::publishPhysicalAxisSnapshotForTest(int physicalAxis, float normalized)
{
    const int axis = std::clamp(physicalAxis, 0, kPhysicalAxisCount - 1);
    const float physical = std::clamp(normalized, -1.0F, 1.0F);
    const size_t index = static_cast<size_t>(axis);

    // This test-only injector mirrors the worker's latest snapshot contract.
    // It deliberately does not construct a processor, send a vJoy report, or
    // signal QML; the normal UI sampler remains responsible for presentation.
    m_runtime.physicalConnected.store(true, std::memory_order_relaxed);
    m_runtime.axisCount.store(std::max(m_runtime.axisCount.load(std::memory_order_relaxed), axis + 1),
                              std::memory_order_relaxed);
    m_runtime.axisAvailable[index].store(true, std::memory_order_relaxed);
    m_runtime.normalized[index].store(physical, std::memory_order_relaxed);
    m_runtime.afterDeadzone[index].store(physical, std::memory_order_relaxed);
    m_runtime.afterHysteresis[index].store(physical, std::memory_order_relaxed);
    m_runtime.afterInversion[index].store(physical, std::memory_order_relaxed);
    m_runtime.curveResponse[index].store(physical, std::memory_order_relaxed);
    m_runtime.transformed[index].store(physical, std::memory_order_relaxed);
    m_runtime.adaptiveEstimated[index].store(physical, std::memory_order_relaxed);
    m_runtime.adaptivePredicted[index].store(physical, std::memory_order_relaxed);
    m_runtime.adaptiveBaselineMapped[index].store(physical, std::memory_order_relaxed);
    m_runtime.adaptivePredictedMapped[index].store(physical, std::memory_order_relaxed);
    m_runtime.adaptiveOutput[index].store(physical, std::memory_order_relaxed);
    // Give the same presentation fields a non-zero deterministic fixture so
    // the live-path test proves propagation beyond Final Output.
    m_runtime.adaptiveVelocity[index].store(physical * 0.5F, std::memory_order_relaxed);
    m_runtime.adaptiveAcceleration[index].store(physical * 0.25F, std::memory_order_relaxed);
    m_runtime.adaptiveHorizonSeconds[index].store(0.016F, std::memory_order_relaxed);
    m_runtime.adaptiveLead[index].store(physical * 0.05F, std::memory_order_relaxed);
    m_runtime.adaptiveConfidence[index].store(0.75F, std::memory_order_relaxed);
    m_runtime.adaptiveRuntimeEnabled[index].store(true, std::memory_order_relaxed);
    m_runtime.adaptiveRuntimeMaximumHorizonSeconds[index].store(0.008F, std::memory_order_relaxed);
    m_runtime.adaptiveRuntimeMaximumLead[index].store(0.12F, std::memory_order_relaxed);
    m_runtime.mappingActive.store(false, std::memory_order_relaxed);
    m_runtime.mappingEffectiveState.store(static_cast<int>(MappingEffectiveState::Suspended),
                                          std::memory_order_relaxed);
    m_runtime.vjoyReady.store(false, std::memory_order_relaxed);
    m_runtime.inputReports.fetch_add(1, std::memory_order_relaxed);
    m_runtime.physicalReportsSinceAcquisition.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    m_runtime.adaptivePublishedAtUs[index].store(static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()),
        std::memory_order_relaxed);
    m_runtime.adaptivePublicationSequence[index].fetch_add(1, std::memory_order_release);
}

void MappingWorker::publishAxisAcquisitionPreviewSnapshot(int source, qint32 value,
                                                           qint32 observedMinimum,
                                                           qint32 observedMaximum,
                                                           quint64 changeCount,
                                                           int movementMagnitude,
                                                           qint64 lastChangeAgeMs)
{
    if (source < 0 || source >= kPhysicalAxisCount || observedMinimum > observedMaximum) return;
    const size_t index = static_cast<size_t>(source);
    // The UI preview always targets member zero of its isolated, in-memory
    // Device Rig.  Publishing both snapshots keeps the selected-device view
    // and the all-devices view coherent without creating an input session.
    m_runtime.physicalConnected.store(true, std::memory_order_relaxed);
    m_runtime.deviceRigMemberPhysicalConnected[0].store(true, std::memory_order_relaxed);
    const auto publish = [index, value, observedMinimum, observedMaximum, changeCount,
                          movementMagnitude, lastChangeAgeMs](AtomicAxisSourceTelemetry &telemetry) {
        telemetry.value[index].store(value, std::memory_order_relaxed);
        telemetry.observedMinimum[index].store(observedMinimum, std::memory_order_relaxed);
        telemetry.observedMaximum[index].store(observedMaximum, std::memory_order_relaxed);
        telemetry.changeCount[index].store(changeCount, std::memory_order_relaxed);
        telemetry.recentMovementMagnitude[index].store(movementMagnitude, std::memory_order_relaxed);
        telemetry.lastChangeAgeMs[index].store(lastChangeAgeMs, std::memory_order_relaxed);
        telemetry.available.store(true, std::memory_order_relaxed);
    };
    publish(m_runtime.axisSourceTelemetry);
    publish(m_runtime.deviceRigMemberAxisSourceTelemetry[0]);
    m_runtime.axisAcquisitionSource[index].store(source, std::memory_order_relaxed);
    m_runtime.axisLiveMovementObserved[index].store(movementMagnitude != 0, std::memory_order_relaxed);
    m_runtime.axisLastMovementAgeMs[index].store(lastChangeAgeMs, std::memory_order_relaxed);
    m_runtime.deviceRigMemberAxisAcquisitionSource[0][index].store(
        source, std::memory_order_relaxed);
    m_runtime.deviceRigMemberAxisLiveMovementObserved[0][index].store(
        movementMagnitude != 0, std::memory_order_relaxed);
    m_runtime.deviceRigMemberAxisLastMovementAgeMs[0][index].store(
        lastChangeAgeMs, std::memory_order_relaxed);
}

void MappingWorker::publishVirtualAxisAvailabilityForTest(bool available)
{
    m_testVirtualAxisAvailability.store(available ? 1 : 0, std::memory_order_relaxed);
    m_runtime.virtualAxisAvailable[0].store(false, std::memory_order_relaxed);
    for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
        m_runtime.virtualAxisAvailable[static_cast<size_t>(axis)].store(
            available, std::memory_order_relaxed);
    }
}

void MappingWorker::updateConfiguration(const MapperConfiguration &configuration)
{
    // Curve construction, point normalization, and LUT allocation are
    // deliberately complete before the worker can observe this update. A
    // point drag must never put spline construction in the report loop.
    const auto compileStarted = std::chrono::steady_clock::now();
    auto compiled = std::make_shared<RuntimeProfileCache>(compileRuntimeProfileCache(configuration));
    const auto compileUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - compileStarted).count());
    const bool requestedDeviceRigRuntime = hasActiveDeviceRigRuntime(configuration);
    QMutexLocker locker(&m_configurationMutex);
    if (hasActiveDeviceRigRuntime(m_configuration) != requestedDeviceRigRuntime) {
        m_runtimeTopologyChangeRequested = true;
    }
    m_configuration = configuration;
    m_preparedProfileCache = std::move(compiled);
    ++m_configurationVersion;
    m_runtime.lastCurveCompileUs = compileUs;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        m_runtime.calibrationMinimum[index] = configuration.calibration[index].minimum;
        m_runtime.calibrationCenter[index] = configuration.calibration[index].center;
        m_runtime.calibrationMaximum[index] = configuration.calibration[index].maximum;
        m_runtime.axisActivity[index] = static_cast<int>(configuration.axisActivity[index]);
    }
}

void MappingWorker::setMappingEnabled(bool enabled)
{
    if (enabled) m_vjoyReleasedForControlPlane = false;
    m_mappingRequested = enabled;
}

void MappingWorker::requestOutputAvailabilityRetry()
{
    m_outputAvailabilityRetryGeneration.fetch_add(1, std::memory_order_relaxed);
}

bool MappingWorker::mappingRequested() const
{
    return m_mappingRequested.load();
}

bool MappingWorker::prepareForDriverConfiguration(int timeoutMs)
{
    m_mappingRequested = false;
    m_vjoyReleasedForControlPlane = false;
    m_releaseVjoyRequested = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_vjoyReleasedForControlPlane.load() && m_runtime.outputNeutralized.load()) return true;
        QThread::msleep(10);
    }
    return false;
}

bool MappingWorker::restoreAfterDriverConfiguration(bool mappingWasRequested, int timeoutMs)
{
    if (!mappingWasRequested) {
        m_mappingRequested = false;
        return true;
    }
    setMappingEnabled(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_runtime.mappingActive.load() && m_runtime.vjoyReady.load()) return true;
        // A disconnected HOTAS cannot actively map, but preserving the user's
        // request allows normal worker discovery to resume without surprise.
        if (!m_runtime.physicalConnected.load()) return true;
        QThread::msleep(10);
    }
    return false;
}

bool MappingWorker::reacquirePhysicalController(const QString &expectedHidInstanceId, int timeoutMs)
{
    const QString expected = expectedHidInstanceId.trimmed();
    if (expected.isEmpty() || timeoutMs <= 0) return false;
    const std::uint64_t request = m_reacquireInputRequested.fetch_add(1) + 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_reacquireInputAcknowledged.load() >= request) {
            const DeviceSnapshot snapshot = deviceSnapshot();
            if (m_runtime.physicalConnected.load()
                && snapshot.hidInstanceId.compare(expected, Qt::CaseInsensitive) == 0
                && m_runtime.physicalReportsSinceAcquisition.load() > 0) {
                return true;
            }
        }
        QThread::msleep(25);
    }
    return false;
}

bool MappingWorker::selectPhysicalController(const QString &expectedDirectInputId, int timeoutMs)
{
    const QString expected = expectedDirectInputId.trimmed();
    if (expected.isEmpty() || timeoutMs <= 0) return false;
    const std::uint64_t request = m_reacquireInputRequested.fetch_add(1) + 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_reacquireInputAcknowledged.load() >= request) {
            const DeviceSnapshot snapshot = deviceSnapshot();
            if (m_runtime.physicalConnected.load()
                && snapshot.id.compare(expected, Qt::CaseInsensitive) == 0
                && m_runtime.physicalReportsSinceAcquisition.load() > 0) return true;
        }
        QThread::msleep(25);
    }
    return false;
}

DirectInputControllerProbe MappingWorker::probeExactPhysicalController(const QString &expectedDirectInputId)
{
    DirectInputControllerProbe result;
    result.povValues.fill(-1);
    const QString expected = expectedDirectInputId.trimmed();
    if (expected.isEmpty()) {
        result.diagnostic = u"No exact DirectInput controller identity was supplied for setup proof."_qs;
        return result;
    }

    LPDIRECTINPUT8W directInput = nullptr;
    const HRESULT initialized = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr);
    if (FAILED(initialized)) {
        result.diagnostic = u"DirectInput initialization failed: "_qs + inputErrorMessage(initialized);
        return result;
    }

    const std::optional<DirectInputDevice> selected = selectDeviceByPersistedId(directInput, expected);
    if (!selected) {
        directInput->Release();
        result.diagnostic = u"The exact saved controller was not visible through DirectInput."_qs;
        return result;
    }

    LPDIRECTINPUTDEVICE8W device = nullptr;
    const HRESULT created = directInput->CreateDevice(selected->guid, &device, nullptr);
    if (FAILED(created)) {
        directInput->Release();
        result.diagnostic = u"DirectInput could not open the exact saved controller: "_qs
            + inputErrorMessage(created);
        return result;
    }
    const HRESULT format = device->SetDataFormat(&c_dfDIJoystick2);
    const HRESULT cooperative = SUCCEEDED(format)
        ? device->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)
        : format;
    if (FAILED(cooperative)) {
        device->Release();
        directInput->Release();
        result.diagnostic = u"DirectInput could not configure the exact saved controller: "_qs
            + inputErrorMessage(cooperative);
        return result;
    }

    std::array<bool, kMaximumPhysicalButtons> buttons{};
    ObjectEnumerationContext objects{device, &result.axes, &buttons, &result.axisDescriptors, false};
    device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
    HRESULT acquired = device->Acquire();
    if (FAILED(acquired)) {
        device->Release();
        directInput->Release();
        result.diagnostic = u"DirectInput could not acquire the exact saved controller: "_qs
            + inputErrorMessage(acquired);
        return result;
    }

    DIJOYSTATE2 state{};
    HRESULT read = device->Poll();
    if (SUCCEEDED(read)) read = device->GetDeviceState(sizeof(state), &state);
    if (read == DIERR_INPUTLOST || read == DIERR_NOTACQUIRED) {
        acquired = device->Acquire();
        if (SUCCEEDED(acquired)) {
            read = device->Poll();
            if (SUCCEEDED(read)) read = device->GetDeviceState(sizeof(state), &state);
        }
    }
    if (FAILED(read)) {
        device->Unacquire();
        device->Release();
        directInput->Release();
        result.diagnostic = u"The exact saved controller did not deliver a DirectInput state report: "_qs
            + inputErrorMessage(read);
        return result;
    }

    result.acquired = true;
    result.name = selected->name;
    result.directInputId = guidToString(selected->guid);
    result.hidInstanceId = hidInstanceIdForDevice(device);
    result.hidContainerId = hidDeviceContainerId(result.hidInstanceId);
    result.axisCount = objects.axisCount;
    result.buttonCount = std::min(objects.buttonCount, kMaximumPhysicalButtons);
    result.povCount = objects.povCount;
    result.unsupportedAxisCount = objects.unsupportedAxisCount;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (!result.axes[static_cast<size_t>(axis)]) continue;
        const NativeAxisDescriptor &descriptor = result.axisDescriptors[static_cast<size_t>(axis)];
        result.normalizedAxes[static_cast<size_t>(axis)] = normalizeDirectInputAxisValue(
            directInputAxisValueAtOffset(state, descriptor.directInputOffset), descriptor);
    }
    for (int button = 0; button < result.buttonCount; ++button) {
        result.buttonPressed[static_cast<size_t>(button)] =
            (state.rgbButtons[static_cast<size_t>(button)] & 0x80U) != 0;
    }
    for (int pov = 0; pov < result.povCount && pov < kMaximumPhysicalPovs; ++pov) {
        const DWORD raw = state.rgdwPOV[static_cast<size_t>(pov)];
        result.povValues[static_cast<size_t>(pov)] = raw != kVjoyPovCentered && raw < 36000UL
            ? static_cast<int>(raw) : -1;
    }
    result.diagnostic = u"Exact DirectInput controller acquired and returned a live state report."_qs;
    device->Unacquire();
    device->Release();
    directInput->Release();
    return result;
}

DirectInputAxisAcquisitionProbe MappingWorker::captureExactPhysicalAxisAcquisition(
    const QString &expectedDirectInputId, int durationMs)
{
    DirectInputAxisAcquisitionProbe result;
    const QString expected = expectedDirectInputId.trimmed();
    result.durationMs = std::clamp(durationMs, 1000, 30000);
    if (expected.isEmpty()) {
        result.diagnostic = u"No exact DirectInput controller identity was supplied."_qs;
        return result;
    }

    LPDIRECTINPUT8W directInput = nullptr;
    const HRESULT initialized = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr);
    if (FAILED(initialized)) {
        result.diagnostic = u"DirectInput initialization failed: "_qs + inputErrorMessage(initialized);
        return result;
    }
    const auto selected = selectDeviceByPersistedId(directInput, expected);
    if (!selected) {
        // A stale persisted ID must never be redirected to another controller.
        // List attached physical candidates so a later read-only capture can
        // target an exact observed identity.
        EnumerationContext candidates;
        directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumDeviceCallback, &candidates,
                                 DIEDFL_ATTACHEDONLY);
        QStringList attachedPhysical;
        for (const DirectInputDevice &candidate : candidates.devices) {
            if (!isVirtualControllerName(candidate.name)) {
                attachedPhysical << u"%1 [%2]"_qs.arg(candidate.name, guidToString(candidate.guid));
            }
        }
        directInput->Release();
        result.diagnostic = u"The requested DirectInput controller was not visible. Attached physical controllers: "_qs
            + (attachedPhysical.isEmpty() ? u"none"_qs : attachedPhysical.join(u"; "_qs));
        return result;
    }

    LPDIRECTINPUTDEVICE8W device = nullptr;
    HRESULT status = directInput->CreateDevice(selected->guid, &device, nullptr);
    if (SUCCEEDED(status)) status = device->SetDataFormat(&c_dfDIJoystick2);
    if (SUCCEEDED(status)) {
        status = device->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    }
    if (FAILED(status)) {
        if (device) device->Release();
        directInput->Release();
        result.diagnostic = u"DirectInput could not configure the exact saved controller: "_qs
            + inputErrorMessage(status);
        return result;
    }

    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    ObjectEnumerationContext objects{device, &axes, &buttons, &result.axisDescriptors, false};
    device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
    result.bufferedConfigureResult = static_cast<qint32>(configureDirectInputBufferedEvents(device));
    status = device->Acquire();
    if (FAILED(status)) {
        device->Release();
        directInput->Release();
        result.diagnostic = u"DirectInput could not acquire the exact saved controller: "_qs
            + inputErrorMessage(status);
        return result;
    }

    result.standardMinimum.fill(std::numeric_limits<LONG>::max());
    result.standardMaximum.fill(std::numeric_limits<LONG>::min());
    result.stateFieldMinimum.fill(std::numeric_limits<LONG>::max());
    result.stateFieldMaximum.fill(std::numeric_limits<LONG>::min());
    result.bufferedMinimum.fill(std::numeric_limits<LONG>::max());
    result.bufferedMaximum.fill(std::numeric_limits<LONG>::min());
    std::array<LONG, kPhysicalAxisCount> lastStandard{};
    std::array<bool, kPhysicalAxisCount> standardKnown{};
    std::array<LONG, kPhysicalAxisCount> lastStateField{};
    std::array<bool, kPhysicalAxisCount> stateFieldKnown{};
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(result.durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        DIJOYSTATE2 state{};
        status = device->Poll();
        if (SUCCEEDED(status)) status = device->GetDeviceState(sizeof(state), &state);
        if (SUCCEEDED(status)) {
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const LONG value = directInputAxisValue(state, static_cast<PhysicalAxis>(axis));
                result.stateFieldMinimum[static_cast<size_t>(axis)] = std::min(
                    result.stateFieldMinimum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                result.stateFieldMaximum[static_cast<size_t>(axis)] = std::max(
                    result.stateFieldMaximum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                if (stateFieldKnown[static_cast<size_t>(axis)]
                    && lastStateField[static_cast<size_t>(axis)] != value) {
                    ++result.stateFieldChanges[static_cast<size_t>(axis)];
                }
                lastStateField[static_cast<size_t>(axis)] = value;
                stateFieldKnown[static_cast<size_t>(axis)] = true;
            }
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                if (!axes[static_cast<size_t>(axis)]) continue;
                const LONG value = directInputAxisValue(state, static_cast<PhysicalAxis>(axis));
                result.standardMinimum[static_cast<size_t>(axis)] = std::min(
                    result.standardMinimum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                result.standardMaximum[static_cast<size_t>(axis)] = std::max(
                    result.standardMaximum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                if (standardKnown[static_cast<size_t>(axis)]
                    && lastStandard[static_cast<size_t>(axis)] != value) {
                    ++result.standardChanges[static_cast<size_t>(axis)];
                }
                lastStandard[static_cast<size_t>(axis)] = value;
                standardKnown[static_cast<size_t>(axis)] = true;
            }
        }
        std::array<DIDEVICEOBJECTDATA, 32> events{};
        DWORD count = static_cast<DWORD>(events.size());
        const HRESULT buffered = device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), events.data(), &count, 0);
        result.lastBufferedReadResult = static_cast<qint32>(buffered);
        if (SUCCEEDED(buffered)) {
            for (DWORD event = 0; event < count; ++event) {
                int axis = -1;
                for (int candidate = 0; candidate < kPhysicalAxisCount; ++candidate) {
                    if (axes[static_cast<size_t>(candidate)]
                        && result.axisDescriptors[static_cast<size_t>(candidate)].directInputOffset
                            == events[event].dwOfs) {
                        axis = candidate;
                        break;
                    }
                }
                if (axis < 0) continue;
                const LONG value = static_cast<LONG>(events[event].dwData);
                result.bufferedMinimum[static_cast<size_t>(axis)] = std::min(
                    result.bufferedMinimum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                result.bufferedMaximum[static_cast<size_t>(axis)] = std::max(
                    result.bufferedMaximum[static_cast<size_t>(axis)], static_cast<qint32>(value));
                ++result.bufferedEvents[static_cast<size_t>(axis)];
            }
        }
        QThread::msleep(4);
    }

    result.acquired = true;
    result.name = selected->name;
    result.directInputId = guidToString(selected->guid);
    result.diagnostic = u"Read-only standard-state and buffered-object capture completed."_qs;
    device->Unacquire();
    device->Release();
    directInput->Release();
    return result;
}

void MappingWorker::requestStop()
{
    m_stopRequested = true;
}

DeviceSnapshot MappingWorker::deviceSnapshot() const
{
    QMutexLocker locker(&m_deviceMutex);
    return m_device;
}

QString MappingWorker::vjoyStatus() const
{
    QMutexLocker locker(&m_statusMutex);
    return m_vjoyStatus;
}

QVariantMap MappingWorker::vjoyOwnershipTelemetry() const
{
    QMutexLocker locker(&m_statusMutex);
    return {{u"deviceId"_qs, m_vjoyOwnership.deviceId},
            {u"rawStatus"_qs, m_vjoyOwnership.rawStatus},
            {u"rawStatusName"_qs, vjoyRawStatusName(m_vjoyOwnership.rawStatus)},
            {u"ownershipState"_qs, vjoyOwnershipStateName(m_vjoyOwnership.state)},
            {u"ownerPidAvailable"_qs, m_vjoyOwnership.ownerPidAvailable},
            {u"ownerPid"_qs, QVariant::fromValue(m_vjoyOwnership.ownerPid)},
            {u"ownerProcessLive"_qs, m_vjoyOwnership.ownerProcess.live},
            {u"ownerProcess"_qs, m_vjoyOwnership.ownerProcess.name},
            {u"ownerProcessPath"_qs, m_vjoyOwnership.ownerProcess.path},
            {u"ownerProcessDiagnostic"_qs, m_vjoyOwnership.ownerProcess.diagnostic},
            {u"hotasProcessId"_qs, QVariant::fromValue(m_vjoyOwnership.hotasProcessId)},
            {u"diagnostic"_qs, m_vjoyOwnership.diagnostic},
            {u"acquireAttempt"_qs, m_vjoyAcquireAttempt},
            {u"lastStatusTransition"_qs, m_vjoyLastStatusTransition}};
}

MappingLatencyPercentiles MappingWorker::latencyPercentiles() const
{
    MappingLatencyPercentiles result;
    const size_t count = static_cast<size_t>(std::min<std::uint64_t>(
        m_runtime.latencySampleCount.load(std::memory_order_acquire), kLatencyTelemetrySamples));
    if (count == 0) return result;

    // This runs on the GUI-side 60 Hz snapshot timer. It observes a rolling
    // atomic copy of the last 2048 reports and performs no work on the
    // real-time mapping thread beyond that thread's single sample store.
    std::array<std::uint64_t, kLatencyTelemetrySamples> values{};
    for (size_t index = 0; index < count; ++index) {
        values[index] = m_runtime.latencySamples[index].load(std::memory_order_acquire);
    }
    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    const auto percentile = [&values, count](double fraction) {
        const size_t index = std::min(count - 1, static_cast<size_t>(std::ceil(
            fraction * static_cast<double>(count))) - 1);
        return values[index];
    };
    result.sampleCount = count;
    result.p95Us = percentile(0.95);
    result.p99Us = percentile(0.99);
    return result;
}

std::shared_ptr<const RuntimeProfileCache> MappingWorker::runtimeProfileCache() const
{
    QMutexLocker locker(&m_configurationMutex);
    return m_preparedProfileCache;
}

MapperConfiguration MappingWorker::configurationCopy()
{
    QMutexLocker locker(&m_configurationMutex);
    return m_configuration;
}

std::pair<MapperConfiguration, std::shared_ptr<const RuntimeProfileCache>>
MappingWorker::preparedConfigurationCopy()
{
    QMutexLocker locker(&m_configurationMutex);
    return {m_configuration, m_preparedProfileCache};
}

void MappingWorker::setDeviceSnapshot(const DeviceSnapshot &snapshot)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_deviceMutex);
        changed = m_device.name != snapshot.name || m_device.id != snapshot.id
            || m_device.hidInstanceId != snapshot.hidInstanceId
            || m_device.hidContainerId != snapshot.hidContainerId;
        m_device = snapshot;
    }
    if (changed) emit hardwareStateChanged();
}

void MappingWorker::setVjoyStatus(const QString &status)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_statusMutex);
        changed = m_vjoyStatus != status;
        m_vjoyStatus = status;
    }
    if (changed) emit hardwareStateChanged();
}

void MappingWorker::setVjoyOwnershipEvidence(const VJoyOwnershipEvidence &evidence,
                                              const QString &acquireAttempt)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_statusMutex);
        const QString previous = QStringLiteral("%1|%2|%3")
            .arg(vjoyRawStatusName(m_vjoyOwnership.rawStatus))
            .arg(m_vjoyOwnership.ownerPid).arg(vjoyOwnershipStateName(m_vjoyOwnership.state));
        const QString current = QStringLiteral("%1|%2|%3")
            .arg(vjoyRawStatusName(evidence.rawStatus))
            .arg(evidence.ownerPid).arg(vjoyOwnershipStateName(evidence.state));
        if (previous != current) {
            m_vjoyLastStatusTransition = QStringLiteral("%1 -> %2").arg(previous, current);
            changed = true;
        }
        changed = changed || m_vjoyAcquireAttempt != acquireAttempt;
        m_vjoyOwnership = evidence;
        m_vjoyAcquireAttempt = acquireAttempt;
    }
    if (changed) emit hardwareStateChanged();
}

void MappingWorker::run()
{
    // Captured once at worker startup only when a qualification observer is
    // active. No report-path work is added.
    InteractiveSchedulingPolicy::recordCurrentThread("mapping");
    LPDIRECTINPUT8W directInput = nullptr;
    const HRESULT initialized = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr);
    if (FAILED(initialized)) {
        emit workerEvent(u"Could not initialize DirectInput: "_qs + inputErrorMessage(initialized));
        return;
    }

    while (!m_stopRequested.load()) {
        if (hasActiveDeviceRigRuntime(configurationCopy())) {
            runDeviceRig(directInput);
        } else {
            runSingleDevice(directInput);
        }
        // Topology changes are handled as bounded acquisition boundaries.
        // A fresh loop resolves only durable configuration, never QML state.
        m_runtimeTopologyChangeRequested = false;
    }
    directInput->Release();
}

void MappingWorker::runSingleDevice(IDirectInput8W *directInput)
{
    VJoyAdapter vjoy;

    LPDIRECTINPUTDEVICE8W device = nullptr;
    HANDLE inputEvent = nullptr;
    std::array<bool, kPhysicalAxisCount> availableAxes{};
    std::array<NativeAxisDescriptor, kPhysicalAxisCount> axisDescriptors{};
    std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> axisAcquisitions{};
    std::array<bool, kPhysicalAxisCount> manualAcquisitionApplied{};
    std::array<LONG, kPhysicalAxisCount> bufferedAxisValues{};
    std::array<bool, kPhysicalAxisCount> bufferedAxisValuesKnown{};
    // One bounded provisional correlation per native object. It is consulted
    // only for a newly queued buffered event, never by ordinary state reads.
    std::array<BufferedObjectCorrelationEvidence, kPhysicalAxisCount> bufferedCorrelationEvidence{};
    std::array<int, kPhysicalAxisCount> axisAcquisitionMethods{};
    // Established at enumeration. A buffered proof may promote a source only
    // when the saved native signature still matches the open DirectInput object.
    std::array<bool, kPhysicalAxisCount> axisEvidenceCompatible{};
    std::array<bool, kPhysicalAxisCount> fixedAxes{};
    std::array<bool, kMaximumPhysicalButtons> availableButtons{};
    std::array<float, kPhysicalAxisCount> lastObservedAxisValues{};
    std::array<std::chrono::steady_clock::time_point, kPhysicalAxisCount> lastAxisMovementAt{};
    std::array<bool, kPhysicalAxisCount> axisLiveMovementObserved{};
    std::array<LONG, kPhysicalAxisCount> sourceMonitorPrevious{};
    std::array<LONG, kPhysicalAxisCount> sourceMonitorMinimum{};
    std::array<LONG, kPhysicalAxisCount> sourceMonitorMaximum{};
    std::array<std::uint64_t, kPhysicalAxisCount> sourceMonitorChanges{};
    std::array<std::chrono::steady_clock::time_point, kPhysicalAxisCount> sourceMonitorLastChanged{};
    bool sourceMonitorInitialized = false;
        lastObservedAxisValues.fill(std::numeric_limits<float>::quiet_NaN());
        axisLiveMovementObserved.fill(false);
        sourceMonitorInitialized = false;
    PhysicalInputMonitor physicalMonitor;
    MeaningfulInputEvidence meaningfulInput;
    quint64 latestMeaningfulInputSequence = 0;
    quint64 lastPublishedMeaningfulInputSequence = 0;
    m_runtime.meaningfulInputSequence.store(0, std::memory_order_relaxed);
    for (std::atomic_uint64_t &sequence : m_runtime.deviceRigMeaningfulInputSequence) {
        sequence.store(0, std::memory_order_relaxed);
    }
    for (std::atomic_uint64_t &sequence : m_runtime.deviceRigMeaningfulOutputSequence) {
        sequence.store(0, std::memory_order_relaxed);
    }
    auto preparedConfiguration = preparedConfigurationCopy();
    MapperConfiguration configuration = std::move(preparedConfiguration.first);
    std::shared_ptr<const RuntimeProfileCache> activeProfileCache
        = std::move(preparedConfiguration.second);
    int effectiveProfileIndex = activeProfileCache->baseProfileIndex;
    const RuntimeMappingConfiguration *activeMapping
        = &activeProfileCache->profiles[static_cast<size_t>(effectiveProfileIndex)];
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        fixedAxes[static_cast<size_t>(index)] = configuration.axisActivity[static_cast<size_t>(index)]
            == PhysicalAxisActivity::Fixed;
    }
    std::array<bool, kVirtualAxisSlotCount> outputLayoutAxes{};
    if (const DeviceRig *rig = findDeviceRig(configuration, configuration.activeDeviceRigId)) {
        if (const VirtualOutputLayout *layout = findOutputLayout(
                configuration, deviceRigPrimaryOutputLayoutId(*rig))) {
            outputLayoutAxes = layout->requirements.axes;
        }
    }
    ProfileTriggerRuntime profileTriggers;
    AutomationRuntime automation;
    automation.setCompiled(activeProfileCache->automation.get());
    quint64 appliedVersion = m_configurationVersion.load();
    std::array<float, kVirtualAxisSlotCount> lastVirtualValues{};
    lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
    // This remains separate from the write-diff cache. Profile/configuration
    // changes invalidate that cache, while bumpless transfer must retain the
    // actual latest output as its continuity anchor.
    std::array<float, kVirtualAxisSlotCount> lastActualVirtualValues{};
    lastActualVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
    AxisMappingTransitionEngine axisTransitions;
    std::array<int, kVirtualAxisSlotCount> virtualAxisSources{};
    virtualAxisSources.fill(-1);
    std::array<AxisHysteresisState, kPhysicalAxisCount> hysteresisStates{};
    std::array<AxisCenterResolverState, kPhysicalAxisCount> centerResolverStates{};
    std::array<AdaptiveResponseProcessor, kPhysicalAxisCount> adaptiveProcessors{};
    // Persistent hierarchy is precompiled in RuntimeAxisMapping. Automation
    // overlays are flattened only when their active property set changes, so
    // the report loop reads this fixed primitive table directly.
    std::array<RuntimeAdaptiveResponseConfig, kPhysicalAxisCount> effectiveAdaptiveConfigurations{};
    std::array<RuntimeAdaptiveResponseOverride, kPhysicalAxisCount> activeAdaptiveOverlays{};
    PhysicalButtonStates latestPhysicalButtons{};
    PhysicalPovValues latestPovValues{};
    latestPovValues.fill(-1);
    RuntimeButtonTargets runtimeButtonTargets{};
    RuntimePovTargets runtimePovTargets{};
    VirtualButtonStates lastVirtualButtonStates{};
    std::array<int, kMaximumPhysicalPovs> lastNativePovValues{};
    lastNativePovValues.fill(-2); // -1 is a valid centered output value.
    std::array<int, kMaximumRuntimeSignalFlowNativePovRoutes> lastSignalFlowNativePovValues{};
    lastSignalFlowNativePovValues.fill(-2); // -1 is a valid centered output value.
    int vjoyButtonCapacity = 0;
    int vjoyContinuousPovCapacity = 0;
    int vjoyDiscretePovCapacity = 0;
    std::array<bool, kVirtualAxisSlotCount> vjoyAxisAvailable{};
    bool buttonDefaultsPending = false;
    bool profileTriggerSessionActive = false;
    bool controlPlaneInitialized = false;
    bool mappingTransitionRequested = false;
    std::array<bool, kMaximumAutomationRules> lastAutomationRuleStates{};
    bool wasMappingRequested = false;
    std::optional<std::chrono::steady_clock::time_point> pendingProfileSwitchStarted;
    std::uint64_t processedReports = 0;
    std::uint64_t latencyTotal = 0;
    std::uint64_t latencySampleSequence = 0;
    std::uint64_t handledReacquireRequest = 0;
    std::uint64_t handledOutputAvailabilityRetry = m_outputAvailabilityRetryGeneration.load(std::memory_order_relaxed);
    auto nextDiscovery = std::chrono::steady_clock::now();
    auto nextVjoyCheck = std::chrono::steady_clock::now();
    auto nextVjoyAcquire = std::chrono::steady_clock::now();

    const auto publishVirtualAxisAvailability = [this](
        const std::array<bool, kVirtualAxisSlotCount> &reported) {
        const int fixture = m_testVirtualAxisAvailability.load(std::memory_order_relaxed);
        for (int axis = 0; axis < kVirtualAxisSlotCount; ++axis) {
            // Test routes must remain deterministic while configuration swaps
            // race the worker's first real vJoy capability poll. Production
            // never enables this explicit fixture and therefore keeps the
            // actual driver descriptor authoritative.
            const bool available = fixture >= 0
                ? axis != 0 && fixture != 0
                : reported[static_cast<size_t>(axis)];
            m_runtime.virtualAxisAvailable[static_cast<size_t>(axis)].store(
                available, std::memory_order_relaxed);
        }
    };

    const auto clearVirtualButtonSnapshot = [&] {
        lastVirtualButtonStates.fill(false);
        for (auto &button : m_runtime.virtualButtonPressed) button = false;
    };

    const auto clearVirtualAxisSnapshot = [&] {
        for (auto &value : m_runtime.virtualValues) {
            value = std::numeric_limits<float>::quiet_NaN();
        }
    };

    const auto clearPhysicalButtonSnapshot = [&] {
        latestPhysicalButtons.fill(false);
        for (auto &button : m_runtime.physicalButtonPressed) button = false;
        m_runtime.lastPhysicalButton = 0;
        m_runtime.lastPhysicalButtonTarget = 0;
    };

    const auto rebuildButtonTargets = [&] {
        runtimeButtonTargets = buildRuntimeButtonTargets(activeMapping->buttons, vjoyButtonCapacity,
            activeProfileCache->profileTriggers);
        for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
            if (activeProfileCache->mappingControls[static_cast<size_t>(source)]
                != MappingControlAction::None) {
                runtimeButtonTargets[static_cast<size_t>(source)] = 0;
            }
        }
        runtimePovTargets = buildRuntimePovTargets(activeMapping->povs, vjoyButtonCapacity,
            activeProfileCache->povProfileTriggers);
    };

    const auto refreshEffectiveAdaptiveConfigurations = [&] {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            RuntimeAdaptiveResponseConfig effective = activeMapping->axes[static_cast<size_t>(axis)]
                .adaptiveResponse;
            const RuntimeAdaptiveResponseOverride &overlay = activeAdaptiveOverlays[
                static_cast<size_t>(axis)];
            if (overlay.active) {
                effective = applyAdaptiveResponseRuntimeOverride(effective, overlay);
            }
            effectiveAdaptiveConfigurations[static_cast<size_t>(axis)] = effective;
            adaptiveProcessors[static_cast<size_t>(axis)].reset();
            centerResolverStates[static_cast<size_t>(axis)] = {};
        }
    };
    refreshEffectiveAdaptiveConfigurations();

    const auto selectEffectiveProfile = [&](const EffectiveProfileSelection &selection,
                                            bool countSwitch) {
        const int selectedIndex = std::clamp(selection.profileIndex, 0,
            static_cast<int>(activeProfileCache->profiles.size()) - 1);
        // A physical/Automation profile trigger is allowed to swap only
        // within the already-acquired output layout. Cross-layout work is an
        // AppBackend control-plane transition (neutralize, release, HidHide,
        // acquire, neutral baseline), never an operation performed by a
        // DirectInput report.
        if (selectedIndex < static_cast<int>(activeProfileCache->profileVjoyDeviceIds.size())
            && activeProfileCache->profileVjoyDeviceIds[static_cast<size_t>(selectedIndex)]
                != configuration.vjoyDeviceId) {
            return false;
        }
        const bool changed = selectedIndex != effectiveProfileIndex;
        effectiveProfileIndex = selectedIndex;
        activeMapping = &activeProfileCache->profiles[static_cast<size_t>(effectiveProfileIndex)];
        m_runtime.effectiveProfileIndex = effectiveProfileIndex;
        m_runtime.profileOverrideButton = selection.sourceButton;
        m_runtime.profileOverridePovHat = selection.sourcePovHat;
        m_runtime.profileOverridePovDirection = selection.sourcePovDirection;
        m_runtime.profileOverrideMode = static_cast<int>(selection.sourceMode);
        m_runtime.profileOverrideAutomationRule = selection.sourceAutomationRule;
        m_runtime.profileOverrideAutomationAction = selection.sourceAutomationAction;
        if (!changed) return false;
        mappingTransitionRequested = true;
        // The current physical snapshot is re-evaluated immediately below.
        // Axis cache invalidation forces a same-report output publication;
        // the normal button diff loop releases/asserts changed routes.
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        lastNativePovValues.fill(-2);
        lastSignalFlowNativePovValues.fill(-2);
        clearVirtualAxisSnapshot();
        for (AxisHysteresisState &state : hysteresisStates) state = {};
        for (AxisCenterResolverState &state : centerResolverStates) state = {};
        refreshEffectiveAdaptiveConfigurations();
        rebuildButtonTargets();
        if (countSwitch) {
            ++m_runtime.profileSwitchCount;
        }
        return true;
    };

    const auto quiesceVirtualController = [&] {
        // This is an event-boundary failsafe, never report-loop behavior.
        // Keeping a successfully acquired vJoy device alive avoids game-side
        // controller re-enumeration while making every game-facing control
        // explicitly inert.
        if (vjoy.acquired()) {
            for (int target = 1; target < kVirtualAxisSlotCount; ++target) {
                if (vjoyAxisAvailable[static_cast<size_t>(target)]
                    && vjoy.setAxis(static_cast<VirtualAxis>(target), 0.0F)) {
                    ++m_runtime.vjoyWrites;
                }
            }
            for (int button = 1; button <= vjoyButtonCapacity; ++button) {
                if (vjoy.setButton(button, false)) ++m_runtime.vjoyWrites;
            }
            for (int pov = 1; pov <= vjoyContinuousPovCapacity; ++pov) {
                if (vjoy.centerContinuousPov(pov)) ++m_runtime.vjoyWrites;
            }
            for (int pov = 1; pov <= vjoyDiscretePovCapacity; ++pov) {
                if (vjoy.centerDiscretePov(pov)) ++m_runtime.vjoyWrites;
            }
        }
        lastVirtualValues.fill(0.0F);
        lastActualVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        axisTransitions.clear();
        lastNativePovValues.fill(-1);
        lastSignalFlowNativePovValues.fill(-1);
        clearVirtualButtonSnapshot();
        for (std::atomic<float> &value : m_runtime.virtualValues) value = 0.0F;
        for (AxisHysteresisState &state : hysteresisStates) state = {};
        for (AxisCenterResolverState &state : centerResolverStates) state = {};
        for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
        m_runtime.mappingActive = false;
        m_runtime.outputNeutralized = true;
    };

    const auto releaseInput = [&] {
        if (m_runtime.mappingActive.load() || !m_runtime.outputNeutralized.load()) {
            quiesceVirtualController();
            emit workerEvent(u"Mapping paused: controller disconnected"_qs);
        }
        if (device) {
            device->Unacquire();
            device->SetEventNotification(nullptr);
            device->Release();
            device = nullptr;
        }
        if (inputEvent) {
            CloseHandle(inputEvent);
            inputEvent = nullptr;
        }
        availableAxes.fill(false);
        axisDescriptors = {};
        axisAcquisitions = {};
        manualAcquisitionApplied.fill(false);
        bufferedAxisValuesKnown.fill(false);
        bufferedCorrelationEvidence = {};
        axisAcquisitionMethods.fill(0);
        axisEvidenceCompatible.fill(false);
        lastObservedAxisValues.fill(std::numeric_limits<float>::quiet_NaN());
        axisLiveMovementObserved.fill(false);
        sourceMonitorInitialized = false;
        availableButtons.fill(false);
        physicalMonitor.disconnect();
        meaningfulInput = {};
        latestMeaningfulInputSequence = 0;
        lastPublishedMeaningfulInputSequence = 0;
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            m_runtime.axisAvailable[static_cast<size_t>(axis)] = false;
            m_runtime.axisAcquisitionSource[static_cast<size_t>(axis)] = -1;
            m_runtime.axisResolvedFormattedSource[static_cast<size_t>(axis)] = -1;
            m_runtime.axisLiveMovementObserved[static_cast<size_t>(axis)] = false;
            m_runtime.axisLastMovementAgeMs[static_cast<size_t>(axis)] = -1;
        }
        m_runtime.axisSourceTelemetry.available.store(false, std::memory_order_relaxed);
        for (auto &button : m_runtime.buttonAvailable) button = false;
        clearPhysicalButtonSnapshot();
        controlPlaneInitialized = false;
        clearVirtualButtonSnapshot();
        profileTriggers.reset();
        automation.reset();
        profileTriggers.clearAutomationContributions();
        profileTriggerSessionActive = false;
        lastAutomationRuleStates.fill(false);
        m_runtime.automationActiveRuleCount = 0;
        m_runtime.automationEvaluationUs = 0;
        for (std::atomic_bool &active : m_runtime.automationRuleActive) active = false;
        selectEffectiveProfile({activeProfileCache->baseProfileIndex, 0,
                                0, -1, ProfileTriggerMode::Disabled}, false);
        for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
        m_runtime.physicalConnected = false;
        m_runtime.physicalReportsSinceAcquisition = 0;
        m_runtime.mappingEffectiveState = m_mappingRequested.load()
            ? static_cast<int>(MappingEffectiveState::Suspended)
            : static_cast<int>(MappingEffectiveState::Off);
        m_runtime.axisCount = 0;
        m_runtime.buttonCount = 0;
        m_runtime.povCount = 0;
        latestPovValues.fill(-1);
        for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
        setDeviceSnapshot({});
    };

    const auto discoverInput = [&] {
        configuration = configurationCopy();
        const auto selected = selectDevice(directInput, configuration);
        if (!selected) {
            return;
        }
        const HRESULT created = directInput->CreateDevice(selected->guid, &device, nullptr);
        if (FAILED(created)) {
            emit workerEvent(u"Could not open controller: "_qs + inputErrorMessage(created));
            device = nullptr;
            return;
        }
        if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))
            || FAILED(device->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
            emit workerEvent(u"Could not configure DirectInput controller"_qs);
            releaseInput();
            return;
        }
        ObjectEnumerationContext objects{device, &availableAxes, &availableButtons, &axisDescriptors};
        device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
        const SavedControllerRecord *record = nullptr;
        if (!configuration.activeControllerRecordId.isEmpty()) {
            const auto found = std::find_if(configuration.savedControllers.cbegin(),
                configuration.savedControllers.cend(), [&configuration](const SavedControllerRecord &candidate) {
                    return candidate.id == configuration.activeControllerRecordId;
                });
            if (found != configuration.savedControllers.cend()) record = &*found;
        }
        axisEvidenceCompatible.fill(false);
        if (record) {
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const size_t index = static_cast<size_t>(axis);
                axisEvidenceCompatible[index] = directInputAxisDescriptorSignatureMatches(
                    axisDescriptors[index], record->axisDescriptors[index]);
                reuseVerifiedFormattedSource(&axisDescriptors[index], record->axisDescriptors[index]);
                // Buffered fallback is deliberately retained only for the exact native
                // object that produced it.  It remains a safety channel until fresh
                // evidence either proves a fixed state field or the user chooses an
                // explicit override; a reconnect must not silently drop it.
                if (axisEvidenceCompatible[index]
                    && record->axisDescriptors[index].acquisitionMethod == 1) {
                    axisAcquisitionMethods[index] = 1;
                }
            }
        }
        static const std::array<AxisAcquisitionOverride, kPhysicalAxisCount> noOverrides{};
        axisAcquisitions = compileRuntimeAxisAcquisitions(axisDescriptors,
            record ? record->axisAcquisitionOverrides : noOverrides, &manualAcquisitionApplied);
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            availableAxes[static_cast<size_t>(axis)] = axisAcquisitions[static_cast<size_t>(axis)].valid;
        }
        // Buffered data is a preconfigured, bounded corroboration channel.
        // A failure is recorded by the absent fallback rather than changing
        // normal DIJOYSTATE2 acquisition behavior.
        const bool bufferedEventsEnabled = SUCCEEDED(configureDirectInputBufferedEvents(device));
        inputEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!inputEvent || FAILED(device->SetEventNotification(inputEvent))) {
            if (inputEvent) {
                CloseHandle(inputEvent);
                inputEvent = nullptr;
            }
            // Event notifications are only an optional wake-up optimization.
            // The fixed cadence below remains the authoritative physical monitor.
            emit workerEvent(u"DirectInput notifications unavailable; using polling"_qs);
        }
        const HRESULT acquired = device->Acquire();
        if (FAILED(acquired)) {
            emit workerEvent(u"Could not acquire controller: "_qs + inputErrorMessage(acquired));
            releaseInput();
            return;
        }
        physicalMonitor.configure(availableAxes, availableButtons, objects.povCount);
        for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
        for (AxisCenterResolverState &state : centerResolverStates) state = {};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            m_runtime.axisAvailable[index] = availableAxes[index];
            m_runtime.axisAcquisitionSource[index] = availableAxes[index]
                ? (manualAcquisitionApplied[static_cast<size_t>(index)] ? 2
                    : axisAcquisitionMethods[static_cast<size_t>(index)]) : -1;
            m_runtime.axisResolvedFormattedSource[index] = -1;
            m_runtime.axisLiveMovementObserved[index] = false;
            m_runtime.axisLastMovementAgeMs[index] = -1;
        }
        for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
            m_runtime.buttonAvailable[index] = availableButtons[index];
        }
        m_runtime.axisCount = objects.axisCount;
        m_runtime.buttonCount = std::min(objects.buttonCount, kMaximumPhysicalButtons);
        m_runtime.povCount = objects.povCount;
        for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
        m_runtime.physicalConnected = true;
        const QString hidInstanceId = hidInstanceIdForDevice(device);
        setDeviceSnapshot({selected->name, guidToString(selected->guid), hidInstanceId,
                           hidDeviceContainerId(hidInstanceId)});
        emit workerEvent(QString(u"Controller connected: %1 · %2 axes · %3 buttons"_qs)
            .arg(selected->name).arg(objects.axisCount).arg(m_runtime.buttonCount.load()));
        if (!bufferedEventsEnabled) {
            emit workerEvent(u"DirectInput buffered axis evidence unavailable; standard state acquisition remains active"_qs);
        }
        if (inputEvent) SetEvent(inputEvent); // Promptly publish an initial state.
    };

    const auto suggestDefaultButtonsIfNeeded = [&] {
        const int physicalCount = m_runtime.buttonCount.load();
        if (device && needsDefaultButtonMappings(activeMapping->buttons, physicalCount, vjoyButtonCapacity)
            && physicalCount > 0
            && vjoyButtonCapacity > 0 && !buttonDefaultsPending) {
            buttonDefaultsPending = true;
            emit buttonConfigurationSuggested(physicalCount, vjoyButtonCapacity);
        }
    };

    const auto refreshVjoyCapabilities = [&] {
        const int reportedCapacity = vjoy.buttonCapacity(configuration.vjoyDeviceId, nullptr);
        const VJoyAdapter::PovCapabilities reportedPovs = vjoy.povCapabilities(
            configuration.vjoyDeviceId, nullptr);
        const std::array<bool, kVirtualAxisSlotCount> reportedAxes =
            vjoy.axisCapabilities(configuration.vjoyDeviceId, nullptr);
        if (reportedAxes != vjoyAxisAvailable) {
            vjoyAxisAvailable = reportedAxes;
            publishVirtualAxisAvailability(vjoyAxisAvailable);
            lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
            emit hardwareStateChanged();
        }
        if (reportedCapacity != vjoyButtonCapacity) {
            vjoyButtonCapacity = reportedCapacity;
            m_runtime.vjoyButtonCount = vjoyButtonCapacity;
            rebuildButtonTargets();
            if (inputEvent) SetEvent(inputEvent);
            emit hardwareStateChanged();
        }
        if (reportedPovs.continuous != vjoyContinuousPovCapacity
            || reportedPovs.discrete != vjoyDiscretePovCapacity) {
            vjoyContinuousPovCapacity = reportedPovs.continuous;
            vjoyDiscretePovCapacity = reportedPovs.discrete;
            m_runtime.vjoyContinuousPovCount = vjoyContinuousPovCapacity;
            m_runtime.vjoyDiscretePovCount = vjoyDiscretePovCapacity;
            lastNativePovValues.fill(-2);
            lastSignalFlowNativePovValues.fill(-2);
            emit hardwareStateChanged();
        }
        suggestDefaultButtonsIfNeeded();
    };

    const auto applyLatestConfiguration = [&] {
        const quint64 currentVersion = m_configurationVersion.load();
        if (currentVersion == appliedVersion) return;
        const int previousVjoyDeviceId = configuration.vjoyDeviceId;
        const QString previousProfileId = configuration.activeProfileId;
        if (m_runtime.mappingActive.load()) {
            // Clear every old native target before its binding can change or
            // be disabled. Explicit writes are the safety mechanism; the
            // driver reset path is intentionally not trusted for neutral.
            for (const NativePovBinding &binding : activeProfileCache->nativePovBindings) {
                if (binding.enabled) vjoy.setPov(binding, -1);
            }
        }
        auto prepared = preparedConfigurationCopy();
        configuration = std::move(prepared.first);
        // A configuration mutation may replace a manual/automatic acquisition
        // contract. Never combine its new bindings with provisional evidence
        // captured for the previous control-plane state.
        bufferedCorrelationEvidence = {};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            fixedAxes[static_cast<size_t>(index)] = configuration.axisActivity[static_cast<size_t>(index)]
                == PhysicalAxisActivity::Fixed;
        }
        outputLayoutAxes.fill(false);
        if (const DeviceRig *rig = findDeviceRig(configuration, configuration.activeDeviceRigId)) {
            if (const VirtualOutputLayout *layout = findOutputLayout(
                    configuration, deviceRigPrimaryOutputLayoutId(*rig))) {
                outputLayoutAxes = layout->requirements.axes;
            }
        }
        publishVirtualAxisAvailability(vjoyAxisAvailable);
        // The mapping loop only swaps a table that was fully built before the
        // configuration version changed; it never builds a spline or LUT.
        activeProfileCache = std::move(prepared.second);
        // Any configuration mutation can alter a curve, limit, sensitivity,
        // or routing transfer function. The actual output is captured only
        // when the next physical report reaches the publication path.
        mappingTransitionRequested = true;
        automation.setCompiled(activeProfileCache->automation.get());
        profileTriggers.clearAutomationContributions();
        lastAutomationRuleStates.fill(false);
        m_runtime.automationEngineEnabled = activeProfileCache->automation
            && activeProfileCache->automation->engineEnabled;
        m_runtime.automationRuleCount = activeProfileCache->automation
            ? activeProfileCache->automation->ruleCount : 0;
        m_runtime.automationActiveRuleCount = 0;
        for (std::atomic_bool &active : m_runtime.automationRuleActive) active = false;
        // Settings/profile updates receive a fully compiled table and begin a
        // new centre-resolution window on the next report.
        for (AxisHysteresisState &state : hysteresisStates) state = {};
        for (AxisCenterResolverState &state : centerResolverStates) state = {};
        for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
        const SavedControllerRecord *record = nullptr;
        if (!configuration.activeControllerRecordId.isEmpty()) {
            const auto found = std::find_if(configuration.savedControllers.cbegin(),
                configuration.savedControllers.cend(), [&configuration](const SavedControllerRecord &candidate) {
                    return candidate.id == configuration.activeControllerRecordId;
                });
            if (found != configuration.savedControllers.cend()) record = &*found;
        }
        if (record) {
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                reuseVerifiedFormattedSource(&axisDescriptors[static_cast<size_t>(axis)],
                                             record->axisDescriptors[static_cast<size_t>(axis)]);
            }
        }
        static const std::array<AxisAcquisitionOverride, kPhysicalAxisCount> noOverrides{};
        axisAcquisitions = compileRuntimeAxisAcquisitions(axisDescriptors,
            record ? record->axisAcquisitionOverrides : noOverrides, &manualAcquisitionApplied);
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            availableAxes[static_cast<size_t>(axis)] =
                axisAcquisitions[static_cast<size_t>(axis)].valid;
            // Method 3 is a transient "proof pending commit" marker.  Once
            // this control-plane update has compiled the persisted verified
            // state field, resume normal direct state acquisition instead of
            // unnecessarily retaining the sampled buffered value.
            if (axisAcquisitionMethods[static_cast<size_t>(axis)] == 3) {
                axisAcquisitionMethods[static_cast<size_t>(axis)] = 0;
            }
        }
        appliedVersion = currentVersion;
        buttonDefaultsPending = false;
        const bool manualBaseChanged = configuration.activeProfileId != previousProfileId;
        profileTriggers.reconcileConfiguration(*activeProfileCache, latestPhysicalButtons,
                                                latestPovValues, m_runtime.povCount.load(),
                                                manualBaseChanged);
        const EffectiveProfileSelection selection = profileTriggerSessionActive
            ? profileTriggers.effectiveProfile(*activeProfileCache)
            : EffectiveProfileSelection{activeProfileCache->baseProfileIndex, 0, 0, -1,
                                        ProfileTriggerMode::Disabled};
        const bool switched = selectEffectiveProfile(selection, true);
        // A profile edit can replace the compiled state at the same index.
        // Rebind the pointer and force a current-state axis reconciliation.
        activeMapping = &activeProfileCache->profiles[static_cast<size_t>(effectiveProfileIndex)];
        activeAdaptiveOverlays = {};
        refreshEffectiveAdaptiveConfigurations();
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        clearVirtualAxisSnapshot();
        rebuildButtonTargets();
        lastNativePovValues.fill(-2);
        lastSignalFlowNativePovValues.fill(-2);
        if (configuration.vjoyDeviceId != previousVjoyDeviceId && m_runtime.mappingActive.load()) {
            quiesceVirtualController();
            vjoy.release();
            m_runtime.vjoyReady = false;
            emit workerEvent(u"vJoy device changed; reacquiring mapping output"_qs);
        }
        if (switched || manualBaseChanged) {
            const int lastButton = m_runtime.lastPhysicalButton.load();
            if (lastButton > 0 && lastButton <= kMaximumPhysicalButtons) {
                m_runtime.lastPhysicalButtonTarget = runtimeButtonTargets[static_cast<size_t>(lastButton - 1)];
            }
        }
        if (inputEvent) SetEvent(inputEvent);
    };

    const auto applyAutomationMappingControl = [&](MappingControlAction action) {
        if (action == MappingControlAction::None) return;
        const bool current = m_mappingRequested.load();
        const bool desired = action == MappingControlAction::MappingOn ? true
            : action == MappingControlAction::MappingOff ? false : !current;
        if (desired != current) {
            m_mappingRequested = desired;
            emit workerEvent(u"Automation: "_qs + mappingControlActionLabel(action));
        }
    };

    while (!m_stopRequested.load()) {
        if (m_runtimeTopologyChangeRequested.exchange(false)) break;
        const auto now = std::chrono::steady_clock::now();
        applyLatestConfiguration();
        const bool mappingRequestedNow = m_mappingRequested.load();
        if (!mappingRequestedNow && wasMappingRequested) {
            // Stop Mapping never preserves Hold or Toggle latches.
            profileTriggers.reset();
            automation.reset();
            profileTriggers.clearAutomationContributions();
            profileTriggerSessionActive = false;
            lastAutomationRuleStates.fill(false);
            m_runtime.automationActiveRuleCount = 0;
            m_runtime.automationEvaluationUs = 0;
            for (std::atomic_bool &active : m_runtime.automationRuleActive) active = false;
            selectEffectiveProfile({activeProfileCache->baseProfileIndex, 0,
                                    0, -1, ProfileTriggerMode::Disabled}, false);
            for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
            quiesceVirtualController();
            m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Off);
            emit workerEvent(u"Mapping off; virtual controller neutralized"_qs);
        }
        if (m_releaseVjoyRequested.exchange(false)) {
            // vJoyConfig must not compete with this process for Device 1.
            // Normal Mapping Off keeps the acquired device stable for games;
            // an explicit setup transaction is the sole exception.
            quiesceVirtualController();
            vjoy.release();
            m_runtime.vjoyReady = false;
            setVjoyStatus(u"vJoy released for controller verification"_qs);
            m_vjoyReleasedForControlPlane = true;
            emit hardwareStateChanged();
        }
        const std::uint64_t requestedReacquire = m_reacquireInputRequested.load();
        if (requestedReacquire != handledReacquireRequest) {
            // A completed HidHide change must be proven against a brand-new
            // DirectInput open, not the handle that existed before cloaking.
            releaseInput();
            handledReacquireRequest = requestedReacquire;
            m_reacquireInputAcknowledged = requestedReacquire;
            nextDiscovery = now;
            emit workerEvent(u"Physical controller reacquisition requested by HOTAS control plane"_qs);
        }
        wasMappingRequested = mappingRequestedNow;
        if (!device && now >= nextDiscovery) {
            discoverInput();
            nextDiscovery = now + std::chrono::seconds(1);
        }

        const std::uint64_t outputAvailabilityRetry = m_outputAvailabilityRetryGeneration.load(std::memory_order_relaxed);
        const bool retryOutputAvailability = outputAvailabilityRetry != handledOutputAvailabilityRetry;
        if (now >= nextVjoyCheck || retryOutputAvailability) {
            QString status;
            const bool ready = vjoy.checkDevice(configuration.vjoyDeviceId, &status);
            m_runtime.vjoyReady = ready;
            setVjoyOwnershipEvidence(vjoy.lastOwnership(), vjoy.lastAcquireAttempt());
            setVjoyStatus(status);
            nextVjoyCheck = now + std::chrono::seconds(1);
            handledOutputAvailabilityRetry = outputAvailabilityRetry;
            refreshVjoyCapabilities();
        }

        if (!device) {
            m_runtime.mappingEffectiveState = mappingRequestedNow
                ? static_cast<int>(MappingEffectiveState::Suspended)
                : static_cast<int>(MappingEffectiveState::Off);
            QThread::msleep(50);
            continue;
        }

        // Poll on every bounded wake-up. Some DirectInput HID stacks do not
        // reliably signal SetEventNotification for immediate state devices;
        // the event is therefore an optimization, never a gate on live UI
        // state or virtual mapping.
        const DWORD waitResult = inputEvent
            ? WaitForSingleObject(inputEvent, kPhysicalPollIntervalMs)
            : WAIT_TIMEOUT;
        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_TIMEOUT) {
            emit workerEvent(u"Controller wait failed; reconnecting"_qs);
            releaseInput();
            // Do one immediate enumeration after an unexpected wait failure.
            // If Windows has not published the removal/reinsertion yet, the
            // normal discovery schedule below provides the bounded backoff.
            nextDiscovery = std::chrono::steady_clock::now();
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 && inputEvent) ResetEvent(inputEvent);

        const auto started = std::chrono::steady_clock::now();
        const HRESULT pollResult = device->Poll();
        if (pollResult == DIERR_INPUTLOST || pollResult == DIERR_NOTACQUIRED) {
            emit workerEvent(u"Controller input was lost; rediscovering"_qs);
            releaseInput();
            nextDiscovery = std::chrono::steady_clock::now();
            continue;
        }
        if (FAILED(pollResult)) {
            emit workerEvent(u"Controller poll failed: "_qs + inputErrorMessage(pollResult));
            releaseInput();
            nextDiscovery = std::chrono::steady_clock::now();
            continue;
        }
        DIJOYSTATE2 state{};
        const HRESULT readResult = device->GetDeviceState(sizeof(state), &state);
        if (readResult == DIERR_INPUTLOST || readResult == DIERR_NOTACQUIRED) {
            emit workerEvent(u"Controller state was lost; rediscovering"_qs);
            releaseInput();
            nextDiscovery = std::chrono::steady_clock::now();
            continue;
        }
        if (FAILED(readResult)) {
            emit workerEvent(u"Controller disconnected: "_qs + inputErrorMessage(readResult));
            releaseInput();
            nextDiscovery = std::chrono::steady_clock::now();
            continue;
        }

        // Consult the buffered object channel only when DirectInput signaled
        // newly queued data (or after a prior source resolution selected it).
        // The common state path remains one fixed-field read per axis.
        bool bufferedSourceInUse = false;
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            const size_t index = static_cast<size_t>(axis);
            bufferedSourceInUse = bufferedSourceInUse
                || axisAcquisitionMethods[index] == 1
                || axisAcquisitionMethods[index] == 3
                // If event notifications are unavailable, keep draining the
                // bounded buffer while a first unique correlation awaits a
                // second, distinct confirmation.
                || bufferedCorrelationEvidence[index].hasProvisional;
        }
        std::array<bool, kPhysicalAxisCount> bufferedAxisEvents{};
        if (waitResult == WAIT_OBJECT_0 || bufferedSourceInUse) {
            readBufferedAxisEvents(device, axisDescriptors, availableAxes,
                                   &bufferedAxisValues, &bufferedAxisValuesKnown,
                                   &bufferedAxisEvents);
        }

        PhysicalInputReport physicalReport;
        const auto observedAt = std::chrono::steady_clock::now();
        if (m_runtime.axisSourceMonitorRequested.load(std::memory_order_relaxed)) {
            AtomicAxisSourceTelemetry &telemetry = m_runtime.axisSourceTelemetry;
            for (int source = 0; source < kPhysicalAxisCount; ++source) {
                const size_t sourceIndex = static_cast<size_t>(source);
                const LONG value = directInputAxisValue(state, static_cast<PhysicalAxis>(source));
                int movementMagnitude = 0;
                if (!sourceMonitorInitialized) {
                    sourceMonitorPrevious[sourceIndex] = value;
                    sourceMonitorMinimum[sourceIndex] = value;
                    sourceMonitorMaximum[sourceIndex] = value;
                    sourceMonitorChanges[sourceIndex] = 0;
                    sourceMonitorLastChanged[sourceIndex] = observedAt;
                } else {
                    const LONG delta = value - sourceMonitorPrevious[sourceIndex];
                    movementMagnitude = std::abs(delta);
                    if (delta != 0) {
                        sourceMonitorPrevious[sourceIndex] = value;
                        ++sourceMonitorChanges[sourceIndex];
                        sourceMonitorLastChanged[sourceIndex] = observedAt;
                    }
                    sourceMonitorMinimum[sourceIndex] = std::min(sourceMonitorMinimum[sourceIndex], value);
                    sourceMonitorMaximum[sourceIndex] = std::max(sourceMonitorMaximum[sourceIndex], value);
                }
                telemetry.value[sourceIndex].store(value, std::memory_order_relaxed);
                telemetry.observedMinimum[sourceIndex].store(sourceMonitorMinimum[sourceIndex], std::memory_order_relaxed);
                telemetry.observedMaximum[sourceIndex].store(sourceMonitorMaximum[sourceIndex], std::memory_order_relaxed);
                telemetry.changeCount[sourceIndex].store(sourceMonitorChanges[sourceIndex], std::memory_order_relaxed);
                telemetry.recentMovementMagnitude[sourceIndex].store(
                    movementMagnitude, std::memory_order_relaxed);
                telemetry.lastChangeAgeMs[sourceIndex].store(std::chrono::duration_cast<std::chrono::milliseconds>(
                    observedAt - sourceMonitorLastChanged[sourceIndex]).count(), std::memory_order_relaxed);
            }
            sourceMonitorInitialized = true;
            telemetry.available.store(true, std::memory_order_relaxed);
        } else {
            sourceMonitorInitialized = false;
            m_runtime.axisSourceTelemetry.available.store(false, std::memory_order_relaxed);
        }
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            const RuntimeAxisAcquisition &binding = axisAcquisitions[static_cast<size_t>(index)];
            if (!availableAxes[index] || !binding.valid) continue;
            const LONG standardValue = directInputAxisValue(state,
                static_cast<PhysicalAxis>(binding.sourceIndex));
            if (bufferedAxisEvents[static_cast<size_t>(index)]
                && bufferedAxisValuesKnown[static_cast<size_t>(index)]
                // A persisted buffered fallback is a safe starting point on
                // reconnect, not a terminal verdict.  Let the exact same
                // signature-bound correlation promote it to a fixed field
                // when fresh reports provide that proof.
                && axisAcquisitionMethods[static_cast<size_t>(index)] != 3
                && !axisDescriptors[static_cast<size_t>(index)].formattedSourceVerified
                && !manualAcquisitionApplied[static_cast<size_t>(index)]
                && (binding.flags & RuntimeAxisAcquisitionAllowBufferedEvidence) != 0) {
                const NativeAxisDescriptor &descriptor = axisDescriptors[static_cast<size_t>(index)];
                const int correlatedSource = uniqueCorrelatedDirectInputStateField(
                    bufferedAxisValues[static_cast<size_t>(index)], state, binding);
                const bool candidateDisagrees = std::abs(normalizeRuntimeAxisAcquisition(
                        bufferedAxisValues[static_cast<size_t>(index)], binding)
                    - normalizeRuntimeAxisAcquisition(standardValue, binding)) > 0.002F;
                const bool evidenceEligible = axisEvidenceCompatible[static_cast<size_t>(index)]
                    && descriptor.metadataContradiction
                    && descriptor.formattedSourceEvidence
                        == AxisFormattedSourceEvidence::ReportedOffsetCandidate
                    && !descriptor.formattedSourceVerified;
                const bool fixedFieldProven = evidenceEligible
                    && observeBufferedObjectCorrelation(
                        &bufferedCorrelationEvidence[static_cast<size_t>(index)],
                        correlatedSource, bufferedAxisValues[static_cast<size_t>(index)]);
                // Method 3 signals a proven alternate fixed field. The GUI
                // persists it and this worker resumes direct state reads. A
                // single match is provisional; only two distinct, consistent
                // native events can reach this branch. An uncorroborated
                // mismatch retains buffered safety fallback.
                if (fixedFieldProven) {
                    axisAcquisitionMethods[static_cast<size_t>(index)] = 3;
                    m_runtime.axisAcquisitionSource[index] = 3;
                    m_runtime.axisResolvedFormattedSource[index].store(
                        correlatedSource, std::memory_order_relaxed);
                } else if (candidateDisagrees) {
                    axisAcquisitionMethods[static_cast<size_t>(index)] = 1;
                    m_runtime.axisAcquisitionSource[index] = 1;
                }
            }
            const LONG acquiredValue = axisAcquisitionMethods[static_cast<size_t>(index)] != 0
                    && bufferedAxisValuesKnown[static_cast<size_t>(index)]
                ? bufferedAxisValues[static_cast<size_t>(index)] : standardValue;
            physicalReport.axes[index] = normalizeRuntimeAxisAcquisition(acquiredValue, binding);
            const float current = physicalReport.axes[index];
            if (!std::isfinite(lastObservedAxisValues[static_cast<size_t>(index)])) {
                lastObservedAxisValues[static_cast<size_t>(index)] = current;
            } else if (std::abs(current - lastObservedAxisValues[static_cast<size_t>(index)]) > 0.002F) {
                lastObservedAxisValues[static_cast<size_t>(index)] = current;
                lastAxisMovementAt[static_cast<size_t>(index)] = observedAt;
                axisLiveMovementObserved[static_cast<size_t>(index)] = true;
                m_runtime.axisLiveMovementObserved[index] = true;
            }
            m_runtime.axisLastMovementAgeMs[index] = axisLiveMovementObserved[static_cast<size_t>(index)]
                ? std::chrono::duration_cast<std::chrono::milliseconds>(
                    observedAt - lastAxisMovementAt[static_cast<size_t>(index)]).count() : -1;
        }
        for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
            physicalReport.buttons[source] = availableButtons[source]
                && (state.rgbButtons[source] & 0x80U) != 0;
        }
        for (int hat = 0; hat < m_runtime.povCount.load() && hat < kMaximumPhysicalPovs; ++hat) {
            const DWORD raw = state.rgdwPOV[static_cast<size_t>(hat)];
            physicalReport.povs[static_cast<size_t>(hat)] = raw != 0xFFFFFFFFUL && raw < 36000UL
                ? static_cast<int>(raw) : -1;
        }
        physicalMonitor.accept(physicalReport);
        const PhysicalInputSnapshot &physicalSnapshot = physicalMonitor.snapshot();
        ++m_runtime.physicalReportsSinceAcquisition;
        if (observeMeaningfulInput(meaningfulInput, physicalSnapshot, availableAxes, availableButtons,
                                   m_runtime.povCount.load(std::memory_order_relaxed))) {
            latestMeaningfulInputSequence = m_runtime.meaningfulInputSequence.fetch_add(
                1, std::memory_order_relaxed) + 1;
            m_runtime.deviceRigMeaningfulInputSequence[0].store(latestMeaningfulInputSequence,
                                                                  std::memory_order_relaxed);
        }

        // Global mapping controls are intentionally evaluated from the fixed
        // physical snapshot before profile/game routing. The first post-
        // reconnect report only seeds edge state, preventing a held button
        // from fabricating a toggle transition.
        if (!controlPlaneInitialized) {
            latestPhysicalButtons = physicalSnapshot.buttons;
            controlPlaneInitialized = true;
        } else {
            for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
                const MappingControlAction action = activeProfileCache->mappingControls[
                    static_cast<size_t>(source)];
                const bool pressed = physicalSnapshot.buttons[static_cast<size_t>(source)];
                const bool rising = pressed && !latestPhysicalButtons[static_cast<size_t>(source)];
                if (!rising || action == MappingControlAction::None) continue;
                const bool current = m_mappingRequested.load();
                const bool desired = action == MappingControlAction::MappingOn ? true
                    : action == MappingControlAction::MappingOff ? false : !current;
                if (desired != current) {
                    m_mappingRequested = desired;
                    emit workerEvent(QString(u"Button %1: %2"_qs).arg(source + 1)
                        .arg(mappingControlActionLabel(action)));
                }
            }
        }
        const bool mappingRequestedAfterControls = m_mappingRequested.load();
        // Mapping controls are control-plane only. Their raw state remains
        // visible to diagnostics, but no profile trigger, Automation rule, or
        // game route can consume the same report as normal input.
        PhysicalButtonStates routedButtons = physicalSnapshot.buttons;
        for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
            if (activeProfileCache->mappingControls[static_cast<size_t>(source)]
                != MappingControlAction::None) {
                routedButtons[static_cast<size_t>(source)] = false;
            }
        }

        // Physical profile controls are resolved first. Automation then sees
        // exactly this pre-Automation effective profile and physical snapshot;
        // it never reads another Automation's output from this report.
        const AutomationEvaluationResult *automationEffects = nullptr;
        AutomationInputSnapshot automationInput;
        std::chrono::steady_clock::time_point automationStarted;
        bool measuredAutomation = false;
        if (mappingRequestedAfterControls) {
            EffectiveProfileSelection selection;
            if (!profileTriggerSessionActive) {
                profileTriggers.initializeForMapping(*activeProfileCache, routedButtons,
                                                     physicalSnapshot.povs, m_runtime.povCount.load());
                profileTriggerSessionActive = true;
                selection = profileTriggers.effectiveProfile(*activeProfileCache);
            } else {
                selection = profileTriggers.processReport(*activeProfileCache, routedButtons,
                                                           physicalSnapshot.povs, m_runtime.povCount.load());
            }
            const auto profileSwitchStarted = std::chrono::steady_clock::now();
            const bool changed = selectEffectiveProfile(selection, true);
            if (changed) {
                pendingProfileSwitchStarted = profileSwitchStarted;
                for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
                for (AxisCenterResolverState &state : centerResolverStates) state = {};
            }

            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                automationInput.physicalAxes[static_cast<size_t>(axis)] = normalizeCalibrated(
                    physicalSnapshot.axes[static_cast<size_t>(axis)],
                    activeMapping->axes[static_cast<size_t>(axis)].calibration);
                automationInput.axisAvailable[static_cast<size_t>(axis)] = availableAxes[static_cast<size_t>(axis)]
                    && !fixedAxes[static_cast<size_t>(axis)];
            }
            automationInput.buttons = routedButtons;
            automationInput.povs = physicalSnapshot.povs;
            automationInput.povCount = m_runtime.povCount.load();
            automationInput.buttonCount = m_runtime.buttonCount.load();
            automationInput.baseProfileIndex = activeProfileCache->baseProfileIndex;
            automationInput.preAutomationEffectiveProfileIndex = effectiveProfileIndex;
            // Reuse the report's one monotonic timestamp for every temporal
            // Automation condition and action in this pass.
            automationInput.timestamp = started;
            automationStarted = started;
            measuredAutomation = true;
            automationEffects = &automation.evaluate(automationInput);
            applyAutomationMappingControl(automationEffects->mappingControlAction);
            profileTriggers.updateAutomationContributions(automationEffects->profileContributions,
                automationEffects->profileContributionCount,
                static_cast<int>(activeProfileCache->profiles.size()));
            const EffectiveProfileSelection automationSelection = profileTriggers.effectiveProfile(*activeProfileCache);
            const bool automationProfileChanged = selectEffectiveProfile(automationSelection, true);
            if (automationProfileChanged) {
                pendingProfileSwitchStarted = automationStarted;
                for (AdaptiveResponseProcessor &processor : adaptiveProcessors) processor.reset();
                for (AxisCenterResolverState &state : centerResolverStates) state = {};
            }
            m_runtime.automationActiveRuleCount = automationEffects->activeRuleCount;
            for (int rule = 0; rule < kMaximumAutomationRules; ++rule) {
                const bool active = automationEffects->activeRules[static_cast<size_t>(rule)];
                m_runtime.automationRuleActive[static_cast<size_t>(rule)] = active;
                if (active != lastAutomationRuleStates[static_cast<size_t>(rule)]
                    && activeProfileCache->automation
                    && rule < activeProfileCache->automation->ruleCount) {
                    // An active Automation can change scale, offset, clamp,
                    // mix, or override routing. Treat its edge as one mapping
                    // transition, never as continuous input filtering.
                    mappingTransitionRequested = true;
                    emit workerEvent((active ? u"Automation activated: "_qs
                                             : u"Automation cleared: "_qs)
                        + activeProfileCache->automation->ruleNames[static_cast<size_t>(rule)]);
                }
                lastAutomationRuleStates[static_cast<size_t>(rule)] = active;
            }
        } else if (activeProfileCache->automation
                   && activeProfileCache->automation->engineEnabled) {
            // Mapping control automation remains a compact control-plane path
            // while game-output actions are intentionally ignored below.
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                automationInput.physicalAxes[static_cast<size_t>(axis)] = normalizeCalibrated(
                    physicalSnapshot.axes[static_cast<size_t>(axis)],
                    activeMapping->axes[static_cast<size_t>(axis)].calibration);
                automationInput.axisAvailable[static_cast<size_t>(axis)] = availableAxes[static_cast<size_t>(axis)]
                    && !fixedAxes[static_cast<size_t>(axis)];
            }
            automationInput.buttons = routedButtons;
            automationInput.povs = physicalSnapshot.povs;
            automationInput.povCount = m_runtime.povCount.load();
            automationInput.buttonCount = m_runtime.buttonCount.load();
            automationInput.baseProfileIndex = activeProfileCache->baseProfileIndex;
            automationInput.preAutomationEffectiveProfileIndex = effectiveProfileIndex;
            automationInput.timestamp = started;
            automationEffects = &automation.evaluateMappingControls(automationInput);
            applyAutomationMappingControl(automationEffects->mappingControlAction);
        }

        std::array<float, kPhysicalAxisCount> transformedAxes{};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            const RuntimeAdaptiveResponseOverride nextOverlay = automationEffects
                ? automationEffects->adaptiveResponseOverlays[static_cast<size_t>(index)]
                : RuntimeAdaptiveResponseOverride{};
            RuntimeAdaptiveResponseOverride &activeOverlay = activeAdaptiveOverlays[
                static_cast<size_t>(index)];
            if (!sameAdaptiveResponseOverlay(activeOverlay, nextOverlay)) {
                activeOverlay = nextOverlay;
                RuntimeAdaptiveResponseConfig effective = activeMapping->axes[static_cast<size_t>(index)]
                    .adaptiveResponse;
                if (activeOverlay.active) {
                    effective = applyAdaptiveResponseRuntimeOverride(effective, activeOverlay);
                }
                effectiveAdaptiveConfigurations[static_cast<size_t>(index)] = effective;
                adaptiveProcessors[static_cast<size_t>(index)].reset();
                centerResolverStates[static_cast<size_t>(index)] = {};
            }
        }
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (!availableAxes[index]) continue;
            const float raw = physicalSnapshot.axes[index];
            m_runtime.raw[index] = raw;
            const RuntimeAxisMapping &mapping = activeMapping->axes[index];
            const float physicalNormalized = normalizeCalibrated(raw, mapping.calibration);
            const float resolvedNormalized = resolveNormalizedAxisCenter(
                physicalNormalized, mapping, centerResolverStates[static_cast<size_t>(index)]);
            const RuntimeAdaptiveResponseConfig &adaptiveConfiguration =
                effectiveAdaptiveConfigurations[static_cast<size_t>(index)];
            // Maximum Lead is now an output-domain setting. The estimator still
            // produces a bounded physical future candidate; mapped authority is
            // applied below after both physical positions traverse F(x).
            RuntimeAdaptiveResponseConfig physicalPredictionConfiguration = adaptiveConfiguration;
            physicalPredictionConfiguration.maximumLead = 0.50F;
            const AdaptiveResponseTelemetry adaptive = adaptiveProcessors[static_cast<size_t>(index)].process(
                resolvedNormalized, physicalPredictionConfiguration, started);
            const AdaptiveMappedAxisOutput mapped = applyCurveAwareAdaptiveResponse(
                resolvedNormalized, adaptive.predicted, adaptiveConfiguration.enabled,
                adaptiveConfiguration.maximumLead, mapping, hysteresisStates[index]);
            // Diagnostics, predictor, and output share the resolved canonical
            // signal. Raw remains available above for hardware inspection.
            m_runtime.normalized[index] = resolvedNormalized;
            m_runtime.afterDeadzone[index] = mapped.baselineSignalPath.afterDeadzone;
            m_runtime.afterHysteresis[index] = mapped.baselineSignalPath.afterHysteresis;
            m_runtime.afterInversion[index] = mapped.baselineSignalPath.afterInversion;
            m_runtime.curveResponse[index] = mapped.baselineSignalPath.afterCurve;
            m_runtime.transformed[index] = mapped.adaptiveOutput;
            m_runtime.adaptiveEstimated[index] = adaptive.estimated;
            m_runtime.adaptivePredicted[index] = adaptive.predicted;
            m_runtime.adaptiveBaselineMapped[index] = mapped.baselineOutput;
            m_runtime.adaptivePredictedMapped[index] = mapped.predictedMappedOutput;
            m_runtime.adaptiveOutput[index] = mapped.adaptiveOutput;
            m_runtime.adaptiveMappedLead[index] = mapped.mappedLead;
            m_runtime.adaptiveAppliedLead[index] = mapped.appliedLead;
            m_runtime.adaptiveLocalCurveGain[index] = mapped.localCurveGain;
            m_runtime.adaptiveVelocity[index] = adaptive.velocity;
            m_runtime.adaptiveAcceleration[index] = adaptive.acceleration;
            m_runtime.adaptiveHorizonSeconds[index] = adaptive.activeHorizonSeconds;
            m_runtime.adaptiveRequestedLead[index] = adaptive.requestedLead;
            m_runtime.adaptiveCappedLead[index] = adaptive.cappedLead;
            m_runtime.adaptiveEndpointTaper[index] = adaptive.endpointTaper;
            m_runtime.adaptiveLead[index] = adaptive.lead;
            m_runtime.adaptiveConfidence[index] = adaptive.confidence;
            m_runtime.adaptiveMotionIntensity[index] = adaptive.motionIntensity;
            m_runtime.adaptiveVelocityAuthority[index].store(adaptive.velocityAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveDeliberateMotionEvidence[index].store(adaptive.deliberateMotionEvidence, std::memory_order_relaxed);
            m_runtime.adaptiveNormalMotionAuthority[index].store(adaptive.normalMotionAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveRapidMotionAuthority[index].store(adaptive.rapidMotionAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveRapidMotionBlend[index].store(adaptive.rapidMotionBlend, std::memory_order_relaxed);
            m_runtime.adaptiveAccelerationIntent[index].store(adaptive.accelerationIntent, std::memory_order_relaxed);
            m_runtime.adaptiveOnsetAuthority[index].store(adaptive.onsetAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveSustainedEvidence[index].store(adaptive.sustainedEvidence, std::memory_order_relaxed);
            m_runtime.adaptiveSustainedAuthority[index].store(adaptive.sustainedAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveMotionUrgency[index].store(adaptive.motionUrgency, std::memory_order_relaxed);
            m_runtime.adaptiveHorizonExtensionEligibility[index].store(adaptive.horizonExtensionEligibility, std::memory_order_relaxed);
            m_runtime.adaptiveNormalMaximumHorizonSeconds[index].store(adaptive.normalMaximumHorizonSeconds, std::memory_order_relaxed);
            m_runtime.adaptiveAllowedMaximumHorizonSeconds[index].store(adaptive.allowedMaximumHorizonSeconds, std::memory_order_relaxed);
            m_runtime.adaptiveTurningPointConfidence[index].store(adaptive.turningPointConfidence, std::memory_order_relaxed);
            m_runtime.adaptiveEstimatedTimeToTurnSeconds[index].store(adaptive.estimatedTimeToTurnSeconds, std::memory_order_relaxed);
            m_runtime.adaptiveEstimatedRemainingTravel[index].store(adaptive.estimatedRemainingTravel, std::memory_order_relaxed);
            m_runtime.adaptiveTurningPointHorizonLimitSeconds[index].store(adaptive.turningPointHorizonLimitSeconds, std::memory_order_relaxed);
            m_runtime.adaptiveTurningPointLeadLimit[index].store(adaptive.turningPointLeadLimit, std::memory_order_relaxed);
            m_runtime.adaptiveReacquisitionAuthority[index].store(adaptive.reacquisitionAuthority, std::memory_order_relaxed);
            m_runtime.adaptiveMotionState[index] = static_cast<int>(adaptive.state);
            m_runtime.adaptiveReversing[index] = adaptive.reversal;
            m_runtime.adaptiveSafetyLimited[index] = adaptive.safetyLimited || mapped.leadLimited;
            m_runtime.adaptiveDeadzoneAuthorityBlocked[index] = mapped.deadzoneAuthorityBlocked;
            m_runtime.adaptiveLeadLimited[index] = mapped.leadLimited;
            m_runtime.adaptiveHighLocalCurveGain[index] = mapped.highLocalCurveGain;
            m_runtime.adaptiveReversalCount[index] = adaptiveProcessors[static_cast<size_t>(index)].reversalCount();
            m_runtime.adaptiveSafetyClampCount[index] = adaptiveProcessors[static_cast<size_t>(index)].safetyClampCount();
            m_runtime.adaptiveRuntimeEnabled[index] = adaptiveConfiguration.enabled;
            m_runtime.adaptiveRuntimeModel[index] = static_cast<int>(adaptiveConfiguration.model);
            m_runtime.adaptiveRuntimeMaximumHorizonSeconds[index] = adaptiveConfiguration.maximumHorizonSeconds;
            m_runtime.adaptiveRuntimeMaximumLead[index] = adaptiveConfiguration.maximumLead;
            m_runtime.adaptiveRuntimeVelocityResponse[index] = adaptiveConfiguration.velocityResponse;
            m_runtime.adaptiveRuntimeAccelerationResponse[index] = adaptiveConfiguration.accelerationResponse;
            m_runtime.adaptiveRuntimeMotionSensitivity[index] = adaptiveConfiguration.motionSensitivity;
            m_runtime.adaptiveRuntimeNoiseRejection[index] = adaptiveConfiguration.noiseRejection;
            m_runtime.adaptiveRuntimeReversalDetection[index] = adaptiveConfiguration.reversalDetection;
            m_runtime.adaptiveRuntimeReversalResponse[index] = adaptiveConfiguration.reversalResponse;
            m_runtime.adaptiveRuntimeDecelerationResponse[index] = adaptiveConfiguration.decelerationResponse;
            m_runtime.adaptiveRuntimeSettlingResponse[index] = adaptiveConfiguration.settlingResponse;
            m_runtime.adaptiveRuntimeEndpointTaper[index] = adaptiveConfiguration.endpointTaper;
            m_runtime.adaptiveRuntimeOnsetAssist[index].store(adaptiveConfiguration.onsetAssist, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeOnsetCap[index].store(adaptiveConfiguration.onsetCap, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeSustainedAssist[index].store(adaptiveConfiguration.sustainedAssist, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeSustainedCap[index].store(adaptiveConfiguration.sustainedCap, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeHorizonExtension[index].store(adaptiveConfiguration.horizonExtension, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeHorizonExtensionCapSeconds[index].store(adaptiveConfiguration.horizonExtensionCapSeconds, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeTurningPointProtection[index].store(adaptiveConfiguration.turningPointProtection, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeTurningPointMargin[index].store(adaptiveConfiguration.turningPointMargin, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeNormalMovementResponse[index].store(adaptiveConfiguration.normalMovementResponse, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeRapidMovementResponse[index].store(adaptiveConfiguration.rapidMovementResponse, std::memory_order_relaxed);
            m_runtime.adaptiveRuntimeEngagementSensitivity[index].store(adaptiveConfiguration.engagementSensitivity, std::memory_order_relaxed);
            const RuntimeAdaptiveResponseOverride &overlay = activeAdaptiveOverlays[static_cast<size_t>(index)];
            m_runtime.adaptiveAutomationOverlayActive[index] = overlay.active;
            m_runtime.adaptiveAutomationOverlayProperties[index] = overlay.properties;
            publishAdaptiveAxisTelemetry(m_runtime, index, raw, resolvedNormalized, mapped, adaptive,
                                         adaptiveConfiguration,
                                         adaptiveProcessors[static_cast<size_t>(index)].reversalCount(),
                                         adaptiveProcessors[static_cast<size_t>(index)].safetyClampCount(),
                                         overlay);
            transformedAxes[static_cast<size_t>(index)] = mapped.adaptiveOutput;
        }
        if (automationEffects) {
            automation.applyAxisActions(automationInput, transformedAxes);
            // The displayed cost covers the complete compiled Automation pass,
            // including deterministic axis composition, but excludes vJoy I/O.
            if (measuredAutomation) {
                const auto automationFinished = std::chrono::steady_clock::now();
                m_runtime.automationEvaluationUs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        automationFinished - automationStarted).count());
            }
            for (int index = 0; index < kPhysicalAxisCount; ++index) {
                m_runtime.transformed[index] = transformedAxes[static_cast<size_t>(index)];
            }
        }
        // Every virtual vJoy axis receives a deliberate parking value before
        // mapped physical routes are overlaid. The fixed-size plan retains the
        // existing change-driven output cadence and keeps configuration/UI
        // work outside this real-time path.
        std::array<bool, kPhysicalAxisCount> routableAxes{};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            routableAxes[static_cast<size_t>(index)] = availableAxes[static_cast<size_t>(index)]
                && !fixedAxes[static_cast<size_t>(index)];
        }
        const VirtualAxisOutputPlan axisOutputPlan = buildVirtualAxisOutputPlan(
            *activeMapping, routableAxes, transformedAxes, outputLayoutAxes,
            configuration.disabledAxisValue);
        std::array<float, kVirtualAxisSlotCount> output = axisOutputPlan.values;
        virtualAxisSources = axisOutputPlan.sourceIndexes;
        const std::uint64_t transitionNowUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                started.time_since_epoch()).count());
        if (mappingTransitionRequested) {
            // The last successful vJoy write is the only valid anchor for a
            // mid-transition reconfiguration. Never substitute an old curve's
            // theoretical result: rapid toggles remain continuous this way.
            for (int target = 1; target < kVirtualAxisSlotCount; ++target) {
                const int source = virtualAxisSources[static_cast<size_t>(target)];
                const float currentInput = source >= 0
                    ? physicalSnapshot.axes[static_cast<size_t>(source)] : 0.0F;
                axisTransitions.begin(static_cast<size_t>(target),
                    lastActualVirtualValues[static_cast<size_t>(target)],
                    output[static_cast<size_t>(target)], currentInput, source, transitionNowUs,
                    activeMapping->curveTransitionSmoothing);
            }
            mappingTransitionRequested = false;
        }
        for (int target = 1; target < kVirtualAxisSlotCount; ++target) {
            const int source = virtualAxisSources[static_cast<size_t>(target)];
            const float currentInput = source >= 0
                ? physicalSnapshot.axes[static_cast<size_t>(source)] : 0.0F;
            output[static_cast<size_t>(target)] = axisTransitions.apply(
                static_cast<size_t>(target), output[static_cast<size_t>(target)], currentInput,
                source, transitionNowUs);
        }
        const float parkedAxisValue = sanitizedDisabledAxisValue(configuration.disabledAxisValue);
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            m_runtime.virtualValues[index] = parkedAxisValue;
        }
        for (int target = 1; target < static_cast<int>(output.size()); ++target) {
            const int source = virtualAxisSources[target];
            if (source >= 0) m_runtime.virtualValues[source] = output[target];
        }

        // This DirectInput monitor drives the UI even when vJoy is disabled
        // and even when Mapping Active is false.
        for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
            const bool pressed = physicalSnapshot.buttons[source];
            latestPhysicalButtons[source] = pressed;
            m_runtime.physicalButtonPressed[source] = pressed;
        }
        if (physicalSnapshot.lastChangedButton > 0) {
            m_runtime.lastPhysicalButton = physicalSnapshot.lastChangedButton;
            m_runtime.lastPhysicalButtonTarget = runtimeButtonTargets[
                static_cast<size_t>(physicalSnapshot.lastChangedButton - 1)];
        }
        for (int hat = 0; hat < kMaximumPhysicalPovs; ++hat) {
            latestPovValues[static_cast<size_t>(hat)] = physicalSnapshot.povs[static_cast<size_t>(hat)];
            m_runtime.povValues[static_cast<size_t>(hat)] = physicalSnapshot.povs[static_cast<size_t>(hat)];
        }

        const bool mappingRequested = m_mappingRequested.load();
        if (mappingRequested && !m_runtime.mappingActive.load()
            && (retryOutputAvailability || std::chrono::steady_clock::now() >= nextVjoyAcquire)) {
            QString status;
            if (vjoy.acquire(configuration.vjoyDeviceId, &status)) {
                // A newly selected pre-provisioned device receives a complete
                // explicit neutral baseline before any mapped report is
                // published. This does not trust vJoy reset defaults and is
                // reached only at an acquire boundary, never per report.
                refreshVjoyCapabilities();
                quiesceVirtualController();
                lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
                lastNativePovValues.fill(-2);
                lastSignalFlowNativePovValues.fill(-2);
                m_runtime.mappingActive = true;
                m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Active);
                m_runtime.outputNeutralized = false;
                m_runtime.vjoyReady = true;
                setVjoyOwnershipEvidence(vjoy.lastOwnership(), vjoy.lastAcquireAttempt());
                setVjoyStatus(status);
                emit workerEvent(u"Mapping active"_qs);
            } else {
                m_runtime.vjoyReady = false;
                m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Suspended);
                setVjoyOwnershipEvidence(vjoy.lastOwnership(), vjoy.lastAcquireAttempt());
                setVjoyStatus(status);
                nextVjoyAcquire = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
        if (!mappingRequested && (m_runtime.mappingActive.load()
            || !m_runtime.outputNeutralized.load())) {
            quiesceVirtualController();
            m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Off);
            emit workerEvent(u"Mapping off; virtual controller neutralized"_qs);
        }
        if (m_runtime.mappingActive.load()) {
            bool outputChanged = false;
            for (int target = 1; target < static_cast<int>(output.size()); ++target) {
                if (!vjoyAxisAvailable[static_cast<size_t>(target)]
                    || !outputLayoutAxes[static_cast<size_t>(target)]) continue;
                const float desired = output[target];
                if (std::isfinite(lastVirtualValues[target])
                    && std::abs(desired - lastVirtualValues[target]) < 0.00001F) {
                    continue;
                }
                if (vjoy.setAxis(static_cast<VirtualAxis>(target), desired)) {
                    lastVirtualValues[target] = desired;
                    lastActualVirtualValues[target] = desired;
                    const int source = virtualAxisSources[target];
                    if (source >= 0) m_runtime.virtualValues[source] = desired;
                    ++m_runtime.vjoyWrites;
                    outputChanged = true;
                }
            }

            VirtualButtonStates desiredButtons = activeMapping->signalFlowTopologyCompiled
                ? mapSignalFlowDigitalStates(latestPhysicalButtons, latestPovValues,
                    m_runtime.povCount.load(), *activeMapping, runtimeButtonTargets,
                    runtimePovTargets, vjoyButtonCapacity)
                : mapButtonStates(latestPhysicalButtons, runtimeButtonTargets, vjoyButtonCapacity);
            if (!activeMapping->signalFlowTopologyCompiled) {
                mapPovStates(desiredButtons, latestPovValues, m_runtime.povCount.load(),
                             runtimePovTargets, vjoyButtonCapacity);
            }
            if (automationEffects) {
                for (int target = 1; target <= vjoyButtonCapacity; ++target) {
                    desiredButtons[static_cast<size_t>(target)] = desiredButtons[static_cast<size_t>(target)]
                        || automationEffects->heldButtons[static_cast<size_t>(target)]
                        || automationEffects->toggledButtons[static_cast<size_t>(target)]
                        || automationEffects->pulsedButtons[static_cast<size_t>(target)];
                }
            }
            for (int target = 1; target <= kMaximumVirtualButtons; ++target) {
                const bool desired = target <= vjoyButtonCapacity && desiredButtons[target];
                if (desired == lastVirtualButtonStates[target]) continue;
                if (target <= vjoyButtonCapacity && vjoy.setButton(target, desired)) {
                    lastVirtualButtonStates[target] = desired;
                    m_runtime.virtualButtonPressed[target - 1] = desired;
                    ++m_runtime.vjoyWrites;
                    outputChanged = true;
                }
            }
            // Native POV passthrough is deliberately separate from
            // direction-to-button/profile-control handling. Canonical Signal
            // Flow supports one physical hat fanning out to multiple vJoy POV
            // endpoints; legacy configurations retain their compact binding
            // loop until topology reconciliation has occurred.
            const int nativePovHats = std::min(m_runtime.povCount.load(), kMaximumPhysicalPovs);
            if (activeMapping->signalFlowTopologyCompiled) {
                const int routeCount = std::clamp(activeMapping->signalFlowNativePovRouteCount, 0,
                                                   kMaximumRuntimeSignalFlowNativePovRoutes);
                for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
                    const RuntimeSignalFlowNativePovRoute &route = activeMapping->signalFlowNativePovRoutes[
                        static_cast<size_t>(routeIndex)];
                    const int source = route.sourcePov;
                    const int target = route.destinationIndex;
                    const NativePovTargetType type = static_cast<NativePovTargetType>(route.destinationType);
                    const bool targetAvailable = type == NativePovTargetType::Continuous
                        ? target >= 1 && target <= vjoyContinuousPovCapacity
                        : type == NativePovTargetType::Discrete
                            && target >= 1 && target <= vjoyDiscretePovCapacity;
                    if (source < 0 || source >= nativePovHats || !targetAvailable) continue;
                    const int desired = latestPovValues[static_cast<size_t>(source)];
                    if (desired == lastSignalFlowNativePovValues[static_cast<size_t>(routeIndex)]) continue;
                    const NativePovBinding binding{true, type, target};
                    if (vjoy.setPov(binding, desired)) {
                        lastSignalFlowNativePovValues[static_cast<size_t>(routeIndex)] = desired;
                        ++m_runtime.vjoyWrites;
                        outputChanged = true;
                    }
                }
            } else {
                for (int hat = 0; hat < nativePovHats; ++hat) {
                    const NativePovBinding &binding = activeProfileCache->nativePovBindings[static_cast<size_t>(hat)];
                    const bool targetAvailable = binding.targetType == NativePovTargetType::Continuous
                        ? binding.targetIndex <= vjoyContinuousPovCapacity
                        : binding.targetType == NativePovTargetType::Discrete
                            && binding.targetIndex <= vjoyDiscretePovCapacity;
                    if (!binding.enabled || !targetAvailable) continue;
                    const int desired = latestPovValues[static_cast<size_t>(hat)];
                    if (desired == lastNativePovValues[static_cast<size_t>(hat)]) continue;
                    if (vjoy.setPov(binding, desired)) {
                        lastNativePovValues[static_cast<size_t>(hat)] = desired;
                        ++m_runtime.vjoyWrites;
                        outputChanged = true;
                    }
                }
            }
            if (outputChanged && latestMeaningfulInputSequence > lastPublishedMeaningfulInputSequence) {
                m_runtime.deviceRigMeaningfulOutputSequence[0].store(latestMeaningfulInputSequence,
                                                                       std::memory_order_relaxed);
                lastPublishedMeaningfulInputSequence = latestMeaningfulInputSequence;
            }
        }

        if (pendingProfileSwitchStarted) {
            // Software-side profile-control latency: this report was observed,
            // selected a cached runtime profile, transformed current axes,
            // reconciled virtual buttons, and reached the vJoy publication
            // path above. USB and driver scheduling are intentionally out of
            // scope for this in-process metric.
            m_runtime.lastProfileSwapUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - *pendingProfileSwitchStarted).count());
            pendingProfileSwitchStarted.reset();
        }

        const auto finished = std::chrono::steady_clock::now();
        const auto latency = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        ++processedReports;
        latencyTotal += latency;
        m_runtime.inputReports = processedReports;
        m_runtime.latencyCurrentUs = latency;
        m_runtime.latencyAverageUs = latencyTotal / processedReports;
        const size_t latencySlot = static_cast<size_t>(latencySampleSequence
            % kLatencyTelemetrySamples);
        m_runtime.latencySamples[latencySlot].store(latency, std::memory_order_release);
        ++latencySampleSequence;
        m_runtime.latencySampleCount.store(std::min<std::uint64_t>(
            latencySampleSequence, kLatencyTelemetrySamples), std::memory_order_release);
        std::uint64_t peak = m_runtime.latencyPeakUs.load();
        while (latency > peak && !m_runtime.latencyPeakUs.compare_exchange_weak(peak, latency)) {}
    }

    quiesceVirtualController();
    releaseInput();
    vjoy.release();
}

void MappingWorker::runDeviceRig(IDirectInput8W *directInput)
{
    // One worker owns a bounded collection of input and output sessions.
    // Every QString/identity lookup and every DirectInput/vJoy capability
    // query happens at an acquisition/configuration boundary; report work is
    // fixed-array state plus change-driven driver writes.
    MapperConfiguration configuration = configurationCopy();
    CompiledDeviceRigRuntime plan = compileDeviceRigRuntime(configuration,
        configuration.activeDeviceRigId, configuration.activeProfileId);
    const quint64 appliedVersion = m_configurationVersion.load();

    struct InputSession {
        const CompiledDeviceRigMember *member = nullptr;
        LPDIRECTINPUTDEVICE8W device = nullptr;
        HANDLE inputEvent = nullptr;
        std::array<bool, kPhysicalAxisCount> availableAxes{};
        std::array<NativeAxisDescriptor, kPhysicalAxisCount> axisDescriptors{};
        std::array<RuntimeAxisAcquisition, kPhysicalAxisCount> axisAcquisitions{};
        std::array<bool, kPhysicalAxisCount> manualAcquisitionApplied{};
        std::array<LONG, kPhysicalAxisCount> bufferedAxisValues{};
        std::array<bool, kPhysicalAxisCount> bufferedAxisValuesKnown{};
        std::array<BufferedObjectCorrelationEvidence, kPhysicalAxisCount> bufferedCorrelationEvidence{};
        std::array<int, kPhysicalAxisCount> axisAcquisitionMethods{};
        std::array<bool, kPhysicalAxisCount> axisEvidenceCompatible{};
        std::array<float, kPhysicalAxisCount> lastObservedAxisValues{};
        std::array<std::chrono::steady_clock::time_point, kPhysicalAxisCount> lastAxisMovementAt{};
        std::array<bool, kPhysicalAxisCount> axisLiveMovementObserved{};
        std::array<LONG, kPhysicalAxisCount> sourceMonitorPrevious{};
        std::array<LONG, kPhysicalAxisCount> sourceMonitorMinimum{};
        std::array<LONG, kPhysicalAxisCount> sourceMonitorMaximum{};
        std::array<std::uint64_t, kPhysicalAxisCount> sourceMonitorChanges{};
        std::array<std::chrono::steady_clock::time_point, kPhysicalAxisCount> sourceMonitorLastChanged{};
        bool sourceMonitorInitialized = false;
        std::array<bool, kMaximumPhysicalButtons> availableButtons{};
        PhysicalInputMonitor monitor;
        std::array<AxisHysteresisState, kPhysicalAxisCount> hysteresis{};
        std::array<AxisCenterResolverState, kPhysicalAxisCount> centers{};
        std::array<AdaptiveResponseProcessor, kPhysicalAxisCount> adaptive{};
        AutomationRuntime automation;
        AutomationEvaluationResult automationEffects{};
        std::array<RuntimeAdaptiveResponseOverride, kPhysicalAxisCount> activeAutomationOverlays{};
        std::array<float, kPhysicalAxisCount> transformed{};
        std::array<int, kMaximumPhysicalPovs> lastNativePovs{};
        MeaningfulInputEvidence meaningfulInput;
        quint64 latestMeaningfulInputSequence = 0;
        int axisCount = 0;
        int buttonCount = 0;
        int povCount = 0;
        bool connected = false;
        std::chrono::steady_clock::time_point nextDiscovery{};

        InputSession()
        {
            lastNativePovs.fill(-2);
            lastObservedAxisValues.fill(std::numeric_limits<float>::quiet_NaN());
        }
    };
    struct OutputSession {
        const CompiledDeviceRigOutput *configured = nullptr;
        VJoyAdapter vjoy;
        std::array<bool, kVirtualAxisSlotCount> axes{};
        std::array<float, kVirtualAxisSlotCount> lastAxes{};
        VirtualButtonStates lastButtons{};
        std::array<std::array<int, kMaximumPhysicalPovs + 1>, 3> lastNativePovs{};
        int buttonCapacity = 0;
        int continuousPovCapacity = 0;
        int discretePovCapacity = 0;
        bool ready = false;
        bool acquired = false;
        std::chrono::steady_clock::time_point nextCheck{};

        OutputSession()
        {
            lastAxes.fill(std::numeric_limits<float>::quiet_NaN());
            for (auto &values : lastNativePovs) values.fill(-2);
        }
    };

    std::array<InputSession, kMaximumDeviceRigMembers> inputs{};
    std::array<OutputSession, kMaximumDeviceRigOutputs> outputs{};
    m_runtime.activeOutputVjoyDeviceId.store(
        plan.outputCount > 0 ? plan.outputs.front().vjoyDeviceId : 0, std::memory_order_relaxed);
    m_runtime.outputReportsSucceeding.store(false, std::memory_order_relaxed);
    m_runtime.successfulOutputReportSequence.store(0, std::memory_order_relaxed);
    m_runtime.lastSuccessfulOutputReportMs.store(0, std::memory_order_relaxed);
    m_runtime.outputWriteFailures.store(0, std::memory_order_relaxed);
    for (std::atomic<float> &value : m_runtime.mappedOutputAxes) {
        value.store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
    }
    for (std::atomic_bool &pressed : m_runtime.mappedOutputButtons) {
        pressed.store(false, std::memory_order_relaxed);
    }
    m_runtime.meaningfulInputSequence.store(0, std::memory_order_relaxed);
    for (std::atomic_uint64_t &sequence : m_runtime.deviceRigMeaningfulInputSequence) {
        sequence.store(0, std::memory_order_relaxed);
    }
    for (std::atomic_uint64_t &sequence : m_runtime.deviceRigMeaningfulOutputSequence) {
        sequence.store(0, std::memory_order_relaxed);
    }
    for (std::atomic_uint64_t &reports : m_runtime.deviceRigInputReports) {
        reports.store(0, std::memory_order_relaxed);
    }
    for (std::atomic_uint64_t &writes : m_runtime.deviceRigOutputWrites) {
        writes.store(0, std::memory_order_relaxed);
    }
    for (int member = 0; member < kMaximumDeviceRigMembers; ++member) {
        m_runtime.deviceRigMemberPhysicalConnected[static_cast<size_t>(member)].store(
            false, std::memory_order_relaxed);
        for (std::atomic_bool &pressed :
             m_runtime.deviceRigMemberPhysicalButtonPressed[static_cast<size_t>(member)]) {
            pressed.store(false, std::memory_order_relaxed);
        }
        for (std::atomic_int &pov :
             m_runtime.deviceRigMemberPovValues[static_cast<size_t>(member)]) {
            pov.store(-1, std::memory_order_relaxed);
        }
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            m_runtime.deviceRigMemberAxisAcquisitionSource[static_cast<size_t>(member)]
                [static_cast<size_t>(axis)].store(-1, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisResolvedFormattedSource[static_cast<size_t>(member)]
                [static_cast<size_t>(axis)].store(-1, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisLiveMovementObserved[static_cast<size_t>(member)]
                [static_cast<size_t>(axis)].store(false, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisLastMovementAgeMs[static_cast<size_t>(member)]
                [static_cast<size_t>(axis)].store(-1, std::memory_order_relaxed);
            AtomicAxisSourceTelemetry &sourceTelemetry = m_runtime.deviceRigMemberAxisSourceTelemetry[
                static_cast<size_t>(member)];
            sourceTelemetry.value[static_cast<size_t>(axis)].store(0, std::memory_order_relaxed);
            sourceTelemetry.observedMinimum[static_cast<size_t>(axis)].store(0, std::memory_order_relaxed);
            sourceTelemetry.observedMaximum[static_cast<size_t>(axis)].store(0, std::memory_order_relaxed);
            sourceTelemetry.changeCount[static_cast<size_t>(axis)].store(0, std::memory_order_relaxed);
            sourceTelemetry.recentMovementMagnitude[static_cast<size_t>(axis)].store(0, std::memory_order_relaxed);
            sourceTelemetry.lastChangeAgeMs[static_cast<size_t>(axis)].store(-1, std::memory_order_relaxed);
        }
        m_runtime.deviceRigMemberAxisSourceTelemetry[static_cast<size_t>(member)].available.store(
            false, std::memory_order_relaxed);
    }
    for (int index = 0; index < plan.memberCount; ++index) {
        inputs[static_cast<size_t>(index)].member = &plan.members[static_cast<size_t>(index)];
        inputs[static_cast<size_t>(index)].automation.setCompiled(
            plan.members[static_cast<size_t>(index)].automation.get());
    }
    for (int index = 0; index < plan.outputCount; ++index) {
        outputs[static_cast<size_t>(index)].configured = &plan.outputs[static_cast<size_t>(index)];
    }

    const auto clearPrimarySnapshot = [&] {
        for (std::atomic_bool &axis : m_runtime.axisAvailable) axis = false;
        for (std::atomic_int &source : m_runtime.axisAcquisitionSource) source = -1;
        for (std::atomic_int &source : m_runtime.axisResolvedFormattedSource) source = -1;
        for (std::atomic_bool &button : m_runtime.buttonAvailable) button = false;
        for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
        for (std::atomic_bool &button : m_runtime.physicalButtonPressed) button = false;
        m_runtime.axisCount = 0;
        m_runtime.buttonCount = 0;
        m_runtime.povCount = 0;
        m_runtime.lastPhysicalButton = 0;
        m_runtime.lastPhysicalButtonTarget = 0;
    };
    const auto clearMemberPhysicalSnapshot = [this](int member) {
        if (member < 0 || member >= kMaximumDeviceRigMembers) return;
        const size_t memberIndex = static_cast<size_t>(member);
        m_runtime.deviceRigMemberPhysicalConnected[memberIndex].store(
            false, std::memory_order_relaxed);
        for (std::atomic_bool &pressed : m_runtime.deviceRigMemberPhysicalButtonPressed[memberIndex]) {
            pressed.store(false, std::memory_order_relaxed);
        }
        for (std::atomic_int &pov : m_runtime.deviceRigMemberPovValues[memberIndex]) {
            pov.store(-1, std::memory_order_relaxed);
        }
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            m_runtime.deviceRigMemberAxisAcquisitionSource[memberIndex][static_cast<size_t>(axis)]
                .store(-1, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisResolvedFormattedSource[memberIndex][static_cast<size_t>(axis)]
                .store(-1, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisLiveMovementObserved[memberIndex][static_cast<size_t>(axis)]
                .store(false, std::memory_order_relaxed);
            m_runtime.deviceRigMemberAxisLastMovementAgeMs[memberIndex][static_cast<size_t>(axis)]
                .store(-1, std::memory_order_relaxed);
        }
        m_runtime.deviceRigMemberAxisSourceTelemetry[memberIndex].available.store(
            false, std::memory_order_relaxed);
    };
    const auto releaseInput = [](InputSession &session) {
        if (session.device) {
            session.device->Unacquire();
            session.device->SetEventNotification(nullptr);
            session.device->Release();
            session.device = nullptr;
        }
        if (session.inputEvent) {
            CloseHandle(session.inputEvent);
            session.inputEvent = nullptr;
        }
        session.availableAxes.fill(false);
        session.axisDescriptors = {};
        session.axisAcquisitions = {};
        session.manualAcquisitionApplied.fill(false);
        session.bufferedAxisValuesKnown.fill(false);
        session.bufferedCorrelationEvidence = {};
        session.axisAcquisitionMethods.fill(0);
        session.axisEvidenceCompatible.fill(false);
        session.lastObservedAxisValues.fill(std::numeric_limits<float>::quiet_NaN());
        session.axisLiveMovementObserved.fill(false);
        session.sourceMonitorInitialized = false;
        session.availableButtons.fill(false);
        session.monitor.disconnect();
        session.connected = false;
        session.axisCount = 0;
        session.buttonCount = 0;
        session.povCount = 0;
        session.hysteresis = {};
        session.centers = {};
        for (AdaptiveResponseProcessor &processor : session.adaptive) processor.reset();
        session.automation.reset();
        session.activeAutomationOverlays = {};
        session.transformed.fill(0.0F);
        session.lastNativePovs.fill(-2);
        session.meaningfulInput = {};
        session.latestMeaningfulInputSequence = 0;
    };
    const auto releaseOutput = [](OutputSession &output) {
        output.vjoy.release();
        output.acquired = false;
        output.lastAxes.fill(std::numeric_limits<float>::quiet_NaN());
        output.lastButtons.fill(false);
        for (auto &values : output.lastNativePovs) values.fill(-2);
    };
    const auto quiesceOutput = [this, &outputs](OutputSession &output) {
        const auto outputIndex = static_cast<size_t>(&output - outputs.data());
        if (!output.vjoy.acquired()) return;
        for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
            if (output.axes[static_cast<size_t>(axis)]
                && output.vjoy.setAxis(static_cast<VirtualAxis>(axis), 0.0F)) {
                ++m_runtime.vjoyWrites;
                ++m_runtime.deviceRigOutputWrites[outputIndex];
            }
        }
        for (int button = 1; button <= output.buttonCapacity; ++button) {
            if (output.vjoy.setButton(button, false)) {
                ++m_runtime.vjoyWrites;
                ++m_runtime.deviceRigOutputWrites[outputIndex];
            }
        }
        for (int pov = 1; pov <= output.continuousPovCapacity; ++pov) {
            if (output.vjoy.centerContinuousPov(pov)) {
                ++m_runtime.vjoyWrites;
                ++m_runtime.deviceRigOutputWrites[outputIndex];
            }
        }
        for (int pov = 1; pov <= output.discretePovCapacity; ++pov) {
            if (output.vjoy.centerDiscretePov(pov)) {
                ++m_runtime.vjoyWrites;
                ++m_runtime.deviceRigOutputWrites[outputIndex];
            }
        }
        output.lastAxes.fill(0.0F);
        output.lastButtons.fill(false);
        for (auto &values : output.lastNativePovs) values.fill(-1);
    };
    const auto refreshOutput = [this](OutputSession &output, QString *status, bool publishDiagnostics) {
        if (!output.configured) return false;
        QString checkStatus;
        if (!output.vjoy.checkDevice(output.configured->vjoyDeviceId, &checkStatus)) {
            if (publishDiagnostics) {
                setVjoyOwnershipEvidence(output.vjoy.lastOwnership(), output.vjoy.lastAcquireAttempt());
            }
            output.ready = false;
            if (status) *status = checkStatus;
            return false;
        }
        if (publishDiagnostics) {
            setVjoyOwnershipEvidence(output.vjoy.lastOwnership(), output.vjoy.lastAcquireAttempt());
        }
        output.axes = output.vjoy.axisCapabilities(output.configured->vjoyDeviceId, nullptr);
        output.buttonCapacity = output.vjoy.buttonCapacity(output.configured->vjoyDeviceId, nullptr);
        const VJoyAdapter::PovCapabilities povs = output.vjoy.povCapabilities(
            output.configured->vjoyDeviceId, nullptr);
        output.continuousPovCapacity = povs.continuous;
        output.discretePovCapacity = povs.discrete;
        bool sufficient = true;
        for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
            sufficient = sufficient && (!output.configured->requiredAxes[static_cast<size_t>(axis)]
                || output.axes[static_cast<size_t>(axis)]);
        }
        sufficient = sufficient && output.buttonCapacity >= output.configured->requiredButtons
            && output.continuousPovCapacity >= output.configured->requiredContinuousPovs
            && output.discretePovCapacity >= output.configured->requiredDiscretePovs;
        output.ready = sufficient;
        if (status) {
            *status = sufficient ? checkStatus
                : QString(u"Device %1 does not meet this Device Rig output layout's requirements."_qs)
                      .arg(output.configured->vjoyDeviceId);
        }
        return sufficient;
    };
    const auto discoverInput = [&](InputSession &session) {
        if (!session.member) return false;
        const std::optional<DirectInputDevice> selected = selectDeviceByPersistedId(
            directInput, session.member->directInputId);
        if (!selected) return false;
        LPDIRECTINPUTDEVICE8W device = nullptr;
        const HRESULT created = directInput->CreateDevice(selected->guid, &device, nullptr);
        if (FAILED(created)) return false;
        if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))
            || FAILED(device->SetCooperativeLevel(GetDesktopWindow(),
                                                  DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
            device->Release();
            return false;
        }
        ObjectEnumerationContext objects{device, &session.availableAxes, &session.availableButtons,
                                         &session.axisDescriptors};
        device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
        const auto record = std::find_if(configuration.savedControllers.cbegin(),
            configuration.savedControllers.cend(), [&session](const SavedControllerRecord &candidate) {
                return session.member && candidate.id == session.member->controllerRecordId;
            });
        session.axisEvidenceCompatible.fill(false);
        if (record != configuration.savedControllers.cend()) {
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const size_t index = static_cast<size_t>(axis);
                session.axisEvidenceCompatible[index] = directInputAxisDescriptorSignatureMatches(
                    session.axisDescriptors[index], record->axisDescriptors[index]);
                reuseVerifiedFormattedSource(&session.axisDescriptors[index],
                                             record->axisDescriptors[index]);
                if (session.axisEvidenceCompatible[index]
                    && record->axisDescriptors[index].acquisitionMethod == 1) {
                    session.axisAcquisitionMethods[index] = 1;
                }
            }
        }
        static const std::array<AxisAcquisitionOverride, kPhysicalAxisCount> noOverrides{};
        session.axisAcquisitions = compileRuntimeAxisAcquisitions(session.axisDescriptors,
            record != configuration.savedControllers.cend() ? record->axisAcquisitionOverrides : noOverrides,
            &session.manualAcquisitionApplied);
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            session.availableAxes[static_cast<size_t>(axis)] =
                session.axisAcquisitions[static_cast<size_t>(axis)].valid;
        }
        configureDirectInputBufferedEvents(device);
        session.inputEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (session.inputEvent && FAILED(device->SetEventNotification(session.inputEvent))) {
            CloseHandle(session.inputEvent);
            session.inputEvent = nullptr;
        }
        if (FAILED(device->Acquire())) {
            if (session.inputEvent) {
                CloseHandle(session.inputEvent);
                session.inputEvent = nullptr;
            }
            device->Release();
            return false;
        }
        session.device = device;
        session.monitor.configure(session.availableAxes, session.availableButtons, objects.povCount);
        session.axisCount = objects.axisCount;
        session.buttonCount = std::min(objects.buttonCount, kMaximumPhysicalButtons);
        session.povCount = objects.povCount;
        session.connected = true;
        return true;
    };

    bool lastMappingAllowed = false;
    std::uint64_t processedReports = 0;
    std::uint64_t handledReacquireRequest = m_reacquireInputAcknowledged.load();
    std::uint64_t handledOutputAvailabilityRetry = m_outputAvailabilityRetryGeneration.load(std::memory_order_relaxed);
    const DeviceRig *activeRig = findDeviceRig(configuration, configuration.activeDeviceRigId);
    if (!plan.valid) {
        setVjoyStatus(u"Device Rig needs attention: "_qs + plan.issue);
        emit workerEvent(u"Device Rig mapping is suspended: "_qs + plan.issue);
    }

    while (!m_stopRequested.load()) {
        if (m_runtimeTopologyChangeRequested.exchange(false)
            || m_configurationVersion.load() != appliedVersion) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t outputAvailabilityRetry = m_outputAvailabilityRetryGeneration.load(std::memory_order_relaxed);
        const bool retryOutputAvailability = outputAvailabilityRetry != handledOutputAvailabilityRetry;
        if (m_releaseVjoyRequested.exchange(false)) {
            // A full verification or explicit driver-configuration
            // transaction is the only control-plane case that relinquishes
            // an output.  Ordinary member loss and membership edits retain
            // the persistent worker vJoy interface and never touch the
            // driver configuration.
            for (int index = 0; index < plan.outputCount; ++index) {
                quiesceOutput(outputs[static_cast<size_t>(index)]);
                releaseOutput(outputs[static_cast<size_t>(index)]);
            }
            lastMappingAllowed = false;
            m_runtime.mappingActive = false;
            m_runtime.outputNeutralized = true;
            m_runtime.vjoyReady = false;
            m_runtime.outputReportsSucceeding = false;
            m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Off);
            setVjoyStatus(u"vJoy released for controller verification"_qs);
            m_vjoyReleasedForControlPlane = true;
            emit hardwareStateChanged();
        }
        const std::uint64_t requestedReacquire = m_reacquireInputRequested.load();
        if (requestedReacquire != handledReacquireRequest) {
            // A successful HidHide/identity transaction must reopen every
            // affected DirectInput session.  Acknowledge the control-plane
            // handoff only after old sessions and their pressed state have
            // been released; the caller separately waits for a fresh report.
            for (int index = 0; index < plan.memberCount; ++index) {
                InputSession &session = inputs[static_cast<size_t>(index)];
                releaseInput(session);
                clearMemberPhysicalSnapshot(index);
                session.nextDiscovery = now;
            }
            clearPrimarySnapshot();
            m_runtime.physicalConnected = false;
            m_runtime.physicalReportsSinceAcquisition = 0;
            handledReacquireRequest = requestedReacquire;
            m_reacquireInputAcknowledged = requestedReacquire;
            emit workerEvent(u"Device Rig DirectInput sessions released for controlled reacquisition"_qs);
        }
        if (!plan.valid) {
            m_runtime.mappingActive = false;
            m_runtime.physicalConnected = false;
            m_runtime.vjoyReady = false;
            m_runtime.outputReportsSucceeding = false;
            m_runtime.mappingEffectiveState = m_mappingRequested.load()
                ? static_cast<int>(MappingEffectiveState::Suspended)
                : static_cast<int>(MappingEffectiveState::Off);
            QThread::msleep(50);
            continue;
        }

        std::array<DeviceRigInputSessionState, kMaximumDeviceRigMembers> inputStates{};
        inputStates.fill(DeviceRigInputSessionState::Disconnected);
        int activeAutomationRules = 0;
        for (std::atomic_bool &active : m_runtime.automationRuleActive) active = false;
        for (int index = 0; index < plan.memberCount; ++index) {
            InputSession &session = inputs[static_cast<size_t>(index)];
            if (!session.connected && now >= session.nextDiscovery) {
                if (discoverInput(session)) {
                    emit workerEvent(QString(u"Device Rig input connected: %1"_qs)
                        .arg(session.member->displayName));
                }
                session.nextDiscovery = now + std::chrono::seconds(1);
            }
            if (!session.connected) {
                clearMemberPhysicalSnapshot(index);
                continue;
            }
            inputStates[static_cast<size_t>(index)] = DeviceRigInputSessionState::Connected;
            const HRESULT poll = session.device->Poll();
            DIJOYSTATE2 state{};
            const HRESULT read = SUCCEEDED(poll)
                ? session.device->GetDeviceState(sizeof(state), &state) : poll;
            if (read == DIERR_INPUTLOST || read == DIERR_NOTACQUIRED || FAILED(read)) {
                const QString phase = SUCCEEDED(poll) ? u"GetDeviceState"_qs : u"Poll"_qs;
                const int outputIndex = session.member->outputIndex;
                const int vjoyDeviceId = outputIndex >= 0 && outputIndex < plan.outputCount
                    ? plan.outputs[static_cast<size_t>(outputIndex)].vjoyDeviceId : 0;
                const QString detail = QString(u"runtime=device-rig\nmember=%1\nmemberId=%2\noutputIndex=%3\noutputLayout=%4\nvjoyDevice=%5\nphase=%6\nhresult=0x%7"_qs)
                    .arg(session.member->displayName)
                    .arg(session.member->controllerRecordId)
                    .arg(outputIndex)
                    .arg(session.member->outputLayoutId)
                    .arg(vjoyDeviceId)
                    .arg(phase)
                    .arg(static_cast<quint32>(read), 8, 16, QLatin1Char('0'));
                // This is a disconnect boundary, never a report-path log.
                // Record it before releasing the DirectInput object so an
                // unexpected native fault leaves the actual member/phase in
                // the crash reporter's bounded event history.
                CrashDiagnostics::recordControlPlaneEvent(
                    u"Device Rig DirectInput loss: "_qs + session.member->displayName, detail);
                emit workerEvent(QString(u"Device Rig input disconnected during %1: %2"_qs)
                    .arg(phase, session.member->displayName));
                inputStates[static_cast<size_t>(index)] = read == DIERR_INPUTLOST
                    ? DeviceRigInputSessionState::InputLost
                    : read == DIERR_NOTACQUIRED ? DeviceRigInputSessionState::NotAcquired
                                             : DeviceRigInputSessionState::Disconnected;
                releaseInput(session);
                clearMemberPhysicalSnapshot(index);
                continue;
            }

            PhysicalInputReport report;
            const bool bufferedEvent = session.inputEvent
                && WaitForSingleObject(session.inputEvent, 0) == WAIT_OBJECT_0;
            if (bufferedEvent) {
                ResetEvent(session.inputEvent);
            }
            bool bufferedSourceInUse = false;
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const size_t axisIndex = static_cast<size_t>(axis);
                bufferedSourceInUse = bufferedSourceInUse
                    || session.axisAcquisitionMethods[axisIndex] == 1
                    || session.axisAcquisitionMethods[axisIndex] == 3
                    || session.bufferedCorrelationEvidence[axisIndex].hasProvisional;
            }
            std::array<bool, kPhysicalAxisCount> bufferedAxisEvents{};
            if (bufferedEvent || bufferedSourceInUse) {
                readBufferedAxisEvents(session.device, session.axisDescriptors, session.availableAxes,
                                       &session.bufferedAxisValues, &session.bufferedAxisValuesKnown,
                                       &bufferedAxisEvents);
            }
            const auto observedAt = std::chrono::steady_clock::now();
            if (m_runtime.axisSourceMonitorRequested.load(std::memory_order_relaxed)) {
                AtomicAxisSourceTelemetry &telemetry = m_runtime.deviceRigMemberAxisSourceTelemetry[
                    static_cast<size_t>(index)];
                for (int source = 0; source < kPhysicalAxisCount; ++source) {
                    const size_t sourceIndex = static_cast<size_t>(source);
                    const LONG value = directInputAxisValue(state, static_cast<PhysicalAxis>(source));
                    int movementMagnitude = 0;
                    if (!session.sourceMonitorInitialized) {
                        session.sourceMonitorPrevious[sourceIndex] = value;
                        session.sourceMonitorMinimum[sourceIndex] = value;
                        session.sourceMonitorMaximum[sourceIndex] = value;
                        session.sourceMonitorChanges[sourceIndex] = 0;
                        session.sourceMonitorLastChanged[sourceIndex] = observedAt;
                    } else {
                        const LONG delta = value - session.sourceMonitorPrevious[sourceIndex];
                        movementMagnitude = std::abs(delta);
                        if (delta != 0) {
                            session.sourceMonitorPrevious[sourceIndex] = value;
                            ++session.sourceMonitorChanges[sourceIndex];
                            session.sourceMonitorLastChanged[sourceIndex] = observedAt;
                        }
                        session.sourceMonitorMinimum[sourceIndex] = std::min(
                            session.sourceMonitorMinimum[sourceIndex], value);
                        session.sourceMonitorMaximum[sourceIndex] = std::max(
                            session.sourceMonitorMaximum[sourceIndex], value);
                    }
                    telemetry.value[sourceIndex].store(value, std::memory_order_relaxed);
                    telemetry.observedMinimum[sourceIndex].store(
                        session.sourceMonitorMinimum[sourceIndex], std::memory_order_relaxed);
                    telemetry.observedMaximum[sourceIndex].store(
                        session.sourceMonitorMaximum[sourceIndex], std::memory_order_relaxed);
                    telemetry.changeCount[sourceIndex].store(
                        session.sourceMonitorChanges[sourceIndex], std::memory_order_relaxed);
                    telemetry.recentMovementMagnitude[sourceIndex].store(
                        movementMagnitude, std::memory_order_relaxed);
                    telemetry.lastChangeAgeMs[sourceIndex].store(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            observedAt - session.sourceMonitorLastChanged[sourceIndex]).count(),
                        std::memory_order_relaxed);
                }
                session.sourceMonitorInitialized = true;
                telemetry.available.store(true, std::memory_order_relaxed);
            } else {
                session.sourceMonitorInitialized = false;
                m_runtime.deviceRigMemberAxisSourceTelemetry[static_cast<size_t>(index)].available.store(
                    false, std::memory_order_relaxed);
            }
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const RuntimeAxisAcquisition &binding = session.axisAcquisitions[static_cast<size_t>(axis)];
                if (session.availableAxes[static_cast<size_t>(axis)] && binding.valid) {
                    const LONG standardValue = directInputAxisValue(state,
                        static_cast<PhysicalAxis>(binding.sourceIndex));
                    if (bufferedAxisEvents[static_cast<size_t>(axis)]
                        && session.bufferedAxisValuesKnown[static_cast<size_t>(axis)]
                        && session.axisAcquisitionMethods[static_cast<size_t>(axis)] != 3
                        && !session.axisDescriptors[static_cast<size_t>(axis)].formattedSourceVerified
                        && !session.manualAcquisitionApplied[static_cast<size_t>(axis)]
                        && (binding.flags & RuntimeAxisAcquisitionAllowBufferedEvidence) != 0) {
                        const NativeAxisDescriptor &descriptor = session.axisDescriptors[
                            static_cast<size_t>(axis)];
                        const int correlatedSource = uniqueCorrelatedDirectInputStateField(
                            session.bufferedAxisValues[static_cast<size_t>(axis)], state, binding);
                        const bool candidateDisagrees = std::abs(normalizeRuntimeAxisAcquisition(
                                session.bufferedAxisValues[static_cast<size_t>(axis)], binding)
                            - normalizeRuntimeAxisAcquisition(standardValue, binding)) > 0.002F;
                        const bool evidenceEligible = session.axisEvidenceCompatible[
                                static_cast<size_t>(axis)]
                            && descriptor.metadataContradiction
                            && descriptor.formattedSourceEvidence
                                == AxisFormattedSourceEvidence::ReportedOffsetCandidate
                            && !descriptor.formattedSourceVerified;
                        const bool fixedFieldProven = evidenceEligible
                            && observeBufferedObjectCorrelation(
                                &session.bufferedCorrelationEvidence[static_cast<size_t>(axis)],
                                correlatedSource,
                                session.bufferedAxisValues[static_cast<size_t>(axis)]);
                        if (fixedFieldProven) {
                            session.axisAcquisitionMethods[static_cast<size_t>(axis)] = 3;
                            m_runtime.deviceRigMemberAxisResolvedFormattedSource[
                                static_cast<size_t>(index)][static_cast<size_t>(axis)].store(
                                correlatedSource, std::memory_order_relaxed);
                        } else if (candidateDisagrees) {
                            session.axisAcquisitionMethods[static_cast<size_t>(axis)] = 1;
                        }
                    }
                    const LONG acquiredValue = session.axisAcquisitionMethods[static_cast<size_t>(axis)] != 0
                            && session.bufferedAxisValuesKnown[static_cast<size_t>(axis)]
                        ? session.bufferedAxisValues[static_cast<size_t>(axis)] : standardValue;
                    report.axes[static_cast<size_t>(axis)] = normalizeRuntimeAxisAcquisition(acquiredValue, binding);
                    const float current = report.axes[static_cast<size_t>(axis)];
                    if (!std::isfinite(session.lastObservedAxisValues[static_cast<size_t>(axis)])) {
                        session.lastObservedAxisValues[static_cast<size_t>(axis)] = current;
                    } else if (std::abs(current - session.lastObservedAxisValues[static_cast<size_t>(axis)]) > 0.002F) {
                        session.lastObservedAxisValues[static_cast<size_t>(axis)] = current;
                        session.lastAxisMovementAt[static_cast<size_t>(axis)] = observedAt;
                        session.axisLiveMovementObserved[static_cast<size_t>(axis)] = true;
                    }
                }
            }
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const bool available = session.availableAxes[static_cast<size_t>(axis)];
                const bool live = available && session.axisLiveMovementObserved[static_cast<size_t>(axis)];
                m_runtime.deviceRigMemberAxisAcquisitionSource[static_cast<size_t>(index)]
                    [static_cast<size_t>(axis)].store(available
                        ? (session.manualAcquisitionApplied[static_cast<size_t>(axis)] ? 2
                            : session.axisAcquisitionMethods[static_cast<size_t>(axis)]) : -1,
                        std::memory_order_relaxed);
                m_runtime.deviceRigMemberAxisLiveMovementObserved[static_cast<size_t>(index)]
                    [static_cast<size_t>(axis)].store(live, std::memory_order_relaxed);
                m_runtime.deviceRigMemberAxisLastMovementAgeMs[static_cast<size_t>(index)]
                    [static_cast<size_t>(axis)].store(live
                        ? std::chrono::duration_cast<std::chrono::milliseconds>(
                            observedAt - session.lastAxisMovementAt[static_cast<size_t>(axis)]).count()
                        : -1, std::memory_order_relaxed);
            }
            for (int button = 0; button < kMaximumPhysicalButtons; ++button) {
                report.buttons[static_cast<size_t>(button)] = session.availableButtons[static_cast<size_t>(button)]
                    && (state.rgbButtons[static_cast<size_t>(button)] & 0x80U) != 0;
            }
            for (int pov = 0; pov < session.povCount && pov < kMaximumPhysicalPovs; ++pov) {
                const DWORD raw = state.rgdwPOV[static_cast<size_t>(pov)];
                report.povs[static_cast<size_t>(pov)] = raw != kVjoyPovCentered && raw < 36000UL
                    ? static_cast<int>(raw) : -1;
            }
            session.monitor.accept(report);
            const PhysicalInputSnapshot &snapshot = session.monitor.snapshot();
            // Publish each member's physical inputs as an independent bounded
            // snapshot.  Selected Device consumes these fields directly;
            // member-zero's legacy aggregate must never stand in for another
            // controller's buttons or POVs.
            m_runtime.deviceRigMemberPhysicalConnected[static_cast<size_t>(index)].store(
                true, std::memory_order_relaxed);
            for (int button = 0; button < kMaximumPhysicalButtons; ++button) {
                m_runtime.deviceRigMemberPhysicalButtonPressed[static_cast<size_t>(index)]
                    [static_cast<size_t>(button)].store(
                        snapshot.buttons[static_cast<size_t>(button)], std::memory_order_relaxed);
            }
            for (int pov = 0; pov < kMaximumPhysicalPovs; ++pov) {
                m_runtime.deviceRigMemberPovValues[static_cast<size_t>(index)]
                    [static_cast<size_t>(pov)].store(
                        snapshot.povs[static_cast<size_t>(pov)], std::memory_order_relaxed);
            }
            if (observeMeaningfulInput(session.meaningfulInput, snapshot, session.availableAxes,
                                       session.availableButtons, session.povCount)) {
                session.latestMeaningfulInputSequence = m_runtime.meaningfulInputSequence.fetch_add(
                    1, std::memory_order_relaxed) + 1;
                m_runtime.deviceRigMeaningfulInputSequence[static_cast<size_t>(index)].store(
                    session.latestMeaningfulInputSequence, std::memory_order_relaxed);
            }
            const auto timestamp = std::chrono::steady_clock::now();
            AutomationInputSnapshot automationInput;
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                const RuntimeAxisMapping &mapping = session.member->mapping.axes[static_cast<size_t>(axis)];
                automationInput.physicalAxes[static_cast<size_t>(axis)] = normalizeCalibrated(
                    snapshot.axes[static_cast<size_t>(axis)], mapping.calibration);
                automationInput.axisAvailable[static_cast<size_t>(axis)] =
                    session.availableAxes[static_cast<size_t>(axis)]
                    && !session.member->fixedAxes[static_cast<size_t>(axis)];
            }
            automationInput.buttons = snapshot.buttons;
            automationInput.povs = snapshot.povs;
            automationInput.povCount = session.povCount;
            automationInput.buttonCount = session.buttonCount;
            automationInput.timestamp = timestamp;
            const AutomationEvaluationResult *automationEffects = m_mappingRequested.load()
                ? &session.automation.evaluate(automationInput)
                : &session.automation.evaluateMappingControls(automationInput);
            session.automationEffects = *automationEffects;
            if (automationEffects->mappingControlAction != MappingControlAction::None) {
                const bool current = m_mappingRequested.load();
                const bool desired = automationEffects->mappingControlAction == MappingControlAction::MappingOn
                    ? true : automationEffects->mappingControlAction == MappingControlAction::MappingOff
                    ? false : !current;
                if (desired != current) {
                    m_mappingRequested = desired;
                    emit workerEvent(u"Device Rig Automation: "_qs
                        + mappingControlActionLabel(automationEffects->mappingControlAction));
                }
            }
            activeAutomationRules += automationEffects->activeRuleCount;
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                if (!session.availableAxes[static_cast<size_t>(axis)]) continue;
                const RuntimeAxisMapping &mapping = session.member->mapping.axes[static_cast<size_t>(axis)];
                const float normalized = normalizeCalibrated(snapshot.axes[static_cast<size_t>(axis)],
                                                             mapping.calibration);
                const float resolved = resolveNormalizedAxisCenter(normalized, mapping,
                    session.centers[static_cast<size_t>(axis)]);
                // Keep the Device Rig execution path on the same effective
                // Adaptive Response configuration as the single-device path.
                // The physical estimator uses its bounded candidate envelope,
                // while the mapped-output cap remains the configured value.
                RuntimeAdaptiveResponseConfig effectiveAdaptiveConfiguration = mapping.adaptiveResponse;
                const RuntimeAdaptiveResponseOverride nextOverlay = automationEffects
                    ? automationEffects->adaptiveResponseOverlays[static_cast<size_t>(axis)]
                    : RuntimeAdaptiveResponseOverride{};
                RuntimeAdaptiveResponseOverride &activeOverlay = session.activeAutomationOverlays[
                    static_cast<size_t>(axis)];
                if (!sameAdaptiveResponseOverlay(activeOverlay, nextOverlay)) {
                    activeOverlay = nextOverlay;
                    session.adaptive[static_cast<size_t>(axis)].reset();
                    session.centers[static_cast<size_t>(axis)] = {};
                    session.hysteresis[static_cast<size_t>(axis)] = {};
                }
                if (activeOverlay.active) {
                    effectiveAdaptiveConfiguration = applyAdaptiveResponseRuntimeOverride(
                        effectiveAdaptiveConfiguration, activeOverlay);
                }
                RuntimeAdaptiveResponseConfig physicalPredictionConfiguration = effectiveAdaptiveConfiguration;
                physicalPredictionConfiguration.maximumLead = 0.50F;
                const AdaptiveResponseTelemetry adaptive = session.adaptive[static_cast<size_t>(axis)].process(
                    resolved, physicalPredictionConfiguration, timestamp);
                const AdaptiveMappedAxisOutput mapped = applyCurveAwareAdaptiveResponse(resolved,
                    adaptive.predicted, effectiveAdaptiveConfiguration.enabled,
                    effectiveAdaptiveConfiguration.maximumLead, mapping,
                    session.hysteresis[static_cast<size_t>(axis)]);
                session.transformed[static_cast<size_t>(axis)] = mapped.adaptiveOutput;
                // Keep the exact physical source available to Selected Device.
                // Input zero remains the legacy aggregate; every other member
                // owns an equivalent bounded latest snapshot.
                AtomicAdaptiveTelemetry &telemetry = index == 0
                    ? static_cast<AtomicAdaptiveTelemetry &>(m_runtime)
                    : m_runtime.deviceRigMemberAdaptive[static_cast<size_t>(index)];
                publishAdaptiveAxisTelemetry(telemetry, axis, snapshot.axes[static_cast<size_t>(axis)],
                                             resolved, mapped, adaptive, effectiveAdaptiveConfiguration,
                                             session.adaptive[static_cast<size_t>(axis)].reversalCount(),
                                             session.adaptive[static_cast<size_t>(axis)].safetyClampCount(),
                                             activeOverlay);
                // Existing overview/diagnostics atomics retain a useful
                // primary-session view; device-specific details use the rig
                // status model and never imply that axis indexes are global.
                if (index == 0) {
                    m_runtime.raw[static_cast<size_t>(axis)] = snapshot.axes[static_cast<size_t>(axis)];
                    m_runtime.normalized[static_cast<size_t>(axis)] = resolved;
                    m_runtime.afterDeadzone[static_cast<size_t>(axis)] = mapped.baselineSignalPath.afterDeadzone;
                    m_runtime.afterHysteresis[static_cast<size_t>(axis)] = mapped.baselineSignalPath.afterHysteresis;
                    m_runtime.afterInversion[static_cast<size_t>(axis)] = mapped.baselineSignalPath.afterInversion;
                    m_runtime.curveResponse[static_cast<size_t>(axis)] = mapped.baselineSignalPath.afterCurve;
                    m_runtime.transformed[static_cast<size_t>(axis)] = mapped.adaptiveOutput;
                    m_runtime.adaptiveEstimated[static_cast<size_t>(axis)] = adaptive.estimated;
                    m_runtime.adaptivePredicted[static_cast<size_t>(axis)] = adaptive.predicted;
                    m_runtime.adaptiveBaselineMapped[static_cast<size_t>(axis)] = mapped.baselineOutput;
                    m_runtime.adaptivePredictedMapped[static_cast<size_t>(axis)] = mapped.predictedMappedOutput;
                    m_runtime.adaptiveOutput[static_cast<size_t>(axis)] = mapped.adaptiveOutput;
                    m_runtime.adaptiveMappedLead[static_cast<size_t>(axis)] = mapped.mappedLead;
                    m_runtime.adaptiveAppliedLead[static_cast<size_t>(axis)] = mapped.appliedLead;
                    m_runtime.adaptiveLocalCurveGain[static_cast<size_t>(axis)] = mapped.localCurveGain;
                    m_runtime.adaptiveVelocity[static_cast<size_t>(axis)] = adaptive.velocity;
                    m_runtime.adaptiveAcceleration[static_cast<size_t>(axis)] = adaptive.acceleration;
                    m_runtime.adaptiveHorizonSeconds[static_cast<size_t>(axis)] = adaptive.activeHorizonSeconds;
                    m_runtime.adaptiveRequestedLead[static_cast<size_t>(axis)] = adaptive.requestedLead;
                    m_runtime.adaptiveCappedLead[static_cast<size_t>(axis)] = adaptive.cappedLead;
                    m_runtime.adaptiveEndpointTaper[static_cast<size_t>(axis)] = adaptive.endpointTaper;
                    m_runtime.adaptiveLead[static_cast<size_t>(axis)] = adaptive.lead;
                    m_runtime.adaptiveConfidence[static_cast<size_t>(axis)] = adaptive.confidence;
                    m_runtime.adaptiveMotionIntensity[static_cast<size_t>(axis)] = adaptive.motionIntensity;
                    m_runtime.adaptiveVelocityAuthority[static_cast<size_t>(axis)] = adaptive.velocityAuthority;
                    m_runtime.adaptiveDeliberateMotionEvidence[static_cast<size_t>(axis)] = adaptive.deliberateMotionEvidence;
                    m_runtime.adaptiveNormalMotionAuthority[static_cast<size_t>(axis)] = adaptive.normalMotionAuthority;
                    m_runtime.adaptiveRapidMotionAuthority[static_cast<size_t>(axis)] = adaptive.rapidMotionAuthority;
                    m_runtime.adaptiveRapidMotionBlend[static_cast<size_t>(axis)] = adaptive.rapidMotionBlend;
                    m_runtime.adaptiveAccelerationIntent[static_cast<size_t>(axis)] = adaptive.accelerationIntent;
                    m_runtime.adaptiveOnsetAuthority[static_cast<size_t>(axis)] = adaptive.onsetAuthority;
                    m_runtime.adaptiveSustainedEvidence[static_cast<size_t>(axis)] = adaptive.sustainedEvidence;
                    m_runtime.adaptiveSustainedAuthority[static_cast<size_t>(axis)] = adaptive.sustainedAuthority;
                    m_runtime.adaptiveMotionUrgency[static_cast<size_t>(axis)] = adaptive.motionUrgency;
                    m_runtime.adaptiveHorizonExtensionEligibility[static_cast<size_t>(axis)] = adaptive.horizonExtensionEligibility;
                    m_runtime.adaptiveNormalMaximumHorizonSeconds[static_cast<size_t>(axis)] = adaptive.normalMaximumHorizonSeconds;
                    m_runtime.adaptiveAllowedMaximumHorizonSeconds[static_cast<size_t>(axis)] = adaptive.allowedMaximumHorizonSeconds;
                    m_runtime.adaptiveTurningPointConfidence[static_cast<size_t>(axis)] = adaptive.turningPointConfidence;
                    m_runtime.adaptiveEstimatedTimeToTurnSeconds[static_cast<size_t>(axis)] = adaptive.estimatedTimeToTurnSeconds;
                    m_runtime.adaptiveEstimatedRemainingTravel[static_cast<size_t>(axis)] = adaptive.estimatedRemainingTravel;
                    m_runtime.adaptiveTurningPointHorizonLimitSeconds[static_cast<size_t>(axis)] = adaptive.turningPointHorizonLimitSeconds;
                    m_runtime.adaptiveTurningPointLeadLimit[static_cast<size_t>(axis)] = adaptive.turningPointLeadLimit;
                    m_runtime.adaptiveReacquisitionAuthority[static_cast<size_t>(axis)] = adaptive.reacquisitionAuthority;
                    m_runtime.adaptiveMotionState[static_cast<size_t>(axis)] = static_cast<int>(adaptive.state);
                    m_runtime.adaptiveReversing[static_cast<size_t>(axis)] = adaptive.reversal;
                    m_runtime.adaptiveSafetyLimited[static_cast<size_t>(axis)] = adaptive.safetyLimited || mapped.leadLimited;
                    m_runtime.adaptiveDeadzoneAuthorityBlocked[static_cast<size_t>(axis)] = mapped.deadzoneAuthorityBlocked;
                    m_runtime.adaptiveLeadLimited[static_cast<size_t>(axis)] = mapped.leadLimited;
                    m_runtime.adaptiveHighLocalCurveGain[static_cast<size_t>(axis)] = mapped.highLocalCurveGain;
                    m_runtime.adaptiveReversalCount[static_cast<size_t>(axis)] = session.adaptive[static_cast<size_t>(axis)].reversalCount();
                    m_runtime.adaptiveSafetyClampCount[static_cast<size_t>(axis)] = session.adaptive[static_cast<size_t>(axis)].safetyClampCount();
                    m_runtime.adaptiveRuntimeEnabled[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.enabled;
                    m_runtime.adaptiveRuntimeModel[static_cast<size_t>(axis)] = static_cast<int>(effectiveAdaptiveConfiguration.model);
                    m_runtime.adaptiveRuntimeMaximumHorizonSeconds[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.maximumHorizonSeconds;
                    m_runtime.adaptiveRuntimeMaximumLead[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.maximumLead;
                    m_runtime.adaptiveRuntimeVelocityResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.velocityResponse;
                    m_runtime.adaptiveRuntimeAccelerationResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.accelerationResponse;
                    m_runtime.adaptiveRuntimeMotionSensitivity[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.motionSensitivity;
                    m_runtime.adaptiveRuntimeNoiseRejection[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.noiseRejection;
                    m_runtime.adaptiveRuntimeReversalDetection[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.reversalDetection;
                    m_runtime.adaptiveRuntimeReversalResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.reversalResponse;
                    m_runtime.adaptiveRuntimeDecelerationResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.decelerationResponse;
                    m_runtime.adaptiveRuntimeSettlingResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.settlingResponse;
                    m_runtime.adaptiveRuntimeEndpointTaper[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.endpointTaper;
                    m_runtime.adaptiveRuntimeOnsetAssist[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.onsetAssist;
                    m_runtime.adaptiveRuntimeOnsetCap[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.onsetCap;
                    m_runtime.adaptiveRuntimeSustainedAssist[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.sustainedAssist;
                    m_runtime.adaptiveRuntimeSustainedCap[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.sustainedCap;
                    m_runtime.adaptiveRuntimeHorizonExtension[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.horizonExtension;
                    m_runtime.adaptiveRuntimeHorizonExtensionCapSeconds[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.horizonExtensionCapSeconds;
                    m_runtime.adaptiveRuntimeTurningPointProtection[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.turningPointProtection;
                    m_runtime.adaptiveRuntimeTurningPointMargin[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.turningPointMargin;
                    m_runtime.adaptiveRuntimeNormalMovementResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.normalMovementResponse;
                    m_runtime.adaptiveRuntimeRapidMovementResponse[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.rapidMovementResponse;
                    m_runtime.adaptiveRuntimeEngagementSensitivity[static_cast<size_t>(axis)] = effectiveAdaptiveConfiguration.engagementSensitivity;
                    m_runtime.adaptiveAutomationOverlayActive[static_cast<size_t>(axis)] = activeOverlay.active;
                    m_runtime.adaptiveAutomationOverlayProperties[static_cast<size_t>(axis)] = activeOverlay.properties;
                }
            }
            session.automation.applyAxisActions(automationInput, session.transformed);
            if (index == 0) {
                for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                    m_runtime.axisAvailable[static_cast<size_t>(axis)] = session.availableAxes[static_cast<size_t>(axis)];
                    m_runtime.axisAcquisitionSource[static_cast<size_t>(axis)] =
                        session.availableAxes[static_cast<size_t>(axis)]
                        ? (session.manualAcquisitionApplied[static_cast<size_t>(axis)] ? 2
                            : session.axisAcquisitionMethods[static_cast<size_t>(axis)]) : -1;
                    m_runtime.axisResolvedFormattedSource[static_cast<size_t>(axis)] =
                        m_runtime.deviceRigMemberAxisResolvedFormattedSource[0]
                            [static_cast<size_t>(axis)].load(std::memory_order_relaxed);
                    const bool live = session.axisLiveMovementObserved[static_cast<size_t>(axis)];
                    m_runtime.axisLiveMovementObserved[static_cast<size_t>(axis)] = live;
                    m_runtime.axisLastMovementAgeMs[static_cast<size_t>(axis)] = live
                        ? std::chrono::duration_cast<std::chrono::milliseconds>(
                            observedAt - session.lastAxisMovementAt[static_cast<size_t>(axis)]).count()
                        : -1;
                }
                for (int button = 0; button < kMaximumPhysicalButtons; ++button) {
                    m_runtime.buttonAvailable[static_cast<size_t>(button)] = session.availableButtons[static_cast<size_t>(button)];
                    m_runtime.physicalButtonPressed[static_cast<size_t>(button)] = snapshot.buttons[static_cast<size_t>(button)];
                }
                for (int pov = 0; pov < kMaximumPhysicalPovs; ++pov) {
                    m_runtime.povValues[static_cast<size_t>(pov)] = snapshot.povs[static_cast<size_t>(pov)];
                }
                m_runtime.axisCount = session.axisCount;
                m_runtime.buttonCount = session.buttonCount;
                m_runtime.povCount = session.povCount;
                m_runtime.lastPhysicalButton = snapshot.lastChangedButton;
                for (int rule = 0; rule < kMaximumAutomationRules; ++rule) {
                    m_runtime.automationRuleActive[static_cast<size_t>(rule)] =
                        automationEffects->activeRules[static_cast<size_t>(rule)];
                }
            }
            ++processedReports;
            ++m_runtime.deviceRigInputReports[static_cast<size_t>(index)];
        }
        m_runtime.automationActiveRuleCount = activeAutomationRules;

        const DeviceRigDisconnectBehavior disconnectBehavior = activeRig
            ? activeRig->disconnectBehavior : DeviceRigDisconnectBehavior::SuspendAffectedRoutes;
        const DeviceRigRuntimeAvailability availability = evaluateDeviceRigRuntimeAvailability(
            plan, inputStates, m_mappingRequested.load(), true, disconnectBehavior);
        const bool anyConnected = availability.anyConnected;
        const int connectedCount = availability.connectedMemberCount;

        QString outputStatus;
        bool allOutputsReady = plan.outputCount > 0;
        for (int index = 0; index < plan.outputCount; ++index) {
            OutputSession &output = outputs[static_cast<size_t>(index)];
            if (now >= output.nextCheck || retryOutputAvailability) {
                QString status;
                refreshOutput(output, &status, index == 0);
                if (index == 0) outputStatus = status;
                output.nextCheck = now + std::chrono::seconds(1);
            }
            allOutputsReady = allOutputsReady && output.ready;
        }
        if (retryOutputAvailability) {
            handledOutputAvailabilityRetry = outputAvailabilityRetry;
        }
        if (plan.outputCount > 0) {
            const OutputSession &primaryOutput = outputs.front();
            for (int axis = 0; axis < kVirtualAxisSlotCount; ++axis) {
                m_runtime.virtualAxisAvailable[static_cast<size_t>(axis)] = axis > 0
                    && primaryOutput.axes[static_cast<size_t>(axis)];
            }
            m_runtime.vjoyButtonCount = primaryOutput.buttonCapacity;
            m_runtime.vjoyContinuousPovCount = primaryOutput.continuousPovCapacity;
            m_runtime.vjoyDiscretePovCount = primaryOutput.discretePovCapacity;
        }
        m_runtime.physicalConnected = anyConnected;
        m_runtime.physicalReportsSinceAcquisition = processedReports;
        m_runtime.inputReports = processedReports;
        if (anyConnected && inputs.front().connected && inputs.front().device) {
            const QString hid = hidInstanceIdForDevice(inputs.front().device);
            setDeviceSnapshot({inputs.front().member->displayName, inputs.front().member->directInputId,
                               hid, hidDeviceContainerId(hid)});
        } else {
            clearPrimarySnapshot();
            setDeviceSnapshot({});
        }

        const bool mappingAllowed = evaluateDeviceRigRuntimeAvailability(plan, inputStates,
            m_mappingRequested.load(), allOutputsReady, disconnectBehavior).mappingAllowed;
        if (!mappingAllowed && lastMappingAllowed) {
            for (int index = 0; index < plan.outputCount; ++index) quiesceOutput(outputs[static_cast<size_t>(index)]);
            emit workerEvent(u"Device Rig mapping suspended; affected routes were neutralized."_qs);
        }
        lastMappingAllowed = mappingAllowed;

        if (mappingAllowed) {
            bool acquired = true;
            for (int index = 0; index < plan.outputCount; ++index) {
                OutputSession &output = outputs[static_cast<size_t>(index)];
                if (!output.acquired) {
                    QString status;
                    output.acquired = output.vjoy.acquire(output.configured->vjoyDeviceId, &status);
                    if (index == 0) {
                        setVjoyOwnershipEvidence(output.vjoy.lastOwnership(), output.vjoy.lastAcquireAttempt());
                    }
                    if (!output.acquired) {
                        output.ready = false;
                        acquired = false;
                        outputStatus = status;
                    } else {
                        quiesceOutput(output);
                        output.lastAxes.fill(std::numeric_limits<float>::quiet_NaN());
                    }
                }
            }
            if (acquired) {
                const float parkedAxisValue = sanitizedDisabledAxisValue(configuration.disabledAxisValue);
                for (std::atomic<float> &value : m_runtime.virtualValues) {
                    value.store(parkedAxisValue, std::memory_order_relaxed);
                }
                for (int outputIndex = 0; outputIndex < plan.outputCount; ++outputIndex) {
                    OutputSession &output = outputs[static_cast<size_t>(outputIndex)];
                    std::array<float, kVirtualAxisSlotCount> desiredAxes{};
                    desiredAxes.fill(sanitizedDisabledAxisValue(configuration.disabledAxisValue));
                    std::array<int, kVirtualAxisSlotCount> axisContributorCounts{};
                    VirtualButtonStates desiredButtons{};
                    bool outputChanged = false;
                    bool outputWriteFailed = false;
                    quint64 mappedInputSequence = 0;
                    for (int memberIndex = 0; memberIndex < plan.memberCount; ++memberIndex) {
                        InputSession &input = inputs[static_cast<size_t>(memberIndex)];
                        if (!input.connected || input.member->outputIndex != outputIndex) continue;
                        mappedInputSequence = std::max(mappedInputSequence, input.latestMeaningfulInputSequence);
                        std::array<bool, kPhysicalAxisCount> routableAxes = input.availableAxes;
                        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                            routableAxes[static_cast<size_t>(axis)] = routableAxes[static_cast<size_t>(axis)]
                                && !input.member->fixedAxes[static_cast<size_t>(axis)];
                        }
                        const VirtualAxisOutputPlan localAxisPlan = buildVirtualAxisOutputPlan(
                            input.member->mapping, routableAxes, input.transformed, output.axes,
                            configuration.disabledAxisValue);
                        for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
                            if (!output.axes[static_cast<size_t>(axis)]) continue;
                            const int source = localAxisPlan.sourceIndexes[static_cast<size_t>(axis)];
                            if (source < 0 || source >= kPhysicalAxisCount) continue;
                            const float value = localAxisPlan.values[static_cast<size_t>(axis)];
                            int &contributors = axisContributorCounts[static_cast<size_t>(axis)];
                            if (contributors == 0) {
                                desiredAxes[static_cast<size_t>(axis)] = value;
                            } else {
                                // The profile-wide canonical mixer is compiled into every
                                // participating member. This worker-side reduction is the
                                // one bounded implementation for both Graphical Editor and
                                // Flight Deck routes; no QML path supplies its own math.
                                switch (input.member->mapping.signalFlowAxisMixers[static_cast<size_t>(axis)]) {
                                case SignalFlowMixerMode::Average:
                                    desiredAxes[static_cast<size_t>(axis)] =
                                        (desiredAxes[static_cast<size_t>(axis)] * static_cast<float>(contributors) + value)
                                        / static_cast<float>(contributors + 1);
                                    break;
                                case SignalFlowMixerMode::SumClamped:
                                    desiredAxes[static_cast<size_t>(axis)] = std::clamp(
                                        desiredAxes[static_cast<size_t>(axis)] + value, -1.0F, 1.0F);
                                    break;
                                case SignalFlowMixerMode::HighestMagnitude:
                                    if (std::abs(value) > std::abs(desiredAxes[static_cast<size_t>(axis)])) {
                                        desiredAxes[static_cast<size_t>(axis)] = value;
                                    }
                                    break;
                                case SignalFlowMixerMode::Disabled:
                                    // Reconciliation prevents this malformed state from
                                    // reaching runtime. Keep the first route authoritative
                                    // rather than letting member iteration become a hidden
                                    // last-writer-wins merge.
                                    continue;
                                }
                            }
                            ++contributors;
                            if (outputIndex == 0) {
                                m_runtime.virtualValues[static_cast<size_t>(source)].store(
                                    desiredAxes[static_cast<size_t>(axis)], std::memory_order_relaxed);
                            }
                        }
                        const RuntimeButtonTargets buttonTargets = buildRuntimeButtonTargets(
                            input.member->mapping.buttons, output.buttonCapacity);
                        const RuntimePovTargets povTargets = buildRuntimePovTargets(
                            input.member->mapping.povs, output.buttonCapacity);
                        const PhysicalInputSnapshot &snapshot = input.monitor.snapshot();
                        VirtualButtonStates localButtons = input.member->mapping.signalFlowTopologyCompiled
                            ? mapSignalFlowDigitalStates(snapshot.buttons, snapshot.povs, input.povCount,
                                input.member->mapping, buttonTargets, povTargets, output.buttonCapacity)
                            : mapButtonStates(snapshot.buttons, buttonTargets, output.buttonCapacity);
                        if (!input.member->mapping.signalFlowTopologyCompiled) {
                            mapPovStates(localButtons, snapshot.povs, input.povCount,
                                         povTargets, output.buttonCapacity);
                        }
                        for (int button = 1; button <= output.buttonCapacity; ++button) {
                            desiredButtons[static_cast<size_t>(button)] = desiredButtons[static_cast<size_t>(button)]
                                || localButtons[static_cast<size_t>(button)]
                                || input.automationEffects.heldButtons[static_cast<size_t>(button)]
                                || input.automationEffects.toggledButtons[static_cast<size_t>(button)]
                                || input.automationEffects.pulsedButtons[static_cast<size_t>(button)];
                        }
                        if (input.member->mapping.signalFlowTopologyCompiled) {
                            const int routeCount = std::clamp(
                                input.member->mapping.signalFlowNativePovRouteCount, 0,
                                kMaximumRuntimeSignalFlowNativePovRoutes);
                            for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
                                const RuntimeSignalFlowNativePovRoute &route =
                                    input.member->mapping.signalFlowNativePovRoutes[static_cast<size_t>(routeIndex)];
                                const int source = route.sourcePov;
                                const int target = route.destinationIndex;
                                const NativePovTargetType type = static_cast<NativePovTargetType>(route.destinationType);
                                const bool available = type == NativePovTargetType::Continuous
                                    ? target >= 1 && target <= output.continuousPovCapacity
                                    : type == NativePovTargetType::Discrete
                                        && target >= 1 && target <= output.discretePovCapacity;
                                if (source < 0 || source >= input.povCount || !available) continue;
                                const size_t typeIndex = static_cast<size_t>(type);
                                const int desired = snapshot.povs[static_cast<size_t>(source)];
                                if (desired == output.lastNativePovs[typeIndex][static_cast<size_t>(target)]) continue;
                                const NativePovBinding binding{true, type, target};
                                if (output.vjoy.setPov(binding, desired)) {
                                    output.lastNativePovs[typeIndex][static_cast<size_t>(target)] = desired;
                                    ++m_runtime.vjoyWrites;
                                    ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                                    outputChanged = true;
                                } else {
                                    outputWriteFailed = true;
                                }
                            }
                        } else {
                            for (int pov = 0; pov < input.povCount && pov < kMaximumPhysicalPovs; ++pov) {
                                const NativePovBinding &binding = input.member->nativePovBindings[static_cast<size_t>(pov)];
                                const bool available = binding.targetType == NativePovTargetType::Continuous
                                    ? binding.targetIndex <= output.continuousPovCapacity
                                    : binding.targetType == NativePovTargetType::Discrete
                                        && binding.targetIndex <= output.discretePovCapacity;
                                if (!binding.enabled || !available) continue;
                                const size_t typeIndex = static_cast<size_t>(binding.targetType);
                                const int desired = snapshot.povs[static_cast<size_t>(pov)];
                                if (desired == output.lastNativePovs[typeIndex][static_cast<size_t>(binding.targetIndex)]) continue;
                                if (output.vjoy.setPov(binding, desired)) {
                                    output.lastNativePovs[typeIndex][static_cast<size_t>(binding.targetIndex)] = desired;
                                    ++m_runtime.vjoyWrites;
                                    ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                                    outputChanged = true;
                                } else {
                                    outputWriteFailed = true;
                                }
                            }
                        }
                    }
                    for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
                        if (!output.axes[static_cast<size_t>(axis)]) continue;
                        const float desired = desiredAxes[static_cast<size_t>(axis)];
                        if (std::isfinite(output.lastAxes[static_cast<size_t>(axis)])
                            && std::abs(output.lastAxes[static_cast<size_t>(axis)] - desired) < 0.00001F) continue;
                        if (output.vjoy.setAxis(static_cast<VirtualAxis>(axis), desired)) {
                            output.lastAxes[static_cast<size_t>(axis)] = desired;
                            ++m_runtime.vjoyWrites;
                            ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                            outputChanged = true;
                        } else {
                            outputWriteFailed = true;
                        }
                    }
                    for (int button = 1; button <= output.buttonCapacity; ++button) {
                        const bool desired = desiredButtons[static_cast<size_t>(button)];
                        if (desired == output.lastButtons[static_cast<size_t>(button)]) continue;
                        if (output.vjoy.setButton(button, desired)) {
                            output.lastButtons[static_cast<size_t>(button)] = desired;
                            if (outputIndex == 0) {
                                m_runtime.virtualButtonPressed[static_cast<size_t>(button - 1)].store(
                                    desired, std::memory_order_relaxed);
                            }
                            ++m_runtime.vjoyWrites;
                            ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                            outputChanged = true;
                        } else {
                            outputWriteFailed = true;
                        }
                    }
                    if (outputWriteFailed) {
                        m_runtime.outputWriteFailures.fetch_add(1, std::memory_order_relaxed);
                        m_runtime.outputReportsSucceeding.store(false, std::memory_order_relaxed);
                    }
                    if (outputChanged) {
                        for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) {
                            if (output.axes[static_cast<size_t>(axis)]) {
                                m_runtime.mappedOutputAxes[static_cast<size_t>(axis)].store(
                                    desiredAxes[static_cast<size_t>(axis)], std::memory_order_relaxed);
                            }
                        }
                        for (int button = 1; button <= output.buttonCapacity; ++button) {
                            m_runtime.mappedOutputButtons[static_cast<size_t>(button - 1)].store(
                                desiredButtons[static_cast<size_t>(button)], std::memory_order_relaxed);
                        }
                        const auto outputTimestamp = std::chrono::steady_clock::now();
                        const auto outputTimestampMs = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                outputTimestamp.time_since_epoch()).count());
                        m_runtime.successfulOutputReportSequence.fetch_add(1, std::memory_order_relaxed);
                        m_runtime.lastSuccessfulOutputReportMs.store(outputTimestampMs,
                            std::memory_order_relaxed);
                        m_runtime.outputReportsSucceeding.store(true, std::memory_order_relaxed);
                    }
                    if (outputChanged && mappedInputSequence > 0) {
                        m_runtime.deviceRigMeaningfulOutputSequence[static_cast<size_t>(outputIndex)].store(
                            mappedInputSequence, std::memory_order_relaxed);
                    }
                }
                m_runtime.mappingActive = true;
                m_runtime.outputNeutralized = false;
                m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Active);
            }
        }
        if (!mappingAllowed || !m_runtime.mappingActive.load()) {
            m_runtime.mappingActive = false;
            m_runtime.outputNeutralized = true;
            m_runtime.mappingEffectiveState = m_mappingRequested.load()
                ? static_cast<int>(MappingEffectiveState::Suspended)
                : static_cast<int>(MappingEffectiveState::Off);
        }
        // A descriptor alone is not enough while mapping is enabled. The
        // active Rig must also have successfully published at least one
        // report through the actual vJoy API.
        m_runtime.vjoyReady = allOutputsReady
            && (!m_mappingRequested.load() || m_runtime.outputReportsSucceeding.load());
        if (!outputStatus.isEmpty()) setVjoyStatus(outputStatus);
        if (connectedCount == 0) QThread::msleep(25);
        else QThread::msleep(kPhysicalPollIntervalMs);
    }

    for (int index = 0; index < plan.outputCount; ++index) {
        quiesceOutput(outputs[static_cast<size_t>(index)]);
        releaseOutput(outputs[static_cast<size_t>(index)]);
    }
    for (int index = 0; index < plan.memberCount; ++index) {
        releaseInput(inputs[static_cast<size_t>(index)]);
        clearMemberPhysicalSnapshot(index);
    }
    m_runtime.mappingActive = false;
    m_runtime.outputNeutralized = true;
    m_runtime.vjoyReady = false;
    m_runtime.outputReportsSucceeding = false;
    m_runtime.activeOutputVjoyDeviceId = 0;
    m_runtime.physicalConnected = false;
    clearPrimarySnapshot();
}

} // namespace hotas
