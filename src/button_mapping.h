#pragma once

#include "mapping_types.h"

#include <array>

namespace hotas {

using PhysicalButtonStates = std::array<bool, kMaximumPhysicalButtons>;
using VirtualButtonStates = std::array<bool, kMaximumVirtualButtons + 1>;
using RuntimeButtonTargets = std::array<int, kMaximumPhysicalButtons>;

struct ButtonCapacityStatus {
    int physicalButtons = 0;
    int virtualButtons = 0;
    bool sufficient = true;
    bool recommended = false;
};

// UI and configuration code use this compact capability result to make an
// undersized virtual device obvious without coupling the monitor to vJoy.
ButtonCapacityStatus assessButtonCapacity(int physicalButtonCount, int vjoyButtonCapacity);

// Returns a passthrough map that is limited by both reported device counts.
ButtonBindings defaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity);
// Completes only implicit sources with the physical-button-number equivalent.
// Explicit user routes and explicit Disabled choices are never replaced.
bool ensureDefaultButtonMappings(ButtonBindings &bindings, int physicalButtonCount,
                                 int vjoyButtonCapacity);
bool needsDefaultButtonMappings(const ButtonBindings &bindings, int physicalButtonCount,
                                int vjoyButtonCapacity);

bool isButtonBindingValid(const ButtonBinding &binding, int vjoyButtonCapacity);
// Removes malformed, out-of-capacity, and duplicate destinations. The first
// source retains a destination so a release can never be ambiguous.
bool normalizeButtonMappings(ButtonBindings &bindings, int vjoyButtonCapacity);
bool hasButtonMappingConflict(const ButtonBindings &bindings, int sourceIndex,
                              int candidateVirtualButton, int vjoyButtonCapacity);

// Converts persisted bindings to a compact, allocation-free hot-path table.
RuntimeButtonTargets buildRuntimeButtonTargets(const ButtonBindings &bindings,
                                               int vjoyButtonCapacity);
// A configured global profile control consumes its physical source while
// leaving the saved per-profile route untouched for later restoration.
RuntimeButtonTargets buildRuntimeButtonTargets(const ButtonBindings &bindings,
                                               int vjoyButtonCapacity,
                                               const std::array<RuntimeProfileTrigger,
                                                                kMaximumPhysicalButtons> &profileTriggers);
VirtualButtonStates mapButtonStates(const PhysicalButtonStates &physical,
                                    const RuntimeButtonTargets &targets,
                                    int vjoyButtonCapacity);

} // namespace hotas
