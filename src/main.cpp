#include "config.hpp"
#include "editor.hpp"
#include "session.hpp"
#include "win.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {

constexpr UINT TrayMessage = WM_APP + 1;
enum MenuId { MenuStart = 100, MenuStop, MenuSelfTest, MenuChoose, MenuSettings, MenuLogs,
              MenuLepLogs, MenuExit };

std::wstring baseDir;
std::wstring logPath;
HWND windowHandle = nullptr;
NOTIFYICONDATAW iconData{};
Config config;
Session session;
std::wstring configError;
bool selfTestBusy = false;

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

void ensureConsole() {
    HANDLE currentOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE currentErr = GetStdHandle(STD_ERROR_HANDLE);
    bool needOut = !currentOut || currentOut == INVALID_HANDLE_VALUE;
    bool needErr = !currentErr || currentErr == INVALID_HANDLE_VALUE;
    if (!needOut && !needErr) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (needOut) {
        HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
        if (out != INVALID_HANDLE_VALUE) SetStdHandle(STD_OUTPUT_HANDLE, out);
    }
    if (needErr) {
        HANDLE err = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
        if (err != INVALID_HANDLE_VALUE) SetStdHandle(STD_ERROR_HANDLE, err);
    }
}

void consoleWrite(const std::wstring& text, bool error = false) {
    HANDLE handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, text.c_str(), (DWORD)text.size(), &written, nullptr);
    } else {
        auto data = utf8(text);
        WriteFile(handle, data.data(), (DWORD)data.size(), &written, nullptr);
    }
    logLine(logPath, text);
}

void showError(const std::wstring& text) {
    logLine(logPath, L"ERROR " + text);
    if (windowHandle) MessageBoxW(windowHandle, text.c_str(), L"ClaudeProtect", MB_OK | MB_ICONERROR);
}

void showInfo(const std::wstring& text) {
    if (windowHandle) MessageBoxW(windowHandle, text.c_str(), L"ClaudeProtect", MB_OK | MB_ICONINFORMATION);
}

bool ensureConfigFile(std::wstring& error) {
    std::wstring iniPath = joinPath(baseDir, L"ClaudeProtect.ini");
    if (fileExists(iniPath)) return true;
    auto candidates = discoverClaudeExecutables();
    return writeDefaultConfig(iniPath, candidates.empty() ? L"" : candidates.front(), error);
}

bool loadAndPrepare(Config& result, std::wstring& error) {
    if (!ensureConfigFile(error)) return false;
    if (!loadConfig(baseDir, result, error)) return false;
    if (!prepareConfig(result, error)) return false;
    for (const auto& warning : result.warnings) logLine(logPath, L"config warning: " + warning);
    return true;
}

bool parseSeconds(const wchar_t* text, DWORD& seconds) {
    wchar_t* end = nullptr;
    unsigned long value = wcstoul(text, &end, 10);
    if (!end || *end != 0 || value > 86400) return false;
    seconds = (DWORD)value;
    return true;
}

std::wstring joinArguments(int start, int argc, wchar_t** argv) {
    std::wstring out;
    for (int i = start; i < argc; ++i) {
        if (!out.empty()) out += L" ";
        out += quoteArgument(argv[i]);
    }
    return out;
}

std::wstring probeArguments(const Config& cfg, DWORD seconds) {
    std::wstring arguments = L"--spawn-child --json --expect-lep --expect-job";
    arguments += L" --expect-timezone " + quoteArgument(cfg.timezoneId);
    arguments += L" --expect-locale " + quoteArgument(cfg.locale);
    arguments += L" --expect-acp " + std::to_wstring(cfg.ansiCodePage);
    arguments += L" --expect-oemcp " + std::to_wstring(cfg.oemCodePage);
    if (cfg.uiLanguageMode > 0)
        arguments += L" --expect-ui " + std::to_wstring(cfg.localeId & 0xFFFF);
    if (cfg.uiLanguageMode == 2) arguments += L" --expect-preferred";
    if (seconds) arguments += L" --seconds " + std::to_wstring(seconds);
    return arguments;
}

void openText(const std::wstring& path, bool settings = false) {
    std::wstring error;
    if (!openTextWindow(windowHandle, path, settings, error)) showError(error);
}

void openFolder(const std::wstring& path) {
    if (dirExists(path))
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
    else if (dirExists(directory(path)))
        ShellExecuteW(nullptr, L"open", directory(path).c_str(), nullptr, nullptr, SW_SHOW);
}

void updateIcon() {
    if (!iconData.hWnd) return;
    std::wstring tip = L"ClaudeProtect - ";
    if (!configError.empty())
        tip += L"configuration error";
    else if (session.active())
        tip += L"Claude running (" + std::to_wstring(session.processCount()) + L" processes)";
    else
        tip += L"stopped";
    wcsncpy(iconData.szTip, tip.c_str(), 127);
    iconData.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &iconData);
}

std::wstring lepStatus() {
    std::wstring error, warning;
    if (!checkLepFiles(config, error, warning)) return L"LEP missing";
    return warning.empty() ? L"LEP ready" : L"LEP ready (x86 pair missing)";
}

void showMenu() {
    Config preview;
    std::wstring previewError;
    if (loadAndPrepare(preview, previewError)) {
        config = preview;
        configError.clear();
    } else {
        configError = previewError;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (session.active() ? MF_GRAYED : 0), MenuStart,
                L"Start Claude Desktop");
    AppendMenuW(menu, MF_STRING | (!session.active() ? MF_GRAYED : 0), MenuStop,
                L"Stop Claude Desktop");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring status = configError.empty()
                              ? L"Virtualization: " + config.timezoneId + L" / " + config.locale +
                                    L" - " + lepStatus()
                              : L"Configuration error - edit settings";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (session.active() ? MF_GRAYED : 0), MenuSelfTest,
                L"Run isolation self-test");
    AppendMenuW(menu, MF_STRING, MenuChoose, L"Choose Claude Desktop executable...");
    AppendMenuW(menu, MF_STRING, MenuSettings, L"Edit settings...");
    AppendMenuW(menu, MF_STRING, MenuLogs, L"Open log");
    AppendMenuW(menu, MF_STRING, MenuLepLogs, L"Open LEP logs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuExit, L"Exit ClaudeProtect");

    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(windowHandle);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, windowHandle, nullptr);
    PostMessageW(windowHandle, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void startClaude() {
    Config fresh;
    std::wstring error;
    if (!loadAndPrepare(fresh, error)) {
        configError = error;
        showError(error);
        return;
    }
    if (fresh.executable.empty() || !fileExists(fresh.executable)) {
        auto candidates = discoverClaudeExecutables();
        if (!candidates.empty()) fresh.executable = candidates.front();
    }
    if (fresh.executable.empty()) {
        showError(L"Claude Desktop was not found. Use \"Choose Claude Desktop executable...\".");
        return;
    }
    LaunchSpec spec;
    spec.executable = fresh.executable;
    spec.arguments = fresh.extraArguments;
    spec.claudeDesktop = isClaudeDesktopPath(fresh.executable, fresh.executable);
    spec.useLep = true;
    if (!session.start(fresh, spec, error)) {
        showError(error);
        return;
    }
    config = fresh;
    configError.clear();
    updateIcon();
}

void runSelfTest() {
    if (selfTestBusy) return;
    Config fresh;
    std::wstring error;
    if (!loadAndPrepare(fresh, error)) {
        showError(error);
        return;
    }
    config = fresh;
    configError.clear();
    std::wstring lepError, lepWarning;
    if (!checkLepFiles(fresh, lepError, lepWarning)) {
        showError(lepError);
        return;
    }
    std::wstring probeLog = joinPath(baseDir, L"selftest.log");
    DeleteFileW(probeLog.c_str());
    selfTestBusy = true;
    std::wstring tail;
    int code = session.runProbe(fresh, probeArguments(fresh, 0), probeLog, 30000, error, tail);
    selfTestBusy = false;
    if (code < 0) {
        showError(error.empty() ? L"Self-test could not start" : error);
        return;
    }
    std::wstring message = code == 0 ? L"Isolation self-test passed."
                                     : L"Isolation self-test failed (exit " +
                                           std::to_wstring(code) + L").";
    message += L"\n\n" + probeLog;
    if (!tail.empty()) {
        if (tail.size() > 1800) tail = tail.substr(tail.size() - 1800);
        message += L"\n\n" + tail;
    }
    logLine(logPath, message);
    showInfo(message);
}

void chooseExecutable() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = windowHandle;
    dialog.lpstrFilter = L"Claude Desktop (claude.exe)\0claude.exe\0Executables (*.exe)\0*.exe\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    if (!isClaudeDesktopPath(path, L"")) {
        if (MessageBoxW(windowHandle,
                        L"The selected file does not look like Claude Desktop. Save it anyway?",
                        L"ClaudeProtect", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    std::wstring error;
    if (!updateIniValue(joinPath(baseDir, L"ClaudeProtect.ini"), L"Executable", path, error)) {
        showError(error);
        return;
    }
    Config fresh;
    if (loadAndPrepare(fresh, error)) {
        config = fresh;
        configError.clear();
    } else {
        configError = error;
        showError(error);
    }
    updateIcon();
}

void exitTray() {
    DestroyWindow(windowHandle);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == taskbarCreated) {
        Shell_NotifyIconW(NIM_ADD, &iconData);
        return 0;
    }
    switch (message) {
    case TrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            showMenu();
        else if (lParam == WM_LBUTTONDBLCLK)
            PostMessageW(hwnd, WM_COMMAND, MenuStart, 0);
        return 0;
    case WM_TIMER: {
        session.poll();
        std::wstring failure = session.takeFailure();
        if (!failure.empty()) showError(L"Managed session stopped: " + failure);
        updateIcon();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case MenuStart: startClaude(); break;
        case MenuStop: session.stop(L"tray-stop"); updateIcon(); break;
        case MenuSelfTest: runSelfTest(); break;
        case MenuChoose: chooseExecutable(); break;
        case MenuSettings: openText(joinPath(baseDir, L"ClaudeProtect.ini"), true); break;
        case MenuLogs: openText(logPath); break;
        case MenuLepLogs: openFolder(joinPath(config.lepDir, L"Log")); break;
        case MenuExit: exitTray(); break;
        }
        return 0;
    case WM_QUERYENDSESSION:
        session.stop(L"session-end");
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        session.stop(L"supervisor-exit");
        Shell_NotifyIconW(NIM_DELETE, &iconData);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void printUsage() {
    consoleWrite(
        L"ClaudeProtect - Claude Desktop time zone and locale supervisor\r\n"
        L"\r\n"
        L"Tray mode: run without arguments.\r\n"
        L"\r\n"
        L"Commands:\r\n"
        L"  --edit-settings         Open the built-in settings editor\r\n"
        L"  --check                 Validate configuration, LEP files and discovery.\r\n"
        L"  --print-config          Print resolved configuration and LEPB fields.\r\n"
        L"  --discover              List discovered Claude Desktop executables.\r\n"
        L"  --selftest [--seconds N]  Run EnvironmentProbe under LEP (parent and child).\r\n"
        L"  --run [options]         Launch Claude Desktop managed by this process.\r\n"
        L"      --seconds N         Keep the session for N seconds (default 5).\r\n"
        L"      --no-preflight      Skip the probe preflight.\r\n"
        L"      --direct            Diagnostic only: launch without LEP virtualization.\r\n"
        L"      --target <exe>      Launch another executable instead of Claude Desktop.\r\n"
        L"      -- <args>           Arguments passed to the target.\r\n");
}

int runCli(int argc, wchar_t** argv) {
    std::wstring command = lower(argv[1]);
    if (command == L"--help" || command == L"-h") {
        printUsage();
        return 0;
    }
    if (command == L"--discover") {
        auto candidates = discoverClaudeExecutables();
        if (candidates.empty()) {
            consoleWrite(L"No Claude Desktop executables found.\r\n", true);
            return 1;
        }
        for (const auto& candidate : candidates) consoleWrite(candidate + L"\r\n");
        return 0;
    }

    Config cfg;
    std::wstring error;
    if (!loadAndPrepare(cfg, error)) {
        consoleWrite(L"Configuration error: " + error + L"\r\n", true);
        return 1;
    }

    if (command == L"--check") {
        consoleWrite(describeConfig(cfg));
        std::wstring lepError, lepWarning;
        bool lepOk = checkLepFiles(cfg, lepError, lepWarning);
        if (lepOk) {
            consoleWrite(L"LEP: ready (" + cfg.lepDir + L")\r\n");
            if (!lepWarning.empty()) consoleWrite(L"LEP warning: " + lepWarning + L"\r\n", true);
        } else {
            consoleWrite(L"LEP: " + lepError + L"\r\n", true);
        }
        if (!fileExists(cfg.probePath))
            consoleWrite(L"Probe: missing (" + cfg.probePath + L")\r\n", true);
        else
            consoleWrite(L"Probe: " + cfg.probePath + L"\r\n");

        auto candidates = discoverClaudeExecutables();
        consoleWrite(L"Discovered Claude Desktop executables: " +
                     std::to_wstring(candidates.size()) + L"\r\n");
        for (const auto& candidate : candidates) consoleWrite(L"  " + candidate + L"\r\n");

        DWORD unmanagedPid = 0;
        std::wstring unmanagedPath, warning;
        if (findUnmanagedClaudeDesktop(cfg.executable, unmanagedPid, unmanagedPath, warning))
            consoleWrite(L"Unmanaged Claude Desktop running: pid " +
                             std::to_wstring(unmanagedPid) + L" " + unmanagedPath + L"\r\n",
                         true);
        else
            consoleWrite(L"No unmanaged Claude Desktop detected.\r\n");
        if (!warning.empty()) consoleWrite(L"Warning: " + warning + L"\r\n", true);
        return lepOk && fileExists(cfg.probePath) ? 0 : 1;
    }

    if (command == L"--print-config") {
        consoleWrite(describeConfig(cfg));
        LepLaunchConfig lepConfig;
        lepConfig.timezoneId = cfg.timezoneId;
        lepConfig.standardName = cfg.standardName;
        lepConfig.daylightName = cfg.daylightName;
        lepConfig.ansiCodePage = cfg.ansiCodePage;
        lepConfig.oemCodePage = cfg.oemCodePage;
        lepConfig.localeId = cfg.localeId;
        lepConfig.fontCharset = cfg.fontCharset;
        lepConfig.registryRedirectionMode = cfg.registryRedirectionMode;
        lepConfig.uiLanguageMode = cfg.uiLanguageMode;
        consoleWrite(LepLoader::describeBlock(LepLoader::buildBlock(lepConfig)) + L"\r\n");
        std::wstring lepError, lepWarning;
        if (!checkLepFiles(cfg, lepError, lepWarning))
            consoleWrite(L"LEP: " + lepError + L"\r\n", true);
        else if (!lepWarning.empty())
            consoleWrite(L"LEP warning: " + lepWarning + L"\r\n", true);
        return 0;
    }

    if (command == L"--selftest") {
        DWORD seconds = 0;
        for (int i = 2; i < argc; ++i) {
            std::wstring argument = argv[i];
            if (argument == L"--seconds" && i + 1 < argc) {
                if (!parseSeconds(argv[++i], seconds)) {
                    consoleWrite(L"Invalid --seconds value\r\n", true);
                    return 2;
                }
            } else {
                consoleWrite(L"Unknown --selftest argument: " + argument + L"\r\n", true);
                return 2;
            }
        }
        std::wstring lepError, lepWarning;
        if (!checkLepFiles(cfg, lepError, lepWarning)) {
            consoleWrite(lepError + L"\r\n", true);
            return 1;
        }
        if (!lepWarning.empty()) consoleWrite(L"Warning: " + lepWarning + L"\r\n", true);
        std::wstring probeLog = joinPath(baseDir, L"selftest.log");
        DeleteFileW(probeLog.c_str());
        Session probeSession;
        std::wstring tail;
        DWORD timeout = (seconds + 30) * 1000;
        int code = probeSession.runProbe(cfg, probeArguments(cfg, seconds), probeLog, timeout,
                                         error, tail);
        if (code < 0) {
            consoleWrite(L"Self-test error: " + error + L"\r\n", true);
            return 1;
        }
        consoleWrite(L"Probe log: " + probeLog + L"\r\n");
        if (!tail.empty()) consoleWrite(tail);
        consoleWrite(L"Self-test exit code: " + std::to_wstring(code) + L"\r\n");
        return code;
    }

    if (command == L"--run") {
        bool direct = false;
        DWORD seconds = 5;
        std::wstring target;
        std::wstring extraArguments;
        for (int i = 2; i < argc; ++i) {
            std::wstring argument = argv[i];
            if (argument == L"--seconds" && i + 1 < argc) {
                if (!parseSeconds(argv[++i], seconds)) {
                    consoleWrite(L"Invalid --seconds value\r\n", true);
                    return 2;
                }
            } else if (argument == L"--no-preflight") {
                cfg.preflight = L"off";
            } else if (argument == L"--direct") {
                direct = true;
            } else if (argument == L"--target" && i + 1 < argc) {
                target = argv[++i];
            } else if (argument == L"--") {
                extraArguments = joinArguments(i + 1, argc, argv);
                break;
            } else {
                consoleWrite(L"Unknown --run argument: " + argument + L"\r\n", true);
                return 2;
            }
        }
        if (target.empty()) target = cfg.executable;
        if (target.empty()) {
            auto candidates = discoverClaudeExecutables();
            if (!candidates.empty()) target = candidates.front();
        }
        if (target.empty() || !fileExists(target)) {
            consoleWrite(L"Target executable not found.\r\n", true);
            return 1;
        }
        LaunchSpec spec;
        spec.executable = target;
        spec.claudeDesktop = isClaudeDesktopPath(target, cfg.executable);
        spec.useLep = !direct;
        if (spec.claudeDesktop) spec.arguments = cfg.extraArguments;
        if (!extraArguments.empty()) {
            if (!spec.arguments.empty()) spec.arguments += L" ";
            spec.arguments += extraArguments;
        }
        Session runSession;
        if (!runSession.start(cfg, spec, error)) {
            consoleWrite(L"Start failed: " + error + L"\r\n", true);
            return 1;
        }
        consoleWrite(L"Started pid " + std::to_wstring(runSession.pid()) + L" target " + target +
                     L"\r\n");
        ULONGLONG deadline = GetTickCount64() + (ULONGLONG)seconds * 1000;
        while (runSession.active() && GetTickCount64() < deadline) {
            runSession.poll();
            Sleep(100);
        }
        if (runSession.active()) {
            consoleWrite(L"Active processes before stop: " +
                         std::to_wstring(runSession.processCount()) + L"\r\n");
            runSession.stop(L"cli-run-finished");
        }
        std::wstring failure = runSession.takeFailure();
        if (!failure.empty()) {
            consoleWrite(L"Failure: " + failure + L"\r\n", true);
            return 1;
        }
        return 0;
    }

    consoleWrite(L"Unknown command: " + command + L"\r\n", true);
    printUsage();
    return 2;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    baseDir = directory(selfPath());
    logPath = joinPath(baseDir, L"ClaudeProtect.log");

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool editSettings = argc == 2 && argv && std::wstring(argv[1]) == L"--edit-settings";
    if (argc > 1 && argv && argv[1][0] == L'-' && !editSettings) {
        ensureConsole();
        int code = runCli(argc, argv);
        LocalFree(argv);
        return code;
    }
    LocalFree(argv);

    std::wstring error;
    if (!loadAndPrepare(config, error)) {
        configError = error;
        logLine(logPath, L"startup configuration error: " + error);
    }

    Handle singleton(CreateMutexW(nullptr, FALSE, L"Local\\ClaudeProtect.Tray"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (editSettings) {
            HWND existing = FindWindowW(L"ClaudeProtectTray", L"ClaudeProtect");
            if (existing) PostMessageW(existing, WM_COMMAND, MenuSettings, 0);
        }
        return 0;
    }

    logLine(logPath, L"tray started");
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"ClaudeProtectTray";
    if (!RegisterClassW(&windowClass)) return 1;
    windowHandle = CreateWindowW(windowClass.lpszClassName, L"ClaudeProtect", WS_OVERLAPPED, 0, 0,
                                 0, 0, nullptr, nullptr, instance, nullptr);
    if (!windowHandle) return 1;

    iconData.cbSize = sizeof(iconData);
    iconData.hWnd = windowHandle;
    iconData.uID = 1;
    iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    iconData.uCallbackMessage = TrayMessage;
    iconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy(iconData.szTip, L"ClaudeProtect - stopped");
    if (!Shell_NotifyIconW(NIM_ADD, &iconData)) {
        logLine(logPath, L"tray icon creation failed");
        DestroyWindow(windowHandle);
        return 1;
    }
    SetTimer(windowHandle, 1, 500, nullptr);
    if (editSettings) PostMessageW(windowHandle, WM_COMMAND, MenuSettings, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (translateTextWindowMessage(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    logLine(logPath, L"tray exited");
    return 0;
}
