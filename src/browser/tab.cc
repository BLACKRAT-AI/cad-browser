#include "browser/tab.h"

#include <chrono>

#include "app/app_state.h"
#include "browser/tab_manager.h"
#include "browser/url_util.h"
#include "include/cef_browser.h"
#include "include/cef_parser.h"
#include "ipc/messages.h"

namespace cad {
namespace {

uint64_t NowMs() {
  using clock = std::chrono::steady_clock;
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

// Every BrowserView in this browser is Alloy style with no Chrome toolbar:
// CAD Browser draws all of its own chrome.
class TabBrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  TabBrowserViewDelegate() = default;

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  ChromeToolbarType GetChromeToolbarType(
      CefRefPtr<CefBrowserView> browser_view) override {
    return CEF_CTT_NONE;
  }

 private:
  IMPLEMENT_REFCOUNTING(TabBrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(TabBrowserViewDelegate);
};

}  // namespace

Tab::Tab(int id, TabManager* manager, std::string url)
    : id_(id), manager_(manager), url_(std::move(url)) {
  created_ms_ = NowMs();
  last_active_ms_ = created_ms_;
  pending_url_ = url_;
  title_ = FallbackTitleForUrl(url_);
}

Tab::~Tab() {
  if (client_handler_) {
    client_handler_->DetachDelegate();
  }
  Discard();
}

std::string Tab::display_title() const {
  if (!title_.empty()) {
    return title_;
  }
  return FallbackTitleForUrl(url_);
}

uint64_t Tab::idle_seconds() const {
  if (is_active_) {
    return 0;
  }
  const uint64_t now = NowMs();
  return now > last_active_ms_ ? (now - last_active_ms_) / 1000 : 0;
}

uint64_t Tab::age_seconds() const {
  const uint64_t now = NowMs();
  return now > created_ms_ ? (now - created_ms_) / 1000 : 0;
}

CefRefPtr<CefBrowser> Tab::browser() const {
  return browser_view_ ? browser_view_->GetBrowser() : nullptr;
}

void Tab::Realize() {
  if (browser_view_) {
    return;
  }
  crashed_ = false;

  AppState* state = AppState::Get();
  client_handler_ =
      new ClientHandler(this, state ? &state->blocklist() : nullptr);
  view_delegate_ = new TabBrowserViewDelegate();

  CefBrowserSettings settings;
  // Leave the rest at CEF defaults; anything we force here would be a
  // per-tab override of a global policy that belongs in switches.cc.

  const std::string load_url = pending_url_.empty() ? url_ : pending_url_;
  browser_view_ = CefBrowserView::CreateBrowserView(
      client_handler_, load_url, settings, /*extra_info=*/nullptr,
      /*request_context=*/nullptr, view_delegate_);
  pending_url_.clear();
}

void Tab::Discard() {
  if (!browser_view_) {
    return;
  }
  // Keep enough to redraw the strip entry and to reload on demand.
  pending_url_ = url_;

  if (CefRefPtr<CefBrowser> current = browser()) {
    if (CefRefPtr<CefBrowserHost> host = current->GetHost()) {
      host->CloseBrowser(/*force_close=*/true);
    }
  }
  if (client_handler_) {
    client_handler_->DetachDelegate();
    client_handler_ = nullptr;
  }
  browser_view_ = nullptr;
  view_delegate_ = nullptr;
  renderer_pid_ = 0;
  is_loading_ = false;
  can_go_back_ = false;
  can_go_forward_ = false;
}

void Tab::SetActive(bool active) {
  is_active_ = active;
  if (active) {
    last_active_ms_ = NowMs();
  }

  if (CefRefPtr<CefBrowser> current = browser()) {
    if (CefRefPtr<CefBrowserHost> host = current->GetHost()) {
      // A performance-pinned tab stays "visible" to Chromium even in the
      // background, which keeps its compositor and rAF loop alive. It still
      // runs at background process priority; see docs/stats.md.
      const bool hidden = !active && !performance_pinned_;
      host->WasHidden(hidden);
      if (active) {
        host->SetFocus(true);
      }
    }
  }
}

void Tab::SetPinned(bool pinned) {
  pinned_ = pinned;
}

void Tab::SetPerformancePinned(bool pinned) {
  performance_pinned_ = pinned;
  // Apply immediately so the tab stops being throttled without a tab switch.
  if (!is_active_) {
    if (CefRefPtr<CefBrowser> current = browser()) {
      if (CefRefPtr<CefBrowserHost> host = current->GetHost()) {
        host->WasHidden(!pinned);
      }
    }
  }
}

void Tab::LoadUrl(const std::string& url) {
  if (url.empty()) {
    return;
  }
  url_ = url;
  if (is_discarded()) {
    pending_url_ = url;
    if (manager_) {
      manager_->RestoreTab(id_);
    }
    return;
  }
  if (CefRefPtr<CefBrowser> current = browser()) {
    if (CefRefPtr<CefFrame> frame = current->GetMainFrame()) {
      frame->LoadURL(url);
    }
  }
}

void Tab::Reload(bool ignore_cache) {
  if (is_discarded()) {
    if (manager_) {
      manager_->RestoreTab(id_);
    }
    return;
  }
  if (CefRefPtr<CefBrowser> current = browser()) {
    if (ignore_cache) {
      current->ReloadIgnoreCache();
    } else {
      current->Reload();
    }
  }
}

void Tab::GoBack() {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GoBack();
  }
}

void Tab::GoForward() {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GoForward();
  }
}

void Tab::StopLoad() {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->StopLoad();
  }
}

void Tab::ShowDevTools() {
  CefRefPtr<CefBrowser> current = browser();
  if (!current) {
    return;
  }
  CefWindowInfo window_info;
  CefBrowserSettings settings;
  window_info.SetAsPopup(nullptr, "DevTools");
  current->GetHost()->ShowDevTools(window_info, nullptr, settings, CefPoint());
}

void Tab::SetZoomLevel(double level) {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GetHost()->SetZoomLevel(level);
  }
}

double Tab::GetZoomLevel() const {
  if (CefRefPtr<CefBrowser> current = browser()) {
    return current->GetHost()->GetZoomLevel();
  }
  return 0.0;
}

void Tab::Find(const std::string& text, bool forward) {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GetHost()->Find(text, forward, /*matchCase=*/false,
                             /*findNext=*/true);
  }
}

void Tab::StopFinding() {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GetHost()->StopFinding(/*clearSelection=*/true);
  }
}

void Tab::Print() {
  if (CefRefPtr<CefBrowser> current = browser()) {
    current->GetHost()->Print();
  }
}

void Tab::SetSiteFocusEnabled(bool enabled) {
  CefRefPtr<CefBrowser> current = browser();
  if (!current) {
    return;
  }
  CefRefPtr<CefFrame> frame = current->GetMainFrame();
  if (!frame) {
    return;
  }
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(ipc::kSetSiteFocus);
  message->GetArgumentList()->SetBool(0, enabled);
  frame->SendProcessMessage(PID_RENDERER, message);
}

void Tab::SetFrameProbeEnabled(bool enabled) {
  CefRefPtr<CefBrowser> current = browser();
  if (!current) {
    return;
  }
  CefRefPtr<CefFrame> frame = current->GetMainFrame();
  if (!frame) {
    return;
  }
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(ipc::kSetProbeEnabled);
  message->GetArgumentList()->SetBool(0, enabled);
  frame->SendProcessMessage(PID_RENDERER, message);
}

void Tab::ExecuteJavaScript(const std::string& script) {
  if (CefRefPtr<CefBrowser> current = browser()) {
    if (CefRefPtr<CefFrame> frame = current->GetMainFrame()) {
      frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    }
  }
}

bool Tab::RequestCapture(bool automatic) {
  CefRefPtr<CefBrowser> current = browser();
  if (!current) {
    return false;
  }
  CefRefPtr<CefFrame> frame = current->GetMainFrame();
  if (!frame) {
    return false;
  }
  const int request_id = next_capture_id_++;
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(ipc::kCaptureRequest);
  message->GetArgumentList()->SetInt(0, request_id);
  frame->SendProcessMessage(PID_RENDERER, message);
  // A renderer that dies or navigates away never answers; keep the list short.
  if (pending_captures_.size() > 16) {
    pending_captures_.erase(pending_captures_.begin());
  }
  pending_captures_.emplace_back(request_id, automatic);
  return true;
}

// ---------------------------------------------------------------------------
// ClientHandler::Delegate
// ---------------------------------------------------------------------------

void Tab::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  // The renderer process id is learned from the renderer itself, with its
  // first frame-timing message. CEF exposes no browser-side call for it, and
  // guessing from the process tree would mis-attribute memory whenever two
  // tabs share a renderer.
  if (manager_) {
    manager_->OnTabBrowserCreated(id_);
  }
}

void Tab::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  renderer_pid_ = 0;
}

void Tab::OnTitleChanged(const std::string& title) {
  title_ = title;
  if (manager_) {
    manager_->OnTabChanged(id_);
  }
}

void Tab::OnUrlChanged(const std::string& url) {
  url_ = url;
  if (manager_) {
    manager_->OnTabChanged(id_);
    // A single-page app changes its URL without a load; this is the only
    // signal auto-capture gets for it.
    manager_->OnTabPageSettled(id_);
  }
}

void Tab::OnFaviconUrlsChanged(const std::vector<std::string>& urls) {
  if (manager_) {
    manager_->OnTabFaviconChanged(id_, urls);
  }
}

void Tab::OnLoadingStateChanged(bool is_loading,
                                bool can_go_back,
                                bool can_go_forward) {
  if (is_loading && !is_loading_) {
    ++load_count_;
  }
  const bool finished = is_loading_ && !is_loading;
  is_loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
  if (manager_) {
    manager_->OnTabChanged(id_);
    if (finished) {
      manager_->OnTabPageSettled(id_);
    }
  }
}

void Tab::OnLoadProgress(double progress) {}

void Tab::OnStatusMessage(const std::string& message) {
  status_message_ = message;
}

void Tab::OnFullscreenChanged(bool fullscreen) {
  is_fullscreen_ = fullscreen;
  if (manager_) {
    manager_->OnTabFullscreenChanged(id_, fullscreen);
  }
}

void Tab::OnOpenUrlInNewTab(const std::string& url, bool foreground) {
  if (manager_) {
    manager_->CreateTab(url, foreground);
  }
}

void Tab::OnUiMessage(const std::string& name, const std::string& payload) {
  if (manager_) {
    manager_->OnTabUiMessage(id_, name, payload);
  }
}

void Tab::OnFrameStats(uint32_t renderer_pid,
                       double fps,
                       double p50_ms,
                       double p99_ms,
                       double longest_ms) {
  renderer_pid_ = renderer_pid;
  if (AppState* state = AppState::Get()) {
    state->sampler().ReportFrameStats(renderer_pid, fps, p50_ms, p99_ms,
                                      longest_ms);
    if (is_active_) {
      state->sampler().SetActiveRendererPid(renderer_pid);
    }
  }
}

void Tab::OnRenderProcessGone(const std::string& reason, bool out_of_memory) {
  crashed_ = true;
  renderer_pid_ = 0;
  is_loading_ = false;

  // The tab is blank at this point. Left alone it stays blank forever, which
  // reads as the browser having broken rather than the page having died, so
  // put something in it that says what happened and offers a way back.
  std::string body =
      "<!doctype html><meta charset=\"utf-8\"><title>Page stopped</title>"
      "<style>body{font:13px 'Segoe UI',sans-serif;color:#E6E8EA;"
      "background:#1B1D1F;margin:0;padding:64px;}"
      "@media (prefers-color-scheme:light){body{background:#F5F6F7;"
      "color:#1B1D1F;}}"
      "h1{font-size:17px;font-weight:600;margin:0 0 10px;}"
      "p{color:#9AA0A6;max-width:66ch;line-height:1.6;margin:0 0 12px;}"
      "button{font:13px 'Segoe UI',sans-serif;color:#fff;background:#2F6FED;"
      "border:0;border-radius:4px;padding:8px 16px;cursor:pointer;}"
      "</style><h1>This page stopped running</h1><p>The page ";
  body += reason;
  body += ".</p>";

  if (out_of_memory) {
    body +=
        "<p>It used more JavaScript memory than one tab is allowed. Large "
        "CAD assemblies do this. The limit is set by the browser engine at "
        "roughly 4 GB per tab and cannot be raised from here, whatever else "
        "this machine has free.</p>"
        "<p>Reloading may work if you open a smaller assembly, hide "
        "components you are not using, or close other tabs first.</p>";
  }

  body +=
      "<p><button onclick=\"location.reload()\">Reload this page</button></p>";

  if (CefRefPtr<CefBrowser> current = browser()) {
    if (CefRefPtr<CefFrame> frame = current->GetMainFrame()) {
      frame->LoadURL("data:text/html;charset=utf-8," +
                     CefURIEncode(body, false).ToString());
    }
  }

  if (manager_) {
    manager_->OnTabChanged(id_);
  }
}

void Tab::OnPageCaptured(int request_id,
                         bool ok,
                         const std::string& error,
                         PageCapture page) {
  bool automatic = false;
  bool known = false;
  for (auto it = pending_captures_.begin(); it != pending_captures_.end();
       ++it) {
    if (it->first == request_id) {
      automatic = it->second;
      known = true;
      pending_captures_.erase(it);
      break;
    }
  }
  if (known && manager_) {
    manager_->OnTabPageCaptured(id_, automatic, ok, error, std::move(page));
  }
}

void Tab::OnDownloadStarted(const std::string& file_name) {
  ++active_downloads_;
}

void Tab::OnDownloadFinished(const std::string& file_name, bool success) {
  if (active_downloads_ > 0) {
    --active_downloads_;
  }
}

}  // namespace cad
