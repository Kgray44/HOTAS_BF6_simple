#include "hid_device_identity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>

#include <string>

namespace hotas {

QString hidDeviceContainerId(const QString &deviceInstanceId)
{
    const QString normalized = deviceInstanceId.trimmed();
    if (normalized.isEmpty()) return {};

    DEVINST deviceNode = 0;
    const std::wstring instance = normalized.toStdWString();
    if (CM_Locate_DevNodeW(&deviceNode, const_cast<wchar_t *>(instance.c_str()),
                           CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return {};
    }

    GUID container{};
    ULONG propertyType = 0;
    ULONG propertySize = sizeof(container);
    if (CM_Get_DevNode_PropertyW(deviceNode, &DEVPKEY_Device_ContainerId, &propertyType,
                                 reinterpret_cast<PBYTE>(&container), &propertySize, 0) != CR_SUCCESS
        || propertyType != DEVPROP_TYPE_GUID || propertySize != sizeof(container)) {
        return {};
    }

    return QStringLiteral("%1-%2-%3-%4%5-%6%7%8%9%10%11")
        .arg(container.Data1, 8, 16, QLatin1Char('0'))
        .arg(container.Data2, 4, 16, QLatin1Char('0'))
        .arg(container.Data3, 4, 16, QLatin1Char('0'))
        .arg(container.Data4[0], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[1], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[2], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[3], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[4], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[5], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[6], 2, 16, QLatin1Char('0'))
        .arg(container.Data4[7], 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace hotas
