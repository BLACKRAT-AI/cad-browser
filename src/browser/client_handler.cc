#include "browser/client_handler.h"

#include "browser/json.h"
#include "browser/request_filter.h"
#include "browser/url_util.h"
#include "include/cef_menu_model.h"
#include "include/cef_parser.h"
#include "ipc/messages.h"

namespace cad {
namespace {

// Context menu command ids for the items we add. CEF reserves everything below
// MENU_ID_USER_FIRST for Chromium's own entries.
constexpr int kMenuOpenLinkInNewTab = MENU_ID_USER_FIRST + 1;
constexpr int kMenuCopyLinkAddress = MENU_ID_USER_FIRST + 2;

}  // namespace

ClientHandler::ClientHandler(Delegate* delegate, Blocklist* blocklist)
    : delegate_(delegate), blocklist_(blocklist) {}

void ClientHandler::DetachDelegate() {
  delegate_ = nullptr;
}

// ---------------------------------------------------------------------------
// CefLifeSpanHandler
// ---------------------------------------------------------------------------

void ClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

void ClientHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserClosing(browser);
  }
}

bool ClientHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                                  bool* no_javascript_access) {
  // Everything opens as a tab. A browser whose whole point is handling many
  // tabs well should not be spawning stray windows, and routing popups here
  // also means the blocklist and the stats overlay apply to them.
  if (delegate_) {
    const bool foreground =
        target_disposition != CEF_WOD_NEW_BACKGROUND_TAB;
    delegate_->OnOpenUrlInNewTab(target_url.ToString(), foreground);
  }
  return true;  // cancel the popup; we handled it
}

// ---------------------------------------------------------------------------
// CefLoadHandler
// ---------------------------------------------------------------------------

void ClientHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool is_loading,
                                         bool can_go_back,
                                         bool can_go_forward) {
  if (delegate_) {
    delegate_->OnLoadingStateChanged(is_loading, can_go_back, can_go_forward);
  }
}

void ClientHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode error_code,
                                const CefString& error_text,
                                const CefString& failed_url) {
  // Aborted loads are routine: they happen whenever the user navigates away
  // before a page finishes. Showing an error page for them would be wrong.
  if (error_code == ERR_ABORTED || !frame || !frame->IsMain()) {
    return;
  }

  const std::string url = failed_url.ToString();
  std::string body =
      "<!doctype html><meta charset=\"utf-8\"><title>Page not available</title>"
      "<style>body{font:13px 'Segoe UI',sans-serif;color:#1B1D1F;"
      "background:#F5F6F7;margin:0;padding:48px;}"
      "@media (prefers-color-scheme:dark){body{background:#1B1D1F;"
      "color:#E6E8EA;}}"
      "h1{font-size:16px;font-weight:600;margin:0 0 8px;}"
      "p{color:#6B7178;max-width:60ch;line-height:1.55;margin:0 0 6px;}"
      "code{font:12px 'Cascadia Mono',Consolas,monospace;}</style>"
      "<h1>This page could not be loaded</h1><p>";
  body += CefString(error_text).ToString();
  body += "</p><p><code>";
  body += url;
  body += "</code></p>";

  frame->LoadURL("data:text/html;charset=utf-8," +
                 CefURIEncode(body, false).ToString());
}

// ---------------------------------------------------------------------------
// CefDisplayHandler
// ---------------------------------------------------------------------------

void ClientHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  if (delegate_) {
    delegate_->OnTitleChanged(title.ToString());
  }
}

void ClientHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  // Subframe navigations must not rewrite the omnibox.
  if (frame && frame->IsMain() && delegate_) {
    delegate_->OnUrlChanged(url.ToString());
  }
}

void ClientHandler::OnFaviconURLChange(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefString>& icon_urls) {
  if (!delegate_) {
    return;
  }
  std::vector<std::string> urls;
  urls.reserve(icon_urls.size());
  for (const CefString& url : icon_urls) {
    urls.push_back(url.ToString());
  }
  delegate_->OnFaviconUrlsChanged(urls);
}

void ClientHandler::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                           bool fullscreen) {
  if (delegate_) {
    delegate_->OnFullscreenChanged(fullscreen);
  }
}

void ClientHandler::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                                            double progress) {
  if (delegate_) {
    delegate_->OnLoadProgress(progress);
  }
}

void ClientHandler::OnStatusMessage(CefRefPtr<CefBrowser> browser,
                                    const CefString& value) {
  if (delegate_) {
    delegate_->OnStatusMessage(value.ToString());
  }
}

// ---------------------------------------------------------------------------
// CefRequestHandler
// ---------------------------------------------------------------------------

CefRefPtr<CefResourceRequestHandler> ClientHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  if (!blocklist_) {
    return nullptr;
  }
  // Created once and reused: this is called for every request, and allocating
  // a handler per request would be pure overhead.
  if (!request_filter_) {
    request_filter_ = new RequestFilter(blocklist_);
  }
  return request_filter_;
}

void ClientHandler::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                              TerminationStatus status,
                                              int error_code,
                                              const CefString& error_string) {
  if (!delegate_) {
    return;
  }

  // Out of memory is the one worth naming. For a browser aimed at CAD work it
  // is the likely cause, it is not random, and the user can do something
  // about it, so saying "it crashed" would waste their time.
  const bool out_of_memory = status == TS_PROCESS_OOM;

  std::string reason;
  switch (status) {
    case TS_PROCESS_OOM:
      reason = "ran out of memory";
      break;
    case TS_PROCESS_CRASHED:
      reason = "crashed";
      break;
    case TS_PROCESS_WAS_KILLED:
      reason = "was ended by the system";
      break;
    case TS_LAUNCH_FAILED:
      reason = "could not be started";
      break;
    default:
      reason = "stopped unexpectedly";
      break;
  }
  if (!error_string.empty()) {
    reason += " (" + error_string.ToString() + ")";
  }

  delegate_->OnRenderProcessGone(reason, out_of_memory);
}

// ---------------------------------------------------------------------------
// CefContextMenuHandler
// ---------------------------------------------------------------------------

void ClientHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefContextMenuParams> params,
                                        CefRefPtr<CefMenuModel> model) {
  if (!params || !model) {
    return;
  }
  // Chromium's Alloy menu has no "open in new tab" because it has no concept
  // of our tabs, so we add it.
  if (!params->GetLinkUrl().empty()) {
    model->InsertItemAt(0, kMenuOpenLinkInNewTab, "Open link in new tab");
    model->InsertItemAt(1, kMenuCopyLinkAddress, "Copy link address");
    model->InsertSeparatorAt(2);
  }
}

bool ClientHandler::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    int command_id,
    EventFlags event_flags) {
  if (!params) {
    return false;
  }
  switch (command_id) {
    case kMenuOpenLinkInNewTab:
      if (delegate_) {
        delegate_->OnOpenUrlInNewTab(params->GetLinkUrl().ToString(), false);
      }
      return true;
    case kMenuCopyLinkAddress:
      if (frame) {
        // Chromium owns the clipboard; the simplest correct route is to let
        // the page's own copy path run in the renderer.
        // JsonEscape produces a quoted, escaped JavaScript string literal, so
        // a URL containing quotes cannot break out of the expression.
        const std::string script =
            "navigator.clipboard && navigator.clipboard.writeText(" +
            JsonEscape(params->GetLinkUrl().ToString()) + ")";
        frame->ExecuteJavaScript(script, frame->GetURL(), 0);
      }
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// CefDownloadHandler
// ---------------------------------------------------------------------------

bool ClientHandler::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!callback) {
    return false;
  }
  // Show the system save dialog. Returning false here would cancel the
  // download outright under Alloy style.
  callback->Continue(suggested_name, true);
  if (delegate_) {
    delegate_->OnDownloadStarted(suggested_name.ToString());
  }
  return true;
}

void ClientHandler::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  if (!download_item || !delegate_) {
    return;
  }
  if (download_item->IsComplete()) {
    delegate_->OnDownloadFinished(download_item->GetFullPath().ToString(), true);
  } else if (download_item->IsCanceled()) {
    delegate_->OnDownloadFinished(download_item->GetFullPath().ToString(),
                                  false);
  }
}

// ---------------------------------------------------------------------------
// Process messages
// ---------------------------------------------------------------------------

bool ClientHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  if (!message || !delegate_) {
    return false;
  }
  const std::string name = message->GetName().ToString();
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args) {
    return false;
  }

  if (name == ipc::kFpsReport && args->GetSize() >= 5) {
    delegate_->OnFrameStats(static_cast<uint32_t>(args->GetInt(0)),
                            args->GetDouble(1), args->GetDouble(2),
                            args->GetDouble(3), args->GetDouble(4));
    return true;
  }

  if (name == ipc::kCaptureResult && args->GetSize() >= 3) {
    const int request_id = args->GetInt(0);
    const bool ok = args->GetBool(1);
    if (!ok) {
      delegate_->OnPageCaptured(request_id, false,
                                args->GetString(2).ToString(), PageCapture());
      return true;
    }
    PageCapture page;
    page.url = args->GetString(2).ToString();
    if (args->GetSize() >= 7) {
      page.title = args->GetString(3).ToString();
      page.text = args->GetString(4).ToString();
      page.html = args->GetString(5).ToString();
      if (CefRefPtr<CefListValue> links = args->GetList(6)) {
        const size_t count = links->GetSize();
        page.links.reserve(count);
        for (size_t i = 0; i < count; ++i) {
          CefRefPtr<CefListValue> entry = links->GetList(i);
          if (entry && entry->GetSize() >= 2) {
            page.links.push_back({entry->GetString(0).ToString(),
                                  entry->GetString(1).ToString()});
          }
        }
      }
    }
    delegate_->OnPageCaptured(request_id, true, std::string(),
                              std::move(page));
    return true;
  }

  if (name == ipc::kUiEvent && args->GetSize() >= 2) {
    delegate_->OnUiMessage(args->GetString(0).ToString(),
                           args->GetString(1).ToString());
    return true;
  }

  return false;
}

}  // namespace cad
