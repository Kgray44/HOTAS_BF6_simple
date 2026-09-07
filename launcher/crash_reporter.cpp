#include "crash_reporter.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace hotas::launcher {
namespace {

constexpr int kCopyButton = 1001;
constexpr int kOpenButton = 1002;
constexpr int kRestartButton = 1003;
constexpr int kCloseButton = 1004;
constexpr int kDetailsButton = 1005;

struct ReporterState {
    std::filesystem::path folder;
    std::wstring diagnostics;
    std::wstring folderText;
    HWND window = nullptr;
    HWND detail = nullptr;
    HWND detailsButton = nullptr;
    bool detailsVisible = false;
};
ReporterState *g_state = nullptr;
HBRUSH g_shellBrush = CreateSolidBrush(RGB(22, 31, 36));
HBRUSH g_detailBrush = CreateSolidBrush(RGB(12, 19, 23));

std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string field(const std::string &json, std::string_view name)
{
    const std::string needle = "\"" + std::string(name) + "\":\"";
    const size_t begin = json.find(needle);
    if (begin == std::string::npos) return {};
    const size_t valueStart = begin + needle.size();
    const size_t end = json.find('"', valueStart);
    return end == std::string::npos ? std::string{} : json.substr(valueStart, end - valueStart);
}

std::wstring expandEscaped(std::string text)
{
    std::string result;
    result.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            const char escaped = text[++index];
            result.push_back(escaped == 'n' ? '\n' : escaped);
        } else result.push_back(text[index]);
    }
    return utf8ToWide(result);
}

void copyDiagnostics()
{
    if (!g_state || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t bytes = (g_state->diagnostics.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void *destination = GlobalLock(memory);
        if (destination) {
            std::memcpy(destination, g_state->diagnostics.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else GlobalFree(memory);
    }
    CloseClipboard();
}

void restartMapper()
{
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return;
    const std::filesystem::path mapper = std::filesystem::path(std::wstring(path.data(), length)).parent_path() / L"HOTAS BF6.exe";
    if (!std::filesystem::is_regular_file(mapper)) return;
    std::wstring command = L"\"" + mapper.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(mapper.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, mapper.parent_path().c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void setTechnicalDetailsVisible(bool visible)
{
    if (!g_state || !g_state->window || !g_state->detail || !g_state->detailsButton) return;
    g_state->detailsVisible = visible;
    ShowWindow(g_state->detail, visible ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g_state->detailsButton, visible ? L"Technical details  \u25b4" : L"Technical details  \u25be");
    const int buttonY = visible ? 402 : 208;
    SetWindowPos(g_state->window, nullptr, 0, 0, 660, visible ? 500 : 280,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_state->detailsButton, nullptr, 20, 145, 175, 28,
        SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_state->detail, nullptr, 20, 180, 604, 190,
        SWP_NOZORDER | SWP_NOACTIVATE);
    for (const int id : {kCopyButton, kOpenButton, kRestartButton, kCloseButton}) {
        HWND button = GetDlgItem(g_state->window, id);
        if (button) SetWindowPos(button, nullptr,
            id == kCopyButton ? 20 : id == kOpenButton ? 165 : id == kRestartButton ? 320 : 480,
            buttonY, id == kCopyButton ? 135 : id == kOpenButton ? 145 : id == kRestartButton ? 150 : 144,
            32, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM)
{
    if (message == WM_CTLCOLORSTATIC) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(232, 238, 238));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_shellBrush);
    }
    if (message == WM_CTLCOLOREDIT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(220, 234, 236));
        SetBkColor(dc, RGB(12, 19, 23));
        return reinterpret_cast<LRESULT>(g_detailBrush);
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case kCopyButton: copyDiagnostics(); return 0;
        case kOpenButton: if (g_state) ShellExecuteW(window, L"open", g_state->folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case kRestartButton: restartMapper(); DestroyWindow(window); return 0;
        case kCloseButton: DestroyWindow(window); return 0;
        case kDetailsButton: if (g_state) setTechnicalDetailsVisible(!g_state->detailsVisible); return 0;
        }
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, 0);
}

HWND staticText(HWND parent, const wchar_t *text, int x, int y, int width, int height, HFONT font)
{
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height, parent, nullptr, nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}
} // namespace

bool canOpenCrashReport(const std::filesystem::path &reportDirectory)
{
    const std::string json = readFile(reportDirectory / L"crash.json");
    return !json.empty() && !field(json, "version").empty()
        && !field(json, "fatalCategory").empty();
}

int runCrashReporter(const std::filesystem::path &reportDirectory)
{
    const std::string json = readFile(reportDirectory / L"crash.json");
    const std::wstring version = utf8ToWide(field(json, "version"));
    const std::wstring build = utf8ToWide(field(json, "build"));
    const std::wstring category = utf8ToWide(field(json, "fatalCategory"));
    const std::wstring code = utf8ToWide(field(json, "exceptionCode"));
    const std::wstring dump = utf8ToWide(field(json, "minidump"));
    const std::wstring context = expandEscaped(field(json, "context"));
    // This is deliberately a small parser.  Require the two fields emitted
    // by the writer so arbitrary text passed to --crash-report is reported as
    // malformed rather than shown as a meaningful application failure.
    const bool valid = canOpenCrashReport(reportDirectory);
    ReporterState state{reportDirectory};
    state.folderText = reportDirectory.wstring();
    state.diagnostics = L"HOTAS BF6 crash diagnostics\r\n\r\nVersion: " + (version.empty() ? L"unknown" : version)
        + L"\r\nBuild: " + (build.empty() ? L"unknown" : build)
        + L"\r\nError: " + (category.empty() ? L"Crash report unavailable" : category)
        + (code.empty() ? L"" : L" (" + code + L")") + L"\r\nMinidump: " + (dump.empty() ? L"unavailable" : dump)
        + L"\r\n\r\nLast control-plane context:\r\n" + context
        + L"\r\n\r\nReport folder:\r\n" + state.folderText;
    g_state = &state;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t className[] = L"HOTASBF6CrashReporter";
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpszClassName = className;
    cls.lpfnWndProc = windowProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = g_shellBrush;
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, className, L"HOTAS BF6 crash reporter",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        660, 280, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    HFONT title = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT body = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    staticText(window, L"HOTAS BF6 stopped unexpectedly", 20, 18, 600, 32, title);
    staticText(window, valid ? L"Diagnostic information was saved locally. Nothing was uploaded." : L"The requested crash report is missing or malformed.", 20, 55, 600, 26, body);
    const std::wstring concise = L"Version  " + (version.empty() ? L"unknown" : version)
        + L"     Build  " + (build.empty() ? L"unknown" : build)
        + L"     Error  " + (category.empty() ? L"unavailable" : category)
        + (code.empty() ? L"" : L" (" + code + L")");
    staticText(window, concise.c_str(), 20, 88, 604, 24, body);
    staticText(window, L"Technical diagnostic data remains on this computer unless you explicitly share it.", 20, 115, 604, 22, body);
    state.window = window;
    state.detail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state.diagnostics.c_str(), WS_CHILD | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 20, 180, 604, 190, window, nullptr, instance, nullptr);
    SendMessageW(state.detail, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    state.detailsButton = CreateWindowExW(0, L"BUTTON", L"Technical details  \u25be", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 145, 175, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDetailsButton)), instance, nullptr);
    HWND copy = CreateWindowExW(0, L"BUTTON", L"Copy diagnostics", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 208, 135, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyButton)), instance, nullptr);
    HWND open = CreateWindowExW(0, L"BUTTON", L"Open crash folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 165, 208, 145, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenButton)), instance, nullptr);
    HWND restart = CreateWindowExW(0, L"BUTTON", L"Restart HOTAS BF6", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 320, 208, 150, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRestartButton)), instance, nullptr);
    HWND close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 480, 208, 144, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButton)), instance, nullptr);
    for (HWND button : {state.detailsButton, copy, open, restart, close}) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(title);
    DeleteObject(body);
    g_state = nullptr;
    return 0;
}

} // namespace hotas::launcher
