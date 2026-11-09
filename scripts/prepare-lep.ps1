param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lep = Join-Path $root 'LocaleEmulatorPlus-Core'
$revision = '0b11f533ef426ab5606e12f91acd4b1c2f7e6bad'
$patch = Join-Path $root 'patches/lep/claude-protect.patch'

if (-not (Test-Path -LiteralPath (Join-Path $lep 'build.py'))) {
    throw 'LEP submodule is missing. Run git submodule update --init --recursive first.'
}
if (-not (Test-Path -LiteralPath $patch)) { throw "Missing LEP integration patch: $patch" }

$headRevision = & git -C $lep rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read LEP submodule revision.' }
if ($headRevision -ne $revision) {
    throw "Expected LEP $revision, found $headRevision. Check the submodule before building."
}

# A complete reverse check also covers the files introduced by the patch.
& git -C $lep apply --reverse --check --whitespace=nowarn $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host 'LEP integration patch is already applied.'
    exit 0
}

& git -C $lep apply --check --whitespace=nowarn $patch
if ($LASTEXITCODE -ne 0) {
    throw 'LEP patch conflicts with local changes or is only partly applied. Review git -C LocaleEmulatorPlus-Core diff before retrying.'
}
& git -C $lep apply --whitespace=nowarn $patch
if ($LASTEXITCODE -ne 0) { throw 'Failed to apply LEP integration patch.' }
Write-Host 'Applied ClaudeProtect LEP integration patch.'
