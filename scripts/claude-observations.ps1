param(
    [int]$Seconds = 20,
    [switch]$Direct,
    [switch]$KillSupervisor,
    [switch]$NoPreflight,
    [string]$ExpectLang,
    [string[]]$ExtraArgs
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ClaudeProtect.exe'
$log = Join-Path $build 'ClaudeProtect.log'

if (-not $Direct -and -not (Test-Path (Join-Path $build 'lep\LoaderDll_x64.dll'))) {
    throw 'LEP DLLs are missing. Build LocaleEmulatorPlus-Core first (scripts/build-lep.ps1), or use -Direct for a baseline.'
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ClaudeProtectJobCheck {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
    public static bool InJob(uint pid) {
        IntPtr handle = OpenProcess(0x1000, false, pid);
        if (handle == IntPtr.Zero) return false;
        try {
            bool result;
            return IsProcessInJob(handle, IntPtr.Zero, out result) && result;
        } finally {
            CloseHandle(handle);
        }
    }
}
"@

function Get-ClaudeDesktopProcesses {
    Get-CimInstance Win32_Process -Filter "Name='claude.exe' OR Name='chrome-native-host.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ExecutablePath -and
            ($_.ExecutablePath -like '*WindowsApps\Claude_*' -or
             $_.ExecutablePath -like '*AnthropicClaude\app-*' -or
             $_.ExecutablePath -like '*Programs\Claude\*')
        }
}

function Get-CoreModules([int]$ProcessId) {
    try {
        $process = Get-Process -Id $ProcessId -ErrorAction Stop
        return @($process.Modules | Where-Object { $_.ModuleName -like 'LocaleEmulatorPlus_*' } |
            ForEach-Object { $_.ModuleName })
    } catch {
        return @('<unavailable>')
    }
}

function Get-LepVerifiedPids {
    $dir = Join-Path $build 'lep\Log'
    $verified = @{}
    if (-not (Test-Path $dir)) { return @() }
    foreach ($file in Get-ChildItem $dir -File -Filter '*.log.txt' -ErrorAction SilentlyContinue) {
        $text = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $text) { continue }
        foreach ($match in [regex]::Matches($text, 'create edge[^\r\n]*childPid=(\d+)[^\r\n]*injectStatus=0+')) {
            $verified[[int]$match.Groups[1].Value] = $true
        }
        foreach ($match in [regex]::Matches($text, 'child injection complete pid=(\d+) status=0+')) {
            $verified[[int]$match.Groups[1].Value] = $true
        }
        if ($text -match 'GlobalData.Initialize end status=00000000') {
            $match = [regex]::Match($file.Name, '\.([0-9A-Fa-f]+)\.log\.txt$')
            if ($match.Success) {
                $verified[[Convert]::ToInt32($match.Groups[1].Value, 16)] = $true
            }
        }
    }
    return @($verified.Keys)
}

$existing = @(Get-ClaudeDesktopProcesses)
if ($existing.Count -gt 0) {
    throw "Claude Desktop is already running (pid $($existing.ProcessId -join ',')). Quit it first."
}

$arguments = @('--run', '--seconds', "$Seconds")
if ($Direct) { $arguments += '--direct' }
if ($NoPreflight -and -not $Direct) { $arguments += '--no-preflight' }
if ($ExtraArgs) { $arguments += @('--') + $ExtraArgs }
Remove-Item -Force $log -ErrorAction SilentlyContinue
Write-Host "Starting Claude Desktop ($(if ($Direct) { 'direct' } else { 'LEP' })) for $Seconds seconds"
$supervisor = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru

$observed = @()
for ($i = 0; $i -lt 60; $i++) {
    $observed = @(Get-ClaudeDesktopProcesses)
    if ($observed.Count -gt 0) { break }
    if ($supervisor.HasExited) { break }
    Start-Sleep -Milliseconds 500
}
if ($observed.Count -eq 0) {
    $tail = if (Test-Path $log) { Get-Content $log -Tail 40 | Out-String } else { '' }
    if (-not $supervisor.HasExited) { Stop-Process -Id $supervisor.Id -Force -ErrorAction SilentlyContinue }
    throw "Claude Desktop did not start.`n$tail"
}

Start-Sleep -Seconds 5
$observed = @(Get-ClaudeDesktopProcesses)
$lepPids = @(Get-LepVerifiedPids)
Write-Host 'Claude Desktop processes:'
$missingCore = @()
$outsideJob = @()
$langMismatch = @()
foreach ($process in $observed) {
    if (-not (Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue)) { continue }
    $commandLine = [string]$process.CommandLine
    $type = if ($commandLine -match '--type=([^ ]+)') { $matches[1] } else { 'main' }
    $lang = if ($commandLine -match '--lang=([^ ]+)') { $matches[1] } else { '' }
    $preferred = if ($commandLine -match 'preferredSystemLanguages.*?\[([^\]]*)\]') { $matches[1] } else { '' }
    $sandbox = if ($commandLine -match '--no-sandbox') { 'no-sandbox' } elseif ($commandLine -match '--enable-sandbox') { 'enable-sandbox' } else { 'default' }
    $core = Get-CoreModules $process.ProcessId
    $lepVerified = $lepPids -contains [int]$process.ProcessId
    $inJob = [ClaudeProtectJobCheck]::InJob([uint32]$process.ProcessId)
    if (-not $Direct -and -not $lepVerified -and
        ($core.Count -eq 0 -or $core[0] -eq '<unavailable>')) {
        $missingCore += $process.ProcessId
    }
    if (-not $Direct -and -not $inJob) {
        $outsideJob += $process.ProcessId
    }
    if ($ExpectLang) {
        $primary = ($ExpectLang -split '-')[0]
        if ($lang -and $lang -ne $ExpectLang -and $lang -ne $primary -and
            -not $lang.StartsWith($primary, [StringComparison]::OrdinalIgnoreCase)) {
            $langMismatch += $process.ProcessId
        }
    }
    Write-Host ("  pid={0} ppid={1} type={2} lang={3} sandbox={4} inJob={5} core={6} lepLog={7} preferred=[{8}]" -f `
        $process.ProcessId, $process.ParentProcessId, $type, $lang, $sandbox, $inJob,
        ($(if ($core.Count) { $core -join ',' } else { 'none' })), $lepVerified, $preferred)
}

if (-not $Direct -and $missingCore.Count -gt 0) {
    Write-Warning "Processes without a visible LEP Core module: $($missingCore -join ',')"
}
if (-not $Direct -and $outsideJob.Count -gt 0) {
    Write-Warning "Claude Desktop processes outside the managed Job: $($outsideJob -join ',')"
}
if ($ExpectLang -and $langMismatch.Count -gt 0) {
    Write-Warning "Processes whose language fields do not match ${ExpectLang}: $($langMismatch -join ',')"
}

if ($KillSupervisor) {
    Write-Host "Killing supervisor pid $($supervisor.Id) to test kill-on-close"
    Stop-Process -Id $supervisor.Id -Force
    Start-Sleep -Seconds 2
    $remaining = @(Get-ClaudeDesktopProcesses)
    if ($remaining.Count -gt 0) {
        throw "Claude Desktop processes survived supervisor termination: $($remaining.ProcessId -join ',')"
    }
    Write-Host 'No Claude Desktop processes remain after supervisor termination.'
} else {
    $supervisor.WaitForExit(($Seconds + 30) * 1000) | Out-Null
    Start-Sleep -Seconds 1
    $remaining = @(Get-ClaudeDesktopProcesses)
    if ($remaining.Count -gt 0) {
        Write-Warning "Claude Desktop processes remain: $($remaining.ProcessId -join ',')"
    } else {
        Write-Host 'No Claude Desktop processes remain.'
    }
}

Write-Host 'Supervisor log tail:'
$logText = ''
if (Test-Path $log) {
    $logText = Get-Content $log -Raw
    Get-Content $log -Tail 30
}

if ($logText -match 'FAIL-CLOSED') {
    Write-Warning 'Supervisor reported a fail-closed injection verification result.'
}
if (-not $Direct -and ($missingCore.Count -gt 0 -or $outsideJob.Count -gt 0)) { exit 1 }
if ($ExpectLang -and $langMismatch.Count -gt 0) { exit 1 }
if ($logText -match 'FAIL-CLOSED') { exit 1 }
exit 0
