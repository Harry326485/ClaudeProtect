#pragma once
#include "config.hpp"
#include <functional>

enum class UiPage { Overview, Environment, Protection, Compatibility, Launch };
enum class UiCommand { Start, Stop, SelfTest, Reload };

struct UiStatus {
    bool active = false;
    bool busy = false;
    DWORD processCount = 0;
    std::wstring timezone, locale, verify, busyLabel;
    std::wstring detail;
};

struct UiActions {
    std::function<UiStatus()> status;
    std::function<void(UiCommand)> command;
};

bool openControlWindow(const std::wstring& baseDir, UiPage page,
                       const UiActions& actions, std::wstring& error);
void closeControlWindow();
bool showUiNotice(const std::wstring& text, bool error);
