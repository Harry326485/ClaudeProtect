#include "../src/config.hpp"
#include "../src/win.hpp"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::wstring base = argv[1], error, text, after;
    CreateDirectoryW(base.c_str(), nullptr);
    auto ini = joinPath(base, L"ClaudeProtect.ini");
    DeleteFileW(ini.c_str()); // Test fixture only.
    Config cfg;
    auto require = [&](bool ok, const char* detail) {
        if (!ok) { fprintf(stderr, "FAIL: %s (%ls)\n", detail, error.c_str()); exit(1); }
    };
    require(loadConfig(base, cfg, error) && prepareConfig(cfg, error), "missing INI defaults");
    require(cfg.timezoneId == L"Pacific Standard Time" && cfg.timezoneIana == L"America/Los_Angeles",
            "LA default and IANA mapping");
    require(cfg.timezonePosix == L"PST8PDT,M3.2.0,M11.1.0", "LA POSIX TZ and DST rules");
    const struct {
        const wchar_t* windows;
        const wchar_t* posix;
    } zones[] = {
        {L"UTC", L"UTC0"},
        {L"Tokyo Standard Time", L"JST-9"},
        {L"India Standard Time", L"<+0530>-5:30"},
        {L"Nepal Standard Time", L"<+0545>-5:45"},
        {L"Lord Howe Standard Time", L"<+1030>-10:30<+1100>-11,M10.1.0,M4.1.0"},
        {L"New Zealand Standard Time", L"<+1200>-12<+1300>,M9.5.0,M4.1.0/3:00:00"},
    };
    for (const auto& zone : zones)
        require(mapWindowsTimeZoneToPosix(zone.windows) == zone.posix, "POSIX TZ conversion");
    require(mapWindowsTimeZoneToPosix(L"Not a real zone").empty(), "unknown POSIX TZ rejected");
    require(writeDefaultConfig(ini, L"", error) && readConfigText(ini, text, error), "default INI");
    require(text.find(L"Timezone=Pacific Standard Time") != text.npos &&
            text.find(L"TimezoneIana=America/Los_Angeles") != text.npos, "written LA defaults");
    text += L"; Unicode path and comment round trip: \u8bbe\u7f6e\r\n";
    require(saveConfigText(base, text, error), "valid save");
    require(readConfigText(ini, after, error) && after == text, "Unicode round trip");
    require(!saveConfigText(base, text + L"Timezone=Not a real zone\r\n", error), "reject invalid zone");
    require(readConfigText(ini, after, error) && after == text, "invalid edit preserves original");
    require(!saveConfigText(base, text + L"HookUILanguageMode=3\r\n", error), "reject invalid mode");
    require(!saveConfigText(base, text + L"Locale=%%%\r\n", error), "reject invalid locale");
    require(readConfigText(ini, after, error) && after == text, "failed saves preserve original");
    require(saveConfigText(base, L"[ClaudeProtect]\r\nTimezone=\r\n", error) &&
            loadConfig(base, cfg, error) && prepareConfig(cfg, error) &&
            cfg.timezoneIana == L"America/Los_Angeles", "blank timezone defaults to LA");
    require(!fileExists(ini + L".tmp"), "atomic-save temporary file cleaned up");
    puts("Settings validation, atomic save, Unicode, LA defaults and POSIX TZ rules passed.");
    return 0;
}
