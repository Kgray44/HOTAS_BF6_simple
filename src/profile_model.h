#pragma once

#include "mapping_types.h"

namespace hotas {

// Pure configuration operations used by the UI now and physical button
// actions later. They do not persist or touch hardware.
bool createProfile(MapperConfiguration &configuration, const QString &name,
                   const QString &startFromId = {}, QString *createdId = nullptr);
bool cloneProfile(MapperConfiguration &configuration, const QString &profileId,
                  QString *createdId = nullptr);
bool renameProfile(MapperConfiguration &configuration, const QString &profileId,
                   const QString &name);
bool deleteProfile(MapperConfiguration &configuration, const QString &profileId);
bool activateProfile(MapperConfiguration &configuration, const QString &profileId);
QString uniqueCloneProfileName(const MapperConfiguration &configuration, const QString &sourceName);

} // namespace hotas
