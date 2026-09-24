import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Shared preparation/status language for the two input editors.  The pages
// derive their facts from the canonical editor context; this component only
// presents that result and relays the one useful next action.
FlightDeckCard {
    id: card

    property string heading: ""
    property string detail: ""
    property var statusPills: []
    property string primaryText: ""
    property string secondaryText: ""
    property bool primaryEnabled: true
    property bool secondaryEnabled: true
    property string primaryObjectName: ""
    property string secondaryObjectName: ""
    signal primaryAction()
    signal secondaryAction()

    contentPadding: tokens.cardPaddingCompact
    implicitHeight: inputStatusContent.implicitHeight + contentPadding * 2

    component StatusButton: Button {
        id: button
        property bool subdued: false
        implicitHeight: card.tokens.compactControlHeight
        leftPadding: card.tokens.space12
        rightPadding: card.tokens.space12
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: button.text
            color: !button.enabled ? card.tokens.textMuted
                : button.subdued ? card.tokens.textSecondary
                : (card.tokens.light ? "white" : card.tokens.primarySurface)
            font.family: card.tokens.telemetryFont
            font.pixelSize: card.tokens.scale(9)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: card.tokens.radiusControl
            color: !button.enabled ? card.tokens.disabled
                : button.subdued ? (button.down ? card.tokens.accentMuted
                    : button.hovered ? card.tokens.selected : card.tokens.secondarySurface)
                : (button.down ? card.tokens.accentMuted : card.tokens.accent)
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? card.tokens.focus
                : button.subdued ? card.tokens.border : card.tokens.accent
        }
    }

    ColumnLayout {
        id: inputStatusContent
        anchors.fill: parent
        anchors.margins: parent.contentPadding
        spacing: tokens.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: tokens.space8

            Text {
                objectName: card.objectName + "Heading"
                text: card.heading
                color: tokens.textPrimary
                font.family: tokens.displayFont
                font.pixelSize: tokens.scale(15)
                font.bold: true
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }

            Flow {
                objectName: card.objectName + "Pills"
                Layout.maximumWidth: Math.max(tokens.scale(150), parent.width * 0.52)
                spacing: tokens.space6
                Repeater {
                    model: card.statusPills
                    delegate: FlightDeckStatusChip {
                        required property var modelData
                        tokens: card.tokens
                        label: String(modelData.label || "STATUS")
                        value: String(modelData.value || "")
                        tone: String(modelData.tone || "informational")
                    }
                }
            }
        }

        Text {
            objectName: card.objectName + "Detail"
            visible: text.length > 0
            text: card.detail
            color: tokens.textSecondary
            font.pixelSize: tokens.scale(10)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        RowLayout {
            visible: card.primaryText.length > 0 || card.secondaryText.length > 0
            Layout.fillWidth: true
            spacing: tokens.space8

            StatusButton {
                objectName: card.primaryObjectName
                visible: text.length > 0
                text: card.primaryText
                enabled: card.primaryEnabled
                onClicked: card.primaryAction()
            }
            StatusButton {
                objectName: card.secondaryObjectName
                visible: text.length > 0
                text: card.secondaryText
                enabled: card.secondaryEnabled
                subdued: true
                onClicked: card.secondaryAction()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
