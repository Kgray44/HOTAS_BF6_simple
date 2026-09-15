#include "hidhide_read_only_protocol.h"

#include <QElapsedTimer>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <array>

namespace hotas {
namespace {

#ifdef Q_OS_WIN
constexpr DWORD kProtocolTimeoutMs = 2500;
constexpr DWORD kMaximumProtocolPayload = 1024 * 1024;

constexpr DWORD hidHideGetIoctl(DWORD function)
{
    // CTL_CODE(32769, function, METHOD_BUFFERED, FILE_READ_DATA).  This
    // translation unit intentionally contains only the four documented GETs.
    return (32769u << 16u) | (1u << 14u) | (function << 2u);
}

constexpr DWORD kIoctlGetWhitelist = hidHideGetIoctl(2048);
constexpr DWORD kIoctlGetBlacklist = hidHideGetIoctl(2050);
constexpr DWORD kIoctlGetActive = hidHideGetIoctl(2052);
constexpr DWORD kIoctlGetInverse = hidHideGetIoctl(2054);

HidHideNativeError win32Error(DWORD code, const QString &operation)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString detail = length ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                                  : QStringLiteral("Win32 error %1").arg(code);
    if (buffer) LocalFree(buffer);
    return {QStringLiteral("win32"), code, operation, operation + QStringLiteral(": ") + detail};
}

struct IoctlResult final {
    bool completed = false;
    bool timedOut = false;
    DWORD bytes = 0;
    HidHideNativeError error;
    bool hasError = false;
    qint64 durationMs = 0;
};

IoctlResult readOnlyIoctl(HANDLE device, DWORD operation, void *output, DWORD outputBytes)
{
    QElapsedTimer timer;
    timer.start();
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return {false, false, 0, win32Error(GetLastError(), QStringLiteral("CreateEventW")), true, timer.elapsed()};

    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    DWORD returned = 0;
    const BOOL immediate = DeviceIoControl(device, operation, nullptr, 0, output, outputBytes,
                                           &returned, &overlapped);
    if (immediate) {
        CloseHandle(event);
        return {true, false, returned, {}, false, timer.elapsed()};
    }
    const DWORD failure = GetLastError();
    if (failure != ERROR_IO_PENDING) {
        CloseHandle(event);
        return {false, false, returned, win32Error(failure, QStringLiteral("DeviceIoControl(GET)")),
                true, timer.elapsed()};
    }

    const DWORD wait = WaitForSingleObject(event, kProtocolTimeoutMs);
    if (wait == WAIT_TIMEOUT) {
        CancelIoEx(device, &overlapped);
        WaitForSingleObject(event, 250);
        CloseHandle(event);
        return {false, true, 0, win32Error(ERROR_TIMEOUT, QStringLiteral("DeviceIoControl(GET) timeout")),
                true, timer.elapsed()};
    }
    if (wait != WAIT_OBJECT_0) {
        const HidHideNativeError error = win32Error(GetLastError(), QStringLiteral("WaitForSingleObject(DeviceIoControl)"));
        CloseHandle(event);
        return {false, false, 0, error, true, timer.elapsed()};
    }
    if (!GetOverlappedResult(device, &overlapped, &returned, FALSE)) {
        const HidHideNativeError error = win32Error(GetLastError(), QStringLiteral("GetOverlappedResult(DeviceIoControl)"));
        CloseHandle(event);
        return {false, false, 0, error, true, timer.elapsed()};
    }
    CloseHandle(event);
    return {true, false, returned, {}, false, timer.elapsed()};
}

HidHideReadObservation observationFrom(const QString &id, const QString &operation, const IoctlResult &result)
{
    HidHideReadObservation observation;
    observation.id = id;
    observation.operation = operation;
    observation.durationMs = result.durationMs;
    observation.nativeError = result.error;
    observation.hasNativeError = result.hasError;
    observation.state = result.timedOut ? HidHideReadState::TimedOut
        : result.completed ? HidHideReadState::Pass : HidHideReadState::Failed;
    return observation;
}

HidHideReadObservation boolQueryExact(HANDLE device, DWORD code, const QString &id, const QString &operation)
{
    BYTE value = 0;
    const IoctlResult result = readOnlyIoctl(device, code, &value, sizeof(value));
    HidHideReadObservation observation = observationFrom(id, operation, result);
    if (observation.state != HidHideReadState::Pass) {
        observation.summary = QStringLiteral("The read-only %1 query did not complete.").arg(operation);
        return observation;
    }
    if (result.bytes != sizeof(value)) {
        observation.state = HidHideReadState::Failed;
        observation.hasNativeError = true;
        observation.nativeError = win32Error(ERROR_INVALID_PARAMETER,
                                             QStringLiteral("Unexpected HidHide boolean GET payload size"));
        observation.summary = QStringLiteral("%1 returned an unexpected payload size.").arg(operation);
        return observation;
    }
    observation.value = value ? QStringLiteral("true") : QStringLiteral("false");
    observation.summary = QStringLiteral("%1 returned %2.").arg(operation, observation.value);
    return observation;
}

QList<HidHideReadObservation> multiStringQuery(HANDLE device, DWORD code, const QString &id,
                                                const QString &operation)
{
    QList<HidHideReadObservation> observations;
    const IoctlResult first = readOnlyIoctl(device, code, nullptr, 0);
    HidHideReadObservation size = observationFrom(id + QStringLiteral("_SIZE"), operation + QStringLiteral(" size"), first);
    size.sizeNegotiation = true;
    if (first.completed && first.bytes == 0) {
        size.value = QStringLiteral("0 bytes");
        size.summary = QStringLiteral("%1 contains no entries.").arg(operation);
        observations.append(size);
        HidHideReadObservation payload = size;
        payload.id = id;
        payload.operation = operation;
        payload.sizeNegotiation = false;
        payload.value = QStringLiteral("0 entries");
        observations.append(payload);
        return observations;
    }

    const DWORD firstError = first.hasError ? first.error.code : ERROR_SUCCESS;
    const bool reportsSize = first.completed || firstError == ERROR_INSUFFICIENT_BUFFER || firstError == ERROR_MORE_DATA;
    if (!reportsSize || first.bytes == 0) {
        size.summary = QStringLiteral("%1 did not provide a valid payload size.").arg(operation);
        observations.append(size);
        return observations;
    }
    size.state = HidHideReadState::Pass;
    size.value = QStringLiteral("%1 bytes").arg(first.bytes);
    size.summary = QStringLiteral("%1 payload size read.").arg(operation);
    observations.append(size);
    if (first.bytes > kMaximumProtocolPayload || first.bytes % sizeof(wchar_t) != 0) {
        observations.last().state = HidHideReadState::Failed;
        observations.last().hasNativeError = true;
        observations.last().nativeError = win32Error(ERROR_INVALID_DATA,
            QStringLiteral("Invalid HidHide MULTI_SZ payload size"));
        return observations;
    }

    QVector<wchar_t> payload(static_cast<qsizetype>(first.bytes / sizeof(wchar_t)));
    const IoctlResult second = readOnlyIoctl(device, code, payload.data(), first.bytes);
    HidHideReadObservation value = observationFrom(id, operation, second);
    if (value.state != HidHideReadState::Pass) {
        value.summary = QStringLiteral("The read-only %1 payload query did not complete.").arg(operation);
        observations.append(value);
        return observations;
    }
    if (second.bytes > first.bytes || second.bytes % sizeof(wchar_t) != 0) {
        value.state = HidHideReadState::Failed;
        value.hasNativeError = true;
        value.nativeError = win32Error(ERROR_INVALID_DATA, QStringLiteral("Invalid HidHide MULTI_SZ payload"));
        value.summary = QStringLiteral("%1 returned an invalid payload.").arg(operation);
        observations.append(value);
        return observations;
    }
    const wchar_t *cursor = payload.constData();
    const wchar_t *const end = cursor + second.bytes / sizeof(wchar_t);
    bool terminated = false;
    while (cursor < end) {
        const wchar_t *next = cursor;
        while (next < end && *next != L'\0') ++next;
        if (next == end) break;
        if (next == cursor) { terminated = true; break; }
        value.values.append(QString::fromWCharArray(cursor, static_cast<int>(next - cursor)));
        cursor = next + 1;
    }
    if (!terminated) {
        value.state = HidHideReadState::Failed;
        value.hasNativeError = true;
        value.nativeError = win32Error(ERROR_INVALID_DATA, QStringLiteral("Unterminated HidHide MULTI_SZ payload"));
        value.summary = QStringLiteral("%1 returned an unterminated payload.").arg(operation);
    } else {
        value.value = QStringLiteral("%1 entries").arg(value.values.size());
        value.summary = QStringLiteral("%1 returned %2.").arg(operation, value.value);
    }
    observations.append(value);
    return observations;
}
#endif

} // namespace

QString hidHideReadStateLabel(HidHideReadState state)
{
    switch (state) {
    case HidHideReadState::Pass: return QStringLiteral("PASS");
    case HidHideReadState::Failed: return QStringLiteral("FAIL");
    case HidHideReadState::TimedOut: return QStringLiteral("TIMED OUT");
    case HidHideReadState::PermissionLimited: return QStringLiteral("PERMISSION LIMITED");
    case HidHideReadState::Unavailable: return QStringLiteral("UNAVAILABLE");
    case HidHideReadState::Cancelled: return QStringLiteral("CANCELLED");
    }
    return QStringLiteral("UNKNOWN");
}

QList<HidHideReadObservation> HidHideReadOnlyProtocol::inspect(std::atomic_bool *cancelled)
{
    QList<HidHideReadObservation> observations;
#ifdef Q_OS_WIN
    if (cancelled && cancelled->load()) return observations;
    HANDLE device = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        HidHideReadObservation open;
        open.id = QStringLiteral("HD-API-001");
        open.operation = QStringLiteral("OPEN_CONTROL");
        const DWORD code = GetLastError();
        open.state = code == ERROR_ACCESS_DENIED ? HidHideReadState::PermissionLimited : HidHideReadState::Unavailable;
        open.summary = QStringLiteral("HidHide control endpoint could not be opened for read-only inspection.");
        open.hasNativeError = true;
        open.nativeError = win32Error(code, QStringLiteral("CreateFileW(\\\\.\\HidHide, GENERIC_READ)"));
        observations.append(open);
        return observations;
    }
    HidHideReadObservation open;
    open.id = QStringLiteral("HD-API-001");
    open.operation = QStringLiteral("OPEN_CONTROL");
    open.state = HidHideReadState::Pass;
    open.summary = QStringLiteral("Opened the HidHide control endpoint with read-only access.");
    open.value = QStringLiteral("\\\\.\\HidHide; GENERIC_READ; overlapped");
    observations.append(open);

    const auto close = [&] { CloseHandle(device); };
    if (!cancelled || !cancelled->load()) observations.append(boolQueryExact(device, kIoctlGetActive,
        QStringLiteral("HD-CFG-001"), QStringLiteral("GET_ACTIVE")));
    if (!cancelled || !cancelled->load()) observations.append(boolQueryExact(device, kIoctlGetInverse,
        QStringLiteral("HD-CFG-002"), QStringLiteral("GET_INVERSE")));
    if (!cancelled || !cancelled->load()) observations += multiStringQuery(device, kIoctlGetWhitelist,
        QStringLiteral("HD-CFG-003"), QStringLiteral("GET_WHITELIST"));
    if (!cancelled || !cancelled->load()) observations += multiStringQuery(device, kIoctlGetBlacklist,
        QStringLiteral("HD-CFG-005"), QStringLiteral("GET_BLACKLIST"));
    close();
#else
    Q_UNUSED(cancelled);
    HidHideReadObservation unavailable;
    unavailable.id = QStringLiteral("HD-API-001");
    unavailable.operation = QStringLiteral("OPEN_CONTROL");
    unavailable.state = HidHideReadState::Unavailable;
    unavailable.summary = QStringLiteral("Direct HidHide protocol inspection is available only on Windows.");
    observations.append(unavailable);
#endif
    return observations;
}

} // namespace hotas
