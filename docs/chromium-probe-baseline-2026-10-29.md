# Chromium probe baseline (2026-10-29)

## Why a proxy

Claude Desktop 4.29.0 (Electron 44.9.3) rejects `--remote-debugging-port`, `--remote-debugging-pipe`, and `--inspect`; the main process exits with code 1 or ignores the switch. Packaged-app `NODE_OPTIONS=--require=...` is also ignored. Copying Claude's `claude.exe` into a separate Electron runtime and loading a minimal app also exited with code 1, so the installed binary is not a general-purpose Electron launcher. Claude's renderer therefore cannot be evaluated through CDP without modifying installed files.

A Chromium proxy is used for renderer/Worker observations. It is not Claude's renderer, but it exercises the same Chromium/ICU/network stack and the supervisor Job Object. Claude itself is still checked through its process tree, command-line language fields, and native probe.

## Environment

- Host: `China Standard Time`, `zh-CN`.
- Browser: Microsoft Edge 149.0.4022.52 (headless, separate `--user-data-dir`, sandbox left at browser defaults).
- Supervisor: `build\ClaudeProtect.exe --run --direct --seconds 15 --target <msedge.exe> -- --headless=new --remote-debugging-port=9224 --user-data-dir=... about:blank`.
- Direct mode: no LEP.

## Observed processes

At sample time the Job held 12 processes: main, crashpad handler, GPU process, two utility processes, and seven renderers. The supervisor reported 15 active processes at Stop as additional renderers spawned. Utility and renderer command lines carried `--lang=zh-CN`. No process carried `--no-sandbox`; the renderers used the browser's default sandbox.

## Renderer and Worker observations

```json
{
  "main": {
    "tz": "Asia/Shanghai",
    "locale": "zh-CN",
    "numberLocale": "zh-CN",
    "languages": ["zh-CN", "en", "en-GB", "en-US"],
    "language": "zh-CN",
    "offsetJan": -480,
    "offsetJul": -480
  },
  "worker": {
    "tz": "Asia/Shanghai",
    "locale": "zh-CN",
    "numberLocale": "zh-CN",
    "languages": ["zh-CN", "en", "en-GB", "en-US"],
    "language": "zh-CN",
    "offsetJan": -480,
    "offsetJul": -480
  }
}
```

`getTimezoneOffset()` is minutes behind UTC, so -480 means UTC+8. The January/July values show no DST for this zone.

## Cleanup

Supervisor logged `Active processes before stop: 15`, then `managed job closed`; a query filtered by the probe profile returned no remaining processes.

## Chrome note

Chrome 156 headless works outside a Job Object and exposes CDP, but exits with code 13 when assigned to the supervisor Job Object. Edge 149 does not. The proxy script therefore prefers Edge and falls back to Chrome. This is a browser behavior, not a ClaudeProtect claim.

## LEP result (2026-10-29)

With LEP built and `Timezone=Tokyo Standard Time`, `Locale=ja-JP`, launched with `--lang=ja-JP`:

```powershell
pwsh -NoProfile -File scripts/chromium-observations.ps1 -Seconds 15 -ExtraArgs '--lang=ja-JP'
```

Renderer and Worker both reported:

```text
tz=Asia/Tokyo, locale=ja-JP, numberLocale=ja-JP,
languages=[ja,en,en-GB,en-US], language=ja,
offsetJan=-540, offsetJul=-540
```

Utility/renderer command lines carried `--lang=ja`; no process had `--no-sandbox`; the supervisor did not fail-closed, and no probe processes remained. With UTC/en-US and `--lang=en-US`, both reported `UTC`/`en-US` and zero offsets.

The Edge proxy is not Claude's renderer, but it uses the same Chromium/ICU stack and the same LEP hooks. Claude itself is observed through its process tree, `--lang` propagation and LEP injection/log evidence; see `lep-verification-2026-10-29.md`.
