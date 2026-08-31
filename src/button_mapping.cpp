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
    const QString customName = binding.customName;
    binding.type = ButtonActionType::Disabled;
    binding.target = 0;
    binding.explicitlyConfigured = true;
    binding.customName = customName;
}

void assign(ButtonBinding &binding, int target)
{
    const QString customName = binding.customName;
    binding = target > 0 ? ButtonBinding{ButtonActionType::VirtualButton, target} : ButtonBinding{};
    binding.explicitlyConfigured = true;
    binding.customName = customName;
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
    bool clean = true;
    for (ButtonBinding &binding : bindings) {
        if (!isButtonBindingValid(binding, capacity)) {
            clean = false;
            disable(binding);
        }
    }
    return clean;
}

ButtonRouteChange analyzeButtonRouteChange(const ButtonBindings &bindings, int sourceIndex,
                                           int candidateVirtualButton,
                                           int vjoyButtonCapacity)
{
    ButtonRouteChange change;
    const int capacity = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    if (sourceIndex < 0 || sourceIndex >= kMaximumPhysicalButtons
        || candidateVirtualButton < 0 || candidateVirtualButton > capacity) {
        return change;
    }
    change.valid = true;
    change.sourceIndex = sourceIndex;
    change.targetVirtualButton = candidateVirtualButton;
    if (sourceIndex < static_cast<int>(bindings.size())) {
        const ButtonBinding &source = bindings[static_cast<size_t>(sourceIndex)];
        if (source.type == ButtonActionType::VirtualButton) change.previousVirtualButton = source.target;
    }
    if (candidateVirtualButton == 0) return change;

    int displacedCount = 0;
    bool previousTargetOccupied = false;
    for (int index = 0; index < static_cast<int>(bindings.size()); ++index) {
        if (index == sourceIndex) continue;
        const ButtonBinding &binding = bindings[static_cast<size_t>(index)];
        if (binding.type != ButtonActionType::VirtualButton) continue;
        if (binding.target == candidateVirtualButton) {
            change.requiresResolution = true;
            change.displacedSourceIndex = index;
            ++displacedCount;
        }
        if (change.previousVirtualButton > 0 && binding.target == change.previousVirtualButton) {
            previousTargetOccupied = true;
        }
    }
    change.canSwap = change.requiresResolution && displacedCount == 1
        && change.previousVirtualButton > 0 && !previousTargetOccupied;
    return change;
}

bool applyButtonRouteChange(ButtonBindings &bindings, const ButtonRouteChange &change,
                            ButtonRouteResolution resolution)
{
    if (!change.valid || resolution == ButtonRouteResolution::Cancel) return false;
    if (bindings.size() <= static_cast<size_t>(change.sourceIndex)) {
        bindings.resize(static_cast<size_t>(change.sourceIndex + 1));
    }
    if (change.requiresResolution && resolution == ButtonRouteResolution::Replace) {
        for (int index = 0; index < static_cast<int>(bindings.size()); ++index) {
            if (index == change.sourceIndex) continue;
            ButtonBinding &binding = bindings[static_cast<size_t>(index)];
            if (binding.type != ButtonActionType::VirtualButton
                || binding.target != change.targetVirtualButton) {
                continue;
            }
            if (change.canSwap && index == change.displacedSourceIndex) {
                assign(binding, change.previousVirtualButton);
            } else {
                disable(binding);
            }
        }
    }
    assign(bindings[static_cast<size_t>(change.sourceIndex)], change.targetVirtualButton);
    return true;
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

bool hasButtonMappingConflict(const ButtonBindings &buttons, const PovBindings &povs,
                              int sourceIndex, int candidateVirtualButton,
                              int vjoyButtonCapacity)
{
    if (hasButtonMappingConflict(buttons, sourceIndex, candidateVirtualButton,
                                 vjoyButtonCapacity)) {
        return true;
    }
    if (candidateVirtualButton <= 0 || candidateVirtualButton > vjoyButtonCapacity) return false;
    for (const PovDirectionBindings &hat : povs) {
        for (const ButtonBinding &binding : hat) {
            if (binding.type == ButtonActionType::VirtualButton
                && binding.target == candidateVirtualButton) {
                return true;
            }
        }
    }
    return false;
}

bool hasPovMappingConflict(const ButtonBindings &buttons, const PovBindings &povs,
                           int povIndex, int directionIndex, int candidateVirtualButton,
                           int vjoyButtonCapacity)
{
    if (candidateVirtualButton <= 0 || candidateVirtualButton > vjoyButtonCapacity) return false;
    for (const ButtonBinding &binding : buttons) {
        if (binding.type == ButtonActionType::VirtualButton
            && binding.target == candidateVirtualButton) {
            return true;
        }
    }
    for (int hatIndex = 0; hatIndex < static_cast<int>(povs.size()); ++hatIndex) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            if (hatIndex == povIndex && direction == directionIndex) continue;
            const ButtonBinding &binding = povs[static_cast<size_t>(hatIndex)][static_cast<size_t>(direction)];
            if (binding.type == ButtonActionType::VirtualButton
                && binding.target == candidateVirtualButton) {
                return true;
            }
        }
    }
    return false;
}

bool normalizePovMappings(PovBindings &povs, const ButtonBindings &buttons,
                          int vjoyButtonCapacity)
{
    bool valid = true;
    if (povs.size() > kMaximumPhysicalPovs) {
        povs.resize(kMaximumPhysicalPovs);
        valid = false;
    }
    std::array<bool, kMaximumVirtualButtons + 1> used{};
    for (const ButtonBinding &binding : buttons) {
        if (isButtonBindingValid(binding, vjoyButtonCapacity)
            && binding.type == ButtonActionType::VirtualButton) {
            used[static_cast<size_t>(binding.target)] = true;
        }
    }
    for (PovDirectionBindings &hat : povs) {
        for (ButtonBinding &binding : hat) {
            if (!isButtonBindingValid(binding, vjoyButtonCapacity)) {
                if (binding.type != ButtonActionType::Disabled || binding.target != 0) valid = false;
                disable(binding);
                continue;
            }
            if (binding.type == ButtonActionType::Disabled) continue;
            if (used[static_cast<size_t>(binding.target)]) {
                valid = false;
                disable(binding);
                continue;
            }
            used[static_cast<size_t>(binding.target)] = true;
        }
    }
    return valid;
}

int requiredVirtualButtonCount(const ButtonBindings &buttons, const PovBindings &povs,
                               int physicalButtonCount)
{
    int required = boundedCount(physicalButtonCount, kMaximumVirtualButtons);
    const auto inspect = [&required](const ButtonBinding &binding) {
        if (binding.type == ButtonActionType::VirtualButton) {
            required = std::max(required, std::clamp(binding.target, 0, kMaximumVirtualButtons));
        }
    };
    for (const ButtonBinding &binding : buttons) inspect(binding);
    for (const PovDirectionBindings &hat : povs) {
        for (const ButtonBinding &binding : hat) inspect(binding);
    }
    return required;
}

RuntimeButtonTargets buildRuntimeButtonTargets(const ButtonBindings &bindings,
                                               int vjoyButtonCapacity)
{
    RuntimeButtonTargets targets{};
    const int count = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalButtons);
    for (int index = 0; index < count; ++index) {
        const ButtonBinding &binding = bindings[static_cast<size_t>(index)];
        if (!isButtonBindingValid(binding, vjoyButtonCapacity)
            || binding.type != ButtonActionType::VirtualButton) {
            continue;
        }
        bool shared = false;
        for (int previous = 0; previous < index; ++previous) {
            int &existing = targets[static_cast<size_t>(previous)];
            if (existing == binding.target || existing == -binding.target) {
                existing = -binding.target;
                shared = true;
            }
        }
        targets[static_cast<size_t>(index)] = shared ? -binding.target : binding.target;
    }
    return targets;
}

RuntimeButtonTargets buildRuntimeButtonTargets(
    const ButtonBindings &bindings, int vjoyButtonCapacity,
    const std::array<RuntimeProfileTrigger, kMaximumPhysicalButtons> &profileTriggers)
{
    RuntimeButtonTargets targets = buildRuntimeButtonTargets(bindings, vjoyButtonCapacity);
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (profileTriggers[static_cast<size_t>(source)].consumesInput) {
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
        } else if (target <= -1 && target >= -capacity) {
            virtualStates[static_cast<size_t>(-target)] = virtualStates[static_cast<size_t>(-target)]
                || physical[static_cast<size_t>(source)];
        }
    }
    return virtualStates;
}

RuntimePovTargets buildRuntimePovTargets(const PovBindings &bindings, int vjoyButtonCapacity)
{
    return buildRuntimePovTargets(bindings, vjoyButtonCapacity, {});
}

RuntimePovTargets buildRuntimePovTargets(const PovBindings &bindings, int vjoyButtonCapacity,
                                         const RuntimePovProfileTriggers &profileTriggers)
{
    RuntimePovTargets targets{};
    const int hats = std::min(static_cast<int>(bindings.size()), kMaximumPhysicalPovs);
    for (int hat = 0; hat < hats; ++hat) {
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            if (profileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)].consumesInput) {
                continue;
            }
            const ButtonBinding &binding = bindings[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
            if (isButtonBindingValid(binding, vjoyButtonCapacity)) {
                targets[static_cast<size_t>(hat)][static_cast<size_t>(direction)] = binding.target;
            }
        }
    }
    return targets;
}

void mapPovStates(VirtualButtonStates &virtualStates, const PhysicalPovValues &rawValues,
                  int povCount, const RuntimePovTargets &targets, int vjoyButtonCapacity)
{
    const int hats = std::clamp(povCount, 0, kMaximumPhysicalPovs);
    const int capacity = boundedCount(vjoyButtonCapacity, kMaximumVirtualButtons);
    for (int hat = 0; hat < hats; ++hat) {
        const int direction = povDirectionIndex(povDirectionFromRaw(rawValues[static_cast<size_t>(hat)]));
        if (direction < 0) continue;
        const int target = targets[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
        if (target >= 1 && target <= capacity) virtualStates[static_cast<size_t>(target)] = true;
    }
}

} // namespace hotas
