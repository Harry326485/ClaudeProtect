# Claude Desktop process tree baseline (2026-10-29)

## Purpose

Record the unvirtualized Claude Desktop process tree and supervisor cleanup before testing LEP. This is a **direct-mode diagnostic** (`--run --direct`): no time zone or locale virtualization was active. Host settings: `China Standard Time`, `zh-CN`, ACP/OEMCP 936.

## Observed run

Command:

```powershell
build\ClaudeProtect.exe --run --direct --seconds 8
```

The target was discovered dynamically:

```text
C:\Program Files\WindowsApps\Claude_4.29.0.0_x64__pzs8sxrjxfjjc\app\claude.exe
```

At t=4s the Job contained 7 processes. Main process PID 76744, parent PID 1668 (ClaudeProtect). Roles observed from command lines:

| Role | Evidence |
| --- | --- |
| Main | `claude.exe` with no `--type`. |
| Crashpad handler | `--type=crashpad-handler --database=%APPDATA%\Claude\Crashpad`. |
| GPU | `--type=gpu-process --gpu-preferences=...`. |
| Network utility | `--type=utility --utility-sub-type=network.mojom.NetworkService --lang=zh-CN`. |
| Renderers | `--type=renderer --enable-sandbox --lang=zh-CN --device-scale-factor=1.5 ...`. |
| Renderer/desktop config | `--desktop-managed-config={... "preferredSystemLanguages":["zh-Hans-CN","en-US"] ...}`. |

The renderer keeps Chromium's sandbox enabled (`--enable-sandbox`). Any LEP test must preserve this; do not disable the sandbox to force a pass.

## Cleanup

At t=8s the supervisor logged:

```text
Active processes before stop: 7
stopping job reason=cli-run-finished pid=76744 active=7
managed job closed
```

A post-exit query for `claude.exe` with an executable path under `WindowsApps\Claude_*` returned `remaining=0`.

## Consequences for LEP verification

1. The MSIX executable can be launched directly with `CreateProcessW`; a package-activation launcher is not required for this build.
2. The Job Object covers main, crashpad, GPU, network utility and renderer processes; Stop terminates all of them.
3. The main process passes `--lang=zh-CN` and a `preferredSystemLanguages` array to children. Under LEP, both should reflect the configured locale. If they still show the host locale, the language hooks do not cover the path used by Claude.
4. The renderer runs with `--enable-sandbox`; verify LEP injection and `Intl`/`navigator` observations with the sandbox enabled.
5. Crashpad may be short-lived or respawn; module verification exempts no crashpad process, so a crashpad child that lives past the grace period without LEP Core would be treated as an injection failure.
6. Direct mode is a diagnostic path only. The tray always uses `LepCreateProcess2`.

## LEP follow-up (2026-10-29)

With LEP built and `Timezone=Tokyo Standard Time`, `Locale=ja-JP`, `claude-observations.ps1` observed 7 processes in the Job: main `--lang=ja-JP`, crashpad, GPU, utility `--lang=ja`, and three renderers with `--lang=ja` and `--enable-sandbox`. Every process had LEP injection/log evidence; no fail-closed; Stop, main-exit and supervisor-kill left 0 processes. ClaudeProtect appends `--lang=<Locale>` automatically. Claude's `preferredSystemLanguages` app field remained the host list; see `lep-verification-2026-10-29.md`.

## Still unproven / residual

- Claude's own renderer JavaScript cannot be inspected directly (CDP and debugging switches are blocked); `Intl`/`navigator` values are inferred from the Edge Chromium proxy under the same LEP hooks.
- Claude's `preferredSystemLanguages` app field still shows the host list even though native `GetUserPreferredUILanguages` returns the configured culture.
- System locale/LCID and native `GetLocaleInfoEx` date formatting remain host.
- Long-running Claude sessions, features that spawn additional helpers, and external security-product helpers need ongoing observation.
