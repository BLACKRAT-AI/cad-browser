// CAD Browser - user settings, backed by settings.json.
//
// Every value has a default that makes the browser behave correctly if the
// file is missing or corrupt, because the file is user-editable and a bad one
// must never stop the browser from starting.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "browser/json.h"

namespace cad {

struct Settings {
  // ---- general ----
  // "restore" | "newtab" | "homepage"
  std::string startup_mode = "restore";
  std::string homepage = "cad://newtab";
  // "%s" is replaced with the percent-encoded query.
  std::string search_url = "https://duckduckgo.com/?q=%s";
  std::string search_name = "DuckDuckGo";
  // "system" | "light" | "dark"
  std::string theme = "system";
  bool show_bookmarks_bar = false;
  // The tab strip and address bar float above the page and appear when the
  // pointer reaches the top edge, so the window is effectively full screen the
  // rest of the time. Turn off for a permanently visible toolbar.
  bool chrome_auto_hide = true;

  // ---- site behaviour ----
  // On onshape.com, collapse the page's own toolbar and instance tree so the
  // 3D viewport fills the window, and bring them back when the pointer
  // reaches a window edge. The viewport is the part of a CAD page anyone is
  // actually looking at, and Onshape gives roughly a third of the window to
  // chrome that is only needed a few seconds at a time.
  bool onshape_focus_mode = true;

  // ---- performance ----
  bool gpu_prefer_high_performance = true;
  // Overrides Chromium's GPU blocklist. Off by default: the blocklist exists
  // because some driver and hardware combinations genuinely crash.
  bool force_hardware_acceleration = false;
  bool uncapped_fps_default = false;
  // Maximum JavaScript heap per tab, in megabytes. 0 derives it from installed
  // RAM. Chromium's own limit is about 4 GB whatever the machine has, which a
  // large CAD assembly can exceed.
  int js_heap_mb = 0;
  // Let pages use more of the graphics card's memory than Chromium's default
  // estimate allows. On by default: a CAD viewport is the case that needs it.
  bool gpu_large_memory_budget = true;
  double discard_available_fraction = 0.12;
  double discard_browser_fraction = 0.60;
  int discard_idle_minutes = 30;
  int discard_tab_count = 20;
  bool discard_enabled = true;
  // Empty means %LOCALAPPDATA%\CADBrowser\Cache.
  std::string cache_path;

  // ---- privacy ----
  bool blocklist_enabled = true;
  std::vector<std::string> allowed_sites;

  // ---- overlay ----
  bool overlay_enabled = false;
  // Which edge of the page area the stats bar docks to:
  // "top" | "bottom" | "left" | "right".
  std::string overlay_position = "bottom";
  int overlay_opacity = 72;  // percent
  bool overlay_show_fps = true;
  bool overlay_show_gpu = true;
  bool overlay_show_memory = true;
  bool overlay_show_cpu = true;
  bool overlay_show_tabs = true;

  // ---- window ----
  int window_width = 1400;
  int window_height = 900;
  bool window_maximized = false;

  // True the first time the browser runs, used to apply the GPU preference
  // and show the welcome page once.
  bool first_run_complete = false;
};

class SettingsStore {
 public:
  SettingsStore() = default;

  // Reads `path`. A missing or invalid file leaves defaults in place and is
  // reported through `had_error`, not by failing.
  void Load(const std::wstring& path);

  // Writes atomically: temp file then rename, so a crash mid-write cannot
  // leave a truncated settings file behind.
  bool Save() const;

  const Settings& settings() const { return settings_; }
  Settings& mutable_settings() { return settings_; }
  const std::wstring& path() const { return path_; }
  bool had_error() const { return had_error_; }
  const std::string& error() const { return error_; }

  static Settings FromJson(const JsonValue& json);
  static JsonValue ToJson(const Settings& settings);

 private:
  Settings settings_;
  std::wstring path_;
  bool had_error_ = false;
  std::string error_;
};

// Writes `contents` to `path` via a temporary file and a rename.
bool WriteFileAtomically(const std::wstring& path, const std::string& contents);
bool ReadFileToString(const std::wstring& path, std::string* contents);

}  // namespace cad
