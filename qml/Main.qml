import QtQuick 6.5
import QtQuick.Controls 6.5

// A single application window hosts exactly one presentation tree. Themes
// remain token variants of the established surfaces; a UX experience may own
// a different shell while consuming the same backend and page host.
ApplicationWindow {
    id: shell
    objectName: "hotasShell"
    width: 1320
    height: 840
    minimumWidth: 900
    minimumHeight: 650
    // Native visual fixtures may exercise the same QML tree while the owner
    // is using the desktop. The explicit test argument keeps that window
    // constructed but never shown or focused; normal launches are unchanged.
    readonly property bool presentationHeadless: Qt.application.arguments.indexOf("--headless-presentation") >= 0
        || Qt.application.arguments.indexOf("--startup-smoke") >= 0
        || Qt.application.arguments.indexOf("--startup-smoke-isolated") >= 0
    visible: !presentationHeadless
    title: "HOTAS BF6"
    property var flightDeckLearningDialog: null
    onClosing: function(close) {
        if (backend.keepRunningInTray && backend.trayAvailable) {
            close.accepted = false
            backend.hideToTray()
        }
    }
    Theme { id: shellTheme }
    color: themeManager.currentExperience === "Flight Deck" ? "#0b1219" : shellTheme.background
    // Raw Text items and Qt Quick Controls inherit independently in some Qt
    // paths.  Select the same verified display family at the application
    // window boundary whenever Flight Deck is active.
    font.family: themeManager.currentExperience === "Flight Deck" ? "Segoe UI" : shellTheme.displayFont
    Component.onCompleted: {
        refreshTrayTheme()
        syncFlightDeckLearningDialog()
    }
    Connections {
        target: themeManager
        function onCurrentThemeChanged() {
            shell.refreshTrayTheme()
            const page = presentation.item && presentation.item.currentPage !== undefined ? presentation.item.currentPage : 8
            backend.recordCrashPresentationState(page, themeManager.currentTheme)
        }
        function onCurrentExperienceChanged() {
            shell.refreshTrayTheme()
            shell.syncFlightDeckLearningDialog()
        }
        function onFlightDeckAppearanceChanged() { shell.refreshTrayTheme() }
    }

    function refreshTrayTheme() {
        backend.setTrayTheme(themeManager.currentExperience === "Flight Deck"
            ? "Flight Deck " + themeManager.flightDeckAppearance
            : themeManager.currentTheme)
    }

    function syncFlightDeckLearningDialog() {
        if (themeManager.currentExperience === "Flight Deck") {
            if (!flightDeckLearningDialog)
                flightDeckLearningDialog = flightDeckLearningDialogComponent.createObject(shell)
            return
        }
        if (flightDeckLearningDialog) {
            flightDeckLearningDialog.close()
            flightDeckLearningDialog.destroy()
            flightDeckLearningDialog = null
        }
    }

    Component { id: legacySurface; Legacy { } }
    Component { id: standardSurface; Standard { } }
    Component { id: flightDeckSurface; FlightDeck { learningDialog: shell.flightDeckLearningDialog } }
    Component { id: flightDeckLearningDialogComponent; FlightDeckInputLearningDialog { } }

    Loader {
        id: presentation
        objectName: "presentationLoader"
        anchors.fill: parent
        sourceComponent: themeManager.currentExperience === "Flight Deck"
            ? flightDeckSurface
            : (themeManager.currentTheme === "Legacy" ? legacySurface : standardSurface)
    }
}
