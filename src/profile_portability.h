#pragma once

#include "mapping_types.h"

#include <QVariantMap>

namespace hotas {

enum class PortableConfigurationKind {
    Profile,
    Pack,
};

// Parsed, validated configuration that has not yet touched the live mapper.
// The bundle carries only durable configuration and is applied to a copied
// MapperConfiguration so a failed import cannot partially alter user data.
struct PortableConfigurationBundle {
    PortableConfigurationKind kind = PortableConfigurationKind::Profile;
    QString name;
    QString description;
    QString exporterVersion;
    std::vector<ProfileCategory> categories;
    std::vector<ControllerProfile> profiles;
    std::vector<PersonalCurvePreset> curves;
    std::vector<AutomationDefinition> automations;
    std::vector<VirtualOutputLayout> outputLayouts;
    ProfileTriggerBindings profileTriggers;
    PovProfileTriggerBindings povProfileTriggers;
    QVariantList deviceDescriptors;
    bool includesDevices = false;
    bool includesCalibration = false;
};

struct PortableImportOptions {
    QString destinationCategoryId;
    bool replaceMatchingProfiles = false;
    bool mergeCategories = true;
};

class ProfilePortability final {
public:
    static bool exportProfile(const MapperConfiguration &configuration, const QString &profileId,
                              const QString &fileName, QString *error = nullptr);
    static bool exportPack(const MapperConfiguration &configuration, const QStringList &categoryIds,
                           const QStringList &profileIds, const QString &name,
                           const QString &description, bool includeDevices, bool includeCalibration,
                           const QString &fileName, QString *error = nullptr);
    static bool inspect(const QString &fileName, PortableConfigurationBundle *bundle,
                        QString *error = nullptr);
    static bool apply(MapperConfiguration *configuration, const PortableConfigurationBundle &bundle,
                      const PortableImportOptions &options, QStringList *warnings = nullptr,
                      QString *error = nullptr);
    static QVariantMap preview(const PortableConfigurationBundle &bundle,
                               const MapperConfiguration &localConfiguration);
};

} // namespace hotas
