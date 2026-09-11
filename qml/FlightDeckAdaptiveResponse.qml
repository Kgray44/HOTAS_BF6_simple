import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Window 6.5

// Native Flight Deck Adaptive Response presentation. The backend remains the
// sole owner of configuration, previews, simulation, telemetry, and history.
Flickable {
    id: root
    objectName: "flightDeckAdaptiveResponse"

    required property var backendObject
    property string profileContext: ""
    property string editScope: "profile"
    property string targetId: ""
    property string scenario: "Human-Like Rapid Reversal"
    property int contextEpoch: 0
    property int telemetryEpoch: 0
    property bool advancedExpanded: false
    property bool testLabExpanded: false
    property bool advancedTracesExpanded: false
    property bool showPhysicalTrace: true
    property bool showPredictedTrace: true
    property bool showOutputTrace: true
    property bool showBaselineTrace: false
    property bool showEstimatedTrace: false
    property string comparisonScope: "preset"
    property string comparisonTargetId: "off"
    property bool presetWorkshopExpanded: false
    property string presetNameDraft: ""
    property string presetDescriptionDraft: ""
    property string renamePresetId: ""
    property string renamePresetDraft: ""
    property string presetError: ""
    property string responseLabSource: "interactive"
    property int historyWindowSeconds: 5
    property bool historyPaused: false
    property bool liveRecording: false
    property bool liveReplaying: false
    property var historySamples: []
    property int historyRevision: 0
    property int historyLastSequence: 0
    property int historyInspectIndex: -1
    property bool simulatorPaused: true
    property bool simulatorRecording: false
    property bool simulatorReplaying: false
    property int simulatorSourceRate: 250
    property real simulatorInput: 0
    property int simulatorLastSequence: 0
    property var simulatorSamples: []
    property var simulatorDisplaySamples: []
    property int simulatorRevision: 0
    property int simulatorDisplayCount: 0
    property int simulatorReplayCursor: 0
    property var simulatorRecordingSamples: []
    property int replaySlowdown: 1
    property real replayPresentationMs: 0
    property bool responseLabNearViewport: false
    property bool responseMonitorVisible: false
    property bool responseMonitorPinned: false
    // Presentation-only fixture seam. It never changes the backend context,
    // configuration, predictor, simulator, or controller connection state.
    property var presentationOverride: null

    readonly property var liveState: backendObject.adaptiveResponseState
    property var runtimeState: {
        telemetryEpoch;
        return backendObject.adaptiveResponseState;
    }
    property var state: {
        contextEpoch;
        return backendObject.adaptiveResponseContextState(editScope, selectedTargetId(), backendObject.selectedAxisIndex);
    }
    property var telemetry: {
        telemetryEpoch;
        return backendObject.adaptiveResponseTelemetry;
    }
    property var previewSamples: backendObject.adaptiveResponsePreviewAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
    property var testLabMetrics: backendObject.adaptiveResponseTestLabAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex)
    property var comparisonSamples: backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    property var comparisonTestLabMetrics: backendObject.adaptiveResponseTestLabAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex)
    property var responseLabSamples: responseLabSource === "live" ? historySamples : simulatorDisplaySamples
    readonly property int historySampleCount: {
        historyRevision;
        return historySamples.length;
    }
    readonly property int responseLabSampleCount: responseLabSource === "live"
        ? historySampleCount : simulatorDisplayCount
    readonly property int responseLabRevision: responseLabSource === "live"
        ? historyRevision : simulatorRevision
    property var responseLabTelemetry: responseLabSource === "live" ? telemetry : simulatorCurrentSample()

    contentWidth: width
    // A Column's implicit height can lag a newly expanded child during the
    // same polish cycle. childrenRect keeps the scroll range honest for the
    // long Advanced and Test Lab sections, including immediately after they
    // are opened.
    contentHeight: Math.max(height, pageContent.implicitHeight + deck.space32, pageContent.childrenRect.height + deck.space32)
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

    function numericOr(value, fallback) {
        return value === undefined || value === null || Number.isNaN(Number(value)) ? fallback : Number(value);
    }

    function presentationValue(key, fallback) {
        if (presentationOverride && presentationOverride[key] !== undefined)
            return presentationOverride[key];
        return fallback;
    }

    function commitPresetRename() {
        presetError = "";
        if (backendObject.renameAdaptiveResponsePreset(renamePresetId, renamePresetDraft)) {
            renamePresetDialog.close();
            setPreview();
            return true;
        }
        presetError = "Use a unique preset name of 64 characters or fewer.";
        return false;
    }

    function effective() {
        return state.runtimeEffective || state.effective || ({});
    }
    function scopeInfo() {
        return state || ({});
    }

    function runtimeAutomationText() {
        const automation = runtimeState.automation || ({});
        if (!automation.active)
            return "None";
        const affected = automation.affectedProperties || [];
        return affected.length > 0 ? "Automation · " + affected.join(", ") : "Automation overlay active";
    }

    function runtimeSourceSummary() {
        const source = String(scopeInfo().source || scopeInfo().presetId || "the inherited response")
        const automation = runtimeAutomationText()
        return "Using " + source + " for this selection. "
            + (automation === "None" ? "No Automation response override is active."
                : automation + ".")
    }

    function liveInputAvailable() {
        return !!backendObject.physicalConnected
            && Number(backendObject.lastPhysicalUpdateAgeMs) >= 0
    }

    function propertyMask(key) {
        const masks = {
            enabled: 1,
            model: 2,
            maximumhorizonms: 4,
            maximumlead: 8,
            velocityresponse: 16,
            accelerationresponse: 32,
            motionsensitivity: 64,
            noiserejection: 128,
            reversaldetection: 256,
            reversalresponse: 512,
            decelerationresponse: 1024,
            settlingresponse: 2048,
            endpointtaper: 4096,
            onsetassist: 8192,
            onsetcap: 16384,
            sustainedassist: 32768,
            sustainedcap: 65536,
            horizonextension: 131072,
            horizonextensioncapms: 262144,
            turningpointprotection: 524288,
            turningpointmargin: 1048576,
            normalmovementresponse: 2097152,
            rapidmovementresponse: 4194304,
            engagementsensitivity: 8388608
        };
        return masks[String(key).toLowerCase()] || 0;
    }

    function inheritedHere(key) {
        return editScope !== "global" && (Number(scopeInfo().properties || 0) & propertyMask(key)) === 0;
    }

    function targetChoices() {
        if (editScope === "global")
            return [
                {
                    id: "",
                    label: "Application defaults"
                }
            ];
        if (editScope === "category")
            return (backendObject.profileCategories || []).map(function (category) {
                return {
                    id: category.id,
                    label: category.name
                };
            });
        if (editScope === "preset")
            return (backendObject.adaptiveResponsePresets || []).filter(function (preset) {
                return !preset.builtIn;
            }).map(function (preset) {
                return {
                    id: preset.id,
                    label: preset.name
                };
            });
        return (backendObject.profiles || []).map(function (profile) {
            return {
                id: profile.id,
                label: profile.displayName || profile.name
            };
        });
    }

    function selectedTargetId() {
        if (editScope === "global")
            return "";
        if (targetId.length > 0)
            return targetId;
        if (editScope === "category")
            return liveState.categoryId || "";
        if (editScope === "preset") {
            const choices = targetChoices();
            return choices.length > 0 ? choices[0].id : "";
        }
        return liveState.profileId || "";
    }

    function targetIndex() {
        const choices = targetChoices();
        const id = selectedTargetId();
        for (let index = 0; index < choices.length; ++index) {
            if (choices[index].id === id)
                return index;
        }
        return 0;
    }

    function comparisonChoices() {
        const entries = [];
        const presets = backendObject.adaptiveResponsePresets || [];
        for (let index = 0; index < presets.length; ++index) {
            entries.push({
                label: "Preset · " + presets[index].name,
                scope: "preset",
                id: presets[index].id
            });
        }
        const profiles = backendObject.profiles || [];
        for (let index = 0; index < profiles.length; ++index) {
            entries.push({
                label: "Profile · " + (profiles[index].displayName || profiles[index].name),
                scope: "profile",
                id: profiles[index].id
            });
        }
        return entries;
    }

    function comparisonIndex() {
        const choices = comparisonChoices();
        for (let index = 0; index < choices.length; ++index) {
            if (choices[index].scope === comparisonScope && choices[index].id === comparisonTargetId)
                return index;
        }
        return 0;
    }

    function axisModelIndex(physicalAxis) {
        const axes = backendObject.axes || [];
        for (let index = 0; index < axes.length; ++index) {
            if (Number(axes[index].index) === Number(physicalAxis))
                return index;
        }
        return 0;
    }

    function selectAxisModelIndex(modelIndex) {
        const axes = backendObject.axes || [];
        if (modelIndex < 0 || modelIndex >= axes.length)
            return false;
        const axis = axes[modelIndex];
        if (!axis || axis.index === undefined || axis.index === null)
            return false;
        backendObject.setSelectedAxis(Number(axis.index));
        setPreview();
        return true;
    }

    function percent(value, digits) {
        const number = numericOr(value, 0) * 100;
        const precision = digits === undefined ? 1 : digits;
        return (number >= 0 ? "+" : "") + number.toFixed(precision) + "%";
    }

    function valueWithUnit(value, unit) {
        const safe = numericOr(value, 0);
        if (unit === "ms")
            return safe.toFixed(1) + " ms";
        if (unit === "%")
            return percent(safe);
        if (unit === "axis")
            return safe.toFixed(3) + " axis";
        if (unit === "axis/s")
            return safe.toFixed(3) + " /s";
        return safe.toFixed(2);
    }

    function setPreview() {
        previewSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex);
        testLabMetrics = backendObject.adaptiveResponseTestLabAtContext(scenario, editScope, selectedTargetId(), backendObject.selectedAxisIndex);
        comparisonSamples = backendObject.adaptiveResponsePreviewAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex);
        comparisonTestLabMetrics = backendObject.adaptiveResponseTestLabAtContext(scenario, comparisonScope, comparisonTargetId, backendObject.selectedAxisIndex);
    }

    function applySimplePreset(presetId) {
        const applied = backendObject.setAdaptiveResponsePresetAtContext(editScope, selectedTargetId(), state.axis, presetId);
        if (applied)
            setPreview();
        return applied;
    }

    function updateParameter(key, value) {
        const updated = backendObject.setAdaptiveResponsePropertyAtContext(editScope, selectedTargetId(), state.axis, key, value);
        if (updated)
            setPreview();
        return updated;
    }

    function restoreInherited(key) {
        const restored = backendObject.setAdaptiveResponsePropertyAtContext(editScope, selectedTargetId(), state.axis, key, 0, true);
        if (restored)
            setPreview();
        return restored;
    }

    function setResponseLabSource(source) {
        if (responseLabSource === source)
            return;
        responseLabSource = source;
        historyLastSequence = 0;
        historySamples = [];
        historyRevision += 1;
        historyPaused = false;
        liveRecording = false;
        liveReplaying = false;
        simulatorLastSequence = 0;
        simulatorSamples = [];
        simulatorDisplaySamples = [];
        simulatorDisplayCount = 0;
        simulatorReplayCursor = 0;
        simulatorRevision += 1;
        if (source === "live")
            refreshHistory(true);
    }

    function refreshHistory(reset) {
        if (historyPaused && !reset)
            return;
        const update = backendObject.adaptiveResponseHistorySince(reset ? 0 : historyLastSequence, historyWindowSeconds);
        const incoming = update.samples || [];
        let changed = false;
        if (reset || update.reset) {
            historySamples = incoming;
            changed = true;
        } else if (incoming.length > 0) {
            for (let index = 0; index < incoming.length; ++index)
                historySamples.push(incoming[index]);
            const maximum = Math.max(180, historyWindowSeconds * 100);
            if (historySamples.length > maximum)
                historySamples.splice(0, historySamples.length - maximum);
            changed = true;
        }
        historyLastSequence = Number(update.newestSequence || historyLastSequence);
        if (changed)
            historyRevision += 1;
        if (!historyPaused)
            historyInspectIndex = historySamples.length - 1;
    }

    function refreshSimulator(reset) {
        const update = backendObject.adaptiveResponseSimulatorHistorySince(reset ? 0 : simulatorLastSequence);
        const incoming = update.samples || [];
        let changed = false;
        if (reset || update.reset) {
            simulatorSamples = incoming;
            changed = true;
        } else if (incoming.length > 0) {
            for (let index = 0; index < incoming.length; ++index)
                simulatorSamples.push(incoming[index]);
            if (simulatorSamples.length > 900)
                simulatorSamples.splice(0, simulatorSamples.length - 900);
            changed = true;
        }
        simulatorLastSequence = Number(update.newestSequence || simulatorLastSequence);
        simulatorDisplaySamples = simulatorSamples;
        simulatorDisplayCount = simulatorSamples.length;
        if (changed)
            simulatorRevision += 1;
    }

    function sampleSimulator() {
        backendObject.adaptiveResponseSimulatorStepAtContext(simulatorInput, editScope, selectedTargetId(), backendObject.selectedAxisIndex, simulatorSourceRate);
    }

    function simulatorCurrentSample() {
        const count = Math.min(simulatorDisplayCount, simulatorDisplaySamples ? simulatorDisplaySamples.length : 0);
        if (!simulatorDisplaySamples || count === 0)
            return ({});
        return simulatorDisplaySamples[count - 1] || ({});
    }

    function startReplay() {
        simulatorRecordingSamples = backendObject.adaptiveResponseSimulatorRecording();
        if (simulatorRecordingSamples.length === 0)
            return;
        simulatorReplaying = true;
        simulatorPaused = false;
        replayPresentationMs = 0;
        simulatorReplayCursor = 1;
        simulatorDisplaySamples = simulatorRecordingSamples;
        simulatorDisplayCount = simulatorReplayCursor;
        simulatorRevision += 1;
    }

    function updateReplayPresentation() {
        if (!simulatorReplaying || simulatorPaused || simulatorRecordingSamples.length === 0)
            return;
        replayPresentationMs += 33;
        const originalElapsed = replayPresentationMs / Math.max(1, replaySlowdown);
        while (simulatorReplayCursor < simulatorRecordingSamples.length
                && numericOr(simulatorRecordingSamples[simulatorReplayCursor].recordedElapsedMs, 0) <= originalElapsed)
            simulatorReplayCursor += 1;
        simulatorDisplayCount = Math.max(1, simulatorReplayCursor);
        simulatorRevision += 1;
        if (originalElapsed >= numericOr(simulatorRecordingSamples[simulatorRecordingSamples.length - 1].recordedElapsedMs, 0)) {
            simulatorPaused = true;
            simulatorReplaying = false;
        }
    }

    function magnitude(samples, fields, minimum, sampleLimit) {
        let maximum = minimum;
        const count = Math.max(0, Math.min(sampleLimit === undefined ? samples.length : sampleLimit, samples.length));
        for (let index = 0; index < count; ++index) {
            for (let field = 0; field < fields.length; ++field)
                maximum = Math.max(maximum, Math.abs(numericOr(samples[index][fields[field]], 0)));
        }
        return maximum;
    }

    function responseLabMagnitude(fields, minimum) {
        responseLabRevision;
        return magnitude(responseLabSamples || [], fields, minimum, responseLabSampleCount);
    }

    function metricMilliseconds(value) {
        return numericOr(value, -1) < 0 ? "—" : numericOr(value, 0).toFixed(1) + " ms";
    }

    function testLabMetricRows() {
        const result = testLabMetrics || ({});
        return [
            {
                caption: "SAMPLES",
                value: String(Math.round(numericOr(result.sampleCount, 0))),
                detail: "Estimator samples",
                tone: deck.textPrimary
            },
            {
                caption: "PEAK LEAD",
                value: percent(result.peakLead),
                detail: "Mapped-output peak",
                tone: deck.attention
            },
            {
                caption: "MEDIAN LEAD",
                value: percent(result.medianLead),
                detail: "Scenario median",
                tone: deck.attention
            },
            {
                caption: "MEAN ERROR",
                value: percent(result.meanAbsolutePredictionError),
                detail: "Prediction error",
                tone: deck.accent
            },
            {
                caption: "RMS ERROR",
                value: percent(result.rmsPredictionError),
                detail: "Prediction error",
                tone: deck.accent
            },
            {
                caption: "P95 ERROR",
                value: percent(result.p95PredictionError),
                detail: "Prediction error",
                tone: deck.attention
            },
            {
                caption: "MAX ERROR",
                value: percent(result.maximumPredictionError),
                detail: "Prediction error",
                tone: deck.attention
            },
            {
                caption: "SUSTAINED AUTHORITY",
                value: percent(result.maximumSustainedAuthority),
                detail: "Maximum applied",
                tone: deck.healthy
            },
            {
                caption: "HORIZON EXTENSION",
                value: numericOr(result.maximumHorizonExtensionMs, 0).toFixed(1) + " ms",
                detail: "Maximum extra",
                tone: deck.healthy
            },
            {
                caption: "ALLOWED HORIZON",
                value: numericOr(result.maximumAllowedHorizonMs, 0).toFixed(1) + " ms",
                detail: "Safety ceiling",
                tone: deck.textPrimary
            },
            {
                caption: "TURN PROTECTION",
                value: String(Math.round(numericOr(result.turningPointProtectionActivations, 0))),
                detail: "Activations",
                tone: deck.healthy
            },
            {
                caption: "TURN OVERSHOOT",
                value: percent(result.maximumTurningPointOvershoot),
                detail: "Bounded result",
                tone: deck.healthy
            },
            {
                caption: "TARGET OVERSHOOT",
                value: percent(result.targetOvershoot),
                detail: result.hasStaticTarget ? "Static target" : "Not applicable",
                tone: deck.textPrimary
            },
            {
                caption: "MOTION RECOGNITION",
                value: metricMilliseconds(result.motionRecognitionDelayMs),
                detail: "Deliberate movement",
                tone: deck.accent
            },
            {
                caption: "TRUE / FALSE REVERSALS",
                value: Math.round(numericOr(result.trueReversalCount, 0)) + " / " + Math.round(numericOr(result.falseReversalCount, 0)),
                detail: "Classifier receipt",
                tone: deck.textPrimary
            },
            {
                caption: "STATIONARY LEAD",
                value: percent(result.stationaryLead),
                detail: "Rest behavior",
                tone: deck.healthy
            },
            {
                caption: "PHYSICAL REVERSAL",
                value: metricMilliseconds(result.physicalReversalMs),
                detail: "Ground truth",
                tone: deck.textPrimary
            },
            {
                caption: "PREDICTOR DETECTED",
                value: metricMilliseconds(result.predictorDetectedMs),
                detail: "Estimator state",
                tone: deck.accent
            },
            {
                caption: "REVERSAL LATENCY",
                value: metricMilliseconds(result.reversalDetectionLatencyMs),
                detail: "From physical reversal",
                tone: deck.healthy
            },
            {
                caption: "PRE / POST LEAD",
                value: percent(result.preReversalLead) + " / " + percent(result.postReversalLead),
                detail: "Cancellation",
                tone: deck.healthy
            },
            {
                caption: "LEAD COLLAPSE",
                value: percent(result.leadCollapseMagnitude),
                detail: "Safety response",
                tone: deck.healthy
            },
            {
                caption: "STALE LEAD CLEARED",
                value: metricMilliseconds(result.staleLeadCancellationMs),
                detail: "Cancellation time",
                tone: deck.healthy
            },
            {
                caption: "OPPOSITE REACQUIRE",
                value: metricMilliseconds(result.oppositeDirectionReacquisitionMs),
                detail: "New direction",
                tone: deck.healthy
            },
            {
                caption: "SETTLING",
                value: metricMilliseconds(result.settlingTimeMs),
                detail: "Return to rest",
                tone: deck.textPrimary
            },
            {
                caption: "MAX PHYSICAL STEP",
                value: percent(result.maximumPhysicalDelta),
                detail: "Input spacing",
                tone: deck.textPrimary
            },
            {
                caption: "MAX PREDICTED STEP",
                value: percent(result.maximumPredictedDelta),
                detail: "Estimator spacing",
                tone: deck.attention
            },
            {
                caption: "PREDICTOR-ONLY STEP",
                value: percent(result.maximumArtificialPredictorStep),
                detail: "Safety receipt",
                tone: deck.healthy
            },
            {
                caption: "VIRTUAL OUTPUT STEP",
                value: percent(result.maximumVirtualOutputStep),
                detail: "Mapped output",
                tone: deck.healthy
            }
        ];
    }

    function sectionNearViewport(section) {
        if (!section || !root.contentItem)
            return false;
        const top = section.mapToItem(root.contentItem, 0, 0).y;
        return top + section.height >= root.contentY - 260 && top <= root.contentY + root.height + 260;
    }

    function refreshViewportActivity() {
        responseLabNearViewport = sectionNearViewport(liveAnalysisSection);
    }

    // Keep programmatic navigation inside the Flickable coordinate system. It
    // is used by keyboard/accessibility-style navigation and makes every long
    // engineering section reachable without changing configuration or runtime
    // behavior.
    function sectionItem(section) {
        if (section === "preview")
            return staticResponsePreviewCard;
        else if (section === "comparison")
            return adaptiveComparisonCard;
        else if (section === "advanced")
            return advancedTuningCard;
        else if (section === "advanced-controls")
            return advancedTuningContent;
        else if (section === "telemetry")
            return telemetrySection;
        else if (section === "analysis")
            return liveAnalysisSection;
        else if (section === "test-lab")
            return testLabCard;
        return null;
    }

    function sectionGeometry(section) {
        const item = sectionItem(section);
        if (!item || !root.contentItem)
            return ({});
        const top = item.mapToItem(root.contentItem, 0, 0).y;
        return {
            top: top,
            bottom: top + item.height,
            height: item.height,
            contentHeight: root.contentHeight
        };
    }

    function scrollToSection(section) {
        const item = sectionItem(section);
        if (!item || !root.contentItem)
            return false;
        const top = item.mapToItem(root.contentItem, 0, 0).y;
        const maximum = Math.max(0, root.contentHeight - root.height);
        root.contentY = Math.max(0, Math.min(maximum, top - deck.space16));
        refreshViewportActivity();
        return true;
    }

    function parametersFor(group) {
        const rows = [
            {
                group: "Flight response",
                key: "normalMovementResponse",
                label: "Normal Movement Response",
                detail: "How strongly Adaptive Response helps during smooth everyday control movement.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Flight response",
                key: "rapidMovementResponse",
                label: "Rapid Movement Response",
                detail: "Additional response available during fast maneuvers.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Flight response",
                key: "engagementSensitivity",
                label: "Engagement Sensitivity",
                detail: "How easily deliberate gentle movement begins receiving assistance.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Prediction",
                key: "maximumHorizonMs",
                label: "Maximum horizon",
                detail: "The time ceiling for prediction. Active horizon may remain below it.",
                from: 0,
                to: 30,
                step: 0.5,
                unit: "ms"
            },
            {
                group: "Prediction",
                key: "maximumLead",
                label: "Maximum mapped-output lead",
                detail: "Hard safety envelope in final mapped-output movement.",
                from: 0.01,
                to: 0.50,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Prediction",
                key: "endpointTaper",
                label: "Endpoint taper",
                detail: "Reduces predictive lead as output headroom closes.",
                from: 0.01,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Motion detection",
                key: "velocityResponse",
                label: "Velocity response",
                detail: "Derivative responsiveness during deliberate movement.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "gain"
            },
            {
                group: "Motion detection",
                key: "accelerationResponse",
                label: "Acceleration response",
                detail: "Acceleration contribution for the selected predictor.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "gain"
            },
            {
                group: "Motion detection",
                key: "motionSensitivity",
                label: "Motion sensitivity",
                detail: "Minimum deliberate movement that can activate prediction.",
                from: 0.001,
                to: 2,
                step: 0.005,
                unit: "axis/s"
            },
            {
                group: "Motion detection",
                key: "noiseRejection",
                label: "Noise rejection",
                detail: "Ignores tiny sensor movement while estimating motion.",
                from: 0,
                to: 0.50,
                step: 0.001,
                unit: "axis"
            },
            {
                group: "Reversal and settling",
                key: "reversalDetection",
                label: "Reversal detection",
                detail: "Motion threshold that clears stale directional lead.",
                from: 0.001,
                to: 10,
                step: 0.01,
                unit: "axis/s"
            },
            {
                group: "Reversal and settling",
                key: "reversalResponse",
                label: "Reversal response",
                detail: "New-direction lead reacquisition after safety cancellation.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "gain"
            },
            {
                group: "Reversal and settling",
                key: "decelerationResponse",
                label: "Deceleration response",
                detail: "Reduces lead while braking.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "gain"
            },
            {
                group: "Reversal and settling",
                key: "settlingResponse",
                label: "Settling response",
                detail: "Collapses horizon and damps state as motion comes to rest.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "gain"
            },
            {
                group: "Coherent motion",
                key: "onsetAssist",
                label: "Onset assist",
                detail: "Builds prediction sooner from coherent acceleration while motion gains speed.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Coherent motion",
                key: "onsetCap",
                label: "Onset cap",
                detail: "Bounds onset contribution; horizon and lead remain absolute limits.",
                from: 0,
                to: 0.40,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Coherent motion",
                key: "sustainedAssist",
                label: "Sustained assist",
                detail: "Builds authority during continuous, predictable control movement.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Coherent motion",
                key: "sustainedCap",
                label: "Sustained cap",
                detail: "Bounds the sustained-motion authority contribution.",
                from: 0,
                to: 0.35,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Coherent motion",
                key: "horizonExtension",
                label: "Adaptive horizon extension",
                detail: "Permits longer prediction only for slow, coherent sustained movement.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Coherent motion",
                key: "horizonExtensionCapMs",
                label: "Horizon extension cap",
                detail: "Hard cap for additional adaptive horizon.",
                from: 0,
                to: 30,
                step: 1,
                unit: "ms"
            },
            {
                group: "Safety and turns",
                key: "turningPointProtection",
                label: "Turning-point protection",
                detail: "Bounds prediction before a credible stop or reversal.",
                from: 0,
                to: 1,
                step: 0.01,
                unit: "%"
            },
            {
                group: "Safety and turns",
                key: "turningPointMargin",
                label: "Turning-point margin",
                detail: "Bounded headroom for stopping-estimate uncertainty.",
                from: 0,
                to: 0.30,
                step: 0.01,
                unit: "%"
            }
        ];
        return rows.filter(function (row) {
            return row.group === group;
        });
    }

    onContentYChanged: refreshViewportActivity()
    onWidthChanged: refreshViewportActivity()
    onProfileContextChanged: {
        if (profileContext.length > 0) {
            editScope = "profile";
            targetId = profileContext;
            setPreview();
        }
    }
    onStateChanged: {
        setPreview();
        historyLastSequence = 0;
        historySamples = [];
        historyRevision += 1;
        if (responseLabSource === "live")
            refreshHistory(true);
    }
    onHistoryWindowSecondsChanged: {
        historyLastSequence = 0;
        historySamples = [];
        historyRevision += 1;
        if (responseLabSource === "live")
            refreshHistory(true);
    }

    Component.onCompleted: {
        if (profileContext.length > 0) {
            editScope = "profile";
            targetId = profileContext;
            setPreview();
        }
        refreshViewportActivity();
    }

    Connections {
        target: backendObject
        function onStateChanged() {
            root.contextEpoch += 1;
            root.telemetryEpoch += 1;
        }
        function onInputTelemetryChanged() {
            root.telemetryEpoch += 1;
        }
    }

    Timer {
        interval: 33
        repeat: true
        triggeredOnStart: true
        running: root.visible && root.responseLabSource === "live" && root.responseLabNearViewport && !root.historyPaused
        onTriggered: root.refreshHistory(false)
    }
    Timer {
        interval: 33
        repeat: true
        running: root.visible && root.responseLabSource === "live" && root.liveReplaying && root.historySampleCount > 0
        onTriggered: {
            const next = root.historyInspectIndex + 1;
            if (next >= root.historySampleCount) {
                root.liveReplaying = false;
                root.historyPaused = true;
            } else {
                root.historyInspectIndex = next;
            }
        }
    }
    Timer {
        interval: 33
        repeat: true
        triggeredOnStart: true
        running: root.visible && root.responseLabSource === "interactive" && root.responseLabNearViewport && !root.simulatorPaused && !root.simulatorReplaying
        onTriggered: root.sampleSimulator()
    }
    Timer {
        interval: 33
        repeat: true
        running: root.visible && root.responseLabSource === "interactive" && root.responseLabNearViewport && root.simulatorReplaying && !root.simulatorPaused
        onTriggered: root.updateReplayPresentation()
    }
    Timer {
        interval: 33
        repeat: true
        triggeredOnStart: true
        running: root.visible && root.responseLabSource === "interactive" && root.responseLabNearViewport && !root.simulatorReplaying
        onTriggered: root.refreshSimulator(false)
    }

    component SectionLabel: Text {
        property string textValue: ""
        text: textValue
        color: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: 9
        font.bold: true
        font.letterSpacing: 1.2
        elide: Text.ElideRight
    }

    component DeckButton: Button {
        id: control
        property bool subdued: false
        property var flightDeckRoot: root
        implicitHeight: deck.compactControlHeight
        padding: deck.space12
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: control.text
            color: !control.enabled ? deck.disabled : control.subdued ? deck.textPrimary : deck.primarySurface
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
        signal choiceActivated(int index, var value)
        implicitHeight: deck.controlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Item {
            Text {
                anchors.fill: parent
                leftPadding: deck.space12
                rightPadding: deck.space24
                text: control.displayText
                color: control.enabled ? deck.textPrimary : deck.disabled
                font.family: deck.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            // Own the pointer path so themed DeckCombo controls behave
            // consistently in a native window and the offscreen harness.
            // Keyboard navigation remains on ComboBox itself.
            MouseArea {
                anchors.fill: parent
                enabled: control.enabled
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (control.popup.visible)
                        control.popup.close();
                    else
                        control.popup.open();
                }
            }
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
            color: control.pressed ? deck.selected : deck.elevatedSurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            objectName: control.objectName + "Choice_" + index
            width: ListView.view.width
            height: 36
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
                color: parent.highlighted ? deck.selected : deck.elevatedSurface
            }
            onPressed: {
                control.choiceActivated(index, control.valueAt(index));
                control.popup.close();
            }
        }
        popup: Popup {
            objectName: control.objectName + "Popup"
            y: control.height - 1
            width: control.width
            padding: deck.popupPadding
            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
            background: Rectangle {
                radius: deck.radiusControl
                color: deck.elevatedSurface
                border.color: deck.border
            }
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
        }
    }

    component DeckSwitch: Switch {
        id: control
        implicitWidth: 54
        implicitHeight: 30
        focusPolicy: Qt.StrongFocus
        indicator: Rectangle {
            x: (control.width - width) / 2
            y: (control.height - height) / 2
            width: 48
            height: 24
            radius: 12
            color: control.checked ? deck.healthy : deck.secondarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : (control.checked ? deck.healthy : deck.border)
            Rectangle {
                width: 18
                height: 18
                radius: 9
                x: control.checked ? parent.width - width - 3 : 3
                anchors.verticalCenter: parent.verticalCenter
                color: control.checked ? deck.primarySurface : deck.textMuted
                Behavior on x {
                    NumberAnimation {
                        duration: 120
                    }
                }
            }
        }
        contentItem: Item {}
    }

    component Gauge: Item {
        property string caption: ""
        property string display: "—"
        property real value: 0
        property real maximum: 1
        property color tone: deck.accent
        implicitWidth: 154
        implicitHeight: 52
        Column {
            anchors.fill: parent
            spacing: 5
            Row {
                width: parent.width
                Text {
                    width: parent.width * 0.55
                    text: parent.parent.parent.caption
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width * 0.45
                    text: parent.parent.parent.display
                    color: parent.parent.parent.tone
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                width: parent.width
                height: 8
                radius: 4
                color: deck.secondarySurface
                border.color: deck.border
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, Math.abs(parent.parent.parent.value) / Math.max(0.0001, parent.parent.parent.maximum)))
                    height: parent.height
                    radius: parent.radius
                    color: parent.parent.parent.tone
                }
            }
        }
    }

    component MetricTile: Rectangle {
        property string caption: ""
        property string value: "—"
        property string detail: ""
        property color tone: deck.textPrimary
        implicitWidth: 148
        implicitHeight: 82
        radius: deck.radiusControl
        color: deck.secondarySurface
        border.color: deck.border
        Column {
            anchors.fill: parent
            anchors.margins: deck.space12
            spacing: 4
            Text {
                width: parent.width
                text: parent.parent.caption
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 8
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: parent.parent.value
                color: parent.parent.tone
                font.family: deck.telemetryFont
                font.pixelSize: 17
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                visible: parent.parent.detail.length > 0
                width: parent.width
                text: parent.parent.detail
                color: deck.textSecondary
                font.pixelSize: 9
                elide: Text.ElideRight
            }
        }
    }

    component TraceLegend: Button {
        id: control
        property color tone: deck.accent
        checkable: true
        implicitHeight: 26
        implicitWidth: legendRow.implicitWidth + deck.space16
        // Keep the trace controls keyboard reachable without turning a mouse
        // click into a persistent focus outline. `visualFocus` is used only
        // for keyboard navigation, while hover/down are transient pointer
        // feedback and checked remains an independent multi-select state.
        focusPolicy: Qt.TabFocus
        contentItem: Row {
            id: legendRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                width: 12
                height: 3
                radius: 2
                color: control.tone
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: (control.checked ? "✓  " : "") + control.text
                color: control.checked ? deck.textPrimary : deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 8
                font.bold: true
            }
        }
        background: Rectangle {
            radius: deck.radiusPill
            color: control.down ? deck.accentMuted
                : control.hovered ? (control.checked ? deck.selected : deck.elevatedSurface)
                : control.checked ? deck.selected : deck.secondarySurface
            border.color: control.visualFocus ? deck.focus
                : control.checked ? control.tone : control.hovered ? deck.textMuted : deck.border
            border.width: control.visualFocus ? 2 : 1
        }
        Accessible.name: (checked ? "Hide " : "Show ") + text + " trace"
    }

    component InstrumentGraph: Canvas {
        id: graph
        property var samples: []
        // A graph observes a revision rather than requiring callers to clone
        // a JavaScript sample array merely to wake Canvas.
        property int sampleRevision: samples === root.responseLabSamples
            ? root.responseLabRevision : samples === root.historySamples ? root.historyRevision : 0
        property int sampleLimit: samples === root.responseLabSamples
            ? root.responseLabSampleCount : samples ? samples.length : 0
        property var series: []
        property real lowerBound: -1
        property real upperBound: 1
        property bool drawInspectionCursor: false
        antialiasing: true
        onSamplesChanged: requestPaint()
        onSampleRevisionChanged: requestPaint()
        onSampleLimitChanged: requestPaint()
        onSeriesChanged: requestPaint()
        onLowerBoundChanged: requestPaint()
        onUpperBoundChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.fillStyle = deck.secondarySurface;
            ctx.fillRect(0, 0, width, height);
            const horizontalPadding = 8;
            const verticalPadding = 8;
            const plotWidth = Math.max(1, width - horizontalPadding * 2);
            const plotHeight = Math.max(1, height - verticalPadding * 2);
            ctx.strokeStyle = deck.divider;
            ctx.lineWidth = 1;
            for (let line = 1; line < 4; ++line) {
                const y = verticalPadding + plotHeight * line / 4;
                ctx.beginPath();
                ctx.moveTo(horizontalPadding, y);
                ctx.lineTo(horizontalPadding + plotWidth, y);
                ctx.stroke();
            }
            if (lowerBound < 0 && upperBound > 0) {
                const zero = verticalPadding + plotHeight * (1 - (0 - lowerBound) / Math.max(0.0001, upperBound - lowerBound));
                ctx.strokeStyle = deck.textMuted;
                ctx.beginPath();
                ctx.moveTo(horizontalPadding, zero);
                ctx.lineTo(horizontalPadding + plotWidth, zero);
                ctx.stroke();
            }
            const sampleCount = Math.max(0, Math.min(sampleLimit, samples ? samples.length : 0));
            if (!samples || sampleCount === 0)
                return;
            ctx.save();
            ctx.beginPath();
            ctx.rect(horizontalPadding, verticalPadding, plotWidth, plotHeight);
            ctx.clip();
            for (let trace = 0; trace < series.length; ++trace) {
                const descriptor = series[trace];
                ctx.strokeStyle = descriptor.color;
                ctx.lineWidth = descriptor.width || 2;
                ctx.beginPath();
                // More points than horizontal pixels cannot add visual
                // fidelity.  Decimate at draw time while preserving the last
                // sample and its original horizontal position.
                const sampleStride = Math.max(1, Math.ceil(sampleCount / Math.max(1, Math.floor(plotWidth))));
                let drewFinalSample = false;
                for (let index = 0; index < sampleCount; index += sampleStride) {
                    const point = samples[index] || ({});
                    const value = Math.max(lowerBound, Math.min(upperBound, root.numericOr(point[descriptor.field], 0)));
                    const x = horizontalPadding + index * plotWidth / Math.max(1, sampleCount - 1);
                    const y = verticalPadding + plotHeight * (1 - (value - lowerBound) / Math.max(0.0001, upperBound - lowerBound));
                    if (index === 0)
                        ctx.moveTo(x, y);
                    else
                        ctx.lineTo(x, y);
                    drewFinalSample = index === sampleCount - 1;
                }
                if (!drewFinalSample && sampleCount > 1) {
                    const index = sampleCount - 1;
                    const point = samples[index] || ({});
                    const value = Math.max(lowerBound, Math.min(upperBound, root.numericOr(point[descriptor.field], 0)));
                    const x = horizontalPadding + index * plotWidth / Math.max(1, sampleCount - 1);
                    const y = verticalPadding + plotHeight * (1 - (value - lowerBound) / Math.max(0.0001, upperBound - lowerBound));
                    ctx.lineTo(x, y);
                }
                ctx.stroke();
            }
            if (drawInspectionCursor && root.historyPaused && root.historyInspectIndex >= 0) {
                const x = horizontalPadding + root.historyInspectIndex * plotWidth / Math.max(1, sampleCount - 1);
                ctx.strokeStyle = deck.attention;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x, verticalPadding);
                ctx.lineTo(x, verticalPadding + plotHeight);
                ctx.stroke();
            }
            ctx.restore();
        }
        Connections {
            target: root
            function onHistoryInspectIndexChanged() {
                graph.requestPaint();
            }
        }
    }

    component TuningControl: Rectangle {
        id: control
        property var descriptor: ({})
        readonly property string key: String(descriptor.key || "")
        readonly property real currentValue: root.numericOr(root.effective()[key], 0)
        implicitHeight: tuningColumn.implicitHeight + deck.space24
        width: parent ? parent.width : implicitWidth
        radius: deck.radiusControl
        color: deck.secondarySurface
        border.width: 1
        border.color: root.inheritedHere(key) ? deck.border : deck.accent
        ColumnLayout {
            id: tuningColumn
            anchors.fill: parent
            anchors.margins: deck.space12
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: control.descriptor.label || ""
                    color: deck.textPrimary
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    text: root.editScope === "global" ? "APPLICATION DEFAULT" : root.inheritedHere(control.key) ? "INHERITED" : "OVERRIDE"
                    color: root.editScope === "global" ? deck.textMuted : root.inheritedHere(control.key) ? deck.textMuted : deck.accent
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    font.bold: true
                }
            }
            Text {
                Layout.fillWidth: true
                text: control.descriptor.detail || ""
                color: deck.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Item {
                    id: sliderSurface
                    objectName: "flightDeckAdaptiveSlider_" + control.key
                    Layout.fillWidth: true
                    Layout.minimumWidth: 160
                    implicitHeight: 28
                    property real from: Number(control.descriptor.from)
                    property real to: Number(control.descriptor.to)
                    property real step: Number(control.descriptor.step)
                    property real value: control.currentValue
                    property int pointerPresses: 0
                    property real visualPosition: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
                    activeFocusOnTab: true
                    Keys.onLeftPressed: commit(value - step)
                    Keys.onRightPressed: commit(value + step)
                    Keys.onDownPressed: commit(value - step)
                    Keys.onUpPressed: commit(value + step)
                    function snapped(raw) {
                        const bounded = Math.max(from, Math.min(to, raw));
                        return step > 0 ? Math.round((bounded - from) / step) * step + from : bounded;
                    }
                    function commit(raw) {
                        root.updateParameter(control.key, snapped(raw));
                    }
                    Rectangle {
                        x: 8
                        y: (parent.height - height) / 2
                        width: Math.max(1, parent.width - 16)
                        height: 6
                        radius: 3
                        color: deck.primarySurface
                        border.color: deck.border
                        Rectangle {
                            width: parent.width * sliderSurface.visualPosition
                            height: parent.height
                            radius: parent.radius
                            color: deck.accent
                        }
                    }
                    Rectangle {
                        x: 8 + sliderSurface.visualPosition * Math.max(0, sliderSurface.width - 32)
                        y: (parent.height - height) / 2
                        width: 16
                        height: 16
                        radius: 8
                        color: pointerSurface.pressed ? deck.accent : deck.elevatedSurface
                        border.color: sliderSurface.activeFocus ? deck.focus : deck.accent
                        border.width: sliderSurface.activeFocus ? 2 : 1
                    }
                    TapHandler {
                        id: pointerSurface
                        objectName: "flightDeckAdaptiveSliderPointer_" + control.key
                        acceptedButtons: Qt.LeftButton
                        function applyPointer(position) {
                            const fraction = Math.max(0, Math.min(1, (position - 8) / Math.max(1, sliderSurface.width - 16)));
                            sliderSurface.commit(sliderSurface.from + (sliderSurface.to - sliderSurface.from) * fraction);
                        }
                        onTapped: function (eventPoint) {
                            sliderSurface.forceActiveFocus();
                            sliderSurface.pointerPresses += 1;
                            applyPointer(eventPoint.position.x);
                        }
                    }
                }
                Text {
                    Layout.preferredWidth: 82
                    text: root.valueWithUnit(control.currentValue, control.descriptor.unit)
                    color: deck.textPrimary
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                }
                DeckButton {
                    visible: root.editScope !== "preset"
                    text: root.editScope === "global" ? "RESET" : root.inheritedHere(control.key) ? "INHERITED" : "INHERIT"
                    subdued: true
                    enabled: root.editScope === "global" || !root.inheritedHere(control.key)
                    onClicked: root.restoreInherited(control.key)
                }
            }
        }
    }

    component TuningGroup: Rectangle {
        id: group
        property string title: ""
        property string detail: ""
        property var rows: []
        implicitHeight: groupContent.implicitHeight + deck.space24
        width: parent ? parent.width : implicitWidth
        radius: deck.radiusCard
        color: deck.primarySurface
        border.color: deck.border
        Column {
            id: groupContent
            anchors.fill: parent
            anchors.margins: deck.space12
            spacing: deck.space8
            Text {
                width: parent.width
                text: group.title
                color: deck.textPrimary
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                visible: group.detail.length > 0
                width: parent.width
                text: group.detail
                color: deck.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: group.rows
                delegate: TuningControl {
                    descriptor: modelData
                }
            }
        }
    }

    Window {
        id: responseMonitor
        visible: root.responseMonitorVisible
        title: "Adaptive Response Monitor"
        width: 620
        height: 360
        minimumWidth: 460
        minimumHeight: 280
        color: deck.applicationBackground
        modality: Qt.NonModal
        transientParent: root.Window.window
        flags: Qt.Window | (root.responseMonitorPinned ? Qt.WindowStaysOnTopHint : 0)
        onClosing: function (close) {
            // Keep the reusable monitor component alive and let its visible
            // binding own dismissal, matching the existing Adaptive Response
            // monitor lifecycle on native Windows windows.
            close.accepted = false;
            root.responseMonitorVisible = false;
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: deck.space16
            radius: deck.radiusCard
            color: deck.primarySurface
            border.color: deck.border
            Column {
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                Text {
                    text: "ADAPTIVE RESPONSE · LIVE MONITOR"
                    color: deck.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: "Bounded snapshot view for " + (root.runtimeState.axisLabel || "selected axis") + ". This monitor observes the same UI-safe telemetry as Flight Deck."
                    color: deck.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        text: "READ ONLY · UI-SAFE SNAPSHOT"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                    }
                    DeckButton {
                        objectName: "adaptiveResponseMonitorPin"
                        text: root.responseMonitorPinned ? "PINNED" : "PIN"
                        subdued: !root.responseMonitorPinned
                        onClicked: root.responseMonitorPinned = !root.responseMonitorPinned
                    }
                    DeckButton {
                        text: "CLOSE"
                        subdued: true
                        onClicked: root.responseMonitorVisible = false
                    }
                }
                Flow {
                    objectName: "adaptiveMonitorMetrics"
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space8
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    MetricTile {
                        caption: "PHYSICAL"
                        value: root.percent(root.telemetry.physical)
                        tone: deck.textPrimary
                    }
                    MetricTile {
                        caption: "PREDICTED"
                        value: root.percent(root.telemetry.predicted)
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "OUTPUT"
                        value: root.percent(root.telemetry.adaptiveOutput)
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "MOTION"
                        value: String(root.telemetry.state || "Stable").toUpperCase()
                        tone: deck.accent
                    }
                }
                Item {
                    width: 1
                    height: 1
                }
                InstrumentGraph {
                    width: parent.width
                    height: 124
                    samples: root.responseLabSource === "live" ? root.historySamples : root.previewSamples
                    series: [
                        {
                            field: "physical",
                            color: deck.textMuted
                        },
                        {
                            field: "predictedMappedOutput",
                            color: deck.attention
                        },
                        {
                            field: "adaptiveOutput",
                            color: deck.healthy
                        }
                    ]
                }
            }
        }
    }

    FlightDeckDialog {
        id: renamePresetDialog
        objectName: "adaptiveRenamePresetDialog"
        tokens: deck
        heading: "Rename Response Preset"
        tone: root.presetError.length > 0 ? "attention" : "informational"
        preferredWidth: 430
        contentItem: ColumnLayout {
            width: renamePresetDialog.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: "Names must be unique and cannot reuse a built-in Response Preset name."
                color: deck.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            TextField {
                id: renamePresetInput
                objectName: "adaptiveRenamePresetInput"
                Layout.fillWidth: true
                text: root.renamePresetDraft
                onTextEdited: root.renamePresetDraft = text
                onAccepted: { root.commitPresetRename(); focus = false }
                color: deck.textPrimary
                background: Rectangle {
                    radius: deck.radiusControl
                    color: deck.primarySurface
                    border.width: renamePresetInput.activeFocus ? 2 : 1
                    border.color: root.presetError.length > 0 ? deck.attention : renamePresetInput.activeFocus ? deck.focus : deck.border
                }
            }
            Text {
                visible: root.presetError.length > 0
                Layout.fillWidth: true
                text: root.presetError
                color: deck.attention
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: renamePresetDialog.close() }
                DeckButton {
                    text: "RENAME"
                    enabled: renamePresetInput.text.trim().length > 0
                    onClicked: root.commitPresetRename()
                }
            }
        }
        onOpened: { root.presetError = ""; renamePresetInput.forceActiveFocus(); renamePresetInput.selectAll(); }
    }

    Item {
        id: pageContent
        x: deck.space24
        width: Math.max(0, root.width - deck.space48)
        implicitHeight: testLabCard.y + testLabCard.height
        height: implicitHeight

        Rectangle {
            id: adaptiveHeroCard
            y: 0
            width: parent.width
            implicitHeight: heroContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.primarySurface
            border.color: deck.border
            Column {
                id: heroContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Configured response"
                            color: deck.textPrimary
                            font.pixelSize: 25
                            font.bold: true
                            font.family: deck.displayFont
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Select a device, profile, and axis, then review the configured limits and response preview."
                            color: deck.textSecondary
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    Rectangle {
                        implicitWidth: 136
                        implicitHeight: 56
                        radius: deck.radiusControl
                        color: root.effective().enabled ? Qt.rgba(deck.healthy.r, deck.healthy.g, deck.healthy.b, 0.12) : deck.secondarySurface
                        border.color: root.effective().enabled ? deck.healthy : deck.border
                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                text: root.effective().enabled ? "ADAPTIVE ON" : "ADAPTIVE OFF"
                                color: root.effective().enabled ? deck.healthy : deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Text {
                                text: root.effective().enabled ? "READY FOR INPUT" : "PREDICTOR IDLE"
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: deck.divider
                }
                Flow {
                    objectName: "adaptiveContextMetrics"
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    MetricTile {
                        objectName: "adaptiveContextMetricCategory"
                        caption: "CATEGORY"
                        value: String(root.presentationValue("category", root.liveState.category || "General"))
                        detail: "Active context"
                        tone: deck.accent
                    }
                    MetricTile {
                        objectName: "adaptiveContextMetricProfile"
                        caption: "PROFILE"
                        value: String(root.presentationValue("profile", root.liveState.profile || "No profile"))
                        detail: "Viewing / editing target"
                        tone: deck.textPrimary
                    }
                    MetricTile {
                        caption: "AXIS"
                        value: String(root.presentationValue("axis", root.liveState.axisLabel || "Axis"))
                        detail: "Physical control"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "CONFIGURATION"
                        value: String(root.scopeInfo().source || root.scopeInfo().presetId || "Inherited")
                        detail: root.editScope === "global" ? "Application default" : "Effective source"
                        tone: deck.healthy
                    }
                }
            }
        }

        Rectangle {
            id: adaptiveContextCard
            y: adaptiveHeroCard.y + adaptiveHeroCard.height + deck.space16
            width: parent.width
            implicitHeight: contextContent.implicitHeight + deck.space24
            height: implicitHeight
            radius: deck.radiusCard
            color: deck.secondarySurface
            border.color: deck.border
            Column {
                id: contextContent
                anchors.fill: parent
                anchors.margins: deck.space12
                spacing: deck.space8
                SectionLabel {
                            textValue: "SELECTED DEVICE CONTEXT"
                }
                Flow {
                    objectName: "adaptiveContextSelectors"
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    Column {
                        objectName: "adaptiveSelectedDeviceContext"
                        width: Math.max(180, Math.min(260, (parent.width - deck.space24) / 3))
                        spacing: 4
                        SectionLabel { textValue: "SELECTED DEVICE" }
                        Rectangle {
                            width: parent.width
                            height: deck.controlHeight
                            radius: deck.radiusControl
                            color: deck.primarySurface
                            border.color: deck.border
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: deck.space12
                                anchors.rightMargin: deck.space12
                                text: backendObject.selectedDeviceLabel
                                color: deck.textPrimary
                                font.family: deck.bodyFont
                                font.pixelSize: 10
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Column {
                        objectName: "adaptiveContextTarget"
                        width: Math.max(180, Math.min(300, (parent.width - deck.space24) / 3))
                        spacing: 4
                        SectionLabel {
                            textValue: root.editScope === "category" ? "CATEGORY" : root.editScope === "preset" ? "RESPONSE PRESET" : root.editScope === "global" ? "PROFILE SOURCE" : "PROFILE"
                        }
                        DeckCombo {
                            objectName: "adaptiveTargetSelector"
                            width: parent.width
                            model: root.targetChoices()
                            textRole: "label"
                            valueRole: "id"
                            currentIndex: root.targetIndex()
                            onChoiceActivated: function (index, value) {
                                root.targetId = String(value);
                                root.setPreview();
                                popup.close();
                            }
                        }
                    }
                    Column {
                        objectName: "adaptiveContextAxis"
                        width: Math.max(160, Math.min(240, (parent.width - deck.space24) / 3))
                        spacing: 4
                        SectionLabel { textValue: "AXIS" }
                        DeckCombo {
                            objectName: "adaptiveAxisSelector"
                            width: parent.width
                            model: backendObject.axes
                            textRole: "label"
                            valueRole: "index"
                            currentIndex: root.axisModelIndex(backendObject.selectedAxisIndex)
                            onChoiceActivated: function (index, value) {
                                root.selectAxisModelIndex(index);
                                popup.close();
                            }
                        }
                    }
                    Column {
                        objectName: "adaptiveContextLevel"
                        width: Math.max(180, Math.min(240, (parent.width - deck.space24) / 3))
                        spacing: 4
                        SectionLabel {
                            textValue: "CONFIGURATION LAYER"
                        }
                        DeckCombo {
                            objectName: "adaptiveEditScopeSelector"
                            width: parent.width
                            model: [
                                {
                                    label: "Application defaults",
                                    value: "global"
                                },
                                {
                                    label: "Category",
                                    value: "category"
                                },
                                {
                                    label: "Game profile",
                                    value: "profile"
                                },
                                {
                                    label: "Response preset",
                                    value: "preset"
                                }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            currentIndex: root.editScope === "global" ? 0 : root.editScope === "category" ? 1 : root.editScope === "preset" ? 3 : 2
                            onChoiceActivated: function (index, value) {
                                root.editScope = String(value);
                                root.targetId = "";
                                root.setPreview();
                                popup.close();
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    implicitHeight: runtimeSourceText.implicitHeight + deck.space16
                    radius: deck.radiusControl
                    color: deck.primarySurface
                    border.color: deck.border
                    Text {
                        id: runtimeSourceText
                        anchors.fill: parent
                        anchors.margins: deck.space8
                        text: root.runtimeSourceSummary()
                        color: deck.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            id: adaptiveBasicCard
            y: adaptiveContextCard.y + adaptiveContextCard.height + deck.space16
            width: parent.width
            implicitHeight: basicContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.elevatedSurface
            border.color: root.effective().enabled ? deck.healthy : deck.border
            Column {
                id: basicContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            textValue: "BASIC RESPONSE · CONFIGURED LIMITS"
                        }
                        Text {
                            text: "What this preset is configured to do"
                            color: deck.textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.effective().enabled ? "These are fixed preset limits for the selected axis. Live behavior is shown below when input is available." : "Adaptive Response is off. Its saved preset limits remain available for review."
                            color: deck.textSecondary
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    Row {
                        spacing: deck.space8
                        Layout.alignment: Qt.AlignVCenter
                        Text {
                            text: root.effective().enabled ? "ON" : "OFF"
                            color: root.effective().enabled ? deck.healthy : deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 12
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        DeckSwitch {
                            objectName: "flightDeckAdaptiveEnabled"
                            checked: !!root.effective().enabled
                            onToggled: root.updateParameter("enabled", checked)
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: deck.divider
                }
                SectionLabel {
                    textValue: "RESPONSE PRESET"
                }
                Flow {
                    objectName: "adaptivePresetFlow"
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space8
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    Repeater {
                        model: backendObject.adaptiveResponsePresets || []
                        delegate: Button {
                            id: presetButton
                            required property var modelData
                            objectName: "adaptivePresetButton_" + modelData.id
                            // Do not size the Button from its anchored contentItem: the
                            // resulting Button -> Column -> Button implicit-size cycle only
                            // surfaces on the native Windows scene graph as a polish loop.
                            // The Flow remains responsive and the two labels elide/wrap inside
                            // this comfortably sized control.
                            implicitWidth: 156
                            implicitHeight: 76
                            padding: deck.space12
                            enabled: root.editScope !== "preset"
                            focusPolicy: Qt.StrongFocus
                            contentItem: Column {
                                id: presetText
                                anchors.fill: parent
                                // Button.padding does not automatically inset an
                                // anchored custom contentItem. Preserve the
                                // control's shared safe area explicitly so both
                                // preset labels stay clear of the rounded edge.
                                anchors.margins: presetButton.padding
                                spacing: 4
                                Text {
                                    objectName: "adaptivePresetTitle_" + presetButton.modelData.id
                                    width: parent.width
                                    text: presetButton.modelData.name.toUpperCase()
                                    color: presetButton.checked ? deck.primarySurface : deck.textPrimary
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 10
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    objectName: "adaptivePresetDescription_" + presetButton.modelData.id
                                    width: parent.width
                                    text: presetButton.modelData.id === "extreme" ? "EXPERIMENTAL" : presetButton.modelData.description
                                    color: presetButton.checked ? deck.primarySurface : deck.textSecondary
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                            checkable: true
                            checked: String(root.scopeInfo().presetId || "") === String(modelData.id)
                            background: Rectangle {
                                radius: deck.radiusControl
                                color: presetButton.checked ? deck.accent : presetButton.hovered ? deck.selected : deck.primarySurface
                                border.width: presetButton.activeFocus ? 2 : 1
                                border.color: presetButton.activeFocus ? deck.focus : presetButton.checked ? deck.accent : deck.border
                            }
                            onClicked: root.applySimplePreset(modelData.id)
                            FlightDeckTooltip {
                                tokens: deck
                                visible: parent.hovered
                                text: modelData.description
                            }
                        }
                    }
                }
                Flow {
                    objectName: "adaptiveBasicMetrics"
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    MetricTile {
                        caption: "MAXIMUM HORIZON"
                        value: root.numericOr(root.effective().maximumHorizonMs, 0).toFixed(1) + " ms"
                        detail: "Preset limit"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "MAXIMUM LEAD"
                        value: root.percent(root.numericOr(root.effective().maximumLead, 0))
                        detail: "Mapped-output limit"
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "PREDICTOR"
                        value: String(root.effective().model || "Configured").toUpperCase()
                        detail: "Preset model"
                        tone: deck.accent
                    }
                    MetricTile {
                        caption: "REVERSAL RESPONSE"
                        value: root.effective().turningPointProtection ? "PROTECTED" : "STANDARD"
                        detail: "Preset behavior"
                        tone: root.effective().turningPointProtection ? deck.healthy : deck.textPrimary
                    }
                    MetricTile {
                        caption: "NORMAL RESPONSE"
                        value: root.percent(root.numericOr(root.effective().normalMovementResponse, 0))
                        detail: "Everyday movement"
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "RAPID RESPONSE"
                        value: root.percent(root.numericOr(root.effective().rapidMovementResponse, 0))
                        detail: "Maneuver authority"
                        tone: deck.attention
                    }
                }
            }
        }

        Rectangle {
            id: staticResponsePreviewCard
            objectName: "staticResponsePreviewCard"
            y: adaptiveBasicCard.y + adaptiveBasicCard.height + deck.space16
            width: parent.width
            implicitHeight: previewContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.primarySurface
            border.color: deck.border
            Column {
                id: previewContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            textValue: "RESPONSE PREVIEW · STATIC / DETERMINISTIC"
                        }
                        Text {
                            text: "What this configuration generally does"
                            color: deck.textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Preview uses simulated movement. Connect a controller for live input."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                    Column {
                        Layout.preferredWidth: 240
                        spacing: 4
                        SectionLabel {
                            textValue: "SYNTHETIC MOTION"
                        }
                        DeckCombo {
                            objectName: "adaptiveScenarioSelector"
                            width: parent.width
                            model: ["Human-Like Rapid Reversal", "Fast Full Sweep", "Very-Fast Full Sweep", "Same-Side Reversal", "Rapid Center Crossing", "Evasive Left/Right", "Sudden Stop", "Precision Correction"]
                            currentIndex: Math.max(0, model.indexOf(root.scenario))
                            onChoiceActivated: function (index, value) {
                                root.scenario = String(value);
                                root.setPreview();
                                popup.close();
                            }
                        }
                    }
                }
                Flow {
                    width: parent.width
                    spacing: deck.space8
                    SectionLabel { textValue: "VISIBLE TRACES" }
                    TraceLegend { objectName: "flightDeckStaticTracePhysical"; text: "PHYSICAL"; tone: deck.textMuted; checked: root.showPhysicalTrace; onClicked: root.showPhysicalTrace = checked }
                    TraceLegend { objectName: "flightDeckStaticTracePredicted"; text: "PREDICTED MAPPED"; tone: deck.attention; checked: root.showPredictedTrace; onClicked: root.showPredictedTrace = checked }
                    TraceLegend { objectName: "flightDeckStaticTraceOutput"; text: "FINAL OUTPUT"; tone: deck.healthy; checked: root.showOutputTrace; onClicked: root.showOutputTrace = checked }
                    TraceLegend { objectName: "flightDeckStaticTraceBaseline"; text: "BASELINE"; tone: deck.informational; checked: root.showBaselineTrace; onClicked: root.showBaselineTrace = checked }
                }
                Rectangle {
                    width: parent.width
                    implicitHeight: 226
                    radius: deck.radiusCard
                    color: deck.secondarySurface
                    border.color: deck.border
                    Column {
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        Row {
                            width: parent.width
                            Text {
                                width: parent.width * 0.5
                                text: "STATIC AXIS POSITION"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                width: parent.width * 0.5
                                text: "SCENARIO · " + root.scenario.toUpperCase()
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                        }
                        InstrumentGraph {
                            width: parent.width
                            height: 158
                            samples: root.previewSamples
                            series: {
                                const lines = [];
                                if (root.showPhysicalTrace)
                                    lines.push({
                                        field: "physical",
                                        color: deck.textMuted,
                                        width: 2
                                    });
                                if (root.showPredictedTrace)
                                    lines.push({
                                        field: "predictedMappedOutput",
                                        color: deck.attention,
                                        width: 2
                                    });
                                if (root.showOutputTrace)
                                    lines.push({
                                        field: "adaptiveOutput",
                                        color: deck.healthy,
                                        width: 2.4
                                    });
                                if (root.showBaselineTrace)
                                    lines.push({
                                        field: "baselineOutput",
                                        color: deck.informational,
                                        width: 1.5
                                    });
                                return lines;
                            }
                        }
                        Row {
                            spacing: deck.space16
                            Text {
                                text: "— Physical input"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                            }
                            Text {
                                text: "— Predicted mapped"
                                color: deck.attention
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                            }
                            Text {
                                text: "— Final adaptive output"
                                color: deck.healthy
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                            }
                            Text {
                                visible: root.showBaselineTrace
                                text: "— Baseline output"
                                color: deck.informational
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    implicitHeight: 112
                    radius: deck.radiusCard
                    color: deck.secondarySurface
                    border.color: deck.border
                    Column {
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        RowLayout {
                            width: parent.width
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "MAPPED-OUTPUT LEAD"
                                    color: deck.textPrimary
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    text: "Magnified against the configured lead limit so small predictive changes remain legible."
                                    color: deck.textSecondary
                                    font.pixelSize: 9
                                }
                            }
                            Text {
                                text: "±" + root.percent(root.numericOr(root.effective().maximumLead, 0.01))
                                color: deck.attention
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                        InstrumentGraph {
                            width: parent.width
                            height: 54
                            samples: root.previewSamples
                            lowerBound: -Math.max(0.001, root.numericOr(root.effective().maximumLead, 0.01))
                            upperBound: Math.max(0.001, root.numericOr(root.effective().maximumLead, 0.01))
                            series: [
                                {
                                    field: "appliedLead",
                                    color: deck.accent,
                                    width: 2
                                }
                            ]
                        }
                    }
                }
                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        text: "Preview recomputes when context, a preset, or a tuning value changes—not on controller reports."
                        color: deck.textMuted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: root.previewSamples.length + " SAMPLES"
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                }
            }
        }

        Rectangle {
            id: adaptiveComparisonCard
            objectName: "adaptiveComparisonCard"
            y: staticResponsePreviewCard.y + staticResponsePreviewCard.height + deck.space16
            width: parent.width
            implicitHeight: comparisonContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.secondarySurface
            border.color: deck.border
            Column {
                id: comparisonContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            textValue: "A / B STATIC COMPARISON"
                        }
                        Text {
                            text: "Compare before you commit"
                            color: deck.textPrimary
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "A is the editing context above. B is another existing Response Preset or profile state; comparison is read-only."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                    Column {
                        Layout.preferredWidth: 290
                        spacing: 4
                        SectionLabel {
                            textValue: "COMPARE B"
                        }
                        DeckCombo {
                            objectName: "adaptiveComparisonSelector"
                            width: parent.width
                            model: root.comparisonChoices()
                            textRole: "label"
                            valueRole: "id"
                            currentIndex: root.comparisonIndex()
                            onChoiceActivated: function (index, value) {
                                const choice = root.comparisonChoices()[index];
                                if (!choice)
                                    return;
                                root.comparisonScope = String(choice.scope);
                                root.comparisonTargetId = String(choice.id);
                                root.setPreview();
                                popup.close();
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    implicitHeight: 176
                    radius: deck.radiusCard
                    color: deck.primarySurface
                    border.color: deck.border
                    Canvas {
                        id: comparisonGraph
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        antialiasing: true
                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();
                            ctx.fillStyle = deck.primarySurface;
                            ctx.fillRect(0, 0, width, height);
                            const padding = 8;
                            const plotWidth = Math.max(1, width - padding * 2);
                            const plotHeight = Math.max(1, height - padding * 2);
                            ctx.strokeStyle = deck.divider;
                            ctx.lineWidth = 1;
                            for (let line = 1; line < 4; ++line) {
                                const y = padding + plotHeight * line / 4;
                                ctx.beginPath();
                                ctx.moveTo(padding, y);
                                ctx.lineTo(padding + plotWidth, y);
                                ctx.stroke();
                            }
                            function trace(samples, field, color, dashed, widthValue) {
                                if (!samples || samples.length === 0)
                                    return;
                                ctx.strokeStyle = color;
                                ctx.lineWidth = widthValue;
                                ctx.setLineDash(dashed ? [5, 4] : []);
                                ctx.beginPath();
                                for (let index = 0; index < samples.length; ++index) {
                                    const value = Math.max(-1, Math.min(1, root.numericOr(samples[index][field], 0)));
                                    const x = padding + index * plotWidth / Math.max(1, samples.length - 1);
                                    const y = padding + plotHeight * (1 - (value + 1) * 0.5);
                                    if (index === 0)
                                        ctx.moveTo(x, y);
                                    else
                                        ctx.lineTo(x, y);
                                }
                                ctx.stroke();
                                ctx.setLineDash([]);
                            }
                            trace(root.previewSamples, "physical", deck.textMuted, false, 1.5);
                            trace(root.previewSamples, "predicted", deck.attention, false, 2);
                            trace(root.comparisonSamples, "predicted", deck.healthy, true, 2);
                        }
                        Connections {
                            target: root
                            function onPreviewSamplesChanged() {
                                comparisonGraph.requestPaint();
                            }
                            function onComparisonSamplesChanged() {
                                comparisonGraph.requestPaint();
                            }
                        }
                    }
                }
                Flow {
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    MetricTile {
                        caption: "A PEAK LEAD"
                        value: root.percent(root.numericOr(root.testLabMetrics.peakLead, 0))
                        detail: "Editing context"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "B PEAK LEAD"
                        value: root.percent(root.numericOr(root.comparisonTestLabMetrics.peakLead, 0))
                        detail: "Comparison target"
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "A − B PEAK"
                        value: root.percent(root.numericOr(root.testLabMetrics.peakLead, 0) - root.numericOr(root.comparisonTestLabMetrics.peakLead, 0))
                        detail: "Lead delta"
                        tone: deck.accent
                    }
                    MetricTile {
                        caption: "A − B MEDIAN"
                        value: root.percent(root.numericOr(root.testLabMetrics.medianLead, 0) - root.numericOr(root.comparisonTestLabMetrics.medianLead, 0))
                        detail: "Lead delta"
                        tone: deck.textPrimary
                    }
                }
                Row {
                    spacing: deck.space16
                    Text {
                        text: "— Physical baseline"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                    }
                    Text {
                        text: "— A predicted"
                        color: deck.attention
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                    }
                    Text {
                        text: "– – B predicted"
                        color: deck.healthy
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                    }
                }
            }
        }

        Rectangle {
            id: advancedTuningCard
            objectName: "flightDeckAdaptiveAdvancedCard"
            y: adaptiveComparisonCard.y + adaptiveComparisonCard.height + deck.space16
            width: parent.width
            implicitHeight: deck.space24 + advancedToggle.height + (root.advancedExpanded ? deck.space12 + advancedTuningContent.implicitHeight : 0)
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.primarySurface
            border.color: deck.border
            Column {
                id: advancedContent
                x: deck.space12
                y: deck.space12
                width: parent.width - deck.space24
                height: advancedToggle.height + (root.advancedExpanded ? spacing + advancedTuningContent.implicitHeight : 0)
                spacing: deck.space12
                Button {
                    id: advancedToggle
                    objectName: "flightDeckAdaptiveAdvancedToggle"
                    width: parent.width
                    implicitHeight: 52
                    focusPolicy: Qt.StrongFocus
                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: deck.space12
                        anchors.rightMargin: deck.space12
                        Text {
                            text: root.advancedExpanded ? "⌄" : "›"
                            color: deck.accent
                            font.pixelSize: 22
                            font.bold: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "ADVANCED TUNING"
                                color: deck.textPrimary
                                font.family: deck.telemetryFont
                                font.pixelSize: 11
                                font.bold: true
                            }
                            Text {
                                text: root.advancedExpanded ? "Every existing tuning control is grouped by engineering purpose." : "Reveal complete predictor, motion, reversal, and safety controls."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            text: root.advancedExpanded ? "COLLAPSE" : "EXPAND"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                        }
                    }
                    background: Rectangle {
                        radius: deck.radiusControl
                        color: advancedToggle.hovered ? deck.selected : deck.secondarySurface
                        border.color: advancedToggle.activeFocus ? deck.focus : deck.border
                        border.width: advancedToggle.activeFocus ? 2 : 1
                    }
                    onClicked: root.advancedExpanded = !root.advancedExpanded
                }
                Column {
                    id: advancedTuningContent
                    visible: root.advancedExpanded
                    width: parent.width
                    spacing: deck.space12
                    Rectangle {
                        id: presetWorkshopCard
                        width: parent.width
                        implicitHeight: presetWorkshopContent.implicitHeight + deck.space24
                        height: implicitHeight
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        Column {
                            id: presetWorkshopContent
                            x: deck.space12
                            y: deck.space12
                            width: parent.width - deck.space24
                            height: presetWorkshopHeader.height + (root.presetWorkshopExpanded ? deck.space8 + presetWorkshopDetails.implicitHeight : 0)
                            spacing: deck.space8
                            RowLayout {
                                id: presetWorkshopHeader
                                width: parent.width
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "CUSTOM RESPONSE PRESETS"
                                        color: deck.textPrimary
                                        font.pixelSize: 14
                                        font.bold: true
                                    }
                                    Text {
                                        text: "Capture the current effective configuration for all axes, then edit its native preset layer here."
                                        color: deck.textSecondary
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                                DeckButton {
                                    objectName: "flightDeckAdaptivePresetWorkshopToggle"
                                    text: root.presetWorkshopExpanded ? "CLOSE WORKSHOP" : "MANAGE PRESETS"
                                    subdued: true
                                    onClicked: root.presetWorkshopExpanded = !root.presetWorkshopExpanded
                                }
                            }
                            Column {
                                id: presetWorkshopDetails
                                visible: root.presetWorkshopExpanded
                                width: parent.width
                                spacing: deck.space8
                                RowLayout {
                                    width: parent.width
                                    TextField {
                                        id: presetNameInput
                                        objectName: "flightDeckAdaptivePresetName"
                                        Layout.preferredWidth: 220
                                        placeholderText: "Preset name"
                                        text: root.presetNameDraft
                                        onTextEdited: root.presetNameDraft = text
                                        onAccepted: { root.commitPresetRename(); focus = false }
                                        Keys.onReturnPressed: function(event) { root.commitPresetRename(); focus = false; event.accepted = true; }
                                        color: deck.textPrimary
                                        background: Rectangle {
                                            radius: deck.radiusControl
                                            color: deck.primarySurface
                                            border.color: presetNameInput.activeFocus ? deck.focus : deck.border
                                        }
                                    }
                                    TextField {
                                        id: presetDescriptionInput
                                        objectName: "flightDeckAdaptivePresetDescription"
                                        Layout.fillWidth: true
                                        placeholderText: "Description (optional)"
                                        text: root.presetDescriptionDraft
                                        onTextEdited: root.presetDescriptionDraft = text
                                        onAccepted: focus = false
                                        color: deck.textPrimary
                                        background: Rectangle {
                                            radius: deck.radiusControl
                                            color: deck.primarySurface
                                            border.color: presetDescriptionInput.activeFocus ? deck.focus : deck.border
                                        }
                                    }
                                    DeckButton {
                                        objectName: "flightDeckAdaptivePresetSave"
                                        text: "SAVE CURRENT"
                                        enabled: root.presetNameDraft.trim().length > 0
                                        onClicked: {
                                            root.presetError = "";
                                            if (backendObject.saveAdaptiveResponsePreset(root.presetNameDraft, root.presetDescriptionDraft)) {
                                                root.presetNameDraft = "";
                                                root.presetDescriptionDraft = "";
                                                root.setPreview();
                                            } else
                                                root.presetError = "Use a unique preset name of 64 characters or fewer.";
                                        }
                                    }
                                }
                                Text {
                                    visible: root.presetError.length > 0
                                    width: parent.width
                                    text: root.presetError
                                    color: deck.attention
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: (backendObject.adaptiveResponsePresets || []).filter(function (preset) {
                                        return !preset.builtIn;
                                    })
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: parent.width
                                        implicitHeight: presetRow.implicitHeight + deck.space16
                                        radius: deck.radiusControl
                                        color: deck.primarySurface
                                        border.color: deck.border
                                        Column {
                                            id: presetRow
                                            anchors.fill: parent
                                            anchors.margins: deck.space8
                                            spacing: 4
                                            RowLayout {
                                                width: parent.width
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                    color: deck.textPrimary
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                }
                                                DeckButton {
                                                    objectName: "adaptivePresetWorkshopEdit_" + modelData.id
                                                    text: "EDIT"
                                                    subdued: true
                                                    onClicked: {
                                                        flightDeckRoot.editScope = "preset";
                                                        flightDeckRoot.targetId = modelData.id;
                                                        flightDeckRoot.setPreview();
                                                    }
                                                }
                                                DeckButton {
                                                    objectName: "adaptivePresetWorkshopRename_" + modelData.id
                                                    text: "RENAME"
                                                    subdued: true
                                                    onClicked: {
                                                        flightDeckRoot.renamePresetId = modelData.id;
                                                        flightDeckRoot.renamePresetDraft = modelData.name;
                                                        renamePresetDialog.open();
                                                    }
                                                }
                                                DeckButton {
                                                    objectName: "adaptivePresetWorkshopDuplicate_" + modelData.id
                                                    text: "DUPLICATE"
                                                    subdued: true
                                                    onClicked: {
                                                        backendObject.duplicateAdaptiveResponsePreset(modelData.id, modelData.name + " Copy");
                                                        flightDeckRoot.setPreview();
                                                    }
                                                }
                                                DeckButton {
                                                    objectName: "adaptivePresetWorkshopDelete_" + modelData.id
                                                    text: "DELETE"
                                                    subdued: true
                                                    enabled: backendObject.adaptiveResponsePresetDependencies(modelData.id).length === 0
                                                    onClicked: {
                                                        backendObject.deleteAdaptiveResponsePreset(modelData.id);
                                                        if (flightDeckRoot.editScope === "preset" && flightDeckRoot.targetId === modelData.id)
                                                            flightDeckRoot.targetId = "";
                                                        flightDeckRoot.setPreview();
                                                    }
                                                }
                                            }
                                            Text {
                                                width: parent.width
                                                text: modelData.description || "No description"
                                                color: deck.textSecondary
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                visible: backendObject.adaptiveResponsePresetDependencies(modelData.id).length > 0
                                                width: parent.width
                                                text: "IN USE · " + backendObject.adaptiveResponsePresetDependencies(modelData.id).join(" · ")
                                                color: deck.attention
                                                font.pixelSize: 8
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: (backendObject.adaptiveResponsePresets || []).filter(function (preset) {
                                        return !preset.builtIn;
                                    }).length === 0
                                    width: parent.width
                                    text: "No custom Response Presets yet. Save the effective configuration above to create one."
                                    color: deck.textMuted
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        implicitHeight: predictorContent.implicitHeight + deck.space24
                        radius: deck.radiusCard
                        color: deck.secondarySurface
                        border.color: deck.border
                        ColumnLayout {
                            id: predictorContent
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "PREDICTOR MODEL"
                                        color: deck.textPrimary
                                        font.pixelSize: 14
                                        font.bold: true
                                    }
                                    Text {
                                        text: "Select only among the current authoritative models. This changes configuration, never Flight Deck logic."
                                        color: deck.textSecondary
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                                DeckCombo {
                                    objectName: "adaptivePredictorSelector"
                                    Layout.preferredWidth: 210
                                    model: ["auto", "velocity", "alpha-beta", "alpha-beta-gamma"]
                                    currentIndex: Math.max(0, model.indexOf(root.effective().model || "auto"))
                                    onChoiceActivated: function (index, value) {
                                        root.updateParameter("model", value);
                                        popup.close();
                                    }
                                }
                                DeckButton {
                                    text: root.editScope === "global" ? "RESET" : root.inheritedHere("model") ? "INHERITED" : "INHERIT"
                                    subdued: true
                                    enabled: root.editScope === "global" || !root.inheritedHere("model")
                                    onClicked: root.restoreInherited("model")
                                }
                            }
                        }
                    }
                    TuningGroup {
                        title: "FLIGHT RESPONSE"
                        detail: "The three everyday-flight controls are independent, inherited with the selected layer, and always remain inside the existing safety protections."
                        rows: root.parametersFor("Flight response")
                    }
                    TuningGroup {
                        title: "PREDICTION"
                        detail: "Limits and output-space safety for the effective response."
                        rows: root.parametersFor("Prediction")
                    }
                    TuningGroup {
                        title: "MOTION DETECTION"
                        detail: "How the existing estimator recognizes meaningful controller movement."
                        rows: root.parametersFor("Motion detection")
                    }
                    TuningGroup {
                        title: "REVERSAL AND SETTLING"
                        detail: "Existing controls for stale-lead cancellation, braking, and return to rest."
                        rows: root.parametersFor("Reversal and settling")
                    }
                    TuningGroup {
                        title: "COHERENT MOTION"
                        detail: "Existing bounded assistance for onset and sustained predictable movement."
                        rows: root.parametersFor("Coherent motion")
                    }
                    TuningGroup {
                        title: "SAFETY AND TURNS"
                        detail: "Safety limits are enforced by the engine; these controls tune only the existing exposed policy."
                        rows: root.parametersFor("Safety and turns")
                    }
                    RowLayout {
                        width: parent.width
                        DeckButton {
                            text: "RESTORE THIS LAYER"
                            subdued: true
                            onClicked: {
                                backendObject.resetAdaptiveResponseAxisAtContext(root.editScope, root.selectedTargetId(), root.state.axis);
                                root.setPreview();
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Restore affects this editing layer and selected axis only."
                            color: deck.textMuted
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Rectangle {
            id: telemetrySection
            objectName: "flightDeckAdaptiveTelemetryCard"
            y: advancedTuningCard.y + advancedTuningCard.height + deck.space16
            width: parent.width
            implicitHeight: telemetryContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.primarySurface
            border.color: deck.border
            Column {
                id: telemetryContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            textValue: "LIVE TELEMETRY · BOUNDED UI SNAPSHOT"
                        }
                        Text {
                            text: "Current behavior at a glance"
                            color: deck.textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: !root.liveInputAvailable() ? "Awaiting controller input. Configured limits above remain unchanged."
                                : root.effective().enabled ? "Live values are observational and do not change the predictor." : "Adaptive Response is off. Physical input may remain available, but prediction is not active."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        text: root.effective().enabled ? String(root.telemetry.state || "Stable").toUpperCase() : "OFF"
                        color: root.effective().enabled ? deck.accent : deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Flow {
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space8
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    MetricTile {
                        caption: "MAPPED PHYSICAL"
                        value: root.liveInputAvailable() ? root.percent(root.telemetry.baselineOutput) : "—"
                        detail: "Physical through curve"
                        tone: deck.textPrimary
                    }
                    MetricTile {
                        caption: "PREDICTOR OUTPUT"
                        value: root.liveInputAvailable() && root.effective().enabled ? root.percent(root.telemetry.predictedMappedOutput) : "—"
                        detail: "Predicted through curve"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "FINAL OUTPUT"
                        value: root.liveInputAvailable() && root.effective().enabled ? root.percent(root.telemetry.adaptiveOutput) : "—"
                        detail: "Mapped output"
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "VELOCITY"
                        value: root.effective().enabled ? root.numericOr(root.telemetry.velocity, 0).toFixed(2) + " /s" : "—"
                        detail: "Axis units"
                        tone: deck.accent
                    }
                    MetricTile {
                        caption: "ACCELERATION"
                        value: root.effective().enabled ? root.numericOr(root.telemetry.acceleration, 0).toFixed(1) + " /s²" : "—"
                        detail: "Axis units"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "PRE-CAP REQUEST"
                        value: root.effective().enabled ? root.percent(root.telemetry.requestedLead) : "—"
                        detail: "Estimator lead request"
                        tone: deck.attention
                    }
                    MetricTile {
                        caption: "POST-CAP LEAD"
                        value: root.effective().enabled ? root.percent(root.telemetry.cappedLead) : "—"
                        detail: "After safety cap"
                        tone: deck.healthy
                    }
                    MetricTile {
                        caption: "ENDPOINT TAPER"
                        value: root.effective().enabled ? Math.round(root.numericOr(root.telemetry.endpointTaper, 1) * 100) + "%" : "—"
                        detail: "Output headroom"
                        tone: deck.textPrimary
                    }
                    MetricTile {
                        caption: "SAFETY"
                        value: root.telemetry.safetyLimited ? "LIMITING" : root.telemetry.reversing ? "REVERSING" : "CLEAR"
                        detail: root.telemetry.leadLimited ? "Lead bounded" : "Engine state"
                        tone: root.telemetry.safetyLimited || root.telemetry.reversing ? deck.attention : deck.healthy
                    }
                }
                Flow {
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    Gauge {
                        caption: "ACTIVE HORIZON"
                        display: root.effective().enabled ? root.numericOr(root.telemetry.activeHorizonMs, 0).toFixed(1) + " ms" : "OFF"
                        value: root.numericOr(root.telemetry.activeHorizonMs, 0)
                        maximum: Math.max(0.1, root.numericOr(root.telemetry.maximumHorizonMs, root.numericOr(root.effective().maximumHorizonMs, 1)))
                        tone: deck.attention
                    }
                    Gauge {
                        caption: "MAPPED LEAD"
                        display: root.effective().enabled ? root.percent(root.telemetry.appliedLead) : "—"
                        value: Math.abs(root.numericOr(root.telemetry.appliedLead, 0))
                        maximum: Math.max(0.001, root.numericOr(root.telemetry.maximumLead, root.numericOr(root.effective().maximumLead, 1)))
                        tone: deck.healthy
                    }
                    Gauge {
                        caption: "CONFIDENCE"
                        display: root.effective().enabled ? Math.round(root.numericOr(root.telemetry.confidence, 0) * 100) + "%" : "—"
                        value: root.numericOr(root.telemetry.confidence, 0)
                        maximum: 1
                        tone: deck.accent
                    }
                    Gauge {
                        caption: "MOTION INTENSITY"
                        display: root.effective().enabled ? Math.round(root.numericOr(root.telemetry.motionIntensity, 0) * 100) + "%" : "—"
                        value: root.numericOr(root.telemetry.motionIntensity, 0)
                        maximum: 1
                        tone: deck.textPrimary
                    }
                    Gauge {
                        caption: "NORMAL MOVEMENT"
                        display: root.effective().enabled ? Math.round(root.numericOr(root.telemetry.normalMotionAuthority, 0) * 100) + "%" : "—"
                        value: root.numericOr(root.telemetry.normalMotionAuthority, 0)
                        maximum: 1
                        tone: deck.healthy
                    }
                    Gauge {
                        caption: "RAPID MOVEMENT"
                        display: root.effective().enabled ? Math.round(root.numericOr(root.telemetry.rapidMotionAuthority, 0) * 100) + "%" : "—"
                        value: root.numericOr(root.telemetry.rapidMotionAuthority, 0)
                        maximum: 1
                        tone: deck.attention
                    }
                }
            }
        }

        Rectangle {
            id: liveAnalysisSection
            objectName: "responseLabCard"
            y: telemetrySection.y + telemetrySection.height + deck.space16
            width: parent.width
            implicitHeight: analysisContent.implicitHeight + deck.space32
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.elevatedSurface
            border.color: deck.border
            Column {
                id: analysisContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            textValue: "LIVE ANALYSIS · ONE WORKSPACE"
                        }
                        Text {
                            text: "Response Lab"
                            color: deck.textPrimary
                            font.pixelSize: 19
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "One analysis surface. Data source changes the feed, not the surrounding information architecture or saved configuration."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                    DeckButton {
                        objectName: "responseLabInteractiveSource"
                        text: "INTERACTIVE"
                        subdued: root.responseLabSource !== "interactive"
                        onClicked: root.setResponseLabSource("interactive")
                    }
                    DeckButton {
                        objectName: "responseLabLiveSource"
                        text: "LIVE CONTROLLER"
                        subdued: root.responseLabSource !== "live"
                        onClicked: root.setResponseLabSource("live")
                    }
                }
                Rectangle {
                    id: responseLabEffectiveResponse
                    objectName: "responseLabEffectiveResponse"
                    width: parent.width
                    implicitHeight: sourceContent.implicitHeight + deck.space24
                    radius: deck.radiusCard
                    color: deck.primarySurface
                    border.color: deck.border
                    Column {
                        id: sourceContent
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        RowLayout {
                            width: parent.width
                            Text {
                                Layout.minimumWidth: 118
                                Layout.preferredWidth: 128
                                Layout.maximumWidth: 150
                                text: root.responseLabSource === "live" ? (root.historyPaused ? "PAUSED INSPECTION" : "LIVE CONTROLLER") : (root.simulatorReplaying ? "SYNTHETIC REPLAY" : root.simulatorPaused ? "INTERACTIVE PAUSED" : "INTERACTIVE LIVE")
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.minimumWidth: 220
                                Layout.fillWidth: true
                                text: root.responseLabSource === "live" ? "Existing 83 Hz bounded history; Flight Deck renders at a bounded display cadence." : "Isolated simulator using the same existing preview engine; no physical or vJoy output is written."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            DeckButton {
                                objectName: "adaptiveResponseMonitorButton"
                                text: "OPEN MONITOR"
                                subdued: true
                                onClicked: root.responseMonitorVisible = true
                            }
                        }
                        RowLayout {
                            width: parent.width
                            DeckButton {
                                text: "LIVE"
                                subdued: !(root.responseLabSource === "live" ? !root.historyPaused : !root.simulatorPaused)
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveReplaying = false;
                                        root.historyPaused = false;
                                        root.refreshHistory(false);
                                    } else {
                                        root.simulatorReplaying = false;
                                        root.simulatorPaused = false;
                                        root.refreshSimulator(false);
                                    }
                                }
                            }
                            DeckButton {
                                text: "RECORD"
                                subdued: !(root.responseLabSource === "live" ? root.liveRecording : root.simulatorRecording)
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveRecording = true;
                                        root.historyPaused = false;
                                        root.historyLastSequence = 0;
                                        root.historySamples = [];
                                        root.historyRevision += 1;
                                        root.refreshHistory(true);
                                    } else {
                                        root.simulatorReplaying = false;
                                        backendObject.adaptiveResponseSimulatorStartRecording();
                                        root.simulatorRecording = true;
                                        root.simulatorPaused = false;
                                        root.refreshSimulator(false);
                                    }
                                }
                            }
                            DeckButton {
                                text: "STOP"
                                subdued: true
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveRecording = false;
                                        root.liveReplaying = false;
                                        root.historyPaused = true;
                                    } else {
                                        backendObject.adaptiveResponseSimulatorStopRecording();
                                        root.simulatorRecording = false;
                                        root.simulatorReplaying = false;
                                        root.simulatorPaused = true;
                                    }
                                }
                            }
                            DeckButton {
                                text: "REPLAY"
                                subdued: !(root.responseLabSource === "live" ? root.liveReplaying : root.simulatorReplaying)
                                enabled: root.responseLabSource === "live" ? root.historySampleCount > 0 : backendObject.adaptiveResponseSimulatorRecording().length > 0
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveRecording = false;
                                        root.historyPaused = true;
                                        root.historyInspectIndex = 0;
                                        root.liveReplaying = true;
                                    } else {
                                        backendObject.adaptiveResponseSimulatorStopRecording();
                                        root.simulatorRecording = false;
                                        root.startReplay();
                                    }
                                }
                            }
                            DeckButton {
                                text: root.responseLabSource === "live" ? (root.historyPaused ? "RESUME" : "PAUSE") : (root.simulatorPaused ? "RESUME" : "PAUSE")
                                subdued: true
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveReplaying = false;
                                        root.historyPaused = !root.historyPaused;
                                        if (!root.historyPaused)
                                            root.refreshHistory(false);
                                    } else
                                        root.simulatorPaused = !root.simulatorPaused;
                                }
                            }
                            DeckButton {
                                text: "CLEAR"
                                subdued: true
                                onClicked: {
                                    if (root.responseLabSource === "live") {
                                        root.liveRecording = false;
                                        root.liveReplaying = false;
                                        root.historyPaused = true;
                                        root.historyLastSequence = 0;
                                        root.historySamples = [];
                                        root.historyRevision += 1;
                                        root.historyInspectIndex = -1;
                                    } else {
                                        backendObject.adaptiveResponseSimulatorClear();
                                        root.simulatorPaused = true;
                                        root.simulatorRecording = false;
                                        root.simulatorReplaying = false;
                                        root.simulatorLastSequence = 0;
                                        root.simulatorSamples = [];
                                        root.simulatorDisplaySamples = [];
                                        root.simulatorDisplayCount = 0;
                                        root.simulatorReplayCursor = 0;
                                        root.simulatorRevision += 1;
                                    }
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SectionLabel {
                                visible: root.responseLabSource === "interactive"
                                textValue: "SYNTHETIC RATE"
                            }
                            DeckCombo {
                                objectName: "adaptiveSourceRateSelector"
                                visible: root.responseLabSource === "interactive"
                                Layout.preferredWidth: 112
                                model: [
                                    {
                                        label: "250 Hz",
                                        rate: 250
                                    },
                                    {
                                        label: "125 Hz",
                                        rate: 125
                                    },
                                    {
                                        label: "60 Hz",
                                        rate: 60
                                    },
                                    {
                                        label: "30 Hz",
                                        rate: 30
                                    }
                                ]
                                textRole: "label"
                                valueRole: "rate"
                                currentIndex: root.simulatorSourceRate === 250 ? 0 : root.simulatorSourceRate === 125 ? 1 : root.simulatorSourceRate === 60 ? 2 : 3
                                onChoiceActivated: function (index, value) {
                                    root.simulatorSourceRate = Number(value);
                                    popup.close();
                                }
                            }
                        }
                        RowLayout {
                            visible: root.responseLabSource === "interactive"
                            width: parent.width
                            spacing: deck.space12
                            Text {
                                text: "MANUAL INPUT"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Slider {
                                id: inputSlider
                                objectName: "adaptiveSimulatorManualInput"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                from: -1
                                to: 1
                                stepSize: 0.001
                                value: root.simulatorInput
                                background: Rectangle {
                                    x: inputSlider.leftPadding
                                    y: (inputSlider.height - height) / 2
                                    width: inputSlider.availableWidth
                                    height: 6
                                    radius: 3
                                    color: deck.secondarySurface
                                    border.color: deck.border
                                    Rectangle {
                                        width: parent.width * inputSlider.visualPosition
                                        height: parent.height
                                        radius: parent.radius
                                        color: deck.accent
                                    }
                                }
                                handle: Rectangle {
                                    x: inputSlider.leftPadding + inputSlider.visualPosition * (inputSlider.availableWidth - width)
                                    y: (inputSlider.height - height) / 2
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: inputSlider.pressed ? deck.accent : deck.elevatedSurface
                                    border.color: deck.accent
                                }
                                onMoved: {
                                    root.simulatorInput = value;
                                    if (!root.simulatorPaused && !root.simulatorReplaying)
                                        root.sampleSimulator();
                                }
                            }
                            Text {
                                text: root.percent(root.simulatorInput)
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        RowLayout {
                            visible: root.responseLabSource === "live"
                            width: parent.width
                            Text {
                                Layout.fillWidth: true
                                text: root.presentationValue("controllerAvailable", backendObject.physicalConnected) ? "Latest physical-input snapshots remain observable while mapping is off, suspended, or vJoy is unavailable." : "CONTROLLER UNAVAILABLE · settings are preserved, but live snapshots are not available."
                                color: root.presentationValue("controllerAvailable", backendObject.physicalConnected) ? deck.textSecondary : deck.attention
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: [2, 5, 10, 30]
                                delegate: DeckButton {
                                    required property var modelData
                                    text: modelData + " S"
                                    subdued: root.historyWindowSeconds !== modelData
                                    onClicked: {
                                        root.historyWindowSeconds = modelData;
                                        root.refreshHistory(true);
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: root.responseLabSource === "live" && root.historyPaused && root.historySampleCount > 1
                            width: parent.width
                            spacing: deck.space12
                            Text {
                                text: "INSPECT"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Slider {
                                id: historyInspectSlider
                                objectName: "adaptiveHistoryInspect"
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0, root.historySampleCount - 1)
                                stepSize: 1
                                value: Math.max(0, root.historyInspectIndex)
                                background: Rectangle {
                                    x: historyInspectSlider.leftPadding
                                    y: (historyInspectSlider.height - height) / 2
                                    width: historyInspectSlider.availableWidth
                                    height: 5
                                    radius: 3
                                    color: deck.secondarySurface
                                    border.color: deck.border
                                    Rectangle {
                                        width: parent.width * historyInspectSlider.visualPosition
                                        height: parent.height
                                        radius: parent.radius
                                        color: deck.attention
                                    }
                                }
                                handle: Rectangle {
                                    x: historyInspectSlider.leftPadding + historyInspectSlider.visualPosition * (historyInspectSlider.availableWidth - width)
                                    y: (historyInspectSlider.height - height) / 2
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: historyInspectSlider.pressed ? deck.attention : deck.elevatedSurface
                                    border.color: deck.attention
                                }
                                onMoved: root.historyInspectIndex = Math.round(value)
                            }
                            Text {
                                text: (Math.max(0, root.historyInspectIndex) + 1) + " / " + root.historySampleCount
                                color: deck.attention
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    implicitHeight: axisGraphContent.implicitHeight + deck.space24
                    radius: deck.radiusCard
                    color: deck.primarySurface
                    border.color: deck.border
                    Column {
                        id: axisGraphContent
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        RowLayout {
                            width: parent.width
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "AXIS POSITION"
                                    color: deck.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                Text {
                                    text: "Physical input and mapped-output values share one normalized axis scale."
                                    color: deck.textSecondary
                                    font.pixelSize: 9
                                }
                            }
                            Text {
                                text: root.responseLabSource === "live" ? "CHRONOLOGICAL · NEWEST RIGHT" : "INTERACTIVE HISTORY"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                                font.bold: true
                            }
                        }
                        Flow {
                            flow: Flow.LeftToRight
                            width: parent.width
                            spacing: deck.space8
                            Component.onCompleted: forceLayout()
                            onWidthChanged: forceLayout()
                            TraceLegend {
                                objectName: "flightDeckTracePhysical"
                                text: "PHYSICAL"
                                tone: deck.textMuted
                                checked: root.showPhysicalTrace
                                onClicked: root.showPhysicalTrace = checked
                            }
                            TraceLegend {
                                objectName: "flightDeckTracePredicted"
                                text: "PREDICTED MAPPED"
                                tone: deck.attention
                                checked: root.showPredictedTrace
                                onClicked: root.showPredictedTrace = checked
                            }
                            TraceLegend {
                                objectName: "flightDeckTraceOutput"
                                text: "FINAL OUTPUT"
                                tone: deck.healthy
                                checked: root.showOutputTrace
                                onClicked: root.showOutputTrace = checked
                            }
                            DeckButton {
                                text: root.advancedTracesExpanded ? "HIDE ADVANCED TRACES" : "ADVANCED TRACES"
                                subdued: true
                                onClicked: root.advancedTracesExpanded = !root.advancedTracesExpanded
                            }
                            TraceLegend {
                                objectName: "flightDeckTraceBaseline"
                                visible: root.advancedTracesExpanded
                                text: "BASELINE"
                                tone: deck.informational
                                checked: root.showBaselineTrace
                                onClicked: root.showBaselineTrace = checked
                            }
                            TraceLegend {
                                objectName: "flightDeckTraceEstimated"
                                visible: root.advancedTracesExpanded
                                text: "ESTIMATED"
                                tone: deck.accent
                                checked: root.showEstimatedTrace
                                onClicked: root.showEstimatedTrace = checked
                            }
                        }
                        InstrumentGraph {
                            width: parent.width
                            height: 188
                            samples: root.responseLabSamples
                            drawInspectionCursor: root.responseLabSource === "live"
                            series: {
                                const lines = [];
                                if (root.showPhysicalTrace)
                                    lines.push({
                                        field: "physical",
                                        color: deck.textMuted,
                                        width: 2
                                    });
                                if (root.showPredictedTrace)
                                    lines.push({
                                        field: "predictedMappedOutput",
                                        color: deck.attention,
                                        width: 2
                                    });
                                if (root.showOutputTrace)
                                    lines.push({
                                        field: "adaptiveOutput",
                                        color: deck.healthy,
                                        width: 2.5
                                    });
                                if (root.advancedTracesExpanded && root.showBaselineTrace)
                                    lines.push({
                                        field: "baselineOutput",
                                        color: deck.informational,
                                        width: 1.5
                                    });
                                if (root.advancedTracesExpanded && root.showEstimatedTrace)
                                    lines.push({
                                        field: "estimated",
                                        color: deck.accent,
                                        width: 1.5
                                    });
                                return lines;
                            }
                        }
                    }
                }
                RowLayout {
                    width: parent.width
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "MOTION"
                            color: deck.textPrimary
                            font.pixelSize: 14
                            font.bold: true
                        }
                        Text {
                            text: "Velocity and acceleration stay on independent charts so their units remain legible."
                            color: deck.textSecondary
                            font.pixelSize: 9
                        }
                    }
                    Text {
                        text: root.responseLabSampleCount + " BOUNDED SAMPLES"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                }
                Flow {
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    Rectangle {
                        width: Math.max(280, (parent.width - deck.space12) / 2)
                        implicitHeight: 168
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "VELOCITY · /s"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 118
                                samples: root.responseLabSamples
                                lowerBound: -root.responseLabMagnitude(["velocity"], 0.1)
                                upperBound: root.responseLabMagnitude(["velocity"], 0.1)
                                series: [
                                    {
                                        field: "velocity",
                                        color: deck.accent,
                                        width: 2
                                    }
                                ]
                            }
                        }
                    }
                    Rectangle {
                        width: Math.max(280, (parent.width - deck.space12) / 2)
                        implicitHeight: 168
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "ACCELERATION · /s²"
                                color: deck.attention
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 118
                                samples: root.responseLabSamples
                                lowerBound: -root.responseLabMagnitude(["acceleration"], 0.1)
                                upperBound: root.responseLabMagnitude(["acceleration"], 0.1)
                                series: [
                                    {
                                        field: "acceleration",
                                        color: deck.attention,
                                        width: 2
                                    }
                                ]
                            }
                        }
                    }
                }
                Text {
                    text: "ADAPTIVE STATE"
                    color: deck.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Flow {
                    flow: Flow.LeftToRight
                    width: parent.width
                    spacing: deck.space12
                    Component.onCompleted: forceLayout()
                    onWidthChanged: forceLayout()
                    Rectangle {
                        width: Math.max(220, (parent.width - deck.space24) / 3)
                        implicitHeight: 150
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "HORIZON · ms"
                                color: deck.attention
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 100
                                samples: root.responseLabSamples
                                lowerBound: 0
                                upperBound: Math.max(0.1, root.responseLabMagnitude(["activeHorizonMs"], root.numericOr(root.effective().maximumHorizonMs, 1)))
                                series: [
                                    {
                                        field: "activeHorizonMs",
                                        color: deck.attention,
                                        width: 2
                                    }
                                ]
                            }
                        }
                    }
                    Rectangle {
                        width: Math.max(220, (parent.width - deck.space24) / 3)
                        implicitHeight: 150
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "AUTHORITY · %"
                                color: deck.healthy
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 100
                                samples: root.responseLabSamples
                                lowerBound: 0
                                upperBound: 1
                                series: [
                                    { field: "normalMotionAuthority", color: deck.healthy, width: 2 },
                                    { field: "rapidMotionAuthority", color: deck.attention, width: 2 }
                                ]
                            }
                        }
                    }
                    Rectangle {
                        width: Math.max(220, (parent.width - deck.space24) / 3)
                        implicitHeight: 150
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "MAPPED LEAD · %"
                                color: deck.healthy
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 100
                                samples: root.responseLabSamples
                                lowerBound: -Math.max(0.01, root.responseLabMagnitude(["appliedLead"], root.numericOr(root.effective().maximumLead, 0.01)))
                                upperBound: Math.max(0.01, root.responseLabMagnitude(["appliedLead"], root.numericOr(root.effective().maximumLead, 0.01)))
                                series: [
                                    {
                                        field: "appliedLead",
                                        color: deck.healthy,
                                        width: 2
                                    }
                                ]
                            }
                        }
                    }
                    Rectangle {
                        width: Math.max(220, (parent.width - deck.space24) / 3)
                        implicitHeight: 150
                        radius: deck.radiusCard
                        color: deck.primarySurface
                        border.color: deck.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: deck.space12
                            spacing: deck.space8
                            Text {
                                text: "CONFIDENCE · %"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            InstrumentGraph {
                                width: parent.width
                                height: 100
                                samples: root.responseLabSamples
                                lowerBound: 0
                                upperBound: 1
                                series: [
                                    {
                                        field: "confidence",
                                        color: deck.accent,
                                        width: 2
                                    }
                                ]
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: testLabCard
            objectName: "flightDeckAdaptiveTestLabCard"
            y: liveAnalysisSection.y + liveAnalysisSection.height + deck.space16
            width: parent.width
            implicitHeight: deck.space24 + testLabToggle.height + (root.testLabExpanded ? deck.space12 + testLabDetails.implicitHeight : 0)
            height: implicitHeight
            radius: deck.radiusPanel
            color: deck.primarySurface
            border.color: deck.border
            Column {
                id: testLabContent
                x: deck.space12
                y: deck.space12
                width: parent.width - deck.space24
                height: testLabToggle.height + (root.testLabExpanded ? spacing + testLabDetails.implicitHeight : 0)
                spacing: deck.space12
                Button {
                    id: testLabToggle
                    objectName: "flightDeckAdaptiveTestLabToggle"
                    width: parent.width
                    implicitHeight: 52
                    focusPolicy: Qt.StrongFocus
                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: deck.space12
                        anchors.rightMargin: deck.space12
                        Text {
                            text: root.testLabExpanded ? "⌄" : "›"
                            color: deck.attention
                            font.pixelSize: 22
                            font.bold: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "TEST LAB"
                                color: deck.textPrimary
                                font.family: deck.telemetryFont
                                font.pixelSize: 11
                                font.bold: true
                            }
                            Text {
                                text: root.testLabExpanded ? "Controlled synthetic motion, authoritative simulation, and compact results." : "Run existing synthetic scenarios without touching live controller output."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            text: root.testLabExpanded ? "CLOSE" : "OPEN"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 8
                            font.bold: true
                        }
                    }
                    background: Rectangle {
                        radius: deck.radiusControl
                        color: testLabToggle.hovered ? deck.selected : deck.secondarySurface
                        border.color: testLabToggle.activeFocus ? deck.focus : deck.border
                        border.width: testLabToggle.activeFocus ? 2 : 1
                    }
                    onClicked: root.testLabExpanded = !root.testLabExpanded
                }
                Column {
                    id: testLabDetails
                    visible: root.testLabExpanded
                    width: parent.width
                    spacing: deck.space12
                    RowLayout {
                        width: parent.width
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "SYNTHETIC TEST"
                                color: deck.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: "Scenario output is generated by the accepted preview/Test Lab model. It does not inject DirectInput, vJoy output, Automation, or profile activation."
                                color: deck.textSecondary
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        DeckCombo {
                            objectName: "flightDeckTestLabScenario"
                            Layout.preferredWidth: 240
                            model: ["Gentle Hover Correction", "Smooth Cyclic Sweep", "Normal Bank", "Sustained Moderate Turn", "Normal Recover", "Rapid Maneuver", "Hard Reversal", "Approach Corrections", "Human-Like Rapid Reversal", "Fast Full Sweep", "Very-Fast Full Sweep", "Same-Side Reversal", "Rapid Center Crossing", "Evasive Left/Right", "Sudden Stop", "Precision Correction"]
                            currentIndex: Math.max(0, model.indexOf(root.scenario))
                            onChoiceActivated: function (index, value) {
                                root.scenario = String(value);
                                root.setPreview();
                                popup.close();
                            }
                        }
                        DeckButton {
                            objectName: "flightDeckTestLabRun"
                            text: "RUN SIMULATION"
                            onClicked: root.setPreview()
                        }
                    }
                    Flow {
                        flow: Flow.LeftToRight
                        width: parent.width
                        spacing: deck.space8
                        Component.onCompleted: forceLayout()
                        onWidthChanged: forceLayout()
                        Repeater {
                            model: root.testLabMetricRows()
                            delegate: MetricTile {
                                required property var modelData
                                caption: modelData.caption
                                value: modelData.value
                                detail: modelData.detail
                                tone: modelData.tone
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "Prediction error compares each prediction with physical position at its active horizon. Reversal timing is measured from the physical reversal; viewing or running this receipt never writes DirectInput, vJoy, Automation, or profile activation."
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
