#pragma once

#include "doctor_session.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace hotas::doctor {

// This is a presentation adapter over one session. It owns no diagnostics,
// environment provider, or repair capability; a layout toggle cannot restart
// or mutate the work it renders.
class DoctorSessionViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString buildIdentity READ buildIdentity CONSTANT)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY sessionChanged)
    Q_PROPERTY(QString currentPhase READ currentPhase NOTIFY sessionChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY sessionChanged)
    Q_PROPERTY(QString currentStepId READ currentStepId NOTIFY sessionChanged)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY sessionChanged)
    Q_PROPERTY(int currentStepProgress READ currentStepProgress NOTIFY sessionChanged)
    Q_PROPERTY(QStringList planItems READ planItems NOTIFY sessionChanged)
    Q_PROPERTY(QStringList resultItems READ resultItems NOTIFY sessionChanged)
    Q_PROPERTY(QStringList findingItems READ findingItems NOTIFY sessionChanged)
    Q_PROPERTY(QString userActionTitle READ userActionTitle NOTIFY sessionChanged)
    Q_PROPERTY(QString userActionDetail READ userActionDetail NOTIFY sessionChanged)
    Q_PROPERTY(bool scanRunning READ scanRunning NOTIFY sessionChanged)
    Q_PROPERTY(int completedChecks READ completedChecks NOTIFY sessionChanged)
    Q_PROPERTY(int remainingChecks READ remainingChecks NOTIFY sessionChanged)
    Q_PROPERTY(int warningOrFailureCount READ warningOrFailureCount NOTIFY sessionChanged)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY sessionChanged)
    Q_PROPERTY(bool commandCenter READ commandCenter NOTIFY presentationChanged)
    Q_PROPERTY(QString density READ density NOTIFY presentationChanged)
    Q_PROPERTY(QString environmentStrip READ environmentStrip NOTIFY sessionChanged)
    Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionChanged)
    Q_PROPERTY(QString simulationLabel READ simulationLabel NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList healthDomains READ healthDomains NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList planPhases READ planPhases NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList findingCards READ findingCards NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList diagnosisCards READ diagnosisCards NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList activityRows READ activityRows NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList evidenceRows READ evidenceRows NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap selectedEvidence READ selectedEvidence NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap currentOperationDetails READ currentOperationDetails NOTIFY sessionChanged)
    Q_PROPERTY(int healthyCheckCount READ healthyCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int informationalCheckCount READ informationalCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int warningCheckCount READ warningCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int failedCheckCount READ failedCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(bool liveEvidenceVisible READ liveEvidenceVisible NOTIFY presentationChanged)
    Q_PROPERTY(QString maximizedPane READ maximizedPane NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList paneWidths READ paneWidths NOTIFY presentationChanged)
public:
    explicit DoctorSessionViewModel(DoctorSession &session, QString buildIdentity, QObject *parent = nullptr);
    QString buildIdentity() const;
    QString sessionId() const;
    QString currentPhase() const;
    QString currentStep() const;
    QString currentStepId() const;
    int overallProgress() const;
    int currentStepProgress() const;
    QStringList planItems() const;
    QStringList resultItems() const;
    QStringList findingItems() const;
    QString userActionTitle() const;
    QString userActionDetail() const;
    bool scanRunning() const;
    int completedChecks() const;
    int remainingChecks() const;
    int warningOrFailureCount() const;
    QString elapsed() const;
    bool commandCenter() const;
    QString density() const;
    QString environmentStrip() const;
    QString sessionState() const;
    QString simulationLabel() const;
    QVariantList healthDomains() const;
    QVariantList planPhases() const;
    QVariantList findingCards() const;
    QVariantList diagnosisCards() const;
    QVariantList activityRows() const;
    QVariantList evidenceRows() const;
    QVariantMap selectedEvidence() const;
    QVariantMap currentOperationDetails() const;
    int healthyCheckCount() const;
    int informationalCheckCount() const;
    int warningCheckCount() const;
    int failedCheckCount() const;
    bool liveEvidenceVisible() const;
    QString maximizedPane() const;
    QVariantList paneWidths() const;
    Q_INVOKABLE void togglePresentation();
    Q_INVOKABLE void setCommandCenter(bool commandCenter);
    Q_INVOKABLE void setDensity(const QString &density);
    Q_INVOKABLE void setLiveEvidenceVisible(bool visible);
    Q_INVOKABLE void setMaximizedPane(const QString &pane);
    Q_INVOKABLE void savePaneWidths(const QVariantList &widths);
    Q_INVOKABLE void resetPaneWidths();
    Q_INVOKABLE void selectEvidence(const QString &evidenceId);
    Q_INVOKABLE void requestCancellation();
    Q_INVOKABLE void requestRerun();
    void notifySessionChanged();
    void replaceSession(DoctorSession session);
    void setScanActions(std::function<void()> cancellation, std::function<void()> rerun);
signals:
    void sessionChanged();
    void presentationChanged();
private:
    DoctorSession m_session;
    QString m_buildIdentity;
    bool m_commandCenter = false;
    QString m_density = QStringLiteral("Compact");
    bool m_liveEvidenceVisible = false;
    QString m_maximizedPane;
    QVariantList m_paneWidths;
    QString m_selectedEvidenceId;
    std::function<void()> m_cancellation;
    std::function<void()> m_rerun;
};

} // namespace hotas::doctor
