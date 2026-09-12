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
    // A transient return snapshot is owned by the Flight Deck shell while a
    // focused editor is open. It preserves visual context only; routing and
    // processor settings stay in the canonical backend.
    property var presentationState: ({})
    property bool presentationRestored: false
    property var graph: backendObject ? backendObject.signalFlowGraph : ({})
    property var source: ({})
    property var inspectedRoute: ({})
    // Selection remembers the canonical edge under the pointer, not a
    // graph-local midpoint. It is restored with the route after a focused
    // editor round trip whenever that edge still exists.
    property string selectedSegmentId: ""
    // Deck cards select into the same truthful inspector rather than acting
    // as decorative scenery around the routing controls.
    property var inspectedNode: ({})
    property string query: ""
    property string mode: "configured"
    property string filter: "all"
    property string routeStateFilter: "all"
    property string notice: ""
    property bool noticeError: false
    property real zoom: 1.0
    property bool workspaceRestored: false
    property var liveTelemetry: ({})
    property bool liveMode: false
    property bool signalFocus: false
    property bool reducedMotion: false
    property var routeExplanation: ({})
    property bool xrayMode: false
    property var wireGeometry: []
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
    property int geometryRebuildCount: 0
    property int liveDragGeometryUpdates: 0
    property int liveDragAffectedSegments: 0
    property int nodePlacementWriteCount: 0
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
    // Presentation-only continuity for source-first learning. The canonical
    // graph remains owned by AppBackend.
    property string learnedSourcePortId: ""
    property var pendingLearnDestination: ({})
    property var dragWire: ({ "active": false, "source": ({}), "x": 0, "y": 0 })
    // A DropArea is permitted to report its drop either before or after the
    // source DragHandler becomes inactive. Keep the initiating port through
    // that handoff so a real release over a destination never loses its
    // connection request to event ordering.
    property var lastSourceDragPort: ({})
    property bool sourceDragDropHandled: false
    property var connectionPreview: ({})
    property string pendingProcessorRouteId: ""
    property string pendingProcessorSegmentId: ""
    readonly property string semanticDensity: {
        const requested = graph.workspace && graph.workspace.densityMode || "compact"
        if (zoom <= 0.62) return "overview"
        if (zoom <= 0.82 && requested === "detailed") return "compact"
        return requested
    }
    readonly property int canvasPortLimit: semanticDensity === "overview" ? 12
        : semanticDensity === "compact" ? 18 : 48
    readonly property var sceneBounds: graphLayoutBounds()
    readonly property real sceneLogicalWidth: Math.max(1040, Number(sceneBounds.maxX || 0) + 64)
    readonly property real sceneLogicalHeight: Math.max(700, Number(sceneBounds.maxY || 0) + 64)

    signal navigateRequested(int page, int axis, var state)

    function normalized(value) { return String(value || "").toLowerCase() }
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
    function routeHasProcessor(route, semantic) {
        const details = route && route.processorDetails ? route.processorDetails : []
        return details.some(function(detail) { return String(detail.semantic || "") === semantic })
    }
    function capturePresentationState() {
        return ({
            "version": 1,
            "inspectedRouteId": String(inspectedRoute && inspectedRoute.id || ""),
            "selectedSegmentId": selectedSegmentId,
            "inspectedNodeId": String(inspectedNode && inspectedNode.id || ""),
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
        return cardGroupRouteCount(nodeData, group, destination) > 0
    }
    function cardGroupCollapsed(nodeData, group, destination) {
        return cardGroupStoredCollapsed(nodeData, group)
            && !cardGroupNeedsAttention(nodeData, group, destination)
    }
    function cardGroupPorts(nodeData, group, destination) {
        const storedCollapsed = cardGroupStoredCollapsed(nodeData, group)
        const attention = cardGroupNeedsAttention(nodeData, group, destination)
        let result = portsForNode(nodeData).filter(function(port) {
            return String(port.group || "") === String(group || "") && matching(port, destination)
        })
        // A collapsed group that is temporarily revealed by a live selection
        // shows the related endpoints, not a surprise bank of 32 controls.
        if (storedCollapsed && attention && normalized(query).length === 0 && filter === "all"
                && routeStateFilter === "all") {
            result = result.filter(function(port) {
                return Boolean(port.mapped) || (source && source.id && (!destination
                    ? String(port.endpointId || port.id) === String(source.endpointId || source.id)
                    : compatible(port))) || (inspectedRoute && routesForPort(port, destination).some(function(route) {
                        return String(route.id || "") === String(inspectedRoute.id || "") }))
            })
        }
        const limit = semanticDensity === "overview" ? 3 : semanticDensity === "compact" ? 10 : 14
        return result.slice(0, limit)
    }
    function groupCollapsed(kind, group) { return cardGroupCollapsed(node(kind), group, kind === "output") }
    function groupPorts(kind, group, limit) { return cardGroupPorts(node(kind), group, kind === "output").slice(0, limit || 48) }
    function setGroup(nodeOrKind, group, collapsed) {
        const card = groupCard(nodeOrKind)
        announce(backendObject.signalFlowSetPortGroupCollapsed(String(card.objectId || card.id || ""), group, collapsed),
            "Port group state was not saved.")
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
    function destinationPorts() { return ports("output").filter(function(port) { return matching(port, true) && !groupCollapsed("output", port.group) }) }
    function visibleCardPorts(nodeData, destination) {
        let result = []
        const state = cardGroups(nodeData)
        for (let index = 0; index < state.length; ++index) {
            const group = String(state[index].group || "")
            if (!cardGroupCollapsed(nodeData, group, destination))
                result = result.concat(cardGroupPorts(nodeData, group, destination))
        }
        return result
    }
    function graphCardWidth(nodeData) { return nodeData && nodeData.kind === "processor" ? 188 : 294 }
    function graphCardHeight(nodeData) {
        if (!nodeData) return 96
        if (nodeData.kind === "processor") return 92 + Math.max(0, Number(nodeData.sharedChannelCount || 0) - 1) * 20
        const destination = nodeData.kind === "output"
        const state = cardGroups(nodeData)
        let rows = 0
        let collapsed = 0
        for (let index = 0; index < state.length; ++index) {
            const group = String(state[index].group || "")
            if (cardGroupCollapsed(nodeData, group, destination)) ++collapsed
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
            const position = nodePosition(nodeData, nodeData.kind === "output" ? 1320 : nodeData.kind === "processor" ? 680 : 80, 120)
            maxX = Math.max(maxX, Number(position.x || 0) + graphCardWidth(nodeData))
            maxY = Math.max(maxY, Number(position.y || 0) + graphCardHeight(nodeData))
        }
        return ({ "maxX": maxX, "maxY": maxY })
    }
    function compatible(port) {
        if (!source || !source.kind || !port) return false
        if (source.kind === "axis") return port.kind === "axis"
        if (source.kind === "native-pov") return port.kind === "native-pov"
        return port.kind === "button"
    }
    // Routing is a card-level task as well as a port-level one.  The source
    // card stays unmistakable, and destination cards advertise whether they
    // are compatible candidates or the current hovered target.
    function routingNodeState(nodeData) {
        if (!nodeData || !source || !source.id) return ""
        const sourceOwnerId = portOwnerNodeId(source)
        if (sourceOwnerId && nodeMatchesId(nodeData, sourceOwnerId)) return "source"
        const previewOwnerId = String(connectionPreview && connectionPreview.ownerNodeId || "")
        if (previewOwnerId && nodeMatchesId(nodeData, previewOwnerId))
            return connectionPreview.compatible ? "target" : "blocked"
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
        return deck.border
    }
    function routingNodeBorderWidth(state) {
        return state === "source" || state === "target" ? 3 : state.length > 0 ? 2 : 1
    }
    function routingNodeSurface(baseColor, state) {
        if (!state) return baseColor
        const emphasis = state === "target" || state === "blocked" ? deck.attention
            : state === "source" ? deck.accent : deck.focus
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
        inspectedNode = nodeData
        inspectedRoute = ({})
        source = ({})
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
        inspectedRoute = ({})
        selectedSegmentId = ""
        inspectedNode = ({})
        source = ({})
    }
    function dismissEmptyGraphAt(x, y) {
        const hit = hitWire(x, y)
        if (hit && hit.route) return false
        clearGraphSelection()
        return true
    }
    function useInputScope(nodeData) {
        if (!nodeData || nodeData.kind !== "input" || !nodeData.controllerRecordId || !graph.deviceRigId) return false
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
        if (!backendObject.activateProfile(String(profile.id))) {
            notice = "The selected profile could not become the active editing context."
            noticeError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        source = ({}); inspectedRoute = ({}); inspectedNode = ({})
        notice = "Editing profile changed to " + (profile.displayName || profile.name || "the selected profile") + "."
        noticeError = false
        return true
    }
    function selectRigContext(rig) {
        if (!rig || !rig.id) return false
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
        if (!port || !port.id) return false
        selectSource(port)
        lastSourceDragPort = port
        sourceDragDropHandled = false
        dragWire = ({ "active": true, "source": port,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
        return true
    }
    function updateSourceDrag(point) {
        if (!dragWire.active) return
        dragWire = ({ "active": true, "source": dragWire.source,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
    }
    function sourceDragPortFromDrop(drop) {
        const reported = drop && drop.source && drop.source.port
        if (reported && reported.id) return reported
        const active = dragWire && dragWire.source
        if (active && active.id) return active
        return lastSourceDragPort && lastSourceDragPort.id ? lastSourceDragPort : null
    }
    function sourceDragDestinationAt(point) {
        if (!point) return null
        const output = node("output")
        if (!output || !output.id) return null
        const candidates = visibleCardPorts(output, true)
        const hitRadius = 18
        const hitRadiusSquared = hitRadius * hitRadius
        let closest = null
        let closestDistanceSquared = hitRadiusSquared
        for (let index = 0; index < candidates.length; ++index) {
            const candidate = candidates[index]
            const center = currentGraphSpacePortCenter(candidate.endpointId, candidate.id, output, false)
            const dx = Number(point.x) - Number(center.x)
            const dy = Number(point.y) - Number(center.y)
            const distanceSquared = dx * dx + dy * dy
            if (distanceSquared <= closestDistanceSquared) {
                closest = candidate
                closestDistanceSquared = distanceSquared
            }
        }
        return closest
    }
    function completeSourceDrag(sourcePort, destinationPort) {
        if (sourceDragDropHandled || !sourcePort || !sourcePort.id || !destinationPort || !destinationPort.id)
            return false
        sourceDragDropHandled = true
        connectDragged(sourcePort, destinationPort)
        return true
    }
    function endSourceDrag() {
        if (!dragWire.active) return
        const sourcePort = sourceDragPortFromDrop(null)
        const releasePoint = ({ "x": Number(dragWire.x || 0), "y": Number(dragWire.y || 0) })
        // Some Qt Quick delivery paths deactivate the handler before the
        // destination DropArea gets onDropped. Resolve the actual rendered
        // target from the release point in that case; DropArea uses the same
        // one-shot completion helper when it arrives first.
        if (!sourceDragDropHandled) {
            const destinationPort = sourceDragDestinationAt(releasePoint)
            if (destinationPort) completeSourceDrag(sourcePort, destinationPort)
        }
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0 })
        connectionPreview = ({})
    }
    function previewDestination(port) {
        if (!port || !source || !source.id) return
        const isCompatible = compatible(port)
        const existing = routesForPort(port, true)
        const requiresMerge = isCompatible && source.kind === "axis" && existing.length > 0
        connectionPreview = ({ "portId": String(port.id), "ownerNodeId": portOwnerNodeId(port), "compatible": isCompatible,
            "message": source.label + " → " + port.label + (isCompatible
                ? requiresMerge ? " · occupied analog output: choose Replace or Mixer."
                : existing.length > 0 ? " · existing route present."
                : " · compatible direct route."
                : " · incompatible endpoint type.") })
    }
    function clearDestinationPreview(port) {
        if (!port || String(connectionPreview.portId || "") === String(port.id || "")) connectionPreview = ({})
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
        const livePosition = liveNodePositions[String(nodeData.id || "")]
            || liveNodePositions[String(nodeData.objectId || "")]
        if (livePosition) return livePosition
        const saved = nodePositions[String(nodeData.objectId || nodeData.id || "")]
            || nodePositions[String(nodeData.id || "")]
        if (saved) return saved
        return ({ "x": Number(nodeData.x === undefined ? fallbackX : nodeData.x),
                  "y": Number(nodeData.y === undefined ? fallbackY : nodeData.y) })
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
        if (scheduleGeometry !== false) wireGeometryTimer.restart()
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
        if (liveDragNodeId && nodeMatchesId(nodeForId(ownerId), liveDragNodeId)
                && !portAnchorDiagnosticsEnabled) return
        const endpointKey = String(port.endpointId || "")
        const legacyKey = String(port.id || "")
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
        wireGeometryTimer.restart()
    }
    // A hidden or destroyed port row no longer describes a rendered endpoint.
    // Drop its measured offset so a collapsed density group falls back to its
    // current card geometry instead of retaining a stale row position.
    function forgetPortAnchor(port) {
        if (!port) return
        const endpointKey = String(port.endpointId || "")
        const legacyKey = String(port.id || "")
        if ((!endpointKey || !portAnchors[endpointKey]) && (!legacyKey || !portAnchors[legacyKey])) return
        const next = ({})
        const offsets = ({})
        for (const key in portAnchors)
            if (key !== endpointKey && key !== legacyKey) next[key] = portAnchors[key]
        for (const key in portAnchorOffsets)
            if (key !== endpointKey && key !== legacyKey) offsets[key] = portAnchorOffsets[key]
        portAnchors = next
        portAnchorOffsets = offsets
        wireGeometryTimer.restart()
    }
    // This is the sole endpoint resolver for Canvas geometry, its spatial hit
    // buckets, live-drag replacement segments, and insertion preview.  Its
    // result is always in the unscaled `scene` logical coordinate space.
    function currentGraphSpacePortCenter(endpointId, legacyEndpointId, nodeData, sourceSide) {
        const endpointKey = String(endpointId || "")
        const legacyKey = String(legacyEndpointId || "")
        const offset = portAnchorOffsets[endpointKey] || portAnchorOffsets[legacyKey]
        if (offset && offset.ownerNodeId) {
            const owner = nodeForId(offset.ownerNodeId)
            if (owner && nodeIdentity(owner)) {
                const position = nodePosition(owner, Number(owner.x || 0), Number(owner.y || 0))
                return ({ "x": Number(position.x) + Number(offset.x), "y": Number(position.y) + Number(offset.y) })
            }
        }
        // A direct delegate measurement is better than synthetic geometry
        // whenever an owner cannot be resolved (for example during creation).
        const measured = portAnchors[endpointKey] || portAnchors[legacyKey]
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
            if (diagram) diagram.requestPaint()
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
        const obstacle = dragSimplified ? ({ "hasDetour": false, "detourY": 0, "underCard": false })
            : obstaclePlan(sourceAnchor.x, sourceAnchor.y, destinationAnchor.x, destinationAnchor.y, excludedIds)
        return decorateWireSegment({ "id": String(canonical.id || ""), "routeSegmentId": String(canonical.id || ""),
            "sourceNodeId": nodeIdentity(sourceNode), "destinationNodeId": nodeIdentity(destinationNode),
            "sourceEndpointId": String(canonical.sourceEndpointId || ""),
            "destinationEndpointId": String(canonical.destinationEndpointId || ""),
            "startX": sourceAnchor.x, "startY": sourceAnchor.y,
            "endX": destinationAnchor.x, "endY": destinationAnchor.y,
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
    function snapshotWireGeometry(entries) {
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
                // Canvas only needs these immutable presentation fields while
                // the current cache is rebuilt underneath it.
                segments.push({ "routeSegmentId": String(segment.routeSegmentId || ""),
                    "startX": Number(segment.startX), "startY": Number(segment.startY),
                    "endX": Number(segment.endX), "endY": Number(segment.endY),
                    "hasDetour": Boolean(segment.hasDetour), "detourY": Number(segment.detourY || 0),
                    "underCard": Boolean(segment.underCard) })
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
    function prepareWireReflow() {
        if (reducedMotion || !wireGeometry || wireGeometry.length === 0) {
            reflowWireGeometry = []
            reflowWireSegmentIndex = ({})
            wireReflowPending = false
            wireReflow = 1
            return
        }
        wireReflowAnimation.stop()
        reflowWireGeometry = snapshotWireGeometry(wireGeometry)
        reflowWireSegmentIndex = indexReflowWireSegments(reflowWireGeometry)
        wireReflowPending = reflowWireGeometry.length > 0
        wireReflow = wireReflowPending ? 0 : 1
    }
    function rebuildWireGeometry() {
        geometryRebuildCount += 1
        const routes = graph.routes || []
        const nodes = graph.nodes || []
        const nodeById = ({})
        const fanOutGroups = ({})
        const fanOutLanes = ({})
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
                segments.push(geometryForCanonicalSegment(route.id, canonical, sourceNode, destinationNode, lane, false))
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
        if (wireReflowPending) {
            wireReflowPending = false
            if (reflowWireGeometry.length > 0 && !reducedMotion) {
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
            }
        }
        if (diagram) diagram.requestPaint()
    }
    function refreshLiveDragGeometry(nodeId) {
        const movedNode = nodeForId(nodeId)
        if (!movedNode || !nodeIdentity(movedNode)) return 0
        const refs = wireNodeSegmentRefs[String(nodeId || "")]
            || wireNodeSegmentRefs[nodeIdentity(movedNode)]
            || wireNodeSegmentRefs[nodeStorageIdentity(movedNode)] || []
        if (refs.length === 0) return 0
        // Preserve all unrelated route entries by reference. Only incident
        // routes receive a replacement segments array.
        const next = wireGeometry
        const changedRoutes = ({})
        const changed = []
        for (let refIndex = 0; refIndex < refs.length; ++refIndex) {
            const ref = refs[refIndex]
            const entry = next[ref.entryIndex]
            if (!entry) continue
            let replacement = changedRoutes[String(ref.entryIndex)]
            if (!replacement) {
                replacement = ({ "route": entry.route || ({}), "routeId": entry.routeId,
                    "lane": Number(entry.lane || 0), "segments": (entry.segments || []).slice() })
                changedRoutes[String(ref.entryIndex)] = replacement
            }
            const oldSegment = replacement.segments[ref.segmentIndex]
            if (!oldSegment) continue
            const sourceNode = nodeForId(oldSegment.sourceNodeId)
            const destinationNode = nodeForId(oldSegment.destinationNodeId)
            const canonical = ({ "id": oldSegment.routeSegmentId,
                "sourceNodeId": oldSegment.sourceNodeId, "destinationNodeId": oldSegment.destinationNodeId,
                "sourceEndpointId": oldSegment.sourceEndpointId,
                "destinationEndpointId": oldSegment.destinationEndpointId })
            const updated = geometryForCanonicalSegment(replacement.routeId, canonical, sourceNode, destinationNode,
                replacement.lane, true)
            replacement.segments[ref.segmentIndex] = updated
            changed.push({ "route": replacement.route, "routeId": replacement.routeId, "segment": updated,
                "oldCacheKey": oldSegment.cacheKey || routeSegmentCacheKey(replacement.routeId, oldSegment) })
        }
        for (const entryIndex in changedRoutes) {
            const replacement = changedRoutes[entryIndex]
            next[Number(entryIndex)] = routeGeometryEntry(replacement.route, replacement.segments, replacement.lane)
        }
        if (changed.length === 0) return 0
        // These maps are presentation caches. Mutating only the bucket keys
        // belonging to affected segments avoids copying or rebuilding the
        // complete spatial index for an otherwise local drag.
        const buckets = wireBuckets
        const segmentBucketKeys = wireSegmentBucketKeys
        for (let changedIndex = 0; changedIndex < changed.length; ++changedIndex) {
            const previous = changed[changedIndex]
            const oldKeys = segmentBucketKeys[previous.oldCacheKey] || []
            for (let keyIndex = 0; keyIndex < oldKeys.length; ++keyIndex) {
                const bucketKey = oldKeys[keyIndex]
                const bucket = buckets[bucketKey] || []
                const remaining = bucket.filter(function(candidate) { return candidate.cacheKey !== previous.oldCacheKey })
                if (remaining.length > 0) buckets[bucketKey] = remaining
                else delete buckets[bucketKey]
            }
            delete segmentBucketKeys[previous.oldCacheKey]
            addSegmentToBuckets(buckets, segmentBucketKeys, previous.route, previous.routeId, previous.segment)
        }
        wireGeometry = next
        liveDragGeometryUpdates += 1
        liveDragAffectedSegments += changed.length
        if (diagram) diagram.requestPaint()
        return changed.length
    }
    function beginLiveNodeDrag(nodeData) {
        const id = nodeIdentity(nodeData)
        if (!id) return false
        liveDragNodeId = id
        return true
    }
    function isLiveNodeDrag(nodeData) {
        return Boolean(liveDragNodeId) && nodeMatchesId(nodeData, liveDragNodeId)
    }
    function updateLiveNodeDrag(nodeData, x, y) {
        const id = nodeIdentity(nodeData)
        if (!id || !isFinite(x) || !isFinite(y)) return 0
        if (!liveDragNodeId) liveDragNodeId = id
        const next = ({})
        for (const key in liveNodePositions) next[key] = liveNodePositions[key]
        const position = ({ "x": Number(x), "y": Number(y) })
        next[id] = position
        const storedId = nodeStorageIdentity(nodeData)
        if (storedId) next[storedId] = position
        liveNodePositions = next
        // Test-only measurement can choose to observe each drag sample without
        // adding visual-anchor work to the product pointer path.
        if (portAnchorDiagnosticsEnabled) requestPortAnchorMeasurement()
        return refreshLiveDragGeometry(id)
    }
    function finishLiveNodeDrag(nodeData, x, y, persist) {
        const id = nodeIdentity(nodeData)
        if (!id) return false
        updateLiveNodeDrag(nodeData, x, y)
        // Snapshot the exact live wire the user just moved. The final cache
        // pass is allowed to route around nearby cards, but it will hand over
        // through a short geometry morph instead of popping to the new path.
        prepareWireReflow()
        noteNodePosition(nodeStorageIdentity(nodeData), x, y, false)
        const next = ({})
        for (const key in liveNodePositions) {
            if (key !== id && key !== nodeStorageIdentity(nodeData)) next[key] = liveNodePositions[key]
        }
        liveNodePositions = next
        liveDragNodeId = ""
        // A fixture can deliberately skip persistence; it still receives one
        // complete obstacle pass on release. The product path gets that same
        // one pass from onGraphChanged after the single layout save, avoiding
        // a redundant pre-save rebuild.
        if (persist === false) {
            rebuildWireGeometry()
            return true
        }
        const saved = saveNodePlacement(nodeData, x, y, Boolean(nodeData.pinned))
        if (!saved) rebuildWireGeometry()
        if (saved) nodePlacementWriteCount += 1
        return saved
    }
    function cancelLiveNodeDrag(nodeData) {
        if (!isLiveNodeDrag(nodeData)) return
        const id = nodeIdentity(nodeData)
        const storedId = nodeStorageIdentity(nodeData)
        const next = ({})
        for (const key in liveNodePositions) {
            if (key !== id && key !== storedId) next[key] = liveNodePositions[key]
        }
        liveNodePositions = next
        liveDragNodeId = ""
        rebuildWireGeometry()
    }
    function restartWireMotion() {
        if (reducedMotion) {
            wireReveal = 1
            wireRetire = 1
            retiringWireGeometry = []
            return
        }
        wireReveal = 0
        wireRevealAnimation.restart()
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
        if (mode === "effective") { notice = "Effective view is read-only. Return to Configured to edit."; noticeError = true; return }
        if (!port.available) { notice = "This input is inactive until calibration reports meaningful travel."; noticeError = true; return }
        source = port
        inspectedRoute = ({})
        inspectedNode = ({})
        notice = "Selected " + port.label + ". Choose a compatible destination."; noticeError = false
    }
    function connect(port, replace) {
        if (!compatible(port)) {
            notice = source && source.kind ? "Select a compatible destination." : "Select an input first."
            noticeError = true
            return
        }
        const sourceBeforeConnect = source
        const destination = destinationFor(port)
        previewDestination(port)
        const result = source.endpointId && port.endpointId
            ? backendObject.connectSignalFlowEndpoints(String(source.endpointId), String(port.endpointId),
                replace ? "replace" : "", Number(graph.revision || 0))
            : backendObject.signalFlowConnect(String(source.kind), Number(source.index), Number(source.subIndex || 0),
                destination, replace, Number(graph.revision || 0))
        announce(result, "Connection was not applied.")
        if (result && result.success) {
            if (!keepLearnedRouteHighlighted(sourceBeforeConnect, port.id)) {
                source = ({})
                inspectedRoute = ({})
            }
        }
        else if (result && String(result.message).indexOf("Choose Replace") >= 0) {
            conflictDialog.destination = destination
            conflictDialog.label = port.label
            conflictDialog.destinationPortId = String(port.id || "")
            conflictDialog.open()
        }
    }
    function connectDragged(sourcePort, destinationPort) {
        if (!sourcePort || !destinationPort) return
        source = sourcePort
        inspectedRoute = ({})
        inspectedNode = ({})
        previewDestination(destinationPort)
        connect(destinationPort, false)
    }
    function connectWithMixer(modeName) {
        const sourceBeforeConnect = source
        const destinationPort = destinationPorts().filter(function(port) {
            return String(port.id || "") === String(conflictDialog.destinationPortId || "")
        })[0]
        const result = source.endpointId && destinationPort && destinationPort.endpointId
            ? backendObject.connectSignalFlowEndpoints(String(source.endpointId), String(destinationPort.endpointId),
                modeName, Number(graph.revision || 0))
            : backendObject.signalFlowConnectWithMixer(String(source.kind), Number(source.index),
                Number(source.subIndex || 0), conflictDialog.destination, modeName, Number(graph.revision || 0))
        announce(result, "Mixer connection was not applied.")
        if (result && result.success) {
            if (!keepLearnedRouteHighlighted(sourceBeforeConnect, conflictDialog.destinationPortId)) {
                source = ({})
                inspectedRoute = ({})
            }
        }
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
        const segments = inspectedRoute.segments || []
        const segmentId = selectedSegmentId || (segments.length > 0 ? String(segments[0].id || "") : "")
        const result = enabled
            ? backendObject.signalFlowRemoveOrBypassProcessor(String(detail.id || ""), Number(graph.revision || 0))
            : backendObject.signalFlowInsertProcessor(segmentId, kind, Number(graph.revision || 0))
        announce(result, "Processor change was not applied.")
        if (result && result.success) processorDialog.close()
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
        const result = backendObject.signalFlowRemoveOrBypassProcessor(String(inspectedNode.objectId),
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
        workspaceRestored = true
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
            "layoutLocked": savedValue("layoutLocked", false)
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
        return persistWorkspace({}, "")
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
    function toggleLayoutLocked() {
        const locked = Boolean(graph.workspace && graph.workspace.layoutLocked)
        return persistWorkspace({ "layoutLocked": !locked }, !locked
            ? "Layout locked. Dragging cards is disabled."
            : "Layout unlocked. Drag cards to reposition them.")
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
        if (conflictDialog.visible) conflictDialog.close()
        else if (processorDialog.visible) processorDialog.close()
        else if (shareDialog.visible) shareDialog.close()
        else if (deckSourceLearnDialog.visible) deckSourceLearnDialog.close()
        else if (learnDialog.visible) learnDialog.close()
        else if (aliasDialog.visible) aliasDialog.close()
        else if (defaultsDialog.visible) defaultsDialog.close()
        else if (explainDialog.visible) explainDialog.close()
        else if (source && source.id || inspectedRoute && inspectedRoute.id || inspectedNode && inspectedNode.id) {
            source = ({})
            inspectedRoute = ({})
            inspectedNode = ({})
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
        if (action === "zoom-in") { zoom = Math.min(1.6, zoom + 0.1); saveTimer.restart(); return true }
        if (action === "zoom-out") { zoom = Math.max(0.5, zoom - 0.1); saveTimer.restart(); return true }
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
        const nextRouteIds = ({})
        const nextRoutes = graph.routes || []
        // Node placement and workspace saves update the graph too.  They do
        // not change a route, so replaying the reveal animation would blank
        // otherwise live wires immediately after a card is dropped.
        const topologyChanged = routeTopologySignature(nextRoutes) !== renderedRouteTopologySignature()
        for (let index = 0; index < nextRoutes.length; ++index)
            nextRouteIds[String(nextRoutes[index].id || "")] = true
        if (topologyChanged) {
            const previousRouteIds = ({})
            const previousGeometry = wireGeometry || []
            for (let index = 0; index < previousGeometry.length; ++index) {
                const routeId = String(previousGeometry[index] && previousGeometry[index].routeId || "")
                if (routeId.length > 0) previousRouteIds[routeId] = true
            }
            // Topology changes also reflow surviving routes. Keep their exact
            // last curve so the stable cache can use the normal geometry morph.
            prepareWireReflow()
            retiringWireGeometry = snapshotWireGeometry(previousGeometry).filter(function(entry) {
                return entry && entry.routeId && !nextRouteIds[String(entry.routeId)]
            })
            const appearing = ({})
            for (let index = 0; index < nextRoutes.length; ++index) {
                const routeId = String(nextRoutes[index].id || "")
                if (routeId.length > 0 && !previousRouteIds[routeId]) appearing[routeId] = true
            }
            appearingWireRouteIds = appearing
            wireAppearancePending = Object.keys(appearing).length > 0
            wireAppear = wireAppearancePending ? 0 : 1
            // Never reset the whole graph to zero opacity for one connection.
            wireRevealAnimation.stop()
            wireReveal = 1
            wireRetireAnimation.stop()
            if (reducedMotion) {
                wireRetire = 1
                retiringWireGeometry = []
                wireAppear = 1
                wireAppearancePending = false
                appearingWireRouteIds = ({})
            } else if (retiringWireGeometry.length > 0) {
                wireRetire = 0
                wireRetireAnimation.restart()
            } else wireRetire = 1
        }
        portAnchors = ({})
        portAnchorOffsets = ({})
        // Clear endpoint measurements before clearing a saved-position
        // override.  That keeps the position-map observer below from
        // rebuilding once with anchors belonging to the graph we just left.
        nodePositions = ({})
        liveNodePositions = ({})
        liveDragNodeId = ""
        // Repeater delegates report their measured centers on the following
        // geometry turn. Their timers are queued before the handoff timer, so
        // this retains the current wire until exactly one stable,
        // obstacle-aware rebuild is ready.
        requestPortAnchorMeasurement()
        // With no routes there cannot be a delegate endpoint callback to do
        // this work, but stale geometry still has to disappear immediately.
        if (nextRoutes.length === 0) wireGeometryTimer.restart()
        else graphGeometryHandoffTimer.restart()
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
        if (!liveDragNodeId && Object.keys(portAnchorOffsets).length > 0)
            wireGeometryTimer.restart()
    }
    onPresentationStateChanged: {
        presentationRestored = false
        Qt.callLater(restorePresentationState)
    }
    onInspectedRouteChanged: if (diagram) diagram.requestPaint()
    onHoveredRouteIdChanged: if (diagram) diagram.requestPaint()
    onSourceChanged: if (diagram) diagram.requestPaint()
    onInspectedNodeChanged: if (diagram) diagram.requestPaint()
    onModeChanged: if (diagram) diagram.requestPaint()
    onLiveTelemetryChanged: if (diagram) diagram.requestPaint()
    onSignalFocusChanged: if (diagram) diagram.requestPaint()
    onXrayModeChanged: if (diagram) diagram.requestPaint()
    onReducedMotionChanged: {
        if (reducedMotion) {
            wireReveal = 1
            wireRetire = 1
            retiringWireGeometry = []
            wireReflow = 1
            wireReflowPending = false
            reflowWireGeometry = []
            reflowWireSegmentIndex = ({})
            wireAppear = 1
            wireAppearancePending = false
            appearingWireRouteIds = ({})
        }
        if (diagram) diagram.requestPaint()
    }
    onWireRevealChanged: if (diagram) diagram.requestPaint()
    onWireRetireChanged: if (diagram) diagram.requestPaint()
    onWireReflowChanged: if (diagram) diagram.requestPaint()
    onWireAppearChanged: if (diagram) diagram.requestPaint()
    onLiveModeChanged: if (diagram) diagram.requestPaint()
    onDragWireChanged: if (diagram) diagram.requestPaint()
    onRouteStateFilterChanged: if (diagram) diagram.requestPaint()
    onQueryChanged: if (diagram) diagram.requestPaint()
    onNoticeChanged: {
        if (notice.length > 0) noticeTimer.restart()
        else noticeTimer.stop()
    }

    Connections {
        target: backendObject
        function onSignalFlowChanged() {
            root.graph = backendObject.signalFlowGraph
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
        // bypass this timer and update connected geometry synchronously.
        interval: 0
        repeat: false
        onTriggered: root.rebuildWireGeometry()
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
        onTriggered: wireGeometryTimer.restart()
    }

    Timer {
        id: noticeTimer
        interval: root.noticeError ? 8000 : 4200
        repeat: false
        onTriggered: { root.notice = ""; root.noticeError = false }
    }

    NumberAnimation {
        id: wireRevealAnimation
        target: root
        property: "wireReveal"
        from: 0
        to: 1
        duration: 220
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: wireRetireAnimation
        target: root
        property: "wireRetire"
        from: 0
        to: 1
        duration: 170
        easing.type: Easing.InCubic
        onStopped: if (root.wireRetire >= 0.999) root.retiringWireGeometry = []
    }
    NumberAnimation {
        id: wireReflowAnimation
        target: root
        property: "wireReflow"
        from: 0
        to: 1
        duration: 260
        easing.type: Easing.InOutCubic
        onStopped: if (root.wireReflow >= 0.999) {
            root.reflowWireGeometry = []
            root.reflowWireSegmentIndex = ({})
        }
    }
    NumberAnimation {
        id: wireAppearAnimation
        target: root
        property: "wireAppear"
        from: 0
        to: 1
        duration: 260
        easing.type: Easing.InOutCubic
        onStopped: if (root.wireAppear >= 0.999) root.appearingWireRouteIds = ({})
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
        ToolTip.visible: deckButton.hovered && deckButton.helpText.length > 0
        ToolTip.delay: 450
        ToolTip.text: deckButton.helpText
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
                root.updateSourceDrag(point)
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
        ToolTip.visible: hover.containsMouse
        ToolTip.delay: 450
        ToolTip.text: !flowPort.port || !flowPort.port.available
            ? "This control is inactive until calibration reports meaningful travel."
            : flowPort.destination && root.source && !flowPort.isCompatible
                ? "Choose a destination compatible with the selected source."
                : flowPort.destination && root.connectionPreview && root.connectionPreview.portId === String(flowPort.port.id || "")
                    ? root.connectionPreview.message
                : flowPort.destination ? "Choose this destination for the selected source."
                                       : "Select this physical source, then choose a compatible destination."
    }

    // A port row lives on its owning card.  The visual dot stays compact while
    // its 30 px target provides the forgiving direct-manipulation affordance.
    component GraphPortRow: Item {
        id: graphPortRow
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
        Accessible.name: (destination ? "Destination " : "Source ") + String(port.label || "port")
        Accessible.description: destination
            ? "Click or drop a compatible source here to create a canonical route."
            : "Click or drag from this physical endpoint to route it."
        function updateAnchor() {
            if (!port || !endpointKey || !scene || !visible) return
            const point = hitTarget.mapToItem(scene, hitTarget.width * 0.5, hitTarget.height * 0.5)
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
            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
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
                    Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 110 } }
                }
                Rectangle {
                    id: destinationDot
                    objectName: "signalFlowPortVisual:" + graphPortRow.endpointKey
                    anchors.centerIn: parent
                    width: graphPortRow.routingCandidate ? 13 : 10
                    height: width; radius: width / 2
                    color: !graphPortRow.port.available ? deck.disabled
                        : graphPortRow.previewedDestination ? deck.attention
                        : graphPortRow.compatibleTarget ? deck.accent : graphPortRow.port.mapped ? deck.healthy : deck.textMuted
                    border.width: graphPortRow.previewedDestination ? 3 : graphPortRow.compatibleTarget ? 2 : 1
                    border.color: graphPortRow.previewedDestination ? deck.attention
                        : graphPortRow.compatibleTarget ? deck.focus : deck.border
                    Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic } }
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
                    width: graphPortRow.sourceSelected ? 13 : 10
                    height: width; radius: width / 2
                    color: !graphPortRow.port.available ? deck.disabled
                        : graphPortRow.sourceSelected
                            ? deck.accent : graphPortRow.port.mapped ? deck.informational : deck.textMuted
                    border.width: graphPortRow.sourceSelected ? 3 : 1
                    border.color: graphPortRow.sourceSelected ? deck.accent : deck.focus
                    Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic } }
                }
            }
        }
        Item {
            id: hitTarget
            objectName: "signalFlowPortHitTarget:" + graphPortRow.endpointKey
            width: 30; height: 30
            x: destination ? 0 : parent.width - width
            y: -1
            Drag.active: sourceDrag.active
            Drag.source: graphPortRow
            Drag.keys: ["signal-flow-source"]
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2
            DragHandler {
                id: sourceDrag
                enabled: !graphPortRow.destination && root.mode === "configured" && graphPortRow.port.available
                function updateWireAtPointer() {
                    const point = hitTarget.mapToItem(scene, centroid.position.x, centroid.position.y)
                    root.updateSourceDrag(point)
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
                    if (graphPortRow.destination) root.connect(graphPortRow.port, false)
                    else root.selectSource(graphPortRow.port)
                }
            }
            HoverHandler {
                onHoveredChanged: {
                    if (hovered && graphPortRow.destination) root.previewDestination(graphPortRow.port)
                    else if (!hovered && graphPortRow.destination) root.clearDestinationPreview(graphPortRow.port)
                }
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
                readonly property bool collapsed: root.cardGroupCollapsed(signalFlowPortGroups.nodeData,
                    groupName, signalFlowPortGroups.destination)
                readonly property int routeCount: root.cardGroupRouteCount(signalFlowPortGroups.nodeData,
                    groupName, signalFlowPortGroups.destination)
                width: signalFlowPortGroups.width
                spacing: 2
                Row {
                    id: groupHeader
                    width: parent.width
                    height: 20
                    spacing: 4
                    Text {
                        width: Math.max(48, groupHeader.width - groupToggle.width - 4)
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupColumn.groupName.toUpperCase() + " · " + groupColumn.routeCount
                            + (groupColumn.routeCount === 1 ? " ROUTE" : " ROUTES")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    DeckButton {
                        id: groupToggle
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
                    visible: groupColumn.collapsed
                    width: parent.width
                    text: groupColumn.routeCount > 0 ? groupColumn.routeCount + " routed endpoint"
                        + (groupColumn.routeCount === 1 ? "" : "s") + " hidden" : "Collapsed"
                    color: deck.textMuted
                    font.family: deck.bodyFont
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
                Repeater {
                    model: groupColumn.collapsed ? [] : root.cardGroupPorts(signalFlowPortGroups.nodeData,
                        groupColumn.groupName, signalFlowPortGroups.destination)
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
        spacing: deck.sectionGap

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space8
            Text { text: "SIGNAL FLOW"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true }
            DeckButton { Layout.preferredWidth: 138; text: "Device Rig"; helpText: root.graph.deviceRigName || "Choose the Device Rig context."; onClicked: deckRigContextMenu.open() }
            DeckButton { Layout.preferredWidth: 126; text: "Profile"; helpText: root.graph.profileName || "Choose the editing profile."; onClicked: deckProfileContextMenu.open() }
            DeckButton { text: "Configured"; emphasized: root.mode === "configured"; onClicked: root.mode = "configured" }
            DeckButton { text: "Effective"; emphasized: root.mode === "effective"; onClicked: root.mode = "effective" }
            TextField {
                id: deckSearch
                Layout.preferredWidth: 190
                Layout.fillWidth: true
                implicitHeight: deck.compactControlHeight
                placeholderText: "Search ports"
                color: deck.textPrimary
                placeholderTextColor: deck.textMuted
                selectByMouse: true
                onTextChanged: root.query = text
                background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border }
            }
            DeckButton { visible: root.query.length > 0; text: "Focus"; helpText: "Center the first matching route or source port."; onClicked: root.focusSearchResult() }
            DeckButton { text: root.routeStateFilter === "all" ? "Filter" : "Filter: " + root.routeStateFilter; helpText: "Filter routes by mapping state, problems, or current activity."; onClicked: deckStateFilterMenu.open() }
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
                Flickable {
                    id: graphViewport
                    objectName: "signalFlowGraphViewport"
                    anchors.fill: parent
                    contentWidth: scene.width * root.zoom
                    contentHeight: scene.height * root.zoom
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    onMovementEnded: saveTimer.restart()
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
                                        if (detour) {
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
                                context.strokeStyle = deck.graphSurface
                                context.lineWidth = lineWidth + 3.2
                                context.globalAlpha = alpha * 0.96
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
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.strokeStyle = deck.graphGrid
                                context.globalAlpha = 0.55
                                for (let x = 0; x < width; x += 48) { context.beginPath(); context.moveTo(x, 0); context.lineTo(x, height); context.stroke() }
                                for (let y = 0; y < height; y += 48) { context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke() }
                                context.globalAlpha = 1
                                const retiring = root.retiringWireGeometry || []
                                for (let retiredIndex = 0; retiredIndex < retiring.length; ++retiredIndex) {
                                    const retired = retiring[retiredIndex]
                                    const retiredSegments = retired.segments || []
                                    for (let segmentIndex = 0; segmentIndex < retiredSegments.length; ++segmentIndex) {
                                        const segment = retiredSegments[segmentIndex]
                                        drawWire(context, segment.startX, segment.startY, segment.endX, segment.endY,
                                            deck.textMuted, 1.8, 0.62 * (1 - root.wireRetire),
                                            Number(retired.lane === undefined ? root.stableLane(retired.routeId, 9) - 4 : retired.lane),
                                            true, 1 - root.wireRetire,
                                            segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                const geometry = root.wireGeometry || []
                                for (let i = 0; i < geometry.length; ++i) {
                                    const entry = geometry[i]
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
                                        const lane = Number(entry.lane === undefined
                                            ? root.stableLane(route.id, 9) - 4 : entry.lane)
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
                                if (root.dragWire && root.dragWire.active) {
                                    const sourcePort = root.dragWire.source || ({})
                                    const sourceId = String(sourcePort.id || "")
                                    const sourceEndpoint = String(sourcePort.endpointId || sourcePort.id || "")
                                    const sourceNode = root.nodeForId(root.portOwnerNodeId(sourcePort))
                                    const start = root.currentGraphSpacePortCenter(sourceEndpoint, sourceId, sourceNode, true)
                                    const startX = Number(start.x)
                                    const startY = Number(start.y)
                                    const pointerX = Number(root.dragWire.x || startX)
                                    const pointerY = Number(root.dragWire.y || startY)
                                    drawDragPreview(context, startX, startY, pointerX, pointerY)
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
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPositionChanged: function(mouse) { root.updateWireHover(mouse.x, mouse.y) }
                            onExited: root.updateWireHover(-10000, -10000)
                            // Flickable may take over a background gesture
                            // before MouseArea emits `clicked`. Dismiss on
                            // the initial empty-canvas press so selection is
                            // never stranded behind a pan gesture.
                            onPressed: function(mouse) {
                                if (mouse.button !== Qt.LeftButton) return
                                root.dismissEmptyGraphAt(mouse.x, mouse.y)
                            }
                            onClicked: function(mouse) {
                                const hit = root.hitWire(mouse.x, mouse.y)
                                if (!hit || !hit.route) {
                                    if (mouse.button === Qt.LeftButton) root.clearGraphSelection()
                                    return
                                }
                                root.inspectedRoute = hit.route
                                root.selectedSegmentId = String(hit.routeSegmentId || "")
                                root.inspectedNode = ({})
                                root.source = ({})
                                if (mouse.button === Qt.RightButton) {
                                    deckRouteContextMenu.targetRoute = hit.route
                                    deckRouteContextMenu.open()
                                }
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
                                const hit = root.hitWire(drag.x, drag.y)
                                if (hit) root.previewProcessorTarget(hit.route, String(hit.routeSegmentId || ""))
                            }
                            onPositionChanged: function(drag) {
                                const hit = root.hitWire(drag.x, drag.y)
                                if (hit) root.previewProcessorTarget(hit.route, String(hit.routeSegmentId || ""))
                            }
                            onExited: {
                                root.pendingProcessorRouteId = ""
                                root.pendingProcessorSegmentId = ""
                            }
                            onDropped: function(drop) {
                                const hit = root.hitWire(drop.x, drop.y)
                                root.pendingProcessorRouteId = ""
                                root.pendingProcessorSegmentId = ""
                                if (!hit || !hit.route || !hit.routeSegmentId || !drop.source || !drop.source.processorKind) return
                                root.inspectedRoute = hit.route
                                root.inspectedNode = ({})
                                root.selectedSegmentId = String(hit.routeSegmentId)
                                const result = backendObject.signalFlowInsertProcessor(String(hit.routeSegmentId),
                                    String(drop.source.processorKind), Number(root.graph.revision || 0))
                                root.announce(result, "Processor was not inserted.")
                                if (result && result.success) processorDialog.close()
                                drop.accepted = true
                            }
                        }
                        FlightDeckCard {
                            id: inputNode
                            tokens: deck
                            readonly property var nodeData: root.node("input")
                            readonly property string routingState: root.routingNodeState(nodeData)
                            x: Number(root.nodePosition(nodeData, 80, 120).x)
                            y: Number(root.nodePosition(nodeData, 80, 120).y)
                            Behavior on x { enabled: !root.isLiveNodeDrag(inputNode.nodeData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            Behavior on y { enabled: !root.isLiveNodeDrag(inputNode.nodeData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            width: root.graphCardWidth(nodeData)
                            height: Math.max(root.graphCardHeight(nodeData), inputCardContent.implicitHeight + contentPadding * 2)
                            z: routingState.length > 0 ? 3 : 2
                            opacity: root.xrayMode ? 0.58 : 1.0
                            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: root.routingNodeSurface(deck.secondarySurface, routingState)
                            border.width: root.routingNodeBorderWidth(routingState)
                            border.color: root.routingNodeBorderColor(routingState)
                            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
                            Column {
                                id: inputCardContent
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { width: parent.width; text: inputNode.nodeData.label || "Input context"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                Text { width: parent.width; text: inputNode.nodeData.connected ? "CONNECTED · VERIFIED" : "OFFLINE · SAVED IDENTITY"; color: inputNode.nodeData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                SignalFlowPortGroups { width: parent.width; nodeData: inputNode.nodeData; destination: false }
                            }
                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? inputNode : null
                                drag.axis: Drag.XAndYAxis
                                onPressed: root.beginLiveNodeDrag(inputNode.nodeData)
                                onPositionChanged: function(mouse) {
                                    if (drag.active) root.updateLiveNodeDrag(inputNode.nodeData, inputNode.x, inputNode.y)
                                }
                                onReleased: {
                                    if (drag.active) {
                                        root.finishLiveNodeDrag(inputNode.nodeData, inputNode.x, inputNode.y)
                                    } else root.cancelLiveNodeDrag(inputNode.nodeData)
                                }
                                onClicked: function(mouse) { if (!drag.active) root.selectNode(inputNode.nodeData) }
                                onDoubleClicked: function(mouse) { if (!drag.active) root.openCardSettings(inputNode.nodeData) }
                            }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(eventPoint, button) { deckNodeContextMenu.targetNode = inputNode.nodeData; deckNodeContextMenu.open() } }
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
                                x: Number(root.nodePosition(modelData, 80, 120).x)
                                y: Number(root.nodePosition(modelData, 80, 120).y)
                                Behavior on x { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                Behavior on y { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                width: root.graphCardWidth(modelData)
                                height: Math.max(root.graphCardHeight(modelData), secondaryCardContent.implicitHeight + contentPadding * 2)
                                z: routingState.length > 0 ? 3 : 2
                                opacity: root.xrayMode ? 0.58 : 1.0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: root.routingNodeSurface(modelData.missingReference
                                    ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.12) : deck.secondarySurface,
                                    routingState)
                                border.width: root.routingNodeBorderWidth(routingState)
                                border.color: root.routingNodeBorderColor(routingState)
                                Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
                                Column {
                                    id: secondaryCardContent
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                    Text { width: parent.width; text: modelData.label || "Saved input"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                    Text { width: parent.width; text: modelData.connected ? "CONNECTED · SAVED MEMBER" : modelData.missingReference ? "MISSING REFERENCE" : "OFFLINE · SAVED MEMBER"; color: modelData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                    SignalFlowPortGroups { width: parent.width; nodeData: modelData; destination: false }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    z: -1
                                    drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? secondaryInputNode : null
                                    drag.axis: Drag.XAndYAxis
                                    onPressed: root.beginLiveNodeDrag(modelData)
                                    onPositionChanged: function(mouse) {
                                        if (drag.active) root.updateLiveNodeDrag(modelData, secondaryInputNode.x, secondaryInputNode.y)
                                    }
                                    onReleased: {
                                        if (drag.active) {
                                            root.finishLiveNodeDrag(modelData, secondaryInputNode.x, secondaryInputNode.y)
                                        } else root.cancelLiveNodeDrag(modelData)
                                    }
                                    onClicked: function(mouse) { if (!drag.active) root.selectNode(modelData) }
                                    onDoubleClicked: function(mouse) { if (!drag.active) root.openCardSettings(modelData) }
                                }
                                TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(eventPoint, button) { deckNodeContextMenu.targetNode = modelData; deckNodeContextMenu.open() } }
                            }
                        }
                        Repeater {
                            model: (root.graph.nodes || []).filter(function(item) { return item.kind === "processor" })
                            delegate: FlightDeckCard {
                                id: processorNode
                                required property var modelData
                                tokens: deck
                                readonly property string routingState: root.routingNodeState(modelData)
                                x: Number(root.nodePosition(modelData, 680, 120).x)
                                y: Number(root.nodePosition(modelData, 680, 120).y)
                                Behavior on x { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                Behavior on y { enabled: !root.isLiveNodeDrag(modelData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                width: root.graphCardWidth(modelData)
                                height: root.graphCardHeight(modelData)
                                z: routingState.length > 0 ? 3 : 2
                                opacity: root.xrayMode ? 0.58 : 1.0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                                Behavior on height { NumberAnimation { duration: root.reducedMotion ? 0 : 190; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: root.routingNodeSurface(deck.elevatedSurface, routingState)
                                border.width: root.routingNodeBorderWidth(routingState)
                                border.color: root.routingNodeBorderColor(routingState)
                                Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: 4
                                    Text { text: modelData.label; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.detail; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                    Text { visible: Boolean(modelData.shared); text: "SHARED · " + Number(modelData.sharedChannelCount || 0) + " CHANNELS"; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; Layout.fillWidth: true }
                                    Repeater {
                                        // The labels mirror the actual paired port rows below;
                                        // they are not decorative processor dots.
                                        model: modelData.shared ? Math.min(4, Number(modelData.channelCount || 0)) : 0
                                        delegate: Row {
                                            spacing: 3
                                            Text { text: "CHANNEL " + (index + 1) + " · IN / OUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8 }
                                        }
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
                                        y: 34 + Math.floor(index / 2) * 18
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
                                            onClicked: {
                                                root.inspectedNode = processorNode.modelData
                                                root.inspectedRoute = root.routeForId(modelData.routeId)
                                                root.selectedSegmentId = String(modelData.routeSegmentId || "")
                                            }
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? processorNode : null
                                    drag.axis: Drag.XAndYAxis
                                    onPressed: root.beginLiveNodeDrag(modelData)
                                    onPositionChanged: function(mouse) {
                                        if (drag.active) root.updateLiveNodeDrag(modelData, processorNode.x, processorNode.y)
                                    }
                                    onReleased: {
                                        if (drag.active) {
                                            root.finishLiveNodeDrag(modelData, processorNode.x, processorNode.y)
                                        } else root.cancelLiveNodeDrag(modelData)
                                    }
                                    onClicked: function(mouse) { if (!drag.active) root.selectNode(modelData) }
                                    onDoubleClicked: function(mouse) { if (!drag.active) root.openNodeSettings(modelData) }
                                }
                                TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(eventPoint, button) { deckNodeContextMenu.targetNode = modelData; deckNodeContextMenu.open() } }
                            }
                        }
                        FlightDeckCard {
                            id: outputNode
                            tokens: deck
                            readonly property var nodeData: root.node("output")
                            readonly property string routingState: root.routingNodeState(nodeData)
                            x: Number(root.nodePosition(nodeData, 1320, 120).x)
                            y: Number(root.nodePosition(nodeData, 1320, 120).y)
                            Behavior on x { enabled: !root.isLiveNodeDrag(outputNode.nodeData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            Behavior on y { enabled: !root.isLiveNodeDrag(outputNode.nodeData); NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            width: root.graphCardWidth(nodeData)
                            height: Math.max(root.graphCardHeight(nodeData), outputCardContent.implicitHeight + contentPadding * 2)
                            z: routingState.length > 0 ? 3 : 2
                            opacity: root.xrayMode ? 0.58 : 1.0
                            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: root.routingNodeSurface(deck.secondarySurface, routingState)
                            border.width: root.routingNodeBorderWidth(routingState)
                            border.color: root.routingNodeBorderColor(routingState)
                            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 110 } }
                            Column {
                                id: outputCardContent
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { width: parent.width; text: outputNode.nodeData.label || "Virtual output"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                                Text { width: parent.width; text: outputNode.nodeData.connected ? "READY · VIRTUAL OUTPUT" : "OUTPUT UNAVAILABLE"; color: outputNode.nodeData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                SignalFlowPortGroups { width: parent.width; nodeData: outputNode.nodeData; destination: true }
                            }
                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? outputNode : null
                                drag.axis: Drag.XAndYAxis
                                onPressed: root.beginLiveNodeDrag(outputNode.nodeData)
                                onPositionChanged: function(mouse) {
                                    if (drag.active) root.updateLiveNodeDrag(outputNode.nodeData, outputNode.x, outputNode.y)
                                }
                                onReleased: {
                                    if (drag.active) {
                                        root.finishLiveNodeDrag(outputNode.nodeData, outputNode.x, outputNode.y)
                                    } else root.cancelLiveNodeDrag(outputNode.nodeData)
                                }
                                onClicked: function(mouse) { if (!drag.active) root.selectNode(outputNode.nodeData) }
                                onDoubleClicked: function(mouse) { if (!drag.active) root.openCardSettings(outputNode.nodeData) }
                            }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(eventPoint, button) { deckNodeContextMenu.targetNode = outputNode.nodeData; deckNodeContextMenu.open() } }
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
            }

            FlightDeckCard {
                tokens: deck
                // Contextual inspector: it explains the current selection and
                // releases its width entirely when no object is selected.
                visible: root.width >= 1600 && (Boolean(root.inspectedRoute && root.inspectedRoute.id)
                    || Boolean(root.inspectedNode && root.inspectedNode.id))
                Layout.preferredWidth: Math.max(220, Math.min(305, root.width * 0.25))
                Layout.fillHeight: true
                contentPadding: deck.cardPaddingTechnical
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: deck.space8
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
                    DeckButton { text: "Remove processor"; destructive: true; visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "processor"); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.removeSelectedProcessor() }
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

    // Notices float above the graph: feedback must never push the topology
    // down or turn an ordinary action into a permanent status strip.
    Popup {
        id: feedbackToast
        parent: Overlay.overlay
        visible: root.notice.length > 0
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
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
        parent: Overlay.overlay
        visible: root.width < 1600 && (Boolean(root.inspectedRoute && root.inspectedRoute.id)
            || Boolean(root.inspectedNode && root.inspectedNode.id))
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        width: Math.min(360, root.width - 32)
        height: Math.min(420, root.height - 32)
        x: root.width - width - deck.space16
        y: Math.max(deck.space16, (root.height - height) * 0.5)
        padding: deck.cardPaddingTechnical
        background: Rectangle { radius: deck.radiusPanel; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: deck.space8
            RowLayout {
                Layout.fillWidth: true
                Text { text: "INSPECTOR"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true }
                DeckButton { text: "×"; width: 28; height: 22; padding: 0; Accessible.name: "Close inspector"; onClicked: { root.inspectedRoute = ({}); root.inspectedNode = ({}) } }
            }
            Text { Layout.fillWidth: true; text: root.inspectedRoute && root.inspectedRoute.id ? root.inspectedRoute.sourceLabel + " → " + root.inspectedRoute.destinationLabel : root.inspectedNode.label || "Signal Flow object"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; text: String(root.inspectedRoute.health || "ready").toUpperCase().replace("-", " ") + " · " + String(root.inspectedRoute.healthDetail || ""); color: root.inspectedRoute.health === "ready" ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }
            Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); Layout.fillWidth: true; text: String(root.inspectedNode.detail || ""); color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Item { Layout.fillHeight: true }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Explain route"; Layout.fillWidth: true; onClicked: root.explainRoute() }
            DeckButton { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: "Disconnect"; destructive: true; enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
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

    Menu {
        id: deckMoreMenu
        title: "Signal Flow tools"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Fit graph"; onTriggered: root.fitGraph() }
        MenuItem { text: "Auto-layout"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onTriggered: root.announce(backendObject.signalFlowAutoLayout(), "Auto-layout was not applied.") }
        MenuItem { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock layout" : "Lock layout"; onTriggered: root.toggleLayoutLocked() }
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
        id: deckNodeContextMenu
        property var targetNode: ({})
        title: targetNode && targetNode.label ? targetNode.label : "Signal Flow node"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem { text: "Inspect card"; enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.id); onTriggered: root.selectNode(deckNodeContextMenu.targetNode) }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.semantic === "curve"
                ? "Open Curve Editor" : "Open Adaptive Response"
            visible: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "processor"
                && (deckNodeContextMenu.targetNode.semantic === "curve"
                    || deckNodeContextMenu.targetNode.semantic === "adaptive-response"))
            onTriggered: root.openNodeSettings(deckNodeContextMenu.targetNode)
        }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.kind === "input"
                ? "Open input setup" : "Open output setup"
            visible: Boolean(deckNodeContextMenu.targetNode && (deckNodeContextMenu.targetNode.kind === "input"
                || deckNodeContextMenu.targetNode.kind === "output"))
            onTriggered: root.openCardSettings(deckNodeContextMenu.targetNode)
        }
        MenuItem {
            text: deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.pinned ? "Unpin placement" : "Pin placement"
            enabled: Boolean(deckNodeContextMenu.targetNode && deckNodeContextMenu.targetNode.objectId)
            onTriggered: root.toggleNodePinned(deckNodeContextMenu.targetNode)
        }
        MenuItem { text: "Center selection"; enabled: Boolean(root.inspectedRoute && root.inspectedRoute.id); onTriggered: root.focusCurrentSelection() }
        MenuSeparator {}
        MenuItem { text: "Auto-layout unpinned cards"; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onTriggered: root.announce(backendObject.signalFlowAutoLayout(), "Auto-layout was not applied.") }
    }

    Menu {
        id: deckRouteContextMenu
        objectName: "flightDeckSignalFlowRouteContextMenu"
        property var targetRoute: ({})
        title: targetRoute && targetRoute.sourceLabel
            ? targetRoute.sourceLabel + " → " + targetRoute.destinationLabel : "Signal Flow route"
        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border; border.width: 1 }
        MenuItem {
            text: "Insert processing…"
            enabled: root.mode === "configured" && Boolean(deckRouteContextMenu.targetRoute && deckRouteContextMenu.targetRoute.id)
            onTriggered: {
                root.inspectedRoute = deckRouteContextMenu.targetRoute
                root.source = ({})
                root.inspectedNode = ({})
                processorDialog.open()
            }
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
        title: "Resolve analog input collision"
        contentItem: ColumnLayout {
            spacing: deck.space16
            Text { Layout.fillWidth: true; text: "“" + (root.source.label || "Selected source") + "” conflicts with the current route into “" + conflictDialog.label + "”. Replace moves that input; Mixer preserves both inputs with an explicit runtime mode."; color: deck.textPrimary; font.family: deck.bodyFont; wrapMode: Text.WordWrap }
            ComboBox {
                id: deckMixerMode
                Layout.fillWidth: true
                model: ["average", "sum-clamped", "highest-magnitude"]
                font.family: deck.bodyFont
                font.pixelSize: 10
                Accessible.name: "Analog mixer mode"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: conflictDialog.close() }
                DeckButton { text: "Create mixer"; onClicked: { const mode = deckMixerMode.currentText; conflictDialog.close(); root.connectWithMixer(mode) } }
                DeckButton {
                    text: "Replace"
                    emphasized: true
                    onClicked: {
                        conflictDialog.close()
                        root.connect({ "kind": conflictDialog.destination.match(/^\\d+$/) ? "button" : "axis",
                            "index": conflictDialog.destination.match(/^\\d+$/) ? Number(conflictDialog.destination) : 0,
                            "technicalLabel": conflictDialog.destination }, true)
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
            Text { Layout.fillWidth: true; text: "Drag a processor chip onto the highlighted visible wire, or click it to apply the same atomic action to the selected canonical segment. Focused settings stay authoritative."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
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
                        width: Math.max(118, chipLabel.implicitWidth + 20); height: deck.compactControlHeight; radius: deck.radiusControl
                        color: root.processorEnabled(processorKind) ? deck.accent : deck.secondarySurface
                        border.color: chipDrag.active ? deck.attention : deck.border; border.width: 1
                        Text { id: chipLabel; anchors.centerIn: parent; text: root.processorIsShared(processorKind) ? "Split shared " + modelData.label : root.processorEnabled(processorKind) ? "Remove " + modelData.label : "Add " + modelData.label; color: root.processorEnabled(processorKind) ? deck.applicationBackground : deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 10; font.bold: true }
                        Drag.active: chipDrag.active
                        Drag.source: deckProcessorChip
                        Drag.keys: ["signal-flow-processor"]
                        Drag.hotSpot.x: width / 2; Drag.hotSpot.y: height / 2
                        DragHandler { id: chipDrag; enabled: root.mode === "configured" }
                        MouseArea { anchors.fill: parent; onClicked: root.toggleProcessor(processorKind) }
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
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, defaultRows.implicitHeight); clip: true
                Column { id: defaultRows; width: parent.availableWidth; spacing: 4
                    Repeater { model: defaultsDialog.preview.changes || []; delegate: Text { required property var modelData; width: parent.width; text: modelData.source + ": " + modelData.from + " → " + modelData.to + (modelData.blocked ? " · " + modelData.reason : ""); color: modelData.blocked ? deck.attention : deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10; elide: Text.ElideRight } }
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
                    enabled: Boolean(defaultsDialog.preview.success && (defaultsDialog.preview.count || 0) > 0)
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
