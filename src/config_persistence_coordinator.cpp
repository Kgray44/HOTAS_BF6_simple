#include "config_persistence_coordinator.h"

#include <QElapsedTimer>

#include <algorithm>
#include <chrono>
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
    for (;;) {
        Pending current;
        bool telemetryEnabled = false;
        {
            QMutexLocker locker(&state->mutex);
            while (!state->stopping && !state->pending) state->changed.wait(&state->mutex);
            if (state->stopping && !state->pending) return;

            current = std::move(*state->pending);
            state->pending.reset();
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
        const bool nothingOutstanding = !m_state->pending && m_state->inFlightGeneration == 0;
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
            m_state->pending.has_value(), m_state->inFlightGeneration != 0};
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
