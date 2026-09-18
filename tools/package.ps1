<#
.SYNOPSIS
  Packages a Release build into a distributable zip.

.DESCRIPTION
  Collects everything CAD Browser needs to run on a machine that has never had
  it, and nothing else. Developer artefacts (the test runner, dump_sysinfo,
  .pdb, .lib, .exp) are excluded unless -IncludeTools is passed.

  The output is a plain zip on purpose: PLAN.md milestone M7 calls for a zip
  rather than an installer for v1, and there is nothing to install. Unzip it
  anywhere and run CADBrowser.exe.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\package.ps1
#>
[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [switch]$IncludeTools
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
$outDir = Join-Path $repo "out\$Configuration"
if (-not (Test-Path (Join-Path $outDir 'CADBrowser.exe'))) {
  throw "Build first: CADBrowser.exe not found in $outDir"
}

# Read the pinned CEF version so the archive name records what it was built on.
$cefVersionHeader = Join-Path $repo 'third_party\cef\include\cef_version.h'
$chromeVersion = 'unknown'
if (Test-Path $cefVersionHeader) {
  $line = Select-String -Path $cefVersionHeader -Pattern '#define CHROME_VERSION_MAJOR\s+(\d+)'
  if ($line) { $chromeVersion = $line.Matches[0].Groups[1].Value }
}

$stamp = Get-Date -Format 'yyyyMMdd'
$stageName = "CADBrowser-chromium$chromeVersion-$stamp"
$stage = Join-Path $env:TEMP $stageName
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# Everything required to run. Keep this list explicit: globbing the output
# directory is how a .pdb or a test runner ends up in someone's download.
$required = @(
  'CADBrowser.exe',       # CEF bootstrap, renamed; loads CADBrowser.dll
  'CADBrowser.dll',       # all of our code
  'libcef.dll',
  'chrome_elf.dll',
  'icudtl.dat',
  'v8_context_snapshot.bin',
  'resources.pak',
  'chrome_100_percent.pak',
  'chrome_200_percent.pak'
)

# Optional, but a browser without them renders badly or loses WebGPU.
$optional = @(
  'd3dcompiler_47.dll',
  'libEGL.dll',
  'libGLESv2.dll',
  'dxcompiler.dll',
  'dxil.dll',
  'vk_swiftshader.dll',
  'vk_swiftshader_icd.json',
  'vulkan-1.dll'
)

foreach ($name in $required) {
  $path = Join-Path $outDir $name
  if (-not (Test-Path $path)) { throw "Missing required file: $name" }
  Copy-Item $path $stage
}
foreach ($name in $optional) {
  $path = Join-Path $outDir $name
  if (Test-Path $path) { Copy-Item $path $stage }
}

$locales = Join-Path $outDir 'locales'
if (Test-Path $locales) {
  Copy-Item $locales (Join-Path $stage 'locales') -Recurse
}

if ($IncludeTools) {
  foreach ($name in @('dump_sysinfo.exe', 'cad_tests.exe')) {
    $path = Join-Path $outDir $name
    if (Test-Path $path) { Copy-Item $path $stage }
  }
}

Copy-Item (Join-Path $repo 'README.md') $stage
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
foreach ($doc in @('flags.md', 'stats.md')) {
  $path = Join-Path $repo "docs\$doc"
  if (Test-Path $path) { Copy-Item $path (Join-Path $stage 'docs') }
}
$license = Join-Path $repo 'third_party\cef\LICENSE.txt'
if (Test-Path $license) { Copy-Item $license (Join-Path $stage 'LICENSE-CEF.txt') }

$zip = Join-Path $repo "out\$stageName.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Remove-Item $stage -Recurse -Force

Write-Host ""
Write-Host "Packaged: $zip" -ForegroundColor Green
Write-Host "Size:     $sizeMb MB"
Write-Host "Unzip anywhere and run CADBrowser.exe. Nothing is installed."
