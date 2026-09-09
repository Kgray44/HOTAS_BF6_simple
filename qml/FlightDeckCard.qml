import QtQuick 6.5

Rectangle {
    required property var tokens
    radius: tokens.radiusCard
    color: tokens.secondarySurface
    border.width: 1
    border.color: tokens.border
    antialiasing: true
}
