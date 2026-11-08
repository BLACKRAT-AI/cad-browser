#include "sys/win_util.h"

#include "app/app_state.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <share.h>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "resources/generated_resources.h"
#include "sys/gpu_preference.h"
#include "ui/main_window.h"

namespace cad {
namespace {

AppState* g_app_state = nullptr;

// Compares what WebGL says it is rendering with the adapter we chose. Driver
// strings are not identical to DXGI descriptions, so this looks for the
// distinguishing words rather than requiring an exact match.
bool RendererLooksLikeAdapter(const std::string& renderer,
                              const std::string& adapter) {
  if (renderer.empty() || adapter.empty()) {
    return false;
  }
  // Split the adapter description into words and require that the meaningful
  // ones all appear. "NVIDIA GeForce RTX 4080 Laptop GPU" against
  // "ANGLE (NVIDIA, NVIDIA GeForce RTX 4080 Laptop GPU (0x000027E0) ...)".
  size_t matched = 0;
  size_t considered = 0;
  size_t start = 0;
  while (start <= adapter.size()) {
    size_t end = adapter.find(' ', start);
    if (end == std::string::npos) {
      end = adapter.size();
    }
    const std::string word = adapter.substr(start, end - start);
    start = end + 1;
    if (word.size() < 3 || word == "GPU" || word == "Graphics") {
      continue;
    }
    ++considered;
    if (sys::ContainsNoCase(renderer, word)) {
      ++matched;
    }
  }
  if (considered == 0) {
    return false;
  }
  // Allow one word to differ: vendors reorder and abbreviate.
  return matched + 1 >= considered;
}

}  // namespace

AppState::AppState() = default;

AppState::~AppState() = default;

AppState* AppState::Get() {
  return g_app_state;
}

void AppState::Create() {
  if (!g_app_state) {
    g_app_state = new AppState();
  }
}

void AppState::Destroy() {
  delete g_app_state;
  g_app_state = nullptr;
}

std::wstring AppState::SettingsPath() const {
  return user_data_dir_.empty() ? std::wstring()
                                : user_data_dir_ + L"\\settings.json";
}

std::wstring AppState::SessionPath() const {
  return user_data_dir_.empty() ? std::wstring()
                                : user_data_dir_ + L"\\session.json";
}

std::wstring AppState::UserBlocklistPath() const {
  return user_data_dir_.empty() ? std::wstring()
                                : user_data_dir_ + L"\\blocklist.txt";
}

std::wstring AppState::CachePath() const {
  const std::string& configured = settings().cache_path;
  if (!configured.empty()) {
    return sys::ToUtf16(configured);
  }
  return user_data_dir_.empty() ? std::wstring() : user_data_dir_ + L"\\Cache";
}

void AppState::PreInitialize() {
  if (pre_initialized_) {
    return;
  }
  pre_initialized_ = true;
  user_data_dir_ = sys::UserDataDirW();
  settings_store_.Load(SettingsPath());

  // Hardware facts, read once, before CefInitialize: the V8 heap and GPU
  // memory switches are derived from them and have to be on the command line
  // before Chromium starts. None of this changes while the browser runs.
  memory_info_ = sys::QueryMemoryInfo();
  cpu_info_ = sys::QueryCpuInfo();
  os_info_ = sys::QueryOsInfo();
  machine_info_ = sys::QueryMachineInfo();
  gpu_info_ = sys::QueryGpuInfo();
}

int AppState::RecommendedJsHeapMb() const {
  // Zero, and deliberately. This used to derive a heap from installed RAM and
  // pass --max-old-space-size=12288 on a 64 GB machine, on the belief that
  // Chromium's roughly 4 GB ceiling was an arbitrary default that a CAD
  // browser should raise. It is not: Chromium builds V8 with pointer
  // compression, which confines the heap to a 4 GB cage. Asking for more does
  // not raise anything, it leaves V8 configured for a limit it can never
  // reach, and it fails badly rather than gracefully.
  //
  // Measured by allocating retained arrays until the tab dies, two runs each:
  //
  //   --max-old-space-size=12288   2,688 MB then 2,944 MB, renderer crashed
  //   --max-old-space-size=4096    2,816 MB then 3,456 MB, renderer crashed
  //   no override (Chromium default)  see below
  //
  // Stock Chrome reached 3,840 MB twice without the tab dying at all. Ours
  // died with "The page crashed (Crashpad_NotConnectedToHandler)" rather than
  // a clean V8 out-of-memory, which is the signature of an allocation failing
  // inside the cage instead of the heap limit being reached. So the feature
  // this browser shouted loudest about was making large assemblies *more*
  // likely to be killed, not less.
  //
  // An explicit value in settings.json is still honoured for anyone who wants
  // to experiment, but it is clamped to what the cage can actually hold.
  if (settings().js_heap_mb > 0) {
    return std::min(settings().js_heap_mb, 4096);
  }
  return 0;
}

int AppState::RecommendedGpuMemoryMb() const {
  if (!settings().gpu_large_memory_budget) {
    return 0;
  }
  const sys::GpuAdapter* adapter = gpu_info_.preferred();
  if (!adapter || adapter->is_software) {
    return 0;
  }
  const uint64_t vram_mb =
      adapter->dedicated_video_memory / (1024ull * 1024ull);
  if (vram_mb < 2048) {
    return 0;  // not enough to be worth overriding the default
  }
  // Six tenths of the card, leaving room for the desktop compositor and
  // anything else using the GPU.
  const int64_t budget = std::clamp<int64_t>(
      static_cast<int64_t>(vram_mb * 6 / 10), 1024, 8192);
  return static_cast<int>(budget);
}

void AppState::Initialize() {
  if (initialized_) {
    return;
  }
  initialized_ = true;
  PreInitialize();

  ApplyGpuPreference();
  LoadBlocklist();

  sampler_.SetBlockedCountProvider(
      [this]() { return blocklist_.blocked_count(); });

  // The sampler thread hands snapshots back on the UI thread, where the views
  // that render them live.
  sampler_.Start([](const sys::StatsSnapshot& snapshot) {
    CefPostTask(TID_UI, base::BindOnce(
                            [](sys::StatsSnapshot delivered) {
                              if (AppState* state = AppState::Get()) {
                                state->OnStatsSnapshot(delivered);
                              }
                            },
                            snapshot));
  });
  sampler_.SetEnabled(settings().overlay_enabled);

  // After the sampler is running, so that turning profiling on can enable it.
  OpenProfileLog();

  if (!settings().first_run_complete) {
    mutable_settings().first_run_complete = true;
    SaveSettings();
  }
}

void AppState::Shutdown() {
  CloseProfileLog();
  sampler_.Stop();
  SaveSettings();
  windows_.clear();
  initialized_ = false;
}

void AppState::SaveSettings() {
  // Keep the persisted allowlist in step with the live blocklist.
  const std::set<std::string> allowed = blocklist_.allowed_sites();
  mutable_settings().allowed_sites.assign(allowed.begin(), allowed.end());
  settings_store_.Save();
}

void AppState::SetAutoCapture(bool enabled) {
  auto_capture_ = enabled;
  for (MainWindow* window : windows_) {
    window->OnAutoCaptureChanged(enabled);
  }
}

void AppState::ApplySettingsChanges() {
  blocklist_.SetEnabled(settings().blocklist_enabled);
  sampler_.SetEnabled(settings().overlay_enabled);
  ApplyGpuPreference();
  SaveSettings();

  for (MainWindow* window : windows_) {
    window->OnSettingsChanged();
  }
}

void AppState::LoadBlocklist() {
  // The built-in list ships in the binary; the user's own additions live
  // beside the settings file so they survive an upgrade.
  if (const resources::Entry* entry =
          resources::Find("blocklists/hosts.txt")) {
    blocklist_.LoadFromText(std::string(
        reinterpret_cast<const char*>(entry->data), entry->size));
  }
  blocklist_.LoadFromFile(UserBlocklistPath());

  for (const std::string& site : settings().allowed_sites) {
    blocklist_.AllowSite(site);
  }
  blocklist_.SetEnabled(settings().blocklist_enabled);
}

void AppState::ApplyGpuPreference() {
  // PLAN.md section 4.3: Windows decides the adapter per executable, so this
  // is the lever that actually moves rendering onto the discrete GPU.
  if (settings().gpu_prefer_high_performance) {
    sys::SetGpuPreference(sys::GpuPreference::kHighPerformance);
  } else {
    sys::ClearGpuPreference();
  }
}

void AppState::AddWindow(MainWindow* window) {
  if (window &&
      std::find(windows_.begin(), windows_.end(), window) == windows_.end()) {
    windows_.push_back(window);
  }
}

void AppState::RemoveWindow(MainWindow* window) {
  windows_.erase(std::remove(windows_.begin(), windows_.end(), window),
                 windows_.end());
}

void AppState::SetProbedGpuRenderer(const std::string& renderer) {
  probed_gpu_renderer_ = renderer;
  probe_completed_ = true;
  for (MainWindow* window : windows_) {
    window->OnGpuStatusChanged();
  }
}

bool AppState::gpu_matches_preferred() const {
  const sys::GpuAdapter* preferred = gpu_info_.preferred();
  if (!preferred || !probe_completed_) {
    return true;  // nothing to contradict yet
  }
  return RendererLooksLikeAdapter(probed_gpu_renderer_, preferred->description);
}

std::string AppState::GpuMenuLine() const {
  const sys::GpuAdapter* preferred = gpu_info_.preferred();
  if (!preferred) {
    return "Active GPU: unknown";
  }
  if (!probe_completed_) {
    return "Active GPU: " + preferred->description + " (checking)";
  }
  if (gpu_matches_preferred()) {
    return "Active GPU: " + preferred->description + "  OK";
  }
  return "Active GPU: not " + preferred->description + " - click to fix";
}

std::string AppState::MemoryMenuLine() const {
  if (memory_info_.installed_bytes == 0) {
    return "RAM: unavailable";
  }
  return "RAM: " + sys::FormatGB(memory_info_.usable_bytes) + " usable of " +
         sys::FormatGB(memory_info_.installed_bytes) + " installed";
}

void AppState::SetOverlayEnabled(bool enabled) {
  mutable_settings().overlay_enabled = enabled;
  // Profiling keeps the sampler running even with the overlay hidden, so a
  // measurement run is not affected by the overlay being in the way.
  sampler_.SetEnabled(enabled || profiling());
  SaveSettings();
  for (MainWindow* window : windows_) {
    window->SetOverlayVisible(enabled);
  }
}

void AppState::OnStatsSnapshot(const sys::StatsSnapshot& snapshot) {
  WriteProfileRow(snapshot);
  for (MainWindow* window : windows_) {
    window->OnStatsSnapshot(snapshot);
  }
}


namespace {

uint64_t MonotonicMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

double Mb(uint64_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace

bool AppState::frame_probe_should_run() const {
  return overlay_enabled() || profiling();
}

void AppState::OpenProfileLog() {
  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  if (!command_line || !command_line->HasSwitch("cad-profile-log")) {
    return;
  }
  const std::wstring path =
      sys::ToUtf16(command_line->GetSwitchValue("cad-profile-log").ToString());
  if (path.empty()) {
    return;
  }
  // Opened shared: the whole point of a profiling log is to be watched while
  // the run is happening, and the default exclusive open makes that impossible.
  profile_file_ = ::_wfsopen(path.c_str(), L"w", _SH_DENYNO);
  if (!profile_file_) {
    return;
  }
  profile_start_ms_ = MonotonicMs();

  // One row per sampler tick. Frame times come from the renderer's own
  // requestAnimationFrame probe, which is the only place they exist: an
  // external sampler can see that a core is busy but not that a frame was
  // late, and lateness is what a person actually feels.
  std::fputs(
      "ms,fps,frame_p50_ms,frame_p99_ms,frame_longest_ms,"
      "cpu_system_pct,cpu_browser_pct,cpu_renderers_pct,cpu_active_tab_pct,"
      "cpu_gpu_process_pct,gpu_system_pct,gpu_browser_pct,"
      "vram_used_mb,vram_browser_mb,vram_total_mb,"
      "active_tab_mb,browser_total_mb,system_used_mb,"
      "tabs,sleeping,blocked\n",
      profile_file_);
  std::fflush(profile_file_);

  // Profiling implies sampling, whatever the overlay is set to.
  sampler_.SetEnabled(true);
}

void AppState::WriteProfileRow(const sys::StatsSnapshot& snapshot) {
  if (!profile_file_) {
    return;
  }
  std::fprintf(
      profile_file_,
      "%llu,%.2f,%.3f,%.3f,%.3f,"
      "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
      "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%llu\n",
      static_cast<unsigned long long>(MonotonicMs() - profile_start_ms_),
      snapshot.frame.valid ? snapshot.frame.fps : -1.0,
      snapshot.frame.valid ? snapshot.frame.frame_ms_p50 : -1.0,
      snapshot.frame.valid ? snapshot.frame.frame_ms_p99 : -1.0,
      snapshot.frame.valid ? snapshot.frame.longest_frame_ms : -1.0,
      snapshot.cpu.system_percent, snapshot.cpu.browser_percent,
      snapshot.cpu.renderer_percent, snapshot.cpu.active_tab_percent,
      snapshot.cpu.gpu_process_percent, snapshot.gpu.system_percent,
      snapshot.gpu.browser_percent, Mb(snapshot.gpu.vram_used_bytes),
      Mb(snapshot.gpu.vram_browser_bytes), Mb(snapshot.gpu.vram_total_bytes),
      Mb(snapshot.memory.active_tab_bytes),
      Mb(snapshot.memory.browser_total_bytes),
      Mb(snapshot.memory.system_used_bytes), snapshot.tabs.total,
      snapshot.tabs.sleeping,
      static_cast<unsigned long long>(snapshot.tabs.blocked_requests));
  // Flushed every row: a profiling run usually ends with the window being
  // closed, and a buffered tail would be the part we wanted.
  std::fflush(profile_file_);
}

void AppState::CloseProfileLog() {
  if (profile_file_) {
    std::fclose(profile_file_);
    profile_file_ = nullptr;
  }
}

}  // namespace cad
