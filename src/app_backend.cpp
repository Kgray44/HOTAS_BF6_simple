#include "app_backend.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "config_store.h"
#include "profile_model.h"
#include "response_curve.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace hotas {
using namespace Qt::StringLiterals;

AppBackend::AppBackend(QObject *parent)
    : QObject(parent), m_configuration(ConfigStore::load()), m_worker(m_configuration)
{
    connect(&m_snapshotTimer, &QTimer::timeout, this, &AppBackend::refreshUiSnapshot);
    connect(&m_worker, &MappingWorker::workerEvent, this, &AppBackend::appendEvent, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::hardwareStateChanged, this, [this] { emit stateChanged(); }, Qt::QueuedConnection);
    connect(&m_worker, &MappingWorker::buttonConfigurationSuggested, this,
            &AppBackend::initializeDefaultButtonMappings, Qt::QueuedConnection);
    m_snapshotTimer.setInterval(16); // UI-only latest-state projection, never the mapping cadence.
    m_snapshotTimer.start();
    m_rateClock.start();
    m_physicalUpdateClock.start();
    rebuildSelectedAxisCurve();
    appendEvent(u"HOTAS Mapper ready"_qs);
    m_worker.start();
    if (m_configuration.startMappingOnLaunch) {
        m_worker.setMappingEnabled(true);
    }
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
        item.insert(u"label"_qs, physicalAxisLabel(axis));
        item.insert(u"detail"_qs, physicalAxisDetail(axis));
        item.insert(u"available"_qs, runtime.axisAvailable[index].load());
        item.insert(u"raw"_qs, runtime.raw[index].load());
        item.insert(u"curveResponse"_qs, runtime.curveResponse[index].load());
        item.insert(u"transformed"_qs, runtime.transformed[index].load());
        const float virtualValue = runtime.virtualValues[index].load();
        item.insert(u"virtualValue"_qs, virtualValue);
        item.insert(u"virtualValid"_qs, std::isfinite(virtualValue));
        item.insert(u"target"_qs, virtualAxisLabel(mapping.target));
        item.insert(u"inverted"_qs, mapping.inverted);
        item.insert(u"deadzone"_qs, mapping.deadzone);
        item.insert(u"hysteresis"_qs, mapping.hysteresis);
        item.insert(u"outputMinimum"_qs, mapping.outputMinimum);
        item.insert(u"outputMaximum"_qs, mapping.outputMaximum);
        item.insert(u"curveSummary"_qs, curveDefinitionSummary(mapping.curve));
        item.insert(u"curvePointEditing"_qs, mapping.curve.pointEditing);
        item.insert(u"unipolar"_qs, isUnipolarAxis(axis));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
    const AxisMapping *current = selectedAxisMapping();
    if (current && current->curve.family == CurveFamily::Custom
        && current->curve.sourceFamily != CurveFamily::Linear) {
        result.append(QVariantMap{{u"id"_qs, u"source"_qs},
            {u"label"_qs, u"Source · "_qs + current->curve.baseLabel}});
    }
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if (isUnipolarAxis(static_cast<PhysicalAxis>(axis)) != unipolar) continue;
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
        if (preset.unipolar != isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex))) continue;
        result.append(QVariantMap{{u"id"_qs, u"personal:"_qs + preset.id},
            {u"label"_qs, u"Personal · "_qs + preset.name}});
    }
    return result;
}

QVariantList AppBackend::curveCopyChoices() const
{
    QVariantList result;
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
    for (const ControllerProfile &profile : m_configuration.profiles) {
        for (int axis = 0; axis < kPhysicalAxisCount; ++axis) {
            if (profile.id == m_configuration.activeProfileId && axis == m_configuration.selectedAxisIndex) continue;
            if (isUnipolarAxis(static_cast<PhysicalAxis>(axis)) != unipolar) continue;
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
        QVariantMap item;
        item.insert(u"index"_qs, source + 1);
        item.insert(u"label"_qs, QString(u"Button %1"_qs).arg(source + 1));
        item.insert(u"pressed"_qs, runtime.physicalButtonPressed[source].load());
        item.insert(u"target"_qs, target);
        item.insert(u"targetLabel"_qs, target > 0
            ? QString(u"vJoy Button %1"_qs).arg(target) : u"Disabled"_qs);
        item.insert(u"virtualPressed"_qs, target > 0
            && runtime.virtualButtonPressed[target - 1].load());
        result.append(item);
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
        item.insert(u"protected"_qs, profile.id == normalProfileId());
        item.insert(u"mappedAxes"_qs, mappedAxes);
        item.insert(u"mappedButtons"_qs, mappedButtons);
        result.append(item);
    }
    return result;
}

QString AppBackend::activeProfileId() const { return m_configuration.activeProfileId; }
QString AppBackend::activeProfileName() const { return currentProfile().name; }
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
int AppBackend::povValue() const { return m_worker.runtime().povValue.load(); }
int AppBackend::vjoyButtonCount() const { return m_worker.runtime().vjoyButtonCount.load(); }
int AppBackend::vjoyRequiredButtonCount() const { return buttonCount(); }
bool AppBackend::vjoyCapacitySufficient() const
{
    return assessButtonCapacity(buttonCount(), vjoyButtonCount()).sufficient;
}
int AppBackend::lastPhysicalButton() const { return m_worker.runtime().lastPhysicalButton.load(); }
int AppBackend::lastPhysicalButtonTarget() const { return m_worker.runtime().lastPhysicalButtonTarget.load(); }
bool AppBackend::mappingActive() const { return m_worker.runtime().mappingActive.load(); }
bool AppBackend::vjoyReady() const { return m_worker.runtime().vjoyReady.load(); }
QString AppBackend::vjoyStatus() const { return m_worker.vjoyStatus(); }
bool AppBackend::hidhideAvailable() const { return m_worker.runtime().hidhideAvailable.load(); }
bool AppBackend::hidhideCloakStateKnown() const
{
    return m_worker.runtime().hidhideCloakStateKnown.load();
}
bool AppBackend::hidhideCloaked() const { return m_worker.runtime().hidhideCloaked.load(); }
bool AppBackend::calibrationActive() const { return m_worker.calibrationRunning(); }
bool AppBackend::startMappingOnLaunch() const { return m_configuration.startMappingOnLaunch; }
int AppBackend::vjoyDeviceId() const { return m_configuration.vjoyDeviceId; }
qulonglong AppBackend::latencyCurrentUs() const { return m_worker.runtime().latencyCurrentUs.load(); }
qulonglong AppBackend::latencyAverageUs() const { return m_worker.runtime().latencyAverageUs.load(); }
qulonglong AppBackend::latencyPeakUs() const { return m_worker.runtime().latencyPeakUs.load(); }
qulonglong AppBackend::profileSwitchCount() const { return m_worker.runtime().profileSwitchCount.load(); }
qulonglong AppBackend::lastProfileSwapUs() const { return m_worker.runtime().lastProfileSwapUs.load(); }
qulonglong AppBackend::lastCurveCompileUs() const { return m_worker.runtime().lastCurveCompileUs.load(); }

QStringList AppBackend::buttonOutputChoices() const
{
    QStringList choices{u"Disabled"_qs};
    for (int button = 1; button <= vjoyButtonCount(); ++button) {
        choices.append(QString(u"vJoy Button %1"_qs).arg(button));
    }
    return choices;
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
            mapping->curve, isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex)));
    } else if (normalized == u"personal"_qs) {
        const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
    mapping->curve = resampleCurveDefinition(mapping->curve, unipolar, density);
    persistAndApply();
}

void AppBackend::setCurveSymmetry(bool enabled)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping || !mapping->curve.pointEditing
        || isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex))) return;
    mapping->curve.symmetry = enabled;
    normalizeCurveDefinition(mapping->curve, false);
    persistAndApply();
}

bool AppBackend::setCurvePoint(int index, double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = updateCurvePoint(mapping->curve,
        isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex)), index,
        static_cast<float>(input), static_cast<float>(output));
    if (changed) persistAndApply();
    return changed;
}

bool AppBackend::setCurvePointLocked(int index, bool locked)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::setCurvePointLocked(mapping->curve,
        isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex)), index, locked);
    if (changed) persistAndApply();
    return changed;
}

int AppBackend::addCurvePoint(double input, double output)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return -1;
    int selected = -1;
    if (!hotas::addCurvePoint(mapping->curve,
            isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex)),
            static_cast<float>(input), static_cast<float>(output), &selected)) return -1;
    persistAndApply();
    return selected;
}

bool AppBackend::removeCurvePoint(int index)
{
    AxisMapping *mapping = selectedAxisMapping();
    if (!mapping) return false;
    const bool changed = hotas::removeCurvePoint(mapping->curve,
        isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex)), index);
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
    const bool targetUnipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
    const bool sourceUnipolar = isUnipolarAxis(static_cast<PhysicalAxis>(axisIndex));
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
    preset.unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(m_configuration.selectedAxisIndex));
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
    if (hasButtonMappingConflict(bindings, source, virtualButton, vjoyButtonCount())) {
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
    }
    bindings[static_cast<size_t>(source)] = virtualButton > 0
        ? ButtonBinding{ButtonActionType::VirtualButton, virtualButton} : ButtonBinding{};
    bindings[static_cast<size_t>(source)].explicitlyConfigured = true;
    persistAndApply();
    appendEvent(QString(u"Button %1 → %2"_qs).arg(physicalButton).arg(
        virtualButton > 0 ? QString(u"vJoy %1"_qs).arg(virtualButton) : u"Disabled"_qs));
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

bool AppBackend::openVjoyConfiguration()
{
    // Request a safe worker-side release before launching the supported utility.
    m_worker.setMappingEnabled(false);
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
    m_worker.refreshHidHideState();
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

void AppBackend::refreshUiSnapshot()
{
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
    m_events.prepend(timestamp + u"  "_qs + event);
    while (m_events.size() > 8) m_events.removeLast();
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
    const bool unipolar = isUnipolarAxis(static_cast<PhysicalAxis>(axisIndex));
    const AxisMapping &axis = currentProfile().axes[axisIndex];
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

} // namespace hotas
