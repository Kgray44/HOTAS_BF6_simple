import QtQuick 6.5

// The sole visual-token source for the application. ThemeManager owns only
// persistence and selection; this object maps that selection to presentation
// tokens so no mapper state or worker path can depend on a theme.
QtObject {
    id: theme

    readonly property bool topGun: themeManager.topGun
    readonly property string name: themeManager.currentTheme
    readonly property string displayFont: topGun ? "Arial Narrow" : "Segoe UI Variable"
    readonly property string telemetryFont: topGun ? "Consolas" : "Consolas"
    readonly property int panelRadius: topGun ? 2 : 6
    readonly property int controlRadius: topGun ? 1 : 4

    // Foundation
    readonly property color background: topGun ? "#071217" : "#0d1013"
    readonly property color backgroundLift: topGun ? "#0b1b21" : "#11171b"
    readonly property color header: topGun ? "#09151a" : "#14191d"
    readonly property color panel: topGun ? "#0b1b21" : "#1a1d23"
    readonly property color panelRaised: topGun ? "#10252c" : "#20282d"
    readonly property color panelInset: topGun ? "#061116" : "#10171b"
    readonly property color panelWash: topGun ? "#1a332f" : "#243843"
    readonly property color border: topGun ? "#8f7043" : "#435660"
    readonly property color borderStrong: topGun ? "#c29a5b" : "#78aab9"
    readonly property color divider: topGun ? "#765d37" : "#335268"
    readonly property color fastener: topGun ? "#b38b50" : "#5c9caf"

    // Typography and semantic state
    readonly property color text: topGun ? "#ead7a3" : "#e8eeee"
    readonly property color textStrong: topGun ? "#f3deb0" : "#f3f7f7"
    readonly property color textMuted: topGun ? "#b6a27b" : "#9aa3a7"
    readonly property color textFaint: topGun ? "#816f55" : "#77919a"
    readonly property color orange: topGun ? "#df6428" : "#78aab9"
    readonly property color orangeBright: topGun ? "#ff7b31" : "#a8d1dc"
    readonly property color cyan: topGun ? "#48b9c1" : "#8fc8c0"
    readonly property color ready: topGun ? "#75b9a4" : "#8fd5c9"
    readonly property color warning: topGun ? "#d9a75d" : "#d4ad69"
    readonly property color danger: topGun ? "#d76043" : "#ca9090"
    readonly property color ivory: topGun ? "#e8d8ad" : "#dbe7e8"

    // Reusable control and graph treatment
    readonly property color control: topGun ? "#09171d" : "#10171b"
    readonly property color controlHover: topGun ? "#162b30" : "#142128"
    readonly property color controlPressed: topGun ? "#2b2b22" : "#244550"
    readonly property color controlDisabled: topGun ? "#0b1114" : "#0c1013"
    readonly property color selection: topGun ? "#51301f" : "#315a66"
    readonly property color selectionCurrent: topGun ? "#332a20" : "#244650"
    readonly property color graphBackground: topGun ? "#061116" : "#0a0f12"
    readonly property color graphGrid: topGun ? "#5f5137" : "#254653"
    readonly property color graphZero: topGun ? "#9a5034" : "#567784"
    readonly property color graphInput: topGun ? "#e5d6b1" : "#dbe7e8"
    readonly property color graphOutput: topGun ? "#4fc1c6" : "#8fc8c0"
    readonly property color graphPreview: topGun ? "#ea7132" : "#377da3"
    readonly property color graphLabel: topGun ? "#b9a57d" : "#76909a"
    readonly property color graphFrame: topGun ? "#8a6c41" : "#4b7081"

    readonly property color buttonSurface: topGun ? "#11191b" : "#324f5a"
    readonly property color buttonHover: topGun ? "#3b241c" : "#456c78"
    readonly property color buttonSecondary: topGun ? "#101b20" : "#222c32"
    readonly property color buttonSecondaryHover: topGun ? "#243035" : "#303d44"
    readonly property color destructive: topGun ? "#451e1b" : "#2c2223"
    readonly property color tooltip: topGun ? "#0b161a" : "#151e23"

    function statusColor(severity) {
        if (severity === "error") return danger
        if (severity === "warning") return warning
        return ready
    }
}
