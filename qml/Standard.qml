import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Page {
    id: root
    anchors.fill: parent
    padding: 0

    // Presentation state only: ThemeManager never reaches AppBackend or the
    // DirectInput-to-vJoy worker.
    Theme { id: theme }
    // Expose the token object through a real root property. Child components
    // cannot reliably use `root.theme`: `theme` is an internal QML id, not a
    // Page property. Keeping this explicit makes the shared Curve Editor use
    // the same live token object as the shell.
    readonly property var themeTokens: theme

    property int currentPage: 8
    property bool menuOpen: false
    // These small value objects survive a Loader unload; the page object
    // trees, Canvas buffers, delegates, and Connections do not.
    property var profileLibraryPresentationState: ({})
    property var automationPresentationState: ({})
    property var curveEditorPresentationState: ({})
    // Keep telemetry-shaped QVariant lists out of pages that cannot render
    // them. The backend still projects its bounded snapshot at its own rate.
    property var allAxes: (currentPage === 0 || currentPage === 2 || currentPage === 3) ? backend.axes : []
    property var allButtons: (currentPage === 1 || currentPage === 3) ? backend.buttons : []
    property var allPovs: (currentPage === 1 || currentPage === 3) ? backend.povs : []
    property var allPovInputs: (currentPage === 1 || currentPage === 3) ? backend.povInputs : []
    property int conflictingAxis: -1
    property string conflictingTarget: "Disabled"
    property int conflictingButton: -1
    property int conflictingVirtualButton: 0
    property int conflictingPovHat: 0
    property int conflictingPovDirection: -1
    readonly property var outputChoices: backend.virtualAxisChoices
    readonly property var buttonOutputChoices: backend.buttonOutputChoices
    readonly property var profileTriggerChoices: backend.profileTriggerChoices
    readonly property var profileTriggerBehaviorChoices: backend.profileTriggerBehaviorChoices
    readonly property var nativePovTargetChoices: backend.nativePovTargetChoices
    readonly property bool hasPhysicalInput: backend.physicalConnected && backend.axisCount > 0
    readonly property var selectedAxisInfo: root.axisAt(backend.selectedAxisIndex)
    readonly property int loadedPageCount: (overviewPageLoader.item ? 1 : 0)
        + (settingsPageLoader.item ? 1 : 0)
        + (profileLibraryLoader.item ? 1 : 0)
        + (axesPageLoader.item ? 1 : 0)
        + (buttonsPageLoader.item ? 1 : 0)
        + (calibrationPageLoader.item ? 1 : 0)
        + (diagnosticsPageLoader.item ? 1 : 0)
        + (curveEditorLoader.item ? 1 : 0)
        + (automationPageLoader.item ? 1 : 0)

    function pageItem(page) {
        switch (page) {
        case 0: return axesPageLoader.item
        case 1: return buttonsPageLoader.item
        case 2: return calibrationPageLoader.item
        case 3: return diagnosticsPageLoader.item
        case 4: return settingsPageLoader.item
        case 5: return profileLibraryLoader.item
        case 6: return curveEditorLoader.item
        case 7: return automationPageLoader.item
        case 8: return overviewPageLoader.item
        }
        return null
    }
    function loadedPage(page) { return pageItem(page) !== null }

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
        if (!backend.physicalConnected) return theme.danger
        return backend.lastPhysicalUpdateAgeMs >= 0 && backend.lastPhysicalUpdateAgeMs < 100 ? theme.ready : theme.textMuted
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
        if (info && info.unipolar) return (Math.max(0, Math.min(1, Number(value))) * 100).toFixed(1) + "%"
        return valuePercent(value)
    }
    function outputState(info) {
        if (!info || !info.virtualRouted) return "NOT ROUTED"
        if (!backend.vjoyReady) return "OUTPUT OFFLINE"
        if (!backend.mappingActive || !info.virtualValid) return "STANDBY"
        return controlValue(info, info.virtualValue)
    }
    function capacityState() {
        if (!backend.vjoyReady) return "VJOY OFFLINE"
        if (!backend.vjoyCapacitySufficient) return "CAPACITY INSUFFICIENT"
        return "READY"
    }
    function capacityColor() {
        return theme.statusColor(backend.vjoyStatusSeverity)
    }
    function vjoyCardColor() {
        if (backend.vjoyStatusSeverity === "ready") return Qt.rgba(theme.ready.r, theme.ready.g, theme.ready.b, 0.14)
        if (backend.vjoyStatusSeverity === "warning") return Qt.rgba(theme.warning.r, theme.warning.g, theme.warning.b, 0.12)
        return Qt.rgba(theme.danger.r, theme.danger.g, theme.danger.b, 0.12)
    }
    function vjoyCardBorder() {
        return theme.statusColor(backend.vjoyStatusSeverity)
    }

    component Panel: AviationPanel { theme: root.themeTokens }
    component FineLine: Rectangle { height: 1; color: theme.divider }
    component StatusDot: Rectangle {
        property color tone: theme.textMuted
        width: 6
 height: 6
 radius: theme.topGun ? 1 : 3
        color: tone
    }
    component CommandButton: Rectangle {
        property string label: "ACTION"
        property bool commandEnabled: true
        property bool subdued: false
        property bool destructive: false
        signal triggered()
        implicitWidth: Math.max(110, labelText.implicitWidth + 30)
        implicitHeight: 36
        radius: theme.controlRadius
        color: !commandEnabled ? theme.controlDisabled : destructive ? (commandMouse.containsMouse ? Qt.rgba(theme.danger.r, theme.danger.g, theme.danger.b, 0.25) : Qt.rgba(theme.danger.r, theme.danger.g, theme.danger.b, 0.14)) : commandMouse.containsMouse ? (subdued ? theme.buttonSecondaryHover : theme.buttonHover) : (subdued ? theme.buttonSecondary : theme.buttonSurface)
        border.color: !commandEnabled ? theme.border : (destructive ? theme.danger : (subdued ? theme.border : theme.orange))
        opacity: commandEnabled ? 1.0 : 0.45
        Text { id: labelText
 anchors.centerIn: parent
 text: parent.label
 color: !parent.commandEnabled ? theme.textFaint : (parent.destructive ? theme.danger : theme.textStrong)
 font.pixelSize: 11
 font.bold: true
 font.family: theme.topGun ? theme.displayFont : root.font.family }
        Rectangle { visible: theme.topGun && !parent.subdued && parent.commandEnabled
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: 4; anchors.bottomMargin: 3
            width: 24; height: 3; color: theme.orangeBright
            Repeater { model: 3; delegate: Rectangle { x: index * 8; width: 3; height: 3; color: theme.background } }
        }
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
            radius: theme.controlRadius
            color: flightCombo.enabled ? (flightCombo.hovered ? theme.controlHover : theme.control) : theme.controlDisabled
            border.color: flightCombo.activeFocus ? theme.orange : flightCombo.hovered ? theme.borderStrong : theme.border
        }
        contentItem: Text {
            leftPadding: flightCombo.leftPadding
            rightPadding: flightCombo.rightPadding
            text: flightCombo.displayText
            color: flightCombo.enabled ? theme.text : theme.textFaint
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: 10
            font.family: theme.topGun ? theme.displayFont : root.font.family
        }
        indicator: Text {
            x: flightCombo.width - width - 9
            y: (flightCombo.height - height) / 2
            text: "⌄"
            color: flightCombo.enabled ? theme.textMuted : theme.textFaint
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
                color: choiceDelegate.highlighted ? theme.textStrong : theme.text
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
                rightPadding: 10
                elide: Text.ElideRight
                font.pixelSize: 10
                font.family: theme.topGun ? theme.displayFont : root.font.family
            }
            background: Rectangle {
                radius: theme.controlRadius
                color: choiceDelegate.highlighted ? theme.selection : (choiceDelegate.currentSelection ? theme.selectionCurrent : (choiceDelegate.hovered ? theme.controlHover : "transparent"))
                border.color: choiceDelegate.highlighted ? theme.orange : (choiceDelegate.currentSelection ? theme.borderStrong : "transparent")
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
                // Keep the delegate model attached across the Popup's open
                // transition. Detaching it while visible can leave a freshly
                // opened profile-source list with no delegates to paint.
                model: flightCombo.delegateModel
                currentIndex: flightCombo.highlightedIndex
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                color: theme.tooltip
                border.color: theme.borderStrong
                radius: theme.controlRadius
            }
        }
    }
    component FlightTextInput: TextField {
        id: flightTextInput
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 10
        topPadding: 7
        bottomPadding: 7
        color: theme.text
        placeholderTextColor: theme.textFaint
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        font.pixelSize: 11
        font.family: theme.topGun ? theme.displayFont : root.font.family
        background: Rectangle {
            radius: theme.controlRadius
            color: !flightTextInput.enabled ? theme.controlDisabled
                : flightTextInput.activeFocus ? (theme.topGun ? "#102127" : theme.controlPressed)
                : (flightTextInput.hovered ? theme.controlHover : theme.control)
            border.color: flightTextInput.activeFocus ? theme.orange
                : (flightTextInput.hovered ? theme.borderStrong : theme.border)
        }
    }
    component FlightNumericStepper: Item {
        id: flightStepper
        property real value: 0
        property real from: -100
        property real to: 100
        property real stepSize: 0.1
        property int decimals: 1
        signal valueEdited(real value)
        implicitWidth: 148
        implicitHeight: 30
        Rectangle {
            anchors.fill: parent
            color: theme.control
            border.color: theme.border
            radius: theme.controlRadius
        }
        Row {
            anchors.fill: parent
            Rectangle {
                width: 33; height: parent.height
                color: minusMouse.containsMouse && flightStepper.value > flightStepper.from ? theme.controlPressed : "transparent"
                Text { anchors.centerIn: parent; text: "−"; color: flightStepper.value > flightStepper.from ? theme.ivory : theme.textFaint; font.pixelSize: 17 }
                MouseArea { id: minusMouse; anchors.fill: parent; hoverEnabled: true
                    enabled: flightStepper.value > flightStepper.from
                    onClicked: flightStepper.valueEdited(Math.max(flightStepper.from, flightStepper.value - flightStepper.stepSize)) }
            }
            Rectangle { width: 1; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; color: theme.divider }
            TextInput { id: valueInput; width: parent.width - 68; height: parent.height
                text: Number(flightStepper.value).toFixed(flightStepper.decimals)
                color: theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                font.pixelSize: 11; font.bold: true; font.family: theme.telemetryFont; selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onEditingFinished: {
                    const parsed = Number(text)
                    if (isFinite(parsed)) flightStepper.valueEdited(Math.max(flightStepper.from, Math.min(flightStepper.to, parsed)))
                    text = Number(flightStepper.value).toFixed(flightStepper.decimals)
                }
                onActiveFocusChanged: if (!activeFocus) text = Number(flightStepper.value).toFixed(flightStepper.decimals)
            }
            Rectangle { width: 1; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; color: theme.divider }
            Rectangle {
                width: 33; height: parent.height
                color: plusMouse.containsMouse && flightStepper.value < flightStepper.to ? theme.controlPressed : "transparent"
                Text { anchors.centerIn: parent; text: "+"; color: flightStepper.value < flightStepper.to ? theme.ivory : theme.textFaint; font.pixelSize: 15 }
                MouseArea { id: plusMouse; anchors.fill: parent; hoverEnabled: true
                    enabled: flightStepper.value < flightStepper.to
                    onClicked: flightStepper.valueEdited(Math.min(flightStepper.to, flightStepper.value + flightStepper.stepSize)) }
            }
        }
    }
    component InstrumentMeter: Item {
        property real value: 0
        property bool offline: false
        property bool valid: true
        property color tone: theme.textMuted
        implicitHeight: 22
 implicitWidth: 160
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 width: parent.width
 height: 6
 radius: 1
 color: theme.panelInset
 border.color: theme.border }
        Rectangle { visible: parent.valid; anchors.verticalCenter: parent.verticalCenter
            x: 2; width: Math.max(0, Math.min(parent.width - 4, ((parent.value + 1) * 0.5) * (parent.width - 4)))
            height: 2; color: Qt.rgba(parent.tone.r, parent.tone.g, parent.tone.b, 0.42) }
        Rectangle { anchors.verticalCenter: parent.verticalCenter
 x: parent.width / 2
 width: 1
 height: 14
 color: theme.graphZero }
        Rectangle { visible: !parent.offline && parent.valid
 width: 10
 height: 10
 radius: 2
 x: Math.max(0, Math.min(parent.width - width, ((parent.value + 1) * 0.5) * (parent.width - width)))
 anchors.verticalCenter: parent.verticalCenter
 color: parent.tone
 border.color: theme.ivory }
        Text { anchors.centerIn: parent
 visible: parent.offline || !parent.valid
 text: parent.offline ? "OFFLINE" : "STANDBY"
 color: theme.textMuted
 font.pixelSize: 9
 font.bold: true }
    }
    component TelemetryItem: Item {
        id: telemetryItem
        property string caption: "CAPTION"
        property string value: "—"
        property color tone: theme.text
        implicitWidth: 150
        implicitHeight: 46
        Column { anchors.verticalCenter: parent.verticalCenter
 width: telemetryItem.width
 spacing: 3
            Text { text: telemetryItem.caption
 color: theme.textMuted
 font.pixelSize: 10
 font.bold: true }
            Text { text: telemetryItem.value
 color: telemetryItem.tone
 font.pixelSize: 15
 font.bold: true
 font.family: theme.telemetryFont
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
 color: theme.textStrong
 font.pixelSize: theme.topGun ? 40 : 26
 font.bold: true
 style: theme.topGun ? Text.Outline : Text.Normal
 styleColor: theme.topGun ? "#8e3321" : "transparent"
 font.family: theme.topGun ? theme.displayFont : root.font.family }
            Text { text: pageTitle.detail
 color: theme.textMuted
 font.pixelSize: 12
 font.family: theme.topGun ? theme.displayFont : root.font.family }
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
                    text: backend.mappingStatus
                    color: backend.mappingActive ? "#a9c9b3" : "#a7afb4"
                    font.pixelSize: 10
                    font.bold: true
                }
            }
            Text {
                Layout.fillWidth: true
                text: "The mapper is idle. Connect a controller whenever you are ready and its live controls will appear here automatically."
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
                value: axisModule.info.unipolar ? Number(axisModule.info.calibrated) * 2 - 1 : Number(axisModule.info.calibrated)
 tone: "#8eb5c1" }
                Text { text: root.controlValue(axisModule.info, axisModule.info.calibrated)
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
                value: axisModule.info.unipolar ? Number(axisModule.info.virtualValue) * 2 - 1 : Number(axisModule.info.virtualValue)
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
        color: theme.topGun ? "#d80a171b" : "#eb11171b"
        border.color: theme.topGun ? theme.graphFrame : "#3b66747d"
        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            RowLayout { width: parent.width
                Text { text: "LIVE TRANSFER"; color: theme.topGun ? theme.ivory : "#dce9eb"; font.pixelSize: 11; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                Text { text: curveViewer.info && curveViewer.info.unipolar ? "0–100% THROTTLE DOMAIN" : "−100% TO +100% NORMALIZED DOMAIN"
                    color: theme.textMuted; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Row { spacing: 12
                    Text { text: "— INPUT"; color: theme.graphInput; font.pixelSize: 9; font.bold: true }
                    Text { text: "— OUTPUT"; color: theme.graphOutput; font.pixelSize: 9; font.bold: true }
                    Text { text: "○ PHYSICAL"; color: theme.ivory; font.pixelSize: 9; font.bold: true }
                    Text { text: "● TRANSFORMED"; color: theme.cyan; font.pixelSize: 9; font.bold: true }
                }
            }
            Canvas {
                id: curveCanvas
                width: parent.width
                height: parent.height - 31
                antialiasing: true
                renderTarget: Canvas.Image
                property var curveSamples: curveViewer.samples
                property real physicalInput: curveViewer.info ? Number(curveViewer.info.calibrated) : 0
                property real transformedOutput: curveViewer.info ? Number(curveViewer.info.transformed) : 0
                property real domainMinimum: curveViewer.info && curveViewer.info.unipolar ? 0 : -1
                function xFor(value, left, plotWidth) { return left + (value - domainMinimum) / (1 - domainMinimum) * plotWidth }
                function yFor(value, top, plotHeight) { return top + (1 - (value - domainMinimum) / (1 - domainMinimum)) * plotHeight }
                onCurveSamplesChanged: requestPaint()
                onPhysicalInputChanged: requestPaint()
                onTransformedOutputChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                // The initial backend snapshot can arrive before this Canvas has
                // a size. Paint once the item has completed so an idle axis still
                // shows its instrument grid and reference trace.
                Component.onCompleted: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const left = 38, right = 12, top = 10, bottom = 25
                    const plotWidth = Math.max(1, width - left - right)
                    const plotHeight = Math.max(1, height - top - bottom)
                    ctx.fillStyle = theme.graphBackground
                    ctx.fillRect(left, top, plotWidth, plotHeight)
                    ctx.strokeStyle = theme.graphGrid
                    ctx.lineWidth = 1
                    for (let tick = 0; tick <= 4; ++tick) {
                        const x = left + plotWidth * tick / 4
                        const y = top + plotHeight * tick / 4
                        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + plotHeight); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(left + plotWidth, y); ctx.stroke()
                    }
                    if (domainMinimum < 0) {
                        ctx.strokeStyle = theme.graphZero
                        ctx.beginPath()
                        ctx.moveTo(left, yFor(0, top, plotHeight))
                        ctx.lineTo(left + plotWidth, yFor(0, top, plotHeight))
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(xFor(0, left, plotWidth), top)
                        ctx.lineTo(xFor(0, left, plotWidth), top + plotHeight)
                        ctx.stroke()
                    }
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
                    trace("input", theme.graphInput, 1.25)
                    trace("output", theme.graphOutput, 2.0)
                    if (curveViewer.info && Number(curveViewer.info.hysteresis) > 0) {
                        const halfBand = Number(curveViewer.info.hysteresis)
                        const x0 = xFor(Math.max(domainMinimum, physicalInput - halfBand), left, plotWidth)
                        const x1 = xFor(Math.min(1, physicalInput + halfBand), left, plotWidth)
                        ctx.fillStyle = theme.graphPreview
                        ctx.fillRect(x0, top, Math.max(1, x1 - x0), plotHeight)
                    }
                    const inputX = xFor(physicalInput, left, plotWidth)
                    const inputY = yFor(physicalInput, top, plotHeight)
                    const outputY = yFor(transformedOutput, top, plotHeight)
                    ctx.fillStyle = theme.graphInput
                    ctx.strokeStyle = theme.graphFrame
                    ctx.lineWidth = 2
                    ctx.beginPath(); ctx.arc(inputX, inputY, 5, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    ctx.fillStyle = theme.graphOutput
                    ctx.strokeStyle = theme.ivory
                    ctx.beginPath(); ctx.arc(inputX, outputY, 4, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    ctx.fillStyle = theme.graphLabel
                    ctx.font = "10px " + theme.telemetryFont
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
                Connections {
                    target: themeManager
                    function onThemeChanged() { curveCanvas.requestPaint() }
                }
            }
        }
    }
    component ButtonCard: Panel {
        id: buttonCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 204
        color: info && info.pressed ? Qt.rgba(theme.orange.r, theme.orange.g, theme.orange.b, theme.topGun ? 0.17 : 0.22) : (theme.topGun ? "#d80b1b20" : "#ed182128")
        border.color: info && info.pressed ? (theme.topGun ? theme.orange : "#93a3cfda") : theme.border
        ColumnLayout {
            id: buttonContent
            anchors.fill: parent
            anchors.margins: 13
            spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: "BUTTON " + ("0" + buttonCard.info.index).slice(-2)
                    color: theme.topGun ? theme.ivory : "#edf7f7"; font.pixelSize: theme.topGun ? 16 : 13; font.weight: Font.DemiBold; font.family: theme.topGun ? theme.displayFont : root.font.family }
                Item { Layout.fillWidth: true }
                Row { spacing: 5
                    StatusDot { tone: buttonCard.info.pressed ? theme.cyan : theme.textFaint }
                    Text { text: buttonCard.info.pressed ? "PRESSED" : "RELEASED"
                        color: buttonCard.info.pressed ? theme.cyan : theme.textMuted; font.pixelSize: 9; font.bold: true }
                }
            }
            Text { text: "PHYSICAL   " + (buttonCard.info.pressed ? "DOWN" : "UP")
                color: buttonCard.info.pressed ? theme.ivory : theme.textFaint; font.pixelSize: 10
                font.family: theme.telemetryFont; font.bold: buttonCard.info.pressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "GAME OUTPUT"; color: theme.topGun ? theme.ivory : "#8c989d"; font.pixelSize: 9; font.bold: true }
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
                    background: Rectangle { radius: theme.controlRadius
                        color: theme.control; border.color: theme.border }
                    contentItem: Text { leftPadding: 8
                        text: buttonDestination.displayText; color: theme.text
                        verticalAlignment: Text.AlignVCenter; font.pixelSize: 10; font.family: theme.topGun ? theme.displayFont : root.font.family }
                }
            }
            Text { text: buttonCard.info.target > 0
                    ? "VIRTUAL    " + (buttonCard.info.virtualPressed ? "DOWN" : "UP")
                    : "VIRTUAL    UNROUTED"
                color: buttonCard.info.virtualPressed ? theme.ready : theme.textFaint
                font.pixelSize: 9; font.family: theme.telemetryFont; font.bold: buttonCard.info.virtualPressed }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "BUTTON NAME"; color: theme.topGun ? theme.ivory : "#8c989d"; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 108 }
                FlightTextInput { Layout.fillWidth: true; text: buttonCard.info.customName || ""; placeholderText: buttonCard.info.hardwareLabel || "Controller button"
                    onEditingFinished: backend.setButtonCustomName(buttonCard.info.index, text) }
            }
        }
    }

    component PovNativeCard: Panel {
        id: nativeCard
        property var info: null
        Layout.fillWidth: true
        Layout.preferredHeight: 143
        color: info && info.nativeEnabled ? Qt.rgba(theme.ready.r, theme.ready.g, theme.ready.b, 0.14) : (theme.topGun ? "#d80b1b20" : "#ed182128")
        border.color: info && info.nativeEnabled ? theme.ready : theme.border
        function targetIndex(key) {
            for (let index = 0; index < root.nativePovTargetChoices.length; ++index) {
                if (root.nativePovTargetChoices[index].key === key) return index
            }
            return 0
        }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 13; spacing: 6
            RowLayout { Layout.fillWidth: true
                Text { text: "POV " + nativeCard.info.index + " / HAT"; color: theme.topGun ? theme.ivory : "#edf7f7"; font.pixelSize: 12; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                Item { Layout.fillWidth: true }
                Text { text: nativeCard.info.nativeStatus; color: nativeCard.info.nativeAvailable ? theme.ready : theme.warning; font.pixelSize: 9; font.bold: true }
            }
            Text { text: nativeCard.info.centered ? "LIVE STATE   CENTERED" : "LIVE STATE   " + nativeCard.info.direction.toUpperCase() + " · " + nativeCard.info.angle + "°"
                color: nativeCard.info.centered ? theme.textFaint : theme.ivory; font.pixelSize: 10; font.family: theme.telemetryFont }
            FineLine { Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Text { text: "NATIVE vJOY OUTPUT"; color: "#8c989d"; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Rectangle { id: nativeToggle; Layout.preferredWidth: 43; Layout.preferredHeight: 21; radius: 11
                    color: nativeCard.info.nativeEnabled ? theme.orange : theme.control; border.color: nativeCard.info.nativeEnabled ? theme.borderStrong : theme.border
                    Rectangle { width: 15; height: 15; radius: 8; anchors.verticalCenter: parent.verticalCenter
                        x: nativeCard.info.nativeEnabled ? parent.width - width - 3 : 3; color: theme.ivory }
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
        Layout.preferredHeight: 152
        color: info && info.active ? Qt.rgba(theme.orange.r, theme.orange.g, theme.orange.b, theme.topGun ? 0.17 : 0.22) : (theme.topGun ? "#d80b1b20" : "#ed182128")
        border.color: info && info.active ? (theme.topGun ? theme.orange : "#93a3cfda") : theme.border
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 13; spacing: 7
            RowLayout { Layout.fillWidth: true
                Text { text: "POV " + povCard.info.hat + " — " + povCard.info.label.toUpperCase(); color: theme.topGun ? theme.ivory : "#edf7f7"; font.pixelSize: 12; font.weight: Font.DemiBold; font.family: theme.topGun ? theme.displayFont : root.font.family }
                Item { Layout.fillWidth: true }
                Text { text: povCard.info.active ? "ACTIVE" : "IDLE"; color: povCard.info.active ? theme.cyan : theme.textMuted; font.pixelSize: 9; font.bold: true }
            }
            RowLayout { Layout.fillWidth: true
                Text { text: "GAME OUTPUT"; color: theme.topGun ? theme.ivory : "#8c989d"; font.pixelSize: 9; font.bold: true }
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
                color: povCard.info.virtualPressed ? theme.ready : theme.textFaint; font.pixelSize: 9; font.family: theme.telemetryFont; font.bold: povCard.info.virtualPressed }
        }
    }

    background: Rectangle {
        color: theme.background
        gradient: Gradient { GradientStop { position: 0.0
 color: theme.topGun ? "#102127" : "#151a1e" }
 GradientStop { position: 0.55
 color: theme.background }
 GradientStop { position: 1.0
 color: theme.topGun ? "#050d11" : "#0b0e10" } }
        Rectangle { width: parent.width
 height: 1
 color: theme.border
 anchors.top: parent.top }
        Rectangle { width: parent.width * 0.58
 height: 260
 x: -100
 y: parent.height - 100
 radius: 180
 color: theme.topGun ? "#4c33221e" : "#071e2930" }
        Repeater { model: 18
            delegate: Rectangle { width: 1; height: parent.height; x: (index + 1) * parent.width / 19
                color: theme.topGun ? "#55422430" : "#163f5261" }
        }
        Repeater { model: 12
            delegate: Rectangle { height: 1; width: parent.width; y: (index + 1) * parent.height / 13
                color: theme.topGun ? "#55422424" : "#123f5261" }
        }
    }

    header: Rectangle {
        id: headerBar
        height: theme.topGun ? 92 : 58
        color: theme.header
        border.color: theme.border
        border.width: 1
        RowLayout {
            visible: !theme.topGun
            anchors.fill: parent
 anchors.leftMargin: theme.topGun ? 26 : 18
 anchors.rightMargin: 20
 spacing: 12
            ToolButton {
                id: menuButton
                Layout.preferredWidth: theme.topGun ? 34 : 30
 Layout.preferredHeight: theme.topGun ? 34 : 30
                text: root.menuOpen ? "×" : "☰"
                font.pixelSize: 19
                onClicked: root.menuOpen = !root.menuOpen
                background: Rectangle { radius: theme.controlRadius
 color: menuButton.hovered || root.menuOpen ? theme.controlHover : theme.control
 border.color: theme.border }
                contentItem: Text { text: menuButton.text
 color: theme.text
 horizontalAlignment: Text.AlignHCenter
 verticalAlignment: Text.AlignVCenter }
            }
            RowLayout { visible: !theme.topGun; spacing: 8
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
     color: theme.textStrong
     font.pixelSize: 15
     font.bold: true }
                    Text { text: "FLIGHT CONTROL INTERFACE"
     color: theme.textMuted
     font.pixelSize: 9
     font.bold: true }
                }
            }
            Item {
                visible: theme.topGun
                Layout.preferredWidth: 226
                Layout.preferredHeight: 58
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Item {
                        width: 68; height: 40
                        Repeater { model: 3; delegate: Rectangle { width: 28 + index * 8; height: 4; y: 5 + index * 8; x: 0; color: index === 1 ? theme.orange : theme.ivory } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "★"; color: theme.ivory; font.pixelSize: 17 }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter; spacing: -2
                        Text { text: "HOTAS BF6"; color: theme.ivory; font.family: theme.displayFont; font.pixelSize: 22; font.bold: true }
                        Text { text: "FLIGHT CONTROL INTERFACE"; color: theme.textMuted; font.family: theme.displayFont; font.pixelSize: 9; font.bold: true }
                    }
                }
            }
            FineLine { Layout.preferredWidth: 1
 Layout.preferredHeight: 24 }
            Row { spacing: 7
                StatusDot { tone: root.physicalStatusColor() }
                Text { text: backend.physicalConnected ? backend.deviceName : "Controller not connected"
 color: theme.text
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
                    StatusDot { tone: backend.vjoyReady ? root.capacityColor() : theme.textMuted }
                Text { text: "VJOY " + backend.vjoyDeviceId
                    color: theme.text; font.pixelSize: 10; font.bold: true }
                Text { text: backend.vjoyReady ? root.capacityState() : "OFFLINE"
                    color: backend.vjoyReady ? root.capacityColor() : theme.textMuted; font.pixelSize: 10; font.bold: true }
            }
            FineLine { visible: root.width >= 1250 || backend.profileSourceLabel !== "Manual base profile"; Layout.preferredWidth: 1
                Layout.preferredHeight: 24 }
            Row { visible: root.width >= 1250 || backend.profileSourceLabel !== "Manual base profile"; spacing: 6
                Text { text: "PROFILE"
                    color: theme.textMuted; font.pixelSize: 9; font.bold: true }
                Text { text: backend.effectiveProfileDisplayName.toUpperCase()
                    color: theme.text; font.pixelSize: 10; font.bold: true
                    elide: Text.ElideRight; width: Math.min(128, implicitWidth) }
                Text { visible: backend.profileSourceLabel !== "Manual base profile"
                    text: "· " + backend.profileSourceLabel.toUpperCase()
                    color: theme.ready; font.pixelSize: 9; font.bold: true }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: backend.updateAvailable && root.width >= 980
                width: theme.topGun ? 31 : 27; height: 24; radius: theme.controlRadius
                color: updateIndicatorMouse.containsMouse ? theme.controlHover : theme.control
                border.color: theme.topGun ? theme.orange : theme.borderStrong
                Text { anchors.centerIn: parent; text: theme.topGun ? "UP" : "⇧"
                    color: theme.topGun ? theme.orangeBright : theme.textStrong
                    font.pixelSize: theme.topGun ? 10 : 16; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                MouseArea { id: updateIndicatorMouse; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: backend.handoffToLauncher() }
                ToolTip { visible: updateIndicatorMouse.containsMouse; delay: 350
                    text: "HOTAS BF6 " + backend.updateAvailableVersion + " is available"
                    background: Panel { color: theme.tooltip; border.color: theme.topGun ? theme.orange : theme.borderStrong }
                    contentItem: Text { text: parent.text; color: theme.text; font.pixelSize: 10 } }
            }
            Rectangle {
                id: globalMappingControl
                implicitWidth: mappingControlRow.implicitWidth + 18; implicitHeight: 30
                radius: theme.topGun ? 1 : theme.controlRadius
                color: mappingControlMouse.pressed ? theme.controlPressed : mappingControlMouse.containsMouse ? theme.controlHover : theme.control
                border.color: backend.mappingActive ? theme.ready : backend.mappingRequested ? theme.warning : theme.borderStrong
                Row { id: mappingControlRow; anchors.centerIn: parent; spacing: 7
                    StatusDot { tone: backend.mappingActive ? theme.ready : (backend.mappingRequested ? theme.warning : (backend.vjoyReady ? theme.cyan : theme.textMuted)) }
                    Text { text: backend.mappingStatus
                        color: backend.mappingActive ? theme.ready : (backend.mappingRequested ? theme.warning : theme.textMuted)
                        font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                    Rectangle { visible: theme.topGun; width: 30; height: 4; anchors.verticalCenter: parent.verticalCenter; color: theme.orange
                        Repeater { model: 4; delegate: Rectangle { x: index * 8; width: 3; height: parent.height; color: theme.background } } }
                }
                MouseArea { id: mappingControlMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: backend.toggleMapping() }
                ToolTip { visible: mappingControlMouse.containsMouse; delay: 350; text: backend.mappingRequested ? "Stop Mapping" : "Start Mapping" }
            }
            Item { visible: theme.topGun; Layout.preferredWidth: 26; Layout.preferredHeight: 44
                Text { anchors.centerIn: parent; text: "✦"; color: theme.ivory; font.pixelSize: 28 }
            }
        }
        Item {
            visible: theme.topGun
            anchors.fill: parent
            ToolButton {
                id: topGunMenu
                x: 10; y: 28; width: 26; height: 30
                text: root.menuOpen ? "×" : "☰"
                font.pixelSize: 16
                onClicked: root.menuOpen = !root.menuOpen
                background: Rectangle { color: "#0a1519"; border.color: theme.border; radius: 1 }
                contentItem: Text { text: topGunMenu.text; color: theme.ivory; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Image { x: 42; y: 12; width: 242; height: 70; source: "qrc:/assets/themes/topgun/wing-badge.svg"; fillMode: Image.PreserveAspectFit; smooth: true }
            Rectangle { x: 300; y: 14; width: 1; height: 64; color: theme.divider }
            Row {
                x: 318; y: 15; height: 62; spacing: 0
                Repeater {
                    visible: root.width >= 960
                    model: [
                        { label: backend.physicalConnected ? backend.deviceName.toUpperCase() : "T.FLIGHT HOTAS ONE", value: root.physicalStatusText(), color: root.physicalStatusColor() },
                        { label: "VJOY " + backend.vjoyDeviceId, value: backend.vjoyReady ? "READY" : "OFFLINE", color: backend.vjoyReady ? theme.cyan : theme.warning },
                        { label: "PROFILE", value: backend.effectiveProfileDisplayName.toUpperCase(), color: theme.ivory }
                    ]
                    delegate: Item {
                        required property var modelData
                        width: index === 0 ? 184 : index === 1 ? 116 : 144; height: parent.height
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: index === 2 ? "transparent" : "#7c442f" }
                        Column { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 14; spacing: 7
                            Row { spacing: 7
                                Rectangle { width: 8; height: 8; radius: 4; color: modelData.color; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.label; color: theme.ivory; font.family: theme.displayFont; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: parent.parent.width - 34 }
                            }
                            Text { text: modelData.value; color: modelData.color; font.family: theme.displayFont; font.pixelSize: 17; font.bold: true }
                        }
                    }
                }
            }
            Item { visible: root.width >= 1180; anchors.right: parent.right; anchors.rightMargin: 18; y: 8; width: 278; height: 76
                Image { anchors.right: parent.right; y: 3; width: 66; height: 62; source: "qrc:/assets/themes/topgun/fighter-silhouette.svg"; fillMode: Image.PreserveAspectFit; opacity: 0.94 }
                Image { x: 0; y: 31; width: 80; height: 20; source: "qrc:/assets/themes/topgun/slash-stripes.svg"; fillMode: Image.PreserveAspectFit }
                Text { x: 85; y: 28; text: backend.mappingStatus; color: backend.mappingActive ? theme.orangeBright : theme.textMuted; font.family: theme.displayFont; font.pixelSize: 15; font.bold: true }
                Rectangle { x: 0; y: 54; width: 238; height: 1; color: theme.orange }
                Rectangle { x: 104; y: 58; width: 120; height: 2; color: "#8d291d" }
                MouseArea { id: topGunMappingMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: backend.toggleMapping() }
                ToolTip { visible: topGunMappingMouse.containsMouse; text: backend.mappingRequested ? "Stop Mapping" : "Start Mapping" }
            }
        }
    }

    footer: Rectangle {
        visible: theme.topGun
        height: visible ? 78 : 0
        color: "#0a1519"
        border.color: theme.border
        Rectangle { anchors.fill: parent; anchors.margins: 3; color: "transparent"; border.color: "#4d3b25" }
        Image { anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter; width: 285; height: 60; source: "qrc:/assets/themes/topgun/wing-badge.svg"; fillMode: Image.PreserveAspectFit }
        Text { anchors.left: parent.left; anchors.leftMargin: Math.min(350, parent.width * 0.29); anchors.verticalCenter: parent.verticalCenter; text: "I FEEL THE NEED... THE NEED FOR SPEED!"; color: "#d53926"; font.family: theme.displayFont; font.pixelSize: 14; font.bold: true }
        Image { anchors.horizontalCenter: parent.horizontalCenter; anchors.horizontalCenterOffset: 160; anchors.verticalCenter: parent.verticalCenter; width: 210; height: 62; source: "qrc:/assets/themes/topgun/fighter-silhouette.svg"; fillMode: Image.PreserveAspectFit }
        Rectangle { anchors.right: footerRight.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; width: 100; height: 1; color: theme.orange }
        Item { id: footerRight; anchors.right: parent.right; anchors.rightMargin: 22; anchors.verticalCenter: parent.verticalCenter; width: 222; height: 56
            Text { anchors.right: parent.right; y: 5; text: "DANGER ZONE"; color: "#d7442b"; font.family: theme.displayFont; font.pixelSize: 18; font.bold: true }
            Image { anchors.right: parent.right; y: 28; width: 126; height: 22; source: "qrc:/assets/themes/topgun/slash-stripes.svg"; fillMode: Image.PreserveAspectFit }
            Text { anchors.right: parent.right; anchors.bottom: parent.bottom; text: "★"; color: theme.orange; font.pixelSize: 17 }
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
        height: 417
        opacity: root.menuOpen ? 1 : 0
        scale: root.menuOpen ? 1 : 0.97
        visible: root.menuOpen
        color: theme.tooltip
        border.color: theme.borderStrong
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
                    Text { text: theme.topGun ? "HOTAS BF6  //  FLIGHT DECK" : "HOTAS BF6"; color: theme.textStrong; font.pixelSize: 13; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                    Text { text: "v" + Qt.application.version + (theme.topGun ? "  ·  TOP GUN" : ""); color: theme.textMuted; font.pixelSize: 9; font.bold: true }
                }
            }
            Rectangle {
                x: 7
                width: parent.width - 14
                height: 1
                color: theme.divider
            }
            Repeater {
                model: [
                    { label: "OVERVIEW", page: 8, future: false }, { label: "AXES", page: 0, future: false }, { label: "BUTTONS", page: 1, future: false },
                    { label: "PROFILES", page: 5, future: false }, { label: "CURVE EDITOR", page: 6, future: false },
                    { label: "AUTOMATION", page: 7, future: false }, { label: "CALIBRATION", page: 2, future: false },
                    { label: "DIAGNOSTICS", page: 3, future: false }, { label: "SETTINGS", page: 4, future: false }
                ]
                delegate: Item {
                    width: parent.width
 height: modelData.page < 0 ? 12 : 35
                    Rectangle { visible: modelData.page < 0
 anchors.verticalCenter: parent.verticalCenter
 width: parent.width - 14
 x: 7
 height: 1
 color: theme.divider }
                    Rectangle { visible: modelData.page >= 0
 anchors.fill: parent
 radius: theme.controlRadius
 color: root.currentPage === modelData.page ? theme.selection : navHit.containsMouse ? theme.controlHover : "transparent"
 border.color: root.currentPage === modelData.page ? theme.orange : "transparent" }
                    Row { visible: modelData.page >= 0
 anchors.verticalCenter: parent.verticalCenter
 anchors.left: parent.left
 anchors.leftMargin: 11
 spacing: 8
                        Rectangle { width: 4
 height: 4
 radius: theme.topGun ? 0 : 2
 anchors.verticalCenter: parent.verticalCenter
 color: root.currentPage === modelData.page ? theme.orangeBright : theme.textFaint }
                        Text { text: modelData.label
 color: modelData.future ? theme.textFaint : (root.currentPage === modelData.page ? theme.textStrong : theme.text)
 font.pixelSize: 11
 font.bold: root.currentPage === modelData.page
 font.family: theme.topGun ? theme.displayFont : root.font.family }
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
        // Only the selected page owns a QML object tree. Editor and import
        // drafts are copied into lightweight root-owned state before unload.
        Loader {
            id: overviewPageLoader
            anchors.fill: parent
            active: root.currentPage === 8
            sourceComponent: Component {
                OverviewPage { anchors.fill: parent; visible: root.currentPage === 8; legacy: false }
            }
        }
        Loader {
            id: settingsPageLoader
            anchors.fill: parent
            active: root.currentPage === 4
            sourceComponent: Component {
                SettingsPage { anchors.fill: parent; visible: root.currentPage === 4; legacy: false }
            }
        }
        Loader {
            id: profileLibraryLoader
            anchors.fill: parent
            active: root.currentPage === 5
            sourceComponent: Component {
                ProfileLibrary { anchors.fill: parent; visible: root.currentPage === 5; backendObject: backend; legacy: false
                    presentationState: root.profileLibraryPresentationState
                    onPresentationStateCaptured: function(state) { root.profileLibraryPresentationState = state }
                    onNavigateToPage: function(page) { root.currentPage = page } }
            }
        }
        Loader {
            id: axesPageLoader
            anchors.fill: parent
            active: root.currentPage === 0
            sourceComponent: Component {
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
                }
                Panel { width: parent.width; height: 90
                    color: theme.topGun ? "#e4191714" : "#e51a2328"; border.color: theme.topGun ? theme.orange : "#46657980"
                    RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 16
                        Rectangle { visible: theme.topGun; Layout.preferredWidth: 142; Layout.fillHeight: true; color: "#4d211b"; border.color: theme.orange
                            clip: true
                            Repeater { model: 9; delegate: Rectangle { width: 42; height: 2; x: index * 18 - 35; y: parent.height - 6; rotation: -28; color: "#9c3a20" } }
                            Column { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12; spacing: 3
                                Text { text: "SELECT AXIS"; color: theme.orangeBright; font.family: theme.displayFont; font.pixelSize: 12; font.bold: true }
                                Text { text: root.hasPhysicalInput ? backend.physicalAxisCapabilitySummary : "WAITING"; color: theme.ivory; font.family: theme.displayFont; font.pixelSize: 9; font.bold: true }
                            }
                        }
                        Column { visible: !theme.topGun; spacing: 4; Layout.preferredWidth: 96
                            Text { text: "SELECT AXIS"; color: "#89a4ad"; font.pixelSize: 10; font.bold: true }
                            Text { text: root.hasPhysicalInput ? backend.physicalAxisCapabilitySummary : "WAITING"; color: "#77919a"; font.pixelSize: 9; font.bold: true }
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
                            background: Rectangle { radius: theme.controlRadius; color: theme.control; border.color: theme.topGun ? theme.orange : "#546d78" }
                            contentItem: Text { leftPadding: 12; text: axisSelector.displayText
                                color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
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
                                color: theme.topGun ? "#de0b1a1f" : "#e61a282e"; border.color: theme.topGun ? theme.borderStrong : "#4b70818a"
                                ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 7
                                    RowLayout { Layout.fillWidth: true
                                        Column { spacing: 2
                                            Text { text: axisIdentityPanel.info.label.toUpperCase(); color: theme.topGun ? theme.ivory : "#edf6f6"; font.pixelSize: theme.topGun ? 24 : 17; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                                            Text { text: (axisIdentityPanel.info.fixed ? axisIdentityPanel.info.activityLabel + " · " + axisIdentityPanel.info.activityDetail : axisIdentityPanel.info.detail).toUpperCase(); color: axisIdentityPanel.info.fixed ? theme.warning : theme.textMuted; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                                        }
                                        Item { Layout.fillWidth: true }
                                        Row { spacing: 6
                                            StatusDot { tone: root.physicalStatusColor() }
                                            Text { text: root.physicalStatusText(); color: root.physicalStatusColor(); font.pixelSize: 10; font.bold: true }
                                        }
                                    }
                                    FineLine { Layout.fillWidth: true }
                                    RowLayout { Layout.fillWidth: true
                                        TelemetryItem { caption: "PROFILE"; value: backend.activeProfileName.toUpperCase(); tone: theme.ivory; Layout.fillWidth: true }
                                        TelemetryItem { caption: "ROUTE"; value: axisIdentityPanel.info.target.toUpperCase(); tone: axisIdentityPanel.info.target === "Disabled" ? theme.textMuted : theme.orangeBright; Layout.fillWidth: true }
                                        TelemetryItem { caption: "STATUS"; value: backend.mappingActive ? "● LIVE" : "STANDBY"; tone: backend.mappingActive ? theme.ready : theme.textMuted; Layout.fillWidth: true }
                                    }
                                }
                            }
                            Panel { id: liveTelemetryPanel; Layout.fillWidth: true; Layout.preferredHeight: 122
                                property var info: root.selectedAxisInfo
                                RowLayout { anchors.fill: parent; anchors.margins: 17; spacing: 20
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Text { text: "CALIBRATED INPUT"; color: theme.textMuted; font.pixelSize: 10; font.bold: true }
                                        Text { text: root.controlValue(liveTelemetryPanel.info, liveTelemetryPanel.info.calibrated); color: theme.topGun ? theme.ivory : "#dce8ea"; font.pixelSize: 31; font.family: theme.telemetryFont; font.bold: true }
                                        Text { text: liveTelemetryPanel.info.detail.toUpperCase(); color: theme.textFaint; font.pixelSize: 9; font.bold: true }
                                    }
                                    FineLine { Layout.preferredWidth: 1; Layout.preferredHeight: 64 }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Text { text: "VIRTUAL OUTPUT"; color: theme.topGun ? theme.cyan : "#85aaa8"; font.pixelSize: 10; font.bold: true }
                                        Text { text: root.outputState(liveTelemetryPanel.info); color: liveTelemetryPanel.info.virtualRouted ? theme.cyan : theme.textMuted; font.pixelSize: liveTelemetryPanel.info.virtualRouted ? 31 : 18; font.family: theme.telemetryFont; font.bold: true }
                                        Text { text: liveTelemetryPanel.info.virtualRouted ? (backend.mappingActive ? "LIVE GAME-FACING OUTPUT" : "OUTPUT STANDBY") : "UNMAPPED VJOY AXES PARKED AT " + Number(backend.disabledAxisValue).toFixed(1) + "%"; color: theme.textFaint; font.pixelSize: 9; font.bold: true }
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
                            color: theme.topGun ? "#dc0a171c" : "#ed151d22"
                            ColumnLayout { id: processingContent; anchors.fill: parent; anchors.margins: 16; spacing: 12
                                Text { text: (theme.topGun ? "✦  " : "") + "AXIS PROCESSING"; color: theme.topGun ? theme.ivory : "#e1eded"; font.pixelSize: theme.topGun ? 18 : 12; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                                Text { text: "Profile-specific settings compile into the worker between reports."; color: theme.textMuted; font.pixelSize: 9; font.bold: true }
                                FineLine { Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "ROUTE"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
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
                                        background: Rectangle { radius: theme.controlRadius; color: theme.control; border.color: theme.topGun ? theme.orange : "#455d67" }
                                        contentItem: Text { leftPadding: 9; text: selectedAxisDestination.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.family: theme.topGun ? theme.displayFont : root.font.family }
                                    }
                                }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "AXIS NAME"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightTextInput { Layout.fillWidth: true; text: processingPanel.info.customName || ""; placeholderText: processingPanel.info.hardwareLabel || "Controller axis"
                                        onEditingFinished: backend.setAxisCustomName(processingPanel.info.index, text) }
                                }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "RANGE"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightComboBox { Layout.fillWidth: true; Layout.preferredHeight: 31; model: ["Centered", "One-Sided"]
                                        currentIndex: processingPanel.info.rangeMode === "oneSided" ? 1 : 0
                                        onActivated: backend.setAxisRangeMode(processingPanel.info.index, currentText) }
                                }
                                RowLayout { Layout.fillWidth: true; visible: processingPanel.info.target !== "Disabled"
                                    Text { text: "VJOY NAME"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightTextInput { Layout.fillWidth: true; text: processingPanel.info.outputAlias || ""; placeholderText: processingPanel.info.target + " alias (e.g. R Up/Down)"
                                        onEditingFinished: backend.setVirtualAxisAlias(processingPanel.info.target, text) }
                                }
                                Text { visible: processingPanel.info.target !== "Disabled" && !processingPanel.info.targetAvailable
                                    text: "The selected vJoy device does not expose this axis."; color: theme.warning; font.pixelSize: 9; Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "INVERT"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    Switch {
                                        id: selectedAxisInvert
                                        checked: processingPanel.info.inverted
                                        onToggled: backend.setAxisInverted(processingPanel.info.index, checked)
                                        indicator: Rectangle { implicitWidth: 36; implicitHeight: 18; radius: 9
                                            color: selectedAxisInvert.checked ? theme.orange : theme.control; border.color: theme.borderStrong
                                            Rectangle { width: 14; height: 14; radius: 7; x: selectedAxisInvert.checked ? 19 : 3
                                                anchors.verticalCenter: parent.verticalCenter; color: theme.ivory }
                                        }
                                    }
                                    Text { text: processingPanel.info.inverted ? "ON" : "OFF"; color: processingPanel.info.inverted ? theme.orangeBright : theme.textMuted; font.pixelSize: 10; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                }
                                FineLine { Layout.fillWidth: true }
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: "DEADZONE"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (Number(processingPanel.info.deadzone) * 100).toFixed(1) + "%"; color: theme.topGun ? theme.ivory : "#c8dce0"; font.pixelSize: 11; font.family: theme.telemetryFont; font.bold: true }
                                    }
                                    Slider { id: selectedAxisDeadzone; Layout.fillWidth: true; from: 0; to: 0.25; value: Number(processingPanel.info.deadzone)
                                        onMoved: backend.setAxisDeadzone(processingPanel.info.index, value)
                                        background: Rectangle { x: selectedAxisDeadzone.leftPadding; y: selectedAxisDeadzone.topPadding + selectedAxisDeadzone.availableHeight / 2 - height / 2; width: selectedAxisDeadzone.availableWidth; height: theme.topGun ? 3 : 5; color: theme.panelInset; border.color: theme.border
                                            Rectangle { width: selectedAxisDeadzone.visualPosition * parent.width; height: parent.height; color: theme.topGun ? theme.orange : theme.cyan }
                                            Repeater { visible: theme.topGun; model: 20; delegate: Rectangle { x: index * parent.width / 19; width: 1; height: 7; y: -2; color: theme.borderStrong } }
                                        }
                                        handle: Rectangle { x: selectedAxisDeadzone.leftPadding + selectedAxisDeadzone.visualPosition * (selectedAxisDeadzone.availableWidth - width); y: selectedAxisDeadzone.topPadding + selectedAxisDeadzone.availableHeight / 2 - height / 2; width: theme.topGun ? 22 : 14; height: theme.topGun ? 22 : 14; radius: width / 2; color: theme.ivory; border.color: theme.borderStrong } }
                                    Text { text: "Rescaled around center before hysteresis."; color: theme.textFaint; font.pixelSize: 9 }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: "HYSTERESIS"; color: theme.topGun ? theme.ivory : "#a7bbc0"; font.pixelSize: 10; font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (Number(processingPanel.info.hysteresis) * 100).toFixed(2) + "%"; color: theme.topGun ? theme.ivory : "#c8dce0"; font.pixelSize: 11; font.family: theme.telemetryFont; font.bold: true }
                                    }
                                    Slider { id: selectedAxisHysteresis; Layout.fillWidth: true; from: 0; to: 0.05; value: Number(processingPanel.info.hysteresis)
                                        onMoved: backend.setAxisHysteresis(processingPanel.info.index, value)
                                        background: Rectangle { x: selectedAxisHysteresis.leftPadding; y: selectedAxisHysteresis.topPadding + selectedAxisHysteresis.availableHeight / 2 - height / 2; width: selectedAxisHysteresis.availableWidth; height: theme.topGun ? 3 : 5; color: theme.panelInset; border.color: theme.border
                                            Rectangle { width: selectedAxisHysteresis.visualPosition * parent.width; height: parent.height; color: theme.topGun ? theme.orange : theme.cyan }
                                            Repeater { visible: theme.topGun; model: 20; delegate: Rectangle { x: index * parent.width / 19; width: 1; height: 7; y: -2; color: theme.borderStrong } }
                                        }
                                        handle: Rectangle { x: selectedAxisHysteresis.leftPadding + selectedAxisHysteresis.visualPosition * (selectedAxisHysteresis.availableWidth - width); y: selectedAxisHysteresis.topPadding + selectedAxisHysteresis.availableHeight / 2 - height / 2; width: theme.topGun ? 22 : 14; height: theme.topGun ? 22 : 14; radius: width / 2; color: theme.ivory; border.color: theme.borderStrong } }
                                    Text { text: "Suppresses sub-threshold report noise; no smoothing delay."; color: theme.textFaint; font.pixelSize: 9 }
                                }
                                FineLine { Layout.fillWidth: true }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: processingPanel.info.unipolar ? "OUTPUT MIN" : "OUTPUT MIN"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightNumericStepper { id: outputMinimum; from: processingPanel.info.unipolar ? 0 : -100; to: 99
                                        value: Number(processingPanel.info.outputMinimum) * 100
                                        onValueEdited: function(nextValue) { backend.setAxisOutputLimits(processingPanel.info.index, nextValue / 100, Number(processingPanel.info.outputMaximum)) } }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "OUTPUT MAX"; color: "#a7bbc0"; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 92 }
                                    FlightNumericStepper { id: outputMaximum; from: processingPanel.info.unipolar ? 1 : -99; to: 100
                                        value: Number(processingPanel.info.outputMaximum) * 100
                                        onValueEdited: function(nextValue) { backend.setAxisOutputLimits(processingPanel.info.index, Number(processingPanel.info.outputMinimum), nextValue / 100) } }
                                    Item { Layout.fillWidth: true }
                                }
                                Text { text: processingPanel.info.unipolar ? "One-sided transfer and limits use 0–100%; this never restores a centered response." : "Limits constrain final virtual authority, not physical calibration."; color: "#718a93"; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
            }
        }
        Loader {
            id: buttonsPageLoader
            anchors.fill: parent
            active: root.currentPage === 1
            sourceComponent: Component {
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
                Text { visible: backend.legacyControlMigrationWarning.length > 0; width: parent.width
                    text: backend.legacyControlMigrationWarning + " Configure a replacement in Automation."
                    color: theme.warning; font.pixelSize: 10; wrapMode: Text.WordWrap }
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
            }
        }
        Loader {
            id: calibrationPageLoader
            anchors.fill: parent
            active: root.currentPage === 2
            sourceComponent: Component {
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
 detail: backend.calibrationStatus }
                    Item { Layout.fillWidth: true }
                    CommandButton { label: backend.calibrationStage === "RANGE" ? "CAPTURING RANGE" : "START CALIBRATION"
 commandEnabled: backend.calibrationStage === "IDLE"
 onTriggered: backend.beginCalibration() }
                    CommandButton { label: "NEXT — CAPTURE CENTER"
 commandEnabled: backend.calibrationStage === "RANGE"
 subdued: true
 onTriggered: backend.beginCalibrationCenterCapture() }
                    CommandButton { label: backend.calibrationStage === "FINALIZING" ? "MEASURING CENTER" : "COMPLETE CALIBRATION"
 commandEnabled: backend.calibrationStage === "CENTER"
 subdued: true
 onTriggered: backend.saveCalibration() }
                    CommandButton { label: "RESET"
 subdued: true
 onTriggered: backend.resetCalibration() }
                }
                Text { visible: backend.calibrationStage === "RANGE"
                    width: parent.width
                    text: "STEP 1 OF 2 — Move the stick fully left/right and forward/back, twist, throttle, paddles, sliders, and every additional axis through its complete travel several times."
                    color: theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                Text { visible: backend.calibrationStage === "CENTER" || backend.calibrationStage === "FINALIZING"
                    width: parent.width
                    text: "STEP 2 OF 2 — Release spring-centered controls and let them rest naturally. Do not touch the stick, twist, rudder, or paddles. Throttles and sliders do not need to be centered."
                    color: theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                Panel { visible: backend.calibrationSuccess; width: parent.width; height: calibrationSuccessContent.implicitHeight + 24
                    color: theme.topGun ? "#202015" : "#16262a"; border.color: theme.topGun ? theme.orange : theme.ready
                    Column { id: calibrationSuccessContent; anchors.fill: parent; anchors.margins: 12; spacing: 4
                        Text { text: "CALIBRATION SUCCESSFUL"; color: theme.topGun ? theme.orangeBright : theme.ready; font.pixelSize: 13; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                        Text { text: backend.calibrationStatus; color: theme.text; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap }
                        Text { text: "You can start a new calibration whenever you are ready."; color: theme.textMuted; font.pixelSize: 10 }
                    }
                }
                Text { text: "AXIS RANGE STATUS"
 color: theme.textMuted
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
                            color: backend.calibrationActive ? Qt.rgba(theme.orange.r, theme.orange.g, theme.orange.b, 0.14) : (theme.topGun ? "#d80b1b20" : "#ed182128")
                            border.color: backend.calibrationActive ? theme.orange : theme.border
                            RowLayout { anchors.fill: parent
 anchors.margins: 15
                                ColumnLayout { Layout.preferredWidth: 130
                                    Text { text: calibrationAxisCard.info.label.toUpperCase()
 color: theme.topGun ? theme.ivory : "#eaf0f1"
 font.pixelSize: theme.topGun ? 15 : 12
 font.bold: true
 font.family: theme.topGun ? theme.displayFont : root.font.family }
                                    Text { text: backend.calibrationStage === "RANGE" ? "STEP 1 · RANGE" : backend.calibrationStage === "CENTER" ? "STEP 2 · RELEASE CENTERED" : backend.calibrationStage === "FINALIZING" ? "MEASURING CENTER" : (calibrationAxisCard.info.calibrationEnabled ? (calibrationAxisCard.info.calibrationCentered ? "CALIBRATED CENTER" : "CALIBRATED RANGE") : "RAW DEFAULT")
 color: backend.calibrationActive ? theme.orangeBright : (calibrationAxisCard.info.calibrationEnabled ? theme.ready : theme.textFaint)
 font.pixelSize: 9 }
                                }
                                Repeater { model: [{ n: "MIN", v: calibrationAxisCard.info.calibrationMinimum }, { n: "CURRENT", v: calibrationAxisCard.info.raw }, { n: "MAX", v: calibrationAxisCard.info.calibrationMaximum }]
                                    delegate: Column { Layout.fillWidth: true
 spacing: 5
                                        Text { text: modelData.n
 color: theme.textMuted
 font.pixelSize: 9
 font.bold: true }
                                        Text { text: Number(modelData.v).toFixed(3)
 color: theme.ivory
 font.pixelSize: 13
 font.family: theme.telemetryFont }
                                    }
                                }
                            }
                        }
                    }
                }
                Panel { width: parent.width; height: calibrationHistoryContent.implicitHeight + 24
                    color: theme.topGun ? "#131b1d" : theme.panel; border.color: theme.border
                    Column { id: calibrationHistoryContent; anchors.fill: parent; anchors.margins: 12; spacing: 8
                        Text { text: "CALIBRATION HISTORY"; color: theme.topGun ? theme.orangeBright : theme.textStrong; font.pixelSize: 11; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                        Text { visible: backend.calibrationHistory.length === 0; text: "Successful calibrations for this and other saved controllers appear here."; color: theme.textMuted; font.pixelSize: 10 }
                        Repeater { model: backend.calibrationHistory
                            delegate: Rectangle { width: calibrationHistoryContent.width; height: 44; color: modelData.currentDevice ? (theme.topGun ? "#2a2419" : theme.panelRaised) : theme.control; border.color: modelData.currentDevice ? (theme.topGun ? theme.orange : theme.ready) : theme.border; radius: theme.topGun ? 1 : theme.controlRadius
                                Column { anchors.left: parent.left; anchors.leftMargin: 9; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 18; spacing: 2
                                    Text { text: modelData.name + (modelData.currentDevice ? "  ·  CURRENT DEVICE" : ""); color: theme.text; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; width: parent.width; font.family: theme.topGun ? theme.displayFont : root.font.family }
                                    Text { text: modelData.when + "  ·  " + modelData.axes + " axes calibrated"; color: theme.textMuted; font.pixelSize: 9 }
                                }
                            }
                        }
                    }
                }
            }
        }
            }
        }
        Loader {
            id: diagnosticsPageLoader
            anchors.fill: parent
            active: root.currentPage === 3
            sourceComponent: Component {
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
                detail: "Worker-side DirectInput telemetry; presentation samples the latest snapshot at 30 Hz" }
                GridLayout { width: parent.width
 columns: width >= 1100 ? 5 : (width >= 760 ? 3 : 2)
 columnSpacing: 10
 rowSpacing: 10
                    Repeater { model: [
                        { c: "PHYSICAL RATE", v: backend.inputReportsPerSecond.toFixed(0) + " HZ", t: backend.inputReportsPerSecond > 0 ? theme.cyan : theme.textMuted },
                        { c: "UPDATE AGE", v: backend.lastPhysicalUpdateAgeMs >= 0 ? backend.lastPhysicalUpdateAgeMs + " MS" : "—", t: backend.lastPhysicalUpdateAgeMs >= 0 && backend.lastPhysicalUpdateAgeMs < 100 ? theme.cyan : theme.textMuted },
                        { c: "MAP LATENCY", v: backend.latencyCurrentUs + " US", t: theme.ivory },
                        { c: "MAP p95 / p99", v: backend.latencyP95Us + " / " + backend.latencyP99Us + " US", t: theme.ivory,
                          note: "ROLLING LAST 2,048 PHYSICAL REPORTS" },
                        { c: "MAP AVG / PEAK", v: backend.latencyAverageUs + " / " + backend.latencyPeakUs + " US", t: theme.ivory,
                          note: "LIFETIME SINCE MAPPING START" },
                        { c: "VJOY WRITES", v: backend.vjoyWritesPerSecond.toFixed(0) + " / S", t: backend.vjoyReady ? theme.cyan : theme.textFaint,
                          note: backend.vjoyWritesPerSecond > 0 ? "ACTIVE · CHANGE-DRIVEN" : "IDLE · CHANGE-DRIVEN" },
                        { c: "MAPPING", v: backend.mappingStatus, t: backend.mappingActive ? theme.ready : (backend.mappingRequested ? theme.warning : theme.textMuted),
                          note: backend.mappingActive ? "OUTPUT ACQUIRED" : (backend.mappingRequested ? "OUTPUT QUIESCED; WILL REACQUIRE" : "OUTPUT NEUTRALIZED") },
                        { c: "BASE PROFILE", v: backend.activeProfileName.toUpperCase(), t: theme.ivory,
                          note: "PERSISTENT MANUAL SELECTION" },
                        { c: "EFFECTIVE PROFILE", v: backend.effectiveProfileName.toUpperCase(), t: theme.ivory,
                          note: backend.profileSourceLabel.toUpperCase() },
                        { c: "PROFILE SWAP", v: backend.lastProfileSwapUs + " US", t: theme.ivory,
                          note: backend.profileSwitchCount + " LIVE SWITCHES" },
                        { c: "AUTOMATION", v: backend.automationEngineEnabled ? "ENABLED" : "DISABLED", t: backend.automationEngineEnabled ? theme.ready : theme.warning,
                          note: backend.automationRuleCount + " RULES / " + backend.automationActiveRuleCount + " ACTIVE" },
                        { c: "AUTO EVAL", v: backend.automationEvaluationUs + " US", t: theme.ivory,
                          note: "WORKER-SIDE RULE EVALUATION" }
                    ]
                        delegate: Panel { Layout.fillWidth: true
 Layout.preferredHeight: 86
                            Column { anchors.fill: parent
 anchors.margins: 13
 spacing: 4
                                Text { text: modelData.c
 color: theme.textMuted
 font.pixelSize: 9
 font.bold: true }
                                Text { text: modelData.v
 color: modelData.t
 font.pixelSize: 16
 font.bold: true
 font.family: theme.telemetryFont }
                                Text { visible: modelData.note !== undefined; text: modelData.note || ""
                                    color: theme.textFaint; font.pixelSize: 8; font.bold: true }
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
                            Text { text: backend.vjoyReady ? backend.vjoyStatus : "Physical monitoring remains independent"
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
 Layout.preferredHeight: 132
                            Column { anchors.fill: parent
 anchors.margins: 12
 spacing: 4
                                Text { text: diagnosticAxisCard.info.label.toUpperCase()
 color: "#aebcc0"
 font.pixelSize: 9
 font.bold: true }
                                Text { text: "RAW       " + root.valuePercent(diagnosticAxisCard.info.raw)
 color: "#dfeaec"
 font.pixelSize: 12
 font.family: "Consolas" }
                                Text { text: "CALIBRATED " + root.controlValue(diagnosticAxisCard.info, diagnosticAxisCard.info.calibrated)
 color: "#a9cad2"
 font.pixelSize: 10
 font.family: "Consolas" }
                                Text { text: "MAPPED    " + root.controlValue(diagnosticAxisCard.info, diagnosticAxisCard.info.transformed)
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
            }
        }
        Loader {
            id: curveEditorLoader
            anchors.fill: parent
            active: root.currentPage === 6
            sourceComponent: Component {
                CurveEditor { anchors.fill: parent; visible: root.currentPage === 6; backendObject: backend; theme: root.themeTokens
                    presentationState: root.curveEditorPresentationState
                    onPresentationStateCaptured: function(state) { root.curveEditorPresentationState = state } }
            }
        }
        Loader {
            id: automationPageLoader
            anchors.fill: parent
            active: root.currentPage === 7
            sourceComponent: Component {
                AutomationPage { anchors.fill: parent; visible: root.currentPage === 7; backendObject: backend; themeTokens: root.themeTokens; topGun: theme.topGun
                    presentationState: root.automationPresentationState
                    onPresentationStateCaptured: function(state) { root.automationPresentationState = state } }
            }
        }
    }

    Dialog {
        id: deviceActionDialog
        modal: true
        property string action: ""
        title: action === "uninstall" ? "Uninstall HOTAS BF6?" : action === "forget" ? "Forget all saved controllers?" : "Reset active-controller calibration?"
        standardButtons: Dialog.Cancel
        contentItem: ColumnLayout { spacing: 12; implicitWidth: 390
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.text
                text: deviceActionDialog.action === "uninstall" ? "HOTAS BF6 will be removed. Shared vJoy, HidHide, profiles, curves, automation, and saved data remain by default." : deviceActionDialog.action === "forget" ? "This removes only HOTAS BF6 controller memory. Profiles, curves, automation, and other settings stay intact." : "This removes calibration only for the active controller. Profiles, curves, and mappings stay intact." }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                Button { text: "Confirm"; onClicked: { if (deviceActionDialog.action === "uninstall") backend.launchUninstaller(); else if (deviceActionDialog.action === "forget") backend.forgetAllSavedControllers(); else backend.resetDeviceCalibration(); deviceActionDialog.close() } }
            }
        }
    }
    Dialog {
        id: controllerSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(740, root.width - 36)
        title: ""
        standardButtons: Dialog.NoButton
        padding: 18
        onClosed: backend.acknowledgeControllerSetup()
        background: Rectangle { color: theme.panel; border.color: theme.borderStrong; radius: theme.panelRadius }
        contentItem: ControllerReadinessPanel { width: parent.width; backendObject: backend; themeTokens: root.themeTokens
            onCloseRequested: controllerSetupDialog.close() }
    }

    Dialog {
        id: newProfileDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 410
        title: ""
        standardButtons: Dialog.NoButton
        padding: 18
        onOpened: {
            profileNameField.text = ""
            startProfile.currentIndex = backend.activeProfileIndex
            profileNameField.forceActiveFocus()
        }
        header: Rectangle {
            implicitHeight: theme.topGun ? 74 : 70
            color: theme.topGun ? "#101b20" : theme.panelRaised
            border.color: theme.topGun ? theme.orange : theme.borderStrong
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text { text: "NEW PROFILE"; color: theme.topGun ? theme.orangeBright : theme.textStrong
                    font.pixelSize: theme.topGun ? 17 : 15; font.bold: true; font.family: theme.topGun ? theme.displayFont : root.font.family }
                Text { text: "Create a new controller configuration"; color: theme.textMuted; font.pixelSize: 10 }
            }
            Rectangle { visible: theme.topGun; anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                width: 42; height: 2; color: theme.cyan }
        }
        contentItem: Column { width: 358; spacing: 12
            Text { text: "NAME"; color: theme.textMuted; font.pixelSize: 10; font.bold: true }
            FlightTextInput { id: profileNameField; width: parent.width
                placeholderText: "Helicopter"
                onAccepted: { if (backend.createProfile(text, startProfile.currentValue)) newProfileDialog.close() } }
            Text { text: "START FROM"; color: theme.textMuted; font.pixelSize: 10; font.bold: true }
            FlightComboBox { id: startProfile; width: parent.width
                model: backend.profiles; textRole: "name"; valueRole: "id" }
            Text { text: "Copies the selected mapping configuration. Calibration remains global to the controller."
                width: parent.width; wrapMode: Text.WordWrap; color: theme.textFaint; font.pixelSize: 10 }
            Row { width: parent.width; spacing: 8
                CommandButton { id: createProfileCancelButton; label: "CANCEL"; subdued: true
                    onTriggered: newProfileDialog.close() }
                Item { width: parent.width - createProfileCancelButton.width - createProfileButton.width - 16; height: 1 }
                CommandButton { id: createProfileButton; label: "CREATE"
                    commandEnabled: profileNameField.text.trim().length > 0
                    onTriggered: { if (backend.createProfile(profileNameField.text, startProfile.currentValue)) newProfileDialog.close() } }
            }
        }
        background: Panel { color: theme.topGun ? "#10171b" : theme.panel; border.color: theme.topGun ? theme.orange : theme.borderStrong }
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
            TextField { id: renameProfileField; width: parent.width; color: theme.text; selectByMouse: true
                background: Rectangle { radius: theme.topGun ? 1 : theme.controlRadius; color: theme.control; border.color: renameProfileField.activeFocus ? (theme.topGun ? theme.orange : theme.borderStrong) : theme.border } }
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
                CommandButton { id: deleteProfileButton; label: "DELETE"; destructive: true
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
