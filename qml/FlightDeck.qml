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
    readonly property var themeTokens: deck
    property int currentPage: 8
    property string flightDeckAutomationContext: ""
    property int flightDeckButtonContext: -1
    property var flightDeckAutomationPresentationState: ({})
    property var learningDialog: null
    // Holds only the transient Signal Flow viewport/selection while one of
    // Flight Deck's authoritative focused editors is shown.
    property var signalFlowPresentationState: ({})
    readonly property int loadedPageCount: currentPage === 7
        ? (automationPageLoader.item ? 1 : 0)
        : currentPage === 11 ? (signalFlowPageLoader.item ? 1 : 0) : standardPageHost.loadedPageCount

    function pageItem(page) {
        if (page === 7)
            return automationPageLoader.item;
        if (page === 11)
            return signalFlowPageLoader.item;
        return standardPageHost.pageItem(page);
    }
    function loadedPage(page) {
        if (page === 7)
            return automationPageLoader.item !== null;
        if (page === 11)
            return signalFlowPageLoader.item !== null;
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
            return "Devices";
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
        case 11:
            return "Signal Flow";
        }
        return "Overview";
    }
    FlightDeckReadiness {
        id: readinessModel
    }
    onCurrentPageChanged: {
        standardPageHost.currentPage = currentPage === 7 || currentPage === 11 ? -1 : currentPage;
        if (currentPage === 1 && flightDeckButtonContext > 0)
            buttonContextTimer.restart();
    }

    Timer {
        id: buttonContextTimer
        interval: 0
        repeat: false
        onTriggered: {
            const buttons = standardPageHost.pageItem(1);
            if (buttons && root.flightDeckButtonContext > 0) {
                buttons.setExpandedButton(root.flightDeckButtonContext);
                root.flightDeckButtonContext = -1;
            }
        }
    }

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
            objectName: "flightDeckNavigationRail"
            tokens: deck
            Layout.fillHeight: true
            Layout.preferredWidth: root.width < 1080 ? 202 : 250
            Layout.minimumWidth: 188
            color: deck.navigationSurface
            // The rail has fixed branding and readiness regions. Only the
            // navigation list consumes flexible height, so a short window
            // never pushes the system state below the rounded surface.
            property bool compactReadiness: height < 720

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: deck.railPadding
                spacing: deck.controlGap

                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: deck.radiusControl
                        color: deck.accentMuted
                        border.color: deck.accent
                        Image {
                            anchors.fill: parent
                            anchors.margins: deck.space4
                            source: "qrc:/assets/icons/png/hotas-bf6-256.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                            Accessible.name: "HOTAS BF6"
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
                            text: "FLIGHT DECK"
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

                Flickable {
                    id: navigationViewport
                    objectName: "flightDeckNavigationViewport"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: deck.navigationRowHeight * 2
                    clip: true
                    contentWidth: width
                    contentHeight: navigationContent.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {
                        policy: navigationViewport.contentHeight > navigationViewport.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }

                    ColumnLayout {
                        id: navigationContent
                        objectName: "flightDeckNavigationContent"
                        width: navigationViewport.width
                        spacing: deck.controlGap

                        Repeater {
                            model: [
                                { label: "Overview", page: 8 },
                                { label: "Devices & setup", page: 2 },
                                { label: "Axes", page: 0 },
                                { label: "Buttons", page: 1 },
                                { label: "Curve editor", page: 6 },
                                { label: "Profiles", page: 5 },
                                { label: "Adaptive Response", page: 9 },
                                { label: "Automation", page: 7 },
                                { label: "Signal Flow", page: 11 },
                                { label: "Diagnostics", page: 3 },
                                { label: "Settings", page: 4 }
                            ]
                            delegate: FlightDeckNavItem {
                                objectName: "flightDeckNav_" + modelData.page
                                tokens: deck
                                label: modelData.label
                                selected: root.currentPage === modelData.page
                                scrollViewport: navigationViewport
                                Layout.fillWidth: true
                                onClicked: root.navigateTo(modelData.page)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: deck.divider
                }

                FlightDeckCard {
                    objectName: "flightDeckReadiness"
                    tokens: deck
                    contentPadding: deck.cardPaddingCompact
                    Layout.fillWidth: true
                    implicitHeight: readinessCard.implicitHeight + contentPadding * 2
                    ColumnLayout {
                        id: readinessCard
                        objectName: "flightDeckReadinessContent"
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        Text {
                            visible: !navigationRail.compactReadiness
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
                            visible: navigationRail.compactReadiness
                            text: readinessModel.input.title + " · " + readinessModel.game.title
                            color: deck.textSecondary
                            font.pixelSize: 9
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        ColumnLayout {
                            visible: !navigationRail.compactReadiness
                            Layout.fillWidth: true
                            spacing: deck.space4
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
                    Layout.preferredHeight: 42
                    Text {
                        objectName: "flightDeckSharedPageTitle"
                        text: root.pageTitle(root.currentPage)
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 22
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    FlightDeckSelectedDeviceSelector {
                        compact: root.width < 1180
                        backendObject: backend
                        tokens: deck
                        onManageDevices: root.navigateTo(2)
                    }
                    FlightDeckHeaderPill {
                        objectName: "flightDeckControllerPill"
                        tokens: deck
                        text: "CONTROLLER"
                        value: backend.physicalConnected ? "CONNECTED" : "WAITING"
                        tone: backend.physicalConnected ? "healthy" : "attention"
                        onClicked: root.navigateTo(2)
                    }
                    FlightDeckHeaderPill {
                        objectName: "flightDeckAppearancePill"
                        tokens: deck
                        text: "APPEARANCE"
                        value: themeManager.flightDeckAppearance.toUpperCase()
                        tone: "informational"
                        onClicked: themeManager.setFlightDeckAppearance(
                            themeManager.flightDeckAppearance === "Light" ? "Dark" : "Light")
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
                        flightDeckLearningDialog: root.learningDialog
                        currentPage: 8
                        onCurrentPageChanged: {
                            if (currentPage === 7 && flightDeckAutomationContext.length > 0)
                                root.flightDeckAutomationContext = flightDeckAutomationContext;
                            if (root.currentPage !== 7 && root.currentPage !== 11 && root.currentPage !== currentPage)
                                root.currentPage = currentPage;
                        }
                    }
                    Loader {
                        id: automationPageLoader
                        objectName: "flightDeckAutomationLoader"
                        anchors.fill: parent
                        active: root.currentPage === 7
                        // An inactive full-surface Loader must not remain in
                        // the hit-test stack above native Standard-host pages.
                        visible: active
                        enabled: active
                        source: Qt.resolvedUrl("FlightDeckAutomation.qml")
                        onLoaded: {
                            item.presentationState = root.flightDeckAutomationPresentationState;
                            item.restorePresentationState();
                            if (root.flightDeckAutomationContext.length > 0) {
                                item.openRuleById(root.flightDeckAutomationContext);
                                root.flightDeckAutomationContext = "";
                            }
                        }
                        Connections {
                            target: automationPageLoader.item
                            function onPresentationStateCaptured(state) {
                                root.flightDeckAutomationPresentationState = state;
                            }
                            function onNavigateToProfile(profileId) {
                                standardPageHost.flightDeckProfileContext = profileId;
                                root.currentPage = 5;
                            }
                            function onNavigateToButton(buttonIndex) {
                                root.flightDeckButtonContext = buttonIndex;
                                root.currentPage = 1;
                            }
                        }
                    }
                    Loader {
                        id: signalFlowPageLoader
                        objectName: "flightDeckSignalFlowLoader"
                        anchors.fill: parent
                        active: root.currentPage === 11
                        visible: active
                        enabled: active
                        sourceComponent: Component {
                            FlightDeckSignalFlow {
                                anchors.fill: parent
                                backendObject: backend
                                presentationState: root.signalFlowPresentationState
                                onNavigateRequested: function(page, axis, state) {
                                    root.signalFlowPresentationState = state
                                    if (axis >= 0) backend.setSelectedAxis(axis)
                                    root.currentPage = page
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
