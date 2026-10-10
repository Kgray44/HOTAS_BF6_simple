#pragma once

#include "doctor_session.h"

#include <QObject>

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
    Q_PROPERTY(QStringList findingItems READ findingItems NOTIFY sessionChanged)
    Q_PROPERTY(QString userActionTitle READ userActionTitle NOTIFY sessionChanged)
    Q_PROPERTY(QString userActionDetail READ userActionDetail NOTIFY sessionChanged)
    Q_PROPERTY(bool commandCenter READ commandCenter NOTIFY presentationChanged)
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
    QStringList findingItems() const;
    QString userActionTitle() const;
    QString userActionDetail() const;
    bool commandCenter() const;
    Q_INVOKABLE void togglePresentation();
signals:
    void sessionChanged();
    void presentationChanged();
private:
    DoctorSession &m_session;
    QString m_buildIdentity;
    bool m_commandCenter = false;
};

} // namespace hotas::doctor
