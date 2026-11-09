param([switch]$SkipBuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ClaudeProtect.exe'
$probe = Join-Path $build 'EnvironmentProbe.exe'
$probeLog = Join-Path $build 'probe.log'
$log = Join-Path $build 'ClaudeProtect.log'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-Log {
    if (Test-Path $log) { return Get-Content $log -Raw } else { return '' }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') | Out-Host
}

Assert-True (Test-Path $exe) "Missing $exe"
Assert-True (Test-Path $probe) "Missing $probe"

$configOutput = ''
Remove-Item -Force $log -ErrorAction SilentlyContinue
& $exe --print-config | Out-Null
if (Test-Path $log) { $configOutput = Get-Content $log -Raw }
Assert-True ($configOutput -match 'LEPB size=416') 'LEPB v4 layout size is not 416'
Assert-True ($configOutput -match 'timezone=') 'Resolved timezone missing from --print-config'
Assert-True ($configOutput -match '(?m)^locale=([^\r\n]+)') 'Resolved locale missing from --print-config'
$posixLocale = $Matches[1].Replace('-', '_') + '.UTF-8'
Assert-True ($configOutput -match '(?m)^childTZ=([^\r\n]*)') 'Child TZ missing from --print-config'
$posixTimezone = $Matches[1]

$timezone = (Get-TimeZone).Id
$culture = (Get-Culture).Name
$acp = [int](Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP

Write-Host "Probe baseline: timezone=$timezone locale=$culture acp=$acp"
& $probe --spawn-child --json --expect-timezone $timezone --expect-locale $culture --expect-acp $acp | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "Probe baseline failed with exit $LASTEXITCODE"

Write-Host 'Probe mismatch detection'
& $probe --expect-timezone 'Tokyo Standard Time' --expect-locale 'ja-JP' | Out-Null
Assert-True ($LASTEXITCODE -eq 2) "Probe mismatch should exit 2, got $LASTEXITCODE"

Write-Host 'Direct mode: Stop terminates the managed job'
Remove-Item -Force $log -ErrorAction SilentlyContinue
Remove-Item -Force $probeLog -ErrorAction SilentlyContinue
& $exe --run --direct --seconds 2 --target $probe -- --seconds 60 | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "Direct run failed with exit $LASTEXITCODE"
$observation = Get-Content $probeLog -Tail 1 | ConvertFrom-Json
Assert-True ($observation.envLANG -ceq $posixLocale) 'LANG is not a UTF-8 POSIX locale'
Assert-True ($observation.envLC_ALL -ceq $posixLocale) 'LC_ALL is not a UTF-8 POSIX locale'
Assert-True ($observation.envTZ -ceq $posixTimezone) 'TZ does not match the configured POSIX rules'
Start-Sleep -Milliseconds 500
$left = @(Get-Process -Name EnvironmentProbe -ErrorAction SilentlyContinue)
Assert-True ($left.Count -eq 0) "Probe survived Stop: $($left.Id -join ',')"
$text = Read-Log
Assert-True ($text -match 'managed job closed') 'Log does not record managed job close'

Write-Host 'Direct mode: Stop terminates a managed process tree'
Remove-Item -Force $log -ErrorAction SilentlyContinue
Remove-Item -Force $probeLog -ErrorAction SilentlyContinue
& $exe --run --direct --seconds 2 --target $probe -- --spawn-child --child-seconds 60 | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "Direct tree run failed with exit $LASTEXITCODE"
$childObservation = Get-Content $probeLog -Tail 1 | ConvertFrom-Json
Assert-True ($childObservation.envLANG -ceq $posixLocale) 'Child LANG is not a UTF-8 POSIX locale'
Assert-True ($childObservation.envLC_ALL -ceq $posixLocale) 'Child LC_ALL is not a UTF-8 POSIX locale'
Assert-True ($childObservation.envTZ -ceq $posixTimezone) 'Child TZ does not match the configured POSIX rules'
Start-Sleep -Milliseconds 500
$left = @(Get-Process -Name EnvironmentProbe -ErrorAction SilentlyContinue)
Assert-True ($left.Count -eq 0) "Probe tree survived Stop: $($left.Id -join ',')"
$text = Read-Log
Assert-True ($text -match 'managed job closed') 'Log does not record managed job close (tree)'

Write-Host 'Direct mode: main exit cleans up remaining job members'
Remove-Item -Force $log -ErrorAction SilentlyContinue
& $exe --run --direct --seconds 10 --target $probe -- --seconds 1 | Out-Null
Assert-True ($LASTEXITCODE -eq 0) "Direct run failed with exit $LASTEXITCODE"
$text = Read-Log
Assert-True ($text -match 'main process exited') 'Log does not record main process exit'
Assert-True ($text -match 'managed job closed') 'Log does not record job close after main exit'

Write-Host 'Supervisor termination triggers kill-on-close'
$supervisor = Start-Process -FilePath $exe -ArgumentList @(
    '--run', '--direct', '--seconds', '60', '--target', ('"' + $probe + '"'), '--', '--seconds', '60'
) -PassThru
Start-Sleep -Seconds 2
Assert-True (@(Get-Process -Name EnvironmentProbe -ErrorAction SilentlyContinue).Count -gt 0) `
    'Probe did not start under supervisor'
Stop-Process -Id $supervisor.Id -Force
Start-Sleep -Seconds 2
$left = @(Get-Process -Name EnvironmentProbe -ErrorAction SilentlyContinue)
Assert-True ($left.Count -eq 0) "Probe survived supervisor termination: $($left.Id -join ',')"

Write-Host 'Unmanaged Claude Desktop refusal (path-based, Claude Code ignored)'
$fakeDir = Join-Path $build 'test-tmp\WindowsApps\Claude_0.0.0.0_x64__test\app'
New-Item -ItemType Directory -Force $fakeDir | Out-Null
$fake = Join-Path $fakeDir 'claude.exe'
Copy-Item $probe $fake -Force
$fakeProcess = Start-Process -FilePath $fake -ArgumentList '--seconds','30' -PassThru
try {
    Remove-Item -Force $log -ErrorAction SilentlyContinue
    & $exe --check | Out-Null
    $text = Read-Log
    Assert-True ($text -match "Unmanaged Claude Desktop running: pid $($fakeProcess.Id)") `
        'Unmanaged fake Claude Desktop was not detected'

    Remove-Item -Force $log -ErrorAction SilentlyContinue
    & $exe --run --direct --seconds 1 --target $fake | Out-Null
    Assert-True ($LASTEXITCODE -eq 1) "Expected refusal exit 1, got $LASTEXITCODE"
    $text = Read-Log
    Assert-True ($text -match 'unmanaged Claude Desktop is already running') `
        'Refusal reason missing from log'
} finally {
    Stop-Process -Id $fakeProcess.Id -Force -ErrorAction SilentlyContinue
}

if (Test-Path (Join-Path $build 'lep\LoaderDll_x64.dll')) {
    Write-Host 'LEP self-test'
    & $exe --selftest | Out-Host
    Assert-True ($LASTEXITCODE -eq 0) "LEP self-test failed with exit $LASTEXITCODE"
} else {
    Write-Host 'LEP DLLs are absent; skipping LEP self-test.'
}

Write-Host 'All supervisor tests passed.'
