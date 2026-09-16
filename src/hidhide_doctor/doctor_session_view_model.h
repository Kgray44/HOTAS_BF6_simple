#pragma once

#include "doctor_session.h"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace hotas::doctor {

// This is a presentation adapter over one session. It owns no diagnostics,
// environment provider, or repair capability; the optional lab callbacks are
// supplied by the executable and remain unavailable in normal Doctor mode.
class DoctorSessionViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString buildIdentity READ buildIdentity CONSTANT)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY sessionChanged)
    Q_PROPERTY(QString currentPhase READ currentPhase NOTIFY sessionChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY sessionChanged)
    Q_PROPERTY(QString currentStepId READ currentStepId NOTIFY sessionChanged)
    Q_PROPERTY(int overallProgress READ overallProgress NOTIFY sessionChanged)
    Q_PROPERTY(int currentStepProgress READ currentStepProgress NOTIFY sessionChanged)
    Q_PROPERTY(double displayProgress READ displayProgress NOTIFY progressPresentationChanged)
    Q_PROPERTY(double displayCurrentStepProgress READ displayCurrentStepProgress NOTIFY progressPresentationChanged)
    Q_PROPERTY(QString presentationElapsed READ presentationElapsed NOTIFY progressPresentationChanged)
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
    Q_PROPERTY(QVariantList environmentGroups READ environmentGroups NOTIFY sessionChanged)
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
    Q_PROPERTY(bool repairPlanAvailable READ repairPlanAvailable NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap repairPlanSummary READ repairPlanSummary NOTIFY sessionChanged)
    Q_PROPERTY(QStringList repairPlanOperations READ repairPlanOperations NOTIFY sessionChanged)
    Q_PROPERTY(QStringList repairPlanCollateral READ repairPlanCollateral NOTIFY sessionChanged)
    Q_PROPERTY(bool labRepairMode READ labRepairMode NOTIFY repairRuntimeChanged)
    Q_PROPERTY(QString repairRuntimeState READ repairRuntimeState NOTIFY repairRuntimeChanged)
    Q_PROPERTY(QString repairRuntimeDetail READ repairRuntimeDetail NOTIFY repairRuntimeChanged)
    Q_PROPERTY(bool repairOperationInFlight READ repairOperationInFlight NOTIFY repairRuntimeChanged)
    Q_PROPERTY(QString recoveryNotice READ recoveryNotice NOTIFY repairRuntimeChanged)
    Q_PROPERTY(int healthyCheckCount READ healthyCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int informationalCheckCount READ informationalCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int warningCheckCount READ warningCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(int failedCheckCount READ failedCheckCount NOTIFY sessionChanged)
    Q_PROPERTY(bool liveEvidenceVisible READ liveEvidenceVisible NOTIFY presentationChanged)
    Q_PROPERTY(QString maximizedPane READ maximizedPane NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList paneFractions READ paneFractions NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList bottomDockFractions READ bottomDockFractions NOTIFY presentationChanged)
    Q_PROPERTY(int layoutResetEpoch READ layoutResetEpoch NOTIFY presentationChanged)
    Q_PROPERTY(QString integrationNotice READ integrationNotice NOTIFY presentationChanged)
    Q_PROPERTY(bool integrationComponentMismatch READ integrationComponentMismatch NOTIFY presentationChanged)
    Q_PROPERTY(bool exportAvailable READ exportAvailable NOTIFY presentationChanged)
    Q_PROPERTY(bool reportBusy READ reportBusy NOTIFY presentationChanged)
    Q_PROPERTY(QString reportStatus READ reportStatus NOTIFY presentationChanged)
public:
    explicit DoctorSessionViewModel(DoctorSession &session, QString buildIdentity, QObject *parent = nullptr);
    QString buildIdentity() const;
    QString sessionId() const;
    QString currentPhase() const;
    QString currentStep() const;
    QString currentStepId() const;
    int overallProgress() const;
    int currentStepProgress() const;
    double displayProgress() const;
    double displayCurrentStepProgress() const;
    QString presentationElapsed() const;
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
    QVariantList environmentGroups() const;
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
    bool repairPlanAvailable() const;
    QVariantMap repairPlanSummary() const;
    QStringList repairPlanOperations() const;
    QStringList repairPlanCollateral() const;
    bool labRepairMode() const;
    QString repairRuntimeState() const;
    QString repairRuntimeDetail() const;
    bool repairOperationInFlight() const;
    QString recoveryNotice() const;
    int healthyCheckCount() const;
    int informationalCheckCount() const;
    int warningCheckCount() const;
    int failedCheckCount() const;
    bool liveEvidenceVisible() const;
    QString maximizedPane() const;
    QVariantList paneFractions() const;
    QVariantList bottomDockFractions() const;
    int layoutResetEpoch() const;
    QString integrationNotice() const;
    bool integrationComponentMismatch() const;
    bool exportAvailable() const;
    bool reportBusy() const;
    QString reportStatus() const;
    static QVariantList defaultPaneFractions();
    static QVariantList normalizedPaneFractions(const QVariantList &candidate);
    Q_INVOKABLE void togglePresentation();
    Q_INVOKABLE void setCommandCenter(bool commandCenter);
    Q_INVOKABLE void setDensity(const QString &density);
    Q_INVOKABLE void setLiveEvidenceVisible(bool visible);
    Q_INVOKABLE void setMaximizedPane(const QString &pane);
    Q_INVOKABLE void savePaneFractions(const QVariantList &fractions);
    Q_INVOKABLE void saveBottomDockFractions(const QVariantList &fractions);
    Q_INVOKABLE void resetWorkspaceLayout();
    Q_INVOKABLE void selectEvidence(const QString &evidenceId);
    Q_INVOKABLE void requestCancellation();
    Q_INVOKABLE void requestRerun();
    Q_INVOKABLE void requestHelperConnectivityTest();
    Q_INVOKABLE void requestLabRepairAuthorization();
    Q_INVOKABLE void copyReportSection(const QString &scope, const QString &format = QStringLiteral("Markdown"),
                                       const QString &privacy = QStringLiteral("Safe to Share"));
    Q_INVOKABLE void copySelectedEvidence(const QString &format = QStringLiteral("Markdown"),
                                          const QString &privacy = QStringLiteral("Safe to Share"));
    Q_INVOKABLE void exportReport(const QString &fileName, const QString &scope, const QString &detail,
                                  const QString &format, const QString &privacy);
    Q_INVOKABLE void exportDiagnosticBundle(const QString &directory, const QString &privacy = QStringLiteral("Safe to Share"));
    Q_INVOKABLE bool exportDiagnosticReport(const QString &fileName);
    void notifySessionChanged();
    void replaceSession(DoctorSession session);
    void setScanActions(std::function<void()> cancellation, std::function<void()> rerun);
    void setLabRepairActions(bool enabled, std::function<void(bool)> action);
    void setCopyAction(std::function<void(QString)> action);
    void setRepairRuntime(QString state, QString detail, bool inFlight);
    void setRecoveryNotice(QString notice);
    void setIntegrationNotice(QString notice, bool componentMismatch);
    void setRedactedDiagnosticReport(QByteArray report);
signals:
    void sessionChanged();
    void presentationChanged();
    void repairRuntimeChanged();
    void progressPresentationChanged();
private:
    void updateProgressPresentation();
    void beginAsynchronousExport(QString destination, QString scope, QString detail, QString format, QString privacy);
    DoctorSession m_session;
    QString m_buildIdentity;
    bool m_commandCenter = false;
    QString m_density = QStringLiteral("Compact");
    bool m_liveEvidenceVisible = false;
    QString m_maximizedPane;
    QVariantList m_paneFractions;
    QVariantList m_bottomDockFractions;
    int m_layoutResetEpoch = 0;
    QString m_selectedEvidenceId;
    std::function<void()> m_cancellation;
    std::function<void()> m_rerun;
    bool m_labRepairMode = false;
    bool m_repairOperationInFlight = false;
    QString m_repairRuntimeState = QStringLiteral("READ ONLY");
    QString m_repairRuntimeDetail = QStringLiteral("No repair request is active.");
    QString m_recoveryNotice;
    std::function<void(bool)> m_labRepairAction;
    std::function<void(QString)> m_copyAction;
    QString m_integrationNotice;
    bool m_integrationComponentMismatch = false;
    QByteArray m_redactedDiagnosticReport;
    QElapsedTimer m_scanClock;
    QElapsedTimer m_activeOperationClock;
    QTimer m_progressPresentationTimer;
    QString m_activePresentationStep;
    double m_displayProgress = 0.0;
    double m_displayCurrentStepProgress = 0.0;
    qint64 m_completedPresentationMs = -1;
    bool m_reportBusy = false;
    QString m_reportStatus;
};

} // namespace hotas::doctor
