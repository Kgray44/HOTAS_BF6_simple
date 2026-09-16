#include "doctor_diagnostics.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVector>

#include <windows.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <shlobj_core.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <winevt.h>
#include <wintrust.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace hotas::doctor {
namespace {

constexpr DWORD kProtocolTimeoutMs = 2500;
constexpr DWORD kMaximumProtocolPayload = 1024 * 1024;
constexpr DWORD kMaximumDevices = 512;
constexpr DWORD kMaximumEvents = 64;
constexpr DWORD kMaxSetupApiExcerptChars = 16000;

// Defined by HidHide's public interface contract.  It is only used to
// discover a readable control-device path; the protocol below still issues
// just the documented GET IOCTLs.
const GUID kHidHideInterfaceGuid = {0x0c320ff7, 0xbd9b, 0x42b6, {0xbd, 0xaf, 0x49, 0xfe, 0xb9, 0xc9, 0x16, 0x49}};

// These are the Windows SDK values for GUID_DEVINTERFACE_HID and
// DEVPKEY_Device_ContainerId. Keeping immutable local copies avoids the
// SDK's process-wide INITGUID definition switch in this provider.
const GUID kHidDeviceInterfaceGuid = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
const DEVPROPKEY kDeviceContainerId = {{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};

constexpr DWORD hidHideIoctl(DWORD function)
{
    // CTL_CODE(32769, function, METHOD_BUFFERED, FILE_READ_DATA).  Only the
    // four documented GET codes below are represented anywhere in this file.
    return (32769u << 16u) | (1u << 14u) | (function << 2u);
}

constexpr DWORD kIoctlGetWhitelist = hidHideIoctl(2048);
constexpr DWORD kIoctlGetBlacklist = hidHideIoctl(2050);
constexpr DWORD kIoctlGetActive = hidHideIoctl(2052);
constexpr DWORD kIoctlGetInverse = hidHideIoctl(2054);

QString fromWide(const wchar_t *value)
{
    return value ? QString::fromWCharArray(value) : QString();
}

QString win32Message(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const auto cleanup = qScopeGuard([&] { if (buffer) LocalFree(buffer); });
    return length ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                  : QStringLiteral("Win32 error %1").arg(code);
}

NativeError win32Error(DWORD code, const QString &operation)
{
    return {NativeErrorDomain::Win32, static_cast<qint64>(code),
        QStringLiteral("WIN32_%1").arg(code),
        operation + QStringLiteral(": ") + win32Message(code)};
}

CpuArchitecture architectureForProcessor(WORD architecture)
{
    switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return CpuArchitecture::X64;
    case PROCESSOR_ARCHITECTURE_ARM64: return CpuArchitecture::Arm64;
    case PROCESSOR_ARCHITECTURE_INTEL: return CpuArchitecture::X86;
    default: return CpuArchitecture::Unknown;
    }
}

CpuArchitecture nativeArchitecture()
{
    SYSTEM_INFO information{};
    GetNativeSystemInfo(&information);
    return architectureForProcessor(information.wProcessorArchitecture);
}

QString knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR value = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value);
    const auto cleanup = qScopeGuard([&] { if (value) CoTaskMemFree(value); });
    return SUCCEEDED(result) ? QString::fromWCharArray(value) : QString();
}

QString windowsDirectory()
{
    std::array<wchar_t, 32768> buffer{};
    const UINT size = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return size && size < buffer.size() ? QString::fromWCharArray(buffer.data(), static_cast<int>(size)) : QString();
}

struct RegistryRead final {
    bool exists = false;
    DWORD type = REG_NONE;
    QByteArray data;
    std::optional<NativeError> error;
};

RegistryRead readRegistryValue(HKEY root, const QString &keyPath, const QString &valueName)
{
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, reinterpret_cast<LPCWSTR>(keyPath.utf16()), 0,
        KEY_READ | KEY_WOW64_64KEY, &key);
    if (openResult != ERROR_SUCCESS) return {false, REG_NONE, {}, win32Error(openResult, QStringLiteral("RegOpenKeyExW"))};
    const auto close = qScopeGuard([&] { RegCloseKey(key); });
    DWORD type = REG_NONE;
    DWORD bytes = 0;
    const LONG sizeResult = RegQueryValueExW(key,
        valueName.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(valueName.utf16()), nullptr, &type, nullptr, &bytes);
    if (sizeResult != ERROR_SUCCESS) return {false, type, {}, win32Error(sizeResult, QStringLiteral("RegQueryValueExW"))};
    QByteArray data(static_cast<int>(bytes), Qt::Uninitialized);
    const LONG readResult = RegQueryValueExW(key,
        valueName.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(valueName.utf16()), nullptr, &type,
        reinterpret_cast<BYTE *>(data.data()), &bytes);
    if (readResult != ERROR_SUCCESS) return {false, type, {}, win32Error(readResult, QStringLiteral("RegQueryValueExW"))};
    data.resize(static_cast<int>(bytes));
    return {true, type, std::move(data), std::nullopt};
}

QString registryString(const RegistryRead &read)
{
    if (!read.exists || (read.type != REG_SZ && read.type != REG_EXPAND_SZ) || read.data.size() < static_cast<int>(sizeof(wchar_t))) return {};
    return QString::fromWCharArray(reinterpret_cast<const wchar_t *>(read.data.constData()),
        read.data.size() / static_cast<int>(sizeof(wchar_t))).split(QChar::Null).front();
}

QStringList registryMultiString(const RegistryRead &read)
{
    QStringList values;
    if (!read.exists || read.type != REG_MULTI_SZ || read.data.size() < static_cast<int>(sizeof(wchar_t))) return values;
    const wchar_t *cursor = reinterpret_cast<const wchar_t *>(read.data.constData());
    const wchar_t *const end = cursor + read.data.size() / static_cast<int>(sizeof(wchar_t));
    while (cursor < end && *cursor != L'\0') {
        const wchar_t *next = cursor;
        while (next < end && *next != L'\0') ++next;
        if (next == end) break;
        values.append(QString::fromWCharArray(cursor, static_cast<int>(next - cursor)));
        cursor = next + 1;
    }
    return values;
}

std::optional<DWORD> registryDword(const RegistryRead &read)
{
    if (!read.exists || read.type != REG_DWORD || read.data.size() != static_cast<int>(sizeof(DWORD))) return std::nullopt;
    DWORD value = 0;
    std::memcpy(&value, read.data.constData(), sizeof(value));
    return value;
}

QStringList readStringProperty(HDEVINFO deviceInfo, PSP_DEVINFO_DATA device, DWORD property, QList<QString> *failures)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (SetupDiGetDeviceRegistryPropertyW(deviceInfo, device, property, &type, nullptr, 0, &bytes)
        || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        if (failures && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_INVALID_DATA)
            failures->append(QStringLiteral("SPDRP_%1:%2").arg(property).arg(GetLastError()));
        return {};
    }
    QVector<BYTE> buffer(static_cast<qsizetype>(bytes));
    if (!SetupDiGetDeviceRegistryPropertyW(deviceInfo, device, property, &type, buffer.data(), bytes, nullptr)) {
        if (failures) failures->append(QStringLiteral("SPDRP_%1:%2").arg(property).arg(GetLastError()));
        return {};
    }
    const RegistryRead read{true, type, QByteArray(reinterpret_cast<const char *>(buffer.constData()), static_cast<int>(bytes)), std::nullopt};
    return type == REG_MULTI_SZ ? registryMultiString(read) : QStringList{registryString(read)};
}

QString readGuidProperty(HDEVINFO deviceInfo, PSP_DEVINFO_DATA device, const DEVPROPKEY &property, QList<QString> *failures)
{
    DEVPROPTYPE type = 0;
    DWORD bytes = 0;
    SetupDiGetDevicePropertyW(deviceInfo, device, &property, &type, nullptr, 0, &bytes, 0);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes != sizeof(GUID)) {
        if (failures && GetLastError() != ERROR_NOT_FOUND && GetLastError() != ERROR_FILE_NOT_FOUND)
            failures->append(QStringLiteral("DEVPKEY:%1").arg(GetLastError()));
        return {};
    }
    GUID value{};
    if (!SetupDiGetDevicePropertyW(deviceInfo, device, &property, &type, reinterpret_cast<PBYTE>(&value), sizeof(value), nullptr, 0)) {
        if (failures) failures->append(QStringLiteral("DEVPKEY:%1").arg(GetLastError()));
        return {};
    }
    wchar_t text[64]{};
    return StringFromGUID2(value, text, static_cast<int>(std::size(text))) ? fromWide(text) : QString();
}

QString fileVersionString(const QString &path, const wchar_t *field)
{
    DWORD unused = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(reinterpret_cast<LPCWSTR>(path.utf16()), &unused);
    if (!bytes) return {};
    QVector<BYTE> buffer(static_cast<qsizetype>(bytes));
    if (!GetFileVersionInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, bytes, buffer.data())) return {};
    struct Translation { WORD language; WORD codePage; };
    Translation *translation = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID *>(&translation), &translationBytes)
        || translationBytes < sizeof(Translation)) return {};
    const QString query = QStringLiteral("\\StringFileInfo\\%1%2\\%3")
        .arg(translation->language, 4, 16, QLatin1Char('0'))
        .arg(translation->codePage, 4, 16, QLatin1Char('0'))
        .arg(QString::fromWCharArray(field));
    wchar_t *value = nullptr;
    UINT valueBytes = 0;
    if (!VerQueryValueW(buffer.data(), reinterpret_cast<LPCWSTR>(query.utf16()), reinterpret_cast<LPVOID *>(&value), &valueBytes)
        || !value || valueBytes == 0) return {};
    return QString::fromWCharArray(value, static_cast<int>(valueBytes)).trimmed();
}

QString sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(256 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) return {};
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}

CpuArchitecture binaryArchitecture(const QString &path)
{
    DWORD type = 0;
    if (!GetBinaryTypeW(reinterpret_cast<LPCWSTR>(path.utf16()), &type)) return CpuArchitecture::Unknown;
    switch (type) {
    case SCS_64BIT_BINARY: return CpuArchitecture::X64;
    case SCS_32BIT_BINARY: return CpuArchitecture::X86;
    default: return CpuArchitecture::Unknown;
    }
}

SignatureTrustState signatureState(const QString &path)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = reinterpret_cast<LPCWSTR>(path.utf16());
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &fileInfo;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    const GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, const_cast<GUID *>(&policy), &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, const_cast<GUID *>(&policy), &trust);
    if (result == ERROR_SUCCESS) return SignatureTrustState::Trusted;
    if (result == TRUST_E_NOSIGNATURE) return SignatureTrustState::NotSigned;
    if (result == TRUST_E_SUBJECT_NOT_TRUSTED || result == CERT_E_UNTRUSTEDROOT) return SignatureTrustState::Untrusted;
    return SignatureTrustState::Error;
}

FileArtifactObservation inspectArtifact(DoctorArtifactKind kind, const QString &role, const QString &path)
{
    FileArtifactObservation observation;
    observation.kind = kind;
    observation.role = role;
    observation.path = QDir::toNativeSeparators(path);
    const QFileInfo info(path);
    observation.exists = info.isFile();
    if (!observation.exists) return observation;
    observation.size = static_cast<quint64>(info.size());
    observation.lastModified = info.lastModified().toUTC();
    observation.fileVersion = fileVersionString(path, L"FileVersion");
    observation.productVersion = fileVersionString(path, L"ProductVersion");
    observation.architecture = binaryArchitecture(path);
    observation.sha256 = sha256(path);
    observation.signatureTrust = signatureState(path);
    return observation;
}

bool mentionsHidHide(const QString &value)
{
    return value.contains(QStringLiteral("hidhide"), Qt::CaseInsensitive);
}

const FileArtifactObservation *artifactFor(const ReadOnlyDiagnosticSnapshot &snapshot, DoctorArtifactKind kind)
{
    for (const FileArtifactObservation &artifact : snapshot.artifacts) {
        if (artifact.kind == kind && artifact.exists) return &artifact;
    }
    return nullptr;
}

const ProtocolObservation *protocolFor(const ReadOnlyDiagnosticSnapshot &snapshot, const QString &operation)
{
    const ProtocolObservation *first = nullptr;
    for (const ProtocolObservation &probe : snapshot.protocol) {
        if (probe.operation != operation) continue;
        if (!first) first = &probe;
        if (probe.status == DoctorCheckStatus::Healthy) return &probe;
    }
    return first;
}

void addCatalogObservation(ReadOnlyDiagnosticSnapshot &snapshot, const QString &id, DoctorCheckStatus status,
    QString summary, QString technicalDetails = {}, std::optional<NativeError> nativeError = std::nullopt)
{
    snapshot.catalogObservations.append({id, status, std::move(summary), std::move(technicalDetails), std::move(nativeError)});
}

} // namespace

QString displayName(DoctorArtifactKind kind)
{
    switch (kind) {
    case DoctorArtifactKind::ClientExecutable: return QStringLiteral("HidHideClient.exe");
    case DoctorArtifactKind::CliExecutable: return QStringLiteral("HidHideCLI.exe");
    case DoctorArtifactKind::DriverBinary: return QStringLiteral("HidHide driver binary");
    case DoctorArtifactKind::DriverStorePackage: return QStringLiteral("Driver Store package");
    case DoctorArtifactKind::InstallerMetadata: return QStringLiteral("Installer metadata");
    case DoctorArtifactKind::Unknown: return QStringLiteral("Unknown artifact");
    }
    return QStringLiteral("Unknown artifact");
}

QString displayName(SignatureTrustState state)
{
    switch (state) {
    case SignatureTrustState::NotChecked: return QStringLiteral("Not checked");
    case SignatureTrustState::Trusted: return QStringLiteral("Trusted");
    case SignatureTrustState::Untrusted: return QStringLiteral("Untrusted");
    case SignatureTrustState::NotSigned: return QStringLiteral("Not signed");
    case SignatureTrustState::Error: return QStringLiteral("Verification error");
    case SignatureTrustState::NotApplicable: return QStringLiteral("Not applicable");
    }
    return QStringLiteral("Unknown");
}

QString displayName(DeviceClassification classification)
{
    switch (classification) {
    case DeviceClassification::PhysicalGamingInput: return QStringLiteral("Physical gaming input");
    case DeviceClassification::VirtualGamingDevice: return QStringLiteral("Virtual gaming device");
    case DeviceClassification::VJoyVirtualOutput: return QStringLiteral("vJoy virtual output");
    case DeviceClassification::HidHideManagedLooking: return QStringLiteral("HidHide-managed-looking");
    case DeviceClassification::StaleOrPhantom: return QStringLiteral("Stale or phantom");
    case DeviceClassification::ProblemDevice: return QStringLiteral("Device with problem");
    case DeviceClassification::Unknown: return QStringLiteral("Unknown or ambiguous");
    }
    return QStringLiteral("Unknown or ambiguous");
}

namespace {

DoctorEnvironment observeEnvironment(QList<PendingRestartObservation> *pendingRestart)
{
    DoctorEnvironment environment;
    const QString currentVersion = QStringLiteral("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    const RegistryRead product = readRegistryValue(HKEY_LOCAL_MACHINE, currentVersion, QStringLiteral("ProductName"));
    const RegistryRead displayVersion = readRegistryValue(HKEY_LOCAL_MACHINE, currentVersion, QStringLiteral("DisplayVersion"));
    const RegistryRead build = readRegistryValue(HKEY_LOCAL_MACHINE, currentVersion, QStringLiteral("CurrentBuildNumber"));
    const RegistryRead ubr = readRegistryValue(HKEY_LOCAL_MACHINE, currentVersion, QStringLiteral("UBR"));
    environment.platform.windowsEdition = registryString(product);
    environment.platform.windowsVersion = registryString(displayVersion);
    bool buildOk = false;
    environment.platform.build = registryString(build).toUInt(&buildOk);
    if (!buildOk) environment.platform.build = 0;
    environment.platform.revision = registryDword(ubr).value_or(0);
    environment.platform.nativeArchitecture = nativeArchitecture();
    environment.platform.processArchitecture = sizeof(void *) == 8 ? CpuArchitecture::X64 : CpuArchitecture::X86;
    environment.platform.doctorBinaryArchitecture = binaryArchitecture(QCoreApplication::applicationFilePath());
    const QString helperPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("HidHideDoctorRepair.exe"));
    environment.platform.helperBinaryArchitecture = binaryArchitecture(helperPath);
    BOOL wow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &wow64)) environment.platform.wow64OrEmulated = wow64 != FALSE;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) environment.platform.localeName = fromWide(locale);

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        const auto close = qScopeGuard([&] { CloseHandle(token); });
        TOKEN_ELEVATION elevation{};
        DWORD bytes = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes))
            environment.privilege = elevation.TokenIsElevated ? PrivilegeState::Elevated : PrivilegeState::Unelevated;
        else
            environment.privilege = PrivilegeState::PermissionLimited;
    } else {
        environment.privilege = PrivilegeState::PermissionLimited;
    }
    environment.capabilities.diagnosisSupported = true;
    environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::DiagnosisSupported;
    // Do not guess this from CMake or pointer size.  The helper is paired by
    // its on-disk image and must be native to the measured Windows platform.
    environment.capabilities.helperArchitectureCompatible = environment.platform.nativeArchitecture != CpuArchitecture::Unknown
        && environment.platform.processArchitecture == environment.platform.nativeArchitecture
        && environment.platform.doctorBinaryArchitecture == environment.platform.nativeArchitecture
        && environment.platform.helperBinaryArchitecture == environment.platform.nativeArchitecture
        && !environment.platform.wow64OrEmulated;

    const auto appendMarker = [&](const QString &key, const QString &value) {
        const RegistryRead marker = readRegistryValue(HKEY_LOCAL_MACHINE, key, value);
        if (marker.exists) pendingRestart->append({key + QStringLiteral("\\") + value,
            QStringLiteral("present"), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt});
    };
    appendMarker(QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Session Manager"), QStringLiteral("PendingFileRenameOperations"));
    appendMarker(QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending"), {});
    appendMarker(QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired"), {});
    return environment;
}

QStringList installRootsFromRegistry()
{
    QStringList roots;
    const QStringList uninstallRoots = {
        QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")};
    for (const QString &uninstallRoot : uninstallRoots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, reinterpret_cast<LPCWSTR>(uninstallRoot.utf16()), 0,
                KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) continue;
        const auto close = qScopeGuard([&] { RegCloseKey(key); });
        for (DWORD index = 0;; ++index) {
            wchar_t name[512]{};
            DWORD length = static_cast<DWORD>(std::size(name));
            const LONG result = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (result == ERROR_NO_MORE_ITEMS) break;
            if (result != ERROR_SUCCESS) continue;
            const QString subkey = uninstallRoot + QLatin1Char('\\') + QString::fromWCharArray(name, static_cast<int>(length));
            const QString displayName = registryString(readRegistryValue(HKEY_LOCAL_MACHINE, subkey, QStringLiteral("DisplayName")));
            const QString publisher = registryString(readRegistryValue(HKEY_LOCAL_MACHINE, subkey, QStringLiteral("Publisher")));
            if (!mentionsHidHide(displayName) && !mentionsHidHide(publisher)) continue;
            const QString root = registryString(readRegistryValue(HKEY_LOCAL_MACHINE, subkey, QStringLiteral("InstallLocation")));
            if (!root.isEmpty()) roots.append(QDir::cleanPath(root));
        }
    }
    const QString programFiles = knownFolder(FOLDERID_ProgramFiles);
    if (!programFiles.isEmpty()) roots.append(QDir(programFiles).filePath(QStringLiteral("Nefarius Software Solutions/HidHide")));
    roots.removeDuplicates();
    return roots;
}

QStringList findNamedFiles(const QStringList &roots, const QString &fileName)
{
    QStringList result;
    for (const QString &root : roots) {
        if (!QFileInfo(root).isDir()) continue;
        QDirIterator files(root, {fileName}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext() && result.size() < 32) result.append(files.next());
    }
    result.removeDuplicates();
    return result;
}

ServiceObservation observeHidHideService()
{
    ServiceObservation observation;
    observation.serviceName = QStringLiteral("HidHide");
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        observation.nativeError = win32Error(GetLastError(), QStringLiteral("OpenSCManagerW"));
        return observation;
    }
    const auto closeManager = qScopeGuard([&] { CloseServiceHandle(manager); });
    SC_HANDLE service = OpenServiceW(manager, L"HidHide", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    if (!service) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST) observation.nativeError = win32Error(error, QStringLiteral("OpenServiceW(HidHide)"));
        return observation;
    }
    observation.present = true;
    const auto closeService = qScopeGuard([&] { CloseServiceHandle(service); });
    DWORD bytes = 0;
    QueryServiceConfigW(service, nullptr, 0, &bytes);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes > 0) {
        QVector<BYTE> buffer(static_cast<qsizetype>(bytes));
        auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (QueryServiceConfigW(service, config, bytes, &bytes)) {
            observation.displayName = fromWide(config->lpDisplayName);
            observation.binaryPath = fromWide(config->lpBinaryPathName);
            observation.startType = QString::number(config->dwStartType);
            if (config->lpDependencies) {
                const wchar_t *dependency = config->lpDependencies;
                while (*dependency) {
                    observation.dependencies.append(fromWide(dependency));
                    dependency += wcslen(dependency) + 1;
                }
            }
        }
    }
    SERVICE_STATUS_PROCESS status{};
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status), sizeof(status), &bytes))
        observation.currentState = QString::number(status.dwCurrentState);
    else
        observation.nativeError = win32Error(GetLastError(), QStringLiteral("QueryServiceStatusEx(HidHide)"));
    return observation;
}

struct IoctlResult final {
    bool completed = false;
    bool timedOut = false;
    DWORD bytes = 0;
    std::optional<NativeError> error;
    qint64 durationMs = 0;
};

IoctlResult readOnlyIoctl(HANDLE device, DWORD operation, void *output, DWORD outputBytes)
{
    QElapsedTimer timer;
    timer.start();
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return {false, false, 0, win32Error(GetLastError(), QStringLiteral("CreateEventW")), timer.elapsed()};
    const auto closeEvent = qScopeGuard([&] { CloseHandle(event); });
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    DWORD returned = 0;
    if (DeviceIoControl(device, operation, nullptr, 0, output, outputBytes, &returned, &overlapped))
        return {true, false, returned, std::nullopt, timer.elapsed()};
    DWORD failure = GetLastError();
    if (failure != ERROR_IO_PENDING)
        return {false, false, returned, win32Error(failure, QStringLiteral("DeviceIoControl(GET)")), timer.elapsed()};
    const DWORD wait = WaitForSingleObject(event, kProtocolTimeoutMs);
    if (wait == WAIT_TIMEOUT) {
        CancelIoEx(device, &overlapped);
        WaitForSingleObject(event, 250);
        return {false, true, 0, win32Error(ERROR_TIMEOUT, QStringLiteral("DeviceIoControl(GET) timeout")), timer.elapsed()};
    }
    if (wait != WAIT_OBJECT_0)
        return {false, false, 0, win32Error(GetLastError(), QStringLiteral("WaitForSingleObject(DeviceIoControl)")), timer.elapsed()};
    if (!GetOverlappedResult(device, &overlapped, &returned, FALSE))
        return {false, false, 0, win32Error(GetLastError(), QStringLiteral("GetOverlappedResult(DeviceIoControl)")), timer.elapsed()};
    return {true, false, returned, std::nullopt, timer.elapsed()};
}

ProtocolObservation boolProtocolQuery(HANDLE device, DWORD code, const QString &operation)
{
    BYTE value = 0;
    const IoctlResult result = readOnlyIoctl(device, code, &value, sizeof(value));
    ProtocolObservation observation;
    observation.operation = operation;
    observation.durationMs = result.durationMs;
    observation.nativeError = result.error;
    if (result.timedOut) observation.status = DoctorCheckStatus::TimedOut;
    else if (!result.completed) observation.status = DoctorCheckStatus::Failed;
    else if (result.bytes != sizeof(value)) {
        observation.status = DoctorCheckStatus::Failed;
        observation.nativeError = win32Error(ERROR_INVALID_PARAMETER, QStringLiteral("Unexpected boolean GET payload size"));
    } else {
        observation.status = DoctorCheckStatus::Healthy;
        observation.value = value ? QStringLiteral("true") : QStringLiteral("false");
    }
    return observation;
}

QList<ProtocolObservation> multiStringProtocolQuery(HANDLE device, DWORD code, const QString &operation)
{
    ProtocolObservation size;
    size.operation = operation + QStringLiteral("_SIZE");
    size.sizeNegotiation = true;
    const IoctlResult first = readOnlyIoctl(device, code, nullptr, 0);
    size.durationMs = first.durationMs;
    if (first.timedOut) {
        size.status = DoctorCheckStatus::TimedOut;
        size.nativeError = first.error;
        return {size};
    }
    const DWORD firstError = first.error ? static_cast<DWORD>(first.error->code) : ERROR_SUCCESS;
    if (first.completed && first.bytes == 0) {
        size.status = DoctorCheckStatus::Healthy;
        size.value = QStringLiteral("0 bytes");
        return {size};
    }
    const bool reportsPayloadSize = first.completed || firstError == ERROR_INSUFFICIENT_BUFFER || firstError == ERROR_MORE_DATA;
    if (!reportsPayloadSize || first.bytes == 0) {
        size.status = DoctorCheckStatus::Failed;
        size.nativeError = first.error;
        return {size};
    }
    size.status = DoctorCheckStatus::Healthy;
    size.value = QStringLiteral("%1 bytes").arg(first.bytes);
    if (first.bytes > kMaximumProtocolPayload || first.bytes % sizeof(wchar_t) != 0) {
        size.status = DoctorCheckStatus::Failed;
        size.nativeError = win32Error(ERROR_INVALID_DATA, QStringLiteral("Invalid HidHide MULTI_SZ size"));
        return {size};
    }
    QVector<wchar_t> payload(static_cast<qsizetype>(first.bytes / sizeof(wchar_t)));
    const IoctlResult second = readOnlyIoctl(device, code, payload.data(), first.bytes);
    ProtocolObservation payloadResult;
    payloadResult.operation = operation;
    payloadResult.durationMs = second.durationMs;
    payloadResult.nativeError = second.error;
    if (second.timedOut) payloadResult.status = DoctorCheckStatus::TimedOut;
    else if (!second.completed) payloadResult.status = DoctorCheckStatus::Failed;
    else if (second.bytes > first.bytes || second.bytes % sizeof(wchar_t) != 0) {
        payloadResult.status = DoctorCheckStatus::Failed;
        payloadResult.nativeError = win32Error(ERROR_INVALID_DATA, QStringLiteral("Invalid HidHide MULTI_SZ payload"));
    } else {
        payloadResult.status = DoctorCheckStatus::Healthy;
        const wchar_t *cursor = payload.constData();
        const wchar_t *const end = cursor + second.bytes / sizeof(wchar_t);
        bool terminated = false;
        while (cursor < end) {
            const wchar_t *next = cursor;
            while (next < end && *next != L'\0') ++next;
            if (next == end) break;
            if (next == cursor) { terminated = true; break; }
            payloadResult.multiStringValues.append(QString::fromWCharArray(cursor, static_cast<int>(next - cursor)));
            cursor = next + 1;
        }
        if (!terminated) {
            payloadResult.status = DoctorCheckStatus::Failed;
            payloadResult.nativeError = win32Error(ERROR_INVALID_DATA, QStringLiteral("Unterminated HidHide MULTI_SZ payload"));
        } else {
            payloadResult.value = QStringLiteral("%1 entries").arg(payloadResult.multiStringValues.size());
        }
    }
    return {size, payloadResult};
}

QStringList observeHidHideInterfaces(QList<QString> *limits)
{
    QStringList paths;
    HDEVINFO devices = SetupDiGetClassDevsW(&kHidHideInterfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_ITEMS && limits)
            limits->append(win32Error(error, QStringLiteral("SetupDiGetClassDevsW(HidHide interface)")).message);
        return paths;
    }
    const auto destroy = qScopeGuard([&] { SetupDiDestroyDeviceInfoList(devices); });
    for (DWORD index = 0; index < 16; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &kHidHideInterfaceGuid, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS && limits)
                limits->append(win32Error(error, QStringLiteral("SetupDiEnumDeviceInterfaces(HidHide)")).message);
            break;
        }
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &bytes, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            if (limits) limits->append(win32Error(GetLastError(), QStringLiteral("SetupDiGetDeviceInterfaceDetailW(HidHide size)")).message);
            continue;
        }
        QVector<BYTE> buffer(static_cast<qsizetype>(bytes));
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, bytes, nullptr, nullptr)) {
            if (limits) limits->append(win32Error(GetLastError(), QStringLiteral("SetupDiGetDeviceInterfaceDetailW(HidHide payload)")).message);
            continue;
        }
        paths.append(fromWide(detail->DevicePath));
    }
    paths.removeDuplicates();
    return paths;
}

QList<ProtocolObservation> observeProtocol(const QStringList &interfacePaths, std::atomic_bool *cancelled)
{
    QList<ProtocolObservation> observations;
    if (cancelled && cancelled->load()) return observations;
    HANDLE device = INVALID_HANDLE_VALUE;
    QString openedPath;
    QString fallbackInterfacePath;
    // Prefer the provider's published interface, then use the documented
    // stable DOS path as a compatibility fallback.  Both handles are opened
    // read-only and attempts are retained independently in the evidence.
    for (const QString &candidate : interfacePaths) {
        HANDLE interfaceDevice = CreateFileW(reinterpret_cast<LPCWSTR>(candidate.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (interfaceDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(interfaceDevice);
            fallbackInterfacePath = candidate;
            observations.append({QStringLiteral("OPEN_INTERFACE_GUID"), DoctorCheckStatus::Healthy,
                QStringLiteral("Published HidHide interface opened with GENERIC_READ"), {}, std::nullopt, 0, false});
            break;
        }
        observations.append({QStringLiteral("OPEN_INTERFACE_GUID"),
            GetLastError() == ERROR_ACCESS_DENIED ? DoctorCheckStatus::PermissionLimited : DoctorCheckStatus::Failed,
            {}, {}, win32Error(GetLastError(), QStringLiteral("CreateFileW(HidHide interface, GENERIC_READ)")), 0, false});
    }
    // The stable name is the upstream client's documented control endpoint.
    // Some installed driver generations publish a SetupAPI interface that is
    // openable but does not route the HidHide IOCTL contract; retain that
    // observation above and prefer the compatible stable endpoint for GETs.
    device = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    openedPath = QStringLiteral("\\\\.\\HidHide");
    if (device == INVALID_HANDLE_VALUE && !fallbackInterfacePath.isEmpty()) {
        device = CreateFileW(reinterpret_cast<LPCWSTR>(fallbackInterfacePath.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        openedPath = fallbackInterfacePath;
    }
    if (device == INVALID_HANDLE_VALUE) {
        const NativeError failure = win32Error(GetLastError(), QStringLiteral("CreateFileW(\\\\.\\HidHide, GENERIC_READ)"));
        observations.append({QStringLiteral("OPEN_CONTROL"),
            failure.code == ERROR_ACCESS_DENIED ? DoctorCheckStatus::PermissionLimited : DoctorCheckStatus::Failed,
            {}, {}, failure, 0, false});
        return observations;
    }
    const auto close = qScopeGuard([&] { CloseHandle(device); });
    observations.append({QStringLiteral("OPEN_CONTROL"), DoctorCheckStatus::Healthy,
        QStringLiteral("%1; GENERIC_READ; shared read/write/delete; overlapped").arg(openedPath), {}, std::nullopt, 0, false});
    observations.append(boolProtocolQuery(device, kIoctlGetActive, QStringLiteral("GET_ACTIVE")));
    if (!cancelled || !cancelled->load()) observations.append(boolProtocolQuery(device, kIoctlGetActive, QStringLiteral("GET_ACTIVE_REPEAT_2")));
    if (!cancelled || !cancelled->load()) observations.append(boolProtocolQuery(device, kIoctlGetActive, QStringLiteral("GET_ACTIVE_REPEAT_3")));
    if (!cancelled || !cancelled->load()) observations.append(boolProtocolQuery(device, kIoctlGetInverse, QStringLiteral("GET_INVERSE")));
    if (!cancelled || !cancelled->load()) observations.append(multiStringProtocolQuery(device, kIoctlGetWhitelist, QStringLiteral("GET_WHITELIST")));
    if (!cancelled || !cancelled->load()) observations.append(multiStringProtocolQuery(device, kIoctlGetWhitelist, QStringLiteral("GET_WHITELIST_REPEAT")));
    if (!cancelled || !cancelled->load()) observations.append(multiStringProtocolQuery(device, kIoctlGetBlacklist, QStringLiteral("GET_BLACKLIST")));
    if (!cancelled || !cancelled->load()) observations.append(multiStringProtocolQuery(device, kIoctlGetBlacklist, QStringLiteral("GET_BLACKLIST_REPEAT")));
    return observations;
}

DeviceClassification classifyDevice(const DeviceObservation &device)
{
    const QString fingerprint = (device.instanceId + QLatin1Char(' ') + device.friendlyName + QLatin1Char(' ')
        + device.manufacturer + QLatin1Char(' ') + device.hardwareIds.join(QChar::Space)).toUpper();
    if (!device.present) return DeviceClassification::StaleOrPhantom;
    if (device.problemCode != 0) return DeviceClassification::ProblemDevice;
    if (fingerprint.contains(QStringLiteral("VJOY")) || fingerprint.contains(QStringLiteral("VID_1234"))) return DeviceClassification::VJoyVirtualOutput;
    if (fingerprint.contains(QStringLiteral("VIRTUAL")) || fingerprint.contains(QStringLiteral("ROOT\\"))) return DeviceClassification::VirtualGamingDevice;
    if (fingerprint.contains(QStringLiteral("HID\\")) || fingerprint.contains(QStringLiteral("USB\\"))) return DeviceClassification::PhysicalGamingInput;
    return DeviceClassification::Unknown;
}

QList<DeviceObservation> observeDevices(std::atomic_bool *cancelled)
{
    QList<DeviceObservation> devices;
    HDEVINFO deviceInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (deviceInfo == INVALID_HANDLE_VALUE) return devices;
    const auto destroy = qScopeGuard([&] { SetupDiDestroyDeviceInfoList(deviceInfo); });
    for (DWORD index = 0; index < kMaximumDevices; ++index) {
        if (cancelled && cancelled->load()) break;
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(deviceInfo, index, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        wchar_t instance[1024]{};
        if (!SetupDiGetDeviceInstanceIdW(deviceInfo, &data, instance, static_cast<DWORD>(std::size(instance)), nullptr)) continue;
        DeviceObservation device;
        device.instanceId = fromWide(instance);
        const QString uppercaseId = device.instanceId.toUpper();
        // Keep the raw doctor inventory focused and bounded.  This is a
        // predicate on structured PnP identity, never a friendly-name guess.
        if (!uppercaseId.contains(QStringLiteral("HID")) && !uppercaseId.contains(QStringLiteral("VJOY"))
            && !uppercaseId.contains(QStringLiteral("XUSB")) && !uppercaseId.contains(QStringLiteral("HIDHIDE"))) continue;
        device.present = true;
        device.friendlyName = readStringProperty(deviceInfo, &data, SPDRP_FRIENDLYNAME, &device.propertyFailures).value(0);
        if (device.friendlyName.isEmpty()) device.friendlyName = readStringProperty(deviceInfo, &data, SPDRP_DEVICEDESC, &device.propertyFailures).value(0);
        device.manufacturer = readStringProperty(deviceInfo, &data, SPDRP_MFG, &device.propertyFailures).value(0);
        device.className = readStringProperty(deviceInfo, &data, SPDRP_CLASS, &device.propertyFailures).value(0);
        device.hardwareIds = readStringProperty(deviceInfo, &data, SPDRP_HARDWAREID, &device.propertyFailures);
        device.compatibleIds = readStringProperty(deviceInfo, &data, SPDRP_COMPATIBLEIDS, &device.propertyFailures);
        device.location = readStringProperty(deviceInfo, &data, SPDRP_LOCATION_INFORMATION, &device.propertyFailures).value(0);
        device.containerId = readGuidProperty(deviceInfo, &data, kDeviceContainerId, &device.propertyFailures);
        const QString driverKey = readStringProperty(deviceInfo, &data, SPDRP_DRIVER, &device.propertyFailures).value(0);
        if (!driverKey.isEmpty()) {
            const QString classPath = QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Class\\") + driverKey;
            device.driverProvider = registryString(readRegistryValue(HKEY_LOCAL_MACHINE, classPath, QStringLiteral("ProviderName")));
            device.driverVersion = registryString(readRegistryValue(HKEY_LOCAL_MACHINE, classPath, QStringLiteral("DriverVersion")));
        }
        ULONG status = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) == CR_SUCCESS) {
            device.statusFlags = status;
            device.problemCode = problem;
        } else {
            device.propertyFailures.append(QStringLiteral("CM_Get_DevNode_Status"));
        }
        device.classification = classifyDevice(device);
        devices.append(std::move(device));
    }
    return devices;
}

void observeHidInterfaces(QList<DeviceObservation> &devices, std::atomic_bool *cancelled)
{
    HDEVINFO interfaces = SetupDiGetClassDevsW(&kHidDeviceInterfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (interfaces == INVALID_HANDLE_VALUE) return;
    const auto destroy = qScopeGuard([&] { SetupDiDestroyDeviceInfoList(interfaces); });
    for (DWORD index = 0; index < kMaximumDevices; ++index) {
        if (cancelled && cancelled->load()) break;
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(interfaces, nullptr, &kHidDeviceInterfaceGuid, index, &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(interfaces, &interfaceData, nullptr, 0, &bytes, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        QVector<BYTE> buffer(static_cast<qsizetype>(bytes));
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(deviceData);
        if (!SetupDiGetDeviceInterfaceDetailW(interfaces, &interfaceData, detail, bytes, nullptr, &deviceData)) continue;
        wchar_t instance[1024]{};
        if (!SetupDiGetDeviceInstanceIdW(interfaces, &deviceData, instance, static_cast<DWORD>(std::size(instance)), nullptr)) continue;
        const QString instanceId = fromWide(instance);
        auto target = std::find_if(devices.begin(), devices.end(), [&](const DeviceObservation &device) {
            return device.instanceId.compare(instanceId, Qt::CaseInsensitive) == 0;
        });
        if (target == devices.end()) continue;
        const QString path = fromWide(detail->DevicePath);
        target->interfacePaths.append(path);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const NativeError error = win32Error(GetLastError(), QStringLiteral("CreateFileW(HID interface, GENERIC_READ)"));
            target->propertyFailures.append(QStringLiteral("HID_INTERFACE_OPEN:%1").arg(error.code));
            if (!target->nativeError) target->nativeError = error;
            continue;
        }
        const auto close = qScopeGuard([&] { CloseHandle(handle); });
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(handle, &preparsed)) {
            const NativeError error = win32Error(GetLastError(), QStringLiteral("HidD_GetPreparsedData"));
            target->propertyFailures.append(QStringLiteral("HID_PREPARSED:%1").arg(error.code));
            if (!target->nativeError) target->nativeError = error;
            continue;
        }
        const auto freePreparsed = qScopeGuard([&] { HidD_FreePreparsedData(preparsed); });
        HIDP_CAPS caps{};
        const NTSTATUS result = HidP_GetCaps(preparsed, &caps);
        if (result != HIDP_STATUS_SUCCESS) {
            target->propertyFailures.append(QStringLiteral("HidP_GetCaps:0x%1").arg(static_cast<quint32>(result), 0, 16));
            continue;
        }
        target->usagePage = caps.UsagePage;
        target->usage = caps.Usage;
    }
    for (DeviceObservation &device : devices) device.interfacePaths.removeDuplicates();
}

QList<ProcessObservation> observeRelevantProcesses()
{
    QList<ProcessObservation> processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        processes.append({{}, 0, {}, win32Error(GetLastError(), QStringLiteral("CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)"))});
        return processes;
    }
    const auto close = qScopeGuard([&] { CloseHandle(snapshot); });
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        processes.append({{}, 0, {}, win32Error(GetLastError(), QStringLiteral("Process32FirstW"))});
        return processes;
    }
    do {
        const QString image = fromWide(entry.szExeFile);
        if (mentionsHidHide(image) || image.compare(QStringLiteral("HOTAS BF6.exe"), Qt::CaseInsensitive) == 0)
            processes.append({image, entry.th32ProcessID, {}, std::nullopt});
    } while (Process32NextW(snapshot, &entry));
    const DWORD finish = GetLastError();
    if (finish != ERROR_NO_MORE_FILES)
        processes.append({{}, 0, {}, win32Error(finish, QStringLiteral("Process32NextW"))});
    return processes;
}

QList<EventObservation> observeSetupApiEvidence()
{
    QList<EventObservation> observations;
    const QString path = QDir(windowsDirectory()).filePath(QStringLiteral("INF/setupapi.dev.log"));
    QFile log(path);
    if (!log.open(QIODevice::ReadOnly)) {
        observations.append({QStringLiteral("SetupAPI"), {}, 0, {}, {}, {}, EvidenceSensitivity::SensitiveLocalOnly,
            win32Error(GetLastError(), QStringLiteral("Open SetupAPI.dev.log"))});
        return observations;
    }
    const qint64 offset = std::max<qint64>(0, log.size() - kMaxSetupApiExcerptChars * 2);
    log.seek(offset);
    const QByteArray bytes = log.readAll();
    const QString excerpt = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    const QStringList lines = excerpt.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!mentionsHidHide(line)) continue;
        observations.append({QStringLiteral("SetupAPI"), QStringLiteral("setupapi.dev.log"), 0,
            QStringLiteral("Informational"), {}, line.left(1024), EvidenceSensitivity::SensitiveLocalOnly, std::nullopt});
        if (observations.size() >= 32) break;
    }
    return observations;
}

QList<EventObservation> observeWerReports()
{
    QList<EventObservation> observations;
    const QString programData = knownFolder(FOLDERID_ProgramData);
    if (programData.isEmpty()) return observations;
    const QString archive = QDir(programData).filePath(QStringLiteral("Microsoft/Windows/WER/ReportArchive"));
    if (!QFileInfo(archive).isDir()) return observations;
    QDirIterator reports(archive, {QStringLiteral("*.wer")}, QDir::Files, QDirIterator::Subdirectories);
    while (reports.hasNext() && observations.size() < 32) {
        const QString path = reports.next();
        if (!mentionsHidHide(path)) continue;
        QFile report(path);
        if (!report.open(QIODevice::ReadOnly)) continue;
        const QString content = QString::fromUtf8(report.read(32 * 1024));
        observations.append({QStringLiteral("WER"), QStringLiteral("Windows Error Reporting"), 0,
            QStringLiteral("Crash report"), QFileInfo(path).lastModified().toUTC(),
            content.left(1024), EvidenceSensitivity::PotentiallyIdentifying, std::nullopt});
    }
    return observations;
}

QString renderEventXml(EVT_HANDLE event)
{
    DWORD bytes = 0;
    DWORD properties = 0;
    EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &bytes, &properties);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return {};
    QVector<wchar_t> buffer(static_cast<qsizetype>(bytes / sizeof(wchar_t) + 1));
    if (!EvtRender(nullptr, event, EvtRenderEventXml, bytes, buffer.data(), &bytes, &properties)) return {};
    return QString::fromWCharArray(buffer.constData());
}

QList<EventObservation> observeEventChannel(const QString &channel, const QString &query)
{
    QList<EventObservation> observations;
    EVT_HANDLE result = EvtQuery(nullptr, reinterpret_cast<LPCWSTR>(channel.utf16()),
        reinterpret_cast<LPCWSTR>(query.utf16()), EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!result) {
        observations.append({channel, {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction,
            win32Error(GetLastError(), QStringLiteral("EvtQuery"))});
        return observations;
    }
    const auto closeResult = qScopeGuard([&] { EvtClose(result); });
    for (DWORD count = 0; count < kMaximumEvents; ++count) {
        EVT_HANDLE event = nullptr;
        DWORD returned = 0;
        if (!EvtNext(result, 1, &event, 0, 0, &returned)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) observations.append({channel, {}, 0, {}, {}, {}, EvidenceSensitivity::RequiresRedaction,
                win32Error(GetLastError(), QStringLiteral("EvtNext"))});
            break;
        }
        const auto closeEvent = qScopeGuard([&] { EvtClose(event); });
        const QString xml = renderEventXml(event);
        if (!mentionsHidHide(xml)) continue;
        const QRegularExpression providerExpression(QStringLiteral("Provider Name=['\"]([^'\"]+)"));
        const QRegularExpression idExpression(QStringLiteral("EventID[^>]*>([0-9]+)"));
        const auto providerMatch = providerExpression.match(xml);
        const auto idMatch = idExpression.match(xml);
        observations.append({channel, providerMatch.hasMatch() ? providerMatch.captured(1) : QString(),
            idMatch.hasMatch() ? idMatch.captured(1).toUInt() : 0, QStringLiteral("Observed"), {},
            xml.left(2048), EvidenceSensitivity::RequiresRedaction, std::nullopt});
    }
    return observations;
}

QList<EventObservation> observeEventEvidence()
{
    const QString sevenDays = QStringLiteral("*[System[TimeCreated[timediff(@SystemTime) <= 604800000]]]");
    QList<EventObservation> observations = observeEventChannel(QStringLiteral("Application"), sevenDays);
    observations.append(observeEventChannel(QStringLiteral("System"), sevenDays));
    return observations;
}

void appendArtifactCandidates(ReadOnlyDiagnosticSnapshot &snapshot, DoctorArtifactKind kind,
    const QString &role, const QStringList &roots, const QString &fileName)
{
    QStringList paths = findNamedFiles(roots, fileName);
    if (paths.isEmpty() && !roots.isEmpty()) paths.append(QDir(roots.front()).filePath(fileName));
    for (const QString &path : paths) snapshot.artifacts.append(inspectArtifact(kind, role, path));
}

void observeDriverStore(ReadOnlyDiagnosticSnapshot &snapshot, std::atomic_bool *cancelled)
{
    const QString store = QDir(windowsDirectory()).filePath(QStringLiteral("System32/DriverStore/FileRepository"));
    if (!QFileInfo(store).isDir()) {
        snapshot.operationalLimits.append(QStringLiteral("Driver Store FileRepository was not available for read-only inspection."));
        return;
    }
    const QDir directory(store);
    const QFileInfoList candidates = directory.entryInfoList({QStringLiteral("hidhide*.inf*")}, QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &candidate : candidates) {
        if (cancelled && cancelled->load()) return;
        DriverPackageObservation package;
        package.infName = candidate.fileName();
        package.path = candidate.absoluteFilePath();
        package.provider = QStringLiteral("Provider unavailable");
        QDirIterator infs(package.path, {QStringLiteral("*.inf")}, QDir::Files, QDirIterator::Subdirectories);
        if (infs.hasNext()) {
            QFile inf(infs.next());
            if (inf.open(QIODevice::ReadOnly)) {
                const QString text = QString::fromUtf8(inf.read(64 * 1024));
                const QRegularExpression providerExpression(QStringLiteral("^\\s*Provider\\s*=\\s*(.+?)\\s*$"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
                const QRegularExpression versionExpression(QStringLiteral("^\\s*DriverVer\\s*=\\s*(.+?)\\s*$"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch provider = providerExpression.match(text);
                const QRegularExpressionMatch version = versionExpression.match(text);
                if (provider.hasMatch()) {
                    package.provider = provider.captured(1).trimmed();
                    // HidHide's official INF declares Provider as the string
                    // token %ManufacturerName%. Resolve that token only from
                    // the same signed package metadata; never substitute a
                    // provider merely because a path contains "HidHide".
                    if (package.provider.compare(QStringLiteral("%ManufacturerName%"), Qt::CaseInsensitive) == 0) {
                        const QRegularExpression manufacturerExpression(
                            QStringLiteral("^\\s*ManufacturerName\\s*=\\s*\\\"([^\\\"]+)\\\"\\s*$"),
                            QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
                        const QRegularExpressionMatch manufacturer = manufacturerExpression.match(text);
                        if (manufacturer.hasMatch()) package.provider = manufacturer.captured(1).trimmed();
                    }
                }
                if (version.hasMatch()) package.version = version.captured(1).trimmed();
            }
        }
        const QStringList drivers = findNamedFiles({package.path}, QStringLiteral("HidHide.sys"));
        if (!drivers.isEmpty()) {
            const FileArtifactObservation artifact = inspectArtifact(DoctorArtifactKind::DriverStorePackage,
                QStringLiteral("Driver Store HidHide.sys"), drivers.front());
            if (package.version.isEmpty()) package.version = artifact.fileVersion;
            package.architecture = artifact.architecture;
            snapshot.artifacts.append(artifact);
        }
        snapshot.driverPackages.append(std::move(package));
    }
}

void observeConfigurationRegistry(ReadOnlyDiagnosticSnapshot &snapshot)
{
    const QString key = QStringLiteral("SYSTEM\\CurrentControlSet\\Services\\HidHide\\Parameters");
    const RegistryRead whitelist = readRegistryValue(HKEY_LOCAL_MACHINE, key, QStringLiteral("WhitelistedFullImageNames"));
    const RegistryRead blacklist = readRegistryValue(HKEY_LOCAL_MACHINE, key, QStringLiteral("BlacklistedDeviceInstancePaths"));
    const RegistryRead active = readRegistryValue(HKEY_LOCAL_MACHINE, key, QStringLiteral("Active"));
    const RegistryRead inverse = readRegistryValue(HKEY_LOCAL_MACHINE, key, QStringLiteral("WhitelistedInverse"));
    snapshot.registryWhitelist = registryMultiString(whitelist);
    snapshot.registryBlacklist = registryMultiString(blacklist);
    if (const auto value = registryDword(active)) snapshot.registryActive = *value != 0;
    if (const auto value = registryDword(inverse)) snapshot.registryInverse = *value != 0;
    if (!whitelist.exists && whitelist.error && whitelist.error->code != ERROR_FILE_NOT_FOUND)
        snapshot.operationalLimits.append(whitelist.error->message);
}

void observeFilterRegistrations(ReadOnlyDiagnosticSnapshot &snapshot)
{
    const QString classRoot = QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Class");
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, reinterpret_cast<LPCWSTR>(classRoot.utf16()), 0,
        KEY_READ | KEY_WOW64_64KEY, &key);
    if (opened != ERROR_SUCCESS) {
        snapshot.hidHideFilterEnumerationError = win32Error(opened, QStringLiteral("RegOpenKeyExW(Control\\Class)"));
        return;
    }
    const auto close = qScopeGuard([&] { RegCloseKey(key); });
    for (DWORD index = 0; index < 512; ++index) {
        wchar_t subkey[256]{};
        DWORD length = static_cast<DWORD>(std::size(subkey));
        const LONG next = RegEnumKeyExW(key, index, subkey, &length, nullptr, nullptr, nullptr, nullptr);
        if (next == ERROR_NO_MORE_ITEMS) break;
        if (next != ERROR_SUCCESS) continue;
        const QString path = classRoot + QLatin1Char('\\') + QString::fromWCharArray(subkey, static_cast<int>(length));
        for (const QString &valueName : {QStringLiteral("UpperFilters"), QStringLiteral("LowerFilters")}) {
            const QStringList filters = registryMultiString(readRegistryValue(HKEY_LOCAL_MACHINE, path, valueName));
            for (const QString &filter : filters) {
                if (mentionsHidHide(filter)) snapshot.hidHideFilterRegistrations.append(QString::fromWCharArray(subkey, static_cast<int>(length))
                    + QLatin1Char('\\') + valueName + QLatin1Char('=') + filter);
            }
        }
    }
    snapshot.hidHideFilterRegistrations.removeDuplicates();
}

void populateCatalogObservations(ReadOnlyDiagnosticSnapshot &snapshot, const QStringList &installRoots)
{
    const auto presentArtifact = [&](DoctorArtifactKind kind) { return artifactFor(snapshot, kind); };
    const auto add = [&](const QString &id, DoctorCheckStatus status, const QString &summary,
                         const QString &technical = {}, std::optional<NativeError> error = std::nullopt) {
        addCatalogObservation(snapshot, id, status, summary, technical, std::move(error));
    };
    const auto protocolStatus = [&](const QString &id, const QString &operation, const QString &label) {
        const ProtocolObservation *probe = protocolFor(snapshot, operation);
        if (!probe) {
            add(id, snapshot.environment.hidhide.present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable,
                snapshot.environment.hidhide.present ? label + QStringLiteral(" was not observed.")
                                                      : QStringLiteral("HidHide is absent; %1 is not applicable.").arg(label));
            return;
        }
        add(id, probe->status, probe->value.isEmpty() ? label + QStringLiteral(" completed.") : probe->value,
            {}, probe->nativeError);
    };
    const auto hasDuplicate = [](const QStringList &values) {
        QSet<QString> normalized;
        for (const QString &value : values) {
            const QString key = QDir::cleanPath(value).toCaseFolded();
            if (normalized.contains(key)) return true;
            normalized.insert(key);
        }
        return false;
    };
    const auto listFor = [&](const QString &operation) -> QStringList {
        if (const ProtocolObservation *probe = protocolFor(snapshot, operation)) return probe->multiStringValues;
        return {};
    };

    // System and portability are individually sourced rather than being a
    // blanket "platform healthy" result.
    const PlatformFingerprint &platform = snapshot.environment.platform;
    const bool platformKnown = !platform.windowsEdition.isEmpty() && platform.build != 0;
    add(QStringLiteral("HD-SYS-001"), platformKnown ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown,
        QStringLiteral("Edition=%1; version=%2; build=%3.%4.").arg(platform.windowsEdition, platform.windowsVersion)
            .arg(platform.build).arg(platform.revision));
    add(QStringLiteral("HD-SYS-002"), platform.nativeArchitecture == CpuArchitecture::Unknown ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Healthy,
        QStringLiteral("Native=%1; Doctor process=%2.").arg(displayName(platform.nativeArchitecture), displayName(platform.processArchitecture)));
    add(QStringLiteral("HD-SYS-003"), DoctorCheckStatus::Informational,
        QStringLiteral("WOW64/emulation=%1.").arg(platform.wow64OrEmulated ? QStringLiteral("true") : QStringLiteral("false")));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime boot = now.addMSecs(-static_cast<qint64>(GetTickCount64()));
    add(QStringLiteral("HD-SYS-004"), DoctorCheckStatus::Healthy, QStringLiteral("Boot timestamp=%1; uptime=%2 ms.")
        .arg(boot.toString(Qt::ISODate), QString::number(GetTickCount64())));
    add(QStringLiteral("HD-SYS-005"), DoctorCheckStatus::Informational,
        QStringLiteral("Token was queried without elevation; current state=%1.").arg(snapshot.environment.privilege == PrivilegeState::Elevated
            ? QStringLiteral("elevated") : QStringLiteral("not elevated or permission-limited")));
    add(QStringLiteral("HD-SYS-006"), DoctorCheckStatus::Healthy,
        QStringLiteral("Doctor current elevation=%1.").arg(snapshot.environment.privilege == PrivilegeState::Elevated
            ? QStringLiteral("elevated") : (snapshot.environment.privilege == PrivilegeState::Unelevated ? QStringLiteral("unelevated") : QStringLiteral("permission-limited"))));
    const RegistryRead secureBoot = readRegistryValue(HKEY_LOCAL_MACHINE,
        QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State"), QStringLiteral("UEFISecureBootEnabled"));
    add(QStringLiteral("HD-SYS-007"), secureBoot.exists ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown,
        secureBoot.exists ? QStringLiteral("UEFI Secure Boot=%1.").arg(registryDword(secureBoot).value_or(0) ? QStringLiteral("enabled") : QStringLiteral("disabled"))
                          : QStringLiteral("Secure Boot state was unavailable through the read-only registry source."), {}, secureBoot.error);
    const RegistryRead startOptions = readRegistryValue(HKEY_LOCAL_MACHINE,
        QStringLiteral("SYSTEM\\CurrentControlSet\\Control"), QStringLiteral("SystemStartOptions"));
    const QString startOptionText = registryString(startOptions);
    add(QStringLiteral("HD-SYS-008"), startOptions.exists ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown,
        startOptions.exists ? QStringLiteral("Test-signing token present=%1.").arg(startOptionText.contains(QStringLiteral("TESTSIGNING"), Qt::CaseInsensitive)
            ? QStringLiteral("true") : QStringLiteral("false")) : QStringLiteral("Boot signing options were unavailable."), {}, startOptions.error);
    const RegistryRead hvci = readRegistryValue(HKEY_LOCAL_MACHINE,
        QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity"), QStringLiteral("Enabled"));
    add(QStringLiteral("HD-SYS-009"), hvci.exists ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown,
        hvci.exists ? QStringLiteral("Memory Integrity/HVCI=%1.").arg(registryDword(hvci).value_or(0) ? QStringLiteral("enabled") : QStringLiteral("disabled"))
                    : QStringLiteral("Memory Integrity state was unavailable."), {}, hvci.error);
    add(QStringLiteral("HD-SYS-010"), DoctorCheckStatus::Informational,
        QStringLiteral("Qualified pending-restart markers observed=%1.").arg(snapshot.pendingRestart.size()));
    const bool pendingRename = std::any_of(snapshot.pendingRestart.cbegin(), snapshot.pendingRestart.cend(),
        [](const PendingRestartObservation &marker) { return marker.source.contains(QStringLiteral("PendingFileRenameOperations")); });
    add(QStringLiteral("HD-SYS-011"), DoctorCheckStatus::Informational,
        QStringLiteral("Pending file rename/replace evidence=%1.").arg(pendingRename ? QStringLiteral("present") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-SYS-012"), (now.date().year() >= 2020 && now.date().year() <= 2100) ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning,
        QStringLiteral("Current UTC timestamp=%1.").arg(now.toString(Qt::ISODateWithMs)));
    add(QStringLiteral("HD-SYS-013"), DoctorCheckStatus::Unknown, QStringLiteral("No reliable local Windows Update history source was queried in Phase 1."));
    const RegistryRead restore = readRegistryValue(HKEY_LOCAL_MACHINE,
        QStringLiteral("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SystemRestore"), QStringLiteral("DisableSR"));
    add(QStringLiteral("HD-SYS-014"), restore.exists ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown,
        restore.exists ? QStringLiteral("System Restore disable marker=%1.").arg(registryDword(restore).value_or(0))
                       : QStringLiteral("System Restore policy state unavailable."), {}, restore.error);
    const QStorageInfo systemStorage = QStorageInfo::root();
    add(QStringLiteral("HD-SYS-015"), systemStorage.isValid() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown,
        systemStorage.isValid() ? QStringLiteral("System drive free bytes=%1.").arg(systemStorage.bytesAvailable())
                                : QStringLiteral("System-drive capacity unavailable."));
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    add(QStringLiteral("HD-SYS-016"), !tempRoot.isEmpty() && QFileInfo(tempRoot).isDir() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown,
        QStringLiteral("Report workspace resolved=%1; existing=%2.").arg(!tempRoot.isEmpty() ? QStringLiteral("true") : QStringLiteral("false"),
            QFileInfo(tempRoot).isDir() ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("HD-SYS-017"), DoctorCheckStatus::Healthy,
        QStringLiteral("Doctor version=%1.").arg(QCoreApplication::applicationVersion()));
    add(QStringLiteral("HD-SYS-018"), DoctorCheckStatus::Informational,
        QStringLiteral("Standalone process context; relevant running processes=%1.").arg(snapshot.processes.size()));

    add(QStringLiteral("HD-PORT-001"), !platform.windowsEdition.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown, platform.windowsEdition);
    add(QStringLiteral("HD-PORT-002"), !platform.windowsVersion.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown, platform.windowsVersion);
    add(QStringLiteral("HD-PORT-003"), platform.build ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown, QString::number(platform.build));
    add(QStringLiteral("HD-PORT-004"), platform.revision ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Informational, QString::number(platform.revision));
    add(QStringLiteral("HD-PORT-005"), platform.nativeArchitecture == CpuArchitecture::Unknown ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Healthy, displayName(platform.nativeArchitecture));
    add(QStringLiteral("HD-PORT-006"), platform.processArchitecture == CpuArchitecture::Unknown ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Healthy, displayName(platform.processArchitecture));
    add(QStringLiteral("HD-PORT-007"), DoctorCheckStatus::Informational, platform.wow64OrEmulated ? QStringLiteral("emulated/WOW64") : QStringLiteral("native process"));
    add(QStringLiteral("HD-PORT-008"), platformKnown ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown, QStringLiteral("Read-only diagnosis remains enabled."));
    add(QStringLiteral("HD-PORT-009"), DoctorCheckStatus::Informational, QStringLiteral("Highest qualified capability=diagnosis only."));
    const bool doctorArchMatches = platform.nativeArchitecture == platform.processArchitecture || platform.wow64OrEmulated;
    add(QStringLiteral("HD-PORT-010"), doctorArchMatches ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning, QStringLiteral("Doctor/native architecture comparison recorded."));
    add(QStringLiteral("HD-PORT-011"), DoctorCheckStatus::NotApplicable, QStringLiteral("No repair helper is present or invoked in Phase 1."));
    const CpuArchitecture packageArchitecture = snapshot.environment.hidhide.packageArchitecture;
    add(QStringLiteral("HD-PORT-012"), packageArchitecture == CpuArchitecture::Unknown ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational, displayName(packageArchitecture));
    add(QStringLiteral("HD-PORT-013"), packageArchitecture == CpuArchitecture::Unknown ? DoctorCheckStatus::Unknown
        : (packageArchitecture == platform.nativeArchitecture ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning),
        QStringLiteral("Package/native architecture comparison recorded."));
    add(QStringLiteral("HD-PORT-014"), DoctorCheckStatus::Healthy, QStringLiteral("Core scan does not depend on pre-existing HOTAS user state."));
    add(QStringLiteral("HD-PORT-015"), !platform.localeName.isEmpty() ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown, platform.localeName);
    add(QStringLiteral("HD-PORT-016"), DoctorCheckStatus::Unknown, QStringLiteral("DPI is not part of the headless evidence contract."));
    add(QStringLiteral("HD-PORT-017"), DoctorCheckStatus::Informational, QStringLiteral("Native Windows APIs only; no external utility is required."));
    add(QStringLiteral("HD-PORT-018"), DoctorCheckStatus::Healthy, QStringLiteral("Registry, SCM, SetupAPI, Event Log, and HidHide GET provider paths are available in this build."));
    add(QStringLiteral("HD-PORT-019"), snapshot.protocol.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational,
        QStringLiteral("Observed GET capabilities=%1.").arg(snapshot.environment.hidhide.protocolCapabilities.join(QStringLiteral(", "))));
    add(QStringLiteral("HD-PORT-020"), DoctorCheckStatus::Healthy, QStringLiteral("No CLI, PowerShell, WMI, or HidHideClient dependency was used."));
    add(QStringLiteral("HD-PORT-021"), DoctorCheckStatus::Informational, QStringLiteral("HOTAS BF6 running=%1.").arg(std::any_of(snapshot.processes.cbegin(), snapshot.processes.cend(),
        [](const ProcessObservation &process) { return process.imageName.compare(QStringLiteral("HOTAS BF6.exe"), Qt::CaseInsensitive) == 0; }) ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("HD-PORT-022"), DoctorCheckStatus::Healthy, QStringLiteral("Standalone executable owns its read-only provider path."));
    add(QStringLiteral("HD-PORT-023"), DoctorCheckStatus::Healthy, QStringLiteral("Install and Driver Store roots were dynamically resolved."));
    add(QStringLiteral("HD-PORT-024"), QFileInfo(QDir(windowsDirectory()).filePath(QStringLiteral("System32/DriverStore/FileRepository"))).isDir()
        ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown, QStringLiteral("Driver Store root dynamically resolved."));
    add(QStringLiteral("HD-PORT-025"), DoctorCheckStatus::Informational, QStringLiteral("Install roots considered=%1.").arg(installRoots.size()));
    const bool packageConflict = std::any_of(snapshot.driverPackages.cbegin(), snapshot.driverPackages.cend(), [&](const DriverPackageObservation &package) {
        return package.architecture != CpuArchitecture::Unknown && package.architecture != platform.nativeArchitecture; });
    add(QStringLiteral("HD-PORT-026"), packageConflict ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Multi-package architecture conflict=%1.").arg(packageConflict ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-PORT-027"), packageConflict ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Wrong-architecture remnants=%1.").arg(packageConflict ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-PORT-028"), platformKnown ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Warning, QStringLiteral("No repair path is enabled for unknown platform evidence."));
    add(QStringLiteral("HD-PORT-029"), DoctorCheckStatus::Informational, QStringLiteral("Future/unqualified builds remain diagnosis-only by policy."));
    add(QStringLiteral("HD-PORT-030"), DoctorCheckStatus::Healthy, QStringLiteral("Privacy-reviewed report contains build, architecture, and capability fingerprint only."));

    const FileArtifactObservation *client = presentArtifact(DoctorArtifactKind::ClientExecutable);
    const FileArtifactObservation *cli = presentArtifact(DoctorArtifactKind::CliExecutable);
    const FileArtifactObservation *driver = presentArtifact(DoctorArtifactKind::DriverBinary);
    const int existingArtifacts = std::count_if(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(),
        [](const FileArtifactObservation &artifact) { return artifact.exists; });
    const int existingRoots = std::count_if(installRoots.cbegin(), installRoots.cend(),
        [](const QString &root) { return QFileInfo(root).isDir(); });
    add(QStringLiteral("HD-INST-001"), installRoots.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Informational,
        QStringLiteral("Uninstall/dependency registry candidates=%1.").arg(installRoots.size()));
    add(QStringLiteral("HD-INST-002"), installRoots.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Informational,
        QStringLiteral("Registry/default install roots discovered=%1.").arg(installRoots.size()));
    add(QStringLiteral("HD-INST-003"), existingRoots ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Existing install roots=%1.").arg(existingRoots));
    add(QStringLiteral("HD-INST-004"), client ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, client ? QStringLiteral("HidHideClient candidate=%1.").arg(client->fileVersion) : QStringLiteral("No HidHideClient executable discovered."));
    add(QStringLiteral("HD-INST-005"), cli ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, cli ? QStringLiteral("HidHideCLI candidate=%1.").arg(cli->fileVersion) : QStringLiteral("No HidHideCLI executable discovered."));
    const bool watchdog = std::any_of(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(), [](const FileArtifactObservation &artifact) { return artifact.role.contains(QStringLiteral("Watchdog")) && artifact.exists; });
    add(QStringLiteral("HD-INST-006"), watchdog ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, watchdog ? QStringLiteral("Watchdog/setup candidate discovered.") : QStringLiteral("No optional Watchdog/setup candidate discovered."));
    add(QStringLiteral("HD-INST-007"), existingRoots > 1 ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Existing roots=%1.").arg(existingRoots));
    add(QStringLiteral("HD-INST-008"), installRoots.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Informational, QStringLiteral("ARP/uninstall candidates were read-only enumerated."));
    add(QStringLiteral("HD-INST-009"), DoctorCheckStatus::Unknown, QStringLiteral("MSI product identity was not exposed by the discovered read-only registration."));
    add(QStringLiteral("HD-INST-010"), existingArtifacts ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Component timestamps observed=%1.").arg(existingArtifacts));
    const bool artifactArchMismatch = std::any_of(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(), [&](const FileArtifactObservation &artifact) { return artifact.exists && artifact.architecture != CpuArchitecture::Unknown && artifact.architecture != platform.nativeArchitecture; });
    add(QStringLiteral("HD-INST-011"), artifactArchMismatch ? DoctorCheckStatus::Warning : (existingArtifacts ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable), QStringLiteral("Payload architecture mismatch=%1.").arg(artifactArchMismatch ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("HD-INST-012"), installRoots.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Informational, QStringLiteral("Uninstall roots were checked for plausible directories."));
    add(QStringLiteral("HD-INST-013"), installRoots.isEmpty() || existingArtifacts ? DoctorCheckStatus::Informational : DoctorCheckStatus::Warning, QStringLiteral("Metadata/payload relationship recorded."));
    add(QStringLiteral("HD-INST-014"), existingArtifacts && installRoots.isEmpty() ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Payload without installer metadata=%1.").arg(existingArtifacts && installRoots.isEmpty() ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-INST-015"), DoctorCheckStatus::Unknown, QStringLiteral("Provider identity requires a signed package/INF correlation not inferred in Phase 1."));
    add(QStringLiteral("HD-INST-016"), DoctorCheckStatus::Unknown, QStringLiteral("License/readme provenance is not inferred from file names."));

    add(QStringLiteral("HD-PKG-001"), client ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, client ? QStringLiteral("File version=%1.").arg(client->fileVersion) : QStringLiteral("Client absent."));
    add(QStringLiteral("HD-PKG-002"), cli ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, cli ? QStringLiteral("File version=%1.").arg(cli->fileVersion) : QStringLiteral("CLI absent."));
    add(QStringLiteral("HD-PKG-003"), driver ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, driver ? QStringLiteral("HidHide.sys candidate found.") : QStringLiteral("No loaded-path HidHide.sys candidate found."));
    add(QStringLiteral("HD-PKG-004"), driver ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, driver ? QStringLiteral("Driver file version=%1.").arg(driver->fileVersion) : QStringLiteral("Driver candidate absent."));
    add(QStringLiteral("HD-PKG-005"), snapshot.driverPackages.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Healthy, QStringLiteral("Driver Store packages=%1.").arg(snapshot.driverPackages.size()));
    add(QStringLiteral("HD-PKG-006"), snapshot.driverPackages.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational, QStringLiteral("Active package resolution retained as Driver Store candidate evidence."));
    add(QStringLiteral("HD-PKG-007"), snapshot.driverPackages.size() > 1 ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Relevant Driver Store packages=%1.").arg(snapshot.driverPackages.size()));
    const auto signatureSummary = [](const FileArtifactObservation *artifact) { return artifact ? displayName(artifact->signatureTrust) : QStringLiteral("not applicable"); };
    add(QStringLiteral("HD-PKG-008"), client ? (client->signatureTrust == SignatureTrustState::Trusted ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable, QStringLiteral("Client signature=%1.").arg(signatureSummary(client)));
    add(QStringLiteral("HD-PKG-009"), cli ? (cli->signatureTrust == SignatureTrustState::Trusted ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable, QStringLiteral("CLI signature=%1.").arg(signatureSummary(cli)));
    add(QStringLiteral("HD-PKG-010"), driver ? (driver->signatureTrust == SignatureTrustState::Trusted ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable, QStringLiteral("Driver signature=%1.").arg(signatureSummary(driver)));
    add(QStringLiteral("HD-PKG-011"), DoctorCheckStatus::Unknown, QStringLiteral("Installer package itself was not retained as an installed artifact."));
    add(QStringLiteral("HD-PKG-012"), existingArtifacts ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, QStringLiteral("SHA-256 inventory count=%1.").arg(existingArtifacts));
    add(QStringLiteral("HD-PKG-013"), DoctorCheckStatus::Unknown, QStringLiteral("Expected payload manifest is deliberately not assumed without a version-qualified source."));
    add(QStringLiteral("HD-PKG-014"), DoctorCheckStatus::Unknown, QStringLiteral("Extra payload determination requires a version-qualified layout manifest."));
    add(QStringLiteral("HD-PKG-015"), existingArtifacts ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable, QStringLiteral("PE architecture observed for component candidates."));
    add(QStringLiteral("HD-PKG-016"), driver ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown, QStringLiteral("Loaded-path candidate version=%1.").arg(driver ? driver->fileVersion : QString()));
    add(QStringLiteral("HD-PKG-017"), driver && !snapshot.driverPackages.isEmpty() ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown, QStringLiteral("Driver/package version correlation retained without diagnosis."));
    add(QStringLiteral("HD-PKG-018"), client && cli ? (client->fileVersion == cli->fileVersion ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable, QStringLiteral("Client/CLI versions compared when both are present."));
    add(QStringLiteral("HD-PKG-019"), DoctorCheckStatus::NotApplicable, QStringLiteral("Protocol-era compatibility matrix is deferred until a qualified source is available."));
    add(QStringLiteral("HD-PKG-020"), DoctorCheckStatus::NotApplicable, QStringLiteral("Install-layout conformance requires a version-qualified provider manifest."));

    const DeviceObservation *rootDevice = nullptr;
    for (const DeviceObservation &device : snapshot.devices) {
        if (device.instanceId.startsWith(QStringLiteral("ROOT\\HIDHIDE"), Qt::CaseInsensitive)) { rootDevice = &device; break; }
    }
    const ProtocolObservation *open = protocolFor(snapshot, QStringLiteral("OPEN_CONTROL"));
    add(QStringLiteral("HD-DRV-001"), rootDevice ? DoctorCheckStatus::Healthy : (snapshot.environment.hidhide.present ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable),
        rootDevice ? QStringLiteral("Root HidHide PnP node observed.") : QStringLiteral("Root HidHide PnP node was not observed."));
    add(QStringLiteral("HD-DRV-002"), rootDevice ? (rootDevice->problemCode ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        rootDevice ? QStringLiteral("PnP status=0x%1; problem=%2.").arg(rootDevice->statusFlags, 0, 16).arg(rootDevice->problemCode) : QStringLiteral("No root device to assess."));
    add(QStringLiteral("HD-DRV-003"), snapshot.service.present ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable,
        snapshot.service.present ? QStringLiteral("HidHide service is registered.") : QStringLiteral("HidHide service is absent."), {}, snapshot.service.nativeError);
    add(QStringLiteral("HD-DRV-004"), snapshot.service.present ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Service start type=%1.").arg(snapshot.service.startType));
    add(QStringLiteral("HD-DRV-005"), snapshot.service.present ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Service current state=%1.").arg(snapshot.service.currentState));
    add(QStringLiteral("HD-DRV-006"), snapshot.service.present && !snapshot.service.binaryPath.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Unknown,
        snapshot.service.binaryPath.isEmpty() ? QStringLiteral("Service binary path was unavailable.") : QStringLiteral("Service binary path observed."));
    protocolStatus(QStringLiteral("HD-DRV-007"), QStringLiteral("OPEN_CONTROL"), QStringLiteral("Control-device read-only open"));
    const ProtocolObservation *interfaceOpen = protocolFor(snapshot, QStringLiteral("OPEN_INTERFACE_GUID"));
    add(QStringLiteral("HD-DRV-008"), snapshot.hidHideInterfacePaths.isEmpty() ? DoctorCheckStatus::NotApplicable
            : (interfaceOpen ? interfaceOpen->status : DoctorCheckStatus::Unknown),
        snapshot.hidHideInterfacePaths.isEmpty() ? QStringLiteral("No HidHide interface was published through SetupAPI.")
            : QStringLiteral("Published HidHide interface paths=%1.").arg(snapshot.hidHideInterfacePaths.size()), {}, interfaceOpen ? interfaceOpen->nativeError : std::nullopt);
    const int filterCount = snapshot.hidHideFilterRegistrations.size();
    add(QStringLiteral("HD-DRV-009"), snapshot.hidHideFilterEnumerationError ? DoctorCheckStatus::PermissionLimited : DoctorCheckStatus::Informational,
        QStringLiteral("HidHide HID-class Upper/Lower filter entries=%1.").arg(filterCount), {}, snapshot.hidHideFilterEnumerationError);
    add(QStringLiteral("HD-DRV-010"), snapshot.hidHideFilterEnumerationError ? DoctorCheckStatus::PermissionLimited : DoctorCheckStatus::Informational,
        QStringLiteral("HidHide XUSB/XInput-related filter entries require class evidence; total registered HidHide filters=%1.").arg(filterCount), {}, snapshot.hidHideFilterEnumerationError);
    add(QStringLiteral("HD-DRV-011"), filterCount ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Filter ordering is not inferred without a provider/version-qualified ordering rule."));
    add(QStringLiteral("HD-DRV-012"), filterCount && !snapshot.service.present ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational,
        QStringLiteral("HidHide filter registration/service relationship recorded; service present=%1.").arg(snapshot.service.present ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("HD-DRV-013"), driver ? DoctorCheckStatus::Informational : DoctorCheckStatus::Unknown,
        QStringLiteral("Loaded-path candidate version=%1.").arg(driver ? driver->fileVersion : QString()));
    add(QStringLiteral("HD-DRV-014"), snapshot.driverPackages.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational,
        QStringLiteral("Driver Store provider metadata candidates=%1.").arg(snapshot.driverPackages.size()));
    add(QStringLiteral("HD-DRV-015"), snapshot.driverPackages.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational,
        QStringLiteral("Driver Store date/version candidates=%1.").arg(snapshot.driverPackages.size()));
    add(QStringLiteral("HD-DRV-016"), snapshot.service.present ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Current replacement/reboot context markers=%1.").arg(snapshot.pendingRestart.size()));
    const int devicePropertyFailures = std::accumulate(snapshot.devices.cbegin(), snapshot.devices.cend(), 0,
        [](int total, const DeviceObservation &device) { return total + device.propertyFailures.size(); });
    add(QStringLiteral("HD-DRV-017"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy,
        QStringLiteral("SetupAPI/CM device property failures=%1.").arg(devicePropertyFailures));
    const qint64 openCode = open && open->nativeError ? open->nativeError->code : 0;
    add(QStringLiteral("HD-DRV-018"), openCode == ERROR_ACCESS_DENIED ? DoctorCheckStatus::PermissionLimited : DoctorCheckStatus::NotApplicable,
        openCode == ERROR_ACCESS_DENIED ? QStringLiteral("Control open was access denied.") : QStringLiteral("No access-denied control-open result observed."), {}, open ? open->nativeError : std::nullopt);
    add(QStringLiteral("HD-DRV-019"), openCode == ERROR_FILE_NOT_FOUND ? DoctorCheckStatus::Failed : DoctorCheckStatus::NotApplicable,
        openCode == ERROR_FILE_NOT_FOUND ? QStringLiteral("Control path was not found.") : QStringLiteral("No path-not-found control-open result observed."), {}, open ? open->nativeError : std::nullopt);
    add(QStringLiteral("HD-DRV-020"), open && open->status == DoctorCheckStatus::TimedOut ? DoctorCheckStatus::TimedOut : DoctorCheckStatus::NotApplicable,
        open && open->status == DoctorCheckStatus::TimedOut ? QStringLiteral("Control open timed out.") : QStringLiteral("No control-open timeout observed."), {}, open ? open->nativeError : std::nullopt);
    add(QStringLiteral("HD-DRV-021"), DoctorCheckStatus::Unknown, QStringLiteral("Driver ETW provider manifest availability was not inferred from provider names."));
    add(QStringLiteral("HD-DRV-022"), DoctorCheckStatus::Unknown, QStringLiteral("Optional driver counter source availability was not observed."));

    protocolStatus(QStringLiteral("HD-API-001"), QStringLiteral("OPEN_CONTROL"), QStringLiteral("Control-device baseline open"));
    protocolStatus(QStringLiteral("HD-API-002"), QStringLiteral("GET_ACTIVE"), QStringLiteral("GET_ACTIVE"));
    protocolStatus(QStringLiteral("HD-API-003"), QStringLiteral("GET_INVERSE"), QStringLiteral("GET_INVERSE"));
    protocolStatus(QStringLiteral("HD-API-004"), QStringLiteral("GET_WHITELIST_SIZE"), QStringLiteral("GET_WHITELIST size query"));
    protocolStatus(QStringLiteral("HD-API-005"), QStringLiteral("GET_WHITELIST"), QStringLiteral("GET_WHITELIST payload query"));
    const ProtocolObservation *whitelist = protocolFor(snapshot, QStringLiteral("GET_WHITELIST"));
    add(QStringLiteral("HD-API-006"), whitelist ? whitelist->status : (snapshot.environment.hidhide.present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable),
        whitelist ? QStringLiteral("MULTI_SZ structural validation follows the payload operation.") : QStringLiteral("Whitelist payload was unavailable."), {}, whitelist ? whitelist->nativeError : std::nullopt);
    protocolStatus(QStringLiteral("HD-API-007"), QStringLiteral("GET_BLACKLIST_SIZE"), QStringLiteral("GET_BLACKLIST size query"));
    protocolStatus(QStringLiteral("HD-API-008"), QStringLiteral("GET_BLACKLIST"), QStringLiteral("GET_BLACKLIST payload query"));
    const ProtocolObservation *blacklist = protocolFor(snapshot, QStringLiteral("GET_BLACKLIST"));
    add(QStringLiteral("HD-API-009"), blacklist ? blacklist->status : (snapshot.environment.hidhide.present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable),
        blacklist ? QStringLiteral("MULTI_SZ structural validation follows the payload operation.") : QStringLiteral("Blacklist payload was unavailable."), {}, blacklist ? blacklist->nativeError : std::nullopt);
    add(QStringLiteral("HD-API-010"), DoctorCheckStatus::NotApplicable, QStringLiteral("Session-blacklist SET capability is intentionally never probed by this GET-only engine."));
    add(QStringLiteral("HD-API-011"), DoctorCheckStatus::NotApplicable, QStringLiteral("Session-clear SET capability is intentionally never probed by this GET-only engine."));
    qint64 protocolDuration = 0;
    for (const ProtocolObservation &probe : snapshot.protocol) protocolDuration += probe.durationMs;
    add(QStringLiteral("HD-API-012"), snapshot.protocol.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Informational,
        QStringLiteral("Independent GET operation cumulative duration=%1 ms.").arg(protocolDuration));
    const ProtocolObservation *activeFirst = protocolFor(snapshot, QStringLiteral("GET_ACTIVE"));
    const ProtocolObservation *activeSecond = protocolFor(snapshot, QStringLiteral("GET_ACTIVE_REPEAT_2"));
    const ProtocolObservation *activeThird = protocolFor(snapshot, QStringLiteral("GET_ACTIVE_REPEAT_3"));
    const bool activeRepeatOk = activeFirst && activeSecond && activeThird && activeFirst->status == DoctorCheckStatus::Healthy
        && activeSecond->status == DoctorCheckStatus::Healthy && activeThird->status == DoctorCheckStatus::Healthy
        && activeFirst->value == activeSecond->value && activeFirst->value == activeThird->value;
    add(QStringLiteral("HD-API-013"), activeRepeatOk ? DoctorCheckStatus::Healthy : (activeFirst ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable),
        QStringLiteral("Three bounded GET_ACTIVE reads were %1.").arg(activeRepeatOk ? QStringLiteral("consistent") : QStringLiteral("not consistently observed")));
    const ProtocolObservation *whitelistRepeat = protocolFor(snapshot, QStringLiteral("GET_WHITELIST_REPEAT"));
    add(QStringLiteral("HD-API-014"), whitelist && whitelistRepeat && whitelist->status == DoctorCheckStatus::Healthy && whitelistRepeat->status == DoctorCheckStatus::Healthy
            && whitelist->multiStringValues == whitelistRepeat->multiStringValues ? DoctorCheckStatus::Healthy : (whitelist ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable),
        QStringLiteral("Bounded whitelist GET repeat was compared without mutation."));
    const ProtocolObservation *blacklistRepeat = protocolFor(snapshot, QStringLiteral("GET_BLACKLIST_REPEAT"));
    add(QStringLiteral("HD-API-015"), blacklist && blacklistRepeat && blacklist->status == DoctorCheckStatus::Healthy && blacklistRepeat->status == DoctorCheckStatus::Healthy
            && blacklist->multiStringValues == blacklistRepeat->multiStringValues ? DoctorCheckStatus::Healthy : (blacklist ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable),
        QStringLiteral("Bounded blacklist GET repeat was compared without mutation."));
    add(QStringLiteral("HD-API-016"), DoctorCheckStatus::NotApplicable, QStringLiteral("Unknown IOCTLs are never issued by the documented GET-only safety policy."));
    add(QStringLiteral("HD-API-017"), DoctorCheckStatus::NotApplicable, QStringLiteral("Client/CLI behavior is not launched or required for direct API evidence."));
    add(QStringLiteral("HD-API-018"), DoctorCheckStatus::NotApplicable, QStringLiteral("CLI is not launched by the read-only Doctor."));
    add(QStringLiteral("HD-API-019"), DoctorCheckStatus::NotApplicable, QStringLiteral("CLI app-list command is not launched by the read-only Doctor."));
    add(QStringLiteral("HD-API-020"), DoctorCheckStatus::NotApplicable, QStringLiteral("CLI dev-list command is not launched by the read-only Doctor."));
    add(QStringLiteral("HD-API-021"), DoctorCheckStatus::NotApplicable, QStringLiteral("CLI dev-gaming command is not launched by the read-only Doctor."));
    const bool clientCrash = std::any_of(snapshot.events.cbegin(), snapshot.events.cend(), [](const EventObservation &event) { return event.summary.contains(QStringLiteral("HidHideClient"), Qt::CaseInsensitive); })
        || std::any_of(snapshot.werReports.cbegin(), snapshot.werReports.cend(), [](const EventObservation &event) { return event.summary.contains(QStringLiteral("HidHideClient"), Qt::CaseInsensitive); });
    add(QStringLiteral("HD-API-022"), clientCrash ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational,
        QStringLiteral("Existing HidHideClient crash evidence=%1.").arg(clientCrash ? QStringLiteral("present") : QStringLiteral("not observed")));
    const bool preservesErrors = std::all_of(snapshot.protocol.cbegin(), snapshot.protocol.cend(), [](const ProtocolObservation &probe) {
        return probe.status == DoctorCheckStatus::Healthy || probe.status == DoctorCheckStatus::Informational || probe.nativeError.has_value(); });
    add(QStringLiteral("HD-API-023"), preservesErrors ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning,
        QStringLiteral("Every failed/timed-out observed GET operation retained a native error=%1.").arg(preservesErrors ? QStringLiteral("true") : QStringLiteral("false")));
    add(QStringLiteral("HD-API-024"), snapshot.protocol.isEmpty() ? DoctorCheckStatus::Unknown : DoctorCheckStatus::Healthy,
        QStringLiteral("Capability fingerprint=%1.").arg(snapshot.environment.hidhide.protocolCapabilities.join(QStringLiteral(", "))));

    const QStringList apiWhitelist = listFor(QStringLiteral("GET_WHITELIST"));
    const QStringList apiBlacklist = listFor(QStringLiteral("GET_BLACKLIST"));
    protocolStatus(QStringLiteral("HD-CFG-001"), QStringLiteral("GET_ACTIVE"), QStringLiteral("Active/cloak state"));
    protocolStatus(QStringLiteral("HD-CFG-002"), QStringLiteral("GET_INVERSE"), QStringLiteral("Whitelist inverse state"));
    add(QStringLiteral("HD-CFG-003"), whitelist ? whitelist->status : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Validated API application list entries=%1.").arg(apiWhitelist.size()), {}, whitelist ? whitelist->nativeError : std::nullopt);
    add(QStringLiteral("HD-CFG-004"), whitelist ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Application paths were normalized in-memory only."));
    add(QStringLiteral("HD-CFG-005"), whitelist ? (hasDuplicate(apiWhitelist) ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Duplicate whitelist entries=%1.").arg(hasDuplicate(apiWhitelist) ? QStringLiteral("observed") : QStringLiteral("not observed")));
    const int missingWhitelist = std::count_if(apiWhitelist.cbegin(), apiWhitelist.cend(), [](const QString &path) { return !QFileInfo(path).exists(); });
    add(QStringLiteral("HD-CFG-006"), whitelist ? (missingWhitelist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Missing whitelist application files=%1.").arg(missingWhitelist));
    const int malformedWhitelist = std::count_if(apiWhitelist.cbegin(), apiWhitelist.cend(), [](const QString &path) { return path.trimmed().isEmpty() || !QDir::isAbsolutePath(path); });
    add(QStringLiteral("HD-CFG-007"), whitelist ? (malformedWhitelist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Malformed/unresolvable whitelist paths=%1.").arg(malformedWhitelist));
    const bool hotasExempt = std::any_of(apiWhitelist.cbegin(), apiWhitelist.cend(), [](const QString &path) { return path.contains(QStringLiteral("HOTAS"), Qt::CaseInsensitive); });
    add(QStringLiteral("HD-CFG-008"), snapshot.environment.hotasBf6Present ? (hotasExempt ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable,
        snapshot.environment.hotasBf6Present ? QStringLiteral("HOTAS exemption=%1.").arg(hotasExempt ? QStringLiteral("observed") : QStringLiteral("not observed")) : QStringLiteral("No trusted HOTAS launch context was supplied."));
    add(QStringLiteral("HD-CFG-009"), DoctorCheckStatus::NotApplicable, QStringLiteral("Doctor does not add or require a whitelist exemption to perform GET diagnostics."));
    add(QStringLiteral("HD-CFG-010"), blacklist ? blacklist->status : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Validated hidden-device entries=%1.").arg(apiBlacklist.size()), {}, blacklist ? blacklist->nativeError : std::nullopt);
    add(QStringLiteral("HD-CFG-011"), blacklist ? (hasDuplicate(apiBlacklist) ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Duplicate blacklist entries=%1.").arg(hasDuplicate(apiBlacklist) ? QStringLiteral("observed") : QStringLiteral("not observed")));
    const int malformedBlacklist = std::count_if(apiBlacklist.cbegin(), apiBlacklist.cend(), [](const QString &path) { return path.trimmed().isEmpty() || !path.contains(QChar('\u005c')); });
    add(QStringLiteral("HD-CFG-012"), blacklist ? (malformedBlacklist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Invalid device-instance path entries=%1.").arg(malformedBlacklist));
    const auto deviceMatches = [&](const QString &entry) {
        return std::any_of(snapshot.devices.cbegin(), snapshot.devices.cend(), [&](const DeviceObservation &device) { return device.instanceId.compare(entry, Qt::CaseInsensitive) == 0; });
    };
    const int unresolvedBlacklist = std::count_if(apiBlacklist.cbegin(), apiBlacklist.cend(), [&](const QString &entry) { return !deviceMatches(entry); });
    add(QStringLiteral("HD-CFG-013"), blacklist ? (unresolvedBlacklist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Unresolved blacklist device paths=%1.").arg(unresolvedBlacklist));
    add(QStringLiteral("HD-CFG-014"), blacklist ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Blacklist entries were correlated to Doctor-owned device classifications when present."));
    const bool virtualHidden = std::any_of(snapshot.devices.cbegin(), snapshot.devices.cend(), [&](const DeviceObservation &device) {
        return device.classification == DeviceClassification::VJoyVirtualOutput && deviceMatches(device.instanceId); });
    add(QStringLiteral("HD-CFG-015"), virtualHidden ? DoctorCheckStatus::Warning : (blacklist ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable),
        QStringLiteral("vJoy/virtual output hidden=%1.").arg(virtualHidden ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-CFG-016"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable,
        QStringLiteral("No trusted HOTAS rig identity was imported into this standalone run."));
    add(QStringLiteral("HD-CFG-017"), DoctorCheckStatus::Informational, QStringLiteral("No configuration entry is treated as HOTAS-owned by Phase 1."));
    add(QStringLiteral("HD-CFG-018"), snapshot.contradictions.isEmpty() ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning,
        QStringLiteral("Registry/API configuration contradictions=%1.").arg(snapshot.contradictions.size()));
    const bool active = activeFirst && activeFirst->status == DoctorCheckStatus::Healthy && activeFirst->value == QStringLiteral("true");
    add(QStringLiteral("HD-CFG-019"), active && apiBlacklist.isEmpty() ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        active && apiBlacklist.isEmpty() ? QStringLiteral("Active HidHide with empty persistent blacklist.") : QStringLiteral("Condition not observed."));
    const bool inverse = protocolFor(snapshot, QStringLiteral("GET_INVERSE")) && protocolFor(snapshot, QStringLiteral("GET_INVERSE"))->value == QStringLiteral("true");
    add(QStringLiteral("HD-CFG-020"), inverse ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        inverse ? QStringLiteral("Whitelist inverse mode is active; semantics are reported, not changed.") : QStringLiteral("Inverse mode is not active or unavailable."));
    add(QStringLiteral("HD-CFG-021"), whitelist ? (hasDuplicate(apiWhitelist) ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Case/canonical duplicate candidates=%1.").arg(hasDuplicate(apiWhitelist) ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-CFG-022"), snapshot.environment.hotasBf6Present ? (missingWhitelist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Nonexistent HOTAS whitelist path evaluation requires supplied trusted HOTAS context."));
    add(QStringLiteral("HD-CFG-023"), DoctorCheckStatus::NotApplicable, QStringLiteral("Session blacklist state is not queried because Phase 1 permits only documented persistent GET operations."));
    const bool configComplete = protocolFor(snapshot, QStringLiteral("GET_ACTIVE")) && protocolFor(snapshot, QStringLiteral("GET_INVERSE")) && whitelist && blacklist;
    add(QStringLiteral("HD-CFG-024"), configComplete ? DoctorCheckStatus::Healthy : (snapshot.environment.hidhide.present ? DoctorCheckStatus::Warning : DoctorCheckStatus::NotApplicable),
        QStringLiteral("All independent configuration fields observed=%1.").arg(configComplete ? QStringLiteral("true") : QStringLiteral("false")));

    const int physicalCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) { return device.classification == DeviceClassification::PhysicalGamingInput; });
    const int virtualCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) { return device.classification == DeviceClassification::VirtualGamingDevice || device.classification == DeviceClassification::VJoyVirtualOutput; });
    add(QStringLiteral("HD-DEV-001"), DoctorCheckStatus::Healthy, QStringLiteral("Gaming/HID candidate devices=%1.").arg(snapshot.devices.size()));
    add(QStringLiteral("HD-DEV-002"), DoctorCheckStatus::Healthy, QStringLiteral("Bounded all-class present-device scan isolated HID/vJoy/XUSB candidates=%1.").arg(snapshot.devices.size()));
    add(QStringLiteral("HD-DEV-003"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy, QStringLiteral("Per-device property faults=%1.").arg(devicePropertyFailures));
    add(QStringLiteral("HD-DEV-004"), snapshot.devices.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Healthy, QStringLiteral("Canonical instance IDs captured for each candidate."));
    add(QStringLiteral("HD-DEV-005"), snapshot.devices.isEmpty() ? DoctorCheckStatus::NotApplicable : DoctorCheckStatus::Informational, QStringLiteral("Friendly/product labels captured where exposed by SetupAPI."));
    const int withoutVidPid = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) { return !device.hardwareIds.join(QChar(' ')).contains(QStringLiteral("VID_"), Qt::CaseInsensitive); });
    add(QStringLiteral("HD-DEV-006"), snapshot.devices.isEmpty() ? DoctorCheckStatus::NotApplicable : (withoutVidPid ? DoctorCheckStatus::Informational : DoctorCheckStatus::Healthy), QStringLiteral("VID/PID unavailable for candidates=%1.").arg(withoutVidPid));
    const int hidInterfaceCount = std::accumulate(snapshot.devices.cbegin(), snapshot.devices.cend(), 0,
        [](int total, const DeviceObservation &device) { return total + device.interfacePaths.size(); });
    const int usageCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(),
        [](const DeviceObservation &device) { return device.usagePage != 0 || device.usage != 0; });
    add(QStringLiteral("HD-DEV-007"), hidInterfaceCount == 0 ? DoctorCheckStatus::NotApplicable
        : (usageCount ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning),
        QStringLiteral("HID interface paths=%1; usage page/usage captured=%2.").arg(hidInterfaceCount).arg(usageCount));
    add(QStringLiteral("HD-DEV-008"), DoctorCheckStatus::Healthy, QStringLiteral("All enumerated device nodes were DIGCF_PRESENT."));
    const int problemCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) { return device.problemCode != 0; });
    add(QStringLiteral("HD-DEV-009"), problemCount ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy, QStringLiteral("PnP problem devices=%1.").arg(problemCount));
    const int driverContextCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(),
        [](const DeviceObservation &device) { return !device.driverProvider.isEmpty() || !device.driverVersion.isEmpty(); });
    add(QStringLiteral("HD-DEV-010"), snapshot.devices.isEmpty() ? DoctorCheckStatus::NotApplicable
        : (driverContextCount ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Informational),
        QStringLiteral("Per-device provider/version context captured=%1.").arg(driverContextCount));
    add(QStringLiteral("HD-DEV-011"), DoctorCheckStatus::Unknown, QStringLiteral("Composite parent/child topology was not inferred without a device-tree traversal."));
    const int containerCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(),
        [](const DeviceObservation &device) { return !device.containerId.isEmpty(); });
    add(QStringLiteral("HD-DEV-012"), snapshot.devices.isEmpty() ? DoctorCheckStatus::NotApplicable
        : (containerCount ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Informational),
        QStringLiteral("Container IDs captured=%1.").arg(containerCount));
    add(QStringLiteral("HD-DEV-013"), DoctorCheckStatus::Informational, QStringLiteral("Duplicate logical exposure requires interface-level topology correlation."));
    add(QStringLiteral("HD-DEV-014"), DoctorCheckStatus::NotApplicable, QStringLiteral("Present-only bounded scan intentionally does not label historical nodes as phantom."));
    add(QStringLiteral("HD-DEV-015"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Per-device access/property failures are retained individually."));
    add(QStringLiteral("HD-DEV-016"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy, QStringLiteral("Descriptor/property query failures=%1.").arg(devicePropertyFailures));
    add(QStringLiteral("HD-DEV-017"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy, QStringLiteral("Unexpected property types/sizes are isolated as device-local evidence."));
    add(QStringLiteral("HD-DEV-018"), virtualCount ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Virtual gaming/output candidates=%1.").arg(virtualCount));
    add(QStringLiteral("HD-DEV-019"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable, QStringLiteral("No HOTAS rig identity was supplied to a standalone scan."));
    add(QStringLiteral("HD-DEV-020"), DoctorCheckStatus::Informational, QStringLiteral("Ambiguous physical identity remains explicit; no HOTAS membership is guessed."));
    add(QStringLiteral("HD-DEV-021"), DoctorCheckStatus::Informational, QStringLiteral("Reconnect necessity is not asserted without a proposed configuration change."));
    add(QStringLiteral("HD-DEV-022"), DoctorCheckStatus::Unknown, QStringLiteral("Current device-use state is not inferred by opening controller devices."));
    add(QStringLiteral("HD-DEV-023"), devicePropertyFailures ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy, QStringLiteral("Broken-enumeration aggregate failures=%1.").arg(devicePropertyFailures));
    add(QStringLiteral("HD-DEV-024"), DoctorCheckStatus::NotApplicable, QStringLiteral("Device enumeration repeat is omitted unless a live instability is observed."));
    const int xusb = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(), [](const DeviceObservation &device) { return device.instanceId.contains(QStringLiteral("XUSB"), Qt::CaseInsensitive); });
    add(QStringLiteral("HD-DEV-025"), xusb ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("XInput/XUSB candidates=%1.").arg(xusb));
    add(QStringLiteral("HD-DEV-026"), DoctorCheckStatus::Unknown, QStringLiteral("Raw HID descriptor vs high-level enumeration comparison is not inferred from one source."));
    add(QStringLiteral("HD-DEV-027"), blacklist ? (unresolvedBlacklist ? DoctorCheckStatus::Warning : DoctorCheckStatus::Healthy) : DoctorCheckStatus::NotApplicable, QStringLiteral("Blacklisted paths absent from Doctor inventory=%1.").arg(unresolvedBlacklist));
    add(QStringLiteral("HD-DEV-028"), DoctorCheckStatus::NotApplicable, QStringLiteral("No HidHide client/CLI path is launched for comparison."));

    add(QStringLiteral("HD-ISO-001"), activeFirst ? activeFirst->status : DoctorCheckStatus::NotApplicable,
        active ? QStringLiteral("HidHide active for intended use.") : QStringLiteral("HidHide active state is false or unavailable."), {}, activeFirst ? activeFirst->nativeError : std::nullopt);
    add(QStringLiteral("HD-ISO-002"), activeFirst && protocolFor(snapshot, QStringLiteral("GET_INVERSE")) ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Whitelist/inverse evidence is recorded without changing feeder access."));
    add(QStringLiteral("HD-ISO-003"), snapshot.environment.hotasBf6Present ? (hotasExempt ? DoctorCheckStatus::Healthy : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("HOTAS expected executable access needs a trusted launch context."));
    add(QStringLiteral("HD-ISO-004"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable,
        QStringLiteral("No intended HOTAS physical target set was supplied."));
    add(QStringLiteral("HD-ISO-005"), virtualHidden ? DoctorCheckStatus::Warning : (blacklist ? DoctorCheckStatus::Healthy : DoctorCheckStatus::NotApplicable),
        QStringLiteral("Virtual outputs unintentionally hidden=%1.").arg(virtualHidden ? QStringLiteral("observed") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-ISO-006"), DoctorCheckStatus::NotApplicable, QStringLiteral("Doctor does not open physical controllers merely to test visibility."));
    add(QStringLiteral("HD-ISO-007"), DoctorCheckStatus::Informational, QStringLiteral("Game-facing visibility is modeled from configuration only; no game proof is claimed."));
    const bool sessionCapability = false;
    add(QStringLiteral("HD-ISO-008"), DoctorCheckStatus::NotApplicable, QStringLiteral("Session blacklist strategy is not capability-probed by the GET-only safety policy."));
    const bool readiness = active && !virtualHidden && (whitelist && whitelist->status == DoctorCheckStatus::Healthy) && (blacklist && blacklist->status == DoctorCheckStatus::Healthy);
    add(QStringLiteral("HD-ISO-009"), snapshot.environment.hidhide.present ? (readiness ? DoctorCheckStatus::Informational : DoctorCheckStatus::Warning) : DoctorCheckStatus::NotApplicable,
        QStringLiteral("Readiness composite is evidence-derived only; state=%1.").arg(readiness ? QStringLiteral("consistent") : QStringLiteral("incomplete")));
    add(QStringLiteral("HD-ISO-010"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable, QStringLiteral("No intended HOTAS target set was supplied."));
    add(QStringLiteral("HD-ISO-011"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable, QStringLiteral("No HOTAS-owned scope is inferred from user entries."));
    add(QStringLiteral("HD-ISO-012"), snapshot.environment.hotasBf6Present ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable, QStringLiteral("No intended HOTAS target set was supplied."));
    add(QStringLiteral("HD-ISO-013"), pendingRename ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Pending replacement evidence=%1.").arg(pendingRename ? QStringLiteral("present") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-ISO-014"), DoctorCheckStatus::NotApplicable, QStringLiteral("HidHideClient is not launched to test hidden-device view."));
    add(QStringLiteral("HD-ISO-015"), sessionCapability ? DoctorCheckStatus::Unknown : DoctorCheckStatus::NotApplicable, QStringLiteral("Session capability is intentionally not probed."));

    const auto countText = [](const QList<EventObservation> &events, const QString &needle) {
        return std::count_if(events.cbegin(), events.cend(), [&](const EventObservation &event) { return event.summary.contains(needle, Qt::CaseInsensitive); });
    };
    const int clientEventCount = countText(snapshot.events, QStringLiteral("HidHideClient"));
    const int cliEventCount = countText(snapshot.events, QStringLiteral("HidHideCLI"));
    const int clientWerCount = countText(snapshot.werReports, QStringLiteral("HidHideClient"));
    const int cliWerCount = countText(snapshot.werReports, QStringLiteral("HidHideCLI"));
    const int systemEventCount = countText(snapshot.events, QStringLiteral("HidHide"));
    add(QStringLiteral("HD-WIN-001"), clientEventCount ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Recent HidHideClient Application/System events=%1.").arg(clientEventCount));
    add(QStringLiteral("HD-WIN-002"), cliEventCount ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("Recent HidHideCLI Application/System events=%1.").arg(cliEventCount));
    add(QStringLiteral("HD-WIN-003"), clientWerCount ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("HidHideClient WER records=%1.").arg(clientWerCount));
    add(QStringLiteral("HD-WIN-004"), cliWerCount ? DoctorCheckStatus::Warning : DoctorCheckStatus::Informational, QStringLiteral("HidHideCLI WER records=%1.").arg(cliWerCount));
    add(QStringLiteral("HD-WIN-005"), systemEventCount ? DoctorCheckStatus::Informational : DoctorCheckStatus::Informational, QStringLiteral("Recent HidHide System/Application event evidence=%1.").arg(systemEventCount));
    add(QStringLiteral("HD-WIN-006"), snapshot.setupApiEvidence.isEmpty() ? DoctorCheckStatus::Informational : DoctorCheckStatus::Informational, QStringLiteral("SetupAPI HidHide records=%1.").arg(snapshot.setupApiEvidence.size()));
    add(QStringLiteral("HD-WIN-007"), systemEventCount ? DoctorCheckStatus::Informational : DoctorCheckStatus::Informational, QStringLiteral("SCM/HidHide event candidates=%1.").arg(systemEventCount));
    add(QStringLiteral("HD-WIN-008"), snapshot.setupApiEvidence.isEmpty() ? DoctorCheckStatus::Informational : DoctorCheckStatus::Healthy, QStringLiteral("Bounded SetupAPI HidHide records=%1.").arg(snapshot.setupApiEvidence.size()));
    add(QStringLiteral("HD-WIN-009"), DoctorCheckStatus::Unknown, QStringLiteral("MSI installer event source is not inferred from generic HidHide event text."));
    add(QStringLiteral("HD-WIN-010"), DoctorCheckStatus::Informational, QStringLiteral("Boot timestamp and bounded event timestamps are retained for correlation."));
    add(QStringLiteral("HD-WIN-011"), (clientWerCount || cliWerCount) ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Crash exception classification is retained only when existing WER text exposes it."));
    add(QStringLiteral("HD-WIN-012"), (clientWerCount || cliWerCount) ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Faulting module is retained only when existing WER text exposes it."));
    add(QStringLiteral("HD-WIN-013"), DoctorCheckStatus::Unknown, QStringLiteral("ETW manifest/provider registration is not inferred from event log text."));
    add(QStringLiteral("HD-WIN-014"), DoctorCheckStatus::NotApplicable, QStringLiteral("Only existing WER and SetupAPI evidence locations are read; no trace search is performed."));
    add(QStringLiteral("HD-WIN-015"), pendingRename ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("Pending replacement/reboot evidence=%1.").arg(pendingRename ? QStringLiteral("present") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-WIN-016"), DoctorCheckStatus::Unknown, QStringLiteral("No explicit security-block event was observed or inferred."));
    add(QStringLiteral("HD-WIN-017"), pendingRename ? DoctorCheckStatus::Informational : DoctorCheckStatus::NotApplicable, QStringLiteral("File replacement evidence=%1.").arg(pendingRename ? QStringLiteral("present") : QStringLiteral("not observed")));
    add(QStringLiteral("HD-WIN-018"), DoctorCheckStatus::Healthy, QStringLiteral("Event timestamps are reported in UTC with current-clock sanity evidence."));

    // Every catalogue item is explicit.  A conditional capability that had
    // no safe local source is recorded as Unknown, never silently promoted to
    // a category-wide healthy result.  Phase 2 correlation/KB items are
    // intentionally left for the engine's explicit deferred result.
    const QStringList allIds = DoctorCatalog::v11DefinedCheckIds();
    for (const QString &id : allIds) {
        if (id.startsWith(QStringLiteral("HD-X-")) || id.startsWith(QStringLiteral("HD-KB-"))) continue;
        const bool exists = std::any_of(snapshot.catalogObservations.cbegin(), snapshot.catalogObservations.cend(),
            [&](const CatalogObservation &observation) { return observation.checkId == id; });
        if (!exists) add(id, DoctorCheckStatus::Unknown,
            QStringLiteral("No safe direct source was available for this conditional Phase 1 catalog requirement."));
    }
}

} // namespace

ReadOnlyDiagnosticSnapshot ReadOnlyWindowsDiagnosticProvider::observe(std::atomic_bool *cancelled,
    ObservationProgress onProgress)
{
    ReadOnlyDiagnosticSnapshot snapshot;
    const auto stage = [&](const char *id, int percent) {
        if (onProgress) onProgress(DoctorCheckId(QString::fromLatin1(id)), percent);
    };
    snapshot.build.channel = QStringLiteral("development");
    snapshot.build.processArchitecture = sizeof(void *) == 8 ? QStringLiteral("x64") : QStringLiteral("x86");
    stage("HD-SYS-001", 0);
    snapshot.environment = observeEnvironment(&snapshot.pendingRestart);
    stage("HD-SYS-001", 100);
    stage("HD-DRV-003", 0);
    snapshot.service = observeHidHideService();
    stage("HD-DRV-003", 100);
    stage("HD-INST-001", 0);
    const QStringList roots = installRootsFromRegistry();
    appendArtifactCandidates(snapshot, DoctorArtifactKind::ClientExecutable, QStringLiteral("HidHideClient.exe"), roots, QStringLiteral("HidHideClient.exe"));
    appendArtifactCandidates(snapshot, DoctorArtifactKind::CliExecutable, QStringLiteral("HidHideCLI.exe"), roots, QStringLiteral("HidHideCLI.exe"));
    appendArtifactCandidates(snapshot, DoctorArtifactKind::Unknown, QStringLiteral("HidHide Watchdog/setup component"), roots, QStringLiteral("HidHideWatchdog.exe"));
    stage("HD-INST-001", 100);
    const QString systemDirectory = [] {
        std::array<wchar_t, 32768> buffer{};
        const UINT size = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
        return size && size < buffer.size() ? QString::fromWCharArray(buffer.data(), static_cast<int>(size)) : QString();
    }();
    if (!systemDirectory.isEmpty()) snapshot.artifacts.append(inspectArtifact(DoctorArtifactKind::DriverBinary,
        QStringLiteral("Loaded-path candidate HidHide.sys"), QDir(systemDirectory).filePath(QStringLiteral("drivers/HidHide.sys"))));
    stage("HD-PKG-005", 0);
    if (!cancelled || !cancelled->load()) observeDriverStore(snapshot, cancelled);
    stage("HD-PKG-005", 100);
    stage("HD-CFG-001", 0);
    observeConfigurationRegistry(snapshot);
    stage("HD-CFG-001", 100);
    stage("HD-DRV-009", 0);
    observeFilterRegistrations(snapshot);
    stage("HD-DRV-009", 100);
    stage("HD-DRV-008", 0);
    snapshot.hidHideInterfacePaths = observeHidHideInterfaces(&snapshot.operationalLimits);
    stage("HD-DRV-008", 100);
    stage("HD-API-001", 0);
    if (!cancelled || !cancelled->load()) snapshot.protocol = observeProtocol(snapshot.hidHideInterfacePaths, cancelled);
    stage("HD-API-001", 100);
    stage("HD-DEV-001", 0);
    if (!cancelled || !cancelled->load()) snapshot.devices = observeDevices(cancelled);
    if (!cancelled || !cancelled->load()) observeHidInterfaces(snapshot.devices, cancelled);
    stage("HD-DEV-001", 100);
    stage("HD-SYS-018", 0);
    if (!cancelled || !cancelled->load()) snapshot.processes = observeRelevantProcesses();
    stage("HD-SYS-018", 100);
    stage("HD-WIN-001", 0);
    if (!cancelled || !cancelled->load()) snapshot.events = observeEventEvidence();
    if (!cancelled || !cancelled->load()) snapshot.werReports = observeWerReports();
    if (!cancelled || !cancelled->load()) snapshot.setupApiEvidence = observeSetupApiEvidence();
    stage("HD-WIN-001", 100);

    snapshot.environment.hidhide.present = snapshot.service.present || std::any_of(snapshot.artifacts.cbegin(), snapshot.artifacts.cend(),
        [](const FileArtifactObservation &artifact) { return artifact.exists; });
    for (const FileArtifactObservation &artifact : snapshot.artifacts) {
        if (!artifact.exists) continue;
        if (artifact.kind == DoctorArtifactKind::ClientExecutable) snapshot.environment.hidhide.clientVersion = artifact.fileVersion;
        if (artifact.kind == DoctorArtifactKind::DriverBinary) snapshot.environment.hidhide.driverVersion = artifact.fileVersion;
    }
    if (!snapshot.driverPackages.isEmpty()) {
        snapshot.environment.hidhide.packageVersion = snapshot.driverPackages.front().version;
        snapshot.environment.hidhide.packageArchitecture = snapshot.driverPackages.front().architecture;
        const auto provider = std::find_if(snapshot.driverPackages.cbegin(), snapshot.driverPackages.cend(),
            [](const DriverPackageObservation &package) {
                return package.provider.contains(QStringLiteral("Nefarius Software Solutions"), Qt::CaseInsensitive);
            });
        if (provider != snapshot.driverPackages.cend())
            snapshot.environment.hidhide.provider = QStringLiteral("Nefarius Software Solutions e.U.");
    }
    for (const ProtocolObservation &probe : snapshot.protocol) {
        if (probe.status == DoctorCheckStatus::Healthy && probe.operation != QStringLiteral("OPEN_CONTROL"))
            snapshot.environment.hidhide.protocolCapabilities.append(probe.operation);
        if (probe.operation == QStringLiteral("OPEN_CONTROL") && probe.status == DoctorCheckStatus::Healthy)
            snapshot.environment.capabilities.directProtocolAvailable = true;
    }
    snapshot.environment.hidhide.protocolCapabilities.removeDuplicates();
    if (snapshot.environment.capabilities.directProtocolAvailable
        && snapshot.environment.capabilities.helperArchitectureCompatible) {
        // This expresses measured execution compatibility only.  Each plan
        // remains separately gated by its Lab/Field qualification and exact
        // package catalog record.
        snapshot.environment.capabilities.highestQualifiedRepairTier = RepairCapabilityTier::RecoverySupported;
    }
    snapshot.environment.devices.physicalControllerCount = std::count_if(snapshot.devices.cbegin(), snapshot.devices.cend(),
        [](const DeviceObservation &device) { return device.classification == DeviceClassification::PhysicalGamingInput; });
    for (const DeviceObservation &device : snapshot.devices) {
        if (!device.manufacturer.isEmpty()) snapshot.environment.devices.vendorLabels.append(device.manufacturer);
        if (!device.propertyFailures.isEmpty()) ++snapshot.environment.devices.malformedObservationCount;
    }
    snapshot.environment.devices.vendorLabels.removeDuplicates();

    const auto findProbe = [&](const QString &operation) -> const ProtocolObservation * {
        for (const ProtocolObservation &probe : snapshot.protocol) if (probe.operation == operation) return &probe;
        return nullptr;
    };
    if (const auto *active = findProbe(QStringLiteral("GET_ACTIVE")); active && active->status == DoctorCheckStatus::Healthy
        && snapshot.registryActive && (active->value == QStringLiteral("true")) != *snapshot.registryActive)
        snapshot.contradictions.append(QStringLiteral("GET_ACTIVE disagrees with persistent Active registry evidence."));
    if (const auto *inverse = findProbe(QStringLiteral("GET_INVERSE")); inverse && inverse->status == DoctorCheckStatus::Healthy
        && snapshot.registryInverse && (inverse->value == QStringLiteral("true")) != *snapshot.registryInverse)
        snapshot.contradictions.append(QStringLiteral("GET_INVERSE disagrees with persistent WhitelistedInverse registry evidence."));
    populateCatalogObservations(snapshot, roots);
return snapshot;
}

} // namespace hotas::doctor
