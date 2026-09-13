import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Window 6.5

// Signal Flow is deliberately a control-plane editor.  Its graph is a
// projection of AppBackend's persisted mapping state; it neither samples HID
// reports nor maintains a second mapping model in QML.
Item {
    id: root
    objectName: "signalFlowPage"
    anchors.fill: parent
    focus: true

    required property var backendObject
    required property var themeTokens
    property bool legacy: false
    // The page host owns this small transient state when a dedicated editor
    // is opened.  It is deliberately separate from the durable workspace:
    // returning from Curves or Adaptive Response restores the operator's
    // selection/focus without making a graph-only configuration store.
    property var presentationState: ({})
    property bool presentationRestored: false
    property var graph: backendObject ? backendObject.signalFlowGraph : ({})
    property var selectedSource: ({})
    property var selectedRoute: ({})
    // Card selection is intentionally separate from route selection.  A
    // physical input, virtual output, or processor is never decorative: a
    // click opens its truthful graph-side detail in the same inspector.
    property var selectedNode: ({})
    property string viewMode: "configured"
    property string portFilter: "all"
    // Type and state filtering deliberately remain independent so an operator
    // can, for example, inspect only problem axes without losing the rest of
    // the graph's spatial context.
    property string routeStateFilter: "all"
    property string searchText: ""
    property string feedback: ""
    property bool feedbackError: false
    property real zoom: 1.0
    property bool workspaceRestored: false
    property bool workspaceDirty: false
    // Live/Focus samples a fixed worker snapshot on a UI timer. It never
    // subscribes the graph to report-rate changes or asks the mapper to walk
    // topology while processing DirectInput.
    property var liveTelemetry: ({})
    property bool liveMode: false
    property bool signalFocus: false
    property bool reducedMotion: false
    property var routeExplanation: ({})
    property bool xrayMode: false
    // Geometry is prepared at graph/layout boundaries only. Live telemetry
    // updates repaint styling against this cache and never recalculate paths.
    property var wireGeometry: []
    // Retired geometry exists only long enough to explain a disconnect. It is
    // never persisted and never participates in route hit testing.
    property var retiringWireGeometry: []
    property real wireReveal: 1.0
    property real wireRetire: 1.0
    property var appearingWireRouteIds: ({})
    property real wireAppear: 1.0
    property var nodePositions: ({})
    // Snapping is deliberately a transient layout aid.  The card keeps its
    // exact pointer position while pressed; this object only describes the
    // restrained proposal that may be applied after release.
    property var nodeSnapPreview: ({})
    property string snapDragNodeId: ""
    property bool snapDragAltBypass: false
    property var snapSettlingNodeIds: ({})
    readonly property bool snapToGridEnabled: !graph.workspace || graph.workspace.snapToGrid !== false
    readonly property real snapGridSize: 40
    readonly property real snapGridThreshold: 10
    readonly property real alignmentSnapThreshold: 10
    readonly property real alignmentSnapHysteresis: 4
    readonly property int snapSettleDuration: 100
    property var alignmentGuideHysteresis: ({})
    // The non-Flight Deck experiences share the same motion semantics while
    // retaining their own visual identity and component hierarchy.
    readonly property int motionFastDuration: reducedMotion ? 50 : 100
    readonly property int motionStructuralDuration: reducedMotion ? 80 : 190
    readonly property int motionLayoutDuration: reducedMotion ? 120 : 320
    property bool autoLayoutMotionActive: false
    // A source-first learn gesture owns this short-lived visual state only.
    // It is not stored in the canonical topology or in the workspace.
    property string learnedSourcePortId: ""
    property var pendingLearnDestination: ({})
    // One presentation-only routing gesture is shared by the four non-Flight
    // Deck themes. It deliberately captures graph context but never stores a
    // graph-local mapping or compatibility decision.
    property var interaction: ({ "mode": "IDLE", "source": ({}), "target": ({}),
        "expectedRevision": 0, "profileId": "", "rigId": "", "token": 0 })
    property int interactionToken: 0
    property bool connectionCommitInFlight: false
    readonly property bool routingActive: Boolean(interaction && interaction.mode
        && interaction.mode !== "IDLE")
    // A drag wire and its hover preview are presentation-only.  The source
    // selection and eventual route still go through the same endpoint command
    // as Flight Deck.
    property var dragWire: ({ "active": false, "source": ({}), "x": 0, "y": 0 })
    property bool sourceDragDropHandled: false
    property var connectionPreview: ({})
    property string pendingProcessorRouteId: ""
    property string pendingProcessorSegmentId: ""
    // A processor command always names the durable segment painted beneath the
    // pointer.  This remains presentation state, never an alternate topology.
    property string selectedSegmentId: ""
    readonly property string semanticDensity: {
        const requested = graph.workspace && graph.workspace.densityMode || "detailed"
        if (zoom <= 0.62) return "overview"
        if (zoom <= 0.82 && requested === "detailed") return "compact"
        return requested
    }
    readonly property int canvasPortLimit: semanticDensity === "overview" ? 12
        : semanticDensity === "compact" ? 18 : 48

    readonly property color pageBackground: themeTokens.background || "#0d1013"
    readonly property color panel: themeTokens.panel || "#1a1d23"
    readonly property color raisedPanel: themeTokens.panelRaised || "#20282d"
    readonly property color insetPanel: themeTokens.panelInset || "#10171b"
    readonly property color control: themeTokens.control || "#10171b"
    readonly property color controlHover: themeTokens.controlHover || "#142128"
    readonly property color border: themeTokens.border || "#435660"
    readonly property color borderStrong: themeTokens.borderStrong || "#78aab9"
    readonly property color textStrong: themeTokens.textStrong || "#f3f7f7"
    readonly property color text: themeTokens.text || "#e8eeee"
    readonly property color textMuted: themeTokens.textMuted || "#9aa3a7"
    readonly property color ready: themeTokens.ready || "#8fd5c9"
    readonly property color warning: themeTokens.warning || "#d4ad69"
    readonly property color danger: themeTokens.danger || "#ca9090"
    readonly property color graphBackground: themeTokens.graphBackground || "#0a0f12"
    readonly property color graphGrid: themeTokens.graphGrid || "#254653"
    readonly property color graphInput: themeTokens.graphInput || "#dbe7e8"
    readonly property color graphOutput: themeTokens.graphOutput || "#8fc8c0"
    readonly property color graphPreview: themeTokens.graphPreview || "#377da3"
    readonly property color graphLabel: themeTokens.graphLabel || "#76909a"
    readonly property color graphFrame: themeTokens.graphFrame || "#4b7081"

    // A host signal keeps Signal Flow independent of a particular shell while
    // still allowing actual deep links into the authoritative focused editors.
    signal navigateRequested(int page, int axis, var state)

    function nodeFor(kind) {
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            if (nodes[index].kind === kind)
                return nodes[index]
        }
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
    function routeForProcessorNode(node) {
        if (!node || String(node.kind || "") !== "processor") return ({})
        const processorId = String(node.objectId || "")
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
            "selectedRouteId": String(selectedRoute && selectedRoute.id || ""),
            "selectedNodeId": String(selectedNode && selectedNode.id || ""),
            "selectedSourceId": String(selectedSource && selectedSource.id || ""),
            "viewMode": viewMode,
            "portFilter": portFilter,
            "routeStateFilter": routeStateFilter,
            "searchText": searchText,
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
        viewMode = presentationState.viewMode === "effective" ? "effective" : "configured"
        portFilter = presentationState.portFilter || "all"
        routeStateFilter = presentationState.routeStateFilter || "all"
        searchText = presentationState.searchText || ""
        signalFocus = Boolean(presentationState.signalFocus)
        liveMode = Boolean(presentationState.liveMode)
        xrayMode = Boolean(presentationState.xrayMode)
        reducedMotion = Boolean(presentationState.reducedMotion)
        if (isFinite(Number(presentationState.zoom))) zoom = Number(presentationState.zoom)
        if (graphViewport) {
            graphViewport.contentX = Math.max(0, Number(presentationState.panX || 0))
            graphViewport.contentY = Math.max(0, Number(presentationState.panY || 0))
        }
        const restoredRoute = routeForId(presentationState.selectedRouteId)
        if (restoredRoute && restoredRoute.id) {
            selectedRoute = restoredRoute
            selectedSource = ({})
            selectedNode = ({})
            feedback = "Returned to the selected Signal Flow route."
            feedbackError = false
            return true
        }
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            if (String(nodes[index].id || "") === String(presentationState.selectedNodeId || "")) {
                selectedNode = nodes[index]
                selectedSource = ({})
                selectedRoute = ({})
                return true
            }
        }
        const ports = inputPorts()
        for (let index = 0; index < ports.length; ++index) {
            if (String(ports[index].id || "") === String(presentationState.selectedSourceId || "")) {
                selectedSource = ports[index]
                selectedRoute = ({})
                selectedNode = ({})
                return true
            }
        }
        return true
    }
    function openFullSettings(semantic, routeCandidate) {
        const route = routeCandidate && routeCandidate.id ? routeCandidate
            : selectedRoute && selectedRoute.id ? selectedRoute
            : routeForProcessorNode(selectedNode)
        const axis = axisForRoute(route)
        if (axis < 0 || axis >= 8) {
            feedback = "Full settings are available only for an axis route. Select the Curve or Adaptive Response processor again."
            feedbackError = true
            return false
        }
        const normalized = String(semantic || "").toLowerCase()
        const page = normalized === "curve" ? 6 : normalized === "adaptive-response" ? 9 : 0
        backendObject.setSelectedAxis(axis)
        const state = capturePresentationState()
        feedback = normalized === "adaptive-response"
            ? "Opening Adaptive Response for this same source axis."
            : normalized === "curve" ? "Opening Curve Editor for this same source axis."
            : "Opening the focused axis settings for this source."
        feedbackError = false
        navigateRequested(page, axis, state)
        return true
    }
    function openNodeSettings(node) {
        if (!node || node.kind !== "processor") return false
        if (node.semantic !== "curve" && node.semantic !== "adaptive-response") {
            feedback = "This processor is summarized here. Its focused settings remain on the Axes page."
            feedbackError = false
            return false
        }
        return openFullSettings(node.semantic, routeForProcessorNode(node))
    }
    function openCardSettings(node) {
        if (!node || (node.kind !== "input" && node.kind !== "output")) return false
        const state = capturePresentationState()
        feedback = node.kind === "input"
            ? "Opening Devices & setup for this physical input context."
            : "Opening Devices & setup for this virtual output context."
        feedbackError = false
        navigateRequested(10, -1, state)
        return true
    }
    function inputPorts() { return nodeFor("input").ports || [] }
    function outputPorts() { return nodeFor("output").ports || [] }
    function normalized(value) { return String(value || "").toLowerCase() }
    function routesForPort(port, output) {
        const matches = []
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            if ((output ? route.destinationPortId : route.sourcePortId) === port.id)
                matches.push(route)
        }
        return matches
    }
    function routeHasProblem(route) { return String(route.health || "ready") !== "ready" }
    function portMatchesState(port, output, state) {
        const normalizedState = String(state || routeStateFilter || "all")
        if (normalizedState === "all") return true
        const routes = routesForPort(port, output)
        if (normalizedState === "mapped") return Boolean(port.mapped)
        if (normalizedState === "unmapped") return !port.mapped
        if (normalizedState === "problems") return routes.some(function(route) { return root.routeHasProblem(route) })
        if (normalizedState === "active") return routes.some(function(route) { return root.routeIsLive(route) })
        return true
    }
    function semanticQueryMatchesPort(port, output, query) {
        if (query === "mapped" || query === "unmapped" || query === "problems" || query === "active")
            return portMatchesState(port, output, query)
        return false
    }
    function portVisible(port, output) {
        const filter = normalized(portFilter)
        const query = normalized(searchText)
        if (filter !== "all" && port.kind !== filter)
            return false
        if (!portMatchesState(port, output))
            return false
        if (query.length > 0 && !semanticQueryMatchesPort(port, output, query)
                && normalized(port.label).indexOf(query) < 0
                && normalized(port.technicalLabel).indexOf(query) < 0)
            return false
        return true
    }
    function destinationFor(port) {
        if (port.kind === "axis") return String(port.technicalLabel)
        if (port.kind === "native-pov") return "native-pov:" + String(port.subIndex) + ":" + String(port.index)
        return String(port.index)
    }
    function portGroups(kind) {
        const node = nodeFor(kind)
        return node.portGroups || []
    }
    function groupCollapsed(kind, group) {
        const node = nodeFor(kind)
        const groups = node.portGroups || []
        let collapsed = false
        for (let index = 0; index < groups.length; ++index) {
            if (groups[index].group === group) { collapsed = Boolean(groups[index].collapsed); break }
        }
        // A persistent collapsed preference remains intact, but the group
        // temporarily opens while the active connection gesture can use it.
        if (collapsed && selectedSource && selectedSource.kind) {
            const ports = node.ports || []
            for (let index = 0; index < ports.length; ++index) {
                if (ports[index].group === group && (kind === "input"
                        ? ports[index].id === selectedSource.id : sourceIsCompatible(ports[index])))
                    return false
            }
        }
        return collapsed
    }
    function portsForGroup(kind, group, limit) {
        const ports = (nodeFor(kind).ports || []).filter(function(port) {
            return port.group === group && portVisible(port, kind === "output")
        })
        return ports.slice(0, limit || 48)
    }
    function portVisibleInNode(port, output) {
        return portVisible(port, output) && !groupCollapsed(output ? "output" : "input", port.group)
    }
    function setGroupCollapsed(kind, group, collapsed) {
        const node = nodeFor(kind)
        const result = backendObject.signalFlowSetPortGroupCollapsed(String(node.objectId || node.id || ""), group, collapsed)
        showResult(result, "Port group state was not saved.")
    }
    function routeLive(route) {
        const routes = liveTelemetry.routes || []
        for (let index = 0; index < routes.length; ++index)
            if (routes[index].id === route.id) return routes[index]
        return ({ "active": false, "value": 0 })
    }
    function routeIsLive(route) { return Boolean(routeLive(route).active) }
    function routeMatchesSearch(route) {
        const query = normalized(searchText)
        if (query.length === 0) return true
        if (query === "mapped") return Boolean(route.enabled)
        if (query === "unmapped") return false
        if (query === "problems") return routeHasProblem(route)
        if (query === "active") return routeIsLive(route)
        const processorText = (route.processors || []).join(" ")
        const text = normalized(String(route.sourceLabel || "") + " "
            + String(route.destinationLabel || "") + " " + String(route.health || "")
            + " " + processorText)
        return text.indexOf(query) >= 0
    }
    function routeMatchesState(route) {
        if (routeStateFilter === "all") return true
        if (routeStateFilter === "mapped") return Boolean(route.enabled)
        if (routeStateFilter === "unmapped") return !route.enabled
        if (routeStateFilter === "problems") return routeHasProblem(route)
        if (routeStateFilter === "active") return routeIsLive(route)
        return true
    }
    function searchResultCount() {
        const routes = (graph.routes || []).filter(function(route) { return routeMatchesSearch(route) })
        if (routes.length > 0) return routes.length
        const ports = inputPorts().concat(outputPorts())
        return ports.filter(function(port) { return portVisible(port, false) || portVisible(port, true) }).length
    }
    function focusSearchResult() {
        const routes = (graph.routes || []).filter(function(route) { return routeMatchesSearch(route) })
        if (routes.length > 0) {
            selectedRoute = routes[0]
            selectedSource = ({})
            selectedNode = ({})
            focusCurrentSelection()
            feedback = "Focused the first matching route. Refine search or use filters to narrow results."
            feedbackError = false
            return true
        }
        const inputs = inputPorts().filter(function(port) { return portVisible(port, false) })
        if (inputs.length > 0) {
            chooseSource(inputs[0])
            focusSourcePort(inputs[0])
            feedback = "Focused the first matching input port."
            feedbackError = false
            return true
        }
        feedback = "No Signal Flow result matches this search."
        feedbackError = true
        return false
    }
    function routeIsXrayRelated(route) {
        if (!xrayMode) return true
        if (selectedSource && selectedSource.id)
            return String(route.sourcePortId || "") === String(selectedSource.id)
        if (!selectedRoute || !selectedRoute.id) return true
        return String(route.id) === String(selectedRoute.id)
            || String(route.sourcePortId || "") === String(selectedRoute.sourcePortId || "")
            || String(route.destinationPortId || "") === String(selectedRoute.destinationPortId || "")
            || (route.viaNodeId && String(route.viaNodeId) === String(selectedRoute.viaNodeId || ""))
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
        for (let index = 0; index < text.length; ++index)
            hash = ((hash << 5) - hash + text.charCodeAt(index)) | 0
        return Math.abs(hash) % Math.max(1, count || 1)
    }
    function nodePosition(node, fallbackX, fallbackY) {
        if (!node) return ({ "x": fallbackX, "y": fallbackY })
        const saved = nodePositions[String(node.objectId || node.id || "")]
        if (saved) return saved
        return ({ "x": Number(node.x === undefined ? fallbackX : node.x),
                  "y": Number(node.y === undefined ? fallbackY : node.y) })
    }
    function nodeMotionDuration(node) {
        if (autoLayoutMotionActive) return motionLayoutDuration
        return nodeIsSnapSettling(node) ? motionFastDuration : 0
    }
    function wireIsAppearing(routeId) {
        return Boolean(appearingWireRouteIds[String(routeId || "")])
    }
    function snapNodeKey(node) {
        return String(node && (node.objectId || node.id) || "")
    }
    function snapNodeSize(node) {
        if (!node) return ({ "width": 238, "height": 86 })
        if (String(node.kind || "") === "processor") {
            return ({ "width": 155, "height": 82 + Math.max(0, Number(node.sharedChannelCount || 0) - 1) * 20 })
        }
        // Input/output cards can grow for visible ports.  This conservative
        // base keeps alignment suggestions small and never pulls across a
        // distant card merely because a group is expanded.
        return ({ "width": 238, "height": 86 })
    }
    function snapGridValue(value) {
        const proposed = Math.round(Number(value) / snapGridSize) * snapGridSize
        return Math.abs(proposed - Number(value)) <= snapGridThreshold
            ? ({ "value": proposed, "guide": proposed, "label": "grid" }) : null
    }
    function stabilizeAlignmentCandidate(node, axis, freeValue, candidate) {
        if (!nodeIsSnapDragging(node)) return candidate
        const key = snapNodeKey(node) + "|" + axis
        const previous = alignmentGuideHysteresis[key]
        let stable = candidate
        // Retain a guide slightly beyond acquisition so equivalent nearby
        // alignments do not flicker as the pointer crosses their boundary.
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
    function nodeSnapCandidate(node, x, y, bypass) {
        const freeX = Number(x)
        const freeY = Number(y)
        if (!isFinite(freeX) || !isFinite(freeY)) return ({ "active": false, "changed": false, "x": x, "y": y })
        if (Boolean(bypass) || !snapToGridEnabled)
            return ({ "active": false, "changed": false, "x": freeX, "y": freeY })
        const size = snapNodeSize(node)
        const key = snapNodeKey(node)
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
            if (!other || snapNodeKey(other) === key) continue
            const position = nodePosition(other, Number(other.x || 0), Number(other.y || 0))
            const otherSize = snapNodeSize(other)
            // Meaningful card relationships win over the visual grid.  Each
            // comparison is bounded by the same small 10px threshold.
            considerX(position.x, position.x, "left edge")
            considerX(position.x + otherSize.width - size.width, position.x + otherSize.width, "right edge")
            considerX(position.x + (otherSize.width - size.width) * 0.5,
                position.x + otherSize.width * 0.5, "vertical center")
            considerY(position.y, position.y, "top edge")
            considerY(position.y + (otherSize.height - size.height) * 0.5,
                position.y + otherSize.height * 0.5, "horizontal center")
        }
        alignmentX = stabilizeAlignmentCandidate(node, "x", freeX, alignmentX)
        alignmentY = stabilizeAlignmentCandidate(node, "y", freeY, alignmentY)
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
    function beginNodeSnapDrag(node, altBypass) {
        snapDragNodeId = snapNodeKey(node)
        snapDragAltBypass = Boolean(altBypass)
        nodeSnapPreview = ({})
        alignmentGuideHysteresis = ({})
    }
    function updateNodeSnapPreview(node, x, y, altBypass) {
        if (snapNodeKey(node) !== snapDragNodeId) return ({})
        snapDragAltBypass = Boolean(altBypass)
        const candidate = nodeSnapCandidate(node, x, y, snapDragAltBypass)
        nodeSnapPreview = candidate.active ? candidate : ({})
        return candidate
    }
    function finishNodeSnapDrag(node, x, y, altBypass) {
        const candidate = nodeSnapCandidate(node, x, y, Boolean(altBypass))
        nodeSnapPreview = ({})
        snapDragNodeId = ""
        snapDragAltBypass = false
        alignmentGuideHysteresis = ({})
        if (candidate.changed) {
            const next = ({})
            for (const id in snapSettlingNodeIds) next[id] = snapSettlingNodeIds[id]
            next[snapNodeKey(node)] = true
            snapSettlingNodeIds = next
            snapSettlingTimer.restart()
        }
        return candidate
    }
    function cancelNodeSnapDrag() {
        nodeSnapPreview = ({})
        snapDragNodeId = ""
        snapDragAltBypass = false
        alignmentGuideHysteresis = ({})
    }
    function nodeIsSnapSettling(node) {
        return Boolean(snapSettlingNodeIds[snapNodeKey(node)])
    }
    function nodeIsSnapDragging(node) {
        return snapNodeKey(node) === snapDragNodeId
    }
    Timer {
        id: snapSettlingTimer
        interval: root.motionFastDuration + 12
        repeat: false
        onTriggered: root.snapSettlingNodeIds = ({})
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
            const cardWidth = candidate.kind === "processor" ? 155 : 238
            const cardHeight = candidate.kind === "processor" ? 108 : 170
            if (position.x + cardWidth <= left + 28 || position.x >= right - 28) continue
            const span = Math.max(1, endX - startX)
            const t = Math.max(0, Math.min(1, (position.x + cardWidth * 0.5 - startX) / span))
            const lineY = startY + (endY - startY) * t
            if (lineY < position.y - 10 || lineY > position.y + cardHeight + 10) continue
            ++blockers
            if (blockers > 2) {
                // Dense routes deliberately remain below the opaque card. A
                // bounded fallback is safer than global autorouting.
                return ({ "hasDetour": false, "underCard": true, "detourY": 0 })
            }
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
        for (let index = 0; index < nodes.length; ++index)
            nodeById[String(nodes[index].id || "")] = nodes[index]
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            const key = String(route.sourcePortId || route.id || index) + "|" + String(route.viaNodeId || "direct")
            bundleCounts[key] = Number(bundleCounts[key] || 0) + 1
        }
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            const source = nodeById[String(route.sourceNodeId || "")]
            const target = nodeById[String(route.destinationNodeId || "")]
            if (!source || !target) continue
            const sourcePosition = nodePosition(source, 80, 160)
            const targetPosition = nodePosition(target, 1380, 160)
            const bundleKey = String(route.sourcePortId || route.id || index) + "|" + String(route.viaNodeId || "direct")
            const bundleCount = Number(bundleCounts[bundleKey] || 1)
            const firstBundleLeg = !bundleLeads[bundleKey]
            bundleLeads[bundleKey] = true
            const excludedIds = ({})
            excludedIds[String(source.id || "")] = true
            excludedIds[String(target.id || "")] = true
            const processorIds = route.processors || []
            const processorWaypoints = []
            for (let processorIndex = 0; processorIndex < processorIds.length; ++processorIndex) {
                const processor = nodeById[String(processorIds[processorIndex] || "")]
                if (!processor) continue
                const position = nodePosition(processor, 420 + processorIndex * 162, 180)
                const processorId = String(processor.id || processor.objectId || "")
                excludedIds[processorId] = true
                processorWaypoints.push({ "id": processorId, "leftX": position.x,
                    "rightX": position.x + 155, "y": position.y + 38 })
            }
            const branchX = bundleCount > 1 ? sourcePosition.x + 304 : sourcePosition.x + 230
            const segments = []
            let currentX = branchX
            let currentY = sourcePosition.y + 52 + stableLane(route.sourcePortId, 13) * 6
            for (let processorIndex = 0; processorIndex < processorWaypoints.length; ++processorIndex) {
                const waypoint = processorWaypoints[processorIndex]
                const obstacle = obstaclePlan(currentX, currentY, waypoint.leftX, waypoint.y, excludedIds)
                const canonicalSegment = (route.segments || [])[processorIndex]
                segments.push({ "startX": currentX, "startY": currentY, "endX": waypoint.leftX,
                    "endY": waypoint.y, "hasDetour": obstacle.hasDetour,
                    "detourY": obstacle.detourY, "underCard": obstacle.underCard,
                    "routeSegmentId": canonicalSegment ? String(canonicalSegment.id || "") : "" })
                currentX = waypoint.rightX
                currentY = waypoint.y
            }
            const finalX = targetPosition.x
            const finalY = targetPosition.y + 52 + stableLane(route.destinationPortId + route.id, 18) * 5
            const finalObstacle = obstaclePlan(currentX, currentY, finalX, finalY, excludedIds)
            const finalCanonicalSegment = (route.segments || [])[processorWaypoints.length]
            segments.push({ "startX": currentX, "startY": currentY, "endX": finalX,
                "endY": finalY, "hasDetour": finalObstacle.hasDetour,
                "detourY": finalObstacle.detourY, "underCard": finalObstacle.underCard,
                "routeSegmentId": finalCanonicalSegment ? String(finalCanonicalSegment.id || "") : "" })
            const focusSegment = segments[Math.floor(segments.length * 0.5)]
            next.push({
                "route": route,
                "routeId": String(route.id || ""),
                "startX": sourcePosition.x + 230,
                "startY": sourcePosition.y + 52 + stableLane(route.sourcePortId, 13) * 6,
                "endX": finalX,
                "endY": finalY,
                "segments": segments,
                "focusX": focusSegment ? (focusSegment.startX + focusSegment.endX) * 0.5 : finalX,
                "focusY": focusSegment ? (focusSegment.startY + focusSegment.endY) * 0.5 : finalY,
                "bundleCount": bundleCount,
                "drawBundleTrunk": bundleCount > 1 && firstBundleLeg,
                "bundleX": sourcePosition.x + 304,
                "processorCount": processorWaypoints.length
            })
        }
        wireGeometry = next
        if (wireCanvas) wireCanvas.requestPaint()
    }
    function restartWireMotion() {
        // Existing routes stay visible. Only newly committed durable IDs draw
        // in, so one connection never blanks unrelated topology.
        wireReveal = 1
        if (!reducedMotion && Object.keys(appearingWireRouteIds).length > 0) {
            wireAppear = 0
            wireAppearAnimation.restart()
        } else {
            wireAppear = 1
            appearingWireRouteIds = ({})
        }
        if (retiringWireGeometry.length > 0) {
            wireRetire = 0
            wireRetireAnimation.restart()
        } else wireRetire = 1
    }
    function fitGraph() {
        const horizontal = Math.max(0.5, Math.min(1.15, (graphViewport.width - 42) / graphScene.width))
        const vertical = Math.max(0.5, Math.min(1.15, (graphViewport.height - 42) / graphScene.height))
        zoom = Math.min(horizontal, vertical)
        graphViewport.contentX = 0
        graphViewport.contentY = 0
        workspaceDirty = true
        workspaceSaveTimer.restart()
        feedback = "Graph fit to the current workspace."
        feedbackError = false
    }
    function applyAutoLayout() {
        if (!graph.editable || (graph.workspace && graph.workspace.layoutLocked)) return false
        if (routingActive) cancelRouting("Connection cancelled while Auto Layout rearranges the graph.", true)
        autoLayoutMotionActive = true
        layoutMotionTimer.restart()
        const result = backendObject.signalFlowAutoLayout()
        showResult(result, "Auto-layout was not applied.")
        if (!result || !result.success) {
            autoLayoutMotionActive = false
            layoutMotionTimer.stop()
        }
        return Boolean(result && result.success)
    }
    function focusCurrentSelection() {
        const selectedId = selectedRoute && selectedRoute.id ? String(selectedRoute.id) : ""
        const geometry = wireGeometry.filter(function(entry) { return entry.routeId === selectedId })[0]
        if (!geometry) { fitGraph(); return }
        const focusX = Number(geometry.focusX === undefined ? (geometry.startX + geometry.endX) * 0.5 : geometry.focusX)
        const focusY = Number(geometry.focusY === undefined ? (geometry.startY + geometry.endY) * 0.5 : geometry.focusY)
        graphViewport.contentX = Math.max(0, (focusX * zoom) - graphViewport.width * 0.5)
        graphViewport.contentY = Math.max(0, (focusY * zoom) - graphViewport.height * 0.5)
        workspaceDirty = true
        workspaceSaveTimer.restart()
        feedback = "Centered the selected route."
        feedbackError = false
    }
    function sourcePortFromLearning(learning) {
        if (!learning || String(learning.kind || "") !== "signal-flow") return null
        const sourceId = String(learning.sourcePortId || "")
        if (sourceId.length === 0) return null
        const ports = inputPorts()
        for (let index = 0; index < ports.length; ++index)
            if (String(ports[index].id || "") === sourceId) return ports[index]
        return null
    }
    function focusSourcePort(port) {
        const input = nodeFor("input")
        const position = nodePosition(input, 80, 160)
        graphViewport.contentX = Math.max(0, position.x * zoom - 28)
        graphViewport.contentY = Math.max(0, position.y * zoom - 28)
        workspaceDirty = true
        workspaceSaveTimer.restart()
    }
    function acceptSignalFlowLearnedSource(learning) {
        const port = sourcePortFromLearning(learning)
        backendObject.cancelInputLearning()
        sourceLearnDialog.close()
        if (!port) {
            feedback = "The learned physical control is no longer available in this Signal Flow scope. Retry learning."
            feedbackError = true
            return false
        }
        chooseSource(port)
        learnedSourcePortId = String(port.id)
        focusSourcePort(port)
        feedback = "Found " + (learning.sourceLabel || port.label) + ". Choose a highlighted compatible destination."
        feedbackError = false
        return true
    }
    function highlightDestinationLearnedRoute(learning) {
        const destination = pendingLearnDestination
        const sourceId = String(learning && learning.sourcePortId || "")
        if (!destination || !destination.id || sourceId.length === 0) return false
        graph = backendObject.signalFlowGraph
        const ports = inputPorts()
        let learnedPort = null
        for (let index = 0; index < ports.length; ++index) {
            if (String(ports[index].id || "") === sourceId) {
                learnedPort = ports[index]
                break
            }
        }
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            if (String(routes[index].sourcePortId || "") !== sourceId
                    || String(routes[index].destinationPortId || "") !== String(destination.id)) continue
            selectedSource = learnedPort || ({})
            selectedRoute = routes[index]
            pendingLearnDestination = ({})
            focusCurrentSelection()
            feedback = "Learned route highlighted in Signal Flow."
            feedbackError = false
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
            selectedSource = sourceBeforeConnect
            selectedRoute = routes[index]
            learnedSourcePortId = ""
            focusCurrentSelection()
            feedback = "Route created from the learned source and highlighted in Signal Flow."
            feedbackError = false
            return true
        }
        return false
    }
    function processorEnabled(kind) {
        const details = selectedRoute && selectedRoute.processorDetails ? selectedRoute.processorDetails : []
        for (let index = 0; index < details.length; ++index)
            if (details[index].semantic === kind) return true
        return false
    }
    function processorDetail(kind) {
        const details = selectedRoute && selectedRoute.processorDetails ? selectedRoute.processorDetails : []
        for (let index = 0; index < details.length; ++index)
            if (details[index].semantic === kind) return details[index]
        return ({})
    }
    function processorIsShared(kind) { return Boolean(processorDetail(kind).shared) }
    function shareableAxisRoutes(kind) {
        const currentId = selectedRoute && selectedRoute.id ? String(selectedRoute.id) : ""
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
    function visibleInputPorts() {
        const ports = inputPorts().filter(function(port) { return portVisible(port, false) })
        return ports.slice(0, semanticDensity === "overview" ? 20 : semanticDensity === "compact" ? 36 : 56)
    }
    function visibleOutputPorts() {
        return outputPorts().filter(function(port) { return portVisible(port, true) })
    }
    function interactionSource() {
        const active = interaction && interaction.source
        return active && active.id ? active : selectedSource
    }
    function samePort(first, second) {
        return String(first && (first.endpointId || first.id) || "")
            === String(second && (second.endpointId || second.id) || "")
    }
    function routingContextIsCurrent() {
        if (!routingActive) return false
        return Number(interaction.expectedRevision || 0) === Number(graph.revision || 0)
            && String(interaction.profileId || "") === String(graph.profileId || "")
            && String(interaction.rigId || "") === String(graph.deviceRigId || "")
    }
    function cancelRouting(reason, announceCancellation) {
        const hadRouting = routingActive || Boolean(dragWire && dragWire.active)
        interaction = ({ "mode": "IDLE", "source": ({}), "target": ({}),
            "expectedRevision": 0, "profileId": "", "rigId": "", "token": interactionToken })
        selectedSource = ({})
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0 })
        sourceDragDropHandled = true
        connectionPreview = ({})
        if (hadRouting && announceCancellation && reason && reason.length > 0) {
            feedback = reason
            feedbackError = false
        }
        return hadRouting
    }
    function armSource(port, beginDrag) {
        if (autoLayoutMotionActive) {
            feedback = "Auto Layout is settling. Wait for the graph to finish moving."
            feedbackError = false
            return false
        }
        if (!port || !port.id) return false
        if (viewMode === "effective" || !graph.editable) {
            feedback = "Effective view is read-only. Switch to Configured to edit this route."
            feedbackError = true
            return false
        }
        if (!port.available) {
            feedback = "This input is inactive. Complete calibration before routing it."
            feedbackError = true
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
        selectedSource = port
        selectedRoute = ({})
        selectedNode = ({})
        connectionPreview = ({})
        feedback = beginDrag ? "Dragging " + port.label + ". Release on a highlighted destination."
                             : "Connection armed: " + port.label + ". Choose a compatible destination."
        feedbackError = false
        return true
    }
    function sourceIsCompatible(port) {
        const activeSource = interactionSource()
        if (!activeSource || !activeSource.kind)
            return false
        if (activeSource.kind === "axis") return port.kind === "axis"
        if (activeSource.kind === "native-pov") return port.kind === "native-pov"
        return port.kind === "button"
    }
    function sourceLabel() {
        const activeSource = interactionSource()
        return activeSource && activeSource.label ? activeSource.label : "Select a source port"
    }
    function selectNode(node) {
        if (!node || !node.id) return false
        if (routingActive) cancelRouting("", false)
        selectedNode = node
        selectedRoute = ({})
        selectedSource = ({})
        feedback = (node.label || "Signal Flow card") + " selected. Its saved identity, readiness, and endpoint summary are shown in the inspector."
        feedbackError = false
        return true
    }
    function useInputScope(node) {
        if (!node || node.kind !== "input" || !node.controllerRecordId || !graph.deviceRigId) return false
        if (routingActive) cancelRouting("Connection cancelled because the editing scope changed.", true)
        const changed = backendObject.setEditingDeviceContext(String(graph.deviceRigId), [String(node.controllerRecordId)])
        if (!changed) {
            feedback = "This saved input could not become the editing scope. Resolve its Device Rig membership in Devices."
            feedbackError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        selectedNode = ({});
        selectedSource = ({});
        selectedRoute = ({});
        feedback = "Editing scope changed to " + (node.label || "the selected input") + ". Its routes are now editable."
        feedbackError = false
        return true
    }
    function selectProfileContext(profile) {
        if (!profile || !profile.id) return false
        if (routingActive) cancelRouting("Connection cancelled because the editing profile changed.", true)
        if (!backendObject.activateProfile(String(profile.id))) {
            feedback = "The selected profile could not become the active editing context."
            feedbackError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        clearSelection()
        feedback = "Editing profile changed to " + (profile.displayName || profile.name || "the selected profile") + "."
        feedbackError = false
        return true
    }
    function selectRigContext(rig) {
        if (!rig || !rig.id) return false
        if (routingActive) cancelRouting("Connection cancelled because the Device Rig changed.", true)
        if (!backendObject.setEditingDeviceContext(String(rig.id), [])) {
            feedback = "The selected Device Rig could not become the Signal Flow context."
            feedbackError = true
            return false
        }
        graph = backendObject.signalFlowGraph
        clearSelection()
        feedback = "Signal Flow now shows " + (rig.name || "the selected Device Rig") + ". Choose a member card before editing its routes."
        feedbackError = false
        return true
    }
    function beginSourceDrag(port, point) {
        if (!armSource(port, true)) return false
        sourceDragDropHandled = false
        dragWire = ({ "active": true, "source": port,
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
        return true
    }
    function updateSourceDrag(point) {
        if (!dragWire.active || !routingContextIsCurrent()) {
            if (routingActive || dragWire.active)
                cancelRouting("The graph changed while the connection was being dragged. No route was changed.", true)
            return
        }
        dragWire = ({ "active": true, "source": interactionSource(),
            "x": Number(point && point.x || 0), "y": Number(point && point.y || 0) })
    }
    function endSourceDrag() {
        if (!dragWire.active) return
        const gestureToken = Number(interaction && interaction.token || 0)
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0 })
        // Qt Quick may retire the source handler before its destination
        // DropArea reports the release. Give that single delivery turn a
        // chance to mark the gesture handled, then cancel a genuine miss.
        Qt.callLater(function() {
            if (!root.routingActive || Number(root.interaction && root.interaction.token || 0) !== gestureToken)
                return
            if (!root.routingContextIsCurrent())
                root.cancelRouting("The graph changed while the connection was being dragged. No route was changed.", true)
            else if (!root.sourceDragDropHandled)
                root.cancelRouting("Connection cancelled: release on a highlighted compatible destination.", true)
        })
    }
    function previewDestination(port) {
        const activeSource = interactionSource()
        if (!port || !activeSource || !activeSource.id || !routingActive) return
        if (!routingContextIsCurrent()) {
            cancelRouting("The graph changed while the connection was being prepared. No route was changed.", true)
            return
        }
        const sourceEndpoint = String(activeSource.endpointId || activeSource.id || "")
        const destinationEndpoint = String(port.endpointId || port.id || "")
        const preview = sourceEndpoint.length > 0 && destinationEndpoint.length > 0
            ? backendObject.signalFlowPreviewConnection(sourceEndpoint, destinationEndpoint, "",
                Number(interaction.expectedRevision || 0))
            : ({ "success": false, "title": "Port is unavailable",
                "message": "This port no longer has a canonical graph endpoint. Refresh the graph and retry." })
        const accepted = Boolean(preview && (preview.success || preview.requiresCollisionDecision))
        connectionPreview = ({ "portId": String(port.id || ""), "compatible": accepted,
            "canCommit": Boolean(preview && preview.canCommit),
            "requiresCollisionDecision": Boolean(preview && preview.requiresCollisionDecision),
            "title": String(preview && preview.title || "Connection preview"),
            "message": String(preview && preview.message || "This destination cannot be used.") })
        if (accepted)
            interaction = ({ "mode": "TARGET_PREVIEW", "source": activeSource, "target": port,
                "expectedRevision": interaction.expectedRevision, "profileId": interaction.profileId,
                "rigId": interaction.rigId, "token": interaction.token })
    }
    function clearDestinationPreview(port) {
        if (!port) return
        if (dragWire && dragWire.active && String(interaction && interaction.target && interaction.target.id || "")
                === String(port.id || "")) return
        if (String(interaction && interaction.target && interaction.target.id || "") === String(port.id || ""))
            interaction = ({ "mode": "SOURCE_ARMED", "source": interactionSource(), "target": ({}),
                "expectedRevision": interaction.expectedRevision, "profileId": interaction.profileId,
                "rigId": interaction.rigId, "token": interaction.token })
        if (String(connectionPreview.portId || "") === String(port.id || ""))
            connectionPreview = ({})
    }
    function previewProcessorTarget(route, segmentId) {
        if (!route || !route.id || !segmentId) return
        pendingProcessorRouteId = String(route.id)
        pendingProcessorSegmentId = String(segmentId)
        processorHysteresis.restart()
    }
    function distanceToWireLine(x, y, first, second) {
        const dx = second.x - first.x, dy = second.y - first.y
        const length = dx * dx + dy * dy
        if (length < 0.0001) return Math.hypot(x - first.x, y - first.y)
        const t = Math.max(0, Math.min(1, ((x - first.x) * dx + (y - first.y) * dy) / length))
        return Math.hypot(x - (first.x + dx * t), y - (first.y + dy * t))
    }
    function cubicPoint(first, controlOne, controlTwo, second, t) {
        const inverse = 1 - t
        return ({ "x": inverse * inverse * inverse * first.x + 3 * inverse * inverse * t * controlOne.x
                    + 3 * inverse * t * t * controlTwo.x + t * t * t * second.x,
                  "y": inverse * inverse * inverse * first.y + 3 * inverse * inverse * t * controlOne.y
                    + 3 * inverse * t * t * controlTwo.y + t * t * t * second.y })
    }
    function distanceToWireSegment(x, y, segment, lane) {
        const first = ({ "x": Number(segment.startX || 0), "y": Number(segment.startY || 0) })
        const second = ({ "x": Number(segment.endX || 0), "y": Number(segment.endY || 0) })
        const direction = second.x >= first.x ? 1 : -1
        if (graph.workspace && graph.workspace.wireStyle === "orthogonal") {
            if (segment.hasDetour) {
                const stub = Math.max(42, Math.min(120, Math.abs(second.x - first.x) * 0.22))
                const firstElbow = ({ "x": first.x + direction * stub, "y": first.y })
                const secondElbow = ({ "x": second.x - direction * stub, "y": second.y })
                return Math.min(distanceToWireLine(x, y, first, firstElbow),
                    distanceToWireLine(x, y, firstElbow, ({ "x": firstElbow.x, "y": Number(segment.detourY || 0) })),
                    distanceToWireLine(x, y, ({ "x": firstElbow.x, "y": Number(segment.detourY || 0) }),
                        ({ "x": secondElbow.x, "y": Number(segment.detourY || 0) })),
                    distanceToWireLine(x, y, ({ "x": secondElbow.x, "y": Number(segment.detourY || 0) }), secondElbow),
                    distanceToWireLine(x, y, secondElbow, second))
            }
            const laneX = Math.round((first.x + second.x) * 0.5 + lane * 12)
            return Math.min(distanceToWireLine(x, y, first, ({ "x": laneX, "y": first.y })),
                distanceToWireLine(x, y, ({ "x": laneX, "y": first.y }), ({ "x": laneX, "y": second.y })),
                distanceToWireLine(x, y, ({ "x": laneX, "y": second.y }), second))
        }
        let best = Infinity
        let previous = first
        const controls = segment.hasDetour
            ? [{ "start": first, "controlOne": ({ "x": first.x + direction * 86, "y": first.y }),
                 "controlTwo": ({ "x": first.x + direction * 118, "y": Number(segment.detourY || 0) }),
                 "end": ({ "x": (first.x + second.x) * 0.5, "y": Number(segment.detourY || 0) }) },
               { "start": ({ "x": (first.x + second.x) * 0.5, "y": Number(segment.detourY || 0) }),
                 "controlOne": ({ "x": second.x - direction * 118, "y": Number(segment.detourY || 0) }),
                 "controlTwo": ({ "x": second.x - direction * 86, "y": second.y }), "end": second }]
            : [{ "start": first, "controlOne": ({ "x": first.x + 148 + lane * 10, "y": first.y }),
                 "controlTwo": ({ "x": second.x - 148 - lane * 10, "y": second.y }), "end": second }]
        for (let curveIndex = 0; curveIndex < controls.length; ++curveIndex) {
            const curve = controls[curveIndex]
            previous = curve.start
            for (let sample = 1; sample <= 12; ++sample) {
                const current = cubicPoint(curve.start, curve.controlOne, curve.controlTwo, curve.end, sample / 12)
                best = Math.min(best, distanceToWireLine(x, y, previous, current))
                previous = current
            }
        }
        return best
    }
    function hitProcessorWire(x, y) {
        let best = null
        let bestDistance = 12
        const geometry = wireGeometry || []
        for (let routeIndex = 0; routeIndex < geometry.length; ++routeIndex) {
            const entry = geometry[routeIndex]
            if (!entry || !entry.route || (viewMode === "effective" && !entry.route.effective)) continue
            const segments = entry.segments || []
            for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                const segment = segments[segmentIndex]
                if (!segment || !segment.routeSegmentId) continue
                const distance = distanceToWireSegment(x, y, segment, stableLane(entry.route.id, 5) - 2)
                if (distance < bestDistance) {
                    bestDistance = distance
                    best = ({ "route": entry.route, "routeSegmentId": String(segment.routeSegmentId) })
                }
            }
        }
        return best
    }
    function clearSelection() {
        interaction = ({ "mode": "IDLE", "source": ({}), "target": ({}),
            "expectedRevision": 0, "profileId": "", "rigId": "", "token": interactionToken })
        selectedSource = ({})
        selectedRoute = ({})
        selectedNode = ({})
        selectedSegmentId = ""
        pendingProcessorSegmentId = ""
        dragWire = ({ "active": false, "source": ({}), "x": 0, "y": 0 })
        sourceDragDropHandled = true
        connectionPreview = ({})
    }
    function applyBackendFocus() {
        const target = backendObject ? String(backendObject.signalFlowFocusObjectId || "") : ""
        if (!target) return
        const routes = graph.routes || []
        for (let index = 0; index < routes.length; ++index) {
            const route = routes[index]
            if (String(route.id || "") === target) {
                selectedRoute = route
                selectedSource = ({})
                selectedNode = ({})
                feedback = "Opened the route selected from App Health."
                feedbackError = false
                Qt.callLater(focusCurrentSelection)
                return
            }
            const details = route.processorDetails || []
            for (let detailIndex = 0; detailIndex < details.length; ++detailIndex) {
                if (String(details[detailIndex].id || "") !== target) continue
                selectedRoute = route
                selectedSource = ({})
                selectedNode = ({})
                feedback = "Opened the processor selected from App Health."
                feedbackError = false
                Qt.callLater(focusCurrentSelection)
                return
            }
        }
    }
    function showResult(result, fallback) {
        feedback = result && result.message ? result.message : fallback
        feedbackError = !(result && result.success)
    }
    function chooseSource(port) {
        return armSource(port, false)
    }
    function destinationPortById(portId) {
        const ports = outputPorts()
        for (let index = 0; index < ports.length; ++index)
            if (String(ports[index].id || "") === String(portId || "")) return ports[index]
        return null
    }
    function commitConnection(port, decision) {
        const sourceBeforeConnect = interactionSource()
        if (!sourceBeforeConnect || !sourceBeforeConnect.id || !port || !port.id) {
            feedback = "Select an input and a compatible destination first."
            feedbackError = true
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
                connectionPreview = ({ "portId": String(port.id || ""), "compatible": true,
                    "canCommit": false, "requiresCollisionDecision": true,
                    "title": String(preview.title || "Resolve destination collision"),
                    "message": String(preview.message || "Choose an explicit collision policy.") })
                interaction = ({ "mode": "TARGET_PREVIEW", "source": sourceBeforeConnect, "target": port,
                    "expectedRevision": interaction.expectedRevision, "profileId": interaction.profileId,
                    "rigId": interaction.rigId, "token": interaction.token })
                replaceDialog.destination = destinationFor(port)
                replaceDialog.portLabel = String(port.label || "destination")
                replaceDialog.destinationPortId = String(port.id || "")
                const options = preview.collisionOptions || []
                replaceDialog.mixerAllowed = options.indexOf("average") >= 0
                replaceDialog.open()
                feedback = String(preview.message || "Choose an explicit collision policy.")
                feedbackError = false
            } else {
                showResult(preview, "Connection was not applied.")
                if (preview && String(preview.title || "") === "Graph context changed")
                    cancelRouting("The graph changed while the connection was being prepared. No route was changed.", true)
            }
            return false
        }
        connectionCommitInFlight = true
        const result = backendObject.connectSignalFlowEndpoints(sourceEndpoint, destinationEndpoint,
            requestedDecision, Number(interaction.expectedRevision || 0))
        connectionCommitInFlight = false
        showResult(result, "Connection was not applied.")
        if (result && result.success) {
            cancelRouting("", false)
            if (!keepLearnedRouteHighlighted(sourceBeforeConnect, port.id)) selectedRoute = ({})
            return true
        }
        return false
    }
    function chooseOutput(port) { return commitConnection(port, "") }
    function chooseDraggedOutput(sourcePort, outputPort) {
        if (!sourcePort || !outputPort) return false
        if (!routingActive || !samePort(sourcePort, interactionSource())) {
            if (!armSource(sourcePort, true)) return false
        }
        previewDestination(outputPort)
        return commitConnection(outputPort, "")
    }
    function applyReplacement() {
        const destination = destinationPortById(replaceDialog.destinationPortId)
        return destination ? commitConnection(destination, "replace") : false
    }
    function applyMixer(mode) {
        const destination = destinationPortById(replaceDialog.destinationPortId)
        return destination ? commitConnection(destination, mode) : false
    }
    function disconnectSelected() {
        if (!selectedRoute || !selectedRoute.id)
            return
        const result = backendObject.signalFlowDisconnect(String(selectedRoute.id), Number(graph.revision || 0))
        showResult(result, "Route was not disconnected.")
        if (result && result.success)
            selectedRoute = ({})
    }
    function explainSelected() {
        if (!selectedRoute || !selectedRoute.id) return
        routeExplanation = backendObject.signalFlowExplainRoute(String(selectedRoute.id))
        explainDialog.open()
    }
    function toggleProcessor(kind) {
        if (!selectedRoute || !selectedRoute.id) return
        if (processorIsShared(kind)) {
            splitSharedProcessor(kind)
            return
        }
        const detail = processorDetail(kind)
        const result = processorEnabled(kind)
            ? backendObject.signalFlowRemoveOrBypassProcessor(String(detail.id || ""), Number(graph.revision || 0))
            : backendObject.signalFlowInsertProcessor(selectedProcessorSegmentId(), kind,
                Number(graph.revision || 0))
        showResult(result, "Processor change was not applied.")
        if (result && result.success) processorDialog.close()
    }
    function selectedProcessorSegmentId() {
        const segments = selectedRoute && selectedRoute.segments ? selectedRoute.segments : []
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
    function removeSelectedProcessor() {
        if (!selectedNode || selectedNode.kind !== "processor" || !selectedNode.objectId) return
        const route = selectedRoute && selectedRoute.id ? selectedRoute : routeForProcessorNode(selectedNode)
        if (selectedNode.semantic === "mixer") {
            mixerDialog.mixerId = String(selectedNode.objectId)
            mixerDialog.routeId = String(route.id || "")
            mixerDialog.currentMode = String(selectedNode.mixerMode || "Average").toLowerCase().replace(" ", "-")
            mixerDialog.open()
            return
        }
        const result = selectedNode.shared
            ? backendObject.signalFlowRemoveSharedProcessorChannel(String(selectedNode.objectId), String(route.id || ""),
                Number(graph.revision || 0))
            : backendObject.signalFlowRemoveOrBypassProcessor(String(selectedNode.objectId),
                Number(graph.revision || 0))
        showResult(result, "Processor was not removed.")
        if (result && result.success) {
            selectedSegmentId = ""
            selectedNode = ({})
        }
    }
    function splitSharedProcessor(kind) {
        if (!selectedRoute || !selectedRoute.id) return
        const result = backendObject.signalFlowSplitSharedProcessor(String(selectedRoute.id), kind,
            Number(graph.revision || 0))
        showResult(result, "Shared processor was not split.")
        if (result && result.success) processorDialog.close()
    }
    function openShareProcessorDialog() {
        if (!selectedRoute || !selectedRoute.id) return
        shareDialog.prepare()
        if (shareDialog.processorKinds.length > 0) shareDialog.open()
        else {
            feedback = "Add or configure a processor on this axis before sharing it."
            feedbackError = true
        }
    }
    function startLearning(destination) {
        if (!destination) return
        let started = false
        if (destination.kind === "axis") started = backendObject.startAxisLearning(String(destination.technicalLabel))
        else if (destination.kind === "button") started = backendObject.startButtonLearning(Number(destination.index))
        pendingLearnDestination = started ? destination : ({})
        feedback = started ? "Learning is armed. Move the physical control you want to route."
                           : "Learning needs a connected physical input and an available virtual destination."
        feedbackError = !started
        if (started) learnDialog.close()
    }
    function startSignalFlowSourceLearning() {
        const started = backendObject.startSignalFlowInputLearning()
        feedback = started ? "Learning is armed. Move or press the physical control you want to route."
                           : "Learning needs a connected physical input."
        feedbackError = !started
        if (started) sourceLearnDialog.open()
        return started
    }
    function previewDefaults(mode) {
        defaultsDialog.mode = mode
        defaultsDialog.preview = backendObject.signalFlowDefaultPreview(mode)
        defaultsDialog.open()
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
            "layoutLocked": savedValue("layoutLocked", false),
            "snapToGrid": savedValue("snapToGrid", true)
        })
    }
    function persistWorkspace(changes, successMessage) {
        const saved = backendObject.signalFlowSaveWorkspace(workspaceSnapshot(changes || ({})))
        if (saved) {
            graph = backendObject.signalFlowGraph
            workspaceDirty = false
            if (successMessage && successMessage.length > 0) {
                feedback = successMessage
                feedbackError = false
            }
        } else {
            feedback = "Signal Flow workspace could not be saved."
            feedbackError = true
        }
        return saved
    }
    function saveWorkspace() {
        if (!workspaceDirty || !graph.workspace) return true
        return persistWorkspace({}, "")
    }
    function toggleWireStyle() {
        const nextStyle = graph.workspace && graph.workspace.wireStyle === "orthogonal" ? "smooth" : "orthogonal"
        persistWorkspace({ "wireStyle": nextStyle }, "Wire style set to " + nextStyle + ".")
    }
    function setDensityMode(mode) {
        if (mode !== "detailed" && mode !== "compact" && mode !== "overview") return false
        return persistWorkspace({ "densityMode": mode }, "Semantic density set to " + mode + ".")
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
    function toggleSnapToGrid() {
        const enabled = snapToGridEnabled
        return persistWorkspace({ "snapToGrid": !enabled }, !enabled
            ? "Snap to Grid enabled. Cards remain free until release."
            : "Snap to Grid disabled. Card positions will stay exactly where released.")
    }
    function saveNodePlacement(node, x, y, pinned) {
        if (!node || !node.objectId) return false
        const saved = backendObject.signalFlowSaveNodeLayout(String(node.objectId), x, y, Boolean(pinned))
        if (saved) {
            graph = backendObject.signalFlowGraph
            feedback = Boolean(pinned) ? "Pinned node placement saved." : "Node placement saved."
            feedbackError = false
        } else {
            feedback = "Node placement was not saved."
            feedbackError = true
        }
        return saved
    }
    function toggleNodePinned(node) {
        if (!node) return false
        const placement = nodePosition(node, Number(node.x || 0), Number(node.y || 0))
        return saveNodePlacement(node, placement.x, placement.y, !Boolean(node.pinned))
    }
    function cancelTransient() {
        if (routingActive) cancelRouting("Connection cancelled.", true)
        else if (replaceDialog.visible) replaceDialog.close()
        else if (processorDialog.visible) processorDialog.close()
        else if (shareDialog.visible) shareDialog.close()
        else if (sourceLearnDialog.visible) sourceLearnDialog.close()
        else if (learnDialog.visible) learnDialog.close()
        else if (aliasDialog.visible) aliasDialog.close()
        else if (defaultsDialog.visible) defaultsDialog.close()
        else if (explainDialog.visible) explainDialog.close()
        else if (selectedSource && selectedSource.id || selectedRoute && selectedRoute.id) {
            clearSelection()
            feedback = "Selection cleared."
            feedbackError = false
        } else return false
        return true
    }
    // Kept as a callable command surface as well as a real key handler below,
    // so automated QML tests can prove keyboard actions reach the same backend
    // commands as pointer gestures.
    function keyboardAction(action) {
        if (action === "undo") {
            if (!backendObject.signalFlowCanUndo) return false
            showResult(backendObject.signalFlowUndo(Number(graph.revision || 0)), "Undo was not applied.")
            return !feedbackError
        }
        if (action === "redo") {
            if (!backendObject.signalFlowCanRedo) return false
            showResult(backendObject.signalFlowRedo(Number(graph.revision || 0)), "Redo was not applied.")
            return !feedbackError
        }
        if (action === "search") { searchField.forceActiveFocus(); searchField.selectAll(); return true }
        if (action === "delete") {
            if (!selectedRoute || !selectedRoute.id || viewMode !== "configured") return false
            disconnectSelected()
            return !feedbackError
        }
        if (action === "escape") return cancelTransient()
        if (action === "fit") { fitGraph(); return true }
        if (action === "focus") { focusCurrentSelection(); return true }
        if (action === "xray") { xrayMode = !xrayMode; feedback = xrayMode ? "X-ray isolates related signal paths." : "X-ray shows the full graph."; feedbackError = false; return true }
        if (action === "live") { liveMode = !liveMode; if (liveMode) liveTelemetry = backendObject.signalFlowLiveTelemetry(); return true }
        if (action === "density") return cycleDensityMode()
        if (action === "zoom-in") { zoom = Math.min(1.6, zoom + 0.1); workspaceDirty = true; workspaceSaveTimer.restart(); return true }
        if (action === "zoom-out") { zoom = Math.max(0.5, zoom - 0.1); workspaceDirty = true; workspaceSaveTimer.restart(); return true }
        return false
    }
    function restoreWorkspace() {
        if (!graph.workspace || workspaceRestored)
            return
        zoom = Number(graph.workspace.zoom || 1.0)
        graphViewport.contentX = Math.max(0, Number(graph.workspace.panX || 0))
        graphViewport.contentY = Math.max(0, Number(graph.workspace.panY || 0))
        workspaceRestored = true
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
        const previousRouteIds = ({})
        const previousGeometry = wireGeometry || []
        for (let index = 0; index < previousGeometry.length; ++index) {
            const routeId = String(previousGeometry[index] && previousGeometry[index].routeId || "")
            if (routeId.length > 0) previousRouteIds[routeId] = true
        }
        for (let index = 0; index < nextRoutes.length; ++index)
            nextRouteIds[String(nextRoutes[index].id || "")] = true
        retiringWireGeometry = previousGeometry.filter(function(entry) {
            return entry && entry.routeId && !nextRouteIds[String(entry.routeId)]
        })
        const appearing = ({})
        for (let index = 0; index < nextRoutes.length; ++index) {
            const routeId = String(nextRoutes[index].id || "")
            if (routeId.length > 0 && !previousRouteIds[routeId]) appearing[routeId] = true
        }
        appearingWireRouteIds = appearing
        if (selectedRoute && selectedRoute.id && !nextRouteIds[String(selectedRoute.id)]) selectedRoute = ({})
        nodePositions = ({})
        cancelNodeSnapDrag()
        rebuildWireGeometry()
        restartWireMotion()
    }
    onPresentationStateChanged: {
        presentationRestored = false
        Qt.callLater(restorePresentationState)
    }
    onSelectedRouteChanged: {
        if (routingActive && selectedRoute && selectedRoute.id)
            cancelRouting("Connection cancelled because a route was selected.", true)
        if (wireCanvas) wireCanvas.requestPaint()
    }
    onSelectedSourceChanged: if (wireCanvas) wireCanvas.requestPaint()
    onSelectedNodeChanged: {
        if (routingActive && selectedNode && selectedNode.id)
            cancelRouting("Connection cancelled because a card was selected.", true)
        if (wireCanvas) wireCanvas.requestPaint()
    }
    onViewModeChanged: {
        if (viewMode === "effective" && routingActive)
            cancelRouting("Connection cancelled because Effective view is read-only.", true)
        if (viewMode === "effective") cancelNodeSnapDrag()
        if (wireCanvas) wireCanvas.requestPaint()
    }
    onLiveTelemetryChanged: if (wireCanvas) wireCanvas.requestPaint()
    onSignalFocusChanged: if (wireCanvas) wireCanvas.requestPaint()
    onXrayModeChanged: if (wireCanvas) wireCanvas.requestPaint()
    onReducedMotionChanged: {
        if (reducedMotion) {
            wireRevealAnimation.stop()
            wireRetireAnimation.stop()
            wireAppearAnimation.stop()
            wireReveal = 1
            wireRetire = 1
            retiringWireGeometry = []
            wireAppear = 1
            appearingWireRouteIds = ({})
        }
        if (wireCanvas) wireCanvas.requestPaint()
    }
    onWireRevealChanged: if (wireCanvas) wireCanvas.requestPaint()
    onWireRetireChanged: if (wireCanvas) wireCanvas.requestPaint()
    onWireAppearChanged: if (wireCanvas) wireCanvas.requestPaint()
    onLiveModeChanged: if (wireCanvas) wireCanvas.requestPaint()
    onDragWireChanged: if (wireCanvas) wireCanvas.requestPaint()
    onRouteStateFilterChanged: if (wireCanvas) wireCanvas.requestPaint()
    onSearchTextChanged: if (wireCanvas) wireCanvas.requestPaint()

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
            if (!root.workspaceRestored)
                Qt.callLater(root.restoreWorkspace)
            Qt.callLater(root.applyBackendFocus)
        }
        function onInputLearningChanged() {
            const learning = backendObject.inputLearning || ({})
            if (String(learning.kind || "") === "signal-flow") {
                if (String(learning.phase || "") === "assigned") {
                    root.acceptSignalFlowLearnedSource(learning)
                } else if (Boolean(learning.active) && !sourceLearnDialog.visible) {
                    sourceLearnDialog.open()
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
            && (root.liveMode || root.signalFocus || root.viewMode === "effective")
        onTriggered: root.liveTelemetry = backendObject.signalFlowLiveTelemetry()
    }

    // A short sustained-hover gate keeps a dragged processor from jumping
    // between adjacent visible wires. The committed drop still targets the
    // exact canonical segment under the pointer and is revision-checked.
    Timer {
        id: processorHysteresis
        interval: 110
        repeat: false
        onTriggered: {
            const routes = root.graph.routes || []
            for (let index = 0; index < routes.length; ++index) {
                if (String(routes[index].id || "") !== root.pendingProcessorRouteId) continue
                root.selectedRoute = routes[index]
                root.selectedSegmentId = root.pendingProcessorSegmentId
                root.selectedSource = ({})
                root.selectedNode = ({})
                break
            }
        }
    }

    Timer {
        id: wireGeometryTimer
        interval: 16
        repeat: false
        onTriggered: root.rebuildWireGeometry()
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
        id: wireAppearAnimation
        target: root
        property: "wireAppear"
        from: 0
        to: 1
        duration: root.motionStructuralDuration
        easing.type: Easing.OutCubic
        onStopped: if (root.wireAppear >= 0.999) root.appearingWireRouteIds = ({})
    }

    component FlowButton: Button {
        id: flowButton
        property bool accent: false
        property bool dangerAction: false
        property string helpText: ""
        implicitHeight: 30
        padding: 10
        font.pixelSize: 11
        font.bold: true
        Accessible.name: text
        contentItem: Text {
            text: flowButton.text
            color: flowButton.enabled ? (flowButton.accent ? root.pageBackground : root.textStrong) : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font: flowButton.font
        }
        background: Rectangle {
            radius: root.themeTokens.controlRadius || 4
            border.width: 1
            border.color: flowButton.dangerAction ? root.danger : flowButton.accent ? root.graphOutput : root.border
            color: !flowButton.enabled ? root.insetPanel
                 : flowButton.down ? root.themeTokens.controlPressed || root.control
                 : flowButton.hovered ? (flowButton.accent ? root.graphOutput : root.controlHover)
                 : flowButton.accent ? root.graphOutput : root.control
        }
        ToolTip.visible: flowButton.hovered && flowButton.helpText.length > 0
        ToolTip.delay: 450
        ToolTip.text: flowButton.helpText
    }

    component PortRow: Item {
        id: portRow
        // A repeater delegate may be constructed before its row is assigned.
        // Keep the shell inert for that brief lifecycle rather than evaluating
        // an undefined port object and emitting runtime QML warnings.
        property var port: ({})
        property bool output: false
        property bool selected: false
        property bool compatible: false
        implicitHeight: 27
        width: parent ? parent.width : 180
        activeFocusOnTab: true
        opacity: output && root.routingActive && !compatible
            && !(root.connectionPreview && root.connectionPreview.portId === String(port.id || "")) ? 0.5 : 1.0
        Behavior on opacity { NumberAnimation { duration: root.motionFastDuration } }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: portRow.selected ? root.graphPreview
                 : portRow.compatible ? Qt.rgba(root.graphOutput.r, root.graphOutput.g, root.graphOutput.b, 0.20)
                 : dropTarget.containsDrag ? Qt.rgba(root.graphOutput.r, root.graphOutput.g, root.graphOutput.b, 0.32)
                 : hit.containsMouse ? root.controlHover : "transparent"
            border.width: portRow.selected || portRow.compatible || dropTarget.containsDrag ? 1 : 0
            border.color: portRow.selected ? root.textStrong : root.graphOutput
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            spacing: 5
            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 4
                color: !portRow.port || !portRow.port.available ? root.textMuted
                     : portRow.port.mapped ? (portRow.output ? root.graphOutput : root.ready) : root.graphInput
            }
            Text {
                Layout.fillWidth: true
                text: portRow.port && portRow.port.label ? portRow.port.label : ""
                color: portRow.port && portRow.port.available ? root.text : root.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            Text {
                text: portRow.port && portRow.port.mapped && !portRow.output ? "ROUTED" : ""
                color: root.ready
                font.pixelSize: 8
                font.bold: true
            }
            Text {
                visible: portRow.output && root.routingActive
                text: portRow.compatible ? "READY" : "DIFFERS"
                color: portRow.compatible ? root.ready : root.textMuted
                font.pixelSize: 8
                font.bold: true
            }
        }
        Drag.active: sourceDrag.active
        Drag.source: portRow
        Drag.keys: ["signal-flow-source"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        DragHandler {
            id: sourceDrag
            enabled: Boolean(!portRow.output && root.graph.editable && root.viewMode === "configured"
                             && portRow.port && portRow.port.available)
            dragThreshold: 8
            onActiveChanged: {
                if (active && portRow.port && portRow.port.id) {
                    const point = portRow.mapToItem(graphScene, portRow.width, portRow.height * 0.5)
                    root.beginSourceDrag(portRow.port, point)
                } else {
                    root.endSourceDrag()
                }
            }
            onTranslationChanged: {
                if (!active) return
                const point = portRow.mapToItem(graphScene, portRow.width + translation.x,
                    portRow.height * 0.5 + translation.y)
                root.updateSourceDrag(point)
            }
        }
        DropArea {
            id: dropTarget
            anchors.fill: parent
            keys: ["signal-flow-source"]
            enabled: Boolean(portRow.output && root.graph.editable && root.viewMode === "configured")
            onEntered: function(drag) {
                if (drag.source && drag.source.port) root.previewDestination(portRow.port)
            }
            onExited: function(drag) { root.clearDestinationPreview(portRow.port) }
            onDropped: function(drop) {
                if (drop.source && drop.source.port) {
                    root.sourceDragDropHandled = true
                    root.chooseDraggedOutput(drop.source.port, portRow.port)
                    root.endSourceDrag()
                    drop.accepted = true
                }
            }
        }
        MouseArea {
            id: hit
            anchors.fill: parent
            hoverEnabled: true
            enabled: Boolean(root.graph.editable && root.viewMode === "configured" && portRow.port && portRow.port.available)
            onContainsMouseChanged: {
                if (!portRow.output) return
                if (containsMouse) root.previewDestination(portRow.port)
                else root.clearDestinationPreview(portRow.port)
            }
            onClicked: {
                if (!portRow.port || !portRow.port.id)
                    return
                if (portRow.output) root.chooseOutput(portRow.port)
                else root.chooseSource(portRow.port)
            }
        }
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter && event.key !== Qt.Key_Space) return
            if (portRow.output) root.chooseOutput(portRow.port)
            else root.chooseSource(portRow.port)
            event.accepted = true
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 4; height: parent.height + 4; radius: 5
            color: "transparent"; border.width: 2; border.color: root.borderStrong
            visible: parent.activeFocus
        }
        ToolTip.visible: hit.containsMouse
        ToolTip.delay: 450
        ToolTip.text: !portRow.port || !portRow.port.available
            ? "This control is inactive until calibration reports meaningful travel."
            : portRow.output && root.selectedSource && !portRow.compatible
                ? "Choose an output compatible with the selected source."
                : portRow.output && root.connectionPreview && root.connectionPreview.portId === String(portRow.port.id || "")
                    ? root.connectionPreview.message
                : portRow.output ? "Choose this destination for the selected source."
                                 : "Select this physical source, then choose a compatible destination."
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: legacy ? 0 : 2
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { text: "Signal Flow"; color: root.textStrong; font.pixelSize: 22; font.bold: true; font.family: root.themeTokens.displayFont }
                Text {
                    text: "Device Rig: " + (root.graph.deviceRigName || "Profile-local input scope")
                        + " · Profile: " + (root.graph.profileName || "the active profile")
                    color: root.textMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    visible: Boolean(root.graph.editingDiffersFromEffective)
                    text: "Effective runtime profile: " + (root.graph.effectiveProfileName || "unknown")
                        + " · editing ownership remains on " + (root.graph.profileName || "the selected profile")
                    color: root.warning
                    font.pixelSize: 9
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            FlowButton { text: "Profile…"; helpText: "Choose the active profile whose durable Signal Flow topology is being edited."; onClicked: profileContextMenu.open() }
            FlowButton { text: "Device Rig…"; helpText: "Choose the Device Rig context. A multi-device rig stays visible until you select one input card to edit."; onClicked: rigContextMenu.open() }
            FlowButton { text: "Configured"; accent: root.viewMode === "configured"; onClicked: root.viewMode = "configured" }
            FlowButton { text: "Effective"; accent: root.viewMode === "effective"; onClicked: root.viewMode = "effective" }
            FlowButton { text: "Undo"; enabled: backendObject.signalFlowCanUndo; onClicked: root.showResult(backendObject.signalFlowUndo(Number(root.graph.revision || 0)), "Undo was not applied.") }
            FlowButton { text: "Redo"; enabled: backendObject.signalFlowCanRedo; onClicked: root.showResult(backendObject.signalFlowRedo(Number(root.graph.revision || 0)), "Redo was not applied.") }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: toolbar.implicitHeight + 16
            radius: root.themeTokens.panelRadius || 6
            color: root.panel
            border.color: root.border
            border.width: 1
            RowLayout {
                id: toolbar
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                TextField {
                    id: searchField
                    Layout.preferredWidth: 190
                    Layout.fillWidth: root.width < 980
                    implicitHeight: 30
                    placeholderText: "Find port or route"
                    color: root.text
                    placeholderTextColor: root.textMuted
                    selectByMouse: true
                    onTextChanged: root.searchText = text
                    background: Rectangle { radius: root.themeTokens.controlRadius || 4; color: root.control; border.color: root.border }
                }
                FlowButton {
                    visible: root.searchText.length > 0
                    text: "Focus result" + (root.searchResultCount() > 1 ? " (" + root.searchResultCount() + ")" : "")
                    helpText: "Center the first matching route or port and keep the relevant graph context visible."
                    onClicked: root.focusSearchResult()
                }
                Repeater {
                    model: [ { label: "All", key: "all" }, { label: "Axes", key: "axis" }, { label: "Buttons", key: "button" }, { label: "POV", key: "pov" }, { label: "Native POV", key: "native-pov" } ]
                    delegate: FlowButton { required property var modelData; text: modelData.label; accent: root.portFilter === modelData.key; onClicked: root.portFilter = modelData.key }
                }
                FlowButton {
                    text: "State: " + (root.routeStateFilter === "all" ? "All" : root.routeStateFilter)
                    helpText: "Filter ports and routes by mapped state, problems, or current activity."
                    onClicked: stateFilterMenu.open()
                }
                Item { Layout.fillWidth: true }
                FlowButton { text: root.signalFocus ? "Focus: on" : "Signal focus"; accent: root.signalFocus; helpText: "Dim inactive routes and sample bounded live telemetry."; onClicked: { root.signalFocus = !root.signalFocus; if (root.signalFocus) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
                FlowButton { text: root.liveMode ? "Live: on" : "Live"; accent: root.liveMode; helpText: "Sample bounded latest telemetry and illuminate active routes without changing MappingWorker behavior."; onClicked: { root.liveMode = !root.liveMode; if (root.liveMode) root.liveTelemetry = backendObject.signalFlowLiveTelemetry() } }
                FlowButton { text: root.xrayMode ? "X-ray: on" : "X-ray"; accent: root.xrayMode; helpText: "Isolate topology related to the selected route or source without changing mapping."; onClicked: root.keyboardAction("xray") }
                FlowButton { text: "Learn input"; helpText: "Move or press a physical control first, then choose one of the highlighted compatible destinations."; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: root.startSignalFlowSourceLearning() }
                FlowButton { text: "Learn destination"; helpText: "Choose a virtual destination first, then move the physical control that should drive it."; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: learnDialog.open() }
                FlowButton { text: "Alias"; helpText: "Assign a profile-local friendly name without changing the vJoy target."; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: aliasDialog.open() }
                FlowButton { text: "Fit"; helpText: "Fit the current workspace into the graph viewport. Shortcut: Home."; onClicked: root.keyboardAction("fit") }
                FlowButton { text: "Auto-layout"; helpText: "Arrange unpinned cards with the stable bounded layout."; enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked); onClicked: root.applyAutoLayout() }
                FlowButton { text: root.graph.workspace && root.graph.workspace.layoutLocked ? "Unlock layout" : "Lock layout"; helpText: "Prevent accidental card dragging while preserving each saved placement."; onClicked: root.toggleLayoutLocked() }
                FlowButton { text: root.snapToGridEnabled ? "Snap to Grid: on" : "Snap to Grid: off"; helpText: "Suggest nearby grid or card alignment on release. Hold Alt for one exact free placement."; onClicked: root.toggleSnapToGrid() }
                FlowButton { text: "Density: " + ((root.graph.workspace && root.graph.workspace.densityMode) || "detailed"); helpText: "Cycle detailed, compact, and overview port density. Shortcut: D."; onClicked: root.cycleDensityMode() }
                FlowButton { text: "Wires: " + ((root.graph.workspace && root.graph.workspace.wireStyle) || "smooth"); helpText: "Switch between smooth and orthogonal cached wire rendering."; onClicked: root.toggleWireStyle() }
                FlowButton { text: root.reducedMotion ? "Reduced motion" : "Full motion"; helpText: "Disable movement animation while keeping topology changes visible."; onClicked: root.reducedMotion = !root.reducedMotion }
                FlowButton { text: "Defaults"; helpText: "Preview deterministic mappings for unassigned routes before applying them."; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: root.previewDefaults("unassigned") }
                FlowButton { text: "Replace all"; dangerAction: true; helpText: "Preview every replacement before rewriting current routes."; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: root.previewDefaults("replace-all") }
                FlowButton { text: "−"; Accessible.name: "Zoom out"; helpText: "Zoom out. Shortcut: Ctrl+-"; onClicked: root.keyboardAction("zoom-out") }
                Text { text: Math.round(root.zoom * 100) + "%"; color: root.textMuted; font.pixelSize: 10 }
                FlowButton { text: "+"; Accessible.name: "Zoom in"; helpText: "Zoom in. Shortcut: Ctrl++"; onClicked: root.keyboardAction("zoom-in") }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.feedback.length > 0 || root.viewMode === "effective"
            implicitHeight: feedbackText.implicitHeight + 14
            radius: root.themeTokens.controlRadius || 4
            color: root.feedbackError ? Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.16)
                : Qt.rgba((root.viewMode === "effective" ? root.graphPreview : root.ready).r,
                          (root.viewMode === "effective" ? root.graphPreview : root.ready).g,
                          (root.viewMode === "effective" ? root.graphPreview : root.ready).b, 0.12)
            border.color: root.feedbackError ? root.danger : (root.viewMode === "effective" ? root.graphPreview : root.ready)
            border.width: 1
            Text {
                id: feedbackText
                anchors.fill: parent
                anchors.margins: 7
                text: root.feedback.length > 0 ? root.feedback : root.graph.effectiveSummary
                color: root.feedbackError ? root.danger : root.text
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: Math.max(190, Math.min(270, root.width * 0.23))
                Layout.fillHeight: true
                radius: root.themeTokens.panelRadius || 6
                color: root.panel
                border.color: root.border
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Text { text: "INPUT PORTS"; color: root.graphLabel; font.pixelSize: 10; font.bold: true; font.family: root.themeTokens.telemetryFont }
                    Text { text: root.sourceLabel(); color: root.selectedSource && root.selectedSource.kind ? root.graphOutput : root.textMuted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 5
                            Repeater {
                                model: root.portGroups("input")
                                delegate: Column {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 2
                                    readonly property bool collapsed: root.groupCollapsed("input", modelData.group)
                                    Row {
                                        width: parent.width
                                        spacing: 4
                                        Text { width: parent.width - 35; text: modelData.group + " · " + root.portsForGroup("input", modelData.group, 999).length; color: root.graphLabel; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                        Button {
                                            width: 28; height: 20; padding: 0
                                            text: parent.parent.collapsed ? "+" : "−"
                                            Accessible.name: (parent.parent.collapsed ? "Expand " : "Collapse ") + modelData.group
                                            onClicked: root.setGroupCollapsed("input", modelData.group, !parent.parent.collapsed)
                                            contentItem: Text { text: parent.text; color: root.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.bold: true }
                                            background: Rectangle { radius: 3; color: parent.hovered ? root.controlHover : root.control; border.color: root.border }
                                        }
                                    }
                                    Repeater {
                                        visible: !parent.collapsed
                                        model: parent.collapsed ? [] : root.portsForGroup("input", modelData.group,
                                            root.canvasPortLimit)
                                        delegate: PortRow {
                                            required property var modelData
                                            width: parent.width
                                            port: modelData
                                            selected: root.selectedSource && root.selectedSource.id === modelData.id
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: root.visibleInputPorts().length === 0
                                width: parent.width
                                text: "No input ports match this filter."
                                color: root.textMuted
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: root.themeTokens.panelRadius || 6
                color: root.graphBackground
                border.color: root.graphFrame
                border.width: 1
                clip: true
                Flickable {
                    id: graphViewport
                    objectName: "signalFlowGraphViewport"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    contentWidth: graphScene.width * root.zoom
                    contentHeight: graphScene.height * root.zoom
                    boundsBehavior: Flickable.StopAtBounds
                    onMovementEnded: { root.workspaceDirty = true; workspaceSaveTimer.restart() }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Item {
                        id: graphScene
                        width: 1880
                        height: 980
                        scale: root.zoom
                        transformOrigin: Item.TopLeft
                        Canvas {
                            id: wireCanvas
                            anchors.fill: parent
                            antialiasing: true
                            // Geometry is rebuilt by root.rebuildWireGeometry only
                            // when canonical topology or card placement changes.
                            // This paint pass intentionally consumes the cache so
                            // live samples alter color/weight, never path shape.
                            function drawWire(ctx, startX, startY, endX, endY, color, lineWidth, alpha,
                                              lane, dashed, reveal, hasDetour, detourY, underCard) {
                                const progress = reveal === undefined ? 1 : Math.max(0, Math.min(1, Number(reveal)))
                                const detour = Boolean(hasDetour)
                                ctx.save()
                                if (progress < 0.999) {
                                    // All canonical endpoints flow left-to-right by default.
                                    // A dragged card can invert that convention, in which case
                                    // the clipped reveal still remains bounded and legible.
                                    const left = Math.min(startX, endX) - 8
                                    const span = Math.abs(endX - startX) * progress + 16
                                    ctx.beginPath()
                                    ctx.rect(left, 0, span, height)
                                    ctx.clip()
                                }
                                const path = function() {
                                    const direction = endX >= startX ? 1 : -1
                                    ctx.beginPath()
                                    ctx.moveTo(startX, startY)
                                    if (root.graph.workspace && root.graph.workspace.wireStyle === "orthogonal") {
                                        if (detour) {
                                            const stub = Math.max(42, Math.min(120, Math.abs(endX - startX) * 0.22))
                                            ctx.lineTo(startX + direction * stub, startY)
                                            ctx.lineTo(startX + direction * stub, detourY)
                                            ctx.lineTo(endX - direction * stub, detourY)
                                            ctx.lineTo(endX - direction * stub, endY)
                                            ctx.lineTo(endX, endY)
                                        } else {
                                            const laneX = Math.round((startX + endX) * 0.5 + lane * 12)
                                            ctx.lineTo(laneX, startY)
                                            ctx.lineTo(laneX, endY)
                                            ctx.lineTo(endX, endY)
                                        }
                                    } else if (detour) {
                                        const middleX = (startX + endX) * 0.5
                                        ctx.bezierCurveTo(startX + direction * 86, startY,
                                            startX + direction * 118, detourY, middleX, detourY)
                                        ctx.bezierCurveTo(endX - direction * 118, detourY,
                                            endX - direction * 86, endY, endX, endY)
                                    } else {
                                        const offset = 148 + lane * 10
                                        ctx.bezierCurveTo(startX + offset, startY, endX - offset, endY, endX, endY)
                                    }
                                }
                                // The quiet clearance pass makes every overlap an obvious
                                // overpass rather than a false junction. Cards are painted
                                // above this canvas, so dense fallback legs read as under-card.
                                ctx.strokeStyle = root.graphBackground
                                ctx.lineWidth = lineWidth + 3.2
                                ctx.globalAlpha = alpha * 0.96
                                ctx.setLineDash([])
                                path()
                                ctx.stroke()
                                ctx.strokeStyle = color
                                ctx.lineWidth = lineWidth
                                ctx.globalAlpha = alpha * (underCard ? 0.72 : 1)
                                ctx.setLineDash(dashed ? [5, 3] : [])
                                path()
                                ctx.stroke()
                                ctx.restore()
                            }
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = root.graphGrid
                                ctx.globalAlpha = 0.38
                                ctx.lineWidth = 1
                                for (let x = 0; x < width; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke() }
                                for (let y = 0; y < height; y += 40) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke() }
                                ctx.globalAlpha = 1
                                const retiring = root.retiringWireGeometry || []
                                for (let retiredIndex = 0; retiredIndex < retiring.length; ++retiredIndex) {
                                    const retired = retiring[retiredIndex]
                                    const retiredSegments = retired.segments || []
                                    for (let segmentIndex = 0; segmentIndex < retiredSegments.length; ++segmentIndex) {
                                        const segment = retiredSegments[segmentIndex]
                                        drawWire(ctx, segment.startX, segment.startY, segment.endX, segment.endY,
                                            root.graphLabel, 1.8, 0.62 * (1 - root.wireRetire),
                                            root.stableLane(retired.routeId, 5) - 2, true, 1 - root.wireRetire,
                                            segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                const geometry = root.wireGeometry || []
                                for (let routeIndex = 0; routeIndex < geometry.length; ++routeIndex) {
                                    const entry = geometry[routeIndex]
                                    const route = entry.route
                                    if (root.viewMode === "effective" && !route.effective) continue
                                    const color = root.selectedRoute && root.selectedRoute.id === route.id
                                        ? root.graphPreview : route.effective ? root.graphOutput : root.graphInput
                                    const live = root.routeIsLive(route)
                                    const width = root.selectedRoute && root.selectedRoute.id === route.id ? 3 : live ? 2.75 : 2
                                    const alpha = root.routeVisualAlpha(route, live)
                                    const dashed = root.routeHasProblem(route)
                                    const entryReveal = root.wireIsAppearing(entry.routeId)
                                        ? root.wireAppear : root.wireReveal
                                    if (entry.drawBundleTrunk) {
                                        const trunkEndX = entry.bundleX
                                        drawWire(ctx, entry.startX, entry.startY, trunkEndX, entry.startY, color,
                                            Math.min(5.0, width + entry.bundleCount * 0.35), alpha, 0, true,
                                            entryReveal, false, 0, false)
                                        ctx.save()
                                        ctx.globalAlpha = alpha * entryReveal
                                        ctx.fillStyle = root.graphLabel
                                        ctx.font = "bold 9px sans-serif"
                                        ctx.fillText(String(entry.bundleCount) + "×", trunkEndX + 5, entry.startY - 5)
                                        ctx.restore()
                                    }
                                    const segments = entry.segments || []
                                    for (let segmentIndex = 0; segmentIndex < segments.length; ++segmentIndex) {
                                        const segment = segments[segmentIndex]
                                        const selectedSegment = String(root.selectedSegmentId || "")
                                            === String(segment.routeSegmentId || "")
                                        const pendingSegment = String(root.pendingProcessorSegmentId || "")
                                            === String(segment.routeSegmentId || "")
                                        const segmentColor = selectedSegment || pendingSegment ? root.warning : color
                                        const segmentWidth = selectedSegment || pendingSegment ? Math.max(3.8, width) : width
                                        drawWire(ctx, segment.startX, segment.startY, segment.endX, segment.endY,
                                            segmentColor, segmentWidth, alpha, root.stableLane(route.id, 5) - 2, dashed,
                                            entryReveal, segment.hasDetour, segment.detourY, segment.underCard)
                                    }
                                }
                                if (root.dragWire && root.dragWire.active) {
                                    const input = root.nodeFor("input")
                                    const inputPosition = root.nodePosition(input, 80, 160)
                                    const sourceId = String(root.dragWire.source && root.dragWire.source.id || "")
                                    const startX = inputPosition.x + 230
                                    const startY = inputPosition.y + 52 + root.stableLane(sourceId, 13) * 6
                                    drawWire(ctx, startX, startY, Number(root.dragWire.x || startX),
                                        Number(root.dragWire.y || startY), root.graphPreview, 2.5, 0.92, 0, true,
                                        1, false, 0, false)
                                    ctx.save()
                                    ctx.fillStyle = root.graphPreview
                                    ctx.globalAlpha = 0.95
                                    ctx.beginPath()
                                    ctx.arc(Number(root.dragWire.x || startX), Number(root.dragWire.y || startY), 5, 0, Math.PI * 2)
                                    ctx.fill()
                                    ctx.restore()
                                }
                            }
                        }
                        // Pointer and drop interaction query this same cached geometry that
                        // Canvas paints.  There is no invisible route list or surrogate lane.
                        MouseArea {
                            id: processorWireInteractionLayer
                            anchors.fill: parent
                            z: 1
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                const hit = root.hitProcessorWire(mouse.x, mouse.y)
                                if (!hit || !hit.route) return
                                root.selectedRoute = hit.route
                                root.selectedSegmentId = String(hit.routeSegmentId || "")
                                root.selectedNode = ({})
                                root.selectedSource = ({})
                                if (mouse.button === Qt.RightButton) {
                                    routeContextMenu.targetRoute = hit.route
                                    routeContextMenu.open()
                                }
                            }
                        }
                        DropArea {
                            anchors.fill: parent
                            z: 1
                            keys: ["signal-flow-processor"]
                            onEntered: function(drag) {
                                const hit = root.hitProcessorWire(drag.x, drag.y)
                                if (hit) root.previewProcessorTarget(hit.route, String(hit.routeSegmentId || ""))
                            }
                            onPositionChanged: function(drag) {
                                const hit = root.hitProcessorWire(drag.x, drag.y)
                                if (hit) root.previewProcessorTarget(hit.route, String(hit.routeSegmentId || ""))
                            }
                            onExited: {
                                root.pendingProcessorRouteId = ""
                                root.pendingProcessorSegmentId = ""
                            }
                            onDropped: function(drop) {
                                const hit = root.hitProcessorWire(drop.x, drop.y)
                                root.pendingProcessorRouteId = ""
                                root.pendingProcessorSegmentId = ""
                                if (!hit || !hit.route || !hit.routeSegmentId || !drop.source || !drop.source.processorKind) return
                                root.selectedRoute = hit.route
                                root.selectedSegmentId = String(hit.routeSegmentId)
                                root.selectedNode = ({})
                                const result = backendObject.signalFlowInsertProcessor(String(hit.routeSegmentId),
                                    String(drop.source.processorKind), Number(root.graph.revision || 0))
                                root.showResult(result, "Processor was not inserted.")
                                if (result && result.success) processorDialog.close()
                                drop.accepted = true
                            }
                        }
                        // The proposal is visible but deliberately non-interactive: while
                        // pressed, the real card follows the pointer exactly above it.
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
                                color: root.graphPreview; opacity: 0.38
                            }
                            Rectangle {
                                visible: isFinite(Number(root.nodeSnapPreview.guideY))
                                x: 0; y: Number(root.nodeSnapPreview.guideY || 0) - 0.5
                                width: parent.width; height: 1
                                color: root.graphPreview; opacity: 0.38
                            }
                            Rectangle {
                                x: Number(root.nodeSnapPreview.x || 0)
                                y: Number(root.nodeSnapPreview.y || 0)
                                width: Number(root.nodeSnapPreview.width || 0)
                                height: Number(root.nodeSnapPreview.height || 0)
                                radius: root.themeTokens.controlRadius || 5
                                color: "transparent"
                                border.width: 1
                                border.color: root.graphPreview
                                opacity: 0.62
                            }
                        }
                        Repeater {
                            model: root.graph.nodes || []
                            delegate: Rectangle {
                                id: flowNode
                                required property var modelData
                                readonly property var node: modelData
                                x: Number(node.x || 0)
                                y: Number(node.y || 0)
                                onXChanged: root.noteNodePosition(node.objectId, x, y)
                                onYChanged: root.noteNodePosition(node.objectId, x, y)
                                Behavior on x { enabled: !root.nodeIsSnapDragging(node); NumberAnimation { duration: root.nodeMotionDuration(node); easing.type: Easing.OutCubic } }
                                Behavior on y { enabled: !root.nodeIsSnapDragging(node); NumberAnimation { duration: root.nodeMotionDuration(node); easing.type: Easing.OutCubic } }
                                width: node.kind === "processor" ? 155 : 238
                                height: node.kind === "processor"
                                    ? 82 + Math.max(0, Number(node.sharedChannelCount || 0) - 1) * 20
                                    : Math.min(620, 86 + portColumn.implicitHeight)
                                Behavior on height { NumberAnimation { duration: root.motionStructuralDuration; easing.type: Easing.OutCubic } }
                                z: 2
                                opacity: root.xrayMode ? 0.58 : 1.0
                                Behavior on opacity { NumberAnimation { duration: root.motionFastDuration; easing.type: Easing.OutCubic } }
                                radius: root.themeTokens.controlRadius || 5
                                color: node.kind === "input" ? Qt.rgba(root.graphInput.r, root.graphInput.g, root.graphInput.b, 0.10)
                                     : node.kind === "output" ? Qt.rgba(root.graphOutput.r, root.graphOutput.g, root.graphOutput.b, 0.10)
                                     : root.raisedPanel
                                border.width: 1
                                border.color: node.kind === "input" ? root.graphInput : node.kind === "output" ? root.graphOutput : root.borderStrong
                                clip: true
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4
                                    Row {
                                        width: parent.width
                                        spacing: 5
                                        Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: node.connected ? root.ready : root.warning }
                                        Text { width: parent.width - 16; text: node.label; color: root.textStrong; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight }
                                    }
                                    Text { width: parent.width; text: node.detail || ""; color: root.textMuted; font.pixelSize: 9; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                                    Column {
                                        visible: node.kind === "processor" && Boolean(node.shared)
                                        width: parent.width
                                        spacing: 1
                                        Text { text: "SHARED · " + Number(node.sharedChannelCount || 0) + " CHANNELS"; color: root.graphOutput; font.pixelSize: 8; font.bold: true }
                                        Repeater {
                                            model: Math.min(4, Number(node.sharedChannelCount || 0))
                                            delegate: Row {
                                                spacing: 3
                                                Rectangle { width: 5; height: 5; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: root.graphInput }
                                                Text { text: "channel " + (index + 1) + "  →"; color: root.textMuted; font.pixelSize: 8 }
                                                Rectangle { width: 5; height: 5; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: root.graphOutput }
                                            }
                                        }
                                    }
                                    Column {
                                        id: portColumn
                                        visible: node.kind === "input" || node.kind === "output"
                                        width: parent.width
                                        spacing: 1
                                        Repeater {
                                            model: {
                                                const ports = node.ports || []
                                                return ports.filter(function(port) { return root.portVisibleInNode(port, node.kind === "output") })
                                                    .slice(0, root.canvasPortLimit)
                                            }
                                            delegate: PortRow {
                                                required property var modelData
                                                width: parent.width
                                                port: modelData
                                                output: node.kind === "output"
                                                selected: !output && root.selectedSource && root.selectedSource.id === modelData.id
                                                compatible: output && root.sourceIsCompatible(modelData)
                                            }
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    propagateComposedEvents: true
                                    drag.target: !root.autoLayoutMotionActive && root.viewMode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked) ? flowNode : null
                                    drag.axis: Drag.XAndYAxis
                                    enabled: true
                                    onPressed: function(mouse) {
                                        if (root.autoLayoutMotionActive) {
                                            mouse.accepted = false
                                            return
                                        }
                                        root.beginNodeSnapDrag(node, Boolean(mouse.modifiers & Qt.AltModifier))
                                        mouse.accepted = false
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (drag.active)
                                            root.updateNodeSnapPreview(node, flowNode.x, flowNode.y,
                                                Boolean(mouse.modifiers & Qt.AltModifier))
                                        mouse.accepted = false
                                    }
                                    onReleased: function(mouse) {
                                        if (drag.active) {
                                            const settled = root.finishNodeSnapDrag(node, flowNode.x, flowNode.y,
                                                Boolean(mouse.modifiers & Qt.AltModifier))
                                            flowNode.x = Number(settled.x)
                                            flowNode.y = Number(settled.y)
                                            root.saveNodePlacement(node, Number(settled.x), Number(settled.y), Boolean(node.pinned))
                                            root.workspaceDirty = true
                                        } else root.cancelNodeSnapDrag()
                                        mouse.accepted = false
                                    }
                                    onClicked: function(mouse) {
                                        if (!drag.active) root.selectNode(node)
                                        mouse.accepted = false
                                    }
                                    onDoubleClicked: function(mouse) {
                                        if (!drag.active && node.kind === "processor") root.openNodeSettings(node)
                                        else if (!drag.active) root.openCardSettings(node)
                                        mouse.accepted = false
                                    }
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: function(eventPoint, button) {
                                        nodeContextMenu.targetNode = node
                                        nodeContextMenu.open()
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    text: root.connectionPreview && root.connectionPreview.message
                        ? root.connectionPreview.message
                        : "Click-click or drag to route · drag a processor chip onto a visible wire · right-click a wire for processing · right-click a card to pin · Delete disconnects"
                    color: root.graphLabel
                    font.pixelSize: 9
                }
            }

            Rectangle {
                Layout.preferredWidth: Math.max(205, Math.min(310, root.width * 0.25))
                Layout.fillHeight: true
                radius: root.themeTokens.panelRadius || 6
                color: root.panel
                border.color: root.border
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7
                    Text { text: root.selectedNode && root.selectedNode.id ? "CARD / INSPECTOR" : "OUTPUT / INSPECTOR"; color: root.graphLabel; font.pixelSize: 10; font.bold: true; font.family: root.themeTokens.telemetryFont }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedRoute && root.selectedRoute.id
                            ? root.selectedRoute.sourceLabel + " → " + root.selectedRoute.destinationLabel
                            : root.selectedNode && root.selectedNode.id ? root.selectedNode.label
                            : "Choose a compatible output after selecting a source."
                        color: root.selectedRoute && root.selectedRoute.id || root.selectedNode && root.selectedNode.id ? root.textStrong : root.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedNode && root.selectedNode.id)
                        Layout.fillWidth: true
                        text: (root.selectedNode.detail || "") + (root.selectedNode.capabilitySummary
                            ? "\n" + root.selectedNode.capabilitySummary : "")
                        color: root.selectedNode && root.selectedNode.missingReference ? root.warning : root.textMuted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedNode && root.selectedNode.id)
                        Layout.fillWidth: true
                        text: Number(root.selectedNode.routeCount || 0) + " configured route"
                            + (Number(root.selectedNode.routeCount || 0) === 1 ? "" : "s")
                            + (root.selectedNode.connected ? " · ready" : " · offline / unavailable")
                        color: root.selectedNode && root.selectedNode.connected ? root.ready : root.warning
                        font.pixelSize: 9
                        font.bold: true
                    }
                    FlowButton {
                        visible: Boolean(root.selectedNode && root.selectedNode.kind === "input"
                            && root.selectedNode.scopeEditable === false && root.selectedNode.controllerRecordId)
                        text: "Edit this input scope"
                        helpText: "Make this saved Device Rig member the explicit input scope before changing its routes."
                        Layout.fillWidth: true
                        onClicked: root.useInputScope(root.selectedNode)
                    }
                    FlowButton {
                        visible: Boolean(root.selectedNode && (root.selectedNode.kind === "input"
                            || root.selectedNode.kind === "output"))
                        text: root.selectedNode && root.selectedNode.kind === "input"
                            ? "Open input setup" : "Open output setup"
                        helpText: "Open Devices & setup while preserving this Signal Flow card selection for return."
                        Layout.fillWidth: true
                        onClicked: root.openCardSettings(root.selectedNode)
                    }
                    Text {
                        visible: Boolean(root.selectedRoute && root.selectedRoute.id)
                        Layout.fillWidth: true
                        text: root.selectedRoute && root.selectedRoute.processors && root.selectedRoute.processors.length > 0
                            ? "Processors: " + root.selectedRoute.processors.length
                                + (root.selectedRoute.viaNodeId ? "; explicit mixer included." : " projected from this axis configuration.")
                            : "No separate processor node is active for this route."
                        color: root.textMuted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedRoute && root.processorDetail("adaptive-response").settingsSummary)
                        Layout.fillWidth: true
                        text: "Adaptive Response · " + String(root.processorDetail("adaptive-response").settingsSummary)
                        color: root.graphLabel
                        font.pixelSize: 9
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedRoute && root.selectedRoute.id)
                        Layout.fillWidth: true
                        text: (root.selectedRoute.health || "ready").toUpperCase().replace("-", " ")
                            + " · " + (root.selectedRoute.healthDetail || "")
                        color: root.selectedRoute.health === "ready" ? root.ready
                            : root.selectedRoute.health === "conflict" ? root.warning : root.textMuted
                        font.pixelSize: 9
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedRoute && root.selectedRoute.id && (root.signalFocus || root.liveMode))
                        Layout.fillWidth: true
                        text: "Live sample: " + Number(root.routeLive(root.selectedRoute).value || 0).toFixed(3)
                            + (root.routeIsLive(root.selectedRoute) ? " · moving" : " · steady")
                        color: root.routeIsLive(root.selectedRoute) ? root.ready : root.textMuted
                        font.pixelSize: 9
                    }
                    FlowButton { text: "Explain this route"; visible: Boolean(root.selectedRoute && root.selectedRoute.id); Layout.fillWidth: true; onClicked: root.explainSelected() }
                    FlowButton {
                        text: "Open Curve Editor"
                        visible: Boolean(root.selectedRoute && root.routeHasProcessor(root.selectedRoute, "curve"))
                        helpText: "Open the authoritative Curve Editor for this source axis and preserve this Signal Flow selection for return."
                        Layout.fillWidth: true
                        onClicked: root.openFullSettings("curve", root.selectedRoute)
                    }
                    FlowButton {
                        text: "Open Adaptive Response"
                        visible: Boolean(root.selectedRoute && root.routeHasProcessor(root.selectedRoute, "adaptive-response"))
                        helpText: "Open the authoritative Adaptive Response editor for this source axis and preserve this Signal Flow selection for return."
                        Layout.fillWidth: true
                        onClicked: root.openFullSettings("adaptive-response", root.selectedRoute)
                    }
                    FlowButton {
                        text: root.selectedNode && root.selectedNode.shared ? "Remove this shared channel" : "Remove processor"
                        dangerAction: true
                        visible: Boolean(root.selectedNode && root.selectedNode.kind === "processor")
                        enabled: root.viewMode === "configured"
                        Layout.fillWidth: true
                        onClicked: root.removeSelectedProcessor()
                    }
                    FlowButton { text: "Processor palette"; visible: Boolean(root.selectedRoute && root.selectedRoute.id); enabled: root.viewMode === "configured"; Layout.fillWidth: true; onClicked: processorDialog.open() }
                    FlowButton { text: "Share active processor…"; visible: Boolean(root.selectedRoute && root.selectedRoute.id); enabled: root.viewMode === "configured"; Layout.fillWidth: true; onClicked: root.openShareProcessorDialog() }
                    FlowButton { text: "Disconnect route"; dangerAction: true; visible: Boolean(root.selectedRoute && root.selectedRoute.id); enabled: root.viewMode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.border }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 5
                            Repeater {
                                model: root.portGroups("output")
                                delegate: Column {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 2
                                    readonly property bool collapsed: root.groupCollapsed("output", modelData.group)
                                    Row {
                                        width: parent.width
                                        spacing: 4
                                        Text { width: parent.width - 35; text: modelData.group + " · " + root.portsForGroup("output", modelData.group, 999).length; color: root.graphLabel; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                        Button {
                                            width: 28; height: 20; padding: 0
                                            text: parent.parent.collapsed ? "+" : "−"
                                            Accessible.name: (parent.parent.collapsed ? "Expand " : "Collapse ") + modelData.group
                                            onClicked: root.setGroupCollapsed("output", modelData.group, !parent.parent.collapsed)
                                            contentItem: Text { text: parent.text; color: root.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.bold: true }
                                            background: Rectangle { radius: 3; color: parent.hovered ? root.controlHover : root.control; border.color: root.border }
                                        }
                                    }
                                    Repeater {
                                        visible: !parent.collapsed
                                        model: parent.collapsed ? [] : root.portsForGroup("output", modelData.group,
                                            root.canvasPortLimit)
                                        delegate: PortRow {
                                            required property var modelData
                                            width: parent.width
                                            port: modelData
                                            output: true
                                            compatible: root.sourceIsCompatible(modelData)
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: root.visibleOutputPorts().length === 0
                                width: parent.width
                                text: "No output ports match this filter."
                                color: root.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }
    }

    Timer {
        id: workspaceSaveTimer
        interval: 650
        repeat: false
        onTriggered: root.saveWorkspace()
    }

    Menu {
        id: profileContextMenu
        title: "Choose editing profile"
        Repeater {
            model: backendObject ? backendObject.profiles : []
            delegate: MenuItem {
                required property var modelData
                text: (modelData.displayName || modelData.name || "Profile")
                    + (modelData.effective ? " · effective" : "")
                checkable: true
                checked: String(modelData.id || "") === String(root.graph.profileId || "")
                enabled: modelData.enabled !== false
                onTriggered: root.selectProfileContext(modelData)
            }
        }
    }

    Menu {
        id: rigContextMenu
        title: "Choose Device Rig context"
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
        id: stateFilterMenu
        title: "Route state"
        MenuItem { text: "All routes"; checkable: true; checked: root.routeStateFilter === "all"; onTriggered: root.routeStateFilter = "all" }
        MenuItem { text: "Mapped"; checkable: true; checked: root.routeStateFilter === "mapped"; onTriggered: root.routeStateFilter = "mapped" }
        MenuItem { text: "Unmapped"; checkable: true; checked: root.routeStateFilter === "unmapped"; onTriggered: root.routeStateFilter = "unmapped" }
        MenuItem { text: "Problems"; checkable: true; checked: root.routeStateFilter === "problems"; onTriggered: root.routeStateFilter = "problems" }
        MenuItem { text: "Active now"; checkable: true; checked: root.routeStateFilter === "active"; onTriggered: root.routeStateFilter = "active" }
    }

    Menu {
        id: nodeContextMenu
        property var targetNode: ({})
        title: targetNode && targetNode.label ? targetNode.label : "Signal Flow node"
        MenuItem {
            text: "Inspect card"
            enabled: Boolean(nodeContextMenu.targetNode && nodeContextMenu.targetNode.id)
            onTriggered: root.selectNode(nodeContextMenu.targetNode)
        }
        MenuItem {
            text: nodeContextMenu.targetNode && nodeContextMenu.targetNode.semantic === "curve"
                ? "Open Curve Editor" : "Open Adaptive Response"
            visible: Boolean(nodeContextMenu.targetNode && nodeContextMenu.targetNode.kind === "processor"
                && (nodeContextMenu.targetNode.semantic === "curve"
                    || nodeContextMenu.targetNode.semantic === "adaptive-response"))
            onTriggered: root.openNodeSettings(nodeContextMenu.targetNode)
        }
        MenuItem {
            text: nodeContextMenu.targetNode && nodeContextMenu.targetNode.kind === "input"
                ? "Open input setup" : "Open output setup"
            visible: Boolean(nodeContextMenu.targetNode && (nodeContextMenu.targetNode.kind === "input"
                || nodeContextMenu.targetNode.kind === "output"))
            onTriggered: root.openCardSettings(nodeContextMenu.targetNode)
        }
        MenuItem {
            text: nodeContextMenu.targetNode && nodeContextMenu.targetNode.pinned ? "Unpin placement" : "Pin placement"
            enabled: Boolean(nodeContextMenu.targetNode && nodeContextMenu.targetNode.objectId)
            onTriggered: root.toggleNodePinned(nodeContextMenu.targetNode)
        }
        MenuItem {
            text: "Center selection"
            enabled: Boolean(root.selectedRoute && root.selectedRoute.id)
            onTriggered: root.focusCurrentSelection()
        }
        MenuSeparator {}
        MenuItem {
            text: "Auto-layout unpinned cards"
            enabled: root.graph.editable && !(root.graph.workspace && root.graph.workspace.layoutLocked)
            onTriggered: root.applyAutoLayout()
        }
    }

    Menu {
        id: routeContextMenu
        objectName: "signalFlowRouteContextMenu"
        property var targetRoute: ({})
        title: targetRoute && targetRoute.sourceLabel
            ? targetRoute.sourceLabel + " → " + targetRoute.destinationLabel : "Signal Flow route"
        MenuItem {
            text: "Insert processing…"
            enabled: root.viewMode === "configured" && Boolean(routeContextMenu.targetRoute && routeContextMenu.targetRoute.id)
            onTriggered: {
                root.selectedRoute = routeContextMenu.targetRoute
                root.selectedSource = ({})
                root.selectedNode = ({})
                processorDialog.open()
            }
        }
        MenuItem {
            text: "Explain this route"
            enabled: Boolean(routeContextMenu.targetRoute && routeContextMenu.targetRoute.id)
            onTriggered: {
                root.selectedRoute = routeContextMenu.targetRoute
                root.selectedSource = ({})
                root.selectedNode = ({})
                root.explainSelected()
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "Disconnect route"
            enabled: root.viewMode === "configured" && Boolean(routeContextMenu.targetRoute && routeContextMenu.targetRoute.id)
            onTriggered: {
                root.selectedRoute = routeContextMenu.targetRoute
                root.selectedSource = ({})
                root.selectedNode = ({})
                root.disconnectSelected()
            }
        }
    }

    Dialog {
        id: replaceDialog
        objectName: "signalFlowReplaceConflictDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        property string destination: ""
        property string destinationPortId: ""
        property string portLabel: ""
        property bool mixerAllowed: true
        title: mixerAllowed ? "Resolve analog input collision" : "Resolve virtual POV collision"
        width: Math.min(470, root.width - 40)
        contentItem: ColumnLayout {
            spacing: 12
            Text { Layout.fillWidth: true; text: "“" + root.sourceLabel() + "” conflicts with the current route into “" + replaceDialog.portLabel + "”. " + (replaceDialog.mixerAllowed ? "Replace moves the existing input; Mixer preserves both inputs with an explicit runtime mode. Both choices can be undone." : "Virtual POV streams cannot be merged; replace the current source or choose another destination."); color: root.text; wrapMode: Text.WordWrap }
            ComboBox {
                id: mixerMode
                Layout.fillWidth: true
                visible: replaceDialog.mixerAllowed
                model: ["average", "sum-clamped", "highest-magnitude"]
                Accessible.name: "Analog mixer mode"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: replaceDialog.close() }
                FlowButton { visible: replaceDialog.mixerAllowed; text: "Create mixer"; onClicked: { const mode = mixerMode.currentText; replaceDialog.close(); root.applyMixer(mode) } }
                FlowButton { text: "Replace route"; accent: true; onClicked: { replaceDialog.close(); root.applyReplacement() } }
            }
        }
    }

    Dialog {
        id: explainDialog
        objectName: "signalFlowExplainDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, root.width - 40)
        title: "Explain this route"
        contentItem: ColumnLayout {
            spacing: 9
            Text { Layout.fillWidth: true; text: root.routeExplanation.summary || "Select a current route to explain it."; color: root.text; wrapMode: Text.WordWrap; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; text: root.routeExplanation.runtimeDetail || ""; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(230, explanationSteps.implicitHeight)
                clip: true
                Column {
                    id: explanationSteps
                    width: parent.availableWidth
                    spacing: 5
                    Repeater {
                        model: root.routeExplanation.steps || []
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width; implicitHeight: stepBody.implicitHeight + 12
                            radius: 4; color: root.insetPanel; border.color: root.border
                            Column { id: stepBody; anchors.fill: parent; anchors.margins: 6; spacing: 2
                                Text { width: parent.width; text: modelData.label || "Signal step"; color: root.textStrong; font.pixelSize: 10; font.bold: true }
                                Text { width: parent.width; text: modelData.detail || ""; color: root.textMuted; font.pixelSize: 9; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
            TextArea {
                visible: technicalDetails.checked
                Layout.fillWidth: true; Layout.preferredHeight: 106
                readOnly: true; text: root.routeExplanation.technicalDetails || ""
                color: root.text; font.family: root.themeTokens.telemetryFont; font.pixelSize: 9
                background: Rectangle { color: root.control; border.color: root.border; radius: 4 }
            }
            RowLayout { Layout.fillWidth: true
                CheckBox { id: technicalDetails; text: "Technical details"; contentItem: Text { text: parent.text; color: root.textMuted; leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 } }
                Item { Layout.fillWidth: true }
                FlowButton { text: "Close"; onClicked: explainDialog.close() }
            }
        }
    }

    Dialog {
        id: processorDialog
        objectName: "signalFlowProcessorPalette"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, root.width - 40)
        title: "Processor palette"
        contentItem: ColumnLayout {
            spacing: 9
            Text { Layout.fillWidth: true; text: "Drag a processor onto its matching visible execution-stage wire, or select that wire then click. Signal Flow rejects visual-only reorders; source-owned processors affect that source’s visible fan-out routes."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            Flow {
                Layout.fillWidth: true
                spacing: 7
                Repeater {
                    model: [
                        { key: "curve", label: "Curve" }, { key: "deadzone", label: "Deadzone" },
                        { key: "center-hold", label: "Center hold" }, { key: "invert", label: "Invert" },
                        { key: "limits", label: "Output limits" }, { key: "adaptive-response", label: "Adaptive Response" }
                    ]
                    delegate: Rectangle {
                        id: processorChip
                        required property var modelData
                        property string processorKind: modelData.key
                        readonly property bool activeProcessor: root.processorEnabled(processorKind)
                        readonly property bool actionAvailable: root.processorActionAvailable(processorKind)
                        width: Math.max(108, chipText.implicitWidth + 20); height: 30; radius: 4
                        color: activeProcessor ? root.graphOutput : root.control
                        opacity: actionAvailable ? 1.0 : 0.54
                        border.color: chipDrag.active ? root.warning : root.borderStrong
                        Text { id: chipText; anchors.centerIn: parent; text: root.processorIsShared(processorKind) ? "Split shared " + modelData.label : activeProcessor ? "Remove " + modelData.label : actionAvailable ? "Add " + modelData.label : "Select stage for " + modelData.label; color: activeProcessor ? root.pageBackground : root.textStrong; font.pixelSize: 10; font.bold: true }
                        Drag.active: chipDrag.active
                        Drag.source: processorChip
                        Drag.keys: ["signal-flow-processor"]
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        DragHandler { id: chipDrag; enabled: root.viewMode === "configured" && !activeProcessor && root.processorCanInsert(processorKind) }
                        MouseArea { anchors.fill: parent; enabled: root.viewMode === "configured" && actionAvailable; onClicked: root.toggleProcessor(processorKind) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Share active processor…"; enabled: root.viewMode === "configured"; onClicked: root.openShareProcessorDialog() }
                FlowButton { text: "Close"; onClicked: processorDialog.close() }
            }
        }
    }

    Dialog {
        id: shareDialog
        objectName: "signalFlowShareProcessorDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(510, root.width - 40)
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
            root.showResult(result, "Shared processor was not created.")
            if (result && result.success) close()
            return Boolean(result && result.success)
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "The first selected route owns the existing processor setting. Signal Flow mirrors that durable setting to every selected axis; split a route later to edit it independently."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            ComboBox {
                id: shareKind
                Layout.fillWidth: true
                model: shareDialog.processorKinds
                textRole: "label"
                Accessible.name: "Processor to share"
                onActivated: {
                    const item = shareDialog.processorKinds[currentIndex]
                    shareDialog.processorKind = item ? String(item.key) : ""
                    shareDialog.resetChoices()
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, shareChoices.implicitHeight)
                clip: true
                Column {
                    id: shareChoices
                    width: parent.availableWidth
                    spacing: 3
                    Repeater {
                        model: shareDialog.choices
                        delegate: CheckBox {
                            required property var modelData
                            width: parent.width
                            checked: Boolean(modelData.selected)
                            text: modelData.label
                            onToggled: shareDialog.setChoice(index, checked)
                            contentItem: Text { text: parent.text; color: root.text; leftPadding: parent.indicator.width + 7; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                    }
                }
            }
            Text { Layout.fillWidth: true; text: "Select at least two distinct physical axis sources. Existing members of a shared processor stay selected when extending it."; color: root.graphLabel; wrapMode: Text.WordWrap; font.pixelSize: 9 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: shareDialog.close() }
                FlowButton {
                    text: "Share processor"
                    accent: true
                    enabled: shareDialog.processorKind.length > 0 && shareDialog.choices.filter(function(choice) { return choice.selected }).length >= 2
                    onClicked: shareDialog.applyShare()
                }
            }
        }
    }

    Dialog {
        id: sourceLearnDialog
        objectName: "signalFlowLearnInputDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(470, root.width - 40)
        title: "Learn physical input"
        onClosed: {
            const learning = backendObject.inputLearning || ({})
            if (Boolean(learning.active) && String(learning.kind || "") === "signal-flow")
                backendObject.cancelInputLearning()
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "Move an axis, press a button, or move a POV direction. Signal Flow will identify the physical source first and then highlight only compatible destinations."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            Text { Layout.fillWidth: true; text: (backendObject.inputLearning.message || "Preparing safe source detection…"); color: root.text; wrapMode: Text.WordWrap; font.pixelSize: 11 }
            Text { visible: String(backendObject.inputLearning.sourceLabel || "").length > 0; Layout.fillWidth: true; text: backendObject.inputLearning.sourceLabel || ""; color: root.ready; font.pixelSize: 11; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Retry"; visible: backendObject.inputLearning.phase === "ambiguous"; onClicked: backendObject.retryInputLearning() }
                FlowButton { text: "Cancel"; onClicked: { backendObject.cancelInputLearning(); sourceLearnDialog.close() } }
            }
        }
    }

    Dialog {
        id: learnDialog
        objectName: "signalFlowLearnDestinationDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, root.width - 40)
        property var choices: []
        title: "Learn destination"
        onOpened: { choices = root.visibleOutputPorts().filter(function(port) { return port.kind === "axis" || port.kind === "button" }) }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "Choose a virtual axis or button, then move the physical control that should drive it. Signal Flow keeps the resulting route highlighted here."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            ComboBox { id: learnChoices; Layout.fillWidth: true; model: learnDialog.choices; textRole: "label"; Accessible.name: "Learn destination" }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: learnDialog.close() }
                FlowButton { text: "Start learning"; accent: true; enabled: learnDialog.choices.length > 0; onClicked: root.startLearning(learnDialog.choices[learnChoices.currentIndex]) }
            }
        }
    }

    Dialog {
        id: aliasDialog
        objectName: "signalFlowAliasDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(440, root.width - 40)
        property var axes: []
        title: "Virtual axis alias"
        onOpened: { axes = root.outputPorts().filter(function(port) { return port.kind === "axis" }); aliasText.text = axes.length > 0 && axes[0].label !== axes[0].technicalLabel ? axes[0].label : "" }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "Aliases are labels only; they never change the vJoy axis identity or runtime route."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            ComboBox { id: aliasAxis; Layout.fillWidth: true; model: aliasDialog.axes; textRole: "technicalLabel"; onCurrentIndexChanged: { const choice = aliasDialog.axes[currentIndex]; aliasText.text = choice && choice.label !== choice.technicalLabel ? choice.label : "" } }
            TextField { id: aliasText; Layout.fillWidth: true; placeholderText: "Optional alias"; color: root.text; selectByMouse: true; background: Rectangle { radius: 4; color: root.control; border.color: root.border } }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: aliasDialog.close() }
                FlowButton { text: "Save alias"; accent: true; enabled: aliasDialog.axes.length > 0; onClicked: { const axis = aliasDialog.axes[aliasAxis.currentIndex]; backendObject.setVirtualAxisAlias(String(axis.technicalLabel), aliasText.text); root.feedback = "Alias saved."; root.feedbackError = false; aliasDialog.close() } }
            }
        }
    }

    Dialog {
        id: mixerDialog
        objectName: "signalFlowMixerDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(500, root.width - 40)
        property string mixerId: ""
        property string routeId: ""
        property string currentMode: "average"
        title: "Explicit analog mixer"
        onOpened: mixerModeEditor.currentIndex = Math.max(0, mixerModeEditor.model.indexOf(currentMode))
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "This mixer owns the named input routes shown on the graph. Its mode changes all of those sources atomically."; color: root.text; wrapMode: Text.WordWrap }
            ComboBox { id: mixerModeEditor; Layout.fillWidth: true; model: ["average", "sum-clamped", "highest-magnitude"]; Accessible.name: "Explicit analog mixer mode" }
            Text { Layout.fillWidth: true; text: "Removing this input disconnects only the selected source. Removing the mixer disconnects every named source; no direct route is silently retained."; color: root.textMuted; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            RowLayout {
                Layout.fillWidth: true
                FlowButton { text: "Cancel"; onClicked: mixerDialog.close() }
                Item { Layout.fillWidth: true }
                FlowButton { text: "Remove this input"; dangerAction: true; enabled: mixerDialog.routeId.length > 0; onClicked: { const result = root.backendObject.signalFlowRemoveMixerInput(mixerDialog.mixerId, mixerDialog.routeId, Number(root.graph.revision || 0)); root.showResult(result, "Mixer input was not removed."); if (result && result.success) mixerDialog.close() } }
                FlowButton { text: "Remove mixer + inputs"; dangerAction: true; onClicked: { const result = root.backendObject.signalFlowRemoveMixer(mixerDialog.mixerId, Number(root.graph.revision || 0)); root.showResult(result, "Mixer was not removed."); if (result && result.success) mixerDialog.close() } }
                FlowButton { text: "Apply mode"; accent: true; onClicked: { const result = root.backendObject.signalFlowSetMixerMode(mixerDialog.mixerId, mixerModeEditor.currentText, Number(root.graph.revision || 0)); root.showResult(result, "Mixer mode was not changed."); if (result && result.success) mixerDialog.close() } }
            }
        }
    }

    Dialog {
        id: defaultsDialog
        objectName: "signalFlowDefaultsPreviewDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, root.width - 40)
        property string mode: "unassigned"
        property var preview: ({})
        title: mode === "replace-all" ? "Preview: replace all axis routes" : "Preview: connect unassigned axes"
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: defaultsDialog.preview.message || "Review the change set before applying it."; color: root.text; wrapMode: Text.WordWrap }
            Text {
                Layout.fillWidth: true
                readonly property var summary: defaultsDialog.preview.summary || ({})
                text: "Added " + Number(summary.added || 0)
                    + " · Kept " + Number(summary.kept || 0)
                    + " · Replaced " + Number(summary.replaced || 0)
                    + " · Mixer changes " + Number(summary.mergeChanges || 0)
                    + " · Ambiguous " + Number(summary.ambiguous || 0)
                    + " · Blocked " + Number(summary.blocked || 0)
                color: root.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(230, defaultsList.implicitHeight)
                clip: true
                Column {
                    id: defaultsList
                    width: parent.availableWidth
                    spacing: 3
                    Repeater {
                        model: defaultsDialog.preview.changes || []
                        delegate: Text { required property var modelData; width: parent.width; text: String(modelData.category || "added").toUpperCase() + " · " + modelData.source + ": " + modelData.from + " → " + modelData.to + " · " + (modelData.pairing || "") + (modelData.offline ? " · OFFLINE / configuration valid, live signal unavailable" : "") + (modelData.reason ? " · " + modelData.reason : ""); color: modelData.category === "blocked" || modelData.category === "ambiguous" ? root.warning : modelData.category === "added" || modelData.category === "replaced" ? root.ready : root.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                    Text { visible: (defaultsDialog.preview.count || 0) === 0; text: "No routes would change."; color: root.ready; font.pixelSize: 10 }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: defaultsDialog.close() }
                FlowButton {
                    text: defaultsDialog.mode === "replace-all" ? "Replace all" : "Apply defaults"
                    accent: true
                    enabled: Boolean(defaultsDialog.preview.success && defaultsDialog.preview.canApply)
                    onClicked: {
                        const result = root.backendObject.signalFlowApplyDefaults(defaultsDialog.mode, Number(defaultsDialog.preview.revision || root.graph.revision || 0))
                        root.showResult(result, "Defaults were not applied.")
                        defaultsDialog.close()
                    }
                }
            }
        }
    }
}
