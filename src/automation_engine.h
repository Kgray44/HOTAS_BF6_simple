#pragma once

#include "button_mapping.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace hotas {

// The compiled records deliberately contain no QStrings, QVariant, or
// configuration pointers. A report only touches these fixed arrays.
struct CompiledAutomationCondition {
    AutomationConditionType type = AutomationConditionType::Always;
    int source = -1;
    float minimum = 0.0F;
    float maximum = 0.0F;
    float hysteresis = 0.0F;
    int pressCount = 2;
    int multiPressWindowMs = 350;
    int longPressDurationMs = 600;
};

struct CompiledAutomationAction {
    AutomationActionType type = AutomationActionType::VJoyButtonHold;
    int target = -1;
    int source = -1;
    int profileIndex = -1;
    AutomationAxisSourceStage sourceStage = AutomationAxisSourceStage::Processed;
    float value = 0.0F;
    float offset = 0.0F;
    float minimum = -1.0F;
    float maximum = 1.0F;
    int tapDurationMs = 80;
};

struct CompiledAutomationRule {
    bool enabled = false;
    AutomationMatchMode matchMode = AutomationMatchMode::All;
    AutomationActivationMode activationMode = AutomationActivationMode::WhileTriggerActive;
    int activeDurationMs = 250;
    int priority = 50;
    int sourceOrder = 0; // Persisted vector order is the equal-priority tie-break.
    int conditionCount = 0;
    int actionCount = 0;
    std::array<CompiledAutomationCondition, kMaximumAutomationConditions> conditions{};
    std::array<CompiledAutomationAction, kMaximumAutomationActions> actions{};
};

struct CompiledAutomationSet {
    bool engineEnabled = true;
    bool publishable = true;
    // These flags keep the legacy level-only path lean: no temporal state or
    // duration checks are touched unless a compiled rule actually needs them.
    bool hasEventConditions = false;
    bool hasTemporalConditions = false;
    bool hasTimedActions = false;
    bool hasLatchedRules = false;
    int ruleCount = 0;
    AutomationHealth health = AutomationHealth::Valid;
    std::array<CompiledAutomationRule, kMaximumAutomationRules> rules{};
    // Diagnostics only. These strings are created at compile time and are
    // never inspected by the worker's report loop.
    std::array<AutomationHealth, kMaximumAutomationRules> ruleHealth{};
    std::array<QString, kMaximumAutomationRules> ruleMessages{};
    std::array<QString, kMaximumAutomationRules> ruleNames{};
    QString message;
};

struct AutomationInputSnapshot {
    std::array<float, kPhysicalAxisCount> physicalAxes{};
    std::array<bool, kPhysicalAxisCount> axisAvailable{};
    PhysicalButtonStates buttons{};
    PhysicalPovValues povs{};
    int povCount = 0;
    int buttonCount = 0;
    int baseProfileIndex = 0;
    int preAutomationEffectiveProfileIndex = 0;
    // The worker obtains this once per physical report and shares it across
    // every condition and action in this evaluation.
    std::chrono::steady_clock::time_point timestamp{};
};

struct AutomationProfileContribution {
    int source = -1;
    int ruleIndex = -1;
    int targetProfileIndex = -1;
    ProfileTriggerMode mode = ProfileTriggerMode::Disabled;
    bool active = false;
    bool rising = false;
};

struct AutomationEvaluationResult {
    std::array<bool, kMaximumAutomationRules> activeRules{};
    std::array<bool, kMaximumVirtualButtons + 1> heldButtons{};
    std::array<bool, kMaximumVirtualButtons + 1> toggledButtons{};
    std::array<bool, kMaximumVirtualButtons + 1> pulsedButtons{};
    std::array<AutomationProfileContribution, kMaximumAutomationProfileContributors>
        profileContributions{};
    int profileContributionCount = 0;
    int activeRuleCount = 0;
    // Control-plane actions are emitted only on a rule activation edge. They
    // deliberately have no vJoy output representation, so the worker can
    // keep them alive while ordinary mapping output is suppressed.
    MappingControlAction mappingControlAction = MappingControlAction::None;
};

// This compiler runs only at a configuration boundary. Invalid individual
// rules are retained for repair but omitted from the published runtime table.
std::shared_ptr<const CompiledAutomationSet> compileAutomationSet(
    const MapperConfiguration &configuration, const RuntimeProfileCache &cache);

class AutomationRuntime final {
public:
    void reset();
    void setCompiled(const CompiledAutomationSet *compiled);
    const AutomationEvaluationResult &evaluate(const AutomationInputSnapshot &input);
    // Evaluates only Mapping On/Off/Toggle actions. This maintains independent
    // edge state so ordinary vJoy/profile/axis Automation effects cannot
    // latch or publish while mapping is explicitly off.
    const AutomationEvaluationResult &evaluateMappingControls(const AutomationInputSnapshot &input);

    // Implements the documented deterministic action order:
    // Follow/Override, Scale, Mix, Offset, Clamp, final legal-domain clamp.
    void applyAxisActions(const AutomationInputSnapshot &input,
                          std::array<float, kPhysicalAxisCount> &processedAxes) const;

private:
    struct ConditionRuntimeState {
        bool initialized = false;
        bool previousButtonState = false;
        bool thresholdLatched = false;
        bool longPressFired = false;
        int pressCount = 0;
        std::chrono::steady_clock::time_point lastPress{};
        std::chrono::steady_clock::time_point holdStarted{};
    };

    struct RuleRuntimeState {
        bool toggledActive = false;
        std::chrono::steady_clock::time_point activeUntil{};
    };

    bool conditionMatches(const CompiledAutomationCondition &condition,
                          const AutomationInputSnapshot &input,
                          bool &latch) const;
    bool eventConditionMatches(const CompiledAutomationCondition &condition,
                               const AutomationInputSnapshot &input,
                               ConditionRuntimeState &state);
    const AutomationEvaluationResult &evaluateLevelOnly(const AutomationInputSnapshot &input);

    const CompiledAutomationSet *m_compiled = nullptr;
    std::array<std::array<ConditionRuntimeState, kMaximumAutomationConditions>,
               kMaximumAutomationRules> m_conditionStates{};
    std::array<std::array<bool, kMaximumAutomationConditions>, kMaximumAutomationRules>
        m_conditionLatches{};
    std::array<RuleRuntimeState, kMaximumAutomationRules> m_ruleStates{};
    std::array<bool, kMaximumAutomationRules> m_previousRuleActive{};
    std::array<bool, kMaximumVirtualButtons + 1> m_toggledButtons{};
    std::array<std::array<std::chrono::steady_clock::time_point,
                          kMaximumAutomationActions>, kMaximumAutomationRules> m_tapExpiry{};
    std::array<std::array<ConditionRuntimeState, kMaximumAutomationConditions>,
               kMaximumAutomationRules> m_controlConditionStates{};
    std::array<std::array<bool, kMaximumAutomationConditions>, kMaximumAutomationRules>
        m_controlConditionLatches{};
    std::array<bool, kMaximumAutomationRules> m_controlPreviousRuleActive{};
    AutomationEvaluationResult m_result{};
};

} // namespace hotas
