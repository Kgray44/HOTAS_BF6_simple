import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    title: "HidHide Doctor — Phase 0"
    color: "#10151d"

    component Surface: Rectangle {
        required property string heading
        required property string detail
        Layout.fillWidth: true
        color: "#192330"
        border.color: "#38536e"
        border.width: 1
        radius: 6
        implicitHeight: content.implicitHeight + 24
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 12
            spacing: 7
            Label { text: parent.parent.heading; color: "#d9ecff"; font.bold: true; font.pixelSize: 14 }
            Label { text: parent.parent.detail; color: "#bed0df"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }

    header: ToolBar {
        background: Rectangle { color: "#17212d" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            Label { text: "HIDHIDE DOCTOR"; color: "#edf7ff"; font.bold: true; font.pixelSize: 17 }
            Label { text: "Standalone Phase 0 architecture shell"; color: "#a9bfce"; Layout.fillWidth: true }
            Button {
                text: doctorSession.commandCenter ? "Focus View" : "Command Center"
                onClicked: doctorSession.togglePresentation()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: doctorSession.currentPhase + " · " + doctorSession.currentStep; color: "#ffffff"; font.pixelSize: 18; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: doctorSession.overallProgress + "% overall"; color: "#75d6a1"; font.bold: true }
        }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: doctorSession.overallProgress }
        Label { text: doctorSession.buildIdentity + "\nSession " + doctorSession.sessionId; color: "#91aabd"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: doctorSession.commandCenter ? commandCenter : focusView
        }
    }

    Component {
        id: focusView
        Flickable {
            contentWidth: width
            contentHeight: focusColumn.implicitHeight
            clip: true
            ColumnLayout {
                id: focusColumn
                width: parent.width
                spacing: 12
                Surface { heading: "Diagnostic Plan"; detail: doctorSession.planItems.join("\n") }
                Surface { heading: "Current Step"; detail: doctorSession.currentStepId + "\n" + doctorSession.currentStep + "\n" + doctorSession.currentStepProgress + "% complete" }
                Surface { heading: "Findings"; detail: doctorSession.findingItems.join("\n") }
                Surface { heading: "User Action"; detail: doctorSession.userActionTitle + "\n" + doctorSession.userActionDetail }
            }
        }
    }

    Component {
        id: commandCenter
        GridLayout {
            columns: width >= 900 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 12
            Surface { Layout.fillHeight: true; heading: "Diagnostic Plan"; detail: doctorSession.planItems.join("\n") }
            Surface { Layout.fillHeight: true; heading: "Current Step"; detail: doctorSession.currentStepId + "\n" + doctorSession.currentStep + "\n" + doctorSession.currentStepProgress + "% complete" }
            Surface { Layout.fillHeight: true; heading: "Findings"; detail: doctorSession.findingItems.join("\n") }
            Surface { Layout.fillHeight: true; heading: "User Action"; detail: doctorSession.userActionTitle + "\n" + doctorSession.userActionDetail }
        }
    }
}
