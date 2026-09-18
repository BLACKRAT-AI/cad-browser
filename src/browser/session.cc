#include "browser/session.h"

#include "browser/json.h"
#include "browser/settings_store.h"

namespace cad {
namespace {

constexpr int kSessionFormatVersion = 1;

}  // namespace

Session LoadSession(const std::wstring& path) {
  Session session;

  std::string contents;
  if (!ReadFileToString(path, &contents) || contents.empty()) {
    return session;
  }

  const JsonValue root = JsonValue::Parse(contents);
  if (!root.is_object()) {
    return session;
  }
  // An unknown future format is ignored rather than guessed at; the worst case
  // is starting with a new tab instead of corrupting someone's session.
  if (root.GetInt("version", 0) != kSessionFormatVersion) {
    return session;
  }

  const JsonValue* windows = root.GetChildArray("windows");
  if (!windows) {
    return session;
  }

  for (const JsonValue& window_json : windows->items()) {
    if (!window_json.is_object()) {
      continue;
    }
    SessionWindow window;
    window.active_index = static_cast<int>(window_json.GetInt("active_index", 0));
    window.width = static_cast<int>(window_json.GetInt("width", 0));
    window.height = static_cast<int>(window_json.GetInt("height", 0));
    window.maximized = window_json.GetBool("maximized", false);

    if (const JsonValue* tabs = window_json.GetChildArray("tabs")) {
      for (const JsonValue& tab_json : tabs->items()) {
        if (!tab_json.is_object()) {
          continue;
        }
        SessionTab tab;
        tab.url = tab_json.GetString("url");
        tab.title = tab_json.GetString("title");
        tab.pinned = tab_json.GetBool("pinned", false);
        if (!tab.url.empty()) {
          window.tabs.push_back(std::move(tab));
        }
      }
    }

    if (!window.tabs.empty()) {
      if (window.active_index < 0 ||
          window.active_index >= static_cast<int>(window.tabs.size())) {
        window.active_index = 0;
      }
      session.windows.push_back(std::move(window));
    }
  }

  return session;
}

bool SaveSession(const std::wstring& path, const Session& session) {
  JsonValue windows = JsonValue::Array();
  for (const SessionWindow& window : session.windows) {
    JsonValue tabs = JsonValue::Array();
    for (const SessionTab& tab : window.tabs) {
      JsonValue entry = JsonValue::Object();
      entry.Set("url", JsonValue::String(tab.url));
      entry.Set("title", JsonValue::String(tab.title));
      if (tab.pinned) {
        entry.Set("pinned", JsonValue::Bool(true));
      }
      tabs.Append(std::move(entry));
    }

    JsonValue entry = JsonValue::Object();
    entry.Set("active_index", JsonValue::Int(window.active_index));
    entry.Set("width", JsonValue::Int(window.width));
    entry.Set("height", JsonValue::Int(window.height));
    entry.Set("maximized", JsonValue::Bool(window.maximized));
    entry.Set("tabs", std::move(tabs));
    windows.Append(std::move(entry));
  }

  JsonValue root = JsonValue::Object();
  root.Set("version", JsonValue::Int(kSessionFormatVersion));
  root.Set("windows", std::move(windows));

  return WriteFileAtomically(path, root.Serialize(true) + "\n");
}

}  // namespace cad
