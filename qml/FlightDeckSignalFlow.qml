import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

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
    // Rebuild these static indexes only when canonical graph topology changes.
    // The 10 Hz Live sampler updates a tiny lookup map rather than making
    // each painted route scan the complete telemetry list.
    property var routeById: ({})
    property var routeMembershipByInput: ({})
    property var routeMembershipByOutput: ({})
    property var routeLiveById: ({})
    property var wireGeometryByRouteId: ({})
    property var retiringWireGeometry: []
    property real wireReveal: 1.0
    property real wireRetire: 1.0
    property var nodePositions: ({})
    // Presentation-only continuity for source-first learning. The canonical
    // graph remains owned by AppBackend.
    property string learnedSourcePortId: ""
    property var pendingLearnDestination: ({})
    property var dragWire: ({ "active": false, "source": ({}), "x": 0, "y": 0 })
    property var connectionPreview: ({})
    property string pendingProcessorRouteId: ""
    readonly property string semanticDensity: {
        const requested = graph.workspace && graph.workspace.densityMode || "detailed"
        if (zoom <= 0.62) return "overview"
        if (zoom <= 0.82 && requested === "detailed") return "compact"
        return requested
    }
    readonly property int canvasPortLimit: semanticDensity === "overview" ? 12
        : semanticDensity === "compact" ? 18 : 48

    signal navigateRequested(int page, int axis, var state)

    function normalized(value) { return String(value || "").toLowerCase() }
    function node(kind) {
        const nodes = graph.nodes || []
        for (let i = 0; i < nodes.length; ++i) if (nodes[i].kind === kind) return nodes[i]
        return ({})
    }
    function routeForId(id) {
        return routeById[String(id || "")] || ({})
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
    function routesForPort(port, isOutput) {
        const membership = isOutput ? routeMembershipByOutput : routeMembershipByInput
        return membership[String(port && port.id || "")] || []
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
    function groupCollapsed(kind, group) {
        const states = groups(kind)
        let collapsed = false
        for (let i = 0; i < states.length; ++i) if (states[i].group === group) { collapsed = Boolean(states[i].collapsed); break }
        if (collapsed && source && source.kind) {
            const allPorts = ports(kind)
            for (let i = 0; i < allPorts.length; ++i) {
                if (allPorts[i].group === group && (kind === "input"
                        ? allPorts[i].id === source.id : compatible(allPorts[i]))) return false
            }
        }
        return collapsed
    }
    function groupPorts(kind, group, limit) {
        return ports(kind).filter(function(port) { return port.group === group && matching(port, kind === "output") })
            .slice(0, limit || 48)
    }
    function setGroup(kind, group, collapsed) {
        const card = node(kind)
        announce(backendObject.signalFlowSetPortGroupCollapsed(String(card.objectId || card.id || ""), group, collapsed),
            "Port group state was not saved.")
    }
    function sourcePorts() { return ports("input").filter(function(port) { return matching(port, false) && !groupCollapsed("input", port.group) }).slice(0, 48) }
    function destinationPorts() { return ports("output").filter(function(port) { return matching(port, true) && !groupCollapsed("output", port.group) }) }
    function compatible(port) {
        if (!source || !source.kind || !port) return false
        if (source.kind === "axis") return port.kind === "axis"
        if (source.kind === "native-pov") return port.kind === "native-pov"
        return port.kind === "button"
    }
    function routeLive(route) {
        return routeLiveById[String(route && route.id || "")] || ({ "active": false, "value": 0 })
    }
    function rebuildGraphIndexes() {
        const routes = graph.routes || []
        const nextRoutes = ({})
        const nextInputMembership = ({})
        const nextOutputMembership = ({})
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            const id = String(route.id || "")
            if (id.length > 0) nextRoutes[id] = route
            const inputId = String(route.sourcePortId || "")
            const outputId = String(route.destinationPortId || "")
            if (inputId.length > 0) {
                if (!nextInputMembership[inputId]) nextInputMembership[inputId] = []
                nextInputMembership[inputId].push(route)
            }
            if (outputId.length > 0) {
                if (!nextOutputMembership[outputId]) nextOutputMembership[outputId] = []
                nextOutputMembership[outputId].push(route)
            }
        }
        routeById = nextRoutes
        routeMembershipByInput = nextInputMembership
        routeMembershipByOutput = nextOutputMembership
    }
    function rebuildLiveTelemetryIndex() {
        const next = ({})
        const entries = liveTelemetry.routes || []
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index]
            const id = String(entry.id || "")
            if (id.length > 0) next[id] = entry
        }
        routeLiveById = next
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
        dragWire = ({ "active": true, "source": port,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
        return true
    }
    function updateSourceDrag(point) {
        if (!dragWire.active) return
        dragWire = ({ "active": true, "source": dragWire.source,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
    }
    function endSourceDrag() {
        if (!dragWire.active) return
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0 })
        connectionPreview = ({})
    }
    function previewDestination(port) {
        if (!port || !source || !source.id) return
        const isCompatible = compatible(port)
        const existing = routesForPort(port, true)
        const requiresMerge = isCompatible && source.kind === "axis" && existing.length > 0
        connectionPreview = ({ "portId": String(port.id), "compatible": isCompatible,
            "message": source.label + " → " + port.label + (isCompatible
                ? requiresMerge ? " · occupied analog output: choose Replace or Mixer."
                : existing.length > 0 ? " · existing route present."
                : " · compatible direct route."
                : " · incompatible endpoint type.") })
    }
    function clearDestinationPreview(port) {
        if (!port || String(connectionPreview.portId || "") === String(port.id || "")) connectionPreview = ({})
    }
    function previewProcessorTarget(route) {
        if (!route || !route.id || String(route.id) === String(inspectedRoute && inspectedRoute.id || "")) return
        pendingProcessorRouteId = String(route.id)
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
        return live ? 1.0 : 0.82
    }
    function stableLane(value, count) {
        const text = String(value || "")
        let hash = 0
        for (let i = 0; i < text.length; ++i) hash = ((hash << 5) - hash + text.charCodeAt(i)) | 0
        return Math.abs(hash) % Math.max(1, count || 1)
    }
    function nodePosition(nodeData, fallbackX, fallbackY) {
        if (!nodeData) return ({ "x": fallbackX, "y": fallbackY })
        const saved = nodePositions[String(nodeData.objectId || nodeData.id || "")]
        if (saved) return saved
        return ({ "x": Number(nodeData.x === undefined ? fallbackX : nodeData.x),
                  "y": Number(nodeData.y === undefined ? fallbackY : nodeData.y) })
    }
    function noteNodePosition(objectId, x, y) {
        const id = String(objectId || "")
        if (!id || !isFinite(x) || !isFinite(y)) return
        const current = nodePositions[id]
        if (current && Math.abs(current.x - x) < 0.1 && Math.abs(current.y - y) < 0.1) return
        const next = ({})
        for (const key in nodePositions) next[key] = nodePositions[key]
        next[id] = ({ "x": Number(x), "y": Number(y) })
        nodePositions = next
        wireGeometryTimer.restart()
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
            const cardWidth = candidate.kind === "processor" ? 180 : 250
            const cardHeight = candidate.kind === "processor" ? 112 : 142
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
    function rebuildWireGeometry() {
        const routes = graph.routes || []
        const nodes = graph.nodes || []
        const nodeById = ({})
        const bundleCounts = ({})
        const bundleLeads = ({})
        const next = []
        for (let i = 0; i < nodes.length; ++i) nodeById[String(nodes[i].id || "")] = nodes[i]
        for (let i = 0; i < routes.length; ++i) {
            const route = routes[i]
            const key = String(route.sourcePortId || route.id || i) + "|" + String(route.viaNodeId || "direct")
            bundleCounts[key] = Number(bundleCounts[key] || 0) + 1
        }
        for (let i = 0; i < routes.length; ++i) {
            const route = routes[i]
            const input = nodeById[String(route.sourceNodeId || "")]
            const output = nodeById[String(route.destinationNodeId || "")]
            if (!input || !output) continue
            const inputPosition = nodePosition(input, 50, 150)
            const outputPosition = nodePosition(output, 1190, 150)
            const bundleKey = String(route.sourcePortId || route.id || i) + "|" + String(route.viaNodeId || "direct")
            const firstBundleLeg = !bundleLeads[bundleKey]
            bundleLeads[bundleKey] = true
            const excludedIds = ({})
            excludedIds[String(input.id || "")] = true
            excludedIds[String(output.id || "")] = true
            const processorIds = route.processors || []
            const processorWaypoints = []
            for (let processorIndex = 0; processorIndex < processorIds.length; ++processorIndex) {
                const processor = nodeById[String(processorIds[processorIndex] || "")]
                if (!processor) continue
                const position = nodePosition(processor, 430 + processorIndex * 184, 180)
                const processorId = String(processor.id || processor.objectId || "")
                excludedIds[processorId] = true
                processorWaypoints.push({ "id": processorId, "leftX": position.x,
                    "rightX": position.x + 180, "y": position.y + 40 })
            }
            const branchX = bundleCounts[bundleKey] > 1 ? inputPosition.x + 334 : inputPosition.x + 260
            const segments = []
            let currentX = branchX
            let currentY = inputPosition.y + 52 + stableLane(route.sourcePortId, 13) * 6
            for (let processorIndex = 0; processorIndex < processorWaypoints.length; ++processorIndex) {
                const waypoint = processorWaypoints[processorIndex]
                const obstacle = obstaclePlan(currentX, currentY, waypoint.leftX, waypoint.y, excludedIds)
                segments.push({ "startX": currentX, "startY": currentY, "endX": waypoint.leftX,
                    "endY": waypoint.y, "hasDetour": obstacle.hasDetour,
                    "detourY": obstacle.detourY, "underCard": obstacle.underCard })
                currentX = waypoint.rightX
                currentY = waypoint.y
            }
            const finalX = outputPosition.x
            const finalY = outputPosition.y + 52 + stableLane(route.destinationPortId + route.id, 18) * 5
            const finalObstacle = obstaclePlan(currentX, currentY, finalX, finalY, excludedIds)
            segments.push({ "startX": currentX, "startY": currentY, "endX": finalX,
                "endY": finalY, "hasDetour": finalObstacle.hasDetour,
                "detourY": finalObstacle.detourY, "underCard": finalObstacle.underCard })
            const focusSegment = segments[Math.floor(segments.length * 0.5)]
            next.push({
                "route": route,
                "routeId": String(route.id || ""),
                "startX": inputPosition.x + 260,
                "startY": inputPosition.y + 52 + stableLane(route.sourcePortId, 13) * 6,
                "endX": finalX,
                "endY": finalY,
                "segments": segments,
                "focusX": focusSegment ? (focusSegment.startX + focusSegment.endX) * 0.5 : finalX,
                "focusY": focusSegment ? (focusSegment.startY + focusSegment.endY) * 0.5 : finalY,
                "bundleCount": Number(bundleCounts[bundleKey] || 1),
                "drawBundleTrunk": Number(bundleCounts[bundleKey] || 1) > 1 && firstBundleLeg,
                "bundleX": inputPosition.x + 334,
                "processorCount": processorWaypoints.length
            })
        }
        wireGeometry = next
        const indexedGeometry = ({})
        for (let index = 0; index < next.length; ++index)
            indexedGeometry[String(next[index].routeId || "")] = next[index]
        wireGeometryByRouteId = indexedGeometry
        if (diagram) diagram.requestPaint()
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
        const horizontal = Math.max(0.5, Math.min(1.15, (graphViewport.width - 42) / scene.width))
        const vertical = Math.max(0.5, Math.min(1.15, (graphViewport.height - 42) / scene.height))
        zoom = Math.min(horizontal, vertical)
        graphViewport.contentX = 0
        graphViewport.contentY = 0
        saveTimer.restart()
        notice = "Graph fit to the current workspace."
        noticeError = false
    }
    function focusCurrentSelection() {
        const selectedId = inspectedRoute && inspectedRoute.id ? String(inspectedRoute.id) : ""
        const geometry = wireGeometryByRouteId[String(selectedId || "")]
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
        if (port && port.scopeEditable === false) {
            notice = "This saved input is visible for the whole rig but is not the active editing scope. Select its card, then choose Edit this input scope."
            noticeError = true
            return
        }
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
        const result = backendObject.signalFlowConnect(String(source.kind), Number(source.index), Number(source.subIndex || 0), destination, replace, Number(graph.revision || 0))
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
        const result = backendObject.signalFlowConnectWithMixer(String(source.kind), Number(source.index),
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
        const result = backendObject.signalFlowToggleProcessor(String(inspectedRoute.id), kind,
            !processorEnabled(kind), Number(graph.revision || 0))
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
        for (let index = 0; index < nextRoutes.length; ++index)
            nextRouteIds[String(nextRoutes[index].id || "")] = true
        retiringWireGeometry = (wireGeometry || []).filter(function(entry) {
            return entry && entry.routeId && !nextRouteIds[String(entry.routeId)]
        })
        nodePositions = ({})
        rebuildGraphIndexes()
        rebuildWireGeometry()
        restartWireMotion()
    }
    onPresentationStateChanged: {
        presentationRestored = false
        Qt.callLater(restorePresentationState)
    }
    onInspectedRouteChanged: if (diagram) diagram.requestPaint()
    onSourceChanged: if (diagram) diagram.requestPaint()
    onInspectedNodeChanged: if (diagram) diagram.requestPaint()
    onModeChanged: if (diagram) diagram.requestPaint()
    onLiveTelemetryChanged: {
        rebuildLiveTelemetryIndex()
        if (diagram) diagram.requestPaint()
    }
    onSignalFocusChanged: if (diagram) diagram.requestPaint()
    onXrayModeChanged: if (diagram) diagram.requestPaint()
    onReducedMotionChanged: {
        if (reducedMotion) {
            wireReveal = 1
            wireRetire = 1
            retiringWireGeometry = []
        }
        if (diagram) diagram.requestPaint()
    }
    onWireRevealChanged: if (diagram) diagram.requestPaint()
    onWireRetireChanged: if (diagram) diagram.requestPaint()
    onLiveModeChanged: if (diagram) diagram.requestPaint()
    onDragWireChanged: if (diagram) diagram.requestPaint()
    onRouteStateFilterChanged: if (diagram) diagram.requestPaint()
    onQueryChanged: if (diagram) diagram.requestPaint()

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
        rebuildGraphIndexes()
        rebuildLiveTelemetryIndex()
        Qt.callLater(restoreWorkspace)
        Qt.callLater(restorePresentationState)
        Qt.callLater(applyBackendFocus)
        Qt.callLater(rebuildWireGeometry)
    }

    Timer {
        id: liveSampleTimer
        interval: 100
        repeat: true
        running: root.liveMode || root.signalFocus || root.mode === "effective"
        onTriggered: root.liveTelemetry = backendObject.signalFlowLiveTelemetry()
    }

    Timer {
        id: wireGeometryTimer
        interval: 16
        repeat: false
        onTriggered: root.rebuildWireGeometry()
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

    Timer {
        id: processorHysteresis
        interval: 110
        repeat: false
        onTriggered: {
            const routes = root.graph.routes || []
            for (let index = 0; index < routes.length; ++index) {
                if (String(routes[index].id || "") !== root.pendingProcessorRouteId) continue
                root.inspectedRoute = routes[index]
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
                if (drop.source && drop.source.port) {
                    root.connectDragged(drop.source.port, flowPort.port)
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

    ColumnLayout {
        anchors.fill: parent
        spacing: deck.sectionGap

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Signal Flow"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 25; font.bold: true }
                Text { text: "Device Rig: " + (root.graph.deviceRigName || "Profile-local input scope") + " · Profile: " + (root.graph.profileName || "active profile"); color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { visible: Boolean(root.graph.editingDiffersFromEffective); text: "Effective runtime profile: " + (root.graph.effectiveProfileName || "unknown") + " · editing remains on " + (root.graph.profileName || "selected profile"); color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            DeckButton { text: "Profile…"; helpText: "Choose the active profile whose durable Signal Flow topology is being edited."; onClicked: deckProfileContextMenu.open() }
            DeckButton { text: "Device Rig…"; helpText: "Choose the Device Rig context. A multi-device rig stays visible until one input card is selected for editing."; onClicked: deckRigContextMenu.open() }
            DeckButton { text: "Configured"; emphasized: root.mode === "configured"; onClicked: root.mode = "configured" }
            DeckButton { text: "Effective"; emphasized: root.mode === "effective"; onClicked: root.mode = "effective" }
            DeckButton { text: "Undo"; enabled: backendObject.signalFlowCanUndo; onClicked: root.announce(backendObject.signalFlowUndo(Number(root.graph.revision || 0)), "Undo was not applied.") }
            DeckButton { text: "Redo"; enabled: backendObject.signalFlowCanRedo; onClicked: root.announce(backendObject.signalFlowRedo(Number(root.graph.revision || 0)), "Redo was not applied.") }
        }

        FlightDeckCard {
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: commandBar.implicitHeight + deck.cardPaddingCompact * 2
            contentPadding: deck.cardPaddingCompact
            color: deck.elevatedSurface
            RowLayout {
                id: commandBar
                anchors.fill: parent
                spacing: deck.space8
                TextField {
                    id: deckSearch
                    Layout.preferredWidth: 220
                    Layout.fillWidth: root.width < 1120
                    implicitHeight: deck.compactControlHeight
                    placeholderText: "Search ports"
                    color: deck.textPrimary
                    placeholderTextColor: deck.textMuted
                    selectByMouse: true
                    onTextChanged: root.query = text
                    background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: deck.border }
                }
                DeckButton { visible: root.query.length > 0; text: "Focus result" + (root.searchResultCount() > 1 ? " (" + root.searchResultCount() + ")" : ""); helpText: "Center the first matching route or source port and preserve the rest of the graph context."; onClicked: root.focusSearchResult() }
                Repeater {
                    model: [ { label: "All", key: "all" }, { label: "Axes", key: "axis" }, { label: "Buttons", key: "button" }, { label: "POV", key: "pov" }, { label: "Native POV", key: "native-pov" } ]
                    delegate: DeckButton { required property var modelData; text: modelData.label; emphasized: root.filter === modelData.key; onClicked: root.filter = modelData.key }
                }
                DeckButton { text: "State: " + (root.routeStateFilter === "all" ? "All" : root.routeStateFilter); helpText: "Filter ports and routes by mapped state, problems, or current activity."; onClicked: deckStateFilterMenu.open() }
                Item { Layout.fillWidth: true }
                DeckButton { text: root.signalFocus ? "Focus: on" : "Signal focus"; emphasized: root.signalFocus; helpText: "Dim inactive routes and sample bounded live telemetry."; onClicked: { root.signalFocus = !root.signalFocus; if (root.signalFocus) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
                DeckButton { text: root.liveMode ? "Live: on" : "Live"; emphasized: root.liveMode; helpText: "Sample bounded latest telemetry and illuminate active routes without affecting MappingWorker."; onClicked: { root.liveMode = !root.liveMode; if (root.liveMode) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
                DeckButton { text: root.xrayMode ? "X-ray: on" : "X-ray"; emphasized: root.xrayMode; helpText: "Isolate topology related to the selected route or source without changing mapping."; onClicked: root.keyboardAction("xray") }
                DeckButton { text: "Learn input"; helpText: "Move or press a physical control first, then choose one of the highlighted compatible destinations."; enabled: root.mode === "configured" && root.graph.editable; onClicked: root.startSignalFlowSourceLearning() }
                DeckButton { text: "Learn destination"; helpText: "Choose a virtual destination first, then move the physical control that should drive it."; enabled: root.mode === "configured" && root.graph.editable; onClicked: learnDialog.open() }
                DeckButton { text: "Alias"; helpText: "Assign a profile-local friendly name without changing the vJoy target."; enabled: root.mode === "configured" && root.graph.editable; onClicked: aliasDialog.open() }
                DeckButton { text: "Fit"; helpText: "Fit the current workspace into the graph viewport. Shortcut: Home."; onClicked: root.keyboardAction("fit") }
                DeckButton { text: "Auto-layout"; helpText: "Arrange unpinned cards with the stable bounded layout."; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onClicked: root.announce(backendObject.signalFlowAutoLayout(), "Auto-layout was not applied.") }
                DeckButton { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock layout" : "Lock layout"; helpText: "Prevent accidental card dragging while preserving each saved placement."; onClicked: root.toggleLayoutLocked() }
                DeckButton { text: "Density: " + ((root.graph.workspace && root.graph.workspace.densityMode) || "detailed"); helpText: "Cycle detailed, compact, and overview port density. Shortcut: D."; onClicked: root.cycleDensityMode() }
                DeckButton { text: "Wires: " + ((root.graph.workspace && root.graph.workspace.wireStyle) || "smooth"); helpText: "Switch between smooth and orthogonal cached wire rendering."; onClicked: root.toggleWireStyle() }
                DeckButton { text: root.reducedMotion ? "Reduced" : "Motion"; emphasized: root.reducedMotion; helpText: "Disable movement animation while keeping topology changes visible."; onClicked: root.reducedMotion = !root.reducedMotion }
                DeckButton { text: "Default map"; helpText: "Preview deterministic mappings for unassigned routes before applying them."; enabled: root.mode === "configured" && root.graph.editable; onClicked: root.preview("unassigned") }
                DeckButton { text: "Replace all"; destructive: true; helpText: "Preview every replacement before rewriting current routes."; enabled: root.mode === "configured" && root.graph.editable; onClicked: root.preview("replace-all") }
                DeckButton { text: "−"; Accessible.name: "Zoom out"; helpText: "Zoom out. Shortcut: Ctrl+-"; onClicked: root.keyboardAction("zoom-out") }
                Text { text: Math.round(root.zoom * 100) + "%"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9 }
                DeckButton { text: "+"; Accessible.name: "Zoom in"; helpText: "Zoom in. Shortcut: Ctrl++"; onClicked: root.keyboardAction("zoom-in") }
            }
        }

        FlightDeckCard {
            tokens: deck
            visible: root.notice.length > 0 || root.mode === "effective"
            Layout.fillWidth: true
            implicitHeight: alertText.implicitHeight + deck.cardPaddingCompact * 2
            contentPadding: deck.cardPaddingCompact
            color: root.noticeError ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.12) : root.mode === "effective" ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.10) : Qt.rgba(deck.healthy.r, deck.healthy.g, deck.healthy.b, 0.10)
            border.color: root.noticeError ? deck.fault : root.mode === "effective" ? deck.attention : deck.healthy
            Text { id: alertText; anchors.fill: parent; text: root.notice.length > 0 ? root.notice : root.graph.effectiveSummary; color: root.noticeError ? deck.fault : deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; wrapMode: Text.WordWrap }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: deck.sectionGap

            FlightDeckCard {
                tokens: deck
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
                        width: 1560
                        height: 850
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
                                    context.beginPath()
                                    context.moveTo(startX, startY)
                                    if (root.graph.workspace && root.graph.workspace.wireStyle === "orthogonal") {
                                        if (detour) {
                                            const stub = Math.max(42, Math.min(120, Math.abs(endX - startX) * 0.22))
                                            context.lineTo(startX + direction * stub, startY)
                                            context.lineTo(startX + direction * stub, detourY)
                                            context.lineTo(endX - direction * stub, detourY)
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
                                            startX + direction * 118, detourY, middleX, detourY)
                                        context.bezierCurveTo(endX - direction * 118, detourY,
                                            endX - direction * 86, endY, endX, endY)
                                    } else {
                                        const offset = 178 + lane * 10
                                        context.bezierCurveTo(startX + offset, startY, endX - offset, endY, endX, endY)
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
                            onPaint: {
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
                                            root.stableLane(retired.routeId, 5) - 2, true, 1 - root.wireRetire,
                                            segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                const geometry = root.wireGeometry || []
                                for (let i = 0; i < geometry.length; ++i) {
                                    const entry = geometry[i]
                                    const route = entry.route
                                    if (root.mode === "effective" && !route.effective) continue
                                    const live = root.routeIsLive(route)
                                    const color = root.inspectedRoute && root.inspectedRoute.id === route.id
                                        ? deck.attention : route.effective ? deck.healthy : deck.accent
                                    const lineWidth = root.inspectedRoute && root.inspectedRoute.id === route.id ? 4 : live ? 2.75 : 2
                                    const alpha = root.routeVisualAlpha(route, live)
                                    const dashed = root.routeHasProblem(route)
                                    if (entry.drawBundleTrunk) {
                                        const trunkEndX = entry.bundleX
                                        drawWire(context, entry.startX, entry.startY, trunkEndX, entry.startY, color,
                                            Math.min(5.0, lineWidth + entry.bundleCount * 0.35), alpha, 0, true,
                                            root.wireReveal, false, 0, false)
                                        context.save()
                                        context.globalAlpha = alpha * root.wireReveal
                                        context.fillStyle = deck.textMuted
                                        context.font = "bold 9px sans-serif"
                                        context.fillText(String(entry.bundleCount) + "×", trunkEndX + 5, entry.startY - 5)
                                        context.restore()
                                    }
                                    const segments = entry.segments || []
                                    for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                                        const segment = segments[segmentIndex]
                                        drawWire(context, segment.startX, segment.startY, segment.endX, segment.endY,
                                            color, lineWidth, alpha, root.stableLane(route.id, 5) - 2, dashed,
                                            root.wireReveal, segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                if (root.dragWire && root.dragWire.active) {
                                    const input = root.node("input")
                                    const inputPosition = root.nodePosition(input, 50, 150)
                                    const sourceId = String(root.dragWire.source && root.dragWire.source.id || "")
                                    const startX = inputPosition.x + 260
                                    const startY = inputPosition.y + 52 + root.stableLane(sourceId, 13) * 6
                                    drawWire(context, startX, startY, Number(root.dragWire.x || startX),
                                        Number(root.dragWire.y || startY), deck.attention, 2.5, 0.94, 0, true,
                                        1, false, 0, false)
                                    context.save()
                                    context.fillStyle = deck.attention
                                    context.globalAlpha = 0.96
                                    context.beginPath()
                                    context.arc(Number(root.dragWire.x || startX), Number(root.dragWire.y || startY), 5, 0, Math.PI * 2)
                                    context.fill()
                                    context.restore()
                                }
                            }
                        }
                        FlightDeckCard {
                            id: inputNode
                            tokens: deck
                            readonly property var nodeData: root.node("input")
                            x: Number(nodeData.x || 50)
                            y: Number(nodeData.y || 150)
                            onXChanged: root.noteNodePosition(nodeData.objectId, x, y)
                            onYChanged: root.noteNodePosition(nodeData.objectId, x, y)
                            Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            width: 250
                            height: 118
                            z: 2
                            opacity: root.xrayMode ? 0.58 : 1.0
                            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: deck.secondarySurface
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { text: inputNode.nodeData.label || "Input context"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: inputNode.nodeData.detail || "Physical source"; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                Text { text: "PORTS " + root.sourcePorts().length; color: deck.informational; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            }
                            MouseArea {
                                anchors.fill: parent
                                drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? inputNode : null
                                drag.axis: Drag.XAndYAxis
                                onReleased: { if (drag.active) root.saveNodePlacement(inputNode.nodeData, inputNode.x, inputNode.y, Boolean(inputNode.nodeData.pinned)); saveTimer.restart() }
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
                                x: Number(modelData.x || 50)
                                y: Number(modelData.y || 390)
                                onXChanged: root.noteNodePosition(modelData.objectId, x, y)
                                onYChanged: root.noteNodePosition(modelData.objectId, x, y)
                                Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                width: 250
                                height: 118
                                z: 2
                                opacity: root.xrayMode ? 0.58 : 1.0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: modelData.missingReference ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.12) : deck.secondarySurface
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                    Text { text: modelData.label || "Saved input"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.detail || "Saved Device Rig member"; color: modelData.missingReference ? deck.attention : deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                    Text { text: modelData.connected ? "SAVED INPUT · READY" : modelData.missingReference ? "MISSING REFERENCE" : "SAVED INPUT · OFFLINE"; color: modelData.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? secondaryInputNode : null
                                    drag.axis: Drag.XAndYAxis
                                    onReleased: { if (drag.active) root.saveNodePlacement(modelData, secondaryInputNode.x, secondaryInputNode.y, Boolean(modelData.pinned)); saveTimer.restart() }
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
                                x: Number(modelData.x || 540)
                                y: Number(modelData.y || 170)
                                onXChanged: root.noteNodePosition(modelData.objectId, x, y)
                                onYChanged: root.noteNodePosition(modelData.objectId, x, y)
                                Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                                width: 180
                                height: 88 + Math.max(0, Number(modelData.sharedChannelCount || 0) - 1) * 20
                                z: 2
                                opacity: root.xrayMode ? 0.58 : 1.0
                                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                                Behavior on height { NumberAnimation { duration: root.reducedMotion ? 0 : 190; easing.type: Easing.OutCubic } }
                                contentPadding: deck.cardPaddingCompact
                                color: deck.elevatedSurface
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: 4
                                    Text { text: modelData.label; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.detail; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                    Text { visible: Boolean(modelData.shared); text: "SHARED · " + Number(modelData.sharedChannelCount || 0) + " CHANNELS"; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; Layout.fillWidth: true }
                                    Repeater {
                                        model: modelData.shared ? Math.min(4, Number(modelData.sharedChannelCount || 0)) : 0
                                        delegate: Row {
                                            spacing: 3
                                            Rectangle { width: 5; height: 5; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: deck.informational }
                                            Text { text: "channel " + (index + 1) + "  →"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8 }
                                            Rectangle { width: 5; height: 5; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: deck.healthy }
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? processorNode : null
                                    drag.axis: Drag.XAndYAxis
                                    onReleased: { if (drag.active) root.saveNodePlacement(modelData, processorNode.x, processorNode.y, Boolean(modelData.pinned)); saveTimer.restart() }
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
                            x: Number(nodeData.x || 1190)
                            y: Number(nodeData.y || 150)
                            onXChanged: root.noteNodePosition(nodeData.objectId, x, y)
                            onYChanged: root.noteNodePosition(nodeData.objectId, x, y)
                            Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 160; easing.type: Easing.OutCubic } }
                            width: 250
                            height: 118
                            z: 2
                            opacity: root.xrayMode ? 0.58 : 1.0
                            Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
                            contentPadding: deck.cardPaddingCompact
                            color: deck.secondarySurface
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { text: outputNode.nodeData.label || "Virtual output"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: outputNode.nodeData.detail || "vJoy destination"; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                Text { text: "DESTINATIONS " + root.destinationPorts().length; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            }
                            MouseArea {
                                anchors.fill: parent
                                drag.target: root.mode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? outputNode : null
                                drag.axis: Drag.XAndYAxis
                                onReleased: { if (drag.active) root.saveNodePlacement(outputNode.nodeData, outputNode.x, outputNode.y, Boolean(outputNode.nodeData.pinned)); saveTimer.restart() }
                                onClicked: function(mouse) { if (!drag.active) root.selectNode(outputNode.nodeData) }
                                onDoubleClicked: function(mouse) { if (!drag.active) root.openCardSettings(outputNode.nodeData) }
                            }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(eventPoint, button) { deckNodeContextMenu.targetNode = outputNode.nodeData; deckNodeContextMenu.open() } }
                        }
                        Column {
                            x: 470; y: 660; width: 620; spacing: 4
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
                                        onEntered: function(drag) { root.previewProcessorTarget(modelData) }
                                        onExited: function(drag) { if (root.pendingProcessorRouteId === String(modelData.id || "")) root.pendingProcessorRouteId = "" }
                                        onDropped: function(drop) {
                                            if (drop.source && drop.source.processorKind) {
                                                root.inspectedRoute = modelData
                                                root.inspectedNode = ({})
                                                const result = backendObject.signalFlowToggleProcessor(String(modelData.id),
                                                    String(drop.source.processorKind), true, Number(root.graph.revision || 0))
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
                Text { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: deck.space12; text: root.connectionPreview && root.connectionPreview.message ? root.connectionPreview.message : "Click-click or drag to route · drop a processor chip on a route lane · right-click a wire for processing · right-click a card to pin · Delete disconnects"; color: deck.graphLabel; font.family: deck.bodyFont; font.pixelSize: 9 }
            }

            FlightDeckCard {
                tokens: deck
                Layout.preferredWidth: Math.max(220, Math.min(305, root.width * 0.25))
                Layout.fillHeight: true
                contentPadding: deck.cardPaddingTechnical
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: deck.space8
                    Text { text: root.inspectedNode && root.inspectedNode.id ? "CARD BUS" : "DESTINATION BUS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Text { text: root.inspectedRoute && root.inspectedRoute.id ? root.inspectedRoute.sourceLabel + " → " + root.inspectedRoute.destinationLabel : root.inspectedNode && root.inspectedNode.id ? root.inspectedNode.label : "Select a route or destination"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); text: String(root.inspectedNode.detail || "") + (root.inspectedNode.capabilitySummary ? "\n" + root.inspectedNode.capabilitySummary : ""); color: root.inspectedNode && root.inspectedNode.missingReference ? deck.attention : deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedNode && root.inspectedNode.id); text: Number(root.inspectedNode.routeCount || 0) + " configured route" + (Number(root.inspectedNode.routeCount || 0) === 1 ? "" : "s") + (root.inspectedNode.connected ? " · READY" : " · OFFLINE / UNAVAILABLE"); color: root.inspectedNode && root.inspectedNode.connected ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    DeckButton { visible: Boolean(root.inspectedNode && root.inspectedNode.kind === "input" && root.inspectedNode.scopeEditable === false && root.inspectedNode.controllerRecordId); text: "Edit this input scope"; helpText: "Make this saved Device Rig member the explicit input scope before changing its routes."; Layout.fillWidth: true; onClicked: root.useInputScope(root.inspectedNode) }
                    DeckButton { visible: Boolean(root.inspectedNode && (root.inspectedNode.kind === "input" || root.inspectedNode.kind === "output")); text: root.inspectedNode && root.inspectedNode.kind === "input" ? "Open input setup" : "Open output setup"; helpText: "Open Devices & setup while preserving this Flight Deck Signal Flow card selection for return."; Layout.fillWidth: true; onClicked: root.openCardSettings(root.inspectedNode) }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: root.inspectedRoute && root.inspectedRoute.processors && root.inspectedRoute.processors.length ? "Conditioning nodes: " + root.inspectedRoute.processors.length + (root.inspectedRoute.viaNodeId ? " · explicit mixer" : "") : "Direct route — no visible conditioning node."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.processorDetail("adaptive-response").settingsSummary); text: "ADAPTIVE RESPONSE · " + String(root.processorDetail("adaptive-response").settingsSummary); color: deck.graphLabel; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: String(root.inspectedRoute.health || "ready").toUpperCase().replace("-", " ") + " · " + String(root.inspectedRoute.healthDetail || ""); color: root.inspectedRoute.health === "ready" ? deck.healthy : root.inspectedRoute.health === "conflict" ? deck.danger : deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id && (root.signalFocus || root.liveMode)); text: "LIVE SAMPLE " + Number(root.routeLive(root.inspectedRoute).value || 0).toFixed(3) + (root.routeIsLive(root.inspectedRoute) ? " · MOVING" : " · STEADY"); color: root.routeIsLive(root.inspectedRoute) ? deck.healthy : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    DeckButton { text: "Explain route"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); Layout.fillWidth: true; onClicked: root.explainRoute() }
                    DeckButton { text: "Open Curve Editor"; visible: Boolean(root.inspectedRoute && root.routeHasProcessor(root.inspectedRoute, "curve")); helpText: "Open the authoritative Curve Editor for this source axis and preserve this Flight Deck Signal Flow selection for return."; Layout.fillWidth: true; onClicked: root.openFullSettings("curve", root.inspectedRoute) }
                    DeckButton { text: "Open Adaptive Response"; visible: Boolean(root.inspectedRoute && root.routeHasProcessor(root.inspectedRoute, "adaptive-response")); helpText: "Open the authoritative Adaptive Response editor for this source axis and preserve this Flight Deck Signal Flow selection for return."; Layout.fillWidth: true; onClicked: root.openFullSettings("adaptive-response", root.inspectedRoute) }
                    DeckButton { text: "Processor palette"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: processorDialog.open() }
                    DeckButton { text: "Share active processor…"; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.openShareProcessorDialog() }
                    DeckButton { text: "Disconnect selected"; destructive: true; helpText: "Remove this canonical route. Shortcut: Delete."; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    ScrollView {
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

    Timer { id: saveTimer; interval: 650; repeat: false; onTriggered: root.saveWorkspace() }

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
            Text { Layout.fillWidth: true; text: "Drag a processor chip onto a route lane, or click it to atomically add/remove it from the selected source chain. The same source setting is visible in its focused editor."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; wrapMode: Text.WordWrap }
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
