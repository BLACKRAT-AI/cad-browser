<#
.SYNOPSIS
  Downloads and installs the latest CAD Browser release for the current user.

.DESCRIPTION
  Run it in PowerShell:

    irm https://raw.githubusercontent.com/BLACKRAT-AI/cad-browser/main/install.ps1 | iex

  Fetches the newest release zip from GitHub, checks it against the release's
  SHA-256 file, unpacks it into %LOCALAPPDATA%\Programs\CADBrowser (replacing
  an older copy) and adds Start menu and desktop shortcuts. No administrator
  rights are needed and nothing outside the user's profile is touched.
  Settings, the cache and captures live in %LOCALAPPDATA%\CADBrowser and are
  kept across reinstalls.

  Environment overrides, mainly for testing:
    CADBROWSER_DIR            install folder instead of the default
    CADBROWSER_NO_SHORTCUTS   set to 1 to skip the shortcuts

  To uninstall: delete %LOCALAPPDATA%\Programs\CADBrowser and the two
  "CAD Browser" shortcuts.
#>

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is far faster without the bar
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$repo = 'BLACKRAT-AI/cad-browser'
$installDir = if ($env:CADBROWSER_DIR) { $env:CADBROWSER_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\CADBrowser' }

if (-not [Environment]::Is64BitOperatingSystem) {
  throw 'CAD Browser needs 64-bit Windows 10 or 11.'
}

Write-Host 'Finding the latest CAD Browser release...'
$release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'cad-browser-installer' }
$zipAsset = $release.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
$sumAsset = $release.assets | Where-Object { $_.name -like '*.sha256' } | Select-Object -First 1
if (-not $zipAsset) { throw "Release $($release.tag_name) has no zip to install." }

$work = Join-Path ([IO.Path]::GetTempPath()) ("cadbrowser-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  $zip = Join-Path $work $zipAsset.name
  $sizeMb = [Math]::Round($zipAsset.size / 1MB)
  Write-Host "Downloading $($zipAsset.name) ($sizeMb MB)..."
  Invoke-WebRequest $zipAsset.browser_download_url -OutFile $zip -UseBasicParsing

  if ($sumAsset) {
    $expected = ((Invoke-WebRequest $sumAsset.browser_download_url -UseBasicParsing).Content -split '\s+')[0].Trim().ToLowerInvariant()
    $actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) { throw "Checksum mismatch: expected $expected, got $actual. Nothing was installed." }
    Write-Host 'Checksum OK.'
  } else {
    Write-Warning 'This release has no checksum file; installing without verification.'
  }

  $unpacked = Join-Path $work 'unpacked'
  Expand-Archive $zip -DestinationPath $unpacked
  $exe = Get-ChildItem $unpacked -Recurse -Filter 'CADBrowser.exe' | Select-Object -First 1
  if (-not $exe) { throw 'The release zip does not contain CADBrowser.exe.' }

  if (Get-Process -Name 'CADBrowser' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$installDir*" }) {
    throw 'CAD Browser is running from the install folder. Close it and run the installer again.'
  }
  if (Test-Path $installDir) { Remove-Item $installDir -Recurse -Force }
  New-Item -ItemType Directory -Force -Path (Split-Path $installDir) | Out-Null
  Copy-Item $exe.Directory.FullName $installDir -Recurse
  Get-ChildItem $installDir -Recurse -File | Unblock-File
} finally {
  Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

$target = Join-Path $installDir 'CADBrowser.exe'
if ($env:CADBROWSER_NO_SHORTCUTS -ne '1') {
  $shell = New-Object -ComObject WScript.Shell
  $places = @(
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'CAD Browser.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Desktop')) 'CAD Browser.lnk')
  )
  foreach ($lnk in $places) {
    $s = $shell.CreateShortcut($lnk)
    $s.TargetPath = $target
    $s.WorkingDirectory = $installDir
    $s.Description = 'CAD Browser'
    $s.Save()
  }
}

Write-Host "CAD Browser $($release.tag_name) is installed in $installDir"
Write-Host 'Start it from the Start menu or the desktop shortcut.'
