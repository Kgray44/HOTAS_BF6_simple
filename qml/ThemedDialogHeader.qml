import QtQuick 6.5

// Shared title treatment for application-owned dialogs. It deliberately owns
// its complete surface so a Qt Quick Controls default header can never leak a
// white/light platform title strip into a themed workflow.
Rectangle {
    id: header
    objectName: "themedDialogHeader"
    required property var theme
    property bool legacy: false
    property string heading: ""
    property string detail: ""
    // This is intentionally observable in the presentation lifecycle test.
    // A themed dialog body alone is insufficient: an unstyled Qt header is
    // especially conspicuous in Day Ops and makes Legacy look imported from
    // Standard. Keep the semantic identity explicit without changing the
    // shared dialog geometry.
    readonly property string surfaceTreatment: legacy ? "legacy-header"
                                                    : theme.topGun ? "top-gun-header"
                                                                   : theme.dayOps ? "day-ops-header"
                                                                                  : "standard-header"

    implicitHeight: theme.topGun ? 62 : legacy ? 58 : 56
    radius: legacy ? 4 : theme.topGun ? 1 : theme.controlRadius
    color: legacy ? "#132027" : theme.topGun ? theme.control : theme.panelRaised
    border.color: legacy ? "#52717c" : theme.topGun ? theme.orange : theme.borderStrong
    border.width: 1
    clip: true

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 52
        anchors.verticalCenter: parent.verticalCenter
        spacing: detail.length > 0 ? 3 : 0
        Text {
            width: parent.width
            text: header.heading.toUpperCase()
            color: header.legacy ? "#eef6f5" : header.theme.topGun ? header.theme.orangeBright : header.theme.textStrong
            font.pixelSize: header.theme.topGun ? 16 : 14
            font.bold: true
            font.family: header.theme.topGun ? header.theme.displayFont : undefined
            elide: Text.ElideRight
        }
        Text {
            visible: header.detail.length > 0
            width: parent.width
            text: header.detail
            color: header.legacy ? "#94adb5" : header.theme.textMuted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.bottom: parent.bottom
        width: theme.topGun ? 44 : 34
        height: 2
        color: legacy ? theme.fastener : theme.orange
    }
    Rectangle {
        visible: theme.topGun
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 2
        color: theme.cyan
    }
}
