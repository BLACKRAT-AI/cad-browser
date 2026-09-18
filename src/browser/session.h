// CAD Browser - saving and restoring the open tabs.
//
// PLAN.md section 6.4. Written atomically on every change, debounced, and
// restored as sleeping tabs so a large session costs one renderer at startup
// rather than one per tab.
#pragma once

#include <string>
#include <vector>

namespace cad {

struct SessionTab {
  std::string url;
  std::string title;
  bool pinned = false;
};

struct SessionWindow {
  std::vector<SessionTab> tabs;
  int active_index = 0;
  int width = 0;
  int height = 0;
  bool maximized = false;
};

struct Session {
  std::vector<SessionWindow> windows;
  bool empty() const { return windows.empty(); }
};

// Reads the session file. A missing or unreadable file yields an empty session
// rather than an error: there is nothing to recover and nothing to report.
Session LoadSession(const std::wstring& path);

bool SaveSession(const std::wstring& path, const Session& session);

}  // namespace cad
