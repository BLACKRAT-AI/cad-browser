// CAD Browser - process-wide state for the browser process.
//
// One owner for the things every window shares: settings, the blocklist, the
// stats sampler, and the hardware facts we read once at startup. Windows and
// tabs hold raw pointers into this; it outlives all of them because it is torn
// down after CefShutdown.
#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "browser/blocklist.h"
#include "browser/settings_store.h"
#include "sys/cpu_info.h"
#include "sys/gpu_info.h"
#include "sys/memory_info.h"
#include "sys/os_info.h"
#include "sys/stats_sampler.h"

namespace cad {

class MainWindow;

class AppState {
 public:
  // Browser process only. Null before Create() and after Destroy().
  static AppState* Get();
  static void Create();
  static void Destroy();

  AppState();
  ~AppState();
  AppState(const AppState&) = delete;
  AppState& operator=(const AppState&) = delete;

  // Reads settings.json and resolves the user data directory. Runs before
  // CefInitialize because the command-line switches depend on two of those
  // settings and the cache path has to be handed to CefSettings.
  void PreInitialize();

  // Reads the hardware, applies the GPU preference, loads the blocklist and
  // starts the sampler. Runs on the UI thread once CEF is up.
  void Initialize();
  void Shutdown();

  // ---- settings ----
  SettingsStore& settings_store() { return settings_store_; }
  const Settings& settings() const { return settings_store_.settings(); }
  Settings& mutable_settings() { return settings_store_.mutable_settings(); }
  void SaveSettings();
  // Re-applies anything that can change at runtime (theme, blocklist state,
  // overlay, discard thresholds) and persists.
  void ApplySettingsChanges();

  // ---- shared services ----
  Blocklist& blocklist() { return blocklist_; }
  sys::StatsSampler& sampler() { return sampler_; }

  // ---- hardware, read once ----
  const sys::GpuInfo& gpu_info() const { return gpu_info_; }
  const sys::MemoryInfo& memory_info() const { return memory_info_; }
  const sys::CpuInfo& cpu_info() const { return cpu_info_; }
  const sys::OsInfo& os_info() const { return os_info_; }
  const sys::MachineInfo& machine_info() const { return machine_info_; }

  // ---- paths ----
  const std::wstring& user_data_dir() const { return user_data_dir_; }
  std::wstring SettingsPath() const;
  std::wstring SessionPath() const;
  std::wstring CachePath() const;
  std::wstring UserBlocklistPath() const;

  // ---- windows ----
  void AddWindow(MainWindow* window);
  void RemoveWindow(MainWindow* window);
  const std::vector<MainWindow*>& windows() const { return windows_; }

  // ---- GPU verification (PLAN.md section 4.3 step 3) ----
  // Set from the cad://gpuprobe page once WebGL reports its real renderer.
  void SetProbedGpuRenderer(const std::string& renderer);
  const std::string& probed_gpu_renderer() const {
    return probed_gpu_renderer_;
  }
  bool probe_completed() const { return probe_completed_; }
  // True when the adapter actually rendering matches the one we chose.
  bool gpu_matches_preferred() const;

  // One-line summaries for the hamburger menu.
  std::string GpuMenuLine() const;
  std::string MemoryMenuLine() const;

  // Maximum V8 old-space per renderer, in megabytes. Chromium's own cap is
  // about 4 GB whatever the machine has, which a large CAD assembly exceeds.
  // Derived from usable RAM unless the user has set a value.
  int RecommendedJsHeapMb() const;
  // GPU memory budget in megabytes, derived from the chosen adapter's VRAM.
  int RecommendedGpuMemoryMb() const;

  // Overlay on/off, kept here because it is global rather than per window.
  void SetOverlayEnabled(bool enabled);
  bool overlay_enabled() const { return settings().overlay_enabled; }

  // Delivered on the UI thread for each sampler tick.
  void OnStatsSnapshot(const sys::StatsSnapshot& snapshot);

  // ---- profiling ----
  // --cad-profile-log=PATH writes every sample to a CSV: frame times, GPU,
  // memory and CPU at 4 Hz. Utilisation alone does not say whether something
  // feels bad; the frame-time percentiles do, and only the renderer knows
  // them, so they have to come from inside the browser rather than from an
  // external sampler.
  bool profiling() const { return profile_file_ != nullptr; }
  // True when the renderers should run the frame-timing probe: either the
  // overlay is showing it, or we are profiling.
  bool frame_probe_should_run() const;

 private:
  void LoadBlocklist();
  void OpenProfileLog();
  void WriteProfileRow(const sys::StatsSnapshot& snapshot);
  void CloseProfileLog();
  void ApplyGpuPreference();

  SettingsStore settings_store_;
  Blocklist blocklist_;
  sys::StatsSampler sampler_;

  sys::GpuInfo gpu_info_;
  sys::MemoryInfo memory_info_;
  sys::CpuInfo cpu_info_;
  sys::OsInfo os_info_;
  sys::MachineInfo machine_info_;

  std::wstring user_data_dir_;
  std::vector<MainWindow*> windows_;

  std::string probed_gpu_renderer_;
  bool probe_completed_ = false;
  bool pre_initialized_ = false;
  bool initialized_ = false;

  // Open only while --cad-profile-log is in effect. Rows are appended from
  // OnStatsSnapshot on the UI thread, so no locking is needed.
  std::FILE* profile_file_ = nullptr;
  uint64_t profile_start_ms_ = 0;
};

}  // namespace cad
