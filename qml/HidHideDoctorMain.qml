import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1440
    height: 880
    minimumWidth: 720
    minimumHeight: 560
    visible: true
    title: "HidHide Doctor — Deep System Diagnostics"
    color: "#111315"

    readonly property int densityGap: doctorSession.density === "Comfortable" ? 14 : doctorSession.density === "Dense" ? 6 : 10
    readonly property int densityPad: doctorSession.density === "Comfortable" ? 18 : doctorSession.density === "Dense" ? 8 : 12
    readonly property bool compactCommandCenter: width < 1040

    function toneColor(tone) {
        if (tone === "healthy") return "#75ba92"
        if (tone === "warning") return "#d5a352"
        if (tone === "fault") return "#d87878"
        if (tone === "limited") return "#8ea7bd"
        if (tone === "running") return "#6eadd6"
        return "#a8afb7"
    }
    function toneSurface(tone) {
        if (tone === "fault") return "#2b1c1e"
        if (tone === "warning") return "#292216"
        if (tone === "healthy") return "#18251e"
        return "#191c1f"
    }
    function openEvidence(evidenceId) {
        if (!evidenceId || evidenceId.length === 0) return
        doctorSession.selectEvidence(evidenceId)
        inspector.open()
    }

    component SectionLabel: Label {
        color: "#a8b0b8"
        font.family: "Segoe UI"
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1.5
    }

    component ThinRule: Rectangle { color: "#30353a"; height: 1; Layout.fillWidth: true }

    component Metric: Item {
        id: metricRoot
        required property string title
        required property var value
        required property string tone
        implicitWidth: 76
        implicitHeight: 47
        Column {
            anchors.fill: parent
            spacing: 2
            SectionLabel { text: metricRoot.title; font.pixelSize: 8; color: "#929aa2" }
            Label { text: metricRoot.value; color: root.toneColor(metricRoot.tone); font.pixelSize: 20; font.weight: Font.DemiBold }
        }
    }

    component PaneFrame: Rectangle {
        id: paneFrame
        required property string paneId
        required property string heading
        property alias bodyContent: paneLoader.sourceComponent
        color: "#181b1f"
        border.color: "#353a40"
        border.width: 1
        radius: 3
        clip: true
        Layout.fillHeight: true
        Layout.minimumWidth: 160
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: root.densityPad
                Layout.rightMargin: root.densityPad
                Layout.topMargin: 10
                Layout.bottomMargin: 8
                SectionLabel { text: paneFrame.heading; color: "#d9dee3"; Layout.fillWidth: true }
                ToolButton {
                    text: doctorSession.maximizedPane === paneFrame.paneId ? "↙" : "↗"
                    Accessible.name: doctorSession.maximizedPane === paneFrame.paneId ? "Restore pane" : "Maximize pane"
                    onClicked: doctorSession.setMaximizedPane(doctorSession.maximizedPane === paneFrame.paneId ? "" : paneFrame.paneId)
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#30353a" }
            Loader { id: paneLoader; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }

    header: ToolBar {
        height: 58
        background: Rectangle { color: "#171a1e"; border.color: "#30353a"; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 18
            spacing: 16
            ColumnLayout {
                spacing: 0
                Label { text: "HIDHIDE DOCTOR"; color: "#f0f2f4"; font.pixelSize: 17; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Label { text: "Deep System Diagnostics"; color: "#9da6ae"; font.pixelSize: 10 }
            }
            Rectangle { width: 1; height: 28; color: "#3a3f44" }
            Row {
                spacing: 6
                Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: "#6fb58e" }
                Label { text: "READ-ONLY DIAGNOSTICS"; color: "#b7dfc4"; font.pixelSize: 10; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
            Item { Layout.fillWidth: true }
            SectionLabel { text: "VIEW" }
            Button {
                text: "Focus"
                checkable: true
                checked: !doctorSession.commandCenter
                onClicked: doctorSession.setCommandCenter(false)
                Accessible.name: "Focus view"
            }
            Button {
                text: "Command Center"
                checkable: true
                checked: doctorSession.commandCenter
                onClicked: doctorSession.setCommandCenter(true)
                Accessible.name: "Command Center view"
            }
        }
    }

    footer: Rectangle {
        height: 29
        color: "#171a1e"
        border.color: "#30353a"
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 18
            SectionLabel { text: "CHECKS " + doctorSession.completedChecks + "/" + (doctorSession.completedChecks + doctorSession.remainingChecks) }
            SectionLabel { text: "HEALTHY " + doctorSession.healthyCheckCount; color: "#8bc6a4" }
            SectionLabel { text: "INFO " + doctorSession.informationalCheckCount }
            SectionLabel { text: "WARNING " + doctorSession.warningCheckCount; color: doctorSession.warningCheckCount ? "#d5a352" : "#a8b0b8" }
            SectionLabel { text: "FAILED " + doctorSession.failedCheckCount; color: doctorSession.failedCheckCount ? "#d87878" : "#a8b0b8" }
            Item { Layout.fillWidth: true }
            SectionLabel { text: "ELAPSED " + doctorSession.elapsed }
            SectionLabel { text: "SESSION " + doctorSession.sessionId.slice(-8) }
            SectionLabel { text: "READ ONLY"; color: "#b7dfc4" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.densityPad
        spacing: root.densityGap

        Rectangle {
            visible: doctorSession.simulationLabel.length > 0
            Layout.fillWidth: true
            implicitHeight: 32
            color: "#24344a"
            border.color: "#55799c"
            radius: 2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label { text: "SIMULATED / DEVELOPMENT FIXTURE"; color: "#c6e2fb"; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                Label { text: doctorSession.simulationLabel; color: "#dcecff"; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: "Not this machine"; color: "#9eb9d2"; font.pixelSize: 10 }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 31
            color: "#191c20"
            border.color: "#30353a"
            radius: 2
            Label { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter; text: doctorSession.environmentStrip; color: "#bac2ca"; font.pixelSize: 11; elide: Text.ElideRight }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 71
            color: "#181b1f"
            border.color: "#353a40"
            radius: 3
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.densityPad
                anchors.rightMargin: root.densityPad
                spacing: 0
                Repeater {
                    model: doctorSession.healthDomains
                    delegate: Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            Label { text: modelData.label; color: "#a7afb7"; font.pixelSize: 9; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                            Label { text: modelData.symbol; color: root.toneColor(modelData.tone); font.pixelSize: 21; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; width: parent.width }
                            Label { text: modelData.status; color: root.toneColor(modelData.tone); font.pixelSize: 8; horizontalAlignment: Text.AlignHCenter; width: parent.width; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 105
            color: "#1a1d21"
            border.color: "#353a40"
            radius: 3
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.densityPad
                anchors.rightMargin: root.densityPad
                spacing: 14
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        SectionLabel { text: "DIAGNOSTIC SESSION"; color: "#d6dce1"; Layout.fillWidth: true }
                        Label { text: doctorSession.sessionState; color: doctorSession.scanRunning ? "#7db9df" : "#b9c2ca"; font.pixelSize: 10; font.weight: Font.DemiBold }
                    }
                    RowLayout {
                        Label { text: doctorSession.currentPhase.toUpperCase(); color: "#dce1e5"; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: doctorSession.overallProgress + "%"; color: "#78b990"; font.pixelSize: 22; font.weight: Font.DemiBold }
                    }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: doctorSession.overallProgress; Accessible.name: "Overall diagnostic progress" }
                    RowLayout {
                        Label { text: doctorSession.currentStepId.length ? doctorSession.currentStepId + "  " + doctorSession.currentStep : doctorSession.currentStep; color: "#acb5bd"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: doctorSession.currentStepProgress + "% step"; color: "#9ea8b1"; font.pixelSize: 10 }
                    }
                }
                Rectangle { width: 1; Layout.fillHeight: true; color: "#363b40" }
                Metric { title: "CHECKS"; value: doctorSession.completedChecks + "/" + (doctorSession.completedChecks + doctorSession.remainingChecks); tone: "neutral" }
                Metric { title: "HEALTHY"; value: doctorSession.healthyCheckCount; tone: "healthy" }
                Metric { title: "WARNINGS"; value: doctorSession.warningCheckCount; tone: "warning" }
                Metric { title: "FAILED"; value: doctorSession.failedCheckCount; tone: "fault" }
                Metric { title: "DIAGNOSES"; value: doctorSession.diagnosisCards.length; tone: doctorSession.diagnosisCards.length ? "warning" : "healthy" }
                ColumnLayout {
                    spacing: 5
                    Button { text: doctorSession.scanRunning ? "Cancel scan" : "Run new scan"; onClicked: doctorSession.scanRunning ? doctorSession.requestCancellation() : doctorSession.requestRerun(); Accessible.name: text }
                    Label { text: "No elevation · no repair"; color: "#9ca5ae"; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !root.compactCommandCenter || !doctorSession.commandCenter
            SectionLabel { text: "WORKSPACE"; Layout.fillWidth: true }
            SectionLabel { text: "DENSITY" }
            ComboBox {
                model: ["Comfortable", "Compact", "Dense"]
                currentIndex: model.indexOf(doctorSession.density)
                onActivated: doctorSession.setDensity(currentText)
                Accessible.name: "Information density"
            }
            CheckBox { text: "Live Evidence"; checked: doctorSession.liveEvidenceVisible; onToggled: doctorSession.setLiveEvidenceVisible(checked); Accessible.name: "Show live evidence" }
            Button { text: "Evidence Inspector"; onClicked: inspector.open(); Accessible.name: "Open Evidence Inspector" }
        }

        Rectangle {
            visible: root.compactCommandCenter && doctorSession.commandCenter
            Layout.fillWidth: true
            implicitHeight: 42
            color: "#25231c"
            border.color: "#66512f"
            radius: 2
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                Label { text: "Command Center needs more width. Focus View is shown without changing this diagnostic session."; color: "#e3c98e"; Layout.fillWidth: true; font.pixelSize: 11 }
                Button { text: "Use Focus"; onClicked: doctorSession.setCommandCenter(false) }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: doctorSession.commandCenter && !root.compactCommandCenter ? commandCenter : focusView
        }
    }

    Component {
        id: planBody
        ScrollView {
            clip: true
            ListView {
                model: doctorSession.planPhases
                spacing: 1
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: doctorSession.density === "Comfortable" ? 62 : doctorSession.density === "Dense" ? 42 : 52
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: root.densityPad; anchors.rightMargin: root.densityPad
                        Label { text: modelData.symbol; color: root.toneColor(modelData.tone); font.pixelSize: 15; Layout.preferredWidth: 18 }
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            Label { text: modelData.title; color: "#d4dade"; font.pixelSize: 10; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.complete + " / " + modelData.total + " · " + modelData.status; color: "#9ba5ae"; font.pixelSize: 9; Layout.fillWidth: true }
                            ProgressBar { visible: modelData.status === "RUNNING"; Layout.fillWidth: true; from: 0; to: 100; value: modelData.progress }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: "#292e33" }
                }
            }
        }
    }

    Component {
        id: currentBody
        Flickable {
            contentWidth: width; contentHeight: details.implicitHeight + root.densityPad * 2; clip: true
            ColumnLayout {
                id: details; width: parent.width - root.densityPad * 2; x: root.densityPad; y: root.densityPad; spacing: root.densityGap
                property var operation: doctorSession.currentOperationDetails
                SectionLabel { text: details.operation.phase.toUpperCase() }
                Label { text: details.operation.title; color: "#e1e5e8"; font.pixelSize: 16; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: details.operation.checkId.length ? details.operation.checkId : "NO ACTIVE CHECK"; color: "#8fb3cf"; font.family: "Consolas"; font.pixelSize: 11 }
                RowLayout { Layout.fillWidth: true
                    Label { text: "● " + details.operation.status; color: doctorSession.scanRunning ? "#75b5dc" : "#9fadba"; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Label { text: details.operation.progress + "%"; color: "#d0d6dc"; font.pixelSize: 12 }
                }
                ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: details.operation.progress }
                ThinRule {}
                SectionLabel { text: "OPERATION TRANSPARENCY" }
                GridLayout { columns: 2; columnSpacing: 12; rowSpacing: 8; Layout.fillWidth: true
                    SectionLabel { text: "ELAPSED" }
                    Label { text: details.operation.elapsed; color: "#d4dade"; font.family: "Consolas"; font.pixelSize: 11 }
                    SectionLabel { text: "TIMEOUT" }
                    Label { text: details.operation.timeout; color: "#d4dade"; font.family: "Consolas"; font.pixelSize: 11 }
                    SectionLabel { text: "ACTION" }
                    Label { text: "None required"; color: "#9acaaa"; font.pixelSize: 11 }
                }
                Label { text: details.operation.detail; color: "#aeb6be"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }

    Component {
        id: findingsBody
        ScrollView {
            clip: true
            ColumnLayout {
                width: parent.width; spacing: root.densityGap
                Repeater {
                    model: doctorSession.diagnosisCards
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: diagnosisColumn.implicitHeight + root.densityPad * 2
                        color: root.toneSurface(modelData.tone); border.color: root.toneColor(modelData.tone); border.width: 1; radius: 2
                        MouseArea { anchors.fill: parent; onClicked: root.openEvidence(modelData.evidenceId) }
                        ColumnLayout { id: diagnosisColumn; anchors.fill: parent; anchors.margins: root.densityPad; spacing: 5
                            RowLayout { Layout.fillWidth: true
                                SectionLabel { text: modelData.role; color: root.toneColor(modelData.tone); Layout.fillWidth: true }
                                Label { text: modelData.confidence.toUpperCase() + " · " + modelData.score + "%"; color: root.toneColor(modelData.tone); font.pixelSize: 10; font.weight: Font.DemiBold }
                            }
                            Label { text: modelData.title; color: "#f0f2f4"; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: modelData.summary; color: "#c4cbd1"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: "IMPACT  " + modelData.impact; color: "#aeb6be"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: "REPAIRABILITY  " + modelData.repairability; color: "#aeb6be"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
                Repeater {
                    model: doctorSession.findingCards
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: findingColumn.implicitHeight + root.densityPad * 2
                        color: "#1b1e22"; border.color: "#343a40"; border.width: 1; radius: 2
                        MouseArea { anchors.fill: parent; onClicked: root.openEvidence(modelData.evidenceId) }
                        ColumnLayout { id: findingColumn; anchors.fill: parent; anchors.margins: root.densityPad; spacing: 4
                            RowLayout { Layout.fillWidth: true
                                SectionLabel { text: modelData.severity; color: root.toneColor(modelData.tone); Layout.fillWidth: true }
                                Label { text: modelData.confidence; color: root.toneColor(modelData.tone); font.pixelSize: 10 }
                            }
                            Label { text: modelData.title; color: "#dfe4e8"; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: modelData.summary; color: "#b7c0c8"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
                Rectangle {
                    visible: doctorSession.diagnosisCards.length === 0 && doctorSession.findingCards.length === 0
                    Layout.fillWidth: true; implicitHeight: 98; color: "#18221d"; border.color: "#355342"; radius: 2
                    Column { anchors.centerIn: parent; spacing: 5
                        Label { text: "✓ HIDHIDE HEALTHY"; color: "#8fc6a4"; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Label { text: "No diagnosis or user action required."; color: "#b6c4ba"; font.pixelSize: 11 }
                    }
                }
                Rectangle {
                    visible: doctorSession.liveEvidenceVisible
                    Layout.fillWidth: true; implicitHeight: 1; color: "#3a4046"
                }
                Repeater {
                    model: doctorSession.liveEvidenceVisible ? doctorSession.activityRows : []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true; Layout.leftMargin: root.densityPad; Layout.rightMargin: root.densityPad
                        Label { text: modelData.time; color: "#87919a"; font.pixelSize: 9; font.family: "Consolas" }
                        Label { text: modelData.checkId; color: "#8fb3cf"; font.pixelSize: 9; font.family: "Consolas" }
                        Label { text: modelData.symbol; color: root.toneColor(modelData.tone); font.pixelSize: 10 }
                        Label { text: modelData.title; color: "#b8c0c7"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                        MouseArea { Layout.preferredWidth: 18; Layout.preferredHeight: 18; onClicked: root.openEvidence(modelData.evidenceId) }
                    }
                }
            }
        }
    }

    Component {
        id: actionBody
        Flickable {
            contentWidth: width; contentHeight: actionColumn.implicitHeight + root.densityPad * 2; clip: true
            ColumnLayout {
                id: actionColumn; width: parent.width - root.densityPad * 2; x: root.densityPad; y: root.densityPad; spacing: root.densityGap
                Label { text: doctorSession.userActionTitle.toUpperCase(); color: "#d6dce1"; font.pixelSize: 14; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: doctorSession.userActionDetail; color: "#b8c1c9"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ThinRule {}
                SectionLabel { text: "PHASE 2 BOUNDARY" }
                Label { text: "Diagnosis and repairability are shown here. Repairs are unavailable by design and no HidHide or Windows configuration has been changed."; color: "#9fa9b1"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Button { text: "REPAIRS DISABLED IN PHASE 2"; enabled: false; Layout.fillWidth: true; Accessible.name: "Repairs disabled in Phase 2" }
                Item { Layout.fillHeight: true }
                Label { text: "Session " + doctorSession.sessionId.slice(-8); color: "#77828b"; font.family: "Consolas"; font.pixelSize: 9 }
            }
        }
    }

    Component {
        id: commandCenter
        Item {
            SplitView {
                id: split
                anchors.fill: parent
                orientation: Qt.Horizontal
                onWidthChanged: widthTimer.restart()
                PaneFrame {
                    visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "plan"
                    paneId: "plan"; heading: "DIAGNOSTIC PLAN"; bodyContent: planBody
                    SplitView.preferredWidth: doctorSession.maximizedPane === "plan" ? split.width : Number(doctorSession.paneWidths[0]) * split.width / 100
                    SplitView.minimumWidth: 175
                }
                PaneFrame {
                    visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "current"
                    paneId: "current"; heading: "CURRENT STEP"; bodyContent: currentBody
                    SplitView.preferredWidth: doctorSession.maximizedPane === "current" ? split.width : Number(doctorSession.paneWidths[1]) * split.width / 100
                    SplitView.minimumWidth: 205
                }
                PaneFrame {
                    visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "findings"
                    paneId: "findings"; heading: "FINDINGS & DIAGNOSES"; bodyContent: findingsBody
                    SplitView.preferredWidth: doctorSession.maximizedPane === "findings" ? split.width : Number(doctorSession.paneWidths[2]) * split.width / 100
                    SplitView.minimumWidth: 260
                }
                PaneFrame {
                    visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "action"
                    paneId: "action"; heading: "USER ACTION"; bodyContent: actionBody
                    SplitView.preferredWidth: doctorSession.maximizedPane === "action" ? split.width : Number(doctorSession.paneWidths[3]) * split.width / 100
                    SplitView.minimumWidth: 175
                }
            }
            Timer {
                id: widthTimer; interval: 450; repeat: false
                onTriggered: {
                    if (doctorSession.maximizedPane.length === 0)
                        doctorSession.savePaneWidths([split.itemAt(0).width, split.itemAt(1).width, split.itemAt(2).width, split.itemAt(3).width])
                }
            }
            Shortcut { sequence: "Escape"; onActivated: doctorSession.setMaximizedPane("") }
        }
    }

    Component {
        id: focusView
        Flickable {
            contentWidth: width; contentHeight: focusColumn.implicitHeight; clip: true
            ColumnLayout {
                id: focusColumn; width: parent.width; spacing: root.densityGap
                PaneFrame { paneId: "current"; heading: "CURRENT STEP"; bodyContent: currentBody; Layout.fillHeight: false; implicitHeight: 255 }
                PaneFrame { paneId: "findings"; heading: "FINDINGS & DIAGNOSES"; bodyContent: findingsBody; Layout.fillHeight: false; implicitHeight: 420 }
                PaneFrame { paneId: "plan"; heading: "DIAGNOSTIC PLAN"; bodyContent: planBody; Layout.fillHeight: false; implicitHeight: 330 }
                PaneFrame { paneId: "action"; heading: "USER ACTION"; bodyContent: actionBody; Layout.fillHeight: false; implicitHeight: 230 }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: activityColumn.implicitHeight + root.densityPad * 2; color: "#181b1f"; border.color: "#353a40"; radius: 3
                    ColumnLayout { id: activityColumn; anchors.fill: parent; anchors.margins: root.densityPad; spacing: 5
                        SectionLabel { text: "ACTIVITY TIMELINE"; color: "#d6dce1" }
                        Repeater { model: doctorSession.activityRows
                            delegate: RowLayout { required property var modelData; Layout.fillWidth: true
                                Label { text: modelData.time; color: "#87919a"; font.family: "Consolas"; font.pixelSize: 9 }
                                Label { text: modelData.checkId; color: "#8fb3cf"; font.family: "Consolas"; font.pixelSize: 9 }
                                Label { text: modelData.title; color: "#bbc3ca"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                Button { text: "Evidence"; onClicked: root.openEvidence(modelData.evidenceId); visible: modelData.evidenceId.length > 0 }
                            }
                        }
                    }
                }
            }
        }
    }

    Drawer {
        id: inspector
        edge: Qt.BottomEdge
        width: parent.width
        height: Math.min(parent.height * 0.54, 440)
        modal: false
        interactive: true
        background: Rectangle { color: "#15181c"; border.color: "#48515a"; border.width: 1 }
        property var selected: doctorSession.selectedEvidence
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 9
            RowLayout { Layout.fillWidth: true
                ColumnLayout { spacing: 1
                    SectionLabel { text: "EVIDENCE INSPECTOR"; color: "#dce2e7" }
                    Label { text: inspector.selected.checkId || "Select a finding, diagnosis, or activity item"; color: "#8fb3cf"; font.family: "Consolas"; font.pixelSize: 12 }
                }
                Item { Layout.fillWidth: true }
                Button { text: "Close"; onClicked: inspector.close() }
            }
            ThinRule {}
            GridLayout { columns: root.width > 920 ? 3 : 2; columnSpacing: 20; rowSpacing: 8; Layout.fillWidth: true
                SectionLabel { text: "SOURCE" }
                Label { text: inspector.selected.source || "Unknown"; color: "#d1d7dc"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                SectionLabel { text: "TIMESTAMP" }
                Label { text: inspector.selected.timestamp || "Unknown"; color: "#d1d7dc"; font.family: "Consolas"; font.pixelSize: 10 }
                SectionLabel { text: "DURATION" }
                Label { text: inspector.selected.duration || "Unknown"; color: "#d1d7dc"; font.family: "Consolas"; font.pixelSize: 10 }
                SectionLabel { text: "PROVENANCE" }
                Label { text: inspector.selected.provenance || "Unknown"; color: "#d1d7dc"; font.pixelSize: 11 }
                SectionLabel { text: "NATIVE ERROR" }
                Label { text: inspector.selected.error || "None"; color: inspector.selected.error && inspector.selected.error !== "None" ? "#dc8585" : "#b8c2ca"; font.family: "Consolas"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            Label { text: "SUMMARY"; color: "#9da7af"; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
            TextArea { text: inspector.selected.summary || "No evidence is selected."; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: "#d6dce1"; background: Rectangle { color: "#1d2126"; border.color: "#343a40" }
                Layout.fillWidth: true; Layout.preferredHeight: 58 }
            Label { text: "TECHNICAL DETAILS"; color: "#9da7af"; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
            TextArea { text: inspector.selected.technical || inspector.selected.structured || "No additional raw detail was captured."; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: "#c0c8cf"; font.family: "Consolas"; background: Rectangle { color: "#1d2126"; border.color: "#343a40" }
                Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
