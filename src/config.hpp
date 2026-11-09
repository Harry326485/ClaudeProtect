#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>

struct Config {
    std::wstring baseDir;
    std::wstring iniPath;
    std::wstring logPath;
    std::wstring lepDir; // Fixed to baseDir/lep; INI overrides are ignored.
    std::wstring probePath;

    std::wstring executable;
    std::wstring timezoneId;   // Windows time zone ID required by LEP.
    std::wstring timezoneIana; // Diagnostic IANA name derived automatically from timezoneId.
    std::wstring timezonePosix; // Derived POSIX TZ for tools without an IANA database.
    std::wstring locale;       // BCP-47 locale name, e.g. en-US.
    std::wstring standardName; // Optional LEP display-name overrides.
    std::wstring daylightName;
    std::wstring extraArguments;

    ULONG ansiCodePage = 0; // 0 = derive from locale.
    ULONG oemCodePage = 0;
    ULONG localeId = 0;
    ULONG fontCharset = 0;
    ULONG registryRedirectionMode = 2;
    ULONG uiLanguageMode = 2;

    std::wstring verifyMode = L"strict"; // strict | warn | off
    int verifyGraceSeconds = 8;
    std::wstring preflight = L"auto";    // auto | require | off

    std::vector<std::wstring> warnings;
};

// baseDir is the directory containing ClaudeProtect.exe. Reads
// ClaudeProtect.ini when present and fills defaults otherwise. Resolution and
// validation of time zone, locale and modes happens in prepareConfig.
bool loadConfig(const std::wstring& baseDir, Config& config, std::wstring& error);
bool writeDefaultConfig(const std::wstring& iniPath, const std::wstring& executable,
                        std::wstring& error);
bool readConfigText(const std::wstring& path, std::wstring& text, std::wstring& error);
// Validate before atomically replacing the INI; invalid edits leave it intact.
bool saveConfigText(const std::wstring& baseDir, const std::wstring& text, std::wstring& error);

using SettingsValues = std::map<std::wstring, std::wstring>;
// Raw values preserve automatic (0/empty) settings and allow repairing invalid INIs in the UI.
bool loadSettings(const std::wstring& baseDir, SettingsValues& values,
                  std::wstring& source, std::wstring& error);
// Validates and atomically saves a complete form, retaining comments and unknown keys.
// Refuses to overwrite a file changed since it was loaded.
bool saveSettings(const std::wstring& baseDir, const SettingsValues& values,
                  const std::wstring& source, std::wstring& error);

// Resolves derived values and validates the LEP environment input.
bool prepareConfig(Config& config, std::wstring& error);

// Checks that the configured LEP directory has a matched loader/core pair.
bool checkLepFiles(const Config& config, std::wstring& error, std::wstring& warning);

std::wstring describeConfig(const Config& config);
std::wstring mapWindowsTimeZoneToIana(const std::wstring& id);
std::wstring mapWindowsTimeZoneToPosix(const std::wstring& id);

// Sorted newest-first. Only Claude Desktop executables are returned; Claude
// Code installations such as npm's claude.exe are excluded.
std::vector<std::wstring> discoverClaudeExecutables();
bool isClaudeDesktopPath(const std::wstring& path, const std::wstring& target);

// Finds a running Claude Desktop executable that is not in our managed job.
bool findUnmanagedClaudeDesktop(const std::wstring& target, DWORD& pid, std::wstring& path,
                                std::wstring& warning);
