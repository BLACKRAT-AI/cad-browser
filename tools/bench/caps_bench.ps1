# CAD Browser - capability ceilings, ours against stock Chrome.
#
# Not what either browser is using right now: what either browser will ever
# allow. The JavaScript heap ceiling is the one that decides whether a large
# assembly survives or the tab is killed, and it is the main thing this
# browser changes.
#
# Same safety rule as resource_bench.ps1: only pids this script launched are
# ever stopped, and Chrome is given a throwaway profile so an already-open
# Chrome is never touched.

[CmdletBinding()]
param(
  [int]$ReadSeconds = 14,
  [string]$Exe,
  [string]$Page,
  [switch]$SkipChrome
)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Exe)  { $Exe  = Join-Path $here '..\..\out\Release\CADBrowser.exe' }
if (-not $Page) { $Page = Join-Path $here 'caps.html' }

$script:ProtectedPids = @{}
foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
  if ($p.ProcessName -in @('chrome', 'CADBrowser', 'msedge', 'firefox')) {
    $script:ProtectedPids[[int]$p.Id] = $true
  }
}
Write-Host "Protected pids that existed before this run: $($script:ProtectedPids.Count)"

function Get-Descendants([int]$RootPid) {
  $all = Get-CimInstance Win32_Process | ForEach-Object {
    [pscustomobject]@{ Id = [int]$_.ProcessId; Parent = [int]$_.ParentProcessId }
  }
  $keep = @{}
  $keep[[int]$RootPid] = $true
  $grew = $true
  while ($grew) {
    $grew = $false
    foreach ($row in $all) {
      if (-not $keep.ContainsKey($row.Id) -and $keep.ContainsKey($row.Parent)) {
        $keep[$row.Id] = $true; $grew = $true
      }
    }
  }
  return @($keep.Keys)
}

function Stop-Launched([int]$RootPid) {
  if ($script:ProtectedPids.ContainsKey($RootPid)) {
    Write-Warning "Refusing to stop pid $RootPid : it existed before this run."
    return
  }
  $tree = Get-Descendants $RootPid
  $root = Get-Process -Id $RootPid -ErrorAction SilentlyContinue
  if ($root -and $root.MainWindowHandle -ne 0) { $null = $root.CloseMainWindow() }
  Start-Sleep -Seconds 3
  foreach ($id in $tree) {
    if ($script:ProtectedPids.ContainsKey($id)) { continue }
    Get-Process -Id $id -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}

# The page rotates two chunks through the window title, because a Win32 title
# is too short to carry all of it at once. Poll until both have been seen.
function Read-Caps([string]$Label, [int]$RootPid) {
  $facts = @{}
  $deadline = (Get-Date).AddSeconds($ReadSeconds)
  while ((Get-Date) -lt $deadline) {
    $p = Get-Process -Id $RootPid -ErrorAction SilentlyContinue
    if ($p) {
      $t = $p.MainWindowTitle
      if ($t -match 'caps A (.+?)(?: - |$)') { $facts['A'] = $matches[1] }
      if ($t -match 'caps B (.+?)(?: - |$)') { $facts['B'] = $matches[1] }
    }
    if ($facts.ContainsKey('A') -and $facts.ContainsKey('B')) { break }
    Start-Sleep -Milliseconds 400
  }

  $row = [ordered]@{ Browser = $Label }
  foreach ($key in @('A', 'B')) {
    if (-not $facts.ContainsKey($key)) { continue }
    foreach ($pair in ($facts[$key] -split '\s+')) {
      if ($pair -match '^([a-z0-9]+)=(.+)$') { $row[$matches[1]] = $matches[2] }
    }
  }
  [pscustomobject]$row
}

$pageUrl = ([Uri](Resolve-Path $Page).Path).AbsoluteUri
Write-Host "Capabilities page: $pageUrl"
Write-Host ""

$results = @()

Write-Host "CAD Browser:"
$a = Start-Process -FilePath (Resolve-Path $Exe).Path -ArgumentList $pageUrl -PassThru
try { $results += Read-Caps -Label 'CAD Browser' -RootPid $a.Id }
finally { Stop-Launched $a.Id }
Start-Sleep -Seconds 2

if (-not $SkipChrome) {
  $chrome = "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
  if (-not (Test-Path $chrome)) { $chrome = "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe" }
  if (Test-Path $chrome) {
    $profileDir = Join-Path ([IO.Path]::GetTempPath()) ("cadcaps_" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    Write-Host "Chrome (throwaway profile, your own Chrome is untouched):"
    $chromeArgs = @(
      "--user-data-dir=$profileDir", '--no-first-run', '--no-default-browser-check',
      '--disable-background-networking', '--window-size=1200,800', $pageUrl
    )
    $b = Start-Process -FilePath $chrome -ArgumentList $chromeArgs -PassThru
    try { $results += Read-Caps -Label 'Chrome (stock)' -RootPid $b.Id }
    finally {
      Stop-Launched $b.Id
      Start-Sleep -Seconds 2
      Remove-Item $profileDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  } else {
    Write-Warning "Chrome not found; CAD Browser measured alone."
  }
}

Write-Host ""
$results | Format-List | Out-String -Width 200
