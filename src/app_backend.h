#pragma once

#include "event_log.h"
#include "app_issue.h"
#include "app_health_service.h"
#include "activation_resolver.h"
#include "controller_readiness.h"
#include "controller_diagnostics.h"
#include "device_rig.h"
#include "adaptive_response.h"
#include "axis_transform.h"
#include "input_learning.h"
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
#include <vector>

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
    Q_PROPERTY(QVariantList curveCustomProfileChoices READ curveCustomProfileChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveComparisonChoices READ curveComparisonChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curvePreviewChoices READ curvePreviewChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantList curveCopyChoices READ curveCopyChoices NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap adaptiveResponseState READ adaptiveResponseState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList adaptiveResponsePresets READ adaptiveResponsePresets NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap adaptiveResponseTelemetry READ adaptiveResponseTelemetry NOTIFY inputTelemetryChanged)
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
    Q_PROPERTY(QVariantList deviceRigs READ deviceRigs NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString activeDeviceRigId READ activeDeviceRigId NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString activeDeviceRigName READ activeDeviceRigName NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString editingDeviceRigId READ editingDeviceRigId NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString editingDeviceRigName READ editingDeviceRigName NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString editingScopeLabel READ editingScopeLabel NOTIFY deviceRigsChanged)
    Q_PROPERTY(QVariantList editingDevices READ editingDevices NOTIFY deviceRigsChanged)
    // Flight Deck calls this persistent viewing and editing context the
    // Selected Device.  The older editing* names remain for serialized
    // configuration compatibility and existing non-Flight-Deck surfaces.
    Q_PROPERTY(QString selectedDeviceRigId READ selectedDeviceRigId NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString selectedDeviceRigName READ selectedDeviceRigName NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString selectedDeviceLabel READ selectedDeviceLabel NOTIFY deviceRigsChanged)
    Q_PROPERTY(QVariantList selectedDevices READ selectedDevices NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString deviceRigMigrationWarning READ deviceRigMigrationWarning NOTIFY deviceRigsChanged)
    Q_PROPERTY(QString deviceRigDetectionMessage READ deviceRigDetectionMessage NOTIFY deviceRigsChanged)
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
    // A beginner-facing projection of readiness.  It is intentionally a
    // structured control-plane model rather than a list of UI sentences, so
    // every themed Setup Assistant can present the same issue and action.
    Q_PROPERTY(QVariantList setupAssistantIssues READ setupAssistantIssues NOTIFY stateChanged)
    Q_PROPERTY(QVariantList setupAssistantSteps READ setupAssistantSteps NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap setupAssistantSummary READ setupAssistantSummary NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap setupAssistantLiveTest READ setupAssistantLiveTest NOTIFY inputTelemetryChanged)
    Q_PROPERTY(QString setupAssistantScopeType READ setupAssistantScopeType NOTIFY stateChanged)
    Q_PROPERTY(QString setupAssistantScopeId READ setupAssistantScopeId NOTIFY stateChanged)
    // App Health reuses the Setup Assistant issue contract for every normal
    // surface.  It updates only with control-plane stateChanged, never the
    // high-frequency telemetry signals.
    Q_PROPERTY(QVariantList appIssues READ appIssues NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap appHealthSummary READ appHealthSummary NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllerReadinessProposedChanges READ controllerReadinessProposedChanges NOTIFY stateChanged)
    Q_PROPERTY(QVariantList controllerRepairOperationResults READ controllerRepairOperationResults NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessState READ controllerReadinessState NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessStatus READ controllerReadinessStatus NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessLastChecked READ controllerReadinessLastChecked NOTIFY stateChanged)
    Q_PROPERTY(QString controllerReadinessRecommendedAction READ controllerReadinessRecommendedAction NOTIFY stateChanged)
    Q_PROPERTY(bool controllerReconnectRequired READ controllerReconnectRequired NOTIFY stateChanged)
    Q_PROPERTY(bool controllerDisconnectObserved READ controllerDisconnectObserved NOTIFY stateChanged)
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
    Q_PROPERTY(bool curveTransitionSmoothingEnabled READ curveTransitionSmoothingEnabled NOTIFY stateChanged)
    Q_PROPERTY(int curveTransitionDurationMs READ curveTransitionDurationMs NOTIFY stateChanged)
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
    Q_PROPERTY(QVariantMap activationResolverState READ activationResolverState NOTIFY stateChanged)
    Q_PROPERTY(bool manualActivationOverride READ manualActivationOverride NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap portableImportPreview READ portableImportPreview NOTIFY stateChanged)
    Q_PROPERTY(QString portableImportStatus READ portableImportStatus NOTIFY stateChanged)
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY eventLogChanged)
    Q_PROPERTY(QVariantMap inputLearning READ inputLearning NOTIFY inputLearningChanged)
    Q_PROPERTY(QVariantList quickAssignAxisTargets READ quickAssignAxisTargets NOTIFY stateChanged)
    Q_PROPERTY(QVariantList quickMapButtonTargets READ quickMapButtonTargets NOTIFY stateChanged)
    // Signal Flow is a low-frequency projection of the same persisted route
    // fields used by Axes and Buttons/POVs. It is intentionally notified only
    // at control-plane boundaries, never by DirectInput report telemetry.
    Q_PROPERTY(QVariantMap signalFlowGraph READ signalFlowGraph NOTIFY signalFlowChanged)
    Q_PROPERTY(qulonglong signalFlowRevision READ signalFlowRevision NOTIFY signalFlowChanged)
    Q_PROPERTY(bool signalFlowCanUndo READ signalFlowCanUndo NOTIFY signalFlowChanged)
    Q_PROPERTY(bool signalFlowCanRedo READ signalFlowCanRedo NOTIFY signalFlowChanged)
    Q_PROPERTY(QString signalFlowActionFeedback READ signalFlowActionFeedback NOTIFY signalFlowChanged)
    // A health/setup navigation target is identity-only state.  It lets the
    // presentation restore the exact route or processor after the shell has
    // navigated to Signal Flow without creating graph-local configuration.
    Q_PROPERTY(QString signalFlowFocusObjectId READ signalFlowFocusObjectId NOTIFY signalFlowChanged)

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
    QVariantList curveCustomProfileChoices() const;
    QVariantList curveComparisonChoices() const;
    QVariantList curvePreviewChoices() const;
    QVariantList curveCopyChoices() const;
    QVariantMap adaptiveResponseState() const;
    QVariantList adaptiveResponsePresets() const;
    QVariantMap adaptiveResponseTelemetry() const;
    Q_INVOKABLE QVariantList adaptiveResponseHistory(int seconds) const;
    // Incremental control-plane retrieval keeps QML from rebuilding the whole
    // telemetry ring for every graph frame. MappingWorker never sees this.
    Q_INVOKABLE QVariantMap adaptiveResponseHistorySince(qint64 lastSequence,
                                                          int seconds) const;
    // Test-only bridge validating that the UI samples a physical snapshot even
    // while output mapping is suspended and vJoy is unavailable.
    void injectAdaptiveResponseLiveSampleForTest(int physicalAxis, float normalized);
    // Test-only control-plane fixture for route-editor coverage. It does not
    // start vJoy or alter production device discovery.
    void setVirtualAxisAvailabilityForTest(bool available);
    // Test-only snapshot of the already-compiled mapping table. It is used to
    // compare editor/configuration state with the runtime route at a safe
    // control-plane boundary, never from a DirectInput report.
    QVariantList runtimeAxisRoutesForTest() const;
    // Test-only structured Setup Assistant input. Production derives the
    // same facts from durable device records and low-frequency readiness
    // snapshots; tests use this seam to cover each user-visible diagnosis
    // without requiring real controllers or driver installation state.
    void setSetupAssistantFactsForTest(const QVariantMap &facts);
#ifdef HOTAS_STARTUP_TESTING
    // Bounded startup-test presentation fixture. It neither enumerates
    // hardware nor reaches the DirectInput-to-vJoy report path.
    void setButtonUiFixtureForTest(int physicalButtonCount, int vjoyButtonCapacity,
                                   int physicalPovCount, int continuousPovCapacity = 0);
    // Deterministic transaction fixture and one-shot stage faults. They are
    // compiled only into the isolated startup suites and cannot alter a
    // production mapper, driver, or visibility transaction.
    bool configureActivationTransactionFixtureForTest();
    void setActivationFaultInjectionsForTest(const QStringList &stages);
#endif
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
    QVariantList deviceRigs() const;
    QString activeDeviceRigId() const;
    QString activeDeviceRigName() const;
    QString editingDeviceRigId() const;
    QString editingDeviceRigName() const;
    QString editingScopeLabel() const;
    QVariantList editingDevices() const;
    QString selectedDeviceRigId() const;
    QString selectedDeviceRigName() const;
    QString selectedDeviceLabel() const;
    QVariantList selectedDevices() const;
    QString deviceRigMigrationWarning() const;
    QString deviceRigDetectionMessage() const;
    Q_INVOKABLE QVariantMap physicalDeviceDetail(const QString &recordId) const;
    Q_INVOKABLE QVariantMap virtualOutputDetail(const QString &layoutId) const;
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
    QVariantList setupAssistantIssues() const;
    QVariantList setupAssistantSteps() const;
    QVariantMap setupAssistantSummary() const;
    QVariantMap setupAssistantLiveTest() const;
    QString setupAssistantScopeType() const;
    QString setupAssistantScopeId() const;
    QVariantList appIssues() const;
    QVariantMap appHealthSummary() const;
    QVariantList controllerReadinessProposedChanges() const;
    QVariantList controllerRepairOperationResults() const;
    QString controllerReadinessState() const;
    QString controllerReadinessStatus() const;
    QString controllerReadinessLastChecked() const;
    QString controllerReadinessRecommendedAction() const;
    bool controllerReconnectRequired() const;
    bool controllerDisconnectObserved() const;
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
    bool curveTransitionSmoothingEnabled() const;
    int curveTransitionDurationMs() const;
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
    QVariantMap inputLearning() const;
    QVariantList quickAssignAxisTargets() const;
    QVariantList quickMapButtonTargets() const;
    QVariantMap signalFlowGraph() const;
    qulonglong signalFlowRevision() const { return m_configurationGeneration; }
    bool signalFlowCanUndo() const;
    bool signalFlowCanRedo() const;
    QString signalFlowActionFeedback() const { return m_signalFlowActionFeedback; }
    QString signalFlowFocusObjectId() const { return m_signalFlowFocusObjectId; }
    bool automaticGameDetection() const { return m_configuration.automaticGameDetection; }
    QVariantMap activationResolverState() const;
    bool manualActivationOverride() const { return m_manualActivationOverride; }
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
    // Focused axis editors use this deliberate conflict transaction instead
    // of retaining the historic implicit row-order collision. `replace`
    // removes competing analog sources; the supported mixer values create a
    // durable, visible Signal Flow mixer before the configuration is applied.
    Q_INVOKABLE QVariantMap resolveAxisMappingConflict(int physicalAxis, const QString &target,
                                                        const QString &decision,
                                                        qulonglong expectedRevision);
    Q_INVOKABLE void setAxisCustomName(int physicalAxis, const QString &name);
    Q_INVOKABLE void setAxisRangeMode(int physicalAxis, const QString &mode);
    Q_INVOKABLE void setVirtualAxisAlias(const QString &target, const QString &alias);
    Q_INVOKABLE bool startAxisLearning(const QString &target);
    Q_INVOKABLE bool startButtonLearning(int virtualButton);
    Q_INVOKABLE bool startPovLearning(int virtualButton);
    // Source-first Signal Flow learning identifies one deliberate physical
    // endpoint but deliberately does not create a route. QML then selects the
    // canonical source port and presents only compatible virtual destinations.
    Q_INVOKABLE bool startSignalFlowInputLearning();
    Q_INVOKABLE void retryInputLearning();
    Q_INVOKABLE void cancelInputLearning();
    Q_INVOKABLE bool resolveInputLearningConflict(const QString &resolution);
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
    Q_INVOKABLE bool setAdaptiveResponsePreset(const QString &scope, int physicalAxis,
                                               const QString &presetId);
    Q_INVOKABLE bool setAdaptiveResponsePresetAtContext(const QString &scope,
                                                        const QString &targetId, int physicalAxis,
                                                        const QString &presetId);
    Q_INVOKABLE bool setAdaptiveResponseProperty(const QString &scope, int physicalAxis,
                                                 const QString &property, const QVariant &value,
                                                 bool inherit = false);
    Q_INVOKABLE bool setAdaptiveResponsePropertyAtContext(const QString &scope,
                                                          const QString &targetId, int physicalAxis,
                                                          const QString &property, const QVariant &value,
                                                          bool inherit = false);
    Q_INVOKABLE bool resetAdaptiveResponseAxis(const QString &scope, int physicalAxis);
    Q_INVOKABLE bool resetAdaptiveResponseAxisAtContext(const QString &scope,
                                                        const QString &targetId, int physicalAxis);
    Q_INVOKABLE bool saveAdaptiveResponsePreset(const QString &name, const QString &description);
    Q_INVOKABLE bool duplicateAdaptiveResponsePreset(const QString &presetId, const QString &name);
    Q_INVOKABLE bool renameAdaptiveResponsePreset(const QString &presetId, const QString &name);
    Q_INVOKABLE QVariantList adaptiveResponsePresetDependencies(const QString &presetId) const;
    Q_INVOKABLE bool deleteAdaptiveResponsePreset(const QString &presetId);
    Q_INVOKABLE QVariantList adaptiveResponsePreview(const QString &scenario) const;
    Q_INVOKABLE QVariantMap adaptiveResponseTestLab(const QString &scenario) const;
    // Context inspection is deliberately control-plane work. It lets an
    // inactive Category, Profile, or custom Response Preset display its own
    // resolved values without changing the mapper's live selected profile.
    Q_INVOKABLE QVariantMap adaptiveResponseContextState(const QString &scope,
                                                         const QString &targetId,
                                                         int physicalAxis) const;
    Q_INVOKABLE QVariantList adaptiveResponsePreviewAtContext(const QString &scenario,
                                                               const QString &scope,
                                                               const QString &targetId,
                                                               int physicalAxis) const;
    Q_INVOKABLE QVariantMap adaptiveResponseTestLabAtContext(const QString &scenario,
                                                              const QString &scope,
                                                              const QString &targetId,
                                                              int physicalAxis) const;
    // The simulator is a UI-thread-only control-plane instrument. It has its
    // own production processor instance and bounded sample/recording rings;
    // neither path reads or writes the mapper's live adaptive state.
    Q_INVOKABLE void adaptiveResponseSimulatorStepAtContext(double physical,
                                                             const QString &scope,
                                                             const QString &targetId,
                                                             int physicalAxis,
                                                             int sourceRateHz);
    Q_INVOKABLE QVariantList adaptiveResponseSimulatorHistory() const;
    Q_INVOKABLE QVariantMap adaptiveResponseSimulatorHistorySince(qint64 lastSequence) const;
    Q_INVOKABLE void adaptiveResponseSimulatorClear();
    Q_INVOKABLE void adaptiveResponseSimulatorStartRecording();
    Q_INVOKABLE void adaptiveResponseSimulatorStopRecording();
    Q_INVOKABLE bool adaptiveResponseSimulatorRecordingActive() const;
    Q_INVOKABLE QVariantList adaptiveResponseSimulatorRecording() const;
    Q_INVOKABLE bool setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride = false);
    Q_INVOKABLE bool resolveButtonRouteChange(int physicalButton, int virtualButton,
                                              const QString &resolution);
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
    Q_INVOKABLE bool setProfileAutomaticSelectionMode(const QString &profileId, const QString &mode);
    Q_INVOKABLE bool reorderCategoryAutomaticProfiles(const QString &categoryId,
                                                      const QStringList &profileIds);
    Q_INVOKABLE bool assignProfileDeviceRig(const QString &profileId, const QString &rigId);
    Q_INVOKABLE QVariantMap activationPreview(const QString &categoryId = {}) const;
    Q_INVOKABLE QVariantMap explainActivation(const QString &categoryId = {}) const;
    Q_INVOKABLE bool activateRecommendedConfiguration(const QString &categoryId = {});
    Q_INVOKABLE bool resumeAutomaticActivation();
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
                                         bool applyImportedCalibration = false,
                                         const QString &adaptivePresetConflictMode = u"copy"_qs);
    Q_INVOKABLE bool selectPortableImportDevice(int descriptorIndex,
                                                const QString &savedControllerId);
    Q_INVOKABLE void beginCalibration();
    // Device Details can start the existing calibration workflow directly.
    // Selecting another connected saved controller remains an explicit
    // control-plane switch; no report-path selection is introduced here.
    Q_INVOKABLE bool beginCalibrationForDevice(const QString &recordId);
    Q_INVOKABLE bool beginCalibrationCenterCapture();
    Q_INVOKABLE bool saveCalibration();
    Q_INVOKABLE void resetCalibration();
    Q_INVOKABLE void setStartMappingOnLaunch(bool enabled);
    Q_INVOKABLE void setVjoyDeviceId(int deviceId);
    Q_INVOKABLE bool assignProfileOutputLayout(const QString &profileId, const QString &layoutId);
    Q_INVOKABLE QString createFiveAxisOutputLayout(const QString &name, int deviceId);
    Q_INVOKABLE int suggestedVirtualOutputDeviceId() const;
    Q_INVOKABLE QString createVirtualOutputLayout(const QString &name, int deviceId,
                                                  const QString &preset = QStringLiteral("bf6-4-axis"));
    // Devices actions return an explicit, presentation-safe result.  The old
    // QString creators remain for compatibility with existing callers, but a
    // QML command must never have to infer why an empty ID was returned.
    Q_INVOKABLE QVariantMap createVirtualOutputLayoutResult(const QString &name, int deviceId,
                                                            const QString &mode,
                                                            const QString &sourceId = {},
                                                            const QVariantList &customAxes = {},
                                                            int buttons = 0,
                                                            int continuousPovs = 0,
                                                            int discretePovs = 0);
    Q_INVOKABLE bool renameVirtualOutputLayout(const QString &layoutId, const QString &name);
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
    Q_INVOKABLE void setCurveTransitionSmoothingEnabled(bool enabled);
    Q_INVOKABLE void setCurveTransitionDurationMs(int durationMs);
    Q_INVOKABLE bool setProfileCurveTransitionSmoothingOverride(const QString &profileId,
                                                                 bool enabled);
    Q_INVOKABLE bool setProfileCurveTransitionSmoothingEnabled(const QString &profileId,
                                                                bool enabled);
    Q_INVOKABLE bool setProfileCurveTransitionDurationMs(const QString &profileId,
                                                          int durationMs);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE bool handoffToLauncher();
    Q_INVOKABLE bool openVjoyConfiguration();
    Q_INVOKABLE void refreshHidHideStatus();
    Q_INVOKABLE bool repairHidHideAccess();
    Q_INVOKABLE bool openHidHideConfiguration();
    Q_INVOKABLE void inspectControllerReadiness();
    Q_INVOKABLE void verifyHotasSetup();
    Q_INVOKABLE QVariantMap startSetupAssistantCheck();
    Q_INVOKABLE QVariantMap startSetupAssistantCheckForScope(const QString &scopeType,
                                                             const QString &scopeId = {});
    // Completes the selected saved controller's exact-identity verification.
    // This differs from a generic diagnostic rerun: success persists the
    // verified device record and advances the assistant to the next cause.
    Q_INVOKABLE QVariantMap completeSetupAssistantDevice(const QString &recordId = {});
    Q_INVOKABLE QVariantMap applySetupAssistantIssueAction(const QString &issueId);
    Q_INVOKABLE QVariantMap applySetupAssistantFix();
    Q_INVOKABLE QVariantMap startSetupAssistantLiveTest();
    Q_INVOKABLE QVariantMap skipCalibrationForSetup(const QString &recordId = {});
    Q_INVOKABLE bool applyControllerReadiness();
    Q_INVOKABLE bool undoControllerReadiness();
    Q_INVOKABLE bool copyControllerDiagnostics();
    Q_INVOKABLE void acknowledgeControllerSetup();
    Q_INVOKABLE void useConnectedDevice();
    Q_INVOKABLE void refreshControllers();
    Q_INVOKABLE QString createDeviceRig(const QString &name, const QStringList &controllerRecordIds,
                                        const QString &outputLayoutId = {});
    Q_INVOKABLE QVariantMap createDeviceRigResult(const QString &name,
                                                  const QStringList &controllerRecordIds,
                                                  const QString &outputLayoutId = {});
    Q_INVOKABLE QString createDeviceRigFromDetected(const QString &name,
                                                    const QStringList &directInputIds,
                                                    const QString &outputLayoutId = {});
    Q_INVOKABLE bool addDetectedDeviceToRig(const QString &rigId, const QString &directInputId,
                                            bool required = true);
    Q_INVOKABLE bool activateDeviceRig(const QString &rigId);
    Q_INVOKABLE bool deactivateDeviceRig(const QString &rigId);
    Q_INVOKABLE bool setDefaultDeviceRig(const QString &rigId);
    Q_INVOKABLE bool clearDefaultDeviceRig(const QString &rigId);
    Q_INVOKABLE bool setDeviceRigAutoActivate(const QString &rigId, bool enabled);
    Q_INVOKABLE bool setDeviceRigActivationPriority(const QString &rigId, int priority);
    Q_INVOKABLE bool setDeviceRigMemberRequired(const QString &rigId, const QString &controllerRecordId,
                                                bool required);
    Q_INVOKABLE bool setDeviceRigMemberEnabled(const QString &rigId, const QString &controllerRecordId,
                                               bool enabled);
    Q_INVOKABLE bool setDeviceRigMemberOutput(const QString &rigId, const QString &controllerRecordId,
                                              const QString &outputLayoutId);
    Q_INVOKABLE bool addDeviceRigOutput(const QString &rigId, const QString &outputLayoutId);
    Q_INVOKABLE bool removeDeviceRigOutput(const QString &rigId, const QString &outputLayoutId);
    Q_INVOKABLE bool setDeviceRigOutputEnabled(const QString &rigId, const QString &outputLayoutId,
                                               bool enabled);
    Q_INVOKABLE bool setDeviceRigInputVisibility(const QString &rigId,
                                                 const QStringList &controllerRecordIds,
                                                 bool hidden);
    Q_INVOKABLE bool setDeviceRigOutputVisibility(const QString &rigId,
                                                  const QStringList &outputLayoutIds,
                                                  bool visible);
    Q_INVOKABLE bool addDeviceRigMember(const QString &rigId, const QString &controllerRecordId,
                                        bool required = true);
    Q_INVOKABLE bool removeDeviceRigMember(const QString &rigId, const QString &controllerRecordId);
    Q_INVOKABLE bool renameDeviceRig(const QString &rigId, const QString &name);
    Q_INVOKABLE bool setDeviceRigEnabled(const QString &rigId, bool enabled);
    Q_INVOKABLE bool setDeviceRigFallback(const QString &rigId, const QString &fallbackRigId);
    Q_INVOKABLE bool setDeviceRigDisconnectBehavior(const QString &rigId, int behavior);
    Q_INVOKABLE void verifyDeviceRig(const QString &rigId = {});
    Q_INVOKABLE bool setEditingDeviceContext(const QString &rigId,
                                             const QStringList &controllerRecordIds = {});
    Q_INVOKABLE bool setSelectedDeviceContext(const QString &rigId,
                                              const QStringList &controllerRecordIds = {});
    // App Health actions enter a persistent editing context before navigating
    // to an owning page. This is intentionally a control-plane operation;
    // input reports never call it.
    Q_INVOKABLE bool focusIssueTarget(const QString &objectType, const QString &objectId);
    Q_INVOKABLE void recordCrashPresentationState(int page, const QString &theme);
    Q_INVOKABLE QVariantMap editingAxisBatchPreview(int physicalAxis, const QString &property,
                                                    const QVariant &value) const;
    Q_INVOKABLE bool applyEditingAxisBatch(int physicalAxis, const QString &property,
                                           const QVariant &value, const QString &mode);
    Q_INVOKABLE bool deleteDeviceRig(const QString &rigId);
    // Structured graph commands carry the caller's canonical revision. A
    // gesture rendered against stale state is rejected before mutation rather
    // than blindly replaying an obsolete preview over focused-editor work.
    Q_INVOKABLE QVariantMap signalFlowConnect(const QString &sourceKind, int sourceIndex,
                                              int sourceSubIndex, const QString &destination,
                                              bool replaceConflicts, qulonglong expectedRevision);
    // Analog fan-in is a separate intentional command.  Keeping it distinct
    // from ordinary Connect makes an accidental merge impossible at the UI
    // boundary and leaves the chosen runtime mixer mode in durable topology.
    Q_INVOKABLE QVariantMap signalFlowConnectWithMixer(const QString &sourceKind, int sourceIndex,
                                                       int sourceSubIndex, const QString &destination,
                                                       const QString &mixerMode,
                                                       qulonglong expectedRevision);
    // The graph owns endpoint identity.  This direct-port command accepts
    // projection-scoped opaque endpoint IDs rather than a visible editing
    // scope, and resolves the physical controller owner internally.
    Q_INVOKABLE QVariantMap connectSignalFlowEndpoints(const QString &sourceEndpointId,
                                                        const QString &destinationEndpointId,
                                                        const QString &collisionDecision,
                                                        qulonglong expectedRevision);
    // Presentation gestures use this non-mutating companion before commit.
    // It reports the same endpoint/context/collision preconditions that the
    // canonical command will revalidate atomically, without creating a
    // graph-local compatibility model in QML.
    Q_INVOKABLE QVariantMap signalFlowPreviewConnection(const QString &sourceEndpointId,
                                                         const QString &destinationEndpointId,
                                                         const QString &collisionDecision,
                                                         qulonglong expectedRevision) const;
    Q_INVOKABLE QVariantMap signalFlowDisconnect(const QString &routeId,
                                                 qulonglong expectedRevision);
    // These are deliberately control-plane helpers. They resolve an existing
    // canonical route into a beginner-readable explanation, a bounded latest
    // snapshot for Live/Signal Focus, and atomic source-owned processor edits.
    // None is called by MappingWorker's DirectInput report path.
    Q_INVOKABLE QVariantMap signalFlowExplainRoute(const QString &routeId) const;
    Q_INVOKABLE QVariantMap signalFlowLiveTelemetry() const;
    Q_INVOKABLE QVariantMap signalFlowToggleProcessor(const QString &routeId,
                                                       const QString &processorKind, bool enabled,
                                                       qulonglong expectedRevision);
    // Phase 2 targets the canonical edge shown under the pointer.  The
    // segment id is durable for the current topology and is revalidated at
    // commit; QML never manufactures a processor-on-wire relationship.
    Q_INVOKABLE QVariantMap signalFlowInsertProcessor(const QString &segmentId,
                                                       const QString &processorKind,
                                                       qulonglong expectedRevision);
    // Returns only the source-owned processors whose fixed runtime stage is
    // represented by this canonical segment.  This keeps the palette honest:
    // Signal Flow cannot offer a graph-only reorder that the mapping worker
    // would not execute.
    Q_INVOKABLE QVariantList signalFlowAvailableProcessorsForSegment(const QString &segmentId,
                                                                      qulonglong expectedRevision) const;
    Q_INVOKABLE QVariantMap signalFlowRemoveOrBypassProcessor(const QString &processorId,
                                                               qulonglong expectedRevision);
    // A shared processor owns one focused setting but can serve several axis
    // channels.  Removing one channel is explicit and leaves the remaining
    // shared processor intact (including its durable identity).
    Q_INVOKABLE QVariantMap signalFlowRemoveSharedProcessorChannel(const QString &processorId,
                                                                    const QString &routeId,
                                                                    qulonglong expectedRevision);
    // A shared processor is a canonical, source-owned relation. The first
    // route is its owner; later selected axis routes receive the same durable
    // focused setting at the next configuration boundary.
    Q_INVOKABLE QVariantMap signalFlowShareProcessor(const QStringList &routeIds,
                                                      const QString &processorKind,
                                                      qulonglong expectedRevision);
    Q_INVOKABLE QVariantMap signalFlowSplitSharedProcessor(const QString &routeId,
                                                           const QString &processorKind,
                                                           qulonglong expectedRevision);
    Q_INVOKABLE QVariantMap signalFlowDefaultPreview(const QString &mode) const;
    Q_INVOKABLE QVariantMap signalFlowApplyDefaults(const QString &mode,
                                                    qulonglong expectedRevision);
    Q_INVOKABLE QVariantMap signalFlowUndo(qulonglong expectedRevision);
    Q_INVOKABLE QVariantMap signalFlowRedo(qulonglong expectedRevision);
    Q_INVOKABLE bool signalFlowSaveWorkspace(const QVariantMap &workspace);
    Q_INVOKABLE bool signalFlowSaveNodeLayout(const QString &objectId, double x, double y,
                                              bool pinned = false);
    Q_INVOKABLE QVariantMap signalFlowSetPortGroupCollapsed(const QString &cardId,
                                                             const QString &group, bool collapsed);
    Q_INVOKABLE QVariantMap signalFlowAutoLayout();
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
    void deviceRigsChanged();
    void runningApplicationsChanged();
    void selectedAxisCurveChanged();
    void eventLogChanged();
    void presentationStateChanged();
    void inputLearningChanged();
    void signalFlowChanged();
    // Setup presentation must keep the discovered controller identity instead
    // of inferring a target from whichever controller is currently active.
    void controllerSetupRequested(const QStringList &targetDirectInputIds);

private slots:
    void refreshUiSnapshot();
    void appendEvent(const QString &event);
    QString crashPresentationContext() const;
    void initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity);
    void finishUpdateCheck(QNetworkReply *reply);
    void failUpdateCheck(const QString &reason);

private:
    enum class PresentationLifecycleState {
        Visible,
        Minimized,
        TrayHidden,
    };

    enum class InputLearningKind { None, Axis, Button, Pov, SignalFlowSource };
    enum class InputLearningPhase { Idle, Arming, Waiting, Ambiguous, Conflict, Assigned };

    struct InputLearningState {
        InputLearningKind kind = InputLearningKind::None;
        InputLearningPhase phase = InputLearningPhase::Idle;
        QString target;
        int virtualButton = 0;
        int sourceAxis = -1;
        int sourceButton = 0;
        int sourcePovHat = 0;
        PovDirection sourcePovDirection = PovDirection::Centered;
        QString sourceLabel;
        QString message;
        qint64 armingStableSinceMs = 0;
        std::array<float, kPhysicalAxisCount> axisBaseline{};
        std::array<bool, kPhysicalAxisCount> axisAvailable{};
        std::array<PhysicalAxisActivity, kPhysicalAxisCount> axisActivity{};
        std::array<bool, kMaximumPhysicalButtons> buttonBaseline{};
        std::array<int, kMaximumPhysicalPovs> povBaseline{[] {
            std::array<int, kMaximumPhysicalPovs> values{};
            values.fill(-1);
            return values;
        }()};
    };

    struct SignalFlowCommand {
        MapperConfiguration before;
        MapperConfiguration after;
        quint64 undoRevision = 0;
        quint64 redoRevision = 0;
        QString description;
    };

    // UI-thread-only bounded history. The mapper publishes atomics; this
    // ring is sampled from the existing presentation timer and is never read
    // or written from the DirectInput-to-vJoy report path.
    struct AdaptiveResponseHistorySample {
        qint64 sequence = 0;
        qint64 elapsedMs = 0;
        int axis = 0;
        float physical = 0.0F;
        float estimated = 0.0F;
        float predicted = 0.0F;
        float baselineMappedOutput = 0.0F;
        float predictedMappedOutput = 0.0F;
        float adaptiveOutput = 0.0F;
        float mappedLead = 0.0F;
        float appliedLead = 0.0F;
        float localCurveGain = 0.0F;
        float virtualOutput = 0.0F;
        float velocity = 0.0F;
        float acceleration = 0.0F;
        float activeHorizonSeconds = 0.0F;
        float maximumHorizonSeconds = 0.0F;
        float requestedLead = 0.0F;
        float cappedLead = 0.0F;
        float endpointTaper = 1.0F;
        float lead = 0.0F;
        float confidence = 0.0F;
        float motionIntensity = 0.0F;
        float velocityAuthority = 0.0F;
        float deliberateMotionEvidence = 0.0F;
        float normalMotionAuthority = 0.0F;
        float rapidMotionAuthority = 0.0F;
        float rapidMotionBlend = 0.0F;
        float accelerationIntent = 0.0F;
        float onsetAuthority = 0.0F;
        float sustainedEvidence = 0.0F;
        float sustainedAuthority = 0.0F;
        float motionUrgency = 0.0F;
        float horizonExtensionEligibility = 0.0F;
        float normalMaximumHorizonSeconds = 0.0F;
        float allowedMaximumHorizonSeconds = 0.0F;
        float turningPointConfidence = 0.0F;
        float estimatedTimeToTurnSeconds = 0.0F;
        float estimatedRemainingTravel = 0.0F;
        float turningPointHorizonLimitSeconds = 0.0F;
        float turningPointLeadLimit = 0.0F;
        float reacquisitionAuthority = 0.0F;
        int motionState = 0;
    };

    struct AdaptiveResponseSimulatorSample {
        qint64 sequence = 0;
        qint64 elapsedMs = 0;
        float physical = 0.0F;
        float estimated = 0.0F;
        float predicted = 0.0F;
        float baselineMappedOutput = 0.0F;
        float predictedMappedOutput = 0.0F;
        float adaptiveOutput = 0.0F;
        float mappedLead = 0.0F;
        float appliedLead = 0.0F;
        float localCurveGain = 0.0F;
        bool deadzoneAuthorityBlocked = false;
        bool leadLimited = false;
        bool highLocalCurveGain = false;
        float virtualOutput = 0.0F;
        float velocity = 0.0F;
        float acceleration = 0.0F;
        float activeHorizonSeconds = 0.0F;
        float maximumHorizonSeconds = 0.0F;
        float maximumLead = 0.0F;
        float requestedLead = 0.0F;
        float cappedLead = 0.0F;
        float endpointTaper = 1.0F;
        float lead = 0.0F;
        float confidence = 0.0F;
        float motionIntensity = 0.0F;
        float velocityAuthority = 0.0F;
        float deliberateMotionEvidence = 0.0F;
        float normalMotionAuthority = 0.0F;
        float rapidMotionAuthority = 0.0F;
        float rapidMotionBlend = 0.0F;
        float accelerationIntent = 0.0F;
        float onsetAuthority = 0.0F;
        float sustainedEvidence = 0.0F;
        float sustainedAuthority = 0.0F;
        float motionUrgency = 0.0F;
        float horizonExtensionEligibility = 0.0F;
        float normalMaximumHorizonSeconds = 0.0F;
        float allowedMaximumHorizonSeconds = 0.0F;
        float turningPointConfidence = 0.0F;
        float estimatedTimeToTurnSeconds = 0.0F;
        float estimatedRemainingTravel = 0.0F;
        float turningPointHorizonLimitSeconds = 0.0F;
        float turningPointLeadLimit = 0.0F;
        float reacquisitionAuthority = 0.0F;
        int motionState = 0;
    };

    void persistAndApply();
    // A focused edit to the owner of a shared Signal Flow conditioner remains
    // one configuration edit for every linked channel. A member edit is left
    // independent so reconciliation can surface it as an explicit split.
    void persistAxisProcessorEdit(const QString &kind, int physicalAxis);
    void persistSelectedAxisProcessorEdit(const QString &kind);
    void propagateProfileAdaptiveResponseIfShared(const QString &scope, const QString &targetId,
                                                  int physicalAxis);
    QVariantMap signalFlowActionResult(bool success, const QString &title, const QString &message,
                                       const QString &objectId = {}) const;
    QVariantMap signalFlowConnectInternal(const QString &sourceKind, int sourceIndex,
                                          int sourceSubIndex, const QString &destination,
                                          bool replaceConflicts, const QString &mixerMode,
                                          qulonglong expectedRevision,
                                          const QString &sourceControllerRecordId = {});
    bool commitSignalFlowCommand(MapperConfiguration before, const QString &description);
    QString signalFlowWorkspaceKey() const;
    bool saveSignalFlowPresentation();
    void sampleAdaptiveResponseHistory();
    void appendAdaptiveResponseSimulatorSample(const AdaptiveResponseSimulatorSample &sample);
    void advanceAdaptiveResponseSimulator(float manualInput, const QString &scope,
                                          const QString &targetId, int physicalAxis,
                                          int sourceRateHz, qint64 nowMs);
    RuntimeAdaptiveResponseConfig adaptiveResponseConfigurationAtContext(
        const QString &scope, const QString &targetId, int physicalAxis,
        AdaptiveResponseAxisOverride *contextOverride = nullptr,
        QString *source = nullptr, RuntimeAxisMapping *staticMapping = nullptr) const;
    void refreshControllerInventory();
    void evaluateGameDetection();
    void refreshNumericTelemetry();
    void applyControllerInventory(QList<DiscoveredController> latestInventory);
    void reconcileDeviceRigInventory();
    void startRunningApplicationSnapshot(bool resolvePaths);
    ActivationContext activationContext(const QString &categoryId = {},
                                        ActivationIntent intent = ActivationIntent::Automatic,
                                        const QString &requestedProfileId = {},
                                        const QString &requestedRigId = {}) const;
    ActivationDecision activationDecision(const QString &categoryId = {},
                                          ActivationIntent intent = ActivationIntent::Automatic,
                                          const QString &requestedProfileId = {},
                                          const QString &requestedRigId = {}) const;
    QVariantMap activationDecisionVariant(const ActivationDecision &decision) const;
    void scheduleActivationResolution(const QString &reason);
    void resolveActivationNow();
    bool applyActivationDecision(const ActivationDecision &decision, ActivationIntent intent);
    void clearManualActivationOverride(const QString &reason = {});
    void sampleForegroundGameContext();
    void updateRequiredDeviceDisconnectGrace();
    bool commitActivationConfiguration(const MapperConfiguration &candidate);
    bool consumeActivationFaultForTest(const QString &stage);
    void updatePresentationLifecycle();
    void setPresentationLifecycle(PresentationLifecycleState state);
    void releasePresentationResources();
    void restorePresentationResources();
    bool rebuildControllerUiModel();
    void rebuildButtonUiModel();
    bool refreshButtonUiModelRuntimeState();
    void captureInputLearningBaseline();
    void enterInputLearningArming();
    void processInputLearning();
    bool applyLearnedInput();
    QString learnedAxisLabel(int physicalAxis) const;
    QString learnedButtonLabel(int physicalButton) const;
    void rebuildCurveAxisChoices();
    const DiscoveredController *discoveredController(const QString &directInputId) const;
    SavedControllerRecord *activeControllerRecord();
    const SavedControllerRecord *activeControllerRecord() const;
    const SavedControllerRecord *savedControllerRecord(const QString &recordId) const;
    DeviceRig *activeDeviceRig();
    const DeviceRig *activeDeviceRig() const;
    const DeviceRig *setupAssistantDeviceRig(const QString &scopeType,
                                             const QString &scopeId) const;
    QVariantList setupAssistantIssuesForScope(const QString &scopeType,
                                              const QString &scopeId) const;
    QVariantMap applyPhysicalDeviceGameVisibility(const QStringList &controllerRecordIds, bool hidden);
    ControllerVJoyRequirements currentVjoyRequirements() const;
    void rememberCurrentController(const QString &expectedRecordId = {});
    void tryAutoSwitchVerifiedController();
    void refreshTrayStatus();
    void rebuildSelectedAxisCurve();
    CurveDefinition comparisonCurveDefinition() const;
    bool fallBackToAvailableAxis();
    AxisMapping *selectedAxisMapping();
    const AxisMapping *selectedAxisMapping() const;
    // The persistent top-bar context selects a physical source for editors.
    // Multi-source route edits must be an explicit transaction, never an
    // accidental mutation of the legacy profile payload.
    bool editingScopeHasSinglePhysicalSource() const;
    DeviceProfileMapping *editingDeviceMappingForWrite();
    const DeviceProfileMapping *editingDeviceMapping() const;
    AdaptiveResponseLayer *adaptiveResponseLayer(const QString &scope, const QString &targetId = {});
    const AdaptiveResponseLayer *adaptiveResponseLayer(const QString &scope,
                                                       const QString &targetId = {}) const;
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
    void observeControllerReconnect();
    void reconcileControllerReconnect(const PhysicalControllerCapabilities &physical);
    void sampleCalibrationControlPlane();
    void finishCalibration();
    void appendCalibrationHistory(const std::array<Calibration, kPhysicalAxisCount> &calibration,
                                  int calibratedAxisCount);
    bool calibrationNeedsSetup(const PhysicalControllerCapabilities &physical) const;
    void refreshVirtualOutputReadiness(const QString &layoutId);
    const ControllerReadinessPlan *virtualOutputReadinessPlan(const QString &layoutId) const;
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
    std::vector<SignalFlowCommand> m_signalFlowUndo;
    std::vector<SignalFlowCommand> m_signalFlowRedo;
    QString m_signalFlowActionFeedback;
    QString m_signalFlowFocusObjectId;
    bool m_signalFlowCommandInFlight = false;
    MappingWorker m_worker;
    // Canonical GUI-side desired Mapping state. It is updated synchronously
    // for every user click and reconciled from worker-side Automation changes.
    bool m_mappingDesired = false;
    // Only the deterministic Live Controller UI test sets this. It verifies
    // a suspended presentation state without asking the worker to acquire
    // vJoy or modify a physical device.
    bool m_liveInputTestSuspended = false;
    // Setup Assistant sessions are UI/control-plane observations.  They
    // snapshot the worker's existing fixed counters only when the user starts
    // a test; the report loop performs no session bookkeeping or allocation.
    bool m_setupAssistantLiveTestActive = false;
    quint64 m_setupAssistantInputBaseline = 0;
    quint64 m_setupAssistantOutputBaseline = 0;
    std::array<quint64, kMaximumDeviceRigMembers> m_setupAssistantMemberBaselines{};
    std::array<quint64, kMaximumDeviceRigOutputs> m_setupAssistantOutputBaselines{};
    QVariantMap m_setupAssistantTestFacts;
    QString m_setupAssistantScopeType = u"application"_qs;
    QString m_setupAssistantScopeId;
    // A saved controller can be present in discovery yet still fail the
    // explicit DirectInput acquisition required for verification. Retain that
    // distinct result so the assistant does not send the user through an
    // indistinguishable Set Up loop.
    QHash<QString, QString> m_setupAssistantDeviceAcquisitionFailures;
    QString m_pendingSetupVerificationRecordId;
    // Output inspection is explicit and scoped.  A rig/device check must not
    // accidentally change another saved output's readiness presentation.
    QHash<QString, ControllerReadinessPlan> m_virtualOutputReadinessPlans;
    QString m_pendingCalibrationRecordId;
    int m_presentedMappingEffectiveState = static_cast<int>(MappingEffectiveState::Off);
    ControllerReadinessService m_readiness;
    // Retained only for upgrade compatibility with the v1.9.0 preference.
    // v1.9.1 never shows a first-run setup modal.
    bool m_controllerSetupSuggested = false;
    bool m_physicalControllerWasConnected = false;
    QList<DiscoveredController> m_discoveredControllers;
    QList<DeviceRigStatus> m_deviceRigStatuses;
    QString m_deviceRigDetectionMessage;
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
    QTimer m_adaptiveResponseHistoryTimer;
    QTimer m_controllerDiscoveryTimer;
    QTimer m_gameDetectionTimer;
    QTimer m_foregroundGameTimer;
    QTimer m_requiredDisconnectGraceTimer;
    QTimer m_activationResolveTimer;
    QSet<QString> m_pendingActivationReasons;
    QStringList m_lastDetectedExecutables;
    QString m_foregroundExecutableCandidate;
    quint64 m_foregroundExecutableCandidateProcessId = 0;
    QString m_stableForegroundExecutable;
    quint64 m_stableForegroundExecutableProcessId = 0;
    QElapsedTimer m_foregroundExecutableClock;
    QHash<QString, qint64> m_requiredDeviceDisconnectStartedMs;
    QString m_requiredDisconnectGraceRigId;
    QString m_requiredDisconnectGraceProfileId;
    QElapsedTimer m_activationControlPlaneClock;
    quint64 m_configurationGeneration = 1;
    quint64 m_inventoryGeneration = 0;
    quint64 m_gameContextGeneration = 0;
    bool m_manualActivationOverride = false;
    QString m_manualOverrideProfileId;
    QString m_manualOverrideCategoryId;
    bool m_activationDegraded = false;
#ifdef HOTAS_STARTUP_TESTING
    QSet<QString> m_activationFaultInjections;
    bool m_activationTransactionTestBypassDriverConfiguration = false;
#endif
    bool m_initialInventoryResolved = false;
    bool m_initialGameContextResolved = false;
    QVariantList m_runningApplications;
    QHash<QString, QString> m_runningApplicationPathCache;
    QVariantList m_buttonUiModel;
    InputLearningState m_inputLearning;
    QElapsedTimer m_rateClock;
    QElapsedTimer m_physicalUpdateClock;
    QElapsedTimer m_latencyPercentileClock;
    QElapsedTimer m_overviewMetricsClock;
    QElapsedTimer m_adaptiveResponseHistoryClock;
    std::array<AdaptiveResponseHistorySample, 3000> m_adaptiveResponseHistory{};
    int m_adaptiveResponseHistoryNext = 0;
    int m_adaptiveResponseHistoryCount = 0;
    qint64 m_adaptiveResponseHistorySequence = 0;
    QElapsedTimer m_adaptiveResponseSimulatorClock;
    AdaptiveResponseProcessor m_adaptiveResponseSimulator;
    AxisHysteresisState m_adaptiveResponseSimulatorHysteresis;
    AxisCenterResolverState m_adaptiveResponseSimulatorCenterResolver;
    // These deliberately live on the heap: AppBackend is constructed on the
    // stack by its QML startup test, while the fixed bounds still ensure the
    // control-plane simulator cannot grow without limit.
    std::vector<AdaptiveResponseSimulatorSample> m_adaptiveResponseSimulatorHistory;
    std::vector<AdaptiveResponseSimulatorSample> m_adaptiveResponseSimulatorRecording;
    int m_adaptiveResponseSimulatorHistoryNext = 0;
    int m_adaptiveResponseSimulatorHistoryCount = 0;
    int m_adaptiveResponseSimulatorRecordingCount = 0;
    int m_adaptiveResponseSimulatorRecordingNext = 0;
    double m_adaptiveResponseSimulatorLastSourceMs = -1.0;
    qint64 m_adaptiveResponseSimulatorLastTickMs = -1;
    qint64 m_adaptiveResponseSimulatorLastManualInputMs = -1;
    qint64 m_adaptiveResponseSimulatorSequence = 0;
    int m_adaptiveResponseSimulatorSourceRate = 250;
    float m_adaptiveResponseSimulatorLastManualInput = 0.0F;
    float m_adaptiveResponseSimulatorHeldInput = 0.0F;
    float m_adaptiveResponseSimulatorResolvedInput = 0.0F;
    bool m_adaptiveResponseSimulatorHasManualInput = false;
    bool m_adaptiveResponseSimulatorRecordingActive = false;
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
    int m_crashPresentationPage = 8;
    QString m_crashPresentationTheme = u"Standard"_qs;
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
