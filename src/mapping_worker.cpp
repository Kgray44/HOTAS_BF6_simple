#include "mapping_worker.h"
#include "crash_diagnostics.h"
#include "device_rig.h"
#include "hid_device_identity.h"

#include "adaptive_response.h"
#include "axis_transform.h"
#include "axis_mapping_transition.h"
#include "automation_engine.h"
#include "button_mapping.h"
#include "physical_input_monitor.h"
#include "profile_trigger_runtime.h"

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

int axisIndexForOffset(DWORD offset)
{
    if (offset == DIJOFS_X) return static_cast<int>(PhysicalAxis::X);
    if (offset == DIJOFS_Y) return static_cast<int>(PhysicalAxis::Y);
    if (offset == DIJOFS_Z) return static_cast<int>(PhysicalAxis::Z);
    if (offset == DIJOFS_RX) return static_cast<int>(PhysicalAxis::Rx);
    if (offset == DIJOFS_RY) return static_cast<int>(PhysicalAxis::Ry);
    if (offset == DIJOFS_RZ) return static_cast<int>(PhysicalAxis::Rz);
    if (offset == DIJOFS_SLIDER(0)) return static_cast<int>(PhysicalAxis::Slider0);
    if (offset == DIJOFS_SLIDER(1)) return static_cast<int>(PhysicalAxis::Slider1);
    return -1;
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

LONG directInputValue(const DIJOYSTATE2 &state, int index)
{
    switch (static_cast<PhysicalAxis>(index)) {
    case PhysicalAxis::X: return state.lX;
    case PhysicalAxis::Y: return state.lY;
    case PhysicalAxis::Z: return state.lZ;
    case PhysicalAxis::Rx: return state.lRx;
    case PhysicalAxis::Ry: return state.lRy;
    case PhysicalAxis::Rz: return state.lRz;
    case PhysicalAxis::Slider0: return state.rglSlider[0];
    case PhysicalAxis::Slider1: return state.rglSlider[1];
    }
    return 0;
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
            return false;
        }
        const int state = m_getStatus(static_cast<UINT>(deviceId));
        if (state == kVjoyStatusBusy) {
            if (status) *status = QString(u"Device %1 busy in another application"_qs).arg(deviceId);
            return false;
        }
        if (state == kVjoyStatusMissing || state == kVjoyStatusUnknown) {
            if (status) *status = QString(u"Device %1 is unavailable"_qs).arg(deviceId);
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
            *status = QString(u"Device %1 Ready · %2 axes · %3 buttons · %4 continuous / %5 discrete POV"_qs)
                .arg(deviceId).arg(axisCount).arg(buttons).arg(povs.continuous).arg(povs.discrete);
        }
        return true;
    }

    bool acquire(int deviceId, QString *status)
    {
        if (!checkDevice(deviceId, status)) {
            return false;
        }
        if (m_acquired && m_deviceId == deviceId) {
            return true;
        }
        release();
        const int state = m_getStatus(static_cast<UINT>(deviceId));
        // vJoy reports OWN as 0 and FREE as 1. A FREE device must be acquired
        // before any SetAxis/SetBtn call; OWN is already this process, while
        // BUSY/UNAVAILABLE were rejected above.
        if (state == kVjoyStatusFree && !m_acquire(static_cast<UINT>(deviceId))) {
            if (status) *status = QString(u"Could not acquire vJoy device %1"_qs).arg(deviceId);
            return false;
        }
        if (state != kVjoyStatusFree && state != kVjoyStatusOwn) {
            if (status) *status = QString(u"Device %1 is unavailable"_qs).arg(deviceId);
            return false;
        }
        m_acquired = true;
        m_deviceId = deviceId;
        if (status) {
            const std::array<bool, kVirtualAxisSlotCount> axes = axisCapabilities(deviceId);
            const int axisCount = static_cast<int>(std::count(axes.begin() + 1, axes.end(), true));
            const PovCapabilities povs = povCapabilities(deviceId, nullptr);
            *status = QString(u"Device %1 Ready · %2 axes · %3 buttons · %4 continuous / %5 discrete POV"_qs)
                .arg(deviceId).arg(axisCount).arg(buttonCapacity(deviceId, nullptr))
                .arg(povs.continuous).arg(povs.discrete);
        }
        return true;
    }

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
    bool m_acquired = false;
    int m_deviceId = 0;
};

struct DirectInputDevice {
    GUID guid{};
    QString name;
};

bool requiresMultiDeviceRuntime(const MapperConfiguration &configuration)
{
    const DeviceRig *rig = findDeviceRig(configuration, configuration.activeDeviceRigId);
    if (!rig || !rig->enabled) return false;
    const int memberCount = static_cast<int>(std::count_if(rig->members.cbegin(), rig->members.cend(),
        [](const DeviceRigMember &member) { return member.enabled; }));
    const int outputCount = static_cast<int>(std::count_if(rig->outputs.cbegin(), rig->outputs.cend(),
        [](const DeviceRigOutputTarget &output) { return output.enabled; }));
    return memberCount > 1 || outputCount > 1;
}

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
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
};

BOOL CALLBACK enumObjectCallback(const DIDEVICEOBJECTINSTANCEW *instance, VOID *context)
{
    auto *objects = static_cast<ObjectEnumerationContext *>(context);
    const DWORD objectType = DIDFT_GETTYPE(instance->dwType);
    if ((objectType & DIDFT_AXIS) != 0) {
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(range);
        range.diph.dwHeaderSize = sizeof(range.diph);
        range.diph.dwHow = DIPH_BYID;
        range.diph.dwObj = instance->dwType;
        range.lMin = -10000;
        range.lMax = 10000;
        objects->device->SetProperty(DIPROP_RANGE, &range.diph);
        const int index = axisIndexForOffset(instance->dwOfs);
        if (index >= 0) {
            (*objects->axes)[index] = true;
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
    m_runtime.adaptiveRuntimeEnabled[index].store(false, std::memory_order_relaxed);
    m_runtime.adaptiveRuntimeMaximumHorizonSeconds[index].store(0.008F, std::memory_order_relaxed);
    m_runtime.adaptiveRuntimeMaximumLead[index].store(0.12F, std::memory_order_relaxed);
    m_runtime.mappingActive.store(false, std::memory_order_relaxed);
    m_runtime.mappingEffectiveState.store(static_cast<int>(MappingEffectiveState::Suspended),
                                          std::memory_order_relaxed);
    m_runtime.vjoyReady.store(false, std::memory_order_relaxed);
    m_runtime.inputReports.fetch_add(1, std::memory_order_relaxed);
    m_runtime.physicalReportsSinceAcquisition.fetch_add(1, std::memory_order_relaxed);
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
    const bool requestedMultiRuntime = requiresMultiDeviceRuntime(configuration);
    QMutexLocker locker(&m_configurationMutex);
    if (requiresMultiDeviceRuntime(m_configuration) != requestedMultiRuntime) {
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

void MappingWorker::run()
{
    LPDIRECTINPUT8W directInput = nullptr;
    const HRESULT initialized = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr);
    if (FAILED(initialized)) {
        emit workerEvent(u"Could not initialize DirectInput: "_qs + inputErrorMessage(initialized));
        return;
    }

    while (!m_stopRequested.load()) {
        if (requiresMultiDeviceRuntime(configurationCopy())) {
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
    std::array<bool, kPhysicalAxisCount> fixedAxes{};
    std::array<bool, kMaximumPhysicalButtons> availableButtons{};
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
    if (const ControllerProfile *profile = findProfile(configuration, configuration.activeProfileId)) {
        if (const VirtualOutputLayout *layout = findOutputLayout(configuration, profile->outputLayoutId)) {
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
        availableButtons.fill(false);
        physicalMonitor.disconnect();
        meaningfulInput = {};
        latestMeaningfulInputSequence = 0;
        lastPublishedMeaningfulInputSequence = 0;
        for (auto &axis : m_runtime.axisAvailable) axis = false;
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
        ObjectEnumerationContext objects{device, &availableAxes, &availableButtons};
        device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
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
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            fixedAxes[static_cast<size_t>(index)] = configuration.axisActivity[static_cast<size_t>(index)]
                == PhysicalAxisActivity::Fixed;
        }
        outputLayoutAxes.fill(false);
        if (const ControllerProfile *profile = findProfile(configuration, configuration.activeProfileId)) {
            if (const VirtualOutputLayout *layout = findOutputLayout(configuration, profile->outputLayoutId)) {
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

        if (now >= nextVjoyCheck) {
            QString status;
            const bool ready = vjoy.checkDevice(configuration.vjoyDeviceId, &status);
            m_runtime.vjoyReady = ready;
            setVjoyStatus(status);
            nextVjoyCheck = now + std::chrono::seconds(1);
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

        PhysicalInputReport physicalReport;
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (!availableAxes[index]) continue;
            physicalReport.axes[index] = normalizedFromDirectInput(directInputValue(state, index));
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
            const int target = static_cast<int>(activeMapping->axes[static_cast<size_t>(index)].profile.target);
            routableAxes[static_cast<size_t>(index)] = availableAxes[static_cast<size_t>(index)]
                && !fixedAxes[static_cast<size_t>(index)]
                && target > 0 && target < kVirtualAxisSlotCount
                && outputLayoutAxes[static_cast<size_t>(target)];
        }
        const VirtualAxisOutputPlan axisOutputPlan = buildVirtualAxisOutputPlan(
            *activeMapping, routableAxes, transformedAxes, configuration.disabledAxisValue);
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
            && std::chrono::steady_clock::now() >= nextVjoyAcquire) {
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
                m_runtime.mappingActive = true;
                m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Active);
                m_runtime.outputNeutralized = false;
                m_runtime.vjoyReady = true;
                setVjoyStatus(status);
                emit workerEvent(u"Mapping active"_qs);
            } else {
                m_runtime.vjoyReady = false;
                m_runtime.mappingEffectiveState = static_cast<int>(MappingEffectiveState::Suspended);
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

            VirtualButtonStates desiredButtons = mapButtonStates(
                latestPhysicalButtons, runtimeButtonTargets, vjoyButtonCapacity);
            mapPovStates(desiredButtons, latestPovValues, m_runtime.povCount.load(),
                         runtimePovTargets, vjoyButtonCapacity);
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
            // Native POV passthrough is deliberately a separate path from
            // direction-to-button and profile-control handling. It preserves
            // a continuous DirectInput angle whenever vJoy exposes one.
            const int nativePovHats = std::min(m_runtime.povCount.load(), kMaximumPhysicalPovs);
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
        std::array<bool, kPhysicalAxisCount> availableAxes{};
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

        InputSession() { lastNativePovs.fill(-2); }
    };
    struct OutputSession {
        const CompiledDeviceRigOutput *configured = nullptr;
        VJoyAdapter vjoy;
        std::array<bool, kVirtualAxisSlotCount> axes{};
        std::array<float, kVirtualAxisSlotCount> lastAxes{};
        VirtualButtonStates lastButtons{};
        int buttonCapacity = 0;
        int continuousPovCapacity = 0;
        int discretePovCapacity = 0;
        bool ready = false;
        bool acquired = false;
        std::chrono::steady_clock::time_point nextCheck{};

        OutputSession() { lastAxes.fill(std::numeric_limits<float>::quiet_NaN()); }
    };

    std::array<InputSession, kMaximumDeviceRigMembers> inputs{};
    std::array<OutputSession, kMaximumDeviceRigOutputs> outputs{};
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
        for (std::atomic_bool &button : m_runtime.buttonAvailable) button = false;
        for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
        for (std::atomic_bool &button : m_runtime.physicalButtonPressed) button = false;
        m_runtime.axisCount = 0;
        m_runtime.buttonCount = 0;
        m_runtime.povCount = 0;
        m_runtime.lastPhysicalButton = 0;
        m_runtime.lastPhysicalButtonTarget = 0;
    };
    const auto releaseInput = [](InputSession &session) {
        if (session.device) {
            session.device->Unacquire();
            session.device->Release();
            session.device = nullptr;
        }
        session.availableAxes.fill(false);
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
    };
    const auto refreshOutput = [this](OutputSession &output, QString *status) {
        if (!output.configured) return false;
        QString checkStatus;
        if (!output.vjoy.checkDevice(output.configured->vjoyDeviceId, &checkStatus)) {
            output.ready = false;
            if (status) *status = checkStatus;
            return false;
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
        ObjectEnumerationContext objects{device, &session.availableAxes, &session.availableButtons};
        device->EnumObjects(enumObjectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
        if (FAILED(device->Acquire())) {
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
                continue;
            }

            PhysicalInputReport report;
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                if (session.availableAxes[static_cast<size_t>(axis)]) {
                    report.axes[static_cast<size_t>(axis)] = normalizedFromDirectInput(
                        directInputValue(state, axis));
                }
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
                RuntimeAdaptiveResponseConfig prediction = mapping.adaptiveResponse;
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
                    prediction = applyAdaptiveResponseRuntimeOverride(prediction, activeOverlay);
                }
                prediction.maximumLead = 0.50F;
                const AdaptiveResponseTelemetry adaptive = session.adaptive[static_cast<size_t>(axis)].process(
                    resolved, prediction, timestamp);
                const AdaptiveMappedAxisOutput mapped = applyCurveAwareAdaptiveResponse(resolved,
                    adaptive.predicted, mapping.adaptiveResponse.enabled,
                    mapping.adaptiveResponse.maximumLead, mapping,
                    session.hysteresis[static_cast<size_t>(axis)]);
                session.transformed[static_cast<size_t>(axis)] = mapped.adaptiveOutput;
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
                    m_runtime.adaptiveOutput[static_cast<size_t>(axis)] = mapped.adaptiveOutput;
                    m_runtime.adaptiveRuntimeEnabled[static_cast<size_t>(axis)] = mapping.adaptiveResponse.enabled;
                }
            }
            session.automation.applyAxisActions(automationInput, session.transformed);
            if (index == 0) {
                for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                    m_runtime.axisAvailable[static_cast<size_t>(axis)] = session.availableAxes[static_cast<size_t>(axis)];
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
            if (now >= output.nextCheck) {
                QString status;
                refreshOutput(output, &status);
                if (index == 0) outputStatus = status;
                output.nextCheck = now + std::chrono::seconds(1);
            }
            allOutputsReady = allOutputsReady && output.ready;
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
                for (int outputIndex = 0; outputIndex < plan.outputCount; ++outputIndex) {
                    OutputSession &output = outputs[static_cast<size_t>(outputIndex)];
                    std::array<float, kVirtualAxisSlotCount> desiredAxes{};
                    desiredAxes.fill(sanitizedDisabledAxisValue(configuration.disabledAxisValue));
                    VirtualButtonStates desiredButtons{};
                    bool outputChanged = false;
                    quint64 mappedInputSequence = 0;
                    for (int memberIndex = 0; memberIndex < plan.memberCount; ++memberIndex) {
                        InputSession &input = inputs[static_cast<size_t>(memberIndex)];
                        if (!input.connected || input.member->outputIndex != outputIndex) continue;
                        mappedInputSequence = std::max(mappedInputSequence, input.latestMeaningfulInputSequence);
                        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                            const int target = static_cast<int>(input.member->mapping.axes[static_cast<size_t>(axis)].profile.target);
                            if (input.availableAxes[static_cast<size_t>(axis)]
                                && !input.member->fixedAxes[static_cast<size_t>(axis)]
                                && target > 0 && target < kVirtualAxisSlotCount
                                && output.axes[static_cast<size_t>(target)]) {
                                desiredAxes[static_cast<size_t>(target)] = input.transformed[static_cast<size_t>(axis)];
                            }
                        }
                        const RuntimeButtonTargets buttonTargets = buildRuntimeButtonTargets(
                            input.member->mapping.buttons, output.buttonCapacity);
                        VirtualButtonStates localButtons = mapButtonStates(input.monitor.snapshot().buttons,
                            buttonTargets, output.buttonCapacity);
                        const RuntimePovTargets povTargets = buildRuntimePovTargets(
                            input.member->mapping.povs, output.buttonCapacity);
                        mapPovStates(localButtons, input.monitor.snapshot().povs, input.povCount,
                                     povTargets, output.buttonCapacity);
                        for (int button = 1; button <= output.buttonCapacity; ++button) {
                            desiredButtons[static_cast<size_t>(button)] = desiredButtons[static_cast<size_t>(button)]
                                || localButtons[static_cast<size_t>(button)]
                                || input.automationEffects.heldButtons[static_cast<size_t>(button)]
                                || input.automationEffects.toggledButtons[static_cast<size_t>(button)]
                                || input.automationEffects.pulsedButtons[static_cast<size_t>(button)];
                        }
                        for (int pov = 0; pov < input.povCount && pov < kMaximumPhysicalPovs; ++pov) {
                            const NativePovBinding &binding = input.member->nativePovBindings[static_cast<size_t>(pov)];
                            const bool available = binding.targetType == NativePovTargetType::Continuous
                                ? binding.targetIndex <= output.continuousPovCapacity
                                : binding.targetType == NativePovTargetType::Discrete
                                    && binding.targetIndex <= output.discretePovCapacity;
                            const int desired = input.monitor.snapshot().povs[static_cast<size_t>(pov)];
                            if (binding.enabled && available
                                && desired != input.lastNativePovs[static_cast<size_t>(pov)]
                                && output.vjoy.setPov(binding, desired)) {
                                input.lastNativePovs[static_cast<size_t>(pov)] = desired;
                                ++m_runtime.vjoyWrites;
                                ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                                outputChanged = true;
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
                        }
                    }
                    for (int button = 1; button <= output.buttonCapacity; ++button) {
                        const bool desired = desiredButtons[static_cast<size_t>(button)];
                        if (desired == output.lastButtons[static_cast<size_t>(button)]) continue;
                        if (output.vjoy.setButton(button, desired)) {
                            output.lastButtons[static_cast<size_t>(button)] = desired;
                            ++m_runtime.vjoyWrites;
                            ++m_runtime.deviceRigOutputWrites[static_cast<size_t>(outputIndex)];
                            outputChanged = true;
                        }
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
        m_runtime.vjoyReady = allOutputsReady;
        if (!outputStatus.isEmpty()) setVjoyStatus(outputStatus);
        if (connectedCount == 0) QThread::msleep(25);
        else QThread::msleep(kPhysicalPollIntervalMs);
    }

    for (int index = 0; index < plan.outputCount; ++index) {
        quiesceOutput(outputs[static_cast<size_t>(index)]);
        releaseOutput(outputs[static_cast<size_t>(index)]);
    }
    for (int index = 0; index < plan.memberCount; ++index) releaseInput(inputs[static_cast<size_t>(index)]);
    m_runtime.mappingActive = false;
    m_runtime.outputNeutralized = true;
    m_runtime.vjoyReady = false;
    m_runtime.physicalConnected = false;
    clearPrimarySnapshot();
}

} // namespace hotas
