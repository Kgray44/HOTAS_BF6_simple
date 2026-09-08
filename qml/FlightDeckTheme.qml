import QtQuick 6.5

// Semantic Flight Deck tokens deliberately live apart from Theme.qml. The
// existing themes retain their current token and layout contracts, while this
// object can support a distinct light/dark experience without mapper state.
QtObject {
    id: tokens

    readonly property bool light: themeManager.flightDeckAppearance === "Light"
    readonly property string displayFont: "Segoe UI Variable"
    readonly property string telemetryFont: "Consolas"

    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space24: 24
    readonly property int space32: 32
    readonly property int space48: 48

    readonly property int radiusShell: 28
    readonly property int radiusPanel: 22
    readonly property int radiusCard: 16
    readonly property int radiusControl: 12
    readonly property int radiusPill: 999

    readonly property int navigationRowHeight: 40
    readonly property int compactControlHeight: 32
    readonly property int controlHeight: 40
    readonly property int hoverDuration: 120
    readonly property int contentTransitionDuration: 180
    readonly property int expandDuration: 200
    readonly property int statusDuration: 180

    readonly property color applicationBackground: light ? "#e9eff5" : "#0b1219"
    readonly property color navigationSurface: light ? "#f7fafc" : "#111d28"
    readonly property color primarySurface: light ? "#f9fbfd" : "#132331"
    readonly property color secondarySurface: light ? "#edf3f8" : "#192c3b"
    readonly property color elevatedSurface: light ? "#ffffff" : "#203545"
    readonly property color border: light ? "#cfdae5" : "#315064"
    readonly property color divider: light ? "#dde5ed" : "#284353"
    readonly property color textPrimary: light ? "#152534" : "#eef7fb"
    readonly property color textSecondary: light ? "#486174" : "#acc0cc"
    readonly property color textMuted: light ? "#6c8190" : "#7893a3"
    readonly property color accent: light ? "#167b9f" : "#4dc5df"
    readonly property color accentMuted: light ? "#d5edf5" : "#173c4c"
    readonly property color healthy: light ? "#277851" : "#61c892"
    readonly property color informational: light ? "#2876b8" : "#72baf0"
    readonly property color attention: light ? "#a86416" : "#e7ad55"
    readonly property color fault: light ? "#b64048" : "#f08089"
    readonly property color disabled: light ? "#a9b7c2" : "#49626f"
    readonly property color selected: light ? "#d7eff7" : "#174656"
    readonly property color focus: light ? "#126b8d" : "#78d8ed"

    function statusColor(tone) {
        if (tone === "healthy")
            return healthy;
        if (tone === "attention")
            return attention;
        if (tone === "fault")
            return fault;
        return informational;
    }
}
