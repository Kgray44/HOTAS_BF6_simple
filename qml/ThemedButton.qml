import QtQuick 6.5

Rectangle {
    id: control
    required property var theme
    property string text: "ACTION"
    property bool commandEnabled: true
    property string tone: "primary" // primary, secondary, danger
    property bool compact: false
    property bool legacy: !!theme.legacy
    // Keep a typed color at the boundary. Legacy passes a compact token map
    // whose values begin as strings, while Theme.qml exposes QColor values.
    // Accessing `.r` directly on the map was therefore unsafe in Legacy.
    property color dangerColor: theme.danger
    signal triggered()

    implicitWidth: Math.max(compact ? 34 : (legacy ? 110 : 92), caption.implicitWidth + (compact ? 18 : (legacy ? 30 : 24)))
    implicitHeight: compact ? 30 : (legacy ? 36 : 34)
    radius: legacy ? 3 : theme.controlRadius
    // Legacy already has a deliberately tuned CommandButton treatment.  Keep
    // these exact values in the shared primitive so Devices can reuse it
    // without bringing a Standard-looking button into the Legacy surface.
    color: !commandEnabled ? (legacy ? "#151a1e" : theme.controlDisabled)
           : tone === "danger" ? (legacy ? (mouse.containsMouse ? "#4f3439" : "#35272b")
                                           : Qt.rgba(dangerColor.r, dangerColor.g, dangerColor.b, mouse.containsMouse ? 0.25 : 0.14))
           : tone === "secondary" ? (mouse.containsMouse ? theme.buttonSecondaryHover : theme.buttonSecondary)
           : (mouse.containsMouse ? theme.buttonHover : theme.buttonSurface)
    border.color: !commandEnabled ? (legacy ? "#182f3539" : theme.border)
                  : tone === "danger" ? theme.danger
                  : tone === "secondary" ? (legacy ? "#536975" : theme.border)
                  : theme.orange
    opacity: commandEnabled ? 1 : 0.5

    Text {
        id: caption
        anchors.centerIn: parent
        anchors.margins: 8
        text: control.text
        color: !control.commandEnabled ? (control.legacy ? "#879196" : theme.textFaint)
                                        : control.tone === "danger" ? theme.danger
                                                                     : (control.legacy ? "#f0f4f5" : theme.textStrong)
        font.pixelSize: control.compact ? 10 : 11
        font.bold: true
        font.family: theme.topGun ? theme.displayFont : "Segoe UI Variable"
        elide: Text.ElideRight
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: control.commandEnabled
        hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: control.triggered()
    }
}
