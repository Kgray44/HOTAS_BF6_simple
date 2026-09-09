import QtQuick 6.5
import QtQuick.Controls 6.5

// Application-owned explanatory tooltip. This intentionally leaves native
// Windows picker and permission prompts under platform ownership.
ToolTip {
    id: control
    required property var tokens
    delay: 350
    timeout: 5000
    leftPadding: tokens.space10
    rightPadding: tokens.space10
    topPadding: tokens.space8
    bottomPadding: tokens.space8
    contentItem: Text {
        text: control.text
        color: control.tokens.textPrimary
        font.family: control.tokens.telemetryFont
        font.pixelSize: 9
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        radius: control.tokens.radiusControl
        color: control.tokens.elevatedSurface
        border.color: control.tokens.border
    }
}
