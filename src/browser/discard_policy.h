// CAD Browser - when to put a background tab to sleep.
//
// PLAN.md section 6.3. Alloy style has none of Chrome's Memory Saver, so the
// browser needs its own policy. It lives here, apart from CEF, because the
// decision is the part worth testing: getting it wrong either wastes memory or
// throws away a tab someone was using.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cad {

struct DiscardCandidate {
  int tab_id = 0;
  bool is_active = false;
  bool pinned = false;
  bool audible = false;
  // "Keep at full speed" - the user marked this tab as one that must not be
  // slowed down or unloaded.
  bool performance_pinned = false;
  bool downloading = false;
  bool has_unsaved_form_data = false;
  bool already_discarded = false;
  uint64_t renderer_bytes = 0;
  uint64_t idle_seconds = 0;
  uint64_t age_seconds = 0;
};

struct DiscardThresholds {
  // Discard when free memory falls below this share of usable RAM.
  double available_fraction_trigger = 0.12;
  // ...or when the browser itself is holding this share of usable RAM.
  double browser_rss_fraction_trigger = 0.60;
  // Free memory to aim for once discarding has started.
  double available_fraction_target = 0.20;
  // A tab idle this long is a candidate once the window is busy.
  uint64_t idle_seconds_trigger = 30 * 60;
  // ...but only once there are more than this many tabs open.
  int idle_tab_count_trigger = 20;
  // Never discard a tab this new: it was almost certainly just opened.
  uint64_t min_age_seconds = 60;
};

enum class DiscardReason {
  kNone,
  kSystemMemoryLow,
  kBrowserMemoryHigh,
  kIdleWithManyTabs,
};

const char* DiscardReasonName(DiscardReason reason);

struct DiscardInputs {
  uint64_t system_available_bytes = 0;
  uint64_t system_usable_bytes = 0;
  uint64_t browser_rss_bytes = 0;
  std::vector<DiscardCandidate> tabs;
};

struct DiscardDecision {
  std::vector<int> tab_ids;
  DiscardReason reason = DiscardReason::kNone;
  // One line suitable for a log or the System page.
  std::string explanation;
};

// True when the tab may be discarded at all, regardless of memory pressure.
bool IsDiscardEligible(const DiscardCandidate& tab,
                       const DiscardThresholds& thresholds);

DiscardDecision ChooseTabsToDiscard(const DiscardInputs& inputs,
                                    const DiscardThresholds& thresholds);

}  // namespace cad
