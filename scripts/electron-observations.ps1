param(
    [int]$Port = 9222,
    [int]$Seconds = 30,
    [switch]$KillSupervisor,
    [switch]$Direct
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ClaudeProtect.exe'
$log = Join-Path $build 'ClaudeProtect.log'

if (-not $Direct -and -not (Test-Path (Join-Path $build 'lep\LoaderDll_x64.dll'))) {
    throw 'LEP DLLs are missing. Build LocaleEmulatorPlus-Core first (scripts/build-lep.ps1), or use -Direct for a host baseline.'
}

function Get-ClaudeDesktopProcesses {
    Get-CimInstance Win32_Process -Filter "Name='claude.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ExecutablePath -and
            ($_.ExecutablePath -match '\\WindowsApps\\Claude_' -or
             $_.ExecutablePath -match '\\AnthropicClaude\\app-' -or
             $_.ExecutablePath -match '\\Programs\\Claude\\')
        }
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
            $result = $ws.ReceiveAsync([ArraySegment[byte]]::new($buffer),
                                       [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
            [void]$builder.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count))
            if ($result.EndOfMessage) {
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

$existing = @(Get-ClaudeDesktopProcesses)
if ($existing.Count -gt 0) {
    throw "Claude Desktop is already running (pid $($existing.ProcessId -join ',')). Quit it first."
}

Write-Host "Starting managed Claude Desktop with remote debugging on port $Port for $Seconds seconds"
$supervisorArgs = @('--run', '--seconds', "$Seconds")
if ($Direct) { $supervisorArgs += '--direct' }
$supervisorArgs += @('--', "--remote-debugging-port=$Port")
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
        throw "No CDP page target on port $Port. Claude may not accept remote debugging.`n$tail"
    }

    Write-Host 'Managed process command-line language fields:'
    foreach ($process in @(Get-ClaudeDesktopProcesses)) {
        $commandLine = [string]$process.CommandLine
        $type = if ($commandLine -match '--type=([^ ]+)') { $matches[1] } else { 'main' }
        $lang = if ($commandLine -match '--lang=([^ ]+)') { $matches[1] } else { '' }
        $preferred = if ($commandLine -match 'preferredSystemLanguages.*?\[([^\]]*)\]') { $matches[1] } else { '' }
        Write-Host ("  pid={0} type={1} lang={2} preferredSystemLanguages=[{3}]" -f $process.ProcessId, $type, $lang, $preferred)
    }

    $mainExpression = @'
JSON.stringify({
  tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
  locale: Intl.DateTimeFormat().resolvedOptions().locale,
  numberLocale: new Intl.NumberFormat().resolvedOptions().locale,
  languages: navigator.languages,
  language: navigator.language,
  offsetJan: new Date(2026, 0, 15).getTimezoneOffset(),
  offsetJul: new Date(2026, 6, 15).getTimezoneOffset()
})
'@

    $workerExpression = @'
new Promise((resolve) => {
  const source = `postMessage(JSON.stringify({
    tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
    locale: Intl.DateTimeFormat().resolvedOptions().locale,
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

    Write-Host 'Renderer observations:'
    Write-Host $main.result.result.value
    Write-Host 'Worker observations:'
    Write-Host $worker.result.result.value

    if ($KillSupervisor) {
        Write-Host "Killing supervisor pid $($supervisor.Id) to test kill-on-close"
        Stop-Process -Id $supervisor.Id -Force
        Start-Sleep -Seconds 2
        $remaining = @(Get-ClaudeDesktopProcesses)
        if ($remaining.Count -gt 0) {
            throw "Managed Claude Desktop processes survived supervisor termination: $($remaining.ProcessId -join ',')"
        }
        Write-Host 'No Claude Desktop processes remain after supervisor termination.'
    } else {
        Write-Host 'Waiting for the supervisor to finish and clean up.'
        $supervisor.WaitForExit(($Seconds + 30) * 1000) | Out-Null
        Start-Sleep -Seconds 1
        $remaining = @(Get-ClaudeDesktopProcesses)
        if ($remaining.Count -gt 0) {
            Write-Host "WARNING: Claude Desktop processes remain: $($remaining.ProcessId -join ',')"
        } else {
            Write-Host 'No Claude Desktop processes remain.'
        }
    }
} finally {
    if (-not $supervisor.HasExited) {
        Stop-Process -Id $supervisor.Id -Force -ErrorAction SilentlyContinue
    }
}
