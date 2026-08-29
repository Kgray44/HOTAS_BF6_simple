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
    void curveEditorUsesSelectedAxisTelemetryAndExplicitPaintContracts();
    void profileOverflowMenuUsesThemedControlContract();
    void installerUpgradeAcceptanceTracksSchema16();
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
    QVERIFY(header.contains(QStringLiteral("void controllersChanged();")));
    QVERIFY(header.contains(QStringLiteral("void telemetryChanged();")));
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
    QVERIFY(settings.contains(QStringLiteral("readonly property var controllerModel: backend.controllers")));
    QVERIFY(settings.contains(QStringLiteral("backend.connectedControllerCount")));
    QVERIFY(!settings.contains(QStringLiteral("backend.controllers[")));
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
    const QString standard = sourceFile(QStringLiteral("qml/Standard.qml"));
    const QString legacy = sourceFile(QStringLiteral("qml/Legacy.qml"));
    for (const QString &page : {standard, legacy}) {
        const qsizetype menuStart = page.indexOf(QStringLiteral("Menu { id: profileActionMenu"));
        QVERIFY(menuStart >= 0);
        const qsizetype menuEnd = page.indexOf(QStringLiteral("Dialog {"), menuStart);
        QVERIFY(menuEnd > menuStart);
        const QString menu = page.mid(menuStart, menuEnd - menuStart);
        QVERIFY(menu.contains(QStringLiteral("y: parent.height + 4")));
        QVERIFY(menu.contains(QStringLiteral("x: parent.width - width")));
        QVERIFY(menu.contains(QStringLiteral("implicitWidth: 174")));
        QVERIFY(menu.contains(QStringLiteral("background: Rectangle")));
        QVERIFY(menu.contains(QStringLiteral("renameProfileMenuItem")));
        QVERIFY(menu.contains(QStringLiteral("cloneProfileMenuItem")));
        QVERIFY(menu.contains(QStringLiteral("deleteProfileMenuItem")));
        QVERIFY(menu.contains(QStringLiteral("MenuSeparator")));
        QVERIFY(menu.contains(QStringLiteral("!deleteProfileMenuItem.enabled")));
        QVERIFY(menu.contains(QStringLiteral("onTriggered: backend.cloneProfile(modelData.id)")));
    }
    QVERIFY(standard.contains(QStringLiteral("theme.topGun ? theme.orange : theme.borderStrong")));
    QVERIFY(standard.contains(QStringLiteral("theme.danger")));
    QVERIFY(legacy.contains(QStringLiteral("color: \"#182a30\"")));
    QVERIFY(legacy.contains(QStringLiteral("color: \"#ca9090\"")));
    for (const QString &page : {standard, legacy}) {
        QVERIFY(page.contains(QStringLiteral("id: renameProfileDialog")));
        QVERIFY(page.contains(QStringLiteral("id: deleteProfileDialog")));
        QVERIFY(page.contains(QStringLiteral("id: renameProfileField")));
    }
}

void UiReleaseContractTests::installerUpgradeAcceptanceTracksSchema16()
{
    const QString fixture = sourceFile(QStringLiteral("tests/upgrade_configuration_fixture.cpp"));
    const QString installer = sourceFile(QStringLiteral("scripts/verify-installer-upgrade.ps1"));
    const QString updater = sourceFile(QStringLiteral("scripts/verify-published-updater.ps1"));
    QVERIFY(fixture.contains(QStringLiteral("persist schema 16")));
    QVERIFY(fixture.contains(QStringLiteral("--assert-v16")));
    QVERIFY(!fixture.contains(QStringLiteral("--assert-v15")));
    QVERIFY(installer.contains(QStringLiteral("& $fixture --assert-v16")));
    QVERIFY(updater.contains(QStringLiteral("& $fixture --assert-v16")));
}

QTEST_MAIN(UiReleaseContractTests)
#include "ui_release_contract_tests.moc"
