#include "doctor_catalog.h"
#include "doctor_diagnostics.h"
#include "doctor_fixtures.h"
#include "doctor_knowledge.h"
#include "doctor_repair_contract.h"
#include "doctor_session_view_model.h"

#include <QtTest>

#include <QFile>
#include <QJsonDocument>

#include <functional>

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

class SnapshotProvider final : public IReadOnlyDiagnosticProvider {
public:
    explicit SnapshotProvider(ReadOnlyDiagnosticSnapshot snapshot) : m_snapshot(std::move(snapshot)) {}
    ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *, ObservationProgress) override { return m_snapshot; }
private:
    ReadOnlyDiagnosticSnapshot m_snapshot;
};

class ProgressSnapshotProvider final : public IReadOnlyDiagnosticProvider {
public:
    explicit ProgressSnapshotProvider(ReadOnlyDiagnosticSnapshot snapshot, bool cancel = false)
        : m_snapshot(std::move(snapshot)), m_cancel(cancel) {}
    ReadOnlyDiagnosticSnapshot observe(std::atomic_bool *cancelled, ObservationProgress progress) override
    {
        if (progress) {
            progress(DoctorCheckId(QStringLiteral("HD-SYS-001")), 20);
            progress(DoctorCheckId(QStringLiteral("HD-SYS-001")), 100);
        }
        if (m_cancel && cancelled) cancelled->store(true);
        return m_snapshot;
    }
private:
    ReadOnlyDiagnosticSnapshot m_snapshot;
    bool m_cancel = false;
};

ReadOnlyDiagnosticSnapshot healthyFixtureSnapshot()
{
    ReadOnlyDiagnosticSnapshot snapshot;
    snapshot.environment = fixtures::windows11X64Healthy();
    snapshot.environment.hidhide.present = true;
    snapshot.environment.hidhide.clientVersion = QStringLiteral("1.5.230.0");
    snapshot.environment.hidhide.driverVersion = QStringLiteral("1.5.230.0");
    snapshot.service = {true, QStringLiteral("HidHide"), QStringLiteral("HidHide"), QStringLiteral("system32\\drivers\\HidHide.sys"), QStringLiteral("system"), QStringLiteral("running"), {}, std::nullopt};
    snapshot.artifacts = {
        {DoctorArtifactKind::ClientExecutable, QStringLiteral("client"), QStringLiteral("C:\\Program Files\\HidHide\\HidHideClient.exe"), true, 1, {}, QStringLiteral("1.5.230.0"), {}, CpuArchitecture::X64, QStringLiteral("a"), SignatureTrustState::Trusted, {}, std::nullopt},
        {DoctorArtifactKind::DriverBinary, QStringLiteral("driver"), QStringLiteral("C:\\Windows\\System32\\drivers\\HidHide.sys"), true, 1, {}, QStringLiteral("1.5.230.0"), {}, CpuArchitecture::X64, QStringLiteral("b"), SignatureTrustState::Trusted, {}, std::nullopt}};
    snapshot.protocol = {
        {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Healthy, QStringLiteral("open"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_ACTIVE"), DoctorCheckStatus::Healthy, QStringLiteral("true"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_INVERSE"), DoctorCheckStatus::Healthy, QStringLiteral("false"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Healthy, QStringLiteral("4 bytes"), {}, std::nullopt, 1, true},
        {QStringLiteral("GET_WHITELIST"), DoctorCheckStatus::Healthy, QStringLiteral("1 entries"), {QStringLiteral("C:\\HOTAS BF6\\HOTAS BF6.exe")}, std::nullopt, 1, false},
        {QStringLiteral("GET_BLACKLIST_SIZE"), DoctorCheckStatus::Healthy, QStringLiteral("4 bytes"), {}, std::nullopt, 1, true},
        {QStringLiteral("GET_BLACKLIST"), DoctorCheckStatus::Healthy, QStringLiteral("1 entries"), {QStringLiteral("HID\\VID_1234")}, std::nullopt, 1, false}};
    snapshot.devices = {{QStringLiteral("HID\\VID_1234"), {}, QStringLiteral("fixture controller"), QStringLiteral("Fixture"), QStringLiteral("HIDClass"), {QStringLiteral("HID\\VID_1234&PID_0001")}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::PhysicalGamingInput, {}, std::nullopt}};
    return snapshot;
}

const DoctorCheckResult *resultFor(const DoctorSession &session, const QString &id)
{
    for (const DoctorCheckResult &result : session.checkResults()) {
        if (result.checkId.value() == id) return &result;
    }
    return nullptr;
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
    void phaseOneEngineKeepsProtocolFailuresIndependentAndExact();
    void phaseOneDeviceMetadataMapsToCatalogChecks();
    void phaseOneReportRedactsSensitiveObservationValues();
    void phaseOneFixtureMatrixUsesOneEngine();
    void phaseOneEvidenceFuzzRemainsBoundedAndSerializable();
    void phaseOneProgressCancellationAndRerunAreCoherent();
    void phaseOneProductionProviderHasNoMutationSurface();
    void phaseTwoDiagnosisFixtureMatrixIsDeterministicAndReadOnly();
    void phaseTwoReportCarriesFindingsDiagnosesAndKnowledgeVersion();
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
    const bool originalPresentation = model.commandCenter();
    model.togglePresentation();
    QCOMPARE(model.commandCenter(), !originalPresentation);
    QCOMPARE(model.sessionId(), id);
    QCOMPARE(session.plan().items().size(), planCount);
    QCOMPARE(model.currentStepId(), QStringLiteral("HD-API-001"));
}

void HidHideDoctorDomainTests::phaseOneEngineKeepsProtocolFailuresIndependentAndExact()
{
    ReadOnlyDiagnosticSnapshot snapshot;
    snapshot.environment = fixtures::windows11X64Healthy();
    snapshot.protocol = {
        {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Healthy, QStringLiteral("read-only open"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_ACTIVE"), DoctorCheckStatus::Healthy, QStringLiteral("true"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_INVERSE"), DoctorCheckStatus::Healthy, QStringLiteral("false"), {}, std::nullopt, 1, false},
        {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 0x57,
            QStringLiteral("ERROR_INVALID_PARAMETER"), QStringLiteral("invalid parameter")}, 3, true},
        {QStringLiteral("GET_BLACKLIST_SIZE"), DoctorCheckStatus::Healthy, QStringLiteral("64 bytes"), {}, std::nullopt, 1, true},
        {QStringLiteral("GET_BLACKLIST"), DoctorCheckStatus::Healthy, QStringLiteral("1 entries"), {QStringLiteral("HID\\fixture")}, std::nullopt, 1, false}};
    snapshot.devices = {{QStringLiteral("HID\\A"), {}, QStringLiteral("Healthy device"), {}, {}, {}, {}, {}, {}, {}, 0, 0, true,
        DeviceClassification::PhysicalGamingInput, {}, std::nullopt},
        {QStringLiteral("HID\\B"), {}, QStringLiteral("Malformed property device"), {}, {}, {}, {}, {}, {}, {}, 0, 0, true,
        DeviceClassification::PhysicalGamingInput, {QStringLiteral("SPDRP_HARDWAREID:13")}, std::nullopt}};
    SnapshotProvider provider(snapshot);
    DoctorDiagnosticEngine engine;
    const DiagnosticRunOutcome outcome = engine.run(provider);
    QCOMPARE(outcome.session.checkResults().size(), DoctorCatalog::v11DefinedCheckIds().size());
    const DoctorCheckResult *whitelist = resultFor(outcome.session, QStringLiteral("HD-API-004"));
    QVERIFY(whitelist);
    QCOMPARE(whitelist->status, DoctorCheckStatus::Failed);
    QVERIFY(whitelist->nativeError.has_value());
    QCOMPARE(whitelist->nativeError->code, qint64(0x57));
    const DoctorCheckResult *active = resultFor(outcome.session, QStringLiteral("HD-API-002"));
    QVERIFY(active);
    QCOMPARE(active->status, DoctorCheckStatus::Healthy);
    const DoctorCheckResult *devices = resultFor(outcome.session, QStringLiteral("HD-DEV-003"));
    QVERIFY(devices);
    QCOMPARE(devices->status, DoctorCheckStatus::Warning);
}

void HidHideDoctorDomainTests::phaseOneDeviceMetadataMapsToCatalogChecks()
{
    ReadOnlyDiagnosticSnapshot snapshot = healthyFixtureSnapshot();
    DeviceObservation &device = snapshot.devices.front();
    device.interfacePaths = {QStringLiteral("\\\\?\\hid#fixture")};
    device.usagePage = 0x01;
    device.usage = 0x04;
    device.containerId = QStringLiteral("{11111111-2222-3333-4444-555555555555}");
    device.driverProvider = QStringLiteral("Fixture Driver Provider");
    device.driverVersion = QStringLiteral("1.2.3.4");

    SnapshotProvider provider(snapshot);
    DoctorDiagnosticEngine engine;
    const DoctorSession session = engine.run(provider).session;
    const DoctorCheckResult *usage = resultFor(session, QStringLiteral("HD-DEV-007"));
    const DoctorCheckResult *driver = resultFor(session, QStringLiteral("HD-DEV-010"));
    const DoctorCheckResult *container = resultFor(session, QStringLiteral("HD-DEV-012"));
    QVERIFY(usage);
    QVERIFY(driver);
    QVERIFY(container);
    QCOMPARE(usage->status, DoctorCheckStatus::Healthy);
    QCOMPARE(driver->status, DoctorCheckStatus::Healthy);
    QCOMPARE(container->status, DoctorCheckStatus::Healthy);
}

void HidHideDoctorDomainTests::phaseOneReportRedactsSensitiveObservationValues()
{
    ReadOnlyDiagnosticSnapshot snapshot;
    snapshot.environment = fixtures::windows11X64Healthy();
    snapshot.protocol = {{QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Healthy, QStringLiteral("open"), {}, std::nullopt, 1, false}};
    snapshot.artifacts = {{DoctorArtifactKind::ClientExecutable, QStringLiteral("client"), QStringLiteral("C:\\Users\\private\\HidHideClient.exe"), true, 1, {}, {}, {}, CpuArchitecture::X64, {}, SignatureTrustState::NotChecked, {}, std::nullopt}};
    snapshot.processes = {{QStringLiteral("HidHideClient.exe"), 123, QStringLiteral("C:\\Users\\private\\HidHideClient.exe"), std::nullopt}};
    SnapshotProvider provider(snapshot);
    DoctorDiagnosticEngine engine;
    const QByteArray report = DoctorDiagnosticEngine::serializeJson(engine.run(provider), true);
    QVERIFY(report.contains("[redacted]"));
    QVERIFY(!report.contains("C:\\Users\\private"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(report, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 3);
    QCOMPARE(document.object().value(QStringLiteral("evidenceRecords")).toArray().size(), DoctorCatalog::v11DefinedCheckIds().size());
}

void HidHideDoctorDomainTests::phaseOneFixtureMatrixUsesOneEngine()
{
    using Mutator = std::function<void(ReadOnlyDiagnosticSnapshot &)>;
    const QList<QPair<QString, Mutator>> cases = {
        {QStringLiteral("healthy installation"), {}},
        {QStringLiteral("HidHide absent"), [](auto &s) { s.environment.hidhide.present = false; s.artifacts.clear(); s.service = {}; s.protocol.clear(); }},
        {QStringLiteral("client installed driver missing"), [](auto &s) { s.service = {}; s.artifacts.removeLast(); }},
        {QStringLiteral("driver installed client missing"), [](auto &s) { s.artifacts.removeFirst(); }},
        {QStringLiteral("client driver version mismatch"), [](auto &s) { s.artifacts[1].fileVersion = QStringLiteral("9.9"); }},
        {QStringLiteral("newer Driver Store package"), [](auto &s) { s.driverPackages.append({QStringLiteral("hidhide.inf_amd64"), {}, QStringLiteral("9.9"), {}, CpuArchitecture::X64, false, false, std::nullopt}); }},
        {QStringLiteral("pending reboot"), [](auto &s) { s.pendingRestart.append({QStringLiteral("PendingFileRenameOperations"), QStringLiteral("present"), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt}); }},
        {QStringLiteral("control endpoint missing"), [](auto &s) { s.protocol[0] = {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 2, QStringLiteral("ERROR_FILE_NOT_FOUND"), {}}, 1, false}; }},
        {QStringLiteral("control endpoint access denied"), [](auto &s) { s.protocol[0] = {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::PermissionLimited, {}, {}, NativeError{NativeErrorDomain::Win32, 5, QStringLiteral("ERROR_ACCESS_DENIED"), {}}, 1, false}; }},
        {QStringLiteral("GetWhitelist 0x57"), [](auto &s) { s.protocol[3] = {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 0x57, QStringLiteral("ERROR_INVALID_PARAMETER"), {}}, 1, true}; }},
        {QStringLiteral("all protocol queries independently fail"), [](auto &s) { for (auto &p : s.protocol) p = {p.operation, DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 31, QStringLiteral("ERROR_GEN_FAILURE"), {}}, 1, p.sizeNegotiation}; }},
        {QStringLiteral("GUI crash evidence healthy driver"), [](auto &s) { s.events.append({QStringLiteral("Application"), {}, 1000, {}, {}, QStringLiteral("HidHideClient crash"), EvidenceSensitivity::RequiresRedaction, std::nullopt}); }},
        {QStringLiteral("one malformed HID"), [](auto &s) { s.devices.append({QStringLiteral("HID\\bad"), {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::Unknown, {QStringLiteral("SPDRP_HARDWAREID:13")}, std::nullopt}); }},
        {QStringLiteral("multiple malformed HID"), [](auto &s) { for (int i = 0; i < 3; ++i) s.devices.append({QStringLiteral("HID\\bad%1").arg(i), {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::Unknown, {QStringLiteral("SPDRP_DEVICEDESC:13")}, std::nullopt}); }},
        {QStringLiteral("zero gaming devices"), [](auto &s) { s.devices.clear(); }},
        {QStringLiteral("large multi-vendor cockpit"), [](auto &s) { for (int i = 0; i < 24; ++i) s.devices.append({QStringLiteral("HID\\cockpit%1").arg(i), {}, QStringLiteral("stick%1").arg(i), QStringLiteral("vendor%1").arg(i), {}, {}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::PhysicalGamingInput, {}, std::nullopt}); }},
        {QStringLiteral("stale hidden reference"), [](auto &s) { s.protocol[6].multiStringValues.append(QStringLiteral("HID\\stale")); }},
        {QStringLiteral("missing application path"), [](auto &s) { s.protocol[4].multiStringValues = {QStringLiteral("C:\\missing.exe")}; }},
        {QStringLiteral("virtual output hidden"), [](auto &s) { s.devices[0].classification = DeviceClassification::VJoyVirtualOutput; }},
        {QStringLiteral("x64 package"), {}},
        {QStringLiteral("ARM64 package"), [](auto &s) { s.environment.platform.nativeArchitecture = CpuArchitecture::Arm64; s.environment.hidhide.packageArchitecture = CpuArchitecture::Arm64; }},
        {QStringLiteral("ARM64 wrong x64 package"), [](auto &s) { s.environment.platform.nativeArchitecture = CpuArchitecture::Arm64; s.environment.hidhide.packageArchitecture = CpuArchitecture::X64; }},
        {QStringLiteral("unknown future Windows"), [](auto &s) { s.environment.platform.build = 99999; }},
        {QStringLiteral("non-English Windows"), [](auto &s) { s.environment.platform.localeName = QStringLiteral("de-DE"); }},
        {QStringLiteral("permission-limited Event Log"), [](auto &s) { s.events.append({QStringLiteral("Application"), {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction, NativeError{NativeErrorDomain::Win32, 5, QStringLiteral("ERROR_ACCESS_DENIED"), {}}}); }},
        {QStringLiteral("WER unavailable"), [](auto &s) { s.werReports.append({QStringLiteral("WER"), {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction, NativeError{NativeErrorDomain::Win32, 2, QStringLiteral("ERROR_FILE_NOT_FOUND"), {}}}); }},
        {QStringLiteral("SetupAPI unavailable"), [](auto &s) { s.setupApiEvidence.append({QStringLiteral("SetupAPI"), {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction, NativeError{NativeErrorDomain::Win32, 5, QStringLiteral("ERROR_ACCESS_DENIED"), {}}}); }},
        {QStringLiteral("timed-out protocol"), [](auto &s) { s.protocol[4] = {QStringLiteral("GET_WHITELIST"), DoctorCheckStatus::TimedOut, {}, {}, NativeError{NativeErrorDomain::Win32, 1460, QStringLiteral("ERROR_TIMEOUT"), {}}, 2500, false}; }},
        {QStringLiteral("cancelled scan"), [](auto &s) { s.pendingRestart.append({QStringLiteral("fixture cancellation boundary"), {}, EvidenceSensitivity::SafeToExport, std::nullopt}); }},
        {QStringLiteral("registry live contradiction"), [](auto &s) { s.registryActive = false; s.contradictions.append(QStringLiteral("fixture contradiction")); }} };
    QCOMPARE(cases.size(), 30);
    DoctorDiagnosticEngine engine;
    for (const auto &fixture : cases) {
        ReadOnlyDiagnosticSnapshot snapshot = healthyFixtureSnapshot();
        if (fixture.second) fixture.second(snapshot);
        SnapshotProvider provider(snapshot);
        const DiagnosticRunOutcome outcome = engine.run(provider);
        QCOMPARE(outcome.session.checkResults().size(), DoctorCatalog::v11DefinedCheckIds().size());
        QVERIFY2(outcome.knowledgeEngineVersion == DoctorKnowledgeEngine::version(), qPrintable(fixture.first));
        QVERIFY2(outcome.session.currentOperation().has_value(), qPrintable(fixture.first));
    }
}

void HidHideDoctorDomainTests::phaseOneEvidenceFuzzRemainsBoundedAndSerializable()
{
    DoctorDiagnosticEngine engine;
    for (int index = 0; index < 64; ++index) {
        ReadOnlyDiagnosticSnapshot snapshot = healthyFixtureSnapshot();
        const QString unusualName(index * 97 + 1, QChar((index % 2) ? 0x03a9 : 0xd800));
        snapshot.devices.append({QStringLiteral("HID\\duplicate"), {}, unusualName, {}, {}, {QStringLiteral("VID_%1").arg(index)}, {}, {}, {}, {}, 0, static_cast<quint32>(index % 4), true,
            DeviceClassification::Unknown, {QStringLiteral("SPDRP_%1:13").arg(index)}, NativeError{NativeErrorDomain::Win32, 0x7000 + index, QStringLiteral("UNKNOWN_%1").arg(index), unusualName}});
        snapshot.registryWhitelist = {QString(), QStringLiteral("C:\\") + unusualName, QStringLiteral("C:\\") + unusualName};
        snapshot.events.append({QStringLiteral("Application"), {}, 0, {}, {}, unusualName, EvidenceSensitivity::PotentiallyIdentifying, std::nullopt});
        SnapshotProvider provider(snapshot);
        const QByteArray serialized = DoctorDiagnosticEngine::serializeJson(engine.run(provider), true);
        QVERIFY2(serialized.size() < 4 * 1024 * 1024, "Malformed evidence must remain bounded.");
        QJsonParseError error;
        QJsonDocument::fromJson(serialized, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
    }
}

void HidHideDoctorDomainTests::phaseOneProgressCancellationAndRerunAreCoherent()
{
    DoctorDiagnosticEngine engine;
    int progressEvents = 0;
    ProgressSnapshotProvider progressProvider(healthyFixtureSnapshot());
    const DiagnosticRunOutcome first = engine.run(progressProvider, nullptr, [&](const DoctorSession &) { ++progressEvents; });
    QVERIFY(progressEvents > 2);
    QVERIFY(first.session.plan().progress().overallPercent == 100);

    std::atomic_bool cancel{false};
    ProgressSnapshotProvider cancelProvider(healthyFixtureSnapshot(), true);
    const DiagnosticRunOutcome cancelled = engine.run(cancelProvider, &cancel);
    QVERIFY(cancelled.cancelled);
    QCOMPARE(cancelled.session.checkResults().size(), DoctorCatalog::v11DefinedCheckIds().size());
    QCOMPARE(cancelled.session.state(), DoctorSessionState::Cancelled);
    QVERIFY(cancelled.session.id().value() != first.session.id().value());
}

void HidHideDoctorDomainTests::phaseTwoDiagnosisFixtureMatrixIsDeterministicAndReadOnly()
{
    using Mutator = std::function<void(ReadOnlyDiagnosticSnapshot &)>;
    struct Case { QString name; Mutator mutate; QString expectedDiagnosis; };
    const QList<Case> cases = {
        {QStringLiteral("healthy"), {}, {}},
        {QStringLiteral("HidHide absent"), [](auto &s) { s.environment.hidhide.present = false; s.artifacts.clear(); s.service = {}; s.protocol.clear(); }, QStringLiteral("HD-DIAG-ABSENT")},
        {QStringLiteral("client only"), [](auto &s) { s.service = {}; s.artifacts.removeLast(); }, QStringLiteral("HD-DIAG-PARTIAL-INSTALL")},
        {QStringLiteral("driver only"), [](auto &s) { s.artifacts.removeFirst(); }, QStringLiteral("HD-DIAG-PARTIAL-INSTALL")},
        {QStringLiteral("client driver mismatch"), [](auto &s) { s.artifacts[1].fileVersion = QStringLiteral("1.4.181.0"); }, QStringLiteral("HD-DIAG-VERSION-MISMATCH")},
        {QStringLiteral("newer package pending replacement"), [](auto &s) { s.artifacts[1].fileVersion = QStringLiteral("1.4.181.0"); s.driverPackages.append({QStringLiteral("hidhide.inf"), {}, QStringLiteral("1.5.230.0"), {}, CpuArchitecture::X64, true, false, std::nullopt}); s.pendingRestart.append({QStringLiteral("PendingRename"), {}, EvidenceSensitivity::SafeToExport, std::nullopt}); }, QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT")},
        {QStringLiteral("pending restart"), [](auto &s) { s.artifacts[1].fileVersion = QStringLiteral("1.4.181.0"); s.environment.hidhide.driverVersion = QStringLiteral("1.4.181.0"); s.pendingRestart.append({QStringLiteral("PendingRename"), {}, EvidenceSensitivity::SafeToExport, std::nullopt}); }, QStringLiteral("HD-DIAG-INCOMPLETE-REPLACEMENT")},
        {QStringLiteral("control endpoint missing"), [](auto &s) { s.protocol[0] = {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 2, QStringLiteral("ERROR_FILE_NOT_FOUND"), {}}, 1, false}; }, QStringLiteral("HD-DIAG-CONTROL-MISSING")},
        {QStringLiteral("access denied"), [](auto &s) { s.protocol[0] = {QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::PermissionLimited, {}, {}, NativeError{NativeErrorDomain::Win32, 5, QStringLiteral("ERROR_ACCESS_DENIED"), {}}, 1, false}; }, QStringLiteral("HD-DIAG-PERMISSION-LIMITED")},
        {QStringLiteral("whitelist invalid parameter"), [](auto &s) { s.protocol[3] = {QStringLiteral("GET_WHITELIST_SIZE"), DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 0x57, QStringLiteral("ERROR_INVALID_PARAMETER"), {}}, 1, true}; }, QStringLiteral("HD-DIAG-WHITELIST-API")},
        {QStringLiteral("all protocol fails"), [](auto &s) { for (auto &p : s.protocol) p = {p.operation, DoctorCheckStatus::Failed, {}, {}, NativeError{NativeErrorDomain::Win32, 31, QStringLiteral("ERROR_GEN_FAILURE"), {}}, 1, p.sizeNegotiation}; }, QStringLiteral("HD-DIAG-PROTOCOL-BROAD-FAILURE")},
        {QStringLiteral("GUI crash only"), [](auto &s) { s.events.append({QStringLiteral("Application"), {}, 1000, {}, {}, QStringLiteral("HidHideClient crash exception"), EvidenceSensitivity::RequiresRedaction, std::nullopt}); }, QStringLiteral("HD-DIAG-CLIENT-FAILURE")},
        {QStringLiteral("one malformed HID"), [](auto &s) { s.devices.append({QStringLiteral("HID\\bad"), {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::ProblemDevice, {QStringLiteral("property failure")}, std::nullopt}); }, QStringLiteral("HD-DIAG-DEVICE-ENUMERATION")},
        {QStringLiteral("multiple malformed HID"), [](auto &s) { for (int i = 0; i < 3; ++i) s.devices.append({QStringLiteral("HID\\bad%1").arg(i), {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, true, DeviceClassification::ProblemDevice, {QStringLiteral("property failure")}, std::nullopt}); }, QStringLiteral("HD-DIAG-DEVICE-ENUMERATION")},
        {QStringLiteral("stale hidden device"), [](auto &s) { s.protocol[6].multiStringValues.append(QStringLiteral("HID\\stale")); }, QStringLiteral("HD-DIAG-STALE-CONFIG")},
        {QStringLiteral("missing application path"), [](auto &s) { s.protocol[4].multiStringValues = {QStringLiteral("C:\\missing.exe")}; }, QStringLiteral("HD-DIAG-STALE-CONFIG")},
        {QStringLiteral("virtual output hidden"), [](auto &s) { s.devices[0].classification = DeviceClassification::VJoyVirtualOutput; }, QStringLiteral("HD-DIAG-VIRTUAL-HIDDEN")},
        {QStringLiteral("architecture mismatch"), [](auto &s) { s.environment.platform.nativeArchitecture = CpuArchitecture::Arm64; s.environment.hidhide.packageArchitecture = CpuArchitecture::X64; }, QStringLiteral("HD-DIAG-ARCH-MISMATCH")},
        {QStringLiteral("future Windows"), [](auto &s) { s.environment.platform.build = 99999; }, QStringLiteral("HD-DIAG-FUTURE-WINDOWS")},
        {QStringLiteral("permission-limited event source"), [](auto &s) { s.events.append({QStringLiteral("Application"), {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction, NativeError{NativeErrorDomain::Win32, 5, QStringLiteral("ERROR_ACCESS_DENIED"), {}}}); }, QStringLiteral("HD-DIAG-PERMISSION-LIMITED")},
        {QStringLiteral("contradiction"), [](auto &s) { s.registryActive = false; s.contradictions.append(QStringLiteral("registry active false, direct GET_ACTIVE true")); }, QStringLiteral("HD-DIAG-INCONSISTENT-EVIDENCE")},
        {QStringLiteral("insufficient evidence"), [](auto &s) { s.artifacts.clear(); s.service = {}; s.protocol.clear(); s.devices.clear(); s.events.clear(); s.environment.hidhide.present = true; }, QStringLiteral("HD-DIAG-INCONCLUSIVE")},
    };
    QCOMPARE(cases.size(), 22);
    DoctorDiagnosticEngine engine;
    for (const Case &fixture : cases) {
        ReadOnlyDiagnosticSnapshot snapshot = healthyFixtureSnapshot();
        if (fixture.mutate) fixture.mutate(snapshot);
        SnapshotProvider provider(snapshot);
        const DiagnosticRunOutcome outcome = engine.run(provider);
        bool matched = fixture.expectedDiagnosis.isEmpty();
        for (const Diagnosis &diagnosis : outcome.session.diagnoses()) {
            if (diagnosis.id.value() == fixture.expectedDiagnosis) {
                matched = true;
                QVERIFY2(diagnosis.confidenceExplanation.score >= 0 && diagnosis.confidenceExplanation.score <= 100, qPrintable(fixture.name));
                QVERIFY2(!diagnosis.knowledgeVersion.isEmpty(), qPrintable(fixture.name));
            }
        }
        QVERIFY2(matched, qPrintable(fixture.name));
        QCOMPARE(outcome.session.userAction().state, UserActionState::NothingRequired);
        QVERIFY(!outcome.knowledgeEngineVersion.isEmpty());
    }
}

void HidHideDoctorDomainTests::phaseTwoReportCarriesFindingsDiagnosesAndKnowledgeVersion()
{
    QString label;
    FixtureDiagnosticProvider provider(createDevelopmentFixture(QStringLiteral("GetWhitelist 0x57"), &label));
    DoctorDiagnosticEngine engine;
    const DiagnosticRunOutcome outcome = engine.run(provider);
    const QJsonDocument document = QJsonDocument::fromJson(DoctorDiagnosticEngine::serializeJson(outcome, true));
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 3);
    QVERIFY(!document.object().value(QStringLiteral("knowledgeEngine")).toObject().value(QStringLiteral("version")).toString().isEmpty());
    QVERIFY(!document.object().value(QStringLiteral("findings")).toArray().isEmpty());
    QVERIFY(!document.object().value(QStringLiteral("diagnoses")).toArray().isEmpty());
    QVERIFY(!document.object().value(QStringLiteral("activityTimeline")).toArray().isEmpty());
    QCOMPARE(label, QStringLiteral("GetWhitelist 0x57"));
}

void HidHideDoctorDomainTests::phaseOneProductionProviderHasNoMutationSurface()
{
    QFile provider(QStringLiteral(HOTAS_DOCTOR_SOURCE_ROOT "/src/hidhide_doctor/doctor_windows_provider.cpp"));
    QVERIFY(provider.open(QIODevice::ReadOnly));
    const QByteArray source = provider.readAll();
    for (const QByteArray forbidden : {QByteArrayLiteral("RegSetValue"), QByteArrayLiteral("RegDelete"), QByteArrayLiteral("ChangeServiceConfig"), QByteArrayLiteral("StartService"), QByteArrayLiteral("ControlService"), QByteArrayLiteral("SetupDiCallClassInstaller"), QByteArrayLiteral("IOCTL_SET")})
        QVERIFY2(!source.contains(forbidden), forbidden.constData());
    QVERIFY(!source.contains("ShellExecute"));
    QVERIFY(!source.contains("CreateProcess"));
}

QTEST_APPLESS_MAIN(HidHideDoctorDomainTests)

#include "hidhide_doctor_domain_tests.moc"
