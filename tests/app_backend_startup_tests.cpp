#include "app_backend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace {

bool verifyActivationTransactionFaults()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString targetProfile = hotas::precisionProfileId();
    const QString baselineProfile = hotas::normalProfileId();
    const QString baselineRig = QStringLiteral("activation-transaction-rig");
    const QString targetRig = QStringLiteral("activation-transaction-alternate-rig");
    const auto retainsBaseline = [&backend, &baselineProfile, &baselineRig]() {
        return backend->activeProfileId() == baselineProfile
            && backend->activeDeviceRigId() == baselineRig;
    };
    for (const QString &stage : {QStringLiteral("prepare"), QStringLiteral("visibility"),
                                 QStringLiteral("persist"), QStringLiteral("reacquire")}) {
        if (!backend->configureActivationTransactionFixtureForTest()) {
            std::fprintf(stderr, "activation transaction fixture could not be configured\n");
            return false;
        }
        backend->setActivationFaultInjectionsForTest({stage});
        if (backend->activateProfile(targetProfile) || !retainsBaseline()
            || backend->activationResolverState().value(QStringLiteral("degraded")).toBool()) {
            std::fprintf(stderr, "activation transaction did not retain the prior route at %s\n",
                         stage.toUtf8().constData());
            return false;
        }
    }
    if (!backend->configureActivationTransactionFixtureForTest()) return false;
    backend->setActivationFaultInjectionsForTest({QStringLiteral("reacquire"),
                                                   QStringLiteral("rollback")});
    if (backend->activateProfile(targetProfile) || !retainsBaseline()
        || !backend->activationResolverState().value(QStringLiteral("degraded")).toBool()) {
        std::fprintf(stderr, "activation transaction did not expose the injected degraded rollback state\n");
        return false;
    }
    if (!backend->configureActivationTransactionFixtureForTest()) return false;
    backend->setActivationFaultInjectionsForTest({});
    if (!backend->activateProfile(targetProfile)
        || backend->activeProfileId() != targetProfile
        || backend->activeDeviceRigId() != targetRig
        || backend->activationResolverState().value(QStringLiteral("degraded")).toBool()) {
        std::fprintf(stderr, "activation transaction normal path did not commit the full route\n");
        return false;
    }
    return true;
}

bool verifyManualRigUsesRigOwnedOutputTransaction()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString rigId = QStringLiteral("activation-transaction-rig");
    const QString outputId = QStringLiteral("activation-transaction-output");
    if (!backend->configureRigOwnedOutputFixtureForTest()) {
        std::fprintf(stderr, "Rig-owned output fixture could not be configured\n");
        return false;
    }
    const QVariantMap result = backend->activateDeviceRigResult(rigId);
    if (!result.value(QStringLiteral("success")).toBool()
        || result.value(QStringLiteral("profileId")).toString() != hotas::normalProfileId()
        || result.value(QStringLiteral("deviceRigId")).toString() != rigId
        || result.value(QStringLiteral("outputLayoutId")).toString() != outputId
        || backend->activeProfileId() != hotas::normalProfileId()
        || backend->activeDeviceRigId() != rigId
        || backend->vjoyDeviceId() != 2) {
        std::fprintf(stderr, "manual Rig activation did not commit its Rig-owned coherent route\n");
        return false;
    }
    for (const QVariant &entry : backend->profiles()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("id")).toString() == hotas::normalProfileId()
            && profile.value(QStringLiteral("outputLayoutId")).toString() == outputId) {
            return true;
        }
    }
    std::fprintf(stderr, "manual Rig activation did not expose its Rig primary output\n");
    return false;
}

bool verifyManualProfileUsesRigOwnedOutputTransaction()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString profileId = hotas::normalProfileId();
    const QString rigId = QStringLiteral("activation-transaction-rig");
    const QString outputId = QStringLiteral("activation-transaction-output");
    if (!backend->configureRigOwnedOutputFixtureForTest()) {
        std::fprintf(stderr, "Rig-owned output fixture could not be configured\n");
        return false;
    }
    const QVariantMap result = backend->activateProfileResult(profileId);
    if (!result.value(QStringLiteral("success")).toBool()
        || result.value(QStringLiteral("intent")).toString() != QStringLiteral("manual-profile")
        || result.value(QStringLiteral("requestedProfileId")).toString() != profileId
        || result.value(QStringLiteral("profileId")).toString() != profileId
        || result.value(QStringLiteral("deviceRigId")).toString() != rigId
        || result.value(QStringLiteral("outputLayoutId")).toString() != outputId
        || backend->activeProfileId() != profileId
        || backend->activeDeviceRigId() != rigId
        || backend->vjoyDeviceId() != 2) {
        std::fprintf(stderr, "manual Profile activation did not commit its Rig-owned coherent route\n");
        return false;
    }
    const QVariantMap precision = backend->activateProfileResult(hotas::precisionProfileId());
    if (!precision.value(QStringLiteral("success")).toBool()
        || backend->activeProfileId() != hotas::precisionProfileId()
        || backend->activeDeviceRigId() != rigId
        || backend->vjoyDeviceId() != 2) {
        std::fprintf(stderr, "same-Rig Profile switch did not retain the Rig-owned vJoy 2 route\n");
        return false;
    }
    const QVariantMap normal = backend->activateProfileResult(profileId);
    if (!normal.value(QStringLiteral("success")).toBool()
        || backend->activeProfileId() != profileId
        || backend->activeDeviceRigId() != rigId
        || backend->vjoyDeviceId() != 2) {
        std::fprintf(stderr, "same-Rig switch back did not retain the Rig-owned vJoy 2 route\n");
        return false;
    }
    for (const QVariant &entry : backend->profiles()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("id")).toString() == profileId
            && profile.value(QStringLiteral("outputLayoutId")).toString() == outputId) {
            return true;
        }
    }
    std::fprintf(stderr, "manual Profile activation did not expose its Rig primary output\n");
    return false;
}

bool verifyViewedProfileUsesRigOwnedOutputForMappingEdits()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureSetupTruthReadyToActivateFixtureForTest()
        || !backend->activeDeviceRigId().isEmpty()) {
        std::fprintf(stderr, "viewed-Profile mapping fixture did not preserve inactive Rig state\n");
        return false;
    }

    // This is the exact VIEWING-versus-ACTIVE seam: the Profile's selected
    // Rig owns its vJoy 2 output, but no Rig is yet active.  Editing remains
    // valid and must not silently activate the Rig.
    backend->setVirtualAxisAvailabilityForTest(true);
    if (!backend->setMapping(0, QStringLiteral("X"), true)
        || !backend->activeDeviceRigId().isEmpty()) {
        std::fprintf(stderr,
            "a viewed Profile could not write through its Device Rig primary output without activating it\n");
        return false;
    }
    const QVariantList axes = backend->axes();
    if (axes.isEmpty() || axes.front().toMap().value(QStringLiteral("target")).toString()
            != QStringLiteral("X")) {
        std::fprintf(stderr, "viewed-Profile mapping edit did not persist its selected virtual axis\n");
        return false;
    }
    return true;
}

bool verifySetupTruthReadyToActivateCompletion()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString rigId = QStringLiteral("activation-transaction-rig");
    const auto mappingGroup = [](const QVariantMap &snapshot) {
        for (const QVariant &entry : snapshot.value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = entry.toMap();
            if (group.value(QStringLiteral("id")).toString() == QStringLiteral("mapping")) return group;
        }
        return QVariantMap{};
    };
    if (!backend->configureSetupTruthReadyToActivateFixtureForTest()) {
        std::fprintf(stderr, "ready-to-activate setup truth fixture could not be configured\n");
        return false;
    }

    const QVariantMap before = backend->setupTruthSnapshot();
    const QVariantMap beforeMapping = mappingGroup(before);
    const QVariantList beforeActions = before.value(QStringLiteral("manualActions")).toList();
    const bool checkComplete = backend->completeSetupCheck().value(QStringLiteral("success")).toBool();
    if (before.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || beforeMapping.value(QStringLiteral("status")).toString() != QStringLiteral("READY TO ACTIVATE")
        || beforeActions.size() != 1
        || beforeActions.front().toMap().value(QStringLiteral("rigId")).toString() != rigId
        || !checkComplete) {
        std::fprintf(stderr,
            "healthy viewed Rig did not produce expected action: overall=%s mapping=%s actions=%d actionRig=%s complete=%d\n",
            before.value(QStringLiteral("overallStatus")).toString().toUtf8().constData(),
            beforeMapping.value(QStringLiteral("status")).toString().toUtf8().constData(),
            static_cast<int>(beforeActions.size()), beforeActions.isEmpty() ? "" : beforeActions.front().toMap()
                .value(QStringLiteral("rigId")).toString().toUtf8().constData(), checkComplete ? 1 : 0);
        return false;
    }

    const QVariantMap activated = backend->activateSetupTruthDeviceRig(rigId);
    const QVariantMap after = activated.value(QStringLiteral("setupTruth")).toMap();
    const QVariantMap afterMapping = mappingGroup(after);
    const QVariantMap session = backend->setupRepairSession();
    if (!activated.value(QStringLiteral("success")).toBool()
        || backend->activeDeviceRigId() != rigId
        || after.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || afterMapping.value(QStringLiteral("status")).toString() != QStringLiteral("READY")
        || !after.value(QStringLiteral("manualActions")).toList().isEmpty()
        || !session.value(QStringLiteral("afterSnapshot")).toMap().value(QStringLiteral("manualActions")).toList().isEmpty()
        || session.value(QStringLiteral("mode")).toString() != QStringLiteral("COMPLETE")) {
        std::fprintf(stderr, "Setup Complete did not refresh to the activated Profile, Rig, and output state\n");
        return false;
    }
    return true;
}

bool verifyStartupSetupTruthPublication()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const auto groupStatus = [](const QVariantMap &snapshot, const QString &id) {
        for (const QVariant &entry : snapshot.value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = entry.toMap();
            if (group.value(QStringLiteral("id")).toString() == id)
                return group.value(QStringLiteral("status")).toString();
        }
        return QString{};
    };
    const QVariantMap launchSnapshot = backend->setupTruthSnapshot();
    if (!backend->startupSetupTruthInspectionScheduledForTest()
        || launchSnapshot.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("CHECKING")) {
        std::fprintf(stderr, "normal startup did not schedule a typed CHECKING setup-truth inspection\n");
        return false;
    }
    if (!backend->configureStartupSetupTruthFixtureForTest()) {
        std::fprintf(stderr, "startup setup truth fixture could not be configured\n");
        return false;
    }

    const QVariantMap before = backend->setupTruthSnapshot();
    const QVariantMap beforeSession = backend->setupRepairSession();
    if (before.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("CHECKING")
        || groupStatus(before, QStringLiteral("physical")) != QStringLiteral("CHECKING")
        || groupStatus(before, QStringLiteral("vjoy")) != QStringLiteral("CHECKING")
        || groupStatus(before, QStringLiteral("isolation")) != QStringLiteral("CHECKING")
        || beforeSession.value(QStringLiteral("active")).toBool()
        || beforeSession.value(QStringLiteral("mode")).toString() != QStringLiteral("IDLE")) {
        std::fprintf(stderr, "startup setup truth did not publish CHECKING without opening a repair session\n");
        return false;
    }

    if (!backend->finishStartupSetupTruthInspectionForTest()) {
        std::fprintf(stderr, "startup setup truth fixture did not finish its passive inspection\n");
        return false;
    }
    const QVariantMap after = backend->setupTruthSnapshot();
    const QVariantMap afterSession = backend->setupRepairSession();
    if (after.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || groupStatus(after, QStringLiteral("physical")) != QStringLiteral("READY")
        || groupStatus(after, QStringLiteral("vjoy")) != QStringLiteral("READY")
        || groupStatus(after, QStringLiteral("isolation")) != QStringLiteral("READY")
        || !after.value(QStringLiteral("fresh")).toBool()
        || afterSession.value(QStringLiteral("active")).toBool()
        || afterSession.value(QStringLiteral("mode")).toString() != QStringLiteral("IDLE")) {
        QVariantMap verificationGroup;
        for (const QVariant &entry : after.value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = entry.toMap();
            if (group.value(QStringLiteral("id")).toString() == QStringLiteral("verification")) {
                verificationGroup = group;
                break;
            }
        }
        const QVariantList verificationMembers = verificationGroup.value(QStringLiteral("evidence"))
            .toMap().value(QStringLiteral("members")).toList();
        const QVariantMap firstVerificationMember = verificationMembers.isEmpty()
            ? QVariantMap{} : verificationMembers.front().toMap();
        std::fprintf(stderr,
            "startup setup truth did not publish one fresh ready snapshot after passive inspection: overall=%s physical=%s verification=%s vjoy=%s isolation=%s mapping=%s fresh=%d sessionActive=%d sessionMode=%s memberVerified=%d lastVerified=%s\n",
            after.value(QStringLiteral("overallStatus")).toString().toUtf8().constData(),
            groupStatus(after, QStringLiteral("physical")).toUtf8().constData(),
            groupStatus(after, QStringLiteral("verification")).toUtf8().constData(),
            groupStatus(after, QStringLiteral("vjoy")).toUtf8().constData(),
            groupStatus(after, QStringLiteral("isolation")).toUtf8().constData(),
            groupStatus(after, QStringLiteral("mapping")).toUtf8().constData(),
            after.value(QStringLiteral("fresh")).toBool() ? 1 : 0,
            afterSession.value(QStringLiteral("active")).toBool() ? 1 : 0,
            afterSession.value(QStringLiteral("mode")).toString().toUtf8().constData(),
            firstVerificationMember.value(QStringLiteral("identityVerified")).toBool() ? 1 : 0,
            firstVerificationMember.value(QStringLiteral("lastVerified")).toString().toUtf8().constData());
        return false;
    }

    const QVariantMap activated = backend->activateDeviceRigResult(
        QStringLiteral("activation-transaction-rig"));
    const QVariantMap duringActivationRefresh = backend->setupTruthSnapshot();
    if (!activated.value(QStringLiteral("success")).toBool()
        || backend->activeDeviceRigId() != QStringLiteral("activation-transaction-rig")
        || duringActivationRefresh.value(QStringLiteral("overallStatus")).toString()
               != QStringLiteral("CHECKING")
        || !backend->finishStartupSetupTruthInspectionForTest()) {
        std::fprintf(stderr, "activation did not invalidate setup truth for a coalesced passive refresh\n");
        return false;
    }
    const QVariantMap afterActivation = backend->setupTruthSnapshot();
    if (groupStatus(afterActivation, QStringLiteral("mapping")) != QStringLiteral("READY")
        || !afterActivation.value(QStringLiteral("manualActions")).toList().isEmpty()
        || backend->setupRepairSession().value(QStringLiteral("mode")).toString() != QStringLiteral("IDLE")) {
        std::fprintf(stderr, "activation did not immediately refresh startup setup truth without a wizard session\n");
        return false;
    }

    auto failureBackend = std::make_unique<hotas::AppBackend>();
    if (!failureBackend->configureStartupSetupTruthInspectionFailureForTest()) {
        std::fprintf(stderr, "startup inspection-failure fixture could not be configured\n");
        return false;
    }
    const QVariantMap failure = failureBackend->setupTruthSnapshot();
    if (!failure.value(QStringLiteral("fresh")).toBool()
        || failure.value(QStringLiteral("overallStatus")).toString()
               != QStringLiteral("UNKNOWN / INSPECTION FAILED")
        || groupStatus(failure, QStringLiteral("isolation"))
               != QStringLiteral("UNKNOWN / INSPECTION FAILED")) {
        std::fprintf(stderr, "a completed inspection failure was not projected as UNKNOWN\n");
        return false;
    }
    return true;
}

QVariantMap setupTruthGroup(const QVariantMap &snapshot, const QString &id)
{
    for (const QVariant &entry : snapshot.value(QStringLiteral("groups")).toList()) {
        const QVariantMap group = entry.toMap();
        if (group.value(QStringLiteral("id")).toString() == id) return group;
    }
    return {};
}

bool hasSetupTruthIssue(const QVariantMap &snapshot, const QString &code)
{
    const QVariantList issues = snapshot.value(QStringLiteral("issues")).toList();
    return std::any_of(issues.cbegin(), issues.cend(), [&code](const QVariant &entry) {
            return entry.toMap().value(QStringLiteral("code")).toString() == code;
        });
}

bool verifyReconnectLifecycleTruth()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureReconnectLifecycleFixtureForTest()) {
        std::fprintf(stderr, "reconnect lifecycle fixture could not be configured\n");
        return false;
    }
    const QVariantMap waiting = backend->setupTruthSnapshot();
    const QVariantMap verification = setupTruthGroup(waiting, QStringLiteral("verification"));
    const QVariantMap mapping = setupTruthGroup(waiting, QStringLiteral("mapping"));
    const QVariantList members = verification.value(QStringLiteral("evidence")).toMap()
        .value(QStringLiteral("members")).toList();
    const QVariantMap member = members.isEmpty() ? QVariantMap{} : members.front().toMap();
    const QVariantMap rigStatus = waiting.value(QStringLiteral("rigStatus")).toMap();
    const quint64 beforeInventory = waiting.value(QStringLiteral("inventoryGeneration")).toULongLong();
    if (verification.value(QStringLiteral("status")).toString() != QStringLiteral("READY")
        || mapping.value(QStringLiteral("status")).toString() != QStringLiteral("WAITING FOR USER")
        || !member.value(QStringLiteral("identityVerified")).toBool()
        || member.value(QStringLiteral("savedDirectInputId")).toString().isEmpty()
        || !waiting.value(QStringLiteral("activationContext")).toMap().contains(
            QStringLiteral("inventoryGeneration"))
        || !waiting.value(QStringLiteral("activationDecision")).toMap().contains(
            QStringLiteral("inventoryGeneration"))
        || rigStatus.value(QStringLiteral("needsVerificationRequiredMemberIds")).toStringList().size() != 0
        || hasSetupTruthIssue(waiting, QStringLiteral("RigActivationRouteInvalid"))) {
        std::fprintf(stderr, "reconnect wait did not preserve verification while exposing runtime acquisition\n");
        return false;
    }
    if (!backend->completeReconnectInventoryRefreshForTest()) {
        std::fprintf(stderr, "matching reconnect did not wait for the inventory refresh gate\n");
        return false;
    }
    const QVariantMap arrived = backend->setupTruthSnapshot();
    const QVariantMap arrivedRigStatus = arrived.value(QStringLiteral("rigStatus")).toMap();
    if (arrived.value(QStringLiteral("inventoryGeneration")).toULongLong() <= beforeInventory
        || setupTruthGroup(arrived, QStringLiteral("verification")).value(QStringLiteral("status")).toString()
            != QStringLiteral("READY")
        || setupTruthGroup(arrived, QStringLiteral("mapping")).value(QStringLiteral("status")).toString()
            != QStringLiteral("READY")
        || !arrivedRigStatus.value(QStringLiteral("needsVerificationRequiredMemberIds")).toStringList().isEmpty()
        || hasSetupTruthIssue(arrived, QStringLiteral("RigActivationRouteInvalid"))) {
        std::fprintf(stderr, "reconnect arrival did not rebuild an eligible RigStatus and mapping decision\n");
        return false;
    }
    return true;
}

bool verifyTargetedVJoyRepairPlan()
{
    const auto verify = [](const QString &rigId, int deviceId) {
        auto backend = std::make_unique<hotas::AppBackend>();
        if (!backend->configureTargetedVJoyRepairFixtureForTest(rigId, deviceId)) return false;
        const QVariantMap snapshot = backend->setupTruthSnapshot();
        const QVariantList issues = snapshot.value(QStringLiteral("issues")).toList();
        const QVariantList plan = snapshot.value(QStringLiteral("repairPlan")).toList();
        const auto issue = std::find_if(issues.cbegin(), issues.cend(), [](const QVariant &entry) {
            return entry.toMap().value(QStringLiteral("code")).toString()
                == QStringLiteral("VJoyDescriptorMismatch");
        });
        if (issue == issues.cend()) return false;
        const QVariantMap evidence = issue->toMap().value(QStringLiteral("evidence")).toMap();
        return !plan.isEmpty()
            && snapshot.value(QStringLiteral("setupTargetRigId")).toString() == rigId
            && evidence.value(QStringLiteral("setupTargetRigId")).toString() == rigId
            && evidence.value(QStringLiteral("deviceId")).toInt() == deviceId
            && setupTruthGroup(snapshot, QStringLiteral("vjoy")).value(QStringLiteral("status")).toString()
                == QStringLiteral("ACTION NEEDED");
    };
    if (!verify(QStringLiteral("activation-transaction-rig"), 1)
        || !verify(QStringLiteral("activation-transaction-alternate-rig"), 2)) {
        std::fprintf(stderr, "vJoy repair plan did not remain scoped to the selected Rig primary output\n");
        return false;
    }
    return true;
}

bool verifySidebarActivationLifecycle()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString helicopterProfileId = QStringLiteral("activation-transaction-helicopter");
    if (!backend->configureSidebarActivationFixtureForTest()) {
        std::fprintf(stderr, "sidebar activation fixture could not be configured\n");
        return false;
    }
    const QString rigId = backend->activeDeviceRigId();
    const int vjoyDeviceId = backend->vjoyDeviceId();
    const auto retainsTopology = [&backend, &rigId, vjoyDeviceId]() {
        return backend->activeDeviceRigId() == rigId && backend->vjoyDeviceId() == vjoyDeviceId;
    };
    if (backend->activeProfileId() != hotas::normalProfileId()
        || !backend->activateProfile(helicopterProfileId)
        || backend->activeProfileId() != helicopterProfileId
        || !retainsTopology()
        || !backend->activateProfile(hotas::precisionProfileId())
        || backend->activeProfileId() != hotas::precisionProfileId()
        || !retainsTopology()) {
        std::fprintf(stderr, "manual sidebar activation did not retain the active Rig topology\n");
        return false;
    }
    backend->setActivationFaultInjectionsForTest({QStringLiteral("persist")});
    if (backend->activateProfile(hotas::normalProfileId())
        || backend->activeProfileId() != hotas::precisionProfileId()
        || !retainsTopology()) {
        std::fprintf(stderr, "failed sidebar activation changed the committed route\n");
        return false;
    }
    backend->setActivationFaultInjectionsForTest({});
    if (!backend->applyAutomaticProfileActivationForTest(hotas::normalProfileId())
        || backend->activeProfileId() != hotas::normalProfileId()
        || !retainsTopology()) {
        std::fprintf(stderr, "automatic sidebar activation did not retain the active Rig topology\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("HOTAS_ENABLE_UI_PERFORMANCE_INSTRUMENTATION", "1");
    // Startup timing is measured against an isolated control plane.  A live
    // HidHide/vJoy inspection can block the owner machine and would turn this
    // presentation test into a driver-integration test.
    qputenv("HOTAS_DISABLE_EXTERNAL_SETUP_INSPECTION", "1");
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    application.setApplicationName(QStringLiteral("HOTAS Mapper"));
    // This suite exercises a known-good startup fixture.  A narrow recovery
    // journal is intentionally durable in production, so remove any record
    // left by another test process before the AppBackend constructors load
    // it.  Recovery semantics themselves are covered by readiness tests.
    QSettings testSettings;
    testSettings.remove(QStringLiteral("readiness/pendingAutomaticRepairRecovery"));
    testSettings.sync();

    hotas::AppBackend backend;
    if (!verifyStartupSetupTruthPublication()) return 1;
    if (!verifyActivationTransactionFaults()) return 1;
    if (!verifyManualRigUsesRigOwnedOutputTransaction()) return 1;
    if (!verifyManualProfileUsesRigOwnedOutputTransaction()) return 1;
    if (!verifyViewedProfileUsesRigOwnedOutputForMappingEdits()) return 1;
    if (!verifySetupTruthReadyToActivateCompletion()) return 1;
    if (!verifyReconnectLifecycleTruth()) return 1;
    if (!verifyTargetedVJoyRepairPlan()) return 1;
    if (!verifySidebarActivationLifecycle()) return 1;
    bool passed = false;
    // Let startup control-plane work settle before exercising the real
    // presentation lifecycle and then taking the visible steady-state sample.
    QTimer::singleShot(1500, &application, [&] {
        auto *lifecycleWindow = new QQuickWindow();
        backend.attachMainWindow(lifecycleWindow);
        lifecycleWindow->show();
        QCoreApplication::processEvents();
        const bool visibleLifecycle = backend.presentationState() == QStringLiteral("Visible")
            && backend.presentationSnapshotActive()
            && backend.presentationSnapshotIntervalMs() == 33
            && backend.controllerDiscoveryIntervalMs() == 2500;

        lifecycleWindow->showMinimized();
        QTimer::singleShot(0, &application, [&, lifecycleWindow, visibleLifecycle] {
            const bool minimizedLifecycle = backend.presentationState() == QStringLiteral("Minimized")
                && backend.presentationSnapshotActive()
                && backend.presentationSnapshotIntervalMs() == 250
                && backend.controllerDiscoveryIntervalMs() == 5000;
            const bool mappingWasRequested = backend.mappingRequested();
            backend.hideToTray();
            QTimer::singleShot(50, &application, [&, lifecycleWindow, visibleLifecycle,
                                                    minimizedLifecycle, mappingWasRequested] {
                backend.resetUiPerformanceCounters();
                const QVariantList controllerModel = backend.controllers();
                const QVariantList profiles = backend.profiles();
                const QVariantList categories = backend.profileCategories();
                Q_UNUSED(controllerModel);
                Q_UNUSED(profiles);
                Q_UNUSED(categories);
                QTimer::singleShot(450, &application, [&, lifecycleWindow, visibleLifecycle,
                                                        minimizedLifecycle, mappingWasRequested] {
                    const QVariantMap trayCounters = backend.uiPerformanceCounters();
                    const bool trayLifecycle = backend.presentationState() == QStringLiteral("TrayHidden")
                        && !backend.presentationSnapshotActive()
                        && backend.presentationSnapshotIntervalMs() == 0
                        && backend.controllerDiscoveryIntervalMs() == 7500
                        && backend.mappingRequested() == mappingWasRequested
                        && trayCounters.value(QStringLiteral("controllerGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("profileGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("categoryGetterCalls")).toULongLong() == 1
                        && trayCounters.value(QStringLiteral("controllerModelRebuilds")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("controllersChanged")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("telemetryChanged")).toULongLong() == 0
                        && trayCounters.value(QStringLiteral("inputTelemetryChanged")).toULongLong() == 0;

                    backend.restoreFromTray();
                    QTimer::singleShot(150, &application, [&, lifecycleWindow, visibleLifecycle,
                                                          minimizedLifecycle, trayLifecycle, mappingWasRequested] {
                        const QVariantMap restoredCounters = backend.uiPerformanceCounters();
                        const bool restoredLifecycle = backend.presentationState() == QStringLiteral("Visible")
                            && backend.presentationSnapshotActive()
                            && backend.presentationSnapshotIntervalMs() == 33
                            && backend.controllerDiscoveryIntervalMs() == 2500
                            && backend.mappingRequested() == mappingWasRequested
                            && restoredCounters.value(QStringLiteral("telemetryChanged")).toULongLong() >= 1
                            && restoredCounters.value(QStringLiteral("inputTelemetryChanged")).toULongLong() >= 1;
                        backend.setAutomaticGameDetection(false);
                        const bool gameDetectionStopsWhenDisabled = !backend.gameDetectionTimerActive();
                        backend.setAutomaticGameDetection(true);
                        const bool gameDetectionRunsWhenEnabled = backend.gameDetectionTimerActive();
                        delete lifecycleWindow;

                        if (!(visibleLifecycle && minimizedLifecycle && trayLifecycle
                              && restoredLifecycle && gameDetectionStopsWhenDisabled
                              && gameDetectionRunsWhenEnabled)) {
                            std::fprintf(stderr,
                                "presentation_lifecycle_visible=%d minimized=%d tray=%d restored=%d game_disabled=%d game_enabled=%d\n",
                                visibleLifecycle ? 1 : 0, minimizedLifecycle ? 1 : 0,
                                trayLifecycle ? 1 : 0, restoredLifecycle ? 1 : 0,
                                gameDetectionStopsWhenDisabled ? 1 : 0,
                                gameDetectionRunsWhenEnabled ? 1 : 0);
                            QCoreApplication::quit();
                            return;
                        }

                        backend.resetUiPerformanceCounters();
        // One explicit cached read establishes the getter baseline. During the
        // following ten seconds no telemetry refresh may invoke it again.
                        const QVariantList steadyControllerModel = backend.controllers();
                        const QVariantList steadyButtonModel = backend.buttons();
                        const QVariantList steadyProfiles = backend.profiles();
                        const QVariantList steadyCategories = backend.profileCategories();
                        const QVariantList steadyAxisConfiguration = backend.axisConfiguration();
                        const QVariantList steadyAxisTelemetry = backend.axisTelemetry();
                        bool axisProjectionSeparated = steadyAxisConfiguration.size() == steadyAxisTelemetry.size();
                        for (int index = 0; axisProjectionSeparated && index < steadyAxisConfiguration.size(); ++index) {
                            const QVariantMap configuration = steadyAxisConfiguration.at(index).toMap();
                            const QVariantMap telemetry = steadyAxisTelemetry.at(index).toMap();
                            axisProjectionSeparated = configuration.contains(QStringLiteral("label"))
                                && !configuration.contains(QStringLiteral("calibrated"))
                                && telemetry.contains(QStringLiteral("calibrated"))
                                && !telemetry.contains(QStringLiteral("label"))
                                && configuration.value(QStringLiteral("index")) == telemetry.value(QStringLiteral("index"));
                        }
                        Q_UNUSED(steadyControllerModel);
                        Q_UNUSED(steadyButtonModel);
                        Q_UNUSED(steadyProfiles);
                        Q_UNUSED(steadyCategories);
                        QTimer::singleShot(10'000, &application, [&, axisProjectionSeparated] {
            const QVariantMap counters = backend.uiPerformanceCounters();
            const qulonglong controllerGetterCalls = counters.value(QStringLiteral("controllerGetterCalls")).toULongLong();
            const qulonglong controllerRebuilds = counters.value(QStringLiteral("controllerModelRebuilds")).toULongLong();
            const qulonglong controllerNotifications = counters.value(QStringLiteral("controllersChanged")).toULongLong();
            const qulonglong buttonGetterCalls = counters.value(QStringLiteral("buttonGetterCalls")).toULongLong();
            const qulonglong buttonRebuilds = counters.value(QStringLiteral("buttonModelRebuilds")).toULongLong();
            const qulonglong profileGetterCalls = counters.value(QStringLiteral("profileGetterCalls")).toULongLong();
            const qulonglong categoryGetterCalls = counters.value(QStringLiteral("categoryGetterCalls")).toULongLong();
            const qulonglong telemetryNotifications = counters.value(QStringLiteral("telemetryChanged")).toULongLong();
            const qulonglong inputNotifications = counters.value(QStringLiteral("inputTelemetryChanged")).toULongLong();
            const qulonglong buttonNotifications = counters.value(QStringLiteral("buttonTelemetryChanged")).toULongLong();
            const qulonglong stateNotifications = counters.value(QStringLiteral("stateChanged")).toULongLong();
            const qulonglong controllerBackgroundRuns = counters.value(QStringLiteral("controllerDiscoveryBackgroundRuns")).toULongLong();
            const bool controllerDiscoveryTimerActive = counters.value(QStringLiteral("controllerDiscoveryTimerActive")).toBool();
            const qulonglong gameBackgroundRuns = counters.value(QStringLiteral("gameDetectionBackgroundRuns")).toULongLong();
            const qulonglong uiStallsOver250Ms = counters.value(QStringLiteral("uiEventLoopDelayOver250Ms")).toULongLong();
            std::fprintf(stderr,
                         "ui_steady_state_seconds=10 controller_getter_calls=%llu button_getter_calls=%llu profile_getter_calls=%llu category_getter_calls=%llu controller_model_rebuilds=%llu button_model_rebuilds=%llu controllers_changed=%llu "
                         "telemetry_changed=%llu input_telemetry_changed=%llu button_telemetry_changed=%llu state_changed=%llu controller_background_runs=%llu controller_discovery_timer_active=%d game_background_runs=%llu ui_stalls_over_250ms=%llu\n",
                         static_cast<unsigned long long>(controllerGetterCalls),
                         static_cast<unsigned long long>(buttonGetterCalls),
                         static_cast<unsigned long long>(profileGetterCalls),
                         static_cast<unsigned long long>(categoryGetterCalls),
                         static_cast<unsigned long long>(controllerRebuilds),
                         static_cast<unsigned long long>(buttonRebuilds),
                         static_cast<unsigned long long>(controllerNotifications),
                         static_cast<unsigned long long>(telemetryNotifications),
                         static_cast<unsigned long long>(inputNotifications),
                         static_cast<unsigned long long>(buttonNotifications),
                         static_cast<unsigned long long>(stateNotifications),
                         static_cast<unsigned long long>(controllerBackgroundRuns),
                         controllerDiscoveryTimerActive ? 1 : 0,
                         static_cast<unsigned long long>(gameBackgroundRuns),
                         static_cast<unsigned long long>(uiStallsOver250Ms));
            // Live analog presentation is capped near 30 Hz and numeric
            // telemetry near 10 Hz. Neither cached controller nor 128-button
            // structure may rebuild during that activity. The controller
            // scheduler may have been phase-reset by the tray restore while a
            // DirectInput call remains external and isolated; it must either
            // have sampled or remain active without stalling the UI heartbeat.
            passed = controllerGetterCalls == 1 && buttonGetterCalls == 1
                && profileGetterCalls == 1 && categoryGetterCalls == 1
                && controllerRebuilds == 0 && buttonRebuilds == 0 && controllerNotifications == 0
                && telemetryNotifications >= 80 && telemetryNotifications <= 130
                && inputNotifications >= 100 && inputNotifications <= 340
                && stateNotifications < telemetryNotifications / 4
                && (controllerBackgroundRuns >= 1 || controllerDiscoveryTimerActive)
                && gameBackgroundRuns >= 1
                && uiStallsOver250Ms == 0
                && axisProjectionSeparated;
            backend.setAutomaticGameDetection(false);
            QCoreApplication::quit();
        });
                    });
                });
            });
        });
    });
    const int eventLoopExitCode = application.exec();
    std::fprintf(stderr, "ui_steady_state_event_loop_exit=%d passed=%d\n", eventLoopExitCode, passed ? 1 : 0);
    return eventLoopExitCode == 0 && passed ? 0 : 1;
}
