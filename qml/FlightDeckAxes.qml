import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Native Flight Deck Axes presentation. AppBackend remains the single owner
// of mappings, transforms, output availability, and bounded UI snapshots.
Flickable {
    id: root
    objectName: "flightDeckAxes"

    property var readinessModel
    // Test-only visual seams. Production always reads the published backend
    // axis snapshot and selected-controller identity.
    property var axisPresentationOverride: null
    property string inputDeviceNameOverride: ""
    property int expandedAxisIndex: -1
    property int configurationRevision: 0
    property int routeNoticeAxis: -1
    property string routeNotice: ""
    property int conflictAxis: -1
    property string conflictTarget: ""
    signal navigateToPage(int page)
    signal requestAxisLearning(string target)
    signal requestQuickMap()

    readonly property bool usingPresentationOverride: axisPresentationOverride !== null
    readonly property var axisItems: usingPresentationOverride ? axisPresentationOverride : backend.axes
    readonly property var outputChoices: backend.virtualAxisChoices
    readonly property string inputDeviceName: inputDeviceNameOverride.length > 0 ? inputDeviceNameOverride : (backend.deviceName || "Selected controller")
    readonly property bool hasVisibleAxes: visibleAxisCount() > 0

    contentWidth: width
    contentHeight: axesContent.implicitHeight + deck.space24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    FlightDeckTheme {
        id: deck
    }

    Rectangle {
        width: root.width
        height: Math.max(root.height, root.contentHeight)
        color: deck.primarySurface
        z: -1
    }

    function visibleAxisCount() {
        let count = 0;
        for (let index = 0; index < axisItems.length; ++index) {
            if (axisItems[index] && axisItems[index].available)
                ++count;
        }
        return count;
    }

    function axisForIndex(index) {
        for (let candidate = 0; candidate < axisItems.length; ++candidate) {
            if (Number(axisItems[candidate].index) === Number(index))
                return axisItems[candidate];
        }
        return null;
    }

    function sourceLabel(axis) {
        const reportedDevice = String((axis || {}).deviceName || "");
        return (reportedDevice.length > 0 ? reportedDevice : inputDeviceName) + " · " + String((axis || {}).detail || (axis || {}).hardwareLabel || "Physical axis");
    }

    function destinationLabel(axis) {
        if (!axis || axis.target === "Disabled")
            return "Output disabled";
        const alias = String(axis.outputAlias || "");
        return "vJoy " + backend.vjoyDeviceId + " · " + (alias.length > 0 ? alias : axis.target);
    }

    function percent(value, unipolar) {
        const number = Number(value || 0);
        return unipolar ? Math.round(number * 100) + "%" : (number >= 0 ? "+" : "") + Math.round(number * 100) + "%";
    }

    // This is a configuration-only query. Its revision deliberately does not
    // depend on the high-frequency axis snapshot used by the live meters.
    function adaptiveStateFor(axisIndex) {
        const revision = configurationRevision;
        return backend.adaptiveResponseContextState("profile", backend.activeProfileId, axisIndex);
    }

    function adaptiveLabel(axisIndex) {
        const state = adaptiveStateFor(axisIndex);
        if (!state || !state.effective || !state.effective.enabled)
            return "Adaptive off";
        const profile = state.profileLayer || ({});
        const category = state.categoryLayer || ({});
        const global = state.global || ({});
        const source = String(profile.source || category.source || global.source || "Enabled");
        return "Adaptive · " + source;
    }

    // Mirrors evaluateStaticNormalizedAxisTransfer: rescaled deadzone,
    // inversion, the existing curve evaluator, and output limits. Adaptive
    // response and hysteresis are intentionally omitted from this static view.
    function staticTransferFor(axisIndex, domainInput) {
        const axis = axisForIndex(axisIndex);
        if (!axis)
            return Number(domainInput);
        const unipolar = Boolean(axis.unipolar);
        const domainMinimum = unipolar ? 0 : -1;
        let value = Math.max(domainMinimum, Math.min(1, Number(domainInput)));
        const deadzone = Math.max(0, Math.min(0.95, Number(axis.deadzone || 0)));
        if (unipolar) {
            value = value <= deadzone ? 0 : Math.min((value - deadzone) / (1 - deadzone), 1);
        } else {
            const magnitude = Math.abs(value);
            value = magnitude <= deadzone ? 0 : Math.sign(value) * Math.min((magnitude - deadzone) / (1 - deadzone), 1);
        }
        if (axis.inverted)
            value = unipolar ? 1 - value : -value;
        const curve = backend.inspectCurve(value);
        if (curve && curve.output !== undefined)
            value = Number(curve.output);
        return Math.max(Number(axis.outputMinimum), Math.min(Number(axis.outputMaximum), value));
    }

    function configureAxis(axisIndex) {
        if (expandedAxisIndex === axisIndex) {
            expandedAxisIndex = -1;
            return;
        }
        backend.setSelectedAxis(axisIndex);
        expandedAxisIndex = axisIndex;
        routeNoticeAxis = -1;
    }

    function requestMapping(axisIndex, target, explicitOverride) {
        const axis = axisForIndex(axisIndex);
        if (axis && axis.fixed && target !== "Disabled") {
            routeNoticeAxis = axisIndex;
            routeNotice = "This axis was marked fixed during calibration. Complete a new calibration before routing it.";
            return false;
        }
        if (backend.setMapping(axisIndex, target, explicitOverride)) {
            routeNoticeAxis = -1;
            routeNotice = "";
            return true;
        }
        if (!explicitOverride) {
            conflictAxis = axisIndex;
            conflictTarget = target;
            routeConflictDialog.open();
        } else {
            routeNoticeAxis = axisIndex;
            routeNotice = "The requested route is not exposed by the current vJoy device. Open setup to review output availability.";
        }
        return false;
    }

    function openAdaptiveForAxis(axisIndex) {
        backend.setSelectedAxis(axisIndex);
        navigateToPage(9);
    }

    Connections {
        target: backend
        function onStateChanged() {
            root.configurationRevision += 1;
        }
    }

    component SummaryChip: Rectangle {
        property string label: ""
        property string tone: "informational"
        implicitHeight: 22
        implicitWidth: chipText.implicitWidth + deck.space16
        radius: deck.radiusPill
        color: tone === "fault" ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.14) : tone === "attention" ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.14) : tone === "healthy" ? Qt.rgba(deck.healthy.r, deck.healthy.g, deck.healthy.b, 0.14) : deck.accentMuted
        border.color: deck.statusColor(tone)
        border.width: 1
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: deck.statusColor(parent.tone)
            font.family: deck.telemetryFont
            font.pixelSize: 8
            font.bold: true
        }
    }

    component DeckButton: Button {
        id: control
        property bool subdued: false
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: control.text
            color: control.enabled ? (control.subdued ? deck.textSecondary : deck.primarySurface) : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.secondarySurface : control.down ? deck.accentMuted : control.hovered ? (control.subdued ? deck.selected : deck.focus) : (control.subdued ? deck.secondarySurface : deck.accent)
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component DeckCombo: ComboBox {
        id: control
        implicitHeight: deck.controlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            leftPadding: deck.space12
            rightPadding: deck.space24
            text: control.displayText
            color: control.enabled ? deck.textPrimary : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - deck.space12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: deck.textSecondary
            font.pixelSize: 16
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: control.pressed ? deck.selected : deck.primarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            objectName: control.objectName + "Choice_" + index
            width: ListView.view.width
            height: 34
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: control.textAt(index)
                color: deck.textPrimary
                font.family: deck.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted ? deck.selected : deck.elevatedSurface
            }
        }
        popup: Popup {
            objectName: control.objectName + "Popup"
            y: control.height - 1
            width: control.width
            padding: 4
            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: deck.radiusControl
                color: deck.elevatedSurface
                border.color: deck.border
            }
        }
    }

    component DeckSlider: Slider {
        id: control
        implicitHeight: 30
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 6
            radius: height / 2
            color: deck.primarySurface
            border.color: deck.border
            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: deck.accent
            }
        }
        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: width / 2
            color: control.pressed ? deck.focus : deck.elevatedSurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.accent
        }
    }

    component SettingTitle: Text {
        color: deck.textSecondary
        font.family: deck.telemetryFont
        font.pixelSize: 9
        font.bold: true
    }

    component AxisCard: FlightDeckCard {
        id: card
        tokens: deck
        property var axis: ({})
        readonly property int axisIndex: Number(axis.index)
        readonly property bool expanded: root.expandedAxisIndex === axisIndex
        readonly property var adaptiveState: root.adaptiveStateFor(axisIndex)
        readonly property bool adaptiveEnabled: Boolean(adaptiveState.effective && adaptiveState.effective.enabled)
        readonly property var curveState: backend.curveEditorState

        objectName: "flightDeckAxisCard_" + axisIndex
        Layout.fillWidth: true
        visible: Boolean(axis && axis.available)
        implicitHeight: visible ? cardContent.implicitHeight + deck.space24 : 0
        color: axis.target === "Disabled" ? deck.secondarySurface : deck.elevatedSurface

        ColumnLayout {
            id: cardContent
            anchors.fill: parent
            anchors.margins: deck.space12
            spacing: deck.space12

            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: axis.label || axis.hardwareLabel || "Axis"
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: root.sourceLabel(axis)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                SummaryChip {
                    label: axis.target === "Disabled" ? "OUTPUT DISABLED" : (axis.fixed ? "FIXED INPUT" : "OUTPUT ACTIVE")
                    tone: axis.target === "Disabled" ? "attention" : (axis.fixed ? "attention" : "healthy")
                }
                DeckButton {
                    text: card.expanded ? "CLOSE" : "CONFIGURE"
                    subdued: card.expanded
                    Layout.preferredWidth: 94
                    onClicked: root.configureAxis(card.axisIndex)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Text {
                    text: "PHYSICAL"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    font.bold: true
                }
                Text {
                    text: axis.hardwareLabel || axis.detail || "Axis"
                    color: deck.textSecondary
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                }
                Text {
                    text: "→"
                    color: deck.accent
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    text: "VIRTUAL"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    font.bold: true
                }
                Text {
                    text: root.destinationLabel(axis)
                    color: axis.target === "Disabled" ? deck.textMuted : deck.textPrimary
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space16
                FlightDeckAxisValueMeter {
                    tokens: deck
                    caption: axis.unipolar ? "NORMALIZED INPUT" : "NORMALIZED INPUT"
                    value: Number(axis.calibrated)
                    valid: true
                    unipolar: Boolean(axis.unipolar)
                    Layout.fillWidth: true
                }
                FlightDeckAxisValueMeter {
                    tokens: deck
                    caption: "FINAL VIRTUAL OUTPUT"
                    value: Number(axis.virtualValue)
                    valid: Boolean(axis.virtualValid)
                    unipolar: Boolean(axis.unipolar)
                    unavailableText: axis.target === "Disabled" ? "Disabled" : "Unavailable"
                    Layout.fillWidth: true
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: deck.space8
                SummaryChip {
                    label: axis.curveSummary || "Linear"
                    tone: "informational"
                }
                SummaryChip {
                    visible: Number(axis.deadzone) > 0
                    label: "DZ " + (Number(axis.deadzone) * 100).toFixed(1) + "%"
                    tone: "informational"
                }
                SummaryChip {
                    visible: Boolean(axis.inverted)
                    label: "INVERTED"
                    tone: "attention"
                }
                SummaryChip {
                    label: root.adaptiveLabel(card.axisIndex)
                    tone: card.adaptiveEnabled ? "healthy" : "informational"
                }
                SummaryChip {
                    visible: !axis.targetAvailable
                    label: "OUTPUT UNAVAILABLE"
                    tone: "fault"
                }
            }

            Item {
                visible: card.expanded
                Layout.fillWidth: true
                implicitHeight: visible ? editorContent.implicitHeight + deck.space12 : 0

                ColumnLayout {
                    id: editorContent
                    width: parent.width
                    spacing: deck.space12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: deck.divider
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: routingContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: routingContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            SettingTitle {
                                text: "ROUTING"
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "INPUT"
                                        color: deck.textMuted
                                        font.family: deck.telemetryFont
                                        font.pixelSize: 8
                                        font.bold: true
                                    }
                                    Text {
                                        text: root.sourceLabel(axis)
                                        color: deck.textPrimary
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Text {
                                    text: "→"
                                    color: deck.accent
                                    font.pixelSize: 22
                                    font.bold: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "OUTPUT · vJoy " + backend.vjoyDeviceId
                                        color: deck.textMuted
                                        font.family: deck.telemetryFont
                                        font.pixelSize: 8
                                        font.bold: true
                                    }
                                    DeckCombo {
                                        id: mappingSelector
                                        objectName: "flightDeckMappingSelector_" + card.axisIndex
                                        model: root.outputChoices
                                        currentIndex: Math.max(0, root.outputChoices.indexOf(axis.target))
                                        Layout.fillWidth: true
                                        onActivated: function (index) {
                                            root.requestMapping(card.axisIndex, mappingSelector.textAt(index), false);
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: axis.target === "Disabled" ? "Output disabled. Physical input remains visible but is not sent to vJoy." : "Mapping changes apply immediately through the existing profile command path."
                                    color: deck.textSecondary
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                DeckButton {
                                    objectName: "flightDeckAxisLearn_" + card.axisIndex
                                    visible: axis.target !== "Disabled"
                                    text: "LEARN INPUT"
                                    subdued: true
                                    Layout.preferredWidth: 106
                                    onClicked: root.requestAxisLearning(axis.target)
                                }
                            }
                            Rectangle {
                                visible: !axis.targetAvailable || root.routeNoticeAxis === card.axisIndex
                                Layout.fillWidth: true
                                implicitHeight: warningText.implicitHeight + deck.space16
                                radius: deck.radiusControl
                                color: Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.10)
                                border.color: deck.fault
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: deck.space8
                                    Text {
                                        text: "!"
                                        color: deck.fault
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    Text {
                                        id: warningText
                                        text: root.routeNoticeAxis === card.axisIndex ? root.routeNotice : "The configured vJoy output is not currently exposed."
                                        color: deck.textPrimary
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    DeckButton {
                                        text: "OPEN SETUP"
                                        subdued: true
                                        Layout.preferredWidth: 94
                                        onClicked: root.navigateToPage(2)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: responseContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: responseContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            SettingTitle {
                                text: "RESPONSE"
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Switch {
                                    id: invertSwitch
                                    checked: Boolean(axis.inverted)
                                    text: "Invert axis"
                                    Layout.fillWidth: true
                                    onToggled: backend.setAxisInverted(card.axisIndex, checked)
                                    contentItem: Text {
                                        text: invertSwitch.text
                                        leftPadding: invertSwitch.indicator.width + 8
                                        color: deck.textPrimary
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    indicator: Rectangle {
                                        implicitWidth: 34
                                        implicitHeight: 18
                                        radius: 9
                                        color: invertSwitch.checked ? deck.accent : deck.primarySurface
                                        border.color: deck.border
                                        Rectangle {
                                            width: 14
                                            height: 14
                                            radius: 7
                                            x: invertSwitch.checked ? 17 : 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: deck.elevatedSurface
                                        }
                                    }
                                }
                                Text {
                                    text: (Number(axis.deadzone) * 100).toFixed(1) + "% deadzone"
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                }
                            }
                            DeckSlider {
                                id: deadzoneSlider
                                from: 0
                                to: 0.25
                                value: Number(axis.deadzone)
                                Layout.fillWidth: true
                                onMoved: backend.setAxisDeadzone(card.axisIndex, value)
                            }
                            Text {
                                text: axis.unipolar ? "Lower-end deadzone is rescaled across the 0–100% input domain." : "Center deadzone is rescaled before inversion and curve response."
                                color: deck.textMuted
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "RESPONSE CURVE"
                                    Layout.preferredWidth: 108
                                }
                                DeckCombo {
                                    id: curveSelector
                                    model: ["Linear", "J-Curve", "S-Curve", "Advanced", "Custom", "Personal"]
                                    currentIndex: Math.max(0, model.indexOf(String(card.curveState.family || "Linear")))
                                    Layout.fillWidth: true
                                    onActivated: function (index) {
                                        backend.setSelectedAxis(card.axisIndex);
                                        backend.setCurveFamily(curveSelector.textAt(index));
                                    }
                                }
                                DeckButton {
                                    text: "EDIT"
                                    subdued: true
                                    Layout.preferredWidth: 58
                                    onClicked: {
                                        backend.setSelectedAxis(card.axisIndex);
                                        root.navigateToPage(6);
                                    }
                                }
                            }
                            RowLayout {
                                visible: String(card.curveState.family || "") === "J-Curve" || String(card.curveState.family || "") === "S-Curve"
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "CURVE STRENGTH"
                                    Layout.preferredWidth: 108
                                }
                                DeckSlider {
                                    from: 0
                                    to: 1
                                    value: Number(card.curveState.strength || 0)
                                    Layout.fillWidth: true
                                    onMoved: backend.setCurveStrength(value)
                                }
                                Text {
                                    text: Math.round(Number(card.curveState.strength || 0) * 100) + "%"
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 34
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "STATIC RESPONSE PREVIEW"
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 9
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                                DeckButton {
                                    text: "RESET CURVE"
                                    subdued: true
                                    Layout.preferredWidth: 94
                                    onClicked: {
                                        backend.setSelectedAxis(card.axisIndex);
                                        backend.resetCurveLinear();
                                    }
                                }
                            }
                            FlightDeckResponsePreview {
                                objectName: "flightDeckResponsePreview_" + card.axisIndex
                                tokens: deck
                                axisIndex: card.axisIndex
                                configurationRevision: root.configurationRevision
                                unipolar: Boolean(axis.unipolar)
                                transferEvaluator: root.staticTransferFor
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "Configured deadzone, inversion, response curve, and output limits. Adaptive Response remains a live-only overlay and is not predicted here."
                                color: deck.textMuted
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: limitsContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: limitsContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            SettingTitle {
                                text: "LIMITS"
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "OUTPUT MIN"
                                    Layout.preferredWidth: 90
                                }
                                DeckSlider {
                                    from: axis.unipolar ? 0 : -1
                                    to: Math.max((axis.unipolar ? 0 : -1) + 0.01, Number(axis.outputMaximum) - 0.01)
                                    value: Number(axis.outputMinimum)
                                    Layout.fillWidth: true
                                    onMoved: backend.setAxisOutputLimits(card.axisIndex, value, Number(axis.outputMaximum))
                                }
                                Text {
                                    text: root.percent(axis.outputMinimum, axis.unipolar)
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 42
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "OUTPUT MAX"
                                    Layout.preferredWidth: 90
                                }
                                DeckSlider {
                                    from: Math.min(0.99, Number(axis.outputMinimum) + 0.01)
                                    to: 1
                                    value: Number(axis.outputMaximum)
                                    Layout.fillWidth: true
                                    onMoved: backend.setAxisOutputLimits(card.axisIndex, Number(axis.outputMinimum), value)
                                }
                                Text {
                                    text: root.percent(axis.outputMaximum, axis.unipolar)
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 42
                                }
                            }
                            Text {
                                text: "Limits constrain final virtual authority; they do not change controller calibration."
                                color: deck.textMuted
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: advancedContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: advancedContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            SettingTitle {
                                text: "ADVANCED"
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "INPUT RANGE"
                                    Layout.preferredWidth: 108
                                }
                                DeckCombo {
                                    id: rangeSelector
                                    model: ["Centered", "One-Sided"]
                                    currentIndex: axis.rangeMode === "oneSided" ? 1 : 0
                                    Layout.fillWidth: true
                                    onActivated: function (index) {
                                        backend.setAxisRangeMode(card.axisIndex, rangeSelector.textAt(index));
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "HYSTERESIS"
                                    Layout.preferredWidth: 108
                                }
                                DeckSlider {
                                    from: 0
                                    to: 0.05
                                    value: Number(axis.hysteresis)
                                    Layout.fillWidth: true
                                    onMoved: backend.setAxisHysteresis(card.axisIndex, value)
                                }
                                Text {
                                    text: (Number(axis.hysteresis) * 100).toFixed(2) + "%"
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 42
                                }
                            }
                            RowLayout {
                                visible: axis.target !== "Disabled"
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "OUTPUT LABEL"
                                    Layout.preferredWidth: 108
                                }
                                TextField {
                                    id: aliasEditor
                                    text: axis.outputAlias || ""
                                    placeholderText: "Optional vJoy label"
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                    color: deck.textPrimary
                                    font.pixelSize: 10
                                    onEditingFinished: backend.setVirtualAxisAlias(axis.target, text)
                                    background: Rectangle {
                                        radius: deck.radiusControl
                                        color: deck.primarySurface
                                        border.color: aliasEditor.activeFocus ? deck.focus : deck.border
                                        border.width: aliasEditor.activeFocus ? 2 : 1
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                SettingTitle {
                                    text: "DISPLAY NAME"
                                    Layout.preferredWidth: 108
                                }
                                TextField {
                                    id: nameEditor
                                    text: axis.customName || ""
                                    placeholderText: axis.hardwareLabel || "Physical axis"
                                    selectByMouse: true
                                    Layout.fillWidth: true
                                    color: deck.textPrimary
                                    font.pixelSize: 10
                                    onEditingFinished: backend.setAxisCustomName(card.axisIndex, text)
                                    background: Rectangle {
                                        radius: deck.radiusControl
                                        color: deck.primarySurface
                                        border.color: nameEditor.activeFocus ? deck.focus : deck.border
                                        border.width: nameEditor.activeFocus ? 2 : 1
                                    }
                                }
                            }
                            Text {
                                text: "Technical identity: " + (axis.key || axis.hardwareLabel || "Unknown") + " · " + (axis.rangeModeLabel || "Configured domain")
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: adaptiveContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: adaptiveContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            SettingTitle {
                                text: "ADAPTIVE RESPONSE"
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: card.adaptiveEnabled ? "Adaptive Response is enabled for this axis." : "Adaptive Response is off for this axis."
                                        color: deck.textPrimary
                                        font.pixelSize: 11
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: "Current predictor: " + String((card.adaptiveState.effective || {}).model || "Configured") + ". Detailed controls remain on the Adaptive Response page."
                                        color: deck.textMuted
                                        font.pixelSize: 9
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                DeckButton {
                                    text: "CONFIGURE →"
                                    Layout.preferredWidth: 116
                                    onClicked: root.openAdaptiveForAxis(card.axisIndex)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: axesContent
        x: deck.space12
        y: deck.space12
        width: root.width - deck.space24
        spacing: deck.space12

        FlightDeckCard {
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: contextContent.implicitHeight + deck.space24
            color: deck.elevatedSurface
            ColumnLayout {
                id: contextContent
                anchors.fill: parent
                anchors.margins: deck.space12
                spacing: deck.space8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "AXIS OVERVIEW"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: "Physical control → configured transformation → virtual output"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 14
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    SummaryChip {
                        label: backend.vjoyReady ? "VJOY READY" : "VJOY ATTENTION"
                        tone: backend.vjoyReady ? "healthy" : "attention"
                    }
                    DeckButton {
                        objectName: "flightDeckAxesQuickMap"
                        text: "QUICK MAP"
                        subdued: true
                        enabled: backend.physicalConnected && backend.quickAssignAxisTargets.length > 0
                        onClicked: root.requestQuickMap()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "INPUT CONTEXT"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                    Text {
                        text: inputDeviceName
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "PROFILE"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                    Text {
                        text: backend.effectiveProfileDisplayName || backend.activeProfileName
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        Layout.maximumWidth: 240
                        elide: Text.ElideRight
                    }
                }
                Text {
                    visible: backend.connectedControllerCount > 1 && !usingPresentationOverride
                    text: "This baseline maps the selected controller's axes. Other connected controllers remain available in Devices & setup; select one there to inspect its authoritative axes."
                    color: deck.textMuted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    text: "Live meters consume the existing bounded presentation snapshot. Configuration remains available when no game is detected."
                    color: deck.textMuted
                    font.pixelSize: 9
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        FlightDeckCard {
            tokens: deck
            visible: !root.hasVisibleAxes
            Layout.fillWidth: true
            implicitHeight: emptyContent.implicitHeight + deck.space32
            color: deck.secondarySurface
            ColumnLayout {
                id: emptyContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space8
                Text {
                    text: "NO PHYSICAL AXES AVAILABLE"
                    color: deck.textPrimary
                    font.family: deck.displayFont
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    text: "Connect or select a verified controller to inspect its axis routing. Existing mappings are not changed while the controller is unavailable."
                    color: deck.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "OPEN DEVICES & SETUP"
                    Layout.preferredWidth: 172
                    onClicked: root.navigateToPage(2)
                }
            }
        }

        Repeater {
            model: root.axisItems
            delegate: AxisCard {
                required property var modelData
                axis: modelData
            }
        }
    }

    FlightDeckDialog {
        id: routeConflictDialog
        objectName: "flightDeckAxisRouteConflict"
        tokens: deck
        heading: "Route needs a decision"
        tone: "attention"
        preferredWidth: 460
        contentItem: ColumnLayout {
            spacing: deck.space12
            Text {
                text: "The requested output may already be used, or it is not currently exposed by vJoy."
                color: deck.textPrimary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                text: "Use this route anyway only when duplicate routing is intentional. Otherwise open setup or choose another output."
                color: deck.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    Layout.preferredWidth: 78
                    onClicked: routeConflictDialog.close()
                }
                DeckButton {
                    text: "USE ANYWAY"
                    Layout.preferredWidth: 102
                    onClicked: {
                        root.requestMapping(root.conflictAxis, root.conflictTarget, true);
                        routeConflictDialog.close();
                    }
                }
            }
        }
    }
}
