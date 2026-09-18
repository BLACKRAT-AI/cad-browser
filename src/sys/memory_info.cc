// windows.h must precede psapi.h; see the note in process_tree.cc.
#include "sys/win_util.h"

#include "sys/memory_info.h"

#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace cad::sys {
namespace {

constexpr uint64_t kMB = 1024ull * 1024ull;
constexpr uint64_t kKB = 1024ull;

// SMBIOS type 17 field offsets.
constexpr size_t k17TotalWidth = 0x08;
constexpr size_t k17DataWidth = 0x0A;
constexpr size_t k17Size = 0x0C;
constexpr size_t k17FormFactor = 0x0E;
constexpr size_t k17DeviceLocator = 0x10;
constexpr size_t k17BankLocator = 0x11;
constexpr size_t k17MemoryType = 0x12;
constexpr size_t k17Speed = 0x15;
constexpr size_t k17Manufacturer = 0x17;
constexpr size_t k17SerialNumber = 0x18;
constexpr size_t k17PartNumber = 0x1A;
constexpr size_t k17Attributes = 0x1B;
constexpr size_t k17ExtendedSize = 0x1C;
constexpr size_t k17ConfiguredSpeed = 0x20;
constexpr size_t k17ExtendedSpeed = 0x54;
constexpr size_t k17ExtendedConfiguredSpeed = 0x58;

// SMBIOS type 16 field offsets.
constexpr size_t k16Location = 0x04;
constexpr size_t k16ErrorCorrection = 0x06;
constexpr size_t k16MaximumCapacity = 0x07;
constexpr size_t k16NumberOfDevices = 0x0D;
constexpr size_t k16ExtendedMaximumCapacity = 0x0F;

// Decodes the type 17 size field, which is three different encodings wearing
// the same two bytes.
uint64_t DecodeModuleSize(const SmbiosStructure& s) {
  const auto raw = s.Word(k17Size);
  if (!raw.has_value())
    return 0;
  const uint16_t value = *raw;
  if (value == 0)
    return 0;           // slot present but empty
  if (value == 0xFFFF)
    return 0;           // size unknown; treat as not counted
  if (value == 0x7FFF) {
    // Larger than 32 GB - 1 MB, so the real size lives in Extended Size.
    const auto ext = s.Dword(k17ExtendedSize);
    if (!ext.has_value())
      return 0;
    return static_cast<uint64_t>(*ext & 0x7FFFFFFFu) * kMB;
  }
  if (value & 0x8000u)
    return static_cast<uint64_t>(value & 0x7FFFu) * kKB;  // bit 15 set: KB
  return static_cast<uint64_t>(value) * kMB;
}

uint32_t DecodeSpeed(const SmbiosStructure& s,
                     size_t base_offset,
                     size_t extended_offset) {
  const auto raw = s.Word(base_offset);
  if (!raw.has_value() || *raw == 0)
    return 0;
  if (*raw == 0xFFFF) {
    const auto ext = s.Dword(extended_offset);
    if (!ext.has_value())
      return 0;
    return *ext & 0x7FFFFFFFu;
  }
  return *raw;
}

uint64_t DecodeArrayMaxCapacity(const SmbiosStructure& s) {
  const auto raw = s.Dword(k16MaximumCapacity);
  if (!raw.has_value())
    return 0;
  if (*raw == 0x80000000u) {
    // Capacity is 2 TB or more, so it is reported in Extended Maximum
    // Capacity, in bytes rather than kilobytes.
    const auto ext = s.Qword(k16ExtendedMaximumCapacity);
    if (!ext.has_value())
      return 0;
    return *ext;
  }
  if (*raw == 0)
    return 0;
  return static_cast<uint64_t>(*raw) * kKB;
}

// Reads the identifier that follows `keyword` in a locator string, e.g.
// "CHANNEL" in "Controller0-ChannelA-DIMM0" yields "A".
std::string IdentifierAfter(const std::string& text, const char* keyword) {
  std::string upper = text;
  for (char& c : upper)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

  const size_t pos = upper.find(keyword);
  if (pos == std::string::npos)
    return {};
  size_t i = pos + std::strlen(keyword);
  while (i < upper.size() &&
         (upper[i] == ' ' || upper[i] == '_' || upper[i] == '-')) {
    ++i;
  }
  if (i < upper.size() && std::isalnum(static_cast<unsigned char>(upper[i])))
    return std::string(1, upper[i]);
  return {};
}

// Pulls a channel identifier out of the locator strings. Firmware naming is
// not standardised, which is why the result is always labelled an estimate.
// Three schemes cover essentially every board seen in practice:
//   "Controller0-ChannelA-DIMM0"  channels are numbered per memory controller
//   "P0 CHANNEL A"                channels are lettered globally
//   "DIMM_A1"                     the letter alone identifies the channel
// The controller has to be part of the key: two modules can both sit on
// "ChannelA" and still be a dual-channel pair because they are on different
// controllers.
std::string ChannelKey(const MemoryModule& m) {
  for (const std::string& locator : {m.device_locator, m.bank_locator}) {
    const std::string controller = IdentifierAfter(locator, "CONTROLLER");
    const std::string channel = IdentifierAfter(locator, "CHANNEL");
    if (!controller.empty() || !channel.empty())
      return controller + ":" + channel;
  }
  for (const std::string& locator : {m.bank_locator, m.device_locator}) {
    const std::string channel = IdentifierAfter(locator, "CHAN");
    if (!channel.empty())
      return ":" + channel;
  }
  const std::string dimm = IdentifierAfter(m.device_locator, "DIMM");
  if (!dimm.empty())
    return "DIMM:" + dimm;
  return {};
}

std::string DescribeChannels(const std::vector<MemoryModule>& modules) {
  std::set<std::string> channels;
  int populated = 0;
  for (const MemoryModule& m : modules) {
    if (!m.populated())
      continue;
    ++populated;
    const std::string key = ChannelKey(m);
    if (!key.empty())
      channels.insert(key);
  }
  if (populated == 0 || channels.empty()) {
    // Guessing from the number of populated slots would be wrong as often as
    // right, so say nothing rather than mislead.
    return {};
  }

  switch (channels.size()) {
    case 1: return "Single channel (estimated)";
    case 2: return "Dual channel (estimated)";
    case 3: return "Triple channel (estimated)";
    case 4: return "Quad channel (estimated)";
    default:
      return std::to_string(channels.size()) + " channels (estimated)";
  }
}

}  // namespace

const char* InstalledMemorySourceName(InstalledMemorySource source) {
  switch (source) {
    case InstalledMemorySource::kWin32InstalledApi:
      return "GetPhysicallyInstalledSystemMemory (SMBIOS)";
    case InstalledMemorySource::kSmbiosDeviceSum:
      return "SMBIOS type 17 sum";
    case InstalledMemorySource::kGlobalMemoryStatus:
      return "GlobalMemoryStatusEx (OS-visible only)";
    case InstalledMemorySource::kNone:
    default:
      return "unavailable";
  }
}

MemoryDynamic QueryMemoryDynamic() {
  MemoryDynamic d;

  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (::GlobalMemoryStatusEx(&status)) {
    d.total_physical_bytes = status.ullTotalPhys;
    d.available_physical_bytes = status.ullAvailPhys;
    d.used_physical_bytes =
        status.ullTotalPhys > status.ullAvailPhys
            ? status.ullTotalPhys - status.ullAvailPhys
            : 0;
    d.load_percent = static_cast<double>(status.dwMemoryLoad);
  }

  PERFORMANCE_INFORMATION perf{};
  perf.cb = sizeof(perf);
  if (::GetPerformanceInfo(&perf, sizeof(perf))) {
    const uint64_t page = perf.PageSize;
    d.page_size_bytes = page;
    d.commit_total_bytes = static_cast<uint64_t>(perf.CommitTotal) * page;
    d.commit_limit_bytes = static_cast<uint64_t>(perf.CommitLimit) * page;
    d.commit_peak_bytes = static_cast<uint64_t>(perf.CommitPeak) * page;
    d.system_cache_bytes = static_cast<uint64_t>(perf.SystemCache) * page;
    d.kernel_paged_bytes = static_cast<uint64_t>(perf.KernelPaged) * page;
    d.kernel_nonpaged_bytes = static_cast<uint64_t>(perf.KernelNonpaged) * page;
    d.process_count = perf.ProcessCount;
    d.thread_count = perf.ThreadCount;
    d.handle_count = perf.HandleCount;
  }

  return d;
}

MemoryInfo BuildMemoryInfo(const SmbiosTable& table,
                           uint64_t win32_installed_bytes,
                           const std::string& win32_error,
                           uint64_t usable_bytes) {
  MemoryInfo info;
  info.win32_installed_bytes = win32_installed_bytes;
  info.win32_installed_error = win32_error;
  info.usable_bytes = usable_bytes;
  info.smbios_ok = table.ok;
  info.smbios_error = table.error;
  info.smbios_version = table.VersionString();

  // ---- type 17: the individual memory devices ----
  int ordinal = 0;
  for (const SmbiosStructure* s : table.OfType(kSmbiosTypeMemoryDevice)) {
    MemoryModule m;
    m.index = ordinal++;
    m.device_locator = s->String(k17DeviceLocator);
    m.bank_locator = s->String(k17BankLocator);
    m.size_bytes = DecodeModuleSize(*s);
    m.memory_type_raw = s->Byte(k17MemoryType).value_or(0);
    m.memory_type = SmbiosMemoryTypeName(m.memory_type_raw);
    m.form_factor = SmbiosFormFactorName(s->Byte(k17FormFactor).value_or(0));
    m.speed_mts = DecodeSpeed(*s, k17Speed, k17ExtendedSpeed);
    m.configured_speed_mts =
        DecodeSpeed(*s, k17ConfiguredSpeed, k17ExtendedConfiguredSpeed);
    m.manufacturer = s->String(k17Manufacturer);
    m.part_number = s->String(k17PartNumber);
    m.serial_number = s->String(k17SerialNumber);
    m.rank = static_cast<uint8_t>(s->Byte(k17Attributes).value_or(0) & 0x0F);

    const uint16_t total_width = s->Word(k17TotalWidth).value_or(0);
    const uint16_t data_width = s->Word(k17DataWidth).value_or(0);
    if (total_width != 0xFFFF)
      m.total_width_bits = total_width;
    if (data_width != 0xFFFF)
      m.data_width_bits = data_width;
    m.ecc = m.total_width_bits > 0 && m.data_width_bits > 0 &&
            m.total_width_bits > m.data_width_bits;

    if (m.populated()) {
      info.smbios_sum_bytes += m.size_bytes;
      ++info.slots_used;
    }
    info.modules.push_back(std::move(m));
  }
  info.slots_total = static_cast<int>(info.modules.size());

  // ---- type 16: the motherboard's limits ----
  if (const SmbiosStructure* s =
          table.FirstOfType(kSmbiosTypePhysicalMemoryArray)) {
    info.array.present = true;
    info.array.max_capacity_bytes = DecodeArrayMaxCapacity(*s);
    info.array.slot_count =
        static_cast<int>(s->Word(k16NumberOfDevices).value_or(0));
    info.array.location =
        SmbiosMemoryArrayLocationName(s->Byte(k16Location).value_or(0));
    info.array.error_correction =
        SmbiosErrorCorrectionName(s->Byte(k16ErrorCorrection).value_or(0));
    if (info.array.slot_count > info.slots_total)
      info.slots_total = info.array.slot_count;
  }

  info.channel_estimate = DescribeChannels(info.modules);

  // ---- pick the number we trust, in order of directness ----
  if (win32_installed_bytes > 0) {
    info.installed_bytes = win32_installed_bytes;
    info.installed_source = InstalledMemorySource::kWin32InstalledApi;
  } else if (info.smbios_sum_bytes > 0) {
    info.installed_bytes = info.smbios_sum_bytes;
    info.installed_source = InstalledMemorySource::kSmbiosDeviceSum;
    info.warnings.push_back(
        "GetPhysicallyInstalledSystemMemory was unavailable" +
        (win32_error.empty() ? std::string() : " (" + win32_error + ")") +
        "; installed RAM is the sum of the SMBIOS memory devices.");
  } else if (usable_bytes > 0) {
    info.installed_bytes = usable_bytes;
    info.installed_source = InstalledMemorySource::kGlobalMemoryStatus;
    info.warnings.push_back(
        "Neither SMBIOS nor GetPhysicallyInstalledSystemMemory was readable, "
        "so installed RAM falls back to OS-visible memory and excludes "
        "anything the firmware reserved.");
  }

  if (info.installed_bytes > info.usable_bytes) {
    info.hardware_reserved_bytes = info.installed_bytes - info.usable_bytes;
  }

  // ---- cross-checks. Anything that does not reconcile gets said out loud ----
  if (win32_installed_bytes > 0 && info.smbios_sum_bytes > 0) {
    const uint64_t a = win32_installed_bytes;
    const uint64_t b = info.smbios_sum_bytes;
    const uint64_t diff = a > b ? a - b : b - a;
    if (diff > kMB) {
      info.warnings.push_back(
          "Installed RAM disagrees between sources: Windows reports " +
          FormatBytes(a) + " and the SMBIOS memory devices sum to " +
          FormatBytes(b) + ". The per-slot table is the more detailed of the "
          "two; a difference usually means the firmware omitted or "
          "double-counted a device.");
    }
  }

  if (info.installed_bytes > 0 && info.usable_bytes > info.installed_bytes) {
    info.warnings.push_back(
        "The OS reports more usable memory (" + FormatBytes(info.usable_bytes) +
        ") than the firmware says is installed (" +
        FormatBytes(info.installed_bytes) +
        "). The SMBIOS data is malformed; trust the usable figure.");
  }

  if (info.hardware_reserved_bytes > 1024ull * kMB) {
    info.warnings.push_back(
        FormatBytes(info.hardware_reserved_bytes) +
        " is reserved by hardware and unavailable to Windows. On a desktop "
        "this usually means an integrated GPU carve-out or a memory remap "
        "setting in the BIOS.");
  }

  if (info.array.present && info.array.max_capacity_bytes > 0 &&
      info.installed_bytes > info.array.max_capacity_bytes) {
    info.warnings.push_back(
        "Firmware reports a maximum capacity (" +
        FormatBytes(info.array.max_capacity_bytes) +
        ") below the amount actually installed (" +
        FormatBytes(info.installed_bytes) +
        "), so the maximum is not reliable on this board.");
  }

  if (!table.ok && !table.error.empty()) {
    info.warnings.push_back("SMBIOS: " + table.error);
  } else if (table.ok && !table.error.empty()) {
    info.warnings.push_back("SMBIOS parsed with a problem: " + table.error);
  }

  return info;
}

MemoryInfo QueryMemoryInfo() {
  // 1. Windows reading SMBIOS on our behalf. Returns kilobytes.
  uint64_t win32_installed = 0;
  std::string win32_error;
  ULONGLONG installed_kb = 0;
  if (::GetPhysicallyInstalledSystemMemory(&installed_kb) && installed_kb > 0) {
    win32_installed = static_cast<uint64_t>(installed_kb) * kKB;
  } else {
    const DWORD err = ::GetLastError();
    win32_error = Win32ErrorString(err);
    if (err == ERROR_INVALID_DATA) {
      win32_error +=
          " (the firmware SMBIOS data is malformed - common under "
          "virtualisation)";
    }
  }

  // 2. What the OS can actually hand out.
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  uint64_t usable = 0;
  if (::GlobalMemoryStatusEx(&status))
    usable = status.ullTotalPhys;

  // 3. Our own read of the firmware table, for per-slot detail and the board
  //    maximum.
  const SmbiosTable table = ReadSmbios();

  MemoryInfo info = BuildMemoryInfo(table, win32_installed, win32_error, usable);
  info.dynamic = QueryMemoryDynamic();
  return info;
}

}  // namespace cad::sys
