#pragma once

#include "event_log.h"
#include "controller_readiness.h"
#include "mapping_worker.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace hotas {

class AppBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList axes READ axes NOTIFY stateChanged)
    Q_PROPERTY(int selectedAxisIndex READ selectedAxisIndex NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedAxisCurve READ selectedAxisCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveEditorResponseCurve READ curveEditorResponseCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveGainSamples READ curveGainSamples NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveComparisonCurve READ curveComparisonCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curvePreviewCurve READ curvePreviewCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList selectedCurvePoints READ selectedCurvePoints NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveEditorState READ curveEditorState NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveAnalysis READ curveAnalysis NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveComparisonState READ curveComparisonState NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveStandardPresets READ curveStandardPresets CONSTANT)
    Q_PROPERTY(QVariantList curveAdvancedPresets READ curveAdvancedPresets CONSTANT)
    Q_PROPERTY(QVariantList personalCurvePresets READ personalCurvePresets NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveComparisonChoices READ curveComparisonChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curvePreviewChoices READ curvePreviewChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveCopyChoices READ curveCopyChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY stateChanged)
    Q_PROPERTY(QVariantList povs READ povs NOTIFY stateChanged)
    Q_PROPERTY(QVariantList povInputs READ povInputs NOTIFY stateChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY stateChanged)
    Q_PROPERTY(QString effectiveProfileName READ effectiveProfileName NOTIFY stateChanged)
    Q_PROPERTY(QString profileSourceLabel READ profileSourceLabel NOTIFY stateChanged)
    Q_PROPERTY(int activeProfileIndex READ activeProfileIndex NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY stateChanged)
    Q_PROPERTY(bool physicalConnected READ physicalConnected NOTIFY stateChanged)
    Q_PROPERTY(int axisCount READ axisCount NOTIFY stateChanged)
    Q_PROPERTY(int buttonCount READ buttonCount NOTIFY stateChanged)
    Q_PROPERTY(int povCount READ povCount NOTIFY stateChanged)
    Q_PROPERTY(int povValue READ povValue NOTIFY stateChanged)
    Q_PROPERTY(int vjoyButtonCount READ vjoyButtonCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyContinuousPovCount READ vjoyContinuousPovCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyDiscretePovCount READ vjoyDiscretePovCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRequiredButtonCount READ vjoyRequiredButtonCount NOTIFY stateChanged)
    Q_PROPERTY(bool vjoyCapacitySufficient READ vjoyCapacitySufficient NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRecommendedButtonCount READ vjoyRecommendedButtonCount CONSTANT)
    Q_PROPERTY(int lastPhysicalButton READ lastPhysicalButton NOTIFY stateChanged)
    Q_PROPERTY(int lastPhysicalButtonTarget READ lastPhysicalButtonTarget NOTIFY stateChanged)
    Q_PROPERTY(bool mappingActive READ mappingActive NOTIFY stateChanged)
    Q_PROPERTY(bool mappingRequested READ mappingRequested NOTIFY stateChanged)
    Q_PROPERTY(QString mappingStatus READ mappingStatus NOTIFY stateChanged)
    Q_PROPERTY(bool vjoyReady READ vjoyReady NOTIFY stateChanged)
    Q_PROPERTY(QString vjoyStatus READ vjoyStatus NOTIFY stateChanged)
    Q_PROPERTY(QString vjoyStatusSeverity READ vjoyStatusSeverity NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideAvailable READ hidhideAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideCloakStateKnown READ hidhideCloakStateKnown NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideCloaked READ hidhideCloaked NOTIFY stateChanged)
    Q_PROPERTY(bool hidhideMapperAllowed READ hidhideMapperAllowed NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllerReadinessChecks READ controllerReadinessChecks NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllerReadinessProposedChanges READ controllerReadinessProposedChanges NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllerRepairOperationResults READ controllerRepairOperationResults NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessState READ controllerReadinessState NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessStatus READ controllerReadinessStatus NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessLastChecked READ controllerReadinessLastChecked NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessRecommendedAction READ controllerReadinessRecommendedAction NOTIFY stateChanged)
    Q_PROPERTY(bool controllerSetupCanApply READ controllerSetupCanApply NOTIFY stateChanged)
    Q_PROPERTY(bool controllerSetupInProgress READ controllerSetupInProgress NOTIFY stateChanged)
    Q_PROPERTY(bool controllerSetupCanUndo READ controllerSetupCanUndo NOTIFY stateChanged)
    Q_PROPERTY(bool controllerSetupSuggested READ controllerSetupSuggested NOTIFY stateChanged)
    Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY stateChanged)
    Q_PROPERTY(bool startMappingOnLaunch READ startMappingOnLaunch NOTIFY stateChanged)
    Q_PROPERTY(int vjoyDeviceId READ vjoyDeviceId NOTIFY stateChanged)
    Q_PROPERTY(double disabledAxisValue READ disabledAxisValue NOTIFY stateChanged)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool updateCheckFailed READ updateCheckFailed NOTIFY stateChanged)
    Q_PROPERTY(QString updateAvailableVersion READ updateAvailableVersion NOTIFY stateChanged)
    Q_PROPERTY(QString updateStatusText READ updateStatusText NOTIFY stateChanged)
    Q_PROPERTY(double inputReportsPerSecond READ inputReportsPerSecond NOTIFY stateChanged)
    Q_PROPERTY(qint64 lastPhysicalUpdateAgeMs READ lastPhysicalUpdateAgeMs NOTIFY stateChanged)
    Q_PROPERTY(double vjoyWritesPerSecond READ vjoyWritesPerSecond NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyCurrentUs READ latencyCurrentUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyAverageUs READ latencyAverageUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyPeakUs READ latencyPeakUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyP95Us READ latencyP95Us NOTIFY stateChanged)
    Q_PROPERTY(qulonglong latencyP99Us READ latencyP99Us NOTIFY stateChanged)
    Q_PROPERTY(qulonglong profileSwitchCount READ profileSwitchCount NOTIFY stateChanged)
    Q_PROPERTY(qulonglong lastProfileSwapUs READ lastProfileSwapUs NOTIFY stateChanged)
    Q_PROPERTY(qulonglong lastCurveCompileUs READ lastCurveCompileUs NOTIFY stateChanged)
    Q_PROPERTY(bool automationEngineEnabled READ automationEngineEnabled NOTIFY stateChanged)
    Q_PROPERTY(int automationRuleCount READ automationRuleCount NOTIFY stateChanged)
    Q_PROPERTY(int automationActiveRuleCount READ automationActiveRuleCount NOTIFY stateChanged)
    Q_PROPERTY(qulonglong automationEvaluationUs READ automationEvaluationUs NOTIFY stateChanged)
    Q_PROPERTY(QVariantList automationRules READ automationRules NOTIFY stateChanged)
    Q_PROPERTY(QString automationValidationMessage READ automationValidationMessage NOTIFY stateChanged)
    Q_PROPERTY(QStringList buttonOutputChoices READ buttonOutputChoices NOTIFY stateChanged)
    Q_PROPERTY(QStringList virtualAxisChoices READ virtualAxisChoices NOTIFY stateChanged)
    Q_PROPERTY(QString virtualAxisStatus READ virtualAxisStatus NOTIFY stateChanged)
    Q_PROPERTY(QStringList mappingControlActionChoices READ mappingControlActionChoices CONSTANT)
    Q_PROPERTY(QVariantList profileTriggerChoices READ profileTriggerChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList nativePovTargetChoices READ nativePovTargetChoices NOTIFY stateChanged)
    Q_PROPERTY(QStringList profileTriggerBehaviorChoices READ profileTriggerBehaviorChoices CONSTANT)
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY eventLogChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend() override;

    QVariantList axes() const;
    int selectedAxisIndex() const;
    QVariantList selectedAxisCurve() const;
    QVariantList curveEditorResponseCurve() const;
    QVariantList curveGainSamples() const;
    QVariantList curveComparisonCurve() const;
    QVariantList curvePreviewCurve() const;
    QVariantList selectedCurvePoints() const;
    QVariantMap curveEditorState() const;
    QVariantMap curveAnalysis() const;
    QVariantMap curveComparisonState() const;
    QVariantList curveStandardPresets() const;
    QVariantList curveAdvancedPresets() const;
    QVariantList personalCurvePresets() const;
    QVariantList curveComparisonChoices() const;
    QVariantList curvePreviewChoices() const;
    QVariantList curveCopyChoices() const;
    QVariantList buttons() const;
    QVariantList povs() const;
    QVariantList povInputs() const;
    QVariantList profiles() const;
    QString activeProfileId() const;
    QString activeProfileName() const;
    QString effectiveProfileName() const;
    QString profileSourceLabel() const;
    int activeProfileIndex() const;
    QString deviceName() const;
    QString deviceId() const;
    bool physicalConnected() const;
    int axisCount() const;
    int buttonCount() const;
    int povCount() const;
    int povValue() const;
    int vjoyButtonCount() const;
    int vjoyContinuousPovCount() const;
    int vjoyDiscretePovCount() const;
    int vjoyRequiredButtonCount() const;
    bool vjoyCapacitySufficient() const;
    int vjoyRecommendedButtonCount() const { return 32; }
    int lastPhysicalButton() const;
    int lastPhysicalButtonTarget() const;
    bool mappingActive() const;
    bool mappingRequested() const;
    QString mappingStatus() const;
    bool vjoyReady() const;
    QString vjoyStatus() const;
    QString vjoyStatusSeverity() const;
    bool hidhideAvailable() const;
    bool hidhideCloakStateKnown() const;
    bool hidhideCloaked() const;
    bool hidhideMapperAllowed() const;
    QVariantList controllerReadinessChecks() const;
    QVariantList controllerReadinessProposedChanges() const;
    QVariantList controllerRepairOperationResults() const;
    QString controllerReadinessState() const;
    QString controllerReadinessStatus() const;
    QString controllerReadinessLastChecked() const;
    QString controllerReadinessRecommendedAction() const;
    bool controllerSetupCanApply() const;
    bool controllerSetupInProgress() const;
    bool controllerSetupCanUndo() const;
    bool controllerSetupSuggested() const { return m_controllerSetupSuggested; }
    bool calibrationActive() const;
    bool startMappingOnLaunch() const;
    int vjoyDeviceId() const;
    double disabledAxisValue() const;
    bool updateChecking() const { return m_updateChecking; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool updateCheckFailed() const { return m_updateCheckFailed; }
    QString updateAvailableVersion() const { return m_updateAvailableVersion; }
    QString updateStatusText() const { return m_updateStatusText; }
    double inputReportsPerSecond() const { return m_inputReportsPerSecond; }
    qint64 lastPhysicalUpdateAgeMs() const { return m_lastPhysicalUpdateAgeMs; }
    double vjoyWritesPerSecond() const { return m_vjoyWritesPerSecond; }
    qulonglong latencyCurrentUs() const;
    qulonglong latencyAverageUs() const;
    qulonglong latencyPeakUs() const;
    qulonglong latencyP95Us() const { return m_latencyP95Us; }
    qulonglong latencyP99Us() const { return m_latencyP99Us; }
    qulonglong profileSwitchCount() const;
    qulonglong lastProfileSwapUs() const;
    qulonglong lastCurveCompileUs() const;
    bool automationEngineEnabled() const;
    int automationRuleCount() const;
    int automationActiveRuleCount() const;
    qulonglong automationEvaluationUs() const;
    QVariantList automationRules() const;
    QString automationValidationMessage() const;
    QStringList buttonOutputChoices() const;
    QStringList virtualAxisChoices() const;
    QString virtualAxisStatus() const;
    QStringList mappingControlActionChoices() const;
    QVariantList profileTriggerChoices() const;
    QVariantList nativePovTargetChoices() const;
    QStringList profileTriggerBehaviorChoices() const;
    QStringList eventLog() const { return m_events.entries(); }

    Q_INVOKABLE void toggleMapping();
    Q_INVOKABLE void setMappingActive(bool active);
    Q_INVOKABLE bool setMapping(int physicalAxis, const QString &target, bool explicitOverride = false);
    Q_INVOKABLE void setAxisCustomName(int physicalAxis, const QString &name);
    Q_INVOKABLE void setAxisRangeMode(int physicalAxis, const QString &mode);
    Q_INVOKABLE void setVirtualAxisAlias(const QString &target, const QString &alias);
    Q_INVOKABLE void setSelectedAxis(int physicalAxis);
    Q_INVOKABLE void setAxisInverted(int physicalAxis, bool inverted);
    Q_INVOKABLE void setAxisDeadzone(int physicalAxis, double deadzone);
    Q_INVOKABLE void setAxisHysteresis(int physicalAxis, double hysteresis);
    Q_INVOKABLE bool setAxisOutputLimits(int physicalAxis, double minimum, double maximum);
    Q_INVOKABLE void setCurveFamily(const QString &family);
    Q_INVOKABLE void setCurveStrength(double strength);
    Q_INVOKABLE void setCurveStandardPreset(const QString &presetId);
    Q_INVOKABLE void applyAdvancedCurvePreset(const QString &presetId);
    Q_INVOKABLE bool applyPersonalCurvePreset(const QString &presetId);
    Q_INVOKABLE void setCurvePointEditing(bool enabled);
    Q_INVOKABLE void setCurveInterpolation(const QString &interpolation);
    Q_INVOKABLE void setCurvePointDensity(int density);
    Q_INVOKABLE void setCurveSymmetry(bool enabled);
    Q_INVOKABLE bool setCurvePoint(int index, double input, double output);
    Q_INVOKABLE bool setCurvePointLocked(int index, bool locked);
    Q_INVOKABLE int addCurvePoint(double input, double output);
    Q_INVOKABLE bool removeCurvePoint(int index);
    Q_INVOKABLE void resetCurveLinear();
    Q_INVOKABLE bool resetCurveToSource();
    Q_INVOKABLE bool copyCurveFrom(const QString &profileId, int axisIndex);
    Q_INVOKABLE bool copyCurveFromSelection(const QString &selectionId);
    Q_INVOKABLE bool saveCurrentCurveAsPersonalPreset(const QString &name);
    Q_INVOKABLE bool renamePersonalCurvePreset(const QString &presetId, const QString &name);
    Q_INVOKABLE bool deletePersonalCurvePreset(const QString &presetId);
    Q_INVOKABLE bool updatePersonalCurvePreset(const QString &presetId);
    Q_INVOKABLE void setCurveComparison(const QString &comparisonId);
    Q_INVOKABLE QVariantMap inspectCurve(double domainInput) const;
    Q_INVOKABLE QString curveEditorSnapshot() const;
    Q_INVOKABLE bool restoreCurveEditorSnapshot(const QString &snapshot);
    Q_INVOKABLE void previewCurvePreset(const QString &presetId);
    Q_INVOKABLE void clearCurvePreview();
    Q_INVOKABLE bool applyCurvePreview();
    Q_INVOKABLE bool setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride = false);
    Q_INVOKABLE void setButtonCustomName(int physicalButton, const QString &name);
    Q_INVOKABLE bool setMappingControl(int physicalButton, const QString &action);
    Q_INVOKABLE bool setPovMapping(int povHat, int direction, int virtualButton,
                                   bool explicitOverride = false);
    Q_INVOKABLE bool setProfileTrigger(int physicalButton, const QString &targetProfileId,
                                       const QString &behavior);
    Q_INVOKABLE bool setPovProfileTrigger(int povHat, int direction,
                                          const QString &targetProfileId, const QString &behavior);
    Q_INVOKABLE bool setNativePovOutput(int povHat, bool enabled, const QString &targetKey);
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
    Q_INVOKABLE void setAutomationEngineEnabled(bool enabled);
    // These return the newly-created stable ID so the presentation can open a
    // full-page draft editor without ever deriving identity from list order.
    Q_INVOKABLE QString createAutomation();
    Q_INVOKABLE QString duplicateAutomation(const QString &id);
    Q_INVOKABLE bool deleteAutomation(const QString &id);
    Q_INVOKABLE bool setAutomationEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool saveAutomation(const QVariantMap &automation);
    Q_INVOKABLE void setDisabledAxisValue(double percent);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE bool handoffToLauncher();
    Q_INVOKABLE bool openVjoyConfiguration();
    Q_INVOKABLE void refreshHidHideStatus();
    Q_INVOKABLE bool openHidHideConfiguration();
    Q_INVOKABLE void inspectControllerReadiness();
    Q_INVOKABLE void verifyHotasSetup();
    Q_INVOKABLE bool applyControllerReadiness();
    Q_INVOKABLE bool undoControllerReadiness();
    Q_INVOKABLE void acknowledgeControllerSetup();
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
    void finishUpdateCheck(QNetworkReply *reply);
    void failUpdateCheck(const QString &reason);

private:
    void persistAndApply();
    void rebuildSelectedAxisCurve();
    CurveDefinition comparisonCurveDefinition() const;
    bool fallBackToAvailableAxis();
    AxisMapping *selectedAxisMapping();
    const AxisMapping *selectedAxisMapping() const;
    bool validAxis(int physicalAxis) const;
    bool validPhysicalButton(int physicalButton) const;
    bool axisIsOneSided(int physicalAxis) const;
    const ControllerProfile &currentProfile() const;
    ControllerProfile &currentProfile();
    QString effectiveProfileId() const;
    PhysicalControllerCapabilities currentPhysicalCapabilities() const;
    void startQuickVerification();
    void startVerification(VerificationMode mode);

    MapperConfiguration m_configuration;
    MappingWorker m_worker;
    ControllerReadinessService m_readiness;
    // Retained only for upgrade compatibility with the v1.9.0 preference.
    // v1.9.1 never shows a first-run setup modal.
    bool m_controllerSetupSuggested = false;
    bool m_verificationInProgress = false;
    QPointer<QThread> m_verificationThread;
    QTimer m_snapshotTimer;
    QElapsedTimer m_rateClock;
    QElapsedTimer m_physicalUpdateClock;
    QElapsedTimer m_latencyPercentileClock;
    quint64 m_previousInputReports = 0;
    quint64 m_previousVjoyWrites = 0;
    double m_inputReportsPerSecond = 0.0;
    qint64 m_lastPhysicalUpdateAgeMs = -1;
    bool m_havePhysicalReport = false;
    double m_vjoyWritesPerSecond = 0.0;
    qulonglong m_latencyP95Us = 0;
    qulonglong m_latencyP99Us = 0;
    QVariantList m_selectedAxisCurve;
    QVariantList m_curveEditorResponseCurve;
    QVariantList m_curveGainSamples;
    QVariantList m_curveComparisonCurve;
    QVariantList m_curvePreviewCurve;
    QVariantMap m_curveAnalysis;
    QString m_curveComparisonId;
    QString m_curveComparisonLabel;
    QString m_curvePreviewId;
    QString m_curvePreviewLabel;
    CurveDefinition m_curvePreviewDefinition;
    EventLog m_events;
    QString m_automationValidationMessage;
    QNetworkAccessManager m_updateNetworkManager;
    QPointer<QNetworkReply> m_updateReply;
    QTimer m_updateTimeout;
    bool m_updateChecking = false;
    bool m_updateTimedOut = false;
    bool m_updateAvailable = false;
    bool m_updateCheckFailed = false;
    QString m_updateAvailableVersion;
    QString m_updateStatusText = u"Update status not checked"_qs;
};

} // namespace hotas
