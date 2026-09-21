import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "doctor" as Doctor
import "doctor/DoctorTheme.js" as Theme

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 820
    minimumHeight: 620
    visible: true
    title: "HidHide Doctor — Deep System Diagnostics"
    color: Theme.workspace

    readonly property int densityGap: doctorSession.density === "Comfortable" ? 14 : doctorSession.density === "Dense" ? 6 : 10
    readonly property int densityPad: doctorSession.density === "Comfortable" ? 18 : doctorSession.density === "Dense" ? 8 : 12
    readonly property int cardPad: doctorSession.density === "Comfortable" ? 18 : doctorSession.density === "Dense" ? 10 : 14
    readonly property int cardGap: doctorSession.density === "Comfortable" ? 10 : doctorSession.density === "Dense" ? 5 : 7
    readonly property real densityTextScale: doctorSession.density === "Comfortable" ? 1.12 : doctorSession.density === "Dense" ? 0.92 : 1.0
    readonly property int workspaceOuterMargin: 12
    readonly property bool commandCenterFits: width >= 1310
    readonly property bool usingFocusFallback: doctorSession.commandCenter && !commandCenterFits

    function toneColor(tone) { return Theme.tone(tone) }
    function textPx(base) { return Math.max(9, Math.round(base * densityTextScale)) }
    function openEvidence(evidenceId) {
        if (!evidenceId || evidenceId.length === 0) return
        doctorSession.selectEvidence(evidenceId)
        inspector.open()
    }

    FileDialog {
        id: exportReportFile
        title: "Export HidHide Doctor report"
        fileMode: FileDialog.SaveFile
        nameFilters: exportSurface.format === "JSON" ? ["Doctor JSON report (*.json)"] : exportSurface.format === "Plain Text" ? ["Doctor text report (*.txt)"] : ["Doctor Markdown report (*.md)"]
        defaultSuffix: exportSurface.format === "JSON" ? "json" : exportSurface.format === "Plain Text" ? "txt" : "md"
        onAccepted: doctorSession.exportReportUrl(selectedFile, exportSurface.scope, exportSurface.detail, exportSurface.format, exportSurface.privacy)
    }

    FolderDialog {
        id: exportBundleFolder
        title: "Choose a destination folder for the HidHide Doctor diagnostic bundle"
        onAccepted: doctorSession.exportDiagnosticBundleUrl(selectedFolder, exportSurface.privacy)
    }

    QtObject {
        id: inspector
        objectName: "evidenceInspectorState"
        property bool visible: false
        function open() { visible = true }
        function close() { visible = false }
    }

    Shortcut {
        sequence: "Ctrl+C"
        enabled: inspector.visible
        onActivated: doctorSession.copySelectedEvidence()
    }

    component Eyebrow: Label {
        color: Theme.textMuted
        font.family: Theme.ui
        font.pixelSize: 9
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }

    component PaneSurface: Rectangle {
        id: pane
        required property string paneId
        required property string heading
        property string countText: ""
        property string statusText: ""
        property string reportScope: "Entire Session"
        property Component bodyContent
        property bool maximizable: true
        color: Theme.surface
        border.color: Theme.separator
        border.width: 1
        radius: 2
        clip: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Doctor.DoctorPaneHeader {
                Layout.fillWidth: true
                title: pane.heading
                countText: pane.countText
                statusText: pane.statusText
                maximized: doctorSession.maximizedPane === pane.paneId
                typographyScale: root.densityTextScale
                visible: pane.maximizable
                onMaximizeRequested: doctorSession.setMaximizedPane(doctorSession.maximizedPane === pane.paneId ? "" : pane.paneId)
                onCopyRequested: doctorSession.copyReportSection(pane.reportScope)
            }
            Rectangle { visible: !pane.maximizable; Layout.fillWidth: true; implicitHeight: 35; color: Theme.surface
                Eyebrow { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12; text: pane.heading }
            }
            Loader { Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: pane.bodyContent }
        }
    }

    header: ToolBar {
        height: 62
        background: Rectangle { color: Theme.surface; border.color: Theme.separator; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 18
            spacing: 15
            ColumnLayout {
                spacing: 0
                Label { text: "HIDHIDE DOCTOR"; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: root.textPx(17); font.weight: Font.DemiBold; font.letterSpacing: 1.3 }
                Label { text: "Deep System Diagnostics"; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(11) }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.separator }
            Doctor.DoctorStatusPill { text: "READ-ONLY DIAGNOSTICS"; tone: "information" }
            Item { Layout.fillWidth: true }
            Eyebrow { text: "VIEW" }
            Doctor.DoctorSegmentedControl {
                values: ["Focus", "Command Center"]
                currentValue: doctorSession.commandCenter ? "Command Center" : "Focus"
                accessibleName: "Diagnostic workspace view"
                onActivated: function(value) { doctorSession.setCommandCenter(value === "Command Center") }
            }
        }
    }

    Popup {
        id: exportSurface
        modal: true
        focus: true
        width: Math.min(root.width - 48, 620)
        height: exportColumn.implicitHeight + 34
        anchors.centerIn: Overlay.overlay
        padding: 0
        property string scope: "Entire Session"
        property string detail: "Detailed"
        property string format: "Markdown"
        property string privacy: "Safe to Share"
        background: Rectangle { color: Theme.elevated; border.color: Theme.separatorStrong; border.width: 1; radius: 2 }
        ColumnLayout {
            id: exportColumn
            anchors.fill: parent
            anchors.margins: 17
            spacing: 12
            RowLayout { Layout.fillWidth: true
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Eyebrow { text: "EXPORT REPORT" }
                    Label { text: "Compose from the complete structured DoctorSession, not visible pane text."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                Doctor.DoctorButton { text: "Close"; compact: true; onClicked: exportSurface.close() }
            }
            Doctor.DoctorDivider { Layout.fillWidth: true }
            Eyebrow { text: "SCOPE" }
            Doctor.DoctorSegmentedControl { Layout.fillWidth: true; values: ["Entire Session", "Diagnostic Plan", "Findings & Diagnoses", "Evidence", "User Actions", "Repair History"]; currentValue: exportSurface.scope; onActivated: function(value) { exportSurface.scope = value } }
            Eyebrow { text: "DETAIL" }
            Doctor.DoctorSegmentedControl { Layout.fillWidth: true; values: ["Summary", "Detailed", "Forensic / Everything"]; currentValue: exportSurface.detail; onActivated: function(value) { exportSurface.detail = value } }
            Eyebrow { text: "FORMAT" }
            Doctor.DoctorSegmentedControl { Layout.fillWidth: true; values: ["Markdown", "JSON", "Plain Text", "Diagnostic Bundle"]; currentValue: exportSurface.format; onActivated: function(value) { exportSurface.format = value } }
            Eyebrow { text: "PRIVACY" }
            Doctor.DoctorSegmentedControl { Layout.fillWidth: true; values: ["Safe to Share", "Local / Unredacted"]; currentValue: exportSurface.privacy; onActivated: function(value) { exportSurface.privacy = value } }
            Rectangle { Layout.fillWidth: true; implicitHeight: previewText.implicitHeight + 14; color: Theme.inset; border.color: Theme.separator; radius: 1
                Label { id: previewText; anchors.fill: parent; anchors.margins: 7; text: exportSurface.privacy === "Safe to Share" ? "INCLUDED: session, plan, results, findings, diagnosis, action ledger, timeline, bounded evidence. REDACTED: sensitive local evidence. EXCLUDED: unrelated personal data and automatic upload." : "LOCAL / UNREDACTED: bounded Doctor evidence is retained locally. Nothing is uploaded automatically."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(10); wrapMode: Text.WordWrap }
            }
            Label { visible: doctorSession.reportStatus.length > 0; text: doctorSession.reportStatus; color: Theme.information; font.family: Theme.ui; font.pixelSize: root.textPx(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Doctor.DoctorButton { text: doctorSession.reportBusy ? "Exporting…" : exportSurface.format === "Diagnostic Bundle" ? "Choose bundle destination" : "Choose report file"; tone: "primary"; enabled: !doctorSession.reportBusy; onClicked: { if (exportSurface.format === "Diagnostic Bundle") exportBundleFolder.open(); else exportReportFile.open() } }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        // Outer workspace geometry is intentionally density-invariant.  Density
        // changes the reading rhythm inside cards, never a released split size.
        anchors.margins: root.workspaceOuterMargin
        spacing: root.densityGap

        Rectangle {
            visible: doctorSession.integrationNotice.length > 0
            Layout.fillWidth: true
            implicitHeight: integrationNoticeText.implicitHeight + 18
            color: doctorSession.integrationComponentMismatch ? "#3a2020" : "#192831"
            border.color: doctorSession.integrationComponentMismatch ? Theme.fault : "#39708a"
            radius: 2
            Label {
                id: integrationNoticeText
                anchors.fill: parent
                anchors.margins: 9
                text: doctorSession.integrationNotice
                color: Theme.textPrimary
                font.family: Theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            visible: doctorSession.simulationLabel.length > 0
            Layout.fillWidth: true
            implicitHeight: 34
            color: "#192831"
            border.color: "#39708a"
            radius: 2
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                Eyebrow { text: "DEVELOPMENT FIXTURE"; color: Theme.information }
                Label { text: doctorSession.simulationLabel; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            color: Theme.inset
            border.color: Theme.separator
            radius: 2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 0
                Repeater {
                    model: doctorSession.environmentGroups
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { spacing: 1; Layout.fillWidth: true
                            Eyebrow { text: modelData.label }
                            Label { text: modelData.value; color: Theme.textSecondary; font.family: modelData.label === "SESSION" ? Theme.mono : Theme.ui; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        Rectangle { visible: index < doctorSession.environmentGroups.length - 1; Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.separator }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 68
            color: Theme.surface
            border.color: Theme.separator
            radius: 2
            GridLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                columns: 7
                columnSpacing: 8
                Repeater {
                    model: doctorSession.healthDomains
                    delegate: Item {
                        required property var modelData
                        Accessible.name: modelData.label + " " + modelData.status
                        Accessible.description: modelData.detail
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ColumnLayout { anchors.centerIn: parent; spacing: 2
                            RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 5
                                Rectangle { Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 3.5; color: root.toneColor(modelData.tone) }
                                Eyebrow { text: modelData.label; color: Theme.textSecondary }
                            }
                            Label { text: modelData.status; color: root.toneColor(modelData.tone); font.family: Theme.ui; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 88
            color: Theme.elevated
            border.color: Theme.separatorStrong
            radius: 2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 13
                spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Eyebrow { text: doctorSession.sessionState }
                Label { text: doctorSession.currentPhase; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: root.textPx(17); font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        Doctor.DoctorProgressBar { Layout.fillWidth: true; value: doctorSession.displayProgress; tone: doctorSession.scanRunning ? "running" : "healthy" }
                        Label { text: doctorSession.displayProgress.toFixed(doctorSession.scanRunning ? 1 : 0) + "%"; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: root.textPx(12); font.weight: Font.DemiBold }
                    }
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 52; color: Theme.separator }
                Repeater {
                    model: [
                        { label: "CHECKS", value: doctorSession.completedChecks + "/" + (doctorSession.completedChecks + doctorSession.remainingChecks), tone: "information" },
                        { label: "HEALTHY", value: doctorSession.healthyCheckCount, tone: "healthy" },
                        { label: "WARNINGS", value: doctorSession.warningCheckCount, tone: "warning" },
                        { label: "FAILED", value: doctorSession.failedCheckCount, tone: "fault" },
                        { label: "DIAGNOSES", value: doctorSession.diagnosisCards.length, tone: doctorSession.diagnosisCards.length ? "warning" : "healthy" }
                    ]
                    delegate: ColumnLayout { required property var modelData; spacing: 1
                        Eyebrow { text: modelData.label }
                        Label { text: modelData.value; color: root.toneColor(modelData.tone); font.family: Theme.mono; font.pixelSize: root.textPx(16); font.weight: Font.DemiBold }
                    }
                }
                Doctor.DoctorButton { text: doctorSession.scanRunning ? "Cancel scan" : "Run new scan"; tone: "primary"; onClicked: doctorSession.scanRunning ? doctorSession.requestCancellation() : doctorSession.requestRerun(); accessibleName: text }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 45
            color: Theme.surface
            border.color: Theme.separator
            radius: 2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 10
                Eyebrow { text: "WORKSPACE"; Layout.fillWidth: true }
                Eyebrow { text: "DENSITY" }
                Doctor.DoctorSegmentedControl {
                    values: ["Comfortable", "Compact", "Dense"]
                    currentValue: doctorSession.density
                    accessibleName: "Information density"
                    onActivated: function(value) { doctorSession.setDensity(value) }
                }
                Eyebrow { text: "ACTIVITY"; visible: doctorSession.commandCenter && root.commandCenterFits }
                Doctor.DoctorSegmentedControl {
                    visible: doctorSession.commandCenter && root.commandCenterFits
                    values: ["Off", "On"]
                    currentValue: doctorSession.liveEvidenceVisible ? "On" : "Off"
                    accessibleName: "Activity timeline"
                    onActivated: function(value) { doctorSession.setLiveEvidenceVisible(value === "On") }
                }
                Doctor.DoctorButton { text: inspector.visible ? "Inspector open" : "Evidence inspector"; compact: true; selected: inspector.visible; onClicked: inspector.visible ? inspector.close() : inspector.open() }
                Doctor.DoctorButton { text: "Copy session"; compact: true; enabled: doctorSession.exportAvailable; tooltipText: "Copy the complete structured session as Markdown"; onClicked: doctorSession.copyReportSection("Entire Session") }
                Doctor.DoctorButton { text: "Export report"; compact: true; enabled: doctorSession.exportAvailable; tooltipText: "Export structured Markdown, JSON, text, or a local diagnostic bundle. Nothing is uploaded."; onClicked: exportSurface.open() }
                Doctor.DoctorButton { text: "Reset layout"; compact: true; tooltipText: "Restore the Phase 2 workspace presentation defaults"; onClicked: doctorSession.resetWorkspaceLayout() }
            }
        }

        Rectangle {
            visible: root.usingFocusFallback
            Layout.fillWidth: true
            implicitHeight: 38
            color: "#292216"
            border.color: "#715b35"
            radius: 2
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8
                Label { text: "Command Center needs more width for its four forensic panes. Focus View is being shown without altering this session."; color: "#e1c586"; font.family: Theme.ui; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                Doctor.DoctorButton { text: "Use Focus"; compact: true; tone: "primary"; onClicked: doctorSession.setCommandCenter(false) }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: doctorSession.commandCenter && root.commandCenterFits ? commandCenterView : focusView
        }
    }

    Component {
        id: planBody
        ScrollView {
            clip: true
            background: Rectangle { color: "transparent" }
            ScrollBar.vertical: Doctor.DoctorScrollBar {}
            ListView {
                model: doctorSession.planPhases
                spacing: 1
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: doctorSession.density === "Comfortable" ? 62 : doctorSession.density === "Dense" ? 43 : 52
                    color: "transparent"
                    RowLayout { anchors.fill: parent; anchors.leftMargin: root.densityPad; anchors.rightMargin: root.densityPad; spacing: 8
                        Rectangle { Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 3.5; color: root.toneColor(modelData.tone) }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Label { text: modelData.title; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(10); font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.complete + " / " + modelData.total + " · " + modelData.status; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: root.textPx(9); Layout.fillWidth: true; elide: Text.ElideRight }
                            Doctor.DoctorProgressBar { Layout.fillWidth: true; value: modelData.status === "RUNNING" ? doctorSession.displayCurrentStepProgress : modelData.progress; tone: modelData.status === "RUNNING" ? "running" : modelData.complete === modelData.total ? "healthy" : "neutral" }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#283039" }
                }
            }
        }
    }

    Component {
        id: currentBody
        Flickable {
            objectName: "currentOperationViewport"
            contentWidth: width
            contentHeight: details.implicitHeight + root.densityPad * 2
            clip: true
            ScrollBar.vertical: Doctor.DoctorScrollBar {}
            ColumnLayout {
                id: details
                width: parent.width - root.densityPad * 2
                x: root.densityPad
                y: root.densityPad
                spacing: root.densityGap
                property var operation: doctorSession.currentOperationDetails
                readonly property bool activeScan: doctorSession.scanRunning
                readonly property string sessionHealth: doctorSession.failedCheckCount ? "Degraded" : doctorSession.warningCheckCount ? "Attention required" : "Healthy"
                readonly property string sessionTone: doctorSession.failedCheckCount ? "fault" : doctorSession.warningCheckCount ? "warning" : "healthy"

                ColumnLayout {
                    visible: details.activeScan
                    Layout.fillWidth: true
                    spacing: root.cardGap
                    Eyebrow { text: "CURRENT OPERATION" }
                    Label { text: details.operation.title; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: root.textPx(16); font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: details.operation.checkId; color: Theme.information; font.family: Theme.mono; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                    RowLayout { Layout.fillWidth: true
                        Doctor.DoctorStatusPill { text: details.operation.status; tone: "running" }
                        Item { Layout.fillWidth: true }
                        Label { text: doctorSession.displayCurrentStepProgress.toFixed(1) + "%"; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: root.textPx(12) }
                    }
                    Doctor.DoctorProgressBar { Layout.fillWidth: true; value: doctorSession.displayCurrentStepProgress; tone: "running" }
                    Doctor.DoctorDivider {}
                    GridLayout { columns: 2; columnSpacing: 12; rowSpacing: 8; Layout.fillWidth: true
                        Eyebrow { text: "ELAPSED" }
                        Label { text: doctorSession.presentationElapsed; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: root.textPx(11) }
                        Eyebrow { text: "TIMEOUT" }
                        Label { text: details.operation.timeout; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: 11 }
                        Eyebrow { text: "SESSION HEALTH" }
                        Label { text: details.sessionHealth; color: Theme.tone(details.sessionTone); font.family: Theme.ui; font.pixelSize: 11 }
                    }
                    Label { text: details.operation.detail; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }

                Rectangle {
                    objectName: "completedOperationSummary"
                    visible: !details.activeScan
                    Layout.fillWidth: true
                    implicitHeight: completedSummary.implicitHeight + root.cardPad * 2
                    color: Theme.inset
                    border.color: Theme.separatorStrong
                    border.width: 1
                    radius: 2
                    ColumnLayout {
                        id: completedSummary
                        anchors.fill: parent
                        anchors.margins: root.cardPad
                        spacing: root.cardGap
                        RowLayout { Layout.fillWidth: true
                            Eyebrow { text: "READ-ONLY ANALYSIS COMPLETE"; color: Theme.information; Layout.fillWidth: true }
                            Doctor.DoctorStatusPill { text: details.sessionHealth.toUpperCase(); tone: details.sessionTone }
                        }
                        Label { text: "All applicable read-only checks completed."; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 16; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Doctor.DoctorDivider {}
                        GridLayout {
                            columns: 2
                            columnSpacing: root.cardPad
                            rowSpacing: root.cardGap
                            Layout.fillWidth: true
                            Repeater {
                                model: [
                                    { label: "ACTIVE CHECK", value: "None", tone: "neutral" },
                                    { label: "STATUS", value: details.sessionHealth, tone: details.sessionTone },
                                    { label: "ELAPSED", value: details.operation.elapsed, tone: "neutral" },
                                    { label: "CHECKS", value: doctorSession.completedChecks + " / " + (doctorSession.completedChecks + doctorSession.remainingChecks), tone: "information" }
                                ]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Eyebrow { text: modelData.label }
                                    Label { text: modelData.value; color: Theme.tone(modelData.tone); font.family: modelData.label === "ELAPSED" || modelData.label === "CHECKS" ? Theme.mono : Theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                            }
                        }
                        Label { text: details.operation.detail; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    Component {
        id: findingsBody
        ScrollView {
            objectName: "findingsViewport"
            clip: true
            background: Rectangle { color: "transparent" }
            ScrollBar.vertical: Doctor.DoctorScrollBar {}
            ColumnLayout {
                width: parent.availableWidth
                spacing: root.cardGap
                Repeater {
                    model: doctorSession.diagnosisCards
                    delegate: Doctor.DoctorButton {
                        objectName: "diagnosisCard_" + modelData.id
                        required property var modelData
                        Layout.fillWidth: true
                        leftPadding: root.cardPad
                        rightPadding: root.cardPad
                        topPadding: root.cardPad
                        bottomPadding: root.cardPad
                        implicitHeight: diagnosisColumn.implicitHeight + root.cardPad * 2
                        text: ""
                        accessibleName: modelData.role + " " + modelData.title
                        onClicked: root.openEvidence(modelData.evidenceId)
                        background: Rectangle { radius: 2; color: modelData.role.indexOf("PRIMARY") >= 0 ? "#20272c" : Theme.elevated; border.width: 1; border.color: root.toneColor(modelData.tone); Rectangle { width: 4; height: parent.height; color: root.toneColor(modelData.tone) } }
                        contentItem: ColumnLayout { id: diagnosisColumn; spacing: root.cardGap
                            RowLayout { Layout.fillWidth: true
                                Eyebrow { text: modelData.role; color: root.toneColor(modelData.tone); Layout.fillWidth: true; elide: Text.ElideRight }
                                Doctor.DoctorStatusPill { text: modelData.confidence.toUpperCase() + " · " + modelData.score + "%"; tone: modelData.tone }
                            }
                            Label { text: modelData.title; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: modelData.role.indexOf("PRIMARY") >= 0 ? 17 : 14; font.weight: Font.DemiBold; lineHeight: 1.16; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: modelData.summary; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; lineHeight: 1.22; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Rectangle { visible: modelData.impact.length > 0; Layout.fillWidth: true; implicitHeight: impactColumn.implicitHeight + root.cardGap * 2; color: Theme.inset; border.color: Theme.separator; border.width: 1; radius: 1
                                ColumnLayout { id: impactColumn; anchors.fill: parent; anchors.margins: root.cardGap; spacing: 3
                                    Eyebrow { text: "IMPACT" }
                                    Label { text: modelData.impact; color: Theme.textMuted; font.family: Theme.ui; font.pixelSize: 10; lineHeight: 1.2; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                }
                Repeater {
                    model: doctorSession.findingCards
                    delegate: Doctor.DoctorButton {
                        objectName: "findingCard_" + modelData.id
                        required property var modelData
                        Layout.fillWidth: true
                        leftPadding: root.cardPad
                        rightPadding: root.cardPad
                        topPadding: root.cardPad
                        bottomPadding: root.cardPad
                        implicitHeight: findingColumn.implicitHeight + root.cardPad * 2
                        text: ""
                        accessibleName: modelData.severity + " " + modelData.title
                        onClicked: root.openEvidence(modelData.evidenceId)
                        background: Rectangle { radius: 2; color: Theme.surface; border.width: 1; border.color: modelData.tone === "fault" ? Theme.critical : modelData.tone === "warning" ? Theme.warning : Theme.separator }
                        contentItem: ColumnLayout { id: findingColumn; spacing: root.cardGap
                            RowLayout { Layout.fillWidth: true
                                Eyebrow { text: modelData.severity; color: root.toneColor(modelData.tone); Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: modelData.confidence; color: root.toneColor(modelData.tone); font.family: Theme.ui; font.pixelSize: 10 }
                            }
                            Label { text: modelData.title; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; lineHeight: 1.15; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: modelData.summary; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; lineHeight: 1.2; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
                Rectangle {
                    visible: doctorSession.diagnosisCards.length === 0 && doctorSession.findingCards.length === 0
                    Layout.fillWidth: true
                    implicitHeight: 98
                    color: "#17231d"
                    border.color: Theme.healthy
                    radius: 2
                    ColumnLayout { anchors.centerIn: parent; spacing: 5
                        Label { text: "HIDHIDE HEALTHY"; color: Theme.healthy; font.family: Theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                        Label { text: "No diagnosis or user action required."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    }
                }
            }
        }
    }

    Component {
        id: actionBody
        Flickable {
            contentWidth: width
            contentHeight: actionColumn.implicitHeight + root.densityPad * 2
            clip: true
            ScrollBar.vertical: Doctor.DoctorScrollBar {}
            ColumnLayout {
                id: actionColumn
                width: parent.width - root.densityPad * 2
                x: root.densityPad
                y: root.densityPad
                spacing: root.densityGap
                Doctor.DoctorStatusPill { text: doctorSession.labRepairMode ? doctorSession.repairRuntimeState : (doctorSession.repairPlanAvailable ? (doctorSession.repairPlanSummary.deep ? "DEEP REPAIR AVAILABLE" : "REPAIR REVIEW") : doctorSession.diagnosisCards.length ? "DIAGNOSIS COMPLETE" : "NOTHING REQUIRED"); tone: doctorSession.repairPlanAvailable || doctorSession.diagnosisCards.length ? "warning" : "healthy" }
                Label { text: doctorSession.userActionTitle; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: doctorSession.userActionDetail; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Rectangle {
                    visible: doctorSession.recoveryNotice.length > 0
                    Layout.fillWidth: true
                    implicitHeight: recoveryNoticeText.implicitHeight + 18
                    color: "#33231d"; border.color: Theme.warning; border.width: 1; radius: 2
                    Label { id: recoveryNoticeText; anchors.fill: parent; anchors.margins: 9; text: "REPAIR RECOVERY REVIEW REQUIRED\n" + doctorSession.recoveryNotice; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
                Rectangle {
                    visible: doctorSession.repairPlanAvailable
                    Layout.fillWidth: true
                    implicitHeight: repairPlanColumn.implicitHeight + 18
                    color: "#17222a"
                    border.color: Theme.warning
                    border.width: 1
                    radius: 2
                    ColumnLayout {
                        id: repairPlanColumn
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 5
                        property var plan: doctorSession.repairPlanSummary
                        RowLayout { Layout.fillWidth: true
                            Eyebrow { text: doctorSession.labRepairMode ? "LAB REPAIR MODE · DEVELOPMENT FIXTURE" : (repairPlanColumn.plan.deep ? "DEEP REPAIR AVAILABLE · READ ONLY" : "REPAIR PLAN · READ ONLY"); color: Theme.warning; Layout.fillWidth: true }
                            Doctor.DoctorStatusPill { text: repairPlanColumn.plan.qualification || "NOT QUALIFIED"; tone: "warning" }
                        }
                        Label { text: repairPlanColumn.plan.title || ""; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: repairPlanColumn.plan.description || ""; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Repeater { model: doctorSession.repairPlanOperations
                            delegate: Rectangle { required property string modelData; Layout.fillWidth: true; implicitHeight: operationText.implicitHeight + 10; color: Theme.inset; border.color: Theme.separator; border.width: 1; radius: 1
                                Label { id: operationText; anchors.fill: parent; anchors.margins: 5; text: modelData; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                            }
                        }
                        Eyebrow { text: "EXPLICITLY UNCHANGED" }
                        Repeater { model: doctorSession.repairPlanCollateral
                            delegate: Label { required property string modelData; text: "• " + modelData; color: Theme.textMuted; font.family: Theme.ui; font.pixelSize: 9; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                        }
                        Label { text: "BACKUP  " + repairPlanColumn.plan.backup + "\nROLLBACK  " + repairPlanColumn.plan.rollback + "\nADMINISTRATOR ACCESS  " + repairPlanColumn.plan.elevation + "\nRESTART  " + repairPlanColumn.plan.restart + "\nCONTINUATION  " + repairPlanColumn.plan.continuation + "\nUSER ACTION  " + repairPlanColumn.plan.userAction; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 9; lineHeight: 1.18; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Rectangle {
                            visible: typeof repairPlanColumn.plan.package === "string"
                                && repairPlanColumn.plan.package.length > 0
                                && repairPlanColumn.plan.package !== "Not applicable"
                            Layout.fillWidth: true
                            implicitHeight: packagePlanText.implicitHeight + 12
                            color: Theme.inset
                            border.color: Theme.separator
                            border.width: 1
                            radius: 1
                            Label { id: packagePlanText; anchors.fill: parent; anchors.margins: 6; text: "APPROVED PACKAGE\n" + repairPlanColumn.plan.package; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WrapAnywhere }
                        }
                        Rectangle {
                            visible: doctorSession.labRepairMode
                            Layout.fillWidth: true
                            implicitHeight: labDetail.implicitHeight + labButtons.implicitHeight + 20
                            color: "#202a24"; border.color: Theme.warning; border.width: 1; radius: 2
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 6
                                Label { id: labDetail; text: doctorSession.repairRuntimeState + "\n" + doctorSession.repairRuntimeDetail; color: Theme.textPrimary; font.family: Theme.ui; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                RowLayout {
                                    id: labButtons; Layout.fillWidth: true; spacing: 6
                                    Doctor.DoctorButton { text: "TEST HELPER — NO CHANGES"; compact: true; enabled: !doctorSession.repairOperationInFlight; tooltipText: "Uses the sealed plan and elevated helper path, but sends no HidHide SET operation."; onClicked: doctorSession.requestHelperConnectivityTest() }
                                    Doctor.DoctorButton { text: "AUTHORIZE LAB REPAIR"; tone: "primary"; compact: true; enabled: !doctorSession.repairOperationInFlight; tooltipText: "Final owner/lab authorization for this exact fixture plan. UAC cancellation makes no changes."; onClicked: doctorSession.requestLabRepairAuthorization() }
                                }
                            }
                        }
                    }
                }
                Doctor.DoctorDivider {}
                Eyebrow { text: "REPAIR SAFETY BOUNDARY" }
                Rectangle { Layout.fillWidth: true; implicitHeight: safetyText.implicitHeight + 20; color: "#17222a"; border.color: Theme.readOnly; radius: 2
                    Label { id: safetyText; anchors.fill: parent; anchors.margins: 10; text: doctorSession.labRepairMode ? "LAB REPAIR MODE — DEVELOPMENT FIXTURE ONLY\nA deliberate final authorization binds the exact plan, transaction, recipe version, targets, fingerprints, and digest. The helper revalidates every target immediately before use; UAC cancellation makes no changes." : "NORMAL MODE IS READ ONLY\nPlans are safe to inspect. LabQualified R1-R5 recipes require a distinct, explicit owner/lab authorization path; this review surface never starts UAC, an installer, a restart, or a HidHide mutation."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap }
                }
                Item { Layout.fillHeight: true }
                Label { text: "SESSION  " + doctorSession.sessionId.slice(-8); color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: 10 }
            }
        }
    }

    Component {
        id: timelinePanel
        Rectangle {
            color: Theme.surface
            border.color: Theme.separatorStrong
            border.width: 1
            radius: 2
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 5
                RowLayout { Layout.fillWidth: true
                    Eyebrow { text: "ACTIVITY TIMELINE"; color: Theme.textSecondary; Layout.fillWidth: true }
                    Label { text: doctorSession.activityRows.length + " EVENTS"; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: 10 }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: doctorSession.activityRows
                    spacing: 1
                    ScrollBar.vertical: Doctor.DoctorScrollBar {}
                    delegate: Doctor.DoctorTimelineRow {
                        required property var modelData
                        width: ListView.view.width
                        time: modelData.time
                        checkId: modelData.checkId
                        eventType: modelData.eventType
                        title: modelData.title
                        symbol: modelData.symbol
                        tone: modelData.tone
                        evidenceId: modelData.evidenceId
                        selected: doctorSession.selectedActivity.activityIndex === modelData.activityIndex
                        onClicked: doctorSession.selectActivity(modelData.activityIndex)
                    }
                }
            }
        }
    }

    Component {
        id: commandCenterView
        Item {
            objectName: "commandWorkspace"
            SplitView {
                id: verticalSplit
                objectName: "commandVerticalSplit"
                anchors.fill: parent
                orientation: Qt.Vertical
                handle: Rectangle { objectName: "commandVerticalSplitHandle"; implicitHeight: 7; color: Theme.separator; Rectangle { anchors.centerIn: parent; width: 32; height: 2; color: Theme.textMuted; radius: 1 } }
                SplitView {
                    id: workspaceSplit
                    objectName: "commandPaneSplit"
                    orientation: Qt.Horizontal
                    SplitView.fillHeight: true
                    property bool layoutRestored: false
                    property int appliedLayoutResetEpoch: -1
                    function restorePaneFractions() {
                        if (width <= 0 || !planPane || !currentPane || !findingsPane || !actionPane) return
                        planPane.SplitView.preferredWidth = Number(doctorSession.paneFractions[0]) * width
                        currentPane.SplitView.preferredWidth = Number(doctorSession.paneFractions[1]) * width
                        findingsPane.SplitView.preferredWidth = Number(doctorSession.paneFractions[2]) * width
                        actionPane.SplitView.preferredWidth = Number(doctorSession.paneFractions[3]) * width
                        layoutRestored = true
                        appliedLayoutResetEpoch = doctorSession.layoutResetEpoch
                    }
                    function persistReleasedLayout() {
                        if (!layoutRestored || doctorSession.maximizedPane.length !== 0 || !itemAt(3)) return
                        const totalPaneWidth = itemAt(0).width + itemAt(1).width + itemAt(2).width + itemAt(3).width
                        if (totalPaneWidth > 0)
                            doctorSession.savePaneFractions([itemAt(0).width / totalPaneWidth, itemAt(1).width / totalPaneWidth, itemAt(2).width / totalPaneWidth, itemAt(3).width / totalPaneWidth])
                    }
                    onWidthChanged: if (!layoutRestored) layoutRestoreTimer.restart()
                    onResizingChanged: if (!resizing) persistReleasedLayout()
                    handle: Rectangle { objectName: "commandPaneSplitHandle"; implicitWidth: 7; color: Theme.separator; Rectangle { anchors.centerIn: parent; width: 2; height: 28; color: Theme.textMuted; radius: 1 } }
                    PaneSurface {
                        id: planPane
                        objectName: "commandPlanPane"
                        visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "plan"
                        paneId: "plan"; heading: "DIAGNOSTIC PLAN"; countText: doctorSession.planPhases.length + " phases"; bodyContent: planBody
                        SplitView.minimumWidth: 300
                        reportScope: "Diagnostic Plan"
                    }
                    PaneSurface {
                        id: currentPane
                        objectName: "commandCurrentPane"
                        visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "current"
                        paneId: "current"; heading: "CURRENT STEP"; bodyContent: currentBody
                        SplitView.minimumWidth: 280
                        reportScope: "Current / Historical Steps"
                    }
                    PaneSurface {
                        id: findingsPane
                        objectName: "commandFindingsPane"
                        visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "findings"
                        paneId: "findings"; heading: "FINDINGS & DIAGNOSES"; countText: (doctorSession.diagnosisCards.length + doctorSession.findingCards.length) + " items"; bodyContent: findingsBody
                        SplitView.minimumWidth: 380
                        reportScope: "Findings & Diagnoses"
                    }
                    PaneSurface {
                        id: actionPane
                        objectName: "commandActionPane"
                        visible: doctorSession.maximizedPane === "" || doctorSession.maximizedPane === "action"
                        paneId: "action"; heading: "USER ACTION"; bodyContent: actionBody
                        SplitView.minimumWidth: 260
                        reportScope: "User Actions"
                    }
                }
                Item {
                    id: commandBottomDock
                    objectName: "commandBottomDock"
                    visible: doctorSession.liveEvidenceVisible || inspector.visible
                    SplitView.minimumHeight: 128
                    SplitView.preferredHeight: 250
                    SplitView.maximumHeight: 430
                    property bool bothOpen: doctorSession.liveEvidenceVisible && inspector.visible
                    Loader { objectName: "commandActivityTimeline"; anchors.fill: parent; visible: doctorSession.liveEvidenceVisible && !commandBottomDock.bothOpen; sourceComponent: timelinePanel }
                    Loader { objectName: "commandEvidenceInspector"; anchors.fill: parent; visible: inspector.visible && !commandBottomDock.bothOpen; sourceComponent: inspectorPanel }
                    SplitView {
                        id: commandBottomDualDock
                        anchors.fill: parent
                        orientation: Qt.Horizontal
                        visible: commandBottomDock.bothOpen && parent.width >= 980
                        property bool layoutRestored: false
                        function restoreDockFractions() {
                            if (!visible || width <= 0) return
                            activityDual.SplitView.preferredWidth = Number(doctorSession.bottomDockFractions[0]) * width
                            inspectorDual.SplitView.preferredWidth = Number(doctorSession.bottomDockFractions[1]) * width
                            layoutRestored = true
                        }
                        function persistReleasedDock() {
                            if (!layoutRestored) return
                            const totalWidth = activityDual.width + inspectorDual.width
                            if (totalWidth > 0)
                                doctorSession.saveBottomDockFractions([activityDual.width / totalWidth, inspectorDual.width / totalWidth])
                        }
                        onWidthChanged: if (!layoutRestored) dockRestoreTimer.restart()
                        onVisibleChanged: if (visible && !layoutRestored) dockRestoreTimer.restart()
                        onResizingChanged: if (!resizing) persistReleasedDock()
                        handle: Rectangle { objectName: "commandBottomDockHandle"; implicitWidth: 7; color: Theme.separator; Rectangle { anchors.centerIn: parent; width: 2; height: 28; color: Theme.textMuted; radius: 1 } }
                        Loader { id: activityDual; objectName: "commandActivityTimelineDual"; SplitView.minimumWidth: 220; sourceComponent: timelinePanel }
                        Loader { id: inspectorDual; objectName: "commandEvidenceInspectorDual"; SplitView.minimumWidth: 300; sourceComponent: inspectorPanel }
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 7
                        visible: commandBottomDock.bothOpen && parent.width < 980
                        Loader { objectName: "commandActivityTimelineStacked"; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: timelinePanel }
                        Loader { objectName: "commandEvidenceInspectorStacked"; Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: inspectorPanel }
                    }
                }
            }
            Timer {
                id: layoutRestoreTimer
                interval: 0
                repeat: false
                onTriggered: workspaceSplit.restorePaneFractions()
            }
            Timer {
                id: dockRestoreTimer
                interval: 0
                repeat: false
                onTriggered: commandBottomDualDock.restoreDockFractions()
            }
            Connections {
                target: doctorSession
                function onPresentationChanged() {
                    if (workspaceSplit.appliedLayoutResetEpoch !== doctorSession.layoutResetEpoch) {
                        layoutRestoreTimer.restart()
                        commandBottomDualDock.layoutRestored = false
                        dockRestoreTimer.restart()
                    }
                }
            }
            Shortcut { sequence: "Escape"; onActivated: doctorSession.setMaximizedPane("") }
        }
    }

    Component {
        id: focusView
        Flickable {
            objectName: "focusWorkspace"
            contentWidth: width
            contentHeight: focusColumn.implicitHeight
            clip: true
            ScrollBar.vertical: Doctor.DoctorScrollBar {}
            ColumnLayout {
                id: focusColumn
                objectName: "focusNarrativeColumn"
                width: Math.min(parent.width, Math.max(560, parent.width * 0.62))
                x: Math.max(0, (parent.width - width) / 2)
                spacing: root.densityGap
                Rectangle { Layout.fillWidth: true; implicitHeight: 47; color: Theme.inset; border.color: Theme.separator; radius: 2
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 13
                        ColumnLayout { spacing: 1; Layout.fillWidth: true
                            Eyebrow { text: "DIAGNOSTIC SESSION" }
                            Label { text: "Readable diagnostic narrative"; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 12 }
                        }
                        Doctor.DoctorStatusPill { text: doctorSession.sessionState; tone: doctorSession.failedCheckCount ? "fault" : doctorSession.warningCheckCount ? "warning" : "healthy" }
                    }
                }
                PaneSurface { objectName: "focusCurrentPane"; Layout.fillWidth: true; implicitHeight: doctorSession.scanRunning ? 280 : 308; paneId: "current"; heading: "CURRENT STEP / OPERATION"; bodyContent: currentBody; maximizable: false }
                PaneSurface { objectName: "focusFindingsPane"; Layout.fillWidth: true; implicitHeight: 520; paneId: "findings"; heading: "PRIMARY DIAGNOSIS & CONTRIBUTING FINDINGS"; bodyContent: findingsBody; maximizable: false }
                PaneSurface { Layout.fillWidth: true; implicitHeight: 340; paneId: "plan"; heading: "DIAGNOSTIC PLAN"; bodyContent: planBody; maximizable: false }
                PaneSurface { Layout.fillWidth: true; implicitHeight: 250; paneId: "action"; heading: "USER ACTION"; bodyContent: actionBody; maximizable: false }
                Loader {
                    objectName: "focusEvidenceInspector"
                    visible: inspector.visible
                    Layout.fillWidth: true
                    Layout.preferredHeight: 380
                    sourceComponent: inspectorPanel
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 66; color: Theme.inset; border.color: Theme.separator; radius: 2
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 10
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Eyebrow { text: "TECHNICAL / EVIDENCE INSPECTOR" }
                            Label { text: "Select a diagnosis or finding to inspect linked evidence."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11 }
                        }
                        Doctor.DoctorButton { text: "Open inspector"; tone: "primary"; onClicked: inspector.open() }
                    }
                }
            }
        }
    }

    Component {
        id: inspectorPanel
        Rectangle {
        id: inspectorSurface
        color: Theme.surface
        border.color: Theme.separatorStrong
        border.width: 1
        radius: 2
        property var selected: doctorSession.selectedEvidence
        property var activity: doctorSession.selectedActivity
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            RowLayout { Layout.fillWidth: true
                ColumnLayout { Layout.fillWidth: true; spacing: 1
                    Eyebrow { text: "EVIDENCE INSPECTOR"; color: Theme.textSecondary }
                    Label { text: inspectorSurface.selected.checkId || "Select a finding, diagnosis, or timeline row"; color: Theme.information; font.family: Theme.mono; font.pixelSize: root.textPx(12); elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Doctor.DoctorButton { text: "↶"; compact: true; iconOnly: true; enabled: inspectorSurface.selected.hasPrevious; tooltipText: "Open previous evidence record"; accessibleName: tooltipText; onClicked: doctorSession.selectAdjacentEvidence(-1) }
                Doctor.DoctorButton { text: "↷"; compact: true; iconOnly: true; enabled: inspectorSurface.selected.hasNext; tooltipText: "Open next evidence record"; accessibleName: tooltipText; onClicked: doctorSession.selectAdjacentEvidence(1) }
                Doctor.DoctorButton { text: "COPY SUMMARY"; compact: true; tooltipText: "Copy the redacted evidence summary"; accessibleName: tooltipText; onClicked: doctorSession.copySelectedEvidenceMode("Summary") }
                Doctor.DoctorButton { text: "COPY TECH"; compact: true; tooltipText: "Copy redacted technical evidence fields"; accessibleName: tooltipText; onClicked: doctorSession.copySelectedEvidenceMode("Technical") }
                Doctor.DoctorButton { text: "COPY ALL"; compact: true; tooltipText: "Copy complete redacted evidence"; accessibleName: tooltipText; onClicked: doctorSession.copySelectedEvidenceMode("Complete") }
                Doctor.DoctorButton { text: "JSON"; compact: true; tooltipText: "Copy complete redacted evidence as JSON"; accessibleName: tooltipText; onClicked: doctorSession.copySelectedEvidenceMode("JSON") }
                Doctor.DoctorButton { text: "Close"; compact: true; onClicked: inspector.close() }
            }
            Doctor.DoctorDivider {}
            Rectangle {
                visible: inspectorSurface.activity.eventType && inspectorSurface.activity.eventType.length > 0
                Layout.fillWidth: true
                implicitHeight: activityContext.implicitHeight + 16
                color: Theme.inset
                border.color: Theme.separator
                radius: 2
                ColumnLayout {
                    id: activityContext
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    Eyebrow { text: "ACTIVITY CONTEXT" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 9
                        Label { text: inspectorSurface.activity.eventType; color: Theme.information; font.family: Theme.mono; font.pixelSize: root.textPx(10) }
                        Label { text: inspectorSurface.activity.phase; color: Theme.textMuted; font.family: Theme.ui; font.pixelSize: root.textPx(10) }
                        Label { text: inspectorSurface.activity.status; color: Theme.tone(inspectorSurface.activity.status.toLowerCase()); font.family: Theme.mono; font.pixelSize: root.textPx(10) }
                        Item { Layout.fillWidth: true }
                        Label { text: inspectorSurface.activity.timestamp; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: root.textPx(9) }
                    }
                    Label { text: inspectorSurface.activity.detail || inspectorSurface.activity.title; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            GridLayout { columns: root.width > 980 ? 4 : 2; columnSpacing: 18; rowSpacing: 8; Layout.fillWidth: true
                Eyebrow { text: "SOURCE" }
                Label { text: inspectorSurface.selected.source || "Unknown"; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(11); Layout.fillWidth: true; elide: Text.ElideRight }
                Eyebrow { text: "TIMESTAMP" }
                Label { text: inspectorSurface.selected.timestamp || "Unknown"; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: root.textPx(10) }
                Eyebrow { text: "DURATION" }
                Label { text: inspectorSurface.selected.duration || "Unknown"; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: root.textPx(10) }
                Eyebrow { text: "PROVENANCE" }
                Label { text: inspectorSurface.selected.provenance || "Unknown"; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(11) }
                Eyebrow { text: "NATIVE ERROR" }
                Label { text: inspectorSurface.selected.error || "None"; color: inspectorSurface.selected.error && inspectorSurface.selected.error !== "None" ? Theme.critical : Theme.textSecondary; font.family: Theme.mono; font.pixelSize: root.textPx(10); Layout.fillWidth: true; elide: Text.ElideRight }
            }
            RowLayout { Layout.fillWidth: true
                Label { text: (inspectorSurface.selected.evidenceIndex || 0) + " / " + (inspectorSurface.selected.evidenceCount || 0) + " EVIDENCE"; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: root.textPx(10) }
                Item { Layout.fillWidth: true }
                Label { text: inspectorSurface.selected.collectionTruncated ? "EVIDENCE COLLECTION TRUNCATED" : "STRUCTURED FORENSIC RECORD"; color: inspectorSurface.selected.collectionTruncated ? Theme.warning : Theme.information; font.family: Theme.ui; font.pixelSize: root.textPx(10) }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: inspectorSurface.selected.groups || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: fieldColumn.implicitHeight + 18
                            color: Theme.inset
                            border.color: Theme.separator
                            radius: 2
                            ColumnLayout {
                                id: fieldColumn
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 5
                                Eyebrow { text: modelData.name }
                                Repeater {
                                    model: modelData.fields
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 12
                                        Label { text: modelData.label; color: Theme.textMuted; font.family: Theme.ui; font.pixelSize: root.textPx(10); font.weight: Font.DemiBold; Layout.preferredWidth: Math.min(260, parent.width * 0.34); Layout.alignment: Qt.AlignTop; wrapMode: Text.WordWrap }
                                        Label { text: modelData.value; color: Theme.textSecondary; font.family: modelData.monospace ? Theme.mono : Theme.ui; font.pixelSize: root.textPx(10); Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: inspectorSurface.selected.attempts && inspectorSurface.selected.attempts.length > 0
                        Layout.fillWidth: true
                        implicitHeight: attemptColumn.implicitHeight + 18
                        color: Theme.inset
                        border.color: Theme.separator
                        radius: 2
                        ColumnLayout {
                            id: attemptColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 5
                            Eyebrow { text: "PROBE ATTEMPTS" }
                            Repeater {
                                model: inspectorSurface.selected.attempts || []
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label { text: "#" + modelData.ordinal + "  " + modelData.operation + " — " + modelData.outcome; color: Theme.textPrimary; font.family: Theme.mono; font.pixelSize: root.textPx(10); wrapMode: Text.WrapAnywhere }
                                    Label { text: modelData.target + " · " + modelData.bytes + " · " + modelData.duration + " / " + modelData.timeout; color: Theme.textSecondary; font.family: Theme.mono; font.pixelSize: root.textPx(9); wrapMode: Text.WrapAnywhere }
                                    Label { visible: modelData.error !== "None"; text: modelData.error; color: Theme.critical; font.family: Theme.ui; font.pixelSize: root.textPx(10); wrapMode: Text.WrapAnywhere }
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: inspectorSurface.selected.relatedEvidenceIds && inspectorSurface.selected.relatedEvidenceIds.length > 0
                        Layout.fillWidth: true
                        Eyebrow { text: "OPEN RELATED EVIDENCE" }
                        Repeater {
                            model: inspectorSurface.selected.relatedEvidenceIds || []
                            delegate: Doctor.DoctorButton { required property var modelData; text: modelData; compact: true; onClicked: root.openEvidence(modelData) }
                        }
                    }
                    Label { visible: !inspectorSurface.selected.id; text: "Select a finding, diagnosis, timeline entry, or evidence record to inspect its captured structured facts."; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: root.textPx(11); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
    }
    }
}
