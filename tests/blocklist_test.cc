#include "cad_test.h"

#include "browser/blocklist.h"

namespace {

void Populate(cad::Blocklist& list) {
  list.LoadFromText(
      "# comment line\n"
      "doubleclick.net\n"
      "0.0.0.0 tracker.example.com\n"
      "127.0.0.1 ads.example.org  metrics.example.org\n"
      "localhost\n"          // reserved, must be ignored
      "badentry\n"           // no dot, must be ignored
      "\n");

}

}  // namespace

CAD_TEST(BlocklistParsesHostsAndPlainLines) {
  cad::Blocklist list; Populate(list);
  // doubleclick.net, tracker.example.com, ads.example.org, metrics.example.org
  CAD_EXPECT_EQ(list.host_count(), size_t(4));
}

CAD_TEST(BlocklistIgnoresReservedAndBareLabels) {
  cad::Blocklist list;
  list.LoadFromText("localhost\nbroadcasthost\nlocal\nnotahost\n");
  CAD_EXPECT_EQ(list.host_count(), size_t(0));
}

CAD_TEST(BlocklistBlocksThirdPartyRequests) {
  cad::Blocklist list; Populate(list);
  CAD_EXPECT(list.ShouldBlock("https://tracker.example.com/pixel.gif",
                              "https://cad-site.test/page"));
  CAD_EXPECT(list.ShouldBlock("https://doubleclick.net/x",
                              "https://cad-site.test/page"));
}

CAD_TEST(BlocklistMatchesSubdomainsOfAListedDomain) {
  cad::Blocklist list; Populate(list);
  // A listed "doubleclick.net" must also cover its subdomains.
  CAD_EXPECT(list.ShouldBlock("https://stats.g.doubleclick.net/collect",
                              "https://cad-site.test/"));
}

CAD_TEST(BlocklistNeverBlocksFirstParty) {
  cad::Blocklist list; Populate(list);
  // The document is the very host on the list. Blocking here would take the
  // site down, so first-party always wins.
  CAD_EXPECT(!list.ShouldBlock("https://tracker.example.com/app.js",
                               "https://tracker.example.com/page"));
  // Same registrable domain, different subdomain, still first party.
  CAD_EXPECT(!list.ShouldBlock("https://cdn.example.com/lib.js",
                               "https://tracker.example.com/page"));
}

CAD_TEST(BlocklistLeavesUnlistedHostsAlone) {
  cad::Blocklist list; Populate(list);
  CAD_EXPECT(!list.ShouldBlock("https://onshape.com/api", "https://cad.test/"));
  CAD_EXPECT(!list.ShouldBlock("https://cdn.jsdelivr.net/three.js",
                               "https://cad.test/"));
}

CAD_TEST(BlocklistIgnoresNonNetworkSchemes) {
  cad::Blocklist list; Populate(list);
  CAD_EXPECT(!list.ShouldBlock("cad://newtab", "https://cad.test/"));
  CAD_EXPECT(!list.ShouldBlock("data:text/html,hi", "https://cad.test/"));
  CAD_EXPECT(!list.ShouldBlock("file:///C:/model.step", "https://cad.test/"));
}

CAD_TEST(DisablingTheBlocklistStopsAllBlocking) {
  cad::Blocklist list; Populate(list);
  list.SetEnabled(false);
  CAD_EXPECT(!list.ShouldBlock("https://doubleclick.net/x",
                               "https://cad-site.test/"));
  list.SetEnabled(true);
  CAD_EXPECT(list.ShouldBlock("https://doubleclick.net/x",
                              "https://cad-site.test/"));
}

CAD_TEST(AllowedSiteDisablesBlockingForThatSiteOnly) {
  cad::Blocklist list; Populate(list);
  list.AllowSite("cad-site.test");

  CAD_EXPECT(!list.ShouldBlock("https://doubleclick.net/x",
                               "https://cad-site.test/page"));
  // ...and for a subdomain of the allowed site.
  CAD_EXPECT(!list.ShouldBlock("https://doubleclick.net/x",
                               "https://app.cad-site.test/page"));
  // ...but nowhere else.
  CAD_EXPECT(list.ShouldBlock("https://doubleclick.net/x",
                              "https://other.test/page"));

  list.BlockSite("cad-site.test");
  CAD_EXPECT(list.ShouldBlock("https://doubleclick.net/x",
                              "https://cad-site.test/page"));
}

CAD_TEST(BlockedCountTracksBlockedRequests) {
  cad::Blocklist list; Populate(list);
  CAD_EXPECT_EQ(list.blocked_count(), uint64_t(0));
  list.ShouldBlock("https://doubleclick.net/a", "https://cad.test/");
  list.ShouldBlock("https://doubleclick.net/b", "https://cad.test/");
  list.ShouldBlock("https://onshape.com/ok", "https://cad.test/");
  CAD_EXPECT_EQ(list.blocked_count(), uint64_t(2));
  list.ResetBlockedCount();
  CAD_EXPECT_EQ(list.blocked_count(), uint64_t(0));
}

CAD_TEST(BlocklistHandlesUrlsAndTrailingDotsInTheList) {
  cad::Blocklist list;
  list.LoadFromText("https://ads.example.net/path\nbeacon.example.net.\n");
  CAD_EXPECT_EQ(list.host_count(), size_t(2));
  CAD_EXPECT(list.ShouldBlock("https://ads.example.net/x", "https://site.test/"));
  CAD_EXPECT(
      list.ShouldBlock("https://beacon.example.net/x", "https://site.test/"));
}
