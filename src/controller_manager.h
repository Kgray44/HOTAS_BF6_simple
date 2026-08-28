#pragma once

#include "mapping_types.h"

#include <QList>

namespace hotas {

enum class ControllerMatchStrength {
    None,
    Capability,
    Product,
    DirectInputInstance,
    HardwareInstance,
};

struct ControllerMatch {
    QString recordId;
    ControllerMatchStrength strength = ControllerMatchStrength::None;
    bool ambiguous = false;
};

// Pure device-registry policy.  DirectInput enumeration and the mapping
// thread remain intentionally outside this component so the policy can be
// tested without a physical device or driver.
class ControllerManager final {
public:
    static QString capabilityFingerprint(const DiscoveredController &controller);
    static ControllerMatch match(const DiscoveredController &controller,
                                 const std::vector<SavedControllerRecord> &records);
    static QString autoSelect(const QList<DiscoveredController> &controllers,
                              const std::vector<SavedControllerRecord> &records,
                              const QString &activeRecordId);
    static bool isVjoySufficient(const ControllerVJoyRequirements &available,
                                 const ControllerVJoyRequirements &required);
    static SavedControllerRecord verifiedRecord(const DiscoveredController &controller,
                                                const std::array<Calibration, kPhysicalAxisCount> &calibration,
                                                const ControllerVJoyRequirements &requirements,
                                                const QString &existingId = {});
};

} // namespace hotas
