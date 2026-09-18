// CAD Browser - processor topology and identification.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad::sys {

struct CacheLevel {
  int level = 0;          // 1, 2, 3
  std::string type;       // "Data", "Instruction", "Unified", "Trace"
  uint64_t size_bytes = 0;  // total across all instances at this level
  int instances = 0;
};

struct CpuInfo {
  std::string brand;             // "13th Gen Intel(R) Core(TM) i9-13900K"
  std::string vendor;            // "GenuineIntel"
  int physical_cores = 0;
  int logical_processors = 0;
  int performance_cores = 0;     // efficiency class > 0
  int efficiency_cores = 0;      // efficiency class 0 on a hybrid part
  bool hybrid = false;
  int numa_nodes = 0;
  int processor_groups = 0;
  uint32_t base_mhz = 0;         // as the firmware reported it at boot
  std::vector<CacheLevel> caches;
  std::string error;

  uint64_t cache_bytes(int level) const;
};

CpuInfo QueryCpuInfo();

}  // namespace cad::sys
