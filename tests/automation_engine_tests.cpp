#include "automation_engine.h"
#include "adaptive_response.h"
#include "config_store.h"
#include "profile_trigger_runtime.h"

#include <QtTest>

#include <chrono>
#include <cmath>

namespace {

using namespace hotas;
using namespace Qt::StringLiterals;

AutomationDefinition rule(const QString &name, AutomationConditionDefinition condition,
                          AutomationActionDefinition action)
{
    AutomationDefinition definition;
    definition.id = name.toLower().replace(u' ', u'-');
    definition.name = name;
    definition.conditions.push_back(std::move(condition));
    definition.actions.push_back(std::move(action));
    return definition;
}

AutomationConditionDefinition always()
{
    return {AutomationConditionType::Always};
}

AutomationInputSnapshot input()
{
    AutomationInputSnapshot snapshot;
    snapshot.povs.fill(-1);
    snapshot.axisAvailable.fill(true);
    snapshot.buttonCount = kMaximumPhysicalButtons;
    return snapshot;
}

void setTimestamp(AutomationInputSnapshot &snapshot, int milliseconds)
{
    snapshot.timestamp = std::chrono::steady_clock::time_point{}
        + std::chrono::milliseconds(milliseconds);
}

const AutomationEvaluationResult &evaluate(AutomationRuntime &runtime,
                                            const RuntimeProfileCache &cache,
                                            const AutomationInputSnapshot &snapshot)
{
    runtime.setCompiled(cache.automation.get());
    return runtime.evaluate(snapshot);
}

class AutomationEngineTests final : public QObject {
    Q_OBJECT

private slots:
    void axisThresholdHysteresis();
    void togglesOnlyOnActivationEdge();
    void allAnyAndPovConditions();
    void deterministicAxisComposition();
    void rejectsFeedbackAndClampConflicts();
    void profileOverrideUsesUnifiedPrecedence();
    void migrationDefaultsToEnabledEmptyEngine();
    void disabledEmptyDraftPersistsWithoutPublishing();
    void legacyAlwaysRuleRoundTripsWithoutChangingBehavior();
    void masterDisableClearsLatchedAutomationState();
    void buttonPressedAndReleasedAreTrueEvents();
    void multiPressResetsAfterItsWindow();
    void longPressFiresOnlyOncePerHold();
    void ruleToggleAndTimedActivationModes();
    void virtualButtonTapIsNonBlockingAndResets();
    void axisCrossingUsesHysteresisAndRearms();
    void v183AutomationMigratesWithSafeTemporalDefaults();
    void temporalStateResetsOnStopDisconnectAndConfigurationSwap();
    void mappingControlsPublishOnlyOnActivationEdges();
    void adaptiveResponseOverlaysAreCompiledAndPriorityResolved();
};

void AutomationEngineTests::axisThresholdHysteresis()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::AxisAbove;
    condition.axis = static_cast<int>(PhysicalAxis::Z);
    condition.minimum = 0.50F;
    condition.hysteresis = 0.10F;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 20;
    configuration.automations.push_back(rule(u"Afterburner"_qs, condition, action));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    snapshot.physicalAxes[static_cast<size_t>(PhysicalAxis::Z)] = 0.51F;
    QVERIFY(evaluate(runtime, cache, snapshot).heldButtons[20]);
    snapshot.physicalAxes[static_cast<size_t>(PhysicalAxis::Z)] = 0.43F;
    QVERIFY(evaluate(runtime, cache, snapshot).heldButtons[20]);
    snapshot.physicalAxes[static_cast<size_t>(PhysicalAxis::Z)] = 0.39F;
    QVERIFY(!evaluate(runtime, cache, snapshot).heldButtons[20]);
}

void AutomationEngineTests::togglesOnlyOnActivationEdge()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonHeld;
    condition.button = 5;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonToggle;
    action.virtualButton = 20;
    configuration.automations.push_back(rule(u"Toggle"_qs, condition, action));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    QVERIFY(!evaluate(runtime, cache, snapshot).toggledButtons[20]);
    snapshot.buttons[4] = true;
    QVERIFY(evaluate(runtime, cache, snapshot).toggledButtons[20]);
    QVERIFY(evaluate(runtime, cache, snapshot).toggledButtons[20]);
    snapshot.buttons[4] = false;
    QVERIFY(evaluate(runtime, cache, snapshot).toggledButtons[20]);
    snapshot.buttons[4] = true;
    QVERIFY(!evaluate(runtime, cache, snapshot).toggledButtons[20]);
}

void AutomationEngineTests::allAnyAndPovConditions()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationDefinition any;
    any.id = u"pov-any"_qs;
    any.name = u"POV Any"_qs;
    any.matchMode = AutomationMatchMode::Any;
    AutomationConditionDefinition up;
    up.type = AutomationConditionType::PovActive;
    up.povHat = 1;
    up.povDirection = PovDirection::Up;
    AutomationConditionDefinition right = up;
    right.povDirection = PovDirection::Right;
    any.conditions = {up, right};
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 21;
    any.actions = {action};
    configuration.automations.push_back(any);
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    snapshot.povCount = 1;
    snapshot.povs[0] = 9000;
    QVERIFY(evaluate(runtime, cache, snapshot).heldButtons[21]);
    snapshot.povs[0] = 18000;
    QVERIFY(!evaluate(runtime, cache, snapshot).heldButtons[21]);
}

void AutomationEngineTests::deterministicAxisComposition()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationActionDefinition follow;
    follow.type = AutomationActionType::AxisFollow;
    follow.sourceAxis = static_cast<int>(PhysicalAxis::X);
    follow.targetAxis = static_cast<int>(PhysicalAxis::Y);
    follow.sourceStage = AutomationAxisSourceStage::Processed;
    follow.value = 0.5F;
    follow.offset = 0.1F;
    configuration.automations.push_back(rule(u"Follow"_qs, always(), follow));
    AutomationActionDefinition scale;
    scale.type = AutomationActionType::AxisScale;
    scale.targetAxis = static_cast<int>(PhysicalAxis::Y);
    scale.value = 0.5F;
    configuration.automations.push_back(rule(u"Scale"_qs, always(), scale));
    AutomationActionDefinition mix;
    mix.type = AutomationActionType::AxisMix;
    mix.sourceAxis = static_cast<int>(PhysicalAxis::Z);
    mix.targetAxis = static_cast<int>(PhysicalAxis::Y);
    mix.sourceStage = AutomationAxisSourceStage::Physical;
    mix.value = 0.2F;
    configuration.automations.push_back(rule(u"Mix"_qs, always(), mix));
    AutomationActionDefinition offset;
    offset.type = AutomationActionType::AxisOffset;
    offset.targetAxis = static_cast<int>(PhysicalAxis::Y);
    offset.value = 0.1F;
    configuration.automations.push_back(rule(u"Offset"_qs, always(), offset));
    AutomationActionDefinition clamp;
    clamp.type = AutomationActionType::AxisClamp;
    clamp.targetAxis = static_cast<int>(PhysicalAxis::Y);
    clamp.minimum = -0.2F;
    clamp.maximum = 0.6F;
    configuration.automations.push_back(rule(u"Clamp"_qs, always(), clamp));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    snapshot.physicalAxes[static_cast<size_t>(PhysicalAxis::Z)] = 0.5F;
    evaluate(runtime, cache, snapshot);
    std::array<float, kPhysicalAxisCount> processed{};
    processed[static_cast<size_t>(PhysicalAxis::X)] = 0.4F;
    processed[static_cast<size_t>(PhysicalAxis::Y)] = 0.2F;
    runtime.applyAxisActions(snapshot, processed);
    QVERIFY(std::abs(processed[static_cast<size_t>(PhysicalAxis::Y)] - 0.35F) < 0.0001F);
}

void AutomationEngineTests::rejectsFeedbackAndClampConflicts()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition profile;
    profile.type = AutomationConditionType::EffectiveProfileIs;
    profile.profileId = normalProfileId();
    AutomationActionDefinition profileAction;
    profileAction.type = AutomationActionType::ProfileHold;
    profileAction.profileId = precisionProfileId();
    configuration.automations.push_back(rule(u"Feedback"_qs, profile, profileAction));
    RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QVERIFY(cache.automation->ruleHealth[0] == AutomationHealth::Invalid);

    configuration.automations.clear();
    AutomationActionDefinition lower;
    lower.type = AutomationActionType::AxisClamp;
    lower.targetAxis = static_cast<int>(PhysicalAxis::X);
    lower.minimum = -1.0F;
    lower.maximum = -0.5F;
    AutomationActionDefinition upper = lower;
    upper.minimum = 0.5F;
    upper.maximum = 1.0F;
    configuration.automations.push_back(rule(u"Low Clamp"_qs, always(), lower));
    configuration.automations.push_back(rule(u"High Clamp"_qs, always(), upper));
    cache = compileRuntimeProfileCache(configuration);
    QVERIFY(!cache.automation->publishable);
}

void AutomationEngineTests::profileOverrideUsesUnifiedPrecedence()
{
    MapperConfiguration configuration = defaultConfiguration();
    ControllerProfile custom = defaultProfile(u"profile-custom"_qs, u"Custom"_qs);
    configuration.profiles.push_back(custom);
    configuration.profileTriggers.resize(1);
    configuration.profileTriggers[0] = {precisionProfileId(), ProfileTriggerMode::Hold};
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    ProfileTriggerRuntime runtime;
    PhysicalButtonStates buttons{};
    buttons[0] = true;
    PhysicalPovValues povs{};
    povs.fill(-1);
    runtime.initializeForMapping(cache, buttons, povs, 0);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, 1);
    std::array<AutomationProfileContribution, kMaximumAutomationProfileContributors> contributions{};
    contributions[0] = {0, 0, 2, ProfileTriggerMode::Hold, true, true};
    runtime.updateAutomationContributions(contributions, 1, 3);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, 2);
    contributions[0].active = false;
    runtime.updateAutomationContributions(contributions, 1, 3);
    QCOMPARE(runtime.effectiveProfile(cache).profileIndex, 1);
}

void AutomationEngineTests::migrationDefaultsToEnabledEmptyEngine()
{
    MapperConfiguration source = defaultConfiguration();
    QJsonObject legacy = ConfigStore::toJson(source);
    legacy.insert(u"version"_qs, 10);
    legacy.remove(u"automationEnabled"_qs);
    legacy.remove(u"automations"_qs);
    bool valid = false;
    const MapperConfiguration migrated = ConfigStore::fromJson(legacy, &valid);
    QVERIFY(valid);
    QVERIFY(migrated.automationEnabled);
    QVERIFY(migrated.automations.empty());
}

void AutomationEngineTests::disabledEmptyDraftPersistsWithoutPublishing()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationDefinition draft;
    draft.id = u"new-automation"_qs;
    draft.name = u"New Automation"_qs;
    draft.enabled = false;
    configuration.automations.push_back(draft);

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.automations.size(), size_t{1});
    QVERIFY(!restored.automations.front().enabled);
    QVERIFY(restored.automations.front().conditions.empty());
    QVERIFY(restored.automations.front().actions.empty());

    const RuntimeProfileCache cache = compileRuntimeProfileCache(restored);
    QVERIFY(cache.automation);
    QCOMPARE(cache.automation->ruleHealth[0], AutomationHealth::Invalid);
    QVERIFY(!cache.automation->rules[0].enabled);
}

void AutomationEngineTests::legacyAlwaysRuleRoundTripsWithoutChangingBehavior()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationDefinition legacy;
    legacy.id = u"legacy-always-and-button"_qs;
    legacy.name = u"Legacy always plus button"_qs;
    legacy.matchMode = AutomationMatchMode::All;
    legacy.conditions = {always(), {AutomationConditionType::ButtonHeld, 0, 0.0F, 0.0F,
        0.0F, 6}};
    AutomationActionDefinition scale;
    scale.type = AutomationActionType::AxisScale;
    scale.targetAxis = static_cast<int>(PhysicalAxis::X);
    scale.value = 0.60F;
    legacy.actions = {scale};
    configuration.automations.push_back(legacy);

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.automations.size(), size_t{1});
    QCOMPARE(restored.automations.front().matchMode, AutomationMatchMode::All);
    QCOMPARE(restored.automations.front().conditions.size(), size_t{2});
    QCOMPARE(restored.automations.front().conditions[0].type, AutomationConditionType::Always);
    QCOMPARE(restored.automations.front().conditions[1].type, AutomationConditionType::ButtonHeld);
    QCOMPARE(restored.automations.front().conditions[1].button, 6);
    QCOMPARE(restored.automations.front().actions[0].type, AutomationActionType::AxisScale);
    QCOMPARE(restored.automations.front().actions[0].value, 0.60F);

    const RuntimeProfileCache cache = compileRuntimeProfileCache(restored);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    snapshot.buttons[5] = true;
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[5] = false;
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
}

void AutomationEngineTests::masterDisableClearsLatchedAutomationState()
{
    MapperConfiguration enabled = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonHeld;
    condition.button = 1;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonToggle;
    action.virtualButton = 20;
    enabled.automations.push_back(rule(u"Latch"_qs, condition, action));
    const RuntimeProfileCache enabledCache = compileRuntimeProfileCache(enabled);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    snapshot.buttons[0] = true;
    QVERIFY(evaluate(runtime, enabledCache, snapshot).toggledButtons[20]);
    MapperConfiguration disabled = enabled;
    disabled.automationEnabled = false;
    const RuntimeProfileCache disabledCache = compileRuntimeProfileCache(disabled);
    QVERIFY(!evaluate(runtime, disabledCache, snapshot).toggledButtons[20]);
}

void AutomationEngineTests::buttonPressedAndReleasedAreTrueEvents()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition pressed;
    pressed.type = AutomationConditionType::ButtonPressed;
    pressed.button = 2;
    AutomationActionDefinition hold;
    hold.type = AutomationActionType::VJoyButtonHold;
    hold.virtualButton = 20;
    configuration.automations.push_back(rule(u"Pressed"_qs, pressed, hold));
    AutomationConditionDefinition released = pressed;
    released.type = AutomationConditionType::ButtonReleaseEvent;
    hold.virtualButton = 21;
    configuration.automations.push_back(rule(u"Released"_qs, released, hold));

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[1] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 20);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[1] = false;
    setTimestamp(snapshot, 30);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[1]);
    setTimestamp(snapshot, 40);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[1]);
    snapshot.buttons[1] = true;
    setTimestamp(snapshot, 50);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
}

void AutomationEngineTests::multiPressResetsAfterItsWindow()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonMultiPress;
    condition.button = 3;
    condition.pressCount = 2;
    condition.multiPressWindowMs = 350;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 22;
    configuration.automations.push_back(rule(u"Double Press"_qs, condition, action));

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[2] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[2] = false;
    setTimestamp(snapshot, 20);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[2] = true;
    setTimestamp(snapshot, 100);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[2] = false;
    setTimestamp(snapshot, 110);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[2] = true;
    setTimestamp(snapshot, 200);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[2] = false;
    setTimestamp(snapshot, 210);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[2] = true;
    setTimestamp(snapshot, 700);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[2] = false;
    setTimestamp(snapshot, 710);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[2] = true;
    setTimestamp(snapshot, 800);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
}

void AutomationEngineTests::longPressFiresOnlyOncePerHold()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonLongPress;
    condition.button = 4;
    condition.longPressDurationMs = 600;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 23;
    configuration.automations.push_back(rule(u"Long Press"_qs, condition, action));

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[3] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 609);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 610);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 700);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[3] = false;
    setTimestamp(snapshot, 710);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[3] = true;
    setTimestamp(snapshot, 720);
    evaluate(runtime, cache, snapshot);
    setTimestamp(snapshot, 1320);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
}

void AutomationEngineTests::ruleToggleAndTimedActivationModes()
{
    MapperConfiguration toggleConfiguration = defaultConfiguration();
    AutomationConditionDefinition pressed;
    pressed.type = AutomationConditionType::ButtonPressed;
    pressed.button = 5;
    AutomationActionDefinition scale;
    scale.type = AutomationActionType::AxisScale;
    scale.targetAxis = static_cast<int>(PhysicalAxis::X);
    scale.value = 0.5F;
    AutomationDefinition toggle = rule(u"Precision"_qs, pressed, scale);
    toggle.activationMode = AutomationActivationMode::ToggleOnTrigger;
    AutomationActionDefinition buttonHold;
    buttonHold.type = AutomationActionType::VJoyButtonHold;
    buttonHold.virtualButton = 29;
    toggle.actions.push_back(buttonHold);
    toggleConfiguration.automations.push_back(toggle);
    const RuntimeProfileCache toggleCache = compileRuntimeProfileCache(toggleConfiguration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, toggleCache, snapshot);
    snapshot.buttons[4] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(evaluate(runtime, toggleCache, snapshot).activeRules[0]);
    QVERIFY(evaluate(runtime, toggleCache, snapshot).heldButtons[29]);
    std::array<float, kPhysicalAxisCount> axes{};
    axes[0] = 1.0F;
    runtime.applyAxisActions(snapshot, axes);
    QCOMPARE(axes[0], 0.5F);
    snapshot.buttons[4] = false;
    setTimestamp(snapshot, 20);
    QVERIFY(evaluate(runtime, toggleCache, snapshot).activeRules[0]);
    snapshot.buttons[4] = true;
    setTimestamp(snapshot, 30);
    QVERIFY(!evaluate(runtime, toggleCache, snapshot).activeRules[0]);
    QVERIFY(!evaluate(runtime, toggleCache, snapshot).heldButtons[29]);

    MapperConfiguration timedConfiguration = defaultConfiguration();
    AutomationActionDefinition hold;
    hold.type = AutomationActionType::VJoyButtonHold;
    hold.virtualButton = 24;
    AutomationDefinition timed = rule(u"Brief"_qs, pressed, hold);
    timed.activationMode = AutomationActivationMode::RunBriefly;
    timed.activeDurationMs = 250;
    timedConfiguration.automations.push_back(timed);
    const RuntimeProfileCache timedCache = compileRuntimeProfileCache(timedConfiguration);
    AutomationRuntime timedRuntime;
    snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(timedRuntime, timedCache, snapshot);
    snapshot.buttons[4] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(evaluate(timedRuntime, timedCache, snapshot).activeRules[0]);
    snapshot.buttons[4] = false;
    setTimestamp(snapshot, 259);
    QVERIFY(evaluate(timedRuntime, timedCache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 260);
    QVERIFY(!evaluate(timedRuntime, timedCache, snapshot).activeRules[0]);
    snapshot.buttons[4] = true;
    setTimestamp(snapshot, 270);
    QVERIFY(evaluate(timedRuntime, timedCache, snapshot).activeRules[0]);
}

void AutomationEngineTests::virtualButtonTapIsNonBlockingAndResets()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonPressed;
    condition.button = 6;
    AutomationActionDefinition tap;
    tap.type = AutomationActionType::VJoyButtonTap;
    tap.virtualButton = 25;
    tap.tapDurationMs = 80;
    configuration.automations.push_back(rule(u"Tap"_qs, condition, tap));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[5] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(evaluate(runtime, cache, snapshot).pulsedButtons[25]);
    setTimestamp(snapshot, 89);
    QVERIFY(evaluate(runtime, cache, snapshot).pulsedButtons[25]);
    setTimestamp(snapshot, 90);
    QVERIFY(!evaluate(runtime, cache, snapshot).pulsedButtons[25]);

    MapperConfiguration disabled = configuration;
    disabled.automationEnabled = false;
    const RuntimeProfileCache disabledCache = compileRuntimeProfileCache(disabled);
    QVERIFY(!evaluate(runtime, disabledCache, snapshot).pulsedButtons[25]);
    QVERIFY(!evaluate(runtime, cache, snapshot).pulsedButtons[25]);
}

void AutomationEngineTests::axisCrossingUsesHysteresisAndRearms()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition above;
    above.type = AutomationConditionType::AxisCrossesAbove;
    above.axis = static_cast<int>(PhysicalAxis::X);
    above.minimum = 0.5F;
    above.hysteresis = 0.1F;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 26;
    configuration.automations.push_back(rule(u"Above"_qs, above, action));
    AutomationConditionDefinition below;
    below.type = AutomationConditionType::AxisCrossesBelow;
    below.axis = static_cast<int>(PhysicalAxis::Y);
    below.minimum = -0.5F;
    below.hysteresis = 0.1F;
    action.virtualButton = 27;
    configuration.automations.push_back(rule(u"Below"_qs, below, action));

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, cache, snapshot);
    snapshot.physicalAxes[0] = 0.6F;
    setTimestamp(snapshot, 10);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);
    setTimestamp(snapshot, 20);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.physicalAxes[0] = 0.45F;
    setTimestamp(snapshot, 30);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.physicalAxes[0] = 0.4F;
    setTimestamp(snapshot, 40);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.physicalAxes[0] = 0.6F;
    setTimestamp(snapshot, 50);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[0]);

    snapshot.physicalAxes[1] = -0.6F;
    setTimestamp(snapshot, 60);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[1]);
    snapshot.physicalAxes[1] = -0.7F;
    setTimestamp(snapshot, 70);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[1]);
    snapshot.physicalAxes[1] = -0.4F;
    setTimestamp(snapshot, 80);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[1]);
    snapshot.physicalAxes[1] = -0.6F;
    setTimestamp(snapshot, 90);
    QVERIFY(evaluate(runtime, cache, snapshot).activeRules[1]);
}

void AutomationEngineTests::v183AutomationMigratesWithSafeTemporalDefaults()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonHeld;
    condition.button = 7;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 28;
    configuration.automations.push_back(rule(u"v183 rule"_qs, condition, action));
    QJsonObject v183 = ConfigStore::toJson(configuration);
    v183.insert(u"version"_qs, 11);
    QJsonArray automations = v183.value(u"automations"_qs).toArray();
    QJsonObject oldRule = automations.first().toObject();
    oldRule.remove(u"activationMode"_qs);
    oldRule.remove(u"activeDurationMs"_qs);
    QJsonArray conditions = oldRule.value(u"conditions"_qs).toArray();
    QJsonObject oldCondition = conditions.first().toObject();
    oldCondition.remove(u"pressCount"_qs);
    oldCondition.remove(u"multiPressWindowMs"_qs);
    oldCondition.remove(u"longPressDurationMs"_qs);
    conditions[0] = oldCondition;
    oldRule.insert(u"conditions"_qs, conditions);
    QJsonArray actions = oldRule.value(u"actions"_qs).toArray();
    QJsonObject oldAction = actions.first().toObject();
    oldAction.remove(u"tapDurationMs"_qs);
    actions[0] = oldAction;
    oldRule.insert(u"actions"_qs, actions);
    automations[0] = oldRule;
    v183.insert(u"automations"_qs, automations);

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(v183, &valid);
    QVERIFY(valid);
    QCOMPARE(restored.automations.size(), size_t{1});
    QCOMPARE(restored.automations[0].activationMode,
             AutomationActivationMode::WhileTriggerActive);
    QCOMPARE(restored.automations[0].activeDurationMs, 250);
    QCOMPARE(restored.automations[0].conditions[0].pressCount, 2);
    QCOMPARE(restored.automations[0].conditions[0].multiPressWindowMs, 350);
    QCOMPARE(restored.automations[0].conditions[0].longPressDurationMs, 600);
    QCOMPARE(restored.automations[0].actions[0].tapDurationMs, 80);
}

void AutomationEngineTests::temporalStateResetsOnStopDisconnectAndConfigurationSwap()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonMultiPress;
    condition.button = 8;
    condition.pressCount = 2;
    AutomationActionDefinition action;
    action.type = AutomationActionType::VJoyButtonHold;
    action.virtualButton = 30;
    configuration.automations.push_back(rule(u"Reset sequence"_qs, condition, action));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    setTimestamp(snapshot, 0);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[7] = true;
    setTimestamp(snapshot, 10);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[7] = false;
    setTimestamp(snapshot, 20);
    evaluate(runtime, cache, snapshot);

    // Mapping stop and controller disconnect both call this runtime reset.
    runtime.reset();
    snapshot.buttons[7] = true;
    setTimestamp(snapshot, 30);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[7] = false;
    setTimestamp(snapshot, 40);
    evaluate(runtime, cache, snapshot);
    snapshot.buttons[7] = true;
    setTimestamp(snapshot, 45);
    QVERIFY(!evaluate(runtime, cache, snapshot).activeRules[0]);
    snapshot.buttons[7] = false;
    setTimestamp(snapshot, 46);
    evaluate(runtime, cache, snapshot);

    // Replacing the immutable compiled table also discards partial sequences.
    MapperConfiguration replacement = configuration;
    replacement.automations[0].priority = 51;
    const RuntimeProfileCache replacementCache = compileRuntimeProfileCache(replacement);
    snapshot.buttons[7] = false;
    setTimestamp(snapshot, 50);
    QVERIFY(!evaluate(runtime, replacementCache, snapshot).activeRules[0]);
    snapshot.buttons[7] = true;
    setTimestamp(snapshot, 60);
    QVERIFY(!evaluate(runtime, replacementCache, snapshot).activeRules[0]);
    snapshot.buttons[7] = false;
    setTimestamp(snapshot, 70);
    evaluate(runtime, replacementCache, snapshot);
    snapshot.buttons[7] = true;
    setTimestamp(snapshot, 80);
    QVERIFY(evaluate(runtime, replacementCache, snapshot).activeRules[0]);
}

void AutomationEngineTests::mappingControlsPublishOnlyOnActivationEdges()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationConditionDefinition condition;
    condition.type = AutomationConditionType::ButtonHeld;
    condition.button = 3;
    AutomationActionDefinition action;
    action.type = AutomationActionType::ToggleMapping;
    configuration.automations.push_back(rule(u"Toggle mapper"_qs, condition, action));
    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QVERIFY(cache.automation->publishable);

    AutomationRuntime runtime;
    AutomationInputSnapshot snapshot = input();
    QCOMPARE(evaluate(runtime, cache, snapshot).mappingControlAction, MappingControlAction::None);
    snapshot.buttons[2] = true;
    QCOMPARE(evaluate(runtime, cache, snapshot).mappingControlAction,
             MappingControlAction::ToggleMapping);
    // Holding the same control does not cause repeated mapping state changes.
    QCOMPARE(evaluate(runtime, cache, snapshot).mappingControlAction, MappingControlAction::None);
    snapshot.buttons[2] = false;
    QCOMPARE(evaluate(runtime, cache, snapshot).mappingControlAction, MappingControlAction::None);
    snapshot.buttons[2] = true;
    QCOMPARE(evaluate(runtime, cache, snapshot).mappingControlAction,
             MappingControlAction::ToggleMapping);

    // The off-state control path has independent edge state and does not
    // publish ordinary game actions from the same Automation rule set.
    runtime.reset();
    snapshot.buttons[2] = false;
    runtime.evaluateMappingControls(snapshot);
    snapshot.buttons[2] = true;
    const AutomationEvaluationResult &controlOnly = runtime.evaluateMappingControls(snapshot);
    QCOMPARE(controlOnly.mappingControlAction, MappingControlAction::ToggleMapping);
    QVERIFY(!controlOnly.heldButtons[1]);
}

void AutomationEngineTests::adaptiveResponseOverlaysAreCompiledAndPriorityResolved()
{
    MapperConfiguration configuration = defaultConfiguration();
    AutomationActionDefinition enable;
    enable.type = AutomationActionType::AdaptiveResponseEnable;
    enable.targetAxis = static_cast<int>(PhysicalAxis::X);
    AutomationDefinition lowPriority = rule(u"Response enabled"_qs, always(), enable);
    lowPriority.priority = 20;

    AutomationActionDefinition preset;
    preset.type = AutomationActionType::AdaptiveResponsePreset;
    preset.targetAxis = static_cast<int>(PhysicalAxis::X);
    preset.adaptiveResponsePresetId = QStringLiteral("fast");
    AutomationDefinition highPriority = rule(u"Response fast"_qs, always(), preset);
    highPriority.priority = 80;
    configuration.automations = {lowPriority, highPriority};

    const RuntimeProfileCache cache = compileRuntimeProfileCache(configuration);
    QVERIFY(cache.automation->publishable);
    AutomationRuntime runtime;
    const AutomationEvaluationResult &result = evaluate(runtime, cache, input());
    const RuntimeAdaptiveResponseOverride &overlay = result.adaptiveResponseOverlays[
        static_cast<size_t>(PhysicalAxis::X)];
    QVERIFY(overlay.active);
    QVERIFY((overlay.properties & AdaptiveResponseMaximumHorizon) != 0);
    QCOMPARE(overlay.settings.maximumHorizonMs, 12.0F);

    RuntimeAdaptiveResponseConfig base;
    const RuntimeAdaptiveResponseConfig effective = applyAdaptiveResponseRuntimeOverride(base, overlay);
    QVERIFY(effective.enabled);
    QCOMPARE(effective.maximumHorizonSeconds, 0.012F);

    bool valid = false;
    const MapperConfiguration restored = ConfigStore::fromJson(ConfigStore::toJson(configuration), &valid);
    QVERIFY(valid);
    QCOMPARE(restored.automations[1].actions[0].adaptiveResponsePresetId, QStringLiteral("fast"));
}

} // namespace

QTEST_MAIN(AutomationEngineTests)
#include "automation_engine_tests.moc"
