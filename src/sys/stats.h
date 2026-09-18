// CAD Browser - the snapshot the overlay and the menu render.
//
// One plain struct, produced by the sampler thread and consumed on the UI
// thread. Every field is defined in docs/stats.md; if a number here cannot be
// measured it stays invalid rather than being guessed at.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad::sys {

// Frame timing for one tab, derived from requestAnimationFrame in its
// renderer. Invalid when the tab has not drawn recently.
struct FrameStats {
  bool valid = false;
  double fps = 0.0;
  double frame_ms_p50 = 0.0;
  double frame_ms_p99 = 0.0;
  double longest_frame_ms = 0.0;
  uint32_t renderer_pid = 0;
};

struct GpuStats {
  bool valid = false;
  std::string adapter_name;
  int64_t luid = 0;
  bool counters_available = false;   // PDH GPU counters present
  double system_percent = 0.0;
  double browser_percent = 0.0;
  uint64_t vram_used_bytes = 0;      // system-wide, from PDH
  uint64_t vram_total_bytes = 0;     // DXGI dedicated video memory
  uint64_t vram_browser_bytes = 0;   // attributed to our processes
  bool is_preferred = false;         // running on the adapter we chose
  std::string mismatch_note;         // set when it is not
};

struct MemoryStats {
  uint64_t active_tab_bytes = 0;
  uint64_t browser_total_bytes = 0;
  uint64_t system_used_bytes = 0;
  uint64_t system_usable_bytes = 0;
  uint64_t system_installed_bytes = 0;
};

struct CpuStats {
  bool valid = false;
  double system_percent = 0.0;
  double browser_percent = 0.0;
  // Split of browser_percent by process role. All are percentages of one
  // logical core, so on a 32-thread machine a fully busy renderer thread
  // reads 100 while the machine itself is only 3% used. Kept apart because a
  // single total cannot tell a stalled renderer from a busy GPU process.
  double renderer_percent = 0.0;      // every renderer together
  double gpu_process_percent = 0.0;   // the GPU process
  double active_tab_percent = 0.0;    // the renderer hosting the active tab
};

struct TabStats {
  int total = 0;
  int sleeping = 0;
  uint64_t blocked_requests = 0;
};

struct StatsSnapshot {
  bool ready = false;
  uint64_t sequence = 0;
  FrameStats frame;
  GpuStats gpu;
  MemoryStats memory;
  CpuStats cpu;
  TabStats tabs;
};

}  // namespace cad::sys
