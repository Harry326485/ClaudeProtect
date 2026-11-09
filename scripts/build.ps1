param(
    [switch]$Clean,
    [switch]$BuildLep,
    [string]$Gcc = 'g++'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null

if ($Clean) {
    Remove-Item -Force (Join-Path $build 'ClaudeProtect.exe') -ErrorAction SilentlyContinue
    Remove-Item -Force (Join-Path $build 'EnvironmentProbe.exe') -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force (Join-Path $build 'lep') -ErrorAction SilentlyContinue
}

$flags = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type',
    '-static', '-static-libgcc', '-static-libstdc++', '-municode'
)

Write-Host 'Building EnvironmentProbe.exe'
& $Gcc @flags (Join-Path $root 'src/probe.cpp') -o (Join-Path $build 'EnvironmentProbe.exe')
if ($LASTEXITCODE) { throw 'Probe build failed' }

Write-Host 'Building ClaudeProtect.exe'
$traySources = @(
    'src/main.cpp', 'src/config.cpp', 'src/session.cpp', 'src/lep.cpp', 'src/editor.cpp'
) | ForEach-Object { Join-Path $root $_ }
& $Gcc @flags -mwindows @traySources -o (Join-Path $build 'ClaudeProtect.exe') `
    -luser32 -lshell32 -lcomdlg32 -ladvapi32
if ($LASTEXITCODE) { throw 'Tray build failed' }

if ($BuildLep) {
    & (Join-Path $PSScriptRoot 'build-lep.ps1')
    if ($LASTEXITCODE) { throw 'LEP build failed' }
}

# Copy LEP artifacts when the upstream build has produced them.
$lepOut = Join-Path $root 'LocaleEmulatorPlus-Core/out'
$lepDest = Join-Path $build 'lep'
if (Test-Path $lepOut) {
    New-Item -ItemType Directory -Force $lepDest | Out-Null
    foreach ($arch in @('x64', 'x86')) {
        $source = Join-Path $lepOut $arch
        if (Test-Path $source) {
            Get-ChildItem $source -Filter *.dll | Copy-Item -Destination $lepDest -Force
            Write-Host "Copied LEP $arch DLLs to $lepDest"
        }
    }
    foreach ($license in @('LICENSE-LGPL.txt', 'LICENSE-GPL.txt')) {
        $source = Join-Path $root "LocaleEmulatorPlus-Core/$license"
        if (Test-Path $source) { Copy-Item $source -Destination $lepDest -Force }
    }
}

$loader = Join-Path $lepDest 'LoaderDll_x64.dll'
$core = Join-Path $lepDest 'LocaleEmulatorPlus_x64.dll'
if ((Test-Path $loader) -and (Test-Path $core)) {
    Write-Host "LEP ready: $lepDest"
} else {
    Write-Warning "LEP DLLs are not present in $lepDest. Run scripts/build-lep.ps1 with MSVC and the Windows SDK installed."
}

Write-Host 'Built build/ClaudeProtect.exe and build/EnvironmentProbe.exe'
