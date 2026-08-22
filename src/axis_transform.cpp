#include "axis_transform.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hotas {

float clampUnit(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

float normalizeCalibrated(float raw, const Calibration &calibration)
{
    raw = clampUnit(raw);
    if (!calibration.enabled) {
        return raw;
    }

    const float minimum = clampUnit(calibration.minimum);
    const float center = clampUnit(calibration.center);
    const float maximum = clampUnit(calibration.maximum);
    if (!(minimum < center && center < maximum)) {
        return raw;
    }

    if (raw >= center) {
        return clampUnit((raw - center) / (maximum - center));
    }
    return clampUnit((raw - center) / (center - minimum));
}

float applyRescaledDeadzone(float value, float deadzone)
{
    value = clampUnit(value);
    deadzone = std::clamp(deadzone, 0.0F, 0.95F);
    const float magnitude = std::abs(value);
    if (magnitude <= deadzone) {
        return 0.0F;
    }

    const float rescaled = (magnitude - deadzone) / (1.0F - deadzone);
    return std::copysign(std::min(rescaled, 1.0F), value);
}

float transformAxis(float raw, const AxisMapping &mapping)
{
    float transformed = normalizeCalibrated(raw, mapping.calibration);
    transformed = applyRescaledDeadzone(transformed, mapping.deadzone);
    if (mapping.inverted) {
        transformed = -transformed;
    }
    // Response curves deliberately belong after this point in a future version.
    return clampUnit(transformed);
}

bool normalizeMappingConflicts(MapperConfiguration &configuration)
{
    std::array<bool, 5> occupied{};
    bool clean = true;
    for (auto &mapping : configuration.axes) {
        const int target = static_cast<int>(mapping.target);
        if (mapping.target == VirtualAxis::Disabled) {
            continue;
        }
        if (target < 1 || target >= static_cast<int>(occupied.size()) || occupied[target]) {
            mapping.target = VirtualAxis::Disabled;
            clean = false;
            continue;
        }
        occupied[target] = true;
    }
    return clean;
}

bool hasMappingConflict(const MapperConfiguration &configuration, int sourceIndex,
                        VirtualAxis candidateTarget)
{
    if (candidateTarget == VirtualAxis::Disabled) {
        return false;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (index != sourceIndex && configuration.axes[index].target == candidateTarget) {
            return true;
        }
    }
    return false;
}

} // namespace hotas
