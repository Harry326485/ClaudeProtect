[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$build = Join-Path $root 'build'
$lepBuild = Join-Path $build 'lep'
$core = Join-Path $root 'LocaleEmulatorPlus-Core'
$coreOut = Join-Path $core 'out'
$cachePaths = @((Join-Path $core '__pycache__'), (Join-Path $core 'tests/__pycache__'))
$targets = [Collections.Generic.List[string]]::new()

# SDK overlay junctions deliberately stay in place. Never traverse them.
$containers = @($build, $lepBuild, $coreOut,
    (Join-Path $coreOut 'x64'), (Join-Path $coreOut 'x86'))
foreach ($path in $containers) {
    if ((Test-Path -LiteralPath $path) -and
        ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to inspect a linked output directory: $path"
    }
}

foreach ($process in @(Get-Process ClaudeProtect, EnvironmentProbe -ErrorAction SilentlyContinue)) {
    if (-not $process.Path -or $process.Path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Close the running ClaudeProtect/probe session before cleaning.'
    }
}

if (Test-Path -LiteralPath $build) {
    foreach ($item in Get-ChildItem -LiteralPath $build -Force) {
        if ($item.Name -in @('ClaudeProtect.exe', 'EnvironmentProbe.exe', 'lep', 'sdk-overlay') -or
            $item.Name -like 'ClaudeProtect.ini*') { continue }
        $targets.Add($item.FullName)
    }
}
if (Test-Path -LiteralPath $lepBuild) {
    foreach ($item in Get-ChildItem -LiteralPath $lepBuild -Force) {
        if ($item.Name -in @('LoaderDll_x64.dll', 'LoaderDll_x86.dll',
            'LocaleEmulatorPlus_x64.dll', 'LocaleEmulatorPlus_x86.dll',
            'LICENSE-GPL.txt', 'LICENSE-LGPL.txt')) { continue }
        $targets.Add($item.FullName)
    }
}
if (Test-Path -LiteralPath $coreOut) {
    foreach ($item in Get-ChildItem -LiteralPath $coreOut -Force) {
        if ($item.PSIsContainer -and $item.Name -in @('x64', 'x86')) {
            foreach ($artifact in Get-ChildItem -LiteralPath $item.FullName -Force) {
                if (-not $artifact.PSIsContainer -and $artifact.Extension -eq '.dll') { continue }
                $targets.Add($artifact.FullName)
            }
        } else {
            $targets.Add($item.FullName)
        }
    }
}
foreach ($path in $cachePaths) {
    if (Test-Path -LiteralPath $path) { $targets.Add($path) }
}

# Validate every absolute deletion target before making the first change.
$fileCount = 0
$byteCount = 0L
foreach ($path in $targets) {
    $absolute = [IO.Path]::GetFullPath($path)
    $allowed = $absolute.StartsWith($build + '\', [StringComparison]::OrdinalIgnoreCase) -or
               $absolute.StartsWith($coreOut + '\', [StringComparison]::OrdinalIgnoreCase) -or
               $absolute -in $cachePaths
    if (-not $allowed) { throw "Deletion target outside generated directories: $absolute" }
    $item = Get-Item -LiteralPath $absolute -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Refusing to delete a link: $absolute"
    }
    $contents = if ($item.PSIsContainer) {
        @(Get-ChildItem -LiteralPath $absolute -Recurse -Force)
    } else { @($item) }
    if (@($contents | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw "Refusing to delete a directory containing links: $absolute"
    }
    $files = @($contents | Where-Object { -not $_.PSIsContainer })
    $fileCount += $files.Count
    $byteCount += ($files | Measure-Object Length -Sum).Sum
}

foreach ($path in $targets) {
    if ($PSCmdlet.ShouldProcess($path, 'Remove generated files')) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
$verb = if ($WhatIfPreference) { 'Would remove' } else { 'Removed' }
Write-Host ("{0} {1} files ({2:N2} MiB). Runtime binaries, INI backups and SDK overlay retained." -f
    $verb, $fileCount, ($byteCount / 1MB))
