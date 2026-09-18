# CAD Browser - resource comparison against stock Chrome under an identical load.
#
# Answers the only question that is universally comparable between two
# browsers: given exactly the same work, how much CPU, GPU and memory does
# each one spend, and how many frames does it deliver.
#
# SAFETY, and this is not optional. An earlier benchmark in this repo stopped
# processes by name and killed the owner's real Chrome window with his tabs in
# it. This script:
#   * snapshots every browser pid that exists before it starts,
#   * stops only pids it launched itself and their descendants,
#   * refuses to touch a pid that was already running, and
#   * gives Chrome its own throwaway --user-data-dir, so a Chrome that is
#     already open is never asked to do anything at all.

[CmdletBinding()]
param(
  [int]$SettleSeconds = 10,
  [switch]$NoFrameStats,
  [int]$SampleSeconds = 25,
  [string]$Exe,
  [string]$Page,
  [switch]$SkipChrome
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot is not dependable in a param default here, so both paths are
# resolved from the script's own location once the body is running.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Exe)  { $Exe  = Join-Path $here '..\..\out\Release\CADBrowser.exe' }
if (-not $Page) { $Page = Join-Path $here 'gpu_bench.html' }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Fg {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
"@ -ErrorAction SilentlyContinue

function Show-Front([int]$RootPid) {
  for ($i = 0; $i -lt 10; $i++) {
    $p = Get-Process -Id $RootPid -ErrorAction SilentlyContinue
    if ($p -and $p.MainWindowHandle -ne 0) {
      [void][Fg]::ShowWindow($p.MainWindowHandle, 9)
      [void][Fg]::BringWindowToTop($p.MainWindowHandle)
      [void][Fg]::SetForegroundWindow($p.MainWindowHandle)
      return
    }
    Start-Sleep -Milliseconds 500
  }
}

$script:ProtectedPids = @{}
foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
  if ($p.ProcessName -in @('chrome', 'CADBrowser', 'msedge', 'firefox')) {
    $script:ProtectedPids[$p.Id] = $true
  }
}
Write-Host "Protected pids that existed before this run: $($script:ProtectedPids.Count)"

function Get-Descendants([int]$RootPid) {
  # The ids come back as UInt32 while the root arrives as Int32, and a
  # hashtable keyed on both matches neither. That is why an earlier run of
  # this script reported one process per browser and no GPU at all.
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

# GPU utilisation for a set of pids, from the PDH GPU Engine counters. The
# instance names carry the owning pid, so they can be filtered down to just
# the processes of interest. Enumerating every instance is slow - there are
# hundreds - so it is done once and the specific paths are then re-read.
function Measure-Gpu([int[]]$Pids, [int]$Samples = 5) {
  $wanted = @{}
  foreach ($id in $Pids) { $wanted["pid_$id" + "_"] = $true }
  $paths = @()
  try {
    $all = (Get-Counter -ListSet 'GPU Engine' -ErrorAction Stop).PathsWithInstances
    foreach ($path in $all) {
      if ($path -notlike '*Utilization Percentage*') { continue }
      foreach ($prefix in $wanted.Keys) {
        if ($path -like "*$prefix*") { $paths += $path; break }
      }
    }
  } catch { return $null }
  if ($paths.Count -eq 0) { return 0.0 }

  $totals = @()
  for ($i = 0; $i -lt $Samples; $i++) {
    try {
      $s = Get-Counter -Counter $paths -ErrorAction Stop
      $sum = 0.0
      foreach ($v in $s.CounterSamples) { $sum += $v.CookedValue }
      $totals += $sum
    } catch { }
    Start-Sleep -Milliseconds 700
  }
  if ($totals.Count -eq 0) { return $null }
  return [math]::Round(($totals | Measure-Object -Average).Average, 1)
}

function Measure-Browser {
  param([string]$Label, [int]$RootPid)

  # Chromium throttles rendering in an occluded window, which is correct
  # behaviour and ruins this measurement: a run with the window buried behind
  # an editor reported 1.8% CPU and no GPU at all, against 65% and 19% for the
  # same workload in front. The window is raised and kept raised instead.
  Show-Front $RootPid

  Write-Host "  settling $SettleSeconds s..."
  Start-Sleep -Seconds $SettleSeconds

  $tree = Get-Descendants $RootPid
  $procs = @()
  foreach ($id in $tree) {
    $q = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($q) { $procs += $q }
  }

  $cores = [Environment]::ProcessorCount
  $cpu0 = @{}
  foreach ($q in $procs) { $cpu0[$q.Id] = $q.TotalProcessorTime.TotalMilliseconds }
  $clock = [Diagnostics.Stopwatch]::StartNew()

  Write-Host "  sampling $SampleSeconds s..."
  $gpu = Measure-Gpu -Pids $tree -Samples 6
  $remaining = $SampleSeconds - [int]$clock.Elapsed.TotalSeconds
  if ($remaining -gt 0) { Start-Sleep -Seconds $remaining }
  $clock.Stop()

  $cpuMs = 0.0; $ws = 0; $priv = 0; $alive = 0
  foreach ($q in $procs) {
    $now = Get-Process -Id $q.Id -ErrorAction SilentlyContinue
    if (-not $now) { continue }
    $alive++
    $ws += $now.WorkingSet64
    $priv += $now.PrivateMemorySize64
    if ($cpu0.ContainsKey($q.Id)) { $cpuMs += ($now.TotalProcessorTime.TotalMilliseconds - $cpu0[$q.Id]) }
  }

  $window = $clock.Elapsed.TotalMilliseconds
  $title = ''
  $root = Get-Process -Id $RootPid -ErrorAction SilentlyContinue
  if ($root) { $title = $root.MainWindowTitle }

  $fps = $null; $p50 = $null; $p99 = $null
  if ($title -match 'fps=([\d.]+)\s+p50=([\d.]+)\s+p99=([\d.]+)') {
    $fps = [double]$matches[1]; $p50 = [double]$matches[2]; $p99 = [double]$matches[3]
  }

  [pscustomobject]@{
    Browser        = $Label
    Processes      = $alive
    CpuPctOneCore  = [math]::Round(100.0 * $cpuMs / $window, 1)
    CpuPctMachine  = [math]::Round(100.0 * $cpuMs / ($window * $cores), 2)
    GpuPct         = $gpu
    WorkingSetMB   = [math]::Round($ws / 1MB)
    PrivateMB      = [math]::Round($priv / 1MB)
    Fps            = $fps
    FrameP50ms     = $p50
    FrameP99ms     = $p99
  }
}

# -Page takes either a local file or a URL, so that a real site can be used
# as the workload instead of the synthetic one.
if ($Page -match '^https?://') { $pageUrl = $Page } else { $pageUrl = ([Uri](Resolve-Path $Page).Path).AbsoluteUri }
Write-Host "Workload: $pageUrl"
Write-Host ""

$results = @()

Write-Host "CAD Browser:"
$a = Start-Process -FilePath (Resolve-Path $Exe).Path -ArgumentList $pageUrl -PassThru
try { $results += Measure-Browser -Label 'CAD Browser' -RootPid $a.Id }
finally { Stop-Launched $a.Id }
Start-Sleep -Seconds 3

if (-not $SkipChrome) {
  $chrome = "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
  if (-not (Test-Path $chrome)) { $chrome = "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe" }
  if (Test-Path $chrome) {
    $profileDir = Join-Path ([IO.Path]::GetTempPath()) ("cadbench_" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    Write-Host "Chrome (throwaway profile, your own Chrome is untouched):"
    $args = @(
      "--user-data-dir=$profileDir", '--no-first-run', '--no-default-browser-check',
      '--disable-background-networking', '--window-size=1500,950', $pageUrl
    )
    $b = Start-Process -FilePath $chrome -ArgumentList $args -PassThru
    try { $results += Measure-Browser -Label 'Chrome (stock)' -RootPid $b.Id }
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
$results | Format-Table -AutoSize | Out-String -Width 200
