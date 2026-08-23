#include "mapping_worker.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "physical_input_monitor.h"
#include "profile_trigger_runtime.h"

#include <dinput.h>
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

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
constexpr DWORD kVjoyUsageRz = 0x35;
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
        if (state == kVjoyStatusBusy) {
            if (status) *status = QString(u"Device %1 busy in another application"_qs).arg(deviceId);
            return false;
        }
        if (state == kVjoyStatusMissing || state == kVjoyStatusUnknown) {
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
        const PovCapabilities povs = povCapabilities(deviceId, nullptr);
        if (status) {
            *status = QString(u"Device %1 Ready · %2 buttons · %3 continuous / %4 discrete POV"_qs)
                .arg(deviceId).arg(buttons).arg(povs.continuous).arg(povs.discrete);
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
        m_reset(static_cast<UINT>(m_deviceId));
        if (status) {
            const PovCapabilities povs = povCapabilities(deviceId, nullptr);
            *status = QString(u"Device %1 Ready · %2 buttons · %3 continuous / %4 discrete POV"_qs)
                .arg(deviceId).arg(buttonCapacity(deviceId, nullptr))
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
    using GetPovNumberFn = int(__cdecl *)(UINT);
    using SetContinuousPovFn = BOOL(__cdecl *)(DWORD, UINT, UCHAR);
    using SetDiscretePovFn = BOOL(__cdecl *)(int, UINT, UCHAR);

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
        // POV APIs are optional because a valid vJoy configuration may expose
        // no hats. Their absence makes the target unavailable, never fatal.
        m_getContinuousPovNumber = reinterpret_cast<GetPovNumberFn>(GetProcAddress(m_library, "GetVJDContPovNumber"));
        m_getDiscretePovNumber = reinterpret_cast<GetPovNumberFn>(GetProcAddress(m_library, "GetVJDDiscPovNumber"));
        m_setContinuousPov = reinterpret_cast<SetContinuousPovFn>(GetProcAddress(m_library, "SetContPov"));
        m_setDiscretePov = reinterpret_cast<SetDiscretePovFn>(GetProcAddress(m_library, "SetDiscPov"));
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
        m_getContinuousPovNumber = nullptr;
        m_getDiscretePovNumber = nullptr;
        m_setContinuousPov = nullptr;
        m_setDiscretePov = nullptr;
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
    GetPovNumberFn m_getContinuousPovNumber = nullptr;
    GetPovNumberFn m_getDiscretePovNumber = nullptr;
    SetContinuousPovFn m_setContinuousPov = nullptr;
    SetDiscretePovFn m_setDiscretePov = nullptr;
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

QString findHidHideCli()
{
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
        u"C:/Program Files"_qs,
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString executable = QDir(root).filePath(
            u"Nefarius Software Solutions/HidHide/x64/HidHideCLI.exe"_qs);
        if (QFileInfo(executable).isExecutable()) return executable;
    }
    return {};
}

std::optional<bool> queryHidHideCloakState()
{
    const QString executable = findHidHideCli();
    if (executable.isEmpty()) return std::nullopt;

    QProcess process;
    // HidHideCLI can otherwise keep its inherited input pipe open after
    // printing a result. Explicit EOF makes this a bounded one-shot query.
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, {u"--cloak-state"_qs});
    // HidHideCLI normally exits in a few milliseconds. Waiting for a separate
    // started signal can therefore race its normal exit and report a false
    // unknown state, so wait for the completed command instead.
    if (!process.waitForFinished(800)) {
        process.terminate();
        if (!process.waitForFinished(200)) {
            process.kill();
            process.waitForFinished(1000);
        }
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::nullopt;
    }
    const QByteArray bytes = process.readAll();
    QString output = QString::fromLocal8Bit(bytes);
    // HidHideCLI writes through std::wcout. When stdout is redirected to
    // QProcess it can be UTF-16LE rather than the console code page.
    if (!output.contains(u"--cloak-"_qs) && bytes.size() % 2 == 0) {
        output = QString::fromUtf16(
            reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    }
    if (output.contains(u"--cloak-on"_qs)) return true;
    if (output.contains(u"--cloak-off"_qs)) return false;
    return std::nullopt;
}

QString hidHideOutput(QProcess *process)
{
    const QByteArray bytes = process->readAll();
    QString output = QString::fromLocal8Bit(bytes);
    // HidHideCLI writes through std::wcout. When stdout is redirected to
    // QProcess it can be UTF-16LE rather than the console code page.
    if (!output.contains(u"--"_qs) && bytes.size() % 2 == 0) {
        output = QString::fromUtf16(
            reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    }
    return output;
}

std::optional<bool> queryHidHideMapperAllowed()
{
    const QString executable = findHidHideCli();
    const QString mapperPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (executable.isEmpty() || mapperPath.isEmpty()) return std::nullopt;

    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, {u"--app-list"_qs});
    if (!process.waitForFinished(800)) {
        process.terminate();
        if (!process.waitForFinished(200)) {
            process.kill();
            process.waitForFinished(1000);
        }
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::nullopt;
    }
    return hidHideOutput(&process).contains(mapperPath, Qt::CaseInsensitive);
}

bool registerHidHideMapper()
{
    const QString executable = findHidHideCli();
    const QString mapperPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (executable.isEmpty() || mapperPath.isEmpty()) return false;

    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, {u"--app-reg"_qs, mapperPath});
    if (!process.waitForFinished(800)) {
        process.terminate();
        if (!process.waitForFinished(200)) {
            process.kill();
            process.waitForFinished(1000);
        }
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
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
    for (const auto &device : context.devices) {
        const QString lower = device.name.toLower();
        if (!isVirtualControllerName(device.name)
            && (lower.contains(u"t.flight"_qs) || lower.contains(u"hotas one"_qs))) {
            return device;
        }
    }
    for (const auto &device : context.devices) {
        if (!isVirtualControllerName(device.name)) return device;
    }
    // A mapper must never consume the vJoy controller it produces. Wait for a
    // real DirectInput device rather than creating a feedback loop.
    return std::nullopt;
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
        m_runtime.virtualValues[index] = std::numeric_limits<float>::quiet_NaN();
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
    for (std::atomic_int &pov : m_runtime.povValues) pov = -1;
    for (std::atomic_uint64_t &sample : m_runtime.latencySamples) sample = 0;
    const auto compileStarted = std::chrono::steady_clock::now();
    m_preparedProfileCache = std::make_shared<RuntimeProfileCache>(
        compileRuntimeProfileCache(m_configuration));
    m_runtime.effectiveProfileIndex = m_preparedProfileCache->baseProfileIndex;
    m_runtime.lastCurveCompileUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - compileStarted).count());
}

MappingWorker::~MappingWorker()
{
    requestStop();
    wait(1500);
}

void MappingWorker::refreshHidHideState()
{
    const bool available = hasHidHideService();
    m_runtime.hidhideAvailable = available;
    const std::optional<bool> cloaked = available ? queryHidHideCloakState() : std::nullopt;
    m_runtime.hidhideCloakStateKnown = cloaked.has_value();
    m_runtime.hidhideCloaked = cloaked.value_or(false);
    std::optional<bool> mapperAllowed;
    if (cloaked.value_or(false)) {
        mapperAllowed = queryHidHideMapperAllowed();
        if (!mapperAllowed.value_or(false) && registerHidHideMapper()) {
            mapperAllowed = queryHidHideMapperAllowed();
        }
    }
    m_runtime.hidhideMapperAllowed = mapperAllowed.value_or(!cloaked.value_or(false));
    if (cloaked.value_or(false) && !m_runtime.hidhideMapperAllowed.load()) {
        emit workerEvent(u"HidHide is cloaking the HOTAS and did not allow this mapper executable"_qs);
    }
    emit hardwareStateChanged();
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
    QMutexLocker locker(&m_configurationMutex);
    m_configuration = configuration;
    m_preparedProfileCache = std::move(compiled);
    ++m_configurationVersion;
    m_runtime.lastCurveCompileUs = compileUs;
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
    refreshHidHideState();

    LPDIRECTINPUTDEVICE8W device = nullptr;
    HANDLE inputEvent = nullptr;
    std::array<bool, kPhysicalAxisCount> availableAxes{};
    std::array<bool, kMaximumPhysicalButtons> availableButtons{};
    PhysicalInputMonitor physicalMonitor;
    auto preparedConfiguration = preparedConfigurationCopy();
    MapperConfiguration configuration = std::move(preparedConfiguration.first);
    std::shared_ptr<const RuntimeProfileCache> activeProfileCache
        = std::move(preparedConfiguration.second);
    int effectiveProfileIndex = activeProfileCache->baseProfileIndex;
    const RuntimeMappingConfiguration *activeMapping
        = &activeProfileCache->profiles[static_cast<size_t>(effectiveProfileIndex)];
    ProfileTriggerRuntime profileTriggers;
    quint64 appliedVersion = m_configurationVersion.load();
    std::array<float, 5> lastVirtualValues{};
    lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
    std::array<int, 5> virtualAxisSources{};
    virtualAxisSources.fill(-1);
    std::array<AxisHysteresisState, kPhysicalAxisCount> hysteresisStates{};
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
    bool buttonDefaultsPending = false;
    bool profileTriggerSessionActive = false;
    bool wasMappingRequested = false;
    std::optional<std::chrono::steady_clock::time_point> pendingProfileSwitchStarted;
    std::uint64_t processedReports = 0;
    std::uint64_t latencyTotal = 0;
    std::uint64_t latencySampleSequence = 0;
    auto nextDiscovery = std::chrono::steady_clock::now();
    auto nextVjoyCheck = std::chrono::steady_clock::now();
    auto nextVjoyAcquire = std::chrono::steady_clock::now();

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
        runtimePovTargets = buildRuntimePovTargets(activeMapping->povs, vjoyButtonCapacity,
            activeProfileCache->povProfileTriggers);
    };

    const auto selectEffectiveProfile = [&](const EffectiveProfileSelection &selection,
                                            bool countSwitch) {
        const int selectedIndex = std::clamp(selection.profileIndex, 0,
            static_cast<int>(activeProfileCache->profiles.size()) - 1);
        const bool changed = selectedIndex != effectiveProfileIndex;
        effectiveProfileIndex = selectedIndex;
        activeMapping = &activeProfileCache->profiles[static_cast<size_t>(effectiveProfileIndex)];
        m_runtime.effectiveProfileIndex = effectiveProfileIndex;
        m_runtime.profileOverrideButton = selection.sourceButton;
        m_runtime.profileOverridePovHat = selection.sourcePovHat;
        m_runtime.profileOverridePovDirection = selection.sourcePovDirection;
        m_runtime.profileOverrideMode = static_cast<int>(selection.sourceMode);
        if (!changed) return false;
        // The current physical snapshot is re-evaluated immediately below.
        // Axis cache invalidation forces a same-report output publication;
        // the normal button diff loop releases/asserts changed routes.
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        lastNativePovValues.fill(-2);
        clearVirtualAxisSnapshot();
        for (AxisHysteresisState &state : hysteresisStates) state = {};
        rebuildButtonTargets();
        if (countSwitch) {
            ++m_runtime.profileSwitchCount;
        }
        return true;
    };

    const auto releaseMappingOutput = [&] {
        if (m_runtime.mappingActive.exchange(false)) {
            vjoy.release();
        }
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        lastNativePovValues.fill(-2);
        clearVirtualAxisSnapshot();
        clearVirtualButtonSnapshot();
        for (AxisHysteresisState &state : hysteresisStates) state = {};
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
        profileTriggers.reset();
        profileTriggerSessionActive = false;
        selectEffectiveProfile({activeProfileCache->baseProfileIndex, 0,
                                0, -1, ProfileTriggerMode::Disabled}, false);
        m_runtime.physicalConnected = false;
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
        setDeviceSnapshot({selected->name, guidToString(selected->guid)});
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
            // be disabled. ResetVJD on release is a second safety net.
            for (const NativePovBinding &binding : activeProfileCache->nativePovBindings) {
                if (binding.enabled) vjoy.setPov(binding, -1);
            }
        }
        auto prepared = preparedConfigurationCopy();
        configuration = std::move(prepared.first);
        // The mapping loop only swaps a table that was fully built before the
        // configuration version changed; it never builds a spline or LUT.
        activeProfileCache = std::move(prepared.second);
        // Settings/profile updates receive a fully compiled table and begin a
        // new hysteresis acceptance window on the next report.
        for (AxisHysteresisState &state : hysteresisStates) state = {};
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
        lastVirtualValues.fill(std::numeric_limits<float>::quiet_NaN());
        clearVirtualAxisSnapshot();
        rebuildButtonTargets();
        lastNativePovValues.fill(-2);
        if (configuration.vjoyDeviceId != previousVjoyDeviceId && m_runtime.mappingActive.load()) {
            releaseMappingOutput();
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

    while (!m_stopRequested.load()) {
        const auto now = std::chrono::steady_clock::now();
        applyLatestConfiguration();
        const bool mappingRequestedNow = m_mappingRequested.load();
        if (!mappingRequestedNow && wasMappingRequested) {
            // Stop Mapping never preserves Hold or Toggle latches.
            profileTriggers.reset();
            profileTriggerSessionActive = false;
            selectEffectiveProfile({activeProfileCache->baseProfileIndex, 0,
                                    0, -1, ProfileTriggerMode::Disabled}, false);
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

        // Profile controls are evaluated before axes and normal game buttons,
        // using this DirectInput snapshot only. The selected mapping is an
        // already-compiled cache entry, so a press affects this same report.
        if (mappingRequestedNow) {
            EffectiveProfileSelection selection;
            if (!profileTriggerSessionActive) {
                profileTriggers.initializeForMapping(*activeProfileCache, physicalSnapshot.buttons,
                                                     physicalSnapshot.povs, m_runtime.povCount.load());
                profileTriggerSessionActive = true;
                selection = profileTriggers.effectiveProfile(*activeProfileCache);
            } else {
                selection = profileTriggers.processReport(*activeProfileCache, physicalSnapshot.buttons,
                                                           physicalSnapshot.povs, m_runtime.povCount.load());
            }
            const auto profileSwitchStarted = std::chrono::steady_clock::now();
            const bool changed = selectEffectiveProfile(selection, true);
            if (changed) {
                pendingProfileSwitchStarted = profileSwitchStarted;
            }
        }

        std::array<float, 5> output{};
        std::array<bool, 5> targetUsed{};
        virtualAxisSources.fill(-1);
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (!availableAxes[index]) continue;
            const float raw = physicalSnapshot.axes[index];
            m_runtime.raw[index] = raw;
            if (m_calibrating.load()) {
                m_runtime.calibrationMinimum[index] = std::min(m_runtime.calibrationMinimum[index].load(), raw);
                m_runtime.calibrationMaximum[index] = std::max(m_runtime.calibrationMaximum[index].load(), raw);
            }
            const RuntimeAxisMapping &mapping = activeMapping->axes[index];
            float curveResponse = 0.0F;
            AxisSignalPath signalPath;
            const float transformed = transformAxisLive(raw, mapping, hysteresisStates[index],
                &curveResponse, &signalPath);
            m_runtime.normalized[index] = signalPath.normalized;
            m_runtime.afterDeadzone[index] = signalPath.afterDeadzone;
            m_runtime.afterHysteresis[index] = signalPath.afterHysteresis;
            m_runtime.afterInversion[index] = signalPath.afterInversion;
            m_runtime.curveResponse[index] = curveResponse;
            m_runtime.transformed[index] = transformed;
            const int target = static_cast<int>(mapping.profile.target);
            if (target > 0 && target < static_cast<int>(output.size()) && !targetUsed[target]) {
                output[target] = transformed;
                targetUsed[target] = true;
                virtualAxisSources[target] = index;
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
        for (int hat = 0; hat < kMaximumPhysicalPovs; ++hat) {
            latestPovValues[static_cast<size_t>(hat)] = physicalSnapshot.povs[static_cast<size_t>(hat)];
            m_runtime.povValues[static_cast<size_t>(hat)] = physicalSnapshot.povs[static_cast<size_t>(hat)];
        }

        const bool mappingRequested = mappingRequestedNow;
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
                    const int source = virtualAxisSources[target];
                    if (source >= 0) m_runtime.virtualValues[source] = desired;
                    ++m_runtime.vjoyWrites;
                }
            }

            VirtualButtonStates desiredButtons = mapButtonStates(
                latestPhysicalButtons, runtimeButtonTargets, vjoyButtonCapacity);
            mapPovStates(desiredButtons, latestPovValues, m_runtime.povCount.load(),
                         runtimePovTargets, vjoyButtonCapacity);
            for (int target = 1; target <= kMaximumVirtualButtons; ++target) {
                const bool desired = target <= vjoyButtonCapacity && desiredButtons[target];
                if (desired == lastVirtualButtonStates[target]) continue;
                if (target <= vjoyButtonCapacity && vjoy.setButton(target, desired)) {
                    lastVirtualButtonStates[target] = desired;
                    m_runtime.virtualButtonPressed[target - 1] = desired;
                    ++m_runtime.vjoyWrites;
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
                }
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

    releaseMappingOutput();
    releaseInput();
    directInput->Release();
}

} // namespace hotas
