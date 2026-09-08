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
    visible: true
    title: "HOTAS BF6"
    onClosing: function(close) {
        if (backend.keepRunningInTray && backend.trayAvailable) {
            close.accepted = false
            backend.hideToTray()
        }
    }
    Theme { id: shellTheme }
    color: themeManager.currentExperience === "Flight Deck" ? "#0b1219" : shellTheme.background
    font.family: shellTheme.displayFont
    Component.onCompleted: backend.setTrayTheme(themeManager.currentTheme)
    Connections {
        target: themeManager
        function onCurrentThemeChanged() {
            backend.setTrayTheme(themeManager.currentTheme)
            const page = presentation.item && presentation.item.currentPage !== undefined ? presentation.item.currentPage : 8
            backend.recordCrashPresentationState(page, themeManager.currentTheme)
        }
    }

    Component { id: legacySurface; Legacy { } }
    Component { id: standardSurface; Standard { } }
    Component { id: flightDeckSurface; FlightDeck { } }

    Loader {
        id: presentation
        objectName: "presentationLoader"
        anchors.fill: parent
        sourceComponent: themeManager.currentExperience === "Flight Deck"
            ? flightDeckSurface
            : (themeManager.currentTheme === "Legacy" ? legacySurface : standardSurface)
    }
}
