import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Dialogs 6.5
import QtQuick.Layouts 6.5

// Native Flight Deck presentation for the existing ProfilePortability command
// path. Files remain selected by the platform-owned picker and all validation,
// previewing, exporting, and import application remain in AppBackend.
FlightDeckDialog {
    id: control
    objectName: "flightDeckTransferDialog"

    required property var backendObject
    required property var deckTokens
    required property var categories
    required property var profiles
    property bool presentationFixture: false
    property string transferMode: "import"
    property string transferKind: "profile"
    property string transferProfileId: ""
    property string transferCategoryId: ""
    property var selectedPackCategoryIds: []
    property var selectedPackProfileIds: []
    property string categoryConflictMode: "merge"
    property string adaptivePresetConflictMode: "copy"
    property bool applyImportedCalibration: false
    property bool replaceCategoryConfirmed: false
    property bool replaceProfilesConfirmed: false
    property string localError: ""

    signal completed(string message)

    tokens: deckTokens
    preferredWidth: 820
    heading: transferMode === "import" ? "Import configuration" : "Export configuration"
    tone: localError.length > 0 ? "attention" : "informational"

    readonly property var preview: backendObject ? backendObject.portableImportPreview : ({})
    readonly property bool hasPreview: Number(preview.profileCount || 0) > 0

    function copy(value) {
        return JSON.parse(JSON.stringify(value || []));
    }
    function hasId(values, id) {
        return (values || []).indexOf(id) >= 0;
    }
    function categoryById(id) {
        for (let index = 0; index < categories.length; ++index) {
            if (String(categories[index].id || "") === String(id || ""))
                return categories[index];
        }
        return null;
    }
    function profilesForCategory(id) {
        const result = [];
        for (let index = 0; index < profiles.length; ++index) {
            if (String(profiles[index].categoryId || "") === String(id || ""))
                result.push(profiles[index]);
        }
        return result;
    }
    function openTransfer(mode, kind, profileId, categoryId) {
        transferMode = mode || "import";
        transferKind = kind || "profile";
        transferProfileId = profileId || "";
        transferCategoryId = categoryId || "";
        selectedPackCategoryIds = categoryId ? [categoryId] : [];
        selectedPackProfileIds = profileId ? [profileId] : [];
        categoryConflictMode = "merge";
        adaptivePresetConflictMode = "copy";
        applyImportedCalibration = false;
        replaceCategoryConfirmed = false;
        replaceProfilesConfirmed = false;
        localError = "";
        open();
    }
    function selectTransferKind(kind) {
        transferKind = kind;
        if (kind === "category" && selectedPackCategoryIds.length === 0 && categories.length > 0) {
            selectedPackCategoryIds = [transferCategoryId || categories[0].id];
            selectedPackProfileIds = [];
        }
    }
    function togglePackCategory(id, checked) {
        const values = copy(selectedPackCategoryIds);
        const index = values.indexOf(id);
        if (checked && index < 0)
            values.push(id);
        if (!checked && index >= 0)
            values.splice(index, 1);
        selectedPackCategoryIds = values;
    }
    function togglePackProfile(id, categoryId, checked) {
        const values = copy(selectedPackProfileIds);
        const categoryIndex = selectedPackCategoryIds.indexOf(categoryId);
        if (categoryIndex >= 0) {
            const categoryValues = copy(selectedPackCategoryIds);
            categoryValues.splice(categoryIndex, 1);
            selectedPackCategoryIds = categoryValues;
            const siblings = profilesForCategory(categoryId);
            for (let index = 0; index < siblings.length; ++index) {
                if (siblings[index].id !== id && values.indexOf(siblings[index].id) < 0)
                    values.push(siblings[index].id);
            }
        }
        const index = values.indexOf(id);
        if (checked && index < 0)
            values.push(id);
        if (!checked && index >= 0)
            values.splice(index, 1);
        selectedPackProfileIds = values;
    }
    function applyImport() {
        if (presentationFixture)
            return false;
        const destination = Number(preview.categoryCount || 0) === 1
            ? importDestinationCategory.currentValue : "";
        const changed = backendObject.applyPortableImport(destination, replaceImportedProfiles.checked,
            categoryConflictMode, applyImportedCalibration, adaptivePresetConflictMode);
        if (changed) {
            completed(String(backendObject.portableImportStatus || "Import completed successfully"));
            close();
        } else {
            localError = String(backendObject.portableImportStatus || "The configuration could not be imported.");
        }
        return changed;
    }
    function requestImport() {
        localError = "";
        if (!hasPreview) {
            localError = String(backendObject.portableImportStatus || "Choose a supported configuration file first.");
            return;
        }
        if (categoryConflictMode === "replace" && !replaceCategoryConfirmed) {
            replaceCategoryConfirmation.open();
            return;
        }
        if (replaceImportedProfiles.checked && !replaceProfilesConfirmed) {
            replaceProfilesConfirmation.open();
            return;
        }
        applyImport();
    }

    component DeckButton: Button {
        id: button
        property bool subdued: false
        property bool destructive: false
        implicitHeight: deckTokens.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: button.text
            color: !button.enabled ? deckTokens.disabled : button.destructive ? deckTokens.fault
                : button.subdued ? deckTokens.textSecondary : deckTokens.primarySurface
            font.family: deckTokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deckTokens.radiusControl
            color: !button.enabled ? deckTokens.secondarySurface : button.down
                ? deckTokens.accentMuted : button.hovered ? deckTokens.selected
                : button.destructive ? Qt.rgba(deckTokens.fault.r, deckTokens.fault.g, deckTokens.fault.b, 0.10)
                : button.subdued ? deckTokens.secondarySurface : deckTokens.accent
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? deckTokens.focus
                : button.destructive ? deckTokens.fault : deckTokens.border
        }
    }

    component DeckCombo: ComboBox {
        id: combo
        implicitHeight: deckTokens.controlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            leftPadding: deckTokens.space12
            rightPadding: deckTokens.space24
            text: combo.displayText
            color: combo.enabled ? deckTokens.textPrimary : deckTokens.disabled
            font.family: deckTokens.telemetryFont
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - deckTokens.space12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: deckTokens.textSecondary
            font.pixelSize: 16
        }
        background: Rectangle {
            radius: deckTokens.radiusControl
            color: combo.pressed ? deckTokens.selected : deckTokens.primarySurface
            border.width: combo.activeFocus ? 2 : 1
            border.color: combo.activeFocus ? deckTokens.focus : deckTokens.border
        }
        delegate: ItemDelegate {
            required property int index
            width: ListView.view.width
            height: 34
            highlighted: combo.highlightedIndex === index
            contentItem: Text {
                leftPadding: deckTokens.popupRowPadding
                rightPadding: deckTokens.popupRowPadding
                text: combo.textAt(index)
                color: deckTokens.textPrimary
                font.family: deckTokens.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: combo.highlightedIndex === index ? deckTokens.selected : deckTokens.elevatedSurface }
        }
        popup: Popup {
            y: combo.height - 1
            width: combo.width
            padding: deckTokens.popupPadding
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: combo.delegateModel
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: deckTokens.radiusControl
                color: deckTokens.elevatedSurface
                border.color: deckTokens.border
            }
        }
    }

    component DeckCheck: CheckBox {
        id: check
        property bool subdued: false
        focusPolicy: Qt.StrongFocus
        spacing: deckTokens.space8
        indicator: Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            x: check.leftPadding
            y: check.topPadding + (check.availableHeight - height) / 2
            radius: 3
            color: check.checked ? deckTokens.accent : deckTokens.primarySurface
            border.width: check.activeFocus ? 2 : 1
            border.color: check.activeFocus ? deckTokens.focus : check.checked ? deckTokens.accent : deckTokens.border
            Text { anchors.centerIn: parent; text: check.checked ? "✓" : ""; color: deckTokens.primarySurface; font.bold: true; font.pixelSize: 11 }
        }
        contentItem: Text {
            leftPadding: check.indicator.width + check.spacing
            text: check.text
            color: check.enabled ? deckTokens.textSecondary : deckTokens.disabled
            font.family: deckTokens.telemetryFont
            font.pixelSize: 9
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ScrollView {
        implicitWidth: control.availableWidth
        // Keep the compact import/export chooser intentional; grow only as
        // real form content arrives, then scroll its body before the modal can
        // extend beyond the application viewport.
        implicitHeight: Math.min(590, Math.max(120, transferBody.implicitHeight))
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            id: transferBody
            width: parent.width
            spacing: deckTokens.space12

            Text {
                Layout.fillWidth: true
                text: control.transferMode === "import"
                    ? "Select a supported HOTAS BF6 package, review the authoritative preview, then confirm it. Nothing changes while you review."
                    : "Choose the profile, category, or pack to export. The existing portability service writes the unchanged supported format."
                color: deckTokens.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: deckTokens.space8
                Repeater {
                    model: [{ label: "IMPORT", value: "import" }, { label: "EXPORT", value: "export" }]
                    delegate: DeckButton {
                        required property var modelData
                        text: modelData.label
                        subdued: control.transferMode !== modelData.value
                        onClicked: { control.transferMode = modelData.value; control.localError = ""; }
                    }
                }
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [{ label: "PROFILE", value: "profile" }, { label: "CATEGORY", value: "category" }, { label: "PACK", value: "pack" }]
                    delegate: DeckButton {
                        required property var modelData
                        text: modelData.label
                        subdued: control.transferKind !== modelData.value
                        onClicked: control.selectTransferKind(modelData.value)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: exportContent.implicitHeight + deckTokens.space24
                visible: control.transferMode === "export"
                radius: deckTokens.radiusControl
                color: deckTokens.secondarySurface
                border.color: deckTokens.border
                ColumnLayout {
                    id: exportContent
                    anchors.fill: parent
                    anchors.margins: deckTokens.space12
                    spacing: deckTokens.space8
                    Text {
                        Layout.fillWidth: true
                        text: control.transferKind === "profile" ? "EXPORT PROFILE"
                            : control.transferKind === "category" ? "EXPORT CATEGORY" : "EXPORT PORTABLE PACK"
                        color: deckTokens.textPrimary
                        font.family: deckTokens.telemetryFont
                        font.pixelSize: 10
                        font.bold: true
                    }
                    ColumnLayout {
                        visible: control.transferKind === "profile"
                        Layout.fillWidth: true
                        spacing: deckTokens.space4
                        Text { text: "PROFILE"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        DeckCombo {
                            id: exportProfileChoice
                            Layout.fillWidth: true
                            model: control.profiles
                            textRole: "displayName"
                            valueRole: "id"
                            currentIndex: {
                                for (let index = 0; index < control.profiles.length; ++index) {
                                    if (String(control.profiles[index].id) === String(control.transferProfileId)) return index;
                                }
                                return backend.activeProfileIndex;
                            }
                            onActivated: control.transferProfileId = currentValue
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Includes this profile’s portable configuration and required dependencies as defined by the existing export format."
                            color: deckTokens.textSecondary
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                    ColumnLayout {
                        visible: control.transferKind === "category"
                        Layout.fillWidth: true
                        spacing: deckTokens.space4
                        Text { text: "CATEGORY"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        DeckCombo {
                            id: exportCategoryChoice
                            Layout.fillWidth: true
                            model: control.categories
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: {
                                for (let index = 0; index < control.categories.length; ++index) {
                                    if (control.hasId(control.selectedPackCategoryIds, control.categories[index].id)) return index;
                                }
                                return 0;
                            }
                            onActivated: { control.selectedPackCategoryIds = [currentValue]; control.selectedPackProfileIds = []; }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Includes this category, its profiles, and its existing game-association behavior."
                            color: deckTokens.textSecondary
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                    ColumnLayout {
                        visible: control.transferKind === "pack"
                        Layout.fillWidth: true
                        spacing: deckTokens.space8
                        Text { text: "PACK CONTENT"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        TextField {
                            id: packName
                            Layout.fillWidth: true
                            text: "HOTAS BF6 Pack"
                            placeholderText: "Pack name"
                            color: deckTokens.textPrimary
                            font.family: deckTokens.telemetryFont
                            font.pixelSize: 10
                            background: Rectangle { radius: deckTokens.radiusControl; color: deckTokens.primarySurface; border.color: parent.activeFocus ? deckTokens.focus : deckTokens.border; border.width: parent.activeFocus ? 2 : 1 }
                        }
                        TextField {
                            id: packDescription
                            Layout.fillWidth: true
                            placeholderText: "Optional description"
                            color: deckTokens.textPrimary
                            font.family: deckTokens.telemetryFont
                            font.pixelSize: 10
                            background: Rectangle { radius: deckTokens.radiusControl; color: deckTokens.primarySurface; border.color: parent.activeFocus ? deckTokens.focus : deckTokens.border; border.width: parent.activeFocus ? 2 : 1 }
                        }
                        Repeater {
                            model: control.categories
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                DeckCheck {
                                    text: modelData.name
                                    checked: control.hasId(control.selectedPackCategoryIds, modelData.id)
                                    onToggled: control.togglePackCategory(modelData.id, checked)
                                }
                                Repeater {
                                    model: control.profilesForCategory(modelData.id)
                                    delegate: DeckCheck {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        leftPadding: deckTokens.space16
                                        text: modelData.displayName || modelData.name
                                        checked: control.hasId(control.selectedPackCategoryIds, modelData.categoryId)
                                            || control.hasId(control.selectedPackProfileIds, modelData.id)
                                        onToggled: control.togglePackProfile(modelData.id, modelData.categoryId, checked)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckCheck { id: includeDevices; text: "Include devices"; checked: false; onToggled: if (!checked) includeCalibration.checked = false }
                            DeckCheck { id: includeCalibration; text: "Include calibration"; enabled: includeDevices.checked; checked: false }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckCheck { id: includeAutomations; text: "Related Automation"; checked: true }
                            DeckCheck { id: includeRelationships; text: "Profile controls"; checked: true }
                            DeckCheck { id: includeGameDetection; text: "Game associations"; checked: true }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Calibration is off by default. Required curves, output requirements, and Response Preset dependencies remain governed by the established pack format."
                            color: deckTokens.textSecondary
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: control.transferMode === "import" && control.hasPreview
                implicitHeight: previewContent.implicitHeight + deckTokens.space24
                radius: deckTokens.radiusControl
                color: deckTokens.secondarySurface
                border.color: deckTokens.healthy
                ColumnLayout {
                    id: previewContent
                    anchors.fill: parent
                    anchors.margins: deckTokens.space12
                    spacing: deckTokens.space8
                    Text {
                        Layout.fillWidth: true
                        text: "IMPORT PREVIEW · " + String(control.preview.kind || "configuration").toUpperCase()
                            + " · " + String(control.preview.name || "Unnamed package")
                        color: deckTokens.textPrimary
                        font.family: deckTokens.telemetryFont
                        font.pixelSize: 10
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Exported by " + String(control.preview.exporterVersion || "unknown version")
                            + " · " + Number(control.preview.categoryCount || 0) + " categories · "
                            + Number(control.preview.profileCount || 0) + " profiles · "
                            + Number(control.preview.automationCount || 0) + " Automation rules"
                        color: deckTokens.textSecondary
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: control.preview.categories || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "CATEGORY · " + modelData.name + " · " + modelData.profileCount + " profiles · " + modelData.conflict
                            color: deckTokens.textSecondary
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                    Repeater {
                        model: control.preview.profiles || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.category + " / " + modelData.name + " · " + modelData.mappedAxes
                                + " axes · " + modelData.mappedButtons + " buttons · " + modelData.povMappings + " POV"
                            color: deckTokens.textSecondary
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                    RowLayout {
                        visible: Number(control.preview.categoryCount || 0) === 1
                        Layout.fillWidth: true
                        Text { text: "DESTINATION"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        DeckCombo {
                            id: importDestinationCategory
                            Layout.fillWidth: true
                            model: [{ id: "", name: "Source category (create or merge)" }].concat(control.categories)
                            textRole: "name"
                            valueRole: "id"
                        }
                    }
                    RowLayout {
                        visible: (control.preview.categories || []).some(function (category) { return category.exists; })
                        Layout.fillWidth: true
                        Text { text: "EXISTING CATEGORY"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        Repeater {
                            model: [{ label: "MERGE", value: "merge" }, { label: "IMPORT AS NEW", value: "new" }, { label: "REPLACE", value: "replace" }]
                            delegate: DeckButton {
                                required property var modelData
                                text: modelData.label
                                subdued: control.categoryConflictMode !== modelData.value
                                onClicked: { control.categoryConflictMode = modelData.value; control.replaceCategoryConfirmed = false; }
                            }
                        }
                    }
                    RowLayout {
                        visible: Number(control.preview.adaptiveResponsePresetCount || 0) > 0
                        Layout.fillWidth: true
                        Text { text: "PRESET CONFLICT"; color: deckTokens.textMuted; font.family: deckTokens.telemetryFont; font.pixelSize: 8; font.bold: true }
                        Repeater {
                            model: [{ label: "KEEP LOCAL", value: "keep" }, { label: "IMPORT COPY", value: "copy" }, { label: "REPLACE", value: "replace" }]
                            delegate: DeckButton {
                                required property var modelData
                                text: modelData.label
                                subdued: control.adaptivePresetConflictMode !== modelData.value
                                onClicked: control.adaptivePresetConflictMode = modelData.value
                            }
                        }
                    }
                    Repeater {
                        model: control.preview.devices || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: deckTokens.space4
                            Text {
                                Layout.fillWidth: true
                                text: "DEVICE · " + modelData.name + " · " + modelData.axisCount + " axes · " + modelData.buttonCount + " buttons · " + modelData.state
                                color: deckTokens.textSecondary
                                font.pixelSize: 9
                                wrapMode: Text.WordWrap
                            }
                            DeckCombo {
                                visible: (modelData.choices || []).length > 1
                                Layout.fillWidth: true
                                model: modelData.choices || []
                                textRole: "name"
                                valueRole: "id"
                                onActivated: backendObject.selectPortableImportDevice(modelData.index, currentValue)
                            }
                        }
                    }
                    DeckCheck {
                        visible: !!control.preview.includesCalibration
                        text: "Apply imported calibration to the matched controller"
                        checked: control.applyImportedCalibration
                        onToggled: control.applyImportedCalibration = checked
                    }
                    Text {
                        visible: !!control.preview.includesCalibration
                        Layout.fillWidth: true
                        text: "Calibration remains local unless you explicitly opt in here. Ambiguous controllers require the existing authoritative selection."
                        color: deckTokens.attention
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: control.preview.warnings || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "REVIEW · " + modelData
                            color: deckTokens.attention
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                    DeckCheck {
                        id: replaceImportedProfiles
                        text: "Replace matching profiles"
                        checked: false
                        onToggled: control.replaceProfilesConfirmed = false
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        DeckButton { text: "IMPORT"; enabled: !control.presentationFixture; onClicked: control.requestImport() }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: control.localError.length > 0 || String(backendObject.portableImportStatus || "").length > 0
                implicitHeight: statusText.implicitHeight + deckTokens.space16
                radius: deckTokens.radiusControl
                color: Qt.rgba(deckTokens.statusColor(control.localError.length > 0 ? "attention" : "informational").r,
                    deckTokens.statusColor(control.localError.length > 0 ? "attention" : "informational").g,
                    deckTokens.statusColor(control.localError.length > 0 ? "attention" : "informational").b, 0.11)
                border.color: control.localError.length > 0 ? deckTokens.attention : deckTokens.border
                Text {
                    id: statusText
                    anchors.fill: parent
                    anchors.margins: deckTokens.space8
                    text: control.localError.length > 0 ? control.localError : String(backendObject.portableImportStatus || "")
                    color: deckTokens.textSecondary
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: control.close() }
                DeckButton {
                    text: control.transferMode === "import" ? "CHOOSE FILE" : "CHOOSE DESTINATION"
                    enabled: !control.presentationFixture
                    onClicked: {
                        control.localError = "";
                        if (control.transferMode === "import") importFileDialog.open();
                        else exportFileDialog.open();
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: replaceCategoryConfirmation
        tokens: deckTokens
        preferredWidth: 460
        heading: "Replace existing category?"
        tone: "fault"
        contentItem: ColumnLayout {
            width: replaceCategoryConfirmation.availableWidth
            spacing: deckTokens.space12
            Text {
                Layout.fillWidth: true
                text: "This replaces the matching non-active category and its profiles with the imported category. General, active, and last remaining categories stay protected."
                color: deckTokens.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: replaceCategoryConfirmation.close() }
                DeckButton {
                    text: "REPLACE CATEGORY"
                    destructive: true
                    onClicked: { control.replaceCategoryConfirmed = true; replaceCategoryConfirmation.close(); control.requestImport(); }
                }
            }
        }
    }

    FlightDeckDialog {
        id: replaceProfilesConfirmation
        tokens: deckTokens
        preferredWidth: 460
        heading: "Replace matching profiles?"
        tone: "fault"
        contentItem: ColumnLayout {
            width: replaceProfilesConfirmation.availableWidth
            spacing: deckTokens.space12
            Text {
                Layout.fillWidth: true
                text: "Matching profile names will be replaced with imported configuration. Leave this off to import safe renamed copies."
                color: deckTokens.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: replaceProfilesConfirmation.close() }
                DeckButton {
                    text: "REPLACE PROFILES"
                    destructive: true
                    onClicked: { control.replaceProfilesConfirmed = true; replaceProfilesConfirmation.close(); control.requestImport(); }
                }
            }
        }
    }

    FileDialog {
        id: importFileDialog
        title: "Import HOTAS BF6 configuration"
        fileMode: FileDialog.OpenFile
        nameFilters: control.transferKind === "pack" || control.transferKind === "category"
            ? ["HOTAS BF6 Pack (*.hbf6pack)"]
            : ["HOTAS BF6 Profile (*.hbf6profile)", "HOTAS BF6 Pack (*.hbf6pack)"]
        onAccepted: {
            control.localError = "";
            if (!backendObject.loadPortableImportPreview(selectedFile))
                control.localError = String(backendObject.portableImportStatus || "Couldn't import this configuration.");
        }
    }

    FileDialog {
        id: exportFileDialog
        title: "Export HOTAS BF6 configuration"
        fileMode: FileDialog.SaveFile
        nameFilters: control.transferKind === "pack" || control.transferKind === "category"
            ? ["HOTAS BF6 Pack (*.hbf6pack)"] : ["HOTAS BF6 Profile (*.hbf6profile)"]
        onAccepted: {
            const exported = control.transferKind === "profile"
                ? backendObject.exportPortableProfile(exportProfileChoice.currentValue, selectedFile)
                : backendObject.exportPortablePack(control.selectedPackCategoryIds, control.selectedPackProfileIds,
                    packName.text, packDescription.text, includeDevices.checked, includeCalibration.checked,
                    includeAutomations.checked, includeRelationships.checked, includeGameDetection.checked, selectedFile);
            if (exported) {
                control.completed(String(backendObject.portableImportStatus || "Export completed successfully"));
                control.close();
            } else {
                control.localError = String(backendObject.portableImportStatus || "The configuration could not be exported.");
            }
        }
    }
}
