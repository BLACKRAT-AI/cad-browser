#include "browser/tab_manager.h"

#include <algorithm>

#include "app/app_state.h"
#include "browser/url_util.h"
#include "ui/main_window.h"

namespace cad {
namespace {

constexpr size_t kMaxClosedTabsRemembered = 25;

}  // namespace

TabManager::TabManager(MainWindow* window) : window_(window) {}

TabManager::~TabManager() = default;

Tab* TabManager::FindTab(int tab_id) const {
  for (const std::unique_ptr<Tab>& tab : tabs_) {
    if (tab->id() == tab_id) {
      return tab.get();
    }
  }
  return nullptr;
}

Tab* TabManager::active_tab() const {
  return FindTab(active_tab_id_);
}

int TabManager::IndexOf(int tab_id) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i]->id() == tab_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int TabManager::sleeping_count() const {
  int count = 0;
  for (const std::unique_ptr<Tab>& tab : tabs_) {
    if (tab->is_discarded()) {
      ++count;
    }
  }
  return count;
}

int TabManager::CreateTab(const std::string& url, bool foreground) {
  const int id = NextId();
  auto tab = std::make_unique<Tab>(id, this, url);
  Tab* raw = tab.get();
  tabs_.push_back(std::move(tab));

  raw->Realize();
  if (window_) {
    window_->OnTabAdded(raw);
  }
  if (foreground || tabs_.size() == 1) {
    ActivateTab(id);
  }
  if (window_) {
    window_->OnTabListChanged();
  }
  return id;
}

int TabManager::CreateSleepingTab(const std::string& url,
                                  const std::string& title,
                                  bool pinned) {
  const int id = NextId();
  auto tab = std::make_unique<Tab>(id, this, url);
  if (!title.empty()) {
    tab->OnTitleChanged(title);
  }
  tab->SetPinned(pinned);
  tabs_.push_back(std::move(tab));
  if (window_) {
    window_->OnTabListChanged();
  }
  return id;
}

void TabManager::RemoveTabAt(size_t index) {
  if (index >= tabs_.size()) {
    return;
  }
  Tab* tab = tabs_[index].get();

  ClosedTab closed;
  closed.url = tab->url();
  closed.title = tab->title();
  closed.index = static_cast<int>(index);
  closed.pinned = tab->pinned();
  if (!closed.url.empty() && !IsNewTabUrl(closed.url)) {
    closed_tabs_.push_back(std::move(closed));
    if (closed_tabs_.size() > kMaxClosedTabsRemembered) {
      closed_tabs_.erase(closed_tabs_.begin());
    }
  }

  if (window_) {
    window_->OnTabRemoved(tab);
  }
  tabs_.erase(tabs_.begin() + static_cast<long>(index));
}

void TabManager::CloseTab(int tab_id) {
  const int index = IndexOf(tab_id);
  if (index < 0) {
    return;
  }
  const bool was_active = tab_id == active_tab_id_;
  RemoveTabAt(static_cast<size_t>(index));

  if (tabs_.empty()) {
    // The last tab closing closes the window, matching every other browser.
    if (window_) {
      window_->OnLastTabClosed();
    }
    return;
  }

  if (was_active) {
    // Activate the tab that slid into this position, or the new last one.
    const int next =
        std::min(index, static_cast<int>(tabs_.size()) - 1);
    ActivateTabAtIndex(next);
  }
  if (window_) {
    window_->OnTabListChanged();
  }
}

void TabManager::CloseOtherTabs(int tab_id) {
  std::vector<int> doomed;
  for (const std::unique_ptr<Tab>& tab : tabs_) {
    if (tab->id() != tab_id) {
      doomed.push_back(tab->id());
    }
  }
  for (const int id : doomed) {
    CloseTab(id);
  }
}

void TabManager::CloseTabsToTheRight(int tab_id) {
  const int index = IndexOf(tab_id);
  if (index < 0) {
    return;
  }
  std::vector<int> doomed;
  for (size_t i = static_cast<size_t>(index) + 1; i < tabs_.size(); ++i) {
    doomed.push_back(tabs_[i]->id());
  }
  for (const int id : doomed) {
    CloseTab(id);
  }
}

bool TabManager::ReopenLastClosedTab() {
  if (closed_tabs_.empty()) {
    return false;
  }
  const ClosedTab closed = closed_tabs_.back();
  closed_tabs_.pop_back();

  const int id = CreateTab(closed.url, true);
  if (Tab* tab = FindTab(id)) {
    tab->SetPinned(closed.pinned);
  }
  MoveTab(id, closed.index);
  return true;
}

void TabManager::ActivateTab(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }

  if (active_tab_id_ != tab_id) {
    if (Tab* previous = FindTab(active_tab_id_)) {
      previous->SetActive(false);
    }
  }
  active_tab_id_ = tab_id;

  // A sleeping tab wakes when it is selected. This is the path the dimmed
  // strip entry takes when clicked.
  if (tab->is_discarded()) {
    RestoreTab(tab_id);
    tab = FindTab(tab_id);
    if (!tab) {
      return;
    }
  }

  tab->SetActive(true);
  if (AppState* state = AppState::Get()) {
    state->sampler().SetActiveRendererPid(tab->renderer_pid());
  }
  if (window_) {
    window_->OnActiveTabChanged(tab);
  }
}

void TabManager::ActivateTabAtIndex(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) {
    return;
  }
  ActivateTab(tabs_[static_cast<size_t>(index)]->id());
}

void TabManager::ActivateNextTab() {
  if (tabs_.empty()) {
    return;
  }
  const int index = IndexOf(active_tab_id_);
  const int next = (index + 1) % static_cast<int>(tabs_.size());
  ActivateTabAtIndex(next);
}

void TabManager::ActivatePreviousTab() {
  if (tabs_.empty()) {
    return;
  }
  const int count = static_cast<int>(tabs_.size());
  const int index = IndexOf(active_tab_id_);
  ActivateTabAtIndex((index - 1 + count) % count);
}

void TabManager::MoveTab(int tab_id, int new_index) {
  const int index = IndexOf(tab_id);
  if (index < 0 || tabs_.empty()) {
    return;
  }
  const int clamped =
      std::clamp(new_index, 0, static_cast<int>(tabs_.size()) - 1);
  if (clamped == index) {
    return;
  }
  std::unique_ptr<Tab> moving = std::move(tabs_[static_cast<size_t>(index)]);
  tabs_.erase(tabs_.begin() + index);
  tabs_.insert(tabs_.begin() + clamped, std::move(moving));
  if (window_) {
    window_->OnTabListChanged();
  }
}

void TabManager::DiscardTab(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab || tab->is_discarded() || tab->id() == active_tab_id_) {
    return;
  }
  if (window_) {
    window_->OnTabRemoved(tab);  // detach the view before destroying it
  }
  tab->Discard();
  if (window_) {
    window_->OnTabListChanged();
  }
}

void TabManager::RestoreTab(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab || !tab->is_discarded()) {
    return;
  }
  tab->Realize();
  if (window_) {
    window_->OnTabAdded(tab);
    window_->OnTabListChanged();
  }
}

DiscardThresholds TabManager::CurrentThresholds() const {
  DiscardThresholds thresholds;
  if (AppState* state = AppState::Get()) {
    const Settings& settings = state->settings();
    thresholds.available_fraction_trigger = settings.discard_available_fraction;
    thresholds.browser_rss_fraction_trigger = settings.discard_browser_fraction;
    thresholds.idle_seconds_trigger =
        static_cast<uint64_t>(settings.discard_idle_minutes) * 60;
    thresholds.idle_tab_count_trigger = settings.discard_tab_count;
  }
  return thresholds;
}

void TabManager::RunDiscardSweep() {
  AppState* state = AppState::Get();
  if (!state || !state->settings().discard_enabled) {
    return;
  }

  const sys::StatsSnapshot stats = state->sampler().LastSnapshot();
  const sys::MemoryDynamic memory = sys::QueryMemoryDynamic();

  DiscardInputs inputs;
  inputs.system_usable_bytes = memory.total_physical_bytes;
  inputs.system_available_bytes = memory.available_physical_bytes;
  inputs.browser_rss_bytes = stats.memory.browser_total_bytes;

  for (const std::unique_ptr<Tab>& tab : tabs_) {
    DiscardCandidate candidate;
    candidate.tab_id = tab->id();
    candidate.is_active = tab->id() == active_tab_id_;
    candidate.pinned = tab->pinned();
    candidate.audible = tab->is_audible();
    candidate.performance_pinned = tab->performance_pinned();
    candidate.downloading = tab->downloading();
    candidate.already_discarded = tab->is_discarded();
    candidate.idle_seconds = tab->idle_seconds();
    candidate.age_seconds = tab->age_seconds();
    // Attributing renderer memory needs the pid the renderer reported. Without
    // it the policy still works, it just cannot weigh this tab by size.
    candidate.renderer_bytes = 0;
    inputs.tabs.push_back(candidate);
  }

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, CurrentThresholds());
  for (const int id : decision.tab_ids) {
    DiscardTab(id);
  }
}

// ---------------------------------------------------------------------------
// Tab callbacks
// ---------------------------------------------------------------------------

void TabManager::OnTabBrowserCreated(int tab_id) {
  Tab* tab = FindTab(tab_id);
  if (!tab) {
    return;
  }
  if (AppState* state = AppState::Get()) {
    tab->SetFrameProbeEnabled(state->frame_probe_should_run());
    // Covers a renderer started after the setting was changed, which would
    // otherwise carry the value the browser started with.
    tab->SetSiteFocusEnabled(state->settings().onshape_focus_mode);
  }
  if (window_) {
    window_->OnTabListChanged();
  }
}

void TabManager::OnTabChanged(int tab_id) {
  if (window_) {
    window_->OnTabStateChanged(FindTab(tab_id));
  }
}

void TabManager::OnTabFaviconChanged(int tab_id,
                                     const std::vector<std::string>& urls) {
  if (window_) {
    window_->OnTabFaviconChanged(FindTab(tab_id), urls);
  }
}

void TabManager::OnTabFullscreenChanged(int tab_id, bool fullscreen) {
  if (window_ && tab_id == active_tab_id_) {
    window_->OnFullscreenChanged(fullscreen);
  }
}

void TabManager::OnTabUiMessage(int tab_id,
                                const std::string& name,
                                const std::string& payload) {
  if (window_) {
    window_->OnUiMessage(FindTab(tab_id), name, payload);
  }
}

void TabManager::OnTabPageSettled(int tab_id) {
  if (window_) {
    window_->OnTabPageSettled(tab_id);
  }
}

void TabManager::OnTabPageCaptured(int tab_id,
                                   bool automatic,
                                   bool ok,
                                   const std::string& error,
                                   PageCapture page) {
  if (window_) {
    window_->OnPageCaptured(FindTab(tab_id), automatic, ok, error,
                            std::move(page));
  }
}

void TabManager::SetFrameProbeEnabled(bool enabled) {
  for (const std::unique_ptr<Tab>& tab : tabs_) {
    tab->SetFrameProbeEnabled(enabled);
  }
}

void TabManager::SetSiteFocusEnabled(bool enabled) {
  for (const std::unique_ptr<Tab>& tab : tabs_) {
    tab->SetSiteFocusEnabled(enabled);
  }
}

}  // namespace cad
