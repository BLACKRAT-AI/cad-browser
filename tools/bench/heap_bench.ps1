# CAD Browser - the real JavaScript heap ceiling, ours against stock Chrome.
#
# This is the claim the whole browser rests on for CAD work: a large assembly
# should not be killed with "V8 javascript OOM" while most of a 64 GB machine
# sits idle. performance.memory.jsHeapSizeLimit reported the same figure in
# both browsers, so it is not evidence either way. This walks into the limit
# instead and records how far each one got before the tab died.
#
# Same safety rule as the other benchmarks: only pids this script launched are
# ever stopped, and Chrome gets a throwaway profile so an already-open Chrome
# is never touched.

[CmdletBinding()]
param(
  [int]$TimeoutSeconds = 420,
  [string]$Exe,
  [string]$Page,
  [switch]$SkipChrome
)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Exe)  { $Exe  = Join-Path $here '..\..\out\Release\CADBrowser.exe' }
if (-not $Page) { $Page = Join-Path $here 'heap.html' }

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

function Measure-Heap([string]$Label, [int]$RootPid) {
  $peak = 0
  $outcome = 'unknown'
  $peakRendererMB = 0
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $stalled = 0

  while ((Get-Date) -lt $deadline) {
    $p = Get-Process -Id $RootPid -ErrorAction SilentlyContinue
    if (-not $p) { $outcome = 'browser gone'; break }
    $t = $p.MainWindowTitle

    if ($t -match 'heap done mb=(\d+)') {
      if ([int]$matches[1] -gt $peak) { $peak = [int]$matches[1] }
      $outcome = 'survived to ceiling'
      break
    }
    if ($t -match 'heap growing mb=(\d+)') {
      $mb = [int]$matches[1]
      if ($mb -gt $peak) { $peak = $mb; $stalled = 0 } else { $stalled++ }
    } else {
      # The title stopped being ours: the page was replaced, which is what an
      # out-of-memory kill looks like from outside.
      if ($peak -gt 0) { $outcome = 'tab died'; break }
    }

    # Largest renderer working set seen along the way.
    foreach ($id in (Get-Descendants $RootPid)) {
      $q = Get-Process -Id $id -ErrorAction SilentlyContinue
      if ($q -and $q.Id -ne $RootPid) {
        $ws = [math]::Round($q.WorkingSet64 / 1MB)
        if ($ws -gt $peakRendererMB) { $peakRendererMB = $ws }
      }
    }

    # Generous: near its ceiling a browser spends a long time in garbage
    # collection before it finally gives up, and cutting it off there turned a
    # Chrome that was still alive into a number that looked like a limit.
    if ($stalled -gt 240) { $outcome = 'stalled 120s'; break }
    Start-Sleep -Milliseconds 500
  }

  [pscustomobject]@{
    Browser           = $Label
    HeapReachedMB     = $peak
    PeakRendererWSMB  = $peakRendererMB
    Outcome           = $outcome
  }
}

$pageUrl = ([Uri](Resolve-Path $Page).Path).AbsoluteUri
Write-Host "Heap page: $pageUrl"
Write-Host ""

$results = @()

Write-Host "CAD Browser:"
$a = Start-Process -FilePath (Resolve-Path $Exe).Path -ArgumentList $pageUrl -PassThru
try { $results += Measure-Heap -Label 'CAD Browser' -RootPid $a.Id }
finally { Stop-Launched $a.Id }
Start-Sleep -Seconds 4

if (-not $SkipChrome) {
  $chrome = "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
  if (-not (Test-Path $chrome)) { $chrome = "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe" }
  if (Test-Path $chrome) {
    $profileDir = Join-Path ([IO.Path]::GetTempPath()) ("cadheap_" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    Write-Host "Chrome (throwaway profile, your own Chrome is untouched):"
    $chromeArgs = @(
      "--user-data-dir=$profileDir", '--no-first-run', '--no-default-browser-check',
      '--disable-background-networking', '--window-size=1000,700', $pageUrl
    )
    $b = Start-Process -FilePath $chrome -ArgumentList $chromeArgs -PassThru
    try { $results += Measure-Heap -Label 'Chrome (stock)' -RootPid $b.Id }
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
