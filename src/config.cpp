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
        error = std::wstring(L"设置值无效，") + name + L" = " + text;
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

    config.application = lower(iniValue(values, L"Application", L"desktop"));
    if (config.application != L"desktop" && config.application != L"science") {
        error = L"目标应用必须为 desktop 或 science。";
        return false;
    }
    config.executable = iniValue(values, L"Executable");
    config.timezoneId = iniValue(values, L"Timezone", L"Pacific Standard Time");
    config.timezoneIana = mapWindowsTimeZoneToIana(config.timezoneId);
    config.locale = iniValue(values, L"Locale", L"en-US");
    config.standardName = iniValue(values, L"StandardName");
    config.daylightName = iniValue(values, L"DaylightName");
    config.extraArguments = iniValue(values, L"ExtraArguments");
    config.desktopExecutable = iniValue(values, L"DesktopExecutable",
        config.application == L"desktop" ? config.executable.c_str() : L"");
    config.scienceExecutable = iniValue(values, L"ScienceExecutable",
        config.application == L"science" ? config.executable.c_str() : L"");
    config.desktopArguments = iniValue(values, L"DesktopArguments",
        config.application == L"desktop" ? config.extraArguments.c_str() : L"");
    if (isClaudeSciencePath(config.desktopExecutable) ||
        isClaudeDesktopPath(config.scienceExecutable, L"")) {
        error = L"DesktopExecutable 或 ScienceExecutable 与应用不匹配，请选择对应的启动器。";
        return false;
    }
    config.executable = config.application == L"science" ? config.scienceExecutable : config.desktopExecutable;
    if (config.application == L"desktop") config.extraArguments = config.desktopArguments;
    config.lepDir = joinPath(baseDir, L"lep");

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
        error = L"注入验证必须为 strict、warn 或 off。";
        return false;
    }
    config.verifyMode = verify;

    unsigned long grace = 8;
    if (!parseUlong(iniValue(values, L"VerifyGraceSeconds"), L"VerifyGraceSeconds", 1, 120, grace, error))
        return false;
    config.verifyGraceSeconds = (int)grace;

    std::wstring preflight = lower(iniValue(values, L"Preflight", L"auto"));
    if (preflight != L"auto" && preflight != L"require" && preflight != L"off") {
        error = L"启动预检必须为 auto、require 或 off。";
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
    static LONG sequence = 0;
    auto temporary = candidate.iniPath + L"." + std::to_wstring(GetCurrentProcessId()) +
                     L"." + std::to_wstring(InterlockedIncrement(&sequence)) + L".tmp";
    if (!writeTextFile(temporary, text, error)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
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
    text += L"; IANA is automatic; LEP is fixed to the lep directory beside the executable.\r\n";
    text += L"[ClaudeProtect]\r\n";
    text += L"Application=desktop\r\n";
    text += L"Executable=" + executable + L"\r\n";
    text += L"Timezone=Pacific Standard Time\r\n";
    text += L"Locale=en-US\r\n";
    text += L"AnsiCodePage=0\r\n";
    text += L"OemCodePage=0\r\n";
    text += L"FontCharset=0\r\n";
    text += L"RegistryRedirectionMode=2\r\n";
    text += L"HookUILanguageMode=2\r\n";
    text += L"VerifyInjection=strict\r\n";
    text += L"VerifyGraceSeconds=8\r\n";
    text += L"Preflight=auto\r\n";
    text += L"ExtraArguments=\r\n";
    return writeTextFile(iniPath, text, error);
}

bool loadSettings(const std::wstring& baseDir, SettingsValues& values,
                  std::wstring& source, std::wstring& error) {
    source.clear();
    error.clear();
    const auto path = joinPath(baseDir, L"ClaudeProtect.ini");
    if (fileExists(path) && !readTextFile(path, source, error)) return false;
    values = parseIni(source);
    return true;
}

bool loadApplicationConfig(const std::wstring& baseDir, const std::wstring& application,
                           Config& config, std::wstring& error) {
    if (application != L"desktop" && application != L"science") {
        error = L"目标应用必须为 desktop 或 science。";
        return false;
    }
    if (!loadConfig(baseDir, config, error)) return false;
    config.application = application;
    config.executable = application == L"science" ? config.scienceExecutable : config.desktopExecutable;
    config.extraArguments = application == L"science" ? L"" : config.desktopArguments;
    return prepareConfig(config, error);
}

bool saveSettings(const std::wstring& baseDir, const SettingsValues& values,
                  const std::wstring& source, std::wstring& error) {
    SettingsValues current;
    std::wstring text;
    if (!loadSettings(baseDir, current, text, error)) return false;
    if (text != source) {
        error = L"配置文件已被其他程序修改。请先重新加载，再保存设置。";
        return false;
    }
    for (const auto& entry : values) {
        if (entry.second.find_first_of(L"\r\n") != std::wstring::npos || entry.second.find(L'\0') != std::wstring::npos) {
            error = L"设置内容不能包含换行。";
            return false;
        }
    }
    SettingsValues remaining = values;
    std::wstring output, section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end;
        if (pos < text.size() && text[pos] == L'\r') ++pos;
        if (pos < text.size() && text[pos] == L'\n') ++pos;
        auto clean = trim(line);
        if (!clean.empty() && clean.front() == L'[' && clean.back() == L']')
            section = lower(trim(clean.substr(1, clean.size() - 2)));
        auto equals = clean.find(L'=');
        if ((section.empty() || section == L"claudeprotect") && !clean.empty() &&
            clean[0] != L';' && clean[0] != L'#' && equals != std::wstring::npos) {
            auto key = lower(trim(clean.substr(0, equals)));
            auto entry = values.find(key);
            if (entry != values.end()) {
                // Update every occurrence because the INI parser uses the last value.
                line = clean.substr(0, equals + 1) + L"\"" + entry->second + L"\"";
                remaining.erase(key);
            }
        }
        output += line + L"\r\n";
    }
    if (!remaining.empty()) {
        if (section != L"claudeprotect") output += L"[ClaudeProtect]\r\n";
        for (const auto& entry : remaining) output += entry.first + L"=\"" + entry.second + L"\"\r\n";
    }
    return saveConfigText(baseDir, output, error);
}

bool prepareConfig(Config& config, std::wstring& error) {
    config.warnings.clear();
    config.iniPath = joinPath(config.baseDir, L"ClaudeProtect.ini");
    config.logPath = joinPath(config.baseDir, L"ClaudeProtect.log");
    config.probePath = joinPath(config.baseDir, L"EnvironmentProbe.exe");

    config.lepDir = joinPath(config.baseDir, L"lep");

    if (config.timezoneId.empty()) config.timezoneId = L"Pacific Standard Time";
    if (!timeZoneExists(config.timezoneId)) {
        error = L"Timezone（Windows 时区 ID）无效，\"" + config.timezoneId +
                L"\"。请从时区列表选择，或填写有效的 Windows 时区 ID。";
        return false;
    }

    if (config.locale.empty()) config.locale = L"en-US";
    if (!IsValidLocaleName(config.locale.c_str())) {
        error = L"Locale（区域代码）无效，\"" + config.locale + L"\"。请填写 en-US、zh-CN 等 BCP-47 代码。";
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

    config.timezoneIana = mapWindowsTimeZoneToIana(config.timezoneId);
    if (config.timezoneIana.empty())
        config.warnings.push_back(L"No IANA mapping for " + config.timezoneId);
    config.timezonePosix = mapWindowsTimeZoneToPosix(config.timezoneId);
    if (config.timezonePosix.empty())
        config.warnings.push_back(L"Cannot derive POSIX TZ for " + config.timezoneId +
                                  L"; TZ will not be set for children.");

    if (config.standardName.size() >= 32 || config.daylightName.size() >= 32) {
        error = L"StandardName / DaylightName（标准时与夏令时名称）均不能超过 31 个 UTF-16 字符。";
        return false;
    }
    if (config.registryRedirectionMode > 2 || config.uiLanguageMode > 2) {
        error = L"注册表重定向和界面语言模式必须为 0、1 或 2。";
        return false;
    }
    if (config.executable.empty()) {
        auto found = discoverApplicationExecutables(config.application);
        if (!found.empty()) config.executable = found.front();
    }
    if ((config.application == L"desktop" && isClaudeSciencePath(config.executable)) ||
        (config.application == L"science" && isClaudeDesktopPath(config.executable, L""))) {
        error = L"启动器与目标应用不匹配，请清空启动器路径以自动发现所选应用。";
        return false;
    }
    if (config.application == L"science" && !config.extraArguments.empty()) {
        error = L"Science 桌面窗口使用默认启动交接，请清空附加启动参数。";
        return false;
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
    text += L"application=" + config.application + L"\r\n";
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
        {L"Dateline Standard Time", L"Etc/GMT+12"},
        {L"UTC-11", L"Pacific/Pago_Pago"},
        {L"US Mountain Standard Time", L"America/Phoenix"},
        {L"Central Standard Time (Mexico)", L"America/Mexico_City"},
        {L"SA Western Standard Time", L"America/Puerto_Rico"},
        {L"UTC-02", L"America/Noronha"},
        {L"Cape Verde Standard Time", L"Atlantic/Cape_Verde"},
        {L"Azores Standard Time", L"Atlantic/Azores"},
        {L"Greenwich Standard Time", L"Atlantic/Reykjavik"},
        {L"FLE Standard Time", L"Europe/Helsinki"},
        {L"E. Australia Standard Time", L"Australia/Brisbane"},
        {L"Fiji Standard Time", L"Pacific/Fiji"},
        {L"Tonga Standard Time", L"Pacific/Tongatapu"},
        {L"Line Islands Standard Time", L"Pacific/Kiritimati"},
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

std::wstring applicationName(const std::wstring& application) {
    return application == L"science" ? L"Claude Science" : L"Claude Desktop";
}

bool isClaudeSciencePath(const std::wstring& path) {
    auto p = lower(path);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    if (fileName(p) != L"claude-science.exe") return false;
    return p.find(L"\\programs\\claudescience\\claude-science.exe") != p.npos ||
           p.find(L"\\program files\\claudescience\\claude-science.exe") != p.npos ||
           p.find(L"\\claudescience\\app-") != p.npos ||
           p.find(L"\\.claude-science\\electron\\") != p.npos;
}

std::vector<std::wstring> discoverScienceExecutables() {
    std::vector<std::wstring> candidates;
    addCandidate(candidates, joinPath(env(L"LOCALAPPDATA"),
                                    L"Programs\\ClaudeScience\\claude-science.exe"));
    addCandidate(candidates, joinPath(env(L"ProgramFiles"), L"ClaudeScience\\claude-science.exe"));
    // The versioned Electron binary requires a shell and launcher environment.
    // Discover the stable CLI entry point only.
    return candidates;
}

std::vector<std::wstring> discoverApplicationExecutables(const std::wstring& application) {
    return application == L"science" ? discoverScienceExecutables() : discoverClaudeExecutables();
}

bool findUnmanagedClaudeDesktop(const std::wstring& target, DWORD& pid, std::wstring& path,
                                std::wstring& warning) {
    return findUnmanagedApplication(L"desktop", target, pid, path, warning);
}

bool findUnmanagedApplication(const std::wstring& application, const std::wstring& target,
                              DWORD& pid, std::wstring& path, std::wstring& warning) {
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
        const auto expected = application == L"science" ? L"claude-science.exe" : L"claude.exe";
        if (_wcsicmp(entry.szExeFile, expected) != 0 &&
            _wcsicmp(entry.szExeFile, fileName(target).c_str()) != 0) continue;
        if (entry.th32ProcessID == GetCurrentProcessId()) continue;
        std::wstring image;
        if (!processImagePath(entry.th32ProcessID, image)) {
            ++inaccessible;
            continue;
        }
        bool matches = application == L"science"
            ? isClaudeSciencePath(image) || _wcsicmp(image.c_str(), target.c_str()) == 0
            : isClaudeDesktopPath(image, target);
        if (matches) {
            pid = entry.th32ProcessID;
            path = image;
            return true;
        }
    } while (Process32NextW(snapshot, &entry));
    if (inaccessible)
        warning = std::to_wstring(inaccessible) +
                  L" candidate process(es) could not be inspected for " + applicationName(application) + L".";
    return false;
}
