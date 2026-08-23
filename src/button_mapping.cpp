#include "button_mapping.h"

#include <algorithm>

namespace hotas {
namespace {

int boundedCount(int value, int maximum)
{
    return std::clamp(value, 0, maximum);
}

void disable(ButtonBinding &binding)
{
    binding.type = ButtonActionType::Disabled;
    binding.target = 0;
}

} // namespace

ButtonCapacityStatus assessButtonCapacity(int physicalButtonCount, int vjoyButtonCapacity)
{
    ButtonCapacityStatus status;
    status.physicalButtons = boundedCount(physicalButtonCount, kMaximumPhysicalButtons);
    status.virtualButtons = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    status.sufficient = status.physicalButtons == 0
        || status.virtualButtons >= status.physicalButtons;
    status.recommended = status.virtualButtons >= 32;
    return status;
}

ButtonBindings defaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity)
{
    const int physicalCount = boundedCount(physicalButtonCount, kMaximumPhysicalButtons);
    const int virtualCount = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    ButtonBindings bindings(static_cast<size_t>(physicalCount));
    for (int index = 0; index < std::min(physicalCount, virtualCount); ++index) {
        bindings[static_cast<size_t>(index)] = {ButtonActionType::VirtualButton, index + 1};
    }
    return bindings;
}

bool needsDefaultButtonMappings(const ButtonBindings &bindings, int physicalButtonCount,
                                int vjoyButtonCapacity)
{
    const int physicalCount = boundedCount(physicalButtonCount, kMaximumPhysicalButtons);
    const int virtualCount = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    for (int source = 0; source < physicalCount; ++source) {
        if (source >= static_cast<int>(bindings.size())) return true;
        const ButtonBinding &binding = bindings[static_cast<size_t>(source)];
        if (binding.explicitlyConfigured) continue;
        const ButtonBinding expected = source < virtualCount
            ? ButtonBinding{ButtonActionType::VirtualButton, source + 1}
            : ButtonBinding{};
        if (binding.type != expected.type || binding.target != expected.target) return true;
    }
    return false;
}

bool ensureDefaultButtonMappings(ButtonBindings &bindings, int physicalButtonCount,
                                 int vjoyButtonCapacity)
{
    const int physicalCount = boundedCount(physicalButtonCount, kMaximumPhysicalButtons);
    const int virtualCount = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    bool changed = false;
    if (bindings.size() < static_cast<size_t>(physicalCount)) {
        bindings.resize(static_cast<size_t>(physicalCount));
        changed = true;
    }
    for (int source = 0; source < physicalCount; ++source) {
        ButtonBinding &binding = bindings[static_cast<size_t>(source)];
        if (binding.explicitlyConfigured) continue;
        const ButtonBinding expected = source < virtualCount
            ? ButtonBinding{ButtonActionType::VirtualButton, source + 1}
            : ButtonBinding{};
        if (binding.type != expected.type || binding.target != expected.target) {
            binding = expected;
            changed = true;
        }
    }
    return changed;
}

bool isButtonBindingValid(const ButtonBinding &binding, int vjoyButtonCapacity)
{
    const int capacity = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    if (binding.type == ButtonActionType::Disabled) {
        return binding.target == 0;
    }
    return binding.type == ButtonActionType::VirtualButton
        && binding.target >= 1 && binding.target <= capacity;
}

bool normalizeButtonMappings(ButtonBindings &bindings, int vjoyButtonCapacity)
{
    const int capacity = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    std::array<bool, kMaximumVirtualButtons + 1> occupied{};
    bool clean = true;
    for (ButtonBinding &binding : bindings) {
        if (!isButtonBindingValid(binding, capacity)) {
            clean = false;
            disable(binding);
            continue;
        }
        if (binding.type == ButtonActionType::Disabled) {
            continue;
        }
        if (occupied[static_cast<size_t>(binding.target)]) {
            clean = false;
            disable(binding);
            continue;
        }
        occupied[static_cast<size_t>(binding.target)] = true;
    }
    return clean;
}

bool hasButtonMappingConflict(const ButtonBindings &bindings, int sourceIndex,
                              int candidateVirtualButton, int vjoyButtonCapacity)
{
    if (candidateVirtualButton == 0) {
        return false;
    }
    if (candidateVirtualButton < 1 || candidateVirtualButton > boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons)) {
        return true;
    }
    for (int index = 0; index < static_cast<int>(bindings.size()); ++index) {
        if (index != sourceIndex && bindings[static_cast<size_t>(index)].type == ButtonActionType::VirtualButton
            && bindings[static_cast<size_t>(index)].target == candidateVirtualButton) {
            return true;
        }
    }
    return false;
}

RuntimeButtonTargets buildRuntimeButtonTargets(const ButtonBindings &bindings,
                                               int vjoyButtonCapacity)
{
    RuntimeButtonTargets targets{};
    std::array<bool, kMaximumVirtualButtons + 1> occupied{};
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalButtons);
    for (int index = 0; index < count; ++index) {
        const ButtonBinding &binding = bindings[static_cast<size_t>(index)];
        if (isButtonBindingValid(binding, vjoyButtonCapacity)
            && binding.type == ButtonActionType::VirtualButton
            && !occupied[static_cast<size_t>(binding.target)]) {
            targets[static_cast<size_t>(index)] = binding.target;
            occupied[static_cast<size_t>(binding.target)] = true;
        }
    }
    return targets;
}

RuntimeButtonTargets buildRuntimeButtonTargets(
    const ButtonBindings &bindings, int vjoyButtonCapacity,
    const std::array<RuntimeProfileTrigger, kMaximumPhysicalButtons> &profileTriggers)
{
    RuntimeButtonTargets targets = buildRuntimeButtonTargets(bindings, vjoyButtonCapacity);
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (profileTriggers[static_cast<size_t>(source)].consumesButton) {
            targets[static_cast<size_t>(source)] = 0;
        }
    }
    return targets;
}

VirtualButtonStates mapButtonStates(const PhysicalButtonStates &physical,
                                    const RuntimeButtonTargets &targets,
                                    int vjoyButtonCapacity)
{
    VirtualButtonStates virtualStates{};
    const int capacity = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        const int target = targets[static_cast<size_t>(source)];
        if (target >= 1 && target <= capacity) {
            virtualStates[static_cast<size_t>(target)] = physical[static_cast<size_t>(source)];
        }
    }
    return virtualStates;
}

} // namespace hotas
