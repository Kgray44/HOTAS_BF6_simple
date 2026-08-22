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

    component Panel: Rectangle {
        color: "#f0171c21"
        border.color: "#3046535c"
        border.width: 1
        radius: 8
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: 1
            radius: 1
            color: "#2d7f9198"
        }
    }
    component FineLine: Rectangle { height: 1
 color: "#203b454c" }
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
        radius: 5
        color: !commandEnabled ? "#171b1f" : commandMouse.containsMouse ? (subdued ? "#2b3439" : "#4b626c") : (subdued ? "#20272c" : "#374a52")
        border.color: !commandEnabled ? "#182f3539" : (subdued ? "#4a596167" : "#738aa0a9")
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
        property color tone: "#a8c2ca"
        implicitHeight: 22
 implicitWidth: 160
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 width: parent.width
 height: 5
 radius: 3
 color: "#0c0f12"
 border.color: "#28384248" }
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 x: parent.width / 2
 width: 1
 height: 11
 color: "#4a8296a0" }
        Rectangle { visible: !parent.offline
 width: 9
 height: 9
 radius: 5
 x: Math.max(0, Math.min(parent.width - width, ((parent.value + 1) * 0.5) * (parent.width - width)))
 anchors.verticalCenter: parent.verticalCenter
 color: parent.tone
 border.color: "#9bcfd8da" }
        Text { anchors.centerIn: parent
 visible: parent.offline
 text: "OFFLINE"
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
 spacing: 3
            Text { text: telemetryItem.caption
 color: "#8b969c"
 font.pixelSize: 10
 font.bold: true }
            Text { text: telemetryItem.value
 color: telemetryItem.tone
 font.pixelSize: 14
 font.bold: true
 elide: Text.ElideRight
 width: parent.width }
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
 color: "#f1f3f2"
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
        Layout.preferredHeight: 188
        visible: info && info.available
        color: "#ec1a2025"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
 spacing: 8
            RowLayout { Layout.fillWidth: true
                Text { text: axisModule.info.label
 color: "#eef2f1"
 font.pixelSize: 15
 font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: axisModule.info.detail.toUpperCase() + " · ROUTE " + axisModule.info.target
 color: axisModule.info.target === "Disabled" ? "#929da1" : "#a6c2ca"
 font.pixelSize: 10 }
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
 tone: "#a7c2ca" }
                Text { text: root.valuePercent(axisModule.info.raw)
 color: "#d7e5e8"
 font.family: "Consolas"
 font.pixelSize: 17
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
 tone: "#b8c7cb" }
                Text { text: backend.vjoyReady ? root.valuePercent(axisModule.info.virtualValue) : "OFFLINE"
 color: backend.vjoyReady ? "#b9d5dc" : "#a7afb4"
 font.family: "Consolas"
 font.pixelSize: backend.vjoyReady ? 13 : 10
 font.weight: backend.vjoyReady ? Font.DemiBold : Font.Normal
 Layout.preferredWidth: 72
 horizontalAlignment: Text.AlignRight }
            }
            Item { Layout.preferredHeight: 1 }
            RowLayout { Layout.fillWidth: true
 spacing: 10
                Text { text: "OUTPUT"
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
        Layout.preferredHeight: 122
        color: info && info.pressed ? "#e42c3d42" : "#ec1a2025"
        border.color: info && info.pressed ? "#7686a4a7" : "#26323a40"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 13
 spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: buttonCard.info.label
 color: "#edf2f1"
 font.pixelSize: 13
 font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Row { spacing: 5
                    StatusDot { tone: buttonCard.info.pressed ? "#a8c8d0" : "#68747a" }
                    Text { text: buttonCard.info.pressed ? "PRESSED" : "RELEASED"
 color: buttonCard.info.pressed ? "#c8e0e1" : "#919ca0"
 font.pixelSize: 9
 font.bold: true }
                }
            }
            Text { text: "Input is " + (buttonCard.info.pressed ? "active" : "released")
 color: "#929da1"
 font.pixelSize: 10 }
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
            Text { visible: buttonCard.info.virtualPressed
 text: "VIRTUAL OUTPUT ACTIVE"
 color: "#b7d4bf"
 font.pixelSize: 9
 font.bold: true }
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
 height: 340
        opacity: root.menuOpen ? 1 : 0
        scale: root.menuOpen ? 1 : 0.97
        visible: opacity > 0
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
                    { label: "CALIBRATION", page: 2, future: false }, { label: "DIAGNOSTICS", page: 3, future: false },
                    { label: "SETTINGS", page: 4, future: false }, { label: "", page: -1, future: false },
                    { label: "PROFILES", page: 5, future: true }, { label: "CURVES", page: 6, future: true }
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
 detail: "Direct input, output routing, and live state" }
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
 value: backend.vjoyReady ? "Ready / " + backend.vjoyButtonCount + " buttons" : "Offline"
 tone: backend.vjoyReady ? "#b9d1d8" : "#a5afb3"
 Layout.fillWidth: true }
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
 detail: "Physical DirectInput state is visible even while vJoy is offline" }
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
 value: backend.vjoyReady ? backend.vjoyButtonCount + " AVAILABLE" : "VJOY OFFLINE"
 tone: backend.vjoyReady ? "#b9d1d8" : "#a5afb3"
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
 Layout.preferredHeight: 112
                            RowLayout { anchors.fill: parent
 anchors.margins: 15
                                ColumnLayout { Layout.preferredWidth: 130
                                    Text { text: calibrationAxisCard.info.label.toUpperCase()
 color: "#eaf0f1"
 font.pixelSize: 12
 font.bold: true }
                                    Text { text: calibrationAxisCard.info.calibrationEnabled ? "SAVED RANGE" : "RAW DEFAULT"
 color: calibrationAxisCard.info.calibrationEnabled ? "#9fc7b1" : "#89979d"
 font.pixelSize: 9 }
                                }
                                Repeater { model: [{ n: "MIN", v: calibrationAxisCard.info.calibrationMinimum }, { n: "CENTER", v: calibrationAxisCard.info.calibrationCenter }, { n: "MAX", v: calibrationAxisCard.info.calibrationMaximum }]
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
                        { c: "POV", v: root.povText(), t: "#c9d6d9" },
                        { c: "MAP LATENCY", v: backend.latencyCurrentUs + " US", t: "#c9d6d9" },
                        { c: "VJOY WRITES", v: backend.vjoyWritesPerSecond.toFixed(0) + " / S", t: backend.vjoyReady ? "#b9d1d8" : "#89979d" }
                    ]
                        delegate: Panel { Layout.fillWidth: true
 Layout.preferredHeight: 78
                            Column { anchors.fill: parent
 anchors.margins: 13
 spacing: 7
                                Text { text: modelData.c
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                                Text { text: modelData.v
 color: modelData.t
 font.pixelSize: 16
 font.bold: true
 font.family: "Consolas" }
                            }
                        }
                    }
                }
                RowLayout { width: parent.width
                    Panel { Layout.fillWidth: true
 Layout.preferredHeight: 116
                        Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 7
                            Text { text: "DIRECTINPUT DEVICE"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                            Text { text: backend.deviceName
 color: "#e8eeee"
 font.pixelSize: 14
 font.bold: true
 elide: Text.ElideRight
 width: parent.width }
                            Text { text: backend.deviceId.length > 0 ? backend.deviceId : "No selected GUID"
 color: "#91a0a6"
 font.pixelSize: 10
 font.family: "Consolas"
 elide: Text.ElideRight
 width: parent.width }
                            Text { text: root.physicalStatusText() + " · " + backend.axisCount + " AXES · " + backend.buttonCount + " BUTTONS"
 color: root.physicalStatusColor()
 font.pixelSize: 10
 font.bold: true }
                        }
                    }
                    Panel { Layout.fillWidth: true
 Layout.preferredHeight: 116
                        Column { anchors.fill: parent
 anchors.margins: 14
 spacing: 7
                            Text { text: "VJOY OUTPUT"
 color: "#7f8d94"
 font.pixelSize: 9
 font.bold: true }
                            Text { text: backend.vjoyStatus
 color: backend.vjoyReady ? "#e8eeee" : "#b77b86"
 font.pixelSize: 14
 font.bold: true
 elide: Text.ElideRight
 width: parent.width }
                            Text { text: backend.vjoyReady ? "X / Y / Z / RZ · " + backend.vjoyButtonCount + " BUTTONS" : "Physical monitoring remains independent"
 color: "#91a0a6"
 font.pixelSize: 10 }
                            Text { text: backend.mappingActive ? "MAPPING ACQUIRED" : "MAPPING NOT ACQUIRED"
 color: backend.mappingActive ? "#9fc7b1" : "#a5afb3"
 font.pixelSize: 10
 font.bold: true }
                        }
                    }
                }
                Text { text: "RAW AXIS SNAPSHOT"
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
 Layout.preferredHeight: 78
                            Column { anchors.fill: parent
 anchors.margins: 12
 spacing: 4
                                Text { text: diagnosticAxisCard.info.label.toUpperCase()
 color: "#aebcc0"
 font.pixelSize: 9
 font.bold: true }
                                Text { text: "RAW  " + root.valuePercent(diagnosticAxisCard.info.raw)
 color: "#dfeaec"
 font.pixelSize: 12
 font.family: "Consolas" }
                                Text { text: "OUT  " + (backend.vjoyReady ? root.valuePercent(diagnosticAxisCard.info.virtualValue) : "OFFLINE")
 color: backend.vjoyReady ? "#a7c5cd" : "#9a7d75"
 font.pixelSize: 10
 font.family: "Consolas" }
                            }
                        }
                    }
                }
                Text { text: "BUTTON SNAPSHOT · " + backend.buttonCount + " PHYSICAL"
 color: "#94a1a6"
 font.pixelSize: 10
 font.bold: true }
                GridLayout { width: parent.width
 columns: width >= 1100 ? 5 : (width >= 760 ? 3 : 2)
 columnSpacing: 8
 rowSpacing: 8
                    Repeater { model: root.allButtons
                        delegate: Panel { Layout.fillWidth: true
 Layout.preferredHeight: 42
 color: modelData.pressed ? "#ed20363c" : "#dc151a1f"
                            Row { anchors.fill: parent
 anchors.margins: 10
 spacing: 7
                                StatusDot { tone: modelData.pressed ? "#91c4d0" : "#59646a"
 anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.label.toUpperCase()
 color: "#d6e1e3"
 font.pixelSize: 9
 font.bold: true
 anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.pressed ? "DOWN" : "UP"
 color: modelData.pressed ? "#b9dbe1" : "#89969b"
 font.pixelSize: 9
 anchors.verticalCenter: parent.verticalCenter }
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
 height: 74
                    RowLayout { anchors.fill: parent
 anchors.margins: 16
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "VJOY DEVICE"
 color: "#e8eeee"
 font.pixelSize: 12
 font.bold: true }
                            Text { text: backend.vjoyReady ? "Available with " + backend.vjoyButtonCount + " buttons" : backend.vjoyStatus
 color: backend.vjoyReady ? "#9fc7b1" : "#a5afb3"
 font.pixelSize: 10 }
                        }
                        SpinBox { from: 1
 to: 16
 value: backend.vjoyDeviceId
 onValueModified: backend.setVjoyDeviceId(value) }
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
 visible: root.currentPage === 5 || root.currentPage === 6
            Column { anchors.centerIn: parent
 spacing: 10
                Text { text: root.currentPage === 5 ? "PROFILES" : "CURVES"
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
