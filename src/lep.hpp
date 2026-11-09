#pragma once
#include "lep_abi.hpp"
#include <string>

// Thin dynamic adapter for LocaleEmulatorPlus-Core's LoaderDll_x64.dll.
// The loader and its Core DLL must sit in the same directory; Core resolves
// LocaleEmulatorPlus_x64.dll relative to the loader module.
struct LepLaunchConfig {
    std::wstring timezoneId;          // Windows time zone ID, e.g. "China Standard Time".
    std::wstring standardName;        // Optional override, empty = registry default.
    std::wstring daylightName;        // Optional override, empty = registry default.
    ULONG ansiCodePage = 0;
    ULONG oemCodePage = 0;
    ULONG localeId = 0;
    ULONG fontCharset = 0;
    ULONG registryRedirectionMode = 2;
    ULONG uiLanguageMode = 2;
};

class LepLoader {
public:
    LepLoader() = default;
    ~LepLoader();
    LepLoader(const LepLoader&) = delete;
    LepLoader& operator=(const LepLoader&) = delete;

    // loaderPath must be the absolute path to LoaderDll_x64.dll. On success
    // also reports whether the x86 pair (needed for cross-architecture
    // children) is present in the same directory.
    bool load(const std::wstring& loaderPath, std::wstring& error, std::wstring& warning);
    void unload();
    bool loaded() const { return create_ != nullptr; }
    const std::wstring& loaderPath() const { return loaderPath_; }
    const std::wstring& loaderDirectory() const { return loaderDirectory_; }
    const std::wstring& corePath() const { return corePath_; }

    // Calls LepCreateProcess2 with CREATE_SUSPENDED. On success the returned
    // process is still suspended and the caller owns both handles. The caller
    // is expected to assign the process to a Job Object before resuming.
    // commandLine must point to writable storage.
    LONG createSuspended(const LepLaunchConfig& config, PCWSTR applicationName, PWSTR commandLine,
                         PCWSTR currentDirectory, DWORD creationFlags, LPSTARTUPINFOW startup,
                         LPPROCESS_INFORMATION processInfo, PVOID environment,
                         std::wstring& error) const;

    static lep::EnvironmentBlock buildBlock(const LepLaunchConfig& config);
    static std::wstring describeBlock(const lep::EnvironmentBlock& block);

private:
    HMODULE module_ = nullptr;
    lep::LepCreateProcessFn create_ = nullptr;
    std::wstring loaderPath_;
    std::wstring loaderDirectory_;
    std::wstring corePath_;
};

// Returns the Windows directory used by the current architecture.
std::wstring defaultLoaderFileName();
