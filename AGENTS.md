# ClaudeProtect

Native Windows x64 tray supervisor for Claude Desktop. User-mode only; never change host timezone, locale, registry settings, or Claude's installed files.

## Architecture
- ClaudeProtect owns the tray UI, discovery/configuration, logs, diagnostics, and kill-on-close Job Objects.
- Backend: `LocaleEmulatorPlus-Core/`, a Git submodule pinned to `0b11f533ef426ab5606e12f91acd4b1c2f7e6bad`. Follow its own `AGENTS.md`; keep upstream changes isolated.
- Integration: dynamically load `LoaderDll_x64.dll`, resolve `LepCreateProcess2`, pass LEPB v4 plus `CREATE_SUSPENDED`, assign the returned process to our Job Object, then resume. Do not use `LepCreateProcess`, which loads Core into the supervisor.
- `src/lep_abi.hpp` is a copied standalone ABI declaration; static asserts enforce 416-byte LEPB and offsets 32/288/352.
- Source split: `src/lep.cpp` adapter, `src/config.cpp` INI/discovery/validation, `src/session.cpp` Job lifecycle/preflight/runtime verification, `src/main.cpp` tray/CLI, `src/editor.cpp` settings/log windows, `src/probe.cpp` native observations.
- Launch refusal is path-based: only an unmanaged **Claude Desktop** blocks a launch. Claude Code's npm `claude.exe` is explicitly ignored.
- Preflight runs `EnvironmentProbe.exe` under LEP (parent and child) before Claude. Runtime verification checks LEP injection/initialization records for live managed app processes and scans LEP logs for injection failures; `VerifyInjection=strict` stops the job on failure.
- Upstream child injection is still fail-open (logged, child continues). Our detection/containment is the mitigation, not upstream behavior.

## Commands
- `pwsh -NoProfile -File scripts/build.ps1` builds `build/ClaudeProtect.exe` and `build/EnvironmentProbe.exe` with MinGW; add `-BuildLep` to run the LEP build.
- `pwsh -NoProfile -File scripts/build-lep.ps1` requires MSVC + Windows SDK. If WDK `km` headers are absent it creates `build/sdk-overlay` with minimal `ntnls.h`/`mountmgr.h` shims for the only WDK declarations Core uses.
- `pwsh -NoProfile -File scripts/test-supervisor.ps1` checks probe expectations, Job cleanup, process-tree cleanup, main-exit cleanup, supervisor kill-on-close, and path-based refusal.
- `pwsh -NoProfile -File scripts/chromium-observations.ps1 [-Direct] [-ExtraArgs '--lang=ja-JP'] [-ExpectLocale ja-JP] [-ExpectTimezone Asia/Tokyo]` runs a separate-profile Edge headless under ClaudeProtect and reports renderer/Worker `Intl`/`navigator` plus process `--lang`/sandbox fields. Use `-KillSupervisor` for kill-on-close.
- `pwsh -NoProfile -File scripts/claude-observations.ps1 [-Direct] [-KillSupervisor] [-ExpectLang ja-JP]` samples Claude's main/renderer/GPU/utility processes, `--lang`, `preferredSystemLanguages`, LEP Core modules, and Job membership.
- `pwsh -NoProfile -File scripts/test-lep.ps1 [-RunChromium] [-RunClaude]` temporarily sets Tokyo/ja-JP (configurable), runs the native LEP self-test, optionally runs the Chromium proxy and Claude observations, then restores the INI.
- `pwsh -NoProfile -File scripts/electron-observations.ps1` is retained for CDP-capable builds; Claude Desktop currently blocks CDP.
- CLI: `build\ClaudeProtect.exe --check | --print-config | --edit-settings | --discover | --selftest | --run`.
- `pwsh -NoProfile -File scripts/clean.ps1 [-WhatIf]` removes generated caches/tests/logs while keeping runtime binaries, configuration/backups and SDK overlay junctions. Historical evidence is under `docs/verification/`.
- LEP upstream tests: `pwsh -NoProfile -File scripts/build-lep.ps1 -Test`.
- Publish the complete LEP integration delta in `patches/lep/claude-protect.patch`, including new files. `scripts/prepare-lep.ps1` applies it to the pinned submodule and is called by `build-lep.ps1`. A modified submodule after preparation is expected; the patch is the publishable source of these changes.
- Keep `build/`, runtime INI files, logs, dumps and credentials out of commits. ClaudeProtect-owned code is GPL-3.0-only; preserve third-party license terms and notices.

## Status / discoveries
- Tray and probe build with MinGW-w64 GCC 8.1. LEP Core/Loader build with MSVC 14.51 + Windows SDK 10.0.26100 via `scripts/build-lep.ps1`; the x86/x64 DLLs are in `build\lep`.
- Native LEP self-test passes for UTC/en-US and Tokyo/ja-JP: dynamic and static time zone, locale, ACP/OEMCP, UI language, preferred languages, Job membership and LEP initialization are all virtualized in the probe and its child.
- Chromium proxy under LEP: renderer and Worker report `UTC`/`en-US` and `Asia/Tokyo`/`ja-JP` as configured when launched with `--lang`; no fail-closed and no leftover processes. ClaudeProtect automatically appends `--lang=<Locale>` for Claude Desktop.
- Claude Desktop under LEP: 7 processes (main, crashpad, GPU, utility, renderers) are in the Job, renderers keep `--enable-sandbox`, `--lang=ja` is propagated, LEP injection/log evidence exists, and Stop, main exit and supervisor kill leave 0 processes.
- Upstream Core changes are isolated in the submodule and documented in `LocaleEmulatorPlus-Core/ai-docs/2026-10-29-claude-protect-integration.md`: dynamic time-zone case (`CP_TIMEZONE`), ACP/OEMCP export hooks, UI-language exports for mode 2, preferred-language exports. Public LEPB v4 layout is unchanged. Upstream four test suites pass.
- Mode 2 now covers thread preferred languages and WinRT `GlobalizationPreferences.Languages`; matching x86/x64 independent probes pass. Final WinRT patch has not been rechecked in a running Claude UI. `systemLocale`/system LCID and native `GetLocaleInfoEx` formatting remain host. See `docs/fixes-2026-11-02.md`; historical renderer JS evidence comes from the Edge proxy.
- Claude Desktop is discovered dynamically; the current MSIX install is `C:/Program Files/WindowsApps/Claude_4.29.0.0_x64__pzs8sxrjxfjjc/app/claude.exe`. Do not hardcode the version.
- LEP Core is manually mapped and invisible to `GetModuleHandle`/Toolhelp. Runtime verification uses LEP injection records and initialization logs instead of module presence. External helpers (e.g. 360 Safe) that fail injection are logged and ignored; managed app processes fail closed.
- Do not claim complete isolation beyond the evidence in `docs/lep-verification-2026-10-29.md`.
