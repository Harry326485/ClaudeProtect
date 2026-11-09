param(
    [int]$Port = 9224,
    [int]$Seconds = 20,
    [switch]$KillSupervisor,
    [switch]$Direct,
    [switch]$NoPreflight,
    [string]$Browser,
    [string[]]$ExtraArgs,
    [string]$ExpectLocale,
    [string]$ExpectTimezone
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ClaudeProtect.exe'
$log = Join-Path $build 'ClaudeProtect.log'
$profile = Join-Path $build 'chromium-probe-profile'
$result = Join-Path $build 'chromium-observations.json'

if (-not $Direct -and -not (Test-Path (Join-Path $build 'lep\LoaderDll_x64.dll'))) {
    throw 'LEP DLLs are missing. Build LocaleEmulatorPlus-Core first (scripts/build-lep.ps1), or use -Direct for a host baseline.'
}

if (-not $Browser) {
    # Edge currently tolerates the supervisor Job Object; Chrome 156 exits
    # with code 13 when assigned to it. Prefer Edge, fall back to Chrome.
    $Browser = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
    if (-not (Test-Path $Browser)) { $Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe' }
}
if (-not (Test-Path $Browser)) { throw 'No Chrome or Edge executable found.' }

function Get-ProbeProcesses {
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe' OR Name='msedge.exe' OR Name='msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($profile) }
}

function Invoke-CdpEval {
    param([string]$WebSocketUrl, [string]$Expression)
    $ws = [System.Net.WebSockets.ClientWebSocket]::new()
    try {
        $ws.ConnectAsync([Uri]$WebSocketUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        $payload = @{
            id = 1
            method = 'Runtime.evaluate'
            params = @{ expression = $Expression; returnByValue = $true; awaitPromise = $true }
        } | ConvertTo-Json -Depth 8 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
        $ws.SendAsync([ArraySegment[byte]]::new($bytes),
                      [Net.WebSockets.WebSocketMessageType]::Text, $true,
                      [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        $buffer = New-Object byte[] 131072
        $builder = [Text.StringBuilder]::new()
        while ($true) {
            $received = $ws.ReceiveAsync([ArraySegment[byte]]::new($buffer),
                                         [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($received.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
            [void]$builder.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $received.Count))
            if ($received.EndOfMessage) {
                $text = $builder.ToString()
                [void]$builder.Clear()
                $message = $text | ConvertFrom-Json
                if ($message.id -eq 1) { return $message }
            }
        }
    } finally {
        $ws.Dispose()
    }
    throw 'CDP connection closed without a response'
}

Remove-Item -Recurse -Force $profile -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $profile | Out-Null
Remove-Item -Force $result -ErrorAction SilentlyContinue
Remove-Item -Force $log -ErrorAction SilentlyContinue

$browserArgs = @(
    '--headless=new',
    "--remote-debugging-port=$Port",
    ('--user-data-dir="' + $profile + '"'),
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-background-networking',
    'about:blank'
)
if ($ExtraArgs) { $browserArgs = @($ExtraArgs) + $browserArgs }
$quotedBrowser = '"' + $Browser + '"'
$supervisorArgs = @('--run', '--seconds', "$Seconds", '--target', $quotedBrowser, '--') + $browserArgs
if ($Direct) {
    $supervisorArgs = @('--run', '--direct', '--seconds', "$Seconds", '--target', $quotedBrowser, '--') + $browserArgs
}
if ($NoPreflight -and -not $Direct) {
    $supervisorArgs = @('--run', '--no-preflight', '--seconds', "$Seconds", '--target', $quotedBrowser, '--') + $browserArgs
}

Write-Host "Starting Chromium probe: $Browser"
Write-Host "Mode: $(if ($Direct) { 'direct (no LEP)' } else { 'LEP' }); profile: $profile"
$supervisor = Start-Process -FilePath $exe -ArgumentList $supervisorArgs -PassThru

$target = $null
for ($i = 0; $i -lt 120; $i++) {
    if ($supervisor.HasExited) { break }
    try {
        $targets = Invoke-RestMethod "http://127.0.0.1:$Port/json/list" -TimeoutSec 2
        $target = $targets | Where-Object { $_.type -eq 'page' -and $_.webSocketDebuggerUrl } |
            Select-Object -First 1
        if ($target) { break }
    } catch {
        # The port is not ready yet.
    }
    Start-Sleep -Milliseconds 500
}

try {
    if (-not $target) {
        $tail = if (Test-Path $log) { Get-Content $log -Tail 40 | Out-String } else { '' }
        throw "No CDP page target on port $Port.`n$tail"
    }

    Write-Host 'Managed Chromium process language fields:'
    foreach ($process in @(Get-ProbeProcesses)) {
        $commandLine = [string]$process.CommandLine
        $type = if ($commandLine -match '--type=([^ ]+)') { $matches[1] } else { 'main' }
        $lang = if ($commandLine -match '--lang=([^ ]+)') { $matches[1] } else { '' }
        $sandbox = if ($commandLine -match '--no-sandbox') { 'no-sandbox' } elseif ($commandLine -match '--enable-sandbox') { 'enable-sandbox' } else { 'default' }
        Write-Host ("  pid={0} type={1} lang={2} sandbox={3}" -f $process.ProcessId, $type, $lang, $sandbox)
    }

    $mainExpression = @'
JSON.stringify({
  tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
  locale: Intl.DateTimeFormat().resolvedOptions().locale,
  numberLocale: new Intl.NumberFormat().resolvedOptions().locale,
  languages: navigator.languages,
  language: navigator.language,
  offsetJan: new Date(2026, 0, 15).getTimezoneOffset(),
  offsetJul: new Date(2026, 6, 15).getTimezoneOffset(),
  userAgent: navigator.userAgent
})
'@

    $workerExpression = @'
new Promise((resolve) => {
  const source = `postMessage(JSON.stringify({
    tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
    locale: Intl.DateTimeFormat().resolvedOptions().locale,
    numberLocale: new Intl.NumberFormat().resolvedOptions().locale,
    languages: navigator.languages,
    language: navigator.language,
    offsetJan: new Date(2026, 0, 15).getTimezoneOffset(),
    offsetJul: new Date(2026, 6, 15).getTimezoneOffset()
  }))`;
  const url = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
  const worker = new Worker(url);
  worker.onmessage = (event) => resolve(event.data);
  worker.onerror = (event) => resolve(JSON.stringify({ error: String(event.message || event) }));
})
'@

    $main = Invoke-CdpEval -WebSocketUrl $target.webSocketDebuggerUrl -Expression $mainExpression
    $worker = Invoke-CdpEval -WebSocketUrl $target.webSocketDebuggerUrl -Expression $workerExpression
    $observations = [ordered]@{
        mode = if ($Direct) { 'direct' } else { 'lep' }
        browser = $Browser
        main = $main.result.result.value | ConvertFrom-Json
        worker = $worker.result.result.value | ConvertFrom-Json
    }
    $observations | ConvertTo-Json -Depth 8 | Tee-Object -FilePath $result
    Write-Host "Observations written to $result"

    $mismatches = @()
    if ($ExpectLocale) {
        if ($observations.main.locale -ne $ExpectLocale) {
            $mismatches += "renderer locale $($observations.main.locale) != $ExpectLocale"
        }
        if ($observations.worker.locale -ne $ExpectLocale) {
            $mismatches += "worker locale $($observations.worker.locale) != $ExpectLocale"
        }
    }
    if ($ExpectTimezone) {
        if ($observations.main.tz -ne $ExpectTimezone) {
            $mismatches += "renderer timezone $($observations.main.tz) != $ExpectTimezone"
        }
        if ($observations.worker.tz -ne $ExpectTimezone) {
            $mismatches += "worker timezone $($observations.worker.tz) != $ExpectTimezone"
        }
    }
    if ($mismatches.Count -gt 0) {
        throw ('Chromium observations did not match expectations: ' + ($mismatches -join '; '))
    }

    if ($KillSupervisor) {
        Write-Host "Killing supervisor pid $($supervisor.Id) to test kill-on-close"
        Stop-Process -Id $supervisor.Id -Force
        Start-Sleep -Seconds 2
        $remaining = @(Get-ProbeProcesses)
        if ($remaining.Count -gt 0) {
            throw "Chromium processes survived supervisor termination: $($remaining.ProcessId -join ',')"
        }
        Write-Host 'No probe Chromium processes remain after supervisor termination.'
    } else {
        $supervisor.WaitForExit(($Seconds + 30) * 1000) | Out-Null
        Start-Sleep -Seconds 1
        $remaining = @(Get-ProbeProcesses)
        if ($remaining.Count -gt 0) {
            Write-Host "WARNING: probe Chromium processes remain: $($remaining.ProcessId -join ',')"
        } else {
            Write-Host 'No probe Chromium processes remain.'
        }
    }
    if (Test-Path $log) {
        $logText = Get-Content $log -Raw
        if ($logText -match 'FAIL-CLOSED') {
            throw 'Supervisor reported a fail-closed injection verification result.'
        }
    }
} finally {
    if (-not $supervisor.HasExited) {
        Stop-Process -Id $supervisor.Id -Force -ErrorAction SilentlyContinue
    }
}
