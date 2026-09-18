#include "cad_test.h"
#include "smbios_fixtures.h"

#include "sys/memory_info.h"
#include "sys/smbios.h"

namespace {

using cad::sys::BuildMemoryInfo;
using cad::sys::InstalledMemorySource;
using cad::sys::MemoryInfo;
using cad::sys::ParseSmbiosTableData;
using cad::sys::SmbiosTable;

constexpr uint64_t kKB = 1024ull;
constexpr uint64_t kMB = 1024ull * 1024ull;
constexpr uint64_t kGB = 1024ull * kMB;
constexpr uint64_t kTB = 1024ull * kGB;

SmbiosTable Parse(const std::vector<uint8_t>& bytes) {
  return ParseSmbiosTableData(bytes.data(), bytes.size(), 3, 4);
}

}  // namespace

CAD_TEST(SmbiosParsesDesktopFixture) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  CAD_EXPECT(table.ok);
  CAD_EXPECT_EQ(table.error, std::string());
  CAD_EXPECT_EQ(table.OfType(17).size(), size_t(4));
  CAD_EXPECT_EQ(table.OfType(16).size(), size_t(1));
  CAD_EXPECT_EQ(table.VersionString(), std::string("3.4"));
}

CAD_TEST(SmbiosStringTableIsOneBased) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  const auto devices = table.OfType(17);
  CAD_EXPECT(!devices.empty());
  // Offset 0x10 holds the device locator string index.
  CAD_EXPECT_EQ(devices[0]->String(0x10),
                std::string("Controller0-ChannelA-DIMM0"));
  CAD_EXPECT_EQ(devices[0]->String(0x11), std::string("BANK 0"));
  // Index 0 means "no string", not "the first string".
  CAD_EXPECT_EQ(devices[0]->String(0x19), std::string());
}

CAD_TEST(DesktopMemorySumsTwoPopulatedSlots) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 31 * kGB);

  CAD_EXPECT_EQ(info.smbios_sum_bytes, 32 * kGB);
  CAD_EXPECT_EQ(info.installed_bytes, 32 * kGB);
  CAD_EXPECT(info.installed_source == InstalledMemorySource::kSmbiosDeviceSum);
  CAD_EXPECT_EQ(info.slots_used, 2);
  CAD_EXPECT_EQ(info.slots_total, 4);
  CAD_EXPECT_EQ(info.array.max_capacity_bytes, 128 * kGB);
  CAD_EXPECT_EQ(info.array.slot_count, 4);
  CAD_EXPECT_EQ(info.modules.size(), size_t(4));
  CAD_EXPECT_EQ(info.modules[0].memory_type, std::string("DDR5"));
  CAD_EXPECT_EQ(info.modules[0].form_factor, std::string("DIMM"));
  CAD_EXPECT_EQ(info.modules[0].speed_mts, uint32_t(4800));
  CAD_EXPECT_EQ(info.modules[0].configured_speed_mts, uint32_t(4800));
  CAD_EXPECT_EQ(info.modules[0].manufacturer, std::string("Corsair"));
  CAD_EXPECT_EQ(info.modules[0].rank, uint8_t(2));
  CAD_EXPECT(!info.modules[1].populated());
}

CAD_TEST(TwoControllersCountAsDualChannel) {
  // Both modules sit on "ChannelA"; what makes them a pair is being on
  // different controllers. Keying on the channel letter alone would call this
  // single channel.
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 31 * kGB);
  CAD_EXPECT_EQ(info.channel_estimate,
                std::string("Dual channel (estimated)"));
}

CAD_TEST(ExtendedSizeEscapeIsDecoded) {
  // 64 GB cannot fit the plain size field, so firmware writes 0x7FFF and puts
  // the real value in Extended Size.
  const SmbiosTable table = Parse(cad_test::LaptopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 63 * kGB);

  CAD_EXPECT_EQ(info.modules.size(), size_t(1));
  CAD_EXPECT_EQ(info.modules[0].size_bytes, 64 * kGB);
  CAD_EXPECT_EQ(info.modules[0].memory_type, std::string("LPDDR5"));
  CAD_EXPECT_EQ(info.modules[0].form_factor, std::string("SODIMM"));
}

CAD_TEST(ExtendedSpeedEscapeIsDecoded) {
  const SmbiosTable table = Parse(cad_test::LaptopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 63 * kGB);
  CAD_EXPECT_EQ(info.modules[0].speed_mts, uint32_t(7500));
  CAD_EXPECT_EQ(info.modules[0].configured_speed_mts, uint32_t(7500));
}

CAD_TEST(ExtendedMaximumCapacityEscapeIsDecoded) {
  const SmbiosTable table = Parse(cad_test::LaptopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 63 * kGB);
  CAD_EXPECT_EQ(info.array.max_capacity_bytes, 2 * kTB);
}

CAD_TEST(KilobyteSizeEncodingIsDecoded) {
  // Bit 15 set means the size is in kilobytes rather than megabytes.
  const SmbiosTable table = Parse(cad_test::VirtualMachineFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 2 * kMB);
  CAD_EXPECT_EQ(info.modules.size(), size_t(1));
  CAD_EXPECT_EQ(info.modules[0].size_bytes, 2048 * kKB);
  CAD_EXPECT_EQ(info.modules[0].manufacturer, std::string());
  CAD_EXPECT_EQ(info.modules[0].device_locator, std::string("RAM slot #0"));
}

CAD_TEST(UnknownWidthsDoNotReportEcc) {
  const SmbiosTable table = Parse(cad_test::VirtualMachineFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 0, "", 2 * kMB);
  CAD_EXPECT(!info.modules[0].ecc);
  CAD_EXPECT_EQ(info.modules[0].total_width_bits, uint16_t(0));
}

CAD_TEST(Win32ApiWinsOverSmbiosSum) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  const MemoryInfo info = BuildMemoryInfo(table, 32 * kGB, "", 31 * kGB);
  CAD_EXPECT(info.installed_source ==
             InstalledMemorySource::kWin32InstalledApi);
  CAD_EXPECT_EQ(info.installed_bytes, 32 * kGB);
  CAD_EXPECT_EQ(info.hardware_reserved_bytes, 1 * kGB);
}

CAD_TEST(DisagreeingSourcesProduceAWarning) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  // Windows says 16 GB, the slot table sums to 32 GB. That must be surfaced.
  const MemoryInfo info = BuildMemoryInfo(table, 16 * kGB, "", 15 * kGB);
  bool mentions_disagreement = false;
  for (const std::string& warning : info.warnings) {
    if (warning.find("disagrees") != std::string::npos)
      mentions_disagreement = true;
  }
  CAD_EXPECT(mentions_disagreement);
}

CAD_TEST(FallsBackToGlobalMemoryStatusWhenFirmwareIsUnreadable) {
  SmbiosTable empty;
  empty.ok = false;
  empty.error = "GetSystemFirmwareTable(RSMB) size query failed";
  const MemoryInfo info =
      BuildMemoryInfo(empty, 0, "ERROR_INVALID_DATA", 16 * kGB);
  CAD_EXPECT(info.installed_source ==
             InstalledMemorySource::kGlobalMemoryStatus);
  CAD_EXPECT_EQ(info.installed_bytes, 16 * kGB);
  CAD_EXPECT(!info.warnings.empty());
}

CAD_TEST(LargeHardwareReservationIsExplained) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  // 4 GB carved out, as an integrated GPU on a misconfigured board would do.
  const MemoryInfo info = BuildMemoryInfo(table, 32 * kGB, "", 28 * kGB);
  bool mentions_reserved = false;
  for (const std::string& warning : info.warnings) {
    if (warning.find("reserved by hardware") != std::string::npos)
      mentions_reserved = true;
  }
  CAD_EXPECT(mentions_reserved);
}

CAD_TEST(TruncatedTableDoesNotCrashAndIsReported) {
  const std::vector<uint8_t> full = cad_test::DesktopFixture();
  const SmbiosTable complete =
      ParseSmbiosTableData(full.data(), full.size(), 3, 4);
  CAD_EXPECT(complete.error.empty());

  // Every prefix must parse without reading out of bounds, must never invent
  // structures, and must say something went wrong: a cut table either ends
  // mid-structure or loses its end-of-table marker.
  for (size_t keep = 0; keep < full.size(); ++keep) {
    const SmbiosTable table = ParseSmbiosTableData(full.data(), keep, 3, 4);
    CAD_EXPECT(table.structures.size() <= complete.structures.size());
    CAD_EXPECT(!table.error.empty());
  }
}

CAD_TEST(StructureWithImpossibleLengthStopsTheWalk) {
  // A length below the 4-byte header means we have lost sync; continuing would
  // reinterpret arbitrary bytes as structures.
  std::vector<uint8_t> bytes = {17, 2, 0x00, 0x11, 0x00, 0x00};
  const SmbiosTable table = ParseSmbiosTableData(bytes.data(), bytes.size());
  CAD_EXPECT(!table.error.empty());
  CAD_EXPECT_EQ(table.structures.size(), size_t(0));
}

CAD_TEST(EmptyTableIsHandled) {
  const SmbiosTable table = ParseSmbiosTableData(nullptr, 0);
  CAD_EXPECT(!table.ok);
  CAD_EXPECT(!table.error.empty());
}

CAD_TEST(FieldReadsPastStructureEndReturnNullopt) {
  const SmbiosTable table = Parse(cad_test::DesktopFixture());
  const auto devices = table.OfType(17);
  CAD_EXPECT(!devices.empty());
  // Well past the end of any type 17 structure.
  CAD_EXPECT(!devices[0]->Qword(0x400).has_value());
  CAD_EXPECT(!devices[0]->Byte(0xFF).has_value());
}
