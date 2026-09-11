import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Signal Flow is deliberately a control-plane editor.  Its graph is a
// projection of AppBackend's persisted mapping state; it neither samples HID
// reports nor maintains a second mapping model in QML.
Item {
    id: root
    objectName: "signalFlowPage"
    anchors.fill: parent

    required property var backendObject
    required property var themeTokens
    property bool legacy: false
    property var graph: backendObject ? backendObject.signalFlowGraph : ({})
    property var selectedSource: ({})
    property var selectedRoute: ({})
    property string viewMode: "configured"
    property string portFilter: "all"
    property string searchText: ""
    property string feedback: ""
    property bool feedbackError: false
    property real zoom: 1.0
    property bool workspaceRestored: false
    property bool workspaceDirty: false

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

    function nodeFor(kind) {
        const nodes = graph.nodes || []
        for (let index = 0; index < nodes.length; ++index) {
            if (nodes[index].kind === kind)
                return nodes[index]
        }
        return ({})
    }
    function inputPorts() { return nodeFor("input").ports || [] }
    function outputPorts() { return nodeFor("output").ports || [] }
    function normalized(value) { return String(value || "").toLowerCase() }
    function portVisible(port, output) {
        const filter = normalized(portFilter)
        const query = normalized(searchText)
        if (filter !== "all" && port.kind !== filter)
            return false
        if (query.length > 0 && normalized(port.label).indexOf(query) < 0
                && normalized(port.technicalLabel).indexOf(query) < 0)
            return false
        if (viewMode === "configured" && !output && filter === "mapped" && !port.mapped)
            return false
        return true
    }
    function visibleInputPorts() {
        const ports = inputPorts().filter(function(port) { return portVisible(port, false) })
        return ports.slice(0, graph.workspace && graph.workspace.densityMode === "overview" ? 20 : 56)
    }
    function visibleOutputPorts() {
        return outputPorts().filter(function(port) { return portVisible(port, true) })
    }
    function sourceIsCompatible(port) {
        if (!selectedSource || !selectedSource.kind)
            return false
        return selectedSource.kind === "axis" ? port.kind === "axis" : port.kind === "button"
    }
    function sourceLabel() {
        return selectedSource && selectedSource.label ? selectedSource.label : "Select a source port"
    }
    function clearSelection() {
        selectedSource = ({})
        selectedRoute = ({})
    }
    function showResult(result, fallback) {
        feedback = result && result.message ? result.message : fallback
        feedbackError = !(result && result.success)
    }
    function chooseSource(port) {
        if (!port.available) {
            feedback = "This input is inactive. Complete calibration before routing it."
            feedbackError = true
            return
        }
        if (viewMode === "effective") {
            feedback = "Effective view is read-only. Switch to Configured to edit routes."
            feedbackError = true
            return
        }
        selectedSource = port
        selectedRoute = ({})
        feedback = "Selected " + port.label + ". Choose a compatible output port."
        feedbackError = false
    }
    function chooseOutput(port) {
        if (!sourceIsCompatible(port)) {
            feedback = selectedSource && selectedSource.kind
                ? "Choose a compatible " + (selectedSource.kind === "axis" ? "virtual axis" : "virtual button") + "."
                : "Select an input source first."
            feedbackError = true
            return
        }
        const destination = port.kind === "axis" ? String(port.technicalLabel) : String(port.index)
        const result = backendObject.signalFlowConnect(String(selectedSource.kind), Number(selectedSource.index),
            Number(selectedSource.subIndex || 0), destination, false, Number(graph.revision || 0))
        showResult(result, "Connection was not applied.")
        if (result && result.success)
            clearSelection()
        else if (result && String(result.message).indexOf("Choose Replace") >= 0) {
            replaceDialog.destination = destination
            replaceDialog.portLabel = port.label
            replaceDialog.open()
        }
    }
    function applyReplacement() {
        const result = backendObject.signalFlowConnect(String(selectedSource.kind), Number(selectedSource.index),
            Number(selectedSource.subIndex || 0), replaceDialog.destination, true, Number(graph.revision || 0))
        showResult(result, "Replacement was not applied.")
        if (result && result.success)
            clearSelection()
    }
    function disconnectSelected() {
        if (!selectedRoute || !selectedRoute.id)
            return
        const result = backendObject.signalFlowDisconnect(String(selectedRoute.id), Number(graph.revision || 0))
        showResult(result, "Route was not disconnected.")
        if (result && result.success)
            selectedRoute = ({})
    }
    function previewDefaults(mode) {
        defaultsDialog.mode = mode
        defaultsDialog.preview = backendObject.signalFlowDefaultPreview(mode)
        defaultsDialog.open()
    }
    function saveWorkspace() {
        if (!workspaceDirty || !graph.workspace)
            return
        backendObject.signalFlowSaveWorkspace({
            "panX": graphViewport.contentX,
            "panY": graphViewport.contentY,
            "zoom": zoom,
            "wireStyle": graph.workspace.wireStyle || "smooth",
            "densityMode": graph.workspace.densityMode || "detailed",
            "inspectorWidth": graph.workspace.inspectorWidth || 360,
            "layoutLocked": graph.workspace.layoutLocked || false
        })
        workspaceDirty = false
    }
    function restoreWorkspace() {
        if (!graph.workspace || workspaceRestored)
            return
        zoom = Number(graph.workspace.zoom || 1.0)
        graphViewport.contentX = Math.max(0, Number(graph.workspace.panX || 0))
        graphViewport.contentY = Math.max(0, Number(graph.workspace.panY || 0))
        workspaceRestored = true
    }

    Connections {
        target: backendObject
        function onSignalFlowChanged() {
            root.graph = backendObject.signalFlowGraph
            if (!root.workspaceRestored)
                Qt.callLater(root.restoreWorkspace)
        }
    }
    Component.onCompleted: Qt.callLater(restoreWorkspace)

    component FlowButton: Button {
        id: flowButton
        property bool accent: false
        property bool dangerAction: false
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
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: portRow.selected ? root.graphPreview
                 : portRow.compatible ? Qt.rgba(root.graphOutput.r, root.graphOutput.g, root.graphOutput.b, 0.20)
                 : hit.containsMouse ? root.controlHover : "transparent"
            border.width: portRow.selected || portRow.compatible ? 1 : 0
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
        }
        MouseArea {
            id: hit
            anchors.fill: parent
            hoverEnabled: true
            enabled: Boolean(root.graph.editable && root.viewMode === "configured" && portRow.port && portRow.port.available)
            onClicked: {
                if (!portRow.port || !portRow.port.id)
                    return
                if (portRow.output) root.chooseOutput(portRow.port)
                else root.chooseSource(portRow.port)
            }
        }
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
                    text: "Inspectable route topology for " + (root.graph.profileName || "the active profile")
                    color: root.textMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
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
                Repeater {
                    model: [ { label: "All", key: "all" }, { label: "Axes", key: "axis" }, { label: "Buttons", key: "button" }, { label: "POV", key: "pov" } ]
                    delegate: FlowButton { required property var modelData; text: modelData.label; accent: root.portFilter === modelData.key; onClicked: root.portFilter = modelData.key }
                }
                Item { Layout.fillWidth: true }
                FlowButton { text: "Defaults"; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: root.previewDefaults("unassigned") }
                FlowButton { text: "Replace all"; dangerAction: true; enabled: root.graph.editable && root.viewMode === "configured"; onClicked: root.previewDefaults("replace-all") }
                FlowButton { text: "−"; Accessible.name: "Zoom out"; onClicked: { root.zoom = Math.max(0.5, root.zoom - 0.1); root.workspaceDirty = true; workspaceSaveTimer.restart() } }
                Text { text: Math.round(root.zoom * 100) + "%"; color: root.textMuted; font.pixelSize: 10 }
                FlowButton { text: "+"; Accessible.name: "Zoom in"; onClicked: { root.zoom = Math.min(1.6, root.zoom + 0.1); root.workspaceDirty = true; workspaceSaveTimer.restart() } }
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
                            spacing: 2
                            Repeater {
                                model: root.visibleInputPorts()
                                delegate: PortRow {
                                    required property var modelData
                                    width: parent.width
                                    port: modelData
                                    selected: root.selectedSource && root.selectedSource.id === modelData.id
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
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = root.graphGrid
                                ctx.globalAlpha = 0.38
                                ctx.lineWidth = 1
                                for (let x = 0; x < width; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke() }
                                for (let y = 0; y < height; y += 40) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke() }
                                ctx.globalAlpha = 1
                                const routes = root.graph.routes || []
                                const nodes = root.graph.nodes || []
                                function nodeById(id) {
                                    for (let n = 0; n < nodes.length; ++n) if (nodes[n].id === id) return nodes[n]
                                    return null
                                }
                                for (let routeIndex = 0; routeIndex < routes.length; ++routeIndex) {
                                    const route = routes[routeIndex]
                                    if (root.viewMode === "effective" && !route.effective) continue
                                    const source = nodeById(route.sourceNodeId)
                                    const target = nodeById(route.destinationNodeId)
                                    if (!source || !target) continue
                                    const startX = Number(source.x || 80) + 230
                                    const startY = Number(source.y || 160) + 52 + routeIndex * 7
                                    const endX = Number(target.x || 1380)
                                    const endY = Number(target.y || 160) + 52 + routeIndex * 7
                                    ctx.strokeStyle = root.selectedRoute && root.selectedRoute.id === route.id
                                        ? root.graphPreview : route.effective ? root.graphOutput : root.graphInput
                                    ctx.lineWidth = root.selectedRoute && root.selectedRoute.id === route.id ? 3 : 2
                                    ctx.beginPath()
                                    ctx.moveTo(startX, startY)
                                    ctx.bezierCurveTo(startX + 150, startY, endX - 150, endY, endX, endY)
                                    ctx.stroke()
                                }
                            }
                            Connections {
                                target: root
                                function onGraphChanged() { wireCanvas.requestPaint() }
                                function onSelectedRouteChanged() { wireCanvas.requestPaint() }
                                function onViewModeChanged() { wireCanvas.requestPaint() }
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
                                width: node.kind === "processor" ? 155 : 238
                                height: node.kind === "processor" ? 82 : Math.min(620, 86 + portColumn.implicitHeight)
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
                                        id: portColumn
                                        visible: node.kind === "input" || node.kind === "output"
                                        width: parent.width
                                        spacing: 1
                                        Repeater {
                                            model: {
                                                const ports = node.ports || []
                                                return ports.filter(function(port) { return root.portVisible(port, node.kind === "output") })
                                                    .slice(0, root.graph.workspace && root.graph.workspace.densityMode === "overview" ? 12 : 18)
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
                                    drag.target: flowNode
                                    drag.axis: Drag.XAndYAxis
                                    enabled: root.viewMode === "configured" && !(root.graph.workspace && root.graph.workspace.layoutLocked)
                                    onPressed: mouse => { mouse.accepted = false }
                                    onReleased: {
                                        if (drag.active) {
                                            root.backendObject.signalFlowSaveNodeLayout(String(node.objectId), flowNode.x, flowNode.y, false)
                                            root.workspaceDirty = true
                                        }
                                        mouse.accepted = false
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: root.graph.routes || []
                            delegate: MouseArea {
                                required property int index
                                required property var modelData
                                // Invisible, narrow targets make each persisted route keyboard/mouse selectable
                                // without pretending Canvas itself owns mapping state.
                                x: 680; y: 80 + index * 24; width: 180; height: 20
                                hoverEnabled: true
                                Accessible.name: "Route " + modelData.sourceLabel + " to " + modelData.destinationLabel
                                onClicked: { root.selectedRoute = modelData; root.selectedSource = ({}) }
                            }
                        }
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    text: "Drag the canvas to pan · drag a node to save its position · click a wire lane to inspect"
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
                    Text { text: "OUTPUT / INSPECTOR"; color: root.graphLabel; font.pixelSize: 10; font.bold: true; font.family: root.themeTokens.telemetryFont }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedRoute && root.selectedRoute.id
                            ? root.selectedRoute.sourceLabel + " → " + root.selectedRoute.destinationLabel
                            : "Choose a compatible output after selecting a source."
                        color: root.selectedRoute && root.selectedRoute.id ? root.textStrong : root.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: Boolean(root.selectedRoute && root.selectedRoute.id)
                        Layout.fillWidth: true
                        text: root.selectedRoute && root.selectedRoute.processors && root.selectedRoute.processors.length > 0
                            ? "Processors: " + root.selectedRoute.processors.length + " projected from this axis configuration."
                            : "No separate processor node is active for this route."
                        color: root.textMuted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    FlowButton { text: "Disconnect route"; dangerAction: true; visible: Boolean(root.selectedRoute && root.selectedRoute.id); enabled: root.viewMode === "configured"; Layout.fillWidth: true; onClicked: root.disconnectSelected() }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.border }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 2
                            Repeater {
                                model: root.visibleOutputPorts()
                                delegate: PortRow {
                                    required property var modelData
                                    width: parent.width
                                    port: modelData
                                    output: true
                                    compatible: root.sourceIsCompatible(modelData)
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

    Dialog {
        id: replaceDialog
        objectName: "signalFlowReplaceConflictDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Replace existing route?"
        property string destination: ""
        property string portLabel: ""
        width: Math.min(470, root.width - 40)
        contentItem: ColumnLayout {
            spacing: 12
            Text { Layout.fillWidth: true; text: "“" + root.sourceLabel() + "” conflicts with the current route into “" + replaceDialog.portLabel + "”. Replacing is explicit and can be undone."; color: root.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlowButton { text: "Cancel"; onClicked: replaceDialog.close() }
                FlowButton { text: "Replace route"; accent: true; onClicked: { replaceDialog.close(); root.applyReplacement() } }
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
                        delegate: Text { required property var modelData; width: parent.width; text: modelData.source + ": " + modelData.from + " → " + modelData.to; color: root.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
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
                    enabled: Boolean(defaultsDialog.preview.success && (defaultsDialog.preview.count || 0) > 0)
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
