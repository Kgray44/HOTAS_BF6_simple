import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: root
    required property var backendObject
    required property var themeTokens
    property bool topGun: false
    property string editScope: "profile"
    property string scenario: "Rapid Reversal"
    property var state: backendObject.adaptiveResponseState
    property var telemetry: backendObject.adaptiveResponseTelemetry
    property var previewSamples: backendObject.adaptiveResponsePreview(scenario)
    property bool advancedExpanded: false
    property bool testLabExpanded: false

    function effective() { return state.effective || ({}) }
    function scopeInfo() {
        if (editScope === "global") return state.global || ({})
        if (editScope === "category") return state.categoryLayer || ({})
        return state.profileLayer || ({})
    }
    function setPreview() { previewSamples = backendObject.adaptiveResponsePreview(scenario) }
    function percent(value) { return (Number(value) * 100 >= 0 ? "+" : "") + (Number(value) * 100).toFixed(1) + "%" }

    component Card: Rectangle {
        default property alias content: contentHost.data
        property color tone: root.themeTokens.border
        color: root.themeTokens.panel
        border.color: tone
        radius: root.themeTokens.controlRadius
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
        property bool accent: true
        implicitHeight: 34
        padding: 12
        font.pixelSize: 10
        font.bold: true
        contentItem: Text { text: parent.text; color: parent.enabled ? root.themeTokens.textStrong : root.themeTokens.textFaint; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: parent.font }
        background: Rectangle { radius: root.themeTokens.controlRadius; color: parent.down ? root.themeTokens.controlPressed : parent.hovered ? root.themeTokens.controlHover : (parent.accent ? root.themeTokens.buttonSurface : root.themeTokens.buttonSecondary); border.color: parent.accent ? root.themeTokens.orange : root.themeTokens.border }
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
    component TuneRow: Item {
        property string label: ""
        property string detail: ""
        property real value: 0
        property real from: 0
        property real to: 1
        property real step: 0.01
        signal changed(real value)
        implicitHeight: 52
        RowLayout { anchors.fill: parent; spacing: 12
            ColumnLayout { Layout.fillWidth: true; spacing: 2
                Text { text: parent.parent.label; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true }
                Text { text: parent.parent.detail; color: root.themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Slider { id: slider; Layout.preferredWidth: 220; from: parent.parent.from; to: parent.parent.to; stepSize: parent.parent.step; value: parent.parent.value; onMoved: parent.parent.changed(value) }
            Text { Layout.preferredWidth: 52; text: Number(parent.value).toFixed(parent.to <= 1 ? 2 : 1); color: root.themeTokens.textStrong; horizontalAlignment: Text.AlignRight; font.pixelSize: 11; font.family: root.themeTokens.telemetryFont }
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
                ActionButton { text: "SAVE AS PRESET"; onClicked: savePresetDialog.open() }
            }

            Card {
                RowLayout { width: parent.width; spacing: 14
                    ColumnLayout { Layout.preferredWidth: 170
                        Caption { text: "EDIT LEVEL" }
                        ComboBox { Layout.fillWidth: true; model: ["Global Defaults", "Category", "Game Profile"]; currentIndex: root.editScope === "global" ? 0 : root.editScope === "category" ? 1 : 2; onActivated: root.editScope = index === 0 ? "global" : index === 1 ? "category" : "profile" }
                    }
                    ColumnLayout { Layout.preferredWidth: 240
                        Caption { text: "AXIS" }
                        ComboBox { id: axisSelector; Layout.fillWidth: true; model: backendObject.axes; textRole: "label"; valueRole: "index"; currentIndex: Math.max(0, backendObject.selectedAxisIndex); onActivated: backendObject.setSelectedAxis(currentValue) }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.themeTokens.divider }
                    ColumnLayout { Layout.fillWidth: true
                        Caption { text: "CURRENT EFFECTIVE SOURCE" }
                        Text { text: "Global: " + (state.global ? state.global.source : "Application default") + "   →   Category: " + (state.categoryLayer ? state.categoryLayer.source : "Inherited") + "   →   Profile: " + (state.profileLayer ? state.profileLayer.source : "Inherited"); color: root.themeTokens.text; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                            delegate: ActionButton { required property var modelData; text: modelData.name.toUpperCase(); accent: (scopeInfo().presetId === modelData.id); ToolTip.visible: hovered; ToolTip.text: modelData.description; onClicked: backendObject.setAdaptiveResponsePreset(root.editScope, state.axis, modelData.id) }
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
                        ComboBox { id: scenarioSelector; model: ["Slow Sweep", "Fast Sweep", "Rapid Reversal", "Micro Adjustments", "Sudden Stop", "Center Fighting"]; currentIndex: Math.max(0, model.indexOf(root.scenario)); onActivated: { root.scenario = currentText; root.setPreview() } }
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
                            trace("physical", root.themeTokens.textMuted); trace("estimated", "#5db4dd"); trace("predicted", root.themeTokens.orange); trace("virtualOutput", root.themeTokens.ready);
                        }
                        Connections { target: root; function onPreviewSamplesChanged() { graph.requestPaint() } }
                    }
                    Row { spacing: 16
                        Text { text: "— Physical"; color: root.themeTokens.textMuted; font.pixelSize: 10 }
                        Text { text: "— Estimated"; color: "#5db4dd"; font.pixelSize: 10 }
                        Text { text: "— Predicted"; color: root.themeTokens.orange; font.pixelSize: 10 }
                        Text { text: "— Virtual output"; color: root.themeTokens.ready; font.pixelSize: 10 }
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
                        ActionButton { text: "RESET LAYER"; accent: false; onClicked: backendObject.resetAdaptiveResponseAxis(root.editScope, state.axis) }
                    }
                    Column { visible: root.advancedExpanded; width: parent.width; spacing: 4
                        RowLayout { width: parent.width
                            Text { text: "Prediction model"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                            ComboBox { model: ["auto", "velocity", "alpha-beta", "alpha-beta-gamma"]; currentIndex: Math.max(0, model.indexOf(effective().model || "auto")); onActivated: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "model", currentText) }
                        }
                        RowLayout { width: parent.width
                            Text { text: "Enabled"; color: root.themeTokens.text; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                            Switch { checked: !!effective().enabled; onToggled: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "enabled", checked) }
                        }
                        TuneRow { label: "Maximum horizon"; detail: "0–30 ms adaptive ceiling"; from: 0; to: 30; step: 0.5; value: effective().maximumHorizonMs || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "maximumHorizonMs", value) }
                        TuneRow { label: "Maximum lead"; detail: "Hard safety envelope in normalized axis units"; from: 0.01; to: 0.50; step: 0.01; value: effective().maximumLead || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "maximumLead", value) }
                        TuneRow { label: "Velocity response"; detail: "Derivative responsiveness during deliberate movement"; value: effective().velocityResponse || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "velocityResponse", value) }
                        TuneRow { label: "Acceleration response"; detail: "ABG acceleration contribution"; value: effective().accelerationResponse || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "accelerationResponse", value) }
                        TuneRow { label: "Reversal response"; detail: "Rapidly cancels stale lead and reacquires direction"; value: effective().reversalResponse || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "reversalResponse", value) }
                        TuneRow { label: "Deceleration response"; detail: "Reduces lead while braking or settling"; value: effective().decelerationResponse || 0; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "decelerationResponse", value) }
                        TuneRow { label: "Endpoint taper"; detail: "Tapers lead into available headroom"; from: 0.01; to: 1; step: 0.01; value: effective().endpointTaper || 0.16; onChanged: backendObject.setAdaptiveResponseProperty(root.editScope, state.axis, "endpointTaper", value) }
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
                        Metric { caption: "ACTIVE HORIZON"; value: Number(telemetry.activeHorizonMs || 0).toFixed(2) + " ms" }
                        Metric { caption: "LEAD"; value: percent(telemetry.lead || 0) }
                        Metric { caption: "CONFIDENCE"; value: Math.round((telemetry.confidence || 0) * 100) + "%" }
                        Metric { caption: "REVERSALS"; value: telemetry.reversalCount || 0 }
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
                        Text { text: "Reversal count: " + (telemetry.reversalCount || 0) + "   ·   Safety clamps: " + (telemetry.safetyClampCount || 0) + "   ·   Live state: " + (telemetry.state || "Stable"); color: root.themeTokens.text; font.pixelSize: 12 }
                        Text { text: "The predictor always measures normalized physical movement. Predicted output is never fed back into its own velocity or acceleration estimate."; color: root.themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
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
}
