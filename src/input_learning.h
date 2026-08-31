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

} // namespace hotas
