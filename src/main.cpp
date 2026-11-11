#include "config.hpp"
#include "ui.hpp"
#include "session.hpp"
#include "resource.h"
#include "win.hpp"
#include <objbase.h>
#include <future>
#include <array>
#include <memory>
#include <chrono>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {

constexpr UINT TrayMessage = WM_APP + 1;
enum MenuId { MenuStart = 100, MenuStop, MenuSelfTest, MenuPanel, MenuSettings,
              MenuLepLogs, MenuExit, MenuStartScience, MenuStopScience };

std::wstring baseDir;
std::wstring logPath;
HWND windowHandle = nullptr;
NOTIFYICONDATAW iconData{};
HICON stoppedIcon = nullptr;
HICON runningIcon = nullptr;
Config config;
std::array<std::unique_ptr<Session>, 2> sessions = {
    std::make_unique<Session>(), std::make_unique<Session>()};
std::array<Config, 2> runningConfigs;
const std::array<std::wstring, 2> applications = {L"desktop", L"science"};
size_t operationIndex = 0;

bool anyActive() {
    return sessions[0]->active() || sessions[1]->active();
}

DWORD totalProcessCount() {
    return sessions[0]->processCount() + sessions[1]->processCount();
}
struct OperationResult {
    Config config;
    bool configurationReady = false;
    std::unique_ptr<Session> started;
    std::wstring error, message;
};
std::future<OperationResult> operation;
Handle operationCancel;
bool testing = false;
std::wstring configError;


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
    if (windowHandle && !showUiNotice(text, true)) MessageBoxW(windowHandle, text.c_str(), L"ClaudeProtect", MB_OK | MB_ICONERROR);
}

void showInfo(const std::wstring& text) {
    if (windowHandle && !showUiNotice(text, false)) MessageBoxW(windowHandle, text.c_str(), L"ClaudeProtect", MB_OK | MB_ICONINFORMATION);
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

void startClaude(size_t index = 0);
void runSelfTest();
void updateIcon();

void reloadConfig() {
    Config fresh;
    std::wstring error;
    if (loadAndPrepare(fresh, error)) {
        config = fresh;
        configError.clear();
    } else {
        configError = error;
    }
    updateIcon();
}

UiActions controlActions() {
    UiActions actions;
    actions.status = [] {
        UiStatus status;
        status.active = anyActive();
        status.processCount = totalProcessCount();
        status.timezone = config.timezoneId;
        status.locale = config.locale;
        status.verify = config.verifyMode;
        status.busy = operation.valid();
        status.busyLabel = testing ? L"正在执行环境自检" : L"正在预检并启动应用";
        status.detail = configError;
        for (size_t i = 0; i < sessions.size(); ++i) {
            const auto& current = sessions[i]->active() ? runningConfigs[i] : config;
            status.applications.push_back({applications[i], applicationName(applications[i]),
                current.timezoneId, current.locale, current.verifyMode,
                sessions[i]->active(), operation.valid() && !testing && operationIndex == i,
                sessions[i]->processCount()});
        }
        return status;
    };
    actions.command = [](UiCommand command, const std::wstring& application) {
        size_t index = application == L"science" ? 1 : 0;
        switch (command) {
        case UiCommand::Start: startClaude(index); break;
        case UiCommand::Stop: sessions[index]->stop(L"panel-stop"); updateIcon(); break;
        case UiCommand::SelfTest: runSelfTest(); break;
        case UiCommand::Reload: reloadConfig(); break;
        }
    };
    return actions;
}

void openPanel(UiPage page) {
    std::wstring error;
    if (!openControlWindow(baseDir, page, controlActions(), error)) showError(error);
}

void openFolder(const std::wstring& path) {
    if (dirExists(path))
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
    else if (dirExists(directory(path)))
        ShellExecuteW(nullptr, L"open", directory(path).c_str(), nullptr, nullptr, SW_SHOW);
}

void updateIcon() {
    if (!iconData.hWnd) return;
    auto icon = anyActive() ? runningIcon : stoppedIcon;
    std::wstring tip = L"ClaudeProtect - ";
    if (operation.valid())
        tip += testing ? L"正在执行环境自检" : L"正在启动";
    else if (!configError.empty())
        tip += L"配置需要检查";
    else if (anyActive())
        tip += L"受监管运行中（" + std::to_wstring(totalProcessCount()) + L" 个进程）";
    else
        tip += L"待机";
    if (iconData.hIcon == icon && tip == iconData.szTip) return;
    iconData.hIcon = icon;
    wcsncpy(iconData.szTip, tip.c_str(), 127);
    iconData.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &iconData);
}

std::wstring lepStatus() {
    std::wstring error, warning;
    if (!checkLepFiles(config, error, warning)) return L"LEP 组件缺失";
    return warning.empty() ? L"LEP 已就绪" : L"LEP 已就绪（缺少 x86 组件）";
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
    for (size_t i = 0; i < sessions.size(); ++i) {
        auto name = applicationName(applications[i]);
        AppendMenuW(menu, MF_STRING | ((sessions[i]->active() || operation.valid()) ? MF_GRAYED : 0),
                    i == 0 ? MenuStart : MenuStartScience, (L"启动 " + name).c_str());
        AppendMenuW(menu, MF_STRING | (!sessions[i]->active() ? MF_GRAYED : 0),
                    i == 0 ? MenuStop : MenuStopScience, (L"停止 " + name).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring status = configError.empty()
                              ? L"虚拟环境  " + config.timezoneId + L" / " + config.locale +
                                    L" - " + lepStatus()
                              : L"配置有误 · 请打开设置检查";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (operation.valid() ? MF_GRAYED : 0), MenuSelfTest,
                L"运行环境自检");
    AppendMenuW(menu, MF_STRING, MenuPanel, L"打开控制面板");
    AppendMenuW(menu, MF_STRING, MenuSettings, L"设置…");
    AppendMenuW(menu, MF_STRING, MenuLepLogs, L"打开 LEP 日志目录");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuExit, L"退出 ClaudeProtect");

    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(windowHandle);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, windowHandle, nullptr);
    PostMessageW(windowHandle, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void beginOperation(bool selfTest, size_t index) {
    if (operation.valid() || (!selfTest && sessions[index]->active())) return;
    operationIndex = index;
    operationCancel.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!operationCancel.h) { showError(L"无法创建后台操作，" + winErrorText(GetLastError())); return; }
    testing = selfTest;
    HANDLE cancel = operationCancel;
    operation = std::async(std::launch::async, [selfTest, cancel, index] {
        OperationResult result;
        if (!ensureConfigFile(result.error) ||
            !loadApplicationConfig(baseDir, applications[index], result.config, result.error)) return result;
        result.configurationReady = true;
        auto& fresh = result.config;
        if (!selfTest && (fresh.executable.empty() || !fileExists(fresh.executable))) {
            auto candidates = discoverApplicationExecutables(fresh.application);
            if (!candidates.empty()) fresh.executable = candidates.front();
        }
        auto worker = std::make_unique<Session>();
        if (!selfTest) {
            if (fresh.executable.empty()) {
                result.error = L"未找到目标应用，请在“启动与路径”中选择可执行文件。";
                return result;
            }
            LaunchSpec spec;
            spec.executable = fresh.executable;
            spec.arguments = fresh.extraArguments;
            spec.claudeScience = fresh.application == L"science" || isClaudeSciencePath(fresh.executable);
            spec.claudeDesktop = !spec.claudeScience;
            spec.useLep = true;
            if (worker->start(fresh, spec, result.error, cancel)) result.started = std::move(worker);
            return result;
        }
        std::wstring warning;
        if (!checkLepFiles(fresh, result.error, warning)) return result;
        auto probeLog = joinPath(baseDir, applications[index] == L"science" ? L"selftest-science.log" : L"selftest.log");
        std::wstring tail;
        int code = worker->runProbe(fresh, probeArguments(fresh, 0), probeLog, 30000, result.error, tail, cancel);
        if (code < 0) return result;
        if (code == 0) {
            result.message = L"环境自检通过。\n\n时区  " + fresh.timezoneId +
                L"\n语言与区域  " + fresh.locale +
                L"\n\n父子进程的环境与 Job 成员关系检查均已通过。\n完整报告  " + probeLog;
        } else {
            result.error = L"环境自检失败（退出码 " + std::to_wstring(code) + L"）。\n完整报告  " + probeLog;
        }
        return result;
    });
    updateIcon();
}

void startClaude(size_t index) { beginOperation(false, index); }
void runSelfTest() { beginOperation(true, 0); }

void pollOperation() {
    if (!operation.valid() || operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    try {
        auto result = operation.get();
        operationCancel.reset();
        if (result.configurationReady) {
            runningConfigs[operationIndex] = result.config;
            configError.clear();
        } else configError = result.error;
        if (result.started) sessions[operationIndex] = std::move(result.started);
        updateIcon();
        if (!result.error.empty()) showError(result.error);
        else if (!result.message.empty()) { logLine(logPath, result.message); showInfo(result.message); }
    } catch (const std::exception& error) {
        operationCancel.reset();
        showError(L"后台操作失败，" + widen(error.what()));
    }
}

void cancelOperation() {
    if (!operation.valid()) return;
    SetEvent(operationCancel);
    try { operation.get(); } catch (const std::exception&) {}
    operationCancel.reset();
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
        else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
            PostMessageW(hwnd, WM_COMMAND, MenuPanel, 0);
        return 0;
    case WM_TIMER: {
        pollOperation();
        for (size_t i = 0; i < sessions.size(); ++i) {
            sessions[i]->poll();
            auto failure = sessions[i]->takeFailure();
            if (!failure.empty()) showError(applicationName(applications[i]) + L" 会话已停止，" + failure);
        }
        updateIcon();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case MenuStart: startClaude(); break;
        case MenuStop: sessions[0]->stop(L"tray-stop"); updateIcon(); break;
        case MenuStartScience: startClaude(1); break;
        case MenuStopScience: sessions[1]->stop(L"tray-stop"); updateIcon(); break;
        case MenuSelfTest: runSelfTest(); break;
        case MenuPanel: reloadConfig(); openPanel(UiPage::Overview); break;
        case MenuSettings: openPanel(UiPage::Environment); break;
        case MenuLepLogs: openFolder(joinPath(config.lepDir, L"Log")); break;
        case MenuExit: exitTray(); break;
        }
        return 0;
    case WM_QUERYENDSESSION:
        cancelOperation();
        for (auto& managed : sessions) managed->stop(L"session-end");
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        closeControlWindow();
        cancelOperation();
        for (auto& managed : sessions) managed->stop(L"supervisor-exit");
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
        L"ClaudeProtect - Claude Desktop / Science time zone and locale supervisor\r\n"
        L"\r\n"
        L"Tray mode: run without arguments.\r\n"
        L"\r\n"
        L"Commands:\r\n"
        L"  --edit-settings         Open the graphical settings panel\r\n"
        L"  --check                 Validate configuration, LEP files and discovery.\r\n"
        L"  --print-config          Print resolved configuration and LEPB fields.\r\n"
        L"  --discover              List discovered Desktop and Science launchers.\r\n"
        L"  --selftest [--seconds N]  Run EnvironmentProbe under LEP (parent and child).\r\n"
        L"  --run [options]         Launch the configured application in a managed Job.\r\n"
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
        auto science = discoverScienceExecutables();
        candidates.insert(candidates.end(), science.begin(), science.end());
        if (candidates.empty()) {
            consoleWrite(L"No Claude Desktop or Claude Science executables found.\r\n", true);
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

        auto candidates = discoverApplicationExecutables(cfg.application);
        consoleWrite(L"Discovered " + applicationName(cfg.application) + L" executables: " +
                     std::to_wstring(candidates.size()) + L"\r\n");
        for (const auto& candidate : candidates) consoleWrite(L"  " + candidate + L"\r\n");

        DWORD unmanagedPid = 0;
        std::wstring unmanagedPath, warning;
        if (findUnmanagedApplication(cfg.application, cfg.executable, unmanagedPid, unmanagedPath, warning))
            consoleWrite(L"Unmanaged " + applicationName(cfg.application) + L" running: pid " +
                             std::to_wstring(unmanagedPid) + L" " + unmanagedPath + L"\r\n",
                         true);
        else
            consoleWrite(L"No unmanaged " + applicationName(cfg.application) + L" detected.\r\n");
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
            auto candidates = discoverApplicationExecutables(cfg.application);
            if (!candidates.empty()) target = candidates.front();
        }
        if (target.empty() || !fileExists(target)) {
            consoleWrite(L"Target executable not found.\r\n", true);
            return 1;
        }
        LaunchSpec spec;
        spec.executable = target;
        spec.claudeScience = isClaudeSciencePath(target) ||
            (cfg.application == L"science" && lower(target) == lower(cfg.executable));
        spec.claudeDesktop = !spec.claudeScience && isClaudeDesktopPath(target, cfg.executable);
        spec.useLep = !direct;
        if (spec.claudeDesktop || spec.claudeScience) spec.arguments = cfg.extraArguments;
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

    // Keep older releases' same-directory activation while allowing a separate
    // configuration directory to supervise another application independently.
    HWND legacy = FindWindowW(L"ClaudeProtectTray", L"ClaudeProtect");
    DWORD legacyPid = 0;
    std::wstring legacyPath;
    if (legacy) GetWindowThreadProcessId(legacy, &legacyPid);
    if (legacyPid && processImagePath(legacyPid, legacyPath) &&
        lower(directory(legacyPath)) == lower(baseDir)) {
        PostMessageW(legacy, WM_COMMAND, editSettings ? MenuSettings : MenuPanel, 0);
        return 0;
    }
    auto instanceKey = std::to_wstring(std::hash<std::wstring>{}(lower(baseDir)));
    auto trayClass = L"ClaudeProtectTray." + instanceKey;
    auto mutexName = L"Local\\ClaudeProtect.Tray." + instanceKey;
    Handle singleton(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
    if (!singleton.h) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(trayClass.c_str(), L"ClaudeProtect");
        if (existing) PostMessageW(existing, WM_COMMAND, editSettings ? MenuSettings : MenuPanel, 0);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    logLine(logPath, L"tray started");
    int iconWidth = GetSystemMetrics(SM_CXSMICON);
    int iconHeight = GetSystemMetrics(SM_CYSMICON);
    stoppedIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CLAUDE_STOPPED),
                                               IMAGE_ICON, iconWidth, iconHeight, LR_SHARED));
    runningIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_CLAUDE_RUNNING),
                                               IMAGE_ICON, iconWidth, iconHeight, LR_SHARED));
    if (!stoppedIcon || !runningIcon) {
        logLine(logPath, L"cannot load tray icon resources: " + winErrorText(GetLastError()));
        return 1;
    }
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_CLAUDE_STOPPED));
    windowClass.lpszClassName = trayClass.c_str();
    if (!RegisterClassW(&windowClass)) return 1;
    windowHandle = CreateWindowW(windowClass.lpszClassName, L"ClaudeProtect", WS_OVERLAPPED, 0, 0,
                                 0, 0, nullptr, nullptr, instance, nullptr);
    if (!windowHandle) return 1;

    iconData.cbSize = sizeof(iconData);
    iconData.hWnd = windowHandle;
    iconData.uID = 1;
    iconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    iconData.uCallbackMessage = TrayMessage;
    iconData.hIcon = stoppedIcon;
    wcscpy(iconData.szTip, L"ClaudeProtect - 待机");
    if (!Shell_NotifyIconW(NIM_ADD, &iconData)) {
        logLine(logPath, L"tray icon creation failed");
        DestroyWindow(windowHandle);
        return 1;
    }
    SetTimer(windowHandle, 1, 500, nullptr);
    PostMessageW(windowHandle, WM_COMMAND, editSettings ? MenuSettings : MenuPanel, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    logLine(logPath, L"tray exited");
    return 0;
}
