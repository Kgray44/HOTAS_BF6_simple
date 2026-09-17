#include "config_persistence_coordinator.h"

#include <QMutex>
#include <QMutexLocker>
#include <QTest>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace hotas {
namespace {

MapperConfiguration configurationWithAxis(int axis)
{
    MapperConfiguration configuration = defaultConfiguration();
    configuration.selectedAxisIndex = axis;
    return configuration;
}

ConfigStore::SaveResult successfulWrite()
{
    return {true, 100, 200, 300};
}

} // namespace

class ConfigPersistenceCoordinatorTests final : public QObject {
    Q_OBJECT

private slots:
    void burstCoalescesAndNewestSnapshotWins();
    void stalePendingGenerationIsNeverWritten();
    void failedWriteDoesNotAdvanceDurabilityAndLaterRequestRecovers();
    void shutdownFlushTimesOutBoundedlyThenStopsWithoutThreadLeak();
};

void ConfigPersistenceCoordinatorTests::burstCoalescesAndNewestSnapshotWins()
{
    QMutex writesMutex;
    std::vector<int> writtenAxes;
    ConfigPersistenceCoordinator coordinator([&](const MapperConfiguration &configuration) {
        QThread::msleep(20);
        QMutexLocker locker(&writesMutex);
        writtenAxes.push_back(configuration.selectedAxisIndex);
        return successfulWrite();
    });
    coordinator.setTelemetryEnabled(true);

    std::vector<qint64> guiRequestTimes;
    guiRequestTimes.reserve(500);
    for (int index = 0; index < 500; ++index) {
        const auto request = coordinator.request(configurationWithAxis(index));
        guiRequestTimes.push_back(request.enqueuedNs - request.captureStartedNs);
    }

    const auto result = coordinator.flushLatest(5000);
    QVERIFY2(result.durable(), "the newest burst generation must become durable");
    const auto stats = coordinator.statistics();
    QCOMPARE(stats.requests, quint64{500});
    QVERIFY2(stats.writes <= 2, "only an in-flight write plus the newest pending snapshot may run");
    QCOMPARE(stats.superseded, quint64{498});
    QCOMPARE(stats.durableGeneration, stats.latestRequestedGeneration);
    std::sort(guiRequestTimes.begin(), guiRequestTimes.end());
    const auto percentileMs = [&guiRequestTimes](double fraction) {
        const size_t index = std::min(guiRequestTimes.size() - 1,
            static_cast<size_t>(std::ceil(fraction * guiRequestTimes.size())) - 1);
        return static_cast<double>(guiRequestTimes[index]) / 1'000'000.0;
    };
    const double p95GuiRequestMs = percentileMs(0.95);
    const double p99GuiRequestMs = percentileMs(0.99);
    qInfo().nospace() << "config_persistence_burst gui_request_p95_ms=" << p95GuiRequestMs
                      << " gui_request_p99_ms=" << p99GuiRequestMs;
    QVERIFY2(p95GuiRequestMs < 2.0 && p99GuiRequestMs < 5.0,
             "GUI snapshot capture plus enqueue must not include a disk sync");
    {
        QMutexLocker locker(&writesMutex);
        QVERIFY(!writtenAxes.empty());
        QCOMPARE(writtenAxes.back(), 499);
    }
    QVERIFY(coordinator.stop(1000));
}

void ConfigPersistenceCoordinatorTests::stalePendingGenerationIsNeverWritten()
{
    QMutex gateMutex;
    QWaitCondition firstWriteStarted;
    QWaitCondition releaseFirstWrite;
    bool firstStarted = false;
    bool release = false;
    std::vector<int> writtenAxes;
    ConfigPersistenceCoordinator coordinator([&](const MapperConfiguration &configuration) {
        QMutexLocker locker(&gateMutex);
        writtenAxes.push_back(configuration.selectedAxisIndex);
        if (!firstStarted) {
            firstStarted = true;
            firstWriteStarted.wakeAll();
            while (!release) releaseFirstWrite.wait(&gateMutex);
        }
        return successfulWrite();
    });

    const auto generation10 = coordinator.request(configurationWithAxis(10));
    {
        QMutexLocker locker(&gateMutex);
        QVERIFY(firstWriteStarted.wait(&gateMutex, 1000));
    }
    const auto generation11 = coordinator.request(configurationWithAxis(11));
    const auto generation12 = coordinator.request(configurationWithAxis(12));
    {
        QMutexLocker locker(&gateMutex);
        release = true;
        releaseFirstWrite.wakeAll();
    }

    const auto result = coordinator.flushThrough(generation12.generation, 2000);
    QVERIFY(result.durable());
    QCOMPARE(generation10.generation, quint64{1});
    QCOMPARE(generation11.generation, quint64{2});
    QCOMPARE(generation12.generation, quint64{3});
    {
        QMutexLocker locker(&gateMutex);
        QCOMPARE(writtenAxes, std::vector<int>({10, 12}));
    }
    const auto stats = coordinator.statistics();
    QCOMPARE(stats.superseded, quint64{1});
    QCOMPARE(stats.durableGeneration, generation12.generation);
    QVERIFY(coordinator.stop(1000));
}

void ConfigPersistenceCoordinatorTests::failedWriteDoesNotAdvanceDurabilityAndLaterRequestRecovers()
{
    std::atomic_int attempts{0};
    ConfigPersistenceCoordinator coordinator([&](const MapperConfiguration &) {
        if (attempts.fetch_add(1) == 0) return ConfigStore::SaveResult{false, 1, 2, 3};
        return successfulWrite();
    });
    const MapperConfiguration first = configurationWithAxis(3);
    const auto failed = coordinator.requestAndFlush(first, 1000);
    QCOMPARE(failed.status, ConfigPersistenceCoordinator::FlushStatus::Failed);
    QCOMPARE(first.selectedAxisIndex, 3);
    const auto afterFailure = coordinator.statistics();
    QCOMPARE(afterFailure.durableGeneration, quint64{0});
    QCOMPARE(afterFailure.failures, quint64{1});

    const MapperConfiguration retry = configurationWithAxis(4);
    const auto recovered = coordinator.requestAndFlush(retry, 1000);
    QVERIFY(recovered.durable());
    const auto afterRecovery = coordinator.statistics();
    QCOMPARE(afterRecovery.durableGeneration, quint64{2});
    QCOMPARE(afterRecovery.failures, quint64{1});
    QCOMPARE(attempts.load(), 2);
    QVERIFY(coordinator.stop(1000));
}

void ConfigPersistenceCoordinatorTests::shutdownFlushTimesOutBoundedlyThenStopsWithoutThreadLeak()
{
    QMutex gateMutex;
    QWaitCondition writerStarted;
    QWaitCondition releaseWriter;
    bool started = false;
    bool release = false;
    ConfigPersistenceCoordinator coordinator([&](const MapperConfiguration &) {
        QMutexLocker locker(&gateMutex);
        started = true;
        writerStarted.wakeAll();
        while (!release) releaseWriter.wait(&gateMutex);
        return successfulWrite();
    });

    coordinator.request(configurationWithAxis(5));
    {
        QMutexLocker locker(&gateMutex);
        QVERIFY(writerStarted.wait(&gateMutex, 1000));
    }
    QCOMPARE(coordinator.flushLatest(20).status, ConfigPersistenceCoordinator::FlushStatus::TimedOut);
    QVERIFY(!coordinator.stop(20));
    {
        QMutexLocker locker(&gateMutex);
        release = true;
        releaseWriter.wakeAll();
    }
    QVERIFY(coordinator.stop(1000));
}

} // namespace hotas

QTEST_GUILESS_MAIN(hotas::ConfigPersistenceCoordinatorTests)
#include "config_persistence_coordinator_tests.moc"
