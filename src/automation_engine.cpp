#include "automation_engine.h"

#include "adaptive_response.h"

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {

bool validAxis(int axis)
{
    return axis >= 0 && axis < kPhysicalAxisCount;
}

bool validProfileIndex(int index, const RuntimeProfileCache &cache)
{
    return index >= 0 && index < static_cast<int>(cache.profiles.size());
}

int profileIndexForId(const MapperConfiguration &configuration, const QString &id)
{
    for (int index = 0; index < static_cast<int>(configuration.profiles.size()); ++index) {
        if (configuration.profiles[static_cast<size_t>(index)].id == id) return index;
    }
    return -1;
}

bool finite(float value)
{
    return std::isfinite(value);
}

bool isEventCondition(AutomationConditionType type)
{
    return type == AutomationConditionType::ButtonPressed
        || type == AutomationConditionType::ButtonReleaseEvent
        || type == AutomationConditionType::ButtonMultiPress
        || type == AutomationConditionType::ButtonLongPress
        || type == AutomationConditionType::AxisCrossesAbove
        || type == AutomationConditionType::AxisCrossesBelow;
}

void copyAdaptiveResponseProperty(AdaptiveResponseSettings &target,
                                  const AdaptiveResponseSettings &source,
                                  std::uint32_t property)
{
    switch (static_cast<AdaptiveResponseProperty>(property)) {
    case AdaptiveResponseEnabled: target.enabled = source.enabled; break;
    case AdaptiveResponseModelProperty: target.model = source.model; break;
    case AdaptiveResponseMaximumHorizon: target.maximumHorizonMs = source.maximumHorizonMs; break;
    case AdaptiveResponseMaximumLead: target.maximumLead = source.maximumLead; break;
    case AdaptiveResponseVelocityResponse: target.velocityResponse = source.velocityResponse; break;
    case AdaptiveResponseAccelerationResponse: target.accelerationResponse = source.accelerationResponse; break;
    case AdaptiveResponseMotionSensitivity: target.motionSensitivity = source.motionSensitivity; break;
    case AdaptiveResponseNoiseRejection: target.noiseRejection = source.noiseRejection; break;
    case AdaptiveResponseReversalDetection: target.reversalDetection = source.reversalDetection; break;
    case AdaptiveResponseReversalResponse: target.reversalResponse = source.reversalResponse; break;
    case AdaptiveResponseDecelerationResponse: target.decelerationResponse = source.decelerationResponse; break;
    case AdaptiveResponseSettlingResponse: target.settlingResponse = source.settlingResponse; break;
    case AdaptiveResponseEndpointTaper: target.endpointTaper = source.endpointTaper; break;
    }
}

MappingControlAction mappingControlForAutomationAction(AutomationActionType type)
{
    switch (type) {
    case AutomationActionType::MappingOn: return MappingControlAction::MappingOn;
    case AutomationActionType::MappingOff: return MappingControlAction::MappingOff;
    case AutomationActionType::ToggleMapping: return MappingControlAction::ToggleMapping;
    default: return MappingControlAction::None;
    }
}

QString ruleName(const AutomationDefinition &definition, int index)
{
    const QString trimmed = definition.name.trimmed();
    return trimmed.isEmpty() ? QString(u"Automation %1"_qs).arg(index + 1) : trimmed;
}

void invalidate(CompiledAutomationSet &set, int index, const QString &message)
{
    set.ruleHealth[static_cast<size_t>(index)] = AutomationHealth::Invalid;
    set.ruleMessages[static_cast<size_t>(index)] = message;
    set.health = AutomationHealth::Invalid;
}

void warn(CompiledAutomationSet &set, int index, const QString &message)
{
    if (set.ruleHealth[static_cast<size_t>(index)] == AutomationHealth::Invalid) return;
    set.ruleHealth[static_cast<size_t>(index)] = AutomationHealth::Warning;
    set.ruleMessages[static_cast<size_t>(index)] = message;
    if (set.health == AutomationHealth::Valid) set.health = AutomationHealth::Warning;
}

} // namespace

std::shared_ptr<const CompiledAutomationSet> compileAutomationSet(
    const MapperConfiguration &configuration, const RuntimeProfileCache &cache)
{
    auto result = std::make_shared<CompiledAutomationSet>();
    result->engineEnabled = configuration.automationEnabled;
    const int count = std::min(static_cast<int>(configuration.automations.size()),
                               kMaximumAutomationRules);
    result->ruleCount = count;
    if (static_cast<int>(configuration.automations.size()) > kMaximumAutomationRules) {
        result->publishable = false;
        result->health = AutomationHealth::Invalid;
        result->message = u"Automation limit is 64 rules."_qs;
    }

    for (int index = 0; index < count; ++index) {
        const AutomationDefinition &definition = configuration.automations[static_cast<size_t>(index)];
        CompiledAutomationRule &rule = result->rules[static_cast<size_t>(index)];
        result->ruleNames[static_cast<size_t>(index)] = ruleName(definition, index);
        result->ruleHealth[static_cast<size_t>(index)] = AutomationHealth::Valid;
        rule.sourceOrder = index;
        rule.priority = std::clamp(definition.priority, 0, 100);
        rule.matchMode = definition.matchMode;
        rule.activationMode = definition.activationMode;
        rule.activeDurationMs = definition.activeDurationMs;
        if (definition.name.trimmed().isEmpty() || definition.name.trimmed().size() > 64) {
            invalidate(*result, index, u"Automation name must contain 1–64 characters."_qs);
            continue;
        }
        if (definition.conditions.empty() || definition.conditions.size() > kMaximumAutomationConditions) {
            invalidate(*result, index, u"Each Automation needs one to four conditions."_qs);
            continue;
        }
        if (definition.actions.empty() || definition.actions.size() > kMaximumAutomationActions) {
            invalidate(*result, index, u"Each Automation needs one to four actions."_qs);
            continue;
        }
        if (definition.activationMode != AutomationActivationMode::WhileTriggerActive
            && definition.activationMode != AutomationActivationMode::ToggleOnTrigger
            && definition.activationMode != AutomationActivationMode::RunBriefly) {
            invalidate(*result, index, u"Automation behavior is invalid."_qs);
            continue;
        }
        if (definition.activeDurationMs < kAutomationMinimumRuleActiveDurationMs
            || definition.activeDurationMs > kAutomationMaximumRuleActiveDurationMs) {
            invalidate(*result, index, u"Brief Automation duration must be 20–5000 ms."_qs);
            continue;
        }

        bool hasEffectiveProfileCondition = false;
        bool hasProfileAction = false;
        bool hasAlwaysCondition = false;
        bool valid = true;
        rule.conditionCount = static_cast<int>(definition.conditions.size());
        rule.actionCount = static_cast<int>(definition.actions.size());
        for (int conditionIndex = 0; conditionIndex < rule.conditionCount; ++conditionIndex) {
            const AutomationConditionDefinition &source = definition.conditions[
                static_cast<size_t>(conditionIndex)];
            CompiledAutomationCondition &target = rule.conditions[static_cast<size_t>(conditionIndex)];
            target.type = source.type;
            target.minimum = source.minimum;
            target.maximum = source.maximum;
            target.hysteresis = source.hysteresis;
            target.pressCount = source.pressCount;
            target.multiPressWindowMs = source.multiPressWindowMs;
            target.longPressDurationMs = source.longPressDurationMs;
            if (!finite(target.minimum) || !finite(target.maximum) || !finite(target.hysteresis)
                || target.hysteresis < 0.0F || target.hysteresis > 1.0F) {
                invalidate(*result, index, u"Condition values must be finite and bounded."_qs);
                valid = false;
                break;
            }
            switch (source.type) {
            case AutomationConditionType::Always:
                hasAlwaysCondition = true;
                break;
            case AutomationConditionType::AxisAbove:
            case AutomationConditionType::AxisBelow:
            case AutomationConditionType::AxisBetween:
            case AutomationConditionType::AxisOutsideRange:
            case AutomationConditionType::AxisCrossesAbove:
            case AutomationConditionType::AxisCrossesBelow:
                if (!validAxis(source.axis) || source.minimum < -1.0F || source.maximum > 1.0F
                    || ((source.type == AutomationConditionType::AxisBetween
                         || source.type == AutomationConditionType::AxisOutsideRange)
                        && source.minimum > source.maximum)) {
                    invalidate(*result, index, u"Axis threshold or range is invalid."_qs);
                    valid = false;
                }
                target.source = source.axis;
                break;
            case AutomationConditionType::ButtonHeld:
            case AutomationConditionType::ButtonReleased:
            case AutomationConditionType::ButtonPressed:
            case AutomationConditionType::ButtonReleaseEvent:
            case AutomationConditionType::ButtonMultiPress:
            case AutomationConditionType::ButtonLongPress:
                if (source.button < 1 || source.button > kMaximumPhysicalButtons) {
                    invalidate(*result, index, u"Physical button reference is invalid."_qs);
                    valid = false;
                }
                if (source.type == AutomationConditionType::ButtonMultiPress
                    && (source.pressCount < 2 || source.pressCount > 5
                        || source.multiPressWindowMs < kAutomationMinimumMultiPressWindowMs
                        || source.multiPressWindowMs > kAutomationMaximumMultiPressWindowMs)) {
                    invalidate(*result, index,
                        u"Multi-press needs two to five presses and a 150–1000 ms gap."_qs);
                    valid = false;
                }
                if (source.type == AutomationConditionType::ButtonLongPress
                    && (source.longPressDurationMs < kAutomationMinimumLongPressDurationMs
                        || source.longPressDurationMs > kAutomationMaximumLongPressDurationMs)) {
                    invalidate(*result, index,
                        u"Long-press duration must be 200–3000 ms."_qs);
                    valid = false;
                }
                target.source = source.button - 1;
                break;
            case AutomationConditionType::PovActive:
            case AutomationConditionType::PovInactive:
                if (source.povHat < 1 || source.povHat > kMaximumPhysicalPovs
                    || povDirectionIndex(source.povDirection) < 0) {
                    invalidate(*result, index, u"POV reference is invalid."_qs);
                    valid = false;
                }
                target.source = (source.povHat - 1) * kPovDirectionCount
                    + povDirectionIndex(source.povDirection);
                break;
            case AutomationConditionType::BaseProfileIs:
            case AutomationConditionType::EffectiveProfileIs:
                target.source = profileIndexForId(configuration, source.profileId);
                if (!validProfileIndex(target.source, cache)) {
                    invalidate(*result, index, u"Target profile no longer exists."_qs);
                    valid = false;
                }
                hasEffectiveProfileCondition = hasEffectiveProfileCondition
                    || source.type == AutomationConditionType::EffectiveProfileIs;
                break;
            }
            if (!valid) break;
        }
        if (!valid) continue;

        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const AutomationActionDefinition &source = definition.actions[static_cast<size_t>(actionIndex)];
            CompiledAutomationAction &target = rule.actions[static_cast<size_t>(actionIndex)];
            target.type = source.type;
            target.value = source.value;
            target.offset = source.offset;
            target.minimum = source.minimum;
            target.maximum = source.maximum;
            target.sourceStage = source.sourceStage;
            target.tapDurationMs = source.tapDurationMs;
            if (!finite(target.value) || !finite(target.offset) || !finite(target.minimum) || !finite(target.maximum)) {
                invalidate(*result, index, u"Action values must be finite."_qs);
                valid = false;
                break;
            }
            switch (source.type) {
            case AutomationActionType::VJoyButtonHold:
            case AutomationActionType::VJoyButtonToggle:
            case AutomationActionType::VJoyButtonTap:
                if (source.virtualButton < 1 || source.virtualButton > kMaximumVirtualButtons) {
                    invalidate(*result, index, u"vJoy button reference is invalid."_qs);
                    valid = false;
                }
                if (source.type == AutomationActionType::VJoyButtonTap
                    && (source.tapDurationMs < kAutomationMinimumTapDurationMs
                        || source.tapDurationMs > kAutomationMaximumTapDurationMs)) {
                    invalidate(*result, index,
                        u"Virtual button tap duration must be 20–500 ms."_qs);
                    valid = false;
                }
                target.target = source.virtualButton;
                break;
            case AutomationActionType::ProfileHold:
            case AutomationActionType::ProfileToggle:
                target.profileIndex = profileIndexForId(configuration, source.profileId);
                if (!validProfileIndex(target.profileIndex, cache)) {
                    invalidate(*result, index, u"Target profile no longer exists."_qs);
                    valid = false;
                }
                hasProfileAction = true;
                break;
            case AutomationActionType::AxisScale:
            case AutomationActionType::AxisOffset:
            case AutomationActionType::AxisOverride:
            case AutomationActionType::AxisClamp:
                if (!validAxis(source.targetAxis) || (source.type == AutomationActionType::AxisClamp
                    && (source.minimum < -1.0F || source.maximum > 1.0F
                        || source.minimum > source.maximum))) {
                    invalidate(*result, index, source.type == AutomationActionType::AxisClamp
                        ? u"Clamp range is invalid."_qs : u"Target axis no longer exists."_qs);
                    valid = false;
                }
                target.target = source.targetAxis;
                break;
            case AutomationActionType::AxisMix:
            case AutomationActionType::AxisFollow:
                if (!validAxis(source.targetAxis) || !validAxis(source.sourceAxis)
                    || (source.type == AutomationActionType::AxisMix
                        && source.targetAxis == source.sourceAxis)) {
                    invalidate(*result, index, source.type == AutomationActionType::AxisMix
                        ? u"Axis Mix cannot use the same source and target."_qs
                        : u"Axis source or target no longer exists."_qs);
                    valid = false;
                }
                target.target = source.targetAxis;
                target.source = source.sourceAxis;
                break;
            case AutomationActionType::MappingOn:
            case AutomationActionType::MappingOff:
            case AutomationActionType::ToggleMapping:
                // No game-output payload: the worker consumes this compact
                // action at a state boundary.
                break;
            case AutomationActionType::AdaptiveResponseEnable:
            case AutomationActionType::AdaptiveResponseDisable:
                if (!validAxis(source.targetAxis)) {
                    invalidate(*result, index, u"Adaptive Response target axis is invalid."_qs);
                    valid = false;
                    break;
                }
                target.target = source.targetAxis;
                target.adaptiveResponse.active = true;
                target.adaptiveResponse.properties = AdaptiveResponseEnabled;
                target.adaptiveResponse.settings.enabled =
                    source.type == AutomationActionType::AdaptiveResponseEnable;
                break;
            case AutomationActionType::AdaptiveResponsePreset: {
                if (!validAxis(source.targetAxis)) {
                    invalidate(*result, index, u"Adaptive Response target axis is invalid."_qs);
                    valid = false;
                    break;
                }
                const AdaptiveResponsePreset *preset = findAdaptiveResponsePreset(
                    configuration, source.adaptiveResponsePresetId);
                if (!preset) {
                    invalidate(*result, index, u"Adaptive Response preset no longer exists."_qs);
                    valid = false;
                    break;
                }
                target.target = source.targetAxis;
                const AdaptiveResponseAxisOverride &presetAxis = preset->axes[
                    static_cast<size_t>(source.targetAxis)];
                target.adaptiveResponse.active = true;
                target.adaptiveResponse.properties = presetAxis.properties;
                target.adaptiveResponse.settings = presetAxis.settings;
                break;
            }
            }
            if (!valid) break;
        }
        if (!valid) continue;
        if (hasAlwaysCondition
            && definition.activationMode != AutomationActivationMode::WhileTriggerActive) {
            invalidate(*result, index,
                       u"All-the-time Automation must use while-active behavior."_qs);
            continue;
        }
        if (hasEffectiveProfileCondition && hasProfileAction) {
            invalidate(*result, index,
                       u"Effective Profile condition cannot drive a Profile action."_qs);
            continue;
        }
        if (rule.priority >= 90) {
            warn(*result, index, u"High-priority override may compete with other rules."_qs);
        }
        rule.enabled = definition.enabled;
        if (!rule.enabled) continue;
        for (int conditionIndex = 0; conditionIndex < rule.conditionCount; ++conditionIndex) {
            const AutomationConditionType type = rule.conditions[static_cast<size_t>(conditionIndex)].type;
            const bool event = type == AutomationConditionType::ButtonPressed
                || type == AutomationConditionType::ButtonReleaseEvent
                || type == AutomationConditionType::ButtonMultiPress
                || type == AutomationConditionType::ButtonLongPress
                || type == AutomationConditionType::AxisCrossesAbove
                || type == AutomationConditionType::AxisCrossesBelow;
            result->hasEventConditions = result->hasEventConditions || event;
            result->hasTemporalConditions = result->hasTemporalConditions
                || type == AutomationConditionType::ButtonMultiPress
                || type == AutomationConditionType::ButtonLongPress;
        }
        result->hasTimedActions = result->hasTimedActions
            || rule.activationMode == AutomationActivationMode::RunBriefly;
        result->hasLatchedRules = result->hasLatchedRules
            || rule.activationMode == AutomationActivationMode::ToggleOnTrigger;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            result->hasTimedActions = result->hasTimedActions
                || rule.actions[static_cast<size_t>(actionIndex)].type
                    == AutomationActionType::VJoyButtonTap;
        }
    }

    // Clamp intersections are order-independent. Reject an impossible set at
    // compile time instead of attempting a policy decision in the worker.
    std::array<float, kPhysicalAxisCount> low{};
    std::array<float, kPhysicalAxisCount> high{};
    low.fill(-1.0F);
    high.fill(1.0F);
    for (int ruleIndex = 0; ruleIndex < result->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = result->rules[static_cast<size_t>(ruleIndex)];
        if (!rule.enabled || result->ruleHealth[static_cast<size_t>(ruleIndex)] == AutomationHealth::Invalid) continue;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            if (action.type != AutomationActionType::AxisClamp) continue;
            low[static_cast<size_t>(action.target)] = std::max(low[static_cast<size_t>(action.target)], action.minimum);
            high[static_cast<size_t>(action.target)] = std::min(high[static_cast<size_t>(action.target)], action.maximum);
        }
    }
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (low[static_cast<size_t>(axis)] <= high[static_cast<size_t>(axis)]) continue;
        result->publishable = false;
        result->health = AutomationHealth::Invalid;
        result->message = u"Active Automation clamps have no common range."_qs;
        break;
    }
    return result;
}

void AutomationRuntime::reset()
{
    m_conditionStates = {};
    m_conditionLatches = {};
    m_ruleStates = {};
    m_previousRuleActive.fill(false);
    m_toggledButtons.fill(false);
    m_tapExpiry = {};
    m_controlConditionStates = {};
    m_controlConditionLatches = {};
    m_controlPreviousRuleActive.fill(false);
    m_result = {};
}

void AutomationRuntime::setCompiled(const CompiledAutomationSet *compiled)
{
    if (m_compiled == compiled) return;
    m_compiled = compiled;
    reset();
}

bool AutomationRuntime::conditionMatches(const CompiledAutomationCondition &condition,
                                         const AutomationInputSnapshot &input,
                                         bool &latch) const
{
    switch (condition.type) {
    case AutomationConditionType::Always:
        return true;
    case AutomationConditionType::AxisAbove: {
        if (!validAxis(condition.source)
            || !input.axisAvailable[static_cast<size_t>(condition.source)]) return latch = false;
        const float value = input.physicalAxes[static_cast<size_t>(condition.source)];
        latch = latch ? value >= condition.minimum - condition.hysteresis : value > condition.minimum;
        return latch;
    }
    case AutomationConditionType::AxisBelow: {
        if (!validAxis(condition.source)
            || !input.axisAvailable[static_cast<size_t>(condition.source)]) return latch = false;
        const float value = input.physicalAxes[static_cast<size_t>(condition.source)];
        latch = latch ? value <= condition.minimum + condition.hysteresis : value < condition.minimum;
        return latch;
    }
    case AutomationConditionType::AxisBetween: {
        if (!validAxis(condition.source)
            || !input.axisAvailable[static_cast<size_t>(condition.source)]) return latch = false;
        const float value = input.physicalAxes[static_cast<size_t>(condition.source)];
        latch = latch ? value >= condition.minimum - condition.hysteresis
                            && value <= condition.maximum + condition.hysteresis
                      : value >= condition.minimum && value <= condition.maximum;
        return latch;
    }
    case AutomationConditionType::AxisOutsideRange: {
        if (!validAxis(condition.source)
            || !input.axisAvailable[static_cast<size_t>(condition.source)]) return latch = false;
        const float value = input.physicalAxes[static_cast<size_t>(condition.source)];
        latch = latch ? value <= condition.minimum + condition.hysteresis
                            || value >= condition.maximum - condition.hysteresis
                      : value < condition.minimum || value > condition.maximum;
        return latch;
    }
    case AutomationConditionType::ButtonHeld:
        return condition.source >= 0 && condition.source < input.buttonCount
            && input.buttons[static_cast<size_t>(condition.source)];
    case AutomationConditionType::ButtonReleased:
        return condition.source >= 0 && condition.source < input.buttonCount
            && !input.buttons[static_cast<size_t>(condition.source)];
    case AutomationConditionType::ButtonPressed:
    case AutomationConditionType::ButtonReleaseEvent:
    case AutomationConditionType::ButtonMultiPress:
    case AutomationConditionType::ButtonLongPress:
    case AutomationConditionType::AxisCrossesAbove:
    case AutomationConditionType::AxisCrossesBelow:
        return false;
    case AutomationConditionType::PovActive:
    case AutomationConditionType::PovInactive: {
        const int hat = condition.source / kPovDirectionCount;
        const int direction = condition.source % kPovDirectionCount;
        const bool available = hat >= 0 && hat < input.povCount;
        if (!available) return false;
        const bool active = povDirectionIndex(
            povDirectionFromRaw(input.povs[static_cast<size_t>(hat)])) == direction;
        return condition.type == AutomationConditionType::PovActive ? active : !active;
    }
    case AutomationConditionType::BaseProfileIs:
        return input.baseProfileIndex == condition.source;
    case AutomationConditionType::EffectiveProfileIs:
        return input.preAutomationEffectiveProfileIndex == condition.source;
    }
    return false;
}

bool AutomationRuntime::eventConditionMatches(const CompiledAutomationCondition &condition,
                                              const AutomationInputSnapshot &input,
                                              ConditionRuntimeState &state)
{
    switch (condition.type) {
    case AutomationConditionType::ButtonPressed:
    case AutomationConditionType::ButtonReleaseEvent:
    case AutomationConditionType::ButtonMultiPress:
    case AutomationConditionType::ButtonLongPress: {
        if (condition.source < 0 || condition.source >= input.buttonCount) {
            state = {};
            return false;
        }
        const bool pressed = input.buttons[static_cast<size_t>(condition.source)];
        if (!state.initialized) {
            state.initialized = true;
            state.previousButtonState = pressed;
            if (pressed && condition.type == AutomationConditionType::ButtonLongPress) {
                state.holdStarted = input.timestamp;
            }
            return false;
        }
        const bool pressedEdge = !state.previousButtonState && pressed;
        const bool releasedEdge = state.previousButtonState && !pressed;
        state.previousButtonState = pressed;
        if (condition.type == AutomationConditionType::ButtonPressed) return pressedEdge;
        if (condition.type == AutomationConditionType::ButtonReleaseEvent) return releasedEdge;
        if (condition.type == AutomationConditionType::ButtonMultiPress) {
            if (!pressedEdge) return false;
            const auto window = std::chrono::milliseconds(condition.multiPressWindowMs);
            if (state.pressCount == 0 || input.timestamp - state.lastPress <= window) {
                ++state.pressCount;
            } else {
                state.pressCount = 1;
            }
            state.lastPress = input.timestamp;
            if (state.pressCount < condition.pressCount) return false;
            state.pressCount = 0;
            return true;
        }
        if (pressed && pressedEdge) {
            state.holdStarted = input.timestamp;
            state.longPressFired = false;
        } else if (!pressed) {
            state.holdStarted = {};
            state.longPressFired = false;
        }
        if (!pressed || state.longPressFired
            || input.timestamp - state.holdStarted
                < std::chrono::milliseconds(condition.longPressDurationMs)) {
            return false;
        }
        state.longPressFired = true;
        return true;
    }
    case AutomationConditionType::AxisCrossesAbove:
    case AutomationConditionType::AxisCrossesBelow: {
        if (!validAxis(condition.source)
            || !input.axisAvailable[static_cast<size_t>(condition.source)]) {
            state = {};
            return false;
        }
        const float value = input.physicalAxes[static_cast<size_t>(condition.source)];
        if (!state.initialized) {
            state.initialized = true;
            state.thresholdLatched = condition.type == AutomationConditionType::AxisCrossesAbove
                ? value > condition.minimum : value < condition.minimum;
            return false;
        }
        if (condition.type == AutomationConditionType::AxisCrossesAbove) {
            if (state.thresholdLatched) {
                if (value <= condition.minimum - condition.hysteresis) state.thresholdLatched = false;
                return false;
            }
            if (value > condition.minimum) {
                state.thresholdLatched = true;
                return true;
            }
            return false;
        }
        if (state.thresholdLatched) {
            if (value >= condition.minimum + condition.hysteresis) state.thresholdLatched = false;
            return false;
        }
        if (value < condition.minimum) {
            state.thresholdLatched = true;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

const AutomationEvaluationResult &AutomationRuntime::evaluateLevelOnly(
    const AutomationInputSnapshot &input)
{
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        const bool valid = m_compiled->ruleHealth[static_cast<size_t>(ruleIndex)]
            != AutomationHealth::Invalid;
        bool matched = rule.enabled && valid;
        if (matched) {
            matched = rule.matchMode == AutomationMatchMode::All;
            for (int conditionIndex = 0; conditionIndex < rule.conditionCount; ++conditionIndex) {
                bool &latch = m_conditionLatches[static_cast<size_t>(ruleIndex)]
                    [static_cast<size_t>(conditionIndex)];
                const bool condition = conditionMatches(
                    rule.conditions[static_cast<size_t>(conditionIndex)], input, latch);
                if (rule.matchMode == AutomationMatchMode::All) matched = matched && condition;
                else matched = matched || condition;
            }
        }
        const bool rising = matched && !m_previousRuleActive[static_cast<size_t>(ruleIndex)];
        m_previousRuleActive[static_cast<size_t>(ruleIndex)] = matched;
        m_result.activeRules[static_cast<size_t>(ruleIndex)] = matched;
        if (matched) ++m_result.activeRuleCount;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            switch (action.type) {
            case AutomationActionType::VJoyButtonHold:
                if (matched) m_result.heldButtons[static_cast<size_t>(action.target)] = true;
                break;
            case AutomationActionType::VJoyButtonToggle:
                if (rising) m_toggledButtons[static_cast<size_t>(action.target)] =
                    !m_toggledButtons[static_cast<size_t>(action.target)];
                break;
            case AutomationActionType::ProfileHold:
            case AutomationActionType::ProfileToggle: {
                AutomationProfileContribution &contribution = m_result.profileContributions[
                    static_cast<size_t>(m_result.profileContributionCount++)];
                contribution.source = ruleIndex * kMaximumAutomationActions + actionIndex;
                contribution.ruleIndex = ruleIndex;
                contribution.targetProfileIndex = action.profileIndex;
                contribution.mode = action.type == AutomationActionType::ProfileHold
                    ? ProfileTriggerMode::Hold : ProfileTriggerMode::Toggle;
                contribution.active = matched;
                contribution.rising = rising;
                break;
            }
            case AutomationActionType::VJoyButtonTap:
                break;
            case AutomationActionType::MappingOn:
            case AutomationActionType::MappingOff:
            case AutomationActionType::ToggleMapping:
                if (rising) m_result.mappingControlAction =
                    mappingControlForAutomationAction(action.type);
                break;
            default:
                break;
            }
        }
    }
    composeAdaptiveResponseOverlays();
    m_result.toggledButtons = m_toggledButtons;
    return m_result;
}

const AutomationEvaluationResult &AutomationRuntime::evaluateMappingControls(
    const AutomationInputSnapshot &input)
{
    m_result = {};
    if (!m_compiled || !m_compiled->engineEnabled || !m_compiled->publishable) {
        return m_result;
    }
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        const bool valid = m_compiled->ruleHealth[static_cast<size_t>(ruleIndex)]
            != AutomationHealth::Invalid;
        bool matched = rule.enabled && valid;
        if (matched) {
            matched = rule.matchMode == AutomationMatchMode::All;
            for (int conditionIndex = 0; conditionIndex < rule.conditionCount; ++conditionIndex) {
                const CompiledAutomationCondition &condition = rule.conditions[
                    static_cast<size_t>(conditionIndex)];
                const bool event = isEventCondition(condition.type);
                const bool matchedCondition = event
                    ? eventConditionMatches(condition, input, m_controlConditionStates[
                        static_cast<size_t>(ruleIndex)][static_cast<size_t>(conditionIndex)])
                    : conditionMatches(condition, input, m_controlConditionLatches[
                        static_cast<size_t>(ruleIndex)][static_cast<size_t>(conditionIndex)]);
                if (rule.matchMode == AutomationMatchMode::All) matched = matched && matchedCondition;
                else matched = matched || matchedCondition;
            }
        }
        const bool rising = matched && !m_controlPreviousRuleActive[static_cast<size_t>(ruleIndex)];
        m_controlPreviousRuleActive[static_cast<size_t>(ruleIndex)] = matched;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            if (rising && (action.type == AutomationActionType::MappingOn
                           || action.type == AutomationActionType::MappingOff
                           || action.type == AutomationActionType::ToggleMapping)) {
                m_result.mappingControlAction = mappingControlForAutomationAction(action.type);
            }
        }
    }
    return m_result;
}

const AutomationEvaluationResult &AutomationRuntime::evaluate(const AutomationInputSnapshot &input)
{
    m_result = {};
    if (!m_compiled || !m_compiled->engineEnabled || !m_compiled->publishable) {
        reset();
        return m_result;
    }
    if (!m_compiled->hasEventConditions && !m_compiled->hasTimedActions
        && !m_compiled->hasLatchedRules) {
        return evaluateLevelOnly(input);
    }
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        const bool valid = m_compiled->ruleHealth[static_cast<size_t>(ruleIndex)] != AutomationHealth::Invalid;
        bool matched = rule.enabled && valid;
        if (matched) {
            matched = rule.matchMode == AutomationMatchMode::All;
            for (int conditionIndex = 0; conditionIndex < rule.conditionCount; ++conditionIndex) {
                const CompiledAutomationCondition &conditionRecord = rule.conditions[
                    static_cast<size_t>(conditionIndex)];
                const bool event = m_compiled->hasEventConditions
                    && isEventCondition(conditionRecord.type);
                const bool condition = event
                    ? eventConditionMatches(conditionRecord, input,
                        m_conditionStates[static_cast<size_t>(ruleIndex)]
                            [static_cast<size_t>(conditionIndex)])
                    : conditionMatches(conditionRecord, input,
                        m_conditionLatches[static_cast<size_t>(ruleIndex)]
                            [static_cast<size_t>(conditionIndex)]);
                if (rule.matchMode == AutomationMatchMode::All) matched = matched && condition;
                else matched = matched || condition;
            }
        }
        // For multiple event conditions, ALL means that every event must be
        // observed in this one physical report; no hidden event window exists.
        const bool triggered = matched && !m_previousRuleActive[static_cast<size_t>(ruleIndex)];
        m_previousRuleActive[static_cast<size_t>(ruleIndex)] = matched;
        bool active = matched;
        if (rule.activationMode == AutomationActivationMode::ToggleOnTrigger) {
            RuleRuntimeState &ruleState = m_ruleStates[static_cast<size_t>(ruleIndex)];
            if (triggered) ruleState.toggledActive = !ruleState.toggledActive;
            active = ruleState.toggledActive;
        } else if (rule.activationMode == AutomationActivationMode::RunBriefly) {
            RuleRuntimeState &ruleState = m_ruleStates[static_cast<size_t>(ruleIndex)];
            if (triggered) {
                ruleState.activeUntil = input.timestamp
                    + std::chrono::milliseconds(rule.activeDurationMs);
            }
            active = input.timestamp < ruleState.activeUntil;
        }
        m_result.activeRules[static_cast<size_t>(ruleIndex)] = active;
        if (active) ++m_result.activeRuleCount;
        const bool startTap = triggered
            && (rule.activationMode != AutomationActivationMode::ToggleOnTrigger || active);
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            switch (action.type) {
            case AutomationActionType::VJoyButtonHold:
                if (active) m_result.heldButtons[static_cast<size_t>(action.target)] = true;
                break;
            case AutomationActionType::VJoyButtonToggle:
                if (triggered) m_toggledButtons[static_cast<size_t>(action.target)] =
                    !m_toggledButtons[static_cast<size_t>(action.target)];
                break;
            case AutomationActionType::VJoyButtonTap: {
                std::chrono::steady_clock::time_point &expiry = m_tapExpiry[
                    static_cast<size_t>(ruleIndex)][static_cast<size_t>(actionIndex)];
                if (startTap) expiry = input.timestamp + std::chrono::milliseconds(action.tapDurationMs);
                if (input.timestamp < expiry) {
                    m_result.pulsedButtons[static_cast<size_t>(action.target)] = true;
                }
                break;
            }
            case AutomationActionType::ProfileHold:
            case AutomationActionType::ProfileToggle: {
                AutomationProfileContribution &contribution = m_result.profileContributions[
                    static_cast<size_t>(m_result.profileContributionCount++)];
                contribution.source = ruleIndex * kMaximumAutomationActions + actionIndex;
                contribution.ruleIndex = ruleIndex;
                contribution.targetProfileIndex = action.profileIndex;
                contribution.mode = action.type == AutomationActionType::ProfileHold
                    ? ProfileTriggerMode::Hold : ProfileTriggerMode::Toggle;
                contribution.active = active;
                contribution.rising = triggered;
                break;
            }
            case AutomationActionType::MappingOn:
            case AutomationActionType::MappingOff:
            case AutomationActionType::ToggleMapping:
                if (triggered) m_result.mappingControlAction =
                    mappingControlForAutomationAction(action.type);
                break;
            default:
                break;
            }
        }
    }
    composeAdaptiveResponseOverlays();
    m_result.toggledButtons = m_toggledButtons;
    return m_result;
}

void AutomationRuntime::composeAdaptiveResponseOverlays()
{
    // Adaptive Response setters compose independently by property. A high
    // priority Enable action therefore cannot erase a lower priority preset's
    // horizon/lead/motion values unless it explicitly sets those properties.
    std::array<std::array<int, 13>, kPhysicalAxisCount> winnerPriority{};
    std::array<std::array<int, 13>, kPhysicalAxisCount> winnerOrder{};
    for (auto &axis : winnerPriority) axis.fill(-1);
    for (auto &axis : winnerOrder) axis.fill(kMaximumAutomationRules + 1);
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        if (!m_result.activeRules[static_cast<size_t>(ruleIndex)]) continue;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            if (!action.adaptiveResponse.active || !validAxis(action.target)) continue;
            const size_t axis = static_cast<size_t>(action.target);
            RuntimeAdaptiveResponseOverride &overlay = m_result.adaptiveResponseOverlays[axis];
            const std::uint32_t properties = action.adaptiveResponse.properties
                & kAdaptiveResponseAllProperties;
            for (int bit = 0; bit < 13; ++bit) {
                const std::uint32_t property = 1U << bit;
                if ((properties & property) == 0) continue;
                if (rule.priority < winnerPriority[axis][static_cast<size_t>(bit)]
                    || (rule.priority == winnerPriority[axis][static_cast<size_t>(bit)]
                        && rule.sourceOrder >= winnerOrder[axis][static_cast<size_t>(bit)])) {
                    continue;
                }
                winnerPriority[axis][static_cast<size_t>(bit)] = rule.priority;
                winnerOrder[axis][static_cast<size_t>(bit)] = rule.sourceOrder;
                copyAdaptiveResponseProperty(overlay.settings, action.adaptiveResponse.settings, property);
                overlay.properties |= property;
                overlay.active = true;
            }
        }
    }
}

void AutomationRuntime::applyAxisActions(const AutomationInputSnapshot &input,
                                         std::array<float, kPhysicalAxisCount> &processedAxes) const
{
    if (!m_compiled || !m_compiled->engineEnabled || !m_compiled->publishable) return;
    std::array<bool, kPhysicalAxisCount> hasOverride{};
    std::array<int, kPhysicalAxisCount> winnerPriority{};
    std::array<int, kPhysicalAxisCount> winnerOrder{};
    std::array<float, kPhysicalAxisCount> winnerValue{};
    winnerPriority.fill(-1);
    winnerOrder.fill(kMaximumAutomationRules + 1);
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        if (!m_result.activeRules[static_cast<size_t>(ruleIndex)]) continue;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            if (action.type != AutomationActionType::AxisOverride
                && action.type != AutomationActionType::AxisFollow) continue;
            if (action.type == AutomationActionType::AxisFollow
                && !input.axisAvailable[static_cast<size_t>(action.source)]) continue;
            float candidate = action.value;
            if (action.type == AutomationActionType::AxisFollow) {
                const float source = action.sourceStage == AutomationAxisSourceStage::Physical
                    ? input.physicalAxes[static_cast<size_t>(action.source)]
                    : processedAxes[static_cast<size_t>(action.source)];
                candidate = source * action.value + action.offset;
            }
            const size_t target = static_cast<size_t>(action.target);
            if (!hasOverride[target] || rule.priority > winnerPriority[target]
                || (rule.priority == winnerPriority[target] && rule.sourceOrder < winnerOrder[target])) {
                hasOverride[target] = true;
                winnerPriority[target] = rule.priority;
                winnerOrder[target] = rule.sourceOrder;
                winnerValue[target] = candidate;
            }
        }
    }
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        if (hasOverride[static_cast<size_t>(axis)]) {
            processedAxes[static_cast<size_t>(axis)] = winnerValue[static_cast<size_t>(axis)];
        }
    }
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        if (!m_result.activeRules[static_cast<size_t>(ruleIndex)]) continue;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            if (action.type == AutomationActionType::AxisScale) {
                processedAxes[static_cast<size_t>(action.target)] *= action.value;
            }
        }
    }
    std::array<float, kPhysicalAxisCount> mix{};
    std::array<float, kPhysicalAxisCount> offset{};
    std::array<float, kPhysicalAxisCount> clampMinimum{};
    std::array<float, kPhysicalAxisCount> clampMaximum{};
    clampMinimum.fill(-1.0F);
    clampMaximum.fill(1.0F);
    for (int ruleIndex = 0; ruleIndex < m_compiled->ruleCount; ++ruleIndex) {
        const CompiledAutomationRule &rule = m_compiled->rules[static_cast<size_t>(ruleIndex)];
        if (!m_result.activeRules[static_cast<size_t>(ruleIndex)]) continue;
        for (int actionIndex = 0; actionIndex < rule.actionCount; ++actionIndex) {
            const CompiledAutomationAction &action = rule.actions[static_cast<size_t>(actionIndex)];
            const size_t target = static_cast<size_t>(action.target);
            if (action.type == AutomationActionType::AxisMix) {
                if (!input.axisAvailable[static_cast<size_t>(action.source)]) continue;
                const float source = action.sourceStage == AutomationAxisSourceStage::Physical
                    ? input.physicalAxes[static_cast<size_t>(action.source)]
                    : processedAxes[static_cast<size_t>(action.source)];
                mix[target] += source * action.value;
            } else if (action.type == AutomationActionType::AxisOffset) {
                offset[target] += action.value;
            } else if (action.type == AutomationActionType::AxisClamp) {
                clampMinimum[target] = std::max(clampMinimum[target], action.minimum);
                clampMaximum[target] = std::min(clampMaximum[target], action.maximum);
            }
        }
    }
    for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
        const size_t index = static_cast<size_t>(axis);
        processedAxes[index] += mix[index];
        processedAxes[index] += offset[index];
        processedAxes[index] = std::clamp(processedAxes[index], clampMinimum[index], clampMaximum[index]);
        processedAxes[index] = std::clamp(processedAxes[index], -1.0F, 1.0F);
    }
}

} // namespace hotas
