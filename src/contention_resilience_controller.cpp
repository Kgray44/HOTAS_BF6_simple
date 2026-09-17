#include "contention_resilience_controller.h"

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotas {
namespace {

QString levelLabel(ContentionResilienceController::Level level)
{
    switch (level) {
    case ContentionResilienceController::Level::Normal: return QStringLiteral("normal");
    case ContentionResilienceController::Level::Pressure: return QStringLiteral("pressure");
    case ContentionResilienceController::Level::Severe: return QStringLiteral("severe");
    }
    return QStringLiteral("normal");
}

#ifdef Q_OS_WIN
quint64 fileTimeValue(const FILETIME &time)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}
#endif

} // namespace

ContentionResilienceController::ContentionResilienceController(QObject *parent)
    : QObject(parent)
    , m_override(overrideFromEnvironment())
{
    m_clock.start();
    // Establish the inexpensive Windows CPU baseline before presentation work
    // starts, so the first GUI heartbeat can classify true saturation rather
    // than losing a full sample interval to initialization.
    readTotalCpuPercent();
    m_lastCpuSampleMs = -750;
    m_heartbeatTimer.setInterval(200);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this] {
        const qint64 nowMs = m_clock.elapsed();
        const qint64 heartbeatMs = m_lastHeartbeatMs == 0 ? 0 : nowMs - m_lastHeartbeatMs;
        m_lastHeartbeatMs = nowMs;
        int cpu = -1;
        if (m_lastCpuSampleMs < 0 || nowMs - m_lastCpuSampleMs >= 750) {
            cpu = readTotalCpuPercent();
            m_lastCpuSampleMs = nowMs;
        }
        observe(cpu, heartbeatMs, nowMs);
    });
    m_heartbeatTimer.start();

    switch (m_override) {
    case Override::Normal: setLevel(Level::Normal, 0); break;
    case Override::Pressure: setLevel(Level::Pressure, 0); break;
    case Override::Severe: setLevel(Level::Severe, 0); break;
    case Override::Auto: break;
    }
}

QString ContentionResilienceController::levelName() const
{
    return levelLabel(m_level);
}

ContentionResilienceController::Policy ContentionResilienceController::policyForLevel(Level level)
{
    switch (level) {
    case Level::Normal: return {33, 33, 1, true, 1.0};
    case Level::Pressure: return {50, 50, 2, true, 0.5};
    case Level::Severe: return {83, 83, 4, false, 0.0};
    }
    return {};
}

void ContentionResilienceController::observeForTest(int totalCpuPercent, qint64 heartbeatMs,
                                                    qint64 nowMs)
{
    observe(totalCpuPercent, heartbeatMs, nowMs);
}

void ContentionResilienceController::recordLiveGraphRefresh()
{
    ++m_liveGraphRefreshes;
}

void ContentionResilienceController::recordTelemetryPublication()
{
    ++m_telemetryPublications;
}

void ContentionResilienceController::recordAdaptiveHistorySample()
{
    ++m_adaptiveHistorySamples;
}

void ContentionResilienceController::recordBackgroundPoll(const QString &kind)
{
    if (kind == QStringLiteral("controller")) ++m_controllerPolls;
    else if (kind == QStringLiteral("game")) ++m_gamePolls;
}

QVariantMap ContentionResilienceController::evidence() const
{
    const qint64 elapsedMs = std::max<qint64>(1, m_clock.elapsed());
    const auto perSecond = [elapsedMs](quint64 count) {
        return static_cast<double>(count) * 1000.0 / static_cast<double>(elapsedMs);
    };
    return {
        {QStringLiteral("level"), levelName()},
        {QStringLiteral("pressureLevel"), pressureLevel()},
        {QStringLiteral("override"), [this] {
            switch (m_override) {
            case Override::Auto: return QStringLiteral("auto");
            case Override::Normal: return QStringLiteral("normal");
            case Override::Pressure: return QStringLiteral("pressure");
            case Override::Severe: return QStringLiteral("severe");
            }
            return QStringLiteral("auto");
        }()},
        {QStringLiteral("lastTotalCpuPercent"), m_lastCpuPercent},
        {QStringLiteral("minimumTotalCpuPercent"), m_cpuSampleCount ? m_minCpuPercent : -1},
        {QStringLiteral("maximumTotalCpuPercent"), m_cpuSampleCount ? m_maxCpuPercent : -1},
        {QStringLiteral("cpuSampleCount"), QVariant::fromValue(m_cpuSampleCount)},
        {QStringLiteral("observedDurationMs"), elapsedMs},
        {QStringLiteral("levelTransitions"), QVariant::fromValue(m_levelTransitions)},
        {QStringLiteral("transitionTimeline"), m_transitionTimeline},
        {QStringLiteral("telemetryPublications"), QVariant::fromValue(m_telemetryPublications)},
        {QStringLiteral("liveGraphRefreshes"), QVariant::fromValue(m_liveGraphRefreshes)},
        {QStringLiteral("adaptiveHistorySamples"), QVariant::fromValue(m_adaptiveHistorySamples)},
        {QStringLiteral("controllerPolls"), QVariant::fromValue(m_controllerPolls)},
        {QStringLiteral("gamePolls"), QVariant::fromValue(m_gamePolls)},
        {QStringLiteral("telemetryIntervalMs"), m_policy.telemetryIntervalMs},
        {QStringLiteral("liveGraphIntervalMs"), m_policy.liveGraphIntervalMs},
        {QStringLiteral("backgroundPollMultiplier"), m_policy.backgroundPollMultiplier},
        {QStringLiteral("decorativeMotionAllowed"), m_policy.decorativeMotionAllowed},
        {QStringLiteral("nonessentialAnimationScale"), m_policy.nonessentialAnimationScale},
        {QStringLiteral("observedCadencePerSecond"), QVariantMap{
            {QStringLiteral("telemetryPublications"), perSecond(m_telemetryPublications)},
            {QStringLiteral("liveGraphRefreshes"), perSecond(m_liveGraphRefreshes)},
            {QStringLiteral("adaptiveHistorySamples"), perSecond(m_adaptiveHistorySamples)},
            {QStringLiteral("controllerPolls"), perSecond(m_controllerPolls)},
            {QStringLiteral("gamePolls"), perSecond(m_gamePolls)},
        }},
    };
}

void ContentionResilienceController::observe(int totalCpuPercent, qint64 heartbeatMs, qint64 nowMs)
{
    if (totalCpuPercent >= 0) {
        m_lastCpuPercent = totalCpuPercent;
        m_minCpuPercent = std::min(m_minCpuPercent, totalCpuPercent);
        m_maxCpuPercent = std::max(m_maxCpuPercent, totalCpuPercent);
        ++m_cpuSampleCount;
    }
    switch (m_override) {
    case Override::Normal: setLevel(Level::Normal, nowMs); return;
    case Override::Pressure: setLevel(Level::Pressure, nowMs); return;
    case Override::Severe: setLevel(Level::Severe, nowMs); return;
    case Override::Auto: break;
    }

    // A genuinely large GUI heartbeat immediately enters protection. A first
    // major miss enters Pressure; another while already pressured enters
    // Severe. Smaller one-off hitches deliberately do not flap state.
    if (heartbeatMs >= 1000) {
        if (m_level == Level::Normal) setLevel(Level::Pressure, nowMs);
        else if (m_level == Level::Pressure) setLevel(Level::Severe, nowMs);
        return;
    }
    // The production heartbeat itself is 200 ms. Thresholds therefore measure
    // delayed turns beyond that cadence, not every ordinary timer wake-up.
    const bool pressure = totalCpuPercent >= 90 || heartbeatMs >= 320;
    const bool severe = totalCpuPercent >= 97 || heartbeatMs >= 550;
    if (m_level == Level::Normal) {
        m_pressureSamples = pressure ? m_pressureSamples + 1 : 0;
        // At genuine near-saturation, one sampled reading is sufficient to
        // begin protection; ordinary high load still needs two samples.
        if (totalCpuPercent >= 97 || m_pressureSamples >= 2)
            setLevel(Level::Pressure, nowMs);
        return;
    }
    if (m_level == Level::Pressure) {
        m_severeSamples = severe ? m_severeSamples + 1 : 0;
        if (m_severeSamples >= 1) {
            setLevel(Level::Severe, nowMs);
            return;
        }
        const bool recovered = totalCpuPercent >= 0 && totalCpuPercent < 84 && heartbeatMs < 260;
        m_recoverySamples = recovered ? m_recoverySamples + 1 : 0;
        if (m_recoverySamples >= 6) setLevel(Level::Normal, nowMs);
        return;
    }
    const bool recovered = totalCpuPercent >= 0 && totalCpuPercent < 94 && heartbeatMs < 300;
    m_recoverySamples = recovered ? m_recoverySamples + 1 : 0;
    if (m_recoverySamples >= 4) setLevel(Level::Pressure, nowMs);
}

void ContentionResilienceController::setLevel(Level level, qint64 nowMs)
{
    if (m_level == level) return;
    m_level = level;
    m_policy = policyForLevel(level);
    ++m_levelTransitions;
    if (m_transitionTimeline.size() == 64) m_transitionTimeline.removeFirst();
    m_transitionTimeline.append(QVariantMap{{QStringLiteral("atMs"), nowMs},
                                            {QStringLiteral("level"), levelLabel(level)}});
    m_pressureSamples = 0;
    m_severeSamples = 0;
    m_recoverySamples = 0;
    emit policyChanged();
}

int ContentionResilienceController::readTotalCpuPercent()
{
#ifdef Q_OS_WIN
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return -1;
    static quint64 previousIdle = 0;
    static quint64 previousKernel = 0;
    static quint64 previousUser = 0;
    const quint64 idleNow = fileTimeValue(idle);
    const quint64 kernelNow = fileTimeValue(kernel);
    const quint64 userNow = fileTimeValue(user);
    const quint64 totalNow = kernelNow + userNow;
    const quint64 previousTotal = previousKernel + previousUser;
    const quint64 totalDelta = totalNow - previousTotal;
    const quint64 idleDelta = idleNow - previousIdle;
    previousIdle = idleNow;
    previousKernel = kernelNow;
    previousUser = userNow;
    if (previousTotal == 0 || totalDelta == 0) return -1;
    return std::clamp(static_cast<int>(100 - (idleDelta * 100 / totalDelta)), 0, 100);
#else
    return -1;
#endif
}

ContentionResilienceController::Override ContentionResilienceController::overrideFromEnvironment()
{
    const QString value = qEnvironmentVariable("HOTAS_CONTENTION_LEVEL").trimmed().toLower();
    if (value == QStringLiteral("normal")) return Override::Normal;
    if (value == QStringLiteral("pressure")) return Override::Pressure;
    if (value == QStringLiteral("severe")) return Override::Severe;
    return Override::Auto;
}

} // namespace hotas
