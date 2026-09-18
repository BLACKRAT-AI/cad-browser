#include "sys/win_util.h"

#include "sys/os_info.h"

// RTL_OSVERSIONINFOW, the argument type for RtlGetVersion.
#include <winternl.h>

#include "sys/smbios.h"

namespace cad::sys {
namespace {

constexpr wchar_t kCurrentVersionKey[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

// SMBIOS type 0 (BIOS) and type 1 / 2 (system, baseboard) string offsets.
constexpr size_t k0Vendor = 0x04;
constexpr size_t k0Version = 0x05;
constexpr size_t k0ReleaseDate = 0x08;
constexpr size_t k1Manufacturer = 0x04;
constexpr size_t k1ProductName = 0x05;
constexpr size_t k2Manufacturer = 0x04;
constexpr size_t k2Product = 0x05;

typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);

}  // namespace

std::string OsInfo::Describe() const {
  std::string out = product_name.empty() ? "Windows" : product_name;
  if (!display_version.empty())
    out += " " + display_version;
  out += " (build " + std::to_string(build);
  if (ubr != 0)
    out += "." + std::to_string(ubr);
  out += ")";
  return out;
}

OsInfo QueryOsInfo() {
  OsInfo info;

  // RtlGetVersion is the only call that reports the real build number without
  // a manifest declaring support for it.
  if (const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
    const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
    if (rtl_get_version) {
      RTL_OSVERSIONINFOW version{};
      version.dwOSVersionInfoSize = sizeof(version);
      if (rtl_get_version(&version) == 0) {
        info.major = version.dwMajorVersion;
        info.minor = version.dwMinorVersion;
        info.build = version.dwBuildNumber;
      }
    }
  }

  const auto product =
      RegReadString(HKEY_LOCAL_MACHINE, kCurrentVersionKey, L"ProductName");
  if (product.has_value())
    info.product_name = ToUtf8(*product);

  const auto display =
      RegReadString(HKEY_LOCAL_MACHINE, kCurrentVersionKey, L"DisplayVersion");
  if (display.has_value())
    info.display_version = ToUtf8(*display);

  const auto edition =
      RegReadString(HKEY_LOCAL_MACHINE, kCurrentVersionKey, L"EditionID");
  if (edition.has_value())
    info.edition = ToUtf8(*edition);

  const auto ubr = RegReadDword(HKEY_LOCAL_MACHINE, kCurrentVersionKey, L"UBR");
  if (ubr.has_value())
    info.ubr = *ubr;

  // Windows 11 keeps reporting ProductName as "Windows 10" in the registry;
  // the build number is what actually distinguishes them.
  if (info.build >= 22000 &&
      info.product_name.rfind("Windows 10", 0) == 0) {
    info.product_name = "Windows 11" + info.product_name.substr(10);
  }

  // Product type: a workstation reports VER_NT_WORKSTATION. We only use this
  // to avoid calling a server SKU "Windows 11" in the System page.
  info.is_server = info.edition.find("Server") != std::string::npos;

  return info;
}

MachineInfo QueryMachineInfo() {
  MachineInfo info;
  const SmbiosTable table = ReadSmbios();
  if (!table.ok)
    return info;

  if (const SmbiosStructure* s = table.FirstOfType(kSmbiosTypeBios)) {
    info.bios_vendor = s->String(k0Vendor);
    info.bios_version = s->String(k0Version);
    info.bios_date = s->String(k0ReleaseDate);
  }
  if (const SmbiosStructure* s = table.FirstOfType(kSmbiosTypeSystem)) {
    info.manufacturer = s->String(k1Manufacturer);
    info.model = s->String(k1ProductName);
  }
  if (const SmbiosStructure* s = table.FirstOfType(kSmbiosTypeBaseboard)) {
    info.baseboard_manufacturer = s->String(k2Manufacturer);
    info.baseboard_product = s->String(k2Product);
  }
  return info;
}

}  // namespace cad::sys
