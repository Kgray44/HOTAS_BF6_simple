#pragma once

#include "mapping_types.h"

#include <QList>

namespace hotas {

// Enumerates DirectInput game controllers at a bounded control-plane cadence.
// It never acquires a device and is deliberately independent of MappingWorker.
class ControllerDiscovery final {
public:
    static QList<DiscoveredController> enumerate();
};

} // namespace hotas
