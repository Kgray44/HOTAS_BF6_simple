#include "app_backend.h"

#include "axis_transform.h"
#include "button_mapping.h"
#include "config_store.h"

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
    for (int index = 0; index < kPhysicalAxisCount; ++index) {
        const auto axis = static_cast<PhysicalAxis>(index);
        const AxisMapping &mapping = m_configuration.axes[index];
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
        item.insert(u"calibrationEnabled"_qs, mapping.calibration.enabled);
        item.insert(u"calibrationMinimum"_qs, runtime.calibrationMinimum[index].load());
        item.insert(u"calibrationCenter"_qs, runtime.calibrationCenter[index].load());
        item.insert(u"calibrationMaximum"_qs, runtime.calibrationMaximum[index].load());
        result.append(item);
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
        const ButtonBinding binding = source < static_cast<int>(m_configuration.buttons.size())
            ? m_configuration.buttons[static_cast<size_t>(source)] : ButtonBinding{};
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
bool AppBackend::calibrationActive() const { return m_worker.calibrationRunning(); }
bool AppBackend::startMappingOnLaunch() const { return m_configuration.startMappingOnLaunch; }
int AppBackend::vjoyDeviceId() const { return m_configuration.vjoyDeviceId; }
qulonglong AppBackend::latencyCurrentUs() const { return m_worker.runtime().latencyCurrentUs.load(); }
qulonglong AppBackend::latencyAverageUs() const { return m_worker.runtime().latencyAverageUs.load(); }
qulonglong AppBackend::latencyPeakUs() const { return m_worker.runtime().latencyPeakUs.load(); }

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
    const VirtualAxis virtualAxis = virtualAxisFromString(target);
    if (hasMappingConflict(m_configuration, physicalAxis, virtualAxis)) {
        if (!explicitOverride) return false;
        for (int index = 0; index < kPhysicalAxisCount; ++index) {
            if (index != physicalAxis && m_configuration.axes[index].target == virtualAxis) {
                m_configuration.axes[index].target = VirtualAxis::Disabled;
            }
        }
    }
    m_configuration.axes[physicalAxis].target = virtualAxis;
    persistAndApply();
    return true;
}

void AppBackend::setAxisInverted(int physicalAxis, bool inverted)
{
    if (!validAxis(physicalAxis)) return;
    m_configuration.axes[physicalAxis].inverted = inverted;
    persistAndApply();
}

void AppBackend::setAxisDeadzone(int physicalAxis, double deadzone)
{
    if (!validAxis(physicalAxis)) return;
    m_configuration.axes[physicalAxis].deadzone = std::clamp(static_cast<float>(deadzone), 0.0F, 0.95F);
    persistAndApply();
}

bool AppBackend::setButtonMapping(int physicalButton, int virtualButton, bool explicitOverride)
{
    if (!validPhysicalButton(physicalButton) || virtualButton < 0
        || virtualButton > vjoyButtonCount()) {
        return false;
    }
    const int source = physicalButton - 1;
    if (m_configuration.buttons.size() <= static_cast<size_t>(source)) {
        m_configuration.buttons.resize(static_cast<size_t>(source + 1));
    }
    if (hasButtonMappingConflict(m_configuration.buttons, source, virtualButton, vjoyButtonCount())) {
        if (!explicitOverride) return false;
        for (int index = 0; index < static_cast<int>(m_configuration.buttons.size()); ++index) {
            ButtonBinding &binding = m_configuration.buttons[static_cast<size_t>(index)];
            if (index != source && binding.type == ButtonActionType::VirtualButton
                && binding.target == virtualButton) {
                binding = {};
            }
        }
    }
    m_configuration.buttons[static_cast<size_t>(source)] = virtualButton > 0
        ? ButtonBinding{ButtonActionType::VirtualButton, virtualButton} : ButtonBinding{};
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
    m_configuration.buttons = defaultButtonMappings(physicalCount, virtualCount);
    persistAndApply();
    appendEvent(u"Button mappings reset to passthrough defaults"_qs);
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
            m_configuration.axes[index].calibration = calibration;
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
    for (auto &mapping : m_configuration.axes) mapping.calibration = Calibration{};
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
    appendEvent(u"Configuration reset to v1.1 defaults"_qs);
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
    if (!m_configuration.buttons.empty() || physicalButtonCount <= 0 || vjoyButtonCapacity <= 0) {
        return;
    }
    m_configuration.buttons = defaultButtonMappings(physicalButtonCount, vjoyButtonCapacity);
    persistAndApply();
    appendEvent(u"Default button passthrough initialized from detected device capacity"_qs);
}

void AppBackend::persistAndApply()
{
    ConfigStore::save(m_configuration);
    m_worker.updateConfiguration(m_configuration);
    emit stateChanged();
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
