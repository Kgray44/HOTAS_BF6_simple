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
    void controllerSetupRetainsItsExplicitTargetAndSuccessfulRepairPersistsIt();
    void sharedSettingsKeepOfflineControllersAndControlsVisuallyExplicit();
    void controllerPresentationIsCachedAndTelemetryIsIsolated();
    void presentationLifecycleSleepsOnlyTheGuiControlPlane();
    void curveEditorUsesSelectedAxisTelemetryAndExplicitPaintContracts();
    void profileOverflowMenuUsesThemedControlContract();
    void reliabilityCleanupUsesRequiredCapacityAndStableAutomationRows();
    void virtualOutputLayoutsAreExactAndTelemetryStaysTruthful();
    void inputLearningAndLiveNameDraftsStayOnControlPlane();
    void buttonLearningIsDestinationFirstAndCardsShowLiveSignalFlow();
    void installerUpgradeAcceptanceTracksSchema20();
    void curveTransitionSmoothingUsesThemedSettingsAndProfileControls();
    void profileLibraryPortabilityIsSharedAndThemed();
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
    QVERIFY(main.contains(QStringLiteral("onCurrentThemeChanged")));
    QVERIFY(main.contains(QStringLiteral("backend.setTrayTheme")));
}

void UiReleaseContractTests::newDeviceSetupExplicitlyAcquiresThenVerifies()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    QVERIFY(backend.contains(QStringLiteral("startExplicitNewControllerVerification(target->directInputId")));
    QVERIFY(backend.contains(QStringLiteral("m_worker.selectPhysicalController(directInputId)")));
    QVERIFY(backend.contains(QStringLiteral("verifyHotasSetup();")));
    QVERIFY(backend.contains(QStringLiteral("New controller detected:")));
}

void UiReleaseContractTests::controllerSetupRetainsItsExplicitTargetAndSuccessfulRepairPersistsIt()
{
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    QVERIFY(header.contains(QStringLiteral("controllerSetupRequested(const QStringList &targetDirectInputIds)")));
    QVERIFY(backend.contains(QStringLiteral("emit controllerSetupRequested(newlyDiscoveredUnverifiedIds)")));
    QVERIFY(backend.contains(QStringLiteral("emit controllerSetupRequested({arrivalId})")));
    QVERIFY(backend.contains(QStringLiteral("commit the controller now rather than requiring Verify Again")));
    QVERIFY(backend.contains(QStringLiteral("verifiedRequirements.buttons = verifiedPlan.requirements.buttons")));
}

void UiReleaseContractTests::sharedSettingsKeepOfflineControllersAndControlsVisuallyExplicit()
{
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    QVERIFY(settings.contains(QStringLiteral("NO CONTROLLERS CONNECTED")));
    QVERIFY(settings.contains(QStringLiteral("SELECTED")));
    QVERIFY(settings.contains(QStringLiteral("OFFLINE")));
    QVERIFY(settings.contains(QStringLiteral("up.indicator")));
    QVERIFY(settings.contains(QStringLiteral("down.indicator")));
    QVERIFY(backend.contains(QStringLiteral("Selected · Offline · Verified")));
}

void UiReleaseContractTests::controllerPresentationIsCachedAndTelemetryIsIsolated()
{
    const QString header = sourceFile(QStringLiteral("src/app_backend.h"));
    const QString backend = sourceFile(QStringLiteral("src/app_backend.cpp"));
    const QString settings = sourceFile(QStringLiteral("qml/SettingsPage.qml"));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(int connectedControllerCount READ connectedControllerCount NOTIFY controllersChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(double inputReportsPerSecond READ inputReportsPerSecond NOTIFY telemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList axes READ axes NOTIFY inputTelemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("Q_PROPERTY(QVariantList buttons READ buttons NOTIFY buttonTelemetryChanged)")));
    QVERIFY(header.contains(QStringLiteral("void controllersChanged();")));
    QVERIFY(header.contains(QStringLiteral("void telemetryChanged();")));
    QVERIFY(header.contains(QStringLiteral("void buttonTelemetryChanged();")));
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
    QVERIFY(settings.contains(QStringLiteral("backend.connectedControllerCount")));
    QVERIFY(!settings.contains(QStringLiteral("backend.controllers[")));
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
    QVERIFY(readiness.contains(QStringLiteral("exact descriptor")));
    QVERIFY(readiness.contains(QStringLiteral("applyManagedOutputVisibility")));
    QVERIFY(readiness.contains(QStringLiteral("validateManagedVirtualOutputIdentity")));
    QVERIFY(readiness.contains(QStringLiteral("VID_1234&PID_BEAD")));
    QVERIFY(worker.contains(QStringLiteral("outputLayoutAxes")));
    QVERIFY(worker.contains(QStringLiteral("&& outputLayoutAxes[static_cast<size_t>(target)]")));
    QVERIFY(settings.contains(QStringLiteral("Virtual Outputs")));
    QVERIFY(settings.contains(QStringLiteral("CREATE 5-AXIS OUTPUT")));
    QVERIFY(settings.contains(QStringLiteral("PREPARE VISIBILITY")));
    QVERIFY(settings.contains(QStringLiteral("already-open controller handle")));
    for (const QString &page : {standard, legacy}) {
        QVERIFY(page.contains(QStringLiteral("NOT ROUTED")));
        QVERIFY(page.contains(QStringLiteral("UNMAPPED VJOY AXES PARKED")));
        QVERIFY(page.contains(QStringLiteral("backend.physicalAxisCapabilitySummary")));
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
    QVERIFY(library.contains(QStringLiteral("WHEN THIS CATEGORY ACTIVATES")));
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

void UiReleaseContractTests::installerUpgradeAcceptanceTracksSchema20()
{
    const QString fixture = sourceFile(QStringLiteral("tests/upgrade_configuration_fixture.cpp"));
    const QString installer = sourceFile(QStringLiteral("scripts/verify-installer-upgrade.ps1"));
    const QString updater = sourceFile(QStringLiteral("scripts/verify-published-updater.ps1"));
    QVERIFY(fixture.contains(QStringLiteral("persist schema 20")));
    QVERIFY(fixture.contains(QStringLiteral("--assert-v20")));
    QVERIFY(!fixture.contains(QStringLiteral("--assert-v16")));
    QVERIFY(installer.contains(QStringLiteral("& $fixture --assert-v20")));
    QVERIFY(updater.contains(QStringLiteral("& $fixture --assert-v20")));
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
