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

// The normal mapper profile has a small, intentional baseline deadzone and
// release band.  They remain endpoint semantics for a plain direct route;
// surfacing them as two processor cards on every fresh mapping would make the
// graph claim that a direct route is conditioned when the user has not opted
// into a distinct transform.  A changed value is still a real, inspectable
// processor and uses the same focused-editor field as before.
bool hasNonDefaultDeadzone(const AxisMapping &mapping)
{
    return std::abs(mapping.deadzone - 0.03F) > 0.0001F;
}

bool hasNonDefaultCenterHold(const AxisMapping &mapping)
{
    return std::abs(mapping.hysteresis - 0.002F) > 0.0001F;
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
    const NativePovBindings &nativePovs = mapping ? mapping->nativePovBindings
                                                   : configuration.nativePovBindings;

    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const AxisMapping &axisMapping = axes[static_cast<size_t>(axis)];
        if (axisMapping.target == VirtualAxis::Disabled) continue;
        desired->routes.insert(routeKey(profile, controllerRecordId, u"axis"_qs, axis));
        if (axisMapping.rangeMode == AxisRangeMode::OneSided) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"domain"_qs));
        }
        if (hasNonDefaultDeadzone(axisMapping)) {
            desired->processors.insert(processorKey(profile, controllerRecordId, axis, u"deadzone"_qs));
        }
        if (hasNonDefaultCenterHold(axisMapping)) {
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

constexpr int kSignalFlowCanonicalTopologyVersion = 1;

QString portKindKey(SignalFlowPortKind kind)
{
    switch (kind) {
    case SignalFlowPortKind::Axis: return u"axis"_qs;
    case SignalFlowPortKind::Button: return u"button"_qs;
    case SignalFlowPortKind::PovDirection: return u"pov"_qs;
    case SignalFlowPortKind::NativePov: return u"native-pov"_qs;
    }
    return u"unknown"_qs;
}

bool validMixerMode(SignalFlowMixerMode mode)
{
    return mode == SignalFlowMixerMode::Disabled || mode == SignalFlowMixerMode::Average
        || mode == SignalFlowMixerMode::SumClamped
        || mode == SignalFlowMixerMode::HighestMagnitude;
}

QString routeScopeKey(const QString &profileId, const QString &controllerRecordId)
{
    return profileId.trimmed() + u":"_qs
        + (controllerRecordId.trimmed().isEmpty() ? QLatin1String(kLegacySourceScope)
                                                   : controllerRecordId.trimmed());
}

QString routeSourceSlotKey(const SignalFlowRoute &route)
{
    return routeScopeKey(route.profileId, route.controllerRecordId) + u":"_qs
        + portKindKey(route.sourceKind) + u":"_qs + QString::number(route.sourceIndex)
        + u":"_qs + QString::number(route.sourceSubIndex);
}

QString routeEndpointKey(const SignalFlowRoute &route)
{
    return routeSourceSlotKey(route) + u":"_qs + portKindKey(route.destinationKind)
        + u":"_qs + QString::number(route.destinationIndex)
        + u":"_qs + QString::number(route.destinationSubIndex);
}

QString fanoutRouteKey(const ControllerProfile &profile, const QString &controllerRecordId,
                       const QString &kind, int sourceIndex, int sourceSubIndex,
                       SignalFlowPortKind destinationKind, int destinationIndex,
                       int destinationSubIndex)
{
    return routeKey(profile, controllerRecordId, kind, sourceIndex, sourceSubIndex)
        + u":fanout:"_qs + portKindKey(destinationKind) + u":"_qs
        + QString::number(destinationIndex) + u":"_qs + QString::number(destinationSubIndex);
}

QString mixerKey(const ControllerProfile &profile, const QString &controllerRecordId,
                 int destinationAxis)
{
    return u"processor:mixer:"_qs + sourceScope(profile, controllerRecordId)
        + u":axis:"_qs + QString::number(destinationAxis);
}

QString sharedProcessorKey(const ControllerProfile &profile, const QString &controllerRecordId,
                           const QString &kind, int ownerAxis)
{
    return u"processor:shared:"_qs + sourceScope(profile, controllerRecordId)
        + u":axis:"_qs + QString::number(ownerAxis) + u":"_qs + kind;
}

bool supportedSharedProcessorKind(const QString &kind)
{
    return kind == u"curve"_qs || kind == u"deadzone"_qs || kind == u"center-hold"_qs
        || kind == u"invert"_qs || kind == u"limits"_qs
        || kind == u"adaptive-response"_qs;
}

bool routeEndpointsAreValid(const SignalFlowRoute &route)
{
    if (route.profileId.trimmed().isEmpty()) return false;
    if (route.sourceKind == SignalFlowPortKind::Axis
        && route.destinationKind == SignalFlowPortKind::Axis) {
        return route.sourceIndex >= 0 && route.sourceIndex < kPhysicalAxisCount
            && route.sourceSubIndex == -1 && route.destinationIndex > 0
            && route.destinationIndex < kVirtualAxisSlotCount && route.destinationSubIndex == -1;
    }
    if (route.sourceKind == SignalFlowPortKind::Button
        && route.destinationKind == SignalFlowPortKind::Button) {
        return route.sourceIndex >= 0 && route.sourceIndex < kMaximumPhysicalButtons
            && route.sourceSubIndex == -1 && route.destinationIndex > 0
            && route.destinationIndex <= kMaximumVirtualButtons && route.destinationSubIndex == -1;
    }
    if (route.sourceKind == SignalFlowPortKind::PovDirection
        && route.destinationKind == SignalFlowPortKind::Button) {
        return route.sourceIndex >= 0 && route.sourceIndex < kMaximumPhysicalPovs
            && route.sourceSubIndex >= 0 && route.sourceSubIndex < kPovDirectionCount
            && route.destinationIndex > 0 && route.destinationIndex <= kMaximumVirtualButtons
            && route.destinationSubIndex == -1;
    }
    if (route.sourceKind == SignalFlowPortKind::NativePov
        && route.destinationKind == SignalFlowPortKind::NativePov) {
        return route.sourceIndex >= 0 && route.sourceIndex < kMaximumPhysicalPovs
            && route.sourceSubIndex == -1 && route.destinationIndex > 0
            && (route.destinationSubIndex == static_cast<int>(NativePovTargetType::Continuous)
                || route.destinationSubIndex == static_cast<int>(NativePovTargetType::Discrete));
    }
    return false;
}

bool routeScopeExists(const MapperConfiguration &configuration, const SignalFlowRoute &route)
{
    const ControllerProfile *profile = findProfile(configuration, route.profileId);
    if (!profile) return false;
    const QString controllerId = route.controllerRecordId.trimmed();
    if (controllerId.isEmpty()) return profile->deviceMappings.empty();
    return findDeviceProfileMapping(*profile, controllerId) != nullptr;
}

bool scopeAxisIsRouted(const SignalFlowState &state, const SignalFlowSharedProcessor &processor,
                       int axis)
{
    return std::any_of(state.routes.cbegin(), state.routes.cend(),
        [&processor, axis](const SignalFlowRoute &route) {
            return route.enabled && route.profileId == processor.profileId
                && route.controllerRecordId == processor.controllerRecordId
                && route.sourceKind == SignalFlowPortKind::Axis && route.sourceIndex == axis;
        });
}

bool sharedProcessorUsesAxis(const SignalFlowSharedProcessor &processor, const QString &profileId,
                             const QString &controllerRecordId, const QString &kind, int axis)
{
    return processor.enabled && processor.profileId == profileId
        && processor.controllerRecordId == controllerRecordId && processor.kind == kind
        && std::find(processor.sourceAxes.cbegin(), processor.sourceAxes.cend(), axis)
            != processor.sourceAxes.cend();
}

const SignalFlowSharedProcessor *sharedProcessorForAxis(const SignalFlowState &state,
                                                        const QString &profileId,
                                                        const QString &controllerRecordId,
                                                        const QString &kind, int axis)
{
    const auto found = std::find_if(state.sharedProcessors.cbegin(), state.sharedProcessors.cend(),
        [&profileId, &controllerRecordId, &kind, axis](const SignalFlowSharedProcessor &processor) {
            return sharedProcessorUsesAxis(processor, profileId, controllerRecordId, kind, axis);
        });
    return found == state.sharedProcessors.cend() ? nullptr : &*found;
}

AxisMappings *axisMappingsForScope(MapperConfiguration *configuration, const QString &profileId,
                                   const QString &controllerRecordId,
                                   AdaptiveResponseLayer **adaptiveLayer = nullptr)
{
    if (adaptiveLayer) *adaptiveLayer = nullptr;
    if (!configuration) return nullptr;
    ControllerProfile *profile = findProfile(*configuration, profileId);
    if (!profile) return nullptr;
    if (controllerRecordId.isEmpty()) {
        if (adaptiveLayer) *adaptiveLayer = &profile->adaptiveResponse;
        return &profile->axes;
    }
    DeviceProfileMapping *mapping = findDeviceProfileMapping(*profile, controllerRecordId);
    if (!mapping) return nullptr;
    if (adaptiveLayer) *adaptiveLayer = &mapping->adaptiveResponse;
    return &mapping->axes;
}

void mirrorSharedProcessorSetting(AxisMapping *destination, const AxisMapping &owner,
                                  AdaptiveResponseAxisOverride *destinationAdaptive,
                                  const AdaptiveResponseAxisOverride &ownerAdaptive,
                                  const QString &kind)
{
    if (!destination) return;
    if (kind == u"curve"_qs) {
        destination->curve = owner.curve;
    } else if (kind == u"deadzone"_qs) {
        destination->deadzone = owner.deadzone;
    } else if (kind == u"center-hold"_qs) {
        destination->hysteresis = owner.hysteresis;
    } else if (kind == u"invert"_qs) {
        destination->inverted = owner.inverted;
    } else if (kind == u"limits"_qs) {
        destination->outputMinimum = owner.outputMinimum;
        destination->outputMaximum = owner.outputMaximum;
        destination->centeredOutputMinimum = owner.centeredOutputMinimum;
        destination->centeredOutputMaximum = owner.centeredOutputMaximum;
        destination->oneSidedOutputMinimum = owner.oneSidedOutputMinimum;
        destination->oneSidedOutputMaximum = owner.oneSidedOutputMaximum;
    } else if (kind == u"adaptive-response"_qs && destinationAdaptive) {
        *destinationAdaptive = ownerAdaptive;
    }
}

bool sameCurveDefinition(const CurveDefinition &left, const CurveDefinition &right)
{
    if (left.family != right.family || left.sourceFamily != right.sourceFamily
        || left.strength != right.strength || left.presetId != right.presetId
        || left.baseLabel != right.baseLabel || left.sourcePresetId != right.sourcePresetId
        || left.pointEditing != right.pointEditing || left.symmetry != right.symmetry
        || left.interpolation != right.interpolation || left.pointDensity != right.pointDensity
        || left.points.size() != right.points.size()) {
        return false;
    }
    for (size_t index = 0; index < left.points.size(); ++index) {
        const CurvePoint &a = left.points[index];
        const CurvePoint &b = right.points[index];
        if (a.input != b.input || a.output != b.output || a.locked != b.locked) return false;
    }
    return true;
}

bool sameAdaptiveResponseSettings(const AdaptiveResponseSettings &left,
                                  const AdaptiveResponseSettings &right)
{
    return left.enabled == right.enabled && left.model == right.model
        && left.maximumHorizonMs == right.maximumHorizonMs && left.maximumLead == right.maximumLead
        && left.velocityResponse == right.velocityResponse
        && left.accelerationResponse == right.accelerationResponse
        && left.motionSensitivity == right.motionSensitivity && left.noiseRejection == right.noiseRejection
        && left.reversalDetection == right.reversalDetection && left.reversalResponse == right.reversalResponse
        && left.decelerationResponse == right.decelerationResponse
        && left.settlingResponse == right.settlingResponse && left.endpointTaper == right.endpointTaper
        && left.onsetAssist == right.onsetAssist && left.onsetCap == right.onsetCap
        && left.sustainedAssist == right.sustainedAssist && left.sustainedCap == right.sustainedCap
        && left.horizonExtension == right.horizonExtension
        && left.horizonExtensionCapMs == right.horizonExtensionCapMs
        && left.turningPointProtection == right.turningPointProtection
        && left.turningPointMargin == right.turningPointMargin
        && left.normalMovementResponse == right.normalMovementResponse
        && left.rapidMovementResponse == right.rapidMovementResponse
        && left.engagementSensitivity == right.engagementSensitivity;
}

bool sharedProcessorSettingsMatch(const AxisMapping &member, const AxisMapping &owner,
                                  const AdaptiveResponseAxisOverride *memberAdaptive,
                                  const AdaptiveResponseAxisOverride *ownerAdaptive,
                                  const QString &kind)
{
    if (kind == u"curve"_qs) return sameCurveDefinition(member.curve, owner.curve);
    if (kind == u"deadzone"_qs) return member.deadzone == owner.deadzone;
    if (kind == u"center-hold"_qs) return member.hysteresis == owner.hysteresis;
    if (kind == u"invert"_qs) return member.inverted == owner.inverted;
    if (kind == u"limits"_qs) {
        return member.outputMinimum == owner.outputMinimum && member.outputMaximum == owner.outputMaximum
            && member.centeredOutputMinimum == owner.centeredOutputMinimum
            && member.centeredOutputMaximum == owner.centeredOutputMaximum
            && member.oneSidedOutputMinimum == owner.oneSidedOutputMinimum
            && member.oneSidedOutputMaximum == owner.oneSidedOutputMaximum;
    }
    if (kind == u"adaptive-response"_qs) {
        if (!memberAdaptive || !ownerAdaptive) return memberAdaptive == ownerAdaptive;
        return memberAdaptive->properties == ownerAdaptive->properties
            && memberAdaptive->presetId == ownerAdaptive->presetId
            && sameAdaptiveResponseSettings(memberAdaptive->settings, ownerAdaptive->settings);
    }
    return false;
}

void appendCompatibilityRoute(std::vector<SignalFlowRoute> *routes, const ControllerProfile &profile,
                              const QString &controllerRecordId, SignalFlowPortKind sourceKind,
                              int sourceIndex, int sourceSubIndex,
                              SignalFlowPortKind destinationKind, int destinationIndex,
                              int destinationSubIndex, bool primary = true,
                              bool implicitDefault = false)
{
    if (!routes) return;
    SignalFlowRoute route;
    route.profileId = profile.id;
    route.controllerRecordId = controllerRecordId.trimmed();
    route.sourceKind = sourceKind;
    route.sourceIndex = sourceIndex;
    route.sourceSubIndex = sourceSubIndex;
    route.destinationKind = destinationKind;
    route.destinationIndex = destinationIndex;
    route.destinationSubIndex = destinationSubIndex;
    route.primaryProjection = primary;
    route.implicitDefault = implicitDefault;
    route.enabled = true;
    route.identityKey = routeKey(profile, controllerRecordId, portKindKey(sourceKind),
        sourceIndex, sourceSubIndex);
    routes->push_back(std::move(route));
}

void appendCompatibilityScopeRoutes(const MapperConfiguration &configuration,
                                    const ControllerProfile &profile,
                                    const DeviceProfileMapping *mapping,
                                    const QString &controllerRecordId,
                                    std::vector<SignalFlowRoute> *routes)
{
    if (!routes) return;
    const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
    const ButtonBindings &buttons = mapping ? mapping->buttons : profile.buttons;
    const PovBindings &povs = mapping ? mapping->povs : profile.povs;
    const NativePovBindings &nativePovs = mapping ? mapping->nativePovBindings
                                                   : configuration.nativePovBindings;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const int target = static_cast<int>(axes[static_cast<size_t>(axis)].target);
        if (target <= 0 || target >= kVirtualAxisSlotCount) continue;
        appendCompatibilityRoute(routes, profile, controllerRecordId, SignalFlowPortKind::Axis,
            axis, -1, SignalFlowPortKind::Axis, target, -1);
    }
    for (int button = 0; button < static_cast<int>(buttons.size()); ++button) {
        const ButtonBinding &binding = buttons[static_cast<size_t>(button)];
        if (binding.type != ButtonActionType::VirtualButton || binding.target <= 0) continue;
        appendCompatibilityRoute(routes, profile, controllerRecordId, SignalFlowPortKind::Button,
            button, -1, SignalFlowPortKind::Button, binding.target, -1, true,
            !binding.explicitlyConfigured);
    }
    for (int hat = 0; hat < static_cast<int>(povs.size()); ++hat) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding &binding = povs[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (binding.type != ButtonActionType::VirtualButton || binding.target <= 0) continue;
            appendCompatibilityRoute(routes, profile, controllerRecordId,
                SignalFlowPortKind::PovDirection, hat, direction,
                SignalFlowPortKind::Button, binding.target, -1, true,
                !binding.explicitlyConfigured);
        }
    }
    for (int hat = 0; hat < static_cast<int>(nativePovs.size()); ++hat) {
        const NativePovBinding &binding = nativePovs[static_cast<size_t>(hat)];
        if (!binding.enabled || binding.targetIndex <= 0
            || binding.targetType == NativePovTargetType::Disabled) continue;
        appendCompatibilityRoute(routes, profile, controllerRecordId, SignalFlowPortKind::NativePov,
            hat, -1, SignalFlowPortKind::NativePov, binding.targetIndex,
            static_cast<int>(binding.targetType));
    }
}

void importCompatibilityTopology(MapperConfiguration *configuration)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    state.routes.clear();
    state.mixers.clear();
    state.sharedProcessors.clear();
    for (const ControllerProfile &profile : configuration->profiles) {
        if (profile.deviceMappings.empty()) {
            appendCompatibilityScopeRoutes(*configuration, profile, nullptr, {}, &state.routes);
            continue;
        }
        for (const DeviceProfileMapping &mapping : profile.deviceMappings) {
            if (!mapping.enabled) continue;
            appendCompatibilityScopeRoutes(*configuration, profile, &mapping,
                                           mapping.controllerRecordId, &state.routes);
        }
    }
    state.topologyVersion = kSignalFlowCanonicalTopologyVersion;
}

void normalizeTopology(MapperConfiguration *configuration)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    std::vector<SignalFlowRoute> repaired;
    repaired.reserve(state.routes.size());
    for (SignalFlowRoute route : state.routes) {
        route.profileId = route.profileId.trimmed().left(96);
        route.controllerRecordId = route.controllerRecordId.trimmed().left(96);
        route.identityKey = route.identityKey.trimmed().left(320);
        route.id = route.id.trimmed().left(96);
        QStringList processors;
        for (const QString &processor : route.processorPath) {
            const QString normalized = processor.trimmed().left(96);
            if (!normalized.isEmpty() && !processors.contains(normalized)
                && processors.size() < kMaximumSignalFlowProcessorPath) {
                processors.append(normalized);
            }
        }
        route.processorPath = std::move(processors);
        if (!routeEndpointsAreValid(route) || !routeScopeExists(*configuration, route)) continue;
        if (route.identityKey.isEmpty()) {
            const ControllerProfile *profile = findProfile(*configuration, route.profileId);
            if (!profile) continue;
            route.identityKey = route.primaryProjection
                ? routeKey(*profile, route.controllerRecordId, portKindKey(route.sourceKind),
                           route.sourceIndex, route.sourceSubIndex)
                : fanoutRouteKey(*profile, route.controllerRecordId, portKindKey(route.sourceKind),
                                 route.sourceIndex, route.sourceSubIndex, route.destinationKind,
                                 route.destinationIndex, route.destinationSubIndex);
        }
        repaired.push_back(std::move(route));
    }
    std::sort(repaired.begin(), repaired.end(), [](const SignalFlowRoute &left,
                                                    const SignalFlowRoute &right) {
        const QString leftSource = routeSourceSlotKey(left);
        const QString rightSource = routeSourceSlotKey(right);
        if (leftSource != rightSource) return leftSource < rightSource;
        if (left.primaryProjection != right.primaryProjection) return left.primaryProjection;
        const QString leftEndpoint = routeEndpointKey(left);
        const QString rightEndpoint = routeEndpointKey(right);
        if (leftEndpoint != rightEndpoint) return leftEndpoint < rightEndpoint;
        return left.identityKey < right.identityKey;
    });
    QSet<QString> endpoints;
    std::vector<SignalFlowRoute> unique;
    unique.reserve(repaired.size());
    for (SignalFlowRoute route : repaired) {
        const QString endpoint = routeEndpointKey(route);
        if (endpoints.contains(endpoint)) continue;
        endpoints.insert(endpoint);
        unique.push_back(std::move(route));
    }
    QSet<QString> primarySources;
    for (SignalFlowRoute &route : unique) {
        const QString source = routeSourceSlotKey(route);
        if (!primarySources.contains(source)) {
            route.primaryProjection = true;
            primarySources.insert(source);
        } else {
            route.primaryProjection = false;
        }
    }
    state.routes = std::move(unique);

    std::vector<SignalFlowMixer> repairedMixers;
    repairedMixers.reserve(state.mixers.size());
    QSet<QString> mixerDestinations;
    for (SignalFlowMixer mixer : state.mixers) {
        mixer.profileId = mixer.profileId.trimmed().left(96);
        mixer.controllerRecordId = mixer.controllerRecordId.trimmed().left(96);
        mixer.identityKey = mixer.identityKey.trimmed().left(320);
        mixer.id = mixer.id.trimmed().left(96);
        if (!findProfile(*configuration, mixer.profileId)
            || mixer.destinationAxis <= 0 || mixer.destinationAxis >= kVirtualAxisSlotCount
            || !validMixerMode(mixer.mode)) continue;
        SignalFlowRoute scope;
        scope.profileId = mixer.profileId;
        scope.controllerRecordId = mixer.controllerRecordId;
        if (!routeScopeExists(*configuration, scope)) continue;
        const ControllerProfile *profile = findProfile(*configuration, mixer.profileId);
        if (!profile) continue;
        if (mixer.identityKey.isEmpty()) {
            mixer.identityKey = mixerKey(*profile, mixer.controllerRecordId, mixer.destinationAxis);
        }
        const QString destination = routeScopeKey(mixer.profileId, mixer.controllerRecordId)
            + u":axis:"_qs + QString::number(mixer.destinationAxis);
        if (mixerDestinations.contains(destination)) continue;
        mixerDestinations.insert(destination);
        repairedMixers.push_back(std::move(mixer));
    }
    std::sort(repairedMixers.begin(), repairedMixers.end(), [](const SignalFlowMixer &left,
                                                                const SignalFlowMixer &right) {
        const QString leftKey = routeScopeKey(left.profileId, left.controllerRecordId)
            + u":"_qs + QString::number(left.destinationAxis);
        const QString rightKey = routeScopeKey(right.profileId, right.controllerRecordId)
            + u":"_qs + QString::number(right.destinationAxis);
        return leftKey == rightKey ? left.identityKey < right.identityKey : leftKey < rightKey;
    });
    state.mixers = std::move(repairedMixers);
    // A mixer represents an actual fan-in operation, not a decorative route
    // annotation. Once a focused editor, disconnect, or import leaves one
    // (or zero) active contributors, remove the redundant processor so every
    // graph card continues to correspond to live canonical behavior.
    state.mixers.erase(std::remove_if(state.mixers.begin(), state.mixers.end(),
        [&state](const SignalFlowMixer &mixer) {
            const int contributors = static_cast<int>(std::count_if(state.routes.cbegin(),
                state.routes.cend(), [&mixer](const SignalFlowRoute &route) {
                    return route.enabled && route.profileId == mixer.profileId
                        && route.controllerRecordId == mixer.controllerRecordId
                        && route.sourceKind == SignalFlowPortKind::Axis
                        && route.destinationKind == SignalFlowPortKind::Axis
                        && route.destinationIndex == mixer.destinationAxis;
                }));
            return contributors < 2;
        }), state.mixers.end());
}

void normalizeSharedProcessors(MapperConfiguration *configuration)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    std::vector<SignalFlowSharedProcessor> candidates;
    candidates.reserve(state.sharedProcessors.size());
    for (SignalFlowSharedProcessor processor : state.sharedProcessors) {
        processor.profileId = processor.profileId.trimmed().left(96);
        processor.controllerRecordId = processor.controllerRecordId.trimmed().left(96);
        processor.identityKey = processor.identityKey.trimmed().left(320);
        processor.id = processor.id.trimmed().left(96);
        processor.kind = processor.kind.trimmed().toLower().left(48);
        if (!processor.enabled || !supportedSharedProcessorKind(processor.kind)
            || !findProfile(*configuration, processor.profileId)) {
            continue;
        }
        SignalFlowRoute scope;
        scope.profileId = processor.profileId;
        scope.controllerRecordId = processor.controllerRecordId;
        if (!routeScopeExists(*configuration, scope)) continue;
        std::sort(processor.sourceAxes.begin(), processor.sourceAxes.end());
        processor.sourceAxes.erase(std::unique(processor.sourceAxes.begin(), processor.sourceAxes.end()),
                                  processor.sourceAxes.end());
        processor.sourceAxes.erase(std::remove_if(processor.sourceAxes.begin(), processor.sourceAxes.end(),
            [&state, &processor](int axis) {
                return axis < 0 || axis >= kPhysicalAxisCount
                    || !scopeAxisIsRouted(state, processor, axis);
            }), processor.sourceAxes.end());
        if (processor.sourceAxes.size() < 2) continue;
        if (std::find(processor.sourceAxes.cbegin(), processor.sourceAxes.cend(), processor.ownerAxis)
            == processor.sourceAxes.cend()) {
            processor.ownerAxis = processor.sourceAxes.front();
        }
        const ControllerProfile *profile = findProfile(*configuration, processor.profileId);
        if (!profile) continue;
        if (processor.identityKey.isEmpty()) {
            processor.identityKey = sharedProcessorKey(*profile, processor.controllerRecordId,
                                                       processor.kind, processor.ownerAxis);
        }
        candidates.push_back(std::move(processor));
    }
    std::sort(candidates.begin(), candidates.end(), [](const SignalFlowSharedProcessor &left,
                                                        const SignalFlowSharedProcessor &right) {
        const QString leftScope = routeScopeKey(left.profileId, left.controllerRecordId)
            + u":"_qs + left.kind + u":"_qs + QString::number(left.ownerAxis);
        const QString rightScope = routeScopeKey(right.profileId, right.controllerRecordId)
            + u":"_qs + right.kind + u":"_qs + QString::number(right.ownerAxis);
        return leftScope == rightScope ? left.identityKey < right.identityKey : leftScope < rightScope;
    });
    QSet<QString> claimedAxes;
    QSet<QString> identities;
    std::vector<SignalFlowSharedProcessor> repaired;
    repaired.reserve(candidates.size());
    for (SignalFlowSharedProcessor processor : candidates) {
        if (identities.contains(processor.identityKey)) continue;
        std::vector<int> available;
        available.reserve(processor.sourceAxes.size());
        for (const int axis : processor.sourceAxes) {
            const QString claim = routeScopeKey(processor.profileId, processor.controllerRecordId)
                + u":"_qs + processor.kind + u":"_qs + QString::number(axis);
            if (claimedAxes.contains(claim)) continue;
            available.push_back(axis);
        }
        processor.sourceAxes = std::move(available);
        if (processor.sourceAxes.size() < 2) continue;
        if (std::find(processor.sourceAxes.cbegin(), processor.sourceAxes.cend(), processor.ownerAxis)
            == processor.sourceAxes.cend()) {
            processor.ownerAxis = processor.sourceAxes.front();
        }
        for (const int axis : processor.sourceAxes) {
            claimedAxes.insert(routeScopeKey(processor.profileId, processor.controllerRecordId)
                + u":"_qs + processor.kind + u":"_qs + QString::number(axis));
        }
        identities.insert(processor.identityKey);
        repaired.push_back(std::move(processor));
    }
    state.sharedProcessors = std::move(repaired);
}

void mirrorSharedProcessorSettings(MapperConfiguration *configuration)
{
    if (!configuration) return;
    for (const SignalFlowSharedProcessor &processor : configuration->signalFlow.sharedProcessors) {
        if (!processor.enabled || processor.sourceAxes.size() < 2) continue;
        AdaptiveResponseLayer *adaptiveLayer = nullptr;
        AxisMappings *axes = axisMappingsForScope(configuration, processor.profileId,
                                                  processor.controllerRecordId, &adaptiveLayer);
        if (!axes || processor.ownerAxis < 0 || processor.ownerAxis >= kPhysicalAxisCount) continue;
        const AxisMapping owner = (*axes)[static_cast<size_t>(processor.ownerAxis)];
        const AdaptiveResponseAxisOverride ownerAdaptive = adaptiveLayer
            ? adaptiveLayer->axes[static_cast<size_t>(processor.ownerAxis)]
            : AdaptiveResponseAxisOverride{};
        for (const int axis : processor.sourceAxes) {
            if (axis < 0 || axis >= kPhysicalAxisCount || axis == processor.ownerAxis) continue;
            AdaptiveResponseAxisOverride *memberAdaptive = adaptiveLayer
                ? &adaptiveLayer->axes[static_cast<size_t>(axis)] : nullptr;
            mirrorSharedProcessorSetting(&(*axes)[static_cast<size_t>(axis)], owner,
                                         memberAdaptive, ownerAdaptive, processor.kind);
        }
    }
}

void splitDivergedSharedProcessorSettings(MapperConfiguration *configuration)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    std::vector<SignalFlowSharedProcessor> coherent;
    coherent.reserve(state.sharedProcessors.size());
    for (SignalFlowSharedProcessor processor : state.sharedProcessors) {
        AdaptiveResponseLayer *adaptiveLayer = nullptr;
        AxisMappings *axes = axisMappingsForScope(configuration, processor.profileId,
                                                  processor.controllerRecordId, &adaptiveLayer);
        if (!axes || processor.ownerAxis < 0 || processor.ownerAxis >= kPhysicalAxisCount) continue;
        const AxisMapping owner = (*axes)[static_cast<size_t>(processor.ownerAxis)];
        const AdaptiveResponseAxisOverride ownerAdaptive = adaptiveLayer
            ? adaptiveLayer->axes[static_cast<size_t>(processor.ownerAxis)]
            : AdaptiveResponseAxisOverride{};
        // A just-created relation has no durable ID yet. Its selected owner
        // deliberately seeds the shared setting, even when its candidate
        // channels previously had individual values. Existing relations do
        // the opposite: an independently changed member is removed before it
        // can be silently overwritten by the owner's value.
        const bool seedFromOwner = processor.id.isEmpty();
        std::vector<int> retainedAxes;
        retainedAxes.reserve(processor.sourceAxes.size());
        for (const int axis : processor.sourceAxes) {
            if (axis < 0 || axis >= kPhysicalAxisCount) continue;
            if (axis == processor.ownerAxis) {
                retainedAxes.push_back(axis);
                continue;
            }
            AdaptiveResponseAxisOverride *memberAdaptive = adaptiveLayer
                ? &adaptiveLayer->axes[static_cast<size_t>(axis)] : nullptr;
            AxisMapping &member = (*axes)[static_cast<size_t>(axis)];
            if (seedFromOwner) {
                mirrorSharedProcessorSetting(&member, owner, memberAdaptive, ownerAdaptive, processor.kind);
                retainedAxes.push_back(axis);
            } else if (sharedProcessorSettingsMatch(member, owner, memberAdaptive,
                                                     adaptiveLayer ? &ownerAdaptive : nullptr,
                                                     processor.kind)) {
                retainedAxes.push_back(axis);
            }
        }
        processor.sourceAxes = std::move(retainedAxes);
        if (processor.sourceAxes.size() >= 2) coherent.push_back(std::move(processor));
    }
    state.sharedProcessors = std::move(coherent);
}

bool activeMixerForDestination(const SignalFlowState &state, const SignalFlowRoute &route)
{
    if (route.destinationKind != SignalFlowPortKind::Axis) return false;
    return std::any_of(state.mixers.cbegin(), state.mixers.cend(), [&route](const SignalFlowMixer &mixer) {
        return mixer.enabled && mixer.mode != SignalFlowMixerMode::Disabled
            && mixer.profileId == route.profileId
            && mixer.controllerRecordId == route.controllerRecordId
            && mixer.destinationAxis == route.destinationIndex;
    });
}

void enforceExplicitAnalogMerge(SignalFlowState *state)
{
    if (!state) return;
    QSet<QString> occupied;
    for (SignalFlowRoute &route : state->routes) {
        if (!route.enabled || route.sourceKind != SignalFlowPortKind::Axis
            || route.destinationKind != SignalFlowPortKind::Axis) continue;
        const QString destination = routeScopeKey(route.profileId, route.controllerRecordId)
            + u":axis:"_qs + QString::number(route.destinationIndex);
        if (!occupied.contains(destination)) {
            occupied.insert(destination);
            continue;
        }
        if (!activeMixerForDestination(*state, route)) {
            // Keep malformed/colliding saved topology inspectable, but never
            // let it become a silent analog merge at runtime.
            route.enabled = false;
        }
    }
}

void projectScopeToCompatibility(const SignalFlowState &state, ControllerProfile *profile,
                                 DeviceProfileMapping *mapping, const QString &controllerRecordId,
                                 NativePovBindings *legacyNativePovs)
{
    if (!profile) return;
    AxisMappings &axes = mapping ? mapping->axes : profile->axes;
    ButtonBindings &buttons = mapping ? mapping->buttons : profile->buttons;
    PovBindings &povs = mapping ? mapping->povs : profile->povs;
    for (AxisMapping &axis : axes) axis.target = VirtualAxis::Disabled;
    // Button, POV and native-POV mappings retain their focused-editor state
    // until a topology route explicitly projects over them.  Unlike axes,
    // their absent entry represents the legacy default behavior; clearing all
    // of them here would silently turn every input into an explicit disable.

    QSet<QString> projectedSources;
    for (const SignalFlowRoute &route : state.routes) {
        if (!route.enabled || route.profileId != profile->id
            || route.controllerRecordId != controllerRecordId) continue;
        const QString source = routeSourceSlotKey(route);
        if (projectedSources.contains(source)) continue;
        projectedSources.insert(source);
        if (route.sourceKind == SignalFlowPortKind::Axis
            && route.destinationKind == SignalFlowPortKind::Axis
            && route.sourceIndex >= 0 && route.sourceIndex < kPhysicalAxisCount) {
            axes[static_cast<size_t>(route.sourceIndex)].target =
                static_cast<VirtualAxis>(route.destinationIndex);
        } else if (route.sourceKind == SignalFlowPortKind::Button
                   && route.destinationKind == SignalFlowPortKind::Button) {
            if (buttons.size() <= static_cast<size_t>(route.sourceIndex)) {
                buttons.resize(static_cast<size_t>(route.sourceIndex + 1));
            }
            ButtonBinding &binding = buttons[static_cast<size_t>(route.sourceIndex)];
            const QString customName = binding.customName;
            binding = {ButtonActionType::VirtualButton, route.destinationIndex,
                       !route.implicitDefault, customName};
        } else if (route.sourceKind == SignalFlowPortKind::PovDirection
                   && route.destinationKind == SignalFlowPortKind::Button) {
            if (povs.size() <= static_cast<size_t>(route.sourceIndex)) {
                povs.resize(static_cast<size_t>(route.sourceIndex + 1));
            }
            ButtonBinding &binding = povs[static_cast<size_t>(route.sourceIndex)]
                [static_cast<size_t>(route.sourceSubIndex)];
            const QString customName = binding.customName;
            binding = {ButtonActionType::VirtualButton, route.destinationIndex,
                       !route.implicitDefault, customName};
        } else if (route.sourceKind == SignalFlowPortKind::NativePov
                   && route.destinationKind == SignalFlowPortKind::NativePov && legacyNativePovs) {
            if (legacyNativePovs->size() <= static_cast<size_t>(route.sourceIndex)) {
                legacyNativePovs->resize(static_cast<size_t>(route.sourceIndex + 1));
            }
            (*legacyNativePovs)[static_cast<size_t>(route.sourceIndex)] = {
                true, static_cast<NativePovTargetType>(route.destinationSubIndex), route.destinationIndex};
        }
    }
}

void projectTopologyToCompatibility(MapperConfiguration *configuration)
{
    if (!configuration) return;
    for (ControllerProfile &profile : configuration->profiles) {
        if (profile.deviceMappings.empty()) {
            projectScopeToCompatibility(configuration->signalFlow, &profile, nullptr, {},
                                        &configuration->nativePovBindings);
            continue;
        }
        for (DeviceProfileMapping &mapping : profile.deviceMappings) {
            projectScopeToCompatibility(configuration->signalFlow, &profile, &mapping,
                                        mapping.controllerRecordId, &mapping.nativePovBindings);
        }
    }
}

void eraseRoutesForSource(std::vector<SignalFlowRoute> *routes, const QString &profileId,
                          const QString &controllerRecordId, SignalFlowPortKind sourceKind,
                          int sourceIndex, int sourceSubIndex)
{
    if (!routes) return;
    routes->erase(std::remove_if(routes->begin(), routes->end(), [&](const SignalFlowRoute &route) {
        return route.profileId == profileId && route.controllerRecordId == controllerRecordId
            && route.sourceKind == sourceKind && route.sourceIndex == sourceIndex
            && route.sourceSubIndex == sourceSubIndex;
    }), routes->end());
}

void synchronizeCompatibilitySource(MapperConfiguration *configuration,
                                    const ControllerProfile &profile,
                                    const QString &controllerRecordId,
                                    SignalFlowPortKind sourceKind, int sourceIndex,
                                    int sourceSubIndex, bool mapped,
                                    SignalFlowPortKind destinationKind, int destinationIndex,
                                    int destinationSubIndex, bool implicitDefault)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    if (!mapped) {
        eraseRoutesForSource(&state.routes, profile.id, controllerRecordId, sourceKind,
                             sourceIndex, sourceSubIndex);
        return;
    }
    const auto matchingPrimary = std::find_if(state.routes.begin(), state.routes.end(),
        [&](const SignalFlowRoute &route) {
            return route.enabled && route.primaryProjection && route.profileId == profile.id
                && route.controllerRecordId == controllerRecordId && route.sourceKind == sourceKind
                && route.sourceIndex == sourceIndex && route.sourceSubIndex == sourceSubIndex
                && route.destinationKind == destinationKind && route.destinationIndex == destinationIndex
                && route.destinationSubIndex == destinationSubIndex;
        });
    if (matchingPrimary != state.routes.end()) {
        matchingPrimary->implicitDefault = implicitDefault;
        return;
    }
    // A focused editor has only one destination field. Treat a changed field
    // as an explicit rewrite of this source, not an ambiguous mutation of one
    // of several native fan-out routes.
    eraseRoutesForSource(&state.routes, profile.id, controllerRecordId, sourceKind,
                         sourceIndex, sourceSubIndex);
    appendCompatibilityRoute(&state.routes, profile, controllerRecordId, sourceKind, sourceIndex,
                             sourceSubIndex, destinationKind, destinationIndex,
                             destinationSubIndex, true, implicitDefault);
}

void synchronizeCompatibilityScope(MapperConfiguration *configuration,
                                   const ControllerProfile &profile,
                                   const DeviceProfileMapping *mapping,
                                   const QString &controllerRecordId)
{
    if (!configuration) return;
    const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
    const ButtonBindings &buttons = mapping ? mapping->buttons : profile.buttons;
    const PovBindings &povs = mapping ? mapping->povs : profile.povs;
    const NativePovBindings &nativePovs = mapping ? mapping->nativePovBindings
                                                   : configuration->nativePovBindings;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const int target = static_cast<int>(axes[static_cast<size_t>(axis)].target);
        synchronizeCompatibilitySource(configuration, profile, controllerRecordId,
            SignalFlowPortKind::Axis, axis, -1, target > 0 && target < kVirtualAxisSlotCount,
            SignalFlowPortKind::Axis, target, -1, false);
    }
    for (int button = 0; button < std::max(static_cast<int>(buttons.size()), kMaximumPhysicalButtons);
         ++button) {
        const ButtonBinding binding = button < static_cast<int>(buttons.size())
            ? buttons[static_cast<size_t>(button)] : ButtonBinding{};
        synchronizeCompatibilitySource(configuration, profile, controllerRecordId,
            SignalFlowPortKind::Button, button, -1,
            binding.type == ButtonActionType::VirtualButton && binding.target > 0,
            SignalFlowPortKind::Button, binding.target, -1, !binding.explicitlyConfigured);
    }
    for (int hat = 0; hat < std::max(static_cast<int>(povs.size()), kMaximumPhysicalPovs); ++hat) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding binding = hat < static_cast<int>(povs.size())
                ? povs[static_cast<size_t>(hat)][static_cast<size_t>(direction)] : ButtonBinding{};
            synchronizeCompatibilitySource(configuration, profile, controllerRecordId,
                SignalFlowPortKind::PovDirection, hat, direction,
                binding.type == ButtonActionType::VirtualButton && binding.target > 0,
                SignalFlowPortKind::Button, binding.target, -1, !binding.explicitlyConfigured);
        }
    }
    for (int hat = 0; hat < std::max(static_cast<int>(nativePovs.size()), kMaximumPhysicalPovs); ++hat) {
        const NativePovBinding binding = hat < static_cast<int>(nativePovs.size())
            ? nativePovs[static_cast<size_t>(hat)] : NativePovBinding{};
        synchronizeCompatibilitySource(configuration, profile, controllerRecordId,
            SignalFlowPortKind::NativePov, hat, -1,
            binding.enabled && binding.targetIndex > 0
                && binding.targetType != NativePovTargetType::Disabled,
            SignalFlowPortKind::NativePov, binding.targetIndex,
            static_cast<int>(binding.targetType), false);
    }
}

void synchronizeTopologyFromCompatibility(MapperConfiguration *configuration)
{
    if (!configuration) return;
    if (configuration->signalFlow.topologyVersion < kSignalFlowCanonicalTopologyVersion) {
        importCompatibilityTopology(configuration);
        return;
    }
    for (const ControllerProfile &profile : configuration->profiles) {
        if (profile.deviceMappings.empty()) {
            synchronizeCompatibilityScope(configuration, profile, nullptr, {});
            continue;
        }
        for (const DeviceProfileMapping &mapping : profile.deviceMappings) {
            synchronizeCompatibilityScope(configuration, profile, &mapping, mapping.controllerRecordId);
        }
    }
}

void collectTopologyDesiredIdentities(const MapperConfiguration &configuration,
                                      DesiredSignalFlowIdentities *desired)
{
    if (!desired) return;
    for (const SignalFlowRoute &route : configuration.signalFlow.routes) {
        if (!route.identityKey.isEmpty()) desired->routes.insert(route.identityKey);
    }
    for (const SignalFlowMixer &mixer : configuration.signalFlow.mixers) {
        if (!mixer.identityKey.isEmpty()) desired->processors.insert(mixer.identityKey);
    }
    for (const SignalFlowSharedProcessor &processor : configuration.signalFlow.sharedProcessors) {
        if (processor.enabled && !processor.identityKey.isEmpty()) {
            desired->processors.insert(processor.identityKey);
        }
    }
    for (const ControllerProfile &profile : configuration.profiles) {
        const auto collectProcessors = [&](const DeviceProfileMapping *mapping,
                                           const QString &controllerId) {
            const AxisMappings &axes = mapping ? mapping->axes : profile.axes;
            for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
                bool routed = false;
                for (const SignalFlowRoute &route : configuration.signalFlow.routes) {
                    routed = routed || (route.enabled && route.profileId == profile.id
                        && route.controllerRecordId == controllerId
                        && route.sourceKind == SignalFlowPortKind::Axis && route.sourceIndex == axis);
                }
                if (!routed) continue;
                const AxisMapping &axisMapping = axes[static_cast<size_t>(axis)];
                const auto addProcessor = [&](bool active, const QString &kind) {
                    if (!active) return;
                    const SignalFlowSharedProcessor *shared = sharedProcessorForAxis(
                        configuration.signalFlow, profile.id, controllerId, kind, axis);
                    desired->processors.insert(shared ? shared->identityKey
                        : processorKey(profile, controllerId, axis, kind));
                };
                addProcessor(axisMapping.rangeMode == AxisRangeMode::OneSided, u"domain"_qs);
                addProcessor(hasNonDefaultDeadzone(axisMapping), u"deadzone"_qs);
                addProcessor(hasNonDefaultCenterHold(axisMapping), u"center-hold"_qs);
                addProcessor(axisMapping.inverted, u"invert"_qs);
                addProcessor(hasNonDefaultCurve(axisMapping), u"curve"_qs);
                addProcessor(hasNonDefaultLimits(axisMapping), u"limits"_qs);
                addProcessor(adaptiveResponseEnabled(configuration, profile, mapping, axis),
                             u"adaptive-response"_qs);
            }
        };
        if (profile.deviceMappings.empty()) collectProcessors(nullptr, {});
        else for (const DeviceProfileMapping &mapping : profile.deviceMappings) {
            if (mapping.enabled) collectProcessors(&mapping, mapping.controllerRecordId);
        }
    }
    for (const AutomationDefinition &automation : configuration.automations) {
        if (!automation.id.trimmed().isEmpty()) {
            desired->processors.insert(u"processor:automation:"_qs + automation.id.trimmed());
        }
    }
}

void repairIdentitySpace(SignalFlowState *state)
{
    if (!state) return;
    QSet<QString> used;
    for (const SignalFlowIdentityRecord &identity : state->routeIdentities) used.insert(identity.id);
    for (SignalFlowIdentityRecord &identity : state->processorIdentities) {
        if (used.contains(identity.id)) {
            identity.id = allocateIdentity(identity.key, identity.generation, &used);
        } else {
            used.insert(identity.id);
        }
    }
}

void populateRouteIdsAndProcessorPaths(MapperConfiguration *configuration)
{
    if (!configuration) return;
    SignalFlowState &state = configuration->signalFlow;
    for (SignalFlowMixer &mixer : state.mixers) {
        const SignalFlowIdentityRecord *identity = findSignalFlowIdentity(
            state.processorIdentities, mixer.identityKey);
        mixer.id = identity && identity->active ? identity->id : QString{};
    }
    for (SignalFlowSharedProcessor &processor : state.sharedProcessors) {
        const SignalFlowIdentityRecord *identity = findSignalFlowIdentity(
            state.processorIdentities, processor.identityKey);
        processor.id = identity && identity->active ? identity->id : QString{};
    }
    for (SignalFlowRoute &route : state.routes) {
        const SignalFlowIdentityRecord *identity = findSignalFlowIdentity(state.routeIdentities,
                                                                            route.identityKey);
        route.id = identity && identity->active ? identity->id : QString{};
        route.processorPath.clear();
        // Persist the actual edges around every processor.  The old route
        // plus processorPath representation is retained as the compatibility
        // projection consumed by focused editors/runtime compilation, but it
        // is no longer the only topology available to the graph.  Every
        // processor now has a durable, route-channel-specific IN and OUT
        // endpoint and every visible leg has its own canonical id.
        route.segments.clear();
        if (!route.enabled || route.id.isEmpty()) continue;
        if (route.sourceKind == SignalFlowPortKind::Axis) {
            const ControllerProfile *profile = findProfile(*configuration, route.profileId);
            if (!profile || route.sourceIndex < 0 || route.sourceIndex >= kPhysicalAxisCount) continue;
            const DeviceProfileMapping *mapping = route.controllerRecordId.isEmpty() ? nullptr
                : findDeviceProfileMapping(*profile, route.controllerRecordId);
            const AxisMappings &axes = mapping ? mapping->axes : profile->axes;
            const AxisMapping &axis = axes[static_cast<size_t>(route.sourceIndex)];
            const auto appendProcessor = [&](bool active, const QString &kind) {
                if (!active) return;
                const SignalFlowSharedProcessor *shared = sharedProcessorForAxis(
                    state, route.profileId, route.controllerRecordId, kind, route.sourceIndex);
                const SignalFlowIdentityRecord *processor = findSignalFlowIdentity(
                    state.processorIdentities,
                    shared ? shared->identityKey
                        : processorKey(*profile, route.controllerRecordId, route.sourceIndex, kind));
                if (processor && processor->active && !route.processorPath.contains(processor->id)) {
                    route.processorPath.append(processor->id);
                }
            };
            appendProcessor(axis.rangeMode == AxisRangeMode::OneSided, u"domain"_qs);
            appendProcessor(hasNonDefaultDeadzone(axis), u"deadzone"_qs);
            appendProcessor(hasNonDefaultCenterHold(axis), u"center-hold"_qs);
            appendProcessor(axis.inverted, u"invert"_qs);
            appendProcessor(hasNonDefaultCurve(axis), u"curve"_qs);
            appendProcessor(hasNonDefaultLimits(axis), u"limits"_qs);
            appendProcessor(adaptiveResponseEnabled(*configuration, *profile, mapping, route.sourceIndex),
                            u"adaptive-response"_qs);
            for (const SignalFlowMixer &mixer : state.mixers) {
                if (!mixer.enabled || mixer.mode == SignalFlowMixerMode::Disabled
                    || mixer.profileId != route.profileId
                    || mixer.controllerRecordId != route.controllerRecordId
                    || mixer.destinationAxis != route.destinationIndex) continue;
                const SignalFlowIdentityRecord *processor = findSignalFlowIdentity(
                    state.processorIdentities, mixer.identityKey);
                if (processor && processor->active) route.processorPath.append(processor->id);
                break;
            }
        }
        QString currentEndpoint = QString(u"sf-endpoint:%1:source"_qs).arg(route.id);
        int ordinal = 0;
        const auto appendSegment = [&route, &currentEndpoint, &ordinal](const QString &nextEndpoint) {
            if (nextEndpoint.isEmpty()) return;
            route.segments.push_back(SignalFlowRouteSegment{
                QString(u"sfseg:%1:%2"_qs).arg(route.id).arg(ordinal++),
                currentEndpoint,
                nextEndpoint});
            currentEndpoint = nextEndpoint;
        };
        for (const QString &processorId : route.processorPath) {
            if (processorId.isEmpty()) continue;
            const QString input = QString(u"sf-port:%1:%2:in"_qs).arg(processorId, route.id);
            const QString output = QString(u"sf-port:%1:%2:out"_qs).arg(processorId, route.id);
            appendSegment(input);
            currentEndpoint = output;
        }
        appendSegment(QString(u"sf-endpoint:%1:destination"_qs).arg(route.id));
    }
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

QString signalFlowFanoutRouteIdentityKey(const ControllerProfile &profile,
                                         const QString &controllerRecordId,
                                         const QString &kind, int sourceIndex,
                                         int sourceSubIndex,
                                         SignalFlowPortKind destinationKind,
                                         int destinationIndex, int destinationSubIndex)
{
    return fanoutRouteKey(profile, controllerRecordId, kind.trimmed(), sourceIndex,
                          sourceSubIndex, destinationKind, destinationIndex,
                          destinationSubIndex);
}

QString signalFlowMixerIdentityKey(const ControllerProfile &profile,
                                   const QString &controllerRecordId, int destinationAxis)
{
    return mixerKey(profile, controllerRecordId, destinationAxis);
}

QString signalFlowSharedProcessorIdentityKey(const ControllerProfile &profile,
                                             const QString &controllerRecordId,
                                             const QString &kind, int ownerAxis)
{
    return sharedProcessorKey(profile, controllerRecordId, kind.trimmed().toLower(), ownerAxis);
}

const SignalFlowIdentityRecord *findSignalFlowIdentity(
    const std::vector<SignalFlowIdentityRecord> &identities, const QString &key)
{
    const QString normalized = key.trimmed();
    const auto found = std::find_if(identities.cbegin(), identities.cend(),
        [&normalized](const SignalFlowIdentityRecord &identity) { return identity.key == normalized; });
    return found == identities.cend() ? nullptr : &*found;
}

const SignalFlowRoute *findSignalFlowRouteById(const SignalFlowState &state, const QString &id)
{
    const QString normalized = id.trimmed();
    const auto found = std::find_if(state.routes.cbegin(), state.routes.cend(),
        [&normalized](const SignalFlowRoute &route) { return route.id == normalized; });
    return found == state.routes.cend() ? nullptr : &*found;
}

SignalFlowRoute *findSignalFlowRouteById(SignalFlowState *state, const QString &id)
{
    if (!state) return nullptr;
    const QString normalized = id.trimmed();
    const auto found = std::find_if(state->routes.begin(), state->routes.end(),
        [&normalized](const SignalFlowRoute &route) { return route.id == normalized; });
    return found == state->routes.end() ? nullptr : &*found;
}

bool signalFlowRouteMatchesScope(const SignalFlowRoute &route,
                                 const ControllerProfile &profile,
                                 const QString &controllerRecordId)
{
    return route.profileId == profile.id && route.controllerRecordId == controllerRecordId.trimmed();
}

bool signalFlowPropagateSharedProcessorSettings(MapperConfiguration *configuration,
                                                const QString &profileId,
                                                const QString &controllerRecordId,
                                                const QString &kind, int ownerAxis)
{
    if (!configuration || ownerAxis < 0 || ownerAxis >= kPhysicalAxisCount) return false;
    const QString normalizedKind = kind.trimmed().toLower();
    const QString normalizedControllerId = controllerRecordId.trimmed();
    const auto shared = std::find_if(configuration->signalFlow.sharedProcessors.cbegin(),
        configuration->signalFlow.sharedProcessors.cend(), [&](const SignalFlowSharedProcessor &processor) {
            return processor.enabled && processor.profileId == profileId
                && processor.controllerRecordId == normalizedControllerId
                && processor.kind == normalizedKind && processor.ownerAxis == ownerAxis;
        });
    if (shared == configuration->signalFlow.sharedProcessors.cend()
        || shared->sourceAxes.size() < 2) {
        return false;
    }
    AdaptiveResponseLayer *adaptiveLayer = nullptr;
    AxisMappings *axes = axisMappingsForScope(configuration, profileId, normalizedControllerId,
                                              &adaptiveLayer);
    if (!axes) return false;
    const AxisMapping owner = (*axes)[static_cast<size_t>(ownerAxis)];
    const AdaptiveResponseAxisOverride ownerAdaptive = adaptiveLayer
        ? adaptiveLayer->axes[static_cast<size_t>(ownerAxis)]
        : AdaptiveResponseAxisOverride{};
    for (const int axis : shared->sourceAxes) {
        if (axis < 0 || axis >= kPhysicalAxisCount || axis == ownerAxis) continue;
        AdaptiveResponseAxisOverride *memberAdaptive = adaptiveLayer
            ? &adaptiveLayer->axes[static_cast<size_t>(axis)] : nullptr;
        mirrorSharedProcessorSetting(&(*axes)[static_cast<size_t>(axis)], owner,
                                     memberAdaptive, ownerAdaptive, normalizedKind);
    }
    return true;
}

void projectSignalFlowTopologyToFocusedEditors(MapperConfiguration *configuration)
{
    projectTopologyToCompatibility(configuration);
}

void synchronizeSignalFlowTopologyFromFocusedEditors(MapperConfiguration *configuration)
{
    synchronizeTopologyFromCompatibility(configuration);
    reconcileSignalFlowState(configuration);
}

void reconcileSignalFlowState(MapperConfiguration *configuration)
{
    if (!configuration) return;
    if (configuration->signalFlow.topologyVersion < kSignalFlowCanonicalTopologyVersion) {
        importCompatibilityTopology(configuration);
    } else {
        // Focused editors still write their established endpoint fields. Their
        // normal immediate-persistence path reaches this reconciler, so import
        // an explicit focused-editor rewrite before rebuilding its projection.
        synchronizeTopologyFromCompatibility(configuration);
    }
    normalizeTopology(configuration);
    normalizeSharedProcessors(configuration);
    // Preserve an independent focused-editor change by splitting its channel
    // before the canonical owner value is mirrored. This makes divergence
    // visible in Signal Flow instead of hiding contradictory settings.
    splitDivergedSharedProcessorSettings(configuration);
    // The fixed-size runtime compiler already executes focused axis settings.
    // Mirror a shared object's owner to each member only at this control-plane
    // boundary, never while DirectInput reports are being processed.
    mirrorSharedProcessorSettings(configuration);
    enforceExplicitAnalogMerge(&configuration->signalFlow);
    // Compatibility fields are retained for focused editor continuity, but
    // topology owns the runtime route set once schema 27 has migrated.
    projectTopologyToCompatibility(configuration);
    DesiredSignalFlowIdentities desired;
    collectTopologyDesiredIdentities(*configuration, &desired);
    reconcileIdentities(&configuration->signalFlow.routeIdentities, desired.routes);
    reconcileIdentities(&configuration->signalFlow.processorIdentities, desired.processors);
    repairIdentitySpace(&configuration->signalFlow);
    populateRouteIdsAndProcessorPaths(configuration);
}

} // namespace hotas
