# LEP verification report (2026-10-29)

## Environment

- Windows 11 22621, Claude Desktop 4.29.0 (Electron 44.9.3).
- MSVC 14.51.36231, Windows SDK 10.0.26100.0.
- LocaleEmulatorPlus-Core built with `scripts/build-lep.ps1`; x64/x86 DLLs copied to `build\lep`.
- Upstream test suites (`run-public-api`, `run-timezone`, `run-lepb-compat`, `run-broker-handle`) all exited 0 after the patches below.

## Upstream changes (isolated in the submodule)

1. **Dynamic time zone**: ClaudeProtect passes `CP_TIMEZONE=<Windows ID>` in the child environment. `LepGlobalData::Initialize` reads it; `LepNtQuerySystemInformation(SystemDynamicTimeZoneInformation)` returns the configured `RTL_DYNAMIC_TIME_ZONE_INFORMATION` including `TimeZoneKeyName`. This fixes Chromium/ICU, which uses the dynamic API.
2. **Code pages**: `GetACP`/`GetOEMCP` exports return the configured ACP/OEMCP. PEB/ntdll sync alone did not cover `GetACP` in this build.
3. **UI language**: `HookUILanguageMode >= 1` installs the `GetUserDefaultUILanguage`/`GetSystemDefaultUILanguage` export hooks (previously mode 1 only), so mode 2 has both MUI redirection and the Win32 exports.
4. **Preferred UI languages**: `GetUserPreferredUILanguages`/`GetSystemPreferredUILanguages` exports return the configured culture; `RtlGetThreadPreferredUILanguages` overrides the host list in virtual MUI mode.

Public LEPB v4 and `LEP_RUNTIME_ENVIRONMENT` layouts are unchanged. Details: `LocaleEmulatorPlus-Core/ai-docs/2026-10-29-claude-protect-integration.md`.

## Native self-test

`build\ClaudeProtect.exe --selftest` with `Timezone=UTC, Locale=en-US`:

- `tzKey=UTC`, static and dynamic bias 0, offsets 0.
- `locale=en-US`, `lcid=1033`, ACP 1252, OEMCP 437, UI 1033.
- `preferredUser=preferredSystem=en-US`.
- `lepCore=true`, `lepLog=true`, `inJob=true`; child matches; exit 0.

With `Timezone=Tokyo Standard Time, Locale=ja-JP`:

- `tzKey=Tokyo Standard Time`, bias -540, names `東京標準時`, offsets 540 (UTC+9).
- `locale=ja-JP`, `lcid=1041`, ACP/OEM 932, UI 1041.
- `preferredUser=preferredSystem=ja-JP`.
- Child matches; exit 0.

Residual native observations: `systemLocale`/`systemLcid` remain host (`zh-CN`/2052); `GetLocaleInfoEx` short-date formatting remained host in this probe (`yyyy/M/d`). The user locale, UI language, code pages and time zone are virtualized.

## Chromium renderer and Worker

Edge 149 headless under LEP, `--lang=en-US`, config UTC/en-US:

```json
main/worker: tz=UTC, locale=en-US, numberLocale=en-US,
             languages=[en-US,en], language=en-US,
             offsetJan=0, offsetJul=0
```

Edge 149 headless under LEP, `--lang=ja-JP`, config Tokyo/ja-JP:

```json
main/worker: tz=Asia/Tokyo, locale=ja-JP, numberLocale=ja-JP,
             languages=[ja,en,en-GB,en-US], language=ja,
             offsetJan=-540, offsetJul=-540
```

Process command lines carried `--lang=ja`; no process had `--no-sandbox`; all probe Chromium processes were gone after Stop. This is the proxy evidence for Electron/Chromium's renderer and Worker behavior. Claude's own renderer cannot be evaluated through CDP (see below).

## Claude Desktop under LEP

`scripts/claude-observations.ps1` with Tokyo/ja-JP:

- 7 processes in the Job: main, crashpad, GPU, network utility, three renderers.
- Main command line: `--lang=ja-JP` (ClaudeProtect appends it automatically from `Locale`).
- Renderers: `--lang=ja`, `--enable-sandbox`; utility `--lang=ja`.
- Every process has a successful LEP injection record and/or initialization log; no fail-closed.
- `preferredSystemLanguages` inside Claude's `--desktop-managed-config` still showed the host list (`zh-Hans-CN,en-US`); see residual gaps.
- Stop left 0 Claude Desktop processes.
- Killing the supervisor left 0 Claude Desktop processes (`KILL_ON_JOB_CLOSE`).
- Killing only the main process caused the supervisor to stop the job and leave 0 helpers.

## Claude CDP limitation

Claude Desktop rejects `--remote-debugging-port`, `--remote-debugging-pipe`, and ignores `--inspect`; packaged `NODE_OPTIONS=--require` is ignored. Copying its binary into a separate Electron runtime also failed. Direct renderer JavaScript observation is therefore not available without modifying installed files. The Edge proxy uses the same Chromium/ICU stack and the same LEP hooks; Claude is observed through its process tree, command-line `--lang`, and native probe.

## Residual gaps

1. Claude's `preferredSystemLanguages` app field remains host even though native `GetUserPreferredUILanguages` returns the configured culture. Electron likely caches/reads a different path; renderer `navigator.language(s)` is still expected to follow `--lang`, as the Edge proxy shows.
2. `GetSystemDefaultLocaleName`/system LCID remain host; only the user locale is virtualized.
3. `GetLocaleInfoEx` short/long date formatting remained host in the probe; Chromium/ICU formatting uses the virtualized locale and is correct.
4. External helpers such as 360 Safe's `browprom64.exe` may be created in the Job and fail LEP injection. ClaudeProtect logs them as external and does not fail the session; managed app processes still fail closed.
5. A short-lived managed process that fails injection and exits before verification can escape the live checks; log evidence remains.

## Reproduce

```powershell
pwsh -NoProfile -File scripts/build-lep.ps1 -Test
build\ClaudeProtect.exe --selftest
pwsh -NoProfile -File scripts/test-lep.ps1 -RunChromium -RunClaude
pwsh -NoProfile -File scripts/chromium-observations.ps1 -ExtraArgs '--lang=ja-JP'
pwsh -NoProfile -File scripts/claude-observations.ps1 -ExpectLang ja-JP
```
