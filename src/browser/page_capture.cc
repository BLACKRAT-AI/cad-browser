#include "browser/page_capture.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "browser/json.h"
#include "browser/settings_store.h"
#include "browser/url_util.h"
#include "sys/win_util.h"

namespace cad {
namespace {

bool IsSlugChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

char LowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string CaptureSlug(std::string_view text, size_t max_length) {
  std::string slug;
  slug.reserve(text.size() < max_length ? text.size() : max_length);
  bool pending_dash = false;
  for (const char raw : text) {
    const char c = LowerAscii(raw);
    if (!IsSlugChar(c)) {
      pending_dash = !slug.empty();
      continue;
    }
    const size_t needed = pending_dash ? 2 : 1;
    if (slug.size() + needed > max_length) {
      break;
    }
    if (pending_dash) {
      slug.push_back('-');
      pending_dash = false;
    }
    slug.push_back(c);
  }
  return slug;
}

std::string CaptureHostPart(std::string_view url) {
  std::string host = HostFromUrl(url);
  if (host.rfind("www.", 0) == 0) {
    host.erase(0, 4);
  }
  std::string out;
  out.reserve(host.size());
  for (const char raw : host) {
    const char c = LowerAscii(raw);
    out.push_back(IsSlugChar(c) || c == '.' || c == '-' ? c : '-');
  }
  return out.empty() ? std::string("local") : out;
}

std::string CaptureFileName(const CaptureTime& t,
                            std::string_view url,
                            std::string_view title) {
  std::string slug = CaptureSlug(title);
  if (slug.empty()) {
    slug = CaptureSlug(PathFromUrl(url));
  }
  if (slug.empty()) {
    slug = "page";
  }
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", t.year,
                t.month, t.day, t.hour, t.minute, t.second);
  return std::string(stamp) + "-" + CaptureHostPart(url) + "-" + slug +
         ".json";
}

std::string CaptureTimestampUtc(const CaptureTime& t) {
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year,
                t.month, t.day, t.hour, t.minute, t.second);
  return stamp;
}

std::string CaptureToJson(const PageCapture& page,
                          std::string_view captured_at) {
  JsonValue root = JsonValue::Object();
  root.Set("url", JsonValue::String(page.url));
  root.Set("title", JsonValue::String(page.title));
  root.Set("captured_at", JsonValue::String(std::string(captured_at)));
  root.Set("text", JsonValue::String(page.text));
  root.Set("html", JsonValue::String(page.html));
  JsonValue links = JsonValue::Array();
  for (const CapturedLink& link : page.links) {
    JsonValue entry = JsonValue::Object();
    entry.Set("text", JsonValue::String(link.text));
    entry.Set("href", JsonValue::String(link.href));
    links.Append(std::move(entry));
  }
  root.Set("links", std::move(links));
  return root.Serialize(/*pretty=*/true);
}

bool IsCapturableUrl(std::string_view url) {
  const std::string scheme = SchemeFromUrl(url);
  return scheme == "http" || scheme == "https" || scheme == "file";
}

std::string SaveCaptureFile(const std::string& folder,
                            const std::string& file_name,
                            const std::string& contents) {
  if (folder.empty() || file_name.empty()) {
    return {};
  }
  namespace fs = std::filesystem;
  const fs::path dir(sys::ToUtf16(folder));
  std::error_code error;
  fs::create_directories(dir, error);
  if (!fs::is_directory(dir, error)) {
    return {};
  }

  std::string stem = file_name;
  std::string extension;
  if (const size_t dot = file_name.rfind('.'); dot != std::string::npos) {
    stem = file_name.substr(0, dot);
    extension = file_name.substr(dot);
  }
  for (int attempt = 1; attempt < 1000; ++attempt) {
    const std::string candidate =
        attempt == 1 ? file_name
                     : stem + "-" + std::to_string(attempt) + extension;
    const fs::path path = dir / sys::ToUtf16(candidate);
    if (fs::exists(path, error)) {
      continue;
    }
    return WriteFileAtomically(path.wstring(), contents) ? candidate
                                                         : std::string();
  }
  return {};
}

}  // namespace cad
