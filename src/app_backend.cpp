#include "app_backend.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "config_store.h"
#include "profile_model.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
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
    if (!validAxis(m_configuration.selectedAxisIndex)) return;
    const int axisIndex = m_configuration.selectedAxisIndex;
    const RuntimeAxisMapping mapping{
        currentProfile().axes[axisIndex], m_configuration.calibration[axisIndex]};
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
}

bool AppBackend::fallBackToAvailableAxis()
{
    const AtomicRuntimeState &runtime = m_worker.runtime();
    if (!runtime.physicalConnected.load()
        || runtime.axisAvailable[m_configuration.selectedAxisIndex].load()) {
        return false;
    }
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        if (!runtime.axisAvailable[index].load()) continue;
        m_configuration.selectedAxisIndex = index;
        ConfigStore::save(m_configuration);
        rebuildSelectedAxisCurve();
        return true;
    }
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
