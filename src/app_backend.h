#pragma once

#include "event_log.h"
#include "controller_readiness.h"
#include "controller_diagnostics.h"
#include "mapping_worker.h"

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

#include <array>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace hotas {

struct PortableConfigurationBundle;

class AppBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList axes READ axes NOTIFY inputTelemetryChanged)
    // Curve Editor keeps this structural selector model separate from the
    // high-frequency axes telemetry list.
    Q_PROPERTY(QVariantList curveAxisChoices READ curveAxisChoices NOTIFY stateChanged)
    Q_PROPERTY(int selectedAxisIndex READ selectedAxisIndex NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedAxisCurve READ selectedAxisCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveEditorResponseCurve READ curveEditorResponseCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveGainSamples READ curveGainSamples NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveComparisonCurve READ curveComparisonCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curvePreviewCurve READ curvePreviewCurve NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList selectedCurvePoints READ selectedCurvePoints NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveEditorState READ curveEditorState NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveEditorTelemetry READ curveEditorTelemetry NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QVariantMap curveAnalysis READ curveAnalysis NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantMap curveComparisonState READ curveComparisonState NOTIFY selectedAxisCurveChanged)
    Q_PROPERTY(QVariantList curveStandardPresets READ curveStandardPresets CONSTANT)
    Q_PROPERTY(QVariantList curveAdvancedPresets READ curveAdvancedPresets CONSTANT)
    Q_PROPERTY(QVariantList personalCurvePresets READ personalCurvePresets NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveComparisonChoices READ curveComparisonChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curvePreviewChoices READ curvePreviewChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveCopyChoices READ curveCopyChoices NOTIFY stateChanged)
    // Button configuration is cached separately from its small live pressed
    // state. Axis updates must never force QML to rebuild up to 128 cards.
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY buttonTelemetryChanged)
    Q_PROPERTY(QVariantList povs READ povs NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QVariantList povInputs READ povInputs NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY stateChanged)
    Q_PROPERTY(QVariantList profileCategories READ profileCategories NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY stateChanged)
    Q_PROPERTY(QString activeProfileDisplayName READ activeProfileDisplayName NOTIFY stateChanged)
    Q_PROPERTY(QString activeCategoryId READ activeCategoryId NOTIFY stateChanged)
    Q_PROPERTY(QString activeCategoryName READ activeCategoryName NOTIFY stateChanged)
    Q_PROPERTY(QString effectiveProfileName READ effectiveProfileName NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QString effectiveProfileDisplayName READ effectiveProfileDisplayName NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QString profileSourceLabel READ profileSourceLabel NOTIFY inputTelemetryChanged)
    Q_PROPERTY(int activeProfileIndex READ activeProfileIndex NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)
    Q_PROPERTY(int connectedControllerCount READ connectedControllerCount NOTIFY controllersChanged)
    Q_PROPERTY(QString activeControllerRecordId READ activeControllerRecordId NOTIFY stateChanged)
    Q_PROPERTY(bool autoSwitchVerifiedController READ autoSwitchVerifiedController NOTIFY stateChanged)
    Q_PROPERTY(bool keepRunningInTray READ keepRunningInTray NOTIFY stateChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY stateChanged)
    // Presentation scheduling is deliberately separate from MappingWorker.
    // It tells the GUI/control plane when it may project runtime state, never
    // how quickly DirectInput reports are processed or written to vJoy.
    Q_PROPERTY(QString presentationState READ presentationState NOTIFY presentationStateChanged)
    Q_PROPERTY(bool physicalConnected READ physicalConnected NOTIFY stateChanged)
    Q_PROPERTY(int axisCount READ axisCount NOTIFY stateChanged)
    Q_PROPERTY(QString physicalAxisCapabilitySummary READ physicalAxisCapabilitySummary NOTIFY stateChanged)
    Q_PROPERTY(int buttonCount READ buttonCount NOTIFY stateChanged)
    Q_PROPERTY(int povCount READ povCount NOTIFY stateChanged)
    Q_PROPERTY(int povValue READ povValue NOTIFY inputTelemetryChanged)
    Q_PROPERTY(int vjoyButtonCount READ vjoyButtonCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyContinuousPovCount READ vjoyContinuousPovCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyDiscretePovCount READ vjoyDiscretePovCount NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRequiredButtonCount READ vjoyRequiredButtonCount NOTIFY stateChanged)
    Q_PROPERTY(bool vjoyCapacitySufficient READ vjoyCapacitySufficient NOTIFY stateChanged)
    Q_PROPERTY(int vjoyRecommendedButtonCount READ vjoyRecommendedButtonCount CONSTANT)
    Q_PROPERTY(int lastPhysicalButton READ lastPhysicalButton NOTIFY inputTelemetryChanged)
    Q_PROPERTY(int lastPhysicalButtonTarget READ lastPhysicalButtonTarget NOTIFY inputTelemetryChanged)
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
    Q_PROPERTY(bool controllerDiagnosticsAvailable READ controllerDiagnosticsAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool controllerSetupSuggested READ controllerSetupSuggested NOTIFY stateChanged)
    Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY stateChanged)
    Q_PROPERTY(QString calibrationStage READ calibrationStage NOTIFY stateChanged)
    Q_PROPERTY(QString calibrationStatus READ calibrationStatus NOTIFY stateChanged)
    Q_PROPERTY(bool calibrationSuccess READ calibrationSuccess NOTIFY stateChanged)
    Q_PROPERTY(QVariantList calibrationHistory READ calibrationHistory NOTIFY stateChanged)
    Q_PROPERTY(QString legacyControlMigrationWarning READ legacyControlMigrationWarning NOTIFY stateChanged)
    Q_PROPERTY(bool startMappingOnLaunch READ startMappingOnLaunch NOTIFY stateChanged)
    Q_PROPERTY(int vjoyDeviceId READ vjoyDeviceId NOTIFY stateChanged)
    Q_PROPERTY(QString activeOutputLayoutName READ activeOutputLayoutName NOTIFY stateChanged)
    Q_PROPERTY(QString activeOutputLayoutDescriptor READ activeOutputLayoutDescriptor NOTIFY stateChanged)
    Q_PROPERTY(QVariantList virtualOutputLayouts READ virtualOutputLayouts NOTIFY stateChanged)
    Q_PROPERTY(double disabledAxisValue READ disabledAxisValue NOTIFY stateChanged)
    Q_PROPERTY(bool updateChecking READ updateChecking NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool updateCheckFailed READ updateCheckFailed NOTIFY stateChanged)
    Q_PROPERTY(QString updateAvailableVersion READ updateAvailableVersion NOTIFY stateChanged)
    Q_PROPERTY(QString updateStatusText READ updateStatusText NOTIFY stateChanged)
    Q_PROPERTY(double inputReportsPerSecond READ inputReportsPerSecond NOTIFY telemetryChanged)
    Q_PROPERTY(qint64 lastPhysicalUpdateAgeMs READ lastPhysicalUpdateAgeMs NOTIFY telemetryChanged)
    Q_PROPERTY(double vjoyWritesPerSecond READ vjoyWritesPerSecond NOTIFY telemetryChanged)
    Q_PROPERTY(double overviewInputRate READ overviewInputRate NOTIFY telemetryChanged)
    Q_PROPERTY(double overviewMapperLatencyUs READ overviewMapperLatencyUs NOTIFY telemetryChanged)
    Q_PROPERTY(double overviewOutputRate READ overviewOutputRate NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong latencyCurrentUs READ latencyCurrentUs NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong latencyAverageUs READ latencyAverageUs NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong latencyPeakUs READ latencyPeakUs NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong latencyP95Us READ latencyP95Us NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong latencyP99Us READ latencyP99Us NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong profileSwitchCount READ profileSwitchCount NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong lastProfileSwapUs READ lastProfileSwapUs NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong lastCurveCompileUs READ lastCurveCompileUs NOTIFY telemetryChanged)
    Q_PROPERTY(bool automationEngineEnabled READ automationEngineEnabled NOTIFY stateChanged)
    Q_PROPERTY(int automationRuleCount READ automationRuleCount NOTIFY stateChanged)
    Q_PROPERTY(int automationActiveRuleCount READ automationActiveRuleCount NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong automationEvaluationUs READ automationEvaluationUs NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList automationRules READ automationRules NOTIFY stateChanged)
    Q_PROPERTY(QString automationValidationMessage READ automationValidationMessage NOTIFY stateChanged)
    Q_PROPERTY(QStringList buttonOutputChoices READ buttonOutputChoices NOTIFY stateChanged)
    Q_PROPERTY(QStringList virtualAxisChoices READ virtualAxisChoices NOTIFY stateChanged)
    Q_PROPERTY(QString virtualAxisStatus READ virtualAxisStatus NOTIFY stateChanged)
    Q_PROPERTY(QStringList mappingControlActionChoices READ mappingControlActionChoices CONSTANT)
    Q_PROPERTY(QVariantList profileTriggerChoices READ profileTriggerChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList nativePovTargetChoices READ nativePovTargetChoices NOTIFY stateChanged)
    Q_PROPERTY(QStringList profileTriggerBehaviorChoices READ profileTriggerBehaviorChoices CONSTANT)
    Q_PROPERTY(bool automaticGameDetection READ automaticGameDetection NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap portableImportPreview READ portableImportPreview NOTIFY stateChanged)
    Q_PROPERTY(QString portableImportStatus READ portableImportStatus NOTIFY stateChanged)
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY eventLogChanged)

public:
    explicit AppBackend(QObject *parent = nullptr);
    ~AppBackend() override;

    QVariantList axes() const;
    QVariantList curveAxisChoices() const;
    int selectedAxisIndex() const;
    QVariantList selectedAxisCurve() const;
    QVariantList curveEditorResponseCurve() const;
    QVariantList curveGainSamples() const;
    QVariantList curveComparisonCurve() const;
    QVariantList curvePreviewCurve() const;
    QVariantList selectedCurvePoints() const;
    QVariantMap curveEditorState() const;
    QVariantMap curveEditorTelemetry() const;
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
    QVariantList profileCategories() const;
    QString activeProfileId() const;
    QString activeProfileName() const;
    QString activeProfileDisplayName() const;
    QString activeCategoryId() const;
    QString activeCategoryName() const;
    QString effectiveProfileName() const;
    QString effectiveProfileDisplayName() const;
    QString profileSourceLabel() const;
    int activeProfileIndex() const;
    QString deviceName() const;
    QString deviceId() const;
    QVariantList controllers() const;
    int connectedControllerCount() const { return m_connectedControllerCount; }
    QString activeControllerRecordId() const;
    bool autoSwitchVerifiedController() const;
    bool keepRunningInTray() const;
    bool trayAvailable() const;
    QString presentationState() const;
    int presentationSnapshotIntervalMs() const;
    int controllerDiscoveryIntervalMs() const;
    bool presentationSnapshotActive() const;
    bool gameDetectionTimerActive() const;
    bool physicalConnected() const;
    int axisCount() const;
    QString physicalAxisCapabilitySummary() const;
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
    bool controllerDiagnosticsAvailable() const;
    bool controllerSetupSuggested() const { return m_controllerSetupSuggested; }
    bool calibrationActive() const;
    QString calibrationStage() const;
    QString calibrationStatus() const { return m_calibrationStatus; }
    bool calibrationSuccess() const { return m_calibrationSuccess; }
    QVariantList calibrationHistory() const;
    QString legacyControlMigrationWarning() const { return m_configuration.legacyControlMigrationWarning; }
    bool startMappingOnLaunch() const;
    int vjoyDeviceId() const;
    QString activeOutputLayoutName() const;
    QString activeOutputLayoutDescriptor() const;
    QVariantList virtualOutputLayouts() const;
    double disabledAxisValue() const;
    bool updateChecking() const { return m_updateChecking; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool updateCheckFailed() const { return m_updateCheckFailed; }
    QString updateAvailableVersion() const { return m_updateAvailableVersion; }
    QString updateStatusText() const { return m_updateStatusText; }
    double inputReportsPerSecond() const { return m_inputReportsPerSecond; }
    qint64 lastPhysicalUpdateAgeMs() const { return m_lastPhysicalUpdateAgeMs; }
    double vjoyWritesPerSecond() const { return m_vjoyWritesPerSecond; }
    double overviewInputRate() const { return m_overviewInputRate; }
    double overviewMapperLatencyUs() const { return m_overviewMapperLatencyUs; }
    double overviewOutputRate() const { return m_overviewOutputRate; }
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
    bool automaticGameDetection() const { return m_configuration.automaticGameDetection; }
    QVariantMap portableImportPreview() const;
    QString portableImportStatus() const { return m_portableImportStatus; }
    QStringList eventLog() const { return m_events.entries(); }
    // These counters are enabled only for the focused test process.
    // Production returns an empty map and keeps the presentation path clean.
    Q_INVOKABLE QVariantMap uiPerformanceCounters() const;
    Q_INVOKABLE void resetUiPerformanceCounters();

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
    Q_INVOKABLE bool createProfileInCategory(const QString &name, const QString &categoryId,
                                             const QString &startFromId = {});
    Q_INVOKABLE bool cloneProfile(const QString &profileId);
    Q_INVOKABLE bool duplicateProfileToCategory(const QString &profileId, const QString &name,
                                                const QString &categoryId);
    Q_INVOKABLE bool renameProfile(const QString &profileId, const QString &name);
    Q_INVOKABLE bool moveProfileToCategory(const QString &profileId, const QString &categoryId);
    Q_INVOKABLE bool setProfileEnabled(const QString &profileId, bool enabled);
    Q_INVOKABLE bool deleteProfile(const QString &profileId);
    Q_INVOKABLE bool activateProfile(const QString &profileId);
    Q_INVOKABLE bool createProfileCategory(const QString &name);
    Q_INVOKABLE bool renameProfileCategory(const QString &categoryId, const QString &name);
    Q_INVOKABLE bool deleteProfileCategory(const QString &categoryId);
    Q_INVOKABLE bool activateProfileCategory(const QString &categoryId);
    Q_INVOKABLE bool setProfileCategoryEnabled(const QString &categoryId, bool enabled);
    Q_INVOKABLE bool setCategoryDefaultProfile(const QString &categoryId, const QString &profileId);
    Q_INVOKABLE bool setCategoryRestoreLastProfile(const QString &categoryId, bool restoreLastProfile);
    Q_INVOKABLE bool setCategoryGameDetectionRules(const QString &categoryId, const QStringList &rules);
    Q_INVOKABLE QVariantList runningApplications() const;
    Q_INVOKABLE void refreshRunningApplications();
    Q_INVOKABLE void setAutomaticGameDetection(bool enabled);
    Q_INVOKABLE QVariantMap profileDetail(const QString &profileId) const;
    Q_INVOKABLE QVariantMap profileRelationships(const QString &profileId) const;
    Q_INVOKABLE bool exportPortableProfile(const QString &profileId, const QString &fileName);
    Q_INVOKABLE bool exportPortablePack(const QStringList &categoryIds, const QStringList &profileIds,
                                        const QString &name, const QString &description,
                                        bool includeDevices, bool includeCalibration,
                                        bool includeAutomations, bool includeProfileRelationships,
                                        bool includeGameDetection,
                                        const QString &fileName);
    Q_INVOKABLE bool loadPortableImportPreview(const QString &fileName);
    Q_INVOKABLE bool applyPortableImport(const QString &destinationCategoryId = {},
                                         bool replaceMatchingProfiles = false,
                                         const QString &categoryConflictMode = u"merge"_qs,
                                         bool applyImportedCalibration = false);
    Q_INVOKABLE bool selectPortableImportDevice(int descriptorIndex,
                                                const QString &savedControllerId);
    Q_INVOKABLE void beginCalibration();
    Q_INVOKABLE bool beginCalibrationCenterCapture();
    Q_INVOKABLE bool saveCalibration();
    Q_INVOKABLE void resetCalibration();
    Q_INVOKABLE void setStartMappingOnLaunch(bool enabled);
    Q_INVOKABLE void setVjoyDeviceId(int deviceId);
    Q_INVOKABLE bool assignProfileOutputLayout(const QString &profileId, const QString &layoutId);
    Q_INVOKABLE QString createFiveAxisOutputLayout(const QString &name, int deviceId);
    Q_INVOKABLE bool adoptVirtualOutputVisibility(const QString &layoutId,
                                                  const QString &deviceInstanceId);
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
    Q_INVOKABLE bool repairHidHideAccess();
    Q_INVOKABLE bool openHidHideConfiguration();
    Q_INVOKABLE void inspectControllerReadiness();
    Q_INVOKABLE void verifyHotasSetup();
    Q_INVOKABLE bool applyControllerReadiness();
    Q_INVOKABLE bool undoControllerReadiness();
    Q_INVOKABLE bool copyControllerDiagnostics();
    Q_INVOKABLE void acknowledgeControllerSetup();
    Q_INVOKABLE void useConnectedDevice();
    Q_INVOKABLE void refreshControllers();
    Q_INVOKABLE bool setActiveController(const QString &recordId);
    Q_INVOKABLE bool selectNewController(const QString &directInputId);
    Q_INVOKABLE bool forgetController(const QString &recordId);
    Q_INVOKABLE void setAutoSwitchVerifiedController(bool enabled);
    Q_INVOKABLE void setKeepRunningInTray(bool enabled);
    // QMenu remains the reliable native tray surface; this updates only its
    // presentation tokens when the user changes the application theme.
    Q_INVOKABLE void setTrayTheme(const QString &themeName);
    Q_INVOKABLE void forgetAllSavedControllers();
    Q_INVOKABLE void resetDeviceCalibration();
    Q_INVOKABLE bool launchUninstaller();
    Q_INVOKABLE void resetApplicationConfiguration();
    void attachMainWindow(QWindow *window);
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void restoreFromTray();
    Q_INVOKABLE void exitApplication();

signals:
    void stateChanged();
    void telemetryChanged();
    void inputTelemetryChanged();
    void buttonTelemetryChanged();
    void controllersChanged();
    void runningApplicationsChanged();
    void selectedAxisCurveChanged();
    void eventLogChanged();
    void presentationStateChanged();
    // Setup presentation must keep the discovered controller identity instead
    // of inferring a target from whichever controller is currently active.
    void controllerSetupRequested(const QStringList &targetDirectInputIds);

private slots:
    void refreshUiSnapshot();
    void appendEvent(const QString &event);
    void initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity);
    void finishUpdateCheck(QNetworkReply *reply);
    void failUpdateCheck(const QString &reason);

private:
    enum class PresentationLifecycleState {
        Visible,
        Minimized,
        TrayHidden,
    };

    void persistAndApply();
    void refreshControllerInventory();
    void evaluateGameDetection();
    void refreshNumericTelemetry();
    void applyControllerInventory(QList<DiscoveredController> latestInventory);
    void startRunningApplicationSnapshot(bool resolvePaths);
    void updatePresentationLifecycle();
    void setPresentationLifecycle(PresentationLifecycleState state);
    void releasePresentationResources();
    void restorePresentationResources();
    bool rebuildControllerUiModel();
    void rebuildButtonUiModel();
    bool refreshButtonUiModelRuntimeState();
    void rebuildCurveAxisChoices();
    const DiscoveredController *discoveredController(const QString &directInputId) const;
    SavedControllerRecord *activeControllerRecord();
    const SavedControllerRecord *activeControllerRecord() const;
    ControllerVJoyRequirements currentVjoyRequirements() const;
    void rememberCurrentController();
    void tryAutoSwitchVerifiedController();
    void refreshTrayStatus();
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
    const VirtualOutputLayout *activeOutputLayout() const;
    VirtualOutputLayout *activeOutputLayout();
    void synchronizeActiveOutputLayout();
    QString profileDisplayName(const QString &profileId) const;
    QString effectiveProfileId() const;
    PhysicalControllerCapabilities currentPhysicalCapabilities() const;
    void startQuickVerification();
    void startVerification(VerificationMode mode);
    void startExplicitNewControllerVerification(const QString &directInputId, const QString &displayName);
    void sampleCalibrationControlPlane();
    void finishCalibration();
    void appendCalibrationHistory(const std::array<Calibration, kPhysicalAxisCount> &calibration,
                                  int calibratedAxisCount);
    bool calibrationNeedsSetup(const PhysicalControllerCapabilities &physical) const;
    ControllerDiagnosticsSnapshot controllerDiagnosticsSnapshot() const;

    enum class CalibrationStageState {
        Idle,
        Range,
        Center,
        Finalizing,
    };

    struct CalibrationCaptureAxis {
        bool available = false;
        float minimum = 0.0F;
        float maximum = 0.0F;
        std::array<float, 32> centerSamples{};
        int centerSampleCount = 0;
    };

    MapperConfiguration m_configuration;
    MappingWorker m_worker;
    // Canonical GUI-side desired Mapping state. It is updated synchronously
    // for every user click and reconciled from worker-side Automation changes.
    bool m_mappingDesired = false;
    int m_presentedMappingEffectiveState = static_cast<int>(MappingEffectiveState::Off);
    ControllerReadinessService m_readiness;
    // Retained only for upgrade compatibility with the v1.9.0 preference.
    // v1.9.1 never shows a first-run setup modal.
    bool m_controllerSetupSuggested = false;
    bool m_physicalControllerWasConnected = false;
    QList<DiscoveredController> m_discoveredControllers;
    QVariantList m_controllerUiModel;
    QString m_controllerUiModelLiveDeviceId;
    int m_connectedControllerCount = 0;
    QSet<QString> m_observedControllerIds;
    bool m_controllerInventoryInitialized = false;
    QPointer<QWindow> m_mainWindow;
    PresentationLifecycleState m_presentationLifecycle = PresentationLifecycleState::Visible;
    bool m_trayHidden = false;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_trayStatusAction = nullptr;
    QAction *m_trayToggleAction = nullptr;
    QString m_pendingControllerArrivalId;
    bool m_verificationInProgress = false;
    QPointer<QThread> m_verificationThread;
    bool m_controllerSelectionInProgress = false;
    QPointer<QThread> m_controllerSelectionThread;
    // Discovery and process inspection are intentionally short-lived,
    // low-priority control-plane threads. They never touch MappingWorker.
    QPointer<QThread> m_controllerDiscoveryThread;
    QPointer<QThread> m_gameDetectionThread;
    bool m_controllerDiscoveryInProgress = false;
    bool m_gameDetectionInProgress = false;
    QTimer m_snapshotTimer;
    QTimer m_numericTelemetryTimer;
    QTimer m_controllerDiscoveryTimer;
    QTimer m_gameDetectionTimer;
    QStringList m_lastDetectedExecutables;
    QVariantList m_runningApplications;
    QHash<QString, QString> m_runningApplicationPathCache;
    QVariantList m_buttonUiModel;
    QElapsedTimer m_rateClock;
    QElapsedTimer m_physicalUpdateClock;
    QElapsedTimer m_latencyPercentileClock;
    QElapsedTimer m_overviewMetricsClock;
    QElapsedTimer m_calibrationFinalizationClock;
    QTimer m_uiEventLoopHeartbeatTimer;
    QElapsedTimer m_uiEventLoopHeartbeatClock;
    CalibrationStageState m_calibrationStage = CalibrationStageState::Idle;
    std::array<CalibrationCaptureAxis, kPhysicalAxisCount> m_calibrationCapture{};
    QString m_calibrationStatus = u"Move each control through its complete range before capturing center."_qs;
    bool m_calibrationSuccess = false;
    quint64 m_previousInputReports = 0;
    quint64 m_previousVjoyWrites = 0;
    double m_inputReportsPerSecond = 0.0;
    qint64 m_lastPhysicalUpdateAgeMs = -1;
    bool m_havePhysicalReport = false;
    double m_vjoyWritesPerSecond = 0.0;
    double m_overviewInputRate = 0.0;
    double m_overviewMapperLatencyUs = 0.0;
    double m_overviewOutputRate = 0.0;
    qulonglong m_latencyP95Us = 0;
    qulonglong m_latencyP99Us = 0;
    QVariantList m_selectedAxisCurve;
    QVariantList m_curveAxisChoices;
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
    std::unique_ptr<PortableConfigurationBundle> m_pendingPortableImport;
    QVariantMap m_portableImportPreview;
    QString m_portableImportStatus;
    QHash<int, QString> m_portableImportDeviceSelections;
    QNetworkAccessManager m_updateNetworkManager;
    QPointer<QNetworkReply> m_updateReply;
    QTimer m_updateTimeout;
    bool m_updateChecking = false;
    bool m_updateTimedOut = false;
    bool m_updateAvailable = false;
    bool m_updateCheckFailed = false;
    QString m_updateAvailableVersion;
    QString m_updateStatusText = u"Update status not checked"_qs;
    bool m_uiPerformanceInstrumentationEnabled = false;
    mutable quint64 m_controllerGetterCalls = 0;
    quint64 m_controllerUiModelRebuilds = 0;
    mutable quint64 m_buttonGetterCalls = 0;
    quint64 m_buttonUiModelRebuilds = 0;
    mutable quint64 m_profileGetterCalls = 0;
    mutable quint64 m_categoryGetterCalls = 0;
    quint64 m_stateChangedNotifications = 0;
    quint64 m_telemetryChangedNotifications = 0;
    quint64 m_inputTelemetryChangedNotifications = 0;
    quint64 m_buttonTelemetryChangedNotifications = 0;
    quint64 m_controllersChangedNotifications = 0;
    quint64 m_controllerDiscoveryBackgroundRuns = 0;
    quint64 m_gameDetectionBackgroundRuns = 0;
    qint64 m_uiEventLoopMaxDelayMs = 0;
    quint64 m_uiEventLoopDelayOver16Ms = 0;
    quint64 m_uiEventLoopDelayOver50Ms = 0;
    quint64 m_uiEventLoopDelayOver100Ms = 0;
    quint64 m_uiEventLoopDelayOver250Ms = 0;
};

} // namespace hotas
