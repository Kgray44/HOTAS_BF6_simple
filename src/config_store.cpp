#include "config_store.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "response_curve.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace hotas {
namespace {

constexpr auto kConfigKey = "mapper/config";
constexpr int kProfileSchemaVersion = 12;
constexpr int kUniversalStrengthSchemaVersion = 7;

QString settingsFilePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + u"/settings.ini"_qs;
}

QJsonObject calibrationToJson(const Calibration &calibration)
{
    return {
        {u"enabled"_qs, calibration.enabled},
        {u"minimum"_qs, calibration.minimum},
        {u"center"_qs, calibration.center},
        {u"maximum"_qs, calibration.maximum},
    };
}

Calibration calibrationFromJson(const QJsonObject &json)
{
    Calibration calibration;
    calibration.enabled = json.value(u"enabled"_qs).toBool(false);
    calibration.minimum = std::clamp(float(json.value(u"minimum"_qs).toDouble(-1.0)), -1.0F, 1.0F);
    calibration.center = std::clamp(float(json.value(u"center"_qs).toDouble(0.0)), -1.0F, 1.0F);
    calibration.maximum = std::clamp(float(json.value(u"maximum"_qs).toDouble(1.0)), -1.0F, 1.0F);
    if (!(calibration.minimum < calibration.center && calibration.center < calibration.maximum)) {
        calibration.enabled = false;
        calibration.minimum = -1.0F;
        calibration.center = 0.0F;
        calibration.maximum = 1.0F;
    }
    return calibration;
}

QJsonObject axisMappingToJson(const AxisMapping &mapping)
{
    return {
        {u"target"_qs, virtualAxisLabel(mapping.target)},
        {u"inverted"_qs, mapping.inverted},
        {u"deadzone"_qs, mapping.deadzone},
        {u"hysteresis"_qs, mapping.hysteresis},
        {u"outputMinimum"_qs, mapping.outputMinimum},
        {u"outputMaximum"_qs, mapping.outputMaximum},
        {u"curve"_qs, curveDefinitionToJson(mapping.curve)},
    };
}

AxisMapping axisMappingFromJson(const QJsonObject &json, bool unipolar)
{
    AxisMapping mapping;
    mapping.target = virtualAxisFromString(json.value(u"target"_qs).toString());
    mapping.inverted = json.value(u"inverted"_qs).toBool(false);
    mapping.deadzone = std::clamp(float(json.value(u"deadzone"_qs).toDouble(0.03)), 0.0F, 0.95F);
    mapping.hysteresis = std::clamp(float(json.value(u"hysteresis"_qs).toDouble(0.002)), 0.0F, 0.25F);
    mapping.outputMinimum = std::clamp(float(json.value(u"outputMinimum"_qs).toDouble(-1.0)), -1.0F, 1.0F);
    mapping.outputMaximum = std::clamp(float(json.value(u"outputMaximum"_qs).toDouble(1.0)), -1.0F, 1.0F);
    mapping.curve = curveDefinitionFromJson(json.value(u"curve"_qs).toObject(), unipolar);
    normalizeAxisProcessing(mapping);
    return mapping;
}

QJsonObject buttonBindingToJson(const ButtonBinding &binding)
{
    if (binding.type == ButtonActionType::VirtualButton) {
        return {{u"type"_qs, u"virtualButton"_qs}, {u"target"_qs, binding.target},
                {u"explicit"_qs, binding.explicitlyConfigured}};
    }
    return {{u"type"_qs, u"disabled"_qs}, {u"explicit"_qs, binding.explicitlyConfigured}};
}

ButtonBinding buttonBindingFromJson(const QJsonObject &json)
{
    const bool explicitlyConfigured = json.value(u"explicit"_qs).toBool(false);
    if (json.value(u"type"_qs).toString().trimmed().compare(u"virtualButton"_qs, Qt::CaseInsensitive) == 0) {
        return {ButtonActionType::VirtualButton, json.value(u"target"_qs).toInt(0), explicitlyConfigured};
    }
    return {ButtonActionType::Disabled, 0, explicitlyConfigured};
}

void readGlobalSettings(const QJsonObject &json, MapperConfiguration &configuration)
{
    configuration.preferredDeviceId = json.value(u"preferredDeviceId"_qs).toString();
    configuration.vjoyDeviceId = std::clamp(json.value(u"vjoyDeviceId"_qs).toInt(1), 1, 16);
    configuration.startMappingOnLaunch = json.value(u"startMappingOnLaunch"_qs).toBool(false);
    configuration.disabledAxisValue = sanitizedDisabledAxisValue(
        static_cast<float>(json.value(u"disabledAxisValue"_qs).toDouble(0.0)));
    configuration.selectedAxisIndex = std::clamp(json.value(u"selectedAxisIndex"_qs)
        .toInt(static_cast<int>(PhysicalAxis::X)), 0, kPhysicalAxisCount - 1);
    configuration.automationEnabled = json.value(u"automationEnabled"_qs).toBool(true);
}

QString automationMatchModeToString(AutomationMatchMode mode)
{
    return mode == AutomationMatchMode::Any ? u"any"_qs : u"all"_qs;
}

bool automationMatchModeFromString(const QString &value, AutomationMatchMode *mode)
{
    if (!mode) return false;
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == u"all"_qs) { *mode = AutomationMatchMode::All; return true; }
    if (normalized == u"any"_qs) { *mode = AutomationMatchMode::Any; return true; }
    return false;
}

QString automationActivationModeToString(AutomationActivationMode mode)
{
    switch (mode) {
    case AutomationActivationMode::WhileTriggerActive: return u"whileTriggerActive"_qs;
    case AutomationActivationMode::ToggleOnTrigger: return u"toggleOnTrigger"_qs;
    case AutomationActivationMode::RunBriefly: return u"runBriefly"_qs;
    }
    return {};
}

bool automationActivationModeFromString(const QString &value, AutomationActivationMode *mode)
{
    if (!mode) return false;
    const QString normalized = value.trimmed();
    if (normalized == u"whileTriggerActive"_qs) {
        *mode = AutomationActivationMode::WhileTriggerActive;
        return true;
    }
    if (normalized == u"toggleOnTrigger"_qs) {
        *mode = AutomationActivationMode::ToggleOnTrigger;
        return true;
    }
    if (normalized == u"runBriefly"_qs) {
        *mode = AutomationActivationMode::RunBriefly;
        return true;
    }
    return false;
}

QString automationConditionTypeToString(AutomationConditionType type)
{
    switch (type) {
    case AutomationConditionType::Always: return u"always"_qs;
    case AutomationConditionType::AxisAbove: return u"axisAbove"_qs;
    case AutomationConditionType::AxisBelow: return u"axisBelow"_qs;
    case AutomationConditionType::AxisBetween: return u"axisBetween"_qs;
    case AutomationConditionType::AxisOutsideRange: return u"axisOutsideRange"_qs;
    case AutomationConditionType::ButtonHeld: return u"buttonHeld"_qs;
    case AutomationConditionType::ButtonReleased: return u"buttonReleased"_qs;
    case AutomationConditionType::PovActive: return u"povActive"_qs;
    case AutomationConditionType::PovInactive: return u"povInactive"_qs;
    case AutomationConditionType::BaseProfileIs: return u"baseProfileIs"_qs;
    case AutomationConditionType::EffectiveProfileIs: return u"effectiveProfileIs"_qs;
    case AutomationConditionType::ButtonPressed: return u"buttonPressed"_qs;
    case AutomationConditionType::ButtonReleaseEvent: return u"buttonReleaseEvent"_qs;
    case AutomationConditionType::ButtonMultiPress: return u"buttonMultiPress"_qs;
    case AutomationConditionType::ButtonLongPress: return u"buttonLongPress"_qs;
    case AutomationConditionType::AxisCrossesAbove: return u"axisCrossesAbove"_qs;
    case AutomationConditionType::AxisCrossesBelow: return u"axisCrossesBelow"_qs;
    }
    return {};
}

bool automationConditionTypeFromString(const QString &value, AutomationConditionType *type)
{
    if (!type) return false;
    const QString normalized = value.trimmed();
    const std::array<std::pair<QString, AutomationConditionType>, 17> choices{{
        {u"always"_qs, AutomationConditionType::Always}, {u"axisAbove"_qs, AutomationConditionType::AxisAbove},
        {u"axisBelow"_qs, AutomationConditionType::AxisBelow}, {u"axisBetween"_qs, AutomationConditionType::AxisBetween},
        {u"axisOutsideRange"_qs, AutomationConditionType::AxisOutsideRange}, {u"buttonHeld"_qs, AutomationConditionType::ButtonHeld},
        {u"buttonReleased"_qs, AutomationConditionType::ButtonReleased}, {u"povActive"_qs, AutomationConditionType::PovActive},
        {u"povInactive"_qs, AutomationConditionType::PovInactive}, {u"baseProfileIs"_qs, AutomationConditionType::BaseProfileIs},
        {u"effectiveProfileIs"_qs, AutomationConditionType::EffectiveProfileIs},
        {u"buttonPressed"_qs, AutomationConditionType::ButtonPressed},
        {u"buttonReleaseEvent"_qs, AutomationConditionType::ButtonReleaseEvent},
        {u"buttonMultiPress"_qs, AutomationConditionType::ButtonMultiPress},
        {u"buttonLongPress"_qs, AutomationConditionType::ButtonLongPress},
        {u"axisCrossesAbove"_qs, AutomationConditionType::AxisCrossesAbove},
        {u"axisCrossesBelow"_qs, AutomationConditionType::AxisCrossesBelow},
    }};
    for (const auto &[key, candidate] : choices) if (normalized == key) { *type = candidate; return true; }
    return false;
}

QString automationActionTypeToString(AutomationActionType type)
{
    switch (type) {
    case AutomationActionType::VJoyButtonHold: return u"vJoyButtonHold"_qs;
    case AutomationActionType::VJoyButtonToggle: return u"vJoyButtonToggle"_qs;
    case AutomationActionType::ProfileHold: return u"profileHold"_qs;
    case AutomationActionType::ProfileToggle: return u"profileToggle"_qs;
    case AutomationActionType::AxisScale: return u"axisScale"_qs;
    case AutomationActionType::AxisOffset: return u"axisOffset"_qs;
    case AutomationActionType::AxisClamp: return u"axisClamp"_qs;
    case AutomationActionType::AxisOverride: return u"axisOverride"_qs;
    case AutomationActionType::AxisMix: return u"axisMix"_qs;
    case AutomationActionType::AxisFollow: return u"axisFollow"_qs;
    case AutomationActionType::VJoyButtonTap: return u"vJoyButtonTap"_qs;
    }
    return {};
}

bool automationActionTypeFromString(const QString &value, AutomationActionType *type)
{
    if (!type) return false;
    const QString normalized = value.trimmed();
    const std::array<std::pair<QString, AutomationActionType>, 11> choices{{
        {u"vJoyButtonHold"_qs, AutomationActionType::VJoyButtonHold}, {u"vJoyButtonToggle"_qs, AutomationActionType::VJoyButtonToggle},
        {u"profileHold"_qs, AutomationActionType::ProfileHold}, {u"profileToggle"_qs, AutomationActionType::ProfileToggle},
        {u"axisScale"_qs, AutomationActionType::AxisScale}, {u"axisOffset"_qs, AutomationActionType::AxisOffset},
        {u"axisClamp"_qs, AutomationActionType::AxisClamp}, {u"axisOverride"_qs, AutomationActionType::AxisOverride},
        {u"axisMix"_qs, AutomationActionType::AxisMix}, {u"axisFollow"_qs, AutomationActionType::AxisFollow},
        {u"vJoyButtonTap"_qs, AutomationActionType::VJoyButtonTap},
    }};
    for (const auto &[key, candidate] : choices) if (normalized == key) { *type = candidate; return true; }
    return false;
}

QJsonObject automationConditionToJson(const AutomationConditionDefinition &condition)
{
    return {{u"type"_qs, automationConditionTypeToString(condition.type)}, {u"axis"_qs, condition.axis},
            {u"minimum"_qs, condition.minimum}, {u"maximum"_qs, condition.maximum},
            {u"hysteresis"_qs, condition.hysteresis}, {u"button"_qs, condition.button},
            {u"povHat"_qs, condition.povHat}, {u"povDirection"_qs, static_cast<int>(condition.povDirection)},
            {u"profileId"_qs, condition.profileId}, {u"pressCount"_qs, condition.pressCount},
            {u"multiPressWindowMs"_qs, condition.multiPressWindowMs},
            {u"longPressDurationMs"_qs, condition.longPressDurationMs}};
}

bool automationConditionFromJson(const QJsonObject &json, AutomationConditionDefinition *condition)
{
    if (!condition || !automationConditionTypeFromString(json.value(u"type"_qs).toString(), &condition->type)) return false;
    condition->axis = json.value(u"axis"_qs).toInt(static_cast<int>(PhysicalAxis::X));
    condition->minimum = static_cast<float>(json.value(u"minimum"_qs).toDouble());
    condition->maximum = static_cast<float>(json.value(u"maximum"_qs).toDouble());
    condition->hysteresis = static_cast<float>(json.value(u"hysteresis"_qs).toDouble());
    condition->button = json.value(u"button"_qs).toInt(1);
    condition->povHat = json.value(u"povHat"_qs).toInt(1);
    condition->povDirection = static_cast<PovDirection>(json.value(u"povDirection"_qs).toInt(static_cast<int>(PovDirection::Up)));
    condition->profileId = json.value(u"profileId"_qs).toString().trimmed().left(96);
    condition->pressCount = json.value(u"pressCount"_qs).toInt(2);
    condition->multiPressWindowMs = json.value(u"multiPressWindowMs"_qs).toInt(350);
    condition->longPressDurationMs = json.value(u"longPressDurationMs"_qs).toInt(600);
    return std::isfinite(condition->minimum) && std::isfinite(condition->maximum)
        && std::isfinite(condition->hysteresis)
        && condition->pressCount >= 2 && condition->pressCount <= 5
        && condition->multiPressWindowMs >= kAutomationMinimumMultiPressWindowMs
        && condition->multiPressWindowMs <= kAutomationMaximumMultiPressWindowMs
        && condition->longPressDurationMs >= kAutomationMinimumLongPressDurationMs
        && condition->longPressDurationMs <= kAutomationMaximumLongPressDurationMs;
}

QJsonObject automationActionToJson(const AutomationActionDefinition &action)
{
    return {{u"type"_qs, automationActionTypeToString(action.type)}, {u"virtualButton"_qs, action.virtualButton},
            {u"profileId"_qs, action.profileId}, {u"targetAxis"_qs, action.targetAxis},
            {u"sourceAxis"_qs, action.sourceAxis}, {u"sourceStage"_qs, static_cast<int>(action.sourceStage)},
            {u"value"_qs, action.value}, {u"offset"_qs, action.offset},
            {u"minimum"_qs, action.minimum}, {u"maximum"_qs, action.maximum},
            {u"tapDurationMs"_qs, action.tapDurationMs}};
}

bool automationActionFromJson(const QJsonObject &json, AutomationActionDefinition *action)
{
    if (!action || !automationActionTypeFromString(json.value(u"type"_qs).toString(), &action->type)) return false;
    action->virtualButton = json.value(u"virtualButton"_qs).toInt(1);
    action->profileId = json.value(u"profileId"_qs).toString().trimmed().left(96);
    action->targetAxis = json.value(u"targetAxis"_qs).toInt(static_cast<int>(PhysicalAxis::X));
    action->sourceAxis = json.value(u"sourceAxis"_qs).toInt(static_cast<int>(PhysicalAxis::X));
    action->sourceStage = static_cast<AutomationAxisSourceStage>(json.value(u"sourceStage"_qs)
        .toInt(static_cast<int>(AutomationAxisSourceStage::Processed)));
    action->value = static_cast<float>(json.value(u"value"_qs).toDouble());
    action->offset = static_cast<float>(json.value(u"offset"_qs).toDouble());
    action->minimum = static_cast<float>(json.value(u"minimum"_qs).toDouble(-1.0));
    action->maximum = static_cast<float>(json.value(u"maximum"_qs).toDouble(1.0));
    action->tapDurationMs = json.value(u"tapDurationMs"_qs).toInt(80);
    return std::isfinite(action->value) && std::isfinite(action->offset)
        && std::isfinite(action->minimum) && std::isfinite(action->maximum)
        && action->tapDurationMs >= kAutomationMinimumTapDurationMs
        && action->tapDurationMs <= kAutomationMaximumTapDurationMs;
}

QJsonObject automationToJson(const AutomationDefinition &automation)
{
    QJsonArray conditions;
    for (const AutomationConditionDefinition &condition : automation.conditions) conditions.append(automationConditionToJson(condition));
    QJsonArray actions;
    for (const AutomationActionDefinition &action : automation.actions) actions.append(automationActionToJson(action));
    return {{u"id"_qs, automation.id}, {u"name"_qs, automation.name}, {u"enabled"_qs, automation.enabled},
            {u"matchMode"_qs, automationMatchModeToString(automation.matchMode)},
            {u"activationMode"_qs, automationActivationModeToString(automation.activationMode)},
            {u"activeDurationMs"_qs, automation.activeDurationMs}, {u"priority"_qs, automation.priority},
            {u"conditions"_qs, conditions}, {u"actions"_qs, actions}};
}

bool automationFromJson(const QJsonObject &json, AutomationDefinition *automation)
{
    if (!automation) return false;
    AutomationDefinition restored;
    restored.id = json.value(u"id"_qs).toString().trimmed();
    restored.name = json.value(u"name"_qs).toString().trimmed();
    restored.enabled = json.value(u"enabled"_qs).toBool(true);
    restored.priority = std::clamp(json.value(u"priority"_qs).toInt(50), 0, 100);
    restored.activeDurationMs = json.value(u"activeDurationMs"_qs).toInt(250);
    if (restored.id.isEmpty() || restored.id.size() > 96 || restored.name.size() > 64
        || !automationMatchModeFromString(json.value(u"matchMode"_qs).toString(), &restored.matchMode)
        || !automationActivationModeFromString(json.value(u"activationMode"_qs)
            .toString(u"whileTriggerActive"_qs), &restored.activationMode)
        || restored.activeDurationMs < kAutomationMinimumRuleActiveDurationMs
        || restored.activeDurationMs > kAutomationMaximumRuleActiveDurationMs) return false;
    const QJsonArray conditions = json.value(u"conditions"_qs).toArray();
    const QJsonArray actions = json.value(u"actions"_qs).toArray();
    const bool emptyDraft = conditions.empty() && actions.empty();
    // The editor may persist a brand-new, disabled draft before it has any
    // runtime meaning. This is the one deliberately incomplete shape that is
    // accepted; all publishable rules still require one to four of each.
    if (conditions.size() > kMaximumAutomationConditions
        || actions.size() > kMaximumAutomationActions
        || (emptyDraft ? restored.enabled : (conditions.empty() || actions.empty()))) return false;
    for (const QJsonValue &value : conditions) {
        AutomationConditionDefinition condition;
        if (!automationConditionFromJson(value.toObject(), &condition)) return false;
        restored.conditions.push_back(std::move(condition));
    }
    for (const QJsonValue &value : actions) {
        AutomationActionDefinition action;
        if (!automationActionFromJson(value.toObject(), &action)) return false;
        restored.actions.push_back(std::move(action));
    }
    *automation = std::move(restored);
    return true;
}

QJsonArray buttonBindingsToJson(const ButtonBindings &bindings)
{
    QJsonArray buttons;
    for (const ButtonBinding &binding : bindings) buttons.append(buttonBindingToJson(binding));
    return buttons;
}

ButtonBindings buttonBindingsFromJson(const QJsonValue &value)
{
    ButtonBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray buttons = value.toArray();
    const int count = std::min(static_cast<int>(buttons.size()), kMaximumPhysicalButtons);
    bindings.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        bindings.push_back(buttonBindingFromJson(buttons.at(index).toObject()));
    }
    normalizeButtonMappings(bindings, kMaximumVirtualButtons);
    return bindings;
}

QJsonArray povBindingsToJson(const PovBindings &bindings)
{
    QJsonArray hats;
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalPovs);
    for (int hat = 0; hat < count; ++hat) {
        QJsonArray directions;
        for (const ButtonBinding &binding : bindings[static_cast<size_t>(hat)]) {
            directions.append(buttonBindingToJson(binding));
        }
        hats.append(directions);
    }
    return hats;
}

PovBindings povBindingsFromJson(const QJsonValue &value)
{
    PovBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray hats = value.toArray();
    const int count = std::min(static_cast<int>(hats.size()), kMaximumPhysicalPovs);
    bindings.resize(static_cast<size_t>(count));
    for (int hat = 0; hat < count; ++hat) {
        const QJsonArray directions = hats.at(hat).toArray();
        if (directions.size() != kPovDirectionCount) {
            bindings[static_cast<size_t>(hat)] = {};
            continue;
        }
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            bindings[static_cast<size_t>(hat)][static_cast<size_t>(direction)] =
                buttonBindingFromJson(directions.at(direction).toObject());
        }
    }
    return bindings;
}

QJsonObject profileTriggerToJson(const ProfileTriggerBinding &binding)
{
    QString mode = u"disabled"_qs;
    if (binding.mode == ProfileTriggerMode::Hold) mode = u"hold"_qs;
    if (binding.mode == ProfileTriggerMode::Toggle) mode = u"toggle"_qs;
    return {{u"mode"_qs, mode}, {u"targetProfileId"_qs, binding.targetProfileId}};
}

ProfileTriggerBinding profileTriggerFromJson(const QJsonObject &json)
{
    ProfileTriggerBinding binding;
    binding.mode = profileTriggerModeFromString(json.value(u"mode"_qs).toString());
    binding.targetProfileId = json.value(u"targetProfileId"_qs).toString().trimmed().left(96);
    if (!profileTriggerBindingEnabled(binding)) return {};
    return binding;
}

QJsonArray profileTriggersToJson(const ProfileTriggerBindings &bindings)
{
    QJsonArray triggers;
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalButtons);
    for (int index = 0; index < count; ++index) {
        triggers.append(profileTriggerToJson(bindings[static_cast<size_t>(index)]));
    }
    return triggers;
}

ProfileTriggerBindings profileTriggersFromJson(const QJsonValue &value)
{
    ProfileTriggerBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray triggers = value.toArray();
    const int count = std::min(static_cast<int>(triggers.size()), kMaximumPhysicalButtons);
    bindings.resize(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        bindings[static_cast<size_t>(index)] = profileTriggerFromJson(triggers.at(index).toObject());
    }
    return bindings;
}

QJsonArray povProfileTriggersToJson(const PovProfileTriggerBindings &bindings)
{
    QJsonArray hats;
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalPovs);
    for (int hat = 0; hat < count; ++hat) {
        QJsonArray directions;
        for (const ProfileTriggerBinding &binding : bindings[static_cast<size_t>(hat)]) {
            directions.append(profileTriggerToJson(binding));
        }
        hats.append(directions);
    }
    return hats;
}

PovProfileTriggerBindings povProfileTriggersFromJson(const QJsonValue &value)
{
    PovProfileTriggerBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray hats = value.toArray();
    const int count = std::min(static_cast<int>(hats.size()), kMaximumPhysicalPovs);
    bindings.resize(static_cast<size_t>(count));
    for (int hat = 0; hat < count; ++hat) {
        const QJsonArray directions = hats.at(hat).toArray();
        if (directions.size() != kPovDirectionCount) continue;
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            bindings[static_cast<size_t>(hat)][static_cast<size_t>(direction)] =
                profileTriggerFromJson(directions.at(direction).toObject());
        }
    }
    return bindings;
}

QJsonObject nativePovBindingToJson(const NativePovBinding &binding)
{
    QString targetType = u"disabled"_qs;
    if (binding.targetType == NativePovTargetType::Continuous) targetType = u"continuous"_qs;
    if (binding.targetType == NativePovTargetType::Discrete) targetType = u"discrete"_qs;
    return {{u"enabled"_qs, binding.enabled}, {u"targetType"_qs, targetType},
            {u"targetIndex"_qs, binding.targetIndex}};
}

NativePovBinding nativePovBindingFromJson(const QJsonObject &json)
{
    NativePovBinding binding;
    const QString targetType = json.value(u"targetType"_qs).toString().trimmed();
    if (targetType.compare(u"continuous"_qs, Qt::CaseInsensitive) == 0) {
        binding.targetType = NativePovTargetType::Continuous;
    } else if (targetType.compare(u"discrete"_qs, Qt::CaseInsensitive) == 0) {
        binding.targetType = NativePovTargetType::Discrete;
    }
    binding.enabled = json.value(u"enabled"_qs).toBool(false);
    binding.targetIndex = std::clamp(json.value(u"targetIndex"_qs).toInt(), 0, 32);
    if (!binding.enabled || binding.targetType == NativePovTargetType::Disabled
        || binding.targetIndex <= 0) return {};
    return binding;
}

QJsonArray nativePovBindingsToJson(const NativePovBindings &bindings)
{
    QJsonArray hats;
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalPovs);
    for (int hat = 0; hat < count; ++hat) {
        hats.append(nativePovBindingToJson(bindings[static_cast<size_t>(hat)]));
    }
    return hats;
}

NativePovBindings nativePovBindingsFromJson(const QJsonValue &value)
{
    NativePovBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray hats = value.toArray();
    const int count = std::min(static_cast<int>(hats.size()), kMaximumPhysicalPovs);
    bindings.resize(static_cast<size_t>(count));
    for (int hat = 0; hat < count; ++hat) {
        NativePovBinding candidate = nativePovBindingFromJson(hats.at(hat).toObject());
        if (!candidate.enabled) continue;
        bool duplicate = false;
        for (int previous = 0; previous < hat; ++previous) {
            const NativePovBinding &existing = bindings[static_cast<size_t>(previous)];
            duplicate = duplicate || (existing.enabled && existing.targetType == candidate.targetType
                && existing.targetIndex == candidate.targetIndex);
        }
        if (!duplicate) bindings[static_cast<size_t>(hat)] = candidate;
    }
    return bindings;
}

QJsonObject profileToJson(const ControllerProfile &profile)
{
    QJsonArray axes;
    for (const AxisMapping &mapping : profile.axes) axes.append(axisMappingToJson(mapping));
    return {
        {u"id"_qs, profile.id},
        {u"name"_qs, profile.name},
        {u"axes"_qs, axes},
        {u"buttons"_qs, buttonBindingsToJson(profile.buttons)},
        {u"povs"_qs, povBindingsToJson(profile.povs)},
    };
}

bool profileFromJson(const QJsonObject &json, ControllerProfile *profile)
{
    if (!profile) return false;
    const QString id = json.value(u"id"_qs).toString().trimmed();
    const QString name = json.value(u"name"_qs).toString().trimmed();
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    if (id.isEmpty() || id.size() > 96 || !isProfileNameValid(name)
        || axes.size() != kPhysicalAxisCount) {
        return false;
    }

    ControllerProfile restored;
    restored.id = id;
    restored.name = name;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QJsonObject axis = axes.at(index).toObject();
        if (axis.isEmpty()) return false;
        restored.axes[index] = axisMappingFromJson(axis, isUnipolarAxis(static_cast<PhysicalAxis>(index)));
    }
    normalizeMappingConflicts(restored.axes);
    restored.buttons = buttonBindingsFromJson(json.value(u"buttons"_qs));
    restored.povs = povBindingsFromJson(json.value(u"povs"_qs));
    normalizePovMappings(restored.povs, restored.buttons, kMaximumVirtualButtons);
    *profile = std::move(restored);
    return true;
}

MapperConfiguration migrateLegacyConfiguration(const QJsonObject &json, int version, bool *valid)
{
    MapperConfiguration configuration = defaultConfiguration();
    readGlobalSettings(json, configuration);
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    if (axes.size() != kPhysicalAxisCount) {
        if (valid) *valid = false;
        return defaultConfiguration();
    }

    ControllerProfile &normal = activeProfile(configuration);
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QJsonObject axis = axes.at(index).toObject();
        if (axis.isEmpty()) {
            if (valid) *valid = false;
            return defaultConfiguration();
        }
        normal.axes[index] = axisMappingFromJson(axis, isUnipolarAxis(static_cast<PhysicalAxis>(index)));
        configuration.calibration[index] = calibrationFromJson(axis.value(u"calibration"_qs).toObject());
    }
    normalizeMappingConflicts(normal.axes);
    if (version >= 2) {
        // An absent or malformed legacy button list was tolerated in v1.1;
        // retain that safe behavior while preserving all valid axis routes.
        normal.buttons = buttonBindingsFromJson(json.value(u"buttons"_qs));
    }
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    *precision = normal;
    precision->id = precisionProfileId();
    precision->name = u"Precision"_qs;
    configuration.activeProfileId = normalProfileId();
    if (valid) *valid = true;
    return configuration;
}

MapperConfiguration fallbackWithGlobalSettings(const QJsonObject &json)
{
    MapperConfiguration fallback = defaultConfiguration();
    readGlobalSettings(json, fallback);
    return fallback;
}

} // namespace

MapperConfiguration ConfigStore::load()
{
    const QSettings stored(settingsFilePath(), QSettings::IniFormat);
    const QByteArray encoded = stored.value(QLatin1String(kConfigKey)).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isObject()) return defaultConfiguration();

    bool valid = false;
    MapperConfiguration configuration = fromJson(document.object(), &valid);
    if (!valid) return configuration;
    if (document.object().value(u"version"_qs).toInt() < kProfileSchemaVersion) {
        // Write the deterministic migration immediately, making every later
        // launch read the profile schema without duplicate default creation.
        save(configuration);
    }
    return configuration;
}

bool ConfigStore::save(const MapperConfiguration &configuration)
{
    QSettings stored(settingsFilePath(), QSettings::IniFormat);
    stored.setValue(QLatin1String(kConfigKey), QJsonDocument(toJson(configuration)).toJson(QJsonDocument::Compact));
    stored.sync();
    return stored.status() == QSettings::NoError;
}

QJsonObject ConfigStore::toJson(const MapperConfiguration &configuration)
{
    QJsonArray calibration;
    for (const Calibration &axis : configuration.calibration) calibration.append(calibrationToJson(axis));

    QJsonArray profiles;
    for (const ControllerProfile &profile : configuration.profiles) profiles.append(profileToJson(profile));

    QJsonArray personalCurvePresets;
    for (const PersonalCurvePreset &preset : configuration.personalCurvePresets) {
        personalCurvePresets.append(personalCurvePresetToJson(preset));
    }
    QJsonArray automations;
    const int automationCount = std::min(static_cast<int>(configuration.automations.size()),
                                         kMaximumAutomationRules);
    for (int index = 0; index < automationCount; ++index) {
        automations.append(automationToJson(configuration.automations[static_cast<size_t>(index)]));
    }

    return {
        {u"version"_qs, kProfileSchemaVersion},
        {u"preferredDeviceId"_qs, configuration.preferredDeviceId},
        {u"vjoyDeviceId"_qs, configuration.vjoyDeviceId},
        {u"startMappingOnLaunch"_qs, configuration.startMappingOnLaunch},
        {u"disabledAxisValue"_qs, sanitizedDisabledAxisValue(configuration.disabledAxisValue)},
        {u"selectedAxisIndex"_qs, configuration.selectedAxisIndex},
        {u"calibration"_qs, calibration},
        {u"profiles"_qs, profiles},
        {u"personalCurvePresets"_qs, personalCurvePresets},
        {u"profileTriggers"_qs, profileTriggersToJson(configuration.profileTriggers)},
        {u"povProfileTriggers"_qs, povProfileTriggersToJson(configuration.povProfileTriggers)},
        {u"nativePovBindings"_qs, nativePovBindingsToJson(configuration.nativePovBindings)},
        {u"automationEnabled"_qs, configuration.automationEnabled},
        {u"automations"_qs, automations},
        {u"activeProfileId"_qs, configuration.activeProfileId},
    };
}

MapperConfiguration ConfigStore::fromJson(const QJsonObject &json, bool *valid)
{
    const int version = json.value(u"version"_qs).toInt();
    if (version == 1 || version == 2) return migrateLegacyConfiguration(json, version, valid);
    if (version != 3 && version != 4 && version != 5 && version != 6 && version != 7 && version != 8
        && version != 9 && version != 10 && version != 11 && version != kProfileSchemaVersion) {
        if (valid) *valid = false;
        return fallbackWithGlobalSettings(json);
    }

    MapperConfiguration configuration = fallbackWithGlobalSettings(json);
    const QJsonArray calibration = json.value(u"calibration"_qs).toArray();
    const QJsonArray profiles = json.value(u"profiles"_qs).toArray();
    if (calibration.size() != kPhysicalAxisCount || profiles.empty()) {
        if (valid) *valid = false;
        return configuration;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QJsonObject axis = calibration.at(index).toObject();
        if (axis.isEmpty()) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        configuration.calibration[index] = calibrationFromJson(axis);
    }

    configuration.profiles.clear();
    QSet<QString> ids;
    QSet<QString> names;
    for (const QJsonValue &value : profiles) {
        ControllerProfile profile;
        if (!profileFromJson(value.toObject(), &profile)
            || ids.contains(profile.id) || names.contains(profile.name.toCaseFolded())) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        ids.insert(profile.id);
        names.insert(profile.name.toCaseFolded());
        configuration.profiles.push_back(std::move(profile));
    }

    const QJsonArray personalPresets = json.value(u"personalCurvePresets"_qs).toArray();
    if (personalPresets.size() > 128) {
        if (valid) *valid = false;
        return fallbackWithGlobalSettings(json);
    }
    QSet<QString> presetIds;
    QSet<QString> presetNames;
    for (const QJsonValue &value : personalPresets) {
        PersonalCurvePreset preset;
        if (!personalCurvePresetFromJson(value.toObject(), &preset)
            || presetIds.contains(preset.id)
            || presetNames.contains(preset.name.toCaseFolded())) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        presetIds.insert(preset.id);
        presetNames.insert(preset.name.toCaseFolded());
        configuration.personalCurvePresets.push_back(std::move(preset));
    }

    if (version < kUniversalStrengthSchemaVersion) {
        // v1.4 before the universal-strength amendment evaluated Advanced,
        // Personal, and point curves at their full definition regardless of
        // the stored (default-zero) strength field. Preserve that effective
        // response when migrating to the explicit blend model.
        const auto migrateCurveStrength = [](CurveDefinition &curve) {
            if (curve.family == CurveFamily::Advanced || curve.family == CurveFamily::Personal
                || curve.family == CurveFamily::Custom || curve.pointEditing) {
                curve.strength = 1.0F;
            }
        };
        for (ControllerProfile &profile : configuration.profiles) {
            for (AxisMapping &axis : profile.axes) migrateCurveStrength(axis.curve);
        }
        for (PersonalCurvePreset &preset : configuration.personalCurvePresets) {
            migrateCurveStrength(preset.definition);
        }
    }

    // v1.5 introduces global profile controls. Absence in a v1.4-or-earlier
    // record is intentionally equivalent to no configured controls.
    if (version >= 8) {
        configuration.profileTriggers = profileTriggersFromJson(json.value(u"profileTriggers"_qs));
    }
    // v1.6.2 adds POV profile controls and native vJoy POV passthrough. Older
    // installations migrate with all new controls safely disabled.
    if (version >= 10) {
        configuration.povProfileTriggers = povProfileTriggersFromJson(json.value(u"povProfileTriggers"_qs));
        configuration.nativePovBindings = nativePovBindingsFromJson(json.value(u"nativePovBindings"_qs));
    }
    // v1.8 adds global Automation with an ON/empty migration. Definitions
    // may intentionally reference a deleted profile or unavailable device;
    // the compiler preserves and marks those rules for repair.
    if (version >= 11) {
        const QJsonArray automations = json.value(u"automations"_qs).toArray();
        if (automations.size() > kMaximumAutomationRules) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        QSet<QString> automationIds;
        for (const QJsonValue &value : automations) {
            AutomationDefinition automation;
            if (!automationFromJson(value.toObject(), &automation) || automationIds.contains(automation.id)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            automationIds.insert(automation.id);
            configuration.automations.push_back(std::move(automation));
        }
    } else {
        configuration.automationEnabled = true;
        configuration.automations.clear();
    }

    // Normal is the durable, protected recovery profile. A malformed/manual
    // file that removes it falls back safely instead of leaving no known base.
    const ControllerProfile *normal = findProfile(configuration, normalProfileId());
    if (!normal || normal->name.compare(u"Normal"_qs, Qt::CaseInsensitive) != 0) {
        if (valid) *valid = false;
        return fallbackWithGlobalSettings(json);
    }
    configuration.activeProfileId = json.value(u"activeProfileId"_qs).toString();
    if (!findProfile(configuration, configuration.activeProfileId)) {
        configuration.activeProfileId = normalProfileId();
    }
    if (valid) *valid = true;
    return configuration;
}

} // namespace hotas
