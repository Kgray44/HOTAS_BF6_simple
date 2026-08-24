# HOTAS BF6 Simple v1.8.4

A lightweight Windows mapper for routing the Thrustmaster T.Flight HOTAS One
to the vJoy inputs Battlefield 6 recognizes.

## Installation and automatic updates

1. Download `HOTAS-BF6-Setup-vX.Y.Z.exe` from the latest [GitHub Release](https://github.com/Kgray44/HOTAS_BF6_simple/releases).
2. Run the installer. It installs per-user to `%LOCALAPPDATA%\Programs\HOTAS BF6` and creates Start Menu and Desktop shortcuts named **HOTAS BF6**.
3. Launch through either shortcut. Both target **HOTAS BF6 Launcher.exe**, not the mapper directly.

At every launch, the native launcher checks the official stable GitHub Release manifest with short timeouts. If a newer stable release is available, it downloads the installer to a temporary staging directory, verifies its SHA-256 digest, silently applies it through a temporary helper, and starts the new launcher. Network, manifest, download, checksum, and installer failures always fall back to the currently installed mapper; no updater remains active while mapping.

The installer upgrades program files only. Existing QSettings data—including profiles, Personal curves, calibration, and button/profile controls—stays in the established user data location. Uninstalling does not remove that data unless you remove it yourself.

## Requirements

- Windows 10/11
- CMake 3.21 or newer
- a C++20 Visual Studio build environment
- Qt 6.5+ with the `Core`, `Gui`, `Network`, `Qml`, `Quick`, `QuickControls2`, and `Test`
  components
- vJoy configured with Device 1 exposing X, Y, Z, Rz, 32 virtual buttons, and
  a continuous or discrete POV if native hat passthrough is wanted
- [HidHide](https://github.com/nefarius/HidHide) is optional but recommended for hiding the physical controller from games when configured correctly

vJoy is required for virtual output. The v1.8.2 installer can offer the pinned
official vJoy and HidHide installers only when they are missing; it never
updates, downgrades, enables, or otherwise reconfigures an already-present
component. Each selected payload is SHA-256 and Authenticode verified before
its normal vendor installer is opened. UAC, device configuration, and any
restart remain user-controlled. See [dependency bootstrap details](docs/dependencies.md).

The app dynamically loads the installed x64 `vJoyInterface.dll`; no vJoy SDK
headers or binaries are copied into this repository. A T.Flight HOTAS One is
the supported initial device, though any DirectInput game controller is visible
for diagnostics and mapping.

## v1.6 packaging and update features

- One authoritative `HOTAS_VERSION` file drives the Qt application, launcher metadata, installer, release manifest, and tag validation. The specific name avoids shadowing C++20's standard `<version>` header on Windows' case-insensitive paths.
- Small console-free native Win32 launcher using WinHTTP and Windows CNG SHA-256, separate from the mapper and Qt runtime
- Stable-release-only updates from `https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json`
- Per-user Inno Setup installer with Start Menu and Desktop shortcuts to the launcher
- GitHub Actions release pipeline for headless tests, synthetic performance benchmark, Qt deployment, installer smoke test, checksums, manifest generation, and release assets
- Optional Authenticode signing hooks through GitHub Secrets; builds are explicitly reported as unsigned until credentials are configured

## v1.8.4 Automation triggers and temporal controls

- Automation now supports true Button **pressed** and **released** events, two-to-five press sequences, long presses, and axis **crosses above/below** events with the established threshold hysteresis.
- A rule-level behavior selector keeps effects composable: apply them while the trigger is active, toggle the entire rule on each trigger, or run it briefly for a per-rule duration. Runtime latch and timer state is never saved and clears on mapping stop, controller disconnect, configuration replacement, and master disable.
- **Tap virtual button** presses a vJoy button immediately and clears it from a bounded worker-owned expiry state—there are no waits, timers, UI reads, or allocations in report processing.
- Timing is shown only under **Advanced** when relevant: multi-press gap, long-press duration, brief active duration, and tap duration. Schema 12 deterministically defaults all new fields for v1.8.0–v1.8.3 Automation rules.
- **RULE SUMMARY** is now a larger primary editor panel and explains event and rule behavior in ordinary language. Overview cards show compact MOMENTARY, TOGGLE, or TIMED behavior labels across Legacy, Standard, and Top Gun.

## v1.8.3 Automation rule-builder UX

- The full-page Automation editor now uses plain-language **WHEN SHOULD THIS HAPPEN?** and **WHAT SHOULD IT DO?** builders. Requirements and effects read as simple HOTAS sentences instead of exposing condition/action arrays, enum names, or match-mode terminology.
- **All the time** is a rule-level choice; the editor creates or normalizes the engine's `Always` condition behind the scenes and hides redundant legacy `Always` terms. Multiple requirements display one consistent **AND** or **OR** connector that maps directly to the existing ALL/ANY behavior.
- A live **RULE SUMMARY** explains the draft before it is saved. New automations remain disabled and blank, show a friendly starter choice, and cannot publish an incomplete rule. Priority is available only in a collapsed **ADVANCED** section.
- Overview cards use readable WHEN/DO summaries, including profile names rather than internal IDs. All formatting occurs while UI models refresh or drafts change; the fixed compiled Automation records and report loop are unchanged.

## v1.8.2 Automation editor redesign

- Automation rules now open in a dedicated full-page editor instead of a
  large modal. The overview keeps engine state, rule counts, active count,
  evaluation time, concise WHEN/THEN summaries, and compact rule actions.
- New rules are persisted as disabled, intentionally incomplete drafts with
  no default condition or action. They cannot be enabled or published until
  they pass normal Automation validation, so creating a rule never alters
  HOTAS output.
- The editor keeps a UI-only draft until **Save Automation** validates,
  persists, and compiles the rule. Its General, WHEN, and THEN sections cover
  every supported condition/action type, match mode, priority, enabled state,
  delete confirmation, and unsaved-change handling.
- Legacy, Standard, and Top Gun share the existing page framework and card
  systems; Top Gun reuses the same aviation panel treatment and tokens as the
  rest of the application. Automation remains presentation/configuration work
  only and adds no per-report UI work.

## v1.8.1 stabilization and update-awareness release

This stabilization release follows the v1.8.0 automation engine release.

- **Disabled Axis Value** is a global, persistent `-100.0%` to `+100.0%`
  safety setting. It defaults to `0.0%`, is not stored in a profile, and is
  applied to every unclaimed virtual X/Y/Z/Rz output by the existing
  change-driven vJoy path. Diagnostics reports that actual parked output while
  raw physical input remains unchanged.
- The **New Profile** dialog now uses the flight UI's rendered title, text
  entry, button, selector, focus, hover, and popup treatments. Its **Start
  From** selector retains its delegate model while opening, so all available
  profile names remain visible. A new profile is a deep copy of the selected
  profile's profile-owned routes and settings; it has no live parent link.
- Legacy, Standard, and Top Gun all render the new setting, update controls,
  update indicator, dialog, input, and profile-source popup through their
  existing visual systems. Standard and Top Gun share behavior while using
  distinct theme tokens; Legacy retains its dedicated surface.
- The mapper checks the launcher's validated stable update manifest
  asynchronously after startup. When a newer semantic version is available,
  the top bar and Settings offer an update handoff. The already-installed
  launcher remains responsible for download, verification, installation, and
  relaunch; a failed launcher start leaves the mapper running.

## v1.7.0 theme system

- **Legacy** is the concrete v1.6.3 visual system and is the default for
  upgrades with no saved theme. **Standard** preserves the current v1.7
  presentation, while **Top Gun** is the aviation visual treatment. All three
  are persistent, instant-switching choices in **Settings → Appearance →
  Theme**.
- Theme state is stored separately from mapper configuration. Switching themes
  never changes the selected axis/profile, curve, calibration, controller
  acquisition, mapping state, or the DirectInput-to-vJoy worker.
- Top Gun applies a dark naval-instrument surface, warm brass structure,
  aviation-orange actions, ivory reference traces, cyan live-output traces,
  compact technical controls, themed menus, and an Axes flight-deck treatment.
- The theme ships as local QML presentation resources; it adds no runtime web
dependency, external artwork, input processing behavior, automation, or
profile semantics.

## v1.8.0 Automation Engine

- Global deterministic rules compile into fixed-capacity worker data before
  mapping reports are processed: 64 rules with up to four conditions and four
  actions each.
- Supports physical axis/button/POV/profile conditions, hysteresis, vJoy
  Button Hold/Toggle, Profile Hold/Toggle, and bounded axis
  Scale/Offset/Clamp/Override/Mix/Follow actions.
- Automation has a persisted master kill switch, explicit invalid-rule health,
  diagnostics, and a dedicated Classic/Top Gun Automation page. See
  `docs/v1.8-automation-engineering-report.md` for deterministic ordering.

## v1.6.3 POV, profile-control, and UI features

- Global **native vJoy POV passthrough** sends a selected DirectInput hat to a
  unique continuous or discrete POV target reported by the active vJoy device.
  Continuous targets preserve the raw DirectInput angle; discrete targets use a
  documented cardinal fallback for diagonals. No native target is enabled by a
  migrated configuration.
- Each of the eight logical POV directions can instead be a global **Hold** or
  **Toggle** profile control. They share the existing deterministic rule:
  newest active Hold, then newest active Toggle, then the manual base profile.
  A profile-control direction is consumed from its vJoy-button route without
  deleting the saved route.
- The Buttons workspace now keeps native-hat output separate from direction
  routing, reports continuous/discrete capacity and availability, and exposes
  profile source feedback for button or POV controls.
- Closed selectors and their popups use the same dark Flight UI treatment;
  axis output limits use compact bounded minus/value/plus steppers instead of
  editable minimum/maximum boxes.
- The supplied HOTAS BF6 icon package is embedded in the mapper, launcher,
  installer, and generated shortcuts. CI verifies its source files, Windows
  resources, dependency metadata, build, headless tests, benchmark, and
  installer smoke layout before publication.

## v1.5 mapping features

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
- Global physical-button **Profile Controls**: assign any button to any stable
  profile ID as **Hold** or **Toggle**. Hold takes precedence over Toggle;
  the newest active control wins within each class. Profile-control buttons are
  consumed, while their saved per-profile vJoy routes remain intact and resume
  if the control is removed.
- Per-profile/per-axis **Curve Editor** with Linear, calculated J-Curve and
  S-Curve families, 8 standard strengths, 10 evidence-informed Advanced
  Presets, reusable Personal Presets, and safe copy/reset actions
- Optional Point Editing with PCHIP-style Smooth or Linear interpolation,
  5/7/9/13/17/25-point materialization, nonuniform point placement,
  add/remove, point locks, snapping, keyboard nudges, and local undo/redo
- Large dual-curve response instrument with always-visible input/reference and
  active output traces, live physical/final markers, gain view, comparison and
  audition overlays, zoom/pan, cursor inspector, health/gain analysis, and a
  collapsible authoritative Signal Path
- Immutable 4097-sample response LUTs compiled on configuration changes and
  swapped between worker reports; editable point count never changes runtime
  curve resolution

## Build and run

From an x64 Native Tools Command Prompt with Qt's CMake package directory on
`CMAKE_PREFIX_PATH` (replace the sample Qt path with the installed kit):

```powershell
cmake -S . -B build-release -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2019_64
cmake --build build-release --config Release
ctest --test-dir build-release --output-on-failure -C Release
.\build-release\HOTAS BF6.exe
```

With a Visual Studio generator, the executable is normally
`build\Release\HOTAS BF6.exe`. Qt deployment is required when launching
outside a Qt development shell; run `windeployqt` on the executable or use the
Qt Creator run target.

## Maintainer release process

1. Finish and test code, then update the root `HOTAS_VERSION` file.
2. Commit and merge the version to `main`.
3. Perform the manual installer, launcher, and HOTAS/vJoy acceptance checks.
4. Create and push the matching annotated tag, for example:

   ```powershell
   git tag -a v1.6.0 -m "HOTAS BF6 v1.6.0"
   git push origin v1.6.0
   ```

The Windows release workflow verifies `HOTAS_VERSION` equals the tag, builds and tests the mapper/launcher, runs `windeployqt`, compiles and smoke-tests the Inno installer on CI, creates `update-manifest.json` and `SHA256SUMS.txt`, and publishes the GitHub Release. A `workflow_dispatch` run is dry-run only and uploads the same artifacts without publishing a release.

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
physical controller's calibration, device preferences, and **Disabled Axis
Value** remain global. On the
first v1.2 launch, the existing v1.1 mapping becomes **Normal** and an
independent **Precision** clone is created. Switching profiles compiles the
complete mapping table before the worker swaps it between reports, so it never
releases/reacquires DirectInput or vJoy. Held buttons are immediately
re-evaluated against the new routes, releasing obsolete virtual buttons and
asserting any new target.

The authoritative v1.5 axis order is **calibration/normalization → rescaled
deadzone → hysteresis → inversion → compiled response curve → output limits →
vJoy range conversion**. Hysteresis retains the last accepted normalized input
only while movement remains under its threshold; it is not temporal smoothing.
The Axes graph samples the same static C++ evaluator used by the worker for
calibration, deadzone, inversion, and limits. Its OUTPUT trace intentionally
does not pretend hysteresis is a fixed curve; hysteresis instead appears in the
live transformed marker. The graph cache is regenerated only for selection or
processing changes, while its markers read the worker snapshot at about 60 Hz.

The Curve Editor and worker share the same C++ curve evaluator/compiler. J is
an endpoint-preserving power family; S is an endpoint-preserving smoothstep
blend. Advanced presets are honestly labelled evidence-informed derived
responses, not proprietary Battlefield curves. Mathematical rationale and
source links are in [docs/curve-research.md](docs/curve-research.md).

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

### Profile Controls

Profile Controls are global to the physical controller rather than stored in a
profile, preventing a held control from disappearing after it changes the
effective profile. **Hold** uses its target only while the physical button is
down. **Toggle** changes state only on a new press. The worker resolves
**most-recent active Hold → most-recent active Toggle → manual base profile**
from its precompiled runtime cache; no QML, persistence, or curve compilation
is involved. Selecting a manual base profile clears Toggle overrides but keeps
currently held controls until release. Stop Mapping and a controller disconnect
clear all runtime-only overrides.

The **Settings** page reports whether HidHide is installed and, when the
supported CLI can answer, whether cloaking is on. It also opens HidHide’s own
Configuration Client. Keep the mapper executable in HidHide’s application
allowlist before hiding the physical HOTAS; otherwise the mapper would lose
the same controller that the game should not see.
