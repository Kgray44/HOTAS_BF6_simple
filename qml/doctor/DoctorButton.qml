import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

AbstractButton {
    id: control
    property string tone: "secondary"
    property bool selected: false
    property bool compact: false
    property bool iconOnly: false
    property string tooltipText: ""
    property string accessibleName: text
    implicitHeight: compact ? 28 : 34
    implicitWidth: iconOnly ? 32 : Math.max(compact ? 58 : 84, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: iconOnly ? 6 : (compact ? 10 : 13)
    rightPadding: iconOnly ? 6 : (compact ? 10 : 13)
    topPadding: 4
    bottomPadding: 4
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.description: tooltipText
    Keys.onReturnPressed: control.click()
    Keys.onEnterPressed: control.click()

    contentItem: Label {
        text: control.text
        color: !control.enabled ? Theme.disabled : (control.tone === "primary" ? Theme.textPrimary : Theme.textSecondary)
        font.family: Theme.ui
        font.pixelSize: control.iconOnly ? 15 : (control.compact ? 10 : 11)
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 2
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.focus : (!control.enabled ? "#3d464e" : control.selected ? Theme.focus : control.hovered ? Theme.separatorStrong : Theme.separator)
        color: !control.enabled ? "#1a1e23" : control.down ? Theme.pressed : control.selected ? Theme.selected : control.hovered ? Theme.hover : control.tone === "primary" ? "#0f5063" : Theme.elevated
    }
    ToolTip {
        parent: control
        visible: control.hovered && control.tooltipText.length > 0
        delay: 650
        text: control.tooltipText
        background: Rectangle { color: "#20272e"; border.color: Theme.separatorStrong; border.width: 1; radius: 2 }
        contentItem: Label { text: control.tooltipText; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 10; padding: 7 }
    }
}
