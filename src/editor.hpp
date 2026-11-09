#pragma once
#include "config.hpp"

// Native Win32 windows: no dependency on the installed Notepad package.
bool openTextWindow(HWND owner, const std::wstring& path, bool settings, std::wstring& error);
bool translateTextWindowMessage(MSG& message);
