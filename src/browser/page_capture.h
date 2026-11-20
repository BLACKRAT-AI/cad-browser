// CAD Browser - saving the page the user is looking at to a JSON file.
//
// The user drives: they browse, log in if a site asks, and either press
// "Capture page" or turn on auto-capture. The browser only writes down what
// is already on screen. There is deliberately no way to make it navigate or
// capture on someone else's command.
//
// This file holds the parts with no CEF in them, so they can be unit tested:
// the file name, the slug and the JSON written to disk.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cad {

// Auto-capture waits this long after the last navigation event before it
// captures, so a single-page app that rewrites its history several times while
// it settles is saved once, with its content in place.
inline constexpr int kAutoCaptureDebounceMs = 2000;

struct CapturedLink {
  std::string text;
  std::string href;
};

struct PageCapture {
  std::string url;
  std::string title;
  std::string text;  // document.body.innerText
  std::string html;  // document.documentElement.outerHTML
  std::vector<CapturedLink> links;
};

// A broken-down time. Local time for the file name, UTC for captured_at.
struct CaptureTime {
  int year = 1970;
  int month = 1;  // 1..12
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

// Lowercase ASCII letters and digits, every other run of characters collapsed
// to one '-', no leading or trailing '-', at most `max_length` characters and
// never cut in a way that leaves a trailing '-'. Empty when nothing survives.
std::string CaptureSlug(std::string_view text, size_t max_length = 60);

// The host for the file name: lowercased, "www." dropped, anything outside
// [a-z0-9.-] replaced. "local" when the URL has no host (file:// pages).
std::string CaptureHostPart(std::string_view url);

// "<yyyymmdd-hhmmss>-<host>-<slug>.json". The slug comes from the title, or
// from the URL path when the title has nothing usable, or is "page".
std::string CaptureFileName(const CaptureTime& local_time,
                            std::string_view url,
                            std::string_view title);

// "2026-11-08T21:03:04Z".
std::string CaptureTimestampUtc(const CaptureTime& utc_time);

// {url, title, captured_at, text, html, links: [{text, href}]}.
std::string CaptureToJson(const PageCapture& page, std::string_view captured_at);

// Only web pages and local files are worth saving: not the browser's own
// cad:// pages, not about:blank, data: error pages or DevTools.
bool IsCapturableUrl(std::string_view url);

// Creates `folder` and its parents when missing, picks a name that is not
// taken yet ("name.json", then "name-2.json", ...) and writes `contents`
// through a temporary file. Both paths are UTF-8. Returns the file name
// actually used, or an empty string when nothing could be written.
std::string SaveCaptureFile(const std::string& folder,
                            const std::string& file_name,
                            const std::string& contents);

}  // namespace cad
