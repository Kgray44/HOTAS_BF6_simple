import QtQuick 6.5
import QtQuick.Controls 6.5

Button {
    id: control
    required property var tokens
    required property var modelData
    property string label: ""
    property string detail: ""
    property bool selected: false
    property var scrollViewport: null
    // The offscreen startup-test capture neutralizes transient pointer/focus
    // decoration after exercising routes. It is always false in the product.
    property bool suppressTransientEmphasis: false
    implicitHeight: tokens.navigationRowHeight
    leftPadding: tokens.space12
    rightPadding: tokens.space12
    focusPolicy: Qt.StrongFocus
    Accessible.name: label

    onSelectedChanged: {
        if (!selected || !scrollViewport)
            return
        Qt.callLater(function () {
            if (!scrollViewport || !control.selected)
                return
            const content = scrollViewport.contentItem
            const top = control.mapToItem(content, 0, 0).y
            const bottom = top + control.height
            if (top < scrollViewport.contentY)
                scrollViewport.contentY = top
            else if (bottom > scrollViewport.contentY + scrollViewport.height)
                scrollViewport.contentY = bottom - scrollViewport.height
        })
    }

    contentItem: Row {
        spacing: tokens.space8
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 7
            height: 7
            radius: width / 2
            color: control.selected ? control.tokens.accent : control.tokens.textMuted
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, control.availableWidth - 26)
            text: control.label
            color: control.enabled ? (control.selected ? control.tokens.textPrimary : control.tokens.textSecondary) : control.tokens.disabled
            font.family: control.tokens.displayFont
            font.pixelSize: 12
            font.bold: control.selected
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: control.tokens.radiusControl
        color: !control.enabled ? "transparent" : control.selected ? control.tokens.selected : (!control.suppressTransientEmphasis && (control.down || control.hovered)) ? (control.down ? control.tokens.accentMuted : control.tokens.secondarySurface) : "transparent"
        border.width: !control.suppressTransientEmphasis && control.activeFocus ? 2 : (control.selected ? 1 : 0)
        border.color: !control.suppressTransientEmphasis && control.activeFocus ? control.tokens.focus : control.selected ? control.tokens.accent : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: control.tokens.hoverDuration
            }
        }
    }
}
