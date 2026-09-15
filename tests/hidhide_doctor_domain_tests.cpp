#include "doctor_catalog.h"
#include "doctor_repair_contract.h"
#include "doctor_session_view_model.h"

#include <QtTest>

using namespace hotas::doctor;

namespace {
DoctorCheckDefinition definition(const QString &id, int weight = 1)
{
    return {DoctorCheckId(id), id, DoctorPhase::SystemEnvironment, {}, {weight}, 1000,
        CatalogImplementationState::Deferred};
}

DiagnosticPlanItem item(const QString &step, const QString &check, int weight)
{
    return {DoctorStepId(step), definition(check, weight)};
}

RepairPlan fieldQualifiedPlan()
{
    RepairPlan plan;
    plan.id = RepairPlanId(QStringLiteral("REPAIR-PLAN-001"));
    plan.sessionId = DoctorSessionId(QStringLiteral("SESSION-001"));
    plan.riskClass = RepairRiskClass::R1Configuration;
    plan.qualification = RepairQualificationLevel::FieldQualified;
    plan.authorization = RepairAuthorization::UserAuthorized;
    plan.preconditions.append({QStringLiteral("provider"), QStringLiteral("official-nefarius")});
    RepairOperation operation;
    operation.id = DoctorOperationId(QStringLiteral("REPAIR-OP-001"));
    operation.kind = RepairOperationKind::AddWhitelistEntry;
    operation.targetKind = RepairTargetKind::WhitelistEntry;
    operation.targetIdentity = QStringLiteral("HOTAS-BF6-EXE");
    operation.requestedValue = QStringLiteral("present");
    operation.verification.append({DoctorCheckId(QStringLiteral("HD-CFG-008")), QStringLiteral("present")});
    plan.operations.append(operation);
    plan.integrityDigest = RepairHelperContract::seal(plan);
    return plan;
}
} // namespace

class HidHideDoctorDomainTests final : public QObject {
    Q_OBJECT
private slots:
    void stableIdsAndSessionTransitions();
    void checkStatusSeparatesObservationFromExecutionFailure();
    void evidencePreservesNativeNumericError();
    void weightedProgressAndNotApplicableBehavior();
    void cancellationAndPlanFreeze();
    void catalogRejectsDuplicatesAndUnknownPrerequisites();
    void catalogCoverageIncludesFullGoverningManifest();
    void portabilityFixturesExpressArchitectureAndFreshUserState();
    void fixtureProtocolFailuresAreIsolated();
    void repairContractRejectsUnknownAndArbitraryTargets();
    void presentationToggleKeepsOneCanonicalSession();
};

void HidHideDoctorDomainTests::stableIdsAndSessionTransitions()
{
    QVERIFY(DoctorCheckId(QStringLiteral("HD-SYS-001")).isValid());
    QVERIFY(!DoctorCheckId(QStringLiteral("hidhide-check")).isValid());
    QVERIFY(DoctorSessionId(QStringLiteral("SESSION-ONE")).isValid());
    DoctorSession session(DoctorSessionId(QStringLiteral("SESSION-UNIT")));
    QVERIFY(session.transitionTo(DoctorSessionState::Diagnosing));
    QVERIFY(session.transitionTo(DoctorSessionState::Analyzing));
    QVERIFY(session.transitionTo(DoctorSessionState::DiagnosisComplete));
    QVERIFY(!session.transitionTo(DoctorSessionState::Repairing));
    QVERIFY(session.transitionTo(DoctorSessionState::FailedSafely));
}

void HidHideDoctorDomainTests::checkStatusSeparatesObservationFromExecutionFailure()
{
    QVERIFY(isTerminal(DoctorCheckStatus::Failed));
    QVERIFY(!isExecutionFailure(DoctorCheckStatus::Failed));
    QVERIFY(isExecutionFailure(DoctorCheckStatus::PermissionLimited));
    QVERIFY(isExecutionFailure(DoctorCheckStatus::Blocked));
    QVERIFY(isExecutionFailure(DoctorCheckStatus::TimedOut));
    QCOMPARE(displayName(DoctorCheckStatus::NotApplicable), QStringLiteral("Not Applicable"));
}

void HidHideDoctorDomainTests::evidencePreservesNativeNumericError()
{
    DoctorSession session;
    EvidenceRecord evidence;
    evidence.checkId = DoctorCheckId(QStringLiteral("HD-API-005"));
    evidence.nativeError = NativeError{NativeErrorDomain::Win32, 0x57, QStringLiteral("ERROR_INVALID_PARAMETER"), QStringLiteral("The parameter is incorrect.")};
    session.appendEvidence(evidence);
    QCOMPARE(session.evidence().size(), 1);
    QVERIFY(session.evidence().front().id.isValid());
    QVERIFY(session.evidence().front().nativeError.has_value());
    QCOMPARE(session.evidence().front().nativeError->code, qint64(0x57));
    QCOMPARE(session.evidence().front().nativeError->symbolicName, QStringLiteral("ERROR_INVALID_PARAMETER"));
}

void HidHideDoctorDomainTests::weightedProgressAndNotApplicableBehavior()
{
    DiagnosticPlan plan;
    QVERIFY(plan.addItem(item(QStringLiteral("STEP-001"), QStringLiteral("HD-SYS-001"), 2)));
    QVERIFY(plan.addItem(item(QStringLiteral("STEP-002"), QStringLiteral("HD-SYS-002"), 1)));
    QVERIFY(plan.addItem(item(QStringLiteral("STEP-003"), QStringLiteral("HD-SYS-003"), 3)));
    QVERIFY(plan.freeze());
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-001")), DoctorCheckStatus::Running));
    QVERIFY(plan.setCurrentStepProgress(DoctorStepId(QStringLiteral("STEP-001")), 50));
    QCOMPARE(plan.progress().overallPercent, 16);
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-001")), DoctorCheckStatus::Healthy));
    QCOMPARE(plan.progress().overallPercent, 33);
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-002")), DoctorCheckStatus::NotApplicable));
    QCOMPARE(plan.progress().overallPercent, 40);
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-003")), DoctorCheckStatus::Running));
    QVERIFY(plan.setCurrentStepProgress(DoctorStepId(QStringLiteral("STEP-003")), 50));
    QCOMPARE(plan.progress().overallPercent, 70);
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-003")), DoctorCheckStatus::Healthy));
    QCOMPARE(plan.progress().overallPercent, 100);
    QCOMPARE(plan.progress().completedChecks, 2);
    QCOMPARE(plan.progress().applicableChecks, 2);
}

void HidHideDoctorDomainTests::cancellationAndPlanFreeze()
{
    DiagnosticPlan plan;
    QVERIFY(plan.addItem(item(QStringLiteral("STEP-A"), QStringLiteral("HD-SYS-001"), 1)));
    QVERIFY(plan.freeze());
    QVERIFY(!plan.addItem(item(QStringLiteral("STEP-B"), QStringLiteral("HD-SYS-002"), 1)));
    QVERIFY(plan.setStatus(DoctorStepId(QStringLiteral("STEP-A")), DoctorCheckStatus::Cancelled));
    QCOMPARE(plan.progress().overallPercent, 0);
    auto extension = item(QStringLiteral("STEP-X"), QStringLiteral("HD-X-001"), 1);
    extension.check.phase = DoctorPhase::ExtendedInvestigation;
    QVERIFY(plan.appendBoundedExtension(extension));
    QVERIFY(!plan.appendBoundedExtension(extension));
}

void HidHideDoctorDomainTests::catalogRejectsDuplicatesAndUnknownPrerequisites()
{
    DoctorCatalog catalog;
    QVERIFY(catalog.registerCheck(definition(QStringLiteral("HD-SYS-001"))));
    QString reason;
    QVERIFY(!catalog.registerCheck(definition(QStringLiteral("HD-SYS-001")), &reason));
    QVERIFY(reason.contains(QStringLiteral("Duplicate")));
    DoctorCheckDefinition dependent = definition(QStringLiteral("HD-SYS-002"));
    dependent.prerequisites.append(DoctorCheckId(QStringLiteral("HD-SYS-099")));
    QVERIFY(!catalog.registerCheck(dependent, &reason));
    QVERIFY(reason.contains(QStringLiteral("Unknown prerequisite")));
}

void HidHideDoctorDomainTests::catalogCoverageIncludesFullGoverningManifest()
{
    const QStringList ids = DoctorCatalog::v11DefinedCheckIds();
    QVERIFY2(ids.size() >= 238, "The embedded v1.1 governing catalog must include its 208 baseline plus 30 portability checks.");
    QVERIFY(ids.contains(QStringLiteral("HD-API-005")));
    QVERIFY(ids.contains(QStringLiteral("HD-PORT-030")));
    DoctorCatalog catalog;
    const CatalogCoverageReport report = catalog.coverage();
    QCOMPARE(report.defined, ids.size());
    QCOMPARE(report.registered, 0);
    QCOMPARE(report.unregisteredIds.size(), ids.size());
}

void HidHideDoctorDomainTests::portabilityFixturesExpressArchitectureAndFreshUserState()
{
    const DoctorEnvironment arm64 = fixtures::windows11Arm64();
    QCOMPARE(arm64.platform.nativeArchitecture, CpuArchitecture::Arm64);
    const DoctorEnvironment mismatch = fixtures::wrongArchitecturePackage();
    QCOMPARE(mismatch.hidhide.packageArchitecture, CpuArchitecture::X64);
    QVERIFY(!mismatch.capabilities.helperArchitectureCompatible);
    const DoctorEnvironment future = fixtures::unknownFutureWindows();
    QCOMPARE(future.capabilities.highestQualifiedRepairTier, RepairCapabilityTier::DiagnosisSupported);
    const DoctorEnvironment fresh = fixtures::freshNonEnglishUser();
    QVERIFY(fresh.freshUser);
    QVERIFY(!fresh.hotasBf6Present);
    QCOMPARE(fresh.platform.localeName, QStringLiteral("de-DE"));
    const DoctorEnvironment clean = fixtures::hidHideCleanInstall();
    QVERIFY(clean.hidhide.present);
    QVERIFY(!clean.hotasBf6Present);
    const DoctorEnvironment noControllers = fixtures::zeroControllers();
    QCOMPARE(noControllers.devices.physicalControllerCount, 0);
    const DoctorEnvironment malformed = fixtures::multipleControllersWithMalformedObservation();
    QCOMPARE(malformed.devices.physicalControllerCount, 4);
    QCOMPARE(malformed.devices.malformedObservationCount, 1);
    QCOMPARE(malformed.devices.vendorLabels.size(), 4);
}

void HidHideDoctorDomainTests::fixtureProtocolFailuresAreIsolated()
{
    FixtureHidHideProtocolProvider provider;
    provider.setResult(QStringLiteral("GET_WHITELIST"), {DoctorCheckStatus::Failed, QStringLiteral("GET_WHITELIST"), {}, NativeError{NativeErrorDomain::Win32, 0x57, QStringLiteral("ERROR_INVALID_PARAMETER"), {}}, 5});
    provider.setResult(QStringLiteral("GET_ACTIVE"), {DoctorCheckStatus::Healthy, QStringLiteral("GET_ACTIVE"), QStringLiteral("true"), std::nullopt, 2});
    QCOMPARE(provider.probe(QStringLiteral("GET_WHITELIST")).status, DoctorCheckStatus::Failed);
    QCOMPARE(provider.probe(QStringLiteral("GET_ACTIVE")).status, DoctorCheckStatus::Healthy);
    QCOMPARE(provider.probe(QStringLiteral("GET_BLACKLIST")).status, DoctorCheckStatus::NotApplicable);
}

void HidHideDoctorDomainTests::repairContractRejectsUnknownAndArbitraryTargets()
{
    const DoctorEnvironment environment = fixtures::windows11X64Healthy();
    RepairPlan valid = fieldQualifiedPlan();
    QString reason;
    QVERIFY(RepairHelperContract::validate(valid, environment, &reason));

    RepairPlan unknown = valid;
    unknown.operations.front().kind = RepairOperationKind::Unknown;
    unknown.integrityDigest = RepairHelperContract::seal(unknown);
    QVERIFY(!RepairHelperContract::validate(unknown, environment, &reason));

    RepairPlan arbitrary = valid;
    arbitrary.operations.front().targetKind = RepairTargetKind::None;
    arbitrary.operations.front().targetIdentity = QStringLiteral("powershell.exe -Command arbitrary mutation");
    arbitrary.integrityDigest = RepairHelperContract::seal(arbitrary);
    QVERIFY(!RepairHelperContract::validate(arbitrary, environment, &reason));
}

void HidHideDoctorDomainTests::presentationToggleKeepsOneCanonicalSession()
{
    DoctorSession session = createPhase0FixtureSession();
    const QString id = session.id().value();
    const int planCount = session.plan().items().size();
    DoctorSessionViewModel model(session, QStringLiteral("unit-test"));
    QVERIFY(!model.commandCenter());
    model.togglePresentation();
    QVERIFY(model.commandCenter());
    QCOMPARE(model.sessionId(), id);
    QCOMPARE(session.plan().items().size(), planCount);
    QCOMPARE(model.currentStepId(), QStringLiteral("HD-API-001"));
}

QTEST_APPLESS_MAIN(HidHideDoctorDomainTests)

#include "hidhide_doctor_domain_tests.moc"
