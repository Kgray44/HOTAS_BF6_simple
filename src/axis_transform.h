#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

float clampUnit(float value);
float normalizeCalibrated(float raw, const Calibration &calibration);
float applyRescaledDeadzone(float value, float deadzone);
float transformAxis(float raw, const RuntimeAxisMapping &mapping);

// Rejects duplicate non-disabled targets, retaining the first assignment.
// Returns true when the configuration was already conflict-free.
bool normalizeMappingConflicts(AxisMappings &mappings);
bool hasMappingConflict(const AxisMappings &mappings, int sourceIndex,
                        VirtualAxis candidateTarget);

} // namespace hotas
