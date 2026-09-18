// CAD Browser - WMI cross-check for installed memory.
//
// A fourth opinion on installed RAM, used only to catch the case where our
// SMBIOS parse and the Win32 API agree with each other but disagree with what
// every other tool on the machine reports. It is deliberately off the hot
// path: WMI can take hundreds of milliseconds and can hang outright when the
// repository is unhealthy, so it runs with a timeout and any failure is simply
// "no fourth opinion available".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad::sys {

struct WmiMemoryResult {
  bool ok = false;
  std::string error;
  uint64_t total_capacity_bytes = 0;
  int module_count = 0;
  std::vector<uint64_t> module_capacities;
};

// Blocks for at most `timeout_ms`. On timeout, returns ok == false; the worker
// is abandoned rather than waited on, so a hung WMI cannot hang the browser.
WmiMemoryResult QueryWmiPhysicalMemory(int timeout_ms = 2000);

}  // namespace cad::sys
