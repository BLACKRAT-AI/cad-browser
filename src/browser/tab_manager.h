// CAD Browser - the tab list for one window.
//
// Owns every Tab, decides which one is active, and runs the discard sweep that
// keeps a large session affordable. PLAN.md section 6.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "browser/discard_policy.h"
#include "browser/tab.h"

namespace cad {

class MainWindow;

class TabManager {
 public:
  explicit TabManager(MainWindow* window);
  ~TabManager();
  TabManager(const TabManager&) = delete;
  TabManager& operator=(const TabManager&) = delete;

  // ---- creation and destruction ----
  // Creates a live tab. Returns its id.
  int CreateTab(const std::string& url, bool foreground);
  // Creates a tab that starts asleep. Used by session restore, which is what
  // makes reopening a hundred tabs cost one renderer instead of a hundred.
  int CreateSleepingTab(const std::string& url,
                        const std::string& title,
                        bool pinned);
  void CloseTab(int tab_id);
  void CloseOtherTabs(int tab_id);
  void CloseTabsToTheRight(int tab_id);
  // Reopens the most recently closed tab.
  bool ReopenLastClosedTab();

  // ---- navigation between tabs ----
  void ActivateTab(int tab_id);
  void ActivateTabAtIndex(int index);
  void ActivateNextTab();
  void ActivatePreviousTab();
  void MoveTab(int tab_id, int new_index);

  // ---- state ----
  int active_tab_id() const { return active_tab_id_; }
  Tab* active_tab() const;
  Tab* FindTab(int tab_id) const;
  int IndexOf(int tab_id) const;
  size_t count() const { return tabs_.size(); }
  int sleeping_count() const;
  const std::vector<std::unique_ptr<Tab>>& tabs() const { return tabs_; }

  // ---- discard ----
  // Puts a tab to sleep, keeping its entry in the strip.
  void DiscardTab(int tab_id);
  // Wakes a sleeping tab and attaches it to the window.
  void RestoreTab(int tab_id);
  // Runs the policy over the whole list. Called on a timer by MainWindow.
  void RunDiscardSweep();

  // ---- callbacks from Tab ----
  void OnTabBrowserCreated(int tab_id);
  void OnTabChanged(int tab_id);
  void OnTabFaviconChanged(int tab_id, const std::vector<std::string>& urls);
  void OnTabFullscreenChanged(int tab_id, bool fullscreen);
  void OnTabUiMessage(int tab_id,
                      const std::string& name,
                      const std::string& payload);

  // Pushes the current probe state to every live renderer.
  void SetFrameProbeEnabled(bool enabled);
  // Onshape focus mode, applied to pages that are already loaded.
  void SetSiteFocusEnabled(bool enabled);

 private:
  struct ClosedTab {
    std::string url;
    std::string title;
    int index = 0;
    bool pinned = false;
  };

  int NextId() { return next_tab_id_++; }
  void RemoveTabAt(size_t index);
  DiscardThresholds CurrentThresholds() const;

  MainWindow* const window_;
  std::vector<std::unique_ptr<Tab>> tabs_;
  int active_tab_id_ = 0;
  int next_tab_id_ = 1;
  std::vector<ClosedTab> closed_tabs_;
};

}  // namespace cad
