#include "cad_test.h"

#include "browser/discard_policy.h"

#include <algorithm>

namespace {

using cad::ChooseTabsToDiscard;
using cad::DiscardCandidate;
using cad::DiscardDecision;
using cad::DiscardInputs;
using cad::DiscardReason;
using cad::DiscardThresholds;
using cad::IsDiscardEligible;

constexpr uint64_t kMB = 1024ull * 1024ull;
constexpr uint64_t kGB = 1024ull * kMB;

DiscardCandidate MakeTab(int id, uint64_t bytes, uint64_t idle_seconds) {
  DiscardCandidate tab;
  tab.tab_id = id;
  tab.renderer_bytes = bytes;
  tab.idle_seconds = idle_seconds;
  tab.age_seconds = 3600;  // old enough to be eligible
  return tab;
}

bool Contains(const std::vector<int>& ids, int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

CAD_TEST(NothingIsDiscardedWhenMemoryIsPlentiful) {
  DiscardInputs inputs;
  inputs.system_usable_bytes = 64 * kGB;
  inputs.system_available_bytes = 40 * kGB;
  inputs.browser_rss_bytes = 2 * kGB;
  inputs.tabs = {MakeTab(1, 300 * kMB, 120), MakeTab(2, 200 * kMB, 60)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(decision.tab_ids.empty());
  CAD_EXPECT(decision.reason == DiscardReason::kNone);
}

CAD_TEST(LowSystemMemoryTriggersDiscard) {
  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 1 * kGB;  // ~6%, below the 12% trigger
  inputs.browser_rss_bytes = 4 * kGB;
  inputs.tabs = {MakeTab(1, 400 * kMB, 600), MakeTab(2, 900 * kMB, 900),
                 MakeTab(3, 100 * kMB, 30)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(!decision.tab_ids.empty());
  CAD_EXPECT(decision.reason == DiscardReason::kSystemMemoryLow);
  // The longest-idle tab goes first.
  CAD_EXPECT_EQ(decision.tab_ids.front(), 2);
  CAD_EXPECT(!decision.explanation.empty());
}

CAD_TEST(HighBrowserMemoryTriggersDiscard) {
  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 8 * kGB;   // system is fine
  inputs.browser_rss_bytes = 11 * kGB;       // but we are hogging it
  inputs.tabs = {MakeTab(1, 900 * kMB, 300)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(!decision.tab_ids.empty());
  CAD_EXPECT(decision.reason == DiscardReason::kBrowserMemoryHigh);
}

CAD_TEST(ProtectedTabsAreNeverDiscarded) {
  DiscardThresholds thresholds;
  DiscardCandidate active = MakeTab(1, 900 * kMB, 999);
  active.is_active = true;
  DiscardCandidate pinned = MakeTab(2, 900 * kMB, 999);
  pinned.pinned = true;
  DiscardCandidate audible = MakeTab(3, 900 * kMB, 999);
  audible.audible = true;
  DiscardCandidate kept = MakeTab(4, 900 * kMB, 999);
  kept.performance_pinned = true;
  DiscardCandidate downloading = MakeTab(5, 900 * kMB, 999);
  downloading.downloading = true;
  DiscardCandidate form = MakeTab(6, 900 * kMB, 999);
  form.has_unsaved_form_data = true;

  for (const DiscardCandidate& tab :
       {active, pinned, audible, kept, downloading, form}) {
    CAD_EXPECT(!IsDiscardEligible(tab, thresholds));
  }

  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 256 * kMB;  // severe pressure
  inputs.browser_rss_bytes = 14 * kGB;
  inputs.tabs = {active, pinned, audible, kept, downloading, form};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, thresholds);
  CAD_EXPECT(decision.tab_ids.empty());
}

CAD_TEST(NewTabsAreNotDiscardedEvenUnderPressure) {
  DiscardCandidate fresh = MakeTab(1, 900 * kMB, 0);
  fresh.age_seconds = 5;  // opened five seconds ago

  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 256 * kMB;
  inputs.browser_rss_bytes = 14 * kGB;
  inputs.tabs = {fresh};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(decision.tab_ids.empty());
}

CAD_TEST(AlreadyDiscardedTabsAreNotDiscardedTwice) {
  DiscardCandidate sleeping = MakeTab(1, 0, 5000);
  sleeping.already_discarded = true;

  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 256 * kMB;
  inputs.browser_rss_bytes = 14 * kGB;
  inputs.tabs = {sleeping};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(decision.tab_ids.empty());
}

CAD_TEST(DiscardStopsOnceTheMemoryTargetIsMet) {
  // 16 GB usable, 1 GB free, target is 20% = 3.2 GB. Freeing two 1.5 GB tabs
  // is enough, so the third must be left alone.
  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 1 * kGB;
  inputs.browser_rss_bytes = 6 * kGB;
  inputs.tabs = {MakeTab(1, 1536 * kMB, 900), MakeTab(2, 1536 * kMB, 800),
                 MakeTab(3, 1536 * kMB, 700)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT_EQ(decision.tab_ids.size(), size_t(2));
  CAD_EXPECT(Contains(decision.tab_ids, 1));
  CAD_EXPECT(Contains(decision.tab_ids, 2));
  CAD_EXPECT(!Contains(decision.tab_ids, 3));
}

CAD_TEST(IdleTabsAreReclaimedOnlyWhenManyTabsAreOpen) {
  DiscardThresholds thresholds;
  DiscardInputs inputs;
  inputs.system_usable_bytes = 64 * kGB;
  inputs.system_available_bytes = 40 * kGB;  // no pressure at all
  inputs.browser_rss_bytes = 3 * kGB;

  // Five tabs, all idle for an hour: below the tab-count trigger, so nothing.
  for (int i = 1; i <= 5; ++i)
    inputs.tabs.push_back(MakeTab(i, 200 * kMB, 3600));
  CAD_EXPECT(ChooseTabsToDiscard(inputs, thresholds).tab_ids.empty());

  // Past the trigger, the long-idle ones are reclaimed.
  for (int i = 6; i <= 25; ++i)
    inputs.tabs.push_back(MakeTab(i, 200 * kMB, 3600));
  const DiscardDecision decision = ChooseTabsToDiscard(inputs, thresholds);
  CAD_EXPECT(!decision.tab_ids.empty());
  CAD_EXPECT(decision.reason == DiscardReason::kIdleWithManyTabs);
}

CAD_TEST(RecentlyUsedTabsSurviveTheIdleSweep) {
  DiscardThresholds thresholds;
  DiscardInputs inputs;
  inputs.system_usable_bytes = 64 * kGB;
  inputs.system_available_bytes = 40 * kGB;
  inputs.browser_rss_bytes = 3 * kGB;

  for (int i = 1; i <= 24; ++i)
    inputs.tabs.push_back(MakeTab(i, 200 * kMB, 3600));
  // One tab used a minute ago must not be swept up with the rest.
  inputs.tabs.push_back(MakeTab(99, 200 * kMB, 60));

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, thresholds);
  CAD_EXPECT(!decision.tab_ids.empty());
  CAD_EXPECT(!Contains(decision.tab_ids, 99));
}

CAD_TEST(PressureWithOneHugeTabStillMakesProgress) {
  // The only eligible tab is smaller than the shortfall. Doing nothing would
  // leave the loop spinning under pressure forever.
  DiscardInputs inputs;
  inputs.system_usable_bytes = 16 * kGB;
  inputs.system_available_bytes = 100 * kMB;
  inputs.browser_rss_bytes = 15 * kGB;
  inputs.tabs = {MakeTab(7, 50 * kMB, 4000)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT_EQ(decision.tab_ids.size(), size_t(1));
  CAD_EXPECT_EQ(decision.tab_ids.front(), 7);
}

CAD_TEST(ZeroUsableMemoryDoesNotTriggerDiscard) {
  // Defensive: if the memory query failed we must not start discarding tabs
  // on the strength of a zero.
  DiscardInputs inputs;
  inputs.system_usable_bytes = 0;
  inputs.system_available_bytes = 0;
  inputs.browser_rss_bytes = 0;
  inputs.tabs = {MakeTab(1, 500 * kMB, 100)};

  const DiscardDecision decision = ChooseTabsToDiscard(inputs, {});
  CAD_EXPECT(decision.tab_ids.empty());
}
