#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

namespace hotas {

// A GUI/control-plane-only contention controller. It deliberately observes
// host contention from the GUI event loop and publishes cadence policy for
// presentation and background control work. It is never referenced by the
// DirectInput -> MappingWorker -> vJoy report path.
class ContentionResilienceController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(int pressureLevel READ pressureLevel NOTIFY policyChanged)
    Q_PROPERTY(QString levelName READ levelName NOTIFY policyChanged)
    Q_PROPERTY(int telemetryIntervalMs READ telemetryIntervalMs NOTIFY policyChanged)
    Q_PROPERTY(int liveGraphIntervalMs READ liveGraphIntervalMs NOTIFY policyChanged)
    Q_PROPERTY(int backgroundPollMultiplier READ backgroundPollMultiplier NOTIFY policyChanged)
    Q_PROPERTY(bool decorativeMotionAllowed READ decorativeMotionAllowed NOTIFY policyChanged)
    Q_PROPERTY(qreal nonessentialAnimationScale READ nonessentialAnimationScale NOTIFY policyChanged)

public:
    enum class Level {
        Normal = 0,
        Pressure = 1,
        Severe = 2,
    };
    Q_ENUM(Level)

    struct Policy {
        int telemetryIntervalMs = 33;
        int liveGraphIntervalMs = 33;
        int backgroundPollMultiplier = 1;
        bool decorativeMotionAllowed = true;
        qreal nonessentialAnimationScale = 1.0;
    };

    explicit ContentionResilienceController(QObject *parent = nullptr);

    Level level() const { return m_level; }
    int pressureLevel() const { return static_cast<int>(m_level); }
    QString levelName() const;
    const Policy &policy() const { return m_policy; }
    int telemetryIntervalMs() const { return m_policy.telemetryIntervalMs; }
    int liveGraphIntervalMs() const { return m_policy.liveGraphIntervalMs; }
    int backgroundPollMultiplier() const { return m_policy.backgroundPollMultiplier; }
    bool decorativeMotionAllowed() const { return m_policy.decorativeMotionAllowed; }
    qreal nonessentialAnimationScale() const { return m_policy.nonessentialAnimationScale; }
    bool isSevere() const { return m_level == Level::Severe; }

    // Narrow test seam. Test values are injected; no CPU stress is generated.
    void observeForTest(int totalCpuPercent, qint64 heartbeatMs, qint64 nowMs);
    static Policy policyForLevel(Level level);

    Q_INVOKABLE void recordLiveGraphRefresh();
    void recordTelemetryPublication();
    void recordAdaptiveHistorySample();
    void recordBackgroundPoll(const QString &kind);
    QVariantMap evidence() const;

signals:
    void policyChanged();

private:
    enum class Override {
        Auto,
        Normal,
        Pressure,
        Severe,
    };

    void observe(int totalCpuPercent, qint64 heartbeatMs, qint64 nowMs);
    void setLevel(Level level, qint64 nowMs);
    int readTotalCpuPercent();
    static Override overrideFromEnvironment();

    QTimer m_heartbeatTimer;
    QElapsedTimer m_clock;
    qint64 m_lastHeartbeatMs = 0;
    qint64 m_lastCpuSampleMs = -1;
    Level m_level = Level::Normal;
    Policy m_policy = policyForLevel(Level::Normal);
    Override m_override = Override::Auto;
    int m_pressureSamples = 0;
    int m_severeSamples = 0;
    int m_recoverySamples = 0;
    int m_lastCpuPercent = -1;
    int m_minCpuPercent = 101;
    int m_maxCpuPercent = -1;
    quint64 m_cpuSampleCount = 0;
    quint64 m_levelTransitions = 0;
    quint64 m_telemetryPublications = 0;
    quint64 m_liveGraphRefreshes = 0;
    quint64 m_adaptiveHistorySamples = 0;
    quint64 m_controllerPolls = 0;
    quint64 m_gamePolls = 0;
    QVariantList m_transitionTimeline;
};

} // namespace hotas
