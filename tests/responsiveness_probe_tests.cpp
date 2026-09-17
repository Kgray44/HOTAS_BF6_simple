#include "contention_resilience_controller.h"
#include "responsiveness_probe.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

namespace {

bool hasArgument(int argc, char *argv[], const char *argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) return true;
    }
    return false;
}

int fail(const char *message)
{
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    return 1;
}

bool policyMatches(const hotas::ContentionResilienceController::Policy &policy,
                   int telemetryIntervalMs, int liveGraphIntervalMs,
                   int backgroundPollMultiplier, bool decorativeMotionAllowed,
                   qreal nonessentialAnimationScale)
{
    return policy.telemetryIntervalMs == telemetryIntervalMs
        && policy.liveGraphIntervalMs == liveGraphIntervalMs
        && policy.backgroundPollMultiplier == backgroundPollMultiplier
        && policy.decorativeMotionAllowed == decorativeMotionAllowed
        && qFuzzyCompare(policy.nonessentialAnimationScale, nonessentialAnimationScale);
}

int verifyContentionResilienceController()
{
    qputenv("HOTAS_CONTENTION_LEVEL", "auto");
    hotas::ContentionResilienceController controller;
    using Level = hotas::ContentionResilienceController::Level;

    // Moderate load and a single small hitch retain Normal. Values are
    // injected, so the test never manufactures CPU or disk contention.
    controller.observeForTest(82, 80, 0);
    controller.observeForTest(84, 300, 750);
    if (controller.level() != Level::Normal
        || !policyMatches(controller.policy(), 33, 33, 1, true, 1.0)) {
        return fail("contention controller changed policy for a small hitch");
    }

    hotas::ContentionResilienceController saturatedController;
    saturatedController.observeForTest(99, 60, 0);
    if (saturatedController.level() != Level::Pressure) {
        return fail("contention controller did not promptly enter pressure at saturation");
    }
    saturatedController.observeForTest(99, 60, 750);
    if (saturatedController.level() != Level::Severe) {
        return fail("contention controller did not promptly enter severe at saturation");
    }

    controller.observeForTest(91, 60, 1500);
    controller.observeForTest(92, 60, 2250);
    if (controller.level() != Level::Pressure
        || !policyMatches(controller.policy(), 50, 50, 2, true, 0.5)) {
        return fail("contention controller did not enter pressure with hysteresis");
    }

    controller.observeForTest(98, 70, 3000);
    controller.observeForTest(98, 70, 3750);
    if (controller.level() != Level::Severe
        || !policyMatches(controller.policy(), 83, 83, 4, false, 0.0)) {
        return fail("contention controller did not enter severe policy");
    }

    controller.observeForTest(82, 70, 4500);
    controller.observeForTest(82, 70, 5250);
    controller.observeForTest(82, 70, 6000);
    controller.observeForTest(82, 70, 6750);
    if (controller.level() != Level::Pressure) {
        return fail("contention controller did not recover severe to pressure");
    }
    for (qint64 nowMs = 7500; nowMs <= 11250; nowMs += 750)
        controller.observeForTest(75, 60, nowMs);
    if (controller.level() != Level::Normal) {
        return fail("contention controller did not recover pressure to normal");
    }

    // A major GUI stall immediately enters Pressure rather than waiting for a
    // second sampled CPU interval; a second major miss escalates from there.
    controller.observeForTest(-1, 1100, 12000);
    if (controller.level() != Level::Pressure) {
        return fail("contention controller did not enter pressure for a major stall");
    }
    controller.observeForTest(-1, 1100, 12200);
    if (controller.level() != Level::Severe) {
        return fail("contention controller did not escalate repeated major stalls");
    }
    controller.observeForTest(-1, 1100, 12400);
    if (controller.level() != Level::Severe) {
        return fail("contention controller demoted severe after another major stall");
    }

    qputenv("HOTAS_CONTENTION_LEVEL", "pressure");
    hotas::ContentionResilienceController forcedPressure;
    if (forcedPressure.level() != Level::Pressure
        || forcedPressure.evidence().value("override").toString() != QStringLiteral("pressure")) {
        return fail("contention pressure override was not honored");
    }
    qputenv("HOTAS_CONTENTION_LEVEL", "severe");
    hotas::ContentionResilienceController forcedSevere;
    if (forcedSevere.level() != Level::Severe
        || forcedSevere.evidence().value("override").toString() != QStringLiteral("severe")) {
        return fail("contention severe override was not honored");
    }
    qunsetenv("HOTAS_CONTENTION_LEVEL");
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (const int controllerResult = verifyContentionResilienceController(); controllerResult != 0)
        return controllerResult;
    if (!hasArgument(argc, argv, "--enabled")) {
        qunsetenv("HOTAS_RESPONSIVENESS_PROBE");
        hotas::ResponsivenessProbe::installIfEnabled(&application);
        return hotas::ResponsivenessProbe::active() ? fail("disabled probe unexpectedly installed") : 0;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) return fail("could not create responsiveness probe temp directory");
    qputenv("HOTAS_RESPONSIVENESS_PROBE", "1");
    hotas::ResponsivenessProbe::installIfEnabled(&application);
    auto *probe = hotas::ResponsivenessProbe::active();
    if (!probe) return fail("enabled probe did not install");

    for (const double delay : {0.0, 5.0, 17.0, 34.0, 51.0, 101.0, 251.0, 501.0, 1001.0, 5001.0, 20000.0}) {
        probe->recordEventLoopDelayForTest(delay);
    }
    probe->recordStartupWindowReadyForTest();
    probe->recordFirstPresentedFrameForTest();
    // The first post-presentation heartbeat establishes the runtime baseline;
    // only a subsequent heartbeat may enter steady-state event-loop samples.
    probe->recordEventLoopHeartbeatForTest();
    probe->recordEventLoopHeartbeatForTest();
    probe->beginScrollSession(QStringLiteral("Settings"), QStringLiteral("flightDeckSettings"),
                              QStringLiteral("normal"), QStringLiteral("normal-1320x840"),
                              QStringLiteral("Normal"), 0.0);
    probe->recordScrollWheel();
    probe->recordScrollWheelDisposition(true);
    probe->recordScrollPosition(24.0);
    probe->endScrollSession();

    const QString reportPath = probe->exportReport(temporaryDirectory.filePath(QStringLiteral("probe.json")));
    if (reportPath.isEmpty()) return fail("enabled probe did not export a report");
    QFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::ReadOnly)) return fail("probe report could not be opened");
    const QJsonDocument document = QJsonDocument::fromJson(reportFile.readAll());
    const QJsonObject eventLoop = document.object().value(QStringLiteral("eventLoop")).toObject();
    if (eventLoop.value(QStringLiteral("sampleCount")).toInt() != 12
        || eventLoop.value(QStringLiteral("p50Ms")).toDouble() != 51.0
        || eventLoop.value(QStringLiteral("p95Ms")).toDouble() != 20000.0
        || eventLoop.value(QStringLiteral("maximumMs")).toDouble() != 20000.0
        || eventLoop.value(QStringLiteral("over5000Ms")).toInt() != 2) {
        return fail("probe percentile or threshold aggregation was incorrect");
    }
    const QJsonObject startupReadiness = document.object()
        .value(QStringLiteral("startupReadiness")).toObject();
    if (startupReadiness.value(QStringLiteral("windowReadySinceProbeStartMs")).isNull()
        || startupReadiness.value(QStringLiteral("firstPresentedFrameSinceProbeStartMs")).isNull()
        || startupReadiness.value(QStringLiteral("steadyStateHeartbeatArmedSinceProbeStartMs")).isNull()
        || startupReadiness.value(QStringLiteral("firstHeartbeatBeforeFirstPresentedFrame")).toBool()
        || !startupReadiness.contains(QStringLiteral("windowReadyToFirstPresentedFrameMs"))) {
        return fail("probe did not report separate startup-readiness boundaries");
    }
    const QJsonArray scrollSessions = document.object().value(QStringLiteral("scrollSessions")).toArray();
    if (scrollSessions.size() != 1) return fail("probe did not export one bounded scroll session");
    const QJsonObject scroll = scrollSessions.first().toObject();
    if (scroll.value(QStringLiteral("page")).toString() != QStringLiteral("Settings")
        || scroll.value(QStringLiteral("surfaceId")).toString() != QStringLiteral("flightDeckSettings")
        || scroll.value(QStringLiteral("wheelEvents")).toInt() != 1
        || scroll.value(QStringLiteral("acceptedWheelEvents")).toInt() != 1
        || scroll.value(QStringLiteral("contentPositionChanges")).toInt() != 1
        || scroll.value(QStringLiteral("totalMovement")).toDouble() != 24.0
        || scroll.value(QStringLiteral("movementLatency")).toObject()
               .value(QStringLiteral("sampleCount")).toInt() != 1) {
        return fail("probe scroll-session aggregation was incorrect");
    }
    return 0;
}
