# What every number in the stats overlay means

The overlay is a thin bar docked to an edge of the page area. Toggle it with
Ctrl+Shift+S or the hamburger menu. Drag it to another edge, or double-click it
to cycle top, right, bottom, left. Docked left or right it becomes a narrow
card, because a single line of text cannot be read sideways.

Every figure below should reconcile with Task Manager. Where one cannot be
measured it reads `-` rather than showing a guess.

---

## FPS

`FPS 144 7.0 ms`

Real frame intervals from `requestAnimationFrame` in the **active tab's**
renderer, reported to the browser process four times a second. The first
number is frames per second over the last 250 ms window; the second is the
median frame interval. The system page also shows the 99th percentile.

**`FPS - idle` is not a failure.** Chromium only produces frames when
something changes. A static page genuinely is not drawing, and saying "0 fps"
or holding the last value would both be lies. A page with a blinking text
caret, an animation, or a 3D viewport reports continuously.

The probe never touches the page: the callback is handed to a closure and the
property that carried it is deleted on the next line, so nothing of ours
remains on `window`. When the overlay is off the loop is never started, and an
already-running loop stops at its next frame, so the cost with the overlay off
is exactly zero.

## GPU

`RTX 4080 1% 3.2 GB / 11.7 GB`

- **Name** is the adapter CAD Browser chose, shortened to keep the bar from
  changing width between samples. Selection rule: most dedicated video memory
  among non-software adapters, discrete vendors breaking a tie.
- **Percent** is whole-machine load on that adapter, from the
  `GPU Engine(*)\Utilization Percentage` performance counter. It is the
  **busiest engine**, not the sum of all engines. Engines run concurrently, so
  summing them is how monitors end up claiming 300%.
- **VRAM** is `GPU Adapter Memory(*)\Dedicated Usage` over the adapter's
  `DedicatedVideoMemory` from DXGI. That is system-wide use, not ours alone.

`no counters` means the WDDM performance counters are absent, which happens on
old drivers and some virtual GPUs. Everything else still works.

A warning segment appears when our own processes are doing their GPU work on a
*different* adapter than the one we chose. That is measured from per-process
GPU engine counters, so it reflects what is actually happening rather than
what we asked Windows for.

Note on DXGI: `IDXGIAdapter3::QueryVideoMemoryInfo` reports the **calling
process's** usage and budget, not a system total. It is shown on the System
page as "this process is using", and is deliberately not what the bar shows.

## TAB / BROWSER / SYS

- **TAB** is the working set of the active tab's renderer process. It is blank
  until that renderer has reported its process id, which it does with its
  first frame-timing message: CEF exposes no browser-side call for it, and
  guessing from the process tree would mis-attribute memory whenever two tabs
  share a renderer.
- **BROWSER** is the working set of the whole CAD Browser process tree:
  browser, GPU, utilities, renderers, crash handler.
- **SYS** is used physical memory over what Windows can actually allocate
  (`GlobalMemoryStatusEx.ullTotalPhys`), not what is installed. On this
  machine that is 63.7 GB of 64 GB; the difference is reserved by hardware.
  The System page shows installed, usable and reserved separately.

## CPU

`CPU 0.7% sys 35%`

The first figure is CAD Browser's whole process tree, from `GetProcessTimes`
deltas divided by elapsed time times logical processor count. The second is
the machine, from `Processor Information(_Total)\% Processor Time`.

Both are clamped to 0..100. A build running in the background will push the
system figure high; that is real, not an artefact.

## TABS / BLOCKED

`TABS 12 (3 asleep)` counts tabs in this window and how many are discarded.
`BLOCKED` is requests the host blocklist cancelled this session, and is hidden
when it is zero. The count comes from the blocklist itself rather than a
second counter, so the two cannot drift apart.

---

## Sampling

One background thread at 4 Hz. While the overlay is off it parks on a
condition variable: no timer, no wakeups, no cost. Rate counters need two
collections before they mean anything, so the first sample after enabling
reports nothing rather than a spike.

The thread owns its PDH query, DXGI adapter list and process-tree walker, and
hands finished snapshots to the UI thread through `CefPostTask`.

---

## The honest limits

- **"Keep at full speed" does less than its name suggests.** Marking a tab
  performance-pinned exempts it from discard and keeps it visible to the
  compositor when another tab is in front, so its rAF loop and compositor stay
  alive. It does **not** raise the renderer's process priority: Chromium's
  `--disable-renderer-backgrounding` is process-global, so using it would
  un-throttle every tab, which is the opposite of what this browser is for. A
  background pinned tab still runs at background priority.
- **Tab memory is unattributed until a tab draws.** The discard policy
  therefore cannot weigh a never-rendered tab by size, and falls back to
  ordering by idle time alone.
- **The system CPU figure is whole-machine.** It is not CAD Browser's share of
  the machine, and on a busy workstation it will often be dominated by
  something else entirely.

---

## Where the bar sits now that the chrome auto-hides

The tab strip and address bar are themselves an overlay that appears only when
the pointer reaches the top of the window. The stats bar is a separate overlay
and is not affected: it stays wherever it is docked.

If you dock the stats bar to the top, the chrome will cover it for as long as
the chrome is revealed. That is a few hundred milliseconds at a time and only
while you are reaching for the toolbar, so it is left alone rather than
shuffled out of the way. Dock it to the bottom, the default, and the two never
meet.
