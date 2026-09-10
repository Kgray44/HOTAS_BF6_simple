import QtQuick 6.5

Rectangle {
    required property var tokens
    // Child layouts opt into this semantic default with
    // `anchors.margins: parent.contentPadding`. Compact and technical cards
    // may select the corresponding token deliberately.
    property int contentPadding: tokens.cardPadding
    radius: tokens.radiusCard
    color: tokens.secondarySurface
    border.width: 1
    border.color: tokens.border
    antialiasing: true
    // A final visual boundary only. Child layouts are still required to wrap,
    // elide, and use contentPadding; this prevents a transient paint from
    // escaping a rounded Flight Deck surface while those bindings settle.
    clip: true
}
