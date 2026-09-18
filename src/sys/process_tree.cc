// win_util.h pulls in windows.h with the project's WIN32_LEAN_AND_MEAN and
// NOMINMAX settings. The SDK headers below do not include it themselves and
// fail to compile without it, so it has to come first.
#include "sys/win_util.h"

#include "sys/process_tree.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace cad::sys {
namespace {

typedef NTSTATUS(NTAPI* NtQueryInformationProcessFn)(HANDLE,
                                                     PROCESSINFOCLASS,
                                                     PVOID,
                                                     ULONG,
                                                     PULONG);

NtQueryInformationProcessFn GetNtQueryInformationProcess() {
  static NtQueryInformationProcessFn cached = [] {
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
      return static_cast<NtQueryInformationProcessFn>(nullptr);
    return reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void*>(
            ::GetProcAddress(ntdll, "NtQueryInformationProcess")));
  }();
  return cached;
}

uint64_t FileTimeTo100ns(const FILETIME& ft) {
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

uint64_t NowIn100ns() {
  FILETIME ft{};
  ::GetSystemTimeAsFileTime(&ft);
  return FileTimeTo100ns(ft);
}

// Reads another process's command line out of its PEB. The structures used
// here are the documented subset in winternl.h, so the field offsets come from
// the SDK rather than from hard-coded constants.
std::string ReadProcessCommandLine(HANDLE process) {
  const auto query = GetNtQueryInformationProcess();
  if (!query)
    return {};

  PROCESS_BASIC_INFORMATION basic{};
  ULONG returned = 0;
  if (query(process, ProcessBasicInformation, &basic, sizeof(basic),
            &returned) != 0) {
    return {};
  }
  if (!basic.PebBaseAddress)
    return {};

  PEB peb{};
  if (!::ReadProcessMemory(process, basic.PebBaseAddress, &peb, sizeof(peb),
                           nullptr)) {
    return {};
  }
  if (!peb.ProcessParameters)
    return {};

  RTL_USER_PROCESS_PARAMETERS params{};
  if (!::ReadProcessMemory(process, peb.ProcessParameters, &params,
                           sizeof(params), nullptr)) {
    return {};
  }
  if (!params.CommandLine.Buffer || params.CommandLine.Length == 0)
    return {};

  // Length is in bytes and excludes the terminator.
  const size_t chars = params.CommandLine.Length / sizeof(wchar_t);
  if (chars == 0 || chars > 64 * 1024)
    return {};

  std::wstring buffer(chars, L'\0');
  if (!::ReadProcessMemory(process, params.CommandLine.Buffer, buffer.data(),
                           params.CommandLine.Length, nullptr)) {
    return {};
  }
  return ToUtf8(buffer);
}

std::string SwitchValue(const std::string& command_line,
                        const std::string& name) {
  const std::string token = "--" + name + "=";
  const size_t pos = command_line.find(token);
  if (pos == std::string::npos)
    return {};
  size_t start = pos + token.size();
  if (start >= command_line.size())
    return {};

  char terminator = ' ';
  if (command_line[start] == '"') {
    terminator = '"';
    ++start;
  }
  const size_t end = command_line.find(terminator, start);
  if (end == std::string::npos)
    return command_line.substr(start);
  return command_line.substr(start, end - start);
}

// "network.mojom.NetworkService" -> "Network"
std::string PrettyUtilitySubType(const std::string& raw) {
  if (raw.empty())
    return {};
  const size_t dot = raw.find('.');
  std::string head = dot == std::string::npos ? raw : raw.substr(0, dot);
  if (head.empty())
    return raw;
  head[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(head[0])));
  return head;
}

}  // namespace

const char* ProcessKindName(ProcessKind kind) {
  switch (kind) {
    case ProcessKind::kBrowser: return "Browser";
    case ProcessKind::kRenderer: return "Renderer";
    case ProcessKind::kGpu: return "GPU";
    case ProcessKind::kUtility: return "Utility";
    case ProcessKind::kCrashpad: return "Crash handler";
    case ProcessKind::kUnknown:
    default: return "Unknown";
  }
}

ProcessKind ClassifyCommandLine(const std::string& command_line,
                                std::string* sub_type_out) {
  if (sub_type_out)
    sub_type_out->clear();

  const std::string type = SwitchValue(command_line, "type");
  if (type.empty()) {
    // No --type switch: this is the browser process itself.
    return ProcessKind::kBrowser;
  }
  if (type == "renderer")
    return ProcessKind::kRenderer;
  if (type == "gpu-process")
    return ProcessKind::kGpu;
  if (type == "crashpad-handler")
    return ProcessKind::kCrashpad;
  if (type == "utility") {
    if (sub_type_out) {
      *sub_type_out =
          PrettyUtilitySubType(SwitchValue(command_line, "utility-sub-type"));
    }
    return ProcessKind::kUtility;
  }
  return ProcessKind::kUnknown;
}

uint64_t ProcessTreeSnapshot::WorkingSetFor(uint32_t pid) const {
  const ProcessEntry* entry = Find(pid);
  return entry ? entry->working_set_bytes : 0;
}

const ProcessEntry* ProcessTreeSnapshot::Find(uint32_t pid) const {
  for (const ProcessEntry& entry : processes) {
    if (entry.pid == pid)
      return &entry;
  }
  return nullptr;
}

ProcessTreeMonitor::ProcessTreeMonitor() {
  SYSTEM_INFO info{};
  ::GetNativeSystemInfo(&info);
  logical_processors_ = static_cast<int>(info.dwNumberOfProcessors);
  if (logical_processors_ < 1)
    logical_processors_ = 1;
}

ProcessTreeMonitor::~ProcessTreeMonitor() {
  for (const auto& [pid, handle] : handle_cache_) {
    ::CloseHandle(static_cast<HANDLE>(handle));
  }
  handle_cache_.clear();
}

bool ProcessTreeMonitor::Sample(ProcessTreeSnapshot* out) {
  if (!out)
    return false;
  *out = ProcessTreeSnapshot();

  const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return false;

  // Build the parent map once, then walk down from our own pid. Chromium
  // children are direct children of the browser process, but collecting
  // transitively costs nothing and survives any future indirection.
  std::map<uint32_t, uint32_t> parent_of;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (::Process32FirstW(snapshot, &entry)) {
    do {
      parent_of[entry.th32ProcessID] = entry.th32ParentProcessID;
    } while (::Process32NextW(snapshot, &entry));
  }
  ::CloseHandle(snapshot);

  const uint32_t self = ::GetCurrentProcessId();
  std::set<uint32_t> family{self};
  // Repeat until nothing new is added; the process list is small.
  bool grew = true;
  while (grew) {
    grew = false;
    for (const auto& [pid, parent] : parent_of) {
      if (family.count(pid) == 0 && family.count(parent) != 0) {
        family.insert(pid);
        grew = true;
      }
    }
  }

  const uint64_t now = NowIn100ns();
  const uint64_t elapsed =
      previous_sample_100ns_ == 0 ? 0 : now - previous_sample_100ns_;

  std::map<uint32_t, CpuSnapshot> current_cpu;

  for (const uint32_t pid : family) {
    // Reuse the handle opened on a previous sample. A handle stays valid for
    // the life of the process, and reopening seven of them several times a
    // second was measurable overhead for no benefit.
    HANDLE process = nullptr;
    const auto cached_handle = handle_cache_.find(pid);
    if (cached_handle != handle_cache_.end()) {
      process = static_cast<HANDLE>(cached_handle->second);
    } else {
      process = ::OpenProcess(
          PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
      if (process) {
        handle_cache_[pid] = process;
      }
    }
    if (!process)
      continue;

    ProcessEntry info;
    info.pid = pid;
    const auto parent_it = parent_of.find(pid);
    info.parent_pid = parent_it == parent_of.end() ? 0 : parent_it->second;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(
            process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
      info.working_set_bytes = counters.WorkingSetSize;
      info.private_bytes = counters.PrivateUsage;
    }

    FILETIME created{}, exited{}, kernel{}, user{};
    if (::GetProcessTimes(process, &created, &exited, &kernel, &user)) {
      CpuSnapshot cpu;
      cpu.kernel_100ns = FileTimeTo100ns(kernel);
      cpu.user_100ns = FileTimeTo100ns(user);
      current_cpu[pid] = cpu;

      const auto previous = previous_cpu_.find(pid);
      if (previous != previous_cpu_.end() && elapsed > 0) {
        const uint64_t before =
            previous->second.kernel_100ns + previous->second.user_100ns;
        const uint64_t after = cpu.kernel_100ns + cpu.user_100ns;
        if (after > before) {
          const double busy = static_cast<double>(after - before);
          const double window =
              static_cast<double>(elapsed) * logical_processors_;
          info.cpu_percent = std::clamp(busy / window * 100.0, 0.0, 100.0);
        }
      }
    }

    // Classification is stable for the life of a process, so it is read once.
    const auto cached = classification_cache_.find(pid);
    if (cached != classification_cache_.end()) {
      info.kind = cached->second.first;
      info.sub_type = cached->second.second;
    } else {
      const std::string command_line =
          pid == self ? std::string() : ReadProcessCommandLine(process);
      if (pid == self) {
        info.kind = ProcessKind::kBrowser;
      } else if (command_line.empty()) {
        info.kind = ProcessKind::kUnknown;
      } else {
        info.kind = ClassifyCommandLine(command_line, &info.sub_type);
      }
      classification_cache_[pid] = {info.kind, info.sub_type};
    }

    info.label = ProcessKindName(info.kind);
    if (info.kind == ProcessKind::kUtility && !info.sub_type.empty())
      info.label = info.sub_type + " utility";


    if (info.kind == ProcessKind::kRenderer)
      ++out->renderer_count;
    out->total_working_set_bytes += info.working_set_bytes;
    out->total_private_bytes += info.private_bytes;
    out->total_cpu_percent += info.cpu_percent;
    out->pids.insert(pid);
    out->processes.push_back(std::move(info));
  }

  // Forget processes that have exited so the caches cannot grow without bound.
  for (auto it = handle_cache_.begin(); it != handle_cache_.end();) {
    if (family.count(it->first) == 0) {
      ::CloseHandle(static_cast<HANDLE>(it->second));
      it = handle_cache_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = classification_cache_.begin();
       it != classification_cache_.end();) {
    it = family.count(it->first) == 0 ? classification_cache_.erase(it)
                                      : std::next(it);
  }

  previous_cpu_ = std::move(current_cpu);
  previous_sample_100ns_ = now;

  std::sort(out->processes.begin(), out->processes.end(),
            [](const ProcessEntry& a, const ProcessEntry& b) {
              if (a.kind != b.kind)
                return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              return a.working_set_bytes > b.working_set_bytes;
            });

  out->total_cpu_percent =
      std::clamp(out->total_cpu_percent, 0.0, 100.0 * logical_processors_);
  return true;
}

}  // namespace cad::sys
