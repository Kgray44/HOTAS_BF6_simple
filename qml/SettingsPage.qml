import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Shared structure with three visual treatments through Theme plus the legacy
// token branch. Settings only changes durable configuration at user actions.
Flickable {
    id: root
    property bool legacy: false
    anchors.fill: parent
    contentWidth: width
    contentHeight: settings.implicitHeight + 26
    clip: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Theme { id: theme }
    readonly property color panelColor: legacy ? "#182126" : theme.panel
    readonly property color insetColor: legacy ? "#10191d" : theme.panelInset
    readonly property color borderColor: legacy ? "#49616b" : theme.border
    readonly property color textColor: legacy ? "#eef5f5" : theme.textStrong
    readonly property color mutedColor: legacy ? "#9fb1b5" : theme.textMuted
    readonly property color readyColor: legacy ? "#9fcbbf" : theme.ready
    readonly property color warningColor: legacy ? "#d6bd78" : theme.warning
    readonly property color dangerColor: legacy ? "#c98e97" : theme.danger
    readonly property color accentColor: legacy ? "#8ec8d0" : theme.orangeBright
    readonly property bool narrow: width < 980

    component SectionLabel: RowLayout {
        property string label: "SECTION"
        Layout.fillWidth: true; spacing: 8
        Rectangle { width: theme.topGun ? 13 : 7; height: theme.topGun ? 3 : 7; radius: theme.topGun ? 0 : 4; color: root.accentColor }
        Text { text: parent.label; color: root.mutedColor; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
        Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }
    }
    component Card: Rectangle {
        default property alias content: body.data
        property string title: ""
        property string detail: ""
        property color accent: root.borderColor
        implicitHeight: body.implicitHeight + 30
        radius: root.legacy ? 3 : theme.panelRadius; color: root.panelColor; border.color: accent
        ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 15; spacing: 9
            Text { visible: parent.parent.title.length > 0; text: parent.parent.title; color: root.textColor; font.pixelSize: 13; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
            Text { visible: parent.parent.detail.length > 0; text: parent.parent.detail; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    component ActionButton: Rectangle {
        property string label: "ACTION"
        property bool subdued: false
        property bool actionEnabled: true
        property bool destructive: false
        signal triggered()
        implicitWidth: Math.max(104, actionLabel.implicitWidth + 26); implicitHeight: 32
        radius: theme.topGun ? 1 : theme.controlRadius
        color: !actionEnabled ? theme.controlDisabled : actionMouse.containsMouse ? (subdued ? theme.buttonSecondaryHover : theme.buttonHover) : (destructive ? theme.destructive : subdued ? theme.buttonSecondary : theme.buttonSurface)
        border.color: !actionEnabled ? root.borderColor : destructive ? root.dangerColor : subdued ? root.borderColor : root.accentColor
        opacity: actionEnabled ? 1.0 : 0.5
        Text { id: actionLabel; anchors.centerIn: parent; text: parent.label; color: root.textColor; font.pixelSize: 9; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
        MouseArea { id: actionMouse; anchors.fill: parent; enabled: parent.actionEnabled; hoverEnabled: true; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: parent.triggered() }
    }
    component Toggle: Rectangle {
        property bool checked: false
        signal toggled(bool checked)
        implicitWidth: 44; implicitHeight: 24; radius: theme.topGun ? 1 : 12
        color: checked ? root.accentColor : root.insetColor; border.color: checked ? root.accentColor : root.borderColor
        Rectangle { width: 18; height: 18; radius: theme.topGun ? 1 : 9; y: 3; x: parent.checked ? parent.width - width - 3 : 3; color: root.textColor
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled(!parent.checked) }
    }
    component SettingRow: Rectangle {
        default property alias content: row.data
        property string title: "SETTING"
        property string detail: ""
        implicitHeight: Math.max(58, row.implicitHeight + 20)
        radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: root.borderColor
        RowLayout { id: row; anchors.fill: parent; anchors.margins: 10; spacing: 14
            ColumnLayout { Layout.fillWidth: true; spacing: 2
                Text { text: parent.parent.title; color: root.textColor; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
                Text { visible: parent.parent.detail.length > 0; text: parent.parent.detail; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }
    component StatusPill: Rectangle {
        property string label: "READY"
        property color tone: root.readyColor
        implicitWidth: statusText.implicitWidth + 16; implicitHeight: 22; radius: theme.topGun ? 1 : 11
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.14); border.color: tone
        Text { id: statusText; anchors.centerIn: parent; text: parent.label; color: parent.tone; font.pixelSize: 8; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
    }

    ColumnLayout {
        id: settings
        x: 1; width: root.width - 14; spacing: 13
        ColumnLayout { Layout.fillWidth: true; spacing: 3
            Text { text: theme.topGun ? "SYSTEM CONFIGURATION" : "Settings"; color: root.textColor; font.pixelSize: theme.topGun ? 24 : 26; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
            Text { text: "Controllers, mapping defaults, application preferences, and maintenance."; color: root.mutedColor; font.pixelSize: 11 }
        }

        SectionLabel { label: "CONTROLLERS" }
        Card { Layout.fillWidth: true; title: theme.topGun ? "HOTAS SYSTEM CHECK" : "HOTAS Setup & Verification"; detail: backend.controllerReadinessStatus; accent: backend.controllerReadinessState === "READY" ? root.readyColor : root.warningColor
            RowLayout { Layout.fillWidth: true
                StatusPill { label: backend.controllerReadinessState; tone: backend.controllerReadinessState === "READY" ? root.readyColor : root.warningColor }
                Item { Layout.fillWidth: true }
                ActionButton { label: "VERIFY SETUP"; onTriggered: { readinessDialog.open(); backend.verifyHotasSetup() } }
            }
        }
        Card { Layout.fillWidth: true; title: "Input Controllers"; detail: "Discovered DirectInput controllers and remembered controller records."; accent: root.borderColor
            RowLayout { Layout.fillWidth: true
                Text { text: backend.controllers.length + " CONTROLLERS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: theme.telemetryFont }
                Item { Layout.fillWidth: true }
                ActionButton { label: "REFRESH"; subdued: true; onTriggered: backend.refreshControllers() }
            }
            Repeater { model: backend.controllers
                delegate: Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 60; radius: theme.topGun ? 1 : theme.controlRadius
                    color: modelData.active ? theme.selectionCurrent : root.insetColor; border.color: modelData.active ? root.accentColor : root.borderColor
                    RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 9
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            RowLayout { Layout.fillWidth: true; spacing: 5
                                Text { text: modelData.name; color: root.textColor; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                StatusPill { visible: modelData.active; label: "ACTIVE"; tone: root.accentColor }
                                StatusPill { visible: modelData.verified; label: "VERIFIED"; tone: root.readyColor }
                                StatusPill { visible: !modelData.verified; label: "NEW"; tone: root.warningColor }
                            }
                            Text { text: modelData.state + "  ·  " + modelData.axisCount + " AXES  ·  " + modelData.buttonCount + " BUTTONS  ·  " + modelData.povCount + " POV"; color: root.mutedColor; font.pixelSize: 8; font.family: theme.telemetryFont; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        ActionButton { visible: !modelData.verified && modelData.connected; label: "SET UP"; onTriggered: backend.selectNewController(modelData.directInputId) }
                        ActionButton { visible: modelData.verified && modelData.connected && !modelData.active; label: "SET ACTIVE"; subdued: true; onTriggered: backend.setActiveController(modelData.id) }
                        ActionButton { visible: modelData.verified; label: "FORGET"; subdued: true; onTriggered: backend.forgetController(modelData.id) }
                    }
                }
            }
            Text { visible: backend.controllers.length === 0; text: "No physical DirectInput controllers detected. vJoy is intentionally excluded."; color: root.mutedColor; font.pixelSize: 10 }
        }
        GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; columnSpacing: 13; rowSpacing: 13
            SettingRow { Layout.fillWidth: true; title: "AUTO-SWITCH VERIFIED CONTROLLER"; detail: "Switch only to one unambiguous remembered controller when the active controller is unavailable."
                Toggle { checked: backend.autoSwitchVerifiedController; onToggled: backend.setAutoSwitchVerifiedController(checked) }
            }
            SettingRow { Layout.fillWidth: true; title: "PREFERRED PHYSICAL DEVICE"; detail: backend.deviceId.length > 0 ? backend.deviceName : "Automatic selection prefers a known controller."
                ActionButton { label: "USE CONNECTED"; subdued: true; onTriggered: backend.useConnectedDevice() }
            }
        }

        SectionLabel { label: "MAPPING" }
        GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; columnSpacing: 13; rowSpacing: 13
            SettingRow { Layout.fillWidth: true; title: "START MAPPING AUTOMATICALLY"; detail: "Starts only after a valid physical input and vJoy output are available."
                Toggle { checked: backend.startMappingOnLaunch; onToggled: backend.setStartMappingOnLaunch(checked) }
            }
            SettingRow { Layout.fillWidth: true; title: "DISABLED AXIS VALUE"; detail: "Neutral output held by virtual axes without an active route."
                Rectangle { implicitWidth: 82; implicitHeight: 30; radius: theme.topGun ? 1 : theme.controlRadius; color: root.panelColor; border.color: root.borderColor
                    TextInput { anchors.fill: parent; anchors.margins: 7; text: Number(backend.disabledAxisValue).toFixed(1); color: root.textColor; font.pixelSize: 10; font.family: theme.telemetryFont; validator: DoubleValidator { bottom: -100; top: 100; decimals: 1 }
                        onEditingFinished: { backend.setDisabledAxisValue(Number(text)); text = Number(backend.disabledAxisValue).toFixed(1) } }
                }
                Text { text: "%"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
            }
        }
        Card { Layout.fillWidth: true; title: "vJoy Output"; detail: backend.vjoyReady ? "Current virtual output capabilities are available to the mapper." : backend.vjoyStatus; accent: backend.vjoyReady ? root.readyColor : root.warningColor
            RowLayout { Layout.fillWidth: true
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    RowLayout { spacing: 7
                        Text { text: "DEVICE " + backend.vjoyDeviceId; color: root.textColor; font.pixelSize: 13; font.bold: true; font.family: theme.telemetryFont }
                        StatusPill { label: backend.vjoyReady ? "READY" : "LIMITED"; tone: backend.vjoyReady ? root.readyColor : root.warningColor }
                    }
                    Text { text: backend.virtualAxisStatus + "  ·  " + backend.vjoyButtonCount + " buttons  ·  " + (backend.vjoyContinuousPovCount + backend.vjoyDiscretePovCount) + " POV"; color: root.mutedColor; font.pixelSize: 9; font.family: theme.telemetryFont; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                SpinBox { from: 1; to: 16; value: backend.vjoyDeviceId; onValueModified: backend.setVjoyDeviceId(value)
                    background: Rectangle { color: root.insetColor; border.color: root.borderColor; radius: theme.topGun ? 1 : theme.controlRadius }
                    contentItem: TextInput { text: parent.textFromValue(parent.value, parent.locale); readOnly: true; color: root.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.family: theme.telemetryFont }
                }
                ActionButton { label: "CONFIGURE VJOY"; subdued: true; onTriggered: backend.openVjoyConfiguration() }
            }
        }

        SectionLabel { label: "APPLICATION" }
        GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; columnSpacing: 13; rowSpacing: 13
            SettingRow { Layout.fillWidth: true; title: "KEEP RUNNING IN SYSTEM TRAY"; detail: backend.trayAvailable ? "Closing the window keeps mapping and monitoring running." : "System tray is unavailable in this Windows session."
                Toggle { checked: backend.keepRunningInTray; onToggled: backend.setKeepRunningInTray(checked) }
            }
            SettingRow { Layout.fillWidth: true; title: "APPEARANCE"; detail: "Legacy, Standard, and Top Gun each use their own visual language."
                ComboBox { id: appearance; implicitWidth: 138; model: themeManager.themeChoices; currentIndex: Math.max(0, model.indexOf(themeManager.currentTheme)); onActivated: themeManager.setCurrentTheme(currentText)
                    contentItem: Text { leftPadding: 9; text: appearance.displayText; color: root.textColor; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: theme.topGun ? 1 : theme.controlRadius }
                }
            }
        }
        Card { Layout.fillWidth: true; title: "Application Update"; detail: "Current version  ·  v" + Qt.application.version; accent: backend.updateAvailable ? root.readyColor : root.borderColor
            RowLayout { Layout.fillWidth: true
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Text { text: backend.updateChecking ? "CHECKING…" : backend.updateStatusText; color: backend.updateCheckFailed ? root.warningColor : backend.updateAvailable ? root.readyColor : root.mutedColor; font.pixelSize: 10 }
                    Text { visible: backend.updateAvailable; text: "UPDATE AVAILABLE  ·  " + backend.updateAvailableVersion; color: root.readyColor; font.pixelSize: 9; font.bold: true }
                }
                ActionButton { label: backend.updateChecking ? "CHECKING…" : "CHECK FOR UPDATES"; subdued: true; actionEnabled: !backend.updateChecking; onTriggered: backend.checkForUpdates() }
                ActionButton { visible: backend.updateAvailable; label: "UPDATE"; onTriggered: backend.handoffToLauncher() }
            }
        }

        SectionLabel { label: "DEVICE HIDING" }
        Card { Layout.fillWidth: true; title: "HidHide"; detail: "Hides only the selected physical controller when configured; implementation details remain in the full verification panel."; accent: backend.hidhideMapperAllowed ? root.readyColor : root.warningColor
            RowLayout { Layout.fillWidth: true
                StatusPill { label: !backend.hidhideAvailable ? "OPTIONAL" : backend.hidhideMapperAllowed ? "ACCESS OK" : "NEEDS REVIEW"; tone: !backend.hidhideAvailable ? root.mutedColor : backend.hidhideMapperAllowed ? root.readyColor : root.warningColor }
                Text { Layout.fillWidth: true; text: backend.hidhideCloakStateKnown ? backend.hidhideCloaked ? "CLOAKING ON" : "CLOAKING OFF" : "CLOAK STATUS UNAVAILABLE"; color: root.mutedColor; font.pixelSize: 9; font.family: theme.telemetryFont }
                ActionButton { label: "REFRESH"; subdued: true; onTriggered: backend.refreshHidHideStatus() }
                ActionButton { label: "OPEN HIDHIDE"; subdued: true; onTriggered: backend.openHidHideConfiguration() }
            }
        }

        SectionLabel { label: "MAINTENANCE" }
        GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; columnSpacing: 13; rowSpacing: 13
            SettingRow { Layout.fillWidth: true; title: "FORGET SAVED CONTROLLERS"; detail: "Removes controller memory only; profiles and automation remain."
                ActionButton { label: "FORGET…"; subdued: true; onTriggered: { actionDialog.action = "forget"; actionDialog.open() } }
            }
            SettingRow { Layout.fillWidth: true; title: "RESET CALIBRATION"; detail: "Clears calibration only for the active controller."
                ActionButton { label: "RESET…"; subdued: true; onTriggered: { actionDialog.action = "calibration"; actionDialog.open() } }
            }
            SettingRow { Layout.fillWidth: true; title: "RESET APPLICATION CONFIGURATION"; detail: "Restores HOTAS BF6 application defaults after confirmation."
                ActionButton { label: "RESET…"; subdued: true; onTriggered: resetDialog.open() }
            }
        }
        Card { Layout.fillWidth: true; title: "Uninstall HOTAS BF6"; detail: "Remove HOTAS BF6 from this computer. Shared vJoy, HidHide, profiles, and user data remain by default."; accent: root.borderColor
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ActionButton { label: "UNINSTALL HOTAS BF6"; destructive: true; onTriggered: { actionDialog.action = "uninstall"; actionDialog.open() } }
            }
        }
    }

    Dialog { id: readinessDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; width: Math.min(740, root.width - 36); title: ""; standardButtons: Dialog.NoButton; padding: 18
        background: Rectangle { color: root.panelColor; border.color: root.accentColor; radius: theme.topGun ? 1 : theme.panelRadius }
        contentItem: ControllerReadinessPanel { width: parent.width; backendObject: backend; themeTokens: theme; legacy: root.legacy; onCloseRequested: readinessDialog.close() }
    }
    Dialog { id: actionDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; property string action: ""; title: action === "uninstall" ? "Uninstall HOTAS BF6?" : action === "forget" ? "Forget saved controllers?" : "Reset active-controller calibration?"; standardButtons: Dialog.NoButton; padding: 18
        background: Rectangle { color: root.panelColor; border.color: action === "uninstall" ? root.dangerColor : root.warningColor; radius: theme.topGun ? 1 : theme.panelRadius }
        contentItem: ColumnLayout { width: 400; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11; text: actionDialog.action === "uninstall" ? "HOTAS BF6 will be removed. Shared vJoy, HidHide, profiles, curves, automation, and saved data remain by default." : actionDialog.action === "forget" ? "This removes only HOTAS BF6 controller memory. Profiles, curves, automation, and other settings stay intact." : "This removes calibration only for the active controller. Profiles, curves, and mappings stay intact." }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ActionButton { label: "CANCEL"; subdued: true; onTriggered: actionDialog.close() }
                ActionButton { label: actionDialog.action === "uninstall" ? "UNINSTALL" : "CONFIRM"; destructive: actionDialog.action === "uninstall"; onTriggered: { if (actionDialog.action === "uninstall") backend.launchUninstaller(); else if (actionDialog.action === "forget") backend.forgetAllSavedControllers(); else backend.resetDeviceCalibration(); actionDialog.close() } }
            }
        }
    }
    Dialog { id: resetDialog; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Reset application configuration?"; standardButtons: Dialog.NoButton; padding: 18
        background: Rectangle { color: root.panelColor; border.color: root.warningColor; radius: theme.topGun ? 1 : theme.panelRadius }
        contentItem: ColumnLayout { width: 400; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11; text: "This restores HOTAS BF6 application defaults and clears saved controller and calibration settings. Profiles, curves, and automation are reset as part of the application configuration." }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { label: "CANCEL"; subdued: true; onTriggered: resetDialog.close() }
                ActionButton { label: "RESET CONFIGURATION"; destructive: true; onTriggered: { backend.resetApplicationConfiguration(); resetDialog.close() } }
            }
        }
    }
}
