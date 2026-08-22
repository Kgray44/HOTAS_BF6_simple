# v1.4 response-curve research and design notes

This is a bounded design record, not a claim that Battlefield exposes its
proprietary response functions. The Advanced presets are **derived,
evidence-informed transfer functions**. They are intentionally named for
control goals rather than presented as game-exact curves.

## Design evidence

- Microsoft’s controller tutorial uses a cubic stick response after a deadzone
  for camera rotation, a clear public example of reducing small-input response
  while retaining full travel: <https://learn.microsoft.com/en-us/windows/uwp/gaming/tutorial--adding-controls>.
- Microsoft documents normalized gamepad stick inputs in the `[-1, +1]` range
  and explains why deadzones are needed for physical stick variation:
  <https://learn.microsoft.com/en-us/windows/uwp/gaming/gamepad-and-vibration>.
- XInput’s guidance likewise describes clipping and renormalizing stick motion
  outside the deadzone: <https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput>.
- Unity’s Axis Deadzone processor documents bounded clamping and
  renormalization, reinforcing separation of input conditioning from response
  shaping: <https://docs.unity3d.com/Packages/com.unity.inputsystem@1.4/manual/Processors.html>.
- Unity documents the difference between individual-axis and radial stick
  deadzones: <https://docs.unity3d.com/Packages/com.unity.inputsystem@1.4/api/UnityEngine.InputSystem.Controls.StickControl.html>.
- BeamNG’s input guidance acknowledges a configurable response-correction
  curve in simulator hardware workflows: <https://documentation.beamng.com/support/hardware/steering_wheel_common_problems/Input_troubleshooting_guide.pdf>.
- Fritsch and Carlson derive conditions for a monotone piecewise cubic
  interpolant: <https://doi.org/10.1137/0717021>.
- The SciPy PCHIP reference concisely describes a monotonic, C1,
  non-overshooting piecewise-cubic implementation and its slope construction:
  <https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.PchipInterpolator.html>.
- Fritsch and Butland describe a local monotone piecewise-cubic construction:
  <https://doi.org/10.1137/0905021>.

## Family equations

All equations use a normalized display-domain input `x`: `[-1, 1]` for a
centered axis and `[0, 1]` for a unipolar axis. The worker compiles the result
to a 4097-sample immutable LUT; this is separate from the number of editable
points.

### Linear

`f(x) = x`. It is the identity baseline with endpoint preservation and unit
local gain.

### J-Curve

Centered axes use `sign(x) × |x|^(1 + 1.80s)`; unipolar axes use
`x^(1 + 1.80s)`, where `s` is the named strength in `[0, 1]`. This is
continuous and monotonic, reaches both endpoints exactly, and makes the center
less sensitive as strength increases.

### S-Curve

For `a = |x|` on a centered axis (or `a = x` on a unipolar axis), the curve is
`sign(x) × ((1-s)a + s(3a² - 2a³))`; the unipolar form omits `sign`. The
smoothstep term is low-gain near the center and edge, with stronger midrange
response. It is monotonic over the supported parameter range and preserves
endpoints.

### Standard strengths

| Name | strength `s` |
| --- | ---: |
| Very Light | 0.12 |
| Light | 0.22 |
| Medium-Light | 0.34 |
| Medium | 0.48 |
| Medium-Strong | 0.64 |
| Strong | 0.78 |
| Very Strong | 0.90 |
| Maximum | 1.00 |

## Advanced presets

Each definition is a deterministic, symmetric monotonic point response that
is evaluated through the same PCHIP/LUT compiler as every other curve. The UI
calculates center, midrange, edge, and peak gain from that authoritative
definition; those values are not hand-authored labels.

| Preset | Intended use | Behavior | Source basis |
| --- | --- | --- | --- |
| Precision Tracking | fine moving-target tracking | soft center, measured middle, full edges | derived from cubic aim response and precision HOTAS practice |
| Fine Gun Aim | firing-solution corrections | very soft center, controlled upper ramp | derived from pointing-gain trade-offs |
| Stable Strafe | lateral/formation corrections | broad calm center, steady ramp | deadzone-normalized steady control |
| High-Rate Maneuver | rapid rate changes | near-linear center, assertive middle | progressive flight-control authority |
| Hover Control | helicopter attitude work | large fine-control zone, gentle edge recovery | simulator curve practice |
| Fast Acquisition | new target/heading acquisition | responsive center, aggressive middle | derived aim-acquisition gain trade-off |
| Center Stabilizer | reduce over-correction | continuous plateau-like center | deadzone/hysteresis stability principles, without a hard step |
| Progressive Authority | measured edge command | soft start and progressive edge authority | HOTAS response-curve convention |
| Hybrid Precision | precision-to-agility blend | soft center, brisk upper ramp | blended pointing and flight-control response |
| Edge Softened | avoid abrupt maximum rate | responsive middle, eased final travel | bounded response-gain control |

## Safety and interpolation

Point editing accepts only ordered X values, monotonic outputs, bounded ranges,
and protected endpoints. Centered symmetric edits mirror `(x, y)` to
`(-x, -y)`. The Linear option is piecewise-linear. Smooth uses a local,
shape-preserving monotone cubic Hermite/PCHIP-style interpolant and clamps each
interval to its endpoint output range so it cannot overshoot.

Hysteresis remains live state in the worker. It is intentionally not included
in the static response/effective-transfer overlays; the live final marker and
Signal Path show its actual current effect.
