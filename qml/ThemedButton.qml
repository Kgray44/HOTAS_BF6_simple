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
    color: !commandEnabled ? theme.controlDisabled
           : tone === "danger" ? Qt.rgba(dangerColor.r, dangerColor.g, dangerColor.b, mouse.containsMouse ? 0.25 : 0.14)
           : tone === "secondary" ? (mouse.containsMouse ? theme.buttonSecondaryHover : theme.buttonSecondary)
           : (mouse.containsMouse ? theme.buttonHover : theme.buttonSurface)
    border.color: !commandEnabled ? theme.border
                  : tone === "danger" ? theme.danger
                  : tone === "secondary" ? theme.border
                  : theme.orange
    opacity: commandEnabled ? 1 : 0.5

    Text {
        id: caption
        anchors.centerIn: parent
        anchors.margins: 8
        text: control.text
        color: !control.commandEnabled ? theme.textFaint : control.tone === "danger" ? theme.danger : theme.textStrong
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
