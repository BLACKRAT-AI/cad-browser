// CAD Browser - host-based ad and tracker blocking.
//
// PLAN.md section 7 item 2. v1 blocks by hostname rather than by filter
// syntax: it is a hash set lookup per request, it cannot mis-parse a rule into
// blocking a whole site, and it removes the large majority of third-party
// beacon traffic that makes a heavy CAD web app feel slow.
//
// Two rules keep it from breaking pages:
//   - first-party requests are never blocked, whatever the list says
//   - a site the user has allowed is never blocked
#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cad {

class Blocklist {
 public:
  Blocklist();

  // Parses "one host per line". Accepts plain hostnames and hosts-file lines
  // ("0.0.0.0 tracker.example"); "#" starts a comment. Adds to whatever is
  // already loaded, so the built-in list and the user's file compose.
  size_t LoadFromText(std::string_view text);

  // Reads a user-maintained list. Missing file is not an error.
  size_t LoadFromFile(const std::wstring& path);

  size_t host_count() const;

  void SetEnabled(bool enabled);
  bool enabled() const;

  // Per-site override, keyed by registrable domain of the *document*.
  void AllowSite(const std::string& registrable_domain);
  void BlockSite(const std::string& registrable_domain);
  bool IsSiteAllowed(const std::string& registrable_domain) const;
  std::set<std::string> allowed_sites() const;

  // The decision made for every subresource request.
  bool ShouldBlock(std::string_view request_url,
                   std::string_view document_url) const;

  uint64_t blocked_count() const;
  void ResetBlockedCount();

 private:
  // True when `host` or any of its parent domains is listed.
  bool MatchesHost(const std::string& host) const;

  mutable std::mutex mutex_;
  std::unordered_set<std::string> hosts_;
  std::set<std::string> allowed_sites_;
  bool enabled_ = true;
  mutable uint64_t blocked_count_ = 0;
};

}  // namespace cad
