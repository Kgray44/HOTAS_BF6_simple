import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Phase 1 alternate shell. It owns navigation and shell composition only;
// Standard remains the established authoritative page host, embedded without
// its own chrome. No backend-facing service, model, or command is duplicated.
Item {
    id: root
    objectName: "flightDeckSurface"
    anchors.fill: parent

    FlightDeckTheme {
        id: deck
        objectName: "flightDeckTheme"
    }
    property int currentPage: 8
    readonly property int loadedPageCount: standardPageHost.loadedPageCount

    function pageItem(page) {
        return standardPageHost.pageItem(page);
    }
    function loadedPage(page) {
        return standardPageHost.loadedPage(page);
    }
    function navigateTo(page) {
        if (currentPage === page)
            return;
        currentPage = page;
    }
    function pageTitle(page) {
        switch (page) {
        case 0:
            return "Axes";
        case 1:
            return "Buttons";
        case 2:
            return "Controller setup";
        case 3:
            return "Diagnostics";
        case 4:
            return "Settings";
        case 5:
            return "Profiles";
        case 6:
            return "Curve editor";
        case 7:
            return "Automation";
        case 8:
            return "Overview";
        case 9:
            return "Adaptive Response";
        }
        return "Overview";
    }
    FlightDeckReadiness {
        id: readinessModel
    }

    onCurrentPageChanged: standardPageHost.currentPage = currentPage

    Rectangle {
        anchors.fill: parent
        color: deck.applicationBackground
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: deck.space16
        spacing: deck.space16

        FlightDeckSurface {
            id: navigationRail
            tokens: deck
            Layout.fillHeight: true
            Layout.preferredWidth: root.width < 1080 ? 202 : 250
            Layout.minimumWidth: 188
            color: deck.navigationSurface

            Flickable {
                id: navigationViewport
                objectName: "flightDeckNavigationViewport"
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: navigationContent.height + deck.space32
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    policy: navigationViewport.contentHeight > navigationViewport.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                ColumnLayout {
                    id: navigationContent
                    x: deck.space16
                    y: deck.space16
                    width: navigationViewport.width - deck.space32
                    // At normal sizes the readiness card remains low in the
                    // rail. At the supported minimum the entire rail scrolls
                    // instead of letting controls escape below the surface.
                    height: Math.max(implicitHeight, navigationViewport.height - deck.space32)
                    spacing: deck.space12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space12
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: deck.radiusControl
                            color: deck.accentMuted
                            border.color: deck.accent
                            Text {
                                anchors.centerIn: parent
                                text: "FD"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: "HOTAS BF6"
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "FLIGHT DECK · PREVIEW"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: deck.divider
                    }

                    Repeater {
                        model: [
                            {
                                label: "Overview",
                                page: 8
                            },
                            {
                                label: "Devices & setup",
                                page: 2
                            },
                            {
                                label: "Axes",
                                page: 0
                            },
                            {
                                label: "Buttons",
                                page: 1
                            },
                            {
                                label: "Profiles",
                                page: 5
                            },
                            {
                                label: "Adaptive Response",
                                page: 9
                            },
                            {
                                label: "Automation",
                                page: 7
                            },
                            {
                                label: "Diagnostics",
                                page: 3
                            },
                            {
                                label: "Settings",
                                page: 4
                            },
                            {
                                label: "Curve editor",
                                page: 6
                            }
                        ]
                        delegate: FlightDeckNavItem {
                            objectName: "flightDeckNav_" + modelData.page
                            tokens: deck
                            label: modelData.label
                            selected: root.currentPage === modelData.page
                            Layout.fillWidth: true
                            onClicked: root.navigateTo(modelData.page)
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    FlightDeckCard {
                        objectName: "flightDeckReadiness"
                        tokens: deck
                        Layout.fillWidth: true
                        Layout.preferredHeight: readinessCard.implicitHeight + deck.space24
                        ColumnLayout {
                            id: readinessCard
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "SYSTEM READINESS"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            FlightDeckStatusChip {
                                tokens: deck
                                label: readinessModel.readiness.label
                                tone: readinessModel.readiness.tone
                                Layout.fillWidth: true
                            }
                            Text {
                                text: readinessModel.input.title
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: readinessModel.output.title
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: readinessModel.game.title + " · " + readinessModel.profile.title
                                color: deck.textMuted
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        FlightDeckSurface {
            tokens: deck
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: deck.primarySurface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 42 : 0
                    visible: root.currentPage !== 8
                    Text {
                        text: root.pageTitle(root.currentPage)
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 22
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    FlightDeckStatusChip {
                        tokens: deck
                        label: backend.physicalConnected ? "CONTROLLER" : "CONTROLLER"
                        value: backend.physicalConnected ? "CONNECTED" : "WAITING"
                        tone: backend.physicalConnected ? "healthy" : "attention"
                        visible: root.width >= 1110
                    }
                    FlightDeckStatusChip {
                        tokens: deck
                        label: "APPEARANCE"
                        value: themeManager.flightDeckAppearance.toUpperCase()
                        tone: "informational"
                        visible: root.width >= 1260
                    }
                }

                FlightDeckCard {
                    tokens: deck
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    color: deck.elevatedSurface

                    // The established page body is intentionally not restyled
                    // in Phase 1. Its real routes, drafts, dialogs, services,
                    // and commands stay authoritative during the shell proof.
                    Standard {
                        id: standardPageHost
                        anchors.fill: parent
                        embedded: true
                        flightDeckMode: true
                        flightDeckReadiness: readinessModel
                        currentPage: 8
                        onCurrentPageChanged: {
                            if (root.currentPage !== currentPage)
                                root.currentPage = currentPage;
                        }
                    }
                }
            }
        }
    }
}
