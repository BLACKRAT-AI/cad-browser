// CAD Browser - the Chromium command line we hand to CEF.
//
// PLAN.md rule: every flag here carries a reason and is listed in
// docs/flags.md. A flag with no measurement behind it is a guess, and guesses
// about Chromium's defaults are usually wrong, so each one is marked with
// whether it has been benchmarked yet.
#pragma once

#include <string>
#include <vector>

#include "include/cef_command_line.h"

namespace cad {

struct FlagNote {
  const char* flag;      // "--disable-background-networking"
  const char* reason;    // why it is set
  bool measured;         // has an M6 before/after number behind it
};

struct SwitchOptions {
  // Settings > Performance > "Force hardware acceleration". Overrides
  // Chromium's GPU blocklist, which exists for good reasons, so it is opt-in.
  bool force_hardware_acceleration = false;
  // Removes the frame rate cap. Useful for benchmarking, wasteful otherwise.
  bool uncapped_frame_rate = false;

  // Maximum V8 old-space per renderer, in megabytes. 0 leaves Chromium's
  // default, which caps at about 4 GB however much RAM the machine has. A
  // large CAD assembly exceeds that and the tab is killed while most of the
  // machine's memory sits unused, which is the single most important thing
  // this browser fixes for its actual workload.
  int js_heap_mb = 0;

  // GPU memory budget in megabytes, 0 to leave Chromium's own estimate.
  int gpu_memory_mb = 0;

  // Settings > General > Onshape focus mode. Passed through to renderers as
  // --cad-onshape-focus, because a renderer can be spawned at any moment and
  // has to start in the right state without asking the browser process.
  bool onshape_focus = false;
};

// Applies our switches to the command line CEF is about to use. Called from
// CefApp::OnBeforeCommandLineProcessing in every process.
void ApplyChromiumSwitches(CefRefPtr<CefCommandLine> command_line,
                           const SwitchOptions& options);

// The documented flag table, for cad://sysinfo and docs/flags.md.
const std::vector<FlagNote>& DocumentedFlags();

}  // namespace cad
