// CAD Browser - one tab.
//
// A tab owns its CefBrowserView, or owns nothing at all when it is asleep. A
// discarded tab keeps only what the strip needs to draw it and what a restore
// needs to reload it, which is the whole point: PLAN.md section 6.4 restores a
// hundred-tab session at the cost of one renderer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "browser/client_handler.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"

namespace cad {

class TabManager;

class Tab : public ClientHandler::Delegate {
 public:
  Tab(int id, TabManager* manager, std::string url);
  ~Tab() override;
  Tab(const Tab&) = delete;
  Tab& operator=(const Tab&) = delete;

  int id() const { return id_; }
  const std::string& url() const { return url_; }
  const std::string& title() const { return title_; }
  const std::string& status_message() const { return status_message_; }
  // The title to draw: falls back to the host while a page is still loading.
  std::string display_title() const;

  bool is_loading() const { return is_loading_; }
  bool can_go_back() const { return can_go_back_; }
  bool can_go_forward() const { return can_go_forward_; }
  bool is_active() const { return is_active_; }
  bool is_discarded() const { return browser_view_ == nullptr; }
  bool is_audible() const { return is_audible_; }
  bool is_fullscreen() const { return is_fullscreen_; }

  bool pinned() const { return pinned_; }
  void SetPinned(bool pinned);
  // "Keep at full speed": exempt from discard, and kept visible to the
  // compositor even when another tab is in front. See PLAN.md section 6.3.
  bool performance_pinned() const { return performance_pinned_; }
  void SetPerformancePinned(bool pinned);

  bool downloading() const { return active_downloads_ > 0; }

  uint64_t created_ms() const { return created_ms_; }
  uint64_t last_active_ms() const { return last_active_ms_; }
  uint64_t idle_seconds() const;
  uint64_t age_seconds() const;
  uint32_t renderer_pid() const { return renderer_pid_; }

  CefRefPtr<CefBrowserView> browser_view() const { return browser_view_; }
  CefRefPtr<CefBrowser> browser() const;

  // Creates the BrowserView. The browser itself is created by CEF once the
  // view is added to a window, which TabManager does immediately afterwards.
  void Realize();
  // Destroys the browser and keeps url/title. Safe to call when discarded.
  void Discard();

  void SetActive(bool active);
  void LoadUrl(const std::string& url);
  void Reload(bool ignore_cache);
  void GoBack();
  void GoForward();
  void StopLoad();
  void ShowDevTools();
  void SetZoomLevel(double level);
  double GetZoomLevel() const;
  void Find(const std::string& text, bool forward);
  void StopFinding();
  void Print();
  // Sends the current frame-probe state to this tab's renderer.
  void SetFrameProbeEnabled(bool enabled);
  void SetSiteFocusEnabled(bool enabled);
  // Runs script in the main frame. Used to push data into cad:// pages.
  void ExecuteJavaScript(const std::string& script);
  // Asks the renderer for the page's content. The answer arrives through
  // TabManager::OnTabPageCaptured. False when there is no live page.
  bool RequestCapture(bool automatic);
  // Counts main-frame loads started, so auto-capture can tell a reload of the
  // same URL from a repeated notification about the page already saved.
  uint64_t load_count() const { return load_count_; }

  // ---- ClientHandler::Delegate ----
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnTitleChanged(const std::string& title) override;
  void OnUrlChanged(const std::string& url) override;
  void OnFaviconUrlsChanged(const std::vector<std::string>& urls) override;
  void OnLoadingStateChanged(bool is_loading,
                             bool can_go_back,
                             bool can_go_forward) override;
  void OnLoadProgress(double progress) override;
  void OnStatusMessage(const std::string& message) override;
  void OnFullscreenChanged(bool fullscreen) override;
  void OnOpenUrlInNewTab(const std::string& url, bool foreground) override;
  void OnUiMessage(const std::string& name,
                   const std::string& payload) override;
  void OnFrameStats(uint32_t renderer_pid,
                    double fps,
                    double p50_ms,
                    double p99_ms,
                    double longest_ms) override;
  void OnDownloadStarted(const std::string& file_name) override;
  void OnDownloadFinished(const std::string& file_name, bool success) override;
  void OnRenderProcessGone(const std::string& reason,
                           bool out_of_memory) override;
  void OnPageCaptured(int request_id,
                      bool ok,
                      const std::string& error,
                      PageCapture page) override;

  bool crashed() const { return crashed_; }

 private:
  const int id_;
  TabManager* const manager_;

  std::string url_;
  std::string title_;
  std::string status_message_;
  std::string pending_url_;  // set while discarded, loaded on restore

  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  bool is_active_ = false;
  bool is_audible_ = false;
  bool is_fullscreen_ = false;
  bool pinned_ = false;
  bool performance_pinned_ = false;
  bool crashed_ = false;
  int active_downloads_ = 0;
  uint64_t load_count_ = 0;
  int next_capture_id_ = 1;
  // Capture requests in flight: request id and whether it was automatic.
  std::vector<std::pair<int, bool>> pending_captures_;

  uint64_t created_ms_ = 0;
  uint64_t last_active_ms_ = 0;
  uint32_t renderer_pid_ = 0;

  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<ClientHandler> client_handler_;
  CefRefPtr<CefBrowserViewDelegate> view_delegate_;
};

}  // namespace cad
