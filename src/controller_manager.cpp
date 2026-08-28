#include "controller_manager.h"

#include <QDateTime>
#include <QUuid>

#include <algorithm>

namespace hotas {
namespace {

bool sameText(const QString &left, const QString &right)
{
    return !left.isEmpty() && !right.isEmpty()
        && left.compare(right, Qt::CaseInsensitive) == 0;
}

bool sameCapabilities(const DiscoveredController &controller, const SavedControllerRecord &record)
{
    return controller.axisCount == record.axisCount
        && controller.buttonCount == record.buttonCount
        && controller.povCount == record.povCount
        && controller.axes == record.axes;
}

ControllerMatchStrength strengthFor(const DiscoveredController &controller,
                                    const SavedControllerRecord &record)
{
    if (sameText(controller.hidInstanceId, record.hidInstanceId)) {
        return ControllerMatchStrength::HardwareInstance;
    }
    if (sameText(controller.directInputId, record.lastDirectInputId)) {
        return ControllerMatchStrength::DirectInputInstance;
    }
    const bool sameProduct = sameText(controller.productGuid, record.productGuid)
        || (controller.vendorId > 0 && controller.productId > 0
            && controller.vendorId == record.vendorId && controller.productId == record.productId);
    if (!sameProduct) return ControllerMatchStrength::None;
    if (sameCapabilities(controller, record)) return ControllerMatchStrength::Product;
    return ControllerMatchStrength::None;
}

} // namespace

QString ControllerManager::capabilityFingerprint(const DiscoveredController &controller)
{
    QString axes;
    axes.reserve(kPhysicalAxisCount);
    for (const bool available : controller.axes) axes.append(available ? u'1' : u'0');
    return QStringLiteral("a%1:%2:b%3:p%4")
        .arg(controller.axisCount).arg(axes).arg(controller.buttonCount).arg(controller.povCount);
}

ControllerMatch ControllerManager::match(const DiscoveredController &controller,
                                         const std::vector<SavedControllerRecord> &records)
{
    ControllerMatch result;
    for (const SavedControllerRecord &record : records) {
        const ControllerMatchStrength strength = strengthFor(controller, record);
        if (strength == ControllerMatchStrength::None) continue;
        if (static_cast<int>(strength) > static_cast<int>(result.strength)) {
            result = {record.id, strength, false};
        } else if (strength == result.strength && record.id != result.recordId) {
            // A weaker identity match may be useful for recognition, never
            // for silently picking between two indistinguishable controllers.
            result.ambiguous = true;
        }
    }
    if (result.ambiguous) result.recordId.clear();
    return result;
}

QString ControllerManager::autoSelect(const QList<DiscoveredController> &controllers,
                                      const std::vector<SavedControllerRecord> &records,
                                      const QString &activeRecordId)
{
    QList<const DiscoveredController *> physical;
    for (const DiscoveredController &controller : controllers) {
        if (controller.connected && !controller.virtualDevice) physical.append(&controller);
    }
    if (physical.isEmpty()) return {};

    // Keep the active saved controller whenever it is uniquely present.
    for (const DiscoveredController *controller : physical) {
        const ControllerMatch candidate = match(*controller, records);
        if (!candidate.ambiguous && candidate.recordId == activeRecordId) return controller->directInputId;
    }

    QList<const DiscoveredController *> verified;
    for (const DiscoveredController *controller : physical) {
        const ControllerMatch candidate = match(*controller, records);
        if (candidate.ambiguous) return {};
        if (!candidate.recordId.isEmpty() && !candidate.ambiguous) verified.append(controller);
    }
    if (verified.size() == 1) return verified.front()->directInputId;
    if (physical.size() == 1) return physical.front()->directInputId;
    return {};
}

bool ControllerManager::isVjoySufficient(const ControllerVJoyRequirements &available,
                                          const ControllerVJoyRequirements &required)
{
    for (int index = 0; index < kVirtualAxisSlotCount; ++index) {
        if (required.axes[static_cast<size_t>(index)] && !available.axes[static_cast<size_t>(index)]) {
            return false;
        }
    }
    return available.buttons >= required.buttons
        && available.continuousPovs >= required.continuousPovs
        && available.discretePovs >= required.discretePovs;
}

SavedControllerRecord ControllerManager::verifiedRecord(
    const DiscoveredController &controller,
    const std::array<Calibration, kPhysicalAxisCount> &calibration,
    const ControllerVJoyRequirements &requirements,
    const QString &existingId)
{
    SavedControllerRecord record;
    record.id = existingId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingId;
    record.displayName = controller.name;
    record.lastDirectInputId = controller.directInputId;
    record.productGuid = controller.productGuid;
    record.hidInstanceId = controller.hidInstanceId;
    record.vendorId = controller.vendorId;
    record.productId = controller.productId;
    record.axes = controller.axes;
    record.axisCount = controller.axisCount;
    record.buttonCount = controller.buttonCount;
    record.povCount = controller.povCount;
    record.capabilityFingerprint = capabilityFingerprint(controller);
    record.lastSeen = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    record.lastVerified = record.lastSeen;
    record.calibration = calibration;
    record.vjoyRequirements = requirements;
    return record;
}

} // namespace hotas
