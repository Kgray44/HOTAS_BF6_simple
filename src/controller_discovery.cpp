#include "controller_discovery.h"

#include <dinput.h>
#include <windows.h>

#include <QString>

#include <algorithm>

namespace hotas {
namespace {

QString guidString(const GUID &guid)
{
    return QStringLiteral("{%1-%2-%3-%4%5-%6%7%8%9%10%11}")
        .arg(guid.Data1, 8, 16, QLatin1Char('0'))
        .arg(guid.Data2, 4, 16, QLatin1Char('0'))
        .arg(guid.Data3, 4, 16, QLatin1Char('0'))
        .arg(guid.Data4[0], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[1], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[2], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[3], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[4], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[5], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[6], 2, 16, QLatin1Char('0'))
        .arg(guid.Data4[7], 2, 16, QLatin1Char('0')).toUpper();
}

QString hidInstanceId(LPDIRECTINPUTDEVICE8W device)
{
    if (!device) return {};
    DIPROPGUIDANDPATH property{};
    property.diph.dwSize = sizeof(property);
    property.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    property.diph.dwHow = DIPH_DEVICE;
    if (FAILED(device->GetProperty(DIPROP_GUIDANDPATH, &property.diph))) return {};
    QString path = QString::fromWCharArray(property.wszPath);
    path.remove(QStringLiteral("\\\\?\\"), Qt::CaseInsensitive);
    const int separator = path.indexOf(QStringLiteral("#{"));
    if (separator >= 0) path.truncate(separator);
    return path.replace(u'#', u'\\').toUpper();
}

struct ObjectContext {
    DiscoveredController *controller = nullptr;
};

BOOL CALLBACK objectCallback(const DIDEVICEOBJECTINSTANCEW *instance, VOID *context)
{
    auto *objects = static_cast<ObjectContext *>(context);
    const DWORD type = DIDFT_GETTYPE(instance->dwType);
    if ((type & DIDFT_AXIS) != 0) {
        ++objects->controller->axisCount;
        const DWORD offset = instance->dwOfs;
        const int index = offset == DIJOFS_X ? 0 : offset == DIJOFS_Y ? 1 : offset == DIJOFS_Z ? 2
            : offset == DIJOFS_RX ? 3 : offset == DIJOFS_RY ? 4 : offset == DIJOFS_RZ ? 5
            : offset == DIJOFS_SLIDER(0) ? 6 : offset == DIJOFS_SLIDER(1) ? 7 : -1;
        if (index >= 0) objects->controller->axes[static_cast<size_t>(index)] = true;
    } else if ((type & DIDFT_BUTTON) != 0) {
        ++objects->controller->buttonCount;
    } else if ((type & DIDFT_POV) != 0) {
        ++objects->controller->povCount;
    }
    return DIENUM_CONTINUE;
}

struct EnumerationContext {
    LPDIRECTINPUT8W directInput = nullptr;
    QList<DiscoveredController> *controllers = nullptr;
};

BOOL CALLBACK deviceCallback(const DIDEVICEINSTANCEW *instance, VOID *context)
{
    auto *enumeration = static_cast<EnumerationContext *>(context);
    DiscoveredController controller;
    controller.name = QString::fromWCharArray(instance->tszProductName);
    controller.directInputId = guidString(instance->guidInstance);
    controller.productGuid = guidString(instance->guidProduct);
    controller.vendorId = static_cast<int>(LOWORD(instance->guidProduct.Data1));
    controller.productId = static_cast<int>(HIWORD(instance->guidProduct.Data1));
    controller.connected = true;
    controller.virtualDevice = isVirtualControllerName(controller.name);

    LPDIRECTINPUTDEVICE8W device = nullptr;
    if (SUCCEEDED(enumeration->directInput->CreateDevice(instance->guidInstance, &device, nullptr))) {
        device->SetDataFormat(&c_dfDIJoystick2);
        ObjectContext objects{&controller};
        device->EnumObjects(objectCallback, &objects, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
        controller.buttonCount = std::min(controller.buttonCount, kMaximumPhysicalButtons);
        controller.povCount = std::min(controller.povCount, kMaximumPhysicalPovs);
        controller.hidInstanceId = hidInstanceId(device);
        device->Release();
    }
    enumeration->controllers->append(std::move(controller));
    return DIENUM_CONTINUE;
}

} // namespace

QList<DiscoveredController> ControllerDiscovery::enumerate()
{
    QList<DiscoveredController> controllers;
    LPDIRECTINPUT8W directInput = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                  IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr))) {
        return controllers;
    }
    EnumerationContext context{directInput, &controllers};
    directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, deviceCallback, &context, DIEDFL_ATTACHEDONLY);
    directInput->Release();
    return controllers;
}

} // namespace hotas
