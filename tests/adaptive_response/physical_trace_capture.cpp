#include "axis_transform.h"
#include "config_store.h"
#include "physical_trace_io.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <dinput.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <optional>
#include <thread>
#include <vector>

namespace {

using namespace hotas;
using namespace hotas::verification;
using namespace Qt::StringLiterals;

struct DirectInputDevice {
    GUID guid{};
    QString name;
};

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

float normalizedFromDirectInput(LONG value, LONG minimum, LONG maximum)
{
    if (minimum >= maximum) return 0.0F;
    const double span = static_cast<double>(maximum) - static_cast<double>(minimum);
    const double normalized = (static_cast<double>(value) - static_cast<double>(minimum)) / span;
    return std::clamp(static_cast<float>(normalized * 2.0 - 1.0), -1.0F, 1.0F);
}

QString guidToString(const GUID &guid)
{
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return QString::fromWCharArray(text);
}

QString inputError(HRESULT result)
{
    return QString(u"DirectInput error 0x%1"_qs)
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

BOOL CALLBACK enumerateDevices(const DIDEVICEINSTANCEW *instance, VOID *context)
{
    auto *devices = static_cast<std::vector<DirectInputDevice> *>(context);
    devices->push_back({instance->guidInstance, QString::fromWCharArray(instance->tszProductName)});
    return DIENUM_CONTINUE;
}

struct AxisEnumerationContext {
    LPDIRECTINPUTDEVICE8W device = nullptr;
    int selectedAxis = -1;
    bool selectedAxisAvailable = false;
    LONG reportedMinimum = -10000;
    LONG reportedMaximum = 10000;
};

BOOL CALLBACK enumerateAxis(const DIDEVICEOBJECTINSTANCEW *instance, VOID *context)
{
    auto *axes = static_cast<AxisEnumerationContext *>(context);
    if (axisIndexForOffset(instance->dwOfs) != axes->selectedAxis) return DIENUM_CONTINUE;
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance->dwType;
    range.lMin = -10000;
    range.lMax = 10000;
    if (SUCCEEDED(axes->device->SetProperty(DIPROP_RANGE, &range.diph))) {
        DIPROPRANGE reported{};
        reported.diph.dwSize = sizeof(reported);
        reported.diph.dwHeaderSize = sizeof(reported.diph);
        reported.diph.dwHow = DIPH_BYID;
        reported.diph.dwObj = instance->dwType;
        if (SUCCEEDED(axes->device->GetProperty(DIPROP_RANGE, &reported.diph))
            && reported.lMin < reported.lMax) {
            axes->reportedMinimum = reported.lMin;
            axes->reportedMaximum = reported.lMax;
        }
        axes->selectedAxisAvailable = true;
    }
    return DIENUM_CONTINUE;
}

std::optional<int> axisIndexFromName(const QString &name)
{
    const QString normalized = name.trimmed().toCaseFolded();
    const std::array<QString, kPhysicalAxisCount> names{
        u"x"_qs, u"y"_qs, u"z"_qs, u"rx"_qs, u"ry"_qs, u"rz"_qs, u"slider0"_qs, u"slider1"_qs};
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (normalized == names[static_cast<size_t>(index)]) return index;
    }
    return std::nullopt;
}

QString axisName(int axis)
{
    const std::array<QString, kPhysicalAxisCount> names{
        u"x"_qs, u"y"_qs, u"z"_qs, u"rx"_qs, u"ry"_qs, u"rz"_qs, u"slider0"_qs, u"slider1"_qs};
    return axis >= 0 && axis < kPhysicalAxisCount ? names[static_cast<size_t>(axis)] : QString{};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    // Read the installed mapper's existing calibration without writing it or
    // creating a second QSettings identity for this test-only executable.
    QCoreApplication::setOrganizationName(u"HOTAS Mapper"_qs);
    QCoreApplication::setOrganizationDomain(u"local.hotasmapper"_qs);
    QCoreApplication::setApplicationName(u"HOTAS Mapper"_qs);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Test-only DirectInput physical-axis recorder. It never creates or writes a vJoy output."_qs);
    parser.addHelpOption();
    const QCommandLineOption listOption(u"list"_qs, u"List DirectInput controller ids and exit."_qs);
    const QCommandLineOption deviceOption(u"device-id"_qs, u"Exact DirectInput id to capture (defaults to configured controller)."_qs, u"id"_qs);
    const QCommandLineOption axisOption(u"axis"_qs, u"Required selected physical axis: x, y, z, rx, ry, rz, slider0, slider1."_qs, u"axis"_qs);
    const QCommandLineOption labelOption(u"label"_qs, u"Required human trace label, for example very-fast-reversal-1."_qs, u"label"_qs);
    const QCommandLineOption secondsOption(u"seconds"_qs, u"Bounded capture duration, 1 through 30 seconds (default 5)."_qs, u"seconds"_qs, u"5"_qs);
    const QCommandLineOption pollOption(u"poll-ms"_qs, u"DirectInput polling interval, 1 through 10 ms (default 4)."_qs, u"milliseconds"_qs, u"4"_qs);
    const QCommandLineOption rawMinimumOption(u"raw-min"_qs, u"Raw DirectInput minimum used only to normalize this test capture (default -10000)."_qs, u"value"_qs, u"-10000"_qs);
    const QCommandLineOption rawMaximumOption(u"raw-max"_qs, u"Raw DirectInput maximum used only to normalize this test capture (default 10000)."_qs, u"value"_qs, u"10000"_qs);
    const QCommandLineOption outputOption(u"output"_qs, u"Required output physical trace CSV path."_qs, u"file"_qs);
    parser.addOptions({listOption, deviceOption, axisOption, labelOption, secondsOption, pollOption,
                       rawMinimumOption, rawMaximumOption, outputOption});
    parser.process(application);

    LPDIRECTINPUT8W directInput = nullptr;
    const HRESULT created = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                                IID_IDirectInput8W,
                                                reinterpret_cast<void **>(&directInput), nullptr);
    if (FAILED(created)) {
        QTextStream(stderr) << "Unable to initialize DirectInput: " << inputError(created) << '\n';
        return 2;
    }
    std::vector<DirectInputDevice> devices;
    directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerateDevices, &devices, DIEDFL_ATTACHEDONLY);
    if (parser.isSet(listOption)) {
        QTextStream output(stdout);
        for (const DirectInputDevice &device : devices) {
            output << guidToString(device.guid) << "\t" << device.name << '\n';
        }
        directInput->Release();
        return devices.empty() ? 1 : 0;
    }

    const std::optional<int> selectedAxis = axisIndexFromName(parser.value(axisOption));
    bool secondsOk = false;
    bool pollOk = false;
    bool rawMinimumOk = false;
    bool rawMaximumOk = false;
    const int seconds = parser.value(secondsOption).toInt(&secondsOk);
    const int pollMs = parser.value(pollOption).toInt(&pollOk);
    const LONG rawMinimum = parser.value(rawMinimumOption).toLong(&rawMinimumOk);
    const LONG rawMaximum = parser.value(rawMaximumOption).toLong(&rawMaximumOk);
    const QString label = parser.value(labelOption).trimmed();
    const QString outputPath = parser.value(outputOption).trimmed();
    if (!selectedAxis || !secondsOk || seconds < 1 || seconds > 30 || !pollOk || pollMs < 1 || pollMs > 10
        || !rawMinimumOk || !rawMaximumOk || rawMinimum >= rawMaximum
        || label.isEmpty() || outputPath.isEmpty()) {
        QTextStream(stderr) << "--axis, --label, and --output are required; --seconds must be 1..30, --poll-ms 1..10, and --raw-min must be less than --raw-max.\n";
        directInput->Release();
        return 2;
    }

    const MapperConfiguration configuration = ConfigStore::load();
    const QString configuredDeviceId = parser.isSet(deviceOption) ? parser.value(deviceOption).trimmed()
                                                                    : configuration.preferredDeviceId.trimmed();
    if (configuredDeviceId.isEmpty()) {
        QTextStream(stderr) << "No --device-id was supplied and no configured DirectInput controller is available. Use --list, then rerun with --device-id.\n";
        directInput->Release();
        return 2;
    }
    const auto selectedDevice = std::find_if(devices.cbegin(), devices.cend(), [&configuredDeviceId](const auto &device) {
        return guidToString(device.guid).compare(configuredDeviceId, Qt::CaseInsensitive) == 0;
    });
    if (selectedDevice == devices.cend()) {
        QTextStream(stderr) << "Requested DirectInput controller is not attached: " << configuredDeviceId << '\n';
        directInput->Release();
        return 3;
    }

    LPDIRECTINPUTDEVICE8W device = nullptr;
    const HRESULT opened = directInput->CreateDevice(selectedDevice->guid, &device, nullptr);
    if (FAILED(opened) || !device) {
        QTextStream(stderr) << "Unable to open controller: " << inputError(opened) << '\n';
        directInput->Release();
        return 3;
    }
    if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))
        || FAILED(device->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        QTextStream(stderr) << "Unable to configure DirectInput capture.\n";
        device->Release();
        directInput->Release();
        return 3;
    }
    AxisEnumerationContext axes{device, *selectedAxis};
    device->EnumObjects(enumerateAxis, &axes, DIDFT_AXIS);
    if (!axes.selectedAxisAvailable) {
        QTextStream(stderr) << "Selected axis is not available on the requested controller: " << axisName(*selectedAxis) << '\n';
        device->Release();
        directInput->Release();
        return 3;
    }
    const HRESULT acquired = device->Acquire();
    if (FAILED(acquired)) {
        QTextStream(stderr) << "Unable to acquire controller for read-only capture: " << inputError(acquired) << '\n';
        device->Release();
        directInput->Release();
        return 3;
    }

    PhysicalAxisTrace trace;
    trace.captureLabel = label;
    trace.selectedAxis = *selectedAxis;
    trace.selectedAxisLabel = axisName(*selectedAxis);
    trace.calibrationEnabled = configuration.calibration[static_cast<size_t>(*selectedAxis)].enabled;
    trace.directInputRangeMinimum = axes.reportedMinimum;
    trace.directInputRangeMaximum = axes.reportedMaximum;
    trace.rawNormalizationMinimum = rawMinimum;
    trace.rawNormalizationMaximum = rawMaximum;
    trace.samples.reserve(static_cast<size_t>(seconds * 1000 / pollMs + 8));
    const Calibration calibration = configuration.calibration[static_cast<size_t>(*selectedAxis)];
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t previousTimestampUs = 0;
    LONG previousRaw = 0;
    bool havePrevious = false;
    QString captureError;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - started).count();
        if (elapsed >= static_cast<long long>(seconds) * 1000000LL) break;
        const HRESULT polled = device->Poll();
        if (FAILED(polled)) {
            captureError = u"Controller poll failed: "_qs + inputError(polled);
            break;
        }
        DIJOYSTATE2 state{};
        const HRESULT read = device->GetDeviceState(sizeof(state), &state);
        if (FAILED(read)) {
            captureError = u"Controller read failed: "_qs + inputError(read);
            break;
        }
        const LONG rawDirectInput = directInputValue(state, *selectedAxis);
        const float rawNormalized = normalizedFromDirectInput(rawDirectInput, rawMinimum, rawMaximum);
        const std::uint64_t timestampUs = static_cast<std::uint64_t>(std::max<long long>(1, elapsed));
        const std::uint64_t intervalUs = havePrevious ? timestampUs - previousTimestampUs : 0;
        trace.samples.push_back({timestampUs, rawNormalized,
                                 normalizeCalibrated(rawNormalized, calibration),
                                 true, !havePrevious || rawDirectInput != previousRaw, intervalUs,
                                 intervalUs == 0 ? 0.0 : 1000000.0 / static_cast<double>(intervalUs),
                                 static_cast<std::int32_t>(rawDirectInput)});
        previousTimestampUs = timestampUs;
        previousRaw = rawDirectInput;
        havePrevious = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    device->Unacquire();
    device->Release();
    directInput->Release();
    if (!captureError.isEmpty()) {
        QTextStream(stderr) << captureError << '\n';
        return 4;
    }
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    QString writeError;
    if (!writePhysicalAxisTraceCsv(outputPath, trace, &writeError)) {
        QTextStream(stderr) << "Unable to write trace: " << writeError << '\n';
        return 5;
    }
    QTextStream(stdout) << "Captured " << trace.samples.size() << " read-only physical reports from "
                        << selectedDevice->name << " axis=" << trace.selectedAxisLabel
                        << " calibration=" << (trace.calibrationEnabled ? "enabled" : "identity")
                        << " directInputRange=" << trace.directInputRangeMinimum << ".."
                        << trace.directInputRangeMaximum
                        << " normalizationRange=" << trace.rawNormalizationMinimum << ".."
                        << trace.rawNormalizationMaximum
                        << " output=" << outputPath << '\n';
    return 0;
}
