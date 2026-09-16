#include "hidhide_health_service.h"

#include <QTest>
#include <QJsonDocument>

#include <algorithm>

using namespace hotas;

namespace {

HidHideHealthContext healthyContext()
{
    HidHideHealthContext context;
    context.sessionId = 42;
    context.contextKey = QStringLiteral("rig-a|hid\\vid_1234|output-a");
    context.deviceRigId = QStringLiteral("rig-a");
    context.selectedControllerId = QStringLiteral("controller-a");
    context.mapperExecutable = QStringLiteral("C:/Program Files/HOTAS BF6/HOTAS BF6.exe");
    context.installed = true;
    context.cliAvailable = true;
    context.serviceReady = true;
    context.cloakKnown = true;
    context.cloaked = true;
    context.mapperAllowlistKnown = true;
    context.mapperAllowlisted = true;
    context.hiddenDeviceListKnown = true;
    context.selectedControllerResolved = true;
    context.selectedControllerHidden = true;
    context.inspectionComplete = true;
    context.expectedPhysicalInstances = {QStringLiteral("HID\\VID_1234&PID_0001\\A")};
    context.hiddenDeviceInstances = context.expectedPhysicalInstances;
    context.managedVirtualOutputInstances = {QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY")};
    context.managedVirtualOutputInspectionKnown = true;
    return context;
}

HidHideReadObservation probe(QString operation, HidHideReadState state = HidHideReadState::Pass,
                             QString value = {})
{
    HidHideReadObservation result;
    result.id = QStringLiteral("test-") + operation;
    result.operation = std::move(operation);
    result.state = state;
    result.value = std::move(value);
    result.summary = QStringLiteral("fixture");
    return result;
}

const HidHideHealthDimension *dimension(const HidHideHealthSnapshot &snapshot, const QString &id)
{
    const auto it = std::find_if(snapshot.dimensions.cbegin(), snapshot.dimensions.cend(),
        [&id](const HidHideHealthDimension &entry) { return entry.id == id; });
    return it == snapshot.dimensions.cend() ? nullptr : &*it;
}

QList<HidHideReadObservation> directHealthy(const HidHideHealthContext &context)
{
    HidHideReadObservation whitelist = probe(QStringLiteral("GET_WHITELIST"));
    whitelist.values = {context.mapperExecutable};
    HidHideReadObservation blacklist = probe(QStringLiteral("GET_BLACKLIST"));
    blacklist.values = context.hiddenDeviceInstances;
    return {probe(QStringLiteral("OPEN_CONTROL")), probe(QStringLiteral("GET_ACTIVE"), HidHideReadState::Pass, QStringLiteral("true")),
            probe(QStringLiteral("GET_INVERSE"), HidHideReadState::Pass, QStringLiteral("false")), whitelist, blacklist};
}

bool hasFinding(const HidHideHealthSnapshot &snapshot, const QString &code)
{
    return std::any_of(snapshot.findings.cbegin(), snapshot.findings.cend(), [&code](const HidHideHealthFinding &finding) {
        return finding.code == code;
    });
}

} // namespace

class HidHideHealthTests final : public QObject {
    Q_OBJECT

private slots:
    void allHealthyIsReady();
    void absentInstallationRequiresDoctorAndOffersNoMutation();
    void cliAbsentButDirectDriverHealthyIsNotInstallationFailure();
    void oneFailedGetDoesNotPoisonOtherDirectEvidence();
    void missingMapperExemptionUsesExistingNarrowRepairClass();
    void visiblePhysicalControllerRequiresExactIdentityForRepair();
    void hiddenVirtualOutputUsesGuidedRepairClass();
    void pendingRecoveryRequiresUserAction();
    void appIssueProjectionUsesStableHidHideCodes();
    void checkingSnapshotCarriesContextIdentity();
    void directActiveContradictionIsExplicitAndNotRepairable();
    void directInverseContradictionIsExplicitAndNotRepairable();
    void directWhitelistAndBlacklistContradictionsAreExplicit();
    void directBlacklistDrivesMultiDeviceIsolation();
    void packageAndDriverMismatchRequiresDoctor();
    void packageRestartEvidenceRequiresRestart();
    void uninspectedPackageAndServiceOnlyDriverStayUnknown();
    void progressIsMonotonicAndCancellationRetainsEvidence();
    void sanitizedEvidenceOmitsPathsAndHidInstances();
};

void HidHideHealthTests::allHealthyIsReady()
{
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(healthyContext(), HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::Ready);
    QCOMPARE(snapshot.dimensions.size(), 12);
    QCOMPARE(dimension(snapshot, QStringLiteral("physical-isolation"))->state, HidHideHealthState::Ready);
    QCOMPARE(dimension(snapshot, QStringLiteral("virtual-output"))->state, HidHideHealthState::Ready);
}

void HidHideHealthTests::absentInstallationRequiresDoctorAndOffersNoMutation()
{
    HidHideHealthContext context = healthyContext();
    context.installed = false;
    context.cliAvailable = false;
    context.serviceReady = false;
    context.inspectionComplete = false;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::DoctorRecommended);
    QCOMPARE(dimension(snapshot, QStringLiteral("installation"))->repairability,
             HidHideRepairability::DoctorRequired);
    QVERIFY(!HidHideHealthService::appIssues(snapshot).first().toMap().value(QStringLiteral("automaticallyFixable")).toBool());
}

void HidHideHealthTests::cliAbsentButDirectDriverHealthyIsNotInstallationFailure()
{
    HidHideHealthContext context = healthyContext();
    context.installed = false;
    context.cliAvailable = false;
    const QList<HidHideReadObservation> observations{
        probe(QStringLiteral("OPEN_CONTROL")), probe(QStringLiteral("GET_ACTIVE")),
        probe(QStringLiteral("GET_INVERSE")), probe(QStringLiteral("GET_WHITELIST")),
        probe(QStringLiteral("GET_BLACKLIST")),
    };
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("installation"))->state, HidHideHealthState::Ready);
    QCOMPARE(dimension(snapshot, QStringLiteral("control-api"))->state, HidHideHealthState::Ready);
}

void HidHideHealthTests::oneFailedGetDoesNotPoisonOtherDirectEvidence()
{
    HidHideHealthContext context = healthyContext();
    HidHideReadObservation whitelist = probe(QStringLiteral("GET_WHITELIST"), HidHideReadState::Failed);
    whitelist.hasNativeError = true;
    whitelist.nativeError = {QStringLiteral("win32"), 0x57, QStringLiteral("GET_WHITELIST"), QStringLiteral("invalid parameter")};
    const QList<HidHideReadObservation> observations{
        probe(QStringLiteral("OPEN_CONTROL")), probe(QStringLiteral("GET_ACTIVE")),
        probe(QStringLiteral("GET_INVERSE")), whitelist, probe(QStringLiteral("GET_BLACKLIST")),
    };
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("control-api"))->state, HidHideHealthState::Ready);
    QCOMPARE(dimension(snapshot, QStringLiteral("application-access"))->state, HidHideHealthState::Unknown);
    QCOMPARE(dimension(snapshot, QStringLiteral("installation"))->state, HidHideHealthState::Ready);
}

void HidHideHealthTests::missingMapperExemptionUsesExistingNarrowRepairClass()
{
    HidHideHealthContext context = healthyContext();
    context.mapperAllowlisted = false;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::RepairAvailable);
    QCOMPARE(dimension(snapshot, QStringLiteral("application-access"))->repairability, HidHideRepairability::FixNow);
}

void HidHideHealthTests::visiblePhysicalControllerRequiresExactIdentityForRepair()
{
    HidHideHealthContext visible = healthyContext();
    visible.selectedControllerHidden = false;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(visible, HidHideHealthScanDepth::Essential);
    QCOMPARE(dimension(snapshot, QStringLiteral("physical-isolation"))->repairability, HidHideRepairability::GuidedRepair);

    visible.selectedControllerResolved = false;
    const HidHideHealthSnapshot unresolved = service.inspect(visible, HidHideHealthScanDepth::Essential);
    QCOMPARE(dimension(unresolved, QStringLiteral("physical-isolation"))->state, HidHideHealthState::Unknown);
    QVERIFY(dimension(unresolved, QStringLiteral("physical-isolation"))->repairability != HidHideRepairability::GuidedRepair);
}

void HidHideHealthTests::hiddenVirtualOutputUsesGuidedRepairClass()
{
    HidHideHealthContext context = healthyContext();
    context.managedVirtualOutputHidden = true;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::RepairAvailable);
    QCOMPARE(dimension(snapshot, QStringLiteral("virtual-output"))->repairability, HidHideRepairability::GuidedRepair);
}

void HidHideHealthTests::pendingRecoveryRequiresUserAction()
{
    HidHideHealthContext context = healthyContext();
    context.pendingReadinessRecovery = true;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::UserActionRequired);
    QCOMPARE(dimension(snapshot, QStringLiteral("hotas-recovery"))->repairability,
             HidHideRepairability::UserActionRequired);
}

void HidHideHealthTests::appIssueProjectionUsesStableHidHideCodes()
{
    HidHideHealthContext context = healthyContext();
    context.selectedControllerHidden = false;
    HidHideHealthService service;
    const QVariantList issues = HidHideHealthService::appIssues(service.inspect(context, HidHideHealthScanDepth::Essential));
    QVERIFY(!issues.isEmpty());
    const QVariantMap issue = issues.first().toMap();
    QCOMPARE(issue.value(QStringLiteral("code")).toString(), QStringLiteral("HIDHIDE_PHYSICAL_ISOLATION"));
    QCOMPARE(issue.value(QStringLiteral("category")).toString(), QStringLiteral("HidHide Health"));
    QCOMPARE(issue.value(QStringLiteral("navigationTarget")).toMap().value(QStringLiteral("section")).toString(),
             QStringLiteral("hidhide-health"));
}

void HidHideHealthTests::checkingSnapshotCarriesContextIdentity()
{
    const HidHideHealthContext context = healthyContext();
    const HidHideHealthSnapshot snapshot = HidHideHealthService::checkingSnapshot(context, HidHideHealthScanDepth::Full);
    QCOMPARE(snapshot.sessionId, context.sessionId);
    QCOMPARE(snapshot.contextKey, context.contextKey);
    QCOMPARE(snapshot.overallState, HidHideHealthState::Checking);
    QVERIFY(snapshot.inProgress);
    QVERIFY(snapshot.checksTotal > 0);
}

void HidHideHealthTests::directActiveContradictionIsExplicitAndNotRepairable()
{
    HidHideHealthContext context = healthyContext();
    context.cloaked = false;
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("cloak-state"))->state, HidHideHealthState::DoctorRecommended);
    QVERIFY(hasFinding(snapshot, QStringLiteral("HIDHIDE_DIRECT_GET_ACTIVE_CONTRADICTION")));
}

void HidHideHealthTests::directInverseContradictionIsExplicitAndNotRepairable()
{
    HidHideHealthContext context = healthyContext();
    context.inverseKnown = true;
    context.inverse = true;
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("inverse-mode"))->state, HidHideHealthState::DoctorRecommended);
    QVERIFY(hasFinding(snapshot, QStringLiteral("HIDHIDE_DIRECT_GET_INVERSE_CONTRADICTION")));
}

void HidHideHealthTests::directWhitelistAndBlacklistContradictionsAreExplicit()
{
    HidHideHealthContext context = healthyContext();
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations[3].values.clear();
    observations[4].values.clear();
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("application-access"))->state, HidHideHealthState::DoctorRecommended);
    QCOMPARE(dimension(snapshot, QStringLiteral("physical-isolation"))->state, HidHideHealthState::DoctorRecommended);
    QVERIFY(hasFinding(snapshot, QStringLiteral("HIDHIDE_DIRECT_GET_WHITELIST_CONTRADICTION")));
    QVERIFY(hasFinding(snapshot, QStringLiteral("HIDHIDE_DIRECT_GET_BLACKLIST_CONTRADICTION")));
}

void HidHideHealthTests::directBlacklistDrivesMultiDeviceIsolation()
{
    HidHideHealthContext context = healthyContext();
    HidHidePhysicalDeviceHealth stick{QStringLiteral("stick"), QStringLiteral("Stick"), true, true,
        {QStringLiteral("HID\\VID_1000&PID_0001\\STICK")}, true};
    HidHidePhysicalDeviceHealth throttle{QStringLiteral("throttle"), QStringLiteral("Throttle"), true, true,
        {QStringLiteral("HID\\VID_1000&PID_0002\\THROTTLE")}, true};
    HidHidePhysicalDeviceHealth pedals{QStringLiteral("pedals"), QStringLiteral("Pedals"), true, true,
        {QStringLiteral("HID\\VID_1000&PID_0003\\PEDALS")}, true};
    context.physicalDevices = {stick, throttle, pedals};
    context.expectedPhysicalInstances = stick.exactCurrentHidInstances;
    HidHideReadObservation blacklist = probe(QStringLiteral("GET_BLACKLIST"));
    blacklist.values = {stick.exactCurrentHidInstances.front(), throttle.exactCurrentHidInstances.front()};
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations.back() = blacklist;
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(snapshot.physicalDevices.size(), 3);
    QCOMPARE(snapshot.physicalDevices.at(2).state, HidHideHealthState::RepairAvailable);
    QCOMPARE(dimension(snapshot, QStringLiteral("physical-isolation"))->state, HidHideHealthState::RepairAvailable);
}

void HidHideHealthTests::packageAndDriverMismatchRequiresDoctor()
{
    HidHideHealthContext context = healthyContext();
    context.packageEvidenceInspected = true;
    context.clientVersion = QStringLiteral("1.2.3.4");
    context.cliVersion = QStringLiteral("1.2.3.4");
    context.loadedDriverVersion = QStringLiteral("1.0.0.0");
    context.driverStorePackageVersion = QStringLiteral("2.0.0.0");
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-version"))->state, HidHideHealthState::Unknown);
    QCOMPARE(dimension(snapshot, QStringLiteral("kernel-driver"))->state, HidHideHealthState::DoctorRecommended);
}

void HidHideHealthTests::packageRestartEvidenceRequiresRestart()
{
    HidHideHealthContext context = healthyContext();
    context.packageEvidenceInspected = true;
    context.pendingPackageRestartKnown = true;
    context.pendingPackageRestart = true;
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-restart-state"))->state, HidHideHealthState::RestartRequired);
}

void HidHideHealthTests::uninspectedPackageAndServiceOnlyDriverStayUnknown()
{
    HidHideHealthContext context = healthyContext();
    context.packageEvidenceInspected = false;
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Essential);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-version"))->state, HidHideHealthState::Unknown);
    QCOMPARE(dimension(snapshot, QStringLiteral("kernel-driver"))->state, HidHideHealthState::Unknown);
    QCOMPARE(dimension(snapshot, QStringLiteral("control-api"))->state, HidHideHealthState::Unknown);
}

void HidHideHealthTests::progressIsMonotonicAndCancellationRetainsEvidence()
{
    HidHideHealthContext context = healthyContext();
    QList<int> progress;
    std::atomic_bool cancelled{false};
    HidHideHealthService service([](std::atomic_bool *token, HidHideReadOnlyProtocol::ObservationCallback callback) {
        HidHideReadObservation open = probe(QStringLiteral("OPEN_CONTROL"));
        callback(open);
        token->store(true);
        return QList<HidHideReadObservation>{open};
    });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full, &cancelled,
        [&progress](const HidHideHealthSnapshot &partial) { progress.append(partial.checksCompleted); });
    QVERIFY(snapshot.cancelled);
    QVERIFY(!snapshot.checks.isEmpty());
    QVERIFY(std::is_sorted(progress.cbegin(), progress.cend()));
    QVERIFY(std::all_of(progress.cbegin(), progress.cend(), [&snapshot](int value) { return value <= snapshot.checksTotal; }));
}

void HidHideHealthTests::sanitizedEvidenceOmitsPathsAndHidInstances()
{
    HidHideHealthContext context = healthyContext();
    HidHideReadObservation whitelist = probe(QStringLiteral("GET_WHITELIST"));
    whitelist.values = {context.mapperExecutable, QStringLiteral("C:/Users/private/Other.exe")};
    HidHideReadObservation blacklist = probe(QStringLiteral("GET_BLACKLIST"));
    blacklist.values = context.expectedPhysicalInstances;
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations[3] = whitelist;
    observations[4] = blacklist;
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    const QByteArray json = QJsonDocument::fromVariant(HidHideHealthService::sanitizedEvidence(snapshot)).toJson();
    QVERIFY(!json.contains("C:/Users/private/Other.exe"));
    QVERIFY(!json.contains("HID\\VID_1234&PID_0001\\A"));
    QVERIFY(json.contains("entryCount"));
}

QTEST_GUILESS_MAIN(HidHideHealthTests)
#include "hidhide_health_tests.moc"
