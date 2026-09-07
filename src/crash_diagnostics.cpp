#include "crash_diagnostics.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <atomic>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <exception>
#include <span>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <dbghelp.h>
#endif

namespace hotas {
namespace {

constexpr size_t kTextCapacity = 4096;
constexpr size_t kEventCapacity = 8192;
std::array<wchar_t, 32768> g_root{};
std::array<wchar_t, 32768> g_marker{};
std::array<wchar_t, 32768> g_executable{};
std::array<wchar_t, 32768> g_reporter{};
std::array<wchar_t, 32768> g_reporterDirectory{};
std::array<wchar_t, 32768> g_lastReport{};
std::array<char, 96> g_version{};
std::array<char, 96> g_build{};
std::array<char, 512> g_executableDisplay{};
std::array<char, 192> g_windowsVersion{};
std::array<char, kTextCapacity> g_context{};
std::array<char, kEventCapacity> g_events{};
std::atomic<long> g_reporting{0};
std::atomic<unsigned long> g_reportSequence{0};
std::atomic<bool> g_previousAbnormal{false};

QString sanitizeForReport(QString value)
{
    // A control-plane event may include a user-selected path (for example an
    // import or update handoff). Keep local reports useful without copying a
    // Windows account name into the text shown by the reporter.
    static const QRegularExpression userPath(uR"([A-Za-z]:\\Users\\[^\\\r\n]+)"_qs,
                                             QRegularExpression::CaseInsensitiveOption);
    value.replace(userPath, u"%USERPROFILE%"_qs);
    return value;
}

void copyUtf8(std::span<char> destination, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    const size_t length = std::min(destination.size() - 1, static_cast<size_t>(utf8.size()));
    std::memcpy(destination.data(), utf8.constData(), length);
    destination[length] = '\0';
}

#ifdef Q_OS_WIN
void copyWideUtf8(std::span<char> destination, const wchar_t *value)
{
    if (!value || destination.empty()) return;
    const int capacity = static_cast<int>(destination.size() - 1);
    const int written = WideCharToMultiByte(CP_UTF8, 0, value, -1, destination.data(), capacity,
        nullptr, nullptr);
    destination[written > 0 ? std::min(written, capacity) : 0] = '\0';
}

const wchar_t *fileNamePart(const wchar_t *path)
{
    if (!path) return L"";
    const wchar_t *name = path;
    for (const wchar_t *cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') name = cursor + 1;
    }
    return name;
}

void writeAll(HANDLE file, const char *text)
{
    if (file == INVALID_HANDLE_VALUE || !text) return;
    const DWORD length = static_cast<DWORD>(std::min<size_t>(std::strlen(text), 0xfffffff0));
    DWORD written = 0;
    WriteFile(file, text, length, &written, nullptr);
}

bool createDirectory(const wchar_t *path)
{
    if (!path || !*path) return false;
    return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

void makeReportPath(std::array<wchar_t, 32768> &result)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const unsigned long sequence = g_reportSequence.fetch_add(1);
    std::swprintf(result.data(), result.size(), L"%ls\\%04u-%02u-%02u_%02u-%02u-%02u_%lu_%lu",
        g_root.data(), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        static_cast<unsigned long>(GetCurrentProcessId()), sequence);
}

void launchReporter(const wchar_t *folder)
{
    if (!folder || !*folder || !*g_reporter.data()) return;
    // The handler uses only paths prepared during healthy startup. Do not
    // parse configuration, enumerate files, or traverse the heap here.
    std::array<wchar_t, 65536> command{};
    std::swprintf(command.data(), command.size(), L"\"%ls\" --crash-report \"%ls\"", g_reporter.data(), folder);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(g_reporter.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            g_reporterDirectory.data(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void writeReport(const wchar_t *category, unsigned long code, void *address, EXCEPTION_POINTERS *exception = nullptr,
                 bool startReporter = true)
{
    if (g_reporting.exchange(1) != 0 || !*g_root.data()) return;
    std::array<wchar_t, 32768> folder{};
    makeReportPath(folder);
    if (!createDirectory(folder.data())) return;
    std::wcsncpy(g_lastReport.data(), folder.data(), g_lastReport.size() - 1);
    // Fatal handling cannot assume that the C++ allocator remains healthy.
    // Build the three short paths in fixed storage rather than creating
    // std::wstring instances on the failure path.
    std::array<wchar_t, 32768> jsonPath{};
    std::array<wchar_t, 32768> eventsPath{};
    std::array<wchar_t, 32768> dumpPath{};
    std::swprintf(jsonPath.data(), jsonPath.size(), L"%ls\\crash.json", folder.data());
    std::swprintf(eventsPath.data(), eventsPath.size(), L"%ls\\recent-events.log", folder.data());
    std::swprintf(dumpPath.data(), dumpPath.size(), L"%ls\\HOTAS-BF6.dmp", folder.data());
    bool dumpWritten = false;
    HANDLE dump = CreateFileW(dumpPath.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = exception;
        exceptionInfo.ClientPointers = FALSE;
        // Thread stacks, module list, handles, and indirect memory keep the
        // dump useful without opting into an enormous full-memory capture.
        dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                MiniDumpWithThreadInfo | MiniDumpWithHandleData), exception ? &exceptionInfo : nullptr, nullptr, nullptr) == TRUE;
        CloseHandle(dump);
        if (!dumpWritten) DeleteFileW(dumpPath.data());
    }
    std::array<char, kTextCapacity * 2> escapedContext{};
    std::array<char, 512> faultModule{};
    if (address) {
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address), &module) && module) {
            std::array<wchar_t, 32768> modulePath{};
            if (GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size())) > 0)
                copyWideUtf8(faultModule, fileNamePart(modulePath.data()));
        }
    }
    size_t contextLength = 0;
    for (size_t index = 0; index < g_context.size() && g_context[index] != '\0'
        && contextLength + 2 < escapedContext.size(); ++index) {
        const char value = g_context[index];
        if (value == '"' || value == '\\') escapedContext[contextLength++] = '\\';
        if (value == '\n') { escapedContext[contextLength++] = '\\'; escapedContext[contextLength++] = 'n'; }
        else if (value != '\r') escapedContext[contextLength++] = value;
    }
    char line[12288]{};
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
#ifdef _WIN64
    constexpr const char *architecture = "x64";
#else
    constexpr const char *architecture = "x86";
#endif
    std::snprintf(line, sizeof(line),
        "{\n\"timestampUtc\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\",\n\"version\":\"%s\",\n\"build\":\"%s\",\n\"executablePath\":\"%s\",\n\"windowsVersion\":\"%s\",\n\"qtVersion\":\"%s\",\n\"architecture\":\"%s\",\n\"fatalCategory\":\"%ls\",\n"
        "\"exceptionCode\":\"0x%08lX\",\n\"exceptionAddress\":\"%p\",\n\"faultModule\":\"%s\",\n"
        "\"threadId\":%lu,\n\"minidump\":\"%s\",\n\"minidumpWritten\":%s,\n\"context\":\"%s\"\n}\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
        g_version.data(), g_build.data(), g_executableDisplay.data(), g_windowsVersion.data(),
        QT_VERSION_STR, architecture, category, code, address, faultModule.data(),
        static_cast<unsigned long>(GetCurrentThreadId()), dumpWritten ? "HOTAS-BF6.dmp" : "unavailable",
        dumpWritten ? "true" : "false", escapedContext.data());
    HANDLE json = CreateFileW(jsonPath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool metadataWritten = json != INVALID_HANDLE_VALUE;
    writeAll(json, line);
    if (metadataWritten) CloseHandle(json);
    HANDLE events = CreateFileW(eventsPath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    writeAll(events, g_events.data());
    if (events != INVALID_HANDLE_VALUE) CloseHandle(events);
    // A completed report is an abnormal termination we have already
    // explained. Keep the marker only if a catastrophic failure prevents
    // reporting, so a user-selected restart does not receive a false alarm.
    if (metadataWritten && *g_marker.data()) DeleteFileW(g_marker.data());
    if (startReporter) launchReporter(folder.data());
}
#endif
} // namespace

void CrashDiagnostics::initialize(const QString &executablePath, const QString &version, const QString &buildId)
{
#ifdef Q_OS_WIN
    QString applicationData;
    // The mapper retains its historical QSettings identity for upgrades, but
    // crash artifacts are product-facing files. Put them in the stable,
    // discoverable %LOCALAPPDATA%\HOTAS BF6 location instead of inheriting
    // the old settings directory name. Test mode remains isolated.
    if (!QStandardPaths::isTestModeEnabled()) {
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
            applicationData = QString::fromWCharArray(localAppData) + u"/HOTAS BF6"_qs;
            CoTaskMemFree(localAppData);
        }
    }
    if (applicationData.isEmpty()) applicationData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString base = applicationData + u"/Crash Reports"_qs;
    const QString marker = applicationData + u"/running.marker"_qs;
    QDir().mkpath(base);
    const std::wstring basePath = QDir::toNativeSeparators(base).toStdWString();
    const std::wstring markerPath = QDir::toNativeSeparators(marker).toStdWString();
    std::wcsncpy(g_root.data(), basePath.c_str(), g_root.size() - 1);
    std::wcsncpy(g_marker.data(), markerPath.c_str(), g_marker.size() - 1);
    const QFileInfo executable(executablePath);
    const std::wstring executableNative = QDir::toNativeSeparators(executable.absoluteFilePath()).toStdWString();
    const std::wstring reporterNative = QDir::toNativeSeparators(
        executable.dir().absoluteFilePath(u"HOTAS BF6 Launcher.exe"_qs)).toStdWString();
    const std::wstring reporterDirectoryNative = QDir::toNativeSeparators(executable.absolutePath()).toStdWString();
    std::wcsncpy(g_executable.data(), executableNative.c_str(), g_executable.size() - 1);
    std::wcsncpy(g_reporter.data(), reporterNative.c_str(), g_reporter.size() - 1);
    std::wcsncpy(g_reporterDirectory.data(), reporterDirectoryNative.c_str(), g_reporterDirectory.size() - 1);
    copyUtf8(g_version, version);
    copyUtf8(g_build, buildId);
    copyUtf8(g_executableDisplay, sanitizeForReport(executable.absoluteFilePath()));
    copyUtf8(g_windowsVersion, QSysInfo::prettyProductName());
    g_previousAbnormal.store(GetFileAttributesW(g_marker.data()) != INVALID_FILE_ATTRIBUTES);
    HANDLE running = CreateFileW(g_marker.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (running != INVALID_HANDLE_VALUE) {
        const std::string state = "pid=" + std::to_string(GetCurrentProcessId());
        writeAll(running, state.c_str());
        CloseHandle(running);
    }
    // Retention is intentionally normal-startup work, not crash-path work.
    const QFileInfoList reports = QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Time | QDir::Reversed);
    for (qsizetype index = 16; index < reports.size(); ++index) {
        QDir(reports.at(index).absoluteFilePath()).removeRecursively();
    }
    SetUnhandledExceptionFilter(&CrashDiagnostics::unhandledExceptionFilter);
#else
    Q_UNUSED(executablePath); Q_UNUSED(version); Q_UNUSED(buildId);
#endif
}

void CrashDiagnostics::recordControlPlaneEvent(const QString &event, const QString &context)
{
    copyUtf8(g_context, sanitizeForReport(context));
    if (event.trimmed().isEmpty()) return;
    const QByteArray entry = sanitizeForReport(event).toUtf8() + '\n';
    const size_t bytes = std::min(static_cast<size_t>(entry.size()), g_events.size() - 1);
    size_t existing = 0;
    while (existing < g_events.size() && g_events[existing] != '\0') ++existing;
    if (existing + bytes >= g_events.size()) {
        const size_t keep = g_events.size() - bytes - 1;
        std::memmove(g_events.data(), g_events.data() + existing - keep, keep);
        g_events[keep] = '\0';
    }
    std::strncat(g_events.data(), entry.constData(), bytes);
}

void CrashDiagnostics::recordQtFatal(const QString &message)
{
#ifdef Q_OS_WIN
    recordControlPlaneEvent(u"Qt fatal: "_qs + message, QString::fromUtf8(g_context.data()));
    writeReport(L"Qt fatal", 0, nullptr);
#else
    Q_UNUSED(message);
#endif
}

void CrashDiagnostics::recordTerminate()
{
#ifdef Q_OS_WIN
    // This is diagnostic classification only; termination always remains
    // terminal. It avoids pretending that a corrupted process can recover.
    writeReport(std::current_exception() ? L"Uncaught C++ exception" : L"Explicit C++ terminate", 0, nullptr);
#endif
}

void CrashDiagnostics::markCleanShutdown()
{
#ifdef Q_OS_WIN
    if (*g_marker.data()) DeleteFileW(g_marker.data());
#endif
}

bool CrashDiagnostics::previousRunWasAbnormal() { return g_previousAbnormal.load(); }
QString CrashDiagnostics::crashReportsDirectory() { return QString::fromWCharArray(g_root.data()); }

QString CrashDiagnostics::writeControlledReportForTest()
{
#ifdef Q_OS_WIN
    g_reporting.store(0);
    writeReport(L"Controlled development test", 0xE0424F53UL, nullptr, nullptr, false);
    return QString::fromWCharArray(g_lastReport.data());
#else
    return {};
#endif
}

#ifdef Q_OS_WIN
long WINAPI CrashDiagnostics::unhandledExceptionFilter(_EXCEPTION_POINTERS *exception)
{
    const auto *record = exception ? exception->ExceptionRecord : nullptr;
    writeReport(L"Windows structured exception", record ? record->ExceptionCode : 0,
        record ? record->ExceptionAddress : nullptr, exception);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace hotas
