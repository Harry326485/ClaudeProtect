param(
    [string]$Timezone = 'Tokyo Standard Time',
    [string]$IanaTimezone = 'Asia/Tokyo',
    [string]$Locale = 'ja-JP',
    [int]$Seconds = 20,
    [switch]$RunChromium,
    [switch]$RunClaude
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$exe = Join-Path $build 'ClaudeProtect.exe'
$ini = Join-Path $build 'ClaudeProtect.ini'
$backup = Join-Path $build 'ClaudeProtect.ini.lep-test-backup'
$log = Join-Path $build 'ClaudeProtect.log'

if (-not (Test-Path (Join-Path $build 'lep\LoaderDll_x64.dll'))) {
    throw 'LEP DLLs are missing. Run scripts/build-lep.ps1 first.'
}
if (-not (Test-Path $exe)) { throw "Missing $exe. Run scripts/build.ps1 first." }

function Set-IniValue([string]$Path, [string]$Key, [string]$Value) {
    $text = Get-Content $Path -Raw
    $pattern = "(?m)^$([regex]::Escape($Key))=.*$"
    if ($text -match $pattern) {
        $text = [regex]::Replace($text, $pattern, "$Key=$Value")
    } else {
        $text = $text.TrimEnd() + "`r`n$Key=$Value`r`n"
    }
    Set-Content -Path $Path -Value $text -Encoding utf8
}

if (-not (Test-Path $ini)) { & $exe --check | Out-Null }
Copy-Item $ini $backup -Force
try {
    Set-IniValue $ini 'Timezone' $Timezone
    Set-IniValue $ini 'TimezoneIana' $IanaTimezone
    Set-IniValue $ini 'Locale' $Locale

    Remove-Item -Force $log -ErrorAction SilentlyContinue
    Write-Host "Native LEP self-test: timezone=$Timezone locale=$Locale"
    & $exe --selftest
    $selfTest = $LASTEXITCODE
    if ($null -eq $selfTest) { $selfTest = 0 }
    Write-Host "Self-test exit code: $selfTest"
    if (Test-Path $log) { Get-Content $log -Tail 40 }
    if ($selfTest -ne 0) { throw "LEP self-test failed with exit $selfTest" }

    if ($RunChromium) {
        Write-Host 'Chromium proxy under LEP'
        & (Join-Path $PSScriptRoot 'chromium-observations.ps1') -Seconds $Seconds `
            -ExtraArgs "--lang=$Locale" -ExpectLocale $Locale -ExpectTimezone $IanaTimezone
        if ($LASTEXITCODE) { throw "Chromium observations failed with exit $LASTEXITCODE" }
    }

    if ($RunClaude) {
        Write-Host 'Claude Desktop under LEP'
        & (Join-Path $PSScriptRoot 'claude-observations.ps1') -Seconds $Seconds -ExpectLang $Locale
        if ($LASTEXITCODE) { throw "Claude observations failed with exit $LASTEXITCODE" }
    }

    Write-Host 'LEP verification completed.'
} finally {
    if (Test-Path $backup) {
        Copy-Item $backup $ini -Force
        Remove-Item $backup -Force
    }
}
