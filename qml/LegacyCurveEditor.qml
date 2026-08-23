import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: editor
    // Retain the original curve-editor body and bind it to the concrete
    // v1.6.3 panel surface rather than the themed panel used elsewhere.
    component AviationPanel: LegacyAviationPanel {}
    property var backendObject
    property var editorState: backendObject ? backendObject.curveEditorState : ({})
    property var analysis: backendObject ? backendObject.curveAnalysis : ({})
    property var comparison: backendObject ? backendObject.curveComparisonState : ({})
    property int selectedPoint: -1
    property bool addingPoint: false
    property bool responseView: true
    property bool showEffective: false
    property bool signalPathVisible: false
    property bool characteristicsVisible: false
    property var undoStack: []
    property var redoStack: []
    property int historyLimit: 50
    property real snapIncrement: 0.01
    property real dragInput: 0
    property real dragOutput: 0
    property real graphContextInput: 0
    property real graphContextOutput: 0
    property int contextPoint: -1

    focus: true

    component AviationButton: Button {
        id: control
        property string role: "secondary"
        implicitHeight: 36
        implicitWidth: Math.max(104, contentItem.implicitWidth + 28)
        font.pixelSize: 12
        font.bold: true
        contentItem: Text {
            text: control.text; color: control.enabled ? "#edf6f6" : "#86989d"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight; font: control.font
        }
        background: Rectangle {
            radius: 4; border.width: 1
            border.color: !control.enabled ? "#304249" : control.down ? "#8bc5cf"
                : control.hovered ? (control.role === "destructive" ? "#b98778" : "#78b7c2")
                : (control.role === "primary" ? "#78aeb9" : control.role === "destructive" ? "#805b56" : "#536e78")
            color: !control.enabled ? "#151b1e" : control.down ? "#244955"
                : control.hovered ? (control.role === "primary" ? "#365f6a" : control.role === "destructive" ? "#432c2b" : "#263c43")
                : (control.role === "primary" ? "#31545f" : control.role === "destructive" ? "#2c2223" : "#1b2a30")
        }
        ToolTip.visible: hovered && ToolTip.text.length > 0
    }

    component AviationCombo: ComboBox {
        id: control
        implicitHeight: 36
        font.pixelSize: 12
        contentItem: Text {
            leftPadding: 11; rightPadding: 28; text: control.displayText
            color: control.enabled ? "#e8f0f1" : "#89999e"; font: control.font
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        indicator: Text { x: control.width - width - 9; y: (control.height - height) / 2
            text: "⌄"; color: control.enabled ? "#9cccd2" : "#6b7f85"; font.pixelSize: 16 }
        background: Rectangle { radius: 4; border.width: 1
            border.color: control.activeFocus ? "#76bac5" : control.hovered ? "#587f89" : "#435e67"
            color: control.enabled ? "#121f24" : "#11181b" }
        delegate: ItemDelegate { width: control.width; height: 34; highlighted: control.highlightedIndex === index
            contentItem: Text { text: modelData[control.textRole] || modelData; color: "#dbe8e9"
                verticalAlignment: Text.AlignVCenter; leftPadding: 11; font.pixelSize: 12; elide: Text.ElideRight }
            background: Rectangle { color: highlighted ? "#1b3a43" : "#111c21" }
        }
        popup: Popup { y: control.height - 1; width: control.width; padding: 1
            implicitHeight: Math.min(contentItem.implicitHeight, 260)
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
            background: Rectangle { color: "#10191e"; border.color: "#4e7881"; radius: 3 }
        }
    }

    component AviationSpinBox: SpinBox {
        id: control
        implicitWidth: 88; implicitHeight: 34
        contentItem: TextInput {
            z: 2; text: control.textFromValue(control.value, control.locale); font: control.font
            color: control.enabled ? "#dce9ea" : "#62757a"; selectionColor: "#3f7b86"; selectedTextColor: "#ffffff"
            horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter; readOnly: !control.editable
            validator: control.validator; inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        background: Rectangle { radius: 4; color: "#0f1a1f"; border.color: control.activeFocus ? "#76bac5" : "#435e67" }
        up.indicator: Rectangle { x: control.width - width; height: control.height / 2; width: 16; color: control.up.pressed ? "#29454e" : "transparent"
            Text { anchors.centerIn: parent; text: "▲"; color: "#8fc3c9"; font.pixelSize: 7 } }
        down.indicator: Rectangle { x: control.width - width; y: control.height / 2; height: control.height / 2; width: 16; color: control.down.pressed ? "#29454e" : "transparent"
            Text { anchors.centerIn: parent; text: "▼"; color: "#8fc3c9"; font.pixelSize: 7 } }
    }

    component AviationMenuItem: MenuItem {
        id: control
        implicitHeight: 29
        contentItem: Text { text: control.text; color: control.enabled ? "#dce9ea" : "#63757a"; leftPadding: 10; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
        background: Rectangle { color: control.highlighted ? "#1d3d47" : "#101b20" }
    }

    component AviationTextField: TextField {
        id: control
        implicitHeight: 36
        font.pixelSize: 12
        color: "#e8f0f1"
        placeholderTextColor: "#83979c"
        selectionColor: "#3f7b86"
        selectedTextColor: "#ffffff"
        leftPadding: 11
        rightPadding: 11
        background: Rectangle {
            radius: 4
            color: "#121f24"
            border.width: 1
            border.color: control.activeFocus ? "#76bac5" : control.hovered ? "#587f89" : "#435e67"
        }
    }

    component AviationToggle: Switch {
        id: control
        implicitWidth: 44
        implicitHeight: 24
        indicator: Rectangle {
            implicitWidth: 38
            implicitHeight: 20
            x: control.leftPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            radius: 10
            color: control.checked ? "#244b54" : "#202b30"
            border.color: control.checked ? "#70b3bd" : "#536b73"
            Rectangle {
                width: 14
                height: 14
                radius: 7
                x: control.checked ? parent.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                color: control.checked ? "#b6e1e3" : "#9caaae"
            }
        }
    }

    component AviationCheckBox: CheckBox {
        id: control
        implicitHeight: 28
        implicitWidth: indicator.implicitWidth + spacing + contentItem.implicitWidth
        spacing: 8
        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            x: control.leftPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            radius: 3
            color: !control.enabled ? "#182126" : control.checked ? "#1d4750" : "#172228"
            border.color: !control.enabled ? "#3d4d52" : control.hovered ? "#83c1c9" : control.checked ? "#6eb1bb" : "#58717a"
            Text {
                anchors.centerIn: parent
                text: "✓"
                visible: control.checked
                color: control.enabled ? "#b9e8e8" : "#819499"
                font.pixelSize: 13
                font.bold: true
            }
        }
        contentItem: Text {
            leftPadding: control.indicator.implicitWidth + control.spacing
            text: control.text
            color: control.enabled ? "#d7e5e7" : "#849499"
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
            font.bold: true
        }
        ToolTip.visible: hovered && ToolTip.text.length > 0
    }

    component CardHeading: Text {
        color: "#edf6f6"
        font.pixelSize: 15
        font.bold: true
    }

    component FieldCaption: Text {
        color: "#9eb3b9"
        font.pixelSize: 12
        font.bold: true
    }

    function tone(ok) { return ok ? "#9fc9bb" : "#d49b62" }
    function percent(value) {
        const n = Number(value)
        if (editorState.unipolar) return (n * 100).toFixed(1) + "%"
        return (n >= 0 ? "+" : "") + (n * 100).toFixed(1) + "%"
    }
    function rawPercent(value) {
        const n = Number(value)
        if (editorState.unipolar) return ((n + 1) * 50).toFixed(1) + "%"
        return (n >= 0 ? "+" : "") + (n * 100).toFixed(1) + "%"
    }
    function recordHistory() {
        if (!backendObject) return
        const snapshot = backendObject.curveEditorSnapshot()
        if (!snapshot.length || (undoStack.length && undoStack[undoStack.length - 1] === snapshot)) return
        undoStack = undoStack.concat([snapshot])
        if (undoStack.length > historyLimit) undoStack.shift()
        redoStack = []
    }
    function undo() {
        if (!backendObject || !undoStack.length) return
        const current = backendObject.curveEditorSnapshot()
        const prior = undoStack[undoStack.length - 1]
        undoStack = undoStack.slice(0, undoStack.length - 1)
        redoStack = redoStack.concat([current])
        backendObject.restoreCurveEditorSnapshot(prior)
        selectedPoint = -1
    }
    function redo() {
        if (!backendObject || !redoStack.length) return
        const current = backendObject.curveEditorSnapshot()
        const next = redoStack[redoStack.length - 1]
        redoStack = redoStack.slice(0, redoStack.length - 1)
        undoStack = undoStack.concat([current])
        backendObject.restoreCurveEditorSnapshot(next)
        selectedPoint = -1
    }
    function pointAt(index) {
        const points = backendObject ? backendObject.selectedCurvePoints : []
        return index >= 0 && index < points.length ? points[index] : null
    }
    function nudgePoint(key, fine) {
        const point = pointAt(selectedPoint)
        if (!point || point.locked) return
        const amount = fine ? 0.001 : 0.01
        let x = Number(point.input), y = Number(point.output)
        if (key === Qt.Key_Left) x -= amount
        if (key === Qt.Key_Right) x += amount
        if (key === Qt.Key_Down) y -= amount
        if (key === Qt.Key_Up) y += amount
        recordHistory()
        backendObject.setCurvePoint(selectedPoint, x, y)
    }
    function resetDisplay() {
        addingPoint = false; selectedPoint = -1; showEffective = false; responseView = true
        if (backendObject) { backendObject.setCurveComparison(""); backendObject.clearCurvePreview() }
        graph.cursorVisible = false; graph.resetView()
    }

    Keys.onPressed: function(event) {
        if (!editorState.pointEditing) return
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            nudgePoint(event.key, event.modifiers & Qt.ShiftModifier)
            event.accepted = true
        }
    }

    Shortcut { sequence: "Ctrl+Z"; enabled: editor.visible; onActivated: editor.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: editor.visible; onActivated: editor.redo() }

    Timer {
        id: dragCompile
        interval: 33
        repeat: true
        onTriggered: backendObject.setCurvePoint(editor.selectedPoint, editor.dragInput, editor.dragOutput)
    }

    Connections {
        target: backendObject
        function onStateChanged() {
            editor.editorState = backendObject.curveEditorState
            editor.comparison = backendObject.curveComparisonState
            graph.requestPaint()
        }
        function onSelectedAxisCurveChanged() {
            editor.editorState = backendObject.curveEditorState
            editor.analysis = backendObject.curveAnalysis
            editor.comparison = backendObject.curveComparisonState
            graph.requestPaint()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0d1013"
    }
    Flickable {
        id: editorScroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: curveContent.height + 4
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: curveContent
            width: editorScroll.width - 10
            height: implicitHeight
            x: 1
            spacing: 14

        AviationPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: editor.width >= 1080 ? 198 : 270
            color: "#e61a282e"
            border.color: "#4b70818a"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Text { text: "CURVE EDITOR"; color: "#f1f7f7"; font.pixelSize: 23; font.bold: true }
                        Text {
                            text: (backendObject ? backendObject.activeProfileName : "Normal") + " / "
                                + (axisSelector.currentText || "Roll") + " · " + (editorState.summary || "Linear · 0%")
                            color: "#a8d3d9"
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Row {
                        spacing: 7
                        Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter
                            color: backendObject && backendObject.mappingActive ? "#9fc9bb" : "#d5b66f" }
                        Text {
                            text: backendObject && backendObject.mappingActive ? "MAPPING LIVE" : "MAPPING STANDBY"
                            color: backendObject && backendObject.mappingActive ? "#b9dfc6" : "#dfc883"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: editor.width >= 1080 ? 4 : 2
                    columnSpacing: 10
                    rowSpacing: 6
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FieldCaption { text: "PROFILE" }
                        AviationCombo { id: profileSelector; Layout.fillWidth: true; model: backendObject ? backendObject.profiles : []; textRole: "name"; valueRole: "id"; currentIndex: backendObject ? backendObject.activeProfileIndex : 0; onActivated: backendObject.activateProfile(currentValue) }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FieldCaption { text: "AXIS" }
                        AviationCombo { id: axisSelector; Layout.fillWidth: true; model: backendObject ? backendObject.axes : []; textRole: "label"; valueRole: "index"; currentIndex: backendObject ? backendObject.selectedAxisIndex : 0; onActivated: backendObject.setSelectedAxis(Number(currentValue)) }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FieldCaption { text: "FAMILY" }
                        AviationCombo { id: familySelector; Layout.fillWidth: true; model: ["Linear", "J-Curve", "S-Curve", "Advanced", "Personal", "Custom"]
                            currentIndex: Math.max(0, model.indexOf(editorState.family || "Linear")); onActivated: { recordHistory(); backendObject.setCurveFamily(currentText) } }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: editorState.family === "Advanced" || editorState.family === "Personal"
                        FieldCaption { text: editorState.family === "Advanced" ? "ADVANCED PRESET" : "PERSONAL PRESET" }
                        AviationCombo { id: presetSelector; Layout.fillWidth: true; model: editorState.family === "Advanced" ? (backendObject ? backendObject.curveAdvancedPresets : []) : (backendObject ? backendObject.personalCurvePresets : []); textRole: "name"; valueRole: "id"
                            currentIndex: { for (let i = 0; i < model.length; ++i) if (model[i].id === editorState.presetId) return i; return -1 }
                            onActivated: { recordHistory(); if (editorState.family === "Advanced") backendObject.applyAdvancedCurvePreset(currentValue); else backendObject.applyPersonalCurvePreset(currentValue) } }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        FieldCaption { text: "RESPONSE STRENGTH" }
                        Item { Layout.fillWidth: true }
                        Text { text: Math.round(Number(editorState.strength || 0) * 100) + "%"; color: responseStrength.enabled ? "#b8e0e2" : "#9aa8ac"; font.family: "Consolas"; font.pixelSize: 16; font.bold: true }
                    }
                    Slider {
                        id: responseStrength
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        from: 0; to: 1; stepSize: 0.01
                        enabled: editorState.family !== "Linear"
                        value: Number(editorState.strength || 0)
                        ToolTip.text: enabled ? "Blends the selected response into the compiled curve." : "Linear uses a fixed 0% response strength."
                        onMoved: backendObject.setCurveStrength(value)
                        background: Rectangle { x: responseStrength.leftPadding; y: responseStrength.topPadding + responseStrength.availableHeight / 2 - height / 2; width: responseStrength.availableWidth; height: 6; radius: 3; color: "#17292f"; border.color: "#405d65"
                            Rectangle { width: responseStrength.visualPosition * parent.width; height: parent.height; radius: 3; color: responseStrength.enabled ? "#68bac5" : "#53686d" } }
                        handle: Rectangle { x: responseStrength.leftPadding + responseStrength.visualPosition * (responseStrength.availableWidth - width); y: responseStrength.topPadding + responseStrength.availableHeight / 2 - height / 2; width: 16; height: 16; radius: 8; color: responseStrength.enabled ? "#c3e8e8" : "#91a3a6"; border.color: "#356f79" }
                    }
                    RowLayout { Layout.fillWidth: true
                        Repeater { model: ["0%", "25%", "50%", "75%", "100%"]
                            delegate: Text { Layout.fillWidth: true; text: modelData; color: "#90a5aa"; font.pixelSize: 11; horizontalAlignment: index === 0 ? Text.AlignLeft : index === 4 ? Text.AlignRight : Text.AlignHCenter }
                        }
                    }
                }
            }
        }

        AviationPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                FieldCaption { text: "VIEW" }
                Text { text: responseView ? "RESPONSE" : "LOCAL GAIN"; color: "#d6e5e7"; font.pixelSize: 13; font.bold: true }
                AviationToggle { checked: !responseView; onToggled: responseView = !checked; ToolTip.text: "Switch between response output and local gain." }
                Text { text: responseView ? "Input + active output" : "Local dy/dx"; color: "#a6bbc0"; font.pixelSize: 12 }
                Item { Layout.preferredWidth: 16 }
                AviationCheckBox { text: "SHOW EFFECTIVE AXIS RESPONSE"; checked: showEffective; onToggled: showEffective = checked; ToolTip.text: "Overlay the effective axis response after the full signal path." }
                Item { Layout.fillWidth: true }
                Text { text: "LUT " + (editorState.runtimeLutSamples || 4097) + " SAMPLES"; color: "#91adb4"; font.pixelSize: 12; font.family: "Consolas"; font.bold: true }
            }
        }

        AviationPanel {
            id: graphPanel
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(350, Math.min(480, editor.height * 0.58))
            color: "#101a1f"
            border.color: "#486873"
            gradient: Gradient {
                GradientStop { position: 0; color: "#27383e" }
                GradientStop { position: 0.08; color: "#17262b" }
                GradientStop { position: 1; color: "#0b1114" }
            }
            Rectangle { anchors.fill: parent; anchors.margins: 8; radius: 3; color: "#060b0e"; border.color: "#203b44" }

            Canvas {
                id: graph
                anchors.fill: parent; anchors.margins: 13
                antialiasing: true; renderTarget: Canvas.Image
                property var responseSamples: backendObject ? backendObject.curveEditorResponseCurve : []
                property var gainSamples: backendObject ? backendObject.curveGainSamples : []
                property var comparisonSamples: backendObject ? backendObject.curveComparisonCurve : []
                property var previewSamples: backendObject ? backendObject.curvePreviewCurve : []
                property var effectiveSamples: backendObject ? backendObject.selectedAxisCurve : []
                property var points: backendObject ? backendObject.selectedCurvePoints : []
                property real domainMin: editorState.unipolar ? 0 : -1
                property real xMin: domainMin
                property real xMax: 1
                property real yMin: domainMin
                property real yMax: 1
                property real cursorInput: 0
                property real cursorOutput: 0
                property real cursorGain: 1
                property bool cursorVisible: false
                property bool panning: false
                property bool draggingPoint: false
                property real panStartX: 0
                property real panStartY: 0
                property real panOriginX: 0
                property real panOriginY: 0
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
                function cursorAt(x, y) {
                    cursorInput = inputFor(x)
                    const inspected = backendObject.inspectCurve(cursorInput)
                    cursorOutput = Number(inspected.output || 0)
                    cursorGain = Number(inspected.gain || 0)
                    cursorVisible = x >= plotLeft && x <= width - plotRight && y >= plotTop && y <= height - plotBottom
                    requestPaint()
                }
                function nearestPoint(x, y) {
                    let nearest = -1, distance = 15
                    for (let i = 0; i < points.length; ++i) {
                        const px = xFor(Number(points[i].input)), py = yFor(Number(points[i].output))
                        const d = Math.hypot(px - x, py - y)
                        if (d < distance) { distance = d; nearest = i }
                    }
                    return nearest
                }
                function trace(ctx, samples, color, lineWidth, key, dashed) {
                    if (!samples || !samples.length) return
                    ctx.save(); ctx.strokeStyle = color; ctx.lineWidth = lineWidth
                    if (dashed) ctx.setLineDash([5, 4])
                    ctx.beginPath()
                    for (let i = 0; i < samples.length; ++i) {
                        const p = samples[i]; const x = xFor(Number(p.input))
                        const y = responseView ? yFor(Number(p[key])) : gainY(Number(p.gain))
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    }
                    ctx.stroke(); ctx.restore()
                }
                onResponseSamplesChanged: requestPaint(); onGainSamplesChanged: requestPaint()
                onComparisonSamplesChanged: requestPaint(); onPreviewSamplesChanged: requestPaint()
                onEffectiveSamplesChanged: requestPaint(); onPointsChanged: requestPaint()
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"), pw = plotWidth(), ph = plotHeight()
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "#071014"; ctx.fillRect(plotLeft, plotTop, pw, ph)
                    ctx.strokeStyle = "#254550"; ctx.lineWidth = 1
                    for (let i = 0; i <= 4; ++i) {
                        const x = plotLeft + pw * i / 4, y = plotTop + ph * i / 4
                        ctx.beginPath(); ctx.moveTo(x, plotTop); ctx.lineTo(x, plotTop + ph); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(plotLeft, y); ctx.lineTo(plotLeft + pw, y); ctx.stroke()
                    }
                    if (responseView) {
                        ctx.strokeStyle = "#557984"; ctx.lineWidth = 1
                        if (domainMin < 0 && xMin <= 0 && xMax >= 0) {
                            ctx.beginPath(); ctx.moveTo(xFor(0), plotTop); ctx.lineTo(xFor(0), plotTop + ph); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(plotLeft, yFor(0)); ctx.lineTo(plotLeft + pw, yFor(0)); ctx.stroke()
                        }
                        trace(ctx, responseSamples, "#6ec3cf", 2.3, "output", false)
                        const identity = []
                        for (let i = 0; i < 2; ++i) identity.push({ input: i === 0 ? domainMin : 1, output: i === 0 ? domainMin : 1 })
                        trace(ctx, identity, "#bac7ca", 1.15, "output", false)
                        trace(ctx, comparisonSamples, "#8a9ba1", 1.2, "output", true)
                        trace(ctx, previewSamples, "#d4b36e", 1.3, "output", true)
                        if (showEffective && effectiveSamples && effectiveSamples.length) {
                            const effective = []
                            for (let i = 0; i < effectiveSamples.length; ++i) {
                                const p = effectiveSamples[i]
                                effective.push({ input: editorState.unipolar ? (Number(p.input) + 1) * .5 : Number(p.input),
                                                 output: editorState.unipolar ? (Number(p.output) + 1) * .5 : Number(p.output) })
                            }
                            trace(ctx, effective, "#3b7584", 1.0, "output", true)
                        }
                        if (editorState.pointEditing) {
                            for (let i = 0; i < points.length; ++i) {
                                let px = xFor(Number(points[i].input)), py = yFor(Number(points[i].output))
                                if (draggingPoint && i === selectedPoint) { px = xFor(dragInput); py = yFor(dragOutput) }
                                ctx.fillStyle = points[i].locked ? "#7f8c90" : (i === selectedPoint ? "#f3d98d" : "#d7f1f0")
                                ctx.strokeStyle = "#15333c"; ctx.lineWidth = 2
                                ctx.beginPath(); ctx.arc(px, py, points[i].locked ? 5 : 4, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                            }
                        }
                    } else {
                        trace(ctx, gainSamples, "#d5b76c", 2.2, "gain", false)
                        ctx.strokeStyle = "#5a7881"; ctx.setLineDash([3, 3])
                        ctx.beginPath(); ctx.moveTo(plotLeft, gainY(1)); ctx.lineTo(plotLeft + pw, gainY(1)); ctx.stroke(); ctx.setLineDash([])
                    }
                    if (cursorVisible) {
                        const cx = xFor(cursorInput), cy = responseView ? yFor(cursorOutput) : gainY(cursorGain)
                        ctx.strokeStyle = "#7ca1aa80"; ctx.setLineDash([3, 3])
                        ctx.beginPath(); ctx.moveTo(cx, plotTop); ctx.lineTo(cx, plotTop + ph); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(plotLeft, cy); ctx.lineTo(plotLeft + pw, cy); ctx.stroke(); ctx.setLineDash([])
                    }
                    if (responseView) {
                        const raw = Number(editorState.physicalInput || 0)
                        const finalValue = Number(editorState.finalOutput || 0)
                        const markerX = xFor(editorState.unipolar ? (raw + 1) * .5 : raw)
                        const inputY = yFor(editorState.unipolar ? (raw + 1) * .5 : raw)
                        const outputY = yFor(editorState.unipolar ? (finalValue + 1) * .5 : finalValue)
                        ctx.fillStyle = "#e1eeee"; ctx.strokeStyle = "#587682"; ctx.lineWidth = 2
                        ctx.beginPath(); ctx.arc(markerX, inputY, 5, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                        ctx.fillStyle = "#8fcfc0"; ctx.strokeStyle = "#e6f5ef"
                        ctx.beginPath(); ctx.arc(markerX, outputY, 4, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    }
                    ctx.fillStyle = "#78949c"; ctx.font = "10px Consolas"
                    const labels = editorState.unipolar ? ["0", "25", "50", "75", "100"] : ["-100", "-50", "0", "+50", "+100"]
                    for (let i = 0; i <= 4; ++i) ctx.fillText(labels[i], plotLeft + pw * i / 4 - 9, height - 7)
                }
            }
            MouseArea {
                anchors.fill: graph; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                onPressed: function(mouse) {
                    graph.cursorAt(mouse.x, mouse.y)
                    if (mouse.button === Qt.RightButton) {
                        contextPoint = editorState.pointEditing ? graph.nearestPoint(mouse.x, mouse.y) : -1
                        graphContextInput = graph.inputFor(mouse.x); graphContextOutput = graph.outputFor(mouse.y)
                        if (contextPoint >= 0) pointContextMenu.open(); else graphContextMenu.open()
                        return
                    }
                    if (mouse.button === Qt.MiddleButton) {
                        graph.panning = true; graph.panStartX = mouse.x; graph.panStartY = mouse.y
                        graph.panOriginX = graph.xMin; graph.panOriginY = graph.yMin; return
                    }
                    if (addingPoint && editorState.pointEditing) {
                        recordHistory(); selectedPoint = backendObject.addCurvePoint(graph.inputFor(mouse.x), graph.outputFor(mouse.y)); addingPoint = false; return
                    }
                    const nearby = graph.nearestPoint(mouse.x, mouse.y)
                    if (!editorState.pointEditing || nearby < 0) {
                        graph.panning = true; graph.panStartX = mouse.x; graph.panStartY = mouse.y
                        graph.panOriginX = graph.xMin; graph.panOriginY = graph.yMin; return
                    }
                    selectedPoint = nearby
                    const point = pointAt(selectedPoint)
                    if (point && !point.locked) {
                        recordHistory(); graph.draggingPoint = true; dragInput = Number(point.input); dragOutput = Number(point.output); dragCompile.start()
                    }
                }
                onPositionChanged: function(mouse) {
                    graph.cursorAt(mouse.x, mouse.y)
                    if (graph.panning) {
                        const dx = (graph.panStartX - mouse.x) / graph.plotWidth() * (graph.xMax - graph.xMin)
                        const dy = (mouse.y - graph.panStartY) / graph.plotHeight() * (graph.yMax - graph.yMin)
                        const xRange = graph.xMax - graph.xMin, yRange = graph.yMax - graph.yMin
                        graph.xMin = graph.panOriginX + dx; graph.xMax = graph.xMin + xRange
                        graph.yMin = graph.panOriginY + dy; graph.yMax = graph.yMin + yRange; graph.requestPaint()
                    } else if (graph.draggingPoint) {
                        let x = graph.inputFor(mouse.x), y = graph.outputFor(mouse.y)
                        if (snapIncrement > 0) { x = Math.round(x / snapIncrement) * snapIncrement; y = Math.round(y / snapIncrement) * snapIncrement }
                        dragInput = x; dragOutput = y; graph.requestPaint()
                    }
                }
                onReleased: function(mouse) {
                    if (graph.draggingPoint) { dragCompile.stop(); backendObject.setCurvePoint(selectedPoint, dragInput, dragOutput); graph.draggingPoint = false }
                    graph.panning = false
                }
                onExited: graph.cursorVisible = false
                onWheel: function(wheel) {
                    const factor = wheel.angleDelta.y > 0 ? 0.82 : 1.22
                    const pivotX = graph.inputFor(wheel.x), pivotY = graph.outputFor(wheel.y)
                    const width = Math.max(0.04, Math.min(2.0, (graph.xMax - graph.xMin) * factor))
                    const height = Math.max(0.04, Math.min(2.0, (graph.yMax - graph.yMin) * factor))
                    const rx = (pivotX - graph.xMin) / (graph.xMax - graph.xMin), ry = (pivotY - graph.yMin) / (graph.yMax - graph.yMin)
                    graph.xMin = pivotX - width * rx; graph.xMax = graph.xMin + width
                    graph.yMin = pivotY - height * ry; graph.yMax = graph.yMin + height; graph.requestPaint()
                }
            }
            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 18; color: "#102027e8"; border.color: "#547681"; width: 194; height: 72; visible: graph.cursorVisible
                Column { anchors.fill: parent; anchors.margins: 9; spacing: 3
                    Text { text: "CURSOR  " + percent(graph.cursorInput); color: "#d8eaec"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                    Text { text: "OUTPUT  " + percent(graph.cursorOutput); color: "#9fdbd3"; font.family: "Consolas"; font.pixelSize: 12 }
                    Text { text: "GAIN    " + graph.cursorGain.toFixed(2) + "×"; color: "#e3c87e"; font.family: "Consolas"; font.pixelSize: 12 }
                }
            }
            Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18; color: "#342b1de8"; border.color: "#a7824c"; width: 230; height: 30; radius: 3; visible: !!editorState.previewLabel
                Text { anchors.centerIn: parent; text: "PREVIEW ACTIVE  ·  " + editorState.previewLabel; color: "#e8c77c"; font.pixelSize: 11; font.bold: true }
            }
        }

        AviationPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: editor.width >= 1180 ? 62 : editor.width >= 900 ? 106 : 150
            GridLayout {
                anchors.fill: parent
                anchors.margins: 13
                columns: editor.width >= 900 ? 5 : 2
                columnSpacing: 18
                rowSpacing: 8
                Repeater {
                    model: [
                        { label: "PHYSICAL INPUT", value: rawPercent(editorState.physicalInput), tone: "#dce9ea" },
                        { label: "CURVE RESPONSE", value: rawPercent(editorState.curveResponse), tone: "#9fdad3" },
                        { label: "FINAL OUTPUT", value: rawPercent(editorState.finalOutput), tone: "#b7ddc2" },
                        { label: "LOCAL GAIN", value: Number(editorState.localGain || 0).toFixed(2) + "×", tone: "#e4c97f" }
                    ]
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: modelData.label; color: "#9db2b8"; font.pixelSize: 11; font.bold: true }
                        Text { text: modelData.value; color: modelData.tone; font.family: "Consolas"; font.pixelSize: 17; font.bold: true }
                    }
                }
                Text { text: "COMPILE  " + (editorState.lastCurveCompileUs || 0) + " µs"; color: "#92aab0"; font.pixelSize: 11; font.family: "Consolas"; font.bold: true; Layout.alignment: Qt.AlignRight | Qt.AlignVCenter }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: editor.width >= 1400 ? 3 : editor.width >= 880 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14

            AviationPanel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: editorState.pointEditing ? 326 : 152
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 9
                    RowLayout {
                        Layout.fillWidth: true
                        CardHeading { text: "POINT EDITING" }
                        Item { Layout.fillWidth: true }
                        Text { text: editorState.pointEditing ? "● ACTIVE" : "OFF"; color: editorState.pointEditing ? "#9fd8c4" : "#a9b8bc"; font.pixelSize: 12; font.bold: true }
                        AviationToggle { checked: !!editorState.pointEditing; onToggled: { recordHistory(); backendObject.setCurvePointEditing(checked) } ToolTip.text: "Enable manual response-curve control points." }
                    }
                    Text { visible: !editorState.pointEditing; Layout.fillWidth: true; text: "Enable manual response-curve point editing. Drag empty graph space to pan; drag a point to edit it."; color: "#b0c0c4"; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    AviationButton { visible: !editorState.pointEditing; text: "ENABLE POINT EDITING"; role: "primary"; onClicked: { recordHistory(); backendObject.setCurvePointEditing(true) } }
                    GridLayout {
                        visible: !!editorState.pointEditing
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 6
                        FieldCaption { text: "SELECTED POINT" }
                        Text { text: selectedPoint >= 0 ? "POINT " + (selectedPoint + 1) : "SELECT A POINT"; color: "#b6e0e2"; font.pixelSize: 13; font.family: "Consolas"; font.bold: true }
                        FieldCaption { text: "INPUT" }
                        AviationSpinBox { id: pointInput; from: editorState.unipolar ? 0 : -1000; to: 1000; stepSize: 1; enabled: selectedPoint >= 0 && pointAt(selectedPoint) && !pointAt(selectedPoint).locked
                            value: pointAt(selectedPoint) ? Math.round(Number(pointAt(selectedPoint).input) * 1000) : 0
                            onValueModified: { const p = pointAt(selectedPoint); if (p) { recordHistory(); backendObject.setCurvePoint(selectedPoint, value / 1000, Number(p.output)) } } }
                        FieldCaption { text: "OUTPUT" }
                        AviationSpinBox { id: pointOutput; from: editorState.unipolar ? 0 : -1000; to: 1000; stepSize: 1; enabled: pointInput.enabled
                            value: pointAt(selectedPoint) ? Math.round(Number(pointAt(selectedPoint).output) * 1000) : 0
                            onValueModified: { const p = pointAt(selectedPoint); if (p) { recordHistory(); backendObject.setCurvePoint(selectedPoint, Number(p.input), value / 1000) } } }
                    }
                    RowLayout { visible: !!editorState.pointEditing; Layout.fillWidth: true; spacing: 8
                        AviationCheckBox { visible: !editorState.unipolar; text: "SYMMETRY"; checked: !!editorState.symmetry; onToggled: { recordHistory(); backendObject.setCurveSymmetry(checked) } ToolTip.text: "Mirror edits around the center point." }
                        AviationCombo { Layout.fillWidth: true; model: ["Smooth", "Linear"]; currentIndex: editorState.interpolation === "Linear" ? 1 : 0; onActivated: { recordHistory(); backendObject.setCurveInterpolation(currentText) } ToolTip.text: "Choose smooth or linear point interpolation." }
                    }
                    RowLayout { visible: !!editorState.pointEditing; Layout.fillWidth: true; spacing: 8
                        AviationCombo { Layout.fillWidth: true; model: [5, 7, 9, 13, 17, 25]; currentIndex: Math.max(0, model.indexOf(editorState.pointDensity)); onActivated: { recordHistory(); backendObject.setCurvePointDensity(currentValue) } ToolTip.text: "Choose the editable point density." }
                        AviationCombo { id: snapSelector; Layout.fillWidth: true; model: ["Snap Off", "5%", "2%", "1%", "0.5%", "0.1%"]; onActivated: { const values = [0, .05, .02, .01, .005, .001]; snapIncrement = values[currentIndex] } ToolTip.text: "Snap dragged points to an input/output grid." }
                    }
                    RowLayout { visible: !!editorState.pointEditing; Layout.fillWidth: true; spacing: 8
                        AviationButton { text: addingPoint ? "CLICK GRAPH" : "+ ADD POINT"; role: "primary"; onClicked: addingPoint = !addingPoint }
                        AviationButton { text: pointAt(selectedPoint) && pointAt(selectedPoint).locked ? "UNLOCK" : "LOCK"; enabled: selectedPoint > 0 && selectedPoint < editorState.pointCount - 1; onClicked: { const p = pointAt(selectedPoint); if (p) { recordHistory(); backendObject.setCurvePointLocked(selectedPoint, !p.locked) } } }
                        AviationButton { text: "REMOVE POINT"; role: "destructive"; enabled: selectedPoint > 0 && selectedPoint < editorState.pointCount - 1; onClicked: { recordHistory(); backendObject.removeCurvePoint(selectedPoint); selectedPoint = -1 } }
                    }
                }
            }

            AviationPanel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: 300
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 9
                    CardHeading { text: "VIEW / HISTORY" }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        AviationButton { text: "FRAME CURVE"; onClicked: graph.resetView(); ToolTip.text: "Fit the full curve domain in the graph." }
                        AviationButton { text: "RESET DISPLAY"; onClicked: editor.resetDisplay(); ToolTip.text: "Clear temporary graph view and preview state." }
                    }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        AviationButton { text: "↶ UNDO"; enabled: undoStack.length > 0; onClicked: undo() }
                        AviationButton { text: "↷ REDO"; enabled: redoStack.length > 0; onClicked: redo() }
                    }
                    FieldCaption { text: "COMPARE WITH" }
                    AviationCombo { id: comparisonSelector; Layout.fillWidth: true; model: backendObject ? backendObject.curveComparisonChoices : []; textRole: "label"; valueRole: "id"; onActivated: backendObject.setCurveComparison(currentValue); ToolTip.text: "Overlay another curve for comparison." }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            FieldCaption { text: "COPY FROM" }
                            AviationCombo { id: copySelector; Layout.fillWidth: true; model: backendObject ? backendObject.curveCopyChoices : []; textRole: "label"; valueRole: "id" }
                        }
                        AviationButton { text: "COPY FROM"; enabled: copySelector.count > 0; onClicked: { recordHistory(); backendObject.copyCurveFromSelection(copySelector.currentValue) } }
                    }
                    Text { visible: !!comparison.label; text: "DIFFERENCE  " + percent(comparison.difference); color: "#b4cbd0"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                }
            }

            AviationPanel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.columnSpan: editor.width >= 1400 ? 1 : editor.width >= 880 ? 2 : 1
                // This card contains wrapped copy and two button rows. Let
                // its layout determine the surface height so the bottom row
                // always keeps the same 16 px padding as other cards.
                Layout.preferredHeight: responsePresetContent.implicitHeight + 32
                ColumnLayout {
                    id: responsePresetContent
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 9
                    CardHeading { text: "RESPONSE / PRESETS" }
                    Text { text: "ACTIVE"; color: "#9eb4ba"; font.pixelSize: 12; font.bold: true }
                    Text { text: editorState.summary || "Linear · 0%"; color: "#dce9ea"; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: "Strength " + Math.round(Number(editorState.strength || 0) * 100) + "%"; color: "#a8d4d7"; font.family: "Consolas"; font.pixelSize: 12 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#4a626a" }
                    FieldCaption { text: "PRESET / OVERLAY" }
                    AviationCombo { id: previewSelector; Layout.fillWidth: true; model: backendObject ? backendObject.curvePreviewChoices : []; textRole: "label"; valueRole: "id"; onActivated: backendObject.previewCurvePreset(currentValue) }
                    Text { text: editorState.previewLabel ? "PREVIEW ACTIVE  ·  " + editorState.previewLabel : "Choose a preset to preview it in the graph."; color: editorState.previewLabel ? "#e3c178" : "#adbec2"; font.pixelSize: 12; font.bold: !!editorState.previewLabel; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        AviationButton { text: "APPLY PREVIEW"; role: "primary"; enabled: !!editorState.previewLabel; onClicked: { recordHistory(); backendObject.applyCurvePreview() } }
                        AviationButton { text: "CLEAR PREVIEW"; role: "destructive"; enabled: !!editorState.previewLabel; onClicked: backendObject.clearCurvePreview() }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#4a626a" }
                    FieldCaption { text: editorState.family === "Personal" ? "PERSONAL PRESET ACTIVE" : "PERSONAL PRESETS" }
                    Text { text: editorState.family === "Personal" ? "This response came from a reusable personal preset." : "Save the current response as a reusable preset."; color: "#adbec2"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        AviationButton { text: "SAVE AS PERSONAL"; onClicked: personalDialog.open() }
                        AviationButton { text: "MANAGE PERSONAL"; onClicked: personalManageDialog.open() }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: editor.width >= 980 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14
            AviationPanel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: 208
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8
                    RowLayout { Layout.fillWidth: true
                        CardHeading { text: "RESPONSE CHARACTERISTICS" }
                        Item { Layout.fillWidth: true }
                        Text { text: analysis.valid ? "✓ MONOTONIC" : "! CHECK CURVE"; color: analysis.valid ? "#9fd8c4" : "#dfb06f"; font.pixelSize: 12; font.bold: true }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 24
                        rowSpacing: 5
                        Repeater { model: [{n:"CENTER GAIN",v:analysis.centerGain},{n:"25% GAIN",v:analysis.quarterGain},{n:"50% GAIN",v:analysis.halfGain},{n:"75% GAIN",v:analysis.threeQuarterGain},{n:"PEAK GAIN",v:analysis.peakGain}]
                            delegate: RowLayout { Layout.fillWidth: true
                                Text { text: modelData.n; color: "#abc0c5"; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: Number(modelData.v || 0).toFixed(2) + "×"; color: "#e0edef"; font.family: "Consolas"; font.pixelSize: 14; font.bold: true }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#4a626a" }
                    Text { text: analysis.valid ? "✓ FULL AUTHORITY   ✓ NO OVERSHOOT" : "Curve validation needs attention."; color: analysis.valid ? "#9fd8c4" : "#dfb06f"; font.pixelSize: 12; font.bold: true }
                    Text { visible: !editorState.neutralMapsToNeutral; text: "ONE-SIDED J: physical neutral maps to " + percent(editorState.neutralOffset); color: "#e3c178"; font.pixelSize: 12; font.bold: true }
                }
            }
            AviationPanel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: 252
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 7
                    CardHeading { text: "SIGNAL PATH" }
                    Repeater { model: [{n:"RAW",v:editorState.physicalInput},{n:"NORMALIZED",v:editorState.normalized},{n:"DEADZONE",v:editorState.afterDeadzone},{n:"HYSTERESIS",v:editorState.afterHysteresis},{n:"INVERT",v:editorState.afterInversion},{n:"CURVE",v:editorState.curveResponse},{n:"LIMITS",v:editorState.finalOutput},{n:"VJOY",v:editorState.finalOutput}]
                        delegate: RowLayout { Layout.fillWidth: true
                            Text { text: modelData.n; color: "#abc0c5"; font.pixelSize: 12; Layout.fillWidth: true }
                            Text { text: rawPercent(modelData.v); color: "#d9e9eb"; font.family: "Consolas"; font.pixelSize: 13; font.bold: true }
                        }
                    }
                    Text { text: "LIVE VALUES · " + editorState.runtimeLutSamples + "-SAMPLE LUT"; color: "#91adb4"; font.pixelSize: 12; font.bold: true }
                }
            }
        }
        }
    }

    Menu {
        id: pointContextMenu
        background: Rectangle { color: "#101b20"; border.color: "#4c7881"; radius: 3 }
        AviationMenuItem { text: pointAt(contextPoint) && pointAt(contextPoint).locked ? "UNLOCK POINT" : "LOCK POINT"; enabled: contextPoint > 0 && contextPoint < editorState.pointCount - 1
            onTriggered: { const p = pointAt(contextPoint); if (p) { recordHistory(); backendObject.setCurvePointLocked(contextPoint, !p.locked) } } }
        MenuSeparator {}
        AviationMenuItem { text: "REMOVE POINT"; enabled: contextPoint > 0 && contextPoint < editorState.pointCount - 1 && pointAt(contextPoint) && !pointAt(contextPoint).locked
            onTriggered: { recordHistory(); backendObject.removeCurvePoint(contextPoint); selectedPoint = -1 } }
        MenuSeparator {}
        AviationMenuItem { text: "SNAP TO GRID"; enabled: contextPoint > 0 && pointAt(contextPoint) && !pointAt(contextPoint).locked
            onTriggered: { const p = pointAt(contextPoint); if (p) { recordHistory(); backendObject.setCurvePoint(contextPoint, Math.round(Number(p.input) / snapIncrement) * snapIncrement, Math.round(Number(p.output) / snapIncrement) * snapIncrement) } } }
        AviationMenuItem { text: "RESET POINT"; enabled: contextPoint > 0 && contextPoint < editorState.pointCount - 1 && pointAt(contextPoint) && !pointAt(contextPoint).locked
            onTriggered: { const p = pointAt(contextPoint); if (p) { recordHistory(); backendObject.setCurvePoint(contextPoint, Number(p.input), Number(p.input)) } } }
    }

    Menu {
        id: graphContextMenu
        background: Rectangle { color: "#101b20"; border.color: "#4c7881"; radius: 3 }
        AviationMenuItem { text: "ADD POINT HERE"; enabled: !!editorState.pointEditing
            onTriggered: { recordHistory(); selectedPoint = backendObject.addCurvePoint(graphContextInput, graphContextOutput) } }
        MenuSeparator {}
        AviationMenuItem { text: "FRAME CURVE"; onTriggered: graph.resetView() }
        AviationMenuItem { text: "RESET DISPLAY"; onTriggered: editor.resetDisplay() }
        MenuSeparator {}
        AviationMenuItem { text: editorState.pointEditing ? "POINT EDITING OFF" : "POINT EDITING ON"; onTriggered: { recordHistory(); backendObject.setCurvePointEditing(!editorState.pointEditing) } }
    }

    Dialog {
        id: personalDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Save personal curve preset"; standardButtons: Dialog.NoButton
        background: AviationPanel { color: "#f018252b"; border.color: "#5a7f89" }
        contentItem: Column { width: 330; spacing: 11
            FieldCaption { text: "NAME" }
            AviationTextField { id: personalName; width: parent.width; placeholderText: "My Precision Roll"; selectByMouse: true }
            Text { text: "Stores a copy for compatible axes. Existing applied curves are never linked."; width: parent.width; wrapMode: Text.WordWrap; color: "#a5b9bd"; font.pixelSize: 12 }
            Row { spacing: 8; AviationButton { text: "CANCEL"; onClicked: personalDialog.close() }
                AviationButton { text: "SAVE"; role: "primary"; enabled: personalName.text.trim().length > 0; onClicked: { if (backendObject.saveCurrentCurveAsPersonalPreset(personalName.text)) personalDialog.close() } } }
        }
        onOpened: { personalName.text = ""; personalName.forceActiveFocus() }
    }
    Dialog {
        id: personalManageDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; width: 530; title: "Manage personal curve presets"; standardButtons: Dialog.NoButton
        background: AviationPanel { color: "#f018252b"; border.color: "#5a7f89" }
        contentItem: Column { width: 480; spacing: 8
            Text { text: "Applying always copies a definition into the selected profile and axis."; color: "#a5b9bd"; font.pixelSize: 12 }
            Repeater { model: backendObject ? backendObject.personalCurvePresets : []
                delegate: RowLayout { width: parent.width; spacing: 8
                    Text { text: modelData.summary; color: "#b1c7ca"; font.pixelSize: 11; Layout.preferredWidth: 130; elide: Text.ElideRight }
                    AviationTextField { id: managedName; text: modelData.name; selectByMouse: true; Layout.fillWidth: true }
                    AviationButton { text: "RENAME"; onClicked: backendObject.renamePersonalCurvePreset(modelData.id, managedName.text) }
                    AviationButton { text: "DELETE"; role: "destructive"; onClicked: { personalDeleteDialog.presetId = modelData.id; personalDeleteDialog.presetName = modelData.name; personalDeleteDialog.open() } }
                }
            }
            AviationButton { text: "CLOSE"; anchors.right: parent.right; onClicked: personalManageDialog.close() }
        }
    }
    Dialog {
        id: personalDeleteDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; width: 340; property string presetId: ""; property string presetName: ""; title: "Delete personal preset?"; standardButtons: Dialog.NoButton
        background: AviationPanel { color: "#f018252b"; border.color: "#805b56" }
        contentItem: Column { width: 290; spacing: 10
            Text { text: "Delete \"" + personalDeleteDialog.presetName + "\"? Applied curves will remain unchanged."; wrapMode: Text.WordWrap; width: parent.width; color: "#d6e2e3"; font.pixelSize: 12 }
            Row { spacing: 8; AviationButton { text: "CANCEL"; onClicked: personalDeleteDialog.close() }
                AviationButton { text: "DELETE"; role: "destructive"; onClicked: { backendObject.deletePersonalCurvePreset(personalDeleteDialog.presetId); personalDeleteDialog.close() } } }
        }
    }
}
