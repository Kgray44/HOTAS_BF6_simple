import QtQuick 6.5
import QtQuick.Controls 6.5

Item {
    id: control
    required property var theme
    property var model: []
    property string textRole: ""
    property string valueRole: ""
    property int currentIndex: 0
    property bool controlEnabled: true
    property int popupMaximumHeight: 260
    readonly property bool legacy: !!theme.legacy
    readonly property var currentValue: valueFor(currentIndex)
    readonly property string displayText: textFor(currentIndex)
    signal activated(int index, var value)
    implicitHeight: legacy ? 31 : 34
    implicitWidth: 160

    function itemAt(index) { return index >= 0 && model && index < model.length ? model[index] : null }
    function textFor(index) {
        const item = itemAt(index)
        if (item === null || item === undefined) return ""
        if (typeof item === "string" || typeof item === "number") return String(item)
        return textRole && item[textRole] !== undefined ? String(item[textRole]) : (item.label !== undefined ? String(item.label) : String(item))
    }
    function valueFor(index) {
        const item = itemAt(index)
        if (item === null || item === undefined) return ""
        if (typeof item === "string" || typeof item === "number") return item
        return valueRole && item[valueRole] !== undefined ? item[valueRole] : item
    }
    function choose(index) {
        if (index < 0 || index >= model.length) return
        currentIndex = index
        popup.close()
        activated(index, valueFor(index))
    }

    Rectangle {
        anchors.fill: parent; radius: theme.controlRadius
        color: control.controlEnabled ? (hit.containsMouse ? theme.controlHover : theme.control) : theme.controlDisabled
        border.color: popup.visible || hit.containsMouse ? theme.borderStrong : theme.border
        Text { anchors.left: parent.left; anchors.right: arrow.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 10; anchors.rightMargin: 6
            text: control.displayText; color: control.controlEnabled ? theme.text : theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        Text { id: arrow; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: popup.visible ? "⌃" : "⌄"; color: theme.textMuted; font.pixelSize: 15 }
    }
    MouseArea { id: hit; anchors.fill: parent; hoverEnabled: true; enabled: control.controlEnabled; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: popup.visible ? popup.close() : popup.open() }
    Popup {
        id: popup
        y: control.height + 4; width: control.width
        padding: 6
        implicitHeight: Math.min(control.popupMaximumHeight, list.contentHeight + topPadding + bottomPadding)
        height: implicitHeight
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: DevicePanel { theme: control.theme; legacy: control.legacy; border.color: theme.borderStrong }
        contentItem: ListView {
            id: list; clip: true; model: control.model; implicitHeight: contentHeight
            delegate: Item {
                required property var modelData
                required property int index
                width: list.width; implicitHeight: 34
                readonly property bool chosen: index === control.currentIndex
                Rectangle { anchors.fill: parent; radius: theme.controlRadius; color: itemHit.containsMouse ? theme.selection : parent.chosen ? theme.selectionCurrent : "transparent"; border.color: parent.chosen ? theme.borderStrong : "transparent" }
                Text { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; verticalAlignment: Text.AlignVCenter; text: control.textFor(index); color: theme.text; font.pixelSize: 10; elide: Text.ElideRight }
                MouseArea { id: itemHit; anchors.fill: parent; hoverEnabled: true; onClicked: control.choose(index) }
            }
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
