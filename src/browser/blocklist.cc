#include "browser/blocklist.h"

#include <cctype>
#include <cstdio>
#include <vector>

#include "browser/url_util.h"

namespace cad {
namespace {

std::string ToLowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool IsRedirectTarget(std::string_view token) {
  return token == "0.0.0.0" || token == "127.0.0.1" || token == "::1" ||
         token == "::";
}

// Hostnames that appear in hosts files but must never be blocked.
bool IsReservedHost(std::string_view host) {
  return host.empty() || host == "localhost" || host == "localhost.localdomain" ||
         host == "local" || host == "broadcasthost" || host == "ip6-localhost" ||
         host == "ip6-loopback";
}

std::vector<std::string_view> SplitWhitespace(std::string_view line) {
  std::vector<std::string_view> tokens;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i]))) {
      ++i;
    }
    const size_t start = i;
    while (i < line.size() &&
           !std::isspace(static_cast<unsigned char>(line[i]))) {
      ++i;
    }
    if (i > start)
      tokens.push_back(line.substr(start, i - start));
  }
  return tokens;
}

}  // namespace

Blocklist::Blocklist() = default;

size_t Blocklist::LoadFromText(std::string_view text) {
  std::vector<std::string> parsed;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;

    const size_t comment = line.find('#');
    if (comment != std::string_view::npos)
      line = line.substr(0, comment);

    const std::vector<std::string_view> tokens = SplitWhitespace(line);
    if (tokens.empty())
      continue;

    // Either "host" or "0.0.0.0 host [host...]".
    size_t first = 0;
    if (tokens.size() > 1 && IsRedirectTarget(tokens[0]))
      first = 1;

    for (size_t i = first; i < tokens.size(); ++i) {
      std::string host = ToLowerAscii(tokens[i]);
      // Tolerate a full URL where a hostname was expected.
      if (host.find("://") != std::string::npos)
        host = HostFromUrl(host);
      while (!host.empty() && host.back() == '.')
        host.pop_back();
      if (IsReservedHost(host))
        continue;
      if (host.find('.') == std::string::npos)
        continue;  // a bare label cannot be a real host to block
      parsed.push_back(std::move(host));
    }

    if (end == text.size())
      break;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const size_t before = hosts_.size();
  for (std::string& host : parsed)
    hosts_.insert(std::move(host));
  return hosts_.size() - before;
}

size_t Blocklist::LoadFromFile(const std::wstring& path) {
  if (path.empty())
    return 0;
  FILE* file = nullptr;
  if (::_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
    return 0;

  std::string contents;
  char buffer[8192];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    contents.append(buffer, read);
  std::fclose(file);

  return LoadFromText(contents);
}

size_t Blocklist::host_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return hosts_.size();
}

void Blocklist::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
}

bool Blocklist::enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

void Blocklist::AllowSite(const std::string& registrable_domain) {
  if (registrable_domain.empty())
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  allowed_sites_.insert(ToLowerAscii(registrable_domain));
}

void Blocklist::BlockSite(const std::string& registrable_domain) {
  std::lock_guard<std::mutex> lock(mutex_);
  allowed_sites_.erase(ToLowerAscii(registrable_domain));
}

bool Blocklist::IsSiteAllowed(const std::string& registrable_domain) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return allowed_sites_.count(ToLowerAscii(registrable_domain)) != 0;
}

std::set<std::string> Blocklist::allowed_sites() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return allowed_sites_;
}

bool Blocklist::MatchesHost(const std::string& host) const {
  // Walk up the domain so that a listed "doubleclick.net" also covers
  // "stats.g.doubleclick.net".
  std::string_view candidate(host);
  for (;;) {
    if (hosts_.count(std::string(candidate)) != 0)
      return true;
    const size_t dot = candidate.find('.');
    if (dot == std::string_view::npos)
      return false;
    candidate = candidate.substr(dot + 1);
    // Stop before a bare public suffix.
    if (candidate.find('.') == std::string_view::npos)
      return false;
  }
}

bool Blocklist::ShouldBlock(std::string_view request_url,
                            std::string_view document_url) const {
  const std::string request_host = HostFromUrl(request_url);
  if (request_host.empty())
    return false;

  // Our own pages and non-network schemes are never filtered.
  const std::string scheme = SchemeFromUrl(request_url);
  if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss")
    return false;

  const std::string document_host = HostFromUrl(document_url);

  // First-party requests are never blocked. A list entry that happens to name
  // the site the user is on must not take that site down.
  if (!document_host.empty() &&
      RegistrableDomain(request_host) == RegistrableDomain(document_host)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return false;
  if (!document_host.empty() &&
      allowed_sites_.count(RegistrableDomain(document_host)) != 0) {
    return false;
  }
  if (!MatchesHost(request_host))
    return false;

  ++blocked_count_;
  return true;
}

uint64_t Blocklist::blocked_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return blocked_count_;
}

void Blocklist::ResetBlockedCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  blocked_count_ = 0;
}

}  // namespace cad
