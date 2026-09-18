<#
.SYNOPSIS
  Configures and builds CAD Browser.

.DESCRIPTION
  Locates the MSVC toolset, downloads and verifies the pinned CEF binary
  distribution on first run, then configures and builds with CMake + Ninja.
  Build products land in out\<config>.

.EXAMPLE
  .\build.ps1                          Release build
  .\build.ps1 -Configuration Debug     Debug build
  .\build.ps1 -Test                    Build then run the unit tests
  .\build.ps1 -Clean                   Delete the build directory first

.NOTES
  The configuration is a named parameter rather than a -Debug switch because
  PowerShell reserves -Debug as a common parameter.
#>
[CmdletBinding()]
param(
  [ValidateSet('Release', 'Debug')]
  [string]$Configuration = 'Release',
  [switch]$Test,
  [switch]$Clean,
  [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $MyInvocation.MyCommand.Definition

# CEF version pinned by PLAN.md / CLAUDE.md. Keep all three in step.
$CefVersion = '152.0.6+g708dc14+chromium-152.0.7977.83'
$CefSha1 = 'e5e3020627f4528bd43e22f4c4970000b0458e99'
$CefPlatform = 'windows64'

$config = $Configuration
$buildDir = Join-Path $repo "build\$config"
$outDir = Join-Path $repo "out\$config"
$cefRoot = Join-Path $repo 'third_party\cef'

function Write-Step([string]$msg) {
  Write-Host ''
  Write-Host "==> $msg" -ForegroundColor Cyan
}

# Runs a native command. CMake and the compiler write warnings to stderr, and
# with $ErrorActionPreference = 'Stop' PowerShell would treat those as fatal
# even on a successful exit. The exit code is the only thing that decides.
function Invoke-Native([scriptblock]$Command, [string]$What) {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & $Command
    $code = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previous
  }
  if ($code -ne 0) {
    throw "$What failed with exit code $code"
  }
}

# ---------------------------------------------------------------------------
# 1. MSVC environment
# ---------------------------------------------------------------------------
function Import-VcVars {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 Build Tools with the C++ desktop workload."
  }
  $install = & $vswhere -prerelease -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath -format value
  if (-not $install) {
    throw "No Visual Studio installation with the C++ x64 toolset was found."
  }
  $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
  if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $install" }

  Write-Host "    MSVC: $install"
  # Run vcvars in a child cmd and lift the resulting environment into this
  # session; CMake needs cl.exe, the SDK and the linker on PATH.
  $lines = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
  foreach ($line in $lines) {
    $i = $line.IndexOf('=')
    if ($i -gt 0) {
      $name = $line.Substring(0, $i)
      $value = $line.Substring($i + 1)
      Set-Item -Path "env:$name" -Value $value -ErrorAction SilentlyContinue
    }
  }
  $cl = (Get-Command cl.exe -ErrorAction SilentlyContinue)
  if (-not $cl) { throw "cl.exe still not on PATH after importing vcvars64." }
  Write-Host "    cl.exe: $($cl.Source)"
}

# ---------------------------------------------------------------------------
# 2. CEF binary distribution
# ---------------------------------------------------------------------------
function Ensure-Cef {
  if (Test-Path (Join-Path $cefRoot 'cmake\FindCEF.cmake')) {
    Write-Host "    CEF present: $cefRoot"
    return
  }
  Write-Step "Downloading CEF $CefVersion"
  $tmp = Join-Path $env:TEMP "cadbrowser-cef"
  New-Item -ItemType Directory -Force -Path $tmp | Out-Null
  $archive = Join-Path $tmp 'cef_minimal.tar.bz2'

  if (-not (Test-Path $archive)) {
    $name = "cef_binary_${CefVersion}_${CefPlatform}_minimal.tar.bz2"
    $url = 'https://cef-builds.spotifycdn.com/' + [uri]::EscapeDataString($name)
    Write-Host "    $url"
    (New-Object System.Net.WebClient).DownloadFile($url, $archive)
  }

  $hash = (Get-FileHash $archive -Algorithm SHA1).Hash.ToLower()
  if ($hash -ne $CefSha1) {
    Remove-Item $archive -Force
    throw "CEF archive SHA1 mismatch (got $hash, expected $CefSha1). Re-run to retry."
  }
  Write-Host "    SHA1 verified"

  $stage = Join-Path $tmp 'extract'
  if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
  New-Item -ItemType Directory -Force -Path $stage | Out-Null
  Invoke-Native { & tar.exe -xf $archive -C $stage } 'tar'

  $inner = Get-ChildItem $stage -Directory | Select-Object -First 1
  New-Item -ItemType Directory -Force -Path (Split-Path $cefRoot) | Out-Null
  Move-Item $inner.FullName $cefRoot
  Write-Host "    Extracted to $cefRoot"
}

# ---------------------------------------------------------------------------
# 3. Configure and build
# ---------------------------------------------------------------------------
Write-Step "CAD Browser $config build"
Import-VcVars
Ensure-Cef

if ($Clean -and (Test-Path $buildDir)) {
  Write-Step "Cleaning $buildDir"
  Remove-Item $buildDir -Recurse -Force
}

$needConfigure = $Reconfigure -or -not (Test-Path (Join-Path $buildDir 'build.ninja'))
if ($needConfigure) {
  Write-Step "Configuring"
  New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
  Invoke-Native {
    & cmake -G Ninja -S $repo -B $buildDir `
        "-DCMAKE_BUILD_TYPE=$config" `
        "-DCMAKE_C_COMPILER=cl" `
        "-DCMAKE_CXX_COMPILER=cl"
  } 'CMake configure'
}

Write-Step "Building"
$sw = [Diagnostics.Stopwatch]::StartNew()
Invoke-Native { & cmake --build $buildDir } 'Build'
$sw.Stop()
Write-Host "    built in $([math]::Round($sw.Elapsed.TotalSeconds,1))s"

if ($Test) {
  Write-Step "Running tests"
  $exe = Join-Path $outDir 'cad_tests.exe'
  if (-not (Test-Path $exe)) { throw "cad_tests.exe not found at $exe" }
  Invoke-Native { & $exe } 'Tests'
}

Write-Step "Done"
Write-Host "    $outDir"
