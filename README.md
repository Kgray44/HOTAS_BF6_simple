# HOTAS BF6 Simple v1.3

A lightweight Windows mapper for routing the Thrustmaster T.Flight HOTAS One
to the vJoy inputs Battlefield 6 recognizes.

## Requirements

- Windows 10/11
- CMake 3.21 or newer
- a C++20 Visual Studio build environment
- Qt 6.5+ with the `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, and `Test`
  components
- [vJoy](https://sourceforge.net/projects/vjoystick/) 2.2.2.0 installed and
  configured with Device 1 exposing X, Y, Z, Rz, and 32 virtual buttons

The app dynamically loads the installed x64 `vJoyInterface.dll`; no vJoy SDK
headers or binaries are copied into this repository. A T.Flight HOTAS One is
the supported initial device, though any DirectInput game controller is visible
for diagnostics and mapping.

## v1.3 features

- Independent 250 Hz-bounded DirectInput worker with a 60 Hz UI snapshot
- Roll, pitch, throttle, and yaw routes to vJoy X, Y, Z, and Rz
- One-to-one physical-to-virtual button mapping through Button 15, calibration,
  capability mismatch warning, live diagnostics, and safe output reset on stop
  or disconnect
- Single-axis **Axes** workspace with selected-axis persistence, detailed
  instrumentation, profile-aware processing controls, and a live dual-curve
  input/output viewer with separate physical and transformed markers
- Persistent Normal and Precision profiles with create, clone, rename, safe
  delete, and instant live activation from the Profiles page
- Per-profile route, inversion, rescaled deadzone, 0.2% default hysteresis,
  output minimum/maximum authority limits, and an identity response-curve
  boundary ready for the v1.4 Curve Editor

## Build and run

From an x64 Native Tools Command Prompt with Qt's CMake package directory on
`CMAKE_PREFIX_PATH` (replace the sample Qt path with the installed kit):

```powershell
cmake -S . -B build-release -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2019_64
cmake --build build-release --config Release
ctest --test-dir build-release --output-on-failure -C Release
.\build-release\HOTASMapper.exe
```

With a Visual Studio generator, the executable is normally
`build\Release\HOTASMapper.exe`. Qt deployment is required when launching
outside a Qt development shell; run `windeployqt` on the executable or use the
Qt Creator run target.

## Default axis routing

| HOTAS control | vJoy axis |
| --- | --- |
| Stick X / Roll | X |
| Stick Y / Pitch | Y |
| Throttle | Z |
| Stick twist / Yaw | Rz |

Mapping runs in its own DirectInput worker thread. The QML UI reads a latest
snapshot at roughly 60 Hz and never schedules virtual-controller writes.

## Axes processing and profiles

Profiles hold axis routes, deadzones, hysteresis, output limits, inversion, and button mappings. The
physical controller's calibration and device preferences remain global. On the
first v1.2 launch, the existing v1.1 mapping becomes **Normal** and an
independent **Precision** clone is created. Switching profiles compiles the
complete mapping table before the worker swaps it between reports, so it never
releases/reacquires DirectInput or vJoy. Held buttons are immediately
re-evaluated against the new routes, releasing obsolete virtual buttons and
asserting any new target.

The authoritative v1.3 axis order is **calibration/normalization → rescaled
deadzone → hysteresis → inversion → Linear response curve → output limits →
vJoy range conversion**. Hysteresis retains the last accepted normalized input
only while movement remains under its threshold; it is not temporal smoothing.
The Axes graph samples the same static C++ evaluator used by the worker for
calibration, deadzone, inversion, and limits. Its OUTPUT trace intentionally
does not pretend hysteresis is a fixed curve; hysteresis instead appears in the
live transformed marker. The graph cache is regenerated only for selection or
processing changes, while its markers read the worker snapshot at about 60 Hz.

Curve controls are intentionally Linear-only in v1.3. **Edit Curve** leads to
the visible v1.4 placeholder; no editable response points or curve storage are
implemented in this release.

## Button mapping

The **Buttons** page enumerates the controller's actual DirectInput buttons and
the selected vJoy device's reported button capacity. A newly detected controller
receives a one-to-one passthrough map until either capacity is exhausted. Each
vJoy destination has exactly one physical source, avoiding ambiguous releases.
If the virtual capacity is below the physical count, Axes, Diagnostics, and
Settings show a clear warning; the supported **Configure vJoy** action opens
vJoy's own configuration utility. Existing saved mappings are never overwritten
when capacity changes.

Button bindings are persisted with axis configuration and a v1.0 configuration
migrates automatically on first detection. Physical state, press-to-identify,
and output state are UI snapshots only; transitions are applied from the same
dedicated worker as the axes. Stop and disconnect reset vJoy output so no
button remains held. vJoy writes are change-driven, so a stationary controller
correctly reports `0 / s · IDLE / CHANGE-DRIVEN` rather than implying a failure.
Each newly discovered physical button defaults to its matching vJoy number
(Button 1 → vJoy Button 1, and so on). Once you set a button to another target
or **Disabled**, that choice is explicit and later automatic initialization
will not replace it.

The **Settings** page reports whether HidHide is installed and, when the
supported CLI can answer, whether cloaking is on. It also opens HidHide’s own
Configuration Client. Keep the mapper executable in HidHide’s application
allowlist before hiding the physical HOTAS; otherwise the mapper would lose
the same controller that the game should not see.
