// CAD Browser - one browser window.
//
// Owns the native chrome (tab strip, toolbar, caption buttons), the content
// area that browser views live in, the stats overlay, and the tab list.
//
// Layout:
//   window
//     root            vertical box
//       top row       horizontal box: tab strip (flex) + caption buttons
//       toolbar       back / forward / reload / omnibox / menu
//       content       fill layout, one visible browser view at a time
//     overlay         floating, added through CefWindow::AddOverlayView
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "browser/session.h"
#include "browser/tab_manager.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "sys/stats.h"
#include "ui/hamburger_menu.h"
#include "ui/overlay_view.h"
#include "ui/tab_strip.h"
#include "ui/capture_status.h"
#include "ui/theme.h"
#include "ui/toolbar.h"

namespace cad {

class Tab;
// Watches the content panel's layout so the stats overlay can follow it.
// Defined in main_window.cc.
class ContentPanelDelegate;

class MainWindow : public CefWindowDelegate,
                   public CefButtonDelegate,
                   public TabStrip::Delegate,
                   public Toolbar::Delegate,
                   public HamburgerMenu::Delegate,
                   public OverlayView::Delegate {
 public:
  // Creates and shows a window. `restore` may be null for a fresh window.
  static void CreateNewWindow(const SessionWindow* restore);

  MainWindow();
  ~MainWindow() override;

  TabManager* tabs() { return tab_manager_.get(); }
  CefRefPtr<CefWindow> window() const { return window_; }

  // ---- called by TabManager ----
  void OnTabAdded(Tab* tab);
  void OnTabRemoved(Tab* tab);
  void OnTabListChanged();
  void OnActiveTabChanged(Tab* tab);
  void OnTabStateChanged(Tab* tab);
  void OnTabFaviconChanged(Tab* tab, const std::vector<std::string>& urls);
  void OnFullscreenChanged(bool fullscreen);
  void OnLastTabClosed();
  void OnUiMessage(Tab* tab,
                   const std::string& name,
                   const std::string& payload);

  // ---- called by AppState ----
  void OnStatsSnapshot(const sys::StatsSnapshot& snapshot);
  void OnSettingsChanged();
  void OnGpuStatusChanged();
  void SetOverlayVisible(bool visible);
  void OnAutoCaptureChanged(bool enabled);

  // ---- page capture (main_window_capture.cc) ----
  void OnTabPageSettled(int tab_id);
  void OnPageCaptured(Tab* tab,
                      bool automatic,
                      bool ok,
                      const std::string& error,
                      PageCapture page);

  // ---- CefWindowDelegate ----
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override;
  cef_show_state_t GetInitialShowState(CefRefPtr<CefWindow> window) override;
  bool IsFrameless(CefRefPtr<CefWindow> window) override;
  bool CanResize(CefRefPtr<CefWindow> window) override;
  bool CanMaximize(CefRefPtr<CefWindow> window) override;
  bool CanMinimize(CefRefPtr<CefWindow> window) override;
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;

  // ---- CefButtonDelegate: the caption buttons ----
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  // Hover and pressed fills for the caption buttons. Close goes red, the
  // way every other window on this desktop behaves.
  void OnButtonStateChanged(CefRefPtr<CefButton> button) override;

  // ---- TabStrip::Delegate ----
  void OnTabSelected(int tab_id) override;
  void OnTabCloseRequested(int tab_id) override;
  void OnNewTabRequested() override;

  // ---- Toolbar::Delegate ----
  void OnBackRequested() override;
  void OnForwardRequested() override;
  void OnReloadOrStopRequested() override;
  void OnNavigateRequested(const std::string& input) override;
  void OnMenuRequested(
      CefRefPtr<CefMenuButton> button,
      const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> pressed_lock) override;

  // ---- HamburgerMenu::Delegate ----
  void OnMenuCommand(int command_id) override;
  void OnMenuOpenChanged(bool open) override;

  // ---- Toolbar::Delegate, focus ----
  void OnOmniboxFocusChanged(bool focused) override;

  // ---- OverlayView::Delegate ----
  void OnOverlayEdgeChanged(OverlayEdge edge) override;
  void OnOverlayDragFinished() override;

  // Public only because the subclassed window procedure in main_window.cc is
  // a free function and cannot reach a private member. True when the click
  // was over a tab and that tab was closed.
  bool HandleMiddleClick(int screen_x, int screen_y);

 private:
  void BuildViews();
  void BuildChromeOverlay();
  // Lays out the page and the stats bar as siblings, with the bar on
  // whichever edge it is docked to. Rebuilt whenever the bar is shown,
  // hidden, or moved.
  void RebuildRootLayout();
  void RegisterAccelerators();
  void ApplyTheme();

  // ---- auto-hiding chrome ----
  // The tab strip and toolbar float above the page and appear when the
  // pointer comes near the top edge, so the window is effectively full screen
  // the rest of the time.
  void ScheduleHoverPoll();
  // Middle-clicking a tab closes it, which every browser does and CEF Views
  // cannot express: a Views button reports only its own activation, and CEF
  // exposes no mouse events on a view. So the window's WndProc is subclassed
  // and WM_MBUTTONUP is hit-tested against the tab strip by screen point.
  void InstallMiddleClickHook();
  void RemoveMiddleClickHook();
  void UpdateChromeFromPointer();
  void SetChromeRevealed(bool revealed);
  void SetChromeCompact(bool compact);
  void LayoutChromeOverlay();
  // True while something other than the pointer requires the chrome: the
  // address bar has focus, or the menu is open.
  bool ChromeIsPinned() const;
  void UpdateToolbarFromActiveTab();
  void UpdateDraggableRegions();
  void ScheduleSessionSave();
  void SaveSessionNow();
  void ScheduleDiscardSweep();
  void SetZoomPercent(int percent);
  void AdjustZoom(int delta_percent);
  // Serves the data behind cad://sysinfo, cad://settings and the GPU probe.
  void HandleUiMessage(Tab* tab,
                       const std::string& name,
                       const std::string& payload);
  void PushSystemInfo(Tab* tab);
  void PushSettings(Tab* tab);
  void RestoreFrom(const SessionWindow& restore);
  void CaptureActivePage();
  void LayoutCaptureStatus();
  void ShowCaptureMessage(const std::string& message, bool error);

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> root_panel_;
  // Holds the tab strip and the toolbar. Added as an overlay rather than a
  // child of root_panel_, so revealing it never resizes the page.
  CefRefPtr<CefPanel> chrome_panel_;
  CefRefPtr<CefOverlayController> chrome_controller_;
  CefRefPtr<CefPanel> top_row_;
  CefRefPtr<CefPanel> caption_panel_;
  CefRefPtr<CefPanel> content_panel_;
  CefRefPtr<ContentPanelDelegate> content_delegate_;
  CefRefPtr<CefLabelButton> minimize_button_;
  CefRefPtr<CefLabelButton> maximize_button_;
  CefRefPtr<CefLabelButton> close_button_;

  CefRefPtr<TabStrip> tab_strip_;
  CefRefPtr<Toolbar> toolbar_;
  CefRefPtr<HamburgerMenu> menu_;
  std::unique_ptr<OverlayView> overlay_;
  std::unique_ptr<TabManager> tab_manager_;
  CaptureStatus capture_status_;
  // Per tab: the debounce generation, and the URL and load count last
  // auto-captured, so one page is saved once however many events it fires.
  struct AutoCaptureState {
    uint64_t generation = 0;
    std::string last_url;
    uint64_t last_load_count = 0;
  };
  std::map<int, AutoCaptureState> auto_capture_;
  uint64_t capture_message_generation_ = 0;

  Theme theme_;
  std::unique_ptr<SessionWindow> pending_restore_;
  int zoom_percent_ = 100;
  bool is_fullscreen_ = false;
  bool closing_ = false;
  bool middle_click_hooked_ = false;
  // Set when --cad-window-position placed the window explicitly.
  bool placed_explicitly_ = false;
  bool chrome_revealed_ = false;
  bool chrome_compact_ = true;
  bool menu_open_ = false;
  bool omnibox_focused_ = false;
  bool hover_poll_running_ = false;
  // Monotonic milliseconds until which the chrome stays up regardless of the
  // pointer. Set at window creation so a new window shows its toolbar once
  // instead of opening as a bare rectangle with no visible way to drive it.
  uint64_t reveal_until_ms_ = 0;
  uint64_t session_save_generation_ = 0;

  IMPLEMENT_REFCOUNTING(MainWindow);
  DISALLOW_COPY_AND_ASSIGN(MainWindow);
};

}  // namespace cad
