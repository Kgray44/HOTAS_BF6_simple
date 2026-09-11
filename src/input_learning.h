#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

enum class AxisLearningResult {
    Waiting,
    Candidate,
    Ambiguous,
};

struct AxisLearningSelection {
    AxisLearningResult result = AxisLearningResult::Waiting;
    int axis = -1;
    float movement = 0.0F;
};

// This detector is deliberately used only by AppBackend's presentation timer.
// MappingWorker keeps publishing its existing bounded atomic snapshots and has
// no awareness of learning state.
AxisLearningSelection selectLearnedAxis(
    const std::array<float, kPhysicalAxisCount> &baseline,
    const std::array<float, kPhysicalAxisCount> &current,
    const std::array<bool, kPhysicalAxisCount> &available,
    const std::array<PhysicalAxisActivity, kPhysicalAxisCount> &activity);

// A button must transition from released at the learning baseline to pressed.
// Returning the first valid source preserves the existing one-button mapping
// behavior without adding report-loop work.
int selectLearnedButton(
    const std::array<bool, kMaximumPhysicalButtons> &baseline,
    const std::array<bool, kMaximumPhysicalButtons> &current,
    const std::array<bool, kMaximumPhysicalButtons> &available);

// Signal Flow learns a physical endpoint before the user chooses a virtual
// destination.  This selection remains entirely in the UI control plane:
// the MappingWorker keeps publishing its fixed atomic snapshots and does not
// receive a graph-learning branch.  `index` and `subIndex` use the same
// zero-based endpoint coordinates that SignalFlowRoute uses.
enum class SignalFlowInputSelectionResult {
    Waiting,
    Candidate,
    Ambiguous,
};

enum class SignalFlowInputSourceKind {
    None,
    Axis,
    Button,
    Pov,
};

struct SignalFlowInputSelection {
    SignalFlowInputSelectionResult result = SignalFlowInputSelectionResult::Waiting;
    SignalFlowInputSourceKind kind = SignalFlowInputSourceKind::None;
    int index = -1;
    int subIndex = -1;
};

// One deliberate axis movement, button press, or POV direction is required.
// Concurrent endpoint activity is explicitly ambiguous rather than letting a
// source-first learning gesture choose an arbitrary physical control.
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
    int availablePovCount);

} // namespace hotas
