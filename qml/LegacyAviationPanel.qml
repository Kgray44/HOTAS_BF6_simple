import QtQuick 6.5

// The shared surface used by the accepted Axes and Buttons-page cards.
// Keep visual treatment here so Curve Editor cards are the same component,
// not a close approximation.
Rectangle {
    color: "#e9161d23"
    border.color: "#41546770"
    border.width: 1
    radius: 6

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 5
        opacity: 0.5
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2438434d" }
            GradientStop { position: 0.38; color: "#0a101419" }
            GradientStop { position: 1.0; color: "#0a0d1016" }
        }
    }
    Rectangle {
        x: 1
        y: 1
        width: parent.width - 2
        height: 1
        radius: 1
        color: "#5c9cafb8"
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#1026323a"
    }
}
