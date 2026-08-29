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

QTEST_MAIN(UiReleaseContractTests)
#include "ui_release_contract_tests.moc"
