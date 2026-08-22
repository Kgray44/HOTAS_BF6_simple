#include "axis_transform.h"
#include "button_mapping.h"
#include "config_store.h"
#include "physical_input_monitor.h"

#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace hotas;

namespace {
bool nearlyEqual(float left, float right)
{
    return std::abs(left - right) < 0.0001F;
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
    void defaultButtonPassthroughIsCapacityBounded();
    void buttonMappingPropagatesPressAndRelease();
    void stoppingMappingReleasesVirtualButtons();
    void disabledAndInvalidButtonsDoNotMap();
    void duplicateButtonDestinationIsRejected();
    void buttonConfigurationRoundTripsAndMigratesV1();
    void malformedButtonConfigurationPreservesAxisConfiguration();
    void physicalMonitorPublishesWhenMappingIsStoppedAndVJoyIsUnavailable();
    void physicalMonitorRetainsFullOfflineButtonCount();
    void physicalMonitorPropagatesPressReleaseAndDisconnect();
    void physicalMonitorRecoversAfterReconnect();
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
    AxisMapping mapping;
    mapping.deadzone = 0.10F;
    mapping.inverted = true;
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
    configuration.preferredDeviceId = QStringLiteral("{0D15EA5E-0000-0000-0000-000000000001}");
    configuration.vjoyDeviceId = 2;
    configuration.startMappingOnLaunch = true;
    configuration.axes[0].inverted = true;
    configuration.axes[1].deadzone = 0.12F;
    configuration.axes[2].calibration = {true, -0.9F, 0.1F, 0.8F};

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.preferredDeviceId, configuration.preferredDeviceId);
    QCOMPARE(restored.vjoyDeviceId, 2);
    QVERIFY(restored.startMappingOnLaunch);
    QVERIFY(restored.axes[0].inverted);
    QCOMPARE(restored.axes[1].deadzone, 0.12F);
    QVERIFY(restored.axes[2].calibration.enabled);
    QCOMPARE(restored.axes[2].calibration.center, 0.1F);
}

void MappingCoreTests::duplicateMappingIsRejectedAndNormalized()
{
    MapperConfiguration configuration = defaultConfiguration();
    QVERIFY(hasMappingConflict(configuration, static_cast<int>(PhysicalAxis::Z), VirtualAxis::X));
    QVERIFY(!hasMappingConflict(configuration, static_cast<int>(PhysicalAxis::Z), VirtualAxis::Disabled));
    configuration.axes[static_cast<int>(PhysicalAxis::Z)].target = VirtualAxis::X;
    QVERIFY(!normalizeMappingConflicts(configuration));
    QCOMPARE(configuration.axes[static_cast<int>(PhysicalAxis::Z)].target, VirtualAxis::Disabled);
}

void MappingCoreTests::defaultButtonPassthroughIsCapacityBounded()
{
    const ButtonBindings bindings = defaultButtonMappings(5, 3);
    QCOMPARE(static_cast<int>(bindings.size()), 5);
    QCOMPARE(bindings[0].target, 1);
    QCOMPARE(bindings[2].target, 3);
    QCOMPARE(bindings[3].type, ButtonActionType::Disabled);
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
    configuration.buttons = defaultButtonMappings(4, 4);
    bool valid = false;
    const QJsonObject v2 = ConfigStore::toJson(configuration);
    const MapperConfiguration restored = ConfigStore::fromJson(v2, &valid);
    QVERIFY(valid);
    QCOMPARE(static_cast<int>(restored.buttons.size()), 4);
    QCOMPARE(restored.buttons[3].target, 4);

    QJsonObject v1 = v2;
    v1.insert(QStringLiteral("version"), 1);
    v1.remove(QStringLiteral("buttons"));
    const MapperConfiguration migrated = ConfigStore::fromJson(v1, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.buttons.empty());
    QCOMPARE(migrated.axes[static_cast<int>(PhysicalAxis::X)].target, VirtualAxis::X);
}

void MappingCoreTests::malformedButtonConfigurationPreservesAxisConfiguration()
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.axes[static_cast<int>(PhysicalAxis::Y)].inverted = true;
    QJsonObject malformed = ConfigStore::toJson(configuration);
    malformed.insert(QStringLiteral("buttons"), QStringLiteral("not an array"));
    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(malformed, &valid);
    QVERIFY(valid);
    QVERIFY(restored.buttons.empty());
    QVERIFY(restored.axes[static_cast<int>(PhysicalAxis::Y)].inverted);
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

QTEST_APPLESS_MAIN(MappingCoreTests)

#include "mapping_core_tests.moc"
