#pragma once

#include "mapping_types.h"

#include <QMutex>
#include <QThread>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace hotas {

constexpr size_t kLatencyTelemetrySamples = 2048;

struct MappingLatencyPercentiles {
    std::uint64_t sampleCount = 0;
    std::uint64_t p95Us = 0;
    std::uint64_t p99Us = 0;
};

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
    // DirectInput reports up to four POVs in hundredths of a degree; -1 is
    // centered. The UI reads this fixed snapshot without entering the worker.
    std::array<std::atomic_int, kMaximumPhysicalPovs> povValues{};
    std::atomic_int vjoyButtonCount{0};
    std::atomic_int lastPhysicalButton{0};
    std::atomic_int lastPhysicalButtonTarget{0};
    std::atomic_bool mappingActive{false};
    std::atomic_bool vjoyReady{false};
    std::atomic_bool hidhideAvailable{false};
    std::atomic_bool hidhideCloakStateKnown{false};
    std::atomic_bool hidhideCloaked{false};
    std::atomic_bool hidhideMapperAllowed{false};
    std::atomic_uint64_t inputReports{0};
    std::atomic_uint64_t vjoyWrites{0};
    std::atomic_uint64_t latencyCurrentUs{0};
    std::atomic_uint64_t latencyAverageUs{0};
    std::atomic_uint64_t latencyPeakUs{0};
    // The worker writes one bounded sample per report. Percentile sorting is
    // deliberately performed by the UI-side snapshot timer, never here.
    std::array<std::atomic_uint64_t, kLatencyTelemetrySamples> latencySamples{};
    std::atomic_uint64_t latencySampleCount{0};
    std::atomic_uint64_t profileSwitchCount{0};
    std::atomic_uint64_t lastProfileSwapUs{0};
    // Index/source are small worker-owned values; names stay on the UI side.
    std::atomic_int effectiveProfileIndex{0};
    std::atomic_int profileOverrideButton{0};
    std::atomic_int profileOverrideMode{static_cast<int>(ProfileTriggerMode::Disabled)};
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
    MappingLatencyPercentiles latencyPercentiles() const;

signals:
    void workerEvent(const QString &message);
    void hardwareStateChanged();
    void buttonConfigurationSuggested(int physicalButtonCount, int vjoyButtonCapacity);

protected:
    void run() override;

private:
    MapperConfiguration configurationCopy();
    std::pair<MapperConfiguration, std::shared_ptr<const RuntimeProfileCache>>
    preparedConfigurationCopy();
    void setDeviceSnapshot(const DeviceSnapshot &snapshot);
    void setVjoyStatus(const QString &status);

    AtomicRuntimeState m_runtime;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_mappingRequested{false};
    std::atomic_bool m_calibrating{false};
    std::atomic_uint64_t m_configurationVersion{0};
    mutable QMutex m_configurationMutex;
    MapperConfiguration m_configuration;
    // Built by the caller before it acquires the configuration mutex. The
    // worker only swaps this fully prepared immutable table between reports.
    std::shared_ptr<const RuntimeProfileCache> m_preparedProfileCache;
    mutable QMutex m_deviceMutex;
    DeviceSnapshot m_device;
    mutable QMutex m_statusMutex;
    QString m_vjoyStatus = u"Not checked"_qs;
};

} // namespace hotas
