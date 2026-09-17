import QtQuick 6.7
import QtQuick.Layouts 6.5

// Shared bottom edge for Flight Deck dialogs. Qt 6.7's independent corner
// radii ensure an opaque footer cannot square off an otherwise rounded modal.
Rectangle {
    id: root

    required property var tokens
    default property alias content: contentHost.data

    implicitHeight: Math.max(tokens.controlHeight + tokens.space12,
        contentHost.implicitHeight + tokens.space12)
    color: tokens.elevatedSurface
    radius: tokens.radiusPanel
    topLeftRadius: 0
    topRightRadius: 0
    bottomLeftRadius: tokens.radiusPanel
    bottomRightRadius: tokens.radiusPanel
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.tokens.divider
    }

    Item {
        id: contentHost
        anchors.fill: parent
        anchors.margins: root.tokens.space6
    }
}
