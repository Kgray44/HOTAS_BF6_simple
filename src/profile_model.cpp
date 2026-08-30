#include "profile_model.h"

#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace hotas {

namespace {

void removeProfileFromCategory(ProfileCategory &category, const QString &profileId)
{
    category.profileIds.erase(std::remove(category.profileIds.begin(), category.profileIds.end(), profileId),
                              category.profileIds.end());
    if (category.defaultProfileId == profileId) category.defaultProfileId.clear();
    if (category.lastActiveProfileId == profileId) category.lastActiveProfileId.clear();
}

void clearProfileReferences(MapperConfiguration &configuration, const QString &profileId)
{
    for (ProfileTriggerBinding &binding : configuration.profileTriggers) {
        if (binding.targetProfileId == profileId) binding = {};
    }
    for (auto &hat : configuration.povProfileTriggers) {
        for (ProfileTriggerBinding &binding : hat) {
            if (binding.targetProfileId == profileId) binding = {};
        }
    }
    for (AutomationDefinition &automation : configuration.automations) {
        for (AutomationConditionDefinition &condition : automation.conditions) {
            if (condition.profileId == profileId) condition.profileId.clear();
        }
        for (AutomationActionDefinition &action : automation.actions) {
            if (action.profileId == profileId) action.profileId.clear();
        }
    }
}

} // namespace

QString categoryProfileLabel(const MapperConfiguration &configuration, const QString &profileId)
{
    if (profileId.isEmpty()) return {};
    const ControllerProfile *profile = findProfile(configuration, profileId);
    if (!profile) return u"Unavailable profile"_qs;
    const ProfileCategory *category = findProfileCategory(configuration, profile->categoryId);
    return category ? QString(u"%1 / %2"_qs).arg(category->name, profile->name) : profile->name;
}

GameCategoryMatch categoryForForegroundExecutable(const MapperConfiguration &configuration,
                                                  const QString &executable)
{
    return categoryForRunningExecutables(configuration, {executable});
}

GameCategoryMatch categoryForRunningExecutables(const MapperConfiguration &configuration,
                                                const QStringList &executables,
                                                const QString &activeCategoryId)
{
    QSet<QString> running;
    for (const QString &executable : executables) {
        const QString basename = QFileInfo(executable.trimmed()).fileName();
        if (!basename.isEmpty()) running.insert(basename.toCaseFolded());
    }
    if (running.isEmpty()) return {};

    const auto categoryMatches = [&running](const ProfileCategory &category) {
        return category.enabled && std::any_of(category.executableRules.cbegin(), category.executableRules.cend(),
            [&running](const QString &rule) {
                return running.contains(QFileInfo(rule.trimmed()).fileName().toCaseFolded());
            });
    };

    // Do not flap merely because another configured game is also running.
    // An active matching category wins. Otherwise the persisted category
    // order is the explicit, stable tie-breaker.
    for (const ProfileCategory &category : configuration.profileCategories) {
        if (category.id == activeCategoryId && categoryMatches(category)) return {category.id, false};
    }
    for (const ProfileCategory &category : configuration.profileCategories) {
        if (categoryMatches(category)) return {category.id, false};
    }
    return {};
}

bool foregroundExecutableChanged(QString *lastExecutable, const QString &currentExecutable)
{
    if (!lastExecutable) return false;
    const QString normalized = currentExecutable.trimmed();
    if (normalized.compare(*lastExecutable, Qt::CaseInsensitive) == 0) return false;
    *lastExecutable = normalized;
    return true;
}

bool createProfileCategory(MapperConfiguration &configuration, const QString &name, QString *createdId)
{
    const QString trimmed = name.trimmed();
    if (!isProfileCategoryNameAvailable(configuration, trimmed)
        || configuration.profileCategories.size() >= 64) return false;
    ProfileCategory category;
    category.id = newProfileCategoryId();
    category.name = trimmed;
    if (createdId) *createdId = category.id;
    configuration.profileCategories.push_back(std::move(category));
    return true;
}

bool renameProfileCategory(MapperConfiguration &configuration, const QString &categoryId, const QString &name)
{
    ProfileCategory *category = findProfileCategory(configuration, categoryId);
    const QString trimmed = name.trimmed();
    if (!category || !isProfileCategoryNameAvailable(configuration, trimmed, categoryId)) return false;
    category->name = trimmed;
    return true;
}

bool deleteProfileCategory(MapperConfiguration &configuration, const QString &categoryId)
{
    if (configuration.profileCategories.size() <= 1) return false;
    for (auto it = configuration.profileCategories.begin(); it != configuration.profileCategories.end(); ++it) {
        if (it->id != categoryId) continue;
        if (!it->profileIds.empty()) return false;
        configuration.profileCategories.erase(it);
        return true;
    }
    return false;
}

QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName)
{
    const QString base = sourceName.left(40).trimmed();
    for (int copy = 2; copy < 1000; ++copy) {
        const QString candidate = QString(u"%1 %2"_qs).arg(base).arg(copy);
        if (isProfileNameAvailable(configuration, candidate)) return candidate;
    }
    return {};
}

QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName,
                               const QString &categoryId)
{
    const QString base = sourceName.left(40).trimmed();
    for (int copy = 2; copy < 1000; ++copy) {
        const QString candidate = QString(u"%1 %2"_qs).arg(base).arg(copy);
        if (isProfileNameAvailableInCategory(configuration, candidate, categoryId)) return candidate;
    }
    return {};
}

bool createProfile(MapperConfiguration &configuration, const QString &name,
                   const QString &startFromId, QString *createdId)
{
    const ControllerProfile *source = findProfile(configuration,
        startFromId.isEmpty() ? configuration.activeProfileId : startFromId);
    if (!source) source = &activeProfile(configuration);
    return createProfileInCategory(configuration, name, source->categoryId, source->id, createdId);
}

bool createProfileInCategory(MapperConfiguration &configuration, const QString &name,
                             const QString &categoryId, const QString &startFromId, QString *createdId)
{
    const QString trimmedName = name.trimmed();
    if (!isProfileNameAvailableInCategory(configuration, trimmedName, categoryId)) return false;
    const ControllerProfile *source = findProfile(configuration,
        startFromId.isEmpty() ? configuration.activeProfileId : startFromId);
    if (!source) source = &activeProfile(configuration);
    ControllerProfile created = *source;
    created.id = newProfileId();
    created.name = trimmedName;
    created.categoryId = categoryId;
    if (createdId) *createdId = created.id;
    ProfileCategory *category = findProfileCategory(configuration, categoryId);
    configuration.profiles.push_back(std::move(created));
    category->profileIds.push_back(configuration.profiles.back().id);
    if (category->defaultProfileId.isEmpty()) category->defaultProfileId = configuration.profiles.back().id;
    return true;
}

bool cloneProfile(MapperConfiguration &configuration, const QString &profileId, QString *createdId)
{
    const ControllerProfile *source = findProfile(configuration, profileId);
    if (!source) return false;
    const QString categoryId = source->categoryId;
    const QString cloneName = uniqueCloneProfileName(configuration, source->name, categoryId);
    return duplicateProfileToCategory(configuration, profileId, cloneName, categoryId, createdId);
}

bool duplicateProfileToCategory(MapperConfiguration &configuration, const QString &profileId,
                                const QString &name, const QString &categoryId, QString *createdId)
{
    const ControllerProfile *source = findProfile(configuration, profileId);
    const QString trimmedName = name.trimmed();
    if (!source || !isProfileNameAvailableInCategory(configuration, trimmedName, categoryId)) return false;
    ControllerProfile clone = *source;
    clone.id = newProfileId();
    clone.name = trimmedName;
    clone.categoryId = categoryId;
    if (createdId) *createdId = clone.id;
    configuration.profiles.push_back(std::move(clone));
    ProfileCategory *category = findProfileCategory(configuration, categoryId);
    category->profileIds.push_back(configuration.profiles.back().id);
    if (category->defaultProfileId.isEmpty()) category->defaultProfileId = configuration.profiles.back().id;
    return true;
}

bool renameProfile(MapperConfiguration &configuration, const QString &profileId, const QString &name)
{
    ControllerProfile *profile = findProfile(configuration, profileId);
    const QString trimmedName = name.trimmed();
    if (!profile || profile->id == normalProfileId()
        || !isProfileNameAvailableInCategory(configuration, trimmedName, profile->categoryId, profileId)) {
        return false;
    }
    profile->name = trimmedName;
    return true;
}

bool moveProfileToCategory(MapperConfiguration &configuration, const QString &profileId,
                           const QString &categoryId)
{
    ControllerProfile *profile = findProfile(configuration, profileId);
    ProfileCategory *destination = findProfileCategory(configuration, categoryId);
    if (!profile || !destination || profile->categoryId == categoryId
        || !isProfileNameAvailableInCategory(configuration, profile->name, categoryId, profileId)) return false;
    if (ProfileCategory *source = findProfileCategory(configuration, profile->categoryId)) {
        removeProfileFromCategory(*source, profileId);
    }
    profile->categoryId = categoryId;
    destination->profileIds.push_back(profileId);
    if (destination->defaultProfileId.isEmpty()) destination->defaultProfileId = profileId;
    return true;
}

bool deleteProfile(MapperConfiguration &configuration, const QString &profileId)
{
    if (profileId == normalProfileId() || profileId == configuration.activeProfileId
        || configuration.profiles.size() <= 1) {
        return false;
    }
    for (auto iterator = configuration.profiles.begin(); iterator != configuration.profiles.end(); ++iterator) {
        if (iterator->id != profileId) continue;
        if (ProfileCategory *category = findProfileCategory(configuration, iterator->categoryId)) {
            removeProfileFromCategory(*category, profileId);
        }
        clearProfileReferences(configuration, profileId);
        configuration.profiles.erase(iterator);
        return true;
    }
    return false;
}

bool activateProfile(MapperConfiguration &configuration, const QString &profileId)
{
    if (!findProfile(configuration, profileId)) return false;
    configuration.activeProfileId = profileId;
    if (const ControllerProfile *profile = findProfile(configuration, profileId)) {
        if (ProfileCategory *category = findProfileCategory(configuration, profile->categoryId)) {
            category->lastActiveProfileId = profileId;
            if (category->defaultProfileId.isEmpty()) category->defaultProfileId = profileId;
        }
    }
    return true;
}

bool activateCategoryProfile(MapperConfiguration &configuration, const QString &categoryId,
                             QString *activatedProfileId)
{
    ProfileCategory *category = findProfileCategory(configuration, categoryId);
    if (!category || !category->enabled) return false;
    QString candidate = category->restoreLastProfile ? category->lastActiveProfileId
                                                      : category->defaultProfileId;
    if (!findProfile(configuration, candidate)) candidate = category->defaultProfileId;
    if (!findProfile(configuration, candidate)) {
        for (const QString &profileId : category->profileIds) {
            const ControllerProfile *profile = findProfile(configuration, profileId);
            if (profile && profile->enabled) { candidate = profileId; break; }
        }
    }
    const ControllerProfile *profile = findProfile(configuration, candidate);
    if (!profile || !profile->enabled || !activateProfile(configuration, candidate)) return false;
    if (activatedProfileId) *activatedProfileId = candidate;
    return true;
}

} // namespace hotas
