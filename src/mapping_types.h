#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QString>
#include <QUuid>

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

// A response definition is durable user configuration.  It deliberately
// contains no compiled or runtime-only state: the worker receives a LUT made
// from this definition at a configuration boundary.
enum class CurveFamily : int {
    Linear = 0,
    JCurve,
    SCurve,
    Advanced,
    Personal,
    Custom,
};

enum class CurveInterpolation : int {
    Linear = 0,
    Smooth,
};

struct CurvePoint {
    float input = 0.0F;
    float output = 0.0F;
    bool locked = false;
};

struct CurveDefinition {
    CurveFamily family = CurveFamily::Linear;
    CurveFamily sourceFamily = CurveFamily::Linear;
    // Standard J/S strength is a continuous [0, 1] parameter. Presets only
    // choose named values for it; they do not store arbitrary point arrays.
    float strength = 0.0F;
    QString presetId = u"linear"_qs;
    QString baseLabel = u"Linear"_qs;
    QString sourcePresetId;
    bool pointEditing = false;
    bool symmetry = true;
    CurveInterpolation interpolation = CurveInterpolation::Smooth;
    int pointDensity = 9;
    std::vector<CurvePoint> points;
};

struct PersonalCurvePreset {
    QString id;
    QString name;
    QString description;
    // Point-edited definitions use a centered or 0–100% domain. Personal
    // presets are intentionally offered only to matching axis domains.
    bool unipolar = false;
    CurveDefinition definition;
};

struct CompiledResponseCurve;

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
    // Hysteresis is evaluated on the normalized, deadzone-rescaled input in
    // the worker. 0.2% filters tiny report noise without adding temporal lag.
    float hysteresis = 0.002F;
    // These are deliberate command-authority limits, independent from
    // physical-device calibration. They are applied after inversion/curve.
    float outputMinimum = -1.0F;
    float outputMaximum = 1.0F;
    CurveDefinition curve;
};

// Calibration belongs to the physical controller. Runtime mappings combine
// it with an active profile only after the profile has been selected.
struct RuntimeAxisMapping {
    AxisMapping profile;
    Calibration calibration;
    std::shared_ptr<const CompiledResponseCurve> responseCurve;
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
    // Defaults are filled as 1:1 passthrough when a controller is discovered.
    // A user edit, including Disabled, makes that source authoritative.
    bool explicitlyConfigured = false;
};

using ButtonBindings = std::vector<ButtonBinding>;

using AxisMappings = std::array<AxisMapping, kPhysicalAxisCount>;

struct ControllerProfile {
    QString id;
    QString name;
    AxisMappings axes{};
    ButtonBindings buttons;
};

struct MapperConfiguration {
    QString preferredDeviceId;
    int vjoyDeviceId = 1;
    bool startMappingOnLaunch = false;
    // UI-only selection. It never determines which axes the worker maps.
    int selectedAxisIndex = static_cast<int>(PhysicalAxis::X);
    std::array<Calibration, kPhysicalAxisCount> calibration{};
    std::vector<ControllerProfile> profiles;
    std::vector<PersonalCurvePreset> personalCurvePresets;
    QString activeProfileId;
};

// This is the complete, allocation-ready mapping payload compiled once when
// configuration changes. The mapping loop only consumes this structure.
struct RuntimeMappingConfiguration {
    std::array<RuntimeAxisMapping, kPhysicalAxisCount> axes{};
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

inline bool isVirtualControllerName(const QString &name)
{
    const QString normalized = name.trimmed().toCaseFolded();
    // Never route a virtual controller back into itself. vJoy appears in
    // DirectInput alongside physical HOTAS devices and may otherwise win a
    // stale saved preference or fallback enumeration.
    return normalized.contains(u"vjoy"_qs)
        || normalized.contains(u"virtual joystick"_qs);
}

inline bool isUnipolarAxis(PhysicalAxis axis)
{
    // The HOTAS throttle is displayed as 0–100%, while the internal mapping
    // representation remains the vJoy-friendly -1…+1 normalized range.
    return axis == PhysicalAxis::Z;
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

inline QString normalProfileId()
{
    return u"profile-normal"_qs;
}

inline QString precisionProfileId()
{
    return u"profile-precision"_qs;
}

inline bool isProfileNameValid(const QString &name)
{
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= 48;
}

inline QString newProfileId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline AxisMappings defaultAxisMappings()
{
    AxisMappings axes;
    axes[static_cast<int>(PhysicalAxis::X)].target = VirtualAxis::X;
    axes[static_cast<int>(PhysicalAxis::Y)].target = VirtualAxis::Y;
    axes[static_cast<int>(PhysicalAxis::Z)].target = VirtualAxis::Z;
    axes[static_cast<int>(PhysicalAxis::Rz)].target = VirtualAxis::Rz;
    return axes;
}

inline ControllerProfile defaultProfile(const QString &id, const QString &name)
{
    ControllerProfile profile;
    profile.id = id;
    profile.name = name;
    profile.axes = defaultAxisMappings();
    return profile;
}

inline const ControllerProfile *findProfile(const MapperConfiguration &configuration,
                                            const QString &id)
{
    for (const ControllerProfile &profile : configuration.profiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

inline ControllerProfile *findProfile(MapperConfiguration &configuration, const QString &id)
{
    for (ControllerProfile &profile : configuration.profiles) {
        if (profile.id == id) return &profile;
    }
    return nullptr;
}

inline const ControllerProfile &activeProfile(const MapperConfiguration &configuration)
{
    if (const ControllerProfile *profile = findProfile(configuration, configuration.activeProfileId)) {
        return *profile;
    }
    return configuration.profiles.front();
}

inline ControllerProfile &activeProfile(MapperConfiguration &configuration)
{
    if (ControllerProfile *profile = findProfile(configuration, configuration.activeProfileId)) {
        return *profile;
    }
    return configuration.profiles.front();
}

inline bool isProfileNameAvailable(const MapperConfiguration &configuration, const QString &name,
                                   const QString &exceptId = {})
{
    const QString desired = name.trimmed().toCaseFolded();
    if (!isProfileNameValid(name)) return false;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (profile.id != exceptId && profile.name.toCaseFolded() == desired) return false;
    }
    return true;
}

// Implemented by the response-curve subsystem so every active profile is
// compiled to immutable LUTs before the mapping worker accepts it.
RuntimeMappingConfiguration compileActiveProfile(const MapperConfiguration &configuration);

inline MapperConfiguration defaultConfiguration()
{
    MapperConfiguration configuration;
    ControllerProfile normal = defaultProfile(normalProfileId(), u"Normal"_qs);
    ControllerProfile precision = normal;
    precision.id = precisionProfileId();
    precision.name = u"Precision"_qs;
    configuration.profiles = {std::move(normal), std::move(precision)};
    configuration.activeProfileId = normalProfileId();
    return configuration;
}

} // namespace hotas
