// windows.h must precede shellapi.h; see the note in process_tree.cc.
#include "sys/win_util.h"

#include "sys/gpu_preference.h"

#include <shellapi.h>

namespace cad::sys {
namespace {

constexpr wchar_t kUserGpuPreferencesKey[] =
    L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

// Windows stores these as a small "key=value;" string rather than a DWORD.
std::wstring PreferenceValueString(GpuPreference preference) {
  switch (preference) {
    case GpuPreference::kPowerSaving:
      return L"GpuPreference=1;";
    case GpuPreference::kHighPerformance:
      return L"GpuPreference=2;";
    case GpuPreference::kSystemDefault:
      return L"GpuPreference=0;";
    case GpuPreference::kUnset:
    default:
      return L"";
  }
}

GpuPreference ParsePreferenceValue(const std::wstring& raw) {
  const size_t pos = raw.find(L"GpuPreference=");
  if (pos == std::wstring::npos)
    return GpuPreference::kUnset;
  const size_t digit = pos + 14;  // length of "GpuPreference="
  if (digit >= raw.size())
    return GpuPreference::kUnset;
  switch (raw[digit]) {
    case L'0': return GpuPreference::kSystemDefault;
    case L'1': return GpuPreference::kPowerSaving;
    case L'2': return GpuPreference::kHighPerformance;
    default: return GpuPreference::kUnset;
  }
}

}  // namespace

const char* GpuPreferenceName(GpuPreference preference) {
  switch (preference) {
    case GpuPreference::kSystemDefault: return "Let Windows decide";
    case GpuPreference::kPowerSaving: return "Power saving";
    case GpuPreference::kHighPerformance: return "High performance";
    case GpuPreference::kUnset:
    default: return "Not set";
  }
}

GpuPreferenceStatus QueryGpuPreference() {
  GpuPreferenceStatus status;
  const std::wstring exe = ExecutablePathW();
  status.executable_path = ToUtf8(exe);
  if (exe.empty())
    return status;

  const auto raw =
      RegReadString(HKEY_CURRENT_USER, kUserGpuPreferencesKey, exe.c_str());
  status.readable = true;
  if (!raw.has_value())
    return status;  // absent is normal, and means "Windows decides"

  status.raw_value = ToUtf8(*raw);
  status.preference = ParsePreferenceValue(*raw);
  return status;
}

bool SetGpuPreference(GpuPreference preference) {
  const std::wstring exe = ExecutablePathW();
  if (exe.empty())
    return false;
  if (preference == GpuPreference::kUnset)
    return ClearGpuPreference();
  return RegWriteString(HKEY_CURRENT_USER, kUserGpuPreferencesKey, exe.c_str(),
                        PreferenceValueString(preference));
}

bool ClearGpuPreference() {
  const std::wstring exe = ExecutablePathW();
  if (exe.empty())
    return false;
  return RegDeleteValueIfPresent(HKEY_CURRENT_USER, kUserGpuPreferencesKey,
                                 exe.c_str());
}

bool OpenWindowsGraphicsSettings() {
  const HINSTANCE result =
      ::ShellExecuteW(nullptr, L"open", L"ms-settings:display-advancedgraphics",
                      nullptr, nullptr, SW_SHOWNORMAL);
  // ShellExecute returns a value above 32 on success.
  return reinterpret_cast<INT_PTR>(result) > 32;
}

}  // namespace cad::sys
