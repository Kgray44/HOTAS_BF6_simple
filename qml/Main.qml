import QtQuick 6.5
import QtQuick.Controls 6.5

// A single application window hosts exactly one presentation tree. Standard
// and Top Gun are live token variants of Standard; Legacy loads the concrete
// v1.6.3 surface. Neither path owns mapper state or the worker.
ApplicationWindow {
    id: shell
    width: 1320
    height: 840
    minimumWidth: 900
    minimumHeight: 650
    visible: true
    title: "HOTAS BF6"
    Theme { id: shellTheme }
    color: shellTheme.background
    font.family: shellTheme.displayFont

    Component { id: legacySurface; Legacy { } }
    Component { id: standardSurface; Standard { } }

    Loader {
        id: presentation
        anchors.fill: parent
        sourceComponent: themeManager.currentTheme === "Legacy" ? legacySurface : standardSurface
    }
}
