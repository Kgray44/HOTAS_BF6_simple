#include "config_store.h"

#include "axis_transform.h"
#include "button_mapping.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace hotas {
namespace {

constexpr auto kConfigKey = "mapper/config";

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

QJsonObject buttonBindingToJson(const ButtonBinding &binding)
{
    if (binding.type == ButtonActionType::VirtualButton) {
        return {{u"type"_qs, u"virtualButton"_qs}, {u"target"_qs, binding.target}};
    }
    return {{u"type"_qs, u"disabled"_qs}};
}

ButtonBinding buttonBindingFromJson(const QJsonObject &json)
{
    if (json.value(u"type"_qs).toString().trimmed().compare(u"virtualButton"_qs, Qt::CaseInsensitive) == 0) {
        return {ButtonActionType::VirtualButton, json.value(u"target"_qs).toInt(0)};
    }
    return {};
}

} // namespace

MapperConfiguration ConfigStore::load()
{
    const QSettings stored(settingsFilePath(), QSettings::IniFormat);
    const QByteArray encoded = stored.value(QLatin1String(kConfigKey)).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isObject()) {
        return defaultConfiguration();
    }
    bool valid = false;
    MapperConfiguration configuration = fromJson(document.object(), &valid);
    return valid ? configuration : defaultConfiguration();
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
    QJsonArray axes;
    for (const auto &mapping : configuration.axes) {
        axes.append(QJsonObject{
            {u"target"_qs, virtualAxisLabel(mapping.target)},
            {u"inverted"_qs, mapping.inverted},
            {u"deadzone"_qs, mapping.deadzone},
            {u"calibration"_qs, calibrationToJson(mapping.calibration)},
        });
    }

    QJsonArray buttons;
    for (const ButtonBinding &binding : configuration.buttons) {
        buttons.append(buttonBindingToJson(binding));
    }

    return {
        {u"version"_qs, 2},
        {u"preferredDeviceId"_qs, configuration.preferredDeviceId},
        {u"vjoyDeviceId"_qs, configuration.vjoyDeviceId},
        {u"startMappingOnLaunch"_qs, configuration.startMappingOnLaunch},
        {u"axes"_qs, axes},
        {u"buttons"_qs, buttons},
    };
}

MapperConfiguration ConfigStore::fromJson(const QJsonObject &json, bool *valid)
{
    MapperConfiguration configuration = defaultConfiguration();
    const int version = json.value(u"version"_qs).toInt();
    bool result = version == 1 || version == 2;
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    result = result && axes.size() == kPhysicalAxisCount;
    if (!result) {
        if (valid) *valid = false;
        return defaultConfiguration();
    }

    configuration.preferredDeviceId = json.value(u"preferredDeviceId"_qs).toString();
    configuration.vjoyDeviceId = std::clamp(json.value(u"vjoyDeviceId"_qs).toInt(1), 1, 16);
    configuration.startMappingOnLaunch = json.value(u"startMappingOnLaunch"_qs).toBool(false);
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QJsonObject axis = axes.at(index).toObject();
        if (axis.isEmpty()) {
            result = false;
            break;
        }
        auto &mapping = configuration.axes[index];
        mapping.target = virtualAxisFromString(axis.value(u"target"_qs).toString());
        mapping.inverted = axis.value(u"inverted"_qs).toBool(false);
        mapping.deadzone = std::clamp(float(axis.value(u"deadzone"_qs).toDouble(0.03)), 0.0F, 0.95F);
        mapping.calibration = calibrationFromJson(axis.value(u"calibration"_qs).toObject());
    }
    // Version 1 had no button configuration. Leaving this empty deliberately
    // lets the worker create a first-detected passthrough without touching axes.
    if (result && version >= 2) {
        const QJsonValue buttonsValue = json.value(u"buttons"_qs);
        if (buttonsValue.isArray()) {
            const QJsonArray buttons = buttonsValue.toArray();
            const int count = std::min(static_cast<int>(buttons.size()), kMaximumPhysicalButtons);
            configuration.buttons.reserve(static_cast<size_t>(count));
            for (int index = 0; index < count; ++index) {
                configuration.buttons.push_back(buttonBindingFromJson(buttons.at(index).toObject()));
            }
            // Bad individual button values are safely disabled rather than
            // discarding the existing axis configuration.
            normalizeButtonMappings(configuration.buttons, kMaximumVirtualButtons);
        }
    }
    if (!normalizeMappingConflicts(configuration)) {
        result = false;
    }
    if (valid) *valid = result;
    return result ? configuration : defaultConfiguration();
}

} // namespace hotas
