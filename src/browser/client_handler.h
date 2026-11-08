// CAD Browser - one CefClient per tab.
//
// A client per tab rather than one shared client keeps the routing trivial:
// every callback already knows which tab it belongs to, with no browser-id
// lookup table to keep in step with tab creation and destruction.
//
// The tab detaches itself before it is destroyed, so a late callback from a
// browser that is still shutting down finds a null delegate rather than a
// dangling one.
#pragma once

#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"

#include "browser/page_capture.h"

namespace cad {

class Blocklist;

class ClientHandler : public CefClient,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefDisplayHandler,
                      public CefRequestHandler,
                      public CefContextMenuHandler,
                      public CefDownloadHandler,
                      public CefFocusHandler {
 public:
  class Delegate {
   public:
    virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnBrowserClosing(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnTitleChanged(const std::string& title) = 0;
    virtual void OnUrlChanged(const std::string& url) = 0;
    virtual void OnFaviconUrlsChanged(const std::vector<std::string>& urls) = 0;
    virtual void OnLoadingStateChanged(bool is_loading,
                                       bool can_go_back,
                                       bool can_go_forward) = 0;
    virtual void OnLoadProgress(double progress) = 0;
    virtual void OnStatusMessage(const std::string& message) = 0;
    virtual void OnFullscreenChanged(bool fullscreen) = 0;
    virtual void OnOpenUrlInNewTab(const std::string& url, bool foreground) = 0;
    // A cad:// page called window.cadBrowser.send(name, payload).
    virtual void OnUiMessage(const std::string& name,
                             const std::string& payload) = 0;
    virtual void OnFrameStats(uint32_t renderer_pid,
                              double fps,
                              double p50_ms,
                              double p99_ms,
                              double longest_ms) = 0;
    virtual void OnDownloadStarted(const std::string& file_name) = 0;
    virtual void OnDownloadFinished(const std::string& file_name,
                                    bool success) = 0;
    // The tab's renderer died. Without this a crashed tab just goes blank.
    virtual void OnRenderProcessGone(const std::string& reason,
                                     bool out_of_memory) = 0;
    // The renderer answered a capture request. `page` is filled only when
    // `ok`; otherwise `error` says why the page could not be read.
    virtual void OnPageCaptured(int request_id,
                                bool ok,
                                const std::string& error,
                                PageCapture page) {}

   protected:
    virtual ~Delegate() = default;
  };

  ClientHandler(Delegate* delegate, Blocklist* blocklist);

  // Called by the tab before it is destroyed.
  void DetachDelegate();

  // ---- CefClient ----
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // ---- CefLifeSpanHandler ----
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     cef_window_open_disposition_t target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  // ---- CefLoadHandler ----
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;

  // ---- CefDisplayHandler ----
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;
  void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                               double progress) override;
  void OnStatusMessage(CefRefPtr<CefBrowser> browser,
                       const CefString& value) override;

  // ---- CefRequestHandler ----
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

  // ---- CefContextMenuHandler ----
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;

  // ---- CefDownloadHandler ----
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

 private:
  Delegate* delegate_ = nullptr;
  Blocklist* const blocklist_;
  CefRefPtr<CefResourceRequestHandler> request_filter_;

  IMPLEMENT_REFCOUNTING(ClientHandler);
  DISALLOW_COPY_AND_ASSIGN(ClientHandler);
};

}  // namespace cad
