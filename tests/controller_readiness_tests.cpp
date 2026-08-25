#include "controller_readiness.h"

#include <QtTest>

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
    QStringList calls;

    SetupProcessResult run(const QString &program, const QStringList &arguments, int) override
    {
        Q_UNUSED(program)
        calls.append(arguments.join(u' '));
        return result(arguments);
    }

    SetupProcessResult runElevated(const QString &program, const QStringList &arguments, int) override
    {
        Q_UNUSED(program)
        calls.append(QStringLiteral("elevated:") + arguments.join(u' '));
        if (failHide && arguments.contains(QStringLiteral("--dev-hide"))) {
            return {true, true, 1, {}, QStringLiteral("simulated dev-hide failure")};
        }
        return result(arguments);
    }

private:
    static SetupProcessResult result(const QStringList &arguments)
    {
        const QString joined = arguments.join(u' ');
        if (joined.contains(QStringLiteral("--cloak-state"))) return {true, true, 0, QStringLiteral("--cloak-off\n"), {}};
        if (joined.contains(QStringLiteral("--app-list"))) return {true, true, 0, {}, {}};
        if (joined.contains(QStringLiteral("--dev-list"))) return {true, true, 0, {}, {}};
        if (joined.contains(QStringLiteral("--dev-gaming"))) {
            return {true, true, 0, QStringLiteral("HID\\VID_044F&PID_B68D\\exact-instance"), {}};
        }
        if (joined.contains(QStringLiteral("-t -c"))) {
            return {true, true, 0, QStringLiteral("vJoyConfig 1 -f -a X Y Z Rz -b 4\n"), {}};
        }
        if (joined.contains(QStringLiteral("-t"))) {
            return {true, true, 0, QStringLiteral("Device: 1\nState: FREE\nButtons: 4\nContinous POVs: 0\nDescrete POVs: 0\nAxes: X Y Z Rz\nFFB Effects: None\n"), {}};
        }
        return {true, true, 0, {}, {}};
    }
};

} // namespace

class ControllerReadinessTests final : public QObject {
    Q_OBJECT

private slots:
    void alreadyCorrectVJoyNeedsNoChange();
    void insufficientButtonsProducesFocusedVJoyRepair();
    void missingAxisProducesFocusedVJoyRepair();
    void nativePovRequirementsAndMixedPovSafety();
    void missingDependenciesAreGuidedNotAutomatic();
    void exactControllerIdentityIsRequiredForHidHide();
    void busyVJoyBlocksAutomaticChange();
    void mapperOwnedVJoyIsHealthy();
    void externalVJoyConflictRequiresAction();
    void passiveIdentityGapIsAttentionNotFailure();
    void checkingPlanPublishesEverySubsystem();
    void activeInputReportsArePhysicalHealthEvidence();
    void processRunnerRollbackOnlyReversesThisTransaction();
    void requirementsCoverProfilesAutomationAndExtendedAxes();
};

void ControllerReadinessTests::alreadyCorrectVJoyNeedsNoChange()
{
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QVERIFY(!plan.hidhideNeedsChanges);
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
    QVERIFY(probe->calls.contains(QStringLiteral("elevated:--app-unreg ") + QCoreApplication::applicationFilePath()));
    QVERIFY(probe->calls.contains(QStringLiteral("elevated:1 -f -a X Y Z Rz -b 4")));
    QVERIFY(!probe->calls.join(u' ').contains(QStringLiteral("app-clean")));
}

void ControllerReadinessTests::requirementsCoverProfilesAutomationAndExtendedAxes()
{
    MapperConfiguration configuration = defaultConfiguration();
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

QTEST_MAIN(ControllerReadinessTests)
#include "controller_readiness_tests.moc"
