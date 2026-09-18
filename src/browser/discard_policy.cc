#include "browser/discard_policy.h"

#include <algorithm>

namespace cad {
namespace {

std::string FormatMegabytes(uint64_t bytes) {
  return std::to_string(bytes / (1024ull * 1024ull)) + " MB";
}

std::string FormatPercent(double fraction) {
  return std::to_string(static_cast<int>(fraction * 100.0 + 0.5)) + "%";
}

}  // namespace

const char* DiscardReasonName(DiscardReason reason) {
  switch (reason) {
    case DiscardReason::kSystemMemoryLow: return "system memory low";
    case DiscardReason::kBrowserMemoryHigh: return "browser memory high";
    case DiscardReason::kIdleWithManyTabs: return "idle with many tabs";
    case DiscardReason::kNone:
    default: return "none";
  }
}

bool IsDiscardEligible(const DiscardCandidate& tab,
                       const DiscardThresholds& thresholds) {
  if (tab.already_discarded)
    return false;
  if (tab.is_active)
    return false;
  if (tab.pinned || tab.audible || tab.performance_pinned)
    return false;
  if (tab.downloading || tab.has_unsaved_form_data)
    return false;
  // A tab that has only just been created is almost certainly one the user is
  // about to look at, whatever its idle time says.
  if (tab.age_seconds < thresholds.min_age_seconds)
    return false;
  return true;
}

DiscardDecision ChooseTabsToDiscard(const DiscardInputs& inputs,
                                    const DiscardThresholds& thresholds) {
  DiscardDecision decision;

  std::vector<const DiscardCandidate*> eligible;
  eligible.reserve(inputs.tabs.size());
  for (const DiscardCandidate& tab : inputs.tabs) {
    if (IsDiscardEligible(tab, thresholds))
      eligible.push_back(&tab);
  }
  if (eligible.empty())
    return decision;

  // Longest idle first, and among equally idle tabs the biggest one, so each
  // discard buys as much memory as possible.
  std::sort(eligible.begin(), eligible.end(),
            [](const DiscardCandidate* a, const DiscardCandidate* b) {
              if (a->idle_seconds != b->idle_seconds)
                return a->idle_seconds > b->idle_seconds;
              return a->renderer_bytes > b->renderer_bytes;
            });

  const uint64_t usable = inputs.system_usable_bytes;
  const bool system_low =
      usable > 0 &&
      static_cast<double>(inputs.system_available_bytes) <
          static_cast<double>(usable) * thresholds.available_fraction_trigger;
  const bool browser_high =
      usable > 0 &&
      static_cast<double>(inputs.browser_rss_bytes) >
          static_cast<double>(usable) * thresholds.browser_rss_fraction_trigger;

  if (system_low || browser_high) {
    decision.reason = system_low ? DiscardReason::kSystemMemoryLow
                                 : DiscardReason::kBrowserMemoryHigh;

    // Discard until the projected free memory reaches the target, rather than
    // dumping every background tab the moment pressure appears.
    const uint64_t target = static_cast<uint64_t>(
        static_cast<double>(usable) * thresholds.available_fraction_target);
    uint64_t projected = inputs.system_available_bytes;

    for (const DiscardCandidate* tab : eligible) {
      if (projected >= target)
        break;
      decision.tab_ids.push_back(tab->tab_id);
      projected += tab->renderer_bytes;
    }

    // Pressure with nothing worth reclaiming still means something is wrong;
    // take the single largest candidate so the loop makes progress.
    if (decision.tab_ids.empty())
      decision.tab_ids.push_back(eligible.front()->tab_id);

    decision.explanation =
        std::string(DiscardReasonName(decision.reason)) + ": " +
        FormatMegabytes(inputs.system_available_bytes) + " free of " +
        FormatMegabytes(usable) + " (threshold " +
        FormatPercent(system_low ? thresholds.available_fraction_trigger
                                 : thresholds.browser_rss_fraction_trigger) +
        "), browser holding " + FormatMegabytes(inputs.browser_rss_bytes) +
        "; discarding " + std::to_string(decision.tab_ids.size()) + " tab(s)";
    return decision;
  }

  // No memory pressure. Reclaim long-idle tabs only once the window has enough
  // tabs open that the user is unlikely to come back to all of them.
  if (static_cast<int>(inputs.tabs.size()) > thresholds.idle_tab_count_trigger) {
    for (const DiscardCandidate* tab : eligible) {
      if (tab->idle_seconds >= thresholds.idle_seconds_trigger)
        decision.tab_ids.push_back(tab->tab_id);
    }
    if (!decision.tab_ids.empty()) {
      decision.reason = DiscardReason::kIdleWithManyTabs;
      decision.explanation =
          std::to_string(decision.tab_ids.size()) + " tab(s) idle for over " +
          std::to_string(thresholds.idle_seconds_trigger / 60) +
          " minutes with " + std::to_string(inputs.tabs.size()) + " tabs open";
    }
  }

  return decision;
}

}  // namespace cad
