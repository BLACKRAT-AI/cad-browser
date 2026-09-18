// CAD Browser - the tab strip, built from native Views.
//
// Native rather than HTML on purpose (PLAN.md section 1): an HTML tab strip
// costs an extra renderer process and its startup, for chrome that never
// changes. This is a CefPanel of buttons.
#pragma once

#include <string>
#include <vector>

#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "ui/theme.h"

namespace cad {

class Tab;

class TabStrip : public CefButtonDelegate {
 public:
  class Delegate {
   public:
    virtual void OnTabSelected(int tab_id) = 0;
    virtual void OnTabCloseRequested(int tab_id) = 0;
    virtual void OnNewTabRequested() = 0;

   protected:
    virtual ~Delegate() = default;
  };

  explicit TabStrip(Delegate* delegate);

  CefRefPtr<CefPanel> view() const { return panel_; }

  // Rebuilds from the current tab list. Called whenever tabs are added,
  // removed, reordered, or change title or state.
  void Rebuild(const std::vector<Tab*>& tabs,
               int active_tab_id,
               const Theme& theme);

  // Compact drops the per-tab close buttons and the new-tab button entirely
  // rather than greying them out, so the strip is only tab titles. They come
  // back when the pointer is actually over the chrome.
  void SetCompact(bool compact) { compact_ = compact; }
  // Which tab, if any, sits under a point given in screen coordinates.
  // Screen coordinates rather than local ones because the caller gets them
  // from a Win32 message and the strip lives inside an overlay, so mapping
  // between the two coordinate spaces by hand would be one more thing to get
  // wrong. Returns 0 when the point is not over a tab.
  int TabIdAtScreenPoint(int x, int y) const;
  bool compact() const { return compact_; }

  void DetachDelegate() { delegate_ = nullptr; }

  // CefButtonDelegate
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  // Tabs and the new-tab button light up under the pointer. Ink drops are
  // off, so this is the only hover feedback there is.
  void OnButtonStateChanged(CefRefPtr<CefButton> button) override;

 private:
  // View ids. A button carries its tab id, which is how a click finds its tab
  // without a side table that could fall out of step with the strip.
  static constexpr int kNewTabButtonId = 1;
  static constexpr int kTabButtonIdBase = 10000;
  static constexpr int kCloseButtonIdBase = 1000000;

  CefRefPtr<CefLabelButton> MakeTabButton(Tab* tab,
                                          bool active,
                                          int width,
                                          const Theme& theme);

  Delegate* delegate_ = nullptr;
  CefRefPtr<CefPanel> panel_;
  bool compact_ = true;
  // Kept so a hover can repaint without the caller handing the theme back.
  Theme theme_;
  int active_tab_id_ = -1;

  IMPLEMENT_REFCOUNTING(TabStrip);
  DISALLOW_COPY_AND_ASSIGN(TabStrip);
};

}  // namespace cad
