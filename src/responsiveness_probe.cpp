#include "responsiveness_probe.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <utility>

namespace hotas {
using namespace Qt::StringLiterals;

namespace {

constexpr int kExpectedHeartbeatMs = 16;
constexpr int kMaximumRetainedSamples = 4096;
constexpr int kMaximumPendingInputs = 512;
constexpr int kMaximumNavigationRecords = 256;
constexpr int kMaximumConfigSaveRecords = 128;
constexpr int kMaximumMajorEvents = 64;
constexpr qint64 kBurstWindowNs = 250'000'000;
constexpr double kMajorStallMs = 250.0;

std::atomic<ResponsivenessProbe *> g_activeProbe{nullptr};

double millisecondsBetween(qint64 startNs, qint64 endNs)
{
    return std::max(0.0, static_cast<double>(endNs - startNs) / 1'000'000.0);
}

QString inputClassForEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::MouseButtonPress: return u"mouse-press"_qs;
    case QEvent::MouseButtonRelease: return u"mouse-release"_qs;
    case QEvent::Wheel: return u"wheel"_qs;
    case QEvent::KeyPress: return u"key-press"_qs;
    case QEvent::KeyRelease: return u"key-release"_qs;
    case QEvent::TouchBegin: return u"touch-begin"_qs;
    case QEvent::TouchUpdate: return u"touch-update"_qs;
    case QEvent::TouchEnd: return u"touch-end"_qs;
    default: return {};
    }
}

} // namespace

void ResponsivenessProbe::SampleSet::add(double milliseconds)
{
    ++sampleCount;
    maximumMs = std::max(maximumMs, milliseconds);
    if (milliseconds > 16.0) ++over16Ms;
    if (milliseconds > 33.0) ++over33Ms;
    if (milliseconds > 50.0) ++over50Ms;
    if (milliseconds > 100.0) ++over100Ms;
    if (milliseconds > 250.0) ++over250Ms;
    if (milliseconds > 500.0) ++over500Ms;
    if (milliseconds > 1000.0) ++over1000Ms;
    if (milliseconds > 5000.0) ++over5000Ms;
    if (retained.size() < kMaximumRetainedSamples) {
        retained.append(milliseconds);
    } else {
        ++droppedSamples;
    }
}

QJsonObject ResponsivenessProbe::summarizeSamples(const SampleSet &samples)
{
    QJsonObject result;
    result.insert(u"sampleCount"_qs, static_cast<qint64>(samples.sampleCount));
    result.insert(u"retainedSampleCount"_qs, samples.retained.size());
    result.insert(u"droppedSampleCount"_qs, static_cast<qint64>(samples.droppedSamples));
    result.insert(u"maximumMs"_qs, samples.maximumMs);

    if (!samples.retained.isEmpty()) {
        QVector<double> sorted = samples.retained;
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&sorted](double fraction) {
            const int count = static_cast<int>(sorted.size());
            const int index = std::clamp(static_cast<int>(std::ceil(fraction * count)) - 1,
                                         0, count - 1);
            return sorted.at(index);
        };
        result.insert(u"p50Ms"_qs, percentile(0.50));
        result.insert(u"p95Ms"_qs, percentile(0.95));
        result.insert(u"p99Ms"_qs, percentile(0.99));
    }

    result.insert(u"over16Ms"_qs, static_cast<qint64>(samples.over16Ms));
    result.insert(u"over33Ms"_qs, static_cast<qint64>(samples.over33Ms));
    result.insert(u"over50Ms"_qs, static_cast<qint64>(samples.over50Ms));
    result.insert(u"over100Ms"_qs, static_cast<qint64>(samples.over100Ms));
    result.insert(u"over250Ms"_qs, static_cast<qint64>(samples.over250Ms));
    result.insert(u"over500Ms"_qs, static_cast<qint64>(samples.over500Ms));
    result.insert(u"over1000Ms"_qs, static_cast<qint64>(samples.over1000Ms));
    result.insert(u"over5000Ms"_qs, static_cast<qint64>(samples.over5000Ms));
    return result;
}

qint64 ResponsivenessProbe::monotonicNowNs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

void ResponsivenessProbe::installIfEnabled(QObject *parent)
{
    if (qEnvironmentVariableIntValue("HOTAS_RESPONSIVENESS_PROBE") == 0
        || g_activeProbe.load(std::memory_order_acquire)) {
        return;
    }
    auto *probe = new ResponsivenessProbe(parent);
    ResponsivenessProbe *expected = nullptr;
    if (!g_activeProbe.compare_exchange_strong(expected, probe, std::memory_order_release,
                                                std::memory_order_acquire)) {
        delete probe;
    }
}

ResponsivenessProbe *ResponsivenessProbe::active()
{
    return g_activeProbe.load(std::memory_order_acquire);
}

void ResponsivenessProbe::exportActive()
{
    if (auto *probe = active()) probe->exportReport();
}

ResponsivenessProbe::ResponsivenessProbe(QObject *parent)
    : QObject(parent)
    , m_startedNs(monotonicNowNs())
    , m_lastHeartbeatNs(m_startedNs)
    , m_startedAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
{
    if (auto *application = QCoreApplication::instance()) application->installEventFilter(this);
    m_eventLoopHeartbeatTimer.setInterval(kExpectedHeartbeatMs);
    connect(&m_eventLoopHeartbeatTimer, &QTimer::timeout, this,
            &ResponsivenessProbe::recordEventLoopHeartbeat);
    m_eventLoopHeartbeatTimer.start();
}

ResponsivenessProbe::~ResponsivenessProbe()
{
    m_eventLoopHeartbeatTimer.stop();
    if (auto *application = QCoreApplication::instance()) application->removeEventFilter(this);
    if (m_frameSwappedConnection) QObject::disconnect(m_frameSwappedConnection);
    ResponsivenessProbe *expected = this;
    g_activeProbe.compare_exchange_strong(expected, nullptr, std::memory_order_release,
                                          std::memory_order_acquire);
}

bool ResponsivenessProbe::eventFilter(QObject *, QEvent *event)
{
    if (!event) return false;
    const QString interactionClass = inputClassForEvent(event->type());
    if (!interactionClass.isEmpty()) recordInput(interactionClass);
    return false;
}

void ResponsivenessProbe::attachWindow(QQuickWindow *window)
{
    if (m_window == window) return;
    if (m_frameSwappedConnection) QObject::disconnect(m_frameSwappedConnection);
    m_window = window;
    if (!m_window) return;

    // frameSwapped is the closest safe practical Qt Quick boundary to a
    // presented frame. The signal may be emitted from the render thread, so
    // this direct slot touches only mutex-protected probe data and never QML.
    m_frameSwappedConnection = connect(m_window, &QQuickWindow::frameSwapped, this,
        [this] { recordFrameSwapped(); }, Qt::DirectConnection);
}

void ResponsivenessProbe::recordInput(const QString &interactionClass)
{
    const qint64 receivedNs = monotonicNowNs();
    QMutexLocker locker(&m_mutex);
    if (m_pendingInputs.size() >= kMaximumPendingInputs) {
        ++m_droppedPendingInputs;
        return;
    }
    m_pendingInputs.append({receivedNs, interactionClass, m_currentPage});
}

ResponsivenessProbe::NavigationRecord *ResponsivenessProbe::findOrCreateNavigationLocked(
    int page, const QString &pageName)
{
    for (auto it = m_navigation.rbegin(); it != m_navigation.rend(); ++it) {
        if (it->page == page && it->firstPresentedNs < 0) {
            if (!pageName.isEmpty()) it->pageName = pageName;
            return &*it;
        }
    }
    if (m_navigation.size() >= kMaximumNavigationRecords) {
        m_navigation.removeFirst();
        ++m_droppedNavigationRecords;
    }
    m_navigation.append({page, pageName});
    return &m_navigation.last();
}

void ResponsivenessProbe::recordNavigationRequested(int page, const QString &pageName)
{
    const qint64 nowNs = monotonicNowNs();
    QMutexLocker locker(&m_mutex);
    m_currentPage = pageName;
    NavigationRecord *record = findOrCreateNavigationLocked(page, pageName);
    record->requestedNs = nowNs;
}

void ResponsivenessProbe::recordNavigationLoaderActivated(int page, const QString &pageName)
{
    const qint64 nowNs = monotonicNowNs();
    QMutexLocker locker(&m_mutex);
    NavigationRecord *record = findOrCreateNavigationLocked(page, pageName);
    if (record->loaderActivatedNs < 0) record->loaderActivatedNs = nowNs;
}

void ResponsivenessProbe::recordNavigationObjectReady(int page, const QString &pageName)
{
    const qint64 nowNs = monotonicNowNs();
    QMutexLocker locker(&m_mutex);
    NavigationRecord *record = findOrCreateNavigationLocked(page, pageName);
    if (record->objectReadyNs < 0) record->objectReadyNs = nowNs;
}

void ResponsivenessProbe::completePendingInputsLocked(qint64 frameNs)
{
    for (const PendingInput &input : std::as_const(m_pendingInputs)) {
        const double latencyMs = millisecondsBetween(input.receivedNs, frameNs);
        m_interactionLatencies.add(latencyMs);
        if (latencyMs >= kMajorStallMs) {
            addMajorEventLocked(u"interaction-to-frame"_qs, input.page, input.interactionClass,
                                input.receivedNs, latencyMs);
        }
    }
    m_pendingInputs.clear();
}

void ResponsivenessProbe::completePendingNavigationLocked(qint64 frameNs)
{
    for (NavigationRecord &navigation : m_navigation) {
        if (navigation.objectReadyNs >= 0 && navigation.firstPresentedNs < 0
            && navigation.objectReadyNs <= frameNs) {
            navigation.firstPresentedNs = frameNs;
            if (navigation.requestedNs >= 0) {
                const double durationMs = millisecondsBetween(navigation.requestedNs, frameNs);
                if (durationMs >= kMajorStallMs) {
                    addMajorEventLocked(u"navigation-to-frame"_qs, navigation.pageName,
                                        u"destination first frame"_qs, navigation.requestedNs, durationMs);
                }
            }
        }
    }
}

void ResponsivenessProbe::recordFrameSwapped()
{
    const qint64 nowNs = monotonicNowNs();
    QMutexLocker locker(&m_mutex);
    if (m_lastFrameNs > 0) {
        const double intervalMs = millisecondsBetween(m_lastFrameNs, nowNs);
        m_frameIntervals.add(intervalMs);
        if (intervalMs >= kMajorStallMs) {
            addMajorEventLocked(u"frame-interval"_qs, m_currentPage, u"frameSwapped interval"_qs,
                                m_lastFrameNs, intervalMs);
        }
    }
    m_lastFrameNs = nowNs;
    completePendingInputsLocked(nowNs);
    completePendingNavigationLocked(nowNs);
}

void ResponsivenessProbe::recordEventLoopHeartbeat()
{
    const qint64 nowNs = monotonicNowNs();
    const double intervalMs = millisecondsBetween(m_lastHeartbeatNs, nowNs);
    const double delayMs = std::max(0.0, intervalMs - kExpectedHeartbeatMs);
    QMutexLocker locker(&m_mutex);
    m_lastHeartbeatNs = nowNs;
    m_eventLoopDelays.add(delayMs);
    if (delayMs >= kMajorStallMs) {
        addMajorEventLocked(u"event-loop-delay"_qs, m_currentPage, u"16 ms heartbeat"_qs,
                            nowNs - static_cast<qint64>(intervalMs * 1'000'000.0), delayMs);
    }
}

void ResponsivenessProbe::recordEventLoopDelayForTest(double delayMs)
{
    QMutexLocker locker(&m_mutex);
    m_eventLoopDelays.add(std::max(0.0, delayMs));
}

void ResponsivenessProbe::recordConfigSave(qint64 startedNs, qint64 finishedNs, qint64 serializationNs,
                                            qint64 setValueNs, qint64 syncNs, bool success,
                                            bool guiThread)
{
    const double totalMs = millisecondsBetween(startedNs, finishedNs);
    QMutexLocker locker(&m_mutex);
    m_configSaveTotals.add(totalMs);
    m_configSerialization.add(static_cast<double>(serializationNs) / 1'000'000.0);
    m_configSetValue.add(static_cast<double>(setValueNs) / 1'000'000.0);
    m_configSync.add(static_cast<double>(syncNs) / 1'000'000.0);
    if (!success) ++m_configSaveFailures;
    if (guiThread) ++m_configSaveGuiThreadCount;
    if (m_lastConfigSaveStartedNs >= 0 && startedNs - m_lastConfigSaveStartedNs <= kBurstWindowNs) {
        ++m_configSaveBursts;
    }
    m_lastConfigSaveStartedNs = startedNs;
    if (m_configSaves.size() >= kMaximumConfigSaveRecords) {
        m_configSaves.removeFirst();
        ++m_droppedConfigSaveRecords;
    }
    m_configSaves.append({startedNs, finishedNs, serializationNs, setValueNs, syncNs, success, guiThread});
    if (totalMs >= kMajorStallMs) {
        addMajorEventLocked(u"config-save"_qs, m_currentPage,
                            success ? u"QSettings::sync completed"_qs : u"QSettings save failed"_qs,
                            startedNs, totalMs);
    }
}

void ResponsivenessProbe::addMajorEventLocked(const QString &kind, const QString &page,
                                               const QString &detail, qint64 timestampNs,
                                               double durationMs)
{
    MajorEvent event{kind, page, detail, timestampNs, durationMs};
    if (m_majorEvents.size() < kMaximumMajorEvents) {
        m_majorEvents.append(std::move(event));
        return;
    }
    auto smallest = std::min_element(m_majorEvents.begin(), m_majorEvents.end(),
        [](const MajorEvent &left, const MajorEvent &right) { return left.durationMs < right.durationMs; });
    if (smallest != m_majorEvents.end() && smallest->durationMs < durationMs) *smallest = std::move(event);
}

QString ResponsivenessProbe::exportReport(const QString &requestedPath)
{
    QJsonObject report;
    report.insert(u"schemaVersion"_qs, 1);
    report.insert(u"commit"_qs, qEnvironmentVariable("HOTAS_RESPONSIVENESS_COMMIT", "unknown"));
    report.insert(u"build"_qs, QCoreApplication::applicationVersion());
    report.insert(u"mode"_qs, qEnvironmentVariable("HOTAS_RESPONSIVENESS_SCENARIO", "unspecified"));
    report.insert(u"startedAt"_qs, m_startedAt);
    report.insert(u"exportedAt"_qs, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert(u"systemLoad"_qs, QJsonObject{
        {u"logicalProcessors"_qs, QThread::idealThreadCount()},
        {u"processId"_qs, static_cast<qint64>(QCoreApplication::applicationPid())},
        {u"kernelType"_qs, QSysInfo::kernelType()},
        {u"kernelVersion"_qs, QSysInfo::kernelVersion()},
        {u"productType"_qs, QSysInfo::productType()},
        {u"scenario"_qs, qEnvironmentVariable("HOTAS_RESPONSIVENESS_SCENARIO", "unspecified")},
        {u"externalLoadEvidence"_qs,
         qEnvironmentVariable("HOTAS_RESPONSIVENESS_LOAD_EVIDENCE", "not-provided")},
    });

    QMutexLocker locker(&m_mutex);
    QJsonObject eventLoop = summarizeSamples(m_eventLoopDelays);
    eventLoop.insert(u"expectedHeartbeatIntervalMs"_qs, kExpectedHeartbeatMs);
    eventLoop.insert(u"definition"_qs, u"observed heartbeat interval minus 16 ms"_qs);
    report.insert(u"eventLoop"_qs, eventLoop);
    report.insert(u"interactionLatency"_qs, summarizeSamples(m_interactionLatencies));
    report.insert(u"framePacing"_qs, summarizeSamples(m_frameIntervals));
    report.insert(u"pendingInputEventsAtExport"_qs, m_pendingInputs.size());
    report.insert(u"droppedPendingInputEvents"_qs, static_cast<qint64>(m_droppedPendingInputs));

    QJsonArray navigation;
    for (const NavigationRecord &record : m_navigation) {
        QJsonObject entry{{u"page"_qs, record.page}, {u"pageName"_qs, record.pageName}};
        if (record.requestedNs >= 0) entry.insert(u"requestedSinceStartMs"_qs,
                                                   millisecondsBetween(m_startedNs, record.requestedNs));
        if (record.requestedNs >= 0 && record.loaderActivatedNs >= 0) {
            entry.insert(u"requestToLoaderActivationMs"_qs,
                         millisecondsBetween(record.requestedNs, record.loaderActivatedNs));
        }
        if (record.requestedNs >= 0 && record.objectReadyNs >= 0) {
            entry.insert(u"requestToObjectReadyMs"_qs,
                         millisecondsBetween(record.requestedNs, record.objectReadyNs));
        }
        if (record.requestedNs >= 0 && record.firstPresentedNs >= 0) {
            entry.insert(u"requestToFirstPresentedFrameMs"_qs,
                         millisecondsBetween(record.requestedNs, record.firstPresentedNs));
        }
        navigation.append(entry);
    }
    report.insert(u"navigation"_qs, navigation);
    report.insert(u"droppedNavigationRecords"_qs, static_cast<qint64>(m_droppedNavigationRecords));

    QJsonObject persistence{{u"total"_qs, summarizeSamples(m_configSaveTotals)},
                            {u"serialization"_qs, summarizeSamples(m_configSerialization)},
                            {u"setValue"_qs, summarizeSamples(m_configSetValue)},
                            {u"sync"_qs, summarizeSamples(m_configSync)},
                            {u"saveCount"_qs, static_cast<qint64>(m_configSaveTotals.sampleCount)},
                            {u"failureCount"_qs, static_cast<qint64>(m_configSaveFailures)},
                            {u"guiThreadSaveCount"_qs, static_cast<qint64>(m_configSaveGuiThreadCount)},
                            {u"backToBackBurstCount"_qs, static_cast<qint64>(m_configSaveBursts)},
                            {u"droppedSaveRecords"_qs, static_cast<qint64>(m_droppedConfigSaveRecords)}};
    QJsonArray recentSaves;
    for (const ConfigSaveRecord &save : m_configSaves) {
        recentSaves.append(QJsonObject{{u"startedSinceStartMs"_qs,
                                        millisecondsBetween(m_startedNs, save.startedNs)},
                                       {u"totalMs"_qs, millisecondsBetween(save.startedNs, save.finishedNs)},
                                       {u"serializationMs"_qs, static_cast<double>(save.serializationNs) / 1'000'000.0},
                                       {u"setValueMs"_qs, static_cast<double>(save.setValueNs) / 1'000'000.0},
                                       {u"syncMs"_qs, static_cast<double>(save.syncNs) / 1'000'000.0},
                                       {u"success"_qs, save.success},
                                       {u"guiThread"_qs, save.guiThread}});
    }
    persistence.insert(u"recentSaves"_qs, recentSaves);
    report.insert(u"configPersistence"_qs, persistence);

    QJsonArray majorEvents;
    std::sort(m_majorEvents.begin(), m_majorEvents.end(),
              [](const MajorEvent &left, const MajorEvent &right) { return left.durationMs > right.durationMs; });
    for (const MajorEvent &event : m_majorEvents) {
        majorEvents.append(QJsonObject{{u"kind"_qs, event.kind}, {u"page"_qs, event.page},
                                       {u"detail"_qs, event.detail},
                                       {u"startedSinceStartMs"_qs,
                                        millisecondsBetween(m_startedNs, event.timestampNs)},
                                       {u"durationMs"_qs, event.durationMs}});
    }
    report.insert(u"majorStalls"_qs, majorEvents);
    report.insert(u"mappingTelemetry"_qs, QJsonObject{{u"instrumented"_qs, false},
        {u"note"_qs, u"No MappingWorker report-path instrumentation was added; use the existing mapping benchmark and telemetry."_qs}});
    report.insert(u"notes"_qs, QJsonArray{
        u"Samples are bounded in memory and this file is written once at explicit export or clean application shutdown."_qs,
        u"Interaction latency is event receipt to the next QQuickWindow frameSwapped boundary; it is not semantic command completion or display scanout proof."_qs,
        u"A missing native input sample means no supported interactive event reached the application during this capture."_qs,
    });

    QString outputPath = requestedPath.trimmed();
    if (outputPath.isEmpty()) outputPath = qEnvironmentVariable("HOTAS_RESPONSIVENESS_PROBE_OUTPUT").trimmed();
    if (outputPath.isEmpty()) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + u"/responsiveness"_qs;
        QDir().mkpath(directory);
        outputPath = directory + u"/responsiveness-probe.json"_qs;
    }
    const QFileInfo outputInfo(outputPath);
    QDir().mkpath(outputInfo.absolutePath());
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) return {};
    output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    if (!output.commit()) return {};
    return outputPath;
}

} // namespace hotas
