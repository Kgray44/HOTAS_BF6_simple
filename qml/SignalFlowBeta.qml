import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Production-safe Signal Flow placeholder. The existing graph editor remains
// available only to developer/test launches while the replacement is built;
// this page neither reads nor mutates mapping topology.
Item {
    id: root
    property var tokens
    property bool flightDeck: false

    readonly property color surface: tokens && tokens.primarySurface !== undefined
        ? tokens.primarySurface : "#10171b"
    readonly property color panel: tokens && tokens.elevatedSurface !== undefined
        ? tokens.elevatedSurface : "#20282d"
    readonly property color primaryText: tokens && tokens.textPrimary !== undefined
        ? tokens.textPrimary : (tokens && tokens.textStrong !== undefined ? tokens.textStrong : "#f3f7f7")
    readonly property color secondaryText: tokens && tokens.textSecondary !== undefined
        ? tokens.textSecondary : (tokens && tokens.textMuted !== undefined ? tokens.textMuted : "#9aa3a7")
    readonly property color accent: tokens && tokens.accent !== undefined
        ? tokens.accent : (tokens && tokens.orange !== undefined ? tokens.orange : "#78aab9")
    readonly property real scaleFactor: tokens && tokens.textScale !== undefined ? tokens.textScale : 1.15
    function scale(value) { return Math.max(1, Math.round(value * scaleFactor)) }

    Rectangle { anchors.fill: parent; color: root.surface }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - scale(48), scale(640))
        spacing: scale(14)
        Text { text: "SIGNAL FLOW"; color: root.primaryText; font.pixelSize: root.scale(28); font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: betaContent.implicitHeight + root.scale(38)
            radius: root.flightDeck ? root.scale(16) : root.scale(6)
            color: root.panel
            border.color: root.accent
            ColumnLayout {
                id: betaContent
                anchors.fill: parent
                anchors.margins: root.scale(19)
                spacing: root.scale(10)
                Text { text: "BETA · IN DEVELOPMENT"; color: root.accent; font.pixelSize: root.scale(11); font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Text { text: "The graphical routing editor is being rebuilt."; color: root.primaryText; font.pixelSize: root.scale(17); font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                Text { text: "Your existing mappings continue to work normally. Use Axes, Buttons, Profiles, or Automation to review and edit current mappings while this page is in beta."; color: root.secondaryText; font.pixelSize: root.scale(12); Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
            }
        }
    }
}
