import QtQuick 6.5

// The shared surface used by the accepted Axes and Buttons-page cards.
// Keep visual treatment here so Curve Editor cards are the same component,
// not a close approximation.
Rectangle {
    Theme { id: defaultTheme }
    property var theme: defaultTheme
    color: theme ? (theme.topGun ? "#e80b1b21" : "#e9161d23") : "#e9161d23"
    border.color: theme ? theme.border : "#41546770"
    border.width: 1
    radius: theme ? theme.panelRadius : 6

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: theme ? Math.max(1, theme.panelRadius - 1) : 5
        opacity: 0.5
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme && theme.topGun ? "#273c3740" : "#2438434d" }
            GradientStop { position: 0.38; color: theme && theme.topGun ? "#121d1d20" : "#0a101419" }
            GradientStop { position: 1.0; color: theme && theme.topGun ? "#050c0f24" : "#0a0d1016" }
        }
    }
    Rectangle {
        x: 1
        y: 1
        width: parent.width - 2
        height: 1
        radius: 1
        color: theme ? theme.fastener : "#5c9cafb8"
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme ? theme.divider : "#1026323a"
    }
    // Restrained physical-panel cues only in the alternate theme.
    Repeater {
        visible: theme && theme.topGun
        model: 4
        Rectangle {
            width: 4; height: 4; radius: 2
            color: "#604a2b"
            border.color: "#a27e46"
            x: index < 2 ? 6 : parent.width - 10
            y: index % 2 === 0 ? 6 : parent.height - 10
        }
    }
}
