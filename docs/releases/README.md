# Curated release history

The JSON files in this directory are the authoritative source for historical GitHub Release notes and release-status warnings.

## Release status policy

| Release | Status | Guidance |
| --- | --- | --- |
| v2.1.4 | Current | Runtime Responsiveness, GUI Efficiency & unloadable page lifecycle; recommended published release |
| v2.1.3 | Superseded | Profiles, Categories & Game Detection UX; upgrade to v2.1.4 for asynchronous discovery and lower-churn presentation |
| v2.1.2 | Superseded | Deep Tray & Runtime Efficiency; upgrade to v2.1.4 for the responsive control-plane follow-through |
| v2.1.1 | Superseded | Profile Library Completion & Portable Pack Safety; upgrade to v2.1.4 for the current responsive runtime |
| v2.1.0 | Superseded | Profile Library & Portable Configuration foundation; upgrade to v2.1.4 for the current runtime |
| v2.0.12 | Superseded | Automation Editor Interaction Qualification; upgrade to v2.1.3 for the completed Profile Library and efficient tray runtime |
| v2.0.7 | Superseded | Published Manifest Transport Hotfix; upgrade to v2.1.3 for the current release |
| v2.0.6 | Superseded | Published Manifest Readiness Hotfix; upgrade to v2.0.7 for correct hosted-runner manifest decoding |
| v2.0.5 | Superseded | Published Updater Acceptance Hotfix; upgrade to v2.0.7 for public latest-manifest readiness validation |
| v2.0.4 | Superseded | UI Responsiveness Hotfix; upgrade to v2.0.7 for published updater acceptance reliability |
| v2.0.3 | Superseded | Multi-Controller Setup & vJoy Reliability Hotfix; upgrade to v2.0.7 for UI responsiveness and published updater acceptance reliability |
| v2.0.2 | Superseded | Dashboard & UX Polish; upgrade to v2.0.7 for multi-controller setup, UI responsiveness, and updater reliability fixes |
| v2.0.1 | Superseded | Startup & Upgrade Hotfix; upgrade to v2.0.7 for dashboard, settings, tray, controller-switching, responsiveness, and updater reliability fixes |
| v2.0.0 | **Deprecated** | **Do not install or use; launch-blocking startup defect; upgrade to v2.0.1+** |
| v1.9.3 | Superseded | Upgrade to v2.0.2 for universal controller management and persistent device memory |
| v1.9.2 | **Deprecated** | **Do not use for new setup; upgrade to v2.0.0+** |
| v1.9.1 | **Deprecated** | **Do not use Automatic Repair; upgrade to v2.0.0+** |
| v1.9.0 | **Deprecated** | **Do not use; upgrade to v2.0.0+** |
| v1.8.5 | Historical stable | Stable v1.8 baseline; newer release recommended |
| v1.8.4 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.3 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.2 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.1 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.8.0 | **Deprecated** | **Do not use; upgrade to v1.8.5+** |
| v1.7.0 | Historical stable | Usable historical release |
| v1.6.3 | Historical stable | Stable v1.6 baseline |
| v1.6.2 | Superseded / known issue | Native POV parsing issue; use v1.6.3+ |
| v1.6.1 | Historical | Usable historical release |
| v1.6.0 | Historical | Usable historical release |
| v1.5 | Historical source release | Retroactive notes only; no installer asset |
| v1.4 | Historical source release | Retroactive notes only; no installer asset |
| v1.3 | Historical source release | Retroactive notes only; no installer asset |
| v1.2 | Historical source release | Retroactive tag/Release at verified v1.2 baseline; no installer asset |
| v1.1 | Historical source release | Retroactive notes only; no installer asset |
| v1.0 | Pre-repository milestone | No exact Git source snapshot exists; do not fabricate a tag/Release |

## v2.1.4 page-lifecycle memory qualification

Run this as a machine-specific acceptance check against a fresh packaged build; it complements, but does not replace, the offscreen lifecycle test.

1. Record the mapper process's **Working Set** and **Private Bytes** immediately after launch and after it is idle.
2. Visit Overview, Settings, Profiles, Axes, Buttons, Calibration, Curve Editor, Automation, and Diagnostics. Return to Overview and repeat the sequence several times.
3. While leaving Automation with an unsaved edit and Profiles with an unfinished import choice, reopen each page and confirm the draft is still present.
4. Record Working Set and Private Bytes again after returning to Overview. Confirm only the active page is live, no inactive diagnostics/button/Canvas tree continues telemetry or rendering, and memory does not keep increasing over repeated completed cycles.

Fresh installs normally aim for roughly 100–160 MB Working Set, typical interactive use for roughly 120–180 MB, and a post-navigation result below 200 MB where the machine and Qt graphics stack permit. These are diagnostic targets, not portable hard failures: record both measures and explain material machine-specific differences.

## Rules

- GitHub Release titles are **always exactly the version tag**, for example `v1.9.2`.
- Existing annotated Git tags and release assets are preserved; the synchronizer updates only GitHub Release title/body metadata.
- Missing Releases for verified historical tags may be created retroactively with curated notes, but **no installer/binary asset is fabricated**.
- v1.2 is the one missing historical tag that can be reconstructed safely because commit `c020e6b350ed43addf5a6090087f70c9701750f1` is the exact v1.2 baseline immediately preceding the v1.3 version bump.
- v1.0 remains documentation-only because the repository begins at v1.1; creating a v1.0 Git tag would invent a source snapshot that Git does not contain.
- `deprecated` means **do not install or use** and generates a red caution block with the replacement version.
- `superseded` identifies a real known issue without pretending the entire release is unusable for every historical purpose.
- `historical` and `historical-stable` remain documented without being mislabeled as pre-releases.
- `current` identifies the recommended published release.
- The synchronizer never rewrites existing tag targets and never touches attached release assets.

The `Release Notes Sync` workflow validates these records, creates missing historical Releases only where explicitly authorized, and synchronizes all curated GitHub Release titles/bodies whenever this directory changes on `main`.
