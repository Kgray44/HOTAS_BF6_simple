#include "direct_input_axis.h"

#include <QString>

#include <algorithm>
#include <iterator>

namespace hotas {

int physicalAxisIndexForDirectInputOffset(DWORD offset)
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

LONG directInputAxisValue(const DIJOYSTATE2 &state, PhysicalAxis axis)
{
    switch (axis) {
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

LONG directInputAxisValueAtOffset(const DIJOYSTATE2 &state, DWORD offset)
{
    const int index = physicalAxisIndexForDirectInputOffset(offset);
    return index < 0 ? 0 : directInputAxisValue(state, static_cast<PhysicalAxis>(index));
}

float normalizeDirectInputAxisValue(LONG value, const NativeAxisDescriptor &descriptor)
{
    const LONG minimum = descriptor.nativeMinimum;
    const LONG maximum = descriptor.nativeMaximum;
    if (maximum <= minimum) {
        return std::clamp(static_cast<float>(value) / 10000.0F, -1.0F, 1.0F);
    }
    const float position = std::clamp((static_cast<float>(value) - static_cast<float>(minimum))
        / (static_cast<float>(maximum) - static_cast<float>(minimum)), 0.0F, 1.0F);
    // Do not infer one-sided behavior from the control name. Mapping-level
    // range policy decides how a valid native span is interpreted; this is a
    // neutral normalized representation of the actual DirectInput range.
    return position * 2.0F - 1.0F;
}

namespace {

QString guidString(const GUID &guid)
{
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return QString::fromWCharArray(text);
}

} // namespace

NativeAxisDescriptor describeDirectInputAxisObject(LPDIRECTINPUTDEVICE8W device,
                                                   const DIDEVICEOBJECTINSTANCEW &instance)
{
    NativeAxisDescriptor descriptor;
    const int index = physicalAxisIndexForDirectInputOffset(instance.dwOfs);
    if (index < 0) return descriptor;

    descriptor.present = true;
    descriptor.nativeName = QString::fromWCharArray(instance.tszName).trimmed();
    descriptor.directInputGuid = guidString(instance.guidType);
    descriptor.directInputType = instance.dwType;
    descriptor.directInputOffset = instance.dwOfs;
    descriptor.directInputInstance = DIDFT_GETINSTANCE(instance.dwType);
    descriptor.relative = (instance.dwType & DIDFT_RELAXIS) != 0;
    if (!device) return descriptor;

    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    const HRESULT read = device->GetProperty(DIPROP_RANGE, &range.diph);
    descriptor.rangeReadResult = static_cast<qint32>(read);
    if (SUCCEEDED(read)) {
        descriptor.nativeMinimum = range.lMin;
        descriptor.nativeMaximum = range.lMax;
    }
    return descriptor;
}

void configureDirectInputAxisRange(LPDIRECTINPUTDEVICE8W device,
                                   const DIDEVICEOBJECTINSTANCEW &instance,
                                   NativeAxisDescriptor *descriptor)
{
    if (!device || physicalAxisIndexForDirectInputOffset(instance.dwOfs) < 0) return;
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    range.lMin = -10000;
    range.lMax = 10000;
    const HRESULT setResult = device->SetProperty(DIPROP_RANGE, &range.diph);
    if (descriptor) {
        descriptor->requestedMinimum = range.lMin;
        descriptor->requestedMaximum = range.lMax;
        descriptor->rangeSetAttempted = true;
        descriptor->rangeSetResult = static_cast<qint32>(setResult);
        // Read after the request even if SetProperty says success. A number
        // of drivers accept a range request but retain a native data range.
        DIPROPRANGE actual{};
        actual.diph.dwSize = sizeof(actual);
        actual.diph.dwHeaderSize = sizeof(actual.diph);
        actual.diph.dwHow = DIPH_BYID;
        actual.diph.dwObj = instance.dwType;
        const HRESULT readResult = device->GetProperty(DIPROP_RANGE, &actual.diph);
        descriptor->rangeReadResult = static_cast<qint32>(readResult);
        if (SUCCEEDED(readResult)) {
            descriptor->nativeMinimum = actual.lMin;
            descriptor->nativeMaximum = actual.lMax;
        }
        descriptor->acquisitionSourceResolved = SUCCEEDED(readResult)
            && physicalAxisIndexForDirectInputOffset(instance.dwOfs) >= 0;
    }
}

HRESULT configureDirectInputBufferedEvents(LPDIRECTINPUTDEVICE8W device, DWORD capacity)
{
    if (!device || capacity == 0) return E_INVALIDARG;
    DIPROPDWORD property{};
    property.diph.dwSize = sizeof(property);
    property.diph.dwHeaderSize = sizeof(property.diph);
    property.diph.dwHow = DIPH_DEVICE;
    property.diph.dwObj = 0;
    property.dwData = capacity;
    return device->SetProperty(DIPROP_BUFFERSIZE, &property.diph);
}

} // namespace hotas
