#include "direct_input_axis.h"

#include <QString>

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
    descriptor.relative = (instance.dwType & DIDFT_RELAXIS) != 0;
    if (!device) return descriptor;

    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    if (SUCCEEDED(device->GetProperty(DIPROP_RANGE, &range.diph))) {
        descriptor.nativeMinimum = range.lMin;
        descriptor.nativeMaximum = range.lMax;
    }
    return descriptor;
}

void configureDirectInputAxisRange(LPDIRECTINPUTDEVICE8W device,
                                   const DIDEVICEOBJECTINSTANCEW &instance)
{
    if (!device || physicalAxisIndexForDirectInputOffset(instance.dwOfs) < 0) return;
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(range.diph);
    range.diph.dwHow = DIPH_BYID;
    range.diph.dwObj = instance.dwType;
    range.lMin = -10000;
    range.lMax = 10000;
    device->SetProperty(DIPROP_RANGE, &range.diph);
}

} // namespace hotas
