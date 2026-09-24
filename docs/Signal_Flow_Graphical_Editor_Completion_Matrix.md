# Signal Flow graphical editor completion matrix

## Scope and evidence snapshot

This is an engineering-status matrix, not a completion declaration.  The
graphical editor is deliberately described without a product-version label;
application build provenance remains versioned separately.

The requested Master Design Specification v1.1 and Invariant Sheet v1.1 were
not present in the worktree, sibling worktrees, or supplied attachments during
this audit.  The available Design Amendment, remediation, handoff, and
closeout records were read, but their prose is not treated as product proof.

Snapshot source: `codex/v2.6.3-signal-flow-phase6-75-inspector-library` at
`76ef4cc07e660d75904d896ac01ac463e3181100`, including the processor
channel-pair delegate's required `index` correction.

The latest focused native run reached the card drag, wire drag, processor
channel dwell, context menu, workspace control, and Library insertion
qualifications.  It then failed the native temporary-hover fixture:

```
Signal Flow native temporary-hover fixture did not reveal an Axes port row
(expanded=0 section=1)
```

The broad lifecycle debt remains separately tracked:

```
app_qml_startup_tests
unresolved broad lifecycle / fixture cleanup termination
exit 0xC0000409
```

Allowed matrix states are used literally: **PASS**, **FAIL**, or an explicit
future-phase deferral.  No row marked PASS substitutes for owner acceptance.

| Requirement | Source implementation | Automated evidence | Native review path | Status |
| --- | --- | --- | --- | --- |
| Master Design Specification v1.1 and Invariant Sheet v1.1 are available for final provenance review | Not present in the audited locations | Filesystem audit found neither source | Supply the authoritative documents for cross-checking | FAIL |
| Candidate identity is visible and trustworthy | `signalFlowBuildProvenance` and Technical Details in `qml/FlightDeckSignalFlow.qml`; backend build-provenance API | `verifySignalFlowNativeWorkspaceControls` reports `provenance=1` | Open Graph Settings → Technical Details | PASS |
| Approximate 1.1-second temporary-hover grace | `temporaryGroupHoverGraceMs: 1100`, per-section state and timer in `FlightDeckSignalFlow.qml` | Fixture asserts the 1100 ms contract but currently cannot receive the Axes header hover | Hover Axes, move through rows, leave/re-enter during grace | FAIL |
| Manual-open precedence over temporary hover | `cardGroupCollapsed`, `cardGroupHoverGraceActive`, `setGroup` | Covered by the temporary-hover fixture after its header-enter assertion; that fixture is currently red | Explicitly open a section and leave it | FAIL |
| Library, ghost, and canvas block presentations share a visual family wherever practical | `SignalFlowBlockPresentation.qml` is used by Library and placement ghost; canvas cards remain specialized interactive compositions | No dedicated visual-family qualification | Compare a processor, endpoint, Text Note, and Group on canvas, in Library, and as ghost | FAIL |
| Canonical processor catalog is truthful | `AppBackend::signalFlowProcessorCatalog` exposes deadzone, center-hold, invert, curve, limits, adaptive-response | Backend catalog checks and native workspace control `catalog=1` | Empty Library lists only real available processors | PASS |
| Unsupported processor families are explicitly classified and not shown as fake palette entries | Palette exposes only the six audited canonical processors; Mixer/OR/AND/Toggle/Pulse/Gate/POV classification record is absent | Catalog coverage confirms no fake displayed entries; no written classification test | Inspect catalog and documented family classifications | FAIL |
| Library categories, previews, search, keyboard arm, and ghost | `SignalFlowBlockPresentation.qml` and Block Library in `FlightDeckSignalFlow.qml` | Native controls: `catalog=1 search_respo=1 search_invert=1 keyboard=1 processor_ghost=1` | Open Library; search `respo`/`invert`; Enter; Esc | PASS |
| Endpoint Library semantics are explicit, persisted presentation behavior | Endpoint entries use existing canonical graph nodes; a final user-facing visibility/reposition contract is not documented | No dedicated hidden-endpoint persistence qualification | Drag visible and hidden endpoint entries; verify membership and mappings remain unchanged | FAIL |
| Catalog tile persists after drag | Stable `blockLibraryCatalog`, library `DragHandler { target: null }` | Native Library drop: `persistent_tile=1` | Drag a processor and verify the source tile remains | PASS |
| Processor insertion on a compatible wire is canonical and undoable | `AppBackend::signalFlowInsertProcessor`, target lookup, and Library drop completion | Native Library drop: `canonical_insert=1 ghost_cleared=1 undo=1` | Drop Response Curve on a compatible wire then Undo | PASS |
| Processor compatibility preview stays local to pointer work | Indexed geometry and precomputed compatibility paths in `FlightDeckSignalFlow.qml` / backend | Native workspace control: `compatibility_precomputed=1 pointer_compatibility_calls=0` | Move an armed processor over compatible/noncompatible wires | PASS |
| Processor channel-pair dwell adds a real pair | `beginProcessorChannelDwell`, canonical backend processor update, `processorNode.channelPairs` | Native processor dwell: `preview_pair=1 canonical_pair=1 undo=1 dwell_ms=1100` | Start a wire and dwell over a processor for ~1.1 seconds | PASS |
| Processor row layout does not use an undeclared delegate index | `required property int index` on the channel-pair delegate | Current QML compile succeeds without the former `ReferenceError: index is not defined` | Inspect a multi-channel processor during routing | PASS |
| Fast click-to-drag freezes disclosure and anchors | `freezeDisclosureGeometry` / `releaseDisclosureGeometry` | Drop continuity log: `disclosure_freeze=[established=1 stable=1 released=1]` | Quickly drag a card immediately after disclosure changes | PASS |
| Card release performs incident-only geometry work | Deferred local release reflow and indexed port anchors | Drop continuity: `full_wire_rebuilds=0`, `incident_wire_updates=4`, `workspace_writes=1` | Repeated card drops must not flash unrelated wires | PASS |
| Wires render above connected card sockets but below unrelated cards | Layering, endpoint caps, and card z-order in `FlightDeckSignalFlow.qml` | Source reviewed; no dedicated native layering assertion | Route wires near connected and unrelated cards | FAIL |
| Orthogonal routes avoid accidental exact overlap | Orthogonal routing and route segment offsets in `FlightDeckSignalFlow.qml` | No dedicated collision/overlap qualification found | Create several distinct orthogonal routes | FAIL |
| Viewport-backed grid and camera | Graph viewport, grid canvas, mouse/trackpad pan, wheel/pinch/pointer-centered zoom | Native workspace control reports `zoom=1`; source reviewed | Pan and zoom across every viewport edge | FAIL |
| Smart/Connected/Compact/Expanded ports menu | `signalFlowPortVisibilityControl` and port-visibility menu | Native workspace control reports `ports=1` | Click Ports: smart and choose each policy | PASS |
| Device Rig and Profile editing context preserve runtime semantics | Rig/Profile menus plus `setSignalFlowEditingProfileContext` | Native controls report `rig=1 profile=1`; backend test asserts editing context does not activate runtime profile | Choose contexts in Configured and Effective views | PASS |
| Inspector is floating and broadly draggable | Inspector panel and drag-region filtering in `FlightDeckSignalFlow.qml` | Source reviewed; no final focused pointer assertion in the current run | Drag header and blank body; verify buttons, text, scrollbar remain interactive | FAIL |
| Graph Settings, themed tooltips, close controls, and panel floating behavior | Graph Settings popup, `DeckTooltip`, shared panel controls | Native workspace control reports `settings=1`; source reviewed | Open/close/move settings and inspect tooltips | FAIL |
| Context menus cover node, port, route, canvas, and sections | Context menus in `FlightDeckSignalFlow.qml` | Native context menu: `node=1 port=1 route=1 canvas=1 section=1` | Right-click each named surface | PASS |
| Text Note and Group / Section annotations attach spatially | Workspace annotation attachment functions in `FlightDeckSignalFlow.qml` | No focused native attachment qualification in current run | Create, move, resize, and attach near a block and route | FAIL |
| Port hover reports direction, topology, route count, and health | `portHoverSummary` and `DeckTooltip` | Source reviewed; not independently owner-qualified | Hover input and output ports with processing | FAIL |
| Indexed presentation lookups avoid per-frame canonical scans | `presentationIndexForGraph`, route/port lookup helpers | Drop continuity shows `full_hit_index_rebuilds=0`, `incremental_bucket_updates=144` | Drag and route without progressive slowdown | PASS |
| Presentation map cloning and QML allocation/GC behavior are measured and bounded | Local QML state maps remain in use | No before/after allocation or GC evidence | Exercise anchors, hover, positions, and Inspector movement at stress scale | FAIL |
| Every processor Open Settings path reaches a useful authoritative editor | `openFullSettings` routes Curve, Adaptive Response, and axis-owned processors to existing editors | Source reviewed; no focused native navigation assertion for every processor/context combination | Use Inspector, context menu, and double-click for every catalog processor | FAIL |
| Library drag/drop crash is absent | Retained model delegate, null drag target, completion timer, canonical insertion path | One native compatible-wire drop passed; no repeated owner-native soak after the reported crash | Repeat drag-to-wire, background drop, cancel, and reopen Library | FAIL |
| Focused native Signal Flow subset is green | `app_qml_startup_tests` native-pointer sequence | Latest run exits `1` at temporary-hover fixture, despite preceding native qualifiers passing | N/A | FAIL |
| Broad QML lifecycle termination is resolved | Test-harness/fixture cleanup architecture | Known unresolved termination `0xC0000409` | N/A | FAIL |
| Backend lifecycle, full CTest, mapping tests, and HOTPATH suite are green | Project test targets | Full regression has not been rerun; backend lifecycle run previously exceeded its bounded observation | N/A | FAIL |
| Final UI and mapping performance qualification | UI performance counters and mapping HOTPATH benchmark | Existing drag continuity evidence only; no final idle/memory/frame/throughput report | Stress graph and normal graph review | FAIL |
| Processor targeting and Library search performance are measured at small, normal, and stress scales | Cached compatible segments and filtered presentation entries | Local pointer call counter is zero after precomputation; no required scale/allocation/frame report | Arm processor, traverse many wires, search ten characters, navigate, arm, and drag | FAIL |
| PR #84 is conflict-clean and reviewable against current main | Git branch and GitHub PR state | Current PR was reported `CONFLICTING` / `DIRTY`; reconciliation has not been completed | Review GitHub PR #84 | FAIL |
| Owner-native acceptance completed | One current isolated native candidate and owner checklist | Not yet performed; no acceptance claim | Complete every item in the owner review checklist | FAIL |
| No Phase 7 work started; no merge, tag, or release occurred | Branch history and repository state | No merge/tag/release action was performed in this phase | Inspect branch and repository release history | PASS |

## Current gate

Phase 6.75 remains open.  The current gating failures are: the focused
temporary-hover native assertion, the tracked broad lifecycle termination,
full regression/performance qualification, PR mergeability, and owner-native
acceptance.  The product candidate must not be described as merge-ready or
release-ready until these rows are resolved.
