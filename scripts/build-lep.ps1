param(
    [string]$VsRoot,
    [string]$MsvcVersion,
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion,
    [switch]$Test,
    [switch]$Verbose,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lep = Join-Path $root 'LocaleEmulatorPlus-Core'
$buildLep = Join-Path $root 'build/lep'

if (-not (Test-Path (Join-Path $lep 'build.py'))) {
    throw "LocaleEmulatorPlus-Core submodule is missing. Run: git submodule update --init --recursive"
}

& (Join-Path $PSScriptRoot 'prepare-lep.ps1')

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
function Find-VsRoot {
    param([string]$Requested)
    if ($Requested) {
        if (Test-Path (Join-Path $Requested 'VC/Tools/MSVC')) { return $Requested }
        throw "VS root has no VC/Tools/MSVC: $Requested"
    }
    if (Test-Path $vswhere) {
        $instances = & $vswhere -all -products * -format json | ConvertFrom-Json
        foreach ($instance in $instances) {
            $path = $instance.installationPath
            if ($path -and (Test-Path (Join-Path $path 'VC/Tools/MSVC'))) { return $path }
        }
    }
    foreach ($candidate in @(
        'C:\BuildTools',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools',
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise'
    )) {
        if (Test-Path (Join-Path $candidate 'VC/Tools/MSVC')) { return $candidate }
    }
    throw @'
No Visual Studio C++ toolchain found. Install Visual Studio Build Tools with the
"Desktop development with C++" workload, then rerun this script. MinGW cannot
build LocaleEmulatorPlus-Core.
'@
}

function Find-MsvcVersion {
    param([string]$VsRoot, [string]$Requested)
    $msvcRoot = Join-Path $VsRoot 'VC/Tools/MSVC'
    if ($Requested) {
        $path = Join-Path $msvcRoot $Requested
        foreach ($arch in @('x64', 'x86')) {
            if (-not (Test-Path (Join-Path $path "bin/Hostx64/$arch/cl.exe")) -or
                -not (Test-Path (Join-Path $path "lib/$arch"))) {
                throw "MSVC $Requested is incomplete for ${arch}: $path"
            }
        }
        if (-not (Test-Path (Join-Path $path 'include'))) {
            throw "MSVC $Requested is missing include: $path"
        }
        return $Requested
    }
    $versions = Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($version in $versions) {
        $complete = (Test-Path (Join-Path $version.FullName 'include'))
        foreach ($arch in @('x64', 'x86')) {
            $complete = $complete -and
                (Test-Path (Join-Path $version.FullName "bin/Hostx64/$arch/cl.exe")) -and
                (Test-Path (Join-Path $version.FullName "lib/$arch"))
        }
        if ($complete) { return $version.Name }
    }
    throw "No usable MSVC toolset under $msvcRoot (need x64/x86 cl.exe, include and lib)."
}

function Find-SdkVersion {
    param([string]$Root, [string]$Requested)
    $includeRoot = Join-Path $Root 'Include'
    if (-not (Test-Path $includeRoot)) { throw "Windows SDK Include directory not found: $includeRoot" }
    $versions = if ($Requested) { @([pscustomobject]@{ Name = $Requested; FullName = Join-Path $includeRoot $Requested }) } else {
        Get-ChildItem $includeRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    }
    $missingReport = @()
    foreach ($version in $versions) {
        $missing = @()
        foreach ($relative in @('um/winnt.h', 'ucrt', 'shared', 'um', 'winrt')) {
            if (-not (Test-Path (Join-Path $version.FullName $relative))) { $missing += $relative }
        }
        $libRoot = Join-Path $Root "Lib/$($version.Name)"
        foreach ($relative in @('um/x64', 'ucrt/x64', 'um/x86', 'ucrt/x86')) {
            if (-not (Test-Path (Join-Path $libRoot $relative))) { $missing += "Lib/$($version.Name)/$relative" }
        }
        if ($missing.Count -eq 0) { return $version.Name }
        $missingReport += "  $($version.Name): missing $($missing -join ', ')"
    }
    throw @"
No complete Windows SDK found under $Root.
Required: Include/<ver>/um/winnt.h, ucrt, shared, um and Lib/<ver>/{um,ucrt}/{x64,x86}.
Candidates checked:
$($missingReport -join "`n")
Install the Windows 11 SDK (Desktop C++ x64/x86), or pass -SdkRoot/-SdkVersion explicitly.
"@
}

function New-SdkOverlay {
    param([string]$RealRoot, [string]$Version, [string]$OverlayRoot)
    $include = Join-Path $OverlayRoot "Include/$Version"
    $lib = Join-Path $OverlayRoot "Lib/$Version"
    New-Item -ItemType Directory -Force $include, $lib | Out-Null
    foreach ($name in @('um', 'shared', 'ucrt', 'winrt')) {
        $link = Join-Path $include $name
        $target = Join-Path $RealRoot "Include/$Version/$name"
        if (Test-Path $link) {
            $current = (Get-Item $link -Force).Target
            if ($current -and $current -ne $target) { Remove-Item $link -Force }
        }
        if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target $target | Out-Null }
    }
    $km = Join-Path $include 'km'
    if (-not (Test-Path $km)) { New-Item -ItemType Directory -Force $km | Out-Null }
    foreach ($shim in @(
        @('ntnls.h', 'ntnls-shim.h'),
        @('mountmgr.h', 'mountmgr-shim.h')
    )) {
        $targetHeader = Join-Path $km $shim[0]
        if (-not (Test-Path $targetHeader)) {
            Copy-Item (Join-Path $PSScriptRoot $shim[1]) $targetHeader -Force
        }
    }
    foreach ($name in @('um', 'ucrt')) {
        $link = Join-Path $lib $name
        $target = Join-Path $RealRoot "Lib/$Version/$name"
        if (Test-Path $link) {
            $current = (Get-Item $link -Force).Target
            if ($current -and $current -ne $target) { Remove-Item $link -Force }
        }
        if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target $target | Out-Null }
    }
    return $OverlayRoot
}

$vsRoot = Find-VsRoot -Requested $VsRoot
if (-not (Test-Path $SdkRoot)) { throw "Windows SDK root not found: $SdkRoot" }
$SdkRoot = (Resolve-Path $SdkRoot).Path
$msvc = Find-MsvcVersion -VsRoot $vsRoot -Requested $MsvcVersion
$sdk = Find-SdkVersion -Root $SdkRoot -Requested $SdkVersion
$vcTools = Join-Path $vsRoot "VC/Tools/MSVC/$msvc"

$sdkRootForBuild = $SdkRoot
$kmInclude = Join-Path $SdkRoot "Include/$sdk/km"
if (-not (Test-Path $kmInclude)) {
    # build.py requires a km include directory, but this codebase does not use
    # WDK implementation code. Supply the two required declarations in km
    # and junctions to the real SDK include/lib trees.
    $overlay = Join-Path $root 'build/sdk-overlay'
    $sdkRootForBuild = New-SdkOverlay -RealRoot $SdkRoot -Version $sdk -OverlayRoot $overlay
    Write-Host "SDK overlay (minimal km headers, WDK not required): $sdkRootForBuild"
}

Write-Host "VS root:  $vsRoot"
Write-Host "MSVC:     $msvc"
Write-Host "SDK root: $sdkRootForBuild"
Write-Host "SDK:      $sdk"

$arguments = @(
    'build.py', 'all', '--no-deploy',
    '--vs-root', $vsRoot,
    '--msvc-version', $msvc,
    '--sdk-root', $sdkRootForBuild,
    '--sdk-version', $sdk
)
if ($Verbose) { $arguments += '--verbose' }
if ($DryRun) { $arguments += '--dry-run' }

Push-Location $lep
try {
    & python @arguments
    if ($LASTEXITCODE) { throw "LEP build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Force $buildLep | Out-Null
Remove-Item (Join-Path $buildLep '*.dll') -Force -ErrorAction SilentlyContinue
foreach ($arch in @('x64', 'x86')) {
    $source = Join-Path $lep "out/$arch"
    if (Test-Path $source) {
        Get-ChildItem $source -Filter *.dll | Copy-Item -Destination $buildLep -Force
        Write-Host "Copied $arch DLLs to $buildLep"
    }
}
foreach ($license in @('LICENSE-LGPL.txt', 'LICENSE-GPL.txt')) {
    $source = Join-Path $lep $license
    if (Test-Path $source) { Copy-Item $source -Destination $buildLep -Force }
}

if ($Test) {
    foreach ($script in @('run-public-api.ps1', 'run-timezone.ps1', 'run-lepb-compat.ps1', 'run-broker-handle.ps1')) {
        $path = Join-Path $lep "tests/$script"
        if (-not (Test-Path $path)) { Write-Warning "Missing test script: $path"; continue }
        Write-Host "Running $script"
        & $path -VcTools $vcTools -SdkRoot $sdkRootForBuild -SdkVersion $sdk
        if ($LASTEXITCODE) { throw "$script failed with exit code $LASTEXITCODE" }
    }
}

Write-Host "LEP build artifacts are in $buildLep"
