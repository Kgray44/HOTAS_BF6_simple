import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Window 6.5

// A Flight Deck-native Signal Flow surface.  It shares the authoritative
// backend commands with the other experiences, but deliberately has its own
// hierarchy, card geometry, typography, and interaction treatment.
Item {
    id: root
    objectName: "flightDeckSignalFlow"
    anchors.fill: parent
    focus: true

    FlightDeckTheme { id: deck }
    property var backendObject: backend
    // Application release metadata and graphical-editor provenance are kept
    // deliberately separate. The editor is unversioned; this compact map is
    // available only in Graph Settings' Technical Details for owner review.
    readonly property var signalFlowBuildProvenance: backendObject && backendObject.signalFlowBuildProvenance
        ? backendObject.signalFlowBuildProvenance() : ({})
    // A transient return snapshot is owned by the Flight Deck shell while a
    // focused editor is open. It preserves visual context only; routing and
    // processor settings stay in the canonical backend.
    property var presentationState: ({})
    property bool presentationRestored: false
    property var graph: backendObject ? backendObject.signalFlowGraph : ({})
    property var source: ({})
    property var inspectedRoute: ({})
    // Ports are selections in their own right.  This remains a view of the
    // canonical projection, never a second endpoint/configuration model.
    property var inspectedPort: ({})
    property var inspectedPortOwner: ({})
    // Selection remembers the canonical edge under the pointer, not a
    // graph-local midpoint. It is restored with the route after a focused
    // editor round trip whenever that edge still exists.
    property string selectedSegmentId: ""
    // Deck cards select into the same truthful inspector rather than acting
    // as decorative scenery around the routing controls.
    property var inspectedNode: ({})
    property bool inspectorOpen: false
    property real inspectorPositionX: -1
    property real inspectorPositionY: -1
    property string query: ""
    property string mode: "configured"
    property string filter: "all"
    property string routeStateFilter: "all"
    property string notice: ""
    property bool noticeError: false
    property real zoom: 1.0
    property bool workspaceRestored: false
    property var workspaceAnnotations: []
    property var inspectedAnnotation: ({})
    property string blockLibraryQuery: ""
    // This is the backend's canonical insertable-type catalog. It is never
    // derived from a selected wire or from processor instances already in
    // the graph; those facts are only consulted at drop time.
    property var blockLibraryCatalog: []
    // Keep the visible palette model stable while a card is being dragged.
    // Re-evaluating a function that constructs a fresh JavaScript array made
    // the Repeater discard its own active drag delegate, which looked like a
    // block had been consumed from the Library.  This cache is presentation
    // only; it is rebuilt deliberately when its catalog, query, or graph
    // context actually changes.
    property var blockLibraryEntries: []
    property int blockLibrarySelectionIndex: 0
    // Compatibility is computed once when a processor is armed and reused by
    // the per-pointer wire hit test. The canvas never synchronously crosses
    // into C++ for every pointer sample.
    property var libraryProcessorCompatibility: ({})
    // An armed library entry is presentation state.  It can carry an existing
    // canonical endpoint, a route-qualified processor, or an annotation
    // template; it never manufactures graph-local configuration.
    property var armedLibraryEntry: ({})
    property var libraryPlacementPoint: ({ "x": 0, "y": 0 })
    // A native drag and keyboard-armed placement intentionally have different
    // completion contracts. A released drag must finish immediately; only a
    // keyboard-armed block may remain as a click-to-place ghost.
    property bool libraryDragActive: false
    property bool libraryDragDropHandled: false
    // Retain the source entry through Qt Quick's DragHandler/DropArea release
    // ordering. A palette card is never consumed by a drag.
    property var libraryDragEntry: ({})
    property real blockLibraryPositionX: -1
    property real blockLibraryPositionY: -1
    property real graphSettingsPositionX: -1
    property real graphSettingsPositionY: -1
    property var liveTelemetry: ({})
    property bool liveMode: false
    property bool signalFocus: false
    property bool reducedMotion: false
    property var routeExplanation: ({})
    property bool xrayMode: false
    property var wireGeometry: []
    // Timer::stop() prevents a future interval, but a zero-turn callback may
    // already be queued in Qt's delivery turn. Keep the request generation
    // with that callback so a superseded graph/anchor rebuild can never cross
    // a later pointer gesture.
    property int wireGeometryInvalidationEpoch: 0
    property int scheduledWireGeometryInvalidationEpoch: 0
    property var wireBuckets: ({})
    // A bucket index lets a live drag replace only the affected route segments,
    // including their Canvas hit targets, instead of reconstructing the graph.
    property var wireSegmentBucketKeys: ({})
    property var wireNodeSegmentRefs: ({})
    property var portAnchors: ({})
    // Each rendered port reports a graph-space center and a card-local offset.
    // The offset remains valid while its card moves, so a child delegate does
    // not need a synthetic x/y notification from a moving parent.
    property var portAnchorOffsets: ({})
    // Incrementing this event-driven epoch asks every live port delegate to
    // re-measure itself in scene logical coordinates.  It deliberately avoids
    // retaining delegate objects or polling them across Repeater churn.
    property int portAnchorMeasurementEpoch: 0
    // Qualification-only state.  Nothing in product UI enables it and it
    // never writes a log on a pointer frame.
    property bool portAnchorDiagnosticsEnabled: false
    property int portAnchorDiagnosticSampleCount: 0
    property int portAnchorDiagnosticFailureCount: 0
    property real portAnchorDiagnosticMaxError: 0
    property var portAnchorDiagnosticRecords: ({})
    property string hoveredRouteId: ""
    property string hoveredSegmentId: ""
    // Diagnostic-only lifecycle counters.  They make event-driven rendering
    // observable in tests without adding product logging.
    property int canvasPaintCount: 0
    // Keep the interaction contract observable. These counters are read only
    // by native qualification fixtures; they do not write logs or allocate on
    // an input frame.
    property int settledCanvasPaintCount: 0
    property int activeCanvasPaintCount: 0
    property int geometryRebuildCount: 0
    property int wireBucketFullRebuildCount: 0
    property int graphRefreshCount: 0
    property int sceneBoundsChangeCount: 0
    property int portAnchorMutationCount: 0
    property int liveDragGeometryUpdates: 0
    property int liveDragAffectedSegments: 0
    property int nodePlacementWriteCount: 0
    // A placement commit is a presentation-only mutation.  Keep its small
    // cache update observable independently from graph-wide topology work.
    property int incidentWireGeometryUpdateCount: 0
    // A moved card can become (or cease to be) an obstacle for a route that
    // does not terminate on that card.  Count those release-only cache
    // replacements separately from endpoint movement for qualification.
    property int obstacleWireGeometryUpdateCount: 0
    property int incrementalWireBucketUpdateCount: 0
    property int layoutPersistenceAcknowledgementCount: 0
    property int graphGeometryHandoffEpoch: 0
    property int scheduledGraphGeometryHandoffEpoch: 0
    // Hover is presentation-only and deliberately scoped to one port group.
    // A card-wide hover used to expand every section and force a graph-wide
    // bounds refresh; that is both visually noisy and too expensive.
    property string hoveredNodeId: ""
    property string hoveredGroupKey: ""
    property string hoveredPortId: ""
    // Each visible section owns an independent small state machine. The
    // group delegate below owns its one single-shot grace timer; no port gets
    // a timer and no hover state is polled per frame.
    property var temporaryGroupHoverStates: ({})
    // A deliberate one-second pause is enough to move across a revealed
    // section without making the graph feel like it is holding stale space.
    // The timer is still per section (never per port) and is paused while a
    // card owns a drag.
    readonly property int temporaryGroupHoverGraceMs: 1100
    // A disclosure changes only one card's rendered port anchors. Hold that
    // card's incident cache handoff separately from graph-wide invalidation.
    property var pendingSectionDisclosureReflowNodeIds: ({})
    // Keep the local-only classification alive through the last delegate
    // polish notification. Without this short settling lease, a late port
    // anchor callback after the first incident reflow would lose its context
    // and incorrectly request a whole-wire cache rebuild.
    property var activeSectionDisclosureReflowNodeIds: ({})
    // Native anchor qualification may explicitly render every port row. It
    // is test-only; ordinary graph presentation remains hover-driven.
    property bool diagnosticExpandAllNodeSections: false
    // Native interaction qualification keeps its before-release references
    // here so it can prove unchanged route records survive a layout commit.
    // Product rendering never reads this diagnostic snapshot.
    property var dropContinuityBaseline: ({})
    property int pointerHitCandidateCount: 0
    property int liveSampleCount: 0
    property var retiringWireGeometry: []
    // A dropped card switches from its inexpensive live geometry to the
    // obstacle-aware route cache.  Keep a snapshot for a brief transition so
    // that switch can morph continuously, never as erased/replaced wiring.
    property var reflowWireGeometry: []
    property var reflowWireSegmentIndex: ({})
    property bool wireReflowPending: false
    property real wireReflow: 1.0
    // An incremental release can also change a route not carried by the
    // active drag overlay. That settled layer needs the same reflow cadence.
    property bool wireReflowTouchesSettledRoutes: false
    // A freshly added route has no prior curve to morph from. Reveal only
    // that route while every surviving route transitions from its snapshot.
    property var appearingWireRouteIds: ({})
    property bool wireAppearancePending: false
    property real wireAppear: 1.0
    property real wireReveal: 1.0
    property real wireRetire: 1.0
    property var nodePositions: ({})
    // Transient positions exist only for a pointer drag.  They are never
    // serialized or sent to AppBackend; the final position is committed once
    // when the pointer is released.
    property var liveNodePositions: ({})
    property string liveDragNodeId: ""
    // A drag owns one frozen disclosure snapshot.  The snapshot makes the
    // current visible card geometry authoritative for the gesture, preventing
    // hover/policy relayout from racing the live card translation.
    property var dragDisclosureGeometry: ({})
    // A disclosure policy may have queued a card-local layout change while a
    // drag was active.  Let its final port measurements reflow only the
    // released card's incident segments; never promote that presentation
    // cleanup into a graph-wide cache rebuild.
    property string pendingDisclosureReleaseReflowNodeId: ""
    // A drop can cause a final delegate measurement on a card other than the
    // one under the pointer. Keep those anchors local to the release handoff
    // and fold their incident routes incrementally once geometry is quiet.
    property bool releaseAnchorReflowActive: false
    property var pendingReleaseAnchorReflowNodeIds: ({})
    // Retains the final live endpoint offset for the short release handoff
    // while the one allowed settled-geometry pass is being prepared.
    property string liveDragRenderNodeId: ""
    property var liveDragPosition: ({})
    property real liveDragDeltaX: 0
    property real liveDragDeltaY: 0
    // The settled canvas deliberately omits these routes while a card owns a
    // pointer. Their lightweight interaction layer then redraws only the
    // incident segments. A released card keeps its updated incident routes on
    // this overlay until a genuine topology rebuild, avoiding an ordinary-drop
    // reset of the complete settled Canvas.
    property var activeDragRouteIds: ({})
    property bool liveDragRouteSettlePending: false
    property string layoutCommitAnchorSuppressionNodeId: ""
    property var pendingNodePlacementPersistences: []
    // Pointer state belongs to the gesture, never to the persisted graph.
    // Qt Quick naturally presents property changes at its render cadence, so
    // the interaction path must not insert a second timer/event-turn throttle
    // between a native pointer event and the moving card.
    property var pendingLiveNodeDrag: ({})
    property bool dragFrameRequested: false
    property int liveDragFrameCount: 0
    property bool canvasPanActive: false
    property real canvasPanLastX: 0
    property real canvasPanLastY: 0
    // Assistive snapping is a workspace-only release decision.  The ghost and
    // guides are transient; a held card always tracks the pointer freely.
    property var nodeSnapPreview: ({})
    property bool snapDragAltBypass: false
    property var snapSettlingNodeIds: ({})
    readonly property bool snapToGridEnabled: !graph.workspace || graph.workspace.snapToGrid !== false
    readonly property real snapGridSize: 48
    readonly property real snapGridThreshold: 12
    readonly property real alignmentSnapThreshold: 10
    readonly property real alignmentSnapHysteresis: 4
    readonly property int snapSettleDuration: 100
    property var alignmentGuideHysteresis: ({})
    // Phase 4 motion language.  These semantic families intentionally avoid
    // a collection of unrelated local literals.  Reduced Motion is shortened
    // rather than erased where a concise transition still explains causality.
    readonly property int motionFastDuration: reducedMotion ? 50 : 100
    readonly property int motionStructuralDuration: reducedMotion ? 80 : 190
    readonly property int motionLayoutDuration: reducedMotion ? 120 : 320
    property bool autoLayoutMotionActive: false
    property int wireReflowDuration: motionStructuralDuration
    property var renderedNodeIds: ({})
    property var appearingNodeIds: ({})
    property real nodeAppear: 1.0
    property var arrivingDestinationPortIds: ({})
    property real routeArrival: 1.0
    property real inspectorContentReveal: 1.0
    // Presentation-only continuity for source-first learning. The canonical
    // graph remains owned by AppBackend.
    property string learnedSourcePortId: ""
    property var pendingLearnDestination: ({})
    // Phase 3 has one presentation-only routing state.  `source`, `dragWire`
    // and `connectionPreview` remain derived rendering conveniences for the
    // existing delegates; they are never canonical mapping state.
    property var interaction: ({ "mode": "IDLE", "source": ({}), "target": ({}),
        "expectedRevision": 0, "profileId": "", "rigId": "", "token": 0 })
    property int interactionToken: 0
    property bool connectionCommitInFlight: false
    readonly property bool routingActive: Boolean(interaction && interaction.mode
        && interaction.mode !== "IDLE")
    // The dot remains compact; a 28 logical-pixel target and a small
    // hysteresis envelope make dense ports reliably reachable without making
    // a near miss look like an unrelated connection.
    readonly property real magneticPortRadius: 28
    readonly property real magneticPortHysteresis: 6
    property var dragWire: ({ "active": false, "source": ({}), "x": 0, "y": 0 })
    // Source-wire dragging also performs compatibility preview work. Like
    // card dragging it applies the current native pointer sample immediately
    // and leaves presentation cadence to Qt Quick.
    property var pendingSourceDragPoint: ({})
    property bool sourceDragFrameRequested: false
    property int liveWireDragFrameCount: 0
    // A DropArea is permitted to report its drop either before or after the
    // source DragHandler becomes inactive. Keep the initiating port through
    // that handoff so a real release over a destination never loses its
    // connection request to event ordering.
    property var lastSourceDragPort: ({})
    property bool sourceDragDropHandled: false
    property var connectionPreview: ({})
    property string pendingProcessorRouteId: ""
    property string pendingProcessorSegmentId: ""
    // A processor channel is a real shared-processor membership, not a
    // graph-local wire.  The pending state merely gives the user a calm
    // one-second dwell affordance before the canonical operation is asked of
    // AppBackend.
    readonly property int processorChannelDwellMs: 1100
    property string pendingProcessorChannelNodeId: ""
    property string pendingProcessorChannelSourceId: ""
    property var pendingProcessorChannelCandidate: ({})
    property bool processorChannelCommitInFlight: false
    // Cards are the occluding objects in the graph. Wires may reach their
    // owning socket, but no unrelated route may paint across a card face.
    // The card-owned sockets deliberately mirror the wire endpoint treatment,
    // so this does not sacrifice a clear, direct connection at an endpoint.
    readonly property bool wiresOverlayCards: false
    readonly property string semanticDensity: {
        const requested = graph.workspace && graph.workspace.densityMode || "compact"
        if (zoom <= 0.62) return "overview"
        if (zoom <= 0.82 && requested === "detailed") return "compact"
        return requested
    }
    readonly property int canvasPortLimit: semanticDensity === "overview" ? 12
        : semanticDensity === "compact" ? 18 : 48
    // The scene extent is a settled-layout cache.  A source-wire pointer
    // changes only the interaction overlay; keeping a reactive bounds binding
    // here caused QML to re-run the full card-height walk for every pointer
    // sample even though its result could not affect the gesture.
    property var sceneBounds: ({ "maxX": 976, "maxY": 636 })
    readonly property real sceneLogicalWidth: Math.max(1040, Number(sceneBounds.maxX || 0) + 64)
    readonly property real sceneLogicalHeight: Math.max(700, Number(sceneBounds.maxY || 0) + 64)

    signal navigateRequested(int page, int axis, var state)

    function routeIsActiveDrag(routeId) {
        return Boolean(activeDragRouteIds[String(routeId || "")])
    }
    function hasWireOverlayRoutes() {
        return Object.keys(activeDragRouteIds || ({})).length > 0
    }
    function requestWirePaint() {
        // Never wake the settled layer for a pointer-owned card or a wire
        // preview.  Canvas otherwise clears and redraws every static route
        // (and the grid) for a local interaction.
        if (liveDragNodeId || liveDragRouteSettlePending || hasWireOverlayRoutes()
                || (dragWire && dragWire.active)) {
            if (activeDiagram) activeDiagram.requestPaint()
        } else if (diagram) {
            diagram.requestPaint()
        }
    }
    function requestSettledWirePaint() {
        if (diagram) diagram.requestPaint()
    }
    // QQmlExpression-based native tests cannot resolve a component-local id
    // directly.  Expose the rendered viewport through the page API so the
    // test continues to drive the actual Flickable rather than a helper.
    function graphViewportForTest() { return graphViewport }

    function normalized(value) { return String(value || "").toLowerCase() }
    function nodeMotionDuration(nodeData) {
        if (autoLayoutMotionActive) return motionLayoutDuration
        // Direct manipulation owns its card position and must keep its cached
        // wire endpoints exact. Only a release-time snap receives a short
        // settle; an ordinary drag release commits without a second drift.
        return nodeIsSnapSettling(nodeData) ? motionFastDuration : 0
    }
    function routeDestinationKey(route) {
        return String(route && (route.destinationEndpointId || route.destinationPortId) || "")
    }
    function routeDestinationAcknowledged(port) {
        const key = String(port && (port.endpointId || port.id) || "")
        return key.length > 0 && Boolean(arrivingDestinationPortIds[key])
    }
    function nodeIsAppearing(nodeData) {
        return Boolean(appearingNodeIds[nodeIdentity(nodeData)])
            || Boolean(appearingNodeIds[nodeStorageIdentity(nodeData)])
    }
    function nodeAppearanceOpacity(nodeData) {
        return nodeIsAppearing(nodeData) ? 0.84 + 0.16 * nodeAppear : 1.0
    }
    function beginRouteDestinationAcknowledgement() {
        if (Object.keys(arrivingDestinationPortIds).length === 0) return
        routeArrival = 0
        routeArrivalAnimation.restart()
    }
    function restartInspectorMotion() {
        inspectorContentReveal = 0
        inspectorContentAnimation.restart()
    }
    function node(kind) {
        const nodes = graph.nodes || []
        for (let i = 0; i < nodes.length; ++i) if (nodes[i].kind === kind) return nodes[i]
        return ({})
    }
    function routeForId(id) {
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index)
            if (String(routes[index].id || "") === String(id || "")) return routes[index]
        return ({})
    }
    function axisForRoute(route) {
        if (!route || String(route.kind || "") !== "axis") return -1
        if (route.sourceIndex !== undefined) return Number(route.sourceIndex)
        const match = String(route.sourcePortId || "").match(/(?:^|\|)axis:(\d+)$/)
        return match ? Number(match[1]) : -1
    }
    function routeForProcessorNode(nodeData) {
        if (!nodeData || String(nodeData.kind || "") !== "processor") return ({})
        const processorId = String(nodeData.objectId || "")
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const details = routes[index].processorDetails || []
            for (let detailIndex = 0; detailIndex < details.length; ++detailIndex) {
                if (String(details[detailIndex].id || "") === processorId) return routes[index]
            }
        }
        return ({})
    }
    function routeForSourcePort(port) {
        const sourceEndpoint = String(port && (port.endpointId || port.id) || "")
        const sourceId = String(port && port.id || "")
        const sourceNodeId = String(port && port.ownerNodeId || "")
        const sourceKind = String(port && (port.kind || port.signalKind) || "")
        const sourceIndex = Number(port && port.index)
        const sourceSubIndex = Number(port && (port.subIndex === undefined ? -1 : port.subIndex))
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index] || ({})
            if (!route.enabled || String(route.kind || "") !== "axis") continue
            if (String(route.sourceEndpointId || "") === sourceEndpoint
                    || String(route.sourcePortId || "") === sourceId
                    // Multi-device projections use a scoped source-port ID.
                    // Match its canonical node/kind/index tuple too, so a
                    // real graph-dot input always resolves its current route.
                    || (sourceNodeId.length > 0
                        && String(route.sourceNodeId || "") === sourceNodeId
                        && String(route.kind || "") === sourceKind
                        && Number(route.sourceIndex) === sourceIndex
                        && Number(route.sourceSubIndex === undefined ? -1 : route.sourceSubIndex)
                            === sourceSubIndex)) return route
        }
        return ({})
    }
    // A processor responds as soon as a real source connection gesture is
    // armed.  Visual dwell feedback must not be hidden behind the later
    // canonical-share preflight: otherwise a valid user gesture appears to do
    // nothing whenever the backend needs to reject its eventual mutation.
    function processorChannelPreviewCandidate(nodeData) {
        const sourcePort = interactionSource()
        if (!sourcePort || !sourcePort.id || !nodeData || nodeData.kind !== "processor"
                || mode !== "configured" || !routingActive || !routingContextIsCurrent()) return null
        const sourceRoute = routeForSourcePort(sourcePort)
        const targetRoute = routeForProcessorNode(nodeData)
        const semantic = String(nodeData.semantic || "")
        if (!semantic.length) return null
        return ({ "source": sourcePort, "sourceRoute": sourceRoute,
            "targetRoute": targetRoute, "node": nodeData, "semantic": semantic })
    }
    function processorChannelCandidate(nodeData) {
        const preview = processorChannelPreviewCandidate(nodeData)
        if (!preview || !preview.sourceRoute.id || !preview.targetRoute.id
                || String(preview.sourceRoute.id) === String(preview.targetRoute.id)) return null
        const routeIds = []
        const seen = ({})
        const addRoute = function(routeId) {
            const id = String(routeId || "")
            if (!id || seen[id]) return
            seen[id] = true
            routeIds.push(id)
        }
        // The processor's current channels retain their shared identity and
        // source settings; the newly armed source is appended as one real
        // channel, never displayed as an ornamental socket.
        addRoute(preview.targetRoute.id)
        const ports = preview.node.ports || []
        for (let index = 0; index < ports.length; ++index) addRoute(ports[index].routeId)
        addRoute(preview.sourceRoute.id)
        return routeIds.length >= 2 ? ({ "source": preview.source, "sourceRoute": preview.sourceRoute,
            "targetRoute": preview.targetRoute, "node": preview.node, "semantic": preview.semantic,
            "routeIds": routeIds }) : null
    }
    function processorChannelDwellActive(nodeData) {
        return Boolean(nodeData && nodeData.id && pendingProcessorChannelNodeId.length > 0
            && String(pendingProcessorChannelNodeId) === String(nodeData.id))
    }
    function clearProcessorChannelDwell(nodeData) {
        const nodeId = String(nodeData && nodeData.id || nodeData || "")
        if (nodeId.length > 0 && String(pendingProcessorChannelNodeId) !== nodeId) return false
        processorChannelDwellTimer.stop()
        pendingProcessorChannelNodeId = ""
        pendingProcessorChannelSourceId = ""
        pendingProcessorChannelCandidate = ({})
        return true
    }
    function beginProcessorChannelDwell(nodeData) {
        const candidate = processorChannelPreviewCandidate(nodeData)
        if (!candidate) {
            clearProcessorChannelDwell()
            return false
        }
        const nextNodeId = String(nodeData.id || "")
        const nextSourceId = String(candidate.source.id || "")
        if (pendingProcessorChannelNodeId === nextNodeId && pendingProcessorChannelSourceId === nextSourceId)
            return true
        pendingProcessorChannelNodeId = nextNodeId
        pendingProcessorChannelSourceId = nextSourceId
        pendingProcessorChannelCandidate = candidate
        processorChannelDwellTimer.restart()
        notice = "Hold over " + String(nodeData.label || "processor")
            + " for one second to add the highlighted channel pair."
        noticeError = false
        return true
    }
    function processorNodeAt(point) {
        if (!point) return null
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            const candidate = nodes[index]
            if (String(candidate.kind || "") !== "processor") continue
            const position = nodePosition(candidate, 680, 120)
            if (Number(point.x) >= Number(position.x) && Number(point.x) <= Number(position.x) + graphCardWidth(candidate)
                    && Number(point.y) >= Number(position.y) && Number(point.y) <= Number(position.y) + graphCardHeight(candidate))
                return candidate
        }
        return null
    }
    function updateProcessorChannelDwellAt(point) {
        const candidate = processorNodeAt(point)
        if (candidate) return beginProcessorChannelDwell(candidate)
        return clearProcessorChannelDwell()
    }
    function commitProcessorChannelDwell() {
        const node = nodeForId(pendingProcessorChannelNodeId)
        const candidate = processorChannelCandidate(node)
        if (!candidate || String(candidate.source.id || "") !== String(pendingProcessorChannelSourceId || "")) {
            clearProcessorChannelDwell()
            return false
        }
        connectionCommitInFlight = true
        processorChannelCommitInFlight = true
        const result = backendObject.signalFlowShareProcessor(candidate.routeIds, candidate.semantic,
            Number(graph.revision || 0))
        processorChannelCommitInFlight = false
        connectionCommitInFlight = false
        announce(result, "Processor channel was not added.")
        if (!result || !result.success) {
            clearProcessorChannelDwell()
            return false
        }
        // The backend has emitted the rebuilt canonical projection. The
        // original source leg is now attached to a newly materialized pair on
        // this processor, so there is no second graph-local drop to commit.
        sourceDragDropHandled = true
        clearProcessorChannelDwell()
        cancelRouting("", false)
        notice = "Added a canonical channel pair to " + String(node.label || "the processor") + "."
        noticeError = false
        return true
    }
    function routeHasProcessor(route, semantic) {
        const details = route && route.processorDetails ? route.processorDetails : []
        return details.some(function(detail) { return String(detail.semantic || "") === semantic })
    }
    function capturePresentationState() {
        return ({
            "version": 1,
            "inspectedRouteId": String(inspectedRoute && inspectedRoute.id || ""),
            "inspectedPortId": String(inspectedPort && (inspectedPort.endpointId || inspectedPort.id) || ""),
            "selectedSegmentId": selectedSegmentId,
            "inspectedNodeId": String(inspectedNode && inspectedNode.id || ""),
            "inspectorOpen": inspectorOpen,
            "sourceId": String(source && source.id || ""),
            "mode": mode,
            "filter": filter,
            "routeStateFilter": routeStateFilter,
            "query": query,
            "signalFocus": signalFocus,
            "liveMode": liveMode,
            "xrayMode": xrayMode,
            "reducedMotion": reducedMotion,
            "zoom": zoom,
            "panX": graphViewport ? graphViewport.contentX : 0,
            "panY": graphViewport ? graphViewport.contentY : 0
        })
    }
    function restorePresentationState() {
        if (presentationRestored || !presentationState || Number(presentationState.version || 0) !== 1)
            return false
        presentationRestored = true
        mode = presentationState.mode === "effective" ? "effective" : "configured"
        filter = presentationState.filter || "all"
        routeStateFilter = presentationState.routeStateFilter || "all"
        query = presentationState.query || ""
        signalFocus = Boolean(presentationState.signalFocus)
        liveMode = Boolean(presentationState.liveMode)
        xrayMode = Boolean(presentationState.xrayMode)
        reducedMotion = Boolean(presentationState.reducedMotion)
        if (isFinite(Number(presentationState.zoom))) zoom = Number(presentationState.zoom)
        if (graphViewport) {
            graphViewport.contentX = Math.max(0, Number(presentationState.panX || 0))
            graphViewport.contentY = Math.max(0, Number(presentationState.panY || 0))
        }
        const restoredRoute = routeForId(presentationState.inspectedRouteId)
        if (restoredRoute && restoredRoute.id) {
            inspectedRoute = restoredRoute
            selectedSegmentId = String(presentationState.selectedSegmentId || "")
            inspectedNode = ({})
            source = ({})
            notice = "Returned to the selected Signal Flow route."
            noticeError = false
            return true
        }
        inspectorOpen = Boolean(presentationState.inspectorOpen)
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            if (String(nodes[index].id || "") === String(presentationState.inspectedNodeId || "")) {
                inspectedNode = nodes[index]
                inspectedRoute = ({})
                source = ({})
                return true
            }
        }
        const inputPorts = ports("input")
        for (let index = 0; index < inputPorts.length; ++index) {
            if (String(inputPorts[index].id || "") === String(presentationState.sourceId || "")) {
                source = inputPorts[index]
                inspectedRoute = ({})
                inspectedNode = ({})
                return true
            }
        }
        return true
    }
    function openFullSettings(semantic, routeCandidate) {
        const route = routeCandidate && routeCandidate.id ? routeCandidate
            : inspectedRoute && inspectedRoute.id ? inspectedRoute
            : routeForProcessorNode(inspectedNode)
        const axis = axisForRoute(route)
        if (axis < 0 || axis >= 8) {
            notice = "Full settings are available only for an axis route. Select the Curve or Adaptive Response processor again."
            noticeError = true
            return false
        }
        const normalized = String(semantic || "").toLowerCase()
        const page = normalized === "curve" ? 6 : normalized === "adaptive-response" ? 9 : 0
        if (route && route.controllerRecordId) backendObject.setActiveController(String(route.controllerRecordId))
        backendObject.setSelectedAxis(axis)
        const state = capturePresentationState()
        notice = normalized === "adaptive-response"
            ? "Opening Adaptive Response for this same source axis."
            : normalized === "curve" ? "Opening Curve Editor for this same source axis."
            : "Opening the focused axis settings for this source."
        noticeError = false
        navigateRequested(page, axis, state)
        return true
    }
    function openNodeSettings(nodeData) {
        if (!nodeData || nodeData.kind !== "processor") return false
        if (nodeData.semantic !== "curve" && nodeData.semantic !== "adaptive-response") {
            notice = "This processor is summarized here. Its focused settings remain on the Axes page."
            noticeError = false
            return false
        }
        return openFullSettings(nodeData.semantic, routeForProcessorNode(nodeData))
    }
    function openCardSettings(nodeData) {
        if (!nodeData || (nodeData.kind !== "input" && nodeData.kind !== "output")) return false
        const state = capturePresentationState()
        notice = nodeData.kind === "input"
            ? "Opening Devices & setup for this physical input context."
            : "Opening Devices & setup for this virtual output context."
        noticeError = false
        navigateRequested(2, -1, state)
        return true
    }
    function ports(kind) { return node(kind).ports || [] }
    function inputNodes() { return (graph.nodes || []).filter(function(item) { return item.kind === "input" }) }
    function portsForNode(nodeData) { return nodeData && nodeData.ports ? nodeData.ports : [] }
    function routesForPort(port, isOutput) {
        const matches = []
        const routes = graph.routes || []
        for (let i = 0; i < routes.length; ++i) {
            const route = routes[i]
            const endpoint = isOutput ? route.destinationEndpointId : route.sourceEndpointId
            const legacy = isOutput ? route.destinationPortId : route.sourcePortId
            if ((port.endpointId && String(endpoint || "") === String(port.endpointId)) || legacy === port.id)
                matches.push(route)
        }
        return matches
    }
    function portOwner(port) {
        if (!port) return ({})
        const ownerId = String(port.ownerNodeId || "")
        const candidates = graph.nodes || []
        for (let index = 0; index < candidates.length; ++index) {
            const candidate = candidates[index]
            if (ownerId && nodeMatchesId(candidate, ownerId)) return candidate
            const ports = candidate.ports || []
            for (let portIndex = 0; portIndex < ports.length; ++portIndex) {
                if (String(ports[portIndex].endpointId || ports[portIndex].id || "")
                        === String(port.endpointId || port.id || "")) return candidate
            }
        }
        return ({})
    }
    function portIsOutput(port) {
        const owner = portOwner(port)
        return Boolean(owner && owner.kind === "output") || String(port && port.direction || "") === "output"
    }
    function inspectPort(port, owner) {
        if (!port || !(port.endpointId || port.id)) return false
        if (routingActive) cancelRouting("", false)
        inspectedPort = port
        inspectedPortOwner = owner && owner.id ? owner : portOwner(port)
        inspectedRoute = ({})
        inspectedNode = ({})
        source = ({})
        inspectorOpen = true
        ensureInspectorPosition()
        notice = (port.label || "Signal Flow port") + " selected."
        noticeError = false
        return true
    }
    function portTopologySummary(port) {
        if (!port) return "No port selected."
        const routes = routesForPort(port, portIsOutput(port))
        if (routes.length === 0) return "UNCONNECTED · no canonical route"
        return routes.length + " canonical route" + (routes.length === 1 ? "" : "s")
    }
    function portHoverSummary(port, destination) {
        if (!port) return "Signal Flow port"
        const owner = portOwner(port)
        const direction = destination ? "Output" : "Input"
        const health = port.available ? "configuration valid" : "offline / unavailable"
        return String(owner.label || "Signal Flow") + "\n" + String(port.label || port.technicalLabel || "Port")
            + "\n" + String(port.kind || "other").toUpperCase() + " · " + direction
            + "\n" + portTopologySummary(port) + " · " + health
    }
    function routeHasProblem(route) { return String(route.health || "ready") !== "ready" }
    function portMatchesState(port, isOutput, state) {
        const normalizedState = String(state || routeStateFilter || "all")
        if (normalizedState === "all") return true
        const routes = routesForPort(port, isOutput)
        if (normalizedState === "mapped") return Boolean(port.mapped)
        if (normalizedState === "unmapped") return !port.mapped
        if (normalizedState === "problems") return routes.some(function(route) { return root.routeHasProblem(route) })
        if (normalizedState === "active") return routes.some(function(route) { return root.routeIsLive(route) })
        return true
    }
    function semanticQueryMatchesPort(port, isOutput, query) {
        if (query === "mapped" || query === "unmapped" || query === "problems" || query === "active")
            return portMatchesState(port, isOutput, query)
        return false
    }
    function matching(port, isOutput) {
        if (filter !== "all" && port.kind !== filter) return false
        const needle = normalized(query)
        if (!portMatchesState(port, isOutput)) return false
        if (needle.length > 0 && !semanticQueryMatchesPort(port, isOutput, needle)
                && normalized(port.label).indexOf(needle) < 0 && normalized(port.technicalLabel).indexOf(needle) < 0) return false
        return true
    }
    function destinationFor(port) {
        if (port.kind === "axis") return String(port.technicalLabel)
        if (port.kind === "native-pov") return "native-pov:" + String(port.subIndex) + ":" + String(port.index)
        return String(port.index)
    }
    function groups(kind) { return node(kind).portGroups || [] }
    function groupCard(nodeOrKind) {
        return typeof nodeOrKind === "string" ? node(nodeOrKind) : (nodeOrKind || ({}))
    }
    function cardGroups(nodeData) {
        const states = nodeData && nodeData.portGroups ? nodeData.portGroups : []
        if (states.length > 0) return states
        const seen = ({})
        let result = []
        const cardPorts = portsForNode(nodeData)
        for (let index = 0; index < cardPorts.length; ++index) {
            const group = String(cardPorts[index].group || "Controls")
            if (seen[group]) continue
            seen[group] = true
            result.push({ "group": group, "collapsed": group !== "Axes" && group !== "Virtual Axes" })
        }
        return result
    }
    function cardGroupStoredCollapsed(nodeData, group) {
        const states = cardGroups(nodeData)
        for (let index = 0; index < states.length; ++index)
            if (String(states[index].group || "") === String(group || "")) return Boolean(states[index].collapsed)
        return String(group || "") !== "Axes" && String(group || "") !== "Virtual Axes"
    }
    function cardGroupHasExplicitState(nodeData, group) {
        const states = cardGroups(nodeData)
        for (let index = 0; index < states.length; ++index)
            if (String(states[index].group || "") === String(group || ""))
                return Boolean(states[index].explicit)
        return false
    }
    function cardGroupRouteCount(nodeData, group, destination) {
        const seen = ({})
        const cardPorts = portsForNode(nodeData)
        for (let index = 0; index < cardPorts.length; ++index) {
            const port = cardPorts[index]
            if (String(port.group || "") !== String(group || "")) continue
            const matches = routesForPort(port, destination)
            for (let routeIndex = 0; routeIndex < matches.length; ++routeIndex)
                seen[String(matches[routeIndex].id || routeIndex)] = true
        }
        return Object.keys(seen).length
    }
    function cardGroupNeedsAttention(nodeData, group, destination) {
        const cardPorts = portsForNode(nodeData)
        if (normalized(query).length > 0 || filter !== "all" || routeStateFilter !== "all") return true
        if (String(learnedSourcePortId || "").length > 0 || (inspectedNode && String(inspectedNode.id || "") === String(nodeData.id || ""))) return true
        for (let index = 0; index < cardPorts.length; ++index) {
            const port = cardPorts[index]
            if (String(port.group || "") !== String(group || "")) continue
            if (source && source.id && (!destination
                    ? String(port.endpointId || port.id) === String(source.endpointId || source.id)
                    : compatible(port))) return true
            if (inspectedRoute && inspectedRoute.id && routesForPort(port, destination).some(function(route) {
                    return String(route.id || "") === String(inspectedRoute.id || "") })) return true
        }
        return false
    }
    function cardGroupKey(nodeData, group) {
        return nodeIdentity(nodeData) + "|" + String(group || "")
    }
    function temporaryGroupStateForKey(key) {
        const state = temporaryGroupHoverStates && temporaryGroupHoverStates[String(key || "")]
        return state || ({ "state": "COLLAPSED", "inside": false })
    }
    function temporaryGroupState(nodeData, group) {
        return temporaryGroupStateForKey(cardGroupKey(nodeData, group))
    }
    function setTemporaryGroupState(key, state) {
        const next = ({})
        for (const existingKey in temporaryGroupHoverStates)
            next[existingKey] = temporaryGroupHoverStates[existingKey]
        next[String(key || "")] = state
        temporaryGroupHoverStates = next
    }
    function temporaryHoverExpansionEnabled() {
        return Boolean(graph.workspace && graph.workspace.autoExpandPorts !== false)
    }
    function cardGroupIsHovered(nodeData, group) {
        return Boolean(temporaryGroupState(nodeData, group).inside)
    }
    function cardGroupTemporarilyOpen(nodeData, group) {
        if (!temporaryHoverExpansionEnabled()) return false
        const state = temporaryGroupState(nodeData, group)
        return state.state === "TEMP_OPEN" || state.state === "TEMP_OPEN_GRACE"
    }
    function cardGroupHoverGraceActive(nodeData, group) {
        const state = temporaryGroupState(nodeData, group)
        if (!temporaryHoverExpansionEnabled() || liveDragNodeId || state.inside
                || state.state !== "TEMP_OPEN_GRACE") return false
        // An explicit open never receives temporary collapse treatment.
        return !(cardGroupHasExplicitState(nodeData, group)
            && !cardGroupStoredCollapsed(nodeData, group))
    }
    function queueSectionDisclosureReflowForNodeId(nodeId) {
        const id = String(nodeId || "")
        if (!id || liveDragNodeId) return false
        const pending = ({})
        const active = ({})
        for (const key in pendingSectionDisclosureReflowNodeIds)
            pending[key] = pendingSectionDisclosureReflowNodeIds[key]
        for (const key in activeSectionDisclosureReflowNodeIds)
            active[key] = activeSectionDisclosureReflowNodeIds[key]
        pending[id] = true
        active[id] = true
        pendingSectionDisclosureReflowNodeIds = pending
        activeSectionDisclosureReflowNodeIds = active
        sectionDisclosureReflowTimer.restart()
        sectionDisclosureReflowSettlingTimer.restart()
        return true
    }
    function sectionDisclosureReflowActiveForNodeId(nodeId) {
        const id = String(nodeId || "")
        const normalized = nodeIdentity(nodeForId(id))
        return Boolean(pendingSectionDisclosureReflowNodeIds[id]
            || pendingSectionDisclosureReflowNodeIds[normalized]
            || activeSectionDisclosureReflowNodeIds[id]
            || activeSectionDisclosureReflowNodeIds[normalized])
    }
    function scheduleSectionDisclosureReflow(nodeData) {
        const nodeId = nodeIdentity(nodeData)
        if (!nodeId || liveDragNodeId) return false
        return queueSectionDisclosureReflowForNodeId(nodeId)
    }
    function setGroupHovered(nodeData, group, hovered) {
        if (liveDragNodeId || (dragWire && dragWire.active)) return false
        if (!temporaryHoverExpansionEnabled()) return false
        const key = cardGroupKey(nodeData, group)
        if (!key) return false
        const current = temporaryGroupStateForKey(key)
        // Ignore a synthetic or duplicate leave before this section has ever
        // been opened.  Only a real TEMP_OPEN state can begin the grace
        // period; otherwise an initial HoverHandler delivery would create an
        // unearned temporary reveal.
        if (Boolean(current.inside) === Boolean(hovered)
                && (hovered || current.state === "TEMP_OPEN_GRACE"
                    || current.state === "COLLAPSED")) return false
        // Arm the incident-only reflow *before* changing the state. QML may
        // create or destroy a revealed port delegate synchronously as this
        // assignment re-evaluates the group Repeater.
        scheduleSectionDisclosureReflow(nodeData)
        const next = ({ "state": hovered ? "TEMP_OPEN" : "TEMP_OPEN_GRACE",
            "inside": Boolean(hovered) })
        setTemporaryGroupState(key, next)
        hoveredGroupKey = hovered ? key
            : (String(hoveredGroupKey || "") === key ? "" : hoveredGroupKey)
        // Group delegates own their height transition and anchor remeasurement.
        // The pre-armed reflow timer updates only this card's incident geometry.
        return true
    }
    function completeGroupHoverGrace(nodeData, group) {
        if (liveDragNodeId || (dragWire && dragWire.active)) return false
        const key = cardGroupKey(nodeData, group)
        const current = temporaryGroupStateForKey(key)
        if (current.inside || current.state !== "TEMP_OPEN_GRACE") return false
        scheduleSectionDisclosureReflow(nodeData)
        setTemporaryGroupState(key, ({ "state": "COLLAPSED", "inside": false }))
        return true
    }
    function clearTemporaryGroupState(nodeData, group) {
        const key = cardGroupKey(nodeData, group)
        if (!key) return false
        setTemporaryGroupState(key, ({ "state": "COLLAPSED", "inside": false }))
        return true
    }
    function cardIsHovered(nodeData) {
        const id = nodeIdentity(nodeData)
        return id.length > 0 && String(hoveredNodeId || "") === id
    }
    function cardKeepsActiveWireSource(nodeData) {
        const activeSource = dragWire && dragWire.active ? dragWire.source : ({});
        const ownerId = portOwnerNodeId(activeSource)
        return ownerId.length > 0 && nodeMatchesId(nodeData, ownerId)
    }
    function cardIsExpanded(nodeData) {
        return diagnosticExpandAllNodeSections || cardKeepsActiveWireSource(nodeData)
    }
    function setNodeHovered(nodeData, hovered) {
        // Keep the card that owns a drag stable. Hover changes to cards under
        // it would otherwise resize unrelated port sections mid-gesture.
        if (liveDragNodeId) return false
        const id = nodeIdentity(nodeData)
        if (!id) return false
        const next = hovered ? id : (String(hoveredNodeId || "") === id ? "" : hoveredNodeId)
        if (String(hoveredNodeId || "") === String(next || "")) return false
        hoveredNodeId = String(next || "")
        return true
    }
    function frozenDisclosureFor(nodeData) {
        const frozen = dragDisclosureGeometry || ({})
        return frozen.nodeId && nodeMatchesId(nodeData, frozen.nodeId) ? frozen : null
    }
    function freezeDisclosureGeometry(nodeData) {
        const id = nodeIdentity(nodeData)
        if (!id) return false
        const collapsed = ({})
        const destination = String(nodeData.kind || "") === "output"
        const states = cardGroups(nodeData)
        for (let index = 0; index < states.length; ++index) {
            const group = String(states[index].group || "")
            collapsed[group] = cardGroupCollapsed(nodeData, group, destination)
        }
        const offsets = ({})
        const anchors = ({})
        for (const key in portAnchorOffsets) offsets[key] = portAnchorOffsets[key]
        for (const key in portAnchors) anchors[key] = portAnchors[key]
        dragDisclosureGeometry = ({ "nodeId": id, "collapsed": collapsed,
            "portAnchorOffsets": offsets, "portAnchors": anchors })
        return true
    }
    function releaseDisclosureGeometry(nodeData, reflowIncrementally) {
        const frozen = frozenDisclosureFor(nodeData)
        if (!frozen) return false
        // Explicit state was never touched. Re-measure on the next stable
        // turn. A normal drop then updates only this card's incident segments
        // from those rendered centers. Cancel/fixture callers retain their
        // established full-rebuild handoff below.
        if (reflowIncrementally !== false)
            pendingDisclosureReleaseReflowNodeId = nodeIdentity(nodeData)
        // Clearing the frozen disclosure can destroy or create port delegates
        // synchronously. Mark the local reflow first so those delegate
        // callbacks cannot queue the global geometry timer in that gap.
        dragDisclosureGeometry = ({})
        requestPortAnchorMeasurement()
        if (reflowIncrementally !== false) disclosureReleaseReflowTimer.restart()
        return true
    }
    function queueReleaseAnchorReflow(ownerId) {
        if (!releaseAnchorReflowActive) return false
        const owner = nodeForId(ownerId)
        const nodeId = nodeIdentity(owner)
        if (!nodeId) return false
        const next = ({})
        for (const key in pendingReleaseAnchorReflowNodeIds) next[key] = true
        next[nodeId] = true
        pendingReleaseAnchorReflowNodeIds = next
        disclosureReleaseReflowTimer.restart()
        return true
    }
    function cardGroupCollapsed(nodeData, group, destination) {
        const frozen = frozenDisclosureFor(nodeData)
        if (frozen && frozen.collapsed && frozen.collapsed[String(group || "")] !== undefined)
            return Boolean(frozen.collapsed[String(group || "")])
        // Explicit open remains authoritative. An explicit closed section can
        // still receive a temporary hover reveal, but returns to that chosen
        // closed state when its grace period ends.
        const taskReveal = cardGroupNeedsAttention(nodeData, group, destination)
        const explicit = cardGroupHasExplicitState(nodeData, group)
        const explicitlyCollapsed = cardGroupStoredCollapsed(nodeData, group)
        const temporaryOpen = cardGroupTemporarilyOpen(nodeData, group)
        if (explicit && !explicitlyCollapsed) return false
        if (explicit && explicitlyCollapsed) return !temporaryOpen
        if (diagnosticExpandAllNodeSections || taskReveal || cardKeepsActiveWireSource(nodeData)
                || temporaryOpen) return false
        const policy = String(graph.workspace && graph.workspace.portVisibility || "smart")
        if (policy === "expanded") return false
        if (policy === "compact") return true
        // Smart and Connected Only render connected rows as a compact summary
        // below the collapsed disclosure. They never hide useful topology.
        return true
    }
    function cardGroupShowsConnectedPorts(nodeData, group, destination) {
        if (!cardGroupCollapsed(nodeData, group, destination)
                || cardGroupHasExplicitState(nodeData, group)) return false
        const policy = String(graph.workspace && graph.workspace.portVisibility || "smart")
        return (policy === "smart" || policy === "connected")
            && cardGroupRouteCount(nodeData, group, destination) > 0
    }
    function cardGroupConnectedPorts(nodeData, group, destination) {
        return cardGroupPorts(nodeData, group, destination).filter(function(port) {
            return routesForPort(port, destination).length > 0
        })
    }
    function cardGroupPorts(nodeData, group, destination) {
        let result = portsForNode(nodeData).filter(function(port) {
            return String(port.group || "") === String(group || "") && matching(port, destination)
        })
        const limit = semanticDensity === "overview" ? 3 : semanticDensity === "compact" ? 10 : 14
        return result.slice(0, limit)
    }
    function groupCollapsed(kind, group) { return cardGroupCollapsed(node(kind), group, kind === "output") }
    function groupPorts(kind, group, limit) { return cardGroupPorts(node(kind), group, kind === "output").slice(0, limit || 48) }
    function setGroup(nodeOrKind, group, collapsed) {
        const card = groupCard(nodeOrKind)
        clearTemporaryGroupState(card, group)
        scheduleSectionDisclosureReflow(card)
        announce(backendObject.signalFlowSetPortGroupCollapsed(String(card.objectId || card.id || ""), group, collapsed),
            "Port group state was not saved.")
    }
    function setAllGroups(nodeData, collapsed) {
        const card = groupCard(nodeData)
        const groups = cardGroups(card)
        let changed = false
        for (let index = 0; index < groups.length; ++index) {
            const result = backendObject.signalFlowSetPortGroupCollapsed(
                String(card.objectId || card.id || ""), String(groups[index].group || ""), collapsed)
            changed = Boolean(result && result.success) || changed
        }
        if (changed) graph = backendObject.signalFlowGraph
        return changed
    }
    function sourcePorts() {
        const all = []
        const cards = inputNodes()
        for (let cardIndex = 0; cardIndex < cards.length; ++cardIndex) {
            const cardPorts = portsForNode(cards[cardIndex])
            for (let portIndex = 0; portIndex < cardPorts.length; ++portIndex) {
                if (matching(cardPorts[portIndex], false)) all.push(cardPorts[portIndex])
            }
        }
        return all.slice(0, 96)
    }
    // Port-group collapse is a visual density choice. Routing helpers must
    // continue to enumerate every compatible canonical endpoint, including
    // a destination card that will expand as the pointer reaches it.
    function destinationPorts() { return ports("output").filter(function(port) { return matching(port, true) }) }
    function visibleCardPorts(nodeData, destination) {
        let result = []
        const state = cardGroups(nodeData)
        for (let index = 0; index < state.length; ++index) {
            const group = String(state[index].group || "")
            if (!cardGroupCollapsed(nodeData, group, destination))
                result = result.concat(cardGroupPorts(nodeData, group, destination))
            else if (cardGroupShowsConnectedPorts(nodeData, group, destination))
                result = result.concat(cardGroupConnectedPorts(nodeData, group, destination))
        }
        return result
    }
    function processorChannelPairs(nodeData) {
        const pairsByRoute = ({})
        const order = []
        const ports = portsForNode(nodeData)
        for (let index = 0; index < ports.length; ++index) {
            const port = ports[index] || ({})
            const routeId = String(port.routeId || port.routeSegmentId || index)
            if (!pairsByRoute[routeId]) {
                pairsByRoute[routeId] = ({ "routeId": routeId, "input": ({}), "output": ({}) })
                order.push(routeId)
            }
            if (String(port.direction || "") === "input") pairsByRoute[routeId].input = port
            else pairsByRoute[routeId].output = port
        }
        return order.map(function(routeId) { return pairsByRoute[routeId] })
    }
    function processorChannelPairForPort(nodeData, port) {
        const endpoint = String(port && (port.endpointId || port.id) || "")
        const pairs = processorChannelPairs(nodeData)
        for (let index = 0; index < pairs.length; ++index) {
            if (String(pairs[index].input && (pairs[index].input.endpointId || pairs[index].input.id) || "") === endpoint
                    || String(pairs[index].output && (pairs[index].output.endpointId || pairs[index].output.id) || "") === endpoint)
                return index
        }
        return 0
    }
    function processorDisplayChannelPairs(nodeData) {
        const pairs = processorChannelPairs(nodeData).slice()
        const candidate = pendingProcessorChannelCandidate || ({})
        if (processorChannelDwellActive(nodeData)
                && String(candidate.node && candidate.node.id || "") === String(nodeData && nodeData.id || "")) {
            // This is an explicit, short-lived presentation preview of the
            // exact canonical membership request. It gives the user an
            // immediately legible new IN/OUT pair while the one-second dwell
            // is completing; it is never persisted or treated as a route.
            pairs.push({ "pending": true, "input": ({ "label": "NEW INPUT" }),
                "output": ({ "label": "NEW OUTPUT" }) })
        }
        return pairs
    }
    function processorPendingPairIndex(nodeData) {
        const candidate = pendingProcessorChannelCandidate || ({})
        if (!processorChannelDwellActive(nodeData)
                || String(candidate.node && candidate.node.id || "") !== String(nodeData && nodeData.id || ""))
            return -1
        return processorChannelPairs(nodeData).length
    }
    function processorPendingInputCenter(nodeData) {
        const pairIndex = processorPendingPairIndex(nodeData)
        if (pairIndex < 0) return ({})
        const position = nodePosition(nodeData, 680, 120)
        return ({ "x": Number(position.x || 0),
            "y": Number(position.y || 0) + deck.cardPaddingCompact + 43 + pairIndex * 28 })
    }
    function graphCardWidth(nodeData) { return nodeData && nodeData.kind === "processor" ? 254 : 294 }
    function graphCardHeight(nodeData) {
        if (!nodeData) return 96
        if (nodeData.kind === "processor") {
            const channels = Math.max(1, processorDisplayChannelPairs(nodeData).length,
                Number(nodeData.channelCount || 0))
            return 104 + channels * 28
        }
        const destination = nodeData.kind === "output"
        const state = cardGroups(nodeData)
        let rows = 0
        let collapsed = 0
        for (let index = 0; index < state.length; ++index) {
            const group = String(state[index].group || "")
            if (cardGroupCollapsed(nodeData, group, destination)) {
                ++collapsed
                if (cardGroupShowsConnectedPorts(nodeData, group, destination))
                    rows += cardGroupConnectedPorts(nodeData, group, destination).length
            }
            else rows += cardGroupPorts(nodeData, group, destination).length
        }
        return 76 + state.length * 22 + rows * 28 + collapsed * 14
    }
    function graphLayoutBounds() {
        const nodes = graph.nodes || []
        let maxX = 976
        let maxY = 636
        for (let index = 0; index < nodes.length; ++index) {
            const nodeData = nodes[index]
            // Viewport extent is a settled-layout concern.  Making it depend
            // on a transient pointer position invalidates scene dimensions
            // and both canvas textures for every drag sample.
            const position = settledNodePosition(nodeData,
                nodeData.kind === "output" ? 1320 : nodeData.kind === "processor" ? 680 : 80, 120)
            maxX = Math.max(maxX, Number(position.x || 0) + graphCardWidth(nodeData))
            maxY = Math.max(maxY, Number(position.y || 0) + graphCardHeight(nodeData))
        }
        return ({ "maxX": maxX, "maxY": maxY })
    }
    function refreshSceneBounds() {
        // Do not make either pointer-owned gesture pay for a full graph walk.
        // The release or the canonical graph update immediately afterwards
        // refreshes this stable viewport extent.
        if (liveDragNodeId || (dragWire && dragWire.active)) return false
        sceneBounds = graphLayoutBounds()
        return true
    }
    function updateInteraction(changes) {
        const next = ({})
        const current = interaction || ({})
        for (const key in current) next[key] = current[key]
        for (const key in changes) next[key] = changes[key]
        interaction = next
        return next
    }
    function interactionSource() {
        const active = interaction && interaction.source
        return active && active.id ? active : source
    }
    function samePort(first, second) {
        return String(first && (first.endpointId || first.id) || "")
            === String(second && (second.endpointId || second.id) || "")
    }
    function sameSignalKind(port) {
        const activeSource = interactionSource()
        if (!activeSource || !activeSource.kind || !port) return false
        if (activeSource.kind === "axis") return port.kind === "axis"
        if (activeSource.kind === "native-pov") return port.kind === "native-pov"
        return port.kind === "button"
    }
    function routingContextIsCurrent() {
        if (!routingActive) return false
        return Number(interaction.expectedRevision || 0) === Number(graph.revision || 0)
            && String(interaction.profileId || "") === String(graph.profileId || "")
            && String(interaction.rigId || "") === String(graph.deviceRigId || "")
    }
    function cancelRouting(reason, announceCancellation) {
        const hadRouting = routingActive || Boolean(dragWire && dragWire.active)
        clearProcessorChannelDwell()
        pendingSourceDragPoint = ({})
        sourceDragFrameRequested = false
        interaction = ({ "mode": "IDLE", "source": ({}), "target": ({}),
            "expectedRevision": 0, "profileId": "", "rigId": "", "token": interactionToken })
        source = ({})
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0, "target": ({}) })
        lastSourceDragPort = ({})
        sourceDragDropHandled = true
        connectionPreview = ({})
        if (hadRouting && announceCancellation && reason && reason.length > 0) {
            notice = reason
            noticeError = false
        }
        return hadRouting
    }
    function armSource(port, beginDrag) {
        if (autoLayoutMotionActive) {
            notice = "Auto Layout is settling. Wait for the graph to finish moving."
            noticeError = false
            return false
        }
        if (!port || !port.id) return false
        if (mode === "effective" || !graph.editable) {
            notice = "Effective view is read-only. Switch to Configured to edit this route."
            noticeError = true
            return false
        }
        if (!port.available) {
            notice = "This input is inactive until calibration reports meaningful travel."
            noticeError = true
            return false
        }
        if (!beginDrag && routingActive && samePort(interactionSource(), port)) {
            cancelRouting("Connection cancelled.", true)
            return false
        }
        interactionToken += 1
        interaction = ({ "mode": beginDrag ? "SOURCE_DRAGGING" : "SOURCE_ARMED", "source": port,
            "target": ({}), "expectedRevision": Number(graph.revision || 0),
            "profileId": String(graph.profileId || ""), "rigId": String(graph.deviceRigId || ""),
            "token": interactionToken })
        source = port
        inspectedRoute = ({})
        inspectedNode = ({})
        connectionPreview = ({})
        notice = beginDrag ? "Dragging " + port.label + ". Release on a highlighted destination."
                          : "Connection armed: " + port.label + ". Choose a compatible destination."
        noticeError = false
        return true
    }
    function portHint(port, destination) {
        if (!port) return "Signal Flow port"
        if (destination && connectionPreview && String(connectionPreview.portId || "") === String(port.id || ""))
            return String(connectionPreview.message || "")
        if (destination && routingActive && !sameSignalKind(port))
            return "This destination carries a different signal kind."
        if (destination && mode === "effective") return "Switch to Configured to edit this route."
        return destination ? "Choose this destination for the armed source."
                           : "Click to arm this source, or drag to create a route."
    }
    function compatible(port) {
        return Boolean(port && port.available && sameSignalKind(port))
    }
    // Routing is a card-level task as well as a port-level one.  The source
    // card stays unmistakable, and destination cards advertise whether they
    // are compatible candidates or the current hovered target.
    function routingNodeState(nodeData) {
        if (!nodeData) return ""
        // Selection remains distinct from every routing state.  A selected
        // card is an inspection decision, never an armed source or candidate.
        if (!source || !source.id) {
            return inspectedNode && inspectedNode.id && nodeMatchesId(nodeData, inspectedNode.id)
                ? "selected" : ""
        }
        const sourceOwnerId = portOwnerNodeId(source)
        if (sourceOwnerId && nodeMatchesId(nodeData, sourceOwnerId)) return "source"
        const previewOwnerId = String(connectionPreview && connectionPreview.ownerNodeId || "")
        if (previewOwnerId && nodeMatchesId(nodeData, previewOwnerId))
            return connectionPreview.compatible ? "target" : "blocked"
        if (nodeData.kind === "processor") {
            if (processorChannelDwellActive(nodeData)) return "target"
            if (processorChannelPreviewCandidate(nodeData)) return "candidate"
            return ""
        }
        if (nodeData.kind !== "output") return ""
        const cardPorts = portsForNode(nodeData)
        for (let index = 0; index < cardPorts.length; ++index) {
            if (matching(cardPorts[index], true) && compatible(cardPorts[index])) return "candidate"
        }
        return ""
    }
    function routingNodeBorderColor(state) {
        if (state === "source") return deck.accent
        if (state === "target") return deck.attention
        if (state === "candidate") return deck.focus
        if (state === "blocked") return deck.attention
        if (state === "selected") return deck.healthy
        return deck.border
    }
    function routingNodeBorderWidth(state) {
        return state === "source" || state === "target" ? 3 : state.length > 0 ? 2 : 1
    }
    function routingNodeSurface(baseColor, state) {
        if (!state) return baseColor
        const emphasis = state === "target" || state === "blocked" ? deck.attention
            : state === "source" ? deck.accent : state === "selected" ? deck.healthy : deck.focus
        const amount = state === "candidate" ? 0.08 : 0.14
        return Qt.rgba(baseColor.r * (1 - amount) + emphasis.r * amount,
            baseColor.g * (1 - amount) + emphasis.g * amount,
            baseColor.b * (1 - amount) + emphasis.b * amount, baseColor.a)
    }
    function routeTopologySignature(routes) {
        const routeParts = []
        const sourceRoutes = routes || []
        for (let routeIndex = 0; routeIndex < sourceRoutes.length; ++routeIndex) {
            const route = sourceRoutes[routeIndex] || ({})
            const segmentParts = []
            const segments = route.segments || []
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                const segment = segments[segmentIndex] || ({})
                segmentParts.push([String(segment.id || ""), String(segment.sourceNodeId || ""),
                    String(segment.destinationNodeId || ""), String(segment.sourceEndpointId || ""),
                    String(segment.destinationEndpointId || "")].join("~"))
            }
            routeParts.push([String(route.id || ""), String(route.sourceNodeId || ""),
                String(route.destinationNodeId || ""), String(route.sourceEndpointId || route.sourcePortId || ""),
                String(route.destinationEndpointId || route.destinationPortId || ""), segmentParts.join(",")].join("\u001f"))
        }
        routeParts.sort()
        return routeParts.join("\u001e")
    }
    function renderedRouteTopologySignature() {
        const renderedRoutes = []
        const entries = wireGeometry || []
        for (let index = 0; index < entries.length; ++index) {
            if (entries[index] && entries[index].route) renderedRoutes.push(entries[index].route)
        }
        return routeTopologySignature(renderedRoutes)
    }
    function routeLive(route) {
        const entries = liveTelemetry.routes || []
        for (let i = 0; i < entries.length; ++i) if (entries[i].id === route.id) return entries[i]
        return ({ "active": false, "value": 0 })
    }
    function routeIsLive(route) { return Boolean(routeLive(route).active) }
    function routeMatchesSearch(route) {
        const needle = normalized(query)
        if (needle.length === 0) return true
        if (needle === "mapped") return Boolean(route.enabled)
        if (needle === "unmapped") return false
        if (needle === "problems") return routeHasProblem(route)
        if (needle === "active") return routeIsLive(route)
        const text = normalized(String(route.sourceLabel || "") + " " + String(route.destinationLabel || "")
            + " " + String(route.health || "") + " " + (route.processors || []).join(" "))
        return text.indexOf(needle) >= 0
    }
    function routeMatchesState(route) {
        if (routeStateFilter === "all") return true
        if (routeStateFilter === "mapped") return Boolean(route.enabled)
        if (routeStateFilter === "unmapped") return !route.enabled
        if (routeStateFilter === "problems") return routeHasProblem(route)
        if (routeStateFilter === "active") return routeIsLive(route)
        return true
    }
    function selectNode(nodeData) {
        if (!nodeData || !nodeData.id) return false
        if (routingActive) cancelRouting("", false)
        inspectedNode = nodeData
        inspectedRoute = ({})
        inspectedPort = ({})
        inspectedPortOwner = ({})
        inspectedAnnotation = ({})
        source = ({})
        inspectorOpen = true
        ensureInspectorPosition()
        notice = (nodeData.label || "Signal Flow card") + " selected. Its saved identity, readiness, and endpoint summary are shown in the destination bus."
        noticeError = false
        return true
    }
    // A click in empty graph space is an explicit dismissal, not an ambiguous
    // no-op.  Clear both the route object and its canonical segment identity:
    // Canvas highlights individual segments, so leaving the latter behind
    // would make a trace appear selected after its inspector was dismissed.
    function clearGraphSelection() {
        // An explicit canvas dismissal wins over a deferred return-state
        // restore from a recently closed settings page.
        presentationRestored = true
        if (routingActive) cancelRouting("", false)
        inspectedRoute = ({})
        selectedSegmentId = ""
        inspectedNode = ({})
        inspectedPort = ({})
        inspectedPortOwner = ({})
        inspectedAnnotation = ({})
        source = ({})
        inspectorOpen = false
    }
    function dismissEmptyGraphAt(x, y) {
        const hit = hitWire(x, y)
        if (hit && hit.route) return false
        clearGraphSelection()
        return true
    }
    function useInputScope(nodeData) {
        if (!nodeData || nodeData.kind !== "input" || !nodeData.controllerRecordId || !graph.deviceRigId) return false
        if (routingActive) cancelRouting("Connection cancelled because the editing scope changed.", true)
        const changed = backendObject.setEditingDeviceContext(String(graph.deviceRigId), [String(nodeData.controllerRecordId)])
        if (!changed) {
            notice = "This saved input could not become the editing scope. Resolve its Device Rig membership in Devices."
            noticeError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        inspectedNode = ({});
        source = ({});
        inspectedRoute = ({});
        notice = "Editing scope changed to " + (nodeData.label || "the selected input") + ". Its routes are now editable."
        noticeError = false
        return true
    }
    function selectProfileContext(profile) {
        if (!profile || !profile.id) return false
        if (routingActive) cancelRouting("Connection cancelled because the editing profile changed.", true)
        if (!backendObject.setSignalFlowEditingProfileContext(String(profile.id))) {
            notice = "The selected profile could not become the Signal Flow editing context."
            noticeError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        source = ({}); inspectedRoute = ({}); inspectedNode = ({})
        notice = "Signal Flow now edits " + (profile.displayName || profile.name || "the selected profile") + ". Runtime mapping was not changed."
        noticeError = false
        return true
    }
    function selectRigContext(rig) {
        if (!rig || !rig.id) return false
        if (routingActive) cancelRouting("Connection cancelled because the Device Rig changed.", true)
        if (!backendObject.setEditingDeviceContext(String(rig.id), [])) {
            notice = "The selected Device Rig could not become the Signal Flow context."
            noticeError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        source = ({}); inspectedRoute = ({}); inspectedNode = ({})
        notice = "Signal Flow now shows " + (rig.name || "the selected Device Rig") + ". Choose a member card before editing its routes."
        noticeError = false
        return true
    }
    function beginSourceDrag(port, point) {
        if (!armSource(port, true)) return false
        pendingSourceDragPoint = ({})
        sourceDragFrameRequested = false
        lastSourceDragPort = port
        sourceDragDropHandled = false
        dragWire = ({ "active": true, "source": port,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0), "target": ({}) })
        updateSourceDrag(point)
        return true
    }
    function updateSourceDrag(point) {
        if (!dragWire.active || !routingContextIsCurrent()) {
            if (routingActive || dragWire.active)
                cancelRouting("The graph changed while the connection was being dragged. No route was changed.", true)
            return
        }
        updateProcessorChannelDwellAt(point)
        const target = sourceDragDestinationAt(point)
        dragWire = ({ "active": true, "source": interactionSource(),
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0),
            "target": target || ({}) })
    }
    function queueSourceDrag(point) {
        if (!dragWire.active || !point || !isFinite(Number(point.x)) || !isFinite(Number(point.y))) return false
        pendingSourceDragPoint = ({ "x": Number(point.x), "y": Number(point.y) })
        return flushQueuedSourceDrag()
    }
    function flushQueuedSourceDrag() {
        const pending = pendingSourceDragPoint || ({})
        pendingSourceDragPoint = ({})
        sourceDragFrameRequested = false
        if (!dragWire.active || !isFinite(Number(pending.x)) || !isFinite(Number(pending.y))) return false
        updateSourceDrag(pending)
        liveWireDragFrameCount += 1
        return true
    }
    function sourceDragPortFromDrop(drop) {
        const reported = drop && drop.source && drop.source.port
        if (reported && reported.id) return reported
        const active = dragWire && dragWire.source
        if (active && active.id) return active
        return lastSourceDragPort && lastSourceDragPort.id ? lastSourceDragPort : null
    }
    function sourceDragDestinationAt(point) {
        if (!point || !routingActive || !routingContextIsCurrent()) return null
        const output = node("output")
        if (!output || !output.id) return null
        const candidates = visibleCardPorts(output, true)
        const hitRadiusSquared = magneticPortRadius * magneticPortRadius
        let closest = null
        let closestDistanceSquared = hitRadiusSquared
        let nearestInvalid = null
        let nearestInvalidDistanceSquared = hitRadiusSquared
        for (let index = 0; index < candidates.length; ++index) {
            const candidate = candidates[index]
            const center = currentGraphSpacePortCenter(candidate.endpointId, candidate.id, output, false)
            const dx = Number(point.x) - Number(center.x)
            const dy = Number(point.y) - Number(center.y)
            const distanceSquared = dx * dx + dy * dy
            if (!compatible(candidate) && distanceSquared <= nearestInvalidDistanceSquared) {
                nearestInvalid = candidate
                nearestInvalidDistanceSquared = distanceSquared
            }
            if (compatible(candidate) && distanceSquared <= closestDistanceSquared) {
                closest = candidate
                closestDistanceSquared = distanceSquared
            }
        }
        // Do not flicker between adjacent targets while the pointer remains
        // close to the currently snapped port. A materially nearer compatible
        // target can still take over immediately.
        const retained = interaction && interaction.target
        if (retained && retained.id && compatible(retained)) {
            const retainedCenter = currentGraphSpacePortCenter(retained.endpointId, retained.id, output, false)
            const dx = Number(point.x) - Number(retainedCenter.x)
            const dy = Number(point.y) - Number(retainedCenter.y)
            const retainedDistanceSquared = dx * dx + dy * dy
            const retainRadius = magneticPortRadius + magneticPortHysteresis
            if (retainedDistanceSquared <= retainRadius * retainRadius
                    && (!closest || retainedDistanceSquared <= closestDistanceSquared + 16)) {
                closest = retained
            }
        }
        if (closest) {
            previewDestination(closest)
            if (connectionPreview && connectionPreview.compatible) return closest
        }
        updateInteraction({ "mode": "SOURCE_DRAGGING", "target": ({}) })
        if (nearestInvalid) previewDestination(nearestInvalid)
        else connectionPreview = ({})
        return null
    }
    function completeSourceDrag(sourcePort, destinationPort) {
        if (sourceDragDropHandled || !sourcePort || !sourcePort.id || !destinationPort || !destinationPort.id
                || !routingActive || !samePort(sourcePort, interactionSource()))
            return false
        sourceDragDropHandled = true
        return connectDragged(sourcePort, destinationPort)
    }
    function endSourceDrag() {
        if (!dragWire.active) return
        if (!sourceDragDropHandled) flushQueuedSourceDrag()
        else {
            pendingSourceDragPoint = ({})
            sourceDragFrameRequested = false
        }
        const sourcePort = sourceDragPortFromDrop(null)
        const releasePoint = ({ "x": Number(dragWire.x || 0), "y": Number(dragWire.y || 0) })
        // Some Qt Quick delivery paths deactivate the handler before the
        // destination DropArea gets onDropped. Resolve the actual rendered
        // target from the release point in that case; DropArea uses the same
        // one-shot completion helper when it arrives first.
        if (!sourceDragDropHandled && routingContextIsCurrent()) {
            const destinationPort = interaction && interaction.target && interaction.target.id
                ? interaction.target : sourceDragDestinationAt(releasePoint)
            if (destinationPort) completeSourceDrag(sourcePort, destinationPort)
            else cancelRouting("Connection cancelled: release on a highlighted compatible destination.", true)
        } else if (!routingContextIsCurrent()) {
            cancelRouting("The graph changed while the connection was being dragged. No route was changed.", true)
        }
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0,
            "target": routingActive && interaction && interaction.target ? interaction.target : ({}) })
    }
    function previewDestination(port) {
        const activeSource = interactionSource()
        if (!port || !activeSource || !activeSource.id || !routingActive) return
        if (!routingContextIsCurrent()) {
            cancelRouting("The graph changed while the connection was being prepared. No route was changed.", true)
            return
        }
        // A held pointer commonly delivers multiple samples inside the same
        // magnetic target.  The canonical graph and compatibility inputs are
        // unchanged in that case, so keep the existing preview rather than
        // asking AppBackend to calculate it again for every sample.
        if (String(connectionPreview && connectionPreview.portId || "") === String(port.id || "")
                && String(connectionPreview && connectionPreview.sourcePortId || "")
                    === String(activeSource.id || "")) return
        const sourceEndpoint = String(activeSource.endpointId || activeSource.id || "")
        const destinationEndpoint = String(port.endpointId || port.id || "")
        const preview = sourceEndpoint.length > 0 && destinationEndpoint.length > 0
            ? backendObject.signalFlowPreviewConnection(sourceEndpoint, destinationEndpoint, "",
                Number(interaction.expectedRevision || 0))
            : ({ "success": false, "title": "Port is unavailable",
                "message": "This port no longer has a canonical graph endpoint. Refresh the graph and retry." })
        const accepted = Boolean(preview && (preview.success || preview.requiresCollisionDecision))
        connectionPreview = ({ "portId": String(port.id || ""), "ownerNodeId": portOwnerNodeId(port),
            "compatible": accepted, "canCommit": Boolean(preview && preview.canCommit),
            "requiresCollisionDecision": Boolean(preview && preview.requiresCollisionDecision),
            "title": String(preview && preview.title || "Connection preview"),
            "message": String(preview && preview.message || "This destination cannot be used."),
            "sourcePortId": String(activeSource.id || "") })
        if (accepted) updateInteraction({ "mode": "TARGET_PREVIEW", "target": port })
    }
    function clearDestinationPreview(port) {
        if (!port) return
        if (dragWire && dragWire.active && String(interaction && interaction.target && interaction.target.id || "")
                === String(port.id || "")) return
        if (String(interaction && interaction.target && interaction.target.id || "") === String(port.id || ""))
            updateInteraction({ "mode": "SOURCE_ARMED", "target": ({}) })
        if (String(connectionPreview.portId || "") === String(port.id || "")) connectionPreview = ({})
    }
    function previewProcessorTarget(route, segmentId) {
        if (!route || !route.id || !segmentId) return
        pendingProcessorRouteId = String(route.id)
        pendingProcessorSegmentId = String(segmentId)
        processorHysteresis.restart()
    }
    function searchResultCount() {
        const routes = (graph.routes || []).filter(function(route) { return routeMatchesSearch(route) })
        return routes.length
    }
    function focusSearchResult() {
        const routes = (graph.routes || []).filter(function(route) { return routeMatchesSearch(route) })
        if (routes.length > 0) {
            inspectedRoute = routes[0]
            inspectedNode = ({})
            source = ({})
            focusCurrentSelection()
            notice = "Focused the first matching route. Refine search or use filters to narrow results."
            noticeError = false
            return true
        }
        const inputs = sourcePorts()
        if (inputs.length > 0) {
            selectSource(inputs[0])
            focusSourcePort(inputs[0])
            notice = "Focused the first matching input port."
            noticeError = false
            return true
        }
        notice = "No Signal Flow result matches this search."
        noticeError = true
        return false
    }
    function routeIsXrayRelated(route) {
        if (!xrayMode) return true
        if (source && source.id) return String(route.sourcePortId || "") === String(source.id)
        if (!inspectedRoute || !inspectedRoute.id) return true
        return String(route.id) === String(inspectedRoute.id)
            || String(route.sourcePortId || "") === String(inspectedRoute.sourcePortId || "")
            || String(route.destinationPortId || "") === String(inspectedRoute.destinationPortId || "")
            || (route.viaNodeId && String(route.viaNodeId) === String(inspectedRoute.viaNodeId || ""))
    }
    function routeVisualAlpha(route, live) {
        if (!routeMatchesSearch(route) || !routeMatchesState(route)) return 0.12
        if (!routeIsXrayRelated(route)) return 0.08
        if (signalFocus && !live) return 0.18
        if (inspectedRoute && String(inspectedRoute.id || "") === String(route.id || "")) return 1.0
        if (routeHasProblem(route)) return 0.76
        return live ? 0.94 : 0.48
    }
    function stableLane(value, count) {
        const text = String(value || "")
        let hash = 0
        for (let i = 0; i < text.length; ++i) hash = ((hash << 5) - hash + text.charCodeAt(i)) | 0
        return Math.abs(hash) % Math.max(1, count || 1)
    }
    function nodeIdentity(nodeData) {
        return String(nodeData && (nodeData.id || nodeData.objectId) || "")
    }
    function nodeStorageIdentity(nodeData) {
        return String(nodeData && (nodeData.objectId || nodeData.id) || "")
    }
    function nodeForId(id) {
        const wanted = String(id || "")
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            const candidate = nodes[index]
            if (String(candidate.id || "") === wanted || String(candidate.objectId || "") === wanted)
                return candidate
        }
        return ({})
    }
    function nodeMatchesId(nodeData, id) {
        const wanted = String(id || "")
        return String(nodeData && nodeData.id || "") === wanted
            || String(nodeData && nodeData.objectId || "") === wanted
    }
    function nodePosition(nodeData, fallbackX, fallbackY) {
        if (!nodeData) return ({ "x": fallbackX, "y": fallbackY })
        if (liveDragNodeId && nodeMatchesId(nodeData, liveDragNodeId)
                && isFinite(Number(liveDragPosition.x)) && isFinite(Number(liveDragPosition.y)))
            return liveDragPosition
        const livePosition = liveNodePositions[String(nodeData.id || "")]
            || liveNodePositions[String(nodeData.objectId || "")]
        if (livePosition) return livePosition
        return settledNodePosition(nodeData, fallbackX, fallbackY)
    }
    function settledNodePosition(nodeData, fallbackX, fallbackY) {
        if (!nodeData) return ({ "x": fallbackX, "y": fallbackY })
        const saved = nodePositions[String(nodeData.objectId || nodeData.id || "")]
            || nodePositions[String(nodeData.id || "")]
        if (saved) return saved
        return ({ "x": Number(nodeData.x === undefined ? fallbackX : nodeData.x),
                  "y": Number(nodeData.y === undefined ? fallbackY : nodeData.y) })
    }
    // Keep a dragging card's layout rectangle at its settled position and
    // move the visual subtree with a scenegraph transform.  Port anchors are
    // recorded in card-local coordinates, so currentGraphSpacePortCenter()
    // can use the same delta for exact wire attachment without re-laying out
    // every child on each pointer sample.
    function liveNodeTranslationX(nodeData, fallbackX, fallbackY) {
        if (!nodeData || !isLiveNodeDrag(nodeData)
                || !isFinite(Number(liveDragPosition.x))) return 0
        return liveDragDeltaX
    }
    function liveNodeTranslationY(nodeData, fallbackX, fallbackY) {
        if (!nodeData || !isLiveNodeDrag(nodeData)
                || !isFinite(Number(liveDragPosition.y))) return 0
        return liveDragDeltaY
    }
    function snapNodeSize(nodeData) {
        return ({ "width": graphCardWidth(nodeData), "height": graphCardHeight(nodeData) })
    }
    function snapGridValue(value) {
        const proposed = Math.round(Number(value) / snapGridSize) * snapGridSize
        return Math.abs(proposed - Number(value)) <= snapGridThreshold
            ? ({ "value": proposed, "guide": proposed, "label": "grid" }) : null
    }
    function stabilizeAlignmentCandidate(nodeData, axis, freeValue, candidate) {
        if (!isLiveNodeDrag(nodeData)) return candidate
        const nodeKey = nodeIdentity(nodeData)
        const key = nodeKey + "|" + axis
        const previous = alignmentGuideHysteresis[key]
        let stable = candidate
        // Keep a current guide a few logical pixels past its acquisition
        // threshold. A materially closer relationship still replaces it, but
        // two nearly-equal candidates do not make the guide flicker.
        if (previous && Math.abs(Number(previous.value) - Number(freeValue))
                <= alignmentSnapThreshold + alignmentSnapHysteresis) {
            const currentDistance = stable ? Number(stable.distance || Number.POSITIVE_INFINITY)
                : Number.POSITIVE_INFINITY
            const previousDistance = Math.abs(Number(previous.value) - Number(freeValue))
            if (!stable || previousDistance <= currentDistance + alignmentSnapHysteresis)
                stable = ({ "value": Number(previous.value), "guide": Number(previous.guide),
                    "distance": previousDistance, "label": String(previous.label || "alignment") })
        }
        const next = ({})
        for (const existingKey in alignmentGuideHysteresis) next[existingKey] = alignmentGuideHysteresis[existingKey]
        if (stable) next[key] = stable
        else delete next[key]
        alignmentGuideHysteresis = next
        return stable
    }
    function nodeSnapCandidate(nodeData, x, y, bypass) {
        const freeX = Number(x)
        const freeY = Number(y)
        if (!isFinite(freeX) || !isFinite(freeY)) return ({ "active": false, "changed": false, "x": x, "y": y })
        if (Boolean(bypass) || !snapToGridEnabled)
            return ({ "active": false, "changed": false, "x": freeX, "y": freeY })
        const size = snapNodeSize(nodeData)
        const identity = nodeIdentity(nodeData)
        let alignmentX = null
        let alignmentY = null
        function considerX(value, guide, label) {
            const distance = Math.abs(Number(value) - freeX)
            if (distance <= alignmentSnapThreshold && (!alignmentX || distance < alignmentX.distance))
                alignmentX = ({ "value": Number(value), "guide": Number(guide), "distance": distance, "label": label })
        }
        function considerY(value, guide, label) {
            const distance = Math.abs(Number(value) - freeY)
            if (distance <= alignmentSnapThreshold && (!alignmentY || distance < alignmentY.distance))
                alignmentY = ({ "value": Number(value), "guide": Number(guide), "distance": distance, "label": label })
        }
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            const other = nodes[index]
            if (!other || nodeMatchesId(other, identity)) continue
            const position = settledNodePosition(other, Number(other.x || 0), Number(other.y || 0))
            const otherSize = snapNodeSize(other)
            // Keep the alignment field small and intentional.  It assists
            // the nearest meaningful edge/center relationship, never a
            // distant magnetic pull across the canvas.
            considerX(position.x, position.x, "left edge")
            considerX(position.x + otherSize.width - size.width, position.x + otherSize.width, "right edge")
            considerX(position.x + (otherSize.width - size.width) * 0.5,
                position.x + otherSize.width * 0.5, "vertical center")
            considerY(position.y, position.y, "top edge")
            considerY(position.y + (otherSize.height - size.height) * 0.5,
                position.y + otherSize.height * 0.5, "horizontal center")
        }
        alignmentX = stabilizeAlignmentCandidate(nodeData, "x", freeX, alignmentX)
        alignmentY = stabilizeAlignmentCandidate(nodeData, "y", freeY, alignmentY)
        const gridX = alignmentX ? null : snapGridValue(freeX)
        const gridY = alignmentY ? null : snapGridValue(freeY)
        const snappedX = alignmentX ? alignmentX.value : gridX ? gridX.value : freeX
        const snappedY = alignmentY ? alignmentY.value : gridY ? gridY.value : freeY
        const active = Boolean(alignmentX || alignmentY || gridX || gridY)
        return ({
            "active": active,
            "changed": active && (Math.abs(snappedX - freeX) > 0.01 || Math.abs(snappedY - freeY) > 0.01),
            "x": snappedX, "y": snappedY, "freeX": freeX, "freeY": freeY,
            "width": size.width, "height": size.height,
            "guideX": alignmentX ? alignmentX.guide : gridX ? gridX.guide : NaN,
            "guideY": alignmentY ? alignmentY.guide : gridY ? gridY.guide : NaN,
            "alignmentX": alignmentX ? alignmentX.label : "",
            "alignmentY": alignmentY ? alignmentY.label : "",
            "gridX": Boolean(gridX), "gridY": Boolean(gridY)
        })
    }
    function updateNodeSnapPreview(nodeData, x, y, altBypass) {
        if (!isLiveNodeDrag(nodeData)) return ({})
        snapDragAltBypass = Boolean(altBypass)
        const candidate = nodeSnapCandidate(nodeData, x, y, snapDragAltBypass)
        nodeSnapPreview = candidate.active ? candidate : ({})
        return candidate
    }
    function clearNodeSnapPreview() {
        nodeSnapPreview = ({})
        snapDragAltBypass = false
        alignmentGuideHysteresis = ({})
    }
    function markNodeSnapSettling(nodeData) {
        const next = ({})
        for (const key in snapSettlingNodeIds) next[key] = snapSettlingNodeIds[key]
        next[nodeIdentity(nodeData)] = true
        const stored = nodeStorageIdentity(nodeData)
        if (stored) next[stored] = true
        snapSettlingNodeIds = next
        snapSettlingTimer.restart()
    }
    function nodeIsSnapSettling(nodeData) {
        return Boolean(snapSettlingNodeIds[nodeIdentity(nodeData)]
            || snapSettlingNodeIds[nodeStorageIdentity(nodeData)])
    }
    Timer {
        id: snapSettlingTimer
        interval: root.motionFastDuration + 12
        repeat: false
        onTriggered: root.snapSettlingNodeIds = ({})
    }
    function noteNodePosition(objectId, x, y, scheduleGeometry) {
        const id = String(objectId || "")
        if (!id || !isFinite(x) || !isFinite(y)) return
        const current = nodePositions[id]
        if (current && Math.abs(current.x - x) < 0.1 && Math.abs(current.y - y) < 0.1) return
        const next = ({})
        for (const key in nodePositions) next[key] = nodePositions[key]
        next[id] = ({ "x": Number(x), "y": Number(y) })
        nodePositions = next
        if (scheduleGeometry !== false) requestWireGeometryRebuild()
    }
    function requestWireGeometryRebuild() {
        wireGeometryInvalidationEpoch += 1
        scheduledWireGeometryInvalidationEpoch = wireGeometryInvalidationEpoch
        wireGeometryTimer.restart()
    }
    function discardQueuedWireGeometryRebuild() {
        wireGeometryInvalidationEpoch += 1
        scheduledWireGeometryInvalidationEpoch = 0
        wireGeometryTimer.stop()
    }
    function flushQueuedWireGeometryRebuild() {
        const scheduled = scheduledWireGeometryInvalidationEpoch
        const shouldFlush = scheduled > 0 && scheduled === wireGeometryInvalidationEpoch
        discardQueuedWireGeometryRebuild()
        if (shouldFlush) rebuildWireGeometry()
        return shouldFlush
    }
    function requestGraphGeometryHandoff() {
        graphGeometryHandoffEpoch += 1
        scheduledGraphGeometryHandoffEpoch = graphGeometryHandoffEpoch
        graphGeometryHandoffTimer.restart()
    }
    function discardQueuedGraphGeometryHandoff() {
        graphGeometryHandoffEpoch += 1
        scheduledGraphGeometryHandoffEpoch = 0
        graphGeometryHandoffTimer.stop()
    }
    function requestPortAnchorMeasurement() {
        portAnchorMeasurementEpoch += 1
    }
    function portOwnerNodeId(port) {
        if (port && port.ownerNodeId) return String(port.ownerNodeId)
        const endpoint = String(port && (port.endpointId || port.id) || "")
        if (!endpoint) return ""
        const nodes = graph.nodes || []
        for (let nodeIndex = 0; nodeIndex < nodes.length; ++nodeIndex) {
            const ports = nodes[nodeIndex].ports || []
            for (let portIndex = 0; portIndex < ports.length; ++portIndex) {
                const candidate = ports[portIndex]
                if (String(candidate.endpointId || candidate.id || "") === endpoint)
                    return nodeIdentity(nodes[nodeIndex])
            }
        }
        return ""
    }
    function rememberPortAnchor(port, key, x, y, anchors, offsets) {
        if (!key) return
        anchors[key] = ({ "x": Number(x), "y": Number(y) })
        const ownerId = portOwnerNodeId(port)
        const owner = nodeForId(ownerId)
        if (!owner || !nodeIdentity(owner)) return
        const position = nodePosition(owner, Number(owner.x || 0), Number(owner.y || 0))
        offsets[key] = ({ "ownerNodeId": nodeIdentity(owner),
            "x": Number(x) - Number(position.x), "y": Number(y) - Number(position.y) })
    }
    // The rendered port dot is the single source of truth for its wire
    // endpoint.  Its recorded card-local offset keeps that truth attached to
    // the card for every transient drag frame.
    function notePortAnchor(port, x, y) {
        if (!port || !isFinite(x) || !isFinite(y)) return
        const ownerId = portOwnerNodeId(port)
        // A live card position plus the existing card-local offset is already
        // authoritative. Processor marker callbacks can therefore avoid
        // cloning the complete anchor cache for every pointer update.
        if (liveDragNodeId && !portAnchorDiagnosticsEnabled) return
        const endpointKey = String(port.endpointId || "")
        const legacyKey = String(port.id || "")
        const disclosureReleaseReflow = pendingDisclosureReleaseReflowNodeId
            && nodeMatchesId(nodeForId(ownerId), pendingDisclosureReleaseReflowNodeId)
        const sectionDisclosureReflow = sectionDisclosureReflowActiveForNodeId(ownerId)
        const releaseAnchorReflow = releaseAnchorReflowActive
        // Moving a card does not change any port's local position. Its prior
        // measured offset remains exact after the committed card coordinate
        // changes, so ignore the one delegate-polish echo that follows a
        // release instead of scheduling a graph-wide cache rebuild.
        if (!releaseAnchorReflow && !disclosureReleaseReflow && !sectionDisclosureReflow && layoutCommitAnchorSuppressionNodeId
                && nodeMatchesId(nodeForId(ownerId), layoutCommitAnchorSuppressionNodeId)
                && (portAnchorOffsets[endpointKey] || portAnchorOffsets[legacyKey])) return
        const endpointAnchor = portAnchors[endpointKey]
        const legacyAnchor = portAnchors[legacyKey]
        const unchanged = function(anchor) {
            return anchor && Math.abs(Number(anchor.x) - Number(x)) < 0.01
                && Math.abs(Number(anchor.y) - Number(y)) < 0.01
        }
        // Qt Quick can deliver repeat geometry notifications while polishing a
        // layout.  A point that has not moved must not reconstruct the wire
        // cache or request another Canvas frame; otherwise harmless polish
        // becomes a self-sustaining repaint/rebuild loop.
        if ((!endpointKey || unchanged(endpointAnchor)) && (!legacyKey || unchanged(legacyAnchor))) return
        const next = ({})
        const offsets = ({})
        for (const key in portAnchors) next[key] = portAnchors[key]
        for (const key in portAnchorOffsets) offsets[key] = portAnchorOffsets[key]
        if (port.endpointId) rememberPortAnchor(port, String(port.endpointId), x, y, next, offsets)
        if (port.id) rememberPortAnchor(port, String(port.id), x, y, next, offsets)
        portAnchors = next
        portAnchorOffsets = offsets
        portAnchorMutationCount += 1
        // During a disclosure-release handoff these exact rendered offsets
        // feed the dedicated incident-only reflow timer. Debounce from the
        // actual delegate geometry so the card is reflowed after its last
        // rendered structural change, never from a guessed wall-clock turn.
        if (releaseAnchorReflow) queueReleaseAnchorReflow(ownerId)
        else if (disclosureReleaseReflow) disclosureReleaseReflowTimer.restart()
        else if (sectionDisclosureReflow) queueSectionDisclosureReflowForNodeId(ownerId)
        else requestWireGeometryRebuild()
    }
    // A hidden or destroyed port row no longer describes a rendered endpoint.
    // Drop its measured offset so a collapsed density group falls back to its
    // current card geometry instead of retaining a stale row position.
    function forgetPortAnchor(port) {
        if (!port) return
        const endpointKey = String(port.endpointId || "")
        const legacyKey = String(port.id || "")
        const ownerId = portOwnerNodeId(port)
        const disclosureReleaseReflow = pendingDisclosureReleaseReflowNodeId
            && nodeMatchesId(nodeForId(ownerId), pendingDisclosureReleaseReflowNodeId)
        const sectionDisclosureReflow = sectionDisclosureReflowActiveForNodeId(ownerId)
        const releaseAnchorReflow = releaseAnchorReflowActive
        if ((!endpointKey || !portAnchors[endpointKey]) && (!legacyKey || !portAnchors[legacyKey])) return
        const next = ({})
        const offsets = ({})
        for (const key in portAnchors)
            if (key !== endpointKey && key !== legacyKey) next[key] = portAnchors[key]
        for (const key in portAnchorOffsets)
            if (key !== endpointKey && key !== legacyKey) offsets[key] = portAnchorOffsets[key]
        portAnchors = next
        portAnchorOffsets = offsets
        portAnchorMutationCount += 1
        if (releaseAnchorReflow) queueReleaseAnchorReflow(ownerId)
        else if (disclosureReleaseReflow) disclosureReleaseReflowTimer.restart()
        else if (sectionDisclosureReflow) queueSectionDisclosureReflowForNodeId(ownerId)
        else requestWireGeometryRebuild()
    }
    // This is the sole endpoint resolver for Canvas geometry, its spatial hit
    // buckets, live-drag replacement segments, and insertion preview.  Its
    // result is always in the unscaled `scene` logical coordinate space.
    function currentGraphSpacePortCenter(endpointId, legacyEndpointId, nodeData, sourceSide) {
        const endpointKey = String(endpointId || "")
        const legacyKey = String(legacyEndpointId || "")
        const frozen = frozenDisclosureFor(nodeData)
        const frozenOffsets = frozen && frozen.portAnchorOffsets ? frozen.portAnchorOffsets : ({})
        const offset = frozenOffsets[endpointKey] || frozenOffsets[legacyKey]
            || portAnchorOffsets[endpointKey] || portAnchorOffsets[legacyKey]
        if (offset && offset.ownerNodeId) {
            const owner = nodeForId(offset.ownerNodeId)
            if (owner && nodeIdentity(owner)) {
                const position = nodePosition(owner, Number(owner.x || 0), Number(owner.y || 0))
                return ({ "x": Number(position.x) + Number(offset.x), "y": Number(position.y) + Number(offset.y) })
            }
        }
        // A direct delegate measurement is better than synthetic geometry
        // whenever an owner cannot be resolved (for example during creation).
        const frozenAnchors = frozen && frozen.portAnchors ? frozen.portAnchors : ({})
        const measured = frozenAnchors[endpointKey] || frozenAnchors[legacyKey]
            || portAnchors[endpointKey] || portAnchors[legacyKey]
        if (measured) return ({ "x": Number(measured.x), "y": Number(measured.y) })
        const position = nodePosition(nodeData, Number(nodeData && nodeData.x || 0), Number(nodeData && nodeData.y || 0))
        return sourceSide ? ({
            "x": Number(position.x) + graphCardWidth(nodeData),
            "y": Number(position.y) + Math.min(graphCardHeight(nodeData) * 0.5,
                48 + stableLane(endpointKey || legacyKey, 13) * 6)
        }) : ({
            "x": Number(position.x),
            "y": Number(position.y) + Math.min(graphCardHeight(nodeData) * 0.5,
                48 + stableLane(endpointKey || legacyKey, 13) * 6)
        })
    }
    function resetPortAnchorDiagnostics() {
        portAnchorDiagnosticSampleCount = 0
        portAnchorDiagnosticFailureCount = 0
        portAnchorDiagnosticMaxError = 0
        portAnchorDiagnosticRecords = ({})
    }
    function collectPortAnchorDiagnostics() {
        if (!portAnchorDiagnosticsEnabled) return ({ "samples": 0, "failures": 0, "maxError": 0 })
        const records = ({})
        let samples = 0
        let failures = 0
        let maxError = 0
        const tolerance = 0.01
        const add = function(endpointId, nodeId, routeId, expected) {
            const actual = portAnchors[String(endpointId || "")]
            if (!actual) return
            const dx = Number(expected.x) - Number(actual.x)
            const dy = Number(expected.y) - Number(actual.y)
            const error = Math.hypot(dx, dy)
            const key = String(endpointId || "")
            records[key] = ({ "endpointId": key, "nodeId": String(nodeId || ""), "routeId": String(routeId || ""),
                "expected": ({ "x": Number(expected.x), "y": Number(expected.y) }),
                "actual": ({ "x": Number(actual.x), "y": Number(actual.y) }), "error": error })
            ++samples
            if (error > tolerance) ++failures
            maxError = Math.max(maxError, error)
        }
        for (let entryIndex = 0; entryIndex < wireGeometry.length; ++entryIndex) {
            const entry = wireGeometry[entryIndex]
            const segments = entry && entry.segments || []
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                const segment = segments[segmentIndex]
                add(segment.sourceEndpointId, segment.sourceNodeId, entry.routeId,
                    ({ "x": segment.startX, "y": segment.startY }))
                add(segment.destinationEndpointId, segment.destinationNodeId, entry.routeId,
                    ({ "x": segment.endX, "y": segment.endY }))
            }
        }
        portAnchorDiagnosticSampleCount += samples
        portAnchorDiagnosticFailureCount += failures
        portAnchorDiagnosticMaxError = Math.max(portAnchorDiagnosticMaxError, maxError)
        portAnchorDiagnosticRecords = records
        return ({ "samples": samples, "failures": failures, "maxError": maxError, "records": records })
    }
    function wirePoints(segment, lane) {
        const points = []
        const direction = segment.endX >= segment.startX ? 1 : -1
        if (graph.workspace && graph.workspace.wireStyle === "orthogonal") {
            // Each distinct endpoint pair receives a distinct, stable rail.
            // The tiny common egress at an actual shared socket is the only
            // permitted overlap; after that, rails fan out immediately.
            if (wiresOverlayCards) {
                const exitX = segment.startX + direction * 20
                const entryX = segment.endX - direction * 20
                const railX = Math.round((segment.startX + segment.endX) * 0.5 + lane * 18)
                const startRailY = Math.round(segment.startY + lane * 14)
                const endRailY = Math.round(segment.endY + lane * 14)
                points.push({ "x": segment.startX, "y": segment.startY })
                points.push({ "x": exitX, "y": segment.startY })
                points.push({ "x": exitX, "y": startRailY })
                points.push({ "x": railX, "y": startRailY })
                points.push({ "x": railX, "y": endRailY })
                points.push({ "x": entryX, "y": endRailY })
                points.push({ "x": entryX, "y": segment.endY })
                points.push({ "x": segment.endX, "y": segment.endY })
                return points
            }
            const stub = segment.hasDetour ? Math.max(42, Math.min(120, Math.abs(segment.endX - segment.startX) * 0.22))
                : Math.round((segment.startX + segment.endX) * 0.5 + lane * 12) - segment.startX
            if (segment.hasDetour) {
                points.push({ "x": segment.startX, "y": segment.startY })
                points.push({ "x": segment.startX + direction * stub, "y": segment.startY })
                points.push({ "x": segment.startX + direction * stub, "y": segment.detourY })
                points.push({ "x": segment.endX - direction * stub, "y": segment.detourY })
                points.push({ "x": segment.endX - direction * stub, "y": segment.endY })
                points.push({ "x": segment.endX, "y": segment.endY })
            } else {
                const middle = Math.round((segment.startX + segment.endX) * 0.5 + lane * 12)
                points.push({ "x": segment.startX, "y": segment.startY })
                points.push({ "x": middle, "y": segment.startY })
                points.push({ "x": middle, "y": segment.endY })
                points.push({ "x": segment.endX, "y": segment.endY })
            }
            return points
        }
        const firstControlX = segment.startX + direction * (segment.hasDetour ? 86 : 178)
        const secondControlX = segment.endX - direction * (segment.hasDetour ? 86 : 178)
        // Stable vertical offsets preserve a fan-out's individual paths
        // without introducing a bundled trunk in the normal view.
        // Detours must retain the route's lane identity too. Otherwise two
        // fan-out paths which share a detour collapse into one apparent wire.
        const detourY = Number(segment.detourY || 0) + lane * 12
        const firstControlY = segment.hasDetour ? detourY : segment.startY + lane * 12
        const secondControlY = segment.hasDetour ? detourY : segment.endY + lane * 12
        for (let step = 0; step <= 16; ++step) {
            const t = step / 16
            const mt = 1 - t
            points.push({
                "x": mt * mt * mt * segment.startX + 3 * mt * mt * t * firstControlX
                    + 3 * mt * t * t * secondControlX + t * t * t * segment.endX,
                "y": mt * mt * mt * segment.startY + 3 * mt * mt * t * firstControlY
                    + 3 * mt * t * t * secondControlY + t * t * t * segment.endY
            })
        }
        return points
    }
    function pointToSegmentDistance(x, y, a, b) {
        const dx = b.x - a.x
        const dy = b.y - a.y
        const length = dx * dx + dy * dy
        if (length < 0.0001) return Math.hypot(x - a.x, y - a.y)
        const t = Math.max(0, Math.min(1, ((x - a.x) * dx + (y - a.y) * dy) / length))
        return Math.hypot(x - (a.x + dx * t), y - (a.y + dy * t))
    }
    function hitWire(x, y) {
        const cell = 96
        const bucket = wireBuckets[Math.floor(x / cell) + ":" + Math.floor(y / cell)] || []
        pointerHitCandidateCount = bucket.length
        let best = null
        let bestDistance = 11
        for (let index = 0; index < bucket.length; ++index) {
            const entry = bucket[index]
            const points = entry.points || []
            for (let point = 1; point < points.length; ++point) {
                let distance = pointToSegmentDistance(x, y, points[point - 1], points[point])
                if (String(entry.routeId || "") === hoveredRouteId) distance -= 0.75
                if (String(entry.routeSegmentId || "") === pendingProcessorSegmentId) distance -= 5.0
                if (distance < bestDistance) { bestDistance = distance; best = entry }
            }
        }
        return best
    }
    function updateWireHover(x, y) {
        const hit = hitWire(x, y)
        const next = hit ? String(hit.routeId || "") : ""
        const nextSegment = hit ? String(hit.routeSegmentId || "") : ""
        if (hoveredRouteId !== next || hoveredSegmentId !== nextSegment) {
            hoveredRouteId = next
            hoveredSegmentId = nextSegment
            requestWirePaint()
        }
    }
    function obstaclePlan(startX, startY, endX, endY, excludedIds) {
        const left = Math.min(startX, endX)
        const right = Math.max(startX, endX)
        if (right - left < 96) return ({ "hasDetour": false, "underCard": false, "detourY": 0 })
        const nodes = graph.nodes || []
        let blockers = 0
        let detourY = 0
        for (let index = 0; index < nodes.length && index < 64; ++index) {
            const candidate = nodes[index]
            const candidateId = String(candidate.id || candidate.objectId || "")
            if (excludedIds && excludedIds[candidateId]) continue
            const position = nodePosition(candidate, 0, 0)
            const cardWidth = graphCardWidth(candidate)
            const cardHeight = graphCardHeight(candidate)
            if (position.x + cardWidth <= left + 28 || position.x >= right - 28) continue
            const span = Math.max(1, endX - startX)
            const t = Math.max(0, Math.min(1, (position.x + cardWidth * 0.5 - startX) / span))
            const lineY = startY + (endY - startY) * t
            if (lineY < position.y - 10 || lineY > position.y + cardHeight + 10) continue
            ++blockers
            if (blockers > 2) return ({ "hasDetour": false, "underCard": true, "detourY": 0 })
            const above = position.y - 26
            const below = position.y + cardHeight + 26
            detourY = Math.abs(startY - above) + Math.abs(endY - above)
                <= Math.abs(startY - below) + Math.abs(endY - below) ? above : below
        }
        return ({ "hasDetour": blockers > 0, "underCard": false, "detourY": detourY })
    }
    function routeSegmentCacheKey(routeId, segment) {
        return String(routeId || "") + "|" + String(segment && segment.routeSegmentId || "")
    }
    function orthogonalSegmentLaneKey(canonical) {
        return String(canonical && canonical.sourceEndpointId || "") + "\u001f"
            + String(canonical && canonical.destinationEndpointId || "")
    }
    function decorateWireSegment(segment, lane) {
        segment.points = wirePoints(segment, lane)
        let minX = Number.POSITIVE_INFINITY
        let minY = Number.POSITIVE_INFINITY
        let maxX = Number.NEGATIVE_INFINITY
        let maxY = Number.NEGATIVE_INFINITY
        for (let pointIndex = 0; pointIndex < segment.points.length; ++pointIndex) {
            const point = segment.points[pointIndex]
            minX = Math.min(minX, point.x); minY = Math.min(minY, point.y)
            maxX = Math.max(maxX, point.x); maxY = Math.max(maxY, point.y)
        }
        segment.bounds = ({ "left": minX - 12, "top": minY - 12, "right": maxX + 12, "bottom": maxY + 12 })
        return segment
    }
    function addSegmentToBuckets(buckets, segmentBucketKeys, route, routeId, segment) {
        const cell = 96
        const cacheKey = routeSegmentCacheKey(routeId, segment)
        const keys = []
        segment.cacheKey = cacheKey
        for (let bx = Math.floor(segment.bounds.left / cell); bx <= Math.floor(segment.bounds.right / cell); ++bx) {
            for (let by = Math.floor(segment.bounds.top / cell); by <= Math.floor(segment.bounds.bottom / cell); ++by) {
                const key = bx + ":" + by
                let bucket = buckets[key]
                if (!bucket) {
                    bucket = []
                    buckets[key] = bucket
                }
                bucket.push({ "route": route, "routeId": routeId,
                    "routeSegmentId": segment.routeSegmentId, "cacheKey": cacheKey, "points": segment.points })
                keys.push(key)
            }
        }
        segmentBucketKeys[cacheKey] = keys
    }
    function addNodeSegmentRef(nodeSegmentRefs, nodeId, entryIndex, segmentIndex) {
        const key = String(nodeId || "")
        if (!key) return
        if (!nodeSegmentRefs[key]) nodeSegmentRefs[key] = []
        nodeSegmentRefs[key].push({ "entryIndex": entryIndex, "segmentIndex": segmentIndex })
    }
    function geometryForCanonicalSegment(routeId, canonical, sourceNode, destinationNode, lane, dragSimplified) {
        const sourceAnchor = currentGraphSpacePortCenter(canonical.sourceEndpointId, "", sourceNode, true)
        const destinationAnchor = currentGraphSpacePortCenter(canonical.destinationEndpointId, "", destinationNode, false)
        const excludedIds = ({})
        excludedIds[nodeIdentity(sourceNode)] = true
        excludedIds[nodeIdentity(destinationNode)] = true
        const obstacle = dragSimplified || wiresOverlayCards
            ? ({ "hasDetour": false, "detourY": 0, "underCard": false })
            : obstaclePlan(sourceAnchor.x, sourceAnchor.y, destinationAnchor.x, destinationAnchor.y, excludedIds)
        return decorateWireSegment({ "id": String(canonical.id || ""), "routeSegmentId": String(canonical.id || ""),
            "sourceNodeId": nodeIdentity(sourceNode), "destinationNodeId": nodeIdentity(destinationNode),
            "sourceEndpointId": String(canonical.sourceEndpointId || ""),
            "destinationEndpointId": String(canonical.destinationEndpointId || ""),
            "startX": sourceAnchor.x, "startY": sourceAnchor.y,
            "endX": destinationAnchor.x, "endY": destinationAnchor.y, "lane": Number(lane || 0),
            "hasDetour": obstacle.hasDetour, "detourY": obstacle.detourY,
            "underCard": obstacle.underCard }, lane)
    }
    function routeGeometryEntry(route, segments, lane) {
        const input = nodeForId(route.sourceNodeId)
        const inputPosition = nodePosition(input, 50, 150)
        const sourceAnchor = ({ "x": segments[0].startX, "y": segments[0].startY })
        const finalX = segments[segments.length - 1].endX
        const finalY = segments[segments.length - 1].endY
        const focusSegment = segments[Math.floor(segments.length * 0.5)]
        return ({
            "route": route,
            "routeId": String(route.id || ""),
            "routeSegmentId": String(segments[0].routeSegmentId || route.id || ""),
            "startX": sourceAnchor.x, "startY": sourceAnchor.y, "endX": finalX, "endY": finalY,
            "segments": segments,
            "focusX": focusSegment ? (focusSegment.startX + focusSegment.endX) * 0.5 : finalX,
            "focusY": focusSegment ? (focusSegment.startY + focusSegment.endY) * 0.5 : finalY,
            "bundleCount": 1, "drawBundleTrunk": false,
            "bundleX": inputPosition.x + graphCardWidth(input),
            "processorCount": Math.max(0, segments.length - 1), "lane": lane
        })
    }
    function snapshotWireGeometry(entries, captureLiveDrag) {
        const snapshot = []
        const sourceEntries = entries || []
        for (let entryIndex = 0; entryIndex < sourceEntries.length; ++entryIndex) {
            const entry = sourceEntries[entryIndex]
            if (!entry) continue
            const segments = []
            const sourceSegments = entry.segments || []
            for (let segmentIndex = 0; segmentIndex < sourceSegments.length; ++segmentIndex) {
                const segment = sourceSegments[segmentIndex]
                if (!segment) continue
                const liveSegment = Boolean(captureLiveDrag) && liveDragSegmentTouches(segment)
                // Canvas only needs these immutable presentation fields while
                // the current cache is rebuilt underneath it.
                segments.push({ "routeSegmentId": String(segment.routeSegmentId || ""),
                    "startX": liveSegment ? liveDragSegmentStartX(segment) : Number(segment.startX),
                    "startY": liveSegment ? liveDragSegmentStartY(segment) : Number(segment.startY),
                    "endX": liveSegment ? liveDragSegmentEndX(segment) : Number(segment.endX),
                    "endY": liveSegment ? liveDragSegmentEndY(segment) : Number(segment.endY),
                    "hasDetour": liveSegment ? false : Boolean(segment.hasDetour),
                    "detourY": liveSegment ? 0 : Number(segment.detourY || 0),
                    "underCard": Boolean(segment.underCard), "lane": Number(segment.lane || 0) })
            }
            if (segments.length > 0) snapshot.push({ "route": entry.route || ({}),
                "routeId": String(entry.routeId || ""), "lane": Number(entry.lane || 0), "segments": segments })
        }
        return snapshot
    }
    function indexReflowWireSegments(entries) {
        const index = ({})
        const sourceEntries = entries || []
        for (let entryIndex = 0; entryIndex < sourceEntries.length; ++entryIndex) {
            const entry = sourceEntries[entryIndex]
            const segments = entry && entry.segments || []
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                const segment = segments[segmentIndex]
                if (segment) index[String(entry.routeId || "") + "|" + String(segment.routeSegmentId || "")] = segment
            }
        }
        return index
    }
    function reflowSourceSegment(routeId, segmentId) {
        return reflowWireSegmentIndex[String(routeId || "") + "|" + String(segmentId || "")] || null
    }
    function wireIsAppearing(routeId) {
        return Boolean(appearingWireRouteIds[String(routeId || "")])
    }
    function prepareWireReflow(duration, captureLiveDrag) {
        if (!wireGeometry || wireGeometry.length === 0) {
            reflowWireGeometry = []
            reflowWireSegmentIndex = ({})
            wireReflowPending = false
            wireReflow = 1
            return
        }
        wireReflowAnimation.stop()
        wireReflowDuration = Number(duration || (autoLayoutMotionActive
            ? motionLayoutDuration : motionStructuralDuration))
        reflowWireGeometry = snapshotWireGeometry(wireGeometry, captureLiveDrag)
        reflowWireSegmentIndex = indexReflowWireSegments(reflowWireGeometry)
        wireReflowPending = reflowWireGeometry.length > 0
        wireReflow = wireReflowPending ? 0 : 1
    }
    function rebuildWireGeometry() {
        geometryRebuildCount += 1
        wireBucketFullRebuildCount += 1
        const routes = graph.routes || []
        const nodes = graph.nodes || []
        const nodeById = ({})
        const fanOutGroups = ({})
        const fanOutLanes = ({})
        const orthogonalLaneKeys = ({})
        const orthogonalLanes = ({})
        const buckets = ({})
        const segmentBucketKeys = ({})
        const nodeSegmentRefs = ({})
        const next = []
        for (let i = 0; i < nodes.length; ++i) {
            nodeById[String(nodes[i].id || "")] = nodes[i]
            nodeById[String(nodes[i].objectId || "")] = nodes[i]
        }
        for (let i = 0; i < routes.length; ++i) {
            const route = routes[i]
            const key = String(route.sourceEndpointId || route.sourcePortId || route.id || i)
            if (!fanOutGroups[key]) fanOutGroups[key] = []
            fanOutGroups[key].push(String(route.id || i))
        }
        for (const key in fanOutGroups) {
            const ids = fanOutGroups[key]
            ids.sort()
            for (let index = 0; index < ids.length; ++index)
                fanOutLanes[ids[index]] = index - (ids.length - 1) * 0.5
        }
        // Orthogonal routes must remain individually legible even when their
        // source/destination rows happen to align.  Allocate a rail per
        // *endpoint pair*, rather than reusing the route's fan-out lane. An
        // exact duplicate pair deliberately retains its one rail.
        if (graph.workspace && graph.workspace.wireStyle === "orthogonal") {
            for (let routeIndex = 0; routeIndex < routes.length; ++routeIndex) {
                const canonicalSegments = routes[routeIndex].segments || []
                for (let segmentIndex = 0; segmentIndex < canonicalSegments.length; ++segmentIndex) {
                    const key = orthogonalSegmentLaneKey(canonicalSegments[segmentIndex])
                    if (key.length > 1) orthogonalLaneKeys[key] = true
                }
            }
            const keys = Object.keys(orthogonalLaneKeys).sort()
            for (let index = 0; index < keys.length; ++index)
                orthogonalLanes[keys[index]] = index - (keys.length - 1) * 0.5
        }
        for (let i = 0; i < routes.length; ++i) {
            const route = routes[i]
            const input = nodeById[String(route.sourceNodeId || "")]
            const output = nodeById[String(route.destinationNodeId || "")]
            const canonicalSegments = route.segments || []
            if (!input || !output || canonicalSegments.length === 0) continue
            const lane = Number(fanOutLanes[String(route.id || i)] || 0)
            const segments = []
            for (let segmentIndex = 0; segmentIndex < canonicalSegments.length; ++segmentIndex) {
                const canonical = canonicalSegments[segmentIndex]
                const sourceNode = nodeById[String(canonical.sourceNodeId || "")] || input
                const destinationNode = nodeById[String(canonical.destinationNodeId || "")] || output
                const segmentLane = graph.workspace && graph.workspace.wireStyle === "orthogonal"
                    ? Number(orthogonalLanes[orthogonalSegmentLaneKey(canonical)] || 0) : lane
                segments.push(geometryForCanonicalSegment(route.id, canonical, sourceNode, destinationNode,
                    segmentLane, false))
            }
            const entry = routeGeometryEntry(route, segments, lane)
            const entryIndex = next.length
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                addSegmentToBuckets(buckets, segmentBucketKeys, route, entry.routeId, segments[segmentIndex])
                addNodeSegmentRef(nodeSegmentRefs, segments[segmentIndex].sourceNodeId, entryIndex, segmentIndex)
                addNodeSegmentRef(nodeSegmentRefs, segments[segmentIndex].destinationNodeId, entryIndex, segmentIndex)
            }
            next.push(entry)
        }
        wireGeometry = next
        wireBuckets = buckets
        wireSegmentBucketKeys = segmentBucketKeys
        wireNodeSegmentRefs = nodeSegmentRefs
        // Full cache reconstruction is reserved for an actual graph/layout
        // invalidation. Only then hand overlay routes back to the settled
        // Canvas; an ordinary card drop keeps its small final overlay intact.
        if (liveDragRouteSettlePending || (!liveDragNodeId && hasWireOverlayRoutes())) {
            liveDragRouteSettlePending = false
            activeDragRouteIds = ({})
            liveDragRenderNodeId = ""
            liveDragDeltaX = 0
            liveDragDeltaY = 0
            requestSettledWirePaint()
            if (activeDiagram) activeDiagram.requestPaint()
        } else {
            requestWirePaint()
        }
        if (wireReflowPending) {
            wireReflowPending = false
            if (reflowWireGeometry.length > 0) {
                wireReflow = 0
                wireReflowAnimation.restart()
            } else {
                reflowWireGeometry = []
                wireReflow = 1
            }
        }
        if (wireAppearancePending) {
            wireAppearancePending = false
            if (!reducedMotion && Object.keys(appearingWireRouteIds).length > 0) {
                wireAppear = 0
                wireAppearAnimation.restart()
            } else {
                wireAppear = 1
                appearingWireRouteIds = ({})
                // In Reduced Motion, the completed route is immediately
                // truthful and its destination supplies the concise cue.
                beginRouteDestinationAcknowledgement()
            }
        }
    }
    function refreshLiveDragGeometry(nodeId) {
        const movedNode = nodeForId(nodeId)
        if (!movedNode || !nodeIdentity(movedNode)) return 0
        const refs = wireNodeSegmentRefs[String(nodeId || "")]
            || wireNodeSegmentRefs[nodeIdentity(movedNode)]
            || wireNodeSegmentRefs[nodeStorageIdentity(movedNode)] || []
        if (refs.length === 0) return 0
        // The settled geometry and hit structure intentionally remain
        // immutable while a card owns the pointer. activeDiagram adjusts the
        // small incident set with liveDragDeltaX/Y at paint time; rebuilding
        // replacement route/segment arrays here used to allocate a graph-wide
        // array for every sample even though only these endpoints could move.
        liveDragGeometryUpdates += 1
        liveDragAffectedSegments += refs.length
        requestWirePaint()
        return refs.length
    }
    function cloneWireBucketForMutation(nextBuckets, clonedBuckets, key) {
        if (!clonedBuckets[key]) {
            nextBuckets[key] = (nextBuckets[key] || []).slice()
            clonedBuckets[key] = true
        }
        return nextBuckets[key]
    }
    function removeSegmentFromIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                                                  clonedBuckets, cacheKey) {
        const keys = nextSegmentBucketKeys[cacheKey] || []
        let updates = 0
        for (let keyIndex = 0; keyIndex < keys.length; ++keyIndex) {
            const key = keys[keyIndex]
            const bucket = cloneWireBucketForMutation(nextBuckets, clonedBuckets, key)
            let writeIndex = 0
            for (let entryIndex = 0; entryIndex < bucket.length; ++entryIndex) {
                if (String(bucket[entryIndex].cacheKey || "") === String(cacheKey)) {
                    updates += 1
                    continue
                }
                bucket[writeIndex++] = bucket[entryIndex]
            }
            bucket.length = writeIndex
        }
        delete nextSegmentBucketKeys[cacheKey]
        return updates
    }
    function addSegmentToIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                                            clonedBuckets, route, routeId, segment) {
        const cell = 96
        const cacheKey = routeSegmentCacheKey(routeId, segment)
        const keys = []
        let updates = 0
        segment.cacheKey = cacheKey
        for (let bx = Math.floor(segment.bounds.left / cell); bx <= Math.floor(segment.bounds.right / cell); ++bx) {
            for (let by = Math.floor(segment.bounds.top / cell); by <= Math.floor(segment.bounds.bottom / cell); ++by) {
                const key = bx + ":" + by
                const bucket = cloneWireBucketForMutation(nextBuckets, clonedBuckets, key)
                bucket.push({ "route": route, "routeId": routeId,
                    "routeSegmentId": segment.routeSegmentId, "cacheKey": cacheKey, "points": segment.points })
                keys.push(key)
                updates += 1
            }
        }
        nextSegmentBucketKeys[cacheKey] = keys
        return updates
    }
    function canonicalSegmentForCachedSegment(route, segment, segmentIndex) {
        const canonicalSegments = route && route.segments || []
        const cachedId = String(segment && segment.routeSegmentId || "")
        const indexed = canonicalSegments[segmentIndex]
        if (indexed && (!cachedId || String(indexed.id || "") === cachedId)) return indexed
        for (let index = 0; index < canonicalSegments.length; ++index) {
            if (String(canonicalSegments[index].id || "") === cachedId) return canonicalSegments[index]
        }
        return null
    }
    function wireSegmentsHaveSameGeometry(first, second) {
        if (!first || !second) return false
        return String(first.sourceNodeId || "") === String(second.sourceNodeId || "")
            && String(first.destinationNodeId || "") === String(second.destinationNodeId || "")
            && String(first.sourceEndpointId || "") === String(second.sourceEndpointId || "")
            && String(first.destinationEndpointId || "") === String(second.destinationEndpointId || "")
            && Math.abs(Number(first.startX) - Number(second.startX)) < 0.01
            && Math.abs(Number(first.startY) - Number(second.startY)) < 0.01
            && Math.abs(Number(first.endX) - Number(second.endX)) < 0.01
            && Math.abs(Number(first.endY) - Number(second.endY)) < 0.01
            && Boolean(first.hasDetour) === Boolean(second.hasDetour)
            && Boolean(first.underCard) === Boolean(second.underCard)
            && Math.abs(Number(first.detourY) - Number(second.detourY)) < 0.01
    }
    // A card release promotes the existing interaction geometry into its
    // stable presentation cache.  Endpoint routes are always reconsidered;
    // every other cached segment is compared once, only at release, so a card
    // that newly obstructs an unrelated wire gets its detour without reviving
    // a graph-wide rebuild or changing any unaffected route-entry identity.
    function rebuildReleaseWireGeometry(nodeId) {
        const movedNode = nodeForId(nodeId)
        if (!movedNode || !nodeIdentity(movedNode)) return 0
        const movedId = nodeIdentity(movedNode)
        const movedStorageId = nodeStorageIdentity(movedNode)
        const replacements = ({})
        const replacementEntries = ({})
        let incidentUpdates = 0
        let obstacleUpdates = 0
        for (let entryIndex = 0; entryIndex < wireGeometry.length; ++entryIndex) {
            const entry = wireGeometry[entryIndex]
            const cachedSegments = entry && entry.segments || []
            for (let segmentIndex = 0; segmentIndex < cachedSegments.length; ++segmentIndex) {
                const oldSegment = cachedSegments[segmentIndex]
                const canonical = canonicalSegmentForCachedSegment(entry && entry.route, oldSegment, segmentIndex)
                if (!entry || !oldSegment || !canonical) continue
                const sourceNode = nodeForId(canonical.sourceNodeId) || nodeForId(entry.route.sourceNodeId)
                const destinationNode = nodeForId(canonical.destinationNodeId) || nodeForId(entry.route.destinationNodeId)
                if (!sourceNode || !destinationNode) continue
                const newSegment = geometryForCanonicalSegment(entry.routeId, canonical, sourceNode,
                    destinationNode, Number(oldSegment.lane === undefined ? entry.lane : oldSegment.lane), false)
                if (wireSegmentsHaveSameGeometry(oldSegment, newSegment)) continue
                const replacementKey = entryIndex + ":" + segmentIndex
                replacements[replacementKey] = ({ "entryIndex": entryIndex, "segmentIndex": segmentIndex,
                    "entry": entry, "oldSegment": oldSegment, "newSegment": newSegment })
                if (!replacementEntries[entryIndex]) replacementEntries[entryIndex] = cachedSegments.slice()
                replacementEntries[entryIndex][segmentIndex] = newSegment
                const touchesMovedNode = String(oldSegment.sourceNodeId || "") === movedId
                    || String(oldSegment.destinationNodeId || "") === movedId
                    || String(oldSegment.sourceNodeId || "") === movedStorageId
                    || String(oldSegment.destinationNodeId || "") === movedStorageId
                if (touchesMovedNode) ++incidentUpdates
                else ++obstacleUpdates
            }
        }
        const replacementKeys = Object.keys(replacements)
        if (replacementKeys.length === 0) return 0
        // Capture the visible held-drag curve before replacing the cache.
        // The active layer can then glide into the obstacle-aware result
        // instead of visually jumping to it on release.
        if (!reducedMotion) {
            prepareWireReflow(motionStructuralDuration, true)
            wireReflowTouchesSettledRoutes = obstacleUpdates > 0
        } else wireReflowTouchesSettledRoutes = false
        const nextGeometry = wireGeometry.slice()
        for (const entryIndex in replacementEntries) {
            const oldEntry = wireGeometry[Number(entryIndex)]
            nextGeometry[Number(entryIndex)] = routeGeometryEntry(oldEntry.route,
                replacementEntries[entryIndex], Number(oldEntry.lane || 0))
        }
        const nextBuckets = ({})
        const nextSegmentBucketKeys = ({})
        const clonedBuckets = ({})
        for (const key in wireBuckets) nextBuckets[key] = wireBuckets[key]
        for (const key in wireSegmentBucketKeys) nextSegmentBucketKeys[key] = wireSegmentBucketKeys[key]
        let bucketUpdates = 0
        for (let replacementIndex = 0; replacementIndex < replacementKeys.length; ++replacementIndex) {
            const replacement = replacements[replacementKeys[replacementIndex]]
            const oldCacheKey = String(replacement.oldSegment.cacheKey
                || routeSegmentCacheKey(replacement.entry.routeId, replacement.oldSegment))
            bucketUpdates += removeSegmentFromIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                clonedBuckets, oldCacheKey)
            bucketUpdates += addSegmentToIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                clonedBuckets, replacement.entry.route, replacement.entry.routeId, replacement.newSegment)
        }
        wireGeometry = nextGeometry
        wireBuckets = nextBuckets
        wireSegmentBucketKeys = nextSegmentBucketKeys
        incidentWireGeometryUpdateCount += incidentUpdates
        obstacleWireGeometryUpdateCount += obstacleUpdates
        incrementalWireBucketUpdateCount += bucketUpdates
        // Full graph rebuilds consume this pending handoff themselves. The
        // incremental release path owns no rebuild, so start the same
        // presentation-only morph once its replacement cache is complete.
        if (!reducedMotion && wireReflowPending) {
            wireReflowPending = false
            wireReflowAnimation.restart()
        }
        requestWirePaint()
        return replacementKeys.length
    }
    // A temporary section reveal changes one card's measured port anchors.
    // Update only the segments incident to that card and their spatial-index
    // cells. This is intentionally narrower than a dropped-card reflow: a
    // hover disclosure must never reconsider unrelated topology or wires.
    function rebuildIncidentWireGeometry(nodeId) {
        const owner = nodeForId(nodeId)
        if (!owner || !nodeIdentity(owner) || liveDragNodeId) return 0
        const ownerId = nodeIdentity(owner)
        const storageId = nodeStorageIdentity(owner)
        const refs = wireNodeSegmentRefs[ownerId] || wireNodeSegmentRefs[storageId] || []
        if (refs.length === 0) return 0
        const replacementEntries = ({})
        const replacements = []
        const seen = ({})
        for (let refIndex = 0; refIndex < refs.length; ++refIndex) {
            const ref = refs[refIndex] || ({})
            const entryIndex = Number(ref.entryIndex)
            const segmentIndex = Number(ref.segmentIndex)
            const replacementKey = entryIndex + ":" + segmentIndex
            if (seen[replacementKey]) continue
            seen[replacementKey] = true
            const entry = wireGeometry[entryIndex]
            const oldSegment = entry && entry.segments ? entry.segments[segmentIndex] : null
            const canonical = canonicalSegmentForCachedSegment(entry && entry.route, oldSegment, segmentIndex)
            if (!entry || !oldSegment || !canonical) continue
            const sourceNode = nodeForId(canonical.sourceNodeId) || nodeForId(entry.route.sourceNodeId)
            const destinationNode = nodeForId(canonical.destinationNodeId) || nodeForId(entry.route.destinationNodeId)
            if (!sourceNode || !destinationNode) continue
            const newSegment = geometryForCanonicalSegment(entry.routeId, canonical, sourceNode,
                destinationNode, Number(oldSegment.lane === undefined ? entry.lane : oldSegment.lane), false)
            if (wireSegmentsHaveSameGeometry(oldSegment, newSegment)) continue
            if (!replacementEntries[entryIndex]) replacementEntries[entryIndex] = entry.segments.slice()
            replacementEntries[entryIndex][segmentIndex] = newSegment
            replacements.push(({ "entryIndex": entryIndex, "segmentIndex": segmentIndex,
                "entry": entry, "oldSegment": oldSegment, "newSegment": newSegment }))
        }
        if (replacements.length === 0) return 0
        const nextGeometry = wireGeometry.slice()
        for (const entryIndex in replacementEntries) {
            const oldEntry = wireGeometry[Number(entryIndex)]
            nextGeometry[Number(entryIndex)] = routeGeometryEntry(oldEntry.route,
                replacementEntries[entryIndex], Number(oldEntry.lane || 0))
        }
        const nextBuckets = ({})
        const nextSegmentBucketKeys = ({})
        const clonedBuckets = ({})
        for (const key in wireBuckets) nextBuckets[key] = wireBuckets[key]
        for (const key in wireSegmentBucketKeys) nextSegmentBucketKeys[key] = wireSegmentBucketKeys[key]
        let bucketUpdates = 0
        for (let index = 0; index < replacements.length; ++index) {
            const replacement = replacements[index]
            const oldCacheKey = String(replacement.oldSegment.cacheKey
                || routeSegmentCacheKey(replacement.entry.routeId, replacement.oldSegment))
            bucketUpdates += removeSegmentFromIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                clonedBuckets, oldCacheKey)
            bucketUpdates += addSegmentToIncrementalBuckets(nextBuckets, nextSegmentBucketKeys,
                clonedBuckets, replacement.entry.route, replacement.entry.routeId, replacement.newSegment)
        }
        wireGeometry = nextGeometry
        wireBuckets = nextBuckets
        wireSegmentBucketKeys = nextSegmentBucketKeys
        incidentWireGeometryUpdateCount += replacements.length
        incrementalWireBucketUpdateCount += bucketUpdates
        requestWirePaint()
        return replacements.length
    }
    function liveDragSegmentTouches(segment) {
        if (!segment || !(liveDragRenderNodeId || liveDragNodeId)) return false
        const renderId = String(liveDragRenderNodeId || liveDragNodeId || "")
        return String(segment.sourceNodeId || "") === renderId
            || String(segment.destinationNodeId || "") === renderId
    }
    function liveDragSegmentStartX(segment) {
        return Number(segment && segment.startX || 0)
            + (String(segment && segment.sourceNodeId || "")
                === String(liveDragRenderNodeId || liveDragNodeId) ? liveDragDeltaX : 0)
    }
    function liveDragSegmentStartY(segment) {
        return Number(segment && segment.startY || 0)
            + (String(segment && segment.sourceNodeId || "")
                === String(liveDragRenderNodeId || liveDragNodeId) ? liveDragDeltaY : 0)
    }
    function liveDragSegmentEndX(segment) {
        return Number(segment && segment.endX || 0)
            + (String(segment && segment.destinationNodeId || "")
                === String(liveDragRenderNodeId || liveDragNodeId) ? liveDragDeltaX : 0)
    }
    function liveDragSegmentEndY(segment) {
        return Number(segment && segment.endY || 0)
            + (String(segment && segment.destinationNodeId || "")
                === String(liveDragRenderNodeId || liveDragNodeId) ? liveDragDeltaY : 0)
    }
    function beginLiveNodeDrag(nodeData) {
        const id = nodeIdentity(nodeData)
        if (!id) return false
        // A graph/anchor change that was already coalesced before pointer-down
        // must settle before this gesture captures its authoritative card
        // geometry. Otherwise that stale timer can rebuild the full cache
        // midway through a fast drag or immediately after its drop. Flushing
        // it here is deliberately outside the live gesture: all subsequent
        // frames remain incident-only.
        discardQueuedGraphGeometryHandoff()
        flushQueuedWireGeometryRebuild()
        // Snapshot before the pointer may cross the drag threshold. This is a
        // no-op for a click, but it makes the first real drag frame use one
        // stable disclosure/anchor layout rather than a hover transition.
        freezeDisclosureGeometry(nodeData)
        pendingLiveNodeDrag = ({})
        dragFrameRequested = false
        liveDragRouteSettlePending = false
        liveDragDeltaX = 0
        liveDragDeltaY = 0
        liveDragRenderNodeId = id
        // The segment index was built at the last stable graph boundary.
        // Resolve the small incident set once on press; it must not be
        // discovered by traversing every route on every pointer frame.
        const refs = wireNodeSegmentRefs[id] || wireNodeSegmentRefs[nodeStorageIdentity(nodeData)] || []
        const active = ({})
        // Previously released cards can remain in the lightweight overlay
        // until a real topology change.  Starting another drag must not make
        // those already-committed wires disappear from either layer.
        for (const routeId in activeDragRouteIds) active[routeId] = activeDragRouteIds[routeId]
        for (let index = 0; index < refs.length; ++index) {
            const entry = wireGeometry[Number(refs[index].entryIndex)]
            if (entry && entry.routeId) active[String(entry.routeId)] = true
        }
        activeDragRouteIds = active
        liveDragNodeId = id
        // One press-time settled paint removes incident routes from its cache.
        // Thereafter only activeDiagram is invalidated until release.
        requestSettledWirePaint()
        if (activeDiagram) activeDiagram.requestPaint()
        return true
    }
    function isLiveNodeDrag(nodeData) {
        return Boolean(liveDragNodeId) && nodeMatchesId(nodeData, liveDragNodeId)
    }
    function updateLiveNodeDrag(nodeData, x, y) {
        const id = nodeIdentity(nodeData)
        if (!id || !isFinite(x) || !isFinite(y)) return 0
        if (!liveDragNodeId) liveDragNodeId = id
        const position = ({ "x": Number(x), "y": Number(y) })
        // One card owns one gesture.  A direct position avoids cloning a
        // graph-wide JS map for every rendered drag sample.
        liveDragPosition = position
        const settled = settledNodePosition(nodeData, Number(nodeData.x || 0), Number(nodeData.y || 0))
        liveDragDeltaX = Number(position.x) - Number(settled.x)
        liveDragDeltaY = Number(position.y) - Number(settled.y)
        // Test-only measurement can choose to observe each drag sample without
        // adding visual-anchor work to the product pointer path.
        if (portAnchorDiagnosticsEnabled) requestPortAnchorMeasurement()
        return refreshLiveDragGeometry(id)
    }
    function queueLiveNodeDrag(nodeData, x, y, altBypass) {
        const id = nodeIdentity(nodeData)
        if (!id || !isFinite(x) || !isFinite(y) || !isLiveNodeDrag(nodeData)) return false
        pendingLiveNodeDrag = ({ "nodeId": id, "x": Number(x), "y": Number(y),
            "altBypass": Boolean(altBypass) })
        return flushQueuedLiveNodeDrag()
    }
    function flushQueuedLiveNodeDrag() {
        const pending = pendingLiveNodeDrag || ({})
        pendingLiveNodeDrag = ({})
        dragFrameRequested = false
        if (!pending.nodeId) return false
        const nodeData = nodeForId(pending.nodeId)
        if (!nodeData || !isLiveNodeDrag(nodeData)) return false
        updateLiveNodeDrag(nodeData, pending.x, pending.y)
        updateNodeSnapPreview(nodeData, pending.x, pending.y, pending.altBypass)
        liveDragFrameCount += 1
        return true
    }
    function finishLiveNodeDrag(nodeData, x, y, persist, altBypass) {
        const id = nodeIdentity(nodeData)
        if (!id) return false
        flushQueuedLiveNodeDrag()
        const livePosition = nodePosition(nodeData, x, y)
        x = Number(livePosition.x)
        y = Number(livePosition.y)
        updateLiveNodeDrag(nodeData, x, y)
        // Test-only callers that explicitly suppress persistence keep their
        // historical exact-coordinate fixture semantics. Product drags use
        // the same assistive candidate shown during the gesture.
        const candidate = persist === false
            ? ({ "active": false, "changed": false, "x": Number(x), "y": Number(y) })
            : nodeSnapCandidate(nodeData, x, y, Boolean(altBypass))
        const settledX = Number(candidate.x)
        const settledY = Number(candidate.y)
        if (candidate.changed) markNodeSnapSettling(nodeData)
        // The release candidate is the only visual destination. Promote it
        // before persistence, then derive the small incident cache from that
        // same coordinate. This eliminates an old-position fallback between
        // the final drag frame and the saved presentation state.
        liveDragPosition = ({ "x": settledX, "y": settledY })
        noteNodePosition(nodeStorageIdentity(nodeData), settledX, settledY, false)
        // A fixture can deliberately skip persistence; that legacy path
        // deliberately exercises a full cache rebuild. Product placement
        // commits instead preserve every unaffected route entry and update
        // only endpoint geometry or routes whose obstacle plan changed.
        if (persist === false) {
            releaseAnchorReflowActive = false
            pendingReleaseAnchorReflowNodeIds = ({})
            liveDragPosition = ({})
            liveDragRouteSettlePending = true
            liveDragNodeId = ""
            releaseDisclosureGeometry(nodeData, false)
            clearNodeSnapPreview()
            rebuildWireGeometry()
            return true
        }
        const obstacleUpdatesBeforeRelease = obstacleWireGeometryUpdateCount
        rebuildReleaseWireGeometry(id)
        layoutCommitAnchorSuppressionNodeId = id
        layoutCommitAnchorSuppressionTimer.restart()
        releaseAnchorReflowActive = true
        pendingReleaseAnchorReflowNodeIds = ({})
        liveDragPosition = ({})
        liveDragRouteSettlePending = false
        liveDragNodeId = ""
        liveDragRenderNodeId = ""
        liveDragDeltaX = 0
        liveDragDeltaY = 0
        releaseDisclosureGeometry(nodeData)
        clearNodeSnapPreview()
        // An endpoint stays on the active interaction layer until a real
        // topology boundary.  A newly rerouted non-endpoint wire belongs to
        // the settled layer, so repaint that texture once after the release
        // state is cleared. Ordinary endpoint-only drops keep it asleep.
        if (obstacleWireGeometryUpdateCount > obstacleUpdatesBeforeRelease)
            requestSettledWirePaint()
        queueNodePlacementPersistence(nodeData, settledX, settledY, Boolean(nodeData.pinned))
        return true
    }
    function queueNodePlacementPersistence(nodeData, x, y, pinned) {
        if (!nodeData || !nodeData.objectId) return false
        const objectId = String(nodeData.objectId)
        const next = (pendingNodePlacementPersistences || []).slice()
        let replaced = false
        for (let index = 0; index < next.length; ++index) {
            if (String(next[index].objectId || "") !== objectId) continue
            next[index] = ({ "objectId": objectId, "x": Number(x), "y": Number(y),
                "pinned": Boolean(pinned) })
            replaced = true
            break
        }
        if (!replaced) next.push(({ "objectId": objectId, "x": Number(x), "y": Number(y),
            "pinned": Boolean(pinned) }))
        pendingNodePlacementPersistences = next
        nodePlacementPersistenceTimer.restart()
        return true
    }
    function cancelLiveNodeDrag(nodeData) {
        if (!isLiveNodeDrag(nodeData)) return
        pendingLiveNodeDrag = ({})
        dragFrameRequested = false
        liveDragPosition = ({})
        liveDragRouteSettlePending = true
        liveDragNodeId = ""
        releaseAnchorReflowActive = false
        pendingReleaseAnchorReflowNodeIds = ({})
        releaseDisclosureGeometry(nodeData, false)
        clearNodeSnapPreview()
        rebuildWireGeometry()
    }
    function restartWireMotion() {
        // A topology transition can retire route records, but it must never
        // hide and redraw every surviving route. Newly added IDs use the
        // per-route wireAppear lifecycle; existing IDs remain continuously
        // visible. This also makes an accidental layout acknowledgement
        // harmless rather than turning it into a global wire flash.
        wireRevealAnimation.stop()
        wireReveal = 1
        if (retiringWireGeometry.length > 0) {
            wireRetire = 0
            wireRetireAnimation.restart()
        } else wireRetire = 1
    }
    function fitGraph() {
        // The scene has a logical 64 px trailing margin and physical cards
        // start at least 80 px in, so Fit always leaves a calm visual border
        // instead of clipping the output card or its endpoint dots.
        const margin = 48
        const horizontal = Math.max(0.42, Math.min(1.15, (graphViewport.width - margin * 2) / scene.width))
        const vertical = Math.max(0.42, Math.min(1.15, (graphViewport.height - margin * 2) / scene.height))
        zoom = Math.min(horizontal, vertical)
        graphViewport.contentX = 0
        graphViewport.contentY = 0
        saveTimer.restart()
        notice = "Graph fit to the current workspace."
        noticeError = false
    }
    function focusCurrentSelection() {
        const selectedId = inspectedRoute && inspectedRoute.id ? String(inspectedRoute.id) : ""
        const geometry = wireGeometry.filter(function(entry) { return entry.routeId === selectedId })[0]
        if (!geometry) { fitGraph(); return }
        const focusX = Number(geometry.focusX === undefined ? (geometry.startX + geometry.endX) * 0.5 : geometry.focusX)
        const focusY = Number(geometry.focusY === undefined ? (geometry.startY + geometry.endY) * 0.5 : geometry.focusY)
        graphViewport.contentX = Math.max(0, (focusX * zoom) - graphViewport.width * 0.5)
        graphViewport.contentY = Math.max(0, (focusY * zoom) - graphViewport.height * 0.5)
        saveTimer.restart()
        notice = "Centered the selected route."
        noticeError = false
    }
    function applyAutoLayout() {
        if (!graph.editable || (graph.workspace && graph.workspace.layoutLocked)) return false
        if (routingActive) cancelRouting("Connection cancelled while Auto Layout rearranges the graph.", true)
        // The backend remains authoritative and commits immediately.  This
        // flag only gives the already-cached presentation geometry/card
        // bindings the shared layout timing while that change arrives.
        autoLayoutMotionActive = true
        layoutMotionTimer.restart()
        const result = backendObject.signalFlowAutoLayout()
        announce(result, "Auto-layout was not applied.")
        if (!result || !result.success) {
            autoLayoutMotionActive = false
            layoutMotionTimer.stop()
        }
        return Boolean(result && result.success)
    }
    function sourcePortFromLearning(learning) {
        if (!learning || String(learning.kind || "") !== "signal-flow") return null
        const sourceId = String(learning.sourcePortId || "")
        if (sourceId.length === 0) return null
        const inputPorts = ports("input")
        for (let index = 0; index < inputPorts.length; ++index)
            if (String(inputPorts[index].id || "") === sourceId) return inputPorts[index]
        return null
    }
    function focusSourcePort(port) {
        const input = node("input")
        const position = nodePosition(input, 50, 150)
        graphViewport.contentX = Math.max(0, position.x * zoom - 28)
        graphViewport.contentY = Math.max(0, position.y * zoom - 28)
        saveTimer.restart()
    }
    function acceptSignalFlowLearnedSource(learning) {
        const port = sourcePortFromLearning(learning)
        backendObject.cancelInputLearning()
        deckSourceLearnDialog.close()
        if (!port) {
            notice = "The learned physical control is no longer available in this Signal Flow scope. Retry learning."
            noticeError = true
            return false
        }
        selectSource(port)
        learnedSourcePortId = String(port.id)
        focusSourcePort(port)
        notice = "Found " + (learning.sourceLabel || port.label) + ". Choose a highlighted compatible destination."
        noticeError = false
        return true
    }
    function highlightDestinationLearnedRoute(learning) {
        const destination = pendingLearnDestination
        const sourceId = String(learning && learning.sourcePortId || "")
        if (!destination || !destination.id || sourceId.length === 0) return false
        graph = backendObject.signalFlowGraph
        const inputPorts = ports("input")
        let learnedPort = null
        for (let index = 0; index < inputPorts.length; ++index) {
            if (String(inputPorts[index].id || "") === sourceId) {
                learnedPort = inputPorts[index]
                break
            }
        }
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            if (String(routes[index].sourcePortId || "") !== sourceId
                    || String(routes[index].destinationPortId || "") !== String(destination.id)) continue
            source = learnedPort || ({})
            inspectedRoute = routes[index]
            pendingLearnDestination = ({})
            focusCurrentSelection()
            notice = "Learned route highlighted in Signal Flow."
            noticeError = false
            return true
        }
        return false
    }
    function keepLearnedRouteHighlighted(sourceBeforeConnect, destinationPortId) {
        if (learnedSourcePortId.length === 0
                || String(sourceBeforeConnect && sourceBeforeConnect.id || "") !== learnedSourcePortId) return false
        graph = backendObject.signalFlowGraph
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            if (String(routes[index].sourcePortId || "") !== String(sourceBeforeConnect.id || "")
                    || String(routes[index].destinationPortId || "") !== String(destinationPortId || "")) continue
            source = sourceBeforeConnect
            inspectedRoute = routes[index]
            learnedSourcePortId = ""
            focusCurrentSelection()
            notice = "Route created from the learned source and highlighted in Signal Flow."
            noticeError = false
            return true
        }
        return false
    }
    function processorEnabled(kind) {
        const details = inspectedRoute && inspectedRoute.processorDetails ? inspectedRoute.processorDetails : []
        for (let i = 0; i < details.length; ++i) if (details[i].semantic === kind) return true
        return false
    }
    function processorDetail(kind) {
        const details = inspectedRoute && inspectedRoute.processorDetails ? inspectedRoute.processorDetails : []
        for (let i = 0; i < details.length; ++i) if (details[i].semantic === kind) return details[i]
        return ({})
    }
    function processorIsShared(kind) { return Boolean(processorDetail(kind).shared) }
    function shareableAxisRoutes(kind) {
        const currentId = inspectedRoute && inspectedRoute.id ? String(inspectedRoute.id) : ""
        const detail = processorDetail(kind)
        const sharedId = detail && detail.shared ? String(detail.id || "") : ""
        const ownerAxis = detail && detail.shared ? Number(detail.sharedOwnerAxis) : -1
        const seen = ({})
        const owner = []
        const selected = []
        const rest = []
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            if (!route.enabled || route.kind !== "axis" || !route.id) continue
            const key = String(route.sourcePortId || route.id)
            if (seen[key]) continue
            seen[key] = true
            const members = route.processorDetails || []
            let belongsToExistingShared = false
            for (let detailIndex = 0; detailIndex < members.length; ++detailIndex)
                if (sharedId && String(members[detailIndex].id || "") === sharedId) belongsToExistingShared = true
            const choice = ({
                "id": String(route.id),
                "label": route.sourceLabel + " → " + route.destinationLabel,
                "selected": String(route.id) === currentId || belongsToExistingShared,
                "axis": Number(String(route.sourcePortId || "axis:-1").split(":")[1])
            })
            if (choice.axis === ownerAxis) owner.push(choice)
            else if (String(route.id) === currentId) selected.push(choice)
            else rest.push(choice)
        }
        return owner.concat(selected).concat(rest)
    }
    function applyBackendFocus() {
        const target = backendObject ? String(backendObject.signalFlowFocusObjectId || "") : ""
        if (!target) return
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            if (String(route.id || "") === target) {
                inspectedRoute = route
                source = ({})
                inspectedNode = ({})
                notice = "Opened the route selected from App Health."
                noticeError = false
                Qt.callLater(focusCurrentSelection)
                return
            }
            const details = route.processorDetails || []
            for (let detailIndex = 0; detailIndex < details.length; ++detailIndex) {
                if (String(details[detailIndex].id || "") !== target) continue
                inspectedRoute = route
                source = ({})
                inspectedNode = ({})
                notice = "Opened the processor selected from App Health."
                noticeError = false
                Qt.callLater(focusCurrentSelection)
                return
            }
        }
    }
    function announce(result, fallback) {
        notice = result && result.message ? result.message : fallback
        noticeError = !(result && result.success)
    }
    function selectSource(port) {
        return armSource(port, false)
    }
    function destinationPortById(portId) {
        const output = node("output")
        const all = portsForNode(output)
        for (let index = 0; index < all.length; ++index) {
            if (String(all[index].id || "") === String(portId || "")) return all[index]
        }
        return null
    }
    function commitConnection(port, decision) {
        const sourceBeforeConnect = interactionSource()
        if (!sourceBeforeConnect || !sourceBeforeConnect.id || !port || !port.id) {
            notice = "Select an input and a compatible destination first."
            noticeError = true
            return false
        }
        if (!routingContextIsCurrent()) {
            cancelRouting("The graph changed while the connection was being prepared. No route was changed.", true)
            return false
        }
        const sourceEndpoint = String(sourceBeforeConnect.endpointId || sourceBeforeConnect.id || "")
        const destinationEndpoint = String(port.endpointId || port.id || "")
        const requestedDecision = String(decision || "")
        const preview = backendObject.signalFlowPreviewConnection(sourceEndpoint, destinationEndpoint,
            requestedDecision, Number(interaction.expectedRevision || 0))
        if (!preview || !preview.success) {
            if (preview && preview.requiresCollisionDecision && requestedDecision.length === 0) {
                connectionPreview = ({ "portId": String(port.id || ""), "ownerNodeId": portOwnerNodeId(port),
                    "compatible": true, "canCommit": false, "requiresCollisionDecision": true,
                    "title": String(preview.title || "Resolve destination collision"),
                    "message": String(preview.message || "Choose an explicit collision policy."),
                    "sourcePortId": String(sourceBeforeConnect.id || "") })
                updateInteraction({ "mode": "TARGET_PREVIEW", "target": port })
                conflictDialog.destination = destinationFor(port)
                conflictDialog.label = String(port.label || "destination")
                conflictDialog.destinationPortId = String(port.id || "")
                const options = preview.collisionOptions || []
                conflictDialog.mixerAllowed = options.indexOf("average") >= 0
                conflictDialog.open()
                notice = String(preview.message || "Choose an explicit collision policy.")
                noticeError = false
            } else {
                announce(preview, "Connection was not applied.")
                if (preview && String(preview.title || "") === "Graph context changed")
                    cancelRouting("The graph changed while the connection was being prepared. No route was changed.", true)
            }
            return false
        }
        connectionCommitInFlight = true
        const result = backendObject.connectSignalFlowEndpoints(sourceEndpoint, destinationEndpoint,
            requestedDecision, Number(interaction.expectedRevision || 0))
        connectionCommitInFlight = false
        announce(result, "Connection was not applied.")
        if (result && result.success) {
            cancelRouting("", false)
            if (!keepLearnedRouteHighlighted(sourceBeforeConnect, port.id)) inspectedRoute = ({})
            return true
        }
        return false
    }
    function connect(port, replace) {
        return commitConnection(port, replace ? "replace" : "")
    }
    function connectDragged(sourcePort, destinationPort) {
        if (!sourcePort || !destinationPort) return false
        if (!routingActive || !samePort(sourcePort, interactionSource())) {
            if (!armSource(sourcePort, true)) return false
        }
        previewDestination(destinationPort)
        return commitConnection(destinationPort, "")
    }
    function connectWithMixer(modeName) {
        const destinationPort = destinationPortById(conflictDialog.destinationPortId)
        if (!destinationPort) {
            notice = "The collision target changed. Review the current graph and retry."
            noticeError = true
            cancelRouting("", false)
            return false
        }
        return commitConnection(destinationPort, modeName)
    }
    function explainRoute() {
        if (!inspectedRoute || !inspectedRoute.id) return
        routeExplanation = backendObject.signalFlowExplainRoute(String(inspectedRoute.id))
        explainDialog.open()
    }
    function toggleProcessor(kind) {
        if (!inspectedRoute || !inspectedRoute.id) return
        if (processorIsShared(kind)) {
            splitSharedProcessor(kind)
            return
        }
        const detail = processorDetail(kind)
        const enabled = processorEnabled(kind)
        const segmentId = selectedProcessorSegmentId()
        const result = enabled
            ? backendObject.signalFlowRemoveOrBypassProcessor(String(detail.id || ""), Number(graph.revision || 0))
            : backendObject.signalFlowInsertProcessor(segmentId, kind, Number(graph.revision || 0))
        announce(result, "Processor change was not applied.")
        if (result && result.success) processorDialog.close()
    }
    function selectedProcessorSegmentId() {
        const segments = inspectedRoute && inspectedRoute.segments ? inspectedRoute.segments : []
        for (let index = 0; index < segments.length; ++index)
            if (String(segments[index].id || "") === String(selectedSegmentId || "")) return selectedSegmentId
        return segments.length > 0 ? String(segments[0].id || "") : ""
    }
    function processorCanInsert(kind) {
        const segmentId = selectedProcessorSegmentId()
        if (!segmentId) return false
        const available = backendObject.signalFlowAvailableProcessorsForSegment(segmentId,
            Number(graph.revision || 0))
        return available.some(function(item) { return String(item.key || "") === String(kind || "") })
    }
    function processorActionAvailable(kind) {
        return processorEnabled(kind) || processorIsShared(kind) || processorCanInsert(kind)
    }
    function splitSharedProcessor(kind) {
        if (!inspectedRoute || !inspectedRoute.id) return
        const result = backendObject.signalFlowSplitSharedProcessor(String(inspectedRoute.id), kind,
            Number(graph.revision || 0))
        announce(result, "Shared processor was not split.")
        if (result && result.success) processorDialog.close()
    }
    function removeSelectedProcessor() {
        if (!inspectedNode || inspectedNode.kind !== "processor" || !inspectedNode.objectId) return
        const route = inspectedRoute && inspectedRoute.id ? inspectedRoute : routeForProcessorNode(inspectedNode)
        if (inspectedNode.semantic === "mixer") {
            deckMixerDialog.mixerId = String(inspectedNode.objectId)
            deckMixerDialog.routeId = String(route.id || "")
            deckMixerDialog.currentMode = String(inspectedNode.mixerMode || "Average").toLowerCase().replace(" ", "-")
            deckMixerDialog.open()
            return
        }
        const result = inspectedNode.shared
            ? backendObject.signalFlowRemoveSharedProcessorChannel(String(inspectedNode.objectId), String(route.id || ""),
                Number(graph.revision || 0))
            : backendObject.signalFlowRemoveOrBypassProcessor(String(inspectedNode.objectId),
                Number(graph.revision || 0))
        announce(result, "Processor was not removed.")
        if (result && result.success) {
            selectedSegmentId = ""
            inspectedNode = ({})
        }
    }
    function openShareProcessorDialog() {
        if (!inspectedRoute || !inspectedRoute.id) return
        shareDialog.prepare()
        if (shareDialog.processorKinds.length > 0) shareDialog.open()
        else {
            notice = "Add or configure a processor on this axis before sharing it."
            noticeError = true
        }
    }
    function startLearning(destination) {
        if (!destination) return
        let started = false
        if (destination.kind === "axis") started = backendObject.startAxisLearning(String(destination.technicalLabel))
        else if (destination.kind === "button") started = backendObject.startButtonLearning(Number(destination.index))
        pendingLearnDestination = started ? destination : ({})
        notice = started ? "Learning is armed. Move the physical control you want to route."
                         : "Learning needs a connected physical input and an available virtual destination."
        noticeError = !started
        if (started) learnDialog.close()
    }
    function startSignalFlowSourceLearning() {
        const started = backendObject.startSignalFlowInputLearning()
        notice = started ? "Learning is armed. Move or press the physical control you want to route."
                         : "Learning needs a connected physical input."
        noticeError = !started
        if (started) deckSourceLearnDialog.open()
        return started
    }
    function preview(modeName) {
        defaultsDialog.mode = modeName
        defaultsDialog.preview = backendObject.signalFlowDefaultPreview(modeName)
        defaultsDialog.open()
    }
    function disconnectSelected() {
        if (!inspectedRoute || !inspectedRoute.id || mode !== "configured") return false
        const result = backendObject.signalFlowDisconnect(String(inspectedRoute.id), Number(graph.revision || 0))
        announce(result, "Route was not disconnected.")
        if (result && result.success) inspectedRoute = ({})
        return Boolean(result && result.success)
    }
    function restoreWorkspace() {
        if (workspaceRestored || !graph.workspace) return
        zoom = Number(graph.workspace.zoom || 1)
        graphViewport.contentX = Math.max(0, Number(graph.workspace.panX || 0))
        graphViewport.contentY = Math.max(0, Number(graph.workspace.panY || 0))
        inspectorPositionX = Number(graph.workspace.inspectorX === undefined ? -1 : graph.workspace.inspectorX)
        inspectorPositionY = Number(graph.workspace.inspectorY === undefined ? -1 : graph.workspace.inspectorY)
        blockLibraryPositionX = Number(graph.workspace.blockLibraryX === undefined ? -1 : graph.workspace.blockLibraryX)
        blockLibraryPositionY = Number(graph.workspace.blockLibraryY === undefined ? -1 : graph.workspace.blockLibraryY)
        graphSettingsPositionX = Number(graph.workspace.graphSettingsX === undefined ? -1 : graph.workspace.graphSettingsX)
        graphSettingsPositionY = Number(graph.workspace.graphSettingsY === undefined ? -1 : graph.workspace.graphSettingsY)
        workspaceAnnotations = graph.workspace.annotations || []
        workspaceRestored = true
    }
    function ensureInspectorPosition() {
        if (inspectorPositionX >= 0 && inspectorPositionY >= 0) return
        const selectedNode = inspectedPortOwner && inspectedPortOwner.id ? inspectedPortOwner
            : inspectedNode && inspectedNode.id ? inspectedNode : ({})
        const selectedPosition = selectedNode && selectedNode.id
            ? nodePosition(selectedNode, 0, 0) : ({ "x": root.width * 0.55, "y": root.height * 0.25 })
        inspectorPositionX = Math.max(deck.space16, Math.min(root.width - 376,
            Number(selectedPosition.x || 0) + 320))
        inspectorPositionY = Math.max(deck.space16, Math.min(root.height - 320,
            Number(selectedPosition.y || 0) + deck.space16))
    }
    function persistInspectorPosition() {
        return backendObject.signalFlowSaveWorkspaceSilently(workspaceSnapshot({
            "inspectorX": inspectorPositionX, "inspectorY": inspectorPositionY
        }))
    }
    function persistFloatingPanelPositions() {
        return backendObject.signalFlowSaveWorkspaceSilently(workspaceSnapshot({
            "blockLibraryX": blockLibraryPositionX, "blockLibraryY": blockLibraryPositionY,
            "graphSettingsX": graphSettingsPositionX, "graphSettingsY": graphSettingsPositionY
        }))
    }
    // Persist floating-panel coordinates relative to the Signal Flow surface,
    // but convert them to the application overlay before rendering. The Deck
    // page occupies the content pane beside the navigation rail; using a
    // surface-local x directly in Overlay.overlay made a right-docked panel
    // drift into the center of the application.
    function overlayOrigin() {
        return root.mapToItem(Overlay.overlay, 0, 0)
    }
    function ensureBlockLibraryPosition() {
        if (blockLibraryPositionX >= 0 && blockLibraryPositionY >= 0) return
        blockLibraryPositionX = Math.max(deck.space16, root.width - Math.min(438, root.width - 32) - deck.space16)
        blockLibraryPositionY = Math.max(deck.space16, deck.space16 + deck.compactControlHeight * 2 + deck.space12)
    }
    function ensureGraphSettingsPosition() {
        if (graphSettingsPositionX >= 0 && graphSettingsPositionY >= 0) return
        graphSettingsPositionX = Math.max(deck.space16, root.width - Math.min(420, root.width - 32) - deck.space16)
        graphSettingsPositionY = Math.max(deck.space16, deck.space16 + deck.compactControlHeight * 2 + deck.space16)
    }
    function persistAnnotations() {
        const saved = backendObject.signalFlowSaveWorkspaceSilently(workspaceSnapshot({
            "annotations": workspaceAnnotations
        }))
        if (!saved) {
            notice = "Workspace annotation could not be saved."
            noticeError = true
        }
        return saved
    }
    function annotationPosition(entry) {
        if (!entry) return ({ "x": 0, "y": 0 })
        const objectId = String(entry.attachedObjectId || "")
        if (objectId.length > 0) {
            const target = nodeForId(objectId)
            if (target && target.id) {
                const position = nodePosition(target, Number(target.x || 0), Number(target.y || 0))
                return ({ "x": Number(position.x || 0) + Number(entry.x || 0),
                    "y": Number(position.y || 0) + Number(entry.y || 0) })
            }
        }
        const routeId = String(entry.attachedRouteId || "")
        if (routeId.length > 0) {
            const entryGeometry = (wireGeometry || []).filter(function(candidate) {
                return String(candidate.routeId || "") === routeId
            })[0]
            const segment = entryGeometry && entryGeometry.segments && entryGeometry.segments[0]
            if (segment) return ({ "x": (Number(segment.startX) + Number(segment.endX)) * 0.5 + Number(entry.x || 0),
                "y": (Number(segment.startY) + Number(segment.endY)) * 0.5 + Number(entry.y || 0) })
        }
        return ({ "x": Number(entry.x || 0), "y": Number(entry.y || 0) })
    }
    function addAnnotation(kind, x, y) {
        if (kind !== "note" && kind !== "group") return false
        const id = kind + ":" + Date.now().toString(36) + ":" + Math.floor(Math.random() * 100000).toString(36)
        const entry = ({ "id": id, "kind": kind, "title": kind === "note" ? "Text note" : "Group / Section",
            "body": kind === "note" ? "Add a workspace note…" : "", "x": Number(x || 180), "y": Number(y || 180),
            "width": kind === "note" ? 240 : 360, "height": kind === "note" ? 132 : 220,
            "attachedObjectId": "", "attachedRouteId": "", "moveContents": false })
        workspaceAnnotations = (workspaceAnnotations || []).concat([entry])
        inspectedAnnotation = entry
        inspectorOpen = true
        ensureInspectorPosition()
        persistAnnotations()
        return true
    }
    function updateAnnotation(id, changes, persist) {
        const next = []
        let selected = ({})
        const values = workspaceAnnotations || []
        for (let index = 0; index < values.length; ++index) {
            const current = values[index]
            const updated = ({})
            for (const key in current) updated[key] = current[key]
            if (String(current.id || "") === String(id || "")) {
                for (const key in changes) updated[key] = changes[key]
                selected = updated
            }
            next.push(updated)
        }
        workspaceAnnotations = next
        if (selected.id) inspectedAnnotation = selected
        return persist === false ? true : persistAnnotations()
    }
    function annotationContentOrigins(group) {
        const origins = ({})
        if (!group || group.kind !== "group" || !group.moveContents) return origins
        const left = Number(group.x || 0)
        const top = Number(group.y || 0)
        const right = left + Number(group.width || 0)
        const bottom = top + Number(group.height || 0)
        const values = workspaceAnnotations || []
        for (let index = 0; index < values.length; ++index) {
            const candidate = values[index]
            if (String(candidate.id || "") === String(group.id || "")) continue
            if (candidate.attachedObjectId || candidate.attachedRouteId) continue
            const x = Number(candidate.x || 0)
            const y = Number(candidate.y || 0)
            if (x >= left && x <= right && y >= top && y <= bottom)
                origins[String(candidate.id || "")] = ({ "x": x, "y": y })
        }
        return origins
    }
    function moveAnnotation(id, x, y, contentOrigins, persist, contentOriginX, contentOriginY) {
        const next = []
        let selected = ({})
        const values = workspaceAnnotations || []
        let originX = contentOriginX === undefined ? x : Number(contentOriginX)
        let originY = contentOriginY === undefined ? y : Number(contentOriginY)
        if (contentOriginX === undefined || contentOriginY === undefined) {
            for (let index = 0; index < values.length; ++index) {
                if (String(values[index].id || "") === String(id || "")) {
                    originX = Number(values[index].x || 0)
                    originY = Number(values[index].y || 0)
                    break
                }
            }
        }
        for (let index = 0; index < values.length; ++index) {
            const current = values[index]
            const updated = ({})
            for (const key in current) updated[key] = current[key]
            if (String(current.id || "") === String(id || "")) {
                updated.x = x
                updated.y = y
                selected = updated
            } else if (contentOrigins && contentOrigins[String(current.id || "")]) {
                const origin = contentOrigins[String(current.id || "")]
                updated.x = Number(origin.x || 0) + x - originX
                updated.y = Number(origin.y || 0) + y - originY
            }
            next.push(updated)
        }
        workspaceAnnotations = next
        if (selected.id) inspectedAnnotation = selected
        return persist === false ? true : persistAnnotations()
    }
    function removeAnnotation(id) {
        workspaceAnnotations = (workspaceAnnotations || []).filter(function(entry) {
            return String(entry.id || "") !== String(id || "")
        })
        if (String(inspectedAnnotation.id || "") === String(id || "")) inspectedAnnotation = ({})
        return persistAnnotations()
    }
    function refreshBlockLibrary() {
        blockLibraryCatalog = backendObject && backendObject.signalFlowProcessorCatalog
            ? backendObject.signalFlowProcessorCatalog() || [] : []
        rebuildBlockLibraryEntries()
        return blockLibraryCatalog
    }
    function processorLibraryCategory(processor) {
        const key = normalized(processor && (processor.key || processor.kind || processor.id))
        if (key.indexOf("mix") >= 0 || key.indexOf("gate") >= 0 || key.indexOf("merge") >= 0)
            return "Merge & Logic"
        if (key.indexOf("button") >= 0 || key.indexOf("pov") >= 0 || key.indexOf("toggle") >= 0)
            return "Buttons & POV"
        if (key.indexOf("condition") >= 0 || key.indexOf("switch") >= 0)
            return "Conditional"
        return "Axis & Response"
    }
    function libraryEntryMatches(entry) {
        const needle = normalized(blockLibraryQuery)
        if (!needle.length) return true
        const haystack = normalized((entry.label || "") + " " + (entry.category || "") + " "
            + (entry.aliases || "") + " " + (entry.kind || "") + " "
            + (entry.processorType || "") + " " + (entry.detail || ""))
        return haystack.indexOf(needle) >= 0
    }
    function buildBlockLibraryEntries() {
        const entries = []
        const nodes = (graph.nodes || []).slice().filter(function(nodeData) {
            return String(nodeData.kind || "") === "input" || String(nodeData.kind || "") === "output"
        }).sort(function(left, right) { return String(left.label || "").localeCompare(String(right.label || "")) })
        for (let index = 0; index < nodes.length; ++index) {
            const nodeData = nodes[index]
            const input = String(nodeData.kind || "") === "input"
            entries.push({ "type": "node", "id": String(nodeData.id || nodeData.objectId || ""),
                "node": nodeData, "kind": input ? "physical-input" : "virtual-output",
                "category": input ? "INPUTS" : "OUTPUTS", "label": String(nodeData.label || (input ? "Physical Input" : "Virtual Output")),
                "aliases": input ? "physical device joystick controller input on canvas" : "virtual output vjoy bf6 output on canvas" })
        }
        const processors = (blockLibraryCatalog || []).slice().sort(function(left, right) {
            return String(left.label || left.key || "").localeCompare(String(right.label || right.key || ""))
        })
        for (let index = 0; index < processors.length; ++index) {
            const processor = processors[index]
            entries.push({ "type": "processor", "id": String(processor.key || processor.id || ""),
                "kind": String(processor.key || processor.id || ""), "processorType": String(processor.processorType || processor.key || processor.id || ""),
                "processor": processor, "category": "PROCESSORS / " + String(processor.category || processorLibraryCategory(processor)),
                "label": String(processor.label || processor.key || "Processor"),
                "detail": String(processor.detail || "Canonical processor"),
                "aliases": String(processor.aliases || "processor transform insert signal route") })
        }
        entries.push({ "type": "annotation", "id": "note", "kind": "note", "category": "OTHER",
            "label": "Text Note", "aliases": "annotation comment documentation presentation" })
        entries.push({ "type": "annotation", "id": "group", "kind": "group", "category": "OTHER",
            "label": "Group / Section", "aliases": "annotation organize presentation section" })
        return entries.filter(function(entry) { return libraryEntryMatches(entry) })
    }
    function rebuildBlockLibraryEntries() {
        const next = buildBlockLibraryEntries()
        blockLibraryEntries = next
        blockLibrarySelectionIndex = Math.max(0, Math.min(Math.max(0, next.length - 1),
            Number(blockLibrarySelectionIndex || 0)))
        return blockLibraryEntries
    }
    function libraryEntries() {
        // Return one retained model identity. A QML Repeater treats a new
        // array as a new model and will destroy a live DragHandler source.
        return blockLibraryEntries || []
    }
    function selectedLibraryEntry() {
        const entries = libraryEntries()
        if (entries.length === 0) return ({})
        blockLibrarySelectionIndex = Math.max(0, Math.min(entries.length - 1, Number(blockLibrarySelectionIndex || 0)))
        return entries[blockLibrarySelectionIndex]
    }
    function moveLibrarySelection(delta) {
        const entries = libraryEntries()
        if (entries.length === 0) return false
        blockLibrarySelectionIndex = (Number(blockLibrarySelectionIndex || 0) + Number(delta || 0) + entries.length) % entries.length
        return true
    }
    function armLibraryEntry(entry) {
        if (!entry || !entry.type) return false
        armedLibraryEntry = entry
        if (entry.type === "processor") {
            cacheLibraryProcessorCompatibility(entry)
            pendingProcessorRouteId = ""
            pendingProcessorSegmentId = ""
            notice = "Place " + entry.label + " on a highlighted compatible signal route. Esc cancels."
        } else if (entry.type === "node") {
            notice = entry.label + " is an existing canonical block. Place it to reposition its workspace card. Esc cancels."
        } else {
            notice = "Place " + entry.label + " on the workspace. Esc cancels."
        }
        noticeError = false
        return true
    }
    function cancelLibraryPlacement(preserveNotice) {
        if (!armedLibraryEntry || !armedLibraryEntry.type) return false
        armedLibraryEntry = ({})
        libraryProcessorCompatibility = ({})
        pendingProcessorRouteId = ""
        pendingProcessorSegmentId = ""
        if (!preserveNotice) {
            notice = ""
            noticeError = false
        }
        return true
    }
    function cacheLibraryProcessorCompatibility(entry) {
        if (!entry || entry.type !== "processor") return false
        const kind = String(entry.id || entry.processorType || "")
        const revision = Number(graph.revision || 0)
        const cached = libraryProcessorCompatibility || ({})
        if (String(cached.kind || "") === kind && Number(cached.revision || -1) === revision)
            return true
        const compatibleSegments = ({})
        const routes = graph.routes || []
        for (let routeIndex = 0; routeIndex < routes.length; ++routeIndex) {
            const segments = routes[routeIndex].segments || []
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                const segmentId = String(segments[segmentIndex].id || "")
                if (!segmentId.length) continue
                const available = backendObject.signalFlowAvailableProcessorsForSegment(segmentId, revision) || []
                compatibleSegments[segmentId] = available.some(function(processor) {
                    return String(processor.key || processor.id || "") === kind
                })
            }
        }
        libraryProcessorCompatibility = ({ "kind": kind, "revision": revision,
            "segments": compatibleSegments })
        return true
    }
    function libraryProcessorSegmentIsCompatible(entry, segmentId) {
        const kind = String(entry && (entry.id || entry.processorType) || "")
        const cached = libraryProcessorCompatibility || ({})
        if (String(cached.kind || "") !== kind || Number(cached.revision || -1) !== Number(graph.revision || 0))
            cacheLibraryProcessorCompatibility(entry)
        return Boolean((libraryProcessorCompatibility.segments || ({}))[String(segmentId || "")])
    }
    function finishLibraryDragDrop(entry, x, y) {
        // QML may deliver DropArea.onDropped before or after the source
        // DragHandler deactivates. Resolve the source before consulting the
        // transient active flag: a valid native drop must not be discarded
        // merely because Qt delivered the handler transition first.
        if (libraryDragDropHandled) return false
        const draggedEntry = entry && entry.type ? entry
            : libraryDragEntry && libraryDragEntry.type ? libraryDragEntry : armedLibraryEntry
        if (!draggedEntry || !draggedEntry.type) return false
        libraryDragCompletionTimer.stop()
        libraryDragDropHandled = true
        armLibraryEntry(draggedEntry)
        const placed = placeArmedLibraryEntry(x, y)
        if (!placed) cancelLibraryPlacement(true)
        libraryDragActive = false
        libraryDragEntry = ({})
        return placed
    }
    function updateLibraryPlacementPoint(x, y) {
        if (!armedLibraryEntry || !armedLibraryEntry.type) return false
        libraryPlacementPoint = ({ "x": Number(x || 0), "y": Number(y || 0) })
        if (armedLibraryEntry.type === "processor") {
            const target = libraryProcessorTargetAt(armedLibraryEntry, x, y)
            const nextRouteId = String(target && target.route && target.route.id || "")
            const nextSegmentId = String(target && target.segmentId || "")
            if (pendingProcessorRouteId !== nextRouteId || pendingProcessorSegmentId !== nextSegmentId) {
                pendingProcessorRouteId = nextRouteId
                pendingProcessorSegmentId = nextSegmentId
                requestWirePaint()
            }
        }
        return true
    }
    function libraryProcessorTargetAt(entry, x, y) {
        if (!entry || entry.type !== "processor") return null
        const processorKind = String(entry.id || entry.processorType || "")
        if (!processorKind.length) return null
        const cell = 96
        const bucket = wireBuckets[Math.floor(Number(x) / cell) + ":" + Math.floor(Number(y) / cell)] || []
        const seen = ({})
        let best = null
        let retained = null
        for (let index = 0; index < bucket.length; ++index) {
            const candidate = bucket[index]
            const segmentId = String(candidate && candidate.routeSegmentId || "")
            if (!segmentId || seen[segmentId]) continue
            seen[segmentId] = true
            if (!libraryProcessorSegmentIsCompatible(entry, segmentId)) continue
            const points = candidate.points || []
            let distance = Number.POSITIVE_INFINITY
            for (let point = 1; point < points.length; ++point)
                distance = Math.min(distance, pointToSegmentDistance(Number(x), Number(y), points[point - 1], points[point]))
            if (distance > 11) continue
            const target = ({ "route": candidate.route, "segmentId": segmentId, "distance": distance })
            if (segmentId === String(pendingProcessorSegmentId || "") && distance <= 15) retained = target
            if (!best || distance < best.distance) best = target
        }
        // A material improvement can replace the current target, but small
        // pointer movement around adjacent routes retains the existing one.
        if (retained && (!best || retained.distance <= best.distance + 4)) return retained
        return best
    }
    function placeArmedLibraryEntry(x, y) {
        const entry = armedLibraryEntry || ({})
        if (!entry.type) return false
        const pointX = Number(x || 0)
        const pointY = Number(y || 0)
        if (entry.type === "node") {
            const card = nodeForId(String(entry.id || ""))
            if (!card || !card.id) return false
            const saved = saveNodePlacement(card, pointX - graphCardWidth(card) * 0.5,
                pointY - graphCardHeight(card) * 0.5, Boolean(card.pinned))
            if (saved) cancelLibraryPlacement()
            return saved
        }
        if (entry.type === "annotation") {
            const created = addAnnotation(String(entry.kind || "note"), pointX, pointY)
            if (created) cancelLibraryPlacement()
            return created
        }
        const target = libraryProcessorTargetAt(entry, pointX, pointY)
        if (!target || !target.route || !target.segmentId) {
            notice = "Drop " + entry.label + " onto a compatible signal route."
            noticeError = true
            return false
        }
        inspectedRoute = target.route
        inspectedNode = ({})
        selectedSegmentId = String(target.segmentId)
        const result = backendObject.signalFlowInsertProcessor(String(target.segmentId), String(entry.id || ""),
            Number(graph.revision || 0))
        announce(result, "Processor was not inserted.")
        if (result && result.success) cancelLibraryPlacement()
        return Boolean(result && result.success)
    }
    function openBlockLibrary() {
        refreshBlockLibrary()
        blockLibrarySelectionIndex = 0
        ensureBlockLibraryPosition()
        blockLibrary.open()
    }
    function closeInspector() {
        inspectedRoute = ({})
        inspectedNode = ({})
        inspectedPort = ({})
        inspectedPortOwner = ({})
        inspectedAnnotation = ({})
        selectedSegmentId = ""
        inspectorOpen = false
    }
    function workspaceSnapshot(changes) {
        const saved = graph.workspace || ({})
        function savedValue(key, fallback) {
            return changes && changes[key] !== undefined ? changes[key]
                : saved[key] !== undefined ? saved[key] : fallback
        }
        return ({
            "panX": changes && changes.panX !== undefined ? changes.panX : graphViewport.contentX,
            "panY": changes && changes.panY !== undefined ? changes.panY : graphViewport.contentY,
            "zoom": changes && changes.zoom !== undefined ? changes.zoom : zoom,
            "wireStyle": savedValue("wireStyle", "smooth"),
            "densityMode": savedValue("densityMode", "detailed"),
            "inspectorWidth": savedValue("inspectorWidth", 360),
            "inspectorX": savedValue("inspectorX", inspectorPositionX),
            "inspectorY": savedValue("inspectorY", inspectorPositionY),
            "blockLibraryX": savedValue("blockLibraryX", blockLibraryPositionX),
            "blockLibraryY": savedValue("blockLibraryY", blockLibraryPositionY),
            "graphSettingsX": savedValue("graphSettingsX", graphSettingsPositionX),
            "graphSettingsY": savedValue("graphSettingsY", graphSettingsPositionY),
            "portVisibility": savedValue("portVisibility", "smart"),
            "autoExpandPorts": savedValue("autoExpandPorts", true),
            "layoutLocked": savedValue("layoutLocked", false),
            "snapToGrid": savedValue("snapToGrid", true),
            "annotations": changes && changes.annotations !== undefined ? changes.annotations : workspaceAnnotations
        })
    }
    function persistWorkspace(changes, successMessage) {
        const saved = backendObject.signalFlowSaveWorkspace(workspaceSnapshot(changes || ({})))
        if (saved) {
            graph = backendObject.signalFlowGraph
            if (successMessage && successMessage.length > 0) {
                notice = successMessage
                noticeError = false
            }
        } else {
            notice = "Signal Flow workspace could not be saved."
            noticeError = true
        }
        return saved
    }
    function saveWorkspace() {
        if (!graph.workspace) return true
        // The 650 ms debounce only captures viewport pan/zoom.  Do not turn
        // that acknowledgement into a graph-model refresh which could land
        // immediately after an unrelated card release.
        const saved = backendObject.signalFlowSaveWorkspaceSilently(workspaceSnapshot({}))
        if (!saved) {
            notice = "Signal Flow workspace could not be saved."
            noticeError = true
        }
        return saved
    }
    function toggleWireStyle() {
        const nextStyle = graph.workspace && graph.workspace.wireStyle === "orthogonal" ? "smooth" : "orthogonal"
        persistWorkspace({ "wireStyle": nextStyle }, "Wire style set to " + nextStyle + ".")
    }
    function setDensityMode(modeName) {
        if (modeName !== "detailed" && modeName !== "compact" && modeName !== "overview") return false
        return persistWorkspace({ "densityMode": modeName }, "Semantic density set to " + modeName + ".")
    }
    function cycleDensityMode() {
        const current = graph.workspace && graph.workspace.densityMode || "detailed"
        return setDensityMode(current === "detailed" ? "compact" : current === "compact" ? "overview" : "detailed")
    }
    function setPortVisibility(modeName) {
        if (modeName !== "smart" && modeName !== "connected" && modeName !== "compact" && modeName !== "expanded") return false
        return persistWorkspace({ "portVisibility": modeName }, "Port visibility set to " + modeName + ".")
    }
    function clampViewportPosition(x, y) {
        if (!graphViewport) return ({ "x": 0, "y": 0 })
        const maxX = Math.max(0, Number(graphViewport.contentWidth || 0) - Number(graphViewport.width || 0))
        const maxY = Math.max(0, Number(graphViewport.contentHeight || 0) - Number(graphViewport.height || 0))
        return ({ "x": Math.max(0, Math.min(maxX, Number(x || 0))),
            "y": Math.max(0, Math.min(maxY, Number(y || 0))) })
    }
    function panCanvasBy(viewportDeltaX, viewportDeltaY) {
        if (!graphViewport) return false
        const next = clampViewportPosition(Number(graphViewport.contentX || 0) + Number(viewportDeltaX || 0),
            Number(graphViewport.contentY || 0) + Number(viewportDeltaY || 0))
        graphViewport.contentX = next.x
        graphViewport.contentY = next.y
        if (viewportGrid) viewportGrid.requestPaint()
        return true
    }
    function beginCanvasPan(sceneX, sceneY) {
        if (liveDragNodeId || (dragWire && dragWire.active)) return false
        canvasPanActive = true
        canvasPanLastX = Number(sceneX || 0)
        canvasPanLastY = Number(sceneY || 0)
        return true
    }
    function updateCanvasPan(sceneX, sceneY) {
        if (!canvasPanActive) return false
        const x = Number(sceneX || 0)
        const y = Number(sceneY || 0)
        const changed = panCanvasBy((canvasPanLastX - x) * zoom, (canvasPanLastY - y) * zoom)
        canvasPanLastX = x
        canvasPanLastY = y
        return changed
    }
    function endCanvasPan() {
        if (!canvasPanActive) return false
        canvasPanActive = false
        saveTimer.restart()
        return true
    }
    function zoomAtViewport(factor, viewportX, viewportY) {
        if (!graphViewport || !isFinite(Number(factor)) || Number(factor) <= 0) return false
        const previous = Number(zoom || 1)
        const next = Math.max(0.42, Math.min(1.6, previous * Number(factor)))
        if (Math.abs(next - previous) < 0.0001) return false
        const focusX = Number(viewportX === undefined ? graphViewport.width * 0.5 : viewportX)
        const focusY = Number(viewportY === undefined ? graphViewport.height * 0.5 : viewportY)
        const worldX = (Number(graphViewport.contentX || 0) + focusX) / previous
        const worldY = (Number(graphViewport.contentY || 0) + focusY) / previous
        zoom = next
        const anchored = clampViewportPosition(worldX * next - focusX, worldY * next - focusY)
        graphViewport.contentX = anchored.x
        graphViewport.contentY = anchored.y
        if (viewportGrid) viewportGrid.requestPaint()
        saveTimer.restart()
        return true
    }
    function toggleLayoutLocked() {
        const locked = Boolean(graph.workspace && graph.workspace.layoutLocked)
        return persistWorkspace({ "layoutLocked": !locked }, !locked
            ? "Layout locked. Dragging cards is disabled."
            : "Layout unlocked. Drag cards to reposition them.")
    }
    function toggleSnapToGrid() {
        const enabled = snapToGridEnabled
        return persistWorkspace({ "snapToGrid": !enabled }, !enabled
            ? "Snap to Grid enabled. Cards remain free until release."
            : "Snap to Grid disabled. Card positions will stay exactly where released.")
    }
    function saveNodePlacement(nodeData, x, y, pinned) {
        if (!nodeData || !nodeData.objectId) return false
        const saved = backendObject.signalFlowSaveNodeLayout(String(nodeData.objectId), x, y, Boolean(pinned))
        if (saved) {
            graph = backendObject.signalFlowGraph
            notice = Boolean(pinned) ? "Pinned node placement saved." : "Node placement saved."
            noticeError = false
        } else {
            notice = "Node placement was not saved."
            noticeError = true
        }
        return saved
    }
    function toggleNodePinned(nodeData) {
        if (!nodeData) return false
        const placement = nodePosition(nodeData, Number(nodeData.x || 0), Number(nodeData.y || 0))
        return saveNodePlacement(nodeData, placement.x, placement.y, !Boolean(nodeData.pinned))
    }
    function cancelTransient() {
        if (routingActive) cancelRouting("Connection cancelled.", true)
        else if (conflictDialog.visible) conflictDialog.close()
        else if (processorDialog.visible) processorDialog.close()
        else if (shareDialog.visible) shareDialog.close()
        else if (deckSourceLearnDialog.visible) deckSourceLearnDialog.close()
        else if (learnDialog.visible) learnDialog.close()
        else if (aliasDialog.visible) aliasDialog.close()
        else if (defaultsDialog.visible) defaultsDialog.close()
        else if (explainDialog.visible) explainDialog.close()
        else if (source && source.id || inspectedRoute && inspectedRoute.id || inspectedNode && inspectedNode.id || inspectedPort && (inspectedPort.id || inspectedPort.endpointId)) {
            source = ({})
            closeInspector()
            notice = "Selection cleared."
            noticeError = false
        } else return false
        return true
    }
    function keyboardAction(action) {
        if (action === "undo") {
            if (!backendObject.signalFlowCanUndo) return false
            announce(backendObject.signalFlowUndo(Number(graph.revision || 0)), "Undo was not applied.")
            return !noticeError
        }
        if (action === "redo") {
            if (!backendObject.signalFlowCanRedo) return false
            announce(backendObject.signalFlowRedo(Number(graph.revision || 0)), "Redo was not applied.")
            return !noticeError
        }
        if (action === "search") { deckSearch.forceActiveFocus(); deckSearch.selectAll(); return true }
        if (action === "delete") return disconnectSelected()
        if (action === "escape") return cancelTransient()
        if (action === "fit") { fitGraph(); return true }
        if (action === "focus") { focusCurrentSelection(); return true }
        if (action === "xray") { xrayMode = !xrayMode; notice = xrayMode ? "X-ray isolates related signal paths." : "X-ray shows the full graph."; noticeError = false; return true }
        if (action === "live") { liveMode = !liveMode; if (liveMode) liveTelemetry = backendObject.signalFlowLiveTelemetry(); return true }
        if (action === "density") return cycleDensityMode()
        if (action === "zoom-in") return zoomAtViewport(1.1)
        if (action === "zoom-out") return zoomAtViewport(1 / 1.1)
        return false
    }

    Keys.onPressed: function(event) {
        const control = Boolean(event.modifiers & Qt.ControlModifier)
        const shift = Boolean(event.modifiers & Qt.ShiftModifier)
        let handled = false
        if (control && event.key === Qt.Key_Z) handled = keyboardAction(shift ? "redo" : "undo")
        else if (control && event.key === Qt.Key_Y) handled = keyboardAction("redo")
        else if (control && event.key === Qt.Key_F) handled = keyboardAction("search")
        else if (!control && event.key === Qt.Key_Delete) handled = keyboardAction("delete")
        else if (!control && event.key === Qt.Key_Escape) handled = keyboardAction("escape")
        else if (!control && event.key === Qt.Key_F) handled = keyboardAction("focus")
        else if (!control && event.key === Qt.Key_Home) handled = keyboardAction("fit")
        else if (!control && event.key === Qt.Key_X) handled = keyboardAction("xray")
        else if (!control && event.key === Qt.Key_L) handled = keyboardAction("live")
        else if (!control && event.key === Qt.Key_D) handled = keyboardAction("density")
        else if (control && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) handled = keyboardAction("zoom-in")
        else if (control && event.key === Qt.Key_Minus) handled = keyboardAction("zoom-out")
        if (handled) event.accepted = true
    }

    onGraphChanged: {
        const nextRoutes = graph.routes || []
        const topologyChanged = routeTopologySignature(nextRoutes) !== renderedRouteTopologySignature()
        if (armedLibraryEntry && armedLibraryEntry.type === "processor")
            cacheLibraryProcessorCompatibility(armedLibraryEntry)
        else if (Number((libraryProcessorCompatibility || {}).revision || -1) !== Number(graph.revision || 0))
            libraryProcessorCompatibility = ({})
        // Node placement persistence is a presentation acknowledgement, not a
        // new graph projection. Its final coordinate and incident geometry
        // already exist locally, so retain those caches through the backend
        // echo instead of clearing every anchor and rebuilding every route.
        const localLayoutCommit = Boolean(layoutCommitAnchorSuppressionNodeId)
            && !topologyChanged
        if (!localLayoutCommit) graphRefreshCount += 1
        if (graph.workspace && graph.workspace.annotations !== undefined)
            workspaceAnnotations = graph.workspace.annotations || []
        if (!localLayoutCommit) refreshSceneBounds()
        const nextRouteIds = ({})
        const nextNodeIds = ({})
        const nextNodes = graph.nodes || []
        const appearingNodes = ({})
        const hadRenderedNodes = Object.keys(renderedNodeIds).length > 0
        for (let index = 0; index < nextNodes.length; ++index) {
            const nodeData = nextNodes[index] || ({})
            const identity = nodeIdentity(nodeData)
            const storedIdentity = nodeStorageIdentity(nodeData)
            if (identity) nextNodeIds[identity] = true
            if (storedIdentity) nextNodeIds[storedIdentity] = true
            if (hadRenderedNodes && identity && !renderedNodeIds[identity]
                    && (!storedIdentity || !renderedNodeIds[storedIdentity])) {
                appearingNodes[identity] = true
                if (storedIdentity) appearingNodes[storedIdentity] = true
            }
        }
        renderedNodeIds = nextNodeIds
        appearingNodeIds = appearingNodes
        if (Object.keys(appearingNodes).length > 0) {
            nodeAppear = 0
            nodeAppearanceAnimation.restart()
        } else nodeAppear = 1
        // Node placement and workspace saves update the graph too.  They do
        // not change a route, so replaying the reveal animation would blank
        // otherwise live wires immediately after a card is dropped.
        for (let index = 0; index < nextRoutes.length; ++index)
            nextRouteIds[String(nextRoutes[index].id || "")] = true
        if (inspectedRoute && inspectedRoute.id && !nextRouteIds[String(inspectedRoute.id)]) {
            inspectedRoute = ({})
            selectedSegmentId = ""
        }
        if (inspectedNode && inspectedNode.id && !nextNodeIds[String(inspectedNode.id || "")]
                && !nextNodeIds[String(inspectedNode.objectId || "")]) {
            inspectedNode = ({})
        }
        if (topologyChanged) {
            const previousRouteIds = ({})
            const previousGeometry = wireGeometry || []
            for (let index = 0; index < previousGeometry.length; ++index) {
                const routeId = String(previousGeometry[index] && previousGeometry[index].routeId || "")
                if (routeId.length > 0) previousRouteIds[routeId] = true
            }
            // Topology changes also reflow surviving routes. Keep their exact
            // last curve so the stable cache can use the normal geometry morph.
            prepareWireReflow(autoLayoutMotionActive ? motionLayoutDuration : motionStructuralDuration)
            retiringWireGeometry = snapshotWireGeometry(previousGeometry).filter(function(entry) {
                return entry && entry.routeId && !nextRouteIds[String(entry.routeId)]
            })
            const appearing = ({})
            const acknowledgements = ({})
            for (let index = 0; index < nextRoutes.length; ++index) {
                const routeId = String(nextRoutes[index].id || "")
                if (routeId.length > 0 && !previousRouteIds[routeId]) {
                    appearing[routeId] = true
                    const destinationKey = routeDestinationKey(nextRoutes[index])
                    if (destinationKey.length > 0) acknowledgements[destinationKey] = true
                }
            }
            routeArrivalAnimation.stop()
            arrivingDestinationPortIds = acknowledgements
            routeArrival = Object.keys(acknowledgements).length > 0 ? 0 : 1
            appearingWireRouteIds = appearing
            wireAppearancePending = Object.keys(appearing).length > 0
            wireAppear = wireAppearancePending ? 0 : 1
            // Never reset the whole graph to zero opacity for one connection.
            wireRevealAnimation.stop()
            wireReveal = 1
            wireRetireAnimation.stop()
            if (retiringWireGeometry.length > 0) {
                wireRetire = 0
                wireRetireAnimation.restart()
            } else wireRetire = 1
        }
        if (localLayoutCommit) {
            // A graph handoff queued before the silent persistence echo is
            // stale now: the incremental release cache already owns this
            // placement. Do not let that queued full rebuild cross the local
            // acknowledgement boundary.
            discardQueuedGraphGeometryHandoff()
            discardQueuedWireGeometryRebuild()
            wireReveal = 1
            if (retiringWireGeometry.length === 0) wireRetire = 1
            return
        }
        portAnchors = ({})
        portAnchorOffsets = ({})
        // Clear endpoint measurements before clearing a saved-position
        // override.  That keeps the position-map observer below from
        // rebuilding once with anchors belonging to the graph we just left.
        nodePositions = ({})
        liveNodePositions = ({})
        liveDragNodeId = ""
        // A card release writes its stable placement through AppBackend, which
        // immediately replaces `graph`.  Keep the active layer's last live
        // endpoint offset through that replacement: clearing it here would
        // make incident routes paint from the old settled cache until the
        // deferred port measurements finished the one allowed final rebuild.
        // rebuildWireGeometry() owns the atomic handoff and clears these
        // values only after the new cache is ready.
        if (!liveDragRouteSettlePending) {
            liveDragRenderNodeId = ""
            liveDragDeltaX = 0
            liveDragDeltaY = 0
        }
        liveDragPosition = ({})
        clearNodeSnapPreview()
        // Repeater delegates report their measured centers on the following
        // geometry turn. Their timers are queued before the handoff timer, so
        // this retains the current wire until exactly one stable,
        // obstacle-aware rebuild is ready.
        requestPortAnchorMeasurement()
        // With no routes there cannot be a delegate endpoint callback to do
        // this work, but stale geometry still has to disappear immediately.
        if (nextRoutes.length === 0) requestWireGeometryRebuild()
        else {
            requestGraphGeometryHandoff()
        }
        if (!topologyChanged) {
            // Keep any already-drawn wire continuously visible while fresh
            // port measurements settle after a layout-only graph update.
            wireReveal = 1
            if (retiringWireGeometry.length === 0) wireRetire = 1
        }
    }
    // A position-map assignment is also a geometry change.  Most product
    // moves pass through noteNodePosition(), but layout restore, test fixtures
    // and future workspace importers may replace the complete map directly.
    // Existing card-local port offsets make this one coalesced rebuild exact
    // without waiting for a visual delegate notification.  Graph replacement
    // clears those offsets first (above), so its deferred anchor handoff stays
    // a single stable rebuild.
    onNodePositionsChanged: {
        refreshSceneBounds()
        if (!liveDragNodeId && Object.keys(portAnchorOffsets).length > 0)
            requestWireGeometryRebuild()
    }
    // This is intentionally a change counter rather than instrumentation in
    // graphLayoutBounds(): mutating state from that binding would itself turn
    // a diagnostic into a layout dependency. A card drag must not alter the
    // settled graph extent at all.
    onSceneBoundsChanged: sceneBoundsChangeCount += 1
    onWireGeometryChanged: requestWirePaint()
    onPresentationStateChanged: {
        presentationRestored = false
        Qt.callLater(restorePresentationState)
    }
    onInspectedRouteChanged: {
        if (routingActive && inspectedRoute && inspectedRoute.id)
            cancelRouting("Connection cancelled because a route was selected.", true)
        if (inspectedRoute && inspectedRoute.id) {
            inspectedPort = ({})
            inspectedPortOwner = ({})
            inspectorOpen = true
            ensureInspectorPosition()
        }
        restartInspectorMotion()
        refreshSceneBounds()
        requestWirePaint()
    }
    onHoveredRouteIdChanged: requestWirePaint()
    onSourceChanged: {
        refreshSceneBounds()
        requestWirePaint()
    }
    onInspectedNodeChanged: {
        if (routingActive && inspectedNode && inspectedNode.id)
            cancelRouting("Connection cancelled because a card was selected.", true)
        if (inspectedNode && inspectedNode.id) {
            inspectedPort = ({})
            inspectedPortOwner = ({})
            inspectorOpen = true
            ensureInspectorPosition()
        }
        restartInspectorMotion()
        refreshSceneBounds()
        requestWirePaint()
    }
    onInspectedPortChanged: {
        if (inspectedPort && (inspectedPort.id || inspectedPort.endpointId)) {
            inspectorOpen = true
            ensureInspectorPosition()
        }
    }
    onModeChanged: {
        if (mode === "effective" && routingActive)
            cancelRouting("Connection cancelled because Effective view is read-only.", true)
        if (mode === "effective") clearNodeSnapPreview()
        requestWirePaint()
    }
    onLiveTelemetryChanged: requestWirePaint()
    onSignalFocusChanged: requestWirePaint()
    onXrayModeChanged: requestWirePaint()
    onReducedMotionChanged: {
        if (reducedMotion) {
            wireRevealAnimation.stop()
            wireRetireAnimation.stop()
            wireReflowAnimation.stop()
            wireAppearAnimation.stop()
            routeArrivalAnimation.stop()
            nodeAppearanceAnimation.stop()
            wireReveal = 1
            wireRetire = 1
            retiringWireGeometry = []
            wireReflow = 1
            wireReflowPending = false
            reflowWireGeometry = []
            reflowWireSegmentIndex = ({})
            wireReflowTouchesSettledRoutes = false
            wireAppear = 1
            wireAppearancePending = false
            appearingWireRouteIds = ({})
            routeArrival = 1
            arrivingDestinationPortIds = ({})
            nodeAppear = 1
            appearingNodeIds = ({})
        }
        requestWirePaint()
    }
    onWireRevealChanged: requestWirePaint()
    onWireRetireChanged: requestWirePaint()
    onWireReflowChanged: {
        // Most card drops remain entirely on the small active overlay. A
        // moved blocker can also re-route a settled wire, so repaint that
        // texture only for that exceptional affected-route case.
        if (wireReflowTouchesSettledRoutes || !hasWireOverlayRoutes()) requestSettledWirePaint()
        if (hasWireOverlayRoutes() && activeDiagram) activeDiagram.requestPaint()
    }
    onWireAppearChanged: requestWirePaint()
    onLiveModeChanged: requestWirePaint()
    onDragWireChanged: {
        // Clearing a source-wire preview must also clear the transparent
        // interaction canvas; a settled repaint alone would leave its last
        // painted tip visible until a later graph invalidation.
        if (activeDiagram) activeDiagram.requestPaint()
        if (!dragWire || !dragWire.active) {
            refreshSceneBounds()
            requestSettledWirePaint()
        }
    }
    onRouteStateFilterChanged: {
        refreshSceneBounds()
        requestWirePaint()
    }
    onFilterChanged: {
        refreshSceneBounds()
        requestWirePaint()
    }
    onQueryChanged: {
        refreshSceneBounds()
        requestWirePaint()
    }
    onBlockLibraryQueryChanged: rebuildBlockLibraryEntries()
    onPendingProcessorChannelCandidateChanged: {
        refreshSceneBounds()
        if (activeDiagram) activeDiagram.requestPaint()
    }
    onPendingProcessorChannelNodeIdChanged: {
        refreshSceneBounds()
        if (activeDiagram) activeDiagram.requestPaint()
    }
    onLearnedSourcePortIdChanged: refreshSceneBounds()
    onNoticeChanged: {
        if (notice.length > 0) noticeTimer.restart()
        else noticeTimer.stop()
    }

    Connections {
        target: backendObject
        function onSignalFlowChanged() {
            const nextGraph = backendObject.signalFlowGraph
            if (root.routingActive && !root.connectionCommitInFlight
                    && (Number(root.interaction.expectedRevision || 0) !== Number(nextGraph.revision || 0)
                        || String(root.interaction.profileId || "") !== String(nextGraph.profileId || "")
                        || String(root.interaction.rigId || "") !== String(nextGraph.deviceRigId || ""))) {
                root.cancelRouting("The graph changed outside this gesture. No route was changed.", true)
            }
            root.graph = nextGraph
            if (!root.workspaceRestored) Qt.callLater(root.restoreWorkspace)
            Qt.callLater(root.applyBackendFocus)
        }
        function onInputLearningChanged() {
            const learning = backendObject.inputLearning || ({})
            if (String(learning.kind || "") === "signal-flow") {
                if (String(learning.phase || "") === "assigned") {
                    root.acceptSignalFlowLearnedSource(learning)
                } else if (Boolean(learning.active) && !deckSourceLearnDialog.visible) {
                    deckSourceLearnDialog.open()
                }
                return
            }
            if (String(learning.phase || "") === "assigned" && root.pendingLearnDestination
                    && root.pendingLearnDestination.id) {
                Qt.callLater(function() { root.highlightDestinationLearnedRoute(learning) })
            } else if (!Boolean(learning.active)) {
                root.pendingLearnDestination = ({})
            }
        }
    }
    Component.onCompleted: {
        refreshBlockLibrary()
        refreshSceneBounds()
        Qt.callLater(restoreWorkspace)
        Qt.callLater(restorePresentationState)
        Qt.callLater(applyBackendFocus)
        Qt.callLater(rebuildWireGeometry)
    }

    Timer {
        id: liveSampleTimer
        interval: 100
        repeat: true
        running: root.visible && (!root.Window.window || (root.Window.window.active
            && root.Window.window.visibility !== Window.Minimized))
            && (root.liveMode || root.signalFocus || root.mode === "effective")
        onTriggered: { root.liveTelemetry = backendObject.signalFlowLiveTelemetry(); root.liveSampleCount += 1 }
    }

    Timer {
        id: wireGeometryTimer
        // Layout/port changes coalesce to the next event turn. Pointer drags
        // use their own frame timer and never queue a graph-wide rebuild.
        interval: 0
        repeat: false
        onTriggered: {
            const epoch = root.scheduledWireGeometryInvalidationEpoch
            if (!epoch || epoch !== root.wireGeometryInvalidationEpoch) return
            root.scheduledWireGeometryInvalidationEpoch = 0
            root.rebuildWireGeometry()
        }
    }

    Timer {
        id: layoutCommitAnchorSuppressionTimer
        // Delegate layout reports immediately after a card coordinate changes
        // repeat its already-known local port offsets. They are not a reason
        // to rebuild every route. Later genuine port changes remain observed.
        interval: root.motionFastDuration + 24
        repeat: false
        onTriggered: root.layoutCommitAnchorSuppressionNodeId = ""
    }

    Timer {
        id: nodePlacementPersistenceTimer
        // Presentation has already committed when this runs. Yield one normal
        // presentation turn before persisting so a synchronous settings write
        // can never hold the pointer-release handler hostage. Its completion
        // remains visually silent and never changes the committed coordinate.
        interval: 16
        repeat: false
        onTriggered: {
            const pending = root.pendingNodePlacementPersistences || []
            if (pending.length === 0) return
            const placement = pending[0]
            root.pendingNodePlacementPersistences = pending.slice(1)
            const saved = root.backendObject
                && root.backendObject.signalFlowSaveNodeLayout(String(placement.objectId),
                    Number(placement.x), Number(placement.y), Boolean(placement.pinned))
            root.nodePlacementWriteCount += 1
            root.layoutPersistenceAcknowledgementCount += 1
            if (saved) {
                root.notice = Boolean(placement.pinned) ? "Pinned node placement saved." : "Node placement saved."
                root.noticeError = false
            } else {
                root.notice = "Node placement was not saved."
                root.noticeError = true
            }
            if (root.pendingNodePlacementPersistences.length > 0) nodePlacementPersistenceTimer.restart()
        }
    }

    Timer {
        id: disclosureReleaseReflowTimer
        // A disclosure can retain its structural motion for the shared card
        // duration. The timer is restarted by each local anchor callback, so
        // fold affected segments after rendered geometry is stable rather than
        // after an arbitrary delay from pointer-up.
        interval: root.motionStructuralDuration + 24
        repeat: false
        onTriggered: {
            const nodeId = root.pendingDisclosureReleaseReflowNodeId
            const nodeIds = root.pendingReleaseAnchorReflowNodeIds || ({})
            root.pendingDisclosureReleaseReflowNodeId = ""
            root.pendingReleaseAnchorReflowNodeIds = ({})
            root.releaseAnchorReflowActive = false
            if (!nodeId && Object.keys(nodeIds).length === 0) return
            const obstacleUpdatesBefore = root.obstacleWireGeometryUpdateCount
            if (nodeId) root.rebuildReleaseWireGeometry(nodeId)
            for (const ownerId in nodeIds) {
                if (String(ownerId) !== String(nodeId || ""))
                    root.rebuildReleaseWireGeometry(ownerId)
            }
            if (root.obstacleWireGeometryUpdateCount > obstacleUpdatesBefore)
                root.requestSettledWirePaint()
        }
    }

    Timer {
        id: sectionDisclosureReflowTimer
        // Hover-section geometry follows the same rendered-anchor settling
        // cadence as a card disclosure, but never promotes that local change
        // to a graph-wide wire rebuild.
        interval: root.motionStructuralDuration + 24
        repeat: false
        onTriggered: {
            // A drag owns a frozen disclosure snapshot. Defer this local
            // reflow until that gesture has released rather than letting a
            // hover transition touch anchors or wires halfway through it.
            if (root.liveDragNodeId) {
                sectionDisclosureReflowTimer.restart()
                return
            }
            const nodeIds = root.pendingSectionDisclosureReflowNodeIds || ({})
            root.pendingSectionDisclosureReflowNodeIds = ({})
            for (const nodeId in nodeIds) root.rebuildIncidentWireGeometry(nodeId)
            sectionDisclosureReflowSettlingTimer.restart()
        }
    }

    Timer {
        id: sectionDisclosureReflowSettlingTimer
        // Keep the local classification through the final delegate-polish
        // turn. A new anchor notification restarts both timers, so this map
        // is cleared only after the last affected card has gone quiet.
        interval: root.motionStructuralDuration + 48
        repeat: false
        onTriggered: {
            if (root.liveDragNodeId || sectionDisclosureReflowTimer.running) {
                sectionDisclosureReflowSettlingTimer.restart()
                return
            }
            root.activeSectionDisclosureReflowNodeIds = ({})
        }
    }

    Timer {
        id: processorChannelDwellTimer
        interval: root.processorChannelDwellMs
        repeat: false
        onTriggered: root.commitProcessorChannelDwell()
    }

    Timer {
        id: graphGeometryHandoffTimer
        // A graph refresh first asks every live delegate to measure its port.
        // Those zero-turn delegate timers are queued before this handoff, so
        // restarting the geometry timer here produces one stable cache even
        // when a backend signal lands between a caller's own measurement
        // request and its next presentation turn.
        interval: 0
        repeat: false
        onTriggered: {
            const epoch = root.scheduledGraphGeometryHandoffEpoch
            if (!epoch || epoch !== root.graphGeometryHandoffEpoch) return
            root.scheduledGraphGeometryHandoffEpoch = 0
            root.requestWireGeometryRebuild()
        }
    }

    Timer {
        id: noticeTimer
        interval: root.noticeError ? 8000 : 4200
        repeat: false
        onTriggered: { root.notice = ""; root.noticeError = false }
    }

    Timer {
        id: layoutMotionTimer
        interval: root.motionLayoutDuration + 40
        repeat: false
        onTriggered: root.autoLayoutMotionActive = false
    }

    NumberAnimation {
        id: wireRevealAnimation
        target: root
        property: "wireReveal"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: wireRetireAnimation
        target: root
        property: "wireRetire"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.InCubic
        onStopped: if (root.wireRetire >= 0.999) root.retiringWireGeometry = []
    }
    NumberAnimation {
        id: wireReflowAnimation
        target: root
        property: "wireReflow"
        from: 0
        to: 1
        duration: root.wireReflowDuration
        // Match card-size and layout movement so cached wire interpolation
        // remains attached to the visible ports for the whole transition.
        easing.type: Easing.OutCubic
        onStopped: if (root.wireReflow >= 0.999) {
            root.reflowWireGeometry = []
            root.reflowWireSegmentIndex = ({})
            root.wireReflowTouchesSettledRoutes = false
            if (activeDiagram) activeDiagram.requestPaint()
        }
    }
    NumberAnimation {
        id: wireAppearAnimation
        target: root
        property: "wireAppear"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.OutCubic
        onStopped: if (root.wireAppear >= 0.999) {
            root.appearingWireRouteIds = ({})
            root.beginRouteDestinationAcknowledgement()
        }
    }
    NumberAnimation {
        id: routeArrivalAnimation
        target: root
        property: "routeArrival"
        from: 0
        to: 1
        duration: root.motionFastDuration
        easing.type: Easing.OutCubic
        onStopped: if (root.routeArrival >= 0.999) root.arrivingDestinationPortIds = ({})
    }
    NumberAnimation {
        id: nodeAppearanceAnimation
        target: root
        property: "nodeAppear"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.OutCubic
        onStopped: if (root.nodeAppear >= 0.999) root.appearingNodeIds = ({})
    }
    NumberAnimation {
        id: inspectorContentAnimation
        target: root
        property: "inspectorContentReveal"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.OutCubic
    }

    Timer {
        id: processorHysteresis
        interval: 110
        repeat: false
        onTriggered: {
            const routes = root.graph.routes || []
            for (let index = 0; index < routes.length; ++index) {
                if (String(routes[index].id || "") !== root.pendingProcessorRouteId) continue
                root.inspectedRoute = routes[index]
                root.selectedSegmentId = root.pendingProcessorSegmentId
                root.inspectedNode = ({})
                root.source = ({})
                break
            }
        }
    }

    Timer {
        id: libraryDragCompletionTimer
        // DropArea delivery is allowed to trail DragHandler deactivation by a
        // presentation turn. Keep the source entry briefly, then clear an
        // actual miss; never leave a release as an armed ghost.
        interval: 80
        repeat: false
        onTriggered: {
            // If no canvas drop arrived, this was a miss rather than an
            // ongoing placement. The Library's retained catalog stays intact.
            if (root.libraryDragActive && !root.libraryDragDropHandled)
                root.cancelLibraryPlacement()
            root.libraryDragActive = false
            root.libraryDragDropHandled = false
            root.libraryDragEntry = ({})
        }
    }

    component DeckTooltip: ToolTip {
        id: deckTooltip
        delay: 450
        timeout: 8000
        padding: deck.space8
        implicitWidth: Math.min(320, Math.max(112, tooltipText.implicitWidth + deck.space16))
        implicitHeight: tooltipText.implicitHeight + deck.space12
        background: Rectangle {
            radius: deck.radiusControl
            color: Qt.rgba(deck.elevatedSurface.r, deck.elevatedSurface.g, deck.elevatedSurface.b, 0.98)
            border.width: 1
            border.color: deck.border
        }
        contentItem: Text {
            id: tooltipText
            width: Math.min(304, implicitWidth)
            text: deckTooltip.text
            color: deck.textPrimary
            font.family: deck.bodyFont
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }

    component FloatingPanelCloseButton: Button {
        id: floatingPanelCloseButton
        implicitWidth: 30
        implicitHeight: 30
        padding: 0
        Accessible.name: "Close panel"
        contentItem: Text {
            text: "×"
            color: !floatingPanelCloseButton.enabled ? deck.disabled
                : floatingPanelCloseButton.down ? deck.applicationBackground : deck.textPrimary
            font.family: deck.bodyFont
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: floatingPanelCloseButton.down ? deck.accentMuted
                : floatingPanelCloseButton.hovered ? deck.selected : deck.secondarySurface
            border.width: 1
            border.color: floatingPanelCloseButton.activeFocus ? deck.focus : deck.border
        }
        DeckTooltip { visible: floatingPanelCloseButton.hovered; text: "Close" }
    }

    component DeckToggle: Button {
        id: deckToggle
        property string label: ""
        checkable: true
        implicitHeight: 30
        padding: deck.space6
        Accessible.name: label
        contentItem: RowLayout {
            spacing: deck.space8
            Text { Layout.fillWidth: true; text: deckToggle.label; color: deckToggle.enabled ? deck.textPrimary : deck.disabled; font.family: deck.bodyFont; font.pixelSize: 10 }
            Rectangle {
                Layout.preferredWidth: 32; Layout.preferredHeight: 16; radius: 8
                color: deckToggle.checked ? deck.accent : deck.secondarySurface
                border.width: 1; border.color: deckToggle.checked ? deck.accent : deck.border
                Rectangle {
                    width: 12; height: 12; radius: 6; y: 2
                    x: deckToggle.checked ? parent.width - width - 2 : 2
                    color: deckToggle.checked ? deck.applicationBackground : deck.textMuted
                    Behavior on x { NumberAnimation { duration: deckToggle.down ? 0 : root.motionFastDuration } }
                }
            }
        }
        background: Rectangle { radius: deck.radiusControl; color: deckToggle.hovered ? deck.selected : "transparent"; border.width: deckToggle.activeFocus ? 1 : 0; border.color: deck.focus }
    }

    component DeckButton: Button {
        id: deckButton
        property bool emphasized: false
        property bool destructive: false
        property string helpText: ""
        implicitHeight: deck.compactControlHeight
        padding: deck.space10
        font.family: deck.bodyFont
        font.pixelSize: 10
        font.bold: true
        Accessible.name: text
        contentItem: Text { text: deckButton.text; color: !deckButton.enabled ? deck.disabled : deckButton.emphasized ? deck.applicationBackground : deck.textPrimary; font: deckButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            radius: deck.radiusControl
            color: !deckButton.enabled ? deck.secondarySurface : deckButton.down ? deck.accentMuted : deckButton.emphasized ? deck.accent : deckButton.hovered ? deck.selected : deck.secondarySurface
            border.width: 1
            border.color: deckButton.destructive ? deck.fault : deckButton.emphasized ? deck.accent : deck.border
        }
        DeckTooltip { visible: deckButton.hovered && deckButton.helpText.length > 0; text: deckButton.helpText }
    }

    component FlowPort: Rectangle {
        id: flowPort
        // Keep delegates quiet while Repeater assigns their model row.
        property var port: ({})
        property bool destination: false
        property bool isSelected: false
        property bool isCompatible: false
        implicitHeight: 31
        radius: deck.radiusControl
        color: isSelected ? deck.accentMuted : isCompatible ? deck.selected : dropTarget.containsDrag ? deck.selected : hover.containsMouse ? deck.secondarySurface : "transparent"
        border.width: isSelected || isCompatible || dropTarget.containsDrag ? 1 : 0
        border.color: isSelected ? deck.accent : deck.focus
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: deck.space8
            anchors.rightMargin: deck.space8
            spacing: deck.space6
            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: !flowPort.port || !flowPort.port.available ? deck.disabled : flowPort.port.mapped ? (flowPort.destination ? deck.healthy : deck.informational) : deck.textMuted }
            Text { Layout.fillWidth: true; text: flowPort.port && flowPort.port.label ? flowPort.port.label : ""; color: flowPort.port && flowPort.port.available ? deck.textPrimary : deck.disabled; font.family: deck.bodyFont; font.pixelSize: 10; elide: Text.ElideRight }
            Text { text: flowPort.port && flowPort.port.mapped && !flowPort.destination ? "LIVE" : ""; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
        }
        Drag.active: sourceDrag.active
        Drag.source: flowPort
        Drag.keys: ["signal-flow-source"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        DragHandler {
            id: sourceDrag
            enabled: Boolean(!flowPort.destination && root.mode === "configured" && root.graph.editable
                             && flowPort.port && flowPort.port.available)
            onActiveChanged: {
                if (active && flowPort.port && flowPort.port.id) {
                    const point = flowPort.mapToItem(scene, flowPort.width, flowPort.height * 0.5)
                    root.beginSourceDrag(flowPort.port, point)
                } else {
                    root.endSourceDrag()
                }
            }
            onTranslationChanged: {
                if (!active) return
                const point = flowPort.mapToItem(scene, flowPort.width + translation.x,
                    flowPort.height * 0.5 + translation.y)
                root.queueSourceDrag(point)
            }
        }
        DropArea {
            id: dropTarget
            anchors.fill: parent
            keys: ["signal-flow-source"]
            enabled: Boolean(flowPort.destination && root.mode === "configured" && root.graph.editable)
            onEntered: function(drag) { if (drag.source && drag.source.port) root.previewDestination(flowPort.port) }
            onExited: function(drag) { root.clearDestinationPreview(flowPort.port) }
            onDropped: function(drop) {
                const sourcePort = root.sourceDragPortFromDrop(drop)
                if (sourcePort) {
                    root.completeSourceDrag(sourcePort, flowPort.port)
                    root.endSourceDrag()
                    drop.accepted = true
                }
            }
        }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            enabled: Boolean(root.mode === "configured" && root.graph.editable && flowPort.port && flowPort.port.available)
            onContainsMouseChanged: {
                if (!flowPort.destination) return
                if (containsMouse) root.previewDestination(flowPort.port)
                else root.clearDestinationPreview(flowPort.port)
            }
            onClicked: { if (!flowPort.port || !flowPort.port.id) return; if (flowPort.destination) root.connect(flowPort.port, false); else root.selectSource(flowPort.port) }
        }
        DeckTooltip {
            visible: hover.containsMouse
            text: !flowPort.port || !flowPort.port.available
                ? "This control is inactive until calibration reports meaningful travel."
                : flowPort.destination && root.source && !flowPort.isCompatible
                    ? "Choose a destination compatible with the selected source."
                    : flowPort.destination && root.connectionPreview && root.connectionPreview.portId === String(flowPort.port.id || "")
                        ? root.connectionPreview.message
                    : flowPort.destination ? "Choose this destination for the selected source."
                                           : "Select this physical source, then choose a compatible destination."
        }
    }

    // A port row lives on its owning card.  The visual dot stays compact while
    // its 30 px target provides the forgiving direct-manipulation affordance.
    component GraphPortRow: Item {
        id: graphPortRow
        objectName: "signalFlowPortRow:" + String(port && (port.endpointId || port.id) || "")
        property var port: ({})
        property bool destination: false
        property bool compatibleTarget: root.compatible(port)
        readonly property string endpointKey: String(port && (port.endpointId || port.id) || "")
        readonly property bool sourceSelected: !destination && root.source
            && String(root.source.endpointId || root.source.id || "") === endpointKey
        readonly property bool previewedDestination: destination && root.connectionPreview
            && String(root.connectionPreview.portId || "") === String(port.id || "")
        readonly property bool routingCandidate: destination && compatibleTarget
        readonly property bool routingEmphasized: sourceSelected || previewedDestination || routingCandidate
        implicitHeight: 28
        opacity: destination && root.routingActive && !routingCandidate && !previewedDestination ? 0.48 : 1.0
        Behavior on opacity { NumberAnimation { duration: root.motionFastDuration } }
        Accessible.name: (destination ? "Destination " : "Source ") + String(port.label || "port")
        Accessible.description: destination
            ? "Click or drop a compatible source here to create a canonical route."
            : "Click or drag from this physical endpoint to route it."
        function updateAnchor() {
            if (!port || !endpointKey || !scene || !visible) return
            // Bind cached wire geometry to the painted port center, not its
            // larger interaction target. Layouts may distribute that target
            // on a half-pixel while the dot remains the visual attachment.
            const visual = destination ? destinationDot : sourceDot
            if (!visual) return
            const point = visual.mapToItem(scene, visual.width * 0.5, visual.height * 0.5)
            root.notePortAnchor(port, point.x, point.y)
        }
        // A group expansion moves this row after its own x/y notifications.
        // Measure on the delegate-owned next event turn so the cache records
        // the settled rendered center, while destruction still removes any
        // stale measurement before the delegate vanishes.
        function queueAnchorMeasurement() { anchorMeasurementTimer.restart() }
        onXChanged: queueAnchorMeasurement()
        onYChanged: queueAnchorMeasurement()
        onWidthChanged: queueAnchorMeasurement()
        onHeightChanged: queueAnchorMeasurement()
        onVisibleChanged: queueAnchorMeasurement()
        Component.onCompleted: queueAnchorMeasurement()
        Component.onDestruction: root.forgetPortAnchor(port)
        Timer {
            id: anchorMeasurementTimer
            interval: 0
            repeat: false
            onTriggered: graphPortRow.updateAnchor()
        }
        Connections {
            target: root
            function onPortAnchorMeasurementEpochChanged() { graphPortRow.queueAnchorMeasurement() }
        }
        Rectangle {
            anchors.fill: parent
            radius: 5
            visible: graphPortRow.routingEmphasized
            color: graphPortRow.previewedDestination
                ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.19)
                : graphPortRow.sourceSelected ? Qt.rgba(deck.accent.r, deck.accent.g, deck.accent.b, 0.18)
                : Qt.rgba(deck.focus.r, deck.focus.g, deck.focus.b, 0.10)
            border.width: graphPortRow.sourceSelected || graphPortRow.previewedDestination ? 2 : 1
            border.color: graphPortRow.previewedDestination ? deck.attention
                : graphPortRow.sourceSelected ? deck.accent : deck.focus
            Behavior on color { ColorAnimation { duration: root.motionFastDuration } }
        }
        RowLayout {
            anchors.fill: parent
            spacing: deck.space6
            Item {
                Layout.preferredWidth: graphPortRow.destination ? 30 : 0
                Layout.preferredHeight: 28
                visible: graphPortRow.destination
                Rectangle {
                    anchors.centerIn: parent
                    width: destinationDot.width + 10; height: destinationDot.height + 10; radius: width / 2
                    visible: graphPortRow.routingCandidate
                    color: graphPortRow.previewedDestination
                        ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.24)
                        : Qt.rgba(deck.focus.r, deck.focus.g, deck.focus.b, 0.18)
                    border.width: 1
                    border.color: graphPortRow.previewedDestination ? deck.attention : deck.focus
                    Behavior on opacity { NumberAnimation { duration: root.motionFastDuration } }
                }
                // The committed wire reaches the actual destination first;
                // this short ring is the acknowledgement, not a second route
                // or an alternate hit target.
                Rectangle {
                    anchors.centerIn: parent
                    visible: root.routeDestinationAcknowledged(graphPortRow.port)
                    width: destinationDot.width + 10 + root.routeArrival * 14
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: deck.healthy
                    opacity: visible ? 0.72 * (1 - root.routeArrival) : 0
                }
                Rectangle {
                    id: destinationDot
                    objectName: "signalFlowPortVisual:" + graphPortRow.endpointKey
                    anchors.centerIn: parent
                    // This is the enduring version of the wire endpoint cap.
                    // The canvas repeats the cap above a settled wire, but the
                    // card itself owns the same socket treatment so a canvas
                    // repaint can never reveal a different, legacy-looking
                    // node underneath it.
                    width: graphPortRow.routingCandidate ? 14 : 12
                    height: width; radius: width / 2
                    color: deck.elevatedSurface
                    border.width: graphPortRow.previewedDestination || graphPortRow.compatibleTarget ? 2 : 1
                    border.color: graphPortRow.previewedDestination ? deck.attention
                        : graphPortRow.compatibleTarget ? deck.focus : deck.border
                    Behavior on width { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 4.2; height: width; radius: width / 2
                        color: !graphPortRow.port.available ? deck.disabled
                            : graphPortRow.previewedDestination ? deck.attention
                            : graphPortRow.compatibleTarget || graphPortRow.port.mapped ? deck.healthy : deck.textMuted
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: String(graphPortRow.port.label || "")
                color: !graphPortRow.port.available ? deck.disabled : deck.textPrimary
                font.family: deck.bodyFont; font.pixelSize: 10
                elide: Text.ElideRight
            }
            Text {
                visible: !graphPortRow.destination && Boolean(graphPortRow.port.mapped)
                text: "LINKED"
                color: deck.informational; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
            }
            Text {
                visible: graphPortRow.destination && root.routingActive
                text: graphPortRow.routingCandidate ? "READY" : "DIFFERS"
                color: graphPortRow.routingCandidate ? deck.healthy : deck.textMuted
                font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
            }
            Item {
                Layout.preferredWidth: graphPortRow.destination ? 0 : 30
                Layout.preferredHeight: 28
                visible: !graphPortRow.destination
                Rectangle {
                    anchors.centerIn: parent
                    width: sourceDot.width + 10; height: sourceDot.height + 10; radius: width / 2
                    visible: graphPortRow.sourceSelected
                    color: Qt.rgba(deck.accent.r, deck.accent.g, deck.accent.b, 0.24)
                    border.width: 1
                    border.color: deck.accent
                }
                Rectangle {
                    id: sourceDot
                    objectName: "signalFlowPortVisual:" + graphPortRow.endpointKey
                    anchors.centerIn: parent
                    // Match the persistent card socket to the cap drawn in
                    // the wire plane; see destinationDot above.
                    width: graphPortRow.sourceSelected ? 14 : 12
                    height: width; radius: width / 2
                    color: deck.elevatedSurface
                    border.width: graphPortRow.sourceSelected ? 2 : 1
                    border.color: graphPortRow.sourceSelected ? deck.accent : deck.focus
                    Behavior on width { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 4.2; height: width; radius: width / 2
                        color: !graphPortRow.port.available ? deck.disabled
                            : graphPortRow.sourceSelected || graphPortRow.port.mapped
                                ? deck.informational : deck.textMuted
                    }
                }
            }
        }
        Item {
            id: hitTarget
            objectName: "signalFlowPortHitTarget:" + graphPortRow.endpointKey
            width: 30; height: 30
            x: destination ? 0 : parent.width - width
            y: -1
            activeFocusOnTab: true
            Drag.active: sourceDrag.active
            Drag.source: graphPortRow
            Drag.keys: ["signal-flow-source"]
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2
            DragHandler {
                id: sourceDrag
                enabled: !graphPortRow.destination && root.mode === "configured" && graphPortRow.port.available
                dragThreshold: 8
                function updateWireAtPointer() {
                    const point = hitTarget.mapToItem(scene, centroid.position.x, centroid.position.y)
                    root.queueSourceDrag(point)
                }
                onActiveChanged: {
                    if (active) {
                        const point = hitTarget.mapToItem(scene, centroid.position.x, centroid.position.y)
                        root.beginSourceDrag(graphPortRow.port, point)
                    } else root.endSourceDrag()
                }
                onTranslationChanged: {
                    if (!active) return
                    updateWireAtPointer()
                }
            }
            DropArea {
                anchors.fill: parent
                keys: ["signal-flow-source"]
                enabled: graphPortRow.destination && root.mode === "configured" && graphPortRow.port.available
                onEntered: function(drag) { root.previewDestination(graphPortRow.port) }
                onExited: root.clearDestinationPreview(graphPortRow.port)
                onDropped: function(drop) {
                    const sourcePort = root.sourceDragPortFromDrop(drop)
                    if (sourcePort) {
                        root.completeSourceDrag(sourcePort, graphPortRow.port)
                        root.endSourceDrag()
                    }
                    drop.accepted = true
                }
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: {
                    if (graphPortRow.destination && root.routingActive) root.connect(graphPortRow.port, false)
                    // A direct click on a physical graph socket is a real
                    // source-first connection gesture, just like clicking a
                    // card-row source. Previously this only inspected the
                    // port, leaving processor hover with no armed source.
                    else if (!graphPortRow.destination) root.selectSource(graphPortRow.port)
                    else root.inspectPort(graphPortRow.port, root.portOwner(graphPortRow.port))
                }
            }
            // Keep the context action on a real mouse target rather than a
            // competing gesture recognizer: the left-side DragHandler never
            // receives this button, so a native right click is deterministic.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                preventStealing: true
                onClicked: function(mouse) {
                    if (mouse.button !== Qt.RightButton) return
                    deckPortContextMenu.targetPort = graphPortRow.port
                    deckPortContextMenu.targetOwner = root.portOwner(graphPortRow.port)
                    deckPortContextMenu.open()
                }
            }
            HoverHandler {
                id: portHover
                onHoveredChanged: {
                    root.hoveredPortId = hovered ? String(graphPortRow.port.endpointId || graphPortRow.port.id || "") : ""
                    if (hovered && graphPortRow.destination) root.previewDestination(graphPortRow.port)
                    else if (!hovered && graphPortRow.destination) root.clearDestinationPreview(graphPortRow.port)
                }
            }
            Keys.onPressed: function(event) {
                if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter && event.key !== Qt.Key_Space) return
                if (graphPortRow.destination && root.routingActive) root.connect(graphPortRow.port, false)
                else if (!graphPortRow.destination) root.selectSource(graphPortRow.port)
                else root.inspectPort(graphPortRow.port, root.portOwner(graphPortRow.port))
                event.accepted = true
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 4; height: parent.height + 4; radius: 5
                color: "transparent"; border.width: 2; border.color: deck.focus
                visible: parent.activeFocus
            }
            DeckTooltip {
                visible: portHover.hovered && !sourceDrag.active && !(root.dragWire && root.dragWire.active)
                delay: 350
                text: root.portHoverSummary(graphPortRow.port, graphPortRow.destination)
            }
        }
    }

    component SignalFlowPortGroups: Column {
        id: signalFlowPortGroups
        property var nodeData: ({})
        property bool destination: false
        width: parent ? parent.width : 0
        spacing: 3
        Repeater {
            model: root.cardGroups(signalFlowPortGroups.nodeData)
            delegate: Column {
                id: groupColumn
                required property var modelData
                readonly property string groupName: String(modelData.group || "Controls")
                objectName: "signalFlowPortSection:" + root.nodeIdentity(signalFlowPortGroups.nodeData)
                    + ":" + groupName
                readonly property bool collapsed: root.cardGroupCollapsed(signalFlowPortGroups.nodeData,
                    groupName, signalFlowPortGroups.destination)
                readonly property bool showConnectedPorts: root.cardGroupShowsConnectedPorts(
                    signalFlowPortGroups.nodeData, groupName, signalFlowPortGroups.destination)
                readonly property int routeCount: root.cardGroupRouteCount(signalFlowPortGroups.nodeData,
                    groupName, signalFlowPortGroups.destination)
                width: signalFlowPortGroups.width
                spacing: 2
                // This region covers the header, every revealed row, and the
                // Column's own padding/spacing. Moving from the disclosure
                // into a revealed port therefore remains inside one section.
                HoverHandler {
                    id: sectionHoverRegion
                    onHoveredChanged: root.setGroupHovered(signalFlowPortGroups.nodeData,
                        groupColumn.groupName, hovered)
                }
                Timer {
                    id: sectionHoverGraceTimer
                    interval: root.temporaryGroupHoverGraceMs
                    repeat: false
                    // Binding `running` pauses the single-shot timer while a
                    // card drag freezes disclosure geometry; it restarts a
                    // full grace period after the drop if still outside.
                    running: root.cardGroupHoverGraceActive(signalFlowPortGroups.nodeData,
                        groupColumn.groupName)
                    onTriggered: root.completeGroupHoverGrace(signalFlowPortGroups.nodeData,
                        groupColumn.groupName)
                }
                Row {
                    id: groupHeader
                    width: parent.width
                    height: 20
                    spacing: 4
                    Text {
                        width: groupHeader.width - groupToggle.width - deck.space4
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupColumn.groupName.toUpperCase() + " · " + groupColumn.routeCount
                            + (groupColumn.routeCount === 1 ? " ROUTE" : " ROUTES")
                            + (groupColumn.collapsed && !groupColumn.showConnectedPorts ? " · UNUSED HIDDEN" : "")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    DeckButton {
                        id: groupToggle
                        objectName: "signalFlowPortGroupToggle:" + root.nodeIdentity(signalFlowPortGroups.nodeData)
                            + ":" + groupColumn.groupName
                        visible: true
                        width: 28
                        height: 20
                        padding: 0
                        text: groupColumn.collapsed ? "+" : "−"
                        Accessible.name: (groupColumn.collapsed ? "Expand " : "Collapse ") + groupColumn.groupName
                        onClicked: root.setGroup(signalFlowPortGroups.nodeData, groupColumn.groupName,
                            !groupColumn.collapsed)
                    }
                }
                Text {
                    visible: groupColumn.collapsed && !groupColumn.showConnectedPorts
                    width: parent.width
                    text: groupColumn.routeCount > 0 ? groupColumn.routeCount + " routed endpoint"
                        + (groupColumn.routeCount === 1 ? "" : "s") + " hidden by explicit compact state" : "Collapsed"
                    color: deck.textMuted
                    font.family: deck.bodyFont
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
                Repeater {
                    model: groupColumn.collapsed
                        ? (groupColumn.showConnectedPorts ? root.cardGroupConnectedPorts(
                            signalFlowPortGroups.nodeData, groupColumn.groupName,
                            signalFlowPortGroups.destination) : [])
                        : root.cardGroupPorts(signalFlowPortGroups.nodeData, groupColumn.groupName,
                            signalFlowPortGroups.destination)
                    delegate: GraphPortRow {
                        required property var modelData
                        width: parent.width
                        port: modelData
                        destination: signalFlowPortGroups.destination
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: deck.space16
        spacing: deck.space12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: deck.space6
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Text { text: "SIGNAL FLOW"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.rightMargin: deck.space4 }
                DeckButton { objectName: "signalFlowDeviceRigControl"; Layout.preferredWidth: 138; text: "Device Rig"; helpText: root.graph.deviceRigName || "Choose the Device Rig context."; onClicked: deckRigContextMenu.open() }
                DeckButton { objectName: "signalFlowProfileControl"; Layout.preferredWidth: 126; text: "Profile"; helpText: root.graph.profileName || "Choose the editing profile."; onClicked: deckProfileContextMenu.open() }
                DeckButton { text: "Configured"; emphasized: root.mode === "configured"; onClicked: root.mode = "configured" }
                DeckButton { text: "Effective"; emphasized: root.mode === "effective"; onClicked: root.mode = "effective" }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Item { Layout.fillWidth: true }
                DeckButton { objectName: "signalFlowInspectorControl"; text: "Inspector"; helpText: "Open the selected-object Inspector, or a concise Signal Flow guide when nothing is selected."; onClicked: { root.inspectorOpen = true; root.ensureInspectorPosition() } }
                DeckButton { objectName: "signalFlowBlockLibraryControl"; text: "Block Library"; helpText: "Browse canonical blocks and workspace annotations."; onClicked: root.openBlockLibrary() }
                DeckButton { objectName: "signalFlowGraphSettingsControl"; text: "Graph Settings"; helpText: "Configure workspace-only Signal Flow presentation."; onClicked: { root.ensureGraphSettingsPosition(); graphSettings.open() } }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space8
            DeckButton {
                id: portVisibilityControl
                objectName: "signalFlowPortVisibilityControl"
                text: "Ports: " + String(root.graph.workspace && root.graph.workspace.portVisibility || "smart")
                helpText: "Choose Smart, Connected Only, Compact, or Expanded port visibility."
                onClicked: deckPortVisibilityMenu.openFor(portVisibilityControl)
            }
            DeckButton { objectName: "signalFlowZoomOutControl"; text: "−"; width: 30; padding: 0; helpText: "Zoom out"; onClicked: root.zoomAtViewport(1 / 1.1) }
            Text { text: Math.round(root.zoom * 100) + "%"; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
            DeckButton { objectName: "signalFlowZoomInControl"; text: "+"; width: 30; padding: 0; helpText: "Zoom in"; onClicked: root.zoomAtViewport(1.1) }
            DeckButton { objectName: "signalFlowFitControl"; text: "Fit"; helpText: "Fit the visible Signal Flow workspace."; onClicked: root.fitGraph() }
            DeckButton { text: "Auto Layout"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onClicked: root.applyAutoLayout() }
            DeckButton { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock" : "Lock"; onClicked: root.toggleLayoutLocked() }
            Item { Layout.fillWidth: true }
            DeckButton { text: root.liveMode ? "Live: on" : "Live"; emphasized: root.liveMode; helpText: "Sample bounded latest telemetry without affecting MappingWorker."; onClicked: { root.liveMode = !root.liveMode; if (root.liveMode) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
            DeckButton { text: "Undo"; enabled: backendObject.signalFlowCanUndo; onClicked: root.announce(backendObject.signalFlowUndo(Number(root.graph.revision || 0)), "Undo was not applied.") }
            DeckButton { text: "Redo"; enabled: backendObject.signalFlowCanRedo; onClicked: root.announce(backendObject.signalFlowRedo(Number(root.graph.revision || 0)), "Redo was not applied.") }
            DeckButton { text: "More"; helpText: "View, layout, learning, and diagnostics."; onClicked: deckMoreMenu.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: deck.sectionGap

            FlightDeckCard {
                tokens: deck
                // Retired V2.6.0 surrogate: direct card ports own routing now.
                visible: false
                enabled: false
                Layout.preferredWidth: Math.max(210, Math.min(275, root.width * 0.23))
                Layout.fillHeight: true
                contentPadding: deck.cardPaddingTechnical
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: deck.space8
                    Text { text: "SOURCE BUS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Text { text: root.source && root.source.label ? root.source.label : "Pick a physical control"; color: root.source && root.source.label ? deck.accent : deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 5
                            Repeater {
                                model: root.groups("input")
                                delegate: Column {
                                    required property var modelData
                                    property string groupName: modelData.group
                                    readonly property bool collapsed: root.groupCollapsed("input", groupName)
                                    width: parent.width; spacing: 2
                                    Row {
                                        width: parent.width; spacing: 4
                                        Text { width: parent.width - 34; text: parent.parent.groupName + " · " + root.groupPorts("input", parent.parent.groupName, 999).length; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                        DeckButton { width: 28; height: 20; padding: 0; text: parent.parent.collapsed ? "+" : "−"; Accessible.name: (parent.parent.collapsed ? "Expand " : "Collapse ") + parent.parent.groupName; onClicked: root.setGroup("input", parent.parent.groupName, !parent.parent.collapsed) }
                                    }
                                    Repeater {
                                        visible: !parent.collapsed
                                        model: parent.collapsed ? [] : root.groupPorts("input", parent.groupName,
                                            root.canvasPortLimit)
                                        delegate: FlowPort { required property var modelData; width: parent.width; port: modelData; isSelected: root.source && root.source.id === modelData.id }
                                    }
                                }
                            }
                            Text { visible: root.sourcePorts().length === 0; width: parent.width; text: "No source ports match."; color: deck.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentPadding: 0
                color: deck.graphSurface
                border.color: deck.graphFrame
                clip: true
                // The grid belongs to the viewport, not the content bounds.
                // It derives its phase from the camera transform, so it fills
                // empty editing space while remaining visually world-anchored.
                Canvas {
                    id: viewportGrid
                    anchors.fill: parent
                    z: 0
                    antialiasing: false
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        const zoomValue = Math.max(0.01, Number(root.zoom || 1))
                        let worldStep = 48
                        while (worldStep * zoomValue < 22) worldStep *= 2
                        while (worldStep * zoomValue > 72 && worldStep > 6) worldStep /= 2
                        const minor = worldStep * zoomValue
                        const major = minor * 4
                        const phase = function(content, spacing) {
                            const raw = -Number(content || 0) % spacing
                            return raw <= 0 ? raw + spacing : raw
                        }
                        const draw = function(spacing, alpha, lineWidth) {
                            const startX = phase(graphViewport ? graphViewport.contentX : 0, spacing)
                            const startY = phase(graphViewport ? graphViewport.contentY : 0, spacing)
                            context.beginPath()
                            for (let x = startX; x < width; x += spacing) { context.moveTo(x, 0); context.lineTo(x, height) }
                            for (let y = startY; y < height; y += spacing) { context.moveTo(0, y); context.lineTo(width, y) }
                            context.globalAlpha = alpha
                            context.lineWidth = lineWidth
                            context.stroke()
                        }
                        context.strokeStyle = deck.graphGrid
                        draw(minor, 0.36, 1)
                        draw(major, 0.62, 1)
                        context.globalAlpha = 1
                    }
                    Connections {
                        target: graphViewport
                        function onContentXChanged() { viewportGrid.requestPaint() }
                        function onContentYChanged() { viewportGrid.requestPaint() }
                    }
                    Connections {
                        target: root
                        function onZoomChanged() { viewportGrid.requestPaint() }
                    }
                }
                Flickable {
                    id: graphViewport
                    objectName: "signalFlowGraphViewport"
                    anchors.fill: parent
                    z: 1
                    contentWidth: scene.width * root.zoom
                    contentHeight: scene.height * root.zoom
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    // The card body claims a drag on press so it can update
                    // its incident wires synchronously. Do not let the graph
                    // viewport steal that already-claimed pointer to pan.
                    interactive: !root.autoLayoutMotionActive && !root.liveDragNodeId && !(root.dragWire && root.dragWire.active) && !root.canvasPanActive
                    onMovementEnded: { saveTimer.restart(); if (viewportGrid) viewportGrid.requestPaint() }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Item {
                            id: scene
                            objectName: "signalFlowGraphScene"
                            width: root.sceneLogicalWidth
                            height: root.sceneLogicalHeight
                        scale: root.zoom
                        transformOrigin: Item.TopLeft
                        Canvas {
                            id: diagram
                            anchors.fill: parent
                            // Settled wires are behind every card. Their
                            // endpoint stops remain visible through the
                            // card-owned socket, while unrelated runs are
                            // cleanly occluded by the card surface.
                            z: 0
                            // Cache route geometry at graph/layout boundaries.
                            // Live samples only repaint the prepared paths.
                            function drawWire(context, startX, startY, endX, endY, color, lineWidth, alpha,
                                              lane, dashed, reveal, hasDetour, detourY, underCard) {
                                const progress = reveal === undefined ? 1 : Math.max(0, Math.min(1, Number(reveal)))
                                const detour = Boolean(hasDetour)
                                context.save()
                                if (progress < 0.999) {
                                    const left = Math.min(startX, endX) - 8
                                    const span = Math.abs(endX - startX) * progress + 16
                                    context.beginPath()
                                    context.rect(left, 0, span, height)
                                    context.clip()
                                }
                                const path = function() {
                                    const direction = endX >= startX ? 1 : -1
                                    const laneDetourY = detourY + lane * 12
                                    context.beginPath()
                                    context.moveTo(startX, startY)
                                    if (root.graph.workspace && root.graph.workspace.wireStyle === "orthogonal") {
                                        if (root.wiresOverlayCards) {
                                            const exitX = startX + direction * 20
                                            const entryX = endX - direction * 20
                                            const railX = Math.round((startX + endX) * 0.5 + lane * 18)
                                            const startRailY = Math.round(startY + lane * 14)
                                            const endRailY = Math.round(endY + lane * 14)
                                            context.lineTo(exitX, startY)
                                            context.lineTo(exitX, startRailY)
                                            context.lineTo(railX, startRailY)
                                            context.lineTo(railX, endRailY)
                                            context.lineTo(entryX, endRailY)
                                            context.lineTo(entryX, endY)
                                            context.lineTo(endX, endY)
                                        } else if (detour) {
                                            const stub = Math.max(42, Math.min(120, Math.abs(endX - startX) * 0.22))
                                            context.lineTo(startX + direction * stub, startY)
                                            context.lineTo(startX + direction * stub, laneDetourY)
                                            context.lineTo(endX - direction * stub, laneDetourY)
                                            context.lineTo(endX - direction * stub, endY)
                                            context.lineTo(endX, endY)
                                        } else {
                                            const laneX = Math.round((startX + endX) * 0.5 + lane * 12)
                                            context.lineTo(laneX, startY)
                                            context.lineTo(laneX, endY)
                                            context.lineTo(endX, endY)
                                        }
                                    } else if (detour) {
                                        const middleX = (startX + endX) * 0.5
                                        context.bezierCurveTo(startX + direction * 86, startY,
                                            startX + direction * 118, laneDetourY, middleX, laneDetourY)
                                        context.bezierCurveTo(endX - direction * 118, laneDetourY,
                                            endX - direction * 86, endY, endX, endY)
                                    } else {
                                        const offset = 178
                                        context.bezierCurveTo(startX + offset, startY + lane * 12,
                                            endX - offset, endY + lane * 12, endX, endY)
                                    }
                                }
                                // A quiet clearance underlay makes crossings read as
                                // overpasses. The native Deck cards paint above this
                                // canvas, preserving an obvious under-card fallback.
                                context.strokeStyle = root.wiresOverlayCards
                                    ? Qt.rgba(0.01, 0.04, 0.07, 0.76) : deck.graphSurface
                                context.lineWidth = lineWidth + (root.wiresOverlayCards ? 2.6 : 3.2)
                                context.globalAlpha = alpha * (root.wiresOverlayCards ? 0.72 : 0.96)
                                context.setLineDash([])
                                path()
                                context.stroke()
                                context.strokeStyle = color
                                context.lineWidth = lineWidth
                                context.globalAlpha = alpha * (underCard ? 0.72 : 1)
                                context.setLineDash(dashed ? [5, 3] : [])
                                path()
                                context.stroke()
                                context.restore()
                            }
                            function drawEndpointCap(context, x, y, sourceSide, color) {
                                context.save()
                                context.setLineDash([])
                                context.globalAlpha = 1
                                context.fillStyle = deck.elevatedSurface
                                context.beginPath()
                                context.arc(x, y, 5.4, 0, Math.PI * 2)
                                context.fill()
                                context.strokeStyle = color
                                context.lineWidth = 2
                                context.beginPath()
                                context.arc(x, y, 4.1, 0, Math.PI * 2)
                                context.stroke()
                                context.fillStyle = sourceSide ? deck.informational : deck.healthy
                                context.beginPath()
                                context.arc(x, y, 2.1, 0, Math.PI * 2)
                                context.fill()
                                context.restore()
                            }
                            // The generic route curve is intentionally generous so
                            // long, left-to-right routes stay readable.  A temporary
                            // drag wire needs a different contract: its visible tip
                            // must be exactly at the pointer, even for a short or
                            // reverse-direction gesture.  Clamped, direction-aware
                            // controls avoid the old loop that put the pointer on the
                            // visual middle of the preview.
                            function drawDragPreview(context, startX, startY, endX, endY) {
                                const deltaX = endX - startX
                                const controlDirection = deltaX >= 0 ? 1 : -1
                                const control = Math.min(96, Math.max(12, Math.abs(deltaX) * 0.32))
                                const path = function() {
                                    context.beginPath()
                                    context.moveTo(startX, startY)
                                    context.bezierCurveTo(startX + controlDirection * control, startY,
                                        endX - controlDirection * control, endY, endX, endY)
                                }
                                context.save()
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                context.setLineDash([])
                                context.strokeStyle = deck.graphSurface
                                context.lineWidth = 6.1
                                context.globalAlpha = 0.96
                                path()
                                context.stroke()
                                context.strokeStyle = deck.attention
                                context.lineWidth = 2.8
                                context.globalAlpha = 0.97
                                context.setLineDash([6, 4])
                                path()
                                context.stroke()
                                context.restore()
                            }
                            // During the handoff from live drag geometry to
                            // the obstacle-aware cache, interpolate the drawn
                            // path itself.  There is never an outgoing/incoming
                            // duplicate: the one visible wire travels to its
                            // final endpoints and bend controls over 260 ms.
                            function drawWireMorph(context, previous, current, color, lineWidth, alpha, lane, dashed) {
                                // Rail identity matters more than a curved reflow in
                                // orthogonal mode. Draw the current unique rail directly
                                // instead of briefly interpolating two paths on top of one
                                // another.
                                if (root.graph.workspace && root.graph.workspace.wireStyle === "orthogonal") {
                                    drawWire(context, current.startX, current.startY, current.endX, current.endY,
                                        color, lineWidth, alpha, lane, dashed, 1,
                                        current.hasDetour, current.detourY, current.underCard)
                                    return
                                }
                                const progress = Math.max(0, Math.min(1, Number(root.wireReflow || 0)))
                                const blend = function(from, to) { return Number(from) + (Number(to) - Number(from)) * progress }
                                const startX = blend(previous.startX, current.startX)
                                const startY = blend(previous.startY, current.startY)
                                const endX = blend(previous.endX, current.endX)
                                const endY = blend(previous.endY, current.endY)
                                const previousDirection = Number(previous.endX) >= Number(previous.startX) ? 1 : -1
                                const currentDirection = Number(current.endX) >= Number(current.startX) ? 1 : -1
                                const previousDetourY = Boolean(previous.hasDetour)
                                    ? Number(previous.detourY) + lane * 12 : Number(previous.startY) + lane * 12
                                const currentDetourY = Boolean(current.hasDetour)
                                    ? Number(current.detourY) + lane * 12 : Number(current.startY) + lane * 12
                                const previousEndControlY = Boolean(previous.hasDetour)
                                    ? Number(previous.detourY) + lane * 12 : Number(previous.endY) + lane * 12
                                const currentEndControlY = Boolean(current.hasDetour)
                                    ? Number(current.detourY) + lane * 12 : Number(current.endY) + lane * 12
                                const previousStartControlX = Number(previous.startX)
                                    + (Boolean(previous.hasDetour) ? previousDirection * 86 : 178)
                                const currentStartControlX = Number(current.startX)
                                    + (Boolean(current.hasDetour) ? currentDirection * 86 : 178)
                                const previousEndControlX = Number(previous.endX)
                                    - (Boolean(previous.hasDetour) ? previousDirection * 86 : 178)
                                const currentEndControlX = Number(current.endX)
                                    - (Boolean(current.hasDetour) ? currentDirection * 86 : 178)
                                const path = function() {
                                    context.beginPath()
                                    context.moveTo(startX, startY)
                                    context.bezierCurveTo(blend(previousStartControlX, currentStartControlX),
                                        blend(previousDetourY, currentDetourY),
                                        blend(previousEndControlX, currentEndControlX),
                                        blend(previousEndControlY, currentEndControlY), endX, endY)
                                }
                                context.save()
                                context.lineCap = "round"
                                context.lineJoin = "round"
                                context.strokeStyle = deck.graphSurface
                                context.lineWidth = lineWidth + 3.2
                                context.globalAlpha = alpha * 0.96
                                context.setLineDash([])
                                path()
                                context.stroke()
                                context.strokeStyle = color
                                context.lineWidth = lineWidth
                                context.globalAlpha = alpha * (Boolean(previous.underCard) || Boolean(current.underCard) ? 0.72 : 1)
                                context.setLineDash(dashed ? [5, 3] : [])
                                path()
                                context.stroke()
                                context.restore()
                            }
                            onPaint: {
                                root.canvasPaintCount += 1
                                root.settledCanvasPaintCount += 1
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                const retiring = root.retiringWireGeometry || []
                                for (let retiredIndex = 0; retiredIndex < retiring.length; ++retiredIndex) {
                                    const retired = retiring[retiredIndex]
                                    const retiredSegments = retired.segments || []
                                    for (let segmentIndex = 0; segmentIndex < retiredSegments.length; ++segmentIndex) {
                                        const segment = retiredSegments[segmentIndex]
                                        drawWire(context, segment.startX, segment.startY, segment.endX, segment.endY,
                                            deck.textMuted, 1.8, 0.62 * (1 - root.wireRetire),
                                            Number(segment.lane === undefined
                                                ? (retired.lane === undefined ? root.stableLane(retired.routeId, 9) - 4 : retired.lane)
                                                : segment.lane),
                                            true, 1 - root.wireRetire,
                                            segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                const geometry = root.wireGeometry || []
                                for (let i = 0; i < geometry.length; ++i) {
                                    const entry = geometry[i]
                                    // A pointer-owned card promotes its few
                                    // incident routes to activeDiagram.  Keep
                                    // this settled texture unchanged while
                                    // that smaller layer follows the pointer.
                                    if (root.routeIsActiveDrag(entry.routeId)) continue
                                    const route = entry.route
                                    if (root.mode === "effective" && !route.effective) continue
                                    const live = root.routeIsLive(route)
                                    const selected = root.inspectedRoute && root.inspectedRoute.id === route.id
                                    const hovered = String(root.hoveredRouteId || "") === String(route.id || "")
                                    const color = selected ? deck.attention : hovered ? deck.focus
                                        : route.effective ? deck.healthy : routeHasProblem(route) ? deck.attention : deck.graphLabel
                                    const lineWidth = selected ? 3.6 : hovered ? 3.0 : live ? 2.5 : 1.65
                                    const alpha = root.routeVisualAlpha(route, live)
                                    const dashed = root.routeHasProblem(route)
                                    const entryReveal = root.wireIsAppearing(entry.routeId)
                                        ? root.wireAppear : root.wireReveal
                                    if (entry.drawBundleTrunk) {
                                        const trunkEndX = entry.bundleX
                                        drawWire(context, entry.startX, entry.startY, trunkEndX, entry.startY, color,
                                            Math.min(5.0, lineWidth + entry.bundleCount * 0.35), alpha, 0, true,
                                            entryReveal, false, 0, false)
                                        context.save()
                                        context.globalAlpha = alpha * entryReveal
                                        context.fillStyle = deck.textMuted
                                        context.font = "bold 9px sans-serif"
                                        context.fillText(String(entry.bundleCount) + "×", trunkEndX + 5, entry.startY - 5)
                                        context.restore()
                                    }
                                    const segments = entry.segments || []
                                    for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                                        const segment = segments[segmentIndex]
                                        const segmentSelected = String(root.selectedSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentHovered = String(root.hoveredSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentPreview = String(root.pendingProcessorSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentColor = segmentSelected || segmentPreview ? deck.attention
                                            : segmentHovered ? deck.focus : color
                                        const segmentWidth = segmentSelected || segmentPreview ? Math.max(3.8, lineWidth)
                                            : segmentHovered ? Math.max(3.1, lineWidth) : lineWidth
                                        const lane = Number(segment.lane === undefined
                                            ? (entry.lane === undefined ? root.stableLane(route.id, 9) - 4 : entry.lane)
                                            : segment.lane)
                                        const previous = root.reflowSourceSegment(entry.routeId, segment.routeSegmentId)
                                        if (previous && root.wireReflow < 0.999) {
                                            drawWireMorph(context, previous, segment, segmentColor, segmentWidth,
                                                alpha, lane, dashed)
                                        } else {
                                            drawWire(context, segment.startX, segment.startY, segment.endX, segment.endY,
                                                segmentColor, segmentWidth, alpha, lane, dashed,
                                                entryReveal, segment.hasDetour, segment.detourY, segment.underCard)
                                        }
                                        if (segmentPreview) {
                                            context.save()
                                            context.globalAlpha = 0.96
                                            context.fillStyle = deck.attention
                                            context.font = "bold 9px " + deck.telemetryFont
                                            context.fillText("INSERT", (segment.startX + segment.endX) * 0.5 + 8,
                                                (segment.startY + segment.endY) * 0.5 - 7)
                                            context.restore()
                                        }
                                    }
                                }
                                // The wire layer is deliberately above card bodies. Repaint
                                // actual socket caps last, so every connection still reads as
                                // terminating at a real endpoint rather than at card chrome.
                                const cappedEndpoints = ({})
                                for (let entryIndex = 0; entryIndex < geometry.length; ++entryIndex) {
                                    const capEntry = geometry[entryIndex]
                                    if (root.routeIsActiveDrag(capEntry.routeId)) continue
                                    const capRoute = capEntry.route || ({})
                                    if (root.mode === "effective" && !capRoute.effective) continue
                                    const capColor = root.routeHasProblem(capRoute) ? deck.attention
                                        : capRoute.effective ? deck.healthy : deck.graphLabel
                                    const capSegments = capEntry.segments || []
                                    for (let capIndex = 0; capIndex < capSegments.length; ++capIndex) {
                                        const capSegment = capSegments[capIndex]
                                        const sourceKey = "s:" + String(capSegment.sourceEndpointId || "")
                                        const destinationKey = "d:" + String(capSegment.destinationEndpointId || "")
                                        if (!cappedEndpoints[sourceKey]) {
                                            cappedEndpoints[sourceKey] = true
                                            drawEndpointCap(context, capSegment.startX, capSegment.startY, true, capColor)
                                        }
                                        if (!cappedEndpoints[destinationKey]) {
                                            cappedEndpoints[destinationKey] = true
                                            drawEndpointCap(context, capSegment.endX, capSegment.endY, false, capColor)
                                        }
                                    }
                                }
                            }
                        }
                        // The active canvas is intentionally clear at rest.
                        // It paints only the local card-drag routes or a
                        // source-wire preview, leaving the full settled graph
                        // texture untouched on every interaction frame.
                        Canvas {
                            id: activeDiagram
                            anchors.fill: parent
                            // Keep an in-progress connection in the same
                            // below-card plane as settled wires.
                            z: 0
                            onPaint: {
                                root.canvasPaintCount += 1
                                root.activeCanvasPaintCount += 1
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                const geometry = root.wireGeometry || []
                                for (let i = 0; i < geometry.length; ++i) {
                                    const entry = geometry[i]
                                    if (!root.routeIsActiveDrag(entry.routeId)) continue
                                    const route = entry.route || ({})
                                    if (root.mode === "effective" && !route.effective) continue
                                    const live = root.routeIsLive(route)
                                    const selected = root.inspectedRoute && root.inspectedRoute.id === route.id
                                    const hovered = String(root.hoveredRouteId || "") === String(route.id || "")
                                    const color = selected ? deck.attention : hovered ? deck.focus
                                        : route.effective ? deck.healthy : root.routeHasProblem(route) ? deck.attention : deck.graphLabel
                                    const lineWidth = selected ? 3.6 : hovered ? 3.0 : live ? 2.5 : 1.65
                                    const alpha = root.routeVisualAlpha(route, live)
                                    const dashed = root.routeHasProblem(route)
                                    const entryReveal = root.wireIsAppearing(entry.routeId)
                                        ? root.wireAppear : root.wireReveal
                                    const segments = entry.segments || []
                                    for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                                        const segment = segments[segmentIndex]
                                        const segmentSelected = String(root.selectedSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentHovered = String(root.hoveredSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentPreview = String(root.pendingProcessorSegmentId || "") === String(segment.routeSegmentId || "")
                                        const segmentColor = segmentSelected || segmentPreview ? deck.attention
                                            : segmentHovered ? deck.focus : color
                                        const segmentWidth = segmentSelected || segmentPreview ? Math.max(3.8, lineWidth)
                                            : segmentHovered ? Math.max(3.1, lineWidth) : lineWidth
                                        const lane = Number(segment.lane === undefined
                                            ? (entry.lane === undefined ? root.stableLane(route.id, 9) - 4 : entry.lane)
                                            : segment.lane)
                                        const previous = root.reflowSourceSegment(entry.routeId, segment.routeSegmentId)
                                        if (previous && root.wireReflow < 0.999) {
                                            // Release has committed the new cache. Animate from
                                            // the exact live curve held under the pointer to that
                                            // settled, obstacle-aware route on this local layer.
                                            diagram.drawWireMorph(context, previous, segment, segmentColor,
                                                segmentWidth, alpha, lane, dashed)
                                        } else {
                                            const liveSegment = root.liveDragSegmentTouches(segment)
                                            const startX = root.liveDragSegmentStartX(segment)
                                            const startY = root.liveDragSegmentStartY(segment)
                                            const endX = root.liveDragSegmentEndX(segment)
                                            const endY = root.liveDragSegmentEndY(segment)
                                            // A held endpoint needs a cheaper, direct curve;
                                            // release restores obstacle refinement through the
                                            // morph above rather than a visual jump.
                                            diagram.drawWire(context, startX, startY, endX, endY,
                                                segmentColor, segmentWidth, alpha, lane, dashed,
                                                entryReveal, liveSegment ? false : segment.hasDetour,
                                                liveSegment ? 0 : segment.detourY, segment.underCard)
                                        }
                                        if (segmentPreview) {
                                            context.save()
                                            context.globalAlpha = 0.96
                                            context.fillStyle = deck.attention
                                            context.font = "bold 9px " + deck.telemetryFont
                                            context.fillText("INSERT", (segment.startX + segment.endX) * 0.5 + 8,
                                                (segment.startY + segment.endY) * 0.5 - 7)
                                            context.restore()
                                        }
                                    }
                                }
                                if (root.dragWire && root.dragWire.active) {
                                    const sourcePort = root.dragWire.source || ({})
                                    const sourceId = String(sourcePort.id || "")
                                    const sourceEndpoint = String(sourcePort.endpointId || sourcePort.id || "")
                                    const sourceNode = root.nodeForId(root.portOwnerNodeId(sourcePort))
                                    const start = root.currentGraphSpacePortCenter(sourceEndpoint, sourceId, sourceNode, true)
                                    const startX = Number(start.x)
                                    const startY = Number(start.y)
                                    const snappedTarget = root.dragWire.target || ({})
                                    const targetNode = root.nodeForId(root.portOwnerNodeId(snappedTarget))
                                    const snappedCenter = snappedTarget && snappedTarget.id
                                        ? root.currentGraphSpacePortCenter(snappedTarget.endpointId, snappedTarget.id,
                                            targetNode, false) : ({})
                                    const pointerX = snappedTarget && snappedTarget.id
                                        ? Number(snappedCenter.x) : Number(root.dragWire.x || startX)
                                    const pointerY = snappedTarget && snappedTarget.id
                                        ? Number(snappedCenter.y) : Number(root.dragWire.y || startY)
                                    diagram.drawDragPreview(context, startX, startY, pointerX, pointerY)
                                    context.save()
                                    context.fillStyle = Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.28)
                                    context.globalAlpha = 0.96
                                    context.beginPath()
                                    context.arc(pointerX, pointerY, 10, 0, Math.PI * 2)
                                    context.fill()
                                    context.fillStyle = deck.attention
                                    context.globalAlpha = 0.96
                                    context.beginPath()
                                    context.arc(pointerX, pointerY, 5, 0, Math.PI * 2)
                                    context.fill()
                                    context.restore()
                                }
                                const pendingChannel = root.pendingProcessorChannelCandidate || ({})
                                const pendingNode = pendingChannel.node || ({})
                                if (pendingNode.id && root.processorChannelDwellActive(pendingNode)) {
                                    const pendingSource = pendingChannel.source || ({})
                                    const pendingSourceNode = root.nodeForId(root.portOwnerNodeId(pendingSource))
                                    const sourceCenter = root.currentGraphSpacePortCenter(
                                        pendingSource.endpointId, pendingSource.id, pendingSourceNode, true)
                                    const pendingCenter = root.processorPendingInputCenter(pendingNode)
                                    if (isFinite(Number(sourceCenter.x)) && isFinite(Number(sourceCenter.y))
                                            && isFinite(Number(pendingCenter.x)) && isFinite(Number(pendingCenter.y))) {
                                        // The short preview is intentionally separate from the
                                        // settled cache: it makes the source-to-new-input leg
                                        // legible immediately without inventing a persistent
                                        // graph route before AppBackend accepts the dwell.
                                        diagram.drawDragPreview(context, Number(sourceCenter.x),
                                            Number(sourceCenter.y), Number(pendingCenter.x), Number(pendingCenter.y))
                                        diagram.drawEndpointCap(context, Number(pendingCenter.x),
                                            Number(pendingCenter.y), false, deck.attention)
                                    }
                                }
                            }
                        }
                        // Workspace annotations sit on the presentation plane.
                        // They neither register ports nor participate in wire
                        // geometry, hit testing, topology, or runtime state.
                        Repeater {
                            model: root.workspaceAnnotations || []
                            delegate: Rectangle {
                                id: annotationCard
                                required property var modelData
                                readonly property var displayPosition: root.annotationPosition(modelData)
                                x: Number(displayPosition.x || 0)
                                y: Number(displayPosition.y || 0)
                                width: Number(modelData.width || (modelData.kind === "group" ? 360 : 240))
                                height: Number(modelData.height || (modelData.kind === "group" ? 220 : 132))
                                z: modelData.kind === "group" ? 1.2 : 3.5
                                radius: modelData.kind === "group" ? deck.radiusPanel : deck.radiusControl
                                color: modelData.kind === "group"
                                    ? Qt.rgba(deck.elevatedSurface.r, deck.elevatedSurface.g, deck.elevatedSurface.b, 0.20)
                                    : Qt.rgba(deck.elevatedSurface.r, deck.elevatedSurface.g, deck.elevatedSurface.b, 0.94)
                                border.width: String(root.inspectedAnnotation.id || "") === String(modelData.id || "") ? 2 : 1
                                border.color: modelData.kind === "group" ? deck.graphLabel : deck.border
                                clip: true
                                Column {
                                    anchors.fill: parent; anchors.margins: deck.space8; spacing: deck.space4
                                    Item {
                                        width: parent.width; height: 18
                                        Text {
                                            id: annotationDragGrip
                                            width: 16; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                            text: "⋮⋮"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 11
                                        }
                                        TextInput {
                                            id: annotationTitle
                                            anchors.left: annotationDragGrip.right; anchors.leftMargin: deck.space4
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.title || (modelData.kind === "group" ? "Group / Section" : "Text note")
                                            color: modelData.kind === "group" ? deck.textSecondary : deck.textPrimary
                                            font.family: deck.bodyFont; font.pixelSize: modelData.kind === "group" ? 11 : 10; font.bold: true
                                            selectByMouse: true
                                            onEditingFinished: root.updateAnnotation(modelData.id, { "title": text })
                                        }
                                        MouseArea {
                                            width: annotationDragGrip.width; height: parent.height; anchors.left: parent.left
                                            cursorShape: Qt.SizeAllCursor
                                            property real startX: 0; property real startY: 0
                                            property real originX: 0; property real originY: 0
                                            property var contentOrigins: ({})
                                            onPressed: function(mouse) {
                                                startX = mouse.x; startY = mouse.y
                                                originX = Number(modelData.x || 0); originY = Number(modelData.y || 0)
                                                contentOrigins = root.annotationContentOrigins(modelData)
                                                root.inspectedAnnotation = modelData; root.inspectorOpen = true; root.ensureInspectorPosition()
                                            }
                                            onPositionChanged: function(mouse) {
                                                if (!pressed) return
                                                root.moveAnnotation(modelData.id, originX + mouse.x - startX,
                                                    originY + mouse.y - startY, contentOrigins, false, originX, originY)
                                            }
                                            onReleased: root.persistAnnotations()
                                        }
                                    }
                                    TextArea {
                                        visible: modelData.kind === "note"
                                        width: parent.width; height: parent.height - 28
                                        text: modelData.body || ""
                                        color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9
                                        wrapMode: TextArea.Wrap; selectByMouse: true
                                        background: Rectangle { color: "transparent"; border.color: "transparent" }
                                        onFocusChanged: if (!focus) root.updateAnnotation(modelData.id, { "body": text })
                                    }
                                    Text {
                                        visible: modelData.kind === "group"
                                        width: parent.width; text: modelData.body || "Presentation-only workspace section"
                                        color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WordWrap
                                    }
                                }
                                MouseArea {
                                    anchors.right: parent.right; anchors.bottom: parent.bottom; width: 18; height: 18
                                    cursorShape: Qt.SizeFDiagCursor
                                    property real startX: 0; property real startY: 0
                                    property real originWidth: 0; property real originHeight: 0
                                    onPressed: function(mouse) { startX = mouse.x; startY = mouse.y; originWidth = annotationCard.width; originHeight = annotationCard.height }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed) return
                                        root.updateAnnotation(modelData.id, { "width": Math.max(80, originWidth + mouse.x - startX),
                                            "height": Math.max(40, originHeight + mouse.y - startY) }, false)
                                    }
                                    onReleased: root.persistAnnotations()
                                }
                            }
                        }
                        // The ghost and guide stay under the freely moving card. They make a
                        // release-time proposal explicit without consuming pointer ownership.
                        Item {
                            id: snapPreviewOverlay
                            anchors.fill: parent
                            z: 1.5
                            visible: Boolean(root.nodeSnapPreview && root.nodeSnapPreview.active)
                                && !root.snapDragAltBypass
                            Rectangle {
                                visible: isFinite(Number(root.nodeSnapPreview.guideX))
                                x: Number(root.nodeSnapPreview.guideX || 0) - 0.5
                                y: 0; width: 1; height: parent.height
                                color: deck.focus; opacity: 0.40
                            }
                            Rectangle {
                                visible: isFinite(Number(root.nodeSnapPreview.guideY))
                                x: 0; y: Number(root.nodeSnapPreview.guideY || 0) - 0.5
                                width: parent.width; height: 1
                                color: deck.focus; opacity: 0.40
                            }
                            Rectangle {
                                x: Number(root.nodeSnapPreview.x || 0)
                                y: Number(root.nodeSnapPreview.y || 0)
                                width: Number(root.nodeSnapPreview.width || 0)
                                height: Number(root.nodeSnapPreview.height || 0)
                                radius: deck.radiusCard
                                color: "transparent"
                                border.width: 1
                                border.color: deck.focus
                                opacity: 0.66
                            }
                        }
                        // Keyboard-armed and drag-armed Library entries use
                        // the same graph-world ghost. It follows only the
                        // pointer, never mutates topology before a valid drop.
                        Item {
                            id: libraryPlacementGhost
                            objectName: "signalFlowLibraryPlacementGhost"
                            z: 7
                            visible: Boolean(root.armedLibraryEntry && root.armedLibraryEntry.type)
                            x: Number(root.libraryPlacementPoint.x || 0) - width * 0.5
                            y: Number(root.libraryPlacementPoint.y || 0) - height * 0.5
                            width: root.armedLibraryEntry && root.armedLibraryEntry.type === "node"
                                ? root.graphCardWidth(root.armedLibraryEntry.node || ({}))
                                : root.armedLibraryEntry && root.armedLibraryEntry.type === "processor" ? 188 : 190
                            height: root.armedLibraryEntry && root.armedLibraryEntry.type === "node"
                                ? Math.min(130, root.graphCardHeight(root.armedLibraryEntry.node || ({})))
                                : root.armedLibraryEntry && root.armedLibraryEntry.type === "processor" ? 76 : 56
                            opacity: 0.82
                            Rectangle {
                                anchors.fill: parent; radius: deck.radiusCard
                                color: root.armedLibraryEntry && root.armedLibraryEntry.type === "annotation"
                                    ? Qt.rgba(deck.elevatedSurface.r, deck.elevatedSurface.g, deck.elevatedSurface.b, 0.88)
                                    : deck.secondarySurface
                                border.width: 2; border.color: deck.focus
                            }
                            Column {
                                anchors.fill: parent; anchors.margins: deck.space8; spacing: deck.space4
                                Text { width: parent.width; text: String(root.armedLibraryEntry && root.armedLibraryEntry.label || "Block"); color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight }
                                Text { width: parent.width; text: root.armedLibraryEntry && root.armedLibraryEntry.type === "processor" ? "PROCESSOR · DROP ON ROUTE" : root.armedLibraryEntry && root.armedLibraryEntry.type === "node" ? "EXISTING CANONICAL BLOCK" : "PRESENTATION ONLY"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                Text { visible: root.armedLibraryEntry && root.armedLibraryEntry.type === "processor"; width: parent.width; text: String(root.armedLibraryEntry.detail || "Canonical processor") + " · drop onto a highlighted compatible signal route"; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: 8; wrapMode: Text.WordWrap }
                            }
                        }
                        // One interaction layer tests the same cached paths that Canvas paints.
                        // Cards sit above it, so endpoint hit areas always take precedence.
                        MouseArea {
                            id: wireInteractionLayer
                            objectName: "signalFlowWireInteractionLayer"
                            anchors.fill: parent
                            z: 1
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            property bool contextMenuPress: false
                            property bool canvasPanCandidate: false
                            property bool canvasPanned: false
                            property real canvasPressX: 0
                            property real canvasPressY: 0
                            onPositionChanged: function(mouse) {
                                root.updateWireHover(mouse.x, mouse.y)
                                root.updateLibraryPlacementPoint(mouse.x, mouse.y)
                                if (!canvasPanCandidate || !pressed) return
                                const dx = Number(mouse.x) - canvasPressX
                                const dy = Number(mouse.y) - canvasPressY
                                if (!canvasPanned && Math.hypot(dx, dy) < 4) return
                                canvasPanned = true
                                root.updateCanvasPan(mouse.x, mouse.y)
                            }
                            onExited: root.updateWireHover(-10000, -10000)
                            // Flickable may take over a background gesture
                            // before MouseArea emits `clicked`. Dismiss on
                            // the initial empty-canvas press so selection is
                            // never stranded behind a pan gesture.
                            onPressed: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    contextMenuPress = true
                                    return
                                }
                                if (mouse.button === Qt.MiddleButton) {
                                    canvasPanCandidate = root.beginCanvasPan(mouse.x, mouse.y)
                                    canvasPanned = false
                                    canvasPressX = mouse.x
                                    canvasPressY = mouse.y
                                    return
                                }
                                contextMenuPress = false
                                if (root.armedLibraryEntry && root.armedLibraryEntry.type) {
                                    canvasPanCandidate = false
                                    canvasPanned = false
                                    root.updateLibraryPlacementPoint(mouse.x, mouse.y)
                                    return
                                }
                                const hit = root.hitWire(mouse.x, mouse.y)
                                canvasPanCandidate = !hit || Boolean(mouse.modifiers & Qt.SpaceModifier)
                                canvasPanned = false
                                canvasPressX = mouse.x
                                canvasPressY = mouse.y
                                if (canvasPanCandidate) root.beginCanvasPan(mouse.x, mouse.y)
                                if (!hit) root.dismissEmptyGraphAt(mouse.x, mouse.y)
                            }
                            onReleased: function(mouse) {
                                if (mouse.button === Qt.LeftButton || mouse.button === Qt.MiddleButton) {
                                    if (canvasPanCandidate) root.endCanvasPan()
                                    canvasPanCandidate = false
                                    return
                                }
                                if (!contextMenuPress || mouse.button !== Qt.RightButton) return
                                const hit = root.hitWire(mouse.x, mouse.y)
                                if (hit && hit.route) {
                                    root.inspectedRoute = hit.route
                                    root.selectedSegmentId = String(hit.routeSegmentId || "")
                                    root.inspectedNode = ({})
                                    root.source = ({})
                                    deckRouteContextMenu.targetRoute = hit.route
                                    deckRouteContextMenu.open()
                                } else {
                                    deckCanvasContextMenu.canvasX = mouse.x
                                    deckCanvasContextMenu.canvasY = mouse.y
                                    deckCanvasContextMenu.open()
                                }
                                contextMenuPress = false
                            }
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) return
                                if (canvasPanned) {
                                    canvasPanned = false
                                    return
                                }
                                if (mouse.button === Qt.MiddleButton) return
                                if (root.armedLibraryEntry && root.armedLibraryEntry.type) {
                                    root.placeArmedLibraryEntry(mouse.x, mouse.y)
                                    return
                                }
                                const hit = root.hitWire(mouse.x, mouse.y)
                                if (!hit || !hit.route) {
                                    root.clearGraphSelection()
                                    return
                                }
                                root.inspectedRoute = hit.route
                                root.selectedSegmentId = String(hit.routeSegmentId || "")
                                root.inspectedNode = ({})
                                root.source = ({})
                            }
                        }
                        // Processor chips target the same cached segment
                        // geometry used by Canvas and pointer selection. No
                        // surrogate lane or invisible drop strip participates.
                        DropArea {
                            anchors.fill: parent
                            z: 1
                            keys: ["signal-flow-processor"]
                            onEntered: function(drag) {
                                if (drag.source && drag.source.libraryEntry)
                                    root.armLibraryEntry(drag.source.libraryEntry)
                                root.updateLibraryPlacementPoint(drag.x, drag.y)
                            }
                            onPositionChanged: function(drag) {
                                root.updateLibraryPlacementPoint(drag.x, drag.y)
                            }
                            onExited: {
                                root.pendingProcessorRouteId = ""
                                root.pendingProcessorSegmentId = ""
                                root.requestWirePaint()
                            }
                            onDropped: function(drop) {
                                if (!drop.source || !drop.source.libraryEntry) return
                                if (!root.armedLibraryEntry || !root.armedLibraryEntry.type)
                                    root.armLibraryEntry(drop.source.libraryEntry)
                                root.finishLibraryDragDrop(drop.source.libraryEntry, drop.x, drop.y)
                                drop.accepted = true
                            }
                        }
                        // Existing endpoints and presentation annotations use
                        // a second palette drop path. It only writes their
                        // existing workspace placement or annotation record;
                        // it can never create fake hardware or a loose route.
                        DropArea {
                            anchors.fill: parent
                            z: 1
                            keys: ["signal-flow-library"]
                            onEntered: function(drag) {
                                if (drag.source && drag.source.libraryEntry)
                                    root.armLibraryEntry(drag.source.libraryEntry)
                                root.updateLibraryPlacementPoint(drag.x, drag.y)
                            }
                            onPositionChanged: function(drag) { root.updateLibraryPlacementPoint(drag.x, drag.y) }
                            onDropped: function(drop) {
                                if (!drop.source || !drop.source.libraryEntry) return
                                if (!root.armedLibraryEntry || !root.armedLibraryEntry.type)
                                    root.armLibraryEntry(drop.source.libraryEntry)
                                root.finishLibraryDragDrop(drop.source.libraryEntry, drop.x, drop.y)
                                drop.accepted = true
                            }
                        }
                        FlightDeckCard {
                            id: inputNode
                            tokens: deck
                            readonly property var nodeData: root.node("input")
                            readonly property string routingState: root.routingNodeState(nodeData)
                            objectName: "signalFlowNodeCard:" + root.nodeIdentity(nodeData)
                            x: Number(root.settledNodePosition(nodeData, 80, 120).x)
                            y: Number(root.settledNodePosition(nodeData, 80, 120).y)
                            transform: Translate {
                                x: root.liveNodeTranslationX(inputNode.nodeData, 80, 120)
                                y: root.liveNodeTranslationY(inputNode.nodeData, 80, 120)
                            }
                            Behavior on x { enabled: !root.isLiveNodeDrag(inputNode.nodeData); NumberAnimation { duration: root.nodeMotionDuration(inputNode.nodeData); easing.type: Easing.OutCubic } }
                            Behavior on y { enabled: !root.isLiveNodeDrag(inputNode.nodeData); NumberAnimation { duration: root.nodeMotionDuration(inputNode.nodeData); easing.type: Easing.OutCubic } }
                            width: root.graphCardWidth(nodeData)
                            height: Math.max(root.graphCardHeight(nodeData), inputCardContent.implicitHeight + contentPadding * 2)
                            Behavior on height { NumberAnimation { duration: root.motionStructuralDuration; easing.type: Easing.OutCubic } }
                            z: routingState.length > 0 ? 3 : 2
                            opacity: (root.xrayMode ? 0.58 : 1.0) * root.nodeAppearanceOpacity(nodeData)
                            Behavior on opacity { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: root.routingNodeSurface(deck.secondarySurface, routingState)
                            border.width: root.routingNodeBorderWidth(routingState)
                            border.color: root.routingNodeBorderColor(routingState)
                            Behavior on color { ColorAnimation { duration: root.motionFastDuration } }
                            Column {
                                id: inputCardContent
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                z: 1
                                Text { width: parent.width; text: inputNode.nodeData.label || "Input context"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                Text { width: parent.width; text: inputNode.nodeData.connected ? "CONNECTED · VERIFIED" : "OFFLINE · SAVED IDENTITY"; color: inputNode.nodeData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                SignalFlowPortGroups { width: parent.width; nodeData: inputNode.nodeData; destination: false }
                            }
                            HoverHandler {
                                onHoveredChanged: root.setNodeHovered(inputNode.nodeData, hovered)
                            }
                            MouseArea {
                                objectName: "signalFlowNodeDrag:" + root.nodeIdentity(inputNode.nodeData)
                                anchors.fill: parent
                                z: 0
                                // A graph pan must never take ownership once a card body
                                // press begins. Port hit targets remain above this body area.
                                preventStealing: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property real pointerStartSceneX: 0
                                property real pointerStartSceneY: 0
                                property real nodeStartX: 0
                                property real nodeStartY: 0
                                property bool pointerMoved: false
                                property bool contextMenuPress: false
                                onPressed: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        contextMenuPress = true
                                        pointerMoved = false
                                        return
                                    }
                                    contextMenuPress = false
                                    if (root.autoLayoutMotionActive || root.mode !== "configured" || (root.graph.workspace && root.graph.workspace.layoutLocked)) return
                                    const point = mapToItem(scene, mouse.x, mouse.y)
                                    pointerStartSceneX = point.x; pointerStartSceneY = point.y
                                    nodeStartX = inputNode.x; nodeStartY = inputNode.y
                                    pointerMoved = false
                                    root.beginLiveNodeDrag(inputNode.nodeData)
                                    root.updateNodeSnapPreview(inputNode.nodeData, nodeStartX, nodeStartY,
                                        Boolean(mouse.modifiers & Qt.AltModifier))
                                }
                                onPositionChanged: function(mouse) {
                                    if (contextMenuPress) return
                                    if (!pressed || !root.isLiveNodeDrag(inputNode.nodeData)) return
                                    const point = mapToItem(scene, mouse.x, mouse.y)
                                    const x = nodeStartX + point.x - pointerStartSceneX
                                    const y = nodeStartY + point.y - pointerStartSceneY
                                    if (Math.abs(x - nodeStartX) > 6 || Math.abs(y - nodeStartY) > 6) pointerMoved = true
                                    if (pointerMoved) {
                                        root.queueLiveNodeDrag(inputNode.nodeData, x, y,
                                            Boolean(mouse.modifiers & Qt.AltModifier))
                                    }
                                }
                                onReleased: function(mouse) {
                                    if (contextMenuPress) return
                                    if (!root.isLiveNodeDrag(inputNode.nodeData)) return
                                    if (pointerMoved) root.finishLiveNodeDrag(inputNode.nodeData,
                                        root.nodePosition(inputNode.nodeData, 80, 120).x,
                                        root.nodePosition(inputNode.nodeData, 80, 120).y,
                                        true, Boolean(mouse.modifiers & Qt.AltModifier))
                                    else root.cancelLiveNodeDrag(inputNode.nodeData)
                                }
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        deckNodeContextMenu.targetNode = inputNode.nodeData
                                        deckNodeContextMenu.open()
                                        return
                                    }
                                    if (!pointerMoved) root.selectNode(inputNode.nodeData)
                                }
                                onDoubleClicked: function(mouse) { if (!pointerMoved) root.openCardSettings(inputNode.nodeData) }
                            }
                        }
                        Repeater {
                            model: (root.graph.nodes || []).filter(function(item) {
                                return item.kind === "input" && String(item.id || "") !== String(root.graph.inputNodeId || "")
                            })
                            delegate: FlightDeckCard {
                                id: secondaryInputNode
                                required property var modelData
                                tokens: deck
                                readonly property string routingState: root.routingNodeState(modelData)
                                readonly property var channelPairs: root.processorChannelPairs(modelData)
                                objectName: "signalFlowNodeCard:" + root.nodeIdentity(modelData)
                                x: Number(root.settledNodePosition(modelData, 80, 120).x)
                                y: Number(root.settledNodePosition(modelData, 80, 120).y)
                                transform: Translate {
                                    x: root.liveNodeTranslationX(modelData, 80, 120)
                                    y: root.liveNodeTranslationY(modelData, 80, 120)
                                }
                                Behavior on x { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.nodeMotionDuration(modelData); easing.type: Easing.OutCubic } }
                                Behavior on y { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.nodeMotionDuration(modelData); easing.type: Easing.OutCubic } }
                                width: root.graphCardWidth(modelData)
                                height: Math.max(root.graphCardHeight(modelData), secondaryCardContent.implicitHeight + contentPadding * 2)
                                z: routingState.length > 0 ? 3 : 2
                                opacity: (root.xrayMode ? 0.58 : 1.0) * root.nodeAppearanceOpacity(modelData)
                                Behavior on opacity { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: root.routingNodeSurface(modelData.missingReference
                                    ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.12) : deck.secondarySurface,
                                    routingState)
                                border.width: root.routingNodeBorderWidth(routingState)
                                border.color: root.routingNodeBorderColor(routingState)
                                Behavior on color { ColorAnimation { duration: root.motionFastDuration } }
                                Column {
                                    id: secondaryCardContent
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                    z: 1
                                    Text { width: parent.width; text: modelData.label || "Saved input"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                    Text { width: parent.width; text: modelData.connected ? "CONNECTED · SAVED MEMBER" : modelData.missingReference ? "MISSING REFERENCE" : "OFFLINE · SAVED MEMBER"; color: modelData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                    SignalFlowPortGroups { width: parent.width; nodeData: modelData; destination: false }
                                }
                                HoverHandler {
                                    onHoveredChanged: root.setNodeHovered(modelData, hovered)
                                }
                                MouseArea {
                                    objectName: "signalFlowNodeDrag:" + root.nodeIdentity(modelData)
                                    anchors.fill: parent
                                    z: 0
                                    preventStealing: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    property real pointerStartSceneX: 0
                                    property real pointerStartSceneY: 0
                                    property real nodeStartX: 0
                                    property real nodeStartY: 0
                                    property bool pointerMoved: false
                                    property bool contextMenuPress: false
                                    onPressed: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            contextMenuPress = true
                                            pointerMoved = false
                                            return
                                        }
                                        contextMenuPress = false
                                        if (root.autoLayoutMotionActive || root.mode !== "configured" || (root.graph.workspace && root.graph.workspace.layoutLocked)) return
                                        const point = mapToItem(scene, mouse.x, mouse.y)
                                        pointerStartSceneX = point.x; pointerStartSceneY = point.y
                                        nodeStartX = secondaryInputNode.x; nodeStartY = secondaryInputNode.y
                                        pointerMoved = false
                                        root.beginLiveNodeDrag(modelData)
                                        root.updateNodeSnapPreview(modelData, nodeStartX, nodeStartY,
                                            Boolean(mouse.modifiers & Qt.AltModifier))
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (contextMenuPress) return
                                        if (!pressed || !root.isLiveNodeDrag(modelData)) return
                                        const point = mapToItem(scene, mouse.x, mouse.y)
                                        const x = nodeStartX + point.x - pointerStartSceneX
                                        const y = nodeStartY + point.y - pointerStartSceneY
                                        if (Math.abs(x - nodeStartX) > 6 || Math.abs(y - nodeStartY) > 6) pointerMoved = true
                                        if (pointerMoved) {
                                            root.queueLiveNodeDrag(modelData, x, y,
                                                Boolean(mouse.modifiers & Qt.AltModifier))
                                        }
                                    }
                                    onReleased: function(mouse) {
                                        if (contextMenuPress) return
                                        if (!root.isLiveNodeDrag(modelData)) return
                                        if (pointerMoved) root.finishLiveNodeDrag(modelData,
                                            root.nodePosition(modelData, 80, 120).x,
                                            root.nodePosition(modelData, 80, 120).y,
                                            true, Boolean(mouse.modifiers & Qt.AltModifier))
                                        else root.cancelLiveNodeDrag(modelData)
                                    }
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            deckNodeContextMenu.targetNode = modelData
                                            deckNodeContextMenu.open()
                                            return
                                        }
                                        if (!pointerMoved) root.selectNode(modelData)
                                    }
                                    onDoubleClicked: function(mouse) { if (!pointerMoved) root.openCardSettings(modelData) }
                                }
                            }
                        }
                        Repeater {
                            model: (root.graph.nodes || []).filter(function(item) { return item.kind === "processor" })
                            delegate: FlightDeckCard {
                                id: processorNode
                                required property var modelData
                                tokens: deck
                                readonly property string routingState: root.routingNodeState(modelData)
                                readonly property var channelPairs: root.processorDisplayChannelPairs(modelData)
                                objectName: "signalFlowNodeCard:" + root.nodeIdentity(modelData)
                                x: Number(root.settledNodePosition(modelData, 680, 120).x)
                                y: Number(root.settledNodePosition(modelData, 680, 120).y)
                                transform: Translate {
                                    x: root.liveNodeTranslationX(modelData, 680, 120)
                                    y: root.liveNodeTranslationY(modelData, 680, 120)
                                }
                                Behavior on x { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.nodeMotionDuration(modelData); easing.type: Easing.OutCubic } }
                                Behavior on y { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.nodeMotionDuration(modelData); easing.type: Easing.OutCubic } }
                                width: root.graphCardWidth(modelData)
                                height: Math.max(root.graphCardHeight(modelData), processorCardContent.implicitHeight + contentPadding * 2)
                                z: routingState.length > 0 ? 3 : 2
                                opacity: (root.xrayMode ? 0.58 : 1.0) * root.nodeAppearanceOpacity(modelData)
                                Behavior on opacity { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                                Behavior on height { NumberAnimation { duration: root.motionStructuralDuration; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: root.routingNodeSurface(deck.elevatedSurface, routingState)
                                border.width: root.routingNodeBorderWidth(routingState)
                                border.color: root.routingNodeBorderColor(routingState)
                                Behavior on color { ColorAnimation { duration: root.motionFastDuration } }
                                Column {
                                    id: processorCardContent
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: 4
                                    z: 1
                                    width: parent.width
                                    Text { width: parent.width; text: modelData.label; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight }
                                    Text {
                                        width: parent.width
                                        text: "PROCESSOR · " + String(modelData.semantic || "signal").toUpperCase()
                                            + (modelData.shared ? " · SHARED" : "")
                                        color: modelData.shared ? deck.healthy : deck.graphLabel
                                        font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight
                                    }
                                    Repeater {
                                        // Each visible row corresponds to the exact two endpoint
                                        // IDs projected by AppBackend. It is the same card/port
                                        // language as device and output blocks, not a chip label.
                                        model: processorNode.channelPairs
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: processorCardContent.width
                                            height: 24
                                            radius: deck.radiusControl
                                            color: Boolean(modelData.pending) ? deck.selected : deck.secondarySurface
                                            border.width: 1
                                            border.color: Boolean(modelData.pending) ? deck.attention : deck.border
                                            Text {
                                                anchors.left: parent.left; anchors.leftMargin: deck.space6; anchors.verticalCenter: parent.verticalCenter
                                                text: Boolean(modelData.pending) ? "NEW IN · ARMED" : "CH " + (index + 1) + " · IN"
                                                color: Boolean(modelData.pending) ? deck.attention : deck.informational
                                                font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
                                            }
                                            Text {
                                                anchors.right: parent.right; anchors.rightMargin: deck.space6; anchors.verticalCenter: parent.verticalCenter
                                                text: Boolean(modelData.pending) ? "ARMED · NEW OUT" : "OUT · CH " + (index + 1)
                                                color: Boolean(modelData.pending) ? deck.attention : deck.healthy
                                                font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
                                            }
                                        }
                                    }
                                    Text {
                                        visible: root.processorChannelDwellActive(processorNode.modelData)
                                        width: parent.width
                                        text: "ADDING CANONICAL CHANNEL PAIR…"
                                        color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: !root.processorChannelDwellActive(processorNode.modelData)
                                        width: parent.width
                                        text: modelData.detail
                                        color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 8; elide: Text.ElideRight
                                    }
                                }
                                Repeater {
                                    // These markers own the exact endpoint ids that the
                                    // canonical segments reference. Their larger hit area
                                    // stays keyboard/pointer discoverable while the visual
                                    // port remains compact.
                                    model: modelData.ports || []
                                    delegate: Rectangle {
                                        id: processorPortMarker
                                        required property var modelData
                                        required property int index
                                        readonly property bool inputPort: String(modelData.direction || "") === "input"
                                        objectName: "signalFlowProcessorPortVisual:" + String(modelData.endpointId || modelData.id || "")
                                        width: 10; height: 10; radius: 5
                                        x: inputPort ? -width / 2 : processorNode.width - width / 2
                                        y: processorNode.contentPadding + 43
                                            + root.processorChannelPairForPort(processorNode.modelData, modelData) * 28
                                        z: 5
                                        color: inputPort ? deck.informational : deck.healthy
                                        border.color: deck.applicationBackground
                                        border.width: 2
                                        Accessible.name: String(modelData.label || (inputPort ? "Processor input" : "Processor output"))
                                        Accessible.description: String(modelData.accessibleDescription || "Canonical Signal Flow processor port")
                                        function registerAnchor() {
                                            if (!scene || !visible) return
                                            const point = mapToItem(scene, width * 0.5, height * 0.5)
                                            root.notePortAnchor(modelData, point.x, point.y)
                                        }
                                        Component.onCompleted: registerAnchor()
                                        onXChanged: registerAnchor()
                                        onYChanged: registerAnchor()
                                        onWidthChanged: registerAnchor()
                                        onHeightChanged: registerAnchor()
                                        Connections {
                                            target: root
                                            function onPortAnchorMeasurementEpochChanged() { processorPortMarker.registerAnchor() }
                                        }
                                        MouseArea {
                                            anchors.centerIn: parent
                                            width: 30; height: 30
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: {
                                                if (mouse.button === Qt.RightButton) {
                                                    deckPortContextMenu.targetPort = modelData
                                                    deckPortContextMenu.targetOwner = processorNode.modelData
                                                    deckPortContextMenu.open()
                                                    return
                                                }
                                                root.inspectPort(modelData, processorNode.modelData)
                                            }
                                        }
                                    }
                                }
                                // While the source hovers over this processor, expose the
                                // prospective pair as real-sized, highlighted sockets. The
                                // backend replaces these preview sockets with canonical port
                                // IDs when the dwell completes.
                                Repeater {
                                    model: root.processorPendingPairIndex(processorNode.modelData) >= 0 ? [true] : []
                                    delegate: Item {
                                        readonly property int pairIndex: root.processorPendingPairIndex(processorNode.modelData)
                                        y: processorNode.contentPadding + 43 + pairIndex * 28 - 5
                                        width: processorNode.width; height: 10
                                        z: 6
                                        Rectangle {
                                            width: 12; height: 12; radius: 6
                                            x: -width / 2; anchors.verticalCenter: parent.verticalCenter
                                            color: deck.attention; border.color: deck.elevatedSurface; border.width: 2
                                        }
                                        Rectangle {
                                            width: 12; height: 12; radius: 6
                                            x: parent.width - width / 2; anchors.verticalCenter: parent.verticalCenter
                                            color: deck.attention; border.color: deck.elevatedSurface; border.width: 2
                                        }
                                    }
                                }
                                HoverHandler {
                                    onHoveredChanged: {
                                        root.setNodeHovered(modelData, hovered)
                                        if (hovered) root.beginProcessorChannelDwell(modelData)
                                        else root.clearProcessorChannelDwell(modelData)
                                    }
                                }
                                DropArea {
                                    anchors.fill: parent
                                    z: 2
                                    keys: ["signal-flow-source"]
                                    onEntered: function(drag) { root.beginProcessorChannelDwell(modelData) }
                                    onExited: root.clearProcessorChannelDwell(modelData)
                                    onDropped: function(drop) {
                                        // A release before the dwell completes is intentionally a
                                        // no-op: the channel pair must be a deliberate canonical
                                        // shared-processor edit, not an accidental pass over a card.
                                        if (!root.processorChannelCommitInFlight && !root.sourceDragDropHandled) {
                                            root.cancelRouting("Hold over the processor for one second to add a channel pair.", true)
                                        }
                                        drop.accepted = true
                                    }
                                }
                                MouseArea {
                                    objectName: "signalFlowNodeDrag:" + root.nodeIdentity(modelData)
                                    anchors.fill: parent
                                    z: 0
                                    preventStealing: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    property real pointerStartSceneX: 0
                                    property real pointerStartSceneY: 0
                                    property real nodeStartX: 0
                                    property real nodeStartY: 0
                                    property bool pointerMoved: false
                                    property bool contextMenuPress: false
                                    onPressed: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            contextMenuPress = true
                                            pointerMoved = false
                                            return
                                        }
                                        contextMenuPress = false
                                        if (root.autoLayoutMotionActive || root.mode !== "configured" || (root.graph.workspace && root.graph.workspace.layoutLocked)) return
                                        const point = mapToItem(scene, mouse.x, mouse.y)
                                        pointerStartSceneX = point.x; pointerStartSceneY = point.y
                                        nodeStartX = processorNode.x; nodeStartY = processorNode.y
                                        pointerMoved = false
                                        root.beginLiveNodeDrag(modelData)
                                        root.updateNodeSnapPreview(modelData, nodeStartX, nodeStartY,
                                            Boolean(mouse.modifiers & Qt.AltModifier))
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (contextMenuPress) return
                                        if (!pressed || !root.isLiveNodeDrag(modelData)) return
                                        const point = mapToItem(scene, mouse.x, mouse.y)
                                        const x = nodeStartX + point.x - pointerStartSceneX
                                        const y = nodeStartY + point.y - pointerStartSceneY
                                        if (Math.abs(x - nodeStartX) > 6 || Math.abs(y - nodeStartY) > 6) pointerMoved = true
                                        if (pointerMoved) {
                                            root.queueLiveNodeDrag(modelData, x, y,
                                                Boolean(mouse.modifiers & Qt.AltModifier))
                                        }
                                    }
                                    onReleased: function(mouse) {
                                        if (contextMenuPress) return
                                        if (!root.isLiveNodeDrag(modelData)) return
                                        if (pointerMoved) root.finishLiveNodeDrag(modelData,
                                            root.nodePosition(modelData, 680, 120).x,
                                            root.nodePosition(modelData, 680, 120).y,
                                            true, Boolean(mouse.modifiers & Qt.AltModifier))
                                        else root.cancelLiveNodeDrag(modelData)
                                    }
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            deckNodeContextMenu.targetNode = modelData
                                            deckNodeContextMenu.open()
                                            return
                                        }
                                        if (!pointerMoved) root.selectNode(modelData)
                                    }
                                    onDoubleClicked: function(mouse) { if (!pointerMoved) root.openNodeSettings(modelData) }
                                }
                            }
                        }
                        FlightDeckCard {
                            id: outputNode
                            tokens: deck
                            readonly property var nodeData: root.node("output")
                            readonly property string routingState: root.routingNodeState(nodeData)
                            objectName: "signalFlowNodeCard:" + root.nodeIdentity(nodeData)
                            x: Number(root.settledNodePosition(nodeData, 1320, 120).x)
                            y: Number(root.settledNodePosition(nodeData, 1320, 120).y)
                            transform: Translate {
                                x: root.liveNodeTranslationX(outputNode.nodeData, 1320, 120)
                                y: root.liveNodeTranslationY(outputNode.nodeData, 1320, 120)
                            }
                            Behavior on x { enabled: !root.isLiveNodeDrag(outputNode.nodeData); NumberAnimation { duration: root.nodeMotionDuration(outputNode.nodeData); easing.type: Easing.OutCubic } }
                            Behavior on y { enabled: !root.isLiveNodeDrag(outputNode.nodeData); NumberAnimation { duration: root.nodeMotionDuration(outputNode.nodeData); easing.type: Easing.OutCubic } }
                            width: root.graphCardWidth(nodeData)
                            height: Math.max(root.graphCardHeight(nodeData), outputCardContent.implicitHeight + contentPadding * 2)
                            Behavior on height { NumberAnimation { duration: root.motionStructuralDuration; easing.type: Easing.OutCubic } }
                            z: routingState.length > 0 ? 3 : 2
                            opacity: (root.xrayMode ? 0.58 : 1.0) * root.nodeAppearanceOpacity(nodeData)
                            Behavior on opacity { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: root.routingNodeSurface(deck.secondarySurface, routingState)
                            border.width: root.routingNodeBorderWidth(routingState)
                            border.color: root.routingNodeBorderColor(routingState)
                            Behavior on color { ColorAnimation { duration: root.motionFastDuration } }
                            Column {
                                id: outputCardContent
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                z: 1
                                Text { width: parent.width; text: outputNode.nodeData.label || "Virtual output"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                Text { width: parent.width; text: outputNode.nodeData.connected ? "READY · VIRTUAL OUTPUT" : "OUTPUT UNAVAILABLE"; color: outputNode.nodeData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                SignalFlowPortGroups { width: parent.width; nodeData: outputNode.nodeData; destination: true }
                            }
                            HoverHandler {
                                onHoveredChanged: root.setNodeHovered(outputNode.nodeData, hovered)
                            }
                            MouseArea {
                                objectName: "signalFlowNodeDrag:" + root.nodeIdentity(outputNode.nodeData)
                                anchors.fill: parent
                                z: 0
                                preventStealing: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property real pointerStartSceneX: 0
                                property real pointerStartSceneY: 0
                                property real nodeStartX: 0
                                property real nodeStartY: 0
                                property bool pointerMoved: false
                                property bool contextMenuPress: false
                                onPressed: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        contextMenuPress = true
                                        pointerMoved = false
                                        return
                                    }
                                    contextMenuPress = false
                                    if (root.autoLayoutMotionActive || root.mode !== "configured" || (root.graph.workspace && root.graph.workspace.layoutLocked)) return
                                    const point = mapToItem(scene, mouse.x, mouse.y)
                                    pointerStartSceneX = point.x; pointerStartSceneY = point.y
                                    nodeStartX = outputNode.x; nodeStartY = outputNode.y
                                    pointerMoved = false
                                    root.beginLiveNodeDrag(outputNode.nodeData)
                                    root.updateNodeSnapPreview(outputNode.nodeData, nodeStartX, nodeStartY,
                                        Boolean(mouse.modifiers & Qt.AltModifier))
                                }
                                onPositionChanged: function(mouse) {
                                    if (contextMenuPress) return
                                    if (!pressed || !root.isLiveNodeDrag(outputNode.nodeData)) return
                                    const point = mapToItem(scene, mouse.x, mouse.y)
                                    const x = nodeStartX + point.x - pointerStartSceneX
                                    const y = nodeStartY + point.y - pointerStartSceneY
                                    if (Math.abs(x - nodeStartX) > 6 || Math.abs(y - nodeStartY) > 6) pointerMoved = true
                                    if (pointerMoved) {
                                        root.queueLiveNodeDrag(outputNode.nodeData, x, y,
                                            Boolean(mouse.modifiers & Qt.AltModifier))
                                    }
                                }
                                onReleased: function(mouse) {
                                    if (contextMenuPress) return
                                    if (!root.isLiveNodeDrag(outputNode.nodeData)) return
                                    if (pointerMoved) root.finishLiveNodeDrag(outputNode.nodeData,
                                        root.nodePosition(outputNode.nodeData, 1320, 120).x,
                                        root.nodePosition(outputNode.nodeData, 1320, 120).y,
                                        true, Boolean(mouse.modifiers & Qt.AltModifier))
                                    else root.cancelLiveNodeDrag(outputNode.nodeData)
                                }
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        deckNodeContextMenu.targetNode = outputNode.nodeData
                                        deckNodeContextMenu.open()
                                        return
                                    }
                                    if (!pointerMoved) root.selectNode(outputNode.nodeData)
                                }
                                onDoubleClicked: function(mouse) { if (!pointerMoved) root.openCardSettings(outputNode.nodeData) }
                            }
                        }
                        Column {
                            x: 470; y: 660; width: 620; spacing: 4
                            // Retired V2.6.0 surrogate: Canvas geometry is the
                            // only wire hover/click/context target in this page.
                            visible: false
                            enabled: false
                            Text { text: "ROUTE LANES · click to inspect"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            Repeater {
                                model: root.graph.routes || []
                                delegate: Rectangle {
                                    required property var modelData
                                    width: parent.width; height: 25; radius: deck.radiusControl
                                    color: routeHit.containsMouse || processorDrop.containsDrag || (root.inspectedRoute && root.inspectedRoute.id === modelData.id) ? deck.selected : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: deck.space8
                                        anchors.rightMargin: deck.space8
                                        Text { Layout.fillWidth: true; text: modelData.sourceLabel + "  →  " + (modelData.viaNodeId ? "Mixer  →  " : "") + modelData.destinationLabel; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; elide: Text.ElideRight }
                                        Text { text: modelData.effective ? "ACTIVE" : String(modelData.health || "configured").toUpperCase().replace("-", " "); color: modelData.effective ? deck.healthy : modelData.health === "conflict" ? deck.danger : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                    }
                                    DropArea {
                                        id: processorDrop
                                        anchors.fill: parent
                                        keys: ["signal-flow-processor"]
                                        onEntered: function(drag) {
                                            const segments = modelData.segments || []
                                            if (segments.length > 0) root.previewProcessorTarget(modelData, String(segments[0].id || ""))
                                        }
                                        onExited: function(drag) {
                                            if (root.pendingProcessorRouteId === String(modelData.id || "")) {
                                                root.pendingProcessorRouteId = ""
                                                root.pendingProcessorSegmentId = ""
                                            }
                                        }
                                        onDropped: function(drop) {
                                            if (drop.source && drop.source.processorKind) {
                                                root.inspectedRoute = modelData
                                                root.inspectedNode = ({})
                                                const segments = modelData.segments || []
                                                const result = backendObject.signalFlowInsertProcessor(
                                                    segments.length > 0 ? String(segments[0].id || "") : "",
                                                    String(drop.source.processorKind), Number(root.graph.revision || 0))
                                                root.announce(result, "Processor was not inserted.")
                                                drop.accepted = true
                                            }
                                        }
                                    }
                                    MouseArea {
                                        id: routeHit
                                        objectName: "signalFlowRouteHit:" + String(modelData.id || "")
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        Accessible.name: "Route " + modelData.sourceLabel + " to " + modelData.destinationLabel
                                        Accessible.description: "Select this route; right-click for processing actions; press Delete to disconnect it in Configured view."
                                        onClicked: function(mouse) {
                                            root.inspectedRoute = modelData
                                            root.source = ({})
                                            root.inspectedNode = ({})
                                            if (mouse.button === Qt.RightButton) {
                                                deckRouteContextMenu.targetRoute = modelData
                                                deckRouteContextMenu.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: deck.space12
                    visible: Boolean(root.connectionPreview && root.connectionPreview.message) || Boolean(root.source && root.source.id)
                    text: root.connectionPreview && root.connectionPreview.message ? root.connectionPreview.message
                        : "Choose a compatible destination for " + String(root.source.label || "the selected source") + "."
                    color: deck.graphLabel
                    font.family: deck.bodyFont
                    font.pixelSize: 9
                }
                WheelHandler {
                    target: null
                    onWheel: function(event) {
                        // Precision touchpads report pixel deltas. Preserve
                        // their natural two-finger pan while ordinary mouse
                        // wheel ticks zoom around the pointer.
                        if (Math.abs(Number(event.pixelDelta.x || 0)) > 0
                                || Math.abs(Number(event.pixelDelta.y || 0)) > 0) {
                            root.panCanvasBy(-Number(event.pixelDelta.x || 0),
                                -Number(event.pixelDelta.y || 0))
                        } else {
                            const ticks = Number(event.angleDelta.y || 0)
                            root.zoomAtViewport(Math.pow(1.0018, ticks),
                                Number(event.point.position.x || graphViewport.width * 0.5),
                                Number(event.point.position.y || graphViewport.height * 0.5))
                        }
                        event.accepted = true
                    }
                }
                PinchHandler {
                    id: graphPinch
                    target: null
                    property real priorScale: 1
                    onActiveChanged: { if (active) priorScale = 1 }
                    onScaleChanged: {
                        if (!active || !isFinite(Number(scale)) || Number(scale) <= 0) return
                        root.zoomAtViewport(Number(scale) / priorScale,
                            Number(centroid.position.x || graphViewport.width * 0.5),
                            Number(centroid.position.y || graphViewport.height * 0.5))
                        priorScale = Number(scale)
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                // Contextual inspector: its reserved space is released when
                // inactive, while its content crossfades rather than making a
                // selected object appear to jump to a different panel.
                readonly property bool inspectorActive: false
                visible: false
                enabled: inspectorActive
                clip: true
                opacity: inspectorActive ? 1 : 0
                Layout.minimumWidth: 0
                Layout.maximumWidth: inspectorActive ? Math.max(220, Math.min(305, root.width * 0.25)) : 0
                Layout.preferredWidth: inspectorActive ? Math.max(220, Math.min(305, root.width * 0.25)) : 0
                Layout.fillHeight: true
                contentPadding: deck.cardPaddingTechnical
                Behavior on opacity { NumberAnimation { duration: root.motionStructuralDuration; easing.type: Easing.OutCubic } }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: deck.space8
                    opacity: root.inspectorContentReveal
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "INSPECTOR"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true }
                        DeckButton { text: "×"; width: 28; height: 22; padding: 0; Accessible.name: "Close inspector"; onClicked: { root.inspectedRoute = ({}); root.inspectedNode = ({}) } }
                    }
                    Text { text: root.inspectedRoute && root.inspectedRoute.id ? root.inspectedRoute.sourceLabel + " → " + root.inspectedRoute.destinationLabel : root.inspectedNode && root.inspectedNode.id ? root.inspectedNode.label : "Select a route or destination"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); text: String(root.inspectedNode.detail || "") + (root.inspectedNode.capabilitySummary ? "\n" + root.inspectedNode.capabilitySummary : ""); color: root.inspectedNode && root.inspectedNode.missingReference ? deck.attention : deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor"); text: "PROCESSOR ID · " + String(root.inspectedNode.objectId || "") + "\nPORTS · " + Number(root.inspectedNode.channelCount || 0) + " channel pair" + (Number(root.inspectedNode.channelCount || 0) === 1 ? "" : "s"); color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); text: Number(root.inspectedNode.routeCount || 0) + " configured route" + (Number(root.inspectedNode.routeCount || 0) === 1 ? "" : "s") + (root.inspectedNode.connected ? " · READY" : " · OFFLINE / UNAVAILABLE"); color: root.inspectedNode && root.inspectedNode.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    DeckButton { visible: Boolean(root.inspectedNode && (root.inspectedNode.kind === "input" || root.inspectedNode.kind === "output")); text: root.inspectedNode && root.inspectedNode.kind === "input" ? "Open input setup" : "Open output setup"; helpText: "Open Devices & setup while preserving this Flight Deck Signal Flow card selection for return."; Layout.fillWidth: true; onClicked: root.openCardSettings(root.inspectedNode) }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: root.inspectedRoute && root.inspectedRoute.processors && root.inspectedRoute.processors.length ? "Conditioning nodes: " + root.inspectedRoute.processors.length + (root.inspectedRoute.viaNodeId ? " · explicit mixer" : "") : "Direct route — no visible conditioning node."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.processorDetail("adaptive-response").settingsSummary); text: "ADAPTIVE RESPONSE · " + String(root.processorDetail("adaptive-response").settingsSummary); color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: String(root.inspectedRoute.health || "ready").toUpperCase().replace("-", " ") + " · " + String(root.inspectedRoute.healthDetail || ""); color: root.inspectedRoute.health === "ready" ? deck.healthy : root.inspectedRoute.health === "conflict" ? deck.danger : deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id && (root.signalFocus || root.liveMode)); text: "LIVE SAMPLE " + Number(root.routeLive(root.inspectedRoute).value || 0).toFixed(3) + (root.routeIsLive(root.inspectedRoute) ? " · MOVING" : " · STEADY"); color: root.routeIsLive(root.inspectedRoute) ? deck.healthy : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    DeckButton { text: "Explain route"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; onClicked: root.explainRoute() }
                    DeckButton { text: "Open Curve Editor"; visible: Boolean(root.inspectedRoute && root.routeHasProcessor(root.inspectedRoute, "curve")); helpText: "Open the authoritative Curve Editor for this source axis and preserve this Flight Deck Signal Flow selection for return."; Layout.fillWidth: true; onClicked: root.openFullSettings("curve", root.inspectedRoute) }
                    DeckButton { text: "Open Adaptive Response"; visible: Boolean(root.inspectedRoute && root.routeHasProcessor(root.inspectedRoute, "adaptive-response")); helpText: "Open the authoritative Adaptive Response editor for this source axis and preserve this Flight Deck Signal Flow selection for return."; Layout.fillWidth: true; onClicked: root.openFullSettings("adaptive-response", root.inspectedRoute) }
                    DeckButton { text: root.inspectedNode && root.inspectedNode.semantic === "curve" ? "Open Curve Editor" : "Open Adaptive Response"; visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor" && (root.inspectedNode.semantic === "curve" || root.inspectedNode.semantic === "adaptive-response")); Layout.fillWidth: true; onClicked: root.openNodeSettings(root.inspectedNode) }
                    DeckButton { text: root.inspectedNode && root.inspectedNode.shared ? "Remove this shared channel" : "Remove processor"; destructive: true; visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor"); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.removeSelectedProcessor() }
                    DeckButton { text: "Processor palette"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: processorDialog.open() }
                    DeckButton { text: "Share active processor…"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.openShareProcessorDialog() }
                    DeckButton { text: "Disconnect selected"; destructive: true; helpText: "Remove this canonical route. Shortcut: Delete."; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    ScrollView {
                        // Port browsing is intentionally retired: destination
                        // selection is performed on the output card itself.
                        visible: false
                        enabled: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 5
                            Repeater {
                                model: root.groups("output")
                                delegate: Column {
                                    required property var modelData
                                    property string groupName: modelData.group
                                    readonly property bool collapsed: root.groupCollapsed("output", groupName)
                                    width: parent.width; spacing: 2
                                    Row {
                                        width: parent.width; spacing: 4
                                        Text { width: parent.width - 34; text: parent.parent.groupName + " · " + root.groupPorts("output", parent.parent.groupName, 999).length; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                        DeckButton { width: 28; height: 20; padding: 0; text: parent.parent.collapsed ? "+" : "−"; Accessible.name: (parent.parent.collapsed ? "Expand " : "Collapse ") + parent.parent.groupName; onClicked: root.setGroup("output", parent.parent.groupName, !parent.parent.collapsed) }
                                    }
                                    Repeater {
                                        visible: !parent.collapsed
                                        model: parent.collapsed ? [] : root.groupPorts("output", parent.groupName,
                                            root.canvasPortLimit)
                                        delegate: FlowPort { required property var modelData; width: parent.width; port: modelData; destination: true; isCompatible: root.compatible(modelData) }
                                    }
                                }
                            }
                            Text { visible: root.destinationPorts().length === 0; width: parent.width; text: "No destinations match."; color: deck.textMuted; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: blockLibrary
        objectName: "signalFlowBlockLibraryPanel"
        parent: Overlay.overlay
        modal: false
        focus: true
        // A palette drag necessarily crosses the panel boundary. Never turn
        // that normal gesture into a popup close (and a destroyed delegate).
        // The explicit rounded close button and Escape remain available.
        closePolicy: Popup.CloseOnEscape
        width: Math.min(438, root.width - 32)
        height: Math.min(600, root.height - 32)
        x: root.overlayOrigin().x + Math.max(deck.space16, Math.min(root.width - width - deck.space16, root.blockLibraryPositionX))
        y: root.overlayOrigin().y + Math.max(deck.space16, Math.min(root.height - height - deck.space16, root.blockLibraryPositionY))
        padding: deck.cardPaddingTechnical
        onOpened: {
            root.ensureBlockLibraryPosition()
            blockLibrarySearch.forceActiveFocus()
        }
        // Popup itself is not an Item, so keyboard navigation deliberately
        // lives on the focused search field below.  The native close policy
        // still handles Escape whenever focus moves elsewhere in the panel.
        onClosed: root.cancelLibraryPlacement()
        background: Rectangle {
            radius: deck.radiusPanel
            color: deck.elevatedSurface
            border.color: deck.border
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: deck.space8
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 30
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "BLOCK LIBRARY"; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                        property real startX: 0; property real startY: 0
                        onPressed: function(mouse) { startX = mouse.x; startY = mouse.y }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            root.blockLibraryPositionX = Math.max(deck.space16, Math.min(root.width - blockLibrary.width - deck.space16,
                                root.blockLibraryPositionX + mouse.x - startX))
                            root.blockLibraryPositionY = Math.max(deck.space16, Math.min(root.height - blockLibrary.height - deck.space16,
                                root.blockLibraryPositionY + mouse.y - startY))
                        }
                        onReleased: root.persistFloatingPanelPositions()
                    }
                }
                FloatingPanelCloseButton { Accessible.name: "Close Block Library"; onClicked: blockLibrary.close() }
            }
            TextField {
                id: blockLibrarySearch
                objectName: "signalFlowBlockLibrarySearch"
                Layout.fillWidth: true
                implicitHeight: 34
                placeholderText: "Search blocks..."
                text: root.blockLibraryQuery
                color: deck.textPrimary
                placeholderTextColor: deck.textMuted
                selectByMouse: true
                background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.width: 1; border.color: blockLibrarySearch.activeFocus ? deck.focus : deck.border }
                onTextChanged: { root.blockLibraryQuery = text; root.blockLibrarySelectionIndex = 0 }
                onAccepted: root.armLibraryEntry(root.selectedLibraryEntry())
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Up) { root.moveLibrarySelection(-1); event.accepted = true }
                    else if (event.key === Qt.Key_Down) { root.moveLibrarySelection(1); event.accepted = true }
                    else if (event.key === Qt.Key_Escape) {
                        if (!root.cancelLibraryPlacement()) blockLibrary.close()
                        event.accepted = true
                    }
                }
            }
            Text { Layout.fillWidth: true; text: root.armedLibraryEntry && root.armedLibraryEntry.type ? "PLACEMENT ARMED · move over the graph and click to place · Esc cancels" : "Enter arms the highlighted block for placement"; color: root.armedLibraryEntry && root.armedLibraryEntry.type ? deck.attention : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; wrapMode: Text.WordWrap }
            ScrollView {
                id: blockLibraryList
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                contentWidth: availableWidth
                Column {
                    id: libraryContent
                    width: Number(blockLibraryList.availableWidth || 0)
                    spacing: deck.space10
                    Text { visible: root.libraryEntries().length === 0; width: parent.width; text: "No canonical blocks match this library search."; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    Text { visible: root.blockLibraryCatalog.length === 0 && root.blockLibraryQuery.length === 0; width: parent.width; text: "No canonical insertable processor types are available in this build."; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
                    Repeater {
                        model: root.libraryEntries()
                        delegate: Column {
                            required property var modelData
                            required property int index
                            // ScrollView establishes its available width one polish later
                            // than this Repeater on the first open. Keep a numeric zero
                            // placeholder for that turn rather than assigning undefined
                            // to a live native delegate width.
                            width: Number(blockLibraryList.availableWidth || 0); spacing: deck.space5
                            readonly property var entries: root.libraryEntries()
                            readonly property bool categoryStart: index === 0 || entries[index - 1].category !== modelData.category
                            Text {
                                visible: parent.categoryStart
                                width: parent.width
                                text: modelData.category
                                color: deck.graphLabel
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                                topPadding: parent.categoryStart && index > 0 ? deck.space6 : 0
                            }
                            Rectangle {
                                id: libraryBlockCard
                                objectName: "signalFlowLibraryCard:" + String(modelData.type || "") + ":" + String(modelData.id || "")
                                width: parent.width
                                height: modelData.type === "node" ? 68 : modelData.type === "processor" ? 76 : 56
                                radius: deck.radiusCard
                                color: libraryBlockHover.hovered || root.blockLibrarySelectionIndex === index ? deck.selected : deck.secondarySurface
                                border.width: root.blockLibrarySelectionIndex === index ? 2 : 1
                                border.color: root.blockLibrarySelectionIndex === index ? deck.focus : deck.border
                                clip: false
                                Drag.active: libraryBlockDrag.active
                                Drag.source: libraryBlockCard
                                Drag.keys: modelData.type === "processor" ? ["signal-flow-processor"] : ["signal-flow-library"]
                                Drag.hotSpot.x: width * 0.5; Drag.hotSpot.y: height * 0.5
                                property string processorKind: String(modelData.id || "")
                                property var libraryEntry: modelData
                                Item {
                                    anchors.fill: parent
                                    anchors.margins: deck.space10
                                    Rectangle {
                                        visible: modelData.type !== "annotation"
                                        width: 10; height: 10; radius: 5
                                        x: modelData.type === "node" && modelData.kind === "virtual-output" ? parent.width - width * 0.5 : -width * 0.5
                                        y: parent.height * 0.5 - height * 0.5
                                        color: modelData.kind === "virtual-output" ? deck.healthy : deck.informational
                                        border.width: 2; border.color: deck.elevatedSurface
                                    }
                                    Rectangle {
                                        visible: modelData.type === "processor"
                                        width: 10; height: 10; radius: 5
                                        x: -width * 0.5; y: parent.height - height * 0.5
                                        color: deck.informational; border.width: 2; border.color: deck.elevatedSurface
                                    }
                                    Rectangle {
                                        visible: modelData.type === "processor"
                                        width: 10; height: 10; radius: 5
                                        x: parent.width - width * 0.5; y: parent.height - height * 0.5
                                        color: deck.healthy; border.width: 2; border.color: deck.elevatedSurface
                                    }
                                    Text {
                                        width: parent.width - (modelData.type === "node" ? 0 : 14)
                                        anchors.left: parent.left; anchors.top: parent.top
                                        text: String(modelData.label || "Block")
                                        color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width - 6
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 19
                                        text: modelData.type === "node" ? (modelData.kind === "physical-input" ? "PHYSICAL INPUT · ON CANVAS" : "VIRTUAL OUTPUT · ON CANVAS")
                                            : modelData.type === "processor" ? String(modelData.detail || "Canonical processor") : "PRESENTATION ONLY"
                                        color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: modelData.type === "processor"
                                        width: parent.width - 12
                                        anchors.left: parent.left; anchors.bottom: parent.bottom
                                        text: "INPUT  ─────────  OUTPUT"
                                        color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: modelData.type !== "processor"
                                        anchors.right: parent.right; anchors.bottom: parent.bottom
                                        text: "DRAG"
                                        color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
                                    }
                                }
                                HoverHandler { id: libraryBlockHover }
                                TapHandler { onTapped: { root.blockLibrarySelectionIndex = index; root.armLibraryEntry(modelData) } }
                                DragHandler {
                                    id: libraryBlockDrag
                                    // A library item is a permanent catalog
                                    // delegate, never the thing being moved.
                                    // Without this explicit null target Qt
                                    // moves the Rectangle itself, making it
                                    // appear consumed from the palette and
                                    // corrupting the drag's drop coordinates.
                                    target: null
                                    enabled: root.mode === "configured" && !(modelData.type === "node" && root.graph.workspace && root.graph.workspace.layoutLocked)
                                    onActiveChanged: {
                                        if (active) {
                                            root.libraryDragActive = true
                                            root.libraryDragDropHandled = false
                                            root.libraryDragEntry = modelData
                                            root.armLibraryEntry(modelData)
                                        } else if (root.libraryDragActive) {
                                            // Let DropArea.onDropped for this
                                            // release run first, regardless of
                                            // Qt's handler ordering.
                                            libraryDragCompletionTimer.restart()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: graphSettings
        objectName: "signalFlowGraphSettingsPanel"
        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(420, root.width - 32)
        padding: deck.cardPaddingTechnical
        x: root.overlayOrigin().x + Math.max(deck.space16, Math.min(root.width - width - deck.space16, root.graphSettingsPositionX))
        y: root.overlayOrigin().y + Math.max(deck.space16, Math.min(root.height - height - deck.space16, root.graphSettingsPositionY))
        onOpened: root.ensureGraphSettingsPosition()
        background: Rectangle {
            radius: deck.radiusPanel
            color: deck.elevatedSurface
            border.color: deck.border
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: deck.space10
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 30
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "GRAPH SETTINGS"; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                        property real startX: 0; property real startY: 0
                        onPressed: function(mouse) { startX = mouse.x; startY = mouse.y }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            root.graphSettingsPositionX = Math.max(deck.space16, Math.min(root.width - graphSettings.width - deck.space16,
                                root.graphSettingsPositionX + mouse.x - startX))
                            root.graphSettingsPositionY = Math.max(deck.space16, Math.min(root.height - graphSettings.height - deck.space16,
                                root.graphSettingsPositionY + mouse.y - startY))
                        }
                        onReleased: root.persistFloatingPanelPositions()
                    }
                }
                FloatingPanelCloseButton { Accessible.name: "Close Graph Settings"; onClicked: graphSettings.close() }
            }
            Text { Layout.fillWidth: true; text: "Presentation choices for this Rig and Profile. They never change routes, processors, or runtime mapping."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: visibilityGroup.implicitHeight + deck.space12
                radius: deck.radiusControl; color: deck.secondarySurface; border.width: 1; border.color: deck.border
                ColumnLayout {
                    id: visibilityGroup
                    anchors.fill: parent; anchors.margins: deck.space6; spacing: deck.space6
                    Text { text: "PORT VISIBILITY"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 3
                        Repeater {
                            model: [{"id":"smart", "label":"Smart"}, {"id":"connected", "label":"Connected"}, {"id":"compact", "label":"Compact"}, {"id":"expanded", "label":"Expanded"}]
                            delegate: Button {
                                required property var modelData
                                readonly property bool selected: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === modelData.id
                                Layout.fillWidth: true; implicitHeight: 28; padding: 3
                                Accessible.name: modelData.label + " port visibility"
                                contentItem: Text { text: modelData.label; color: selected ? deck.applicationBackground : deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 8; font.bold: selected; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                background: Rectangle { radius: 6; color: selected ? deck.accent : parent.hovered ? deck.selected : deck.control; border.width: 1; border.color: selected ? deck.accent : deck.border }
                                onClicked: root.setPortVisibility(modelData.id)
                            }
                        }
                    }
                    Text { Layout.fillWidth: true; text: "Smart shows connected ports in compact summaries. Explicit section choices always take priority."; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
                }
            }
            DeckToggle {
                Layout.fillWidth: true
                label: "Temporary hover expansion"
                checked: !root.graph.workspace || root.graph.workspace.autoExpandPorts !== false
                onToggled: root.persistWorkspace({ "autoExpandPorts": checked }, "Hover expansion " + (checked ? "enabled." : "disabled."))
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.border; opacity: 0.7 }
            Text { text: "WIRE STYLE"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
            RowLayout {
                Layout.fillWidth: true; spacing: deck.space6
                Repeater {
                    model: [{"id":"smooth", "label":"Smooth"}, {"id":"orthogonal", "label":"Orthogonal"}]
                    delegate: Button {
                        required property var modelData
                        readonly property bool selected: String(root.graph.workspace && root.graph.workspace.wireStyle || "smooth") === modelData.id
                        Layout.fillWidth: true; implicitHeight: 30
                        contentItem: Text { text: modelData.label; color: selected ? deck.applicationBackground : deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: deck.radiusControl; color: selected ? deck.accent : parent.hovered ? deck.selected : deck.secondarySurface; border.width: 1; border.color: selected ? deck.accent : deck.border }
                        onClicked: { if (!selected) root.toggleWireStyle() }
                    }
                }
            }
            DeckToggle { Layout.fillWidth: true; label: "Lock Layout"; checked: Boolean(root.graph.workspace && root.graph.workspace.layoutLocked); onToggled: root.toggleLayoutLocked() }
            DeckToggle { Layout.fillWidth: true; label: "Reduced Motion"; checked: root.reducedMotion; onToggled: root.reducedMotion = checked }
            Rectangle {
                objectName: "signalFlowBuildProvenance"
                Layout.fillWidth: true
                implicitHeight: buildProvenanceDetails.implicitHeight + deck.space12
                radius: deck.radiusControl
                color: deck.secondarySurface
                border.width: 1
                border.color: deck.border
                Accessible.name: "Signal Flow Graphical Editor build provenance"
                ColumnLayout {
                    id: buildProvenanceDetails
                    anchors.fill: parent
                    anchors.margins: deck.space6
                    spacing: 2
                    Text { text: "TECHNICAL DETAILS"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: "Signal Flow Graphical Editor"
                        color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "HOTAS BF6 build · " + String(root.signalFlowBuildProvenance.applicationVersion || "unknown")
                            + " · " + String(root.signalFlowBuildProvenance.buildId || "unknown")
                        color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: String(root.signalFlowBuildProvenance.sourceBranch || "unknown")
                            + " · " + String(root.signalFlowBuildProvenance.buildType || "unknown")
                        color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // Notices float above the graph: feedback must never push the topology
    // down or turn an ordinary action into a permanent status strip.
    Popup {
        id: feedbackToast
        parent: Overlay.overlay
        visible: root.notice.length > 0
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape
        width: Math.min(460, Math.max(250, root.width - deck.space32))
        x: Math.max(deck.space16, root.width - width - deck.space16)
        y: deck.space16 + deck.compactControlHeight + deck.space8
        padding: deck.space10
        background: Rectangle {
            radius: deck.radiusControl
            color: root.noticeError ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.94)
                : Qt.rgba(deck.elevatedSurface.r, deck.elevatedSurface.g, deck.elevatedSurface.b, 0.97)
            border.width: 1
            border.color: root.noticeError ? deck.fault : deck.border
        }
        contentItem: Text {
            width: feedbackToast.availableWidth
            text: root.notice
            color: root.noticeError ? deck.applicationBackground : deck.textPrimary
            font.family: deck.bodyFont
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }

    // At narrower widths the inspector overlays the graph instead of taking a
    // permanent third column.  Its open/close state does not touch workspace
    // coordinates or topology.
    Popup {
        id: compactInspector
        objectName: "signalFlowInspectorPanel"
        parent: Overlay.overlay
        // The Inspector is independently summonable from the workspace menu.
        // Selection enriches it; it is never a prerequisite for opening it.
        visible: root.inspectorOpen
        modal: false
        focus: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(390, root.width - 32)
        // The Inspector is information-dense by default, but it sizes to the
        // selected object's real content instead of reserving an empty pane.
        height: Math.min(560, Math.min(root.height - 32,
            Math.max(174, inspectorHeader.implicitHeight + deck.space8
                + Math.min(inspectorColumn.implicitHeight, 460) + padding * 2)))
        x: Math.max(deck.space16, Math.min(root.width - width - deck.space16, root.inspectorPositionX))
        y: Math.max(deck.space16, Math.min(root.height - height - deck.space16, root.inspectorPositionY))
        padding: deck.cardPaddingTechnical
        background: Rectangle { radius: deck.radiusPanel; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        onClosed: {
            if (root.inspectorOpen) root.inspectorOpen = false
        }
        contentItem: Item {
            id: inspectorSurface
            MouseArea {
                // This sits behind real controls and the scroll surface, so
                // broad empty card space is draggable without stealing text,
                // buttons, links, or scrolling from interactive children.
                anchors.fill: parent
                z: 0
                cursorShape: Qt.SizeAllCursor
                property real startX: 0
                property real startY: 0
                onPressed: function(mouse) { startX = mouse.x; startY = mouse.y }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    root.inspectorPositionX = Math.max(deck.space16, Math.min(root.width - compactInspector.width - deck.space16,
                        root.inspectorPositionX + mouse.x - startX))
                    root.inspectorPositionY = Math.max(deck.space16, Math.min(root.height - compactInspector.height - deck.space16,
                        root.inspectorPositionY + mouse.y - startY))
                }
                onReleased: root.persistInspectorPosition()
            }
            ColumnLayout {
            id: inspectorShell
            anchors.fill: parent
            z: 1
            spacing: deck.space8
            RowLayout {
                id: inspectorHeader
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 22
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "INSPECTOR"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                        property real startX: 0; property real startY: 0
                        onPressed: function(mouse) { startX = mouse.x; startY = mouse.y }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            root.inspectorPositionX = Math.max(deck.space16, Math.min(root.width - compactInspector.width - deck.space16,
                                root.inspectorPositionX + mouse.x - startX))
                            root.inspectorPositionY = Math.max(deck.space16, Math.min(root.height - compactInspector.height - deck.space16,
                                root.inspectorPositionY + mouse.y - startY))
                        }
                        onReleased: root.persistInspectorPosition()
                    }
                }
                FloatingPanelCloseButton { Accessible.name: "Close inspector"; onClicked: root.closeInspector() }
            }
            ScrollView {
                id: inspectorScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(inspectorColumn.implicitHeight, 460)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: inspectorColumn
                    width: inspectorScroll.availableWidth
                    spacing: deck.space8
            Text { Layout.fillWidth: true; text: root.inspectedRoute && root.inspectedRoute.id ? root.inspectedRoute.sourceLabel + " → " + root.inspectedRoute.destinationLabel : root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId) ? root.inspectedPort.label || root.inspectedPort.technicalLabel || "Signal Flow port" : root.inspectedAnnotation && root.inspectedAnnotation.id ? root.inspectedAnnotation.title || "Workspace annotation" : root.inspectedNode && root.inspectedNode.id ? root.inspectedNode.label || "Signal Flow object" : "Signal Flow Inspector"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; wrapMode: Text.WordWrap }
            Text { visible: !Boolean(root.inspectedRoute && root.inspectedRoute.id) && !Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)) && !Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id) && !Boolean(root.inspectedNode && root.inspectedNode.id); Layout.fillWidth: true; text: "Select a port, wire, block, processor, or workspace annotation to inspect its canonical Signal Flow details. The graph remains fully interactive while this surface is open."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id); Layout.fillWidth: true; text: String(root.inspectedAnnotation.kind || "note").toUpperCase() + " · PRESENTATION ONLY\nThis object has no ports, topology, processor state, or runtime meaning."; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)); text: "IDENTITY"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
            Text { visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)); Layout.fillWidth: true; text: "Alias · " + String(root.inspectedPort.label || root.inspectedPort.technicalLabel || "Unnamed port") + "\nRaw ID · " + String(root.inspectedPort.endpointId || root.inspectedPort.id || "") + "\nOwner · " + String(root.inspectedPortOwner.label || "Signal Flow") + "\n" + String(root.inspectedPort.kind || "other").toUpperCase() + " · " + (root.portIsOutput(root.inspectedPort) ? "OUTPUT" : "INPUT"); color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
            Text { visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)); text: "STATE & TOPOLOGY"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
            Text { visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)); Layout.fillWidth: true; text: root.portTopologySummary(root.inspectedPort) + " · " + (root.inspectedPort.available ? "AVAILABLE / CONFIGURATION VALID" : "OFFLINE / SAVED CONFIGURATION VALID") + "\n" + (root.portIsOutput(root.inspectedPort) ? "Sources feeding this output:" : "Destinations driven by this input:"); color: root.inspectedPort.available ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            Repeater {
                visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId))
                model: root.inspectedPort ? root.routesForPort(root.inspectedPort, root.portIsOutput(root.inspectedPort)) : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: routePortDetail.implicitHeight + deck.space8
                    radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border; border.width: 1
                    Column {
                        id: routePortDetail
                        anchors.fill: parent; anchors.margins: deck.space4; spacing: 2
                        Text { width: parent.width; text: String(modelData.sourceLabel || "Source") + " → " + String(modelData.destinationLabel || "Destination"); color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 9; elide: Text.ElideRight }
                        Text { width: parent.width; text: (modelData.processors && modelData.processors.length ? "Processing · " + modelData.processors.join(" → ") : "Direct canonical route") + " · " + String(modelData.health || "ready").toUpperCase(); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight }
                    }
                }
            }
            Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "SOURCE · PROCESSING · DESTINATION"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
            Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; text: "SOURCE\n" + String(root.inspectedRoute.sourceLabel || "Unknown source") + "\n\nPROCESSING\n" + (root.inspectedRoute.processors && root.inspectedRoute.processors.length ? root.inspectedRoute.processors.join(" → ") : "Direct signal — no canonical processor") + "\n\nDESTINATION\n" + String(root.inspectedRoute.destinationLabel || "Unknown destination"); color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; text: String(root.inspectedRoute.sourceLabel || "The source") + " controls " + String(root.inspectedRoute.destinationLabel || "the destination") + "." + (root.inspectedRoute.processors && root.inspectedRoute.processors.length ? " The signal passes through " + root.inspectedRoute.processors.join(", ") + " before reaching the virtual output." : " The signal is routed directly."); color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; text: "STATUS · " + String(root.inspectedRoute.health || "ready").toUpperCase().replace("-", " ") + (root.inspectedRoute.healthDetail ? "\n" + String(root.inspectedRoute.healthDetail) : ""); color: root.inspectedRoute.health === "ready" ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); text: root.inspectedNode.kind === "processor" ? "PROCESSOR" : "DEVICE / OUTPUT"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
            Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); Layout.fillWidth: true; text: "Identity · " + String(root.inspectedNode.label || "Signal Flow block") + "\nState · " + (root.inspectedNode.connected ? "CONNECTED" : "SAVED / OFFLINE") + "\nPorts · " + Number((root.inspectedNode.ports || []).length) + " · Routes · " + Number(root.inspectedNode.routeCount || 0) + "\n" + String(root.inspectedNode.detail || ""); color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor"); Layout.fillWidth: true; text: "Type · " + String(root.inspectedNode.semantic || root.inspectedNode.label || "processor") + "\nChannels · " + Number(root.inspectedNode.channelCount || 1) + (root.inspectedNode.shared ? " · SHARED " + Number(root.inspectedNode.sharedChannelCount || 0) + " routes" : "") + "\nEndpoints · " + (root.inspectedNode.ports || []).map(function(port) { return String(port.label || port.id || "port") }).join(" · "); color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; wrapMode: Text.WrapAnywhere }
            DeckToggle { id: inspectorTechnicalDetails; Layout.fillWidth: true; visible: Boolean((root.inspectedRoute && root.inspectedRoute.id) || (root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId)) || (root.inspectedNode && root.inspectedNode.id)); label: "Technical Details"; checked: false }
            Text { visible: inspectorTechnicalDetails.visible && inspectorTechnicalDetails.checked; Layout.fillWidth: true; text: root.inspectedRoute && root.inspectedRoute.id ? "routeId · " + String(root.inspectedRoute.id || "") + "\nsegments · " + (root.inspectedRoute.segments || []).map(function(segment) { return String(segment.id || "") }).join(", ") + "\nrevision · " + Number(root.graph.revision || 0) + "\nprofile / rig · " + String(root.graph.profileId || "") + " / " + String(root.graph.deviceRigId || "") : root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId) ? "endpointId · " + String(root.inspectedPort.endpointId || root.inspectedPort.id || "") + "\nownerId · " + String(root.inspectedPortOwner.objectId || root.inspectedPortOwner.id || "") + "\nrevision · " + Number(root.graph.revision || 0) : "objectId · " + String(root.inspectedNode.objectId || root.inspectedNode.id || "") + "\nrevision · " + Number(root.graph.revision || 0); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; wrapMode: Text.WrapAnywhere }
            DeckButton { visible: Boolean(root.inspectedNode && (root.inspectedNode.kind === "input" || root.inspectedNode.kind === "output")); text: "Open Devices & Setup"; Layout.fillWidth: true; onClicked: root.openCardSettings(root.inspectedNode) }
            DeckButton { visible: Boolean(root.inspectedNode && (root.inspectedNode.kind === "input" || root.inspectedNode.kind === "output")); text: "Expand all sections"; Layout.fillWidth: true; onClicked: root.setAllGroups(root.inspectedNode, false) }
            DeckButton { visible: Boolean(root.inspectedNode && (root.inspectedNode.kind === "input" || root.inspectedNode.kind === "output")); text: "Collapse all sections"; Layout.fillWidth: true; onClicked: root.setAllGroups(root.inspectedNode, true) }
            DeckButton { visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor" && (root.inspectedNode.semantic === "curve" || root.inspectedNode.semantic === "adaptive-response")); text: root.inspectedNode.semantic === "curve" ? "Open Curve Editor" : "Open Adaptive Response"; Layout.fillWidth: true; onClicked: root.openNodeSettings(root.inspectedNode) }
            DeckButton { visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor" && root.inspectedNode.semantic === "mixer"); text: "Configure Mixer"; Layout.fillWidth: true; onClicked: root.removeSelectedProcessor() }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Insert Processor"; enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.openBlockLibrary() }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Open Source"; Layout.fillWidth: true; onClicked: root.selectNode(root.nodeForId(String(root.inspectedRoute.sourceNodeId || ""))) }
            DeckButton {
                visible: Boolean(root.inspectedRoute && root.inspectedRoute.id)
                text: "Open Destination"
                Layout.fillWidth: true
                onClicked: {
                    const destination = root.nodeForId(String(root.inspectedRoute.destinationNodeId || ""))
                    if (destination) root.selectNode(destination)
                }
            }
            DeckButton { visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id && root.inspectedAnnotation.kind === "group"); text: root.inspectedAnnotation.moveContents ? "Move contained annotations: on" : "Move contained annotations: off"; Layout.fillWidth: true; onClicked: root.updateAnnotation(root.inspectedAnnotation.id, { "moveContents": !root.inspectedAnnotation.moveContents }) }
            DeckButton { visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id); text: "Detach annotation"; enabled: Boolean(root.inspectedAnnotation.attachedObjectId || root.inspectedAnnotation.attachedRouteId); Layout.fillWidth: true; onClicked: root.updateAnnotation(root.inspectedAnnotation.id, { "attachedObjectId": "", "attachedRouteId": "" }) }
            DeckButton { visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id); text: "Delete annotation"; destructive: true; Layout.fillWidth: true; onClicked: root.removeAnnotation(root.inspectedAnnotation.id) }
            DeckButton { visible: Boolean(root.inspectedPort && (root.inspectedPort.id || root.inspectedPort.endpointId) && !root.portIsOutput(root.inspectedPort)); text: "Start connection"; enabled: Boolean(root.mode === "configured" && root.inspectedPort && root.inspectedPort.available); Layout.fillWidth: true; onClicked: root.armSource(root.inspectedPort, false) }
            DeckButton { visible: Boolean(root.inspectedPort && root.routesForPort(root.inspectedPort, root.portIsOutput(root.inspectedPort)).length > 0); text: "Inspect route"; Layout.fillWidth: true; onClicked: { root.inspectedRoute = root.routesForPort(root.inspectedPort, root.portIsOutput(root.inspectedPort))[0]; root.selectedSegmentId = "" } }
            DeckButton { visible: Boolean(root.inspectedPort && root.inspectedPortOwner && root.inspectedPortOwner.id); text: root.portIsOutput(root.inspectedPort) ? "Open output setup" : "Open full settings"; Layout.fillWidth: true; onClicked: { if (root.portIsOutput(root.inspectedPort)) root.openCardSettings(root.inspectedPortOwner); else root.openFullSettings("axis", root.routesForPort(root.inspectedPort, false)[0] || ({})) } }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Explain route"; Layout.fillWidth: true; onClicked: root.explainRoute() }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Disconnect"; destructive: true; enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
        }
        }
        }
    }
    }

    Timer {
        id: saveTimer
        // A named object lets the native qualification fixture stop only its
        // own deferred save while it temporarily supplies a synthetic graph.
        // Product interactions still use this same debounce unchanged.
        objectName: "signalFlowWorkspaceSaveTimer"
        interval: 650
        repeat: false
        onTriggered: root.saveWorkspace()
    }

    Menu {
        id: deckProfileContextMenu
        objectName: "signalFlowProfileContextMenu"
        title: "Choose editing profile"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        Repeater {
            model: backendObject ? backendObject.profiles : []
            delegate: MenuItem {
                required property var modelData
                text: (modelData.displayName || modelData.name || "Profile") + (modelData.effective ? " · effective" : "")
                checkable: true
                checked: String(modelData.id || "") === String(root.graph.profileId || "")
                enabled: modelData.enabled !== false
                onTriggered: root.selectProfileContext(modelData)
            }
        }
    }

    Menu {
        id: deckRigContextMenu
        objectName: "signalFlowRigContextMenu"
        title: "Choose Device Rig context"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        Repeater {
            model: backendObject ? backendObject.deviceRigs : []
            delegate: MenuItem {
                required property var modelData
                text: modelData.name || "Device Rig"
                checkable: true
                checked: String(modelData.id || "") === String(root.graph.deviceRigId || "")
                enabled: modelData.enabled !== false
                onTriggered: root.selectRigContext(modelData)
            }
        }
    }

    Menu {
        id: deckStateFilterMenu
        title: "Route state"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "All routes"; checkable: true; checked: root.routeStateFilter === "all"; onTriggered: root.routeStateFilter = "all" }
        MenuItem { text: "Mapped"; checkable: true; checked: root.routeStateFilter === "mapped"; onTriggered: root.routeStateFilter = "mapped" }
        MenuItem { text: "Unmapped"; checkable: true; checked: root.routeStateFilter === "unmapped"; onTriggered: root.routeStateFilter = "unmapped" }
        MenuItem { text: "Problems"; checkable: true; checked: root.routeStateFilter === "problems"; onTriggered: root.routeStateFilter = "problems" }
        MenuItem { text: "Active now"; checkable: true; checked: root.routeStateFilter === "active"; onTriggered: root.routeStateFilter = "active" }
    }

    Popup {
        id: deckPortVisibilityMenu
        objectName: "signalFlowPortVisibilityMenu"
        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: 218
        padding: deck.space8
        function openFor(control) {
            if (!control) return
            const point = control.mapToItem(Overlay.overlay, 0, control.height + deck.space6)
            x = Math.max(deck.space8, Math.min(Overlay.overlay.width - width - deck.space8, point.x))
            y = Math.max(deck.space8, Math.min(Overlay.overlay.height - height - deck.space8, point.y))
            open()
        }
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.focus; border.width: 1 }
        contentItem: Column {
            width: parent.width
            spacing: deck.space4
            Text { width: parent.width; text: "PORT VISIBILITY"; color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; bottomPadding: deck.space4 }
            Repeater {
                model: [
                    { "id": "smart", "label": "Smart", "detail": "Connected ports plus concise summaries" },
                    { "id": "connected", "label": "Connected Only", "detail": "Hide unconnected port rows" },
                    { "id": "compact", "label": "Compact", "detail": "Collapse all port groups" },
                    { "id": "expanded", "label": "Expanded", "detail": "Show every available port" }
                ]
                delegate: DeckButton {
                    required property var modelData
                    width: parent.width
                    height: 38
                    emphasized: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === modelData.id
                    text: (emphasized ? "✓  " : "    ") + modelData.label
                    helpText: modelData.detail
                    onClicked: {
                        root.setPortVisibility(modelData.id)
                        deckPortVisibilityMenu.close()
                    }
                }
            }
        }
    }

    Menu {
        id: deckMoreMenu
        title: "Signal Flow tools"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Fit graph"; onTriggered: root.fitGraph() }
        MenuItem { text: "Open Block Library"; onTriggered: root.openBlockLibrary() }
        MenuItem { text: "Show Inspector"; onTriggered: { root.inspectorOpen = true; root.ensureInspectorPosition() } }
        MenuItem { text: "Auto-layout"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onTriggered: root.applyAutoLayout() }
        MenuItem { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock layout" : "Lock layout"; onTriggered: root.toggleLayoutLocked() }
        MenuItem { text: "Snap to Grid"; checkable: true; checked: root.snapToGridEnabled; onTriggered: root.toggleSnapToGrid() }
        MenuSeparator {}
        MenuItem { text: root.signalFocus ? "Signal focus: on" : "Signal focus"; checkable: true; checked: root.signalFocus; onTriggered: { root.signalFocus = !root.signalFocus; if (root.signalFocus) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
        MenuItem { text: "X-ray related paths"; checkable: true; checked: root.xrayMode; onTriggered: root.keyboardAction("xray") }
        MenuItem { text: "Density: " + ((root.graph.workspace && root.graph.workspace.densityMode) || "detailed"); onTriggered: root.cycleDensityMode() }
        MenuItem { text: "Wire style: " + ((root.graph.workspace && root.graph.workspace.wireStyle) || "smooth"); onTriggered: root.toggleWireStyle() }
        MenuItem { text: root.reducedMotion ? "Reduced motion: on" : "Reduced motion: off"; onTriggered: root.reducedMotion = !root.reducedMotion }
        MenuSeparator {}
        MenuItem { text: "Learn input"; enabled: root.mode === "configured" && root.graph.editable; onTriggered: root.startSignalFlowSourceLearning() }
        MenuItem { text: "Learn destination"; enabled: root.mode === "configured" && root.graph.editable; onTriggered: learnDialog.open() }
        MenuItem { text: "Default connections"; enabled: root.mode === "configured" && root.graph.editable; onTriggered: root.preview("unassigned") }
        MenuItem { text: "Replace all…"; enabled: root.mode === "configured" && root.graph.editable; onTriggered: root.preview("replace-all") }
        MenuItem { text: "Manage aliases"; enabled: root.mode === "configured" && root.graph.editable; onTriggered: aliasDialog.open() }
    }

    Menu {
        id: deckCanvasContextMenu
        objectName: "flightDeckSignalFlowCanvasContextMenu"
        property real canvasX: 160
        property real canvasY: 160
        title: "Signal Flow workspace"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Show Inspector"; onTriggered: { root.inspectorOpen = true; root.ensureInspectorPosition() } }
        MenuItem { text: "Open Block Library"; onTriggered: root.openBlockLibrary() }
        MenuItem { text: "Graph Settings"; onTriggered: graphSettings.open() }
        MenuSeparator {}
        Menu {
            title: "Add"
            MenuItem {
                text: "Physical input…"
                onTriggered: root.openCardSettings(root.node("input"))
            }
            MenuItem {
                text: "Virtual output…"
                onTriggered: root.openCardSettings(root.node("output"))
            }
            MenuItem {
                text: "Processor on selected wire…"
                enabled: Boolean(root.inspectedRoute && root.inspectedRoute.id)
                onTriggered: root.openBlockLibrary()
            }
            MenuSeparator {}
            MenuItem { text: "Text Note"; onTriggered: root.addAnnotation("note", deckCanvasContextMenu.canvasX, deckCanvasContextMenu.canvasY) }
            MenuItem { text: "Group / Section"; onTriggered: root.addAnnotation("group", deckCanvasContextMenu.canvasX, deckCanvasContextMenu.canvasY) }
        }
        MenuSeparator {}
        MenuItem { text: "Port visibility: Smart"; checkable: true; checked: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === "smart"; onTriggered: root.setPortVisibility("smart") }
        MenuItem { text: "Port visibility: Connected Only"; checkable: true; checked: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === "connected"; onTriggered: root.setPortVisibility("connected") }
        MenuItem { text: "Port visibility: Compact"; checkable: true; checked: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === "compact"; onTriggered: root.setPortVisibility("compact") }
        MenuItem { text: "Port visibility: Expanded"; checkable: true; checked: String(root.graph.workspace && root.graph.workspace.portVisibility || "smart") === "expanded"; onTriggered: root.setPortVisibility("expanded") }
        MenuItem { text: "Wire style: " + ((root.graph.workspace && root.graph.workspace.wireStyle) || "smooth"); onTriggered: root.toggleWireStyle() }
        MenuItem { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock layout" : "Lock layout"; onTriggered: root.toggleLayoutLocked() }
        MenuItem { text: "Fit all"; onTriggered: root.fitGraph() }
        MenuItem { text: "Auto layout"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onTriggered: root.applyAutoLayout() }
        MenuItem { text: "Undo"; enabled: backendObject.signalFlowCanUndo; onTriggered: root.keyboardAction("undo") }
        MenuItem { text: "Redo"; enabled: backendObject.signalFlowCanRedo; onTriggered: root.keyboardAction("redo") }
    }

    Menu {
        id: deckNodeContextMenu
        objectName: "flightDeckSignalFlowNodeContextMenu"
        property var targetNode: ({})
        title: targetNode && targetNode.label ? targetNode.label : "Signal Flow node"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Inspect card"; enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.id); onTriggered: root.selectNode(deckNodeContextMenu.targetNode) }
        MenuItem {
            text: "Expand all port sections"
            enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.id)
            onTriggered: root.setAllGroups(deckNodeContextMenu.targetNode, false)
        }
        MenuItem {
            text: "Collapse all port sections"
            enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.id)
            onTriggered: root.setAllGroups(deckNodeContextMenu.targetNode, true)
        }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.semantic === "curve"
                ? "Open Curve Editor" : "Open Adaptive Response"
            visible: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "processor"
                && (deckNodeContextMenu.targetNode.semantic === "curve"
                    || deckNodeContextMenu.targetNode.semantic === "adaptive-response"))
            onTriggered: root.openNodeSettings(deckNodeContextMenu.targetNode)
        }
        MenuItem {
            text: "Open processor configuration"
            visible: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "processor"
                && deckNodeContextMenu.targetNode.semantic !== "curve"
                && deckNodeContextMenu.targetNode.semantic !== "adaptive-response")
            onTriggered: {
                root.selectNode(deckNodeContextMenu.targetNode)
                root.openNodeSettings(deckNodeContextMenu.targetNode)
            }
        }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "input"
                ? "Open input setup" : "Open output setup"
            visible: Boolean(deckNodeContextMenu.targetNode && (deckNodeContextMenu.targetNode.kind === "input"
                || deckNodeContextMenu.targetNode.kind === "output"))
            onTriggered: root.openCardSettings(deckNodeContextMenu.targetNode)
        }
        MenuItem {
            text: "Remove processor"
            visible: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "processor")
            enabled: root.mode === "configured"
            onTriggered: {
                root.selectNode(deckNodeContextMenu.targetNode)
                root.removeSelectedProcessor()
            }
        }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.pinned ? "Unpin placement" : "Pin placement"
            enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.objectId)
            onTriggered: root.toggleNodePinned(deckNodeContextMenu.targetNode)
        }
        MenuItem {
            text: "Attach selected annotation"
            visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id)
            enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.id)
            onTriggered: root.updateAnnotation(root.inspectedAnnotation.id, {
                "attachedObjectId": String(deckNodeContextMenu.targetNode.objectId || deckNodeContextMenu.targetNode.id),
                "attachedRouteId": "", "x": 0, "y": -36
            })
        }
        MenuItem { text: "Center selection"; enabled: Boolean(root.inspectedRoute && root.inspectedRoute.id); onTriggered: root.focusCurrentSelection() }
        MenuSeparator {}
        MenuItem { text: "Auto-layout unpinned cards"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onTriggered: root.applyAutoLayout() }
    }

    Menu {
        id: deckPortContextMenu
        objectName: "flightDeckSignalFlowPortContextMenu"
        property var targetPort: ({})
        property var targetOwner: ({})
        readonly property bool targetIsOutput: root.portIsOutput(targetPort)
        readonly property var targetRoutes: root.routesForPort(targetPort, targetIsOutput)
        title: targetPort && targetPort.label ? targetPort.label : "Signal Flow port"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Inspect port"; enabled: Boolean(deckPortContextMenu.targetPort && (deckPortContextMenu.targetPort.id || deckPortContextMenu.targetPort.endpointId)); onTriggered: root.inspectPort(deckPortContextMenu.targetPort, deckPortContextMenu.targetOwner) }
        MenuItem {
            text: "Start connection"
            visible: !deckPortContextMenu.targetIsOutput
            enabled: root.mode === "configured" && Boolean(deckPortContextMenu.targetPort && deckPortContextMenu.targetPort.available)
            onTriggered: root.armSource(deckPortContextMenu.targetPort, false)
        }
        MenuItem {
            text: deckPortContextMenu.targetRoutes.length === 1 ? "Inspect route" : "Inspect first route"
            visible: deckPortContextMenu.targetRoutes.length > 0
            onTriggered: {
                root.inspectedRoute = deckPortContextMenu.targetRoutes[0]
                root.selectedSegmentId = ""
            }
        }
        MenuItem {
            text: deckPortContextMenu.targetIsOutput ? "Open output setup" : "Open axis settings"
            enabled: Boolean(deckPortContextMenu.targetOwner && deckPortContextMenu.targetOwner.id)
            onTriggered: {
                if (deckPortContextMenu.targetIsOutput) root.openCardSettings(deckPortContextMenu.targetOwner)
                else root.openFullSettings("axis", deckPortContextMenu.targetRoutes.length > 0
                    ? deckPortContextMenu.targetRoutes[0] : ({}))
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "Disconnect route"
            visible: deckPortContextMenu.targetRoutes.length > 0
            enabled: root.mode === "configured"
            onTriggered: {
                root.inspectedRoute = deckPortContextMenu.targetRoutes[0]
                root.disconnectSelected()
            }
        }
    }

    Menu {
        id: deckRouteContextMenu
        objectName: "flightDeckSignalFlowRouteContextMenu"
        property var targetRoute: ({})
        title: targetRoute && targetRoute.sourceLabel
            ? targetRoute.sourceLabel + " → " + targetRoute.destinationLabel : "Signal Flow route"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem {
            text: "Inspect route"
            enabled: Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.id)
            onTriggered: {
                root.inspectedRoute = deckRouteContextMenu.targetRoute
                root.inspectedNode = ({})
                root.inspectedPort = ({})
                root.inspectorOpen = true
                root.ensureInspectorPosition()
            }
        }
        MenuItem {
            text: "Insert processing from Block Library…"
            enabled: root.mode === "configured" && Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.id)
            onTriggered: {
                root.inspectedRoute = deckRouteContextMenu.targetRoute
                root.source = ({})
                root.inspectedNode = ({})
                root.openBlockLibrary()
            }
        }
        MenuItem {
            text: "Open source"
            enabled: Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.sourceNodeId)
            onTriggered: root.selectNode(root.nodeForId(deckRouteContextMenu.targetRoute.sourceNodeId))
        }
        MenuItem {
            text: "Open destination"
            enabled: Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.destinationNodeId)
            onTriggered: root.selectNode(root.nodeForId(deckRouteContextMenu.targetRoute.destinationNodeId))
        }
        MenuItem {
            text: "Explain route"
            enabled: Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.id)
            onTriggered: {
                root.inspectedRoute = deckRouteContextMenu.targetRoute
                root.source = ({})
                root.inspectedNode = ({})
                root.explainRoute()
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "Disconnect route"
            enabled: root.mode === "configured" && Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.id)
            onTriggered: {
                root.inspectedRoute = deckRouteContextMenu.targetRoute
                root.source = ({})
                root.disconnectSelected()
            }
        }
        MenuItem {
            text: "Attach selected annotation"
            visible: Boolean(root.inspectedAnnotation && root.inspectedAnnotation.id)
            onTriggered: root.updateAnnotation(root.inspectedAnnotation.id, {
                "attachedObjectId": "", "attachedRouteId": String(deckRouteContextMenu.targetRoute.id || ""),
                "x": 18, "y": -34
            })
        }
    }

    Dialog {
        id: conflictDialog
        objectName: "flightDeckSignalFlowConflictDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, root.width - 40)
        property string destination: ""
        property string destinationPortId: ""
        property string label: ""
        property bool mixerAllowed: true
        title: mixerAllowed ? "Resolve analog input collision" : "Resolve virtual POV collision"
        contentItem: ColumnLayout {
            spacing: deck.space16
            Text { Layout.fillWidth: true; text: "“" + (root.source.label || "Selected source") + "” conflicts with the current route into “" + conflictDialog.label + "”. " + (conflictDialog.mixerAllowed ? "Replace moves that input; Mixer preserves both inputs with an explicit runtime mode." : "Virtual POV streams cannot be merged; replace the current source or choose another destination."); color: deck.textPrimary; font.family: deck.bodyFont; wrapMode: Text.WordWrap }
            ComboBox {
                id: deckMixerMode
                Layout.fillWidth: true
                visible: conflictDialog.mixerAllowed
                model: ["average", "sum-clamped", "highest-magnitude"]
                font.family: deck.bodyFont
                font.pixelSize: 10
                Accessible.name: "Analog mixer mode"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: conflictDialog.close() }
                DeckButton { visible: conflictDialog.mixerAllowed; text: "Create mixer"; onClicked: { const mode = deckMixerMode.currentText; conflictDialog.close(); root.connectWithMixer(mode) } }
                DeckButton {
                    text: "Replace"
                    emphasized: true
                    onClicked: {
                        conflictDialog.close()
                        const port = root.destinationPortById(conflictDialog.destinationPortId)
                        if (port) root.commitConnection(port, "replace")
                        else {
                            root.notice = "The collision target changed. Review the current graph and retry."
                            root.noticeError = true
                            root.cancelRouting("", false)
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: explainDialog
        objectName: "flightDeckSignalFlowExplainDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, root.width - 40)
        title: "Explain route"
        contentItem: ColumnLayout {
            spacing: deck.space8
            Text { Layout.fillWidth: true; text: root.routeExplanation.summary || "Select a current route to explain it."; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; text: root.routeExplanation.runtimeDetail || ""; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, explainSteps.implicitHeight); clip: true
                Column {
                    id: explainSteps; width: parent.availableWidth; spacing: 5
                    Repeater {
                        model: root.routeExplanation.steps || []
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width; implicitHeight: stepText.implicitHeight + deck.space12
                            radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border
                            Column { id: stepText; anchors.fill: parent; anchors.margins: deck.space6; spacing: 2
                                Text { width: parent.width; text: modelData.label || "Signal step"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 10; font.bold: true }
                                Text { width: parent.width; text: modelData.detail || ""; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
            CheckBox { id: deckTechnical; text: "Technical details"; font.family: deck.bodyFont; font.pixelSize: 10 }
            TextArea { visible: deckTechnical.checked; Layout.fillWidth: true; Layout.preferredHeight: 100; readOnly: true; text: root.routeExplanation.technicalDetails || ""; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 9; background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border } }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Close"; onClicked: explainDialog.close() }
            }
        }
    }

    Dialog {
        id: processorDialog
        objectName: "flightDeckSignalFlowProcessorPalette"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(530, root.width - 40)
        title: "Processor palette"
        contentItem: ColumnLayout {
            spacing: deck.space8
            Text { Layout.fillWidth: true; text: "Drag a processor chip onto its matching highlighted execution-stage wire, or click it to apply the same atomic action to the selected canonical segment. Visual-only reorders are not offered; focused settings stay authoritative."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Flow {
                Layout.fillWidth: true; spacing: deck.space8
                Repeater {
                    model: [
                        { key: "curve", label: "Curve" }, { key: "deadzone", label: "Deadzone" },
                        { key: "center-hold", label: "Center hold" }, { key: "invert", label: "Invert" },
                        { key: "limits", label: "Output limits" }, { key: "adaptive-response", label: "Adaptive Response" }
                    ]
                    delegate: Rectangle {
                        id: deckProcessorChip
                        required property var modelData
                        property string processorKind: modelData.key
                        readonly property bool activeProcessor: root.processorEnabled(processorKind)
                        readonly property bool actionAvailable: root.processorActionAvailable(processorKind)
                        width: Math.max(118, chipLabel.implicitWidth + 20); height: deck.compactControlHeight; radius: deck.radiusControl
                        color: activeProcessor ? deck.accent : deck.secondarySurface
                        opacity: actionAvailable ? 1.0 : 0.54
                        border.color: chipDrag.active ? deck.attention : deck.border; border.width: 1
                        Text { id: chipLabel; anchors.centerIn: parent; text: root.processorIsShared(processorKind) ? "Split shared " + modelData.label : activeProcessor ? "Remove " + modelData.label : actionAvailable ? "Add " + modelData.label : "Select stage for " + modelData.label; color: activeProcessor ? deck.applicationBackground : deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 10; font.bold: true }
                        Drag.active: chipDrag.active
                        Drag.source: deckProcessorChip
                        Drag.keys: ["signal-flow-processor"]
                        Drag.hotSpot.x: width / 2; Drag.hotSpot.y: height / 2
                        DragHandler { id: chipDrag; enabled: root.mode === "configured" && !activeProcessor && root.processorCanInsert(processorKind) }
                        MouseArea { anchors.fill: parent; enabled: root.mode === "configured" && actionAvailable; onClicked: root.toggleProcessor(processorKind) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Share active processor…"; enabled: root.mode === "configured"; onClicked: root.openShareProcessorDialog() }
                DeckButton { text: "Close"; onClicked: processorDialog.close() }
            }
        }
    }

    Dialog {
        id: shareDialog
        objectName: "flightDeckSignalFlowShareProcessorDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(530, root.width - 40)
        property string processorKind: ""
        property var processorKinds: []
        property var choices: []
        title: "Share a processor"
        function resetChoices() { choices = root.shareableAxisRoutes(processorKind) }
        function prepare() {
            const supported = [
                { key: "curve", label: "Curve" }, { key: "deadzone", label: "Deadzone" },
                { key: "center-hold", label: "Center hold" }, { key: "invert", label: "Invert" },
                { key: "limits", label: "Output limits" }, { key: "adaptive-response", label: "Adaptive Response" }
            ]
            processorKinds = supported.filter(function(item) { return root.processorEnabled(item.key) })
            processorKind = processorKinds.length > 0 ? processorKinds[0].key : ""
            resetChoices()
        }
        function setChoice(choiceIndex, selected) {
            const next = choices.slice(0)
            const choice = next[choiceIndex]
            next[choiceIndex] = ({ "id": choice.id, "label": choice.label, "selected": selected, "axis": choice.axis })
            choices = next
        }
        function applyShare() {
            const routeIds = choices.filter(function(choice) { return choice.selected })
                .map(function(choice) { return String(choice.id) })
            const result = backendObject.signalFlowShareProcessor(routeIds, processorKind,
                Number(root.graph.revision || 0))
            root.announce(result, "Shared processor was not created.")
            if (result && result.success) close()
            return Boolean(result && result.success)
        }
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: "The first selected route owns the existing processor setting. Signal Flow mirrors that durable setting to every selected axis; split a route later to edit it independently."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            ComboBox {
                id: deckShareKind
                Layout.fillWidth: true
                model: shareDialog.processorKinds
                textRole: "label"
                font.family: deck.bodyFont
                font.pixelSize: 10
                Accessible.name: "Processor to share"
                onActivated: {
                    const item = shareDialog.processorKinds[currentIndex]
                    shareDialog.processorKind = item ? String(item.key) : ""
                    shareDialog.resetChoices()
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, deckShareChoices.implicitHeight)
                clip: true
                Column {
                    id: deckShareChoices
                    width: parent.availableWidth
                    spacing: deck.space4
                    Repeater {
                        model: shareDialog.choices
                        delegate: CheckBox {
                            required property var modelData
                            width: parent.width
                            checked: Boolean(modelData.selected)
                            text: modelData.label
                            onToggled: shareDialog.setChoice(index, checked)
                            contentItem: Text { text: parent.text; color: deck.textPrimary; leftPadding: parent.indicator.width + deck.space8; verticalAlignment: Text.AlignVCenter; font.family: deck.bodyFont; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                    }
                }
            }
            Text { Layout.fillWidth: true; text: "Select at least two distinct physical axis sources. Existing members remain selected when extending a shared object."; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: shareDialog.close() }
                DeckButton {
                    text: "Share processor"
                    emphasized: true
                    enabled: shareDialog.processorKind.length > 0 && shareDialog.choices.filter(function(choice) { return choice.selected }).length >= 2
                    onClicked: shareDialog.applyShare()
                }
            }
        }
    }

    Dialog {
        id: deckSourceLearnDialog
        objectName: "flightDeckSignalFlowLearnInputDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, root.width - 40)
        title: "Learn physical input"
        onClosed: {
            const learning = backendObject.inputLearning || ({})
            if (Boolean(learning.active) && String(learning.kind || "") === "signal-flow")
                backendObject.cancelInputLearning()
        }
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: "Move an axis, press a button, or move a POV direction. Flight Deck identifies the physical source first, then presents compatible destinations in the route map."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; text: backendObject.inputLearning.message || "Preparing safe source detection…"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Text { visible: String(backendObject.inputLearning.sourceLabel || "").length > 0; Layout.fillWidth: true; text: backendObject.inputLearning.sourceLabel || ""; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 11; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Retry"; visible: backendObject.inputLearning.phase === "ambiguous"; onClicked: backendObject.retryInputLearning() }
                DeckButton { text: "Cancel"; onClicked: { backendObject.cancelInputLearning(); deckSourceLearnDialog.close() } }
            }
        }
    }

    Dialog {
        id: learnDialog
        objectName: "flightDeckSignalFlowLearnDestinationDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(470, root.width - 40)
        property var choices: []
        title: "Learn destination"
        onOpened: { choices = root.destinationPorts().filter(function(port) { return port.kind === "axis" || port.kind === "button" }) }
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: "Choose the virtual destination, then move the physical control to use. Flight Deck keeps the resulting route highlighted in this workspace."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            ComboBox { id: deckLearnChoices; Layout.fillWidth: true; model: learnDialog.choices; textRole: "label"; font.family: deck.bodyFont; font.pixelSize: 10; Accessible.name: "Learn destination" }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: learnDialog.close() }
                DeckButton { text: "Start learning"; emphasized: true; enabled: learnDialog.choices.length > 0; onClicked: root.startLearning(learnDialog.choices[deckLearnChoices.currentIndex]) }
            }
        }
    }

    Dialog {
        id: aliasDialog
        objectName: "flightDeckSignalFlowAliasDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(440, root.width - 40)
        property var axes: []
        title: "Virtual axis alias"
        onOpened: { axes = root.ports("output").filter(function(port) { return port.kind === "axis" }); deckAliasText.text = axes.length > 0 && axes[0].label !== axes[0].technicalLabel ? axes[0].label : "" }
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: "An alias only changes the label in this profile. The vJoy axis and runtime route are unchanged."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            ComboBox { id: deckAliasAxis; Layout.fillWidth: true; model: aliasDialog.axes; textRole: "technicalLabel"; font.family: deck.bodyFont; font.pixelSize: 10; onCurrentIndexChanged: { const choice = aliasDialog.axes[currentIndex]; deckAliasText.text = choice && choice.label !== choice.technicalLabel ? choice.label : "" } }
            TextField { id: deckAliasText; Layout.fillWidth: true; placeholderText: "Optional alias"; color: deck.textPrimary; selectByMouse: true; font.family: deck.bodyFont; font.pixelSize: 10; background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border } }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: aliasDialog.close() }
                DeckButton { text: "Save alias"; emphasized: true; enabled: aliasDialog.axes.length > 0; onClicked: { const axis = aliasDialog.axes[deckAliasAxis.currentIndex]; backendObject.setVirtualAxisAlias(String(axis.technicalLabel), deckAliasText.text); root.notice = "Alias saved."; root.noticeError = false; aliasDialog.close() } }
            }
        }
    }

    Dialog {
        id: deckMixerDialog
        objectName: "flightDeckSignalFlowMixerDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, root.width - 40)
        property string mixerId: ""
        property string routeId: ""
        property string currentMode: "average"
        title: "Explicit analog mixer"
        onOpened: deckMixerModeEditor.currentIndex = Math.max(0, deckMixerModeEditor.model.indexOf(currentMode))
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: "This mixer owns the named input routes shown on the graph. Its mode changes all of those sources atomically."; color: deck.textPrimary; font.family: deck.bodyFont; wrapMode: Text.WordWrap }
            ComboBox { id: deckMixerModeEditor; Layout.fillWidth: true; model: ["average", "sum-clamped", "highest-magnitude"]; font.family: deck.bodyFont; font.pixelSize: 10; Accessible.name: "Explicit analog mixer mode" }
            Text { Layout.fillWidth: true; text: "Remove this input disconnects only the selected source. Remove mixer + inputs disconnects every named source; no direct route is silently retained."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                DeckButton { text: "Cancel"; onClicked: deckMixerDialog.close() }
                Item { Layout.fillWidth: true }
                DeckButton { text: "Remove this input"; destructive: true; enabled: deckMixerDialog.routeId.length > 0; onClicked: { const result = backendObject.signalFlowRemoveMixerInput(deckMixerDialog.mixerId, deckMixerDialog.routeId, Number(root.graph.revision || 0)); root.announce(result, "Mixer input was not removed."); if (result && result.success) deckMixerDialog.close() } }
                DeckButton { text: "Remove mixer + inputs"; destructive: true; onClicked: { const result = backendObject.signalFlowRemoveMixer(deckMixerDialog.mixerId, Number(root.graph.revision || 0)); root.announce(result, "Mixer was not removed."); if (result && result.success) deckMixerDialog.close() } }
                DeckButton { text: "Apply mode"; emphasized: true; onClicked: { const result = backendObject.signalFlowSetMixerMode(deckMixerDialog.mixerId, deckMixerModeEditor.currentText, Number(root.graph.revision || 0)); root.announce(result, "Mixer mode was not changed."); if (result && result.success) deckMixerDialog.close() } }
            }
        }
    }

    Dialog {
        id: defaultsDialog
        objectName: "flightDeckSignalFlowDefaultsDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, root.width - 40)
        property string mode: "unassigned"
        property var preview: ({})
        title: mode === "replace-all" ? "Replace all: preview" : "Default map: preview"
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text { Layout.fillWidth: true; text: defaultsDialog.preview.message || "Review pending route changes."; color: deck.textPrimary; font.family: deck.bodyFont; wrapMode: Text.WordWrap }
            Text {
                Layout.fillWidth: true
                readonly property var summary: defaultsDialog.preview.summary || ({})
                text: "Added " + Number(summary.added || 0)
                    + " · Kept " + Number(summary.kept || 0)
                    + " · Replaced " + Number(summary.replaced || 0)
                    + " · Mixer changes " + Number(summary.mergeChanges || 0)
                    + " · Ambiguous " + Number(summary.ambiguous || 0)
                    + " · Blocked " + Number(summary.blocked || 0)
                color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10; wrapMode: Text.WordWrap
            }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, defaultRows.implicitHeight); clip: true
                Column { id: defaultRows; width: parent.availableWidth; spacing: 4
                    Repeater { model: defaultsDialog.preview.changes || []; delegate: Text { required property var modelData; width: parent.width; text: String(modelData.category || "added").toUpperCase() + " · " + modelData.source + ": " + modelData.from + " → " + modelData.to + " · " + (modelData.pairing || "") + (modelData.offline ? " · OFFLINE / configuration valid, live signal unavailable" : "") + (modelData.reason ? " · " + modelData.reason : ""); color: modelData.category === "blocked" || modelData.category === "ambiguous" ? deck.attention : modelData.category === "added" || modelData.category === "replaced" ? deck.healthy : deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10; wrapMode: Text.WordWrap } }
                    Text { visible: (defaultsDialog.preview.count || 0) === 0; text: "No route would change."; color: deck.healthy; font.family: deck.bodyFont; font.pixelSize: 10 }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: defaultsDialog.close() }
                DeckButton {
                    text: defaultsDialog.mode === "replace-all" ? "Replace all" : "Apply defaults"
                    emphasized: true
                    enabled: Boolean(defaultsDialog.preview.success && defaultsDialog.preview.canApply)
                    onClicked: {
                        const result = backendObject.signalFlowApplyDefaults(defaultsDialog.mode, Number(defaultsDialog.preview.revision || root.graph.revision || 0))
                        root.announce(result, "Defaults were not applied.")
                        defaultsDialog.close()
                    }
                }
            }
        }
    }
}
