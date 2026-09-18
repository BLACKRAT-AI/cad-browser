#include "cad_test.h"

#include "browser/url_util.h"

namespace {

constexpr char kSearch[] = "https://duckduckgo.com/?q=%s";

std::string Resolve(const char* input) {
  return cad::ResolveOmniboxInput(input, kSearch);
}

}  // namespace

CAD_TEST(HostIsExtractedFromUrls) {
  CAD_EXPECT_EQ(cad::HostFromUrl("https://www.example.com/path?q=1"),
                std::string("www.example.com"));
  CAD_EXPECT_EQ(cad::HostFromUrl("http://example.com:8080/"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::HostFromUrl("https://user:pass@example.com/"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::HostFromUrl("https://EXAMPLE.COM"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::HostFromUrl("https://[::1]:8080/x"), std::string("[::1]"));
  CAD_EXPECT_EQ(cad::HostFromUrl("about:blank"), std::string());
  CAD_EXPECT_EQ(cad::HostFromUrl("data:text/html,x"), std::string());
}

CAD_TEST(SchemeIsExtracted) {
  CAD_EXPECT_EQ(cad::SchemeFromUrl("https://x.test/"), std::string("https"));
  CAD_EXPECT_EQ(cad::SchemeFromUrl("CAD://newtab"), std::string("cad"));
  CAD_EXPECT_EQ(cad::SchemeFromUrl("example.com/path"), std::string());
  CAD_EXPECT_EQ(cad::SchemeFromUrl("//example.com"), std::string());
}

CAD_TEST(RegistrableDomainHandlesMultiPartSuffixes) {
  CAD_EXPECT_EQ(cad::RegistrableDomain("www.example.com"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("a.b.c.example.com"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("www.example.co.uk"),
                std::string("example.co.uk"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("shop.example.com.au"),
                std::string("example.com.au"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("example.com"),
                std::string("example.com"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("localhost"), std::string("localhost"));
  CAD_EXPECT_EQ(cad::RegistrableDomain("192.168.1.10"),
                std::string("192.168.1.10"));
}

CAD_TEST(TypedUrlsAreNavigatedNotSearched) {
  CAD_EXPECT_EQ(Resolve("https://onshape.com"),
                std::string("https://onshape.com"));
  CAD_EXPECT_EQ(Resolve("onshape.com"), std::string("https://onshape.com"));
  CAD_EXPECT_EQ(Resolve("www.onshape.com/documents"),
                std::string("https://www.onshape.com/documents"));
  CAD_EXPECT_EQ(Resolve("localhost:8080"), std::string("https://localhost:8080"));
  CAD_EXPECT_EQ(Resolve("192.168.1.10/setup"),
                std::string("https://192.168.1.10/setup"));
  CAD_EXPECT_EQ(Resolve("cad://settings"), std::string("cad://settings"));
  CAD_EXPECT_EQ(Resolve("file:///C:/model.step"),
                std::string("file:///C:/model.step"));
}

CAD_TEST(TypedTextBecomesASearch) {
  CAD_EXPECT_EQ(Resolve("fillet radius"),
                std::string("https://duckduckgo.com/?q=fillet+radius"));
  // A single word with no dot is a search, not a hostname.
  CAD_EXPECT_EQ(Resolve("onshape"),
                std::string("https://duckduckgo.com/?q=onshape"));
  // A sentence containing a dot is still a search because of the spaces.
  CAD_EXPECT_EQ(Resolve("what is example.com"),
                std::string("https://duckduckgo.com/?q=what+is+example.com"));
}

CAD_TEST(SearchQueriesArePercentEncoded) {
  CAD_EXPECT_EQ(Resolve("c++ & cad"),
                std::string("https://duckduckgo.com/?q=c%2B%2B+%26+cad"));
}

CAD_TEST(EmptyInputResolvesToNothing) {
  CAD_EXPECT_EQ(Resolve(""), std::string());
  CAD_EXPECT_EQ(Resolve("   "), std::string());
}

CAD_TEST(DisplayUrlDropsHttpsAndTrailingSlash) {
  CAD_EXPECT_EQ(cad::DisplayUrl("https://onshape.com/"),
                std::string("onshape.com"));
  CAD_EXPECT_EQ(cad::DisplayUrl("https://onshape.com/documents/"),
                std::string("onshape.com/documents/"));
  CAD_EXPECT_EQ(cad::DisplayUrl("http://example.test/"),
                std::string("http://example.test/"));
}

CAD_TEST(FallbackTitleUsesTheHost) {
  CAD_EXPECT_EQ(cad::FallbackTitleForUrl("https://www.onshape.com/doc/1"),
                std::string("onshape.com"));
  CAD_EXPECT_EQ(cad::FallbackTitleForUrl(""), std::string("New tab"));
}

CAD_TEST(SameSiteComparesRegistrableDomains) {
  CAD_EXPECT(cad::IsSameSite("https://a.example.com/1",
                             "https://b.example.com/2"));
  CAD_EXPECT(!cad::IsSameSite("https://example.com", "https://example.org"));
  CAD_EXPECT(!cad::IsSameSite("about:blank", "https://example.com"));
}

CAD_TEST(InternalUrlsAreRecognised) {
  CAD_EXPECT(cad::IsInternalUrl("cad://settings"));
  CAD_EXPECT(!cad::IsInternalUrl("https://cad.test/"));
}
