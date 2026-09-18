#include "sys/win_util.h"

#include "sys/cpu_info.h"

#include <intrin.h>

#include <cstring>
#include <map>

namespace cad::sys {
namespace {

// Counts set bits in a processor affinity mask.
int PopCount(KAFFINITY mask) {
  int count = 0;
  while (mask) {
    mask &= (mask - 1);
    ++count;
  }
  return count;
}

std::string CpuBrandString() {
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, 0x80000000);
  const unsigned max_extended = static_cast<unsigned>(regs[0]);
  if (max_extended < 0x80000004u)
    return {};

  char brand[49] = {};
  for (unsigned leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
    __cpuid(regs, static_cast<int>(leaf));
    std::memcpy(brand + (leaf - 0x80000002u) * 16, regs, sizeof(regs));
  }
  brand[48] = '\0';
  return TrimAscii(brand);
}

std::string CpuVendorString() {
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, 0);
  char vendor[13] = {};
  std::memcpy(vendor + 0, &regs[1], 4);  // EBX
  std::memcpy(vendor + 4, &regs[3], 4);  // EDX
  std::memcpy(vendor + 8, &regs[2], 4);  // ECX
  vendor[12] = '\0';
  return TrimAscii(vendor);
}

std::string CacheTypeName(PROCESSOR_CACHE_TYPE type) {
  switch (type) {
    case CacheUnified: return "Unified";
    case CacheInstruction: return "Instruction";
    case CacheData: return "Data";
    case CacheTrace: return "Trace";
    default: return "Unknown";
  }
}

}  // namespace

uint64_t CpuInfo::cache_bytes(int level) const {
  uint64_t total = 0;
  for (const CacheLevel& c : caches) {
    if (c.level == level)
      total += c.size_bytes;
  }
  return total;
}

CpuInfo QueryCpuInfo() {
  CpuInfo info;
  info.brand = CpuBrandString();
  info.vendor = CpuVendorString();

  SYSTEM_INFO sysinfo{};
  ::GetNativeSystemInfo(&sysinfo);
  info.logical_processors = static_cast<int>(sysinfo.dwNumberOfProcessors);

  // Walk the variable-length processor information records once, tallying
  // everything we care about.
  DWORD length = 0;
  ::GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
  if (length == 0 || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    info.error = "GetLogicalProcessorInformationEx failed: " +
                 Win32ErrorString(::GetLastError());
    return info;
  }

  std::vector<uint8_t> buffer(length);
  if (!::GetLogicalProcessorInformationEx(
          RelationAll,
          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
              buffer.data()),
          &length)) {
    info.error = "GetLogicalProcessorInformationEx failed: " +
                 Win32ErrorString(::GetLastError());
    return info;
  }

  // level -> (type -> [size, instances])
  std::map<int, std::map<std::string, std::pair<uint64_t, int>>> cache_totals;
  int logical_from_cores = 0;

  size_t offset = 0;
  while (offset + sizeof(LOGICAL_PROCESSOR_RELATIONSHIP) <= buffer.size()) {
    auto* record = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
        buffer.data() + offset);
    if (record->Size == 0 || offset + record->Size > buffer.size())
      break;

    switch (record->Relationship) {
      case RelationProcessorCore: {
        ++info.physical_cores;
        for (WORD g = 0; g < record->Processor.GroupCount; ++g)
          logical_from_cores += PopCount(record->Processor.GroupMask[g].Mask);
        // On hybrid parts Windows labels the efficient cores class 0 and the
        // performance cores with a higher class.
        if (record->Processor.EfficiencyClass > 0) {
          ++info.performance_cores;
          info.hybrid = true;
        } else {
          ++info.efficiency_cores;
        }
        break;
      }
      case RelationCache: {
        const CACHE_RELATIONSHIP& cache = record->Cache;
        const int level = cache.Level;
        const std::string type = CacheTypeName(cache.Type);
        auto& entry = cache_totals[level][type];
        entry.first += cache.CacheSize;
        entry.second += 1;
        break;
      }
      case RelationNumaNode:
        ++info.numa_nodes;
        break;
      case RelationGroup:
        info.processor_groups = record->Group.ActiveGroupCount;
        break;
      default:
        break;
    }

    offset += record->Size;
  }

  if (logical_from_cores > 0)
    info.logical_processors = logical_from_cores;

  // A non-hybrid CPU has every core in class 0, which would otherwise read as
  // "all efficiency cores".
  if (!info.hybrid) {
    info.performance_cores = info.physical_cores;
    info.efficiency_cores = 0;
  }

  for (const auto& [level, by_type] : cache_totals) {
    for (const auto& [type, totals] : by_type) {
      CacheLevel entry;
      entry.level = level;
      entry.type = type;
      entry.size_bytes = totals.first;
      entry.instances = totals.second;
      info.caches.push_back(entry);
    }
  }

  const auto mhz = RegReadDword(
      HKEY_LOCAL_MACHINE,
      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
  if (mhz.has_value())
    info.base_mhz = *mhz;

  if (info.brand.empty()) {
    const auto name = RegReadString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString");
    if (name.has_value())
      info.brand = TrimAscii(ToUtf8(*name));
  }

  return info;
}

}  // namespace cad::sys
