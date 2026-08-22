#pragma once

#include "mapping_types.h"

#include <QMutex>
#include <QThread>

#include <array>
#include <atomic>

namespace hotas {

struct AtomicRuntimeState {
    std::array<std::atomic<float>, kPhysicalAxisCount> raw{};
    std::array<std::atomic<float>, kPhysicalAxisCount> normalized{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterDeadzone{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterHysteresis{};
    std::array<std::atomic<float>, kPhysicalAxisCount> afterInversion{};
    std::array<std::atomic<float>, kPhysicalAxisCount> curveResponse{};
    std::array<std::atomic<float>, kPhysicalAxisCount> transformed{};
    std::array<std::atomic<float>, kPhysicalAxisCount> virtualValues{};
    std::array<std::atomic<bool>, kPhysicalAxisCount> axisAvailable{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMinimum{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationCenter{};
    std::array<std::atomic<float>, kPhysicalAxisCount> calibrationMaximum{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> physicalButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> virtualButtonPressed{};
    std::array<std::atomic<bool>, kMaximumPhysicalButtons> buttonAvailable{};
    std::atomic_bool physicalConnected{false};
    std::atomic_int axisCount{0};
    std::atomic_int buttonCount{0};
    std::atomic_int povCount{0};
    // DirectInput reports POV in hundredths of a degree; -1 is centered.
    std::atomic_int povValue{-1};
    std::atomic_int vjoyButtonCount{0};
    std::atomic_int lastPhysicalButton{0};
    std::atomic_int lastPhysicalButtonTarget{0};
    std::atomic_bool mappingActive{false};
    std::atomic_bool vjoyReady{false};
    std::atomic_bool hidhideAvailable{false};
    std::atomic_bool hidhideCloakStateKnown{false};
    std::atomic_bool hidhideCloaked{false};
    std::atomic_uint64_t inputReports{0};
    std::atomic_uint64_t vjoyWrites{0};
    std::atomic_uint64_t latencyCurrentUs{0};
    std::atomic_uint64_t latencyAverageUs{0};
    std::atomic_uint64_t latencyPeakUs{0};
    std::atomic_uint64_t profileSwitchCount{0};
    std::atomic_uint64_t lastProfileSwapUs{0};
    std::atomic_uint64_t lastCurveCompileUs{0};
};

struct DeviceSnapshot {
    QString name = u"No controller detected"_qs;
    QString id;
};

class MappingWorker final : public QThread {
    Q_OBJECT

public:
    explicit MappingWorker(MapperConfiguration configuration, QObject *parent = nullptr);
    ~MappingWorker() override;

    void updateConfiguration(const MapperConfiguration &configuration);
    void setMappingEnabled(bool enabled);
    bool mappingRequested() const;
    void requestStop();
    void beginCalibration();
    void cancelCalibration();
    bool calibrationRunning() const;
    std::array<Calibration, kPhysicalAxisCount> capturedCalibration() const;
    void refreshHidHideState();
    const AtomicRuntimeState &runtime() const { return m_runtime; }
    DeviceSnapshot deviceSnapshot() const;
    QString vjoyStatus() const;

signals:
    void workerEvent(const QString &message);
    void hardwareStateChanged();
    void buttonConfigurationSuggested(int physicalButtonCount, int vjoyButtonCapacity);

protected:
    void run() override;

private:
    MapperConfiguration configurationCopy();
    void setDeviceSnapshot(const DeviceSnapshot &snapshot);
    void setVjoyStatus(const QString &status);

    AtomicRuntimeState m_runtime;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_mappingRequested{false};
    std::atomic_bool m_calibrating{false};
    std::atomic_uint64_t m_configurationVersion{0};
    mutable QMutex m_configurationMutex;
    MapperConfiguration m_configuration;
    mutable QMutex m_deviceMutex;
    DeviceSnapshot m_device;
    mutable QMutex m_statusMutex;
    QString m_vjoyStatus = u"Not checked"_qs;
};

} // namespace hotas
