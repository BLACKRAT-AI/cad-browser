# CAD Browser — implementer rules

Read `PLAN.md` first. It holds every architectural decision; this file only holds the mechanics.

## What this is
Minimal Chromium-based Windows browser on **CEF (Views framework, Alloy runtime style, windowed
rendering)**, C++20 + CMake + Ninja + VS2022 Build Tools. Tuned for CAD/CAM/WebGL/WebGPU/gaming.
No Google services, no extensions, no sync.

## Pinned versions
- CEF Stable **152.0.6+g708dc14+chromium-152.0.7977.83** (windows64, "minimal" distribution).
  Bumped from 144.0.35 on 17 September 2026: the 144 build was nine Chromium
  milestones behind the Chrome it was being benchmarked against, which made
  every browser-against-browser number meaningless. See docs/flags.md.
  Do not bump without updating `PLAN.md §12` and re-reading CEF release notes.
- Toolchain: VS 2022 Build Tools (C++ desktop workload), Windows 11 SDK, CMake ≥ 3.21, Ninja.

## Build / run / test (create these in M0 and keep them working)
```powershell
.\build.ps1                 # configure + build Release into out\Release
.\build.ps1 -Debug          # Debug build
.\build.ps1 -Test           # build + run tests\ (doctest)
.\out\Release\CADBrowser.exe
.\out\Release\dump_sysinfo.exe   # prints GPU/RAM/SMBIOS facts; M1 acceptance tool
```
`build.ps1` must download the CEF binary distribution into `third_party/cef/` on first run
(cef-project's `DownloadCEF.cmake`) and never require Docker, Python, or a network after that.

## Order of work
M0 toolchain → M1 `sys/` + `dump_sysinfo` (validate numbers on the owner's PC before any UI) →
M2 tabs → M3 menu/sysinfo → M4 overlay → M5 GPU steering → M6 efficiency benchmarks → M7 theme.
Each milestone's exit criterion is in `PLAN.md §10`; demonstrate it, don't assert it.

## Hard rules
- Windowed rendering only. Never off-screen rendering, never `--single-process`, never disable
  the sandbox or site isolation.
- Every Chromium flag goes in `docs/flags.md` with the reason and a before/after number.
  A flag without a measurement gets removed.
- RAM and GPU facts come from `sys/` (SMBIOS, DXGI, PDH, GlobalMemoryStatusEx) — never from
  parsing `systeminfo`/`wmic` text, never WMI as the sole source.
- The stats overlay and the FPS probe must never modify page DOM or add page globals, and must
  cost nothing when the overlay is off.
- Native CEF Views for tab strip/toolbar/menu. Internal pages (`cad://`) are plain HTML/CSS/JS,
  no frameworks, no bundlers, embedded as resources.
- Theme tokens live in `src/ui/theme.cc` and `src/resources/base.css` only. Flat, one accent.
- No telemetry, no network calls the user didn't initiate (blocklist updates are manual in v1).

## Reporting back to the owner
Keep replies to 4 short lines: what to run/open, pass/fail with one number, the one decision
needed. Details go in `docs/` or the commit message, not chat.

## Never stop a process by name

`Stop-Process -Name chrome` in `tools/benchmark.ps1` killed the owner's real
Chrome window, with his tabs, while benchmarking against a throwaway profile.

Any script here that starts a process records its pid and stops only that pid
and its descendants. Before doing so it snapshots every pid that already
existed and refuses to touch those. `tools/benchmark.ps1` is the reference
implementation; it also skips the Chrome comparison entirely when Chrome is
already running.

The same applies to anything that launches the browser: `Get-Process -Name
CADBrowser | Stop-Process` is acceptable only because that name is ours.

## Do not put a window over someone's screen without asking

`--cad-window-position=X,Y` exists so the UI can be screenshotted without
appearing over whatever the person at the machine is doing. Windows clamps
positions to the visible work area, so it does **not** reliably place a window
off-screen; move it with `SetWindowPos` after creation if that is needed.

When the owner is in a meeting or has said not to, do not launch the browser
at all. Build, test, and defer visual checks.

## Architecture facts that are easy to get wrong

- `CADBrowser.exe` is a renamed copy of CEF's `bootstrap.exe`. All of our code
  is `CADBrowser.dll`, entered through the exported `RunWinMain`. This is what
  keeps the sandbox on without `cef_sandbox.lib`.
- `--disable-features` is a list and CEF sets it first. Merge, never skip on
  `HasSwitch`, or every feature we disable is silently dropped. Re-verify from
  a child process's command line after any CEF bump.
- A docked CEF overlay ignores `SetSize`; it takes its size from the contents
  view's `GetPreferredSize`. Use `CEF_DOCKING_MODE_CUSTOM` plus `SetBounds` to
  place it against the content area rather than the window.
- `settings.json` may have a UTF-8 BOM. The JSON parser skips it; do not
  "simplify" that away.

- A background colour set on a view inside the chrome overlay before that
  overlay's widget exists is silently dropped. `MainWindow::ApplyTheme` runs
  again from `SetChromeRevealed` for exactly this reason. The tab strip hid
  the bug for a long time: it is rebuilt and re-themed whenever a tab title
  changes, so it picked its colour up later on its own while the toolbar and
  the caption buttons stayed transparent and the page showed through them.
  If chrome colours ever look wrong again, paint each view a different colour
  and screenshot the window with `PrintWindow(hwnd, hdc, 2)`. Reading the
  code will not tell you which view is not painting; the picture will in one
  pass.
- Ink drops are off for every button in the chrome, so hover and pressed
  fills are painted by hand in `OnButtonStateChanged`. Without that a button
  looks identical whether or not the pointer is over it.
- `SetBackgroundColor` with an alpha below 255 does not composite against the
  page underneath. The chrome must be fully opaque.
- `CadApp::OnContextInitialized` must call `AppState::Initialize()` before
  anything that can return early. Command-line URL handling returns as soon as
  it has opened its window, and when `Initialize()` sat below it, launching
  with a URL skipped the stats sampler, the blocklist and the GPU preference.
  The symptom was a stats bar stuck on "collecting..." that looked like an
  engine regression. Every benchmark script launches with a URL, so this path
  is the common one, not the rare one.
- CEF Views cannot report which mouse button activated a control.
  `CefButtonDelegate::OnButtonPressed` carries no button field and
  `CefViewDelegate` has no mouse callbacks at all, so middle-click to close a
  tab is done by subclassing the window's WndProc, watching `WM_MBUTTONUP`
  and hit-testing the tab strip with `CefView::GetBoundsInScreen`. Screen
  coordinates, not local ones: the strip lives inside an overlay with its own
  space. The subclass always forwards the message on.
- Do not include `<windowsx.h>` in a file that also includes CEF headers. It
  defines `GetFirstChild` and `GetNextSibling` as macros and breaks
  `cef_dom.h`. Pull the signed halves out of `LPARAM` by hand instead.
