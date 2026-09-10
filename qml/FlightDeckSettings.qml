import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// A native control-plane settings surface. It consumes the established
// AppBackend and ThemeManager ownership; it never creates a settings store or
// observes the controller report path.
Flickable {
    id: root
    objectName: "flightDeckSettings"
    anchors.fill: parent
    clip: true
    contentWidth: width
    contentHeight: content.implicitHeight + deck.space24
    boundsBehavior: Flickable.StopAtBounds

    signal navigateToPage(int page)

    // Startup tests may provide display-only exceptional states here. The
    // production page leaves this empty and always reads authoritative state.
    property var presentationState: ({})
    readonly property bool narrow: width < 760
    readonly property bool compact: width < 980
    readonly property var outputLayouts: backend.virtualOutputLayouts

    FlightDeckTheme {
        id: deck
        objectName: "flightDeckSettingsTheme"
    }

    function displayValue(key, fallback) {
        return presentationState && presentationState[key] !== undefined ? presentationState[key] : fallback;
    }

    function nextOutputDeviceId() {
        for (let deviceId = 1; deviceId <= 16; ++deviceId) {
            let used = false;
            for (let index = 0; index < outputLayouts.length; ++index) {
                if (outputLayouts[index].deviceId === deviceId) {
                    used = true;
                    break;
                }
            }
            if (!used)
                return deviceId;
        }
        return 0;
    }

    component DeckButton: Button {
        id: control
        implicitHeight: deck.compactControlHeight
        leftPadding: deck.space12
        rightPadding: deck.space12
        focusPolicy: Qt.StrongFocus
        font.family: deck.telemetryFont
        font.pixelSize: 9
        font.bold: true

        contentItem: Text {
            text: control.text
            color: !control.enabled ? deck.disabled : control.down ? (deck.light ? deck.primarySurface : deck.textPrimary) : deck.accent
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.secondarySurface : control.down ? deck.accentMuted : control.hovered ? deck.secondarySurface : "transparent"
            border.width: control.activeFocus ? 2 : 1
            border.color: !control.enabled ? deck.border : control.activeFocus ? deck.focus : deck.accent
        }
    }

    component DeckPrimaryButton: DeckButton {
        id: primary
        property bool destructive: false
        contentItem: Text {
            text: primary.text
            color: !primary.enabled ? deck.textMuted : primary.destructive ? deck.fault : (deck.light ? "white" : deck.primarySurface)
            font: primary.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !primary.enabled ? deck.disabled : primary.down ? (primary.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.22) : deck.accentMuted) : primary.hovered ? (primary.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.15) : deck.focus) : primary.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.10) : deck.accent
            border.width: primary.activeFocus ? 2 : 1
            border.color: primary.activeFocus ? deck.focus : primary.destructive ? deck.fault : deck.accent
        }
    }

    // Keep compact numeric controls inside the Flight Deck language. Qt's
    // platform SpinBox indicators otherwise remain visible even when the
    // surrounding field has a themed background.
    component DeckStepper: SpinBox {
        id: stepper
        property int stepperButtonWidth: deck.space24 + deck.space4
        property bool flightDeckStyled: true

        implicitWidth: 92
        implicitHeight: deck.compactControlHeight
        editable: false
        font.family: deck.telemetryFont
        font.pixelSize: 10

        contentItem: Text {
            text: stepper.textFromValue(stepper.value, stepper.locale)
            color: stepper.enabled ? deck.textPrimary : deck.disabled
            font: stepper.font
            leftPadding: deck.space8
            rightPadding: stepper.stepperButtonWidth + deck.space8
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: deck.radiusControl
            color: stepper.enabled ? deck.primarySurface : deck.secondarySurface
            border.width: stepper.activeFocus ? 2 : 1
            border.color: stepper.activeFocus ? deck.focus : deck.border

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: stepper.stepperButtonWidth
                radius: parent.radius
                color: stepper.enabled ? deck.secondarySurface : deck.primarySurface
            }
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: stepper.stepperButtonWidth
                height: 1
                color: deck.divider
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: stepper.stepperButtonWidth
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: deck.divider
            }
        }

        up.indicator: Item {
            objectName: "flightDeckSettingsVjoyDeviceIncrement"
            x: stepper.mirrored ? 0 : stepper.width - width
            y: 0
            width: stepper.stepperButtonWidth
            height: stepper.height / 2
            Rectangle {
                anchors.fill: parent
                color: !stepper.enabled ? "transparent"
                      : stepper.up.pressed ? deck.accentMuted
                      : stepper.up.hovered ? Qt.rgba(deck.accent.r, deck.accent.g, deck.accent.b, 0.13)
                      : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "+"
                color: !stepper.enabled ? deck.disabled : stepper.up.hovered ? deck.accent : deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 13
                font.bold: true
            }
        }

        down.indicator: Item {
            objectName: "flightDeckSettingsVjoyDeviceDecrement"
            x: stepper.mirrored ? 0 : stepper.width - width
            y: stepper.height / 2
            width: stepper.stepperButtonWidth
            height: stepper.height - y
            Rectangle {
                anchors.fill: parent
                color: !stepper.enabled ? "transparent"
                      : stepper.down.pressed ? deck.accentMuted
                      : stepper.down.hovered ? Qt.rgba(deck.accent.r, deck.accent.g, deck.accent.b, 0.13)
                      : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "−"
                color: !stepper.enabled ? deck.disabled : stepper.down.hovered ? deck.accent : deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 13
                font.bold: true
            }
        }
    }

    component DeckToggle: Rectangle {
        id: toggle
        property bool checked: false
        signal toggled(bool value)
        implicitWidth: 46
        implicitHeight: 26
        radius: implicitHeight / 2
        color: !enabled ? deck.secondarySurface : checked ? deck.accent : deck.primarySurface
        border.width: activeFocus ? 2 : 1
        border.color: !enabled ? deck.border : activeFocus ? deck.focus : checked ? deck.accent : deck.border
        opacity: enabled ? 1.0 : 0.58
        focus: true
        activeFocusOnTab: true

        Rectangle {
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            x: toggle.checked ? parent.width - width - 3 : 3
            radius: width / 2
            color: deck.light ? "white" : deck.textPrimary
            Behavior on x {
                NumberAnimation {
                    duration: deck.hoverDuration
                }
            }
        }
        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            enabled: toggle.enabled
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                toggle.forceActiveFocus();
                toggle.toggled(!toggle.checked);
            }
        }
        Keys.onSpacePressed: function (event) {
            if (toggle.enabled)
                toggle.toggled(!toggle.checked);
            event.accepted = true;
        }
        Keys.onReturnPressed: function (event) {
            if (toggle.enabled)
                toggle.toggled(!toggle.checked);
            event.accepted = true;
        }
    }

    component SectionHeading: RowLayout {
        property string title: "SECTION"
        Layout.fillWidth: true
        spacing: deck.space8
        Text {
            text: parent.title.toUpperCase()
            color: deck.textMuted
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: deck.divider
        }
    }

    component SettingsGroup: FlightDeckCard {
        id: group
        default property alias content: groupContent.data
        property string title: ""
        property string detail: ""
        tokens: deck
        Layout.fillWidth: true
        implicitHeight: groupContent.implicitHeight + contentPadding * 2

        ColumnLayout {
            id: groupContent
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space4
            Text {
                visible: group.title.length > 0
                text: group.title
                color: deck.textPrimary
                font.family: deck.displayFont
                font.pixelSize: 15
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                visible: group.detail.length > 0
                text: group.detail
                color: deck.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item {
                Layout.preferredHeight: deck.space4
            }
        }
    }

    component SettingsRow: Item {
        id: rowRoot
        default property alias content: controls.data
        property string title: "SETTING"
        property string detail: ""
        property bool last: false
        Layout.fillWidth: true
        implicitHeight: rowLayout.implicitHeight + deck.space12

        GridLayout {
            id: rowLayout
            anchors.left: parent.left
            anchors.right: parent.right
            columns: root.narrow ? 1 : 2
            columnSpacing: deck.space24
            rowSpacing: deck.space8
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: root.narrow ? 0 : 270
                spacing: 2
                Text {
                    text: rowRoot.title
                    color: rowRoot.enabled ? deck.textPrimary : deck.disabled
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: rowRoot.detail.length > 0
                    text: rowRoot.detail
                    color: rowRoot.enabled ? deck.textSecondary : deck.textMuted
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                id: controls
                Layout.fillWidth: root.narrow
                Layout.alignment: root.narrow ? Qt.AlignLeft : Qt.AlignRight | Qt.AlignVCenter
                spacing: deck.space8
            }
            Rectangle {
                visible: !rowRoot.last
                Layout.columnSpan: root.narrow ? 1 : 2
                Layout.fillWidth: true
                Layout.topMargin: deck.space4
                Layout.preferredHeight: 1
                color: deck.divider
            }
        }
    }

    component AppearanceSegment: Button {
        id: segment
        property bool selected: false
        implicitWidth: 84
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        font.family: deck.telemetryFont
        font.pixelSize: 9
        font.bold: true
        contentItem: Text {
            text: segment.text
            color: segment.selected ? deck.textPrimary : segment.enabled ? deck.textSecondary : deck.disabled
            font: segment.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !segment.enabled ? deck.secondarySurface : segment.selected ? deck.selected : segment.down ? deck.accentMuted : segment.hovered ? deck.secondarySurface : deck.primarySurface
            border.width: segment.activeFocus ? 2 : 1
            border.color: segment.activeFocus ? deck.focus : segment.selected ? deck.accent : deck.border
        }
    }

    component ExperienceCard: Button {
        id: card
        required property var choice
        property bool selected: themeManager.currentPresentationId === choice.id
        objectName: "flightDeckExperience_" + choice.id
        implicitHeight: 156
        implicitWidth: 220
        leftPadding: deck.cardPadding
        rightPadding: deck.cardPadding
        topPadding: deck.cardPadding
        bottomPadding: deck.cardPadding
        focusPolicy: Qt.StrongFocus
        Accessible.name: choice.label
        onClicked: {
            const presentationId = choice.id
            Qt.callLater(function() { themeManager.selectPresentation(presentationId) })
        }

        contentItem: ColumnLayout {
            width: card.availableWidth
            height: card.availableHeight
            spacing: deck.space8
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: card.choice.label.toUpperCase()
                    color: deck.textPrimary
                    font.family: deck.telemetryFont
                    font.pixelSize: 11
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            Text {
                text: card.choice.description
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                Layout.fillHeight: true
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignTop
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: card.selected ? "CURRENT" : "USE EXPERIENCE"
                    color: card.selected ? deck.healthy : deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    font.bold: true
                    Layout.fillWidth: true
                }
                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: implicitWidth / 2
                    color: card.selected ? deck.healthy : deck.border
                }
            }
        }
        background: Rectangle {
            radius: deck.radiusCard
            color: card.selected ? deck.selected : card.down ? deck.accentMuted : card.hovered ? deck.elevatedSurface : deck.primarySurface
            border.width: card.activeFocus ? 2 : card.selected ? 2 : 1
            border.color: card.activeFocus ? deck.focus : card.selected ? deck.healthy : deck.border
        }
    }

    ScrollBar.vertical: ScrollBar {
        policy: root.contentHeight > root.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }

    ColumnLayout {
        id: content
        x: deck.space4
        width: root.width - deck.space8
        spacing: deck.space16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: deck.space4
            Text {
                text: "SETTINGS"
                color: deck.textPrimary
                font.family: deck.displayFont
                font.pixelSize: 25
                font.bold: true
            }
            Text {
                text: "Application preferences and presentation. Controller configuration stays in its dedicated workspaces."
                color: deck.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        SectionHeading {
            title: "General"
        }
        SettingsGroup {
            objectName: "flightDeckSettingsGeneralGroup"
            title: "Application behavior"
            detail: "Preferences apply immediately through HOTAS BF6's existing configuration ownership."
            SettingsRow {
                title: "KEEP RUNNING IN SYSTEM TRAY"
                detail: backend.trayAvailable ? "Closing the window keeps mapping and monitoring available from the system tray." : "System tray is unavailable in this Windows session."
                DeckToggle {
                    objectName: "flightDeckSettingsTrayToggle"
                    checked: backend.keepRunningInTray
                    enabled: backend.trayAvailable
                    onToggled: function(value) { backend.setKeepRunningInTray(value) }
                }
            }
            SettingsRow {
                title: "CONTROLLER SELECTION"
                detail: "Choose, verify, or repair the active physical controller in Devices & setup."
                DeckButton {
                    objectName: "flightDeckSettingsOpenDevices"
                    text: "OPEN DEVICE SETUP"
                    onClicked: root.navigateToPage(2)
                }
            }
            SettingsRow {
                title: "AUTO-SWITCH VERIFIED CONTROLLER"
                detail: "Switch only to one unambiguous remembered controller when the active controller is unavailable."
                DeckToggle {
                    objectName: "flightDeckSettingsAutoSwitchToggle"
                    checked: backend.autoSwitchVerifiedController
                    onToggled: function(value) { backend.setAutoSwitchVerifiedController(value) }
                }
            }
            SettingsRow {
                title: "PREFERRED PHYSICAL DEVICE"
                detail: backend.deviceId.length > 0 ? backend.deviceName : "Automatic selection prefers a known controller."
                last: true
                DeckButton {
                    objectName: "flightDeckSettingsUseConnected"
                    text: "USE CONNECTED"
                    onClicked: backend.useConnectedDevice()
                }
            }
        }

        SectionHeading {
            title: "Appearance"
        }
        SettingsGroup {
            objectName: "flightDeckSettingsAppearanceGroup"
            title: "Experience"
            detail: "Switching presentation never changes profiles, mappings, Automation, Adaptive Response, device verification, vJoy, or HidHide configuration."
            Flow {
                Layout.fillWidth: true
                spacing: deck.space12
                Repeater {
                    model: themeManager.presentationChoices
                    delegate: ExperienceCard {
                        required property var modelData
                        choice: modelData
                        width: Math.max(208, Math.min(250, (parent.width - deck.space24) / (root.compact ? 2 : 4)))
                    }
                }
            }
            Item {
                height: deck.space4
            }
            SettingsRow {
                title: "FLIGHT DECK COLOR MODE"
                detail: "Flight Deck has independent Light and Dark semantic resources. System color mode is not provided by this application version."
                last: true
                RowLayout {
                    spacing: deck.space8
                    AppearanceSegment {
                        objectName: "flightDeckSettingsAppearanceDark"
                        text: "DARK"
                        selected: themeManager.flightDeckAppearance === "Dark"
                        onClicked: themeManager.setFlightDeckAppearance("Dark")
                    }
                    AppearanceSegment {
                        objectName: "flightDeckSettingsAppearanceLight"
                        text: "LIGHT"
                        selected: themeManager.flightDeckAppearance === "Light"
                        onClicked: themeManager.setFlightDeckAppearance("Light")
                    }
                }
            }
        }

        SectionHeading {
            title: "Startup and game detection"
        }
        SettingsGroup {
            objectName: "flightDeckSettingsStartupGroup"
            title: "Application start and automatic activation"
            SettingsRow {
                title: "START MAPPING AUTOMATICALLY"
                detail: "Starts only after a valid physical input and vJoy output are available."
                DeckToggle {
                    objectName: "flightDeckSettingsStartMappingToggle"
                    checked: backend.startMappingOnLaunch
                    onToggled: function(value) { backend.setStartMappingOnLaunch(value) }
                }
            }
            SettingsRow {
                title: "AUTOMATIC GAME CATEGORY"
                detail: "Low-frequency foreground executable detection selects a matching category and restores its last-used profile."
                DeckToggle {
                    objectName: "flightDeckSettingsGameDetectionToggle"
                    checked: backend.automaticGameDetection
                    onToggled: function(value) { backend.setAutomaticGameDetection(value) }
                }
            }
            SettingsRow {
                title: "CONFIGURED GAMES AND PROFILES"
                detail: "Manage executable associations, automatic category behavior, and profiles in the native Profiles workspace."
                last: true
                DeckButton {
                    objectName: "flightDeckSettingsOpenProfiles"
                    text: "MANAGE PROFILES"
                    onClicked: root.navigateToPage(5)
                }
            }
        }

        SectionHeading {
            title: "Updates"
        }
        SettingsGroup {
            objectName: "flightDeckSettingsUpdatesGroup"
            title: "Application update"
            SettingsRow {
                title: backend.updateAvailable ? "UPDATE AVAILABLE" : "UPDATE STATUS"
                detail: displayValue("updateStatus", backend.updateChecking ? "Checking for updates…" : backend.updateStatusText)
                last: true
                RowLayout {
                    DeckButton {
                        objectName: "flightDeckSettingsCheckUpdates"
                        text: backend.updateChecking ? "CHECKING…" : "CHECK FOR UPDATES"
                        enabled: !backend.updateChecking
                        onClicked: backend.checkForUpdates()
                    }
                    DeckPrimaryButton {
                        visible: backend.updateAvailable
                        objectName: "flightDeckSettingsInstallUpdate"
                        text: "UPDATE " + backend.updateAvailableVersion
                        onClicked: backend.handoffToLauncher()
                    }
                }
            }
        }

        SectionHeading {
            title: "Advanced"
        }
        SettingsGroup {
            objectName: "flightDeckSettingsMappingDefaultsGroup"
            title: "Mapping defaults"
            detail: "These are global fallback preferences. Detailed controller mappings stay in Axes and Buttons."
            SettingsRow {
                title: "DISABLED AXIS VALUE"
                detail: "Neutral output held by virtual axes without an active route."
                RowLayout {
                    TextField {
                        id: disabledAxisValue
                        objectName: "flightDeckSettingsDisabledAxisValue"
                        implicitWidth: 92
                        implicitHeight: deck.compactControlHeight
                        text: Number(backend.disabledAxisValue).toFixed(1)
                        selectByMouse: true
                        validator: DoubleValidator {
                            bottom: -100
                            top: 100
                            decimals: 1
                        }
                        color: deck.textPrimary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        onEditingFinished: {
                            backend.setDisabledAxisValue(Number(text));
                            text = Number(backend.disabledAxisValue).toFixed(1);
                        }
                        background: Rectangle {
                            radius: deck.radiusControl
                            color: deck.primarySurface
                            border.width: disabledAxisValue.activeFocus ? 2 : 1
                            border.color: disabledAxisValue.activeFocus ? deck.focus : deck.border
                        }
                    }
                    Text {
                        text: "%"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }
            SettingsRow {
                title: "CURVE TRANSITION SMOOTHING"
                detail: "Prevents sudden virtual-axis jumps when mappings change. Physical stick movement remains direct."
                DeckToggle {
                    objectName: "flightDeckSettingsCurveSmoothingToggle"
                    checked: backend.curveTransitionSmoothingEnabled
                    onToggled: function(value) { backend.setCurveTransitionSmoothingEnabled(value) }
                }
            }
            SettingsRow {
                title: "TRANSITION TIME"
                detail: backend.curveTransitionSmoothingEnabled ? "Bumpless-transfer time in milliseconds. Instant (0 ms) preserves legacy immediate changes." : "Enable curve transition smoothing to change the bumpless-transfer duration."
                last: true
                RowLayout {
                    TextField {
                        id: transitionTime
                        objectName: "flightDeckSettingsTransitionTime"
                        implicitWidth: 92
                        implicitHeight: deck.compactControlHeight
                        enabled: backend.curveTransitionSmoothingEnabled
                        text: Number(backend.curveTransitionDurationMs).toFixed(0)
                        selectByMouse: true
                        validator: IntValidator {
                            bottom: 0
                            top: 1000
                        }
                        color: enabled ? deck.textPrimary : deck.disabled
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        onEditingFinished: {
                            backend.setCurveTransitionDurationMs(Number(text));
                            text = Number(backend.curveTransitionDurationMs).toFixed(0);
                        }
                        background: Rectangle {
                            radius: deck.radiusControl
                            color: transitionTime.enabled ? deck.primarySurface : deck.secondarySurface
                            border.width: transitionTime.activeFocus ? 2 : 1
                            border.color: transitionTime.activeFocus ? deck.focus : deck.border
                        }
                    }
                    Text {
                        text: "ms"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }
        }

        SettingsGroup {
            objectName: "flightDeckSettingsVirtualOutputGroup"
            title: "Virtual output"
            detail: displayValue("vjoyStatus", backend.vjoyStatusSeverity === "ready" ? "Current required virtual output capabilities are available to the mapper." : backend.vjoyStatus)
            SettingsRow {
                title: "VJOY DEVICE"
                detail: backend.virtualAxisStatus + " · " + backend.vjoyButtonCount + " buttons · " + (backend.vjoyContinuousPovCount + backend.vjoyDiscretePovCount) + " POV"
                RowLayout {
                    DeckStepper {
                        id: vjoyDevice
                        objectName: "flightDeckSettingsVjoyDevice"
                        from: 1
                        to: 16
                        value: backend.vjoyDeviceId
                        onValueModified: backend.setVjoyDeviceId(value)
                    }
                    DeckButton {
                        text: "CONFIGURE VJOY"
                        onClicked: backend.openVjoyConfiguration()
                    }
                }
            }
            SettingsRow {
                title: "OUTPUT LAYOUTS"
                detail: outputLayouts.length + " configured layout" + (outputLayouts.length === 1 ? "" : "s") + ". Profiles choose a compatible layout from their output setting."
                RowLayout {
                    DeckButton {
                        objectName: "flightDeckSettingsCreateOutput"
                        text: "CREATE 5-AXIS OUTPUT"
                        enabled: root.nextOutputDeviceId() > 0
                        onClicked: {
                            const deviceId = root.nextOutputDeviceId();
                            backend.createFiveAxisOutputLayout("5-Axis Output " + deviceId, deviceId);
                        }
                    }
                    DeckButton {
                        text: "MANAGE PROFILES"
                        onClicked: root.navigateToPage(5)
                    }
                }
            }
            SettingsRow {
                title: "MANAGED OUTPUT VISIBILITY"
                detail: "Adopt only an exact vJoy HID instance already shown by HidHide. A running game can retain an open controller handle."
                last: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: visibilityLayout
                            objectName: "flightDeckSettingsVisibilityLayout"
                            Layout.preferredWidth: root.narrow ? 160 : 190
                            implicitHeight: deck.compactControlHeight
                            model: outputLayouts
                            textRole: "name"
                            valueRole: "id"
                            enabled: outputLayouts.length > 0
                            contentItem: Text {
                                leftPadding: deck.space8
                                rightPadding: deck.space24
                                text: visibilityLayout.displayText
                                color: visibilityLayout.enabled ? deck.textPrimary : deck.disabled
                                font.pixelSize: 10
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: deck.radiusControl
                                color: visibilityLayout.enabled ? deck.primarySurface : deck.secondarySurface
                                border.width: visibilityLayout.activeFocus ? 2 : 1
                                border.color: visibilityLayout.activeFocus ? deck.focus : deck.border
                            }
                            indicator: Text {
                                x: visibilityLayout.width - width - deck.space8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "⌄"
                                color: deck.textMuted
                                font.pixelSize: 11
                            }
                        }
                        TextField {
                            id: virtualOutputIdentity
                            objectName: "flightDeckSettingsVisibilityIdentity"
                            Layout.fillWidth: true
                            implicitHeight: deck.compactControlHeight
                            placeholderText: "Exact vJoy HID instance from HidHide"
                            selectByMouse: true
                            color: deck.textPrimary
                            font.pixelSize: 10
                            background: Rectangle {
                                radius: deck.radiusControl
                                color: deck.primarySurface
                                border.width: virtualOutputIdentity.activeFocus ? 2 : 1
                                border.color: virtualOutputIdentity.activeFocus ? deck.focus : deck.border
                            }
                        }
                    }
                    DeckButton {
                        objectName: "flightDeckSettingsPrepareVisibility"
                        text: "PREPARE VISIBILITY"
                        enabled: visibilityLayout.currentValue !== undefined && virtualOutputIdentity.text.trim().length > 0
                        onClicked: {
                            if (backend.adoptVirtualOutputVisibility(visibilityLayout.currentValue, virtualOutputIdentity.text))
                                virtualOutputIdentity.text = "";
                        }
                    }
                }
            }
        }

        SettingsGroup {
            objectName: "flightDeckSettingsHidHideGroup"
            title: "Device hiding"
            detail: backend.hidhideAvailable ? (backend.hidhideMapperAllowed ? "HidHide access is available to HOTAS BF6." : "HidHide needs review before using isolation.") : "HidHide is optional and unavailable in this session."
            SettingsRow {
                title: backend.hidhideCloakStateKnown ? (backend.hidhideCloaked ? "CLOAKING ON" : "CLOAKING OFF") : "CLOAK STATUS UNAVAILABLE"
                detail: "HidHide prevents a game from seeing both the physical controller and virtual output."
                last: true
                RowLayout {
                    DeckButton {
                        text: "REFRESH"
                        onClicked: backend.refreshHidHideStatus()
                    }
                    DeckButton {
                        text: "OPEN HIDHIDE"
                        onClicked: backend.openHidHideConfiguration()
                    }
                    DeckButton {
                        text: "OPEN DEVICE SETUP"
                        onClicked: root.navigateToPage(2)
                    }
                }
            }
        }

        SettingsGroup {
            objectName: "flightDeckSettingsMaintenanceGroup"
            title: "Maintenance"
            detail: "Destructive actions state their exact scope before they run."
            SettingsRow {
                title: "FORGET SAVED CONTROLLERS"
                detail: "Removes controller memory only. Profiles and Automation remain."
                DeckButton {
                    objectName: "flightDeckSettingsForgetControllers"
                    text: "FORGET…"
                    onClicked: {
                        maintenanceDialog.action = "forget";
                        maintenanceDialog.open();
                    }
                }
            }
            SettingsRow {
                title: "RESET CALIBRATION"
                detail: "Clears calibration only for the active controller. Profiles, curves, and mappings remain."
                DeckButton {
                    objectName: "flightDeckSettingsResetCalibration"
                    text: "RESET…"
                    onClicked: {
                        maintenanceDialog.action = "calibration";
                        maintenanceDialog.open();
                    }
                }
            }
            SettingsRow {
                title: "RESET APPLICATION CONFIGURATION"
                detail: "Restores application defaults and resets configuration, profiles, curves, and Automation."
                DeckButton {
                    objectName: "flightDeckSettingsResetConfiguration"
                    text: "RESET…"
                    onClicked: {
                        maintenanceDialog.action = "configuration";
                        maintenanceDialog.open();
                    }
                }
            }
            SettingsRow {
                title: "UNINSTALL HOTAS BF6"
                detail: "Removes HOTAS BF6. Shared vJoy, HidHide, profiles, and user data remain by default."
                last: true
                DeckButton {
                    objectName: "flightDeckSettingsUninstall"
                    text: "UNINSTALL…"
                    onClicked: {
                        maintenanceDialog.action = "uninstall";
                        maintenanceDialog.open();
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: maintenanceDialog
        property string action: ""
        tokens: deck
        heading: maintenanceDialog.action === "uninstall" ? "Uninstall HOTAS BF6?" : maintenanceDialog.action === "forget" ? "Forget saved controllers?" : maintenanceDialog.action === "calibration" ? "Reset active-controller calibration?" : "Reset application configuration?"
        tone: maintenanceDialog.action === "uninstall" || maintenanceDialog.action === "configuration" ? "fault" : "attention"
        preferredWidth: 460
        contentItem: ColumnLayout {
            width: maintenanceDialog.availableWidth
            spacing: deck.space16
            Text {
                text: maintenanceDialog.action === "uninstall" ? "HOTAS BF6 will be removed. Shared vJoy, HidHide, profiles, curves, Automation, and saved data remain by default." : maintenanceDialog.action === "forget" ? "This removes only HOTAS BF6 controller memory. Profiles and Automation remain." : maintenanceDialog.action === "calibration" ? "This clears calibration only for the active controller. Profiles, curves, and mappings remain." : "This restores HOTAS BF6 application defaults and clears saved controller and calibration settings. Profiles, curves, and Automation are reset as part of the application configuration."
                color: deck.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    onClicked: maintenanceDialog.close()
                }
                DeckPrimaryButton {
                    objectName: "flightDeckSettingsConfirmMaintenance"
                    text: maintenanceDialog.action === "uninstall" ? "UNINSTALL" : maintenanceDialog.action === "configuration" ? "RESET CONFIGURATION" : "CONFIRM"
                    destructive: maintenanceDialog.action === "uninstall" || maintenanceDialog.action === "configuration"
                    onClicked: {
                        if (maintenanceDialog.action === "uninstall")
                            backend.launchUninstaller();
                        else if (maintenanceDialog.action === "forget")
                            backend.forgetAllSavedControllers();
                        else if (maintenanceDialog.action === "calibration")
                            backend.resetDeviceCalibration();
                        else
                            backend.resetApplicationConfiguration();
                        maintenanceDialog.close();
                    }
                }
            }
        }
    }
}
