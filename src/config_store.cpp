#include "config_store.h"

#include "axis_transform.h"
#include "adaptive_response.h"
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
constexpr int kProfileSchemaVersion = 21;
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
        {u"centered"_qs, calibration.centered},
    };
}

Calibration calibrationFromJson(const QJsonObject &json)
{
    Calibration calibration;
    calibration.enabled = json.value(u"enabled"_qs).toBool(false);
    calibration.minimum = std::clamp(float(json.value(u"minimum"_qs).toDouble(-1.0)), -1.0F, 1.0F);
    calibration.center = std::clamp(float(json.value(u"center"_qs).toDouble(0.0)), -1.0F, 1.0F);
    calibration.maximum = std::clamp(float(json.value(u"maximum"_qs).toDouble(1.0)), -1.0F, 1.0F);
    calibration.centered = json.value(u"centered"_qs).toBool(true);
    const bool valid = calibration.centered
        ? calibration.minimum < calibration.center && calibration.center < calibration.maximum
        : calibration.minimum < calibration.maximum;
    if (!valid) {
        calibration.enabled = false;
        calibration.minimum = -1.0F;
        calibration.center = 0.0F;
        calibration.maximum = 1.0F;
        calibration.centered = true;
    }
    return calibration;
}

QJsonObject adaptiveResponseSettingsToJson(const AdaptiveResponseSettings &settings)
{
    const AdaptiveResponseSettings clean = sanitizedAdaptiveResponseSettings(settings);
    return {{u"enabled"_qs, clean.enabled}, {u"model"_qs, adaptiveResponseModelKey(clean.model)},
            {u"maximumHorizonMs"_qs, clean.maximumHorizonMs}, {u"maximumLead"_qs, clean.maximumLead},
            {u"velocityResponse"_qs, clean.velocityResponse},
            {u"accelerationResponse"_qs, clean.accelerationResponse},
            {u"motionSensitivity"_qs, clean.motionSensitivity}, {u"noiseRejection"_qs, clean.noiseRejection},
            {u"reversalDetection"_qs, clean.reversalDetection},
            {u"reversalResponse"_qs, clean.reversalResponse},
            {u"decelerationResponse"_qs, clean.decelerationResponse},
            {u"settlingResponse"_qs, clean.settlingResponse}, {u"endpointTaper"_qs, clean.endpointTaper},
            {u"onsetAssist"_qs, clean.onsetAssist}, {u"onsetCap"_qs, clean.onsetCap},
            {u"sustainedAssist"_qs, clean.sustainedAssist}, {u"sustainedCap"_qs, clean.sustainedCap},
            {u"horizonExtension"_qs, clean.horizonExtension},
            {u"horizonExtensionCapMs"_qs, clean.horizonExtensionCapMs},
            {u"turningPointProtection"_qs, clean.turningPointProtection},
            {u"turningPointMargin"_qs, clean.turningPointMargin}};
}

bool adaptiveResponseSettingsFromJson(const QJsonObject &json, AdaptiveResponseSettings *settings)
{
    if (!settings || json.isEmpty()) return false;
    const QString model = json.value(u"model"_qs).toString();
    const QString normalizedModel = model.trimmed().toCaseFolded();
    if (normalizedModel != u"auto"_qs && normalizedModel != u"velocity"_qs
        && normalizedModel != u"alpha-beta"_qs && normalizedModel != u"alpha-beta-gamma"_qs
        && normalizedModel != u"abg"_qs) return false;
    AdaptiveResponseSettings restored;
    restored.enabled = json.value(u"enabled"_qs).toBool(false);
    restored.model = adaptiveResponseModelFromKey(model);
    restored.maximumHorizonMs = static_cast<float>(json.value(u"maximumHorizonMs"_qs).toDouble(8.0));
    restored.maximumLead = static_cast<float>(json.value(u"maximumLead"_qs).toDouble(0.12));
    restored.velocityResponse = static_cast<float>(json.value(u"velocityResponse"_qs).toDouble(0.72));
    restored.accelerationResponse = static_cast<float>(json.value(u"accelerationResponse"_qs).toDouble(0.58));
    restored.motionSensitivity = static_cast<float>(json.value(u"motionSensitivity"_qs).toDouble(0.035));
    restored.noiseRejection = static_cast<float>(json.value(u"noiseRejection"_qs).toDouble(0.012));
    restored.reversalDetection = static_cast<float>(json.value(u"reversalDetection"_qs).toDouble(0.075));
    restored.reversalResponse = static_cast<float>(json.value(u"reversalResponse"_qs).toDouble(1.0));
    restored.decelerationResponse = static_cast<float>(json.value(u"decelerationResponse"_qs).toDouble(0.85));
    restored.settlingResponse = static_cast<float>(json.value(u"settlingResponse"_qs).toDouble(0.92));
    restored.endpointTaper = static_cast<float>(json.value(u"endpointTaper"_qs).toDouble(0.16));
    // Missing keys are pre-Onset-Assist configurations; retain their prior
    // behavior deterministically by disabling the new authority path.
    restored.onsetAssist = static_cast<float>(json.value(u"onsetAssist"_qs).toDouble(0.0));
    restored.onsetCap = static_cast<float>(json.value(u"onsetCap"_qs).toDouble(0.0));
    restored.sustainedAssist = static_cast<float>(json.value(u"sustainedAssist"_qs).toDouble(0.0));
    restored.sustainedCap = static_cast<float>(json.value(u"sustainedCap"_qs).toDouble(0.0));
    restored.horizonExtension = static_cast<float>(json.value(u"horizonExtension"_qs).toDouble(0.0));
    restored.horizonExtensionCapMs = static_cast<float>(json.value(u"horizonExtensionCapMs"_qs).toDouble(0.0));
    restored.turningPointProtection = static_cast<float>(json.value(u"turningPointProtection"_qs).toDouble(0.0));
    restored.turningPointMargin = static_cast<float>(json.value(u"turningPointMargin"_qs).toDouble(0.0));
    *settings = sanitizedAdaptiveResponseSettings(restored);
    return true;
}

QJsonObject adaptiveResponseOverrideToJson(const AdaptiveResponseAxisOverride &override)
{
    return {{u"properties"_qs, static_cast<int>(override.properties & kAdaptiveResponseAllProperties)},
            {u"presetId"_qs, override.presetId.trimmed().left(96)},
            {u"settings"_qs, adaptiveResponseSettingsToJson(override.settings)}};
}

bool adaptiveResponseOverrideFromJson(const QJsonObject &json, AdaptiveResponseAxisOverride *override)
{
    if (!override || json.isEmpty()) return false;
    const int properties = json.value(u"properties"_qs).toInt(-1);
    if (properties < 0 || (static_cast<std::uint32_t>(properties) & ~kAdaptiveResponseAllProperties) != 0) return false;
    AdaptiveResponseAxisOverride restored;
    restored.properties = static_cast<std::uint32_t>(properties);
    restored.presetId = json.value(u"presetId"_qs).toString().trimmed().left(96);
    if (!adaptiveResponseSettingsFromJson(json.value(u"settings"_qs).toObject(), &restored.settings)) return false;
    *override = std::move(restored);
    return true;
}

QJsonObject adaptiveResponseLayerToJson(const AdaptiveResponseLayer &layer)
{
    QJsonArray axes;
    for (const AdaptiveResponseAxisOverride &axis : layer.axes) axes.append(adaptiveResponseOverrideToJson(axis));
    return {{u"axes"_qs, axes}};
}

bool adaptiveResponseLayerFromJson(const QJsonValue &value, AdaptiveResponseLayer *layer)
{
    if (!layer || !value.isObject()) return false;
    const QJsonArray axes = value.toObject().value(u"axes"_qs).toArray();
    if (axes.size() != kPhysicalAxisCount) return false;
    AdaptiveResponseLayer restored;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (!adaptiveResponseOverrideFromJson(axes.at(axis).toObject(),
                                              &restored.axes[static_cast<size_t>(axis)])) return false;
    }
    *layer = std::move(restored);
    return true;
}

QJsonObject adaptiveResponsePresetToJson(const AdaptiveResponsePreset &preset)
{
    QJsonArray axes;
    for (const AdaptiveResponseAxisOverride &axis : preset.axes) axes.append(adaptiveResponseOverrideToJson(axis));
    return {{u"id"_qs, preset.id.trimmed().left(96)}, {u"name"_qs, preset.name.trimmed().left(64)},
            {u"description"_qs, preset.description.trimmed().left(160)}, {u"axes"_qs, axes}};
}

bool adaptiveResponsePresetFromJson(const QJsonObject &json, AdaptiveResponsePreset *preset)
{
    if (!preset) return false;
    AdaptiveResponsePreset restored;
    restored.id = json.value(u"id"_qs).toString().trimmed().left(96);
    restored.name = json.value(u"name"_qs).toString().trimmed().left(64);
    restored.description = json.value(u"description"_qs).toString().trimmed().left(160);
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    if (restored.id.isEmpty() || restored.name.isEmpty() || axes.size() != kPhysicalAxisCount) return false;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (!adaptiveResponseOverrideFromJson(axes.at(axis).toObject(),
                                              &restored.axes[static_cast<size_t>(axis)])) return false;
    }
    *preset = std::move(restored);
    return true;
}

QJsonObject calibrationHistoryEntryToJson(const CalibrationHistoryEntry &entry)
{
    QJsonArray calibration;
    for (const Calibration &axis : entry.calibration) calibration.append(calibrationToJson(axis));
    return {{u"controllerRecordId"_qs, entry.controllerRecordId},
            {u"controllerDisplayName"_qs, entry.controllerDisplayName},
            {u"controllerIdentity"_qs, entry.controllerIdentity},
            {u"completedAtUtc"_qs, entry.completedAtUtc},
            {u"applicationVersion"_qs, entry.applicationVersion},
            {u"calibratedAxisCount"_qs, entry.calibratedAxisCount},
            {u"calibration"_qs, calibration}};
}

bool calibrationHistoryEntryFromJson(const QJsonObject &json, CalibrationHistoryEntry *entry)
{
    if (!entry) return false;
    const QJsonArray calibration = json.value(u"calibration"_qs).toArray();
    CalibrationHistoryEntry restored;
    restored.controllerRecordId = json.value(u"controllerRecordId"_qs).toString().trimmed().left(96);
    restored.controllerDisplayName = json.value(u"controllerDisplayName"_qs).toString().trimmed().left(128);
    restored.controllerIdentity = json.value(u"controllerIdentity"_qs).toString().trimmed().left(256);
    restored.completedAtUtc = json.value(u"completedAtUtc"_qs).toString().trimmed().left(64);
    restored.applicationVersion = json.value(u"applicationVersion"_qs).toString().trimmed().left(32);
    restored.calibratedAxisCount = std::clamp(json.value(u"calibratedAxisCount"_qs).toInt(), 0,
                                               kPhysicalAxisCount);
    if (restored.controllerDisplayName.isEmpty() || restored.completedAtUtc.isEmpty()
        || calibration.size() != kPhysicalAxisCount) return false;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        restored.calibration[static_cast<size_t>(index)] =
            calibrationFromJson(calibration.at(index).toObject());
    }
    *entry = std::move(restored);
    return true;
}

QJsonObject controllerVjoyRequirementsToJson(const ControllerVJoyRequirements &requirements)
{
    QJsonArray axes;
    for (const bool enabled : requirements.axes) axes.append(enabled);
    return {{u"axes"_qs, axes}, {u"buttons"_qs, requirements.buttons},
            {u"continuousPovs"_qs, requirements.continuousPovs},
            {u"discretePovs"_qs, requirements.discretePovs}, {u"deviceId"_qs, requirements.deviceId}};
}

QJsonArray axisActivityToJson(const std::array<PhysicalAxisActivity, kPhysicalAxisCount> &activity)
{
    QJsonArray result;
    for (const PhysicalAxisActivity axis : activity) result.append(physicalAxisActivityKey(axis));
    return result;
}

bool axisActivityFromJson(const QJsonValue &value,
                          std::array<PhysicalAxisActivity, kPhysicalAxisCount> *activity)
{
    if (!activity || !value.isArray()) return false;
    const QJsonArray values = value.toArray();
    if (values.size() != kPhysicalAxisCount) return false;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        (*activity)[static_cast<size_t>(index)] = physicalAxisActivityFromKey(
            values.at(index).toString());
    }
    return true;
}

bool controllerVjoyRequirementsFromJson(const QJsonObject &json, ControllerVJoyRequirements *requirements)
{
    if (!requirements) return false;
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    if (axes.size() != kVirtualAxisSlotCount) return false;
    for (int index = 0; index < kVirtualAxisSlotCount; ++index) {
        requirements->axes[static_cast<size_t>(index)] = axes.at(index).toBool();
    }
    requirements->buttons = std::clamp(json.value(u"buttons"_qs).toInt(), 0, kMaximumVirtualButtons);
    requirements->continuousPovs = std::clamp(json.value(u"continuousPovs"_qs).toInt(), 0, 32);
    requirements->discretePovs = std::clamp(json.value(u"discretePovs"_qs).toInt(), 0, 32);
    requirements->deviceId = std::clamp(json.value(u"deviceId"_qs).toInt(1), 1, 16);
    return true;
}

QJsonObject outputLayoutToJson(const VirtualOutputLayout &layout)
{
    return {{u"id"_qs, layout.id}, {u"name"_qs, layout.name},
            {u"requirements"_qs, controllerVjoyRequirementsToJson(layout.requirements)},
            {u"hidHideDeviceInstanceId"_qs, layout.hidHideDeviceInstanceId},
            {u"hidhideManaged"_qs, layout.hidhideManaged}};
}

bool outputLayoutFromJson(const QJsonObject &json, VirtualOutputLayout *layout)
{
    if (!layout) return false;
    VirtualOutputLayout restored;
    restored.id = json.value(u"id"_qs).toString().trimmed().left(96);
    restored.name = json.value(u"name"_qs).toString().trimmed().left(64);
    if (restored.id.isEmpty() || restored.name.isEmpty()
        || !controllerVjoyRequirementsFromJson(json.value(u"requirements"_qs).toObject(),
                                                &restored.requirements)) {
        return false;
    }
    restored.requirements.axes[0] = false;
    restored.hidHideDeviceInstanceId = json.value(u"hidHideDeviceInstanceId"_qs)
        .toString().trimmed().left(512);
    restored.hidhideManaged = json.value(u"hidhideManaged"_qs).toBool(false)
        && !restored.hidHideDeviceInstanceId.isEmpty();
    *layout = std::move(restored);
    return true;
}

QJsonObject savedControllerToJson(const SavedControllerRecord &record)
{
    QJsonArray axes;
    QJsonArray calibration;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        axes.append(record.axes[static_cast<size_t>(index)]);
        calibration.append(calibrationToJson(record.calibration[static_cast<size_t>(index)]));
    }
    QJsonArray ownedInstances;
    for (const QString &instance : record.ownedHidHideDeviceInstances) ownedInstances.append(instance);
    return {{u"id"_qs, record.id}, {u"displayName"_qs, record.displayName},
            {u"lastDirectInputId"_qs, record.lastDirectInputId}, {u"productGuid"_qs, record.productGuid},
            {u"hidInstanceId"_qs, record.hidInstanceId}, {u"vendorId"_qs, record.vendorId},
            {u"productId"_qs, record.productId}, {u"axes"_qs, axes}, {u"axisCount"_qs, record.axisCount},
            {u"buttonCount"_qs, record.buttonCount}, {u"povCount"_qs, record.povCount},
            {u"capabilityFingerprint"_qs, record.capabilityFingerprint}, {u"lastSeen"_qs, record.lastSeen},
            {u"lastVerified"_qs, record.lastVerified}, {u"verificationVersion"_qs, record.verificationVersion},
            {u"calibration"_qs, calibration}, {u"axisActivity"_qs, axisActivityToJson(record.axisActivity)},
            {u"vjoyRequirements"_qs, controllerVjoyRequirementsToJson(record.vjoyRequirements)},
            {u"ownedHidHideDeviceInstances"_qs, ownedInstances}};
}

bool savedControllerFromJson(const QJsonObject &json, SavedControllerRecord *record)
{
    if (!record) return false;
    SavedControllerRecord parsed;
    parsed.id = json.value(u"id"_qs).toString().trimmed();
    parsed.displayName = json.value(u"displayName"_qs).toString().trimmed();
    parsed.lastDirectInputId = json.value(u"lastDirectInputId"_qs).toString().trimmed();
    parsed.productGuid = json.value(u"productGuid"_qs).toString().trimmed();
    parsed.hidInstanceId = json.value(u"hidInstanceId"_qs).toString().trimmed();
    const QJsonArray axes = json.value(u"axes"_qs).toArray();
    const QJsonArray calibration = json.value(u"calibration"_qs).toArray();
    if (parsed.id.isEmpty() || parsed.displayName.isEmpty() || axes.size() != kPhysicalAxisCount
        || calibration.size() != kPhysicalAxisCount
        || !controllerVjoyRequirementsFromJson(json.value(u"vjoyRequirements"_qs).toObject(), &parsed.vjoyRequirements)) {
        return false;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        parsed.axes[static_cast<size_t>(index)] = axes.at(index).toBool();
        parsed.calibration[static_cast<size_t>(index)] = calibrationFromJson(calibration.at(index).toObject());
    }
    if (json.contains(u"axisActivity"_qs)
        && !axisActivityFromJson(json.value(u"axisActivity"_qs), &parsed.axisActivity)) {
        return false;
    }
    parsed.vendorId = std::max(0, json.value(u"vendorId"_qs).toInt());
    parsed.productId = std::max(0, json.value(u"productId"_qs).toInt());
    parsed.axisCount = std::clamp(json.value(u"axisCount"_qs).toInt(), 0, kPhysicalAxisCount);
    parsed.buttonCount = std::clamp(json.value(u"buttonCount"_qs).toInt(), 0, kMaximumPhysicalButtons);
    parsed.povCount = std::clamp(json.value(u"povCount"_qs).toInt(), 0, kMaximumPhysicalPovs);
    parsed.capabilityFingerprint = json.value(u"capabilityFingerprint"_qs).toString();
    parsed.lastSeen = json.value(u"lastSeen"_qs).toString();
    parsed.lastVerified = json.value(u"lastVerified"_qs).toString();
    parsed.verificationVersion = std::max(1, json.value(u"verificationVersion"_qs).toInt(1));
    for (const QJsonValue &value : json.value(u"ownedHidHideDeviceInstances"_qs).toArray()) {
        const QString instance = value.toString().trimmed();
        if (!instance.isEmpty() && !parsed.ownedHidHideDeviceInstances.contains(instance, Qt::CaseInsensitive)) {
            parsed.ownedHidHideDeviceInstances.append(instance);
        }
    }
    *record = std::move(parsed);
    return true;
}

QJsonObject axisMappingToJson(const AxisMapping &mapping)
{
    QJsonObject json{
        {u"target"_qs, virtualAxisLabel(mapping.target)},
        {u"rangeMode"_qs, axisRangeModeKey(mapping.rangeMode)},
        {u"customName"_qs, mapping.customName.trimmed().left(48)},
        {u"inverted"_qs, mapping.inverted},
        {u"deadzone"_qs, mapping.deadzone},
        {u"hysteresis"_qs, mapping.hysteresis},
        {u"outputMinimum"_qs, mapping.outputMinimum},
        {u"outputMaximum"_qs, mapping.outputMaximum},
        {u"centeredOutputMinimum"_qs, mapping.centeredOutputMinimum},
        {u"centeredOutputMaximum"_qs, mapping.centeredOutputMaximum},
        {u"oneSidedOutputMinimum"_qs, mapping.oneSidedOutputMinimum},
        {u"oneSidedOutputMaximum"_qs, mapping.oneSidedOutputMaximum},
        {u"curve"_qs, curveDefinitionToJson(mapping.curve)},
    };
    if (mapping.hasCenteredCurveBackup) {
        json.insert(u"centeredCurveBackup"_qs, curveDefinitionToJson(mapping.centeredCurveBackup));
    }
    if (mapping.hasOneSidedCurveBackup) {
        json.insert(u"oneSidedCurveBackup"_qs, curveDefinitionToJson(mapping.oneSidedCurveBackup));
    }
    return json;
}

AxisMapping axisMappingFromJson(const QJsonObject &json, AxisRangeMode legacyRangeMode)
{
    AxisMapping mapping;
    mapping.target = virtualAxisFromString(json.value(u"target"_qs).toString());
    mapping.rangeMode = axisRangeModeFromString(json.value(u"rangeMode"_qs).toString(),
                                                 legacyRangeMode);
    mapping.customName = json.value(u"customName"_qs).toString().trimmed().left(48);
    mapping.inverted = json.value(u"inverted"_qs).toBool(false);
    mapping.deadzone = std::clamp(float(json.value(u"deadzone"_qs).toDouble(0.03)), 0.0F, 0.95F);
    mapping.hysteresis = std::clamp(float(json.value(u"hysteresis"_qs).toDouble(0.002)), 0.0F, 0.25F);
    mapping.outputMinimum = std::clamp(float(json.value(u"outputMinimum"_qs).toDouble(-1.0)), -1.0F, 1.0F);
    mapping.outputMaximum = std::clamp(float(json.value(u"outputMaximum"_qs).toDouble(1.0)), -1.0F, 1.0F);
    const bool hasDomainOutputLimits = json.contains(u"centeredOutputMinimum"_qs)
        && json.contains(u"centeredOutputMaximum"_qs)
        && json.contains(u"oneSidedOutputMinimum"_qs)
        && json.contains(u"oneSidedOutputMaximum"_qs);
    if (hasDomainOutputLimits) {
        mapping.centeredOutputMinimum = std::clamp(
            float(json.value(u"centeredOutputMinimum"_qs).toDouble(-1.0)), -1.0F, 1.0F);
        mapping.centeredOutputMaximum = std::clamp(
            float(json.value(u"centeredOutputMaximum"_qs).toDouble(1.0)), -1.0F, 1.0F);
        mapping.oneSidedOutputMinimum = std::clamp(
            float(json.value(u"oneSidedOutputMinimum"_qs).toDouble(0.0)), 0.0F, 1.0F);
        mapping.oneSidedOutputMaximum = std::clamp(
            float(json.value(u"oneSidedOutputMaximum"_qs).toDouble(1.0)), 0.0F, 1.0F);
    }
    mapping.curve = curveDefinitionFromJson(json.value(u"curve"_qs).toObject(),
                                             mapping.rangeMode == AxisRangeMode::OneSided);
    const QJsonObject centeredBackup = json.value(u"centeredCurveBackup"_qs).toObject();
    if (!centeredBackup.isEmpty()) {
        mapping.centeredCurveBackup = curveDefinitionFromJson(centeredBackup, false);
        mapping.hasCenteredCurveBackup = curveDefinitionIsValid(mapping.centeredCurveBackup, false);
    }
    const QJsonObject oneSidedBackup = json.value(u"oneSidedCurveBackup"_qs).toObject();
    if (!oneSidedBackup.isEmpty()) {
        mapping.oneSidedCurveBackup = curveDefinitionFromJson(oneSidedBackup, true);
        mapping.hasOneSidedCurveBackup = curveDefinitionIsValid(mapping.oneSidedCurveBackup, true);
    }
    normalizeAxisProcessing(mapping);
    if (!hasDomainOutputLimits) {
        // Schema 16 stored only the active range. Preserve it in its actual
        // domain and populate the untouched alternate range safely.
        if (mapping.rangeMode == AxisRangeMode::OneSided) {
            mapping.oneSidedOutputMinimum = mapping.outputMinimum;
            mapping.oneSidedOutputMaximum = mapping.outputMaximum;
        } else {
            mapping.centeredOutputMinimum = mapping.outputMinimum;
            mapping.centeredOutputMaximum = mapping.outputMaximum;
        }
    } else {
        if (mapping.centeredOutputMinimum >= mapping.centeredOutputMaximum) {
            mapping.centeredOutputMinimum = -1.0F;
            mapping.centeredOutputMaximum = 1.0F;
        }
        if (mapping.oneSidedOutputMinimum >= mapping.oneSidedOutputMaximum) {
            mapping.oneSidedOutputMinimum = 0.0F;
            mapping.oneSidedOutputMaximum = 1.0F;
        }
    }
    return mapping;
}

QJsonObject buttonBindingToJson(const ButtonBinding &binding)
{
    if (binding.type == ButtonActionType::VirtualButton) {
        return {{u"type"_qs, u"virtualButton"_qs}, {u"target"_qs, binding.target},
                {u"explicit"_qs, binding.explicitlyConfigured},
                {u"customName"_qs, binding.customName.trimmed().left(48)}};
    }
    return {{u"type"_qs, u"disabled"_qs}, {u"explicit"_qs, binding.explicitlyConfigured},
            {u"customName"_qs, binding.customName.trimmed().left(48)}};
}

ButtonBinding buttonBindingFromJson(const QJsonObject &json)
{
    const bool explicitlyConfigured = json.value(u"explicit"_qs).toBool(false);
    const QString customName = json.value(u"customName"_qs).toString().trimmed().left(48);
    if (json.value(u"type"_qs).toString().trimmed().compare(u"virtualButton"_qs, Qt::CaseInsensitive) == 0) {
        return {ButtonActionType::VirtualButton, json.value(u"target"_qs).toInt(0), explicitlyConfigured,
                customName};
    }
    return {ButtonActionType::Disabled, 0, explicitlyConfigured, customName};
}

QJsonObject curveTransitionSmoothingToJson(CurveTransitionSmoothingSettings settings)
{
    settings = sanitizedCurveTransitionSmoothing(settings);
    return {{u"enabled"_qs, settings.enabled}, {u"durationMs"_qs, settings.durationMs}};
}

CurveTransitionSmoothingSettings curveTransitionSmoothingFromJson(
    const QJsonValue &value, CurveTransitionSmoothingSettings fallback = {})
{
    if (!value.isObject()) return sanitizedCurveTransitionSmoothing(fallback);
    const QJsonObject json = value.toObject();
    fallback.enabled = json.value(u"enabled"_qs).toBool(fallback.enabled);
    fallback.durationMs = json.value(u"durationMs"_qs).toInt(fallback.durationMs);
    return sanitizedCurveTransitionSmoothing(fallback);
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
    configuration.curveTransitionSmoothing = curveTransitionSmoothingFromJson(
        json.value(u"curveTransitionSmoothing"_qs), configuration.curveTransitionSmoothing);
}

AxisRangeMode legacyRangeModeFor(PhysicalAxis axis)
{
    // Preserve pre-v1.8.5 throttle presentation when migrating an existing
    // profile, while every newly created v1.8.5 mapping is centered by
    // explicit default.
    return axis == PhysicalAxis::Z ? AxisRangeMode::OneSided : AxisRangeMode::Centered;
}

QJsonArray mappingControlsToJson(const MappingControlBindings &bindings)
{
    QJsonArray controls;
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalButtons);
    for (int index = 0; index < count; ++index) {
        controls.append(mappingControlActionKey(bindings[static_cast<size_t>(index)]));
    }
    return controls;
}

MappingControlBindings mappingControlsFromJson(const QJsonValue &value)
{
    MappingControlBindings bindings;
    if (!value.isArray()) return bindings;
    const QJsonArray controls = value.toArray();
    const int count = std::min(static_cast<int>(controls.size()), kMaximumPhysicalButtons);
    bindings.resize(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        bindings[static_cast<size_t>(index)] = mappingControlActionFromString(
            controls.at(index).toString());
    }
    return bindings;
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
    case AutomationActionType::MappingOn: return u"mappingOn"_qs;
    case AutomationActionType::MappingOff: return u"mappingOff"_qs;
    case AutomationActionType::ToggleMapping: return u"toggleMapping"_qs;
    case AutomationActionType::AdaptiveResponseEnable: return u"adaptiveResponseEnable"_qs;
    case AutomationActionType::AdaptiveResponseDisable: return u"adaptiveResponseDisable"_qs;
    case AutomationActionType::AdaptiveResponsePreset: return u"adaptiveResponsePreset"_qs;
    }
    return {};
}

bool automationActionTypeFromString(const QString &value, AutomationActionType *type)
{
    if (!type) return false;
    const QString normalized = value.trimmed();
    const std::array<std::pair<QString, AutomationActionType>, 17> choices{{
        {u"vJoyButtonHold"_qs, AutomationActionType::VJoyButtonHold}, {u"vJoyButtonToggle"_qs, AutomationActionType::VJoyButtonToggle},
        {u"profileHold"_qs, AutomationActionType::ProfileHold}, {u"profileToggle"_qs, AutomationActionType::ProfileToggle},
        {u"axisScale"_qs, AutomationActionType::AxisScale}, {u"axisOffset"_qs, AutomationActionType::AxisOffset},
        {u"axisClamp"_qs, AutomationActionType::AxisClamp}, {u"axisOverride"_qs, AutomationActionType::AxisOverride},
        {u"axisMix"_qs, AutomationActionType::AxisMix}, {u"axisFollow"_qs, AutomationActionType::AxisFollow},
        {u"vJoyButtonTap"_qs, AutomationActionType::VJoyButtonTap},
        {u"mappingOn"_qs, AutomationActionType::MappingOn},
        {u"mappingOff"_qs, AutomationActionType::MappingOff},
        {u"toggleMapping"_qs, AutomationActionType::ToggleMapping},
        {u"adaptiveResponseEnable"_qs, AutomationActionType::AdaptiveResponseEnable},
        {u"adaptiveResponseDisable"_qs, AutomationActionType::AdaptiveResponseDisable},
        {u"adaptiveResponsePreset"_qs, AutomationActionType::AdaptiveResponsePreset},
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
            {u"profileId"_qs, action.profileId},
            {u"adaptiveResponsePresetId"_qs, action.adaptiveResponsePresetId},
            {u"targetAxis"_qs, action.targetAxis},
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
    action->adaptiveResponsePresetId = json.value(u"adaptiveResponsePresetId"_qs)
        .toString().trimmed().left(96);
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
    QJsonObject virtualAliases;
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const QString alias = profile.virtualAxisAliases[static_cast<size_t>(index)].trimmed().left(48);
        if (!alias.isEmpty()) {
            virtualAliases.insert(virtualAxisLabel(static_cast<VirtualAxis>(index)).toCaseFolded()
                                      .remove(u" "_qs), alias);
        }
    }
    return {
        {u"id"_qs, profile.id},
        {u"name"_qs, profile.name},
        {u"categoryId"_qs, profile.categoryId},
        {u"enabled"_qs, profile.enabled},
        {u"outputLayoutId"_qs, profile.outputLayoutId},
        {u"curveTransitionSmoothingOverride"_qs, profile.curveTransitionSmoothingOverride},
        {u"curveTransitionSmoothing"_qs,
         curveTransitionSmoothingToJson(profile.curveTransitionSmoothing)},
        {u"adaptiveResponse"_qs, adaptiveResponseLayerToJson(profile.adaptiveResponse)},
        {u"axes"_qs, axes},
        {u"buttons"_qs, buttonBindingsToJson(profile.buttons)},
        {u"povs"_qs, povBindingsToJson(profile.povs)},
        {u"virtualAxisAliases"_qs, virtualAliases},
    };
}

bool profileFromJson(const QJsonObject &json, ControllerProfile *profile, bool migrateLegacyRangeMode)
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
    restored.categoryId = json.value(u"categoryId"_qs).toString().trimmed().left(96);
    restored.enabled = json.value(u"enabled"_qs).toBool(true);
    restored.outputLayoutId = json.value(u"outputLayoutId"_qs).toString().trimmed().left(96);
    restored.curveTransitionSmoothingOverride = json.value(
        u"curveTransitionSmoothingOverride"_qs).toBool(false);
    restored.curveTransitionSmoothing = curveTransitionSmoothingFromJson(
        json.value(u"curveTransitionSmoothing"_qs));
    if (json.contains(u"adaptiveResponse"_qs)
        && !adaptiveResponseLayerFromJson(json.value(u"adaptiveResponse"_qs), &restored.adaptiveResponse)) {
        return false;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QJsonObject axis = axes.at(index).toObject();
        if (axis.isEmpty()) return false;
        restored.axes[index] = axisMappingFromJson(axis, migrateLegacyRangeMode
            ? legacyRangeModeFor(static_cast<PhysicalAxis>(index)) : AxisRangeMode::Centered);
    }
    normalizeMappingConflicts(restored.axes);
    restored.buttons = buttonBindingsFromJson(json.value(u"buttons"_qs));
    restored.povs = povBindingsFromJson(json.value(u"povs"_qs));
    normalizePovMappings(restored.povs, restored.buttons, kMaximumVirtualButtons);
    const QJsonObject virtualAliases = json.value(u"virtualAxisAliases"_qs).toObject();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const QString key = virtualAxisLabel(static_cast<VirtualAxis>(index)).toCaseFolded()
            .remove(u" "_qs);
        restored.virtualAxisAliases[static_cast<size_t>(index)] = virtualAliases.value(key)
            .toString().trimmed().left(48);
    }
    *profile = std::move(restored);
    return true;
}

QJsonObject profileCategoryToJson(const ProfileCategory &category)
{
    QJsonArray profileIds;
    for (const QString &profileId : category.profileIds) profileIds.append(profileId);
    QJsonArray executableRules;
    for (const QString &rule : category.executableRules) executableRules.append(rule);
    return {
        {u"id"_qs, category.id},
        {u"name"_qs, category.name},
        {u"icon"_qs, category.icon.left(64)},
        {u"profileIds"_qs, profileIds},
        {u"defaultProfileId"_qs, category.defaultProfileId},
        {u"lastActiveProfileId"_qs, category.lastActiveProfileId},
        {u"executableRules"_qs, executableRules},
        {u"enabled"_qs, category.enabled},
        {u"restoreLastProfile"_qs, category.restoreLastProfile},
        {u"adaptiveResponse"_qs, adaptiveResponseLayerToJson(category.adaptiveResponse)},
    };
}

bool profileCategoryFromJson(const QJsonObject &json, ProfileCategory *category)
{
    if (!category) return false;
    ProfileCategory restored;
    restored.id = json.value(u"id"_qs).toString().trimmed();
    restored.name = json.value(u"name"_qs).toString().trimmed();
    restored.icon = json.value(u"icon"_qs).toString().trimmed().left(64);
    restored.defaultProfileId = json.value(u"defaultProfileId"_qs).toString().trimmed().left(96);
    restored.lastActiveProfileId = json.value(u"lastActiveProfileId"_qs).toString().trimmed().left(96);
    restored.enabled = json.value(u"enabled"_qs).toBool(true);
    restored.restoreLastProfile = json.value(u"restoreLastProfile"_qs).toBool(true);
    if (json.contains(u"adaptiveResponse"_qs)
        && !adaptiveResponseLayerFromJson(json.value(u"adaptiveResponse"_qs), &restored.adaptiveResponse)) {
        return false;
    }
    const QJsonArray profileIds = json.value(u"profileIds"_qs).toArray();
    const QJsonArray rules = json.value(u"executableRules"_qs).toArray();
    if (restored.id.isEmpty() || restored.id.size() > 96 || restored.name.isEmpty()
        || restored.name.size() > 64 || profileIds.size() > 256 || rules.size() > 32) return false;
    QSet<QString> seenProfileIds;
    for (const QJsonValue &value : profileIds) {
        const QString profileId = value.toString().trimmed().left(96);
        if (profileId.isEmpty() || seenProfileIds.contains(profileId)) return false;
        seenProfileIds.insert(profileId);
        restored.profileIds.push_back(profileId);
    }
    QSet<QString> seenRules;
    for (const QJsonValue &value : rules) {
        const QString rule = value.toString().trimmed().left(260);
        if (rule.isEmpty() || seenRules.contains(rule.toCaseFolded())) return false;
        seenRules.insert(rule.toCaseFolded());
        restored.executableRules.push_back(rule);
    }
    *category = std::move(restored);
    return true;
}

bool appendMigratedAutomation(MapperConfiguration &configuration, const QString &id,
                              const QString &name, AutomationConditionDefinition condition,
                              AutomationActionDefinition action)
{
    const auto existing = std::find_if(configuration.automations.cbegin(), configuration.automations.cend(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (existing != configuration.automations.cend()) return true;
    if (static_cast<int>(configuration.automations.size()) >= kMaximumAutomationRules) return false;
    AutomationDefinition automation;
    automation.id = id;
    automation.name = name;
    automation.conditions = {std::move(condition)};
    automation.actions = {std::move(action)};
    configuration.automations.push_back(std::move(automation));
    return true;
}

void appendLegacyMigrationWarning(MapperConfiguration &configuration, const QString &message)
{
    if (configuration.legacyControlMigrationWarning.isEmpty()) {
        configuration.legacyControlMigrationWarning = message;
    }
}

void migrateLegacyControlsToAutomation(MapperConfiguration &configuration)
{
    for (int source = 0; source < static_cast<int>(configuration.mappingControls.size()); ++source) {
        MappingControlAction &legacy = configuration.mappingControls[static_cast<size_t>(source)];
        if (legacy == MappingControlAction::None) continue;
        AutomationActionDefinition action;
        action.type = legacy == MappingControlAction::MappingOn ? AutomationActionType::MappingOn
            : legacy == MappingControlAction::MappingOff ? AutomationActionType::MappingOff
            : AutomationActionType::ToggleMapping;
        AutomationConditionDefinition condition;
        condition.type = AutomationConditionType::ButtonPressed;
        condition.button = source + 1;
        const QString label = mappingControlActionLabel(legacy);
        if (appendMigratedAutomation(configuration,
                QString(u"migration-v16-mapping-button-%1"_qs).arg(source + 1),
                QString(u"Migrated: Button %1 %2"_qs).arg(source + 1).arg(label), condition, action)) {
            legacy = MappingControlAction::None;
        } else {
            appendLegacyMigrationWarning(configuration,
                u"Some legacy mapping controls remain active because Automation is at its 64-rule limit."_qs);
        }
    }

    for (int source = 0; source < static_cast<int>(configuration.profileTriggers.size()); ++source) {
        ProfileTriggerBinding &legacy = configuration.profileTriggers[static_cast<size_t>(source)];
        if (!profileTriggerBindingEnabled(legacy)) continue;
        const ControllerProfile *target = findProfile(configuration, legacy.targetProfileId);
        if (!target) {
            legacy = {};
            appendLegacyMigrationWarning(configuration,
                u"A legacy profile control referenced a missing profile and was disabled; no hidden behavior remains."_qs);
            continue;
        }
        AutomationConditionDefinition condition;
        condition.type = legacy.mode == ProfileTriggerMode::Hold
            ? AutomationConditionType::ButtonHeld : AutomationConditionType::ButtonPressed;
        condition.button = source + 1;
        AutomationActionDefinition action;
        action.type = legacy.mode == ProfileTriggerMode::Hold
            ? AutomationActionType::ProfileHold : AutomationActionType::ProfileToggle;
        action.profileId = target->id;
        const QString mode = legacy.mode == ProfileTriggerMode::Hold ? u"Hold"_qs : u"Toggle"_qs;
        if (appendMigratedAutomation(configuration,
                QString(u"migration-v16-profile-button-%1"_qs).arg(source + 1),
                QString(u"Migrated: Button %1 %2 %3"_qs).arg(source + 1).arg(mode).arg(target->name),
                condition, action)) {
            legacy = {};
        } else {
            appendLegacyMigrationWarning(configuration,
                u"Some legacy profile controls remain active because Automation is at its 64-rule limit."_qs);
        }
    }

    for (int hat = 0; hat < static_cast<int>(configuration.povProfileTriggers.size()); ++hat) {
        auto &directions = configuration.povProfileTriggers[static_cast<size_t>(hat)];
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            ProfileTriggerBinding &legacy = directions[static_cast<size_t>(direction)];
            if (!profileTriggerBindingEnabled(legacy)) continue;
            const ControllerProfile *target = findProfile(configuration, legacy.targetProfileId);
            if (!target) {
                legacy = {};
                appendLegacyMigrationWarning(configuration,
                    u"A legacy POV profile control referenced a missing profile and was disabled; no hidden behavior remains."_qs);
                continue;
            }
            AutomationConditionDefinition condition;
            condition.type = AutomationConditionType::PovActive;
            condition.povHat = hat + 1;
            condition.povDirection = static_cast<PovDirection>(direction + 1);
            AutomationActionDefinition action;
            action.type = legacy.mode == ProfileTriggerMode::Hold
                ? AutomationActionType::ProfileHold : AutomationActionType::ProfileToggle;
            action.profileId = target->id;
            const QString mode = legacy.mode == ProfileTriggerMode::Hold ? u"Hold"_qs : u"Toggle"_qs;
            const QString directionName = povDirectionLabel(condition.povDirection);
            if (appendMigratedAutomation(configuration,
                    QString(u"migration-v16-profile-pov-%1-%2"_qs).arg(hat + 1).arg(direction + 1),
                    QString(u"Migrated: POV %1 %2 %3 %4"_qs)
                        .arg(hat + 1).arg(directionName).arg(mode).arg(target->name), condition, action)) {
                legacy = {};
            } else {
                appendLegacyMigrationWarning(configuration,
                    u"Some legacy POV profile controls remain active because Automation is at its 64-rule limit."_qs);
            }
        }
    }
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
        normal.axes[index] = axisMappingFromJson(axis,
            legacyRangeModeFor(static_cast<PhysicalAxis>(index)));
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

    QJsonArray calibrationHistory;
    const int historyCount = std::min(static_cast<int>(configuration.calibrationHistory.size()),
                                      kMaximumCalibrationHistoryEntries);
    for (int index = 0; index < historyCount; ++index) {
        calibrationHistory.append(calibrationHistoryEntryToJson(
            configuration.calibrationHistory[static_cast<size_t>(index)]));
    }

    QJsonArray profiles;
    for (const ControllerProfile &profile : configuration.profiles) profiles.append(profileToJson(profile));

    QJsonArray profileCategories;
    for (const ProfileCategory &category : configuration.profileCategories) {
        profileCategories.append(profileCategoryToJson(category));
    }

    QJsonArray outputLayouts;
    for (const VirtualOutputLayout &layout : configuration.outputLayouts) {
        outputLayouts.append(outputLayoutToJson(layout));
    }

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
    QJsonArray savedControllers;
    for (const SavedControllerRecord &record : configuration.savedControllers) {
        savedControllers.append(savedControllerToJson(record));
    }
    QJsonArray adaptiveResponsePresets;
    const int adaptivePresetCount = std::min(static_cast<int>(configuration.adaptiveResponsePresets.size()), 64);
    for (int index = 0; index < adaptivePresetCount; ++index) {
        adaptiveResponsePresets.append(adaptiveResponsePresetToJson(
            configuration.adaptiveResponsePresets[static_cast<size_t>(index)]));
    }

    return {
        {u"version"_qs, kProfileSchemaVersion},
        {u"preferredDeviceId"_qs, configuration.preferredDeviceId},
        {u"savedControllers"_qs, savedControllers},
        {u"activeControllerRecordId"_qs, configuration.activeControllerRecordId},
        {u"autoSwitchVerifiedController"_qs, configuration.autoSwitchVerifiedController},
        {u"keepRunningInTray"_qs, configuration.keepRunningInTray},
        {u"vjoyDeviceId"_qs, configuration.vjoyDeviceId},
        {u"startMappingOnLaunch"_qs, configuration.startMappingOnLaunch},
        {u"disabledAxisValue"_qs, sanitizedDisabledAxisValue(configuration.disabledAxisValue)},
        {u"curveTransitionSmoothing"_qs,
         curveTransitionSmoothingToJson(configuration.curveTransitionSmoothing)},
        {u"adaptiveResponseSchemaVersion"_qs, kAdaptiveResponseSchemaVersion},
        {u"adaptiveResponseGlobal"_qs, adaptiveResponseLayerToJson(configuration.adaptiveResponseGlobal)},
        {u"adaptiveResponsePresets"_qs, adaptiveResponsePresets},
        {u"selectedAxisIndex"_qs, configuration.selectedAxisIndex},
        {u"calibration"_qs, calibration},
        {u"axisActivity"_qs, axisActivityToJson(configuration.axisActivity)},
        {u"calibrationHistory"_qs, calibrationHistory},
        {u"outputLayouts"_qs, outputLayouts},
        {u"profiles"_qs, profiles},
        {u"profileCategories"_qs, profileCategories},
        {u"automaticGameDetection"_qs, configuration.automaticGameDetection},
        {u"personalCurvePresets"_qs, personalCurvePresets},
        {u"profileTriggers"_qs, profileTriggersToJson(configuration.profileTriggers)},
        {u"povProfileTriggers"_qs, povProfileTriggersToJson(configuration.povProfileTriggers)},
        {u"mappingControls"_qs, mappingControlsToJson(configuration.mappingControls)},
        {u"legacyControlMigrationWarning"_qs, configuration.legacyControlMigrationWarning},
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
        && version != 9 && version != 10 && version != 11 && version != 12 && version != 13 && version != 14
        && version != 15 && version != 16 && version != 17 && version != 18 && version != 19 && version != 20
        && version != kProfileSchemaVersion) {
        if (valid) *valid = false;
        return fallbackWithGlobalSettings(json);
    }

    MapperConfiguration configuration = fallbackWithGlobalSettings(json);
    if (version >= 21) {
        const int adaptiveSchema = json.value(u"adaptiveResponseSchemaVersion"_qs).toInt();
        const QJsonArray presets = json.value(u"adaptiveResponsePresets"_qs).toArray();
        if (adaptiveSchema != kAdaptiveResponseSchemaVersion || presets.size() > 64
            || !adaptiveResponseLayerFromJson(json.value(u"adaptiveResponseGlobal"_qs),
                                               &configuration.adaptiveResponseGlobal)) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        QSet<QString> presetIds;
        QSet<QString> presetNames;
        for (const QJsonValue &value : presets) {
            AdaptiveResponsePreset preset;
            if (!adaptiveResponsePresetFromJson(value.toObject(), &preset)
                || presetIds.contains(preset.id) || presetNames.contains(preset.name.toCaseFolded())
                || findAdaptiveResponsePreset(defaultConfiguration(), preset.id)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            presetIds.insert(preset.id);
            presetNames.insert(preset.name.toCaseFolded());
            configuration.adaptiveResponsePresets.push_back(std::move(preset));
        }
        configuration.adaptiveResponseSchemaVersion = adaptiveSchema;
    }
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
    if (version >= 18 && !axisActivityFromJson(json.value(u"axisActivity"_qs),
                                                &configuration.axisActivity)) {
        if (valid) *valid = false;
        return fallbackWithGlobalSettings(json);
    }
    if (version >= 16) {
        const QJsonArray history = json.value(u"calibrationHistory"_qs).toArray();
        if (history.size() > kMaximumCalibrationHistoryEntries) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        for (const QJsonValue &value : history) {
            CalibrationHistoryEntry entry;
            if (!calibrationHistoryEntryFromJson(value.toObject(), &entry)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            configuration.calibrationHistory.push_back(std::move(entry));
        }
        configuration.legacyControlMigrationWarning =
            json.value(u"legacyControlMigrationWarning"_qs).toString().trimmed().left(256);
    }
    if (version >= 15) {
        const QJsonArray savedControllers = json.value(u"savedControllers"_qs).toArray();
        if (savedControllers.size() > 64) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        QSet<QString> savedControllerIds;
        for (const QJsonValue &value : savedControllers) {
            SavedControllerRecord record;
            if (!savedControllerFromJson(value.toObject(), &record) || savedControllerIds.contains(record.id)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            savedControllerIds.insert(record.id);
            configuration.savedControllers.push_back(std::move(record));
        }
        configuration.activeControllerRecordId = json.value(u"activeControllerRecordId"_qs).toString().trimmed();
        if (!savedControllerIds.contains(configuration.activeControllerRecordId)) {
            configuration.activeControllerRecordId.clear();
        }
        configuration.autoSwitchVerifiedController = json.value(u"autoSwitchVerifiedController"_qs).toBool(true);
        configuration.keepRunningInTray = json.value(u"keepRunningInTray"_qs).toBool(true);
    }

    configuration.profiles.clear();
    QSet<QString> ids;
    for (const QJsonValue &value : profiles) {
        ControllerProfile profile;
        if (!profileFromJson(value.toObject(), &profile, version < 18)
            || ids.contains(profile.id)) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        ids.insert(profile.id);
        configuration.profiles.push_back(std::move(profile));
    }

    if (version >= 19) {
        const QJsonArray categories = json.value(u"profileCategories"_qs).toArray();
        if (categories.empty() || categories.size() > 64) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        configuration.profileCategories.clear();
        QSet<QString> categoryIds;
        QSet<QString> categoryNames;
        for (const QJsonValue &value : categories) {
            ProfileCategory category;
            if (!profileCategoryFromJson(value.toObject(), &category)
                || categoryIds.contains(category.id)
                || categoryNames.contains(category.name.toCaseFolded())) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            categoryIds.insert(category.id);
            categoryNames.insert(category.name.toCaseFolded());
            configuration.profileCategories.push_back(std::move(category));
        }
        QSet<QString> orderedProfileIds;
        for (const ProfileCategory &category : configuration.profileCategories) {
            for (const QString &profileId : category.profileIds) {
                const ControllerProfile *profile = findProfile(configuration, profileId);
                if (!profile || profile->categoryId != category.id || orderedProfileIds.contains(profileId)) {
                    if (valid) *valid = false;
                    return fallbackWithGlobalSettings(json);
                }
                orderedProfileIds.insert(profileId);
            }
            if ((!category.defaultProfileId.isEmpty()
                    && (!findProfile(configuration, category.defaultProfileId)
                        || findProfile(configuration, category.defaultProfileId)->categoryId != category.id))
                || (!category.lastActiveProfileId.isEmpty()
                    && (!findProfile(configuration, category.lastActiveProfileId)
                        || findProfile(configuration, category.lastActiveProfileId)->categoryId != category.id))) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
        }
        QSet<QString> namesByCategory;
        for (const ControllerProfile &profile : configuration.profiles) {
            if (!categoryIds.contains(profile.categoryId) || !orderedProfileIds.contains(profile.id)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            const QString scopedName = profile.categoryId + u"\x1f"_qs + profile.name.toCaseFolded();
            if (namesByCategory.contains(scopedName)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            namesByCategory.insert(scopedName);
        }
        configuration.automaticGameDetection = json.value(u"automaticGameDetection"_qs).toBool(true);
    } else {
        // v2.1.0 deliberately performs no name-based game inference. Every
        // existing profile enters the neutral General category unchanged.
        ProfileCategory general;
        general.id = generalProfileCategoryId();
        general.name = u"General"_qs;
        for (ControllerProfile &profile : configuration.profiles) {
            profile.categoryId = general.id;
            profile.enabled = true;
            general.profileIds.push_back(profile.id);
        }
        general.defaultProfileId = findProfile(configuration, normalProfileId())
            ? normalProfileId() : general.profileIds.front();
        general.lastActiveProfileId = general.defaultProfileId;
        configuration.profileCategories = {std::move(general)};
        configuration.automaticGameDetection = true;
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
    // v1.8.5 adds global mapping controls. Earlier records migrate safely
    // with no source assigned.
    if (version >= 13) {
        configuration.mappingControls = mappingControlsFromJson(json.value(u"mappingControls"_qs));
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

    if (version < 16) migrateLegacyControlsToAutomation(configuration);

    if (version >= 18) {
        const QJsonArray layouts = json.value(u"outputLayouts"_qs).toArray();
        if (layouts.empty() || layouts.size() > 16) {
            if (valid) *valid = false;
            return fallbackWithGlobalSettings(json);
        }
        configuration.outputLayouts.clear();
        QSet<QString> layoutIds;
        QSet<int> layoutDeviceIds;
        for (const QJsonValue &value : layouts) {
            VirtualOutputLayout layout;
            if (!outputLayoutFromJson(value.toObject(), &layout)
                || layoutIds.contains(layout.id) || layoutDeviceIds.contains(layout.requirements.deviceId)) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            layoutIds.insert(layout.id);
            layoutDeviceIds.insert(layout.requirements.deviceId);
            configuration.outputLayouts.push_back(std::move(layout));
        }
    } else {
        // v2.0.10 migration is intentionally data-only: preserve the selected
        // device and every mapping, but never touch the vJoy driver while a
        // configuration is read. The compact default derives from existing
        // routes so a legacy five-axis configuration is not silently reduced.
        VirtualOutputLayout migrated = defaultBf6OutputLayout();
        migrated.requirements.deviceId = configuration.vjoyDeviceId;
        migrated.requirements.axes.fill(false);
        migrated.requirements.buttons = 0;
        migrated.requirements.continuousPovs = 0;
        migrated.requirements.discretePovs = 0;
        for (const ControllerProfile &profile : configuration.profiles) {
            for (const AxisMapping &axis : profile.axes) {
                const int target = static_cast<int>(axis.target);
                if (target > 0 && target < kVirtualAxisSlotCount) {
                    migrated.requirements.axes[static_cast<size_t>(target)] = true;
                }
            }
            for (const ButtonBinding &binding : profile.buttons) {
                if (binding.type == ButtonActionType::VirtualButton) {
                    migrated.requirements.buttons = std::max(migrated.requirements.buttons, binding.target);
                }
            }
            for (const auto &hat : profile.povs) {
                for (const ButtonBinding &binding : hat) {
                    if (binding.type == ButtonActionType::VirtualButton) {
                        migrated.requirements.buttons = std::max(migrated.requirements.buttons, binding.target);
                    }
                }
            }
        }
        for (const NativePovBinding &binding : configuration.nativePovBindings) {
            if (!binding.enabled) continue;
            if (binding.targetType == NativePovTargetType::Continuous) {
                migrated.requirements.continuousPovs = std::max(migrated.requirements.continuousPovs,
                                                               binding.targetIndex);
            } else if (binding.targetType == NativePovTargetType::Discrete) {
                migrated.requirements.discretePovs = std::max(migrated.requirements.discretePovs,
                                                             binding.targetIndex);
            }
        }
        for (const AutomationDefinition &automation : configuration.automations) {
            for (const AutomationActionDefinition &action : automation.actions) {
                if (action.type == AutomationActionType::VJoyButtonHold
                    || action.type == AutomationActionType::VJoyButtonToggle
                    || action.type == AutomationActionType::VJoyButtonTap) {
                    migrated.requirements.buttons = std::max(migrated.requirements.buttons,
                                                             action.virtualButton);
                }
            }
        }
        if (std::none_of(migrated.requirements.axes.cbegin() + 1,
                         migrated.requirements.axes.cend(), [](bool axis) { return axis; })) {
            migrated = defaultBf6OutputLayout();
            migrated.requirements.deviceId = configuration.vjoyDeviceId;
        }
        configuration.outputLayouts = {std::move(migrated)};
        for (ControllerProfile &profile : configuration.profiles) {
            profile.outputLayoutId = defaultOutputLayoutId();
        }
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
    for (ControllerProfile &profile : configuration.profiles) {
        if (!findOutputLayout(configuration, profile.outputLayoutId)) {
            if (version >= 18) {
                if (valid) *valid = false;
                return fallbackWithGlobalSettings(json);
            }
            profile.outputLayoutId = defaultOutputLayoutId();
        }
    }
    if (const VirtualOutputLayout *activeLayout = findOutputLayout(configuration,
            activeProfile(configuration).outputLayoutId)) {
        configuration.vjoyDeviceId = activeLayout->requirements.deviceId;
    }
    if (valid) *valid = true;
    return configuration;
}

QJsonObject ConfigStore::portableProfileToJson(const ControllerProfile &profile)
{
    return profileToJson(profile);
}

bool ConfigStore::portableProfileFromJson(const QJsonObject &json, ControllerProfile *profile)
{
    return profileFromJson(json, profile, false);
}

QJsonObject ConfigStore::portableCategoryToJson(const ProfileCategory &category)
{
    return profileCategoryToJson(category);
}

bool ConfigStore::portableCategoryFromJson(const QJsonObject &json, ProfileCategory *category)
{
    return profileCategoryFromJson(json, category);
}

QJsonObject ConfigStore::portableCurveToJson(const PersonalCurvePreset &preset)
{
    return personalCurvePresetToJson(preset);
}

bool ConfigStore::portableCurveFromJson(const QJsonObject &json, PersonalCurvePreset *preset)
{
    return personalCurvePresetFromJson(json, preset);
}

QJsonObject ConfigStore::portableAdaptiveResponsePresetToJson(const AdaptiveResponsePreset &preset)
{
    return adaptiveResponsePresetToJson(preset);
}

bool ConfigStore::portableAdaptiveResponsePresetFromJson(const QJsonObject &json,
                                                          AdaptiveResponsePreset *preset)
{
    return adaptiveResponsePresetFromJson(json, preset);
}

QJsonObject ConfigStore::portableAdaptiveResponseLayerToJson(const AdaptiveResponseLayer &layer)
{
    return adaptiveResponseLayerToJson(layer);
}

bool ConfigStore::portableAdaptiveResponseLayerFromJson(const QJsonValue &value,
                                                         AdaptiveResponseLayer *layer)
{
    return adaptiveResponseLayerFromJson(value, layer);
}

QJsonObject ConfigStore::portableAutomationToJson(const AutomationDefinition &automation)
{
    return automationToJson(automation);
}

bool ConfigStore::portableAutomationFromJson(const QJsonObject &json, AutomationDefinition *automation)
{
    return automationFromJson(json, automation);
}

QJsonObject ConfigStore::portableOutputLayoutToJson(const VirtualOutputLayout &layout)
{
    return outputLayoutToJson(layout);
}

bool ConfigStore::portableOutputLayoutFromJson(const QJsonObject &json, VirtualOutputLayout *layout)
{
    return outputLayoutFromJson(json, layout);
}

QJsonArray ConfigStore::portableProfileTriggersToJson(const ProfileTriggerBindings &bindings)
{
    return profileTriggersToJson(bindings);
}

ProfileTriggerBindings ConfigStore::portableProfileTriggersFromJson(const QJsonValue &value)
{
    return profileTriggersFromJson(value);
}

QJsonArray ConfigStore::portablePovProfileTriggersToJson(const PovProfileTriggerBindings &bindings)
{
    return povProfileTriggersToJson(bindings);
}

PovProfileTriggerBindings ConfigStore::portablePovProfileTriggersFromJson(const QJsonValue &value)
{
    return povProfileTriggersFromJson(value);
}

} // namespace hotas
