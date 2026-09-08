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
    // Exposed for presentation-contract tests.  The content geometry is
    // shared, while every named theme resolves a deliberate surface system.
    // In particular, Legacy must never silently fall through to Standard.
    readonly property string surfaceTreatment: legacy ? "legacy-layered"
                                                    : theme.topGun ? "top-gun-instrument"
                                                                   : theme.dayOps ? "day-ops-deck"
                                                                                  : "standard-raised"
    // Standard is intentionally a clean, raised modern technical surface.
    // Its generous corner treatment and opaque hierarchy keep it visibly
    // separate from Legacy's inset, layered cockpit panel without changing
    // any Devices content geometry.
    radius: legacy ? 6 : theme.topGun ? theme.panelRadius : theme.dayOps ? theme.panelRadius : 9
    color: legacy ? "#e9161d23" : theme.devicePanelSurface
    border.color: legacy ? "#41546770" : theme.devicePanelBorder
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
    Rectangle { objectName: "legacyPanelTopHighlight"; visible: legacy; x: 1; y: 1; width: parent.width - 2; height: 1; radius: 1; color: "#5c9cafb8" }
    Rectangle { objectName: "legacyPanelBottomEdge"; visible: legacy; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#1026323a" }
    // Standard receives a restrained technical inset rail rather than
    // Legacy's edge highlight. It is paint-only: geometry and data hierarchy
    // stay identical across themes.
    Rectangle { visible: !legacy && !theme.topGun && !theme.dayOps; anchors.left: parent.left; anchors.leftMargin: 1; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: 12; anchors.bottomMargin: 12; width: 3; radius: 2; color: theme.devicePanelAccent; opacity: 0.72 }
    // Fasteners are an instrument-panel detail, never a Standard or Legacy
    // decoration. Naming the group makes this exclusion assertable in the
    // rendered Devices fixture as well as visually obvious in Legacy.
    Repeater { objectName: "topGunCornerFasteners"; visible: !legacy && theme.topGun; model: 4; delegate: Rectangle { width: 4; height: 4; radius: 2; color: "#604a2b"; border.color: "#a27e46"; x: index < 2 ? 6 : parent.width - 10; y: index % 2 === 0 ? 6 : parent.height - 10 } }
}
