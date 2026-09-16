#include "responsiveness_probe.h"

#include <QCoreApplication>
#include <QFile>
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

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
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

    const QString reportPath = probe->exportReport(temporaryDirectory.filePath(QStringLiteral("probe.json")));
    if (reportPath.isEmpty()) return fail("enabled probe did not export a report");
    QFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::ReadOnly)) return fail("probe report could not be opened");
    const QJsonDocument document = QJsonDocument::fromJson(reportFile.readAll());
    const QJsonObject eventLoop = document.object().value(QStringLiteral("eventLoop")).toObject();
    if (eventLoop.value(QStringLiteral("sampleCount")).toInt() != 11
        || eventLoop.value(QStringLiteral("p50Ms")).toDouble() != 101.0
        || eventLoop.value(QStringLiteral("p95Ms")).toDouble() != 20000.0
        || eventLoop.value(QStringLiteral("maximumMs")).toDouble() != 20000.0
        || eventLoop.value(QStringLiteral("over5000Ms")).toInt() != 2) {
        return fail("probe percentile or threshold aggregation was incorrect");
    }
    return 0;
}
