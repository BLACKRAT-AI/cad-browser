#include "sys/win_util.h"

#include "browser/settings_store.h"

#include <algorithm>
#include <cstdio>

namespace cad {
namespace {

std::string ClampChoice(const std::string& value,
                        const std::vector<std::string>& allowed,
                        const std::string& fallback) {
  for (const std::string& candidate : allowed) {
    if (value == candidate)
      return value;
  }
  return fallback;
}

}  // namespace

bool ReadFileToString(const std::wstring& path, std::string* contents) {
  if (!contents || path.empty())
    return false;
  contents->clear();

  FILE* file = nullptr;
  if (::_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
    return false;

  char buffer[8192];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    contents->append(buffer, read);
  std::fclose(file);
  return true;
}

bool WriteFileAtomically(const std::wstring& path, const std::string& contents) {
  if (path.empty())
    return false;
  const std::wstring temp = path + L".tmp";

  FILE* file = nullptr;
  if (::_wfopen_s(&file, temp.c_str(), L"wb") != 0 || !file)
    return false;
  const size_t written =
      contents.empty() ? 0
                       : std::fwrite(contents.data(), 1, contents.size(), file);
  const bool complete = written == contents.size();
  std::fflush(file);
  std::fclose(file);
  if (!complete) {
    ::DeleteFileW(temp.c_str());
    return false;
  }

  // A rename over the existing file is what makes this atomic for readers.
  if (!::MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ::DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

Settings SettingsStore::FromJson(const JsonValue& json) {
  Settings s;
  if (!json.is_object())
    return s;

  if (const JsonValue* general = json.GetChildObject("general")) {
    s.startup_mode = ClampChoice(general->GetString("startup_mode", s.startup_mode),
                                 {"restore", "newtab", "homepage"}, "restore");
    s.homepage = general->GetString("homepage", s.homepage);
    s.search_url = general->GetString("search_url", s.search_url);
    s.search_name = general->GetString("search_name", s.search_name);
    s.theme = ClampChoice(general->GetString("theme", s.theme),
                          {"system", "light", "dark"}, "system");
    s.show_bookmarks_bar =
        general->GetBool("show_bookmarks_bar", s.show_bookmarks_bar);
    s.chrome_auto_hide =
        general->GetBool("chrome_auto_hide", s.chrome_auto_hide);
    s.onshape_focus_mode =
        general->GetBool("onshape_focus_mode", s.onshape_focus_mode);
  }

  if (const JsonValue* perf = json.GetChildObject("performance")) {
    s.gpu_prefer_high_performance =
        perf->GetBool("gpu_prefer_high_performance", s.gpu_prefer_high_performance);
    s.force_hardware_acceleration =
        perf->GetBool("force_hardware_acceleration", s.force_hardware_acceleration);
    s.uncapped_fps_default =
        perf->GetBool("uncapped_fps_default", s.uncapped_fps_default);
    s.js_heap_mb = std::clamp(
        static_cast<int>(perf->GetInt("js_heap_mb", s.js_heap_mb)), 0, 65536);
    s.gpu_large_memory_budget =
        perf->GetBool("gpu_large_memory_budget", s.gpu_large_memory_budget);
    s.discard_enabled = perf->GetBool("discard_enabled", s.discard_enabled);
    s.discard_available_fraction = std::clamp(
        perf->GetDouble("discard_available_fraction", s.discard_available_fraction),
        0.02, 0.50);
    s.discard_browser_fraction = std::clamp(
        perf->GetDouble("discard_browser_fraction", s.discard_browser_fraction),
        0.20, 0.95);
    s.discard_idle_minutes = std::clamp(
        static_cast<int>(perf->GetInt("discard_idle_minutes", s.discard_idle_minutes)),
        1, 24 * 60);
    s.discard_tab_count = std::clamp(
        static_cast<int>(perf->GetInt("discard_tab_count", s.discard_tab_count)),
        2, 1000);
    s.cache_path = perf->GetString("cache_path", s.cache_path);
  }
  if (const JsonValue* capture = json.GetChildObject("capture")) {
    s.capture_folder = capture->GetString("folder", s.capture_folder);
    if (s.capture_folder.empty()) {
      s.capture_folder = kDefaultCaptureFolder;
    }
  }

  if (const JsonValue* privacy = json.GetChildObject("privacy")) {
    s.blocklist_enabled =
        privacy->GetBool("blocklist_enabled", s.blocklist_enabled);
    if (const JsonValue* sites = privacy->GetChildArray("allowed_sites")) {
      s.allowed_sites.clear();
      for (const JsonValue& site : sites->items()) {
        if (site.is_string() && !site.string().empty())
          s.allowed_sites.push_back(site.string());
      }
    }
  }

  if (const JsonValue* overlay = json.GetChildObject("overlay")) {
    s.overlay_enabled = overlay->GetBool("enabled", s.overlay_enabled);

    // The overlay used to be a corner panel and its position was a corner.
    // Map the old values onto the nearest edge so an existing settings file
    // keeps the side the user chose instead of silently reverting.
    std::string position = overlay->GetString("position", s.overlay_position);
    if (position == "top-left" || position == "top-right") {
      position = "top";
    } else if (position == "bottom-left" || position == "bottom-right") {
      position = "bottom";
    }
    s.overlay_position =
        ClampChoice(position, {"top", "bottom", "left", "right"}, "bottom");
    s.overlay_opacity = std::clamp(
        static_cast<int>(overlay->GetInt("opacity", s.overlay_opacity)), 20, 100);
    s.overlay_show_fps = overlay->GetBool("show_fps", s.overlay_show_fps);
    s.overlay_show_gpu = overlay->GetBool("show_gpu", s.overlay_show_gpu);
    s.overlay_show_memory = overlay->GetBool("show_memory", s.overlay_show_memory);
    s.overlay_show_cpu = overlay->GetBool("show_cpu", s.overlay_show_cpu);
    s.overlay_show_tabs = overlay->GetBool("show_tabs", s.overlay_show_tabs);
  }

  if (const JsonValue* window = json.GetChildObject("window")) {
    s.window_width = std::clamp(
        static_cast<int>(window->GetInt("width", s.window_width)), 480, 16384);
    s.window_height = std::clamp(
        static_cast<int>(window->GetInt("height", s.window_height)), 360, 16384);
    s.window_maximized = window->GetBool("maximized", s.window_maximized);
  }

  s.first_run_complete = json.GetBool("first_run_complete", s.first_run_complete);
  return s;
}

JsonValue SettingsStore::ToJson(const Settings& s) {
  JsonValue general = JsonValue::Object();
  general.Set("startup_mode", JsonValue::String(s.startup_mode));
  general.Set("homepage", JsonValue::String(s.homepage));
  general.Set("search_url", JsonValue::String(s.search_url));
  general.Set("search_name", JsonValue::String(s.search_name));
  general.Set("theme", JsonValue::String(s.theme));
  general.Set("show_bookmarks_bar", JsonValue::Bool(s.show_bookmarks_bar));
  general.Set("chrome_auto_hide", JsonValue::Bool(s.chrome_auto_hide));
  general.Set("onshape_focus_mode", JsonValue::Bool(s.onshape_focus_mode));

  JsonValue performance = JsonValue::Object();
  performance.Set("gpu_prefer_high_performance",
                  JsonValue::Bool(s.gpu_prefer_high_performance));
  performance.Set("force_hardware_acceleration",
                  JsonValue::Bool(s.force_hardware_acceleration));
  performance.Set("uncapped_fps_default",
                  JsonValue::Bool(s.uncapped_fps_default));
  performance.Set("js_heap_mb", JsonValue::Int(s.js_heap_mb));
  performance.Set("gpu_large_memory_budget",
                  JsonValue::Bool(s.gpu_large_memory_budget));
  performance.Set("discard_enabled", JsonValue::Bool(s.discard_enabled));
  performance.Set("discard_available_fraction",
                  JsonValue::Number(s.discard_available_fraction));
  performance.Set("discard_browser_fraction",
                  JsonValue::Number(s.discard_browser_fraction));
  performance.Set("discard_idle_minutes", JsonValue::Int(s.discard_idle_minutes));
  performance.Set("discard_tab_count", JsonValue::Int(s.discard_tab_count));
  performance.Set("cache_path", JsonValue::String(s.cache_path));

  JsonValue capture = JsonValue::Object();
  capture.Set("folder", JsonValue::String(s.capture_folder));

  JsonValue privacy = JsonValue::Object();
  privacy.Set("blocklist_enabled", JsonValue::Bool(s.blocklist_enabled));
  JsonValue sites = JsonValue::Array();
  for (const std::string& site : s.allowed_sites)
    sites.Append(JsonValue::String(site));
  privacy.Set("allowed_sites", std::move(sites));

  JsonValue overlay = JsonValue::Object();
  overlay.Set("enabled", JsonValue::Bool(s.overlay_enabled));
  overlay.Set("position", JsonValue::String(s.overlay_position));
  overlay.Set("opacity", JsonValue::Int(s.overlay_opacity));
  overlay.Set("show_fps", JsonValue::Bool(s.overlay_show_fps));
  overlay.Set("show_gpu", JsonValue::Bool(s.overlay_show_gpu));
  overlay.Set("show_memory", JsonValue::Bool(s.overlay_show_memory));
  overlay.Set("show_cpu", JsonValue::Bool(s.overlay_show_cpu));
  overlay.Set("show_tabs", JsonValue::Bool(s.overlay_show_tabs));

  JsonValue window = JsonValue::Object();
  window.Set("width", JsonValue::Int(s.window_width));
  window.Set("height", JsonValue::Int(s.window_height));
  window.Set("maximized", JsonValue::Bool(s.window_maximized));

  JsonValue root = JsonValue::Object();
  root.Set("general", std::move(general));
  root.Set("performance", std::move(performance));
  root.Set("privacy", std::move(privacy));
  root.Set("capture", std::move(capture));
  root.Set("overlay", std::move(overlay));
  root.Set("window", std::move(window));
  root.Set("first_run_complete", JsonValue::Bool(s.first_run_complete));
  return root;
}

void SettingsStore::Load(const std::wstring& path) {
  path_ = path;
  had_error_ = false;
  error_.clear();

  std::string contents;
  if (!ReadFileToString(path, &contents) || contents.empty()) {
    // No file yet is the normal first-run case, not an error.
    settings_ = Settings();
    return;
  }

  std::string parse_error;
  const JsonValue json = JsonValue::Parse(contents, &parse_error);
  if (!json.is_object()) {
    had_error_ = true;
    error_ = parse_error.empty() ? "settings.json is not a JSON object"
                                 : parse_error;
    settings_ = Settings();
    return;
  }
  settings_ = FromJson(json);
}

bool SettingsStore::Save() const {
  if (path_.empty())
    return false;
  return WriteFileAtomically(path_, ToJson(settings_).Serialize(true) + "\n");
}

}  // namespace cad
