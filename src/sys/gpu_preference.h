// CAD Browser - steering Windows onto the discrete GPU.
//
// PLAN.md section 4.3. On a hybrid laptop this is the single change that
// decides whether a CAD viewer runs at 15 fps or 60.
//
// A note on mechanism. PLAN.md originally also called for exporting
// NvOptimusEnablement / AmdPowerXpressRequestHighPerformance from the
// executable. Under CEF 144 the executable is a renamed copy of CEF's
// bootstrap.exe, which we do not compile, so those exports cannot be placed
// where the vendor drivers look for them. The per-executable registry
// preference below is the supported modern mechanism, it is what the Windows
// Settings UI writes, it needs no administrator rights, and it applies to
// every child process because they all share our executable path.
#pragma once

#include <string>

namespace cad::sys {

enum class GpuPreference {
  kUnset = -1,       // no entry: Windows decides
  kSystemDefault = 0,
  kPowerSaving = 1,
  kHighPerformance = 2,
};

struct GpuPreferenceStatus {
  std::string executable_path;
  GpuPreference preference = GpuPreference::kUnset;
  std::string raw_value;
  bool readable = false;
};

const char* GpuPreferenceName(GpuPreference preference);

// Reads the current per-executable preference for this process's exe.
GpuPreferenceStatus QueryGpuPreference();

// Writes the preference for this process's exe. Returns false if the registry
// write failed. Takes effect for processes started afterwards, which is why
// the browser applies it at first run and verifies with the GPU probe.
bool SetGpuPreference(GpuPreference preference);

// Removes our entry entirely, handing the choice back to Windows.
bool ClearGpuPreference();

// Opens Settings > System > Display > Graphics, for the case where an OEM
// control panel is overriding us and the user has to resolve it there.
bool OpenWindowsGraphicsSettings();

}  // namespace cad::sys
