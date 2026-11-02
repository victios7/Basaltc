# Build Basalt MSI with WiX (candle/light) on Windows
param(
  [string]$Version = $env:VERSION,
  [string]$PayloadDir = "payload",
  [string]$OutDir = "dist"
)
if (-not $Version) { $Version = "0.4.0" }
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$wxs = Join-Path $PSScriptRoot "wix\Product.wxs"
$wixobj = Join-Path $OutDir "Product.wixobj"
$msi = Join-Path $OutDir "Basalt-$Version-win-x64.msi"

& candle.exe -nologo -arch x64 "-dProductVersion=$Version" "-dPayloadDir=$PayloadDir" -out $wixobj $wxs
& light.exe -nologo -out $msi $wixobj
Write-Host "Wrote $msi"
