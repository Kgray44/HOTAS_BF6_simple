import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// First-use presentation choice only. It deliberately has no AppBackend
// reference: choosing an explanation density cannot create a rig, profile,
// output, mapping request, or repair task.
FlightDeckDialog {
    id: root
    objectName: "flightDeckGuidanceOnboarding"
    tokens: deck
    heading: "Choose your setup guidance"
    preferredWidth: 620
    closePolicy: Popup.NoAutoClose
    property bool offerAllowed: true

    FlightDeckTheme { id: deck }

    function choose(level) {
        if (!themeManager.chooseGuidanceLevel(level)) {
            saveError = "Could not save that choice. The current view has not changed."
            return
        }
        saveError = ""
        close()
    }

    property string saveError: ""

    contentItem: ColumnLayout {
        width: root.availableWidth
        spacing: deck.space16

        Text {
            text: "Both choices keep every existing HOTAS BF6 capability. Guidance changes only the starting explanations and which detail panels open first."
            color: deck.textSecondary
            font.pixelSize: deck.body
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space12

            Repeater {
                model: [{ level: "Guided", summary: "Use the six everyday setup pages: overview, devices, axes, buttons, profiles, and settings. Advanced editors stay in Full." },
                        { level: "Full", summary: "Use the complete application, including every editor, technical control, and diagnostic view." }]
                delegate: Button {
                    id: choiceButton
                    required property var modelData
                    objectName: "flightDeckGuidanceOnboarding" + modelData.level
                    Layout.fillWidth: true
                    Layout.preferredHeight: deck.scale(156)
                    focusPolicy: Qt.StrongFocus
                    onClicked: root.choose(modelData.level)
                    contentItem: ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        Text {
                            text: modelData.level.toUpperCase()
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: deck.bodyStrong
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.summary
                            color: deck.textSecondary
                            font.pixelSize: deck.bodySmall
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                        Text {
                            text: "USE " + modelData.level.toUpperCase()
                            color: deck.accent
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.caption
                            font.bold: true
                        }
                    }
                    background: Rectangle {
                        radius: deck.radiusCard
                        color: choiceButton.down ? deck.accentMuted : choiceButton.hovered ? deck.selected : deck.primarySurface
                        border.width: choiceButton.activeFocus ? 2 : 1
                        border.color: choiceButton.activeFocus ? deck.focus : deck.border
                    }
                }
            }
        }
        Text {
            visible: root.saveError.length > 0
            text: root.saveError
            color: deck.fault
            font.pixelSize: deck.bodySmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    footer: FlightDeckDialogFooter {
        tokens: deck
        RowLayout {
            anchors.fill: parent
            Item { Layout.fillWidth: true }
            Button {
                id: skipButton
                objectName: "flightDeckGuidanceOnboardingSkip"
                text: "SKIP FOR NOW"
                focusPolicy: Qt.StrongFocus
                onClicked: root.choose("Guided")
                contentItem: Text {
                    text: skipButton.text
                    color: deck.textSecondary
                    font.family: deck.telemetryFont
                    font.pixelSize: deck.caption
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: deck.radiusControl
                    color: skipButton.down ? deck.secondarySurface : skipButton.hovered ? deck.primarySurface : "transparent"
                    border.width: skipButton.activeFocus ? 2 : 1
                    border.color: skipButton.activeFocus ? deck.focus : deck.border
                }
            }
        }
    }

    Component.onCompleted: {
        if (offerAllowed && themeManager.guidanceOnboardingPending)
            Qt.callLater(function() { root.open() })
    }
    onOfferAllowedChanged: {
        if (offerAllowed && themeManager.guidanceOnboardingPending)
            root.open()
    }
    Connections {
        target: themeManager
        function onGuidanceOnboardingChanged() {
            if (root.offerAllowed && themeManager.guidanceOnboardingPending)
                root.open()
            else
                root.close()
        }
    }
}
