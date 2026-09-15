#include "doctor_types.h"

#include <QRegularExpression>
#include <QUuid>

namespace hotas::doctor {
namespace {

QString normalized(QString value)
{
    return value.trimmed().toUpper();
}

bool isStable(const QString &value)
{
    return QRegularExpression(QStringLiteral("^[A-Z][A-Z0-9-]{2,127}$")).match(value).hasMatch();
}

} // namespace

DoctorSessionId::DoctorSessionId(QString value) : m_value(normalized(std::move(value))) {}
DoctorSessionId DoctorSessionId::create() { return DoctorSessionId(QStringLiteral("SESSION-") + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper()); }
bool DoctorSessionId::isValid() const { return m_value.startsWith(QStringLiteral("SESSION-")) && isStable(m_value); }
const QString &DoctorSessionId::value() const { return m_value; }

DoctorCheckId::DoctorCheckId(QString value) : m_value(normalized(std::move(value))) {}
bool DoctorCheckId::isValid() const { return QRegularExpression(QStringLiteral("^HD-(SYS|INST|PKG|DRV|API|CFG|DEV|ISO|WIN|X|KB|PORT)-[0-9]{3}$")).match(m_value).hasMatch(); }
const QString &DoctorCheckId::value() const { return m_value; }

DoctorStepId::DoctorStepId(QString value) : m_value(normalized(std::move(value))) {}
bool DoctorStepId::isValid() const { return isStable(m_value); }
const QString &DoctorStepId::value() const { return m_value; }

EvidenceId::EvidenceId(QString value) : m_value(normalized(std::move(value))) {}
EvidenceId EvidenceId::create() { return EvidenceId(QStringLiteral("EVIDENCE-") + QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper()); }
bool EvidenceId::isValid() const { return m_value.startsWith(QStringLiteral("EVIDENCE-")) && isStable(m_value); }
const QString &EvidenceId::value() const { return m_value; }

FindingId::FindingId(QString value) : m_value(normalized(std::move(value))) {}
bool FindingId::isValid() const { return isStable(m_value); }
const QString &FindingId::value() const { return m_value; }
DiagnosisId::DiagnosisId(QString value) : m_value(normalized(std::move(value))) {}
bool DiagnosisId::isValid() const { return isStable(m_value); }
const QString &DiagnosisId::value() const { return m_value; }
KnowledgeSignatureId::KnowledgeSignatureId(QString value) : m_value(normalized(std::move(value))) {}
bool KnowledgeSignatureId::isValid() const { return isStable(m_value); }
const QString &KnowledgeSignatureId::value() const { return m_value; }
DoctorOperationId::DoctorOperationId(QString value) : m_value(normalized(std::move(value))) {}
bool DoctorOperationId::isValid() const { return isStable(m_value); }
const QString &DoctorOperationId::value() const { return m_value; }

QString displayName(DoctorPhase phase)
{
    switch (phase) {
    case DoctorPhase::SystemEnvironment: return QStringLiteral("System Environment");
    case DoctorPhase::InstallationDiscovery: return QStringLiteral("Installation Discovery");
    case DoctorPhase::PackageIntegrity: return QStringLiteral("Package Integrity");
    case DoctorPhase::KernelDriverState: return QStringLiteral("Kernel Driver State");
    case DoctorPhase::ProtocolApiHealth: return QStringLiteral("Protocol / API Health");
    case DoctorPhase::ConfigurationIntegrity: return QStringLiteral("Configuration Integrity");
    case DoctorPhase::DeviceEcosystem: return QStringLiteral("Device Ecosystem");
    case DoctorPhase::IsolationVerification: return QStringLiteral("Isolation Verification");
    case DoctorPhase::WindowsEvidence: return QStringLiteral("Windows Evidence");
    case DoctorPhase::ConsistencyAnalysis: return QStringLiteral("Consistency Analysis");
    case DoctorPhase::Diagnosis: return QStringLiteral("Diagnosis");
    case DoctorPhase::RepairRecommendation: return QStringLiteral("Repair Recommendation");
    case DoctorPhase::ExtendedInvestigation: return QStringLiteral("Extended Investigation");
    }
    return QStringLiteral("Unknown phase");
}

QString displayName(DoctorCheckStatus status)
{
    switch (status) {
    case DoctorCheckStatus::Waiting: return QStringLiteral("Waiting");
    case DoctorCheckStatus::Running: return QStringLiteral("Running");
    case DoctorCheckStatus::Healthy: return QStringLiteral("Healthy");
    case DoctorCheckStatus::Informational: return QStringLiteral("Informational");
    case DoctorCheckStatus::Warning: return QStringLiteral("Warning");
    case DoctorCheckStatus::Failed: return QStringLiteral("Failed");
    case DoctorCheckStatus::PermissionLimited: return QStringLiteral("Permission Limited");
    case DoctorCheckStatus::Blocked: return QStringLiteral("Blocked");
    case DoctorCheckStatus::NotApplicable: return QStringLiteral("Not Applicable");
    case DoctorCheckStatus::Cancelled: return QStringLiteral("Cancelled");
    case DoctorCheckStatus::TimedOut: return QStringLiteral("Timed Out");
    case DoctorCheckStatus::Unknown: return QStringLiteral("Unknown");
    case DoctorCheckStatus::Inconclusive: return QStringLiteral("Inconclusive");
    }
    return QStringLiteral("Unknown");
}

bool isTerminal(DoctorCheckStatus status)
{
    return status != DoctorCheckStatus::Waiting && status != DoctorCheckStatus::Running;
}

bool isExecutionFailure(DoctorCheckStatus status)
{
    return status == DoctorCheckStatus::PermissionLimited || status == DoctorCheckStatus::Blocked
        || status == DoctorCheckStatus::TimedOut
        || status == DoctorCheckStatus::Cancelled || status == DoctorCheckStatus::Unknown
        || status == DoctorCheckStatus::Inconclusive;
}

bool isTransitionAllowed(DoctorSessionState from, DoctorSessionState to)
{
    if (from == to) return true;
    if (to == DoctorSessionState::Cancelled || to == DoctorSessionState::FailedSafely) return true;
    switch (from) {
    case DoctorSessionState::Preparing: return to == DoctorSessionState::Diagnosing;
    case DoctorSessionState::Diagnosing: return to == DoctorSessionState::Analyzing;
    case DoctorSessionState::Analyzing: return to == DoctorSessionState::DiagnosisComplete || to == DoctorSessionState::DegradedComplete;
    case DoctorSessionState::DiagnosisComplete: return to == DoctorSessionState::AwaitingUser || to == DoctorSessionState::RepairReady || to == DoctorSessionState::DegradedComplete;
    case DoctorSessionState::AwaitingUser: return to == DoctorSessionState::RepairReady;
    case DoctorSessionState::RepairReady: return to == DoctorSessionState::AwaitingElevation || to == DoctorSessionState::Repairing;
    case DoctorSessionState::AwaitingElevation: return to == DoctorSessionState::Repairing;
    case DoctorSessionState::Repairing: return to == DoctorSessionState::AwaitingReboot || to == DoctorSessionState::Verifying;
    case DoctorSessionState::AwaitingReboot: return to == DoctorSessionState::ResumingAfterReboot;
    case DoctorSessionState::ResumingAfterReboot: return to == DoctorSessionState::Verifying;
    case DoctorSessionState::Verifying: return to == DoctorSessionState::RepairComplete || to == DoctorSessionState::DegradedComplete;
    case DoctorSessionState::RepairComplete:
    case DoctorSessionState::DegradedComplete:
    case DoctorSessionState::Cancelled:
    case DoctorSessionState::FailedSafely:
        return false;
    }
    return false;
}

} // namespace hotas::doctor
