#include "hotas_build_version.h"
#include "launcher_core.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hotas::launcher::SemanticVersion;
using hotas::launcher::UpdateAction;
using hotas::launcher::UpdateManifest;

constexpr wchar_t kManifestUrl[] =
    L"https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json";
constexpr wchar_t kMapperName[] = L"HOTAS BF6.exe";
constexpr wchar_t kLauncherName[] = L"HOTAS BF6 Launcher.exe";
constexpr wchar_t kUpdateMutexName[] = L"Local\\HOTAS-BF6-Update-Mutex";
constexpr DWORD kNetworkTimeoutMs = 2500;
constexpr size_t kMaximumManifestBytes = 64 * 1024;
constexpr unsigned long long kMaximumLogBytes = 256 * 1024;

std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        result.data(), length);
    return result;
}

std::string wideToUtf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length,
        nullptr, nullptr);
    return result;
}

std::filesystem::path modulePath()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::filesystem::path localAppDataPath()
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) || !path) return {};
    const std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path logPath()
{
    const auto root = localAppDataPath();
    return root.empty() ? std::filesystem::path{} : root / L"HOTAS BF6" / L"logs" / L"updater.log";
}

void logEvent(std::wstring_view event)
{
    const auto path = logPath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    if (std::filesystem::exists(path, error) && std::filesystem::file_size(path, error) > kMaximumLogBytes) {
        const auto oldPath = path.parent_path() / L"updater.previous.log";
        std::filesystem::remove(oldPath, error);
        std::filesystem::rename(path, oldPath, error);
        if (error) {
            error.clear();
            std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
        }
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t timestamp[32]{};
    std::swprintf(timestamp, std::size(timestamp), L"%04u-%02u-%02u %02u:%02u:%02u",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (output) output << wideToUtf8(timestamp) << "  " << wideToUtf8(event) << "\r\n";
}

std::wstring quoteArgument(std::wstring_view argument)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(character);
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

bool startProcess(const std::filesystem::path &application, const std::vector<std::wstring> &arguments,
                  bool hidden, HANDLE *processHandle = nullptr)
{
    if (application.empty() || !std::filesystem::is_regular_file(application)) return false;
    std::wstring command = quoteArgument(application.wstring());
    for (const std::wstring &argument : arguments) {
        command += L" ";
        command += quoteArgument(argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden) {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE,
            hidden ? CREATE_NO_WINDOW : 0, nullptr, application.parent_path().c_str(), &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    if (processHandle) *processHandle = process.hProcess;
    else CloseHandle(process.hProcess);
    return true;
}

bool launchMapper()
{
    const auto mapper = modulePath().parent_path() / kMapperName;
    if (!startProcess(mapper, {}, false)) {
        logEvent(L"fallback launch failed: mapper executable is unavailable");
        return false;
    }
    logEvent(L"launching installed mapper");
    return true;
}

bool launchInstalledLauncherWithoutUpdate(const std::filesystem::path &launcher)
{
    if (!startProcess(launcher, {L"--skip-update"}, false)) {
        logEvent(L"fallback launcher start failed");
        return false;
    }
    logEvent(L"launching installed launcher without a second update check");
    return true;
}

class UpdateProgressWindow final {
public:
    void show()
    {
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&controls);
        m_window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"HOTAS BF6", WS_OVERLAPPED | WS_CAPTION,
            CW_USEDEFAULT, CW_USEDEFAULT, 340, 126, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!m_window) return;
        m_status = CreateWindowExW(0, L"STATIC", L"Downloading update...", WS_CHILD | WS_VISIBLE,
            18, 20, 300, 22, m_window, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
            18, 52, 300, 20, m_window, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(m_bar, PBM_SETRANGE32, 0, 100);
        ShowWindow(m_window, SW_SHOWNORMAL);
        update(L"Downloading update...", 0);
    }

    void update(std::wstring_view status, int percent)
    {
        if (!m_window) return;
        SetWindowTextW(m_status, std::wstring(status).c_str());
        SendMessageW(m_bar, PBM_SETPOS, std::clamp(percent, 0, 100), 0);
        pump();
    }

    ~UpdateProgressWindow()
    {
        if (m_window) DestroyWindow(m_window);
    }

private:
    void pump()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    HWND m_window = nullptr;
    HWND m_status = nullptr;
    HWND m_bar = nullptr;
};

bool readHttps(const std::wstring &url, size_t maximumBytes,
               const std::function<bool(const BYTE *, DWORD)> &onChunk,
               DWORD *contentLength = nullptr)
{
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    std::array<wchar_t, 256> host{};
    std::array<wchar_t, 4096> path{};
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) return false;

    const std::wstring userAgent = L"HOTAS-BF6-Launcher/" + utf8ToWide(HOTAS_BF6_VERSION);
    HINTERNET session = WinHttpOpen(userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    bool result = false;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    do {
        WinHttpSetTimeouts(session, kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs);
        connection = WinHttpConnect(session, std::wstring(host.data(), components.dwHostNameLength).c_str(),
            components.nPort, 0);
        if (!connection) break;
        request = WinHttpOpenRequest(connection, L"GET", std::wstring(path.data(), components.dwUrlPathLength).c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) break;
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status != 200) break;
        DWORD length = 0;
        DWORD lengthSize = sizeof(length);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize, WINHTTP_NO_HEADER_INDEX) && contentLength) {
            *contentLength = length;
        }
        size_t downloaded = 0;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) break;
            if (available == 0) {
                result = true;
                break;
            }
            if (downloaded + available > maximumBytes) break;
            std::vector<BYTE> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0 || !onChunk(buffer.data(), read)) break;
            downloaded += read;
        }
    } while (false);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

bool fetchManifest(std::string &manifest)
{
    manifest.clear();
    const bool fetched = readHttps(kManifestUrl, kMaximumManifestBytes,
        [&manifest](const BYTE *data, DWORD count) {
            manifest.append(reinterpret_cast<const char *>(data), count);
            return true;
        });
    if (!fetched) logEvent(L"manifest fetch failed; using installed version");
    return fetched;
}

std::optional<std::filesystem::path> downloadAndVerifyInstaller(const UpdateManifest &manifest,
                                                                  UpdateProgressWindow &progress)
{
    std::error_code error;
    const auto staging = std::filesystem::temp_directory_path(error) / L"HOTAS-BF6-Update"
        / utf8ToWide(manifest.versionText);
    if (error || staging.empty() || !std::filesystem::create_directories(staging, error) && error) {
        logEvent(L"could not create update staging directory");
        return std::nullopt;
    }
    const auto installer = staging / utf8ToWide(manifest.installer);
    const auto temporary = std::filesystem::path(installer.wstring() + L".tmp");
    std::filesystem::remove(temporary, error);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        logEvent(L"could not create temporary update installer");
        return std::nullopt;
    }
    DWORD total = 0;
    size_t downloaded = 0;
    progress.update(L"Downloading update...", 0);
    logEvent(L"update installer download started");
    const bool downloadedSuccessfully = readHttps(utf8ToWide(manifest.installerUrl), 1024ULL * 1024ULL * 1024ULL,
        [&output, &progress, &total, &downloaded](const BYTE *data, DWORD count) {
            output.write(reinterpret_cast<const char *>(data), count);
            if (!output) return false;
            downloaded += count;
            if (total > 0) progress.update(L"Downloading update...",
                static_cast<int>(std::min<size_t>(99, downloaded * 100 / total)));
            return true;
        }, &total);
    output.close();
    if (!downloadedSuccessfully) {
        std::filesystem::remove(temporary, error);
        logEvent(L"update installer download failed; using installed version");
        return std::nullopt;
    }
    progress.update(L"Verifying update...", 100);
    std::string hashError;
    if (!hotas::launcher::verifyFileSha256(temporary.wstring(), manifest.sha256, &hashError)) {
        std::filesystem::remove(temporary, error);
        logEvent(L"update installer SHA-256 verification failed; using installed version");
        return std::nullopt;
    }
    if (!MoveFileExW(temporary.c_str(), installer.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        logEvent(L"verified installer could not be finalized");
        return std::nullopt;
    }
    logEvent(L"update installer SHA-256 verified");
    return installer;
}

bool startUpdateHelper(const std::filesystem::path &installer, const UpdateManifest &manifest)
{
    const auto installedLauncher = modulePath();
    const auto helper = installer.parent_path() / L"HOTAS BF6 Update Helper.exe";
    if (installedLauncher.empty() || !CopyFileW(installedLauncher.c_str(), helper.c_str(), FALSE)) {
        logEvent(L"could not create temporary update helper");
        return false;
    }
    const auto targetLauncher = installedLauncher.parent_path() / kLauncherName;
    const std::vector<std::wstring> arguments{
        L"--apply-update", L"--parent-pid", std::to_wstring(GetCurrentProcessId()),
        L"--installer", installer.wstring(), L"--sha256", utf8ToWide(manifest.sha256),
        L"--launcher", targetLauncher.wstring(),
    };
    if (!startProcess(helper, arguments, true)) {
        logEvent(L"could not start temporary update helper");
        return false;
    }
    logEvent(L"verified update handed to temporary helper");
    return true;
}

std::optional<std::wstring> argumentValue(const std::vector<std::wstring> &arguments, std::wstring_view name)
{
    for (size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == name) return arguments[index + 1];
    }
    return std::nullopt;
}

bool waitForParentExit(DWORD parentPid)
{
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parent) return true;
    const DWORD waited = WaitForSingleObject(parent, 30 * 1000);
    CloseHandle(parent);
    return waited == WAIT_OBJECT_0;
}

int applyUpdate(const std::vector<std::wstring> &arguments)
{
    const auto parentText = argumentValue(arguments, L"--parent-pid");
    const auto installerText = argumentValue(arguments, L"--installer");
    const auto hash = argumentValue(arguments, L"--sha256");
    const auto launcherText = argumentValue(arguments, L"--launcher");
    if (!parentText || !installerText || !hash || !launcherText) {
        logEvent(L"temporary update helper received invalid arguments");
        return 1;
    }
    DWORD parentPid = 0;
    try {
        parentPid = static_cast<DWORD>(std::stoul(*parentText));
    } catch (...) {
        logEvent(L"temporary update helper received invalid parent process ID");
        return 1;
    }
    const std::filesystem::path installer(*installerText);
    const std::filesystem::path launcher(*launcherText);
    if (installer.filename().wstring().rfind(L"HOTAS-BF6-Setup-v", 0) != 0
        || installer.extension() != L".exe" || !hotas::launcher::verifyFileSha256(installer.wstring(),
            wideToUtf8(*hash))) {
        logEvent(L"temporary update helper rejected installer before execution");
        launchInstalledLauncherWithoutUpdate(launcher);
        return 1;
    }
    if (!waitForParentExit(parentPid)) {
        logEvent(L"temporary update helper timed out waiting for launcher exit");
        return 1;
    }
    HANDLE installerProcess = nullptr;
    const std::vector<std::wstring> installerArguments{
        L"/VERYSILENT", L"/SUPPRESSMSGBOXES", L"/NORESTART", L"/SP-",
        L"/DIR=" + launcher.parent_path().wstring(),
    };
    if (!startProcess(installer, installerArguments, true, &installerProcess)) {
        logEvent(L"temporary update helper could not start installer");
        launchInstalledLauncherWithoutUpdate(launcher);
        return 1;
    }
    const DWORD waited = WaitForSingleObject(installerProcess, 10 * 60 * 1000);
    DWORD exitCode = 1;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(installerProcess, &exitCode);
    CloseHandle(installerProcess);
    if (waited != WAIT_OBJECT_0 || exitCode != 0) {
        logEvent(L"installer failed or timed out; preserving installed version");
        if (waited == WAIT_OBJECT_0) launchInstalledLauncherWithoutUpdate(launcher);
        return 1;
    }
    logEvent(L"installer completed successfully; starting updated launcher");
    return launchInstalledLauncherWithoutUpdate(launcher) ? 0 : 1;
}

std::vector<std::wstring> commandLineArguments()
{
    int count = 0;
    LPWSTR *values = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> arguments;
    if (values) {
        for (int index = 1; index < count; ++index) arguments.emplace_back(values[index]);
        LocalFree(values);
    }
    return arguments;
}

bool hasArgument(const std::vector<std::wstring> &arguments, std::wstring_view expected)
{
    return std::find(arguments.begin(), arguments.end(), expected) != arguments.end();
}

int runLauncher()
{
    SemanticVersion localVersion{};
    std::string versionError;
    if (!hotas::launcher::parseSemanticVersion(HOTAS_BF6_VERSION, localVersion, &versionError)) {
        logEvent(L"compiled launcher version is invalid; using installed mapper");
        return launchMapper() ? 0 : 1;
    }
    logEvent(L"launcher started with version " + utf8ToWide(HOTAS_BF6_VERSION));
    std::string manifestJson;
    const bool fetched = fetchManifest(manifestJson);
    UpdateManifest manifest;
    std::string decisionReason;
    const UpdateAction action = hotas::launcher::decideUpdate(fetched, manifestJson, localVersion,
        &manifest, &decisionReason);
    if (action != UpdateAction::InstallUpdate) {
        logEvent(L"update check result: " + utf8ToWide(decisionReason));
        return launchMapper() ? 0 : 1;
    }
    logEvent(L"new stable release available: " + utf8ToWide(manifest.versionText));
    UpdateProgressWindow progress;
    progress.show();
    const auto installer = downloadAndVerifyInstaller(manifest, progress);
    if (!installer || !startUpdateHelper(*installer, manifest)) {
        return launchMapper() ? 0 : 1;
    }
    return 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::vector<std::wstring> arguments = commandLineArguments();
    if (hasArgument(arguments, L"--apply-update")) return applyUpdate(arguments);
    if (hasArgument(arguments, L"--skip-update")) return launchMapper() ? 0 : 1;

    HANDLE updateMutex = CreateMutexW(nullptr, TRUE, kUpdateMutexName);
    if (!updateMutex) return launchMapper() ? 0 : 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(updateMutex);
        logEvent(L"another launcher owns the update lock; using installed mapper");
        return launchMapper() ? 0 : 1;
    }
    const int result = runLauncher();
    ReleaseMutex(updateMutex);
    CloseHandle(updateMutex);
    return result;
}
