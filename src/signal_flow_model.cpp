#include "signal_flow_model.h"

#include <QCryptographicHash>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {

constexpr auto kLegacySourceScope = "legacy-source";

struct DesiredSignalFlowIdentities {
    QSet<QString> routes;
    QSet<QString> processors;
};

QString sourceScope(const ControllerProfile &profile, const QString &controllerRecordId)
{
    return profile.id + u":"_qs
        + (controllerRecordId.trimmed().isEmpty() ? QLatin1String(kLegacySourceScope)
                                                   : controllerRecordId.trimmed());
}

QString routeKey(const ControllerProfile &profile, const QString &controllerRecordId,
                 const QString &kind, int index, int subIndex = -1)
{
    QString key = u"route:"_qs + kind + u":"_qs + sourceScope(profile, controllerRecordId)
        + u":"_qs + QString::number(index);
    if (subIndex >= 0) key += u":"_qs + QString::number(subIndex);
    return key;
}

QString processorKey(const ControllerProfile &profile, const QString &controllerRecordId,
                     int axis, const QString &kind)
{
    return u"processor:axis:"_qs + sourceScope(profile, controllerRecordId)
        + u":"_qs + QString::number(axis) + u":"_qs + kind;
}

bool hasNonDefaultCurve(const AxisMapping &mapping)
{
    return mapping.curve.family != CurveFamily::Linear || mapping.curve.pointEditing
        || std::abs(mapping.curve.strength) > 0.0001F;
}

bool hasNonDefaultLimits(const AxisMapping &mapping)
{
    const float defaultMinimum = mapping.rangeMode == AxisRangeMode::OneSided ? 0.0F : -1.0F;
    return std::abs(mapping.outputMinimum - defaultMinimum) > 0.0001F
        || std::abs(mapping.outputMaximum - 1.0F) > 0.0001F;
}

bool adaptiveResponseEnabled(const MapperConfiguration &configuration,
                             const ControllerProfile &profile,
                             const DeviceProfileMapping *mapping, int axis)
{
    const size_t index = static_cast<size_t>(axis);
    return configuration.adaptiveResponseGlobal.axes[index].settings.enabled
        || profile.adaptiveResponse.axes[index].settings.enabled
        || (mapping && mapping->adaptiveResponse.axes[index].settings.enabled);
}

void collectMappingIdentities(const MapperConfiguration &configuration,
                              const ControllerProfile &profile,
                              const DeviceProfileMapping *mapping,
                              const QString &controllerRecordId,
                              DesiredSignalFlowIdentities *desired)
{
    if (!desired) return;
    const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
    const ButtonBindings &buttons = mapping ? mapping->buttons : profile.buttons;
    const PovBindings &povs = mapping ? mapping->povs : profile.povs;
    const NativePovBindings &nativePovs = mapping ? mapping->nativePovBindings : NativePovBindings{};

    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const AxisMapping &axisMapping = axes[static_cast<size_t>(axis)];
        if (axisMapping.target == VirtualAxis::Disabled) continue;
        desired->routes.insert(routeKey(profile, controllerRecordId, u"axis"_qs, axis));
        if (axisMapping.rangeMode == AxisRangeMode::OneSided) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"domain"_qs));
        }
        if (axisMapping.deadzone > 0.0001F) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"deadzone"_qs));
        }
        if (axisMapping.hysteresis > 0.0001F) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"center-hold"_qs));
        }
        if (axisMapping.inverted) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"invert"_qs));
        }
        if (hasNonDefaultCurve(axisMapping)) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"curve"_qs));
        }
        if (hasNonDefaultLimits(axisMapping)) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"limits"_qs));
        }
        if (adaptiveResponseEnabled(configuration, profile, mapping, axis)) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"adaptive-response"_qs));
        }
    }

    for (int button = 0; button < static_cast<int>(buttons.size()); ++button) {
        const ButtonBinding &binding = buttons[static_cast<size_t>(button)];
        if (binding.type == ButtonActionType::VirtualButton && binding.target > 0) {
            desired->routes.insert(routeKey(profile, controllerRecordId, u"button"_qs, button));
        }
    }
    for (int hat = 0; hat < static_cast<int>(povs.size()); ++hat) {
        const PovDirectionBindings &directions = povs[static_cast<size_t>(hat)];
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding &binding = directions[static_cast<size_t>(direction)];
            if (binding.type == ButtonActionType::VirtualButton && binding.target > 0) {
                desired->routes.insert(routeKey(profile, controllerRecordId, u"pov"_qs, hat, direction));
            }
        }
    }
    for (int hat = 0; hat < static_cast<int>(nativePovs.size()); ++hat) {
        if (nativePovs[static_cast<size_t>(hat)].enabled) {
            desired->routes.insert(routeKey(profile, controllerRecordId, u"native-pov"_qs, hat));
        }
    }
}

void collectDesiredIdentities(const MapperConfiguration &configuration,
                              DesiredSignalFlowIdentities *desired)
{
    if (!desired) return;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (profile.deviceMappings.empty()) {
            collectMappingIdentities(configuration, profile, nullptr, {}, desired);
            continue;
        }
        for (const DeviceProfileMapping &mapping : profile.deviceMappings) {
            if (!mapping.enabled) continue;
            collectMappingIdentities(configuration, profile, &mapping, mapping.controllerRecordId, desired);
        }
    }
    for (const AutomationDefinition &automation : configuration.automations) {
        if (!automation.id.trimmed().isEmpty()) {
            desired->processors.insert(u"processor:automation:"_qs + automation.id.trimmed());
        }
    }
}

QString allocateIdentity(const QString &key, std::uint32_t generation, QSet<QString> *used)
{
    for (int collision = 0; collision < 10000; ++collision) {
        const QString candidateKey = collision == 0 ? key
            : key + u":collision:"_qs + QString::number(collision);
        const QString candidate = signalFlowDeterministicId(candidateKey, generation);
        if (!used || !used->contains(candidate)) {
            if (used) used->insert(candidate);
            return candidate;
        }
    }
    // This is astronomically unlikely for the hash-based identity. Leaving an
    // empty value would be materially worse than a deterministic final guard.
    const QString candidate = signalFlowDeterministicId(key + u":collision:overflow"_qs, generation);
    if (used) used->insert(candidate);
    return candidate;
}

void reconcileIdentities(std::vector<SignalFlowIdentityRecord> *identities,
                         const QSet<QString> &desired)
{
    if (!identities) return;
    std::sort(identities->begin(), identities->end(), [](const auto &left, const auto &right) {
        return left.key < right.key;
    });

    QSet<QString> seenKeys;
    QSet<QString> usedIds;
    std::vector<SignalFlowIdentityRecord> repaired;
    repaired.reserve(identities->size() + static_cast<size_t>(desired.size()));
    for (SignalFlowIdentityRecord identity : *identities) {
        identity.key = identity.key.trimmed().left(320);
        if (identity.key.isEmpty() || seenKeys.contains(identity.key)) continue;
        identity.generation = std::max<std::uint32_t>(1, identity.generation);
        seenKeys.insert(identity.key);
        repaired.push_back(std::move(identity));
    }
    std::sort(repaired.begin(), repaired.end(), [](const auto &left, const auto &right) {
        return left.key < right.key;
    });

    for (SignalFlowIdentityRecord &identity : repaired) {
        const bool nowActive = desired.contains(identity.key);
        const bool reactivated = nowActive && !identity.active;
        if (reactivated) ++identity.generation;
        identity.active = nowActive;
        if (identity.id.trimmed().isEmpty() || usedIds.contains(identity.id) || reactivated) {
            identity.id = allocateIdentity(identity.key, identity.generation, &usedIds);
        } else {
            identity.id = identity.id.trimmed().left(96);
            usedIds.insert(identity.id);
        }
    }

    QStringList missing = desired.values();
    std::sort(missing.begin(), missing.end());
    for (const QString &key : missing) {
        if (seenKeys.contains(key)) continue;
        SignalFlowIdentityRecord identity;
        identity.key = key;
        identity.generation = 1;
        identity.active = true;
        identity.id = allocateIdentity(identity.key, identity.generation, &usedIds);
        repaired.push_back(std::move(identity));
    }
    std::sort(repaired.begin(), repaired.end(), [](const auto &left, const auto &right) {
        return left.key < right.key;
    });
    *identities = std::move(repaired);
}

} // namespace

QString signalFlowDeterministicId(const QString &key, std::uint32_t generation)
{
    const QByteArray seed = (u"hotas-bf6/signal-flow/v1/"_qs + key.trimmed()
                             + u"/generation/"_qs + QString::number(generation)).toUtf8();
    QByteArray bytes = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).left(16);
    // Mark the stable SHA-256 truncation as an RFC 4122 variant/version-5 UUID
    // for familiar diagnostics and deep-link ergonomics without randomness.
    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes[6]) & 0x0F) | 0x50);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes[8]) & 0x3F) | 0x80);
    return QUuid::fromRfc4122(bytes).toString(QUuid::WithoutBraces);
}

QString signalFlowRouteIdentityKey(const ControllerProfile &profile,
                                   const QString &controllerRecordId,
                                   const QString &kind, int index, int subIndex)
{
    return routeKey(profile, controllerRecordId, kind.trimmed(), index, subIndex);
}

QString signalFlowAxisProcessorIdentityKey(const ControllerProfile &profile,
                                           const QString &controllerRecordId,
                                           int axis, const QString &kind)
{
    return processorKey(profile, controllerRecordId, axis, kind.trimmed());
}

const SignalFlowIdentityRecord *findSignalFlowIdentity(
    const std::vector<SignalFlowIdentityRecord> &identities, const QString &key)
{
    const QString normalized = key.trimmed();
    const auto found = std::find_if(identities.cbegin(), identities.cend(),
        [&normalized](const SignalFlowIdentityRecord &identity) { return identity.key == normalized; });
    return found == identities.cend() ? nullptr : &*found;
}

void reconcileSignalFlowState(MapperConfiguration *configuration)
{
    if (!configuration) return;
    DesiredSignalFlowIdentities desired;
    collectDesiredIdentities(*configuration, &desired);
    reconcileIdentities(&configuration->signalFlow.routeIdentities, desired.routes);
    reconcileIdentities(&configuration->signalFlow.processorIdentities, desired.processors);
}

} // namespace hotas
