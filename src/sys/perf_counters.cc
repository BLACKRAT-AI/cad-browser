// windows.h must precede pdh.h; see the note in process_tree.cc.
#include "sys/win_util.h"

#include "sys/perf_counters.h"

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <map>

namespace cad::sys {
namespace {

// Reads an unsigned decimal number starting at `pos`, advancing it past the
// digits. Returns false when there is no digit there at all.
bool ReadDecimal(const std::wstring& text, size_t& pos, uint64_t* out) {
  const size_t start = pos;
  uint64_t value = 0;
  while (pos < text.size() && text[pos] >= L'0' && text[pos] <= L'9') {
    value = value * 10 + static_cast<uint64_t>(text[pos] - L'0');
    ++pos;
  }
  if (pos == start)
    return false;
  *out = value;
  return true;
}

// Reads "0x0000F4B3" at `pos`.
bool ReadHex(const std::wstring& text, size_t& pos, uint64_t* out) {
  if (pos + 2 > text.size() || text[pos] != L'0' ||
      (text[pos + 1] != L'x' && text[pos + 1] != L'X')) {
    return false;
  }
  pos += 2;
  const size_t start = pos;
  uint64_t value = 0;
  while (pos < text.size()) {
    const wchar_t c = text[pos];
    int digit;
    if (c >= L'0' && c <= L'9')
      digit = c - L'0';
    else if (c >= L'a' && c <= L'f')
      digit = 10 + (c - L'a');
    else if (c >= L'A' && c <= L'F')
      digit = 10 + (c - L'A');
    else
      break;
    value = value * 16 + static_cast<uint64_t>(digit);
    ++pos;
  }
  if (pos == start)
    return false;
  *out = value;
  return true;
}

bool TokenAt(const std::wstring& text, size_t pos, const wchar_t* token) {
  return text.compare(pos, ::wcslen(token), token) == 0;
}

}  // namespace

bool ParseGpuInstanceName(const std::wstring& name, GpuInstanceKey* out) {
  if (!out)
    return false;
  *out = GpuInstanceKey();

  size_t pos = 0;
  if (TokenAt(name, pos, L"pid_")) {
    pos += 4;
    uint64_t pid = 0;
    if (!ReadDecimal(name, pos, &pid))
      return false;
    out->has_pid = true;
    out->pid = static_cast<uint32_t>(pid);
    if (pos < name.size() && name[pos] == L'_')
      ++pos;
  }

  if (!TokenAt(name, pos, L"luid_"))
    return false;
  pos += 5;

  uint64_t high = 0;
  if (!ReadHex(name, pos, &high))
    return false;
  if (pos >= name.size() || name[pos] != L'_')
    return false;
  ++pos;
  uint64_t low = 0;
  if (!ReadHex(name, pos, &low))
    return false;

  // The high part is a signed 32-bit value in the original LUID.
  LARGE_INTEGER packed;
  packed.HighPart = static_cast<LONG>(static_cast<uint32_t>(high));
  packed.LowPart = static_cast<DWORD>(low);
  out->luid = packed.QuadPart;

  if (pos < name.size() && name[pos] == L'_')
    ++pos;
  if (TokenAt(name, pos, L"phys_")) {
    pos += 5;
    uint64_t phys = 0;
    if (ReadDecimal(name, pos, &phys))
      out->phys = static_cast<int>(phys);
    if (pos < name.size() && name[pos] == L'_')
      ++pos;
  }

  if (TokenAt(name, pos, L"eng_")) {
    pos += 4;
    uint64_t eng = 0;
    if (ReadDecimal(name, pos, &eng))
      out->engine = static_cast<int>(eng);
    if (pos < name.size() && name[pos] == L'_')
      ++pos;
  }

  if (TokenAt(name, pos, L"engtype_")) {
    pos += 8;
    out->engine_type = ToUtf8(name.substr(pos));
  }

  return true;
}

struct PerfCounters::Impl {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER gpu_engine = nullptr;
  PDH_HCOUNTER adapter_dedicated = nullptr;
  PDH_HCOUNTER adapter_shared = nullptr;
  PDH_HCOUNTER process_dedicated = nullptr;
  PDH_HCOUNTER cpu_total = nullptr;
  std::vector<uint8_t> scratch;
};

PerfCounters::PerfCounters() : impl_(new Impl()) {}

PerfCounters::~PerfCounters() {
  Shutdown();
  delete impl_;
  impl_ = nullptr;
}

void PerfCounters::Shutdown() {
  if (impl_ && impl_->query) {
    ::PdhCloseQuery(impl_->query);
    impl_->query = nullptr;
  }
}

bool PerfCounters::Initialize() {
  if (::PdhOpenQueryW(nullptr, 0, &impl_->query) != ERROR_SUCCESS) {
    error_ = "PdhOpenQuery failed";
    return false;
  }

  auto add = [this](const wchar_t* path, PDH_HCOUNTER* counter) -> bool {
    // The English variant keeps us independent of the display language, where
    // the localised counter names differ.
    const PDH_STATUS status =
        ::PdhAddEnglishCounterW(impl_->query, path, 0, counter);
    return status == ERROR_SUCCESS;
  };

  gpu_available_ =
      add(L"\\GPU Engine(*)\\Utilization Percentage", &impl_->gpu_engine);
  if (gpu_available_) {
    add(L"\\GPU Adapter Memory(*)\\Dedicated Usage", &impl_->adapter_dedicated);
    add(L"\\GPU Adapter Memory(*)\\Shared Usage", &impl_->adapter_shared);
    add(L"\\GPU Process Memory(*)\\Dedicated Usage", &impl_->process_dedicated);
  } else {
    // WDDM 1.x drivers and some virtual GPUs do not publish these. The overlay
    // shows "n/a" rather than failing.
    error_ = "GPU performance counters are not available on this system";
  }

  cpu_available_ =
      add(L"\\Processor Information(_Total)\\% Processor Time", &impl_->cpu_total);
  if (!cpu_available_) {
    cpu_available_ =
        add(L"\\Processor(_Total)\\% Processor Time", &impl_->cpu_total);
  }

  if (!gpu_available_ && !cpu_available_) {
    Shutdown();
    return false;
  }

  // Prime the rate counters.
  ::PdhCollectQueryData(impl_->query);
  sample_count_ = 1;
  return true;
}

bool PerfCounters::Sample() {
  if (!impl_->query)
    return false;
  if (::PdhCollectQueryData(impl_->query) != ERROR_SUCCESS)
    return false;
  ++sample_count_;
  if (sample_count_ < 2)
    return false;

  engine_samples_.clear();
  adapter_memory_samples_.clear();
  process_memory_samples_.clear();

  // Reads one wildcard counter into a caller-supplied visitor.
  auto for_each_instance =
      [this](PDH_HCOUNTER counter,
             const std::function<void(const std::wstring&, double)>& visit) {
        if (!counter)
          return;
        DWORD buffer_size = 0;
        DWORD item_count = 0;
        PDH_STATUS status = ::PdhGetFormattedCounterArrayW(
            counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &buffer_size,
            &item_count, nullptr);
        if (status != PDH_MORE_DATA || buffer_size == 0)
          return;
        impl_->scratch.assign(buffer_size, 0);
        auto* items =
            reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(impl_->scratch.data());
        status = ::PdhGetFormattedCounterArrayW(
            counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &buffer_size,
            &item_count, items);
        if (status != ERROR_SUCCESS)
          return;
        for (DWORD i = 0; i < item_count; ++i) {
          if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
              items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
            continue;
          }
          if (!items[i].szName)
            continue;
          visit(std::wstring(items[i].szName), items[i].FmtValue.doubleValue);
        }
      };

  if (gpu_available_) {
    for_each_instance(impl_->gpu_engine,
                      [this](const std::wstring& name, double value) {
                        GpuEngineSample sample;
                        if (!ParseGpuInstanceName(name, &sample.key))
                          return;
                        sample.utilization_percent = value;
                        engine_samples_.push_back(std::move(sample));
                      });

    std::map<std::wstring, GpuMemorySample> adapter_map;
    for_each_instance(impl_->adapter_dedicated,
                      [&adapter_map](const std::wstring& name, double value) {
                        GpuInstanceKey key;
                        if (!ParseGpuInstanceName(name, &key))
                          return;
                        GpuMemorySample& sample = adapter_map[name];
                        sample.key = key;
                        sample.dedicated_bytes =
                            value > 0 ? static_cast<uint64_t>(value) : 0;
                      });
    for_each_instance(impl_->adapter_shared,
                      [&adapter_map](const std::wstring& name, double value) {
                        GpuInstanceKey key;
                        if (!ParseGpuInstanceName(name, &key))
                          return;
                        GpuMemorySample& sample = adapter_map[name];
                        sample.key = key;
                        sample.shared_bytes =
                            value > 0 ? static_cast<uint64_t>(value) : 0;
                      });
    for (auto& [name, sample] : adapter_map)
      adapter_memory_samples_.push_back(sample);

    for_each_instance(impl_->process_dedicated,
                      [this](const std::wstring& name, double value) {
                        GpuMemorySample sample;
                        if (!ParseGpuInstanceName(name, &sample.key))
                          return;
                        sample.dedicated_bytes =
                            value > 0 ? static_cast<uint64_t>(value) : 0;
                        process_memory_samples_.push_back(std::move(sample));
                      });
  }

  if (cpu_available_ && impl_->cpu_total) {
    PDH_FMT_COUNTERVALUE value{};
    if (::PdhGetFormattedCounterValue(impl_->cpu_total, PDH_FMT_DOUBLE, nullptr,
                                      &value) == ERROR_SUCCESS &&
        value.CStatus == PDH_CSTATUS_VALID_DATA) {
      system_cpu_percent_ = std::clamp(value.doubleValue, 0.0, 100.0);
    }
  }

  return true;
}

double PerfCounters::SystemGpuPercent(int64_t luid) const {
  // Engines run in parallel, so the meaningful headline number is the busiest
  // engine, not the sum. Summing is how monitors end up claiming 300%.
  double busiest = 0.0;
  std::map<int, double> per_engine;
  for (const GpuEngineSample& sample : engine_samples_) {
    if (sample.key.luid != luid)
      continue;
    per_engine[sample.key.engine] += sample.utilization_percent;
  }
  for (const auto& [engine, total] : per_engine)
    busiest = std::max(busiest, total);
  return std::clamp(busiest, 0.0, 100.0);
}

double PerfCounters::ProcessGpuPercent(const std::set<uint32_t>& pids,
                                       int64_t luid) const {
  double busiest = 0.0;
  std::map<int, double> per_engine;
  for (const GpuEngineSample& sample : engine_samples_) {
    if (sample.key.luid != luid || !sample.key.has_pid)
      continue;
    if (pids.find(sample.key.pid) == pids.end())
      continue;
    per_engine[sample.key.engine] += sample.utilization_percent;
  }
  for (const auto& [engine, total] : per_engine)
    busiest = std::max(busiest, total);
  return std::clamp(busiest, 0.0, 100.0);
}

uint64_t PerfCounters::AdapterDedicatedBytes(int64_t luid) const {
  uint64_t total = 0;
  for (const GpuMemorySample& sample : adapter_memory_samples_) {
    if (sample.key.luid == luid)
      total += sample.dedicated_bytes;
  }
  return total;
}

uint64_t PerfCounters::AdapterSharedBytes(int64_t luid) const {
  uint64_t total = 0;
  for (const GpuMemorySample& sample : adapter_memory_samples_) {
    if (sample.key.luid == luid)
      total += sample.shared_bytes;
  }
  return total;
}

uint64_t PerfCounters::ProcessDedicatedBytes(const std::set<uint32_t>& pids,
                                             int64_t luid) const {
  uint64_t total = 0;
  for (const GpuMemorySample& sample : process_memory_samples_) {
    if (sample.key.luid != luid || !sample.key.has_pid)
      continue;
    if (pids.find(sample.key.pid) == pids.end())
      continue;
    total += sample.dedicated_bytes;
  }
  return total;
}

}  // namespace cad::sys
