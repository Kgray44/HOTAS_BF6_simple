#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

using PhysicalButtonStates = std::array<bool, kMaximumPhysicalButtons>;
using VirtualButtonStates = std::array<bool, kMaximumVirtualButtons + 1>;
using RuntimeButtonTargets = std::array<int, kMaximumPhysicalButtons>;

// Returns a passthrough map that is limited by both reported device counts.
ButtonBindings defaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity);

bool isButtonBindingValid(const ButtonBinding &binding, int vjoyButtonCapacity);
// Removes malformed, out-of-capacity, and duplicate destinations. The first
// source retains a destination so a release can never be ambiguous.
bool normalizeButtonMappings(ButtonBindings &bindings, int vjoyButtonCapacity);
bool hasButtonMappingConflict(const ButtonBindings &bindings, int sourceIndex,
                              int candidateVirtualButton, int vjoyButtonCapacity);

// Converts persisted bindings to a compact, allocation-free hot-path table.
RuntimeButtonTargets buildRuntimeButtonTargets(const ButtonBindings &bindings,
                                               int vjoyButtonCapacity);
VirtualButtonStates mapButtonStates(const PhysicalButtonStates &physical,
                                    const RuntimeButtonTargets &targets,
                                    int vjoyButtonCapacity);

} // namespace hotas
