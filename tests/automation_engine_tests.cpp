#include "automation_engine.h"
#include "config_store.h"
#include "profile_trigger_runtime.h"

#include <QtTest>

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
    void masterDisableClearsLatchedAutomationState();
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

} // namespace

QTEST_MAIN(AutomationEngineTests)
#include "automation_engine_tests.moc"
