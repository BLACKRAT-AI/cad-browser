// CAD Browser - URL parsing for the omnibox, the blocklist and the UI.
//
// Scope note: this is not a general URL library. It handles the three jobs the
// browser actually has - deciding whether typed text is a URL or a search,
// pulling the host out of a URL for host-based blocking, and shortening a URL
// for display. Anything that needs full correctness (resource loading, origin
// comparisons) is left to Chromium, which already does it properly.
#pragma once

#include <string>
#include <string_view>

namespace cad {

// The scheme, lowercased and without the colon. Empty when there is none.
std::string SchemeFromUrl(std::string_view url);

// The host, lowercased, without userinfo or port. Empty when there is none.
std::string HostFromUrl(std::string_view url);

// The path and everything after it, starting with '/'.
std::string PathFromUrl(std::string_view url);

// Approximate registrable domain ("example.co.uk" from "www.example.co.uk").
//
// This uses a short table of common multi-part suffixes rather than the full
// Public Suffix List. It is good enough for grouping blocklist entries and for
// the "block on this site" toggle; it is deliberately not used for anything
// security relevant.
std::string RegistrableDomain(std::string_view host);

// True when the text should be treated as a URL rather than a search query.
bool LooksLikeUrl(std::string_view input);

// Turns whatever was typed in the omnibox into something navigable. Non-URL
// input is substituted into `search_url_template`, where "%s" marks the spot
// for the percent-encoded query.
std::string ResolveOmniboxInput(std::string_view input,
                                std::string_view search_url_template);

// Shortens a URL for display: drops "https://" and a bare trailing slash.
std::string DisplayUrl(std::string_view url);

// A short title for a tab that has none yet.
std::string FallbackTitleForUrl(std::string_view url);

std::string PercentEncodeQuery(std::string_view text);

// True when both URLs have the same registrable domain.
bool IsSameSite(std::string_view a, std::string_view b);

// True for the browser's own cad:// pages.
bool IsInternalUrl(std::string_view url);

// True for the new tab page. Chromium normalises "cad://newtab" to
// "cad://newtab/", so anything comparing against it has to accept both.
bool IsNewTabUrl(std::string_view url);

}  // namespace cad
