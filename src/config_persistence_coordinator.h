#pragma once

#include "config_store.h"

#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace hotas {

// The configuration file remains owned by ConfigStore.  This coordinator owns
// only when a complete configuration snapshot reaches that synchronous writer.
// It is deliberately a control-plane service and is never reachable from the
// DirectInput -> MappingWorker -> vJoy report path.
class ConfigPersistenceCoordinator final {
public:
    using WriteResult = ConfigStore::SaveResult;
    using Writer = std::function<WriteResult(const MapperConfiguration &)>;
    using UntimedWriter = std::function<bool(const MapperConfiguration &)>;

    struct RequestReceipt {
        quint64 generation = 0;
        qint64 captureStartedNs = 0;
        qint64 captureFinishedNs = 0;
        qint64 enqueuedNs = 0;
        bool supersededPending = false;
    };

    struct Completion {
        quint64 generation = 0;
        qint64 enqueuedNs = 0;
        qint64 workerStartedNs = 0;
        qint64 workerFinishedNs = 0;
        WriteResult write;
    };

    struct Statistics {
        quint64 requests = 0;
        quint64 writes = 0;
        quint64 superseded = 0;
        quint64 latestRequestedGeneration = 0;
        quint64 durableGeneration = 0;
        quint64 failures = 0;
        quint64 lastFailedGeneration = 0;
        bool hasPending = false;
        bool hasInFlight = false;
    };

    enum class FlushStatus {
        Durable,
        Failed,
        TimedOut,
        Stopped,
    };

    struct FlushResult {
        FlushStatus status = FlushStatus::Durable;
        quint64 generation = 0;
        quint64 durableGeneration = 0;

        bool durable() const { return status == FlushStatus::Durable; }
    };

    explicit ConfigPersistenceCoordinator(Writer writer, UntimedWriter untimedWriter = {});
    ~ConfigPersistenceCoordinator();

    ConfigPersistenceCoordinator(const ConfigPersistenceCoordinator &) = delete;
    ConfigPersistenceCoordinator &operator=(const ConfigPersistenceCoordinator &) = delete;

    // The caller supplies an immutable-by-value snapshot boundary. The newest
    // queued request replaces an older pending request; an already-running
    // write is allowed to finish on the dedicated serial thread.
    RequestReceipt request(const MapperConfiguration &configuration);

    // A barrier is reserved for real transactional, relaunch, and shutdown
    // boundaries. Ordinary GUI handlers must use request() only.
    FlushResult flushThrough(quint64 generation, int timeoutMs);
    FlushResult requestAndFlush(const MapperConfiguration &configuration, int timeoutMs);
    // Exact setup facts are stronger than ordinary current-state persistence:
    // the supplied snapshot is never replaced by a later coalesced request.
    // Pending ordinary work captured before this call is older than the
    // transaction snapshot and is discarded. New ordinary work remains
    // coalesced, but runs only after the exact snapshot has been written.
    // This remains a serial control-plane operation and is unreachable from
    // the DirectInput -> MappingWorker -> vJoy report path.
    FlushResult requestAndFlushExact(const MapperConfiguration &configuration, int timeoutMs);
    // Releases the short post-write fence held for an exact transaction. The
    // owner must complete the immediate read-back before ordinary snapshots
    // are allowed to reach storage; it is safe to call after a timeout.
    void completeExact(quint64 generation);
    FlushResult flushLatest(int timeoutMs);

    Statistics statistics() const;

    // Probe completions are retained only when telemetry is enabled. Draining
    // happens on the GUI thread so the probe itself remains GUI-owned.
    void setTelemetryEnabled(bool enabled);
    std::vector<Completion> takeCompletions();

    // Signals an orderly stop and waits only for the supplied bound. On a
    // timeout the self-owned worker is allowed to finish later rather than
    // forcing a QThread destruction or an unbounded GUI shutdown wait.
    bool stop(int timeoutMs);

private:
    struct Pending;
    struct SharedState;

    static qint64 monotonicNowNs();
    static void runWorker(const std::shared_ptr<SharedState> &state);

    std::shared_ptr<SharedState> m_state;
    QPointer<QThread> m_thread;
};

} // namespace hotas
