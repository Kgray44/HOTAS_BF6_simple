import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// A shared, keyboard-accessible Flight Deck header action.  Keeping the
// controls here prevents each page from inventing its own status shortcut.
Button {
    id: control
    required property var tokens
    property string value: ""
    property string tone: "informational"

    objectName: "flightDeckHeaderPill_" + text.toLowerCase()
    implicitHeight: 38
    implicitWidth: pillContent.implicitWidth + tokens.space24
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    // Mouse activation must use the same persistent focus treatment as a
    // keyboard activation. The next focusable control owns focus normally.
    onPressed: control.forceActiveFocus()
    Accessible.name: text + (value.length > 0 ? ": " + value : "")
    Accessible.role: Accessible.Button

    contentItem: RowLayout {
        id: pillContent
        spacing: tokens.space8
        Text {
            text: control.text
            color: control.enabled ? tokens.textSecondary : tokens.disabled
            font.family: tokens.telemetryFont
            font.pixelSize: 8
            font.bold: true
        }
        Text {
            visible: control.value.length > 0
            text: control.value
            color: control.enabled ? tokens.statusColor(control.tone) : tokens.disabled
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
        }
    }
    background: Rectangle {
        radius: tokens.radiusPill
        color: !control.enabled ? tokens.secondarySurface
            : control.down ? tokens.accentMuted
            : control.hovered ? tokens.selected : tokens.secondarySurface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? tokens.focus
            : control.hovered ? tokens.statusColor(control.tone) : tokens.border
    }
}
