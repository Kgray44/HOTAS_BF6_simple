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

namespace hotas {
namespace {

constexpr auto kConfigKey = "mapper/config";
constexpr int kProfileSchemaVersion = 9;
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
    configuration.selectedAxisIndex = std::clamp(json.value(u"selectedAxisIndex"_qs)
        .toInt(static_cast<int>(PhysicalAxis::X)), 0, kPhysicalAxisCount - 1);
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

    return {
        {u"version"_qs, kProfileSchemaVersion},
        {u"preferredDeviceId"_qs, configuration.preferredDeviceId},
        {u"vjoyDeviceId"_qs, configuration.vjoyDeviceId},
        {u"startMappingOnLaunch"_qs, configuration.startMappingOnLaunch},
        {u"selectedAxisIndex"_qs, configuration.selectedAxisIndex},
        {u"calibration"_qs, calibration},
        {u"profiles"_qs, profiles},
        {u"personalCurvePresets"_qs, personalCurvePresets},
        {u"profileTriggers"_qs, profileTriggersToJson(configuration.profileTriggers)},
        {u"activeProfileId"_qs, configuration.activeProfileId},
    };
}

MapperConfiguration ConfigStore::fromJson(const QJsonObject &json, bool *valid)
{
    const int version = json.value(u"version"_qs).toInt();
    if (version == 1 || version == 2) return migrateLegacyConfiguration(json, version, valid);
    if (version != 3 && version != 4 && version != 5 && version != 6 && version != 7 && version != 8
        && version != kProfileSchemaVersion) {
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
