import QtQuick 6.5
import QtQuick.Layouts 6.5

// A contextual, task-specific density treatment shared by the existing Flight
// Deck pages. It is intentionally not a global level badge: Guided explains
// the next decision at its point of use, while Full leaves a compact editing
// context and exposes the page's existing optional detail through the shared
// disclosure policy.
Item {
    id: root
    objectName: "flightDeckGuidanceCallout"

    required property var tokens
    property string guidedTitle: "NEXT DECISION"
    property string guidedText: ""
    property string fullTitle: "EDITING CONTEXT"
    property string fullText: ""
    readonly property bool guided: themeManager.guidanceLevel === "Guided"

    Layout.fillWidth: true
    implicitHeight: callout.implicitHeight

    Rectangle {
        id: callout
        width: parent.width
        implicitHeight: content.implicitHeight + root.tokens.space16
        radius: root.tokens.radiusControl
        color: root.guided ? root.tokens.selected : root.tokens.secondarySurface
        border.color: root.guided ? root.tokens.accent : root.tokens.border
        border.width: 1

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: root.tokens.space8
            spacing: root.guided ? root.tokens.space4 : 1

            Text {
                Layout.fillWidth: true
                text: root.guided ? root.guidedTitle : root.fullTitle
                color: root.guided ? root.tokens.accent : root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.scale(9)
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.guided ? root.guidedText : root.fullText
                color: root.guided ? root.tokens.textPrimary : root.tokens.textSecondary
                font.family: root.tokens.bodyFont
                font.pixelSize: root.tokens.scale(root.guided ? 11 : 9)
                wrapMode: root.guided ? Text.WordWrap : Text.NoWrap
                elide: root.guided ? Text.ElideNone : Text.ElideRight
            }
        }
    }
}
