// CAD Browser - prints everything the sys/ layer detects.
//
// This is the acceptance tool for milestone M1 in PLAN.md. Its output has to
// agree with Task Manager, dxdiag and the BIOS before the browser UI is built
// on top of these numbers. Run it, read it, check it against those three.

#include <cstdio>
#include <string>

#include "sys/cpu_info.h"
#include "sys/gpu_info.h"
#include "sys/gpu_preference.h"
#include "sys/memory_info.h"
#include "sys/os_info.h"
#include "sys/perf_counters.h"
#include "sys/process_tree.h"
#include "sys/smbios.h"
#include "sys/win_util.h"
#include "sys/wmi_memory.h"

namespace {

using namespace cad::sys;

void Rule(const char* title) {
  std::printf("\n");
  std::printf("== %s ", title);
  const size_t used = std::string(title).size() + 4;
  for (size_t i = used; i < 78; ++i)
    std::printf("=");
  std::printf("\n");
}

void Field(const char* name, const std::string& value) {
  std::printf("  %-26s %s\n", name, value.c_str());
}

void Field(const char* name, const char* value) {
  std::printf("  %-26s %s\n", name, value);
}

std::string Mts(uint32_t value) {
  if (value == 0)
    return "unknown";
  return std::to_string(value) + " MT/s";
}

void PrintOs() {
  Rule("System");
  const OsInfo os = QueryOsInfo();
  Field("Windows", os.Describe());
  if (!os.edition.empty())
    Field("Edition", os.edition);

  const MachineInfo machine = QueryMachineInfo();
  if (!machine.manufacturer.empty() || !machine.model.empty())
    Field("Machine", machine.manufacturer + " " + machine.model);
  if (!machine.baseboard_product.empty()) {
    Field("Baseboard",
          machine.baseboard_manufacturer + " " + machine.baseboard_product);
  }
  if (!machine.bios_version.empty()) {
    Field("BIOS", machine.bios_vendor + " " + machine.bios_version + " (" +
                      machine.bios_date + ")");
  }
}

void PrintCpu() {
  Rule("CPU");
  const CpuInfo cpu = QueryCpuInfo();
  if (!cpu.error.empty())
    Field("Error", cpu.error);
  Field("Model", cpu.brand.empty() ? "unknown" : cpu.brand);
  Field("Vendor", cpu.vendor);
  Field("Cores",
        std::to_string(cpu.physical_cores) + " physical, " +
            std::to_string(cpu.logical_processors) + " logical");
  if (cpu.hybrid) {
    Field("Hybrid topology",
          std::to_string(cpu.performance_cores) + " performance + " +
              std::to_string(cpu.efficiency_cores) + " efficiency");
  }
  if (cpu.base_mhz > 0)
    Field("Base clock", std::to_string(cpu.base_mhz) + " MHz");
  Field("NUMA nodes", std::to_string(cpu.numa_nodes));
  Field("Processor groups", std::to_string(cpu.processor_groups));
  for (int level = 1; level <= 3; ++level) {
    const uint64_t bytes = cpu.cache_bytes(level);
    if (bytes > 0)
      Field(("L" + std::to_string(level) + " cache").c_str(),
            FormatBytes(bytes));
  }
}

void PrintMemory() {
  Rule("Memory");
  const MemoryInfo memory = QueryMemoryInfo();

  Field("Installed",
        FormatBytes(memory.installed_bytes) + "  (source: " +
            InstalledMemorySourceName(memory.installed_source) + ")");
  Field("Usable by Windows", FormatBytes(memory.usable_bytes));
  Field("Hardware reserved", FormatBytes(memory.hardware_reserved_bytes));
  Field("In use now",
        FormatBytes(memory.dynamic.used_physical_bytes) + " of " +
            FormatBytes(memory.dynamic.total_physical_bytes) + "  (" +
            FormatDouble(memory.dynamic.load_percent, 0) + "%)");
  Field("Available", FormatBytes(memory.dynamic.available_physical_bytes));
  Field("Commit",
        FormatBytes(memory.dynamic.commit_total_bytes) + " of " +
            FormatBytes(memory.dynamic.commit_limit_bytes) + "  (peak " +
            FormatBytes(memory.dynamic.commit_peak_bytes) + ")");
  Field("Cached", FormatBytes(memory.dynamic.system_cache_bytes));
  Field("Kernel paged", FormatBytes(memory.dynamic.kernel_paged_bytes));
  Field("Kernel non-paged", FormatBytes(memory.dynamic.kernel_nonpaged_bytes));

  std::printf("\n  -- sources --\n");
  Field("GetPhysicallyInstalled",
        memory.win32_installed_bytes > 0
            ? FormatBytes(memory.win32_installed_bytes)
            : ("unavailable: " + memory.win32_installed_error));
  Field("SMBIOS device sum",
        memory.smbios_sum_bytes > 0 ? FormatBytes(memory.smbios_sum_bytes)
                                    : std::string("unavailable"));
  Field("GlobalMemoryStatusEx", FormatBytes(memory.usable_bytes));

  const WmiMemoryResult wmi = QueryWmiPhysicalMemory(2000);
  if (wmi.ok) {
    const uint64_t a = wmi.total_capacity_bytes;
    const uint64_t b = memory.smbios_sum_bytes;
    const uint64_t diff = a > b ? a - b : b - a;
    Field("WMI Win32_PhysicalMemory",
          FormatBytes(a) + " across " + std::to_string(wmi.module_count) +
              " modules" +
              (diff > 1024 * 1024 ? "   <-- DISAGREES with SMBIOS" : "  (agrees)"));
  } else {
    Field("WMI Win32_PhysicalMemory", "unavailable: " + wmi.error);
  }

  std::printf("\n  -- firmware (SMBIOS %s) --\n",
              memory.smbios_version.empty() ? "unknown version"
                                            : memory.smbios_version.c_str());
  if (memory.array.present) {
    Field("Motherboard maximum",
          FormatBytes(memory.array.max_capacity_bytes) +
              "  (as reported by firmware)");
    Field("Location", memory.array.location);
    Field("Error correction", memory.array.error_correction);
  } else {
    Field("Motherboard maximum", "not reported by firmware");
  }
  Field("Slots",
        std::to_string(memory.slots_used) + " used of " +
            std::to_string(memory.slots_total));
  if (!memory.channel_estimate.empty())
    Field("Channels", memory.channel_estimate);

  if (!memory.modules.empty()) {
    std::printf("\n  %-28s %-9s %-7s %-10s %-10s %-12s %s\n", "SLOT", "SIZE",
                "TYPE", "RATED", "RUNNING", "MANUFACTURER", "PART");
    for (const MemoryModule& m : memory.modules) {
      if (!m.populated()) {
        std::printf("  %-28s %-9s\n",
                    m.device_locator.empty() ? "(slot)"
                                             : m.device_locator.c_str(),
                    "empty");
        continue;
      }
      std::printf("  %-28s %-9s %-7s %-10s %-10s %-12s %s\n",
                  m.device_locator.c_str(), FormatBytes(m.size_bytes, 0).c_str(),
                  m.memory_type.c_str(), Mts(m.speed_mts).c_str(),
                  Mts(m.configured_speed_mts).c_str(), m.manufacturer.c_str(),
                  m.part_number.c_str());
    }
  }

  if (!memory.warnings.empty()) {
    std::printf("\n  -- notes --\n");
    for (const std::string& warning : memory.warnings)
      std::printf("  * %s\n", warning.c_str());
  }
}

void PrintGpu() {
  Rule("GPU");
  const GpuInfo gpu = QueryGpuInfo();
  if (!gpu.error.empty())
    Field("Error", gpu.error);

  for (size_t i = 0; i < gpu.adapters.size(); ++i) {
    const GpuAdapter& a = gpu.adapters[i];
    const bool preferred = static_cast<int>(i) == gpu.preferred_index;
    const bool os_choice = static_cast<int>(i) == gpu.os_high_performance_index;

    std::string marks;
    if (preferred)
      marks += "  [our choice]";
    if (os_choice)
      marks += "  [Windows high-performance]";
    if (a.is_software)
      marks += "  [software]";

    std::printf("\n  Adapter %d: %s%s\n", a.index, a.description.c_str(),
                marks.c_str());
    Field("  Vendor", a.vendor_name + "  (vendor " + FormatHex32(a.vendor_id) +
                          ", device " + FormatHex32(a.device_id) + ")");
    Field("  Dedicated VRAM", FormatBytes(a.dedicated_video_memory));
    Field("  Shared system memory", FormatBytes(a.shared_system_memory));
    Field("  Outputs", std::to_string(a.output_count));
    Field("  Driver (user mode)",
          a.driver_version.empty() ? "unknown" : a.driver_version);
    if (a.live.valid) {
      Field("  This process is using", FormatBytes(a.live.process_usage_bytes) +
                                           " of a " +
                                           FormatBytes(a.live.budget_bytes) +
                                           " budget");
    }
  }

  Rule("GPU preference");
  const GpuPreferenceStatus preference = QueryGpuPreference();
  Field("Executable", preference.executable_path);
  Field("Registry preference", GpuPreferenceName(preference.preference));
  if (!preference.raw_value.empty())
    Field("Raw value", preference.raw_value);
  std::printf(
      "\n  Windows decides the adapter per executable. CAD Browser writes\n"
      "  HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences for its own\n"
      "  exe path; this tool reports its own path, not the browser's.\n");
}

void PrintCounters() {
  Rule("Performance counters");
  PerfCounters counters;
  if (!counters.Initialize()) {
    Field("Status", "unavailable: " + counters.error());
    return;
  }
  Field("GPU counters", counters.gpu_available() ? "available" : "NOT available");
  Field("CPU counters", counters.cpu_available() ? "available" : "NOT available");
  if (!counters.error().empty())
    Field("Note", counters.error());

  // Rate counters need a second collection separated in time.
  ::Sleep(600);
  counters.Sample();
  ::Sleep(600);
  counters.Sample();

  Field("System CPU", FormatDouble(counters.SystemCpuPercent(), 1) + " %");

  const GpuInfo gpu = QueryGpuInfo();
  for (const GpuAdapter& adapter : gpu.adapters) {
    if (adapter.is_software)
      continue;
    const uint64_t used = counters.AdapterDedicatedBytes(adapter.luid);
    std::printf("  %-38s %9s used of %9s, %6s busy\n",
                adapter.description.c_str(), FormatBytes(used).c_str(),
                FormatBytes(adapter.dedicated_video_memory).c_str(),
                (FormatDouble(counters.SystemGpuPercent(adapter.luid), 1) + " %")
                    .c_str());
  }
  std::printf("  (%d GPU engine instances seen)\n",
              static_cast<int>(counters.engine_samples().size()));
}

void PrintProcesses() {
  Rule("Process tree");
  ProcessTreeMonitor monitor;
  ProcessTreeSnapshot snapshot;
  monitor.Sample(&snapshot);
  ::Sleep(400);
  monitor.Sample(&snapshot);

  std::printf("  %-8s %-18s %-12s %-12s %s\n", "PID", "KIND", "WORKING SET",
              "PRIVATE", "CPU");
  for (const ProcessEntry& entry : snapshot.processes) {
    std::printf("  %-8u %-18s %-12s %-12s %s\n", entry.pid,
                entry.label.c_str(), FormatBytes(entry.working_set_bytes).c_str(),
                FormatBytes(entry.private_bytes).c_str(),
                (FormatDouble(entry.cpu_percent, 1) + " %").c_str());
  }
  Field("Total working set", FormatBytes(snapshot.total_working_set_bytes));
}

void PrintSmbiosSummary() {
  Rule("SMBIOS raw");
  const SmbiosTable table = ReadSmbios();
  Field("Parsed", table.ok ? "yes" : "no");
  Field("Version", table.VersionString().empty() ? "unknown"
                                                 : table.VersionString());
  Field("Structures", std::to_string(table.structures.size()));
  if (!table.error.empty())
    Field("Note", table.error);
}

}  // namespace

int main() {
  std::printf("CAD Browser - system report\n");
  std::printf("Compare against Task Manager, dxdiag and the BIOS.\n");

  PrintOs();
  PrintCpu();
  PrintMemory();
  PrintSmbiosSummary();
  PrintGpu();
  PrintCounters();
  PrintProcesses();

  std::printf("\n");
  return 0;
}
