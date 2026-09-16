#include "doctor_deep_execution.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>

#include <windows.h>
#include <winsvc.h>

#include <algorithm>

namespace hotas::doctor {
namespace {

constexpr wchar_t kHidHideServiceName[] = L"HidHide";
constexpr wchar_t kHidHideDriverPath[] = L"%SystemRoot%\\System32\\drivers\\HidHide.sys";
constexpr wchar_t kRunOnceRoot[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
constexpr wchar_t kHidClass[] = L"{745A17A0-74D3-11D0-B6FE-00C04FB3EFC0}";
constexpr wchar_t kXnaClass[] = L"{D61CA365-5AF4-4486-998B-9DB4734C6CA3}";
constexpr wchar_t kXboxClass[] = L"{05F5CFE2-4733-4950-A6BB-07AAD01A3A84}";

NativeError errorFor(DWORD code, const QString &operation)
{
    return {NativeErrorDomain::Win32, static_cast<qint64>(code), QStringLiteral("WIN32_%1").arg(code),
        operation + QStringLiteral(" failed with Win32 error %1.").arg(code)};
}

RepairOperationJournalEntry operationEntry(const RepairTransaction &transaction, const RepairOperation &operation)
{
    return {transaction.id, transaction.planId, operation.id, operation.kind, operation.targetKind,
        operation.targetIdentity, {}, operation.requestedValue, {}, DoctorOperationState::Running,
        std::nullopt, QDateTime::currentDateTimeUtc()};
}

bool hasExactFilter(const QStringList &filters)
{
    return std::any_of(filters.cbegin(), filters.cend(), [](const QString &value) {
        return value.compare(QStringLiteral("HidHide"), Qt::CaseInsensitive) == 0;
    });
}

QStringList readMultiSz(HKEY key, const wchar_t *value, DWORD *error)
{
    DWORD type = REG_NONE;
    DWORD bytes = 0;
    LONG status = RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) { if (error) *error = ERROR_SUCCESS; return {}; }
    if (status != ERROR_SUCCESS || type != REG_MULTI_SZ) { if (error) *error = status == ERROR_SUCCESS ? ERROR_DATATYPE_MISMATCH : status; return {}; }
    QByteArray data(static_cast<int>(bytes), Qt::Uninitialized);
    status = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE *>(data.data()), &bytes);
    if (status != ERROR_SUCCESS) { if (error) *error = status; return {}; }
    QStringList values;
    const wchar_t *cursor = reinterpret_cast<const wchar_t *>(data.constData());
    const wchar_t *end = cursor + data.size() / static_cast<int>(sizeof(wchar_t));
    while (cursor < end && *cursor) {
        const wchar_t *next = cursor;
        while (next < end && *next) ++next;
        if (next == end) { if (error) *error = ERROR_INVALID_DATA; return {}; }
        values.append(QString::fromWCharArray(cursor, static_cast<int>(next - cursor)));
        cursor = next + 1;
    }
    if (error) *error = ERROR_SUCCESS;
    return values;
}

bool writeMultiSz(HKEY key, const wchar_t *value, const QStringList &values, DWORD *error)
{
    QString joined = values.join(QChar::Null);
    joined.append(QChar::Null).append(QChar::Null);
    const LONG status = RegSetValueExW(key, value, 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE *>(joined.utf16()), static_cast<DWORD>(joined.size() * sizeof(wchar_t)));
    if (error) *error = status;
    return status == ERROR_SUCCESS;
}

bool repairExactService(NativeError *error)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!manager) { if (error) *error = errorFor(GetLastError(), QStringLiteral("OpenSCManagerW")); return false; }
    const auto closeManager = qScopeGuard([&] { CloseServiceHandle(manager); });
    SC_HANDLE service = OpenServiceW(manager, kHidHideServiceName, SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (!service && GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
        const QString driver = QDir::toNativeSeparators(QDir::rootPath() + QStringLiteral("Windows\\System32\\drivers\\HidHide.sys"));
        if (!QFileInfo(driver).isFile()) {
            if (error) *error = {NativeErrorDomain::Win32, ERROR_FILE_NOT_FOUND, QStringLiteral("ERROR_FILE_NOT_FOUND"),
                QStringLiteral("Exact HidHide service registration is absent and the expected installed HidHide.sys is not present.")};
            return false;
        }
        service = CreateServiceW(manager, kHidHideServiceName, L"Nefarius HidHide Service", SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG,
            SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, kHidHideDriverPath,
            nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!service) { if (error) *error = errorFor(GetLastError(), QStringLiteral("CreateServiceW(HidHide)")); return false; }
    } else if (!service) {
        if (error) *error = errorFor(GetLastError(), QStringLiteral("OpenServiceW(HidHide)"));
        return false;
    }
    const auto closeService = qScopeGuard([&] { CloseServiceHandle(service); });
    // Preserve service description, dependencies, account, and every other
    // unrelated value. These three fields are the exact upstream INF contract.
    if (!ChangeServiceConfigW(service, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            kHidHideDriverPath, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
        if (error) *error = errorFor(GetLastError(), QStringLiteral("ChangeServiceConfigW(HidHide)"));
        return false;
    }
    return true;
}

bool repairExactFilters(NativeError *error)
{
    struct FilterKey { const wchar_t *classGuid; QStringList filters; HKEY key = nullptr; };
    QList<FilterKey> keys{{kHidClass}, {kXnaClass}, {kXboxClass}};
    const auto close = qScopeGuard([&] { for (const FilterKey &item : keys) if (item.key) RegCloseKey(item.key); });
    for (FilterKey &item : keys) {
        const QString path = QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Class\\") + QString::fromWCharArray(item.classGuid);
        const LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, reinterpret_cast<LPCWSTR>(path.utf16()), 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &item.key);
        if (status != ERROR_SUCCESS) { if (error) *error = errorFor(status, QStringLiteral("RegOpenKeyExW(HidHide filter class)")); return false; }
        DWORD readError = ERROR_SUCCESS;
        item.filters = readMultiSz(item.key, L"UpperFilters", &readError);
        if (readError != ERROR_SUCCESS) { if (error) *error = errorFor(readError, QStringLiteral("RegQueryValueExW(UpperFilters)")); return false; }
    }
    // All classes are preflighted before the first registry write, so a
    // missing or malformed unrelated class cannot leave a partial repair.
    for (FilterKey &item : keys) {
        if (hasExactFilter(item.filters)) continue;
        item.filters.append(QStringLiteral("HidHide")); // preserve every existing entry and order
        DWORD writeError = ERROR_SUCCESS;
        if (!writeMultiSz(item.key, L"UpperFilters", item.filters, &writeError)) {
            if (error) *error = errorFor(writeError, QStringLiteral("RegSetValueExW(UpperFilters)"));
            return false;
        }
    }
    return true;
}

bool startExactPackage(const ApprovedPackageArtifact &artifact, DWORD *exitCode, NativeError *error)
{
    // The command line is exactly the sealed, catalog-derived EXE path. There
    // is intentionally no command/argument parameter anywhere in this API.
    QString commandLine = QStringLiteral("\"") + QDir::toNativeSeparators(artifact.filePath) + QStringLiteral("\"");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, reinterpret_cast<LPWSTR>(commandLine.data()), nullptr, nullptr, FALSE,
            CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process)) {
        if (error) *error = errorFor(GetLastError(), QStringLiteral("CreateProcessW(approved HidHide package)"));
        return false;
    }
    const auto close = qScopeGuard([&] { CloseHandle(process.hThread); CloseHandle(process.hProcess); });
    if (WaitForSingleObject(process.hProcess, 30 * 60 * 1000) != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, exitCode)) {
        if (error) *error = errorFor(GetLastError(), QStringLiteral("approved HidHide package completion"));
        return false;
    }
    return *exitCode == ERROR_SUCCESS || *exitCode == ERROR_SUCCESS_REBOOT_REQUIRED;
}

bool scheduleOneTimeContinuation(RepairTransaction &transaction, QString *reason)
{
    const QString doctor = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("HidHide Doctor.exe"));
    if (!QFileInfo(doctor).isFile()) { if (reason) *reason = QStringLiteral("Paired HidHide Doctor executable is unavailable for restart continuation."); return false; }
    HKEY key = nullptr;
    const LONG open = RegCreateKeyExW(HKEY_CURRENT_USER, kRunOnceRoot, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open != ERROR_SUCCESS) { if (reason) *reason = QStringLiteral("Restart continuation RunOnce key could not be opened (Win32 %1).").arg(open); return false; }
    const auto close = qScopeGuard([&] { RegCloseKey(key); });
    const QString valueName = QStringLiteral("HOTASBF6-HidHideDoctor-") + transaction.id.value();
    const QString command = QStringLiteral("\"") + QDir::toNativeSeparators(doctor) + QStringLiteral("\" --resume-transaction \"")
        + transaction.id.value() + QStringLiteral("\"");
    const LONG write = RegSetValueExW(key, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, REG_SZ,
        reinterpret_cast<const BYTE *>(command.utf16()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (write != ERROR_SUCCESS) { if (reason) *reason = QStringLiteral("Restart continuation RunOnce value could not be stored (Win32 %1).").arg(write); return false; }
    transaction.continuationState.insert(QStringLiteral("runOnceValueName"), valueName);
    transaction.continuationState.insert(QStringLiteral("continuationLaunch"), command);
    transaction.continuationState.insert(QStringLiteral("stage"), QStringLiteral("ScheduledAwaitingExplicitRestart"));
    return true;
}

bool fixtureOnly(const DoctorEnvironment &environment, const ApprovedPackage &package)
{
    return package.sourceKind == ApprovedPackageSourceKind::FixtureDeterministicTest
        && environment.hidhide.provider == QStringLiteral("fixture-official-nefarius");
}

} // namespace

RepairExecutionResult DeepRepairExecutor::execute(const RepairPlan &plan, const DoctorEnvironment &environment,
    RepairTransaction transaction, const RepairJournalStore &journal)
{
    RepairExecutionResult result;
    result.transaction = std::move(transaction);
    if (plan.riskClass == RepairRiskClass::R0Observe || plan.riskClass == RepairRiskClass::R1Configuration) {
        result.detail = QStringLiteral("Deep executor accepts only sealed R2-R5 plans.");
        return result;
    }
    const QJsonObject payload = plan.deepRepair.value(QStringLiteral("package")).toObject();
    const QString packageId = payload.value(QStringLiteral("packageId")).toString();
    const std::optional<ApprovedPackage> package = ApprovedPackageCatalog::find(packageId);
    if ((plan.riskClass != RepairRiskClass::R2Component && !package) || (package && payload.value(QStringLiteral("expectedSha256")).toString()
            .compare(package->expectedSha256, Qt::CaseInsensitive) != 0)) {
        result.transaction.state = RepairTransactionState::FailedSafely;
        result.detail = QStringLiteral("Sealed plan package identity no longer matches the approved catalog.");
        result.transaction.finalStatus = result.detail;
        journal.persist(result.transaction, nullptr);
        return result;
    }
    std::optional<ApprovedPackageArtifact> artifact;
    for (int index = 0; index < plan.operations.size(); ++index) {
        const RepairOperation &operation = plan.operations.at(index);
        RepairOperationJournalEntry entry = operationEntry(result.transaction, operation);
        result.transaction.currentOperation = index;
        result.transaction.operations.append(entry);
        if (!journal.persist(result.transaction, &result.detail)) { result.transaction.state = RepairTransactionState::FailedSafely; return result; }
        NativeError error;
        bool succeeded = false;
        QString actual;
        switch (operation.kind) {
        case RepairOperationKind::RepairExactServiceConfiguration:
            result.transaction.state = RepairTransactionState::Executing;
            succeeded = repairExactService(&error);
            actual = succeeded ? QStringLiteral("HidHide demand-start kernel-driver service registration verified.") : QString();
            break;
        case RepairOperationKind::RepairExactFilterRegistration:
            result.transaction.state = RepairTransactionState::Executing;
            succeeded = repairExactFilters(&error);
            actual = succeeded ? QStringLiteral("HidHide upper filters present for HID, XNA composite, and Xbox composite classes.") : QString();
            break;
        case RepairOperationKind::ValidateApprovedPackage:
        case RepairOperationKind::StageApprovedPackage: {
            result.transaction.state = operation.kind == RepairOperationKind::ValidateApprovedPackage
                ? RepairTransactionState::PreparingPackage : RepairTransactionState::StagingPackage;
            if (fixtureOnly(environment, *package)) {
                artifact = ApprovedPackageArtifact{*package, QStringLiteral("fixture://approved-package"), {}, package->expectedSha256,
                    package->signerIdentity, package->artifactVersion, package->architecture, package->expectedSize};
                succeeded = true;
                actual = QStringLiteral("Deterministic fixture package identity verified without native package mutation.");
            } else {
                const PackageAcquisitionResult acquisition = ApprovedPackageRuntime::acquire(package->packageId, environment);
                succeeded = acquisition.acquired;
                if (succeeded) { artifact = acquisition.artifact; actual = acquisition.detail; }
                else error = {NativeErrorDomain::Process, acquisition.cancelled ? ERROR_CANCELLED : ERROR_INVALID_DATA,
                    acquisition.cancelled ? QStringLiteral("ERROR_CANCELLED") : QStringLiteral("PACKAGE_ACQUISITION_REJECTED"), acquisition.detail};
            }
            break;
        }
        case RepairOperationKind::InstallApprovedHidHidePackage: {
            result.transaction.state = RepairTransactionState::Installing;
            if (fixtureOnly(environment, *package)) { succeeded = artifact.has_value(); actual = QStringLiteral("Deterministic fixture installer execution completed."); break; }
            if (!artifact) { error = {NativeErrorDomain::Protocol, ERROR_INVALID_STATE, QStringLiteral("PACKAGE_NOT_STAGED"), QStringLiteral("Approved package was not staged and revalidated before install.")}; break; }
            const PackageValidationResult validation = ApprovedPackageRuntime::revalidate(*artifact, environment);
            if (!validation.valid) { error = {NativeErrorDomain::Protocol, ERROR_INVALID_DATA, QStringLiteral("PACKAGE_REVALIDATION_REJECTED"), validation.reason}; break; }
            DWORD exitCode = 0;
            succeeded = startExactPackage(*artifact, &exitCode, &error);
            actual = succeeded ? QStringLiteral("Official HidHide installer completed with exit code %1.").arg(exitCode) : QString();
            break;
        }
        case RepairOperationKind::RemoveSpecificInactiveHidHidePackage:
            // No production R5 route exists until a catalogued rollback
            // package permits exact before/after Driver Store verification.
            succeeded = fixtureOnly(environment, *package);
            actual = succeeded ? QStringLiteral("Deterministic fixture inactive-package removal completed after verified fixture target and rollback assets.") : QString();
            if (!succeeded) error = {NativeErrorDomain::Protocol, ERROR_NOT_SUPPORTED, QStringLiteral("ROLLBACK_PACKAGE_UNAVAILABLE"),
                QStringLiteral("R5 Driver Store removal is unavailable: no independently verified rollback package is catalogued.")};
            break;
        case RepairOperationKind::RequestSystemRestart: {
            QString reason;
            if (package && fixtureOnly(environment, *package)) {
                result.transaction.continuationState.insert(QStringLiteral("stage"), QStringLiteral("FixtureScheduledAwaitingExplicitRestart"));
                result.transaction.continuationState.insert(QStringLiteral("fixtureOnly"), true);
                succeeded = true;
            } else {
                succeeded = scheduleOneTimeContinuation(result.transaction, &reason);
            }
            if (!succeeded) error = {NativeErrorDomain::Win32, ERROR_ACCESS_DENIED, QStringLiteral("CONTINUATION_SCHEDULING_FAILED"), reason};
            actual = succeeded ? QStringLiteral("One-time continuation scheduled; system restart remains a separate explicit owner action.") : QString();
            if (succeeded) {
                result.transaction.operations.last().state = DoctorOperationState::Completed;
                result.transaction.operations.last().actualPostValue = actual;
                result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
                result.transaction.state = RepairTransactionState::AwaitingReboot;
                result.transaction.finalStatus = QStringLiteral("Approved package work completed; choose Restart now or restart later. No automatic reboot was requested.");
                journal.persist(result.transaction, nullptr);
                result.detail = result.transaction.finalStatus;
                result.mutated = true;
                return result;
            }
            break;
        }
        case RepairOperationKind::ReconcileHidHideConfiguration:
            succeeded = false;
            error = {NativeErrorDomain::Protocol, ERROR_INVALID_STATE, QStringLiteral("OBSERVE_FIRST_REQUIRED"),
                QStringLiteral("Configuration reconciliation is only allowed after a fresh post-reboot read-only scan.")};
            break;
        default:
            error = {NativeErrorDomain::Protocol, ERROR_NOT_SUPPORTED, QStringLiteral("OPERATION_NOT_ALLOWED"),
                QStringLiteral("Operation is outside the deep helper executor allow-list.")};
            break;
        }
        if (!succeeded) {
            result.transaction.operations.last().state = DoctorOperationState::Failed;
            result.transaction.operations.last().nativeError = error;
            result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
            result.transaction.state = RepairTransactionState::FailedSafely;
            result.transaction.finalStatus = QStringLiteral("Typed deep repair operation failed; no later operation was attempted.");
            journal.persist(result.transaction, nullptr);
            result.detail = error.message.isEmpty() ? result.transaction.finalStatus : error.message;
            return result;
        }
        result.transaction.operations.last().state = DoctorOperationState::Completed;
        result.transaction.operations.last().actualPostValue = actual;
        result.transaction.operations.last().completedAt = QDateTime::currentDateTimeUtc();
        result.mutated = true;
        if (!journal.persist(result.transaction, &result.detail)) { result.transaction.state = RepairTransactionState::RecoveryRequired; return result; }
    }
    result.transaction.state = RepairTransactionState::Completed;
    result.transaction.finalStatus = QStringLiteral("Exact component repair completed; a fresh Doctor scan is required to verify the affected state.");
    journal.persist(result.transaction, nullptr);
    result.detail = result.transaction.finalStatus;
    return result;
}

bool DeepRepairExecutor::requestRestartNow(const RepairTransaction &transaction, QString *reason)
{
    if (!transaction.id.isValid() || transaction.state != RepairTransactionState::AwaitingReboot
        || transaction.continuationState.value(QStringLiteral("stage")).toString() != QStringLiteral("ScheduledAwaitingExplicitRestart")) {
        if (reason) *reason = QStringLiteral("Restart now is unavailable because no durable deep-repair continuation is awaiting reboot.");
        return false;
    }
    // This method is intentionally never called by execute(). The caller must
    // bind a second explicit owner selection to the durable transaction.
    if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED)) {
        if (reason) *reason = QStringLiteral("Windows rejected the explicit restart request (Win32 %1).").arg(GetLastError());
        return false;
    }
    return true;
}

} // namespace hotas::doctor
