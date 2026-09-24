import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// One reusable editor for the physical-acquisition escape hatch. It does not
// expose mapping, curve, calibration, or output settings because those stay
// in their existing Flight Deck owners.
Dialog {
    id: root
    objectName: "flightDeckAxisAcquisitionDialog"
    required property var tokens
    property var axis: ({})
    property int axisIndex: -1
    property var sourceMonitor: []
    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(tokens.scale(690), parent ? parent.width - tokens.space24 * 2 : tokens.scale(690))
    height: Math.min(content.implicitHeight + tokens.space24 * 2, parent ? parent.height - tokens.space24 * 2 : content.implicitHeight + tokens.space24 * 2)
    padding: tokens.space16
    standardButtons: Dialog.NoButton

    property var axisChoices: [
        { label: "X", value: 0 }, { label: "Y", value: 1 }, { label: "Z", value: 2 },
        { label: "Rx", value: 3 }, { label: "Ry", value: 4 }, { label: "Rz", value: 5 },
        { label: "Slider 0", value: 6 }, { label: "Slider 1", value: 7 }
    ]

    function indexForValue(choices, value) {
        for (let index = 0; index < choices.length; ++index)
            if (Number(choices[index].value) === Number(value)) return index
        return 0
    }

    function sourceChoiceModel() {
        const choices = []
        for (let index = 0; index < axisChoices.length; ++index)
            choices.push({ label: "DirectInput " + axisChoices[index].label, value: axisChoices[index].value })
        return choices
    }

    function canonicalChoiceModel() {
        const choices = [{ label: "Automatic", value: -1 }]
        for (let index = 0; index < axisChoices.length; ++index)
            choices.push(axisChoices[index])
        return choices
    }

    onOpened: {
        canonical.currentIndex = Boolean(axis.manualOverride) && !Boolean(axis.manualAutomaticTarget)
            ? indexForValue(canonicalChoiceModel(), Number(axis.manualTargetAxisIndex)) : 0
        source.currentIndex = indexForValue(sourceChoiceModel(), Number(axis.formattedSourceIndex))
        mode.currentIndex = axis.manualOverrideMode === "Exact native object" ? 2
            : Boolean(axis.manualOverride) && !Boolean(axis.manualAutomaticSource) ? 1 : 0
        range.currentIndex = axis.manualRangePolicy === "Manual" ? 2 : axis.manualRangePolicy === "Observed snapshot" ? 3 : axis.manualRangePolicy === "Driver-reported" ? 1 : 0
        interpretation.currentIndex = axis.manualInterpretation === "Centered absolute" ? 1 : axis.manualInterpretation === "One-sided absolute" ? 2 : 0
        polarity.currentIndex = axis.manualPolarity === "Normal" ? 1 : axis.manualPolarity === "Reversed" ? 2 : 0
        minimum.text = String(axis.manualMinimum !== undefined ? axis.manualMinimum : axis.nativeRangeMinimum)
        maximum.text = String(axis.manualMaximum !== undefined ? axis.manualMaximum : axis.nativeRangeMaximum)
        validation.text = ""
    }

    background: Rectangle {
        radius: 9
        color: root.tokens.elevatedSurface
        border.width: 1
        border.color: root.tokens.accent
    }

    contentItem: Flickable {
        id: scroll
        Accessible.name: "Manual raw input acquisition override"
        implicitWidth: root.width - root.leftPadding - root.rightPadding
        implicitHeight: Math.min(content.implicitHeight, root.parent ? root.parent.height - root.tokens.space48 : content.implicitHeight)
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: content
            width: scroll.width
            spacing: root.tokens.space12
            Text {
                text: "MANUAL RAW-INPUT OVERRIDE"
                color: root.tokens.textPrimary
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.section
                font.bold: true
                Layout.fillWidth: true
            }
            Text {
                text: "This controls only physical acquisition and its raw normalization. The normal calibration, transformation, curve, Adaptive Response, and vJoy route remain unchanged."
                color: root.tokens.textSecondary
                font.pixelSize: root.tokens.body
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                text: "Raw HID acquisition is unavailable here. This dialog uses the inspected DirectInput capability record only; it does not open a second input path."
                color: root.tokens.textMuted
                font.pixelSize: root.tokens.bodySmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: automatic.implicitHeight + root.tokens.space16 * 2
                radius: 6
                color: root.tokens.primarySurface
                border.color: root.tokens.border
                ColumnLayout {
                    id: automatic
                    anchors.fill: parent
                    anchors.margins: root.tokens.space12
                    spacing: root.tokens.space4
                    Text { text: "CURRENT AUTOMATIC RESOLUTION"; color: root.tokens.textMuted; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true }
                    Text { text: String(root.axis.resolutionSource || "Unresolved") + " · " + String(root.axis.resolutionConfidence || "Low") + " confidence"; color: root.tokens.textPrimary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.body }
                    Text { text: "Native " + String(root.axis.nativeObjectName || "unknown") + " · " + String(root.axis.directInputGuid || "no semantic GUID") + " · source " + String(root.axis.formattedSource || "not resolved"); color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                }
            }

            component FieldLabel: Text {
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.caption
                font.bold: true
            }
            component EditorCombo: ComboBox {
                id: combo
                implicitHeight: root.tokens.controlHeight
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                contentItem: Text { leftPadding: root.tokens.space10; rightPadding: root.tokens.space24; text: combo.displayText; color: combo.enabled ? root.tokens.textPrimary : root.tokens.disabled; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                indicator: Text { x: combo.width - width - root.tokens.space10; anchors.verticalCenter: parent.verticalCenter; text: "⌄"; color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.section }
                background: Rectangle { radius: 5; color: root.tokens.primarySurface; border.width: combo.activeFocus ? 2 : 1; border.color: combo.activeFocus ? root.tokens.focus : root.tokens.border }
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    highlighted: combo.highlightedIndex === index
                    contentItem: Text { text: String(modelData.label || modelData); color: root.tokens.textPrimary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; verticalAlignment: Text.AlignVCenter; leftPadding: root.tokens.space10 }
                    background: Rectangle { color: parent.highlighted ? root.tokens.selected : root.tokens.elevatedSurface }
                }
                popup: Popup {
                    y: combo.height - 1
                    width: combo.width
                    padding: root.tokens.space4
                    contentItem: ListView { clip: true; implicitHeight: Math.min(contentHeight, root.tokens.scale(250)); model: combo.delegateModel; currentIndex: combo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
                    background: Rectangle { radius: 5; color: root.tokens.elevatedSurface; border.color: root.tokens.border }
                }
            }
            component EditorField: TextField {
                id: field
                implicitHeight: root.tokens.controlHeight
                Layout.fillWidth: true
                selectByMouse: true
                color: root.tokens.textPrimary
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.bodySmall
                validator: IntValidator { bottom: -1000000; top: 1000000 }
                background: Rectangle { radius: 5; color: root.tokens.primarySurface; border.width: field.activeFocus ? 2 : 1; border.color: field.activeFocus ? root.tokens.focus : root.tokens.border }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width < root.tokens.scale(560) ? 1 : 2
                rowSpacing: root.tokens.space8
                columnSpacing: root.tokens.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "CANONICAL AXIS IDENTITY" }
                    EditorCombo { id: canonical; model: root.canonicalChoiceModel() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "ACQUISITION SOURCE" }
                    EditorCombo {
                        id: mode
                        model: [ { label: "Automatic DirectInput source", value: "automatic-source" }, { label: "DirectInput formatted source", value: "direct-input" }, { label: "Exact native object", value: "exact-native" } ]
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: mode.currentValue === "direct-input"
                    FieldLabel { text: "DIRECTINPUT SOURCE" }
                    EditorCombo { id: source; model: root.sourceChoiceModel() }
                }
                Text {
                    visible: mode.currentValue === "automatic-source"
                    Layout.fillWidth: true
                    Layout.columnSpan: root.width < root.tokens.scale(560) ? 1 : 2
                    text: "Uses the uniquely resolved DirectInput source for the selected canonical axis. Reconnect validation keeps this source automatic; no enumeration position is saved."
                    color: root.tokens.textMuted
                    font.pixelSize: root.tokens.bodySmall
                    wrapMode: Text.WordWrap
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: mode.currentValue === "exact-native"
                    FieldLabel { text: "EXACT NATIVE SIGNATURE" }
                    Text { text: String(root.axis.directInputGuid || "No safe native identity") + " · offset " + String(root.axis.directInputOffset || 0); color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "RAW RANGE" }
                    EditorCombo { id: range; model: [ { label: "Automatic", value: "automatic" }, { label: "Driver-reported", value: "driver" }, { label: "Manual", value: "manual" }, { label: "Observed snapshot", value: "observed" } ] }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "RAW INTERPRETATION" }
                    EditorCombo { id: interpretation; model: [ { label: "Automatic", value: "automatic" }, { label: "Centered absolute", value: "centered" }, { label: "One-sided absolute", value: "one-sided" } ] }
                }
                ColumnLayout {
                    visible: range.currentValue === "manual"
                    Layout.fillWidth: true
                    FieldLabel { text: "RAW MINIMUM" }
                    EditorField { id: minimum }
                }
                ColumnLayout {
                    visible: range.currentValue === "manual"
                    Layout.fillWidth: true
                    FieldLabel { text: "RAW MAXIMUM" }
                    EditorField { id: maximum }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    FieldLabel { text: "RAW POLARITY" }
                    EditorCombo { id: polarity; model: [ { label: "Automatic", value: "automatic" }, { label: "Normal", value: "normal" }, { label: "Reversed", value: "reversed" } ] }
                }
            }
            Text {
                visible: range.currentValue === "observed"
                text: "Observed range saves the current bounded monitor span. Move the control while the source monitor is open before saving."
                color: root.tokens.attention
                font.pixelSize: root.tokens.bodySmall
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text { id: validation; color: root.tokens.fault; font.pixelSize: root.tokens.bodySmall; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "CANCEL"
                    onClicked: root.close()
                    contentItem: Text { text: parent.text; color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 5; color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.selected : "transparent"; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border; border.width: parent.activeFocus ? 2 : 1 }
                }
                Button {
                    text: "SAVE OVERRIDE"
                    onClicked: {
                        if (range.currentValue === "manual" && Number(minimum.text) >= Number(maximum.text)) {
                            validation.text = "Raw minimum must be below raw maximum."
                            return
                        }
                        const saved = backend.saveAxisAcquisitionOverride(root.axisIndex,
                            Number(canonical.currentValue), mode.currentValue === "automatic-source"
                                ? -1 : Number(source.currentValue),
                            String(mode.currentValue), String(range.currentValue), Number(minimum.text),
                            Number(maximum.text), String(interpretation.currentValue), String(polarity.currentValue))
                        if (saved) root.close()
                        else validation.text = "The override was not saved. Review the selected controller and source evidence."
                    }
                    contentItem: Text { text: parent.text; color: root.tokens.primarySurface; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 5; color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.focus : root.tokens.accent; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.accent; border.width: parent.activeFocus ? 2 : 1 }
                }
            }
        }
    }
}
