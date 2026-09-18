// CAD Browser - Windows performance counters for GPU and CPU.
//
// PLAN.md section 4.1. DXGI can tell us what an adapter *is* and what the
// calling process is using, but not what the machine as a whole is doing with
// it. The WDDM performance counters can, and they are the same source Task
// Manager reads, which is what makes the overlay reconcilable with it.
//
// Instance naming, which everything here depends on:
//   GPU Engine          pid_1234_luid_0x00000000_0x0000F4B3_phys_0_eng_3_engtype_3D
//   GPU Adapter Memory  luid_0x00000000_0x0000F4B3_phys_0
//   GPU Process Memory  pid_1234_luid_0x00000000_0x0000F4B3_phys_0
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace cad::sys {

// Decoded pieces of a GPU counter instance name.
struct GpuInstanceKey {
  bool has_pid = false;
  uint32_t pid = 0;
  int64_t luid = 0;
  int phys = 0;
  int engine = -1;
  std::string engine_type;  // "3D", "VideoDecode", "Copy", "Compute", ...
};

// Parses an instance name. Returns false when it does not match the WDDM
// naming scheme at all.
bool ParseGpuInstanceName(const std::wstring& name, GpuInstanceKey* out);

struct GpuEngineSample {
  GpuInstanceKey key;
  double utilization_percent = 0.0;
};

struct GpuMemorySample {
  GpuInstanceKey key;
  uint64_t dedicated_bytes = 0;
  uint64_t shared_bytes = 0;
};

// Owns one PDH query. Rate counters need two collections before they produce a
// number, so the first Sample() after Initialize() reports not-ready.
class PerfCounters {
 public:
  PerfCounters();
  ~PerfCounters();
  PerfCounters(const PerfCounters&) = delete;
  PerfCounters& operator=(const PerfCounters&) = delete;

  bool Initialize();
  void Shutdown();

  // Collects a new set of values. Returns false if nothing usable was read.
  bool Sample();

  bool gpu_available() const { return gpu_available_; }
  bool cpu_available() const { return cpu_available_; }
  bool ready() const { return sample_count_ >= 2; }
  const std::string& error() const { return error_; }

  // ---- aggregations over the most recent sample ----

  // Whole-machine GPU load for one adapter, 0..100. Takes the busiest engine
  // rather than the sum: engines run concurrently, so summing them produces
  // numbers above 100 that mean nothing to a reader.
  double SystemGpuPercent(int64_t luid) const;

  // Same, restricted to the given process ids.
  double ProcessGpuPercent(const std::set<uint32_t>& pids, int64_t luid) const;

  // System-wide dedicated video memory in use on one adapter.
  uint64_t AdapterDedicatedBytes(int64_t luid) const;
  uint64_t AdapterSharedBytes(int64_t luid) const;

  // Dedicated video memory attributed to the given processes.
  uint64_t ProcessDedicatedBytes(const std::set<uint32_t>& pids,
                                 int64_t luid) const;

  // Whole-machine CPU utilisation, 0..100.
  double SystemCpuPercent() const { return system_cpu_percent_; }

  const std::vector<GpuEngineSample>& engine_samples() const {
    return engine_samples_;
  }

 private:
  struct Impl;
  Impl* impl_ = nullptr;

  std::vector<GpuEngineSample> engine_samples_;
  std::vector<GpuMemorySample> adapter_memory_samples_;
  std::vector<GpuMemorySample> process_memory_samples_;
  double system_cpu_percent_ = 0.0;

  bool gpu_available_ = false;
  bool cpu_available_ = false;
  int sample_count_ = 0;
  std::string error_;
};

}  // namespace cad::sys
