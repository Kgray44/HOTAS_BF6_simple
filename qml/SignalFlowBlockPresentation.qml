import QtQuick 6.5

// A visual-only Signal Flow block family. Canvas cards retain their existing
// port interaction delegates; the Library and placement ghost reuse this
// same Flight Deck card/port language without gaining canonical state.
FlightDeckCard {
    id: card
    required property var block
    property bool highlighted: false
    property bool placementGhost: false

    contentPadding: tokens.space10
    color: placementGhost
        ? (block && block.type === "annotation"
            ? Qt.rgba(tokens.elevatedSurface.r, tokens.elevatedSurface.g, tokens.elevatedSurface.b, 0.88)
            : tokens.secondarySurface)
        : (highlighted ? tokens.selected : tokens.secondarySurface)
    border.width: highlighted || placementGhost ? 2 : 1
    border.color: highlighted || placementGhost ? tokens.focus : tokens.border
    clip: false

    Item {
        anchors.fill: parent
        anchors.margins: card.contentPadding

        Rectangle {
            visible: card.block && card.block.type !== "annotation"
            width: 10; height: 10; radius: 5
            x: card.block && card.block.type === "node" && card.block.kind === "virtual-output"
                ? parent.width - width * 0.5 : -width * 0.5
            y: parent.height * 0.5 - height * 0.5
            color: card.block && card.block.kind === "virtual-output" ? tokens.healthy : tokens.informational
            border.width: 2; border.color: card.color
        }
        Rectangle {
            visible: card.block && card.block.type === "processor"
            width: 10; height: 10; radius: 5
            x: -width * 0.5; y: parent.height - height * 0.5
            color: tokens.informational; border.width: 2; border.color: card.color
        }
        Rectangle {
            visible: card.block && card.block.type === "processor"
            width: 10; height: 10; radius: 5
            x: parent.width - width * 0.5; y: parent.height - height * 0.5
            color: tokens.healthy; border.width: 2; border.color: card.color
        }
        Text {
            width: parent.width - (card.block && card.block.type === "node" ? 0 : 14)
            anchors.left: parent.left; anchors.top: parent.top
            text: String(card.block && card.block.label || "Block")
            color: tokens.textPrimary; font.family: tokens.bodyFont; font.pixelSize: 11; font.bold: true
            elide: Text.ElideRight
        }
        Text {
            width: parent.width - 6
            anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 19
            text: card.block && card.block.type === "node"
                ? (card.block.kind === "physical-input" ? "CANONICAL INPUT · REPOSITION" : "CANONICAL OUTPUT · REPOSITION")
                : card.block && card.block.type === "processor"
                    ? (card.placementGhost ? "PROCESSOR · DROP ON ROUTE" : String(card.block.detail || "Canonical processor"))
                    : "PRESENTATION ONLY"
            color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight
        }
        Text {
            visible: card.block && card.block.type === "processor"
            width: parent.width - 12
            anchors.left: parent.left; anchors.bottom: parent.bottom
            text: "INPUT  ─────────  OUTPUT"
            color: tokens.graphLabel; font.family: tokens.telemetryFont; font.pixelSize: 8; font.bold: true
            elide: Text.ElideRight
        }
        Text {
            visible: card.block && card.block.type !== "processor"
            anchors.right: parent.right; anchors.bottom: parent.bottom
            text: card.placementGhost ? "PLACE" : "DRAG"
            color: tokens.graphLabel; font.family: tokens.telemetryFont; font.pixelSize: 8; font.bold: true
        }
    }
}
