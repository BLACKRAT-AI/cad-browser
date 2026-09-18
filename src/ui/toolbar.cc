#include "ui/toolbar.h"

#include "browser/url_util.h"
#include "include/views/cef_box_layout.h"

namespace cad {

Toolbar::Toolbar(Delegate* delegate) : delegate_(delegate) {
  panel_ = CefPanel::CreatePanel(nullptr);

  CefBoxLayoutSettings settings;
  settings.horizontal = 1;
  settings.between_child_spacing = 2;
  settings.inside_border_horizontal_spacing = 6;
  settings.inside_border_vertical_spacing = 4;
  settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  CefRefPtr<CefBoxLayout> layout = panel_->SetToBoxLayout(settings);

  back_ = MakeToolButton(kBackButtonId, icons::kBack, "Back (Alt+Left)");
  forward_ =
      MakeToolButton(kForwardButtonId, icons::kForward, "Forward (Alt+Right)");
  reload_ = MakeToolButton(kReloadButtonId, icons::kReload, "Reload (Ctrl+R)");
  panel_->AddChildView(back_);
  panel_->AddChildView(forward_);
  panel_->AddChildView(reload_);

  omnibox_ = CefTextfield::CreateTextfield(this);
  omnibox_->SetFontList(UiFontList(12));
  omnibox_->SetPlaceholderText("Search or enter address");
  omnibox_->SetAccessibleName("Address and search bar");
  panel_->AddChildView(omnibox_);
  // The omnibox takes every pixel the buttons do not.
  layout->SetFlexForView(omnibox_, 1);

  menu_ = CefMenuButton::CreateMenuButton(this, icons::kMenu);
  menu_->SetID(0);
  menu_->SetInkDropEnabled(false);
  menu_->SetFocusable(false);
  menu_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  menu_->SetFontList(UiFontList(17));
  menu_->SetMinimumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kOmniboxHeight));
  menu_->SetMaximumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kOmniboxHeight));
  menu_->SetTooltipText("Menu");
  menu_->SetAccessibleName("Main menu");
  panel_->AddChildView(menu_);

  // Starts compact. The chrome is hidden by default and reveals in its
  // compact form first, so the buttons must not be visible until asked for.
  for (CefRefPtr<CefLabelButton> button : {back_, forward_, reload_}) {
    button->SetVisible(false);
  }
  menu_->SetVisible(false);
}

CefRefPtr<CefLabelButton> Toolbar::MakeToolButton(int id,
                                                  const std::string& label,
                                                  const std::string& tooltip) {
  CefRefPtr<CefLabelButton> button =
      CefLabelButton::CreateLabelButton(this, label);
  button->SetID(id);
  button->SetInkDropEnabled(false);
  button->SetFocusable(false);
  // Centred explicitly. These glyphs come from different Unicode blocks and
  // have different metrics, so leaving the alignment to default made each
  // button look as though its icon had been placed by hand.
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  button->SetFontList(UiFontList(17));
  button->SetMinimumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kOmniboxHeight));
  button->SetMaximumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kOmniboxHeight));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  return button;
}

void Toolbar::ApplyTheme(const Theme& theme) {
  theme_ = theme;
  if (!panel_) {
    return;
  }
  panel_->SetBackgroundColor(theme.chrome_surface);
  for (CefRefPtr<CefLabelButton> button : {back_, forward_, reload_}) {
    if (button) {
      button->SetBackgroundColor(theme.chrome_surface);
      button->SetEnabledTextColors(theme.text);
    }
  }
  if (menu_) {
    menu_->SetBackgroundColor(theme.chrome_surface);
    menu_->SetEnabledTextColors(theme.text);
  }
  // The textfield colour setters were removed in CEF API 15000. Text,
  // placeholder and selection colours are now window-level theme colours,
  // applied by MainWindow::ApplyTheme; only the background is still set on
  // the view itself.
  if (omnibox_) {
    omnibox_->SetBackgroundColor(theme.omnibox);
  }
}

void Toolbar::OnButtonStateChanged(CefRefPtr<CefButton> button) {
  if (!button) {
    return;
  }
  switch (button->GetState()) {
    case CEF_BUTTON_STATE_HOVERED:
      button->SetBackgroundColor(theme_.chrome_hover);
      break;
    case CEF_BUTTON_STATE_PRESSED:
      button->SetBackgroundColor(theme_.chrome_pressed);
      break;
    default:
      button->SetBackgroundColor(theme_.chrome_surface);
      break;
  }
}

void Toolbar::UpdateState(const std::string& url,
                          bool is_loading,
                          bool can_go_back,
                          bool can_go_forward) {
  is_loading_ = is_loading;

  if (back_) {
    back_->SetEnabled(can_go_back);
  }
  if (forward_) {
    forward_->SetEnabled(can_go_forward);
  }
  if (reload_) {
    reload_->SetText(is_loading ? icons::kStop : icons::kReload);
    reload_->SetTooltipText(is_loading ? "Stop loading (Esc)"
                                       : "Reload (Ctrl+R)");
  }

  if (omnibox_ && !omnibox_->HasFocus()) {
    // Only rewrite the address when the user is not editing it. The new tab
    // page shows an empty bar: Chromium normalises the URL to "cad://newtab/",
    // so both spellings have to be recognised.
    const bool is_new_tab = url == "cad://newtab" || url == "cad://newtab/";
    suppress_omnibox_update_ = true;
    omnibox_->SetText(is_new_tab ? std::string() : url);
    suppress_omnibox_update_ = false;
  }
}

void Toolbar::FocusOmnibox() {
  if (omnibox_) {
    omnibox_->RequestFocus();
    omnibox_->SelectAll(false);
  }
}

std::string Toolbar::omnibox_text() const {
  return omnibox_ ? omnibox_->GetText().ToString() : std::string();
}

void Toolbar::OnButtonPressed(CefRefPtr<CefButton> button) {
  if (!delegate_ || !button) {
    return;
  }
  switch (button->GetID()) {
    case kBackButtonId:
      delegate_->OnBackRequested();
      break;
    case kForwardButtonId:
      delegate_->OnForwardRequested();
      break;
    case kReloadButtonId:
      delegate_->OnReloadOrStopRequested();
      break;
    default:
      break;
  }
}

void Toolbar::OnMenuButtonPressed(
    CefRefPtr<CefMenuButton> menu_button,
    const CefPoint& screen_point,
    CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) {
  if (delegate_) {
    delegate_->OnMenuRequested(menu_button, screen_point, button_pressed_lock);
  }
}

void Toolbar::SetCompact(bool compact) {
  if (compact_ == compact) {
    return;
  }
  compact_ = compact;
  for (CefRefPtr<CefLabelButton> button : {back_, forward_, reload_}) {
    if (button) {
      button->SetVisible(!compact);
    }
  }
  if (menu_) {
    menu_->SetVisible(!compact);
  }
  if (panel_) {
    panel_->Layout();
  }
}

void Toolbar::OnFocus(CefRefPtr<CefView> view) {
  if (!omnibox_ || !view || !view->IsSame(omnibox_)) {
    return;
  }
  omnibox_focused_ = true;
  if (delegate_) {
    delegate_->OnOmniboxFocusChanged(true);
  }
}

void Toolbar::OnBlur(CefRefPtr<CefView> view) {
  if (!omnibox_ || !view || !view->IsSame(omnibox_)) {
    return;
  }
  omnibox_focused_ = false;
  if (delegate_) {
    delegate_->OnOmniboxFocusChanged(false);
  }
}

bool Toolbar::OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                         const CefKeyEvent& event) {
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  constexpr int kVkReturn = 0x0D;
  constexpr int kVkEscape = 0x1B;

  if (event.windows_key_code == kVkReturn) {
    if (delegate_ && textfield) {
      delegate_->OnNavigateRequested(textfield->GetText().ToString());
    }
    return true;
  }
  if (event.windows_key_code == kVkEscape) {
    // Escape abandons the edit and puts the real address back.
    if (textfield) {
      textfield->ClearSelection();
    }
    return false;
  }
  return false;
}

}  // namespace cad
