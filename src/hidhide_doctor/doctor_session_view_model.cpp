#include "doctor_session_view_model.h"

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
    return QStringLiteral("Phase 0 fixture");
}
QString DoctorSessionViewModel::currentStep() const
{
    for (const DiagnosticPlanItem &item : m_session.plan().items()) {
        if (item.status == DoctorCheckStatus::Running) return item.check.title;
    }
    return QStringLiteral("No running operation");
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
QStringList DoctorSessionViewModel::findingItems() const
{
    QStringList values;
    for (const Finding &finding : m_session.findings()) values.append(finding.title + QStringLiteral(" — ") + finding.explanation);
    return values;
}
QString DoctorSessionViewModel::userActionTitle() const { return m_session.userAction().title; }
QString DoctorSessionViewModel::userActionDetail() const { return m_session.userAction().explanation; }
bool DoctorSessionViewModel::commandCenter() const { return m_commandCenter; }
void DoctorSessionViewModel::togglePresentation()
{
    m_commandCenter = !m_commandCenter;
    emit presentationChanged();
}

} // namespace hotas::doctor
