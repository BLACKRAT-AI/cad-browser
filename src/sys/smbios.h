// CAD Browser - SMBIOS (DMI) firmware table reader and parser.
//
// Windows exposes the raw SMBIOS table through GetSystemFirmwareTable('RSMB').
// Parsing it ourselves is what lets the System page report per-DIMM detail and
// the motherboard's maximum capacity, none of which any single Win32 call
// provides. See PLAN.md section 5.
//
// Everything here is bounds checked and never throws: firmware tables are
// vendor-written and malformed ones are common, especially in VMs.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cad::sys {

// SMBIOS structure types we care about.
inline constexpr uint8_t kSmbiosTypeBios = 0;
inline constexpr uint8_t kSmbiosTypeSystem = 1;
inline constexpr uint8_t kSmbiosTypeBaseboard = 2;
inline constexpr uint8_t kSmbiosTypeProcessor = 4;
inline constexpr uint8_t kSmbiosTypePhysicalMemoryArray = 16;
inline constexpr uint8_t kSmbiosTypeMemoryDevice = 17;
inline constexpr uint8_t kSmbiosTypeMemoryArrayMappedAddress = 19;
inline constexpr uint8_t kSmbiosTypeEndOfTable = 127;

// One SMBIOS structure: a fixed "formatted area" followed by a string table.
// Owns a copy of its bytes, so it stays valid independently of the table.
class SmbiosStructure {
 public:
  SmbiosStructure() = default;
  SmbiosStructure(const uint8_t* formatted,
                  size_t formatted_len,
                  std::vector<std::string> strings);

  uint8_t type() const { return type_; }
  uint16_t handle() const { return handle_; }
  size_t length() const { return formatted_.size(); }

  // Field readers. They return nullopt when this structure is shorter than the
  // requested offset, which is normal rather than an error: SMBIOS grew field
  // by field across revisions and old firmware simply stops early.
  std::optional<uint8_t> Byte(size_t offset) const;
  std::optional<uint16_t> Word(size_t offset) const;
  std::optional<uint32_t> Dword(size_t offset) const;
  std::optional<uint64_t> Qword(size_t offset) const;

  // Reads the byte at `offset` as a 1-based index into the string table.
  // Returns "" for index 0, which is the SMBIOS "no string" value.
  std::string String(size_t offset) const;

  const std::vector<std::string>& strings() const { return strings_; }

 private:
  std::vector<uint8_t> formatted_;
  std::vector<std::string> strings_;
  uint8_t type_ = 0;
  uint16_t handle_ = 0;
};

struct SmbiosTable {
  bool ok = false;
  // Set whenever something was wrong. A table can be usable and still carry a
  // warning here, for example when it was truncated part way through.
  std::string error;
  uint8_t major_version = 0;
  uint8_t minor_version = 0;
  uint8_t dmi_revision = 0;
  std::vector<SmbiosStructure> structures;

  std::vector<const SmbiosStructure*> OfType(uint8_t type) const;
  const SmbiosStructure* FirstOfType(uint8_t type) const;
  // "3.6" - empty when the version is unknown.
  std::string VersionString() const;
};

// Reads and parses the live table via GetSystemFirmwareTable('RSMB').
SmbiosTable ReadSmbios();

// Parses the structure area alone. Exposed so tests can feed captured fixtures
// and deliberately truncated buffers.
SmbiosTable ParseSmbiosTableData(const uint8_t* data,
                                 size_t size,
                                 uint8_t major = 0,
                                 uint8_t minor = 0);

// ---- decoding helpers shared with memory_info ----
std::string SmbiosMemoryTypeName(uint8_t value);
std::string SmbiosFormFactorName(uint8_t value);
std::string SmbiosMemoryArrayLocationName(uint8_t value);
std::string SmbiosErrorCorrectionName(uint8_t value);

}  // namespace cad::sys
