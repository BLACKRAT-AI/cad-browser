#include "browser/scheme_handler.h"

#include <algorithm>

#include "browser/string_resource_handler.h"
#include "browser/url_util.h"
#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "resources/generated_resources.h"

namespace cad {
namespace {

class CadSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CadSchemeHandlerFactory() = default;

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    const std::string url = request->GetURL().ToString();
    const std::string path = ResourcePathForCadUrl(url);

    const resources::Entry* entry =
        path.empty() ? nullptr : resources::Find(path.c_str());
    if (!entry) {
      return MakeNotFound(path);
    }

    // The data lives in the executable's read-only section for the life of the
    // process, so handing out a pointer to it is safe and copy-free.
    CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForData(
        const_cast<unsigned char*>(entry->data), entry->size);
    if (!stream) {
      return MakeNotFound(path);
    }

    CefResponse::HeaderMap headers;
    // Internal pages are static and versioned with the binary.
    headers.insert({"Cache-Control", "no-cache"});
    // They never need to reach the network or embed anything external.
    headers.insert(
        {"Content-Security-Policy",
         "default-src 'self' cad:; style-src 'self' cad: 'unsafe-inline'; "
         "img-src 'self' cad: data:; script-src 'self' cad: 'unsafe-inline'; "
         "connect-src 'none'; frame-ancestors 'none'"});
    headers.insert({"X-Content-Type-Options", "nosniff"});

    return new CefStreamResourceHandler(200, "OK", entry->mime_type, headers,
                                        stream);
  }

 private:
  static CefRefPtr<CefResourceHandler> MakeNotFound(const std::string& path) {
    // StringResourceHandler owns the body; building it on the stack and
    // handing a pointer to a stream reader would be a lifetime bug.
    std::string body =
        "<!doctype html><meta charset=\"utf-8\">"
        "<title>Not found</title>"
        "<body style=\"font:13px Segoe UI,sans-serif;padding:24px\">"
        "<h1 style=\"font-size:15px\">cad:// page not found</h1><p>";
    body += path.empty() ? "(no resource named)" : path;
    body += "</p></body>";

    CefRefPtr<StringResourceHandler> handler =
        new StringResourceHandler(std::move(body), "text/html", 404);
    handler->AddHeader("Cache-Control", "no-store");
    return handler;
  }

  IMPLEMENT_REFCOUNTING(CadSchemeHandlerFactory);
  DISALLOW_COPY_AND_ASSIGN(CadSchemeHandlerFactory);
};

}  // namespace

std::string ResourcePathForCadUrl(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }

  const std::string host = CefString(&parts.host).ToString();
  std::string path = CefString(&parts.path).ToString();

  // Strip a query or fragment if CefParseURL left them attached.
  const size_t cut = path.find_first_of("?#");
  if (cut != std::string::npos) {
    path = path.substr(0, cut);
  }
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }

  // Refuse anything that tries to escape the resource namespace. There is no
  // filesystem behind this, but a path with ".." in it is never legitimate and
  // is worth rejecting outright rather than relying on lookup to fail.
  if (path.find("..") != std::string::npos) {
    return {};
  }

  if (path.empty()) {
    if (host.empty()) {
      return {};
    }
    return host + ".html";
  }
  return path;
}

void RegisterCadCustomScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  if (!registrar) {
    return;
  }
  // STANDARD gives the scheme a real origin, which pages need for relative
  // URLs and for anything storage-backed. SECURE stops the pages being treated
  // as mixed content. FETCH_ENABLED lets a page fetch its own assets.
  registrar->AddCustomScheme(
      kCadScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                      CEF_SCHEME_OPTION_CORS_ENABLED |
                      CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void RegisterCadSchemeHandlerFactory() {
  CefRegisterSchemeHandlerFactory(kCadScheme, std::string(),
                                  new CadSchemeHandlerFactory());
}

}  // namespace cad
