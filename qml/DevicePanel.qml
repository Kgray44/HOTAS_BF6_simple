import QtQuick 6.5
import QtQuick.Layouts 6.5

// Shared Devices surface.  Legacy deliberately retains its original layered
// card construction.  Standard deliberately uses its clean modern technical
// panel instead of a recoloured Legacy card; Top Gun and Day Ops then apply
// their own instrument/deck variants from the same semantic geometry.
Rectangle {
    id: panel
    required property var theme
    property bool legacy: false
    radius: legacy ? 6 : theme.panelRadius
    color: legacy ? "#e9161d23" : (theme.topGun ? "#e80b1b21" : theme.panel)
    border.color: legacy ? "#41546770" : theme.border
    border.width: 1
    // A panel's content may have an intrinsically wide title or action row,
    // but the page, not that content, owns the viewport width.  Opt out of
    // Layout's implicit-width minimum so a resized Devices host can shrink
    // the card and let its rows elide/wrap instead of overflowing sideways.
    Layout.minimumWidth: 0

    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: panel.legacy ? 5 : Math.max(1, panel.theme.panelRadius - 1)
        visible: panel.legacy || panel.theme.topGun || panel.theme.dayOps
        opacity: panel.legacy ? 0.5 : panel.theme.topGun ? 0.62 : 0.34
        gradient: Gradient {
            GradientStop { position: 0.0; color: panel.legacy ? "#2438434d" : panel.theme.dayOps ? "#54ffffff" : panel.theme.topGun ? "#273c3740" : "#2438434d" }
            GradientStop { position: 0.38; color: panel.legacy ? "#0a101419" : panel.theme.dayOps ? "#18ffffff" : panel.theme.topGun ? "#121d1d20" : "#0a101419" }
            GradientStop { position: 1.0; color: panel.legacy ? "#0a0d1016" : panel.theme.dayOps ? "#1f425b66" : panel.theme.topGun ? "#050c0f24" : "#0a0d1016" }
        }
    }
    // These two lines are an intentional part of the original Legacy card,
    // not generic decoration.  Hiding them for Standard keeps the two visual
    // systems recognisably different even though their data hierarchy matches.
    Rectangle { visible: legacy; x: 1; y: 1; width: parent.width - 2; height: 1; radius: 1; color: "#5c9cafb8" }
    Rectangle { visible: legacy; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#1026323a" }
    Repeater { visible: !legacy && theme.topGun; model: 4; delegate: Rectangle { width: 4; height: 4; radius: 2; color: "#604a2b"; border.color: "#a27e46"; x: index < 2 ? 6 : parent.width - 10; y: index % 2 === 0 ? 6 : parent.height - 10 } }
}
