#include "doctor_deep_repair.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace hotas::doctor {
namespace {

constexpr auto kDeepRepairEngineVersion = "4.0";

QString sha256(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool supportedBuild(const ApprovedPackage &package, quint32 build)
{
    return build >= package.minimumWindowsBuild
        && (package.maximumWindowsBuild == 0 || build <= package.maximumWindowsBuild);
}

bool atLeast(DiagnosisConfidence candidate, DiagnosisConfidence required)
{
    return static_cast<int>(candidate) >= static_cast<int>(required);
}

const Diagnosis *diagnosisFor(const DoctorSession &session, const QStringList &ids,
    DiagnosisConfidence minimum)
{
    for (const Diagnosis &diagnosis : session.diagnoses()) {
        if (ids.contains(diagnosis.id.value()) && diagnosis.contradictingEvidence.isEmpty()
            && atLeast(diagnosis.confidence, minimum)) return &diagnosis;
    }
    return nullptr;
}

QString targetVersionFor(const ReadOnlyDiagnosticSnapshot &snapshot)
{
    if (!snapshot.environment.hidhide.packageVersion.isEmpty()) return snapshot.environment.hidhide.packageVersion;
    if (!snapshot.environment.hidhide.clientVersion.isEmpty()) return snapshot.environment.hidhide.clientVersion;
    for (const DriverPackageObservation &package : snapshot.driverPackages)
        if (package.activeCandidate && !package.version.isEmpty()) return package.version;
    return {};
}

QJsonObject packageJson(const ApprovedPackage &package)
{
    return {{QStringLiteral("packageId"), package.packageId},
        {QStringLiteral("provider"), package.provider},
        {QStringLiteral("version"), package.version},
        {QStringLiteral("architecture"), displayName(package.architecture)},
        {QStringLiteral("channel"), package.channel},
        {QStringLiteral("source"), package.source},
        {QStringLiteral("sourceKind"), displayName(package.sourceKind)},
        {QStringLiteral("expectedSha256"), package.expectedSha256},
        {QStringLiteral("signaturePolicy"), displayName(package.signaturePolicy)},
        {QStringLiteral("signerIdentity"), package.signerIdentity},
        {QStringLiteral("minimumWindowsBuild"), static_cast<int>(package.minimumWindowsBuild)},
        {QStringLiteral("maximumWindowsBuild"), static_cast<int>(package.maximumWindowsBuild)},
        {QStringLiteral("expectedMaximumReboots"), package.expectedMaximumReboots},
        {QStringLiteral("qualification"), static_cast<int>(package.qualification)},
        {QStringLiteral("provenance"), package.provenance}};
}

QString driverStoreDigest(const ReadOnlyDiagnosticSnapshot &snapshot)
{
    QJsonArray packages;
    for (const DriverPackageObservation &package : snapshot.driverPackages) {
        packages.append(QJsonObject{{QStringLiteral("inf"), package.infName},
            {QStringLiteral("provider"), package.provider}, {QStringLiteral("version"), package.version},
            {QStringLiteral("active"), package.activeCandidate}, {QStringLiteral("stale"), package.staleCandidate}});
    }
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(packages).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

RepairOperation operation(const QString &id, RepairOperationKind kind, RepairTargetKind target,
    const QString &identity, const QString &requested, const DoctorCheckId &verify)
{
    RepairOperation result;
    result.id = DoctorOperationId(id);
    result.kind = kind;
    result.targetKind = target;
    result.targetIdentity = identity;
    result.requestedValue = requested;
    result.verification = {{verify, QStringLiteral("independent read-only verification")}};
    return result;
}

struct SelectedRecipe final {
    RepairRecipeId id;
    RepairRiskClass risk = RepairRiskClass::R0Observe;
    QString targetVersion;
    QString requestedReason;
};

std::optional<SelectedRecipe> selectRecipe(const DoctorSession &session,
    const ReadOnlyDiagnosticSnapshot &snapshot, bool explicitUpgradeRequested)
{
    if (diagnosisFor(session, {QStringLiteral("HD-DIAG-RECOVERY-REQUIRED")}, DiagnosisConfidence::Confirmed))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R5-RECOVER-APPROVED-PACKAGE")), RepairRiskClass::R5Recovery,
            targetVersionFor(snapshot), QStringLiteral("Confirmed damaged or failed-repair state")};
    if (diagnosisFor(session, {QStringLiteral("HD-DIAG-SERVICE-REGISTRATION")}, DiagnosisConfidence::VeryHigh))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R2-REPAIR-HIDHIDE-SERVICE")), RepairRiskClass::R2Component,
            targetVersionFor(snapshot), QStringLiteral("Exact HidHide service registration inconsistency")};
    if (diagnosisFor(session, {QStringLiteral("HD-DIAG-FILTER-REGISTRATION")}, DiagnosisConfidence::VeryHigh))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R2-REPAIR-HIDHIDE-FILTER")), RepairRiskClass::R2Component,
            targetVersionFor(snapshot), QStringLiteral("Exact HidHide filter registration inconsistency")};
    if (diagnosisFor(session, {QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT")}, DiagnosisConfidence::VeryHigh))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R3-COMPLETE-DRIVER-REPLACEMENT")), RepairRiskClass::R3Package,
            targetVersionFor(snapshot), QStringLiteral("Installed package is newer than the loaded driver")};
    if (diagnosisFor(session, {QStringLiteral("HD-DIAG-PARTIAL-INSTALL"), QStringLiteral("HD-DIAG-CONTROL-MISSING")}, DiagnosisConfidence::VeryHigh))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R3-REPAIR-INSTALLATION")), RepairRiskClass::R3Package,
            targetVersionFor(snapshot), QStringLiteral("Confirmed partial HidHide installation")};
    if (explicitUpgradeRequested
        && diagnosisFor(session, {QStringLiteral("HD-DIAG-VERSION-MISMATCH")}, DiagnosisConfidence::High))
        return SelectedRecipe{RepairRecipeId(QStringLiteral("HD-R4-UPGRADE-APPROVED-PACKAGE")), RepairRiskClass::R4ApprovedUpgrade,
            targetVersionFor(snapshot), QStringLiteral("Explicit owner-requested approved upgrade")};
    return std::nullopt;
}

} // namespace

QList<ApprovedPackage> ApprovedPackageCatalog::packages()
{
    // The only executable catalog records in Phase 4 are deterministic
    // fixture artifacts.  They exercise the same identity/provenance gates
    // as a production source without falsely claiming a public MSI is
    // qualified simply because its name resembles HidHide.
    return {
        {QStringLiteral("HD-PKG-FIXTURE-OFFICIAL-1.5.230.0-X64"),
            QStringLiteral("fixture-official-nefarius"), QStringLiteral("1.5.230.0"), CpuArchitecture::X64,
            QStringLiteral("lab"), QStringLiteral("fixture://official-nefarius/HidHide_1.5.230.0_x64.msi"),
            ApprovedPackageSourceKind::FixtureDeterministicTest, sha256(QStringLiteral("fixture-hidhide-1.5.230.0-x64")),
            PackageSignaturePolicy::MsiAndDriverSignatureRequired,
            QStringLiteral("Nefarius Software Solutions e.U. [fixture attested]"), 22621, 26199,
            {QStringLiteral("1.5.212.0"), QStringLiteral("1.5.230.0")}, {}, 1,
            RepairQualificationLevel::LabQualified,
            QStringLiteral("Deterministic Phase 4 test artifact with pinned test hash and signer attestation.")},
        {QStringLiteral("HD-PKG-FIXTURE-OFFICIAL-1.5.240.0-X64"),
            QStringLiteral("fixture-official-nefarius"), QStringLiteral("1.5.240.0"), CpuArchitecture::X64,
            QStringLiteral("lab"), QStringLiteral("fixture://official-nefarius/HidHide_1.5.240.0_x64.msi"),
            ApprovedPackageSourceKind::FixtureDeterministicTest, sha256(QStringLiteral("fixture-hidhide-1.5.240.0-x64")),
            PackageSignaturePolicy::MsiAndDriverSignatureRequired,
            QStringLiteral("Nefarius Software Solutions e.U. [fixture attested]"), 22621, 26199,
            {QStringLiteral("1.5.230.0")}, {QStringLiteral("1.5.240.0")}, 1,
            RepairQualificationLevel::LabQualified,
            QStringLiteral("Deterministic Phase 4 approved-upgrade test artifact with pinned provenance.")},
    };
}

std::optional<ApprovedPackage> ApprovedPackageCatalog::find(const QString &packageId)
{
    for (const ApprovedPackage &package : packages())
        if (package.packageId.compare(packageId, Qt::CaseInsensitive) == 0) return package;
    return std::nullopt;
}

std::optional<ApprovedPackage> ApprovedPackageCatalog::selectFor(const DoctorEnvironment &environment,
    const QString &targetVersion, bool upgrade)
{
    QList<ApprovedPackage> candidates;
    for (const ApprovedPackage &package : packages()) {
        if (package.provider == environment.hidhide.provider
            && package.architecture == environment.platform.nativeArchitecture
            && supportedBuild(package, environment.platform.build)
            && package.qualification != RepairQualificationLevel::Retired) candidates.append(package);
    }
    if (candidates.isEmpty()) return std::nullopt;
    if (upgrade) {
        std::sort(candidates.begin(), candidates.end(), [](const ApprovedPackage &left, const ApprovedPackage &right) {
            return left.version > right.version;
        });
        return candidates.first();
    }
    for (const ApprovedPackage &candidate : candidates)
        if (candidate.version == targetVersion) return candidate;
    return std::nullopt;
}

PackageValidationResult ApprovedPackageCatalog::validate(const ApprovedPackage &package,
    const DoctorEnvironment &environment, const QString &observedHash, const QString &observedSigner,
    const QString &observedVersion, CpuArchitecture observedArchitecture, const QString &observedSource)
{
    PackageValidationResult result;
    if (package.provider != environment.hidhide.provider) { result.reason = QStringLiteral("Package provider does not match the diagnosed HidHide provider."); return result; }
    result.checks.append(QStringLiteral("provider exact"));
    if (package.architecture != environment.platform.nativeArchitecture || observedArchitecture != package.architecture) {
        result.reason = QStringLiteral("Package architecture does not match the measured Windows architecture."); return result;
    }
    result.checks.append(QStringLiteral("architecture exact"));
    if (!supportedBuild(package, environment.platform.build)) { result.reason = QStringLiteral("Package is not qualified for this Windows build."); return result; }
    result.checks.append(QStringLiteral("Windows build qualified"));
    if (observedVersion != package.version) { result.reason = QStringLiteral("Package metadata version does not match the catalog entry."); return result; }
    result.checks.append(QStringLiteral("version exact"));
    if (observedHash.compare(package.expectedSha256, Qt::CaseInsensitive) != 0) { result.reason = QStringLiteral("Package SHA-256 does not match the approved catalog entry."); return result; }
    result.checks.append(QStringLiteral("SHA-256 exact"));
    if (observedSigner != package.signerIdentity) { result.reason = QStringLiteral("Package signer does not match the approved catalog entry."); return result; }
    result.checks.append(QStringLiteral("signature and signer exact"));
    if (observedSource != package.source) { result.reason = QStringLiteral("Package source/provenance does not match the approved catalog entry."); return result; }
    result.checks.append(QStringLiteral("source/provenance exact"));
    result.valid = true;
    result.reason = QStringLiteral("Approved package identity, provenance, hash, signer, version, architecture, and platform qualification verified.");
    return result;
}

RepairPlanProposal DeepRepairPlanner::propose(const DoctorSession &session,
    const ReadOnlyDiagnosticSnapshot &snapshot, bool ownerLabMode, bool explicitUpgradeRequested) const
{
    RepairPlanProposal proposal;
    const std::optional<SelectedRecipe> selected = selectRecipe(session, snapshot, explicitUpgradeRequested);
    if (!selected) {
        proposal.status = RepairProposalStatus::NotApplicable;
        proposal.reason = explicitUpgradeRequested
            ? QStringLiteral("No confirmed deep-repair diagnosis supports the explicitly requested upgrade.")
            : QStringLiteral("No confirmed R2-R5 recipe is applicable; a newer version alone never creates an upgrade plan.");
        return proposal;
    }
    if (!snapshot.contradictions.isEmpty()) {
        proposal.status = RepairProposalStatus::Blocked;
        proposal.reason = QStringLiteral("Contradictory evidence blocks deep repair planning.");
        return proposal;
    }
    if (snapshot.environment.platform.nativeArchitecture == CpuArchitecture::Unknown
        || !snapshot.environment.capabilities.helperArchitectureCompatible) {
        proposal.status = RepairProposalStatus::Blocked;
        proposal.reason = QStringLiteral("Deep repair requires a measured compatible helper and native architecture.");
        return proposal;
    }
    const std::optional<HidHideConfigurationSnapshot> configuration = RepairPlanner::configurationFrom(snapshot);
    if (!configuration) {
        proposal.status = RepairProposalStatus::Blocked;
        proposal.reason = QStringLiteral("Deep repair requires a complete read-only HidHide configuration backup before mutation.");
        return proposal;
    }
    const bool packageRequired = selected->risk != RepairRiskClass::R2Component;
    std::optional<ApprovedPackage> package;
    if (packageRequired) {
        package = ApprovedPackageCatalog::selectFor(snapshot.environment, selected->targetVersion,
            selected->risk == RepairRiskClass::R4ApprovedUpgrade);
        if (!package) {
            proposal.status = RepairProposalStatus::Blocked;
            proposal.reason = QStringLiteral("No exact approved, pinned package is catalogued for this provider, version, architecture, and Windows build. No package action is available.");
            return proposal;
        }
        if (selected->risk == RepairRiskClass::R4ApprovedUpgrade
            && !package->upgradeFromVersions.contains(selected->targetVersion)) {
            proposal.status = RepairProposalStatus::Blocked;
            proposal.reason = QStringLiteral("The approved package does not allow an upgrade from the measured package version.");
            return proposal;
        }
    }
    const std::optional<RepairRecipe> recipe = RepairRecipeRegistry::recipe(selected->id);
    if (!recipe) {
        proposal.status = RepairProposalStatus::Blocked;
        proposal.reason = QStringLiteral("The selected deep-repair recipe is not registered.");
        return proposal;
    }
    RepairPlan plan;
    plan.id = RepairPlanId(QStringLiteral("REPAIR-PLAN-") + sha256(session.id().value() + selected->id.value()).left(24).toUpper());
    plan.sessionId = session.id();
    plan.recipeId = recipe->id;
    plan.recipeVersion = recipe->version;
    plan.title = recipe->title;
    plan.description = recipe->description;
    plan.riskClass = recipe->riskClass;
    plan.qualification = recipe->qualification;
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.elevationRequired = true;
    plan.restartRequired = recipe->restartRequired;
    plan.maximumReboots = recipe->maxReboots;
    plan.estimatedSeconds = recipe->estimatedSeconds;
    plan.expectedPreState = configuration->stableJson();
    plan.preconditionFingerprint = configuration->fingerprint();
    plan.expectedPostState = QStringLiteral("Deep package/component post-state is verified only by post-operation read-back.");
    plan.expectedPostFingerprint = {};
    plan.preconditions = {{QStringLiteral("configuration.sha256"), plan.preconditionFingerprint},
        {QStringLiteral("provider"), snapshot.environment.hidhide.provider},
        {QStringLiteral("windows.build"), QString::number(snapshot.environment.platform.build)},
        {QStringLiteral("architecture"), displayName(snapshot.environment.platform.nativeArchitecture)},
        {QStringLiteral("driverStore.sha256"), driverStoreDigest(snapshot)}};
    QJsonObject deep{{QStringLiteral("engineVersion"), QLatin1String(kDeepRepairEngineVersion)},
        {QStringLiteral("recipeReason"), selected->requestedReason},
        {QStringLiteral("driverStoreBeforeDigest"), driverStoreDigest(snapshot)},
        {QStringLiteral("installedVsLoaded"), QJsonObject{{QStringLiteral("installedPackage"), snapshot.environment.hidhide.packageVersion},
             {QStringLiteral("loadedDriver"), snapshot.environment.hidhide.driverVersion}}},
        {QStringLiteral("reboot"), QJsonObject{{QStringLiteral("required"), plan.restartRequired},
             {QStringLiteral("maximumCount"), plan.maximumReboots}, {QStringLiteral("observeFirstAfterRestart"), true}}},
        {QStringLiteral("configurationReconciliation"), QStringLiteral("Compare fresh API read-back to backup; classify preserved, restoration required, migration, incompatible legacy entry, or conflict.")},
        {QStringLiteral("rollback"), QStringLiteral("Verified rollback assets must remain locally available before destructive package mutation.")},
        {QStringLiteral("recovery"), QStringLiteral("Recovery is separately planned and separately authorized; it never inherits forward-repair authorization.")}};
    if (package) {
        deep.insert(QStringLiteral("package"), packageJson(*package));
        plan.preconditions.append({QStringLiteral("package.id"), package->packageId});
        plan.preconditions.append({QStringLiteral("package.sha256"), package->expectedSha256});
        plan.preconditions.append({QStringLiteral("package.signer"), package->signerIdentity});
    }
    plan.deepRepair = deep;
    if (selected->risk == RepairRiskClass::R2Component) {
        if (selected->id.value() == QStringLiteral("HD-R2-REPAIR-HIDHIDE-SERVICE"))
            plan.operations.append(operation(QStringLiteral("OP-R2-SERVICE-REPAIR"), RepairOperationKind::RepairExactServiceConfiguration,
                RepairTargetKind::HidHideService, QStringLiteral("HidHide"), QStringLiteral("restore catalogued service registration"), DoctorCheckId(QStringLiteral("HD-DRV-003"))));
        else
            plan.operations.append(operation(QStringLiteral("OP-R2-FILTER-REPAIR"), RepairOperationKind::RepairExactFilterRegistration,
                RepairTargetKind::HidHideFilterRegistration, QStringLiteral("HidHideFilterRegistration"), QStringLiteral("preserve unrelated filter order and restore exact HidHide registration"), DoctorCheckId(QStringLiteral("HD-DRV-009"))));
    } else {
        const QString packageId = package->packageId;
        plan.operations.append(operation(QStringLiteral("OP-DEEP-PACKAGE-VALIDATE"), RepairOperationKind::ValidateApprovedPackage,
            RepairTargetKind::ApprovedPackage, packageId, QStringLiteral("verify immutable identity immediately before use"), DoctorCheckId(QStringLiteral("HD-PKG-011"))));
        plan.operations.append(operation(QStringLiteral("OP-DEEP-PACKAGE-STAGE"), RepairOperationKind::StageApprovedPackage,
            RepairTargetKind::ApprovedPackage, packageId, QStringLiteral("stage verified package and rollback asset"), DoctorCheckId(QStringLiteral("HD-PKG-005"))));
        if (selected->risk == RepairRiskClass::R5Recovery)
            plan.operations.append(operation(QStringLiteral("OP-R5-REMOVE-INACTIVE"), RepairOperationKind::RemoveSpecificInactiveHidHidePackage,
                RepairTargetKind::InactiveHidHidePackage, packageId, QStringLiteral("remove only exact inactive HidHide package after target and rollback package are available"), DoctorCheckId(QStringLiteral("HD-PKG-005"))));
        plan.operations.append(operation(QStringLiteral("OP-DEEP-PACKAGE-INSTALL"), RepairOperationKind::InstallApprovedHidHidePackage,
            RepairTargetKind::ApprovedPackage, packageId, QStringLiteral("install exact approved package using provider-specific typed operation"), DoctorCheckId(QStringLiteral("HD-DRV-013"))));
        if (plan.restartRequired)
            plan.operations.append(operation(QStringLiteral("OP-DEEP-RESTART-BOUNDARY"), RepairOperationKind::RequestSystemRestart,
                RepairTargetKind::RebootBoundary, QStringLiteral("WindowsRestart"), QStringLiteral("persist continuation before user-authorized restart"), DoctorCheckId(QStringLiteral("HD-SYS-010"))));
        plan.operations.append(operation(QStringLiteral("OP-DEEP-CONFIG-RECONCILE"), RepairOperationKind::ReconcileHidHideConfiguration,
            RepairTargetKind::TransactionSnapshot, QStringLiteral("DeepRecoverySnapshot"), QStringLiteral("observe then reconcile backed-up configuration without overwriting conflicts"), DoctorCheckId(QStringLiteral("HD-CFG-024"))));
    }
    plan.unchangedCollateral = {QStringLiteral("Unrelated HidHide configuration entries remain preserved."),
        QStringLiteral("Historical Driver Store packages remain unless an exact inactive package is explicitly planned."),
        QStringLiteral("No vendor device driver, USB topology, Secure Boot, Memory Integrity, or signing policy is changed.")};
    plan.integrityDigest = RepairHelperContract::seal(plan);
    proposal.recipe = *recipe;
    proposal.plan = plan;
    proposal.before = *configuration;
    proposal.after = *configuration;
    proposal.collateralPreserved = plan.unchangedCollateral;
    proposal.status = ownerLabMode ? RepairProposalStatus::AvailableForOwnerLab : RepairProposalStatus::IdentifiedButNotFieldQualified;
    proposal.reason = ownerLabMode
        ? QStringLiteral("Lab-qualified deep repair plan is available for explicit owner authorization only.")
        : QStringLiteral("Deep repair is identified but remains LabQualified; normal mode stays read-only.");
    return proposal;
}

QString displayName(ApprovedPackageSourceKind source)
{
    switch (source) {
    case ApprovedPackageSourceKind::InstalledValidatedCache: return QStringLiteral("Installed validated cache");
    case ApprovedPackageSourceKind::OfficialSignedRelease: return QStringLiteral("Known official signed release");
    case ApprovedPackageSourceKind::HotasQualifiedProvider: return QStringLiteral("HOTAS-qualified signed provider");
    case ApprovedPackageSourceKind::FixtureDeterministicTest: return QStringLiteral("Deterministic test fixture only");
    }
    return QStringLiteral("Unknown source");
}

QString displayName(PackageSignaturePolicy policy)
{
    switch (policy) {
    case PackageSignaturePolicy::AuthenticodeRequired: return QStringLiteral("Authenticode signature required");
    case PackageSignaturePolicy::MsiAndDriverSignatureRequired: return QStringLiteral("MSI and driver signatures required");
    }
    return QStringLiteral("Signature policy unavailable");
}

} // namespace hotas::doctor
