#pragma once

#include "doctor_repair_engine.h"

#include <QJsonObject>

namespace hotas::doctor {

// Package metadata is a first-class security boundary.  A catalog record
// names one immutable artifact; it is never a request to fetch "latest".
enum class ApprovedPackageSourceKind { InstalledValidatedCache, OfficialSignedRelease, HotasQualifiedProvider, FixtureDeterministicTest };
enum class PackageSignaturePolicy { AuthenticodeRequired, MsiAndDriverSignatureRequired };

struct ApprovedPackage final {
    QString packageId;
    QString provider;
    QString version;
    CpuArchitecture architecture = CpuArchitecture::Unknown;
    QString channel;
    QString source;
    ApprovedPackageSourceKind sourceKind = ApprovedPackageSourceKind::OfficialSignedRelease;
    QString expectedSha256;
    PackageSignaturePolicy signaturePolicy = PackageSignaturePolicy::MsiAndDriverSignatureRequired;
    QString signerIdentity;
    quint32 minimumWindowsBuild = 0;
    quint32 maximumWindowsBuild = 0;
    QStringList upgradeFromVersions;
    QStringList downgradeFromVersions;
    int expectedMaximumReboots = 1;
    RepairQualificationLevel qualification = RepairQualificationLevel::LabQualified;
    QString provenance;
    // Official release tags and PE file versions need not have the same
    // spelling (the currently approved HidHide asset is v1.5.230.0 but its
    // signed PE reports 1.5.230). Both values are immutable catalog facts.
    QString artifactFileName;
    QString artifactVersion;
    quint64 expectedSize = 0;
    QString rollbackPackageId;
};

struct PackageValidationResult final {
    bool valid = false;
    QString reason;
    QStringList checks;
};

class ApprovedPackageCatalog final {
public:
    static QList<ApprovedPackage> packages();
    static std::optional<ApprovedPackage> find(const QString &packageId);
    static std::optional<ApprovedPackage> selectFor(const DoctorEnvironment &environment,
        const QString &targetVersion, bool upgrade);
    static PackageValidationResult validate(const ApprovedPackage &package,
        const DoctorEnvironment &environment, const QString &observedHash,
        const QString &observedSigner, const QString &observedVersion,
        CpuArchitecture observedArchitecture, const QString &observedSource);
};

class DeepRepairPlanner final {
public:
    // Planning remains read-only.  `explicitUpgradeRequested` is deliberately
    // separate from a version mismatch so a healthy install is never nagged
    // or upgraded merely because a newer catalog record exists.
    RepairPlanProposal propose(const DoctorSession &session,
        const ReadOnlyDiagnosticSnapshot &snapshot, bool ownerLabMode,
        bool explicitUpgradeRequested = false) const;
};

QString displayName(ApprovedPackageSourceKind source);
QString displayName(PackageSignaturePolicy policy);

} // namespace hotas::doctor
