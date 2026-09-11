#pragma once

#include "mapping_types.h"

#include <QJsonArray>
#include <QJsonObject>

namespace hotas {

class ConfigStore final {
public:
    static MapperConfiguration load();
    static bool save(const MapperConfiguration &configuration);

    static QJsonObject toJson(const MapperConfiguration &configuration);
    static MapperConfiguration fromJson(const QJsonObject &json, bool *valid = nullptr);

    // Bounded portable-format adapters. They expose only durable
    // configuration records and deliberately never expose QSettings paths or
    // runtime state.
    static QJsonObject portableProfileToJson(const ControllerProfile &profile);
    static bool portableProfileFromJson(const QJsonObject &json, ControllerProfile *profile);
    // Portable profiles carry their canonical Signal Flow topology separately
    // from the focused profile fields. Runtime IDs and workspace layout are
    // intentionally excluded: import remaps profile identity first, then
    // reconciliation allocates collision-safe durable IDs locally.
    static QJsonObject portableSignalFlowTopologyToJson(const SignalFlowState &state,
                                                         const QStringList &profileIds);
    static bool portableSignalFlowTopologyFromJson(const QJsonValue &value,
                                                   SignalFlowState *state);
    static QJsonObject portableCategoryToJson(const ProfileCategory &category);
    static bool portableCategoryFromJson(const QJsonObject &json, ProfileCategory *category);
    static QJsonObject portableCurveToJson(const PersonalCurvePreset &preset);
    static bool portableCurveFromJson(const QJsonObject &json, PersonalCurvePreset *preset);
    static QJsonObject portableAdaptiveResponsePresetToJson(const AdaptiveResponsePreset &preset);
    static bool portableAdaptiveResponsePresetFromJson(const QJsonObject &json, AdaptiveResponsePreset *preset);
    static QJsonObject portableAdaptiveResponseLayerToJson(const AdaptiveResponseLayer &layer);
    static bool portableAdaptiveResponseLayerFromJson(const QJsonValue &value, AdaptiveResponseLayer *layer);
    static QJsonObject portableAutomationToJson(const AutomationDefinition &automation);
    static bool portableAutomationFromJson(const QJsonObject &json, AutomationDefinition *automation);
    static QJsonObject portableOutputLayoutToJson(const VirtualOutputLayout &layout);
    static bool portableOutputLayoutFromJson(const QJsonObject &json, VirtualOutputLayout *layout);
    static QJsonArray portableProfileTriggersToJson(const ProfileTriggerBindings &bindings);
    static ProfileTriggerBindings portableProfileTriggersFromJson(const QJsonValue &value);
    static QJsonArray portablePovProfileTriggersToJson(const PovProfileTriggerBindings &bindings);
    static PovProfileTriggerBindings portablePovProfileTriggersFromJson(const QJsonValue &value);
};

} // namespace hotas
