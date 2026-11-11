param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ui = Join-Path $root 'ui'
$html = [IO.File]::ReadAllText((Join-Path $ui 'index.html'))
$html = $html.Replace('/* APP_STYLE */', [IO.File]::ReadAllText((Join-Path $ui 'app.css')))
$html = $html.Replace('/* APP_SCRIPT */', [IO.File]::ReadAllText((Join-Path $ui 'app.js')))
$stoppedLogo = [IO.File]::ReadAllText((Join-Path $root 'assets/claude-stopped.svg'))
$runningLogo = [IO.File]::ReadAllText((Join-Path $root 'assets/claude-running.svg'))
$html = $html.Replace('<!-- APP_LOGO_STOPPED -->', $stoppedLogo)
$html = $html.Replace('<!-- APP_LOGO_RUNNING -->', $runningLogo)
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
[IO.File]::WriteAllText((Join-Path $root 'build/ui.html'), $html, [Text.UTF8Encoding]::new($false))
