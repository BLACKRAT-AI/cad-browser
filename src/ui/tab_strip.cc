#include "ui/tab_strip.h"

#include <algorithm>

#include "browser/tab.h"
#include "include/views/cef_box_layout.h"

namespace cad {
namespace {

// Truncates a title to fit the tab width, in characters rather than pixels.
// Views has no text-measuring API exposed to clients, so this is an estimate
// tuned to Segoe UI at 12px; a slightly short label reads better than one the
// widget clips mid-glyph.
std::string ElideTitle(const std::string& title, int width_dip) {
  const size_t budget = static_cast<size_t>(std::max(4, (width_dip - 34) / 7));
  if (title.size() <= budget) {
    return title;
  }
  if (budget <= 1) {
    return "...";
  }
  return title.substr(0, budget - 1) + "...";
}

}  // namespace

TabStrip::TabStrip(Delegate* delegate) : delegate_(delegate) {
  panel_ = CefPanel::CreatePanel(nullptr);

  CefBoxLayoutSettings settings;
  settings.horizontal = 1;
  settings.between_child_spacing = 1;
  settings.inside_border_horizontal_spacing = 4;
  settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  panel_->SetToBoxLayout(settings);
}

CefRefPtr<CefLabelButton> TabStrip::MakeTabButton(Tab* tab,
                                                  bool active,
                                                  int width,
                                                  const Theme& theme) {
  std::string label;
  if (tab->pinned()) {
    // A pinned tab shows no text, only its state.
    label = tab->is_discarded() ? icons::kSleeping : std::string();
  } else {
    label = ElideTitle(tab->display_title(), width);
  }

  CefRefPtr<CefLabelButton> button =
      CefLabelButton::CreateLabelButton(this, label);
  button->SetID(kTabButtonIdBase + tab->id());
  button->SetInkDropEnabled(false);
  button->SetFocusable(false);
  button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
  button->SetFontList(UiFontList(12));
  button->SetMinimumSize(CefSize(width, metrics::kTabStripHeight));
  button->SetMaximumSize(CefSize(width, metrics::kTabStripHeight));
  button->SetBackgroundColor(active ? theme.tab_active : theme.tab_inactive);

  // A sleeping tab is drawn muted so the strip shows at a glance what is
  // actually resident.
  const cef_color_t text_color =
      tab->is_discarded() ? theme.text_muted : theme.text;
  button->SetEnabledTextColors(active ? theme.text : text_color);

  std::string tooltip = tab->display_title();
  if (!tab->url().empty()) {
    tooltip += "\n" + tab->url();
  }
  if (tab->is_discarded()) {
    tooltip += "\nSleeping - click to reload";
  }
  if (tab->performance_pinned()) {
    tooltip += "\nKept at full speed";
  }
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tab->display_title());
  return button;
}

void TabStrip::Rebuild(const std::vector<Tab*>& tabs,
                       int active_tab_id,
                       const Theme& theme) {
  if (!panel_) {
    return;
  }
  theme_ = theme;
  active_tab_id_ = active_tab_id;
  panel_->RemoveAllChildViews();
  // The strip sits inside the floating chrome bar, so it takes the bar's own
  // surface. Painting the app background here left the top half of the bar a
  // flat opaque black while the toolbar below it was the intended grey.
  panel_->SetBackgroundColor(theme.chrome_surface);

  if (tabs.empty()) {
    panel_->Layout();
    return;
  }

  // Share the available width, shrinking tabs as the count grows, exactly as
  // PLAN.md section 6.2 describes. Pinned tabs keep a fixed narrow width.
  const int panel_width = std::max(panel_->GetSize().width, 400);
  int pinned_count = 0;
  for (const Tab* tab : tabs) {
    if (tab->pinned()) {
      ++pinned_count;
    }
  }
  const int unpinned_count = static_cast<int>(tabs.size()) - pinned_count;
  const int reserved =
      pinned_count * metrics::kTabPinnedWidth +
      (compact_ ? 0 : metrics::kToolbarButtonWidth) + 16;
  int per_tab = metrics::kTabMaxWidth;
  if (unpinned_count > 0) {
    per_tab = std::clamp((panel_width - reserved) / unpinned_count,
                         metrics::kTabMinWidth, metrics::kTabMaxWidth);
  }

  for (Tab* tab : tabs) {
    const bool active = tab->id() == active_tab_id;
    const int width = tab->pinned() ? metrics::kTabPinnedWidth : per_tab;
    panel_->AddChildView(MakeTabButton(tab, active, width, theme));

    // The close affordance appears only when the chrome is fully revealed and
    // there is room for it; below that width the tab is already down to a few
    // characters.
    if (!compact_ && !tab->pinned() && width >= 90) {
      CefRefPtr<CefLabelButton> close =
          CefLabelButton::CreateLabelButton(this, icons::kClose);
      close->SetID(kCloseButtonIdBase + tab->id());
      close->SetInkDropEnabled(false);
      close->SetFocusable(false);
      close->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
      close->SetFontList(UiFontList(14));
      close->SetMinimumSize(CefSize(24, metrics::kTabStripHeight));
      close->SetMaximumSize(CefSize(22, metrics::kTabStripHeight));
      close->SetBackgroundColor(active ? theme.tab_active : theme.tab_inactive);
      close->SetEnabledTextColors(theme.text_muted);
      close->SetTooltipText("Close tab");
      close->SetAccessibleName("Close " + tab->display_title());
      panel_->AddChildView(close);
    }
  }

  if (compact_) {
    panel_->Layout();
    return;
  }

  CefRefPtr<CefLabelButton> new_tab =
      CefLabelButton::CreateLabelButton(this, icons::kAdd);
  new_tab->SetID(kNewTabButtonId);
  new_tab->SetInkDropEnabled(false);
  new_tab->SetFocusable(false);
  new_tab->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
  new_tab->SetFontList(UiFontList(17));
  new_tab->SetMinimumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kTabStripHeight));
  new_tab->SetMaximumSize(
      CefSize(metrics::kToolbarButtonWidth, metrics::kTabStripHeight));
  new_tab->SetBackgroundColor(theme.chrome_surface);
  new_tab->SetEnabledTextColors(theme.text_muted);
  new_tab->SetTooltipText("New tab (Ctrl+T)");
  new_tab->SetAccessibleName("New tab");
  panel_->AddChildView(new_tab);

  panel_->Layout();
}

void TabStrip::OnButtonStateChanged(CefRefPtr<CefButton> button) {
  if (!button) {
    return;
  }
  // The active tab keeps its own fill whatever the pointer does; moving it
  // under the pointer would make the selection ambiguous.
  const bool active = button->GetID() == active_tab_id_;
  if (active) {
    button->SetBackgroundColor(theme_.tab_active);
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
      button->SetBackgroundColor(theme_.tab_inactive);
      break;
  }
}

int TabStrip::TabIdAtScreenPoint(int x, int y) const {
  if (!panel_) {
    return 0;
  }
  for (size_t i = 0; i < panel_->GetChildViewCount(); ++i) {
    CefRefPtr<CefView> child = panel_->GetChildViewAt(static_cast<int>(i));
    if (!child || !child->IsVisible()) {
      continue;
    }
    const int id = child->GetID();
    if (id < kTabButtonIdBase || id >= kCloseButtonIdBase) {
      continue;  // the new-tab button and the per-tab close buttons
    }
    const CefRect bounds = child->GetBoundsInScreen();
    if (x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y &&
        y < bounds.y + bounds.height) {
      return id - kTabButtonIdBase;
    }
  }
  return 0;
}

void TabStrip::OnButtonPressed(CefRefPtr<CefButton> button) {
  if (!delegate_ || !button) {
    return;
  }
  const int id = button->GetID();
  if (id == kNewTabButtonId) {
    delegate_->OnNewTabRequested();
  } else if (id >= kCloseButtonIdBase) {
    delegate_->OnTabCloseRequested(id - kCloseButtonIdBase);
  } else if (id >= kTabButtonIdBase) {
    delegate_->OnTabSelected(id - kTabButtonIdBase);
  }
}

}  // namespace cad
