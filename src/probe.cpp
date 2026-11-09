#include "win.hpp"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <climits>
#include <string>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitMismatch = 2;
constexpr int kExitChildFailure = 3;
constexpr int kExitUsage = 4;

struct Expectations {
    std::wstring timezone;
    std::wstring locale;
    ULONG acp = 0;
    ULONG oemcp = 0;
    LANGID ui = 0;
    bool hasTimezone = false;
    bool hasLocale = false;
    bool hasAcp = false;
    bool hasOemcp = false;
    bool hasUi = false;
    bool expectLep = false;
    bool expectJob = false;
    bool expectPreferred = false;
};

struct Observations {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring arch;
    bool wow64 = false;
    bool inJob = false;
    bool lepCore = false;
    bool lepLog = false;
    std::wstring lepLogFile;

    std::wstring tzKey;
    DWORD tzState = 0;
    LONG tzBias = 0;
    std::wstring tzStdName;
    std::wstring tzDltName;
    DWORD tzStateDynamic = 0;
    LONG tzBiasDynamic = 0;
    std::wstring tzStdNameDynamic;
    std::wstring tzDltNameDynamic;

    int offsetCurrent = INT_MIN;
    int offsetJanuary = INT_MIN;
    int offsetJuly = INT_MIN;

    std::wstring locale;
    std::wstring systemLocale;
    LCID lcid = 0;
    LCID systemLcid = 0;
    LCID threadLcid = 0;
    LANGID uiLang = 0;
    LANGID systemUiLang = 0;
    LANGID userLangId = 0;
    LANGID systemLangId = 0;
    std::wstring preferredUser;
    std::wstring preferredSystem;
    std::wstring preferredThread;
    std::wstring preferredThreadMerged;
    std::wstring preferredWinrt;
    bool winrtContract = false;

    UINT acp = 0;
    UINT oemcp = 0;
    UINT acpEarly = 0;
    UINT oemcpEarly = 0;
    UINT consoleCp = 0;
    UINT consoleOutputCp = 0;

    std::wstring shortDate;
    std::wstring longDate;
    std::wstring shortTime;
    std::wstring timeFormat;
    std::wstring decimalSep;
    std::wstring thousandSep;
    std::wstring formattedDate;
    std::wstring formattedTime;

    std::wstring envTZ;
    std::wstring envLANG;
    std::wstring envLC_ALL;
    std::wstring envLANGUAGE;
};

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

std::string jsonEscape(const std::wstring& value) {
    std::string input = utf8(value);
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                sprintf(buffer, "\\u%04X", c);
                out += buffer;
            } else {
                out.push_back((char)c);
            }
        }
    }
    return out;
}

std::string jsonString(const std::wstring& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

std::string jsonInt(long long value) {
    return std::to_string(value);
}

std::string jsonOptionalOffset(int value) {
    return value == INT_MIN ? std::string("null") : std::to_string(value);
}

std::wstring localeInfo(LCTYPE type) {
    wchar_t buffer[256]{};
    int n = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, 256);
    return n > 0 ? std::wstring(buffer, n - 1) : std::wstring();
}

std::wstring preferredLanguages(const char* exportName, DWORD flags = 0x8) {
    using Fn = BOOL(WINAPI*)(DWORD, PULONG, PWSTR, PULONG);
    auto fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), exportName));
    if (!fn) return L"";
    ULONG count = 0, size = 0;
    if (!fn(flags, &count, nullptr, &size) || size == 0) return L"";
    std::wstring buffer(size, 0);
    if (!fn(flags, &count, &buffer[0], &size)) return L"";
    std::wstring result;
    size_t pos = 0;
    while (pos < buffer.size() && buffer[pos]) {
        std::wstring item(&buffer[pos]);
        if (!result.empty()) result += L";";
        result += item;
        pos += item.size() + 1;
    }
    return result;
}

// Match Electron's Windows path without launching Electron or Claude.
std::wstring winrtLanguages(bool& contract) {
    contract = false;
    HMODULE module = LoadLibraryW(L"combase.dll");
    if (!module) return L"";
    using Initialize = HRESULT(WINAPI*)(UINT);
    using Uninitialize = void(WINAPI*)();
    using CreateString = HRESULT(WINAPI*)(PCWSTR, UINT32, void**);
    using DeleteString = HRESULT(WINAPI*)(void*);
    using StringBuffer = PCWSTR(WINAPI*)(void*, UINT32*);
    using Factory = HRESULT(WINAPI*)(void*, const GUID&, void**);
    auto initialize = reinterpret_cast<Initialize>(GetProcAddress(module, "RoInitialize"));
    auto uninitialize = reinterpret_cast<Uninitialize>(GetProcAddress(module, "RoUninitialize"));
    auto create = reinterpret_cast<CreateString>(GetProcAddress(module, "WindowsCreateString"));
    auto destroy = reinterpret_cast<DeleteString>(GetProcAddress(module, "WindowsDeleteString"));
    auto raw = reinterpret_cast<StringBuffer>(GetProcAddress(module, "WindowsGetStringRawBuffer"));
    auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "RoGetActivationFactory"));
    if (!initialize || !uninitialize || !create || !destroy || !raw || !factory) {
        FreeLibrary(module);
        return L"";
    }
    HRESULT initialized = initialize(1); // RO_INIT_MULTITHREADED
    std::wstring result;
    void* classId = nullptr;
    void* preferences = nullptr;
    void* languages = nullptr;
    const wchar_t* name = L"Windows.System.UserProfile.GlobalizationPreferences";
    const GUID iid = {0x01bf4326, 0xed37, 0x4e96, {0xb0, 0xe9, 0xc1, 0x34, 0x0d, 0x1e, 0xa1, 0x58}};
    using Release = ULONG(WINAPI*)(void*);
    if (SUCCEEDED(create(name, (UINT32)wcslen(name), &classId)) &&
        SUCCEEDED(factory(classId, iid, &preferences))) {
        auto table = *static_cast<void***>(preferences);
        using GetLanguages = HRESULT(WINAPI*)(void*, void**);
        if (SUCCEEDED(reinterpret_cast<GetLanguages>(table[9])(preferences, &languages))) {
            auto view = *static_cast<void***>(languages);
            using Size = HRESULT(WINAPI*)(void*, UINT32*);
            using GetAt = HRESULT(WINAPI*)(void*, UINT32, void**);
            UINT32 size = 0;
            if (SUCCEEDED(reinterpret_cast<Size>(view[7])(languages, &size))) {
                for (UINT32 i = 0; i < size && i < 64; ++i) {
                    void* value = nullptr;
                    if (SUCCEEDED(reinterpret_cast<GetAt>(view[6])(languages, i, &value))) {
                        UINT32 length = 0;
                        PCWSTR data = raw(value, &length);
                        if (!result.empty()) result += L";";
                        result.append(data, length);
                        destroy(value);
                    }
                }
                // Exercise the immutable vector ABI, ownership and bounds as
                // well as the GetAt/Size path Electron uses.
                using IndexOf = HRESULT(WINAPI*)(void*, void*, UINT32*, unsigned char*);
                using GetMany = HRESULT(WINAPI*)(void*, UINT32, UINT32, void**, UINT32*);
                void* first = nullptr;
                void* batch = nullptr;
                void* outside = nullptr;
                UINT32 actual = 0, index = 99;
                unsigned char found = 0;
                contract = size > 0 && SUCCEEDED(reinterpret_cast<GetAt>(view[6])(languages, 0, &first));
                if (contract) {
                    contract = SUCCEEDED(reinterpret_cast<IndexOf>(view[8])(languages, first, &index, &found)) &&
                               found && index == 0 &&
                               SUCCEEDED(reinterpret_cast<GetMany>(view[9])(languages, 0, 1, &batch, &actual)) &&
                               actual == 1;
                    if (contract) {
                        UINT32 firstLength = 0, batchLength = 0;
                        PCWSTR a = raw(first, &firstLength), b = raw(batch, &batchLength);
                        contract = firstLength == batchLength &&
                                   wmemcmp(a, b, firstLength) == 0;
                    }
                }
                contract = contract && reinterpret_cast<GetAt>(view[6])(languages, size, &outside) ==
                                           (HRESULT)0x8000000B &&
                           SUCCEEDED(reinterpret_cast<GetMany>(view[9])(languages, size, 1, &outside, &actual)) &&
                           actual == 0;
                if (first) destroy(first);
                if (batch) destroy(batch);
                if (outside) destroy(outside);
            }
            reinterpret_cast<Release>(view[2])(languages);
        }
        reinterpret_cast<Release>(table[2])(preferences);
    }
    if (classId) destroy(classId);
    if (SUCCEEDED(initialized)) uninitialize();
    FreeLibrary(module);
    return result;
}

int offsetMinutes(const DYNAMIC_TIME_ZONE_INFORMATION& zone, const SYSTEMTIME& utc) {
    SYSTEMTIME local{};
    if (!SystemTimeToTzSpecificLocalTimeEx(&zone, &utc, &local)) return INT_MIN;
    FILETIME utcFile{}, localFile{};
    if (!SystemTimeToFileTime(&utc, &utcFile) || !SystemTimeToFileTime(&local, &localFile))
        return INT_MIN;
    ULARGE_INTEGER u, l;
    u.LowPart = utcFile.dwLowDateTime;
    u.HighPart = utcFile.dwHighDateTime;
    l.LowPart = localFile.dwLowDateTime;
    l.HighPart = localFile.dwHighDateTime;
    LONGLONG diff = (LONGLONG)l.QuadPart - (LONGLONG)u.QuadPart;
    return (int)(diff / 600000000LL);
}

int offsetForMonth(const DYNAMIC_TIME_ZONE_INFORMATION& zone, WORD month) {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    utc.wMonth = month;
    utc.wDay = 15;
    utc.wHour = 12;
    utc.wMinute = 0;
    utc.wSecond = 0;
    utc.wMilliseconds = 0;
    return offsetMinutes(zone, utc);
}

std::wstring currentArchitecture(bool& wow64) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (fn) {
        USHORT processMachine = 0, nativeMachine = 0;
        if (fn(GetCurrentProcess(), &processMachine, &nativeMachine)) {
            wow64 = processMachine != 0;
            if (processMachine == IMAGE_FILE_MACHINE_I386) return L"x86";
            if (processMachine == IMAGE_FILE_MACHINE_ARM64) return L"arm64";
            return L"x64";
        }
    }
    BOOL isWow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &isWow64);
    wow64 = isWow64 != FALSE;
#if defined(_M_X64) || defined(__x86_64__)
    return wow64 ? L"x86" : L"x64";
#else
    return L"x86";
#endif
}

DWORD parentProcessId() {    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.h) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) return 0;
    do {
        if (entry.th32ProcessID == GetCurrentProcessId()) return entry.th32ParentProcessID;
    } while (Process32NextW(snapshot, &entry));
    return 0;
}

bool readWideTextFile(const std::wstring& path, std::wstring& out) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) return false;
    std::vector<unsigned char> bytes((size_t)size.QuadPart);
    DWORD read = 0;
    if (!bytes.empty() && !ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr))
        return false;
    bytes.resize(read);
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        out.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        size_t chars = (bytes.size() - 2) / 2;
        out.resize(chars);
        for (size_t i = 0; i < chars; ++i)
            out[i] = (wchar_t)((bytes[2 + i * 2] << 8) | bytes[3 + i * 2]);
        return true;
    }
    int length = (int)bytes.size();
    int wide = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), length,
                                   nullptr, 0);
    if (wide <= 0) return false;
    out.resize(wide);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), length, &out[0],
                        wide);
    return true;
}

bool findLepLog(DWORD pid, std::wstring& fileName) {
    std::wstring dir = env(L"CP_LEP_LOG_DIR");
    if (dir.empty() || !dirExists(dir)) return false;
    wchar_t hex[24];
    swprintf(hex, 24, L".%lX.log.txt", (unsigned long)pid);
    std::wstring suffix = lower(hex);
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(joinPath(dir, L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = entry.cFileName;
        std::wstring lowerName = lower(name);
        if (lowerName.size() < suffix.size() ||
            lowerName.compare(lowerName.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        std::wstring text;
        if (!readWideTextFile(joinPath(dir, name), text)) continue;
        if (text.find(L"GlobalData.Initialize end status=00000000") != std::wstring::npos) {
            fileName = name;
            found = true;
            break;
        }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    return found;
}

Observations observe() {
    Observations o;
    o.acpEarly = GetACP();
    o.oemcpEarly = GetOEMCP();
    o.pid = GetCurrentProcessId();
    o.parentPid = parentProcessId();
    o.arch = currentArchitecture(o.wow64);
    BOOL inJob = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &inJob)) o.inJob = inJob != FALSE;
    o.lepCore = GetModuleHandleW(L"LocaleEmulatorPlus_x64.dll") != nullptr ||
                GetModuleHandleW(L"LocaleEmulatorPlus_x86.dll") != nullptr;

    TIME_ZONE_INFORMATION tz{};
    o.tzState = GetTimeZoneInformation(&tz);
    o.tzBias = tz.Bias;
    o.tzStdName = tz.StandardName;
    o.tzDltName = tz.DaylightName;

    DYNAMIC_TIME_ZONE_INFORMATION dynamic{};
    o.tzStateDynamic = GetDynamicTimeZoneInformation(&dynamic);
    o.tzBiasDynamic = dynamic.Bias;
    o.tzStdNameDynamic = dynamic.StandardName;
    o.tzDltNameDynamic = dynamic.DaylightName;
    o.tzKey = dynamic.TimeZoneKeyName;

    SYSTEMTIME utc{}, local{};
    GetSystemTime(&utc);
    GetLocalTime(&local);
    o.offsetCurrent = offsetMinutes(dynamic, utc);
    o.offsetJanuary = offsetForMonth(dynamic, 1);
    o.offsetJuly = offsetForMonth(dynamic, 7);

    wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH)) o.locale = localeName;
    wchar_t systemLocale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetSystemDefaultLocaleName(systemLocale, LOCALE_NAME_MAX_LENGTH))
        o.systemLocale = systemLocale;
    o.lcid = GetUserDefaultLCID();
    o.systemLcid = GetSystemDefaultLCID();
    o.threadLcid = GetThreadLocale();
    o.uiLang = GetUserDefaultUILanguage();
    o.systemUiLang = GetSystemDefaultUILanguage();
    o.userLangId = GetUserDefaultLangID();
    o.systemLangId = GetSystemDefaultLangID();
    o.preferredUser = preferredLanguages("GetUserPreferredUILanguages");
    o.preferredSystem = preferredLanguages("GetSystemPreferredUILanguages");
    o.preferredThread = preferredLanguages("GetThreadPreferredUILanguages");
    // Chromium/Electron merges the user and system fallback lists (0x10/0x20).
    o.preferredThreadMerged = preferredLanguages("GetThreadPreferredUILanguages", 0x8 | 0x10 | 0x20);
    o.preferredWinrt = winrtLanguages(o.winrtContract);

    o.acp = GetACP();
    o.oemcp = GetOEMCP();
    o.consoleCp = GetConsoleCP();
    o.consoleOutputCp = GetConsoleOutputCP();

    o.shortDate = localeInfo(LOCALE_SSHORTDATE);
    o.longDate = localeInfo(LOCALE_SLONGDATE);
    o.shortTime = localeInfo(LOCALE_STIMEFORMAT);
    o.timeFormat = localeInfo(LOCALE_STIMEFORMAT);
    o.decimalSep = localeInfo(LOCALE_SDECIMAL);
    o.thousandSep = localeInfo(LOCALE_STHOUSAND);
    wchar_t buffer[256]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, buffer, 256,
                        nullptr))
        o.formattedDate = buffer;
    buffer[0] = 0;
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, buffer, 256))
        o.formattedTime = buffer;

    o.envTZ = env(L"TZ");
    o.envLANG = env(L"LANG");
    o.envLC_ALL = env(L"LC_ALL");
    o.envLANGUAGE = env(L"LANGUAGE");

    // Do file I/O last: it can trigger lazy kernelbase NLS initialization,
    // which must not run before the ACP/OEMCP/locale observations above.
    o.lepLog = findLepLog(o.pid, o.lepLogFile);
    if (o.lepLog) o.lepCore = true;
    return o;
}

void check(const Observations& o, const Expectations& e, std::vector<std::wstring>& mismatches) {
    if (e.hasTimezone && _wcsicmp(o.tzKey.c_str(), e.timezone.c_str()) != 0)
        mismatches.push_back(L"timezone: expected " + e.timezone + L", observed " + o.tzKey);
    if (e.hasLocale && _wcsicmp(o.locale.c_str(), e.locale.c_str()) != 0)
        mismatches.push_back(L"locale: expected " + e.locale + L", observed " + o.locale);
    if (e.hasAcp && o.acp != e.acp)
        mismatches.push_back(L"ACP: expected " + std::to_wstring(e.acp) + L", observed " +
                             std::to_wstring(o.acp));
    if (e.hasOemcp && o.oemcp != e.oemcp)
        mismatches.push_back(L"OEMCP: expected " + std::to_wstring(e.oemcp) + L", observed " +
                             std::to_wstring(o.oemcp));
    if (e.hasUi && o.uiLang != e.ui)
        mismatches.push_back(L"UI language: expected " + std::to_wstring(e.ui) + L", observed " +
                             std::to_wstring(o.uiLang));
    if (e.expectPreferred && e.hasUi) {
        if (!o.winrtContract) mismatches.push_back(L"WinRT language vector contract failed");
        wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
        LCIDToLocaleName(e.ui, name, LOCALE_NAME_MAX_LENGTH, 0);
        for (const auto& item : std::vector<std::pair<const wchar_t*, std::wstring>>{
                 {L"preferred user", o.preferredUser}, {L"preferred system", o.preferredSystem},
                 {L"preferred thread", o.preferredThread},
                 {L"preferred thread merged", o.preferredThreadMerged},
                 {L"preferred WinRT", o.preferredWinrt}}) {
            if (!name[0] || _wcsicmp(item.second.c_str(), name) != 0)
                mismatches.push_back(std::wstring(item.first) + L": expected " + name +
                                     L", observed " + item.second);
        }
    }
    if (e.expectLep && !o.lepCore)
        mismatches.push_back(L"LEP core DLL is not loaded");
    if (e.expectJob && !o.inJob)
        mismatches.push_back(L"process is not in the supervisor Job Object");
}

std::string reportJson(const Observations& o, const Expectations& e,
                       const std::vector<std::wstring>& mismatches, bool isChild,
                       const std::wstring& childJson, int childExit, bool childOk) {
    std::string json = "{";
    json += "\"role\":" + jsonString(isChild ? L"child" : L"parent");
    json += ",\"pid\":" + jsonInt(o.pid);
    json += ",\"ppid\":" + jsonInt(o.parentPid);
    json += ",\"arch\":" + jsonString(o.arch);
    json += ",\"wow64\":" + jsonBool(o.wow64);
    json += ",\"inJob\":" + jsonBool(o.inJob);
    json += ",\"lepCore\":" + jsonBool(o.lepCore);
    json += ",\"lepLog\":" + jsonBool(o.lepLog);
    json += ",\"lepLogFile\":" + jsonString(o.lepLogFile);
    json += ",\"tzKey\":" + jsonString(o.tzKey);
    json += ",\"tzState\":" + jsonInt(o.tzState);
    json += ",\"tzBias\":" + jsonInt(o.tzBias);
    json += ",\"tzStdName\":" + jsonString(o.tzStdName);
    json += ",\"tzDltName\":" + jsonString(o.tzDltName);
    json += ",\"tzStateDynamic\":" + jsonInt(o.tzStateDynamic);
    json += ",\"tzBiasDynamic\":" + jsonInt(o.tzBiasDynamic);
    json += ",\"tzStdNameDynamic\":" + jsonString(o.tzStdNameDynamic);
    json += ",\"tzDltNameDynamic\":" + jsonString(o.tzDltNameDynamic);
    json += ",\"offsetMinutes\":" + jsonOptionalOffset(o.offsetCurrent);
    json += ",\"offsetJanuary\":" + jsonOptionalOffset(o.offsetJanuary);
    json += ",\"offsetJuly\":" + jsonOptionalOffset(o.offsetJuly);
    json += ",\"locale\":" + jsonString(o.locale);
    json += ",\"systemLocale\":" + jsonString(o.systemLocale);
    json += ",\"lcid\":" + jsonInt(o.lcid);
    json += ",\"systemLcid\":" + jsonInt(o.systemLcid);
    json += ",\"threadLcid\":" + jsonInt(o.threadLcid);
    json += ",\"uiLang\":" + jsonInt(o.uiLang);
    json += ",\"systemUiLang\":" + jsonInt(o.systemUiLang);
    json += ",\"userLangId\":" + jsonInt(o.userLangId);
    json += ",\"systemLangId\":" + jsonInt(o.systemLangId);
    json += ",\"preferredUser\":" + jsonString(o.preferredUser);
    json += ",\"preferredSystem\":" + jsonString(o.preferredSystem);
    json += ",\"preferredThread\":" + jsonString(o.preferredThread);
    json += ",\"preferredThreadMerged\":" + jsonString(o.preferredThreadMerged);
    json += ",\"preferredWinrt\":" + jsonString(o.preferredWinrt);
    json += ",\"winrtContract\":" + jsonBool(o.winrtContract);
    json += ",\"acp\":" + jsonInt(o.acp);
    json += ",\"oemcp\":" + jsonInt(o.oemcp);
    json += ",\"acpEarly\":" + jsonInt(o.acpEarly);
    json += ",\"oemcpEarly\":" + jsonInt(o.oemcpEarly);
    json += ",\"consoleCp\":" + jsonInt(o.consoleCp);
    json += ",\"consoleOutputCp\":" + jsonInt(o.consoleOutputCp);
    json += ",\"shortDate\":" + jsonString(o.shortDate);
    json += ",\"longDate\":" + jsonString(o.longDate);
    json += ",\"shortTime\":" + jsonString(o.shortTime);
    json += ",\"decimalSep\":" + jsonString(o.decimalSep);
    json += ",\"thousandSep\":" + jsonString(o.thousandSep);
    json += ",\"formattedDate\":" + jsonString(o.formattedDate);
    json += ",\"formattedTime\":" + jsonString(o.formattedTime);
    json += ",\"envTZ\":" + jsonString(o.envTZ);
    json += ",\"envLANG\":" + jsonString(o.envLANG);
    json += ",\"envLC_ALL\":" + jsonString(o.envLC_ALL);
    json += ",\"envLANGUAGE\":" + jsonString(o.envLANGUAGE);
    json += ",\"expected\":{";
    bool first = true;
    auto addExpected = [&](const char* name, const std::string& value) {
        if (!first) json += ",";
        first = false;
        json += std::string("\"") + name + "\":" + value;
    };
    if (e.hasTimezone) addExpected("timezone", jsonString(e.timezone));
    if (e.hasLocale) addExpected("locale", jsonString(e.locale));
    if (e.hasAcp) addExpected("acp", jsonInt(e.acp));
    if (e.hasOemcp) addExpected("oemcp", jsonInt(e.oemcp));
    if (e.hasUi) addExpected("ui", jsonInt(e.ui));
    if (e.expectLep) addExpected("lep", "true");
    if (e.expectJob) addExpected("job", "true");
    json += "},\"mismatches\":[";
    for (size_t i = 0; i < mismatches.size(); ++i) {
        if (i) json += ",";
        json += jsonString(mismatches[i]);
    }
    json += "],\"ok\":" + jsonBool(mismatches.empty());
    if (!isChild) {
        json += ",\"childExit\":" + jsonInt(childExit);
        json += ",\"childOk\":" + jsonBool(childOk);
        json += ",\"childReport\":" + jsonString(childJson);
    }
    json += "}";
    return json;
}

void appendFile(const std::wstring& path, const std::wstring& text) {
    if (path.empty()) return;
    Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) return;
    auto data = utf8(text);
    DWORD written = 0;
    WriteFile(file, data.data(), (DWORD)data.size(), &written, nullptr);
}

void writeResult(const std::wstring& path, const std::wstring& text) {
    if (path.empty()) return;
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) return;
    auto data = utf8(text);
    DWORD written = 0;
    WriteFile(file, data.data(), (DWORD)data.size(), &written, nullptr);
}

void emit(const std::wstring& json) {
    std::wstring line = json + L"\r\n";
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    DWORD written = 0;
    if (out && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        WriteConsoleW(out, line.c_str(), (DWORD)line.size(), &written, nullptr);
    } else {
        auto data = utf8(line);
        WriteFile(out, data.data(), (DWORD)data.size(), &written, nullptr);
    }
    std::wstring logPath = env(L"CP_PROBE_LOG");
    if (logPath.empty()) logPath = joinPath(directory(selfPath()), L"probe.log");
    appendFile(logPath, line);
}

bool parseNumber(const wchar_t* text, unsigned long& value) {
    wchar_t* end = nullptr;
    value = wcstoul(text, &end, 0);
    return end && *end == 0;
}

bool runChild(const Expectations& expectations, const std::wstring& childResult,
              const std::wstring& probeLog, DWORD childSeconds, std::wstring& childJson,
              int& exitCode) {
    std::wstring command = quoteArgument(selfPath()) + L" --child --json";
    if (childSeconds) command += L" --seconds " + std::to_wstring(childSeconds);
    if (expectations.hasTimezone)
        command += L" --expect-timezone " + quoteArgument(expectations.timezone);
    if (expectations.hasLocale)
        command += L" --expect-locale " + quoteArgument(expectations.locale);
    if (expectations.hasAcp) command += L" --expect-acp " + std::to_wstring(expectations.acp);
    if (expectations.hasOemcp) command += L" --expect-oemcp " + std::to_wstring(expectations.oemcp);
    if (expectations.hasUi) command += L" --expect-ui " + std::to_wstring(expectations.ui);
    if (expectations.expectLep) command += L" --expect-lep";
    if (expectations.expectJob) command += L" --expect-job";
    if (expectations.expectPreferred) command += L" --expect-preferred";

    std::vector<std::wstring> variables;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (LPWCH p = block; *p; p += wcslen(p) + 1) {
            std::wstring entry = p;
            if (entry.empty() || entry[0] == L'=') continue;
            auto equals = entry.find(L'=');
            auto key = lower(entry.substr(0, equals));
            if (key == L"cp_probe_result" || key == L"cp_probe_log") continue;
            variables.push_back(entry);
        }
        FreeEnvironmentStringsW(block);
    }
    variables.push_back(L"CP_PROBE_RESULT=" + childResult);
    variables.push_back(L"CP_PROBE_LOG=" + probeLog);
    std::sort(variables.begin(), variables.end(),
              [](const std::wstring& a, const std::wstring& b) {
                  return _wcsicmp(a.c_str(), b.c_str()) < 0;
              });
    std::vector<wchar_t> environment;
    for (const auto& variable : variables) {
        environment.insert(environment.end(), variable.begin(), variable.end());
        environment.push_back(0);
    }
    environment.push_back(0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, environment.data(),
                        directory(selfPath()).c_str(), &startup, &info)) {
        childJson = L"CreateProcessW failed: " + winErrorText(GetLastError());
        exitCode = -1;
        return false;
    }
    Handle process(info.hProcess);
    Handle thread(info.hThread);
    DWORD wait = WaitForSingleObject(process, 20000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 2000);
        childJson = L"child timed out";
        exitCode = -1;
        return false;
    }
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    exitCode = (int)code;

    Handle file(CreateFileW(childResult.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.h) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) && size.QuadPart < 1024 * 1024) {
            std::vector<char> bytes((size_t)size.QuadPart);
            DWORD read = 0;
            if (!bytes.empty() &&
                ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr)) {
                int wide = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, nullptr, 0);
                if (wide > 0) {
                    childJson.resize(wide);
                    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, &childJson[0], wide);
                }
            }
        }
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Expectations expectations;
    bool spawnChild = false;
    bool isChild = false;
    DWORD seconds = 0;
    DWORD childSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        std::wstring argument = argv[i];
        auto next = [&](const wchar_t* name) -> const wchar_t* {
            if (i + 1 >= argc) {
                std::wstring message = std::wstring(L"Missing value for ") + name + L"\r\n";
                auto data = utf8(message);
                DWORD written = 0;
                WriteFile(GetStdHandle(STD_ERROR_HANDLE), data.data(), (DWORD)data.size(),
                          &written, nullptr);
                return nullptr;
            }
            return argv[++i];
        };
        if (argument == L"--child") {
            isChild = true;
        } else if (argument == L"--spawn-child") {
            spawnChild = true;
        } else if (argument == L"--json") {
            // JSON is the only output format.
        } else if (argument == L"--expect-lep") {
            expectations.expectLep = true;
        } else if (argument == L"--expect-job") {
            expectations.expectJob = true;
        } else if (argument == L"--expect-preferred") {
            expectations.expectPreferred = true;
        } else if (argument == L"--expect-timezone") {
            auto value = next(L"--expect-timezone");
            if (!value) return kExitUsage;
            expectations.timezone = value;
            expectations.hasTimezone = true;
        } else if (argument == L"--expect-locale") {
            auto value = next(L"--expect-locale");
            if (!value) return kExitUsage;
            expectations.locale = value;
            expectations.hasLocale = true;
        } else if (argument == L"--expect-acp" || argument == L"--expect-oemcp" ||
                   argument == L"--expect-ui") {
            auto value = next(argument.c_str());
            if (!value) return kExitUsage;
            unsigned long number = 0;
            if (!parseNumber(value, number)) return kExitUsage;
            if (argument == L"--expect-acp") {
                expectations.acp = (ULONG)number;
                expectations.hasAcp = true;
            } else if (argument == L"--expect-oemcp") {
                expectations.oemcp = (ULONG)number;
                expectations.hasOemcp = true;
            } else {
                expectations.ui = (LANGID)number;
                expectations.hasUi = true;
            }
        } else if (argument == L"--seconds") {
            auto value = next(L"--seconds");
            if (!value) return kExitUsage;
            unsigned long number = 0;
            if (!parseNumber(value, number) || number > 86400) return kExitUsage;
            seconds = (DWORD)number;
        } else if (argument == L"--child-seconds") {
            auto value = next(L"--child-seconds");
            if (!value) return kExitUsage;
            unsigned long number = 0;
            if (!parseNumber(value, number) || number > 86400) return kExitUsage;
            childSeconds = (DWORD)number;
        } else if (argument == L"--help" || argument == L"-h") {
            emit(L"EnvironmentProbe: native time zone, locale, code page and LEP/job observations.\r\n"
                 L"Options: --spawn-child --child --json --expect-timezone ID --expect-locale NAME\r\n"
                 L"         --expect-acp N --expect-oemcp N --expect-ui LANGID --expect-lep\r\n"
                 L"         --expect-job --expect-preferred --seconds N --child-seconds N\r\n");
            return kExitOk;
        } else {
            std::wstring message = L"Unknown argument: " + argument + L"\r\n";
            auto data = utf8(message);
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), data.data(), (DWORD)data.size(), &written,
                      nullptr);
            return kExitUsage;
        }
    }

    Observations observations = observe();
    std::vector<std::wstring> mismatches;
    check(observations, expectations, mismatches);

    bool childOk = true;
    std::wstring childJson;
    int childExit = 0;
    std::wstring probeLog = env(L"CP_PROBE_LOG");
    if (probeLog.empty()) probeLog = joinPath(directory(selfPath()), L"probe.log");
    if (spawnChild && !isChild) {
        std::wstring childResult = probeLog + L".child.json";
        DeleteFileW(childResult.c_str());
        if (!runChild(expectations, childResult, probeLog, childSeconds, childJson, childExit)) {
            childOk = false;
        } else {
            childOk = childExit == 0 && childJson.find(L"\"ok\":true") != std::wstring::npos;
            if (expectations.expectLep &&
                childJson.find(L"\"lepCore\":true") == std::wstring::npos)
                childOk = false;
            if (expectations.expectJob && childJson.find(L"\"inJob\":true") == std::wstring::npos)
                childOk = false;
        }
    }

    std::string json = reportJson(observations, expectations, mismatches, isChild, childJson,
                                  childExit, childOk);
    std::wstring wideJson = widen(json);
    emit(wideJson);
    std::wstring resultPath = env(L"CP_PROBE_RESULT");
    if (!resultPath.empty()) writeResult(resultPath, wideJson);

    for (const auto& mismatch : mismatches)
        emit(L"mismatch: " + mismatch);
    if (!mismatches.empty()) return kExitMismatch;
    if (!childOk) return kExitChildFailure;
    if (seconds) Sleep(seconds * 1000);
    return kExitOk;
}
