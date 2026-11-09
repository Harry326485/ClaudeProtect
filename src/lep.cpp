#include "lep.hpp"
#include "win.hpp"
#include <cwchar>

namespace {

std::wstring safeCopy(const std::wstring& value, size_t capacity) {
    if (value.size() >= capacity) return value.substr(0, capacity - 1);
    return value;
}

} // namespace

std::wstring defaultLoaderFileName() {
#if defined(_M_X64) || defined(__x86_64__)
    return L"LoaderDll_x64.dll";
#else
    return L"LoaderDll_x86.dll";
#endif
}

LepLoader::~LepLoader() {
    unload();
}

void LepLoader::unload() {
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
    }
    create_ = nullptr;
    loaderPath_.clear();
    loaderDirectory_.clear();
    corePath_.clear();
}

bool LepLoader::load(const std::wstring& loaderPath, std::wstring& error, std::wstring& warning) {
    unload();
    warning.clear();
    if (!fileExists(loaderPath)) {
        error = L"LEP loader not found: " + loaderPath;
        return false;
    }
    loaderDirectory_ = directory(loaderPath);
    const std::wstring loaderName = lower(fileName(loaderPath));
    corePath_ = joinPath(loaderDirectory_,
                         loaderName == L"loaderdll_x86.dll" ? L"LocaleEmulatorPlus_x86.dll"
                                                            : L"LocaleEmulatorPlus_x64.dll");
    if (!fileExists(corePath_)) {
        error = L"LEP Core DLL not found next to the loader: " + corePath_;
        return false;
    }

    module_ = LoadLibraryW(loaderPath.c_str());
    if (!module_) {
        error = L"LoadLibraryW failed for " + loaderPath + L": " + winErrorText(GetLastError());
        return false;
    }
    create_ = reinterpret_cast<lep::LepCreateProcessFn>(GetProcAddress(module_, "LepCreateProcess2"));
    if (!create_) {
        error = L"LepCreateProcess2 export not found in " + loaderPath + L": " +
                winErrorText(GetLastError());
        unload();
        return false;
    }
    loaderPath_ = loaderPath;

    // Cross-architecture propagation needs the matched x86 pair in the same
    // directory. Claude Desktop is x64, but native helpers can still be x86.
    const std::wstring x86Loader = joinPath(loaderDirectory_, L"LoaderDll_x86.dll");
    const std::wstring x86Core = joinPath(loaderDirectory_, L"LocaleEmulatorPlus_x86.dll");
    if (!fileExists(x86Loader) || !fileExists(x86Core))
        warning = L"cross-architecture LEP pair missing in " + loaderDirectory_ +
                  L"; x86 children cannot be virtualized";
    return true;
}

lep::EnvironmentBlock LepLoader::buildBlock(const LepLaunchConfig& config) {
    lep::EnvironmentBlock block{};
    block.Size = sizeof(block);
    block.Version = lep::kEnvironmentVersion;
    block.AnsiCodePage = config.ansiCodePage;
    block.OemCodePage = config.oemCodePage;
    block.LocaleId = config.localeId;
    block.FontCharset = config.fontCharset;
    block.RegistryRedirectionMode = config.registryRedirectionMode;
    block.HookUILanguageMode = config.uiLanguageMode;

    auto timezone = safeCopy(config.timezoneId, 128);
    auto standard = safeCopy(config.standardName, 32);
    auto daylight = safeCopy(config.daylightName, 32);
    std::wmemcpy(block.TimezoneId, timezone.c_str(), timezone.size());
    std::wmemcpy(block.StandardNameOverride, standard.c_str(), standard.size());
    std::wmemcpy(block.DaylightNameOverride, daylight.c_str(), daylight.size());
    return block;
}

std::wstring LepLoader::describeBlock(const lep::EnvironmentBlock& block) {
    wchar_t buffer[512];
    swprintf(buffer, 512,
             L"LEPB size=%lu version=%lu acp=%lu oemcp=%lu lcid=0x%04lX charset=%lu registry=%lu "
             L"ui=%lu tz=%ls",
             (unsigned long)block.Size, (unsigned long)block.Version,
             (unsigned long)block.AnsiCodePage, (unsigned long)block.OemCodePage,
             (unsigned long)block.LocaleId, (unsigned long)block.FontCharset,
             (unsigned long)block.RegistryRedirectionMode, (unsigned long)block.HookUILanguageMode,
             block.TimezoneId);
    return buffer;
}

LONG LepLoader::createSuspended(const LepLaunchConfig& config, PCWSTR applicationName,
                                PWSTR commandLine, PCWSTR currentDirectory, DWORD creationFlags,
                                LPSTARTUPINFOW startup, LPPROCESS_INFORMATION processInfo,
                                PVOID environment, std::wstring& error) const {
    if (!create_) {
        error = L"LEP loader is not loaded";
        return (LONG)0xC0000135L; // STATUS_DLL_NOT_FOUND
    }
    if (config.timezoneId.empty()) {
        error = L"LEP time zone ID is empty";
        return (LONG)0xC000000DL; // STATUS_INVALID_PARAMETER
    }
    if (config.timezoneId.size() >= 128 || config.standardName.size() >= 32 ||
        config.daylightName.size() >= 32) {
        error = L"LEP time zone ID or name override is too long";
        return (LONG)0xC000000DL;
    }
    if (config.registryRedirectionMode > lep::kRegistryRedirectionModeMax ||
        config.uiLanguageMode > lep::kUiLanguageModeMax) {
        error = L"LEP mode value out of range";
        return (LONG)0xC000000DL;
    }

    lep::EnvironmentBlock block = buildBlock(config);
    // Force suspension even if a caller forgets: the supervisor must assign
    // the process to its Job Object before any injected code or child runs.
    DWORD flags = creationFlags | CREATE_SUSPENDED;
    PROCESS_INFORMATION info{};
    LONG status = create_(&block, applicationName, commandLine, currentDirectory, flags, startup,
                          &info, nullptr, nullptr, environment, nullptr);
    if (status < 0) {
        error = L"LepCreateProcess2 failed: " + ntStatusText(status);
        return status;
    }
    if (!info.hProcess || !info.hThread) {
        error = L"LepCreateProcess2 succeeded without process handles";
        if (info.hProcess) CloseHandle(info.hProcess);
        if (info.hThread) CloseHandle(info.hThread);
        return (LONG)0xC0000001L; // STATUS_UNSUCCESSFUL
    }
    *processInfo = info;
    return status;
}
