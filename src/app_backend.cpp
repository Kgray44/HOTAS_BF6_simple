#include "app_backend.h"

#include "axis_transform.h"
#include "automation_engine.h"
#include "button_mapping.h"
#include "config_store.h"
#include "hotas_build_version.h"
#include "launcher_core.h"
#include "profile_model.h"
#include "response_curve.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

QString automationProfileName(const MapperConfiguration &configuration, const QString &id)
{
    if (const ControllerProfile *profile = findProfile(configuration, id)) return profile->name;
    return id.isEmpty() ? u"a profile"_qs : u"missing profile"_qs;
}

QString automationBehaviorLabel(AutomationActivationMode mode)
{
    switch (mode) {
    case AutomationActivationMode::WhileTriggerActive: return u"MOMENTARY"_qs;
    case AutomationActivationMode::ToggleOnTrigger: return u"TOGGLE"_qs;
    case AutomationActivationMode::RunBriefly: return u"TIMED"_qs;
    }
    return u"UNKNOWN"_qs;
}

QString automationConditionSummary(const AutomationConditionDefinition &condition,
                                   const MapperConfiguration &configuration)
{
    const auto percent = [&condition](float value) {
        // DirectInput throttle is internally normalized to -1..+1, but the
        // flight-control UI exposes it as its natural 0..100 percent travel.
        const float display = condition.axis == static_cast<int>(PhysicalAxis::Z)
            ? (value + 1.0F) * 50.0F : value * 100.0F;
        return QString::number(display, 'f', 0) + u"%"_qs;
    };
    switch (condition.type) {
    case AutomationConditionType::Always: return u"All the time"_qs;
    case AutomationConditionType::AxisAbove: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is above "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisBelow: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is below "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisBetween: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is between "_qs + percent(condition.minimum) + u" and "_qs + percent(condition.maximum);
    case AutomationConditionType::AxisOutsideRange: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" is outside "_qs + percent(condition.minimum) + u" to "_qs + percent(condition.maximum);
    case AutomationConditionType::ButtonHeld: return QString(u"Button %1 is held"_qs).arg(condition.button);
    case AutomationConditionType::ButtonReleased: return QString(u"Button %1 is not held"_qs).arg(condition.button);
    case AutomationConditionType::ButtonPressed: return QString(u"Button %1 is pressed"_qs).arg(condition.button);
    case AutomationConditionType::ButtonReleaseEvent: return QString(u"Button %1 is released"_qs).arg(condition.button);
    case AutomationConditionType::ButtonMultiPress: return QString(u"Button %1 is pressed %2 times"_qs)
        .arg(condition.button).arg(condition.pressCount);
    case AutomationConditionType::ButtonLongPress: return QString(u"Button %1 is held for %2 ms"_qs)
        .arg(condition.button).arg(condition.longPressDurationMs);
    case AutomationConditionType::AxisCrossesAbove: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" crosses above "_qs + percent(condition.minimum);
    case AutomationConditionType::AxisCrossesBelow: return physicalAxisLabel(static_cast<PhysicalAxis>(condition.axis))
        + u" crosses below "_qs + percent(condition.minimum);
    case AutomationConditionType::PovActive: return QString(u"POV %1 points %2"_qs).arg(condition.povHat)
        .arg(povDirectionLabel(condition.povDirection));
    case AutomationConditionType::PovInactive: return QString(u"POV %1 is not pointing %2"_qs).arg(condition.povHat)
        .arg(povDirectionLabel(condition.povDirection));
    case AutomationConditionType::BaseProfileIs: return u"Selected profile is "_qs
        + automationProfileName(configuration, condition.profileId);
    case AutomationConditionType::EffectiveProfileIs: return u"Active profile is "_qs
        + automationProfileName(configuration, condition.profileId);
    }
    return u"Invalid condition"_qs;
}

QString automationActionSummary(const AutomationActionDefinition &action,
                                const MapperConfiguration &configuration)
{
    const auto percent = [](float value) { return QString::number(value * 100.0F, 'f', 0) + u"%"_qs; };
    switch (action.type) {
    case AutomationActionType::VJoyButtonHold: return QString(u"Press and hold virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::VJoyButtonToggle: return QString(u"Toggle virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::VJoyButtonTap: return QString(u"Tap virtual button %1"_qs)
        .arg(action.virtualButton);
    case AutomationActionType::ProfileHold: return u"Use "_qs
        + automationProfileName(configuration, action.profileId) + u" while active"_qs;
    case AutomationActionType::ProfileToggle: return u"Switch to "_qs
        + automationProfileName(configuration, action.profileId);
    case AutomationActionType::AxisScale: return u"Change "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" sensitivity to "_qs
        + percent(action.value);
    case AutomationActionType::AxisOffset: return u"Adjust "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" output by "_qs
        + percent(action.value);
    case AutomationActionType::AxisClamp: return u"Limit "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" output to "_qs
        + percent(action.minimum) + u"–"_qs + percent(action.maximum);
    case AutomationActionType::AxisOverride: return u"Force "_qs
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" to "_qs
        + percent(action.value);
    case AutomationActionType::AxisMix: return u"Mix "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.sourceAxis))
        + (action.sourceStage == AutomationAxisSourceStage::Physical ? u" from controller input into "_qs
                                                                     : u" from current mapped output into "_qs)
        + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis)) + u" at "_qs + percent(action.value);
    case AutomationActionType::AxisFollow: {
        QString summary = u"Make "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.targetAxis))
            + u" follow "_qs + physicalAxisLabel(static_cast<PhysicalAxis>(action.sourceAxis))
            + (action.sourceStage == AutomationAxisSourceStage::Physical ? u" from controller input at "_qs
                                                                         : u" from current mapped output at "_qs)
            + percent(action.value);
        if (std::abs(action.offset) > 0.00001F) summary += u" with "_qs + percent(action.offset) + u" offset"_qs;
        return summary;
    }
    case AutomationActionType::MappingOn: return u"Turn mapping on"_qs;
    case AutomationActionType::MappingOff: return u"Turn mapping off"_qs;
    case AutomationActionType::ToggleMapping: return u"Toggle mapping on or off"_qs;
    }
    return u"Invalid action"_qs;
}

bool automationDefinitionFromVariant(const QVariantMap &map, AutomationDefinition *definition,
                                     QString *reason)
{
    if (!definition) return false;
    AutomationDefinition restored;
    restored.id = map.value(u"id"_qs).toString().trimmed();
    restored.name = map.value(u"name"_qs).toString().trimmed();
    restored.enabled = map.value(u"enabled"_qs, true).toBool();
    restored.matchMode = static_cast<AutomationMatchMode>(map.value(u"matchMode"_qs, 0).toInt());
    restored.activationMode = static_cast<AutomationActivationMode>(map.value(u"activationMode"_qs, 0).toInt());
    restored.activeDurationMs = map.value(u"activeDurationMs"_qs, 250).toInt();
    restored.priority = std::clamp(map.value(u"priority"_qs, 50).toInt(), 0, 100);
    if (restored.id.isEmpty() || restored.name.isEmpty() || restored.name.size() > 64
        || (restored.matchMode != AutomationMatchMode::All && restored.matchMode != AutomationMatchMode::Any)
        || (restored.activationMode != AutomationActivationMode::WhileTriggerActive
            && restored.activationMode != AutomationActivationMode::ToggleOnTrigger
            && restored.activationMode != AutomationActivationMode::RunBriefly)
        || restored.activeDurationMs < kAutomationMinimumRuleActiveDurationMs
        || restored.activeDurationMs > kAutomationMaximumRuleActiveDurationMs) {
        if (reason) *reason = u"Name and match mode are required."_qs;
        return false;
    }
    const QVariantList conditions = map.value(u"conditions"_qs).toList();
    const QVariantList actions = map.value(u"actions"_qs).toList();
    if (conditions.empty() || conditions.size() > kMaximumAutomationConditions
        || actions.empty() || actions.size() > kMaximumAutomationActions) {
        if (reason) *reason = u"Rules support one to four conditions and actions."_qs;
        return false;
    }
    for (const QVariant &value : conditions) {
        const QVariantMap input = value.toMap();
        AutomationConditionDefinition condition;
        condition.type = static_cast<AutomationConditionType>(input.value(u"type"_qs).toInt());
        condition.axis = input.value(u"axis"_qs, 0).toInt();
        condition.minimum = static_cast<float>(input.value(u"minimum"_qs, 0.0).toDouble());
        condition.maximum = static_cast<float>(input.value(u"maximum"_qs, 0.0).toDouble());
        condition.hysteresis = static_cast<float>(input.value(u"hysteresis"_qs, 0.0).toDouble());
        condition.button = input.value(u"button"_qs, 1).toInt();
        condition.povHat = input.value(u"povHat"_qs, 1).toInt();
        condition.povDirection = static_cast<PovDirection>(input.value(u"povDirection"_qs,
            static_cast<int>(PovDirection::Up)).toInt());
        condition.profileId = input.value(u"profileId"_qs).toString().trimmed();
        condition.pressCount = input.value(u"pressCount"_qs, 2).toInt();
        condition.multiPressWindowMs = input.value(u"multiPressWindowMs"_qs, 350).toInt();
        condition.longPressDurationMs = input.value(u"longPressDurationMs"_qs, 600).toInt();
        if (static_cast<int>(condition.type) < static_cast<int>(AutomationConditionType::Always)
            || static_cast<int>(condition.type) > static_cast<int>(AutomationConditionType::AxisCrossesBelow)) {
            if (reason) *reason = u"Condition type is invalid."_qs;
            return false;
        }
        restored.conditions.push_back(std::move(condition));
    }
    for (const QVariant &value : actions) {
        const QVariantMap input = value.toMap();
        AutomationActionDefinition action;
        action.type = static_cast<AutomationActionType>(input.value(u"type"_qs).toInt());
        action.virtualButton = input.value(u"virtualButton"_qs, 1).toInt();
        action.profileId = input.value(u"profileId"_qs).toString().trimmed();
        action.targetAxis = input.value(u"targetAxis"_qs, 0).toInt();
        action.sourceAxis = input.value(u"sourceAxis"_qs, 0).toInt();
        action.sourceStage = static_cast<AutomationAxisSourceStage>(input.value(u"sourceStage"_qs,
            static_cast<int>(AutomationAxisSourceStage::Processed)).toInt());
        action.value = static_cast<float>(input.value(u"value"_qs, 0.0).toDouble());
        action.offset = static_cast<float>(input.value(u"offset"_qs, 0.0).toDouble());
        action.minimum = static_cast<float>(input.value(u"minimum"_qs, -1.0).toDouble());
        action.maximum = static_cast<float>(input.value(u"maximum"_qs, 1.0).toDouble());
        action.tapDurationMs = input.value(u"tapDurationMs"_qs, 80).toInt();
        if (static_cast<int>(action.type) < static_cast<int>(AutomationActionType::VJoyButtonHold)
            || static_cast<int>(action.type) > static_cast<int>(AutomationActionType::ToggleMapping)) {
            if (reason) *reason = u"Action type is invalid."_qs;
            return false;
        }
        restored.actions.push_back(std::move(action));
    }
    *definition = std::move(restored);
    return true;
}

} // namespace

AppBackend::AppBackend(QObject *parent)
    : QObject(parent), m_configuration(ConfigStore::load()), m_worker(m_configuration)
{
    QSettings settings;
    m_controllerSetupSuggested = !settings.value(u"readiness/controllerSetupIntroSeen"_qs, false).toBool();
    connect(&m_snapshotTimer, &QTimer::timeout, this, &AppBackend::refreshUiSnapshot);
    connect(&m_worker, &MappingWorker::workerEvent, this, &AppBackend::appendEvent, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::hardwareStateChanged, this, [this] { emit stateChanged(); }, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::buttonConfigurationSuggested, this,
            &AppBackend::initializeDefaultButtonMappings, Qt::QueuedConnection);
    m_snapshotTimer.setInterval(16); // UI-only latest-state projection, never the mapping cadence.
    m_snapshotTimer.start();
    m_rateClock.start();
    m_physicalUpdateClock.start();
    m_latencyPercentileClock.start();
    m_updateTimeout.setSingleShot(true);
    connect(&m_updateTimeout, &QTimer::timeout, this, [this] {
        if (!m_updateReply) return;
        m_updateTimedOut = true;
        m_updateReply->abort();
    });
    rebuildSelectedAxisCurve();
    appendEvent(u"HOTAS Mapper ready"_qs);
    // The mapping thread consumes physical reports while the GUI may be
    // rebuilding editor data. HighPriority is intentionally below
    // TimeCriticalPriority: it favors real input responsiveness without
    // starving normal system or rendering work on a constrained CPU.
    m_worker.start(QThread::HighPriority);
    if (m_configuration.startMappingOnLaunch) {
        m_worker.setMappingEnabled(true);
    }
    // Update network activity is intentionally scheduled on the UI event loop
    // after startup. It never enters the DirectInput/vJoy worker or its hot
    // path, and a bounded timeout leaves mapper startup fully independent.
    QTimer::singleShot(500, this, &AppBackend::checkForUpdates);
}

AppBackend::~AppBackend()
{
    m_worker.requestStop();
    m_worker.wait(1500);
}

QVariantList AppBackend::axes() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const ControllerProfile &profile = currentProfile();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const auto axis = static_cast<PhysicalAxis>(index);
        const AxisMapping &mapping = profile.axes[index];
        QVariantMap item;
        item.insert(u"index"_qs, index);
        item.insert(u"key"_qs, physicalAxisKey(axis));
        const QString hardwareLabel = physicalAxisLabel(axis);
        const QString customLabel = mapping.customName.trimmed();
        item.insert(u"label"_qs, customLabel.isEmpty() ? hardwareLabel : customLabel);
        item.insert(u"hardwareLabel"_qs, hardwareLabel);
        item.insert(u"customName"_qs, customLabel);
        item.insert(u"detail"_qs, physicalAxisDetail(axis));
        item.insert(u"available"_qs, runtime.axisAvailable[index].load());
        item.insert(u"raw"_qs, runtime.raw[index].load());
        item.insert(u"curveResponse"_qs, runtime.curveResponse[index].load());
        item.insert(u"transformed"_qs, runtime.transformed[index].load());
        const float virtualValue = runtime.virtualValues[index].load();
        item.insert(u"virtualValue"_qs, virtualValue);
        item.insert(u"virtualValid"_qs, std::isfinite(virtualValue));
        item.insert(u"target"_qs, virtualAxisLabel(mapping.target));
        const int targetIndex = static_cast<int>(mapping.target);
        const QString alias = targetIndex > 0 && targetIndex < kVirtualAxisSlotCount
            ? profile.virtualAxisAliases[static_cast<size_t>(targetIndex)].trimmed() : QString{};
        item.insert(u"outputAlias"_qs, alias);
        item.insert(u"targetAvailable"_qs, targetIndex == 0 || runtime.virtualAxisAvailable[
            static_cast<size_t>(targetIndex)].load());
        item.insert(u"rangeMode"_qs, axisRangeModeKey(mapping.rangeMode));
        item.insert(u"rangeModeLabel"_qs, axisRangeModeLabel(mapping.rangeMode));
        item.insert(u"inverted"_qs, mapping.inverted);
        item.insert(u"deadzone"_qs, mapping.deadzone);
        item.insert(u"hysteresis"_qs, mapping.hysteresis);
        item.insert(u"outputMinimum"_qs, mapping.outputMinimum);
        item.insert(u"outputMaximum"_qs, mapping.outputMaximum);
        item.insert(u"curveSummary"_qs, curveDefinitionSummary(mapping.curve));
        item.insert(u"curvePointEditing"_qs, mapping.curve.pointEditing);
        item.insert(u"unipolar"_qs, mapping.rangeMode == AxisRangeMode::OneSided);
        item.insert(u"calibrationEnabled"_qs, m_configuration.calibration[index].enabled);
        item.insert(u"calibrationMinimum"_qs, runtime.calibrationMinimum[index].load());
        item.insert(u"calibrationCenter"_qs, runtime.calibrationCenter[index].load());
        item.insert(u"calibrationMaximum"_qs, runtime.calibrationMaximum[index].load());
        result.append(item);
    }
    return result;
}

int AppBackend::selectedAxisIndex() const
{
    return m_configuration.selectedAxisIndex;
}

QVariantList AppBackend::selectedAxisCurve() const
{
    return m_selectedAxisCurve;
}

QVariantList AppBackend::curveEditorResponseCurve() const
{
    return m_curveEditorResponseCurve;
}

QVariantList AppBackend::curveGainSamples() const
{
    return m_curveGainSamples;
}

QVariantList AppBackend::curveComparisonCurve() const
{
    return m_curveComparisonCurve;
}

QVariantList AppBackend::curvePreviewCurve() const
{
    return m_curvePreviewCurve;
}

QVariantList AppBackend::selectedCurvePoints() const
{
    QVariantList points;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing) return points;
    for (int index = 0; index < static_cast<int>(mapping->curve.points.size()); ++index) {
        const CurvePoint &point = mapping->curve.points[static_cast<size_t>(index)];
        QVariantMap item;
        item.insert(u"index"_qs, index);
        item.insert(u"input"_qs, point.input);
        item.insert(u"output"_qs, point.output);
        item.insert(u"locked"_qs, point.locked);
        points.append(item);
    }
    return points;
}

QVariantMap AppBackend::curveEditorState() const
{
    QVariantMap state;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return state;
    const bool unipolar = mapping->rangeMode == AxisRangeMode::OneSided;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const float raw = runtime.raw[m_configuration.selectedAxisIndex].load();
    const float afterInversion = runtime.afterInversion[m_configuration.selectedAxisIndex].load();
    const float curveInput = unipolar ? (afterInversion + 1.0F) * 0.5F : afterInversion;
    state.insert(u"family"_qs, curveFamilyLabel(mapping->curve.family));
    state.insert(u"familyId"_qs, static_cast<int>(mapping->curve.family));
    state.insert(u"presetId"_qs, mapping->curve.presetId);
    state.insert(u"sourceFamilyId"_qs, static_cast<int>(mapping->curve.sourceFamily));
    state.insert(u"sourcePresetId"_qs, mapping->curve.sourcePresetId);
    state.insert(u"summary"_qs, curveDefinitionSummary(mapping->curve));
    state.insert(u"baseLabel"_qs, mapping->curve.baseLabel);
    state.insert(u"strength"_qs, mapping->curve.family == CurveFamily::Linear ? 0.0 : mapping->curve.strength);
    state.insert(u"pointEditing"_qs, mapping->curve.pointEditing);
    state.insert(u"symmetry"_qs, mapping->curve.symmetry);
    state.insert(u"interpolation"_qs, curveInterpolationLabel(mapping->curve.interpolation));
    state.insert(u"pointDensity"_qs, mapping->curve.pointDensity);
    state.insert(u"pointCount"_qs, static_cast<int>(mapping->curve.points.size()));
    state.insert(u"unipolar"_qs, unipolar);
    state.insert(u"physicalInput"_qs, raw);
    state.insert(u"normalized"_qs, runtime.normalized[m_configuration.selectedAxisIndex].load());
    state.insert(u"afterDeadzone"_qs, runtime.afterDeadzone[m_configuration.selectedAxisIndex].load());
    state.insert(u"afterHysteresis"_qs, runtime.afterHysteresis[m_configuration.selectedAxisIndex].load());
    state.insert(u"afterInversion"_qs, afterInversion);
    state.insert(u"curveInput"_qs, afterInversion);
    state.insert(u"curveResponse"_qs, runtime.curveResponse[m_configuration.selectedAxisIndex].load());
    state.insert(u"finalOutput"_qs, runtime.transformed[m_configuration.selectedAxisIndex].load());
    state.insert(u"localGain"_qs, evaluateCurveGain(curveInput, mapping->curve, unipolar));
    state.insert(u"runtimeLutSamples"_qs, kResponseCurveLutSamples);
    state.insert(u"lastCurveCompileUs"_qs, static_cast<qulonglong>(m_worker.runtime().lastCurveCompileUs.load()));
    state.insert(u"previewLabel"_qs, m_curvePreviewLabel);
    const CurveAnalysis health = analyzeCurveDefinition(mapping->curve, unipolar);
    state.insert(u"neutralOffset"_qs, health.neutralOffset);
    state.insert(u"neutralMapsToNeutral"_qs, health.neutralMapsToNeutral);
    if (const AdvancedCurvePresetInfo *advanced = mapping->curve.family == CurveFamily::Advanced
            ? advancedCurvePreset(mapping->curve.presetId) : nullptr) {
        state.insert(u"advancedBestFor"_qs, advanced->bestFor);
        state.insert(u"advancedCategory"_qs, advanced->category);
        state.insert(u"advancedBehavior"_qs, advanced->behavior);
        state.insert(u"advancedSourceBasis"_qs, advanced->provenance);
    }
    return state;
}

QVariantMap AppBackend::curveAnalysis() const
{
    return m_curveAnalysis;
}

QVariantMap AppBackend::curveComparisonState() const
{
    QVariantMap state;
    if (m_curveComparisonId.isEmpty()) return state;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return state;
    const bool unipolar = mapping->rangeMode == AxisRangeMode::OneSided;
    const CurveDefinition comparison = comparisonCurveDefinition();
    const float raw = m_worker.runtime().raw[m_configuration.selectedAxisIndex].load();
    const float input = unipolar ? (raw + 1.0F) * 0.5F : raw;
    const float current = evaluateCurveDefinition(input, mapping->curve, unipolar);
    const float reference = evaluateCurveDefinition(input, comparison, unipolar);
    state.insert(u"label"_qs, m_curveComparisonLabel);
    state.insert(u"currentOutput"_qs, current);
    state.insert(u"referenceOutput"_qs, reference);
    state.insert(u"difference"_qs, current - reference);
    state.insert(u"currentGain"_qs, evaluateCurveGain(input, mapping->curve, unipolar));
    state.insert(u"referenceGain"_qs, evaluateCurveGain(input, comparison, unipolar));
    return state;
}

QVariantList AppBackend::curveStandardPresets() const
{
    QVariantList result;
    for (const CurvePresetInfo &preset : standardCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
                                  {u"strength"_qs, preset.strength}});
    }
    return result;
}

QVariantList AppBackend::curveAdvancedPresets() const
{
    QVariantList result;
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        const CurveDefinition definition = advancedCurveDefinition(preset.id);
        const CurveAnalysis analysis = analyzeCurveDefinition(definition, false);
        const float edgeGain = evaluateCurveGain(0.90F, definition, false);
        const auto responseBand = [](float gain) {
            if (gain < 0.35F) return u"Very Soft"_qs;
            if (gain < 0.75F) return u"Soft"_qs;
            if (gain <= 1.25F) return u"Moderate"_qs;
            return u"Strong"_qs;
        };
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
            {u"category"_qs, preset.category}, {u"bestFor"_qs, preset.bestFor}, {u"behavior"_qs, preset.behavior},
            {u"sourceBasis"_qs, preset.provenance},
            {u"centerResponse"_qs, responseBand(analysis.centerGain)},
            {u"midrangeResponse"_qs, responseBand(analysis.halfGain)},
            {u"edgeResponse"_qs, responseBand(edgeGain)},
            {u"centerGain"_qs, analysis.centerGain}, {u"quarterGain"_qs, analysis.quarterGain},
            {u"midrangeGain"_qs, analysis.halfGain}, {u"threeQuarterGain"_qs, analysis.threeQuarterGain},
            {u"peakGain"_qs, analysis.peakGain}, {u"symmetric"_qs, true},
            {u"fullAuthority"_qs, analysis.fullAuthority},
            {u"strengthBehavior"_qs, u"0% Linear · 100% full researched response"_qs}});
    }
    return result;
}

QVariantList AppBackend::personalCurvePresets() const
{
    QVariantList result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != unipolar) continue;
        result.append(QVariantMap{{u"id"_qs, preset.id}, {u"name"_qs, preset.name},
                                  {u"description"_qs, preset.description},
                                  {u"summary"_qs, curveDefinitionSummary(preset.definition)}});
    }
    return result;
}

QVariantList AppBackend::curveComparisonChoices() const
{
    QVariantList result;
    result.append(QVariantMap{{u"id"_qs, QString{}}, {u"label"_qs, u"None"_qs}});
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const AxisMapping *current = selectedAxisMapping();
    if (current && current->curve.family == CurveFamily::Custom
        && current->curve.sourceFamily != CurveFamily::Linear) {
        result.append(QVariantMap{{u"id"_qs, u"source"_qs},
            {u"label"_qs, u"Source · "_qs + current->curve.baseLabel}});
    }
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if ((profile.axes[static_cast<size_t>(axis)].rangeMode == AxisRangeMode::OneSided) != unipolar) continue;
            result.append(QVariantMap{{u"id"_qs, QString(u"profile:%1:%2"_qs).arg(profile.id).arg(axis)},
                {u"label"_qs, profile.name + u" / "_qs
                    + physicalAxisLabel(static_cast<PhysicalAxis>(axis))}});
        }
    }
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, u"advanced:"_qs + preset.id},
            {u"label"_qs, u"Advanced · "_qs + preset.name}});
    }
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != unipolar) continue;
        result.append(QVariantMap{{u"id"_qs, u"personal:"_qs + preset.id},
            {u"label"_qs, u"Personal · "_qs + preset.name}});
    }
    return result;
}

QVariantList AppBackend::curvePreviewChoices() const
{
    QVariantList result;
    for (const AdvancedCurvePresetInfo &preset : advancedCurvePresets()) {
        result.append(QVariantMap{{u"id"_qs, u"advanced:"_qs + preset.id},
            {u"label"_qs, u"Advanced · "_qs + preset.name}});
    }
    for (const PersonalCurvePreset &preset : m_configuration.personalCurvePresets) {
        if (preset.unipolar != axisIsOneSided(m_configuration.selectedAxisIndex)) continue;
        result.append(QVariantMap{{u"id"_qs, u"personal:"_qs + preset.id},
            {u"label"_qs, u"Personal · "_qs + preset.name}});
    }
    return result;
}

QVariantList AppBackend::curveCopyChoices() const
{
    QVariantList result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if ((profile.axes[static_cast<size_t>(axis)].rangeMode == AxisRangeMode::OneSided) != unipolar) continue;
            result.append(QVariantMap{{u"id"_qs, QString(u"%1:%2"_qs).arg(profile.id).arg(axis)},
                {u"label"_qs, profile.name + u" / "_qs
                    + physicalAxisLabel(static_cast<PhysicalAxis>(axis)) + u" · "_qs
                    + curveDefinitionSummary(profile.axes[axis].curve)}});
        }
    }
    return result;
}

QVariantList AppBackend::buttons() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const int capacity = vjoyButtonCount();
    for (int source = 0; source < kMaximumPhysicalButtons; ++source) {
        if (!runtime.buttonAvailable[source].load()) continue;
        const ButtonBindings &bindings = currentProfile().buttons;
        const ButtonBinding binding = source < static_cast<int>(bindings.size())
            ? bindings[static_cast<size_t>(source)] : ButtonBinding{};
        const int target = binding.type == ButtonActionType::VirtualButton
            && isButtonBindingValid(binding, capacity) ? binding.target : 0;
        const ProfileTriggerBinding trigger = source < static_cast<int>(m_configuration.profileTriggers.size())
            ? m_configuration.profileTriggers[static_cast<size_t>(source)] : ProfileTriggerBinding{};
        const bool profileControlEnabled = profileTriggerBindingEnabled(trigger);
        const ControllerProfile *triggerTarget = profileControlEnabled
            ? findProfile(m_configuration, trigger.targetProfileId) : nullptr;
        const ProfileTriggerMode activeMode = static_cast<ProfileTriggerMode>(
            runtime.profileOverrideMode.load());
        QVariantMap item;
        item.insert(u"index"_qs, source + 1);
        const QString hardwareLabel = QString(u"Button %1"_qs).arg(source + 1);
        const QString customLabel = binding.customName.trimmed();
        const MappingControlAction mappingControl = source < static_cast<int>(m_configuration.mappingControls.size())
            ? m_configuration.mappingControls[static_cast<size_t>(source)] : MappingControlAction::None;
        item.insert(u"label"_qs, customLabel.isEmpty() ? hardwareLabel : customLabel);
        item.insert(u"hardwareLabel"_qs, hardwareLabel);
        item.insert(u"customName"_qs, customLabel);
        item.insert(u"pressed"_qs, runtime.physicalButtonPressed[source].load());
        item.insert(u"target"_qs, target);
        item.insert(u"targetLabel"_qs, target > 0
            ? QString(u"vJoy Button %1"_qs).arg(target) : u"Disabled"_qs);
        item.insert(u"virtualPressed"_qs, target > 0
            && runtime.virtualButtonPressed[target - 1].load());
        item.insert(u"profileControlEnabled"_qs, profileControlEnabled);
        item.insert(u"profileControlTargetId"_qs, trigger.targetProfileId);
        item.insert(u"profileControlTargetName"_qs, triggerTarget
            ? triggerTarget->name : (profileControlEnabled ? u"Target profile unavailable"_qs : QString{}));
        item.insert(u"profileControlTargetAvailable"_qs, triggerTarget != nullptr);
        item.insert(u"profileControlMode"_qs, profileTriggerModeLabel(trigger.mode));
        item.insert(u"profileControlActive"_qs, runtime.profileOverrideButton.load() == source + 1
            && activeMode == trigger.mode && profileControlEnabled);
        item.insert(u"mappingControl"_qs, mappingControlActionLabel(mappingControl));
        item.insert(u"mappingControlKey"_qs, mappingControlActionKey(mappingControl));
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::povs() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const int count = std::clamp(povCount(), 0, kMaximumPhysicalPovs);
    for (int hat = 0; hat < count; ++hat) {
        const int raw = runtime.povValues[static_cast<size_t>(hat)].load();
        const PovDirection direction = povDirectionFromRaw(raw);
        QVariantMap item;
        item.insert(u"index"_qs, hat + 1);
        item.insert(u"raw"_qs, raw);
        item.insert(u"centered"_qs, direction == PovDirection::Centered);
        item.insert(u"direction"_qs, povDirectionLabel(direction));
        item.insert(u"angle"_qs, direction == PovDirection::Centered ? -1 : raw / 100);
        const NativePovBinding binding = hat < static_cast<int>(m_configuration.nativePovBindings.size())
            ? m_configuration.nativePovBindings[static_cast<size_t>(hat)] : NativePovBinding{};
        const bool targetAvailable = binding.targetType == NativePovTargetType::Continuous
            ? binding.targetIndex <= vjoyContinuousPovCount()
            : binding.targetType == NativePovTargetType::Discrete
                && binding.targetIndex <= vjoyDiscretePovCount();
        const QString targetKind = binding.targetType == NativePovTargetType::Continuous
            ? u"Continuous"_qs : binding.targetType == NativePovTargetType::Discrete
                ? u"Discrete"_qs : QString{};
        item.insert(u"nativeEnabled"_qs, binding.enabled);
        item.insert(u"nativeTargetKey"_qs, binding.targetType == NativePovTargetType::Continuous
            ? QString(u"continuous:%1"_qs).arg(binding.targetIndex)
            : binding.targetType == NativePovTargetType::Discrete
                ? QString(u"discrete:%1"_qs).arg(binding.targetIndex) : QString{});
        item.insert(u"nativeTargetLabel"_qs, binding.enabled
            ? QString(u"vJoy %1 POV %2"_qs).arg(targetKind).arg(binding.targetIndex)
            : u"Off"_qs);
        item.insert(u"nativeAvailable"_qs, targetAvailable);
        item.insert(u"nativeStatus"_qs, !binding.enabled ? u"OFF"_qs
            : targetAvailable ? u"READY"_qs : u"UNAVAILABLE"_qs);
        result.append(item);
    }
    return result;
}

QVariantList AppBackend::povInputs() const
{
    QVariantList result;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    const PovBindings &bindings = currentProfile().povs;
    const int capacity = vjoyButtonCount();
    const int hats = std::clamp(povCount(), 0, kMaximumPhysicalPovs);
    for (int hat = 0; hat < hats; ++hat) {
        const PovDirection active = povDirectionFromRaw(
            runtime.povValues[static_cast<size_t>(hat)].load());
        for (int direction = 0; direction < kPovDirectionCount; ++direction) {
            const ButtonBinding binding = hat < static_cast<int>(bindings.size())
                ? bindings[static_cast<size_t>(hat)][static_cast<size_t>(direction)] : ButtonBinding{};
            const int target = binding.type == ButtonActionType::VirtualButton
                && isButtonBindingValid(binding, capacity) ? binding.target : 0;
            const PovDirection logicalDirection = static_cast<PovDirection>(direction + 1);
            const ProfileTriggerBinding trigger = hat < static_cast<int>(m_configuration.povProfileTriggers.size())
                ? m_configuration.povProfileTriggers[static_cast<size_t>(hat)][static_cast<size_t>(direction)]
                : ProfileTriggerBinding{};
            const bool profileControlEnabled = profileTriggerBindingEnabled(trigger);
            const ControllerProfile *triggerTarget = profileControlEnabled
                ? findProfile(m_configuration, trigger.targetProfileId) : nullptr;
            const ProfileTriggerMode activeMode = static_cast<ProfileTriggerMode>(
                runtime.profileOverrideMode.load());
            QVariantMap item;
            item.insert(u"hat"_qs, hat + 1);
            item.insert(u"direction"_qs, direction);
            item.insert(u"label"_qs, povDirectionLabel(logicalDirection));
            item.insert(u"active"_qs, active == logicalDirection);
            item.insert(u"target"_qs, target);
            item.insert(u"targetLabel"_qs, target > 0
                ? QString(u"vJoy Button %1"_qs).arg(target) : u"Disabled"_qs);
            item.insert(u"virtualPressed"_qs, target > 0
                && runtime.virtualButtonPressed[static_cast<size_t>(target - 1)].load());
            item.insert(u"profileControlEnabled"_qs, profileControlEnabled);
            item.insert(u"profileControlTargetId"_qs, trigger.targetProfileId);
            item.insert(u"profileControlTargetName"_qs, triggerTarget
                ? triggerTarget->name : (profileControlEnabled ? u"Target profile unavailable"_qs : QString{}));
            item.insert(u"profileControlTargetAvailable"_qs, triggerTarget != nullptr);
            item.insert(u"profileControlMode"_qs, profileTriggerModeLabel(trigger.mode));
            item.insert(u"profileControlActive"_qs, runtime.profileOverridePovHat.load() == hat + 1
                && runtime.profileOverridePovDirection.load() == direction
                && activeMode == trigger.mode && profileControlEnabled);
            result.append(item);
        }
    }
    return result;
}

QVariantList AppBackend::profiles() const
{
    QVariantList result;
    for (const ControllerProfile &profile : m_configuration.profiles) {
        int mappedAxes = 0;
        for (const AxisMapping &axis : profile.axes) {
            if (axis.target != VirtualAxis::Disabled) ++mappedAxes;
        }
        int mappedButtons = 0;
        for (const ButtonBinding &binding : profile.buttons) {
            if (binding.type == ButtonActionType::VirtualButton) ++mappedButtons;
        }
        QVariantMap item;
        item.insert(u"id"_qs, profile.id);
        item.insert(u"name"_qs, profile.name);
        item.insert(u"active"_qs, profile.id == m_configuration.activeProfileId);
        item.insert(u"effective"_qs, profile.id == effectiveProfileId());
        item.insert(u"effectiveSource"_qs, profile.id == effectiveProfileId()
            ? profileSourceLabel() : QString{});
        item.insert(u"protected"_qs, profile.id == normalProfileId());
        item.insert(u"mappedAxes"_qs, mappedAxes);
        item.insert(u"mappedButtons"_qs, mappedButtons);
        result.append(item);
    }
    return result;
}

QString AppBackend::activeProfileId() const { return m_configuration.activeProfileId; }
QString AppBackend::activeProfileName() const { return currentProfile().name; }
QString AppBackend::effectiveProfileName() const
{
    const int index = m_worker.runtime().effectiveProfileIndex.load();
    if (index >= 0 && index < static_cast<int>(m_configuration.profiles.size())) {
        return m_configuration.profiles[static_cast<size_t>(index)].name;
    }
    return currentProfile().name;
}

QString AppBackend::effectiveProfileId() const
{
    const int index = m_worker.runtime().effectiveProfileIndex.load();
    if (index >= 0 && index < static_cast<int>(m_configuration.profiles.size())) {
        return m_configuration.profiles[static_cast<size_t>(index)].id;
    }
    return m_configuration.activeProfileId;
}

QString AppBackend::profileSourceLabel() const
{
    const int button = m_worker.runtime().profileOverrideButton.load();
    const int povHat = m_worker.runtime().profileOverridePovHat.load();
    const int povDirection = m_worker.runtime().profileOverridePovDirection.load();
    const ProfileTriggerMode mode = static_cast<ProfileTriggerMode>(
        m_worker.runtime().profileOverrideMode.load());
    const int automationRule = m_worker.runtime().profileOverrideAutomationRule.load();
    if (mode == ProfileTriggerMode::Disabled) return u"Manual base profile"_qs;
    if (button > 0) {
        return QString(u"Button %1 · %2"_qs).arg(button).arg(profileTriggerModeLabel(mode));
    }
    if (povHat > 0 && povDirection >= 0 && povDirection < kPovDirectionCount) {
        return QString(u"POV %1 %2 · %3"_qs).arg(povHat)
            .arg(povDirectionLabel(static_cast<PovDirection>(povDirection + 1)))
            .arg(profileTriggerModeLabel(mode));
    }
    if (automationRule >= 0 && automationRule < static_cast<int>(m_configuration.automations.size())) {
        const QString name = m_configuration.automations[static_cast<size_t>(automationRule)].name;
        return QString(u"Automation %1 · %2"_qs).arg(name)
            .arg(profileTriggerModeLabel(mode));
    }
    return u"Manual base profile"_qs;
}
int AppBackend::activeProfileIndex() const
{
    for (int index = 0; index < static_cast<int>(m_configuration.profiles.size()); ++index) {
        if (m_configuration.profiles[static_cast<size_t>(index)].id == m_configuration.activeProfileId) {
            return index;
        }
    }
    return 0;
}

QString AppBackend::deviceName() const { return m_worker.deviceSnapshot().name; }
QString AppBackend::deviceId() const { return m_worker.deviceSnapshot().id; }
bool AppBackend::physicalConnected() const { return m_worker.runtime().physicalConnected.load(); }
int AppBackend::axisCount() const { return m_worker.runtime().axisCount.load(); }
int AppBackend::buttonCount() const { return m_worker.runtime().buttonCount.load(); }
int AppBackend::povCount() const { return m_worker.runtime().povCount.load(); }
int AppBackend::povValue() const { return m_worker.runtime().povValues[0].load(); }
int AppBackend::vjoyButtonCount() const { return m_worker.runtime().vjoyButtonCount.load(); }
int AppBackend::vjoyContinuousPovCount() const
{
    return m_worker.runtime().vjoyContinuousPovCount.load();
}
int AppBackend::vjoyDiscretePovCount() const
{
    return m_worker.runtime().vjoyDiscretePovCount.load();
}
int AppBackend::vjoyRequiredButtonCount() const
{
    return requiredVirtualButtonCount(currentProfile().buttons, currentProfile().povs, buttonCount());
}
bool AppBackend::vjoyCapacitySufficient() const
{
    return vjoyButtonCount() >= vjoyRequiredButtonCount();
}
int AppBackend::lastPhysicalButton() const { return m_worker.runtime().lastPhysicalButton.load(); }
int AppBackend::lastPhysicalButtonTarget() const { return m_worker.runtime().lastPhysicalButtonTarget.load(); }
bool AppBackend::mappingActive() const { return m_worker.runtime().mappingActive.load(); }
bool AppBackend::mappingRequested() const { return m_worker.mappingRequested(); }
QString AppBackend::mappingStatus() const
{
    switch (static_cast<MappingEffectiveState>(m_worker.runtime().mappingEffectiveState.load())) {
    case MappingEffectiveState::Active: return u"MAPPING ACTIVE"_qs;
    case MappingEffectiveState::Suspended: return u"DEVICE MISSING / MAPPING SUSPENDED"_qs;
    case MappingEffectiveState::Off: return u"MAPPING OFF"_qs;
    }
    return u"MAPPING OFF"_qs;
}
bool AppBackend::vjoyReady() const { return m_worker.runtime().vjoyReady.load(); }
QString AppBackend::vjoyStatus() const { return m_worker.vjoyStatus(); }
QString AppBackend::vjoyStatusSeverity() const
{
    if (!vjoyReady()) return u"error"_qs;
    if (!vjoyCapacitySufficient() || vjoyButtonCount() < vjoyRecommendedButtonCount()) {
        return u"warning"_qs;
    }
    return u"ready"_qs;
}
bool AppBackend::hidhideAvailable() const { return m_readiness.plan().hidhide.installed; }
bool AppBackend::hidhideCloakStateKnown() const
{
    return m_readiness.plan().hidhide.cloakKnown;
}
bool AppBackend::hidhideCloaked() const { return m_readiness.plan().hidhide.cloaked; }
bool AppBackend::hidhideMapperAllowed() const
{
    return m_readiness.plan().hidhide.mapperAllowlisted;
}

QVariantList AppBackend::controllerReadinessChecks() const
{
    QVariantList checks;
    for (const QString &finding : m_readiness.plan().findings) {
        const QString upper = finding.toUpper();
        const QString severity = upper.contains(u"READY"_qs) || upper.contains(u"DETECTED"_qs)
            ? u"ready"_qs : (upper.contains(u"MANUAL"_qs) || upper.contains(u"NOT DETECTED"_qs)
            || upper.contains(u"UNAVAILABLE"_qs) ? u"warning"_qs : u"info"_qs);
        checks.append(QVariantMap{{u"message"_qs, finding}, {u"severity"_qs, severity}});
    }
    return checks;
}

QVariantList AppBackend::controllerReadinessProposedChanges() const
{
    QVariantList changes;
    for (const QString &change : m_readiness.plan().proposedChanges) {
        changes.append(QVariantMap{{u"message"_qs, change}});
    }
    return changes;
}

QString AppBackend::controllerReadinessState() const
{
    return ControllerReadinessService::stateLabel(m_readiness.plan().state);
}

QString AppBackend::controllerReadinessStatus() const
{
    const QString status = m_readiness.plan().status;
    return status.isEmpty() ? QStringLiteral("Open Controller Setup to inspect your system.") : status;
}
bool AppBackend::controllerSetupCanApply() const { return m_readiness.plan().canApplyAutomatically; }
bool AppBackend::controllerSetupInProgress() const { return m_readiness.transactionActive(); }
bool AppBackend::controllerSetupCanUndo() const { return m_readiness.canUndo(); }
bool AppBackend::calibrationActive() const { return m_worker.calibrationRunning(); }
bool AppBackend::startMappingOnLaunch() const { return m_configuration.startMappingOnLaunch; }
int AppBackend::vjoyDeviceId() const { return m_configuration.vjoyDeviceId; }

double AppBackend::disabledAxisValue() const
{
    return static_cast<double>(sanitizedDisabledAxisValue(m_configuration.disabledAxisValue)) * 100.0;
}
qulonglong AppBackend::latencyCurrentUs() const { return m_worker.runtime().latencyCurrentUs.load(); }
qulonglong AppBackend::latencyAverageUs() const { return m_worker.runtime().latencyAverageUs.load(); }
qulonglong AppBackend::latencyPeakUs() const { return m_worker.runtime().latencyPeakUs.load(); }
qulonglong AppBackend::profileSwitchCount() const { return m_worker.runtime().profileSwitchCount.load(); }
qulonglong AppBackend::lastProfileSwapUs() const { return m_worker.runtime().lastProfileSwapUs.load(); }
qulonglong AppBackend::lastCurveCompileUs() const { return m_worker.runtime().lastCurveCompileUs.load(); }
bool AppBackend::automationEngineEnabled() const { return m_configuration.automationEnabled; }
int AppBackend::automationRuleCount() const { return static_cast<int>(m_configuration.automations.size()); }
int AppBackend::automationActiveRuleCount() const { return m_worker.runtime().automationActiveRuleCount.load(); }
qulonglong AppBackend::automationEvaluationUs() const { return m_worker.runtime().automationEvaluationUs.load(); }
QString AppBackend::automationValidationMessage() const { return m_automationValidationMessage; }

QVariantList AppBackend::automationRules() const
{
    QVariantList rules;
    const std::shared_ptr<const RuntimeProfileCache> compiled = m_worker.runtimeProfileCache();
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 0; index < static_cast<int>(m_configuration.automations.size()); ++index) {
        const AutomationDefinition &definition = m_configuration.automations[static_cast<size_t>(index)];
        QVariantList conditions;
        QStringList conditionLabels;
        for (const AutomationConditionDefinition &condition : definition.conditions) {
            conditionLabels.append(automationConditionSummary(condition, m_configuration));
            conditions.append(QVariantMap{{u"type"_qs, static_cast<int>(condition.type)},
                {u"axis"_qs, condition.axis}, {u"minimum"_qs, condition.minimum},
                {u"maximum"_qs, condition.maximum}, {u"hysteresis"_qs, condition.hysteresis},
                {u"button"_qs, condition.button}, {u"povHat"_qs, condition.povHat},
                {u"povDirection"_qs, static_cast<int>(condition.povDirection)},
                {u"profileId"_qs, condition.profileId}, {u"pressCount"_qs, condition.pressCount},
                {u"multiPressWindowMs"_qs, condition.multiPressWindowMs},
                {u"longPressDurationMs"_qs, condition.longPressDurationMs}});
        }
        QVariantList actions;
        QStringList actionLabels;
        for (const AutomationActionDefinition &action : definition.actions) {
            actionLabels.append(automationActionSummary(action, m_configuration));
            actions.append(QVariantMap{{u"type"_qs, static_cast<int>(action.type)},
                {u"virtualButton"_qs, action.virtualButton}, {u"profileId"_qs, action.profileId},
                {u"targetAxis"_qs, action.targetAxis}, {u"sourceAxis"_qs, action.sourceAxis},
                {u"sourceStage"_qs, static_cast<int>(action.sourceStage)}, {u"value"_qs, action.value},
                {u"offset"_qs, action.offset}, {u"minimum"_qs, action.minimum}, {u"maximum"_qs, action.maximum},
                {u"tapDurationMs"_qs, action.tapDurationMs}});
        }
        AutomationHealth health = AutomationHealth::Valid;
        QString healthMessage;
        if (compiled && compiled->automation && index < compiled->automation->ruleCount) {
            health = compiled->automation->ruleHealth[static_cast<size_t>(index)];
            healthMessage = compiled->automation->ruleMessages[static_cast<size_t>(index)];
        }
        // Availability is inherently dynamic, so the compiler cannot decide
        // it. Surface a connected device's missing route as repairable
        // attention in the Automation UI without publishing unsafe output.
        QString availabilityMessage;
        if (runtime.physicalConnected.load()) {
            for (const AutomationConditionDefinition &condition : definition.conditions) {
                const bool axisCondition = condition.type == AutomationConditionType::AxisAbove
                    || condition.type == AutomationConditionType::AxisBelow
                    || condition.type == AutomationConditionType::AxisBetween
                    || condition.type == AutomationConditionType::AxisOutsideRange
                    || condition.type == AutomationConditionType::AxisCrossesAbove
                    || condition.type == AutomationConditionType::AxisCrossesBelow;
                if (axisCondition && (condition.axis < 0 || condition.axis >= kPhysicalAxisCount
                    || !runtime.axisAvailable[static_cast<size_t>(condition.axis)].load())) {
                    availabilityMessage = u"Required physical axis is unavailable on the connected controller."_qs;
                    break;
                }
                const bool buttonCondition = condition.type == AutomationConditionType::ButtonHeld
                    || condition.type == AutomationConditionType::ButtonReleased
                    || condition.type == AutomationConditionType::ButtonPressed
                    || condition.type == AutomationConditionType::ButtonReleaseEvent
                    || condition.type == AutomationConditionType::ButtonMultiPress
                    || condition.type == AutomationConditionType::ButtonLongPress;
                if (buttonCondition && (condition.button < 1 || condition.button > kMaximumPhysicalButtons
                    || !runtime.buttonAvailable[static_cast<size_t>(condition.button - 1)].load())) {
                    availabilityMessage = u"Required physical button is unavailable on the connected controller."_qs;
                    break;
                }
                const bool povCondition = condition.type == AutomationConditionType::PovActive
                    || condition.type == AutomationConditionType::PovInactive;
                if (povCondition && (condition.povHat < 1 || condition.povHat > runtime.povCount.load())) {
                    availabilityMessage = u"Required POV hat is unavailable on the connected controller."_qs;
                    break;
                }
            }
            if (availabilityMessage.isEmpty()) {
                for (const AutomationActionDefinition &action : definition.actions) {
                    const bool axisTarget = action.type == AutomationActionType::AxisScale
                        || action.type == AutomationActionType::AxisOffset
                        || action.type == AutomationActionType::AxisClamp
                        || action.type == AutomationActionType::AxisOverride
                        || action.type == AutomationActionType::AxisMix
                        || action.type == AutomationActionType::AxisFollow;
                    const bool usesAxisSource = action.type == AutomationActionType::AxisMix
                        || action.type == AutomationActionType::AxisFollow;
                    if ((axisTarget && (action.targetAxis < 0 || action.targetAxis >= kPhysicalAxisCount
                         || !runtime.axisAvailable[static_cast<size_t>(action.targetAxis)].load()))
                        || (usesAxisSource && (action.sourceAxis < 0 || action.sourceAxis >= kPhysicalAxisCount
                            || !runtime.axisAvailable[static_cast<size_t>(action.sourceAxis)].load()))) {
                        availabilityMessage = u"An Automation axis action references an unavailable controller axis."_qs;
                        break;
                    }
                    if ((action.type == AutomationActionType::VJoyButtonHold
                         || action.type == AutomationActionType::VJoyButtonToggle
                         || action.type == AutomationActionType::VJoyButtonTap)
                        && runtime.vjoyButtonCount.load() > 0
                        && action.virtualButton > runtime.vjoyButtonCount.load()) {
                        availabilityMessage = u"Automation targets a vJoy button not exposed by the active device."_qs;
                        break;
                    }
                }
            }
        }
        if (!availabilityMessage.isEmpty()) {
            health = AutomationHealth::Invalid;
            healthMessage = availabilityMessage;
        }
        const auto always = [](const AutomationConditionDefinition &condition) {
            return condition.type == AutomationConditionType::Always;
        };
        const bool containsAlways = std::any_of(definition.conditions.cbegin(), definition.conditions.cend(), always);
        if (containsAlways) {
            // A single Always condition is a friendly rule-level mode. It is
            // redundant under ALL with other requirements and dominates under
            // ANY, so never force people to decode it on overview cards.
            if (definition.matchMode == AutomationMatchMode::Any || definition.conditions.size() == 1) {
                conditionLabels = {u"All the time"_qs};
            } else {
                QStringList filtered;
                for (int labelIndex = 0; labelIndex < static_cast<int>(definition.conditions.size()); ++labelIndex) {
                    if (!always(definition.conditions[static_cast<size_t>(labelIndex)])) {
                        filtered.append(conditionLabels[labelIndex]);
                    }
                }
                conditionLabels = std::move(filtered);
            }
        }
        const QString connector = definition.matchMode == AutomationMatchMode::Any ? u" OR "_qs : u" AND "_qs;
        rules.append(QVariantMap{{u"id"_qs, definition.id}, {u"name"_qs, definition.name},
            {u"enabled"_qs, definition.enabled}, {u"matchMode"_qs, static_cast<int>(definition.matchMode)},
            {u"activationMode"_qs, static_cast<int>(definition.activationMode)},
            {u"activeDurationMs"_qs, definition.activeDurationMs},
            {u"behaviorLabel"_qs, automationBehaviorLabel(definition.activationMode)},
            {u"priority"_qs, definition.priority}, {u"conditions"_qs, conditions}, {u"actions"_qs, actions},
            {u"conditionSummary"_qs, conditionLabels.join(connector)},
            {u"actionSummary"_qs, actionLabels.join(u" · "_qs)},
            {u"active"_qs, m_worker.runtime().automationRuleActive[static_cast<size_t>(index)].load()},
            {u"health"_qs, static_cast<int>(health)}, {u"healthMessage"_qs, healthMessage}});
    }
    return rules;
}

QStringList AppBackend::buttonOutputChoices() const
{
    QStringList choices{u"Disabled"_qs};
    for (int button = 1; button <= vjoyButtonCount(); ++button) {
        choices.append(QString(u"vJoy Button %1"_qs).arg(button));
    }
    return choices;
}

QStringList AppBackend::virtualAxisChoices() const
{
    QStringList choices{u"Disabled"_qs};
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        const VirtualAxis axis = static_cast<VirtualAxis>(index);
        const bool available = runtime.virtualAxisAvailable[static_cast<size_t>(index)].load();
        bool configured = false;
        for (const AxisMapping &mapping : currentProfile().axes) {
            configured = configured || mapping.target == axis;
        }
        if (available || configured) choices.append(virtualAxisLabel(axis));
    }
    return choices;
}

QString AppBackend::virtualAxisStatus() const
{
    QStringList available;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    for (int index = 1; index < kVirtualAxisSlotCount; ++index) {
        if (runtime.virtualAxisAvailable[static_cast<size_t>(index)].load()) {
            available.append(virtualAxisLabel(static_cast<VirtualAxis>(index)));
        }
    }
    return available.isEmpty() ? u"No vJoy axis reported"_qs
        : u"Axes: "_qs + available.join(u" / "_qs);
}

QStringList AppBackend::mappingControlActionChoices() const
{
    return {u"None"_qs, u"Mapping On"_qs, u"Mapping Off"_qs, u"Toggle Mapping"_qs};
}

QVariantList AppBackend::profileTriggerChoices() const
{
    QVariantList choices;
    choices.append(QVariantMap{{u"id"_qs, QString{}}, {u"label"_qs, u"None"_qs}});
    for (const ControllerProfile &profile : m_configuration.profiles) {
        choices.append(QVariantMap{{u"id"_qs, profile.id}, {u"label"_qs, profile.name}});
    }
    return choices;
}

QVariantList AppBackend::nativePovTargetChoices() const
{
    QVariantList choices;
    for (int index = 1; index <= vjoyContinuousPovCount(); ++index) {
        choices.append(QVariantMap{{u"key"_qs, QString(u"continuous:%1"_qs).arg(index)},
            {u"label"_qs, QString(u"vJoy POV %1 · Continuous"_qs).arg(index)}});
    }
    for (int index = 1; index <= vjoyDiscretePovCount(); ++index) {
        choices.append(QVariantMap{{u"key"_qs, QString(u"discrete:%1"_qs).arg(index)},
            {u"label"_qs, QString(u"vJoy POV %1 · Discrete"_qs).arg(index)}});
    }
    return choices;
}

QStringList AppBackend::profileTriggerBehaviorChoices() const
{
    return {u"Hold"_qs, u"Toggle"_qs};
}

void AppBackend::toggleMapping()
{
    setMappingActive(!m_worker.mappingRequested());
}

void AppBackend::setMappingActive(bool active)
{
    m_worker.setMappingEnabled(active);
    appendEvent(active ? u"Starting mapping…"_qs : u"Stopping mapping…"_qs);
    emit stateChanged();
}

bool AppBackend::setMapping(int physicalAxis, const QString &target, bool explicitOverride)
{
    if (!validAxis(physicalAxis)) return false;
    ControllerProfile &profile = currentProfile();
    const VirtualAxis virtualAxis = virtualAxisFromString(target);
    const int targetIndex = static_cast<int>(virtualAxis);
    if (virtualAxis != VirtualAxis::Disabled
        && (targetIndex < 1 || targetIndex >= kVirtualAxisSlotCount
            || !m_worker.runtime().virtualAxisAvailable[static_cast<size_t>(targetIndex)].load())) {
        appendEvent(u"Selected vJoy axis is not exposed by the active device"_qs);
        return false;
    }
    if (hasMappingConflict(profile.axes, physicalAxis, virtualAxis)) {
        if (!explicitOverride) return false;
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (index != physicalAxis && profile.axes[index].target == virtualAxis) {
                profile.axes[index].target = VirtualAxis::Disabled;
            }
        }
    }
    profile.axes[physicalAxis].target = virtualAxis;
    persistAndApply();
    return true;
}

void AppBackend::setAxisCustomName(int physicalAxis, const QString &name)
{
    if (!validAxis(physicalAxis)) return;
    AxisMapping &mapping = currentProfile().axes[static_cast<size_t>(physicalAxis)];
    const QString normalized = name.trimmed().left(48);
    if (mapping.customName == normalized) return;
    mapping.customName = normalized;
    persistAndApply();
}

void AppBackend::setAxisRangeMode(int physicalAxis, const QString &mode)
{
    if (!validAxis(physicalAxis)) return;
    AxisMapping &mapping = currentProfile().axes[static_cast<size_t>(physicalAxis)];
    const AxisRangeMode normalized = axisRangeModeFromString(mode, mapping.rangeMode);
    if (mapping.rangeMode == normalized) return;
    mapping.rangeMode = normalized;
    // Keep existing limits and curve data intact; only curve-domain metadata
    // is normalized at the configuration boundary.
    normalizeCurveDefinition(mapping.curve, normalized == AxisRangeMode::OneSided);
    persistAndApply();
}

void AppBackend::setVirtualAxisAlias(const QString &target, const QString &alias)
{
    const VirtualAxis axis = virtualAxisFromString(target);
    const int index = static_cast<int>(axis);
    if (index <= 0 || index >= kVirtualAxisSlotCount) return;
    QString normalized = alias.trimmed().left(48);
    currentProfile().virtualAxisAliases[static_cast<size_t>(index)] = normalized;
    persistAndApply();
}

void AppBackend::setSelectedAxis(int physicalAxis)
{
    if (!validAxis(physicalAxis) || m_configuration.selectedAxisIndex == physicalAxis) return;
    m_configuration.selectedAxisIndex = physicalAxis;
    ConfigStore::save(m_configuration);
    rebuildSelectedAxisCurve();
    emit stateChanged();
}

void AppBackend::setAxisInverted(int physicalAxis, bool inverted)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].inverted = inverted;
    persistAndApply();
}

void AppBackend::setAxisDeadzone(int physicalAxis, double deadzone)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].deadzone = std::clamp(static_cast<float>(deadzone), 0.0F, 0.95F);
    persistAndApply();
}

void AppBackend::setAxisHysteresis(int physicalAxis, double hysteresis)
{
    if (!validAxis(physicalAxis)) return;
    currentProfile().axes[physicalAxis].hysteresis = std::clamp(
        static_cast<float>(hysteresis), 0.0F, 0.25F);
    persistAndApply();
}

bool AppBackend::setAxisOutputLimits(int physicalAxis, double minimum, double maximum)
{
    if (!validAxis(physicalAxis)) return false;
    const float boundedMinimum = std::clamp(static_cast<float>(minimum), -1.0F, 1.0F);
    const float boundedMaximum = std::clamp(static_cast<float>(maximum), -1.0F, 1.0F);
    if (boundedMinimum >= boundedMaximum) {
        appendEvent(u"Output minimum must remain below output maximum"_qs);
        return false;
    }
    AxisMapping &mapping = currentProfile().axes[physicalAxis];
    mapping.outputMinimum = boundedMinimum;
    mapping.outputMaximum = boundedMaximum;
    persistAndApply();
    return true;
}

void AppBackend::setCurveFamily(const QString &family)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    const QString normalized = family.trimmed().toCaseFolded();
    if (normalized == u"linear"_qs) {
        mapping->curve = linearCurveDefinition();
    } else if (normalized == u"j-curve"_qs || normalized == u"j"_qs) {
        mapping->curve = standardCurveDefinition(CurveFamily::JCurve, 0.50F);
    } else if (normalized == u"s-curve"_qs || normalized == u"s"_qs) {
        mapping->curve = standardCurveDefinition(CurveFamily::SCurve, 0.50F);
    } else if (normalized == u"advanced"_qs) {
        mapping->curve = advancedCurveDefinition(advancedCurvePresets().front().id);
    } else if (normalized == u"custom"_qs) {
        mapping->curve = materializeCurveDefinition(
            mapping->curve, axisIsOneSided(m_configuration.selectedAxisIndex));
    } else if (normalized == u"personal"_qs) {
        const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
        const auto preset = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [unipolar](const PersonalCurvePreset &entry) {
                return entry.unipolar == unipolar;
            });
        if (preset != m_configuration.personalCurvePresets.cend()) {
            applyPersonalCurvePreset(preset->id);
            return;
        }
        CurveDefinition personal = materializeCurveDefinition(mapping->curve, unipolar);
        personal.family = CurveFamily::Personal;
        personal.sourceFamily = CurveFamily::Personal;
        personal.presetId.clear();
        personal.sourcePresetId.clear();
        personal.baseLabel = u"Unsaved Personal Response"_qs;
        personal.pointEditing = false;
        mapping->curve = std::move(personal);
    } else {
        return;
    }
    persistAndApply();
}

void AppBackend::setCurveStrength(double strength)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || mapping->curve.family == CurveFamily::Linear) return;
    const float bounded = std::clamp(static_cast<float>(strength), 0.0F, 1.0F);
    if (std::abs(mapping->curve.strength - bounded) < 0.0001F) return;
    mapping->curve.strength = bounded;
    persistAndApply();
}

void AppBackend::setCurveStandardPreset(const QString &presetId)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    const CurveFamily family = mapping->curve.family == CurveFamily::SCurve
        ? CurveFamily::SCurve : CurveFamily::JCurve;
    mapping->curve = standardCurveDefinition(family, presetId);
    persistAndApply();
}

void AppBackend::applyAdvancedCurvePreset(const QString &presetId)
{
    if (!advancedCurvePreset(presetId)) return;
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = advancedCurveDefinition(presetId);
    persistAndApply();
}

bool AppBackend::applyPersonalCurvePreset(const QString &presetId)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
        m_configuration.personalCurvePresets.cend(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.cend() || found->unipolar != unipolar) return false;
    mapping->curve = found->definition;
    mapping->curve.family = CurveFamily::Personal;
    mapping->curve.sourceFamily = CurveFamily::Personal;
    mapping->curve.presetId = found->id;
    mapping->curve.sourcePresetId = found->id;
    mapping->curve.baseLabel = found->name;
    persistAndApply();
    appendEvent(u"Applied personal curve preset: "_qs + found->name);
    return true;
}

void AppBackend::setCurvePointEditing(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || mapping->curve.pointEditing == enabled) return;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    if (enabled) {
        mapping->curve = materializeCurveDefinition(mapping->curve, unipolar);
    } else {
        mapping->curve.pointEditing = false;
    }
    persistAndApply();
}

void AppBackend::setCurveInterpolation(const QString &interpolation)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing) return;
    mapping->curve.interpolation = interpolation.trimmed().compare(
        u"linear"_qs, Qt::CaseInsensitive) == 0 ? CurveInterpolation::Linear : CurveInterpolation::Smooth;
    persistAndApply();
}

void AppBackend::setCurvePointDensity(int density)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing || !supportedCurvePointDensity(density)) return;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    mapping->curve = resampleCurveDefinition(mapping->curve, unipolar, density);
    persistAndApply();
}

void AppBackend::setCurveSymmetry(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing
        || axisIsOneSided(m_configuration.selectedAxisIndex)) return;
    mapping->curve.symmetry = enabled;
    normalizeCurveDefinition(mapping->curve, false);
    persistAndApply();
}

bool AppBackend::setCurvePoint(int index, double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = updateCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index,
        static_cast<float>(input), static_cast<float>(output));
    if (changed) persistAndApply();
    return changed;
}

bool AppBackend::setCurvePointLocked(int index, bool locked)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::setCurvePointLocked(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index, locked);
    if (changed) persistAndApply();
    return changed;
}

int AppBackend::addCurvePoint(double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return -1;
    int selected = -1;
    if (!hotas::addCurvePoint(mapping->curve,
            axisIsOneSided(m_configuration.selectedAxisIndex),
            static_cast<float>(input), static_cast<float>(output), &selected)) return -1;
    persistAndApply();
    return selected;
}

bool AppBackend::removeCurvePoint(int index)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::removeCurvePoint(mapping->curve,
        axisIsOneSided(m_configuration.selectedAxisIndex), index);
    if (changed) persistAndApply();
    return changed;
}

void AppBackend::resetCurveLinear()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return;
    mapping->curve = linearCurveDefinition();
    persistAndApply();
}

bool AppBackend::resetCurveToSource()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const CurveDefinition current = mapping->curve;
    if (current.sourceFamily == CurveFamily::JCurve || current.sourceFamily == CurveFamily::SCurve) {
        mapping->curve = standardCurveDefinition(current.sourceFamily,
            current.sourcePresetId.isEmpty() ? u"medium"_qs : current.sourcePresetId);
    } else if (current.sourceFamily == CurveFamily::Advanced) {
        mapping->curve = advancedCurveDefinition(current.sourcePresetId);
    } else if (current.sourceFamily == CurveFamily::Personal) {
        if (!applyPersonalCurvePreset(current.sourcePresetId)) return false;
        return true;
    } else {
        mapping->curve = linearCurveDefinition();
    }
    persistAndApply();
    return true;
}

bool AppBackend::copyCurveFrom(const QString &profileId, int axisIndex)
{
    if (!validAxis(axisIndex)) return false;
    const ControllerProfile *source = findProfile(m_configuration, profileId);
    AxisMapping *target = selectedAxisMapping();
    if (!source || !target) return false;
    const bool targetUnipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const bool sourceUnipolar = source->axes[static_cast<size_t>(axisIndex)].rangeMode
        == AxisRangeMode::OneSided;
    const CurveDefinition &curve = source->axes[axisIndex].curve;
    if (targetUnipolar != sourceUnipolar && (curve.pointEditing || curve.family == CurveFamily::Custom)) {
        appendEvent(u"Point-edited curves can only copy to a compatible axis domain"_qs);
        return false;
    }
    target->curve = curve;
    persistAndApply();
    appendEvent(u"Copied response curve from "_qs + source->name);
    return true;
}

bool AppBackend::copyCurveFromSelection(const QString &selectionId)
{
    const int separator = selectionId.lastIndexOf(u':');
    if (separator <= 0) return false;
    bool valid = false;
    const int axis = selectionId.mid(separator + 1).toInt(&valid);
    return valid && copyCurveFrom(selectionId.left(separator), axis);
}

bool AppBackend::saveCurrentCurveAsPersonalPreset(const QString &name)
{
    const QString trimmed = name.trimmed();
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !personalCurvePresetNameAvailable(m_configuration.personalCurvePresets, trimmed)) {
        appendEvent(u"Personal preset names must be unique and between 1 and 48 characters"_qs);
        return false;
    }
    PersonalCurvePreset preset;
    preset.id = u"curve-"_qs + QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = trimmed;
    preset.unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    preset.definition = mapping->curve;
    // A library entry owns a concrete curve shape. Materialize generated
    // families once so applying it later is always a copy, never a reference
    // back to a mutable source preset.
    if (preset.definition.points.empty()) {
        preset.definition = materializeCurveDefinition(preset.definition, preset.unipolar);
        preset.definition.pointEditing = false;
    }
    m_configuration.personalCurvePresets.push_back(std::move(preset));
    persistAndApply();
    appendEvent(u"Saved personal curve preset: "_qs + trimmed);
    return true;
}

bool AppBackend::renamePersonalCurvePreset(const QString &presetId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!personalCurvePresetNameAvailable(m_configuration.personalCurvePresets, trimmed, presetId)) return false;
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end()) return false;
    found->name = trimmed;
    persistAndApply();
    return true;
}

bool AppBackend::deletePersonalCurvePreset(const QString &presetId)
{
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end()) return false;
    const QString name = found->name;
    m_configuration.personalCurvePresets.erase(found);
    persistAndApply();
    appendEvent(u"Deleted personal curve preset: "_qs + name);
    return true;
}

bool AppBackend::updatePersonalCurvePreset(const QString &presetId)
{
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const auto found = std::find_if(m_configuration.personalCurvePresets.begin(),
        m_configuration.personalCurvePresets.end(), [&presetId](const PersonalCurvePreset &preset) {
            return preset.id == presetId;
        });
    if (found == m_configuration.personalCurvePresets.end() || found->unipolar != unipolar) return false;
    found->definition = mapping->curve;
    if (found->definition.points.empty()) {
        found->definition = materializeCurveDefinition(found->definition, unipolar);
        found->definition.pointEditing = false;
    }
    found->definition.sourceFamily = CurveFamily::Personal;
    found->definition.sourcePresetId = found->id;
    persistAndApply();
    appendEvent(u"Updated personal curve preset: "_qs + found->name);
    return true;
}

void AppBackend::setCurveComparison(const QString &comparisonId)
{
    m_curveComparisonId.clear();
    m_curveComparisonLabel.clear();
    if (!comparisonId.isEmpty()) {
        const QVariantList choices = curveComparisonChoices();
        for (const QVariant &choiceValue : choices) {
            const QVariantMap choice = choiceValue.toMap();
            if (choice.value(u"id"_qs).toString() != comparisonId) continue;
            m_curveComparisonId = comparisonId;
            m_curveComparisonLabel = choice.value(u"label"_qs).toString();
            break;
        }
    }
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

QVariantMap AppBackend::inspectCurve(double domainInput) const
{
    QVariantMap result;
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return result;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    const float input = std::clamp(static_cast<float>(domainInput), unipolar ? 0.0F : -1.0F, 1.0F);
    result.insert(u"input"_qs, input);
    result.insert(u"output"_qs, evaluateCurveDefinition(input, mapping->curve, unipolar));
    result.insert(u"gain"_qs, evaluateCurveGain(input, mapping->curve, unipolar));
    return result;
}

QString AppBackend::curveEditorSnapshot() const
{
    const AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return {};
    return QString::fromUtf8(QJsonDocument(curveDefinitionToJson(mapping->curve))
        .toJson(QJsonDocument::Compact));
}

bool AppBackend::restoreCurveEditorSnapshot(const QString &snapshot)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const QJsonDocument document = QJsonDocument::fromJson(snapshot.toUtf8());
    if (!document.isObject()) return false;
    const bool unipolar = axisIsOneSided(m_configuration.selectedAxisIndex);
    CurveDefinition restored = curveDefinitionFromJson(document.object(), unipolar);
    if (!curveDefinitionIsValid(restored, unipolar)) return false;
    mapping->curve = std::move(restored);
    persistAndApply();
    return true;
}

void AppBackend::previewCurvePreset(const QString &presetId)
{
    const QStringList parts = presetId.split(u':');
    CurveDefinition definition;
    if (parts.size() == 3 && parts[0] == u"standard"_qs) {
        definition = standardCurveDefinition(parts[1] == u"s"_qs ? CurveFamily::SCurve
                                                                   : CurveFamily::JCurve,
                                           parts[2]);
    } else if (parts.size() == 2 && parts[0] == u"advanced"_qs) {
        definition = advancedCurveDefinition(parts[1]);
    } else if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found == m_configuration.personalCurvePresets.cend()) return;
        definition = found->definition;
    } else {
        return;
    }
    m_curvePreviewId = presetId;
    m_curvePreviewDefinition = std::move(definition);
    for (const QVariant &choice : curvePreviewChoices()) {
        const QVariantMap item = choice.toMap();
        if (item.value(u"id"_qs).toString() == presetId) {
            m_curvePreviewLabel = item.value(u"label"_qs).toString();
            break;
        }
    }
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

void AppBackend::clearCurvePreview()
{
    if (m_curvePreviewId.isEmpty()) return;
    m_curvePreviewId.clear();
    m_curvePreviewLabel.clear();
    m_curvePreviewDefinition = linearCurveDefinition();
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
}

bool AppBackend::applyCurvePreview()
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || m_curvePreviewId.isEmpty()) return false;
    mapping->curve = m_curvePreviewDefinition;
    const QStringList parts = m_curvePreviewId.split(u':');
    if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found != m_configuration.personalCurvePresets.cend()) {
            mapping->curve.family = CurveFamily::Personal;
            mapping->curve.presetId = found->id;
            mapping->curve.baseLabel = found->name;
            mapping->curve.sourcePresetId = found->id;
        }
    }
    if (mapping->curve.family == CurveFamily::Personal) {
        mapping->curve.sourceFamily = CurveFamily::Personal;
    }
    m_curvePreviewId.clear();
    m_curvePreviewLabel.clear();
    m_curvePreviewDefinition = linearCurveDefinition();
    persistAndApply();
    return true;
}

bool AppBackend::setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride)
{
    if (!validPhysicalButton(physicalButton) || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    const int source = physicalButton - 1;
    ButtonBindings &bindings = currentProfile().buttons;
    if (bindings.size() <= static_cast<size_t>(source)) {
        bindings.resize(static_cast<size_t>(source + 1));
    }
    if (hasButtonMappingConflict(bindings, currentProfile().povs, source, virtualButton,
                                 vjoyButtonCount())) {
        if (!explicitOverride) return false;
        for (int index = 0; index < static_cast<int>(bindings.size()); ++index) {
            ButtonBinding &binding = bindings[static_cast<size_t>(index)];
            if (index != source && binding.type == ButtonActionType::VirtualButton
                && binding.target == virtualButton) {
                binding = {};
                // Replacing a destination intentionally leaves its former source unused.
                // Keep that choice from being treated as an implicit default on reconnect.
                binding.explicitlyConfigured = true;
            }
        }
        for (PovDirectionBindings &hat : currentProfile().povs) {
            for (ButtonBinding &binding : hat) {
                if (binding.type == ButtonActionType::VirtualButton
                    && binding.target == virtualButton) {
                    binding = {};
                    binding.explicitlyConfigured = true;
                }
            }
        }
    }
    ButtonBinding &updated = bindings[static_cast<size_t>(source)];
    const QString customName = updated.customName;
    updated = virtualButton > 0
        ? ButtonBinding{ButtonActionType::VirtualButton, virtualButton} : ButtonBinding{};
    updated.customName = customName;
    updated.explicitlyConfigured = true;
    persistAndApply();
    appendEvent(QString(u"Button %1 → %2"_qs).arg(physicalButton).arg(
        virtualButton > 0 ? QString(u"vJoy %1"_qs).arg(virtualButton) : u"Disabled"_qs));
    return true;
}

void AppBackend::setButtonCustomName(int physicalButton, const QString &name)
{
    if (!validPhysicalButton(physicalButton)) return;
    const int source = physicalButton - 1;
    ButtonBindings &bindings = currentProfile().buttons;
    if (bindings.size() <= static_cast<size_t>(source)) {
        bindings.resize(static_cast<size_t>(source + 1));
    }
    ButtonBinding &binding = bindings[static_cast<size_t>(source)];
    const QString normalized = name.trimmed().left(48);
    if (binding.customName == normalized) return;
    binding.customName = normalized;
    persistAndApply();
}

bool AppBackend::setMappingControl(int physicalButton, const QString &action)
{
    if (!validPhysicalButton(physicalButton)) return false;
    const int source = physicalButton - 1;
    const MappingControlAction normalized = mappingControlActionFromString(action);
    if (m_configuration.mappingControls.size() <= static_cast<size_t>(source)) {
        m_configuration.mappingControls.resize(static_cast<size_t>(source + 1));
    }
    if (m_configuration.mappingControls[static_cast<size_t>(source)] == normalized) return true;
    m_configuration.mappingControls[static_cast<size_t>(source)] = normalized;
    persistAndApply();
    appendEvent(QString(u"Button %1 mapping control: %2"_qs).arg(physicalButton)
        .arg(mappingControlActionLabel(normalized)));
    return true;
}

bool AppBackend::setPovMapping(int povHat, int direction, int virtualButton, bool explicitOverride)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= povCount() || hat >= kMaximumPhysicalPovs
        || direction < 0 || direction >= kPovDirectionCount || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    PovBindings &povs = currentProfile().povs;
    if (povs.size() <= static_cast<size_t>(hat)) povs.resize(static_cast<size_t>(hat + 1));
    if (hasPovMappingConflict(currentProfile().buttons, povs, hat, direction, virtualButton,
                              vjoyButtonCount())) {
        if (!explicitOverride) return false;
        for (ButtonBinding &binding : currentProfile().buttons) {
            if (binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton) {
                binding = {};
                binding.explicitlyConfigured = true;
            }
        }
        for (int otherHat = 0; otherHat < static_cast<int>(povs.size()); ++otherHat) {
            for (int otherDirection = 0; otherDirection < kPovDirectionCount; ++otherDirection) {
                if (otherHat == hat && otherDirection == direction) continue;
                ButtonBinding &binding = povs[static_cast<size_t>(otherHat)][static_cast<size_t>(otherDirection)];
                if (binding.type == ButtonActionType::VirtualButton && binding.target == virtualButton) {
                    binding = {};
                    binding.explicitlyConfigured = true;
                }
            }
        }
    }
    ButtonBinding &binding = povs[static_cast<size_t>(hat)][static_cast<size_t>(direction)];
    binding = virtualButton > 0 ? ButtonBinding{ButtonActionType::VirtualButton, virtualButton}
                                : ButtonBinding{};
    binding.explicitlyConfigured = true;
    persistAndApply();
    appendEvent(QString(u"POV %1 %2 → %3"_qs).arg(povHat)
        .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)))
        .arg(virtualButton > 0 ? QString(u"vJoy %1"_qs).arg(virtualButton) : u"Disabled"_qs));
    return true;
}

bool AppBackend::setProfileTrigger(int physicalButton, const QString &targetProfileId,
                                   const QString &behavior)
{
    const int source = physicalButton - 1;
    if (source < 0 || source >= kMaximumPhysicalButtons) return false;
    if (m_configuration.profileTriggers.size() <= static_cast<size_t>(source)) {
        m_configuration.profileTriggers.resize(static_cast<size_t>(source + 1));
    }
    ProfileTriggerBinding &trigger = m_configuration.profileTriggers[static_cast<size_t>(source)];
    const QString target = targetProfileId.trimmed();
    if (target.isEmpty()) {
        trigger = {};
        persistAndApply();
        appendEvent(QString(u"Button %1 profile control cleared; game route restored"_qs).arg(physicalButton));
        return true;
    }
    const ControllerProfile *profile = findProfile(m_configuration, target);
    if (!profile) return false;
    const ProfileTriggerMode mode = profileTriggerModeFromString(behavior);
    if (mode == ProfileTriggerMode::Disabled) return false;
    trigger = {target, mode};
    persistAndApply();
    appendEvent(QString(u"Button %1 → profile %2 · %3 (game route consumed)"_qs)
        .arg(physicalButton).arg(profile->name).arg(profileTriggerModeLabel(mode)));
    return true;
}

bool AppBackend::setPovProfileTrigger(int povHat, int direction,
                                      const QString &targetProfileId, const QString &behavior)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= kMaximumPhysicalPovs || direction < 0 || direction >= kPovDirectionCount) {
        return false;
    }
    if (m_configuration.povProfileTriggers.size() <= static_cast<size_t>(hat)) {
        m_configuration.povProfileTriggers.resize(static_cast<size_t>(hat + 1));
    }
    ProfileTriggerBinding &trigger = m_configuration.povProfileTriggers[static_cast<size_t>(hat)]
        [static_cast<size_t>(direction)];
    const QString target = targetProfileId.trimmed();
    const QString inputLabel = QString(u"POV %1 %2"_qs).arg(povHat)
        .arg(povDirectionLabel(static_cast<PovDirection>(direction + 1)));
    if (target.isEmpty()) {
        trigger = {};
        persistAndApply();
        appendEvent(inputLabel + u" profile control cleared; game route restored"_qs);
        return true;
    }
    const ControllerProfile *profile = findProfile(m_configuration, target);
    if (!profile) return false;
    const ProfileTriggerMode mode = profileTriggerModeFromString(behavior);
    if (mode == ProfileTriggerMode::Disabled) return false;
    trigger = {target, mode};
    persistAndApply();
    appendEvent(QString(u"%1 → profile %2 · %3 (direction route consumed)"_qs)
        .arg(inputLabel).arg(profile->name).arg(profileTriggerModeLabel(mode)));
    return true;
}

bool AppBackend::setNativePovOutput(int povHat, bool enabled, const QString &targetKey)
{
    const int hat = povHat - 1;
    if (hat < 0 || hat >= kMaximumPhysicalPovs) return false;
    if (m_configuration.nativePovBindings.size() <= static_cast<size_t>(hat)) {
        m_configuration.nativePovBindings.resize(static_cast<size_t>(hat + 1));
    }
    NativePovBinding binding;
    if (enabled) {
        const QStringList parts = targetKey.split(u":"_qs);
        if (parts.size() != 2) return false;
        if (parts[0] == u"continuous"_qs) binding.targetType = NativePovTargetType::Continuous;
        else if (parts[0] == u"discrete"_qs) binding.targetType = NativePovTargetType::Discrete;
        else return false;
        bool ok = false;
        binding.targetIndex = parts[1].toInt(&ok);
        if (!ok || binding.targetIndex < 1) return false;
        const bool targetAvailable = binding.targetType == NativePovTargetType::Continuous
            ? binding.targetIndex <= vjoyContinuousPovCount()
            : binding.targetIndex <= vjoyDiscretePovCount();
        if (!targetAvailable) return false;
        for (int otherHat = 0; otherHat < static_cast<int>(m_configuration.nativePovBindings.size()); ++otherHat) {
            if (otherHat == hat) continue;
            const NativePovBinding &existing = m_configuration.nativePovBindings[static_cast<size_t>(otherHat)];
            if (existing.enabled && existing.targetType == binding.targetType
                && existing.targetIndex == binding.targetIndex) {
                appendEvent(u"Each native vJoy POV target can have only one physical POV owner"_qs);
                return false;
            }
        }
        binding.enabled = true;
    }
    m_configuration.nativePovBindings[static_cast<size_t>(hat)] = binding;
    persistAndApply();
    appendEvent(enabled ? QString(u"POV %1 native vJoy output enabled"_qs).arg(povHat)
                        : QString(u"POV %1 native vJoy output disabled"_qs).arg(povHat));
    return true;
}

void AppBackend::resetButtonMappings()
{
    const int physicalCount = buttonCount();
    const int virtualCount = vjoyButtonCount();
    if (physicalCount <= 0) {
        appendEvent(u"Connect a controller before resetting button mappings"_qs);
        return;
    }
    currentProfile().buttons = defaultButtonMappings(physicalCount, virtualCount);
    persistAndApply();
    appendEvent(u"Button mappings reset to passthrough defaults"_qs);
}

bool AppBackend::createProfile(const QString &name, const QString &startFromId)
{
    const QString trimmedName = name.trimmed();
    if (!hotas::createProfile(m_configuration, trimmedName, startFromId)) {
        appendEvent(u"Profile name must be unique and between 1 and 48 characters"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Created profile: "_qs + trimmedName);
    return true;
}

bool AppBackend::cloneProfile(const QString &profileId)
{
    const ControllerProfile *source = findProfile(m_configuration, profileId);
    if (!source) return false;
    const QString sourceName = source->name;
    if (!hotas::cloneProfile(m_configuration, profileId)) return false;
    persistAndApply();
    appendEvent(u"Cloned profile: "_qs + sourceName);
    return true;
}

bool AppBackend::renameProfile(const QString &profileId, const QString &name)
{
    ControllerProfile *profile = findProfile(m_configuration, profileId);
    const QString trimmedName = name.trimmed();
    if (!profile) {
        return false;
    }
    const QString previousName = profile->name;
    if (!hotas::renameProfile(m_configuration, profileId, trimmedName)) {
        appendEvent(u"Profile name must be unique and between 1 and 48 characters"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Renamed profile: "_qs + previousName + u" → "_qs + trimmedName);
    return true;
}

bool AppBackend::deleteProfile(const QString &profileId)
{
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile) return false;
    const QString name = profile->name;
    if (!hotas::deleteProfile(m_configuration, profileId)) {
        appendEvent(u"Activate another profile before deleting this one"_qs);
        return false;
    }
    persistAndApply();
    appendEvent(u"Deleted profile: "_qs + name);
    return true;
}

bool AppBackend::activateProfile(const QString &profileId)
{
    const ControllerProfile *profile = findProfile(m_configuration, profileId);
    if (!profile) return false;
    if (profileId == m_configuration.activeProfileId) return true;
    const QString name = profile->name;
    if (!hotas::activateProfile(m_configuration, profileId)) return false;
    persistAndApply();
    appendEvent(u"Activated profile: "_qs + name);
    return true;
}

void AppBackend::beginCalibration()
{
    m_worker.beginCalibration();
    appendEvent(u"Calibration capture started; center controls, then move through full travel"_qs);
    emit stateChanged();
}

bool AppBackend::saveCalibration()
{
    const auto captured = m_worker.capturedCalibration();
    bool savedAny = false;
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!m_worker.runtime().axisAvailable[index].load()) continue;
        const Calibration calibration = captured[index];
        if (calibration.minimum < calibration.center && calibration.center < calibration.maximum) {
            m_configuration.calibration[index] = calibration;
            savedAny = true;
        }
    }
    m_worker.cancelCalibration();
    if (savedAny) {
        persistAndApply();
        appendEvent(u"Calibration saved"_qs);
    } else {
        appendEvent(u"Calibration needs movement on both sides of the captured center"_qs);
    }
    emit stateChanged();
    return savedAny;
}

void AppBackend::resetCalibration()
{
    m_worker.cancelCalibration();
    for (Calibration &calibration : m_configuration.calibration) calibration = Calibration{};
    persistAndApply();
    appendEvent(u"Calibration reset"_qs);
}

void AppBackend::setStartMappingOnLaunch(bool enabled)
{
    m_configuration.startMappingOnLaunch = enabled;
    persistAndApply();
}

void AppBackend::setVjoyDeviceId(int deviceId)
{
    m_configuration.vjoyDeviceId = std::clamp(deviceId, 1, 16);
    persistAndApply();
}

void AppBackend::setAutomationEngineEnabled(bool enabled)
{
    if (m_configuration.automationEnabled == enabled) return;
    m_configuration.automationEnabled = enabled;
    persistAndApply();
    appendEvent(enabled ? u"Automation engine enabled"_qs : u"Automation engine disabled"_qs);
}

QString AppBackend::createAutomation()
{
    if (static_cast<int>(m_configuration.automations.size()) >= kMaximumAutomationRules) {
        m_automationValidationMessage = u"Automation limit is 64 rules."_qs;
        emit stateChanged();
        return {};
    }
    AutomationDefinition automation;
    automation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    automation.name = u"New Automation"_qs;
    int suffix = 2;
    const auto nameTaken = [this, &automation] {
        return std::any_of(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
            [&automation](const AutomationDefinition &existing) {
                return existing.name.compare(automation.name, Qt::CaseInsensitive) == 0;
            });
    };
    while (nameTaken()) automation.name = QString(u"New Automation %1"_qs).arg(suffix++);
    // A new rule is an intentionally incomplete, disabled draft. It is
    // persisted so a user can safely leave and resume editing, but cannot
    // enter the compiled runtime set until Save supplies valid conditions and
    // actions. Never invent a default action that can alter flight controls.
    automation.enabled = false;
    m_configuration.automations.push_back(std::move(automation));
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation draft created"_qs);
    return m_configuration.automations.back().id;
}

QString AppBackend::duplicateAutomation(const QString &id)
{
    if (static_cast<int>(m_configuration.automations.size()) >= kMaximumAutomationRules) return {};
    const auto found = std::find_if(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.cend()) return {};
    AutomationDefinition copy = *found;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.enabled = false;
    const QString sourceName = copy.name.trimmed();
    int suffix = 2;
    QString suffixText = u" Copy"_qs;
    do {
        copy.name = sourceName.left(64 - suffixText.size()).trimmed() + suffixText;
        suffixText = QString(u" Copy %1"_qs).arg(suffix++);
    } while (std::any_of(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&copy](const AutomationDefinition &automation) {
            return automation.name.compare(copy.name, Qt::CaseInsensitive) == 0;
        }));
    m_configuration.automations.push_back(std::move(copy));
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation duplicated"_qs);
    return m_configuration.automations.back().id;
}

bool AppBackend::deleteAutomation(const QString &id)
{
    const auto found = std::find_if(m_configuration.automations.cbegin(), m_configuration.automations.cend(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.cend()) return false;
    const QString name = found->name;
    m_configuration.automations.erase(found);
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation deleted: "_qs + name);
    return true;
}

bool AppBackend::setAutomationEnabled(const QString &id, bool enabled)
{
    const auto found = std::find_if(m_configuration.automations.begin(), m_configuration.automations.end(),
        [&id](const AutomationDefinition &automation) { return automation.id == id; });
    if (found == m_configuration.automations.end() || found->enabled == enabled) return false;
    if (enabled) {
        MapperConfiguration proposed = m_configuration;
        const auto proposedRule = std::find_if(proposed.automations.begin(), proposed.automations.end(),
            [&id](const AutomationDefinition &automation) { return automation.id == id; });
        proposedRule->enabled = true;
        const RuntimeProfileCache compiled = compileRuntimeProfileCache(proposed);
        const int index = static_cast<int>(std::distance(proposed.automations.begin(), proposedRule));
        if (!compiled.automation || !compiled.automation->publishable
            || compiled.automation->ruleHealth[static_cast<size_t>(index)] == AutomationHealth::Invalid) {
            m_automationValidationMessage = compiled.automation && !compiled.automation->ruleMessages[
                static_cast<size_t>(index)].isEmpty() ? compiled.automation->ruleMessages[static_cast<size_t>(index)]
                : (compiled.automation ? compiled.automation->message : u"Automation validation failed."_qs);
            emit stateChanged();
            return false;
        }
    }
    found->enabled = enabled;
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent((enabled ? u"Automation enabled: "_qs : u"Automation disabled: "_qs) + found->name);
    return true;
}

bool AppBackend::saveAutomation(const QVariantMap &automation)
{
    AutomationDefinition candidate;
    QString reason;
    if (!automationDefinitionFromVariant(automation, &candidate, &reason)) {
        m_automationValidationMessage = reason;
        emit stateChanged();
        return false;
    }
    const auto found = std::find_if(m_configuration.automations.begin(), m_configuration.automations.end(),
        [&candidate](const AutomationDefinition &existing) { return existing.id == candidate.id; });
    if (found == m_configuration.automations.end()) {
        m_automationValidationMessage = u"Automation no longer exists."_qs;
        emit stateChanged();
        return false;
    }
    MapperConfiguration proposed = m_configuration;
    const auto proposedRule = std::find_if(proposed.automations.begin(), proposed.automations.end(),
        [&candidate](const AutomationDefinition &existing) { return existing.id == candidate.id; });
    *proposedRule = candidate;
    const RuntimeProfileCache compiled = compileRuntimeProfileCache(proposed);
    if (!compiled.automation || !compiled.automation->publishable
        || compiled.automation->ruleHealth[static_cast<size_t>(std::distance(
            proposed.automations.begin(), proposedRule))] == AutomationHealth::Invalid) {
        const int index = static_cast<int>(std::distance(proposed.automations.begin(), proposedRule));
        m_automationValidationMessage = compiled.automation && !compiled.automation->ruleMessages[
            static_cast<size_t>(index)].isEmpty() ? compiled.automation->ruleMessages[static_cast<size_t>(index)]
            : (compiled.automation ? compiled.automation->message : u"Automation validation failed."_qs);
        emit stateChanged();
        return false;
    }
    *found = std::move(candidate);
    m_automationValidationMessage.clear();
    persistAndApply();
    appendEvent(u"Automation saved: "_qs + found->name);
    return true;
}

void AppBackend::setDisabledAxisValue(double percent)
{
    const float normalized = std::isfinite(percent)
        ? sanitizedDisabledAxisValue(static_cast<float>(std::clamp(percent, -100.0, 100.0) / 100.0))
        : 0.0F;
    if (std::abs(m_configuration.disabledAxisValue - normalized) < 0.00001F) return;
    m_configuration.disabledAxisValue = normalized;
    persistAndApply();
    appendEvent(QString(u"Disabled Axis Value set to %1%"_qs)
        .arg(static_cast<double>(normalized) * 100.0, 0, 'f', 1));
}

void AppBackend::checkForUpdates()
{
    if (m_updateChecking) return;
    m_updateChecking = true;
    m_updateTimedOut = false;
    m_updateCheckFailed = false;
    m_updateStatusText = u"Checking for updates…"_qs;
    appendEvent(u"Update check started"_qs);

    QNetworkRequest request(QUrl(QString::fromUtf8(hotas::launcher::updateManifestUrl().data(),
                                                     static_cast<qsizetype>(hotas::launcher::updateManifestUrl().size()))));
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QString(u"HOTAS-BF6/%1"_qs).arg(QString::fromLatin1(HOTAS_BF6_VERSION)));
    QNetworkReply *reply = m_updateNetworkManager.get(request);
    m_updateReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        finishUpdateCheck(reply);
    });
    m_updateTimeout.start(3000);
    emit stateChanged();
}

void AppBackend::finishUpdateCheck(QNetworkReply *reply)
{
    if (!reply || reply != m_updateReply) {
        if (reply) reply->deleteLater();
        return;
    }
    m_updateTimeout.stop();
    m_updateReply = nullptr;
    m_updateChecking = false;
    const bool timedOut = m_updateTimedOut;
    m_updateTimedOut = false;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray manifestJson = networkError == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
    const QString networkReason = timedOut ? u"request timed out"_qs : reply->errorString();
    reply->deleteLater();

    if (timedOut || networkError != QNetworkReply::NoError) {
        failUpdateCheck(networkReason);
        return;
    }
    if (manifestJson.isEmpty() || manifestJson.size() > 64 * 1024) {
        failUpdateCheck(u"release metadata was empty or too large"_qs);
        return;
    }

    hotas::launcher::SemanticVersion localVersion{};
    std::string reason;
    if (!hotas::launcher::parseSemanticVersion(HOTAS_BF6_VERSION, localVersion, &reason)) {
        failUpdateCheck(u"installed application version is invalid"_qs);
        return;
    }
    hotas::launcher::UpdateManifest manifest;
    const std::string response(manifestJson.constData(), static_cast<size_t>(manifestJson.size()));
    const hotas::launcher::UpdateAction action = hotas::launcher::decideUpdate(
        true, response, localVersion, &manifest, &reason);
    if (action == hotas::launcher::UpdateAction::InstallUpdate) {
        m_updateAvailable = true;
        m_updateCheckFailed = false;
        m_updateAvailableVersion = QString(u"v%1"_qs).arg(QString::fromStdString(manifest.versionText));
        m_updateStatusText = QString(u"%1 is available"_qs).arg(m_updateAvailableVersion);
        appendEvent(QString(u"Update available: %1"_qs).arg(m_updateAvailableVersion));
    } else if (reason == "installed version is current or newer") {
        m_updateAvailable = false;
        m_updateCheckFailed = false;
        m_updateAvailableVersion.clear();
        m_updateStatusText = u"You're running the latest version."_qs;
        appendEvent(u"Update check completed: application is current"_qs);
    } else {
        failUpdateCheck(QString::fromStdString(reason));
        return;
    }
    emit stateChanged();
}

void AppBackend::failUpdateCheck(const QString &reason)
{
    m_updateChecking = false;
    m_updateAvailable = false;
    m_updateCheckFailed = true;
    m_updateAvailableVersion.clear();
    m_updateStatusText = u"Update status unavailable"_qs;
    appendEvent(QString(u"Update check error: %1"_qs).arg(reason));
    emit stateChanged();
}

bool AppBackend::handoffToLauncher()
{
    if (!m_updateAvailable) return false;
    // Ensure the existing QSettings record is durable before a separate
    // launcher process takes over. A launcher start is confirmed first; only
    // then do we begin stopping controller I/O and exit this application.
    ConfigStore::save(m_configuration);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QString launcherPath = QDir(applicationDirectory).filePath(u"HOTAS BF6 Launcher.exe"_qs);
    if (!QFileInfo(launcherPath).isExecutable()) {
        m_updateStatusText = u"Update available, but the HOTAS BF6 Launcher was not found."_qs;
        appendEvent(QString(u"Launcher-start failure: %1"_qs).arg(launcherPath));
        emit stateChanged();
        return false;
    }
    const QStringList arguments{u"--wait-for-pid"_qs,
                                QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(launcherPath, arguments, applicationDirectory)) {
        m_updateStatusText = u"Update available, but the launcher could not be started."_qs;
        appendEvent(QString(u"Launcher-start failure: %1"_qs).arg(launcherPath));
        emit stateChanged();
        return false;
    }
    appendEvent(QString(u"Launcher started for update handoff: %1"_qs).arg(launcherPath));
    m_worker.setMappingEnabled(false);
    QTimer::singleShot(100, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
    return true;
}

bool AppBackend::openVjoyConfiguration()
{
    if (!m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not release vJoy for manual configuration; Mapping remains Off"_qs);
        return false;
    }
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
        u"C:/Program Files"_qs,
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString executable = QDir(root).filePath(u"vJoy/x64/vJoyConf.exe"_qs);
        if (QFileInfo(executable).isExecutable() && QProcess::startDetached(executable)) {
            appendEvent(u"Mapping stopped; opened the supported vJoy configuration utility"_qs);
            return true;
        }
    }
    appendEvent(u"vJoy configuration utility was not found"_qs);
    return false;
}

void AppBackend::refreshHidHideStatus()
{
    inspectControllerReadiness();
    appendEvent(hidhideAvailable()
        ? u"HidHide status refreshed"_qs
        : u"HidHide is not installed or its service is unavailable"_qs);
}

bool AppBackend::openHidHideConfiguration()
{
    const QStringList roots{
        qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"),
        u"C:/Program Files"_qs,
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString executable = QDir(root).filePath(
            u"Nefarius Software Solutions/HidHide/x64/HidHideClient.exe"_qs);
        if (QFileInfo(executable).isExecutable() && QProcess::startDetached(executable)) {
            appendEvent(u"Opened the HidHide Configuration Client"_qs);
            return true;
        }
    }
    appendEvent(u"HidHide Configuration Client was not found"_qs);
    return false;
}

void AppBackend::inspectControllerReadiness()
{
    const ControllerReadinessPlan &plan = m_readiness.inspect(m_configuration, currentPhysicalCapabilities());
    appendEvent(QString(u"Controller readiness: %1"_qs).arg(plan.status));
    emit stateChanged();
}

bool AppBackend::applyControllerReadiness()
{
    // The worker owns vJoy while mapping normally. Release it before the
    // privileged control-plane transaction; it remains explicitly Off after.
    if (!m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Controller setup could not release vJoy; Mapping remains Off"_qs);
        emit stateChanged();
        return false;
    }
    m_readiness.inspect(m_configuration, currentPhysicalCapabilities());
    if (!m_readiness.plan().canApplyAutomatically) {
        appendEvent(u"Controller setup needs manual review before it can apply changes"_qs);
        emit stateChanged();
        return false;
    }
    const bool completed = m_readiness.applyAutomatically();
    appendEvent(completed ? u"Controller automatic setup verified; Mapping remains Off"_qs
                          : m_readiness.plan().status);
    emit stateChanged();
    return completed;
}

bool AppBackend::undoControllerReadiness()
{
    if (!m_worker.prepareForDriverConfiguration()) {
        appendEvent(u"Could not release vJoy for setup rollback; Mapping remains Off"_qs);
        emit stateChanged();
        return false;
    }
    const bool restored = m_readiness.undoLastAutomaticSetup();
    appendEvent(restored ? u"Automatic controller setup was undone; Mapping remains Off"_qs
                         : m_readiness.plan().status);
    emit stateChanged();
    return restored;
}

void AppBackend::acknowledgeControllerSetup()
{
    if (!m_controllerSetupSuggested) return;
    QSettings settings;
    settings.setValue(u"readiness/controllerSetupIntroSeen"_qs, true);
    m_controllerSetupSuggested = false;
    emit stateChanged();
}

void AppBackend::useConnectedDevice()
{
    const QString id = deviceId();
    if (id.isEmpty()) {
        appendEvent(u"Connect a controller before selecting it"_qs);
        return;
    }
    m_configuration.preferredDeviceId = id;
    persistAndApply();
    appendEvent(u"Connected controller saved as preferred device"_qs);
}

void AppBackend::resetApplicationConfiguration()
{
    m_worker.setMappingEnabled(false);
    m_configuration = defaultConfiguration();
    persistAndApply();
    appendEvent(u"Configuration reset to v1.3 defaults"_qs);
}

PhysicalControllerCapabilities AppBackend::currentPhysicalCapabilities() const
{
    PhysicalControllerCapabilities physical;
    const DeviceSnapshot snapshot = m_worker.deviceSnapshot();
    physical.name = snapshot.name;
    physical.directInputId = snapshot.id;
    physical.hidInstanceId = snapshot.hidInstanceId;
    const AtomicRuntimeState &runtime = m_worker.runtime();
    physical.connected = runtime.physicalConnected.load();
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        physical.axes[static_cast<size_t>(index)] = runtime.axisAvailable[index].load();
    }
    physical.buttons = runtime.buttonCount.load();
    physical.povs = runtime.povCount.load();
    return physical;
}

void AppBackend::refreshUiSnapshot()
{
    // Percentiles are diagnostic telemetry; four updates per second avoids
    // turning their presentation into a source of GUI-side CPU pressure.
    if (m_latencyPercentileClock.elapsed() >= 250) {
        const MappingLatencyPercentiles percentiles = m_worker.latencyPercentiles();
        m_latencyP95Us = percentiles.p95Us;
        m_latencyP99Us = percentiles.p99Us;
        m_latencyPercentileClock.restart();
    }
    const qint64 elapsed = m_rateClock.restart();
    if (elapsed > 0) {
        const quint64 reports = m_worker.runtime().inputReports.load();
        const quint64 writes = m_worker.runtime().vjoyWrites.load();
        const bool receivedPhysicalUpdate = reports != m_previousInputReports;
        m_inputReportsPerSecond = (reports - m_previousInputReports) * 1000.0 / elapsed;
        m_vjoyWritesPerSecond = (writes - m_previousVjoyWrites) * 1000.0 / elapsed;
        m_previousInputReports = reports;
        m_previousVjoyWrites = writes;
        if (reports > 0) {
            if (!m_havePhysicalReport || receivedPhysicalUpdate) {
                m_physicalUpdateClock.restart();
                m_havePhysicalReport = true;
            }
            m_lastPhysicalUpdateAgeMs = m_physicalUpdateClock.elapsed();
        } else {
            m_lastPhysicalUpdateAgeMs = -1;
        }
    }
    const bool selectedAxisChanged = fallBackToAvailableAxis();
    if (selectedAxisChanged) emit selectedAxisCurveChanged();
    emit stateChanged();
}

void AppBackend::appendEvent(const QString &event)
{
    const QString timestamp = QDateTime::currentDateTime().toString(u"HH:mm:ss"_qs);
    m_events.append(timestamp + u"  "_qs + event);
    emit eventLogChanged();
}

void AppBackend::initializeDefaultButtonMappings(int physicalButtonCount, int vjoyButtonCapacity)
{
    if (physicalButtonCount <= 0 || vjoyButtonCapacity <= 0) {
        return;
    }
    if (!ensureDefaultButtonMappings(currentProfile().buttons, physicalButtonCount, vjoyButtonCapacity)) {
        return;
    }
    persistAndApply();
    appendEvent(u"Implicit button routes initialized as 1:1 passthrough"_qs);
}

const ControllerProfile &AppBackend::currentProfile() const
{
    return activeProfile(m_configuration);
}

ControllerProfile &AppBackend::currentProfile()
{
    return activeProfile(m_configuration);
}

AxisMapping *AppBackend::selectedAxisMapping()
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

const AxisMapping *AppBackend::selectedAxisMapping() const
{
    if (!validAxis(m_configuration.selectedAxisIndex)) return nullptr;
    return &currentProfile().axes[m_configuration.selectedAxisIndex];
}

void AppBackend::persistAndApply()
{
    ConfigStore::save(m_configuration);
    m_worker.updateConfiguration(m_configuration);
    rebuildSelectedAxisCurve();
    emit selectedAxisCurveChanged();
    emit stateChanged();
}

void AppBackend::rebuildSelectedAxisCurve()
{
    m_selectedAxisCurve.clear();
    m_curveEditorResponseCurve.clear();
    m_curveGainSamples.clear();
    m_curveComparisonCurve.clear();
    m_curvePreviewCurve.clear();
    m_curveAnalysis.clear();
    if (!validAxis(m_configuration.selectedAxisIndex)) return;
    const int axisIndex = m_configuration.selectedAxisIndex;
    const AxisMapping &axis = currentProfile().axes[axisIndex];
    const bool unipolar = axis.rangeMode == AxisRangeMode::OneSided;
    RuntimeAxisMapping mapping;
    mapping.profile = axis;
    mapping.calibration = m_configuration.calibration[axisIndex];
    mapping.responseCurve = compileResponseCurve(axis.curve, unipolar);
    constexpr int kSamples = 101;
    m_selectedAxisCurve.reserve(kSamples);
    for (int sample = 0; sample < kSamples; ++sample) {
        const float input = -1.0F + 2.0F * static_cast<float>(sample) / static_cast<float>(kSamples - 1);
        QVariantMap point;
        point.insert(u"input"_qs, input);
        // Reuse the mapping engine's static evaluator so the graph cannot
        // drift from deadzone, inversion, calibration, or output limits.
        point.insert(u"output"_qs, evaluateStaticAxisTransfer(input, mapping));
        m_selectedAxisCurve.append(point);
    }

    constexpr int kEditorSamples = 201;
    const float minimum = unipolar ? 0.0F : -1.0F;
    m_curveEditorResponseCurve.reserve(kEditorSamples);
    m_curveGainSamples.reserve(kEditorSamples);
    if (!m_curveComparisonId.isEmpty()) m_curveComparisonCurve.reserve(kEditorSamples);
    if (!m_curvePreviewId.isEmpty()) m_curvePreviewCurve.reserve(kEditorSamples);
    const CurveDefinition comparison = comparisonCurveDefinition();
    for (int sample = 0; sample < kEditorSamples; ++sample) {
        const float input = minimum + (1.0F - minimum) * static_cast<float>(sample)
            / static_cast<float>(kEditorSamples - 1);
        m_curveEditorResponseCurve.append(QVariantMap{{u"input"_qs, input},
            {u"output"_qs, evaluateCurveDefinition(input, axis.curve, unipolar)}});
        m_curveGainSamples.append(QVariantMap{{u"input"_qs, input},
            {u"gain"_qs, evaluateCurveGain(input, axis.curve, unipolar)}});
        if (!m_curveComparisonId.isEmpty()) {
            m_curveComparisonCurve.append(QVariantMap{{u"input"_qs, input},
                {u"output"_qs, evaluateCurveDefinition(input, comparison, unipolar)}});
        }
        if (!m_curvePreviewId.isEmpty()) {
            m_curvePreviewCurve.append(QVariantMap{{u"input"_qs, input},
                {u"output"_qs, evaluateCurveDefinition(input, m_curvePreviewDefinition, unipolar)}});
        }
    }
    const CurveAnalysis analysis = analyzeCurveDefinition(axis.curve, unipolar);
    m_curveAnalysis.insert(u"valid"_qs, analysis.valid);
    m_curveAnalysis.insert(u"monotonic"_qs, analysis.monotonic);
    m_curveAnalysis.insert(u"continuous"_qs, analysis.continuous);
    m_curveAnalysis.insert(u"fullAuthority"_qs, analysis.fullAuthority);
    m_curveAnalysis.insert(u"noOvershoot"_qs, analysis.noOvershoot);
    m_curveAnalysis.insert(u"centerGain"_qs, analysis.centerGain);
    m_curveAnalysis.insert(u"quarterGain"_qs, analysis.quarterGain);
    m_curveAnalysis.insert(u"halfGain"_qs, analysis.halfGain);
    m_curveAnalysis.insert(u"threeQuarterGain"_qs, analysis.threeQuarterGain);
    m_curveAnalysis.insert(u"peakGain"_qs, analysis.peakGain);
    m_curveAnalysis.insert(u"largestGainTransition"_qs, analysis.largestGainTransition);
}

CurveDefinition AppBackend::comparisonCurveDefinition() const
{
    if (m_curveComparisonId.isEmpty()) return linearCurveDefinition();
    if (m_curveComparisonId == u"source"_qs) {
        const AxisMapping *current = selectedAxisMapping();
        if (!current) return linearCurveDefinition();
        const CurveDefinition &curve = current->curve;
        if (curve.sourceFamily == CurveFamily::JCurve || curve.sourceFamily == CurveFamily::SCurve) {
            return standardCurveDefinition(curve.sourceFamily, curve.sourcePresetId);
        }
        if (curve.sourceFamily == CurveFamily::Advanced) {
            return advancedCurveDefinition(curve.sourcePresetId);
        }
        if (curve.sourceFamily == CurveFamily::Personal) {
            const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
                m_configuration.personalCurvePresets.cend(), [&curve](const PersonalCurvePreset &preset) {
                    return preset.id == curve.sourcePresetId;
                });
            if (found != m_configuration.personalCurvePresets.cend()) return found->definition;
        }
        return linearCurveDefinition();
    }
    const QStringList parts = m_curveComparisonId.split(u':');
    if (parts.size() == 3 && parts[0] == u"profile"_qs) {
        const ControllerProfile *profile = findProfile(m_configuration, parts[1]);
        const int axis = parts[2].toInt();
        if (profile && validAxis(axis)) return profile->axes[axis].curve;
    }
    if (parts.size() == 3 && parts[0] == u"standard"_qs) {
        return standardCurveDefinition(parts[1] == u"s"_qs ? CurveFamily::SCurve : CurveFamily::JCurve,
                                       parts[2]);
    }
    if (parts.size() == 2 && parts[0] == u"advanced"_qs) return advancedCurveDefinition(parts[1]);
    if (parts.size() == 2 && parts[0] == u"personal"_qs) {
        const auto found = std::find_if(m_configuration.personalCurvePresets.cbegin(),
            m_configuration.personalCurvePresets.cend(), [&parts](const PersonalCurvePreset &preset) {
                return preset.id == parts[1];
            });
        if (found != m_configuration.personalCurvePresets.cend()) return found->definition;
    }
    return linearCurveDefinition();
}

bool AppBackend::fallBackToAvailableAxis()
{
    // Curve selection is a durable editor context, not a live-device routing
    // decision. Earlier code silently jumped back to the first discovered
    // axis when a selected DirectInput object was absent, making Rotation and
    // slider editing appear broken. All eight logical axes remain selectable;
    // availability is shown by the axes UI without rewriting this selection.
    return false;
}

bool AppBackend::validAxis(int physicalAxis) const
{
    return physicalAxis >= 0 && physicalAxis < kPhysicalAxisCount;
}

bool AppBackend::validPhysicalButton(int physicalButton) const
{
    const int source = physicalButton - 1;
    return source >= 0 && source < kMaximumPhysicalButtons
        && m_worker.runtime().buttonAvailable[source].load();
}

bool AppBackend::axisIsOneSided(int physicalAxis) const
{
    return validAxis(physicalAxis)
        && currentProfile().axes[static_cast<size_t>(physicalAxis)].rangeMode
            == AxisRangeMode::OneSided;
}

} // namespace hotas
