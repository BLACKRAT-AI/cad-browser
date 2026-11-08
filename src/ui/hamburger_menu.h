// CAD Browser - the three-bar menu.
//
// Layout is fixed by PLAN.md section 9. The GPU and RAM lines are disabled
// entries rather than commands: they exist so the two numbers people actually
// want are one click away, not buried in a settings page.
#pragma once

#include <string>

#include "include/cef_menu_model.h"
#include "include/cef_menu_model_delegate.h"

namespace cad {

enum MenuCommand {
  kMenuNewTab = 100,
  kMenuNewWindow,
  kMenuToggleOverlay,
  kMenuGpuStatus,
  kMenuMemoryStatus,
  kMenuSystemInfo,
  kMenuHistory,
  kMenuDownloads,
  kMenuZoomOut,
  kMenuZoomReset,
  kMenuZoomIn,
  kMenuFind,
  kMenuPrint,
  kMenuDevTools,
  kMenuSettings,
  kMenuAbout,
  kMenuExit,
  kMenuToggleBookmarksBar,
  kMenuToggleOnshapeFocus,
  kMenuCapturePage,
  kMenuToggleAutoCapture,
};

// What the menu needs to know to draw itself.
struct MenuState {
  bool overlay_enabled = false;
  bool gpu_mismatch = false;
  std::string gpu_line;
  std::string memory_line;
  int zoom_percent = 100;
  bool bookmarks_bar_visible = false;
  bool onshape_focus_mode = false;
  bool auto_capture = false;
};

class HamburgerMenu : public CefMenuModelDelegate {
 public:
  class Delegate {
   public:
    virtual void OnMenuCommand(int command_id) = 0;
    // The menu opened or closed. The chrome stays revealed while it is open,
    // or the menu would dismiss the bar it is anchored to.
    virtual void OnMenuOpenChanged(bool open) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit HamburgerMenu(Delegate* delegate);

  // Builds a fresh model. Rebuilt on every open so the live values are right.
  CefRefPtr<CefMenuModel> Build(const MenuState& state);

  void DetachDelegate() { delegate_ = nullptr; }

  // CefMenuModelDelegate
  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model,
                      int command_id,
                      cef_event_flags_t event_flags) override;
  void MenuWillShow(CefRefPtr<CefMenuModel> menu_model) override;
  void MenuClosed(CefRefPtr<CefMenuModel> menu_model) override;

 private:
  Delegate* delegate_ = nullptr;

  IMPLEMENT_REFCOUNTING(HamburgerMenu);
  DISALLOW_COPY_AND_ASSIGN(HamburgerMenu);
};

}  // namespace cad
