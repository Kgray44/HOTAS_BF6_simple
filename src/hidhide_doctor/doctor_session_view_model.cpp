#include "doctor_session_view_model.h"

#include <algorithm>

namespace hotas::doctor {

DoctorSessionViewModel::DoctorSessionViewModel(DoctorSession &session, QString buildIdentity, QObject *parent)
    : QObject(parent), m_session(session), m_buildIdentity(std::move(buildIdentity)) {}

QString DoctorSessionViewModel::buildIdentity() const { return m_buildIdentity; }
QString DoctorSessionViewModel::sessionId() const { return m_session.id().value(); }
QString DoctorSessionViewModel::currentPhase() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) {
        if (item.status == DoctorCheckStatus::Running) return displayName(item.check.phase);
    }
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("Diagnostic scan complete")
        : (m_session.state() == DoctorSessionState::Cancelled ? QStringLiteral("Diagnostic scan cancelled") : QStringLiteral("Preparing diagnostic scan"));
}
QString DoctorSessionViewModel::currentStep() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) {
        if (item.status == DoctorCheckStatus::Running) return item.check.title;
    }
    return m_session.state() == DoctorSessionState::DiagnosisComplete ? QStringLiteral("All applicable read-only checks completed")
        : QStringLiteral("No native operation is currently running");
}
QString DoctorSessionViewModel::currentStepId() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) {
        if (item.status == DoctorCheckStatus::Running) return item.check.id.value();
    }
    return {};
}
int DoctorSessionViewModel::overallProgress() const { return m_session.plan().progress().overallPercent; }
int DoctorSessionViewModel::currentStepProgress() const { return m_session.plan().progress().currentStepPercent; }
QStringList DoctorSessionViewModel::planItems() const
{
    QStringList values;
    for (const DiagnosticPlanItem &item : m_session.plan().items()) {
        values.append(QStringLiteral("%1  %2 — %3").arg(item.check.id.value(), item.check.title, displayName(item.status)));
    }
    return values;
}
QStringList DoctorSessionViewModel::resultItems() const
{
    QStringList values;
    for (const DoctorCheckResult &result : m_session.checkResults()) {
        values.append(QStringLiteral("%1  %2 — %3")
            .arg(result.checkId.value(), displayName(result.status), result.summary));
    }
    return values;
}
QStringList DoctorSessionViewModel::findingItems() const
{
    QStringList values;
    for (const Finding &finding : m_session.findings()) values.append(finding.title + QStringLiteral(" — ") + finding.explanation);
    return values;
}
QString DoctorSessionViewModel::userActionTitle() const { return m_session.userAction().title; }
QString DoctorSessionViewModel::userActionDetail() const { return m_session.userAction().explanation; }
bool DoctorSessionViewModel::scanRunning() const { return m_session.state() == DoctorSessionState::Preparing || m_session.state() == DoctorSessionState::Diagnosing || m_session.state() == DoctorSessionState::Analyzing; }
int DoctorSessionViewModel::completedChecks() const { return m_session.plan().progress().completedChecks; }
int DoctorSessionViewModel::remainingChecks() const
{
    const OperationProgress progress = m_session.plan().progress();
    return std::max(0, progress.applicableChecks - progress.completedChecks);
}
int DoctorSessionViewModel::warningOrFailureCount() const
{
    int count = 0;
    for (const DoctorCheckResult &result : m_session.checkResults()) {
        if (result.status == DoctorCheckStatus::Warning || result.status == DoctorCheckStatus::Failed
            || result.status == DoctorCheckStatus::PermissionLimited || result.status == DoctorCheckStatus::TimedOut) ++count;
    }
    return count;
}
QString DoctorSessionViewModel::elapsed() const
{
    const qint64 milliseconds = m_session.createdAt().msecsTo(QDateTime::currentDateTimeUtc());
    return QStringLiteral("%1 ms").arg(std::max<qint64>(0, milliseconds));
}
bool DoctorSessionViewModel::commandCenter() const { return m_commandCenter; }
void DoctorSessionViewModel::togglePresentation()
{
    m_commandCenter = !m_commandCenter;
    emit presentationChanged();
}
void DoctorSessionViewModel::notifySessionChanged() { emit sessionChanged(); }
void DoctorSessionViewModel::requestCancellation() { if (m_cancellation) m_cancellation(); }
void DoctorSessionViewModel::requestRerun() { if (m_rerun) m_rerun(); }
void DoctorSessionViewModel::replaceSession(DoctorSession session)
{
    m_session = std::move(session);
    emit sessionChanged();
}
void DoctorSessionViewModel::setScanActions(std::function<void()> cancellation, std::function<void()> rerun)
{
    m_cancellation = std::move(cancellation);
    m_rerun = std::move(rerun);
}

} // namespace hotas::doctor
