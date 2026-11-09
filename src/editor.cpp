#include "editor.hpp"
#include "win.hpp"
#include "resource.h"

namespace {
HWND settingsWindow = nullptr;
HWND logWindow = nullptr;
constexpr int SaveButton = 101;

struct Editor {
    std::wstring path;
    std::wstring text;
    bool settings;
    HWND edit = nullptr;
    HWND status = nullptr;
    HWND save = nullptr;
    HWND close = nullptr;
};

bool save(HWND window, Editor& editor) {
    int size = GetWindowTextLengthW(editor.edit);
    std::wstring text(size + 1, 0);
    GetWindowTextW(editor.edit, &text[0], size + 1);
    text.resize(size);
    std::wstring error;
    if (!saveConfigText(directory(editor.path), text, error)) {
        MessageBoxW(window, error.c_str(), L"Cannot save settings", MB_OK | MB_ICONERROR);
        return false;
    }
    SendMessageW(editor.edit, EM_SETMODIFY, FALSE, 0);
    SetWindowTextW(editor.status, L"Saved. Changes apply on the next Claude launch.");
    return true;
}

LRESULT CALLBACK editorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        editor = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        auto instance = GetModuleHandleW(nullptr);
        editor->status = CreateWindowW(L"STATIC", editor->path.c_str(), WS_CHILD | WS_VISIBLE,
                                       0, 0, 0, 0, window, nullptr, instance, nullptr);
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                      ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN;
        if (!editor->settings) style |= ES_READONLY;
        editor->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
                                       0, 0, 0, 0, window, nullptr, instance, nullptr);
        SendMessageW(editor->edit, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
        SetWindowTextW(editor->edit, editor->text.c_str());
        SendMessageW(editor->edit, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FIXED_FONT), TRUE);
        SendMessageW(editor->edit, EM_SETMODIFY, FALSE, 0);
        if (editor->settings)
            editor->save = CreateWindowW(L"BUTTON", L"&Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          0, 0, 0, 0, window, (HMENU)SaveButton, instance, nullptr);
        editor->close = CreateWindowW(L"BUTTON", L"&Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       0, 0, 0, 0, window, (HMENU)IDCANCEL, instance, nullptr);
        SetFocus(editor->edit);
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam), height = HIWORD(lParam);
        MoveWindow(editor->status, 12, 10, width - 24, 24, TRUE);
        MoveWindow(editor->edit, 12, 40, width - 24, height - 94, TRUE);
        if (editor->save) MoveWindow(editor->save, width - 202, height - 42, 88, 30, TRUE);
        MoveWindow(editor->close, width - 102, height - 42, 88, 30, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {480, 300};
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == SaveButton && editor->settings) { save(window, *editor); return 0; }
        if (LOWORD(wParam) == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        break;
    case WM_CLOSE:
        if (editor->settings && SendMessageW(editor->edit, EM_GETMODIFY, 0, 0)) {
            int choice = MessageBoxW(window, L"Save changes to settings?", L"ClaudeProtect",
                                      MB_YESNOCANCEL | MB_ICONQUESTION);
            if (choice == IDCANCEL || (choice == IDYES && !save(window, *editor))) return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        if (settingsWindow == window) settingsWindow = nullptr;
        if (logWindow == window) logWindow = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete editor;
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool openTextWindow(HWND owner, const std::wstring& path, bool settings, std::wstring& error) {
    HWND& existing = settings ? settingsWindow : logWindow;
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return true;
    }
    std::wstring text;
    if (!readConfigText(path, text, error)) return false;
    WNDCLASSW wc{};
    wc.lpfnWndProc = editorProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_CLAUDE_STOPPED));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ClaudeProtectTextWindow";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"Cannot register settings window: " + winErrorText(GetLastError());
        return false;
    }
    auto editor = new Editor{path, text, settings};
    // The window owns editor after WM_NCCREATE and releases it on WM_NCDESTROY.
    existing = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName,
                               settings ? L"ClaudeProtect - Edit settings" : L"ClaudeProtect - Log",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 860, 640,
                               owner, nullptr, wc.hInstance, editor);
    if (!existing) {
        error = L"Cannot open settings window: " + winErrorText(GetLastError());
        return false;
    }
    ShowWindow(existing, SW_SHOW);
    SetForegroundWindow(existing);
    return true;
}

bool translateTextWindowMessage(MSG& message) {
    for (HWND window : {settingsWindow, logWindow}) {
        if (!window || (message.hwnd != window && !IsChild(window, message.hwnd))) continue;
        if (message.message == WM_KEYDOWN && message.wParam == 'S' && GetKeyState(VK_CONTROL) < 0) {
            SendMessageW(window, WM_COMMAND, SaveButton, 0);
            return true;
        }
        return IsDialogMessageW(window, &message) != FALSE;
    }
    return false;
}
