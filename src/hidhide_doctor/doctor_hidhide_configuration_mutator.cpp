#include "doctor_hidhide_configuration_mutator.h"

#include "doctor_repair_helper_protocol.h"

#include <QDateTime>
#include <QScopeGuard>
#include <QVector>

#include <windows.h>

namespace hotas::doctor {
namespace {

constexpr DWORD kMaximumPayload = 1024 * 1024;
constexpr DWORD ioctl(DWORD function)
{
    // Upstream documents the same FILE_READ_DATA access flag for all
    // 2048–2055 GET/SET functions.  The separate helper opens the control
    // device only after authentication and explicit owner authorization.
    return (32769u << 16u) | (1u << 14u) | (function << 2u);
}
constexpr DWORD kGetWhitelist = ioctl(2048);
constexpr DWORD kSetWhitelist = ioctl(2049);
constexpr DWORD kGetBlacklist = ioctl(2050);
constexpr DWORD kSetBlacklist = ioctl(2051);
constexpr DWORD kGetActive = ioctl(2052);
constexpr DWORD kSetActive = ioctl(2053);
constexpr DWORD kGetInverse = ioctl(2054);
constexpr DWORD kSetInverse = ioctl(2055);

NativeError failure(DWORD code, const QString &operation)
{
    return {NativeErrorDomain::Win32, static_cast<qint64>(code), QStringLiteral("WIN32_%1").arg(code), operation};
}

HANDLE openControl(NativeError *error)
{
    HANDLE control = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (control == INVALID_HANDLE_VALUE && error) *error = failure(GetLastError(), QStringLiteral("CreateFileW(\\\\.\\HidHide)"));
    return control;
}

bool readBoolean(HANDLE control, DWORD code, std::optional<bool> *value, NativeError *error)
{
    BOOLEAN result = FALSE;
    DWORD bytes = 0;
    if (!DeviceIoControl(control, code, nullptr, 0, &result, sizeof(result), &bytes, nullptr) || bytes != sizeof(result)) {
        if (error) *error = failure(GetLastError(), QStringLiteral("HidHide boolean GET"));
        return false;
    }
    *value = result != FALSE;
    return true;
}

bool readMultiString(HANDLE control, DWORD code, QStringList *values, NativeError *error)
{
    DWORD bytes = 0;
    const BOOL first = DeviceIoControl(control, code, nullptr, 0, nullptr, 0, &bytes, nullptr);
    const DWORD firstError = first ? ERROR_SUCCESS : GetLastError();
    if (first && bytes == 0) { values->clear(); return true; }
    if ((firstError != ERROR_INSUFFICIENT_BUFFER && firstError != ERROR_MORE_DATA && !first) || bytes == 0
        || bytes > kMaximumPayload || bytes % sizeof(wchar_t) != 0) {
        if (error) *error = failure(firstError, QStringLiteral("HidHide MULTI_SZ size GET"));
        return false;
    }
    QVector<wchar_t> payload(static_cast<qsizetype>(bytes / sizeof(wchar_t)));
    DWORD actual = 0;
    if (!DeviceIoControl(control, code, nullptr, 0, payload.data(), bytes, &actual, nullptr) || actual > bytes || actual % sizeof(wchar_t) != 0) {
        if (error) *error = failure(GetLastError(), QStringLiteral("HidHide MULTI_SZ payload GET"));
        return false;
    }
    values->clear();
    const wchar_t *cursor = payload.constData();
    const wchar_t *end = cursor + actual / sizeof(wchar_t);
    while (cursor < end) {
        const wchar_t *next = cursor;
        while (next < end && *next != L'\0') ++next;
        if (next == end) { if (error) *error = failure(ERROR_INVALID_DATA, QStringLiteral("Unterminated HidHide MULTI_SZ")); return false; }
        if (next == cursor) return true;
        values->append(QString::fromWCharArray(cursor, static_cast<int>(next - cursor)));
        cursor = next + 1;
    }
    if (error) *error = failure(ERROR_INVALID_DATA, QStringLiteral("Missing HidHide MULTI_SZ terminator"));
    return false;
}

bool writeMultiString(HANDLE control, DWORD code, const QStringList &values, NativeError *error)
{
    QString packed;
    for (const QString &value : values) packed += value + QChar::Null;
    packed += QChar::Null;
    const DWORD bytes = static_cast<DWORD>(packed.size() * sizeof(QChar));
    DWORD ignored = 0;
    if (bytes == 0 || !DeviceIoControl(control, code, const_cast<QChar *>(packed.constData()), bytes, nullptr, 0, &ignored, nullptr)) {
        if (error) *error = failure(GetLastError(), QStringLiteral("HidHide MULTI_SZ SET"));
        return false;
    }
    return true;
}

bool writeBoolean(HANDLE control, DWORD code, const QString &requested, NativeError *error)
{
    const bool enabled = requested.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    BOOLEAN value = enabled ? TRUE : FALSE;
    DWORD ignored = 0;
    if (!DeviceIoControl(control, code, &value, sizeof(value), nullptr, 0, &ignored, nullptr)) {
        if (error) *error = failure(GetLastError(), QStringLiteral("HidHide boolean SET"));
        return false;
    }
    return true;
}

} // namespace

HidHideConfigurationMutator::HidHideConfigurationMutator(QString provider, QString providerVersion)
    : m_provider(std::move(provider)), m_providerVersion(std::move(providerVersion)) {}

HidHideConfigurationSnapshot HidHideConfigurationMutator::readConfiguration()
{
    HidHideConfigurationSnapshot snapshot;
    snapshot.provider = m_provider;
    snapshot.providerVersion = m_providerVersion;
    snapshot.observedAt = QDateTime::currentDateTimeUtc();
    NativeError error;
    HANDLE control = openControl(&error);
    if (control == INVALID_HANDLE_VALUE) return snapshot;
    const auto close = qScopeGuard([&] { CloseHandle(control); });
    snapshot.whitelistKnown = readMultiString(control, kGetWhitelist, &snapshot.whitelist, &error);
    snapshot.blacklistKnown = readMultiString(control, kGetBlacklist, &snapshot.blacklist, &error);
    readBoolean(control, kGetActive, &snapshot.active, &error);
    readBoolean(control, kGetInverse, &snapshot.inverse, &error);
    return snapshot;
}

bool HidHideConfigurationMutator::apply(const RepairOperation &operation, NativeError *error)
{
    QString targetReason;
    if (!RepairHelperProtocol::targetIsAllowed(operation, &targetReason)) {
        if (error) *error = {NativeErrorDomain::Protocol, ERROR_INVALID_PARAMETER, QStringLiteral("ERROR_INVALID_PARAMETER"), targetReason};
        return false;
    }
    HANDLE control = openControl(error);
    if (control == INVALID_HANDLE_VALUE) return false;
    const auto close = qScopeGuard([&] { CloseHandle(control); });
    QStringList values;
    switch (operation.kind) {
    case RepairOperationKind::AddWhitelistEntry:
    case RepairOperationKind::RemoveWhitelistEntry:
        if (!readMultiString(control, kGetWhitelist, &values, error)) return false;
        values = operation.kind == RepairOperationKind::AddWhitelistEntry
            ? ConfigurationDeltaEngine::addExact(values, operation.targetIdentity).resulting
            : ConfigurationDeltaEngine::removeExact(values, operation.targetIdentity).resulting;
        return writeMultiString(control, kSetWhitelist, values, error);
    case RepairOperationKind::AddBlacklistEntry:
    case RepairOperationKind::RemoveBlacklistEntry:
        if (!readMultiString(control, kGetBlacklist, &values, error)) return false;
        values = operation.kind == RepairOperationKind::AddBlacklistEntry
            ? ConfigurationDeltaEngine::addExact(values, operation.targetIdentity).resulting
            : ConfigurationDeltaEngine::removeExact(values, operation.targetIdentity).resulting;
        return writeMultiString(control, kSetBlacklist, values, error);
    case RepairOperationKind::SetHidHideActive: return writeBoolean(control, kSetActive, operation.requestedValue, error);
    case RepairOperationKind::SetHidHideInverse: return writeBoolean(control, kSetInverse, operation.requestedValue, error);
    default:
        if (error) *error = {NativeErrorDomain::Protocol, ERROR_NOT_SUPPORTED, QStringLiteral("ERROR_NOT_SUPPORTED"), QStringLiteral("Operation is outside the Phase 3 R1 mutator allow-list.")};
        return false;
    }
}

} // namespace hotas::doctor
