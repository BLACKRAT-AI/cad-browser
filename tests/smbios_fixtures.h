// CAD Browser - synthetic SMBIOS tables for the parser tests.
//
// These are built rather than captured so each test can target one specific
// encoding rule: the three ways a type 17 size field can be written, the
// extended maximum capacity escape in type 16, string-table indexing, and the
// malformed shapes that firmware and virtual machines actually produce.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad_test {

// Assembles well-formed (or deliberately broken) SMBIOS structure areas.
class SmbiosBuilder {
 public:
  // Starts a structure. `formatted_length` includes the 4-byte header.
  SmbiosBuilder& BeginStructure(uint8_t type,
                                uint16_t handle,
                                uint8_t formatted_length);
  SmbiosBuilder& Byte(uint8_t value);
  SmbiosBuilder& Word(uint16_t value);
  SmbiosBuilder& Dword(uint32_t value);
  SmbiosBuilder& Qword(uint64_t value);
  // Pads the formatted area out to the declared length.
  SmbiosBuilder& PadFormatted();
  // Overwrites a field of the structure being built, addressed from its start.
  // Used for fields past the point the fluent writers have reached.
  SmbiosBuilder& PatchDword(size_t offset_in_structure, uint32_t value);
  // Ends the structure, writing the string table.
  SmbiosBuilder& EndStructure(const std::vector<std::string>& strings);

  SmbiosBuilder& EndOfTable();

  const std::vector<uint8_t>& bytes() const { return data_; }
  // Cuts the table short, to exercise the truncation paths.
  std::vector<uint8_t> Truncated(size_t keep_bytes) const;

 private:
  std::vector<uint8_t> data_;
  size_t structure_start_ = 0;
  uint8_t declared_length_ = 0;
};

// A desktop: two of four DDR5 slots populated, 32 GB each, 128 GB maximum.
std::vector<uint8_t> DesktopFixture();

// A hybrid laptop: soldered LPDDR5, one "slot", size expressed in the
// extended field, maximum capacity in the extended field too.
std::vector<uint8_t> LaptopFixture();

// A virtual machine: a single device whose size is reported in kilobytes and
// whose manufacturer strings are absent.
std::vector<uint8_t> VirtualMachineFixture();

}  // namespace cad_test
