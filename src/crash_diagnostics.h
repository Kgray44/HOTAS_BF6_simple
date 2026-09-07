#pragma once

#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace hotas {

// CrashDiagnostics owns only bounded control-plane snapshots. It is never
// called from DirectInput report processing or MappingWorker::processReport.
class CrashDiagnostics final {
public:
    static void initialize(const QString &executablePath, const QString &version, const QString &buildId);
    static void recordControlPlaneEvent(const QString &event, const QString &context);
    static void recordQtFatal(const QString &message);
    static void recordTerminate();
    static void markCleanShutdown();
    static bool previousRunWasAbnormal();
    static QString crashReportsDirectory();
    // Test-only seam; no QML invokable or production UI path exposes it.
    static QString writeControlledReportForTest();

#ifdef Q_OS_WIN
    static long WINAPI unhandledExceptionFilter(_EXCEPTION_POINTERS *exception);
#endif
};

} // namespace hotas
