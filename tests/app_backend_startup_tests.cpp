#include "app_backend.h"
#include "contention_resilience_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>
#include <utility>

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
        if (backend->activateProfileAfterRigSwitchConfirmation(targetProfile).value(QStringLiteral("success")).toBool()
            || !retainsBaseline()
            || backend->activationResolverState().value(QStringLiteral("degraded")).toBool()) {
            std::fprintf(stderr, "activation transaction did not retain the prior route at %s\n",
                         stage.toUtf8().constData());
            return false;
        }
    }
    if (!backend->configureActivationTransactionFixtureForTest()) return false;
    backend->setActivationFaultInjectionsForTest({QStringLiteral("reacquire"),
                                                   QStringLiteral("rollback")});
    if (backend->activateProfileAfterRigSwitchConfirmation(targetProfile).value(QStringLiteral("success")).toBool()
        || !retainsBaseline()
        || !backend->activationResolverState().value(QStringLiteral("degraded")).toBool()) {
        std::fprintf(stderr, "activation transaction did not expose the injected degraded rollback state\n");
        return false;
    }
    if (!backend->configureActivationTransactionFixtureForTest()) return false;
    backend->setActivationFaultInjectionsForTest({});
    if (!backend->activateProfileAfterRigSwitchConfirmation(targetProfile).value(QStringLiteral("success")).toBool()
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

bool verifyHealthyRigActivatesWithoutCompatibleProfile()
{
    constexpr auto kRigId = "activation-transaction-unmapped-rig";
    const auto mappingGroup = [](const QVariantMap &snapshot) {
        for (const QVariant &entry : snapshot.value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = entry.toMap();
            if (group.value(QStringLiteral("id")).toString() == QStringLiteral("mapping")) return group;
        }
        return QVariantMap{};
    };
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureUnmappedRigActivationFixtureForTest()) {
        std::fprintf(stderr, "unmapped Rig activation fixture could not be configured\n");
        return false;
    }
    const QVariantMap result = backend->activateDeviceRigResult(QLatin1String(kRigId));
    if (!result.value(QStringLiteral("success")).toBool()
        || !result.value(QStringLiteral("unmapped")).toBool()
        || !result.value(QStringLiteral("profileId")).toString().isEmpty()
        || backend->activeDeviceRigId() != QLatin1String(kRigId)
        || !backend->activeProfileId().isEmpty()
        || backend->activeProfileName() != QStringLiteral("No active Profile")) {
        std::fprintf(stderr, "healthy Rig did not become active in the safe unmapped state\n");
        return false;
    }
    const QVariantMap mapping = mappingGroup(backend->setupTruthSnapshot());
    if (mapping.value(QStringLiteral("status")).toString() != QStringLiteral("READY")
        || !mapping.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("unmapped")).toBool()
        || !mapping.value(QStringLiteral("detail")).toString().contains(QStringLiteral("no Profile mapped"))) {
        std::fprintf(stderr, "active unmapped Rig was incorrectly presented as a Profile activation fault: status=%s detail=%s unmapped=%d\n",
            qPrintable(mapping.value(QStringLiteral("status")).toString()),
            qPrintable(mapping.value(QStringLiteral("detail")).toString()),
            mapping.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("unmapped")).toBool() ? 1 : 0);
        return false;
    }
    for (const QVariant &entry : backend->deviceRigs()) {
        const QVariantMap rig = entry.toMap();
        if (rig.value(QStringLiteral("id")).toString() != QLatin1String(kRigId)) continue;
        if (!rig.value(QStringLiteral("active")).toBool()
            || !rig.value(QStringLiteral("unmapped")).toBool()
            || rig.value(QStringLiteral("health")).toString() != QStringLiteral("ready")) {
            std::fprintf(stderr, "active unmapped Rig was not presented as healthy hardware\n");
            return false;
        }
        const QString categoryId = backend->profileDetail(hotas::normalProfileId())
            .value(QStringLiteral("categoryId")).toString();
        const QString createdId = backend->createProfileForRigInCategory(
            QStringLiteral("Pedals Only"), categoryId, QLatin1String(kRigId));
        if (createdId.isEmpty()
            || backend->selectedProfileId() != createdId
            || !backend->activeProfileId().isEmpty()
            || backend->activeDeviceRigId() != QLatin1String(kRigId)) {
            std::fprintf(stderr, "blank Profile creation changed the active unmapped Rig state\n");
            return false;
        }
        const QVariantMap created = backend->profileDetail(createdId);
        if (created.value(QStringLiteral("deviceRigId")).toString() != QLatin1String(kRigId)
            || created.value(QStringLiteral("mappedAxes")).toInt() != 0
            || created.value(QStringLiteral("automaticSelectionMode")).toString()
                != QStringLiteral("manual-only")) {
            std::fprintf(stderr, "blank Profile was not prepared as an inactive Rig-compatible editor target\n");
            return false;
        }
        const QVariantMap activated = backend->activateProfileResult(createdId);
        if (!activated.value(QStringLiteral("success")).toBool()
            || backend->activeProfileId() != createdId
            || backend->activeDeviceRigId() != QLatin1String(kRigId)) {
            std::fprintf(stderr, "new Profile did not activate without reactivating the already active Rig\n");
            return false;
        }
        return true;
    }
    std::fprintf(stderr, "active unmapped Rig was missing from the device Rig projection\n");
    return false;
}

bool verifySelectedButtonPresentationIsBounded()
{
    constexpr int kButtonCount = 32;
    constexpr int kTransitions = kButtonCount * 120;
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureSelectedButtonPresentationFixtureForTest(kButtonCount)) {
        std::fprintf(stderr, "selected button presentation fixture could not be configured\n");
        return false;
    }
    const QVariantList initialConfiguration = backend->buttonConfiguration();
    if (initialConfiguration.size() != kButtonCount
        || backend->buttonInputTelemetry().size() != kButtonCount) {
        std::fprintf(stderr, "selected button presentation did not expose the 32-button snapshot\n");
        return false;
    }

    backend->resetUiPerformanceCounters();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int transition = 0; transition < kTransitions; ++transition) {
        const int physicalButton = (transition % kButtonCount) + 1;
        const bool pressed = ((transition / kButtonCount) % 2) == 0;
        if (!backend->publishSelectedButtonStateForTest(physicalButton, pressed)) {
            std::fprintf(stderr, "selected button presentation rejected synthetic button %d\n", physicalButton);
            return false;
        }
        const QVariantList telemetry = backend->buttonInputTelemetry();
        const QVariantMap current = telemetry.at(physicalButton - 1).toMap();
        if (current.value(QStringLiteral("index")).toInt() != physicalButton
            || current.value(QStringLiteral("pressed")).toBool() != pressed) {
            std::fprintf(stderr, "selected button telemetry did not publish the latest bounded state\n");
            return false;
        }
    }
    const QVariantMap counters = backend->uiPerformanceCounters();
    const qulonglong configurationRebuilds = counters.value(
        QStringLiteral("selectedButtonConfigurationRebuilds")).toULongLong();
    const qulonglong legacyRebuilds = counters.value(
        QStringLiteral("buttonModelRebuilds")).toULongLong();
    const qulonglong publishes = counters.value(
        QStringLiteral("selectedButtonTelemetryPublishes")).toULongLong();
    const qint64 elapsedMs = elapsed.elapsed();
    std::fprintf(stderr,
        "selected_button_presentation transitions=%d elapsed_ms=%lld configuration_rebuilds=%llu legacy_rebuilds=%llu publishes=%llu\n",
        kTransitions, static_cast<long long>(elapsedMs),
        static_cast<unsigned long long>(configurationRebuilds),
        static_cast<unsigned long long>(legacyRebuilds),
        static_cast<unsigned long long>(publishes));
    if (backend->buttonConfiguration() != initialConfiguration
        || configurationRebuilds != 0 || legacyRebuilds != 0
        || publishes != static_cast<qulonglong>(kTransitions)
        || elapsedMs >= 1000) {
        std::fprintf(stderr, "selected button presentation was not bounded and configuration-stable\n");
        return false;
    }
    return true;
}

bool verifyProfileRigAssignmentSurvivesActivationSwitch()
{
    constexpr auto kPrimaryRig = "activation-transaction-rig";
    constexpr auto kAlternateRig = "activation-transaction-alternate-rig";
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureActivationTransactionFixtureForTest()) return false;

    const QString normalId = hotas::normalProfileId();
    const QString precisionId = hotas::precisionProfileId();
    const QVariantMap normalBefore = backend->profileDetail(normalId);
    const QVariantMap precisionBefore = backend->profileDetail(precisionId);
    if (normalBefore.value(QStringLiteral("deviceRigId")).toString() != QLatin1String(kPrimaryRig)
        || precisionBefore.value(QStringLiteral("deviceRigId")).toString() != QLatin1String(kAlternateRig)) {
        std::fprintf(stderr, "activation assignment fixture did not establish independent Profile Rig bindings\n");
        return false;
    }

    const QVariantMap requested = backend->activateProfileResult(precisionId);
    if (requested.value(QStringLiteral("success")).toBool()
        || !requested.value(QStringLiteral("requiresRigSwitchConfirmation")).toBool()
        || requested.value(QStringLiteral("currentDeviceRigName")).toString()
            != QStringLiteral("Activation Transaction Rig")
        || requested.value(QStringLiteral("configuredDeviceRigName")).toString()
            != QStringLiteral("Activation Transaction Alternate Rig")
        || !requested.value(QStringLiteral("message")).toString().contains(
            QStringLiteral("Activation Transaction Alternate Rig"))
        || !requested.value(QStringLiteral("message")).toString().contains(
            QStringLiteral("Activation Transaction Rig"))
        || backend->activeProfileId() != normalId
        || backend->activeDeviceRigId() != QLatin1String(kPrimaryRig)
        || backend->profileDetail(normalId).value(QStringLiteral("deviceRigId")).toString()
            != QLatin1String(kPrimaryRig)
        || backend->profileDetail(precisionId).value(QStringLiteral("deviceRigId")).toString()
            != QLatin1String(kAlternateRig)) {
        std::fprintf(stderr, "cross-Rig activation was not held behind an immutable-assignment confirmation\n");
        return false;
    }

    const QVariantMap activated = backend->activateProfileAfterRigSwitchConfirmation(precisionId);
    if (!activated.value(QStringLiteral("success")).toBool()
        || backend->activeProfileId() != precisionId
        || backend->activeDeviceRigId() != QLatin1String(kAlternateRig)
        || backend->profileDetail(normalId).value(QStringLiteral("deviceRigId")).toString()
            != QLatin1String(kPrimaryRig)
        || backend->profileDetail(precisionId).value(QStringLiteral("deviceRigId")).toString()
            != QLatin1String(kAlternateRig)) {
        std::fprintf(stderr, "confirmed Rig switch rewrote a Profile's persistent Rig assignment\n");
        return false;
    }
    return true;
}

bool verifyRigProfileResolutionIsNeverAnActivationBlocker()
{
    constexpr auto kRigId = "activation-transaction-rig";
    {
        auto backend = std::make_unique<hotas::AppBackend>();
        if (!backend->configureRigProfileResolutionFixtureForTest(QStringLiteral("multiple"))) return false;
        const QVariantMap result = backend->activateDeviceRigResult(QLatin1String(kRigId));
        if (!result.value(QStringLiteral("success")).toBool()
            || !result.value(QStringLiteral("unmapped")).toBool()
            || !result.value(QStringLiteral("profileChoiceRequired")).toBool()
            || result.value(QStringLiteral("compatibleProfiles")).toList().size() != 2
            || backend->activeDeviceRigId() != QLatin1String(kRigId)
            || !backend->activeProfileId().isEmpty()) {
            std::fprintf(stderr, "multiple compatible Profiles did not leave the healthy Rig active for an explicit choice\n");
            return false;
        }
    }
    {
        auto backend = std::make_unique<hotas::AppBackend>();
        if (!backend->configureRigProfileResolutionFixtureForTest(QStringLiteral("default"))) return false;
        const QVariantMap result = backend->activateDeviceRigResult(QLatin1String(kRigId));
        if (!result.value(QStringLiteral("success")).toBool()
            || result.value(QStringLiteral("profileId")).toString() != hotas::precisionProfileId()
            || backend->activeProfileId() != hotas::precisionProfileId()
            || backend->activeDeviceRigId() != QLatin1String(kRigId)) {
            std::fprintf(stderr, "valid Rig default Profile was not applied after hardware activation\n");
            return false;
        }
    }
    {
        auto backend = std::make_unique<hotas::AppBackend>();
        if (!backend->configureRigProfileResolutionFixtureForTest(QStringLiteral("invalid-default"))) return false;
        const QVariantMap result = backend->activateDeviceRigResult(QLatin1String(kRigId));
        if (!result.value(QStringLiteral("success")).toBool()
            || !result.value(QStringLiteral("unmapped")).toBool()
            || result.value(QStringLiteral("profileAdvisory")).toString().isEmpty()
            || backend->activeDeviceRigId() != QLatin1String(kRigId)
            || !backend->activeProfileId().isEmpty()) {
            std::fprintf(stderr, "invalid Rig default incorrectly blocked healthy hardware activation\n");
            return false;
        }
    }
    {
        auto backend = std::make_unique<hotas::AppBackend>();
        if (!backend->configureRigProfileResolutionFixtureForTest(QStringLiteral("none"))) return false;
        const QVariantMap result = backend->activateDeviceRigResult(QLatin1String(kRigId));
        if (!result.value(QStringLiteral("success")).toBool()
            || !result.value(QStringLiteral("unmapped")).toBool()
            || backend->activeDeviceRigId() != QLatin1String(kRigId)
            || !backend->activeProfileId().isEmpty()
            || result.value(QStringLiteral("profileAdvisory")).toString().isEmpty()) {
            std::fprintf(stderr, "an explicit None default Profile did not preserve the active unmapped Rig\n");
            return false;
        }
    }
    return true;
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
    // valid and must not silently activate the Rig.  The physical source is
    // explicit editor context, even for this single-member fixture.
    backend->setVirtualAxisAvailabilityForTest(true);
    if (!backend->selectControllerForEditing(QStringLiteral("activation-transaction-controller"))
        || !backend->setMapping(0, QStringLiteral("Axis 1"), true)
        || !backend->activeDeviceRigId().isEmpty()) {
        std::fprintf(stderr,
            "a viewed Profile could not write through its Device Rig primary output without activating it\n");
        return false;
    }
    const QVariantList axes = backend->axes();
    if (axes.isEmpty() || axes.front().toMap().value(QStringLiteral("target")).toString()
            != QStringLiteral("Axis 1")) {
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

bool verifyHidHideTimeoutRetainsLastKnownGoodReadback()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureHidHideRefreshTimeoutWithLastKnownGoodForTest()) {
        std::fprintf(stderr, "last-known-good HidHide timeout fixture could not be configured\n");
        return false;
    }
    const QVariantMap snapshot = backend->setupTruthSnapshot();
    const QVariantMap isolation = setupTruthGroup(snapshot, QStringLiteral("isolation"));
    const QVariantMap evidence = isolation.value(QStringLiteral("evidence")).toMap();
    if (snapshot.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || isolation.value(QStringLiteral("status")).toString() != QStringLiteral("READY")
        || !evidence.value(QStringLiteral("refreshDelayed")).toBool()
        || evidence.value(QStringLiteral("lastKnownGoodAt")).toString().isEmpty()
        || hasSetupTruthIssue(snapshot, QStringLiteral("HidHideInspectionFailed"))) {
        std::fprintf(stderr, "a transient HidHide timeout displaced valid prior isolation evidence\n");
        return false;
    }
    return true;
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

bool verifyExternalVJoyBusyTruth()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureExternalVJoyBusyFixtureForTest()) {
        std::fprintf(stderr, "external vJoy busy fixture could not be configured\n");
        return false;
    }
    const QVariantMap snapshot = backend->setupTruthSnapshot();
    const QVariantMap output = setupTruthGroup(snapshot, QStringLiteral("vjoy"));
    const QVariantMap mapping = setupTruthGroup(snapshot, QStringLiteral("mapping"));
    const QVariantList repairPlan = snapshot.value(QStringLiteral("repairPlan")).toList();
    const QVariantMap outputEvidence = output.value(QStringLiteral("evidence")).toMap();
    const QVariantList outputs = outputEvidence.value(QStringLiteral("outputs")).toList();
    const QVariantMap primary = outputs.isEmpty() ? QVariantMap{} : outputs.front().toMap();
    const bool hasDescriptorRepair = std::any_of(repairPlan.cbegin(), repairPlan.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("code")).toString()
            == QStringLiteral("VJoyDescriptorMismatch");
    });
    if (snapshot.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("WAITING FOR USER")
        || output.value(QStringLiteral("status")).toString() != QStringLiteral("WAITING FOR USER")
        || mapping.value(QStringLiteral("status")).toString() != QStringLiteral("WAITING FOR USER")
        || !hasSetupTruthIssue(snapshot, QStringLiteral("VJoyOutputBusy"))
        || hasDescriptorRepair
        || !primary.value(QStringLiteral("busy")).toBool()
        || primary.value(QStringLiteral("ownedByHotasBf6")).toBool()
        || primary.value(QStringLiteral("ownerPid")).toULongLong() != 12345
        || primary.value(QStringLiteral("ownerProcess")).toString() != QStringLiteral("HOTAS BF6 Test Host.exe")
        || !mapping.value(QStringLiteral("detail")).toString().contains(QStringLiteral("Waiting for Virtual Output"))) {
        std::fprintf(stderr, "configured external vJoy busy was not projected as one wait state\n");
        return false;
    }
    return true;
}

bool verifyFreshSetupCheckSessionLifecycle()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureSetupTruthReadyToActivateFixtureForTest()) return false;
    const QString priorSessionId = backend->setupRepairSession().value(QStringLiteral("sessionId")).toString();
    if (!backend->completeSetupCheck().value(QStringLiteral("success")).toBool()) return false;
    const QVariantMap fresh = backend->beginSetupCheckSessionForTest();
    const QVariantMap snapshot = fresh.value(QStringLiteral("checkSnapshot")).toMap();
    const QVariantList steps = fresh.value(QStringLiteral("steps")).toList();
    if (priorSessionId.isEmpty()
        || fresh.value(QStringLiteral("sessionId")).toString().isEmpty()
        || fresh.value(QStringLiteral("sessionId")).toString() == priorSessionId
        || fresh.value(QStringLiteral("mode")).toString() != QStringLiteral("CHECKING")
        || !fresh.value(QStringLiteral("active")).toBool()
        || snapshot.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("CHECKING")
        || steps.size() != 1
        || steps.front().toMap().value(QStringLiteral("id")).toString() != QStringLiteral("inspect")
        || steps.front().toMap().value(QStringLiteral("status")).toString() != QStringLiteral("RUNNING")
        || !fresh.value(QStringLiteral("afterSnapshot")).toMap().isEmpty()) {
        std::fprintf(stderr, "new setup check did not replace the prior terminal session with a fresh CHECK lifecycle\n");
        return false;
    }
    return true;
}

bool verifyDamagedVJoyIsDetectedOnSecondFreshCheck()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureTargetedVJoyRepairFixtureForTest(
            QStringLiteral("activation-transaction-rig"), 1)) return false;
    const QString priorSessionId = backend->setupRepairSession().value(QStringLiteral("sessionId")).toString();
    if (!backend->completeFreshSetupCheckWithFixturePlansForTest()) return false;
    const QVariantMap session = backend->setupRepairSession();
    const QVariantMap snapshot = session.value(QStringLiteral("checkSnapshot")).toMap();
    if (session.value(QStringLiteral("sessionId")).toString().isEmpty()
        || session.value(QStringLiteral("sessionId")).toString() == priorSessionId
        || session.value(QStringLiteral("mode")).toString() != QStringLiteral("RESULTS")
        || !hasSetupTruthIssue(snapshot, QStringLiteral("VJoyDescriptorMismatch"))
        || setupTruthGroup(snapshot, QStringLiteral("vjoy")).value(QStringLiteral("status")).toString()
            != QStringLiteral("ACTION NEEDED")) {
        std::fprintf(stderr, "fresh setup check did not rediscover the damaged vJoy descriptor\n");
        return false;
    }
    return true;
}

bool verifyControllerVerificationConvergesInOneSetupRun()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureControllerVerificationConvergenceFixtureForTest()) {
        std::fprintf(stderr, "controller verification convergence fixture could not be configured\n");
        return false;
    }
    const QVariantMap before = backend->setupTruthSnapshot();
    const QVariantMap beforeRig = before.value(QStringLiteral("rigStatus")).toMap();
    if (!hasSetupTruthIssue(before, QStringLiteral("PhysicalDeviceUnverified"))
        || beforeRig.value(QStringLiteral("needsVerificationRequiredMemberIds")).toStringList().isEmpty()
        || setupTruthGroup(before, QStringLiteral("verification")).value(QStringLiteral("status")).toString()
            != QStringLiteral("ACTION NEEDED")) {
        std::fprintf(stderr, "first setup check did not expose the required unverified controller\n");
        return false;
    }

    if (!backend->completeControllerVerificationConvergenceForTest()) {
        std::fprintf(stderr, "controller verification did not converge within the first setup session\n");
        return false;
    }
    const QVariantMap firstSession = backend->setupRepairSession();
    const QVariantMap firstAfter = firstSession.value(QStringLiteral("afterSnapshot")).toMap();
    const QVariantMap firstRig = firstAfter.value(QStringLiteral("rigStatus")).toMap();
    const QVariantMap firstActivation = firstAfter.value(QStringLiteral("activationDecision")).toMap();
    if (firstSession.value(QStringLiteral("mode")).toString() != QStringLiteral("COMPLETE")
        || firstAfter.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || hasSetupTruthIssue(firstAfter, QStringLiteral("PhysicalDeviceUnverified"))
        || setupTruthGroup(firstAfter, QStringLiteral("verification")).value(QStringLiteral("status")).toString()
            != QStringLiteral("READY")
        || !firstRig.value(QStringLiteral("needsVerificationRequiredMemberIds")).toStringList().isEmpty()
        || !firstActivation.value(QStringLiteral("valid")).toBool()) {
        std::fprintf(stderr, "first setup session retained stale verification, Rig, or activation truth\n");
        return false;
    }

    // This is the regression invariant: without any fixture or external-state
    // change, a second CHECK finds no repairable verification work. It may not
    // improve the result that the first successful verification already froze.
    if (!backend->completeFreshSetupCheckWithFixturePlansForTest()) {
        std::fprintf(stderr, "second unchanged setup check could not complete\n");
        return false;
    }
    const QVariantMap secondSession = backend->setupRepairSession();
    const QVariantMap secondCheck = secondSession.value(QStringLiteral("checkSnapshot")).toMap();
    if (secondSession.value(QStringLiteral("mode")).toString() != QStringLiteral("RESULTS")
        || secondCheck.value(QStringLiteral("overallStatus")).toString() != QStringLiteral("READY")
        || hasSetupTruthIssue(secondCheck, QStringLiteral("PhysicalDeviceUnverified"))
        || !secondCheck.value(QStringLiteral("repairPlan")).toList().isEmpty()
        || setupTruthGroup(secondCheck, QStringLiteral("verification")).value(QStringLiteral("status")).toString()
            != QStringLiteral("READY")) {
        std::fprintf(stderr, "second unchanged setup check improved the first session's result\n");
        return false;
    }
    return true;
}

bool verifyAcquiredOutputWaitsForReportWithoutBecomingUnavailable()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureAcquiredOutputWaitingForReportFixtureForTest()) return false;
    const QVariantMap output = backend->outputRuntimeTelemetry();
    if (output.value(QStringLiteral("descriptorState")).toString() != QStringLiteral("CONFIGURED")
        || output.value(QStringLiteral("ownershipState")).toString() != QStringLiteral("OWNED BY HOTAS BF6")
        || output.value(QStringLiteral("runtimeReportState")).toString()
            != QStringLiteral("WAITING FOR FIRST MAPPED REPORT")
        || !output.value(QStringLiteral("outputAvailable")).toBool()
        || output.value(QStringLiteral("outputReportsSucceeding")).toBool()) {
        std::fprintf(stderr, "acquired vJoy output was not projected independently from first mapped report\n");
        return false;
    }
    return true;
}

bool verifyWaitingForUserDoesNotLatch()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureReconnectLifecycleFixtureForTest()) return false;
    const QVariantMap waiting = backend->setupRepairSession();
    if (waiting.value(QStringLiteral("mode")).toString() != QStringLiteral("WAITING FOR USER")
        || waiting.value(QStringLiteral("waitingReason")).toString().isEmpty()
        || !backend->completeReconnectInventoryRefreshForTest()) {
        std::fprintf(stderr, "live reconnect wait did not carry its concrete reason\n");
        return false;
    }
    const QVariantMap afterReconnect = backend->setupRepairSession();
    if (afterReconnect.value(QStringLiteral("mode")).toString() == QStringLiteral("WAITING FOR USER")
        || afterReconnect.contains(QStringLiteral("waitingReason"))) {
        std::fprintf(stderr, "reconnect completion left a waiting-for-user latch\n");
        return false;
    }

    auto staleBackend = std::make_unique<hotas::AppBackend>();
    if (!staleBackend->configureStaleWaitingForUserFixtureForTest()) return false;
    const QVariantList rigs = staleBackend->deviceRigs();
    const QVariantMap rig = rigs.isEmpty() ? QVariantMap{} : rigs.front().toMap();
    const QVariantMap staleSession = staleBackend->setupRepairSession();
    if (rig.value(QStringLiteral("setupStatus")).toString() != QStringLiteral("READY")
        || staleSession.value(QStringLiteral("mode")).toString() == QStringLiteral("WAITING FOR USER")
        || staleSession.contains(QStringLiteral("waitingReason"))) {
        std::fprintf(stderr, "healthy Rig retained a stale waiting-for-user presentation\n");
        return false;
    }
    return true;
}

bool verifyMultiControllerMemberIsolation()
{
    constexpr auto kRigId = "activation-transaction-rig";
    constexpr auto kPrimaryRecordId = "activation-transaction-controller";
    constexpr auto kOptionalRecordId = "multi-controller-xbox";
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureMultiControllerRigFixtureForTest()) {
        std::fprintf(stderr, "multi-controller fixture could not be configured\n");
        return false;
    }

    const auto rigState = [&backend]() {
        for (const QVariant &entry : backend->deviceRigs()) {
            const QVariantMap rig = entry.toMap();
            if (rig.value(QStringLiteral("id")).toString() == QLatin1String(kRigId)) return rig;
        }
        return QVariantMap{};
    };
    const auto memberState = [](const QVariantMap &rig, const QString &recordId) {
        for (const QVariant &entry : rig.value(QStringLiteral("members")).toList()) {
            const QVariantMap member = entry.toMap();
            if (member.value(QStringLiteral("id")).toString() == recordId) return member;
        }
        return QVariantMap{};
    };
    const auto setupGroup = [&backend](const QString &id) {
        for (const QVariant &entry : backend->setupTruthSnapshot().value(QStringLiteral("groups")).toList()) {
            const QVariantMap group = entry.toMap();
            if (group.value(QStringLiteral("id")).toString() == id) return group;
        }
        return QVariantMap{};
    };

    // Two independently identified members are visible to the global read-only
    // setup projection. The verified required controller cannot silently
    // overwrite the connected-but-unverified optional member's own state.
    const QVariantMap initialRig = rigState();
    const QVariantMap initialPrimary = memberState(initialRig, QLatin1String(kPrimaryRecordId));
    const QVariantMap initialOptional = memberState(initialRig, QLatin1String(kOptionalRecordId));
    const QVariantList physicalMembers = setupGroup(QStringLiteral("physical"))
        .value(QStringLiteral("evidence")).toMap().value(QStringLiteral("members")).toList();
    if (!initialRig.value(QStringLiteral("complete")).toBool()
        || initialRig.value(QStringLiteral("setupStatus")).toString() != QStringLiteral("READY")
        || initialPrimary.isEmpty() || !initialPrimary.value(QStringLiteral("required")).toBool()
        || !initialPrimary.value(QStringLiteral("verified")).toBool()
        || initialOptional.isEmpty() || initialOptional.value(QStringLiteral("required")).toBool()
        || initialOptional.value(QStringLiteral("verified")).toBool()
        || !initialOptional.value(QStringLiteral("needsVerification")).toBool()
        || physicalMembers.size() != 2) {
        std::fprintf(stderr, "multi-controller setup did not preserve a Ready Rig or independent required/optional member truth\n");
        return false;
    }

    bool sawPrimary = false;
    bool sawOptional = false;
    for (const QVariant &entry : physicalMembers) {
        const QVariantMap member = entry.toMap();
        const QString id = member.value(QStringLiteral("recordId")).toString();
        sawPrimary = sawPrimary || (id == QLatin1String(kPrimaryRecordId)
            && member.value(QStringLiteral("required")).toBool()
            && member.value(QStringLiteral("identityVerified")).toBool());
        sawOptional = sawOptional || (id == QLatin1String(kOptionalRecordId)
            && !member.value(QStringLiteral("required")).toBool()
            && !member.value(QStringLiteral("identityVerified")).toBool());
    }
    if (!sawPrimary || !sawOptional) {
        std::fprintf(stderr, "global setup projection did not inspect every enabled Rig member\n");
        return false;
    }

    // A connected optional controller that has not yet been verified is an
    // advisory, not an app-wide readiness blocker. This asserts the actual
    // Setup Assistant aggregate (the owner-visible READY/ACTION NEEDED path),
    // rather than only the lower-level Rig status.
    bool sawOptionalVerificationNote = false;
    bool sawBlockingOptionalVerification = false;
    for (const QVariant &entry : backend->setupAssistantIssues()) {
        const QVariantMap issue = entry.toMap();
        sawOptionalVerificationNote = sawOptionalVerificationNote
            || (issue.value(QStringLiteral("code")).toString()
                    == QStringLiteral("OptionalDeviceUnverified")
                && issue.value(QStringLiteral("severity")).toString() == QStringLiteral("note"));
        sawBlockingOptionalVerification = sawBlockingOptionalVerification
            || (issue.value(QStringLiteral("code")).toString()
                    == QStringLiteral("PhysicalDeviceUnverified")
                && issue.value(QStringLiteral("affectedObjectId")).toString()
                    == QLatin1String(kOptionalRecordId));
    }
    if (!sawOptionalVerificationNote || sawBlockingOptionalVerification) {
        std::fprintf(stderr, "an unverified optional controller was not retained as an advisory-only setup condition\n");
        return false;
    }

    // Elevating the second member to required immediately makes it a separate
    // blocker; restoring Optional returns the existing Rig to Ready without
    // requiring a new global setup pass.
    if (!backend->setDeviceRigMemberRequired(QLatin1String(kRigId), QLatin1String(kOptionalRecordId), true)
        || rigState().value(QStringLiteral("complete")).toBool()
        || !backend->setDeviceRigMemberRequired(QLatin1String(kRigId), QLatin1String(kOptionalRecordId), false)
        || !rigState().value(QStringLiteral("complete")).toBool()) {
        std::fprintf(stderr, "optional member readiness was not isolated from the required Rig state\n");
        return false;
    }

    const QVariantMap beforeSelection = backend->setupTruthSnapshot();
    const QString activeProfile = backend->activeProfileId();
    const QString activeRig = backend->activeDeviceRigId();
    const QString activeController = backend->activeControllerRecordId();
    const int outputDevice = backend->vjoyDeviceId();
    if (!backend->selectControllerForEditing(QLatin1String(kOptionalRecordId))
        || backend->setupTruthSnapshot() != beforeSelection
        || backend->activeProfileId() != activeProfile
        || backend->activeDeviceRigId() != activeRig
        || backend->activeControllerRecordId() != activeController
        || backend->vjoyDeviceId() != outputDevice) {
        std::fprintf(stderr, "SELECT DEVICE changed runtime or setup truth instead of editor context only\n");
        return false;
    }

    // Selected Device is the only physical-source authority for the editors.
    // The optional controller's saved capabilities must be surfaced exactly,
    // including an explicit Disabled source, without borrowing the primary
    // controller's mapping or capability projection.
    const QVariantList optionalButtonChoices = backend->selectedDeviceButtonChoices();
    const QVariantList optionalAxes = backend->axisConfiguration();
    const int availableOptionalAxes = static_cast<int>(std::count_if(
        optionalAxes.cbegin(), optionalAxes.cend(), [](const QVariant &entry) {
            return entry.toMap().value(QStringLiteral("available")).toBool();
        }));
    if (optionalButtonChoices.size() != 17
        || optionalButtonChoices.front().toMap().value(QStringLiteral("button")).toInt() != 0
        || optionalButtonChoices.front().toMap().value(QStringLiteral("label")).toString()
               != QStringLiteral("Disabled")
        || optionalButtonChoices.back().toMap().value(QStringLiteral("button")).toInt() != 16
        || availableOptionalAxes != 3
        || backend->axisMappingCollision(0, QStringLiteral("Axis 1"))
               .value(QStringLiteral("exists")).toBool()) {
        std::fprintf(stderr, "selected-device editor choices did not use only the optional controller's capabilities\n");
        return false;
    }

    // All Devices is intentionally overview-only. It may not infer a first,
    // active, or previously selected source for a new mapping command.
    if (!backend->setEditingDeviceContext(QLatin1String(kRigId), {})
        || backend->selectedDeviceIsSpecific()
        || !backend->selectedDeviceButtonChoices().isEmpty()
        || backend->setMapping(0, QStringLiteral("Axis 1"), false)
        || backend->setupTruthSnapshot() != beforeSelection
        || backend->activeProfileId() != activeProfile
        || backend->activeDeviceRigId() != activeRig
        || backend->activeControllerRecordId() != activeController
        || backend->vjoyDeviceId() != outputDevice
        || !backend->selectControllerForEditing(QLatin1String(kOptionalRecordId))) {
        std::fprintf(stderr, "All Devices silently resolved a physical mapping source\n");
        return false;
    }

    // Reuse the exact persisted-record terminal commit path used after the
    // card's real DirectInput acquisition. It must verify only Xbox and leave
    // the current runtime route untouched.
    if (!backend->commitExactControllerVerificationForTest(QLatin1String(kOptionalRecordId))
        || backend->activeProfileId() != activeProfile
        || backend->activeDeviceRigId() != activeRig
        || backend->activeControllerRecordId() != activeController
        || backend->vjoyDeviceId() != outputDevice
        || !memberState(rigState(), QLatin1String(kPrimaryRecordId)).value(QStringLiteral("verified")).toBool()
        || !memberState(rigState(), QLatin1String(kOptionalRecordId)).value(QStringLiteral("verified")).toBool()) {
        std::fprintf(stderr, "exact optional-controller verification altered the runtime route or did not commit independently\n");
        return false;
    }

    // Disconnecting an optional member is advisory only. The primary required
    // controller's Rig remains usable and does not inherit a stale failure.
    if (!backend->disconnectFixtureControllerForTest(QLatin1String(kOptionalRecordId))
        || !rigState().value(QStringLiteral("complete")).toBool()
        || memberState(rigState(), QLatin1String(kOptionalRecordId)).value(QStringLiteral("connected")).toBool()) {
        std::fprintf(stderr, "optional-controller disconnect blocked the otherwise-ready Rig\n");
        return false;
    }

    // Adding a second controller must be a topology change only.  It must not
    // clear, copy, or otherwise disturb the first controller's persisted
    // routes.  The re-added controller begins as its own intentionally blank
    // input channel, even though it has the same logical axis indices.
    backend->setVirtualAxisAvailabilityForTest(true);
    if (!backend->selectControllerForEditing(QLatin1String(kPrimaryRecordId))
        || !backend->setMapping(0, QStringLiteral("Axis 1"), false)) {
        std::fprintf(stderr, "primary controller route could not be configured before adding a second controller\n");
        return false;
    }
    const auto configuredTarget = [&backend]() {
        const QVariantList axes = backend->axisConfiguration();
        return axes.isEmpty() ? QString{} : axes.front().toMap().value(QStringLiteral("target")).toString();
    };
    if (configuredTarget() != QStringLiteral("Axis 1")
        || !backend->removeDeviceRigMember(QLatin1String(kRigId), QLatin1String(kOptionalRecordId))
        || !backend->addDeviceRigMember(QLatin1String(kRigId), QLatin1String(kOptionalRecordId), false)
        || configuredTarget() != QStringLiteral("Axis 1")
        || !backend->selectControllerForEditing(QLatin1String(kOptionalRecordId))
        || configuredTarget() != QStringLiteral("Disabled")) {
        std::fprintf(stderr, "adding a controller erased an existing route or inherited it onto the new controller\n");
        return false;
    }

    // Button ownership uses the same controller-qualified route seam. A
    // virtual-button card may show a T.Flight-equivalent source while the
    // Selected Device chooses the independently configurable Xbox source.
    if (!backend->selectControllerForEditing(QLatin1String(kPrimaryRecordId))
        || !backend->assignSelectedDeviceButtonToVirtualOutput(1, 1, false)
        || !backend->selectControllerForEditing(QLatin1String(kOptionalRecordId))) {
        std::fprintf(stderr, "controller-qualified button fixture could not configure the primary source\n");
        return false;
    }
    const auto buttonByOutput = [&backend](int output) {
        for (const QVariant &entry : backend->buttons()) {
            const QVariantMap button = entry.toMap();
            if (button.value(QStringLiteral("target")).toInt() == output) return button;
        }
        return QVariantMap{};
    };
    const QVariantMap primaryButtonOutput = buttonByOutput(1);
    if (primaryButtonOutput.value(QStringLiteral("sourceCount")).toInt() != 1
        || primaryButtonOutput.value(QStringLiteral("primaryControllerId")).toString()
               != QLatin1String(kPrimaryRecordId)
        || !backend->assignSelectedDeviceButtonToVirtualOutput(5, 4, false)) {
        std::fprintf(stderr, "button grid did not retain its actual primary-controller source\n");
        return false;
    }
    const QVariantMap optionalButtonOutput = buttonByOutput(5);
    if (optionalButtonOutput.value(QStringLiteral("sourceCount")).toInt() != 1
        || optionalButtonOutput.value(QStringLiteral("primaryControllerId")).toString()
               != QLatin1String(kOptionalRecordId)
        || optionalButtonOutput.value(QStringLiteral("primaryPhysicalButton")).toInt() != 4) {
        std::fprintf(stderr, "selected-device button assignment did not preserve independent source ownership\n");
        return false;
    }
    return true;
}

bool verifyHidHideContextRejectsReconnectedMemberIdentity()
{
    constexpr auto kOptionalRecordId = "multi-controller-xbox";
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureMultiControllerRigFixtureForTest()) {
        std::fprintf(stderr, "HidHide context fixture could not be configured\n");
        return false;
    }
    const QString oldContext = backend->hidHideHealthContextKeyForTest();
    if (oldContext.isEmpty()
        || !backend->reconnectFixtureControllerWithHidIdentityForTest(
            QLatin1String(kOptionalRecordId), QStringLiteral("HID\\MULTI_CONTROLLER_XBOX\\RECONNECTED"))) {
        std::fprintf(stderr, "fixture optional controller could not reconnect with a new HID identity\n");
        return false;
    }
    const QString currentContext = backend->hidHideHealthContextKeyForTest();
    const bool contextChanged = currentContext != oldContext;
    const bool staleAccepted = backend->hidHideHealthResultAcceptedForTest(oldContext);
    const bool currentAccepted = backend->hidHideHealthResultAcceptedForTest(currentContext);
    if (!contextChanged || staleAccepted || !currentAccepted) {
        std::fprintf(stderr, "HidHide stale result state changed=%d staleAccepted=%d currentAccepted=%d\n",
                     contextChanged, staleAccepted, currentAccepted);
        return false;
    }
    return true;
}

bool verifyHidHideHealthActionFeedbackContracts()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    backend->setHidHideHealthRepairFixtureForTest(true);
    const QVariantMap available = backend->reviewHidHideHealthRepair();
    backend->setHidHideHealthRepairFixtureForTest(false);
    const QVariantMap unavailable = backend->reviewHidHideHealthRepair();
    const bool copied = backend->copyHidHideHealthEvidence();
    const QVariantMap doctor = backend->openHidHideDoctor();
    if (!available.value(QStringLiteral("success")).toBool()
        || available.value(QStringLiteral("nextAction")).toString() != QStringLiteral("repair-hidhide-access")
        || unavailable.value(QStringLiteral("success")).toBool()
        || unavailable.value(QStringLiteral("nextAction")).toString() != QStringLiteral("open-hidhide-doctor")
        || !copied || doctor.value(QStringLiteral("success")).toBool()) {
        std::fprintf(stderr, "HidHide review/copy/Doctor action result contract failed\n");
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

bool verifySelectedProfileEditorContext()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    const QString helicopterProfileId = QStringLiteral("activation-transaction-helicopter");
    const QString normalProfileId = hotas::normalProfileId();
    const QString precisionProfileId = hotas::precisionProfileId();
    if (!backend->configureSidebarActivationFixtureForTest()) {
        std::fprintf(stderr, "selected-Profile editor fixture could not be configured\n");
        return false;
    }

    // Force the same unavailable runtime seam that blocks activation. Editor
    // selection and durable profile changes must remain available from saved
    // profile/Rig metadata alone.
    backend->setButtonUiFixtureForTest(0, 0, 0);
    backend->setVirtualAxisAvailabilityForTest(false);
    if (!backend->selectProfileForEditing(helicopterProfileId)
        || backend->selectedProfileId() != helicopterProfileId
        || backend->activeProfileId() != normalProfileId
        || backend->selectedProfileActive()) {
        std::fprintf(stderr, "selecting an offline Profile changed runtime activation state\n");
        return false;
    }
    const auto profileHasState = [&backend](const QString &id, bool selected, bool active) {
        for (const QVariant &entry : backend->profiles()) {
            const QVariantMap profile = entry.toMap();
            if (profile.value(QStringLiteral("id")).toString() == id) {
                return profile.value(QStringLiteral("selected")).toBool() == selected
                    && profile.value(QStringLiteral("active")).toBool() == active;
            }
        }
        return false;
    };
    if (!profileHasState(helicopterProfileId, true, false)
        || !profileHasState(normalProfileId, false, true)) {
        std::fprintf(stderr, "selected and active Profile presentation states were not distinct\n");
        return false;
    }

    if (!backend->selectControllerForEditing(QStringLiteral("activation-transaction-controller"))) {
        std::fprintf(stderr, "selected-Profile editor could not establish its physical device channel\n");
        return false;
    }
    if (!backend->assignSelectedDeviceButtonToVirtualOutput(1, 1, false)) {
        std::fprintf(stderr, "selected-Profile editor could not route its selected device button\n");
        return false;
    }
    backend->setAxisCustomName(0, QStringLiteral("Helicopter Roll"));
    backend->setButtonCustomName(1, QStringLiteral("Helicopter Fire"));
    const QVariantList axes = backend->axisConfiguration();
    const QVariantList buttons = backend->buttons();
    const QVariantMap firstButton = buttons.isEmpty() ? QVariantMap{} : buttons.front().toMap();
    const QVariantList firstButtonSources = firstButton.value(QStringLiteral("sources")).toList();
    const QString firstButtonPhysicalLabel = firstButtonSources.isEmpty() ? QString{}
        : firstButtonSources.front().toMap().value(QStringLiteral("physicalLabel")).toString();
    if (axes.isEmpty() || buttons.isEmpty()
        || axes.front().toMap().value(QStringLiteral("customName")).toString()
            != QStringLiteral("Helicopter Roll")
        || firstButtonPhysicalLabel
            != QStringLiteral("Helicopter Fire")
        || backend->activeProfileId() != normalProfileId) {
        std::fprintf(stderr, "offline selected-Profile editor changes did not stay outside mapper activation\n");
        return false;
    }

    backend->setActivationFaultInjectionsForTest({QStringLiteral("persist")});
    if (backend->activateProfile(helicopterProfileId)
        || backend->activeProfileId() != normalProfileId
        || backend->selectedProfileId() != helicopterProfileId) {
        std::fprintf(stderr, "failed activation did not retain the selected editable Profile and prior active Profile\n");
        return false;
    }
    backend->setActivationFaultInjectionsForTest({});

    if (!backend->activateProfile(helicopterProfileId)
        || backend->activeProfileId() != helicopterProfileId
        || backend->selectedProfileId() != helicopterProfileId
        || !backend->selectedProfileActive()
        || !backend->selectProfileForEditing(precisionProfileId)
        || backend->selectedProfileId() != precisionProfileId
        || backend->activeProfileId() != helicopterProfileId
        || !backend->selectProfileForEditing(helicopterProfileId)
        || !backend->applyAutomaticProfileActivationForTest(precisionProfileId)
        || backend->activeProfileId() != precisionProfileId
        || backend->selectedProfileId() != helicopterProfileId
        || backend->selectedProfileActive()) {
        std::fprintf(stderr, "selected Profile did not remain independent of manual and automatic activation\n");
        return false;
    }
    return true;
}

bool verifyAxisAcquisitionIdentifyLifecycle()
{
    auto backend = std::make_unique<hotas::AppBackend>();
    if (!backend->configureAxisAcquisitionFixtureForTest()
        || backend->axisSourceMonitor().size() != hotas::kPhysicalAxisCount) {
        std::fprintf(stderr, "axis acquisition fixture did not publish eight candidates\n");
        return false;
    }
    const int x = static_cast<int>(hotas::PhysicalAxis::X);
    const int rz = static_cast<int>(hotas::PhysicalAxis::Rz);

    if (!backend->beginAxisIdentification(rz)
        || !backend->setAxisSourceMonitorCandidateForTest(x, 100, 0, 65535, 2, 4)
        || !backend->setAxisSourceMonitorCandidateForTest(rz, 32761, 102, 65411, 20, 120)
        || !backend->axisSourceMonitorRequestedForTest()) {
        std::fprintf(stderr, "axis identification could not start its bounded monitor capture\n");
        return false;
    }
    backend->completeAxisIdentificationForTest();
    const QVariantMap strong = backend->axisIdentification();
    if (strong.value(QStringLiteral("status")).toString() != QStringLiteral("STRONG MATCH FOUND")
        || strong.value(QStringLiteral("selectedSource")).toInt() != rz
        || backend->axisSourceMonitorRequestedForTest()
        || !backend->useIdentifiedAxisSource()) {
        std::fprintf(stderr, "strong unique axis identification did not require explicit use or release capture\n");
        return false;
    }
    const QVariantMap applied = backend->axisConfiguration().at(rz).toMap();
    if (!applied.value(QStringLiteral("manualOverride")).toBool()
        || applied.value(QStringLiteral("formattedSourceIndex")).toInt() != rz
        || !backend->resetAxisAcquisitionOverride(rz)
        || backend->axisConfiguration().at(rz).toMap().value(QStringLiteral("manualOverride")).toBool()) {
        std::fprintf(stderr, "identified manual override did not apply and reset cleanly\n");
        return false;
    }

    // An expert can retain the canonical/source resolver while overriding raw
    // range behavior. The saved record keeps that intent rather than freezing
    // an enumeration slot, and the projection makes it explicit to the UI.
    if (!backend->saveAxisAcquisitionOverride(rz, -1, -1, QStringLiteral("automatic-source"),
                                               QStringLiteral("manual"), 0, 65535,
                                               QStringLiteral("automatic"),
                                               QStringLiteral("automatic"))) {
        std::fprintf(stderr, "automatic source manual normalization was not accepted\n");
        return false;
    }
    const QVariantMap automaticSource = backend->axisConfiguration().at(rz).toMap();
    if (!automaticSource.value(QStringLiteral("manualOverride")).toBool()
        || !automaticSource.value(QStringLiteral("manualAutomaticTarget")).toBool()
        || !automaticSource.value(QStringLiteral("manualAutomaticSource")).toBool()
        || automaticSource.value(QStringLiteral("formattedSourceIndex")).toInt() != rz
        || !backend->resetAxisAcquisitionOverride(rz)) {
        std::fprintf(stderr, "automatic source override did not preserve the verified resolver\n");
        return false;
    }

    if (!backend->beginAxisIdentification(rz)
        || !backend->setAxisSourceMonitorCandidateForTest(x, 200, 0, 65535, 10, 8)
        || !backend->setAxisSourceMonitorCandidateForTest(rz, 32000, 102, 65411, 24, 80)) {
        std::fprintf(stderr, "ambiguous axis identification fixture could not advance candidates\n");
        return false;
    }
    backend->completeAxisIdentificationForTest();
    const QVariantMap ambiguous = backend->axisIdentification();
    if (ambiguous.value(QStringLiteral("status")).toString() != QStringLiteral("NO UNIQUE SOURCE IDENTIFIED")
        || ambiguous.value(QStringLiteral("selectedSource")).toInt() >= 0
        || backend->axisSourceMonitorRequestedForTest()) {
        std::fprintf(stderr, "ambiguous movement silently selected a source\n");
        return false;
    }

    if (!backend->beginAxisIdentification(rz)) {
        std::fprintf(stderr, "no-movement axis identification fixture could not start\n");
        return false;
    }
    backend->completeAxisIdentificationForTest();
    const QVariantMap noMovement = backend->axisIdentification();
    if (noMovement.value(QStringLiteral("status")).toString() != QStringLiteral("NO MOVEMENT OBSERVED")
        || noMovement.value(QStringLiteral("selectedSource")).toInt() >= 0
        || backend->axisSourceMonitorRequestedForTest()) {
        std::fprintf(stderr, "no-movement capture did not remain non-destructive\n");
        return false;
    }

    backend->setAxisSourceMonitorVisible(true);
    if (!backend->beginAxisIdentification(rz)) return false;
    backend->completeAxisIdentificationForTest();
    const bool panelCaptureRetained = backend->axisSourceMonitorRequestedForTest();
    backend->setAxisSourceMonitorVisible(false);
    if (!panelCaptureRetained || backend->axisSourceMonitorRequestedForTest()) {
        std::fprintf(stderr, "Identify Axis did not restore the panel monitor ownership\n");
        return false;
    }
    return true;
}

using StartupFixture = bool (*)();

const std::array<std::pair<QString, StartupFixture>, 23> &startupFixtures()
{
    static const std::array<std::pair<QString, StartupFixture>, 23> fixtures{{
        {QStringLiteral("startup-truth"), verifyStartupSetupTruthPublication},
        {QStringLiteral("hidhide-timeout"), verifyHidHideTimeoutRetainsLastKnownGoodReadback},
        {QStringLiteral("activation-faults"), verifyActivationTransactionFaults},
        {QStringLiteral("manual-rig-output"), verifyManualRigUsesRigOwnedOutputTransaction},
        {QStringLiteral("unmapped-rig"), verifyHealthyRigActivatesWithoutCompatibleProfile},
        {QStringLiteral("rig-profile-resolution"), verifyRigProfileResolutionIsNeverAnActivationBlocker},
        {QStringLiteral("manual-profile-output"), verifyManualProfileUsesRigOwnedOutputTransaction},
        {QStringLiteral("viewed-profile-output"), verifyViewedProfileUsesRigOwnedOutputForMappingEdits},
        {QStringLiteral("ready-to-activate"), verifySetupTruthReadyToActivateCompletion},
        {QStringLiteral("reconnect"), verifyReconnectLifecycleTruth},
        {QStringLiteral("targeted-vjoy"), verifyTargetedVJoyRepairPlan},
        {QStringLiteral("external-vjoy"), verifyExternalVJoyBusyTruth},
        {QStringLiteral("fresh-setup"), verifyFreshSetupCheckSessionLifecycle},
        {QStringLiteral("damaged-vjoy"), verifyDamagedVJoyIsDetectedOnSecondFreshCheck},
        {QStringLiteral("controller-verification"), verifyControllerVerificationConvergesInOneSetupRun},
        {QStringLiteral("acquired-output"), verifyAcquiredOutputWaitsForReportWithoutBecomingUnavailable},
        {QStringLiteral("stale-waiting"), verifyWaitingForUserDoesNotLatch},
        {QStringLiteral("multi-controller"), verifyMultiControllerMemberIsolation},
        {QStringLiteral("hidhide-context-fingerprint"), verifyHidHideContextRejectsReconnectedMemberIdentity},
        {QStringLiteral("hidhide-health-actions"), verifyHidHideHealthActionFeedbackContracts},
        {QStringLiteral("sidebar"), verifySidebarActivationLifecycle},
        {QStringLiteral("selected-profile"), verifySelectedProfileEditorContext},
        {QStringLiteral("axis-acquisition"), verifyAxisAcquisitionIdentifyLifecycle},
    }};
    return fixtures;
}

bool runFixtureInFreshProcess(const QString &name)
{
    QProcess fixture;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOTAS_STARTUP_FOCUSED_TEST"), name);
    fixture.setProcessEnvironment(environment);
    fixture.setProgram(QCoreApplication::applicationFilePath());
    fixture.setArguments({QStringLiteral("--isolated-presentation")});
    fixture.setProcessChannelMode(QProcess::ForwardedChannels);
    fixture.start();
    if (!fixture.waitForStarted(10'000) || !fixture.waitForFinished(30'000)
        || fixture.exitStatus() != QProcess::NormalExit || fixture.exitCode() != 0) {
        std::fprintf(stderr, "isolated startup fixture failed: %s (exit=%d)\n",
                     name.toUtf8().constData(), fixture.exitCode());
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
    const QString focusedFixture = qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST");
    application.setOrganizationName(QStringLiteral("HOTAS Mapper"));
    application.setOrganizationDomain(QStringLiteral("local.hotasmapper"));
    // Every synthetic fixture owns an independent ConfigStore directory.
    // They exercise deliberately different persisted Rig/Profile states and
    // must never inherit one another's topology merely because CTest runs
    // them under one executable name.
    application.setApplicationName(focusedFixture.isEmpty()
        ? QStringLiteral("HOTAS Mapper")
        : QStringLiteral("HOTAS Mapper Fixture %1").arg(focusedFixture));
    // Synthetic fixtures each establish their own persisted topology.  Test
    // mode isolates this store from the owner, and clearing it here keeps a
    // prior CTest invocation from becoming hidden input to the next fixture.
    // Recovery persistence is covered explicitly by readiness tests.
    QSettings testSettings;
    testSettings.clear();
    testSettings.sync();

    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("controller-verification-convergence")) {
        return verifyControllerVerificationConvergesInOneSetupRun() ? 0 : 1;
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("multi-controller-member-isolation")) {
        return verifyMultiControllerMemberIsolation() ? 0 : 1;
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("hidhide-last-known-good-timeout")) {
        return verifyHidHideTimeoutRetainsLastKnownGoodReadback() ? 0 : 1;
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("rig-unmapped-activation")) {
        return verifyHealthyRigActivatesWithoutCompatibleProfile() ? 0 : 1;
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("selected-button-presentation")) {
        // The real startup suite owns worker-thread teardown. This focused
        // microbenchmark deliberately exercises only UI-side atomics, then
        // exits immediately after its assertion so Qt shutdown timing cannot
        // contaminate the measured presentation result.
        const bool passed = verifySelectedButtonPresentationIsBounded();
        std::fflush(stderr);
        std::_Exit(passed ? 0 : 1);
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("profile-rig-assignment")) {
        const bool passed = verifyProfileRigAssignmentSurvivesActivationSwitch();
        std::fflush(stderr);
        std::_Exit(passed ? 0 : 1);
    }
    if (qEnvironmentVariable("HOTAS_STARTUP_FOCUSED_TEST")
            == QStringLiteral("rig-profile-resolution")) {
        return verifyRigProfileResolutionIsNeverAnActivationBlocker() ? 0 : 1;
    }
    if (!focusedFixture.isEmpty()) {
        for (const auto &[name, fixture] : startupFixtures()) {
            if (name == focusedFixture) {
                // These child fixtures deliberately mutate the same isolated
                // ConfigStore. Terminate at the assertion boundary instead
                // of letting Qt's process-wide platform/tray teardown from
                // one synthetic process influence the next fixture.
                const bool passed = fixture();
                std::fflush(stderr);
                std::_Exit(passed ? 0 : 1);
            }
        }
        std::fprintf(stderr, "unknown startup fixture: %s\n", focusedFixture.toUtf8().constData());
        return 1;
    }

    // Each control-plane fixture deliberately rewrites persisted topology.
    // Execute those destructive seams in short-lived child processes so a
    // queued timer or worker teardown from one fixture cannot touch the next
    // fixture's ConfigStore state. Production uses one long-lived backend;
    // this isolates only the test harness's independent scenarios.
    for (const auto &[name, fixture] : startupFixtures()) {
        Q_UNUSED(fixture);
        if (!runFixtureInFreshProcess(name)) return 1;
    }
    // Fixture tests above intentionally rewrite isolated persisted topology.
    // Construct the long-lived presentation backend only after that control-
    // plane work has completed, so it cannot race the fixture ConfigStore.
    hotas::AppBackend backend;
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

        // Drive the published controller test seam instead of manufacturing
        // host load. Normal, Pressure, and Severe may change presentation
        // cadence, but mapping intent must remain independent throughout.
        auto *contention = backend.contentionResilienceController();
        const bool mappingBeforeContention = backend.mappingRequested();
        qint64 contentionNowMs = 10'000;
        for (int sample = 0; contention && sample < 10; ++sample) {
            contention->observeForTest(75, 60, contentionNowMs);
            contentionNowMs += 750;
        }
        const bool normalCadence = contention
            && backend.presentationSnapshotIntervalMs() == 33
            && backend.controllerDiscoveryIntervalMs() == 2500;
        if (contention) {
            contention->observeForTest(91, 60, contentionNowMs);
            contentionNowMs += 750;
            contention->observeForTest(92, 60, contentionNowMs);
            contentionNowMs += 750;
        }
        const bool pressureCadence = contention
            && backend.presentationSnapshotIntervalMs() == 50
            && backend.controllerDiscoveryIntervalMs() == 5000;
        if (contention) {
            contention->observeForTest(98, 60, contentionNowMs);
            contentionNowMs += 750;
        }
        const bool severeCadence = contention
            && backend.presentationSnapshotIntervalMs() == 83
            && backend.controllerDiscoveryIntervalMs() == 10000;
        for (int sample = 0; contention && sample < 10; ++sample) {
            contention->observeForTest(75, 60, contentionNowMs);
            contentionNowMs += 750;
        }
        const bool contentionCadence = normalCadence && pressureCadence && severeCadence
            && backend.presentationSnapshotIntervalMs() == 33
            && backend.controllerDiscoveryIntervalMs() == 2500
            && backend.mappingRequested() == mappingBeforeContention;

        lifecycleWindow->showMinimized();
        QTimer::singleShot(0, &application, [&, lifecycleWindow, visibleLifecycle, contentionCadence] {
            const bool minimizedLifecycle = backend.presentationState() == QStringLiteral("Minimized")
                && backend.presentationSnapshotActive()
                && backend.presentationSnapshotIntervalMs() == 250
                && backend.controllerDiscoveryIntervalMs() == 5000;
            const bool mappingWasRequested = backend.mappingRequested();
            backend.hideToTray();
            QTimer::singleShot(50, &application, [&, lifecycleWindow, visibleLifecycle,
                                                    contentionCadence, minimizedLifecycle, mappingWasRequested] {
                backend.resetUiPerformanceCounters();
                const QVariantList controllerModel = backend.controllers();
                const QVariantList profiles = backend.profiles();
                const QVariantList categories = backend.profileCategories();
                Q_UNUSED(controllerModel);
                Q_UNUSED(profiles);
                Q_UNUSED(categories);
                QTimer::singleShot(450, &application, [&, lifecycleWindow, visibleLifecycle,
                                                        contentionCadence, minimizedLifecycle, mappingWasRequested] {
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
                                                          contentionCadence, minimizedLifecycle, trayLifecycle, mappingWasRequested] {
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

                        if (!(visibleLifecycle && contentionCadence && minimizedLifecycle && trayLifecycle
                              && restoredLifecycle && gameDetectionStopsWhenDisabled
                              && gameDetectionRunsWhenEnabled)) {
                            std::fprintf(stderr,
                                "presentation_lifecycle_visible=%d contention=%d minimized=%d tray=%d restored=%d game_disabled=%d game_enabled=%d\n",
                                visibleLifecycle ? 1 : 0, contentionCadence ? 1 : 0,
                                minimizedLifecycle ? 1 : 0,
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
