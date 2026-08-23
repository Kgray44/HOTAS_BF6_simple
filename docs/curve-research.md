# v1.4 Curve Studio: research, equations, and preset record

This is a bounded engineering record. None of the presets claims to reproduce a proprietary game curve. They are deterministic, evidence-informed response definitions with a stated control purpose. Every statistic shown in the app and in this document is derived from the authoritative evaluator, rather than entered as presentation metadata.

## Evidence basis

- [Microsoft XInput guidance](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput) explicitly discusses normalizing after a deadzone and using a cubic response to gain lower-range precision while retaining fast full-travel response.
- [Microsoft controller input tutorial](https://learn.microsoft.com/en-us/windows/uwp/gaming/tutorial--adding-controls) gives a public cubic stick-response example.
- [Unity AxisDeadzoneProcessor](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.4/api/UnityEngine.InputSystem.Processors.AxisDeadzoneProcessor.html) documents clamp-and-renormalize conditioning as a separate input stage.
- [Unity Input System processors](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.4/manual/Processors.html) documents configurable processing stages and axis/stick deadzones.
- [War Thunder’s official controls guide](https://wiki.warthunder.com/controls/4785-setting-up-control-axis-and-sensitivity) describes nonlinearity as lower center sensitivity with recovered edge authority, specifically for joystick/aircraft aiming.
- [DCS Axis Tune documentation](https://forum.dcs.world/applications/core/interface/file/attachment.php?id=124317) shows a public flight-simulator axis curve/curvature control.
- [DCS controller guide](https://forum.dcs.world/applications/core/interface/file/attachment.php?id=33751) explains softening reactions around neutral through an axis response curve.
- [Fritsch and Carlson](https://doi.org/10.1137/0717021) and [SciPy’s PCHIP reference](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html) support the monotone, no-overshoot interpolation constraints used for editable and preset point curves.

These sources motivate categories and constraints, not game-exact copies. In particular, “Dynamic-Inspired”, “ADS Precision-Inspired”, and “Dual-Zone Turn-Inspired” are continuous approximations of public response concepts—not Call of Duty, Battlefield, or any other proprietary transfer function.

## Runtime architecture

The persisted curve is configuration. A configuration edit builds a 4097-sample immutable response LUT before the worker accepts the configuration. The real-time path is:

```text
DirectInput -> calibration -> deadzone -> hysteresis -> invert -> LUT -> limits -> vJoy
```

The worker does not construct splines, sort points, blend strength, inspect presets, read QSettings, access QML, calculate gain, or generate graph data per physical report. All families reduce to the same LUT lookup at runtime.

## Domains and families

The display/input domain is `[-1, 1]` for centered axes and `[0, 1]` for a unipolar axis such as throttle. Linear is always `L(x)=x`.

### Continuous J-Curve

Let `s` be Response Strength in `[0,1]` and `p(s)=1+1.80s`.

For unipolar controls:

```text
J(u,s) = u^p(s)
```

This is monotonic, bounded, preserves `J(0)=0` and `J(1)=1`, and is exactly linear at `s=0`. For a centered axis, the deliberate one-sided adaptation is:

```text
u = (x+1)/2
Jcenter(x,s) = 2u^p(s)-1
```

It remains continuous, monotonic, and endpoint preserving. At non-zero strength it intentionally maps physical neutral away from virtual neutral; Curve Health and Signal Path identify that offset rather than hiding it by substituting an S-Curve.

### Correct centered S-Curve

For a centered axis, S is the mirror of the progressive J half:

```text
S(x,s) = sign(x) |x|^p(s)
```

Thus `S(0,s)=0`, endpoints are exact, and both sides have low center gain and increasing authority toward their respective extrema. This replaces the prior smoothstep-centered definition.

For a unipolar control, the valid monotonic S adaptation is:

```text
Sunipolar(u,s) = (1-s)u + s(3u^2 - 2u^3)
```

It is linear at `s=0`, preserves `0`, `0.5`, and `1`, has no overshoot, and increases S-shaped deviation as strength rises.

### Universal Response Strength

J/S use `s` directly in their analytical equations. Advanced, Personal, and Custom have a full response `A(x)` and use:

```text
F(x,s) = (1-s)x + sA(x)
```

Consequently `0%` is Linear, `100%` is the full definition, and every intermediate value is deterministic, bounded, and monotonic when `A` is. Strength is per profile and axis, persists with that configuration, and is compiled into the LUT—not computed in the report loop.

## Advanced presets (15 total)

All rows below use symmetric monotonic point definitions on a centered domain. For a unipolar axis the same definition is normalized from `[0,1]` into the centered evaluator and back. The application calculates Center, 25%, 50%, 75%, and Peak Gain from those definitions on demand; the GUI exposes those authoritative values plus `0% Linear / 100% full researched response`.

| Preset | Category | Best for | Shape / provenance basis |
| --- | --- | --- | --- |
| Precision Tracking | General | small moving targets | soft center, measured middle; cubic aim/HOTAS practice |
| Fine Gun Aim | General | firing-solution corrections | very soft center; pointing-gain trade-off |
| Stable Strafe | General | lateral or formation corrections | broad calm center; steady-control practice |
| High-Rate Maneuver | General | rapid turn-rate changes | near-linear center; progressive flight authority |
| Hover Control | General | helicopter attitude control | large fine-control region; simulator curve practice |
| Fast Acquisition | General | a new target or heading | responsive center; assertive middle |
| Center Stabilizer | General | reducing over-correction | continuous plateau-like center; no hard step |
| Progressive Authority | General | measured edge command | soft start; progressive edge authority |
| Hybrid Precision | General | precision-to-agility transitions | soft center; brisk upper ramp |
| Edge Softened | General | easing final maximum-rate command | responsive middle; softened extreme |
| Shooter Dynamic-Inspired | Shooter / Flight Derived | adaptive-feeling acquisition | public dynamic-response concept; not proprietary exact |
| ADS Precision-Inspired | Shooter / Flight Derived | deliberate sight-picture tracking | documented ADS/pointing sensitivity concepts |
| Dual-Zone Turn-Inspired | Shooter / Flight Derived | quick large turns | public two-zone/turn-rate concepts, continuous approximation |
| Aircraft Gun Tracking | Shooter / Flight Derived | pursuit and lead correction | War Thunder/DCS-style aircraft response guidance |
| Aircraft Maneuver Progressive | Shooter / Flight Derived | progressive high-deflection authority | public simulator/HOTAS curve guidance |

The first ten are preserved from the original v1.4 record. The final five are a distinct Shooter / Flight Derived subcategory with different purposes, not five strength variants. Names deliberately signal inspiration rather than an unverifiable exact clone.

## Editing and health guarantees

Custom and Personal definitions accept ordered inputs, monotonic outputs, bounded domains, protected endpoints, optional centered symmetry, locks, and 0.1–5% grid snapping. Smooth interpolation is a shape-preserving PCHIP-style curve clamped to each adjacent output range; Linear interpolation remains available. The health evaluator reports monotonicity, continuity, full authority, no overshoot, local gains, and the centered-J neutral offset.

Hysteresis is intentionally live state, so static graph/analysis transfer views do not pretend to include it. The permanent Signal Path card displays the actual latest RAW, normalized, deadzone, hysteresis, inversion, curve, limits, and vJoy values instead.
