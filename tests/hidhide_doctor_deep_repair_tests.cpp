#include "doctor_deep_repair.h"
#include "doctor_fixtures.h"
#include "doctor_repair_helper_protocol.h"

#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace hotas::doctor;

namespace {

DiagnosticRunOutcome runFixture(const QString &name, bool explicitUpgrade = false)
{
    FixtureDiagnosticProvider provider(createDevelopmentFixture(name));
    return DoctorDiagnosticEngine().run(provider, nullptr, {}, explicitUpgrade);
}

RepairPlan deepPlan(const QString &fixture, bool explicitUpgrade = false)
{
    DiagnosticRunOutcome outcome = runFixture(fixture, explicitUpgrade);
    const RepairPlanProposal proposal = RepairPlanner().propose(outcome.session, outcome.snapshot, true, explicitUpgrade);
    if (proposal.status != RepairProposalStatus::AvailableForOwnerLab) return {};
    return proposal.plan;
}

DoctorEnvironment deepEnvironment()
{
    DoctorEnvironment environment = fixtures::windows11X64Healthy();
    environment.hidhide.provider = QStringLiteral("fixture-official-nefarius");
    environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::RecoverySupported;
    return environment;
}

} // namespace

class HidHideDoctorDeepRepairTests final : public QObject {
    Q_OBJECT
private slots:
    void approvedCatalogRejectsAllIdentityDrift();
    void deepPlansRemainClassSeparated();
    void packagePlanRequiresExactTypedOperations();
    void rebootContinuationObservesBeforeCompletion();
    void reportContainsPhase4SchemaAndCatalog();
    void fixtureMatrixIsBroadAndUnique();
};

void HidHideDoctorDeepRepairTests::approvedCatalogRejectsAllIdentityDrift()
{
    const std::optional<ApprovedPackage> package = ApprovedPackageCatalog::find(QStringLiteral("HD-PKG-FIXTURE-OFFICIAL-1.5.230.0-X64"));
    QVERIFY(package.has_value());
    const DoctorEnvironment environment = deepEnvironment();
    const auto valid = ApprovedPackageCatalog::validate(*package, environment, package->expectedSha256,
        package->signerIdentity, package->version, package->architecture, package->source);
    QVERIFY2(valid.valid, qPrintable(valid.reason));
    QVERIFY(!ApprovedPackageCatalog::validate(*package, environment, QString(64, QLatin1Char('0')),
        package->signerIdentity, package->version, package->architecture, package->source).valid);
    QVERIFY(!ApprovedPackageCatalog::validate(*package, environment, package->expectedSha256,
        QStringLiteral("Unexpected signer"), package->version, package->architecture, package->source).valid);
    QVERIFY(!ApprovedPackageCatalog::validate(*package, environment, package->expectedSha256,
        package->signerIdentity, QStringLiteral("9.9.9.9"), package->architecture, package->source).valid);
    QVERIFY(!ApprovedPackageCatalog::validate(*package, environment, package->expectedSha256,
        package->signerIdentity, package->version, CpuArchitecture::Arm64, package->source).valid);
    QVERIFY(!ApprovedPackageCatalog::validate(*package, environment, package->expectedSha256,
        package->signerIdentity, package->version, package->architecture, QStringLiteral("https://untrusted.example/package.msi")).valid);
}

void HidHideDoctorDeepRepairTests::deepPlansRemainClassSeparated()
{
    const RepairPlan r3 = deepPlan(QStringLiteral("Deep Incomplete Driver Replacement"));
    QVERIFY(r3.id.isValid());
    QCOMPARE(r3.riskClass, RepairRiskClass::R3Package);
    QCOMPARE(r3.recipeId.value(), QStringLiteral("HD-R3-COMPLETE-DRIVER-REPLACEMENT"));
    QCOMPARE(r3.maximumReboots, 1);
    QVERIFY(!r3.deepRepair.value(QStringLiteral("package")).toObject().isEmpty());

    const RepairPlan r2 = deepPlan(QStringLiteral("Missing HidHide Service Registration"));
    QVERIFY(r2.id.isValid());
    QCOMPARE(r2.riskClass, RepairRiskClass::R2Component);
    QCOMPARE(r2.operations.size(), 1);
    QCOMPARE(r2.operations.first().kind, RepairOperationKind::RepairExactServiceConfiguration);

    const RepairPlan r4 = deepPlan(QStringLiteral("Approved Upgrade"), true);
    QVERIFY(r4.id.isValid());
    QCOMPARE(r4.riskClass, RepairRiskClass::R4ApprovedUpgrade);
    QCOMPARE(r4.deepRepair.value(QStringLiteral("package")).toObject().value(QStringLiteral("version")).toString(), QStringLiteral("1.5.240.0"));

    const RepairPlan r5 = deepPlan(QStringLiteral("Recovery"));
    QVERIFY(r5.id.isValid());
    QCOMPARE(r5.riskClass, RepairRiskClass::R5Recovery);
    QVERIFY(std::any_of(r5.operations.cbegin(), r5.operations.cend(), [](const RepairOperation &operation) {
        return operation.kind == RepairOperationKind::RemoveSpecificInactiveHidHidePackage;
    }));

    DiagnosticRunOutcome normal = runFixture(QStringLiteral("Client Driver Mismatch"));
    const RepairPlanProposal normalProposal = RepairPlanner().propose(normal.session, normal.snapshot, false);
    QCOMPARE(normalProposal.status, RepairProposalStatus::NotApplicable);
}

void HidHideDoctorDeepRepairTests::packagePlanRequiresExactTypedOperations()
{
    DiagnosticRunOutcome outcome = runFixture(QStringLiteral("Deep Incomplete Driver Replacement"));
    RepairPlan plan = RepairPlanner().propose(outcome.session, outcome.snapshot, true).plan;
    plan.authorization = RepairAuthorization::OwnerLabAuthorized;
    plan.integrityDigest = RepairHelperContract::seal(plan);
    QString reason;
    QVERIFY2(RepairHelperContract::validate(plan, outcome.snapshot.environment, true, &reason), qPrintable(reason));

    RepairHelperRequest request;
    request.transactionId = RepairTransactionId(QStringLiteral("REPAIR-TX-DEEP-HELPER-001"));
    request.plan = plan;
    request.doctorBuildId = QStringLiteral("DEEP-TEST-BUILD");
    request.helperBuildId = QStringLiteral("DEEP-TEST-BUILD");
    request.nonce = QString(32, QLatin1Char('A'));
    request.expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);
    request.requestDigest = RepairHelperProtocol::seal(request);
    QVERIFY2(RepairHelperProtocol::validate(request, outcome.snapshot.environment, request.doctorBuildId, request.nonce, &reason), qPrintable(reason));

    QJsonObject tamperedPackage = request.plan.deepRepair.value(QStringLiteral("package")).toObject();
    tamperedPackage.insert(QStringLiteral("path"), QStringLiteral("C:/arbitrary.msi"));
    request.plan.deepRepair.insert(QStringLiteral("package"), tamperedPackage);
    request.plan.integrityDigest = RepairHelperContract::seal(request.plan);
    request.requestDigest = RepairHelperProtocol::seal(request);
    QVERIFY(!RepairHelperProtocol::validate(request, outcome.snapshot.environment, request.doctorBuildId, request.nonce, &reason));
}

void HidHideDoctorDeepRepairTests::rebootContinuationObservesBeforeCompletion()
{
    DiagnosticRunOutcome before = runFixture(QStringLiteral("Deep Incomplete Driver Replacement"));
    const RepairPlanProposal proposal = RepairPlanner().propose(before.session, before.snapshot, true);
    QCOMPARE(proposal.status, RepairProposalStatus::AvailableForOwnerLab);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    RepairJournalStore journal(temporary.path());
    RepairExecutionResult staged = RepairTransactionCoordinator().dryRun(proposal, before.snapshot.environment, before.session.id(), journal,
        RepairTransactionId(QStringLiteral("REPAIR-TX-DEEP-REBOOT-001")));
    QCOMPARE(staged.transaction.state, RepairTransactionState::Planned);
    staged.transaction.state = RepairTransactionState::AwaitingReboot;
    staged.transaction.maximumReboots = 1;
    QVERIFY(journal.persist(staged.transaction));

    ReadOnlyDiagnosticSnapshot after = before.snapshot;
    after.environment.hidhide.driverVersion = QStringLiteral("1.5.230.0");
    after.artifacts[1].fileVersion = QStringLiteral("1.5.230.0");
    const RepairRecoveryResult resumed = RepairTransactionCoordinator().reconcileAfterReboot(staged.transaction, after, journal);
    QCOMPARE(resumed.transaction.state, RepairTransactionState::Completed);
    QVERIFY(!resumed.requiresOwnerReview);
    QCOMPARE(resumed.transaction.continuationState.value(QStringLiteral("observeFirst")).toBool(), true);

    staged.transaction.rebootCount = 1;
    const RepairRecoveryResult bounded = RepairTransactionCoordinator().reconcileAfterReboot(staged.transaction, before.snapshot, journal);
    QCOMPARE(bounded.transaction.state, RepairTransactionState::RecoveryRequired);
    QVERIFY(bounded.requiresOwnerReview);
}

void HidHideDoctorDeepRepairTests::reportContainsPhase4SchemaAndCatalog()
{
    DiagnosticRunOutcome outcome = runFixture(QStringLiteral("Deep Incomplete Driver Replacement"));
    const QJsonDocument report = QJsonDocument::fromJson(DoctorDiagnosticEngine::serializeJson(outcome, true));
    QVERIFY(report.isObject());
    QCOMPARE(report.object().value(QStringLiteral("schemaVersion")).toInt(), 5);
    QVERIFY(report.object().value(QStringLiteral("approvedPackageCatalog")).toArray().size() >= 2);
    QCOMPARE(report.object().value(QStringLiteral("repairProposal")).toObject().value(QStringLiteral("status")).toString(),
        QStringLiteral("REPAIR IDENTIFIED — NOT FIELD QUALIFIED"));
    QVERIFY(report.object().value(QStringLiteral("repairPlan")).toObject().value(QStringLiteral("deepRepair")).isObject());
}

void HidHideDoctorDeepRepairTests::fixtureMatrixIsBroadAndUnique()
{
    const QStringList names = developmentFixtureNames();
    QVERIFY(names.size() >= 40);
    QCOMPARE(names.size(), QSet<QString>(names.cbegin(), names.cend()).size());
    for (const QString &name : names) {
        QString label;
        const ReadOnlyDiagnosticSnapshot snapshot = createDevelopmentFixture(name, &label);
        QVERIFY2(!label.isEmpty(), qPrintable(name));
        QVERIFY(!snapshot.environment.platform.windowsEdition.isEmpty());
    }
}

QTEST_MAIN(HidHideDoctorDeepRepairTests)
#include "hidhide_doctor_deep_repair_tests.moc"
