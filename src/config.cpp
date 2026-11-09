#include "config.hpp"
#include "win.hpp"
#include <map>
#include <cwchar>
#include <cwctype>
#include <cstdlib>

#ifndef LOCALE_IDEFAULTANSICODEPAGE
#define LOCALE_IDEFAULTANSICODEPAGE 0x00001004
#endif
#ifndef LOCALE_IDEFAULTOEMCODEPAGE
#define LOCALE_IDEFAULTOEMCODEPAGE 0x00001005
#endif

namespace {

std::wstring trim(const std::wstring& value) {
    size_t begin = 0, end = value.size();
    while (begin < end && iswspace(value[begin])) ++begin;
    while (end > begin && iswspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

std::wstring stripQuotes(const std::wstring& value) {
    if (value.size() >= 2 &&
        ((value.front() == L'"' && value.back() == L'"') ||
         (value.front() == L'\'' && value.back() == L'\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

bool readTextFile(const std::wstring& path, std::wstring& out, std::wstring& error) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) {
        error = L"Cannot open " + path + L": " + winErrorText(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) {
        error = L"Configuration file is too large: " + path;
        return false;
    }
    std::vector<unsigned char> bytes((size_t)size.QuadPart);
    DWORD read = 0;
    if (!bytes.empty() && !ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr)) {
        error = L"Cannot read " + path + L": " + winErrorText(GetLastError());
        return false;
    }
    bytes.resize(read);
    if (bytes.empty()) { out.clear(); return true; }

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        size_t chars = (bytes.size() - 2) / 2;
        out.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), chars);
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        size_t chars = (bytes.size() - 2) / 2;
        out.resize(chars);
        for (size_t i = 0; i < chars; ++i) {
            out[i] = (wchar_t)((bytes[2 + i * 2] << 8) | bytes[3 + i * 2]);
        }
        return true;
    }
    size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) offset = 3;

    // A UTF-16LE file without BOM commonly has NUL bytes in the first line.
    bool likelyUtf16 = false;
    for (size_t i = offset; i + 1 < bytes.size() && i < offset + 64; i += 2) {
        if (bytes[i] != 0 && bytes[i + 1] == 0) {
            likelyUtf16 = true;
            break;
        }
    }
    if (likelyUtf16) {
        size_t chars = (bytes.size() - offset) / 2;
        out.assign(reinterpret_cast<const wchar_t*>(bytes.data() + offset), chars);
        return true;
    }

    const char* data = reinterpret_cast<const char*>(bytes.data() + offset);
    int length = (int)(bytes.size() - offset);
    int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, length, nullptr, 0);
    UINT codePage = CP_UTF8;
    if (wide <= 0) {
        codePage = CP_ACP;
        wide = MultiByteToWideChar(CP_ACP, 0, data, length, nullptr, 0);
    }
    if (wide <= 0) {
        error = L"Cannot decode " + path;
        return false;
    }
    out.resize(wide);
    MultiByteToWideChar(codePage, 0, data, length, &out[0], wide);
    return true;
}

bool writeTextFile(const std::wstring& path, const std::wstring& text, std::wstring& error) {
    auto data = utf8(text);
    std::string bytes;
    bytes.push_back((char)0xEF);
    bytes.push_back((char)0xBB);
    bytes.push_back((char)0xBF);
    bytes.append(data);
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.h) {
        error = L"Cannot write " + path + L": " + winErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) ||
        written != bytes.size()) {
        error = L"Cannot write " + path + L": " + winErrorText(GetLastError());
        return false;
    }
    return true;
}

std::map<std::wstring, std::wstring> parseIni(const std::wstring& text) {
    std::map<std::wstring, std::wstring> values;
    std::wstring section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        auto equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        if (!section.empty() && section != L"claudeprotect") continue;
        auto key = lower(trim(line.substr(0, equals)));
        auto value = stripQuotes(trim(line.substr(equals + 1)));
        values[key] = value;
    }
    return values;
}

std::wstring iniValue(const std::map<std::wstring, std::wstring>& values, const wchar_t* key,
                      const wchar_t* fallback = L"") {
    auto it = values.find(lower(key));
    return it == values.end() ? std::wstring(fallback) : it->second;
}

bool parseUlong(const std::wstring& text, const wchar_t* name, unsigned long min,
                unsigned long max, ULONG& value, std::wstring& error) {
    if (text.empty()) return true;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (!end || *end != 0 || parsed < min || parsed > max) {
        error = std::wstring(L"Invalid value for ") + name + L": " + text;
        return false;
    }
    value = (ULONG)parsed;
    return true;
}

std::wstring localeInfo(const std::wstring& locale, LCTYPE type) {
    wchar_t buffer[128]{};
    int n = GetLocaleInfoEx(locale.c_str(), type, buffer, 128);
    return n > 0 ? std::wstring(buffer, n - 1) : std::wstring();
}

ULONG parseLocaleNumber(const std::wstring& text, ULONG fallback) {
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    unsigned long value = wcstoul(text.c_str(), &end, 10);
    return end && *end == 0 && value != 0 && value <= 0xFFFF ? (ULONG)value : fallback;
}

ULONG charsetForCodePage(ULONG codePage) {
    switch (codePage) {
    case 932: return SHIFTJIS_CHARSET;
    case 936: return GB2312_CHARSET;
    case 949: return HANGUL_CHARSET;
    case 950: return CHINESEBIG5_CHARSET;
    case 874: return THAI_CHARSET;
    case 1250: return EASTEUROPE_CHARSET;
    case 1251: return RUSSIAN_CHARSET;
    case 1252: return ANSI_CHARSET;
    case 1253: return GREEK_CHARSET;
    case 1254: return TURKISH_CHARSET;
    case 1255: return HEBREW_CHARSET;
    case 1256: return ARABIC_CHARSET;
    case 1257: return BALTIC_CHARSET;
    case 1258: return VIETNAMESE_CHARSET;
    default: return DEFAULT_CHARSET;
    }
}

bool timeZoneExists(const std::wstring& id) {
    if (id.empty() || id.find_first_of(L"\\/") != std::wstring::npos) return false;
    HKEY key = nullptr;
    std::wstring path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\" + id;
    LSTATUS status =
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (key) RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::wstring posixOffset(LONG bias) {
    wchar_t buffer[32];
    LONG minutes = std::abs(bias);
    swprintf(buffer, 32, L"%ls%ld", bias < 0 ? L"-" : L"", minutes / 60);
    std::wstring result = buffer;
    if (minutes % 60) {
        swprintf(buffer, 32, L":%02ld", minutes % 60);
        result += buffer;
    }
    return result;
}

std::wstring posixZoneName(const std::wstring& id, LONG bias, bool daylight) {
    static const struct {
        const wchar_t* windows;
        const wchar_t* standard;
        const wchar_t* daylight;
    } names[] = {
        {L"UTC", L"UTC", L"UTC"},
        {L"Pacific Standard Time", L"PST", L"PDT"},
        {L"Mountain Standard Time", L"MST", L"MDT"},
        {L"Central Standard Time", L"CST", L"CDT"},
        {L"Eastern Standard Time", L"EST", L"EDT"},
        {L"China Standard Time", L"CST", L"CST"},
        {L"Tokyo Standard Time", L"JST", L"JST"},
    };
    for (const auto& name : names) {
        if (_wcsicmp(name.windows, id.c_str()) == 0)
            return daylight ? name.daylight : name.standard;
    }
    // Numeric names avoid relying on localized Windows display names.
    wchar_t buffer[32];
    LONG minutes = std::abs(bias);
    swprintf(buffer, 32, L"<%lc%02ld%02ld>", bias > 0 ? L'-' : L'+',
             minutes / 60, minutes % 60);
    return buffer;
}

std::wstring posixTransition(const SYSTEMTIME& date) {
    if (date.wYear != 0) return {};
    std::wstring result = L"M" + std::to_wstring(date.wMonth) + L"." +
                          std::to_wstring(date.wDay) + L"." +
                          std::to_wstring(date.wDayOfWeek);
    if (date.wHour != 2 || date.wMinute != 0 || date.wSecond != 0) {
        wchar_t buffer[32];
        swprintf(buffer, 32, L"/%u:%02u:%02u", date.wHour, date.wMinute, date.wSecond);
        result += buffer;
    }
    return result;
}

bool isAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) return true;
    return false;
}

std::vector<int> versionParts(const std::wstring& path) {
    std::wstring p = lower(path);
    size_t start = p.find(L"app-");
    if (start != std::wstring::npos)
        start += 4;
    else {
        start = p.find(L"claude_");
        if (start != std::wstring::npos) start += 7;
        else start = 0;
    }
    std::vector<int> parts;
    size_t i = start;
    while (i < p.size() && parts.size() < 4) {
        if (iswdigit(p[i])) {
            int value = 0;
            while (i < p.size() && iswdigit(p[i])) {
                value = value * 10 + (p[i] - L'0');
                ++i;
            }
            parts.push_back(value);
            if (i < p.size() && p[i] == L'.') ++i;
        } else if (p[i] == L'.') {
            ++i;
        } else {
            break;
        }
    }
    if (parts.empty()) parts.push_back(0);
    return parts;
}

int compareVersions(const std::wstring& a, const std::wstring& b) {
    auto av = versionParts(a), bv = versionParts(b);
    size_t count = (std::max)(av.size(), bv.size());
    for (size_t i = 0; i < count; ++i) {
        int x = i < av.size() ? av[i] : 0;
        int y = i < bv.size() ? bv[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

void addCandidate(std::vector<std::wstring>& candidates, const std::wstring& path) {
    if (!isAbsolutePath(path)) return;
    if (!fileExists(path)) return;
    std::wstring full(32768, 0);
    DWORD n = GetFullPathNameW(path.c_str(), 32768, &full[0], nullptr);
    if (!n || n >= 32768) return;
    full.resize(n);
    for (const auto& existing : candidates) {
        if (_wcsicmp(existing.c_str(), full.c_str()) == 0) return;
    }
    candidates.push_back(full);
}

void scanAppDirectories(const std::wstring& root, std::vector<std::wstring>& candidates) {
    if (root.empty() || !isAbsolutePath(root)) return;
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(joinPath(root, L"app-*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            addCandidate(candidates, joinPath(joinPath(root, entry.cFileName), L"claude.exe"));
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
}

} // namespace

static bool parseConfigText(const std::wstring& baseDir, const std::wstring& text,
                            Config& config, std::wstring& error) {
    config = Config();
    config.baseDir = baseDir;
    config.iniPath = joinPath(baseDir, L"ClaudeProtect.ini");
    config.logPath = joinPath(baseDir, L"ClaudeProtect.log");
    config.probePath = joinPath(baseDir, L"EnvironmentProbe.exe");

    auto values = parseIni(text);

    config.executable = iniValue(values, L"Executable");
    config.timezoneId = iniValue(values, L"Timezone", L"Pacific Standard Time");
    config.timezoneIana = iniValue(values, L"TimezoneIana");
    config.locale = iniValue(values, L"Locale", L"en-US");
    config.standardName = iniValue(values, L"StandardName");
    config.daylightName = iniValue(values, L"DaylightName");
    config.extraArguments = iniValue(values, L"ExtraArguments");
    config.lepDir = iniValue(values, L"LepDirectory");

    if (!parseUlong(iniValue(values, L"AnsiCodePage"), L"AnsiCodePage", 0, 65535, config.ansiCodePage, error))
        return false;
    if (!parseUlong(iniValue(values, L"OemCodePage"), L"OemCodePage", 0, 65535, config.oemCodePage, error))
        return false;
    if (!parseUlong(iniValue(values, L"FontCharset"), L"FontCharset", 0, 255, config.fontCharset, error))
        return false;
    if (!parseUlong(iniValue(values, L"RegistryRedirectionMode"), L"RegistryRedirectionMode", 0, 2,
                    config.registryRedirectionMode, error))
        return false;
    if (!parseUlong(iniValue(values, L"HookUILanguageMode"), L"HookUILanguageMode", 0, 2,
                    config.uiLanguageMode, error))
        return false;

    std::wstring verify = lower(iniValue(values, L"VerifyInjection", L"strict"));
    if (verify == L"1") verify = L"strict";
    if (verify == L"0") verify = L"off";
    if (verify != L"strict" && verify != L"warn" && verify != L"off") {
        error = L"VerifyInjection must be strict, warn, or off";
        return false;
    }
    config.verifyMode = verify;

    unsigned long grace = 8;
    if (!parseUlong(iniValue(values, L"VerifyGraceSeconds"), L"VerifyGraceSeconds", 1, 120, grace, error))
        return false;
    config.verifyGraceSeconds = (int)grace;

    std::wstring preflight = lower(iniValue(values, L"Preflight", L"auto"));
    if (preflight != L"auto" && preflight != L"require" && preflight != L"off") {
        error = L"Preflight must be auto, require, or off";
        return false;
    }
    config.preflight = preflight;
    return true;
}

bool loadConfig(const std::wstring& baseDir, Config& config, std::wstring& error) {
    std::wstring text;
    auto path = joinPath(baseDir, L"ClaudeProtect.ini");
    if (fileExists(path) && !readTextFile(path, text, error)) return false;
    return parseConfigText(baseDir, text, config, error);
}

bool readConfigText(const std::wstring& path, std::wstring& text, std::wstring& error) {
    error.clear();
    return readTextFile(path, text, error);
}

bool saveConfigText(const std::wstring& baseDir, const std::wstring& text, std::wstring& error) {
    error.clear();
    Config candidate;
    if (!parseConfigText(baseDir, text, candidate, error) || !prepareConfig(candidate, error))
        return false;
    auto temporary = candidate.iniPath + L".tmp";
    if (!writeTextFile(temporary, text, error)) return false;
    if (!MoveFileExW(temporary.c_str(), candidate.iniPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"Cannot save settings: " + winErrorText(GetLastError());
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool writeDefaultConfig(const std::wstring& iniPath, const std::wstring& executable,
                        std::wstring& error) {
    std::wstring text;
    text += L"; ClaudeProtect configuration. Changes apply on the next launch.\r\n";
    text += L"; Timezone is a Windows time zone ID, not a display name or IANA name.\r\n";
    text += L"; Examples: UTC, China Standard Time, Tokyo Standard Time, Pacific Standard Time.\r\n";
    text += L"; TimezoneIana only sets the child TZ variable; it is not proof of Electron behavior.\r\n";
    text += L"[ClaudeProtect]\r\n";
    text += L"Executable=" + executable + L"\r\n";
    text += L"Timezone=Pacific Standard Time\r\n";
    text += L"TimezoneIana=America/Los_Angeles\r\n";
    text += L"Locale=en-US\r\n";
    text += L"AnsiCodePage=0\r\n";
    text += L"OemCodePage=0\r\n";
    text += L"FontCharset=0\r\n";
    text += L"RegistryRedirectionMode=2\r\n";
    text += L"HookUILanguageMode=2\r\n";
    text += L"VerifyInjection=strict\r\n";
    text += L"VerifyGraceSeconds=8\r\n";
    text += L"Preflight=auto\r\n";
    text += L"LepDirectory=lep\r\n";
    text += L"ExtraArguments=\r\n";
    return writeTextFile(iniPath, text, error);
}

bool updateIniValue(const std::wstring& iniPath, const std::wstring& key,
                    const std::wstring& value, std::wstring& error) {
    if (value.find_first_of(L"\r\n") != std::wstring::npos) {
        error = L"Configuration values cannot contain line breaks";
        return false;
    }
    std::wstring text;
    if (fileExists(iniPath) && !readTextFile(iniPath, text, error)) return false;

    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = text.size();
        lines.push_back(text.substr(pos, end - pos));
        pos = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();

    size_t sectionStart = lines.size();
    size_t sectionEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring line = trim(lines[i]);
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']' &&
            lower(line.substr(1, line.size() - 2)) == L"claudeprotect") {
            sectionStart = i;
            sectionEnd = lines.size();
            for (size_t j = i + 1; j < lines.size(); ++j) {
                std::wstring next = trim(lines[j]);
                if (!next.empty() && next.front() == L'[') {
                    sectionEnd = j;
                    break;
                }
            }
            break;
        }
    }
    if (sectionStart == lines.size()) {
        lines.push_back(L"[ClaudeProtect]");
        sectionStart = lines.size() - 1;
        sectionEnd = lines.size();
    }

    bool replaced = false;
    for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
        std::wstring line = trim(lines[i]);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        auto equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        if (lower(trim(line.substr(0, equals))) == lower(key)) {
            lines[i] = key + L"=" + value;
            replaced = true;
            break;
        }
    }
    if (!replaced) lines.insert(lines.begin() + sectionEnd, key + L"=" + value);

    std::wstring output;
    for (const auto& line : lines) output += line + L"\r\n";
    return writeTextFile(iniPath, output, error);
}

bool prepareConfig(Config& config, std::wstring& error) {
    config.warnings.clear();
    config.iniPath = joinPath(config.baseDir, L"ClaudeProtect.ini");
    config.logPath = joinPath(config.baseDir, L"ClaudeProtect.log");
    config.probePath = joinPath(config.baseDir, L"EnvironmentProbe.exe");

    if (config.lepDir.empty()) config.lepDir = L"lep";
    if (!isAbsolutePath(config.lepDir)) config.lepDir = joinPath(config.baseDir, config.lepDir);

    if (config.timezoneId.empty()) config.timezoneId = L"Pacific Standard Time";
    if (!timeZoneExists(config.timezoneId)) {
        error = L"Unknown Windows time zone ID: \"" + config.timezoneId +
                L"\". Use an ID such as UTC, China Standard Time, or Tokyo Standard Time.";
        return false;
    }

    if (config.locale.empty()) config.locale = L"en-US";
    if (!IsValidLocaleName(config.locale.c_str())) {
        error = L"Invalid locale name: \"" + config.locale + L"\". Use a BCP-47 name such as en-US.";
        return false;
    }
    config.localeId = LocaleNameToLCID(config.locale.c_str(), 0);
    if (!config.localeId) {
        error = L"Cannot resolve locale name to an LCID: " + config.locale;
        return false;
    }

    if (!config.ansiCodePage) {
        config.ansiCodePage =
            parseLocaleNumber(localeInfo(config.locale, LOCALE_IDEFAULTANSICODEPAGE), 1252);
    }
    if (!config.oemCodePage) {
        config.oemCodePage =
            parseLocaleNumber(localeInfo(config.locale, LOCALE_IDEFAULTOEMCODEPAGE), 0);
        if (!config.oemCodePage) {
            // GetLocaleInfoEx returns "0" for most locales; DBCS locales use
            // the same code page for ANSI and OEM, others default to 437.
            switch (config.ansiCodePage) {
            case 932:
            case 936:
            case 949:
            case 950:
                config.oemCodePage = config.ansiCodePage;
                break;
            default:
                config.oemCodePage = 437;
                break;
            }
        }
    }
    if (!config.fontCharset) config.fontCharset = charsetForCodePage(config.ansiCodePage);

    if (config.timezoneIana.empty()) {
        config.timezoneIana = mapWindowsTimeZoneToIana(config.timezoneId);
        if (config.timezoneIana.empty())
            config.warnings.push_back(L"No IANA mapping for " + config.timezoneId);
    }
    config.timezonePosix = mapWindowsTimeZoneToPosix(config.timezoneId);
    if (config.timezonePosix.empty())
        config.warnings.push_back(L"Cannot derive POSIX TZ for " + config.timezoneId +
                                  L"; TZ will not be set for children.");

    if (config.standardName.size() >= 32 || config.daylightName.size() >= 32) {
        error = L"StandardName/DaylightName must be shorter than 32 characters";
        return false;
    }
    if (config.registryRedirectionMode > 2 || config.uiLanguageMode > 2) {
        error = L"RegistryRedirectionMode and HookUILanguageMode must be 0..2";
        return false;
    }
    if (config.executable.empty()) {
        auto found = discoverClaudeExecutables();
        if (!found.empty()) config.executable = found.front();
    }
    return true;
}

bool checkLepFiles(const Config& config, std::wstring& error, std::wstring& warning) {
    const std::wstring loader = joinPath(config.lepDir, L"LoaderDll_x64.dll");
    const std::wstring core = joinPath(config.lepDir, L"LocaleEmulatorPlus_x64.dll");
    if (!fileExists(loader)) {
        error = L"Missing LEP loader: " + loader +
                L". Build LocaleEmulatorPlus-Core with MSVC and copy the x64 DLLs into " +
                config.lepDir + L".";
        return false;
    }
    if (!fileExists(core)) {
        error = L"Missing LEP Core DLL: " + core;
        return false;
    }
    const std::wstring x86Loader = joinPath(config.lepDir, L"LoaderDll_x86.dll");
    const std::wstring x86Core = joinPath(config.lepDir, L"LocaleEmulatorPlus_x86.dll");
    warning.clear();
    if (!fileExists(x86Loader) || !fileExists(x86Core))
        warning = L"x86 LEP pair missing in " + config.lepDir +
                  L"; x86 child processes cannot be virtualized.";
    return true;
}

std::wstring describeConfig(const Config& config) {
    wchar_t buffer[1024];
    std::wstring text;
    text += L"ini=" + config.iniPath + L"\r\n";
    text += L"log=" + config.logPath + L"\r\n";
    text += L"lep=" + config.lepDir + L"\r\n";
    text += L"probe=" + config.probePath + L"\r\n";
    text += L"executable=" + config.executable + L"\r\n";
    text += L"timezone=" + config.timezoneId + L" (" + config.timezoneIana + L")\r\n";
    text += L"childTZ=" + config.timezonePosix + L"\r\n";
    text += L"locale=" + config.locale + L"\r\n";
    swprintf(buffer, 1024, L"lcid=0x%04lX acp=%lu oemcp=%lu charset=%lu registry=%lu ui=%lu\r\n",
             (unsigned long)config.localeId, (unsigned long)config.ansiCodePage,
             (unsigned long)config.oemCodePage, (unsigned long)config.fontCharset,
             (unsigned long)config.registryRedirectionMode, (unsigned long)config.uiLanguageMode);
    text += buffer;
    text += L"verify=" + config.verifyMode + L" grace=" + std::to_wstring(config.verifyGraceSeconds) +
            L"s preflight=" + config.preflight + L"\r\n";
    if (!config.extraArguments.empty()) text += L"arguments=" + config.extraArguments + L"\r\n";
    for (const auto& warning : config.warnings) text += L"warning: " + warning + L"\r\n";
    return text;
}

std::wstring mapWindowsTimeZoneToPosix(const std::wstring& id) {
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    for (DWORD index = 0; EnumDynamicTimeZoneInformation(index, &zone) == ERROR_SUCCESS; ++index) {
        if (_wcsicmp(zone.TimeZoneKeyName, id.c_str()) != 0) continue;
        SYSTEMTIME now{};
        GetSystemTime(&now);
        TIME_ZONE_INFORMATION rules{};
        if (!GetTimeZoneInformationForYear(now.wYear, &zone, &rules)) return {};
        LONG standardBias = rules.Bias + rules.StandardBias;
        LONG daylightBias = rules.Bias + rules.DaylightBias;
        std::wstring result = posixZoneName(id, standardBias, false) + posixOffset(standardBias);
        if (!rules.DaylightDate.wMonth || !rules.StandardDate.wMonth || standardBias == daylightBias)
            return result;
        auto start = posixTransition(rules.DaylightDate);
        auto end = posixTransition(rules.StandardDate);
        if (start.empty() || end.empty()) return {};
        result += posixZoneName(id, daylightBias, true);
        if (daylightBias != standardBias - 60) result += posixOffset(daylightBias);
        return result + L"," + start + L"," + end;
    }
    return {};
}

std::wstring mapWindowsTimeZoneToIana(const std::wstring& id) {
    static const struct {
        const wchar_t* windows;
        const wchar_t* iana;
    } table[] = {
        {L"UTC", L"Etc/UTC"},
        {L"China Standard Time", L"Asia/Shanghai"},
        {L"Taipei Standard Time", L"Asia/Taipei"},
        {L"Tokyo Standard Time", L"Asia/Tokyo"},
        {L"Korea Standard Time", L"Asia/Seoul"},
        {L"Singapore Standard Time", L"Asia/Singapore"},
        {L"India Standard Time", L"Asia/Kolkata"},
        {L"W. Europe Standard Time", L"Europe/Berlin"},
        {L"Central Europe Standard Time", L"Europe/Budapest"},
        {L"Romance Standard Time", L"Europe/Paris"},
        {L"GMT Standard Time", L"Europe/London"},
        {L"GTB Standard Time", L"Europe/Bucharest"},
        {L"E. Europe Standard Time", L"Europe/Chisinau"},
        {L"Russian Standard Time", L"Europe/Moscow"},
        {L"Turkey Standard Time", L"Europe/Istanbul"},
        {L"Israel Standard Time", L"Asia/Jerusalem"},
        {L"Arab Standard Time", L"Asia/Riyadh"},
        {L"Arabian Standard Time", L"Asia/Dubai"},
        {L"Iran Standard Time", L"Asia/Tehran"},
        {L"Pakistan Standard Time", L"Asia/Karachi"},
        {L"Bangladesh Standard Time", L"Asia/Dhaka"},
        {L"SE Asia Standard Time", L"Asia/Bangkok"},
        {L"Myanmar Standard Time", L"Asia/Yangon"},
        {L"Nepal Standard Time", L"Asia/Kathmandu"},
        {L"Sri Lanka Standard Time", L"Asia/Colombo"},
        {L"AUS Eastern Standard Time", L"Australia/Sydney"},
        {L"AUS Central Standard Time", L"Australia/Darwin"},
        {L"Cen. Australia Standard Time", L"Australia/Adelaide"},
        {L"W. Australia Standard Time", L"Australia/Perth"},
        {L"Tasmania Standard Time", L"Australia/Hobart"},
        {L"New Zealand Standard Time", L"Pacific/Auckland"},
        {L"Hawaiian Standard Time", L"Pacific/Honolulu"},
        {L"Alaskan Standard Time", L"America/Anchorage"},
        {L"Pacific Standard Time", L"America/Los_Angeles"},
        {L"Mountain Standard Time", L"America/Denver"},
        {L"Central Standard Time", L"America/Chicago"},
        {L"Eastern Standard Time", L"America/New_York"},
        {L"Atlantic Standard Time", L"America/Halifax"},
        {L"Newfoundland Standard Time", L"America/St_Johns"},
        {L"SA Pacific Standard Time", L"America/Bogota"},
        {L"E. South America Standard Time", L"America/Sao_Paulo"},
        {L"Argentina Standard Time", L"America/Argentina/Buenos_Aires"},
        {L"South Africa Standard Time", L"Africa/Johannesburg"},
        {L"Egypt Standard Time", L"Africa/Cairo"},
        {L"Morocco Standard Time", L"Africa/Casablanca"},
        {L"W. Central Africa Standard Time", L"Africa/Lagos"},
        {L"E. Africa Standard Time", L"Africa/Nairobi"},
        {L"West Pacific Standard Time", L"Pacific/Port_Moresby"},
        {L"Central Pacific Standard Time", L"Pacific/Guadalcanal"},
    };
    for (const auto& entry : table) {
        if (_wcsicmp(entry.windows, id.c_str()) == 0) return entry.iana;
    }
    return std::wstring();
}

std::vector<std::wstring> discoverClaudeExecutables() {
    std::vector<std::wstring> candidates;
    const std::wstring localAppData = env(L"LOCALAPPDATA");
    scanAppDirectories(joinPath(localAppData, L"AnthropicClaude"), candidates);
    scanAppDirectories(joinPath(localAppData, L"Claude"), candidates);
    scanAppDirectories(joinPath(localAppData, L"Claude-3p"), candidates);
    scanAppDirectories(joinPath(localAppData, L"Programs\\AnthropicClaude"), candidates);
    addCandidate(candidates, joinPath(localAppData, L"Programs\\Claude\\Claude.exe"));
    scanAppDirectories(joinPath(env(L"ProgramFiles"), L"AnthropicClaude"), candidates);
    scanAppDirectories(joinPath(env(L"ProgramFiles"), L"Claude"), candidates);
    scanAppDirectories(joinPath(env(L"ProgramFiles(x86)"), L"AnthropicClaude"), candidates);
    scanAppDirectories(joinPath(env(L"ProgramFiles(x86)"), L"Claude"), candidates);

    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(L"C:\\Program Files\\WindowsApps\\Claude_*", &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            addCandidate(candidates,
                         std::wstring(L"C:\\Program Files\\WindowsApps\\") + entry.cFileName +
                             L"\\app\\claude.exe");
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    addCandidate(candidates, joinPath(env(L"ProgramFiles"), L"Claude\\Claude.exe"));
    addCandidate(candidates, joinPath(env(L"ProgramFiles(x86)"), L"Claude\\Claude.exe"));

    std::sort(candidates.begin(), candidates.end(), [](const std::wstring& a, const std::wstring& b) {
        int cmp = compareVersions(a, b);
        if (cmp != 0) return cmp > 0;
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    return candidates;
}

bool isClaudeDesktopPath(const std::wstring& path, const std::wstring& target) {
    if (!target.empty() && _wcsicmp(path.c_str(), target.c_str()) == 0) return true;
    std::wstring p = lower(path);
    std::wstring name = fileName(p);
    if (name != L"claude.exe") return false;
    // Claude Code's npm launcher is also named claude.exe; never treat it as
    // Claude Desktop for the single-instance refusal check.
    if (p.find(L"\\npm\\node_modules\\@anthropic-ai\\claude-code\\") != std::wstring::npos)
        return false;
    if (p.find(L"\\windowsapps\\claude_") != std::wstring::npos &&
        p.find(L"\\app\\claude.exe") != std::wstring::npos)
        return true;
    if (p.find(L"\\anthropicclaude\\app-") != std::wstring::npos) return true;
    if (p.find(L"\\claude\\app-") != std::wstring::npos) return true;
    if (p.find(L"\\programs\\claude\\claude.exe") != std::wstring::npos) return true;
    if (p.find(L"\\program files\\claude\\claude.exe") != std::wstring::npos) return true;
    if (p.find(L"\\program files (x86)\\claude\\claude.exe") != std::wstring::npos) return true;
    if (p.find(L"\\claude-3p\\app-") != std::wstring::npos) return true;
    return false;
}

bool findUnmanagedClaudeDesktop(const std::wstring& target, DWORD& pid, std::wstring& path,
                                std::wstring& warning) {
    pid = 0;
    path.clear();
    warning.clear();
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.h) {
        warning = L"Cannot enumerate processes: " + winErrorText(GetLastError());
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        warning = L"Cannot enumerate processes: " + winErrorText(GetLastError());
        return false;
    }
    int inaccessible = 0;
    do {
        if (_wcsicmp(entry.szExeFile, L"claude.exe") != 0) continue;
        if (entry.th32ProcessID == GetCurrentProcessId()) continue;
        std::wstring image;
        if (!processImagePath(entry.th32ProcessID, image)) {
            ++inaccessible;
            continue;
        }
        if (isClaudeDesktopPath(image, target)) {
            pid = entry.th32ProcessID;
            path = image;
            return true;
        }
    } while (Process32NextW(snapshot, &entry));
    if (inaccessible)
        warning = std::to_wstring(inaccessible) +
                  L" claude.exe process(es) could not be inspected; they were not treated as "
                  L"Claude Desktop.";
    return false;
}
