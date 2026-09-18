#include "sys/stats_sampler.h"

#include <algorithm>
#include <chrono>
#include <iterator>

#include "sys/win_util.h"

namespace cad::sys {
namespace {

constexpr int kSampleIntervalMs = 250;  // 4 Hz, per PLAN.md section 8
// A tab that has not reported a frame in this long is not drawing, so the
// overlay shows a dash rather than a stale rate.
constexpr uint64_t kFrameStatsStaleMs = 2000;

uint64_t MonotonicMs() {
  using clock = std::chrono::steady_clock;
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

}  // namespace

StatsSampler::StatsSampler() = default;

StatsSampler::~StatsSampler() {
  Stop();
}

bool StatsSampler::Start(Callback on_sample) {
  if (running_.load(std::memory_order_acquire))
    return false;
  callback_ = std::move(on_sample);
  running_.store(true, std::memory_order_release);
  thread_ = std::thread(&StatsSampler::ThreadMain, this);
  return true;
}

void StatsSampler::Stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel))
    return;
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void StatsSampler::SetEnabled(bool enabled) {
  const bool was = enabled_.exchange(enabled, std::memory_order_acq_rel);
  if (was != enabled)
    cv_.notify_all();
}

void StatsSampler::ReportFrameStats(uint32_t renderer_pid,
                                    double fps,
                                    double frame_ms_p50,
                                    double frame_ms_p99,
                                    double longest_frame_ms) {
  if (renderer_pid == 0)
    return;
  FrameReport report;
  report.fps = fps;
  report.p50 = frame_ms_p50;
  report.p99 = frame_ms_p99;
  report.longest = longest_frame_ms;
  report.received_ms = MonotonicMs();

  std::lock_guard<std::mutex> lock(mutex_);
  frame_reports_[renderer_pid] = report;
}

void StatsSampler::SetActiveRendererPid(uint32_t pid) {
  active_renderer_pid_.store(pid, std::memory_order_relaxed);
}

void StatsSampler::SetTabCounts(int total, int sleeping) {
  tab_total_.store(total, std::memory_order_relaxed);
  tab_sleeping_.store(sleeping, std::memory_order_relaxed);
}

void StatsSampler::SetBlockedCountProvider(std::function<uint64_t()> provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  blocked_count_provider_ = std::move(provider);
}

StatsSnapshot StatsSampler::LastSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_snapshot_;
}

std::set<uint32_t> StatsSampler::ProcessIds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_pids_;
}

void StatsSampler::ThreadMain() {
  // All of these are thread-affine to the sampler thread by construction.
  counters_.Initialize();
  gpu_.Initialize();
  installed_memory_bytes_ = QueryMemoryInfo().installed_bytes;

  while (running_.load(std::memory_order_acquire)) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Park while the overlay is off. No timer, no wakeups, no cost.
      cv_.wait(lock, [this] {
        return !running_.load(std::memory_order_acquire) ||
               enabled_.load(std::memory_order_acquire);
      });
      if (!running_.load(std::memory_order_acquire))
        break;
    }

    StatsSnapshot snapshot;
    BuildSnapshot(&snapshot);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_snapshot_ = snapshot;
    }
    if (callback_)
      callback_(snapshot);

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(kSampleIntervalMs), [this] {
      return !running_.load(std::memory_order_acquire) ||
             !enabled_.load(std::memory_order_acquire);
    });
  }
}

void StatsSampler::BuildSnapshot(StatsSnapshot* out) {
  // The expensive half: the PDH counter set and the process table walk. See
  // the note on kExpensiveEveryNTicks.
  const bool expensive = (tick_ % kExpensiveEveryNTicks) == 0 || !have_tree_;
  ++tick_;

  if (expensive) {
    counters_.Sample();
    gpu_.SampleLiveMemory();
    processes_.Sample(&last_tree_);
    have_tree_ = true;
  }
  const ProcessTreeSnapshot& tree = last_tree_;

  // Cheap enough to do every tick, and the numbers that actually move.
  const MemoryDynamic memory = QueryMemoryDynamic();
  const uint32_t active_pid =
      active_renderer_pid_.load(std::memory_order_relaxed);

  out->sequence = ++sequence_;
  out->ready = counters_.ready();

  // ---- frame timing for the active tab ----
  {
    const uint64_t now = MonotonicMs();
    std::lock_guard<std::mutex> lock(mutex_);
    // Drop reports from renderers that have gone away.
    for (auto it = frame_reports_.begin(); it != frame_reports_.end();) {
      it = (now - it->second.received_ms > 10 * kFrameStatsStaleMs)
               ? frame_reports_.erase(it)
               : std::next(it);
    }
    const auto found = frame_reports_.find(active_pid);
    if (found != frame_reports_.end() &&
        now - found->second.received_ms <= kFrameStatsStaleMs) {
      out->frame.valid = true;
      out->frame.fps = found->second.fps;
      out->frame.frame_ms_p50 = found->second.p50;
      out->frame.frame_ms_p99 = found->second.p99;
      out->frame.longest_frame_ms = found->second.longest;
      out->frame.renderer_pid = active_pid;
    }
    last_pids_ = tree.pids;
  }

  // ---- GPU ----
  const GpuInfo& gpu_info = gpu_.info();
  if (const GpuAdapter* adapter = gpu_info.preferred()) {
    out->gpu.valid = true;
    out->gpu.adapter_name = adapter->description;
    out->gpu.luid = adapter->luid;
    out->gpu.vram_total_bytes = adapter->dedicated_video_memory;
    out->gpu.counters_available = counters_.gpu_available();
    out->gpu.is_preferred = true;

    if (counters_.gpu_available() && counters_.ready()) {
      out->gpu.system_percent = counters_.SystemGpuPercent(adapter->luid);
      out->gpu.browser_percent =
          counters_.ProcessGpuPercent(tree.pids, adapter->luid);
      out->gpu.vram_used_bytes = counters_.AdapterDedicatedBytes(adapter->luid);
      out->gpu.vram_browser_bytes =
          counters_.ProcessDedicatedBytes(tree.pids, adapter->luid);
    }

    // If our processes are doing their GPU work on a different adapter than
    // the one we chose, say so rather than reporting an idle discrete card.
    int64_t busiest_luid = adapter->luid;
    double busiest = out->gpu.browser_percent;
    for (const GpuAdapter& other : gpu_info.adapters) {
      if (other.luid == adapter->luid || other.is_software)
        continue;
      const double percent = counters_.ProcessGpuPercent(tree.pids, other.luid);
      if (percent > busiest + 1.0) {
        busiest = percent;
        busiest_luid = other.luid;
      }
    }
    if (busiest_luid != adapter->luid) {
      for (const GpuAdapter& other : gpu_info.adapters) {
        if (other.luid == busiest_luid) {
          out->gpu.is_preferred = false;
          out->gpu.mismatch_note =
              "rendering on " + other.description + " instead";
          break;
        }
      }
    }
  }

  // ---- memory ----
  out->memory.browser_total_bytes = tree.total_working_set_bytes;
  out->memory.active_tab_bytes = tree.WorkingSetFor(active_pid);
  out->memory.system_used_bytes = memory.used_physical_bytes;
  out->memory.system_usable_bytes = memory.total_physical_bytes;
  out->memory.system_installed_bytes = installed_memory_bytes_;

  // ---- cpu ----
  out->cpu.valid = counters_.cpu_available() && counters_.ready();
  out->cpu.system_percent = counters_.SystemCpuPercent();
  out->cpu.browser_percent = tree.total_cpu_percent;
  for (const ProcessEntry& entry : tree.processes) {
    if (entry.kind == ProcessKind::kRenderer) {
      out->cpu.renderer_percent += entry.cpu_percent;
      if (entry.pid == active_pid) {
        out->cpu.active_tab_percent = entry.cpu_percent;
      }
    } else if (entry.kind == ProcessKind::kGpu) {
      out->cpu.gpu_process_percent += entry.cpu_percent;
    }
  }

  // ---- tabs ----
  out->tabs.total = tab_total_.load(std::memory_order_relaxed);
  out->tabs.sleeping = tab_sleeping_.load(std::memory_order_relaxed);
  {
    std::function<uint64_t()> provider;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      provider = blocked_count_provider_;
    }
    if (provider)
      out->tabs.blocked_requests = provider();
  }
}

}  // namespace cad::sys
