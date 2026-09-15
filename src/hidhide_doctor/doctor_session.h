#pragma once

#include "doctor_catalog.h"

namespace hotas::doctor {

struct DiagnosticPlanItem final {
    DoctorStepId stepId;
    DoctorCheckDefinition check;
    DoctorCheckStatus status = DoctorCheckStatus::Waiting;
    int currentStepProgressPercent = 0;
    QString statusDetail;
};

struct OperationProgress final {
    int overallPercent = 0;
    int currentStepPercent = 0;
    int completedChecks = 0;
    int applicableChecks = 0;
    int currentOrdinal = 0;
    int totalSteps = 0;
};

class DiagnosticPlan final {
public:
    bool addItem(DiagnosticPlanItem item, QString *reason = nullptr);
    bool freeze(QString *reason = nullptr);
    bool appendBoundedExtension(DiagnosticPlanItem item, QString *reason = nullptr);
    bool setStatus(const DoctorStepId &step, DoctorCheckStatus status, QString detail = {}, QString *reason = nullptr);
    bool setCurrentStepProgress(const DoctorStepId &step, int percent, QString *reason = nullptr);
    bool isFrozen() const;
    const QList<DiagnosticPlanItem> &items() const;
    OperationProgress progress() const;

private:
    int indexOf(const DoctorStepId &step) const;
    int computeOverallPercent() const;
    QList<DiagnosticPlanItem> m_items;
    bool m_frozen = false;
    bool m_extensionUsed = false;
    int m_progressHighWater = 0;
};

class DoctorSession final {
public:
    DoctorSession();
    explicit DoctorSession(DoctorSessionId id);

    const DoctorSessionId &id() const;
    DoctorSessionState state() const;
    bool transitionTo(DoctorSessionState next, QString *reason = nullptr);
    DiagnosticPlan &plan();
    const DiagnosticPlan &plan() const;
    void appendEvidence(EvidenceRecord evidence);
    void appendCheckResult(DoctorCheckResult result);
    void appendFinding(Finding finding);
    void appendDiagnosis(Diagnosis diagnosis);
    void setUserAction(UserAction action);
    void setCurrentOperation(DoctorOperation operation);
    const QList<EvidenceRecord> &evidence() const;
    const QList<DoctorCheckResult> &checkResults() const;
    const QList<Finding> &findings() const;
    const QList<Diagnosis> &diagnoses() const;
    const UserAction &userAction() const;
    const std::optional<DoctorOperation> &currentOperation() const;
    QDateTime createdAt() const;

private:
    DoctorSessionId m_id;
    DoctorSessionState m_state = DoctorSessionState::Preparing;
    QDateTime m_createdAt;
    DiagnosticPlan m_plan;
    QList<EvidenceRecord> m_evidence;
    QList<DoctorCheckResult> m_checkResults;
    QList<Finding> m_findings;
    QList<Diagnosis> m_diagnoses;
    UserAction m_userAction;
    std::optional<DoctorOperation> m_currentOperation;
};

DoctorSession createPhase0FixtureSession();

} // namespace hotas::doctor
