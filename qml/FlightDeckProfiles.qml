import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Dialogs 6.5
import QtQuick.Layouts 6.5

// Native Flight Deck Profiles presentation. The page owns only navigation and
// low-frequency configuration summaries. It reads AppBackend's published
// profile/category models and routes every mutation through the established
// command path; inspecting a card never changes the active runtime profile.
Flickable {
    id: root
    objectName: "flightDeckProfiles"

    property var readinessModel
    // Startup-test-only presentation seams. They never call an AppBackend
    // command and are deliberately ignored by production rendering.
    property var profilesPresentationOverride: null
    property var categoriesPresentationOverride: null
    property var runningApplicationsPresentationOverride: null
    property var profileDetailPresentationOverride: null
    property string view: "library" // library, category, profile
    property string selectedCategoryId: ""
    property string selectedProfileId: ""
    property string profileFilter: "all" // all, active, associated
    property string searchText: ""
    property var runningApplicationsSnapshot: []
    property var presentationState: ({})
    property string actionNotice: ""
    property string actionNoticeTone: "informational"

    signal navigateToPage(int page)
    // A Profile may point to a Device Rig, but inspecting that relationship
    // is intentionally view-only.  The Flight Deck shell owns the route.
    signal navigateToDeviceRig(string rigId)
    signal navigateToAutomation(string automationId)
    signal navigateToAdaptiveProfile(string profileId)
    signal presentationStateCaptured(var state)

    readonly property var categories: categoriesPresentationOverride !== null && categoriesPresentationOverride !== undefined ? categoriesPresentationOverride : backend.profileCategories
    readonly property var profiles: profilesPresentationOverride !== null && profilesPresentationOverride !== undefined ? profilesPresentationOverride : backend.profiles
    readonly property var runningApplications: runningApplicationsPresentationOverride !== null && runningApplicationsPresentationOverride !== undefined ? runningApplicationsPresentationOverride : runningApplicationsSnapshot
    readonly property var selectedCategory: categoryById(selectedCategoryId)
    readonly property var selectedProfile: profileById(selectedProfileId)
    readonly property var selectedDetail: detailFor(selectedProfileId)
    readonly property var selectedCategoryActivation: selectedCategoryId.length > 0
        ? backend.activationPreview(selectedCategoryId) : ({})
    readonly property bool usingPresentationFixture: (profilesPresentationOverride !== null && profilesPresentationOverride !== undefined) || (categoriesPresentationOverride !== null && categoriesPresentationOverride !== undefined)

    contentWidth: width
    contentHeight: profileContent.implicitHeight + deck.space24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    FlightDeckTheme {
        id: deck
    }

    Rectangle {
        parent: root
        anchors.fill: parent
        color: deck.primarySurface
        z: -1
    }

    function lower(value) {
        return String(value || "").toLowerCase();
    }
    function copyValue(value) {
        return JSON.parse(JSON.stringify(value || ({})));
    }
    function categoryById(id) {
        for (let index = 0; index < categories.length; ++index) {
            if (String(categories[index].id || "") === String(id || ""))
                return categories[index];
        }
        return null;
    }
    function profileById(id) {
        for (let index = 0; index < profiles.length; ++index) {
            if (String(profiles[index].id || "") === String(id || ""))
                return profiles[index];
        }
        return null;
    }
    function detailFor(id) {
        // Referencing profiles makes this recalculation follow stateChanged,
        // while avoiding a separate profile detail cache in the presentation.
        profiles;
        if (!id || String(id).length === 0)
            return ({});
        if (profileDetailPresentationOverride && profileDetailPresentationOverride[String(id)] !== undefined) {
            return profileDetailPresentationOverride[String(id)];
        }
        return backend.profileDetail(String(id));
    }
    function profilesForCategory(id) {
        const result = [];
        const includedIds = ({});
        const category = categoryById(id);
        const orderedIds = category ? (category.profileIds || []) : [];
        for (let ordered = 0; ordered < orderedIds.length; ++ordered) {
            const profile = profileById(orderedIds[ordered]);
            const profileId = profile ? String(profile.id || "") : "";
            if (profile && profileId.length > 0
                    && String(profile.categoryId || "") === String(id || "")
                    && !includedIds[profileId]) {
                result.push(profile);
                includedIds[profileId] = true;
            }
        }
        for (let index = 0; index < profiles.length; ++index) {
            const profile = profiles[index];
            const profileId = String(profile.id || "");
            if (String(profile.categoryId || "") === String(id || "")
                    && profileId.length > 0 && !includedIds[profileId]) {
                result.push(profile);
                includedIds[profileId] = true;
            }
        }
        return result;
    }
    function moveAutomaticProfile(profileId, direction) {
        if (usingPresentationFixture || !selectedCategoryId.length)
            return false;
        const ids = profilesForCategory(selectedCategoryId).map(function(profile) { return String(profile.id || ""); });
        const source = ids.indexOf(String(profileId || ""));
        const destination = source + direction;
        if (source < 0 || destination < 0 || destination >= ids.length)
            return false;
        const moved = ids[source];
        ids[source] = ids[destination];
        ids[destination] = moved;
        return backend.reorderCategoryAutomaticProfiles(selectedCategoryId, ids);
    }
    function friendlyGameName(executable) {
        const base = String(executable || "").split(/[\\/]/).pop();
        const stem = base.replace(/\.exe$/i, "");
        if (stem.toLowerCase() === "bf6")
            return "Battlefield 6";
        if (stem.toLowerCase() === "starcitizen")
            return "Star Citizen";
        return stem.replace(/([a-z])([A-Z])/g, "$1 $2").replace(/[_-]+/g, " ") || base;
    }
    function runningApplicationFor(executable) {
        const target = String(executable || "").split(/[\\/]/).pop().toLowerCase();
        for (let index = 0; index < runningApplications.length; ++index) {
            if (String(runningApplications[index].executable || "").toLowerCase() === target) {
                return runningApplications[index];
            }
        }
        return null;
    }
    function categoryHasRunningGame(category) {
        const rules = category ? (category.executableRules || []) : [];
        for (let index = 0; index < rules.length; ++index) {
            if (runningApplicationFor(rules[index]))
                return true;
        }
        return false;
    }
    function categoryBehavior(category) {
        if (!category)
            return "No category selected";
        return "Resolver order: Preferred profiles, then Fallback profiles; legacy defaults are compatibility-only.";
    }
    function profileMatchesFilter(profile) {
        if (!profile)
            return false;
        if (profileFilter === "active" && !profile.active)
            return false;
        if (profileFilter === "associated") {
            const category = categoryById(profile.categoryId);
            if (!category || (category.executableRules || []).length === 0)
                return false;
        }
        const needle = lower(searchText).trim();
        if (needle.length === 0)
            return true;
        const category = categoryById(profile.categoryId);
        const fields = [profile.name, profile.displayName, profile.categoryName, category ? category.name : ""];
        for (let index = 0; index < fields.length; ++index) {
            if (lower(fields[index]).indexOf(needle) >= 0)
                return true;
        }
        return false;
    }
    function openCategory(id) {
        selectedCategoryId = String(id || "");
        selectedProfileId = "";
        view = "category";
        refreshRunningApplications();
    }
    function openProfile(id) {
        selectedProfileId = String(id || "");
        const profile = profileById(selectedProfileId);
        selectedCategoryId = profile ? String(profile.categoryId || "") : "";
        view = "profile";
    }
    function selectLibraryCategory(id) {
        selectedCategoryId = String(id || "");
        selectedProfileId = "";
    }
    function selectLibraryProfile(id) {
        selectedProfileId = String(id || "");
        const profile = profileById(selectedProfileId);
        selectedCategoryId = profile ? String(profile.categoryId || "") : "";
    }
    function ensureLibrarySelection() {
        if (selectedProfileId.length || selectedCategoryId.length)
            return;
        const active = backend.activeProfileId || "";
        if (active.length && profileById(active)) {
            selectLibraryProfile(active);
        } else if (profiles.length) {
            selectLibraryProfile(profiles[0].id);
        } else if (categories.length) {
            selectLibraryCategory(categories[0].id);
        }
    }
    function returnToLibrary() {
        view = "library";
        selectedCategoryId = "";
        selectedProfileId = "";
    }
    function activateProfile(id) {
        if (usingPresentationFixture)
            return false;
        return backend.activateProfile(String(id || ""));
    }
    function openActiveProfileEditor(page) {
        // Axes and Buttons are intentionally active-profile editors in this
        // baseline. Never activate an inspected profile merely to satisfy a
        // navigation request.
        if (!selectedDetail.active)
            return false;
        navigateToPage(page);
        return true;
    }
    function openAdaptiveForSelectedProfile() {
        if (!selectedProfileId.length)
            return false;
        navigateToAdaptiveProfile(selectedProfileId);
        return true;
    }
    function openAutomationForSelectedProfile() {
        const rules = selectedDetail.automations || [];
        navigateToAutomation(rules.length > 0 ? String(rules[0].id || "") : "");
    }
    function refreshRunningApplications() {
        if (usingPresentationFixture)
            return;
        runningApplicationsSnapshot = backend.runningApplications();
        backend.refreshRunningApplications();
    }
    function addGameRule(categoryId, rawRule) {
        const category = categoryById(categoryId);
        const rule = String(rawRule || "").trim();
        if (!category || rule.length === 0 || usingPresentationFixture)
            return false;
        const rules = (category.executableRules || []).slice();
        rules.push(rule);
        return backend.setCategoryGameDetectionRules(categoryId, rules);
    }
    function removeGameRule(categoryId, rule) {
        const category = categoryById(categoryId);
        if (!category || usingPresentationFixture)
            return false;
        const remaining = (category.executableRules || []).filter(function (candidate) {
            return String(candidate).toLowerCase() !== String(rule).toLowerCase();
        });
        return backend.setCategoryGameDetectionRules(categoryId, remaining);
    }
    function capturePresentationState() {
        presentationStateCaptured({
            view: view,
            selectedCategoryId: selectedCategoryId,
            selectedProfileId: selectedProfileId,
            profileFilter: profileFilter,
            searchText: searchText,
            contentY: contentY
        });
    }
    function restorePresentationState() {
        const saved = presentationState || ({});
        if (!saved.view)
            return;
        view = saved.view;
        selectedCategoryId = saved.selectedCategoryId || "";
        selectedProfileId = saved.selectedProfileId || "";
        profileFilter = saved.profileFilter || "all";
        searchText = saved.searchText || "";
        contentY = Number(saved.contentY || 0);
    }

    Component.onCompleted: {
        restorePresentationState();
        refreshRunningApplications();
        ensureLibrarySelection();
    }
    Component.onDestruction: capturePresentationState()

    Connections {
        target: backend
        function onRunningApplicationsChanged() {
            if (!root.usingPresentationFixture)
                root.runningApplicationsSnapshot = backend.runningApplications();
        }
    }

    component SectionLabel: RowLayout {
        property string label: "SECTION"
        Layout.fillWidth: true
        spacing: deck.space8
        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: width / 2
            color: deck.accent
        }
        Text {
            text: parent.label
            color: deck.textMuted
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: deck.divider
        }
    }

    component SummaryChip: Rectangle {
        property string label: "STATUS"
        property string tone: "informational"
        implicitHeight: 23
        implicitWidth: chipText.implicitWidth + deck.space16
        radius: deck.radiusPill
        color: Qt.rgba(deck.statusColor(tone).r, deck.statusColor(tone).g, deck.statusColor(tone).b, deck.light ? 0.11 : 0.18)
        border.width: 1
        border.color: Qt.rgba(deck.statusColor(tone).r, deck.statusColor(tone).g, deck.statusColor(tone).b, 0.75)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: deck.textPrimary
            font.family: deck.telemetryFont
            font.pixelSize: 8
            font.bold: true
            elide: Text.ElideRight
        }
    }

    component DeckButton: Button {
        id: control
        property bool subdued: false
        property bool destructive: false
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: control.text
            color: control.enabled ? (control.destructive ? deck.fault : control.subdued ? deck.textSecondary : deck.primarySurface) : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.secondarySurface : control.down ? (control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.2) : deck.accentMuted) : control.hovered ? (control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.16) : control.subdued ? deck.selected : deck.focus) : control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.08) : control.subdued ? deck.secondarySurface : deck.accent
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : control.destructive ? deck.fault : deck.border
        }
    }

    component DeckField: TextField {
        id: control
        implicitHeight: deck.controlHeight
        selectByMouse: true
        color: deck.textPrimary
        placeholderTextColor: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: 10
        leftPadding: deck.space12
        rightPadding: deck.space12
        onAccepted: focus = false
        background: Rectangle {
            radius: deck.radiusControl
            color: deck.primarySurface
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
                leftPadding: deck.popupRowPadding
                rightPadding: deck.popupRowPadding
                text: control.textAt(index)
                color: deck.textPrimary
                font.family: deck.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: control.highlightedIndex === index ? deck.selected : deck.elevatedSurface
            }
        }
        popup: Popup {
            objectName: control.objectName + "Popup"
            y: control.height - 1
            width: control.width
            padding: deck.popupPadding
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

    component FilterButton: Button {
        id: control
        property string filterValue: "all"
        implicitHeight: 28
        implicitWidth: filterLabel.implicitWidth + deck.space16
        focusPolicy: Qt.StrongFocus
        onClicked: root.profileFilter = filterValue
        contentItem: Text {
            id: filterLabel
            text: control.text
            color: root.profileFilter === control.filterValue ? deck.primarySurface : deck.textSecondary
            font.family: deck.telemetryFont
            font.pixelSize: 8
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: deck.radiusPill
            color: root.profileFilter === control.filterValue ? deck.accent : deck.secondarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component LibraryRow: Button {
        id: row
        property bool selected: false
        property bool category: false
        property string secondary: ""
        implicitHeight: category ? 34 : 30
        Layout.fillWidth: true
        focusPolicy: Qt.StrongFocus
        contentItem: RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: row.category ? deck.space8 : deck.space20
            anchors.rightMargin: deck.space8
            spacing: deck.space8
            Text { text: row.category ? "▾" : "•"; color: row.selected ? deck.accent : deck.textMuted; font.pixelSize: row.category ? 12 : 10; Layout.preferredWidth: 12 }
            Text { text: row.text; color: row.selected ? deck.textPrimary : deck.textSecondary; font.family: row.category ? deck.bodyFont : deck.telemetryFont; font.pixelSize: row.category ? 11 : 9; font.bold: row.category; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { visible: row.secondary.length > 0; text: row.secondary; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; elide: Text.ElideRight }
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: row.down ? deck.accentMuted : row.hovered ? deck.secondarySurface : row.selected ? deck.selected : "transparent"
            border.width: row.activeFocus ? 2 : 1
            border.color: row.activeFocus ? deck.focus : row.selected ? deck.accent : "transparent"
        }
    }

    component CategoryCard: FlightDeckCard {
        id: card
        tokens: deck
        property var category: ({})
        readonly property var categoryProfiles: root.profilesForCategory(String(category.id || ""))
        readonly property bool running: root.categoryHasRunningGame(category)
        objectName: "flightDeckCategoryCard_" + String(category.id || "")
        width: root.width >= 1110 ? (categoryFlow.width - deck.space12) / 2 : categoryFlow.width
        implicitHeight: categoryContent.implicitHeight + contentPadding * 2
        color: category.active ? deck.selected : deck.elevatedSurface
        border.color: category.active ? deck.accent : deck.border

        ColumnLayout {
            id: categoryContent
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        text: String(category.name || "Unnamed category")
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 17
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Text {
                        text: Number(category.profileCount !== undefined ? category.profileCount : categoryProfiles.length) + " profile" + (categoryProfiles.length === 1 ? "" : "s")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                }
                SummaryChip {
                    visible: !!category.active
                    label: "ACTIVE CATEGORY"
                    tone: "healthy"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                SummaryChip {
                    label: (category.executableRules || []).length === 0 ? "MANUAL" : running ? "GAME RUNNING" : "GAME CONFIGURED"
                    tone: running ? "healthy" : (category.executableRules || []).length > 0 ? "informational" : "attention"
                }
                SummaryChip {
                    visible: !category.enabled
                    label: "AUTO OFF"
                    tone: "attention"
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            Text {
                text: (category.executableRules || []).length > 0 ? (category.executableRules || []).map(root.friendlyGameName).join(" · ") : "No game association — activate this category manually."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: deck.divider
            }
            Text {
                text: "WHEN SELECTED  ·  " + root.categoryBehavior(category)
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 8
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: deck.space8
                Repeater {
                    model: categoryProfiles.slice(0, 4)
                    delegate: Rectangle {
                        required property var modelData
                        implicitWidth: Math.min(profileTag.implicitWidth + deck.space16,
                                                card.width - card.contentPadding * 2)
                        implicitHeight: 25
                        radius: deck.radiusPill
                        color: modelData.active ? deck.accentMuted : deck.secondarySurface
                        border.width: 1
                        border.color: modelData.active ? deck.accent : deck.border
                        Text {
                            id: profileTag
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: deck.space8
                            anchors.rightMargin: deck.space8
                            text: String(modelData.name || "Profile") + (modelData.active ? "  ACTIVE" : "")
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                visible: categoryProfiles.length === 0
                text: "No profiles yet. Create one to define this category's controller setup."
                color: deck.textMuted
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    visible: Number(category.adaptiveOverrideAxes || 0) > 0
                    text: Number(category.adaptiveOverrideAxes) + " category Response override" + (Number(category.adaptiveOverrideAxes) === 1 ? "" : "s")
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
                DeckButton {
                    text: "OPEN CATEGORY"
                    subdued: true
                    onClicked: root.openCategory(category.id)
                }
            }
        }
    }

    component ProfileCard: FlightDeckCard {
        id: card
        tokens: deck
        property var profile: ({})
        readonly property bool selectedForEditing: root.view === "profile" && String(root.selectedProfileId) === String(profile.id || "")
        objectName: "flightDeckProfileCard_" + String(profile.id || "")
        width: root.width >= 1160 ? (profileFlow.width - deck.space12) / 2 : profileFlow.width
        implicitHeight: profileContent.implicitHeight + contentPadding * 2
        color: profile.active ? deck.selected : selectedForEditing ? deck.secondarySurface : deck.elevatedSurface
        border.color: profile.active ? deck.accent : selectedForEditing ? deck.focus : deck.border

        ColumnLayout {
            id: profileContent
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space8
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        text: String(profile.name || "Unnamed profile")
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 15
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Text {
                        text: String(profile.categoryName || "General")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                SummaryChip {
                    visible: !!profile.active
                    label: "ACTIVE"
                    tone: "healthy"
                }
                SummaryChip {
                    visible: !profile.active && selectedForEditing
                    label: "VIEWING"
                    tone: "informational"
                }
            }
            Text {
                text: Number(profile.mappedAxes || 0) + " axes configured  ·  " + Number(profile.mappedButtons || 0) + " buttons assigned  ·  " + Number(profile.mappedPovs || 0) + " POV routes"
                color: deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 8
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                SummaryChip {
                    label: Number(profile.adaptiveOverrideAxes || 0) > 0 ? Number(profile.adaptiveOverrideAxes) + " RESPONSE CUSTOM" : "RESPONSE INHERITED"
                    tone: Number(profile.adaptiveOverrideAxes || 0) > 0 ? "informational" : "healthy"
                }
                Text {
                    Layout.fillWidth: true
                    text: Number(profile.automationCount || 0) + " Automation relationship" + (Number(profile.automationCount || 0) === 1 ? "" : "s")
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
            }
            RowLayout {
                Layout.fillWidth: true
                SummaryChip {
                    label: String(profile.automaticSelectionLabel || "Preferred").toUpperCase()
                    tone: String(profile.automaticSelectionMode || "preferred") === "manual-only" ? "attention" : "informational"
                }
                Text {
                    Layout.fillWidth: true
                    text: String(profile.deviceRigName || "No Device Rig") + (profile.deviceRigReady ? "  ·  READY" : "  ·  REVIEW RIG")
                    color: profile.deviceRigReady ? deck.textMuted : deck.statusColor("attention")
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
            }
            RowLayout {
                Layout.fillWidth: true
                DeckButton {
                    text: "OPEN PROFILE"
                    subdued: true
                    onClicked: root.openProfile(profile.id)
                }
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    objectName: "flightDeckProfileActivate_" + String(profile.id || "")
                    text: profile.active ? "ACTIVE NOW" : "ACTIVATE"
                    enabled: !profile.active && !!profile.enabled && !root.usingPresentationFixture
                    onClicked: root.activateProfile(profile.id)
                }
                DeckButton {
                    visible: root.view === "category"
                    text: "↑"
                    subdued: true
                    enabled: !root.usingPresentationFixture && root.profilesForCategory(root.selectedCategoryId).indexOf(profile) > 0
                    onClicked: root.moveAutomaticProfile(profile.id, -1)
                }
                DeckButton {
                    visible: root.view === "category"
                    text: "↓"
                    subdued: true
                    enabled: !root.usingPresentationFixture && root.profilesForCategory(root.selectedCategoryId).indexOf(profile) < root.profilesForCategory(root.selectedCategoryId).length - 1
                    onClicked: root.moveAutomaticProfile(profile.id, 1)
                }
            }
        }
    }

    ColumnLayout {
        id: profileContent
        x: deck.space4
        width: root.width - deck.space8
        spacing: deck.space16

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space8
            DeckButton {
                visible: root.view !== "library"
                text: "← CONFIGURATIONS"
                subdued: true
                onClicked: root.returnToLibrary()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.view === "library" ? "Browse categories and profiles without changing what is active." : root.view === "category" ? "Game association, category behavior, and contained profiles" : "Viewing this profile does not activate it."
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            DeckButton {
                objectName: "flightDeckProfilesTransfer"
                visible: root.view === "library"
                text: "IMPORT / EXPORT"
                subdued: true
                onClicked: transferDialog.openTransfer("import", "profile", "", "")
            }
            DeckButton {
                objectName: "flightDeckNewCategory"
                visible: root.view === "library"
                text: "+ CATEGORY"
                subdued: true
                enabled: !root.usingPresentationFixture
                onClicked: newCategoryDialog.open()
            }
            DeckButton {
                visible: root.view === "library"
                text: "+ PROFILE"
                enabled: !root.usingPresentationFixture && root.categories.length > 0
                onClicked: {
                    newProfileDialog.categoryId = backend.activeCategoryId;
                    newProfileDialog.open();
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.actionNotice.length > 0
            implicitHeight: noticeText.implicitHeight + deck.space16
            radius: deck.radiusControl
            color: Qt.rgba(deck.statusColor(root.actionNoticeTone).r,
                deck.statusColor(root.actionNoticeTone).g,
                deck.statusColor(root.actionNoticeTone).b, deck.light ? 0.10 : 0.16)
            border.color: deck.statusColor(root.actionNoticeTone)
            Text {
                id: noticeText
                anchors.fill: parent
                anchors.margins: deck.space8
                text: root.actionNotice
                color: deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.actionNotice = ""
            }
        }

        Item {
            id: compactProfileLibrary
            Layout.fillWidth: true
            visible: root.view === "library"
            Layout.preferredHeight: visible ? libraryMasterDetail.implicitHeight : 0
            RowLayout {
                id: libraryMasterDetail
                width: parent.width
                spacing: deck.space16

                FlightDeckCard {
                    id: libraryPane
                    objectName: "flightDeckProfileLibrary"
                    tokens: deck
                    Layout.preferredWidth: root.width >= 960 ? Math.max(290, Math.min(380, root.width * 0.36)) : root.width
                    Layout.maximumWidth: root.width >= 960 ? Math.max(290, Math.min(380, root.width * 0.36)) : root.width
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: libraryPaneContent.implicitHeight + contentPadding * 2
                    color: deck.elevatedSurface
                    ColumnLayout {
                        id: libraryPaneContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: "CONFIGURATION LIBRARY"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 15; font.bold: true }
                                Text { text: "Categories contain profiles"; color: deck.textMuted; font.pixelSize: 9 }
                            }
                            DeckButton { objectName: "flightDeckNewProfile"; text: "+ PROFILE"; enabled: !root.usingPresentationFixture && root.categories.length > 0; onClicked: { newProfileDialog.categoryId = root.selectedCategoryId || backend.activeCategoryId; newProfileDialog.open(); } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FilterButton { text: "ALL"; filterValue: "all" }
                            FilterButton { text: "ACTIVE"; filterValue: "active" }
                            FilterButton { text: "GAME"; filterValue: "associated" }
                        }
                        DeckField { objectName: "flightDeckProfileSearch"; Layout.fillWidth: true; placeholderText: "Search categories and profiles…"; text: root.searchText; onTextEdited: root.searchText = text }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                        Repeater {
                            model: root.categories
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                LibraryRow {
                                    objectName: "flightDeckCategoryCard_" + String(modelData.id || "")
                                    category: true
                                    text: String(modelData.name || "Unnamed category")
                                    secondary: Number(modelData.profileCount || root.profilesForCategory(modelData.id).length) + " profiles"
                                    selected: root.selectedCategoryId === String(modelData.id || "") && !root.selectedProfileId.length
                                    onClicked: root.selectLibraryCategory(modelData.id)
                                }
                                Repeater {
                                    model: root.profilesForCategory(modelData.id)
                                    delegate: LibraryRow {
                                        required property var modelData
                                        objectName: "flightDeckProfileCard_" + String(modelData.id || "")
                                        visible: root.profileMatchesFilter(modelData)
                                        text: String(modelData.name || "Profile")
                                        secondary: modelData.active ? "ACTIVE" : Number(modelData.mappedAxes || 0) + " axes"
                                        selected: root.selectedProfileId === String(modelData.id || "")
                                        onClicked: root.selectLibraryProfile(modelData.id)
                                    }
                                }
                            }
                        }
                        Text { visible: root.categories.length === 0; text: "Create a category, then add a profile to define a controller setup."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckButton { objectName: "flightDeckNewCategory"; text: "+ CATEGORY"; subdued: true; enabled: !root.usingPresentationFixture; onClicked: newCategoryDialog.open() }
                            DeckButton { objectName: "flightDeckProfilesTransfer"; text: "IMPORT / EXPORT"; subdued: true; onClicked: transferDialog.openTransfer("import", "profile", "", "") }
                        }
                    }
                }

                FlightDeckCard {
                    id: detailPane
                    objectName: "flightDeckProfileDetailPane"
                    tokens: deck
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: detailPaneContent.implicitHeight + contentPadding * 2
                    color: deck.secondarySurface
                    ColumnLayout {
                        id: detailPaneContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space12
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: root.selectedProfileId.length ? String(root.selectedDetail.name || "Profile") : String((root.selectedCategory || {}).name || "Select a profile"); color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: root.selectedProfileId.length ? String(root.selectedDetail.category || "General") + " · " + (root.selectedDetail.active ? "ACTIVE AT RUNTIME" : "VIEWING ONLY") : "Choose a profile in the library to review its configuration."; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                            SummaryChip { visible: root.selectedProfileId.length && root.selectedDetail.active; label: "ACTIVE NOW"; tone: "healthy" }
                        }
                        Text { visible: root.selectedProfileId.length; text: root.selectedDetail.active ? "This is the profile currently used by HOTAS BF6." : "Viewing a profile never activates it."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        GridLayout {
                            visible: root.selectedProfileId.length
                            Layout.fillWidth: true
                            columns: root.width >= 1180 ? 2 : 1
                            rowSpacing: deck.space8
                            columnSpacing: deck.space8
                            Repeater {
                                model: [
                                    { label: "AXES", value: Number(root.selectedDetail.mappedAxes || 0) + " configured" },
                                    { label: "BUTTONS & HATS", value: Number(root.selectedDetail.mappedButtons || 0) + " buttons · " + Number(root.selectedDetail.mappedPovs || 0) + " POV" },
                                    { label: "ADAPTIVE RESPONSE", value: Number(root.selectedDetail.adaptiveProfileOverrideAxes || 0) > 0 ? "Custom response" : "Inherited response" },
                                    { label: "AUTOMATION", value: Number(root.selectedDetail.automationCount || 0) + " relationship" + (Number(root.selectedDetail.automationCount || 0) === 1 ? "" : "s") }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 58
                                    radius: deck.radiusControl
                                    color: deck.primarySurface
                                    border.color: deck.border
                                    Column { anchors.fill: parent; anchors.margins: deck.space8; spacing: 2
                                        Text { text: modelData.label; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                        Text { text: modelData.value; color: deck.textPrimary; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; width: parent.width }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: root.selectedProfileId.length
                            Layout.fillWidth: true
                            DeckButton { text: root.selectedDetail.active ? "ACTIVE NOW" : "ACTIVATE"; enabled: !root.selectedDetail.active && !!root.selectedDetail.enabled && !root.usingPresentationFixture; onClicked: root.activateProfile(root.selectedProfileId) }
                            DeckButton { text: "OPEN DETAILS"; subdued: true; onClicked: root.openProfile(root.selectedProfileId) }
                            Item { Layout.fillWidth: true }
                            DeckButton { text: "OPEN RESPONSE"; subdued: true; onClicked: root.openAdaptiveForSelectedProfile() }
                        }
                        RowLayout {
                            visible: !root.selectedProfileId.length && root.selectedCategoryId.length
                            Layout.fillWidth: true
                            DeckButton {
                                objectName: "flightDeckCategoryOpenDetails"
                                text: "OPEN DETAILS"
                                subdued: true
                                onClicked: root.openCategory(root.selectedCategoryId)
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Text { visible: !root.selectedProfileId.length && root.selectedCategoryId.length; text: String((root.selectedCategory || {}).profileCount || 0) + " profiles · " + root.categoryBehavior(root.selectedCategory); color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: false // Superseded by the compact hierarchical library above.
            Layout.preferredHeight: visible ? libraryColumn.implicitHeight : 0
            ColumnLayout {
                id: libraryColumn
                width: parent.width
                spacing: deck.space16

                FlightDeckCard {
                    id: activeHero
                    objectName: "flightDeckActiveProfileHero"
                    tokens: deck
                    Layout.fillWidth: true
                    implicitHeight: activeHeroContent.implicitHeight + deck.space32
                    color: deck.selected
                    border.color: deck.accent
                    ColumnLayout {
                        id: activeHeroContent
                        anchors.fill: parent
                        anchors.margins: deck.space16
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "ACTIVE NOW"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SummaryChip {
                                label: "RUNTIME ACTIVE"
                                tone: "healthy"
                            }
                        }
                        Text {
                            text: backend.activeProfileDisplayName || backend.activeProfileName || "No active profile"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 23
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "CATEGORY  ·  " + (backend.activeCategoryName || "General")
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 15
                                color: deck.divider
                            }
                            Text {
                                text: "SOURCE  ·  " + (backend.profileSourceLabel || "Manual base profile")
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            DeckButton {
                                text: "VIEW ACTIVE"
                                subdued: true
                                onClicked: root.openProfile(backend.activeProfileId)
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "YOUR CONFIGURATIONS"
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    FilterButton {
                        text: "ALL"
                        filterValue: "all"
                    }
                    FilterButton {
                        text: "ACTIVE"
                        filterValue: "active"
                    }
                    FilterButton {
                        text: "GAME LINKED"
                        filterValue: "associated"
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    DeckField {
                        objectName: "flightDeckProfileSearch"
                        Layout.preferredWidth: root.width >= 900 ? 260 : 180
                        placeholderText: "Search profiles…"
                        text: root.searchText
                        onTextEdited: root.searchText = text
                    }
                }
                Text {
                    visible: root.categories.length === 0
                    text: "No categories yet. Create a category first, then add a profile to define a controller setup."
                    color: deck.textSecondary
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Flow {
                    id: categoryFlow
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Repeater {
                        model: root.categories
                        delegate: CategoryCard {
                            required property var modelData
                            category: modelData
                        }
                    }
                }
                SectionLabel {
                    label: "PROFILES"
                }
                Text {
                    visible: root.profiles.length === 0
                    text: "No profiles are available. A profile stores the controller mappings for a category."
                    color: deck.textSecondary
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: root.profiles.length > 0 && root.profiles.filter(root.profileMatchesFilter).length === 0
                    text: "No profiles match this filter. Filtering changes only this view."
                    color: deck.textMuted
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }
                Flow {
                    id: profileFlow
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Repeater {
                        model: root.profiles
                        delegate: ProfileCard {
                            required property var modelData
                            visible: root.profileMatchesFilter(modelData)
                            profile: modelData
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: root.view === "category"
            Layout.preferredHeight: visible ? categoryColumn.implicitHeight : 0
            ColumnLayout {
                id: categoryColumn
                width: parent.width
                spacing: deck.space16

                FlightDeckCard {
                    tokens: deck
                    contentPadding: deck.cardPadding
                    Layout.fillWidth: true
                    implicitHeight: categoryHeroContent.implicitHeight + contentPadding * 2
                    color: deck.selected
                    border.color: deck.accent
                    ColumnLayout {
                        id: categoryHeroContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: String((root.selectedCategory || {}).name || "Category")
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: 20
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: Number((root.selectedCategory || {}).profileCount || 0) + " profiles  ·  " + root.categoryBehavior(root.selectedCategory)
                                    color: deck.textSecondary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 9
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                            SummaryChip {
                                visible: !!(root.selectedCategory || {}).active
                                label: "ACTIVE CATEGORY"
                                tone: "healthy"
                            }
                            SummaryChip {
                                visible: !(root.selectedCategory || {}).active
                                label: "VIEWING"
                                tone: "informational"
                            }
                        }
                        Text {
                            text: "Viewing a category never activates it. Use ‘Activate category now’ only when you deliberately want its configured behavior to choose a profile."
                            color: deck.textMuted
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckButton {
                                text: "ACTIVATE CATEGORY NOW"
                                enabled: !(root.selectedCategory || {}).active && !!(root.selectedCategory || {}).enabled && !root.usingPresentationFixture
                                onClicked: backend.activateProfileCategory(root.selectedCategoryId)
                            }
                            DeckButton {
                                text: "RENAME"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: {
                                    renameCategoryDialog.categoryId = root.selectedCategoryId;
                                    renameCategoryDialog.name = root.selectedCategory.name;
                                    renameCategoryDialog.open();
                                }
                            }
                            DeckButton {
                                text: "EXPORT"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: transferDialog.openTransfer("export", "category", "", root.selectedCategoryId)
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            DeckButton {
                                text: "+ PROFILE"
                                enabled: !root.usingPresentationFixture
                                onClicked: {
                                    newProfileDialog.categoryId = root.selectedCategoryId;
                                    newProfileDialog.open();
                                }
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "GAME ASSOCIATIONS"
                }
                FlightDeckCard {
                    tokens: deck
                    contentPadding: deck.cardPaddingCompact
                    Layout.fillWidth: true
                    implicitHeight: gameAssociationContent.implicitHeight + contentPadding * 2
                    ColumnLayout {
                        id: gameAssociationContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        Text {
                            text: backend.automaticGameDetection ? "When a configured game runs, HOTAS BF6 selects this category." : "Game detection is currently disabled globally; these associations are retained but will not select a category."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: (root.selectedCategory || {}).executableRules || []
                            delegate: Rectangle {
                                required property string modelData
                                readonly property var application: root.runningApplicationFor(modelData)
                                Layout.fillWidth: true
                                implicitHeight: gameRow.implicitHeight + deck.space16
                                radius: deck.radiusControl
                                color: deck.secondarySurface
                                border.color: application ? deck.healthy : deck.border
                                RowLayout {
                                    id: gameRow
                                    anchors.fill: parent
                                    anchors.margins: deck.space8
                                    spacing: deck.space8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Text {
                                            text: root.friendlyGameName(modelData)
                                            color: deck.textPrimary
                                            font.pixelSize: 11
                                            font.bold: true
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: modelData + "  ·  " + (application ? "RUNNING" : "CONFIGURED — NOT RUNNING")
                                            color: application ? deck.healthy : deck.textMuted
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 8
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                    }
                                    DeckButton {
                                        text: "REMOVE"
                                        subdued: true
                                        enabled: !root.usingPresentationFixture
                                        onClicked: root.removeGameRule(root.selectedCategoryId, modelData)
                                    }
                                }
                            }
                        }
                        Text {
                            visible: ((root.selectedCategory || {}).executableRules || []).length === 0
                            text: "No games are associated. This category remains available for manual activation."
                            color: deck.textMuted
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckButton {
                                text: backend.automaticGameDetection ? "DETECTION ON" : "DETECTION OFF"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: backend.setAutomaticGameDetection(!backend.automaticGameDetection)
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            DeckButton {
                                text: "+ ADD GAME"
                                enabled: !root.usingPresentationFixture
                                onClicked: {
                                    addGameDialog.categoryId = root.selectedCategoryId;
                                    addGameDialog.open();
                                }
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "AUTOMATIC ACTIVATION"
                }
                FlightDeckCard {
                    objectName: "flightDeckCategoryActivationResolver"
                    tokens: deck
                    Layout.fillWidth: true
                    implicitHeight: behaviorContent.implicitHeight + deck.space24
                    ColumnLayout {
                        id: behaviorContent
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        Text {
                            text: "The resolver considers this category's ordered profiles: valid Preferred configurations first, then valid Fallback configurations. Manual Only never switches automatically. A valid active configuration remains stable until it becomes unavailable."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SummaryChip {
                                label: String(root.selectedCategoryActivation.reason || "checking").replace(/-/g, " ").toUpperCase()
                                tone: root.selectedCategoryActivation.valid ? "healthy" : "attention"
                            }
                            SummaryChip {
                                visible: !!root.selectedCategoryActivation.manualOverride
                                label: "MANUAL OVERRIDE"
                                tone: "attention"
                            }
                            Item { Layout.fillWidth: true }
                            DeckButton {
                                objectName: "flightDeckResumeAutomaticActivation"
                                visible: !!root.selectedCategoryActivation.manualOverride
                                text: "RESUME AUTOMATIC"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: backend.resumeAutomaticActivation()
                            }
                            DeckButton {
                                objectName: "flightDeckSwitchRecommendedConfiguration"
                                visible: !!root.selectedCategoryActivation.higherPreferenceAvailable
                                text: "SWITCH NOW"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: backend.activateRecommendedConfiguration(root.selectedCategoryId)
                            }
                        }
                        Text {
                            text: String(root.selectedCategoryActivation.explanation || "Checking automatic configuration.")
                            color: deck.textPrimary
                            font.pixelSize: 11
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            visible: !!root.selectedCategoryActivation.valid
                            text: String(root.selectedCategoryActivation.profileName || "Profile") + "  →  " + String(root.selectedCategoryActivation.deviceRigName || "Device Rig") + "  →  " + String(root.selectedCategoryActivation.outputLayoutName || "Virtual Output")
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: root.selectedCategoryActivation.candidates || []
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.selected ? "● " : modelData.current ? "○ " : "· ")
                                        + String(modelData.profileName || "Profile") + " → "
                                        + String(modelData.deviceRigName || "Device Rig") + " → "
                                        + String(modelData.outputLayoutName || modelData.outputLayoutId || "Output") + "  ["
                                        + String(modelData.mode || "preferred").toUpperCase() + "]"
                                    color: modelData.selected ? deck.accent : modelData.eligible ? deck.textSecondary : deck.textMuted
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: modelData.blockers || []
                                    delegate: Text {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: "  blocked: " + String(modelData)
                                        color: deck.statusColor("attention")
                                        font.pixelSize: 8
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                Repeater {
                                    model: modelData.warnings || []
                                    delegate: Text {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: "  notice: " + String(modelData)
                                        color: deck.textMuted
                                        font.pixelSize: 8
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: root.selectedCategoryActivation.blockers || []
                            delegate: Text {
                                required property var modelData
                                text: "• " + String(modelData)
                                color: deck.statusColor("attention")
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "CONFIGURATION ORDER"
                }
                Text {
                    visible: root.profilesForCategory(root.selectedCategoryId).length === 0
                    text: "No profiles are in this category yet."
                    color: deck.textSecondary
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }
                Flow {
                    id: categoryProfileFlow
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Repeater {
                        model: root.profilesForCategory(root.selectedCategoryId)
                        delegate: ProfileCard {
                            required property var modelData
                            profile: modelData
                        }
                    }
                }
                SectionLabel {
                    label: "ADVANCED"
                }
                RowLayout {
                    Layout.fillWidth: true
                    DeckButton {
                        text: (root.selectedCategory || {}).enabled ? "DISABLE AUTO ACTIVATION" : "ENABLE AUTO ACTIVATION"
                        subdued: true
                        enabled: !(root.selectedCategory || {}).active && !root.usingPresentationFixture
                        onClicked: backend.setProfileCategoryEnabled(root.selectedCategoryId, !(root.selectedCategory || {}).enabled)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    DeckButton {
                        text: "DELETE EMPTY CATEGORY"
                        destructive: true
                        enabled: Number((root.selectedCategory || {}).profileCount || 0) === 0 && !(root.selectedCategory || {}).active && !root.usingPresentationFixture
                        onClicked: {
                            deleteCategoryDialog.categoryId = root.selectedCategoryId;
                            deleteCategoryDialog.name = root.selectedCategory.name;
                            deleteCategoryDialog.open();
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            visible: root.view === "profile"
            Layout.preferredHeight: visible ? profileDetailColumn.implicitHeight : 0
            ColumnLayout {
                id: profileDetailColumn
                width: parent.width
                spacing: deck.space16

                FlightDeckCard {
                    tokens: deck
                    contentPadding: deck.cardPadding
                    Layout.fillWidth: true
                    implicitHeight: profileHeroContent.implicitHeight + contentPadding * 2
                    color: root.selectedDetail.active ? deck.selected : deck.elevatedSurface
                    border.color: root.selectedDetail.active ? deck.accent : deck.focus
                    ColumnLayout {
                        id: profileHeroContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: String(root.selectedDetail.name || "Profile")
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: 21
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: String(root.selectedDetail.category || "General") + "  /  " + String(root.selectedDetail.name || "Profile")
                                    color: deck.textMuted
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 9
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                            SummaryChip {
                                label: root.selectedDetail.active ? "ACTIVE AT RUNTIME" : "VIEWING FOR EDITING"
                                tone: root.selectedDetail.active ? "healthy" : "informational"
                            }
                        }
                        Text {
                            text: root.selectedDetail.active ? "This is the base profile currently selected by HOTAS BF6." : "This profile is selected for editing only. It is not active until you explicitly activate it."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckButton {
                                objectName: "flightDeckSelectedProfileActivate"
                                text: root.selectedDetail.active ? "ACTIVE NOW" : "ACTIVATE PROFILE"
                                enabled: !root.selectedDetail.active && !!root.selectedDetail.enabled && !root.usingPresentationFixture
                                onClicked: root.activateProfile(root.selectedProfileId)
                            }
                            DeckButton {
                                text: "OPEN CATEGORY"
                                subdued: true
                                onClicked: root.openCategory(root.selectedDetail.categoryId)
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SummaryChip {
                                visible: !root.selectedDetail.enabled
                                label: "DISABLED"
                                tone: "attention"
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "DEVICE RIG & AUTOMATIC ACTIVATION"
                }
                FlightDeckCard {
                    tokens: deck
                    Layout.fillWidth: true
                    implicitHeight: activationProfileContent.implicitHeight + deck.space24
                    ColumnLayout {
                        id: activationProfileContent
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        Text {
                            text: "Device Rig"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                        }
                        DeckCombo {
                            id: profileRigSelector
                            objectName: "flightDeckProfileRigSelector"
                            Layout.fillWidth: true
                            model: backend.deviceRigs
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: {
                                const rigs = backend.deviceRigs || [];
                                for (let index = 0; index < rigs.length; ++index) {
                                    if (String(rigs[index].id || "") === String(root.selectedDetail.deviceRigId || ""))
                                        return index;
                                }
                                return -1;
                            }
                            enabled: !root.usingPresentationFixture && (backend.deviceRigs || []).length > 0
                            onActivated: backend.assignProfileDeviceRig(root.selectedProfileId, currentValue)
                        }
                        Text {
                            text: String(root.selectedDetail.deviceRigName || "Device Rig assignment required") + "  ·  " + (root.selectedDetail.deviceRigReady ? "ready for automatic selection" : "requires a complete, verified rig before automatic selection")
                            color: root.selectedDetail.deviceRigReady ? deck.textSecondary : deck.statusColor("attention")
                            font.pixelSize: 9
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            text: "Automatic policy"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                        }
                        DeckCombo {
                            id: profileAutomaticPolicySelector
                            objectName: "flightDeckProfileAutomaticPolicySelector"
                            Layout.fillWidth: true
                            model: [
                                { name: "Preferred — first safe choice", value: "preferred" },
                                { name: "Fallback — used only when no Preferred choice is safe", value: "fallback" },
                                { name: "Manual Only — never selected automatically", value: "manual-only" }
                            ]
                            textRole: "name"
                            valueRole: "value"
                            currentIndex: {
                                const policy = String(root.selectedDetail.automaticSelectionMode || "preferred");
                                return policy === "fallback" ? 1 : policy === "manual-only" ? 2 : 0;
                            }
                            enabled: !root.usingPresentationFixture
                            onActivated: backend.setProfileAutomaticSelectionMode(root.selectedProfileId, currentValue)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "Automatic order follows the Configuration Order on this profile's category page."
                                color: deck.textMuted
                                font.pixelSize: 9
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "flightDeckOpenAssignedRig"
                                text: root.selectedDetail.deviceRigId ? "OPEN RIG" : "OPEN DEVICES"
                                subdued: true
                                onClicked: {
                                    if (root.selectedDetail.deviceRigId)
                                        root.navigateToDeviceRig(String(root.selectedDetail.deviceRigId))
                                    else
                                        root.navigateToPage(2)
                                }
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "WHAT THIS PROFILE CONFIGURES"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 980 ? 2 : 1
                    rowSpacing: deck.space12
                    columnSpacing: deck.space12
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: axesSummary.implicitHeight + deck.space24
                        ColumnLayout {
                            id: axesSummary
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "AXES"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: Number(root.selectedDetail.mappedAxes || 0) + " configured"
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                            Text {
                                text: root.selectedDetail.active ? "Open the native Axes workspace for this active profile." : "The current Axes editor is active-profile scoped. Viewing this profile never activates it."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "flightDeckConfigureAxes"
                                text: root.selectedDetail.active ? "CONFIGURE AXES" : "ACTIVE PROFILE REQUIRED"
                                subdued: true
                                enabled: !!root.selectedDetail.active
                                onClicked: root.openActiveProfileEditor(0)
                            }
                        }
                    }
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: buttonsSummary.implicitHeight + deck.space24
                        ColumnLayout {
                            id: buttonsSummary
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "BUTTONS & HATS"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: Number(root.selectedDetail.mappedButtons || 0) + " buttons assigned  ·  " + Number(root.selectedDetail.mappedPovs || 0) + " POV routes"
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 16
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                text: root.selectedDetail.active ? "Open the native Buttons workspace for this active profile." : "The current Buttons editor is active-profile scoped. No activation is performed here."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "flightDeckConfigureButtons"
                                text: root.selectedDetail.active ? "CONFIGURE BUTTONS" : "ACTIVE PROFILE REQUIRED"
                                subdued: true
                                enabled: !!root.selectedDetail.active
                                onClicked: root.openActiveProfileEditor(1)
                            }
                        }
                    }
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: adaptiveSummary.implicitHeight + deck.space24
                        ColumnLayout {
                            id: adaptiveSummary
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "ADAPTIVE RESPONSE"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: Number(root.selectedDetail.adaptiveProfileOverrideAxes || 0) > 0 ? Number(root.selectedDetail.adaptiveProfileOverrideAxes) + " custom profile override" + (Number(root.selectedDetail.adaptiveProfileOverrideAxes) === 1 ? "" : "s") : "No profile-specific overrides"
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 16
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                text: String(root.selectedDetail.adaptiveSource || "Inherited response defaults")
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "flightDeckConfigureAdaptive"
                                text: "CONFIGURE ADAPTIVE"
                                subdued: true
                                onClicked: root.openAdaptiveForSelectedProfile()
                            }
                        }
                    }
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: automationSummary.implicitHeight + deck.space24
                        ColumnLayout {
                            id: automationSummary
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "AUTOMATION"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: Number(root.selectedDetail.automationCount || 0) + " related rule" + (Number(root.selectedDetail.automationCount || 0) === 1 ? "" : "s")
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 16
                                font.bold: true
                            }
                            Text {
                                text: (root.selectedDetail.automations || []).length > 0 ? (root.selectedDetail.automations || []).map(function (rule) {
                                    return rule.name || "Rule";
                                }).join(" · ") : "No Automation rule currently references this profile."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "flightDeckOpenAutomation"
                                text: "OPEN AUTOMATION"
                                subdued: true
                                onClicked: root.openAutomationForSelectedProfile()
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "EFFECTIVE CONFIGURATION"
                }
                FlightDeckCard {
                    tokens: deck
                    Layout.fillWidth: true
                    implicitHeight: inheritanceContent.implicitHeight + deck.space24
                    ColumnLayout {
                        id: inheritanceContent
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            SummaryChip {
                                label: "MAPPINGS ARE PROFILE-SPECIFIC"
                                tone: "informational"
                            }
                            SummaryChip {
                                label: root.selectedDetail.curveTransitionSmoothingOverride ? "CURVE TRANSITION CUSTOM" : "CURVE TRANSITION INHERITED"
                                tone: root.selectedDetail.curveTransitionSmoothingOverride ? "informational" : "healthy"
                            }
                        }
                        Text {
                            text: "Axes, buttons, and hats are stored with this profile. Adaptive Response can inherit Global → Category defaults until this profile defines an override."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            text: "Adaptive Response: " + String(root.selectedDetail.adaptiveSource || "Built-in response defaults") + "  ·  Curve transfer: " + (root.selectedDetail.curveTransitionSmoothingOverride ? "Custom for this profile" : "Inherited from global settings")
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                SectionLabel {
                    label: "GAME & RELATIONSHIPS"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 980 ? 2 : 1
                    rowSpacing: deck.space12
                    columnSpacing: deck.space12
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: gameProfileContent.implicitHeight + deck.space24
                        ColumnLayout {
                            id: gameProfileContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "CATEGORY & GAMES"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: String(root.selectedDetail.category || "General")
                                color: deck.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: (root.selectedDetail.categoryGames || []).length > 0 ? (root.selectedDetail.categoryGames || []).map(root.friendlyGameName).join(" · ") : "Manual category — no games linked"
                                color: deck.textSecondary
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                text: String(root.selectedDetail.categoryActivationBehavior || "Category behavior unavailable")
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    FlightDeckCard {
                        tokens: deck
                        Layout.fillWidth: true
                        implicitHeight: relationshipContent.implicitHeight + deck.space24
                        ColumnLayout {
                            id: relationshipContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "PROFILE REFERENCES"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Repeater {
                                model: ((root.selectedDetail.relationships || {}).referencedBy || [])
                                delegate: Text {
                                    required property var modelData
                                    text: "Used by  ·  " + String(modelData.profile || "Profile control") + "  ·  " + String(modelData.via || "Reference")
                                    color: deck.textSecondary
                                    font.pixelSize: 10
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Repeater {
                                model: ((root.selectedDetail.relationships || {}).references || [])
                                delegate: Text {
                                    required property var modelData
                                    text: "References  ·  " + String(modelData.profile || "Profile") + "  ·  " + String(modelData.via || "Reference")
                                    color: deck.textSecondary
                                    font.pixelSize: 10
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Text {
                                visible: ((root.selectedDetail.relationships || {}).referencedBy || []).length === 0 && ((root.selectedDetail.relationships || {}).references || []).length === 0
                                text: "No profile-control references were found."
                                color: deck.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                SectionLabel {
                    label: "PROFILE ACTIONS"
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    DeckButton {
                        text: "RENAME"
                        subdued: true
                        enabled: !root.usingPresentationFixture && !root.selectedDetail.protected
                        onClicked: {
                            renameProfileDialog.profileId = root.selectedProfileId;
                            renameProfileDialog.name = root.selectedDetail.name;
                            renameProfileDialog.open();
                        }
                    }
                    DeckButton {
                        text: "DUPLICATE"
                        subdued: true
                        enabled: !root.usingPresentationFixture
                        onClicked: {
                            duplicateProfileDialog.profileId = root.selectedProfileId;
                            duplicateProfileDialog.name = root.selectedDetail.name + " Copy";
                            duplicateProfileDialog.categoryId = root.selectedDetail.categoryId;
                            duplicateProfileDialog.open();
                        }
                    }
                    DeckButton {
                        text: "EXPORT"
                        subdued: true
                        enabled: !root.usingPresentationFixture
                        onClicked: transferDialog.openTransfer("export", "profile", root.selectedProfileId, "")
                    }
                    DeckButton {
                        text: "MOVE CATEGORY"
                        subdued: true
                        enabled: !root.usingPresentationFixture
                        onClicked: {
                            moveProfileDialog.profileId = root.selectedProfileId;
                            moveProfileDialog.categoryId = root.selectedDetail.categoryId;
                            moveProfileDialog.open();
                        }
                    }
                    DeckButton {
                        text: root.selectedDetail.enabled ? "DISABLE" : "ENABLE"
                        subdued: true
                        enabled: !root.selectedDetail.active && !root.usingPresentationFixture
                        onClicked: backend.setProfileEnabled(root.selectedProfileId, !root.selectedDetail.enabled)
                    }
                    DeckButton {
                        objectName: "flightDeckProfileDelete"
                        text: "DELETE PROFILE"
                        destructive: true
                        enabled: !root.selectedDetail.active && !root.selectedDetail.protected && !root.usingPresentationFixture
                        onClicked: {
                            deleteProfileDialog.profileId = root.selectedProfileId;
                            deleteProfileDialog.name = root.selectedDetail.displayName || root.selectedDetail.name;
                            deleteProfileDialog.open();
                        }
                    }
                }
            }
        }
        Item {
            Layout.preferredHeight: deck.space12
        }
    }

    FlightDeckTransferDialog {
        id: transferDialog
        deckTokens: deck
        backendObject: backend
        categories: root.categories
        profiles: root.profiles
        presentationFixture: root.usingPresentationFixture
        onCompleted: function(message) {
            root.actionNotice = message;
            root.actionNoticeTone = "healthy";
        }
    }

    component DeckDialog: FlightDeckDialog {
        id: dialog
        tokens: deck
        preferredWidth: 480
    }

    DeckDialog {
        id: newCategoryDialog
        objectName: "flightDeckNewCategoryDialog"
        property string errorMessage: ""
        heading: "New category"
        contentItem: ColumnLayout {
            width: newCategoryDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "CATEGORY NAME"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckField {
                id: newCategoryName
                objectName: "flightDeckNewCategoryName"
                Layout.fillWidth: true
                placeholderText: "Battlefield"
                onTextEdited: newCategoryDialog.errorMessage = ""
            }
            Text {
                objectName: "flightDeckNewCategoryError"
                visible: newCategoryDialog.errorMessage.length > 0
                Layout.fillWidth: true
                text: newCategoryDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }
            Text {
                text: "Categories group profiles and can optionally be selected when a configured game is running."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: newCategoryDialog.close()
                }
                DeckButton {
                    objectName: "flightDeckNewCategorySave"
                    text: "CREATE CATEGORY"
                    onClicked: {
                        if (backend.createProfileCategory(newCategoryName.text))
                            newCategoryDialog.close();
                        else
                            newCategoryDialog.errorMessage = "Choose a unique category name.";
                    }
                }
            }
        }
        onOpened: {
            newCategoryName.text = "";
            newCategoryDialog.errorMessage = "";
            newCategoryName.forceActiveFocus();
        }
    }

    DeckDialog {
        id: newProfileDialog
        property string categoryId: ""
        property string errorMessage: ""
        heading: "New profile"
        contentItem: ColumnLayout {
            width: newProfileDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "PROFILE NAME"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckField {
                id: newProfileName
                Layout.fillWidth: true
                placeholderText: "Helicopter Precision"
                onTextEdited: newProfileDialog.errorMessage = ""
            }
            Text {
                visible: newProfileDialog.errorMessage.length > 0
                Layout.fillWidth: true
                text: newProfileDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }
            Text {
                text: "CATEGORY"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckCombo {
                id: newProfileCategory
                Layout.fillWidth: true
                model: root.categories
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    for (let index = 0; index < root.categories.length; ++index) {
                        if (String(root.categories[index].id) === String(newProfileDialog.categoryId))
                            return index;
                    }
                    return 0;
                }
            }
            Text {
                text: "START FROM"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckCombo {
                id: newProfileSource
                Layout.fillWidth: true
                model: root.profiles
                textRole: "displayName"
                valueRole: "id"
                currentIndex: backend.activeProfileIndex
            }
            Text {
                text: "A new profile copies the selected existing profile. Creating it does not activate it."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: newProfileDialog.close()
                }
                DeckButton {
                    text: "CREATE PROFILE"
                    enabled: newProfileName.text.trim().length > 0 && root.categories.length > 0
                    onClicked: {
                        if (backend.createProfileInCategory(newProfileName.text, newProfileCategory.currentValue, newProfileSource.currentValue))
                            newProfileDialog.close();
                        else
                            newProfileDialog.errorMessage = "Choose a unique profile name and a valid destination category.";
                    }
                }
            }
        }
        onOpened: {
            newProfileName.text = "";
            newProfileDialog.errorMessage = "";
            newProfileName.forceActiveFocus();
        }
    }

    DeckDialog {
        id: renameCategoryDialog
        property string categoryId: ""
        property string name: ""
        property string errorMessage: ""
        heading: "Rename category"
        contentItem: ColumnLayout {
            width: renameCategoryDialog.availableWidth
            spacing: deck.space12
            DeckField {
                id: renameCategoryName
                Layout.fillWidth: true
                onTextEdited: renameCategoryDialog.errorMessage = ""
            }
            Text {
                visible: renameCategoryDialog.errorMessage.length > 0
                Layout.fillWidth: true
                text: renameCategoryDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: renameCategoryDialog.close()
                }
                DeckButton {
                    text: "RENAME"
                    enabled: renameCategoryName.text.trim().length > 0
                    onClicked: {
                        if (backend.renameProfileCategory(renameCategoryDialog.categoryId, renameCategoryName.text))
                            renameCategoryDialog.close();
                        else
                            renameCategoryDialog.errorMessage = "Choose a unique category name.";
                    }
                }
            }
        }
        onOpened: {
            renameCategoryName.text = name;
            renameCategoryDialog.errorMessage = "";
            renameCategoryName.forceActiveFocus();
        }
    }

    DeckDialog {
        id: renameProfileDialog
        property string profileId: ""
        property string name: ""
        property string errorMessage: ""
        heading: "Rename profile"
        contentItem: ColumnLayout {
            width: renameProfileDialog.availableWidth
            spacing: deck.space12
            DeckField {
                id: renameProfileName
                Layout.fillWidth: true
                onTextEdited: renameProfileDialog.errorMessage = ""
            }
            Text {
                visible: renameProfileDialog.errorMessage.length > 0
                Layout.fillWidth: true
                text: renameProfileDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: renameProfileDialog.close()
                }
                DeckButton {
                    text: "RENAME"
                    enabled: renameProfileName.text.trim().length > 0
                    onClicked: {
                        if (backend.renameProfile(renameProfileDialog.profileId, renameProfileName.text))
                            renameProfileDialog.close();
                        else
                            renameProfileDialog.errorMessage = "Choose a unique profile name.";
                    }
                }
            }
        }
        onOpened: {
            renameProfileName.text = name;
            renameProfileDialog.errorMessage = "";
            renameProfileName.forceActiveFocus();
        }
    }

    DeckDialog {
        id: duplicateProfileDialog
        property string profileId: ""
        property string categoryId: ""
        property string name: ""
        property string errorMessage: ""
        heading: "Duplicate profile"
        contentItem: ColumnLayout {
            width: duplicateProfileDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "NEW PROFILE NAME"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckField {
                id: duplicateProfileName
                Layout.fillWidth: true
                onTextEdited: duplicateProfileDialog.errorMessage = ""
            }
            Text {
                visible: duplicateProfileDialog.errorMessage.length > 0
                Layout.fillWidth: true
                text: duplicateProfileDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
            }
            Text {
                text: "DESTINATION CATEGORY"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckCombo {
                id: duplicateProfileCategory
                Layout.fillWidth: true
                model: root.categories
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    for (let index = 0; index < root.categories.length; ++index)
                        if (String(root.categories[index].id) === String(duplicateProfileDialog.categoryId))
                            return index;
                    return 0;
                }
            }
            Text {
                text: "This copies the profile's stored configuration. Category game rules and global Automation ownership are not duplicated."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: duplicateProfileDialog.close()
                }
                DeckButton {
                    text: "DUPLICATE"
                    enabled: duplicateProfileName.text.trim().length > 0
                    onClicked: {
                        if (backend.duplicateProfileToCategory(duplicateProfileDialog.profileId, duplicateProfileName.text, duplicateProfileCategory.currentValue))
                            duplicateProfileDialog.close();
                        else
                            duplicateProfileDialog.errorMessage = "Choose a unique profile name and a valid destination category.";
                    }
                }
            }
        }
        onOpened: {
            duplicateProfileName.text = name;
            duplicateProfileDialog.errorMessage = "";
            duplicateProfileName.forceActiveFocus();
        }
    }

    DeckDialog {
        id: moveProfileDialog
        property string profileId: ""
        property string categoryId: ""
        heading: "Move profile"
        contentItem: ColumnLayout {
            width: moveProfileDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "DESTINATION CATEGORY"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            DeckCombo {
                id: moveProfileCategory
                Layout.fillWidth: true
                model: root.categories
                textRole: "name"
                valueRole: "id"
                currentIndex: {
                    for (let index = 0; index < root.categories.length; ++index)
                        if (String(root.categories[index].id) === String(moveProfileDialog.categoryId))
                            return index;
                    return 0;
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: moveProfileDialog.close()
                }
                DeckButton {
                    text: "MOVE"
                    onClicked: {
                        if (backend.moveProfileToCategory(moveProfileDialog.profileId, moveProfileCategory.currentValue))
                            moveProfileDialog.close();
                    }
                }
            }
        }
    }

    DeckDialog {
        id: deleteProfileDialog
        objectName: "flightDeckProfileDeleteDialog"
        property string profileId: ""
        property string name: ""
        heading: "Delete profile?"
        contentItem: ColumnLayout {
            width: deleteProfileDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "Delete ‘" + deleteProfileDialog.name + "’? This removes this profile and its profile-specific configuration. Active and protected baseline profiles cannot be deleted."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    objectName: "flightDeckProfileDeleteCancel"
                    text: "CANCEL"
                    subdued: true
                    onClicked: deleteProfileDialog.close()
                }
                DeckButton {
                    objectName: "flightDeckProfileDeleteConfirm"
                    text: "DELETE PROFILE"
                    destructive: true
                    onClicked: {
                        if (backend.deleteProfile(deleteProfileDialog.profileId)) {
                            deleteProfileDialog.close();
                            root.returnToLibrary();
                        }
                    }
                }
            }
        }
    }

    DeckDialog {
        id: deleteCategoryDialog
        property string categoryId: ""
        property string name: ""
        heading: "Delete empty category?"
        contentItem: ColumnLayout {
            width: deleteCategoryDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "Delete ‘" + deleteCategoryDialog.name + "’? Categories must be empty, inactive, and leave at least one category behind. No profiles are cascaded."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: deleteCategoryDialog.close()
                }
                DeckButton {
                    text: "DELETE CATEGORY"
                    destructive: true
                    onClicked: {
                        if (backend.deleteProfileCategory(deleteCategoryDialog.categoryId)) {
                            deleteCategoryDialog.close();
                            root.returnToLibrary();
                        }
                    }
                }
            }
        }
    }

    DeckDialog {
        id: addGameDialog
        objectName: "flightDeckAddGameDialog"
        property string categoryId: ""
        property string mode: "running"
        property string browsePath: ""
        property string errorMessage: ""
        property string runningSearchText: ""
        readonly property var filteredRunningApplications: root.runningApplications.filter(function(application) {
            const query = addGameDialog.runningSearchText.trim().toLowerCase();
            if (query.length === 0)
                return true;
            const displayName = String(application.name || root.friendlyGameName(application.executable)).toLowerCase();
            const executable = String(application.executable || "").toLowerCase();
            return displayName.indexOf(query) >= 0 || executable.indexOf(query) >= 0;
        })
        heading: "Add game association"
        contentItem: ColumnLayout {
            width: addGameDialog.availableWidth
            spacing: deck.space12
            Text {
                text: "Detect a running game, choose an executable, or enter an executable name. Adding an association never activates this category immediately."
                color: deck.textSecondary
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Repeater {
                    model: [{ label: "RUNNING", value: "running" }, { label: "BROWSE", value: "browse" }, { label: "MANUAL", value: "manual" }]
                    delegate: DeckButton {
                        required property var modelData
                        text: modelData.label
                        subdued: addGameDialog.mode !== modelData.value
                        onClicked: addGameDialog.mode = modelData.value
                    }
                }
            }
            ScrollView {
                id: runningGameScroll
                visible: addGameDialog.mode === "running"
                Layout.fillWidth: true
                Layout.preferredHeight: 222
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        DeckField {
                            id: runningGameSearch
                            objectName: "flightDeckRunningGameSearch"
                            Layout.fillWidth: true
                            placeholderText: "Search running games or executables"
                            text: addGameDialog.runningSearchText
                            onTextEdited: addGameDialog.runningSearchText = text
                        }
                        DeckButton {
                            objectName: "flightDeckRunningGameSearchClear"
                            text: "CLEAR"
                            subdued: true
                            visible: addGameDialog.runningSearchText.length > 0
                            onClicked: {
                                addGameDialog.runningSearchText = "";
                                runningGameSearch.text = "";
                            }
                        }
                    }
                    Repeater {
                        model: addGameDialog.filteredRunningApplications
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: runningGameRow.implicitHeight + deck.space16
                            radius: deck.radiusControl
                            color: deck.secondarySurface
                            border.color: deck.border
                            RowLayout {
                                id: runningGameRow
                                anchors.fill: parent
                                anchors.margins: deck.space8
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: String(modelData.name || root.friendlyGameName(modelData.executable)); color: deck.textPrimary; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: String(modelData.executable || ""); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                                DeckButton {
                                    text: "ADD"
                                    onClicked: {
                                        if (root.addGameRule(addGameDialog.categoryId, modelData.executable)) addGameDialog.close();
                                        else addGameDialog.errorMessage = "This game association could not be saved.";
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.runningApplications.length === 0
                        text: "No suitable running applications were found. You can still choose an executable or enter a name."
                        color: deck.textMuted
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        objectName: "flightDeckRunningGameNoMatch"
                        visible: root.runningApplications.length > 0 && addGameDialog.filteredRunningApplications.length === 0
                        text: "No running game or executable matches \"" + addGameDialog.runningSearchText + "\". Clear the search to see all running applications."
                        color: deck.textMuted
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
            ColumnLayout {
                visible: addGameDialog.mode === "browse"
                Layout.fillWidth: true
                spacing: deck.space8
                Text { text: "EXECUTABLE"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    DeckField { Layout.fillWidth: true; readOnly: true; text: addGameDialog.browsePath; placeholderText: "No executable selected" }
                    DeckButton { text: "CHOOSE…"; onClicked: gameExecutableDialog.open() }
                }
                Text {
                    visible: addGameDialog.browsePath.length > 0
                    text: "Game: " + root.friendlyGameName(addGameDialog.browsePath) + "  ·  Executable: " + addGameDialog.browsePath.split(/[\\/]/).pop()
                    color: deck.textSecondary
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
            ColumnLayout {
                visible: addGameDialog.mode === "manual"
                Layout.fillWidth: true
                spacing: deck.space8
                Text { text: "EXECUTABLE"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                DeckField { id: gameExecutable; Layout.fillWidth: true; placeholderText: "bf6.exe" }
            }
            Text {
                visible: addGameDialog.errorMessage.length > 0
                text: addGameDialog.errorMessage
                color: deck.fault
                font.pixelSize: 9
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: addGameDialog.close()
                }
                DeckButton {
                    text: "ADD GAME"
                    visible: addGameDialog.mode !== "running"
                    enabled: addGameDialog.mode === "browse" ? addGameDialog.browsePath.length > 0 : gameExecutable.text.trim().length > 0
                    onClicked: {
                        const rule = addGameDialog.mode === "browse" ? addGameDialog.browsePath : gameExecutable.text;
                        if (root.addGameRule(addGameDialog.categoryId, rule))
                            addGameDialog.close();
                        else
                            addGameDialog.errorMessage = "This game association could not be saved.";
                    }
                }
            }
        }
        onOpened: {
            addGameDialog.mode = "running";
            addGameDialog.browsePath = "";
            addGameDialog.errorMessage = "";
            addGameDialog.runningSearchText = "";
            runningGameSearch.text = "";
            gameExecutable.text = "";
            root.refreshRunningApplications();
        }
    }

    FileDialog {
        id: gameExecutableDialog
        title: "Choose a game executable"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Windows applications (*.exe)"]
        onAccepted: addGameDialog.browsePath = selectedFile.toString()
    }
}
