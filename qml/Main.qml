import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

ApplicationWindow {
    id: root
    width: 1320
    height: 840
    minimumWidth: 920
    minimumHeight: 650
    visible: true
    title: "HOTAS BF6"
    color: "#0d1013"
    font.family: "Segoe UI Variable"

    property int currentPage: 0
    property bool menuOpen: false
    property bool otherAxesExpanded: false
    property var allAxes: backend.axes
    property var allButtons: backend.buttons
    property int conflictingAxis: -1
    property string conflictingTarget: "Disabled"
    property int conflictingButton: -1
    property int conflictingVirtualButton: 0
    readonly property var outputChoices: ["Disabled", "X", "Y", "Z", "Rz"]
    readonly property var buttonOutputChoices: backend.buttonOutputChoices
    readonly property var primaryAxisOrder: [0, 1, 5, 2]
    readonly property bool hasPhysicalInput: backend.physicalConnected && backend.axisCount > 0

    function axisAt(index) { return allAxes[index] }
    function isPrimaryAxis(index) { return primaryAxisOrder.indexOf(index) >= 0 }
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
        if (info && info.key === "z") return ((Number(value) + 1) * 50).toFixed(1) + "%"
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
        return backend.vjoyButtonCount >= backend.vjoyRecommendedButtonCount ? "READY" : "READY · 32 RECOMMENDED"
    }
    function capacityColor() {
        if (!backend.vjoyReady || !backend.vjoyCapacitySufficient) return "#d49b62"
        return "#9fc9bb"
    }

    component Panel: Rectangle {
        color: "#e9161d23"
        border.color: "#41546770"
        border.width: 1
        radius: 6
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 5
            opacity: 0.5
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#2438434d" }
                GradientStop { position: 0.38; color: "#0a101419" }
                GradientStop { position: 1.0; color: "#0a0d1016" }
            }
        }
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: 1
            radius: 1
            color: "#5c9cafb8"
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1; color: "#1026323a" }
    }
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
                    Text { text: backend.mappingActive ? "Output will resume when input returns." : "Mapping stays off until you start it."; color: "#899397"; font.pixelSize: 11 }
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
                ComboBox {
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
    component ButtonCard: Panel {
        id: buttonCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 134
        color: info && info.pressed ? "#ec263e48" : "#ed182128"
        border.color: info && info.pressed ? "#93a3cfda" : "#43546770"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 13
 spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: "BUTTON " + ("0" + buttonCard.info.index).slice(-2)
 color: "#edf7f7"
 font.pixelSize: 13
 font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Row { spacing: 5
                    StatusDot { tone: buttonCard.info.pressed ? "#a8d9e6" : "#68747a" }
                    Text { text: buttonCard.info.pressed ? "PRESSED" : "RELEASED"
 color: buttonCard.info.pressed ? "#d6f0f4" : "#919ca0"
 font.pixelSize: 9
 font.bold: true }
                }
            }
            Text { text: "PHYSICAL   " + (buttonCard.info.pressed ? "DOWN" : "UP")
 color: buttonCard.info.pressed ? "#c4e4e9" : "#849398"
 font.pixelSize: 10
 font.family: "Consolas"
 font.bold: buttonCard.info.pressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "OUTPUT"
 color: "#8c989d"
 font.pixelSize: 9
 font.bold: true }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: buttonDestination
                    Layout.preferredWidth: 126
 Layout.preferredHeight: 29
                    model: root.buttonOutputChoices
                    currentIndex: buttonCard.info.target
                    onActivated: {
                        if (!backend.setButtonMapping(buttonCard.info.index, currentIndex, false)) {
                            root.conflictingButton = buttonCard.info.index
                            root.conflictingVirtualButton = currentIndex
                            currentIndex = buttonCard.info.target
                            buttonConflictDialog.open()
                        }
                    }
                    background: Rectangle { radius: 5
 color: "#0c1013"
 border.color: "#435660" }
                    contentItem: Text { leftPadding: 8
 text: buttonDestination.displayText
 color: "#dce4e4"
 verticalAlignment: Text.AlignVCenter
 font.pixelSize: 10 }
                }
            }
            Text { text: buttonCard.info.target > 0
                    ? "VIRTUAL    " + (buttonCard.info.virtualPressed ? "DOWN" : "UP")
                    : "VIRTUAL    UNROUTED"
                color: buttonCard.info.virtualPressed ? "#b9dcc2" : "#819297"
                font.pixelSize: 9
                font.family: "Consolas"
                font.bold: buttonCard.info.virtualPressed }
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
            FineLine { visible: root.width >= 1250; Layout.preferredWidth: 1
                Layout.preferredHeight: 24 }
            Row { visible: root.width >= 1250; spacing: 6
                Text { text: "PROFILE"
                    color: "#78919a"; font.pixelSize: 9; font.bold: true }
                Text { text: backend.activeProfileName.toUpperCase()
                    color: "#c3d8d9"; font.pixelSize: 10; font.bold: true
                    elide: Text.ElideRight; width: Math.min(128, implicitWidth) }
            }
            Item { Layout.fillWidth: true }
            Row { spacing: 7
                StatusDot { tone: backend.mappingActive ? "#91c4a4" : (backend.vjoyReady ? "#91bcc8" : "#a5afb3") }
                Text { text: backend.mappingActive ? "MAPPING ACTIVE" : (backend.vjoyReady ? "OUTPUT READY" : "MAPPING STOPPED")
 color: backend.mappingActive ? "#c0d8c6" : "#b5c0c1"
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
        height: 283
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
            Repeater {
                model: [
                    { label: "MAPPER", page: 0, future: false }, { label: "BUTTONS", page: 1, future: false },
                    { label: "PROFILES", page: 5, future: false }, { label: "CALIBRATION", page: 2, future: false }, { label: "DIAGNOSTICS", page: 3, future: false },
                    { label: "SETTINGS", page: 4, future: false }
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
            id: mapperPage
            anchors.fill: parent
 visible: root.currentPage === 0
 contentWidth: width
 contentHeight: mapperContent.implicitHeight + 18
 clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: mapperContent
                x: 1
 width: mapperPage.width - 10
 spacing: 16
                RowLayout { width: parent.width
                    PageTitle { heading: "Mapper"
 detail: "Direct input, output routing, and live state · Profile: " + backend.activeProfileName }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: backend.mappingActive ? "STOP MAPPING" : "START MAPPING"
 subdued: !backend.mappingActive
 onTriggered: backend.toggleMapping() }
                }
                Panel { width: parent.width
 height: 72
                    color: "#e71a2025"
                    RowLayout { anchors.fill: parent
 anchors.leftMargin: 20
 anchors.rightMargin: 20
 spacing: 20
                        TelemetryItem { caption: "DEVICE"
 value: backend.physicalConnected ? backend.deviceName : "No controller connected"
 tone: root.physicalStatusColor()
 Layout.fillWidth: true }
                        FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 28 }
                        TelemetryItem { caption: "LIVE INPUT"
 value: backend.physicalConnected ? backend.axisCount + " axes / " + backend.buttonCount + " buttons" : "Waiting for DirectInput"
 tone: backend.inputReportsPerSecond > 0 ? "#b9d1d8" : "#a5afb3"
 Layout.fillWidth: true }
                        FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 28 }
                        TelemetryItem { caption: "OUTPUT DEVICE " + backend.vjoyDeviceId
 value: backend.vjoyReady ? backend.vjoyButtonCount + " BUTTONS · " + root.capacityState() : "OFFLINE"
 tone: backend.vjoyReady ? root.capacityColor() : "#a5afb3"
 Layout.fillWidth: true }
                    }
                }
                Panel { visible: root.hasPhysicalInput && backend.vjoyReady && !backend.vjoyCapacitySufficient
                    width: parent.width; height: 58; color: "#e52d2419"; border.color: "#c28b624f"
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                        Text { text: "⚠"; color: "#d6a169"; font.pixelSize: 18; font.bold: true }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: "VJOY CAPACITY WARNING"; color: "#e0b075"; font.pixelSize: 10; font.bold: true }
                            Text { text: "Physical controller: " + backend.buttonCount + " buttons   ·   Virtual controller: " + backend.vjoyButtonCount + " buttons   ·   Increase Device 1 to at least " + backend.vjoyRequiredButtonCount + " (32 recommended)."
                                color: "#bda68f"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                    }
                }
                OfflineMapper { width: parent.width
                    visible: !root.hasPhysicalInput }
                Text { visible: root.hasPhysicalInput
                    text: "Primary controls"
                    color: "#aab4b6"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold }
                GridLayout { width: parent.width
 visible: root.hasPhysicalInput
 columns: width >= 980 ? 2 : 1
 columnSpacing: 12
 rowSpacing: 12
                    Repeater { model: root.primaryAxisOrder
 delegate: AxisModule { info: root.axisAt(modelData) } }
                }
                Panel {
                    id: otherAxesPanel
                    width: parent.width
 visible: root.hasPhysicalInput
 height: root.otherAxesExpanded ? otherAxesContent.implicitHeight + 56 : 52
 clip: true
 color: "#dc1a2025"
                    RowLayout { id: otherAxesHeader
 anchors.left: parent.left
 anchors.right: parent.right
 anchors.top: parent.top
 anchors.margins: 15
 height: 23
                        Text { text: "Other physical axes"
 color: "#ccd5d5"
 font.pixelSize: 11
 font.weight: Font.DemiBold }
                        Text { text: Math.max(0, backend.axisCount - 4) + " detected"
 color: "#879399"
 font.pixelSize: 9 }
                        Item { Layout.fillWidth: true }
                        Text { text: root.otherAxesExpanded ? "Collapse" : "Expand"
 color: "#aabfc5"
 font.pixelSize: 10
 font.bold: true }
                    }
                    MouseArea { anchors.left: parent.left
 anchors.right: parent.right
 anchors.top: parent.top
 height: 52
 cursorShape: Qt.PointingHandCursor
 onClicked: root.otherAxesExpanded = !root.otherAxesExpanded }
                    GridLayout {
                        id: otherAxesContent
                        visible: root.otherAxesExpanded
                        anchors.left: parent.left
 anchors.right: parent.right
 anchors.top: otherAxesHeader.bottom
 anchors.topMargin: 14
 anchors.leftMargin: 15
 anchors.rightMargin: 15
                        columns: width >= 980 ? 2 : 1
 columnSpacing: 12
 rowSpacing: 12
                        Repeater { model: 8
 delegate: AxisModule { info: root.axisAt(index)
 visible: info && info.available && !root.isPrimaryAxis(index)
 Layout.preferredHeight: 210 } }
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
                Text { text: allButtons.length > 0 ? "PHYSICAL BUTTON MATRIX" : "NO DIRECTINPUT BUTTONS AVAILABLE"
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
                        { c: "VJOY WRITES", v: backend.vjoyWritesPerSecond.toFixed(0) + " / S", t: backend.vjoyReady ? "#b9d1d8" : "#89979d",
                          note: backend.vjoyWritesPerSecond > 0 ? "ACTIVE · CHANGE-DRIVEN" : "IDLE · CHANGE-DRIVEN" },
                        { c: "MAPPING", v: backend.mappingActive ? "ACTIVE" : "STOPPED", t: backend.mappingActive ? "#a8cfba" : "#a5afb3",
                          note: backend.mappingActive ? "OUTPUT ACQUIRED" : "PHYSICAL MONITORING CONTINUES" },
                        { c: "ACTIVE PROFILE", v: backend.activeProfileName.toUpperCase(), t: "#b9d1d8",
                          note: "PERSISTENT MAPPING CONFIGURATION" },
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
 height: 122
                    Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 5
                        Text { text: "EVENT LOG"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                        Repeater { model: backend.eventLog
 delegate: Text { text: modelData
 color: index === 0 ? "#cbdadd" : "#839197"
 font.pixelSize: 10
 font.family: "Consolas" } }
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
                Text { text: "ACTIVE PROFILE"
                    color: "#94a1a6"; font.pixelSize: 10; font.bold: true }
                Panel { width: parent.width; height: 102
                    color: "#e51a352f"; border.color: "#4a91a8a0"
                    RowLayout { anchors.fill: parent; anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { text: backend.activeProfileName.toUpperCase()
                                color: "#e8f3f2"; font.pixelSize: 16; font.bold: true }
                            Text { text: "Current controller configuration · Mapper and Buttons edit this profile"
                                color: "#9db8bd"; font.pixelSize: 10; elide: Text.ElideRight
                                Layout.fillWidth: true }
                        }
                        Row { spacing: 7
                            StatusDot { tone: "#98d1bd" }
                            Text { text: "ACTIVE"; color: "#add8c5"; font.pixelSize: 10; font.bold: true }
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
                                    Row { visible: modelData.active; spacing: 5
                                        StatusDot { tone: "#98d1bd" }
                                        Text { text: "ACTIVE"; color: "#add8c5"; font.pixelSize: 9; font.bold: true }
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
                                    CommandButton { label: modelData.active ? "ACTIVE" : "ACTIVATE"
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
                    color: backend.vjoyReady && !backend.vjoyCapacitySufficient ? "#e52d2419" : "#ed182128"
                    border.color: backend.vjoyReady && !backend.vjoyCapacitySufficient ? "#c28b624f" : "#41546770"
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "VJOY DEVICE"
 color: "#e8eeee"
 font.pixelSize: 12
 font.bold: true }
                            Text { text: "Device " + backend.vjoyDeviceId + "   ·   " + (backend.vjoyReady ? "READY" : "OFFLINE")
                                color: backend.vjoyReady ? "#b7d8df" : "#d49b62"; font.pixelSize: 13; font.bold: true }
                            Text { text: "Axes       X   Y   Z   Rz"
                                color: "#9dafb4"; font.pixelSize: 10; font.family: "Consolas" }
                            Text { text: "Buttons    " + backend.vjoyButtonCount + "     Required " + backend.vjoyRequiredButtonCount + "     Recommended " + backend.vjoyRecommendedButtonCount
                                color: "#9dafb4"; font.pixelSize: 10; font.family: "Consolas" }
                            Text { text: backend.vjoyReady ? root.capacityState() : backend.vjoyStatus
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
        Item { anchors.fill: parent
 visible: root.currentPage === 6
            Column { anchors.centerIn: parent
 spacing: 10
                Text { text: "CURVES"
 color: "#dfe7e9"
 font.pixelSize: 22
 font.bold: true
 anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "PLANNED FOR A LATER RELEASE"
 color: "#829096"
 font.pixelSize: 10
 font.bold: true
 anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
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
            ComboBox { id: startProfile; width: parent.width
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
 onTriggered: { backend.setButtonMapping(root.conflictingButton, root.conflictingVirtualButton, true)
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
