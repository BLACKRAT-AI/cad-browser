// CAD Browser - the 4 Hz sampler behind the stats overlay.
//
// Owns one background thread that merges four sources into a StatsSnapshot:
// PDH counters, the DXGI adapter list, our process tree, and frame timings
// reported by the renderers. Everything the browser feeds in is thread safe;
// the callback fires on the sampler thread and the caller marshals it.
//
// When the overlay is off the thread parks on a condition variable and no
// sampling happens at all, which is what keeps the idle cost at zero.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "sys/gpu_info.h"
#include "sys/memory_info.h"
#include "sys/perf_counters.h"
#include "sys/process_tree.h"
#include "sys/stats.h"

namespace cad::sys {

class StatsSampler {
 public:
  using Callback = std::function<void(const StatsSnapshot&)>;

  StatsSampler();
  ~StatsSampler();
  StatsSampler(const StatsSampler&) = delete;
  StatsSampler& operator=(const StatsSampler&) = delete;

  // Starts the thread. The callback runs on the sampler thread roughly four
  // times a second while enabled.
  bool Start(Callback on_sample);
  void Stop();

  // Sampling only runs while enabled. Starts disabled.
  void SetEnabled(bool enabled);
  bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

  // ---- inputs, callable from any thread ----
  void ReportFrameStats(uint32_t renderer_pid,
                        double fps,
                        double frame_ms_p50,
                        double frame_ms_p99,
                        double longest_frame_ms);
  void SetActiveRendererPid(uint32_t pid);
  void SetTabCounts(int total, int sleeping);
  // Pull rather than push: the blocklist already counts what it blocked, and
  // keeping a second counter in step with it across threads would only create
  // a way for the two to disagree.
  void SetBlockedCountProvider(std::function<uint64_t()> provider);

  StatsSnapshot LastSnapshot() const;

  // Process ids of our whole family, for callers that need them (the GPU
  // probe, the tab hover card). Empty until the first sample.
  std::set<uint32_t> ProcessIds() const;

 private:
  struct FrameReport {
    double fps = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double longest = 0.0;
    uint64_t received_ms = 0;
  };

  void ThreadMain();
  void BuildSnapshot(StatsSnapshot* out);

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> enabled_{false};
  std::atomic<uint32_t> active_renderer_pid_{0};
  std::atomic<int> tab_total_{0};
  std::atomic<int> tab_sleeping_{0};

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<uint32_t, FrameReport> frame_reports_;
  StatsSnapshot last_snapshot_;
  std::set<uint32_t> last_pids_;
  std::function<uint64_t()> blocked_count_provider_;

  Callback callback_;

  // Owned by the sampler thread only.
  PerfCounters counters_;
  GpuMonitor gpu_;
  ProcessTreeMonitor processes_;
  uint64_t installed_memory_bytes_ = 0;
  uint64_t sequence_ = 0;

  // Not everything is worth sampling four times a second. Walking the process
  // table means enumerating every process on the machine, and the GPU engine
  // counter has around nine hundred instances to parse, both of which cost far
  // more than reading a memory status. Measured on the development machine,
  // doing all of it at 4 Hz kept the browser process at about 13% of a core
  // while the page itself was idle: the instrument cost more than the thing it
  // was measuring. Frame timing and memory stay at 4 Hz because they are cheap
  // and they are what moves; the rest runs once a second and is carried over
  // in between.
  static constexpr int kExpensiveEveryNTicks = 4;
  int tick_ = 0;
  ProcessTreeSnapshot last_tree_;
  bool have_tree_ = false;
};

}  // namespace cad::sys
