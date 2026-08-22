#pragma once

#include "mapping_worker.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace hotas {

class AppBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList axes READ axes NOTIFY stateChanged)
    Q_PROPERTY(int selectedAxisIndex READ selectedAxisIndex NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedAxisCurve READ selectedAxisCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY stateChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY stateChanged)
    Q_PROPERTY(int activeProfileIndex READ activeProfileIndex NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY stateChanged)
    Q_PROPERTY(bool physicalConnected READ physicalConnected NOTIFY stateChanged)
    Q_PROPERTY(int axisCount READ axisCount NOTIFY stateChanged)
    Q_PROPERTY(int buttonCount READ buttonCount NOTIFY stateChanged)
    Q_PROPERTY(int povCount READ povCount NOTIFY stateChanged)
    Q_PROPERTY(int povValue READ povValue NOTIFY stateChanged)
    Q_PROPERTY(int vjoyButtonCount READ vjoyButtonCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRequiredButtonCount READ vjoyRequiredButtonCount NOTIFY stateChanged)
    Q_PROPERTY(bool vjoyCapacitySufficient READ vjoyCapacitySufficient NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRecommendedButtonCount READ vjoyRecommendedButtonCount CONSTANT)
    Q_PROPERTY(int lastPhysicalButton READ lastPhysicalButton NOTIFY stateChanged)
    Q_PROPERTY(int lastPhysicalButtonTarget READ lastPhysicalButtonTarget NOTIFY stateChanged)
    Q_PROPERTY(bool mappingActive READ mappingActive NOTIFY stateChanged)
    Q_PROPERTY(bool vjoyReady READ vjoyReady NOTIFY stateChanged)
    Q_PROPERTY(QString vjoyStatus READ vjoyStatus NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideAvailable READ hidhideAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideCloakStateKnown READ hidhideCloakStateKnown NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideCloaked READ hidhideCloaked NOTIFY stateChanged)
    Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY stateChanged)
    Q_PROPERTY(bool startMappingOnLaunch READ startMappingOnLaunch NOTIFY stateChanged)
    Q_PROPERTY(int vjoyDeviceId READ vjoyDeviceId NOTIFY stateChanged)
    Q_PROPERTY(double inputReportsPerSecond READ inputReportsPerSecond NOTIFY stateChanged)
    Q_PROPERTY(qint64 lastPhysicalUpdateAgeMs READ lastPhysicalUpdateAgeMs NOTIFY stateChanged)
    Q_PROPERTY(double vjoyWritesPerSecond READ vjoyWritesPerSecond NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyCurrentUs READ latencyCurrentUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyAverageUs READ latencyAverageUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyPeakUs READ latencyPeakUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong profileSwitchCount READ profileSwitchCount NOTIFY stateChanged)
    Q_PROPERTY(qulonglong lastProfileSwapUs READ lastProfileSwapUs NOTIFY stateChanged)
    Q_PROPERTY(QStringList buttonOutputChoices READ buttonOutputChoices NOTIFY stateChanged)
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY eventLogChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend() override;

    QVariantList axes() const;
    int selectedAxisIndex() const;
    QVariantList selectedAxisCurve() const;
    QVariantList buttons() const;
    QVariantList profiles() const;
    QString activeProfileId() const;
    QString activeProfileName() const;
    int activeProfileIndex() const;
    QString deviceName() const;
    QString deviceId() const;
    bool physicalConnected() const;
    int axisCount() const;
    int buttonCount() const;
    int povCount() const;
    int povValue() const;
    int vjoyButtonCount() const;
    int vjoyRequiredButtonCount() const;
    bool vjoyCapacitySufficient() const;
    int vjoyRecommendedButtonCount() const { return 32; }
    int lastPhysicalButton() const;
    int lastPhysicalButtonTarget() const;
    bool mappingActive() const;
    bool vjoyReady() const;
    QString vjoyStatus() const;
    bool hidhideAvailable() const;
    bool hidhideCloakStateKnown() const;
    bool hidhideCloaked() const;
    bool calibrationActive() const;
    bool startMappingOnLaunch() const;
    int vjoyDeviceId() const;
    double inputReportsPerSecond() const { return m_inputReportsPerSecond; }
    qint64 lastPhysicalUpdateAgeMs() const { return m_lastPhysicalUpdateAgeMs; }
    double vjoyWritesPerSecond() const { return m_vjoyWritesPerSecond; }
    qulonglong latencyCurrentUs() const;
    qulonglong latencyAverageUs() const;
    qulonglong latencyPeakUs() const;
    qulonglong profileSwitchCount() const;
    qulonglong lastProfileSwapUs() const;
    QStringList buttonOutputChoices() const;
    QStringList eventLog() const { return m_events; }

    Q_INVOKABLE void toggleMapping();
    Q_INVOKABLE void setMappingActive(bool active);
    Q_INVOKABLE bool setMapping(int physicalAxis, const QString &target, bool explicitOverride = false);
    Q_INVOKABLE void setSelectedAxis(int physicalAxis);
    Q_INVOKABLE void setAxisInverted(int physicalAxis, bool inverted);
    Q_INVOKABLE void setAxisDeadzone(int physicalAxis, double deadzone);
    Q_INVOKABLE void setAxisHysteresis(int physicalAxis, double hysteresis);
    Q_INVOKABLE bool setAxisOutputLimits(int physicalAxis, double minimum, double maximum);
    Q_INVOKABLE bool setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride = false);
    Q_INVOKABLE void resetButtonMappings();
    Q_INVOKABLE bool createProfile(const QString &name, const QString &startFromId = {});
    Q_INVOKABLE bool cloneProfile(const QString &profileId);
    Q_INVOKABLE bool renameProfile(const QString &profileId, const QString &name);
    Q_INVOKABLE bool deleteProfile(const QString &profileId);
    Q_INVOKABLE bool activateProfile(const QString &profileId);
    Q_INVOKABLE void beginCalibration();
    Q_INVOKABLE bool saveCalibration();
    Q_INVOKABLE void resetCalibration();
    Q_INVOKABLE void setStartMappingOnLaunch(bool enabled);
    Q_INVOKABLE void setVjoyDeviceId(int deviceId);
    Q_INVOKABLE bool openVjoyConfiguration();
    Q_INVOKABLE void refreshHidHideStatus();
    Q_INVOKABLE bool openHidHideConfiguration();
    Q_INVOKABLE void useConnectedDevice();
    Q_INVOKABLE void resetApplicationConfiguration();

signals:
    void stateChanged();
    void selectedAxisCurveChanged();
    void eventLogChanged();

private slots:
    void refreshUiSnapshot();
    void appendEvent(const QString &event);
    void initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity);

private:
    void persistAndApply();
    void rebuildSelectedAxisCurve();
    bool fallBackToAvailableAxis();
    bool validAxis(int physicalAxis) const;
    bool validPhysicalButton(int physicalButton) const;
    const ControllerProfile &currentProfile() const;
    ControllerProfile &currentProfile();

    MapperConfiguration m_configuration;
    MappingWorker m_worker;
    QTimer m_snapshotTimer;
    QElapsedTimer m_rateClock;
    QElapsedTimer m_physicalUpdateClock;
    quint64 m_previousInputReports = 0;
    quint64 m_previousVjoyWrites = 0;
    double m_inputReportsPerSecond = 0.0;
    qint64 m_lastPhysicalUpdateAgeMs = -1;
    bool m_havePhysicalReport = false;
    double m_vjoyWritesPerSecond = 0.0;
    QVariantList m_selectedAxisCurve;
    QStringList m_events;
};

} // namespace hotas
