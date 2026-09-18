// CAD Browser - back, forward, reload, omnibox, hamburger.
//
// Native Views, like the tab strip. The omnibox is a CefTextfield: pressing
// Enter hands the text to the delegate, which decides whether it is a URL or a
// search (see browser/url_util.h).
#pragma once

#include <string>

#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "ui/theme.h"

namespace cad {

class Toolbar : public CefMenuButtonDelegate, public CefTextfieldDelegate {
 public:
  class Delegate {
   public:
    virtual void OnBackRequested() = 0;
    virtual void OnForwardRequested() = 0;
    // Reload, or stop when the page is still loading.
    virtual void OnReloadOrStopRequested() = 0;
    virtual void OnNavigateRequested(const std::string& input) = 0;
    virtual void OnMenuRequested(
        CefRefPtr<CefMenuButton> button,
        const CefPoint& screen_point,
        CefRefPtr<CefMenuButtonPressedLock> pressed_lock) = 0;
    // The address bar gained or lost keyboard focus.
    virtual void OnOmniboxFocusChanged(bool focused) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit Toolbar(Delegate* delegate);

  CefRefPtr<CefPanel> view() const { return panel_; }

  void ApplyTheme(const Theme& theme);
  // Reflects the active tab: address, loading state, history availability.
  void UpdateState(const std::string& url,
                   bool is_loading,
                   bool can_go_back,
                   bool can_go_forward);
  void FocusOmnibox();
  std::string omnibox_text() const;

  // Compact hides the navigation and menu buttons, leaving only the address.
  // "Almost all buttons removed" is the point: they come back when the
  // pointer is actually over the chrome.
  void SetCompact(bool compact);
  bool compact() const { return compact_; }

  // True while the address bar has keyboard focus. The chrome stays revealed
  // for as long as it does, or typing a URL would dismiss the thing being
  // typed into.
  bool omnibox_focused() const { return omnibox_focused_; }

  void DetachDelegate() { delegate_ = nullptr; }

  // CefButtonDelegate
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  // Views ink drops are off for these buttons, so the hover and pressed
  // fills are painted here instead. Without it a button looks identical
  // whether the pointer is over it or not, which reads as broken.
  void OnButtonStateChanged(CefRefPtr<CefButton> button) override;
  // CefMenuButtonDelegate
  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> menu_button,
      const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override;
  // CefTextfieldDelegate
  bool OnKeyEvent(CefRefPtr<CefTextfield> textfield,
                  const CefKeyEvent& event) override;
  // CefViewDelegate, which CefTextfieldDelegate extends. These are the only
  // focus notifications Views gives a client for a textfield.
  void OnFocus(CefRefPtr<CefView> view) override;
  void OnBlur(CefRefPtr<CefView> view) override;

 private:
  static constexpr int kBackButtonId = 11;
  static constexpr int kForwardButtonId = 12;
  static constexpr int kReloadButtonId = 13;

  CefRefPtr<CefLabelButton> MakeToolButton(int id,
                                           const std::string& label,
                                           const std::string& tooltip);

  Delegate* delegate_ = nullptr;
  CefRefPtr<CefPanel> panel_;
  CefRefPtr<CefLabelButton> back_;
  CefRefPtr<CefLabelButton> forward_;
  CefRefPtr<CefLabelButton> reload_;
  CefRefPtr<CefTextfield> omnibox_;
  CefRefPtr<CefMenuButton> menu_;
  bool is_loading_ = false;
  bool compact_ = true;
  bool omnibox_focused_ = false;
  // Kept so a state change can repaint without the caller passing it in.
  Theme theme_;
  // True while UpdateState is writing the address, so the user's own edits are
  // never overwritten mid-typing.
  bool suppress_omnibox_update_ = false;

  IMPLEMENT_REFCOUNTING(Toolbar);
  DISALLOW_COPY_AND_ASSIGN(Toolbar);
};

}  // namespace cad
