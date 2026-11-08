#include "sys/win_util.h"

#include "ui/main_window.h"
#include "include/cef_color_ids.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>

#include "app/app_state.h"
#include "browser/json.h"
#include "browser/tab.h"
#include "browser/url_util.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_closure_task.h"
#include "sys/gpu_preference.h"

namespace cad {
namespace {

// Accelerator ids. Kept clear of MenuCommand, which starts at 100, so a menu
// item and a shortcut can share one handler without colliding.
enum AcceleratorId {
  kAccelNewTab = 200,
  kAccelCloseTab,
  kAccelReopenTab,
  kAccelNextTab,
  kAccelPrevTab,
  kAccelFocusOmnibox,
  kAccelReload,
  kAccelReloadHard,
  kAccelBack,
  kAccelForward,
  kAccelToggleOverlay,
  kAccelDevTools,
  kAccelFind,
  kAccelNewWindow,
  kAccelZoomIn,
  kAccelZoomOut,
  kAccelZoomReset,
  // Pins the chrome open permanently and back again. An escape hatch: if the
  // hover reveal ever misbehaves there must still be a way to reach the tabs,
  // the address bar and the window's own close button.
  kAccelToggleChrome,
  kAccelToggleFullscreen,
  kAccelCapturePage,
  kAccelTab1,  // through kAccelTab1 + 8 for Ctrl+1..Ctrl+9
};

constexpr int kVkTab = 0x09;
constexpr int kVkEscape = 0x1B;
constexpr int kVkLeft = 0x25;
constexpr int kVkRight = 0x27;
constexpr int kVkF5 = 0x74;
constexpr int kVkF11 = 0x7A;
constexpr int kVkF12 = 0x7B;
constexpr int kVk0 = 0x30;
constexpr int kVk1 = 0x31;
constexpr int kVkT = 0x54;
constexpr int kVkW = 0x57;
constexpr int kVkL = 0x4C;
constexpr int kVkR = 0x52;
constexpr int kVkS = 0x53;
constexpr int kVkF = 0x46;
constexpr int kVkN = 0x4E;
constexpr int kVkPlus = 0xBB;   // VK_OEM_PLUS
constexpr int kVkMinus = 0xBD;  // VK_OEM_MINUS

constexpr int kSessionSaveDebounceMs = 500;
constexpr int kDiscardSweepIntervalMs = 10000;

// How long a new window keeps its chrome up before it starts hiding. Long
// enough to notice the toolbar exists and where it lives.
constexpr uint64_t kInitialRevealMs = 2600;

uint64_t NowMs() {
  using clock = std::chrono::steady_clock;
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

constexpr char kNewTabUrl[] = "cad://newtab";

}  // namespace

// Watches the content panel so the stats overlay can follow it.
//
// Positioning the overlay once after Show() is not enough: Views lays out
// lazily, so the panel still has empty bounds at that point and the overlay
// ends up parked at the window origin. Reacting to the panel's own layout is
// the only way to be sure the bounds are real.
class ContentPanelDelegate : public CefPanelDelegate {
 public:
  using LayoutCallback = std::function<void(const CefRect&)>;

  explicit ContentPanelDelegate(LayoutCallback callback)
      : callback_(std::move(callback)) {}

  // Called before the window that owns the callback's captured pointer goes
  // away.
  void Detach() { callback_ = nullptr; }

  void OnLayoutChanged(CefRefPtr<CefView> view,
                       const CefRect& new_bounds) override {
    if (callback_) {
      callback_(new_bounds);
    }
  }

 private:
  LayoutCallback callback_;

  IMPLEMENT_REFCOUNTING(ContentPanelDelegate);
  DISALLOW_COPY_AND_ASSIGN(ContentPanelDelegate);
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainWindow::MainWindow() {
  theme_ = BuildTheme("system");
}

MainWindow::~MainWindow() = default;

void MainWindow::CreateNewWindow(const SessionWindow* restore) {
  CefRefPtr<MainWindow> window = new MainWindow();
  if (restore) {
    window->pending_restore_ = std::make_unique<SessionWindow>(*restore);
  }
  // CreateTopLevelWindow holds the delegate for the life of the window and
  // calls back into OnWindowCreated before it returns.
  CefWindow::CreateTopLevelWindow(window);
}

// ---------------------------------------------------------------------------
// CefWindowDelegate
// ---------------------------------------------------------------------------

cef_runtime_style_t MainWindow::GetWindowRuntimeStyle() {
  // Alloy: no Chrome UI at all. CAD Browser draws every pixel of its chrome.
  return CEF_RUNTIME_STYLE_ALLOY;
}

bool MainWindow::IsFrameless(CefRefPtr<CefWindow> window) {
  // The tab strip doubles as the title bar; see UpdateDraggableRegions for the
  // area that still drags the window.
  return true;
}

bool MainWindow::CanResize(CefRefPtr<CefWindow> window) {
  return true;
}

bool MainWindow::CanMaximize(CefRefPtr<CefWindow> window) {
  return true;
}

bool MainWindow::CanMinimize(CefRefPtr<CefWindow> window) {
  return true;
}

CefRect MainWindow::GetInitialBounds(CefRefPtr<CefWindow> window) {
  int width = 1400;
  int height = 900;
  if (AppState* state = AppState::Get()) {
    width = state->settings().window_width;
    height = state->settings().window_height;
  }
  if (pending_restore_ && pending_restore_->width > 0 &&
      pending_restore_->height > 0) {
    width = pending_restore_->width;
    height = pending_restore_->height;
  }

  // Developer switch: --cad-window-position=X,Y places the window at an exact
  // screen position, including off-screen. It exists so the UI can be captured
  // for review without the window appearing over whatever the person at the
  // machine is actually doing.
  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  if (command_line && command_line->HasSwitch("cad-window-position")) {
    const std::string value =
        command_line->GetSwitchValue("cad-window-position").ToString();
    const size_t comma = value.find(',');
    if (comma != std::string::npos) {
      const long x = std::strtol(value.substr(0, comma).c_str(), nullptr, 10);
      const long y = std::strtol(value.substr(comma + 1).c_str(), nullptr, 10);
      placed_explicitly_ = true;
      return CefRect(static_cast<int>(x), static_cast<int>(y), width, height);
    }
  }

  // An empty origin asks CEF to centre the window on the default display.
  return CefRect(0, 0, width, height);
}

cef_show_state_t MainWindow::GetInitialShowState(CefRefPtr<CefWindow> window) {
  // Developer switch, paired with --cad-window-position: start minimised so
  // the window can be moved off-screen and captured without ever being drawn
  // over someone's desktop. Windows clamps *initial* bounds to the visible
  // work area, so starting off-screen is not otherwise possible.
  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  if (command_line && command_line->HasSwitch("cad-start-minimized")) {
    return CEF_SHOW_STATE_MINIMIZED;
  }

  const bool maximized =
      pending_restore_ ? pending_restore_->maximized
                       : (AppState::Get() &&
                          AppState::Get()->settings().window_maximized);
  return maximized ? CEF_SHOW_STATE_MAXIMIZED : CEF_SHOW_STATE_NORMAL;
}

void MainWindow::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  tab_manager_ = std::make_unique<TabManager>(this);

  if (AppState* state = AppState::Get()) {
    theme_ = BuildTheme(state->settings().theme);
    state->AddWindow(this);
  }

  BuildViews();
  RegisterAccelerators();

  overlay_ = std::make_unique<OverlayView>(this);
  if (AppState* state = AppState::Get()) {
    overlay_->Create(state->settings());
    overlay_->SetVisible(state->settings().overlay_enabled);
  }
  RebuildRootLayout();

  // Centring would undo an explicit --cad-window-position.
  if (!placed_explicitly_) {
    window_->CenterWindow(CefSize(window_->GetBounds().width,
                                  window_->GetBounds().height));
  }
  window_->SetTitle("CAD Browser");

  if (pending_restore_) {
    RestoreFrom(*pending_restore_);
    pending_restore_.reset();
  } else {
    tab_manager_->CreateTab(kNewTabUrl, true);
  }

  // The chrome overlay goes on last so it sits above the stats bar.
  BuildChromeOverlay();
  capture_status_.Attach(window_, theme_);
  if (AppState* app = AppState::Get()) {
    capture_status_.SetAutoCapture(app->auto_capture());
  }
  reveal_until_ms_ = NowMs() + kInitialRevealMs;
  SetChromeRevealed(true);
  SetChromeCompact(false);

  window_->Show();
  // The content panel only has real bounds once the window has laid out, and
  // both overlays are positioned from those bounds.
  if (root_panel_) {
    root_panel_->Layout();
  }
  LayoutChromeOverlay();
  ScheduleDiscardSweep();
  InstallMiddleClickHook();
  ScheduleHoverPoll();
}

void MainWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  closing_ = true;
  RemoveMiddleClickHook();
  capture_status_.Detach();
  if (overlay_) {
    overlay_->Destroy();
    overlay_.reset();
  }
  if (tab_strip_) {
    tab_strip_->DetachDelegate();
  }
  if (toolbar_) {
    toolbar_->DetachDelegate();
  }
  if (menu_) {
    menu_->DetachDelegate();
  }
  if (content_delegate_) {
    // Must happen before this object can be destroyed: the delegate holds a
    // callback that captures `this`.
    content_delegate_->Detach();
  }
  tab_manager_.reset();

  if (AppState* state = AppState::Get()) {
    state->RemoveWindow(this);
    if (state->windows().empty()) {
      CefQuitMessageLoop();
    }
  }
  window_ = nullptr;
}

bool MainWindow::CanClose(CefRefPtr<CefWindow> window) {
  SaveSessionNow();
  return true;
}

void MainWindow::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                       const CefRect& new_bounds) {
  if (closing_ || !window) {
    return;
  }
  if (AppState* state = AppState::Get()) {
    if (!window->IsMaximized() && !window->IsMinimized()) {
      state->mutable_settings().window_width = new_bounds.width;
      state->mutable_settings().window_height = new_bounds.height;
    }
    state->mutable_settings().window_maximized = window->IsMaximized();
  }
  // Tab widths are a function of the strip width, so the strip is rebuilt.
  OnTabListChanged();
  UpdateDraggableRegions();
  LayoutChromeOverlay();
}

void MainWindow::OnOverlayEdgeChanged(OverlayEdge edge) {
  if (AppState* state = AppState::Get()) {
    state->mutable_settings().overlay_position = OverlayEdgeToString(edge);
    state->SaveSettings();
  }
  // The bar is part of the layout, so moving it means relaying out the page.
  RebuildRootLayout();
}

void MainWindow::OnOverlayDragFinished() {
  // The bar has to accept a click to be draggable, which means it can take
  // focus. Give it straight back so typing continues to go to the page.
  if (!tab_manager_) {
    return;
  }
  if (Tab* tab = tab_manager_->active_tab()) {
    if (CefRefPtr<CefBrowserView> view = tab->browser_view()) {
      view->RequestFocus();
    }
  }
}

// ---------------------------------------------------------------------------
// View construction
// ---------------------------------------------------------------------------

void MainWindow::BuildViews() {
  // The root holds only the page. The chrome is an overlay on top of it, so
  // the page is always full height and revealing the chrome costs no reflow.
  root_panel_ = CefPanel::CreatePanel(nullptr);
  root_panel_->SetToFillLayout();

  // ---- top row: tab strip plus the caption buttons ----
  top_row_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings top_settings;
  top_settings.horizontal = 1;
  top_settings.between_child_spacing = 0;
  top_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  top_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> top_layout = top_row_->SetToBoxLayout(top_settings);

  tab_strip_ = new TabStrip(this);
  top_row_->AddChildView(tab_strip_->view());
  top_layout->SetFlexForView(tab_strip_->view(), 1);

  caption_panel_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings caption_settings;
  caption_settings.horizontal = 1;
  caption_settings.between_child_spacing = 0;
  caption_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_END;
  caption_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  caption_panel_->SetToBoxLayout(caption_settings);

  auto make_caption_button = [this](int id, const char* label,
                                    const char* tooltip) {
    CefRefPtr<CefLabelButton> button =
        CefLabelButton::CreateLabelButton(this, label);
    button->SetID(id);
    button->SetInkDropEnabled(false);
    button->SetFocusable(false);
    // The same icon font Windows draws its own caption buttons with.
    button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
    button->SetFontList(UiFontList(14));
    button->SetMinimumSize(CefSize(46, metrics::kTabStripHeight));
    button->SetMaximumSize(CefSize(46, metrics::kTabStripHeight));
    button->SetTooltipText(tooltip);
    button->SetAccessibleName(tooltip);
    return button;
  };

  // Ids above the tab strip's own ranges so a caption click is never mistaken
  // for a tab click.
  minimize_button_ = make_caption_button(9000001, icons::kMinimize, "Minimize");
  maximize_button_ = make_caption_button(9000002, icons::kMaximize, "Maximize");
  close_button_ = make_caption_button(9000003, icons::kClose, "Close");
  caption_panel_->AddChildView(minimize_button_);
  caption_panel_->AddChildView(maximize_button_);
  caption_panel_->AddChildView(close_button_);
  top_row_->AddChildView(caption_panel_);

  // ---- toolbar ----
  toolbar_ = new Toolbar(this);

  // ---- the chrome, stacked, to become an overlay ----
  chrome_panel_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings chrome_settings;
  chrome_settings.horizontal = 0;
  chrome_settings.between_child_spacing = 0;
  chrome_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  chrome_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  chrome_panel_->SetToBoxLayout(chrome_settings);
  chrome_panel_->AddChildView(top_row_);
  chrome_panel_->AddChildView(toolbar_->view());

  // ---- content ----
  // A raw `this` in the callback is safe because the delegate is detached in
  // OnWindowDestroyed, before the window releases its last reference.
  content_delegate_ = new ContentPanelDelegate(
      [this](const CefRect& bounds) { LayoutChromeOverlay(); });
  content_panel_ = CefPanel::CreatePanel(content_delegate_);
  content_panel_->SetToFillLayout();

  menu_ = new HamburgerMenu(this);

  window_->AddChildView(root_panel_);
  ApplyTheme();
  UpdateDraggableRegions();
}

void MainWindow::RebuildRootLayout() {
  if (!root_panel_ || !content_panel_) {
    return;
  }

  CefRefPtr<CefBrowserView> bar = overlay_ ? overlay_->view() : nullptr;
  const bool show_bar = bar && overlay_->visible();
  const OverlayEdge edge = overlay_ ? overlay_->edge() : OverlayEdge::kBottom;
  const bool vertical = OverlayEdgeIsVertical(edge);

  root_panel_->RemoveAllChildViews();

  CefBoxLayoutSettings settings;
  settings.horizontal = vertical ? 1 : 0;
  settings.between_child_spacing = 0;
  settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> layout = root_panel_->SetToBoxLayout(settings);

  // The bar is a sibling of the page, so the page shrinks by exactly the
  // bar's thickness and nothing is ever hidden behind it. Docked left or
  // right it runs the full height, which the cross-axis stretch gives us.
  const bool bar_first = edge == OverlayEdge::kTop || edge == OverlayEdge::kLeft;
  if (show_bar && bar_first) {
    root_panel_->AddChildView(bar);
  }
  root_panel_->AddChildView(content_panel_);
  if (show_bar && !bar_first) {
    root_panel_->AddChildView(bar);
  }

  // Only the page takes the slack; the bar keeps its preferred thickness.
  layout->SetFlexForView(content_panel_, 1);
  root_panel_->Layout();
  LayoutChromeOverlay();
}

void MainWindow::BuildChromeOverlay() {
  if (!window_ || !chrome_panel_ || chrome_controller_) {
    return;
  }
  // can_activate is true because the address bar has to be typeable and the
  // buttons clickable. The chrome is only present when the pointer is at the
  // top of the window, so it cannot take a click meant for the page.
  chrome_controller_ = window_->AddOverlayView(
      chrome_panel_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/true);
  if (!chrome_controller_) {
    return;
  }
  LayoutChromeOverlay();
  chrome_controller_->SetVisible(false);
  chrome_revealed_ = false;
}

void MainWindow::LayoutChromeOverlay() {
  if (!chrome_controller_ || !window_) {
    return;
  }
  // Measured from the window's own client area, not from a child panel.
  // root_panel_->GetSize() reports zero until Views has laid out, and the
  // early return that used to guard against that left the overlay sized 0x0,
  // which is why the bar could be "revealed" and still not appear.
  int width = window_->GetClientAreaBoundsInScreen().width;
  if (width <= 0 && root_panel_) {
    width = root_panel_->GetSize().width;
  }
  if (width <= 0) {
    width = window_->GetBounds().width;
  }
  if (width <= 0) {
    return;
  }
  chrome_controller_->SetBounds(CefRect(0, 0, width, metrics::kChromeHeight));
  LayoutCaptureStatus();
}

// ---------------------------------------------------------------------------
// Auto-hiding chrome
// ---------------------------------------------------------------------------

bool MainWindow::ChromeIsPinned() const {
  // Anything the user is in the middle of keeps the chrome up. Hiding the
  // address bar while it is being typed into, or the menu while it is open,
  // would be actively hostile. The startup grace period is here too, so a new
  // window does not open as a bare rectangle with no visible controls.
  return omnibox_focused_ || menu_open_ || NowMs() < reveal_until_ms_;
}

void MainWindow::ScheduleHoverPoll() {
  if (closing_ || hover_poll_running_) {
    return;
  }
  hover_poll_running_ = true;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(
                         [](CefRefPtr<MainWindow> self) {
                           self->hover_poll_running_ = false;
                           if (self->closing_) {
                             return;
                           }
                           self->UpdateChromeFromPointer();
                           self->ScheduleHoverPoll();
                         },
                         CefRefPtr<MainWindow>(this)),
                     metrics::kChromeHoverPollMs);
}

void MainWindow::UpdateChromeFromPointer() {
  if (closing_ || !window_ || !chrome_controller_) {
    return;
  }

  // A page in fullscreen gets the window to itself; nothing reveals over it.
  if (is_fullscreen_) {
    SetChromeRevealed(false);
    return;
  }

  const bool auto_hide =
      AppState::Get() ? AppState::Get()->settings().chrome_auto_hide : true;
  if (!auto_hide) {
    SetChromeRevealed(true);
    SetChromeCompact(false);
    return;
  }

  if (ChromeIsPinned()) {
    SetChromeRevealed(true);
    SetChromeCompact(false);
    return;
  }

  POINT cursor{};
  if (!::GetCursorPos(&cursor)) {
    return;
  }

  // The *client area*, not the window rect. A maximised frameless window
  // extends past the work area by the resize border, so measuring from the
  // window rect put the reveal band partly off-screen and the chrome could
  // not be summoned at all. That is what made the window look stuck.
  const CefRect client = window_->GetClientAreaBoundsInScreen();
  if (client.width <= 0 || client.height <= 0) {
    return;
  }
  const int x = cursor.x - client.x;
  const int y = cursor.y - client.y;

  const bool inside_horizontally = x >= -4 && x < client.width + 4;

  // Onshape focus mode puts its own toggle flush against the top of the page,
  // centred. Reaching for it would otherwise summon this toolbar on top of it,
  // so the strip it occupies does not trigger the reveal. The toolbar is still
  // available from anywhere else along the top edge. Making the toggle move
  // out of the way instead was tried and was worse: a control that shifts
  // position is harder to use than one in a fixed awkward place.
  bool over_page_toggle = false;
  if (AppState* app = AppState::Get()) {
    if (app->settings().onshape_focus_mode && tab_manager_) {
      if (Tab* active = tab_manager_->active_tab()) {
        if (RegistrableDomain(HostFromUrl(active->url())) == "onshape.com") {
          const int half = metrics::kPageToggleGuardWidth / 2;
          const int centre = client.width / 2;
          over_page_toggle = x >= centre - half && x <= centre + half;
        }
      }
    }
  }
  // Anything at or above the top of the client area counts, so throwing the
  // pointer at the top of the screen always works.
  const bool in_reveal_zone = inside_horizontally && !over_page_toggle &&
                              y < metrics::kChromeRevealZone;
  // Once revealed it stays until the pointer is clear of it by a margin,
  // which stops it flickering at the boundary.
  const bool near_chrome =
      inside_horizontally &&
      y < metrics::kChromeHeight + metrics::kChromeHideMargin;

  // One stage: if the pointer is at the top, the whole bar is there, buttons
  // and all. SetChromeRevealed handles the compact flag.
  SetChromeRevealed(in_reveal_zone || (chrome_revealed_ && near_chrome));
}

void MainWindow::SetChromeRevealed(bool revealed) {
  if (chrome_revealed_ == revealed) {
    return;
  }
  chrome_revealed_ = revealed;
  if (revealed) {
    // Re-themed on every reveal, deliberately. A background colour set on
    // these panels and buttons while the overlay widget does not yet exist is
    // silently dropped, and the page then shows straight through the toolbar
    // and the caption buttons. The tab strip looked correct only by accident:
    // it is rebuilt and re-themed whenever a tab title changes, so it picked
    // its colour up later on its own. Proved by painting each view a
    // different colour and screenshotting the window.
    ApplyTheme();
  }
  if (chrome_controller_) {
    // Re-measure every time. The window may have been resized, maximised or
    // moved to another display while the bar was hidden.
    if (revealed) {
      LayoutChromeOverlay();
    }
    chrome_controller_->SetVisible(revealed);
  }
  // Revealing shows the whole bar, buttons included. The earlier two-stage
  // version, where brushing the top gave a bar without buttons and you had to
  // move further in to get them, was fiddly and left people clicking at
  // controls that were not there yet.
  SetChromeCompact(!revealed);
  UpdateDraggableRegions();
}

void MainWindow::SetChromeCompact(bool compact) {
  if (chrome_compact_ == compact) {
    return;
  }
  chrome_compact_ = compact;
  if (toolbar_) {
    toolbar_->SetCompact(compact);
  }
  if (caption_panel_) {
    caption_panel_->SetVisible(!compact);
  }
  if (tab_strip_) {
    tab_strip_->SetCompact(compact);
  }
  // The close and new-tab buttons are created during a rebuild, so the strip
  // has to be rebuilt rather than just toggled.
  OnTabListChanged();
  if (chrome_panel_) {
    chrome_panel_->Layout();
  }
}

void MainWindow::OnMenuOpenChanged(bool open) {
  menu_open_ = open;
  UpdateChromeFromPointer();
}

void MainWindow::OnOmniboxFocusChanged(bool focused) {
  omnibox_focused_ = focused;
  UpdateChromeFromPointer();
}

void MainWindow::ApplyTheme() {
  if (!window_) {
    return;
  }
  if (AppState* state = AppState::Get()) {
    theme_ = BuildTheme(state->settings().theme);
  }

  // Window-level theme colours. CEF API 15000 removed the per-textfield
  // colour setters, and these are their replacement: they belong to the
  // window and are handed down to every view in its hierarchy.
  window_->SetThemeColor(CEF_ColorTextfieldBackground, theme_.omnibox);
  window_->SetThemeColor(CEF_ColorTextfieldForeground, theme_.text);
  window_->SetThemeColor(CEF_ColorTextfieldForegroundPlaceholder,
                         theme_.text_muted);
  window_->SetThemeColor(CEF_ColorTextfieldSelectionBackground, theme_.accent);
  window_->SetThemeColor(CEF_ColorTextfieldSelectionForeground, theme_.text);
  window_->SetThemeColor(CEF_ColorPrimaryBackground, theme_.background);
  // Deliberately not calling CefWindow::ThemeChanged here. It resets every
  // view's explicit background colour, and the ones set immediately below
  // were being wiped by it: the chrome bar painted but everything inside it
  // vanished. The theme colours above are picked up without it, and this
  // whole function runs again on every reveal anyway.

  window_->SetBackgroundColor(theme_.background);
  if (root_panel_) {
    root_panel_->SetBackgroundColor(theme_.background);
  }
  // The chrome floats over page content, so it has to be opaque. Without an
  // explicit background the panel is transparent and the page shows straight
  // through the tab strip and address bar, which is unreadable.
  if (chrome_panel_) {
    chrome_panel_->SetBackgroundColor(theme_.chrome_surface);
  }
  if (top_row_) {
    top_row_->SetBackgroundColor(theme_.chrome_surface);
  }
  if (caption_panel_) {
    caption_panel_->SetBackgroundColor(theme_.chrome_surface);
  }
  if (content_panel_) {
    content_panel_->SetBackgroundColor(theme_.background);
  }
  for (CefRefPtr<CefLabelButton> button :
       {minimize_button_, maximize_button_, close_button_}) {
    if (button) {
      button->SetBackgroundColor(theme_.chrome_surface);
      button->SetEnabledTextColors(theme_.text_muted);
    }
  }
  if (toolbar_) {
    toolbar_->ApplyTheme(theme_);
  }
  OnTabListChanged();
}

void MainWindow::OnButtonStateChanged(CefRefPtr<CefButton> button) {
  if (!button) {
    return;
  }
  CefRefPtr<CefLabelButton> label = button->AsLabelButton();
  const bool is_close = close_button_ && button->GetID() == close_button_->GetID();
  switch (button->GetState()) {
    case CEF_BUTTON_STATE_HOVERED:
      button->SetBackgroundColor(is_close ? theme_.danger : theme_.chrome_hover);
      if (label) label->SetEnabledTextColors(is_close ? 0xFFFFFFFFu : theme_.text);
      break;
    case CEF_BUTTON_STATE_PRESSED:
      button->SetBackgroundColor(is_close ? theme_.danger
                                          : theme_.chrome_pressed);
      if (label) label->SetEnabledTextColors(is_close ? 0xFFFFFFFFu : theme_.text);
      break;
    default:
      button->SetBackgroundColor(theme_.chrome_surface);
      if (label) label->SetEnabledTextColors(theme_.text_muted);
      break;
  }
}

void MainWindow::UpdateDraggableRegions() {
  if (!window_ || !top_row_ || !tab_strip_) {
    return;
  }
  // Nothing is draggable while the chrome is hidden: there is no title bar on
  // screen to grab. Brushing the top edge brings it back, and the empty part
  // of the tab strip moves the window as a title bar would.
  if (!chrome_revealed_) {
    window_->SetDraggableRegions(std::vector<CefDraggableRegion>());
    return;
  }
  // The empty stretch of the tab strip drags the window. Buttons inside it
  // must be excluded or they would drag instead of clicking, which is why the
  // caption area and the strip itself are marked separately.
  const CefRect strip_bounds = tab_strip_->view()->GetBounds();
  const CefRect caption_bounds =
      caption_panel_ ? caption_panel_->GetBounds() : CefRect();

  std::vector<CefDraggableRegion> regions;
  regions.emplace_back(
      CefRect(0, 0, strip_bounds.width + caption_bounds.width,
              metrics::kTabStripHeight),
      /*draggable=*/true);

  // Everything interactive in that band is carved back out.
  if (CefRefPtr<CefPanel> strip = tab_strip_->view()) {
    for (size_t i = 0; i < strip->GetChildViewCount(); ++i) {
      CefRefPtr<CefView> child = strip->GetChildViewAt(static_cast<int>(i));
      if (child) {
        regions.emplace_back(child->GetBounds(), /*draggable=*/false);
      }
    }
  }
  if (caption_panel_) {
    regions.emplace_back(caption_bounds, /*draggable=*/false);
  }

  window_->SetDraggableRegions(regions);
}

void MainWindow::RegisterAccelerators() {
  if (!window_) {
    return;
  }
  // high_priority = true for the window-level shortcuts, so a page cannot
  // swallow Ctrl+T or Ctrl+W.
  auto add = [this](int id, int key, bool shift, bool ctrl, bool alt) {
    window_->SetAccelerator(id, key, shift, ctrl, alt, /*high_priority=*/true);
  };

  add(kAccelNewTab, kVkT, false, true, false);
  add(kAccelCloseTab, kVkW, false, true, false);
  add(kAccelReopenTab, kVkT, true, true, false);
  add(kAccelNextTab, kVkTab, false, true, false);
  add(kAccelPrevTab, kVkTab, true, true, false);
  add(kAccelFocusOmnibox, kVkL, false, true, false);
  add(kAccelReload, kVkR, false, true, false);
  add(kAccelReload, kVkF5, false, false, false);
  add(kAccelReloadHard, kVkR, true, true, false);
  add(kAccelBack, kVkLeft, false, false, true);
  add(kAccelForward, kVkRight, false, false, true);
  add(kAccelToggleOverlay, kVkS, true, true, false);
  add(kAccelDevTools, kVkF12, false, false, false);
  add(kAccelFind, kVkF, false, true, false);
  add(kAccelNewWindow, kVkN, false, true, false);
  add(kAccelZoomIn, kVkPlus, false, true, false);
  add(kAccelZoomOut, kVkMinus, false, true, false);
  add(kAccelZoomReset, kVk0, false, true, false);
  // Ctrl+S, "save page" in every other browser. Ctrl+Shift+S stays with the
  // stats overlay, which PLAN.md section 8 gave it first.
  add(kAccelCapturePage, kVkS, false, true, false);
  // F11 is full screen, as it is in every other browser. Pinning this
  // browser's own auto-hiding chrome open is a different, rarer thing and
  // moved to Ctrl+F11 rather than sitting on the key people expect.
  add(kAccelToggleFullscreen, kVkF11, false, false, false);
  add(kAccelToggleChrome, kVkF11, false, true, false);

  for (int i = 0; i < 9; ++i) {
    add(kAccelTab1 + i, kVk1 + i, false, true, false);
  }
}

// ---------------------------------------------------------------------------
// Accelerators and buttons
// ---------------------------------------------------------------------------

bool MainWindow::OnAccelerator(CefRefPtr<CefWindow> window, int command_id) {
  if (!tab_manager_) {
    return false;
  }

  if (command_id >= kAccelTab1 && command_id < kAccelTab1 + 9) {
    const int index = command_id - kAccelTab1;
    if (index == 8) {
      // Ctrl+9 means the last tab, as in every other browser.
      tab_manager_->ActivateTabAtIndex(
          static_cast<int>(tab_manager_->count()) - 1);
    } else {
      tab_manager_->ActivateTabAtIndex(index);
    }
    return true;
  }

  switch (command_id) {
    case kAccelNewTab:
      tab_manager_->CreateTab(kNewTabUrl, true);
      return true;
    case kAccelCloseTab:
      tab_manager_->CloseTab(tab_manager_->active_tab_id());
      return true;
    case kAccelReopenTab:
      tab_manager_->ReopenLastClosedTab();
      return true;
    case kAccelNextTab:
      tab_manager_->ActivateNextTab();
      return true;
    case kAccelPrevTab:
      tab_manager_->ActivatePreviousTab();
      return true;
    case kAccelFocusOmnibox:
      // The address bar is hidden until the pointer goes near the top, so the
      // shortcut has to bring the chrome out before it can focus anything.
      SetChromeRevealed(true);
      SetChromeCompact(false);
      if (toolbar_) {
        toolbar_->FocusOmnibox();
      }
      return true;
    case kAccelReload:
      if (Tab* tab = tab_manager_->active_tab()) {
        tab->Reload(false);
      }
      return true;
    case kAccelReloadHard:
      if (Tab* tab = tab_manager_->active_tab()) {
        tab->Reload(true);
      }
      return true;
    case kAccelBack:
      OnBackRequested();
      return true;
    case kAccelForward:
      OnForwardRequested();
      return true;
    case kAccelToggleOverlay:
      OnMenuCommand(kMenuToggleOverlay);
      return true;
    case kAccelDevTools:
      OnMenuCommand(kMenuDevTools);
      return true;
    case kAccelCapturePage:
      OnMenuCommand(kMenuCapturePage);
      return true;
    case kAccelFind:
      OnMenuCommand(kMenuFind);
      return true;
    case kAccelNewWindow:
      MainWindow::CreateNewWindow(nullptr);
      return true;
    case kAccelZoomIn:
      AdjustZoom(10);
      return true;
    case kAccelZoomOut:
      AdjustZoom(-10);
      return true;
    case kAccelZoomReset:
      SetZoomPercent(100);
      return true;
    case kAccelToggleFullscreen:
      if (window_) {
        // Real full screen: the window takes the whole display and the
        // taskbar goes away. Toggling our own toolbar is not the same thing
        // and pretending it was is why this did nothing useful before.
        window_->SetFullscreen(!window_->IsFullscreen());
      }
      return true;
    case kAccelToggleChrome:
      if (AppState* state = AppState::Get()) {
        const bool now_auto_hide = !state->settings().chrome_auto_hide;
        state->mutable_settings().chrome_auto_hide = now_auto_hide;
        state->SaveSettings();
        if (!now_auto_hide) {
          SetChromeRevealed(true);
          SetChromeCompact(false);
        }
        UpdateChromeFromPointer();
      }
      return true;
    default:
      return false;
  }
}

void MainWindow::OnButtonPressed(CefRefPtr<CefButton> button) {
  if (!window_ || !button) {
    return;
  }
  switch (button->GetID()) {
    case 9000001:
      window_->Minimize();
      break;
    case 9000002:
      if (window_->IsMaximized()) {
        window_->Restore();
        if (maximize_button_) {
          maximize_button_->SetText(icons::kMaximize);
        }
      } else {
        window_->Maximize();
        if (maximize_button_) {
          maximize_button_->SetText(icons::kRestore);
        }
      }
      break;
    case 9000003:
      window_->Close();
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// TabStrip::Delegate
// ---------------------------------------------------------------------------

void MainWindow::OnTabSelected(int tab_id) {
  if (tab_manager_) {
    tab_manager_->ActivateTab(tab_id);
  }
}

void MainWindow::OnTabCloseRequested(int tab_id) {
  if (tab_manager_) {
    tab_manager_->CloseTab(tab_id);
  }
}

void MainWindow::OnNewTabRequested() {
  if (tab_manager_) {
    tab_manager_->CreateTab(kNewTabUrl, true);
  }
}

// ---------------------------------------------------------------------------
// Toolbar::Delegate
// ---------------------------------------------------------------------------

void MainWindow::OnBackRequested() {
  if (tab_manager_) {
    if (Tab* tab = tab_manager_->active_tab()) {
      tab->GoBack();
    }
  }
}

void MainWindow::OnForwardRequested() {
  if (tab_manager_) {
    if (Tab* tab = tab_manager_->active_tab()) {
      tab->GoForward();
    }
  }
}

void MainWindow::OnReloadOrStopRequested() {
  if (!tab_manager_) {
    return;
  }
  Tab* tab = tab_manager_->active_tab();
  if (!tab) {
    return;
  }
  if (tab->is_loading()) {
    tab->StopLoad();
  } else {
    tab->Reload(false);
  }
}

void MainWindow::OnNavigateRequested(const std::string& input) {
  if (!tab_manager_) {
    return;
  }
  std::string search_url = "https://duckduckgo.com/?q=%s";
  if (AppState* state = AppState::Get()) {
    search_url = state->settings().search_url;
  }
  const std::string url = ResolveOmniboxInput(input, search_url);
  if (url.empty()) {
    return;
  }
  if (Tab* tab = tab_manager_->active_tab()) {
    tab->LoadUrl(url);
  } else {
    tab_manager_->CreateTab(url, true);
  }
}

void MainWindow::OnMenuRequested(
    CefRefPtr<CefMenuButton> button,
    const CefPoint& screen_point,
    CefRefPtr<CefMenuButtonPressedLock> pressed_lock) {
  if (!menu_ || !button) {
    return;
  }
  MenuState state;
  if (AppState* app = AppState::Get()) {
    state.overlay_enabled = app->settings().overlay_enabled;
    state.gpu_line = app->GpuMenuLine();
    state.memory_line = app->MemoryMenuLine();
    state.gpu_mismatch = !app->gpu_matches_preferred();
    state.bookmarks_bar_visible = app->settings().show_bookmarks_bar;
    state.onshape_focus_mode = app->settings().onshape_focus_mode;
    state.auto_capture = app->auto_capture();
  }
  state.zoom_percent = zoom_percent_;

  button->ShowMenu(menu_->Build(state), screen_point, CEF_MENU_ANCHOR_TOPRIGHT);
}

// ---------------------------------------------------------------------------
// HamburgerMenu::Delegate
// ---------------------------------------------------------------------------

void MainWindow::OnMenuCommand(int command_id) {
  AppState* state = AppState::Get();
  Tab* tab = tab_manager_ ? tab_manager_->active_tab() : nullptr;

  switch (command_id) {
    case kMenuNewTab:
      OnNewTabRequested();
      break;
    case kMenuNewWindow:
      MainWindow::CreateNewWindow(nullptr);
      break;
    case kMenuToggleOverlay:
      if (state) {
        const bool enabled = !state->settings().overlay_enabled;
        state->SetOverlayEnabled(enabled);
        if (tab_manager_) {
          tab_manager_->SetFrameProbeEnabled(state->frame_probe_should_run());
        }
      }
      break;
    case kMenuGpuStatus:
      // Only reachable when the probe disagreed with our choice. Windows owns
      // the final say through its own Graphics settings page.
      sys::OpenWindowsGraphicsSettings();
      break;
    case kMenuSystemInfo:
      if (tab_manager_) {
        tab_manager_->CreateTab("cad://sysinfo", true);
      }
      break;
    case kMenuHistory:
      if (tab_manager_) {
        tab_manager_->CreateTab("cad://history", true);
      }
      break;
    case kMenuDownloads:
      if (tab_manager_) {
        tab_manager_->CreateTab("cad://downloads", true);
      }
      break;
    case kMenuZoomIn:
      AdjustZoom(10);
      break;
    case kMenuZoomOut:
      AdjustZoom(-10);
      break;
    case kMenuZoomReset:
      SetZoomPercent(100);
      break;
    case kMenuFind:
      // A find bar is not built yet; the shortcut must not silently do
      // nothing, so it opens the page's own find where one exists.
      if (tab) {
        tab->ExecuteJavaScript("window.find && window.find('')");
      }
      break;
    case kMenuPrint:
      if (tab) {
        tab->Print();
      }
      break;
    case kMenuDevTools:
      if (tab) {
        tab->ShowDevTools();
      }
      break;
    case kMenuToggleBookmarksBar:
      if (state) {
        state->mutable_settings().show_bookmarks_bar =
            !state->settings().show_bookmarks_bar;
        state->ApplySettingsChanges();
      }
      break;
    case kMenuCapturePage:
      CaptureActivePage();
      break;
    case kMenuToggleAutoCapture:
      if (state) {
        state->SetAutoCapture(!state->auto_capture());
      }
      break;
    case kMenuToggleOnshapeFocus:
      if (state) {
        const bool on = !state->settings().onshape_focus_mode;
        state->mutable_settings().onshape_focus_mode = on;
        state->ApplySettingsChanges();
        // Applied to the pages that are already open, so the toggle does
        // something the moment it is used rather than at the next reload.
        if (tab_manager_) {
          tab_manager_->SetSiteFocusEnabled(on);
        }
      }
      break;
    case kMenuSettings:
      if (tab_manager_) {
        tab_manager_->CreateTab("cad://settings", true);
      }
      break;
    case kMenuAbout:
      if (tab_manager_) {
        tab_manager_->CreateTab("cad://about", true);
      }
      break;
    case kMenuExit:
      if (window_) {
        window_->Close();
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------

void MainWindow::SetZoomPercent(int percent) {
  zoom_percent_ = std::clamp(percent, 25, 500);
  if (!tab_manager_) {
    return;
  }
  Tab* tab = tab_manager_->active_tab();
  if (!tab) {
    return;
  }
  // Chromium's zoom level is logarithmic: level = log(factor) / log(1.2).
  const double factor = zoom_percent_ / 100.0;
  const double level = std::log(factor) / std::log(1.2);
  tab->SetZoomLevel(level);
}

void MainWindow::AdjustZoom(int delta_percent) {
  SetZoomPercent(zoom_percent_ + delta_percent);
}

// ---------------------------------------------------------------------------
// TabManager callbacks
// ---------------------------------------------------------------------------

void MainWindow::OnTabAdded(Tab* tab) {
  if (!tab || !content_panel_) {
    return;
  }
  CefRefPtr<CefBrowserView> view = tab->browser_view();
  if (!view) {
    return;
  }
  // Every live tab is a child of the content panel; only the active one is
  // visible. Adding and removing views instead would tear the browser down on
  // every tab switch.
  content_panel_->AddChildView(view);
  view->SetVisible(tab->is_active());
  content_panel_->Layout();
}

void MainWindow::OnTabRemoved(Tab* tab) {
  if (!tab || !content_panel_) {
    return;
  }
  if (CefRefPtr<CefBrowserView> view = tab->browser_view()) {
    content_panel_->RemoveChildView(view);
    content_panel_->Layout();
  }
}

void MainWindow::OnTabListChanged() {
  if (!tab_strip_ || !tab_manager_) {
    return;
  }
  std::vector<Tab*> tabs;
  tabs.reserve(tab_manager_->count());
  for (const std::unique_ptr<Tab>& tab : tab_manager_->tabs()) {
    tabs.push_back(tab.get());
  }
  tab_strip_->Rebuild(tabs, tab_manager_->active_tab_id(), theme_);
  UpdateDraggableRegions();

  if (AppState* state = AppState::Get()) {
    state->sampler().SetTabCounts(static_cast<int>(tab_manager_->count()),
                                  tab_manager_->sleeping_count());
  }
  ScheduleSessionSave();
}

void MainWindow::OnActiveTabChanged(Tab* active) {
  if (!content_panel_ || !tab_manager_) {
    return;
  }
  for (const std::unique_ptr<Tab>& tab : tab_manager_->tabs()) {
    if (CefRefPtr<CefBrowserView> view = tab->browser_view()) {
      view->SetVisible(tab.get() == active);
    }
  }
  content_panel_->Layout();

  if (active && window_) {
    window_->SetTitle(active->display_title() + " - CAD Browser");
  }
  UpdateToolbarFromActiveTab();
  OnTabListChanged();
  if (active) {
    if (CefRefPtr<CefBrowserView> view = active->browser_view()) {
      view->RequestFocus();
    }
  }
}

void MainWindow::OnTabStateChanged(Tab* tab) {
  if (!tab_manager_) {
    return;
  }
  if (tab && tab->id() == tab_manager_->active_tab_id()) {
    UpdateToolbarFromActiveTab();
    if (window_) {
      window_->SetTitle(tab->display_title() + " - CAD Browser");
    }
  }
  OnTabListChanged();
}

void MainWindow::OnTabFaviconChanged(Tab* tab,
                                     const std::vector<std::string>& urls) {
  // Favicons are not drawn in the strip yet; the hook exists so the download
  // path has somewhere to land when they are.
}

void MainWindow::OnFullscreenChanged(bool fullscreen) {
  is_fullscreen_ = fullscreen;
  if (!window_) {
    return;
  }
  // The page already fills the window; going fullscreen only removes the
  // window frame and stops the chrome revealing on hover.
  if (fullscreen) {
    SetChromeRevealed(false);
  }
  window_->SetFullscreen(fullscreen);
}

void MainWindow::OnLastTabClosed() {
  // Deliberately does not close the window. Chrome closes on the last tab,
  // and copying that turned every tab control into something that could end
  // the session by accident: close the one tab you had open, or misjudge a
  // click near the tab strip, and the browser is gone with it. Here the only
  // things that close the window are the caption X, the Exit menu item and
  // the window manager. An empty window gets a new tab page instead.
  if (closing_ || !tab_manager_) {
    return;
  }
  tab_manager_->CreateTab(kNewTabUrl, true);
}

void MainWindow::UpdateToolbarFromActiveTab() {
  if (!toolbar_ || !tab_manager_) {
    return;
  }
  Tab* tab = tab_manager_->active_tab();
  if (!tab) {
    toolbar_->UpdateState(std::string(), false, false, false);
    return;
  }
  toolbar_->UpdateState(tab->url(), tab->is_loading(), tab->can_go_back(),
                        tab->can_go_forward());
}

// ---------------------------------------------------------------------------
// AppState callbacks
// ---------------------------------------------------------------------------

void MainWindow::OnStatsSnapshot(const sys::StatsSnapshot& snapshot) {
  if (overlay_ && overlay_->visible()) {
    if (AppState* state = AppState::Get()) {
      overlay_->Update(snapshot, state->settings());
    }
  }
}

void MainWindow::OnSettingsChanged() {
  ApplyTheme();
  UpdateChromeFromPointer();
  if (overlay_) {
    if (AppState* state = AppState::Get()) {
      overlay_->ApplySettings(state->settings());
      overlay_->SetVisible(state->settings().overlay_enabled);
      RebuildRootLayout();
    }
  }
}

void MainWindow::OnGpuStatusChanged() {
  // The menu reads the status when it opens, so it needs nothing here. An
  // open System page does: it renders once on request, and the WebGL probe
  // answers afterwards, so without this push it would sit on "still checking"
  // until the user reloaded it.
  AppState* state = AppState::Get();
  if (!tab_manager_ || !state) {
    return;
  }

  JsonValue gpu = JsonValue::Object();
  gpu.Set("probed", JsonValue::String(state->probed_gpu_renderer()));
  gpu.Set("matches", JsonValue::Bool(state->gpu_matches_preferred()));
  const std::string script =
      "window.cadPage&&window.cadPage.updateGpu&&window.cadPage.updateGpu(" +
      gpu.Serialize() + ")";

  for (const std::unique_ptr<Tab>& tab : tab_manager_->tabs()) {
    if (tab->url().rfind("cad://sysinfo", 0) == 0) {
      tab->ExecuteJavaScript(script);
    }
  }
}

void MainWindow::SetOverlayVisible(bool visible) {
  if (overlay_) {
    overlay_->SetVisible(visible);
    // The bar occupies layout space, so showing or hiding it resizes the page.
    RebuildRootLayout();
  }
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

void MainWindow::RestoreFrom(const SessionWindow& restore) {
  if (!tab_manager_) {
    return;
  }
  for (const SessionTab& tab : restore.tabs) {
    tab_manager_->CreateSleepingTab(tab.url, tab.title, tab.pinned);
  }
  if (tab_manager_->count() == 0) {
    tab_manager_->CreateTab(kNewTabUrl, true);
    return;
  }
  // Only the active tab is woken; the rest stay asleep until selected.
  tab_manager_->ActivateTabAtIndex(restore.active_index);
}

void MainWindow::ScheduleSessionSave() {
  if (closing_) {
    return;
  }
  const uint64_t generation = ++session_save_generation_;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<MainWindow> self, uint64_t expected) {
            // A newer change has already been scheduled; let that one write.
            if (self->session_save_generation_ == expected) {
              self->SaveSessionNow();
            }
          },
          CefRefPtr<MainWindow>(this), generation),
      kSessionSaveDebounceMs);
}

void MainWindow::SaveSessionNow() {
  AppState* state = AppState::Get();
  if (!state || !tab_manager_) {
    return;
  }

  Session session;
  for (MainWindow* window : state->windows()) {
    TabManager* manager = window->tabs();
    if (!manager) {
      continue;
    }
    SessionWindow entry;
    entry.active_index = manager->IndexOf(manager->active_tab_id());
    if (entry.active_index < 0) {
      entry.active_index = 0;
    }
    if (CefRefPtr<CefWindow> native = window->window()) {
      const CefRect bounds = native->GetBounds();
      entry.width = bounds.width;
      entry.height = bounds.height;
      entry.maximized = native->IsMaximized();
    }
    for (const std::unique_ptr<Tab>& tab : manager->tabs()) {
      if (tab->url().empty() || IsNewTabUrl(tab->url())) {
        continue;
      }
      SessionTab session_tab;
      session_tab.url = tab->url();
      session_tab.title = tab->title();
      session_tab.pinned = tab->pinned();
      entry.tabs.push_back(std::move(session_tab));
    }
    if (!entry.tabs.empty()) {
      session.windows.push_back(std::move(entry));
    }
  }

  SaveSession(state->SessionPath(), session);
}

void MainWindow::ScheduleDiscardSweep() {
  if (closing_) {
    return;
  }
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(
                         [](CefRefPtr<MainWindow> self) {
                           if (self->closing_ || !self->tab_manager_) {
                             return;
                           }
                           self->tab_manager_->RunDiscardSweep();
                           self->ScheduleDiscardSweep();
                         },
                         CefRefPtr<MainWindow>(this)),
                     kDiscardSweepIntervalMs);
}

// ---------------------------------------------------------------------------
// cad:// page messages
// ---------------------------------------------------------------------------

void MainWindow::OnUiMessage(Tab* tab,
                             const std::string& name,
                             const std::string& payload) {
  HandleUiMessage(tab, name, payload);
}

void MainWindow::HandleUiMessage(Tab* tab,
                                 const std::string& name,
                                 const std::string& payload) {
  AppState* state = AppState::Get();
  if (!state) {
    return;
  }

  if (name == "sysinfo.request") {
    PushSystemInfo(tab);
    return;
  }
  if (name == "settings.request") {
    PushSettings(tab);
    return;
  }
  if (name == "gpu.probe") {
    // cad://gpuprobe reports what WebGL says it is actually rendering with.
    const JsonValue json = JsonValue::Parse(payload);
    state->SetProbedGpuRenderer(json.GetString("renderer"));
    return;
  }
  if (name == "navigate") {
    // The new tab page sends whatever was typed. It goes through exactly the
    // same URL-or-search rule as the omnibox, so the two cannot disagree.
    const JsonValue json = JsonValue::Parse(payload);
    const std::string input = json.GetString("url");
    if (input.empty() || !tab_manager_) {
      return;
    }
    const std::string url =
        ResolveOmniboxInput(input, state->settings().search_url);
    if (url.empty()) {
      return;
    }
    if (Tab* active = tab_manager_->active_tab()) {
      active->LoadUrl(url);
    }
    return;
  }
  if (name == "settings.set") {
    const JsonValue json = JsonValue::Parse(payload);
    Settings& settings = state->mutable_settings();
    const std::string key = json.GetString("key");

    if (key == "theme") {
      settings.theme = json.GetString("value", settings.theme);
    } else if (key == "startup_mode") {
      settings.startup_mode = json.GetString("value", settings.startup_mode);
    } else if (key == "search_url") {
      settings.search_url = json.GetString("value", settings.search_url);
    } else if (key == "homepage") {
      settings.homepage = json.GetString("value", settings.homepage);
    } else if (key == "chrome_auto_hide") {
      settings.chrome_auto_hide = json.GetBool("value", true);
    } else if (key == "gpu_prefer_high_performance") {
      settings.gpu_prefer_high_performance = json.GetBool("value", true);
    } else if (key == "force_hardware_acceleration") {
      settings.force_hardware_acceleration = json.GetBool("value", false);
    } else if (key == "uncapped_fps_default") {
      settings.uncapped_fps_default = json.GetBool("value", false);
    } else if (key == "discard_enabled") {
      settings.discard_enabled = json.GetBool("value", true);
    } else if (key == "discard_idle_minutes") {
      settings.discard_idle_minutes =
          static_cast<int>(json.GetInt("value", settings.discard_idle_minutes));
    } else if (key == "discard_tab_count") {
      settings.discard_tab_count =
          static_cast<int>(json.GetInt("value", settings.discard_tab_count));
    } else if (key == "blocklist_enabled") {
      settings.blocklist_enabled = json.GetBool("value", true);
    } else if (key == "overlay_position") {
      settings.overlay_position =
          json.GetString("value", settings.overlay_position);
    } else if (key == "overlay_opacity") {
      settings.overlay_opacity =
          static_cast<int>(json.GetInt("value", settings.overlay_opacity));
    } else if (key == "overlay_show_fps") {
      settings.overlay_show_fps = json.GetBool("value", true);
    } else if (key == "overlay_show_gpu") {
      settings.overlay_show_gpu = json.GetBool("value", true);
    } else if (key == "overlay_show_memory") {
      settings.overlay_show_memory = json.GetBool("value", true);
    } else if (key == "overlay_show_cpu") {
      settings.overlay_show_cpu = json.GetBool("value", true);
    } else if (key == "overlay_show_tabs") {
      settings.overlay_show_tabs = json.GetBool("value", true);
    } else if (key == "cache_path") {
      settings.cache_path = json.GetString("value", settings.cache_path);
    } else if (key == "capture_folder") {
      settings.capture_folder =
          json.GetString("value", settings.capture_folder);
      if (settings.capture_folder.empty()) {
        settings.capture_folder = kDefaultCaptureFolder;
      }
    }

    state->ApplySettingsChanges();
    PushSettings(tab);
    return;
  }
  if (name == "openGraphicsSettings") {
    sys::OpenWindowsGraphicsSettings();
    return;
  }
}

void MainWindow::PushSystemInfo(Tab* tab) {
  AppState* state = AppState::Get();
  if (!state || !tab) {
    return;
  }

  const sys::MemoryInfo& memory = state->memory_info();
  const sys::CpuInfo& cpu = state->cpu_info();
  const sys::GpuInfo& gpu = state->gpu_info();
  const sys::OsInfo& os = state->os_info();
  const sys::MachineInfo& machine = state->machine_info();

  JsonValue root = JsonValue::Object();

  JsonValue system = JsonValue::Object();
  system.Set("os", JsonValue::String(os.Describe()));
  system.Set("edition", JsonValue::String(os.edition));
  system.Set("machine", JsonValue::String(machine.manufacturer + " " +
                                          machine.model));
  system.Set("baseboard", JsonValue::String(machine.baseboard_manufacturer +
                                            " " + machine.baseboard_product));
  system.Set("bios", JsonValue::String(machine.bios_vendor + " " +
                                       machine.bios_version + " (" +
                                       machine.bios_date + ")"));
  root.Set("system", std::move(system));

  JsonValue cpu_json = JsonValue::Object();
  cpu_json.Set("brand", JsonValue::String(cpu.brand));
  cpu_json.Set("cores", JsonValue::Int(cpu.physical_cores));
  cpu_json.Set("threads", JsonValue::Int(cpu.logical_processors));
  cpu_json.Set("hybrid", JsonValue::Bool(cpu.hybrid));
  cpu_json.Set("performanceCores", JsonValue::Int(cpu.performance_cores));
  cpu_json.Set("efficiencyCores", JsonValue::Int(cpu.efficiency_cores));
  cpu_json.Set("baseMhz", JsonValue::Int(cpu.base_mhz));
  cpu_json.Set("l3", JsonValue::String(sys::FormatBytes(cpu.cache_bytes(3))));
  root.Set("cpu", std::move(cpu_json));

  JsonValue memory_json = JsonValue::Object();
  memory_json.Set("installed",
                  JsonValue::String(sys::FormatBytes(memory.installed_bytes)));
  memory_json.Set("installedSource",
                  JsonValue::String(
                      sys::InstalledMemorySourceName(memory.installed_source)));
  memory_json.Set("usable",
                  JsonValue::String(sys::FormatBytes(memory.usable_bytes)));
  memory_json.Set("reserved", JsonValue::String(sys::FormatBytes(
                                  memory.hardware_reserved_bytes)));
  memory_json.Set("maxCapacity",
                  JsonValue::String(
                      memory.array.max_capacity_bytes > 0
                          ? sys::FormatBytes(memory.array.max_capacity_bytes)
                          : std::string("not reported")));
  memory_json.Set("slotsUsed", JsonValue::Int(memory.slots_used));
  memory_json.Set("slotsTotal", JsonValue::Int(memory.slots_total));
  memory_json.Set("channels", JsonValue::String(memory.channel_estimate));
  memory_json.Set("smbiosVersion", JsonValue::String(memory.smbios_version));

  JsonValue modules = JsonValue::Array();
  for (const sys::MemoryModule& module : memory.modules) {
    JsonValue entry = JsonValue::Object();
    entry.Set("slot", JsonValue::String(module.device_locator));
    entry.Set("populated", JsonValue::Bool(module.populated()));
    entry.Set("size", JsonValue::String(
                          module.populated()
                              ? sys::FormatBytes(module.size_bytes, 0)
                              : std::string("empty")));
    entry.Set("type", JsonValue::String(module.memory_type));
    entry.Set("rated", JsonValue::Int(module.speed_mts));
    entry.Set("running", JsonValue::Int(module.configured_speed_mts));
    entry.Set("manufacturer", JsonValue::String(module.manufacturer));
    entry.Set("part", JsonValue::String(module.part_number));
    modules.Append(std::move(entry));
  }
  memory_json.Set("modules", std::move(modules));

  JsonValue warnings = JsonValue::Array();
  for (const std::string& warning : memory.warnings) {
    warnings.Append(JsonValue::String(warning));
  }
  memory_json.Set("warnings", std::move(warnings));
  root.Set("memory", std::move(memory_json));

  JsonValue adapters = JsonValue::Array();
  for (size_t i = 0; i < gpu.adapters.size(); ++i) {
    const sys::GpuAdapter& adapter = gpu.adapters[i];
    JsonValue entry = JsonValue::Object();
    entry.Set("name", JsonValue::String(adapter.description));
    entry.Set("vendor", JsonValue::String(adapter.vendor_name));
    entry.Set("vendorId", JsonValue::String(sys::FormatHex32(adapter.vendor_id)));
    entry.Set("deviceId", JsonValue::String(sys::FormatHex32(adapter.device_id)));
    entry.Set("vram", JsonValue::String(
                          sys::FormatBytes(adapter.dedicated_video_memory)));
    entry.Set("shared", JsonValue::String(
                            sys::FormatBytes(adapter.shared_system_memory)));
    entry.Set("driver", JsonValue::String(adapter.driver_version));
    entry.Set("outputs", JsonValue::Int(adapter.output_count));
    entry.Set("software", JsonValue::Bool(adapter.is_software));
    entry.Set("preferred",
              JsonValue::Bool(static_cast<int>(i) == gpu.preferred_index));
    entry.Set("osHighPerformance",
              JsonValue::Bool(static_cast<int>(i) ==
                              gpu.os_high_performance_index));
    adapters.Append(std::move(entry));
  }

  JsonValue gpu_json = JsonValue::Object();
  gpu_json.Set("adapters", std::move(adapters));
  gpu_json.Set("probed", JsonValue::String(state->probed_gpu_renderer()));
  gpu_json.Set("matches", JsonValue::Bool(state->gpu_matches_preferred()));
  root.Set("gpu", std::move(gpu_json));

  JsonValue blocking = JsonValue::Object();
  blocking.Set("hosts",
               JsonValue::Int(static_cast<int64_t>(
                   state->blocklist().host_count())));
  blocking.Set("blocked",
               JsonValue::Int(static_cast<int64_t>(
                   state->blocklist().blocked_count())));
  blocking.Set("enabled", JsonValue::Bool(state->settings().blocklist_enabled));
  root.Set("blocking", std::move(blocking));

  tab->ExecuteJavaScript("window.cadPage&&window.cadPage.render(" +
                         root.Serialize() + ")");
}

void MainWindow::PushSettings(Tab* tab) {
  AppState* state = AppState::Get();
  if (!state || !tab) {
    return;
  }
  JsonValue root = SettingsStore::ToJson(state->settings());
  root.Set("searchName", JsonValue::String(state->settings().search_name));
  tab->ExecuteJavaScript("window.cadPage&&window.cadPage.render(" +
                         root.Serialize() + ")");
}

// ---------------------------------------------------------------------------
// Middle-click to close a tab
// ---------------------------------------------------------------------------
//
// CEF Views cannot express this. A CefButton tells its delegate that it was
// activated and nothing else: there is no button field, and CefViewDelegate
// has no mouse callbacks at all. So the window's WndProc is subclassed and
// WM_MBUTTONUP is hit-tested against the tab strip by screen point.
//
// The message is always passed on to the original procedure afterwards. This
// only ever adds behaviour; it never swallows an event Views was going to
// handle.

namespace {

// Set on the subclassed window so the static procedure can find its owner
// without a global table.
constexpr wchar_t kOwnerProp[] = L"CadBrowserWindow";
constexpr wchar_t kPrevProcProp[] = L"CadBrowserPrevProc";

LRESULT CALLBACK MiddleClickProc(HWND hwnd,
                                 UINT message,
                                 WPARAM wparam,
                                 LPARAM lparam) {
  auto* previous = reinterpret_cast<WNDPROC>(::GetPropW(hwnd, kPrevProcProp));
  if (message == WM_MBUTTONUP) {
    if (auto* self =
            static_cast<MainWindow*>(::GetPropW(hwnd, kOwnerProp))) {
      // lParam is client-relative; the hit test works in screen coordinates
      // because the tab strip sits inside an overlay with its own space.
      // windowsx.h has GET_X_LPARAM for this, but including it here defines
      // GetFirstChild and GetNextSibling as macros and breaks CEF's DOM
      // headers, so the two signed halves are taken out by hand.
      POINT point{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                  static_cast<int>(static_cast<short>(HIWORD(lparam)))};
      ::ClientToScreen(hwnd, &point);
      self->HandleMiddleClick(point.x, point.y);
    }
  }
  if (message == WM_NCDESTROY) {
    ::RemovePropW(hwnd, kOwnerProp);
    ::RemovePropW(hwnd, kPrevProcProp);
    if (previous) {
      ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(previous));
    }
  }
  if (previous) {
    return ::CallWindowProcW(previous, hwnd, message, wparam, lparam);
  }
  return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

void MainWindow::InstallMiddleClickHook() {
  if (!window_ || middle_click_hooked_) {
    return;
  }
  HWND hwnd = window_->GetWindowHandle();
  if (!hwnd) {
    return;
  }
  auto previous = reinterpret_cast<WNDPROC>(
      ::GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
  if (!previous) {
    return;
  }
  ::SetPropW(hwnd, kOwnerProp, this);
  ::SetPropW(hwnd, kPrevProcProp, reinterpret_cast<HANDLE>(previous));
  ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&MiddleClickProc));
  middle_click_hooked_ = true;
}

void MainWindow::RemoveMiddleClickHook() {
  if (!window_ || !middle_click_hooked_) {
    return;
  }
  middle_click_hooked_ = false;
  HWND hwnd = window_->GetWindowHandle();
  if (!hwnd) {
    return;
  }
  // The owner is cleared first: a message can still arrive between here and
  // the procedure being restored, and it must not reach a window that is on
  // its way out.
  ::RemovePropW(hwnd, kOwnerProp);
  auto* previous = reinterpret_cast<WNDPROC>(::GetPropW(hwnd, kPrevProcProp));
  if (previous) {
    ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(previous));
    ::RemovePropW(hwnd, kPrevProcProp);
  }
}

bool MainWindow::HandleMiddleClick(int screen_x, int screen_y) {
  if (closing_ || !tab_strip_ || !tab_manager_) {
    return false;
  }
  const int tab_id = tab_strip_->TabIdAtScreenPoint(screen_x, screen_y);
  if (tab_id == 0) {
    return false;
  }
  tab_manager_->CloseTab(tab_id);
  return true;
}

}  // namespace cad
