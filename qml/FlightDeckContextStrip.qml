import QtQuick 6.5
import QtQuick.Layouts 6.5

// A compact, passive context line shared by every Flight Deck page. It makes
// editor ownership distinct from the route currently used at runtime without
// turning a browse action into activation.
Rectangle {
    id: root
    required property var backendObject
    required property var tokens

    readonly property string editingName: String(backendObject.selectedProfileName || "No Profile selected")
    readonly property string activeName: String(backendObject.activeProfileName || "No Profile active")
    readonly property string effectiveName: String(backendObject.effectiveProfileName || "")
    readonly property bool effectiveDiffers: effectiveName.length > 0
        && effectiveName !== editingName && effectiveName !== activeName

    implicitHeight: contextRow.implicitHeight + tokens.space8
    radius: tokens.radiusControl
    color: tokens.secondarySurface
    border.color: tokens.border

    RowLayout {
        id: contextRow
        anchors.fill: parent
        anchors.leftMargin: tokens.space12
        anchors.rightMargin: tokens.space12
        spacing: tokens.space16
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 160
            text: "EDITING · " + root.editingName
            color: tokens.textPrimary
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 160
            text: "CURRENTLY USING · " + root.activeName
            color: root.activeName === root.editingName ? tokens.healthy : tokens.textSecondary
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            visible: root.effectiveDiffers
            Layout.fillWidth: visible
            Layout.minimumWidth: visible ? 160 : 0
            text: "EFFECTIVE · " + root.effectiveName
                + (backendObject.profileSourceLabel ? " · " + backendObject.profileSourceLabel : "")
            color: tokens.focus
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            text: "VIEWING DOES NOT ACTIVATE"
            color: tokens.textMuted
            font.family: tokens.telemetryFont
            font.pixelSize: 8
            font.bold: true
            visible: root.width >= 900 && root.editingName !== root.activeName
        }
    }
}
