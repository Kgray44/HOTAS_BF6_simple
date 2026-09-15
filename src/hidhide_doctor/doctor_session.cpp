#include "doctor_session.h"

#include <algorithm>

namespace hotas::doctor {
namespace {

bool contributesCompletedWork(DoctorCheckStatus status)
{
    return isTerminal(status) && status != DoctorCheckStatus::NotApplicable
        && status != DoctorCheckStatus::Cancelled;
}

bool isApplicable(const DiagnosticPlanItem &item)
{
    return item.status != DoctorCheckStatus::NotApplicable;
}

} // namespace

bool DiagnosticPlan::addItem(DiagnosticPlanItem item, QString *reason)
{
    if (m_frozen) {
        if (reason) *reason = QStringLiteral("Diagnostic plan is frozen. Use an explicit bounded extension.");
        return false;
    }
    if (!item.stepId.isValid() || !item.check.id.isValid() || !item.check.weight.isValid()) {
        if (reason) *reason = QStringLiteral("Plan item requires stable IDs and a positive work weight.");
        return false;
    }
    if (indexOf(item.stepId) >= 0) {
        if (reason) *reason = QStringLiteral("Duplicate DoctorStepId: %1").arg(item.stepId.value());
        return false;
    }
    m_items.append(std::move(item));
    return true;
}

bool DiagnosticPlan::freeze(QString *reason)
{
    if (m_items.isEmpty()) {
        if (reason) *reason = QStringLiteral("A diagnostic plan must contain applicable or explicitly not-applicable work.");
        return false;
    }
    m_frozen = true;
    m_progressHighWater = computeOverallPercent();
    return true;
}

bool DiagnosticPlan::appendBoundedExtension(DiagnosticPlanItem item, QString *reason)
{
    if (!m_frozen || m_extensionUsed || item.check.phase != DoctorPhase::ExtendedInvestigation) {
        if (reason) *reason = QStringLiteral("Only one explicit Extended Investigation item may extend a frozen plan.");
        return false;
    }
    m_frozen = false;
    const bool added = addItem(std::move(item), reason);
    m_frozen = true;
    if (added) m_extensionUsed = true;
    return added;
}

bool DiagnosticPlan::setStatus(const DoctorStepId &step, DoctorCheckStatus status, QString detail, QString *reason)
{
    const int index = indexOf(step);
    if (index < 0) {
        if (reason) *reason = QStringLiteral("Unknown DoctorStepId: %1").arg(step.value());
        return false;
    }
    DiagnosticPlanItem &item = m_items[index];
    if (isTerminal(item.status) && item.status != DoctorCheckStatus::Waiting) {
        if (reason) *reason = QStringLiteral("A completed plan item cannot be silently rewritten.");
        return false;
    }
    if (item.status == DoctorCheckStatus::Waiting && status == DoctorCheckStatus::Running) {
        // expected forward transition
    } else if (item.status == DoctorCheckStatus::Waiting && isTerminal(status)) {
        // A prerequisite can make a check Not Applicable/Blocked-equivalent
        // before it runs. The outcome remains explicit in evidence.
    } else if (item.status == DoctorCheckStatus::Running && isTerminal(status)) {
        // expected completion
    } else if (item.status != status) {
        if (reason) *reason = QStringLiteral("Invalid check-status transition.");
        return false;
    }
    item.status = status;
    item.statusDetail = std::move(detail);
    if (isTerminal(status)) item.currentStepProgressPercent = 100;
    m_progressHighWater = std::max(m_progressHighWater, computeOverallPercent());
    return true;
}

bool DiagnosticPlan::setCurrentStepProgress(const DoctorStepId &step, int percent, QString *reason)
{
    const int index = indexOf(step);
    if (index < 0 || m_items[index].status != DoctorCheckStatus::Running) {
        if (reason) *reason = QStringLiteral("Current-step progress requires a running plan item.");
        return false;
    }
    DiagnosticPlanItem &item = m_items[index];
    if (percent < item.currentStepProgressPercent || percent < 0 || percent > 100) {
        if (reason) *reason = QStringLiteral("Current-step progress may only advance from 0 to 100.");
        return false;
    }
    item.currentStepProgressPercent = percent;
    m_progressHighWater = std::max(m_progressHighWater, computeOverallPercent());
    return true;
}

bool DiagnosticPlan::isFrozen() const { return m_frozen; }
const QList<DiagnosticPlanItem> &DiagnosticPlan::items() const { return m_items; }

OperationProgress DiagnosticPlan::progress() const
{
    OperationProgress result;
    result.totalSteps = m_items.size();
    for (int index = 0; index < m_items.size(); ++index) {
        const DiagnosticPlanItem &item = m_items.at(index);
        if (isApplicable(item)) ++result.applicableChecks;
        if (contributesCompletedWork(item.status)) ++result.completedChecks;
        if (item.status == DoctorCheckStatus::Running) {
            result.currentOrdinal = index + 1;
            result.currentStepPercent = item.currentStepProgressPercent;
        }
    }
    result.overallPercent = std::max(m_progressHighWater, computeOverallPercent());
    return result;
}

int DiagnosticPlan::indexOf(const DoctorStepId &step) const
{
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).stepId.value() == step.value()) return index;
    }
    return -1;
}

int DiagnosticPlan::computeOverallPercent() const
{
    qint64 totalWeight = 0;
    qint64 completedWeight = 0;
    qint64 partialPercentWeight = 0;
    for (const DiagnosticPlanItem &item : m_items) {
        if (!isApplicable(item)) continue;
        totalWeight += item.check.weight.units;
        if (contributesCompletedWork(item.status)) {
            completedWeight += item.check.weight.units;
        } else if (item.status == DoctorCheckStatus::Running) {
            // Preserve fractional work until the final division. Rounding a
            // 3-unit step at 50% down to one unit would turn truthful 70%
            // progress into 60%.
            partialPercentWeight += static_cast<qint64>(item.check.weight.units) * item.currentStepProgressPercent;
        }
    }
    if (totalWeight == 0) return 100;
    return static_cast<int>((completedWeight * 100 + partialPercentWeight) / totalWeight);
}

DoctorSession::DoctorSession() : DoctorSession(DoctorSessionId::create()) {}
DoctorSession::DoctorSession(DoctorSessionId id) : m_id(std::move(id)), m_createdAt(QDateTime::currentDateTimeUtc()) {}
const DoctorSessionId &DoctorSession::id() const { return m_id; }
DoctorSessionState DoctorSession::state() const { return m_state; }

bool DoctorSession::transitionTo(DoctorSessionState next, QString *reason)
{
    if (!isTransitionAllowed(m_state, next)) {
        if (reason) *reason = QStringLiteral("Invalid DoctorSession state transition.");
        return false;
    }
    m_state = next;
    return true;
}

DiagnosticPlan &DoctorSession::plan() { return m_plan; }
const DiagnosticPlan &DoctorSession::plan() const { return m_plan; }
void DoctorSession::appendEvidence(EvidenceRecord evidence) { if (!evidence.id.isValid()) evidence.id = EvidenceId::create(); if (!evidence.recordedAt.isValid()) evidence.recordedAt = QDateTime::currentDateTimeUtc(); m_evidence.append(std::move(evidence)); }
void DoctorSession::appendCheckResult(DoctorCheckResult result) { m_checkResults.append(std::move(result)); }
void DoctorSession::appendFinding(Finding finding) { m_findings.append(std::move(finding)); }
void DoctorSession::appendDiagnosis(Diagnosis diagnosis) { m_diagnoses.append(std::move(diagnosis)); }
void DoctorSession::appendActivity(DoctorActivityEvent event)
{
    if (!event.timestamp.isValid()) event.timestamp = QDateTime::currentDateTimeUtc();
    // The timeline is intentionally bounded: a long device or Event Log scan
    // must not turn UI transparency into unbounded retained UI work.
    constexpr int maxActivityEvents = 600;
    if (m_activity.size() >= maxActivityEvents) m_activity.remove(0, m_activity.size() - maxActivityEvents + 1);
    m_activity.append(std::move(event));
}
void DoctorSession::setUserAction(UserAction action) { m_userAction = std::move(action); }
void DoctorSession::setCurrentOperation(DoctorOperation operation) { m_currentOperation = std::move(operation); }
void DoctorSession::setEnvironment(DoctorEnvironment environment) { m_environment = std::move(environment); }
void DoctorSession::setSessionLabel(QString label) { m_sessionLabel = std::move(label); }
const QList<EvidenceRecord> &DoctorSession::evidence() const { return m_evidence; }
const QList<DoctorCheckResult> &DoctorSession::checkResults() const { return m_checkResults; }
const QList<Finding> &DoctorSession::findings() const { return m_findings; }
const QList<Diagnosis> &DoctorSession::diagnoses() const { return m_diagnoses; }
const QList<DoctorActivityEvent> &DoctorSession::activity() const { return m_activity; }
const UserAction &DoctorSession::userAction() const { return m_userAction; }
const std::optional<DoctorOperation> &DoctorSession::currentOperation() const { return m_currentOperation; }
const std::optional<DoctorEnvironment> &DoctorSession::environment() const { return m_environment; }
const QString &DoctorSession::sessionLabel() const { return m_sessionLabel; }
QDateTime DoctorSession::createdAt() const { return m_createdAt; }

DoctorSession createPhase0FixtureSession()
{
    DoctorSession session;
    session.transitionTo(DoctorSessionState::Diagnosing);
    session.plan().addItem({DoctorStepId(QStringLiteral("STEP-SYS-001")),
        {DoctorCheckId(QStringLiteral("HD-SYS-001")), QStringLiteral("Windows product/version/build"), DoctorPhase::SystemEnvironment, {}, {2}, 2000},
        DoctorCheckStatus::Healthy, 100, QStringLiteral("Fixture environment recorded")});
    session.plan().addItem({DoctorStepId(QStringLiteral("STEP-PORT-001")),
        {DoctorCheckId(QStringLiteral("HD-PORT-001")), QStringLiteral("Windows edition"), DoctorPhase::SystemEnvironment, {}, {1}, 2000},
        DoctorCheckStatus::Healthy, 100, QStringLiteral("Fixture Windows edition recorded")});
    session.plan().addItem({DoctorStepId(QStringLiteral("STEP-API-001")),
        {DoctorCheckId(QStringLiteral("HD-API-001")), QStringLiteral("Control-device baseline open"), DoctorPhase::ProtocolApiHealth, {}, {3}, 5000},
        DoctorCheckStatus::Running, 45, QStringLiteral("Fixture-only protocol adapter is active")});
    session.plan().freeze();
    session.setCurrentOperation({DoctorOperationId(QStringLiteral("OP-FIXTURE-PROTOCOL")), DoctorOperationState::Running,
        QStringLiteral("Reading fixture protocol capability"), 45, 5000});
    session.appendEvidence({EvidenceId::create(), DoctorCheckId(QStringLiteral("HD-SYS-001")), EvidenceKind::Observation,
        EvidenceProvenance::Fixture, EvidenceSensitivity::SafeToExport, QDateTime::currentDateTimeUtc(),
        QStringLiteral("phase0-fixture"), QStringLiteral("Windows 11 x64 fixture selected"),
        QStringLiteral("No local HidHide configuration was read or changed."), QStringLiteral("build=26100"), std::nullopt, 1, true});
    session.appendFinding({FindingId(QStringLiteral("FINDING-PHASE0-FIXTURE")), FindingSeverity::Informational,
        QStringLiteral("Phase 0 fixture session"), QStringLiteral("The standalone shell is rendering canonical fixture data only."), {}});
    session.setUserAction({UserActionState::NothingRequired, FindingSeverity::Informational,
        QStringLiteral("Nothing required"), QStringLiteral("Phase 0 performs no repair or elevation."), {}, {}, {}});
    return session;
}

} // namespace hotas::doctor
