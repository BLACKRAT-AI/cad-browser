#include "sys/smbios.h"

#include "sys/win_util.h"

#include <cstring>

namespace cad::sys {
namespace {

// 'RSMB', the raw SMBIOS firmware table provider.
constexpr DWORD kRsmbProvider = 0x52534D42u;

uint16_t ReadU16(const uint8_t* p) {
  uint16_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

uint32_t ReadU32(const uint8_t* p) {
  uint32_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

// "0x1A" - used when firmware reports an enum value we do not have a name for.
std::string HexByte(uint8_t value) {
  static const char kDigits[] = "0123456789ABCDEF";
  std::string out = "0x";
  out += kDigits[(value >> 4) & 0x0F];
  out += kDigits[value & 0x0F];
  return out;
}

}  // namespace

SmbiosStructure::SmbiosStructure(const uint8_t* formatted,
                                 size_t formatted_len,
                                 std::vector<std::string> strings)
    : formatted_(formatted, formatted + formatted_len),
      strings_(std::move(strings)) {
  if (formatted_.size() >= 4) {
    type_ = formatted_[0];
    handle_ = ReadU16(formatted_.data() + 2);
  }
}

std::optional<uint8_t> SmbiosStructure::Byte(size_t offset) const {
  if (offset + 1 > formatted_.size())
    return std::nullopt;
  return formatted_[offset];
}

std::optional<uint16_t> SmbiosStructure::Word(size_t offset) const {
  if (offset + 2 > formatted_.size())
    return std::nullopt;
  return ReadU16(formatted_.data() + offset);
}

std::optional<uint32_t> SmbiosStructure::Dword(size_t offset) const {
  if (offset + 4 > formatted_.size())
    return std::nullopt;
  return ReadU32(formatted_.data() + offset);
}

std::optional<uint64_t> SmbiosStructure::Qword(size_t offset) const {
  if (offset + 8 > formatted_.size())
    return std::nullopt;
  uint64_t v = 0;
  std::memcpy(&v, formatted_.data() + offset, sizeof(v));
  return v;
}

std::string SmbiosStructure::String(size_t offset) const {
  const std::optional<uint8_t> index = Byte(offset);
  if (!index.has_value() || *index == 0)
    return {};
  const size_t i = static_cast<size_t>(*index) - 1;
  if (i >= strings_.size())
    return {};
  return TrimAscii(strings_[i]);
}

std::vector<const SmbiosStructure*> SmbiosTable::OfType(uint8_t type) const {
  std::vector<const SmbiosStructure*> out;
  for (const SmbiosStructure& s : structures) {
    if (s.type() == type)
      out.push_back(&s);
  }
  return out;
}

const SmbiosStructure* SmbiosTable::FirstOfType(uint8_t type) const {
  for (const SmbiosStructure& s : structures) {
    if (s.type() == type)
      return &s;
  }
  return nullptr;
}

std::string SmbiosTable::VersionString() const {
  if (major_version == 0 && minor_version == 0)
    return {};
  return std::to_string(major_version) + "." + std::to_string(minor_version);
}

SmbiosTable ParseSmbiosTableData(const uint8_t* data,
                                 size_t size,
                                 uint8_t major,
                                 uint8_t minor) {
  SmbiosTable table;
  table.major_version = major;
  table.minor_version = minor;

  if (data == nullptr || size == 0) {
    table.error = "SMBIOS table is empty";
    return table;
  }

  size_t pos = 0;
  bool saw_end_of_table = false;
  while (pos + 4 <= size) {
    const uint8_t type = data[pos];
    const uint8_t len = data[pos + 1];

    // A header is 4 bytes; anything shorter means we have lost sync and must
    // not keep walking, or we would read arbitrary offsets as structures.
    if (len < 4) {
      table.error = "malformed structure header at offset " +
                    std::to_string(pos) + " (length " + std::to_string(len) +
                    ")";
      break;
    }
    if (pos + len > size) {
      table.error = "structure at offset " + std::to_string(pos) +
                    " runs past the end of the table";
      break;
    }

    // The string table follows the formatted area and ends with two NULs.
    std::vector<std::string> strings;
    size_t cur = pos + len;
    bool terminated = false;

    if (cur + 1 < size && data[cur] == 0 && data[cur + 1] == 0) {
      // No strings at all: the area is just the double terminator.
      cur += 2;
      terminated = true;
    } else {
      while (cur < size) {
        const size_t start = cur;
        while (cur < size && data[cur] != 0)
          ++cur;
        if (cur >= size)
          break;  // unterminated string; handled below
        strings.emplace_back(reinterpret_cast<const char*>(data + start),
                             cur - start);
        ++cur;  // step over the NUL
        if (cur < size && data[cur] == 0) {
          ++cur;  // step over the second NUL
          terminated = true;
          break;
        }
      }
    }

    if (!terminated) {
      table.error = "unterminated string table in structure at offset " +
                    std::to_string(pos);
      // Keep the structure: its formatted fields are still readable and they
      // are what the memory report is built from.
      table.structures.emplace_back(data + pos, len, std::move(strings));
      break;
    }

    table.structures.emplace_back(data + pos, len, std::move(strings));
    pos = cur;

    if (type == kSmbiosTypeEndOfTable) {
      saw_end_of_table = true;
      break;
    }
  }

  // A well-formed table always ends with a type 127 structure. Its absence
  // means the data was cut short somewhere, even when every structure we did
  // read parsed cleanly, so it is worth reporting rather than assuming the
  // table simply ended.
  if (!saw_end_of_table && table.error.empty()) {
    table.error = "table ended without an end-of-table structure";
  }

  table.ok = !table.structures.empty();
  if (!table.ok && table.error.empty())
    table.error = "no SMBIOS structures found";
  return table;
}

SmbiosTable ReadSmbios() {
  SmbiosTable table;

  const UINT needed = ::GetSystemFirmwareTable(kRsmbProvider, 0, nullptr, 0);
  if (needed == 0) {
    table.error = "GetSystemFirmwareTable(RSMB) size query failed: " +
                  Win32ErrorString(::GetLastError());
    return table;
  }

  std::vector<uint8_t> buffer(needed);
  const UINT got =
      ::GetSystemFirmwareTable(kRsmbProvider, 0, buffer.data(), needed);
  if (got == 0 || got > needed) {
    table.error = "GetSystemFirmwareTable(RSMB) read failed: " +
                  Win32ErrorString(::GetLastError());
    return table;
  }

  // RawSMBIOSData: used20CallingMethod, major, minor, dmiRevision, length,
  // then the structure area.
  constexpr size_t kHeaderSize = 8;
  if (got < kHeaderSize) {
    table.error = "SMBIOS buffer too small (" + std::to_string(got) + " bytes)";
    return table;
  }

  const uint8_t major = buffer[1];
  const uint8_t minor = buffer[2];
  const uint8_t dmi_revision = buffer[3];
  uint32_t length = ReadU32(buffer.data() + 4);

  const size_t available = static_cast<size_t>(got) - kHeaderSize;
  std::string clamp_note;
  if (length > available) {
    clamp_note = "declared table length " + std::to_string(length) +
                 " exceeds the " + std::to_string(available) +
                 " bytes returned; using the smaller value";
    length = static_cast<uint32_t>(available);
  }

  table = ParseSmbiosTableData(buffer.data() + kHeaderSize, length, major, minor);
  table.dmi_revision = dmi_revision;
  if (!clamp_note.empty()) {
    if (table.error.empty())
      table.error = clamp_note;
    else
      table.error = clamp_note + "; " + table.error;
  }
  return table;
}

std::string SmbiosMemoryTypeName(uint8_t value) {
  switch (value) {
    case 0x01: return "Other";
    case 0x02: return "Unknown";
    case 0x03: return "DRAM";
    case 0x04: return "EDRAM";
    case 0x05: return "VRAM";
    case 0x06: return "SRAM";
    case 0x07: return "RAM";
    case 0x08: return "ROM";
    case 0x09: return "Flash";
    case 0x0A: return "EEPROM";
    case 0x0B: return "FEPROM";
    case 0x0C: return "EPROM";
    case 0x0D: return "CDRAM";
    case 0x0E: return "3DRAM";
    case 0x0F: return "SDRAM";
    case 0x10: return "SGRAM";
    case 0x11: return "RDRAM";
    case 0x12: return "DDR";
    case 0x13: return "DDR2";
    case 0x14: return "DDR2 FB-DIMM";
    case 0x18: return "DDR3";
    case 0x19: return "FBD2";
    case 0x1A: return "DDR4";
    case 0x1B: return "LPDDR";
    case 0x1C: return "LPDDR2";
    case 0x1D: return "LPDDR3";
    case 0x1E: return "LPDDR4";
    case 0x1F: return "Logical non-volatile device";
    case 0x20: return "HBM";
    case 0x21: return "HBM2";
    case 0x22: return "DDR5";
    case 0x23: return "LPDDR5";
    case 0x24: return "HBM3";
    default: return "Type " + HexByte(value);
  }
}

std::string SmbiosFormFactorName(uint8_t value) {
  switch (value) {
    case 0x01: return "Other";
    case 0x02: return "Unknown";
    case 0x03: return "SIMM";
    case 0x04: return "SIP";
    case 0x05: return "Chip";
    case 0x06: return "DIP";
    case 0x07: return "ZIP";
    case 0x08: return "Proprietary Card";
    case 0x09: return "DIMM";
    case 0x0A: return "TSOP";
    case 0x0B: return "Row of chips";
    case 0x0C: return "RIMM";
    case 0x0D: return "SODIMM";
    case 0x0E: return "SRIMM";
    case 0x0F: return "FB-DIMM";
    case 0x10: return "Die";
    case 0x11: return "CAMM";
    default: return "Unknown";
  }
}

std::string SmbiosMemoryArrayLocationName(uint8_t value) {
  switch (value) {
    case 0x01: return "Other";
    case 0x02: return "Unknown";
    case 0x03: return "System board or motherboard";
    case 0x04: return "ISA add-on card";
    case 0x05: return "EISA add-on card";
    case 0x06: return "PCI add-on card";
    case 0x07: return "MCA add-on card";
    case 0x08: return "PCMCIA add-on card";
    case 0x09: return "Proprietary add-on card";
    case 0x0A: return "NuBus";
    case 0xA0: return "PC-98/C20 add-on card";
    case 0xA1: return "PC-98/C24 add-on card";
    case 0xA2: return "PC-98/E add-on card";
    case 0xA3: return "PC-98/Local bus add-on card";
    case 0xA4: return "CXL add-on card";
    default: return "Unknown";
  }
}

std::string SmbiosErrorCorrectionName(uint8_t value) {
  switch (value) {
    case 0x01: return "Other";
    case 0x02: return "Unknown";
    case 0x03: return "None";
    case 0x04: return "Parity";
    case 0x05: return "Single-bit ECC";
    case 0x06: return "Multi-bit ECC";
    case 0x07: return "CRC";
    default: return "Unknown";
  }
}

}  // namespace cad::sys
