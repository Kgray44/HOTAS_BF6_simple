#pragma once

#include "mapping_types.h"

#include <QHash>
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
    QString exportedAtUtc;
    QVariantMap sourceController;
    std::vector<ProfileCategory> categories;
    std::vector<ControllerProfile> profiles;
    std::vector<PersonalCurvePreset> curves;
    std::vector<AdaptiveResponsePreset> adaptiveResponsePresets;
    AdaptiveResponseLayer adaptiveResponseGlobal;
    bool includesAdaptiveResponseGlobal = false;
    std::vector<AutomationDefinition> automations;
    std::vector<VirtualOutputLayout> outputLayouts;
    ProfileTriggerBindings profileTriggers;
    PovProfileTriggerBindings povProfileTriggers;
    QVariantList deviceDescriptors;
    bool includesDevices = false;
    bool includesCalibration = false;
};

enum class PortableCategoryConflictMode {
    Merge,
    ImportAsNew,
    Replace,
};

struct PortableImportOptions {
    QString destinationCategoryId;
    bool replaceMatchingProfiles = false;
    // Retained for source compatibility with the v2.1.0 API. New callers use
    // the explicit mode so a category replacement can never be implicit.
    bool mergeCategories = true;
    PortableCategoryConflictMode categoryConflictMode = PortableCategoryConflictMode::Merge;
    // Descriptor index -> explicitly selected local saved-controller record.
    // IDs never leave this control-plane import plan.
    QHash<int, QString> deviceSelections;
    bool applyImportedCalibration = false;
};

class ProfilePortability final {
public:
    static bool exportProfile(const MapperConfiguration &configuration, const QString &profileId,
                              const QString &fileName, QString *error = nullptr);
    static bool exportPack(const MapperConfiguration &configuration, const QStringList &categoryIds,
                           const QStringList &profileIds, const QString &name,
                           const QString &description, bool includeDevices, bool includeCalibration,
                           bool includeAutomations, bool includeProfileRelationships,
                           bool includeGameDetection,
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
