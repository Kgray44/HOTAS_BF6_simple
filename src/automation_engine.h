#pragma once

#include "button_mapping.h"

#include <array>
#include <cstddef>
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
};

struct CompiledAutomationRule {
    bool enabled = false;
    AutomationMatchMode matchMode = AutomationMatchMode::All;
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
    std::array<AutomationProfileContribution, kMaximumAutomationProfileContributors>
        profileContributions{};
    int profileContributionCount = 0;
    int activeRuleCount = 0;
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

    // Implements the documented deterministic action order:
    // Follow/Override, Scale, Mix, Offset, Clamp, final legal-domain clamp.
    void applyAxisActions(const AutomationInputSnapshot &input,
                          std::array<float, kPhysicalAxisCount> &processedAxes) const;

private:
    bool conditionMatches(const CompiledAutomationCondition &condition,
                          const AutomationInputSnapshot &input,
                          bool &latch) const;

    const CompiledAutomationSet *m_compiled = nullptr;
    std::array<std::array<bool, kMaximumAutomationConditions>, kMaximumAutomationRules>
        m_conditionLatches{};
    std::array<bool, kMaximumAutomationRules> m_previousRuleActive{};
    std::array<bool, kMaximumVirtualButtons + 1> m_toggledButtons{};
    AutomationEvaluationResult m_result{};
};

} // namespace hotas
