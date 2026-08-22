# HOTAS BF6 Simple v1.1

A lightweight Windows mapper for routing the Thrustmaster T.Flight HOTAS One
to the vJoy inputs Battlefield 6 recognizes.

## Requirements

- Windows 10/11
- CMake 3.21 or newer
- a C++20 Visual Studio build environment
- Qt 6.5+ with the `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, and `Test`
  components
- [vJoy](https://sourceforge.net/projects/vjoystick/) 2.2.2.0 installed and
  configured with Device 1 exposing X, Y, Z, Rz, and up to 32 virtual buttons

The app dynamically loads the installed x64 `vJoyInterface.dll`; no vJoy SDK
headers or binaries are copied into this repository. A T.Flight HOTAS One is
the supported initial device, though any DirectInput game controller is visible
for diagnostics and mapping.

## v1.1 features

- Independent 250 Hz-bounded DirectInput worker with a 60 Hz UI snapshot
- Roll, pitch, throttle, and yaw routes to vJoy X, Y, Z, and Rz
- One-to-one physical-to-virtual button mapping, calibration, live diagnostics,
  and safe output reset on stop or disconnect
- Compact Mapper, Buttons, Calibration, Diagnostics, and Settings pages

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

## Button mapping

The **Buttons** page enumerates the controller's actual DirectInput buttons and
the selected vJoy device's reported button capacity. A newly detected controller
receives a one-to-one passthrough map until either capacity is exhausted. Each
vJoy destination has exactly one physical source, avoiding ambiguous releases.

Button bindings are persisted with axis configuration and a v1.0 configuration
migrates automatically on first detection. Physical state, press-to-identify,
and output state are UI snapshots only; transitions are applied from the same
dedicated worker as the axes. Stop and disconnect reset vJoy output so no
button remains held.

HidHide is status-only in v1.1. When installed, use its normal UI to hide the
physical HOTAS from the game if duplicate controller input is a problem.
