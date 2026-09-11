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

    FlightDeckTheme { id: deck }
    property var backendObject: backend
    property var graph: backendObject ? backendObject.signalFlowGraph : ({})
    property var source: ({})
    property var inspectedRoute: ({})
    property string query: ""
    property string mode: "configured"
    property string filter: "all"
    property string notice: ""
    property bool noticeError: false
    property real zoom: 1.0
    property bool workspaceRestored: false

    function normalized(value) { return String(value || "").toLowerCase() }
    function node(kind) {
        const nodes = graph.nodes || []
        for (let i = 0; i < nodes.length; ++i) if (nodes[i].kind === kind) return nodes[i]
        return ({})
    }
    function ports(kind) { return node(kind).ports || [] }
    function matching(port, isOutput) {
        if (filter !== "all" && port.kind !== filter) return false
        const needle = normalized(query)
        if (needle.length > 0 && normalized(port.label).indexOf(needle) < 0 && normalized(port.technicalLabel).indexOf(needle) < 0) return false
        return true
    }
    function sourcePorts() { return ports("input").filter(function(port) { return matching(port, false) }).slice(0, 40) }
    function destinationPorts() { return ports("output").filter(function(port) { return matching(port, true) }) }
    function compatible(port) {
        return Boolean(source && source.kind && port
            && (source.kind === "axis" ? port.kind === "axis" : port.kind === "button"))
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
        notice = "Selected " + port.label + ". Choose a compatible destination."; noticeError = false
    }
    function connect(port, replace) {
        if (!compatible(port)) {
            notice = source && source.kind ? "Select a compatible destination." : "Select an input first."
            noticeError = true
            return
        }
        const destination = port.kind === "axis" ? String(port.technicalLabel) : String(port.index)
        const result = backendObject.signalFlowConnect(String(source.kind), Number(source.index), Number(source.subIndex || 0), destination, replace, Number(graph.revision || 0))
        announce(result, "Connection was not applied.")
        if (result && result.success) { source = ({}); inspectedRoute = ({}) }
        else if (result && String(result.message).indexOf("Choose Replace") >= 0) {
            conflictDialog.destination = destination
            conflictDialog.label = port.label
            conflictDialog.open()
        }
    }
    function preview(modeName) {
        defaultsDialog.mode = modeName
        defaultsDialog.preview = backendObject.signalFlowDefaultPreview(modeName)
        defaultsDialog.open()
    }
    function restoreWorkspace() {
        if (workspaceRestored || !graph.workspace) return
        zoom = Number(graph.workspace.zoom || 1)
        graphViewport.contentX = Math.max(0, Number(graph.workspace.panX || 0))
        graphViewport.contentY = Math.max(0, Number(graph.workspace.panY || 0))
        workspaceRestored = true
    }
    function saveWorkspace() {
        if (!graph.workspace) return
        backendObject.signalFlowSaveWorkspace({
            "panX": graphViewport.contentX, "panY": graphViewport.contentY, "zoom": zoom,
            "wireStyle": graph.workspace.wireStyle || "smooth", "densityMode": graph.workspace.densityMode || "detailed",
            "inspectorWidth": graph.workspace.inspectorWidth || 360, "layoutLocked": graph.workspace.layoutLocked || false
        })
    }

    Connections {
        target: backendObject
        function onSignalFlowChanged() {
            root.graph = backendObject.signalFlowGraph
            if (!root.workspaceRestored) Qt.callLater(root.restoreWorkspace)
        }
    }
    Component.onCompleted: Qt.callLater(restoreWorkspace)

    component DeckButton: Button {
        id: deckButton
        property bool emphasized: false
        property bool destructive: false
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
        color: isSelected ? deck.accentMuted : isCompatible ? deck.selected : hover.containsMouse ? deck.secondarySurface : "transparent"
        border.width: isSelected || isCompatible ? 1 : 0
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
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            enabled: Boolean(root.mode === "configured" && root.graph.editable && flowPort.port && flowPort.port.available)
            onClicked: { if (!flowPort.port || !flowPort.port.id) return; if (flowPort.destination) root.connect(flowPort.port, false); else root.selectSource(flowPort.port) }
        }
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
                Text { text: "Route map · " + (root.graph.profileName || "active profile") + " · persisted mapping state"; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            }
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
                Repeater {
                    model: [ { label: "All", key: "all" }, { label: "Axes", key: "axis" }, { label: "Buttons", key: "button" }, { label: "POV", key: "pov" } ]
                    delegate: DeckButton { required property var modelData; text: modelData.label; emphasized: root.filter === modelData.key; onClicked: root.filter = modelData.key }
                }
                Item { Layout.fillWidth: true }
                DeckButton { text: "Default map"; enabled: root.mode === "configured" && root.graph.editable; onClicked: root.preview("unassigned") }
                DeckButton { text: "Replace all"; destructive: true; enabled: root.mode === "configured" && root.graph.editable; onClicked: root.preview("replace-all") }
                DeckButton { text: "−"; Accessible.name: "Zoom out"; onClicked: { root.zoom = Math.max(0.5, root.zoom - 0.1); saveTimer.restart() } }
                Text { text: Math.round(root.zoom * 100) + "%"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9 }
                DeckButton { text: "+"; Accessible.name: "Zoom in"; onClicked: { root.zoom = Math.min(1.6, root.zoom + 0.1); saveTimer.restart() } }
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
                            spacing: 3
                            Repeater { model: root.sourcePorts(); delegate: FlowPort { required property var modelData; width: parent.width; port: modelData; isSelected: root.source && root.source.id === modelData.id } }
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
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.strokeStyle = deck.graphGrid
                                context.globalAlpha = 0.55
                                for (let x = 0; x < width; x += 48) { context.beginPath(); context.moveTo(x, 0); context.lineTo(x, height); context.stroke() }
                                for (let y = 0; y < height; y += 48) { context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke() }
                                context.globalAlpha = 1
                                const allRoutes = root.graph.routes || []
                                for (let i = 0; i < allRoutes.length; ++i) {
                                    const route = allRoutes[i]
                                    if (root.mode === "effective" && !route.effective) continue
                                    context.strokeStyle = root.inspectedRoute && root.inspectedRoute.id === route.id ? deck.attention : route.effective ? deck.healthy : deck.accent
                                    context.lineWidth = root.inspectedRoute && root.inspectedRoute.id === route.id ? 4 : 2
                                    const y = 175 + (i % 18) * 28
                                    context.beginPath(); context.moveTo(310, y); context.bezierCurveTo(550, y, 915, y, 1190, y); context.stroke()
                                }
                            }
                            Connections {
                                target: root
                                function onGraphChanged() { diagram.requestPaint() }
                                function onInspectedRouteChanged() { diagram.requestPaint() }
                                function onModeChanged() { diagram.requestPaint() }
                            }
                        }
                        FlightDeckCard {
                            id: inputNode
                            tokens: deck
                            x: Number(root.node("input").x || 50)
                            y: Number(root.node("input").y || 150)
                            width: 250
                            height: 118
                            contentPadding: deck.cardPaddingCompact
                            color: deck.secondarySurface
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { text: root.node("input").label || "Input context"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: root.node("input").detail || "Physical source"; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                Text { text: "PORTS " + root.sourcePorts().length; color: deck.informational; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            }
                            MouseArea { anchors.fill: parent; drag.target: inputNode; drag.axis: Drag.XAndYAxis; enabled: root.mode === "configured"; onReleased: { backendObject.signalFlowSaveNodeLayout(String(root.node("input").objectId), inputNode.x, inputNode.y, false); saveTimer.restart() } }
                        }
                        Repeater {
                            model: (root.graph.nodes || []).filter(function(item) { return item.kind === "processor" })
                            delegate: FlightDeckCard {
                                id: processorNode
                                required property var modelData
                                tokens: deck
                                x: Number(modelData.x || 540)
                                y: Number(modelData.y || 170)
                                width: 180
                                height: 88
                                contentPadding: deck.cardPaddingCompact
                                color: deck.elevatedSurface
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: 4
                                    Text { text: modelData.label; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.detail; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                }
                                MouseArea { anchors.fill: parent; drag.target: processorNode; drag.axis: Drag.XAndYAxis; enabled: root.mode === "configured"; onReleased: { backendObject.signalFlowSaveNodeLayout(String(modelData.objectId), processorNode.x, processorNode.y, false); saveTimer.restart() } }
                            }
                        }
                        FlightDeckCard {
                            id: outputNode
                            tokens: deck
                            x: Number(root.node("output").x || 1190)
                            y: Number(root.node("output").y || 150)
                            width: 250
                            height: 118
                            contentPadding: deck.cardPaddingCompact
                            color: deck.secondarySurface
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: parent.contentPadding; spacing: deck.space6
                                Text { text: root.node("output").label || "Virtual output"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: root.node("output").detail || "vJoy destination"; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2 }
                                Text { text: "DESTINATIONS " + root.destinationPorts().length; color: deck.healthy; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            }
                            MouseArea { anchors.fill: parent; drag.target: outputNode; drag.axis: Drag.XAndYAxis; enabled: root.mode === "configured"; onReleased: { backendObject.signalFlowSaveNodeLayout(String(root.node("output").objectId), outputNode.x, outputNode.y, false); saveTimer.restart() } }
                        }
                        Column {
                            x: 470; y: 660; width: 620; spacing: 4
                            Text { text: "ROUTE LANES · click to inspect"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                            Repeater {
                                model: root.graph.routes || []
                                delegate: Rectangle {
                                    required property var modelData
                                    width: parent.width; height: 25; radius: deck.radiusControl
                                    color: routeHit.containsMouse || (root.inspectedRoute && root.inspectedRoute.id === modelData.id) ? deck.selected : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: deck.space8
                                        anchors.rightMargin: deck.space8
                                        Text { Layout.fillWidth: true; text: modelData.sourceLabel + "  →  " + modelData.destinationLabel; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; elide: Text.ElideRight }
                                        Text { text: modelData.effective ? "ACTIVE" : "CONFIGURED"; color: modelData.effective ? deck.healthy : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                    }
                                    MouseArea { id: routeHit; anchors.fill: parent; hoverEnabled: true; onClicked: { root.inspectedRoute = modelData; root.source = ({}) } }
                                }
                            }
                        }
                    }
                }
                Text { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: deck.space12; text: "Drag card positions to persist this workspace · use the source and destination buses for route editing"; color: deck.graphLabel; font.family: deck.bodyFont; font.pixelSize: 9 }
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
                    Text { text: "DESTINATION BUS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Text { text: root.inspectedRoute && root.inspectedRoute.id ? root.inspectedRoute.sourceLabel + " → " + root.inspectedRoute.destinationLabel : "Select a route or destination"; color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Text { visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); text: root.inspectedRoute && root.inspectedRoute.processors && root.inspectedRoute.processors.length ? "Conditioning nodes: " + root.inspectedRoute.processors.length : "Direct route — no visible conditioning node."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    DeckButton { text: "Disconnect selected"; destructive: true; visible: Boolean(root.inspectedRoute && root.inspectedRoute.id); enabled: root.mode === "configured"; Layout.fillWidth: true; onClicked: { const result = backendObject.signalFlowDisconnect(String(root.inspectedRoute.id), Number(root.graph.revision || 0)); root.announce(result, "Route was not disconnected."); if (result && result.success) root.inspectedRoute = ({}) } }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: parent.availableWidth
                            spacing: 3
                            Repeater { model: root.destinationPorts(); delegate: FlowPort { required property var modelData; width: parent.width; port: modelData; destination: true; isCompatible: root.compatible(modelData) } }
                            Text { visible: root.destinationPorts().length === 0; width: parent.width; text: "No destinations match."; color: deck.textMuted; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }
    }

    Timer { id: saveTimer; interval: 650; repeat: false; onTriggered: root.saveWorkspace() }

    Dialog {
        id: conflictDialog
        objectName: "flightDeckSignalFlowConflictDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, root.width - 40)
        property string destination: ""
        property string label: ""
        title: "Replace existing destination?"
        contentItem: ColumnLayout {
            spacing: deck.space16
            Text { Layout.fillWidth: true; text: "“" + (root.source.label || "Selected source") + "” would replace the current route into “" + conflictDialog.label + "”. This decision is explicit and reversible with Undo."; color: deck.textPrimary; font.family: deck.bodyFont; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "Cancel"; onClicked: conflictDialog.close() }
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
                    Repeater { model: defaultsDialog.preview.changes || []; delegate: Text { required property var modelData; width: parent.width; text: modelData.source + ": " + modelData.from + " → " + modelData.to; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10; elide: Text.ElideRight } }
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
