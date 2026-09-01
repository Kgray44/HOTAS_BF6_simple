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
    property string scenario: "Rapid Reversal"
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
    property int historyWindowSeconds: 5
    property bool historyPaused: false
    property var historySamples: []
    property int historyInspectIndex: -1
    property string comparisonScope: "preset"
    property string comparisonTargetId: "off"
    property var comparisonSamples: backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)

    function effective() { return state.runtimeEffective || state.effective || ({}) }
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
    function setPreview() {
        previewSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
        testLabMetrics = backendObject.adaptiveResponseTestLabAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
        comparisonSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    }
    function refreshHistory() {
        if (!historyPaused) historySamples = backendObject.adaptiveResponseHistory(historyWindowSeconds)
    }
    function historyMagnitude(fields, minimum) {
        let maximum = minimum
        for (let i = 0; i < historySamples.length; ++i) {
            for (let j = 0; j < fields.length; ++j) maximum = Math.max(maximum, Math.abs(Number(historySamples[i][fields[j]] || 0)))
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
    onStateChanged: setPreview()
    onHistoryWindowSecondsChanged: refreshHistory()
    onHistorySamplesChanged: {
        if (!historyPaused) historyInspectIndex = historySamples.length - 1
        else historyInspectIndex = Math.max(0, Math.min(historyInspectIndex, historySamples.length - 1))
    }
    onHistoryPausedChanged: if (historyPaused) historyInspectIndex = historySamples.length - 1
    Connections {
        target: backendObject
        function onStateChanged() { root.contextEpoch += 1; root.runtimeEpoch += 1 }
        function onInputTelemetryChanged() { root.runtimeEpoch += 1 }
    }
    Timer { interval: 150; running: !root.historyPaused; repeat: true; triggeredOnStart: true; onTriggered: root.refreshHistory() }

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
        delegate: ItemDelegate {
            id: choice
            width: combo.width; implicitHeight: 32; highlighted: combo.highlightedIndex === index
            contentItem: Text {
                leftPadding: 10; rightPadding: 10; text: combo.textAt(index)
                color: choice.highlighted ? root.themeTokens.textStrong : root.themeTokens.text
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 10
            }
            background: Rectangle {
                radius: root.themeTokens.controlRadius
                color: choice.highlighted ? root.themeTokens.selection : (choice.hovered ? root.themeTokens.controlHover : "transparent")
                border.color: choice.highlighted ? root.themeTokens.orange : "transparent"
            }
        }
        popup: Popup {
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
    component TuneRow: Item {
        property string label: ""
        property string detail: ""
        property real value: 0
        property real from: 0
        property real to: 1
        property real step: 0.01
        property string propertyKey: ""
        signal changed(real value)
        implicitHeight: 52
        RowLayout { anchors.fill: parent; spacing: 12
            ColumnLayout { Layout.fillWidth: true; spacing: 2
                Text { text: parent.parent.label; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                Text { text: parent.parent.detail + (root.editScope === "global" ? " · Default" : root.inheritedHere(parent.parent.propertyKey) ? " · Inherited" : " · Override"); color: root.themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Slider { id: slider; Layout.preferredWidth: 220; from: parent.parent.from; to: parent.parent.to; stepSize: parent.parent.step; value: parent.parent.value; onMoved: parent.parent.changed(value) }
            Text { Layout.preferredWidth: 52; text: Number(parent.value).toFixed(parent.to <= 1 ? 2 : 1); color: root.themeTokens.textStrong; horizontalAlignment: Text.AlignRight; font.pixelSize: 11; font.family: root.themeTokens.telemetryFont }
            ActionButton {
                visible: parent.parent.propertyKey.length > 0
                text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere(parent.parent.propertyKey) ? "INHERITED" : "INHERIT"
                accent: false
                enabled: root.editScope === "global" || !root.inheritedHere(parent.parent.propertyKey)
                implicitHeight: 28
                padding: 8
                onClicked: {
                    backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, parent.parent.propertyKey, 0, true)
                    root.setPreview()
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 18
        clip: true
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
                        Metric { caption: "MODEL"; value: String((runtimeState.runtimeEffective && runtimeState.runtimeEffective.model) || "auto").toUpperCase(); tone: root.themeTokens.orange }
                        Metric { caption: "MAX HORIZON"; value: Number((runtimeState.runtimeEffective && runtimeState.runtimeEffective.maximumHorizonMs) || 0).toFixed(1) + " ms" }
                        Metric { caption: "MAX LEAD"; value: root.percent((runtimeState.runtimeEffective && runtimeState.runtimeEffective.maximumLead) || 0) }
                    }
                }
            }

            Card {
                RowLayout { width: parent.width; spacing: 14
                    ColumnLayout { Layout.preferredWidth: 170
                        Caption { text: "EDIT LEVEL" }
                        ResponseCombo { Layout.fillWidth: true; model: [{name:"Global Defaults", value:"global"}, {name:"Category", value:"category"}, {name:"Game Profile", value:"profile"}, {name:"Response Preset", value:"preset"}]; textRole: "name"; valueRole: "value"; currentIndex: root.editScope === "global" ? 0 : root.editScope === "category" ? 1 : root.editScope === "preset" ? 3 : 2; onActivated: { root.editScope = currentValue; root.targetId = ""; root.setPreview() } }
                    }
                    ColumnLayout { Layout.preferredWidth: 220
                        Caption { text: "TARGET" }
                        ResponseCombo { Layout.fillWidth: true; model: root.targetChoices(); textRole: "name"; valueRole: "id"; currentIndex: root.targetIndex(); onActivated: { root.targetId = currentValue; root.setPreview() } }
                    }
                    ColumnLayout { Layout.preferredWidth: 240
                        Caption { text: "AXIS" }
                        ResponseCombo { id: axisSelector; Layout.fillWidth: true; model: backendObject.axes; textRole: "label"; valueRole: "index"; currentIndex: Math.max(0, backendObject.selectedAxisIndex); onActivated: backendObject.setSelectedAxis(currentValue) }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.themeTokens.divider }
                    ColumnLayout { Layout.fillWidth: true
                        Caption { text: "EDITING CONTEXT" }
                        Text { text: root.editScope === "preset" ? "Response Preset → " + root.selectedTargetName() + " → " + (state.axisLabel || "Axis") : "Editing " + root.editScope.toUpperCase() + " · " + root.selectedTargetName() + " · " + (scopeInfo().source || "Inherited") + " · " + (state.axisLabel || "Axis"); color: root.themeTokens.text; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 12
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Simple controls"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Choose a response level for " + state.axisLabel + ". The configured horizon is a maximum; response remains adaptive at every level."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { text: scopeInfo().source || "Inherited"; color: root.themeTokens.orange; font.pixelSize: 11; font.bold: true }
                    }
                    Flow { width: parent.width; spacing: 8
                        Repeater { model: backendObject.adaptiveResponsePresets
                            delegate: ActionButton { required property var modelData; text: modelData.name.toUpperCase(); accent: (scopeInfo().presetId === modelData.id); ToolTip.visible: hovered; ToolTip.text: modelData.description; enabled: root.editScope !== "preset"; onClicked: backendObject.setAdaptiveResponsePresetAtContext(root.editScope, root.selectedTargetId(), state.axis, modelData.id) }
                        }
                    }
                    Row { spacing: 28
                        Metric { caption: "MAX HORIZON"; value: Number(effective().maximumHorizonMs || 0).toFixed(1) + " ms"; tone: root.themeTokens.orange }
                        Metric { caption: "MAX LEAD"; value: percent(effective().maximumLead || 0) }
                        Metric { caption: "PREDICTOR"; value: String(effective().model || "auto").toUpperCase() }
                        Metric { caption: "REVERSAL"; value: Math.round((effective().reversalResponse || 0) * 100) + "%" }
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
                        ResponseCombo { id: scenarioSelector; Layout.preferredWidth: 176; model: ["Slow Sweep", "Fast Sweep", "Rapid Reversal", "Positive-Side Reversal", "Negative-Side Reversal", "Center-Crossing Reversal", "Micro Adjustments", "Sudden Stop", "Center Fighting"]; currentIndex: Math.max(0, model.indexOf(root.scenario)); onActivated: { root.scenario = currentText; root.setPreview() } }
                    }
                    Canvas { id: graph; width: parent.width; height: 220
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset(); ctx.fillStyle = root.themeTokens.panelInset; ctx.fillRect(0, 0, width, height);
                            ctx.strokeStyle = root.themeTokens.divider; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke();
                            function trace(field, color) {
                                if (!root.previewSamples || root.previewSamples.length === 0) return;
                                ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
                                for (let i = 0; i < root.previewSamples.length; ++i) { const point = root.previewSamples[i]; const v = Math.max(-1, Math.min(1, Number(point[field]))); const x = i * width / Math.max(1, root.previewSamples.length - 1); const y = height * (1 - (v + 1) * 0.5); if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
                                ctx.stroke();
                            }
                            trace("physical", root.themeTokens.textMuted); trace("estimated", root.themeTokens.cyan); trace("predicted", root.themeTokens.orange); trace("virtualOutput", root.themeTokens.ready);
                        }
                        Connections { target: root; function onPreviewSamplesChanged() { graph.requestPaint() } }
                    }
                    Row { spacing: 16
                        Text { text: "— Physical"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Text { text: "— Estimated"; color: root.themeTokens.cyan; font.pixelSize: 10 }
                        Text { text: "— Predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                        Text { text: "— Virtual output"; color: root.themeTokens.ready; font.pixelSize: 10 }
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
                        ResponseCombo { Layout.preferredWidth: 260; model: root.comparisonChoices(); textRole: "label"; currentIndex: root.comparisonIndex(); onActivated: function(index) { const choice = root.comparisonChoices()[index]; root.comparisonScope = choice.scope; root.comparisonTargetId = choice.id; root.setPreview() } }
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
                        ActionButton { text: "RESET LAYER"; accent: false; onClicked: backendObject.resetAdaptiveResponseAxisAtContext(root.editScope, root.selectedTargetId(), state.axis) }
                    }
                    Column { visible: root.advancedExpanded; width: parent.width; spacing: 4
                        RowLayout { width: parent.width
                            Text { text: "Prediction model"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                            ResponseCombo { Layout.preferredWidth: 190; model: ["auto", "velocity", "alpha-beta", "alpha-beta-gamma"]; currentIndex: Math.max(0, model.indexOf(effective().model || "auto")); onActivated: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "model", currentText); root.setPreview() } }
                            ActionButton { text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere("model") ? "INHERITED" : "INHERIT"; accent: false; enabled: root.editScope === "global" || !root.inheritedHere("model"); implicitHeight: 28; padding: 8; onClicked: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "model", "auto", true); root.setPreview() } }
                        }
                        RowLayout { width: parent.width
                            Text { text: "Enabled"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                            Switch { checked: !!effective().enabled; onToggled: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "enabled", checked); root.setPreview() } }
                            ActionButton { text: root.editScope === "global" ? "RESET DEFAULT" : root.inheritedHere("enabled") ? "INHERITED" : "INHERIT"; accent: false; enabled: root.editScope === "global" || !root.inheritedHere("enabled"); implicitHeight: 28; padding: 8; onClicked: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "enabled", false, true); root.setPreview() } }
                        }
                        TuneRow { label: "Maximum horizon"; detail: "0–30 ms adaptive ceiling"; from: 0; to: 30; step: 0.5; propertyKey: "maximumHorizonMs"; value: effective().maximumHorizonMs || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "maximumHorizonMs", value); root.setPreview() } }
                        TuneRow { label: "Maximum lead"; detail: "Hard safety envelope in normalized axis units"; from: 0.01; to: 0.50; step: 0.01; propertyKey: "maximumLead"; value: effective().maximumLead || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "maximumLead", value); root.setPreview() } }
                        TuneRow { label: "Velocity response"; detail: "Derivative responsiveness during deliberate movement"; propertyKey: "velocityResponse"; value: effective().velocityResponse || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "velocityResponse", value); root.setPreview() } }
                        TuneRow { label: "Acceleration response"; detail: "ABG acceleration contribution"; propertyKey: "accelerationResponse"; value: effective().accelerationResponse || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "accelerationResponse", value); root.setPreview() } }
                        TuneRow { label: "Motion sensitivity"; detail: "How much deliberate movement activates prediction"; from: 0.001; to: 2; step: 0.005; propertyKey: "motionSensitivity"; value: effective().motionSensitivity || 0.035; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "motionSensitivity", value); root.setPreview() } }
                        TuneRow { label: "Noise rejection"; detail: "Ignore tiny sensor movement when estimating motion"; from: 0; to: 0.50; step: 0.001; propertyKey: "noiseRejection"; value: effective().noiseRejection || 0.012; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "noiseRejection", value); root.setPreview() } }
                        TuneRow { label: "Reversal detection"; detail: "Motion threshold that clears stale directional lead"; from: 0.001; to: 10; step: 0.01; propertyKey: "reversalDetection"; value: effective().reversalDetection || 0.075; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "reversalDetection", value); root.setPreview() } }
                        TuneRow { label: "Reversal response"; detail: "Rapidly cancels stale lead and reacquires direction"; propertyKey: "reversalResponse"; value: effective().reversalResponse || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "reversalResponse", value); root.setPreview() } }
                        TuneRow { label: "Deceleration response"; detail: "Reduces lead while braking"; propertyKey: "decelerationResponse"; value: effective().decelerationResponse || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "decelerationResponse", value); root.setPreview() } }
                        TuneRow { label: "Settling response"; detail: "Collapses horizon and damps state as motion comes to rest"; propertyKey: "settlingResponse"; value: effective().settlingResponse || 0; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "settlingResponse", value); root.setPreview() } }
                        TuneRow { label: "Endpoint taper"; detail: "Tapers lead into available headroom"; from: 0.01; to: 1; step: 0.01; propertyKey: "endpointTaper"; value: effective().endpointTaper || 0.16; onChanged: { backendObject.setAdaptiveResponsePropertyAtContext(root.editScope, root.selectedTargetId(), state.axis, "endpointTaper", value); root.setPreview() } }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 12
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Live telemetry"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Sampled from an atomic latest-state snapshot at UI cadence. Mapping reports never drive the UI directly."; color: root.themeTokens.textMuted; font.pixelSize: 11 }
                        }
                        Text { text: telemetry.state || "Stable"; color: root.themeTokens.orange; font.pixelSize: 16; font.bold: true }
                    }
                    Flow { width: parent.width; spacing: 16
                        Metric { caption: "PHYSICAL"; value: percent(telemetry.physical || 0) }
                        Metric { caption: "ESTIMATED"; value: percent(telemetry.estimated || 0) }
                        Metric { caption: "PREDICTED"; value: percent(telemetry.predicted || 0); tone: root.themeTokens.orange }
                        Metric { caption: "VIRTUAL OUTPUT"; value: percent(telemetry.virtualOutput || 0); tone: root.themeTokens.ready }
                        Metric { caption: "VELOCITY"; value: Number(telemetry.velocity || 0).toFixed(2) }
                        Metric { caption: "ACCELERATION"; value: Number(telemetry.acceleration || 0).toFixed(1) }
                        Metric { caption: "MOTION"; value: Math.round((telemetry.motionIntensity || 0) * 100) + "%" }
                        Metric { caption: "ACTIVE HORIZON"; value: Number(telemetry.activeHorizonMs || 0).toFixed(2) + " ms" }
                        Metric { caption: "MAX HORIZON"; value: Number(telemetry.maximumHorizonMs || 0).toFixed(1) + " ms" }
                        Metric { caption: "LEAD"; value: percent(telemetry.lead || 0) }
                        Metric { caption: "MAX LEAD"; value: percent(telemetry.maximumLead || 0) }
                        Metric { caption: "CONFIDENCE"; value: Math.round((telemetry.confidence || 0) * 100) + "%" }
                        Metric { caption: "REVERSALS"; value: telemetry.reversalCount || 0 }
                        Metric { caption: "SAFETY CLAMPS"; value: telemetry.safetyClampCount || 0 }
                    }
                    Flow { width: parent.width; spacing: 14
                        Gauge { caption: "ACTIVE HORIZON"; value: telemetry.activeHorizonMs || 0; maximum: Math.max(0.1, telemetry.maximumHorizonMs || 0); tone: root.themeTokens.orange }
                        Gauge { caption: "PREDICTION LEAD"; value: Math.abs(telemetry.lead || 0); maximum: Math.max(0.001, telemetry.maximumLead || 0); tone: root.themeTokens.ready }
                        Gauge { caption: "CONFIDENCE"; value: telemetry.confidence || 0; maximum: 1; tone: root.themeTokens.textStrong }
                        Gauge { caption: "MOTION INTENSITY"; value: telemetry.motionIntensity || 0; maximum: 1; tone: root.themeTokens.orange }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 9
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Live analysis"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "A fixed UI-side ring samples the atomic latest state at presentation cadence. Position, motion, and Adaptive Response each retain their own readable scale."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Repeater { model: [2, 5, 10, 30]
                            delegate: ActionButton { required property var modelData; text: modelData + "s"; accent: root.historyWindowSeconds === modelData; implicitHeight: 28; padding: 8; onClicked: { root.historyWindowSeconds = modelData; root.refreshHistory() } }
                        }
                        ActionButton { text: root.historyPaused ? "RESUME" : "PAUSE"; accent: false; implicitHeight: 28; padding: 8; onClicked: { root.historyPaused = !root.historyPaused; if (!root.historyPaused) root.refreshHistory() } }
                    }
                    RowLayout { width: parent.width
                        Caption { text: (root.historyPaused ? "PAUSED INSPECTION" : "LIVE") + " · " + (runtimeState.axisLabel || state.axisLabel || "Axis").toUpperCase() }
                        Item { Layout.fillWidth: true }
                        Caption { text: "−" + root.historyWindowSeconds + "s   →   NOW · shared time axis" }
                    }
                    Text { text: "AXIS POSITION  ·  normalized axis units"; color: root.themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
                    HistoryGraph { Layout.fillWidth: true; Layout.preferredHeight: 156; lowerBound: -1; upperBound: 1
                        series: [{field:"physical", color:root.themeTokens.textMuted}, {field:"estimated", color:root.themeTokens.cyan}, {field:"predicted", color:root.themeTokens.orange}, {field:"virtualOutput", color:root.themeTokens.ready}] }
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
                        Slider { width: parent.width; from: 0; to: Math.max(0, root.historySamples.length - 1); stepSize: 1; value: Math.max(0, root.historyInspectIndex); onMoved: root.historyInspectIndex = Math.round(value) }
                        Flow { width: parent.width; spacing: 14
                            Metric { caption: "INSPECT TIME"; value: Number(root.inspectedHistorySample().timeMs || 0).toFixed(0) + " ms" }
                            Metric { caption: "PHYSICAL / ESTIMATE"; value: root.percent(root.inspectedHistorySample().physical || 0) + " / " + root.percent(root.inspectedHistorySample().estimated || 0) }
                            Metric { caption: "PREDICTED / OUTPUT"; value: root.percent(root.inspectedHistorySample().predicted || 0) + " / " + root.percent(root.inspectedHistorySample().virtualOutput || 0) }
                            Metric { caption: "VELOCITY / ACCEL"; value: Number(root.inspectedHistorySample().velocity || 0).toFixed(2) + " / " + Number(root.inspectedHistorySample().acceleration || 0).toFixed(1) }
                            Metric { caption: "HORIZON / LEAD"; value: Number(root.inspectedHistorySample().activeHorizonMs || 0).toFixed(2) + " ms / " + root.percent(root.inspectedHistorySample().lead || 0) }
                            Metric { caption: "CONFIDENCE / STATE"; value: Math.round((root.inspectedHistorySample().confidence || 0) * 100) + "% / " + (root.inspectedHistorySample().state || "Stable") }
                        }
                    }
                }
            }

            Card {
                Column { width: parent.width; spacing: 8
                    RowLayout { width: parent.width
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "Test Lab"; color: root.themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                            Text { text: "Use the repeatable graph above to inspect sweep, reversal, stop, micro-adjustment, and center-fighting behavior before launching a game."; color: root.themeTokens.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { text: root.testLabExpanded ? "COLLAPSE" : "OPEN"; accent: false; onClicked: root.testLabExpanded = !root.testLabExpanded }
                    }
                    Column { visible: root.testLabExpanded; width: parent.width; spacing: 4
                        Text { text: root.scenario.toUpperCase() + " · " + (testLabMetrics.sampleCount || 0) + " estimator samples"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                        Flow { width: parent.width; spacing: 16
                            Metric { caption: "PEAK LEAD"; value: percent(testLabMetrics.peakLead || 0); tone: root.themeTokens.orange }
                            Metric { caption: "PEAK PREDICTION ERROR"; value: percent(testLabMetrics.peakPredictionError || 0) }
                            Metric { caption: "REVERSAL DETECTED"; value: Number(testLabMetrics.reversalDetectionMs || 0) < 0 ? "—" : Number(testLabMetrics.reversalDetectionMs).toFixed(1) + " ms" }
                            Metric { caption: "STALE LEAD CLEARED"; value: Number(testLabMetrics.staleLeadCancellationMs || 0) < 0 ? "—" : Number(testLabMetrics.staleLeadCancellationMs).toFixed(1) + " ms"; tone: root.themeTokens.ready }
                            Metric { caption: "OPPOSITE LEAD READY"; value: Number(testLabMetrics.oppositeDirectionReacquisitionMs || 0) < 0 ? "—" : Number(testLabMetrics.oppositeDirectionReacquisitionMs).toFixed(1) + " ms"; tone: root.themeTokens.ready }
                            Metric { caption: "SETTLING TIME"; value: Number(testLabMetrics.settlingTimeMs || 0) < 0 ? "—" : Number(testLabMetrics.settlingTimeMs).toFixed(1) + " ms" }
                        }
                        Text { text: "The Test Lab uses the same fixed-size runtime estimator as mapping, but creates an isolated synthetic processor. Physical input always remains the final predictive baseline."; color: root.themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
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
