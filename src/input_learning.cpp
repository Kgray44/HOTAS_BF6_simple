#include "input_learning.h"

#include <algorithm>
#include <cmath>

namespace hotas {
namespace {
constexpr float kDeliberateAxisMovement = 0.16F;
constexpr float kAmbiguousAxisMovement = 0.12F;
constexpr float kDominanceRatio = 0.75F;
}

AxisLearningSelection selectLearnedAxis(
    const std::array<float, kPhysicalAxisCount> &baseline,
    const std::array<float, kPhysicalAxisCount> &current,
    const std::array<bool, kPhysicalAxisCount> &available,
    const std::array<PhysicalAxisActivity, kPhysicalAxisCount> &activity)
{
    AxisLearningSelection best;
    float runnerUp = 0.0F;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!available[static_cast<size_t>(index)]
            || activity[static_cast<size_t>(index)] == PhysicalAxisActivity::Fixed) {
            continue;
        }
        const float movement = std::abs(current[static_cast<size_t>(index)]
            - baseline[static_cast<size_t>(index)]);
        if (movement > best.movement) {
            runnerUp = best.movement;
            best.axis = index;
            best.movement = movement;
        } else {
            runnerUp = std::max(runnerUp, movement);
        }
    }
    if (best.movement < kDeliberateAxisMovement) return best;
    if (runnerUp >= kAmbiguousAxisMovement && runnerUp >= best.movement * kDominanceRatio) {
        best.result = AxisLearningResult::Ambiguous;
        best.axis = -1;
        return best;
    }
    best.result = AxisLearningResult::Candidate;
    return best;
}

int selectLearnedButton(
    const std::array<bool, kMaximumPhysicalButtons> &baseline,
    const std::array<bool, kMaximumPhysicalButtons> &current,
    const std::array<bool, kMaximumPhysicalButtons> &available)
{
    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        const size_t slot = static_cast<size_t>(index);
        if (available[slot] && !baseline[slot] && current[slot]) return index + 1;
    }
    return 0;
}

SignalFlowInputSelection selectSignalFlowInput(
    const std::array<float, kPhysicalAxisCount> &axisBaseline,
    const std::array<float, kPhysicalAxisCount> &axisCurrent,
    const std::array<bool, kPhysicalAxisCount> &axisAvailable,
    const std::array<PhysicalAxisActivity, kPhysicalAxisCount> &axisActivity,
    const std::array<bool, kMaximumPhysicalButtons> &buttonBaseline,
    const std::array<bool, kMaximumPhysicalButtons> &buttonCurrent,
    const std::array<bool, kMaximumPhysicalButtons> &buttonAvailable,
    const std::array<int, kMaximumPhysicalPovs> &povBaseline,
    const std::array<int, kMaximumPhysicalPovs> &povCurrent,
    int availablePovCount)
{
    SignalFlowInputSelection selection;
    int candidates = 0;

    const AxisLearningSelection axis = selectLearnedAxis(axisBaseline, axisCurrent,
                                                           axisAvailable, axisActivity);
    if (axis.result == AxisLearningResult::Ambiguous) {
        selection.result = SignalFlowInputSelectionResult::Ambiguous;
        return selection;
    }
    if (axis.result == AxisLearningResult::Candidate) {
        ++candidates;
        selection.kind = SignalFlowInputSourceKind::Axis;
        selection.index = axis.axis;
    }

    for (int index = 0; index < kMaximumPhysicalButtons; ++index) {
        const size_t slot = static_cast<size_t>(index);
        if (!buttonAvailable[slot] || buttonBaseline[slot] || !buttonCurrent[slot]) continue;
        ++candidates;
        selection.kind = SignalFlowInputSourceKind::Button;
        selection.index = index;
        selection.subIndex = -1;
    }

    const int povLimit = std::clamp(availablePovCount, 0, kMaximumPhysicalPovs);
    for (int index = 0; index < povLimit; ++index) {
        const size_t slot = static_cast<size_t>(index);
        const PovDirection direction = povDirectionFromRaw(povCurrent[slot]);
        if (direction == PovDirection::Centered || povCurrent[slot] == povBaseline[slot]) continue;
        ++candidates;
        selection.kind = SignalFlowInputSourceKind::Pov;
        selection.index = index;
        selection.subIndex = povDirectionIndex(direction);
    }

    if (candidates == 0) return selection;
    if (candidates > 1) {
        selection.result = SignalFlowInputSelectionResult::Ambiguous;
        selection.kind = SignalFlowInputSourceKind::None;
        selection.index = -1;
        selection.subIndex = -1;
        return selection;
    }
    selection.result = SignalFlowInputSelectionResult::Candidate;
    return selection;
}

} // namespace hotas
