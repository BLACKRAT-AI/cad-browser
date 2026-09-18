#include "smbios_fixtures.h"

#include <cstring>

namespace cad_test {
namespace {

constexpr uint8_t kTypePhysicalMemoryArray = 16;
constexpr uint8_t kTypeMemoryDevice = 17;
constexpr uint8_t kTypeEndOfTable = 127;

// Type 17 is 0x54 bytes through SMBIOS 3.3, which is long enough to carry the
// extended size and extended speed fields the tests exercise.
constexpr uint8_t kMemoryDeviceLength = 0x5C;
constexpr uint8_t kMemoryArrayLength = 0x17;

}  // namespace

SmbiosBuilder& SmbiosBuilder::BeginStructure(uint8_t type,
                                             uint16_t handle,
                                             uint8_t formatted_length) {
  structure_start_ = data_.size();
  declared_length_ = formatted_length;
  data_.push_back(type);
  data_.push_back(formatted_length);
  Word(handle);
  return *this;
}

SmbiosBuilder& SmbiosBuilder::Byte(uint8_t value) {
  data_.push_back(value);
  return *this;
}

SmbiosBuilder& SmbiosBuilder::Word(uint16_t value) {
  data_.push_back(static_cast<uint8_t>(value & 0xFF));
  data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  return *this;
}

SmbiosBuilder& SmbiosBuilder::Dword(uint32_t value) {
  for (int i = 0; i < 4; ++i)
    data_.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
  return *this;
}

SmbiosBuilder& SmbiosBuilder::Qword(uint64_t value) {
  for (int i = 0; i < 8; ++i)
    data_.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
  return *this;
}

SmbiosBuilder& SmbiosBuilder::PadFormatted() {
  while (data_.size() - structure_start_ < declared_length_)
    data_.push_back(0);
  return *this;
}

SmbiosBuilder& SmbiosBuilder::PatchDword(size_t offset_in_structure,
                                         uint32_t value) {
  PadFormatted();
  const size_t at = structure_start_ + offset_in_structure;
  if (at + 4 <= data_.size())
    std::memcpy(data_.data() + at, &value, sizeof(value));
  return *this;
}

SmbiosBuilder& SmbiosBuilder::EndStructure(
    const std::vector<std::string>& strings) {
  PadFormatted();
  if (strings.empty()) {
    // No strings: the area is just the double terminator.
    data_.push_back(0);
    data_.push_back(0);
    return *this;
  }
  for (const std::string& text : strings) {
    data_.insert(data_.end(), text.begin(), text.end());
    data_.push_back(0);
  }
  data_.push_back(0);
  return *this;
}

SmbiosBuilder& SmbiosBuilder::EndOfTable() {
  BeginStructure(kTypeEndOfTable, 0x7F00, 4);
  EndStructure({});
  return *this;
}

std::vector<uint8_t> SmbiosBuilder::Truncated(size_t keep_bytes) const {
  if (keep_bytes >= data_.size())
    return data_;
  return std::vector<uint8_t>(data_.begin(), data_.begin() + keep_bytes);
}

std::vector<uint8_t> DesktopFixture() {
  SmbiosBuilder b;

  // Type 16: 128 GB maximum across 4 slots, expressed in kilobytes.
  b.BeginStructure(kTypePhysicalMemoryArray, 0x1000, kMemoryArrayLength)
      .Byte(0x03)                       // location: system board
      .Byte(0x03)                       // use: system memory
      .Byte(0x03)                       // error correction: none
      .Dword(128u * 1024u * 1024u)      // 128 GB in KB
      .Word(0xFFFE)                     // error information handle
      .Word(4)                          // number of devices
      .EndStructure({});

  // Two populated 16 GB DDR5 modules on separate controllers, two empty slots.
  //
  // 16384 MB is the largest round size that still fits the plain size field:
  // bit 15 of that field means "the value is in kilobytes", so 32768 would
  // decode as 0 KB rather than 32 GB. Real firmware reports anything at or
  // above 32 GB through the extended size escape, which LaptopFixture covers.
  const char* locators[] = {"Controller0-ChannelA-DIMM0",
                            "Controller0-ChannelA-DIMM1",
                            "Controller1-ChannelA-DIMM0",
                            "Controller1-ChannelA-DIMM1"};
  for (int i = 0; i < 4; ++i) {
    const bool populated = (i % 2) == 0;
    b.BeginStructure(kTypeMemoryDevice, static_cast<uint16_t>(0x1100 + i),
                     kMemoryDeviceLength)
        .Word(0x1000)                              // array handle
        .Word(0xFFFE)                              // error info handle
        .Word(populated ? 64 : 0)                  // total width
        .Word(populated ? 64 : 0)                  // data width
        .Word(populated ? 16384 : 0)               // size: 16384 MB = 16 GB
        .Byte(0x09)                                // form factor: DIMM
        .Byte(0)                                   // device set
        .Byte(1)                                   // device locator -> string 1
        .Byte(2)                                   // bank locator -> string 2
        .Byte(0x22)                                // memory type: DDR5
        .Word(0x0080)                              // type detail
        .Word(populated ? 4800 : 0)                // speed
        .Byte(populated ? 3 : 0)                   // manufacturer -> string 3
        .Byte(populated ? 4 : 0)                   // serial -> string 4
        .Byte(0)                                   // asset tag
        .Byte(populated ? 5 : 0)                   // part number -> string 5
        .Byte(0x02)                                // attributes: rank 2
        .Dword(0)                                  // extended size (unused)
        .Word(populated ? 4800 : 0)                // configured speed
        .PadFormatted();
    if (populated) {
      b.EndStructure({locators[i], "BANK 0", "Corsair", "SN00000000",
                      "CMSX64GX5M2A4800C40"});
    } else {
      b.EndStructure({locators[i], "BANK 0"});
    }
  }

  b.EndOfTable();
  return b.bytes();
}

std::vector<uint8_t> LaptopFixture() {
  SmbiosBuilder b;

  // Maximum capacity above the 32-bit kilobyte field, so it escapes to the
  // extended field: 2 TB expressed in bytes.
  b.BeginStructure(kTypePhysicalMemoryArray, 0x1000, kMemoryArrayLength)
      .Byte(0x03)
      .Byte(0x03)
      .Byte(0x03)
      .Dword(0x80000000u)                       // escape: use extended
      .Word(0xFFFE)
      .Word(1)
      .Qword(2ull * 1024 * 1024 * 1024 * 1024)  // 2 TB in bytes
      .EndStructure({});

  // One soldered 64 GB LPDDR5 device. 64 GB exceeds the 0x7FFF MB limit of the
  // plain size field, so it escapes to Extended Size.
  b.BeginStructure(kTypeMemoryDevice, 0x1100, kMemoryDeviceLength)
      .Word(0x1000)
      .Word(0xFFFE)
      .Word(64)
      .Word(64)
      .Word(0x7FFF)                 // escape: use extended size
      .Byte(0x0D)                   // form factor: SODIMM
      .Byte(0)
      .Byte(1)                      // device locator
      .Byte(2)                      // bank locator
      .Byte(0x23)                   // memory type: LPDDR5
      .Word(0x0080)
      .Word(0xFFFF)                 // escape: use extended speed
      .Byte(3)
      .Byte(0)
      .Byte(0)
      .Byte(4)
      .Byte(0x01)
      .Dword(65536)                 // extended size: 65536 MB = 64 GB
      .Word(0xFFFF)                 // escape: use extended configured speed
      .PadFormatted()
      // Extended Speed and Extended Configured Memory Speed, SMBIOS 3.3.
      .PatchDword(0x54, 7500)
      .PatchDword(0x58, 7500)
      .EndStructure({"Onboard Memory", "P0 CHANNEL A", "Samsung",
                     "K3LKLKL0EM"});

  b.EndOfTable();
  return b.bytes();
}

std::vector<uint8_t> VirtualMachineFixture() {
  SmbiosBuilder b;

  b.BeginStructure(kTypePhysicalMemoryArray, 0x1000, kMemoryArrayLength)
      .Byte(0x03)
      .Byte(0x03)
      .Byte(0x02)                    // error correction: unknown
      .Dword(8u * 1024u * 1024u)     // 8 GB maximum
      .Word(0xFFFE)
      .Word(1)
      .EndStructure({});

  // A hypervisor reporting the size in kilobytes (bit 15 set) and supplying no
  // manufacturer strings at all.
  b.BeginStructure(kTypeMemoryDevice, 0x1100, kMemoryDeviceLength)
      .Word(0x1000)
      .Word(0xFFFE)
      .Word(0xFFFF)                  // total width unknown
      .Word(0xFFFF)                  // data width unknown
      .Word(0x8000u | 2048u)         // bit 15 set: 2048 KB
      .Byte(0x09)
      .Byte(0)
      .Byte(1)                       // device locator
      .Byte(0)                       // no bank locator
      .Byte(0x02)                    // memory type: unknown
      .Word(0)
      .Word(0)
      .Byte(0)                       // no manufacturer
      .Byte(0)
      .Byte(0)
      .Byte(0)
      .Byte(0)
      .Dword(0)
      .Word(0)
      .PadFormatted()
      .EndStructure({"RAM slot #0"});

  b.EndOfTable();
  return b.bytes();
}

}  // namespace cad_test
