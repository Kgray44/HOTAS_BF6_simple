#include "crash_diagnostics.h"
#include "crash_reporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cstdio>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::fprintf(stderr, "crash_diagnostics_tests: %s\n", message);
    return false;
}
}

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("HOTAS Mapper Crash Tests"));
    application.setApplicationName(QStringLiteral("HOTAS BF6"));
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir(root).removeRecursively();

    hotas::CrashDiagnostics::initialize(QCoreApplication::applicationFilePath(), QStringLiteral("2.4.0"), QStringLiteral("test-build"));
    if (!require(!hotas::CrashDiagnostics::previousRunWasAbnormal(), "fresh session was marked abnormal")) return 1;
    hotas::CrashDiagnostics::recordControlPlaneEvent(QStringLiteral("Devices page opened"),
        QStringLiteral("page=Devices\ntheme=Day Ops\nactiveRig=Test Rig\neditingScope=All Devices\npath=C:\\Users\\Test Pilot\\Documents"));
    for (int event = 0; event < 900; ++event) {
        hotas::CrashDiagnostics::recordControlPlaneEvent(QStringLiteral("Devices refresh %1").arg(event),
            QStringLiteral("page=Devices\ntheme=Day Ops\nactiveRig=Test Rig"));
    }
    hotas::CrashDiagnostics::recordControlPlaneEvent(QStringLiteral("Opened C:\\Users\\Test Pilot\\Documents\\profile.hbf6profile"),
        QStringLiteral("page=Devices\ntheme=Day Ops\npath=C:\\Users\\Test Pilot\\Documents"));
    const QString report = hotas::CrashDiagnostics::writeControlledReportForTest();
    if (!require(!report.isEmpty() && QFile::exists(report + QStringLiteral("/crash.json")), "crash metadata was not written")) return 1;
    if (!require(hotas::launcher::canOpenCrashReport(report.toStdWString()), "reporter rejected a valid crash report")) return 1;
    if (!require(!hotas::launcher::canOpenCrashReport((QDir(root).filePath(QStringLiteral("missing-report"))).toStdWString()),
                 "reporter accepted a missing crash report")) return 1;
    const QString malformed = QDir(root).filePath(QStringLiteral("malformed-report"));
    QDir().mkpath(malformed);
    QFile malformedMetadata(malformed + QStringLiteral("/crash.json"));
    if (!require(malformedMetadata.open(QIODevice::WriteOnly)
                 && malformedMetadata.write("{\"version\":\"2.4.0\"}") > 0,
                 "malformed reporter fixture could not be written")) return 1;
    malformedMetadata.close();
    if (!require(!hotas::launcher::canOpenCrashReport(malformed.toStdWString()),
                 "reporter accepted a malformed crash report")) return 1;
    if (!require(QFile::exists(report + QStringLiteral("/recent-events.log")), "recent event history was not written")) return 1;
    QFile metadata(report + QStringLiteral("/crash.json"));
    if (!require(metadata.open(QIODevice::ReadOnly), "crash metadata could not be read")) return 1;
    const QByteArray content = metadata.readAll();
    if (!require(content.contains("Controlled development test") && content.contains("theme=Day Ops")
                 && content.contains("windowsVersion") && content.contains("executablePath")
                 && content.contains("%USERPROFILE%") && !content.contains("Test Pilot"),
                 "crash metadata lost context or leaked a user path")) return 1;
    if (!require(QFile::exists(report + QStringLiteral("/HOTAS-BF6.dmp")), "controlled crash minidump was not written")) return 1;
    QFile events(report + QStringLiteral("/recent-events.log"));
    if (!require(events.open(QIODevice::ReadOnly), "recent event history could not be read")) return 1;
    const QByteArray eventHistory = events.readAll();
    if (!require(eventHistory.size() < 8192 && eventHistory.contains("Devices refresh 899")
                 && !eventHistory.contains("Test Pilot"), "event ring was not bounded, ordered, or sanitized")) return 1;

    hotas::CrashDiagnostics::markCleanShutdown();
    hotas::CrashDiagnostics::initialize(QCoreApplication::applicationFilePath(), QStringLiteral("2.4.0"), QStringLiteral("test-build"));
    if (!require(!hotas::CrashDiagnostics::previousRunWasAbnormal(), "clean marker was treated as abnormal")) return 1;
    QFile staleMarker(root + QStringLiteral("/running.marker"));
    if (!require(staleMarker.open(QIODevice::WriteOnly), "stale marker could not be created")) return 1;
    staleMarker.write("abnormal-test");
    staleMarker.close();
    hotas::CrashDiagnostics::initialize(QCoreApplication::applicationFilePath(), QStringLiteral("2.4.0"), QStringLiteral("test-build"));
    if (!require(hotas::CrashDiagnostics::previousRunWasAbnormal(), "abnormal marker was not detected")) return 1;
    hotas::CrashDiagnostics::markCleanShutdown();

    // Retention runs only at normal startup. Produce more than the retained
    // cap through the test-only seam, then reinitialize and verify that it
    // retains a bounded number of independent report folders.
    for (int index = 0; index < 18; ++index) {
        hotas::CrashDiagnostics::recordControlPlaneEvent(QStringLiteral("Retention report %1").arg(index),
            QStringLiteral("page=Diagnostics"));
        if (!require(!hotas::CrashDiagnostics::writeControlledReportForTest().isEmpty(), "retention report was not written")) return 1;
    }
    hotas::CrashDiagnostics::markCleanShutdown();
    hotas::CrashDiagnostics::initialize(QCoreApplication::applicationFilePath(), QStringLiteral("2.4.0"), QStringLiteral("test-build"));
    const QDir reportRoot(hotas::CrashDiagnostics::crashReportsDirectory());
    if (!require(reportRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() <= 16,
                 "crash-report retention exceeded its cap")) return 1;
    hotas::CrashDiagnostics::markCleanShutdown();
    return 0;
}
