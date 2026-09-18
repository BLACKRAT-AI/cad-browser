// CAD Browser - GPU adapter enumeration and selection.
//
// PLAN.md section 4. Enumerates every DXGI adapter, records what each one is
// and how much memory it has, and picks the one a CAD or WebGL workload should
// be running on. Steering Chromium onto that adapter lives in gpu_preference.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sys/win_util.h"

namespace cad::sys {

// Live video memory. QueryVideoMemoryInfo reports the *calling process's*
// usage and the budget the OS is offering it, not a system-wide total. For the
// system-wide figure the overlay uses the GPU Adapter Memory PDH counter; see
// perf_counters.h.
struct GpuLiveMemory {
  bool valid = false;
  uint64_t budget_bytes = 0;
  uint64_t process_usage_bytes = 0;
  uint64_t available_for_reservation_bytes = 0;
  uint64_t current_reservation_bytes = 0;
};

struct GpuAdapter {
  int index = 0;                 // position in EnumAdapters1 order
  std::string description;       // "NVIDIA GeForce RTX 4070"
  std::string vendor_name;       // "NVIDIA"
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  uint32_t subsys_id = 0;
  uint32_t revision = 0;
  uint64_t dedicated_video_memory = 0;   // on-board VRAM
  uint64_t dedicated_system_memory = 0;
  uint64_t shared_system_memory = 0;     // system RAM the GPU may borrow
  int64_t luid = 0;                      // packed LUID, as PDH instance names use
  int32_t luid_high = 0;
  uint32_t luid_low = 0;
  bool is_software = false;              // WARP / Basic Render Driver
  bool has_output = false;               // a monitor is attached to this adapter
  int output_count = 0;
  std::string driver_version;            // user-mode driver, "32.0.15.7270"
  GpuLiveMemory live;

  bool is_discrete() const {
    return !is_software && dedicated_video_memory >= 512ull * 1024 * 1024;
  }
};

struct GpuInfo {
  std::vector<GpuAdapter> adapters;
  // Our choice: the adapter CAD and WebGL work should run on.
  int preferred_index = -1;
  // What DXGI itself ranks first for DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE.
  int os_high_performance_index = -1;
  std::string error;

  const GpuAdapter* preferred() const {
    if (preferred_index < 0 ||
        preferred_index >= static_cast<int>(adapters.size()))
      return nullptr;
    return &adapters[static_cast<size_t>(preferred_index)];
  }
};

// One-shot enumeration. Used by dump_sysinfo and the System page.
GpuInfo QueryGpuInfo();

std::string GpuVendorName(uint32_t vendor_id);

// Chooses the preferred adapter from an already populated list. Exposed so the
// selection rule can be exercised without any hardware.
int ChoosePreferredAdapter(const std::vector<GpuAdapter>& adapters);

// Keeps the DXGI objects alive so live memory can be sampled repeatedly
// without re-enumerating the whole adapter list.
class GpuMonitor {
 public:
  GpuMonitor();
  ~GpuMonitor();
  GpuMonitor(const GpuMonitor&) = delete;
  GpuMonitor& operator=(const GpuMonitor&) = delete;

  bool Initialize();
  const GpuInfo& info() const { return info_; }

  // Refreshes GpuAdapter::live for every adapter. Cheap; safe at 4 Hz.
  void SampleLiveMemory();

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  GpuInfo info_;
};

}  // namespace cad::sys
