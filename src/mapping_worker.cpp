#include "mapping_worker.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "physical_input_monitor.h"

#include <dinput.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <iterator>
#include <optional>
#include <QStringList>
#include <string>
#include <utility>
#include <vector>

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

constexpr DWORD kVjoyUsageX = 0x30;
constexpr DWORD kVjoyUsageY = 0x31;
constexpr DWORD kVjoyUsageZ = 0x32;
constexpr DWORD kVjoyUsageRz = 0x35;
constexpr LONG kVjoyMinimum = 0;
constexpr LONG kVjoyMaximum = 32767;
constexpr DWORD kPhysicalPollIntervalMs = 4; // 250 Hz bounded worker cadence.

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
    case VirtualAxis::Rz: return kVjoyUsageRz;
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
    ~VJoyAdapter() { unload(); }

    bool checkDevice(int deviceId, QString *status)
    {
        if (!load(status)) {
            return false;
        }
        const int state = m_getStatus(static_cast<UINT>(deviceId));
        if (state == 2) {
            if (status) *status = QString(u"Device %1 busy in another application"_qs).arg(deviceId);
            return false;
        }
        if (state == 3 || state == 4) {
            if (status) *status = QString(u"Device %1 is unavailable"_qs).arg(deviceId);
            return false;
        }
        for (const DWORD usage : {kVjoyUsageX, kVjoyUsageY, kVjoyUsageZ, kVjoyUsageRz}) {
            if (!m_axisExists(static_cast<UINT>(deviceId), usage)) {
                if (status) *status = QString(u"Device %1 must expose X, Y, Z and Rz"_qs).arg(deviceId);
                return false;
            }
        }
        const int buttons = buttonCapacity(deviceId, nullptr);
        if (status) {
            *status = QString(u"Device %1 Ready · %2 buttons"_qs).arg(deviceId).arg(buttons);
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
        // A FREE device must be acquired before any SetAxis/SetBtn call. OWN
        // is already this process, while BUSY/UNAVAILABLE were rejected above.
        if (state == 0 && !m_acquire(static_cast<UINT>(deviceId))) {
            if (status) *status = QString(u"Could not acquire vJoy device %1"_qs).arg(deviceId);
            return false;
        }
        m_acquired = true;
        m_deviceId = deviceId;
        m_reset(static_cast<UINT>(m_deviceId));
        if (status) *status = QString(u"Device %1 Ready · %2 buttons"_qs)
            .arg(deviceId).arg(buttonCapacity(deviceId, nullptr));
        return true;
    }

    bool setAxis(VirtualAxis axis, float value)
    {
        if (!m_acquired || axis == VirtualAxis::Disabled) {
            return false;
        }
        return m_setAxis(vjoyValue(value), static_cast<UINT>(m_deviceId), vjoyUsage(axis));
    }

    int buttonCapacity(int deviceId, QString *status)
    {
        if (!load(status) || !m_getButtonNumber) {
            return 0;
        }
        const int reported = m_getButtonNumber(static_cast<UINT>(deviceId));
        return std::clamp(reported, 0, kMaximumVirtualButtons);
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
        if (m_acquired && m_reset && m_relinquish) {
            m_reset(static_cast<UINT>(m_deviceId));
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
    using ResetVJDFn = BOOL(__cdecl *)(UINT);

    bool load(QString *status)
    {
        if (m_library) {
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
        m_reset = reinterpret_cast<ResetVJDFn>(GetProcAddress(m_library, "ResetVJD"));
        if (!m_getStatus || !m_axisExists || !m_acquire || !m_relinquish || !m_setAxis || !m_reset) {
            if (status) *status = u"vJoyInterface.dll is missing a required API"_qs;
            unload();
            return false;
        }
        return true;
    }

    void unload()
    {
        release();
        if (m_library) FreeLibrary(m_library);
        m_library = nullptr;
        m_getStatus = nullptr;
        m_axisExists = nullptr;
        m_acquire = nullptr;
        m_relinquish = nullptr;
        m_setAxis = nullptr;
        m_getButtonNumber = nullptr;
        m_setButton = nullptr;
        m_reset = nullptr;
    }

    HMODULE m_library = nullptr;
    GetVJDStatusFn m_getStatus = nullptr;
    GetVJDAxisExistFn m_axisExists = nullptr;
    AcquireVJDFn m_acquire = nullptr;
    RelinquishVJDFn m_relinquish = nullptr;
    SetAxisFn m_setAxis = nullptr;
    GetVJDButtonNumberFn m_getButtonNumber = nullptr;
    SetBtnFn m_setButton = nullptr;
    ResetVJDFn m_reset = nullptr;
    bool m_acquired = false;
    int m_deviceId = 0;
};

bool hasHidHideService()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, L"HidHide", SERVICE_QUERY_STATUS);
    const bool found = service != nullptr;
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return found;
}

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
        ++objects->povCount;
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
            if (guidToString(device.guid) == configuration.preferredDeviceId) {
                return device;
            }
        }
    }
    for (const auto &device : context.devices) {
        const QString lower = device.name.toLower();
        if (lower.contains(u"t.flight"_qs) || lower.contains(u"hotas one"_qs)) {
            return device;
        }
    }
    return context.devices.front();
}

} // namespace

MappingWorker::MappingWorker(MapperConfiguration configuration, QObject *parent)
    : QThread(parent), m_configuration(std::move(configuration))
{
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        m_runtime.raw[index] = 0.0F;
        m_runtime.transformed[index] = 0.0F;
        m_runtime.virtualValues[index] = 0.0F;
        m_runtime.axisAvailable[index] = false;
        m_runtime.calibrationMinimum[index] = -1.0F;
        m_runtime.calibrationCenter[index] = 0.0F;
        m_runtime.calibrationMaximum[index] = 1.0F;
    }
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        m_runtime.physicalButtonPressed[index] = false;
        m_runtime.virtualButtonPressed[index] = false;
        m_runtime.buttonAvailable[index] = false;
    }
}

MappingWorker::~MappingWorker()
{
    requestStop();
    wait(1500);
}

void MappingWorker::updateConfiguration(const MapperConfiguration &configuration)
{
    QMutexLocker locker(&m_configurationMutex);
    m_configuration = configuration;
    ++m_configurationVersion;
}

void MappingWorker::setMappingEnabled(bool enabled)
{
    m_mappingRequested = enabled;
}

bool MappingWorker::mappingRequested() const
{
    return m_mappingRequested.load();
}

void MappingWorker::requestStop()
{
    m_stopRequested = true;
}

void MappingWorker::beginCalibration()
{
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const float current = m_runtime.raw[index].load();
        m_runtime.calibrationMinimum[index] = current;
        m_runtime.calibrationCenter[index] = current;
        m_runtime.calibrationMaximum[index] = current;
    }
    m_calibrating = true;
}

void MappingWorker::cancelCalibration()
{
    m_calibrating = false;
}

bool MappingWorker::calibrationRunning() const
{
    return m_calibrating.load();
}

std::array<Calibration, kPhysicalAxisCount> MappingWorker::capturedCalibration() const
{
    std::array<Calibration, kPhysicalAxisCount> captured;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        captured[index] = {
            true,
            m_runtime.calibrationMinimum[index].load(),
            m_runtime.calibrationCenter[index].load(),
            m_runtime.calibrationMaximum[index].load(),
        };
    }
    return captured;
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

MapperConfiguration MappingWorker::configurationCopy()
{
    QMutexLocker locker(&m_configurationMutex);
    return m_configuration;
}

void MappingWorker::setDeviceSnapshot(const DeviceSnapshot &snapshot)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_deviceMutex);
        changed = m_device.name != snapshot.name || m_device.id != snapshot.id;
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

    VJoyAdapter vjoy;
    m_runtime.hidhideAvailable = hasHidHideService();
    emit hardwareStateChanged();

    LPDIRECTINPUTDEVICE8W device = nullptr;
    HANDLE inputEvent = nullptr;
    std::array<bool, kPhysicalAxisCount> availableAxes{};
    std::array<bool, kMaximumPhysicalButtons> availableButtons{};
    PhysicalInputMonitor physicalMonitor;
    MapperConfiguration configuration = configurationCopy();
    quint64 appliedVersion = m_configurationVersion.load();
    std::array<float, 5> lastVirtualValues{};
    lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
    PhysicalButtonStates latestPhysicalButtons{};
    RuntimeButtonTargets runtimeButtonTargets{};
    VirtualButtonStates lastVirtualButtonStates{};
    int vjoyButtonCapacity = 0;
    bool buttonDefaultsPending = false;
    std::uint64_t processedReports = 0;
    std::uint64_t latencyTotal = 0;
    auto nextDiscovery = std::chrono::steady_clock::now();
    auto nextVjoyCheck = std::chrono::steady_clock::now();
    auto nextVjoyAcquire = std::chrono::steady_clock::now();

    const auto clearVirtualButtonSnapshot = [&] {
        lastVirtualButtonStates.fill(false);
        for (auto &button : m_runtime.virtualButtonPressed) button = false;
    };

    const auto clearPhysicalButtonSnapshot = [&] {
        latestPhysicalButtons.fill(false);
        for (auto &button : m_runtime.physicalButtonPressed) button = false;
        m_runtime.lastPhysicalButton = 0;
        m_runtime.lastPhysicalButtonTarget = 0;
    };

    const auto rebuildButtonTargets = [&] {
        runtimeButtonTargets = buildRuntimeButtonTargets(configuration.buttons, vjoyButtonCapacity);
    };

    const auto releaseMappingOutput = [&] {
        if (m_runtime.mappingActive.exchange(false)) {
            vjoy.release();
        }
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        clearVirtualButtonSnapshot();
    };

    const auto releaseInput = [&] {
        if (m_runtime.mappingActive.load()) {
            releaseMappingOutput();
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
        for (auto &axis : m_runtime.axisAvailable) axis = false;
        for (auto &button : m_runtime.buttonAvailable) button = false;
        clearPhysicalButtonSnapshot();
        clearVirtualButtonSnapshot();
        m_runtime.physicalConnected = false;
        m_runtime.axisCount = 0;
        m_runtime.buttonCount = 0;
        m_runtime.povCount = 0;
        m_runtime.povValue = -1;
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
        if (FAILED(acquired) && acquired != DIERR_INPUTLOST && acquired != DIERR_NOTACQUIRED) {
            emit workerEvent(u"Could not acquire controller: "_qs + inputErrorMessage(acquired));
            releaseInput();
            return;
        }
        physicalMonitor.configure(availableAxes, availableButtons, objects.povCount);
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            m_runtime.axisAvailable[index] = availableAxes[index];
        }
        for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
            m_runtime.buttonAvailable[index] = availableButtons[index];
        }
        m_runtime.axisCount = objects.axisCount;
        m_runtime.buttonCount = std::min(objects.buttonCount, kMaximumPhysicalButtons);
        m_runtime.povCount = objects.povCount;
        m_runtime.povValue = -1;
        m_runtime.physicalConnected = true;
        setDeviceSnapshot({selected->name, guidToString(selected->guid)});
        emit workerEvent(QString(u"Controller connected: %1 · %2 axes · %3 buttons"_qs)
            .arg(selected->name).arg(objects.axisCount).arg(m_runtime.buttonCount.load()));
        if (inputEvent) SetEvent(inputEvent); // Promptly publish an initial state.
    };

    const auto suggestDefaultButtonsIfNeeded = [&] {
        const int physicalCount = m_runtime.buttonCount.load();
        if (device && configuration.buttons.empty() && physicalCount > 0
            && vjoyButtonCapacity > 0 && !buttonDefaultsPending) {
            buttonDefaultsPending = true;
            emit buttonConfigurationSuggested(physicalCount, vjoyButtonCapacity);
        }
    };

    const auto refreshVjoyButtonCapacity = [&] {
        const int reportedCapacity = vjoy.buttonCapacity(configuration.vjoyDeviceId, nullptr);
        if (reportedCapacity != vjoyButtonCapacity) {
            vjoyButtonCapacity = reportedCapacity;
            m_runtime.vjoyButtonCount = vjoyButtonCapacity;
            rebuildButtonTargets();
            if (inputEvent) SetEvent(inputEvent);
            emit hardwareStateChanged();
        }
        suggestDefaultButtonsIfNeeded();
    };

    const auto applyLatestConfiguration = [&] {
        const quint64 currentVersion = m_configurationVersion.load();
        if (currentVersion == appliedVersion) return;
        const int previousVjoyDeviceId = configuration.vjoyDeviceId;
        configuration = configurationCopy();
        appliedVersion = currentVersion;
        buttonDefaultsPending = !configuration.buttons.empty();
        rebuildButtonTargets();
        if (configuration.vjoyDeviceId != previousVjoyDeviceId && m_runtime.mappingActive.load()) {
            releaseMappingOutput();
            emit workerEvent(u"vJoy device changed; reacquiring mapping output"_qs);
        }
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        if (inputEvent) SetEvent(inputEvent);
    };

    while (!m_stopRequested.load()) {
        const auto now = std::chrono::steady_clock::now();
        applyLatestConfiguration();
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
            refreshVjoyButtonCapacity();
        }

        if (!device) {
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
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 && inputEvent) ResetEvent(inputEvent);

        const auto started = std::chrono::steady_clock::now();
        const HRESULT pollResult = device->Poll();
        if (pollResult == DIERR_INPUTLOST || pollResult == DIERR_NOTACQUIRED) {
            device->Acquire();
            continue;
        }
        if (FAILED(pollResult)) {
            emit workerEvent(u"Controller poll failed: "_qs + inputErrorMessage(pollResult));
            releaseInput();
            continue;
        }
        DIJOYSTATE2 state{};
        const HRESULT readResult = device->GetDeviceState(sizeof(state), &state);
        if (readResult == DIERR_INPUTLOST || readResult == DIERR_NOTACQUIRED) {
            device->Acquire();
            continue;
        }
        if (FAILED(readResult)) {
            emit workerEvent(u"Controller disconnected: "_qs + inputErrorMessage(readResult));
            releaseInput();
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
        physicalReport.pov = (m_runtime.povCount.load() > 0 && state.rgdwPOV[0] != 0xFFFFFFFFUL)
            ? static_cast<int>(state.rgdwPOV[0]) : -1;
        physicalMonitor.accept(physicalReport);
        const PhysicalInputSnapshot &physicalSnapshot = physicalMonitor.snapshot();

        std::array<float, 5> output{};
        std::array<bool, 5> targetUsed{};
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (!availableAxes[index]) continue;
            const float raw = physicalSnapshot.axes[index];
            m_runtime.raw[index] = raw;
            if (m_calibrating.load()) {
                m_runtime.calibrationMinimum[index] = std::min(m_runtime.calibrationMinimum[index].load(), raw);
                m_runtime.calibrationMaximum[index] = std::max(m_runtime.calibrationMaximum[index].load(), raw);
            }
            const AxisMapping &mapping = configuration.axes[index];
            const float transformed = transformAxis(raw, mapping);
            m_runtime.transformed[index] = transformed;
            m_runtime.virtualValues[index] = 0.0F;
            const int target = static_cast<int>(mapping.target);
            if (target > 0 && target < static_cast<int>(output.size()) && !targetUsed[target]) {
                output[target] = transformed;
                targetUsed[target] = true;
                m_runtime.virtualValues[index] = transformed;
            }
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
        m_runtime.povValue = physicalSnapshot.pov;

        const bool mappingRequested = m_mappingRequested.load();
        if (mappingRequested && !m_runtime.mappingActive.load()
            && std::chrono::steady_clock::now() >= nextVjoyAcquire) {
            QString status;
            if (vjoy.acquire(configuration.vjoyDeviceId, &status)) {
                m_runtime.mappingActive = true;
                m_runtime.vjoyReady = true;
                setVjoyStatus(status);
                emit workerEvent(u"Mapping active"_qs);
            } else {
                m_runtime.vjoyReady = false;
                setVjoyStatus(status);
                nextVjoyAcquire = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
        if (!mappingRequested && m_runtime.mappingActive.load()) {
            releaseMappingOutput();
            emit workerEvent(u"Mapping stopped"_qs);
        }
        if (m_runtime.mappingActive.load()) {
            for (int target = 1; target < static_cast<int>(output.size()); ++target) {
                const float desired = targetUsed[target] ? output[target] : 0.0F;
                if (std::isfinite(lastVirtualValues[target])
                    && std::abs(desired - lastVirtualValues[target]) < 0.00001F) {
                    continue;
                }
                if (vjoy.setAxis(static_cast<VirtualAxis>(target), desired)) {
                    lastVirtualValues[target] = desired;
                    ++m_runtime.vjoyWrites;
                }
            }

            const VirtualButtonStates desiredButtons = mapButtonStates(
                latestPhysicalButtons, runtimeButtonTargets, vjoyButtonCapacity);
            for (int target = 1; target <= kMaximumVirtualButtons; ++target) {
                const bool desired = target <= vjoyButtonCapacity && desiredButtons[target];
                if (desired == lastVirtualButtonStates[target]) continue;
                if (target <= vjoyButtonCapacity && vjoy.setButton(target, desired)) {
                    lastVirtualButtonStates[target] = desired;
                    m_runtime.virtualButtonPressed[target - 1] = desired;
                    ++m_runtime.vjoyWrites;
                }
            }
        }

        const auto finished = std::chrono::steady_clock::now();
        const auto latency = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
        ++processedReports;
        latencyTotal += latency;
        m_runtime.inputReports = processedReports;
        m_runtime.latencyCurrentUs = latency;
        m_runtime.latencyAverageUs = latencyTotal / processedReports;
        std::uint64_t peak = m_runtime.latencyPeakUs.load();
        while (latency > peak && !m_runtime.latencyPeakUs.compare_exchange_weak(peak, latency)) {}
    }

    releaseMappingOutput();
    releaseInput();
    directInput->Release();
}

} // namespace hotas
