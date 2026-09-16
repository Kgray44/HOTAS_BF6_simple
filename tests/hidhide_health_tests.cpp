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

HidHideHealthContext fullyBoundPackageContext()
{
    HidHideHealthContext context = healthyContext();
    context.packageEvidenceInspected = true;
    context.clientVersion = QStringLiteral("1.5.0.0");
    context.cliVersion = QStringLiteral("1.5.0.0");
    context.onDiskDriverVersion = QStringLiteral("1.5.0.0");
    context.runtimeLoadedDriverVersionKnown = true;
    context.runtimeLoadedDriverVersion = QStringLiteral("1.5.0.0");
    context.driverStorePackageCandidates = {{QStringLiteral("hidhide.inf_amd64_new/hidhide.inf"), QStringLiteral("1.5.0.0")}};
    context.activeDriverPackageKnown = true;
    context.activeDriverPackageId = QStringLiteral("hidhide.inf_amd64_new/hidhide.inf");
    context.activeDriverPackageVersion = QStringLiteral("1.5.0.0");
    context.pendingPackageRestartKnown = true;
    return context;
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
    void pendingPackageUsesOnDiskAndRuntimeEvidenceWithoutConflation();
    void activeDriverStorePackageIsSelectedByExactBinding();
    void ambiguousDriverStoreCandidatesStayUnknown();
    void packageRestartEvidenceRequiresRestart();
    void uninspectedPackageAndServiceOnlyDriverStayUnknown();
    void progressIsMonotonicAndCancellationRetainsEvidence();
    void everyPartialProgressSnapshotRemainsInProgress();
    void delayedControlEndpointRetriesOnceInBackground();
    void inactiveCloakAgainstRigIntentOffersGuidedRepair();
    void inversePolicyControlsEffectiveApplicationAccess_data();
    void inversePolicyControlsEffectiveApplicationAccess();
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
    QCOMPARE(dimension(snapshot, QStringLiteral("application-access"))->repairability, HidHideRepairability::DoctorRecommended);
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

void HidHideHealthTests::pendingPackageUsesOnDiskAndRuntimeEvidenceWithoutConflation()
{
    HidHideHealthContext context = fullyBoundPackageContext();
    context.onDiskDriverVersion = QStringLiteral("1.5.0.0");
    context.runtimeLoadedDriverVersion = QStringLiteral("1.4.0.0");
    context.activeDriverPackageVersion = QStringLiteral("1.4.0.0");
    context.driverStorePackageCandidates = {{QStringLiteral("hidhide.inf_amd64_old/hidhide.inf"), QStringLiteral("1.4.0.0")},
                                             {QStringLiteral("hidhide.inf_amd64_new/hidhide.inf"), QStringLiteral("1.5.0.0")}};
    context.activeDriverPackageId = QStringLiteral("hidhide.inf_amd64_old/hidhide.inf");
    context.pendingPackageRestart = true;
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-version"))->state, HidHideHealthState::RestartRequired);
    QCOMPARE(dimension(snapshot, QStringLiteral("kernel-driver"))->state, HidHideHealthState::RestartRequired);
    QVERIFY(dimension(snapshot, QStringLiteral("package-version"))->technicalDetails.contains(QStringLiteral("on-disk System32 candidate: 1.5.0.0")));
    QVERIFY(dimension(snapshot, QStringLiteral("package-version"))->technicalDetails.contains(QStringLiteral("runtime-loaded driver: 1.4.0.0")));
}

void HidHideHealthTests::activeDriverStorePackageIsSelectedByExactBinding()
{
    HidHideHealthContext context = fullyBoundPackageContext();
    context.driverStorePackageCandidates = {{QStringLiteral("hidhide.inf_amd64_old/hidhide.inf"), QStringLiteral("1.4.0.0")},
                                             {QStringLiteral("hidhide.inf_amd64_new/hidhide.inf"), QStringLiteral("1.5.0.0")}};
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-version"))->state, HidHideHealthState::Ready);
    QCOMPARE(dimension(snapshot, QStringLiteral("kernel-driver"))->state, HidHideHealthState::Ready);
}

void HidHideHealthTests::ambiguousDriverStoreCandidatesStayUnknown()
{
    HidHideHealthContext context = fullyBoundPackageContext();
    context.activeDriverPackageKnown = false;
    context.activeDriverPackageId.clear();
    context.activeDriverPackageVersion.clear();
    context.driverStorePackageCandidates = {{QStringLiteral("hidhide.inf_amd64_old/hidhide.inf"), QStringLiteral("1.4.0.0")},
                                             {QStringLiteral("hidhide.inf_amd64_new/hidhide.inf"), QStringLiteral("1.5.0.0")}};
    const QList<HidHideReadObservation> observations = directHealthy(context);
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    QCOMPARE(dimension(snapshot, QStringLiteral("package-version"))->state, HidHideHealthState::Unknown);
    QCOMPARE(dimension(snapshot, QStringLiteral("kernel-driver"))->state, HidHideHealthState::Unknown);
    QVERIFY(dimension(snapshot, QStringLiteral("package-version"))->technicalDetails.contains(QStringLiteral("active bound package: not proven")));
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

void HidHideHealthTests::everyPartialProgressSnapshotRemainsInProgress()
{
    HidHideHealthContext context = healthyContext();
    const QList<HidHideReadObservation> observations = directHealthy(context);
    QList<HidHideHealthSnapshot> published;
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback callback) {
        for (const HidHideReadObservation &observation : observations) callback(observation);
        return observations;
    });
    const HidHideHealthSnapshot final = service.inspect(context, HidHideHealthScanDepth::Full, nullptr,
        [&published](const HidHideHealthSnapshot &partial) { published.append(partial); });
    QVERIFY(published.size() > 1);
    for (qsizetype index = 0; index < published.size() - 1; ++index) {
        QVERIFY2(published.at(index).inProgress, "every partial Full Check snapshot must remain in progress");
    }
    QVERIFY(!published.constLast().inProgress);
    QVERIFY(!final.inProgress);
    QCOMPARE(final.percentComplete, 100);
}

void HidHideHealthTests::delayedControlEndpointRetriesOnceInBackground()
{
    const HidHideHealthContext context = healthyContext();
    int calls = 0;
    QList<HidHideHealthSnapshot> published;
    HidHideHealthService service([&calls, context](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback callback) {
        ++calls;
        if (calls == 1) {
            HidHideReadObservation delayed = probe(QStringLiteral("OPEN_CONTROL"), HidHideReadState::TimedOut);
            delayed.summary = QStringLiteral("fixture control endpoint delay");
            callback(delayed);
            return QList<HidHideReadObservation>{delayed};
        }
        const QList<HidHideReadObservation> healthy = directHealthy(context);
        for (const HidHideReadObservation &entry : healthy) callback(entry);
        return healthy;
    });
    const HidHideHealthSnapshot final = service.inspect(context, HidHideHealthScanDepth::Full, nullptr,
        [&published](const HidHideHealthSnapshot &partial) { published.append(partial); });
    QCOMPARE(calls, 2);
    QVERIFY(final.responseDelayed);
    QCOMPARE(final.retryCount, 1);
    QCOMPARE(final.retryLimit, 1);
    QVERIFY(final.inspectionStartedAt.isValid());
    QVERIFY(std::any_of(published.cbegin(), published.cend(), [](const HidHideHealthSnapshot &partial) {
        return partial.responseDelayed && partial.inProgress;
    }));
    QVERIFY(!final.inProgress);
}

void HidHideHealthTests::inactiveCloakAgainstRigIntentOffersGuidedRepair()
{
    HidHideHealthContext context = healthyContext();
    context.cloakKnown = false;
    context.cloaked = false;
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations[1] = probe(QStringLiteral("GET_ACTIVE"), HidHideReadState::Pass, QStringLiteral("false"));
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    const HidHideHealthDimension *cloak = dimension(snapshot, QStringLiteral("cloak-state"));
    const HidHideHealthDimension *physical = dimension(snapshot, QStringLiteral("physical-isolation"));
    QCOMPARE(cloak->state, HidHideHealthState::RepairAvailable);
    QCOMPARE(cloak->repairability, HidHideRepairability::GuidedRepair);
    QCOMPARE(physical->state, HidHideHealthState::RepairAvailable);
    QVERIFY(!snapshot.physicalDevices.isEmpty());
    QVERIFY(snapshot.physicalDevices.front().state != HidHideHealthState::Ready);
}

void HidHideHealthTests::inversePolicyControlsEffectiveApplicationAccess_data()
{
    QTest::addColumn<bool>("inverse");
    QTest::addColumn<bool>("mapperEntryPresent");
    QTest::addColumn<HidHideHealthState>("expectedState");
    QTest::addColumn<HidHideRepairability>("expectedRepairability");
    QTest::newRow("standard-present") << false << true << HidHideHealthState::Ready << HidHideRepairability::None;
    QTest::newRow("standard-absent") << false << false << HidHideHealthState::RepairAvailable << HidHideRepairability::FixNow;
    QTest::newRow("inverse-present") << true << true << HidHideHealthState::DoctorRecommended << HidHideRepairability::DoctorRecommended;
    QTest::newRow("inverse-absent") << true << false << HidHideHealthState::Ready << HidHideRepairability::None;
}

void HidHideHealthTests::inversePolicyControlsEffectiveApplicationAccess()
{
    QFETCH(bool, inverse);
    QFETCH(bool, mapperEntryPresent);
    QFETCH(HidHideHealthState, expectedState);
    QFETCH(HidHideRepairability, expectedRepairability);
    HidHideHealthContext context = healthyContext();
    context.mapperAllowlistKnown = false;
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations[2] = probe(QStringLiteral("GET_INVERSE"), HidHideReadState::Pass,
        inverse ? QStringLiteral("true") : QStringLiteral("false"));
    observations[3].values = mapperEntryPresent ? QStringList{context.mapperExecutable} : QStringList{};
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    const HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    const HidHideHealthDimension *access = dimension(snapshot, QStringLiteral("application-access"));
    QCOMPARE(access->state, expectedState);
    QCOMPARE(access->repairability, expectedRepairability);
}

void HidHideHealthTests::sanitizedEvidenceOmitsPathsAndHidInstances()
{
    HidHideHealthContext context = healthyContext();
    HidHideReadObservation whitelist = probe(QStringLiteral("GET_WHITELIST"));
    whitelist.values = {context.mapperExecutable, QStringLiteral("C:/Users/private/Other.exe")};
    HidHideReadObservation blacklist = probe(QStringLiteral("GET_BLACKLIST"));
    blacklist.values.clear();
    blacklist.summary = QStringLiteral("HID\\VID_1234&PID_0001\\A from C:/Users/private/blacklist.txt");
    blacklist.hasNativeError = true;
    blacklist.nativeError = {QStringLiteral("win32"), 5, QStringLiteral("GET_BLACKLIST"),
        QStringLiteral("failed for HID\\VID_1234&PID_0001\\A at C:/Users/private/driver.log")};
    QList<HidHideReadObservation> observations = directHealthy(context);
    observations[3] = whitelist;
    observations[4] = blacklist;
    HidHideHealthService service([observations](std::atomic_bool *, HidHideReadOnlyProtocol::ObservationCallback) { return observations; });
    HidHideHealthSnapshot snapshot = service.inspect(context, HidHideHealthScanDepth::Full);
    const auto failingIsolation = std::find_if(snapshot.findings.begin(), snapshot.findings.end(), [](const HidHideHealthFinding &finding) {
        return finding.dimensionId == QStringLiteral("physical-isolation");
    });
    QVERIFY(failingIsolation != snapshot.findings.end());
    QVERIFY(!failingIsolation->affectedObjectIds.isEmpty());
    failingIsolation->technicalDetails = QStringLiteral("raw HID\\VID_1234&PID_0001\\A and C:/Users/private/finding.txt");
    failingIsolation->affectedObjectIds.append(QStringLiteral("HID\\VID_1234&PID_0001\\A"));
    const QByteArray json = QJsonDocument::fromVariant(HidHideHealthService::sanitizedEvidence(snapshot)).toJson();
    QVERIFY(!json.contains("C:/Users/private/Other.exe"));
    QVERIFY(!json.contains("HID\\VID_1234&PID_0001\\A"));
    QVERIFY(!json.contains("C:/Users/private/blacklist.txt"));
    QVERIFY(!json.contains("C:/Users/private/finding.txt"));
    QVERIFY(json.contains("controller-a"));
    QVERIFY(json.contains("entryCount"));
}

QTEST_GUILESS_MAIN(HidHideHealthTests)
#include "hidhide_health_tests.moc"
