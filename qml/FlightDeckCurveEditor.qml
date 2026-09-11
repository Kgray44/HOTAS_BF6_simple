import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Flight Deck's curve workspace intentionally owns a separate composition
// from the legacy CurveEditor.  Both route to the same AppBackend curve
// commands and compiled response math; only the presentation hierarchy is
// different.
Flickable {
    id: root
    objectName: "flightDeckCurveEditor"
    required property var backendObject
    required property var tokens
    property var presentationState: ({})
    property var editorState: backendObject ? backendObject.curveEditorState : ({})
    property var analysis: backendObject ? backendObject.curveAnalysis : ({})
    property var liveTelemetry: backendObject ? backendObject.curveEditorTelemetry : ({})
    property var comparison: backendObject ? backendObject.curveComparisonState : ({})
    property bool responseView: true
    property bool showEffective: false
    property bool detailsExpanded: false
    property bool addingPoint: false
    property int selectedPoint: -1
    property var undoStack: []
    property var redoStack: []
    signal presentationStateCaptured(var state)

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + tokens.space24)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    function capturePresentationState() {
        presentationStateCaptured({
            contentY: contentY,
            responseView: responseView,
            showEffective: showEffective,
            detailsExpanded: detailsExpanded
        });
    }
    function restorePresentationState() {
        const saved = presentationState || ({});
        contentY = Number(saved.contentY || 0);
        responseView = saved.responseView === undefined ? true : !!saved.responseView;
        showEffective = !!saved.showEffective;
        detailsExpanded = !!saved.detailsExpanded;
    }
    function recordHistory() {
        if (!backendObject) return;
        const snapshot = backendObject.curveEditorSnapshot();
        if (!snapshot.length || (undoStack.length > 0 && undoStack[undoStack.length - 1] === snapshot)) return;
        undoStack = undoStack.concat([snapshot]);
        if (undoStack.length > 50) undoStack.shift();
        redoStack = [];
    }
    function undo() {
        if (!backendObject || undoStack.length === 0) return;
        const current = backendObject.curveEditorSnapshot();
        const prior = undoStack[undoStack.length - 1];
        undoStack = undoStack.slice(0, undoStack.length - 1);
        redoStack = redoStack.concat([current]);
        backendObject.restoreCurveEditorSnapshot(prior);
        selectedPoint = -1;
    }
    function redo() {
        if (!backendObject || redoStack.length === 0) return;
        const current = backendObject.curveEditorSnapshot();
        const next = redoStack[redoStack.length - 1];
        redoStack = redoStack.slice(0, redoStack.length - 1);
        undoStack = undoStack.concat([current]);
        backendObject.restoreCurveEditorSnapshot(next);
        selectedPoint = -1;
    }
    function pointAt(index) {
        const points = backendObject ? backendObject.selectedCurvePoints : [];
        return index >= 0 && index < points.length ? points[index] : null;
    }
    function percent(value) {
        const number = Number(value || 0) * 100;
        return (number >= 0 ? "+" : "") + number.toFixed(1) + "%";
    }
    function rawPercent(value) {
        const number = Number(value || 0) * 100;
        return (number >= 0 ? "+" : "") + number.toFixed(1) + "%";
    }

    Component.onCompleted: restorePresentationState()
    Component.onDestruction: capturePresentationState()
    Connections {
        target: backendObject
        function onStateChanged() {
            root.editorState = backendObject.curveEditorState;
            root.analysis = backendObject.curveAnalysis;
            root.liveTelemetry = backendObject.curveEditorTelemetry;
            root.comparison = backendObject.curveComparisonState;
            graph.requestPaint();
        }
        function onSelectedAxisCurveChanged() {
            root.editorState = backendObject.curveEditorState;
            root.analysis = backendObject.curveAnalysis;
            root.comparison = backendObject.curveComparisonState;
            graph.requestPaint();
        }
        function onInputTelemetryChanged() {
            root.liveTelemetry = backendObject.curveEditorTelemetry;
            graph.requestPaint();
        }
    }

    component SectionLabel: Text {
        property string caption: ""
        text: caption
        color: tokens.textMuted
        font.family: tokens.telemetryFont
        font.pixelSize: 9
        font.bold: true
        font.letterSpacing: 1.1
        elide: Text.ElideRight
    }
    component DeckButton: Button {
        id: control
        property bool subdued: false
        property bool destructive: false
        implicitHeight: tokens.compactControlHeight
        leftPadding: tokens.space12
        rightPadding: tokens.space12
        focusPolicy: Qt.TabFocus
        contentItem: Text {
            text: control.text
            color: !control.enabled ? tokens.disabled : control.destructive ? tokens.fault
                : control.subdued ? tokens.textPrimary : tokens.primarySurface
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: tokens.radiusControl
            color: !control.enabled ? tokens.secondarySurface
                : control.down ? (control.destructive ? tokens.secondarySurface : tokens.accentMuted)
                : control.hovered ? (control.destructive ? tokens.secondarySurface
                    : control.subdued ? tokens.selected : tokens.focus)
                : control.destructive ? tokens.secondarySurface
                : control.subdued ? tokens.secondarySurface : tokens.accent
            border.width: control.visualFocus ? 2 : 1
            border.color: control.visualFocus ? tokens.focus
                : control.destructive ? tokens.fault : control.hovered ? tokens.accent : tokens.border
        }
    }
    component DeckCombo: ComboBox {
        id: control
        implicitHeight: tokens.controlHeight
        focusPolicy: Qt.TabFocus
        contentItem: Text {
            leftPadding: tokens.space12
            rightPadding: tokens.space24
            text: control.displayText
            color: control.enabled ? tokens.textPrimary : tokens.disabled
            font.family: tokens.bodyFont
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - tokens.space12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: tokens.textSecondary
            font.pixelSize: 16
        }
        background: Rectangle {
            radius: tokens.radiusControl
            color: control.pressed ? tokens.selected : tokens.elevatedSurface
            border.width: control.visualFocus ? 2 : 1
            border.color: control.visualFocus ? tokens.focus : control.hovered ? tokens.textMuted : tokens.border
        }
        delegate: ItemDelegate {
            width: ListView.view.width
            height: 34
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                leftPadding: tokens.space12
                rightPadding: tokens.space12
                text: control.textAt(index)
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: parent.highlighted ? tokens.selected : tokens.elevatedSurface }
        }
        popup: Popup {
            y: control.height - 1
            width: control.width
            padding: tokens.popupPadding
            background: Rectangle { radius: tokens.radiusControl; color: tokens.elevatedSurface; border.color: tokens.border }
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
        }
    }
    component ContextField: ColumnLayout {
        property string label: ""
        Layout.fillWidth: true
        spacing: tokens.space4
        SectionLabel { caption: parent.label }
    }
    component Metric: ColumnLayout {
        property string label: ""
        property string value: "—"
        Layout.fillWidth: true
        spacing: 2
        SectionLabel { caption: parent.label }
        Text {
            text: parent.value
            color: tokens.textPrimary
            font.family: tokens.telemetryFont
            font.pixelSize: 11
            font.bold: true
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }

    Rectangle {
        width: root.width
        height: Math.max(root.height, root.contentHeight)
        color: tokens.primarySurface
        z: -1
    }
    ColumnLayout {
        id: content
        width: root.width
        spacing: tokens.space16

        FlightDeckCard {
            tokens: root.tokens
            Layout.fillWidth: true
            implicitHeight: contextContent.implicitHeight + contentPadding * 2
            color: tokens.elevatedSurface
            ColumnLayout {
                id: contextContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: tokens.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: tokens.space4
                        SectionLabel { caption: "ACTIVE CURVE CONTEXT" }
                        Text {
                            text: (backendObject ? backendObject.activeProfileName : "Active profile")
                                + "  ·  " + (editorState.summary || "Linear response")
                            color: tokens.textPrimary
                            font.family: tokens.displayFont
                            font.pixelSize: 17
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        implicitWidth: mappingStatus.implicitWidth + tokens.space16
                        implicitHeight: 25
                        radius: tokens.radiusPill
                        color: backendObject && backendObject.mappingActive ? tokens.selected : tokens.secondarySurface
                        border.color: backendObject && backendObject.mappingActive ? tokens.healthy : tokens.border
                        Text {
                            id: mappingStatus
                            anchors.centerIn: parent
                            text: backendObject && backendObject.mappingActive ? "MAPPING LIVE" : "MAPPING STANDBY"
                            color: backendObject && backendObject.mappingActive ? tokens.healthy : tokens.textMuted
                            font.family: tokens.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 1060 ? 4 : 2
                    columnSpacing: tokens.space12
                    rowSpacing: tokens.space10
                    ContextField {
                        label: "PROFILE"
                        DeckCombo {
                            id: profileSelector
                            Layout.fillWidth: true
                            model: backendObject ? backendObject.profiles : []
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: backendObject ? backendObject.activeProfileIndex : 0
                            onActivated: backendObject.activateProfile(currentValue)
                        }
                    }
                    ContextField {
                        label: "AXIS"
                        DeckCombo {
                            id: axisSelector
                            Layout.fillWidth: true
                            model: backendObject ? backendObject.curveAxisChoices : []
                            textRole: "label"
                            valueRole: "index"
                            currentIndex: backendObject ? backendObject.selectedAxisIndex : 0
                            onActivated: backendObject.setSelectedAxis(Number(currentValue))
                        }
                    }
                    ContextField {
                        label: "FAMILY"
                        DeckCombo {
                            Layout.fillWidth: true
                            model: ["Linear", "J-Curve", "S-Curve", "Advanced", "Personal", "Custom"]
                            currentIndex: Math.max(0, model.indexOf(editorState.family || "Linear"))
                            onActivated: { root.recordHistory(); backendObject.setCurveFamily(currentText) }
                        }
                    }
                    ContextField {
                        label: editorState.family === "Advanced" ? "PRESET" : editorState.family === "Personal" ? "PERSONAL PRESET" : "CUSTOM SOURCE"
                        DeckCombo {
                            Layout.fillWidth: true
                            visible: editorState.family === "Advanced" || editorState.family === "Personal" || editorState.family === "Custom"
                            model: editorState.family === "Advanced" ? (backendObject ? backendObject.curveAdvancedPresets : [])
                                : editorState.family === "Personal" ? (backendObject ? backendObject.personalCurvePresets : [])
                                : (backendObject ? backendObject.curveCustomProfileChoices : [])
                            textRole: "name"
                            valueRole: "id"
                            onActivated: {
                                root.recordHistory();
                                if (editorState.family === "Advanced") backendObject.applyAdvancedCurvePreset(currentValue);
                                else if (editorState.family === "Personal") backendObject.applyPersonalCurvePreset(currentValue);
                                else backendObject.copyCurveFrom(currentValue, backendObject.selectedAxisIndex);
                            }
                        }
                        Text {
                            visible: editorState.family !== "Advanced" && editorState.family !== "Personal" && editorState.family !== "Custom"
                            text: "No external source"
                            color: tokens.textMuted
                            font.pixelSize: 10
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: tokens.space12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: tokens.space4
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { caption: "RESPONSE STRENGTH" }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: Math.round(Number(editorState.strength || 0) * 100) + "%"
                                color: strengthSlider.enabled ? tokens.accent : tokens.textMuted
                                font.family: tokens.telemetryFont
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }
                        Slider {
                            id: strengthSlider
                            Layout.fillWidth: true
                            from: 0; to: 1; stepSize: 0.01
                            enabled: editorState.family !== "Linear"
                            value: Number(editorState.strength || 0)
                            onMoved: backendObject.setCurveStrength(value)
                            background: Rectangle {
                                x: strengthSlider.leftPadding
                                y: strengthSlider.topPadding + strengthSlider.availableHeight / 2 - height / 2
                                width: strengthSlider.availableWidth
                                height: 6
                                radius: 3
                                color: tokens.secondarySurface
                                border.color: tokens.border
                                Rectangle {
                                    width: strengthSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: strengthSlider.enabled ? tokens.accent : tokens.disabled
                                }
                            }
                            handle: Rectangle {
                                x: strengthSlider.leftPadding + strengthSlider.visualPosition * (strengthSlider.availableWidth - width)
                                y: strengthSlider.topPadding + strengthSlider.availableHeight / 2 - height / 2
                                width: 16; height: 16; radius: 8
                                color: strengthSlider.enabled ? tokens.textPrimary : tokens.disabled
                                border.color: tokens.border
                            }
                        }
                    }
                    DeckButton { text: "RESET VIEW"; subdued: true; onClicked: graph.resetView() }
                }
            }
        }

        FlightDeckCard {
            tokens: root.tokens
            Layout.fillWidth: true
            implicitHeight: graphContent.implicitHeight + contentPadding * 2
            color: tokens.elevatedSurface
            ColumnLayout {
                id: graphContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: tokens.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SectionLabel { caption: "RESPONSE SURFACE" }
                        Text {
                            text: responseView ? "Input → configured response" : "Local response gain"
                            color: tokens.textPrimary
                            font.family: tokens.displayFont
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }
                    DeckButton {
                        text: responseView ? "SHOW GAIN" : "SHOW RESPONSE"
                        subdued: true
                        onClicked: { responseView = !responseView; graph.requestPaint() }
                    }
                    DeckButton {
                        text: showEffective ? "EFFECTIVE ON" : "EFFECTIVE OFF"
                        subdued: true
                        onClicked: { showEffective = !showEffective; graph.requestPaint() }
                    }
                    DeckButton { text: "↶"; subdued: true; enabled: undoStack.length > 0; onClicked: root.undo() }
                    DeckButton { text: "↷"; subdued: true; enabled: redoStack.length > 0; onClicked: root.redo() }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(320, Math.min(480, root.height * 0.55))
                    radius: tokens.radiusPanel
                    color: tokens.graphSurface
                    border.color: tokens.graphFrame
                    clip: true
                    Canvas {
                        id: graph
                        objectName: "flightDeckCurveGraph"
                        anchors.fill: parent
                        anchors.margins: tokens.space12
                        antialiasing: true
                        renderTarget: Canvas.Image
                        property var responseSamples: backendObject ? backendObject.curveEditorResponseCurve : []
                        property var gainSamples: backendObject ? backendObject.curveGainSamples : []
                        property var comparisonSamples: backendObject ? backendObject.curveComparisonCurve : []
                        property var previewSamples: backendObject ? backendObject.curvePreviewCurve : []
                        property var effectiveSamples: backendObject ? backendObject.selectedAxisCurve : []
                        property var points: backendObject ? backendObject.selectedCurvePoints : []
                        // The same UI-safe selected-axis snapshot that feeds
                        // the established editors. These are visual markers
                        // only: no live input enters the curve compiler.
                        readonly property bool liveMarkerVisible: responseView
                            && root.liveTelemetry
                            && root.liveTelemetry.physicalInput !== undefined
                            && root.liveTelemetry.finalOutput !== undefined
                        readonly property real liveMarkerInput: Number(root.liveTelemetry.physicalInput || 0)
                        readonly property real liveMarkerOutput: Number(root.liveTelemetry.finalOutput || 0)
                        property real domainMin: editorState.unipolar ? 0 : -1
                        property real xMin: domainMin
                        property real xMax: 1
                        property real yMin: domainMin
                        property real yMax: 1
                        readonly property real plotLeft: 42
                        readonly property real plotRight: 14
                        readonly property real plotTop: 14
                        readonly property real plotBottom: 28
                        function plotWidth() { return Math.max(1, width - plotLeft - plotRight) }
                        function plotHeight() { return Math.max(1, height - plotTop - plotBottom) }
                        function xFor(value) { return plotLeft + (value - xMin) / (xMax - xMin) * plotWidth() }
                        function yFor(value) { return plotTop + (1 - (value - yMin) / (yMax - yMin)) * plotHeight() }
                        function inputFor(x) { return Math.max(domainMin, Math.min(1, xMin + (x - plotLeft) / plotWidth() * (xMax - xMin))) }
                        function outputFor(y) { return Math.max(domainMin, Math.min(1, yMin + (1 - (y - plotTop) / plotHeight()) * (yMax - yMin))) }
                        function gainY(value) {
                            const peak = Math.max(1.25, Number(analysis.peakGain || 1) * 1.12)
                            return plotTop + (1 - Math.max(0, Math.min(peak, value)) / peak) * plotHeight()
                        }
                        function resetView() { xMin = domainMin; xMax = 1; yMin = domainMin; yMax = 1; requestPaint() }
                        function nearestPoint(x, y) {
                            let nearest = -1, distance = 16;
                            for (let index = 0; index < points.length; ++index) {
                                const pointX = xFor(Number(points[index].input));
                                const pointY = yFor(Number(points[index].output));
                                const distanceHere = Math.hypot(pointX - x, pointY - y);
                                if (distanceHere < distance) { distance = distanceHere; nearest = index; }
                            }
                            return nearest;
                        }
                        function trace(context, samples, color, lineWidth, key, dashed) {
                            if (!samples || samples.length === 0) return;
                            context.save(); context.strokeStyle = color; context.lineWidth = lineWidth;
                            if (dashed) context.setLineDash([5, 4]);
                            context.beginPath();
                            for (let index = 0; index < samples.length; ++index) {
                                const point = samples[index];
                                const x = xFor(Number(point.input));
                                const y = responseView ? yFor(Number(point[key])) : gainY(Number(point.gain));
                                if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
                            }
                            context.stroke(); context.restore();
                        }
                        onResponseSamplesChanged: requestPaint()
                        onGainSamplesChanged: requestPaint()
                        onComparisonSamplesChanged: requestPaint()
                        onPreviewSamplesChanged: requestPaint()
                        onEffectiveSamplesChanged: requestPaint()
                        onPointsChanged: requestPaint()
                        onLiveMarkerVisibleChanged: requestPaint()
                        onLiveMarkerInputChanged: requestPaint()
                        onLiveMarkerOutputChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const context = getContext("2d");
                            const pw = plotWidth();
                            const ph = plotHeight();
                            context.reset();
                            context.fillStyle = tokens.graphBackground;
                            context.fillRect(0, 0, width, height);
                            context.strokeStyle = tokens.graphGrid;
                            context.lineWidth = 1;
                            for (let line = 0; line <= 4; ++line) {
                                const x = plotLeft + pw * line / 4;
                                const y = plotTop + ph * line / 4;
                                context.beginPath(); context.moveTo(x, plotTop); context.lineTo(x, plotTop + ph); context.stroke();
                                context.beginPath(); context.moveTo(plotLeft, y); context.lineTo(plotLeft + pw, y); context.stroke();
                            }
                            if (responseView) {
                                context.strokeStyle = tokens.graphInput; context.setLineDash([5, 4]);
                                context.beginPath(); context.moveTo(xFor(domainMin), yFor(domainMin)); context.lineTo(xFor(1), yFor(1)); context.stroke(); context.setLineDash([]);
                                trace(context, responseSamples, tokens.healthy, 2.4, "output", false);
                                trace(context, comparisonSamples, tokens.graphInput, 1.2, "output", true);
                                trace(context, previewSamples, tokens.attention, 1.5, "output", true);
                                if (showEffective) trace(context, effectiveSamples, tokens.accent, 1.8, "output", false);
                                if (editorState.pointEditing) {
                                    for (let index = 0; index < points.length; ++index) {
                                        const point = points[index];
                                        context.fillStyle = point.locked ? tokens.graphLockedPoint : index === selectedPoint ? tokens.graphSelectedPoint : tokens.graphPoint;
                                        context.beginPath(); context.arc(xFor(Number(point.input)), yFor(Number(point.output)), point.locked ? 5 : 4, 0, Math.PI * 2); context.fill();
                                    }
                                }
                                if (liveMarkerVisible && Number.isFinite(liveMarkerInput)
                                    && Number.isFinite(liveMarkerOutput)) {
                                    const markerX = xFor(liveMarkerInput);
                                    const inputY = yFor(liveMarkerInput);
                                    const outputY = yFor(liveMarkerOutput);
                                    context.fillStyle = tokens.graphInput;
                                    context.strokeStyle = tokens.graphFrame;
                                    context.lineWidth = 2;
                                    context.beginPath(); context.arc(markerX, inputY, 5, 0, Math.PI * 2);
                                    context.fill(); context.stroke();
                                    context.fillStyle = tokens.graphOutput;
                                    context.strokeStyle = tokens.textPrimary;
                                    context.beginPath(); context.arc(markerX, outputY, 4, 0, Math.PI * 2);
                                    context.fill(); context.stroke();
                                }
                            } else {
                                trace(context, gainSamples, tokens.accent, 2.3, "gain", false);
                                context.strokeStyle = tokens.graphInput; context.setLineDash([4, 4]);
                                context.beginPath(); context.moveTo(plotLeft, gainY(1)); context.lineTo(plotLeft + pw, gainY(1)); context.stroke(); context.setLineDash([]);
                            }
                            context.fillStyle = tokens.graphLabel; context.font = "10px " + tokens.telemetryFont;
                            context.fillText(responseView ? "INPUT" : "INPUT", plotLeft, height - 7);
                            context.fillText(responseView ? "RESPONSE" : "GAIN", 4, plotTop + 10);
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: responseView && !!editorState.pointEditing
                            property int draggedPoint: -1
                            onPressed: function(mouse) {
                                draggedPoint = graph.nearestPoint(mouse.x, mouse.y);
                                if (draggedPoint >= 0) {
                                    root.selectedPoint = draggedPoint;
                                    root.recordHistory();
                                } else if (root.addingPoint
                                    && mouse.x >= graph.plotLeft && mouse.x <= graph.width - graph.plotRight
                                    && mouse.y >= graph.plotTop && mouse.y <= graph.height - graph.plotBottom) {
                                    root.recordHistory();
                                    root.selectedPoint = backendObject.addCurvePoint(graph.inputFor(mouse.x), graph.outputFor(mouse.y));
                                    root.addingPoint = false;
                                }
                            }
                            onPositionChanged: function(mouse) {
                                if (draggedPoint < 0) return;
                                const point = root.pointAt(draggedPoint);
                                if (!point || point.locked) return;
                                backendObject.setCurvePoint(draggedPoint, graph.inputFor(mouse.x), graph.outputFor(mouse.y));
                            }
                            onReleased: draggedPoint = -1
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: tokens.space12
                    Text { text: "SOLID · CONFIGURED"; color: tokens.healthy; font.family: tokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Text { text: "DASHED · REFERENCE / OVERLAY"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Text { visible: editorState.pointEditing; text: addingPoint ? "CLICK GRAPH TO ADD" : "DRAG A POINT TO EDIT"; color: tokens.attention; font.family: tokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                }
            }
        }

        FlightDeckCard {
            tokens: root.tokens
            Layout.fillWidth: true
            implicitHeight: toolsContent.implicitHeight + contentPadding * 2
            color: tokens.secondarySurface
            ColumnLayout {
                id: toolsContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: tokens.space10
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        SectionLabel { caption: "OVERLAY & WORKSPACE TOOLS" }
                        Text { text: "Compare or preview a response without changing the active curve."; color: tokens.textSecondary; font.pixelSize: 10 }
                    }
                    DeckButton { text: detailsExpanded ? "HIDE DETAILS" : "CURVE DETAILS"; subdued: true; onClicked: detailsExpanded = !detailsExpanded }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 900 ? 3 : 1
                    columnSpacing: tokens.space12
                    rowSpacing: tokens.space10
                    ContextField {
                        label: "COMPARE WITH"
                        DeckCombo {
                            Layout.fillWidth: true
                            model: backendObject ? backendObject.curveComparisonChoices : []
                            textRole: "label"; valueRole: "id"
                            onActivated: backendObject.setCurveComparison(currentValue)
                        }
                    }
                    ContextField {
                        label: "PREVIEW PRESET"
                        DeckCombo {
                            id: previewSelector
                            Layout.fillWidth: true
                            model: backendObject ? backendObject.curvePreviewChoices : []
                            textRole: "label"; valueRole: "id"
                            onActivated: backendObject.previewCurvePreset(currentValue)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        DeckButton { text: "APPLY PREVIEW"; enabled: !!editorState.previewLabel; onClicked: { root.recordHistory(); backendObject.applyCurvePreview() } }
                        DeckButton { text: "CLEAR"; subdued: true; enabled: !!editorState.previewLabel; onClicked: backendObject.clearCurvePreview() }
                    }
                }
                Text {
                    visible: !!editorState.previewLabel
                    text: "Previewing · " + editorState.previewLabel
                    color: tokens.attention
                    font.family: tokens.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                }
            }
        }

        GridLayout {
            visible: detailsExpanded
            Layout.fillWidth: true
            columns: root.width >= 980 ? 2 : 1
            columnSpacing: tokens.space16
            rowSpacing: tokens.space16
            FlightDeckCard {
                tokens: root.tokens
                Layout.fillWidth: true
                implicitHeight: pointContent.implicitHeight + contentPadding * 2
                ColumnLayout {
                    id: pointContent
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: tokens.space10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            SectionLabel { caption: "CUSTOM POINTS" }
                            Text { text: editorState.pointEditing ? "Point editing is active" : "Use custom points only when the family needs manual shaping."; color: tokens.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        DeckButton {
                            text: editorState.pointEditing ? "POINTS ON" : "ENABLE POINTS"
                            subdued: !editorState.pointEditing
                            onClicked: { root.recordHistory(); backendObject.setCurvePointEditing(!editorState.pointEditing); root.selectedPoint = -1; root.addingPoint = false }
                        }
                    }
                    RowLayout {
                        visible: !!editorState.pointEditing
                        Layout.fillWidth: true
                        DeckButton { text: addingPoint ? "CANCEL ADD" : "+ ADD POINT"; onClicked: addingPoint = !addingPoint }
                        DeckButton {
                            text: pointAt(selectedPoint) && pointAt(selectedPoint).locked ? "UNLOCK" : "LOCK"
                            subdued: true
                            enabled: selectedPoint > 0 && selectedPoint < Number(editorState.pointCount || 0) - 1
                            onClicked: { const point = root.pointAt(selectedPoint); if (point) { root.recordHistory(); backendObject.setCurvePointLocked(selectedPoint, !point.locked) } }
                        }
                        DeckButton {
                            text: "REMOVE"
                            destructive: true
                            enabled: selectedPoint > 0 && selectedPoint < Number(editorState.pointCount || 0) - 1
                            onClicked: { root.recordHistory(); backendObject.removeCurvePoint(selectedPoint); selectedPoint = -1 }
                        }
                    }
                    GridLayout {
                        visible: !!editorState.pointEditing
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: tokens.space12
                        rowSpacing: tokens.space8
                        ContextField {
                            label: "SELECTED POINT"
                            DeckCombo {
                                Layout.fillWidth: true
                                model: backendObject ? backendObject.selectedCurvePoints : []
                                textRole: "label"
                                currentIndex: selectedPoint
                                onActivated: selectedPoint = currentIndex
                                delegate: ItemDelegate {
                                    width: ListView.view.width; height: 32
                                    contentItem: Text { leftPadding: tokens.space12; text: "Point " + (index + 1); color: tokens.textPrimary; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                                }
                            }
                        }
                        ContextField {
                            label: "INTERPOLATION"
                            DeckCombo {
                                Layout.fillWidth: true
                                model: ["Smooth", "Linear"]
                                currentIndex: editorState.interpolation === "Linear" ? 1 : 0
                                onActivated: { root.recordHistory(); backendObject.setCurveInterpolation(currentText) }
                            }
                        }
                    }
                    RowLayout {
                        visible: !!editorState.pointEditing && selectedPoint >= 0
                        Layout.fillWidth: true
                        Text { text: pointAt(selectedPoint) ? "Input " + percent(pointAt(selectedPoint).input) + " · Output " + percent(pointAt(selectedPoint).output) : "Select a point in the graph"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true }
                        DeckButton {
                            text: "RESET POINT"
                            subdued: true
                            enabled: pointAt(selectedPoint) && !pointAt(selectedPoint).locked
                            onClicked: { const point = root.pointAt(selectedPoint); if (point) { root.recordHistory(); backendObject.setCurvePoint(selectedPoint, Number(point.input), Number(point.input)) } }
                        }
                    }
                }
            }
            FlightDeckCard {
                tokens: root.tokens
                Layout.fillWidth: true
                implicitHeight: analysisContent.implicitHeight + contentPadding * 2
                ColumnLayout {
                    id: analysisContent
                    anchors.fill: parent
                    anchors.margins: parent.contentPadding
                    spacing: tokens.space10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            SectionLabel { caption: "CURVE ANALYSIS" }
                            Text { text: analysis.valid ? "Compiled response is monotonic and bounded" : "Curve validation needs attention"; color: analysis.valid ? tokens.healthy : tokens.attention; font.pixelSize: 11; font.bold: true }
                        }
                        DeckButton { text: "SAVE PERSONAL"; subdued: true; onClicked: personalDialog.open() }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: tokens.space16
                        rowSpacing: tokens.space10
                        Metric { label: "CENTER GAIN"; value: Number(analysis.centerGain || 0).toFixed(2) + "×" }
                        Metric { label: "PEAK GAIN"; value: Number(analysis.peakGain || 0).toFixed(2) + "×" }
                        Metric { label: "QUARTER GAIN"; value: Number(analysis.quarterGain || 0).toFixed(2) + "×" }
                        Metric { label: "LUT"; value: String(editorState.runtimeLutSamples || 4097) + " samples" }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: tokens.divider }
                    SectionLabel { caption: "LIVE SIGNAL PATH" }
                    Repeater {
                        model: [
                            { label: "INPUT", value: liveTelemetry.physicalInput },
                            { label: "DEADZONE", value: liveTelemetry.afterDeadzone },
                            { label: "CURVE", value: liveTelemetry.curveResponse },
                            { label: "OUTPUT", value: liveTelemetry.finalOutput }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text { text: modelData.label; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 8; Layout.fillWidth: true }
                            Text { text: root.rawPercent(modelData.value); color: tokens.textPrimary; font.family: tokens.telemetryFont; font.pixelSize: 10; font.bold: true }
                        }
                    }
                }
            }
        }
        Item { Layout.preferredHeight: tokens.space8 }
    }

    Dialog {
        id: personalDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Save personal curve preset"
        standardButtons: Dialog.NoButton
        background: Rectangle { radius: tokens.radiusCard; color: tokens.elevatedSurface; border.color: tokens.border }
        contentItem: ColumnLayout {
            width: 360
            spacing: tokens.space12
            Text { text: "Save a copy of this response for compatible axes. Existing curves remain independent."; color: tokens.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextField {
                id: personalName
                Layout.fillWidth: true
                placeholderText: "My Precision Roll"
                color: tokens.textPrimary
                placeholderTextColor: tokens.textMuted
                focusPolicy: Qt.StrongFocus
                background: Rectangle { radius: tokens.radiusControl; color: tokens.secondarySurface; border.width: parent.visualFocus ? 2 : 1; border.color: parent.visualFocus ? tokens.focus : tokens.border }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: personalDialog.close() }
                DeckButton { text: "SAVE"; enabled: personalName.text.trim().length > 0; onClicked: { if (backendObject.saveCurrentCurveAsPersonalPreset(personalName.text)) personalDialog.close() } }
            }
        }
        onOpened: { personalName.text = ""; personalName.forceActiveFocus() }
    }
}
