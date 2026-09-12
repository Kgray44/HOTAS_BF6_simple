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

bool containsCanonicalApplicationPath(const QStringList &paths)
{
    const QString application = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    return std::any_of(paths.cbegin(), paths.cend(), [&application](const QString &path) {
        return QFileInfo(path).canonicalFilePath().compare(application, Qt::CaseInsensitive) == 0;
    });
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
    for (int axis = 1; axis < kVirtualAxisSlotCount; ++axis) vjoy.axes[axis] = true;
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
    bool failRuntimeSecondVisibilityOperation = false;
    bool cancelElevation = false;
    bool repairApplied = false;
    bool cloakEnabled = false;
    bool vjoy219HumanReadableOutput = false;
    int staleVJoyCapabilityInspections = 0;
    int elevatedTransactions = 0;
    QString lastRepairRequest;
    QStringList calls;
    QStringList elevatedPrograms;
    QStringList gamingDevices;
    QStringList hiddenDevices;
    QStringList allowlistedApplications;
    int runtimeVisibilityOperations = 0;

    SetupProcessResult run(const QString &program, const QStringList &arguments, int) override
    {
        Q_UNUSED(program)
        calls.append(arguments.join(u' '));
        const bool visibilityOperation = arguments.contains(QStringLiteral("--dev-hide"))
            || arguments.contains(QStringLiteral("--dev-unhide"));
        if (visibilityOperation) ++runtimeVisibilityOperations;
        if ((failRuntimeUnhide && arguments.contains(QStringLiteral("--dev-unhide")))
            || (failRuntimeSecondVisibilityOperation && runtimeVisibilityOperations == 2)) {
            return {true, true, 5, {}, QStringLiteral("Access denied")};
        }
        for (int index = 0; index < arguments.size(); ++index) {
            const QString &argument = arguments.at(index);
            if (argument == QStringLiteral("--dev-hide") && index + 1 < arguments.size()) {
                const QString &instance = arguments.at(++index);
                if (!hiddenDevices.contains(instance, Qt::CaseInsensitive)) hiddenDevices.append(instance);
            } else if (argument == QStringLiteral("--dev-unhide") && index + 1 < arguments.size()) {
                hiddenDevices.removeAll(arguments.at(++index));
            }
        }
        return result(arguments);
    }

    SetupProcessResult runElevated(const QString &program, const QStringList &arguments, int) override
    {
        calls.append(QStringLiteral("elevated:") + arguments.join(u' '));
        elevatedPrograms.append(program);
        ++elevatedTransactions;
        if (cancelElevation) {
            SetupProcessResult cancelled;
            cancelled.cancelled = true;
            cancelled.windowsErrorCode = 1223;
            cancelled.error = QStringLiteral("Administrator approval was cancelled");
            return cancelled;
        }
        if (arguments.contains(QStringLiteral("--hotas-repair-transaction"))) {
            return runApprovedRepairHelper(arguments);
        }
        if (failRollback && arguments.contains(QStringLiteral("--dev-unhide"))) {
            return {true, true, 5, {}, QStringLiteral("Access denied")};
        }
        if (failHide && arguments.contains(QStringLiteral("--dev-hide"))) {
            return {true, true, 5, {}, QStringLiteral("Access denied")};
        }
        for (int index = 0; index < arguments.size(); ++index) {
            const QString &argument = arguments.at(index);
            if (argument == QStringLiteral("--dev-hide") && index + 1 < arguments.size()) {
                const QString &instance = arguments.at(++index);
                if (!hiddenDevices.contains(instance, Qt::CaseInsensitive)) hiddenDevices.append(instance);
            } else if (argument == QStringLiteral("--dev-unhide") && index + 1 < arguments.size()) {
                hiddenDevices.removeAll(arguments.at(++index));
            } else if (argument == QStringLiteral("--app-reg") && index + 1 < arguments.size()) {
                const QString &application = arguments.at(++index);
                if (!allowlistedApplications.contains(application, Qt::CaseInsensitive)) {
                    allowlistedApplications.append(application);
                }
            } else if (argument == QStringLiteral("--app-unreg") && index + 1 < arguments.size()) {
                allowlistedApplications.removeAll(arguments.at(++index));
            } else if (argument == QStringLiteral("--cloak-on")) {
                cloakEnabled = true;
            } else if (argument == QStringLiteral("--cloak-off")) {
                cloakEnabled = false;
            }
        }
        repairApplied = true;
        return result(arguments);
    }

private:
    SetupProcessResult runApprovedRepairHelper(const QStringList &arguments)
    {
        const int requestIndex = arguments.indexOf(QStringLiteral("--request"));
        const int resultIndex = arguments.indexOf(QStringLiteral("--result"));
        if (requestIndex < 0 || resultIndex < 0 || requestIndex + 1 >= arguments.size()
            || resultIndex + 1 >= arguments.size()) {
            return {false, false, -1, {}, QStringLiteral("Repair request arguments were invalid")};
        }
        lastRepairRequest = arguments.at(requestIndex + 1);
        QFile request(lastRepairRequest);
        QJsonParseError error;
        if (!request.open(QIODevice::ReadOnly)) return {false, false, -1, {}, QStringLiteral("Repair request was unavailable")};
        const QJsonDocument document = QJsonDocument::fromJson(request.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return {false, false, -1, {}, QStringLiteral("Repair request was invalid")};
        }

        const auto execute = [this](const QJsonObject &operation, bool rollback) {
            const QJsonArray encoded = rollback ? operation.value(QStringLiteral("rollbackArguments")).toArray()
                                                : operation.value(QStringLiteral("arguments")).toArray();
            QStringList args;
            for (const QJsonValue &value : encoded) args.append(value.toString());
            calls.append(QStringLiteral("helper:") + args.join(u' '));
            const bool rejected = (!rollback && failHide && args.contains(QStringLiteral("--dev-hide")))
                || (rollback && failRollback && args.contains(QStringLiteral("--dev-unhide")));
            QJsonObject item{{QStringLiteral("name"), operation.value(QStringLiteral("name")).toString()},
                             {QStringLiteral("rollback"), rollback}, {QStringLiteral("started"), true},
                             {QStringLiteral("finished"), true}, {QStringLiteral("succeeded"), !rejected},
                             {QStringLiteral("exitCode"), rejected ? 5 : 0}};
            if (rejected) {
                item.insert(QStringLiteral("message"), QStringLiteral("Access denied"));
                return item;
            }
            for (int index = 0; index < args.size(); ++index) {
                const QString &argument = args.at(index);
                if (argument == QStringLiteral("--dev-hide") && index + 1 < args.size()) {
                    const QString &instance = args.at(++index);
                    if (!hiddenDevices.contains(instance, Qt::CaseInsensitive)) hiddenDevices.append(instance);
                } else if (argument == QStringLiteral("--dev-unhide") && index + 1 < args.size()) {
                    hiddenDevices.removeAll(args.at(++index));
                } else if (argument == QStringLiteral("--app-reg") && index + 1 < args.size()) {
                    const QString &application = args.at(++index);
                    if (!allowlistedApplications.contains(application, Qt::CaseInsensitive)) allowlistedApplications.append(application);
                } else if (argument == QStringLiteral("--app-unreg") && index + 1 < args.size()) {
                    allowlistedApplications.removeAll(args.at(++index));
                } else if (argument == QStringLiteral("--cloak-on")) {
                    cloakEnabled = true;
                } else if (argument == QStringLiteral("--cloak-off")) {
                    cloakEnabled = false;
                }
            }
            repairApplied = true;
            return item;
        };

        QJsonArray results;
        QList<QJsonObject> completed;
        bool success = true;
        for (const QJsonValue &value : document.object().value(QStringLiteral("operations")).toArray()) {
            const QJsonObject operation = value.toObject();
            const QJsonObject item = execute(operation, false);
            results.append(item);
            if (!item.value(QStringLiteral("succeeded")).toBool()) { success = false; break; }
            completed.append(operation);
        }
        if (!success) {
            for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
                if (!it->value(QStringLiteral("rollbackArguments")).toArray().isEmpty()) {
                    results.append(execute(*it, true));
                }
            }
        }
        QSaveFile output(arguments.at(resultIndex + 1));
        if (!output.open(QIODevice::WriteOnly)) return {false, false, -1, {}, QStringLiteral("Repair response was unavailable")};
        output.write(QJsonDocument(QJsonObject{{QStringLiteral("success"), success},
            {QStringLiteral("operations"), results}}).toJson(QJsonDocument::Compact));
        if (!output.commit()) return {false, false, -1, {}, QStringLiteral("Repair response could not be written")};
        return {true, true, success ? 0 : 1, {}, {}};
    }

    SetupProcessResult result(const QStringList &arguments)
    {
        const QString joined = arguments.join(u' ');
        if (joined.contains(QStringLiteral("--cloak-state"))) {
            return {true, true, 0, (cloakEnabled || repairApplied)
                ? QStringLiteral("--cloak-on\n") : QStringLiteral("--cloak-off\n"), {}};
        }
        if (joined.contains(QStringLiteral("--app-list"))) {
            QStringList applications = allowlistedApplications;
            if (repairApplied && !applications.contains(QCoreApplication::applicationFilePath(), Qt::CaseInsensitive)) {
                applications.append(QCoreApplication::applicationFilePath());
            }
            QStringList lines;
            for (const QString &application : applications) {
                lines.append(QStringLiteral("--app-reg \"") + application + QStringLiteral("\""));
            }
            return {true, true, 0, lines.join(u'\n') + (lines.isEmpty() ? QString{} : QStringLiteral("\n")), {}};
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
            return {true, true, 0, vjoy219HumanReadableOutput
                ? QStringLiteral("vJoyConfig 1 -f -a X Y Z Rx Ry Rz -b 15 -e all\n")
                : QStringLiteral("vJoyConfig 1 -f -a X Y Z Rz -b 4\n"), {}};
        }
        if (joined.contains(QStringLiteral("-t"))) {
            const bool targetedCapabilityReport = arguments == QStringList{QStringLiteral("-t"), QStringLiteral("1")};
            bool capabilitiesConverged = repairApplied;
            if (capabilitiesConverged && targetedCapabilityReport && staleVJoyCapabilityInspections > 0) {
                --staleVJoyCapabilityInspections;
                capabilitiesConverged = false;
            }
            const QString report = vjoy219HumanReadableOutput
                ? QStringLiteral("Device 1 FREE\nButtons %1\nContinous POVs 0\nDescrete POVs 0\nAxes X Y Z Rx Ry Rz Sl0 Sl1\nFFB All Effects\n")
                      .arg(capabilitiesConverged ? 32 : 15)
                : QStringLiteral("Device: 1\nState: FREE\nButtons: %1\nContinous POVs: 0\nDescrete POVs: 0\nAxes: X Y Z Rx Ry Rz Sl0 Sl1\nFFB Effects: None\n")
                      .arg(capabilitiesConverged ? 32 : 4);
            return {true, true, 0, report, {}};
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
    void knownControllerArrivalUsesStableIdentity();
    void activeInputReportsArePhysicalHealthEvidence();
    void processRunnerRollbackOnlyReversesThisTransaction();
    void repairCompletesWithElevatedUtilitiesAndVerifies();
    void repairWaitsForDelayedVJoyCapabilityPublication();
    void reconnectCycleRequiresObservedDisconnectAndLiveReports();
    void selfAccessFailureRollsBackBeforeReportingReady();
    void failedReacquisitionRequestsReconnectInsteadOfReady();
    void failedRecoveryReportsRollbackFailure();
    void diagnosticsAreScopedSanitizedAndCopyable();
    void uacCancellationIsNotReportedAsRepairFailure();
    void requirementsCoverProfilesAutomationAndExtendedAxes();
    void canonicalSignalFlowFanOutContributesOutputRequirements();
    void buttonCapacityUsesMappedRoutesRatherThanProvisionedLayout();
    void virtualAxisCapabilitySupersetIsReady();
    void vjoyShortSliderAliasesRemainReady();
    void scopedHidHideRepairPreservesVJoyOperationScope();
    void validVJoySupersetCannotDisagreeWithAggregateHealth();
    void staleVJoyPlanBecomesReadyImmediatelyAfterCorrection();
    void currentCandidateExecutableMustBeAllowlistedAndReadBack();
    void physicalControllerContainerIdentitySurvivesReenumeration();
    void automaticRepairConvergesWithoutChangingUnrelatedHidHideRules();
    void savedControllerVjoyRequirementsDetectInsufficientOutput();
    void managedVirtualOutputIdentityRequiresExactEnumeratedVjoy();
    void managedVirtualOutputsSwitchWithoutElevationAndRollBackOnFailure();
    void managedPhysicalInputsRequireExactIdentityAndRollBackOnFailure();
    void vjoy219HumanReadableOutputIsParsedAsHealthyDescriptor();
};

void ControllerReadinessTests::alreadyCorrectVJoyNeedsNoChange()
{
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QVERIFY(!plan.hidhideNeedsChanges);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
}

void ControllerReadinessTests::vjoy219HumanReadableOutputIsParsedAsHealthyDescriptor()
{
    auto fake = std::make_unique<FakeRunner>();
    fake->vjoy219HumanReadableOutput = true;
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperConfiguration configuration = defaultConfiguration();
    service.inspect(configuration, connectedController(), VerificationMode::Full);
    const VJoyCapabilities &vjoy = service.plan().vjoy;
    QVERIFY(vjoy.inspectionComplete);
    QVERIFY(vjoy.devicePresent);
    QVERIFY(vjoy.driverReady);
    QCOMPARE(vjoy.buttons, 15);
    QVERIFY(vjoy.axes[static_cast<size_t>(VirtualAxis::Rz)]);
    QVERIFY(vjoy.forceFeedbackKnown);
    QVERIFY(vjoy.forceFeedbackEffects.contains(QStringLiteral("all")));
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
    QVERIFY(!plan.vjoyNeedsChanges);
    QVERIFY(!plan.vjoyCanApply);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Attention);
    QCOMPARE(plan.state, ControllerReadinessState::Attention);
    QVERIFY(plan.vjoySummary.contains(QStringLiteral("another application")));
    QVERIFY(plan.vjoySummary.contains(QStringLiteral("capabilities are correct")));
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

void ControllerReadinessTests::knownControllerArrivalUsesStableIdentity()
{
    PhysicalControllerCapabilities verified = connectedController();
    verified.hidContainerId = QStringLiteral("{F6B6CF3A-8C6D-4A8A-9821-123456789ABC}");

    SavedControllerRecord remembered;
    remembered.id = QStringLiteral("remembered-hotas");
    remembered.lastDirectInputId = verified.directInputId;
    remembered.hidInstanceId = verified.hidInstanceId;
    remembered.hidContainerId = verified.hidContainerId;

    PhysicalControllerCapabilities reenumerated = verified;
    reenumerated.directInputId = QStringLiteral("{new-directinput-guid}");
    reenumerated.hidInstanceId = QStringLiteral("HID\\VID_044F&PID_B68D\\re-enumerated-interface");

    QVERIFY(ControllerReadinessService::isKnownPhysicalController(reenumerated, {remembered}));

    const ControllerReadinessPlan ready = ControllerReadinessService::planFor(
        reenumerated, defaultRequirements(), readyVJoy(), readyHidHide(), VerificationMode::Quick);
    QVERIFY(!ControllerReadinessService::needsSetupAfterControllerArrival(true, ready));

    reenumerated.hidContainerId = QStringLiteral("{C4CE6D3A-3A34-4F8B-80E1-987654321ABC}");
    QVERIFY(!ControllerReadinessService::isKnownPhysicalController(reenumerated, {remembered}));
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
    QVERIFY(std::any_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("helper:"))
            && call.contains(QStringLiteral("--dev-hide"))
            && call.contains(QStringLiteral("HID\\VID_044F&PID_B68D\\exact-instance"), Qt::CaseInsensitive);
    }));
    QVERIFY(containsCanonicalApplicationPath(probe->elevatedPrograms));
    QCOMPARE(service.lastAutomaticRepairResult().outcome, AutomaticRepairOutcome::Failed);
    QVERIFY(service.lastAutomaticRepairResult().message.contains(QStringLiteral("HidHide device repair failed")));
    QVERIFY(service.lastAutomaticRepairResult().message.contains(QStringLiteral("code 5")));
    QVERIFY(!service.plan().status.contains(QStringLiteral("failed: ."), Qt::CaseInsensitive));
}

void ControllerReadinessTests::repairCompletesWithElevatedUtilitiesAndVerifies()
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
    QVERIFY(containsCanonicalApplicationPath(probe->elevatedPrograms));
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

void ControllerReadinessTests::canonicalSignalFlowFanOutContributesOutputRequirements()
{
    MapperConfiguration configuration = defaultConfiguration();
    const QString profileId = configuration.profiles.front().id;
    configuration.signalFlow.topologyVersion = 1;
    SignalFlowRoute axis;
    axis.profileId = profileId;
    axis.sourceKind = SignalFlowPortKind::Axis;
    axis.sourceIndex = 0;
    axis.destinationKind = SignalFlowPortKind::Axis;
    axis.destinationIndex = static_cast<int>(VirtualAxis::Slider1);
    SignalFlowRoute button;
    button.profileId = profileId;
    button.sourceKind = SignalFlowPortKind::Button;
    button.sourceIndex = 0;
    button.destinationKind = SignalFlowPortKind::Button;
    button.destinationIndex = 47;
    SignalFlowRoute continuousPov;
    continuousPov.profileId = profileId;
    continuousPov.sourceKind = SignalFlowPortKind::NativePov;
    continuousPov.sourceIndex = 0;
    continuousPov.destinationKind = SignalFlowPortKind::NativePov;
    continuousPov.destinationIndex = 1;
    continuousPov.destinationSubIndex = static_cast<int>(NativePovTargetType::Continuous);
    SignalFlowRoute discretePov = continuousPov;
    discretePov.destinationIndex = 2;
    discretePov.destinationSubIndex = static_cast<int>(NativePovTargetType::Discrete);
    configuration.signalFlow.routes = {axis, button, continuousPov, discretePov};

    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(configuration);
    QVERIFY(requirements.axes[static_cast<int>(VirtualAxis::Slider1)]);
    QCOMPARE(requirements.buttons, 47);
    QCOMPARE(requirements.continuousPovs, 1);
    QCOMPARE(requirements.discretePovs, 2);
    QVERIFY(requirements.incompatiblePovMix);
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

void ControllerReadinessTests::virtualAxisCapabilitySupersetIsReady()
{
    ControllerVJoyRequirements saved;
    saved.deviceId = 1;
    saved.axes[1] = saved.axes[2] = saved.axes[3] = saved.axes[6] = true;
    saved.buttons = 15;
    saved.continuousPovs = 1;
    const MapperOutputRequirements requirements = ControllerReadinessService::requirementsFor(saved);
    VJoyCapabilities vjoy = readyVJoy();
    // The recommended full vJoy descriptor is a valid capability superset of
    // an older four-axis profile requirement.
    vjoy.buttons = 32; // Button capacity remains a minimum.
    vjoy.continuousPovs = 1;
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), requirements, vjoy, readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
    QVERIFY(plan.vjoySummary.contains(
        QStringLiteral("Extra available axes: Rx, Ry, Slider 0, Slider 1.")));
}

void ControllerReadinessTests::vjoyShortSliderAliasesRemainReady()
{
    auto fake = std::make_unique<FakeRunner>();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    fake->repairApplied = true;
    ControllerReadinessService service(std::move(fake), utilities);
    MapperOutputRequirements requirements = defaultRequirements();
    requirements.axes[static_cast<size_t>(VirtualAxis::Slider0)] = true;
    requirements.axes[static_cast<size_t>(VirtualAxis::Slider1)] = true;
    const ControllerReadinessPlan &plan = service.inspectForRequirements(
        defaultConfiguration(), connectedController(), requirements);
    QVERIFY(!plan.vjoyNeedsChanges);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Ready);
}

void ControllerReadinessTests::scopedHidHideRepairPreservesVJoyOperationScope()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);
    service.inspect(defaultConfiguration(), connectedController(), VerificationMode::Full);
    QVERIFY(service.plan().hidhideNeedsChanges);
    QVERIFY(service.plan().hidhideCanApply);
    QVERIFY(service.applyHidHideConfiguration());
    QVERIFY(!service.plan().hidhideNeedsChanges);
    QVERIFY(std::any_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("helper:"))
            && call.contains(QStringLiteral("--dev-hide HID\\VID_044F&PID_B68D\\exact-instance"),
                             Qt::CaseInsensitive);
    }));
    QVERIFY(std::none_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("helper:")) && call.contains(QStringLiteral(" -f -a "));
    }));
}

void ControllerReadinessTests::validVJoySupersetCannotDisagreeWithAggregateHealth()
{
    MapperOutputRequirements requirements;
    requirements.axes[static_cast<size_t>(VirtualAxis::X)] = true;
    requirements.axes[static_cast<size_t>(VirtualAxis::Y)] = true;
    requirements.axes[static_cast<size_t>(VirtualAxis::Z)] = true;
    requirements.axes[static_cast<size_t>(VirtualAxis::Rz)] = true;
    requirements.buttons = 15;
    VJoyCapabilities vjoy = readyVJoy();
    vjoy.buttons = 30;
    // The real Device 1 descriptor is a valid eight-axis capability superset.
    const ControllerReadinessPlan plan = ControllerReadinessService::planFor(
        connectedController(), requirements, vjoy, readyHidHide());
    QVERIFY(!plan.vjoyNeedsChanges);
    QCOMPARE(plan.vjoyStatus, VerificationSubsystemState::Ready);
    QCOMPARE(plan.state, ControllerReadinessState::Ready);
}

void ControllerReadinessTests::staleVJoyPlanBecomesReadyImmediatelyAfterCorrection()
{
    VJoyCapabilities insufficient = readyVJoy();
    insufficient.axes[static_cast<size_t>(VirtualAxis::Slider1)] = false;
    MapperOutputRequirements requirements = defaultRequirements();
    requirements.axes[static_cast<size_t>(VirtualAxis::Slider1)] = true;
    const ControllerReadinessPlan invalid = ControllerReadinessService::planFor(
        connectedController(), requirements, insufficient, readyHidHide());
    QCOMPARE(invalid.vjoyStatus, VerificationSubsystemState::Error);

    VJoyCapabilities corrected = readyVJoy();
    corrected.buttons = 30;
    const ControllerReadinessPlan ready = ControllerReadinessService::planFor(
        connectedController(), requirements, corrected, readyHidHide());
    QVERIFY(!ready.vjoyNeedsChanges);
    QCOMPARE(ready.vjoyStatus, VerificationSubsystemState::Ready);
    QCOMPARE(ready.state, ControllerReadinessState::Ready);
}

void ControllerReadinessTests::currentCandidateExecutableMustBeAllowlistedAndReadBack()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    // A production install entry is not proof that this candidate executable
    // has HidHide access.
    probe->cloakEnabled = true;
    probe->allowlistedApplications = {QStringLiteral("C:/Program Files/HOTAS BF6/HOTAS BF6.exe")};
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);

    const MapperConfiguration configuration = defaultConfiguration();
    const ControllerReadinessPlan &blocked = service.inspect(configuration, connectedController());
    QVERIFY(!blocked.hidhide.mapperAllowlisted);
    QCOMPARE(blocked.hidhideStatus, VerificationSubsystemState::Error);
    QVERIFY(blocked.hidhideCanApply);
    QVERIFY(service.allowlistMapperOnly());
    QVERIFY(service.plan().hidhide.mapperAllowlisted);
    QVERIFY(service.plan().hidhide.allowlistedApplications.contains(
        QCoreApplication::applicationFilePath(), Qt::CaseInsensitive));
    QVERIFY(service.plan().hidhide.allowlistedApplications.contains(
        QStringLiteral("C:/Program Files/HOTAS BF6/HOTAS BF6.exe"), Qt::CaseInsensitive));
    QVERIFY(std::any_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("elevated:--app-reg "));
    }));
}

void ControllerReadinessTests::physicalControllerContainerIdentitySurvivesReenumeration()
{
    PhysicalControllerCapabilities before = connectedController();
    before.hidContainerId = QStringLiteral("{F6B6CF3A-8C6D-4A8A-9821-123456789ABC}");
    PhysicalControllerCapabilities after = before;
    after.hidInstanceId = QStringLiteral("HID\\VID_044F&PID_B68D\\re-enumerated-interface");
    QVERIFY(ControllerReadinessService::samePhysicalController(before, after));

    // A different container is never silently treated as the selected HOTAS,
    // even if it happens to share a product family.
    after.hidContainerId = QStringLiteral("{C4CE6D3A-3A34-4F8B-80E1-987654321ABC}");
    QVERIFY(!ControllerReadinessService::samePhysicalController(before, after));
}

void ControllerReadinessTests::automaticRepairConvergesWithoutChangingUnrelatedHidHideRules()
{
    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    probe->hiddenDevices = {QStringLiteral("HID\\VID_9999&PID_0001\\unrelated-controller")};
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.vjoyConfig = QStringLiteral("fake-vJoyConfig.exe");
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;
    ControllerReadinessService service(std::move(fake), utilities);

    MapperConfiguration configuration = defaultConfiguration();
    const ControllerReadinessPlan &before = service.inspect(configuration, connectedController());
    QVERIFY(before.canApplyAutomatically);
    QVERIFY(service.applyAutomatically());
    service.completePhysicalAccessVerification(true, true);
    QCOMPARE(service.plan().physicalStatus, VerificationSubsystemState::Ready);
    QCOMPARE(service.plan().vjoyStatus, VerificationSubsystemState::Ready);
    QCOMPARE(service.plan().hidhideStatus, VerificationSubsystemState::Ready);
    QCOMPARE(service.plan().state, ControllerReadinessState::Ready);
    QVERIFY(probe->hiddenDevices.contains(
        QStringLiteral("HID\\VID_9999&PID_0001\\unrelated-controller")));
    QVERIFY(!std::any_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.contains(QStringLiteral("--dev-unhide HID\\VID_9999&PID_0001"));
    }));
    const QString elevatedCommands = probe->calls.join(u'\n');
    QVERIFY(elevatedCommands.contains(QStringLiteral("VID_044F&PID_B68D")));
    QVERIFY(!elevatedCommands.contains(QStringLiteral("VID_1234&PID_BEAD")));
    QVERIFY(containsCanonicalApplicationPath(probe->elevatedPrograms));
}

void ControllerReadinessTests::reconnectCycleRequiresObservedDisconnectAndLiveReports()
{
    ControllerReadinessService service;
    const ControllerReadinessPlan initial = ControllerReadinessService::planFor(
        connectedController(), defaultRequirements(), readyVJoy(), readyHidHide());
    service.adoptPlan(initial);
    QCOMPARE(initial.state, ControllerReadinessState::Ready);

    service.beginPhysicalReconnectVerification();
    QVERIFY(service.reconnectVerificationPending());
    QVERIFY(!service.reconnectDisconnectObserved());
    QCOMPARE(service.plan().state, ControllerReadinessState::Verifying);
    QVERIFY(service.plan().status.contains(QStringLiteral("Unplug")));

    // A retained DirectInput handle is deliberately insufficient proof.
    QVERIFY(!service.observePhysicalReconnect(true, true, true));
    QVERIFY(service.reconnectVerificationPending());

    QVERIFY(service.observePhysicalReconnect(false, false, false));
    QVERIFY(service.reconnectDisconnectObserved());
    QVERIFY(service.plan().physicalSummary.contains(QStringLiteral("disconnected ✓")));

    QVERIFY(service.observePhysicalReconnect(true, false, true));
    QVERIFY(service.reconnectVerificationPending());
    QVERIFY(service.plan().physicalSummary.contains(QStringLiteral("not the selected")));

    QVERIFY(service.observePhysicalReconnect(true, true, false));
    QVERIFY(service.reconnectVerificationPending());
    QVERIFY(service.plan().physicalSummary.contains(QStringLiteral("Waiting for a live input report")));

    QVERIFY(service.observePhysicalReconnect(true, true, true));
    QVERIFY(!service.reconnectVerificationPending());
    QVERIFY(service.reconnectReconciliationPending());
    QCOMPARE(service.plan().state, ControllerReadinessState::Verifying);
    QCOMPARE(service.plan().hidhideStatus, VerificationSubsystemState::Checking);
    QVERIFY(service.plan().status.contains(QStringLiteral("reconcil"), Qt::CaseInsensitive));
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

void ControllerReadinessTests::managedPhysicalInputsRequireExactIdentityAndRollBackOnFailure()
{
    const QString primary = QStringLiteral("HID\\VID_044F&PID_B68D\\exact-instance");
    const QString secondary = QStringLiteral("HID\\VID_044F&PID_B68D\\second-interface");
    const QString normalizedPrimary = QStringLiteral("HID\\VID_044F&PID_B68D\\EXACT-INSTANCE");
    const QString normalizedSecondary = QStringLiteral("HID\\VID_044F&PID_B68D\\SECOND-INTERFACE");
    SetupUtilityPaths utilities;
    utilities.supplied = true;
    utilities.hidhideCli = QStringLiteral("fake-HidHideCLI.exe");
    utilities.hidhideServiceReady = true;

    auto fake = std::make_unique<FakeRunner>();
    FakeRunner *probe = fake.get();
    // Keep the fake's visibility list literal for this transaction. Its
    // repair fixture otherwise synthesizes one unrelated default hidden HID.
    probe->cloakEnabled = true;
    probe->allowlistedApplications = {QCoreApplication::applicationFilePath()};
    probe->gamingDevices = {primary, secondary};
    ControllerReadinessService service(std::move(fake), utilities);
    QStringList normalized;
    QString status;
    QVERIFY(service.validateManagedPhysicalInputIdentities({primary, secondary}, &normalized, &status));
    QCOMPARE(normalized, QStringList({normalizedPrimary, normalizedSecondary}));
    QVERIFY(!service.validateManagedPhysicalInputIdentities(
        {primary, QStringLiteral("HID\\VID_1234&PID_BEAD\\vJoy")}, &normalized, &status));
    QVERIFY(status.contains(QStringLiteral("non-vJoy"), Qt::CaseInsensitive));

    const ManagedVisibilityTransactionResult hidden = service.applyManagedPhysicalInputVisibility(
        {primary, secondary}, true);
    QVERIFY(hidden.succeeded);
    QVERIFY(hidden.changed);
    QVERIFY(probe->hiddenDevices.contains(primary, Qt::CaseInsensitive));
    QVERIFY(probe->hiddenDevices.contains(secondary, Qt::CaseInsensitive));
    QVERIFY(std::none_of(probe->calls.cbegin(), probe->calls.cend(), [](const QString &call) {
        return call.startsWith(QStringLiteral("elevated:"));
    }));

    auto failingFake = std::make_unique<FakeRunner>();
    FakeRunner *failingProbe = failingFake.get();
    failingProbe->cloakEnabled = true;
    failingProbe->allowlistedApplications = {QCoreApplication::applicationFilePath()};
    failingProbe->gamingDevices = {primary, secondary};
    failingProbe->hiddenDevices = {primary, secondary};
    failingProbe->failRuntimeSecondVisibilityOperation = true;
    ControllerReadinessService failingService(std::move(failingFake), utilities);
    const ManagedVisibilityTransactionResult shown = failingService.applyManagedPhysicalInputVisibility(
        {primary, secondary}, false);
    QVERIFY(!shown.succeeded);
    QVERIFY(shown.status.contains(QStringLiteral("rolled back"), Qt::CaseInsensitive));
    QVERIFY(failingProbe->calls.contains(QStringLiteral("--dev-unhide ") + normalizedPrimary));
    QVERIFY(failingProbe->calls.contains(QStringLiteral("--dev-unhide ") + normalizedSecondary));
    QVERIFY(failingProbe->calls.contains(QStringLiteral("--dev-hide ") + normalizedPrimary));
    QVERIFY(failingProbe->hiddenDevices.contains(primary, Qt::CaseInsensitive));
    QVERIFY(failingProbe->hiddenDevices.contains(secondary, Qt::CaseInsensitive));
}

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    ControllerReadinessTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "controller_readiness_tests.moc"
