// CAD Browser - our own process tree: who they are and what they cost.
//
// A CEF application is a family of processes sharing one executable. To report
// "browser memory" or "browser GPU %" honestly we have to find every
// descendant of this process and attribute its cost, then classify each one by
// the --type= switch Chromium launched it with.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cad::sys {

enum class ProcessKind {
  kBrowser,
  kRenderer,
  kGpu,
  kUtility,
  kCrashpad,
  kUnknown,
};

const char* ProcessKindName(ProcessKind kind);

struct ProcessEntry {
  uint32_t pid = 0;
  uint32_t parent_pid = 0;
  ProcessKind kind = ProcessKind::kUnknown;
  // For utilities, the service they host: "Network", "Audio", "Storage".
  std::string sub_type;
  // "Renderer", "GPU", "Network utility" - what the UI shows.
  std::string label;
  uint64_t working_set_bytes = 0;
  uint64_t private_bytes = 0;
  double cpu_percent = 0.0;
};

struct ProcessTreeSnapshot {
  std::vector<ProcessEntry> processes;
  std::set<uint32_t> pids;
  uint64_t total_working_set_bytes = 0;
  uint64_t total_private_bytes = 0;
  double total_cpu_percent = 0.0;
  int renderer_count = 0;
  // Working set of one process, for the tab hover card.
  uint64_t WorkingSetFor(uint32_t pid) const;
  const ProcessEntry* Find(uint32_t pid) const;
};

// Samples this process and all of its descendants. CPU percentages need two
// samples to mean anything, so the first call reports 0 for them.
class ProcessTreeMonitor {
 public:
  ProcessTreeMonitor();

  bool Sample(ProcessTreeSnapshot* out);

 private:
  struct CpuSnapshot {
    uint64_t kernel_100ns = 0;
    uint64_t user_100ns = 0;
  };

  std::map<uint32_t, CpuSnapshot> previous_cpu_;
  // Command lines never change, and reading one costs a cross-process read.
  std::map<uint32_t, std::pair<ProcessKind, std::string>> classification_cache_;
  // Open handles, kept between samples. Reopening seven processes on every
  // sample was pure overhead: the handles stay valid for the life of each
  // process and are closed when it disappears.
  std::map<uint32_t, void*> handle_cache_;
  uint64_t previous_sample_100ns_ = 0;
  int logical_processors_ = 1;

 public:
  ~ProcessTreeMonitor();
  ProcessTreeMonitor(const ProcessTreeMonitor&) = delete;
  ProcessTreeMonitor& operator=(const ProcessTreeMonitor&) = delete;
};

// Exposed for tests: maps a Chromium command line to a process kind.
ProcessKind ClassifyCommandLine(const std::string& command_line,
                                std::string* sub_type_out);

}  // namespace cad::sys
