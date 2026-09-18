// CAD Browser - serves a response body the handler owns.
//
// Used for cad:// pages whose content is generated at request time, and for
// error pages. CefStreamResourceHandler over CefStreamReader::CreateForData is
// fine for the embedded resources, whose bytes live in the executable's
// read-only data for the whole process, but it is not a safe pattern for a
// body built on the stack. This handler owns its buffer outright, so the
// question never arises.
#pragma once

#include <string>

#include "include/cef_resource_handler.h"

namespace cad {

class StringResourceHandler : public CefResourceHandler {
 public:
  StringResourceHandler(std::string body,
                        std::string mime_type,
                        int status_code = 200);

  // CefResourceHandler
  bool Open(CefRefPtr<CefRequest> request,
            bool& handle_request,
            CefRefPtr<CefCallback> callback) override;
  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString& redirectUrl) override;
  bool Read(void* data_out,
            int bytes_to_read,
            int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override;
  void Cancel() override;

  void AddHeader(const std::string& name, const std::string& value);

 private:
  const std::string body_;
  const std::string mime_type_;
  const int status_code_;
  CefResponse::HeaderMap headers_;
  size_t offset_ = 0;

  IMPLEMENT_REFCOUNTING(StringResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(StringResourceHandler);
};

}  // namespace cad
