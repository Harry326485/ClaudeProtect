# Implementation and verification

## Component map

| File | Responsibility |
| --- | --- |
| `src/lep_abi.hpp` | Standalone LEPB v4 declaration copied from the pinned Core public API. Static asserts: 416 bytes, offsets 32/288/352. |
| `src/lep.cpp` | Loads `LoaderDll_x64.dll` by absolute path, resolves `LepCreateProcess2`, validates input, forces `CREATE_SUSPENDED`, returns the suspended process. |
| `src/config.cpp` | UTF-8/UTF-16 INI reader, dynamic Claude Desktop discovery, Windows time zone validation, locale/LCID/code-page/font-charset resolution, path-based unmanaged-instance detection. |
| `src/session.cpp` | Job Object creation with `KILL_ON_JOB_CLOSE`, LEP launch, probe preflight, runtime injection-record/log verification, Stop and main-exit cleanup. |
| `src/main.cpp` | Tray UI and CLI (`--check`, `--print-config`, `--edit-settings`, `--discover`, `--selftest`, `--run`). |
| `src/ui.cpp`, `ui/` | Window-scoped WebView2 host, embedded Chinese settings forms and a native executable file dialog. |
| `src/probe.cpp` | Native observation probe for time zone, locale, UI language, code pages, Job membership, LEP Core presence, and child propagation. |

## WebView2 interface

The UI uses vanilla HTML/CSS/JavaScript embedded as an EXE resource. The build downloads the official SDK 1.0.2903.40 with a pinned checksum and copies its x64 loader and notices into the runtime directory. MinGW uses a minimal EventToken.h declaration to bridge its Windows headers.

The host creates WebView2 only when a panel opens. Closing calls Close and releases all controller/environment references. Event callbacks hold weak references to the window state. Minimizing hides the controller and requests TrySuspend, restoring resumes it. A visible panel samples status once per second and sends only changed snapshots. There are no external UI resources or HTTP listener. The embedded page has a restrictive CSP, navigation is restricted to the initial bundled document, new windows and permission requests are blocked, and the native bridge accepts a fixed action/setting allowlist from that document.

The raw settings form preserves empty/zero automatic values, legacy boolean aliases, unknown entries and comments. Atomic saving validates the candidate first and rejects a stale disk snapshot. Startup and environment self-tests run in a background future using a private Session, then transfer ownership on the UI thread. An event cancels probe waits on supervisor exit. The normal CLI backend remains independent of the WebView2 loader.

The default window is 740×600 at 96 DPI (minimum 600×440); CSS uses compact typography and spacing without a scaling transform. The timezone selector contains curated city groups across each whole-hour offset from UTC−12 to UTC+14, with offsets and daylight labels read from the current year’s Windows rules on settings load. UTC−12 uses uninhabited Baker Island. Locale is a select control. Existing values outside either catalog remain visible as a disabled current option until the user chooses a replacement; simply opening the panel does not rewrite them. There is no log page or web bridge log-reading action.

## Launch sequence

1. Refuse if another managed session is active.
2. For Claude Desktop targets, enumerate `claude.exe` processes, read their image paths, and refuse if an unmanaged Claude Desktop is running. Claude Code's npm `claude.exe` is excluded by path.
3. Validate the Windows time zone ID against `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones`, resolve the BCP-47 locale to an LCID, and derive ACP/OEMCP/font charset.
4. Run `EnvironmentProbe.exe` under LEP with `--spawn-child`, expecting the configured time zone, locale, code pages, UI language, LEP Core presence, and Job membership in both parent and child. Any mismatch aborts the Claude launch.
5. Create a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
6. Build a child-only environment block (host environment minus `TZ`/`LANG`/`LC_ALL`/`LANGUAGE`/`LC_MESSAGES`/`CP_*`, plus configured values, `CP_TIMEZONE`, `CP_LOCALE`, and `CLAUDE_PROTECT_SESSION=1`). LEP Core reads `CP_TIMEZONE` for dynamic time-zone queries.
7. For Claude Desktop, append `--lang=<Locale>` unless the configured arguments already contain `--lang=`, then call `LepCreateProcess2` with LEPB v4 and `CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT`.
8. Assign the suspended process to the Job Object. On failure, terminate the still-suspended process and close handles.
9. Resume the primary thread.
10. Poll the main process. If it exits, terminate the job to clean up helpers. Tray Stop terminates the job explicitly. Runtime verification accepts a process only with LEP injection records or a successful initialization log; managed processes fail closed in strict mode.

## Configuration

`ClaudeProtect.ini` is created beside the executable. Values apply on the next launch.

| Key | Meaning |
| --- | --- |
| `Executable` | Claude Desktop executable. Empty triggers dynamic discovery. |
| `Timezone` | Windows time zone ID required by LEP, e.g. `China Standard Time`, `Tokyo Standard Time`, `UTC`. |
| `TimezoneIana` | Legacy INI override ignored; the diagnostic IANA name is always derived from the Windows time zone. The child `TZ` is derived as POSIX rules from the Windows time zone. |
| `StandardName`, `DaylightName` | Legacy LEP display-name overrides (max 31 chars), no longer exposed in the UI. Empty uses the time zone registry names. |
| `Locale` | BCP-47 locale, e.g. `en-US`, `ja-JP`, `zh-CN`. |
| `AnsiCodePage`, `OemCodePage`, `FontCharset` | `0` derives from the locale. |
| `RegistryRedirectionMode` | 0 covers basic ACP/OEMCP/default-language queries, 1 adjusts NLS queries, 2 adds user region and preferred languages (default). |
| `HookUILanguageMode` | 0 disables dedicated UI-language hooks, 1 covers Win32 UI-language exports, 2 adds native UI, MUI, preferred-language and WinRT hooks (default). |
| `VerifyInjection` | `strict` stops the job when a managed job process has no LEP injection record/log; `warn` logs only; `off` disables. |
| `VerifyGraceSeconds` | Age before a process is checked (default 8). |
| `Preflight` | `auto`, `require`, or `off`. `auto` runs the probe when present. |
| `LepDirectory` | Legacy INI override ignored; both DLLs are loaded from the fixed `lep` directory beside the executable. |
| `ExtraArguments` | Extra arguments passed to Claude Desktop. `--lang=<Locale>` is appended automatically unless this already contains a `--lang=` switch. |

## Failure containment

- **Unmanaged instance**: refused before launch, because Electron single-instance forwarding could route the launch to an unsupervised process. Detection reads each `claude.exe` image path and ignores Claude Code's npm launcher. If a `claude.exe` image path cannot be read (for example, a process at a different integrity level), the launch is refused because the check cannot be completed.
- **Preflight failure**: no Claude process is created.
- **Job assignment failure**: the still-suspended process is terminated before it can execute.
- **Runtime injection gap**: LEP Core is manually mapped and invisible to `GetModuleHandle`/Toolhelp. A live job process is accepted only if it has a `create edge ... injectStatus=00000000` record, a successful `child injection complete` record, or a `GlobalData.Initialize end status=00000000` log. `rundll32.exe` (LEP cross-architecture broker), `conhost.exe`, and `WerFault.exe` are exempt. In strict mode a managed process without evidence stops the job; external helpers outside the target directory are logged and ignored. Cross-architecture children require the x86 loader/core pair.
- **LEP log evidence**: new `Log\*.log.txt` and broker logs are scanned for non-zero injection status. Failures for managed image paths stop the job in strict mode; failures for external helpers (for example antivirus helpers) are logged and ignored. A failure whose child has already exited is logged but not treated as managed because its path cannot be classified.
- **Supervisor exit or crash**: closing the last Job Object handle triggers `KILL_ON_JOB_CLOSE`.
- **Main process exit**: remaining helpers are terminated by the supervisor.
- **Direct diagnostic mode** (`--run --direct`): LEP is not loaded and injection verification is disabled for that session; it exercises Job lifecycle and Claude launch only. The tray never uses this path.

Limits: a managed process that fails injection and exits before a verification pass can evade live checks; LEP log evidence remains. External helper failures are not contained. These checks are detection/containment rather than upstream fail-closed behavior.

## Residual host values

The observations below describe the October 29 build. The later thread/WinRT language patch passes independent probes; the final Claude interface has not been retested. See [current language verification](fixes-2026-11-02.md).

- Claude's `preferredSystemLanguages` field inside `--desktop-managed-config` still shows the host list (`zh-Hans-CN,en-US`), although native `GetUserPreferredUILanguages` returns the configured culture. Chromium's renderer language follows `--lang`, which ClaudeProtect appends automatically.
- `GetSystemDefaultLocaleName`/system LCID remain host; only the user locale is virtualized.
- `GetLocaleInfoEx` short/long date formatting remained host in the probe; Chromium/ICU `Intl` uses the virtualized locale and reports the configured locale.
- External helpers (for example 360 Safe's `browprom64.exe`) may fail LEP injection; they are logged as external and do not fail the session. Managed app processes still fail closed.
- A managed process that fails injection and exits before a verification pass may escape live checks; LEP log evidence remains.

## Verification matrix

| Area | Evidence | Status |
| --- | --- | --- |
| Probe baseline (host) | `EnvironmentProbe.exe` reports host `China Standard Time`, `zh-CN`, ACP/OEMCP 936, UI 2052; parent/child both run. | Proven |
| Probe expectations | Wrong time zone/locale exits 2; matching values exit 0. | Proven |
| Job Stop cleanup | `--run --direct --seconds 2 --target EnvironmentProbe.exe -- --seconds 60`; no probe remains; log records `managed job closed`. | Proven (direct mode) |
| Job tree cleanup | Probe parent plus child (`--spawn-child --child-seconds 60`) both gone after Stop. | Proven (direct mode) |
| Main-exit cleanup | Probe with `--seconds 1`; log records `main process exited` and job close. | Proven (direct mode) |
| Supervisor kill-on-close | Supervisor killed with `Stop-Process -Force`; probe job member terminated within 2s. | Proven (direct mode) |
| Path-based refusal | Fake `...\WindowsApps\Claude_...\app\claude.exe` detected; Claude Code npm process ignored; `--run` exits 1. | Proven |
| Claude Desktop direct launch | MSIX `claude.exe` launched directly; Job contained 7 processes (main, crashpad, GPU, network utility, renderers); Stop left 0. Renderer uses `--enable-sandbox`, `--lang=zh-CN`, and `preferredSystemLanguages`. See `claude-desktop-baseline-2026-10-29.md`. | Proven (direct mode, no virtualization) |
| LEP loader/ABI | Adapter compiles; LEPB static asserts pass; `--print-config` reports `LEPB size=416`. | Proven (compile-time) |
| LEP upstream build-script tests | `python -m unittest discover -s tests -p test_build.py -v` — 8/8 pass. These use mocks and do not validate hooks or injection. | Proven |
| LEP native suites | `scripts/build-lep.ps1 -Test`: `run-public-api`, `run-timezone`, `run-lepb-compat`, `run-broker-handle` all exit 0 after the integration patches. | Proven |
| LEP preflight / injection | `--selftest` passes for UTC/en-US and Tokyo/ja-JP: dynamic/static time zone, locale, ACP/OEMCP, UI language, preferred languages, LEP log and Job in parent and child. | Proven |
| Claude main/renderer/GPU/utility/worker | Under LEP: 7 processes in the Job, `--lang=ja` on renderers, `--enable-sandbox`, LEP injection/log evidence, no fail-closed; Stop/main-exit/supervisor-kill leave 0. | Proven at process level |
| Chromium proxy baseline | Direct Edge 149 headless: 12 processes at sample time (15 at Stop), renderer/Worker `Asia/Shanghai`/`zh-CN`; Stop and supervisor kill leave 0. See `chromium-probe-baseline-2026-10-29.md`. | Proven (direct mode) |
| Electron `Intl`/`navigator` | Under LEP, Edge proxy reports `UTC`/`en-US` and `Asia/Tokyo`/`ja-JP` in renderer and Worker with `--lang`; ClaudeProtect appends `--lang=<Locale>` to Claude. Claude's own JS is not directly observable (CDP blocked). See `lep-verification-2026-10-29.md`. | Proxy proven; Claude JS inferred |
| Chromium sandbox compatibility | Claude renderers run with `--enable-sandbox` under LEP with verified injection/logs; Edge proxy uses the default sandbox and no `--no-sandbox`. No sandbox was disabled. | Proven at process level |

## Toolchain

- MinGW-w64 GCC 8.1 builds the tray and probe.
- LEP is built with MSVC 14.51.36231 and Windows SDK 10.0.26100.0. `scripts/build-lep.ps1` discovers both with `vswhere`, creates `build/sdk-overlay` when the WDK `km` directory is absent, and copies x64/x86 DLLs to `build\lep`.
- The overlay supplies two minimal compatibility headers, `ntnls.h` and `mountmgr.h`, because Core includes those WDK headers but does not otherwise use WDK code. They are shims in `scripts/`, not copied third-party headers.
- Build and test:

```powershell
pwsh -NoProfile -File scripts/build-lep.ps1 -Test
pwsh -NoProfile -File scripts/build.ps1
build\ClaudeProtect.exe --selftest
```

- Upstream `run-public-api`, `run-timezone`, `run-lepb-compat`, and `run-broker-handle` all pass after the integration patches.

## Upstream integration patches

Published in [the LEP integration patch](../patches/lep/README.md) and applied to `LocaleEmulatorPlus-Core` by `scripts/prepare-lep.ps1`; public LEPB v4 is unchanged. After preparation, details are available in `LocaleEmulatorPlus-Core/ai-docs/2026-10-29-claude-protect-integration.md` and `ai-docs/claude-language-virtualization.md`.

- `SystemDynamicTimeZoneInformation` returns the configured dynamic time zone, with the Windows key name supplied by the launcher through `CP_TIMEZONE` (set by `Session::start` and the probe).
- `GetACP`/`GetOEMCP` exports return configured code pages.
- `HookUILanguageMode >= 1` installs the `GetUserDefaultUILanguage`/`GetSystemDefaultUILanguage` export hooks.
- `GetUserPreferredUILanguages`/`GetSystemPreferredUILanguages` return the configured culture; `RtlGetThreadPreferredUILanguages` is overridden in virtual MUI mode.
- LEP Core is manually mapped and invisible to module enumeration; runtime verification uses `create edge ... injectStatus=00000000` records and `GlobalData.Initialize end status=00000000` logs.

## Electron observation procedure

Claude Desktop blocks `--remote-debugging-port`, `--remote-debugging-pipe`, and `--inspect`; packaged `NODE_OPTIONS=--require` is ignored. `scripts/electron-observations.ps1` is retained for builds where CDP is available, but the working renderer/Worker probe is the Edge headless Chromium proxy:

```powershell
pwsh -NoProfile -File scripts/chromium-observations.ps1 -Seconds 20 `
  -ExtraArgs '--lang=ja-JP' -ExpectLocale ja-JP -ExpectTimezone Asia/Tokyo
pwsh -NoProfile -File scripts/chromium-observations.ps1 -Seconds 20 -KillSupervisor
pwsh -NoProfile -File scripts/claude-observations.ps1 -Seconds 20 -ExpectLang ja-JP
# Or, temporarily set Tokyo/ja-JP, run self-test plus both observations, restore INI:
pwsh -NoProfile -File scripts/test-lep.ps1 -RunChromium -RunClaude
```

It launches a separate-profile Edge under ClaudeProtect (use `-Direct` for the host baseline), evaluates `Intl.DateTimeFormat().resolvedOptions()`, `Intl.NumberFormat().resolvedOptions()`, `navigator.language(s)`, and January/July offsets in the page and a Web Worker, prints process `--lang` fields, and verifies cleanup. `-KillSupervisor` kills the supervisor to verify kill-on-close. `test-lep.ps1` adds `--lang=<Locale>` automatically for the Chromium proxy; ClaudeProtect appends the same switch to Claude Desktop.

Claude itself is observed through its process tree, renderer/utility command-line `--lang`, LEP injection/log evidence, and the native probe. Historical host values are recorded in `lep-verification-2026-10-29.md`; the current thread/WinRT patch and verification limits are described in `fixes-2026-11-02.md`.
