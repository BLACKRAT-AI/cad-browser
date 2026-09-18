// CAD Browser - the per-request hook that applies the host blocklist.
//
// Runs on the IO thread for every subresource. It does one hash-set lookup per
// request and nothing else, because anything slower here is felt on every page
// load. The decision itself lives in Blocklist so it can be tested.
#pragma once

#include "browser/blocklist.h"
#include "include/cef_resource_request_handler.h"

namespace cad {

class RequestFilter : public CefResourceRequestHandler {
 public:
  // `blocklist` outlives every filter: it is owned by AppState, which is
  // destroyed after all browsers have closed.
  explicit RequestFilter(Blocklist* blocklist);

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;

 private:
  Blocklist* const blocklist_;

  IMPLEMENT_REFCOUNTING(RequestFilter);
  DISALLOW_COPY_AND_ASSIGN(RequestFilter);
};

}  // namespace cad
