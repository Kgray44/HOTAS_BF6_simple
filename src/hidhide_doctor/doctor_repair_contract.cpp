#include "doctor_repair_contract.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>

namespace hotas::doctor {
namespace {

QString normalized(QString value) { return value.trimmed().toUpper(); }
bool isStable(const QString &value) { return QRegularExpression(QStringLiteral("^[A-Z][A-Z0-9-]{2,127}$")).match(value).hasMatch(); }

bool targetMatches(RepairOperationKind operation, RepairTargetKind target)
{
    switch (operation) {
    case RepairOperationKind::SetHidHideActive: return target == RepairTargetKind::HidHideActiveState;
    case RepairOperationKind::SetHidHideInverse: return target == RepairTargetKind::HidHideInverseState;
    case RepairOperationKind::AddWhitelistEntry:
    case RepairOperationKind::RemoveWhitelistEntry: return target == RepairTargetKind::WhitelistEntry;
    case RepairOperationKind::AddBlacklistEntry:
    case RepairOperationKind::RemoveBlacklistEntry: return target == RepairTargetKind::BlacklistEntry;
    case RepairOperationKind::StageApprovedPackage: return target == RepairTargetKind::ApprovedPackage;
    case RepairOperationKind::ValidateApprovedPackage: return target == RepairTargetKind::ApprovedPackage;
    case RepairOperationKind::InstallApprovedHidHidePackage: return target == RepairTargetKind::ApprovedPackage;
    case RepairOperationKind::RemoveSpecificInactiveHidHidePackage: return target == RepairTargetKind::InactiveHidHidePackage;
    case RepairOperationKind::ReconcileHidHideConfiguration: return target == RepairTargetKind::TransactionSnapshot;
    case RepairOperationKind::RequestSystemRestart: return target == RepairTargetKind::RebootBoundary;
    case RepairOperationKind::RestoreSnapshot: return target == RepairTargetKind::TransactionSnapshot;
    case RepairOperationKind::RepairExactServiceConfiguration: return target == RepairTargetKind::HidHideService;
    case RepairOperationKind::RepairExactFilterRegistration: return target == RepairTargetKind::HidHideFilterRegistration;
    case RepairOperationKind::Unknown: return false;
    }
    return false;
}

} // namespace

RepairPlanId::RepairPlanId(QString value) : m_value(normalized(std::move(value))) {}
bool RepairPlanId::isValid() const { return m_value.startsWith(QStringLiteral("REPAIR-PLAN-")) && isStable(m_value); }
const QString &RepairPlanId::value() const { return m_value; }
RepairTransactionId::RepairTransactionId(QString value) : m_value(normalized(std::move(value))) {}
bool RepairTransactionId::isValid() const { return m_value.startsWith(QStringLiteral("REPAIR-TX-")) && isStable(m_value); }
const QString &RepairTransactionId::value() const { return m_value; }
RepairRecipeId::RepairRecipeId(QString value) : m_value(normalized(std::move(value))) {}
bool RepairRecipeId::isValid() const
{
    return QRegularExpression(QStringLiteral("^HD-R[1-5]-[A-Z0-9-]{3,127}$")).match(m_value).hasMatch();
}
const QString &RepairRecipeId::value() const { return m_value; }

bool RepairOperation::isWellFormed(QString *reason) const
{
    if (!id.isValid() || kind == RepairOperationKind::Unknown || !targetMatches(kind, targetKind)
        || targetIdentity.trimmed().isEmpty() || requestedValue.trimmed().isEmpty()) {
        if (reason) *reason = QStringLiteral("Repair operation is not a known typed operation with an allowed target.");
        return false;
    }
    const auto isExactIdentifier = [](const QString &value) {
        return !value.contains(QLatin1Char('\\')) && !value.contains(QLatin1Char('/'))
            && !value.contains(QLatin1Char(':')) && !value.contains(QStringLiteral(".."));
    };
    if ((kind == RepairOperationKind::RepairExactServiceConfiguration && targetIdentity != QStringLiteral("HidHide"))
        || (kind == RepairOperationKind::RepairExactFilterRegistration && targetIdentity != QStringLiteral("HidHideFilterRegistration"))
        || ((kind == RepairOperationKind::ValidateApprovedPackage || kind == RepairOperationKind::StageApprovedPackage
                || kind == RepairOperationKind::InstallApprovedHidHidePackage
                || kind == RepairOperationKind::RemoveSpecificInactiveHidHidePackage)
            && (!targetIdentity.startsWith(QStringLiteral("HD-PKG-")) || !isExactIdentifier(targetIdentity)))
        || (kind == RepairOperationKind::RequestSystemRestart && targetIdentity != QStringLiteral("WindowsRestart"))) {
        if (reason) *reason = QStringLiteral("Deep-repair operation target is not the exact allow-listed HidHide identity.");
        return false;
    }
    for (const RepairVerification &check : verification) {
        if (!check.checkId.isValid()) {
            if (reason) *reason = QStringLiteral("Repair verification requires a stable CheckId.");
            return false;
        }
    }
    return true;
}

QString RepairHelperContract::seal(const RepairPlan &plan)
{
    QByteArray canonical = plan.id.value().toUtf8() + '\n' + plan.sessionId.value().toUtf8() + '\n';
    canonical += plan.recipeId.value().toUtf8() + '\n' + plan.recipeVersion.toUtf8() + '\n';
    canonical += QByteArray::number(static_cast<int>(plan.riskClass)) + '\n';
    canonical += QByteArray::number(static_cast<int>(plan.qualification)) + '\n';
    canonical += QByteArray::number(static_cast<int>(plan.authorization)) + '\n';
    canonical += QByteArray::number(plan.maximumReboots) + '\n';
    canonical += plan.preconditionFingerprint.toUtf8() + '\n';
    canonical += plan.expectedPreState.toUtf8() + '\n' + plan.expectedPostState.toUtf8() + '\n'
        + plan.expectedPostFingerprint.toUtf8() + '\n';
    for (const RepairPrecondition &precondition : plan.preconditions) {
        canonical += precondition.stableKey.toUtf8() + '=' + precondition.expectedValue.toUtf8() + '\n';
    }
    for (const RepairOperation &operation : plan.operations) {
        canonical += operation.id.value().toUtf8() + '|'
            + QByteArray::number(static_cast<int>(operation.kind)) + '|'
            + QByteArray::number(static_cast<int>(operation.targetKind)) + '|'
            + operation.targetIdentity.toUtf8() + '|'
            + operation.requestedValue.toUtf8() + '\n';
    }
    canonical += QJsonDocument(plan.deepRepair).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

bool RepairHelperContract::validate(const RepairPlan &plan, const DoctorEnvironment &environment, QString *reason)
{
    return validate(plan, environment, false, reason);
}

bool RepairHelperContract::validate(const RepairPlan &plan, const DoctorEnvironment &environment,
    bool ownerLabMode, QString *reason)
{
    if (!plan.id.isValid() || !plan.sessionId.isValid() || !plan.recipeId.isValid()
        || plan.recipeVersion.isEmpty() || plan.operations.isEmpty()) {
        if (reason) *reason = QStringLiteral("Repair plan lacks a stable identity, session, or typed operations.");
        return false;
    }
    if (plan.riskClass == RepairRiskClass::R0Observe || plan.qualification == RepairQualificationLevel::Retired) {
        if (reason) *reason = QStringLiteral("Repair plan has an invalid active repair class or retired qualification.");
        return false;
    }
    const bool fieldAuthorized = plan.authorization == RepairAuthorization::UserAuthorized
        && plan.qualification == RepairQualificationLevel::FieldQualified;
    const bool labAuthorized = ownerLabMode && plan.authorization == RepairAuthorization::OwnerLabAuthorized
        && plan.qualification == RepairQualificationLevel::LabQualified;
    if (!fieldAuthorized && !labAuthorized) {
        if (reason) *reason = QStringLiteral("Repair plan has not received user authorization.");
        return false;
    }
    if (!environment.capabilities.helperArchitectureCompatible || !environment.capabilities.directProtocolAvailable) {
        if (reason) *reason = QStringLiteral("Repair helper architecture is not compatible with the measured platform.");
        return false;
    }
    const auto requiredTier = [&] {
        switch (plan.riskClass) {
        case RepairRiskClass::R1Configuration: return RepairCapabilityTier::ConfigurationRepairSupported;
        case RepairRiskClass::R2Component: return RepairCapabilityTier::ComponentRepairSupported;
        case RepairRiskClass::R3Package: return RepairCapabilityTier::PackageRepairSupported;
        case RepairRiskClass::R4ApprovedUpgrade: return RepairCapabilityTier::UpgradeRepairSupported;
        case RepairRiskClass::R5Recovery: return RepairCapabilityTier::RecoverySupported;
        case RepairRiskClass::R0Observe: return RepairCapabilityTier::DiagnosisSupported;
        }
        return RepairCapabilityTier::RecoverySupported;
    }();
    if (plan.riskClass != RepairRiskClass::R1Configuration
        && static_cast<int>(environment.capabilities.highestQualifiedRepairTier) < static_cast<int>(requiredTier)) {
        if (reason) *reason = QStringLiteral("The measured platform is not qualified for this repair class.");
        return false;
    }
    if (plan.riskClass != RepairRiskClass::R1Configuration
        && (plan.deepRepair.isEmpty() || plan.maximumReboots < 0)) {
        if (reason) *reason = QStringLiteral("Deep repair is missing sealed package, continuation, or recovery metadata.");
        return false;
    }
    for (const RepairOperation &operation : plan.operations) {
        if (!operation.isWellFormed(reason)) return false;
    }
    if (plan.integrityDigest != seal(plan)) {
        if (reason) *reason = QStringLiteral("Repair plan integrity digest does not match the typed operation payload.");
        return false;
    }
    return true;
}

} // namespace hotas::doctor
