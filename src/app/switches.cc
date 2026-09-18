#include "app/switches.h"

namespace cad {
namespace {

// Features we turn off. Chromium logs and ignores names it does not know, so a
// renamed feature degrades to "no effect" rather than a startup failure; the
// log is checked on every CEF version bump.
constexpr char kDisabledFeatures[] =
    "Translate,"                          // no translation service calls
    "MediaRouter,"                        // no Cast discovery traffic
    "OptimizationHints,"                  // no hint fetching
    "OptimizationGuideModelDownloading,"  // no model downloads
    "AutofillServerCommunication,"        // no autofill uploads
    "InterestFeedContentSuggestions,"     // no feed
    "SegmentationPlatform,"               // no on-device behaviour profiling
    // Push messaging pulls in Google Cloud Messaging, which registers with a
    // Google endpoint at startup. Verified against debug.log: without this the
    // browser logs a GCM registration attempt on every launch.
    //
    // The Notifications feature itself stays on: the Notification API works
    // locally without any push service, and turning it off would break a
    // standard web API rather than remove a Google dependency.
    "PushMessaging";

}  // namespace

const std::vector<FlagNote>& DocumentedFlags() {
  static const std::vector<FlagNote> kFlags = {
      // ---- no Google plumbing. PLAN.md section 7 item 1 ----
      {"--disable-background-networking",
       "Stops variations, field trial and component traffic that a CAD "
       "workstation never benefits from.",
       false},
      {"--disable-component-update",
       "No background component downloads; the browser updates by being "
       "replaced, not by phoning home.",
       false},
      {"--disable-domain-reliability",
       "No reliability beacons to Google.", false},
      {"--no-pings",
       "Never sends hyperlink auditing pings.", false},
      {"--disable-client-side-phishing-detection",
       "Removes the phishing model and its updates. No Safe Browsing service "
       "is configured either way.",
       false},
      {"--no-default-browser-check",
       "Never prompts to become the default browser.", false},
      {"--no-first-run",
       "Skips Chromium's own first-run flow; CAD Browser has its own.", false},
      {"--disable-features=<list>",
       "Translate, MediaRouter, OptimizationHints, "
       "OptimizationGuideModelDownloading, AutofillServerCommunication, "
       "InterestFeedContentSuggestions, SegmentationPlatform.",
       false},

      // ---- GPU. PLAN.md section 4.4 ----
      {"--use-angle=d3d11",
       "Pins ANGLE to D3D11 so a driver quirk cannot silently drop us to a "
       "slower backend mid-session.",
       false},
      {"--enable-gpu-rasterization",
       "Rasterise on the GPU. Already the default on supported hardware; set "
       "explicitly so the setting is visible and testable.",
       false},
      {"--enable-zero-copy",
       "Avoids a copy of every rasterised tile.", false},

      // ---- opt-in only ----
      {"--ignore-gpu-blocklist",
       "Only when Settings > Performance > Force hardware acceleration is on. "
       "The blocklist exists because some driver and hardware combinations "
       "crash.",
       false},
      {"--disable-frame-rate-limit",
       "Only when a tab is set to uncapped FPS. Off by default because an "
       "uncapped compositor burns power for no visible benefit.",
       false},
  };
  return kFlags;
}

void ApplyChromiumSwitches(CefRefPtr<CefCommandLine> command_line,
                           const SwitchOptions& options) {
  if (!command_line) {
    return;
  }

  auto set = [&command_line](const char* name) {
    if (!command_line->HasSwitch(name)) {
      command_line->AppendSwitch(name);
    }
  };
  auto set_value = [&command_line](const char* name, const char* value) {
    if (!command_line->HasSwitch(name)) {
      command_line->AppendSwitchWithValue(name, value);
    }
  };

  // --disable-features is a comma-separated list, and CEF has already put its
  // own entries on it by the time this runs. Skipping when the switch exists
  // would silently drop every feature we ask to disable, which is exactly what
  // happened before this merge was added: the child processes came up with
  // CEF's list and none of ours. Appending is the only correct handling for a
  // list-valued switch.
  // Appends to a switch whose value is a list, using the given separator.
  // Some of these are comma separated (--disable-features) and some are space
  // separated (--js-flags).
  auto append_to_list_with = [&command_line](const char* name,
                                             const char* separator,
                                             const std::string& addition) {
    std::string merged = command_line->GetSwitchValue(name).ToString();
    if (merged.find(addition) == std::string::npos) {
      if (!merged.empty()) {
        merged += separator;
      }
      merged += addition;
    }
    command_line->AppendSwitchWithValue(name, merged);
  };

  auto append_to_list = [&command_line](const char* name,
                                        const std::string& additions) {
    std::string merged = command_line->GetSwitchValue(name).ToString();
    for (size_t start = 0; start <= additions.size();) {
      const size_t comma = additions.find(',', start);
      const size_t end = comma == std::string::npos ? additions.size() : comma;
      const std::string entry = additions.substr(start, end - start);
      start = end + 1;
      if (entry.empty()) {
        continue;
      }
      // Avoid duplicating an entry CEF already listed.
      const size_t found = merged.find(entry);
      const bool already_present =
          found != std::string::npos &&
          (found == 0 || merged[found - 1] == ',') &&
          (found + entry.size() == merged.size() ||
           merged[found + entry.size()] == ',');
      if (already_present) {
        continue;
      }
      if (!merged.empty()) {
        merged += ",";
      }
      merged += entry;
      if (comma == std::string::npos) {
        break;
      }
    }
    command_line->AppendSwitchWithValue(name, merged);
  };

  // No Google plumbing.
  set("disable-background-networking");
  set("disable-component-update");
  set("disable-domain-reliability");
  set("no-pings");
  set("disable-client-side-phishing-detection");
  set("no-default-browser-check");
  set("no-first-run");
  append_to_list("disable-features", kDisabledFeatures);

  // GPU path.
  set_value("use-angle", "d3d11");
  set("enable-gpu-rasterization");
  set("enable-zero-copy");

  // V8 heap. Chromium caps a renderer's old space at roughly 4 GB regardless
  // of installed memory. A large CAD assembly hits that ceiling and the tab
  // dies with "V8 javascript OOM" while tens of gigabytes sit free, which is
  // exactly what happened on a 64 GB machine with a big Onshape document.
  // js-flags is space separated and forwarded to renderers by Chromium.
  if (options.js_heap_mb > 0) {
    append_to_list_with(
        "js-flags", " ",
        "--max-old-space-size=" + std::to_string(options.js_heap_mb));
  }

  // GPU memory budget. Chromium's own estimate is conservative on cards with
  // a lot of VRAM, and a CAD viewport is exactly the case that can use it.
  if (options.gpu_memory_mb > 0) {
    set_value("force-gpu-mem-available-mb",
              std::to_string(options.gpu_memory_mb).c_str());
  }

  if (options.force_hardware_acceleration) {
    set("ignore-gpu-blocklist");
  }
  if (options.uncapped_frame_rate) {
    set("disable-frame-rate-limit");
    set("disable-gpu-vsync");
  }

  // Ours, not Chromium's: read back by the renderer process. Chromium ignores
  // switches it does not know, which is what makes this the simplest way to
  // get a setting into every renderer at the moment it starts.
  if (options.onshape_focus) {
    set("cad-onshape-focus");
  }

  // Deliberately NOT set, and the reasons, so nobody adds them later thinking
  // they were an oversight:
  //   --single-process                 destroys the isolation that makes a
  //                                    browser safe to run untrusted content in
  //   --no-sandbox                     same
  //   --disable-site-isolation-trials  same
  //   --disable-gpu-driver-bug-workarounds
  //                                    those workarounds are why rendering is
  //                                    correct on real drivers
  //   --disable-features=CalculateNativeWinOcclusion
  //                                    occlusion detection saves GPU work when
  //                                    the window is covered; keep it
}

}  // namespace cad
