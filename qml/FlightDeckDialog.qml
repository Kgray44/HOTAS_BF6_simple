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

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(preferredWidth, Math.max(320, (parent ? parent.width : preferredWidth) - tokens.space32))
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.NoButton
    padding: tokens.space16

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

    header: ColumnLayout {
        x: control.tokens.space16
        y: control.tokens.space16
        width: control.width - control.tokens.space32
        spacing: control.tokens.space4
        Text {
            Layout.fillWidth: true
            text: control.heading
            visible: text.length > 0
            color: control.tokens.textPrimary
            font.family: control.tokens.displayFont
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: control.heading.length > 0
            color: control.tokens.divider
        }
    }
}
