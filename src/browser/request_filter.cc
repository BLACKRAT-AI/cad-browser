#include "browser/request_filter.h"

#include "include/cef_frame.h"

namespace cad {

RequestFilter::RequestFilter(Blocklist* blocklist) : blocklist_(blocklist) {}

CefResourceRequestHandler::ReturnValue RequestFilter::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  if (!blocklist_ || !request) {
    return RV_CONTINUE;
  }

  // Never interfere with the document itself. Cancelling a main-frame load
  // would blank the tab rather than remove an advert from it.
  if (request->GetResourceType() == RT_MAIN_FRAME) {
    return RV_CONTINUE;
  }

  const std::string url = request->GetURL().ToString();

  // The document URL is what decides first-party. Prefer the frame's own URL
  // and fall back to the request's initiator when the frame is gone.
  std::string document_url;
  if (frame) {
    document_url = frame->GetURL().ToString();
  }
  if (document_url.empty()) {
    document_url = request->GetReferrerURL().ToString();
  }

  if (blocklist_->ShouldBlock(url, document_url)) {
    return RV_CANCEL;
  }
  return RV_CONTINUE;
}

}  // namespace cad
