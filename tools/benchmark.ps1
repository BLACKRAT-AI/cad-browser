<#
.SYNOPSIS
  Measures CAD Browser startup and memory, and compares against Chrome.

.DESCRIPTION
  Produces the numbers in docs/flags.md. Every figure is the median of
  -Runs launches, because a single cold start on Windows is dominated by
  whatever the disk cache happened to hold.

  SAFETY
  ------
  This script only ever stops processes it started itself, identified by
  process id and tracked through their children.

  It never stops a process by name. An earlier version of this script did, and
  it killed the user's real Chrome window along with the one it had launched.
  Before any measurement it records every process id that already existed, and
  it refuses to touch those ids no matter what.

  What it measures:
    cold start   process start until the main window exists and is visible
    idle RSS     working set of the launched process tree, one tab, after settle
    tab RSS      the same with a session of N tabs

  Chrome is measured with --user-data-dir pointing at a temporary profile so an
  existing session cannot skew the result. If Chrome is already running, the
  Chrome comparison is skipped: a second instance would either attach to the
  running one or make the measurement meaningless, and neither is worth the
  risk of disturbing it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1 -Runs 5
#>
[CmdletBinding()]
param(
  [int]$Runs = 5,
  [int]$TabCount = 30,
  [int]$SettleSeconds = 12,
  # Measure Chrome too. Off unless asked for, and skipped when Chrome is
  # already running.
  [switch]$CompareChrome
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
$exe = Join-Path $repo 'out\Release\CADBrowser.exe'
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }

$dataDir = Join-Path $env:LOCALAPPDATA 'CADBrowser'
$sessionPath = Join-Path $dataDir 'session.json'

# Every process id that existed before we started. Nothing here is ever
# stopped, whatever else happens.
$script:ProtectedPids = @{}
foreach ($p in Get-CimInstance Win32_Process) {
  $script:ProtectedPids[[int]$p.ProcessId] = $true
}

function Get-DescendantPids([int]$rootPid) {
  $all = Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId
  $result = New-Object System.Collections.Generic.List[int]
  $result.Add($rootPid)
  $grew = $true
  while ($grew) {
    $grew = $false
    foreach ($p in $all) {
      $id = [int]$p.ProcessId
      $parent = [int]$p.ParentProcessId
      if (-not $result.Contains($id) -and $result.Contains($parent)) {
        $result.Add($id)
        $grew = $true
      }
    }
  }
  return $result
}

# Stops a process tree we launched. Refuses any id that predates this script.
function Stop-LaunchedTree([int]$rootPid) {
  if ($rootPid -le 0) { return }
  if ($script:ProtectedPids.ContainsKey($rootPid)) {
    Write-Warning "Refusing to stop pid $rootPid - it existed before this script ran."
    return
  }
  $pids = Get-DescendantPids $rootPid
  foreach ($id in $pids) {
    if ($script:ProtectedPids.ContainsKey($id)) { continue }
    try { Stop-Process -Id $id -Force -ErrorAction Stop } catch { }
  }
  Start-Sleep -Seconds 2
}

function Get-TreeWorkingSetMB([int]$rootPid) {
  $pids = Get-DescendantPids $rootPid
  $sum = 0
  foreach ($id in $pids) {
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p) { $sum += $p.WorkingSet64 }
  }
  return [math]::Round($sum / 1MB, 1)
}

function Get-TreeProcessCount([int]$rootPid) {
  $pids = Get-DescendantPids $rootPid
  $count = 0
  foreach ($id in $pids) {
    if (Get-Process -Id $id -ErrorAction SilentlyContinue) { $count++ }
  }
  return $count
}

# Launches, waits for a visible main window, and returns the elapsed
# milliseconds plus the pid it started.
function Start-AndTime([string]$path, [string[]]$arguments) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  if ($arguments -and $arguments.Count -gt 0) {
    $proc = Start-Process -FilePath $path -ArgumentList $arguments -PassThru
  } else {
    $proc = Start-Process -FilePath $path -PassThru
  }
  $ms = -1
  while ($sw.Elapsed.TotalSeconds -lt 40) {
    $pids = Get-DescendantPids $proc.Id
    foreach ($id in $pids) {
      $p = Get-Process -Id $id -ErrorAction SilentlyContinue
      if ($p -and $p.MainWindowHandle -ne 0) {
        $ms = [math]::Round($sw.Elapsed.TotalMilliseconds, 0)
        break
      }
    }
    if ($ms -ge 0) { break }
    Start-Sleep -Milliseconds 20
  }
  $sw.Stop()
  return [pscustomobject]@{ Ms = $ms; Pid = $proc.Id }
}

function Median([double[]]$values) {
  if (-not $values -or $values.Count -eq 0) { return 0 }
  $sorted = $values | Sort-Object
  return $sorted[[int][math]::Floor($sorted.Count / 2)]
}

Write-Host "CAD Browser benchmark  ($Runs runs, $TabCount tabs)" -ForegroundColor Cyan
Write-Host "Only processes started by this script are ever stopped." -ForegroundColor DarkGray
Write-Host ""

# ---------------------------------------------------------------------------
# CAD Browser: one tab
# ---------------------------------------------------------------------------
[IO.File]::WriteAllText($sessionPath, '{"version":1,"windows":[]}')

$cadStarts = @()
$cadIdle = @()
$cadProcs = @()
for ($i = 0; $i -lt $Runs; $i++) {
  $run = Start-AndTime $exe @()
  Start-Sleep -Seconds $SettleSeconds
  $cadStarts += $run.Ms
  $cadIdle += Get-TreeWorkingSetMB $run.Pid
  $cadProcs += Get-TreeProcessCount $run.Pid
  Stop-LaunchedTree $run.Pid
}

# ---------------------------------------------------------------------------
# CAD Browser: a restored session of N tabs. They come back asleep, which is
# the whole point of the session format.
# ---------------------------------------------------------------------------
$tabs = @()
for ($i = 0; $i -lt $TabCount; $i++) {
  $tabs += ('{{ "url": "cad://about?t={0}", "title": "Tab {0}" }}' -f $i)
}
[IO.File]::WriteAllText($sessionPath,
  ('{ "version":1, "windows":[ { "active_index":0, "width":1400, "height":900, "tabs":[' +
   ($tabs -join ',') + '] } ] }'))

$tabRun = Start-AndTime $exe @()
Start-Sleep -Seconds $SettleSeconds
$cadTabsRss = Get-TreeWorkingSetMB $tabRun.Pid
$cadTabsProcs = Get-TreeProcessCount $tabRun.Pid
Stop-LaunchedTree $tabRun.Pid
[IO.File]::WriteAllText($sessionPath, '{"version":1,"windows":[]}')

# ---------------------------------------------------------------------------
# Chrome, same method, throwaway profile. Opt-in, and skipped when Chrome is
# already running.
# ---------------------------------------------------------------------------
$chrome = $null
$chromeSkipReason = 'not requested'
if ($CompareChrome) {
  if (Get-Process -Name chrome -ErrorAction SilentlyContinue) {
    $chromeSkipReason = 'Chrome is already running; not disturbing it'
  } else {
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe'))) {
      if (Test-Path $candidate) { $chrome = $candidate; break }
    }
    if (-not $chrome) { $chromeSkipReason = 'Chrome not installed' }
  }
}

$chromeStarts = @()
$chromeIdle = @()
$chromeProcs = @()
$chromeTabsRss = 0
$chromeTabsProcs = 0
$chromeTabsMs = 0
if ($chrome) {
  $profileDir = Join-Path $env:TEMP 'cadbench-chrome'
  $baseArgs = @("--user-data-dir=$profileDir", '--no-first-run',
                '--no-default-browser-check', '--new-window')
  for ($i = 0; $i -lt $Runs; $i++) {
    $run = Start-AndTime $chrome ($baseArgs + @('about:blank'))
    Start-Sleep -Seconds $SettleSeconds
    $chromeStarts += $run.Ms
    $chromeIdle += Get-TreeWorkingSetMB $run.Pid
    $chromeProcs += Get-TreeProcessCount $run.Pid
    Stop-LaunchedTree $run.Pid
  }

  $urls = @()
  for ($i = 0; $i -lt $TabCount; $i++) { $urls += 'about:blank' }
  $run = Start-AndTime $chrome ($baseArgs + $urls)
  Start-Sleep -Seconds $SettleSeconds
  $chromeTabsMs = $run.Ms
  $chromeTabsRss = Get-TreeWorkingSetMB $run.Pid
  $chromeTabsProcs = Get-TreeProcessCount $run.Pid
  Stop-LaunchedTree $run.Pid
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
function Col($value) { if ($chrome) { return $value } else { return 'n/a' } }

Write-Host ("{0,-34} {1,12} {2,12}" -f 'measure', 'CAD Browser', 'Chrome')
Write-Host ("{0,-34} {1,12} {2,12}" -f '----------------------------------', '-----------', '------')
Write-Host ("{0,-34} {1,12} {2,12}" -f 'cold start to window (ms)', (Median $cadStarts), (Col (Median $chromeStarts)))
Write-Host ("{0,-34} {1,12} {2,12}" -f 'idle working set, 1 tab (MB)', (Median $cadIdle), (Col (Median $chromeIdle)))
Write-Host ("{0,-34} {1,12} {2,12}" -f 'processes, 1 tab', (Median $cadProcs), (Col (Median $chromeProcs)))
Write-Host ""
Write-Host ("{0,-34} {1,12} {2,12}" -f "$TabCount tabs, working set (MB)", $cadTabsRss, (Col $chromeTabsRss))
Write-Host ("{0,-34} {1,12} {2,12}" -f "$TabCount tabs, processes", $cadTabsProcs, (Col $chromeTabsProcs))
Write-Host ("{0,-34} {1,12} {2,12}" -f "$TabCount tabs, start (ms)", $tabRun.Ms, (Col $chromeTabsMs))
if (-not $chrome) {
  Write-Host ""
  Write-Host "Chrome comparison skipped: $chromeSkipReason" -ForegroundColor Yellow
} else {
  Write-Host ""
  Write-Host "CAD Browser restores those tabs asleep; Chrome loads them. The"
  Write-Host "difference in strategy is what that row measures, not page weight."
}
