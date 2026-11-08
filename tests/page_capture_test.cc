#include "cad_test.h"

#include <filesystem>
#include <system_error>

#include "browser/json.h"
#include "browser/page_capture.h"
#include "sys/win_util.h"

namespace {

using cad::CaptureFileName;
using cad::CaptureHostPart;
using cad::CaptureSlug;
using cad::CaptureTime;
using cad::JsonValue;

CaptureTime SampleTime() {
  CaptureTime t;
  t.year = 2026;
  t.month = 11;
  t.day = 8;
  t.hour = 9;
  t.minute = 5;
  t.second = 3;
  return t;
}

}  // namespace

CAD_TEST(CaptureSlugLowercasesAndCollapses) {
  CAD_EXPECT_EQ(CaptureSlug("Most Voted -- Improvement Requests!"),
                std::string("most-voted-improvement-requests"));
  CAD_EXPECT_EQ(CaptureSlug("  ...Leading and trailing...  "),
                std::string("leading-and-trailing"));
  CAD_EXPECT_EQ(CaptureSlug("/r/SolidWorks/top/?t=year"),
                std::string("r-solidworks-top-t-year"));
  CAD_EXPECT_EQ(CaptureSlug("!!!"), std::string());
  CAD_EXPECT_EQ(CaptureSlug(""), std::string());
}

CAD_TEST(CaptureSlugDropsNonAscii) {
  // "Caf\xC3\xA9 ideas": the UTF-8 bytes of the accented letter become a break.
  CAD_EXPECT_EQ(CaptureSlug("Caf\xC3\xA9 ideas"), std::string("caf-ideas"));
}

CAD_TEST(CaptureSlugRespectsMaxLengthWithoutTrailingDash) {
  CAD_EXPECT_EQ(CaptureSlug("abcd efgh", 4), std::string("abcd"));
  CAD_EXPECT_EQ(CaptureSlug("abcd efgh", 5), std::string("abcd"));
  CAD_EXPECT_EQ(CaptureSlug("abcd efgh", 6), std::string("abcd-e"));
  const std::string long_title(200, 'x');
  CAD_EXPECT_EQ(CaptureSlug(long_title).size(), size_t(60));
}

CAD_TEST(CaptureHostPartStripsWwwAndPort) {
  CAD_EXPECT_EQ(CaptureHostPart("https://www.reddit.com/r/cad/top"),
                std::string("reddit.com"));
  CAD_EXPECT_EQ(CaptureHostPart("https://forum.onshape.com:443/categories"),
                std::string("forum.onshape.com"));
  CAD_EXPECT_EQ(CaptureHostPart("file:///C:/pages/saved.html"),
                std::string("local"));
}

CAD_TEST(CaptureFileNameUsesTitleThenPathThenPage) {
  CAD_EXPECT_EQ(
      CaptureFileName(SampleTime(), "https://forum.onshape.com/categories/x",
                      "Improvement Requests"),
      std::string("20261108-090503-forum.onshape.com-improvement-requests.json"));
  CAD_EXPECT_EQ(CaptureFileName(SampleTime(),
                                "https://www.reddit.com/r/SolidWorks/top/", ""),
                std::string("20261108-090503-reddit.com-r-solidworks-top.json"));
  CAD_EXPECT_EQ(CaptureFileName(SampleTime(), "https://example.com/", "???"),
                std::string("20261108-090503-example.com-page.json"));
}

CAD_TEST(CaptureTimestampIsIsoUtc) {
  CAD_EXPECT_EQ(cad::CaptureTimestampUtc(SampleTime()),
                std::string("2026-11-08T09:05:03Z"));
}

CAD_TEST(CaptureJsonRoundTripsEveryField) {
  cad::PageCapture page;
  page.url = "https://example.com/a?b=\"c\"";
  page.title = "Title with \"quotes\" and \\ slash";
  page.text = "line one\nline two\ttabbed";
  page.html = "<html><body><p>x</p></body></html>";
  page.links.push_back({"First link", "https://example.com/1"});
  page.links.push_back({"", "https://example.com/2"});

  const std::string json =
      cad::CaptureToJson(page, "2026-11-08T09:05:03Z");
  std::string error;
  const JsonValue parsed = JsonValue::Parse(json, &error);
  CAD_EXPECT_EQ(error, std::string());
  CAD_EXPECT_EQ(parsed.GetString("url"), page.url);
  CAD_EXPECT_EQ(parsed.GetString("title"), page.title);
  CAD_EXPECT_EQ(parsed.GetString("captured_at"),
                std::string("2026-11-08T09:05:03Z"));
  CAD_EXPECT_EQ(parsed.GetString("text"), page.text);
  CAD_EXPECT_EQ(parsed.GetString("html"), page.html);
  const JsonValue* links = parsed.GetChildArray("links");
  CAD_EXPECT(links != nullptr);
  CAD_EXPECT_EQ(links->items().size(), size_t(2));
  CAD_EXPECT_EQ(links->items()[0].GetString("text"), std::string("First link"));
  CAD_EXPECT_EQ(links->items()[1].GetString("href"),
                std::string("https://example.com/2"));
  // The keys stay in the documented order.
  CAD_EXPECT_EQ(parsed.members().front().first, std::string("url"));
  CAD_EXPECT_EQ(parsed.members().back().first, std::string("links"));
}

CAD_TEST(CaptureOnlyWebPagesAndFiles) {
  CAD_EXPECT(cad::IsCapturableUrl("https://forum.onshape.com/"));
  CAD_EXPECT(cad::IsCapturableUrl("http://example.com/"));
  CAD_EXPECT(cad::IsCapturableUrl("file:///C:/x.html"));
  CAD_EXPECT(!cad::IsCapturableUrl("cad://newtab"));
  CAD_EXPECT(!cad::IsCapturableUrl("about:blank"));
  CAD_EXPECT(!cad::IsCapturableUrl("data:text/html,hi"));
  CAD_EXPECT(!cad::IsCapturableUrl("devtools://devtools/x"));
}

CAD_TEST(CaptureSaveCreatesFolderAndNeverOverwrites) {
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path root =
      fs::temp_directory_path(error) / L"cad_capture_test" / L"nested";
  fs::remove_all(root.parent_path(), error);
  const std::string folder = cad::sys::ToUtf8(root.wstring());

  const std::string first = cad::SaveCaptureFile(folder, "a-b.json", "{}");
  CAD_EXPECT_EQ(first, std::string("a-b.json"));
  const std::string second = cad::SaveCaptureFile(folder, "a-b.json", "[]");
  CAD_EXPECT_EQ(second, std::string("a-b-2.json"));
  CAD_EXPECT(fs::exists(root / L"a-b.json", error));
  CAD_EXPECT(fs::exists(root / L"a-b-2.json", error));
  CAD_EXPECT_EQ(fs::file_size(root / L"a-b.json", error), uintmax_t(2));
  CAD_EXPECT_EQ(cad::SaveCaptureFile("", "x.json", "{}"), std::string());

  fs::remove_all(root.parent_path(), error);
}
