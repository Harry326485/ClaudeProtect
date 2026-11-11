param([string]$Gcc = 'g++')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$testDir = Join-Path $root 'build/multi-session-test'
New-Item -ItemType Directory -Force $testDir | Out-Null
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-cast-function-type', '-static', '-municode')
$fixture = Join-Path $testDir 'desktop-fixture.exe'
& $Gcc @flags (Join-Path $root 'tests/multi-session-fixture.cpp') -o $fixture
if ($LASTEXITCODE) { throw 'Fixture build failed' }
Copy-Item -LiteralPath $fixture -Destination (Join-Path $testDir 'claude-science.exe') -Force
$test = Join-Path $testDir 'multi-session-test.exe'
& $Gcc @flags -Wno-unknown-pragmas -I (Join-Path $root 'src/webview2_compat') `
    -isystem (Join-Path $root 'build/deps/webview2/build/native/include') `
    (Join-Path $root 'tests/multi-session.cpp') (Join-Path $root 'src/config.cpp') `
    (Join-Path $root 'src/session.cpp') (Join-Path $root 'src/lep.cpp') (Join-Path $root 'src/ui.cpp') `
    -o $test -luser32 -lshell32 -ladvapi32 -lole32 -luuid -lgdi32
if ($LASTEXITCODE) { throw 'Dual-session test build failed' }
$lepDir = Join-Path $testDir 'lep'
New-Item -ItemType Directory -Force $lepDir | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'build/lep') -File | Copy-Item -Destination $lepDir -Force
$text = "[ClaudeProtect]`r`nDesktopExecutable=$fixture`r`nScienceExecutable=$(Join-Path $testDir 'claude-science.exe')`r`nPreflight=off`r`nVerifyInjection=strict`r`nVerifyGraceSeconds=1`r`n"
[IO.File]::WriteAllText((Join-Path $testDir 'ClaudeProtect.ini'), $text)
& $test $testDir
if ($LASTEXITCODE) { throw 'Dual-session lifecycle test failed' }
