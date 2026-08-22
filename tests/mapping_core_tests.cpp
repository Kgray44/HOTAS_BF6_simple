#include "axis_transform.h"
#include "button_mapping.h"
#include "config_store.h"
#include "physical_input_monitor.h"
#include "profile_model.h"

#include <QtTest>

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace hotas;

namespace {
bool nearlyEqual(float left, float right)
{
    return std::abs(left - right) < 0.0001F;
}

QJsonObject legacyConfigurationJson(const MapperConfiguration &configuration, int version)
{
    const ControllerProfile &profile = activeProfile(configuration);
    QJsonArray axes;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const AxisMapping &mapping = profile.axes[index];
        const Calibration &calibration = configuration.calibration[index];
        axes.append(QJsonObject{
            {QStringLiteral("target"), virtualAxisLabel(mapping.target)},
            {QStringLiteral("inverted"), mapping.inverted},
            {QStringLiteral("deadzone"), mapping.deadzone},
            {QStringLiteral("calibration"), QJsonObject{
                {QStringLiteral("enabled"), calibration.enabled},
                {QStringLiteral("minimum"), calibration.minimum},
                {QStringLiteral("center"), calibration.center},
                {QStringLiteral("maximum"), calibration.maximum},
            }},
        });
    }
    QJsonObject json{
        {QStringLiteral("version"), version},
        {QStringLiteral("preferredDeviceId"), configuration.preferredDeviceId},
        {QStringLiteral("vjoyDeviceId"), configuration.vjoyDeviceId},
        {QStringLiteral("startMappingOnLaunch"), configuration.startMappingOnLaunch},
        {QStringLiteral("axes"), axes},
    };
    if (version >= 2) {
        QJsonArray buttons;
        for (const ButtonBinding &binding : profile.buttons) {
            buttons.append(binding.type == ButtonActionType::VirtualButton
                ? QJsonObject{{QStringLiteral("type"), QStringLiteral("virtualButton")},
                              {QStringLiteral("target"), binding.target}}
                : QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}});
        }
        json.insert(QStringLiteral("buttons"), buttons);
    }
    return json;
}
}

class MappingCoreTests final : public QObject {
    Q_OBJECT

private slots:
    void calibrationNormalizesBothSides();
    void invalidCalibrationFallsBackToRaw();
    void deadzoneIsRescaled();
    void inversionIsAppliedAfterDeadzone();
    void rangeIsClamped();
    void configurationRoundTrips();
    void duplicateMappingIsRejectedAndNormalized();
    void buttonCapacityMismatchIsReported();
    void defaultButtonPassthroughIsCapacityBounded();
    void fifteenButtonPassthroughUsesButtonsOneThroughFifteen();
    void buttonMappingPropagatesPressAndRelease();
    void buttonsNineThroughFifteenPropagatePressAndRelease();
    void stoppingMappingReleasesVirtualButtons();
    void disabledAndInvalidButtonsDoNotMap();
    void duplicateButtonDestinationIsRejected();
    void buttonConfigurationRoundTripsAndMigratesV1();
    void malformedButtonConfigurationPreservesAxisConfiguration();
    void physicalMonitorPublishesWhenMappingIsStoppedAndVJoyIsUnavailable();
    void physicalMonitorRetainsFullOfflineButtonCount();
    void physicalMonitorPropagatesPressReleaseAndDisconnect();
    void physicalMonitorRecoversAfterReconnect();
    void legacyConfigurationMigratesToNormalAndPrecision();
    void profileMigrationIsIdempotent();
    void profileCrudValidatesNamesAndProtectsNormal();
    void profilesRemainIsolatedAndPersist();
    void profileSwitchCompilesCompleteAxisConfiguration();
    void profileSwitchReevaluatesHeldButtons();
    void calibrationRemainsGlobalAcrossProfiles();
    void malformedProfileConfigurationFallsBackSafely();
};

void MappingCoreTests::calibrationNormalizesBothSides()
{
    const Calibration calibration{true, -0.80F, 0.10F, 0.90F};
    QVERIFY(nearlyEqual(normalizeCalibrated(-0.35F, calibration), -0.50F));
    QVERIFY(nearlyEqual(normalizeCalibrated(0.50F, calibration), 0.50F));
    QVERIFY(nearlyEqual(normalizeCalibrated(-1.0F, calibration), -1.0F));
    QVERIFY(nearlyEqual(normalizeCalibrated(1.0F, calibration), 1.0F));
}

void MappingCoreTests::invalidCalibrationFallsBackToRaw()
{
    const Calibration calibration{true, -0.20F, -0.20F, 0.80F};
    QVERIFY(nearlyEqual(normalizeCalibrated(0.37F, calibration), 0.37F));
}

void MappingCoreTests::deadzoneIsRescaled()
{
    QVERIFY(nearlyEqual(applyRescaledDeadzone(0.20F, 0.20F), 0.0F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(0.60F, 0.20F), 0.50F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(-0.60F, 0.20F), -0.50F));
    QVERIFY(nearlyEqual(applyRescaledDeadzone(1.0F, 0.20F), 1.0F));
}

void MappingCoreTests::inversionIsAppliedAfterDeadzone()
{
    RuntimeAxisMapping mapping;
    mapping.profile.deadzone = 0.10F;
    mapping.profile.inverted = true;
    QVERIFY(nearlyEqual(transformAxis(0.55F, mapping), -0.50F));
    QVERIFY(nearlyEqual(transformAxis(0.02F, mapping), 0.0F));
}

void MappingCoreTests::rangeIsClamped()
{
    QCOMPARE(clampUnit(5.0F), 1.0F);
    QCOMPARE(clampUnit(-5.0F), -1.0F);
    QVERIFY(nearlyEqual(applyRescaledDeadzone(4.0F, 0.03F), 1.0F));
}

void MappingCoreTests::configurationRoundTrips()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &normal = activeProfile(configuration);
    configuration.preferredDeviceId = QStringLiteral("{0D15EA5E-0000-0000-0000-000000000001}");
    configuration.vjoyDeviceId = 2;
    configuration.startMappingOnLaunch = true;
    normal.axes[0].inverted = true;
    normal.axes[1].deadzone = 0.12F;
    configuration.calibration[2] = {true, -0.9F, 0.1F, 0.8F};
    normal.buttons = defaultButtonMappings(4, 4);
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter")));
    QVERIFY(activateProfile(configuration, QStringLiteral("profile-normal")));

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.preferredDeviceId, configuration.preferredDeviceId);
    QCOMPARE(restored.vjoyDeviceId, 2);
    QVERIFY(restored.startMappingOnLaunch);
    QCOMPARE(static_cast<int>(restored.profiles.size()), 3);
    QVERIFY(activeProfile(restored).axes[0].inverted);
    QCOMPARE(activeProfile(restored).axes[1].deadzone, 0.12F);
    QVERIFY(restored.calibration[2].enabled);
    QCOMPARE(restored.calibration[2].center, 0.1F);
    QCOMPARE(activeProfile(restored).buttons[3].target, 4);
}

void MappingCoreTests::duplicateMappingIsRejectedAndNormalized()
{
    AxisMappings mappings = defaultAxisMappings();
    QVERIFY(hasMappingConflict(mappings, static_cast<int>(PhysicalAxis::Z), VirtualAxis::X));
    QVERIFY(!hasMappingConflict(mappings, static_cast<int>(PhysicalAxis::Z), VirtualAxis::Disabled));
    mappings[static_cast<int>(PhysicalAxis::Z)].target = VirtualAxis::X;
    QVERIFY(!normalizeMappingConflicts(mappings));
    QCOMPARE(mappings[static_cast<int>(PhysicalAxis::Z)].target, VirtualAxis::Disabled);
}

void MappingCoreTests::buttonCapacityMismatchIsReported()
{
    const ButtonCapacityStatus insufficient = assessButtonCapacity(15, 8);
    QCOMPARE(insufficient.physicalButtons, 15);
    QCOMPARE(insufficient.virtualButtons, 8);
    QVERIFY(!insufficient.sufficient);
    QVERIFY(!insufficient.recommended);

    const ButtonCapacityStatus ready = assessButtonCapacity(15, 32);
    QVERIFY(ready.sufficient);
    QVERIFY(ready.recommended);
}

void MappingCoreTests::defaultButtonPassthroughIsCapacityBounded()
{
    const ButtonBindings bindings = defaultButtonMappings(5, 3);
    QCOMPARE(static_cast<int>(bindings.size()), 5);
    QCOMPARE(bindings[0].target, 1);
    QCOMPARE(bindings[2].target, 3);
    QCOMPARE(bindings[3].type, ButtonActionType::Disabled);
}

void MappingCoreTests::fifteenButtonPassthroughUsesButtonsOneThroughFifteen()
{
    const ButtonBindings bindings = defaultButtonMappings(15, 32);
    QCOMPARE(static_cast<int>(bindings.size()), 15);
    for (int source = 0; source < 15; ++source) {
        QCOMPARE(bindings[static_cast<size_t>(source)].type, ButtonActionType::VirtualButton);
        QCOMPARE(bindings[static_cast<size_t>(source)].target, source + 1);
    }
}

void MappingCoreTests::buttonMappingPropagatesPressAndRelease()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(4, 4), 4);
    PhysicalButtonStates physical{};
    physical[1] = true;
    QVERIFY(mapButtonStates(physical, targets, 4)[2]);
    physical[1] = false;
    QVERIFY(!mapButtonStates(physical, targets, 4)[2]);
}

void MappingCoreTests::buttonsNineThroughFifteenPropagatePressAndRelease()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(15, 32), 32);
    for (int source = 8; source < 15; ++source) {
        PhysicalButtonStates physical{};
        physical[static_cast<size_t>(source)] = true;
        const VirtualButtonStates pressed = mapButtonStates(physical, targets, 32);
        QVERIFY(pressed[static_cast<size_t>(source + 1)]);

        physical[static_cast<size_t>(source)] = false;
        const VirtualButtonStates released = mapButtonStates(physical, targets, 32);
        QVERIFY(!released[static_cast<size_t>(source + 1)]);
    }
}

void MappingCoreTests::stoppingMappingReleasesVirtualButtons()
{
    const RuntimeButtonTargets targets = buildRuntimeButtonTargets(defaultButtonMappings(2, 2), 2);
    PhysicalButtonStates physical{};
    physical[0] = true;
    QVERIFY(mapButtonStates(physical, targets, 2)[1]);

    // Stopping mapping clears the virtual output snapshot before releasing vJoy.
    physical.fill(false);
    const VirtualButtonStates released = mapButtonStates(physical, targets, 2);
    QVERIFY(!released[1]);
    QVERIFY(!released[2]);
}

void MappingCoreTests::disabledAndInvalidButtonsDoNotMap()
{
    ButtonBindings bindings{{ButtonActionType::Disabled, 0}, {ButtonActionType::VirtualButton, 9}};
    QVERIFY(!normalizeButtonMappings(bindings, 4));
    QCOMPARE(bindings[1].type, ButtonActionType::Disabled);
    const PhysicalButtonStates physical{true, true};
    const auto output = mapButtonStates(physical, buildRuntimeButtonTargets(bindings, 4), 4);
    for (bool pressed : output) QVERIFY(!pressed);
}

void MappingCoreTests::duplicateButtonDestinationIsRejected()
{
    ButtonBindings bindings{{ButtonActionType::VirtualButton, 2}, {ButtonActionType::VirtualButton, 2}};
    QVERIFY(hasButtonMappingConflict(bindings, 1, 2, 4));
    QVERIFY(!normalizeButtonMappings(bindings, 4));
    QCOMPARE(bindings[1].type, ButtonActionType::Disabled);
}

void MappingCoreTests::buttonConfigurationRoundTripsAndMigratesV1()
{
    MapperConfiguration configuration = defaultConfiguration();
    activeProfile(configuration).buttons = defaultButtonMappings(4, 4);
    bool valid = false;
    const QJsonObject v2 = legacyConfigurationJson(configuration, 2);
    const MapperConfiguration restored = ConfigStore::fromJson(v2, &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(activeProfile(restored).buttons.size()), 4);
    QCOMPARE(activeProfile(restored).buttons[3].target, 4);
    const ControllerProfile *precision = findProfile(restored, precisionProfileId());
    QVERIFY(precision);
    QCOMPARE(precision->buttons[3].target, 4);

    const QJsonObject v1 = legacyConfigurationJson(configuration, 1);
    const MapperConfiguration migrated = ConfigStore::fromJson(v1, &valid);
    QVERIFY(valid);
    QVERIFY(activeProfile(migrated).buttons.empty());
    QCOMPARE(activeProfile(migrated).axes[static_cast<int>(PhysicalAxis::X)].target, VirtualAxis::X);
}

void MappingCoreTests::malformedButtonConfigurationPreservesAxisConfiguration()
{
    MapperConfiguration configuration = defaultConfiguration();
    activeProfile(configuration).axes[static_cast<int>(PhysicalAxis::Y)].inverted = true;
    QJsonObject malformed = legacyConfigurationJson(configuration, 2);
    malformed.insert(QStringLiteral("buttons"), QStringLiteral("not an array"));
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(malformed, &valid);
    QVERIFY(valid);
    QVERIFY(activeProfile(restored).buttons.empty());
    QVERIFY(activeProfile(restored).axes[static_cast<int>(PhysicalAxis::Y)].inverted);
}

void MappingCoreTests::physicalMonitorPublishesWhenMappingIsStoppedAndVJoyIsUnavailable()
{
    // PhysicalInputMonitor intentionally has no mapping/vJoy dependency. The
    // worker uses it before attempting virtual-device acquisition.
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    axes[static_cast<int>(PhysicalAxis::X)] = true;
    buttons[0] = true;
    monitor.configure(axes, buttons, 1);

    PhysicalInputReport report;
    report.axes[static_cast<int>(PhysicalAxis::X)] = 0.421F;
    report.buttons[0] = true;
    report.pov = 9000;
    monitor.accept(report);

    QVERIFY(nearlyEqual(monitor.snapshot().axes[static_cast<int>(PhysicalAxis::X)], 0.421F));
    QVERIFY(monitor.snapshot().buttons[0]);
    QCOMPARE(monitor.snapshot().pov, 9000);
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{1});
}

void MappingCoreTests::physicalMonitorRetainsFullOfflineButtonCount()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    for (int index = 0; index < 15; ++index) buttons[index] = true;
    monitor.configure(axes, buttons, 0);

    QCOMPARE(static_cast<int>(std::count(monitor.availableButtons().begin(),
                                         monitor.availableButtons().end(), true)), 15);
    QVERIFY(defaultButtonMappings(15, 0).size() == 15);
}

void MappingCoreTests::physicalMonitorPropagatesPressReleaseAndDisconnect()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    buttons[4] = true;
    monitor.configure(axes, buttons, 0);

    PhysicalInputReport pressed;
    pressed.buttons[4] = true;
    monitor.accept(pressed);
    QVERIFY(monitor.snapshot().buttons[4]);
    QCOMPARE(monitor.snapshot().lastChangedButton, 5);

    monitor.accept({});
    QVERIFY(!monitor.snapshot().buttons[4]);
    QCOMPARE(monitor.snapshot().lastChangedButton, 5);

    monitor.disconnect();
    QVERIFY(std::none_of(monitor.availableButtons().begin(), monitor.availableButtons().end(),
                         [](bool available) { return available; }));
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{0});
}

void MappingCoreTests::physicalMonitorRecoversAfterReconnect()
{
    PhysicalInputMonitor monitor;
    std::array<bool, kPhysicalAxisCount> axes{};
    std::array<bool, kMaximumPhysicalButtons> buttons{};
    axes[static_cast<int>(PhysicalAxis::X)] = true;
    buttons[14] = true;
    monitor.configure(axes, buttons, 1);

    PhysicalInputReport beforeDisconnect;
    beforeDisconnect.axes[static_cast<int>(PhysicalAxis::X)] = -0.25F;
    beforeDisconnect.buttons[14] = true;
    monitor.accept(beforeDisconnect);
    monitor.disconnect();

    monitor.configure(axes, buttons, 1);
    PhysicalInputReport afterReconnect;
    afterReconnect.axes[static_cast<int>(PhysicalAxis::X)] = 0.75F;
    afterReconnect.buttons[14] = false;
    afterReconnect.pov = 27000;
    monitor.accept(afterReconnect);

    QVERIFY(nearlyEqual(monitor.snapshot().axes[static_cast<int>(PhysicalAxis::X)], 0.75F));
    QVERIFY(!monitor.snapshot().buttons[14]);
    QCOMPARE(monitor.snapshot().pov, 27000);
    QCOMPARE(monitor.snapshot().reportCount, std::uint64_t{1});
}

void MappingCoreTests::legacyConfigurationMigratesToNormalAndPrecision()
{
    MapperConfiguration legacy = defaultConfiguration();
    ControllerProfile &normal = activeProfile(legacy);
    normal.axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.08F;
    normal.axes[static_cast<int>(PhysicalAxis::Rz)].inverted = true;
    normal.buttons = defaultButtonMappings(15, 32);
    legacy.calibration[static_cast<int>(PhysicalAxis::X)] = {true, -0.8F, 0.1F, 0.9F};

    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacyConfigurationJson(legacy, 2), &valid);
    QVERIFY(valid);
    QCOMPARE(migrated.activeProfileId, normalProfileId());
    const ControllerProfile *migratedNormal = findProfile(migrated, normalProfileId());
    const ControllerProfile *precision = findProfile(migrated, precisionProfileId());
    QVERIFY(migratedNormal);
    QVERIFY(precision);
    QCOMPARE(migratedNormal->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.08F);
    QVERIFY(migratedNormal->axes[static_cast<int>(PhysicalAxis::Rz)].inverted);
    QCOMPARE(migratedNormal->buttons[14].target, 15);
    QCOMPARE(precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.08F);
    QCOMPARE(precision->buttons[14].target, 15);
    QVERIFY(migrated.calibration[static_cast<int>(PhysicalAxis::X)].enabled);
    QCOMPARE(migrated.calibration[static_cast<int>(PhysicalAxis::X)].center, 0.1F);
}

void MappingCoreTests::profileMigrationIsIdempotent()
{
    MapperConfiguration legacy = defaultConfiguration();
    activeProfile(legacy).axes[0].deadzone = 0.06F;
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacyConfigurationJson(legacy, 2), &valid);
    QVERIFY(valid);
    const MapperConfiguration reread = ConfigStore::fromJson(ConfigStore::toJson(migrated), &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(reread.profiles.size()), 2);
    QCOMPARE(reread.activeProfileId, normalProfileId());
    QCOMPARE(activeProfile(reread).axes[0].deadzone, 0.06F);
    QVERIFY(findProfile(reread, precisionProfileId()));
}

void MappingCoreTests::profileCrudValidatesNamesAndProtectsNormal()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    QVERIFY(!helicopterId.isEmpty());
    QVERIFY(!createProfile(configuration, QStringLiteral(" helicopter ")));
    QVERIFY(!createProfile(configuration, QStringLiteral("   ")));
    QVERIFY(!renameProfile(configuration, normalProfileId(), QStringLiteral("Combat")));
    QVERIFY(renameProfile(configuration, helicopterId, QStringLiteral("Heli")));
    QString cloneId;
    QVERIFY(cloneProfile(configuration, helicopterId, &cloneId));
    QVERIFY(findProfile(configuration, cloneId));
    QVERIFY(!deleteProfile(configuration, normalProfileId()));
    QVERIFY(activateProfile(configuration, helicopterId));
    QVERIFY(!deleteProfile(configuration, helicopterId));
    QVERIFY(activateProfile(configuration, normalProfileId()));
    QVERIFY(deleteProfile(configuration, helicopterId));
    QVERIFY(!findProfile(configuration, helicopterId));
}

void MappingCoreTests::profilesRemainIsolatedAndPersist()
{
    MapperConfiguration configuration = defaultConfiguration();
    QString helicopterId;
    QVERIFY(createProfile(configuration, QStringLiteral("Helicopter"), {}, &helicopterId));
    ControllerProfile *helicopter = findProfile(configuration, helicopterId);
    QVERIFY(helicopter);
    helicopter->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.12F;
    helicopter->buttons = defaultButtonMappings(4, 32);
    helicopter->buttons[0].target = 10;
    QVERIFY(activateProfile(configuration, helicopterId));

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.activeProfileId, helicopterId);
    const ControllerProfile *restoredHelicopter = findProfile(restored, helicopterId);
    const ControllerProfile *normal = findProfile(restored, normalProfileId());
    QVERIFY(restoredHelicopter);
    QVERIFY(normal);
    QCOMPARE(restoredHelicopter->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.12F);
    QCOMPARE(restoredHelicopter->buttons[0].target, 10);
    QCOMPARE(normal->axes[static_cast<int>(PhysicalAxis::X)].deadzone, 0.03F);
    QCOMPARE(normal->buttons.size(), size_t{0});
}

void MappingCoreTests::profileSwitchCompilesCompleteAxisConfiguration()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.08F;
    precision->axes[static_cast<int>(PhysicalAxis::Rz)].target = VirtualAxis::Disabled;

    const RuntimeMappingConfiguration normalRuntime = compileActiveProfile(configuration);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration precisionRuntime = compileActiveProfile(configuration);
    QCOMPARE(normalRuntime.axes[static_cast<int>(PhysicalAxis::X)].profile.deadzone, 0.03F);
    QCOMPARE(precisionRuntime.axes[static_cast<int>(PhysicalAxis::X)].profile.deadzone, 0.08F);
    QCOMPARE(normalRuntime.axes[static_cast<int>(PhysicalAxis::Rz)].profile.target, VirtualAxis::Rz);
    QCOMPARE(precisionRuntime.axes[static_cast<int>(PhysicalAxis::Rz)].profile.target, VirtualAxis::Disabled);
    QVERIFY(nearlyEqual(transformAxis(0.50F, normalRuntime.axes[0]), 0.484536F));
    QVERIFY(nearlyEqual(transformAxis(0.50F, precisionRuntime.axes[0]), 0.456522F));
}

void MappingCoreTests::profileSwitchReevaluatesHeldButtons()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile &normal = activeProfile(configuration);
    normal.buttons.resize(5);
    normal.buttons[4] = {ButtonActionType::VirtualButton, 5};
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->buttons.resize(5);
    precision->buttons[4] = {ButtonActionType::VirtualButton, 10};
    PhysicalButtonStates held{};
    held[4] = true;

    const RuntimeMappingConfiguration before = compileActiveProfile(configuration);
    const VirtualButtonStates beforeStates = mapButtonStates(
        held, buildRuntimeButtonTargets(before.buttons, 32), 32);
    QVERIFY(beforeStates[5]);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration after = compileActiveProfile(configuration);
    const VirtualButtonStates afterStates = mapButtonStates(
        held, buildRuntimeButtonTargets(after.buttons, 32), 32);
    QVERIFY(!afterStates[5]);
    QVERIFY(afterStates[10]);
}

void MappingCoreTests::calibrationRemainsGlobalAcrossProfiles()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.calibration[static_cast<int>(PhysicalAxis::X)] = {true, -0.75F, 0.05F, 0.90F};
    ControllerProfile *precision = findProfile(configuration, precisionProfileId());
    QVERIFY(precision);
    precision->axes[static_cast<int>(PhysicalAxis::X)].deadzone = 0.10F;
    const RuntimeMappingConfiguration normal = compileActiveProfile(configuration);
    QVERIFY(activateProfile(configuration, precisionProfileId()));
    const RuntimeMappingConfiguration precisionRuntime = compileActiveProfile(configuration);
    QVERIFY(normal.axes[0].calibration.enabled);
    QCOMPARE(normal.axes[0].calibration.center, 0.05F);
    QCOMPARE(precisionRuntime.axes[0].calibration.center, 0.05F);
    QCOMPARE(precisionRuntime.axes[0].profile.deadzone, 0.10F);
}

void MappingCoreTests::malformedProfileConfigurationFallsBackSafely()
{
    QJsonObject malformed = ConfigStore::toJson(defaultConfiguration());
    malformed.insert(QStringLiteral("profiles"), QStringLiteral("not an array"));
    bool valid = true;
    const MapperConfiguration restored = ConfigStore::fromJson(malformed, &valid);
    QVERIFY(!valid);
    QCOMPARE(restored.activeProfileId, normalProfileId());
    QVERIFY(findProfile(restored, normalProfileId()));
    QVERIFY(findProfile(restored, precisionProfileId()));
}

QTEST_APPLESS_MAIN(MappingCoreTests)

#include "mapping_core_tests.moc"
