#include "profile_model.h"

namespace hotas {

QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName)
{
    const QString base = sourceName.left(40).trimmed();
    for (int copy = 2; copy < 1000; ++copy) {
        const QString candidate = QString(u"%1 %2"_qs).arg(base).arg(copy);
        if (isProfileNameAvailable(configuration, candidate)) return candidate;
    }
    return {};
}

bool createProfile(MapperConfiguration &configuration, const QString &name,
                   const QString &startFromId, QString *createdId)
{
    const QString trimmedName = name.trimmed();
    if (!isProfileNameAvailable(configuration, trimmedName)) return false;
    const ControllerProfile *source = findProfile(configuration,
        startFromId.isEmpty() ? configuration.activeProfileId : startFromId);
    if (!source) source = &activeProfile(configuration);
    ControllerProfile created = *source;
    created.id = newProfileId();
    created.name = trimmedName;
    if (createdId) *createdId = created.id;
    configuration.profiles.push_back(std::move(created));
    return true;
}

bool cloneProfile(MapperConfiguration &configuration, const QString &profileId, QString *createdId)
{
    const ControllerProfile *source = findProfile(configuration, profileId);
    if (!source) return false;
    ControllerProfile clone = *source;
    clone.id = newProfileId();
    clone.name = uniqueCloneProfileName(configuration, source->name);
    if (clone.name.isEmpty()) return false;
    if (createdId) *createdId = clone.id;
    configuration.profiles.push_back(std::move(clone));
    return true;
}

bool renameProfile(MapperConfiguration &configuration, const QString &profileId, const QString &name)
{
    ControllerProfile *profile = findProfile(configuration, profileId);
    const QString trimmedName = name.trimmed();
    if (!profile || profile->id == normalProfileId()
        || !isProfileNameAvailable(configuration, trimmedName, profileId)) {
        return false;
    }
    profile->name = trimmedName;
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
        configuration.profiles.erase(iterator);
        return true;
    }
    return false;
}

bool activateProfile(MapperConfiguration &configuration, const QString &profileId)
{
    if (!findProfile(configuration, profileId)) return false;
    configuration.activeProfileId = profileId;
    return true;
}

} // namespace hotas
