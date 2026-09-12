#include <QFile>
#include <QtTest>

namespace {

QString sourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(HOTAS_SOURCE_DIR "/") + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

class UiReleaseContractTests final : public QObject {
    Q_OBJECT

private slots:
    void headerIsTheOnlyPrimaryMappingControl();
    void trayAndThemeRefreshRemainOnTheUiSide();
    void newDeviceSetupExplicitlyAcquiresThenVerifies();
    void controllerSetupRequiresFreshPostRestoreIdentityProof();
    void controllerSetupRetainsItsExplicitTargetAndSuccessfulRepairPersistsIt();
    void sharedSettingsKeepOfflineControllersAndControlsVisuallyExplicit();
    void controllerPresentationIsCachedAndTelemetryIsIsolated();
    void presentationLifecycleSleepsOnlyTheGuiControlPlane();
    void curveEditorUsesSelectedAxisTelemetryAndExplicitPaintContracts();
    void profileOverflowMenuUsesThemedControlContract();
    void unifiedVerifierUsesSharedThemedButtons();
    void deviceDialogsUseSharedThemedHeaders();
    void reliabilityCleanupUsesRequiredCapacityAndStableAutomationRows();
    void virtualOutputLayoutsAreExactAndTelemetryStaysTruthful();
    void inputLearningAndLiveNameDraftsStayOnControlPlane();
    void buttonLearningIsDestinationFirstAndCardsShowLiveSignalFlow();
    void axisConflictsRequireExplicitSignalFlowDecisions();
    void installerUpgradeAcceptanceTracksSchema28();
    void flightDeckTypographyContract();
    void flightDeckInformationArchitectureContract();
    void mapperPostBuildDeploymentIncludesQmlModules();
    void curveTransitionSmoothingUsesThemedSettingsAndProfileControls();
    void profileLibraryPortabilityIsSharedAndThemed();
    void allThemeSelectorsUseSkinnedDarkPopups();
    void adaptiveResponseControlsRetainZeroAndExposeSignalMetrics();
    void adaptiveResponseVisualizerKeepsPredictorAndSimulatorOnTheControlPlane();
    void deviceRigRuntimeRetainsDisconnectAndControlPlaneSafetyContracts();
    void setupAssistantAndOutputCreationExposeObservableContracts();
};

void UiReleaseContractTests::headerIsTheOnlyPrimaryMappingControl()
{
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    QVERIFY(standard.contains(QStringLiteral("id: globalMappingControl")));
    QVERIFY(legacy.contains(QStringLiteral("id: globalMappingControl")));
    QVERIFY(standard.contains(QStringLiteral("onClicked: backend.toggleMapping()")));
    QVERIFY(legacy.contains(QStringLiteral("onClicked: backend.toggleMapping()")));
    QCOMPARE(standard.count(QStringLiteral("label: backend.mappingRequested ? \"STOP MAPPING\" : \"START MAPPING\"")), 0);
    QCOMPARE(legacy.count(QStringLiteral("label: backend.mappingRequested ? \"STOP MAPPING\" : \"START MAPPING\"")), 0);
}

void UiReleaseContractTests::trayAndThemeRefreshRemainOnTheUiSide()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString main = sourceFile(QStringLiteral("qml/Main.qml"));
    QVERIFY(backend.contains(QStringLiteral("void AppBackend::setTrayTheme")));
    QVERIFY(backend.contains(QStringLiteral("QMenu::item:selected")));
    QVERIFY(backend.contains(QStringLiteral("normalized == u\"flight deck light\"_qs")));
    QVERIFY(backend.contains(QStringLiteral("normalized == u\"flight deck\"_qs || normalized == u\"flight deck dark\"_qs")));
    QVERIFY(main.contains(QStringLiteral("onCurrentThemeChanged")));
    QVERIFY(main.contains(QStringLiteral("backend.setTrayTheme")));
    QVERIFY(main.contains(QStringLiteral("function refreshTrayTheme()")));
    QVERIFY(main.contains(QStringLiteral("onFlightDeckAppearanceChanged()")));
    QVERIFY(main.contains(QStringLiteral("? \"Flight Deck \" + themeManager.flightDeckAppearance")));
}

void UiReleaseContractTests::newDeviceSetupExplicitlyAcquiresThenVerifies()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    QVERIFY(backend.contains(QStringLiteral("startExplicitNewControllerVerification(target->directInputId")));
    QVERIFY(backend.contains(QStringLiteral("m_worker.selectPhysicalController(directInputId)")));
    QVERIFY(backend.contains(QStringLiteral("verifyHotasSetup();")));
    QVERIFY(backend.contains(QStringLiteral("New controller detected:")));
}

void UiReleaseContractTests::controllerSetupRequiresFreshPostRestoreIdentityProof()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const qsizetype verifierStart = backend.indexOf(QStringLiteral("void AppBackend::startVerification("));
    QVERIFY(verifierStart >= 0);
    const qsizetype verifierEnd = backend.indexOf(QStringLiteral("bool AppBackend::applyControllerReadinessForConfiguration("), verifierStart);
    QVERIFY(verifierEnd > verifierStart);
    const QString verifier = backend.mid(verifierStart, verifierEnd - verifierStart);

    QVERIFY(verifier.contains(QStringLiteral("finalIdentityProof = m_worker.selectPhysicalController(physical.directInputId)")));
    QVERIFY(verifier.contains(QStringLiteral("finalIdentityProof\n                && m_readiness.reconcilePendingRecoveryAfterVerifiedReadback")));
    QVERIFY(verifier.contains(QStringLiteral("could not obtain a fresh DirectInput report from the exact selected controller after restoring the mapping session")));
}

void UiReleaseContractTests::controllerSetupRetainsItsExplicitTargetAndSuccessfulRepairPersistsIt()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    QVERIFY(header.contains(QStringLiteral("controllerSetupRequested(const QStringList &targetDirectInputIds)")));
    QVERIFY(backend.contains(QStringLiteral("emit controllerSetupRequested(newlyDiscoveredUnverifiedIds)")));
    QVERIFY(backend.contains(QStringLiteral("emit controllerSetupRequested({arrivalId})")));
    QVERIFY(backend.contains(QStringLiteral("ControllerReadinessService::isKnownPhysicalController(")));
    QVERIFY(backend.contains(QStringLiteral("commit the controller now rather than requiring Verify Again")));
    QVERIFY(backend.contains(QStringLiteral("verifiedRequirements.buttons = verifiedPlan.requirements.buttons")));
}

void UiReleaseContractTests::sharedSettingsKeepOfflineControllersAndControlsVisuallyExplicit()
{
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString devices = sourceFile(QStringLiteral("qml/DevicesPage.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    // Device ownership is now singular: Settings offers an intentional
    // handoff, while Devices owns selected/offline physical-controller state.
    QVERIFY(settings.contains(QStringLiteral("Device setup belongs in Devices")));
    QVERIFY(settings.contains(QStringLiteral("OPEN DEVICES")));
    QVERIFY(!settings.contains(QStringLiteral("NO CONTROLLERS CONNECTED")));
    QVERIFY(devices.contains(QStringLiteral("EDIT THIS")));
    QVERIFY(devices.contains(QStringLiteral("OFFLINE")));
    QVERIFY(settings.contains(QStringLiteral("up.indicator")));
    QVERIFY(settings.contains(QStringLiteral("down.indicator")));
    QVERIFY(backend.contains(QStringLiteral("Selected · Offline · Verified")));
}

void UiReleaseContractTests::controllerPresentationIsCachedAndTelemetryIsIsolated()
{
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString devices = sourceFile(QStringLiteral("qml/DevicesPage.qml"));
    const QString flightDeckAxes = sourceFile(QStringLiteral("qml/FlightDeckAxes.qml"));
    const QString signalFlow = sourceFile(QStringLiteral("qml/FlightDeckSignalFlow.qml"));
    const QString qmlLifecycle = sourceFile(QStringLiteral("tests/app_qml_startup_tests.cpp"));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(int connectedControllerCount READ connectedControllerCount NOTIFY controllersChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(double inputReportsPerSecond READ inputReportsPerSecond NOTIFY telemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList axes READ axes NOTIFY inputTelemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList axisConfiguration READ axisConfiguration NOTIFY stateChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList axisTelemetry READ axisTelemetry NOTIFY inputTelemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("effectiveProfileName READ effectiveProfileName NOTIFY profilePresentationChanged")));
    QVERIFY(header.contains(QStringLiteral("effectiveProfileDisplayName READ effectiveProfileDisplayName NOTIFY profilePresentationChanged")));
    QVERIFY(header.contains(QStringLiteral("profileSourceLabel READ profileSourceLabel NOTIFY profilePresentationChanged")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList buttons READ buttons NOTIFY buttonTelemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("void controllersChanged();")));
    QVERIFY(header.contains(QStringLiteral("void telemetryChanged();")));
    QVERIFY(header.contains(QStringLiteral("void buttonTelemetryChanged();")));
    QVERIFY(header.contains(QStringLiteral("void profilePresentationChanged();")));
    QVERIFY(backend.contains(QStringLiteral("QVariantList AppBackend::axisConfiguration() const")));
    QVERIFY(backend.contains(QStringLiteral("QVariantList AppBackend::axisTelemetry() const")));
    QVERIFY(backend.contains(QStringLiteral("void AppBackend::publishProfilePresentationIfChanged()")));
    QVERIFY(backend.contains(QStringLiteral("return m_controllerUiModel;")));
    QVERIFY(backend.contains(QStringLiteral("sameControllerInventory")));
    QVERIFY(backend.contains(QStringLiteral("if (inventoryChanged && rebuildControllerUiModel()) emit stateChanged();")));
    const qsizetype snapshotStart = backend.indexOf(QStringLiteral("void AppBackend::refreshUiSnapshot()"));
    QVERIFY(snapshotStart >= 0);
    const qsizetype snapshotEnd = backend.indexOf(QStringLiteral("void AppBackend::appendEvent"), snapshotStart);
    QVERIFY(snapshotEnd > snapshotStart);
    const QString snapshot = backend.mid(snapshotStart, snapshotEnd - snapshotStart);
    QCOMPARE(snapshot.count(QStringLiteral("emit stateChanged();")), 1);
    QVERIFY(snapshot.contains(QStringLiteral("const bool selectedAxisChanged = fallBackToAvailableAxis();")));
    QVERIFY(snapshot.contains(QStringLiteral("if (selectedAxisChanged || connectionChanged || mappingIntentChanged || mappingEffectiveChanged) emit stateChanged();")));
    QVERIFY(snapshot.contains(QStringLiteral("emit telemetryChanged();")));
    QVERIFY(snapshot.contains(QStringLiteral("emit inputTelemetryChanged();")));
    QVERIFY(backend.contains(QStringLiteral("void AppBackend::rebuildButtonUiModel()")));
    QVERIFY(backend.contains(QStringLiteral("bool AppBackend::refreshButtonUiModelRuntimeState()")));
    QVERIFY(backend.contains(QStringLiteral("QThread::create([this]")));
    QVERIFY(backend.contains(QStringLiteral("ControllerDiscovery::enumerate()")));
    QVERIFY(backend.contains(QStringLiteral("startRunningApplicationSnapshot(false)")));
    QVERIFY(settings.contains(QStringLiteral("readonly property var controllerModel: backend.controllers")));
    QVERIFY(devices.contains(QStringLiteral("readonly property var controllers: backendObject ? backendObject.controllers : []")));
    QVERIFY(!settings.contains(QStringLiteral("backend.controllers[")));
    QVERIFY(flightDeckAxes.contains(QStringLiteral("backend.axisConfiguration")));
    QVERIFY(flightDeckAxes.contains(QStringLiteral("backend.axisTelemetry")));
    QVERIFY(!flightDeckAxes.contains(QStringLiteral("backend.axes")));
    QVERIFY(signalFlow.contains(QStringLiteral("function rebuildGraphIndexes()")));
    QVERIFY(signalFlow.contains(QStringLiteral("function rebuildLiveTelemetryIndex()")));
    QVERIFY(signalFlow.contains(QStringLiteral("routeLiveById[String(route && route.id || \"\")]")));
    QVERIFY(!signalFlow.contains(QStringLiteral("wireGeometry.filter(")));
    QVERIFY(qmlLifecycle.contains(QStringLiteral("HOTAS_QML_FLIGHT_DECK_PERF_ONLY")));
    QVERIFY(qmlLifecycle.contains(QStringLiteral("verifyFlightDeckPageNavigationPerformance")));
    QVERIFY(qmlLifecycle.contains(QStringLiteral("flight_deck_page_navigation_summary")));
}

void UiReleaseContractTests::presentationLifecycleSleepsOnlyTheGuiControlPlane()
{
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString worker = sourceFile(QStringLiteral("src/mapping_worker.cpp"));

    QVERIFY(header.contains(QStringLiteral("presentationState READ presentationState NOTIFY presentationStateChanged")));
    QVERIFY(header.contains(QStringLiteral("enum class PresentationLifecycleState")));
    QVERIFY(header.contains(QStringLiteral("Q_INVOKABLE void restoreFromTray()")));
    QVERIFY(backend.contains(QStringLiteral("kVisibleSnapshotIntervalMs = 33")));
    QVERIFY(backend.contains(QStringLiteral("kMinimizedSnapshotIntervalMs = 250")));
    QVERIFY(backend.contains(QStringLiteral("kVisibleNumericTelemetryIntervalMs = 100")));
    QVERIFY(backend.contains(QStringLiteral("kTrayHiddenControllerDiscoveryIntervalMs = 7500")));
    QVERIFY(backend.contains(QStringLiteral("m_snapshotTimer.stop();")));
    QVERIFY(backend.contains(QStringLiteral("m_snapshotTimer.start(kMinimizedSnapshotIntervalMs);")));
    QVERIFY(backend.contains(QStringLiteral("m_snapshotTimer.start(kVisibleSnapshotIntervalMs);")));
    QVERIFY(backend.contains(QStringLiteral("quickWindow->releaseResources();")));
    QVERIFY(backend.contains(QStringLiteral("quickWindow->setPersistentSceneGraph(false);")));
    QVERIFY(backend.contains(QStringLiteral("m_gameDetectionTimer.start(kVisibleGameDetectionIntervalMs);")));
    QVERIFY(backend.contains(QStringLiteral("m_gameDetectionTimer.stop();")));
    for (const QString &page : {sourceFile(QStringLiteral("qml/Standard.qml")),
                                sourceFile(QStringLiteral("qml/Legacy.qml"))}) {
        QVERIFY(page.count(QStringLiteral("active: root.currentPage")) >= 9);
        QVERIFY(!page.contains(QStringLiteral("|| item !== null")));
        QVERIFY(page.contains(QStringLiteral("id: axesPageLoader")));
        QVERIFY(page.contains(QStringLiteral("id: buttonsPageLoader")));
        QVERIFY(page.contains(QStringLiteral("id: calibrationPageLoader")));
        QVERIFY(page.contains(QStringLiteral("id: diagnosticsPageLoader")));
        QVERIFY(page.contains(QStringLiteral("loadedPageCount")));
        QVERIFY(page.contains(QStringLiteral("profileLibraryPresentationState")));
        QVERIFY(page.contains(QStringLiteral("automationPresentationState")));
        QVERIFY(!page.contains(QStringLiteral("id: profilesPage")));
        QVERIFY(!page.contains(QStringLiteral("id: settingsPage\n")));
    }
    const QString profiles = sourceFile(QStringLiteral("qml/ProfileLibrary.qml"));
    const QString automation = sourceFile(QStringLiteral("qml/AutomationPage.qml"));
    QVERIFY(profiles.contains(QStringLiteral("Component.onDestruction: capturePresentationState()")));
    QVERIFY(profiles.contains(QStringLiteral("transferDialogOpen")));
    QVERIFY(automation.contains(QStringLiteral("Component.onDestruction: capturePresentationState()")));
    QVERIFY(automation.contains(QStringLiteral("draft: editing ? clone(draft)")));
    QVERIFY(!worker.contains(QStringLiteral("presentationState")));
    QVERIFY(!worker.contains(QStringLiteral("presentationLifecycle")));
}

void UiReleaseContractTests::curveEditorUsesSelectedAxisTelemetryAndExplicitPaintContracts()
{
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString standard = sourceFile(QStringLiteral("qml/CurveEditor.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/LegacyCurveEditor.qml"));
    const QString flightDeck = sourceFile(QStringLiteral("qml/FlightDeckCurveEditor.qml"));
    const QString axes = sourceFile(QStringLiteral("qml/FlightDeckAxes.qml"));
    QVERIFY(header.contains(QStringLiteral("curveAxisChoices READ curveAxisChoices NOTIFY stateChanged")));
    QVERIFY(header.contains(QStringLiteral("curveEditorTelemetry READ curveEditorTelemetry NOTIFY inputTelemetryChanged")));
    QVERIFY(backend.contains(QStringLiteral("QVariantMap AppBackend::curveEditorTelemetry() const")));
    QVERIFY(backend.contains(QStringLiteral("return false;\n}")));
    for (const QString &editor : {standard, legacy}) {
        QVERIFY(editor.contains(QStringLiteral("backendObject ? backendObject.curveAxisChoices : []")));
        QVERIFY(!editor.contains(QStringLiteral("backendObject ? backendObject.axes : []")));
        QVERIFY(editor.contains(QStringLiteral("function onInputTelemetryChanged()")));
        QVERIFY(editor.contains(QStringLiteral("editor.liveTelemetry = backendObject.curveEditorTelemetry")));
        QVERIFY(editor.contains(QStringLiteral("showEffective = checked; graph.requestPaint()")));
        QVERIFY(editor.contains(QStringLiteral("responseView = !checked; graph.requestPaint()")));
        QVERIFY(editor.contains(QStringLiteral("DASHED · LINEAR REFERENCE")));
        QVERIFY(editor.contains(QStringLiteral("trace(ctx, identity")));
        QVERIFY(editor.contains(QStringLiteral("\"output\", true)")));
        QVERIFY(editor.contains(QStringLiteral("trace(ctx, effective")));
        QVERIFY(editor.contains(QStringLiteral("\"output\", false)")));
    }
    QVERIFY(flightDeck.contains(QStringLiteral("objectName: \"flightDeckCurveGraph\"")));
    QVERIFY(flightDeck.contains(QStringLiteral("readonly property bool liveMarkerVisible")));
    QVERIFY(flightDeck.contains(QStringLiteral("root.liveTelemetry.physicalInput")));
    QVERIFY(flightDeck.contains(QStringLiteral("context.arc(markerX, inputY, 5")));
    QVERIFY(flightDeck.contains(QStringLiteral("context.arc(markerX, outputY, 4")));
    QVERIFY(!axes.contains(QStringLiteral("FlightDeckResponsePreview")));
    QVERIFY(!axes.contains(QStringLiteral("STATIC RESPONSE PREVIEW")));
}

void UiReleaseContractTests::profileOverflowMenuUsesThemedControlContract()
{
    const QString library = sourceFile(QStringLiteral("qml/ProfileLibrary.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    QVERIFY(standard.contains(QStringLiteral("ProfileLibrary")));
    QVERIFY(legacy.contains(QStringLiteral("ProfileLibrary")));
    QVERIFY(library.contains(QStringLiteral("ActionButton { label: \"RENAME\"")));
    QVERIFY(library.contains(QStringLiteral("ActionButton { label: \"DUPLICATE\"")));
    QVERIFY(library.contains(QStringLiteral("ActionButton { label: \"MOVE CATEGORY\"")));
    QVERIFY(library.contains(QStringLiteral("ActionButton { label: \"DELETE\"")));
    QVERIFY(library.contains(QStringLiteral("id: renameProfileDialog")));
    QVERIFY(library.contains(QStringLiteral("id: deleteProfileDialog")));
    QVERIFY(library.contains(QStringLiteral("legacy ? \"#182126\"")));
    QVERIFY(library.contains(QStringLiteral("theme.danger")));
}

void UiReleaseContractTests::unifiedVerifierUsesSharedThemedButtons()
{
    const QString readinessPanel = sourceFile(QStringLiteral("qml/ControllerReadinessPanel.qml"));
    const QString themedButton = sourceFile(QStringLiteral("qml/ThemedButton.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));

    // The V2.6.2 setup truth surface is shared by every non-Flight-Deck shell
    // and uses the common button primitive rather than native controls.
    QVERIFY(readinessPanel.contains(QStringLiteral("ThemedButton")));
    QVERIFY(!readinessPanel.contains(QStringLiteral("\n            Button {")));
    QVERIFY(readinessPanel.contains(QStringLiteral("emphasis: root.hasRepair() ? \"warning\" : \"ready\"")));
    QVERIFY(readinessPanel.contains(QStringLiteral("backendObject ? backendObject.setupTruthSnapshot")));
    QVERIFY(readinessPanel.contains(QStringLiteral("backendObject ? backendObject.setupRepairSession")));
    QVERIFY(readinessPanel.contains(QStringLiteral("return session.active && session.currentStep")));
    QVERIFY(readinessPanel.contains(QStringLiteral("CHECK & REPAIR SETUP")));
    QVERIFY(themedButton.contains(QStringLiteral("property string emphasis")));
    QVERIFY(legacy.contains(QStringLiteral("ControllerReadinessPanel { id: setupAssistantPanel; width: setupAssistantScroll.width; backendObject: backend; themeTokens: root.adaptiveThemeTokens; legacy: true; showTitle: false")));
}

void UiReleaseContractTests::deviceDialogsUseSharedThemedHeaders()
{
    const QString devices = sourceFile(QStringLiteral("qml/DevicesPage.qml"));
    const QString header = sourceFile(QStringLiteral("qml/ThemedDialogHeader.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString context = sourceFile(QStringLiteral("qml/DeviceContextSelector.qml"));

    // Devices owns eleven transactional dialogs plus the batch-review dialog.
    // They must all use the shared header; otherwise Day Ops can silently
    // inherit a generic white Qt title bar even if its dialog body is themed.
    QVERIFY(devices.contains(QStringLiteral("component DeviceDialog: Dialog")));
    QVERIFY(devices.contains(QStringLiteral("header: ThemedDialogHeader")));
    QCOMPARE(devices.count(QStringLiteral("DeviceDialog {")), 12);
    QCOMPARE(devices.count(QStringLiteral("\n    Dialog {")), 0);
    QVERIFY(header.contains(QStringLiteral("property bool legacy")));
    QVERIFY(header.contains(QStringLiteral("theme.panelRaised")));
    QVERIFY(header.contains(QStringLiteral("legacy ? \"#132027\"")));
    QVERIFY(!header.contains(QStringLiteral("#ffffff")));

    // EDIT THIS replaces the shared scope with exactly one saved member. It
    // must be visible in both the persistent header and the rig row.
    QVERIFY(devices.contains(QStringLiteral("function editThisDevice(id)")));
    QVERIFY(devices.contains(QStringLiteral("setEditingDeviceContext(rig.id, [id])")));
    QVERIFY(devices.contains(QStringLiteral("property bool editingTarget: !!modelData.editing")));
    QVERIFY(devices.contains(QStringLiteral("text: editingTarget ? \"EDITING\" : \"EDIT THIS\"")));
    QVERIFY(!devices.contains(QStringLiteral("entries[i].id === modelData.id ? !modelData.selected")));
    QVERIFY(backend.contains(QStringLiteral("{u\"editing\"_qs, rig.id == m_configuration.editingDeviceRigId")));
    QVERIFY(backend.contains(QStringLiteral("emit inputTelemetryChanged();")));
    QVERIFY(backend.contains(QStringLiteral("emit buttonTelemetryChanged();")));
    QVERIFY(context.contains(QStringLiteral("objectName: \"deviceContextLabel\"")));
}

void UiReleaseContractTests::reliabilityCleanupUsesRequiredCapacityAndStableAutomationRows()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString readiness = sourceFile(QStringLiteral("src/controller_readiness.cpp"));
    const QString automation = sourceFile(QStringLiteral("qml/AutomationPage.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));

    QVERIFY(backend.contains(QStringLiteral("return vjoyCapacitySufficient() ? u\"ready\"_qs : u\"warning\"_qs;")));
    QVERIFY(!backend.contains(QStringLiteral("vjoyButtonCount() < vjoyRecommendedButtonCount()")));
    QVERIFY(readiness.contains(QStringLiteral("kRepairReinspectionAttempts = 8")));
    QVERIFY(readiness.contains(QStringLiteral("VJOY CONVERGENCE TIMEOUT — Required %1 buttons; observed %2.")));
    QVERIFY(standard.contains(QStringLiteral("return \"READY\"")));
    QVERIFY(legacy.contains(QStringLiteral("return \"READY\"")));
    QVERIFY(!standard.contains(QStringLiteral("CONFIGURATION LIMITED")));
    QVERIFY(!legacy.contains(QStringLiteral("CONFIGURATION LIMITED")));
    QVERIFY(settings.contains(QStringLiteral("Optional recommended headroom")));

    QVERIFY(automation.contains(QStringLiteral("property int actionIndex: index")));
    QVERIFY(automation.contains(QStringLiteral("property int conditionIndex: index")));
    QVERIFY(automation.contains(QStringLiteral("root.setEffectType(actionCard.actionIndex, choiceIndex)")));
    QVERIFY(automation.contains(QStringLiteral("root.updateAction(actionCard.actionIndex")));
    QVERIFY(automation.contains(QStringLiteral("root.updateCondition(conditionCard.conditionIndex")));
    QVERIFY(!automation.contains(QStringLiteral("root.setEffectType(index, currentIndex)")));
    QVERIFY(!automation.contains(QStringLiteral("root.updateAction(index,")));
    QVERIFY(!automation.contains(QStringLiteral("root.updateCondition(index,")));

    const qsizetype calibrationStart = backend.indexOf(QStringLiteral("void AppBackend::finishCalibration()"));
    const qsizetype calibrationEnd = backend.indexOf(QStringLiteral("void AppBackend::appendCalibrationHistory"), calibrationStart);
    QVERIFY(calibrationStart >= 0 && calibrationEnd > calibrationStart);
    const QString calibration = backend.mid(calibrationStart, calibrationEnd - calibrationStart);
    QVERIFY(calibration.contains(QStringLiteral("No meaningful axis travel was observed")));
    QVERIFY(calibration.contains(QStringLiteral("const bool interiorCenter")));
    QVERIFY(!calibration.contains(QStringLiteral("currentProfile().axes")));
}

void UiReleaseContractTests::virtualOutputLayoutsAreExactAndTelemetryStaysTruthful()
{
    const QString backendHeader = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString readiness = sourceFile(QStringLiteral("src/controller_readiness.cpp"));
    const QString readinessPanel = sourceFile(QStringLiteral("qml/ControllerReadinessPanel.qml"));
    const QString worker = sourceFile(QStringLiteral("src/mapping_worker.cpp"));
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));

    QVERIFY(backendHeader.contains(QStringLiteral("virtualOutputLayouts READ virtualOutputLayouts")));
    QVERIFY(backendHeader.contains(QStringLiteral("assignProfileOutputLayout")));
    QVERIFY(backendHeader.contains(QStringLiteral("createFiveAxisOutputLayout")));
    QVERIFY(backendHeader.contains(QStringLiteral("adoptVirtualOutputVisibility")));
    QVERIFY(backend.contains(QStringLiteral("physicalAxisActivityForObservedSpan")));
    QVERIFY(backend.contains(QStringLiteral("No meaningful movement observed during completed calibration")));
    QVERIFY(readiness.contains(QStringLiteral("capabilityAxesMatch")));
    QVERIFY(readiness.contains(QStringLiteral("Extra available axes")));
    QVERIFY(readiness.contains(QStringLiteral("beginPhysicalReconnectVerification")));
    QVERIFY(readiness.contains(QStringLiteral("Controller disconnected ✓")));
    QVERIFY(readiness.contains(QStringLiteral("applyManagedOutputVisibility")));
    QVERIFY(readiness.contains(QStringLiteral("validateManagedVirtualOutputIdentity")));
    QVERIFY(readiness.contains(QStringLiteral("VID_1234&PID_BEAD")));
    QVERIFY(worker.contains(QStringLiteral("outputLayoutAxes")));
    QVERIFY(worker.contains(QStringLiteral("|| !outputLayoutAxes[static_cast<size_t>(target)]) continue;")));
    QVERIFY(settings.contains(QStringLiteral("Virtual Outputs")));
    QVERIFY(settings.contains(QStringLiteral("CREATE 5-AXIS OUTPUT")));
    QVERIFY(settings.contains(QStringLiteral("PREPARE VISIBILITY")));
    QVERIFY(settings.contains(QStringLiteral("already-open controller handle")));
    QVERIFY(readinessPanel.contains(QStringLiteral("RECONNECT CONTROLLER")));
    QVERIFY(readinessPanel.contains(QStringLiteral("controllerDisconnectObserved")));
    for (const QString &page : {standard, legacy}) {
        QVERIFY(page.contains(QStringLiteral("NOT ROUTED")));
        QVERIFY(page.contains(QStringLiteral("UNMAPPED VJOY AXES PARKED")));
        QVERIFY(page.contains(QStringLiteral("backend.physicalAxisCapabilitySummary")));
        // The route selector must retain its declarative binding after a
        // conflict. An imperative currentIndex write made the displayed row
        // stale even though the backend and worker had accepted the route.
        QVERIFY(!page.contains(QStringLiteral("currentIndex = root.outputChoices.indexOf")));
        QVERIFY(page.contains(QStringLiteral("currentIndex: Math.max(0, root.outputChoices.indexOf")));
    }
}

void UiReleaseContractTests::profileLibraryPortabilityIsSharedAndThemed()
{
    const QString library = sourceFile(QStringLiteral("qml/ProfileLibrary.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString portability = sourceFile(QStringLiteral("src/profile_portability.cpp"));
    QVERIFY(library.contains(QStringLiteral("PROFILE LIBRARY")));
    QVERIFY(library.contains(QStringLiteral("Profile Detail")));
    QVERIFY(library.contains(QStringLiteral("GAME DETECTION")));
    QVERIFY(library.contains(QStringLiteral("AUTOMATIC SELECTION")));
    QVERIFY(!library.contains(QStringLiteral("WHEN THIS CATEGORY ACTIVATES")));
    QVERIFY(!library.contains(QStringLiteral("restoreLastProfile")));
    QVERIFY(library.contains(QStringLiteral("RUNNING APPLICATIONS")));
    QVERIFY(library.contains(QStringLiteral("BROWSE FOR GAME")));
    QVERIFY(library.contains(QStringLiteral("IMPORT / EXPORT")));
    QVERIFY(!library.contains(QStringLiteral("ONE EXECUTABLE PER LINE")));
    QVERIFY(library.contains(QStringLiteral("selectedPackCategoryIds")));
    QVERIFY(library.contains(QStringLiteral("togglePackProfile")));
    QVERIFY(library.contains(QStringLiteral("RELATED CONFIGURATION")));
    QVERIFY(library.contains(QStringLiteral("IMPORT AS NEW")));
    QVERIFY(library.contains(QStringLiteral("APPLY IMPORTED CALIBRATION")));
    QVERIFY(library.contains(QStringLiteral("id: renameCategoryDialog")));
    QVERIFY(library.contains(QStringLiteral("id: deleteCategoryDialog")));
    QVERIFY(library.contains(QStringLiteral("component SelectionToggle")));
    QVERIFY(library.contains(QStringLiteral("component ThemedComboBox")));
    QVERIFY(!library.contains(QStringLiteral("CheckBox")));
    QVERIFY(!library.contains(QStringLiteral("\n                    ComboBox { id:")));
    QVERIFY(standard.contains(QStringLiteral("ProfileLibrary")));
    QVERIFY(legacy.contains(QStringLiteral("ProfileLibrary")));
    QVERIFY(backend.contains(QStringLiteral("runningApplicationSnapshot")));
    QVERIFY(backend.contains(QStringLiteral("categoryForRunningExecutables")));
    QVERIFY(backend.contains(QStringLiteral("selectPortableImportDevice")));
    QVERIFY(portability.contains(QStringLiteral("kPortableProfileSchemaVersion")));
    QVERIFY(portability.contains(QStringLiteral("kPortablePackSchemaVersion")));
    QVERIFY(portability.contains(QStringLiteral("USER SELECTION REQUIRED")));
}

void UiReleaseContractTests::allThemeSelectorsUseSkinnedDarkPopups()
{
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString library = sourceFile(QStringLiteral("qml/ProfileLibrary.qml"));
    const QString automation = sourceFile(QStringLiteral("qml/AutomationPage.qml"));
    const QString curve = sourceFile(QStringLiteral("qml/CurveEditor.qml"));
    const QString legacyCurve = sourceFile(QStringLiteral("qml/LegacyCurveEditor.qml"));
    const QString adaptive = sourceFile(QStringLiteral("qml/AdaptiveResponsePage.qml"));

    for (const QString &page : {standard, legacy, settings, library, automation, curve, legacyCurve, adaptive}) {
        QVERIFY2(page.contains(QStringLiteral("popup: Popup")),
                 "Every selector must own a skinned Popup rather than use a native dropdown.");
        QVERIFY(page.contains(QStringLiteral("background: Rectangle")));
    }
    QVERIFY(standard.contains(QStringLiteral("component FlightComboBox")));
    QVERIFY(standard.contains(QStringLiteral("color: theme.tooltip")));
    QVERIFY(legacy.contains(QStringLiteral("component FlightComboBox")));
    QVERIFY(legacy.contains(QStringLiteral("color: \"#151e23\"")));
    QVERIFY(settings.contains(QStringLiteral("id: appearance")));
    QVERIFY(settings.contains(QStringLiteral("color: root.panelColor")));
    QVERIFY(adaptive.contains(QStringLiteral("component ResponseCombo")));
    QVERIFY(adaptive.contains(QStringLiteral("color: root.themeTokens.tooltip")));
    QVERIFY(curve.contains(QStringLiteral("component AviationMenuItem")));
    QVERIFY(legacyCurve.contains(QStringLiteral("component AviationMenuItem")));
}

void UiReleaseContractTests::adaptiveResponseControlsRetainZeroAndExposeSignalMetrics()
{
    const QString adaptive = sourceFile(QStringLiteral("qml/AdaptiveResponsePage.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    QVERIFY(adaptive.contains(QStringLiteral("function numericOr(value, fallback)")));
    QVERIFY(adaptive.contains(QStringLiteral("adaptiveResponseHistorySince")));
    QVERIFY(adaptive.contains(QStringLiteral("Timer { interval: 33")));
    QVERIFY(adaptive.contains(QStringLiteral("value: root.numericOr(effective().motionSensitivity, 0.035)")));
    QVERIFY(adaptive.contains(QStringLiteral("value: root.numericOr(effective().noiseRejection, 0.012)")));
    QVERIFY(adaptive.contains(QStringLiteral("Safety cancellation is always active")));
    for (const QString &metric : {QStringLiteral("MEDIAN LEAD"), QStringLiteral("MEAN ABS PREDICTION ERROR"),
                                   QStringLiteral("REVERSAL DETECTION LATENCY"), QStringLiteral("FALSE REVERSALS"),
                                   QStringLiteral("STATIONARY LEAD")}) {
        QVERIFY(adaptive.contains(metric));
    }
    QVERIFY(backend.contains(QStringLiteral("const auto physicalAt")));
    QVERIFY(backend.contains(QStringLiteral("meanAbsolutePredictionError")));
    QVERIFY(backend.contains(QStringLiteral("targetOvershoot")));
    QVERIFY(!backend.contains(QStringLiteral("std::abs(predicted) - 1.0F")));
}

void UiReleaseContractTests::adaptiveResponseVisualizerKeepsPredictorAndSimulatorOnTheControlPlane()
{
    const QString adaptive = sourceFile(QStringLiteral("qml/AdaptiveResponsePage.qml"));
    const QString flightDeckAdaptive = sourceFile(QStringLiteral("qml/FlightDeckAdaptiveResponse.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    QVERIFY(adaptive.contains(QStringLiteral("function axisModelIndex(physicalAxis)")));
    QVERIFY(adaptive.contains(QStringLiteral("function selectAxisModelIndex(modelIndex)")));
    QVERIFY(adaptive.contains(QStringLiteral("objectName: \"adaptiveAxisSelector\"")));
    QVERIFY(!adaptive.contains(QStringLiteral("staticPreviewView")));
    QVERIFY(!adaptive.contains(QStringLiteral("showEstimatedTrace")));
    QVERIFY(!adaptive.contains(QStringLiteral("TRACKER STATE")));
    QVERIFY(!adaptive.contains(QStringLiteral("PREDICTOR INTERNALS")));
    QVERIFY(adaptive.contains(QStringLiteral("property bool showFinalTrace: true")));
    QVERIFY(adaptive.contains(QStringLiteral("EFFECTIVE RESPONSE")));
    QVERIFY(adaptive.contains(QStringLiteral("BASELINE OUTPUT")));
    QVERIFY(adaptive.contains(QStringLiteral("PREDICTED MAPPED")));
    QVERIFY(adaptive.contains(QStringLiteral("ONSET / MOTION ACQUISITION")));
    QVERIFY(adaptive.contains(QStringLiteral("Uses coherent acceleration to build predictive response sooner while motion is still gaining speed.")));
    QVERIFY(adaptive.contains(QStringLiteral("Limits how much acceleration may add to predictive authority. Maximum Horizon and Maximum Lead remain absolute limits.")));
    QVERIFY(adaptive.contains(QStringLiteral("SUSTAINED MOTION")));
    QVERIFY(adaptive.contains(QStringLiteral("Builds additional predictive response during continuous, predictable movement, including slower sustained control inputs.")));
    QVERIFY(adaptive.contains(QStringLiteral("Permits longer temporal prediction only when slow, coherent sustained movement supports it.")));
    QVERIFY(adaptive.contains(QStringLiteral("TURNING / REVERSAL")));
    QVERIFY(adaptive.contains(QStringLiteral("Prevents prediction from extending beyond a credible imminent stop or reversal; a correctness floor remains at 0%.")));
    QVERIFY(adaptive.contains(QStringLiteral("MAGNIFIED MAPPED-OUTPUT LEAD")));
    QVERIFY(adaptive.contains(QStringLiteral("configured maximum mapped-output lead")));
    QVERIFY(adaptive.contains(QStringLiteral("piecewise-linear resampling")));
    QVERIFY(adaptive.contains(QStringLiteral("function staticTimeTickLabels()")));
    QVERIFY(adaptive.contains(QStringLiteral("Human-Like Rapid Reversal")));
    QVERIFY(adaptive.contains(QStringLiteral("Fast Full Sweep")));
    QVERIFY(adaptive.contains(QStringLiteral("Very-Fast Full Sweep")));
    QVERIFY(adaptive.contains(QStringLiteral("Same-Side Reversal")));
    QVERIFY(adaptive.contains(QStringLiteral("Rapid Center Crossing")));
    QVERIFY(adaptive.contains(QStringLiteral("Evasive Left/Right")));
    QVERIFY(adaptive.contains(QStringLiteral("Sudden Stop")));
    QVERIFY(adaptive.contains(QStringLiteral("Precision Correction")));
    QVERIFY(adaptive.contains(QStringLiteral("Response Lab")));
    QVERIFY(adaptive.contains(QStringLiteral("component ResponseLabCard")));
    QVERIFY(adaptive.contains(QStringLiteral("ResponseLabCard { id: responseLabCard")));
    QVERIFY(adaptive.contains(QStringLiteral("LIVE CONTROLLER")));
    QVERIFY(!adaptive.contains(QStringLiteral("LIVE HOTAS")));
    QVERIFY(!adaptive.contains(QStringLiteral("Response Lab · Interactive")));
    QVERIFY(!adaptive.contains(QStringLiteral("Response Lab · Live")));
    QVERIFY(adaptive.contains(QStringLiteral("responseLabSource")));
    QVERIFY(adaptive.contains(QStringLiteral("responseLabNearViewport")));
    QVERIFY(adaptive.contains(QStringLiteral("property var responseLabSamples")));
    QVERIFY(adaptive.contains(QStringLiteral("samples: root.responseLabSamples")));
    QVERIFY(adaptive.contains(QStringLiteral("ADAPTIVE RESPONSE MONITOR")));
    QVERIFY(adaptive.contains(QStringLiteral("adaptiveResponseMonitorButton")));
    QVERIFY(adaptive.contains(QStringLiteral("Qt.WindowStaysOnTopHint")));
    QVERIFY(backend.contains(QStringLiteral("captureAdaptiveHistory = connected")));
    QVERIFY(!backend.contains(QStringLiteral("captureAdaptiveHistory = connected && workerRequested")));
    const QString worker = sourceFile(QStringLiteral("src/mapping_worker.cpp"));
    const int directInputPoll = worker.indexOf(QStringLiteral("const HRESULT pollResult = device->Poll();"));
    const int vjoyOutputGate = worker.indexOf(
        QStringLiteral("if (mappingRequested && !m_runtime.mappingActive.load()"));
    QVERIFY(directInputPoll >= 0);
    QVERIFY(vjoyOutputGate >= 0);
    QVERIFY(directInputPoll < vjoyOutputGate);
    QVERIFY(adaptive.contains(QStringLiteral("SLOW-MOTION PLAYBACK")));
    QVERIFY(adaptive.contains(QStringLiteral("Replay speed")));
    QVERIFY(adaptive.contains(QStringLiteral("CHRONOLOGICAL · NEWEST AT RIGHT")));
    QVERIFY(adaptive.contains(QStringLiteral("component ThemedSlider")));
    QVERIFY(adaptive.contains(QStringLiteral("component ThemedSwitch")));
    for (const QString &metric : {QStringLiteral("PRE-REVERSAL LEAD"),
                                  QStringLiteral("LEAD COLLAPSE"),
                                  QStringLiteral("PREDICTOR-ONLY STEP"),
                                  QStringLiteral("VIRTUAL OUTPUT STEP")}) {
        QVERIFY(adaptive.contains(metric));
    }
    QVERIFY(header.contains(QStringLiteral("adaptiveResponseSimulatorStepAtContext")));
    QVERIFY(header.contains(QStringLiteral("adaptiveResponseSimulatorHistorySince")));
    QVERIFY(header.contains(QStringLiteral("injectAdaptiveResponseLiveSampleForTest")));
    QVERIFY(header.contains(QStringLiteral("AdaptiveResponseSimulatorSample")));
    QVERIFY(backend.contains(QStringLiteral("m_adaptiveResponseSimulator.process")));
    QVERIFY(backend.contains(QStringLiteral("reconstructs the\n    // physical gesture between QML pointer events")));
    QVERIFY(backend.contains(QStringLiteral("m_adaptiveResponseSimulatorRecording")));
    QVERIFY(!sourceFile(QStringLiteral("src/mapping_worker.cpp")).contains(
        QStringLiteral("adaptiveResponseSimulator")));
    QVERIFY(flightDeckAdaptive.contains(QStringLiteral("backendObject.axes")));
    QVERIFY(flightDeckAdaptive.contains(QStringLiteral("property int simulatorReplayCursor")));
    QVERIFY(flightDeckAdaptive.contains(QStringLiteral("function updateReplayPresentation()")));
    QVERIFY(flightDeckAdaptive.contains(QStringLiteral("Math.ceil(sampleCount / Math.max(1, Math.floor(plotWidth)))")));
    QVERIFY(!flightDeckAdaptive.contains(QStringLiteral("simulatorSamples.slice(0)")));
    QVERIFY(!flightDeckAdaptive.contains(QStringLiteral("simulatorDisplaySamples = simulatorSamples.slice(0)")));
    QVERIFY(!flightDeckAdaptive.contains(QStringLiteral("interval: 16")));
}

void UiReleaseContractTests::deviceRigRuntimeRetainsDisconnectAndControlPlaneSafetyContracts()
{
    const QString worker = sourceFile(QStringLiteral("src/mapping_worker.cpp"));
    const QString rigHeader = sourceFile(QStringLiteral("src/device_rig.h"));
    const QString rigSource = sourceFile(QStringLiteral("src/device_rig.cpp"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString devices = sourceFile(QStringLiteral("qml/DevicesPage.qml"));

    // Successful vJoy API loads stay process-resident while the mapping worker
    // opens/closes DirectInput and output-device sessions.  Releasing output
    // ownership is still explicit; unloading the vendor DLL between sessions
    // is deliberately not part of the control-plane contract.
    QVERIFY(worker.contains(QStringLiteral("persistentWorkerInterface()")));
    QVERIFY(worker.contains(QStringLiteral("// Do not call FreeLibrary here.  A topology transaction may destroy")));
    QVERIFY(!worker.contains(QStringLiteral("release();\n        FreeLibrary(m_library)")));

    // A multi-member Device Rig has to service the same setup/reacquisition
    // handshake as the compatibility path.  This prevents full verification
    // from racing an acquired vJoy device or stale DirectInput handles.
    QVERIFY(worker.contains(QStringLiteral("if (m_releaseVjoyRequested.exchange(false))")));
    QVERIFY(worker.contains(QStringLiteral("m_vjoyReleasedForControlPlane = true;")));
    QVERIFY(worker.contains(QStringLiteral("Device Rig DirectInput sessions released for controlled reacquisition")));
    QVERIFY(worker.contains(QStringLiteral("m_reacquireInputAcknowledged = requestedReacquire;")));

    // The per-member state must be captured after the DirectInput poll/read
    // boundary, then used for the common mapping gate.  This keeps a lost or
    // not-acquired member from contributing stale output on the current pass.
    QVERIFY(rigHeader.contains(QStringLiteral("enum class DeviceRigInputSessionState")));
    QVERIFY(rigHeader.contains(QStringLiteral("InputLost")));
    QVERIFY(rigHeader.contains(QStringLiteral("NotAcquired")));
    QVERIFY(rigHeader.contains(QStringLiteral("evaluateDeviceRigRuntimeAvailability")));
    QVERIFY(worker.contains(QStringLiteral("Device Rig DirectInput loss:")));
    QVERIFY(worker.contains(QStringLiteral("inputStates[static_cast<size_t>(index)] = read == DIERR_INPUTLOST")));
    QVERIFY(worker.contains(QStringLiteral("evaluateDeviceRigRuntimeAvailability(plan, inputStates,")));
    QVERIFY(rigSource.contains(QStringLiteral("deactivateForRequiredLoss = disconnectBehavior")));
    QVERIFY(rigSource.contains(QStringLiteral("availability.mappingAllowed = mappingRequested")));

    // Devices derives member use from the selected Profile's compiled routes,
    // not from Device Rig membership. Its independent user-facing states must
    // not collapse a connected-but-unused controller into "Disabled".
    QVERIFY(backend.contains(QStringLiteral("compileDeviceRigRuntime(")));
    QVERIFY(backend.contains(QStringLiteral("compiledMemberHasRoute")));
    QVERIFY(backend.contains(QStringLiteral("{u\"inUse\"_qs, inUse}")));
    QVERIFY(devices.contains(QStringLiteral("CONNECTED")));
    QVERIFY(devices.contains(QStringLiteral("OFFLINE")));
    QVERIFY(devices.contains(QStringLiteral("REQUIRED")));
    QVERIFY(devices.contains(QStringLiteral("OPTIONAL")));
    QVERIFY(devices.contains(QStringLiteral("IN USE BY CURRENT PROFILE")));
    QVERIFY(devices.contains(QStringLiteral("UNUSED BY CURRENT PROFILE")));
    QVERIFY(devices.contains(QStringLiteral("HIDDEN FROM GAMES")));
    QVERIFY(devices.contains(QStringLiteral("VISIBLE TO GAMES")));
    QVERIFY(devices.contains(QStringLiteral("ISOLATION NEEDS ATTENTION")));
}

void UiReleaseContractTests::setupAssistantAndOutputCreationExposeObservableContracts()
{
    const QString backendHeader = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString assistant = sourceFile(QStringLiteral("qml/ControllerReadinessPanel.qml"));
    const QString devices = sourceFile(QStringLiteral("qml/DevicesPage.qml"));
    const QString flightDeckDevices = sourceFile(QStringLiteral("qml/FlightDeckDevices.qml"));
    const QString flightDeckOverview = sourceFile(QStringLiteral("qml/FlightDeckOverview.qml"));
    const QString overview = sourceFile(QStringLiteral("qml/OverviewPage.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    const QString issue = sourceFile(QStringLiteral("src/app_issue.h"));
    const QString health = sourceFile(QStringLiteral("qml/AppHealthPopup.qml"));
    const QString readiness = sourceFile(QStringLiteral("src/controller_readiness.cpp"));

    QVERIFY(backendHeader.contains(QStringLiteral("setupAssistantIssues READ setupAssistantIssues")));
    QVERIFY(backendHeader.contains(QStringLiteral("setupAssistantScopeType READ setupAssistantScopeType")));
    QVERIFY(backendHeader.contains(QStringLiteral("setupAssistantSteps READ setupAssistantSteps")));
    QVERIFY(backendHeader.contains(QStringLiteral("appHealthSummary READ appHealthSummary")));
    QVERIFY(backendHeader.contains(QStringLiteral("startSetupAssistantCheckForScope")));
    QVERIFY(backendHeader.contains(QStringLiteral("completeSetupAssistantDevice")));
    QVERIFY(backendHeader.contains(QStringLiteral("beginCalibrationForDevice")));
    QVERIFY(backendHeader.contains(QStringLiteral("skipCalibrationForSetup")));
    QVERIFY(backendHeader.contains(QStringLiteral("applySetupAssistantIssueAction")));
    QVERIFY(backendHeader.contains(QStringLiteral("focusIssueTarget")));
    QVERIFY(backendHeader.contains(QStringLiteral("setSetupAssistantFactsForTest")));
    QVERIFY(backendHeader.contains(QStringLiteral("createDeviceRigResult")));
    QVERIFY(backendHeader.contains(QStringLiteral("createVirtualOutputLayoutResult")));
    const int completeSetupStart = backend.indexOf(
        QStringLiteral("QVariantMap AppBackend::completeSetupAssistantDevice"));
    const int completeSetupEnd = backend.indexOf(
        QStringLiteral("QVariantMap AppBackend::applyPhysicalDeviceGameVisibility"), completeSetupStart);
    QVERIFY(completeSetupStart >= 0);
    QVERIFY(completeSetupEnd > completeSetupStart);
    const QString completeSetup = backend.mid(completeSetupStart,
        completeSetupEnd - completeSetupStart);
    // A saved controller that discovery has already matched must be acquired
    // before verification. Requiring an active worker snapshot here would
    // make the Set Up action reject the very controller it needs to acquire.
    QVERIFY(completeSetup.contains(QStringLiteral("ControllerManager::match(controller")));
    QVERIFY(completeSetup.contains(QStringLiteral("!match.ambiguous && match.recordId == targetId")));
    QVERIFY(completeSetup.contains(QStringLiteral(
        "startExplicitNewControllerVerification(discovered->directInputId, discovered->name)")));
    QVERIFY(!completeSetup.contains(QStringLiteral("currentPhysicalCapabilities()")));
    QVERIFY(backend.contains(QStringLiteral("PhysicalDeviceAcquisitionFailed")));
    QVERIFY(backend.contains(QStringLiteral("RETRY ACQUISITION")));
    QVERIFY(backend.contains(QStringLiteral("m_setupAssistantDeviceAcquisitionFailures")));
    QVERIFY(backend.contains(QStringLiteral("Connect a controller to create your first Device Rig.")));
    QVERIFY(backend.contains(QStringLiteral("match-physical")));
    QVERIFY(backend.contains(QStringLiteral("copy-output")));
    QVERIFY(backend.contains(QStringLiteral("continuousPovs")));
    QVERIFY(backend.contains(QStringLiteral("discretePovs")));
    QVERIFY(backend.contains(QStringLiteral("PhysicalDeviceOffline")));
    QVERIFY(backend.contains(QStringLiteral("OptionalDeviceOffline")));
    QVERIFY(backend.contains(QStringLiteral("VirtualOutputBusy")));
    QVERIFY(backend.contains(QStringLiteral("m_pendingSetupVerificationRecordId")));
    QVERIFY(backend.contains(QStringLiteral("physicalStatus == VerificationSubsystemState::Ready")));
    QVERIFY(backend.contains(QStringLiteral("HidHideUnavailable")));
    QVERIFY(backend.contains(QStringLiteral("NoMappedControl")));
    QVERIFY(backend.contains(QStringLiteral("meaningfulInputSequence")));
    QVERIFY(backend.contains(QStringLiteral("deviceRigMeaningfulOutputSequence")));
    QVERIFY(backend.contains(QStringLiteral("m_virtualOutputReadinessPlans")));
    QVERIFY(backend.contains(QStringLiteral("refreshVirtualOutputReadiness(normalizedId)")));
    QVERIFY(backend.contains(QStringLiteral("An absent custom calibration is a safe, supported default")));
    QVERIFY(!backend.contains(QStringLiteral("const bool ready = active && !m_configuration.activeDeviceRigId.isEmpty()")));
    QVERIFY(backend.contains(QStringLiteral("QVariantList AppBackend::setupAssistantSteps")));
    QVERIFY(backend.contains(QStringLiteral("applyPhysicalDeviceGameVisibility")));
    QVERIFY(!backend.contains(QStringLiteral("name.startsWith(u\"INPUT")));
    QVERIFY(!backend.contains(QStringLiteral("name.contains(u\"VISIBILITY")));
    QVERIFY(standard.contains(QStringLiteral("SETUP HEALTH & REPAIR")));
    QVERIFY(standard.contains(QStringLiteral("standardAppHealthControl")));
    QVERIFY(standard.contains(QStringLiteral("navigateToIssue(target)")));
    QVERIFY(standard.contains(QStringLiteral("backend.focusIssueTarget")));
    QVERIFY(standard.contains(QStringLiteral("devices.focusIssueTarget(target)")));
    for (const QString &ui : {standard, legacy}) {
        QVERIFY(ui.contains(QStringLiteral("controllerSetupDialog.open()")));
        QVERIFY(ui.contains(QStringLiteral("backend.setEditingDeviceContext(rigId, deviceId !== \"\" ? [deviceId] : [])")));
        QVERIFY(ui.contains(QStringLiteral("onOpened: backend.checkSetupHealth()")));
    }
    QVERIFY(devices.contains(QStringLiteral("function showTransientActionFeedback")));
    QVERIFY(devices.contains(QStringLiteral("actionFeedbackDismissTimer")));
    QVERIFY(devices.contains(QStringLiteral("contentHeight: contentLayout ? contentLayout.measuredHeight + 20 : 0")));
    QVERIFY(devices.contains(QStringLiteral("rigDetailsActionsDismissArea")));
    QVERIFY(devices.contains(QStringLiteral("function reposition()")));
    QVERIFY(!devices.contains(QStringLiteral("HIDE ALL INPUTS")));
    QVERIFY(!devices.contains(QStringLiteral("SHOW ACTIVE OUTPUTS")));
    QVERIFY(devices.contains(QStringLiteral("Refreshing devices")));
    QVERIFY(devices.contains(QStringLiteral("5000")));
    QVERIFY(health.contains(QStringLiteral("x: Math.max(0, Math.round(((parent ? parent.width : width) - width) / 2))")));
    QVERIFY(health.contains(QStringLiteral("y: Math.max(0, Math.round(((parent ? parent.height : height) - height) / 2))")));
    QVERIFY(health.contains(QStringLiteral("border.width: 2")));
    QVERIFY(health.contains(QStringLiteral("implicitWidth: 32")));
    QVERIFY(backendHeader.contains(QStringLiteral("setupTruthSnapshot READ setupTruthSnapshot")));
    QVERIFY(backendHeader.contains(QStringLiteral("setupRepairSession READ setupRepairSession")));
    QVERIFY(backendHeader.contains(QStringLiteral("checkSetupHealth")));
    QVERIFY(backendHeader.contains(QStringLiteral("repairSetupHealth")));
    QVERIFY(backend.contains(QStringLiteral("PhysicalDeviceUnverified")));
    QVERIFY(backend.contains(QStringLiteral("UNKNOWN / INSPECTION FAILED")));
    QVERIFY(backend.contains(QStringLiteral("SetupConvergenceStage::Results")));
    QVERIFY(backend.contains(QStringLiteral("applyScopedVJoyRepair")));
    QVERIFY(backend.contains(QStringLiteral("applyScopedHidHideRepair")));
    QVERIFY(backend.contains(QStringLiteral("issue.value(u\"code\"_qs).toString() != u\"HidHideMismatch\"_qs")));
    QVERIFY(backend.contains(QStringLiteral("m_setupTruthBeforeSnapshot = m_setupTruthSnapshot")));
    QVERIFY(backend.contains(QStringLiteral("progressPercent")));
    QVERIFY(backend.contains(QStringLiteral("completedStepCount")));
    QVERIFY(backend.contains(QStringLiteral("Performing final full inspection")));
    QVERIFY(readiness.contains(QStringLiteral("QString vJoyConfigurationAxisToken(VirtualAxis axis)")));
    QVERIFY(readiness.contains(QStringLiteral("case VirtualAxis::Slider0: return QStringLiteral(\"Sl0\")")));
    QVERIFY(readiness.contains(QStringLiteral("case VirtualAxis::Slider1: return QStringLiteral(\"Sl1\")")));
    QVERIFY(readiness.contains(QStringLiteral("arguments.append(vJoyConfigurationAxisToken")));
    QVERIFY(readiness.contains(QStringLiteral("bool ControllerReadinessService::applyHidHideConfiguration()")));
    QVERIFY(readiness.contains(QStringLiteral("hidhideOnlyPlan.vjoyNeedsChanges = false")));
    QVERIFY(assistant.contains(QStringLiteral("backendObject ? backendObject.setupTruthSnapshot")));
    QVERIFY(assistant.contains(QStringLiteral("backendObject ? backendObject.setupRepairSession")));
    QVERIFY(assistant.contains(QStringLiteral("CURRENT STEP")));
    QVERIFY(assistant.contains(QStringLiteral("STAGE PROGRESS")));
    QVERIFY(assistant.contains(QStringLiteral("setupStageTimeline")));
    QVERIFY(assistant.contains(QStringLiteral("progressPercent")));
    QVERIFY(assistant.contains(QStringLiteral("REPAIR PLAN")));
    QVERIFY(assistant.contains(QStringLiteral("COPY FULL DIAGNOSTICS")));
    QVERIFY(assistant.contains(QStringLiteral("repairSetupHealth")));
    QVERIFY(assistant.contains(QStringLiteral("checkingColor")));
    QVERIFY(assistant.contains(QStringLiteral("useHostRepairConfirmation")));
    QVERIFY(assistant.contains(QStringLiteral("ThemedDialogHeader")));
    QVERIFY(flightDeckDevices.contains(QStringLiteral("useHostRepairConfirmation: true")));
    QVERIFY(flightDeckDevices.contains(QStringLiteral("onRepairRequested: repairConfirmation.open()")));
    QVERIFY(flightDeckDevices.contains(QStringLiteral("model: root.setupRepairPlan")));
    QVERIFY(flightDeckDevices.contains(QStringLiteral("setupTruth.repairPlan")));
    QVERIFY(flightDeckOverview.contains(QStringLiteral("backend.setupTruthSnapshot")));
    QVERIFY(flightDeckOverview.contains(QStringLiteral("readonly property var setupPhysical: setupGroup(\"physical\")")));
    QVERIFY(flightDeckOverview.contains(QStringLiteral("readonly property var setupOutput: setupGroup(\"vjoy\")")));
    QVERIFY(flightDeckOverview.contains(QStringLiteral("readonly property var setupIsolation: setupGroup(\"isolation\")")));
    QVERIFY(flightDeckOverview.contains(QStringLiteral("label: setupTruth.overallStatus || \"CHECKING\"")));
    QVERIFY(overview.contains(QStringLiteral("model: root.setupTruth.groups || []")));
    QVERIFY(overview.contains(QStringLiteral("backend.setupTruthSnapshot")));
    QVERIFY(standard.contains(QStringLiteral("showTitle: false")));
    QVERIFY(legacy.contains(QStringLiteral("showTitle: false")));
    QVERIFY(devices.contains(QStringLiteral("MATCH PHYSICAL DEVICE")));
    QVERIFY(devices.contains(QStringLiteral("COPY VJOY OUTPUT")));
    QVERIFY(devices.contains(QStringLiteral("CREATE OUTPUT")));
    QVERIFY(devices.contains(QStringLiteral("Copy ")));
    QVERIFY(devices.contains(QStringLiteral("firstDeviceInputsPanel")));
    QVERIFY(devices.contains(QStringLiteral("PHYSICAL INPUTS")));
    QVERIFY(devices.contains(QStringLiteral("virtualInputsPanel")));
    QVERIFY(devices.contains(QStringLiteral("Routing loop not allowed")));
    QVERIFY(devices.contains(QStringLiteral("virtualOutputsInventoryPanel")));
    QVERIFY(devices.contains(QStringLiteral("openStandaloneOutputCreator")));
    QVERIFY(devices.contains(QStringLiteral("openStandaloneCreateOutputFromEmptyButton")));
    QVERIFY(devices.contains(QStringLiteral("returnToOutputInventory")));
    QVERIFY(devices.contains(QStringLiteral("BUTTON CAPACITY")));
    QVERIFY(devices.contains(QStringLiteral("customButtonCapacityStepper")));
    QVERIFY(devices.contains(QStringLiteral("customContinuousPovsStepper")));
    QVERIFY(devices.contains(QStringLiteral("customDiscretePovsStepper")));
    QVERIFY(devices.contains(QStringLiteral("createRigWithInputs")));
    QVERIFY(devices.contains(QStringLiteral("deviceActionFeedback")));
    QVERIFY(devices.contains(QStringLiteral("function focusIssueTarget(target)")));
    QVERIFY(devices.contains(QStringLiteral("REFRESH DEVICES")));
    QVERIFY(devices.contains(QStringLiteral("verificationRequested(string rigId, string deviceId, string outputId)")));
    QVERIFY(devices.contains(QStringLiteral("root.requestVerification(root.selectedRigId, \"\", root.selectedOutputId)")));
    QVERIFY(devices.contains(QStringLiteral("calibrationRequested")));
    QVERIFY(devices.contains(QStringLiteral("to calibrate it.")));
    QVERIFY(devices.contains(QStringLiteral("SAVED / OFFLINE")));
    QVERIFY(devices.contains(QStringLiteral("savedOfflineControllerRepeater")));
    QVERIFY(!devices.contains(QStringLiteral("NOT ADOPTED")));
    QVERIFY(!devices.contains(QStringLiteral("HID identity")));
    QVERIFY(!devices.contains(QStringLiteral("CREATE & VERIFY")));
    for (const QString &ui : {standard, legacy}) {
        QVERIFY(ui.contains(QStringLiteral("contentItem: Flickable")));
        QVERIFY(ui.contains(QStringLiteral("contentHeight: setupAssistantPanel.implicitHeight")));
    }
    QVERIFY(overview.contains(QStringLiteral("systemReadinessList")));
    QVERIFY(overview.contains(QStringLiteral("CHECK SETUP")));
    QVERIFY(issue.contains(QStringLiteral("struct AppIssue")));
    QVERIFY(issue.contains(QStringLiteral("navigationTarget")));
    QVERIFY(issue.contains(QStringLiteral("affectedObjectIds")));
    QVERIFY(readiness.contains(QStringLiteral("applyManagedPhysicalInputVisibility")));
    QVERIFY(readiness.contains(QStringLiteral("capabilities are correct")));
    QVERIFY(readiness.contains(QStringLiteral("ownership is reported separately")));
    QVERIFY(readiness.contains(QStringLiteral("const SetupProcessResult readback")));
    QVERIFY(readiness.contains(QStringLiteral("completed changes were rolled back")));
    QVERIFY(health.contains(QStringLiteral("APP HEALTH")));
    QVERIFY(health.contains(QStringLiteral("navigationRequested")));
}

void UiReleaseContractTests::inputLearningAndLiveNameDraftsStayOnControlPlane()
{
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString readiness = sourceFile(QStringLiteral("src/controller_readiness.cpp"));

    for (const QString &ui : {standard, legacy}) {
        QVERIFY(ui.contains(QStringLiteral("component LiveDraftTextInput")));
        QVERIFY(ui.contains(QStringLiteral("property string persistedText")));
        QVERIFY(ui.contains(QStringLiteral("onPersistedTextChanged: if (!editing")));
        QVERIFY(ui.contains(QStringLiteral("Keys.onEscapePressed")));
        QVERIFY(ui.contains(QStringLiteral("persistedText: processingPanel.info.customName || \"\"")));
        QVERIFY(ui.contains(QStringLiteral("persistedText: processingPanel.info.outputAlias || \"\"")));
        QVERIFY(ui.contains(QStringLiteral("GAME OUTPUT NAME")));
        QVERIFY(ui.contains(QStringLiteral("buttonCard.info.customName ? buttonCard.info.customName.toUpperCase()")));
        QVERIFY(ui.contains(QStringLiteral("buttonNameEditor")));
        QVERIFY(!ui.contains(QStringLiteral("text: buttonCard.info.customName || \"\"")));
        QVERIFY(ui.contains(QStringLiteral("label: \"LEARN INPUT\"")));
        QVERIFY(ui.contains(QStringLiteral("QUICK MAP — AXES")));
        QVERIFY(ui.contains(QStringLiteral("QUICK MAP — BUTTONS")));
        QVERIFY(ui.contains(QStringLiteral("text: \"DISABLED\"")));
        QVERIFY(ui.contains(QStringLiteral("header: Item { implicitHeight: 0 }")));
    }
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantMap inputLearning")));
    QVERIFY(header.contains(QStringLiteral("quickMapButtonTargets")));
    QVERIFY(header.contains(QStringLiteral("void processInputLearning();")));
    QVERIFY(backend.contains(QStringLiteral("selectLearnedAxis(m_inputLearning.axisBaseline")));
    QVERIFY(backend.contains(QStringLiteral("InputLearningPhase::Arming")));
    QVERIFY(backend.contains(QStringLiteral("HOLD CONTROLS STEADY")));
    QVERIFY(backend.contains(QStringLiteral("RELEASE HELD BUTTONS")));
    QVERIFY(backend.contains(QStringLiteral("processInputLearning();")));
    QVERIFY(!sourceFile(QStringLiteral("src/mapping_worker.cpp")).contains(QStringLiteral("InputLearning")));
    QVERIFY(readiness.contains(QStringLiteral("Output-layout button counts are provisioned capacity")));
    QVERIFY(readiness.contains(QStringLiteral("requirements.buttons = 0;")));
}

void UiReleaseContractTests::buttonLearningIsDestinationFirstAndCardsShowLiveSignalFlow()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    for (const QString &ui : {sourceFile(QStringLiteral("qml/Standard.qml")),
                              sourceFile(QStringLiteral("qml/Legacy.qml"))}) {
        const qsizetype cardStart = ui.indexOf(QStringLiteral("component ButtonCard: Panel"));
        const qsizetype cardEnd = ui.indexOf(QStringLiteral("component PovNativeCard: Panel"), cardStart);
        QVERIFY(cardStart >= 0);
        QVERIFY(cardEnd > cardStart);
        const QString card = ui.mid(cardStart, cardEnd - cardStart);
        QVERIFY(!card.contains(QStringLiteral("LEARN INPUT")));
        QVERIFY(!card.contains(QStringLiteral("PHYSICAL   ")));
        QVERIFY(!card.contains(QStringLiteral("VIRTUAL    ")));
        QVERIFY(card.contains(QStringLiteral("PHYSICAL INPUT")));
        QVERIFY(card.contains(QStringLiteral("VIRTUAL OUTPUT")));
        QVERIFY(card.contains(QStringLiteral("PRESSED")));
        QVERIFY(card.contains(QStringLiteral("RELEASED")));
        QVERIFY(card.contains(QStringLiteral("DISABLED")));
        QVERIFY(card.contains(QStringLiteral("text: \"▶\"")));
        QVERIFY(card.contains(QStringLiteral("buttonCard.info.virtualPressed")));
        QVERIFY(card.contains(QStringLiteral("buttonCard.info.pressed || buttonCard.info.virtualPressed")));
        QVERIFY(ui.contains(QStringLiteral("label: \"LEARN BUTTON\"")));
        QVERIFY(ui.contains(QStringLiteral("id: learnButtonDialog")));
        QVERIFY(ui.contains(QStringLiteral("model: root.buttonOutputChoices.slice(1)")));
        QVERIFY(ui.contains(QStringLiteral("enabled: !backend.inputLearning.active")));
        QVERIFY(ui.contains(QStringLiteral("CURRENT PHYSICAL INPUT")));
        QVERIFY(ui.contains(QStringLiteral("backend.startButtonLearning(learnButtonDialog.selectedTarget)")));
        QVERIFY(ui.contains(QStringLiteral("backend.resolveInputLearningConflict(\"ignore\")")));
    }

    const qsizetype buttonLearningStart = backend.indexOf(QStringLiteral("bool AppBackend::startButtonLearning"));
    const qsizetype buttonLearningEnd = backend.indexOf(QStringLiteral("bool AppBackend::startPovLearning"), buttonLearningStart);
    QVERIFY(buttonLearningStart >= 0);
    QVERIFY(buttonLearningEnd > buttonLearningStart);
    const QString buttonLearning = backend.mid(buttonLearningStart, buttonLearningEnd - buttonLearningStart);
    QVERIFY(buttonLearning.contains(QStringLiteral("m_inputLearning.virtualButton = virtualButton")));

    const qsizetype learningProcessingStart = backend.indexOf(QStringLiteral("void AppBackend::processInputLearning()"));
    const qsizetype learningProcessingEnd = backend.indexOf(QStringLiteral("void AppBackend::refreshUiSnapshot()"), learningProcessingStart);
    QVERIFY(learningProcessingStart >= 0);
    QVERIFY(learningProcessingEnd > learningProcessingStart);
    const QString learningProcessing = backend.mid(learningProcessingStart, learningProcessingEnd - learningProcessingStart);
    QVERIFY(learningProcessing.contains(QStringLiteral("m_inputLearning.sourceButton = button")));
    QVERIFY(learningProcessing.contains(QStringLiteral("applyLearnedInput();")));
    QVERIFY(!learningProcessing.contains(QStringLiteral("m_inputLearning.virtualButton = button")));
}

void UiReleaseContractTests::axisConflictsRequireExplicitSignalFlowDecisions()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QStringList editors = {
        sourceFile(QStringLiteral("qml/Standard.qml")),
        sourceFile(QStringLiteral("qml/Legacy.qml")),
        sourceFile(QStringLiteral("qml/FlightDeckAxes.qml")),
    };
    QVERIFY(backend.contains(QStringLiteral("QVariantMap AppBackend::resolveAxisMappingConflict")));
    QVERIFY(backend.contains(QStringLiteral("SignalFlowMixerMode::HighestMagnitude")));
    QVERIFY(backend.contains(QStringLiteral("commitSignalFlowCommand")));
    for (const QString &editor : editors) {
        QVERIFY(editor.contains(QStringLiteral("resolveAxisMappingConflict")));
        QVERIFY(editor.contains(QStringLiteral("REPLACE")));
        QVERIFY(editor.contains(QStringLiteral("AVERAGE")));
        QVERIFY(editor.contains(QStringLiteral("HIGHEST")));
        QVERIFY(!editor.contains(QStringLiteral("row-order output policy")));
    }
}

void UiReleaseContractTests::installerUpgradeAcceptanceTracksSchema28()
{
    const QString fixture = sourceFile(QStringLiteral("tests/upgrade_configuration_fixture.cpp"));
    const QString installer = sourceFile(QStringLiteral("scripts/verify-installer-upgrade.ps1"));
    const QString updater = sourceFile(QStringLiteral("scripts/verify-published-updater.ps1"));
    QVERIFY(fixture.contains(QStringLiteral("persist schema 28")));
    QVERIFY(fixture.contains(QStringLiteral("--assert-v28")));
    QVERIFY(fixture.contains(QStringLiteral("--assert-fresh-v28")));
    QVERIFY(!fixture.contains(QStringLiteral("--assert-v16")));
    QVERIFY(installer.contains(QStringLiteral("& $fixture --assert-v28")));
    QVERIFY(installer.contains(QStringLiteral("& $fixture --assert-fresh-v28")));
    QVERIFY(installer.contains(QStringLiteral("v2.5.0 -> candidate")));
    QVERIFY(installer.contains(QStringLiteral("Assert-InstalledPackage")));
    QVERIFY(installer.contains(QStringLiteral("-AllowMissingLauncher")));
    QVERIFY(installer.contains(QStringLiteral("Remove-InstallerTestInstallation $priorStableInstall")));
    QVERIFY(installer.contains(QStringLiteral("Default acceptance path")));
    QVERIFY(updater.contains(QStringLiteral("& $fixture --assert-v28")));
    QVERIFY(updater.contains(QStringLiteral("v2.5.0 updater")));
}

void UiReleaseContractTests::flightDeckTypographyContract()
{
    const QString tokens = sourceFile(QStringLiteral("qml/FlightDeckTheme.qml"));
    const QString shell = sourceFile(QStringLiteral("qml/Main.qml"));
    const QString startup = sourceFile(QStringLiteral("src/main.cpp"));
    const QString releaseWorkflow = sourceFile(QStringLiteral(".github/workflows/release.yml"));
    const QString ciWorkflow = sourceFile(QStringLiteral(".github/workflows/ci.yml"));
    const QString toolchainCheck = sourceFile(QStringLiteral("scripts/verify-toolchain-consistency.ps1"));

    QVERIFY(tokens.contains(QStringLiteral("readonly property string displayFont: \"Segoe UI\"")));
    QVERIFY(tokens.contains(QStringLiteral("readonly property string bodyFont: displayFont")));
    QVERIFY(tokens.contains(QStringLiteral("readonly property string telemetryFont: \"Consolas\"")));
    QVERIFY(shell.contains(QStringLiteral("themeManager.currentExperience === \"Flight Deck\" ? \"Segoe UI\"")));
    QVERIFY(startup.contains(QStringLiteral("application.setFont(QFont(QStringLiteral(\"Segoe UI\")))")));
    for (const QString &workflow : {releaseWorkflow, ciWorkflow}) {
        QVERIFY(workflow.contains(QStringLiteral("Install qualified Qt 6.8.3")));
        QVERIFY(workflow.contains(QStringLiteral("version: '6.8.3'")));
        QVERIFY(workflow.contains(QStringLiteral("arch: 'win64_msvc2022_64'")));
        QVERIFY(workflow.contains(QStringLiteral("verify-toolchain-consistency.ps1")));
        QVERIFY(!workflow.contains(QStringLiteral("6.5.3")));
        QVERIFY(!workflow.contains(QStringLiteral("msvc2019")));
    }
    QVERIFY(ciWorkflow.contains(QStringLiteral("qmllint failed for")));
    QVERIFY(!ciWorkflow.contains(QStringLiteral("Qt 6.5 uses")));
    QVERIFY(toolchainCheck.contains(QStringLiteral("$qualifiedQtVersion = '6.8.3'")));
    QVERIFY(toolchainCheck.contains(QStringLiteral("$qualifiedQtArch = 'win64_msvc2022_64'")));
}

void UiReleaseContractTests::flightDeckInformationArchitectureContract()
{
    const QString flightDeck = sourceFile(QStringLiteral("qml/FlightDeck.qml"));
    const QString headerPill = sourceFile(QStringLiteral("qml/FlightDeckHeaderPill.qml"));
    const QString selector = sourceFile(QStringLiteral("qml/FlightDeckSelectedDeviceSelector.qml"));
    const QString adaptive = sourceFile(QStringLiteral("qml/FlightDeckAdaptiveResponse.qml"));
    const QString profiles = sourceFile(QStringLiteral("qml/FlightDeckProfiles.qml"));
    const QString curve = sourceFile(QStringLiteral("qml/CurveEditor.qml"));
    const QString flightDeckCurve = sourceFile(QStringLiteral("qml/FlightDeckCurveEditor.qml"));
    const QString flightDeckTheme = sourceFile(QStringLiteral("qml/FlightDeckTheme.qml"));
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString backendHeader = sourceFile(QStringLiteral("src/app_backend.h"));

    QVERIFY(flightDeck.contains(QStringLiteral("objectName: \"flightDeckSharedPageTitle\"")));
    QVERIFY(!flightDeck.contains(QStringLiteral("visible: root.currentPage !== 8")));
    QVERIFY(!flightDeck.contains(QStringLiteral("visible: [0, 1, 3, 5, 6, 9].indexOf(root.currentPage) >= 0")));
    QVERIFY(flightDeck.contains(QStringLiteral("objectName: \"flightDeckControllerPill\"")));
    QVERIFY(flightDeck.contains(QStringLiteral("objectName: \"flightDeckAppearancePill\"")));
    QVERIFY(flightDeck.contains(QStringLiteral("onClicked: root.navigateTo(2)")));
    QVERIFY(flightDeck.contains(QStringLiteral("themeManager.setFlightDeckAppearance")));
    const qsizetype axes = flightDeck.indexOf(QStringLiteral("{ label: \"Axes\", page: 0 }"));
    const qsizetype curveNavigation = flightDeck.indexOf(QStringLiteral("{ label: \"Curve editor\", page: 6 }"));
    const qsizetype buttons = flightDeck.indexOf(QStringLiteral("{ label: \"Buttons\", page: 1 }"));
    QVERIFY(axes >= 0 && buttons > axes && curveNavigation > buttons);
    QVERIFY(headerPill.contains(QStringLiteral("Accessible.role: Accessible.Button")));
    QVERIFY(headerPill.contains(QStringLiteral("focusPolicy: Qt.TabFocus")));
    QVERIFY(headerPill.contains(QStringLiteral("control.visualFocus")));
    QVERIFY(!headerPill.contains(QStringLiteral("forceActiveFocus()")));
    QVERIFY(selector.contains(QStringLiteral("SELECTED DEVICE")));
    QVERIFY(selector.contains(QStringLiteral("backendObject.selectedDevices")));
    QVERIFY(selector.contains(QStringLiteral("backendObject.setSelectedDeviceContext")));
    QVERIFY(backendHeader.contains(QStringLiteral("Q_PROPERTY(QString selectedDeviceRigId")));
    QVERIFY(backendHeader.contains(QStringLiteral("Q_INVOKABLE bool setSelectedDeviceContext")));
    QVERIFY(adaptive.contains(QStringLiteral("BASIC RESPONSE · CONFIGURED LIMITS")));
    QVERIFY(adaptive.contains(QStringLiteral("root.effective().maximumHorizonMs")));
    QVERIFY(adaptive.contains(QStringLiteral("VISIBLE TRACES")));
    QVERIFY(adaptive.contains(QStringLiteral("Normal Movement Response")));
    QVERIFY(adaptive.contains(QStringLiteral("Rapid Movement Response")));
    QVERIFY(adaptive.contains(QStringLiteral("Engagement Sensitivity")));
    QVERIFY(adaptive.contains(QStringLiteral("NORMAL MOVEMENT")));
    QVERIFY(adaptive.contains(QStringLiteral("RAPID MOVEMENT")));
    QVERIFY(adaptive.contains(QStringLiteral("MAPPED PHYSICAL")));
    QVERIFY(adaptive.contains(QStringLiteral("PREDICTOR OUTPUT")));
    QVERIFY(adaptive.contains(QStringLiteral("PRE-CAP REQUEST")));
    QVERIFY(adaptive.contains(QStringLiteral("POST-CAP LEAD")));
    QVERIFY(adaptive.contains(QStringLiteral("ENDPOINT TAPER")));
    QVERIFY(adaptive.contains(QStringLiteral("component TraceLegend: Button")));
    QVERIFY(adaptive.contains(QStringLiteral("focusPolicy: Qt.TabFocus")));
    QVERIFY(adaptive.contains(QStringLiteral("control.visualFocus")));
    QVERIFY(!adaptive.contains(QStringLiteral("onPressed: control.forceActiveFocus()")));
    QVERIFY(adaptive.contains(QStringLiteral("Awaiting controller input")));
    QVERIFY(adaptive.indexOf(QStringLiteral("objectName: \"adaptiveSelectedDeviceContext\""))
            < adaptive.indexOf(QStringLiteral("objectName: \"adaptiveContextTarget\"")));
    QVERIFY(adaptive.indexOf(QStringLiteral("objectName: \"adaptiveContextTarget\""))
            < adaptive.indexOf(QStringLiteral("objectName: \"adaptiveContextAxis\"")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckProfileLibrary\"")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckProfileDetailPane\"")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckCategoryOpenDetails\"")));
    QVERIFY(profiles.contains(QStringLiteral("root.openCategory(root.selectedCategoryId)")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckRunningGameSearch\"")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckAddGameDialog\"")));
    QVERIFY(profiles.contains(QStringLiteral("objectName: \"flightDeckRunningGameNoMatch\"")));
    QVERIFY(profiles.contains(QStringLiteral("filteredRunningApplications")));
    QVERIFY(profiles.contains(QStringLiteral("displayName.indexOf(query)")));
    QVERIFY(profiles.contains(QStringLiteral("executable.indexOf(query)")));
    QVERIFY(profiles.contains(QStringLiteral("Clear the search to see all running applications")));
    QVERIFY(profiles.contains(QStringLiteral("AUTOMATIC ACTIVATION")));
    QVERIFY(profiles.contains(QStringLiteral("flightDeckCategoryActivationResolver")));
    QVERIFY(profiles.contains(QStringLiteral("flightDeckProfileAutomaticPolicySelector")));
    QVERIFY(profiles.contains(QStringLiteral("reorderCategoryAutomaticProfiles")));
    QVERIFY(backendHeader.contains(QStringLiteral("Q_PROPERTY(QVariantMap activationResolverState")));
    QVERIFY(backendHeader.contains(QStringLiteral("Q_INVOKABLE bool resumeAutomaticActivation")));
    QVERIFY(standard.contains(QStringLiteral("root.flightDeckMode ? flightDeckCurveEditorComponent : legacyCurveEditorComponent")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("objectName: \"flightDeckCurveEditor\"")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("ACTIVE CURVE CONTEXT")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("RESPONSE SURFACE")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("OVERLAY & WORKSPACE TOOLS")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("backendObject.setCurveFamily")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("backendObject.setCurveStrength")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("backendObject.setCurvePoint")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("color: tokens.graphSurface")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("context.fillStyle = tokens.graphBackground")));
    QVERIFY(flightDeckCurve.contains(QStringLiteral("tokens.graphLockedPoint : index === selectedPoint ? tokens.graphSelectedPoint : tokens.graphPoint")));
    QVERIFY(flightDeckTheme.contains(QStringLiteral("readonly property color graphSurface: light ?")));
    QVERIFY(flightDeckTheme.contains(QStringLiteral("readonly property color graphPoint: light ?")));
    QVERIFY(!flightDeckCurve.contains(QStringLiteral("AviationPanel")));
    QVERIFY(curve.contains(QStringLiteral("readonly property bool flightDeck")));
    QVERIFY(curve.contains(QStringLiteral("flightDeck ? theme.primarySurface")));
    QVERIFY(curve.contains(QStringLiteral("PHYSICAL INPUT  ·  %")));
    QVERIFY(curve.contains(QStringLiteral("MAPPED OUTPUT  ·  %")));
    QVERIFY(curve.contains(QStringLiteral("CURVE ANALYSIS")));
}

void UiReleaseContractTests::mapperPostBuildDeploymentIncludesQmlModules()
{
    const QString cmake = sourceFile(QStringLiteral("CMakeLists.txt"));
    const QString staging = sourceFile(QStringLiteral("scripts/stage-package.ps1"));
    QVERIFY(cmake.contains(QStringLiteral("--qmldir \"${CMAKE_CURRENT_SOURCE_DIR}/qml\"")));
    QVERIFY(staging.contains(QStringLiteral("--qmldir (Join-Path $repoRoot 'qml')")));
    QVERIFY(staging.contains(QStringLiteral("Find-MsvcRuntimeDirectory")));
    QVERIFY(staging.contains(QStringLiteral("msvcp140.dll")));
    QVERIFY(staging.contains(QStringLiteral("vcruntime140.dll")));
    QVERIFY(staging.contains(QStringLiteral("vcruntime140_1.dll")));
    QVERIFY(staging.contains(QStringLiteral("qoffscreen.dll")));
}

void UiReleaseContractTests::curveTransitionSmoothingUsesThemedSettingsAndProfileControls()
{
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString profiles = sourceFile(QStringLiteral("qml/ProfileLibrary.qml"));
    const QString backendHeader = sourceFile(QStringLiteral("src/app_backend.h"));

    QVERIFY(settings.contains(QStringLiteral("ADVANCED CONTROLS")));
    QVERIFY(settings.contains(QStringLiteral("CURVE TRANSITION SMOOTHING")));
    QVERIFY(settings.contains(QStringLiteral("backend.setCurveTransitionSmoothingEnabled")));
    QVERIFY(settings.contains(QStringLiteral("backend.setCurveTransitionDurationMs")));
    QVERIFY(settings.contains(QStringLiteral("SettingRow")));
    QVERIFY(profiles.contains(QStringLiteral("Override for This Profile")));
    QVERIFY(profiles.contains(QStringLiteral("backendObject.setProfileCurveTransitionSmoothingOverride")));
    QVERIFY(profiles.contains(QStringLiteral("backendObject.setProfileCurveTransitionSmoothingEnabled")));
    QVERIFY(profiles.contains(QStringLiteral("backendObject.setProfileCurveTransitionDurationMs")));
    QVERIFY(profiles.contains(QStringLiteral("Card {")));
    QVERIFY(backendHeader.contains(QStringLiteral("Q_PROPERTY(bool curveTransitionSmoothingEnabled")));
}

QTEST_MAIN(UiReleaseContractTests)
#include "ui_release_contract_tests.moc"
