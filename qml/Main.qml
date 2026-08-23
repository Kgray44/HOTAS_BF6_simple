import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

ApplicationWindow {
    id: root
    width: 1320
    height: 840
    minimumWidth: 900
    minimumHeight: 650
    visible: true
    title: "HOTAS BF6"
    color: "#0d1013"
    font.family: "Segoe UI Variable"

    property int currentPage: 0
    property bool menuOpen: false
    property var allAxes: backend.axes
    property var allButtons: backend.buttons
    property var allPovs: backend.povs
    property var allPovInputs: backend.povInputs
    property int conflictingAxis: -1
    property string conflictingTarget: "Disabled"
    property int conflictingButton: -1
    property int conflictingVirtualButton: 0
    property int conflictingPovHat: 0
    property int conflictingPovDirection: -1
    readonly property var outputChoices: ["Disabled", "X", "Y", "Z", "Rz"]
    readonly property var buttonOutputChoices: backend.buttonOutputChoices
    readonly property var profileTriggerChoices: backend.profileTriggerChoices
    readonly property var profileTriggerBehaviorChoices: backend.profileTriggerBehaviorChoices
    readonly property var nativePovTargetChoices: backend.nativePovTargetChoices
    readonly property bool hasPhysicalInput: backend.physicalConnected && backend.axisCount > 0
    readonly property var selectedAxisInfo: root.axisAt(backend.selectedAxisIndex)

    function axisAt(index) { return allAxes[index] }
    function isPrimaryAxis(index) { return [0, 1, 5, 2].indexOf(index) >= 0 }
    function axisSelectorModel() {
        const choices = []
        for (let index = 0; index < allAxes.length; ++index) {
            const axis = allAxes[index]
            if (!axis || !axis.available) continue
            choices.push({ axisIndex: axis.index,
                display: (isPrimaryAxis(axis.index) ? "PRIMARY  ·  " : "ADDITIONAL  ·  ")
                    + axis.label + " / " + axis.detail })
        }
        return choices
    }
    function buttonForVirtualButton(target) {
        for (let source = 0; source < allButtons.length; ++source) {
            if (allButtons[source].target === target) return allButtons[source]
        }
        return null
    }
    function physicalStatusText() {
        if (!backend.physicalConnected) return "NO CONTROLLER"
        if (backend.lastPhysicalUpdateAgeMs < 0) return "ACQUIRING"
        return backend.lastPhysicalUpdateAgeMs < 100 ? "LIVE" : "STALE"
    }
    function physicalStatusColor() {
        if (!backend.physicalConnected) return "#b77b86"
        return backend.lastPhysicalUpdateAgeMs >= 0 && backend.lastPhysicalUpdateAgeMs < 100 ? "#9fc4bb" : "#a7afb4"
    }
    function povText() {
        if (backend.povCount === 0) return "NOT PRESENT"
        if (backend.povValue < 0) return "CENTERED"
        return (backend.povValue / 100).toFixed(0) + "°"
    }
    function valuePercent(value) {
        return (Number(value) * 100 >= 0 ? "+" : "") + (Number(value) * 100).toFixed(1) + "%"
    }
    function controlValue(info, value) {
        if (info && info.unipolar) return ((Number(value) + 1) * 50).toFixed(1) + "%"
        return valuePercent(value)
    }
    function outputState(info) {
        if (!backend.vjoyReady) return "OUTPUT OFFLINE"
        if (!backend.mappingActive || !info.virtualValid) return "STANDBY"
        return controlValue(info, info.virtualValue)
    }
    function capacityState() {
        if (!backend.vjoyReady) return "VJOY OFFLINE"
        if (!backend.vjoyCapacitySufficient) return "CAPACITY INSUFFICIENT"
        return backend.vjoyButtonCount >= backend.vjoyRecommendedButtonCount ? "READY" : "CONFIGURATION LIMITED"
    }
    function capacityColor() {
        if (backend.vjoyStatusSeverity === "error") return "#ca9090"
        if (backend.vjoyStatusSeverity === "warning") return "#d4ad69"
        return "#8fd5c9"
    }
    function vjoyCardColor() {
        if (backend.vjoyStatusSeverity === "ready") return "#e51a352f"
        if (backend.vjoyStatusSeverity === "warning") return "#e52d2419"
        return "#e52f171b"
    }
    function vjoyCardBorder() {
        if (backend.vjoyStatusSeverity === "ready") return "#3c9ca8a0"
        if (backend.vjoyStatusSeverity === "warning") return "#c28b624f"
        return "#b75e674f"
    }

    component Panel: AviationPanel {}
    component FineLine: Rectangle { height: 1; color: "#33526870" }
    component StatusDot: Rectangle {
        property color tone: "#a5b9c0"
        width: 6
 height: 6
 radius: 3
        color: tone
    }
    component CommandButton: Rectangle {
        property string label: "ACTION"
        property bool commandEnabled: true
        property bool subdued: false
        signal triggered()
        implicitWidth: Math.max(110, labelText.implicitWidth + 30)
        implicitHeight: 36
        radius: 3
        color: !commandEnabled ? "#151a1e" : commandMouse.containsMouse ? (subdued ? "#303d44" : "#456c78") : (subdued ? "#222c32" : "#324f5a")
        border.color: !commandEnabled ? "#182f3539" : (subdued ? "#536975" : "#78aab9")
        opacity: commandEnabled ? 1.0 : 0.45
        Text { id: labelText
 anchors.centerIn: parent
 text: parent.label
 color: parent.commandEnabled ? "#f0f4f5" : "#879196"
 font.pixelSize: 11
 font.bold: true }
        MouseArea { id: commandMouse
 anchors.fill: parent
 hoverEnabled: true
 enabled: parent.commandEnabled
 cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
 onClicked: parent.triggered() }
    }
    component FlightComboBox: ComboBox {
        id: flightCombo
        property int popupMaximumHeight: 264
        implicitHeight: 31
        leftPadding: 9
        rightPadding: 28
        background: Rectangle {
            radius: 4
            color: flightCombo.enabled ? (flightCombo.hovered ? "#142128" : "#10171b") : "#0c1013"
            border.color: flightCombo.activeFocus ? "#78aab9" : flightCombo.hovered ? "#527482" : "#435660"
        }
        contentItem: Text {
            leftPadding: flightCombo.leftPadding
            rightPadding: flightCombo.rightPadding
            text: flightCombo.displayText
            color: flightCombo.enabled ? "#dce7e8" : "#748187"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: 10
        }
        indicator: Text {
            x: flightCombo.width - width - 9
            y: (flightCombo.height - height) / 2
            text: "⌄"
            color: flightCombo.enabled ? "#94adb5" : "#64747a"
            font.pixelSize: 15
        }
        delegate: ItemDelegate {
            id: choiceDelegate
            readonly property bool currentSelection: flightCombo.currentIndex === index
            width: flightCombo.width
            implicitHeight: Math.max(31, choiceText.implicitHeight + 12)
            highlighted: flightCombo.highlightedIndex === index
            contentItem: Text {
                id: choiceText
                text: flightCombo.textAt(index)
                color: choiceDelegate.highlighted ? "#eff8f7" : "#d0dcdd"
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
                rightPadding: 10
                elide: Text.ElideRight
                font.pixelSize: 10
            }
            background: Rectangle {
                radius: 3
                color: choiceDelegate.highlighted ? "#315a66" : (choiceDelegate.currentSelection ? "#244650" : (choiceDelegate.hovered ? "#1d333b" : "transparent"))
                border.color: choiceDelegate.highlighted ? "#6f9fac" : (choiceDelegate.currentSelection ? "#527d88" : "transparent")
            }
        }
        popup: Popup {
            id: selectorPopup
            y: flightCombo.height + 4
            width: flightCombo.width
            implicitHeight: Math.min(flightCombo.popupMaximumHeight,
                                     popupList.contentHeight + topPadding + bottomPadding)
            height: implicitHeight
            topPadding: 6
            bottomPadding: 6
            leftPadding: 6
            rightPadding: 6
            contentItem: ListView {
                id: popupList
                clip: true
                implicitHeight: contentHeight
                model: flightCombo.popup.visible ? flightCombo.delegateModel : null
                currentIndex: flightCombo.highlightedIndex
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                color: "#151e23"
                border.color: "#52717c"
                radius: 5
            }
        }
    }
    component FlightNumericStepper: Item {
        id: flightStepper
        property int value: 0
        property int from: -100
        property int to: 100
        signal valueEdited(int value)
        implicitWidth: 148
        implicitHeight: 30
        Rectangle {
            anchors.fill: parent
            color: "#11191d"
            border.color: "#435c66"
            radius: 4
        }
        Row {
            anchors.fill: parent
            Rectangle {
                width: 33; height: parent.height
                color: minusMouse.containsMouse && flightStepper.value > flightStepper.from ? "#244550" : "transparent"
                Text { anchors.centerIn: parent; text: "−"; color: flightStepper.value > flightStepper.from ? "#a8c8cf" : "#56656a"; font.pixelSize: 17 }
                MouseArea { id: minusMouse; anchors.fill: parent; hoverEnabled: true
                    enabled: flightStepper.value > flightStepper.from
                    onClicked: flightStepper.valueEdited(flightStepper.value - 1) }
            }
            Rectangle { width: 1; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; color: "#304954" }
            Text { width: parent.width - 68; height: parent.height; text: flightStepper.value + " %"
                color: "#e3eeee"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                font.pixelSize: 11; font.bold: true; font.family: "Consolas" }
            Rectangle { width: 1; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; color: "#304954" }
            Rectangle {
                width: 33; height: parent.height
                color: plusMouse.containsMouse && flightStepper.value < flightStepper.to ? "#244550" : "transparent"
                Text { anchors.centerIn: parent; text: "+"; color: flightStepper.value < flightStepper.to ? "#a8c8cf" : "#56656a"; font.pixelSize: 15 }
                MouseArea { id: plusMouse; anchors.fill: parent; hoverEnabled: true
                    enabled: flightStepper.value < flightStepper.to
                    onClicked: flightStepper.valueEdited(flightStepper.value + 1) }
            }
        }
    }
    component InstrumentMeter: Item {
        property real value: 0
        property bool offline: false
        property bool valid: true
        property color tone: "#a8c2ca"
        implicitHeight: 22
 implicitWidth: 160
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 width: parent.width
 height: 6
 radius: 1
 color: "#080c0f"
 border.color: "#354b5665" }
        Rectangle { visible: parent.valid; anchors.verticalCenter: parent.verticalCenter
            x: 2; width: Math.max(0, Math.min(parent.width - 4, ((parent.value + 1) * 0.5) * (parent.width - 4)))
            height: 2; color: Qt.rgba(parent.tone.r, parent.tone.g, parent.tone.b, 0.42) }
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 x: parent.width / 2
 width: 1
 height: 14
 color: "#6a9db0bb" }
        Rectangle { visible: !parent.offline && parent.valid
 width: 10
 height: 10
 radius: 2
 x: Math.max(0, Math.min(parent.width - width, ((parent.value + 1) * 0.5) * (parent.width - width)))
 anchors.verticalCenter: parent.verticalCenter
 color: parent.tone
 border.color: "#d0edf2" }
        Text { anchors.centerIn: parent
 visible: parent.offline || !parent.valid
 text: parent.offline ? "OFFLINE" : "STANDBY"
 color: "#9d8580"
 font.pixelSize: 9
 font.bold: true }
    }
    component TelemetryItem: Item {
        id: telemetryItem
        property string caption: "CAPTION"
        property string value: "—"
        property color tone: "#dce5e8"
        implicitWidth: 150
        implicitHeight: 46
        Column { anchors.verticalCenter: parent.verticalCenter
 width: telemetryItem.width
 spacing: 3
            Text { text: telemetryItem.caption
 color: "#8099a4"
 font.pixelSize: 10
 font.bold: true }
            Text { text: telemetryItem.value
 color: telemetryItem.tone
 font.pixelSize: 15
 font.bold: true
 font.family: "Consolas"
 elide: Text.ElideRight
 width: telemetryItem.width }
        }
    }
    component PageTitle: Item {
        id: pageTitle
        property string heading: "PAGE"
        property string detail: ""
        implicitHeight: 66
 implicitWidth: 300
        Column { anchors.verticalCenter: parent.verticalCenter
 spacing: 4
            Text { text: pageTitle.heading
 color: "#f3f7f7"
 font.pixelSize: 26
 font.bold: true }
            Text { text: pageTitle.detail
 color: "#9aa3a7"
 font.pixelSize: 12 }
        }
    }
    component OfflineMapper: Item {
        implicitHeight: 356
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            anchors.topMargin: 24
            anchors.bottomMargin: 18
            spacing: 18
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "No controller connected"
                    color: "#f0f2f0"
                    font.pixelSize: 30
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: backend.mappingActive ? "Mapping active" : "Mapping stopped"
                    color: backend.mappingActive ? "#a9c9b3" : "#a7afb4"
                    font.pixelSize: 10
                    font.bold: true
                }
            }
            Text {
                Layout.fillWidth: true
                text: "The mapper is idle. Connect the HOTAS whenever you are ready and its live controls will appear here automatically."
                color: "#9fa9ad"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Item { Layout.preferredHeight: 8 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 24
                Column {
                    Layout.fillWidth: true
                    spacing: 7
                    Text { text: "Physical input"; color: "#7f8a8f"; font.pixelSize: 11; font.bold: true }
                    Text { text: "Waiting for DirectInput"; color: "#dce2e0"; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: "No axes or buttons are being captured."; color: "#899397"; font.pixelSize: 11 }
                }
                FineLine { Layout.preferredWidth: 1; Layout.preferredHeight: 66 }
                Column {
                    Layout.fillWidth: true
                    spacing: 7
                    Text { text: "Virtual output"; color: "#7f8a8f"; font.pixelSize: 11; font.bold: true }
                    Text { text: backend.vjoyReady ? "Device ready" : "No output device in use"; color: "#dce2e0"; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text { text: backend.mappingActive ? "Output will resume when input returns." : backend.mappingRequested ? "Waiting for the controller; output resumes automatically." : "Mapping stays off until you start it."; color: "#899397"; font.pixelSize: 11 }
                }
            }
            Item { Layout.fillHeight: true }
            FineLine { Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Safe to leave open while your controller is disconnected."; color: "#859095"; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                StatusDot { tone: "#b77b86" }
                Text { text: "Waiting for device"; color: "#a9b2b4"; font.pixelSize: 11 }
            }
        }
    }
    component AxisModule: Panel {
        id: axisModule
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 220
        visible: info && info.available
        color: "#ed182128"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
 spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: axisModule.info.label
 color: "#eef7f7"
 font.pixelSize: 14
 font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: axisModule.info.detail.toUpperCase() + " · ROUTE " + axisModule.info.target
 color: axisModule.info.target === "Disabled" ? "#929da1" : "#a8d1dc"
 font.pixelSize: 9
 font.bold: true }
            }
            RowLayout { Layout.fillWidth: true
                Column { spacing: 1
                    Text { text: "LIVE COMMAND"; color: "#7694a0"; font.pixelSize: 8; font.bold: true }
                    Text { text: root.controlValue(axisModule.info, axisModule.info.transformed)
                        color: "#d9edf1"; font.pixelSize: 28; font.family: "Consolas"; font.weight: Font.DemiBold }
                }
                Item { Layout.fillWidth: true }
                Column { spacing: 2
                    Text { text: "VIRTUAL OUTPUT"; color: "#7694a0"; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignRight; width: 126 }
                    Text { text: root.outputState(axisModule.info); color: backend.vjoyReady ? "#9fcbbb" : "#d49b62"
                        font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignRight; width: 126 }
                }
            }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
 spacing: 12
                Text { text: "INPUT"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true
 Layout.preferredWidth: 48 }
                InstrumentMeter { Layout.fillWidth: true
 value: Number(axisModule.info.raw)
 tone: "#8eb5c1" }
                Text { text: root.controlValue(axisModule.info, axisModule.info.raw)
 color: "#c6dce1"
 font.family: "Consolas"
 font.pixelSize: 14
 font.weight: Font.DemiBold
 Layout.preferredWidth: 72
 horizontalAlignment: Text.AlignRight }
            }
            RowLayout { Layout.fillWidth: true
 spacing: 12
                Text { text: "OUTPUT"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true
 Layout.preferredWidth: 48 }
                InstrumentMeter { Layout.fillWidth: true
 value: Number(axisModule.info.virtualValue)
 offline: !backend.vjoyReady
 valid: axisModule.info.virtualValid
 tone: "#b7d7bf" }
                Text { text: root.outputState(axisModule.info)
 color: backend.vjoyReady && axisModule.info.virtualValid ? "#b8d9c2" : "#c59a79"
 font.family: "Consolas"
 font.pixelSize: backend.vjoyReady && axisModule.info.virtualValid ? 14 : 9
 font.weight: Font.DemiBold
 Layout.preferredWidth: 72
 horizontalAlignment: Text.AlignRight }
            }
            Item { Layout.preferredHeight: 1 }
            RowLayout { Layout.fillWidth: true
 spacing: 10
                Text { text: "ROUTE"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true }
                FlightComboBox {
                    id: axisDestination
                    Layout.preferredWidth: 88
 Layout.preferredHeight: 30
                    model: root.outputChoices
                    currentIndex: Math.max(0, root.outputChoices.indexOf(axisModule.info.target))
                    onActivated: {
                        if (!backend.setMapping(axisModule.info.index, currentText, false)) {
                            root.conflictingAxis = axisModule.info.index
                            root.conflictingTarget = currentText
                            currentIndex = root.outputChoices.indexOf(axisModule.info.target)
                            axisConflictDialog.open()
                        }
                    }
                    background: Rectangle { radius: 5
 color: "#0c1013"
 border.color: "#435660" }
                    contentItem: Text { leftPadding: 9
 text: axisDestination.displayText
 color: "#dce4e4"
 verticalAlignment: Text.AlignVCenter
 font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
                Text { text: "INVERT"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true }
                Switch {
                    id: invertControl
                    checked: axisModule.info.inverted
                    onToggled: backend.setAxisInverted(axisModule.info.index, checked)
                    indicator: Rectangle { implicitWidth: 32
 implicitHeight: 16
 radius: 8
 color: invertControl.checked ? "#5a737c" : "#363e43"
 border.color: "#3f525a"
                        Rectangle { width: 12
 height: 12
 radius: 6
 x: invertControl.checked ? 17 : 3
 anchors.verticalCenter: parent.verticalCenter
 color: "#e8edec" }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true
                Text { text: "DEADZONE  " + Math.round(Number(axisModule.info.deadzone) * 100) + "%"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true
 Layout.preferredWidth: 90 }
                Slider {
                    id: deadzoneControl
                    Layout.fillWidth: true
 from: 0
 to: 0.25
 value: Number(axisModule.info.deadzone)
                    onMoved: backend.setAxisDeadzone(axisModule.info.index, value)
                    background: Rectangle { x: deadzoneControl.leftPadding
 y: deadzoneControl.topPadding + deadzoneControl.availableHeight / 2 - height / 2
 width: deadzoneControl.availableWidth
 height: 4
 radius: 2
 color: "#0c0f12"
                        Rectangle { width: deadzoneControl.visualPosition * parent.width
 height: parent.height
 radius: 2
 color: "#829da5" }
                    }
                    handle: Rectangle { x: deadzoneControl.leftPadding + deadzoneControl.visualPosition * (deadzoneControl.availableWidth - width)
 y: deadzoneControl.topPadding + deadzoneControl.availableHeight / 2 - height / 2
 width: 12
 height: 12
 radius: 6
 color: "#e1e7e5"
 border.color: "#111518" }
                }
            }
        }
    }
    component CurveViewer: Panel {
        id: curveViewer
        property var info: null
        property var samples: backend.selectedAxisCurve
        Layout.fillWidth: true
        Layout.preferredHeight: 356
        color: "#eb11171b"
        border.color: "#3b66747d"
        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            RowLayout { width: parent.width
                Text { text: "LIVE TRANSFER"; color: "#dce9eb"; font.pixelSize: 11; font.bold: true }
                Text { text: curveViewer.info && curveViewer.info.unipolar ? "0–100% THROTTLE DOMAIN" : "−100% TO +100% NORMALIZED DOMAIN"
                    color: "#758f99"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Row { spacing: 12
                    Text { text: "— INPUT"; color: "#c5d0d3"; font.pixelSize: 9; font.bold: true }
                    Text { text: "— OUTPUT"; color: "#88bec8"; font.pixelSize: 9; font.bold: true }
                    Text { text: "○ PHYSICAL"; color: "#d7e5e7"; font.pixelSize: 9; font.bold: true }
                    Text { text: "● TRANSFORMED"; color: "#91c8c0"; font.pixelSize: 9; font.bold: true }
                }
            }
            Canvas {
                id: curveCanvas
                width: parent.width
                height: parent.height - 31
                antialiasing: true
                renderTarget: Canvas.Image
                property var curveSamples: curveViewer.samples
                property real physicalInput: curveViewer.info ? Number(curveViewer.info.raw) : 0
                property real transformedOutput: curveViewer.info ? Number(curveViewer.info.transformed) : 0
                function xFor(value, left, plotWidth) { return left + ((value + 1) * 0.5) * plotWidth }
                function yFor(value, top, plotHeight) { return top + (1 - ((value + 1) * 0.5)) * plotHeight }
                onCurveSamplesChanged: requestPaint()
                onPhysicalInputChanged: requestPaint()
                onTransformedOutputChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const left = 38, right = 12, top = 10, bottom = 25
                    const plotWidth = Math.max(1, width - left - right)
                    const plotHeight = Math.max(1, height - top - bottom)
                    ctx.fillStyle = "#0a0f12"
                    ctx.fillRect(left, top, plotWidth, plotHeight)
                    ctx.strokeStyle = "#25465357"
                    ctx.lineWidth = 1
                    for (let tick = 0; tick <= 4; ++tick) {
                        const x = left + plotWidth * tick / 4
                        const y = top + plotHeight * tick / 4
                        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + plotHeight); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(left + plotWidth, y); ctx.stroke()
                    }
                    ctx.strokeStyle = "#5677848c"
                    ctx.beginPath()
                    ctx.moveTo(left, yFor(0, top, plotHeight))
                    ctx.lineTo(left + plotWidth, yFor(0, top, plotHeight))
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.moveTo(xFor(0, left, plotWidth), top)
                    ctx.lineTo(xFor(0, left, plotWidth), top + plotHeight)
                    ctx.stroke()
                    const trace = function(key, color, widthValue) {
                        if (!curveSamples || curveSamples.length === 0) return
                        ctx.strokeStyle = color
                        ctx.lineWidth = widthValue
                        ctx.beginPath()
                        for (let index = 0; index < curveSamples.length; ++index) {
                            const point = curveSamples[index]
                            const x = xFor(Number(point.input), left, plotWidth)
                            const y = yFor(key === "input" ? Number(point.input) : Number(point.output), top, plotHeight)
                            if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                    trace("input", "#bac8cb", 1.25)
                    trace("output", "#69aeb8", 2.0)
                    if (curveViewer.info && Number(curveViewer.info.hysteresis) > 0) {
                        const halfBand = Number(curveViewer.info.hysteresis)
                        const x0 = xFor(Math.max(-1, physicalInput - halfBand), left, plotWidth)
                        const x1 = xFor(Math.min(1, physicalInput + halfBand), left, plotWidth)
                        ctx.fillStyle = "#377da38c"
                        ctx.fillRect(x0, top, Math.max(1, x1 - x0), plotHeight)
                    }
                    const inputX = xFor(physicalInput, left, plotWidth)
                    const inputY = yFor(physicalInput, top, plotHeight)
                    const outputY = yFor(transformedOutput, top, plotHeight)
                    ctx.fillStyle = "#dbe7e8"
                    ctx.strokeStyle = "#6d8790"
                    ctx.lineWidth = 2
                    ctx.beginPath(); ctx.arc(inputX, inputY, 5, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    ctx.fillStyle = "#8fc8c0"
                    ctx.strokeStyle = "#e2f0ee"
                    ctx.beginPath(); ctx.arc(inputX, outputY, 4, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    ctx.fillStyle = "#76909a"
                    ctx.font = "10px Consolas"
                    const labels = curveViewer.info && curveViewer.info.unipolar ? ["0", "25", "50", "75", "100"] : ["-100", "-50", "0", "+50", "+100"]
                    for (let labelIndex = 0; labelIndex < labels.length; ++labelIndex) {
                        ctx.fillText(labels[labelIndex], left + plotWidth * labelIndex / 4 - 10, height - 7)
                        ctx.fillText(labels[4 - labelIndex], 2, top + plotHeight * labelIndex / 4 + 3)
                    }
                }
                Connections {
                    target: backend
                    function onStateChanged() { curveCanvas.requestPaint() }
                }
            }
        }
    }
    component ButtonCard: Panel {
        id: buttonCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 258
        color: info && (info.pressed || info.profileControlActive) ? "#ec263e48" : "#ed182128"
        border.color: info && info.profileControlActive ? "#9dcdb0" : (info && info.pressed ? "#93a3cfda" : "#43546770")
        function triggerChoiceIndex(targetId) {
            for (let index = 0; index < root.profileTriggerChoices.length; ++index) {
                if (root.profileTriggerChoices[index].id === targetId) return index
            }
            return 0
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 13
            spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: "BUTTON " + ("0" + buttonCard.info.index).slice(-2)
                    color: "#edf7f7"; font.pixelSize: 13; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Row { spacing: 5
                    StatusDot { tone: buttonCard.info.pressed ? "#a8d9e6" : "#68747a" }
                    Text { text: buttonCard.info.pressed ? "PRESSED" : "RELEASED"
                        color: buttonCard.info.pressed ? "#d6f0f4" : "#919ca0"; font.pixelSize: 9; font.bold: true }
                }
            }
            Text { text: "PHYSICAL   " + (buttonCard.info.pressed ? "DOWN" : "UP")
                color: buttonCard.info.pressed ? "#c4e4e9" : "#849398"; font.pixelSize: 10
                font.family: "Consolas"; font.bold: buttonCard.info.pressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "GAME OUTPUT"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                FlightComboBox {
                    id: buttonDestination
                    Layout.preferredWidth: 126
                    Layout.preferredHeight: 29
                    model: root.buttonOutputChoices
                    currentIndex: buttonCard.info.target
                    onActivated: {
                        if (!backend.setButtonMapping(buttonCard.info.index, currentIndex, false)) {
                            root.conflictingButton = buttonCard.info.index
                            root.conflictingVirtualButton = currentIndex
                            root.conflictingPovHat = 0
                            root.conflictingPovDirection = -1
                            currentIndex = buttonCard.info.target
                            buttonConflictDialog.open()
                        }
                    }
                    background: Rectangle { radius: 5
                        color: "#0c1013"; border.color: "#435660" }
                    contentItem: Text { leftPadding: 8
                        text: buttonDestination.displayText; color: "#dce4e4"
                        verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                }
            }
            Text { text: buttonCard.info.target > 0
                    ? "VIRTUAL    " + (buttonCard.info.virtualPressed ? "DOWN" : "UP")
                    : "VIRTUAL    UNROUTED"
                color: buttonCard.info.virtualPressed ? "#b9dcc2" : "#819297"
                font.pixelSize: 9; font.family: "Consolas"; font.bold: buttonCard.info.virtualPressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "PROFILE CONTROL"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                FlightComboBox {
                    id: profileControlTarget
                    Layout.preferredWidth: 146; Layout.preferredHeight: 29
                    model: root.profileTriggerChoices; textRole: "label"; valueRole: "id"
                    currentIndex: buttonCard.triggerChoiceIndex(buttonCard.info.profileControlTargetId)
                    onActivated: backend.setProfileTrigger(buttonCard.info.index, currentValue,
                        buttonCard.info.profileControlEnabled ? buttonCard.info.profileControlMode : "Hold")
                    background: Rectangle { radius: 5; color: "#0c1013"; border.color: "#435660" }
                    contentItem: Text { leftPadding: 8; text: profileControlTarget.displayText; color: "#dce4e4"
                        verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                }
            }
            ColumnLayout { Layout.fillWidth: true; visible: buttonCard.info.profileControlEnabled; spacing: 6
                RowLayout { Layout.fillWidth: true
                    Text { text: "TARGET PROFILE"; color: "#82949a"; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text { text: buttonCard.info.profileControlTargetName.toUpperCase()
                        color: buttonCard.info.profileControlTargetAvailable ? "#b8d8dc" : "#d49b62"
                        font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.maximumWidth: 150 }
                }
            }
            RowLayout { Layout.fillWidth: true
                Text { text: "MODE"; color: "#82949a"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Rectangle {
                    id: profileControlMode
                    Layout.preferredWidth: 150; Layout.preferredHeight: 28
                    radius: 5
                    color: "#0c1013"
                    border.color: buttonCard.info.profileControlEnabled ? "#435660" : "#263137"
                    opacity: buttonCard.info.profileControlEnabled ? 1.0 : 0.55
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: ["Hold", "Toggle"]
                            delegate: Rectangle {
                                width: profileControlMode.width / 2
                                height: profileControlMode.height
                                radius: 4
                                color: buttonCard.info.profileControlEnabled
                                    && buttonCard.info.profileControlMode === modelData ? "#37626a" : "transparent"
                                border.color: buttonCard.info.profileControlEnabled
                                    && buttonCard.info.profileControlMode === modelData ? "#78aab9" : "transparent"
                                Text { anchors.centerIn: parent; text: modelData.toUpperCase()
                                    color: buttonCard.info.profileControlEnabled ? "#dce7e6" : "#7b8589"
                                    font.pixelSize: 9; font.bold: true }
                                MouseArea {
                                    id: modeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: buttonCard.info.profileControlEnabled
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: backend.setProfileTrigger(buttonCard.info.index,
                                        buttonCard.info.profileControlTargetId, modelData)
                                }
                                ToolTip.visible: modeMouse.containsMouse
                                ToolTip.text: modelData === "Hold"
                                    ? "Target profile is active only while this button is held."
                                    : "Press once to activate; press again to release."
                            }
                        }
                    }
                }
            }
            Text { visible: buttonCard.info.profileControlEnabled
                text: buttonCard.info.profileControlActive ? "● PROFILE CONTROL ACTIVE · "
                    + buttonCard.info.profileControlMode.toUpperCase() : "Profile control consumes this button."
                color: buttonCard.info.profileControlActive ? "#a8d9b4" : "#819297"
                font.pixelSize: 9; font.family: "Consolas"; font.bold: buttonCard.info.profileControlActive }
        }
    }

    component PovNativeCard: Panel {
        id: nativeCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 143
        color: info && info.nativeEnabled ? "#e51a352f" : "#ed182128"
        border.color: info && info.nativeEnabled ? "#5f9a9f" : "#43546770"
        function targetIndex(key) {
            for (let index = 0; index < root.nativePovTargetChoices.length; ++index) {
                if (root.nativePovTargetChoices[index].key === key) return index
            }
            return 0
        }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 13; spacing: 6
            RowLayout { Layout.fillWidth: true
                Text { text: "POV " + nativeCard.info.index + " / HAT"; color: "#edf7f7"; font.pixelSize: 12; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: nativeCard.info.nativeStatus; color: nativeCard.info.nativeAvailable ? "#98d1bd" : "#d4ad69"; font.pixelSize: 9; font.bold: true }
            }
            Text { text: nativeCard.info.centered ? "LIVE STATE   CENTERED" : "LIVE STATE   " + nativeCard.info.direction.toUpperCase() + " · " + nativeCard.info.angle + "°"
                color: nativeCard.info.centered ? "#89969b" : "#c4e4e9"; font.pixelSize: 10; font.family: "Consolas" }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "NATIVE vJOY OUTPUT"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Rectangle { id: nativeToggle; Layout.preferredWidth: 43; Layout.preferredHeight: 21; radius: 11
                    color: nativeCard.info.nativeEnabled ? "#466e78" : "#273136"; border.color: nativeCard.info.nativeEnabled ? "#8fc3c7" : "#4a5a60"
                    Rectangle { width: 15; height: 15; radius: 8; anchors.verticalCenter: parent.verticalCenter
                        x: nativeCard.info.nativeEnabled ? parent.width - width - 3 : 3; color: "#e3eeee" }
                    MouseArea { anchors.fill: parent; hoverEnabled: true
                        enabled: nativeCard.info.nativeEnabled || root.nativePovTargetChoices.length > 0
                        onClicked: backend.setNativePovOutput(nativeCard.info.index, !nativeCard.info.nativeEnabled,
                            nativeTarget.currentValue) }
                }
            }
            FlightComboBox { id: nativeTarget; Layout.fillWidth: true; Layout.preferredHeight: 29
                model: root.nativePovTargetChoices; textRole: "label"; valueRole: "key"
                enabled: root.nativePovTargetChoices.length > 0
                currentIndex: nativeCard.targetIndex(nativeCard.info.nativeTargetKey)
                onActivated: backend.setNativePovOutput(nativeCard.info.index, nativeCard.info.nativeEnabled, currentValue) }
            Text { visible: root.nativePovTargetChoices.length === 0
                text: "Unavailable · selected vJoy device exposes no POV target."
                color: "#c69a72"; font.pixelSize: 9; font.family: "Consolas" }
        }
    }

    component PovCard: Panel {
        id: povCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 246
        color: info && (info.active || info.profileControlActive) ? "#ec263e48" : "#ed182128"
        border.color: info && info.profileControlActive ? "#9dcdb0" : (info && info.active ? "#93a3cfda" : "#43546770")
        function triggerChoiceIndex(targetId) {
            for (let index = 0; index < root.profileTriggerChoices.length; ++index) {
                if (root.profileTriggerChoices[index].id === targetId) return index
            }
            return 0
        }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 13; spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: "POV " + povCard.info.hat + " — " + povCard.info.label.toUpperCase(); color: "#edf7f7"; font.pixelSize: 12; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: povCard.info.active ? "ACTIVE" : "IDLE"; color: povCard.info.active ? "#d6f0f4" : "#919ca0"; font.pixelSize: 9; font.bold: true }
            }
            RowLayout { Layout.fillWidth: true
                Text { text: "GAME OUTPUT"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                FlightComboBox { id: povDestination; Layout.preferredWidth: 142
                    model: root.buttonOutputChoices; currentIndex: povCard.info.target
                    onActivated: {
                        if (!backend.setPovMapping(povCard.info.hat, povCard.info.direction, currentIndex, false)) {
                            root.conflictingButton = -1; root.conflictingVirtualButton = currentIndex
                            root.conflictingPovHat = povCard.info.hat; root.conflictingPovDirection = povCard.info.direction
                            currentIndex = povCard.info.target; buttonConflictDialog.open()
                        }
                    }
                }
            }
            Text { text: povCard.info.target > 0 ? "VIRTUAL    " + (povCard.info.virtualPressed ? "DOWN" : "UP") : "VIRTUAL    UNROUTED"
                color: povCard.info.virtualPressed ? "#b9dcc2" : "#819297"; font.pixelSize: 9; font.family: "Consolas"; font.bold: povCard.info.virtualPressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "PROFILE CONTROL"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                FlightComboBox { id: povProfileTarget; Layout.preferredWidth: 150
                    model: root.profileTriggerChoices; textRole: "label"; valueRole: "id"
                    currentIndex: povCard.triggerChoiceIndex(povCard.info.profileControlTargetId)
                    onActivated: backend.setPovProfileTrigger(povCard.info.hat, povCard.info.direction, currentValue,
                        povCard.info.profileControlEnabled ? povCard.info.profileControlMode : "Hold") }
            }
            RowLayout { Layout.fillWidth: true
                Text { text: povCard.info.profileControlEnabled ? povCard.info.profileControlTargetName.toUpperCase() : "NONE"
                    color: povCard.info.profileControlTargetAvailable || !povCard.info.profileControlEnabled ? "#b8d8dc" : "#d49b62"
                    font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Rectangle { id: povProfileMode; Layout.preferredWidth: 126; Layout.preferredHeight: 26; radius: 4
                    color: "#0c1013"; border.color: povCard.info.profileControlEnabled ? "#435660" : "#263137"; opacity: povCard.info.profileControlEnabled ? 1 : 0.55
                    Row { anchors.fill: parent
                        Repeater { model: ["Hold", "Toggle"]
                            delegate: Rectangle { width: povProfileMode.width / 2; height: povProfileMode.height; radius: 3
                                color: povCard.info.profileControlEnabled && povCard.info.profileControlMode === modelData ? "#37626a" : "transparent"
                                Text { anchors.centerIn: parent; text: modelData.toUpperCase(); color: povCard.info.profileControlEnabled ? "#dce7e6" : "#7b8589"; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; enabled: povCard.info.profileControlEnabled
                                    onClicked: backend.setPovProfileTrigger(povCard.info.hat, povCard.info.direction, povCard.info.profileControlTargetId, modelData) }
                            }
                        }
                    }
                }
            }
            Text { visible: povCard.info.profileControlEnabled
                text: povCard.info.profileControlActive ? "● PROFILE CONTROL ACTIVE · " + povCard.info.profileControlMode.toUpperCase() : "Profile control consumes this direction route."
                color: povCard.info.profileControlActive ? "#a8d9b4" : "#819297"; font.pixelSize: 9; font.family: "Consolas"; font.bold: povCard.info.profileControlActive }
        }
    }

    background: Rectangle {
        color: "#0d1013"
        gradient: Gradient { GradientStop { position: 0.0
 color: "#151a1e" }
 GradientStop { position: 0.55
 color: "#0d1013" }
 GradientStop { position: 1.0
 color: "#0b0e10" } }
        Rectangle { width: parent.width
 height: 1
 color: "#556a7479"
 anchors.top: parent.top }
        Rectangle { width: parent.width * 0.58
 height: 260
 x: -100
 y: parent.height - 100
 radius: 180
 color: "#071e2930" }
        Repeater { model: 18
            delegate: Rectangle { width: 1; height: parent.height; x: (index + 1) * parent.width / 19
                color: "#163f5261" }
        }
        Repeater { model: 12
            delegate: Rectangle { height: 1; width: parent.width; y: (index + 1) * parent.height / 13
                color: "#123f5261" }
        }
    }

    header: Rectangle {
        id: headerBar
        height: 58
        color: "#f114191d"
        border.color: "#1e3a444b"
        border.width: 1
        RowLayout {
            anchors.fill: parent
 anchors.leftMargin: 18
 anchors.rightMargin: 20
 spacing: 12
            ToolButton {
                id: menuButton
                Layout.preferredWidth: 30
 Layout.preferredHeight: 30
                text: root.menuOpen ? "×" : "☰"
                font.pixelSize: 19
                onClicked: root.menuOpen = !root.menuOpen
                background: Rectangle { radius: 5
 color: menuButton.hovered || root.menuOpen ? "#303b40" : "#20282d"
 border.color: "#334752" }
                contentItem: Text { text: menuButton.text
 color: "#d8e1e0"
 horizontalAlignment: Text.AlignHCenter
 verticalAlignment: Text.AlignVCenter }
            }
            RowLayout { spacing: 8
                Image {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    source: "qrc:/assets/icons/png/hotas-bf6-256.png"
                    sourceSize.width: 68
                    sourceSize.height: 68
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                ColumnLayout { spacing: 0
                    Text { text: "HOTAS BF6"
     color: "#f1f3f1"
     font.pixelSize: 15
     font.bold: true }
                    Text { text: "FLIGHT CONTROL INTERFACE"
     color: "#8d989d"
     font.pixelSize: 9
     font.bold: true }
                }
            }
            FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 24 }
            Row { spacing: 7
                StatusDot { tone: root.physicalStatusColor() }
                Text { text: backend.physicalConnected ? backend.deviceName : "Controller not connected"
 color: "#c3cecf"
 font.pixelSize: 10
 font.bold: true
 elide: Text.ElideRight
 width: Math.min(240, implicitWidth) }
                Text { text: backend.physicalConnected ? root.physicalStatusText() : "waiting"
 color: root.physicalStatusColor()
 font.pixelSize: 10
 font.bold: true }
            }
            FineLine { visible: root.width >= 1100; Layout.preferredWidth: 1
                Layout.preferredHeight: 24 }
            Row { visible: root.width >= 1100; spacing: 7
                StatusDot { tone: backend.vjoyReady ? root.capacityColor() : "#a5afb3" }
                Text { text: "VJOY " + backend.vjoyDeviceId
                    color: "#c3d2d5"; font.pixelSize: 10; font.bold: true }
                Text { text: backend.vjoyReady ? root.capacityState() : "OFFLINE"
                    color: backend.vjoyReady ? root.capacityColor() : "#a5afb3"; font.pixelSize: 10; font.bold: true }
            }
            FineLine { visible: root.width >= 1250 || backend.profileSourceLabel !== "Manual base profile"; Layout.preferredWidth: 1
                Layout.preferredHeight: 24 }
            Row { visible: root.width >= 1250 || backend.profileSourceLabel !== "Manual base profile"; spacing: 6
                Text { text: "PROFILE"
                    color: "#78919a"; font.pixelSize: 9; font.bold: true }
                Text { text: backend.effectiveProfileName.toUpperCase()
                    color: "#c3d8d9"; font.pixelSize: 10; font.bold: true
                    elide: Text.ElideRight; width: Math.min(128, implicitWidth) }
                Text { visible: backend.profileSourceLabel !== "Manual base profile"
                    text: "· " + backend.profileSourceLabel.toUpperCase()
                    color: "#9ac7b1"; font.pixelSize: 9; font.bold: true }
            }
            Item { Layout.fillWidth: true }
            Row { spacing: 7
                StatusDot { tone: backend.mappingActive ? "#91c4a4" : (backend.mappingRequested ? "#d6bd78" : (backend.vjoyReady ? "#91bcc8" : "#a5afb3")) }
                Text { text: backend.mappingActive ? "MAPPING ACTIVE" : (backend.mappingRequested ? "MAPPING PAUSED · RECONNECTING" : (backend.vjoyReady ? "OUTPUT READY" : "MAPPING STOPPED"))
 color: backend.mappingActive ? "#c0d8c6" : (backend.mappingRequested ? "#e1c887" : "#b5c0c1")
 font.pixelSize: 10
 font.bold: true }
            }
        }
    }

    MouseArea { anchors.fill: parent
 z: 40
 visible: root.menuOpen
 onClicked: root.menuOpen = false }
    Panel {
        id: navigationOverlay
        z: 50
        x: 12
 y: headerBar.height + 10
        width: 248
        height: 346
        opacity: root.menuOpen ? 1 : 0
        scale: root.menuOpen ? 1 : 0.97
        visible: root.menuOpen
        color: "#fb1a2025"
        border.color: "#48515d65"
        Behavior on opacity { NumberAnimation { duration: 130
 easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 130
 easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent
 onClicked: function(mouse) { mouse.accepted = true } }
        Column {
            anchors.fill: parent
 anchors.margins: 8
 spacing: 2
            Row {
                x: 7
                width: parent.width - 14
                height: 48
                spacing: 9
                Image {
                    width: 44
                    height: 44
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/assets/icons/png/hotas-bf6-256.png"
                    sourceSize.width: 88
                    sourceSize.height: 88
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text { text: "HOTAS BF6"; color: "#edf5f4"; font.pixelSize: 13; font.bold: true }
                    Text { text: "v" + Qt.application.version; color: "#8fa6ad"; font.pixelSize: 9; font.bold: true }
                }
            }
            Rectangle {
                x: 7
                width: parent.width - 14
                height: 1
                color: "#214d5964"
            }
            Repeater {
                model: [
                    { label: "AXES", page: 0, future: false }, { label: "BUTTONS", page: 1, future: false },
                    { label: "PROFILES", page: 5, future: false }, { label: "CALIBRATION", page: 2, future: false }, { label: "DIAGNOSTICS", page: 3, future: false },
                    { label: "SETTINGS", page: 4, future: false }, { label: "", page: -1, future: false },
                    { label: "CURVE EDITOR", page: 6, future: false }
                ]
                delegate: Item {
                    width: parent.width
 height: modelData.page < 0 ? 12 : 35
                    Rectangle { visible: modelData.page < 0
 anchors.verticalCenter: parent.verticalCenter
 width: parent.width - 14
 x: 7
 height: 1
 color: "#18ffffff" }
                    Rectangle { visible: modelData.page >= 0
 anchors.fill: parent
 radius: 5
 color: root.currentPage === modelData.page ? "#3a4b565c" : navHit.containsMouse ? "#17313a40" : "transparent" }
                    Row { visible: modelData.page >= 0
 anchors.verticalCenter: parent.verticalCenter
 anchors.left: parent.left
 anchors.leftMargin: 11
 spacing: 8
                        Rectangle { width: 4
 height: 4
 radius: 2
 anchors.verticalCenter: parent.verticalCenter
 color: root.currentPage === modelData.page ? "#cfdbda" : "#718087" }
                        Text { text: modelData.label
 color: modelData.future ? "#7f8a8e" : (root.currentPage === modelData.page ? "#f0f3f1" : "#bdc7c8")
 font.pixelSize: 11
 font.bold: root.currentPage === modelData.page }
                        Text { visible: modelData.future
 text: "FUTURE"
 color: "#7d878b"
 font.pixelSize: 8
 font.bold: true }
                    }
                    MouseArea { id: navHit
 anchors.fill: parent
 hoverEnabled: true
 enabled: modelData.page >= 0
 onClicked: { root.currentPage = modelData.page
 root.menuOpen = false } }
                }
            }
        }
    }

    Item {
        id: pageHost
        anchors.fill: parent
 anchors.margins: 24
        Flickable {
            id: axesPage
            anchors.fill: parent
            visible: root.currentPage === 0
            contentWidth: width
            contentHeight: axesContent.implicitHeight + 18
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: axesContent
                x: 1
                width: axesPage.width - 10
                spacing: 14
                RowLayout { width: parent.width
                    PageTitle { heading: "Axes"
                        detail: "One selected physical axis; all configured axes continue mapping · Profile: " + backend.activeProfileName }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: backend.mappingRequested ? "STOP MAPPING" : "START MAPPING"
                        subdued: !backend.mappingRequested
                        onTriggered: backend.toggleMapping() }
                }
                Panel { width: parent.width; height: 90
                    color: "#e51a2328"; border.color: "#46657980"
                    RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 16
                        Column { spacing: 4; Layout.preferredWidth: 96
                            Text { text: "SELECT AXIS"; color: "#89a4ad"; font.pixelSize: 10; font.bold: true }
                            Text { text: root.hasPhysicalInput ? backend.axisCount + " DETECTED" : "WAITING"; color: "#77919a"; font.pixelSize: 9; font.bold: true }
                        }
                        FlightComboBox {
                            id: axisSelector
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            enabled: root.hasPhysicalInput
                            model: root.axisSelectorModel()
                            textRole: "display"
                            valueRole: "axisIndex"
                            currentIndex: {
                                for (let index = 0; index < model.length; ++index) {
                                    if (model[index].axisIndex === backend.selectedAxisIndex) return index
                                }
                                return 0
                            }
                            onActivated: backend.setSelectedAxis(currentValue)
                            background: Rectangle { radius: 3; color: "#0d1216"; border.color: "#546d78" }
                            contentItem: Text { leftPadding: 12; text: axisSelector.displayText
                                color: "#dde9e9"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.bold: true }
                        }
                    }
                }
                OfflineMapper { width: parent.width; visible: !root.hasPhysicalInput }
                Item {
                    width: parent.width
                    height: axesWorkspace.implicitHeight
                    visible: root.hasPhysicalInput && root.selectedAxisInfo
                    GridLayout {
                        id: axesWorkspace
                        width: parent.width
                        columns: width >= 1100 ? 2 : 1
                        columnSpacing: 14
                        rowSpacing: 14
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 14
                            Panel { id: axisIdentityPanel; Layout.fillWidth: true; Layout.preferredHeight: 144
                                property var info: root.selectedAxisInfo
                                color: "#e61a282e"; border.color: "#4b70818a"
                                ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 7
                                    RowLayout { Layout.fillWidth: true
                                        Column { spacing: 2
                                            Text { text: axisIdentityPanel.info.label.toUpperCase(); color: "#edf6f6"; font.pixelSize: 17; font.bold: true }
                                            Text { text: axisIdentityPanel.info.detail.toUpperCase(); color: "#8ca6ae"; font.pixelSize: 10; font.bold: true }
                                        }
                                        Item { Layout.fillWidth: true }
                                        Row { spacing: 6
                                            StatusDot { tone: root.physicalStatusColor() }
                                            Text { text: root.physicalStatusText(); color: root.physicalStatusColor(); font.pixelSize: 10; font.bold: true }
                                        }
                                    }
                                    FineLine { Layout.fillWidth: true }
                                    RowLayout { Layout.fillWidth: true
                                        TelemetryItem { caption: "PROFILE"; value: backend.activeProfileName.toUpperCase(); tone: "#c6dce0"; Layout.fillWidth: true }
                                        TelemetryItem { caption: "ROUTE"; value: axisIdentityPanel.info.target.toUpperCase(); tone: axisIdentityPanel.info.target === "Disabled" ? "#939da1" : "#9bcbd1"; Layout.fillWidth: true }
                                        TelemetryItem { caption: "STATUS"; value: backend.mappingActive ? "● LIVE" : "STANDBY"; tone: backend.mappingActive ? "#a1cbbb" : "#a5afb3"; Layout.fillWidth: true }
                                    }
                                }
                            }
                            Panel { id: liveTelemetryPanel; Layout.fillWidth: true; Layout.preferredHeight: 122
                                property var info: root.selectedAxisInfo
                                RowLayout { anchors.fill: parent; anchors.margins: 17; spacing: 20
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Text { text: "PHYSICAL INPUT"; color: "#89a2ab"; font.pixelSize: 10; font.bold: true }
                                        Text { text: root.controlValue(liveTelemetryPanel.info, liveTelemetryPanel.info.raw); color: "#dce8ea"; font.pixelSize: 31; font.family: "Consolas"; font.bold: true }
                                        Text { text: liveTelemetryPanel.info.detail.toUpperCase(); color: "#718a93"; font.pixelSize: 9; font.bold: true }
                                    }
                                    FineLine { Layout.preferredWidth: 1; Layout.preferredHeight: 64 }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Text { text: "VIRTUAL OUTPUT"; color: "#85aaa8"; font.pixelSize: 10; font.bold: true }
                                        Text { text: root.controlValue(liveTelemetryPanel.info, liveTelemetryPanel.info.transformed); color: "#9ad0c5"; font.pixelSize: 31; font.family: "Consolas"; font.bold: true }
                                        Text { text: backend.mappingActive ? "LIVE PROCESSED COMMAND" : "LIVE COMMAND PREVIEW"; color: "#718f8b"; font.pixelSize: 9; font.bold: true }
                                    }
                                }
                            }
                            CurveViewer { info: root.selectedAxisInfo }
                        }
                        Panel {
                            id: processingPanel
                            property var info: root.selectedAxisInfo
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            // Processing controls grow with their content so the
                            // Curve description and action remain inside the panel.
                            Layout.preferredHeight: processingContent.implicitHeight + 32
                            color: "#ed151d22"
                            ColumnLayout { id: processingContent; anchors.fill: parent; anchors.margins: 16; spacing: 12
                                Text { text: "AXIS PROCESSING"; color: "#e1eded"; font.pixelSize: 12; font.bold: true }
                                Text { text: "Profile-specific settings compile into the worker between reports."; color: "#8199a1"; font.pixelSize: 9; font.bold: true }
                                FineLine { Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "ROUTE"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightComboBox {
                                        id: selectedAxisDestination
                                        Layout.fillWidth: true; Layout.preferredHeight: 31
                                        model: root.outputChoices
                                        currentIndex: Math.max(0, root.outputChoices.indexOf(processingPanel.info.target))
                                        onActivated: {
                                            if (!backend.setMapping(processingPanel.info.index, currentText, false)) {
                                                root.conflictingAxis = processingPanel.info.index
                                                root.conflictingTarget = currentText
                                                currentIndex = root.outputChoices.indexOf(processingPanel.info.target)
                                                axisConflictDialog.open()
                                            }
                                        }
                                        background: Rectangle { radius: 3; color: "#0d1215"; border.color: "#455d67" }
                                        contentItem: Text { leftPadding: 9; text: selectedAxisDestination.displayText; color: "#dce7e8"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
                                    }
                                }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "INVERT"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    Switch {
                                        id: selectedAxisInvert
                                        checked: processingPanel.info.inverted
                                        onToggled: backend.setAxisInverted(processingPanel.info.index, checked)
                                        indicator: Rectangle { implicitWidth: 36; implicitHeight: 18; radius: 9
                                            color: selectedAxisInvert.checked ? "#526e77" : "#343d42"; border.color: "#435963"
                                            Rectangle { width: 14; height: 14; radius: 7; x: selectedAxisInvert.checked ? 19 : 3
                                                anchors.verticalCenter: parent.verticalCenter; color: "#e5ecec" }
                                        }
                                    }
                                    Text { text: processingPanel.info.inverted ? "ON" : "OFF"; color: "#99b6bb"; font.pixelSize: 10; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                }
                                FineLine { Layout.fillWidth: true }
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: "DEADZONE"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (Number(processingPanel.info.deadzone) * 100).toFixed(1) + "%"; color: "#c8dce0"; font.pixelSize: 11; font.family: "Consolas"; font.bold: true }
                                    }
                                    Slider { id: selectedAxisDeadzone; Layout.fillWidth: true; from: 0; to: 0.25; value: Number(processingPanel.info.deadzone)
                                        onMoved: backend.setAxisDeadzone(processingPanel.info.index, value) }
                                    Text { text: "Rescaled around center before hysteresis."; color: "#718a93"; font.pixelSize: 9 }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: "HYSTERESIS"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (Number(processingPanel.info.hysteresis) * 100).toFixed(2) + "%"; color: "#c8dce0"; font.pixelSize: 11; font.family: "Consolas"; font.bold: true }
                                    }
                                    Slider { id: selectedAxisHysteresis; Layout.fillWidth: true; from: 0; to: 0.05; value: Number(processingPanel.info.hysteresis)
                                        onMoved: backend.setAxisHysteresis(processingPanel.info.index, value) }
                                    Text { text: "Suppresses sub-threshold report noise; no smoothing delay."; color: "#718a93"; font.pixelSize: 9 }
                                }
                                FineLine { Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "OUTPUT MIN"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightNumericStepper { id: outputMinimum; from: -100; to: 99
                                        value: Math.round(Number(processingPanel.info.outputMinimum) * 100)
                                        onValueEdited: function(nextValue) { backend.setAxisOutputLimits(processingPanel.info.index, nextValue / 100, Number(processingPanel.info.outputMaximum)) } }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "OUTPUT MAX"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightNumericStepper { id: outputMaximum; from: -99; to: 100
                                        value: Math.round(Number(processingPanel.info.outputMaximum) * 100)
                                        onValueEdited: function(nextValue) { backend.setAxisOutputLimits(processingPanel.info.index, Number(processingPanel.info.outputMinimum), nextValue / 100) } }
                                    Item { Layout.fillWidth: true }
                                }
                                Text { text: "Limits constrain final virtual authority, not physical calibration."; color: "#718a93"; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                FineLine { Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                                        Text { text: "CURVE"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true }
                                        Text { text: processingPanel.info.curveSummary; color: "#d5e2e3"; font.pixelSize: 12; font.bold: true }
                                    }
                                    CommandButton { label: "EDIT CURVE"; subdued: true; onTriggered: root.currentPage = 6 }
                                }
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id: buttonsPage
            anchors.fill: parent
 visible: root.currentPage === 1
 contentWidth: width
 contentHeight: buttonsContent.implicitHeight + 18
 clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: buttonsContent
                x: 1
 width: buttonsPage.width - 10
 spacing: 14
                RowLayout { width: parent.width
                    PageTitle { heading: "Buttons"
 detail: "Physical DirectInput state is visible even while vJoy is offline · Profile: " + backend.activeProfileName }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: "RESET MAPPINGS"
 commandEnabled: backend.buttonCount > 0
 subdued: true
 onTriggered: backend.resetButtonMappings() }
                }
                Panel { width: parent.width
 height: 62
                    RowLayout { anchors.fill: parent
 anchors.leftMargin: 16
 anchors.rightMargin: 16
 spacing: 18
                        TelemetryItem { caption: "PHYSICAL BUTTONS"
 value: backend.buttonCount + " DETECTED"
 tone: backend.physicalConnected ? "#b9d1d8" : "#a5afb3"
 Layout.fillWidth: true }
                        FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 30 }
                        TelemetryItem { caption: "VIRTUAL BUTTONS"
 value: backend.vjoyReady ? backend.vjoyButtonCount + " · " + root.capacityState() : "VJOY OFFLINE"
 tone: backend.vjoyReady ? root.capacityColor() : "#a5afb3"
 Layout.fillWidth: true }
                        FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 30 }
                        TelemetryItem { caption: "LAST INPUT"
 value: backend.lastPhysicalButton > 0 ? "BUTTON " + backend.lastPhysicalButton : "WAITING"
 tone: backend.lastPhysicalButton > 0 ? "#b9d1d8" : "#8f9a9f"
 Layout.fillWidth: true }
                    }
                }
                Text { text: allButtons.length > 0 ? "PHYSICAL BUTTONS" : "NO DIRECTINPUT BUTTONS AVAILABLE"
 color: "#94a1a6"
 font.pixelSize: 10
 font.bold: true }
                GridLayout { width: parent.width
 columns: width >= 1160 ? 3 : (width >= 760 ? 2 : 1)
 columnSpacing: 12
 rowSpacing: 12
                    Repeater { model: root.allButtons
 delegate: ButtonCard { info: modelData } }
                }
                Text { visible: root.allPovs.length > 0; text: "POV / HAT · NATIVE vJOY OUTPUT"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                GridLayout { visible: root.allPovs.length > 0; width: parent.width
                    columns: width >= 1160 ? 3 : (width >= 760 ? 2 : 1)
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater { model: root.allPovs
                        delegate: PovNativeCard { info: modelData } }
                }
                Text { visible: root.allPovInputs.length > 0; text: "POV DIRECTION ROUTES"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                GridLayout { visible: root.allPovInputs.length > 0; width: parent.width
                    columns: width >= 1160 ? 4 : (width >= 760 ? 2 : 1)
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater { model: root.allPovInputs
                        delegate: PovCard { info: modelData } }
                }
            }
        }
        Flickable {
            id: calibrationPage
            anchors.fill: parent
 visible: root.currentPage === 2
 contentWidth: width
 contentHeight: calibrationContent.implicitHeight + 18
 clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: calibrationContent
                x: 1
 width: calibrationPage.width - 10
 spacing: 14
                RowLayout { width: parent.width
                    PageTitle { heading: "Calibration"
 detail: backend.calibrationActive ? "Capture active: move each control through full travel" : "Save custom ranges only after a complete movement pass" }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: backend.calibrationActive ? "CAPTURING" : "START CAPTURE"
 onTriggered: backend.beginCalibration() }
                    CommandButton { label: "SAVE"
 commandEnabled: backend.calibrationActive
 subdued: true
 onTriggered: backend.saveCalibration() }
                    CommandButton { label: "RESET"
 subdued: true
 onTriggered: backend.resetCalibration() }
                }
                Text { text: "AXIS RANGE STATUS"
 color: "#94a1a6"
 font.pixelSize: 10
 font.bold: true }
                GridLayout { width: parent.width
 columns: width >= 980 ? 2 : 1
 columnSpacing: 12
 rowSpacing: 12
                    Repeater { model: 8
                        delegate: Panel {
                            id: calibrationAxisCard
                            property var info: root.axisAt(index)
                            visible: info && info.available
                            Layout.fillWidth: true
                            Layout.preferredHeight: 118
                            color: backend.calibrationActive ? "#ea1a2930" : "#ed182128"
                            border.color: backend.calibrationActive ? "#5a7e9aa8" : "#41546770"
                            RowLayout { anchors.fill: parent
 anchors.margins: 15
                                ColumnLayout { Layout.preferredWidth: 130
                                    Text { text: calibrationAxisCard.info.label.toUpperCase()
 color: "#eaf0f1"
 font.pixelSize: 12
 font.bold: true }
                                    Text { text: backend.calibrationActive ? "CAPTURING LIVE" : (calibrationAxisCard.info.calibrationEnabled ? "SAVED RANGE" : "RAW DEFAULT")
 color: backend.calibrationActive ? "#abd7e2" : (calibrationAxisCard.info.calibrationEnabled ? "#9fc7b1" : "#89979d")
 font.pixelSize: 9 }
                                }
                                Repeater { model: [{ n: "MIN", v: calibrationAxisCard.info.calibrationMinimum }, { n: "CURRENT", v: calibrationAxisCard.info.raw }, { n: "MAX", v: calibrationAxisCard.info.calibrationMaximum }]
                                    delegate: Column { Layout.fillWidth: true
 spacing: 5
                                        Text { text: modelData.n
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                                        Text { text: Number(modelData.v).toFixed(3)
 color: "#c9d6d9"
 font.pixelSize: 13
 font.family: "Consolas" }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id: diagnosticsPage
            anchors.fill: parent
 visible: root.currentPage === 3
 contentWidth: width
 contentHeight: diagnosticsContent.implicitHeight + 18
 clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: diagnosticsContent
                x: 1
 width: diagnosticsPage.width - 10
 spacing: 14
                PageTitle { heading: "Diagnostics"
 detail: "Worker-side DirectInput telemetry; presentation samples the latest snapshot at 60 Hz" }
                GridLayout { width: parent.width
 columns: width >= 1100 ? 5 : (width >= 760 ? 3 : 2)
 columnSpacing: 10
 rowSpacing: 10
                    Repeater { model: [
                        { c: "PHYSICAL RATE", v: backend.inputReportsPerSecond.toFixed(0) + " HZ", t: backend.inputReportsPerSecond > 0 ? "#b9d1d8" : "#a5afb3" },
                        { c: "UPDATE AGE", v: backend.lastPhysicalUpdateAgeMs >= 0 ? backend.lastPhysicalUpdateAgeMs + " MS" : "—", t: backend.lastPhysicalUpdateAgeMs >= 0 && backend.lastPhysicalUpdateAgeMs < 100 ? "#b9d1d8" : "#a5afb3" },
                        { c: "MAP LATENCY", v: backend.latencyCurrentUs + " US", t: "#c9d6d9" },
                        { c: "MAP p95 / p99", v: backend.latencyP95Us + " / " + backend.latencyP99Us + " US", t: "#c9d6d9",
                          note: "ROLLING LAST 2,048 PHYSICAL REPORTS" },
                        { c: "MAP AVG / PEAK", v: backend.latencyAverageUs + " / " + backend.latencyPeakUs + " US", t: "#c9d6d9",
                          note: "LIFETIME SINCE MAPPING START" },
                        { c: "VJOY WRITES", v: backend.vjoyWritesPerSecond.toFixed(0) + " / S", t: backend.vjoyReady ? "#b9d1d8" : "#89979d",
                          note: backend.vjoyWritesPerSecond > 0 ? "ACTIVE · CHANGE-DRIVEN" : "IDLE · CHANGE-DRIVEN" },
                        { c: "MAPPING", v: backend.mappingActive ? "ACTIVE" : (backend.mappingRequested ? "RECONNECTING" : "STOPPED"), t: backend.mappingActive ? "#a8cfba" : (backend.mappingRequested ? "#e1c887" : "#a5afb3"),
                          note: backend.mappingActive ? "OUTPUT ACQUIRED" : (backend.mappingRequested ? "OUTPUT WILL REACQUIRE AUTOMATICALLY" : "PHYSICAL MONITORING CONTINUES") },
                        { c: "BASE PROFILE", v: backend.activeProfileName.toUpperCase(), t: "#b9d1d8",
                          note: "PERSISTENT MANUAL SELECTION" },
                        { c: "EFFECTIVE PROFILE", v: backend.effectiveProfileName.toUpperCase(), t: "#b9d1d8",
                          note: backend.profileSourceLabel.toUpperCase() },
                        { c: "PROFILE SWAP", v: backend.lastProfileSwapUs + " US", t: "#c9d6d9",
                          note: backend.profileSwitchCount + " LIVE SWITCHES" }
                    ]
                        delegate: Panel { Layout.fillWidth: true
 Layout.preferredHeight: 86
                            Column { anchors.fill: parent
 anchors.margins: 13
 spacing: 4
                                Text { text: modelData.c
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                                Text { text: modelData.v
 color: modelData.t
 font.pixelSize: 16
 font.bold: true
 font.family: "Consolas" }
                                Text { visible: modelData.note !== undefined; text: modelData.note || ""
                                    color: "#7895a0"; font.pixelSize: 8; font.bold: true }
                            }
                        }
                    }
                }
                RowLayout { width: parent.width
                    Panel { Layout.fillWidth: true
 Layout.preferredHeight: 122
                        Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 7
                            Text { text: "PHYSICAL DEVICE"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                            Text { text: backend.deviceName
 color: "#e8eeee"
 font.pixelSize: 14
 font.bold: true
 elide: Text.ElideRight
 width: parent.width }
                            Text { text: backend.axisCount + " AXES   ·   " + backend.buttonCount + " BUTTONS   ·   POV " + root.povText()
 color: "#91a0a6"
 font.pixelSize: 10
 font.family: "Consolas"
 width: parent.width }
                            Text { text: root.physicalStatusText() + " · DIRECTINPUT LIVE SNAPSHOT"
 color: root.physicalStatusColor()
 font.pixelSize: 10
 font.bold: true }
                        }
                    }
                    Panel { Layout.fillWidth: true
 Layout.preferredHeight: 122
                        Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 7
                            Text { text: "VIRTUAL DEVICE"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                            Text { text: "vJoy Device " + backend.vjoyDeviceId + " · " + (backend.vjoyReady ? "READY" : "OFFLINE")
 color: backend.vjoyReady ? "#e8eeee" : "#d49b62"
 font.pixelSize: 14
 font.bold: true
 elide: Text.ElideRight
 width: parent.width }
                            Text { text: backend.vjoyReady ? "X / Y / Z / RZ   ·   " + backend.vjoyButtonCount + " BUTTONS" : "Physical monitoring remains independent"
 color: "#91a0a6"; font.pixelSize: 10; font.family: "Consolas" }
                            Text { text: backend.vjoyReady ? root.capacityState() + "   ·   REQUIRED " + backend.vjoyRequiredButtonCount : backend.vjoyStatus
 color: root.capacityColor()
 font.pixelSize: 10
 font.bold: true }
                        }
                    }
                }
                Text { text: "PHYSICAL / VIRTUAL AXIS ROUTES"
 color: "#94a1a6"
 font.pixelSize: 10
 font.bold: true }
                GridLayout { width: parent.width
 columns: width >= 1100 ? 4 : (width >= 760 ? 2 : 1)
 columnSpacing: 10
 rowSpacing: 10
                    Repeater { model: 8
                        delegate: Panel {
                            id: diagnosticAxisCard
                            property var info: root.axisAt(index)
                            visible: info && info.available
                            Layout.fillWidth: true
 Layout.preferredHeight: 116
                            Column { anchors.fill: parent
 anchors.margins: 12
 spacing: 4
                                Text { text: diagnosticAxisCard.info.label.toUpperCase()
 color: "#aebcc0"
 font.pixelSize: 9
 font.bold: true }
                                Text { text: "RAW       " + root.controlValue(diagnosticAxisCard.info, diagnosticAxisCard.info.raw)
 color: "#dfeaec"
 font.pixelSize: 12
 font.family: "Consolas" }
                                Text { text: "PHYSICAL  " + root.controlValue(diagnosticAxisCard.info, diagnosticAxisCard.info.transformed)
 color: "#a9cad2"
 font.pixelSize: 10
 font.family: "Consolas" }
                                Text { text: "OUTPUT    " + root.outputState(diagnosticAxisCard.info)
 color: diagnosticAxisCard.info.virtualValid ? "#b7d7c0" : "#c59a79"
 font.pixelSize: 10
 font.family: "Consolas" }
                                Text { text: "ROUTE     " + diagnosticAxisCard.info.target.toUpperCase()
 color: "#7c97a1"; font.pixelSize: 9; font.family: "Consolas" }
                            }
                        }
                    }
                }
                Text { visible: root.allPovs.length > 0; text: "POV / HAT INPUTS"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                GridLayout { visible: root.allPovs.length > 0; width: parent.width
                    columns: width >= 1100 ? 4 : (width >= 760 ? 2 : 1)
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater { model: root.allPovs
                        delegate: Panel {
                            id: diagnosticPovCard
                            property var info: modelData
                            property var directions: [
                                { label: "UP-L", direction: "Up-Left" }, { label: "UP", direction: "Up" }, { label: "UP-R", direction: "Up-Right" },
                                { label: "LEFT", direction: "Left" }, { label: "·", direction: "" }, { label: "RIGHT", direction: "Right" },
                                { label: "DOWN-L", direction: "Down-Left" }, { label: "DOWN", direction: "Down" }, { label: "DOWN-R", direction: "Down-Right" }
                            ]
                            Layout.fillWidth: true
                            Layout.preferredHeight: 186
                            color: diagnosticPovCard.info.centered ? "#ed182128" : "#ec263e48"
                            border.color: diagnosticPovCard.info.centered ? "#43546770" : "#93a3cfda"
                            Column { anchors.fill: parent; anchors.margins: 12; spacing: 5
                                Text { text: "POV " + diagnosticPovCard.info.index + " / HAT"
                                    color: "#aebcc0"; font.pixelSize: 9; font.bold: true }
                                Text { text: "STATE     " + diagnosticPovCard.info.direction.toUpperCase()
                                    color: diagnosticPovCard.info.centered ? "#9ba8ac" : "#d6f0f4"
                                    font.pixelSize: 12; font.family: "Consolas"; font.bold: true }
                                Text { visible: !diagnosticPovCard.info.centered
                                    text: "ANGLE     " + diagnosticPovCard.info.angle + "°"
                                    color: "#a9cad2"; font.pixelSize: 10; font.family: "Consolas" }
                                Text { text: "RAW       " + (diagnosticPovCard.info.centered ? "—" : diagnosticPovCard.info.raw)
                                    color: "#7c97a1"; font.pixelSize: 10; font.family: "Consolas" }
                                Text { text: "NATIVE    " + diagnosticPovCard.info.nativeStatus
                                        + (diagnosticPovCard.info.nativeEnabled ? " · " + diagnosticPovCard.info.nativeTargetLabel : "")
                                    color: diagnosticPovCard.info.nativeAvailable ? "#9fcfbd" : "#c69a72"
                                    font.pixelSize: 9; font.family: "Consolas" }
                                Grid { columns: 3; columnSpacing: 9; rowSpacing: 3
                                    Repeater { model: diagnosticPovCard.directions
                                        delegate: Text { width: 50; horizontalAlignment: Text.AlignHCenter
                                            text: modelData.label
                                            color: modelData.direction !== "" && modelData.direction === diagnosticPovCard.info.direction
                                                ? "#9fcfbd" : "#607177"
                                            font.pixelSize: 8; font.bold: true; font.family: "Consolas" }
                                    }
                                }
                            }
                        }
                    }
                }
                Text { text: "BUTTON ROUTES · " + backend.buttonCount + " PHYSICAL / " + backend.vjoyButtonCount + " VIRTUAL"
 color: "#94a1a6"
 font.pixelSize: 10
 font.bold: true }
                GridLayout { width: parent.width
 columns: width >= 1100 ? 5 : (width >= 760 ? 3 : 2)
 columnSpacing: 8
 rowSpacing: 8
                    Repeater { model: root.allButtons
                        delegate: Panel { Layout.fillWidth: true
 Layout.preferredHeight: 62
 color: modelData.pressed ? "#ed20363c" : "#dc151a1f"
                            Column { anchors.fill: parent
 anchors.margins: 10; spacing: 4
                                Row { spacing: 7
                                    StatusDot { tone: modelData.pressed ? "#91c4d0" : "#59646a" }
                                    Text { text: modelData.label.toUpperCase() + "  " + (modelData.pressed ? "PHYSICAL DOWN" : "PHYSICAL UP")
                                        color: modelData.pressed ? "#c9e9ee" : "#89969b"; font.pixelSize: 9; font.bold: true }
                                }
                                Text { text: modelData.target > 0 ? "ROUTE vJOY " + ("0" + modelData.target).slice(-2) + "  ·  VIRTUAL " + (modelData.virtualPressed ? "DOWN" : "UP") : "ROUTE UNASSIGNED"
                                    color: modelData.virtualPressed ? "#b9dcc2" : "#819297"; font.pixelSize: 8; font.family: "Consolas" }
                            }
                        }
                    }
                }
                Panel { width: parent.width
                    height: Math.max(256, Math.min(330, diagnosticsPage.height * 0.42))
                    Column { anchors.fill: parent
                        anchors.margins: 14
                        spacing: 5
                        Text { text: "EVENT LOG"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                        ListView {
                            id: eventLogView
                            width: parent.width
                            height: parent.height - 24
                            clip: true
                            model: backend.eventLog
                            spacing: 3
                            property bool followTail: true
                            onMovementEnded: followTail = atYEnd
                            onCountChanged: Qt.callLater(function() {
                                if (eventLogView.followTail) eventLogView.positionViewAtEnd()
                            })
                            Component.onCompleted: positionViewAtEnd()
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Text {
                                width: eventLogView.width - 16
                                text: modelData
                                color: index === eventLogView.count - 1 ? "#cbdadd" : "#839197"
                                font.pixelSize: 10
                                font.family: "Consolas"
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id: profilesPage
            anchors.fill: parent
            visible: root.currentPage === 5
            contentWidth: width
            contentHeight: profilesContent.implicitHeight + 18
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: profilesContent
                x: 1
                width: profilesPage.width - 10
                spacing: 14
                RowLayout { width: parent.width
                    PageTitle { heading: "Profiles"
                        detail: "Independent mapping configurations; calibration remains tied to the physical controller" }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: "+ NEW PROFILE"
                        onTriggered: newProfileDialog.open() }
                }
                Text { text: "BASE PROFILE"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                Panel { width: parent.width; height: 102
                    color: "#e51a352f"; border.color: "#4a91a8a0"
                    RowLayout { anchors.fill: parent; anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { text: backend.activeProfileName.toUpperCase()
                                color: "#e8f3f2"; font.pixelSize: 16; font.bold: true }
                            Text { text: backend.effectiveProfileName === backend.activeProfileName
                                ? "Current controller configuration · Axes and Buttons edit this profile"
                                : "Effective: " + backend.effectiveProfileName + " · " + backend.profileSourceLabel
                                color: "#9db8bd"; font.pixelSize: 10; elide: Text.ElideRight
                                Layout.fillWidth: true }
                        }
                        Row { spacing: 7
                            StatusDot { tone: "#98d1bd" }
                            Text { text: "BASE"; color: "#add8c5"; font.pixelSize: 10; font.bold: true }
                        }
                    }
                }
                Text { text: "AVAILABLE PROFILES"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                GridLayout { width: parent.width
                    columns: width >= 1080 ? 3 : (width >= 700 ? 2 : 1)
                    columnSpacing: 12; rowSpacing: 12
                    Repeater { model: backend.profiles
                        delegate: Panel { Layout.fillWidth: true; Layout.preferredHeight: 164
                            color: modelData.active ? "#e51a352f" : "#ed182128"
                            border.color: modelData.active ? "#4a91a8a0" : "#41546770"
                            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 7
                                RowLayout { Layout.fillWidth: true
                                    Text { text: modelData.name.toUpperCase()
                                        color: "#e8eeee"; font.pixelSize: 13; font.bold: true
                                        elide: Text.ElideRight; Layout.fillWidth: true }
                                    Row { visible: modelData.effective; spacing: 5
                                        StatusDot { tone: "#98d1bd" }
                                        Text { text: modelData.effectiveSource.toUpperCase(); color: "#add8c5"; font.pixelSize: 9; font.bold: true }
                                    }
                                    ToolButton { text: "⋯"; font.pixelSize: 17
                                        contentItem: Text { text: parent.text; color: "#a9bbc0"
                                            font.pixelSize: parent.font.pixelSize
                                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 3; color: parent.hovered ? "#2b393f" : "transparent" }
                                        onClicked: profileActionMenu.open()
                                        Menu { id: profileActionMenu
                                            MenuItem { text: "Rename"; enabled: !modelData.protected
                                                onTriggered: { renameProfileDialog.profileId = modelData.id
                                                    renameProfileDialog.profileName = modelData.name
                                                    renameProfileDialog.open() } }
                                            MenuItem { text: "Clone"; onTriggered: backend.cloneProfile(modelData.id) }
                                            MenuSeparator { }
                                            MenuItem { text: "Delete"; enabled: !modelData.protected && !modelData.active
                                                onTriggered: { deleteProfileDialog.profileId = modelData.id
                                                    deleteProfileDialog.profileName = modelData.name
                                                    deleteProfileDialog.open() } }
                                        }
                                    }
                                }
                                Text { text: modelData.mappedAxes + " mapped axes"
                                    color: "#a4bdc3"; font.pixelSize: 10; font.family: "Consolas" }
                                Text { text: modelData.mappedButtons + " mapped buttons"
                                    color: "#a4bdc3"; font.pixelSize: 10; font.family: "Consolas" }
                                Item { Layout.fillHeight: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { visible: modelData.protected; text: "PROTECTED FALLBACK"
                                        color: "#72858b"; font.pixelSize: 8; font.bold: true; Layout.fillWidth: true }
                                    Item { visible: !modelData.protected; Layout.fillWidth: true }
                                    CommandButton { label: modelData.active ? "BASE" : "ACTIVATE"
                                        subdued: modelData.active
                                        commandEnabled: !modelData.active
                                        onTriggered: backend.activateProfile(modelData.id) }
                                }
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id: settingsPage
            anchors.fill: parent
 visible: root.currentPage === 4
 contentWidth: width
 contentHeight: settingsContent.implicitHeight + 18
 clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: settingsContent
                x: 1
 width: settingsPage.width - 10
 spacing: 14
                PageTitle { heading: "Settings"
 detail: "Persistent device selection and safe mapping defaults" }
                Panel { width: parent.width
  height: 76
                    RowLayout { anchors.fill: parent
  anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "HOTAS BF6  ·  VERSION " + Qt.application.version
  color: "#e8eeee"
  font.pixelSize: 12
  font.bold: true }
                            Text { text: "Stable updates are checked automatically before launch."
  color: "#9dafb4"
  font.pixelSize: 10 }
                        }
                        Text { text: "CHANNEL  STABLE"
  color: "#8fd5c9"
  font.pixelSize: 10
  font.bold: true }
                    }
                }
                Panel { width: parent.width
 height: 84
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "PREFERRED PHYSICAL DEVICE"
 color: "#e8eeee"
 font.pixelSize: 12
 font.bold: true }
                            Text { text: backend.deviceId.length > 0 ? backend.deviceName + " · " + backend.deviceId : "Automatic selection: prefers T.Flight HOTAS One"
 color: "#8e9ba1"
 font.pixelSize: 10
 elide: Text.ElideRight
 Layout.fillWidth: true }
                        }
                        CommandButton { label: "USE CONNECTED"
 subdued: true
 onTriggered: backend.useConnectedDevice() }
                    }
                }
                Panel { width: parent.width
 height: 68
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        Text { text: "START MAPPING AUTOMATICALLY"
 color: "#e8eeee"
 font.pixelSize: 12
 font.bold: true
 Layout.fillWidth: true }
                        Switch {
                            id: startMappingToggle
                            checked: backend.startMappingOnLaunch
                            onToggled: backend.setStartMappingOnLaunch(checked)
                            indicator: Rectangle { implicitWidth: 36
 implicitHeight: 18
 radius: 9
 color: startMappingToggle.checked ? "#476d78" : "#30383d"
 border.color: "#33dce5e8"
                                Rectangle { width: 14
 height: 14
 radius: 7
 x: startMappingToggle.checked ? 19 : 3
 anchors.verticalCenter: parent.verticalCenter
 color: "#e4ecee" }
                            }
                        }
                    }
                }
                Panel { width: parent.width
 height: 136
                    color: root.vjoyCardColor()
                    border.color: root.vjoyCardBorder()
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Row { spacing: 7
                                Text { text: "VJOY DEVICE"; color: "#e8eeee"; font.pixelSize: 12; font.bold: true }
                                StatusDot { tone: root.capacityColor() }
                                Text { text: backend.vjoyStatusSeverity.toUpperCase()
                                    color: root.capacityColor(); font.pixelSize: 10; font.bold: true }
                            }
                            Text { text: "Device " + backend.vjoyDeviceId
                                color: backend.vjoyReady ? "#d9ebe7" : root.capacityColor(); font.pixelSize: 13; font.bold: true }
                            Text { text: "X / Y / Z / Rz"
                                color: "#9dafb4"; font.pixelSize: 10; font.family: "Consolas" }
                            Text { text: backend.vjoyButtonCount + " buttons   ·   Required " + backend.vjoyRequiredButtonCount
                                color: "#9dafb4"; font.pixelSize: 10; font.family: "Consolas" }
                            Text { text: backend.vjoyReady ? (backend.vjoyStatusSeverity === "ready"
                                ? "Virtual output is ready." : root.capacityState()) : backend.vjoyStatus
                                color: root.capacityColor(); font.pixelSize: 10; font.bold: true }
                        }
                        Column { spacing: 7
                            SpinBox { from: 1
                                to: 16
                                value: backend.vjoyDeviceId
                                onValueModified: backend.setVjoyDeviceId(value) }
                            CommandButton { label: "CONFIGURE VJOY"
                                subdued: true
                                onTriggered: backend.openVjoyConfiguration() }
                        }
                    }
                }
                Panel { width: parent.width
 height: 118
                    color: !backend.hidhideAvailable ? "#e52d2419" : backend.hidhideCloakStateKnown && backend.hidhideCloaked ? "#e51a352f" : "#ed182128"
                    border.color: !backend.hidhideAvailable ? "#c28b624f" : backend.hidhideCloakStateKnown && backend.hidhideCloaked ? "#3c9ca8a0" : "#41546770"
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "HIDHIDE"
 color: "#e8eeee"
 font.pixelSize: 12
 font.bold: true }
                            Text { text: !backend.hidhideAvailable ? "NOT DETECTED" : !backend.hidhideCloakStateKnown ? "INSTALLED · CLOAK STATE UNAVAILABLE" : backend.hidhideCloaked ? (backend.hidhideMapperAllowed ? "CLOAKING ACTIVE · MAPPER ALLOWED" : "CLOAKING ACTIVE · MAPPER BLOCKED") : "CLOAKING OFF"
                                color: !backend.hidhideAvailable ? "#d49b62" : backend.hidhideCloakStateKnown && backend.hidhideCloaked ? "#8fd5c9" : "#b7d8df"; font.pixelSize: 12; font.bold: true }
                            Text { text: "The mapper registers only its own executable when cloaking is active, so reconnects do not depend on a build-folder path."
                                color: "#9dafb4"; font.pixelSize: 10; wrapMode: Text.WordWrap
 Layout.fillWidth: true }
                        }
                        Column { spacing: 7
                            CommandButton { label: "REFRESH STATUS"
 subdued: true
 onTriggered: backend.refreshHidHideStatus() }
                            CommandButton { label: "OPEN HIDHIDE"
 subdued: true
 onTriggered: backend.openHidHideConfiguration() }
                        }
                    }
                }
                Panel { width: parent.width
 height: 84
 color: "#ef251a1a"
 border.color: "#44bd7777"
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "RESET APPLICATION CONFIGURATION"
 color: "#f0d7d5"
 font.pixelSize: 12
 font.bold: true }
                            Text { text: "Restore default routes and clear saved calibration."
 color: "#ab999d"
 font.pixelSize: 10 }
                        }
                        CommandButton { label: "RESET"
 subdued: true
 onTriggered: resetDialog.open() }
                    }
                }
            }
        }
        CurveEditor { anchors.fill: parent; visible: root.currentPage === 6; backendObject: backend }
    }

    Dialog {
        id: newProfileDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 410
        title: "New profile"
        standardButtons: Dialog.NoButton
        onOpened: {
            profileNameField.text = ""
            startProfile.currentIndex = backend.activeProfileIndex
            profileNameField.forceActiveFocus()
        }
        contentItem: Column { width: 358; spacing: 12
            Text { text: "NAME"; color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
            TextField { id: profileNameField; width: parent.width
                placeholderText: "Helicopter"; color: "#e7f0f1"
                selectByMouse: true }
            Text { text: "START FROM"; color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
            FlightComboBox { id: startProfile; width: parent.width
                model: backend.profiles; textRole: "name"; valueRole: "id" }
            Text { text: "Copies the selected mapping configuration. Calibration remains global to the controller."
                width: parent.width; wrapMode: Text.WordWrap; color: "#879ba1"; font.pixelSize: 10 }
            Row { width: parent.width; spacing: 8
                CommandButton { id: createProfileCancelButton; label: "CANCEL"; subdued: true
                    onTriggered: newProfileDialog.close() }
                Item { width: parent.width - createProfileCancelButton.width - createProfileButton.width - 16; height: 1 }
                CommandButton { id: createProfileButton; label: "CREATE"
                    commandEnabled: profileNameField.text.trim().length > 0
                    onTriggered: { if (backend.createProfile(profileNameField.text, startProfile.currentValue)) newProfileDialog.close() } }
            }
        }
        background: Panel { color: "#1b2126"; border.color: "#3adce5e8" }
    }
    Dialog {
        id: renameProfileDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 390
        property string profileId: ""
        property string profileName: ""
        title: "Rename profile"
        standardButtons: Dialog.NoButton
        onOpened: { renameProfileField.text = profileName; renameProfileField.forceActiveFocus(); renameProfileField.selectAll() }
        contentItem: Column { width: 338; spacing: 12
            Text { text: "NAME"; color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
            TextField { id: renameProfileField; width: parent.width; color: "#e7f0f1"; selectByMouse: true }
            Row { width: parent.width; spacing: 8
                CommandButton { id: renameProfileCancelButton; label: "CANCEL"; subdued: true
                    onTriggered: renameProfileDialog.close() }
                Item { width: parent.width - renameProfileCancelButton.width - renameProfileButton.width - 16; height: 1 }
                CommandButton { id: renameProfileButton; label: "RENAME"
                    commandEnabled: renameProfileField.text.trim().length > 0
                    onTriggered: { if (backend.renameProfile(renameProfileDialog.profileId, renameProfileField.text)) renameProfileDialog.close() } }
            }
        }
        background: Panel { color: "#1b2126"; border.color: "#3adce5e8" }
    }
    Dialog {
        id: deleteProfileDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 390
        property string profileId: ""
        property string profileName: ""
        title: "Delete profile?"
        standardButtons: Dialog.NoButton
        contentItem: Column { width: 338; spacing: 14
            Text { text: "Delete \"" + deleteProfileDialog.profileName + "\"?\n\nThis profile's mappings will be removed."
                width: parent.width; wrapMode: Text.WordWrap; color: "#d5e0e3"; font.pixelSize: 12 }
            Row { width: parent.width; spacing: 8
                CommandButton { id: deleteProfileCancelButton; label: "CANCEL"; subdued: true
                    onTriggered: deleteProfileDialog.close() }
                Item { width: parent.width - deleteProfileCancelButton.width - deleteProfileButton.width - 16; height: 1 }
                CommandButton { id: deleteProfileButton; label: "DELETE"; subdued: true
                    onTriggered: { if (backend.deleteProfile(deleteProfileDialog.profileId)) deleteProfileDialog.close() } }
            }
        }
        background: Panel { color: "#241b1b"; border.color: "#44bd7777" }
    }
    Dialog {
        id: axisConflictDialog
        parent: Overlay.overlay
 anchors.centerIn: parent
 modal: true
 width: 390
        title: "Replace axis route?"
 standardButtons: Dialog.Cancel
        contentItem: Column { width: 340
 spacing: 14
            Text { width: parent.width
 text: "This vJoy axis already has a source. Replacing it disables the earlier route."
 wrapMode: Text.WordWrap
 color: "#d5e0e3"
 font.pixelSize: 12 }
            CommandButton { width: parent.width
 label: "REPLACE ROUTE"
 onTriggered: { backend.setMapping(root.conflictingAxis, root.conflictingTarget, true)
 axisConflictDialog.close() } }
        }
        background: Panel { color: "#1b2126"
 border.color: "#3adce5e8" }
    }
    Dialog {
        id: buttonConflictDialog
        parent: Overlay.overlay
 anchors.centerIn: parent
 modal: true
 width: 390
        title: "Replace button route?"
 standardButtons: Dialog.Cancel
        contentItem: Column { width: 340
 spacing: 14
            Text { width: parent.width
 text: "This virtual button already has a physical source. Replacing it disables the earlier source."
 wrapMode: Text.WordWrap
 color: "#d5e0e3"
 font.pixelSize: 12 }
            CommandButton { width: parent.width
 label: "REPLACE ROUTE"
 onTriggered: {
     if (root.conflictingPovHat > 0) {
         backend.setPovMapping(root.conflictingPovHat, root.conflictingPovDirection,
                               root.conflictingVirtualButton, true)
     } else {
         backend.setButtonMapping(root.conflictingButton, root.conflictingVirtualButton, true)
     }
 buttonConflictDialog.close() } }
        }
        background: Panel { color: "#1b2126"
 border.color: "#3adce5e8" }
    }
    Dialog {
        id: resetDialog
        parent: Overlay.overlay
 anchors.centerIn: parent
 modal: true
 width: 370
        title: "Reset configuration?"
 standardButtons: Dialog.Cancel
        contentItem: Column { width: 320
 spacing: 14
            Text { width: parent.width
 text: "This restores default routes and clears calibration data."
 wrapMode: Text.WordWrap
 color: "#d5e0e3"
 font.pixelSize: 12 }
            CommandButton { width: parent.width
 label: "RESET CONFIGURATION"
 onTriggered: { backend.resetApplicationConfiguration()
 resetDialog.close() } }
        }
        background: Panel { color: "#241b1b"
 border.color: "#44bd7777" }
    }
}
