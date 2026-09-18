// CAD Browser - Windows version and machine identity.
//
// Version comes from RtlGetVersion rather than GetVersionEx, which lies to
// applications that lack a compatibility manifest entry for the running build.
#pragma once

#include <cstdint>
#include <string>

namespace cad::sys {

struct OsInfo {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t build = 0;
  uint32_t ubr = 0;            // update build revision, the ".3324" part
  std::string product_name;    // "Windows 11 Home"
  std::string display_version; // "24H2"
  std::string edition;
  bool is_server = false;
  // "Windows 11 Home 24H2 (build 26200.3324)"
  std::string Describe() const;
};

struct MachineInfo {
  std::string manufacturer;   // SMBIOS type 1
  std::string model;
  std::string baseboard_manufacturer;  // SMBIOS type 2
  std::string baseboard_product;
  std::string bios_vendor;    // SMBIOS type 0
  std::string bios_version;
  std::string bios_date;
};

OsInfo QueryOsInfo();
MachineInfo QueryMachineInfo();

}  // namespace cad::sys
