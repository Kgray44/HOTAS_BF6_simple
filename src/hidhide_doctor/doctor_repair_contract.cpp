#include "doctor_repair_contract.h"

#include <QCryptographicHash>
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
    case RepairOperationKind::RestoreSnapshot: return target == RepairTargetKind::TransactionSnapshot;
    case RepairOperationKind::RepairExactServiceConfiguration:
    case RepairOperationKind::RepairExactFilterRegistration: return target != RepairTargetKind::None;
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

bool RepairOperation::isWellFormed(QString *reason) const
{
    if (!id.isValid() || kind == RepairOperationKind::Unknown || !targetMatches(kind, targetKind)
        || targetIdentity.trimmed().isEmpty() || requestedValue.trimmed().isEmpty()) {
        if (reason) *reason = QStringLiteral("Repair operation is not a known typed operation with an allowed target.");
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
    canonical += QByteArray::number(static_cast<int>(plan.riskClass)) + '\n';
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
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

bool RepairHelperContract::validate(const RepairPlan &plan, const DoctorEnvironment &environment, QString *reason)
{
    if (!plan.id.isValid() || !plan.sessionId.isValid() || plan.operations.isEmpty()) {
        if (reason) *reason = QStringLiteral("Repair plan lacks a stable identity, session, or typed operations.");
        return false;
    }
    if (plan.authorization != RepairAuthorization::UserAuthorized) {
        if (reason) *reason = QStringLiteral("Repair plan has not received user authorization.");
        return false;
    }
    if (plan.qualification != RepairQualificationLevel::FieldQualified) {
        if (reason) *reason = QStringLiteral("Only FieldQualified repairs may cross the future helper boundary.");
        return false;
    }
    if (!environment.capabilities.helperArchitectureCompatible) {
        if (reason) *reason = QStringLiteral("Repair helper architecture is not compatible with the measured platform.");
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
