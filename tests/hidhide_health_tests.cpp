#include "hidhide_health_service.h"

#include <QTest>

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
};

void HidHideHealthTests::allHealthyIsReady()
{
    HidHideHealthService service;
    const HidHideHealthSnapshot snapshot = service.inspect(healthyContext(), HidHideHealthScanDepth::Essential);
    QCOMPARE(snapshot.overallState, HidHideHealthState::Ready);
    QCOMPARE(snapshot.dimensions.size(), 9);
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
    HidHideHealthService service([observations](std::atomic_bool *) { return observations; });
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
    HidHideHealthService service([observations](std::atomic_bool *) { return observations; });
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
    QCOMPARE(dimension(snapshot, QStringLiteral("restart-state"))->repairability,
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

QTEST_GUILESS_MAIN(HidHideHealthTests)
#include "hidhide_health_tests.moc"
