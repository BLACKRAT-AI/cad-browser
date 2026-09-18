# Onshape profiling, 17 September 2026

Document: a production assembly of 730 instances on a private Onshape tenant.
(Identifiers left out deliberately: the numbers below are about the browser,
not about anyone's parts.)
Machine: 32 logical cores, 64 GB RAM, RTX 4080 Laptop (12 GB).
Capture: `--cad-profile-log`, 4 Hz, 495 samples with frame data, plus a 12 s
per-thread CPU window taken while the model was being orbited.
Raw data: `docs/profile-onshape.csv`.

## What the frame times say

| metric | value |
| --- | --- |
| frame p50, median over the run | 21.5 ms |
| frame p50, 90th percentile sample | 1,146.6 ms |
| frame p50, worst sample | 1,507.7 ms |
| frame p99, median sample | 360.4 ms |
| frame p99, worst sample | 1,716.2 ms |
| fps, median | 10.5 |
| fps, 10th percentile | 0.7 |
| samples below 30 fps | 391 of 495 |

Interaction is not smooth. Roughly four frames in five are below 30 fps and
the worst single frame in the run took 1.7 seconds.

## Where the time goes

Per-thread CPU in the busiest renderer over a 12 second orbit, as a
percentage of one core:

| thread | % of one core |
| --- | --- |
| CrRendererMain | 93.3 |
| Compositor | 6.1 |
| ThreadPoolForegroundWorker | 4.8 |
| Chrome_ChildIOThread | 2.6 |
| SharedWorker thread | 1.6 |

Meanwhile, over the same run:

| metric | median |
| --- | --- |
| GPU utilisation, whole system | 10.4% |
| CPU utilisation, whole system | 40.5% |
| renderer working set, peak | 4,557 MB |

## Conclusion

The renderer's main thread is saturated and the graphics card is idle. This
is a single-threaded JavaScript and WebAssembly bottleneck inside Onshape's
own client, not a graphics bottleneck and not a memory bottleneck.

That matters because it rules out the whole class of fixes that were on the
table. No GPU flag, no larger VRAM budget, no ANGLE backend and no
rasterisation setting can help: the graphics card is already waiting for
work that the main thread has not finished producing. An earlier reading
that showed "nothing saturated" averaged idle time into the window and hid
this; per-thread sampling during actual interaction is what exposed it.

The earlier session's headline numbers (renderer 52.9% of one core, busiest
thread 33.7%) are superseded by these. They were taken across a window that
included pauses between movements.

## What is worth trying next, in order

1. **Renderer thread priority.** The whole machine is 40% busy from other
   work while our critical path is one thread. Raising the foreground tab's
   renderer above normal priority stops it being descheduled behind
   unrelated work. Cheap to implement, directly motivated by the numbers,
   and measurable with the same instrument.

2. **V8 young-generation size** (`--max-semi-space-size`). The 1.7 second
   frame is the right shape for a major collection on a 4.5 GB heap. A
   larger young generation promotes less into old space and so triggers
   fewer major collections. Unmeasured, and it should not ship until an A/B
   run with this instrument says it helps.

3. **Nothing else on the browser side.** Beyond those two the work is
   Onshape's to do. The remaining lever available to us is to give the
   viewport more of the window, which is what Onshape focus mode does.

## How to reproduce

```
CADBrowser.exe --cad-profile-log=C:\path\to\run.csv
```

One row per 250 ms sample. Frame times come from the renderer's own
requestAnimationFrame probe, which is the only place they exist. `-1` in the
frame columns means the tab did not draw during that sample. The log is
opened shared, so it can be read while the run is in progress.
