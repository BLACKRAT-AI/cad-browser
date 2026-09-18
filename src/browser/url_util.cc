#include "browser/url_util.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace cad {
namespace {

std::string ToLowerAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string Trim(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  auto is_space = [](unsigned char c) { return c <= 0x20; };
  while (begin < end && is_space(static_cast<unsigned char>(text[begin])))
    ++begin;
  while (end > begin && is_space(static_cast<unsigned char>(text[end - 1])))
    --end;
  return std::string(text.substr(begin, end - begin));
}

// Schemes we will navigate to directly when the user types one.
bool IsNavigableScheme(const std::string& scheme) {
  static const char* const kSchemes[] = {"http", "https", "file", "cad",
                                         "about", "chrome", "data", "ftp",
                                         "view-source", "devtools", "blob"};
  for (const char* candidate : kSchemes) {
    if (scheme == candidate)
      return true;
  }
  return false;
}

// Two-part public suffixes common enough to be worth handling without
// shipping the full Public Suffix List. See the note in url_util.h.
bool IsMultiPartSuffix(std::string_view second_level, std::string_view top) {
  static const char* const kSecondLevel[] = {"co", "com", "net", "org", "gov",
                                             "edu", "ac",  "or",  "ne", "gr",
                                             "sch", "nhs", "police", "mod"};
  static const char* const kTopLevel[] = {
      "uk", "au", "nz", "za", "jp", "kr", "in", "br", "mx", "il",
      "tr", "id", "th", "sg", "my", "ph", "ar", "cn", "hk", "tw"};

  bool second_matches = false;
  for (const char* candidate : kSecondLevel) {
    if (second_level == candidate) {
      second_matches = true;
      break;
    }
  }
  if (!second_matches)
    return false;
  for (const char* candidate : kTopLevel) {
    if (top == candidate)
      return true;
  }
  return false;
}

std::vector<std::string> SplitHost(std::string_view host) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= host.size(); ++i) {
    if (i == host.size() || host[i] == '.') {
      if (i > start)
        parts.emplace_back(host.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

// Finds where the scheme ends. Returns npos when there is no scheme.
size_t FindSchemeEnd(std::string_view url) {
  for (size_t i = 0; i < url.size(); ++i) {
    const char c = url[i];
    if (c == ':')
      return i == 0 ? std::string_view::npos : i;
    const bool valid = std::isalnum(static_cast<unsigned char>(c)) || c == '+' ||
                       c == '-' || c == '.';
    if (!valid)
      return std::string_view::npos;
    // A scheme must start with a letter.
    if (i == 0 && !std::isalpha(static_cast<unsigned char>(c)))
      return std::string_view::npos;
  }
  return std::string_view::npos;
}

// True when the text begins with a scheme we are willing to navigate to.
bool HasNavigableScheme(std::string_view text) {
  const size_t end = FindSchemeEnd(text);
  if (end == std::string_view::npos)
    return false;
  return IsNavigableScheme(ToLowerAscii(text.substr(0, end)));
}

// Distinguishes "localhost:8080" from a scheme. Both have a colon in the same
// place; only one of them has digits after it.
bool LooksLikeHostAndPort(std::string_view text) {
  const size_t colon = FindSchemeEnd(text);
  if (colon == std::string_view::npos)
    return false;
  size_t i = colon + 1;
  const size_t digits_start = i;
  while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
    ++i;
  if (i == digits_start)
    return false;
  return i == text.size() || text[i] == '/' || text[i] == '?' || text[i] == '#';
}

// The authority portion of text that has no scheme: up to the first delimiter.
std::string_view AuthorityOf(std::string_view text) {
  size_t end = text.size();
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '/' || c == '?' || c == '#' || c == ':') {
      end = i;
      break;
    }
  }
  return text.substr(0, end);
}

}  // namespace

std::string SchemeFromUrl(std::string_view url) {
  const size_t end = FindSchemeEnd(url);
  if (end == std::string_view::npos)
    return {};
  return ToLowerAscii(url.substr(0, end));
}

std::string HostFromUrl(std::string_view url) {
  std::string_view rest = url;
  const size_t scheme_end = FindSchemeEnd(url);
  if (scheme_end != std::string_view::npos) {
    rest = url.substr(scheme_end + 1);
    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
      rest = rest.substr(2);
    else
      return {};  // opaque scheme such as "about:" or "data:"
  }

  // The authority ends at the first '/', '?' or '#'.
  size_t end = rest.size();
  for (size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == '/' || rest[i] == '?' || rest[i] == '#') {
      end = i;
      break;
    }
  }
  std::string_view authority = rest.substr(0, end);

  // Strip any userinfo.
  const size_t at = authority.rfind('@');
  if (at != std::string_view::npos)
    authority = authority.substr(at + 1);

  // Strip the port, taking care not to mangle a bracketed IPv6 literal.
  if (!authority.empty() && authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close != std::string_view::npos)
      return ToLowerAscii(authority.substr(0, close + 1));
    return ToLowerAscii(authority);
  }
  const size_t colon = authority.find(':');
  if (colon != std::string_view::npos)
    authority = authority.substr(0, colon);

  return ToLowerAscii(authority);
}

std::string PathFromUrl(std::string_view url) {
  std::string_view rest = url;
  const size_t scheme_end = FindSchemeEnd(url);
  if (scheme_end != std::string_view::npos) {
    rest = url.substr(scheme_end + 1);
    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
      rest = rest.substr(2);
    else
      return std::string(rest);
  }
  const size_t slash = rest.find('/');
  if (slash == std::string_view::npos)
    return "/";
  return std::string(rest.substr(slash));
}

std::string RegistrableDomain(std::string_view host) {
  const std::string lower = ToLowerAscii(host);
  // An IP literal has no registrable domain; return it unchanged.
  if (!lower.empty() && (lower.front() == '[' ||
                         lower.find_first_not_of("0123456789.") ==
                             std::string::npos)) {
    return lower;
  }

  const std::vector<std::string> parts = SplitHost(lower);
  if (parts.size() <= 2)
    return lower;

  const std::string& top = parts[parts.size() - 1];
  const std::string& second = parts[parts.size() - 2];
  const size_t take = IsMultiPartSuffix(second, top) ? 3 : 2;
  if (parts.size() <= take)
    return lower;

  std::string out;
  for (size_t i = parts.size() - take; i < parts.size(); ++i) {
    if (!out.empty())
      out.push_back('.');
    out += parts[i];
  }
  return out;
}

bool LooksLikeUrl(std::string_view input) {
  const std::string text = Trim(input);
  if (text.empty())
    return false;

  if (HasNavigableScheme(text))
    return true;

  // There is a colon early on, but it introduces neither a scheme we navigate
  // to nor a port number. Treat it as a search rather than guessing.
  if (FindSchemeEnd(text) != std::string_view::npos &&
      !LooksLikeHostAndPort(text)) {
    return false;
  }

  // A space anywhere means it is a search. An explicit scheme was the one
  // exception and it was handled above.
  if (text.find(' ') != std::string::npos)
    return false;

  if (text.rfind("//", 0) == 0)
    return true;

  const std::string authority = ToLowerAscii(AuthorityOf(text));
  if (authority.empty())
    return false;

  if (authority == "localhost")
    return true;

  // Needs a dot with something either side of it to be a hostname.
  const size_t dot = authority.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= authority.size())
    return false;

  // A trailing component that is all digits is an IP address, otherwise it has
  // to look like a TLD.
  const std::vector<std::string> parts = SplitHost(authority);
  if (parts.size() < 2)
    return false;
  const std::string& last = parts.back();
  const bool all_digits =
      last.find_first_not_of("0123456789") == std::string::npos;
  if (all_digits)
    return parts.size() == 4;  // dotted-quad IPv4
  return last.size() >= 2 &&
         last.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") ==
             std::string::npos;
}

std::string PercentEncodeQuery(std::string_view text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    const bool unreserved = std::isalnum(c) || c == '-' || c == '_' ||
                            c == '.' || c == '~';
    if (unreserved) {
      out.push_back(raw);
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[(c >> 4) & 0x0F]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::string ResolveOmniboxInput(std::string_view input,
                                std::string_view search_url_template) {
  const std::string text = Trim(input);
  if (text.empty())
    return {};

  if (LooksLikeUrl(text)) {
    // Only a scheme we recognise is left alone. "localhost:8080" looks like a
    // scheme to a naive parser and must still get an https:// prefix.
    if (HasNavigableScheme(text))
      return text;
    if (text.rfind("//", 0) == 0)
      return "https:" + text;
    return "https://" + text;
  }

  std::string url(search_url_template);
  const size_t placeholder = url.find("%s");
  const std::string encoded = PercentEncodeQuery(text);
  if (placeholder == std::string::npos)
    return url + encoded;
  url.replace(placeholder, 2, encoded);
  return url;
}

std::string DisplayUrl(std::string_view url) {
  std::string text(url);
  if (text.rfind("https://", 0) == 0)
    text = text.substr(8);
  // A bare host with nothing after it reads better without the slash.
  if (text.size() > 1 && text.back() == '/' &&
      text.find('/') == text.size() - 1) {
    text.pop_back();
  }
  return text;
}

std::string FallbackTitleForUrl(std::string_view url) {
  if (url.empty())
    return "New tab";
  const std::string host = HostFromUrl(url);
  if (!host.empty()) {
    if (host.rfind("www.", 0) == 0)
      return host.substr(4);
    return host;
  }
  return std::string(url);
}

bool IsSameSite(std::string_view a, std::string_view b) {
  const std::string host_a = HostFromUrl(a);
  const std::string host_b = HostFromUrl(b);
  if (host_a.empty() || host_b.empty())
    return false;
  return RegistrableDomain(host_a) == RegistrableDomain(host_b);
}

bool IsInternalUrl(std::string_view url) {
  return SchemeFromUrl(url) == "cad";
}

bool IsNewTabUrl(std::string_view url) {
  return url == "cad://newtab" || url == "cad://newtab/";
}

}  // namespace cad
