#include "profile_portability.h"

#include "adaptive_response.h"
#include "config_store.h"
#include "profile_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {

constexpr qsizetype kMaximumPortableFileBytes = 5 * 1024 * 1024;
// Profile and Pack schemas deliberately advance independently.  They happen
// to start at the same revision, but a future Pack addition must not make a
// standalone Profile unreadable (or vice versa).
constexpr int kPortableProfileSchemaVersion = 1;
constexpr int kPortablePackSchemaVersion = 1;

QString localPath(const QString &fileName)
{
    const QUrl url(fileName);
    return url.isLocalFile() ? url.toLocalFile() : fileName;
}

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

bool hasExpectedExtension(const QString &fileName, PortableConfigurationKind kind)
{
    return QFileInfo(localPath(fileName)).suffix().compare(
        kind == PortableConfigurationKind::Profile ? u"hbf6profile"_qs : u"hbf6pack"_qs,
        Qt::CaseInsensitive) == 0;
}

QString portableFormat(PortableConfigurationKind kind)
{
    return kind == PortableConfigurationKind::Profile ? u"HOTAS-BF6-Profile"_qs
                                                      : u"HOTAS-BF6-Pack"_qs;
}

bool automationReferencesAnyProfile(const AutomationDefinition &automation, const QSet<QString> &profileIds)
{
    for (const AutomationConditionDefinition &condition : automation.conditions) {
        if (profileIds.contains(condition.profileId)) return true;
    }
    for (const AutomationActionDefinition &action : automation.actions) {
        if (profileIds.contains(action.profileId)) return true;
    }
    return false;
}

bool adaptiveLayerReferencesAvailable(const AdaptiveResponseLayer &layer,
                                      const QSet<QString> &portablePresetIds)
{
    MapperConfiguration builtIns;
    for (const AdaptiveResponseAxisOverride &axis : layer.axes) {
        if (axis.presetId.isEmpty()) continue;
        if (!portablePresetIds.contains(axis.presetId)
            && !findAdaptiveResponsePreset(builtIns, axis.presetId)) return false;
    }
    return true;
}

void collectAdaptiveResponsePresetDependencies(const AdaptiveResponseLayer &layer,
                                               QSet<QString> *presetIds)
{
    if (!presetIds) return;
    for (const AdaptiveResponseAxisOverride &axis : layer.axes) {
        if (!axis.presetId.trimmed().isEmpty()) presetIds->insert(axis.presetId);
    }
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
        && left.settlingResponse == right.settlingResponse && left.endpointTaper == right.endpointTaper;
}

bool sameAdaptiveResponsePresetContent(const AdaptiveResponsePreset &left,
                                       const AdaptiveResponsePreset &right)
{
    if (left.name != right.name || left.description != right.description) return false;
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const AdaptiveResponseAxisOverride &a = left.axes[static_cast<size_t>(axis)];
        const AdaptiveResponseAxisOverride &b = right.axes[static_cast<size_t>(axis)];
        if (a.properties != b.properties || a.presetId != b.presetId
            || !sameAdaptiveResponseSettings(a.settings, b.settings)) return false;
    }
    return true;
}

QString uniqueImportedAdaptiveResponsePresetId(const MapperConfiguration &configuration,
                                               const QString &sourceId)
{
    const QString base = sourceId.left(72).trimmed() + u"-imported"_qs;
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const QString candidate = suffix == 1 ? base : base + u"-"_qs + QString::number(suffix);
        if (!findAdaptiveResponsePreset(configuration, candidate)) return candidate;
    }
    return {};
}

void remapAdaptiveResponseLayer(AdaptiveResponseLayer *layer, const QHash<QString, QString> &ids)
{
    if (!layer) return;
    for (AdaptiveResponseAxisOverride &axis : layer->axes) {
        const auto found = ids.constFind(axis.presetId);
        if (found != ids.cend()) axis.presetId = *found;
    }
}

bool adaptivePresetReferenceAvailable(const QString &id, const QSet<QString> &portablePresetIds)
{
    if (id.isEmpty()) return false;
    MapperConfiguration builtIns;
    return portablePresetIds.contains(id) || findAdaptiveResponsePreset(builtIns, id);
}

QJsonObject vjoyRequirementsDescriptor(const ControllerVJoyRequirements &requirements)
{
    QJsonArray axes;
    for (const bool axis : requirements.axes) axes.append(axis);
    return {{u"axes"_qs, axes}, {u"buttons"_qs, requirements.buttons},
            {u"continuousPovs"_qs, requirements.continuousPovs},
            {u"discretePovs"_qs, requirements.discretePovs},
            {u"deviceId"_qs, requirements.deviceId}};
}

QJsonObject deviceDescriptor(const SavedControllerRecord &record, bool includeCalibration)
{
    QJsonArray axes;
    for (const bool axis : record.axes) axes.append(axis);
    QJsonObject descriptor{
        {u"name"_qs, record.displayName.left(96)},
        {u"productGuid"_qs, record.productGuid.left(96)},
        {u"vendorId"_qs, record.vendorId},
        {u"productId"_qs, record.productId},
        {u"axisCount"_qs, record.axisCount},
        {u"buttonCount"_qs, record.buttonCount},
        {u"povCount"_qs, record.povCount},
        {u"axes"_qs, axes},
        {u"vjoyRequirements"_qs, vjoyRequirementsDescriptor(record.vjoyRequirements)},
    };
    if (includeCalibration) {
        QJsonArray calibration;
        for (const Calibration &axis : record.calibration) {
            calibration.append(QJsonObject{{u"enabled"_qs, axis.enabled}, {u"minimum"_qs, axis.minimum},
                                             {u"center"_qs, axis.center}, {u"maximum"_qs, axis.maximum},
                                             {u"centered"_qs, axis.centered}});
        }
        descriptor.insert(u"calibration"_qs, calibration);
    }
    return descriptor;
}

bool boundedInteger(const QJsonValue &value, int minimum, int maximum)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= minimum && number <= maximum;
}

bool portableDeviceDescriptorIsValid(const QJsonObject &device, bool requireIdentity)
{
    if (device.isEmpty() || device.value(u"name"_qs).toString().trimmed().size() > 96
        || device.value(u"productGuid"_qs).toString().trimmed().size() > 96
        || !boundedInteger(device.value(u"vendorId"_qs), 0, 65535)
        || !boundedInteger(device.value(u"productId"_qs), 0, 65535)
        || !boundedInteger(device.value(u"axisCount"_qs), 0, kPhysicalAxisCount)
        || !boundedInteger(device.value(u"buttonCount"_qs), 0, kMaximumPhysicalButtons)
        || !boundedInteger(device.value(u"povCount"_qs), 0, kMaximumPhysicalPovs)) {
        return false;
    }
    if (requireIdentity && device.value(u"name"_qs).toString().trimmed().isEmpty()) return false;
    const QJsonValue axesValue = device.value(u"axes"_qs);
    if (axesValue.isArray() && axesValue.toArray().size() != kPhysicalAxisCount) return false;
    const QJsonValue requirements = device.value(u"vjoyRequirements"_qs);
    if (requirements.isObject()) {
        const QJsonObject values = requirements.toObject();
        const QJsonArray axes = values.value(u"axes"_qs).toArray();
        if (axes.size() != kVirtualAxisSlotCount
            || !boundedInteger(values.value(u"buttons"_qs), 0, kMaximumVirtualButtons)
            || !boundedInteger(values.value(u"continuousPovs"_qs), 0, 32)
            || !boundedInteger(values.value(u"discretePovs"_qs), 0, 32)
            || !boundedInteger(values.value(u"deviceId"_qs), 1, 16)) return false;
    }
    const QJsonValue calibrationValue = device.value(u"calibration"_qs);
    if (!calibrationValue.isUndefined()) {
        if (!calibrationValue.isArray() || calibrationValue.toArray().size() != kPhysicalAxisCount) return false;
        for (const QJsonValue &axisValue : calibrationValue.toArray()) {
            const QJsonObject axis = axisValue.toObject();
            if (axis.isEmpty() || !axis.value(u"enabled"_qs).isBool()
                || !axis.value(u"centered"_qs).isBool()) return false;
            for (const QString &key : {u"minimum"_qs, u"center"_qs, u"maximum"_qs}) {
                const QJsonValue value = axis.value(key);
                if (!value.isDouble() || !std::isfinite(value.toDouble())
                    || value.toDouble() < -1.0 || value.toDouble() > 1.0) return false;
            }
        }
    }
    return true;
}

bool deviceDescriptorMatchesRecord(const QVariantMap &descriptor, const SavedControllerRecord &record)
{
    const QString sourceGuid = descriptor.value(u"productGuid"_qs).toString().trimmed();
    const QString localGuid = record.productGuid.trimmed();
    const int sourceVendor = descriptor.value(u"vendorId"_qs).toInt();
    const int sourceProduct = descriptor.value(u"productId"_qs).toInt();
    const bool guidMatches = !sourceGuid.isEmpty() && !localGuid.isEmpty()
        && sourceGuid.compare(localGuid, Qt::CaseInsensitive) == 0;
    const bool vidPidMatches = sourceVendor > 0 && sourceProduct > 0
        && sourceVendor == record.vendorId && sourceProduct == record.productId;
    if (!guidMatches && !vidPidMatches) return false;
    const int sourceAxes = descriptor.value(u"axisCount"_qs).toInt();
    const int sourceButtons = descriptor.value(u"buttonCount"_qs).toInt();
    const int sourcePovs = descriptor.value(u"povCount"_qs).toInt();
    return (sourceAxes == 0 || record.axisCount == 0 || record.axisCount >= sourceAxes)
        && (sourceButtons == 0 || record.buttonCount == 0 || record.buttonCount >= sourceButtons)
        && (sourcePovs == 0 || record.povCount == 0 || record.povCount >= sourcePovs);
}

QList<const SavedControllerRecord *> matchingSavedControllers(const MapperConfiguration &configuration,
                                                              const QVariantMap &descriptor)
{
    QList<const SavedControllerRecord *> matches;
    for (const SavedControllerRecord &record : configuration.savedControllers) {
        if (deviceDescriptorMatchesRecord(descriptor, record)) matches.append(&record);
    }
    return matches;
}

bool calibrationFromPortableDescriptor(const QVariantMap &descriptor,
                                       std::array<Calibration, kPhysicalAxisCount> *calibration)
{
    if (!calibration) return false;
    const QVariantList values = descriptor.value(u"calibration"_qs).toList();
    if (values.size() != kPhysicalAxisCount) return false;
    std::array<Calibration, kPhysicalAxisCount> parsed{};
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const QVariantMap value = values.at(index).toMap();
        if (value.isEmpty()) return false;
        Calibration axis;
        axis.enabled = value.value(u"enabled"_qs).toBool();
        axis.minimum = static_cast<float>(value.value(u"minimum"_qs).toDouble());
        axis.center = static_cast<float>(value.value(u"center"_qs).toDouble());
        axis.maximum = static_cast<float>(value.value(u"maximum"_qs).toDouble());
        axis.centered = !value.contains(u"centered"_qs) || value.value(u"centered"_qs).toBool();
        if (!std::isfinite(axis.minimum) || !std::isfinite(axis.center) || !std::isfinite(axis.maximum)
            || axis.minimum < -1.0F || axis.maximum > 1.0F || axis.minimum >= axis.maximum
            || axis.center < axis.minimum || axis.center > axis.maximum) return false;
        parsed[static_cast<size_t>(index)] = axis;
    }
    *calibration = parsed;
    return true;
}

QJsonObject bundleToJson(const MapperConfiguration &configuration, PortableConfigurationKind kind,
                         const QSet<QString> &selectedProfileIds, const QString &name,
                         const QString &description, bool includeDevices, bool includeCalibration,
                         bool includeAutomations, bool includeProfileRelationships,
                         bool includeGameDetection)
{
    QSet<QString> resolvedProfileIds = selectedProfileIds;
    // A selected Automation can reference another profile.  Its relationship
    // is part of the selected profile's behavior, so include that dependency
    // rather than serialising an unresolved reference.
    bool dependenciesAdded = includeAutomations;
    while (dependenciesAdded) {
        dependenciesAdded = false;
        for (const AutomationDefinition &automation : configuration.automations) {
            if (!automationReferencesAnyProfile(automation, resolvedProfileIds)) continue;
            for (const AutomationConditionDefinition &condition : automation.conditions) {
                if (!condition.profileId.isEmpty() && findProfile(configuration, condition.profileId)
                    && !resolvedProfileIds.contains(condition.profileId)) {
                    resolvedProfileIds.insert(condition.profileId); dependenciesAdded = true;
                }
            }
            for (const AutomationActionDefinition &action : automation.actions) {
                if (!action.profileId.isEmpty() && findProfile(configuration, action.profileId)
                    && !resolvedProfileIds.contains(action.profileId)) {
                    resolvedProfileIds.insert(action.profileId); dependenciesAdded = true;
                }
            }
        }
    }
    QSet<QString> selectedCategoryIds;
    QSet<QString> selectedLayoutIds;
    QSet<QString> selectedCurveIds;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (!resolvedProfileIds.contains(profile.id)) continue;
        selectedCategoryIds.insert(profile.categoryId);
        selectedLayoutIds.insert(profile.outputLayoutId);
        for (const AxisMapping &axis : profile.axes) {
            if (axis.curve.family == CurveFamily::Personal && !axis.curve.presetId.isEmpty()) {
                selectedCurveIds.insert(axis.curve.presetId);
            }
        }
    }

    QJsonArray categories;
    for (const ProfileCategory &source : configuration.profileCategories) {
        if (!selectedCategoryIds.contains(source.id)) continue;
        ProfileCategory category = source;
        category.profileIds.erase(std::remove_if(category.profileIds.begin(), category.profileIds.end(),
            [&resolvedProfileIds](const QString &id) { return !resolvedProfileIds.contains(id); }),
            category.profileIds.end());
        if (!resolvedProfileIds.contains(category.defaultProfileId)) category.defaultProfileId.clear();
        if (!resolvedProfileIds.contains(category.lastActiveProfileId)) category.lastActiveProfileId.clear();
        if (!includeGameDetection) category.executableRules.clear();
        categories.append(ConfigStore::portableCategoryToJson(category));
    }

    QJsonArray profiles;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (resolvedProfileIds.contains(profile.id)) profiles.append(ConfigStore::portableProfileToJson(profile));
    }
    QJsonArray curves;
    for (const PersonalCurvePreset &curve : configuration.personalCurvePresets) {
        if (selectedCurveIds.contains(curve.id)) curves.append(ConfigStore::portableCurveToJson(curve));
    }
    QJsonArray layouts;
    for (const VirtualOutputLayout &layout : configuration.outputLayouts) {
        if (selectedLayoutIds.contains(layout.id)) layouts.append(ConfigStore::portableOutputLayoutToJson(layout));
    }
    QJsonArray automations;
    for (const AutomationDefinition &automation : configuration.automations) {
        if (includeAutomations && automationReferencesAnyProfile(automation, resolvedProfileIds)) {
            automations.append(ConfigStore::portableAutomationToJson(automation));
        }
    }
    // Carry only the custom Adaptive Response presets that this export can
    // actually reference. Built-ins travel by stable ID and are reconstructed
    // locally, while unrelated user presets stay private to the source setup.
    QSet<QString> requiredAdaptivePresetIds;
    if (kind == PortableConfigurationKind::Pack) {
        collectAdaptiveResponsePresetDependencies(configuration.adaptiveResponseGlobal,
                                                   &requiredAdaptivePresetIds);
    }
    for (const ProfileCategory &category : configuration.profileCategories) {
        if (selectedCategoryIds.contains(category.id)) {
            collectAdaptiveResponsePresetDependencies(category.adaptiveResponse,
                                                       &requiredAdaptivePresetIds);
        }
    }
    for (const ControllerProfile &profile : configuration.profiles) {
        if (resolvedProfileIds.contains(profile.id)) {
            collectAdaptiveResponsePresetDependencies(profile.adaptiveResponse,
                                                       &requiredAdaptivePresetIds);
        }
    }
    if (includeAutomations) {
        for (const AutomationDefinition &automation : configuration.automations) {
            if (!automationReferencesAnyProfile(automation, resolvedProfileIds)) continue;
            for (const AutomationActionDefinition &action : automation.actions) {
                if (action.type == AutomationActionType::AdaptiveResponsePreset
                    && !action.adaptiveResponsePresetId.trimmed().isEmpty()) {
                    requiredAdaptivePresetIds.insert(action.adaptiveResponsePresetId);
                }
            }
        }
    }
    QJsonArray adaptiveResponsePresets;
    for (const AdaptiveResponsePreset &preset : configuration.adaptiveResponsePresets) {
        if (requiredAdaptivePresetIds.contains(preset.id)) {
            adaptiveResponsePresets.append(ConfigStore::portableAdaptiveResponsePresetToJson(preset));
        }
    }

    ProfileTriggerBindings profileTriggers;
    PovProfileTriggerBindings povProfileTriggers;
    if (includeProfileRelationships) {
        profileTriggers = configuration.profileTriggers;
        for (ProfileTriggerBinding &binding : profileTriggers) {
            if (!resolvedProfileIds.contains(binding.targetProfileId)) binding = {};
        }
        povProfileTriggers = configuration.povProfileTriggers;
        for (auto &hat : povProfileTriggers) {
            for (ProfileTriggerBinding &binding : hat) {
                if (!resolvedProfileIds.contains(binding.targetProfileId)) binding = {};
            }
        }
    }

    QJsonObject payload{
        {u"categories"_qs, categories},
        {u"profiles"_qs, profiles},
        {u"curves"_qs, curves},
        {u"automations"_qs, automations},
        {u"adaptiveResponsePresets"_qs, adaptiveResponsePresets},
        {u"outputLayouts"_qs, layouts},
        {u"profileTriggers"_qs, ConfigStore::portableProfileTriggersToJson(profileTriggers)},
        {u"povProfileTriggers"_qs, ConfigStore::portablePovProfileTriggersToJson(povProfileTriggers)},
    };
    if (kind == PortableConfigurationKind::Pack) {
        payload.insert(u"adaptiveResponseGlobal"_qs,
                       ConfigStore::portableAdaptiveResponseLayerToJson(configuration.adaptiveResponseGlobal));
    }
    if (includeDevices) {
        QJsonArray devices;
        for (const SavedControllerRecord &record : configuration.savedControllers) {
            devices.append(deviceDescriptor(record, includeCalibration));
        }
        payload.insert(u"deviceDescriptors"_qs, devices);
    }

    const SavedControllerRecord *sourceController = nullptr;
    for (const SavedControllerRecord &record : configuration.savedControllers) {
        if (record.id == configuration.activeControllerRecordId) { sourceController = &record; break; }
    }
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? u"2.1.0"_qs : QCoreApplication::applicationVersion();
    return {
        {u"format"_qs, portableFormat(kind)},
        {u"schemaVersion"_qs, kind == PortableConfigurationKind::Profile
            ? kPortableProfileSchemaVersion : kPortablePackSchemaVersion},
        {u"exportedByVersion"_qs, version.left(32)},
        {u"exportedAtUtc"_qs, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {u"manifest"_qs, QJsonObject{{u"name"_qs, name.left(96)},
                                       {u"description"_qs, description.left(512)},
                                       {u"includesDevices"_qs, includeDevices},
                                       {u"includesCalibration"_qs, includeDevices && includeCalibration},
                                       {u"includesAutomations"_qs, includeAutomations},
                                       {u"includesProfileRelationships"_qs, includeProfileRelationships},
                                       {u"includesGameDetection"_qs, includeGameDetection},
                                       {u"includesAdaptiveResponseGlobal"_qs,
                                        kind == PortableConfigurationKind::Pack},
                                       {u"profileCount"_qs, static_cast<int>(resolvedProfileIds.size())},
                                       {u"categoryCount"_qs, static_cast<int>(selectedCategoryIds.size())},
                                       {u"sourceController"_qs, sourceController
                                            ? deviceDescriptor(*sourceController, false) : QJsonObject{}},
                                       {u"dependencySummary"_qs, QJsonObject{
                                            {u"requiredCurves"_qs, curves.size()},
                                            {u"requiredOutputLayouts"_qs, layouts.size()},
                                            {u"requiredAdaptiveResponsePresets"_qs, adaptiveResponsePresets.size()},
                                            {u"relatedAutomations"_qs, automations.size()},
                                            {u"profileControls"_qs, includeProfileRelationships}}}}},
        {u"payload"_qs, payload},
    };
}

bool writeDocument(const QString &fileName, PortableConfigurationKind kind, const QJsonObject &document,
                   QString *error)
{
    if (!hasExpectedExtension(fileName, kind)) {
        setError(error, QString(u"Select a .%1 file"_qs)
            .arg(kind == PortableConfigurationKind::Profile ? u"hbf6profile"_qs : u"hbf6pack"_qs));
        return false;
    }
    QFile file(localPath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, u"Unable to create the selected portable configuration file"_qs);
        return false;
    }
    if (file.write(QJsonDocument(document).toJson(QJsonDocument::Indented)) < 0) {
        setError(error, u"Unable to write the selected portable configuration file"_qs);
        return false;
    }
    return true;
}

bool parseArray(const QJsonObject &payload, const QString &key, int maximum, QJsonArray *result)
{
    const QJsonValue value = payload.value(key);
    if (!value.isArray() || value.toArray().size() > maximum) return false;
    *result = value.toArray();
    return true;
}

QString uniqueImportedName(const MapperConfiguration &configuration, const QString &sourceName,
                           const QString &categoryId)
{
    const QString base = sourceName.left(36).trimmed() + u" (Imported)"_qs;
    if (isProfileNameAvailableInCategory(configuration, base, categoryId)) return base;
    for (int number = 2; number < 1000; ++number) {
        const QString candidate = sourceName.left(31).trimmed()
            + QString(u" (Imported %1)"_qs).arg(number);
        if (isProfileNameAvailableInCategory(configuration, candidate, categoryId)) return candidate;
    }
    return {};
}

QString uniqueImportedCategoryName(const MapperConfiguration &configuration, const QString &sourceName)
{
    const QString base = sourceName.left(51).trimmed() + u" (Imported)"_qs;
    if (isProfileCategoryNameAvailable(configuration, base)) return base;
    for (int number = 2; number < 1000; ++number) {
        const QString candidate = sourceName.left(46).trimmed()
            + QString(u" (Imported %1)"_qs).arg(number);
        if (isProfileCategoryNameAvailable(configuration, candidate)) return candidate;
    }
    return {};
}

QString uniqueImportedCurveName(const MapperConfiguration &configuration, const QString &sourceName)
{
    const auto available = [&configuration](const QString &candidate) {
        return std::none_of(configuration.personalCurvePresets.cbegin(), configuration.personalCurvePresets.cend(),
            [&candidate](const PersonalCurvePreset &curve) {
                return curve.name.compare(candidate, Qt::CaseInsensitive) == 0;
            });
    };
    const QString base = sourceName.left(36).trimmed() + u" (Imported)"_qs;
    if (available(base)) return base;
    for (int number = 2; number < 1000; ++number) {
        const QString candidate = sourceName.left(31).trimmed()
            + QString(u" (Imported %1)"_qs).arg(number);
        if (available(candidate)) return candidate;
    }
    return {};
}

} // namespace

bool ProfilePortability::exportProfile(const MapperConfiguration &configuration, const QString &profileId,
                                       const QString &fileName, QString *error)
{
    const ControllerProfile *profile = findProfile(configuration, profileId);
    if (!profile) { setError(error, u"The selected profile is unavailable"_qs); return false; }
    const ProfileCategory *category = findProfileCategory(configuration, profile->categoryId);
    const QString name = category ? QString(u"%1 / %2"_qs).arg(category->name, profile->name) : profile->name;
    return writeDocument(fileName, PortableConfigurationKind::Profile,
        bundleToJson(configuration, PortableConfigurationKind::Profile, {profileId}, name, {}, false, false,
                     true, true, true), error);
}

bool ProfilePortability::exportPack(const MapperConfiguration &configuration, const QStringList &categoryIds,
                                    const QStringList &profileIds, const QString &name,
                                    const QString &description, bool includeDevices, bool includeCalibration,
                                    bool includeAutomations, bool includeProfileRelationships,
                                    bool includeGameDetection,
                                    const QString &fileName, QString *error)
{
    QSet<QString> selected(profileIds.cbegin(), profileIds.cend());
    for (const QString &categoryId : categoryIds) {
        const ProfileCategory *category = findProfileCategory(configuration, categoryId);
        if (!category) continue;
        for (const QString &profileId : category->profileIds) selected.insert(profileId);
    }
    if (selected.empty()) { setError(error, u"Select at least one profile for the Pack"_qs); return false; }
    const QString trimmedName = name.trimmed().left(96);
    if (trimmedName.isEmpty()) { setError(error, u"Give the Pack a name before exporting"_qs); return false; }
    return writeDocument(fileName, PortableConfigurationKind::Pack,
        bundleToJson(configuration, PortableConfigurationKind::Pack, selected, trimmedName,
                     description, includeDevices, includeCalibration, includeAutomations,
                     includeProfileRelationships, includeGameDetection), error);
}

bool ProfilePortability::inspect(const QString &fileName, PortableConfigurationBundle *bundle, QString *error)
{
    if (!bundle) { setError(error, u"No import target was supplied"_qs); return false; }
    const QString path = localPath(fileName);
    QFileInfo info(path);
    if (!info.exists() || info.size() <= 0 || info.size() > kMaximumPortableFileBytes) {
        setError(error, u"Portable files must be a readable JSON file smaller than 5 MB"_qs); return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { setError(error, u"Unable to read the selected file"_qs); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) { setError(error, u"The selected file is not a valid HOTAS BF6 portable configuration"_qs); return false; }
    const QJsonObject root = document.object();
    const QString format = root.value(u"format"_qs).toString();
    PortableConfigurationKind kind;
    if (format == u"HOTAS-BF6-Profile"_qs) kind = PortableConfigurationKind::Profile;
    else if (format == u"HOTAS-BF6-Pack"_qs) kind = PortableConfigurationKind::Pack;
    else { setError(error, u"This file is not a HOTAS BF6 Profile or Pack"_qs); return false; }
    if (!hasExpectedExtension(path, kind)) {
        setError(error, u"The selected file extension does not match its portable configuration format"_qs);
        return false;
    }
    const int expectedSchema = kind == PortableConfigurationKind::Profile
        ? kPortableProfileSchemaVersion : kPortablePackSchemaVersion;
    const int suppliedSchema = root.value(u"schemaVersion"_qs).toInt();
    if (suppliedSchema != expectedSchema) {
        const QString label = kind == PortableConfigurationKind::Profile ? u"Profile"_qs : u"Pack"_qs;
        setError(error, suppliedSchema > expectedSchema
            ? QString(u"This %1 was created with a newer unsupported format. Update HOTAS BF6 to import it."_qs).arg(label)
            : QString(u"This %1 uses an unsupported older format revision."_qs).arg(label));
        return false;
    }
    const QJsonObject manifest = root.value(u"manifest"_qs).toObject();
    const QJsonObject payload = root.value(u"payload"_qs).toObject();
    QJsonArray categories; QJsonArray profiles; QJsonArray curves; QJsonArray automations; QJsonArray layouts;
    QJsonArray adaptiveResponsePresets;
    if (payload.isEmpty() || !parseArray(payload, u"categories"_qs, 64, &categories)
        || !parseArray(payload, u"profiles"_qs, 256, &profiles)
        || !parseArray(payload, u"curves"_qs, 128, &curves)
        || !parseArray(payload, u"automations"_qs, kMaximumAutomationRules, &automations)
        || !parseArray(payload, u"outputLayouts"_qs, 16, &layouts) || profiles.empty()) {
        setError(error, u"The portable configuration has an invalid or oversized payload"_qs); return false;
    }
    if (payload.contains(u"adaptiveResponsePresets"_qs)
        && !parseArray(payload, u"adaptiveResponsePresets"_qs, 64, &adaptiveResponsePresets)) {
        setError(error, u"The portable configuration has invalid Adaptive Response preset dependencies"_qs); return false;
    }
    PortableConfigurationBundle parsed;
    parsed.kind = kind;
    parsed.name = manifest.value(u"name"_qs).toString().trimmed().left(96);
    parsed.description = manifest.value(u"description"_qs).toString().trimmed().left(512);
    parsed.exporterVersion = root.value(u"exportedByVersion"_qs).toString().trimmed().left(32);
    parsed.exportedAtUtc = root.value(u"exportedAtUtc"_qs).toString().trimmed().left(64);
    parsed.includesDevices = manifest.value(u"includesDevices"_qs).toBool(false);
    parsed.includesCalibration = parsed.includesDevices && manifest.value(u"includesCalibration"_qs).toBool(false);
    parsed.includesAdaptiveResponseGlobal = manifest.value(u"includesAdaptiveResponseGlobal"_qs).toBool(false);
    if (parsed.includesAdaptiveResponseGlobal
        && !ConfigStore::portableAdaptiveResponseLayerFromJson(payload.value(u"adaptiveResponseGlobal"_qs),
                                                                &parsed.adaptiveResponseGlobal)) {
        setError(error, u"The portable configuration has invalid Adaptive Response global defaults"_qs); return false;
    }
    const QJsonObject sourceController = manifest.value(u"sourceController"_qs).toObject();
    if (!sourceController.isEmpty() && !portableDeviceDescriptorIsValid(sourceController, false)) {
        setError(error, u"The portable configuration has invalid source controller metadata"_qs); return false;
    }
    parsed.sourceController = sourceController.toVariantMap();
    QSet<QString> categoryIds;
    for (const QJsonValue &value : categories) {
        ProfileCategory category;
        if (!ConfigStore::portableCategoryFromJson(value.toObject(), &category) || categoryIds.contains(category.id)) {
            setError(error, u"The portable configuration has an invalid category"_qs); return false;
        }
        categoryIds.insert(category.id); parsed.categories.push_back(std::move(category));
    }
    QSet<QString> profileIds;
    for (const QJsonValue &value : profiles) {
        ControllerProfile profile;
        if (!ConfigStore::portableProfileFromJson(value.toObject(), &profile) || profileIds.contains(profile.id)
            || !categoryIds.contains(profile.categoryId)) {
            setError(error, u"The portable configuration has an invalid profile relationship"_qs); return false;
        }
        profileIds.insert(profile.id); parsed.profiles.push_back(std::move(profile));
    }
    for (const ProfileCategory &category : parsed.categories) {
        for (const QString &profileId : category.profileIds) {
            const ControllerProfile *profile = findProfile(MapperConfiguration{.profiles = parsed.profiles}, profileId);
            if (!profile || profile->categoryId != category.id) {
                setError(error, u"The portable configuration has an invalid category membership"_qs); return false;
            }
        }
    }
    QSet<QString> curveIds;
    for (const QJsonValue &value : curves) {
        PersonalCurvePreset curve;
        if (!ConfigStore::portableCurveFromJson(value.toObject(), &curve) || curveIds.contains(curve.id)) {
            setError(error, u"The portable configuration has an invalid curve"_qs); return false;
        }
        curveIds.insert(curve.id);
        parsed.curves.push_back(std::move(curve));
    }
    QSet<QString> adaptivePresetIds;
    QSet<QString> adaptivePresetNames;
    for (const QJsonValue &value : adaptiveResponsePresets) {
        AdaptiveResponsePreset preset;
        if (!ConfigStore::portableAdaptiveResponsePresetFromJson(value.toObject(), &preset)
            || adaptivePresetIds.contains(preset.id) || adaptivePresetNames.contains(preset.name.toCaseFolded())
            || findAdaptiveResponsePreset(MapperConfiguration{}, preset.id)) {
            setError(error, u"The portable configuration has an invalid Adaptive Response preset"_qs); return false;
        }
        adaptivePresetIds.insert(preset.id);
        adaptivePresetNames.insert(preset.name.toCaseFolded());
        parsed.adaptiveResponsePresets.push_back(std::move(preset));
    }
    if (parsed.includesAdaptiveResponseGlobal
        && !adaptiveLayerReferencesAvailable(parsed.adaptiveResponseGlobal, adaptivePresetIds)) {
        setError(error, u"Adaptive Response global defaults reference a missing preset"_qs); return false;
    }
    QSet<QString> automationIds;
    for (const QJsonValue &value : automations) {
        AutomationDefinition automation;
        if (!ConfigStore::portableAutomationFromJson(value.toObject(), &automation)
            || automationIds.contains(automation.id)) {
            setError(error, u"The portable configuration has an invalid Automation rule"_qs); return false;
        }
        automationIds.insert(automation.id);
        parsed.automations.push_back(std::move(automation));
    }
    QSet<QString> layoutIds;
    for (const QJsonValue &value : layouts) {
        VirtualOutputLayout layout;
        if (!ConfigStore::portableOutputLayoutFromJson(value.toObject(), &layout)
            || layoutIds.contains(layout.id)) {
            setError(error, u"The portable configuration has an invalid vJoy requirement"_qs); return false;
        }
        layoutIds.insert(layout.id);
        parsed.outputLayouts.push_back(std::move(layout));
    }
    parsed.profileTriggers = ConfigStore::portableProfileTriggersFromJson(payload.value(u"profileTriggers"_qs));
    parsed.povProfileTriggers = ConfigStore::portablePovProfileTriggersFromJson(payload.value(u"povProfileTriggers"_qs));
    const QJsonArray devices = payload.value(u"deviceDescriptors"_qs).toArray();
    if (devices.size() > 64) { setError(error, u"The portable configuration has too many device descriptors"_qs); return false; }
    for (const QJsonValue &value : devices) {
        const QJsonObject device = value.toObject();
        if (!portableDeviceDescriptorIsValid(device, true)) {
            setError(error, u"The portable configuration has an invalid device descriptor"_qs); return false;
        }
        parsed.deviceDescriptors.append(device.toVariantMap());
    }
    if (parsed.includesDevices != !parsed.deviceDescriptors.isEmpty()) {
        setError(error, u"The portable configuration device manifest does not match its payload"_qs); return false;
    }
    for (const ControllerProfile &profile : parsed.profiles) {
        if (!layoutIds.contains(profile.outputLayoutId)) {
            setError(error, u"A portable profile is missing its required vJoy contract"_qs); return false;
        }
        for (const AxisMapping &axis : profile.axes) {
            if (axis.curve.family == CurveFamily::Personal
                && (!curveIds.contains(axis.curve.presetId)
                    || (!axis.curve.sourcePresetId.isEmpty() && !curveIds.contains(axis.curve.sourcePresetId)))) {
                setError(error, u"A portable profile is missing a required custom curve"_qs); return false;
            }
        }
        if (!adaptiveLayerReferencesAvailable(profile.adaptiveResponse, adaptivePresetIds)) {
            setError(error, u"A portable profile references a missing Adaptive Response preset"_qs); return false;
        }
    }
    for (const ProfileCategory &category : parsed.categories) {
        if (!adaptiveLayerReferencesAvailable(category.adaptiveResponse, adaptivePresetIds)) {
            setError(error, u"A portable category references a missing Adaptive Response preset"_qs); return false;
        }
    }
    const auto isBundleProfile = [&profileIds](const QString &id) {
        return id.isEmpty() || profileIds.contains(id);
    };
    for (const AutomationDefinition &automation : parsed.automations) {
        for (const AutomationConditionDefinition &condition : automation.conditions) {
            if (!isBundleProfile(condition.profileId)) {
                setError(error, u"An Automation references a profile missing from this portable configuration"_qs); return false;
            }
        }
        for (const AutomationActionDefinition &action : automation.actions) {
            if (!isBundleProfile(action.profileId)) {
                setError(error, u"An Automation references a profile missing from this portable configuration"_qs); return false;
            }
            if (action.type == AutomationActionType::AdaptiveResponsePreset
                && !adaptivePresetReferenceAvailable(action.adaptiveResponsePresetId, adaptivePresetIds)) {
                setError(error, u"An Automation references a missing Adaptive Response preset"_qs); return false;
            }
        }
    }
    for (const ProfileTriggerBinding &binding : parsed.profileTriggers) {
        if (profileTriggerBindingEnabled(binding) && !profileIds.contains(binding.targetProfileId)) {
            setError(error, u"A profile control references a profile missing from this portable configuration"_qs); return false;
        }
    }
    for (const auto &hat : parsed.povProfileTriggers) for (const ProfileTriggerBinding &binding : hat) {
        if (profileTriggerBindingEnabled(binding) && !profileIds.contains(binding.targetProfileId)) {
            setError(error, u"A POV profile control references a profile missing from this portable configuration"_qs); return false;
        }
    }
    *bundle = std::move(parsed);
    return true;
}

bool ProfilePortability::apply(MapperConfiguration *configuration, const PortableConfigurationBundle &sourceBundle,
                               const PortableImportOptions &options, QStringList *warnings, QString *error)
{
    if (!configuration) { setError(error, u"No live configuration was supplied"_qs); return false; }
    // Resolve Adaptive Response preset content conflicts before any profile,
    // category, global, or Automation reference is applied. This keeps the
    // import atomic and never silently binds an imported rule to different
    // local preset content that happens to share an ID.
    PortableConfigurationBundle bundle = sourceBundle;
    MapperConfiguration candidate = *configuration;
    PortableCategoryConflictMode categoryMode = options.categoryConflictMode;
    if (!options.mergeCategories && categoryMode == PortableCategoryConflictMode::Merge) {
        categoryMode = PortableCategoryConflictMode::ImportAsNew;
    }
    QHash<QString, QString> presetIdRemap;
    QSet<QString> alreadyResolvedPresetIds;
    for (AdaptiveResponsePreset &source : bundle.adaptiveResponsePresets) {
        const AdaptiveResponsePreset *existing = findAdaptiveResponsePreset(candidate, source.id);
        if (!existing) continue;
        if (sameAdaptiveResponsePresetContent(*existing, source)) {
            alreadyResolvedPresetIds.insert(source.id);
            if (warnings) warnings->append(QString(u"Adaptive Response preset %1 matches local content and was reused"_qs)
                .arg(source.name));
            continue;
        }
        if (options.adaptiveResponsePresetConflictMode
            == PortableAdaptiveResponsePresetConflictMode::KeepLocal) {
            alreadyResolvedPresetIds.insert(source.id);
            if (warnings) warnings->append(QString(u"Adaptive Response preset %1 kept local by explicit choice"_qs)
                .arg(source.name));
            continue;
        }
        if (options.adaptiveResponsePresetConflictMode
            == PortableAdaptiveResponsePresetConflictMode::Replace) {
            auto local = std::find_if(candidate.adaptiveResponsePresets.begin(),
                candidate.adaptiveResponsePresets.end(), [&source](const AdaptiveResponsePreset &preset) {
                    return preset.id == source.id;
                });
            if (local == candidate.adaptiveResponsePresets.end()) {
                setError(error, u"Built-in Adaptive Response presets cannot be replaced by an import"_qs);
                return false;
            }
            *local = source;
            alreadyResolvedPresetIds.insert(source.id);
            if (warnings) warnings->append(QString(u"Adaptive Response preset %1 was replaced by explicit choice"_qs)
                .arg(source.name));
            continue;
        }
        const QString importedId = uniqueImportedAdaptiveResponsePresetId(candidate, source.id);
        if (importedId.isEmpty()) {
            setError(error, u"A unique imported Adaptive Response preset ID could not be created"_qs);
            return false;
        }
        presetIdRemap.insert(source.id, importedId);
        source.id = importedId;
        if (warnings) warnings->append(QString(u"Adaptive Response preset %1 had different local content and was imported as a copy"_qs)
            .arg(source.name));
    }
    if (!presetIdRemap.isEmpty()) {
        remapAdaptiveResponseLayer(&bundle.adaptiveResponseGlobal, presetIdRemap);
        for (AdaptiveResponsePreset &preset : bundle.adaptiveResponsePresets) {
            AdaptiveResponseLayer layer;
            layer.axes = preset.axes;
            remapAdaptiveResponseLayer(&layer, presetIdRemap);
            preset.axes = layer.axes;
        }
        for (ProfileCategory &category : bundle.categories) {
            remapAdaptiveResponseLayer(&category.adaptiveResponse, presetIdRemap);
        }
        for (ControllerProfile &profile : bundle.profiles) {
            remapAdaptiveResponseLayer(&profile.adaptiveResponse, presetIdRemap);
        }
        for (AutomationDefinition &automation : bundle.automations) {
            for (AutomationActionDefinition &action : automation.actions) {
                if (action.type != AutomationActionType::AdaptiveResponsePreset) continue;
                const auto found = presetIdRemap.constFind(action.adaptiveResponsePresetId);
                if (found != presetIdRemap.cend()) action.adaptiveResponsePresetId = *found;
            }
        }
    }
    // Presets are dependencies, not a hidden extra hierarchy. Keep an exact
    // local ID if present; otherwise import it before profiles/categories are
    // copied so every persistent reference remains resolvable.
    for (const AdaptiveResponsePreset &source : bundle.adaptiveResponsePresets) {
        if (alreadyResolvedPresetIds.contains(source.id)) continue;
        if (findAdaptiveResponsePreset(candidate, source.id)) {
            if (warnings) warnings->append(QString(u"Adaptive Response preset %1 was kept local by ID"_qs)
                .arg(source.name));
            continue;
        }
        if (candidate.adaptiveResponsePresets.size() >= 64) {
            setError(error, u"The Pack requires too many Adaptive Response presets"_qs); return false;
        }
        AdaptiveResponsePreset copied = source;
        const auto nameAvailable = [&candidate](const QString &name) {
            return std::none_of(candidate.adaptiveResponsePresets.cbegin(),
                candidate.adaptiveResponsePresets.cend(), [&name](const AdaptiveResponsePreset &existing) {
                    return existing.name.compare(name, Qt::CaseInsensitive) == 0;
                });
        };
        if (!nameAvailable(copied.name)) {
            const QString base = copied.name.left(52);
            for (int suffix = 2; suffix < 100; ++suffix) {
                const QString candidateName = QString(u"%1 (Imported %2)"_qs).arg(base).arg(suffix);
                if (nameAvailable(candidateName)) { copied.name = candidateName; break; }
            }
        }
        if (!nameAvailable(copied.name)) {
            setError(error, u"A unique imported Adaptive Response preset name could not be created"_qs); return false;
        }
        candidate.adaptiveResponsePresets.push_back(std::move(copied));
    }
    if (bundle.includesAdaptiveResponseGlobal) {
        candidate.adaptiveResponseGlobal = bundle.adaptiveResponseGlobal;
    }
    QHash<QString, QString> categoryIds;
    for (const ProfileCategory &source : bundle.categories) {
        QString destinationId;
        bool createdDestination = false;
        if (!options.destinationCategoryId.isEmpty() && bundle.categories.size() == 1) {
            destinationId = options.destinationCategoryId;
        } else {
            ProfileCategory *existing = nullptr;
            for (ProfileCategory &category : candidate.profileCategories) {
                if (category.name.compare(source.name, Qt::CaseInsensitive) == 0) { existing = &category; break; }
            }
            if (existing && categoryMode == PortableCategoryConflictMode::Merge) {
                destinationId = existing->id;
            } else if (existing && categoryMode == PortableCategoryConflictMode::Replace) {
                const ControllerProfile *active = findProfile(candidate, candidate.activeProfileId);
                if (existing->id == generalProfileCategoryId()
                    || (active && existing->id == active->categoryId)
                    || candidate.profileCategories.size() <= 1) {
                    setError(error, u"Replace Category requires a non-active, non-General category and another category to remain"_qs);
                    return false;
                }
                const QString replacingId = existing->id;
                const std::vector<QString> existingProfileIds = existing->profileIds;
                for (const QString &profileId : existingProfileIds) {
                    if (!deleteProfile(candidate, profileId)) {
                        setError(error, u"The existing Category cannot be safely replaced because one of its profiles is protected"_qs);
                        return false;
                    }
                }
                existing = findProfileCategory(candidate, replacingId);
                if (!existing) { setError(error, u"The destination Category became unavailable"_qs); return false; }
                existing->profileIds.clear();
                existing->defaultProfileId.clear();
                existing->lastActiveProfileId.clear();
                existing->executableRules = source.executableRules;
                existing->restoreLastProfile = source.restoreLastProfile;
                existing->enabled = source.enabled;
                existing->adaptiveResponse = source.adaptiveResponse;
                destinationId = existing->id;
                if (warnings) warnings->append(QString(u"Replaced Category %1 after clearing its existing profiles"_qs)
                    .arg(existing->name));
            } else if (existing && categoryMode == PortableCategoryConflictMode::ImportAsNew) {
                const QString importedName = uniqueImportedCategoryName(candidate, source.name);
                if (importedName.isEmpty() || !createProfileCategory(candidate, importedName, &destinationId)) {
                    setError(error, u"A new imported Category name could not be created"_qs); return false;
                }
                createdDestination = true;
                if (warnings) warnings->append(QString(u"Imported Category %1 as %2 to preserve the existing Category"_qs)
                    .arg(source.name, importedName));
            }
        }
        if (destinationId.isEmpty()) {
            if (!createProfileCategory(candidate, source.name, &destinationId)) {
                setError(error, u"A safe destination category could not be created"_qs); return false;
            }
            createdDestination = true;
        }
        ProfileCategory *destination = findProfileCategory(candidate, destinationId);
        if (!destination) { setError(error, u"The destination category is unavailable"_qs); return false; }
        if (createdDestination) {
            destination->executableRules = source.executableRules;
            destination->restoreLastProfile = source.restoreLastProfile;
            destination->enabled = source.enabled;
            destination->adaptiveResponse = source.adaptiveResponse;
        }
        categoryIds.insert(source.id, destinationId);
    }

    QHash<QString, QString> layoutIds;
    for (const VirtualOutputLayout &source : bundle.outputLayouts) {
        QString destinationId;
        if (findOutputLayout(candidate, source.id)) destinationId = source.id;
        if (destinationId.isEmpty()) {
            for (const VirtualOutputLayout &existing : candidate.outputLayouts) {
                if (existing.requirements.deviceId == source.requirements.deviceId) { destinationId = existing.id; break; }
            }
        }
        if (destinationId.isEmpty()) {
            if (candidate.outputLayouts.size() >= 16) { setError(error, u"The Pack requires too many virtual outputs"_qs); return false; }
            VirtualOutputLayout copied = source;
            copied.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            copied.hidHideDeviceInstanceId.clear();
            copied.hidhideManaged = false;
            destinationId = copied.id;
            candidate.outputLayouts.push_back(std::move(copied));
        }
        layoutIds.insert(source.id, destinationId);
    }

    QHash<QString, QString> curveIds;
    for (const PersonalCurvePreset &source : bundle.curves) {
        PersonalCurvePreset copied = source;
        copied.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copied.name = uniqueImportedCurveName(candidate, source.name);
        if (copied.name.isEmpty()) { setError(error, u"A unique imported curve name could not be created"_qs); return false; }
        curveIds.insert(source.id, copied.id);
        candidate.personalCurvePresets.push_back(std::move(copied));
    }

    QHash<QString, QString> profileIds;
    for (const ControllerProfile &source : bundle.profiles) {
        const QString destinationCategoryId = categoryIds.value(source.categoryId);
        if (destinationCategoryId.isEmpty()) { setError(error, u"A profile has no destination category"_qs); return false; }
        ControllerProfile copied = source;
        QString destinationName = copied.name;
        ControllerProfile *conflict = nullptr;
        for (ControllerProfile &existing : candidate.profiles) {
            if (existing.categoryId == destinationCategoryId
                && existing.name.compare(destinationName, Qt::CaseInsensitive) == 0) { conflict = &existing; break; }
        }
        if (conflict) {
            if (options.replaceMatchingProfiles && conflict->id != normalProfileId()
                && conflict->id != candidate.activeProfileId) {
                if (!deleteProfile(candidate, conflict->id)) { setError(error, u"The matching profile cannot be safely replaced"_qs); return false; }
            } else {
                destinationName = uniqueImportedName(candidate, copied.name, destinationCategoryId);
                if (destinationName.isEmpty()) { setError(error, u"A unique imported profile name could not be created"_qs); return false; }
                if (warnings) warnings->append(QString(u"Imported %1 as %2 to avoid overwriting an existing profile"_qs)
                    .arg(copied.name, destinationName));
            }
        }
        const QString oldId = copied.id;
        copied.id = newProfileId();
        copied.name = destinationName;
        copied.categoryId = destinationCategoryId;
        copied.enabled = true;
        const QString mappedLayout = layoutIds.value(copied.outputLayoutId);
        copied.outputLayoutId = mappedLayout.isEmpty() ? candidate.outputLayouts.front().id : mappedLayout;
        for (AxisMapping &axis : copied.axes) {
            if (!axis.curve.presetId.isEmpty() && curveIds.contains(axis.curve.presetId)) {
                axis.curve.presetId = curveIds.value(axis.curve.presetId);
                if (curveIds.contains(axis.curve.sourcePresetId)) axis.curve.sourcePresetId = curveIds.value(axis.curve.sourcePresetId);
            }
        }
        profileIds.insert(oldId, copied.id);
        candidate.profiles.push_back(std::move(copied));
        ProfileCategory *category = findProfileCategory(candidate, destinationCategoryId);
        category->profileIds.push_back(candidate.profiles.back().id);
        if (category->defaultProfileId.isEmpty()) category->defaultProfileId = candidate.profiles.back().id;
    }

    for (const AutomationDefinition &source : bundle.automations) {
        if (candidate.automations.size() >= kMaximumAutomationRules) {
            if (warnings) warnings->append(u"Automation import stopped at the configured 64-rule safety limit"_qs);
            break;
        }
        AutomationDefinition copied = source;
        copied.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (AutomationConditionDefinition &condition : copied.conditions) {
            if (profileIds.contains(condition.profileId)) condition.profileId = profileIds.value(condition.profileId);
            else if (!condition.profileId.isEmpty()) condition.profileId.clear();
        }
        for (AutomationActionDefinition &action : copied.actions) {
            if (profileIds.contains(action.profileId)) action.profileId = profileIds.value(action.profileId);
            else if (!action.profileId.isEmpty()) action.profileId.clear();
        }
        candidate.automations.push_back(std::move(copied));
    }
    for (int index = 0; index < static_cast<int>(bundle.profileTriggers.size()); ++index) {
        const ProfileTriggerBinding &source = bundle.profileTriggers[static_cast<size_t>(index)];
        if (!profileIds.contains(source.targetProfileId)) continue;
        if (index >= static_cast<int>(candidate.profileTriggers.size())) candidate.profileTriggers.resize(index + 1);
        if (!profileTriggerBindingEnabled(candidate.profileTriggers[static_cast<size_t>(index)])) {
            ProfileTriggerBinding copied = source;
            copied.targetProfileId = profileIds.value(source.targetProfileId);
            candidate.profileTriggers[static_cast<size_t>(index)] = copied;
        } else if (warnings) warnings->append(QString(u"Button %1 profile control was kept local"_qs).arg(index + 1));
    }
    for (int hat = 0; hat < static_cast<int>(bundle.povProfileTriggers.size()); ++hat) {
        if (hat >= static_cast<int>(candidate.povProfileTriggers.size())) candidate.povProfileTriggers.resize(hat + 1);
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ProfileTriggerBinding &source = bundle.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (!profileIds.contains(source.targetProfileId)) continue;
            ProfileTriggerBinding &destination = candidate.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (!profileTriggerBindingEnabled(destination)) {
                ProfileTriggerBinding copied = source;
                copied.targetProfileId = profileIds.value(source.targetProfileId);
                destination = copied;
            } else if (warnings) {
                warnings->append(QString(u"POV %1 %2 profile control was kept local"_qs)
                    .arg(hat + 1).arg(direction + 1));
            }
        }
    }

    struct CalibrationApplyPlan {
        QString recordId;
        std::array<Calibration, kPhysicalAxisCount> calibration{};
    };
    std::vector<CalibrationApplyPlan> calibrationPlans;
    if (bundle.includesCalibration && options.applyImportedCalibration) {
        for (int index = 0; index < bundle.deviceDescriptors.size(); ++index) {
            const QVariantMap &descriptor = bundle.deviceDescriptors.at(index).toMap();
            if (!descriptor.contains(u"calibration"_qs)) continue;
            std::array<Calibration, kPhysicalAxisCount> calibration;
            if (!calibrationFromPortableDescriptor(descriptor, &calibration)) {
                setError(error, u"Imported calibration is malformed; no changes were applied"_qs); return false;
            }
            const QList<const SavedControllerRecord *> matches = matchingSavedControllers(candidate, descriptor);
            QString recordId = options.deviceSelections.value(index);
            if (recordId.isEmpty() && matches.size() == 1) recordId = matches.front()->id;
            if (recordId.isEmpty() && matches.size() > 1) {
                setError(error, u"Imported calibration has multiple compatible local controllers; choose one before applying"_qs);
                return false;
            }
            const SavedControllerRecord *selected = nullptr;
            for (const SavedControllerRecord *match : matches) {
                if (match->id == recordId) { selected = match; break; }
            }
            if (!selected) {
                setError(error, u"Imported calibration has no compatible selected local controller"_qs); return false;
            }
            calibrationPlans.push_back({recordId, calibration});
        }
        for (const CalibrationApplyPlan &plan : calibrationPlans) {
            SavedControllerRecord *record = nullptr;
            for (SavedControllerRecord &candidateRecord : candidate.savedControllers) {
                if (candidateRecord.id == plan.recordId) { record = &candidateRecord; break; }
            }
            if (!record) { setError(error, u"Selected local controller is no longer available"_qs); return false; }
            record->calibration = plan.calibration;
            if (candidate.activeControllerRecordId == record->id) candidate.calibration = plan.calibration;
        }
        if (warnings && !calibrationPlans.empty()) {
            warnings->append(u"Imported calibration was applied only to the explicitly matched local controller"_qs);
        }
    }

    bool valid = false;
    MapperConfiguration verified = ConfigStore::fromJson(ConfigStore::toJson(candidate), &valid);
    if (!valid) { setError(error, u"The import plan failed configuration validation; no changes were applied"_qs); return false; }
    *configuration = std::move(verified);
    if (bundle.includesCalibration && !options.applyImportedCalibration && warnings) {
        warnings->append(u"Imported calibration was kept local; select Apply Imported Calibration only after reviewing a matched controller"_qs);
    }
    return true;
}

QVariantMap ProfilePortability::preview(const PortableConfigurationBundle &bundle,
                                        const MapperConfiguration &localConfiguration)
{
    QVariantMap result;
    result.insert(u"kind"_qs, bundle.kind == PortableConfigurationKind::Profile ? u"PROFILE"_qs : u"PACK"_qs);
    result.insert(u"name"_qs, bundle.name);
    result.insert(u"description"_qs, bundle.description);
    result.insert(u"exporterVersion"_qs, bundle.exporterVersion);
    result.insert(u"exportedAtUtc"_qs, bundle.exportedAtUtc);
    result.insert(u"sourceController"_qs, bundle.sourceController);
    result.insert(u"categoryCount"_qs, static_cast<int>(bundle.categories.size()));
    result.insert(u"profileCount"_qs, static_cast<int>(bundle.profiles.size()));
    result.insert(u"automationCount"_qs, static_cast<int>(bundle.automations.size()));
    result.insert(u"curveCount"_qs, static_cast<int>(bundle.curves.size()));
    result.insert(u"adaptiveResponsePresetCount"_qs, static_cast<int>(bundle.adaptiveResponsePresets.size()));
    result.insert(u"includesDevices"_qs, bundle.includesDevices);
    result.insert(u"includesCalibration"_qs, bundle.includesCalibration);
    QVariantList warnings;
    QVariantList conflicts;
    QVariantList categories;
    for (const ProfileCategory &category : bundle.categories) {
        QVariantMap item;
        item.insert(u"name"_qs, category.name);
        item.insert(u"profileCount"_qs, static_cast<int>(category.profileIds.size()));
        item.insert(u"rules"_qs, category.executableRules);
        item.insert(u"restoreLastProfile"_qs, category.restoreLastProfile);
        const bool exists = std::any_of(localConfiguration.profileCategories.cbegin(),
            localConfiguration.profileCategories.cend(), [&category](const ProfileCategory &local) {
                return local.name.compare(category.name, Qt::CaseInsensitive) == 0;
            });
        item.insert(u"exists"_qs, exists);
        item.insert(u"conflict"_qs, exists ? u"CATEGORY EXISTS — choose Merge, Import as New, or Replace"_qs
                                             : u"NEW CATEGORY"_qs);
        if (exists) conflicts.append(QVariantMap{{u"type"_qs, u"Category"_qs},
                                                  {u"name"_qs, category.name},
                                                  {u"resolution"_qs, u"Choose an explicit category action"_qs}});
        categories.append(item);
    }
    QVariantList profiles;
    QVariantList layouts;
    QVariantList curves;
    QVariantList automations;
    QVariantList adaptiveResponsePresets;
    int profileControlCount = 0;
    for (const ProfileTriggerBinding &binding : bundle.profileTriggers) {
        if (profileTriggerBindingEnabled(binding)) ++profileControlCount;
    }
    for (const auto &hat : bundle.povProfileTriggers) for (const ProfileTriggerBinding &binding : hat) {
        if (profileTriggerBindingEnabled(binding)) ++profileControlCount;
    }
    for (const PersonalCurvePreset &curve : bundle.curves) {
        curves.append(QVariantMap{{u"name"_qs, curve.name}, {u"pointCount"_qs,
                                   static_cast<int>(curve.definition.points.size())}});
    }
    for (const AutomationDefinition &automation : bundle.automations) {
        automations.append(QVariantMap{{u"name"_qs, automation.name}, {u"enabled"_qs, automation.enabled},
                                       {u"conditions"_qs, static_cast<int>(automation.conditions.size())},
                                       {u"actions"_qs, static_cast<int>(automation.actions.size())}});
    }
    for (const AdaptiveResponsePreset &preset : bundle.adaptiveResponsePresets) {
        adaptiveResponsePresets.append(QVariantMap{{u"name"_qs, preset.name},
            {u"description"_qs, preset.description}, {u"axisCount"_qs, kPhysicalAxisCount}});
    }
    for (const VirtualOutputLayout &layout : bundle.outputLayouts) {
        int axisCount = 0;
        for (const bool enabled : layout.requirements.axes) if (enabled) ++axisCount;
        layouts.append(QVariantMap{{u"name"_qs, layout.name}, {u"vjoyDevice"_qs, layout.requirements.deviceId},
            {u"axes"_qs, axisCount}, {u"buttons"_qs, layout.requirements.buttons},
            {u"continuousPovs"_qs, layout.requirements.continuousPovs},
            {u"discretePovs"_qs, layout.requirements.discretePovs}});
    }
    for (const ControllerProfile &profile : bundle.profiles) {
        QVariantMap item;
        item.insert(u"name"_qs, profile.name);
        const ProfileCategory *category = findProfileCategory(MapperConfiguration{.profileCategories = bundle.categories}, profile.categoryId);
        item.insert(u"category"_qs, category ? category->name : u"Unknown"_qs);
        int axes = 0; int buttons = 0; int povs = 0; int profileAutomations = 0;
        for (const AxisMapping &axis : profile.axes) if (axis.target != VirtualAxis::Disabled) ++axes;
        for (const ButtonBinding &binding : profile.buttons) if (binding.type == ButtonActionType::VirtualButton) ++buttons;
        for (const auto &hat : profile.povs) for (const ButtonBinding &binding : hat) if (binding.type == ButtonActionType::VirtualButton) ++povs;
        item.insert(u"mappedAxes"_qs, axes); item.insert(u"mappedButtons"_qs, buttons); item.insert(u"povMappings"_qs, povs);
        for (const AutomationDefinition &automation : bundle.automations) {
            if (automationReferencesAnyProfile(automation, QSet<QString>{profile.id})) ++profileAutomations;
        }
        item.insert(u"automationCount"_qs, profileAutomations);
        item.insert(u"curveCount"_qs, static_cast<int>(std::count_if(profile.axes.cbegin(), profile.axes.cend(),
            [](const AxisMapping &axis) { return axis.curve.family != CurveFamily::Linear; })));
        const ProfileCategory *localCategory = nullptr;
        for (const ProfileCategory &candidate : localConfiguration.profileCategories) {
            if (category && candidate.name.compare(category->name, Qt::CaseInsensitive) == 0) {
                localCategory = &candidate; break;
            }
        }
        bool nameConflict = false;
        if (localCategory) {
            for (const ControllerProfile &local : localConfiguration.profiles) {
                if (local.categoryId == localCategory->id
                    && local.name.compare(profile.name, Qt::CaseInsensitive) == 0) { nameConflict = true; break; }
            }
        }
        item.insert(u"nameConflict"_qs, nameConflict);
        if (nameConflict) conflicts.append(QVariantMap{{u"type"_qs, u"Profile"_qs},
            {u"name"_qs, QString(u"%1 / %2"_qs).arg(category ? category->name : u"Unknown"_qs, profile.name)},
            {u"resolution"_qs, u"Import as renamed profile (safe default) or explicitly replace"_qs}});
        profiles.append(item);
    }
    QVariantList devices;
    for (int index = 0; index < bundle.deviceDescriptors.size(); ++index) {
        const QVariantMap descriptor = bundle.deviceDescriptors.at(index).toMap();
        const QList<const SavedControllerRecord *> matches = matchingSavedControllers(localConfiguration, descriptor);
        QVariantList choices;
        for (const SavedControllerRecord *match : matches) {
            choices.append(QVariantMap{{u"id"_qs, match->id}, {u"name"_qs, match->displayName}});
        }
        const QString state = matches.isEmpty() ? u"NO COMPATIBLE LOCAL CONTROLLER"_qs
            : matches.size() == 1 ? u"MATCHED LOCAL CONTROLLER"_qs
                                  : u"MULTIPLE MATCHES — USER SELECTION REQUIRED"_qs;
        devices.append(QVariantMap{{u"index"_qs, index}, {u"name"_qs, descriptor.value(u"name"_qs)},
            {u"axisCount"_qs, descriptor.value(u"axisCount"_qs)},
            {u"buttonCount"_qs, descriptor.value(u"buttonCount"_qs)},
            {u"povCount"_qs, descriptor.value(u"povCount"_qs)}, {u"state"_qs, state},
            {u"choices"_qs, choices}, {u"calibrationAvailable"_qs, descriptor.contains(u"calibration"_qs)}});
        if (matches.size() > 1) warnings.append(QString(u"%1 has ambiguous local controller matches"_qs)
            .arg(descriptor.value(u"name"_qs).toString()));
    }
    result.insert(u"curves"_qs, curves);
    result.insert(u"automations"_qs, automations);
    result.insert(u"adaptiveResponsePresets"_qs, adaptiveResponsePresets);
    result.insert(u"outputLayouts"_qs, layouts);
    result.insert(u"profileControlCount"_qs, profileControlCount);
    result.insert(u"devices"_qs, devices);
    result.insert(u"conflicts"_qs, conflicts);
    result.insert(u"warnings"_qs, warnings);
    result.insert(u"categories"_qs, categories);
    result.insert(u"profiles"_qs, profiles);
    return result;
}

} // namespace hotas
