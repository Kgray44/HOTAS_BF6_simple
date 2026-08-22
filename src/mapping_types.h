#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <QString>

namespace hotas {
using namespace Qt::StringLiterals;

constexpr int kPhysicalAxisCount = 8;
// DIJOYSTATE2 exposes up to 128 DirectInput button-state bytes. The actual
// controller count is always enumerated at runtime; this is only storage.
constexpr int kMaximumPhysicalButtons = 128;
constexpr int kMaximumVirtualButtons = 128;

enum class PhysicalAxis : int {
    X = 0,
    Y,
    Z,
    Rx,
    Ry,
    Rz,
    Slider0,
    Slider1,
};

enum class VirtualAxis : int {
    Disabled = 0,
    X,
    Y,
    Z,
    Rz,
};

struct Calibration {
    bool enabled = false;
    float minimum = -1.0F;
    float center = 0.0F;
    float maximum = 1.0F;
};

struct AxisMapping {
    VirtualAxis target = VirtualAxis::Disabled;
    bool inverted = false;
    float deadzone = 0.03F;
    Calibration calibration;
};

// v1.1 intentionally supports just one action. Keeping the type separate
// from its target leaves a small, stable path for future button actions.
enum class ButtonActionType : int {
    Disabled = 0,
    VirtualButton,
};

struct ButtonBinding {
    ButtonActionType type = ButtonActionType::Disabled;
    int target = 0; // One-based vJoy button number when type is VirtualButton.
};

using ButtonBindings = std::vector<ButtonBinding>;

struct MapperConfiguration {
    QString preferredDeviceId;
    int vjoyDeviceId = 1;
    bool startMappingOnLaunch = false;
    std::array<AxisMapping, kPhysicalAxisCount> axes{};
    ButtonBindings buttons;
};

inline constexpr std::array<PhysicalAxis, kPhysicalAxisCount> kPhysicalAxes {
    PhysicalAxis::X, PhysicalAxis::Y, PhysicalAxis::Z, PhysicalAxis::Rx,
    PhysicalAxis::Ry, PhysicalAxis::Rz, PhysicalAxis::Slider0, PhysicalAxis::Slider1,
};

inline QString physicalAxisKey(PhysicalAxis axis)
{
    switch (axis) {
    case PhysicalAxis::X: return u"x"_qs;
    case PhysicalAxis::Y: return u"y"_qs;
    case PhysicalAxis::Z: return u"z"_qs;
    case PhysicalAxis::Rx: return u"rx"_qs;
    case PhysicalAxis::Ry: return u"ry"_qs;
    case PhysicalAxis::Rz: return u"rz"_qs;
    case PhysicalAxis::Slider0: return u"slider0"_qs;
    case PhysicalAxis::Slider1: return u"slider1"_qs;
    }
    return u"unknown"_qs;
}

inline QString physicalAxisLabel(PhysicalAxis axis)
{
    switch (axis) {
    case PhysicalAxis::X: return u"Roll"_qs;
    case PhysicalAxis::Y: return u"Pitch"_qs;
    case PhysicalAxis::Z: return u"Throttle"_qs;
    case PhysicalAxis::Rz: return u"Yaw"_qs;
    case PhysicalAxis::Rx: return u"Rotation X"_qs;
    case PhysicalAxis::Ry: return u"Rotation Y"_qs;
    case PhysicalAxis::Slider0: return u"Additional axis 1"_qs;
    case PhysicalAxis::Slider1: return u"Additional axis 2"_qs;
    }
    return u"Unknown axis"_qs;
}

inline QString physicalAxisDetail(PhysicalAxis axis)
{
    switch (axis) {
    case PhysicalAxis::X: return u"Stick X"_qs;
    case PhysicalAxis::Y: return u"Stick Y"_qs;
    case PhysicalAxis::Z: return u"Throttle"_qs;
    case PhysicalAxis::Rz: return u"Stick twist"_qs;
    default: return physicalAxisKey(axis).toUpper();
    }
}

inline QString virtualAxisLabel(VirtualAxis axis)
{
    switch (axis) {
    case VirtualAxis::Disabled: return u"Disabled"_qs;
    case VirtualAxis::X: return u"X"_qs;
    case VirtualAxis::Y: return u"Y"_qs;
    case VirtualAxis::Z: return u"Z"_qs;
    case VirtualAxis::Rz: return u"Rz"_qs;
    }
    return u"Disabled"_qs;
}

inline VirtualAxis virtualAxisFromString(const QString &value)
{
    const auto normalized = value.trimmed().toLower();
    if (normalized == u"x") return VirtualAxis::X;
    if (normalized == u"y") return VirtualAxis::Y;
    if (normalized == u"z") return VirtualAxis::Z;
    if (normalized == u"rz") return VirtualAxis::Rz;
    return VirtualAxis::Disabled;
}

inline MapperConfiguration defaultConfiguration()
{
    MapperConfiguration configuration;
    configuration.axes[static_cast<int>(PhysicalAxis::X)].target = VirtualAxis::X;
    configuration.axes[static_cast<int>(PhysicalAxis::Y)].target = VirtualAxis::Y;
    configuration.axes[static_cast<int>(PhysicalAxis::Z)].target = VirtualAxis::Z;
    configuration.axes[static_cast<int>(PhysicalAxis::Rz)].target = VirtualAxis::Rz;
    return configuration;
}

} // namespace hotas
