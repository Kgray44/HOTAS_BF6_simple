#include "profile_portability.h"

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

namespace hotas {
namespace {

constexpr qsizetype kMaximumPortableFileBytes = 5 * 1024 * 1024;
constexpr int kPortableSchemaVersion = 1;

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

QJsonObject deviceDescriptor(const SavedControllerRecord &record, bool includeCalibration)
{
    QJsonObject descriptor{
        {u"name"_qs, record.displayName.left(96)},
        {u"productGuid"_qs, record.productGuid.left(96)},
        {u"vendorId"_qs, record.vendorId},
        {u"productId"_qs, record.productId},
        {u"axisCount"_qs, record.axisCount},
        {u"buttonCount"_qs, record.buttonCount},
        {u"povCount"_qs, record.povCount},
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

QJsonObject bundleToJson(const MapperConfiguration &configuration, PortableConfigurationKind kind,
                         const QSet<QString> &selectedProfileIds, const QString &name,
                         const QString &description, bool includeDevices, bool includeCalibration)
{
    QSet<QString> selectedCategoryIds;
    QSet<QString> selectedLayoutIds;
    QSet<QString> selectedCurveIds;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (!selectedProfileIds.contains(profile.id)) continue;
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
            [&selectedProfileIds](const QString &id) { return !selectedProfileIds.contains(id); }),
            category.profileIds.end());
        if (!selectedProfileIds.contains(category.defaultProfileId)) category.defaultProfileId.clear();
        if (!selectedProfileIds.contains(category.lastActiveProfileId)) category.lastActiveProfileId.clear();
        categories.append(ConfigStore::portableCategoryToJson(category));
    }

    QJsonArray profiles;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (selectedProfileIds.contains(profile.id)) profiles.append(ConfigStore::portableProfileToJson(profile));
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
        if (automationReferencesAnyProfile(automation, selectedProfileIds)) {
            automations.append(ConfigStore::portableAutomationToJson(automation));
        }
    }

    ProfileTriggerBindings profileTriggers = configuration.profileTriggers;
    for (ProfileTriggerBinding &binding : profileTriggers) {
        if (!selectedProfileIds.contains(binding.targetProfileId)) binding = {};
    }
    PovProfileTriggerBindings povProfileTriggers = configuration.povProfileTriggers;
    for (auto &hat : povProfileTriggers) {
        for (ProfileTriggerBinding &binding : hat) {
            if (!selectedProfileIds.contains(binding.targetProfileId)) binding = {};
        }
    }

    QJsonObject payload{
        {u"categories"_qs, categories},
        {u"profiles"_qs, profiles},
        {u"curves"_qs, curves},
        {u"automations"_qs, automations},
        {u"outputLayouts"_qs, layouts},
        {u"profileTriggers"_qs, ConfigStore::portableProfileTriggersToJson(profileTriggers)},
        {u"povProfileTriggers"_qs, ConfigStore::portablePovProfileTriggersToJson(povProfileTriggers)},
    };
    if (includeDevices) {
        QJsonArray devices;
        for (const SavedControllerRecord &record : configuration.savedControllers) {
            devices.append(deviceDescriptor(record, includeCalibration));
        }
        payload.insert(u"deviceDescriptors"_qs, devices);
    }

    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? u"2.1.0"_qs : QCoreApplication::applicationVersion();
    return {
        {u"format"_qs, portableFormat(kind)},
        {u"schemaVersion"_qs, kPortableSchemaVersion},
        {u"exportedByVersion"_qs, version.left(32)},
        {u"exportedAtUtc"_qs, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {u"manifest"_qs, QJsonObject{{u"name"_qs, name.left(96)},
                                       {u"description"_qs, description.left(512)},
                                       {u"includesDevices"_qs, includeDevices},
                                       {u"includesCalibration"_qs, includeCalibration},
                                       {u"profileCount"_qs, static_cast<int>(selectedProfileIds.size())},
                                       {u"categoryCount"_qs, static_cast<int>(selectedCategoryIds.size())}}},
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
        bundleToJson(configuration, PortableConfigurationKind::Profile, {profileId}, name, {}, false, false), error);
}

bool ProfilePortability::exportPack(const MapperConfiguration &configuration, const QStringList &categoryIds,
                                    const QStringList &profileIds, const QString &name,
                                    const QString &description, bool includeDevices, bool includeCalibration,
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
                     description, includeDevices, includeCalibration), error);
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
    if (!hasExpectedExtension(path, kind) || root.value(u"schemaVersion"_qs).toInt() != kPortableSchemaVersion) {
        setError(error, u"This portable configuration uses an unsupported file type or schema version"_qs); return false;
    }
    const QJsonObject manifest = root.value(u"manifest"_qs).toObject();
    const QJsonObject payload = root.value(u"payload"_qs).toObject();
    QJsonArray categories; QJsonArray profiles; QJsonArray curves; QJsonArray automations; QJsonArray layouts;
    if (payload.isEmpty() || !parseArray(payload, u"categories"_qs, 64, &categories)
        || !parseArray(payload, u"profiles"_qs, 256, &profiles)
        || !parseArray(payload, u"curves"_qs, 128, &curves)
        || !parseArray(payload, u"automations"_qs, kMaximumAutomationRules, &automations)
        || !parseArray(payload, u"outputLayouts"_qs, 16, &layouts) || profiles.empty()) {
        setError(error, u"The portable configuration has an invalid or oversized payload"_qs); return false;
    }
    PortableConfigurationBundle parsed;
    parsed.kind = kind;
    parsed.name = manifest.value(u"name"_qs).toString().trimmed().left(96);
    parsed.description = manifest.value(u"description"_qs).toString().trimmed().left(512);
    parsed.exporterVersion = root.value(u"exportedByVersion"_qs).toString().trimmed().left(32);
    parsed.includesDevices = manifest.value(u"includesDevices"_qs).toBool(false);
    parsed.includesCalibration = manifest.value(u"includesCalibration"_qs).toBool(false);
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
    for (const QJsonValue &value : curves) {
        PersonalCurvePreset curve;
        if (!ConfigStore::portableCurveFromJson(value.toObject(), &curve)) {
            setError(error, u"The portable configuration has an invalid curve"_qs); return false;
        }
        parsed.curves.push_back(std::move(curve));
    }
    for (const QJsonValue &value : automations) {
        AutomationDefinition automation;
        if (!ConfigStore::portableAutomationFromJson(value.toObject(), &automation)) {
            setError(error, u"The portable configuration has an invalid Automation rule"_qs); return false;
        }
        parsed.automations.push_back(std::move(automation));
    }
    for (const QJsonValue &value : layouts) {
        VirtualOutputLayout layout;
        if (!ConfigStore::portableOutputLayoutFromJson(value.toObject(), &layout)) {
            setError(error, u"The portable configuration has an invalid vJoy requirement"_qs); return false;
        }
        parsed.outputLayouts.push_back(std::move(layout));
    }
    parsed.profileTriggers = ConfigStore::portableProfileTriggersFromJson(payload.value(u"profileTriggers"_qs));
    parsed.povProfileTriggers = ConfigStore::portablePovProfileTriggersFromJson(payload.value(u"povProfileTriggers"_qs));
    const QJsonArray devices = payload.value(u"deviceDescriptors"_qs).toArray();
    if (devices.size() > 64) { setError(error, u"The portable configuration has too many device descriptors"_qs); return false; }
    for (const QJsonValue &value : devices) {
        const QJsonObject device = value.toObject();
        if (device.isEmpty() || device.value(u"name"_qs).toString().size() > 96) {
            setError(error, u"The portable configuration has an invalid device descriptor"_qs); return false;
        }
        parsed.deviceDescriptors.append(device.toVariantMap());
    }
    *bundle = std::move(parsed);
    return true;
}

bool ProfilePortability::apply(MapperConfiguration *configuration, const PortableConfigurationBundle &bundle,
                               const PortableImportOptions &options, QStringList *warnings, QString *error)
{
    if (!configuration) { setError(error, u"No live configuration was supplied"_qs); return false; }
    MapperConfiguration candidate = *configuration;
    QHash<QString, QString> categoryIds;
    for (const ProfileCategory &source : bundle.categories) {
        QString destinationId;
        if (!options.destinationCategoryId.isEmpty() && bundle.categories.size() == 1) {
            destinationId = options.destinationCategoryId;
        } else if (options.mergeCategories) {
            for (const ProfileCategory &existing : candidate.profileCategories) {
                if (existing.name.compare(source.name, Qt::CaseInsensitive) == 0) { destinationId = existing.id; break; }
            }
        }
        if (destinationId.isEmpty()) {
            if (!createProfileCategory(candidate, source.name, &destinationId)) {
                setError(error, u"A safe destination category could not be created"_qs); return false;
            }
            ProfileCategory *created = findProfileCategory(candidate, destinationId);
            created->executableRules = source.executableRules;
            created->restoreLastProfile = source.restoreLastProfile;
            created->enabled = source.enabled;
        }
        if (!findProfileCategory(candidate, destinationId)) { setError(error, u"The destination category is unavailable"_qs); return false; }
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

    bool valid = false;
    MapperConfiguration verified = ConfigStore::fromJson(ConfigStore::toJson(candidate), &valid);
    if (!valid) { setError(error, u"The import plan failed configuration validation; no changes were applied"_qs); return false; }
    *configuration = std::move(verified);
    if (bundle.includesCalibration && warnings) {
        warnings->append(u"Imported calibration was retained for review and was not applied to a local controller"_qs);
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
    result.insert(u"categoryCount"_qs, static_cast<int>(bundle.categories.size()));
    result.insert(u"profileCount"_qs, static_cast<int>(bundle.profiles.size()));
    result.insert(u"automationCount"_qs, static_cast<int>(bundle.automations.size()));
    result.insert(u"curveCount"_qs, static_cast<int>(bundle.curves.size()));
    result.insert(u"includesDevices"_qs, bundle.includesDevices);
    result.insert(u"includesCalibration"_qs, bundle.includesCalibration);
    QVariantList categories;
    for (const ProfileCategory &category : bundle.categories) {
        QVariantMap item;
        item.insert(u"name"_qs, category.name);
        item.insert(u"profileCount"_qs, static_cast<int>(category.profileIds.size()));
        item.insert(u"exists"_qs, std::any_of(localConfiguration.profileCategories.cbegin(),
            localConfiguration.profileCategories.cend(), [&category](const ProfileCategory &local) {
                return local.name.compare(category.name, Qt::CaseInsensitive) == 0;
            }));
        categories.append(item);
    }
    QVariantList profiles;
    for (const ControllerProfile &profile : bundle.profiles) {
        QVariantMap item;
        item.insert(u"name"_qs, profile.name);
        const ProfileCategory *category = findProfileCategory(MapperConfiguration{.profileCategories = bundle.categories}, profile.categoryId);
        item.insert(u"category"_qs, category ? category->name : u"Unknown"_qs);
        int axes = 0; int buttons = 0; int povs = 0;
        for (const AxisMapping &axis : profile.axes) if (axis.target != VirtualAxis::Disabled) ++axes;
        for (const ButtonBinding &binding : profile.buttons) if (binding.type == ButtonActionType::VirtualButton) ++buttons;
        for (const auto &hat : profile.povs) for (const ButtonBinding &binding : hat) if (binding.type == ButtonActionType::VirtualButton) ++povs;
        item.insert(u"mappedAxes"_qs, axes); item.insert(u"mappedButtons"_qs, buttons); item.insert(u"povMappings"_qs, povs);
        profiles.append(item);
    }
    result.insert(u"categories"_qs, categories);
    result.insert(u"profiles"_qs, profiles);
    return result;
}

} // namespace hotas
