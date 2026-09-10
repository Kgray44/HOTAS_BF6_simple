import QtQuick 6.5

// Semantic Flight Deck tokens deliberately live apart from Theme.qml. The
// existing themes retain their current token and layout contracts, while this
// object can support a distinct light/dark experience without mapper state.
QtObject {
    id: tokens

    readonly property bool light: themeManager.flightDeckAppearance === "Light"
    // "Segoe UI Variable" is not a stable umbrella family across the Qt
    // runtimes we support.  Keep Flight Deck's human-facing font exact and
    // reserve the technical face for dense diagnostic data only.
    readonly property string displayFont: "Segoe UI"
    readonly property string bodyFont: displayFont
    readonly property string telemetryFont: "Consolas"

    readonly property int space4: 4
    readonly property int space6: 6
    readonly property int space8: 8
    readonly property int space10: 10
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int space32: 32
    readonly property int space48: 48

    // Semantic content-safe areas keep the soft Flight Deck surfaces
    // comfortable without forcing every page to invent ad-hoc margins.
    readonly property int cardPadding: space20
    readonly property int cardPaddingCompact: space16
    readonly property int cardPaddingTechnical: space16
    readonly property int dialogPadding: space24
    readonly property int popupPadding: space8
    readonly property int popupRowPadding: space12
    readonly property int sectionGap: space16
    readonly property int controlGap: space8
    readonly property int railPadding: space16

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

    // Compatibility aliases let the established Curve Editor render inside
    // Flight Deck without falling back to undefined legacy tokens. They are
    // presentation-only mappings; curve data and mathematics stay untouched.
    readonly property bool topGun: false
    readonly property bool dayOps: false
    readonly property bool legacy: false
    readonly property color background: primarySurface
    readonly property color borderStrong: focus
    readonly property color buttonHover: selected
    readonly property color buttonSecondary: secondarySurface
    readonly property color buttonSecondaryHover: selected
    readonly property color buttonSurface: accent
    readonly property color control: secondarySurface
    readonly property color controlDisabled: disabled
    readonly property color controlPressed: accentMuted
    readonly property int controlRadius: radiusControl
    readonly property color curveDangerBorder: fault
    readonly property color curveDangerSurface: secondarySurface
    readonly property color curveDialogBorder: border
    readonly property color curveDialogSurface: elevatedSurface
    readonly property color curveMenuBorder: border
    readonly property color curveMenuSurface: elevatedSurface
    readonly property color curvePanelSurface: elevatedSurface
    readonly property color cyan: accent
    readonly property color danger: fault
    readonly property color destructive: fault
    // The graph is a first-class Flight Deck surface. Keep the instrument
    // treatment in Dark mode, but give Light mode its own high-contrast
    // technical canvas instead of carrying the dark canvas across themes.
    readonly property color graphSurface: light ? "#e7f0f6" : "#0d1b25"
    readonly property color graphBackground: light ? "#f8fbfd" : "#0a141c"
    readonly property color graphFrame: light ? "#7e96a7" : "#315064"
    readonly property color graphGrid: light ? "#c2d0da" : "#315064"
    readonly property color graphInput: light ? "#668093" : textMuted
    readonly property color graphLabel: light ? "#466071" : textSecondary
    readonly property color graphOutput: healthy
    readonly property color graphPoint: light ? "#2b7591" : textPrimary
    readonly property color graphSelectedPoint: accent
    readonly property color graphLockedPoint: light ? "#879eac" : disabled
    readonly property color graphPanelGradientTop: graphBackground
    readonly property color graphPanelGradientMiddle: graphBackground
    readonly property color graphPanelGradientBottom: graphBackground
    readonly property color graphPreview: attention
    readonly property color graphZero: graphInput
    readonly property color ivory: textPrimary
    readonly property color orange: accent
    readonly property color orangeBright: accent
    readonly property color panelInset: secondarySurface
    readonly property color ready: healthy
    readonly property color selection: selected
    readonly property color text: textPrimary
    readonly property color textFaint: disabled
    readonly property color textStrong: textPrimary
    readonly property color tooltip: elevatedSurface
    readonly property color warning: attention

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
