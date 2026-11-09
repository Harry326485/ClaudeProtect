param([string]$Gcc = 'g++')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null
$testExe = Join-Path $build 'settings-test.exe'
& $Gcc -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -static -municode `
    (Join-Path $root 'tests/settings.cpp') (Join-Path $root 'src/config.cpp') -o $testExe
if ($LASTEXITCODE) { throw 'Settings test build failed' }
& $testExe (Join-Path $build 'settings-test-data')
if ($LASTEXITCODE) { throw 'Settings tests failed' }
