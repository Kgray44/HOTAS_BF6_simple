import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "DoctorTheme.js" as Theme

Item {
    id: root
    property string title: ""
    property string countText: ""
    property string statusText: ""
    property bool maximized: false
    signal maximizeRequested()
    implicitHeight: 42
    Rectangle { anchors.fill: parent; color: Theme.surface }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 7
        spacing: 8
        Label { text: root.title; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.1; Layout.fillWidth: true; elide: Text.ElideRight }
        Label { visible: root.countText.length > 0; text: root.countText; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: 10 }
        Label { visible: root.statusText.length > 0; text: root.statusText; color: Theme.textMuted; font.family: Theme.ui; font.pixelSize: 9; elide: Text.ElideRight }
        DoctorButton { text: root.maximized ? "RESTORE" : "MAX"; compact: true; tooltipText: root.maximized ? "Restore pane" : "Maximize pane"; accessibleName: tooltipText; onClicked: root.maximizeRequested() }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.separator }
}
