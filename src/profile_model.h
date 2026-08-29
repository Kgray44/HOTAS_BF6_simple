#pragma once

#include "mapping_types.h"

namespace hotas {

// Pure configuration operations used by the UI and low-frequency services.
// They do not persist, enumerate processes, or touch hardware.
QString categoryProfileLabel(const MapperConfiguration &configuration, const QString &profileId);
bool createProfileCategory(MapperConfiguration &configuration, const QString &name,
                           QString *createdId = nullptr);
bool renameProfileCategory(MapperConfiguration &configuration, const QString &categoryId,
                           const QString &name);
bool deleteProfileCategory(MapperConfiguration &configuration, const QString &categoryId);
bool createProfile(MapperConfiguration &configuration, const QString &name,
                   const QString &startFromId = {}, QString *createdId = nullptr);
bool createProfileInCategory(MapperConfiguration &configuration, const QString &name,
                             const QString &categoryId, const QString &startFromId = {},
                             QString *createdId = nullptr);
bool cloneProfile(MapperConfiguration &configuration, const QString &profileId,
                  QString *createdId = nullptr);
bool duplicateProfileToCategory(MapperConfiguration &configuration, const QString &profileId,
                                const QString &name, const QString &categoryId,
                                QString *createdId = nullptr);
bool renameProfile(MapperConfiguration &configuration, const QString &profileId,
                   const QString &name);
bool moveProfileToCategory(MapperConfiguration &configuration, const QString &profileId,
                           const QString &categoryId);
bool deleteProfile(MapperConfiguration &configuration, const QString &profileId);
bool activateProfile(MapperConfiguration &configuration, const QString &profileId);
bool activateCategoryProfile(MapperConfiguration &configuration, const QString &categoryId,
                             QString *activatedProfileId = nullptr);
QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName);
QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName,
                               const QString &categoryId);

} // namespace hotas
