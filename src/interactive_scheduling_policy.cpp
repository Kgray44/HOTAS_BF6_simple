#include "interactive_scheduling_policy.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <atomic>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotas {
namespace {

std::atomic<InteractiveSchedulingPolicy *> g_activePolicy{nullptr};
constexpr int kMaximumStallRecords = 64;

QString modeName(InteractiveSchedulingPolicy::Mode mode)
{
    switch (mode) {
    case InteractiveSchedulingPolicy::Mode::Current: return QStringLiteral("current");
    case InteractiveSchedulingPolicy::Mode::Gui: return QStringLiteral("gui");
    case InteractiveSchedulingPolicy::Mode::Render: return QStringLiteral("render");
    case InteractiveSchedulingPolicy::Mode::GuiAndRender: return QStringLiteral("gui-render");
    case InteractiveSchedulingPolicy::Mode::ProcessAboveNormal: return QStringLiteral("process-above-normal");
    }
    return QStringLiteral("current");
}

InteractiveSchedulingPolicy::Mode modeFromEnvironment()
{
    const QString value = qEnvironmentVariable("HOTAS_RESPONSIVENESS_SCHEDULING_POLICY")
                              .trimmed().toLower();
    if (value == QStringLiteral("gui")) return InteractiveSchedulingPolicy::Mode::Gui;
    if (value == QStringLiteral("render")) return InteractiveSchedulingPolicy::Mode::Render;
    if (value == QStringLiteral("gui-render")) return InteractiveSchedulingPolicy::Mode::GuiAndRender;
    if (value == QStringLiteral("process-above-normal"))
        return InteractiveSchedulingPolicy::Mode::ProcessAboveNormal;
    return InteractiveSchedulingPolicy::Mode::Current;
}

bool boostsGui(InteractiveSchedulingPolicy::Mode mode)
{
    return mode == InteractiveSchedulingPolicy::Mode::Gui
        || mode == InteractiveSchedulingPolicy::Mode::GuiAndRender;
}

bool boostsRender(InteractiveSchedulingPolicy::Mode mode)
{
    return mode == InteractiveSchedulingPolicy::Mode::Render
        || mode == InteractiveSchedulingPolicy::Mode::GuiAndRender;
}

} // namespace

struct InteractiveSchedulingPolicy::State {
    struct CpuTimes {
        double userMs = 0.0;
        double kernelMs = 0.0;
        bool valid = false;
    };

    struct ThreadState {
        quintptr handle = 0;
        quint32 id = 0;
        int initialPriority = 0;
        int effectivePriority = 0;
        quint32 lastError = 0;
        bool observed = false;
        bool policyApplied = false;
        CpuTimes latestCpu;
    };

    struct Stall {
        double wallMs = 0.0;
        CpuTimes guiCpu;
        CpuTimes renderCpu;
    };

    mutable QMutex mutex;
    QHash<QString, ThreadState> threads;
    QVector<Stall> stalls;
    CpuTimes lastGuiHeartbeatCpu;
    CpuTimes lastRenderHeartbeatCpu;
    int initialProcessPriorityClass = 0;
    int effectiveProcessPriorityClass = 0;
    quint32 processPolicyError = 0;
    bool processPolicyApplied = false;
};

#ifdef Q_OS_WIN
namespace {

InteractiveSchedulingPolicy::State::CpuTimes threadCpuTimes(HANDLE thread)
{
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!thread || !GetThreadTimes(thread, &creation, &exit, &kernel, &user)) return {};
    const auto milliseconds = [](const FILETIME &time) {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<double>(value.QuadPart) / 10'000.0;
    };
    return {milliseconds(user), milliseconds(kernel), true};
}

HANDLE openCurrentThreadForObservation()
{
    return OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
}

InteractiveSchedulingPolicy::State::CpuTimes delta(
    const InteractiveSchedulingPolicy::State::CpuTimes &before,
    const InteractiveSchedulingPolicy::State::CpuTimes &after)
{
    if (!before.valid || !after.valid) return {};
    return {std::max(0.0, after.userMs - before.userMs),
            std::max(0.0, after.kernelMs - before.kernelMs), true};
}

} // namespace
#endif

InteractiveSchedulingPolicy::InteractiveSchedulingPolicy(QObject *parent, Mode mode, bool captureEvidence)
    : QObject(parent)
    , m_mode(mode)
    , m_captureEvidence(captureEvidence)
    , m_state(std::make_unique<State>())
{
#ifdef Q_OS_WIN
    m_state->initialProcessPriorityClass = static_cast<int>(GetPriorityClass(GetCurrentProcess()));
    m_state->effectiveProcessPriorityClass = m_state->initialProcessPriorityClass;
#endif
}

InteractiveSchedulingPolicy::~InteractiveSchedulingPolicy()
{
#ifdef Q_OS_WIN
    QMutexLocker locker(&m_state->mutex);
    for (auto it = m_state->threads.begin(); it != m_state->threads.end(); ++it) {
        if (it->handle) CloseHandle(reinterpret_cast<HANDLE>(it->handle));
    }
#endif
    InteractiveSchedulingPolicy *expected = this;
    g_activePolicy.compare_exchange_strong(expected, nullptr, std::memory_order_release,
                                            std::memory_order_acquire);
}

void InteractiveSchedulingPolicy::installProduction(QObject *parent)
{
    if (g_activePolicy.load(std::memory_order_acquire)) return;
    auto *policy = new InteractiveSchedulingPolicy(parent, Mode::Gui, false);
    InteractiveSchedulingPolicy *expected = nullptr;
    if (!g_activePolicy.compare_exchange_strong(expected, policy, std::memory_order_release,
                                                std::memory_order_acquire)) {
        delete policy;
        return;
    }
    policy->applyGuiThreadPolicy();
}

void InteractiveSchedulingPolicy::installForQualification(QObject *parent)
{
    if (g_activePolicy.load(std::memory_order_acquire)) return;
    auto *policy = new InteractiveSchedulingPolicy(parent, modeFromEnvironment(), true);
    InteractiveSchedulingPolicy *expected = nullptr;
    if (!g_activePolicy.compare_exchange_strong(expected, policy, std::memory_order_release,
                                                std::memory_order_acquire)) {
        delete policy;
        return;
    }
    policy->applyGuiThreadPolicy();
}

InteractiveSchedulingPolicy *InteractiveSchedulingPolicy::active()
{
    return g_activePolicy.load(std::memory_order_acquire);
}

void InteractiveSchedulingPolicy::attachWindow(QQuickWindow *window)
{
    if (auto *policy = active()) policy->attach(window);
}

void InteractiveSchedulingPolicy::recordCurrentThread(const char *role)
{
    if (!role) return;
    if (auto *policy = active(); policy && policy->m_captureEvidence)
        policy->recordCurrentThreadImpl(QString::fromLatin1(role));
}

void InteractiveSchedulingPolicy::recordGuiHeartbeat(double wallStallMs)
{
    if (auto *policy = active(); policy && policy->m_captureEvidence)
        policy->recordGuiHeartbeatImpl(wallStallMs);
}

QJsonObject InteractiveSchedulingPolicy::evidence()
{
    if (auto *policy = active(); policy && policy->m_captureEvidence) return policy->evidenceImpl();
    return QJsonObject{{QStringLiteral("enabled"), false},
                       {QStringLiteral("reason"), QStringLiteral("scheduler qualification was not requested")}};
}

void InteractiveSchedulingPolicy::applyGuiThreadPolicy()
{
    recordCurrentThreadImpl(QStringLiteral("gui"));
#ifdef Q_OS_WIN
    if (m_mode == Mode::ProcessAboveNormal) {
        if (SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
            QMutexLocker locker(&m_state->mutex);
            m_state->processPolicyApplied = true;
            m_state->effectiveProcessPriorityClass = static_cast<int>(GetPriorityClass(GetCurrentProcess()));
        } else {
            const quint32 error = GetLastError();
            QMutexLocker locker(&m_state->mutex);
            m_state->processPolicyError = error;
        }
    }
    if (boostsGui(m_mode)) {
        const bool applied = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
        const quint32 error = applied ? 0 : GetLastError();
        QMutexLocker locker(&m_state->mutex);
        auto &state = m_state->threads[QStringLiteral("gui")];
        state.policyApplied = applied;
        if (!applied) state.lastError = error;
    }
#endif
    recordCurrentThreadImpl(QStringLiteral("gui"));
}

void InteractiveSchedulingPolicy::applyRenderThreadPolicy()
{
    recordCurrentThreadImpl(QStringLiteral("render"));
#ifdef Q_OS_WIN
    if (boostsRender(m_mode)) {
        const bool applied = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
        const quint32 error = applied ? 0 : GetLastError();
        QMutexLocker locker(&m_state->mutex);
        auto &state = m_state->threads[QStringLiteral("render")];
        state.policyApplied = applied;
        if (!applied) state.lastError = error;
    }
#endif
    recordCurrentThreadImpl(QStringLiteral("render"));
}

void InteractiveSchedulingPolicy::attach(QQuickWindow *window)
{
    if (!window || !m_captureEvidence) return;
    connect(window, &QQuickWindow::sceneGraphInitialized, this,
            [this] { applyRenderThreadPolicy(); }, Qt::DirectConnection);
}

void InteractiveSchedulingPolicy::recordCurrentThreadImpl(const QString &role)
{
#ifdef Q_OS_WIN
    const quint32 id = GetCurrentThreadId();
    HANDLE current = openCurrentThreadForObservation();
    const int priority = GetThreadPriority(GetCurrentThread());
    const State::CpuTimes cpu = threadCpuTimes(GetCurrentThread());
    QMutexLocker locker(&m_state->mutex);
    auto &state = m_state->threads[role];
    if (!state.observed) {
        state.initialPriority = priority;
        state.observed = true;
    }
    state.effectivePriority = priority;
    state.id = id;
    state.latestCpu = cpu;
    if (current && state.handle != reinterpret_cast<quintptr>(current)) {
        if (state.handle) CloseHandle(reinterpret_cast<HANDLE>(state.handle));
        state.handle = reinterpret_cast<quintptr>(current);
    } else if (current) {
        CloseHandle(current);
    }
#else
    Q_UNUSED(role)
#endif
}

void InteractiveSchedulingPolicy::recordGuiHeartbeatImpl(double wallStallMs)
{
#ifdef Q_OS_WIN
    const State::CpuTimes guiNow = threadCpuTimes(GetCurrentThread());
    QMutexLocker locker(&m_state->mutex);
    auto &gui = m_state->threads[QStringLiteral("gui")];
    gui.latestCpu = guiNow;
    State::CpuTimes renderNow;
    if (const auto it = m_state->threads.constFind(QStringLiteral("render"));
        it != m_state->threads.cend() && it->handle) {
        renderNow = threadCpuTimes(reinterpret_cast<HANDLE>(it->handle));
    }
    if (wallStallMs >= 250.0 && m_state->lastGuiHeartbeatCpu.valid) {
        const State::Stall stall{wallStallMs, delta(m_state->lastGuiHeartbeatCpu, guiNow),
                                 delta(m_state->lastRenderHeartbeatCpu, renderNow)};
        if (m_state->stalls.size() < kMaximumStallRecords) {
            m_state->stalls.append(stall);
        } else {
            auto smallest = std::min_element(m_state->stalls.begin(), m_state->stalls.end(),
                [](const State::Stall &left, const State::Stall &right) {
                    return left.wallMs < right.wallMs;
                });
            if (smallest != m_state->stalls.end() && smallest->wallMs < stall.wallMs)
                *smallest = stall;
        }
    }
    m_state->lastGuiHeartbeatCpu = guiNow;
    m_state->lastRenderHeartbeatCpu = renderNow;
#else
    Q_UNUSED(wallStallMs)
#endif
}

QJsonObject InteractiveSchedulingPolicy::evidenceImpl() const
{
    QJsonObject result{{QStringLiteral("enabled"), true},
                       {QStringLiteral("requestedPolicy"), modeName(m_mode)}};
#ifdef Q_OS_WIN
    QMutexLocker locker(&m_state->mutex);
    result.insert(QStringLiteral("platform"), QStringLiteral("windows"));
    result.insert(QStringLiteral("process"), QJsonObject{
        {QStringLiteral("initialPriorityClass"), m_state->initialProcessPriorityClass},
        {QStringLiteral("effectivePriorityClass"), m_state->effectiveProcessPriorityClass},
        {QStringLiteral("policyApplied"), m_state->processPolicyApplied},
        {QStringLiteral("policyError"), static_cast<qint64>(m_state->processPolicyError)}});
    QJsonArray threads;
    const QStringList roles{QStringLiteral("gui"), QStringLiteral("render"),
                            QStringLiteral("mapping"), QStringLiteral("persistence"),
                            QStringLiteral("hidhide-background")};
    for (const QString &role : roles) {
        const auto it = m_state->threads.constFind(role);
        if (it == m_state->threads.cend() || !it->observed) {
            threads.append(QJsonObject{{QStringLiteral("role"), role},
                                       {QStringLiteral("observed"), false},
                                       {QStringLiteral("status"), QStringLiteral("not-started-in-this-run")}});
            continue;
        }
        const State::ThreadState &thread = *it;
        threads.append(QJsonObject{{QStringLiteral("role"), role},
                                   {QStringLiteral("observed"), true},
                                   {QStringLiteral("threadId"), static_cast<qint64>(thread.id)},
                                   {QStringLiteral("initialEffectivePriority"), thread.initialPriority},
                                   {QStringLiteral("effectivePriority"), thread.effectivePriority},
                                   {QStringLiteral("policyApplied"), thread.policyApplied},
                                   {QStringLiteral("policyError"), static_cast<qint64>(thread.lastError)},
                                   {QStringLiteral("userCpuMs"), thread.latestCpu.userMs},
                                   {QStringLiteral("kernelCpuMs"), thread.latestCpu.kernelMs}});
    }
    result.insert(QStringLiteral("threads"), threads);
    QJsonArray stalls;
    QVector<State::Stall> ordered = m_state->stalls;
    std::sort(ordered.begin(), ordered.end(), [](const State::Stall &left, const State::Stall &right) {
        return left.wallMs > right.wallMs;
    });
    for (const State::Stall &stall : ordered) {
        stalls.append(QJsonObject{{QStringLiteral("wallStallMs"), stall.wallMs},
                                  {QStringLiteral("guiUserCpuDeltaMs"), stall.guiCpu.userMs},
                                  {QStringLiteral("guiKernelCpuDeltaMs"), stall.guiCpu.kernelMs},
                                  {QStringLiteral("guiCpuAvailable"), stall.guiCpu.valid},
                                  {QStringLiteral("renderUserCpuDeltaMs"), stall.renderCpu.userMs},
                                  {QStringLiteral("renderKernelCpuDeltaMs"), stall.renderCpu.kernelMs},
                                  {QStringLiteral("renderCpuAvailable"), stall.renderCpu.valid}});
    }
    result.insert(QStringLiteral("majorGuiHeartbeatStalls"), stalls);
#else
    result.insert(QStringLiteral("platform"), QStringLiteral("unsupported"));
#endif
    return result;
}

} // namespace hotas
