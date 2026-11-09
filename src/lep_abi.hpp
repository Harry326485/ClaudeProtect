#pragma once
// Standalone copy of LocaleEmulatorPlus-Core's public LEPB v4 declaration.
// Source of truth: LocaleEmulatorPlus-Core/include/LocaleEmulatorPlus.ixx at the
// pinned submodule commit. Kept as a plain C++17 header so the tray build does
// not need C++23 modules or Core headers.
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <cstddef>

namespace lep {

using NTSTATUS = LONG;
inline constexpr ULONG kEnvironmentVersion = 4;
inline constexpr ULONG kRegistryRedirectionModeMax = 2;
inline constexpr ULONG kUiLanguageModeMax = 2;

#pragma pack(push, 8)
struct EnvironmentBlock {
    ULONG Size;
    ULONG Version;
    ULONG AnsiCodePage;
    ULONG OemCodePage;
    ULONG LocaleId;
    ULONG FontCharset;
    ULONG RegistryRedirectionMode;
    ULONG HookUILanguageMode;
    WCHAR TimezoneId[128];          // Required, zero-terminated Windows ID.
    WCHAR StandardNameOverride[32]; // Empty uses the resolved default.
    WCHAR DaylightNameOverride[32]; // Empty uses the resolved default.
};
#pragma pack(pop)

// Both exports share this signature. The return value is a signed NTSTATUS,
// not a BOOL or GetLastError code; success is status >= 0.
using LepCreateProcessFn = NTSTATUS(WINAPI*)(EnvironmentBlock*, PCWSTR, PWSTR, PCWSTR, ULONG,
                                            LPSTARTUPINFOW, LPPROCESS_INFORMATION,
                                            LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, PVOID,
                                            HANDLE);

static_assert(sizeof(EnvironmentBlock) == 416, "LEPB v4 must be 416 bytes on x64");
static_assert(offsetof(EnvironmentBlock, TimezoneId) == 32, "LEPB TimezoneId offset");
static_assert(offsetof(EnvironmentBlock, StandardNameOverride) == 288, "LEPB standard name offset");
static_assert(offsetof(EnvironmentBlock, DaylightNameOverride) == 352, "LEPB daylight name offset");

} // namespace lep
