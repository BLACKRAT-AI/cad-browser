#include "browser/string_resource_handler.h"

#include <algorithm>
#include <cstring>

namespace cad {

StringResourceHandler::StringResourceHandler(std::string body,
                                             std::string mime_type,
                                             int status_code)
    : body_(std::move(body)),
      mime_type_(std::move(mime_type)),
      status_code_(status_code) {}

void StringResourceHandler::AddHeader(const std::string& name,
                                      const std::string& value) {
  headers_.insert({name, value});
}

bool StringResourceHandler::Open(CefRefPtr<CefRequest> request,
                                 bool& handle_request,
                                 CefRefPtr<CefCallback> callback) {
  // The whole body is already in hand, so the request is handled synchronously.
  handle_request = true;
  return true;
}

void StringResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                               int64_t& response_length,
                                               CefString& redirectUrl) {
  response->SetStatus(status_code_);
  response->SetMimeType(mime_type_);
  if (!headers_.empty()) {
    response->SetHeaderMap(headers_);
  }
  response_length = static_cast<int64_t>(body_.size());
}

bool StringResourceHandler::Read(void* data_out,
                                 int bytes_to_read,
                                 int& bytes_read,
                                 CefRefPtr<CefResourceReadCallback> callback) {
  bytes_read = 0;
  if (!data_out || bytes_to_read <= 0 || offset_ >= body_.size()) {
    return false;  // end of stream
  }

  const size_t remaining = body_.size() - offset_;
  const size_t count =
      std::min(remaining, static_cast<size_t>(bytes_to_read));
  std::memcpy(data_out, body_.data() + offset_, count);
  offset_ += count;
  bytes_read = static_cast<int>(count);
  return true;
}

void StringResourceHandler::Cancel() {}

}  // namespace cad
