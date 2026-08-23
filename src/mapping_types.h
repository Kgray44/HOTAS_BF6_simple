#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <QString>
#include <QUuid>

namespace hotas {
using namespace Qt::StringLiterals;

constexpr int kPhysicalAxisCount = 8;
constexpr int kVirtualAxisSlotCount = 5;
// DIJOYSTATE2 exposes up to 128 DirectInput button-state bytes. The actual
// controller count is always enumerated at runtime; this is only storage.
constexpr int kMaximumPhysicalButtons = 128;
constexpr int kMaximumVirtualButtons = 128;
constexpr int kMaximumAutomationRules = 64;
constexpr int kMaximumAutomationConditions = 4;
constexpr int kMaximumAutomationActions = 4;
constexpr int kMaximumAutomationProfileContributors =
    kMaximumAutomationRules * kMaximumAutomationActions;
// DIJOYSTATE2 exposes four POV values. Keep the physical report fixed-size so
// the input thread can retain every reported hat without allocating.
constexpr int kMaximumPhysicalPovs = 4;
constexpr int kPovDirectionCount = 8;

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

// A POV is a distinct physical input, not eight fabricated DirectInput
// buttons. The runtime table uses the compact direction index below.
enum class PovDirection : int {
    Centered = 0,
    Up,
    UpRight,
    Right,
    DownRight,
    Down,
    DownLeft,
    Left,
    UpLeft,
};

using PovDirectionBindings = std::array<ButtonBinding, kPovDirectionCount>;
using PovBindings = std::vector<PovDirectionBindings>;
using PhysicalPovValues = std::array<int, kMaximumPhysicalPovs>;
using RuntimePovTargets = std::array<std::array<int, kPovDirectionCount>,
                                     kMaximumPhysicalPovs>;

// Native vJoy POV passthrough is intentionally independent from the eight
// logical direction routes above. A user can therefore retain a precise
// physical hat on vJoy while also assigning a direction to a vJoy button or
// a profile control.
enum class NativePovTargetType : int {
    Disabled = 0,
    Continuous,
    Discrete,
};

struct NativePovBinding {
    bool enabled = false;
    NativePovTargetType targetType = NativePovTargetType::Disabled;
    int targetIndex = 0; // One-based index within the selected vJoy POV type.
};

using NativePovBindings = std::vector<NativePovBinding>;

inline int povDirectionIndex(PovDirection direction)
{
    const int value = static_cast<int>(direction) - 1;
    return value >= 0 && value < kPovDirectionCount ? value : -1;
}

inline PovDirection povDirectionFromRaw(int rawValue)
{
    // DirectInput uses hundredths of a degree and UINT_MAX when centered.
    // Values outside the documented range are treated as safely centered.
    if (rawValue < 0 || rawValue >= 36000) return PovDirection::Centered;
    const int sector = ((rawValue + 2250) / 4500) % kPovDirectionCount;
    return static_cast<PovDirection>(sector + 1);
}

inline QString povDirectionLabel(PovDirection direction)
{
    switch (direction) {
    case PovDirection::Up: return u"Up"_qs;
    case PovDirection::UpRight: return u"Up-Right"_qs;
    case PovDirection::Right: return u"Right"_qs;
    case PovDirection::DownRight: return u"Down-Right"_qs;
    case PovDirection::Down: return u"Down"_qs;
    case PovDirection::DownLeft: return u"Down-Left"_qs;
    case PovDirection::Left: return u"Left"_qs;
    case PovDirection::UpLeft: return u"Up-Left"_qs;
    case PovDirection::Centered: return u"Centered"_qs;
    }
    return u"Centered"_qs;
}

// Profile controls are intentionally separate from profile-specific game
// button bindings. A configured profile-control button is global to the
// physical controller and is consumed before normal vJoy routing.
enum class ProfileTriggerMode : int {
    Disabled = 0,
    Hold,
    Toggle,
};

struct ProfileTriggerBinding {
    QString targetProfileId;
    ProfileTriggerMode mode = ProfileTriggerMode::Disabled;
};

// Automation definitions are durable, UI-facing data only. They are resolved
// into compact numeric records by AutomationCompiler before the mapping
// worker observes them. The fixed limits are deliberately part of the
// configuration contract: no configuration can make a report take unbounded
// work.
enum class AutomationMatchMode : int {
    All = 0,
    Any,
};

enum class AutomationConditionType : int {
    Always = 0,
    AxisAbove,
    AxisBelow,
    AxisBetween,
    AxisOutsideRange,
    ButtonHeld,
    ButtonReleased,
    PovActive,
    PovInactive,
    BaseProfileIs,
    EffectiveProfileIs,
};

enum class AutomationActionType : int {
    VJoyButtonHold = 0,
    VJoyButtonToggle,
    ProfileHold,
    ProfileToggle,
    AxisScale,
    AxisOffset,
    AxisClamp,
    AxisOverride,
    AxisMix,
    AxisFollow,
};

enum class AutomationAxisSourceStage : int {
    Physical = 0,
    Processed,
};

enum class AutomationHealth : int {
    Valid = 0,
    Warning,
    Invalid,
};

struct AutomationConditionDefinition {
    AutomationConditionType type = AutomationConditionType::Always;
    int axis = static_cast<int>(PhysicalAxis::X);
    float minimum = 0.0F;
    float maximum = 0.0F;
    float hysteresis = 0.0F;
    int button = 1; // One-based physical button.
    int povHat = 1; // One-based physical POV hat.
    PovDirection povDirection = PovDirection::Up;
    QString profileId;
};

struct AutomationActionDefinition {
    AutomationActionType type = AutomationActionType::VJoyButtonHold;
    int virtualButton = 1; // One-based vJoy button.
    QString profileId;
    int targetAxis = static_cast<int>(PhysicalAxis::X);
    int sourceAxis = static_cast<int>(PhysicalAxis::X);
    AutomationAxisSourceStage sourceStage = AutomationAxisSourceStage::Processed;
    float value = 0.0F;   // Scale, offset, override, or mix gain.
    float offset = 0.0F;  // Axis Follow offset.
    float minimum = -1.0F;
    float maximum = 1.0F;
};

struct AutomationDefinition {
    QString id;
    QString name;
    bool enabled = true;
    AutomationMatchMode matchMode = AutomationMatchMode::All;
    int priority = 50;
    std::vector<AutomationConditionDefinition> conditions;
    std::vector<AutomationActionDefinition> actions;
};

using ProfileTriggerBindings = std::vector<ProfileTriggerBinding>;
using PovProfileTriggerBindings = std::vector<std::array<ProfileTriggerBinding,
                                                          kPovDirectionCount>>;

using AxisMappings = std::array<AxisMapping, kPhysicalAxisCount>;

struct ControllerProfile {
    QString id;
    QString name;
    AxisMappings axes{};
    ButtonBindings buttons;
    // Missing entries mean safely disabled hats. A saved controller can have
    // fewer hats than a later-connected controller without losing anything.
    PovBindings povs;
};

struct MapperConfiguration {
    QString preferredDeviceId;
    int vjoyDeviceId = 1;
    bool startMappingOnLaunch = false;
    // Global safety value for physical routes with no active virtual target,
    // and for vJoy targets that have no mapped physical source. It is stored
    // in the same normalized domain as the mapper (-1.0 .. +1.0), never in a
    // profile, so a profile change cannot alter parked virtual axes.
    float disabledAxisValue = 0.0F;
    // UI-only selection. It never determines which axes the worker maps.
    int selectedAxisIndex = static_cast<int>(PhysicalAxis::X);
    std::array<Calibration, kPhysicalAxisCount> calibration{};
    std::vector<ControllerProfile> profiles;
    std::vector<PersonalCurvePreset> personalCurvePresets;
    // Global physical-input profile controls. Runtime activation/latch state
    // is deliberately not persisted here.
    ProfileTriggerBindings profileTriggers;
    PovProfileTriggerBindings povProfileTriggers;
    // Native vJoy POV passthrough is global to the selected physical device,
    // rather than profile-specific, and defaults safely off on migration.
    NativePovBindings nativePovBindings;
    // Automation is global to the selected controller configuration. An
    // absent field migrates to this ON/empty state, preserving v1.7 behavior.
    bool automationEnabled = true;
    std::vector<AutomationDefinition> automations;
    QString activeProfileId;
};

inline float sanitizedDisabledAxisValue(float value)
{
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

// This is the complete, allocation-ready mapping payload compiled once when
// configuration changes. The mapping loop only consumes this structure.
struct RuntimeMappingConfiguration {
    std::array<RuntimeAxisMapping, kPhysicalAxisCount> axes{};
    ButtonBindings buttons;
    PovBindings povs;
};

// A complete, immutable profile cache. All curve compilation happens while
// this is built at a configuration boundary, never when a button is pressed.
struct RuntimeProfileTrigger {
    int targetProfileIndex = -1;
    ProfileTriggerMode mode = ProfileTriggerMode::Disabled;
    bool consumesInput = false;
};

using RuntimePovProfileTriggers = std::array<std::array<RuntimeProfileTrigger,
                                                         kPovDirectionCount>,
                                           kMaximumPhysicalPovs>;

struct RuntimeProfileCache {
    std::vector<RuntimeMappingConfiguration> profiles;
    std::array<RuntimeProfileTrigger, kMaximumPhysicalButtons> profileTriggers{};
    RuntimePovProfileTriggers povProfileTriggers{};
    std::array<NativePovBinding, kMaximumPhysicalPovs> nativePovBindings{};
    std::shared_ptr<const struct CompiledAutomationSet> automation;
    int baseProfileIndex = 0;
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
RuntimeProfileCache compileRuntimeProfileCache(const MapperConfiguration &configuration);

inline QString profileTriggerModeLabel(ProfileTriggerMode mode)
{
    switch (mode) {
    case ProfileTriggerMode::Hold: return u"Hold"_qs;
    case ProfileTriggerMode::Toggle: return u"Toggle"_qs;
    case ProfileTriggerMode::Disabled: return u"None"_qs;
    }
    return u"None"_qs;
}

inline ProfileTriggerMode profileTriggerModeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == u"hold"_qs) return ProfileTriggerMode::Hold;
    if (normalized == u"toggle"_qs) return ProfileTriggerMode::Toggle;
    return ProfileTriggerMode::Disabled;
}

inline bool profileTriggerBindingEnabled(const ProfileTriggerBinding &binding)
{
    return binding.mode != ProfileTriggerMode::Disabled && !binding.targetProfileId.trimmed().isEmpty();
}

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
