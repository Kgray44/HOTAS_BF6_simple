import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Shared Flight Deck modal foundation. It owns visual and modal behavior only;
// callers continue to own their authoritative command and validation paths.
Dialog {
    id: control

    required property var tokens
    property string heading: ""
    property string tone: "informational" // informational, attention, fault
    property int preferredWidth: 500
    property int contentPadding: tokens.dialogPadding
    // Consumers with an intrinsically long form use this shared body budget
    // for their ScrollView/Flickable. It reserves the rounded top header and
    // both vertical safe insets, so a capped dialog never hides its actions.
    readonly property int maximumViewportHeight: Math.max(220,
        (parent ? parent.height : implicitHeight) - tokens.space48)
    readonly property int maximumBodyHeight: Math.max(tokens.controlHeight,
        maximumViewportHeight - (header ? header.implicitHeight : 0)
        - contentPadding * 2)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(preferredWidth, Math.max(320, (parent ? parent.width : preferredWidth) - tokens.space48))
    height: Math.min(implicitHeight, maximumViewportHeight)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.NoButton
    padding: contentPadding

    Overlay.modal: Rectangle {
        color: Qt.rgba(0.01, 0.03, 0.05, control.tokens.light ? 0.28 : 0.58)
    }

    background: Rectangle {
        radius: control.tokens.radiusPanel
        color: control.tokens.elevatedSurface
        border.width: 1
        border.color: control.tone === "fault" ? control.tokens.fault
            : control.tone === "attention" ? control.tokens.attention : control.tokens.border
    }

    // Dialog controls position `header` themselves, so an x/y assignment on a
    // bare layout is not a reliable safe inset. Reserve a header region and
    // inset its content inside it; every consumer now gets a true rounded-top
    // cushion before the title and a separate padded body below.
    header: Item {
        objectName: "flightDeckDialogHeader"
        implicitHeight: control.heading.length > 0
            ? headerContent.implicitHeight + control.contentPadding + control.tokens.space8 : 0
        ColumnLayout {
            id: headerContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: control.contentPadding
            anchors.rightMargin: control.contentPadding
            anchors.topMargin: control.contentPadding
            spacing: control.tokens.space4
            Text {
                objectName: "flightDeckDialogHeading"
                Layout.fillWidth: true
                text: control.heading
                color: control.tokens.textPrimary
                font.family: control.tokens.displayFont
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: control.tokens.divider
            }
        }
    }
}
