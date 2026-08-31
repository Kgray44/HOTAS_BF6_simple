#include "controller_readiness.h"
#include "controller_diagnostics.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtTest>

#include <algorithm>

using namespace hotas;

namespace {

PhysicalControllerCapabilities connectedController()
{
    PhysicalControllerCapabilities physical;
    physical.name = QStringLiteral("T.Flight HOTAS One");
    physical.directInputId = QStringLiteral("{stable-guid}");
    physical.hidInstanceId = QStringLiteral("HID\\VID_044F&PID_B68D\\exact-instance");
    physical.connected = true;
    physical.axes[0] = physical.axes[1] = physical.axes[2] = physical.axes[5] = true;
    physical.buttons = 15;
    physical.povs = 1;
    return physical;
}

VJoyCapabilities readyVJoy()
{
    VJoyCapabilities vjoy;
    vjoy.installed = true;
    vjoy.configurationUtilityAvailable = true;
    vjoy.driverReady = true;
    vjoy.devicePresent = true;
    vjoy.reportValid = true;
    vjoy.deviceId = 1;
    vjoy.axes[1] = vjoy.axes[2] = vjoy.axes[3] = vjoy.axes[6] = true;
    vjoy.buttons = 32;
    vjoy.forceFeedbackKnown = true;
    vjoy.restoreCommand = QStringLiteral("1 -f -a X Y Z Rz -b 32");
    return vjoy;
}

HidHideCapabilities readyHidHide()
{
    HidHideCapabilities hidhide;
    hidhide.installed = true;
    hidhide.cliAvailable = true;
    hidhide.serviceReady = true;
    hidhide.cloakKnown = true;
    hidhide.cloaked = true;
    hidhide.mapperAllowlisted = true;
    hidhide.selectedControllerResolved = true;
    hidhide.selectedControllerHidden = true;
    return hidhide;
}

MapperOutputRequirements defaultRequirements()
{
    MapperOutputRequirements requirements;
    requirements.axes[1] = requirements.axes[2] = requirements.axes[3] = requirements.axes[6] = true;
    requirements.buttons = 15;
    return requirements;
}

class FakeRunner final : public SetupProcessRunner {
public:
    bool failHide = false;
    bool failRollback = false;
    bool failRuntimeUnhide = false;
    bool cancelElevation = false;
    bool repairApplied = false;
    int staleVJoyCapabilityInspections = 0;
    int elevatedTransactions = 0;
    QString lastRepairRequest;
    QStringList calls;
    QStringList gamingDevices;
    QStringList hiddenDevices;

    SetupProcessResult run(const QString &program, const QStringList &arguments, int) override
    {
        Q_UNUSED(program)
        calls.append(arguments.join(u' '));
        if (failRuntimeUnhide && arguments.contains(QStringLiteral("--dev-unhide"))) {
            return {true, true, 5, {}, QStringLiteral("Access denied")};
        }
        return result(arguments);
    }

    SetupProcessResult runElevated(const QString &program, const QStringList &arguments, int) override
    {
        Q_UNUSED(program)
        calls.append(QStringLiteral("elevated:") + arguments.join(u' '));
        if (arguments.contains(QStringLiteral("--hotas-repair-transaction"))) {
            ++elevatedTransactions;
            if (cancelElevation) {
                SetupProcessResult cancelled;
                cancelled.cancelled = true;
                cancelled.windowsErrorCode = 1223;
                cancelled.error = QStringLiteral("Administrator approval was cancelled");
                return cancelled;
            }
            const int requestIndex = arguments.indexOf(QStringLiteral("--request"));
            const int resultIndex = arguments.indexOf(QStringLiteral("--result"));
            if (requestIndex < 0 || resultIndex < 0 || requestIndex + 1 >= arguments.size()
                || resultIndex + 1 >= arguments.size()) {
                return {false, false, -1, {}, QStringLiteral("fake repair transaction arguments are incomplete")};
            }
            const QString requestPath = arguments.at(requestIndex + 1);
            const QString resultPath = arguments.at(resultIndex + 1);
            QFile requestFile(requestPath);
            if (!requestFile.open(QIODevice::ReadOnly)) {
                return {false, false, -1, {}, QStringLiteral("fake repair request could not be read")};
            }
            const QJsonDocument request = QJsonDocument::fromJson(requestFile.readAll());
            lastRepairRequest = QString::fromUtf8(request.toJson(QJsonDocument::Compact));

            QJsonArray results;
            bool success = true;
            QJsonArray completed;
            for (const QJsonValue &value : request.object().value(QStringLiteral("operations")).toArray()) {
                const QJsonObject operation = value.toObject();
                QStringList operationArguments;
                for (const QJsonValue &argument : operation.value(QStringLiteral("arguments")).toArray()) {
                    operationArguments.append(argument.toString());
                }
                const bool failed = failHide && operationArguments.contains(QStringLiteral("--dev-hide"));
                QJsonObject item;
                item.insert(QStringLiteral("name"), operation.value(QStringLiteral("name")));
                item.insert(QStringLiteral("started"), true);
                item.insert(QStringLiteral("finished"), true);
                item.insert(QStringLiteral("succeeded"), !failed);
                item.insert(QStringLiteral("rollback"), false);
                item.insert(QStringLiteral("exitCode"), failed ? 5 : 0);
                item.insert(QStringLiteral("message"), failed ? QStringLiteral("Access denied") : QString{});
                results.append(item);
                if (failed) { success = false; break; }
                completed.append(operation);
            }
            if (!success) {
                for (int index = completed.size() - 1; index >= 0; --index) {
                    const QJsonObject operation = completed.at(index).toObject();
                    if (operation.value(QStringLiteral("rollbackArguments")).toArray().isEmpty()) continue;
                    QJsonObject rollback;
                    rollback.insert(QStringLiteral("name"), operation.value(QStringLiteral("name")));
                    rollback.insert(QStringLiteral("started"), true);
                    rollback.insert(QStringLiteral("finished"), true);
                    rollback.insert(QStringLiteral("succeeded"), true);
                    rollback.insert(QStringLiteral("rollback"), true);
                    rollback.insert(QStringLiteral("exitCode"), 0);
                    results.append(rollback);
                }
            } else {
                repairApplied = true;
            }
            QSaveFile resultFile(resultPath);
            if (!resultFile.open(QIODevice::WriteOnly)) {
                return {false, false, -1, {}, QStringLiteral("fake repair result could not be written")};
            }
            resultFile.write(QJsonDocument(QJsonObject{{QStringLiteral("success"), success},
                                                        {QStringLiteral("operations"), results}})
                .toJson(QJsonDocument::Compact));
            if (!resultFile.commit()) {
                return {false, false, -1, {}, QStringLiteral("fake repair result could not be committed")};
            }
            return {true, true, success ? 0 : 1, {}, {}};
        }
        if (failRollback && arguments.contains(QStringLiteral("--dev-unhide"))) {
            return {true, true, 5, {}, QStringLiteral("Access denied")};
        }
        return result(arguments);
    }

private:
    SetupProcessResult result(const QStringList &arguments)
    {
        const QString joined = arguments.join(u' ');
        if (joined.contains(QStringLiteral("--cloak-state"))) {
            return {true, true, 0, repairApplied ? QStringLiteral("--cloak-on\n") : QStringLiteral("--cloak-off\n"), {}};
        }
        if (joined.contains(QStringLiteral("--app-list"))) {
            return {true, true, 0, repairApplied
                ? QStringLiteral("--app-reg \"") + QCoreApplication::applicationFilePath() + QStringLiteral("\"\n") : QString{}, {}};
        }
        if (joined.contains(QStringLiteral("--dev-list"))) {
            if (!hiddenDevices.isEmpty()) {
                QStringList lines;
                for (const QString &instance : hiddenDevices) {
                    lines.append(QStringLiteral("--dev-hide \"") + instance + QStringLiteral("\""));
                }
                return {true, true, 0, lines.join(u'\n') + u'\n', {}};
            }
            return {true, true, 0, repairApplied
                ? QStringLiteral("--dev-hide \"HID\\VID_044F&PID_B68D\\exact-instance\"\n") : QString{}, {}};
        }
        if (joined.contains(QStringLiteral("--dev-gaming"))) {
            if (!gamingDevices.isEmpty()) return {true, true, 0, gamingDevices.join(u'\n'), {}};
            return {true, true, 0, QStringLiteral("HID\\VID_044F&PID_B68D\\exact-instance"), {}};
        }
        if (joined.contains(QStringLiteral("-t -c"))) {
            return {true, true, 0, QStringLiteral("vJoyConfig 1 -f -a X Y Z Rz -b 4\n"), {}};
        }
        if (joined.contains(QStringLiteral("-t"))) {
            const bool targetedCapabilityReport = arguments == QStringList{QStringLiteral("-t"), QStringLiteral("1")};
            bool capabilitiesConverged = repairApplied;
            if (capabilitiesConverged && targetedCapabilityReport && staleVJoyCapabilityInspections > 0) {
                --staleVJoyCapabilityInspections;
                capabilitiesConverged = false;
            }
            return {true, true, 0, QStringLiteral("Device: 1\nState: FREE\nButtons: %1\nContinous POVs: 0\nDescrete POVs: 0\nAxes: X Y Z Rz\nFFB Effects: None\n")
                .arg(capabilitiesConverged ? 32 : 4), {}};
        }
        return {true, true, 0, {}, {}};
    }
};

} // namespace

class ControllerReadinessTests final : public QObject {
    Q_OBJECT

private slots:
    void alreadyCorrectVJoyNeedsNoChange();
    void exactRequiredVJoyCapacityIsReady();
    void insufficientButtonsProducesFocusedVJoyRepair();
    void missingAxisProducesFocusedVJoyRepair();
    void nativePovRequirementsAndMixedPovSafety();
    void missingDependenciesAreGuidedNotAutomatic();
    void exactControllerIdentityIsRequiredForHidHide();
    void busyVJoyBlocksAutomaticChange();
    void mapperOwnedVJoyIsHealthy();
    void mapperOwnedVJoyStillRequiresCapacity();
    void externalVJoyConflictRequiresAction();
    void passiveIdentityGapIsAttentionNotFailure();
    void checkingPlanPublishesEverySubsystem();
    void controllerArrivalRequestsSetupOnlyForActionableTransitions();
    void activeInputReportsArePhysicalHealthEvidence();
    void processRunnerRollbackOnlyReversesThisTransaction();
    void repairCompletesInOneElevationAndVerifies();
    void repairWaitsForDelayedVJoyCapabilityPublication();
    void selfAccessFailureRollsBackBeforeReportingReady();
    void failedReacquisitionRequestsReconnectInsteadOfReady();
    void failedRecoveryReportsRollbackFailure();
    void diagnosticsAreScopedSanitizedAndCopyable();
    void uacCancellationIsNotReportedAsRepairFailure();
    void requirementsCoverProfilesAutomationAndExtendedAxes();
    void buttonCapacityUsesMappedRoutesRatherThanProvisionedLayout();
    void virtualAxisDescriptorsMustMatchExactly();
    void savedControllerVjoyRequirementsDetectInsufficientOutput();
    void managedVirtualOutputIdentityRequiresExactEnumeratedVjoy();
    void managedVirtualOutputsSwitchWithoutElevationAndRollBackOnFailure();
};

void ControllerReadinessTests::alreadyCorrectVJoyNeedsNoChange()
{
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QVERIFY(!plan.hidhideNeedsChanges);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
}

void ControllerReadinessTests::exactRequiredVJoyCapacityIsReady()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 15;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Ready);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
}

void ControllerReadinessTests::insufficientButtonsProducesFocusedVJoyRepair()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 8;
    // A fresh profile uses the mapper's bounded 1:1 button default. The
    // planner must not mistake empty saved routes for zero output capacity.
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), MapperOutputRequirements{}, vjoy, readyHidHide());
    QCOMPARE(plan.requirements.buttons, 15);
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.vjoyCanApply);
    QVERIFY(plan.proposedChanges.join(u' ').contains(QStringLiteral("15 buttons")));
}

void ControllerReadinessTests::missingAxisProducesFocusedVJoyRepair()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.axes[6] = false;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide());
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.vjoyCanApply);
}

void ControllerReadinessTests::nativePovRequirementsAndMixedPovSafety()
{
    MapperOutputRequirements continuous = defaultRequirements();
    continuous.continuousPovs = 1;
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.continuousPovs = 1;
    QVERIFY(!ControllerReadinessService::planFor(connectedController(), continuous, vjoy, readyHidHide()).vjoyNeedsChanges);

    MapperOutputRequirements mixed = continuous;
    mixed.discretePovs = 1;
    mixed.incompatiblePovMix = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), mixed, vjoy, readyHidHide());
    QVERIFY(!plan.vjoyCanApply);
    QVERIFY(!plan.canApplyAutomatically);
}

void ControllerReadinessTests::missingDependenciesAreGuidedNotAutomatic()
{
    VJoyCapabilities missingVJoy;
    HidHideCapabilities missingHidHide;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), missingVJoy, missingHidHide);
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.hidhideNeedsChanges);
    QVERIFY(!plan.canApplyAutomatically);
}

void ControllerReadinessTests::exactControllerIdentityIsRequiredForHidHide()
{
    HidHideCapabilities hidhide = readyHidHide();
    hidhide.selectedControllerResolved = false;
    hidhide.selectedControllerHidden = false;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), hidhide);
    QVERIFY(plan.hidhideNeedsChanges);
    QVERIFY(!plan.hidhideCanApply);
    QVERIFY(!plan.canApplyAutomatically);
}

void ControllerReadinessTests::busyVJoyBlocksAutomaticChange()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 8;
    vjoy.busy = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide());
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(!plan.vjoyCanApply);
}

void ControllerReadinessTests::mapperOwnedVJoyIsHealthy()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.busy = true; // vJoyConfig cannot distinguish the current process.
    vjoy.ownedByHotasBf6 = true;
    vjoy.outputReportsSucceeding = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide(), VerificationMode::Quick);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Ready);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
    QVERIFY(plan.vjoySummary.contains(QStringLiteral("HOTAS BF6 currently owns")));
}

void ControllerReadinessTests::mapperOwnedVJoyStillRequiresCapacity()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 8;
    vjoy.busy = true;
    vjoy.ownedByHotasBf6 = true;
    vjoy.outputReportsSucceeding = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide(), VerificationMode::Quick);
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.vjoyCanApply);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Error);
    QVERIFY(plan.proposedChanges.join(u' ').contains(QStringLiteral("15 buttons")));
}

void ControllerReadinessTests::externalVJoyConflictRequiresAction()
{
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.busy = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), vjoy, readyHidHide(), VerificationMode::Quick);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Error);
    QCOMPARE(plan.state, ControllerReadinessState::NeedsChanges);
    QVERIFY(plan.vjoySummary.contains(QStringLiteral("another application")));
}

void ControllerReadinessTests::passiveIdentityGapIsAttentionNotFailure()
{
    HidHideCapabilities hidhide = readyHidHide();
    hidhide.selectedControllerResolved = false;
    hidhide.selectedControllerHidden = false;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), hidhide, VerificationMode::Quick);
    QCOMPARE(plan.hidhideStatus, VerificationSubsystemState::Attention);
    QCOMPARE(plan.state, ControllerReadinessState::Attention);
}

void ControllerReadinessTests::checkingPlanPublishesEverySubsystem()
{
    const ControllerReadinessPlan plan = ControllerReadinessService::checkingPlan(
        connectedController(), VerificationMode::Full);
    QVERIFY(plan.isChecking);
    QCOMPARE(plan.state, ControllerReadinessState::Inspecting);
    QCOMPARE(plan.physicalStatus, VerificationSubsystemState::Checking);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Checking);
    QCOMPARE(plan.hidhideStatus, VerificationSubsystemState::Checking);
}

void ControllerReadinessTests::controllerArrivalRequestsSetupOnlyForActionableTransitions()
{
    QVERIFY(ControllerReadinessService::isNewPhysicalControllerArrival(false, true));
    QVERIFY(!ControllerReadinessService::isNewPhysicalControllerArrival(true, true));
    QVERIFY(!ControllerReadinessService::isNewPhysicalControllerArrival(false, false));
    QVERIFY(!ControllerReadinessService::isNewPhysicalControllerArrival(true, false));

    const ControllerReadinessPlan needsChanges = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), HidHideCapabilities{}, VerificationMode::Quick);
    QVERIFY(ControllerReadinessService::needsSetupAfterControllerArrival(true, needsChanges));
    QVERIFY(!ControllerReadinessService::needsSetupAfterControllerArrival(false, needsChanges));

    const ControllerReadinessPlan ready = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), readyHidHide(), VerificationMode::Quick);
    QVERIFY(!ControllerReadinessService::needsSetupAfterControllerArrival(true, ready));
}

void ControllerReadinessTests::activeInputReportsArePhysicalHealthEvidence()
{
    PhysicalControllerCapabilities physical = connectedController();
    physical.inputReportsReceived = true;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        physical, defaultRequirements(), readyVJoy(), readyHidHide(), VerificationMode::Quick);
    QCOMPARE(plan.physicalStatus, VerificationSubsystemState::Ready);
    QVERIFY(plan.physicalSummary.contains(QStringLiteral("input reports received")));
}

void ControllerReadinessTests::processRunnerRollbackOnlyReversesThisTransaction()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->failHide = true;
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(probe->calls.contains(QStringLiteral("-t 1")));
    QVERIFY(probe->calls.contains(QStringLiteral("-t -c 1")));
    QVERIFY(!service.applyAutomatically());
    QCOMPARE(probe->elevatedTransactions, 1);
    QVERIFY(probe->lastRepairRequest.contains(QStringLiteral("--dev-hide")));
    QVERIFY(probe->lastRepairRequest.contains(QStringLiteral("HID\\\\VID_044F&PID_B68D\\\\EXACT-INSTANCE")));
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Failed);
    QVERIFY(service.lastAutomaticRepairResult().message.contains(QStringLiteral("HidHide device repair failed")));
    QVERIFY(service.lastAutomaticRepairResult().message.contains(QStringLiteral("code 5")));
    QVERIFY(!service.plan().status.contains(QStringLiteral("failed: ."), Qt::CaseInsensitive));
}

void ControllerReadinessTests::repairCompletesInOneElevationAndVerifies()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    service.completePhysicalAccessVerification(true, true);
    QCOMPARE(probe->elevatedTransactions, 1);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Ready);
    QCOMPARE(service.plan().state, ControllerReadinessState::Ready);
    QCOMPARE(service.plan().status, QStringLiteral("READY — Controller setup repaired successfully and physical input was reacquired."));
}

void ControllerReadinessTests::repairWaitsForDelayedVJoyCapabilityPublication()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->staleVJoyCapabilityInspections = 2;
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    QCOMPARE(probe->staleVJoyCapabilityInspections, 0);
    QCOMPARE(probe->elevatedTransactions, 1);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Ready);
    QCOMPARE(service.plan().state, ControllerReadinessState::Verifying);
}

void ControllerReadinessTests::selfAccessFailureRollsBackBeforeReportingReady()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    QCOMPARE(service.plan().state, ControllerReadinessState::Verifying);
    QVERIFY(service.recoverFromPhysicalAccessFailure());
    QVERIFY(!service.hasPendingRecovery());
    service.completePhysicalAccessVerification(false, false, true, true, true);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Failed);
    QCOMPARE(service.plan().state, ControllerReadinessState::Attention);
    QVERIFY(service.plan().status.contains(QStringLiteral("SELF-ACCESS FAILURE")));
    QVERIFY(probe->calls.contains(QStringLiteral("elevated:--dev-unhide HID\\VID_044F&PID_B68D\\EXACT-INSTANCE")));
}

void ControllerReadinessTests::failedReacquisitionRequestsReconnectInsteadOfReady()
{
    auto fake = std::make_unique<FakeRunner>();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    QVERIFY(service.recoverFromPhysicalAccessFailure());
    // Configuration rollback alone is not enough: without new reports, this
    // must remain a reconnect state rather than a false successful undo.
    service.completePhysicalAccessVerification(false, false, true, true, false);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Failed);
    QCOMPARE(service.plan().state, ControllerReadinessState::Failed);
    QVERIFY(service.plan().status.contains(QStringLiteral("PHYSICAL CONTROLLER LOST")));
    QVERIFY(service.plan().status.contains(QStringLiteral("reconnect"), Qt::CaseInsensitive));
    QVERIFY(!service.lastAutomaticRepairResult().physicalReportsReceivedAfterRollback);
}

void ControllerReadinessTests::failedRecoveryReportsRollbackFailure()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    probe->failRollback = true;
    QVERIFY(!service.recoverFromPhysicalAccessFailure());
    QVERIFY(service.hasPendingRecovery());
    service.completePhysicalAccessVerification(false, false, true, false, false);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Failed);
    QCOMPARE(service.plan().state, ControllerReadinessState::Failed);
    QVERIFY(service.plan().status.contains(QStringLiteral("ROLLBACK FAILURE")));
    QVERIFY(service.lastAutomaticRepairResult().rollbackAttempted);
    QVERIFY(!service.lastAutomaticRepairResult().rollbackSucceeded);
    QVERIFY(probe->calls.contains(QStringLiteral("elevated:--dev-unhide HID\\VID_044F&PID_B68D\\EXACT-INSTANCE")));
}

void ControllerReadinessTests::diagnosticsAreScopedSanitizedAndCopyable()
{
    ControllerDiagnosticsSnapshot snapshot;
    snapshot.version = QStringLiteral("1.9.3");
    snapshot.timestamp = QStringLiteral("2026-08-25T15:00:00Z");
    snapshot.windowsVersion = QStringLiteral("Windows 11");
    snapshot.physical = connectedController();
    snapshot.physical.inputReportsReceived = true;
    snapshot.vjoy = readyVJoy();
    snapshot.hidhide = readyHidHide();
    snapshot.repair.outcome = AutomaticRepairOutcome::Failed;
    snapshot.repair.message = QStringLiteral("HIDHIDE SELF-ACCESS FAILURE");
    snapshot.repair.physicalReacquisitionAttempted = true;
    snapshot.repair.rollbackAttempted = true;
    snapshot.repair.rollbackSucceeded = true;
    snapshot.repair.physicalReportsReceivedAfterRollback = true;
    snapshot.repair.operations.append({QStringLiteral("Hide selected HOTAS"), true, true, false, false,
        5, 31, QStringLiteral("C:\\Users\\Snow\\HOTAS failure"),
        QStringLiteral("See C:\\Program Files\\HOTAS BF6\\setup.log"), {}});
    snapshot.axes.append({QStringLiteral("X"), -1.0F, -0.041F, 1.0F, 0.0F, 0.0F});
    snapshot.axes.back().activity = PhysicalAxisActivity::Fixed;
    snapshot.activeProfileName = QStringLiteral("Battlefield 6");
    snapshot.virtualOutputs.append({QStringLiteral("BF6 Output"), QStringLiteral("X · Y · Z · Rz"),
        1, true, true, false});
    snapshot.selectedHidInstance = connectedController().hidInstanceId;
    snapshot.privatePaths = {QStringLiteral("C:\\Program Files\\HOTAS BF6")};

    const QString report = buildControllerDiagnostics(snapshot);
    QVERIFY(report.contains(QStringLiteral("HOTAS BF6 Diagnostics")));
    QVERIFY(report.contains(QStringLiteral("PHYSICAL CONTROLLER")));
    QVERIFY(report.contains(QStringLiteral("exit code 5")));
    QVERIFY(report.contains(QStringLiteral("X RAW MIN: -1.000  RAW NEUTRAL: -0.041")));
    QVERIFY(report.contains(QStringLiteral("ACTIVITY: Inactive device axis")));
    QVERIFY(report.contains(QStringLiteral("ACTIVE PROFILE / OUTPUT")));
    QVERIFY(report.contains(QStringLiteral("Output: BF6 Output  vJoy 1")));
    QVERIFY(report.contains(snapshot.selectedHidInstance));
    QVERIFY(report.contains(QStringLiteral("<USER_HOME>")));
    QVERIFY(report.contains(QStringLiteral("<LOCAL_PATH>")));
    QVERIFY(!report.contains(QStringLiteral("Snow")));
    QVERIFY(!report.contains(QStringLiteral("C:\\Program Files\\HOTAS BF6")));

    QVERIFY(copyControllerDiagnosticsToClipboard(snapshot));
    QCOMPARE(QGuiApplication::clipboard()->text(), report);
    QVERIFY(isControllerDiagnosticsAvailable(ControllerReadinessState::Attention));
    QVERIFY(isControllerDiagnosticsAvailable(ControllerReadinessState::Failed));
    QVERIFY(isControllerDiagnosticsAvailable(ControllerReadinessState::NeedsChanges));
    QVERIFY(!isControllerDiagnosticsAvailable(ControllerReadinessState::Ready));
}

void ControllerReadinessTests::uacCancellationIsNotReportedAsRepairFailure()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->cancelElevation = true;
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 15;
    configuration.profiles.front().buttons = {button};
    QVERIFY(service.inspect(configuration, connectedController()).canApplyAutomatically);
    QVERIFY(!service.applyAutomatically());
    QCOMPARE(probe->elevatedTransactions, 1);
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Cancelled);
    QCOMPARE(service.plan().state, ControllerReadinessState::Cancelled);
    QVERIFY(service.plan().status.contains(QStringLiteral("administrator approval was not granted"), Qt::CaseInsensitive));
    QVERIFY(!service.plan().status.contains(QStringLiteral("failed"), Qt::CaseInsensitive));
}

void ControllerReadinessTests::requirementsCoverProfilesAutomationAndExtendedAxes()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.outputLayouts.front().requirements.axes[static_cast<int>(VirtualAxis::Slider1)] = true;
    configuration.profiles.front().axes[3].target = VirtualAxis::Slider1;
    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 42;
    configuration.profiles.front().buttons = {button};
    AutomationDefinition automation;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonTap;
    action.virtualButton = 64;
    automation.actions = {action};
    configuration.automations = {automation};
    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(configuration);
    QVERIFY(requirements.axes[static_cast<int>(VirtualAxis::Slider1)]);
    QCOMPARE(requirements.buttons, 64);
}

void ControllerReadinessTests::buttonCapacityUsesMappedRoutesRatherThanProvisionedLayout()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &primary = configuration.profiles.front();
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 0);

    ButtonBinding button;
    button.type = ButtonActionType::VirtualButton;
    button.target = 28;
    primary.buttons = {button};
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 28);

    primary.povs.resize(1);
    primary.povs.front()[static_cast<size_t>(povDirectionIndex(PovDirection::Right))] =
        ButtonBinding{ButtonActionType::VirtualButton, 29};
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 29);

    AutomationDefinition automation;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonTap;
    action.virtualButton = 30;
    automation.actions = {action};
    configuration.automations = {automation};
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 30);

    VirtualOutputLayout otherLayout = configuration.outputLayouts.front();
    otherLayout.id = QStringLiteral("other-layout");
    configuration.outputLayouts.push_back(otherLayout);
    ControllerProfile other = primary;
    other.id = QStringLiteral("other-profile");
    other.outputLayoutId = otherLayout.id;
    other.buttons.front().target = 64;
    configuration.profiles.push_back(other);
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 30);

    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 30;
    const ControllerReadinessPlan ready = ControllerReadinessService::planFor(
        connectedController(), ControllerReadinessService::requirementsFor(configuration), vjoy, readyHidHide());
    QVERIFY(!ready.vjoyNeedsChanges);
    vjoy.buttons = 15;
    const ControllerReadinessPlan insufficient = ControllerReadinessService::planFor(
        connectedController(), ControllerReadinessService::requirementsFor(configuration), vjoy, readyHidHide());
    QVERIFY(insufficient.vjoyNeedsChanges);

    configuration.automations.front().enabled = false;
    QCOMPARE(ControllerReadinessService::requirementsFor(configuration).buttons, 29);
}

void ControllerReadinessTests::virtualAxisDescriptorsMustMatchExactly()
{
    ControllerVJoyRequirements saved;
    saved.deviceId = 1;
    saved.axes[1] = saved.axes[2] = saved.axes[3] = saved.axes[6] = true;
    saved.buttons = 15;
    saved.continuousPovs = 1;
    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(saved);
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.axes[4] = vjoy.axes[5] = true; // Extra advertised axes are incompatible.
    vjoy.buttons = 32;                  // Button capacity remains a minimum.
    vjoy.continuousPovs = 1;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), requirements, vjoy, readyHidHide());
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.vjoySummary.contains(QStringLiteral("additional axes")));
}

void ControllerReadinessTests::savedControllerVjoyRequirementsDetectInsufficientOutput()
{
    ControllerVJoyRequirements saved;
    saved.axes[1] = saved.axes[2] = saved.axes[3] = saved.axes[6] = true;
    saved.buttons = 15;
    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(saved);
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 8;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), requirements, vjoy, readyHidHide());
    QVERIFY(plan.vjoyNeedsChanges);
    QVERIFY(plan.vjoyCanApply);
}

void ControllerReadinessTests::managedVirtualOutputIdentityRequiresExactEnumeratedVjoy()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->gamingDevices = {QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY-ONE")};
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);

    QString normalized;
    QString status;
    QVERIFY(service.validateManagedVirtualOutputIdentity(
        QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY-ONE"), &normalized, &status));
    QCOMPARE(normalized, QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY-ONE"));
    QVERIFY(status.contains(QStringLiteral("verified"), Qt::CaseInsensitive));
    probe->gamingDevices = {QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY-ONE-OTHER")};
    QVERIFY(!service.validateManagedVirtualOutputIdentity(
        QStringLiteral("HID\\VID_1234&PID_BEAD\\VJOY-ONE"), &normalized, &status));
    QVERIFY(status.contains(QStringLiteral("not currently enumerated"), Qt::CaseInsensitive));
    QVERIFY(!service.validateManagedVirtualOutputIdentity(
        QStringLiteral("HID\\VID_044F&PID_B68D\\physical"), &normalized, &status));
    QVERIFY(status.contains(QStringLiteral("display names"), Qt::CaseInsensitive));
    QVERIFY(std::none_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("elevated:"));
    }));
}

void ControllerReadinessTests::managedVirtualOutputsSwitchWithoutElevationAndRollBackOnFailure()
{
    const QString bf6Instance = QStringLiteral("HID\\VID_1234&PID_BEAD\\BF6-OUTPUT");
    const QString starInstance = QStringLiteral("HID\\VID_1234&PID_BEAD\\STAR-OUTPUT");
    MapperConfiguration configuration = defaultConfiguration();
    VirtualOutputLayout &bf6 = configuration.outputLayouts.front();
    bf6.hidhideManaged = true;
    bf6.hidHideDeviceInstanceId = bf6Instance;
    VirtualOutputLayout star = defaultBf6OutputLayout();
    star.id = QStringLiteral("star-output");
    star.name = QStringLiteral("Star Citizen Output");
    star.requirements.deviceId = 2;
    star.requirements.axes[static_cast<size_t>(VirtualAxis::Slider0)] = true;
    star.hidhideManaged = true;
    star.hidHideDeviceInstanceId = starInstance;
    configuration.outputLayouts.push_back(star);

    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->repairApplied = true;
    probe->hiddenDevices = {starInstance};
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    const OutputVisibilitySwitchResult result = service.applyManagedOutputVisibility(
        configuration, star.id);
    QVERIFY(result.succeeded);
    QVERIFY(result.changed);
    const int hideBf6 = probe->calls.indexOf(QStringLiteral("--dev-hide ") + bf6Instance);
    const int unhideStar = probe->calls.indexOf(QStringLiteral("--dev-unhide ") + starInstance);
    QVERIFY(hideBf6 >= 0);
    QVERIFY(unhideStar > hideBf6);
    QVERIFY(std::none_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("elevated:"));
    }));

    auto failingFake = std::make_unique<FakeRunner>();
    FakeRunner *failingProbe = failingFake.get();
    failingProbe->repairApplied = true;
    failingProbe->hiddenDevices = {starInstance};
    failingProbe->failRuntimeUnhide = true;
    ControllerReadinessService failingService(std::move(failingFake), utilities);
    const OutputVisibilitySwitchResult failed = failingService.applyManagedOutputVisibility(
        configuration, star.id);
    QVERIFY(!failed.succeeded);
    QVERIFY(failed.status.contains(QStringLiteral("rolled back"), Qt::CaseInsensitive));
    QVERIFY(failingProbe->calls.contains(QStringLiteral("--dev-hide ") + bf6Instance));
    // The selected output was already hidden when its unhide command failed;
    // only the completed hide of the old output needs reversal.
    QVERIFY(failingProbe->calls.contains(QStringLiteral("--dev-unhide ") + bf6Instance));
    QVERIFY(!failingProbe->calls.contains(QStringLiteral("--dev-hide ") + starInstance));
}

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    ControllerReadinessTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "controller_readiness_tests.moc"
