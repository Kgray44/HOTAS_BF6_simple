import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The one application-level owner for transient operation feedback.  It is
// intentionally a sibling of the presentation Loader rather than a child of
// any page/Flickable, so notifications never change document geometry or get
// lost when a page is unloaded.
Item {
    id: root
    readonly property real textScale: typeof themeManager !== "undefined" ? themeManager.textScale : 1.15
    objectName: "globalNotificationHost"
    // The host owns only the notification column, not the entire application
    // canvas.  Besides keeping its hit area honest, this lets unrelated page
    // controls remain fully live in the empty space around floating cards.
    readonly property real windowWidth: parent ? parent.width : 0
    readonly property real windowHeight: parent ? parent.height : 0
    readonly property real hostWidth: Math.min(380, Math.max(250, windowWidth - 42))
    width: hostWidth
    height: Math.max(0, windowHeight - topSafeMargin - 18)
    x: Math.max(0, windowWidth - width - 20)
    y: topSafeMargin
    z: 10000
    property var backendObject: null
    property bool flightDeck: false
    property var activeNotifications: []
    property var queuedNotifications: []
    property var detailsNotification: ({})
    property int nextNotificationId: 1
    readonly property int visibleLimit: 3
    // Flight Deck's header controls occupy the first 58px.  A tight 66px
    // safe edge makes the cards feel attached to the shell without covering
    // Selected Device/Profile, Controller, or Appearance.
    readonly property int topSafeMargin: flightDeck ? 66 : 104
    readonly property color surfaceColor: flightDeck ? deck.elevatedSurface : "#20282d"
    readonly property color hoverSurfaceColor: flightDeck ? deck.secondarySurface : "#29353b"
    readonly property color textColor: flightDeck ? deck.textPrimary : "#e8eeee"
    readonly property color mutedColor: flightDeck ? deck.textSecondary : "#9dafb4"
    readonly property color infoColor: flightDeck ? deck.informational : "#7caebc"
    readonly property color successColor: flightDeck ? deck.healthy : "#8fd5c9"
    readonly property color warningColor: flightDeck ? deck.attention : "#d4ad69"
    readonly property color errorColor: flightDeck ? deck.fault : "#ca9090"

    FlightDeckTheme {
        id: deck
    }

    signal dismissRequested(string notificationId)
    signal notificationRefreshed(string notificationId)

    function severityFor(result) {
        const explicitSeverity = String(result && result.severity || "").toLowerCase()
        if (explicitSeverity === "success" || explicitSeverity === "info"
                || explicitSeverity === "warning" || explicitSeverity === "error")
            return explicitSeverity
        if (result && result.inProgress) return "info"
        return result && result.success ? "success" : "error"
    }

    function copyObject(value) {
        const copy = ({})
        for (const key in value) copy[key] = value[key]
        return copy
    }

    function replacementKeyFor(result, title, message) {
        const type = String(result && result.affectedObjectType || "")
        const id = String(result && result.affectedObjectId || "")
        if (type && id) return type + ":" + id
        return String(title || "") + "\n" + String(message || "")
    }

    function replaceEntry(entries, replacement) {
        const next = []
        for (let index = 0; index < entries.length; ++index)
            next.push(String(entries[index].id) === String(replacement.id) ? replacement : entries[index])
        return next
    }

    function enqueue(result, fallbackTitle, fallbackMessage, durationMs) {
        const source = result && result.title ? result
            : ({ success: false, title: fallbackTitle || "Operation feedback", message: fallbackMessage || "" })
        // Persistent health truth never enters this transient presentation.
        if (source.persistent || !source.title) return ""
        const title = String(source.title || fallbackTitle || "Operation feedback")
        const message = String(source.message || fallbackMessage || "")
        const key = replacementKeyFor(source, title, message)
        const all = activeNotifications.concat(queuedNotifications)
        for (let index = 0; index < all.length; ++index) {
            const existing = all[index]
            if (String(existing.replaceKey) !== key) continue
            const refreshed = copyObject(existing)
            refreshed.title = title
            refreshed.message = message
            refreshed.severity = severityFor(source)
            refreshed.timestamp = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss")
            refreshed.durationMs = durationMs > 0 ? durationMs : 5000
            refreshed.repeatCount = Number(existing.repeatCount || 1) + 1
            if (activeNotifications.some(function(entry) { return String(entry.id) === String(existing.id) }))
                activeNotifications = replaceEntry(activeNotifications, refreshed)
            else
                queuedNotifications = replaceEntry(queuedNotifications, refreshed)
            notificationRefreshed(String(existing.id))
            return String(existing.id)
        }
        const entry = {
            id: String(nextNotificationId++), title: title, message: message,
            severity: severityFor(source), timestamp: Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss"),
            durationMs: durationMs > 0 ? durationMs : 5000, replaceKey: key, repeatCount: 1
        }
        if (activeNotifications.length < visibleLimit) {
            const nextActive = activeNotifications.slice(0)
            nextActive.push(entry)
            activeNotifications = nextActive
        } else {
            const nextQueue = queuedNotifications.slice(0)
            nextQueue.push(entry)
            queuedNotifications = nextQueue
        }
        return entry.id
    }

    function dismiss(notificationId) {
        if (!notificationId) return
        dismissRequested(String(notificationId))
    }

    function finalizeDismiss(notificationId) {
        const id = String(notificationId || "")
        activeNotifications = activeNotifications.filter(function(entry) { return String(entry.id) !== id })
        if (String(detailsNotification.id || "") === id) detailsNotification = ({})
        if (queuedNotifications.length > 0) {
            const nextQueue = queuedNotifications.slice(0)
            const next = nextQueue.shift()
            const nextActive = activeNotifications.slice(0)
            nextActive.push(next)
            queuedNotifications = nextQueue
            activeNotifications = nextActive
        }
    }

    function openDetails(entry) {
        if (!entry || !entry.id) return
        detailsNotification = entry
        notificationDetails.open()
    }

    function copyDetails() {
        if (!detailsNotification || !detailsNotification.id || !backendObject) return false
        const text = "Notification: " + String(detailsNotification.title || "")
            + "\nSeverity: " + String(detailsNotification.severity || "information").toUpperCase()
            + "\nDetails: " + String(detailsNotification.message || "")
            + "\nTime: " + String(detailsNotification.timestamp || "")
        return backendObject.copyTextToClipboard(text)
    }

    Item {
        id: stack
        objectName: "globalNotificationStack"
        anchors.fill: parent
        clip: false

        Repeater {
            model: root.activeNotifications
            delegate: Item {
                id: notificationDelegate
                required property var modelData
                required property int index
                readonly property var entry: modelData
                property bool entered: false
                property bool leaving: false
                property bool hovered: notificationMouse.containsMouse
                property bool detailOpen: String(root.detailsNotification.id || "") === String(entry.id || "")
                property int remainingMs: Number(entry.durationMs || 5000)
                width: stack.width
                height: notificationCard.implicitHeight
                y: index * (height + 10)
                x: leaving || !entered ? width + 28 : 0
                Behavior on x { NumberAnimation { duration: root.flightDeck ? 190 : 150; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                Timer {
                    id: enterTimer
                    interval: 0
                    repeat: false
                    onTriggered: notificationDelegate.entered = true
                }
                Timer {
                    id: lifetimeTimer
                    interval: 50
                    repeat: true
                    running: notificationDelegate.entered && !notificationDelegate.leaving
                        && !notificationDelegate.hovered && !notificationDelegate.detailOpen
                    onTriggered: {
                        notificationDelegate.remainingMs -= interval
                        if (notificationDelegate.remainingMs <= 0)
                            notificationDelegate.beginDismiss()
                    }
                }
                Timer {
                    id: finalizeTimer
                    interval: root.flightDeck ? 210 : 170
                    repeat: false
                    onTriggered: root.finalizeDismiss(String(entry.id || ""))
                }
                function beginDismiss() {
                    if (leaving) return
                    leaving = true
                    finalizeTimer.restart()
                }
                Component.onCompleted: enterTimer.start()
                Connections {
                    target: root
                    function onDismissRequested(notificationId) {
                        if (String(notificationId) === String(notificationDelegate.entry.id || ""))
                            notificationDelegate.beginDismiss()
                    }
                    function onNotificationRefreshed(notificationId) {
                        if (String(notificationId) !== String(notificationDelegate.entry.id || "")) return
                        notificationDelegate.remainingMs = Number(notificationDelegate.entry.durationMs || 5000)
                        notificationDelegate.leaving = false
                        notificationDelegate.entered = true
                    }
                }

                Rectangle {
                    id: notificationCard
                    objectName: "floatingNotification_" + String(entry.id || "")
                    width: parent.width
                    implicitHeight: notificationContent.implicitHeight + (root.flightDeck ? deck.space24 : 24)
                    radius: root.flightDeck ? deck.radiusCard : 6
                    color: notificationDelegate.hovered ? root.hoverSurfaceColor : root.surfaceColor
                    border.width: notificationDelegate.hovered ? 2 : 1
                    border.color: notificationDelegate.hovered ? severityColor
                        : root.flightDeck ? deck.border : severityColor
                    readonly property color severityColor: entry.severity === "success" ? root.successColor
                        : entry.severity === "warning" ? root.warningColor
                        : entry.severity === "error" ? root.errorColor : root.infoColor
                    Behavior on color { ColorAnimation { duration: root.flightDeck ? deck.hoverDuration : 120 } }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        anchors.topMargin: root.flightDeck ? deck.space12 : 0
                        anchors.bottomMargin: root.flightDeck ? deck.space12 : 0
                        anchors.leftMargin: root.flightDeck ? deck.space8 : 0
                        width: 4
                        radius: 2
                        color: parent.severityColor
                    }
                    ColumnLayout {
                        id: notificationContent
                        anchors.fill: parent
                        anchors.margins: root.flightDeck ? deck.space12 : 12
                        anchors.leftMargin: root.flightDeck ? deck.space24 : 16
                        anchors.rightMargin: root.flightDeck ? deck.space32 : 30
                        spacing: root.flightDeck ? deck.space6 : 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: root.flightDeck ? deck.space8 : 5
                            Text {
                                Layout.fillWidth: true
                                text: String(entry.title || "") + (Number(entry.repeatCount || 1) > 1
                                    ? "  ×" + Number(entry.repeatCount) : "")
                                color: root.textColor
                                font.family: root.flightDeck ? deck.bodyFont : "Segoe UI Variable"
                                font.pixelSize: Math.round((root.flightDeck ? 12 : 11) * root.textScale)
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: notificationDelegate.detailOpen
                                implicitWidth: openLabel.implicitWidth + (root.flightDeck ? deck.space12 : 8)
                                implicitHeight: root.flightDeck ? 18 : 15
                                radius: root.flightDeck ? deck.radiusPill : 7
                                color: Qt.rgba(notificationCard.severityColor.r, notificationCard.severityColor.g,
                                               notificationCard.severityColor.b, root.flightDeck ? 0.17 : 0.12)
                                Text {
                                    id: openLabel
                                    anchors.centerIn: parent
                                    text: "OPEN"
                                    color: notificationCard.severityColor
                                    font.family: root.flightDeck ? deck.telemetryFont : "Segoe UI Variable"
                                    font.pixelSize: Math.round(8 * root.textScale)
                                    font.bold: true
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: String(entry.message || "").length > 0
                            text: String(entry.message || "")
                            color: root.mutedColor
                            font.family: root.flightDeck ? deck.bodyFont : "Segoe UI Variable"
                            font.pixelSize: Math.round((root.flightDeck ? 10 : 10) * root.textScale)
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(entry.severity || "info").toUpperCase()
                                + " · " + String(entry.timestamp || "")
                            color: notificationCard.severityColor
                            font.family: root.flightDeck ? deck.telemetryFont : "Segoe UI Variable"
                            font.pixelSize: Math.round((root.flightDeck ? 8 : 8) * root.textScale)
                            font.bold: true
                        }
                    }
                    Rectangle {
                        id: notificationClose
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: root.flightDeck ? deck.space8 : 5
                        width: root.flightDeck ? 24 : 22
                        height: width
                        radius: root.flightDeck ? deck.radiusControl : width / 2
                        color: notificationCloseMouse.containsMouse
                            ? Qt.rgba(notificationCard.severityColor.r, notificationCard.severityColor.g,
                                      notificationCard.severityColor.b, 0.16) : "transparent"
                        Text { anchors.centerIn: parent; text: "×"; color: root.mutedColor; font.family: root.flightDeck ? deck.bodyFont : "Segoe UI Variable"; font.pixelSize: Math.round(16 * root.textScale) }
                        MouseArea {
                            id: notificationCloseMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: notificationDelegate.beginDismiss()
                        }
                    }
                    MouseArea {
                        id: notificationMouse
                        anchors.fill: parent
                        anchors.rightMargin: 28
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openDetails(entry)
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: notificationDetails
        objectName: "notificationDetailsDialog"
        tokens: deck
        heading: "NOTIFICATION DETAILS"
        tone: String(root.detailsNotification.severity || "").toLowerCase() === "error" ? "fault"
            : String(root.detailsNotification.severity || "").toLowerCase() === "warning" ? "attention" : "informational"
        preferredWidth: 520
        z: root.z + 1
        property bool copied: false
        readonly property color detailSeverityColor: String(root.detailsNotification.severity || "").toLowerCase() === "success"
            ? root.successColor : String(root.detailsNotification.severity || "").toLowerCase() === "warning"
                ? root.warningColor : String(root.detailsNotification.severity || "").toLowerCase() === "error"
                    ? root.errorColor : root.infoColor
        closePolicy: Popup.CloseOnEscape
        onClosed: {
            const id = String(root.detailsNotification.id || "")
            root.detailsNotification = ({})
            if (id) root.dismiss(id)
        }
        Timer {
            id: copiedReset
            interval: 1600
            repeat: false
            onTriggered: notificationDetails.copied = false
        }
        component DetailButton: Button {
            id: detailButton
            property bool primary: false
            implicitHeight: deck.compactControlHeight
            leftPadding: deck.space12
            rightPadding: deck.space12
            focusPolicy: Qt.StrongFocus
            background: Rectangle {
                radius: deck.radiusControl
                color: !detailButton.enabled ? deck.disabled
                    : detailButton.down ? (detailButton.primary ? deck.accentMuted : deck.selected)
                    : detailButton.primary ? deck.accent : deck.secondarySurface
                border.width: detailButton.activeFocus ? 2 : 1
                border.color: detailButton.activeFocus ? deck.focus
                    : detailButton.primary ? deck.accent : deck.border
            }
            contentItem: Text {
                text: detailButton.text
                color: !detailButton.enabled ? deck.textMuted
                    : detailButton.primary ? (deck.light ? "white" : deck.primarySurface) : deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: Math.round(9 * root.textScale)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        contentItem: ScrollView {
            id: notificationDetailsScroll
            clip: true
            implicitWidth: notificationDetails.availableWidth
            implicitHeight: Math.min(notificationDetails.maximumBodyHeight,
                notificationDetailsContent.implicitHeight + deck.space4)
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
                id: notificationDetailsContent
                width: notificationDetailsScroll.availableWidth
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Rectangle {
                        implicitWidth: severityLabel.implicitWidth + deck.space16
                        implicitHeight: 22
                        radius: deck.radiusPill
                        color: Qt.rgba(notificationDetails.detailSeverityColor.r,
                                       notificationDetails.detailSeverityColor.g,
                                       notificationDetails.detailSeverityColor.b, 0.16)
                        border.color: notificationDetails.detailSeverityColor
                        Text {
                            id: severityLabel
                            anchors.centerIn: parent
                            text: String(root.detailsNotification.severity || "information").toUpperCase()
                            color: notificationDetails.detailSeverityColor
                            font.family: deck.telemetryFont
                            font.pixelSize: Math.round(8 * root.textScale)
                            font.bold: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: String(root.detailsNotification.timestamp || "")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: Math.round(9 * root.textScale)
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: String(root.detailsNotification.title || "")
                    color: deck.textPrimary
                    font.family: deck.bodyFont
                    font.pixelSize: Math.round(16 * root.textScale)
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: String(root.detailsNotification.message || "")
                    color: deck.textSecondary
                    font.family: deck.bodyFont
                    font.pixelSize: Math.round(11 * root.textScale)
                    wrapMode: Text.WordWrap
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Item { Layout.fillWidth: true }
                    DetailButton {
                        text: notificationDetails.copied ? "✓ COPIED" : "COPY DETAILS"
                        onClicked: {
                            if (root.copyDetails()) {
                                notificationDetails.copied = true
                                copiedReset.restart()
                            }
                        }
                    }
                    DetailButton { text: "CLOSE"; primary: true; onClicked: notificationDetails.close() }
                }
            }
        }
    }
}
