import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: root
    required property var backendObject
    required property var themeTokens
    property bool topGun: false
    property string editScope: "profile"
    property string targetId: ""
    property string renamePresetId: ""
    property string scenario: "Human-Like Rapid Reversal"
    property int contextEpoch: 0
    property int runtimeEpoch: 0
    property var liveState: backendObject.adaptiveResponseState
    property var runtimeState: {
        runtimeEpoch
        return backendObject.adaptiveResponseState
    }
    property var state: {
        contextEpoch
        return backendObject.adaptiveResponseContextState(editScope, selectedTargetId(), backendObject.selectedAxisIndex)
    }
    property var telemetry: backendObject.adaptiveResponseTelemetry
    property var previewSamples: backendObject.adaptiveResponsePreviewAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
    property var testLabMetrics: backendObject.adaptiveResponseTestLabAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
    property bool advancedExpanded: false
    property bool testLabExpanded: false
    property bool simulatorExpanded: false
    property bool liveAnalysisExpanded: false
    property int historyWindowSeconds: 5
    property bool historyPaused: false
    property var historySamples: []
    property int historyLastSequence: 0
    property int historyInspectIndex: -1
    property string comparisonScope: "preset"
    property string comparisonTargetId: "off"
    property var comparisonSamples: backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    property var comparisonTestLabMetrics: backendObject.adaptiveResponseTestLabAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    property string staticPreviewView: "predictor"
    property bool showPhysicalTrace: true
    property bool showEstimatedTrace: false
    property bool showPredictedTrace: true
    property bool showFinalTrace: false
    property real simulatorInput: 0
    property int simulatorSourceRate: 250
    property bool simulatorPaused: true
    property bool simulatorRecording: false
    property bool simulatorReplaying: false
    property int replaySlowdown: 1
    property real replayPresentationMs: 0
    property var simulatorSamples: []
    property var simulatorRecordingSamples: []
    property var simulatorDisplaySamples: []
    property int simulatorLastSequence: 0
    property bool simulatorNearViewport: false
    property bool liveAnalysisNearViewport: false

    function effective() { return state.runtimeEffective || state.effective || ({}) }
    function numericOr(value, fallback) {
        return value === undefined || value === null || Number.isNaN(Number(value)) ? fallback : Number(value)
    }
    function scopeInfo() { return state || ({}) }
    function propertyMask(key) {
        const masks = { enabled: 1, model: 2, maximumhorizonms: 4, maximumlead: 8,
            velocityresponse: 16, accelerationresponse: 32, motionsensitivity: 64,
            noiserejection: 128, reversaldetection: 256, reversalresponse: 512,
            decelerationresponse: 1024, settlingresponse: 2048, endpointtaper: 4096 }
        return masks[String(key).toLowerCase()] || 0
    }
    function inheritedHere(key) { return (Number(scopeInfo().properties || 0) & propertyMask(key)) === 0 }
    function targetChoices() {
        if (editScope === "global") return [{ id: "", name: "Application defaults" }]
        if (editScope === "category") return backendObject.profileCategories || []
        if (editScope === "preset") return (backendObject.adaptiveResponsePresets || []).filter(function(preset) { return !preset.builtIn })
        return (backendObject.profiles || []).map(function(profile) { return { id: profile.id, name: profile.displayName || profile.name } })
    }
    function selectedTargetId() {
        if (editScope === "global") return ""
        if (targetId.length > 0) return targetId
        if (editScope === "category") return liveState.categoryId || ""
        if (editScope === "preset") return targetChoices().length > 0 ? targetChoices()[0].id : ""
        return liveState.profileId || ""
    }
    function selectedTargetName() {
        const choices = targetChoices()
        const id = selectedTargetId()
        for (let index = 0; index < choices.length; ++index) if (choices[index].id === id) return choices[index].name
        return editScope === "global" ? "Application defaults" : "Choose a target"
    }
    function targetIndex() {
        const choices = targetChoices(); const id = selectedTargetId()
        for (let index = 0; index < choices.length; ++index) if (choices[index].id === id) return index
        return 0
    }
    function comparisonChoices() {
        const entries = []
        const presets = backendObject.adaptiveResponsePresets || []
        for (let i = 0; i < presets.length; ++i) entries.push({ label: "Preset · " + presets[i].name, scope: "preset", id: presets[i].id })
        const profiles = backendObject.profiles || []
        for (let j = 0; j < profiles.length; ++j) entries.push({ label: "Profile · " + (profiles[j].displayName || profiles[j].name), scope: "profile", id: profiles[j].id })
        return entries
    }
    function comparisonIndex() {
        const choices = comparisonChoices()
        for (let i = 0; i < choices.length; ++i) if (choices[i].scope === comparisonScope && choices[i].id === comparisonTargetId) return i
        return 0
    }
    function axisModelIndex(physicalAxis) {
        const choices = backendObject.axes || []
        for (let index = 0; index < choices.length; ++index) {
            if (Number(choices[index].index) === Number(physicalAxis)) return index
        }
        return 0
    }
    function selectAxisModelIndex(modelIndex) {
        const choices = backendObject.axes || []
        if (modelIndex < 0 || modelIndex >= choices.length) return false
        const entry = choices[modelIndex]
        if (!entry || entry.index === undefined || entry.index === null) return false
        backendObject.setSelectedAxis(Number(entry.index))
        root.setPreview()
        return true
    }
    function setStaticPreviewView(view) {
        staticPreviewView = view
        showPhysicalTrace = true
        showEstimatedTrace = view === "predictor"
        showPredictedTrace = true
        showFinalTrace = view === "pipeline"
    }
    function staticScenarioDurationMs() {
        if (!previewSamples || previewSamples.length === 0) return 0
        return root.numericOr(previewSamples[previewSamples.length - 1].time, 0) * 1000
    }
    function staticTimeTickLabels() {
        const duration = staticScenarioDurationMs()
        const labels = []
        for (let index = 0; index < 6; ++index) labels.push(Math.round(duration * index / 5) + " ms")
        return labels
    }
    function sectionNearViewport(section) {
        if (!section || !adaptiveScroll.contentItem) return false
        const top = section.mapToItem(adaptiveScroll.contentItem, 0, 0).y
        return top + section.height >= adaptiveScroll.contentY - 240
            && top <= adaptiveScroll.contentY + adaptiveScroll.height + 240
    }
    function refreshViewportActivity() {
        simulatorNearViewport = sectionNearViewport(simulatorCard)
        liveAnalysisNearViewport = sectionNearViewport(liveAnalysisCard)
    }
    function refreshSimulator(reset) {
        const update = backendObject.adaptiveResponseSimulatorHistorySince(reset ? 0 : simulatorLastSequence)
        const incoming = update.samples || []
        if (reset || update.reset) simulatorSamples = incoming
        else if (incoming.length > 0) {
            for (let index = 0; index < incoming.length; ++index) simulatorSamples.push(incoming[index])
            const retained = Math.max(180, Math.min(900, simulatorSamples.length))
            if (simulatorSamples.length > retained) simulatorSamples.splice(0, simulatorSamples.length - retained)
        }
        simulatorLastSequence = Number(update.newestSequence || simulatorLastSequence)
    }
    function sampleSimulator() {
        backendObject.adaptiveResponseSimulatorStepAtContext(simulatorInput, editScope, selectedTargetId(), backendObject.selectedAxisIndex, simulatorSourceRate)
        refreshSimulator()
    }
    function simulatorCurrentSample() {
        if (!simulatorDisplaySamples || simulatorDisplaySamples.length === 0) return ({})
        return simulatorDisplaySamples[simulatorDisplaySamples.length - 1] || ({})
    }
    function simulatorMagnitude(fields, minimum) {
        let maximum = minimum
        for (let index = 0; index < simulatorDisplaySamples.length; ++index) {
            for (let field = 0; field < fields.length; ++field) maximum = Math.max(maximum, Math.abs(root.numericOr(simulatorDisplaySamples[index][fields[field]], 0)))
        }
        return maximum
    }
    function startReplay() {
        simulatorRecordingSamples = backendObject.adaptiveResponseSimulatorRecording()
        if (simulatorRecordingSamples.length === 0) return
        simulatorReplaying = true
        simulatorPaused = false
        replayPresentationMs = 0
        simulatorDisplaySamples = [simulatorRecordingSamples[0]]
    }
    function updateReplayPresentation() {
        if (!simulatorReplaying || simulatorPaused || simulatorRecordingSamples.length === 0) return
        replayPresentationMs += 16
        const originalElapsed = replayPresentationMs / Math.max(1, replaySlowdown)
        const replayed = []
        for (let index = 0; index < simulatorRecordingSamples.length; ++index) {
            const sample = simulatorRecordingSamples[index]
            if (root.numericOr(sample.recordedElapsedMs, 0) <= originalElapsed) replayed.push(sample)
            else break
        }
        simulatorDisplaySamples = replayed.length > 0 ? replayed : [simulatorRecordingSamples[0]]
        const finalSample = simulatorRecordingSamples[simulatorRecordingSamples.length - 1]
        if (originalElapsed >= root.numericOr(finalSample.recordedElapsedMs, 0)) {
            simulatorPaused = true
            simulatorReplaying = false
        }
    }
    function setPreview() {
        previewSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
        testLabMetrics = backendObject.adaptiveResponseTestLabAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
        comparisonSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
        comparisonTestLabMetrics = backendObject.adaptiveResponseTestLabAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    }
    function applySimplePreset(presetId) {
        const applied = backendObject.setAdaptiveResponsePresetAtContext(
            editScope, selectedTargetId(), root.state.axis, presetId)
        if (applied) root.setPreview()
        return applied
    }
    function refreshHistory(reset) {
        if (historyPaused) return
        const update = backendObject.adaptiveResponseHistorySince(reset ? 0 : historyLastSequence, historyWindowSeconds)
        const incoming = update.samples || []
        if (reset || update.reset) historySamples = incoming
        else if (incoming.length > 0) {
            for (let index = 0; index < incoming.length; ++index) historySamples.push(incoming[index])
            const maximum = Math.max(180, historyWindowSeconds * 100)
            if (historySamples.length > maximum) historySamples.splice(0, historySamples.length - maximum)
        }
        historyLastSequence = Number(update.newestSequence || historyLastSequence)
    }
    function historyMagnitude(fields, minimum) {
        let maximum = minimum
        for (let i = 0; i < historySamples.length; ++i) {
            for (let j = 0; j < fields.length; ++j) maximum = Math.max(maximum, Math.abs(root.numericOr(historySamples[i][fields[j]], 0)))
        }
        return maximum
    }
    function inspectedHistorySample() {
        if (!historySamples || historySamples.length === 0) return ({})
        const index = Math.max(0, Math.min(historySamples.length - 1, historyInspectIndex < 0 ? historySamples.length - 1 : historyInspectIndex))
        return historySamples[index] || ({})
    }
    function runtimeAutomationText() {
        const automation = runtimeState.automation || ({})
        if (!automation.active) return "None"
        const affected = automation.affectedProperties || []
        return affected.length > 0 ? "Automation · " + affected.join(", ") : "Automation overlay active"
    }
    function percent(value) { return (Number(value) * 100 >= 0 ? "+" : "") + (Number(value) * 100).toFixed(1) + "%" }
    onStateChanged: { setPreview(); historyLastSequence = 0; historySamples = []; refreshHistory(true) }
    onHistoryWindowSecondsChanged: { historyLastSequence = 0; historySamples = []; refreshHistory(true) }
    onHistorySamplesChanged: {
        if (!historyPaused) historyInspectIndex = historySamples.length - 1
        else historyInspectIndex = Math.max(0, Math.min(historyInspectIndex, historySamples.length - 1))
    }
    onHistoryPausedChanged: if (historyPaused) historyInspectIndex = historySamples.length - 1
    Component.onCompleted: refreshViewportActivity()
    Connections {
        target: backendObject
        function onStateChanged() { root.contextEpoch += 1; root.runtimeEpoch += 1 }
        function onInputTelemetryChanged() { root.runtimeEpoch += 1 }
    }
    Timer { interval: 33; running: root.liveAnalysisExpanded && root.liveAnalysisNearViewport && !root.historyPaused; repeat: true; triggeredOnStart: true; onTriggered: root.refreshHistory(false) }
    Timer { interval: 16; running: root.simulatorExpanded && root.simulatorNearViewport && !root.simulatorPaused && !root.simulatorReplaying; repeat: true; triggeredOnStart: true; onTriggered: root.sampleSimulator() }
    Timer { interval: 16; running: root.simulatorExpanded && root.simulatorNearViewport && root.simulatorReplaying && !root.simulatorPaused; repeat: true; onTriggered: root.updateReplayPresentation() }
    Timer { interval: 33; running: root.simulatorExpanded && root.simulatorNearViewport && !root.simulatorReplaying; repeat: true; triggeredOnStart: true; onTriggered: { root.refreshSimulator(false); root.simulatorDisplaySamples = root.simulatorSamples.slice(0) } }

    component Card: Rectangle {
        default property alias content: contentHost.data
        property color tone: root.themeTokens.border
        color: root.themeTokens.panel
        border.color: tone
        radius: root.themeTokens.controlRadius
        // Cards live in the Flickable's content column.  Rectangle has no
        // layout fill behavior of its own, so bind every card to that column
        // instead of shrinking it to the first child’s implicit width.
        width: parent ? parent.width : implicitWidth
        implicitHeight: contentHost.implicitHeight + 28
        Item { id: contentHost; anchors.fill: parent; anchors.margins: 14; implicitHeight: childrenRect.height }
    }
    component Caption: Text {
        color: root.themeTokens.textMuted
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 0.6
    }
    component ActionButton: Button {
        id: actionButton
        property bool accent: true
        implicitHeight: 34
        padding: 12
        font.pixelSize: 10
        font.bold: true
        contentItem: Text { text: actionButton.text; color: actionButton.enabled ? root.themeTokens.textStrong : root.themeTokens.textFaint; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: actionButton.font }
        background: Rectangle { radius: root.themeTokens.controlRadius; color: actionButton.down ? root.themeTokens.controlPressed : actionButton.hovered ? root.themeTokens.controlHover : (actionButton.accent ? root.themeTokens.buttonSurface : root.themeTokens.buttonSecondary); border.color: actionButton.accent ? root.themeTokens.orange : root.themeTokens.border }
    }
    // Keep every Adaptive Response selector inside the same dark, themed
    // control family as the shell's FlightComboBox. Qt's unstyled ComboBox
    // otherwise falls back to a white native field/popup in all three skins.
    component ResponseCombo: ComboBox {
        id: combo
        property int popupMaximumHeight: 264
        // A state-changing selection can rebuild this page while Qt's
        // ComboBox is closing its popup. Report the chosen row directly so
        // axis/context updates never depend on that deferred close sequence.
        signal choiceActivated(int index, var value)
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 29
        font.pixelSize: 10
        background: Rectangle {
            radius: root.themeTokens.controlRadius
            color: combo.enabled ? (combo.hovered ? root.themeTokens.controlHover : root.themeTokens.control) : root.themeTokens.controlDisabled
            border.color: combo.activeFocus ? root.themeTokens.orange : combo.hovered ? root.themeTokens.borderStrong : root.themeTokens.border
        }
        contentItem: Text {
            leftPadding: combo.leftPadding; rightPadding: combo.rightPadding
            text: combo.displayText; color: combo.enabled ? root.themeTokens.text : root.themeTokens.textFaint
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font: combo.font
        }
        indicator: Text {
            x: combo.width - width - 10; y: (combo.height - height) / 2
            text: "⌄"; color: combo.enabled ? root.themeTokens.textMuted : root.themeTokens.textFaint; font.pixelSize: 15
        }
        delegate: Rectangle {
            id: choice
            objectName: combo.objectName.length > 0 ? combo.objectName + "Choice_" + index : "responseComboChoice_" + index
            width: combo.width; implicitHeight: 32
            property bool highlighted: combo.highlightedIndex === index
            property bool hovered: choiceMouse.containsMouse
            radius: root.themeTokens.controlRadius
            color: choice.highlighted ? root.themeTokens.selection : (choice.hovered ? root.themeTokens.controlHover : "transparent")
            border.color: choice.highlighted ? root.themeTokens.orange : "transparent"
            Text {
                anchors.fill: parent; leftPadding: 10; rightPadding: 10; text: combo.textAt(index)
                color: choice.highlighted ? root.themeTokens.textStrong : root.themeTokens.text
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 10
            }
            MouseArea {
                id: choiceMouse
                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: {
                    combo.choiceActivated(index, combo.valueAt(index))
                    Qt.callLater(function() { if (combo.popup.visible) combo.popup.close() })
                }
            }
        }
        popup: Popup {
            objectName: combo.objectName.length > 0 ? combo.objectName + "Popup" : "responseComboPopup"
            y: combo.height + 4; width: combo.width
            implicitHeight: Math.min(combo.popupMaximumHeight, choices.contentHeight + topPadding + bottomPadding)
            topPadding: 6; bottomPadding: 6; leftPadding: 6; rightPadding: 6
            contentItem: ListView {
                id: choices; clip: true; implicitHeight: contentHeight
                model: combo.delegateModel; currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                radius: root.themeTokens.controlRadius; color: root.themeTokens.tooltip
                border.color: root.themeTokens.borderStrong
            }
        }
    }
    component Metric: Item {
        property string caption: ""
        property string value: "—"
        property color tone: root.themeTokens.text
        implicitWidth: 135; implicitHeight: 46
        Column { anchors.verticalCenter: parent.verticalCenter; spacing: 3
            Caption { text: parent.parent.caption }
            Text { text: parent.parent.value; color: parent.parent.tone; font.pixelSize: 16; font.bold: true; font.family: root.themeTokens.telemetryFont }
        }
    }
    component Gauge: Item {
        property string caption: ""
        property real value: 0
        property real maximum: 1
        property color tone: root.themeTokens.orange
        implicitWidth: 170; implicitHeight: 44
        Column { anchors.fill: parent; spacing: 5
            Caption { text: parent.parent.caption }
            Rectangle { width: parent.width; height: 8; radius: 4; color: root.themeTokens.panelInset; border.color: root.themeTokens.border
                Rectangle { width: parent.width * Math.max(0, Math.min(1, parent.parent.parent.value / Math.max(0.0001, parent.parent.parent.maximum))); height: parent.height; radius: parent.radius; color: parent.parent.parent.tone }
            }
        }
    }
    component HistoryGraph: Canvas {
        id: historyGraph
        property var series: []
        property real lowerBound: -1
        property real upperBound: 1
        onSeriesChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = root.themeTokens.panelInset
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = root.themeTokens.divider
            ctx.lineWidth = 1
            const center = lowerBound < 0 && upperBound > 0
                ? height * (1 - (0 - lowerBound) / Math.max(0.0001, upperBound - lowerBound)) : height - 1
            ctx.beginPath(); ctx.moveTo(0, center); ctx.lineTo(width, center); ctx.stroke()
            if (!root.historySamples || root.historySamples.length === 0) return
            for (let line = 0; line < series.length; ++line) {
                const descriptor = series[line]
                ctx.strokeStyle = descriptor.color
                ctx.lineWidth = 2
                ctx.beginPath()
                for (let index = 0; index < root.historySamples.length; ++index) {
                    const point = root.historySamples[index]
                    const value = Math.max(lowerBound, Math.min(upperBound, Number(point[descriptor.field] || 0)))
                    const x = index * width / Math.max(1, root.historySamples.length - 1)
                    const y = height * (1 - (value - lowerBound) / Math.max(0.0001, upperBound - lowerBound))
                    if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
            if (root.historyPaused && root.historyInspectIndex >= 0 && root.historySamples.length > 0) {
                const inspectX = root.historyInspectIndex * width / Math.max(1, root.historySamples.length - 1)
                ctx.strokeStyle = root.themeTokens.orange
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(inspectX, 0); ctx.lineTo(inspectX, height); ctx.stroke()
            }
        }
        Connections { target: root; function onHistorySamplesChanged() { historyGraph.requestPaint() } function onHistoryInspectIndexChanged() { historyGraph.requestPaint() } }
    }
    component TimeAxis: RowLayout {
        id: timeAxis
        property int seconds: 5
        Layout.fillWidth: true
        Repeater {
            model: 6
            delegate: Caption {
                required property int index
                text: index === 5 ? "NOW" : "−" + Math.round(timeAxis.seconds * (1 - index / 5)) + " s"
                Layout.fillWidth: true
                horizontalAlignment: index === 0 ? Text.AlignLeft : index === 5 ? Text.AlignRight : Text.AlignHCenter
            }
        }
    }
    component SimulatorGraph: Canvas {
        id: simulatorGraph
        property var samples: []
        property var series: []
        property real lowerBound: -1
        property real upperBound: 1
        onSamplesChanged: requestPaint()
        onSeriesChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = root.themeTokens.panelInset
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = root.themeTokens.divider
            ctx.lineWidth = 1
            const centre = lowerBound < 0 && upperBound > 0 ? height * (1 - (0 - lowerBound) / Math.max(0.0001, upperBound - lowerBound)) : height - 1
            ctx.beginPath(); ctx.moveTo(0, centre); ctx.lineTo(width, centre); ctx.stroke()
            for (let line = 0; line < series.length; ++line) {
                if (!samples || samples.length === 0) continue
                const descriptor = series[line]
                ctx.strokeStyle = descriptor.color
                ctx.lineWidth = 2
                ctx.beginPath()
                // Piecewise-linear display resampling only. It fills wide
                // canvases without filtering, changing stored samples, or
                // hiding a real reversal discontinuity.
                const renderCount = Math.max(samples.length, Math.floor(width))
                for (let index = 0; index < renderCount; ++index) {
                    const sourcePosition = index * (samples.length - 1) / Math.max(1, renderCount - 1)
                    const leftIndex = Math.floor(sourcePosition)
                    const rightIndex = Math.min(samples.length - 1, leftIndex + 1)
                    const fraction = sourcePosition - leftIndex
                    const left = root.numericOr(samples[leftIndex][descriptor.field], 0)
                    const right = root.numericOr(samples[rightIndex][descriptor.field], 0)
                    const value = Math.max(lowerBound, Math.min(upperBound, left + (right - left) * fraction))
                    const x = index * width / Math.max(1, renderCount - 1)
                    const y = height * (1 - (value - lowerBound) / Math.max(0.0001, upperBound - lowerBound))
                    if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }
    }
    component VerticalSimulatorSlider: Slider {
        id: verticalSlider
        orientation: Qt.Vertical
        from: -1
        to: 1
        implicitWidth: 54
        implicitHeight: 250
        topPadding: 9
        bottomPadding: 9
        background: Rectangle {
            x: (verticalSlider.width - width) / 2
            y: verticalSlider.topPadding
            width: 8
            height: verticalSlider.availableHeight
            radius: 4
            color: root.themeTokens.panelInset
            border.color: root.themeTokens.border
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: verticalSlider.visualPosition < 0.5 ? parent.height * verticalSlider.visualPosition : parent.height / 2
                width: parent.width
                height: Math.abs(parent.height * (verticalSlider.visualPosition - 0.5))
                radius: parent.radius
                color: root.themeTokens.orange
            }
        }
        handle: Rectangle {
            x: (verticalSlider.width - width) / 2
            y: verticalSlider.topPadding + (1 - (verticalSlider.value - verticalSlider.from) / (verticalSlider.to - verticalSlider.from)) * (verticalSlider.availableHeight - height)
            width: 20
            height: 16
            radius: root.topGun ? 1 : 8
            color: verticalSlider.pressed ? root.themeTokens.orange : root.themeTokens.buttonSurface
            border.color: root.themeTokens.orange
        }
    }
    component ThemedSlider: Slider {
        id: themedSlider
        implicitHeight: 26
        leftPadding: 8
        rightPadding: 8
        background: Rectangle {
            x: themedSlider.leftPadding
            y: (themedSlider.height - height) / 2
            width: themedSlider.availableWidth
            height: 6
            radius: 3
            color: root.themeTokens.panelInset
            border.color: root.themeTokens.border
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, themedSlider.visualPosition))
                height: parent.height
                radius: parent.radius
                color: root.themeTokens.orange
            }
        }
        handle: Rectangle {
            x: themedSlider.leftPadding + themedSlider.visualPosition * (themedSlider.availableWidth - width)
            y: (themedSlider.height - height) / 2
            width: 16
            height: 16
            radius: root.topGun ? 1 : 8
            color: themedSlider.pressed ? root.themeTokens.orange : root.themeTokens.buttonSurface
            border.color: root.themeTokens.orange
        }
    }
    component ThemedSwitch: Switch {
        id: themedSwitch
        implicitWidth: 48
        implicitHeight: 28
        indicator: Rectangle {
            x: (themedSwitch.width - width) / 2
            y: (themedSwitch.height - height) / 2
            width: 42
            height: 20
            radius: root.topGun ? 1 : 10
            color: themedSwitch.checked ? root.themeTokens.ready : root.themeTokens.controlDisabled
            border.color: themedSwitch.checked ? root.themeTokens.ready : root.themeTokens.border
            Rectangle {
                width: 14
                height: 14
                radius: root.topGun ? 1 : 7
                x: themedSwitch.checked ? parent.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                color: themedSwitch.checked ? root.themeTokens.panelInset : root.themeTokens.textMuted
                Behavior on x { NumberAnimation { duration: 110 } }
            }
        }
        contentItem: Item {}
    }
    component TuneRow: Rectangle {
        id: tuneRow
        property string label: ""
        property string detail: ""
        property real value: 0
        property real from: 0
        property real to: 1
        property real step: 0.01
        property string propertyKey: ""
        property string unit: "gain"
        signal changed(real value)
        function displayValue() {
            const safe = root.numericOr(value, 0)
            if (unit === "ms") return safe.toFixed(1) + " ms"
            if (unit === "%") return (safe * 100).toFixed(1) + "%"
            if (unit === "axis") return safe.toFixed(3) + " axis"
            if (unit === "axis/s") return safe.toFixed(3) + " axis/s"
            return safe.toFixed(2)
        }
        width: parent ? parent.width : implicitWidth
        implicitHeight: 82
        radius: root.themeTokens.controlRadius
        color: root.themeTokens.panel
        border.color: root.inheritedHere(propertyKey) && root.editScope !== "global" ? root.themeTokens.border : root.themeTokens.borderStrong
        RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 12
            ColumnLayout { Layout.preferredWidth: 300; Layout.maximumWidth: 360; Layout.fillWidth: true; spacing: 3
                Text { text: tuneRow.label; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                Text { text: tuneRow.detail; color: root.themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Caption { text: root.editScope === "global" ? "APPLICATION DEFAULT" : root.inheritedHere(tuneRow.propertyKey) ? "INHERITED FROM PARENT" : "OVERRIDE AT THIS LEVEL" }
            }
            ThemedSlider { id: slider; Layout.fillWidth: true; Layout.minimumWidth: 180; from: tuneRow.from; to: tuneRow.to; stepSize: tuneRow.step; value: tuneRow.value; onMoved: tuneRow.changed(value) }
            Text { Layout.preferredWidth: 78; text: tuneRow.displayValue(); color: root.themeTokens.textStrong; horizontalAlignment: Text.AlignRight; font.pixelSize: 11; font.family: root.themeTokens.telemetryFont }
            ActionButton {
                visible: tuneRow.propertyKey.length > 0
                text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere(tuneRow.propertyKey) ? "INHERITED" : "INHERIT"
                accent: false
                enabled: root.editScope === "global" || !root.inheritedHere(tuneRow.propertyKey)
                implicitHeight: 28
                padding: 8
                onClicked: {
                    backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, tuneRow.propertyKey, 0, true)
                    root.setPreview()
                }
            }
        }
    }
    component TuneGroup: Rectangle {
        id: tuneGroup
        property string title: ""
        property string detail: ""
        default property alias content: tuneGroupContent.data
        width: parent ? parent.width : implicitWidth
        implicitHeight: tuneGroupContent.implicitHeight + 24
        radius: root.themeTokens.controlRadius
        color: root.themeTokens.panelInset
        border.color: root.themeTokens.border
        Column {
            id: tuneGroupContent
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Text { text: tuneGroup.title; color: root.themeTokens.textStrong; font.pixelSize: 12; font.bold: true }
            Text { visible: tuneGroup.detail.length > 0; text: tuneGroup.detail; color: root.themeTokens.textMuted; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap }
        }
    }

    Flickable {
        id: adaptiveScroll
        objectName: "adaptiveResponseScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 18
        clip: true
        onContentYChanged: root.refreshViewportActivity()
        onHeightChanged: root.refreshViewportActivity()
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: content
            x: 1
            width: parent.width - 10
            spacing: 14

            RowLayout {
                width: parent.width
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: "Adaptive Response"; color: root.themeTokens.textStrong; font.pixelSize: root.topGun ? 36 : 26; font.bold: true; font.family: root.themeTokens.displayFont }
                    Text { text: "Short-horizon physical-motion prediction that leads intentional input without smoothing or persistent offset."; color: root.themeTokens.textMuted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                ActionButton { text: "MANAGE PRESETS"; accent: false; onClicked: presetManageDialog.open() }
                ActionButton { text: "SAVE AS PRESET"; onClicked: savePresetDialog.open() }
            }

            Card {
                Column { width: parent.width; spacing: 7
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Effective runtime"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Read-only worker configuration for the active profile and axis. Editing another target above never changes this view."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { text: runtimeState.automation && runtimeState.automation.active ? "AUTOMATION ACTIVE" : "PERSISTENT"; color: runtimeState.automation && runtimeState.automation.active ? root.themeTokens.orange : root.themeTokens.ready; font.pixelSize: 10; font.bold: true }
                    }
                    Text { text: "Built-in default  →  Global: " + ((runtimeState.global && runtimeState.global.source) || "Application default") + "  →  Category: " + ((runtimeState.categoryLayer && runtimeState.categoryLayer.source) || "Inherited") + "  →  Profile: " + ((runtimeState.profileLayer && runtimeState.profileLayer.source) || "Inherited") + "  →  Automation: " + root.runtimeAutomationText() + "  →  Effective runtime"; color: root.themeTokens.text; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                    Row { spacing: 28
                        Metric { caption: "PROFILE"; value: runtimeState.profile || "—" }
                        Metric { caption: "CATEGORY"; value: runtimeState.category || "General" }
                        Metric { caption: "AXIS"; value: runtimeState.axisLabel || "—" }
                        Metric { caption: "STATUS"; value: runtimeState.runtimeEffective && runtimeState.runtimeEffective.enabled ? "ON" : "OFF"; tone: runtimeState.runtimeEffective && runtimeState.runtimeEffective.enabled ? root.themeTokens.ready : root.themeTokens.textMuted }
                        Metric { caption: "PREDICTOR"; value: runtimeState.runtimeEffective && runtimeState.runtimeEffective.enabled ? String(runtimeState.runtimeEffective.model || "auto").toUpperCase() : "INACTIVE"; tone: root.themeTokens.orange }
                        Metric { caption: runtimeState.runtimeEffective && runtimeState.runtimeEffective.enabled ? "MAX HORIZON" : "DORMANT HORIZON"; value: Number((runtimeState.runtimeEffective && runtimeState.runtimeEffective.maximumHorizonMs) || 0).toFixed(1) + " ms" }
                        Metric { caption: runtimeState.runtimeEffective && runtimeState.runtimeEffective.enabled ? "MAX LEAD" : "DORMANT LEAD"; value: root.percent((runtimeState.runtimeEffective && runtimeState.runtimeEffective.maximumLead) || 0) }
                    }
                }
            }

            Card {
                RowLayout { width: parent.width; spacing: 14
                    ColumnLayout { Layout.preferredWidth: 170
                        Caption { text: "EDIT LEVEL" }
                        ResponseCombo { objectName: "adaptiveEditScopeSelector"; Layout.fillWidth: true; model: [{name:"Global Defaults", value:"global"}, {name:"Category", value:"category"}, {name:"Game Profile", value:"profile"}, {name:"Response Preset", value:"preset"}]; textRole: "name"; valueRole: "value"; currentIndex: root.editScope === "global" ? 0 : root.editScope === "category" ? 1 : root.editScope === "preset" ? 3 : 2; onChoiceActivated: function(index, value) { root.editScope = String(value); root.targetId = ""; root.setPreview() } }
                    }
                    ColumnLayout { Layout.preferredWidth: 220
                        Caption { text: "TARGET" }
                        ResponseCombo { objectName: "adaptiveTargetSelector"; Layout.fillWidth: true; model: root.targetChoices(); textRole: "name"; valueRole: "id"; currentIndex: root.targetIndex(); onChoiceActivated: function(index, value) { root.targetId = String(value); root.setPreview() } }
                    }
                    ColumnLayout { Layout.preferredWidth: 240
                        Caption { text: "AXIS" }
                        ResponseCombo { id: axisSelector; objectName: "adaptiveAxisSelector"; Layout.fillWidth: true; model: backendObject.axes; textRole: "label"; valueRole: "index"; currentIndex: root.axisModelIndex(backendObject.selectedAxisIndex); onChoiceActivated: function(index) { root.selectAxisModelIndex(index) } }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.themeTokens.divider }
                    ColumnLayout { Layout.fillWidth: true
                        Caption { text: "EDITING CONTEXT" }
                        Text { text: root.editScope === "preset" ? "Response Preset → " + root.selectedTargetName() + " → " + (root.state.axisLabel || "Axis") : "Editing " + root.editScope.toUpperCase() + " · " + root.selectedTargetName() + " · " + (scopeInfo().source || "Inherited") + " · " + (root.state.axisLabel || "Axis"); color: root.themeTokens.text; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
            }

            Card {
                id: simulatorCard
                Column { width: parent.width; spacing: 10
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Interactive simulator"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "An isolated production AdaptiveResponseProcessor. It uses the selected context but never writes mapper state, DirectInput state, or vJoy output."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Caption { text: root.simulatorReplaying ? "REPLAY" : root.simulatorRecording ? "RECORDING" : root.simulatorPaused ? "PAUSED" : "LIVE" }
                        ActionButton { text: root.simulatorExpanded ? "COLLAPSE" : "OPEN"; accent: false; onClicked: root.simulatorExpanded = !root.simulatorExpanded }
                    }
                    Column { visible: root.simulatorExpanded; width: parent.width; spacing: 10
                    RowLayout { width: parent.width; spacing: 7
                        ActionButton { text: "LIVE"; implicitHeight: 28; padding: 8; accent: !root.simulatorPaused && !root.simulatorReplaying && !root.simulatorRecording; onClicked: { root.simulatorReplaying = false; root.simulatorRecording = false; backendObject.adaptiveResponseSimulatorStopRecording(); root.simulatorPaused = false; root.refreshSimulator() } }
                        ActionButton { text: "RECORD"; implicitHeight: 28; padding: 8; accent: root.simulatorRecording; onClicked: { root.simulatorReplaying = false; backendObject.adaptiveResponseSimulatorStartRecording(); root.simulatorRecording = true; root.simulatorPaused = false; root.refreshSimulator() } }
                        ActionButton { text: "STOP"; implicitHeight: 28; padding: 8; accent: false; onClicked: { backendObject.adaptiveResponseSimulatorStopRecording(); root.simulatorRecording = false; root.simulatorReplaying = false; root.simulatorPaused = true; root.refreshSimulator() } }
                        ActionButton { text: "REPLAY"; implicitHeight: 28; padding: 8; accent: root.simulatorReplaying; enabled: backendObject.adaptiveResponseSimulatorRecording().length > 0; onClicked: { backendObject.adaptiveResponseSimulatorStopRecording(); root.simulatorRecording = false; root.startReplay() } }
                        ActionButton { text: root.simulatorPaused ? "RESUME" : "PAUSE"; implicitHeight: 28; padding: 8; accent: false; onClicked: root.simulatorPaused = !root.simulatorPaused }
                        ActionButton { text: "CLEAR"; implicitHeight: 28; padding: 8; accent: false; onClicked: { backendObject.adaptiveResponseSimulatorClear(); root.simulatorPaused = true; root.simulatorRecording = false; root.simulatorReplaying = false; root.simulatorLastSequence = 0; root.simulatorSamples = []; root.simulatorRecordingSamples = []; root.simulatorDisplaySamples = [] } }
                        Item { Layout.fillWidth: true }
                        Caption { text: "SYNTHETIC SOURCE RATE" }
                        ResponseCombo { objectName: "adaptiveSourceRateSelector"; Layout.preferredWidth: 118; model: [{label:"250 Hz",rate:250},{label:"125 Hz",rate:125},{label:"60 Hz",rate:60},{label:"30 Hz",rate:30}]; textRole: "label"; valueRole: "rate"; currentIndex: root.simulatorSourceRate === 250 ? 0 : root.simulatorSourceRate === 125 ? 1 : root.simulatorSourceRate === 60 ? 2 : 3; onChoiceActivated: function(index, value) { root.simulatorSourceRate = Number(value) } }
                    }
                    GridLayout { width: parent.width; columns: 2; columnSpacing: 14; rowSpacing: 0
                        ColumnLayout { Layout.row: 0; Layout.column: 1; Layout.preferredWidth: 112; Layout.alignment: Qt.AlignTop | Qt.AlignHCenter; spacing: 4
                            Caption { text: "MANUAL INPUT"; Layout.alignment: Qt.AlignHCenter }
                            Text { text: "+100"; color: root.themeTokens.textMuted; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                            VerticalSimulatorSlider { objectName: "adaptiveSimulatorManualInput"; Layout.alignment: Qt.AlignHCenter; value: root.simulatorInput; onMoved: { root.simulatorInput = value; if (!root.simulatorPaused && !root.simulatorReplaying) root.sampleSimulator() } }
                            Text { text: "0"; color: root.themeTokens.textMuted; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                            Text { text: "−100"; color: root.themeTokens.textMuted; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                            Text { text: root.percent(root.simulatorInput); color: root.themeTokens.orange; font.pixelSize: 13; font.bold: true; font.family: root.themeTokens.telemetryFont; Layout.alignment: Qt.AlignHCenter }
                        }
                        ColumnLayout { Layout.row: 0; Layout.column: 0; Layout.fillWidth: true; spacing: 7
                            RowLayout { Layout.fillWidth: true
                                Text { text: "SIMULATED SIGNAL PATH"; color: root.themeTokens.text; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true }
                                Caption { text: "OLDER  ←                 →  NEWEST" }
                            }
                            SimulatorGraph { Layout.fillWidth: true; Layout.preferredHeight: 148; samples: root.simulatorDisplaySamples; lowerBound: -1; upperBound: 1
                                series: [{field:"physical", color:root.themeTokens.textMuted}, {field:"estimated", color:root.themeTokens.cyan}, {field:"predicted", color:root.themeTokens.orange}, {field:"virtualOutput", color:root.themeTokens.ready}] }
                            Row { spacing: 16
                                Text { text: "— Physical"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                                Text { text: "— Estimated"; color: root.themeTokens.cyan; font.pixelSize: 10 }
                                Text { text: "— Predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                                Text { text: "— Final output"; color: root.themeTokens.ready; font.pixelSize: 10 }
                            }
                            Text { text: "VELOCITY  ·  axis units / second  ·  newest at right"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                            SimulatorGraph { Layout.fillWidth: true; Layout.preferredHeight: 82; samples: root.simulatorDisplaySamples; lowerBound: -root.simulatorMagnitude(["velocity"], 0.1); upperBound: root.simulatorMagnitude(["velocity"], 0.1); series: [{field:"velocity",color:root.themeTokens.cyan}] }
                            Text { text: "ACCELERATION  ·  axis units / second²  ·  newest at right"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                            SimulatorGraph { Layout.fillWidth: true; Layout.preferredHeight: 82; samples: root.simulatorDisplaySamples; lowerBound: -root.simulatorMagnitude(["acceleration"], 0.1); upperBound: root.simulatorMagnitude(["acceleration"], 0.1); series: [{field:"acceleration",color:root.themeTokens.orange}] }
                            Text { text: "ADAPTIVE ACTIVITY  ·  horizon / confidence  ·  percent of configured range"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                            SimulatorGraph { Layout.fillWidth: true; Layout.preferredHeight: 82; samples: root.simulatorDisplaySamples; lowerBound: 0; upperBound: 1; series: [{field:"horizonRatio",color:root.themeTokens.orange},{field:"confidence",color:root.themeTokens.ready}] }
                            Text { text: "PREDICTION LEAD  ·  ±" + root.percent(root.numericOr(root.simulatorCurrentSample().maximumLead, 0)).replace("+", "") + " configured limit  ·  axis units"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                            SimulatorGraph { Layout.fillWidth: true; Layout.preferredHeight: 82; samples: root.simulatorDisplaySamples; lowerBound: -Math.max(0.005, root.numericOr(root.simulatorCurrentSample().maximumLead, 0.01)); upperBound: Math.max(0.005, root.numericOr(root.simulatorCurrentSample().maximumLead, 0.01)); series: [{field:"lead",color:root.themeTokens.cyan}] }
                            TimeAxis { seconds: 5 }
                        }
                    }
                    RowLayout { width: parent.width; visible: root.simulatorRecordingSamples.length > 0 || root.simulatorReplaying
                        Caption { text: "SLOW-MOTION PLAYBACK" }
                        Text { text: "Replay speed"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Repeater { model: [1, 2, 4, 6, 8, 10]
                            delegate: ActionButton { required property var modelData; text: modelData + "×"; implicitHeight: 27; padding: 7; accent: root.replaySlowdown === modelData; onClicked: root.replaySlowdown = modelData }
                        }
                        Item { Layout.fillWidth: true }
                        Caption { text: root.replaySlowdown === 1 ? "1× original presentation" : root.replaySlowdown + "× slower presentation · original timestamps and results" }
                    }
                    Flow { width: parent.width; spacing: 16
                        Metric { caption: "PHYSICAL"; value: root.percent(root.numericOr(root.simulatorCurrentSample().physical, 0)) }
                        Metric { caption: "ESTIMATED"; value: root.percent(root.numericOr(root.simulatorCurrentSample().estimated, 0)) }
                        Metric { caption: "PREDICTED"; value: root.percent(root.numericOr(root.simulatorCurrentSample().predicted, 0)); tone: root.themeTokens.orange }
                        Metric { caption: "FINAL OUTPUT"; value: root.percent(root.numericOr(root.simulatorCurrentSample().virtualOutput, 0)); tone: root.themeTokens.ready }
                        Metric { caption: "VELOCITY"; value: root.numericOr(root.simulatorCurrentSample().velocity, 0).toFixed(2) + " /s" }
                        Metric { caption: "ACCELERATION"; value: root.numericOr(root.simulatorCurrentSample().acceleration, 0).toFixed(1) + " /s²" }
                        Metric { caption: "ACTIVE HORIZON"; value: root.numericOr(root.simulatorCurrentSample().activeHorizonMs, 0).toFixed(2) + " ms" }
                        Metric { caption: "LEAD"; value: root.percent(root.numericOr(root.simulatorCurrentSample().lead, 0)) }
                        Metric { caption: "CONFIDENCE"; value: Math.round(root.numericOr(root.simulatorCurrentSample().confidence, 0) * 100) + "%" }
                        Metric { caption: "STATE"; value: root.simulatorCurrentSample().state || "Stable" }
                    }
                    Text { text: "Move the slider like a virtual HOTAS. The simulator reconstructs your continuous gesture, samples it at the selected controller rate, then runs the real predictor. New samples appear at the right; older motion scrolls left."; color: root.themeTokens.textMuted; font.pixelSize: 11; width: parent.width; wrapMode: Text.WordWrap }
                    Text { text: "Physical = manual slider · Estimated = internal motion estimate · Predicted = expected near-future stick position · Final Output = prediction after the active axis curve and transforms. Synthetic Source Rate emulates how often a physical controller reports a new axis sample."; color: root.themeTokens.textMuted; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap }
                    Text { text: root.simulatorSamples.length === 0 || root.simulatorPaused ? "PAUSED — press LIVE or RECORD and move Manual Input." : "RECORD stores original timestamps and output samples. REPLAY redraws those stored samples only; it never recomputes predictor output."; color: root.themeTokens.textMuted; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 12
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Simple controls"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Choose a response level for " + root.state.axisLabel + ". The configured horizon is a maximum; response remains adaptive at every level."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { text: scopeInfo().source || "Inherited"; color: root.themeTokens.orange; font.pixelSize: 11; font.bold: true }
                    }
                    Flow { width: parent.width; spacing: 8
                        Repeater { model: backendObject.adaptiveResponsePresets
                            delegate: ActionButton { required property var modelData; objectName: "adaptivePresetButton_" + modelData.id; text: modelData.name.toUpperCase(); accent: (scopeInfo().presetId === modelData.id); ToolTip.visible: hovered; ToolTip.text: modelData.description; enabled: root.editScope !== "preset"; onClicked: root.applySimplePreset(modelData.id) }
                        }
                    }
                    Row { spacing: 28
                        Metric { caption: "MAX HORIZON"; value: root.numericOr(effective().maximumHorizonMs, 0).toFixed(1) + " ms"; tone: root.themeTokens.orange }
                        Metric { caption: "MAX LEAD"; value: percent(root.numericOr(effective().maximumLead, 0)) }
                        Metric { caption: "PREDICTOR"; value: effective().enabled ? String(effective().model || "auto").toUpperCase() : "INACTIVE" }
                        Metric { caption: "REVERSAL"; value: Math.round(root.numericOr(effective().reversalResponse, 0) * 100) + "%" }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 10
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Static response preview"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Repeatable synthetic input uses the same lightweight estimator as runtime. This chart never touches the mapper hot path."; color: root.themeTokens.textMuted; font.pixelSize: 11 }
                        }
                        ResponseCombo { id: scenarioSelector; objectName: "adaptiveScenarioSelector"; Layout.preferredWidth: 206; model: ["Slow Sweep", "Fast Sweep", "Instant Reversal Torture", "Human-Like Rapid Reversal", "Positive-Side Reversal", "Negative-Side Reversal", "Center-Crossing Reversal", "Micro Adjustments", "Sudden Stop", "Center Fighting"]; currentIndex: Math.max(0, model.indexOf(root.scenario)); onChoiceActivated: function(index, value) { root.scenario = String(value); root.setPreview() } }
                    }
                    RowLayout { width: parent.width; spacing: 8
                        Caption { text: "VIEW" }
                        ActionButton { text: "PREDICTOR"; implicitHeight: 28; padding: 8; accent: root.staticPreviewView === "predictor"; onClicked: root.setStaticPreviewView("predictor") }
                        ActionButton { text: "FINAL PIPELINE"; implicitHeight: 28; padding: 8; accent: root.staticPreviewView === "pipeline"; onClicked: root.setStaticPreviewView("pipeline") }
                        Item { Layout.fillWidth: true }
                        Caption { text: root.staticPreviewView === "predictor" ? "Estimator view · no curve/output-stage dominance" : "Includes the selected axis's active response curve and mapping transformations." }
                    }
                    RowLayout { width: parent.width; spacing: 6
                        Caption { text: "TRACES" }
                        ActionButton { text: "PHYSICAL"; implicitHeight: 26; padding: 7; accent: root.showPhysicalTrace; onClicked: root.showPhysicalTrace = !root.showPhysicalTrace }
                        ActionButton { text: "ESTIMATED"; implicitHeight: 26; padding: 7; accent: root.showEstimatedTrace; onClicked: root.showEstimatedTrace = !root.showEstimatedTrace }
                        ActionButton { text: "PREDICTED"; implicitHeight: 26; padding: 7; accent: root.showPredictedTrace; onClicked: root.showPredictedTrace = !root.showPredictedTrace }
                        ActionButton { text: "FINAL OUTPUT"; implicitHeight: 26; padding: 7; accent: root.showFinalTrace; onClicked: root.showFinalTrace = !root.showFinalTrace }
                        Item { Layout.fillWidth: true }
                        Caption { text: root.staticScenarioDurationMs().toFixed(0) + " ms synthetic scenario" }
                    }
                    Canvas { id: graph; width: parent.width; height: 220
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset(); ctx.fillStyle = root.themeTokens.panelInset; ctx.fillRect(0, 0, width, height);
                            ctx.strokeStyle = root.themeTokens.divider; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke();
                            function trace(field, color, weight, dashed) {
                                if (!root.previewSamples || root.previewSamples.length === 0) return;
                                // Rendering-only piecewise-linear resampling. It never
                                // enters AdaptiveResponseProcessor or Test Lab metrics;
                                // original samples remain the endpoints of each segment.
                                const sampleCount = root.previewSamples.length
                                const renderCount = Math.max(sampleCount, Math.floor(width))
                                ctx.strokeStyle = color; ctx.lineWidth = weight; ctx.setLineDash(dashed ? [5, 4] : []); ctx.beginPath();
                                for (let renderIndex = 0; renderIndex < renderCount; ++renderIndex) {
                                    const samplePosition = renderIndex * (sampleCount - 1) / Math.max(1, renderCount - 1)
                                    const leftIndex = Math.floor(samplePosition)
                                    const rightIndex = Math.min(sampleCount - 1, leftIndex + 1)
                                    const fraction = samplePosition - leftIndex
                                    const left = root.numericOr(root.previewSamples[leftIndex][field], 0)
                                    const right = root.numericOr(root.previewSamples[rightIndex][field], 0)
                                    const value = Math.max(-1, Math.min(1, left + (right - left) * fraction))
                                    const x = renderIndex * width / Math.max(1, renderCount - 1)
                                    const y = height * (1 - (value + 1) * 0.5)
                                    if (renderIndex === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.stroke(); ctx.setLineDash([])
                            }
                            if (root.showPhysicalTrace) trace("physical", root.themeTokens.textStrong, 2.5, false)
                            if (root.showEstimatedTrace) trace("estimated", root.themeTokens.cyan, 1.25, true)
                            if (root.showPredictedTrace) trace("predicted", root.themeTokens.orange, 3, false)
                            if (root.showFinalTrace) trace("virtualOutput", root.themeTokens.ready, 2.25, false)
                        }
                        Connections { target: root; function onPreviewSamplesChanged() { graph.requestPaint() } function onStaticPreviewViewChanged() { graph.requestPaint() } function onShowPhysicalTraceChanged() { graph.requestPaint() } function onShowEstimatedTraceChanged() { graph.requestPaint() } function onShowPredictedTraceChanged() { graph.requestPaint() } function onShowFinalTraceChanged() { graph.requestPaint() } }
                    }
                    Row { spacing: 16
                        Text { visible: root.showPhysicalTrace; text: "— Physical"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Text { visible: root.showEstimatedTrace; text: "— Estimated"; color: root.themeTokens.cyan; font.pixelSize: 10 }
                        Text { visible: root.showPredictedTrace; text: "— Predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                        Text { visible: root.showFinalTrace; text: "— Final output"; color: root.themeTokens.ready; font.pixelSize: 10 }
                    }
                    RowLayout { width: parent.width
                        Repeater { model: root.staticTimeTickLabels()
                            delegate: Caption { required property int index; required property var modelData; text: modelData; Layout.fillWidth: true; horizontalAlignment: index === 0 ? Text.AlignLeft : index === 5 ? Text.AlignRight : Text.AlignHCenter }
                        }
                    }
                    Text { visible: root.scenario === "Instant Reversal Torture"; text: "INSTANTANEOUS REVERSAL — worst-case synthetic torture test; not representative of physically smooth human HOTAS movement."; color: root.themeTokens.warning || root.themeTokens.orange; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap }
                    Rectangle { width: parent.width; height: 1; color: root.themeTokens.divider }
                    Column { id: staticLeadDetail; width: parent.width; spacing: 5
                        property real configuredMaximum: Math.max(0.001, root.numericOr(root.effective().maximumLead, 0.01))
                        property real currentLead: root.previewSamples && root.previewSamples.length > 0 ? root.numericOr(root.previewSamples[root.previewSamples.length - 1].lead, 0) : 0
                        property real peakLead: {
                            let peak = 0
                            for (let index = 0; index < root.previewSamples.length; ++index) peak = Math.max(peak, Math.abs(root.numericOr(root.previewSamples[index].lead, 0)))
                            return peak
                        }
                        RowLayout { width: parent.width
                            ColumnLayout { Layout.fillWidth: true
                                Text { text: "MAGNIFIED PREDICTION LEAD"; color: root.themeTokens.text; font.pixelSize: 11; font.bold: true }
                                Text { text: "Scale is the configured maximum lead: ±" + root.percent(staticLeadDetail.configuredMaximum); color: root.themeTokens.textMuted; font.pixelSize: 10 }
                            }
                            Caption { text: "CURRENT " + root.percent(staticLeadDetail.currentLead) + "  ·  PEAK " + root.percent(staticLeadDetail.peakLead) + "  ·  ACTIVE HORIZON " + (root.previewSamples.length > 0 ? root.numericOr(root.previewSamples[root.previewSamples.length - 1].horizonMs, 0).toFixed(1) : "0.0") + " ms" }
                        }
                        Canvas { id: previewLeadGraph; width: parent.width; height: 78
                            onPaint: {
                                const ctx = getContext("2d"); ctx.reset(); ctx.fillStyle = root.themeTokens.panelInset; ctx.fillRect(0, 0, width, height)
                                ctx.strokeStyle = root.themeTokens.divider; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                                if (!root.previewSamples || root.previewSamples.length === 0) return
                                const maxLead = Math.max(0.001, parent.configuredMaximum)
                                const sampleCount = root.previewSamples.length
                                const renderCount = Math.max(sampleCount, Math.floor(width))
                                ctx.strokeStyle = root.themeTokens.cyan; ctx.lineWidth = 2; ctx.beginPath()
                                for (let renderIndex = 0; renderIndex < renderCount; ++renderIndex) { const samplePosition = renderIndex * (sampleCount - 1) / Math.max(1, renderCount - 1); const leftIndex = Math.floor(samplePosition); const rightIndex = Math.min(sampleCount - 1, leftIndex + 1); const fraction = samplePosition - leftIndex; const left = root.numericOr(root.previewSamples[leftIndex].lead, 0); const right = root.numericOr(root.previewSamples[rightIndex].lead, 0); const value = Math.max(-maxLead, Math.min(maxLead, left + (right - left) * fraction)); const x = renderIndex * width / Math.max(1, renderCount - 1); const y = height * (0.5 - value / (2 * maxLead)); if (renderIndex === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y) }
                                ctx.stroke()
                            }
                            Connections { target: root; function onPreviewSamplesChanged() { previewLeadGraph.requestPaint() } }
                        }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 9
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "A / B comparison"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "A is the context currently being edited. Compare its prediction trace against a saved Response Preset or another profile state before applying any change."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Caption { text: "B" }
                        ResponseCombo { objectName: "adaptiveComparisonSelector"; Layout.preferredWidth: 260; model: root.comparisonChoices(); textRole: "label"; currentIndex: root.comparisonIndex(); onChoiceActivated: function(index) { const choice = root.comparisonChoices()[index]; root.comparisonScope = choice.scope; root.comparisonTargetId = choice.id; root.setPreview() } }
                    }
                    Canvas { id: comparisonGraph; width: parent.width; height: 145
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset(); ctx.fillStyle = root.themeTokens.panelInset; ctx.fillRect(0, 0, width, height)
                            ctx.strokeStyle = root.themeTokens.divider; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                            function trace(samples, field, color, dashed) {
                                if (!samples || samples.length === 0) return
                                ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.setLineDash(dashed ? [5, 4] : []); ctx.beginPath()
                                for (let i = 0; i < samples.length; ++i) { const value = Math.max(-1, Math.min(1, Number(samples[i][field]))); const x = i * width / Math.max(1, samples.length - 1); const y = height * (1 - (value + 1) * 0.5); if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y) }
                                ctx.stroke(); ctx.setLineDash([])
                            }
                            trace(root.previewSamples, "physical", root.themeTokens.textMuted, false)
                            trace(root.previewSamples, "predicted", root.themeTokens.orange, false)
                            trace(root.comparisonSamples, "predicted", root.themeTokens.ready, true)
                        }
                        Connections { target: root; function onPreviewSamplesChanged() { comparisonGraph.requestPaint() } function onComparisonSamplesChanged() { comparisonGraph.requestPaint() } }
                    }
                    Row { spacing: 16
                        Text { text: "— Physical baseline"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Text { text: "— A predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                        Text { text: "– – B predicted"; color: root.themeTokens.ready; font.pixelSize: 10 }
                    }
                    Flow { width: parent.width; spacing: 16
                        Metric { caption: "A PEAK LEAD"; value: root.percent(root.numericOr(root.testLabMetrics.peakLead, 0)); tone: root.themeTokens.orange }
                        Metric { caption: "A MEDIAN LEAD"; value: root.percent(root.numericOr(root.testLabMetrics.medianLead, 0)); tone: root.themeTokens.orange }
                        Metric { caption: "B PEAK LEAD"; value: root.percent(root.numericOr(root.comparisonTestLabMetrics.peakLead, 0)); tone: root.themeTokens.ready }
                        Metric { caption: "B MEDIAN LEAD"; value: root.percent(root.numericOr(root.comparisonTestLabMetrics.medianLead, 0)); tone: root.themeTokens.ready }
                        Metric { caption: "PEAK DELTA A−B"; value: root.percent(root.numericOr(root.testLabMetrics.peakLead, 0) - root.numericOr(root.comparisonTestLabMetrics.peakLead, 0)) }
                        Metric { caption: "MEDIAN DELTA A−B"; value: root.percent(root.numericOr(root.testLabMetrics.medianLead, 0) - root.numericOr(root.comparisonTestLabMetrics.medianLead, 0)) }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 8
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Advanced tuning"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Property overrides stay at the selected level; clear an override to inherit it again."; color: root.themeTokens.textMuted; font.pixelSize: 11 }
                        }
                        ActionButton { text: root.advancedExpanded ? "HIDE" : "SHOW"; accent: false; onClicked: root.advancedExpanded = !root.advancedExpanded }
                        ActionButton { text: "RESET LAYER"; accent: false; onClicked: backendObject.resetAdaptiveResponseAxisAtContext(root.editScope, root.selectedTargetId(), root.state.axis) }
                    }
                    Column { visible: root.advancedExpanded; width: parent.width; spacing: 10
                        TuneGroup { title: "MODE AND OWNERSHIP"; detail: "Enablement and estimator model are independent overrides. Each row states whether this editing context owns the value or inherits it."
                            RowLayout { width: parent.width
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Text { text: "Predictor enabled"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                                    Caption { text: root.editScope === "global" ? "APPLICATION DEFAULT" : root.inheritedHere("enabled") ? "INHERITED FROM PARENT" : "OVERRIDE AT THIS LEVEL" }
                                }
                                ThemedSwitch { checked: !!effective().enabled; onToggled: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "enabled", checked); root.setPreview() } }
                                ActionButton { text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere("enabled") ? "INHERITED" : "INHERIT"; accent: false; enabled: root.editScope === "global" || !root.inheritedHere("enabled"); implicitHeight: 28; padding: 8; onClicked: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "enabled", false, true); root.setPreview() } }
                            }
                            RowLayout { width: parent.width
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Text { text: "Prediction model"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                                    Caption { text: root.editScope === "global" ? "APPLICATION DEFAULT" : root.inheritedHere("model") ? "INHERITED FROM PARENT" : "OVERRIDE AT THIS LEVEL" }
                                }
                                ResponseCombo { objectName: "adaptivePredictorSelector"; Layout.preferredWidth: 210; model: ["auto", "velocity", "alpha-beta", "alpha-beta-gamma"]; currentIndex: Math.max(0, model.indexOf(effective().model || "auto")); onChoiceActivated: function(index) { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "model", ["auto", "velocity", "alpha-beta", "alpha-beta-gamma"][index]); root.setPreview() } }
                                ActionButton { text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere("model") ? "INHERITED" : "INHERIT"; accent: false; enabled: root.editScope === "global" || !root.inheritedHere("model"); implicitHeight: 28; padding: 8; onClicked: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "model", "auto", true); root.setPreview() } }
                            }
                        }
                        TuneGroup { title: "PREDICTION ENVELOPE"; detail: "Safety limits in time and normalized-axis headroom."
                            TuneRow { label: "Maximum horizon"; detail: "Adaptive ceiling; active prediction can remain below it."; from: 0; to: 30; step: 0.5; unit: "ms"; propertyKey: "maximumHorizonMs"; value: root.numericOr(effective().maximumHorizonMs, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "maximumHorizonMs", value); root.setPreview() } }
                            TuneRow { label: "Maximum lead"; detail: "Hard safety envelope in normalized axis travel."; from: 0.01; to: 0.50; step: 0.01; unit: "%"; propertyKey: "maximumLead"; value: root.numericOr(effective().maximumLead, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "maximumLead", value); root.setPreview() } }
                            TuneRow { label: "Endpoint taper"; detail: "Tapers lead into the remaining output headroom."; from: 0.01; to: 1; step: 0.01; unit: "%"; propertyKey: "endpointTaper"; value: root.numericOr(effective().endpointTaper, 0.16); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "endpointTaper", value); root.setPreview() } }
                        }
                        TuneGroup { title: "MOTION ESTIMATION"; detail: "Evidence thresholds and model gains; these rows are control-plane settings, not a live report loop."
                            TuneRow { label: "Velocity response"; detail: "Derivative responsiveness during deliberate movement."; propertyKey: "velocityResponse"; value: root.numericOr(effective().velocityResponse, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "velocityResponse", value); root.setPreview() } }
                            TuneRow { label: "Acceleration response"; detail: "Alpha-beta-gamma acceleration contribution."; propertyKey: "accelerationResponse"; value: root.numericOr(effective().accelerationResponse, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "accelerationResponse", value); root.setPreview() } }
                            TuneRow { label: "Motion sensitivity"; detail: "Minimum deliberate motion that activates prediction."; from: 0.001; to: 2; step: 0.005; unit: "axis/s"; propertyKey: "motionSensitivity"; value: root.numericOr(effective().motionSensitivity, 0.035); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "motionSensitivity", value); root.setPreview() } }
                            TuneRow { label: "Noise rejection"; detail: "Ignore tiny sensor movement while estimating motion."; from: 0; to: 0.50; step: 0.001; unit: "axis"; propertyKey: "noiseRejection"; value: root.numericOr(effective().noiseRejection, 0.012); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "noiseRejection", value); root.setPreview() } }
                        }
                        TuneGroup { title: "REVERSAL AND SETTLING"; detail: "Safety cancellation is always active; these parameters govern the evidence and recovery behaviour after that cancellation."
                            TuneRow { label: "Reversal detection"; detail: "Motion threshold that clears stale directional lead."; from: 0.001; to: 10; step: 0.01; unit: "axis/s"; propertyKey: "reversalDetection"; value: root.numericOr(effective().reversalDetection, 0.075); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "reversalDetection", value); root.setPreview() } }
                            TuneRow { label: "Reversal response"; detail: "Controls new-direction lead reacquisition after safety cancellation."; propertyKey: "reversalResponse"; value: root.numericOr(effective().reversalResponse, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "reversalResponse", value); root.setPreview() } }
                            TuneRow { label: "Deceleration response"; detail: "Reduces lead while braking."; propertyKey: "decelerationResponse"; value: root.numericOr(effective().decelerationResponse, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "decelerationResponse", value); root.setPreview() } }
                            TuneRow { label: "Settling response"; detail: "Collapses horizon and damps state as motion comes to rest."; propertyKey: "settlingResponse"; value: root.numericOr(effective().settlingResponse, 0); onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), root.state.axis, "settlingResponse", value); root.setPreview() } }
                        }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 12
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Live telemetry"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Captured from the atomic latest-state snapshot at 83 Hz; graphs render independently at about 30 Hz. Mapping reports never drive the UI directly."; color: root.themeTokens.textMuted; font.pixelSize: 11 }
                        }
                        Text { text: telemetry.enabled ? (telemetry.state || "Stable") : "OFF · PREDICTOR INACTIVE"; color: telemetry.enabled ? root.themeTokens.orange : root.themeTokens.textMuted; font.pixelSize: 16; font.bold: true }
                    }
                    Flow { width: parent.width; spacing: 16
                        Metric { caption: "PHYSICAL"; value: percent(root.numericOr(telemetry.physical, 0)) }
                        Metric { caption: "ESTIMATED"; value: percent(root.numericOr(telemetry.estimated, 0)) }
                        Metric { caption: "PREDICTED"; value: percent(root.numericOr(telemetry.predicted, 0)); tone: root.themeTokens.orange }
                        Metric { caption: "VIRTUAL OUTPUT"; value: percent(root.numericOr(telemetry.virtualOutput, 0)); tone: root.themeTokens.ready }
                        Metric { caption: "VELOCITY"; value: root.numericOr(telemetry.velocity, 0).toFixed(2) }
                        Metric { caption: "ACCELERATION"; value: root.numericOr(telemetry.acceleration, 0).toFixed(1) }
                        Metric { caption: "MOTION"; value: Math.round(root.numericOr(telemetry.motionIntensity, 0) * 100) + "%" }
                        Metric { caption: "ACTIVE PREDICTION"; value: (telemetry.enabled ? root.numericOr(telemetry.activeHorizonMs, 0) : 0).toFixed(2) + " ms" }
                        Metric { caption: telemetry.enabled ? "MAX HORIZON" : "DORMANT HORIZON"; value: root.numericOr(telemetry.maximumHorizonMs, 0).toFixed(1) + " ms" }
                        Metric { caption: "LEAD"; value: percent(telemetry.enabled ? root.numericOr(telemetry.lead, 0) : 0) }
                        Metric { caption: telemetry.enabled ? "MAX LEAD" : "DORMANT LEAD"; value: percent(root.numericOr(telemetry.maximumLead, 0)) }
                        Metric { caption: "CONFIDENCE"; value: Math.round(root.numericOr(telemetry.confidence, 0) * 100) + "%" }
                        Metric { caption: "REVERSALS"; value: root.numericOr(telemetry.reversalCount, 0) }
                        Metric { caption: "SAFETY CLAMPS"; value: root.numericOr(telemetry.safetyClampCount, 0) }
                    }
                    Flow { width: parent.width; spacing: 14
                        Gauge { caption: "ACTIVE PREDICTION"; value: telemetry.enabled ? root.numericOr(telemetry.activeHorizonMs, 0) : 0; maximum: Math.max(0.1, root.numericOr(telemetry.maximumHorizonMs, 0)); tone: root.themeTokens.orange }
                        Gauge { caption: "PREDICTION LEAD"; value: telemetry.enabled ? Math.abs(root.numericOr(telemetry.lead, 0)) : 0; maximum: Math.max(0.001, root.numericOr(telemetry.maximumLead, 0)); tone: root.themeTokens.ready }
                        Gauge { caption: "CONFIDENCE"; value: root.numericOr(telemetry.confidence, 0); maximum: 1; tone: root.themeTokens.textStrong }
                        Gauge { caption: "MOTION INTENSITY"; value: root.numericOr(telemetry.motionIntensity, 0); maximum: 1; tone: root.themeTokens.orange }
                    }
                }
            }

            Card {
                id: liveAnalysisCard
                Column { width: parent.width; spacing: 9
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Live analysis"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "A fixed UI-side ring captures the atomic latest state at 83 Hz while this section renders near 30 Hz. Position, motion, and Adaptive Response each retain their own readable scale."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { text: root.liveAnalysisExpanded ? "COLLAPSE" : "OPEN"; accent: false; onClicked: root.liveAnalysisExpanded = !root.liveAnalysisExpanded }
                    }
                    Column { visible: root.liveAnalysisExpanded; width: parent.width; spacing: 9
                        Repeater { model: [2, 5, 10, 30]
                            delegate: ActionButton { required property var modelData; text: modelData + "s"; accent: root.historyWindowSeconds === modelData; implicitHeight: 28; padding: 8; onClicked: { root.historyWindowSeconds = modelData; root.refreshHistory() } }
                        }
                        ActionButton { text: root.historyPaused ? "RESUME" : "PAUSE"; accent: false; implicitHeight: 28; padding: 8; onClicked: { root.historyPaused = !root.historyPaused; if (!root.historyPaused) root.refreshHistory() } }
                    RowLayout { width: parent.width
                        Caption { text: (root.historyPaused ? "PAUSED INSPECTION" : "LIVE") + " · " + (runtimeState.axisLabel || root.state.axisLabel || "Axis").toUpperCase() }
                        Item { Layout.fillWidth: true }
                        Caption { text: "CHRONOLOGICAL · NEWEST AT RIGHT" }
                    }
                    Text { text: "AXIS POSITION  ·  normalized axis units"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                    HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 156; lowerBound: -1; upperBound: 1
                        series: [{field:"physical", color:root.themeTokens.textMuted}, {field:"estimated", color:root.themeTokens.cyan}, {field:"predicted", color:root.themeTokens.orange}, {field:"virtualOutput", color:root.themeTokens.ready}] }
                    TimeAxis { seconds: root.historyWindowSeconds }
                    Row { spacing: 16
                        Text { text: "— Physical"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Text { text: "— Estimated"; color: root.themeTokens.cyan; font.pixelSize: 10 }
                        Text { text: "— Predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                        Text { text: "— Virtual output"; color: root.themeTokens.ready; font.pixelSize: 10 }
                    }
                    Text { text: "MOTION  ·  independent velocity and acceleration scales"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                    RowLayout { width: parent.width; spacing: 10
                        ColumnLayout { Layout.fillWidth: true
                            Caption { text: "VELOCITY / s" }
                            HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 112; lowerBound: -root.historyMagnitude(["velocity"], 0.1); upperBound: root.historyMagnitude(["velocity"], 0.1); series: [{field:"velocity", color:root.themeTokens.cyan}] }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Caption { text: "ACCELERATION / s²" }
                            HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 112; lowerBound: -root.historyMagnitude(["acceleration"], 0.1); upperBound: root.historyMagnitude(["acceleration"], 0.1); series: [{field:"acceleration", color:root.themeTokens.orange}] }
                        }
                    }
                    Text { text: "ADAPTIVE RESPONSE  ·  horizon/confidence and lead retain independent scales"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                    RowLayout { width: parent.width; spacing: 10
                        ColumnLayout { Layout.fillWidth: true
                            Caption { text: "HORIZON / CONFIDENCE · % OF ACTIVE LIMIT" }
                            HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 112; lowerBound: 0; upperBound: 1
                                series: [{field:"horizonRatio", color:root.themeTokens.orange}, {field:"confidence", color:root.themeTokens.ready}] }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Caption { text: "PREDICTION LEAD · AXIS UNITS" }
                            HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 112; lowerBound: -root.historyMagnitude(["lead"], 0.01); upperBound: root.historyMagnitude(["lead"], 0.01); series: [{field:"lead", color:root.themeTokens.cyan}] }
                        }
                    }
                    Column { visible: root.historyPaused && root.historySamples.length > 0; width: parent.width; spacing: 5
                        ThemedSlider { width: parent.width; from: 0; to: Math.max(0, root.historySamples.length - 1); stepSize: 1; value: Math.max(0, root.historyInspectIndex); onMoved: root.historyInspectIndex = Math.round(value) }
                        Flow { width: parent.width; spacing: 14
                            Metric { caption: "INSPECT TIME"; value: root.numericOr(root.inspectedHistorySample().timeMs, 0).toFixed(0) + " ms" }
                            Metric { caption: "PHYSICAL / ESTIMATE"; value: root.percent(root.numericOr(root.inspectedHistorySample().physical, 0)) + " / " + root.percent(root.numericOr(root.inspectedHistorySample().estimated, 0)) }
                            Metric { caption: "PREDICTED / OUTPUT"; value: root.percent(root.numericOr(root.inspectedHistorySample().predicted, 0)) + " / " + root.percent(root.numericOr(root.inspectedHistorySample().virtualOutput, 0)) }
                            Metric { caption: "VELOCITY / ACCEL"; value: root.numericOr(root.inspectedHistorySample().velocity, 0).toFixed(2) + " / " + root.numericOr(root.inspectedHistorySample().acceleration, 0).toFixed(1) }
                            Metric { caption: "HORIZON / LEAD"; value: root.numericOr(root.inspectedHistorySample().activeHorizonMs, 0).toFixed(2) + " ms / " + root.percent(root.numericOr(root.inspectedHistorySample().lead, 0)) }
                            Metric { caption: "CONFIDENCE / STATE"; value: Math.round(root.numericOr(root.inspectedHistorySample().confidence, 0) * 100) + "% / " + (root.inspectedHistorySample().state || "Stable") }
                        }
                    }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 8
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Test Lab"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Compare an instantaneous reversal torture case with a rounded human-like reversal, then inspect prediction and final-output step metrics before launching a game."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { text: root.testLabExpanded ? "COLLAPSE" : "OPEN"; accent: false; onClicked: root.testLabExpanded = !root.testLabExpanded }
                    }
                    Column { visible: root.testLabExpanded; width: parent.width; spacing: 4
                        Text { text: root.scenario.toUpperCase() + " · " + root.numericOr(testLabMetrics.sampleCount, 0) + " estimator samples"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                        Flow { width: parent.width; spacing: 16
                            Metric { caption: "PEAK LEAD"; value: percent(root.numericOr(testLabMetrics.peakLead, 0)); tone: root.themeTokens.orange }
                            Metric { caption: "MEDIAN LEAD"; value: percent(root.numericOr(testLabMetrics.medianLead, 0)) }
                            Metric { caption: "MEAN ABS PREDICTION ERROR"; value: percent(root.numericOr(testLabMetrics.meanAbsolutePredictionError, 0)) }
                            Metric { caption: "RMS PREDICTION ERROR"; value: percent(root.numericOr(testLabMetrics.rmsPredictionError, 0)) }
                            Metric { caption: "P95 PREDICTION ERROR"; value: percent(root.numericOr(testLabMetrics.p95PredictionError, 0)) }
                            Metric { caption: "MAX PREDICTION ERROR"; value: percent(root.numericOr(testLabMetrics.maximumPredictionError, 0)) }
                            Metric { visible: !!testLabMetrics.hasStaticTarget; caption: "TARGET OVERSHOOT"; value: percent(root.numericOr(testLabMetrics.targetOvershoot, 0)) }
                            Metric { caption: "MOTION RECOGNITION"; value: root.numericOr(testLabMetrics.motionRecognitionDelayMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.motionRecognitionDelayMs, 0).toFixed(1) + " ms" }
                            Metric { caption: "TRUE REVERSAL DETECTIONS"; value: root.numericOr(testLabMetrics.trueReversalCount, 0) }
                            Metric { caption: "FALSE REVERSALS"; value: root.numericOr(testLabMetrics.falseReversalCount, 0) }
                            Metric { caption: "STATIONARY LEAD"; value: percent(root.numericOr(testLabMetrics.stationaryLead, 0)) }
                            Metric { caption: "PHYSICAL REVERSAL"; value: root.numericOr(testLabMetrics.physicalReversalMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.physicalReversalMs, 0).toFixed(1) + " ms" }
                            Metric { caption: "PREDICTOR DETECTED"; value: root.numericOr(testLabMetrics.predictorDetectedMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.predictorDetectedMs, 0).toFixed(1) + " ms" }
                            Metric { caption: "REVERSAL DETECTION LATENCY"; value: root.numericOr(testLabMetrics.reversalDetectionLatencyMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.reversalDetectionLatencyMs, 0).toFixed(1) + " ms" }
                            Metric { caption: "PRE-REVERSAL LEAD"; value: percent(root.numericOr(testLabMetrics.preReversalLead, 0)) }
                            Metric { caption: "POST-CANCEL LEAD"; value: percent(root.numericOr(testLabMetrics.postReversalLead, 0)); tone: root.themeTokens.ready }
                            Metric { caption: "LEAD COLLAPSE"; value: percent(root.numericOr(testLabMetrics.leadCollapseMagnitude, 0)); tone: root.themeTokens.ready }
                            Metric { caption: "STALE LEAD CLEARED"; value: root.numericOr(testLabMetrics.staleLeadCancellationMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.staleLeadCancellationMs, 0).toFixed(1) + " ms"; tone: root.themeTokens.ready }
                            Metric { caption: "OPPOSITE LEAD REACQUISITION"; value: root.numericOr(testLabMetrics.oppositeDirectionReacquisitionMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.oppositeDirectionReacquisitionMs, 0).toFixed(1) + " ms"; tone: root.themeTokens.ready }
                            Metric { caption: "SETTLING TIME"; value: root.numericOr(testLabMetrics.settlingTimeMs, -1) < 0 ? "—" : root.numericOr(testLabMetrics.settlingTimeMs, 0).toFixed(1) + " ms" }
                            Metric { caption: "MAX PHYSICAL STEP"; value: percent(root.numericOr(testLabMetrics.maximumPhysicalDelta, 0)) }
                            Metric { caption: "MAX PREDICTED STEP"; value: percent(root.numericOr(testLabMetrics.maximumPredictedDelta, 0)) }
                            Metric { caption: "PREDICTOR-ONLY STEP"; value: percent(root.numericOr(testLabMetrics.maximumArtificialPredictorStep, 0)) }
                            Metric { caption: "VIRTUAL OUTPUT STEP"; value: percent(root.numericOr(testLabMetrics.maximumVirtualOutputStep, 0)) }
                        }
                        Text { text: "Prediction error compares each prediction with ground-truth physical position at that sample’s active horizon. Reversal latency and opposite-lead reacquisition are measured from ground-truth physical reversal. Settling requires lead < 0.2%, horizon < 0.25 ms, predicted ≈ physical, and Stable state for " + root.numericOr(testLabMetrics.settlingPersistenceMs, 48).toFixed(0) + " ms."; color: root.themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                    }
                }
            }
        }
    }

    Dialog {
        id: savePresetDialog
        title: "Save current setup as preset"
        modal: true
        anchors.centerIn: parent
        width: 440
        standardButtons: Dialog.Cancel
        background: Rectangle { color: root.themeTokens.panel; border.color: root.themeTokens.borderStrong; radius: root.themeTokens.controlRadius }
        contentItem: ColumnLayout { spacing: 10
            Text { text: "Captures the current effective Adaptive Response configuration for all axes."; color: root.themeTokens.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField { id: presetName; Layout.fillWidth: true; placeholderText: "Preset name" }
            TextField { id: presetDescription; Layout.fillWidth: true; placeholderText: "Optional description" }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "SAVE PRESET"; onClicked: { if (backendObject.saveAdaptiveResponsePreset(presetName.text, presetDescription.text)) { presetName.text = ""; presetDescription.text = ""; savePresetDialog.close() } } }
            }
        }
    }

    Dialog {
        id: presetManageDialog
        title: "Response Presets"
        modal: true
        anchors.centerIn: parent
        width: Math.min(680, root.width - 36)
        standardButtons: Dialog.Close
        background: Rectangle { color: root.themeTokens.panel; border.color: root.themeTokens.borderStrong; radius: root.themeTokens.controlRadius }
        contentItem: ScrollView { clip: true; contentWidth: availableWidth; implicitHeight: Math.min(440, root.height - 140)
            ColumnLayout { width: presetManageDialog.width - 30; spacing: 8
                Text { text: "Custom presets can be edited directly, renamed, duplicated, exported with their required Pack dependencies, or deleted once all references are resolved."; color: root.themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Repeater { model: (backendObject.adaptiveResponsePresets || []).filter(function(preset) { return !preset.builtIn })
                    delegate: Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 74; radius: root.themeTokens.controlRadius; color: root.themeTokens.panelInset; border.color: root.themeTokens.border
                        ColumnLayout { anchors.fill: parent; anchors.margins: 9; spacing: 3
                            RowLayout { Layout.fillWidth: true
                                Text { text: modelData.name; color: root.themeTokens.textStrong; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                                ActionButton { text: "EDIT"; accent: false; onClicked: { root.editScope = "preset"; root.targetId = modelData.id; presetManageDialog.close() } }
                                ActionButton { text: "RENAME"; accent: false; onClicked: { root.renamePresetId = modelData.id; renamePresetName.text = modelData.name; renamePresetError.text = ""; renamePresetDialog.open() } }
                                ActionButton { text: "DUPLICATE"; accent: false; onClicked: backendObject.duplicateAdaptiveResponsePreset(modelData.id, modelData.name + " Copy") }
                                ActionButton { text: "DELETE"; accent: false; enabled: backendObject.adaptiveResponsePresetDependencies(modelData.id).length === 0; onClicked: backendObject.deleteAdaptiveResponsePreset(modelData.id) }
                            }
                            Text { text: modelData.description || "No description"; color: root.themeTokens.textMuted; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { visible: backendObject.adaptiveResponsePresetDependencies(modelData.id).length > 0; text: "IN USE: " + backendObject.adaptiveResponsePresetDependencies(modelData.id).join(" · "); color: root.themeTokens.warning || root.themeTokens.orange; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
                Text { visible: (backendObject.adaptiveResponsePresets || []).filter(function(preset) { return !preset.builtIn }).length === 0; text: "No custom Response Presets yet. Save a current setup to create one."; color: root.themeTokens.textMuted; font.pixelSize: 10 }
            }
        }
    }

    Dialog {
        id: renamePresetDialog
        title: "Rename Response Preset"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Cancel
        background: Rectangle { color: root.themeTokens.panel; border.color: root.themeTokens.borderStrong; radius: root.themeTokens.controlRadius }
        contentItem: ColumnLayout { spacing: 10
            Text { text: "Names must be unique and cannot reuse a built-in Response Preset name."; color: root.themeTokens.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField { id: renamePresetName; Layout.fillWidth: true; placeholderText: "Preset name" }
            Text { id: renamePresetError; visible: text.length > 0; text: ""; color: root.themeTokens.warning || root.themeTokens.orange; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "SAVE NAME"; enabled: renamePresetName.text.trim().length > 0; onClicked: { if (backendObject.renameAdaptiveResponsePreset(root.renamePresetId, renamePresetName.text)) { renamePresetDialog.close() } else { renamePresetError.text = "Use a unique name of 64 characters or fewer." } } }
            }
        }
    }
}
