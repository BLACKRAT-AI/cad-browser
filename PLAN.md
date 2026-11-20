# CAD Browser — Build Plan

Owner: jmp. Written 2026-09-17. Target implementer: Claude Opus (or any engineer new to this repo).
Status: PLANNING — nothing built yet. Read this whole file before writing code.

---

## 0. One-paragraph brief

CAD Browser is a minimal, professional Chromium-based desktop browser for Windows, tuned for
CAD / CAM / WebGL / WebGPU / browser-gaming workloads. It is built on **CEF (Chromium Embedded
Framework)** using the **Views framework in Alloy runtime style**, in C++20 with CMake. It ships
with no Google services, no sync, no sign-in, no extensions, no telemetry. It auto-detects the
GPU and steers Chromium's GPU process onto the discrete adapter, reports installed / usable /
motherboard-max RAM correctly from SMBIOS, handles hundreds of tabs via its own discard policy,
and has an optional stats overlay (FPS, frame time, memory, GPU, CPU) toggled from the hamburger
menu. Theme is flat, light/dark following Windows, one accent colour.

---

## 1. Decisions already made (do not re-open)

| Decision | Choice | Why |
|---|---|---|
| Engine | CEF binary distribution, **Stable channel**, Windows x64. Pinned: **CEF 152.0.6 / Chromium 152.0.7977.83** (was 144.0.35 / 144.0.7559.262 until 17 September 2026) (beta is 154). Pin the exact version in `CMakeLists.txt` and `CLAUDE.md`. | A full Chromium fork is a 100 GB checkout and multi-hour builds; CEF builds in minutes, is multi-process like Chrome, and still gives full control over command-line flags, process model, request handling, and UI. |
| Runtime style | **Alloy** style windows + browsers (`CEF_RUNTIME_STYLE_ALLOY`) via CEF Views. No Chrome toolbar (`CEF_CTT_NONE`). | Alloy = no Chrome UI, we own the whole chrome. Views gives native window, tab buttons, textfield, menus, and `CefWindow::AddOverlayView` for the stats overlay. Confirmed present in current CEF headers. |
| Rendering mode | **Windowed** (not off-screen rendering). | OSR copies every frame through CPU memory; windowed lets the GPU process present directly. Non-negotiable for "faster than Chrome". |
| Language / build | C++20, CMake ≥ 3.21, Ninja, Visual Studio 2022 Build Tools, Windows 11 SDK. Start from the official `cef-project` CMake template. | Matches CEF distribution exactly. |
| Internal pages | Custom scheme `cad://` served from embedded resources (new tab, settings, system info, stats overlay content, about). Plain HTML/CSS/vanilla JS, no framework, no bundler. | Zero build tooling for UI, tiny footprint, fully offline. |
| Top chrome (tab strip, omnibox, menu) | **Native CEF Views** controls, not HTML. | No extra renderer process for the UI; lower idle memory; instant startup. |
| Stats overlay | `CefWindow::AddOverlayView` hosting a small `CefBrowserView` that loads `cad://overlay` with a transparent background, docked top-right. Updated 4×/s over a JS binding. | Overlay must not touch page DOM (would break CAD apps, screenshots, CSS). Views overlay sits above the content view natively. |
| Extensions | **None in v1.** Built-in host-based ad/tracker blocker instead (see §7). | Alloy extension support is gone/unsupported; extensions are the #1 source of Chrome bloat anyway. |
| Platform | Windows 10 (1903+) / 11, x64 only for v1. | All of the hardware-detection code is Win32/DXGI/PDH. Keep a `platform/` seam so Linux/macOS are possible later. |
| Ad blocking engine | v1: host + URL-prefix block from EasyList/EasyPrivacy-derived host lists. v2: adblock-rust via its C FFI. | v1 is a day of work; v2 is real filter-syntax support. |

---

## 2. Non-goals (v1)

Sync, accounts, password manager, autofill, translate, reading mode, extensions, Cast,
spellcheck, PWAs/install, profiles, macOS/Linux, ARM64, automatic updates (manual download only),
GPU temperature/clock (needs vendor SDKs — deferred).

---

## 3. Repository layout

```
CADBrowser/
  CLAUDE.md                 # build/run commands + rules for the implementer
  PLAN.md                   # this file
  CMakeLists.txt            # top level; downloads/pins CEF binary distribution
  cmake/                    # DownloadCEF.cmake (from cef-project), warnings, resource embedding
  third_party/cef/          # populated by CMake (git-ignored)
  src/
    app/                    # main(), CefApp, process split (browser vs renderer vs other)
      main_win.cc
      cad_app.h/.cc         # CefApp: OnBeforeCommandLineProcessing (flags), scheme registration
      browser_app.cc        # CefBrowserProcessHandler
      renderer_app.cc       # CefRenderProcessHandler: FPS probe + PID report (see §8)
    ui/                     # everything the user sees, CEF Views only
      main_window.h/.cc     # CefWindowDelegate; owns tab strip, toolbar, content area, overlay
      tab_strip.h/.cc       # CefPanel of TabButton views; drag-reorder; pinned; close buttons
      toolbar.h/.cc         # back/forward/reload, omnibox (CefTextfield), hamburger (CefMenuButton)
      hamburger_menu.h/.cc  # CefMenuModel; stats toggle; system info; settings; etc.
      overlay_view.h/.cc    # stats overlay (AddOverlayView + CefBrowserView on cad://overlay)
      theme.h/.cc           # colour tokens, light/dark detection, DPI
    browser/                # per-tab logic
      tab.h/.cc             # Tab model: CefBrowser, state, last-active, discarded snapshot
      tab_manager.h/.cc     # create/close/activate/discard; discard policy (see §6)
      client_handler.h/.cc  # CefClient + Life/Load/Display/Request/Download/Keyboard handlers
      request_filter.h/.cc  # ad/tracker host blocking (§7)
      history.h/.cc         # SQLite history + bookmarks (sqlite3 amalgamation vendored)
      session.h/.cc         # restore tabs on start; crash-safe write-ahead JSON
    sys/                    # hardware & OS facts. Pure Win32, no CEF dependency, unit-testable
      gpu_info.h/.cc        # DXGI adapter enumeration, VRAM budget/usage, preferred-adapter logic (§4)
      gpu_preference.h/.cc  # NvOptimusEnablement/AmdPowerXpress exports + HKCU GpuPreference (§4)
      memory_info.h/.cc     # installed vs usable vs motherboard-max RAM via SMBIOS (§5)
      smbios.h/.cc          # GetSystemFirmwareTable('RSMB') parser: types 16, 17, 19
      perf_counters.h/.cc   # PDH: GPU Engine utilisation, GPU adapter memory, CPU
      process_tree.h/.cc    # enumerate our child processes, classify by --type=, per-PID memory/CPU
      stats_sampler.h/.cc   # 4 Hz sampler thread; produces StatsSnapshot for overlay + menu
    ipc/
      messages.h            # names/schemas of CefProcessMessage used between browser and renderer
    resources/              # embedded into the exe as RC data, served on cad://
      newtab.html  settings.html  sysinfo.html  overlay.html  about.html  base.css  icons/*.svg
      blocklists/hosts.txt
  tests/                    # doctest or Catch2; tests for sys/ (SMBIOS parser with fixtures), tab_manager policy, request_filter
  tools/
    dump_sysinfo.cc         # tiny console exe that prints everything sys/ detects. Build first, run on jmp's PC.
  docs/
    flags.md                # every Chromium flag we set, with the reason and the benchmark that justified it
    stats.md                # exact definition of every stat in the overlay
```

---

## 4. GPU detection and selection (must be right on hybrid laptops and multi-GPU desktops)

### 4.1 Enumerate (at startup, `sys/gpu_info.cc`)
1. `CreateDXGIFactory1` → `IDXGIFactory6` if available (Win10 1803+).
2. `EnumAdapterByGpuPreference(DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)` for the OS's own ranking,
   and `EnumAdapters1` for the full list.
3. For each adapter capture: `Description`, `VendorId` (0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel,
   0x1414 Microsoft Basic Render = software), `DeviceId`, `DedicatedVideoMemory`,
   `SharedSystemMemory`, LUID, `DXGI_ADAPTER_FLAG_SOFTWARE`, driver version via
   `IDXGIAdapter::CheckInterfaceSupport(IDXGIDevice)` UMD version, and whether it has outputs
   (`EnumOutputs`) — the one with the monitor attached matters on laptops.
4. Live VRAM: `IDXGIAdapter3::QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, …)` →
   `Budget`, `CurrentUsage`. This is the number the overlay shows. Sample at 4 Hz.
5. Utilisation: PDH counters `\GPU Engine(*)\Utilization Percentage` filtered to instances whose
   name contains our process tree's PIDs (instance format `pid_<pid>_luid_…_phys_<n>_eng_<m>_engtype_3D`)
   → "browser GPU %" and unfiltered → "system GPU %". Also `\GPU Adapter Memory(*)\Dedicated Usage`
   as a cross-check for QueryVideoMemoryInfo.

### 4.2 Choose
Preferred adapter = highest `DedicatedVideoMemory` among non-software adapters, tie-break
NVIDIA/AMD over Intel, then "has an output". Store as `PreferredAdapter{luid, name}`.

### 4.3 Make Chromium actually use it
There is no CEF/Chromium flag that reliably picks the adapter; Windows decides per executable.
Do all three:
1. Export from the main exe (applies to every child process since they're the same exe):
   ```cpp
   extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 1;
                __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1; }
   ```
2. On first run (and whenever the exe path changes) write
   `HKCU\Software\Microsoft\DirectX\UserGpuPreferences\<full exe path> = "GpuPreference=2;"`
   (2 = high performance). No admin needed. Setting "Auto-select high-performance GPU" in
   Settings toggles this; default ON.
3. Verify: after the first tab's GPU process is up, load a hidden `cad://gpuprobe` page once that
   reads `WEBGL_debug_renderer_info` UNMASKED_RENDERER + `navigator.gpu.requestAdapter()` info
   and posts it to the browser process. Compare against `PreferredAdapter`. Show "Active GPU:
   NVIDIA … ✓" or "Active GPU: Intel … (expected NVIDIA — click to fix)" in the hamburger menu.
   The fix action opens Windows Graphics Settings (`ms-settings:display-advancedgraphics`) since
   some OEM drivers override HKCU.

### 4.4 Flags relevant to GPU (see `docs/flags.md`; every flag must be benchmarked, not assumed)
Set in `OnBeforeCommandLineProcessing` for the browser process:
- `--enable-gpu-rasterization`, `--enable-zero-copy`, `--enable-oop-rasterization` (mostly default now; keep explicit + benchmark)
- `--use-angle=d3d11` (default on Windows; explicit so it never silently falls back to d3d9/gl)
- `--enable-features=WebGPU,Vulkan?` — **do not** enable Vulkan by default; benchmark on jmp's PC first
- `--ignore-gpu-blocklist` — **off by default**, exposed as a settings toggle "Force hardware acceleration" with a warning
- `--disable-gpu-driver-bug-workarounds` — never
- `--enable-unsafe-webgpu` — never
- `--disable-frame-rate-limit` / `--disable-gpu-vsync` — **per-tab "Uncapped FPS" option only**, off by default. Useful for benchmarking games; wastes power otherwise.

---

## 5. RAM detection — do it properly

Previous tooling on jmp's machine gave a lazy/wrong RAM number. The rule here: **three independent
sources, cross-checked, all shown, discrepancies explained** — never a single API call, never
scraping `systeminfo` text, never WMI as the only source.

`sys/memory_info.cc` produces:

| Field | Source | Notes |
|---|---|---|
| Installed RAM | `GetPhysicallyInstalledSystemMemory` (kB) | Reads SMBIOS. Fails with ERROR_INVALID_DATA in some VMs → fall back to SMBIOS sum, then GlobalMemoryStatusEx, and flag the fallback. |
| Installed RAM (per-DIMM) | Own SMBIOS parser: `GetSystemFirmwareTable('RSMB')`, walk structures, **type 17** Memory Device: Size (0x7FFF ⇒ use ExtendedSize), Speed / ConfiguredMemorySpeed, MemoryType (DDR4/DDR5…), FormFactor, DeviceLocator (slot label), Manufacturer, PartNumber, SerialNumber. Size 0 = empty slot. | Sum must equal Installed RAM; if not, show both and say which is trusted (SMBIOS sum). |
| Usable by OS | `GlobalMemoryStatusEx().ullTotalPhys` | Installed minus hardware-reserved. Show "Hardware reserved: X MB". |
| Hardware reserved | Installed − Usable | Large values (> 1 GB) on desktops indicate iGPU carve-out or a misconfigured BIOS; say so in the UI. |
| In use / available now | `GlobalMemoryStatusEx().ullAvailPhys`, `GetPerformanceInfo()` (commit total/limit, cached, kernel) | Overlay uses these. |
| Motherboard max | SMBIOS **type 16** Physical Memory Array: `MaximumCapacity` (kB; if 0x80000000 use `ExtendedMaximumCapacity` bytes), `NumberOfMemoryDevices` (slots). | Firmware lies sometimes; label it "as reported by firmware". |
| Slots used / total | count of type 17 with Size > 0 / type 16 NumberOfMemoryDevices | |
| Channel config | infer from populated slots' locators (A1/B1 etc.) — best effort, label "estimated" | |
| Cross-check | WMI `Win32_PhysicalMemory` Capacity sum (via COM, optional, timeout 2 s) | Shown only if it disagrees with SMBIOS. |

Acceptance test: `tools/dump_sysinfo.exe` on jmp's PC prints installed / usable / reserved /
per-DIMM / max, and the numbers match Task Manager → Performance → Memory and the BIOS. This is
milestone M1's exit criterion, before any browser UI is written.

Where it shows: hamburger → **System** panel (`cad://sysinfo`), and the overlay's memory line uses
`Usable` as the denominator (that's what the OS can actually give us).

---

## 6. Tabs — "as easy as Chrome, unlike Firefox"

### 6.1 Process model
CEF default = Chromium's site-per-process. Keep it. Do not set `--renderer-process-limit`;
Chromium derives it from physical RAM already. Revisit only if M6 benchmarks show a win, and
record the decision in `docs/flags.md`.

### 6.2 Tab strip (native Views)
- `CefPanel` with horizontal box layout of `TabButton` (custom `CefLabelButton` subclass + close
  `CefButton`). Favicon via `CefDisplayHandler::OnFaviconURLChange` + `DownloadImage`.
- Behaviours: Ctrl+T / Ctrl+W / Ctrl+Tab / Ctrl+Shift+Tab / Ctrl+1..9 / Ctrl+Shift+T reopen,
  middle-click close, middle-click link → background tab, drag reorder, pin (narrow favicon-only),
  mute indicator, tab overflow → scroll buttons + "Search tabs" (Ctrl+Shift+A) list with fuzzy match.
- Hover card: title, URL, **memory (MB) and CPU % of that tab's renderer**, last active, discarded?
- New windows: each `MainWindow` is its own tab strip; drag tab out → new window (v1.1).

### 6.3 Discard policy (our own; Alloy has no Chrome Memory Saver)
`TabManager::DiscardPolicy` runs every 10 s on the sampler thread:
- Inputs: system available RAM %, our process-tree RSS, per-tab renderer RSS, tab last-active,
  tab flags (pinned, audible, **performance-pinned**, has unsaved form data via
  `beforeunload` presence check).
- Discard when (available < 12 % of usable) OR (our RSS > 60 % of usable) OR (tab idle > 30 min
  and not exempt and total tabs > 20). Order: oldest idle, largest RSS first. Exempt: active tab,
  pinned, audible, performance-pinned, downloading.
- Discarding = destroy `CefBrowser`, keep `{url, title, favicon, scroll, form-less}` snapshot; tab
  shows dimmed; click restores. Never discard within 60 s of creation.
- **Performance-pin (unique to CAD Browser):** tab context menu → "Keep at full speed". v1
  mechanism: the tab is exempt from discard, and `CefBrowserHost::WasHidden(false)` is kept for
  it even when another tab is active, so its compositor and rAF loop stay alive. Chromium's
  `--disable-renderer-backgrounding` is process-global, so it is NOT used. Document the
  limitation (background priority still applies) honestly in `docs/stats.md`.

### 6.4 Session
`session.json` written on every tab change (debounced 500 ms) with a temp-file + rename. Restore
on start as **discarded** tabs (URL/title/favicon only) → cold start with 100 tabs costs one
renderer, not 100.

---

## 7. Efficiency work that actually makes it "faster than Chrome"

Ranked by expected payoff. Each gets a before/after measurement in `docs/flags.md`.
1. **No Google plumbing**: no sync, no safe-browsing pings, no field trials, no variations seed,
   no metrics, no component updater, no preconnect to search. Flags:
   `--disable-background-networking --disable-component-update --disable-sync
   --disable-features=Translate,MediaRouter,OptimizationHints,InterestFeedContentSuggestions,
   AutofillServerCommunication,CalculateNativeWinOcclusion?` (occlusion: keep, it saves GPU) —
   verify each against the current Chromium feature list; remove unknown ones (Chromium logs
   unknown features).
2. **Host blocker** (`request_filter.cc`): `CefResourceRequestHandler::OnBeforeResourceLoad`
   → `RV_CANCEL` if host ∈ blocklist (hash set of eTLD+1 and exact hosts, ~100k entries, loaded
   at startup from embedded `hosts.txt`, user additions in `%LOCALAPPDATA%\CADBrowser\blocklist.txt`).
   Per-site allow toggle in the menu. Counter shown in the overlay ("blocked: 37").
3. **Discard + cold-restore** (§6.3/6.4).
4. **No UI renderer**: native Views chrome (§1).
5. **Cache placement**: `CefSettings.cache_path` on the fastest local drive (default
   `%LOCALAPPDATA%\CADBrowser\Cache`); setting to relocate.
6. **GPU steering** (§4.3) — on hybrid laptops this alone is the difference between 15 and 60 fps in a CAD viewer.
7. **Startup**: no splash; window shown before first navigation; new-tab page is a static
   embedded HTML with no network.

What NOT to do: don't disable site isolation, don't disable the sandbox, don't set
`--single-process`, don't set `--disable-gpu-process-crash-limit`. Faster ≠ unsafe.

---

## 8. Stats overlay and stats data

### 8.1 Data flow
```
renderer (per tab)  --CefProcessMessage "cad.fps"-->  browser process  ---> StatsSampler (4 Hz)
   rAF timestamps, own PID                                ↑ PDH, DXGI, process tree, GlobalMemoryStatusEx
                                                          ↓ StatsSnapshot (struct)
                                    OverlayView.Update() → JS binding → cad://overlay DOM
                                    HamburgerMenu labels (on open)
```
- Renderer side (`renderer_app.cc`): on `OnContextCreated` for main frames only, inject a tiny
  script via `CefV8Context` that runs a `requestAnimationFrame` loop, accumulates frame
  intervals, and every 250 ms sends `{pid, frames, p50ms, p99ms, longest}` with
  `CefFrame::SendProcessMessage`. Off when the overlay is off (browser tells renderers via
  message) so idle cost is zero. **Never** modifies the page DOM or exposes globals (use a
  closure; the only footprint is one rAF callback).
- Browser side: `StatsSampler` thread merges per-tab FPS with system samples.

### 8.2 What the overlay shows (compact, monospace, semi-transparent, click-through)
```
FPS 60  (16.7 ms  p99 18.2)          ← active tab, from rAF. "—" if tab hasn't drawn
GPU NVIDIA RTX 4070  34% | VRAM 2.1/12 GB     ← active adapter, browser GPU % / system %
MEM tab 412 MB | browser 1.9 GB | sys 11.2/31.8 GB
CPU browser 8% | sys 21%
TABS 14 (3 sleeping) | blocked 37
```
Toggle: hamburger → "Stats overlay" (checkable), hotkey **Ctrl+Shift+S**. Position: settings
(top-left / top-right / bottom-right). Overlay is a `CefBrowserView` inside `AddOverlayView`
with `can_activate=false` and `CefBrowserSettings.background_color = transparent`, docked
`CEF_DOCKING_MODE_TOP_RIGHT`, size ~360×110 DIP.

### 8.3 Full details page
Hamburger → "System info" opens `cad://sysinfo` in a tab: GPU list with the preferred/active
marks, RAM table from §5, CPU model/cores (`GetLogicalProcessorInformationEx`), Chromium/CEF
version, active flags, links to `chrome://gpu` and `chrome://process-internals` (both work in
Alloy style — keep them reachable for debugging).

---

## 9. UI / theme spec (minimal, professional)

- Fonts: Segoe UI Variable (fallback Segoe UI), 13 px UI, 12 px tab titles. Overlay: Cascadia Mono 12 px.
- Layout: title bar merged with tab strip (frameless `CefWindow`, custom caption buttons, drag
  region = empty tab-strip area). Toolbar 36 px: ◀ ▶ ⟳ | omnibox (full width, 28 px, 4 px radius)
  | ☰ . No bookmarks bar by default (toggle). No profile avatar, no extensions puzzle, no side panel.
- Tabs: 34 px tall, square with 4 px top radius, 1 px separators, max 240 px, min 40 px (favicon only), active tab = surface colour + accent 2 px underline. No Chrome-style curves.
- Colour tokens (light / dark):
  - `bg` #F5F6F7 / #1B1D1F · `surface` #FFFFFF / #242628 · `border` #D9DCE0 / #35393D
  - `text` #1B1D1F / #E6E8EA · `muted` #6B7178 / #9AA0A6 · `accent` #2F6FED (both)
  - overlay bg rgba(20,22,24,0.72), text #E6E8EA, warn #F2B33D, bad #E5484D
- Follow Windows theme (`HKCU\...\Themes\Personalize\AppsUseLightTheme`, watch `WM_SETTINGCHANGE`); manual override in settings.
- Hamburger menu (CefMenuModel), in order:
  ```
  New tab            Ctrl+T
  New window         Ctrl+N
  ───────
  ☐ Stats overlay    Ctrl+Shift+S
  Active GPU: NVIDIA GeForce RTX 4070 ✓      (disabled label; or "⚠ Intel Iris — expected NVIDIA")
  RAM: 31.8 GB usable of 32 GB               (disabled label)
  System info…
  ───────
  History            Ctrl+H
  Downloads          Ctrl+J
  Bookmarks          ▸
  Zoom  −  100%  +
  Find…              Ctrl+F
  Print…             Ctrl+P
  Developer tools    F12
  ───────
  Settings
  About CAD Browser
  Exit
  ```
- Settings page (`cad://settings`) sections: General (startup, search engine, theme), Performance
  (GPU auto-select, force HW accel, discard thresholds, uncapped-FPS default, cache location),
  Privacy (blocklist on/off, allowlist, clear data), Overlay (position, opacity, which lines).
  Stored as JSON in `%LOCALAPPDATA%\CADBrowser\settings.json`.

---

## 10. Milestones with exit criteria (build in this order)

| # | Milestone | Exit criterion (must be demonstrated, not claimed) |
|---|---|---|
| M0 | Repo + toolchain | `cmake --preset release && cmake --build --preset release` produces `CADBrowser.exe` that opens one window with `cad://newtab`. CEF version pinned. `tests/` runs in CI-style script `build.ps1 -Test`. |
| M1 | `sys/` + `tools/dump_sysinfo` | Run on jmp's PC; RAM installed/usable/reserved/per-DIMM/max and GPU list + preferred adapter are correct vs Task Manager, BIOS, and dxdiag. SMBIOS parser has unit tests with 3 captured fixtures (desktop, hybrid laptop, VM). |
| M2 | Tabs + toolbar | 50 tabs open/close/switch smoothly; keyboard shortcuts; session restore as sleeping tabs; hover card shows per-tab memory. |
| M3 | Hamburger + System info page | Menu matches §9; GPU/RAM labels live; `cad://sysinfo` complete. |
| M4 | Stats overlay | Toggle on/off; FPS matches a known 60/144 Hz WebGL page; zero renderer cost when off (verify with the sampler: no `cad.fps` messages arrive). |
| M5 | GPU steering | On a hybrid laptop, active GPU = discrete after first run; the mismatch warning fires when HKCU key is deleted manually. |
| M6 | Efficiency pass | `docs/flags.md` filled with before/after on: cold start ms, idle RSS with 1 tab, RSS with 30 tabs (10 sleeping), Onshape/Fusion-web/three.js demo FPS vs Chrome on the same PC. Host blocker on. |
| M7 | Theme + polish | Light/dark, frameless window, DPI 100/150/200 %, installer (`Inno Setup` or plain zip) — zip is fine for v1. |

Benchmark pages for M6 (stable, public): three.js examples (webgl_animation_keyframes),
a WebGPU sample, Onshape demo document, Autodesk Viewer sample, and a Krunker-style WebGL game.
Record GPU model, driver, and Windows build with every number.

---

## 11. Key CEF APIs the implementer will need (for orientation)

- `CefInitialize / CefRunMessageLoop / CefShutdown`; `CefSettings` (`no_sandbox=false`, `cache_path`, `log_file`, `background_color`)
- `CefApp::OnBeforeCommandLineProcessing`, `OnRegisterCustomSchemes` (`cad`, standard+secure+cors-enabled+fetch-enabled)
- `CefSchemeHandlerFactory` + `CefResourceHandler` (or `CefStreamResourceHandler`) for embedded resources
- Views: `CefWindow::CreateTopLevelWindow`, `CefWindowDelegate` (`OnWindowCreated`, `IsFrameless`, `CanResize`, `GetInitialBounds`, `OnKeyEvent`), `CefBrowserView::CreateBrowserView`, `CefBrowserViewDelegate::GetBrowserRuntimeStyle → ALLOY`, `GetChromeToolbarType → CEF_CTT_NONE`, `OnPopupBrowserViewCreated` (route popups into new tabs), `CefPanel`, `CefBoxLayout`, `CefLabelButton`, `CefMenuButton` + `CefMenuModel`, `CefTextfield` (+ `CefTextfieldDelegate::OnKeyEvent` for Enter), `CefWindow::AddOverlayView`, `CefOverlayController`
- `CefClient` handlers: `CefLifeSpanHandler` (`OnBeforePopup` → new tab), `CefLoadHandler`, `CefDisplayHandler` (title/favicon/status/fullscreen), `CefRequestHandler` + `CefResourceRequestHandler` (blocker, byte counting), `CefDownloadHandler`, `CefKeyboardHandler`, `CefContextMenuHandler`, `CefFindHandler`, `CefFocusHandler`
- `CefBrowserHost`: `SetZoomLevel`, `Find`, `ShowDevTools`, `Print`, `WasHidden`, `SetAudioMuted`, `CloseBrowser`
- Renderer: `CefRenderProcessHandler::OnContextCreated`, `CefV8Context/CefV8Value`, `CefFrame::SendProcessMessage`, `CefProcessMessage`, `CefClient::OnProcessMessageReceived`
- `CefCommandLine` in `OnBeforeChildProcessLaunch` if per-child flags are ever needed

Chromium internal pages that work in Alloy and are useful during development: `chrome://gpu`,
`chrome://version`, `chrome://process-internals`, `chrome://net-export`.

---

## 12. Risks and how to handle them

- **CEF version churn**: APIs above were verified against CEF master headers on 2026-09-17
  (`AddOverlayView`, `GetBrowserRuntimeStyle`, `GetChromeToolbarType`, `cef_runtime_style_t`).
  Pin Stable 144 and only bump deliberately, one major at a time, reading CEF release notes.
- **Chromium feature names change**: unknown `--disable-features` entries are ignored with a
  log line; check the log on every CEF bump.
- **GPU steering not honoured** (OEM/driver panel overrides HKCU): detect via probe (§4.3.3),
  tell the user, link to Windows Graphics Settings. Don't silently claim success.
- **SMBIOS oddities** (VMs, some OEM boards report max capacity wrong): always show the source
  and fall back gracefully; never crash on a malformed table (fuzz the parser with truncated fixtures).
- **PDH GPU counters missing** (old WDDM 1.x drivers): overlay shows "GPU % n/a", never blocks.
- **Overlay eats input**: `can_activate=false` and no interactive elements; if hit-testing still
  intercepts, shrink the overlay to text bounds.
- **Frameless window edge cases** (snap layouts, Aero shake, maximise bounds): use
  `CefWindowDelegate::IsFrameless` + `CefWindow::SetDraggableRegions` for the empty tab-strip
  area and let CEF handle the frame; caption buttons are Views. Test Win+arrow snapping and
  Windows 11 snap layouts before calling M7 done. If this gets ugly, ship with the standard
  frame in v1 (`IsFrameless=false`) rather than sinking days into it.

---

## 13. Definition of "done" for v1

All milestones M0–M7 demonstrated on jmp's PC, `docs/flags.md` shows CAD Browser ≤ Chrome on
cold start, idle RSS, and 30-tab RSS, and ≥ Chrome FPS on the three WebGL/WebGPU benchmark pages
with the same GPU. Overlay numbers reconcile with Task Manager within 5 %.

---

# Build log: what changed from this plan, and why

Written while building, 2026-09-17. The sections above are the plan as
written; this section is what the code actually does where it differs, so the
two are never silently out of step.

## 1. The executable is CEF's, and our code is a DLL

PLAN section 1 assumed `CADBrowser.exe` would be our own executable linking
`cef_sandbox.lib`. The minimal CEF 144 distribution does not ship that library
at all, and linking it pins the build to one exact MSVC toolset.

CEF 144 offers a better route. `Release/bootstrap.exe` contains the sandbox
code; renamed to `CADBrowser.exe` it loads `CADBrowser.dll` from the same
directory and calls its exported `RunWinMain`, handing in a ready-made
`sandbox_info`. So:

- `CADBrowser.exe` is a renamed copy of `bootstrap.exe`.
- `CADBrowser.dll` is all of our code.
- The sandbox is fully on, and the build is not tied to a toolset version.

`USE_SANDBOX` defines `CEF_USE_BOOTSTRAP`, which selects the `RunWinMain`
entry point in `src/app/main_win.cc`.

## 2. GPU steering: the exports are not available to us

PLAN section 4.3 listed three mechanisms. The first, exporting
`NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`, **cannot be
done**: vendor drivers read those from the main executable's export table, and
the main executable is CEF's bootstrap, which we do not compile.

The per-executable registry preference is therefore the mechanism, not a
belt-and-braces addition to it. It is also the supported modern one, is what
the Windows Settings UI writes, needs no administrator rights, and applies to
every child process because they share our executable path.

Verified on the development machine:

```
HKCU\Software\Microsoft\DirectX\UserGpuPreferences
  <install folder>\CADBrowser.exe = GpuPreference=2;
```

and `cad://gpuprobe` reports

```
ANGLE (NVIDIA, NVIDIA GeForce RTX 4080 Laptop GPU (0x000027E0) Direct3D11 ...)
WebGPU adapter: nvidia lovelace
```

on a hybrid laptop whose other adapters are two Intel UHD entries. That is
M5's exit criterion met.

## 3. The overlay is a bar, not a corner panel

PLAN section 8.2 specified a 360x110 panel docked top-right. Built and shown,
it was too large, covered the toolbar, and read as an unorganised block. It is
now:

- a 26px single-line bar spanning the content width, docked to any edge;
- a narrow card when docked left or right, because a line of text cannot be
  read sideways;
- dragged to an edge, or double-clicked to cycle edges;
- positioned with `CEF_DOCKING_MODE_CUSTOM` against the **content area**, not
  the window, so it never covers the address bar.

Two CEF details worth keeping: a docked overlay ignores
`CefOverlayController::SetSize` and takes its size from the contents view's
preferred size, and a `CefBrowserView` has no natural preferred size, so
without a `GetPreferredSize` override the overlay is created, made visible,
and drawn at zero by zero. The bar also needs `can_activate = true` to receive
a drag, so focus is handed back to the page as soon as the drag ends.

## 4. Settings files may have a byte order mark

`settings.json` and `session.json` are meant to be edited by hand, and
Notepad, PowerShell `Set-Content` and most Windows editors write a UTF-8 BOM.
The JSON parser originally rejected it, which silently reset every setting the
moment someone opened the file and saved it. `JsonValue::Parse` now skips a
leading BOM, with a test.

## 5. List-valued command line switches must be merged

See `docs/flags.md`. Guarding `--disable-features` with `HasSwitch` dropped
every feature we asked to disable, because CEF sets that switch first. This is
the single highest-value thing to re-check after a CEF version bump.

## 6. Scope actually delivered in v1

Built and working: Alloy-style frameless window, native tab strip with
close/new/reorder-by-id, toolbar with omnibox, hamburger menu, cad:// internal
pages, host blocklist, tab discard with the full policy, session save and
restore as sleeping tabs, stats overlay, GPU detection and steering with
verification, the complete memory reporting from section 5, 73 unit tests.

Deliberately not built, with the pages saying so rather than pretending:

- **History and downloads** keep no database. Back/forward, Ctrl+Shift+T and
  session restore all work; there is no searchable history list.
- **Bookmarks** are a menu entry and a settings toggle only.
- **Find in page** has no find bar. `CefBrowserHost::Find` is wired in `Tab`
  but nothing drives it.
- **Favicons** are not drawn in the tab strip. The plumbing
  (`OnFaviconURLChange`) is there and lands in an empty handler.
- **Drag a tab out to a new window**, and the tab search popup (Ctrl+Shift+A),
  are not implemented.
- **The blocklist ships 202 curated hosts**, not a 100k dump. Users extend it
  through `%LOCALAPPDATA%\CADBrowser\blocklist.txt`.

## 7. Still to verify with a human at the keyboard

Everything below needs someone to actually use the window, and none of it can
be settled from a screenshot:

- Frameless window behaviour: Windows 11 snap layouts, Win+arrow snapping,
  Aero shake, dragging by the empty tab strip, resize from every edge.
- Dragging the overlay bar between edges. It depends on pointer capture
  continuing to deliver events after the pointer leaves a 26px-tall view;
  double-click-to-cycle exists as a fallback precisely because that is
  uncertain.
- Every keyboard accelerator.
- The hamburger menu opening and each command.
- Light theme, and DPI scaling at 125%, 150% and 200%.

---

# UI revision: auto-hiding chrome

Asked for after the first build: "remove almost all buttons and make them
appear on hover of the top nav bar, and only make that appear on hover near
the top so it will nearly be full screen mode all the time."

## What changed structurally

The tab strip and toolbar used to be children of the root panel, above the
content, in a vertical box layout. They are now a single `chrome_panel_` added
through `CefWindow::AddOverlayView` with custom docking, and the content panel
fills the entire window.

That is the important part: **the page is full height at all times**.
Revealing the chrome does not resize or reflow it. A CAD viewport that relaid
out every time the pointer crossed the top of the window would be unusable,
and a layout-based show/hide would do exactly that.

## Reveal model

Three states, driven by a 90 ms `GetCursorPos` poll on the UI thread. Views
gives a client no window-wide mouse-move callback, so polling is the
mechanism; at that rate it is about ten calls a second.

| Pointer | Result |
|---|---|
| Anywhere else | No chrome at all. Page only. |
| Within 5px of the top edge | Chrome appears, compact: tab titles and the address, no buttons. |
| Further down, over the chrome | Buttons appear: back, forward, reload, menu, tab close, new tab, caption buttons. |

Buttons stay while the pointer is anywhere over the chrome, so they do not
flicker as it moves between rows.

The chrome is also pinned open, regardless of the pointer, while the address
bar has keyboard focus or the menu is open. Hiding a menu's own anchor, or the
field someone is typing a URL into, would be actively hostile.

A new window shows its chrome for 2.6 seconds before it starts hiding. Without
that a first-time window opens as a bare rectangle with no visible way to
drive it, which reads as broken rather than minimal.

Ctrl+L reveals the chrome before focusing the address bar, since there is
otherwise nothing on screen to focus.

`Settings > General > Hide the toolbar until needed` turns the whole thing
off for a permanently visible toolbar.

## Consequence worth knowing

While the chrome is hidden there is no draggable region, so the window cannot
be moved by dragging, and there are no caption buttons. Brushing the top edge
brings them back. Alt+F4 still closes the window. This is the deliberate cost
of a browser that is "nearly full screen all the time".

## Verified

Captured off-screen, so nothing appeared on the owner's display:

- hidden: page content starts at y=0, no chrome of any kind;
- revealed: two tabs with close buttons, new-tab button, caption buttons,
  back/forward/reload, hamburger and the address bar, floating above page
  content that is visibly behind it.

Not verified, because it needs a pointer on a visible window: the hover
transitions themselves, and the compact-to-full step.
