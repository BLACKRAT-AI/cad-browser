#include "cad_test.h"

#include "sys/perf_counters.h"
#include "sys/process_tree.h"
#include "sys/win_util.h"

namespace {

using cad::sys::ClassifyCommandLine;
using cad::sys::FormatBytes;
using cad::sys::FormatHex32;
using cad::sys::GpuInstanceKey;
using cad::sys::ParseGpuInstanceName;
using cad::sys::ProcessKind;

}  // namespace

CAD_TEST(FormatBytesPicksSensibleUnits) {
  CAD_EXPECT_EQ(FormatBytes(0), std::string("0 B"));
  CAD_EXPECT_EQ(FormatBytes(1023), std::string("1023 B"));
  CAD_EXPECT_EQ(FormatBytes(1024), std::string("1.0 KB"));
  CAD_EXPECT_EQ(FormatBytes(1024ull * 1024), std::string("1.0 MB"));
  CAD_EXPECT_EQ(FormatBytes(64ull * 1024 * 1024 * 1024),
                std::string("64.0 GB"));
  CAD_EXPECT_EQ(FormatBytes(1024ull * 1024 * 1024, 0), std::string("1 GB"));
}

CAD_TEST(FormatHexPadsToFourDigits) {
  CAD_EXPECT_EQ(FormatHex32(0x10DE), std::string("0x10DE"));
  CAD_EXPECT_EQ(FormatHex32(0x8C), std::string("0x008C"));
  CAD_EXPECT_EQ(FormatHex32(0), std::string("0x0000"));
}

CAD_TEST(TrimAsciiRemovesControlCharacters) {
  CAD_EXPECT_EQ(cad::sys::TrimAscii("  hello \r\n"), std::string("hello"));
  CAD_EXPECT_EQ(cad::sys::TrimAscii(""), std::string());
  CAD_EXPECT_EQ(cad::sys::TrimAscii("   "), std::string());
}

CAD_TEST(GpuEngineInstanceNamesAreParsed) {
  GpuInstanceKey key;
  CAD_EXPECT(ParseGpuInstanceName(
      L"pid_15884_luid_0x00000000_0x0000F4B3_phys_0_eng_3_engtype_3D", &key));
  CAD_EXPECT(key.has_pid);
  CAD_EXPECT_EQ(key.pid, uint32_t(15884));
  CAD_EXPECT_EQ(key.luid, int64_t(0x0000F4B3));
  CAD_EXPECT_EQ(key.phys, 0);
  CAD_EXPECT_EQ(key.engine, 3);
  CAD_EXPECT_EQ(key.engine_type, std::string("3D"));
}

CAD_TEST(GpuAdapterMemoryInstanceNamesHaveNoPid) {
  GpuInstanceKey key;
  CAD_EXPECT(
      ParseGpuInstanceName(L"luid_0x00000000_0x0000F4B3_phys_0", &key));
  CAD_EXPECT(!key.has_pid);
  CAD_EXPECT_EQ(key.luid, int64_t(0x0000F4B3));
  CAD_EXPECT_EQ(key.engine, -1);
}

CAD_TEST(GpuInstanceLuidHighPartIsSigned) {
  // A high part with the top bit set must sign-extend the way LUID does, or it
  // will never match the value DXGI reported.
  GpuInstanceKey key;
  CAD_EXPECT(
      ParseGpuInstanceName(L"luid_0xFFFFFFFF_0x0000ABCD_phys_1", &key));
  LARGE_INTEGER expected;
  expected.HighPart = -1;
  expected.LowPart = 0x0000ABCD;
  CAD_EXPECT_EQ(key.luid, expected.QuadPart);
  CAD_EXPECT_EQ(key.phys, 1);
}

CAD_TEST(MalformedGpuInstanceNamesAreRejected) {
  GpuInstanceKey key;
  CAD_EXPECT(!ParseGpuInstanceName(L"", &key));
  CAD_EXPECT(!ParseGpuInstanceName(L"Total", &key));
  CAD_EXPECT(!ParseGpuInstanceName(L"pid_notanumber_luid_0x0_0x0", &key));
  CAD_EXPECT(!ParseGpuInstanceName(L"luid_0x00000000", &key));
}

CAD_TEST(ChromiumProcessTypesAreClassified) {
  std::string sub_type;
  CAD_EXPECT(ClassifyCommandLine("CADBrowser.exe --type=renderer --lang=en-US",
                                 &sub_type) == ProcessKind::kRenderer);
  CAD_EXPECT(ClassifyCommandLine("CADBrowser.exe --type=gpu-process",
                                 &sub_type) == ProcessKind::kGpu);
  CAD_EXPECT(ClassifyCommandLine("CADBrowser.exe --type=crashpad-handler",
                                 &sub_type) == ProcessKind::kCrashpad);
  // No --type switch at all means the browser process.
  CAD_EXPECT(ClassifyCommandLine("CADBrowser.exe", &sub_type) ==
             ProcessKind::kBrowser);
}

CAD_TEST(UtilitySubTypeIsPrettyPrinted) {
  std::string sub_type;
  CAD_EXPECT(ClassifyCommandLine(
                 "CADBrowser.exe --type=utility "
                 "--utility-sub-type=network.mojom.NetworkService",
                 &sub_type) == ProcessKind::kUtility);
  CAD_EXPECT_EQ(sub_type, std::string("Network"));

  CAD_EXPECT(ClassifyCommandLine(
                 "CADBrowser.exe --type=utility "
                 "--utility-sub-type=audio.mojom.AudioService --field=x",
                 &sub_type) == ProcessKind::kUtility);
  CAD_EXPECT_EQ(sub_type, std::string("Audio"));
}

CAD_TEST(QuotedSwitchValuesAreParsed) {
  std::string sub_type;
  CAD_EXPECT(ClassifyCommandLine("\"C:\\Program Files\\CADBrowser.exe\" "
                                 "--type=\"renderer\" --x=1",
                                 &sub_type) == ProcessKind::kRenderer);
}
