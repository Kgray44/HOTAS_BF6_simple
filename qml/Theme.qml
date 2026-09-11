import QtQuick 6.5

// The sole visual-token source for the application. ThemeManager owns only
// persistence and selection; this object maps that selection to presentation
// tokens so no mapper state or worker path can depend on a theme.
QtObject {
    id: theme

    // Shared visual primitives can be constructed in an isolated QML test or
    // preview before Main.qml installs its context object.  Fall back to the
    // Standard token family for that presentation-only interval rather than
    // emitting repeated ReferenceErrors from every panel binding.
    readonly property var manager: typeof themeManager !== "undefined" ? themeManager : null
    readonly property bool topGun: manager ? manager.topGun : false
    readonly property bool dayOps: manager ? manager.dayOps : false
    readonly property bool legacy: false
    readonly property string name: manager ? manager.currentTheme : "Standard"
    readonly property string displayFont: topGun ? "Arial Narrow" : "Segoe UI Variable"
    readonly property string telemetryFont: topGun ? "Consolas" : "Consolas"
    readonly property int panelRadius: topGun ? 2 : dayOps ? 4 : 6
    readonly property int controlRadius: topGun ? 1 : dayOps ? 3 : 4

    // Foundation
    readonly property color background: topGun ? "#071217" : dayOps ? "#c8ced0" : "#0d1013"
    readonly property color backgroundLift: topGun ? "#0b1b21" : dayOps ? "#d5dad9" : "#11171b"
    // Shell colors intentionally stand apart from page panels. Keeping this
    // trio here prevents a light page from being framed by a dark root
    // gradient or loader while Day Ops is selected.
    readonly property color shellGradientTop: topGun ? "#102127" : dayOps ? "#e0e3df" : "#151a1e"
    readonly property color shellGradientMiddle: topGun ? "#071217" : dayOps ? "#c8ced0" : "#0d1013"
    readonly property color shellGradientBottom: topGun ? "#050b0f" : dayOps ? "#b7c1c3" : "#080b0d"
    readonly property color shellMotifArc: topGun ? "#4c33221e" : dayOps ? "#245d6c22" : "#071e2930"
    readonly property color shellMotifVertical: topGun ? "#55422430" : dayOps ? "#376b7a26" : "#163f5261"
    readonly property color shellMotifHorizontal: topGun ? "#55422424" : dayOps ? "#48758420" : "#123f5261"
    readonly property color header: topGun ? "#09151a" : dayOps ? "#bbc5c8" : "#14191d"
    readonly property color panel: topGun ? "#0b1b21" : dayOps ? "#d9ddda" : "#1a1d23"
    readonly property color panelRaised: topGun ? "#10252c" : dayOps ? "#e9ece7" : "#20282d"
    readonly property color panelInset: topGun ? "#061116" : dayOps ? "#b9c2c4" : "#10171b"
    readonly property color panelWash: topGun ? "#1a332f" : dayOps ? "#c3d0cd" : "#243843"
    readonly property color border: topGun ? "#8f7043" : dayOps ? "#526d7c" : "#435660"
    readonly property color borderStrong: topGun ? "#c29a5b" : dayOps ? "#2e5268" : "#78aab9"
    readonly property color divider: topGun ? "#765d37" : dayOps ? "#78909a" : "#335268"
    readonly property color fastener: topGun ? "#b38b50" : dayOps ? "#587584" : "#5c9caf"

    // Typography and semantic state
    readonly property color text: topGun ? "#ead7a3" : dayOps ? "#142a38" : "#e8eeee"
    readonly property color textStrong: topGun ? "#f3deb0" : dayOps ? "#0e2534" : "#f3f7f7"
    readonly property color textMuted: topGun ? "#b6a27b" : dayOps ? "#4f6672" : "#9aa3a7"
    readonly property color textFaint: topGun ? "#816f55" : dayOps ? "#71828a" : "#77919a"
    readonly property color orange: topGun ? "#df6428" : dayOps ? "#c56622" : "#78aab9"
    readonly property color orangeBright: topGun ? "#ff7b31" : dayOps ? "#e17827" : "#a8d1dc"
    readonly property color cyan: topGun ? "#48b9c1" : dayOps ? "#237f90" : "#8fc8c0"
    readonly property color ready: topGun ? "#75b9a4" : dayOps ? "#3f8a72" : "#8fd5c9"
    readonly property color warning: topGun ? "#d9a75d" : dayOps ? "#b77b19" : "#d4ad69"
    readonly property color danger: topGun ? "#d76043" : dayOps ? "#b6463d" : "#ca9090"
    readonly property color ivory: topGun ? "#e8d8ad" : dayOps ? "#eff1e9" : "#dbe7e8"

    // Reusable control and graph treatment
    readonly property color control: topGun ? "#09171d" : dayOps ? "#d0d7d6" : "#10171b"
    readonly property color controlHover: topGun ? "#162b30" : dayOps ? "#c2d0d1" : "#142128"
    readonly property color controlPressed: topGun ? "#2b2b22" : dayOps ? "#aebfc4" : "#244550"
    readonly property color controlDisabled: topGun ? "#0b1114" : dayOps ? "#b5bcbd" : "#0c1013"
    readonly property color selection: topGun ? "#51301f" : dayOps ? "#b9d0d1" : "#315a66"
    readonly property color selectionCurrent: topGun ? "#332a20" : dayOps ? "#d8c29f" : "#244650"
    readonly property color graphBackground: topGun ? "#061116" : dayOps ? "#26353c" : "#0a0f12"
    readonly property color graphGrid: topGun ? "#5f5137" : dayOps ? "#526b76" : "#254653"
    readonly property color graphZero: topGun ? "#9a5034" : dayOps ? "#c27d32" : "#567784"
    readonly property color graphInput: topGun ? "#e5d6b1" : dayOps ? "#e2edea" : "#dbe7e8"
    readonly property color graphOutput: topGun ? "#4fc1c6" : dayOps ? "#42bcc7" : "#8fc8c0"
    readonly property color graphPreview: topGun ? "#ea7132" : dayOps ? "#e17827" : "#377da3"
    readonly property color graphLabel: topGun ? "#b9a57d" : dayOps ? "#c8d5d4" : "#76909a"
    readonly property color graphFrame: topGun ? "#8a6c41" : dayOps ? "#335566" : "#4b7081"
    readonly property color graphPanelGradientTop: topGun ? "#20352f" : dayOps ? "#3a4b52" : "#27383e"
    readonly property color graphPanelGradientMiddle: topGun ? "#132126" : dayOps ? "#2e4048" : "#17262b"
    readonly property color graphPanelGradientBottom: topGun ? "#061116" : dayOps ? "#202f36" : "#0b1114"

    readonly property color buttonSurface: topGun ? "#11191b" : dayOps ? "#c56622" : "#324f5a"
    readonly property color buttonHover: topGun ? "#3b241c" : dayOps ? "#e17827" : "#456c78"
    readonly property color buttonSecondary: topGun ? "#101b20" : dayOps ? "#c7cfd0" : "#222c32"
    readonly property color buttonSecondaryHover: topGun ? "#243035" : dayOps ? "#adbdc1" : "#303d44"
    readonly property color destructive: topGun ? "#451e1b" : dayOps ? "#d9aaa4" : "#2c2223"
    readonly property color tooltip: topGun ? "#0b161a" : dayOps ? "#e7e9e2" : "#151e23"

    // Devices use one shared semantic card.  These tokens deliberately make
    // Standard a clean, raised technical workspace without borrowing the
    // inset construction of Legacy; Top Gun and Day Ops retain their own
    // instrument/deck readings.  Legacy keeps its immutable original values
    // in the legacy presentation map.
    readonly property color devicePanelSurface: topGun ? "#e80b1b21" : dayOps ? panel : "#26343a"
    readonly property color devicePanelBorder: topGun ? border : dayOps ? border : "#5d7b87"
    readonly property color devicePanelAccent: topGun ? orangeBright : dayOps ? borderStrong : "#76b4c2"

    // Standard-page legacy cockpit accents. Centralizing them lets the shared
    // geometry retain its established dark Standard treatment while Day Ops
    // substitutes readable aluminum-panel surfaces rather than sprinkling
    // light-theme conditionals through every control.
    readonly property color cockpitSurface: dayOps ? panelRaised : "#ed182128"
    readonly property color cockpitSurfaceStrong: dayOps ? panel : "#e51a2328"
    readonly property color cockpitGraphSurface: dayOps ? panelRaised : "#eb11171b"
    readonly property color cockpitBorder: dayOps ? border : "#3b66747d"
    readonly property color cockpitActiveBorder: dayOps ? borderStrong : "#93a3cfda"
    readonly property color cockpitText: dayOps ? textStrong : "#edf7f7"
    readonly property color cockpitTelemetry: dayOps ? text : "#dce8ea"
    readonly property color cockpitLabel: dayOps ? textMuted : "#a7bbc0"
    readonly property color cockpitSubtle: dayOps ? textMuted : "#8c989d"
    readonly property color cockpitMetric: dayOps ? text : "#c8dce0"
    readonly property color cockpitHelp: dayOps ? textFaint : "#718a93"
    // The original Standard workspaces use a denser cockpit sub-language
    // than the primary panels. Keep that language semantic so Day Ops can
    // render the same hierarchy as light naval equipment rather than dark
    // Standard literals on an aluminum surface.
    readonly property color cockpitFaint: dayOps ? textFaint : "#7f8d94"
    readonly property color cockpitReady: dayOps ? ready : "#a9c9b3"
    readonly property color cockpitWarning: dayOps ? warning : "#d49b62"
    readonly property color cockpitDanger: dayOps ? danger : "#b77b86"
    readonly property color cockpitControl: dayOps ? control : "#0c1013"
    readonly property color cockpitControlHover: dayOps ? controlHover : "#363e43"
    readonly property color cockpitControlAccent: dayOps ? cyan : "#829da5"
    readonly property color cockpitInput: dayOps ? cyan : "#b9d1d8"
    readonly property color cockpitGraphInset: dayOps ? graphBackground : "#0c0f12"
    readonly property color cockpitGraphFrame: dayOps ? graphFrame : "#111518"
    readonly property color cockpitDialogSurface: dayOps ? tooltip : "#1b2126"
    readonly property color cockpitDialogBorder: dayOps ? borderStrong : "#3adce5e8"
    readonly property color cockpitDangerSurface: dayOps ? destructive : "#241b1b"
    readonly property color cockpitDangerBorder: dayOps ? danger : "#44bd7777"
    readonly property color cockpitReadySurface: dayOps ? panelWash : "#16262a"
    readonly property color cockpitActiveSurface: dayOps ? selection : "#ec263e48"
    readonly property color cockpitPressedSurface: dayOps ? selectionCurrent : "#ed20363c"
    readonly property color cockpitIdleSurface: dayOps ? panelRaised : "#dc151a1f"
    readonly property color curvePanelSurface: topGun ? "#d80b1b20" : dayOps ? panel : "#e61a282e"
    readonly property color curveMenuSurface: dayOps ? tooltip : "#101b20"
    readonly property color curveMenuBorder: dayOps ? borderStrong : "#4c7881"
    readonly property color curveDialogSurface: dayOps ? panelRaised : "#f018252b"
    readonly property color curveDialogBorder: dayOps ? borderStrong : "#5a7f89"
    readonly property color curveDangerSurface: dayOps ? destructive : "#f018252b"
    readonly property color curveDangerBorder: dayOps ? danger : "#805b56"

    function statusColor(severity) {
        if (severity === "error") return danger
        if (severity === "warning") return warning
        return ready
    }
}
