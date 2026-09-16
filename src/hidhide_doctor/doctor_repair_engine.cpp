#include "doctor_repair_engine.h"

#include "doctor_deep_repair.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUuid>

#include <windows.h>
#include <sddl.h>

#include <algorithm>

namespace hotas::doctor {
namespace {

QString hash(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString enumText(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

bool boolValue(const QString &value, std::optional<bool> *out)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) { *out = true; return true; }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) { *out = false; return true; }
    return false;
}

const ProtocolObservation *protocol(const ReadOnlyDiagnosticSnapshot &snapshot, const QString &operation)
{
    for (const ProtocolObservation &candidate : snapshot.protocol) {
        if (candidate.operation.compare(operation, Qt::CaseInsensitive) == 0) return &candidate;
    }
    return nullptr;
}

bool readable(const ProtocolObservation *observation)
{
    return observation && (observation->status == DoctorCheckStatus::Healthy
        || observation->status == DoctorCheckStatus::Informational);
}

bool enoughConfidence(DiagnosisConfidence confidence)
{
    return confidence == DiagnosisConfidence::High || confidence == DiagnosisConfidence::VeryHigh
        || confidence == DiagnosisConfidence::Confirmed;
}

QStringList asJsonArray(const QStringList &values)
{
    return values;
}

QJsonArray jsonArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

QJsonObject configurationObject(const HidHideConfigurationSnapshot &snapshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("whitelist"), jsonArray(snapshot.whitelist));
    object.insert(QStringLiteral("blacklist"), jsonArray(snapshot.blacklist));
    object.insert(QStringLiteral("whitelistKnown"), snapshot.whitelistKnown);
    object.insert(QStringLiteral("blacklistKnown"), snapshot.blacklistKnown);
    if (snapshot.active) object.insert(QStringLiteral("active"), *snapshot.active);
    if (snapshot.inverse) object.insert(QStringLiteral("inverse"), *snapshot.inverse);
    object.insert(QStringLiteral("provider"), snapshot.provider);
    object.insert(QStringLiteral("providerVersion"), snapshot.providerVersion);
    return object;
}

QString canonicalJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool hardenJournalRoot(const QString &root, QString *reason)
{
    // Protect the journal directory from inherited broad ACLs.  OW resolves
    // to the current per-user owner; SYSTEM is retained for recovery tooling.
    // New atomic QSaveFile replacements inherit only these object/container
    // ACEs, so a partial or copied-in file cannot broaden journal access.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) {
        if (reason) *reason = QStringLiteral("Could not construct the private repair-journal ACL (Win32 %1).").arg(GetLastError());
        return false;
    }
    const auto freeDescriptor = qScopeGuard([&] { LocalFree(descriptor); });
    if (!SetFileSecurityW(reinterpret_cast<LPCWSTR>(root.utf16()),
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor)) {
        if (reason) *reason = QStringLiteral("Could not apply the private repair-journal ACL (Win32 %1).").arg(GetLastError());
        return false;
    }
    return true;
}

RepairTransactionId newTransactionId()
{
    return RepairTransactionId(QStringLiteral("REPAIR-TX-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());
}

RepairPlanId newPlanId()
{
    return RepairPlanId(QStringLiteral("REPAIR-PLAN-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());
}

QString ownerLabel(ConfigurationOwnership ownership)
{
    return displayName(ownership);
}

ConfigurationOwnership whitelistOwnership(const QString &entry, const DoctorEnvironment &environment)
{
    if (environment.repairIntent.suppliedByHotas && !environment.repairIntent.expectedExecutable.isEmpty()
        && ConfigurationDeltaEngine::semanticKey(entry) == ConfigurationDeltaEngine::semanticKey(environment.repairIntent.expectedExecutable))
        return ConfigurationOwnership::HotasOwned;
    if (entry.contains(QStringLiteral("HidHideDoctor"), Qt::CaseInsensitive)) return ConfigurationOwnership::DoctorOwned;
    if (entry.contains(QStringLiteral("HOTAS BF6"), Qt::CaseInsensitive)) return ConfigurationOwnership::HotasOwned;
    return ConfigurationOwnership::UserOrUnrelated;
}

ConfigurationOwnership blacklistOwnership(const QString &entry, const DoctorEnvironment &environment)
{
    if (environment.repairIntent.expectedPhysicalDeviceIds.contains(entry, Qt::CaseInsensitive)
        || environment.repairIntent.expectedVirtualOutputIds.contains(entry, Qt::CaseInsensitive)) return ConfigurationOwnership::HotasOwned;
    if (entry.contains(QStringLiteral("HidHideDoctor"), Qt::CaseInsensitive)) return ConfigurationOwnership::DoctorOwned;
    return ConfigurationOwnership::UserOrUnrelated;
}

bool containsSemantic(const QStringList &values, const QString &entry)
{
    const QString key = ConfigurationDeltaEngine::semanticKey(entry);
    return std::any_of(values.cbegin(), values.cend(), [&key](const QString &value) {
        return ConfigurationDeltaEngine::semanticKey(value) == key;
    });
}

const Diagnosis *diagnosisFor(const DoctorSession &session, const QStringList &ids)
{
    for (const Diagnosis &diagnosis : session.diagnoses()) {
        if (ids.contains(diagnosis.id.value()) && enoughConfidence(diagnosis.confidence)
            && diagnosis.contradictingEvidence.isEmpty()) return &diagnosis;
    }
    return nullptr;
}

RepairTransaction transactionFor(const RepairPlan &plan, const DoctorEnvironment &environment,
    const DoctorSessionId &sessionId, RepairTransactionState state, const RepairTransactionId &transactionId = {})
{
    RepairTransaction transaction;
    transaction.id = transactionId.isValid() ? transactionId : newTransactionId();
    transaction.sessionId = sessionId;
    transaction.planId = plan.id;
    transaction.recipeId = plan.recipeId;
    transaction.recipeVersion = plan.recipeVersion;
    transaction.riskClass = plan.riskClass;
    transaction.qualification = plan.qualification;
    transaction.provider = environment.hidhide.provider;
    transaction.windowsBuild = environment.platform.build;
    transaction.architecture = displayName(environment.platform.nativeArchitecture);
    transaction.startedAt = QDateTime::currentDateTimeUtc();
    transaction.state = state;
    transaction.preconditions = plan.preconditions;
    transaction.preconditionFingerprint = plan.preconditionFingerprint;
    transaction.expectedPostFingerprint = plan.expectedPostFingerprint;
    transaction.verificationPlan = plan.riskClass == RepairRiskClass::R1Configuration
        ? QStringLiteral("Independent read-back and exact collateral comparison.")
        : QStringLiteral("Observe package, loaded driver, service/filter, direct API, configuration reconciliation, then run a new Doctor diagnostic session.");
    transaction.rollbackPlan = plan.riskClass == RepairRiskClass::R1Configuration
        ? QStringLiteral("Restore the exact captured R1 configuration only if no external change is detected.")
        : QStringLiteral("Use only the locally available, verified rollback package and preserved configuration after conflict-safe revalidation.");
    transaction.rebootBoundary = plan.restartRequired
        ? QStringLiteral("AwaitingReboot — continuation must observe before any follow-up action")
        : QStringLiteral("None — no reboot expected by this recipe");
    transaction.maximumReboots = plan.maximumReboots;
    transaction.deepRepair = plan.deepRepair;
    transaction.continuationState = {{QStringLiteral("stage"), QStringLiteral("NotStarted")},
        {QStringLiteral("observeFirst"), true}, {QStringLiteral("forwardOrRollback"), QStringLiteral("forward")}};
    transaction.separateRecoveryAuthorizationRequired = plan.riskClass == RepairRiskClass::R5Recovery;
    return transaction;
}

QJsonArray preconditionsJson(const QList<RepairPrecondition> &preconditions)
{
    QJsonArray array;
    for (const RepairPrecondition &precondition : preconditions)
        array.append(QJsonObject{{QStringLiteral("key"), precondition.stableKey}, {QStringLiteral("expected"), precondition.expectedValue}});
    return array;
}

QJsonArray operationsJson(const QList<RepairOperationJournalEntry> &operations)
{
    QJsonArray array;
    for (const RepairOperationJournalEntry &operation : operations) {
        QJsonObject object{{QStringLiteral("operationId"), operation.operationId.value()},
            {QStringLiteral("kind"), static_cast<int>(operation.kind)}, {QStringLiteral("targetKind"), static_cast<int>(operation.targetKind)},
            {QStringLiteral("target"), operation.targetIdentity},
            {QStringLiteral("priorValue"), operation.priorValue}, {QStringLiteral("requestedValue"), operation.requestedValue},
            {QStringLiteral("actualPostValue"), operation.actualPostValue}, {QStringLiteral("state"), static_cast<int>(operation.state)},
            {QStringLiteral("rollbackStatus"), operation.rollbackStatus},
            {QStringLiteral("startedAt"), operation.startedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("completedAt"), operation.completedAt.toString(Qt::ISODateWithMs)}};
        if (operation.nativeError) object.insert(QStringLiteral("nativeError"), QJsonObject{{QStringLiteral("domain"), static_cast<int>(operation.nativeError->domain)},
            {QStringLiteral("code"), QString::number(operation.nativeError->code)}, {QStringLiteral("name"), operation.nativeError->symbolicName},
            {QStringLiteral("message"), operation.nativeError->message}});
        array.append(object);
    }
    return array;
}

QJsonObject transactionObject(const RepairTransaction &transaction, bool includeChecksum)
{
    QJsonObject backup{{QStringLiteral("schemaVersion"), transaction.backupManifest.schemaVersion},
        {QStringLiteral("capturedAt"), transaction.backupManifest.capturedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("scope"), transaction.backupManifest.scope}, {QStringLiteral("targetScope"), transaction.backupManifest.targetScope},
        {QStringLiteral("state"), transaction.backupManifest.serializedState},
        {QStringLiteral("sha256"), transaction.backupManifest.sha256}, {QStringLiteral("provider"), transaction.backupManifest.provider},
        {QStringLiteral("repairIntent"), transaction.backupManifest.repairIntent},
        {QStringLiteral("capabilityEvidence"), transaction.backupManifest.capabilityEvidence},
        {QStringLiteral("windowsBuild"), static_cast<int>(transaction.backupManifest.windowsBuild)},
        {QStringLiteral("architecture"), transaction.backupManifest.architecture},
        {QStringLiteral("privacy"), transaction.backupManifest.privacyClassification},
        {QStringLiteral("restoreEligible"), transaction.backupManifest.restoreEligible}};
    if (transaction.schemaVersion >= 3)
        backup.insert(QStringLiteral("deepRecoverySnapshot"), transaction.backupManifest.deepRecoverySnapshot);
    QJsonObject object{{QStringLiteral("schemaVersion"), transaction.schemaVersion}, {QStringLiteral("transactionId"), transaction.id.value()},
        {QStringLiteral("sessionId"), transaction.sessionId.value()}, {QStringLiteral("planId"), transaction.planId.value()},
        {QStringLiteral("recipeId"), transaction.recipeId.value()}, {QStringLiteral("recipeVersion"), transaction.recipeVersion},
        {QStringLiteral("riskClass"), static_cast<int>(transaction.riskClass)}, {QStringLiteral("qualification"), static_cast<int>(transaction.qualification)},
        {QStringLiteral("provider"), transaction.provider}, {QStringLiteral("windowsBuild"), static_cast<int>(transaction.windowsBuild)},
        {QStringLiteral("architecture"), transaction.architecture}, {QStringLiteral("startedAt"), transaction.startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("state"), static_cast<int>(transaction.state)}, {QStringLiteral("preconditions"), preconditionsJson(transaction.preconditions)},
        {QStringLiteral("preconditionFingerprint"), transaction.preconditionFingerprint},
        {QStringLiteral("expectedPostFingerprint"), transaction.expectedPostFingerprint}, {QStringLiteral("backupManifest"), backup},
        {QStringLiteral("operations"), operationsJson(transaction.operations)}, {QStringLiteral("currentOperation"), transaction.currentOperation},
        {QStringLiteral("verificationPlan"), transaction.verificationPlan}, {QStringLiteral("rollbackPlan"), transaction.rollbackPlan},
        {QStringLiteral("rebootBoundary"), transaction.rebootBoundary}, {QStringLiteral("finalStatus"), transaction.finalStatus}};
    if (transaction.schemaVersion >= 3) {
        object.insert(QStringLiteral("rebootCount"), transaction.rebootCount);
        object.insert(QStringLiteral("maximumReboots"), transaction.maximumReboots);
        object.insert(QStringLiteral("continuationState"), transaction.continuationState);
        object.insert(QStringLiteral("deepRepair"), transaction.deepRepair);
        object.insert(QStringLiteral("separateRecoveryAuthorizationRequired"), transaction.separateRecoveryAuthorizationRequired);
    }
    if (includeChecksum) object.insert(QStringLiteral("checksum"), transaction.checksum);
    return object;
}

std::optional<RepairTransaction> transactionFromObject(const QJsonObject &object, QString *reason)
{
    RepairTransaction transaction;
    transaction.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt();
    transaction.id = RepairTransactionId(object.value(QStringLiteral("transactionId")).toString());
    transaction.sessionId = DoctorSessionId(object.value(QStringLiteral("sessionId")).toString());
    transaction.planId = RepairPlanId(object.value(QStringLiteral("planId")).toString());
    transaction.recipeId = RepairRecipeId(object.value(QStringLiteral("recipeId")).toString());
    transaction.recipeVersion = object.value(QStringLiteral("recipeVersion")).toString();
    transaction.riskClass = static_cast<RepairRiskClass>(object.value(QStringLiteral("riskClass")).toInt());
    transaction.qualification = static_cast<RepairQualificationLevel>(object.value(QStringLiteral("qualification")).toInt());
    transaction.provider = object.value(QStringLiteral("provider")).toString();
    transaction.windowsBuild = static_cast<quint32>(object.value(QStringLiteral("windowsBuild")).toInt());
    transaction.architecture = object.value(QStringLiteral("architecture")).toString();
    transaction.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
    transaction.state = static_cast<RepairTransactionState>(object.value(QStringLiteral("state")).toInt());
    transaction.preconditionFingerprint = object.value(QStringLiteral("preconditionFingerprint")).toString();
    transaction.expectedPostFingerprint = object.value(QStringLiteral("expectedPostFingerprint")).toString();
    transaction.verificationPlan = object.value(QStringLiteral("verificationPlan")).toString();
    transaction.rollbackPlan = object.value(QStringLiteral("rollbackPlan")).toString();
    transaction.rebootBoundary = object.value(QStringLiteral("rebootBoundary")).toString();
    transaction.finalStatus = object.value(QStringLiteral("finalStatus")).toString();
    transaction.checksum = object.value(QStringLiteral("checksum")).toString();
    transaction.rebootCount = object.value(QStringLiteral("rebootCount")).toInt();
    transaction.maximumReboots = object.value(QStringLiteral("maximumReboots")).toInt();
    transaction.continuationState = object.value(QStringLiteral("continuationState")).toObject();
    transaction.deepRepair = object.value(QStringLiteral("deepRepair")).toObject();
    transaction.separateRecoveryAuthorizationRequired = object.value(QStringLiteral("separateRecoveryAuthorizationRequired")).toBool();
    for (const QJsonValue &value : object.value(QStringLiteral("preconditions")).toArray()) {
        const QJsonObject condition = value.toObject();
        transaction.preconditions.append({condition.value(QStringLiteral("key")).toString(), condition.value(QStringLiteral("expected")).toString()});
    }
    const QJsonObject backup = object.value(QStringLiteral("backupManifest")).toObject();
    transaction.backupManifest.schemaVersion = backup.value(QStringLiteral("schemaVersion")).toInt(1);
    transaction.backupManifest.capturedAt = QDateTime::fromString(backup.value(QStringLiteral("capturedAt")).toString(), Qt::ISODateWithMs);
    transaction.backupManifest.scope = backup.value(QStringLiteral("scope")).toString();
    transaction.backupManifest.targetScope = backup.value(QStringLiteral("targetScope")).toString();
    transaction.backupManifest.serializedState = backup.value(QStringLiteral("state")).toString();
    transaction.backupManifest.sha256 = backup.value(QStringLiteral("sha256")).toString();
    transaction.backupManifest.provider = backup.value(QStringLiteral("provider")).toString();
    transaction.backupManifest.repairIntent = backup.value(QStringLiteral("repairIntent")).toString();
    transaction.backupManifest.capabilityEvidence = backup.value(QStringLiteral("capabilityEvidence")).toString();
    transaction.backupManifest.windowsBuild = static_cast<quint32>(backup.value(QStringLiteral("windowsBuild")).toInt());
    transaction.backupManifest.architecture = backup.value(QStringLiteral("architecture")).toString();
    transaction.backupManifest.privacyClassification = backup.value(QStringLiteral("privacy")).toString();
    transaction.backupManifest.restoreEligible = backup.value(QStringLiteral("restoreEligible")).toBool();
    transaction.backupManifest.deepRecoverySnapshot = backup.value(QStringLiteral("deepRecoverySnapshot")).toObject();
    transaction.currentOperation = object.value(QStringLiteral("currentOperation")).toInt(-1);
    for (const QJsonValue &value : object.value(QStringLiteral("operations")).toArray()) {
        const QJsonObject item = value.toObject();
        RepairOperationJournalEntry operation;
        operation.transactionId = transaction.id;
        operation.planId = transaction.planId;
        operation.operationId = DoctorOperationId(item.value(QStringLiteral("operationId")).toString());
        operation.kind = static_cast<RepairOperationKind>(item.value(QStringLiteral("kind")).toInt());
        operation.targetKind = static_cast<RepairTargetKind>(item.value(QStringLiteral("targetKind")).toInt());
        operation.targetIdentity = item.value(QStringLiteral("target")).toString();
        operation.priorValue = item.value(QStringLiteral("priorValue")).toString();
        operation.requestedValue = item.value(QStringLiteral("requestedValue")).toString();
        operation.actualPostValue = item.value(QStringLiteral("actualPostValue")).toString();
        operation.state = static_cast<DoctorOperationState>(item.value(QStringLiteral("state")).toInt());
        operation.rollbackStatus = item.value(QStringLiteral("rollbackStatus")).toString();
        operation.startedAt = QDateTime::fromString(item.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
        operation.completedAt = QDateTime::fromString(item.value(QStringLiteral("completedAt")).toString(), Qt::ISODateWithMs);
        const QJsonObject error = item.value(QStringLiteral("nativeError")).toObject();
        if (!error.isEmpty()) operation.nativeError = NativeError{static_cast<NativeErrorDomain>(error.value(QStringLiteral("domain")).toInt()),
            error.value(QStringLiteral("code")).toString().toLongLong(), error.value(QStringLiteral("name")).toString(), error.value(QStringLiteral("message")).toString()};
        transaction.operations.append(std::move(operation));
    }
    if ((transaction.schemaVersion != 2 && transaction.schemaVersion != 3) || !transaction.id.isValid() || !transaction.sessionId.isValid()
        || !transaction.planId.isValid() || !transaction.recipeId.isValid() || transaction.checksum.isEmpty()) {
        if (reason) *reason = QStringLiteral("Repair journal has an invalid schema or required stable identifier.");
        return std::nullopt;
    }
    const QString expected = hash(QJsonDocument(transactionObject(transaction, false)).toJson(QJsonDocument::Compact));
    if (expected != transaction.checksum) {
        if (reason) *reason = QStringLiteral("Repair journal checksum does not match its persisted contents.");
        return std::nullopt;
    }
    return transaction;
}

BackupManifest backupFor(const HidHideConfigurationSnapshot &snapshot, const DoctorEnvironment &environment,
    const RepairPlan &plan)
{
    BackupManifest backup;
    backup.capturedAt = QDateTime::currentDateTimeUtc();
    backup.scope = QStringLiteral("HidHide whitelist, blacklist, cloak, inverse, provider and exact R1 targets");
    backup.targetScope = QStringLiteral("%1 :: %2 typed R1 target(s)")
        .arg(plan.recipeId.value()).arg(plan.operations.size());
    backup.serializedState = snapshot.stableJson();
    backup.sha256 = hash(backup.serializedState.toUtf8());
    backup.provider = snapshot.provider;
    backup.repairIntent = environment.repairIntent.suppliedByHotas
        ? QStringLiteral("HOTAS supplied intent: ") + environment.repairIntent.expectedExecutable
        : QStringLiteral("No HOTAS repair intent supplied");
    backup.capabilityEvidence = QStringLiteral("directProtocol=%1; helperArchitectureCompatible=%2")
        .arg(environment.capabilities.directProtocolAvailable ? QStringLiteral("true") : QStringLiteral("false"),
            environment.capabilities.helperArchitectureCompatible ? QStringLiteral("true") : QStringLiteral("false"));
    backup.windowsBuild = environment.platform.build;
    backup.architecture = displayName(environment.platform.nativeArchitecture);
    backup.privacyClassification = QStringLiteral("Sensitive local diagnostic data; explicit redacted export only.");
    backup.restoreEligible = snapshot.isComplete();
    if (plan.riskClass != RepairRiskClass::R1Configuration) {
        backup.scope = QStringLiteral("Deep HidHide recovery snapshot: configuration, package identity, service/filter intent, driver-store digest, version evidence, reboot/recovery metadata");
        backup.targetScope = QStringLiteral("%1 :: %2 typed deep operation(s)")
            .arg(plan.recipeId.value()).arg(plan.operations.size());
        backup.deepRecoverySnapshot = {{QStringLiteral("configuration"), configurationObject(snapshot)},
            {QStringLiteral("planDeepRepair"), plan.deepRepair},
            {QStringLiteral("preconditionFingerprint"), plan.preconditionFingerprint},
            {QStringLiteral("capturedBeforeMutation"), true}};
    }
    return backup;
}

QList<RepairOperation> restoreOperations(const HidHideConfigurationSnapshot &from,
    const HidHideConfigurationSnapshot &target)
{
    QList<RepairOperation> operations;
    auto appendListChanges = [&operations](const QStringList &current, const QStringList &wanted,
                                  RepairOperationKind add, RepairOperationKind remove, RepairTargetKind targetKind,
                                  const QString &prefix) {
        for (const QString &entry : current) if (!containsSemantic(wanted, entry))
            operations.append({DoctorOperationId(prefix + QStringLiteral("-REMOVE-") + QString::number(operations.size())), remove, targetKind, entry, entry});
        for (const QString &entry : wanted) if (!containsSemantic(current, entry))
            operations.append({DoctorOperationId(prefix + QStringLiteral("-ADD-") + QString::number(operations.size())), add, targetKind, entry, entry});
    };
    appendListChanges(from.whitelist, target.whitelist, RepairOperationKind::AddWhitelistEntry,
        RepairOperationKind::RemoveWhitelistEntry, RepairTargetKind::WhitelistEntry, QStringLiteral("OP-R1-ROLLBACK-WL"));
    appendListChanges(from.blacklist, target.blacklist, RepairOperationKind::AddBlacklistEntry,
        RepairOperationKind::RemoveBlacklistEntry, RepairTargetKind::BlacklistEntry, QStringLiteral("OP-R1-ROLLBACK-BL"));
    if (from.active != target.active && target.active.has_value()) operations.append({DoctorOperationId(QStringLiteral("OP-R1-ROLLBACK-ACTIVE")),
        RepairOperationKind::SetHidHideActive, RepairTargetKind::HidHideActiveState, QStringLiteral("active"), enumText(*target.active)});
    if (from.inverse != target.inverse && target.inverse.has_value()) operations.append({DoctorOperationId(QStringLiteral("OP-R1-ROLLBACK-INVERSE")),
        RepairOperationKind::SetHidHideInverse, RepairTargetKind::HidHideInverseState, QStringLiteral("inverse"), enumText(*target.inverse)});
    return operations;
}

} // namespace

bool HidHideConfigurationSnapshot::isComplete() const
{
    return whitelistKnown && blacklistKnown && active.has_value() && inverse.has_value() && !provider.isEmpty();
}

QString HidHideConfigurationSnapshot::stableJson() const { return canonicalJson(configurationObject(*this)); }
QString HidHideConfigurationSnapshot::fingerprint() const { return hash(stableJson().toUtf8()); }

QString ConfigurationDeltaEngine::semanticKey(const QString &value)
{
    QString result = QDir::fromNativeSeparators(value.trimmed());
    while (result.contains(QStringLiteral("//"))) result.replace(QStringLiteral("//"), QStringLiteral("/"));
    return result.toUpper();
}

ConfigurationDelta ConfigurationDeltaEngine::addExact(const QStringList &current, const QString &entry)
{
    ConfigurationDelta delta;
    delta.resulting = current;
    if (containsSemantic(current, entry)) { delta.unchanged = current; return delta; }
    delta.additions = {entry};
    delta.resulting.append(entry);
    delta.unchanged = current;
    return delta;
}

ConfigurationDelta ConfigurationDeltaEngine::removeExact(const QStringList &current, const QString &entry)
{
    ConfigurationDelta delta;
    for (const QString &candidate : current) {
        if (semanticKey(candidate) == semanticKey(entry)) delta.removals.append(candidate);
        else { delta.resulting.append(candidate); delta.unchanged.append(candidate); }
    }
    return delta;
}

QList<RepairRecipe> RepairRecipeRegistry::recipes()
{
    return {
        {RepairRecipeId(QStringLiteral("HD-R1-ADD-HOTAS-WHITELIST")), QStringLiteral("1.0"),
            QStringLiteral("Add missing HOTAS BF6 application exemption"),
            QStringLiteral("Adds one independently verified HOTAS BF6 executable exemption and preserves every other application entry."),
            RepairRiskClass::R1Configuration, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-MISSING-HOTAS-EXEMPTION")}, DiagnosisConfidence::High,
            {QStringLiteral("GET_WHITELIST"), QStringLiteral("GET_ACTIVE"), QStringLiteral("GET_INVERSE")},
            QStringLiteral("HOTAS launch context with verified executable identity"), QStringLiteral("Full HidHide configuration snapshot"),
            QStringLiteral("Read back whitelist and rerun the affected configuration check."),
            QStringLiteral("Remove only the exact entry if Doctor's expected post-state still exists."), true, false},
        {RepairRecipeId(QStringLiteral("HD-R1-REPLACE-STALE-HOTAS-WHITELIST")), QStringLiteral("1.0"),
            QStringLiteral("Replace stale HOTAS BF6 application exemption"),
            QStringLiteral("Removes one proven stale HOTAS-owned exemption and adds the verified current executable path."),
            RepairRiskClass::R1Configuration, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-STALE-CONFIG")}, DiagnosisConfidence::High,
            {QStringLiteral("GET_WHITELIST")}, QStringLiteral("HOTAS launch context with verified replacement executable"),
            QStringLiteral("Full HidHide configuration snapshot"), QStringLiteral("Read back the exact whitelist delta."),
            QStringLiteral("Restore only the original stale entry after exact post-state comparison."), true, false},
        {RepairRecipeId(QStringLiteral("HD-R1-UNHIDE-HOTAS-VIRTUAL-OUTPUT")), QStringLiteral("1.0"),
            QStringLiteral("Unhide exact HOTAS virtual output"),
            QStringLiteral("Removes only a verified HOTAS virtual output from HidHide's hidden-device list."),
            RepairRiskClass::R1Configuration, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-VIRTUAL-HIDDEN")}, DiagnosisConfidence::High,
            {QStringLiteral("GET_BLACKLIST")}, QStringLiteral("HOTAS launch context with verified virtual-output identity"),
            QStringLiteral("Full HidHide configuration snapshot"), QStringLiteral("Read back blacklist and rerun virtual-output isolation check."),
            QStringLiteral("Restore the exact virtual-output entry only when no external change occurred."), true, false},
        {RepairRecipeId(QStringLiteral("HD-R2-REPAIR-HIDHIDE-SERVICE")), QStringLiteral("1.0"),
            QStringLiteral("Repair exact HidHide service registration"),
            QStringLiteral("Restores only the catalogued HidHide service registration after independent service and package evidence proves the expected state."),
            RepairRiskClass::R2Component, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-SERVICE-REGISTRATION")}, DiagnosisConfidence::VeryHigh,
            {QStringLiteral("GET_ACTIVE")}, QStringLiteral("Confirmed exact HidHide service identity"),
            QStringLiteral("Deep recovery snapshot including service configuration"),
            QStringLiteral("Read back exact HidHide service configuration and control availability."),
            QStringLiteral("Restore captured HidHide service configuration only if its post-state remains unchanged."), true, false, 0, 20},
        {RepairRecipeId(QStringLiteral("HD-R2-REPAIR-HIDHIDE-FILTER")), QStringLiteral("1.0"),
            QStringLiteral("Repair exact HidHide filter registration"),
            QStringLiteral("Restores only a known HidHide filter registration while preserving every unrelated filter and its order."),
            RepairRiskClass::R2Component, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-FILTER-REGISTRATION")}, DiagnosisConfidence::VeryHigh,
            {QStringLiteral("GET_ACTIVE")}, QStringLiteral("Confirmed exact HidHide filter and catalogued expected order"),
            QStringLiteral("Deep recovery snapshot including filter registration"),
            QStringLiteral("Re-read filter ordering and affected device/filter evidence."),
            QStringLiteral("Restore the captured filter position without rebuilding the class list."), true, false, 0, 25},
        {RepairRecipeId(QStringLiteral("HD-R3-REPAIR-INSTALLATION")), QStringLiteral("1.0"),
            QStringLiteral("Repair approved HidHide installation"),
            QStringLiteral("Repairs a confirmed partial HidHide installation using a single approved, verified package and durable restart continuation."),
            RepairRiskClass::R3Package, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-PARTIAL-INSTALL"), QStringLiteral("HD-DIAG-CONTROL-MISSING")}, DiagnosisConfidence::VeryHigh,
            {QStringLiteral("GET_ACTIVE")}, QStringLiteral("Confirmed package/component diagnosis"),
            QStringLiteral("Deep package/configuration/service/filter recovery snapshot"),
            QStringLiteral("Verify installed and loaded package state, protocol, configuration, and a fresh diagnosis session."),
            QStringLiteral("Use locally staged, verified rollback package and saved configuration only after conflict checks."), true, true, 1, 120},
        {RepairRecipeId(QStringLiteral("HD-R3-COMPLETE-DRIVER-REPLACEMENT")), QStringLiteral("1.0"),
            QStringLiteral("Complete approved HidHide driver replacement"),
            QStringLiteral("Completes a confirmed package-newer-than-loaded-driver replacement with an observation-first reboot continuation."),
            RepairRiskClass::R3Package, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT")}, DiagnosisConfidence::VeryHigh,
            {QStringLiteral("GET_ACTIVE"), QStringLiteral("GET_INVERSE")}, QStringLiteral("Confirmed installed-versus-loaded driver mismatch"),
            QStringLiteral("Deep package/configuration/service/filter recovery snapshot"),
            QStringLiteral("Verify target package, loaded driver, control API, configuration reconciliation, and a fresh diagnosis session."),
            QStringLiteral("Retain exact locally verified rollback asset before any replacement boundary."), true, true, 1, 150},
        {RepairRecipeId(QStringLiteral("HD-R4-UPGRADE-APPROVED-PACKAGE")), QStringLiteral("1.0"),
            QStringLiteral("Upgrade to an approved HidHide package"),
            QStringLiteral("Applies an explicitly requested, catalogued version transition; it is never offered solely because a higher version exists."),
            RepairRiskClass::R4ApprovedUpgrade, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-VERSION-MISMATCH")}, DiagnosisConfidence::VeryHigh,
            {QStringLiteral("GET_ACTIVE")}, QStringLiteral("Explicit owner upgrade request with a qualified version transition"),
            QStringLiteral("Deep package/configuration/service/filter recovery snapshot"),
            QStringLiteral("Verify target package, loaded driver, control API, configuration reconciliation, and a fresh diagnosis session."),
            QStringLiteral("Retain a locally verified rollback package before package transition."), true, true, 1, 180},
        {RepairRecipeId(QStringLiteral("HD-R5-RECOVER-APPROVED-PACKAGE")), QStringLiteral("1.0"),
            QStringLiteral("Recover HidHide from a damaged repair state"),
            QStringLiteral("Uses a separate recovery plan and authorization to restore a known-good approved package after a confirmed failed/degraded transaction."),
            RepairRiskClass::R5Recovery, RepairQualificationLevel::LabQualified,
            {QStringLiteral("HD-DIAG-RECOVERY-REQUIRED")}, DiagnosisConfidence::Confirmed,
            {QStringLiteral("GET_ACTIVE")}, QStringLiteral("Confirmed damaged state or explicit owner-selected recovery"),
            QStringLiteral("Complete deep recovery snapshot and locally verified rollback assets"),
            QStringLiteral("Verify package, loaded driver, control API, configuration reconciliation, and a new full diagnosis session."),
            QStringLiteral("Recovery does not inherit forward-repair authorization and may itself require a separately journaled restart rollback."), true, true, 2, 240},
    };
}

std::optional<RepairRecipe> RepairRecipeRegistry::recipe(const RepairRecipeId &id)
{
    for (const RepairRecipe &candidate : recipes()) if (candidate.id.value() == id.value()) return candidate;
    return std::nullopt;
}

std::optional<HidHideConfigurationSnapshot> RepairPlanner::configurationFrom(const ReadOnlyDiagnosticSnapshot &snapshot)
{
    HidHideConfigurationSnapshot configuration;
    configuration.provider = snapshot.environment.hidhide.provider.isEmpty()
        ? QStringLiteral("HidHide WDM control device") : snapshot.environment.hidhide.provider;
    configuration.providerVersion = snapshot.environment.hidhide.driverVersion;
    configuration.observedAt = QDateTime::currentDateTimeUtc();
    const ProtocolObservation *whitelist = protocol(snapshot, QStringLiteral("GET_WHITELIST"));
    const ProtocolObservation *blacklist = protocol(snapshot, QStringLiteral("GET_BLACKLIST"));
    const ProtocolObservation *active = protocol(snapshot, QStringLiteral("GET_ACTIVE"));
    const ProtocolObservation *inverse = protocol(snapshot, QStringLiteral("GET_INVERSE"));
    if (readable(whitelist)) { configuration.whitelistKnown = true; configuration.whitelist = whitelist->multiStringValues; }
    if (readable(blacklist)) { configuration.blacklistKnown = true; configuration.blacklist = blacklist->multiStringValues; }
    if (readable(active)) boolValue(active->value, &configuration.active);
    if (readable(inverse)) boolValue(inverse->value, &configuration.inverse);
    if (!configuration.isComplete()) return std::nullopt;
    return configuration;
}

RepairPlanProposal RepairPlanner::propose(const DoctorSession &session, const ReadOnlyDiagnosticSnapshot &snapshot,
    bool ownerLabMode, bool explicitApprovedUpgradeRequest) const
{
    // Deep planning runs before the R1 ownership/launch-context gates. R2-R5
    // are diagnosis/package driven and must not be mistaken for a HOTAS
    // whitelist repair. A blocked deep plan remains a visible, read-only
    // result rather than falling through to an unrelated R1 suggestion.
    const RepairPlanProposal deepProposal = DeepRepairPlanner().propose(session, snapshot, ownerLabMode, explicitApprovedUpgradeRequest);
    if (deepProposal.status != RepairProposalStatus::NotApplicable) return deepProposal;
    RepairPlanProposal proposal;
    const std::optional<HidHideConfigurationSnapshot> observed = configurationFrom(snapshot);
    if (!observed) { proposal.status = RepairProposalStatus::Blocked; proposal.reason = QStringLiteral("R1 planning requires complete independent HidHide configuration evidence."); return proposal; }
    proposal.before = *observed;
    const DoctorEnvironment &environment = snapshot.environment;
    if (!snapshot.contradictions.isEmpty()) { proposal.status = RepairProposalStatus::Blocked; proposal.reason = QStringLiteral("Contradictory evidence blocks repair planning."); return proposal; }
    if (!environment.capabilities.directProtocolAvailable || !environment.capabilities.helperArchitectureCompatible) {
        proposal.status = RepairProposalStatus::Blocked; proposal.reason = QStringLiteral("The current provider capability or helper architecture cannot support a safe R1 transaction."); return proposal;
    }
    if (!environment.repairIntent.suppliedByHotas) {
        proposal.status = RepairProposalStatus::NotApplicable; proposal.reason = QStringLiteral("This diagnosis requires verified HOTAS launch context; standalone Doctor will not guess intent."); return proposal;
    }

    std::optional<RepairRecipe> selected;
    QString target;
    ConfigurationDelta whitelistDelta;
    ConfigurationDelta blacklistDelta;
    HidHideConfigurationSnapshot after = *observed;
    if (const Diagnosis *diagnosis = diagnosisFor(session, {QStringLiteral("HD-DIAG-MISSING-HOTAS-EXEMPTION")})) {
        Q_UNUSED(diagnosis)
        if (!environment.repairIntent.expectedExecutable.isEmpty() && !containsSemantic(after.whitelist, environment.repairIntent.expectedExecutable)) {
            selected = RepairRecipeRegistry::recipe(RepairRecipeId(QStringLiteral("HD-R1-ADD-HOTAS-WHITELIST")));
            target = environment.repairIntent.expectedExecutable;
            whitelistDelta = ConfigurationDeltaEngine::addExact(after.whitelist, target);
            after.whitelist = whitelistDelta.resulting;
        }
    }
    if (!selected) if (const Diagnosis *diagnosis = diagnosisFor(session, {QStringLiteral("HD-DIAG-STALE-CONFIG")})) {
        Q_UNUSED(diagnosis)
        if (!environment.repairIntent.expectedExecutable.isEmpty()) {
            for (const QString &entry : after.whitelist) {
                if (whitelistOwnership(entry, environment) == ConfigurationOwnership::HotasOwned
                    && ConfigurationDeltaEngine::semanticKey(entry) != ConfigurationDeltaEngine::semanticKey(environment.repairIntent.expectedExecutable)) {
                    selected = RepairRecipeRegistry::recipe(RepairRecipeId(QStringLiteral("HD-R1-REPLACE-STALE-HOTAS-WHITELIST")));
                    target = entry;
                    whitelistDelta = ConfigurationDeltaEngine::removeExact(after.whitelist, entry);
                    after.whitelist = whitelistDelta.resulting;
                    const ConfigurationDelta add = ConfigurationDeltaEngine::addExact(after.whitelist, environment.repairIntent.expectedExecutable);
                    whitelistDelta.additions = add.additions;
                    whitelistDelta.unchanged = add.unchanged;
                    after.whitelist = add.resulting;
                    break;
                }
            }
        }
    }
    if (!selected) if (const Diagnosis *diagnosis = diagnosisFor(session, {QStringLiteral("HD-DIAG-VIRTUAL-HIDDEN")})) {
        Q_UNUSED(diagnosis)
        for (const QString &entry : environment.repairIntent.expectedVirtualOutputIds) {
            if (containsSemantic(after.blacklist, entry)) {
                selected = RepairRecipeRegistry::recipe(RepairRecipeId(QStringLiteral("HD-R1-UNHIDE-HOTAS-VIRTUAL-OUTPUT")));
                target = entry;
                blacklistDelta = ConfigurationDeltaEngine::removeExact(after.blacklist, entry);
                after.blacklist = blacklistDelta.resulting;
                break;
            }
        }
    }
    if (!selected) { proposal.status = RepairProposalStatus::NotApplicable; proposal.reason = QStringLiteral("No R1 recipe is applicable to this completed diagnosis and ownership context."); return proposal; }

    RepairPlan plan;
    plan.id = newPlanId();
    plan.sessionId = session.id();
    plan.recipeId = selected->id;
    plan.recipeVersion = selected->version;
    plan.title = selected->title;
    plan.description = selected->description;
    plan.riskClass = selected->riskClass;
    plan.qualification = selected->qualification;
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.elevationRequired = selected->elevationRequired;
    plan.restartRequired = selected->restartRequired;
    plan.estimatedSeconds = 5;
    plan.preconditionFingerprint = observed->fingerprint();
    plan.expectedPreState = observed->stableJson();
    plan.expectedPostState = after.stableJson();
    plan.expectedPostFingerprint = after.fingerprint();
    plan.preconditions = {{QStringLiteral("configuration.sha256"), plan.preconditionFingerprint},
        {QStringLiteral("provider"), observed->provider}, {QStringLiteral("windows.build"), QString::number(environment.platform.build)},
        {QStringLiteral("architecture"), displayName(environment.platform.nativeArchitecture)}};
    if (!whitelistDelta.additions.isEmpty()) plan.operations.append({DoctorOperationId(QStringLiteral("OP-R1-WHITELIST-ADD")),
        RepairOperationKind::AddWhitelistEntry, RepairTargetKind::WhitelistEntry, whitelistDelta.additions.first(), whitelistDelta.additions.first()});
    if (!whitelistDelta.removals.isEmpty()) plan.operations.append({DoctorOperationId(QStringLiteral("OP-R1-WHITELIST-REMOVE")),
        RepairOperationKind::RemoveWhitelistEntry, RepairTargetKind::WhitelistEntry, whitelistDelta.removals.first(), whitelistDelta.removals.first()});
    if (!blacklistDelta.removals.isEmpty()) plan.operations.append({DoctorOperationId(QStringLiteral("OP-R1-BLACKLIST-REMOVE")),
        RepairOperationKind::RemoveBlacklistEntry, RepairTargetKind::BlacklistEntry, blacklistDelta.removals.first(), blacklistDelta.removals.first()});
    for (const QString &entry : observed->whitelist) if (whitelistOwnership(entry, environment) != ConfigurationOwnership::HotasOwned)
        plan.unchangedCollateral.append(QStringLiteral("Whitelist preserved: %1 (%2)").arg(entry, ownerLabel(whitelistOwnership(entry, environment))));
    for (const QString &entry : observed->blacklist) if (blacklistOwnership(entry, environment) != ConfigurationOwnership::HotasOwned)
        plan.unchangedCollateral.append(QStringLiteral("Hidden-device entry preserved: %1 (%2)").arg(entry, ownerLabel(blacklistOwnership(entry, environment))));
    plan.unchangedCollateral.append(QStringLiteral("Cloak state unchanged"));
    plan.unchangedCollateral.append(QStringLiteral("Inverse mode unchanged"));
    plan.integrityDigest = RepairHelperContract::seal(plan);
    proposal.recipe = *selected;
    proposal.plan = plan;
    proposal.after = after;
    proposal.whitelistDelta = whitelistDelta;
    proposal.blacklistDelta = blacklistDelta;
    proposal.collateralPreserved = plan.unchangedCollateral;
    proposal.status = ownerLabMode ? RepairProposalStatus::AvailableForOwnerLab : RepairProposalStatus::IdentifiedButNotFieldQualified;
    proposal.reason = ownerLabMode ? QStringLiteral("Lab-qualified R1 plan is available for explicit owner authorization only.")
        : QStringLiteral("Repair identified but not field qualified; normal production mode will not offer execution.");
    return proposal;
}

MemoryRepairConfigurationMutator::MemoryRepairConfigurationMutator(HidHideConfigurationSnapshot snapshot) : m_snapshot(std::move(snapshot)) {}
HidHideConfigurationSnapshot MemoryRepairConfigurationMutator::readConfiguration() { m_snapshot.observedAt = QDateTime::currentDateTimeUtc(); return m_snapshot; }
void MemoryRepairConfigurationMutator::failNextOperation(NativeError error) { m_nextError = std::move(error); }
void MemoryRepairConfigurationMutator::replaceExternally(HidHideConfigurationSnapshot snapshot) { m_snapshot = std::move(snapshot); }
bool MemoryRepairConfigurationMutator::apply(const RepairOperation &operation, NativeError *error)
{
    if (m_nextError) { if (error) *error = *m_nextError; m_nextError.reset(); return false; }
    switch (operation.kind) {
    case RepairOperationKind::AddWhitelistEntry: m_snapshot.whitelist = ConfigurationDeltaEngine::addExact(m_snapshot.whitelist, operation.targetIdentity).resulting; return true;
    case RepairOperationKind::RemoveWhitelistEntry: m_snapshot.whitelist = ConfigurationDeltaEngine::removeExact(m_snapshot.whitelist, operation.targetIdentity).resulting; return true;
    case RepairOperationKind::AddBlacklistEntry: m_snapshot.blacklist = ConfigurationDeltaEngine::addExact(m_snapshot.blacklist, operation.targetIdentity).resulting; return true;
    case RepairOperationKind::RemoveBlacklistEntry: m_snapshot.blacklist = ConfigurationDeltaEngine::removeExact(m_snapshot.blacklist, operation.targetIdentity).resulting; return true;
    case RepairOperationKind::SetHidHideActive: return boolValue(operation.requestedValue, &m_snapshot.active);
    case RepairOperationKind::SetHidHideInverse: return boolValue(operation.requestedValue, &m_snapshot.inverse);
    default: if (error) *error = {NativeErrorDomain::Protocol, 50, QStringLiteral("ERROR_NOT_SUPPORTED"), QStringLiteral("Operation is not permitted by the Phase 3 memory mutator.")}; return false;
    }
}

RepairJournalStore::RepairJournalStore(QString root)
    : m_root(root.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
          .filePath(QStringLiteral("repair-transactions")) : std::move(root)) {}
QString RepairJournalStore::root() const { return m_root; }
QString RepairJournalStore::journalPath(const RepairTransactionId &id) const { return QDir(m_root).filePath(id.value() + QStringLiteral(".json")); }
bool RepairJournalStore::persist(RepairTransaction transaction, QString *reason) const
{
    if (!transaction.id.isValid()) { if (reason) *reason = QStringLiteral("Cannot persist a transaction without a stable transaction ID."); return false; }
    if (!QDir().mkpath(m_root)) { if (reason) *reason = QStringLiteral("The per-user repair journal directory could not be created."); return false; }
    if (!hardenJournalRoot(m_root, reason)) return false;
    transaction.checksum.clear();
    transaction.checksum = hash(QJsonDocument(transactionObject(transaction, false)).toJson(QJsonDocument::Compact));
    QSaveFile file(journalPath(transaction.id));
    if (!file.open(QIODevice::WriteOnly)) { if (reason) *reason = QStringLiteral("The repair journal could not be opened for an atomic write."); return false; }
    const QByteArray document = QJsonDocument(transactionObject(transaction, true)).toJson(QJsonDocument::Compact);
    if (file.write(document) != document.size() || !file.commit()) { if (reason) *reason = QStringLiteral("The repair journal could not be atomically committed."); return false; }
    return true;
}

std::optional<RepairTransaction> RepairJournalStore::load(const RepairTransactionId &id, QString *reason) const
{
    QFile file(journalPath(id));
    if (!file.open(QIODevice::ReadOnly)) { if (reason) *reason = QStringLiteral("Repair journal is unavailable."); return std::nullopt; }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) { if (reason) *reason = QStringLiteral("Repair journal is malformed or incomplete."); return std::nullopt; }
    return transactionFromObject(document.object(), reason);
}

QList<RepairTransaction> RepairJournalStore::history(QString *reason) const
{
    QList<RepairTransaction> result;
    QDir directory(m_root);
    if (!directory.exists()) return result;
    for (const QFileInfo &file : directory.entryInfoList({QStringLiteral("REPAIR-TX-*.json")}, QDir::Files, QDir::Time)) {
        const QString value = file.completeBaseName();
        QString loadReason;
        const std::optional<RepairTransaction> transaction = load(RepairTransactionId(value), &loadReason);
        if (transaction) result.append(*transaction);
        else if (reason && reason->isEmpty()) *reason = loadReason;
    }
    return result;
}

RepairExecutionResult RepairTransactionCoordinator::dryRun(const RepairPlanProposal &proposal, const DoctorEnvironment &environment,
    const DoctorSessionId &sessionId, const RepairJournalStore &journal, const RepairTransactionId &transactionId) const
{
    RepairExecutionResult result;
    if (proposal.status == RepairProposalStatus::Blocked || proposal.plan.operations.isEmpty()) { result.detail = proposal.reason; return result; }
    result.transaction = transactionFor(proposal.plan, environment, sessionId, RepairTransactionState::Planned, transactionId);
    result.transaction.backupManifest = backupFor(proposal.before, environment, proposal.plan);
    result.transaction.finalStatus = QStringLiteral("Dry run complete — no mutation was requested or performed.");
    QString reason;
    if (!journal.persist(result.transaction, &reason)) { result.transaction.state = RepairTransactionState::FailedSafely; result.detail = reason; return result; }
    result.detail = result.transaction.finalStatus;
    return result;
}

RepairExecutionResult RepairTransactionCoordinator::executeOwnerLab(const RepairPlanProposal &proposal, const DoctorEnvironment &environment,
    IRepairConfigurationMutator &mutator, const RepairJournalStore &journal, const RepairTransactionId &transactionId,
    IRepairPostconditionVerifier *postconditionVerifier) const
{
    RepairExecutionResult result;
    RepairPlan plan = proposal.plan;
    plan.authorization = RepairAuthorization::OwnerLabAuthorized;
    plan.integrityDigest = RepairHelperContract::seal(plan);
    QString reason;
    if (proposal.status != RepairProposalStatus::AvailableForOwnerLab || !RepairHelperContract::validate(plan, environment, true, &reason)) {
        result.detail = reason.isEmpty() ? proposal.reason : reason;
        return result;
    }
    QLockFile lock(QDir(journal.root()).filePath(QStringLiteral("active-repair.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { result.detail = QStringLiteral("Another HidHide Doctor repair transaction is active."); return result; }
    const HidHideConfigurationSnapshot before = mutator.readConfiguration();
    result.transaction = transactionFor(plan, environment, plan.sessionId, RepairTransactionState::Authorized, transactionId);
    result.transaction.finalStatus = QStringLiteral("Exact plan authorization, recipe version, targets, digest, and qualification are bound.");
    if (!journal.persist(result.transaction, &reason)) { result.transaction.state = RepairTransactionState::FailedSafely; result.detail = reason; return result; }
    result.transaction.state = RepairTransactionState::Revalidating;
    if (before.fingerprint() != plan.preconditionFingerprint) {
        result.transaction.state = RepairTransactionState::StalePlan;
        result.transaction.finalStatus = QStringLiteral("Plan became stale before mutation; replan required.");
        journal.persist(result.transaction, nullptr);
        result.detail = result.transaction.finalStatus;
        return result;
    }
    result.transaction.state = RepairTransactionState::CapturingBackup;
    result.transaction.backupManifest = backupFor(before, environment, plan);
    if (!journal.persist(result.transaction, &reason)) { result.transaction.state = RepairTransactionState::FailedSafely; result.detail = reason; return result; }
    result.transaction.state = RepairTransactionState::Executing;
    for (int index = 0; index < plan.operations.size(); ++index) {
        const RepairOperation &operation = plan.operations.at(index);
        RepairOperationJournalEntry entry{result.transaction.id, plan.id, operation.id, operation.kind, operation.targetKind,
            operation.targetIdentity, before.stableJson(), operation.requestedValue, {}, DoctorOperationState::Running,
            std::nullopt, QDateTime::currentDateTimeUtc()};
        result.transaction.currentOperation = index;
        result.transaction.operations.append(entry);
        if (!journal.persist(result.transaction, &reason)) { result.transaction.state = RepairTransactionState::FailedSafely; result.detail = reason; return result; }
        NativeError nativeError;
        if (!mutator.apply(operation, &nativeError)) {
            result.transaction.operations.last().state = DoctorOperationState::Failed;
            result.transaction.operations.last().nativeError = nativeError;
            result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
            result.transaction.state = RepairTransactionState::FailedSafely;
            result.transaction.finalStatus = QStringLiteral("Repair operation failed before verification; no additional operation was attempted.");
            journal.persist(result.transaction, nullptr);
            result.detail = result.transaction.finalStatus;
            return result;
        }
        result.mutated = true;
        result.transaction.operations.last().state = DoctorOperationState::Completed;
        result.transaction.operations.last().actualPostValue = mutator.readConfiguration().stableJson();
        result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
        if (!journal.persist(result.transaction, &reason)) {
            result.transaction.state = RepairTransactionState::RecoveryRequired;
            result.transaction.finalStatus = QStringLiteral("Repair operation completed but its durable journal update failed; read-back reconciliation is required.");
            result.detail = result.transaction.finalStatus;
            return result;
        }
    }
    result.transaction.state = RepairTransactionState::Verifying;
    const HidHideConfigurationSnapshot after = mutator.readConfiguration();
    QString postconditionReason;
    if (after.fingerprint() == plan.expectedPostFingerprint
        && (!postconditionVerifier || postconditionVerifier->verify(plan, after, &postconditionReason))) {
        result.transaction.state = RepairTransactionState::Completed;
        result.transaction.finalStatus = QStringLiteral("Repair complete — independent configuration read-back matched the authorized exact delta.");
        journal.persist(result.transaction, nullptr);
        result.detail = result.transaction.finalStatus;
        return result;
    }
    if (after.fingerprint() == plan.expectedPostFingerprint) {
        // The configuration delta is exact, but a separate affected check did
        // not verify.  Because current state still exactly matches the
        // authorized post-state, restoring the captured pre-state is safe.
        result.transaction.state = RepairTransactionState::RollingBack;
        result.transaction.finalStatus = QStringLiteral("Post-repair check did not verify; restoring the exact captured pre-state.");
        if (!journal.persist(result.transaction, &reason)) {
            result.transaction.state = RepairTransactionState::RecoveryRequired;
            result.transaction.finalStatus = QStringLiteral("Rollback was not started because its durable journal boundary could not be recorded.");
            result.detail = result.transaction.finalStatus;
            return result;
        }
        const HidHideConfigurationSnapshot beforeRollback = mutator.readConfiguration();
        if (beforeRollback.fingerprint() != plan.expectedPostFingerprint) {
            result.transaction.state = RepairTransactionState::RecoveryRequired;
            result.transaction.finalStatus = QStringLiteral("ROLLBACK PAUSED — configuration changed after verification; manual review required.");
            journal.persist(result.transaction, nullptr);
            result.detail = result.transaction.finalStatus;
            return result;
        }
        for (const RepairOperation &operation : restoreOperations(beforeRollback, before)) {
            RepairOperationJournalEntry entry{result.transaction.id, plan.id, operation.id, operation.kind, operation.targetKind,
                operation.targetIdentity, beforeRollback.stableJson(), operation.requestedValue, {}, DoctorOperationState::Running,
                std::nullopt, QDateTime::currentDateTimeUtc(), {}, QStringLiteral("Rollback pending")};
            result.transaction.operations.append(entry);
            result.transaction.currentOperation = result.transaction.operations.size() - 1;
            if (!journal.persist(result.transaction, &reason)) {
                result.transaction.state = RepairTransactionState::RecoveryRequired;
                result.transaction.finalStatus = QStringLiteral("ROLLBACK PAUSED — durable journal update failed before restore.");
                result.detail = result.transaction.finalStatus;
                return result;
            }
            NativeError rollbackError;
            if (!mutator.apply(operation, &rollbackError)) {
                result.transaction.operations.last().state = DoctorOperationState::Failed;
                result.transaction.operations.last().nativeError = rollbackError;
                result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
                result.transaction.operations.last().rollbackStatus = QStringLiteral("Rollback failed");
                result.transaction.state = RepairTransactionState::RecoveryRequired;
                result.transaction.finalStatus = QStringLiteral("ROLLBACK PAUSED — restore operation failed; manual review required.");
                journal.persist(result.transaction, nullptr);
                result.detail = result.transaction.finalStatus;
                return result;
            }
            result.transaction.operations.last().state = DoctorOperationState::Completed;
            result.transaction.operations.last().actualPostValue = mutator.readConfiguration().stableJson();
            result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
            result.transaction.operations.last().rollbackStatus = QStringLiteral("Restored exact pre-state component");
            if (!journal.persist(result.transaction, &reason)) {
                result.transaction.state = RepairTransactionState::RecoveryRequired;
                result.transaction.finalStatus = QStringLiteral("Rollback applied but journal persistence failed; read-only reconciliation required.");
                result.detail = result.transaction.finalStatus;
                return result;
            }
        }
        if (mutator.readConfiguration().fingerprint() == before.fingerprint()) {
            result.rolledBack = true;
            result.transaction.state = RepairTransactionState::FailedSafely;
            result.transaction.finalStatus = QStringLiteral("Repair failed — original state restored after independent verification did not pass.");
            journal.persist(result.transaction, nullptr);
            result.detail = result.transaction.finalStatus;
            return result;
        }
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("ROLLBACK PAUSED — exact pre-state comparison failed; manual review required.");
        journal.persist(result.transaction, nullptr);
        result.detail = result.transaction.finalStatus;
        return result;
    }
    // Never restore over a state that differs from Doctor's exact post-state.
    // That mismatch may be an external user/application change rather than a
    // failed SET, so automatic rollback would risk destroying it.
    result.transaction.state = RepairTransactionState::RecoveryRequired;
    result.transaction.finalStatus = QStringLiteral("Repair applied but verification differed; rollback withheld because the current state is not the authorized post-state.");
    journal.persist(result.transaction, nullptr);
    result.detail = result.transaction.finalStatus;
    return result;
}

RepairRecoveryResult RepairTransactionCoordinator::reconcileIncomplete(const RepairTransaction &transaction,
    IRepairConfigurationMutator &mutator, const RepairJournalStore &journal) const
{
    RepairRecoveryResult result;
    result.transaction = transaction;
    if (!transaction.id.isValid() || transaction.state == RepairTransactionState::Completed
        || transaction.state == RepairTransactionState::FailedSafely || transaction.state == RepairTransactionState::Cancelled) {
        result.detail = QStringLiteral("No incomplete repair transaction requires reconciliation.");
        return result;
    }
    const HidHideConfigurationSnapshot current = mutator.readConfiguration();
    if (current.fingerprint() == transaction.preconditionFingerprint) {
        result.transaction.state = RepairTransactionState::Cancelled;
        result.transaction.finalStatus = QStringLiteral("Restart reconciliation observed the captured pre-state; no mutation is pending.");
        result.detail = result.transaction.finalStatus;
    } else if (!transaction.expectedPostFingerprint.isEmpty()
        && current.fingerprint() == transaction.expectedPostFingerprint) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Restart reconciliation observed the expected configuration delta; affected checks require owner review before completion. No retry or mutation was attempted.");
        result.detail = result.transaction.finalStatus;
        result.requiresOwnerReview = true;
    } else {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Restart reconciliation found an unknown configuration state; no retry or rollback was attempted.");
        result.detail = result.transaction.finalStatus;
        result.requiresOwnerReview = true;
    }
    // Reconciliation itself is a durable, read-only boundary.  It contains no
    // apply call and cannot restart an interrupted helper transaction.
    journal.persist(result.transaction, nullptr);
    return result;
}

RepairRecoveryResult RepairTransactionCoordinator::reconcileAfterReboot(const RepairTransaction &transaction,
    const ReadOnlyDiagnosticSnapshot &snapshot, const RepairJournalStore &journal) const
{
    RepairRecoveryResult result;
    result.transaction = transaction;
    if (!transaction.id.isValid() || transaction.riskClass == RepairRiskClass::R1Configuration
        || transaction.state != RepairTransactionState::AwaitingReboot) {
        result.detail = QStringLiteral("No deep AwaitingReboot transaction requires post-reboot observation.");
        return result;
    }
    result.transaction.state = RepairTransactionState::ContinuingAfterReboot;
    result.transaction.rebootCount += 1;
    result.transaction.continuationState = {{QStringLiteral("stage"), QStringLiteral("ObservingAfterReboot")},
        {QStringLiteral("observeFirst"), true}, {QStringLiteral("forwardOrRollback"), QStringLiteral("forward")},
        {QStringLiteral("observedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const QJsonObject package = transaction.deepRepair.value(QStringLiteral("package")).toObject();
    const QString targetVersion = package.value(QStringLiteral("version")).toString();
    const bool targetPackageActive = !targetVersion.isEmpty()
        && snapshot.environment.hidhide.packageVersion == targetVersion;
    const bool targetDriverLoaded = !targetVersion.isEmpty()
        && snapshot.environment.hidhide.driverVersion == targetVersion;
    const std::optional<HidHideConfigurationSnapshot> configuration = RepairPlanner::configurationFrom(snapshot);
    const bool controlApiHealthy = readable(protocol(snapshot, QStringLiteral("GET_ACTIVE")))
        && readable(protocol(snapshot, QStringLiteral("GET_INVERSE")))
        && readable(protocol(snapshot, QStringLiteral("GET_WHITELIST")))
        && readable(protocol(snapshot, QStringLiteral("GET_BLACKLIST")));
    result.transaction.continuationState.insert(QStringLiteral("targetPackageActive"), targetPackageActive);
    result.transaction.continuationState.insert(QStringLiteral("targetDriverLoaded"), targetDriverLoaded);
    result.transaction.continuationState.insert(QStringLiteral("controlApiHealthy"), controlApiHealthy);
    result.transaction.continuationState.insert(QStringLiteral("configurationReadable"), configuration.has_value());

    if (result.transaction.rebootCount > result.transaction.maximumReboots) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Maximum reboot count exceeded; automatic continuation is stopped and recovery requires a new owner-authorized plan.");
        result.requiresOwnerReview = true;
    } else if (!targetPackageActive) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Post-reboot observation found the target package missing or inactive; no mutation was replayed.");
        result.requiresOwnerReview = true;
    } else if (!targetDriverLoaded) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Post-reboot observation found the previous driver still loaded; no additional reboot or mutation was assumed safe.");
        result.requiresOwnerReview = true;
    } else if (!controlApiHealthy) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Post-reboot target package/driver evidence exists but the HidHide control API did not verify; recovery review is required.");
        result.requiresOwnerReview = true;
    } else if (!configuration) {
        result.transaction.state = RepairTransactionState::RecoveryRequired;
        result.transaction.finalStatus = QStringLiteral("Post-reboot configuration cannot be read completely; restoration is withheld pending conflict-safe recovery review.");
        result.requiresOwnerReview = true;
    } else {
        result.transaction.state = RepairTransactionState::Completed;
        result.transaction.continuationState.insert(QStringLiteral("stage"), QStringLiteral("VerifiedAfterReboot"));
        result.transaction.finalStatus = QStringLiteral("Post-reboot observation verified target package, loaded driver, control API, and readable configuration. A new Doctor session supplied independent verification evidence.");
    }
    journal.persist(result.transaction, nullptr);
    result.detail = result.transaction.finalStatus;
    return result;
}

QString displayName(ConfigurationOwnership ownership)
{
    switch (ownership) {
    case ConfigurationOwnership::HotasOwned: return QStringLiteral("HOTAS-owned / expected");
    case ConfigurationOwnership::DoctorOwned: return QStringLiteral("Doctor-owned / temporary");
    case ConfigurationOwnership::UserOrUnrelated: return QStringLiteral("User or unrelated");
    case ConfigurationOwnership::Unknown: return QStringLiteral("Unknown ownership");
    }
    return QStringLiteral("Unknown ownership");
}

QString displayName(RepairProposalStatus status)
{
    switch (status) {
    case RepairProposalStatus::AvailableForOwnerLab: return QStringLiteral("LAB QUALIFIED — OWNER TEST ONLY");
    case RepairProposalStatus::IdentifiedButNotFieldQualified: return QStringLiteral("REPAIR IDENTIFIED — NOT FIELD QUALIFIED");
    case RepairProposalStatus::NotApplicable: return QStringLiteral("NO QUALIFIED REPAIR APPLICABLE");
    case RepairProposalStatus::Blocked: return QStringLiteral("REPAIR BLOCKED");
    }
    return QStringLiteral("REPAIR BLOCKED");
}

QString displayName(RepairTransactionState state)
{
    switch (state) {
    case RepairTransactionState::Planned: return QStringLiteral("REPAIR REVIEW");
    case RepairTransactionState::AwaitingAuthorization: return QStringLiteral("AWAITING AUTHORIZATION");
    case RepairTransactionState::Authorized: return QStringLiteral("REPAIR AUTHORIZED");
    case RepairTransactionState::CapturingBackup: return QStringLiteral("CAPTURING BACKUP");
    case RepairTransactionState::Revalidating: return QStringLiteral("REVALIDATING");
    case RepairTransactionState::AwaitingElevation: return QStringLiteral("AWAITING ELEVATION");
    case RepairTransactionState::PreparingPackage: return QStringLiteral("PREPARING APPROVED PACKAGE");
    case RepairTransactionState::PackageValidated: return QStringLiteral("PACKAGE VALIDATED");
    case RepairTransactionState::StagingPackage: return QStringLiteral("STAGING VERIFIED PACKAGE");
    case RepairTransactionState::Installing: return QStringLiteral("INSTALLING APPROVED PACKAGE");
    case RepairTransactionState::AwaitingReboot: return QStringLiteral("RESTART REQUIRED");
    case RepairTransactionState::ContinuingAfterReboot: return QStringLiteral("RESUMING REPAIR — OBSERVING SYSTEM STATE");
    case RepairTransactionState::ReconcilingConfiguration: return QStringLiteral("RECONCILING CONFIGURATION");
    case RepairTransactionState::Executing: return QStringLiteral("REPAIR IN PROGRESS");
    case RepairTransactionState::Verifying: return QStringLiteral("VERIFYING REPAIR");
    case RepairTransactionState::RollingBack: return QStringLiteral("ROLLING BACK");
    case RepairTransactionState::Completed: return QStringLiteral("REPAIR COMPLETE");
    case RepairTransactionState::FailedSafely: return QStringLiteral("REPAIR FAILED SAFELY");
    case RepairTransactionState::RecoveryRequired: return QStringLiteral("RECOVERY REQUIRED");
    case RepairTransactionState::RecoveryPlanning: return QStringLiteral("RECOVERY PLAN REVIEW");
    case RepairTransactionState::Recovering: return QStringLiteral("RECOVERY IN PROGRESS");
    case RepairTransactionState::Cancelled: return QStringLiteral("REPAIR CANCELLED");
    case RepairTransactionState::StalePlan: return QStringLiteral("PLAN BECAME STALE");
    }
    return QStringLiteral("REPAIR REVIEW");
}

} // namespace hotas::doctor
