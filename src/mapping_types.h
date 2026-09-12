#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <QString>
#include <QStringList>
#include <QUuid>

namespace hotas {
using namespace Qt::StringLiterals;

constexpr int kPhysicalAxisCount = 8;
// Slot zero is the durable Disabled sentinel. Keep every real vJoy usage at
// an explicit value: configuration serializes string keys, while the worker
// can retain a compact fixed-size index with no lookup or allocation.
constexpr int kVirtualAxisSlotCount = 9;
// DIJOYSTATE2 exposes up to 128 DirectInput button-state bytes. The actual
// controller count is always enumerated at runtime; this is only storage.
constexpr int kMaximumPhysicalButtons = 128;
constexpr int kMaximumVirtualButtons = 128;
constexpr int kMaximumAutomationRules = 64;
constexpr int kMaximumCalibrationHistoryEntries = 50;
constexpr int kMaximumAutomationConditions = 4;
constexpr int kMaximumAutomationActions = 4;
constexpr int kMaximumAutomationProfileContributors =
    kMaximumAutomationRules * kMaximumAutomationActions;
constexpr int kAutomationMinimumMultiPressWindowMs = 150;
constexpr int kAutomationMaximumMultiPressWindowMs = 1000;
constexpr int kAutomationMinimumLongPressDurationMs = 200;
constexpr int kAutomationMaximumLongPressDurationMs = 3000;
constexpr int kAutomationMinimumRuleActiveDurationMs = 20;
constexpr int kAutomationMaximumRuleActiveDurationMs = 5000;
constexpr int kAutomationMinimumTapDurationMs = 20;
constexpr int kAutomationMaximumTapDurationMs = 500;
// DIJOYSTATE2 exposes four POV values. Keep the physical report fixed-size so
// the input thread can retain every reported hat without allocating.
constexpr int kMaximumPhysicalPovs = 4;
constexpr int kPovDirectionCount = 8;
// Device Rigs are deliberately bounded.  The limits are control-plane
// configuration limits, not a statement about how many controllers Windows
// can enumerate.  They let compilation reserve every report-path slot before
// mapping begins, so adding a device never makes input handling unbounded.
constexpr int kMaximumDeviceRigMembers = 8;
constexpr int kMaximumDeviceRigOutputs = 4;
constexpr int kCurveTransitionMinimumDurationMs = 0;
constexpr int kCurveTransitionMaximumDurationMs = 1000;
constexpr int kDefaultCurveTransitionDurationMs = 100;
constexpr int kAdaptiveResponseSchemaVersion = 2;

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
    X = 1,
    Y = 2,
    Z = 3,
    Rx = 4,
    Ry = 5,
    Rz = 6,
    Slider0 = 7,
    Slider1 = 8,
};

enum class AxisRangeMode : int {
    Centered = 0,
    OneSided,
};

// Adaptive Response is a short-horizon phase-lead estimator. These durable
// records intentionally contain no history: at a configuration boundary they
// are resolved into RuntimeAdaptiveResponseConfig, which is all the report
// loop ever reads.
enum class AdaptiveResponseModel : int {
    Auto = 0,
    Velocity,
    AlphaBeta,
    AlphaBetaGamma,
};

enum AdaptiveResponseProperty : std::uint32_t {
    AdaptiveResponseEnabled = 1U << 0,
    AdaptiveResponseModelProperty = 1U << 1,
    AdaptiveResponseMaximumHorizon = 1U << 2,
    AdaptiveResponseMaximumLead = 1U << 3,
    AdaptiveResponseVelocityResponse = 1U << 4,
    AdaptiveResponseAccelerationResponse = 1U << 5,
    AdaptiveResponseMotionSensitivity = 1U << 6,
    AdaptiveResponseNoiseRejection = 1U << 7,
    AdaptiveResponseReversalDetection = 1U << 8,
    AdaptiveResponseReversalResponse = 1U << 9,
    AdaptiveResponseDecelerationResponse = 1U << 10,
    AdaptiveResponseSettlingResponse = 1U << 11,
    AdaptiveResponseEndpointTaper = 1U << 12,
    // Append-only: these numeric flags are persisted in user configuration.
    // Do not reorder earlier Adaptive Response properties.
    AdaptiveResponseOnsetAssist = 1U << 13,
    AdaptiveResponseOnsetCap = 1U << 14,
    AdaptiveResponseSustainedAssist = 1U << 15,
    AdaptiveResponseSustainedCap = 1U << 16,
    AdaptiveResponseHorizonExtension = 1U << 17,
    AdaptiveResponseHorizonExtensionCap = 1U << 18,
    AdaptiveResponseTurningPointProtection = 1U << 19,
    AdaptiveResponseTurningPointMargin = 1U << 20,
    // V2.5.2 keeps ordinary deliberate movement distinct from the extra
    // authority available during rapid maneuvers.  Append-only because the
    // mask is durable configuration data.
    AdaptiveResponseNormalMovementResponse = 1U << 21,
    AdaptiveResponseRapidMovementResponse = 1U << 22,
    AdaptiveResponseEngagementSensitivity = 1U << 23,
};

constexpr std::uint32_t kAdaptiveResponseAllProperties =
    AdaptiveResponseEnabled | AdaptiveResponseModelProperty | AdaptiveResponseMaximumHorizon
    | AdaptiveResponseMaximumLead | AdaptiveResponseVelocityResponse
    | AdaptiveResponseAccelerationResponse | AdaptiveResponseMotionSensitivity
    | AdaptiveResponseNoiseRejection | AdaptiveResponseReversalDetection
    | AdaptiveResponseReversalResponse | AdaptiveResponseDecelerationResponse
    | AdaptiveResponseSettlingResponse | AdaptiveResponseEndpointTaper
    | AdaptiveResponseOnsetAssist | AdaptiveResponseOnsetCap
    | AdaptiveResponseSustainedAssist | AdaptiveResponseSustainedCap
    | AdaptiveResponseHorizonExtension | AdaptiveResponseHorizonExtensionCap
    | AdaptiveResponseTurningPointProtection | AdaptiveResponseTurningPointMargin
    | AdaptiveResponseNormalMovementResponse | AdaptiveResponseRapidMovementResponse
    | AdaptiveResponseEngagementSensitivity;

struct AdaptiveResponseSettings {
    bool enabled = false;
    AdaptiveResponseModel model = AdaptiveResponseModel::Auto;
    float maximumHorizonMs = 8.0F;
    float maximumLead = 0.12F;
    float velocityResponse = 0.72F;
    float accelerationResponse = 0.58F;
    float motionSensitivity = 0.035F;
    float noiseRejection = 0.012F;
    float reversalDetection = 0.075F;
    float reversalResponse = 1.0F;
    float decelerationResponse = 0.85F;
    float settlingResponse = 0.92F;
    float endpointTaper = 0.16F;
    // Acceleration can fill only unused velocity-derived predictive authority.
    // Defaults preserve pre-onset-assist configuration behavior exactly.
    float onsetAssist = 0.0F;
    float onsetCap = 0.0F;
    // Sustained/horizon/turning defaults retain the pre-V2.3.0 predictor
    // unless the owning preset or layer explicitly enables them.
    float sustainedAssist = 0.0F;
    float sustainedCap = 0.0F;
    float horizonExtension = 0.0F;
    float horizonExtensionCapMs = 0.0F;
    float turningPointProtection = 0.0F;
    float turningPointMargin = 0.0F;
    // V2.5.2 authority controls are user-facing, durable percentages.  The
    // defaults provide a balanced response when an older partial override
    // inherits newly introduced fields.
    float normalMovementResponse = 0.48F;
    float rapidMovementResponse = 0.85F;
    float engagementSensitivity = 0.50F;
};

// A zero mask means "inherit every property". A layer can select a reusable
// preset then override only chosen fields, avoiding duplicate configuration
// blobs while retaining deterministic Global -> Category -> Profile precedence.
struct AdaptiveResponseAxisOverride {
    std::uint32_t properties = 0;
    QString presetId;
    AdaptiveResponseSettings settings;
};

struct AdaptiveResponseLayer {
    std::array<AdaptiveResponseAxisOverride, kPhysicalAxisCount> axes{};
};

struct AdaptiveResponsePreset {
    QString id;
    QString name;
    QString description;
    bool builtIn = false;
    std::array<AdaptiveResponseAxisOverride, kPhysicalAxisCount> axes{};
};

// Fully flattened, trivial runtime data. This is purposely not a pointer to
// persistence data and cannot cause hierarchy/preset/QString work per report.
struct RuntimeAdaptiveResponseConfig {
    bool enabled = false;
    AdaptiveResponseModel model = AdaptiveResponseModel::Auto;
    float maximumHorizonSeconds = 0.008F;
    float maximumLead = 0.12F;
    float velocityResponse = 0.72F;
    float accelerationResponse = 0.58F;
    float motionSensitivity = 0.035F;
    float noiseRejection = 0.012F;
    float reversalDetection = 0.075F;
    float reversalResponse = 1.0F;
    float decelerationResponse = 0.85F;
    float settlingResponse = 0.92F;
    float endpointTaper = 0.16F;
    float onsetAssist = 0.0F;
    float onsetCap = 0.0F;
    float sustainedAssist = 0.0F;
    float sustainedCap = 0.0F;
    float horizonExtension = 0.0F;
    float horizonExtensionCapSeconds = 0.0F;
    float turningPointProtection = 0.0F;
    float turningPointMargin = 0.0F;
    float normalMovementResponse = 0.48F;
    float rapidMovementResponse = 0.85F;
    float engagementSensitivity = 0.50F;
    float domainMinimum = -1.0F;
    float domainMaximum = 1.0F;
};

// Automation compiles temporary response overlays into this primitive-only
// record. It is intentionally separate from the durable QString preset
// selector so report processing never resolves names or persistence layers.
struct RuntimeAdaptiveResponseOverride {
    bool active = false;
    std::uint32_t properties = 0;
    AdaptiveResponseSettings settings;
};

// DirectInput descriptors can contain placeholder objects that never move.
// This is persisted only after a completed calibration has actual travel
// evidence; Unknown deliberately remains the safe pre-calibration state.
enum class PhysicalAxisActivity : int {
    Unknown = 0,
    Active,
    Fixed,
};

constexpr float kPhysicalAxisActivityMinimumTravel = 0.08F;

inline PhysicalAxisActivity physicalAxisActivityForObservedSpan(float minimum, float maximum,
                                                                 bool calibrationCompleted)
{
    if (!calibrationCompleted || !std::isfinite(minimum) || !std::isfinite(maximum)
        || maximum < minimum) return PhysicalAxisActivity::Unknown;
    return maximum - minimum < kPhysicalAxisActivityMinimumTravel
        ? PhysicalAxisActivity::Fixed : PhysicalAxisActivity::Active;
}

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
    // A throttle, slider, or other positional control has useful range but no
    // natural neutral. Keep that distinction durable so calibration never
    // invents a center from wherever a non-centering control happened to be.
    bool centered = true;
};

struct AxisMapping {
    VirtualAxis target = VirtualAxis::Disabled;
    // A centered axis is -1..+1. A one-sided axis promotes the old centered
    // origin to the 0 end-stop and processes only the positive half as 0..1.
    // This is a mapper semantic, not a presentation hint.
    AxisRangeMode rangeMode = AxisRangeMode::Centered;
    QString customName;
    bool inverted = false;
    float deadzone = 0.03F;
    // Hysteresis is evaluated on the normalized, deadzone-rescaled input in
    // the worker. 0.2% filters tiny report noise without adding temporal lag.
    float hysteresis = 0.002F;
    // These are deliberate command-authority limits, independent from
    // physical-device calibration. They are applied after inversion/curve.
    float outputMinimum = -1.0F;
    float outputMaximum = 1.0F;
    // Output limits belong to the active input domain just as curve points do.
    // Retain both ranges so a One-Sided edit never overwrites the user's
    // Centered limits (and vice versa) when Range is toggled.
    float centeredOutputMinimum = -1.0F;
    float centeredOutputMaximum = 1.0F;
    float oneSidedOutputMinimum = 0.0F;
    float oneSidedOutputMaximum = 1.0F;
    CurveDefinition curve;
    // Curve points are domain-specific. Keep the most recent alternate-domain
    // definition so toggling Range never silently destroys a custom curve.
    CurveDefinition centeredCurveBackup;
    CurveDefinition oneSidedCurveBackup;
    bool hasCenteredCurveBackup = false;
    bool hasOneSidedCurveBackup = false;
};

// This is deliberately a mapping-transition setting, not a physical-input
// filter. It is consumed only when the active mapping changes; ordinary
// DirectInput reports retain their direct response path.
struct CurveTransitionSmoothingSettings {
    bool enabled = true;
    int durationMs = kDefaultCurveTransitionDurationMs;
};

inline CurveTransitionSmoothingSettings sanitizedCurveTransitionSmoothing(
    CurveTransitionSmoothingSettings settings)
{
    settings.durationMs = std::clamp(settings.durationMs, kCurveTransitionMinimumDurationMs,
                                     kCurveTransitionMaximumDurationMs);
    return settings;
}

// Calibration belongs to the physical controller. Runtime mappings combine
// it with an active profile only after the profile has been selected.
struct RuntimeAxisMapping {
    AxisMapping profile;
    Calibration calibration;
    std::shared_ptr<const CompiledResponseCurve> responseCurve;
    RuntimeAdaptiveResponseConfig adaptiveResponse;
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
    // Per-profile presentation metadata. The physical identity stays the
    // button's fixed DirectInput index.
    QString customName;
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

// Mapping state is global control-plane configuration, deliberately separate
// from profile-specific game routes. A configured source is consumed before
// normal vJoy button routing.
enum class MappingControlAction : int {
    None = 0,
    MappingOn,
    MappingOff,
    ToggleMapping,
};

enum class MappingEffectiveState : int {
    Active = 0,
    Off,
    Suspended,
};

using MappingControlBindings = std::vector<MappingControlAction>;

// Automation definitions are durable, UI-facing data only. They are resolved
// into compact numeric records by AutomationCompiler before the mapping
// worker observes them. The fixed limits are deliberately part of the
// configuration contract: no configuration can make a report take unbounded
// work.
enum class AutomationMatchMode : int {
    All = 0,
    Any,
};

// This is deliberately rule-level rather than an action type: the same
// lifetime semantics apply to every existing and future Automation action.
enum class AutomationActivationMode : int {
    WhileTriggerActive = 0,
    ToggleOnTrigger,
    RunBriefly,
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
    // Keep the original values above stable: persisted v1.8.0–v1.8.3
    // `buttonReleased` means the level condition "not held". The two new
    // event types intentionally have distinct names and serialized values.
    ButtonPressed,
    ButtonReleaseEvent,
    ButtonMultiPress,
    ButtonLongPress,
    AxisCrossesAbove,
    AxisCrossesBelow,
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
    VJoyButtonTap,
    MappingOn,
    MappingOff,
    ToggleMapping,
    AdaptiveResponseEnable,
    AdaptiveResponseDisable,
    AdaptiveResponsePreset,
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
    int pressCount = 2;
    int multiPressWindowMs = 350;
    int longPressDurationMs = 600;
    // Empty is the V2.3 compatibility sentinel. V2.4 migration resolves it
    // to the proven active physical controller when one exists.
    QString controllerRecordId;
};

struct AutomationActionDefinition {
    AutomationActionType type = AutomationActionType::VJoyButtonHold;
    int virtualButton = 1; // One-based vJoy button.
    QString profileId;
    QString adaptiveResponsePresetId;
    int targetAxis = static_cast<int>(PhysicalAxis::X);
    int sourceAxis = static_cast<int>(PhysicalAxis::X);
    AutomationAxisSourceStage sourceStage = AutomationAxisSourceStage::Processed;
    float value = 0.0F;   // Scale, offset, override, or mix gain.
    float offset = 0.0F;  // Axis Follow offset.
    float minimum = -1.0F;
    float maximum = 1.0F;
    int tapDurationMs = 80;
    QString sourceControllerRecordId;
    QString outputLayoutId;
};

struct AutomationDefinition {
    QString id;
    QString name;
    bool enabled = true;
    AutomationMatchMode matchMode = AutomationMatchMode::All;
    AutomationActivationMode activationMode = AutomationActivationMode::WhileTriggerActive;
    int activeDurationMs = 250;
    int priority = 50;
    std::vector<AutomationConditionDefinition> conditions;
    std::vector<AutomationActionDefinition> actions;
};

using ProfileTriggerBindings = std::vector<ProfileTriggerBinding>;
using PovProfileTriggerBindings = std::vector<std::array<ProfileTriggerBinding,
                                                          kPovDirectionCount>>;

using AxisMappings = std::array<AxisMapping, kPhysicalAxisCount>;

// These stable keys are the V2.4 route identity.  Axis/POV/button indices
// retain their native DirectInput meaning, while the saved-controller ID
// makes the source unambiguous across a rig.  Neither USB enumeration order
// nor a friendly name participates in routing identity.
enum class PhysicalInputType : int {
    Axis = 0,
    Button,
    Pov,
};

struct PhysicalInputKey {
    QString controllerRecordId;
    PhysicalInputType type = PhysicalInputType::Axis;
    int index = 0;
    PovDirection direction = PovDirection::Centered;
};

enum class VirtualOutputTargetType : int {
    Axis = 0,
    Button,
    Pov,
};

struct VirtualOutputKey {
    QString outputLayoutId;
    VirtualOutputTargetType type = VirtualOutputTargetType::Axis;
    int index = 0;
};

enum class DeviceRigDisconnectBehavior : int {
    SuspendAffectedRoutes = 0,
    DeactivateRig,
    UseFallback,
};

// The resolver deliberately uses a small, explicit policy vocabulary.  The
// category order remains the durable tie-breaker; this setting only controls
// whether a profile participates before or after fallback candidates.
enum class ProfileAutomaticSelectionMode : int {
    Preferred = 0,
    Fallback,
    ManualOnly,
};

struct DeviceRigMember {
    QString controllerRecordId;
    bool enabled = true;
    bool required = true;
    QString preferredOutputLayoutId;
};

struct DeviceRigOutputTarget {
    QString outputLayoutId;
    bool enabled = true;
};

struct DeviceRig {
    QString id;
    QString name;
    bool enabled = true;
    bool isDefault = false;
    bool autoActivate = true;
    int activationPriority = 50;
    QString fallbackRigId;
    DeviceRigDisconnectBehavior disconnectBehavior = DeviceRigDisconnectBehavior::SuspendAffectedRoutes;
    std::vector<DeviceRigMember> members;
    std::vector<DeviceRigOutputTarget> outputs;
    // V2.4 preserves the existing opt-in HidHide ownership contract.  It is
    // presentation/control-plane metadata only; reports never change it.
    bool hidhideManaged = false;
    int presentationOrder = 0;
};

// A profile still owns gameplay behavior, but each physical member owns a
// separate mapping payload.  The legacy fields remain temporarily on
// ControllerProfile to make the schema migration lossless and to preserve
// portable V2.3 profiles; V2.4 runtime compilation selects these records.
struct DeviceProfileMapping {
    QString controllerRecordId;
    bool enabled = true;
    AxisMappings axes{};
    ButtonBindings buttons;
    PovBindings povs;
    NativePovBindings nativePovBindings;
    AdaptiveResponseLayer adaptiveResponse;
};

struct ControllerProfile {
    QString id;
    QString name;
    // Category identity is durable presentation/control-plane metadata. The
    // DirectInput worker receives the profile selected at a configuration
    // boundary and never reads categories while processing a report.
    QString categoryId;
    bool enabled = true;
    // Control-plane-only activation policy. MappingWorker receives the
    // resolved profile at a configuration boundary and never reads this.
    ProfileAutomaticSelectionMode automaticSelectionMode = ProfileAutomaticSelectionMode::Preferred;
    // Empty means an unassigned portable/legacy profile.  A V2.4 migration
    // fills it only when an existing active controller can be proven.
    QString deviceRigId;
    // A profile chooses a reusable pre-provisioned virtual controller.  The
    // report loop receives only the already-resolved device ID at a
    // configuration boundary; it never looks this string up.
    QString outputLayoutId;
    // Profiles inherit the global bumpless-transfer behavior unless this
    // explicit advanced override is selected.
    bool curveTransitionSmoothingOverride = false;
    CurveTransitionSmoothingSettings curveTransitionSmoothing;
    AdaptiveResponseLayer adaptiveResponse;
    AxisMappings axes{};
    ButtonBindings buttons;
    // Missing entries mean safely disabled hats. A saved controller can have
    // fewer hats than a later-connected controller without losing anything.
    PovBindings povs;
    // Aliases are profile-local user-facing labels; VirtualAxis remains the
    // immutable vJoy HID identity used by the worker.
    std::array<QString, kVirtualAxisSlotCount> virtualAxisAliases{};
    std::vector<DeviceProfileMapping> deviceMappings;
};

// Categories organise profiles without changing their mapping ownership.
// `profileIds` is an ordered presentation list; profile.categoryId remains
// the authoritative relationship so a damaged order list can be rebuilt
// conservatively during configuration load.
struct ProfileCategory {
    QString id;
    QString name;
    QString icon;
    QStringList executableRules;
    std::vector<QString> profileIds;
    QString defaultProfileId;
    QString lastActiveProfileId;
    bool enabled = true;
    bool restoreLastProfile = true;
    AdaptiveResponseLayer adaptiveResponse;
};

// Controller identity lives with the durable configuration rather than the
// DirectInput worker.  These snapshots are created by the low-frequency
// discovery service and are never consulted while processing an input report.
struct ControllerVJoyRequirements {
    std::array<bool, kVirtualAxisSlotCount> axes{};
    int buttons = 0;
    int continuousPovs = 0;
    int discretePovs = 0;
    int deviceId = 1;
};

// One vJoy descriptor that HOTAS BF6 may adopt or create. Profiles reference
// this durable object instead of treating the global device ID as a profile
// setting. The exact HidHide instance path is optional and is used only when
// the user has explicitly adopted visibility management for this output.
struct VirtualOutputLayout {
    QString id;
    QString name;
    ControllerVJoyRequirements requirements;
    QString hidHideDeviceInstanceId;
    bool hidhideManaged = false;
};

struct DiscoveredController {
    QString name;
    QString directInputId;
    QString productGuid;
    QString hidInstanceId;
    QString hidContainerId;
    int vendorId = 0;
    int productId = 0;
    std::array<bool, kPhysicalAxisCount> axes{};
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
    bool connected = false;
    bool virtualDevice = false;
};

struct SavedControllerRecord {
    QString id;
    QString displayName;
    QString lastDirectInputId;
    QString productGuid;
    QString hidInstanceId;
    QString hidContainerId;
    int vendorId = 0;
    int productId = 0;
    std::array<bool, kPhysicalAxisCount> axes{};
    int axisCount = 0;
    int buttonCount = 0;
    int povCount = 0;
    QString capabilityFingerprint;
    QString lastSeen;
    QString lastVerified;
    int verificationVersion = 1;
    std::array<Calibration, kPhysicalAxisCount> calibration{};
    std::array<PhysicalAxisActivity, kPhysicalAxisCount> axisActivity{};
    ControllerVJoyRequirements vjoyRequirements;
    // Only exact instances HOTAS BF6 has explicitly configured belong here;
    // unrelated HidHide entries are intentionally never represented.
    QStringList ownedHidHideDeviceInstances;
};

// Calibration history is deliberately durable, bounded control-plane data.
// The mapper never reads it while processing DirectInput reports.
struct CalibrationHistoryEntry {
    QString controllerRecordId;
    QString controllerDisplayName;
    QString controllerIdentity;
    QString completedAtUtc;
    QString applicationVersion;
    int calibratedAxisCount = 0;
    std::array<Calibration, kPhysicalAxisCount> calibration{};
};

// Signal Flow is the canonical topology layer. Existing focused editors retain
// their axis/button/POV settings as the configuration surface for transforms,
// but their route fields are a compatibility projection of this topology. The
// mapper receives a bounded compilation of these records, never QML objects or
// dynamic graph traversal.
//
// Keys are stable canonical endpoint/processor descriptors, while IDs are
// durable opaque references used by the graph, diagnostics, undo, and deep
// links. A retained inactive record is a tombstone: recreating a deleted route
// receives a new lifecycle generation rather than borrowing the old identity.
constexpr int kMaximumSignalFlowIdentityRecords = 4096;
constexpr int kMaximumSignalFlowRoutes = 4096;
constexpr int kMaximumSignalFlowMixers = 512;
// A shared conditioner is a first-class topology object rather than a copy of
// a card in the scene.  Keep it bounded with its actual source-axis membership
// so compilation and migration remain predictably small.
constexpr int kMaximumSignalFlowSharedProcessors = 512;
constexpr int kMaximumSignalFlowProcessorPath = 16;
constexpr int kMaximumSignalFlowWorkspaces = 128;
constexpr int kMaximumSignalFlowNodeLayouts = 2048;
constexpr int kMaximumSignalFlowPortGroupStates = 2048;

enum class SignalFlowPortKind : int {
    Axis = 0,
    Button,
    PovDirection,
    NativePov,
};

// Analog fan-in is always explicit. The initial shipping mixer modes are
// intentionally small, deterministic, allocation-free, and testable in the
// compiled mapper. Additional modes must extend this enum and its compiler;
// they may not appear as graph-only labels.
enum class SignalFlowMixerMode : int {
    Disabled = 0,
    Average,
    SumClamped,
    HighestMagnitude,
};

struct SignalFlowIdentityRecord {
    QString key;
    QString id;
    std::uint32_t generation = 0;
    bool active = false;
};

// A route segment is a canonical control-plane edge.  It is deliberately not
// a QML paint primitive: the ids name the endpoints that are persisted with
// the route and are used for hit testing, insertion preconditions, undo, and
// focused-editor reconciliation.  The DirectInput worker continues to consume
// the bounded compiled mapping tables, not this graph representation.
struct SignalFlowRouteSegment {
    QString id;
    QString sourceEndpointId;
    QString destinationEndpointId;
};

struct SignalFlowRoute {
    // identityKey resolves through routeIdentities. `id` is persisted as an
    // integrity check and direct deep-link payload, and is reconciled to that
    // identity record on load/mutation.
    QString identityKey;
    QString id;
    QString profileId;
    QString controllerRecordId;
    SignalFlowPortKind sourceKind = SignalFlowPortKind::Axis;
    int sourceIndex = -1;
    int sourceSubIndex = -1;
    SignalFlowPortKind destinationKind = SignalFlowPortKind::Axis;
    int destinationIndex = -1;
    int destinationSubIndex = -1;
    // Exactly one route per legacy source slot is the focused-editor
    // projection. Additional route records are native Signal Flow fan-out and
    // remain visible rather than being folded into an ambiguous target field.
    bool primaryProjection = false;
    // Compatibility imports retain whether the focused editor considered a
    // one-to-one digital binding an untouched default.  This is canonical
    // route metadata, not a presentation hint: projecting the graph back to
    // legacy controls must preserve the same explicit/default semantics.
    bool implicitDefault = false;
    bool enabled = true;
    // The ordered processor path remains the compact runtime/configuration
    // compatibility projection.  `segments` is the canonical edge topology
    // that makes each processor input and output a real graph endpoint.
    QStringList processorPath;
    std::vector<SignalFlowRouteSegment> segments;
};

struct SignalFlowMixer {
    QString identityKey;
    QString id;
    QString profileId;
    QString controllerRecordId;
    int destinationAxis = -1;
    SignalFlowMixerMode mode = SignalFlowMixerMode::Disabled;
    bool enabled = true;
};

// Several routed physical axes can deliberately refer to one conditioning
// object.  The owner carries the focused-editor settings; reconciliation
// mirrors that processor's durable settings to the member axes so the existing
// fixed-size runtime compiler continues to execute the same signal chain.
// This record is the authoritative sharing relation, not presentation state.
struct SignalFlowSharedProcessor {
    QString identityKey;
    QString id;
    QString profileId;
    QString controllerRecordId;
    QString kind;
    int ownerAxis = -1;
    std::vector<int> sourceAxes;
    bool enabled = true;
};

struct SignalFlowWorkspaceState {
    QString key;
    float panX = 0.0F;
    float panY = 0.0F;
    float zoom = 1.0F;
    QString wireStyle = u"smooth"_qs;
    QString densityMode = u"detailed"_qs;
    int inspectorWidth = 360;
    bool layoutLocked = false;
    // Workspace-only assistance. This has no bearing on routes, compilation,
    // or runtime mapping; it merely decides whether a released graph card may
    // settle onto a nearby visible grid or alignment candidate.
    bool snapToGrid = true;
};

struct SignalFlowNodeLayout {
    QString workspaceKey;
    QString objectId;
    float x = 0.0F;
    float y = 0.0F;
    bool pinned = false;
};

struct SignalFlowPortGroupState {
    QString workspaceKey;
    QString cardId;
    QString group;
    bool collapsed = false;
};

struct SignalFlowState {
    // `topologyVersion == 0` means a pre-topology (schema 26) state. The
    // reconciler deterministically imports legacy profile/device routes once,
    // preserving the identity records already backfilled by V2.6 phase 0.
    int topologyVersion = 0;
    std::vector<SignalFlowRoute> routes;
    std::vector<SignalFlowMixer> mixers;
    std::vector<SignalFlowSharedProcessor> sharedProcessors;
    std::vector<SignalFlowIdentityRecord> routeIdentities;
    std::vector<SignalFlowIdentityRecord> processorIdentities;
    std::vector<SignalFlowWorkspaceState> workspaces;
    std::vector<SignalFlowNodeLayout> nodeLayouts;
    std::vector<SignalFlowPortGroupState> portGroups;
};

struct MapperConfiguration {
    QString preferredDeviceId;
    std::vector<SavedControllerRecord> savedControllers;
    QString activeControllerRecordId;
    std::vector<DeviceRig> deviceRigs;
    QString activeDeviceRigId;
    // The editing rig/scope is intentionally UI-only.  It is persisted for
    // continuity across pages but never consulted by MappingWorker activation.
    QString editingDeviceRigId;
    QStringList editingDeviceRecordIds;
    // One-launch migration notice. It is deliberately runtime-only: preserving
    // legacy data is durable configuration, while explaining the migration is
    // an event that must not reappear on every later Devices visit.
    QString deviceRigMigrationWarning;
    bool autoSwitchVerifiedController = true;
    bool keepRunningInTray = true;
    int vjoyDeviceId = 1;
    bool startMappingOnLaunch = false;
    // Global safety value for physical routes with no active virtual target,
    // and for vJoy targets that have no mapped physical source. It is stored
    // in the same normalized domain as the mapper (-1.0 .. +1.0), never in a
    // profile, so a profile change cannot alter parked virtual axes.
    float disabledAxisValue = 0.0F;
    // Global default for event-driven mapping transitions. This never enables
    // continuous physical-axis filtering.
    CurveTransitionSmoothingSettings curveTransitionSmoothing;
    int adaptiveResponseSchemaVersion = kAdaptiveResponseSchemaVersion;
    AdaptiveResponseLayer adaptiveResponseGlobal;
    std::vector<AdaptiveResponsePreset> adaptiveResponsePresets;
    // UI-only selection. It never determines which axes the worker maps.
    int selectedAxisIndex = static_cast<int>(PhysicalAxis::X);
    std::array<Calibration, kPhysicalAxisCount> calibration{};
    // Current selected-controller activity cache. The matching saved record
    // retains the same data so activity is controller-specific across
    // controller changes.
    std::array<PhysicalAxisActivity, kPhysicalAxisCount> axisActivity{};
    std::vector<CalibrationHistoryEntry> calibrationHistory;
    std::vector<VirtualOutputLayout> outputLayouts;
    std::vector<ControllerProfile> profiles;
    std::vector<ProfileCategory> profileCategories;
    // Detection is control-plane work sampled at a low frequency by
    // AppBackend. It is deliberately absent from RuntimeProfileCache.
    bool automaticGameDetection = true;
    // Legacy candidate-schema input only.  V2.5.4 manual overrides are
    // session-scoped AppBackend state and are intentionally never serialized.
    // Retaining these fields lets an unreleased schema-25 candidate load
    // safely, after which ConfigStore clears them.
    bool activationManualOverride = false;
    QString manualOverrideProfileId;
    std::vector<PersonalCurvePreset> personalCurvePresets;
    // Global physical-input profile controls. Runtime activation/latch state
    // is deliberately not persisted here.
    ProfileTriggerBindings profileTriggers;
    PovProfileTriggerBindings povProfileTriggers;
    MappingControlBindings mappingControls;
    // Normal schema-16 migration clears these after converting each exact
    // legacy control into Automation. A rare overflow keeps only the
    // unconverted controls and exposes this explicit warning instead of
    // creating hidden behavior.
    QString legacyControlMigrationWarning;
    // Native vJoy POV passthrough is global to the selected physical device,
    // rather than profile-specific, and defaults safely off on migration.
    NativePovBindings nativePovBindings;
    // Automation is global to the selected controller configuration. An
    // absent field migrates to this ON/empty state, preserving v1.7 behavior.
    bool automationEnabled = true;
    std::vector<AutomationDefinition> automations;
    // V2.6.0 canonical graph identity and presentation metadata.  Mapping
    // semantics remain in profiles/device mappings until the Signal Flow
    // compiler projects them; the worker never reads this field per report.
    SignalFlowState signalFlow;
    QString activeProfileId;
};

inline float sanitizedDisabledAxisValue(float value)
{
    return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

inline QString defaultOutputLayoutId()
{
    return u"bf6-output"_qs;
}

inline VirtualOutputLayout defaultBf6OutputLayout()
{
    VirtualOutputLayout layout;
    layout.id = defaultOutputLayoutId();
    layout.name = u"BF6 Output"_qs;
    layout.requirements.deviceId = 1;
    layout.requirements.buttons = 32;
    // The standard vJoy device supports all eight conventional axes. A
    // profile still routes only the axes it assigns, but the recommended
    // baseline must not hide usable capability from its editor or verifier.
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        layout.requirements.axes[static_cast<size_t>(index)] = true;
    }
    return layout;
}

inline const VirtualOutputLayout *findOutputLayout(const MapperConfiguration &configuration,
                                                    const QString &id)
{
    const auto found = std::find_if(configuration.outputLayouts.cbegin(),
        configuration.outputLayouts.cend(), [&id](const VirtualOutputLayout &layout) {
            return layout.id == id;
        });
    return found == configuration.outputLayouts.cend() ? nullptr : &*found;
}

inline VirtualOutputLayout *findOutputLayout(MapperConfiguration &configuration, const QString &id)
{
    const auto found = std::find_if(configuration.outputLayouts.begin(),
        configuration.outputLayouts.end(), [&id](const VirtualOutputLayout &layout) {
            return layout.id == id;
        });
    return found == configuration.outputLayouts.end() ? nullptr : &*found;
}

constexpr int kMaximumRuntimeSignalFlowAxisRoutes = kPhysicalAxisCount
    * (kVirtualAxisSlotCount - 1);
// A canonical topology can contain a large number of digital fan-out legs.
// Keep every compiled record in a fixed report-path table; configuration caps
// the total route corpus at kMaximumSignalFlowRoutes, while source buckets
// make an idle input report pay only for pressed/active sources.
constexpr int kRuntimeSignalFlowDigitalSourceCount = kMaximumPhysicalButtons
    + kMaximumPhysicalPovs * kPovDirectionCount;
constexpr int kMaximumRuntimeSignalFlowDigitalRoutes = kMaximumSignalFlowRoutes;
// vJoy exposes at most four continuous and four discrete POV targets. A
// physical hat may deliberately fan out to each distinct target, so this
// covers the complete source/destination space without a heap table.
constexpr int kMaximumRuntimeSignalFlowNativePovRoutes = kMaximumPhysicalPovs
    * kMaximumPhysicalPovs * 2;

constexpr int signalFlowDigitalSourceSlot(SignalFlowPortKind kind, int index, int subIndex)
{
    if (kind == SignalFlowPortKind::Button) {
        return index >= 0 && index < kMaximumPhysicalButtons ? index : -1;
    }
    if (kind == SignalFlowPortKind::PovDirection
        && index >= 0 && index < kMaximumPhysicalPovs
        && subIndex >= 0 && subIndex < kPovDirectionCount) {
        return kMaximumPhysicalButtons + index * kPovDirectionCount + subIndex;
    }
    return -1;
}

struct RuntimeSignalFlowAxisRoute {
    std::uint8_t sourceAxis = 0;
    std::uint8_t destinationAxis = 0;
};

struct RuntimeSignalFlowDigitalRoute {
    std::uint8_t destinationButton = 0;
};

struct RuntimeSignalFlowNativePovRoute {
    std::uint8_t sourcePov = 0;
    std::uint8_t destinationIndex = 0;
    std::uint8_t destinationType = static_cast<std::uint8_t>(NativePovTargetType::Disabled);
};

// This is the complete, allocation-ready mapping payload compiled once when
// configuration changes. The mapping loop only consumes this structure.
struct RuntimeMappingConfiguration {
    std::array<RuntimeAxisMapping, kPhysicalAxisCount> axes{};
    ButtonBindings buttons;
    PovBindings povs;
    CurveTransitionSmoothingSettings curveTransitionSmoothing;
    // Canonical graph topology compiled into fixed report-path tables. The
    // fixed route tables preserve complete analog, digital, and native-POV
    // fan-out without graph traversal or allocation on a DirectInput report.
    std::array<RuntimeSignalFlowAxisRoute, kMaximumRuntimeSignalFlowAxisRoutes> signalFlowAxisRoutes{};
    int signalFlowAxisRouteCount = 0;
    std::array<RuntimeSignalFlowDigitalRoute, kMaximumRuntimeSignalFlowDigitalRoutes> signalFlowDigitalRoutes{};
    std::array<std::uint16_t, kRuntimeSignalFlowDigitalSourceCount> signalFlowDigitalRouteOffsets{};
    std::array<std::uint16_t, kRuntimeSignalFlowDigitalSourceCount> signalFlowDigitalRouteCounts{};
    int signalFlowDigitalRouteCount = 0;
    std::array<RuntimeSignalFlowNativePovRoute, kMaximumRuntimeSignalFlowNativePovRoutes>
        signalFlowNativePovRoutes{};
    int signalFlowNativePovRouteCount = 0;
    bool signalFlowTopologyCompiled = false;
    std::array<SignalFlowMixerMode, kVirtualAxisSlotCount> signalFlowAxisMixers{};
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
    // Resolved at compile time so runtime profile controls can reject a
    // cross-layout request without a QString lookup or device transition in a
    // DirectInput report.
    std::vector<int> profileVjoyDeviceIds;
    std::array<RuntimeProfileTrigger, kMaximumPhysicalButtons> profileTriggers{};
    std::array<MappingControlAction, kMaximumPhysicalButtons> mappingControls{};
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

inline QString physicalAxisActivityKey(PhysicalAxisActivity activity)
{
    switch (activity) {
    case PhysicalAxisActivity::Unknown: return u"unknown"_qs;
    case PhysicalAxisActivity::Active: return u"active"_qs;
    case PhysicalAxisActivity::Fixed: return u"fixed"_qs;
    }
    return u"unknown"_qs;
}

inline PhysicalAxisActivity physicalAxisActivityFromKey(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    if (normalized == u"active"_qs) return PhysicalAxisActivity::Active;
    if (normalized == u"fixed"_qs || normalized == u"inactive"_qs) return PhysicalAxisActivity::Fixed;
    return PhysicalAxisActivity::Unknown;
}

inline QString physicalAxisActivityLabel(PhysicalAxisActivity activity)
{
    switch (activity) {
    case PhysicalAxisActivity::Unknown: return u"Activity unknown"_qs;
    case PhysicalAxisActivity::Active: return u"Active device axis"_qs;
    case PhysicalAxisActivity::Fixed: return u"Inactive device axis"_qs;
    }
    return u"Activity unknown"_qs;
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
    Q_UNUSED(axis);
    // v1.8.5 removes the old physical-Z special case. This helper remains for
    // source compatibility with legacy migration only; new mappings are
    // explicitly configured through AxisRangeMode.
    return false;
}

inline QString axisRangeModeKey(AxisRangeMode mode)
{
    return mode == AxisRangeMode::OneSided ? u"oneSided"_qs : u"centered"_qs;
}

inline AxisRangeMode axisRangeModeFromString(const QString &value,
                                              AxisRangeMode fallback = AxisRangeMode::Centered)
{
    QString normalized = value.trimmed().toCaseFolded();
    normalized.remove(u' ');
    normalized.remove(u'-');
    if (normalized == u"centered"_qs) return AxisRangeMode::Centered;
    if (normalized == u"onesided"_qs || normalized == u"one-sided"_qs) {
        return AxisRangeMode::OneSided;
    }
    return fallback;
}

inline QString axisRangeModeLabel(AxisRangeMode mode)
{
    return mode == AxisRangeMode::OneSided
        ? u"One-Sided (0 to 100)"_qs : u"Centered (-100 to +100)"_qs;
}

inline QString mappingControlActionKey(MappingControlAction action)
{
    switch (action) {
    case MappingControlAction::MappingOn: return u"mappingOn"_qs;
    case MappingControlAction::MappingOff: return u"mappingOff"_qs;
    case MappingControlAction::ToggleMapping: return u"toggleMapping"_qs;
    case MappingControlAction::None: return u"none"_qs;
    }
    return u"none"_qs;
}

inline MappingControlAction mappingControlActionFromString(const QString &value)
{
    QString normalized = value.trimmed().toCaseFolded();
    normalized.remove(u' ');
    normalized.remove(u'-');
    if (normalized == u"mappingon"_qs) return MappingControlAction::MappingOn;
    if (normalized == u"mappingoff"_qs) return MappingControlAction::MappingOff;
    if (normalized == u"togglemapping"_qs) return MappingControlAction::ToggleMapping;
    return MappingControlAction::None;
}

inline QString mappingControlActionLabel(MappingControlAction action)
{
    switch (action) {
    case MappingControlAction::MappingOn: return u"Mapping On"_qs;
    case MappingControlAction::MappingOff: return u"Mapping Off"_qs;
    case MappingControlAction::ToggleMapping: return u"Toggle Mapping"_qs;
    case MappingControlAction::None: return u"None"_qs;
    }
    return u"None"_qs;
}

inline QString virtualAxisLabel(VirtualAxis axis)
{
    switch (axis) {
    case VirtualAxis::Disabled: return u"Disabled"_qs;
    case VirtualAxis::X: return u"X"_qs;
    case VirtualAxis::Y: return u"Y"_qs;
    case VirtualAxis::Z: return u"Z"_qs;
    case VirtualAxis::Rx: return u"Rx"_qs;
    case VirtualAxis::Ry: return u"Ry"_qs;
    case VirtualAxis::Rz: return u"Rz"_qs;
    case VirtualAxis::Slider0: return u"Slider 0"_qs;
    case VirtualAxis::Slider1: return u"Slider 1"_qs;
    }
    return u"Disabled"_qs;
}

inline VirtualAxis virtualAxisFromString(const QString &value)
{
    const auto normalized = value.trimmed().toLower();
    if (normalized == u"x") return VirtualAxis::X;
    if (normalized == u"y") return VirtualAxis::Y;
    if (normalized == u"z") return VirtualAxis::Z;
    if (normalized == u"rx") return VirtualAxis::Rx;
    if (normalized == u"ry") return VirtualAxis::Ry;
    if (normalized == u"rz") return VirtualAxis::Rz;
    if (normalized == u"slider0"_qs || normalized == u"slider 0"_qs) return VirtualAxis::Slider0;
    if (normalized == u"slider1"_qs || normalized == u"slider 1"_qs) return VirtualAxis::Slider1;
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

inline QString generalProfileCategoryId()
{
    return u"category-general"_qs;
}

inline QString newProfileCategoryId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
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

inline const ProfileCategory *findProfileCategory(const MapperConfiguration &configuration,
                                                  const QString &id)
{
    for (const ProfileCategory &category : configuration.profileCategories) {
        if (category.id == id) return &category;
    }
    return nullptr;
}

inline ProfileCategory *findProfileCategory(MapperConfiguration &configuration, const QString &id)
{
    for (ProfileCategory &category : configuration.profileCategories) {
        if (category.id == id) return &category;
    }
    return nullptr;
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
    // These are editable game-facing aliases only; no BF6 raw-HID assumption
    // is made by the worker or the route defaults.
    profile.virtualAxisAliases[static_cast<int>(VirtualAxis::X)] = u"L Left/Right"_qs;
    profile.virtualAxisAliases[static_cast<int>(VirtualAxis::Y)] = u"L Up/Down"_qs;
    profile.virtualAxisAliases[static_cast<int>(VirtualAxis::Z)] = u"R Left/Right"_qs;
    profile.virtualAxisAliases[static_cast<int>(VirtualAxis::Rx)] = u"R Up/Down"_qs;
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

inline const DeviceProfileMapping *findDeviceProfileMapping(const ControllerProfile &profile,
                                                            const QString &controllerRecordId)
{
    const auto found = std::find_if(profile.deviceMappings.cbegin(), profile.deviceMappings.cend(),
                                    [&controllerRecordId](const DeviceProfileMapping &mapping) {
        return mapping.controllerRecordId == controllerRecordId;
    });
    return found == profile.deviceMappings.cend() ? nullptr : &*found;
}

inline DeviceProfileMapping *findDeviceProfileMapping(ControllerProfile &profile,
                                                      const QString &controllerRecordId)
{
    const auto found = std::find_if(profile.deviceMappings.begin(), profile.deviceMappings.end(),
                                    [&controllerRecordId](const DeviceProfileMapping &mapping) {
        return mapping.controllerRecordId == controllerRecordId;
    });
    return found == profile.deviceMappings.end() ? nullptr : &*found;
}

inline DeviceProfileMapping &ensureDeviceProfileMapping(ControllerProfile &profile,
                                                        const QString &controllerRecordId)
{
    if (DeviceProfileMapping *existing = findDeviceProfileMapping(profile, controllerRecordId)) return *existing;
    DeviceProfileMapping mapping;
    mapping.controllerRecordId = controllerRecordId;
    // This copy makes the first V2.4 mapping deterministic and lossless for
    // every V2.3 route, curve, button, POV, and Adaptive Response setting.
    mapping.axes = profile.axes;
    mapping.buttons = profile.buttons;
    mapping.povs = profile.povs;
    mapping.adaptiveResponse = profile.adaptiveResponse;
    profile.deviceMappings.push_back(std::move(mapping));
    return profile.deviceMappings.back();
}

inline const DeviceRig *findDeviceRig(const MapperConfiguration &configuration, const QString &id)
{
    const auto found = std::find_if(configuration.deviceRigs.cbegin(), configuration.deviceRigs.cend(),
                                    [&id](const DeviceRig &rig) { return rig.id == id; });
    return found == configuration.deviceRigs.cend() ? nullptr : &*found;
}

inline DeviceRig *findDeviceRig(MapperConfiguration &configuration, const QString &id)
{
    const auto found = std::find_if(configuration.deviceRigs.begin(), configuration.deviceRigs.end(),
                                    [&id](const DeviceRig &rig) { return rig.id == id; });
    return found == configuration.deviceRigs.end() ? nullptr : &*found;
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

inline bool isProfileNameAvailableInCategory(const MapperConfiguration &configuration,
                                             const QString &name, const QString &categoryId,
                                             const QString &exceptId = {})
{
    const QString desired = name.trimmed().toCaseFolded();
    if (!isProfileNameValid(name) || !findProfileCategory(configuration, categoryId)) return false;
    for (const ControllerProfile &profile : configuration.profiles) {
        if (profile.id != exceptId && profile.categoryId == categoryId
            && profile.name.toCaseFolded() == desired) return false;
    }
    return true;
}

inline bool isProfileCategoryNameAvailable(const MapperConfiguration &configuration,
                                           const QString &name, const QString &exceptId = {})
{
    const QString desired = name.trimmed().toCaseFolded();
    if (desired.isEmpty() || desired.size() > 64) return false;
    for (const ProfileCategory &category : configuration.profileCategories) {
        if (category.id != exceptId && category.name.toCaseFolded() == desired) return false;
    }
    return true;
}

// Implemented by the response-curve subsystem so every active profile is
// compiled to immutable LUTs before the mapping worker accepts it.
RuntimeMappingConfiguration compileActiveProfile(const MapperConfiguration &configuration);
// Compiles the profile payload for one durable physical controller.  This is
// a configuration-boundary operation; the returned table is safe for the
// report hot path and preserves V2.3 transform/Adaptive behavior per device.
RuntimeMappingConfiguration compileDeviceProfileMapping(const MapperConfiguration &configuration,
                                                         const ControllerProfile &profile,
                                                         const DeviceProfileMapping &deviceMapping,
                                                         const SavedControllerRecord *record);
// Profile Hold/Toggle controls are report-path overlays, not Device Rig
// transitions. Resolve their compatibility at compile time so a physical
// report can never create Profile B + Rig A.
bool runtimeProfileControlTargetIsCompatible(const MapperConfiguration &configuration,
                                             const RuntimeProfileCache &cache,
                                             int targetProfileIndex);
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
    ProfileCategory general;
    general.id = generalProfileCategoryId();
    general.name = u"General"_qs;
    ControllerProfile normal = defaultProfile(normalProfileId(), u"Normal"_qs);
    normal.outputLayoutId = defaultOutputLayoutId();
    normal.categoryId = general.id;
    ControllerProfile precision = normal;
    precision.id = precisionProfileId();
    precision.name = u"Precision"_qs;
    configuration.profiles = {std::move(normal), std::move(precision)};
    general.profileIds = {normalProfileId(), precisionProfileId()};
    general.defaultProfileId = normalProfileId();
    general.lastActiveProfileId = normalProfileId();
    configuration.profileCategories = {std::move(general)};
    configuration.outputLayouts = {defaultBf6OutputLayout()};
    configuration.vjoyDeviceId = configuration.outputLayouts.front().requirements.deviceId;
    configuration.activeProfileId = normalProfileId();
    return configuration;
}

} // namespace hotas
