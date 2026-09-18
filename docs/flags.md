# Chromium flags, and what they bought

PLAN.md rule: every flag carries a reason, and a flag with no measurement
behind it is a guess. This file records both, and the measurements that
justify the efficiency claims.

Flags are applied in `src/app/switches.cc`, browser process only. Child
processes inherit what Chromium chooses to pass them.

---

## The bug this file exists to prevent

The first version of `ApplyChromiumSwitches` set every switch with

```cpp
if (!command_line->HasSwitch(name)) command_line->AppendSwitchWithValue(...);
```

`--disable-features` is a **list**, and CEF has already put its own entries on
it before the client callback runs. The guard saw the switch present and
skipped it, so **every feature we asked to disable was silently dropped**. The
child processes came up with CEF's list and none of ours.

It was only caught by reading a child process's actual command line:

```powershell
Get-CimInstance Win32_Process -Filter "Name='CADBrowser.exe'" |
  Where-Object { $_.CommandLine -match '--type=' } |
  ForEach-Object { if ($_.CommandLine -match '--disable-features=([^\s]+)') { $Matches[1] } }
```

Before the fix: `GlicActorUi,LensOverlay,SideBySide` (CEF's own).
After: those plus all eight of ours.

**Check this after every CEF version bump.** A list-valued switch must be
merged, never skipped.

---

## Flags we set

### No Google plumbing

| Flag | Why |
|---|---|
| `--disable-background-networking` | Stops variations, field trial and component traffic a CAD workstation never benefits from. |
| `--disable-component-update` | No background component downloads. This browser updates by being replaced. |
| `--disable-domain-reliability` | No reliability beacons to Google. |
| `--no-pings` | Never sends hyperlink auditing pings. |
| `--disable-client-side-phishing-detection` | Removes the phishing model and its updates. No Safe Browsing service is configured either way. |
| `--no-default-browser-check` | Never prompts to become the default browser. |
| `--no-first-run` | Skips Chromium's first-run flow; CAD Browser has its own. |

### `--disable-features`

`Translate`, `MediaRouter`, `OptimizationHints`,
`OptimizationGuideModelDownloading`, `AutofillServerCommunication`,
`InterestFeedContentSuggestions`, `SegmentationPlatform`, `PushMessaging`.

`PushMessaging` is the measured one. With it enabled, every launch logged a
Google Cloud Messaging registration attempt to `debug.log`:

```
ERROR:google_apis\gcm\engine\registration_request.cc:291]
Registration response error message: DEPRECATED_ENDPOINT
```

That is a network request to Google at startup, which is exactly what PLAN.md
section 7 says to remove. With `PushMessaging` disabled and a 45 second
observation window, no such line appears.

One caveat, found the hard way. A launch *after* the flag was added still
logged two GCM attempts, seven and thirty-two seconds in. GCM state lives in
the profile, so a registration queued by an earlier run retries on a later
startup regardless of the feature flag. It stopped on subsequent launches.

So the honest claim is: **the flag stops CAD Browser from starting GCM**, not
that a profile which has already registered will never retry. Delete
`%LOCALAPPDATA%\CADBrowser` if you need a clean measurement, and observe for
at least 45 seconds, because the first attempt is not immediate.

`Notifications` is deliberately **not** disabled: the Notification API works
locally without any push service, and turning it off would break a standard
web API rather than remove a Google dependency.

### GPU

| Flag | Why |
|---|---|
| `--use-angle=d3d11` | Pins ANGLE to D3D11 so a driver quirk cannot silently drop rendering to a slower backend mid-session. |
| `--enable-gpu-rasterization` | Rasterise on the GPU. Already the default on supported hardware; set explicitly so it is visible and testable. |
| `--enable-zero-copy` | Avoids a copy of every rasterised tile. |

### Opt-in only

| Flag | When |
|---|---|
| `--ignore-gpu-blocklist` | Settings > Performance > Force hardware acceleration. Off by default: the blocklist exists because some driver and hardware combinations crash. |
| `--disable-frame-rate-limit`, `--disable-gpu-vsync` | Settings > Performance > Uncapped frame rate. Off by default because an uncapped compositor burns power for no visible benefit. |

### Deliberately never set

`--single-process`, `--no-sandbox`, `--disable-site-isolation-trials`,
`--disable-gpu-driver-bug-workarounds`, and
`--disable-features=CalculateNativeWinOcclusion`.

The first three destroy the isolation that makes a browser safe to run
untrusted content in. The fourth is why rendering is correct on real drivers.
Occlusion detection saves GPU work when the window is covered, so it stays on.
Faster is not worth unsafe, and none of these were measured to help anyway.

---

## Measurements

Hardware: Acer Predator PH16-71, i9-13900HX (8P + 16E), 64 GB DDR5-4800 dual
channel, RTX 4080 Laptop, Windows 11 Home 25H2 build 26200.9457.

Method: `tools\benchmark.ps1`, median of 3 launches, 10 s settle before each
memory reading. "Working set" is the sum over the whole launched process tree.
Cold start is from process creation to a visible main window.

| Measure | CAD Browser | Chrome |
|---|---|---|
| Cold start to window | 235 ms | 378 ms |
| Idle working set, 1 tab | 412 MB | 446 MB |
| Processes, 1 tab | 7 | 8 |
| 30 tabs, working set | 415 MB | 2939 MB |
| 30 tabs, processes | 7 | 53 |
| 30 tabs, time to window | 525 ms | 464 ms |

An earlier run of the same script gave 344 ms / 589 ms cold start and
410 MB / 482 MB idle, so treat the cold-start figures as "roughly a third
faster", not as three significant figures.

### Reading the 30-tab row honestly

This is **not** a like-for-like content comparison and must not be quoted as
one. CAD Browser restores those tabs asleep and materialises one renderer;
Chrome was given 30 `about:blank` URLs and loaded all of them. The row
measures the difference in restore strategy, which is the actual product
difference, not page weight. Chrome with 30 real pages would be far heavier
still, and CAD Browser would grow as the user woke tabs.

The honest claim is: **a restored session costs one renderer until you touch
the tabs**, and the 415 MB against 2939 MB is what that is worth on this
machine.

### Caveat on the Chrome column

Those Chrome figures were taken with a throwaway `--user-data-dir`. The
benchmark script now refuses to measure Chrome unless `-CompareChrome` is
passed *and* no Chrome is already running, because an earlier version of it
stopped processes by name and killed a real browser window. See the safety
note at the top of `tools\benchmark.ps1`.

---

## Still unmeasured

These are claims PLAN.md wants numbers for, which this build does not yet
have. They are listed so nobody mistakes an untested default for a result.

- **Per-flag attribution.** The table above measures the whole flag set
  against stock Chrome. No flag has been measured in isolation, so none of the
  GPU flags has earned its place on evidence yet.
- **WebGL and WebGPU frame rates** against Chrome on the same page. The
  overlay reports real frame times, so the instrument exists; the comparison
  runs have not been done.
- **Spare renderer.** A single-tab window runs 7 processes: browser, GPU, two
  utilities, and three renderers, where only two are accounted for (the page
  and the stats overlay). The third is Chromium's spare renderer, kept warm to
  make the next navigation faster. It costs about 30 MB. Whether to keep it is
  a measurement nobody has made.

---

## Our own switches

These are not Chromium's. Chromium ignores switches it does not recognise,
which is what makes the command line a usable way to get a value into every
renderer at the moment it starts. Note that Chromium copies only the switches
it knows about into a child process's command line, so `--cad-onshape-focus`
has to be put back by `CadApp::OnBeforeChildProcessLaunch`. Forgetting that
is why it silently did nothing at first.

| switch | effect |
| --- | --- |
| `--cad-onshape-focus` | Runs onshape.com pages in focus mode. Set from Settings > General, and from the hamburger menu, which also pushes the change to pages that are already open. |
| `--cad-profile-log=PATH` | Writes every 4 Hz sample to a CSV and forces the sampler and the frame probe on regardless of the overlay. |
| `--cad-start-minimized` | Starts without taking the screen. |
| `--cad-window-position=X,Y` | Initial window position. |

### The profile log

One row per 250 ms sample, with frame times, the CPU split by process role,
GPU utilisation, VRAM and memory. `-1` in a frame column means the tab did not
draw during that sample. The file is opened shared, so it can be read while
the run is still going; it was opened exclusively at first and that made it
useless for watching a run in progress.

Frame times come from the renderer's own requestAnimationFrame probe because
that is the only place they exist. An external sampler can see that a core is
busy but not that a frame was late, and lateness is the thing a person feels.
The first Onshape profiling session missed a saturated main thread entirely by
looking only at utilisation averaged over a window that included idle time.
See `docs/onshape-profile.md`.

### Onshape focus mode

Collapses the chrome Onshape puts around the viewport: the top document bar,
the left icon rail, the element toolbar, the document tab strip and the bottom
element tab bar. On a 1500x926 window with a 730-instance assembly the WebGL
canvas goes from 1119x820 to 1159x926, which is 26% more pixels.

The feature tree is deliberately left alone. Onshape already has its own
control for collapsing it, and hiding it as well took away somewhere people
work rather than something in the way.

A small handle sits on the middle of the top edge and toggles the whole lot.
It is never hidden, because it is the only way back, and its state is kept in
`sessionStorage` so it survives navigation within the tab. It is offset down
by `metrics::kChromeHeight`, substituted into the script at install time: this
browser's own toolbar is a native overlay above the whole page, so a handle at
the very top would be covered by it exactly when someone reached for it.

An earlier version revealed the chrome automatically when the pointer neared
a window edge. A control the person presses turned out to be better on three
counts: it costs no mousemove handler on a page whose main thread is already
the bottleneck, it cannot surprise anyone mid-drag, and it cannot strand
anyone the way a broken reveal would.

Every selector it uses was measured against a live Onshape document rather
than guessed, and they are specific to Onshape's current markup. If Onshape
restructures its page the mode will stop taking effect; it will not break the
page, because everything it does is a collapsed height or a display rule, and
the setting turns it off.

Four things about the implementation are easy to get wrong and were:

- Hide with `display: none`, not by sliding panels out of the way. The panes
  are positioned by Onshape's own splitters from inline styles, so moving
  them left the rest of the page offset instead of reflowing into the space
  they freed.
- Two containers still carry inline geometry describing a toolbar and a tab
  bar that are no longer there, and the fixed navbar sits in a container that
  reserves its height. All of them need collapsing too, or the space is not
  reclaimed.
- The script runs before the document that replaces it has parsed, so the
  stylesheet, the class on `<html>` and the handle are all discarded. It
  re-applies on each readiness step, watches the class attribute on `<html>`,
  and watches the direct children of `<body>` so the handle survives Onshape
  swapping tabs.
- The `<html>` element must never be cached. The one that exists when the
  script runs is thrown away, and holding a reference to it put the class on
  a detached node while the live page stayed untouched.

---

## Resource comparison under an identical load

The start-up and idle figures above say nothing about what either browser
costs while it is working. This measures that directly: the same WebGL2 page,
the same 24,000 instanced draw calls and the same fixed arithmetic per frame,
run in each browser in turn on an otherwise unchanged machine.

`tools\bench\resource_bench.ps1`, 10 s settle then a 25 s sample, two runs.
Chrome gets a throwaway `--user-data-dir`, so a Chrome that is already open is
never touched. The page publishes its own frame timings through
`document.title`, which is the one channel an external sampler can read from
any browser without a debugging port or an extension.

| Measure | CAD Browser | Chrome | Difference |
|---|---|---|---|
| Processes | 7 | 8 | 1 fewer |
| CPU, % of one core | 65.8 | 76.0 | 13% less |
| CPU, % of the machine | 2.06 | 2.38 | 13% less |
| GPU utilisation | 18.8% | 19.0% | the same |
| Working set | 391 MB | 495 MB | 21% less |
| Private bytes | 288 MB | 343 MB | 16% less |
| Frames per second | 123 | 114 | 8% more |
| Frame time, median | 7.05 ms | 7.10 ms | the same |
| Frame time, 99th percentile | 21.6 ms | 14.5 ms | **49% worse** |

Both runs agreed to within a few percent on every row, so these are the
browser's properties rather than one noisy sample.

### What this does and does not show

**GPU utilisation is identical, and that is the expected result.** The same
page issuing the same draw calls to the same card costs the same. No browser
flag changes what a shader costs. Anything claiming a GPU saving from a
Chromium fork on identical content should be treated as a measurement error.

**The CPU and memory savings are real and they come from structure, not from
flags.** Fewer processes, no extension host, no sync, no background
networking. That is where 13% of a core and a hundred megabytes go.

**The 99th percentile frame time is worse, by a lot.** Median frame time is
identical, so the typical frame is fine, but this browser's worst frames are
about 7 ms longer than Chrome's. Something in the compositing or presentation
path is less consistent. This is the one row where stock Chrome is clearly
better, it is not yet explained, and it should not be quietly dropped from
this table.

---

## Capability ceilings

What each browser will ever allow, as opposed to what it is using. Measured
with `tools\bench\caps_bench.ps1` and `tools\bench\heap_bench.ps1`, both of
which report through the window title because that is the only channel an
external sampler can read from any browser without a debugging port.

| Ceiling | CAD Browser | Chrome | Note |
|---|---|---|---|
| JavaScript heap, reported | 3,586 MB | 3,586 MB | identical |
| JavaScript heap, actually reached | 2,944 MB, tab died | 3,840 MB, survived | **we are worse** |
| Device memory reported to pages | 8 GB | 32 GB | Chrome reports the real figure |
| CPU cores reported to pages | 32 | 32 | identical |
| Max WebGL texture | 16,384 | 16,384 | driver, not browser |
| Max renderbuffer | 16,384 | 16,384 | driver, not browser |
| Max viewport | 32,767 | 32,767 | driver, not browser |
| Combined texture units | 32 | 32 | driver, not browser |
| Simultaneous WebGL2 contexts | 16 | 16 | identical |
| WebGL2 | yes | yes | identical |

Every graphics ceiling is a property of the driver, so the two browsers agree
and always will. The two rows that differ are ours to explain.

### Defect: the raised JavaScript heap does not work

This browser sets `--js-flags=--max-old-space-size=12288`, derived from
installed RAM, and `docs/` and the out-of-memory error page both told the user
that the limit had been raised well above the browser default. **That claim is
false and the setting appears to make things worse.**

The evidence, two runs each, allocating retained arrays until the tab dies:

| Setting | Heap reached before the tab died |
|---|---|
| CAD Browser, `max-old-space-size=12288` | 2,688 MB, then 2,944 MB |
| CAD Browser, `max-old-space-size=4096` | 2,816 MB, then 3,456 MB |
| Chrome, stock | 3,840 MB, then 3,840 MB, survived both times |

`performance.memory.jsHeapSizeLimit` reports 3,586 MB whatever the flag says,
which is the first sign the flag is not taking effect. The reason is that
Chromium builds V8 with pointer compression, which caps the heap near 4 GB
whatever `max-old-space-size` asks for. A value of 12 GB is not merely
ignored: it is out of range, and the heap growth heuristics are then tuned
against a ceiling that cannot be reached.

So the single feature this browser most loudly claims for CAD work is not
delivering, and on this evidence a large assembly is *more* likely to be
killed here than in stock Chrome, not less.

This needs fixing before the claim is repeated anywhere. The likely shape of
the fix is to stop asking for more than pointer compression allows, and to
find out what is ending the renderer at roughly three quarters of the limit
rather than at it. Until then no user-visible text should say the limit has
been raised.

### Device memory

`navigator.deviceMemory` reports 8 GB here and 32 GB in Chrome on a 64 GB
machine. Neither is the true figure; the specification caps the value and
buckets it. It is listed only so nobody reads the difference as a capability.

---

## The version confound, and what it invalidates

Every browser-against-browser number on this page was taken against whatever
Chrome happened to be installed. That turned out to be:

| | Chromium version |
|---|---|
| CAD Browser (CEF 144.0.35) | 144.0.7559.262 |
| Chrome as installed | 153.0.8010.48 |

**Nine milestones apart.** V8's heap sizing and garbage collector, the
compositor and the frame scheduler all changed across those releases. So none
of these measurements isolate what this project built; they measure this
project's code *and* a nine-version Chromium gap together, and they cannot
tell the two apart.

That matters most for the two rows where this browser looked worse:

- **The heap ceiling**, 2,944 MB against 3,840 MB. Setting the V8 limit to
  12288, to 4096, to 3584 and removing it entirely all produced the same
  2,688 to 2,944 MB, so the limit is not what binds. A ceiling that does not
  move whatever it is set to, in a build nine milestones behind, is far more
  likely to be a property of Chromium 144 than of anything here.
- **The 99th percentile frame time**, 21.6 ms against 14.5 ms. Same caveat.
  Frame scheduling is exactly the sort of thing that changed in between.

The rows where this browser looked better are confounded in the same
direction and deserve the same caution, although process count and the
absence of an extension host, sync and background networking are structural
and would survive a version match.

### How to get an honest comparison

Compare against stock Chromium 144, not against whatever Chrome updated
itself to. Until that is done, the defensible claims are the ones measured
against this browser's own previous behaviour, not against Chrome.

The other honest remedy is to stop being nine milestones behind. `CLAUDE.md`
pins CEF deliberately and says not to bump it without updating `PLAN.md` and
reading the release notes, so that is a decision to take rather than a thing
to do quietly.

---

## Re-measured on matched versions (CEF 152)

CEF was bumped from Chromium 144 to 152 on 17 September 2026 precisely so
that this comparison would mean something. Chrome on this machine is 153, so
the gap is one milestone rather than nine. Three source changes were needed:
`CefTextfield::SetTextColor`, `SetPlaceholderTextColor` and
`SetSelectionBackgroundColor` were removed in CEF API 15000 and are replaced
by window-level theme colours set through `CefWindow::SetThemeColor`.

Synthetic WebGL workload, two runs, windows in the foreground:

| Measure | CAD Browser | Chrome | Was, on Chromium 144 |
|---|---|---|---|
| CPU, % of one core | 132.5 | 132.2 | 65.8 vs 76.0, "13% better" |
| GPU utilisation | 10.2% | 12.0% | 18.8% vs 19.0% |
| Working set | 382 MB | 527 MB | 391 vs 495 |
| Private bytes | 283 MB | 358 MB | 288 vs 343 |
| Processes | 7 | 9 | 7 vs 8 |
| Frames per second | 123 | 121 | 123 vs 114 |
| Frame time, 99th percentile | 18.5 ms | 17.6 ms | 21.6 vs 14.5 |

**Two of the earlier findings did not survive the version match.** The 13% CPU
advantage disappeared, and so did the 49% worse 99th percentile frame time
that was flagged as a possible defect. Both were Chromium 144 against
Chromium 153, not this project against Chrome.

**What survives is memory and process count**: 27% less working set, 21% less
private, two fewer processes. Those come from structure, not from flags.

### Occlusion, and a measurement that was worthless

A run taken with the benchmark windows behind an editor reported 1.8% CPU and
no GPU at all, against 132% and 10% for the same workload in front. Chromium
throttles rendering in an occluded window, which is correct behaviour and
makes any background measurement meaningless. `resource_bench.ps1` now raises
the window before sampling. Any figure in this file taken from a buried
window is not a figure.

### Onshape's own check as the workload

`https://cad.onshape.com/check` runs a real graphics benchmark, which is a
better test for this browser than a synthetic page. CAD Browser passes it and
reports 877.2 million triangles per second and 653.5 million lines per second
on the RTX 4080 through ANGLE D3D11, confirming the GPU steering works end to
end.

Sampled while that page runs, one sample only: 49.5% of a core against
Chrome's 89.6%, with memory about level. **One sample, and the workload
starts and stops, so the two browsers may not have been doing the same thing
at the same time. This is not yet a result.**

### The heap ceiling is still unexplained

Five runs on CEF 152: 3,456, 1,664, 2,560, 2,944 and 2,688 MB, against
Chrome's 3,840 MB on every single run. The version bump moved the best case
but not the typical one, and the tab still dies with a renderer crash rather
than a clean out-of-memory. This remains open.

### Correction to the numbers above

Every benchmark in this file launches the browser with a URL on the command
line, and until 17 September 2026 that path skipped `AppState::Initialize()`,
so the stats sampler was not running during any of them. The effect is small
and in one direction: this browser's CPU figures are understated by whatever
the sampler costs, roughly 7% of a core. It applied equally to the Chromium
144 and 152 runs, so the comparison between them stands, but the absolute CPU
numbers for CAD Browser are a little flattering.
