#pragma once

#include <QString>

namespace hotas {

// A DirectInput HID instance path changes when Windows re-enumerates a USB
// device. The PnP container is the durable identity shared by the HID
// collections belonging to the same physical controller.
QString hidDeviceContainerId(const QString &deviceInstanceId);

} // namespace hotas
