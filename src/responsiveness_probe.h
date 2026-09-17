#pragma once

#include <QMetaObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QEvent;
class QJsonObject;
class QQuickWindow;

namespace hotas {

// Opt-in, control-plane-only responsiveness evidence.  This object is never
// created unless HOTAS_RESPONSIVENESS_PROBE=1, and it is intentionally absent
// from the DirectInput -> MappingWorker -> vJoy report path.
class ResponsivenessProbe final : public QObject {
    Q_OBJECT

public:
    static void installIfEnabled(QObject *parent);
    static ResponsivenessProbe *active();
    static void exportActive();
    static qint64 monotonicNowNs();

    ~ResponsivenessProbe() override;

    void attachWindow(QQuickWindow *window);
    void recordNavigationRequested(int page, const QString &pageName);
    void recordNavigationLoaderActivated(int page, const QString &pageName);
    void recordNavigationObjectReady(int page, const QString &pageName);
    void recordConfigSave(qint64 startedNs, qint64 finishedNs, qint64 serializationNs,
                          qint64 setValueNs, qint64 syncNs, bool success, bool guiThread);
    void recordPersistenceEnqueue(qint64 captureStartedNs, qint64 captureFinishedNs,
                                  qint64 enqueuedNs, quint64 generation, bool supersededPending);
    void recordPersistenceWorker(quint64 generation, qint64 enqueuedNs, qint64 workerStartedNs,
                                 qint64 workerFinishedNs, qint64 serializationNs,
                                 qint64 setValueNs, qint64 syncNs, bool success);
    void recordPersistenceState(quint64 requests, quint64 writes, quint64 superseded,
                                quint64 latestRequestedGeneration, quint64 durableGeneration,
                                quint64 failures, quint64 lastFailedGeneration);
    // Qualification-only scroll seam.  The native driver owns wheel injection
    // and samples the actual Flickable content position; this probe joins that
    // position change to the first following Qt Quick frame without retaining
    // a per-pixel trace or writing during the session.
    void beginScrollSession(const QString &page, const QString &surfaceId,
                            const QString &pattern, const QString &windowClass,
                            const QString &contentionLevel, qreal initialContentY);
    void recordScrollWheel();
    void recordScrollWheelDisposition(bool accepted);
    void recordScrollPosition(qreal contentY);
    void recordNotScrollable(const QString &page, const QString &surfaceId,
                             const QString &windowClass, const QString &contentionLevel,
                             const QString &reason);
    void endScrollSession();
    QString exportReport(const QString &requestedPath = QString());

    // Narrow test seam: it verifies aggregation and bounded reporting without
    // manufacturing application input or waking a render loop.
    void recordEventLoopDelayForTest(double delayMs);
    // Narrow startup-boundary seams: they exercise report classification
    // without creating a native window in the Core-only aggregation test.
    void recordStartupWindowReadyForTest();
    void recordFirstPresentedFrameForTest();
    void recordEventLoopHeartbeatForTest();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct SampleSet {
        QVector<double> retained;
        quint64 sampleCount = 0;
        quint64 droppedSamples = 0;
        double maximumMs = 0.0;
        quint64 over16Ms = 0;
        quint64 over33Ms = 0;
        quint64 over50Ms = 0;
        quint64 over100Ms = 0;
        quint64 over250Ms = 0;
        quint64 over500Ms = 0;
        quint64 over1000Ms = 0;
        quint64 over5000Ms = 0;

        void add(double milliseconds);
    };

    struct PendingInput {
        qint64 receivedNs = 0;
        QString interactionClass;
        QString page;
    };

    struct NavigationRecord {
        int page = -1;
        QString pageName;
        qint64 requestedNs = -1;
        qint64 loaderActivatedNs = -1;
        qint64 objectReadyNs = -1;
        qint64 firstPresentedNs = -1;
    };

    struct ConfigSaveRecord {
        qint64 startedNs = 0;
        qint64 finishedNs = 0;
        qint64 serializationNs = 0;
        qint64 setValueNs = 0;
        qint64 syncNs = 0;
        bool success = false;
        bool guiThread = false;
    };

    struct MajorEvent {
        QString kind;
        QString page;
        QString detail;
        qint64 timestampNs = 0;
        double durationMs = 0.0;
    };

    struct ScrollWheelRecord {
        qint64 receivedNs = 0;
        qint64 movementNs = -1;
        qint64 firstPresentedNs = -1;
        bool accepted = false;
        bool dispositionRecorded = false;
    };

    struct ScrollSession {
        QString page;
        QString surfaceId;
        QString pattern;
        QString windowClass;
        QString contentionLevel;
        QString notScrollableReason;
        qreal initialContentY = 0.0;
        qreal lastContentY = 0.0;
        qreal totalMovement = 0.0;
        qint64 startedNs = 0;
        qint64 lastWheelNs = -1;
        qint64 lastMovementNs = -1;
        qint64 finishedNs = -1;
        qint64 firstWheelToVisibleMovementNs = -1;
        quint64 wheelEvents = 0;
        quint64 droppedWheelRecords = 0;
        quint64 contentPositionChanges = 0;
        quint64 acceptedWheelEvents = 0;
        quint64 unacceptedWheelEvents = 0;
        bool scrollable = true;
        SampleSet movementLatency;
        SampleSet frameLatency;
        SampleSet activeFrameIntervals;
        QVector<ScrollWheelRecord> wheels;
    };

    explicit ResponsivenessProbe(QObject *parent);

    void recordInput(const QString &interactionClass);
    void recordFrameSwapped();
    void recordEventLoopHeartbeat();
    void recordWindowReady(qint64 nowNs);
    void completePendingInputsLocked(qint64 frameNs);
    void completePendingNavigationLocked(qint64 frameNs);
    void completePendingScrollFramesLocked(qint64 frameNs, double frameIntervalMs);
    void addMajorEventLocked(const QString &kind, const QString &page, const QString &detail,
                             qint64 timestampNs, double durationMs);
    NavigationRecord *findOrCreateNavigationLocked(int page, const QString &pageName);
    static QJsonObject summarizeSamples(const SampleSet &samples);

    QMutex m_mutex;
    QTimer m_eventLoopHeartbeatTimer;
    QMetaObject::Connection m_frameSwappedConnection;
    QQuickWindow *m_window = nullptr;
    qint64 m_startedNs = 0;
    qint64 m_lastHeartbeatNs = 0;
    qint64 m_lastFrameNs = 0;
    qint64 m_windowReadyNs = -1;
    qint64 m_firstPresentedFrameNs = -1;
    qint64 m_firstHeartbeatNs = -1;
    qint64 m_runtimeHeartbeatArmedNs = -1;
    double m_firstHeartbeatSinceProbeStartMs = 0.0;
    double m_runtimeHeartbeatBaselineMs = 0.0;
    bool m_firstHeartbeatBeforeFirstPresentedFrame = false;
    qint64 m_lastConfigSaveStartedNs = -1;
    QString m_startedAt;
    QString m_currentPage;
    SampleSet m_eventLoopDelays;
    SampleSet m_interactionLatencies;
    SampleSet m_frameIntervals;
    SampleSet m_configSaveTotals;
    SampleSet m_configSerialization;
    SampleSet m_configSetValue;
    SampleSet m_configSync;
    SampleSet m_persistenceGuiSnapshot;
    SampleSet m_persistenceGuiEnqueue;
    SampleSet m_persistenceWorkerQueueWait;
    SampleSet m_persistenceWorkerTotal;
    SampleSet m_persistenceWorkerSerialization;
    SampleSet m_persistenceWorkerSetValue;
    SampleSet m_persistenceWorkerSync;
    QVector<PendingInput> m_pendingInputs;
    QVector<NavigationRecord> m_navigation;
    QVector<ConfigSaveRecord> m_configSaves;
    QVector<MajorEvent> m_majorEvents;
    QVector<ScrollSession> m_scrollSessions;
    int m_activeScrollSession = -1;
    quint64 m_droppedPendingInputs = 0;
    quint64 m_droppedNavigationRecords = 0;
    quint64 m_droppedScrollSessions = 0;
    quint64 m_droppedConfigSaveRecords = 0;
    quint64 m_configSaveBursts = 0;
    quint64 m_configSaveFailures = 0;
    quint64 m_configSaveGuiThreadCount = 0;
    quint64 m_persistenceRequests = 0;
    quint64 m_persistenceWrites = 0;
    quint64 m_persistenceSuperseded = 0;
    quint64 m_persistenceLatestRequestedGeneration = 0;
    quint64 m_persistenceDurableGeneration = 0;
    quint64 m_persistenceFailures = 0;
    quint64 m_persistenceLastFailedGeneration = 0;
};

} // namespace hotas
