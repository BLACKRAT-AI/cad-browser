// CAD Browser - installed / usable / maximum RAM, done properly.
//
// PLAN.md section 5: three independent sources, cross-checked, all shown, and
// any discrepancy explained rather than hidden behind one convenient number.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sys/smbios.h"

namespace cad::sys {

enum class InstalledMemorySource {
  kNone,
  // GetPhysicallyInstalledSystemMemory: Windows reading SMBIOS for us.
  kWin32InstalledApi,
  // Our own sum over SMBIOS type 17 records.
  kSmbiosDeviceSum,
  // Last resort. OS-visible memory only, so it under-reports by whatever the
  // firmware reserved.
  kGlobalMemoryStatus,
};

const char* InstalledMemorySourceName(InstalledMemorySource source);

// One memory device (type 17). A slot with size 0 is present but empty.
struct MemoryModule {
  int index = 0;
  std::string device_locator;      // "DIMM A1"
  std::string bank_locator;        // "P0 CHANNEL A"
  uint64_t size_bytes = 0;
  uint8_t memory_type_raw = 0;
  std::string memory_type;         // "DDR5"
  std::string form_factor;         // "DIMM", "SODIMM"
  uint32_t speed_mts = 0;              // rated speed
  uint32_t configured_speed_mts = 0;   // speed it is actually running at
  std::string manufacturer;
  std::string part_number;
  std::string serial_number;
  uint8_t rank = 0;
  uint16_t data_width_bits = 0;
  uint16_t total_width_bits = 0;
  bool ecc = false;

  bool populated() const { return size_bytes > 0; }
};

// Physical memory array (type 16): the motherboard's own limits.
struct MemoryArrayInfo {
  bool present = false;
  uint64_t max_capacity_bytes = 0;
  int slot_count = 0;
  std::string location;
  std::string error_correction;
};

// The parts that change moment to moment. Cheap enough to sample at 4 Hz.
struct MemoryDynamic {
  uint64_t total_physical_bytes = 0;
  uint64_t available_physical_bytes = 0;
  uint64_t used_physical_bytes = 0;
  double load_percent = 0.0;
  uint64_t commit_total_bytes = 0;
  uint64_t commit_limit_bytes = 0;
  uint64_t commit_peak_bytes = 0;
  uint64_t system_cache_bytes = 0;
  uint64_t kernel_paged_bytes = 0;
  uint64_t kernel_nonpaged_bytes = 0;
  uint64_t page_size_bytes = 0;
  uint32_t process_count = 0;
  uint32_t thread_count = 0;
  uint32_t handle_count = 0;
};

struct MemoryInfo {
  // What is physically in the machine.
  uint64_t installed_bytes = 0;
  InstalledMemorySource installed_source = InstalledMemorySource::kNone;

  // The individual sources, kept so the System page can show its work.
  uint64_t win32_installed_bytes = 0;
  std::string win32_installed_error;
  uint64_t smbios_sum_bytes = 0;
  uint64_t usable_bytes = 0;             // what the OS can actually allocate
  uint64_t hardware_reserved_bytes = 0;  // installed - usable

  std::vector<MemoryModule> modules;
  MemoryArrayInfo array;
  int slots_used = 0;
  int slots_total = 0;
  std::string channel_estimate;

  bool smbios_ok = false;
  std::string smbios_error;
  std::string smbios_version;

  // Human-readable notes about anything that did not add up.
  std::vector<std::string> warnings;

  MemoryDynamic dynamic;
};

// Full query. Touches SMBIOS, so call it once at startup and cache it.
MemoryInfo QueryMemoryInfo();

// Cheap sample for the overlay.
MemoryDynamic QueryMemoryDynamic();

// Testable core. Kept free of Win32 calls so fixtures can drive it.
MemoryInfo BuildMemoryInfo(const SmbiosTable& table,
                           uint64_t win32_installed_bytes,
                           const std::string& win32_error,
                           uint64_t usable_bytes);

}  // namespace cad::sys
