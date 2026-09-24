#include "config_persistence_coordinator.h"

#include "interactive_scheduling_policy.h"

#include <QElapsedTimer>

#include <algorithm>
#include <chrono>
#include <deque>
#include <unordered_set>
#include <utility>

namespace hotas {

struct ConfigPersistenceCoordinator::Pending {
    MapperConfiguration configuration;
    quint64 generation = 0;
    qint64 enqueuedNs = 0;
};

struct ConfigPersistenceCoordinator::SharedState {
    explicit SharedState(Writer configuredWriter, UntimedWriter configuredUntimedWriter)
        : writer(std::move(configuredWriter))
        , untimedWriter(std::move(configuredUntimedWriter))
    {
    }

    mutable QMutex mutex;
    QWaitCondition changed;
    Writer writer;
    UntimedWriter untimedWriter;
    std::optional<Pending> pending;
    // Exact transactions form a small FIFO that ordinary coalescing can
    // never replace. They are deliberately separate from `pending` so a
    // verification proof cannot be reported durable after only a later,
    // unrelated snapshot reached disk.
    std::deque<Pending> exactPending;
    std::deque<std::pair<quint64, bool>> exactCompletions;
    // Once an exact write finishes, the worker must not let an ordinary
    // snapshot overwrite it until its caller has performed the required
    // durable read-back. A timed-out caller may release before the write
    // finishes, so remember those early releases as well.
    quint64 exactReadbackFenceGeneration = 0;
    std::unordered_set<quint64> releasedExactGenerations;
    std::vector<Completion> completions;
    quint64 requests = 0;
    quint64 writes = 0;
    quint64 superseded = 0;
    quint64 latestRequestedGeneration = 0;
    quint64 durableGeneration = 0;
    quint64 failures = 0;
    quint64 lastFailedGeneration = 0;
    quint64 inFlightGeneration = 0;
    bool telemetryEnabled = false;
    bool stopping = false;
};

qint64 ConfigPersistenceCoordinator::monotonicNowNs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

ConfigPersistenceCoordinator::ConfigPersistenceCoordinator(Writer writer, UntimedWriter untimedWriter)
    : m_state(std::make_shared<SharedState>(std::move(writer), std::move(untimedWriter)))
{
    // QThread::create gives the worker a self-contained lifetime. That lets a
    // bounded shutdown report a timeout truthfully without terminating a live
    // QThread or turning the application exit into an unbounded join.
    m_thread = QThread::create([state = m_state] { runWorker(state); });
    QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start(QThread::LowPriority);
}

ConfigPersistenceCoordinator::~ConfigPersistenceCoordinator()
{
    stop(2000);
}

void ConfigPersistenceCoordinator::runWorker(const std::shared_ptr<SharedState> &state)
{
    // One qualification-time observation at worker entry; never a write-path
    // probe and never reached from MappingWorker.
    InteractiveSchedulingPolicy::recordCurrentThread("persistence");
    for (;;) {
        Pending current;
        bool exact = false;
        bool telemetryEnabled = false;
        {
            QMutexLocker locker(&state->mutex);
            while (!state->stopping && (state->exactReadbackFenceGeneration != 0
                                         || (state->exactPending.empty() && !state->pending)))
                state->changed.wait(&state->mutex);
            if (state->stopping && state->exactPending.empty() && !state->pending) return;

            if (!state->exactPending.empty()) {
                current = std::move(state->exactPending.front());
                state->exactPending.pop_front();
                exact = true;
            } else {
                current = std::move(*state->pending);
                state->pending.reset();
            }
            state->inFlightGeneration = current.generation;
            telemetryEnabled = state->telemetryEnabled;
        }

        const qint64 workerStartedNs = telemetryEnabled ? monotonicNowNs() : 0;
        WriteResult result;
        if (telemetryEnabled || !state->untimedWriter) {
            result = state->writer(current.configuration);
        } else {
            result.success = state->untimedWriter(current.configuration);
        }
        const qint64 workerFinishedNs = telemetryEnabled ? monotonicNowNs() : 0;

        {
            QMutexLocker locker(&state->mutex);
            ++state->writes;
            state->inFlightGeneration = 0;
            if (result.success) {
                // There is one writer. Still, advance only forward so an older
                // completion can never present itself as newer durability.
                state->durableGeneration = std::max(state->durableGeneration, current.generation);
            } else {
                ++state->failures;
                state->lastFailedGeneration = std::max(state->lastFailedGeneration, current.generation);
            }
            if (exact) {
                state->exactCompletions.emplace_back(current.generation, result.success);
                // Exact setup transactions are rare and synchronous. Keep a
                // bounded completion history solely so a delayed waiter can
                // distinguish its own failed write from a newer completion.
                while (state->exactCompletions.size() > 64) state->exactCompletions.pop_front();
                if (state->releasedExactGenerations.erase(current.generation) == 0) {
                    state->exactReadbackFenceGeneration = current.generation;
                }
            }
            if (telemetryEnabled) {
                state->completions.push_back({current.generation, current.enqueuedNs, workerStartedNs,
                                              workerFinishedNs, result});
            }
            state->changed.wakeAll();
        }
    }
}

ConfigPersistenceCoordinator::RequestReceipt
ConfigPersistenceCoordinator::request(const MapperConfiguration &configuration)
{
    const bool telemetryEnabled = [&] {
        QMutexLocker locker(&m_state->mutex);
        return m_state->telemetryEnabled;
    }();
    RequestReceipt receipt;
    receipt.captureStartedNs = telemetryEnabled ? monotonicNowNs() : 0;
    MapperConfiguration snapshot = configuration;
    receipt.captureFinishedNs = telemetryEnabled ? monotonicNowNs() : 0;

    {
        QMutexLocker locker(&m_state->mutex);
        if (m_state->stopping) return receipt;
        receipt.generation = ++m_state->latestRequestedGeneration;
        ++m_state->requests;
        receipt.supersededPending = m_state->pending.has_value();
        if (receipt.supersededPending) ++m_state->superseded;
        receipt.enqueuedNs = telemetryEnabled ? monotonicNowNs() : 0;
        m_state->pending = Pending{std::move(snapshot), receipt.generation, receipt.enqueuedNs};
        m_state->changed.wakeOne();
    }
    return receipt;
}

ConfigPersistenceCoordinator::FlushResult
ConfigPersistenceCoordinator::flushThrough(quint64 generation, int timeoutMs)
{
    FlushResult result;
    result.generation = generation;
    QElapsedTimer elapsed;
    elapsed.start();
    QMutexLocker locker(&m_state->mutex);
    while (m_state->durableGeneration < generation) {
        if (m_state->stopping) {
            result.status = FlushStatus::Stopped;
            break;
        }
        const bool nothingOutstanding = !m_state->pending
            && m_state->exactPending.empty()
            && m_state->exactReadbackFenceGeneration == 0
            && m_state->inFlightGeneration == 0;
        if (nothingOutstanding && m_state->lastFailedGeneration >= generation) {
            result.status = FlushStatus::Failed;
            break;
        }
        const int remainingMs = timeoutMs - static_cast<int>(elapsed.elapsed());
        if (remainingMs <= 0) {
            result.status = FlushStatus::TimedOut;
            break;
        }
        m_state->changed.wait(&m_state->mutex, static_cast<unsigned long>(remainingMs));
    }
    if (m_state->durableGeneration >= generation) result.status = FlushStatus::Durable;
    result.durableGeneration = m_state->durableGeneration;
    return result;
}

ConfigPersistenceCoordinator::FlushResult
ConfigPersistenceCoordinator::requestAndFlush(const MapperConfiguration &configuration, int timeoutMs)
{
    const RequestReceipt receipt = request(configuration);
    if (receipt.generation == 0) {
        return {FlushStatus::Stopped, 0, statistics().durableGeneration};
    }
    return flushThrough(receipt.generation, timeoutMs);
}

ConfigPersistenceCoordinator::FlushResult
ConfigPersistenceCoordinator::requestAndFlushExact(const MapperConfiguration &configuration, int timeoutMs)
{
    RequestReceipt receipt;
    const bool telemetryEnabled = [&] {
        QMutexLocker locker(&m_state->mutex);
        return m_state->telemetryEnabled;
    }();
    receipt.captureStartedNs = telemetryEnabled ? monotonicNowNs() : 0;
    MapperConfiguration snapshot = configuration;
    receipt.captureFinishedNs = telemetryEnabled ? monotonicNowNs() : 0;

    {
        QMutexLocker locker(&m_state->mutex);
        if (m_state->stopping) return {FlushStatus::Stopped, 0, m_state->durableGeneration};
        receipt.generation = ++m_state->latestRequestedGeneration;
        ++m_state->requests;
        // An ordinary pending snapshot is necessarily older than this exact
        // GUI-thread transaction. Letting it write after verification could
        // overwrite the newly proven record, so discard it before queuing the
        // non-coalescible transaction.
        if (m_state->pending) {
            ++m_state->superseded;
            m_state->pending.reset();
        }
        receipt.enqueuedNs = telemetryEnabled ? monotonicNowNs() : 0;
        m_state->exactPending.push_back(Pending{std::move(snapshot), receipt.generation,
                                                 receipt.enqueuedNs});
        m_state->changed.wakeOne();
    }

    QElapsedTimer elapsed;
    elapsed.start();
    QMutexLocker locker(&m_state->mutex);
    for (;;) {
        const auto completed = std::find_if(m_state->exactCompletions.cbegin(),
                                            m_state->exactCompletions.cend(),
                                            [&receipt](const auto &entry) {
                                                return entry.first == receipt.generation;
                                            });
        if (completed != m_state->exactCompletions.cend()) {
            return {completed->second ? FlushStatus::Durable : FlushStatus::Failed,
                    receipt.generation, m_state->durableGeneration};
        }
        if (m_state->stopping) return {FlushStatus::Stopped, receipt.generation,
                                       m_state->durableGeneration};
        const int remainingMs = timeoutMs - static_cast<int>(elapsed.elapsed());
        if (remainingMs <= 0) return {FlushStatus::TimedOut, receipt.generation,
                                      m_state->durableGeneration};
        m_state->changed.wait(&m_state->mutex, static_cast<unsigned long>(remainingMs));
    }
}

void ConfigPersistenceCoordinator::completeExact(quint64 generation)
{
    if (generation == 0) return;
    QMutexLocker locker(&m_state->mutex);
    if (m_state->exactReadbackFenceGeneration == generation) {
        m_state->exactReadbackFenceGeneration = 0;
    } else if (std::none_of(m_state->exactCompletions.cbegin(),
                             m_state->exactCompletions.cend(),
                             [generation](const auto &completion) {
                                 return completion.first == generation;
                             })) {
        // The writer can still be in flight when a bounded caller gives up.
        // Let runWorker observe this release when it records completion.
        m_state->releasedExactGenerations.insert(generation);
    }
    m_state->changed.wakeAll();
}

ConfigPersistenceCoordinator::FlushResult ConfigPersistenceCoordinator::flushLatest(int timeoutMs)
{
    quint64 generation = 0;
    {
        QMutexLocker locker(&m_state->mutex);
        generation = m_state->latestRequestedGeneration;
    }
    return flushThrough(generation, timeoutMs);
}

ConfigPersistenceCoordinator::Statistics ConfigPersistenceCoordinator::statistics() const
{
    QMutexLocker locker(&m_state->mutex);
    return {m_state->requests, m_state->writes, m_state->superseded,
            m_state->latestRequestedGeneration, m_state->durableGeneration,
             m_state->failures, m_state->lastFailedGeneration,
             m_state->pending.has_value() || !m_state->exactPending.empty()
                 || m_state->exactReadbackFenceGeneration != 0,
             m_state->inFlightGeneration != 0};
}

void ConfigPersistenceCoordinator::setTelemetryEnabled(bool enabled)
{
    QMutexLocker locker(&m_state->mutex);
    m_state->telemetryEnabled = enabled;
    if (!enabled) m_state->completions.clear();
}

std::vector<ConfigPersistenceCoordinator::Completion> ConfigPersistenceCoordinator::takeCompletions()
{
    QMutexLocker locker(&m_state->mutex);
    std::vector<Completion> completions;
    completions.swap(m_state->completions);
    return completions;
}

bool ConfigPersistenceCoordinator::stop(int timeoutMs)
{
    if (!m_thread) return true;
    {
        QMutexLocker locker(&m_state->mutex);
        m_state->stopping = true;
        m_state->changed.wakeAll();
    }
    QThread *thread = m_thread;
    if (!thread->wait(static_cast<unsigned long>(std::max(0, timeoutMs)))) return false;
    // deleteLater is queued to the creating thread. Delete now that the worker
    // is joined so a no-event-loop shutdown does not retain its QThread object.
    m_thread = nullptr;
    delete thread;
    return true;
}

} // namespace hotas
