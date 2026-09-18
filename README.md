# CAD Browser

A minimal Chromium browser for Windows, tuned for CAD, CAM, WebGL and WebGPU
work. No sync, no sign-in, no extensions, no telemetry.

Built on CEF 144 (Chromium 144) using the Views framework in Alloy runtime
style, so every pixel of the browser chrome is ours and none of Chrome's
services are present.

## The window is the page

There is no permanent toolbar. Tabs and the address bar float above the page
and appear when the pointer reaches the top edge; move further into the bar
and the buttons appear with it. The page is full height at all times, and
revealing the chrome never reflows it.

A new window shows its toolbar for a moment so you can see where it lives.
Ctrl+L brings it back at any time. `Settings > General > Hide the toolbar
until needed` turns the behaviour off.

While the chrome is hidden the window cannot be dragged and has no caption
buttons: brush the top edge to get them back. That is the deliberate cost of
being nearly full screen all the time.

## What it does that Chrome does not

- **Runs on the right GPU.** Enumerates every DXGI adapter, picks the one with
  the most dedicated video memory, and asks Windows to run on it. Then it
  checks what is *actually* rendering through a WebGL probe and tells you when
  the two disagree, with a button that opens the Windows setting that wins.
- **Reports memory honestly.** Installed, usable and hardware-reserved RAM
  from three independent sources, cross-checked, with per-slot DDR detail and
  the motherboard maximum read from the firmware. Any disagreement between
  sources is shown rather than hidden.
- **Sleeps idle tabs.** Background tabs are unloaded under memory pressure and
  reloaded on click. A restored session costs one renderer, not one per tab:
  30 restored tabs use 415 MB here, against 2.9 GB for Chrome opening 30.
- **Blocks tracker hosts.** A host list applied per request. First-party
  requests are never blocked, so a site cannot break itself this way.
- **Measures itself.** An optional thin overlay bar shows real frame times,
  GPU load, video memory, CPU and tab counts, from the same counters Task
  Manager reads.

Measured against Chrome on the development machine: 235 ms to a window against
378 ms, 412 MB idle against 446 MB. See `docs/flags.md` for the method and the
caveats, which matter.

## Build

Needs Visual Studio 2022 Build Tools (C++ desktop workload), Windows 11 SDK,
CMake 3.21+ and Ninja. The first build downloads and SHA-1 verifies the pinned
CEF distribution, about 144 MB.

```powershell
.\build.ps1                          # Release into out\Release
.\build.ps1 -Configuration Debug
.\build.ps1 -Test                    # build, then run the unit tests
.\build.ps1 -Clean
```

Then:

```powershell
.\out\Release\CADBrowser.exe
.\out\Release\dump_sysinfo.exe       # everything the hardware layer detects
.\out\Release\cad_tests.exe          # 73 unit tests
```

`CADBrowser.exe` is a renamed copy of CEF's `bootstrap.exe`; all of the code
is in `CADBrowser.dll`. That is how the Windows sandbox stays fully enabled
without linking `cef_sandbox.lib`. See PLAN.md section 1 of the build log.

## Layout

| Path | What is in it |
|---|---|
| `src/sys/` | Hardware and OS facts: SMBIOS, DXGI, PDH counters, process tree. No CEF. |
| `src/browser/` | Tabs, discard policy, blocklist, session, settings, JSON, URL handling. |
| `src/ui/` | Native Views chrome: window, tab strip, toolbar, menu, overlay, theme. |
| `src/app/` | CefApp, process entry point, Chromium switches, shared state. |
| `src/resources/` | The `cad://` pages, compiled into the binary. |
| `tests/` | 73 unit tests, dependency-free harness. |
| `tools/` | `dump_sysinfo`, `benchmark.ps1`. |
| `docs/` | `flags.md`, `stats.md`. |

`src/sys/` and `src/browser/`'s logic are deliberately free of CEF so the
parts worth testing can be tested.

## Internal pages

`cad://newtab`, `cad://settings`, `cad://sysinfo`, `cad://gpuprobe`,
`cad://about`, `cad://history`, `cad://downloads`. All served from memory, so
they work with no disk or network access.

Chromium's own diagnostics still work: `chrome://gpu`, `chrome://version`,
`chrome://process-internals`.

## User data

`%LOCALAPPDATA%\CADBrowser\`

| File | Purpose |
|---|---|
| `settings.json` | All settings. Hand-editable; a UTF-8 BOM is fine. |
| `session.json` | Open tabs, written atomically and debounced. |
| `blocklist.txt` | Your own blocked hosts, added to the built-in list. |
| `Cache\` | Chromium's cache. Relocatable in Settings. |
| `debug.log` | Warnings and errors only. |

## Keyboard

Ctrl+T / Ctrl+W new and close tab, Ctrl+Shift+T reopen, Ctrl+Tab and
Ctrl+Shift+Tab to move between tabs, Ctrl+1..9 to jump (Ctrl+9 is the last),
Ctrl+L address bar, Ctrl+Shift+S overlay, Ctrl+R or F5 reload (add Shift to
bypass cache), Alt+Left and Alt+Right for history, F12 developer tools.

## Status

Milestones M0 to M6 of PLAN.md are built and verified on the development
machine. What is deliberately not in v1, and what still needs a human at the
keyboard to sign off, is listed at the end of PLAN.md. Read that before
assuming a feature exists.
