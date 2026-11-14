#include "ui.hpp"
#include "win.hpp"
#include "resource.h"
#include "../third_party/picojson/picojson.h"
#include <WebView2.h>
#include <shlobj.h>
#include <memory>
#include <map>
#include <cstdlib>

namespace {
using Json = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
constexpr UINT ClosePanel = WM_APP + 20;

// WebView2's generated COM interfaces work with MinGW without a WRL dependency.
template<class T> class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* value) : value_(value) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    T** put() { reset(); return &value_; }
    void reset() { if (value_) value_->Release(); value_ = nullptr; }
private:
    T* value_ = nullptr;
};

IID interfaceId(const wchar_t* text) {
    IID result{};
    IIDFromString(text, &result);
    return result;
}

template<class Interface, class... Args> class Callback final : public Interface {
public:
    Callback(const wchar_t* id, std::function<HRESULT(Args...)> function)
        : id_(interfaceId(id)), function_(std::move(function)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != id_) return E_NOINTERFACE;
        *result = static_cast<Interface*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = InterlockedDecrement(&references_);
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return function_(args...); }
private:
    LONG references_ = 1;
    IID id_;
    std::function<HRESULT(Args...)> function_;
};

template<class Interface, class... Args, class Function>
ComPtr<Interface> callback(const wchar_t* id, Function function) {
    return ComPtr<Interface>(new Callback<Interface, Args...>(id, std::move(function)));
}

struct Panel {
    HWND window = nullptr;
    UiPage page = UiPage::Overview;
    UiActions actions;
    std::wstring baseDir, source, lastStatus, documentUri;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    bool closed = false, ready = false, dirty = false, initialNavigation = true;
    int dpi = 96;
};
std::shared_ptr<Panel> current;

std::string stringValue(const Json& object, const std::string& key) {
    const auto& value = object.get(key);
    return value.is<std::string>() ? value.get<std::string>() : std::string();
}

void send(Panel& panel, const Json& message) {
    if (!panel.closed && panel.webview) panel.webview->PostWebMessageAsJson(widen(message.serialize()).c_str());
}

Object statusData(Panel& panel) {
    auto status = panel.actions.status();
    Array applications;
    for (const auto& app : status.applications) {
        applications.push_back(Json(Object{{"id", Json(utf8(app.id))}, {"name", Json(utf8(app.name))},
            {"active", Json(app.active)}, {"busy", Json(app.busy)},
            {"processCount", Json((double)app.processCount)}, {"timezone", Json(utf8(app.timezone))},
            {"locale", Json(utf8(app.locale))}, {"verify", Json(utf8(app.verify))}}));
    }
    return {{"applications", Json(applications)}, {"active", Json(status.active)}, {"busy", Json(status.busy)},
            {"busyLabel", Json(utf8(status.busyLabel))}, {"processCount", Json((double)status.processCount)},
            {"timezone", Json(utf8(status.timezone))}, {"locale", Json(utf8(status.locale))},
            {"verify", Json(utf8(status.verify))}, {"detail", Json(utf8(status.detail))}};
}

void pushStatus(Panel& panel) {
    if (panel.closed || !panel.webview || !panel.ready || IsIconic(panel.window)) return;
    Json message(Object{{"type", Json("status")}, {"data", Json(statusData(panel))}});
    auto text = widen(message.serialize());
    if (text == panel.lastStatus) return;
    panel.lastStatus = text;
    panel.webview->PostWebMessageAsJson(text.c_str());
}

Object navigation(UiPage page) {
    std::string view = "settings", tab = "general";
    switch (page) {
    case UiPage::Overview: view = "overview"; break;
    case UiPage::Environment: break;
    case UiPage::Protection: tab = "protection"; break;
    case UiPage::Compatibility: tab = "advanced"; break;
    case UiPage::Launch: tab = "launch"; break;
    }
    return {{"view", Json(view)}, {"page", Json(tab)}};
}

Object settingsData(Panel& panel, std::wstring& error) {
    SettingsValues values;
    if (!loadSettings(panel.baseDir, values, panel.source, error)) return {};
    Object output;
    for (const auto& entry : values) output[utf8(entry.first)] = Json(utf8(entry.second));
    return output;
}

std::wstring browse(Panel& panel, std::wstring& error) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IFileOpenDialog, (void**)dialog.put());
    if (FAILED(result)) { error = L"无法创建路径选择窗口。"; return L""; }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       FOS_FILEMUSTEXIST);
    dialog->SetTitle(L"选择 Claude Desktop 或 Claude Science");
    {
        const COMDLG_FILTERSPEC filters[] = {{L"Claude (claude.exe / claude-science.exe)", L"claude.exe;claude-science.exe"},
                                             {L"可执行文件 (*.exe)", L"*.exe"}};
        dialog->SetFileTypes(2, filters);
    }
    result = dialog->Show(panel.window);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return L"";
    ComPtr<IShellItem> item;
    if (FAILED(result) || FAILED(dialog->GetResult(item.put()))) {
        error = L"无法选择路径，请重试。";
        return L"";
    }
    LPWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        error = L"请选择文件系统中的有效路径。";
        return L"";
    }
    std::wstring value = path;
    CoTaskMemFree(path);
    return value;
}

// A small curated catalog; offsets and DST labels follow this year's Windows rules.
Array timezoneChoices() {
    static const struct { const wchar_t* id; const wchar_t* cities; } catalog[] = {
        {L"Dateline Standard Time", L"贝克岛（无人常住）"},
        {L"UTC-11", L"帕果帕果"},
        {L"Hawaiian Standard Time", L"檀香山"},
        {L"Alaskan Standard Time", L"安克雷奇"},
        {L"Pacific Standard Time", L"洛杉矶 / 温哥华"},
        {L"Mountain Standard Time", L"丹佛"},
        {L"US Mountain Standard Time", L"凤凰城"},
        {L"Central Standard Time", L"芝加哥"},
        {L"Central Standard Time (Mexico)", L"墨西哥城"},
        {L"Eastern Standard Time", L"纽约 / 多伦多"},
        {L"SA Western Standard Time", L"圣胡安"},
        {L"Argentina Standard Time", L"布宜诺斯艾利斯"},
        {L"E. South America Standard Time", L"圣保罗"},
        {L"UTC-02", L"雷梅迪乌斯镇"},
        {L"Cape Verde Standard Time", L"普拉亚"},
        {L"Azores Standard Time", L"蓬塔德尔加达"},
        {L"GMT Standard Time", L"伦敦 / 里斯本"},
        {L"Greenwich Standard Time", L"雷克雅未克"},
        {L"Romance Standard Time", L"巴黎"},
        {L"W. Europe Standard Time", L"柏林 / 罗马"},
        {L"GTB Standard Time", L"雅典"},
        {L"FLE Standard Time", L"赫尔辛基"},
        {L"South Africa Standard Time", L"开普敦"},
        {L"E. Africa Standard Time", L"内罗毕"},
        {L"Arab Standard Time", L"利雅得"},
        {L"Arabian Standard Time", L"迪拜"},
        {L"Pakistan Standard Time", L"卡拉奇"},
        {L"Bangladesh Standard Time", L"达卡"},
        {L"SE Asia Standard Time", L"曼谷 / 河内 / 雅加达"},
        {L"Singapore Standard Time", L"新加坡"},
        {L"W. Australia Standard Time", L"珀斯"},
        {L"Taipei Standard Time", L"台北"},
        {L"Tokyo Standard Time", L"东京"},
        {L"Korea Standard Time", L"首尔"},
        {L"E. Australia Standard Time", L"布里斯班"},
        {L"AUS Eastern Standard Time", L"悉尼 / 墨尔本"},
        {L"Central Pacific Standard Time", L"霍尼亚拉 / 努美阿"},
        {L"New Zealand Standard Time", L"奥克兰"},
        {L"Fiji Standard Time", L"苏瓦"},
        {L"Tonga Standard Time", L"努库阿洛法"},
        {L"Line Islands Standard Time", L"伦敦（基里蒂马蒂岛）"},
    };
    std::map<std::wstring, DYNAMIC_TIME_ZONE_INFORMATION> installed;
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &zone) == ERROR_SUCCESS; ++i)
        installed.emplace(zone.TimeZoneKeyName, zone);
    auto offset = [](LONG bias) {
        int minutes = -(int)bias;
        wchar_t text[24];
        swprintf(text, 24, L"UTC%lc%02d:%02d", minutes < 0 ? L'−' : L'+',
                 std::abs(minutes) / 60, std::abs(minutes) % 60);
        return std::wstring(text);
    };
    SYSTEMTIME now{};
    GetSystemTime(&now);
    Array choices;
    for (const auto& entry : catalog) {
        auto found = installed.find(entry.id);
        if (found == installed.end()) continue;
        TIME_ZONE_INFORMATION rules{};
        if (!GetTimeZoneInformationForYear(now.wYear, &found->second, &rules)) continue;
        auto label = offset(rules.Bias + rules.StandardBias) + L" · " + entry.cities;
        if (rules.DaylightDate.wMonth && rules.StandardDate.wMonth && rules.DaylightBias != rules.StandardBias)
            label += L"（夏令时 " + offset(rules.Bias + rules.DaylightBias) + L"）";
        choices.emplace_back(Object{{"id", Json(utf8(entry.id))}, {"label", Json(utf8(label))}});
    }
    return choices;
}

void handleMessage(Panel& panel, const Json& message) {
    const auto action = stringValue(message, "action");
    if (action == "dirty") {
        const auto& dirty = message.get("dirty");
        if (dirty.is<bool>()) panel.dirty = dirty.get<bool>();
        return;
    }
    const auto& id = message.get("id");
    if (!id.is<double>()) return;
    Object result;
    std::wstring error;
    if (action == "load") {
        result["values"] = Json(settingsData(panel, error));
        if (error.empty()) {
            panel.ready = true;
            panel.dirty = false;
            panel.actions.command(UiCommand::Reload, L"");
            result["status"] = Json(statusData(panel));
            auto target = navigation(panel.page);
            result.insert(target.begin(), target.end());
            auto zones = timezoneChoices();
            result["timezones"] = Json(zones);
        }
    } else if (action == "save") {
        const auto& input = message.get("values");
        const char* keys[] = {"timezone", "locale",
            "verifyinjection", "verifygraceseconds", "preflight", "ansicodepage", "oemcodepage",
            "fontcharset", "registryredirectionmode", "hookuilanguagemode", "desktopexecutable",
            "scienceexecutable", "desktoparguments"};
        SettingsValues values;
        if (!input.is<Object>()) error = L"设置表单无效，请重新加载后再试。";
        for (const auto key : keys) {
            if (!error.empty()) break;
            const auto& value = input.get(key);
            if (!value.is<std::string>() || value.get<std::string>().size() > 131068) {
                error = L"设置内容无效，请重新加载后再试。";
                break;
            }
            values[widen(key)] = widen(value.get<std::string>());
        }
        if (error.empty() && saveSettings(panel.baseDir, values, panel.source, error)) {
            result["values"] = Json(settingsData(panel, error));
            panel.dirty = false;
            panel.actions.command(UiCommand::Reload, L"");
        }
    } else if (action == "browse") {
        auto key = stringValue(message, "key");
        if (key != "desktopexecutable" && key != "scienceexecutable") error = L"路径设置无效。";
        else result["path"] = Json(utf8(browse(panel, error)));
    } else if (action == "start" || action == "stop" || action == "selftest") {
        auto status = panel.actions.status();
        auto application = stringValue(message, "application");
        auto selected = std::find_if(status.applications.begin(), status.applications.end(),
            [&](const auto& app) { return utf8(app.id) == application; });
        if (action != "selftest" && selected == status.applications.end())
            error = L"请选择 Desktop 或 Science。";
        else if (action != "stop" && status.busy) error = L"已有启动或自检正在进行，请稍候。";
        else if (action == "start" && selected->active) error = L"该应用已受监管运行。";
        else panel.actions.command(action == "start" ? UiCommand::Start :
            action == "stop" ? UiCommand::Stop : UiCommand::SelfTest, widen(application));
    } else if (action == "close") {
        PostMessageW(panel.window, ClosePanel, 0, 0);
    } else {
        error = L"无法识别的界面操作。";
    }
    send(panel, Json(Object{{"type", Json("response")}, {"id", id}, {"ok", Json(error.empty())},
                            {"data", Json(result)}, {"error", Json(utf8(error))}}));
    pushStatus(panel);
}

void initializationFailed(const std::shared_ptr<Panel>& panel, HRESULT result) {
    if (panel->closed) return;
    auto message = L"无法加载 WebView2 界面（" + winErrorText((DWORD)result) +
        L"）。请安装或修复 Microsoft Edge WebView2 Evergreen Runtime 后重试。\n\n"
        L"下载地址 https://developer.microsoft.com/microsoft-edge/webview2/";
    MessageBoxW(panel->window, message.c_str(), L"ClaudeProtect", MB_OK | MB_ICONERROR);
    DestroyWindow(panel->window);
}

void initializeWebview(const std::shared_ptr<Panel>& panel) {
    // Loader stays mapped for asynchronous completion callbacks, but the browser is window-scoped.
    static HMODULE loader = LoadLibraryExW(joinPath(panel->baseDir, L"WebView2Loader.dll").c_str(),
                                           nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) { initializationFailed(panel, HRESULT_FROM_WIN32(GetLastError())); return; }
    using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                 ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = reinterpret_cast<CreateEnvironment>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create) { initializationFailed(panel, E_NOINTERFACE); return; }
    auto environmentHandler = callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}", [panel](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        if (panel->closed) return S_OK;
        if (FAILED(result) || !environment) { initializationFailed(panel, result); return S_OK; }
        environment->AddRef();
        *panel->environment.put() = environment;
        auto controllerHandler = callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
            L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}", [panel](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (panel->closed) { if (controller) controller->Close(); return S_OK; }
            if (FAILED(result) || !controller) { initializationFailed(panel, result); return S_OK; }
            controller->AddRef();
            *panel->controller.put() = controller;
            result = controller->get_CoreWebView2(panel->webview.put());
            if (FAILED(result) || !panel->webview) { initializationFailed(panel, FAILED(result) ? result : E_FAIL); return S_OK; }
            ComPtr<ICoreWebView2Settings> settings;
            result = panel->webview->get_Settings(settings.put());
            if (FAILED(result) || !settings) { initializationFailed(panel, FAILED(result) ? result : E_FAIL); return S_OK; }
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
            ComPtr<ICoreWebView2_19> memorySettings;
            if (SUCCEEDED(panel->webview->QueryInterface(interfaceId(L"{6921f954-79b0-437f-a997-c85811897c68}"), (void**)memorySettings.put())))
                memorySettings->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
            ComPtr<ICoreWebView2Settings3> settings3;
            if (SUCCEEDED(settings->QueryInterface(interfaceId(L"{fdb5ab74-af33-4854-84f0-0a631deb5eba}"), (void**)settings3.put())))
                settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            std::weak_ptr<Panel> weak = panel;
            auto messages = callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}", [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                auto panel = weak.lock();
                if (!panel || panel->closed) return S_OK;
                LPWSTR source = nullptr, raw = nullptr;
                if (FAILED(args->get_Source(&source))) return S_OK;
                bool trusted = source && (std::wstring(source) == L"about:blank" || std::wstring(source) == panel->documentUri);
                CoTaskMemFree(source);
                if (!trusted || FAILED(args->get_WebMessageAsJson(&raw))) return S_OK;
                std::wstring text = raw ? raw : L"";
                CoTaskMemFree(raw);
                if (text.size() > 128 * 1024) return S_OK;
                Json message;
                if (picojson::parse(message, utf8(text)).empty() && message.is<Object>()) handleMessage(*panel, message);
                return S_OK;
            });
            EventRegistrationToken token{};
            panel->webview->add_WebMessageReceived(messages.get(), &token);
            auto navigation = callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}", [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                auto panel = weak.lock();
                std::wstring target = uri ? uri : L"";
                // Newer Evergreen runtimes report NavigateToString's internal data URL.
                bool bundled = target == L"about:blank" || target.rfind(L"data:text/html;charset=utf-8;base64,", 0) == 0;
                if (!panel || !panel->initialNavigation || !bundled) args->put_Cancel(TRUE);
                else { panel->initialNavigation = false; panel->documentUri = target; }
                CoTaskMemFree(uri);
                return S_OK;
            });
            panel->webview->add_NavigationStarting(navigation.get(), &token);
            auto newWindow = callback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}", [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                return args->put_Handled(TRUE);
            });
            panel->webview->add_NewWindowRequested(newWindow.get(), &token);
            auto permissions = callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
                L"{15e1c6a3-c72a-4df3-91d7-d097fbec6bfd}", [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            });
            panel->webview->add_PermissionRequested(permissions.get(), &token);
            auto failed = callback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
                L"{79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1}", [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                auto panel = weak.lock();
                if (panel && !panel->closed) {
                    MessageBoxW(panel->window, L"界面进程已退出，请从托盘重新打开控制面板。", L"ClaudeProtect", MB_OK | MB_ICONERROR);
                    PostMessageW(panel->window, ClosePanel, 0, 0);
                }
                return S_OK;
            });
            panel->webview->add_ProcessFailed(failed.get(), &token);
            RECT bounds{};
            GetClientRect(panel->window, &bounds);
            controller->put_Bounds(bounds);
            controller->put_IsVisible(!IsIconic(panel->window));
            auto resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_UI), RT_RCDATA);
            auto bytes = resource ? LoadResource(GetModuleHandleW(nullptr), resource) : nullptr;
            auto data = bytes ? static_cast<const char*>(LockResource(bytes)) : nullptr;
            if (!data) { initializationFailed(panel, E_FAIL); return S_OK; }
            auto html = widen(std::string(data, SizeofResource(GetModuleHandleW(nullptr), resource)));
            result = panel->webview->NavigateToString(html.c_str());
            if (FAILED(result)) initializationFailed(panel, result);
            else SetTimer(panel->window, 1, 1000, nullptr);
            return S_OK;
        });
        result = environment->CreateCoreWebView2Controller(panel->window, controllerHandler.get());
        if (FAILED(result)) initializationFailed(panel, result);
        return S_OK;
    });
    // The profile is cached outside the source/build tree and reused across window openings.
    auto profile = joinPath(env(L"LOCALAPPDATA"), L"ClaudeProtect");
    CreateDirectoryW(profile.c_str(), nullptr);
    profile = joinPath(profile, L"WebView2");
    HRESULT result = create(nullptr, profile.c_str(), nullptr, environmentHandler.get());
    if (FAILED(result)) initializationFailed(panel, result);
}

void updateVisibility(const std::shared_ptr<Panel>& panel) {
    if (!panel->controller) return;
    bool minimized = IsIconic(panel->window) != FALSE;
    panel->controller->put_IsVisible(!minimized);
    ComPtr<ICoreWebView2_3> webview3;
    if (SUCCEEDED(panel->webview->QueryInterface(interfaceId(L"{A0D6DF20-3B92-416D-AA0C-437A9C727857}"), (void**)webview3.put()))) {
        if (minimized) {
            std::weak_ptr<Panel> weak = panel;
            auto suspended = callback<ICoreWebView2TrySuspendCompletedHandler, HRESULT, BOOL>(
                L"{00f206a7-9d17-4605-91f6-4e8e4de192e3}", [weak](HRESULT, BOOL) -> HRESULT {
                auto panel = weak.lock();
                if (panel && !panel->closed && !IsIconic(panel->window)) updateVisibility(panel);
                return S_OK;
            });
            webview3->TrySuspend(suspended.get());
        } else webview3->Resume();
    }
    if (!minimized) {
        RECT bounds{};
        GetClientRect(panel->window, &bounds);
        panel->controller->put_Bounds(bounds);
        pushStatus(*panel);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto holder = reinterpret_cast<std::shared_ptr<Panel>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        holder = static_cast<std::shared_ptr<Panel>*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder));
        (*holder)->window = window;
    }
    if (!holder) return DefWindowProcW(window, message, wParam, lParam);
    auto panel = *holder;
    switch (message) {
    case WM_SIZE: updateVisibility(panel); return 0;
    case WM_MOVE:
    case WM_MOVING:
        if (panel->controller) panel->controller->NotifyParentWindowPositionChanged();
        break;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {MulDiv(600, panel->dpi, 96), MulDiv(440, panel->dpi, 96)};
        return 0;
    case WM_DPICHANGED: {
        panel->dpi = HIWORD(wParam);
        auto rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && panel->controller)
            panel->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        break;
    case WM_TIMER: pushStatus(*panel); return 0;
    case WM_CLOSE:
        if (panel->ready && panel->dirty) send(*panel, Json(Object{{"type", Json("closeRequested")}}));
        else DestroyWindow(window);
        return 0;
    case ClosePanel: DestroyWindow(window); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (!panel->ready) {
            RECT rect{};
            GetClientRect(window, &rect);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"正在加载控制面板…", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        panel->closed = true;
        KillTimer(window, 1);
        if (panel->controller) panel->controller->Close();
        panel->webview.reset();
        panel->controller.reset();
        panel->environment.reset();
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (current == panel) current.reset();
        delete holder;
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool openControlWindow(const std::wstring& baseDir, UiPage page,
                       const UiActions& actions, std::wstring& error) {
    if (current && !current->closed) {
        current->page = page;
        if (current->ready) send(*current, Json(Object{{"type", Json("navigate")}, {"data", Json(navigation(page))}}));
        ShowWindow(current->window, IsIconic(current->window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(current->window);
        return true;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_CLAUDE_STOPPED));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ClaudeProtectPanel";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"无法创建控制面板，" + winErrorText(GetLastError());
        return false;
    }
    auto panel = std::make_shared<Panel>();
    panel->baseDir = baseDir;
    panel->page = page;
    panel->actions = actions;
    HDC screen = GetDC(nullptr);
    panel->dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = (std::min)(MulDiv(740, panel->dpi, 96), (int)(work.right - work.left));
    int height = (std::min)(MulDiv(600, panel->dpi, 96), (int)(work.bottom - work.top));
    auto holder = new std::shared_ptr<Panel>(panel);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"ClaudeProtect", WS_OVERLAPPEDWINDOW,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, wc.hInstance, holder);
    if (!window) { error = L"无法打开控制面板，" + winErrorText(GetLastError()); return false; }
    current = panel;
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    initializeWebview(panel);
    return true;
}

void closeControlWindow() {
    if (current && !current->closed) DestroyWindow(current->window);
}

bool showUiNotice(const std::wstring& text, bool error) {
    if (!current || !current->ready || current->closed || IsIconic(current->window)) return false;
    send(*current, Json(Object{{"type", Json("notice")}, {"text", Json(utf8(text))}, {"error", Json(error)}}));
    return true;
}
