param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$version = '1.0.2903.40'
$checksum = 'EF128016DD1E51C59178C827ED5B8AA3322C57AFA8675D930F8109505542AD74'
$cache = Join-Path $root 'build/deps'
$package = Join-Path $cache 'webview2.nupkg'
$sdk = Join-Path $cache 'webview2'
New-Item -ItemType Directory -Force $cache | Out-Null
if (-not (Test-Path -LiteralPath $package)) {
    Write-Host "Downloading Microsoft WebView2 SDK $version"
    $url = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg"
    $download = Join-Path $cache "webview2.$PID.download"
    try {
        Invoke-WebRequest $url -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $checksum) {
            throw 'Downloaded WebView2 SDK checksum mismatch.'
        }
        Move-Item -LiteralPath $download -Destination $package -Force
    } finally {
        if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Force }
    }
}
if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne $checksum) {
    throw "WebView2 SDK checksum mismatch. Remove $package and retry."
}
$required = @('build/native/include/WebView2.h', 'build/native/x64/WebView2Loader.dll', 'LICENSE.txt', 'NOTICE.txt')
if (@($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $sdk $_)) }).Count) {
    Expand-Archive -LiteralPath $package -DestinationPath $sdk -Force
}
$loaderSource = Join-Path $sdk 'build/native/x64/WebView2Loader.dll'
$loaderTarget = Join-Path $root 'build/WebView2Loader.dll'
if (-not (Test-Path -LiteralPath $loaderTarget) -or
    (Get-FileHash -LiteralPath $loaderSource).Hash -ne (Get-FileHash -LiteralPath $loaderTarget).Hash) {
    Copy-Item -LiteralPath $loaderSource -Destination $loaderTarget -Force
}
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $root 'build/WebView2-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $sdk 'NOTICE.txt') -Destination (Join-Path $root 'build/WebView2-NOTICE.txt') -Force
