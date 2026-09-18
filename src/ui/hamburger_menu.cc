#include "ui/hamburger_menu.h"

namespace cad {
namespace {

// Virtual key codes for the accelerator labels shown beside menu items.
constexpr int kVkT = 0x54;
constexpr int kVkN = 0x4E;
constexpr int kVkS = 0x53;
constexpr int kVkH = 0x48;
constexpr int kVkJ = 0x4A;
constexpr int kVkF = 0x46;
constexpr int kVkP = 0x50;
constexpr int kVkF12 = 0x7B;

}  // namespace

HamburgerMenu::HamburgerMenu(Delegate* delegate) : delegate_(delegate) {}

CefRefPtr<CefMenuModel> HamburgerMenu::Build(const MenuState& state) {
  CefRefPtr<CefMenuModel> model = CefMenuModel::CreateMenuModel(this);

  model->AddItem(kMenuNewTab, "New tab");
  model->SetAccelerator(kMenuNewTab, kVkT, false, true, false);
  model->AddItem(kMenuNewWindow, "New window");
  model->SetAccelerator(kMenuNewWindow, kVkN, false, true, false);

  model->AddSeparator();

  model->AddCheckItem(kMenuToggleOverlay, "Stats overlay");
  model->SetChecked(kMenuToggleOverlay, state.overlay_enabled);
  model->SetAccelerator(kMenuToggleOverlay, kVkS, true, true, false);

  // Live hardware status. These are labels, except that a GPU mismatch is
  // worth making clickable so the fix is one step away.
  model->AddItem(kMenuGpuStatus, state.gpu_line);
  model->SetEnabled(kMenuGpuStatus, state.gpu_mismatch);

  model->AddItem(kMenuMemoryStatus, state.memory_line);
  model->SetEnabled(kMenuMemoryStatus, false);

  model->AddItem(kMenuSystemInfo, "System info...");

  model->AddSeparator();

  model->AddItem(kMenuHistory, "History");
  model->SetAccelerator(kMenuHistory, kVkH, false, true, false);
  model->AddItem(kMenuDownloads, "Downloads");
  model->SetAccelerator(kMenuDownloads, kVkJ, false, true, false);

  model->AddItem(kMenuZoomOut, "Zoom out");
  model->AddItem(kMenuZoomReset,
                 "Zoom " + std::to_string(state.zoom_percent) + "%");
  model->AddItem(kMenuZoomIn, "Zoom in");

  model->AddItem(kMenuFind, "Find...");
  model->SetAccelerator(kMenuFind, kVkF, false, true, false);
  model->AddItem(kMenuPrint, "Print...");
  model->SetAccelerator(kMenuPrint, kVkP, false, true, false);
  model->AddItem(kMenuDevTools, "Developer tools");
  model->SetAccelerator(kMenuDevTools, kVkF12, false, false, false);

  model->AddCheckItem(kMenuToggleBookmarksBar, "Bookmarks bar");
  model->SetChecked(kMenuToggleBookmarksBar, state.bookmarks_bar_visible);
  model->AddCheckItem(kMenuToggleOnshapeFocus, "Onshape focus mode");
  model->SetChecked(kMenuToggleOnshapeFocus, state.onshape_focus_mode);

  model->AddSeparator();

  model->AddItem(kMenuSettings, "Settings");
  model->AddItem(kMenuAbout, "About CAD Browser");
  model->AddItem(kMenuExit, "Exit");

  return model;
}

void HamburgerMenu::ExecuteCommand(CefRefPtr<CefMenuModel> menu_model,
                                   int command_id,
                                   cef_event_flags_t event_flags) {
  if (delegate_) {
    delegate_->OnMenuCommand(command_id);
  }
}

void HamburgerMenu::MenuWillShow(CefRefPtr<CefMenuModel> menu_model) {
  if (delegate_) {
    delegate_->OnMenuOpenChanged(true);
  }
}

void HamburgerMenu::MenuClosed(CefRefPtr<CefMenuModel> menu_model) {
  if (delegate_) {
    delegate_->OnMenuOpenChanged(false);
  }
}

}  // namespace cad
