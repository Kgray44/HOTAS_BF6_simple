import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: editor
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

    focus: true

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
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Text { text: "CURVE EDITOR"; color: "#f0f6f6"; font.pixelSize: 23; font.bold: true }
            Text { text: editorState.summary || "Linear"; color: "#80b9c2"; font.pixelSize: 11; font.bold: true }
            Item { Layout.fillWidth: true }
            Rectangle { width: 8; height: 8; radius: 4; color: backendObject && backendObject.mappingActive ? "#9fc9bb" : "#8e989c" }
            Text { text: backendObject && backendObject.mappingActive ? "MAPPING LIVE" : "MAPPING STANDBY"; color: "#a9c9c3"; font.pixelSize: 10; font.bold: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: "#151d22"
            border.color: "#3c5660"
            RowLayout {
                anchors.fill: parent; anchors.margins: 9; spacing: 9
                ColumnLayout { spacing: 2
                    Text { text: "PROFILE"; color: "#819aa3"; font.pixelSize: 8; font.bold: true }
                    ComboBox { id: profileSelector; Layout.preferredWidth: 145; model: backendObject ? backendObject.profiles : []
                        textRole: "name"; valueRole: "id"; currentIndex: backendObject ? backendObject.activeProfileIndex : 0
                        onActivated: backendObject.activateProfile(currentValue) }
                }
                ColumnLayout { spacing: 2
                    Text { text: "AXIS"; color: "#819aa3"; font.pixelSize: 8; font.bold: true }
                    ComboBox { id: axisSelector; Layout.preferredWidth: 164; model: backendObject ? backendObject.axes : []
                        textRole: "label"; valueRole: "index"; currentIndex: backendObject ? backendObject.selectedAxisIndex : 0
                        onActivated: backendObject.setSelectedAxis(currentValue) }
                }
                ColumnLayout { spacing: 2
                    Text { text: "FAMILY"; color: "#819aa3"; font.pixelSize: 8; font.bold: true }
                    ComboBox { id: familySelector; Layout.preferredWidth: 118; model: ["Linear", "J-Curve", "S-Curve", "Advanced", "Personal"]
                        currentIndex: {
                            const labels = ["Linear", "J-Curve", "S-Curve", "Advanced", "Personal"]
                            return Math.max(0, labels.indexOf(editorState.family === "Custom" ? "Linear" : editorState.family))
                        }
                        onActivated: {
                            if (currentText === "Personal") return
                            recordHistory(); backendObject.setCurveFamily(currentText)
                        }
                    }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Text { text: editorState.family === "Advanced" ? "ADVANCED PRESET" : editorState.family === "Personal" ? "PERSONAL PRESET" : "PRESET"; color: "#819aa3"; font.pixelSize: 8; font.bold: true }
                    ComboBox { id: presetSelector; Layout.fillWidth: true
                        model: editorState.family === "Advanced" ? (backendObject ? backendObject.curveAdvancedPresets : [])
                               : editorState.family === "Personal" ? (backendObject ? backendObject.personalCurvePresets : [])
                               : (backendObject ? backendObject.curveStandardPresets : [])
                        textRole: "name"; valueRole: "id"
                        currentIndex: {
                            for (let i = 0; i < model.length; ++i) if (model[i].id === editorState.presetId) return i
                            return 0
                        }
                        onActivated: {
                            recordHistory()
                            if (editorState.family === "Advanced") backendObject.applyAdvancedCurvePreset(currentValue)
                            else if (editorState.family === "Personal") backendObject.applyPersonalCurvePreset(currentValue)
                            else backendObject.setCurveStandardPreset(currentValue)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 30; spacing: 9
            Text { text: responseView ? "VIEW  RESPONSE" : "VIEW  GAIN"; color: "#a8c4c8"; font.pixelSize: 10; font.bold: true }
            Switch { checked: !responseView; onToggled: responseView = !checked }
            Text { text: responseView ? "INPUT + ACTIVE OUTPUT" : "LOCAL dy/dx"; color: "#7e969e"; font.pixelSize: 9 }
            CheckBox { text: "SHOW EFFECTIVE AXIS RESPONSE"; checked: showEffective; onToggled: showEffective = checked; font.pixelSize: 9 }
            Item { Layout.fillWidth: true }
            Text { text: "LUT " + (editorState.runtimeLutSamples || 4097) + " SAMPLES"; color: "#78949b"; font.pixelSize: 9; font.family: "Consolas" }
        }

        Rectangle {
            id: graphPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 300
            color: "#090e11"
            border.color: "#426370"

            Canvas {
                id: graph
                anchors.fill: parent; anchors.margins: 8
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
                readonly property real left: 42
                readonly property real right: 14
                readonly property real top: 14
                readonly property real bottom: 28
                function plotWidth() { return Math.max(1, width - left - right) }
                function plotHeight() { return Math.max(1, height - top - bottom) }
                function xFor(value) { return left + (value - xMin) / (xMax - xMin) * plotWidth() }
                function yFor(value) { return top + (1 - (value - yMin) / (yMax - yMin)) * plotHeight() }
                function inputFor(x) { return Math.max(domainMin, Math.min(1, xMin + (x - left) / plotWidth() * (xMax - xMin))) }
                function outputFor(y) { return Math.max(domainMin, Math.min(1, yMin + (1 - (y - top) / plotHeight()) * (yMax - yMin))) }
                function gainY(value) {
                    const peak = Math.max(1.25, Number(analysis.peakGain || 1) * 1.12)
                    return top + (1 - Math.max(0, Math.min(peak, value)) / peak) * plotHeight()
                }
                function resetView() { xMin = domainMin; xMax = 1; yMin = domainMin; yMax = 1; requestPaint() }
                function cursorAt(x, y) {
                    cursorInput = inputFor(x)
                    const inspected = backendObject.inspectCurve(cursorInput)
                    cursorOutput = Number(inspected.output || 0)
                    cursorGain = Number(inspected.gain || 0)
                    cursorVisible = x >= left && x <= width - right && y >= top && y <= height - bottom
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
                    ctx.fillStyle = "#071014"; ctx.fillRect(left, top, pw, ph)
                    ctx.strokeStyle = "#254550"; ctx.lineWidth = 1
                    for (let i = 0; i <= 4; ++i) {
                        const x = left + pw * i / 4, y = top + ph * i / 4
                        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + ph); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(left + pw, y); ctx.stroke()
                    }
                    if (responseView) {
                        ctx.strokeStyle = "#557984"; ctx.lineWidth = 1
                        if (domainMin < 0 && xMin <= 0 && xMax >= 0) {
                            ctx.beginPath(); ctx.moveTo(xFor(0), top); ctx.lineTo(xFor(0), top + ph); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(left, yFor(0)); ctx.lineTo(left + pw, yFor(0)); ctx.stroke()
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
                        ctx.beginPath(); ctx.moveTo(left, gainY(1)); ctx.lineTo(left + pw, gainY(1)); ctx.stroke(); ctx.setLineDash([])
                    }
                    if (cursorVisible) {
                        const cx = xFor(cursorInput), cy = responseView ? yFor(cursorOutput) : gainY(cursorGain)
                        ctx.strokeStyle = "#7ca1aa80"; ctx.setLineDash([3, 3])
                        ctx.beginPath(); ctx.moveTo(cx, top); ctx.lineTo(cx, top + ph); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(left, cy); ctx.lineTo(left + pw, cy); ctx.stroke(); ctx.setLineDash([])
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
                    for (let i = 0; i <= 4; ++i) ctx.fillText(labels[i], left + pw * i / 4 - 9, height - 7)
                }
            }
            MouseArea {
                anchors.fill: graph; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                onPressed: function(mouse) {
                    graph.cursorAt(mouse.x, mouse.y)
                    if (mouse.button === Qt.MiddleButton) {
                        graph.panning = true; graph.panStartX = mouse.x; graph.panStartY = mouse.y
                        graph.panOriginX = graph.xMin; graph.panOriginY = graph.yMin; return
                    }
                    if (addingPoint && editorState.pointEditing) {
                        recordHistory(); selectedPoint = backendObject.addCurvePoint(graph.inputFor(mouse.x), graph.outputFor(mouse.y)); addingPoint = false; return
                    }
                    if (!editorState.pointEditing) return
                    selectedPoint = graph.nearestPoint(mouse.x, mouse.y)
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
            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 14; color: "#102027e5"; border.color: "#405f68"; width: 180; height: 62; visible: graph.cursorVisible
                Column { anchors.fill: parent; anchors.margins: 7; spacing: 2
                    Text { text: "CURSOR  " + percent(graph.cursorInput); color: "#c7dfe2"; font.family: "Consolas"; font.pixelSize: 10 }
                    Text { text: "OUTPUT  " + percent(graph.cursorOutput); color: "#8fcfca"; font.family: "Consolas"; font.pixelSize: 10 }
                    Text { text: "GAIN    " + graph.cursorGain.toFixed(2) + "×"; color: "#d7bd78"; font.family: "Consolas"; font.pixelSize: 10 }
                }
            }
            Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; color: "#102027e5"; border.color: "#405f68"; width: 208; height: 24; visible: !!editorState.previewLabel
                Text { anchors.centerIn: parent; text: "PREVIEW  " + editorState.previewLabel; color: "#d8b86d"; font.pixelSize: 9; font.bold: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 46; color: "#141c20"; border.color: "#3a545d"
            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 14
                Text { text: "PHYSICAL INPUT  " + rawPercent(editorState.physicalInput); color: "#d5e3e5"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                Text { text: "CURVE RESPONSE  " + rawPercent(editorState.curveResponse); color: "#8fcfc8"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                Text { text: "FINAL OUTPUT  " + rawPercent(editorState.finalOutput); color: "#a9d6ba"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                Text { text: "LOCAL GAIN  " + Number(editorState.localGain || 0).toFixed(2) + "×"; color: "#e0bf77"; font.family: "Consolas"; font.pixelSize: 12; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: "compile " + (editorState.lastCurveCompileUs || 0) + " µs"; color: "#789198"; font.pixelSize: 9; font.family: "Consolas" }
            }
        }

        RowLayout { Layout.fillWidth: true; spacing: 8
            CheckBox { text: "POINT EDITING"; checked: editorState.pointEditing
                onToggled: { recordHistory(); backendObject.setCurvePointEditing(checked) }
                font.pixelSize: 10 }
            CheckBox { visible: !editorState.unipolar; text: "SYMMETRY"; checked: editorState.symmetry; enabled: editorState.pointEditing
                onToggled: { recordHistory(); backendObject.setCurveSymmetry(checked) }
                font.pixelSize: 10 }
            ComboBox { visible: editorState.pointEditing; Layout.preferredWidth: 103; model: ["Smooth", "Linear"]
                currentIndex: editorState.interpolation === "Linear" ? 1 : 0
                onActivated: { recordHistory(); backendObject.setCurveInterpolation(currentText) } }
            ComboBox { visible: editorState.pointEditing; Layout.preferredWidth: 84; model: [5, 7, 9, 13, 17, 25]
                currentIndex: Math.max(0, model.indexOf(editorState.pointDensity))
                onActivated: { recordHistory(); backendObject.setCurvePointDensity(currentValue) } }
            ComboBox { visible: editorState.pointEditing; id: snapSelector; Layout.preferredWidth: 90; model: ["Snap Off", "5%", "2%", "1%", "0.5%", "0.1%"]
                onActivated: { const values = [0, .05, .02, .01, .005, .001]; snapIncrement = values[currentIndex] } }
            Button { visible: editorState.pointEditing; text: addingPoint ? "CLICK GRAPH" : "+ ADD POINT"; onClicked: addingPoint = !addingPoint }
            Button { visible: editorState.pointEditing; text: "REMOVE"; enabled: selectedPoint > 0; onClicked: { recordHistory(); backendObject.removeCurvePoint(selectedPoint); selectedPoint = -1 } }
            Button { visible: editorState.pointEditing; text: pointAt(selectedPoint) && pointAt(selectedPoint).locked ? "UNLOCK" : "LOCK"; enabled: selectedPoint > 0
                onClicked: { const point = pointAt(selectedPoint); if (point) { recordHistory(); backendObject.setCurvePointLocked(selectedPoint, !point.locked) } } }
            Item { Layout.fillWidth: true }
            Button { text: "FIT CURVE"; onClicked: graph.resetView() }
            Button { text: "UNDO"; enabled: undoStack.length > 0; onClicked: undo() }
            Button { text: "REDO"; enabled: redoStack.length > 0; onClicked: redo() }
            Button { text: "RESET LINEAR"; onClicked: { recordHistory(); backendObject.resetCurveLinear() } }
            Button { text: "RESET SOURCE"; onClicked: { recordHistory(); backendObject.resetCurveToSource() } }
        }

        RowLayout { Layout.fillWidth: true; spacing: 8; visible: editorState.pointEditing && selectedPoint >= 0
            Text { text: "SELECTED POINT"; color: "#819ba3"; font.pixelSize: 9; font.bold: true }
            SpinBox { id: pointInput; from: editorState.unipolar ? 0 : -1000; to: 1000; stepSize: 1
                value: pointAt(selectedPoint) ? Math.round(Number(pointAt(selectedPoint).input) * 1000) : 0
                onValueModified: { const p = pointAt(selectedPoint); if (p) { recordHistory(); backendObject.setCurvePoint(selectedPoint, value / 1000, Number(p.output)) } } }
            Text { text: "INPUT %"; color: "#92aab0"; font.pixelSize: 9 }
            SpinBox { id: pointOutput; from: editorState.unipolar ? 0 : -1000; to: 1000; stepSize: 1
                value: pointAt(selectedPoint) ? Math.round(Number(pointAt(selectedPoint).output) * 1000) : 0
                onValueModified: { const p = pointAt(selectedPoint); if (p) { recordHistory(); backendObject.setCurvePoint(selectedPoint, Number(p.input), value / 1000) } } }
            Text { text: "OUTPUT %  ·  arrows 1%  ·  shift-arrows 0.1%"; color: "#92aab0"; font.pixelSize: 9 }
        }

        RowLayout { Layout.fillWidth: true; spacing: 8
            ComboBox { id: copySelector; Layout.preferredWidth: 210; model: backendObject ? backendObject.curveCopyChoices : []; textRole: "label"; valueRole: "id" }
            Button { text: "COPY FROM"; enabled: copySelector.count > 0; onClicked: { recordHistory(); backendObject.copyCurveFromSelection(copySelector.currentValue) } }
            ComboBox { id: comparisonSelector; Layout.preferredWidth: 244; model: backendObject ? backendObject.curveComparisonChoices : []; textRole: "label"; valueRole: "id"
                onActivated: backendObject.setCurveComparison(currentValue) }
            Text { visible: comparison.label; text: "CURRENT " + percent(comparison.currentOutput) + "  ·  REF " + percent(comparison.referenceOutput) + "  ·  Δ " + percent(comparison.difference)
                color: "#9fb6ba"; font.family: "Consolas"; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            ComboBox { id: previewSelector; Layout.preferredWidth: 218; model: backendObject ? backendObject.curvePreviewChoices : []; textRole: "label"; valueRole: "id"
                onActivated: backendObject.previewCurvePreset(currentValue) }
            Button { text: "APPLY PREVIEW"; enabled: !!editorState.previewLabel; onClicked: { recordHistory(); backendObject.applyCurvePreview() } }
            Button { text: "CLEAR PREVIEW"; enabled: !!editorState.previewLabel; onClicked: backendObject.clearCurvePreview() }
            Button { text: "SAVE PERSONAL"; onClicked: personalDialog.open() }
            Button { text: "MANAGE"; onClicked: personalManageDialog.open() }
        }

        RowLayout { Layout.fillWidth: true; spacing: 8
            Button { text: characteristicsVisible ? "RESPONSE CHARACTERISTICS ▾" : "RESPONSE CHARACTERISTICS ▸"; onClicked: characteristicsVisible = !characteristicsVisible }
            Item { Layout.fillWidth: true }
            Text { text: "Curve health  " + (analysis.valid ? "VALID" : "INVALID") + " · " + (analysis.peakGain > 3 ? "AGGRESSIVE " + Number(analysis.peakGain).toFixed(2) + "×" : "MONOTONIC / NO OVERSHOOT")
                color: analysis.valid ? "#9fc9bb" : "#d49b62"; font.pixelSize: 10; font.bold: true }
        }
        Rectangle { visible: characteristicsVisible; Layout.fillWidth: true; Layout.preferredHeight: 43; color: "#121a1e"; border.color: "#324a53"
            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 18
                Text { text: "CENTER " + Number(analysis.centerGain || 0).toFixed(2) + "×"; color: "#d2e0e1"; font.family: "Consolas"; font.pixelSize: 10 }
                Text { text: "25% " + Number(analysis.quarterGain || 0).toFixed(2) + "×"; color: "#d2e0e1"; font.family: "Consolas"; font.pixelSize: 10 }
                Text { text: "50% " + Number(analysis.halfGain || 0).toFixed(2) + "×"; color: "#d2e0e1"; font.family: "Consolas"; font.pixelSize: 10 }
                Text { text: "75% " + Number(analysis.threeQuarterGain || 0).toFixed(2) + "×"; color: "#d2e0e1"; font.family: "Consolas"; font.pixelSize: 10 }
                Text { text: "PEAK " + Number(analysis.peakGain || 0).toFixed(2) + "×"; color: "#e2bf78"; font.family: "Consolas"; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Text { text: "" + editorState.pointCount + " editable points · " + editorState.runtimeLutSamples + " LUT samples"; color: "#839ca3"; font.pixelSize: 9 }
            }
        }
        RowLayout { Layout.fillWidth: true; spacing: 8
            Button { text: signalPathVisible ? "SIGNAL PATH ▾" : "SIGNAL PATH ▸"; onClicked: signalPathVisible = !signalPathVisible }
            Item { Layout.fillWidth: true }
            Text { text: "HYSTERESIS IS LIVE-STATE ONLY"; color: "#79949a"; font.pixelSize: 9; font.bold: true }
        }
        Rectangle { visible: signalPathVisible; Layout.fillWidth: true; Layout.preferredHeight: 52; color: "#121a1e"; border.color: "#324a53"
            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 12
                Repeater { model: [
                    { n: "RAW", v: editorState.physicalInput }, { n: "NORMAL", v: editorState.normalized }, { n: "DEADZONE", v: editorState.afterDeadzone },
                    { n: "HYSTERESIS", v: editorState.afterHysteresis }, { n: "INVERT", v: editorState.afterInversion }, { n: "CURVE", v: editorState.curveResponse }, { n: "LIMITS", v: editorState.finalOutput }
                ]
                    delegate: Column { spacing: 2; Text { text: modelData.n; color: "#7f99a0"; font.pixelSize: 8; font.bold: true }
                        Text { text: rawPercent(modelData.v); color: "#cae0e1"; font.pixelSize: 10; font.family: "Consolas" } }
                }
            }
        }
    }

    Dialog {
        id: personalDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Save personal curve preset"; standardButtons: Dialog.NoButton
        contentItem: Column { width: 330; spacing: 11
            Text { text: "NAME"; color: "#8ba4ac"; font.pixelSize: 10; font.bold: true }
            TextField { id: personalName; width: parent.width; placeholderText: "My Precision Roll"; selectByMouse: true }
            Text { text: "Stores a copy for compatible axes. Existing applied curves are never linked."; width: parent.width; wrapMode: Text.WordWrap; color: "#879ba0"; font.pixelSize: 10 }
            Row { spacing: 8; Button { text: "CANCEL"; onClicked: personalDialog.close() }
                Button { text: "SAVE"; enabled: personalName.text.trim().length > 0; onClicked: { if (backendObject.saveCurrentCurveAsPersonalPreset(personalName.text)) personalDialog.close() } } }
        }
        onOpened: { personalName.text = ""; personalName.forceActiveFocus() }
    }
    Dialog {
        id: personalManageDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; width: 530; title: "Manage personal curve presets"; standardButtons: Dialog.Close
        contentItem: Column { width: 480; spacing: 8
            Text { text: "Applying always copies a definition into the selected profile and axis."; color: "#879ba0"; font.pixelSize: 10 }
            Repeater { model: backendObject ? backendObject.personalCurvePresets : []
                delegate: RowLayout { width: parent.width; spacing: 8
                    Text { text: modelData.summary; color: "#9dbbc0"; font.pixelSize: 9; Layout.preferredWidth: 130; elide: Text.ElideRight }
                    TextField { id: managedName; text: modelData.name; selectByMouse: true; Layout.fillWidth: true }
                    Button { text: "RENAME"; onClicked: backendObject.renamePersonalCurvePreset(modelData.id, managedName.text) }
                    Button { text: "DELETE"; onClicked: { personalDeleteDialog.presetId = modelData.id; personalDeleteDialog.presetName = modelData.name; personalDeleteDialog.open() } }
                }
            }
        }
    }
    Dialog {
        id: personalDeleteDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; width: 340; property string presetId: ""; property string presetName: ""; title: "Delete personal preset?"; standardButtons: Dialog.NoButton
        contentItem: Column { width: 290; spacing: 10
            Text { text: "Delete \"" + personalDeleteDialog.presetName + "\"? Applied curves will remain unchanged."; wrapMode: Text.WordWrap; width: parent.width; color: "#d6e2e3" }
            Row { spacing: 8; Button { text: "CANCEL"; onClicked: personalDeleteDialog.close() }
                Button { text: "DELETE"; onClicked: { backendObject.deletePersonalCurvePreset(personalDeleteDialog.presetId); personalDeleteDialog.close() } } }
        }
    }
}
