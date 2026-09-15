#include "doctor_repair_helper_client.h"

#include "doctor_repair_helper_protocol.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QUuid>

#include <windows.h>
#include <shellapi.h>

namespace hotas::doctor {
namespace {

QString randomHex()
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();
}

QString quoted(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

bool writeExact(HANDLE handle, const QByteArray &payload)
{
    const DWORD bytes = static_cast<DWORD>(payload.size());
    DWORD written = 0;
    return WriteFile(handle, &bytes, sizeof(bytes), &written, nullptr) && written == sizeof(bytes)
        && WriteFile(handle, payload.constData(), bytes, &written, nullptr) && written == bytes;
}

bool readExact(HANDLE handle, void *buffer, DWORD bytes)
{
    DWORD read = 0;
    return ReadFile(handle, buffer, bytes, &read, nullptr) && read == bytes;
}

RepairHelperClientResult rejected(RepairHelperOutcome outcome, const QString &detail, bool connectivityOnly)
{
    RepairHelperClientResult result;
    result.outcome = outcome;
    result.detail = detail;
    result.connectivityOnly = connectivityOnly;
    return result;
}

} // namespace

RepairHelperClientResult RepairHelperClient::invoke(const RepairPlan &authorizedPlan,
    const DoctorEnvironment &environment, const QString &doctorBuildId,
    const QString &helperBuildId, bool connectivityOnly, const RepairTransactionId &transactionId)
{
    QString reason;
    if (!RepairHelperContract::validate(authorizedPlan, environment, true, &reason))
        return rejected(RepairHelperOutcome::ProtocolRejected, reason, connectivityOnly);
    if (doctorBuildId.isEmpty() || helperBuildId.isEmpty())
        return rejected(RepairHelperOutcome::ProtocolRejected, QStringLiteral("Doctor/helper build identity is required."), connectivityOnly);

    const QString nonce = randomHex();
    const QString pipeName = QStringLiteral("HOTASBF6-Doctor-") + randomHex().left(32);
    RepairHelperRequest request;
    request.doctorBuildId = doctorBuildId;
    request.helperBuildId = helperBuildId;
    request.transactionId = transactionId.isValid() ? transactionId
        : RepairTransactionId(QStringLiteral("REPAIR-TX-") + randomHex().left(32));
    request.plan = authorizedPlan;
    request.nonce = nonce;
    request.expiresAt = QDateTime::currentDateTimeUtc().addSecs(120);
    request.connectivityOnly = connectivityOnly;
    request.requestDigest = RepairHelperProtocol::seal(request);
    const QByteArray payload = QJsonDocument(RepairHelperProtocol::serialize(request)).toJson(QJsonDocument::Compact);

    const QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("HidHide Doctor Repair.exe"));
    if (!QFileInfo::exists(helper))
        return rejected(RepairHelperOutcome::LaunchDenied, QStringLiteral("The paired HidHide Doctor Repair helper is unavailable."), connectivityOnly);
    const QString parameters = QStringLiteral("--pipe %1 --nonce %2 --doctor-build %3")
        .arg(quoted(pipeName), quoted(nonce), quoted(doctorBuildId));
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = L"runas";
    launch.lpFile = reinterpret_cast<LPCWSTR>(helper.utf16());
    launch.lpParameters = reinterpret_cast<LPCWSTR>(parameters.utf16());
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) {
        const DWORD error = GetLastError();
        return rejected(error == ERROR_CANCELLED ? RepairHelperOutcome::AuthorizationCancelled : RepairHelperOutcome::LaunchDenied,
            error == ERROR_CANCELLED ? QStringLiteral("Authorization cancelled — no changes made.")
                : QStringLiteral("The elevated repair helper could not be launched (Win32 %1). ").arg(error), connectivityOnly);
    }
    if (launch.hProcess) CloseHandle(launch.hProcess);

    const QString fullPipe = QStringLiteral("\\\\.\\pipe\\") + pipeName;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline) {
        pipe = CreateFileW(reinterpret_cast<LPCWSTR>(fullPipe.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) break;
        WaitNamedPipeW(reinterpret_cast<LPCWSTR>(fullPipe.utf16()), 250);
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return rejected(RepairHelperOutcome::ConnectTimedOut, QStringLiteral("The elevated helper did not accept its one-time secure connection."), connectivityOnly);
    const auto closePipe = qScopeGuard([&] { CloseHandle(pipe); });
    if (!writeExact(pipe, payload))
        return rejected(RepairHelperOutcome::Disconnected, QStringLiteral("The helper disconnected before it received the sealed request."), connectivityOnly);
    DWORD size = 0;
    if (!readExact(pipe, &size, sizeof(size)) || size == 0 || size > RepairHelperRequest::maximumMessageBytes)
        return rejected(RepairHelperOutcome::MalformedResponse, QStringLiteral("The helper response was malformed or oversized."), connectivityOnly);
    QByteArray frame(static_cast<int>(size), Qt::Uninitialized);
    if (!readExact(pipe, frame.data(), size))
        return rejected(RepairHelperOutcome::Disconnected, QStringLiteral("The helper disconnected before its response completed."), connectivityOnly);
    QJsonParseError error;
    const QJsonDocument response = QJsonDocument::fromJson(frame, &error);
    if (error.error != QJsonParseError::NoError || !response.isObject()
        || response.object().value(QStringLiteral("protocolVersion")).toInt() != RepairHelperRequest::protocolVersion)
        return rejected(RepairHelperOutcome::MalformedResponse, QStringLiteral("The helper returned an incompatible response."), connectivityOnly);
    const QJsonObject object = response.object();
    RepairHelperClientResult result;
    result.outcome = object.value(QStringLiteral("accepted")).toBool() ? RepairHelperOutcome::Accepted : RepairHelperOutcome::ProtocolRejected;
    result.detail = object.value(QStringLiteral("detail")).toString();
    result.transactionId = RepairTransactionId(object.value(QStringLiteral("transactionId")).toString());
    result.state = static_cast<RepairTransactionState>(object.value(QStringLiteral("state")).toInt(static_cast<int>(RepairTransactionState::FailedSafely)));
    result.mutated = object.value(QStringLiteral("mutated")).toBool();
    result.connectivityOnly = connectivityOnly;
    return result;
}

QString displayName(RepairHelperOutcome outcome)
{
    switch (outcome) {
    case RepairHelperOutcome::Accepted: return QStringLiteral("HELPER ACCEPTED");
    case RepairHelperOutcome::AuthorizationCancelled: return QStringLiteral("AUTHORIZATION CANCELLED");
    case RepairHelperOutcome::LaunchDenied: return QStringLiteral("HELPER LAUNCH DENIED");
    case RepairHelperOutcome::ConnectTimedOut: return QStringLiteral("HELPER CONNECTION TIMED OUT");
    case RepairHelperOutcome::ProtocolRejected: return QStringLiteral("HELPER REJECTED REQUEST");
    case RepairHelperOutcome::MalformedResponse: return QStringLiteral("MALFORMED HELPER RESPONSE");
    case RepairHelperOutcome::Disconnected: return QStringLiteral("HELPER DISCONNECTED");
    }
    return QStringLiteral("HELPER FAILED SAFELY");
}

} // namespace hotas::doctor
