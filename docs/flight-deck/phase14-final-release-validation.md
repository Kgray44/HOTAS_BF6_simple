# Flight Deck Phase 14 final-release validation

## Status

**Pre-promotion qualification complete — no v2.5.0 promotion, tag, or package
publication has occurred.** The candidate source graduates Flight Deck into the
normal presentation selector; that source change is not a published release.
The Phase 14 visual-containment P2 gate is closed by the evidence below.
Installer/CI and governed-promotion gates remain; this record is not yet a
release handoff.

## Starting provenance

| Source point | SHA | Result |
| --- | --- | --- |
| Accepted Phase 13 candidate | `62aad057f77e71348266a1a7dbc45e4eb5d1276e` | Original Phase 14 lineage. |
| Released v2.4.1 `origin/main` | `e46458d93ec270c86513a72389691dd04764f7b3` | Verified merged main SHA and peeled `v2.4.1` tag target. |
| v2.4.1 predecessor release | workflow `34441875812` | Completed successfully with public installer, checksums, and updater manifest. |
| Current candidate relationship | rebased onto `e46458d` | v2.5.0 now incorporates v2.4.1 mainline reliability work before promotion. |

The existing `v2.4.1` tag is retained as the immediate rollback reference.
Flight Deck is a substantial new product capability after that release, so the
candidate version is **v2.5.0**. The version/catalog/documentation update
identifies a candidate only; it is not evidence that a release has happened.

## Post-v2.4.1 rebase correction

The candidate was rebased after the v2.4.1 promotion. The rebase retained
v2.4.1's device-setup reliability behavior and its physical-condition safety
guard. Qualification fixtures now create and remove their own single-device
automation rig rather than weakening that production guard. Two stable QML
identities required for actual pointer proof were restored: `standardSurface`
for the Standard settings host and `experienceAppearanceSelectorPopup` for its
live ComboBox popup. The test opens that popup and clicks the Flight Deck row;
it does not call the ThemeManager directly to simulate success.

The explicit `--startup-smoke` and `--startup-smoke-isolated` routes now
construct the shipped QML root, backend models, and tray while keeping the
presentation hidden. They deliberately do not start DirectInput/vJoy capture,
inventory/verification, game detection, or update work, then return directly
after successful construction. This makes the package smoke deterministic and
safe on an owner machine with an active controller session; ordinary launches
retain the existing event loop and hardware startup behavior.

## Containment correction

The shared Flight Deck spacing system establishes semantic card, compact-card,
technical-card, dialog, popup, row, section, control, and rail insets.
`FlightDeckCard`, `FlightDeckDialog`, status/health surfaces, settings rows,
menus, and page content use those shared values rather than ad-hoc per-label
margins. Reusable text surfaces now bound width, wrap or elide intentionally,
and use content-driven heights where expansion can change layout.

The rail is a three-region layout: fixed identity, a constrained scrollable
navigation viewport, and a fixed/compact readiness card. The rail now uses the
real packaged application icon resource
`qrc:/assets/icons/png/hotas-bf6-256.png`; it does not render a text-only
placeholder.

The Adaptive Response preset selector has an explicit `contentItem` safe
inset equal to its shared Button padding. Qt Quick does not apply `Button`
padding automatically when that custom content is anchored to fill the
control, which was the cause of the title and description touching the
rounded choice boundaries.

Settings' vJoy Device number field now uses `DeckStepper`, an explicit Flight
Deck-styled stepper. It owns its field, divider, and stacked plus/minus
indicators instead of inheriting the platform `SpinBox` indicators, which had
made that Flight Deck control look like an unthemed Windows default.

`FlightDeckDialog` also publishes a header-aware maximum-body budget. The
variable-length setup-repair plan and Input Learning workflow consume that
budget through internal `Flickable` bodies, so long content scrolls before it
can push actions beyond a modal. Input Learning also uses a wrapping action
flow at narrow widths. The Automation delete form and Axis conflict form bind
their bodies to the actual available width rather than a preferred-width
assumption. A new native geometry assertion supplies a deliberately long
presentation-only plan and checks the heading inset, body scroll range, and
action-row reachability; it passed in both appearances during the authorized
native validation.

## Deterministic and focused native evidence

The non-UI checks below did not display or focus the application, did not
touch the user mapper, and did not probe physical devices. The geometry row
is explicitly different: it used a real temporary QQuick window while native
visual testing was owner-authorized. It proves layout bounds, not a completed
human screenshot review.

| Check | Result | Evidence |
| --- | --- | --- |
| Release build of the updated QML containment test | Pass | `app_qml_startup_tests` rebuilt successfully with the repository Qt 6.8.3/MSVC 2022 x64 toolchain. |
| Full release suite | Pass | The final post-rebase run of `ctest --test-dir b14-release -C Release --output-on-failure` passed 12/12, including QML startup, updater-manifest, and ThemeManager contracts. |
| Standard-to-Flight Deck selector | Pass, actual pointer route | The QML startup fixture opens the Standard Settings ComboBox, locates the live popup, and clicks the Flight Deck row; the resulting replacement shell and retained controller configuration are asserted. |
| Adaptive choice safe area | Pass, native geometry | Focused native QQuick fixture verified title and description bounds for Off, Light, Balanced, Fast, Aggressive, and Extreme in both Dark and Light. |
| Dialog title safe area | Pass, native geometry | Focused native QQuick fixture opened the real Devices repair dialog and verified its heading remains within the shared rounded-corner header inset in both Dark and Light. |
| Rail/footer containment | Pass, native geometry | Focused native QQuick fixture verified the footer, its padded content, navigation viewport separation, constrained-height scroll stability, and selected-item reachability at 900x650, 1000x720, 1200x800, 1400x900, and 1600x980 in both Dark and Light. |
| Native visual matrix | Pass | Owner-authorized native review completed: 79 Dark captures in `phase14-flight-deck-visual-revalidation-20260910-0520-native` and 79 Light captures in `phase14-flight-deck-visual-revalidation-20260910-0555-native-light`; the complete offscreen matrix also produced 158 captures. |
| Native Settings vJoy stepper | Pass | Direct Dark and Light Settings captures show the Flight Deck field, divider, and themed plus/minus controls; the test asserts the custom controls remain contained in the selector. |
| Scaled containment | Pass, Qt process scale | `HOTAS_QML_CONTAINMENT_GEOMETRY_ONLY=1` passed at 100%, `QT_SCALE_FACTOR=1.25`, and `QT_SCALE_FACTOR=1.5`. This is process-local Qt rendering validation and did not change the owner's Windows display scaling. |
| Shared-surface QML lint | Pass | Repository Qt 6.8.3 `qmllint` reported no warning or error diagnostics for `FlightDeck.qml`, `FlightDeckAdaptiveResponse.qml`, `FlightDeckDialog.qml`, and `FlightDeckTheme.qml`. |
| Documentation source consistency | Pass | `scripts/sync-documentation.ps1 -Check` reports synchronized v2.5.0 documentation after the final record update. |
| Whitespace integrity | Pass | `git diff --check` passes before the candidate commit. |
| Hot-path performance | Pass, synthetic | The rebuilt release `mapping_core_tests --hot-path-benchmark` completed successfully with zero hot-path allocations. The all-eight-axis Adaptive path measured p99 2.1 us; profile-control p99 was at most 1.8 us; Automation p99 was at most 1.3 us. This benchmark deliberately excludes DirectInput and vJoy driver calls. |
| Staged package contents | Pass | `scripts/stage-package.ps1` staged the rebuilt v2.5.0 mapper, launcher, VERSION, required Qt libraries, platform plugin, and QML runtime in `b14-release/phase14-stage-v2.5.0`. No installer was created locally. |
| Staged package startup | Pass, isolated | The staged mapper exited 0 through the actual hidden `--startup-smoke-isolated` route. The smoke path keeps the QML root headless and skips physical I/O; it does not alter the interactive recovery dialog or normal startup behavior. |

The focused fixture needs an exposed QQuick surface for its layout polish and
therefore runs only when an owner authorizes a native UI test. It is a geometry
regression check, not a replacement for native screenshot review; no further
window is launched while the owner is using the desktop.

## Current hardware boundary

A read-only Windows inventory on 2026-09-10 found present vJoy Driver/vJoy
Device and Nefarius HidHide Device registrations, plus a generic HID-compliant
game-controller node. It did **not** establish the selected physical HOTAS
identity, a DirectInput-to-Raw-Input identity join, physical report capture,
or an active game-visibility transition. The user also has mapper processes
running while playing; they were neither stopped nor controlled. Consequently,
vJoy and HidHide are *detected only*, and physical-controller qualification,
HidHide visibility verification, and any repair remain pending an
owner-controlled session. This evidence must not be substituted for the
required hardware gate.

## Required visual-containment report

| Required final item | Current status | Remaining evidence |
| --- | --- | --- |
| Global content containment | Pass | Shared safe-area components, native geometry fixtures, and native Dark/Light review are green. |
| Card safe padding | Pass | Normal, compact, and technical card tokens were exercised through the width/height visual matrix. |
| Popup / dialog safe padding | Pass | Representative repair, delete, Input Learning, and calibration dialogs were reviewed with shared title/body/action insets and bounded bodies. |
| Minimum-width text containment | Pass | Long-name, long-description, adaptive-choice, and responsive-width fixtures remain contained at the tested sizes. |
| Left-rail footer at minimum height | Pass | Native geometry and the 900x650 through 1600x980 matrix show a fixed reachable readiness footer and independent navigation scroll area. |
| DPI text containment | Pass (Qt scale) | The native containment fixture passed at 125% and 150% `QT_SCALE_FACTOR`; Windows global display scaling was not modified. |
| Visual matrix re-review | Pass | Owner reviewed the native pass; fresh 79-image Dark and 79-image Light capture sets were inspected after the shared fixes. |

## Runtime-warning cleanup after the first native pass

The native warning capture identified high-frequency binding churn outside the
Flight Deck shell. The release candidate now replaces conditional
`undefined` font-family values with an explicit default family, gives
Settings a defined faint-text color, defaults transient profile/import/device
maps to concrete empty values, uses explicit widths for conventional dialogs,
and guards Legacy's transient axis selection. These are containment and
lifecycle corrections; the required owner-authorized native rerun is now
complete.

## Remaining Phase 14 gates

1. Owner-session physical HOTAS, vJoy, and HidHide qualification remains
   unavailable: no exact selected-device DirectInput-to-Raw-Input identity
   join or game-visibility proof exists. This presentation-only candidate must
   not represent detected devices as qualified hardware.
2. The actual installer and upgrade path is CI-only: Inno Setup is not
   installed locally and `verify-installer-upgrade.ps1` deliberately refuses
   to run outside an isolated GitHub Actions Windows runner. The local staged
   package smoke passed; CI must still prove clean install, v1.9.3 upgrade,
   and v2.0.0 recovery against the published installer.
3. Run the final documentation/whitespace checks, commit and push the
   candidate, follow repository mainline governance, and tag only the merged
   promoted SHA. Verify the terminal CI workflow, public assets, updater
   manifest, checksum, and signing state against that peeled tag before
   calling this a release.
