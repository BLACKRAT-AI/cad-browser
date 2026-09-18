#include "sys/gpu_info.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>

namespace cad::sys {
namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kVendorNvidia = 0x10DE;
constexpr uint32_t kVendorAmd = 0x1002;
constexpr uint32_t kVendorAmdAlt = 0x1022;
constexpr uint32_t kVendorIntel = 0x8086;
constexpr uint32_t kVendorMicrosoft = 0x1414;
constexpr uint32_t kVendorQualcomm = 0x5143;
constexpr uint32_t kVendorArm = 0x13B5;
constexpr uint32_t kVendorApple = 0x106B;

// The user-mode driver version arrives as four packed 16-bit fields.
std::string FormatDriverVersion(LARGE_INTEGER umd) {
  return std::to_string(HIWORD(umd.HighPart)) + "." +
         std::to_string(LOWORD(umd.HighPart)) + "." +
         std::to_string(HIWORD(umd.LowPart)) + "." +
         std::to_string(LOWORD(umd.LowPart));
}

int64_t PackLuid(const LUID& luid) {
  LARGE_INTEGER li;
  li.HighPart = luid.HighPart;
  li.LowPart = luid.LowPart;
  return li.QuadPart;
}

}  // namespace

std::string GpuVendorName(uint32_t vendor_id) {
  switch (vendor_id) {
    case kVendorNvidia: return "NVIDIA";
    case kVendorAmd:
    case kVendorAmdAlt: return "AMD";
    case kVendorIntel: return "Intel";
    case kVendorMicrosoft: return "Microsoft";
    case kVendorQualcomm: return "Qualcomm";
    case kVendorArm: return "ARM";
    case kVendorApple: return "Apple";
    default: return "Unknown";
  }
}

int ChoosePreferredAdapter(const std::vector<GpuAdapter>& adapters) {
  // PLAN.md section 4.2: most dedicated VRAM wins, discrete vendors break a
  // tie, then whichever actually drives a display.
  int best = -1;
  for (size_t i = 0; i < adapters.size(); ++i) {
    const GpuAdapter& candidate = adapters[i];
    if (candidate.is_software)
      continue;
    if (best < 0) {
      best = static_cast<int>(i);
      continue;
    }
    const GpuAdapter& current = adapters[static_cast<size_t>(best)];

    if (candidate.dedicated_video_memory != current.dedicated_video_memory) {
      if (candidate.dedicated_video_memory > current.dedicated_video_memory)
        best = static_cast<int>(i);
      continue;
    }

    auto vendor_rank = [](const GpuAdapter& a) {
      if (a.vendor_id == kVendorNvidia || a.vendor_id == kVendorAmd ||
          a.vendor_id == kVendorAmdAlt) {
        return 2;
      }
      if (a.vendor_id == kVendorIntel)
        return 1;
      return 0;
    };
    const int candidate_rank = vendor_rank(candidate);
    const int current_rank = vendor_rank(current);
    if (candidate_rank != current_rank) {
      if (candidate_rank > current_rank)
        best = static_cast<int>(i);
      continue;
    }

    if (candidate.has_output && !current.has_output)
      best = static_cast<int>(i);
  }

  // Every adapter was software. Fall back to the first one so callers still
  // have something to name.
  if (best < 0 && !adapters.empty())
    best = 0;
  return best;
}

struct GpuMonitor::Impl {
  ComPtr<IDXGIFactory1> factory;
  // Parallel to GpuInfo::adapters. Entries may be null when an adapter does
  // not support IDXGIAdapter3.
  std::vector<ComPtr<IDXGIAdapter3>> adapters3;
};

GpuMonitor::GpuMonitor() : impl_(new Impl()) {}

GpuMonitor::~GpuMonitor() {
  delete impl_;
  impl_ = nullptr;
}

bool GpuMonitor::Initialize() {
  info_ = GpuInfo();
  impl_->adapters3.clear();

  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = ::CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    info_.error = "CreateDXGIFactory1 failed: " +
                  Win32ErrorString(static_cast<DWORD>(hr));
    return false;
  }
  impl_->factory = factory;

  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(i, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(hr))
      break;

    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
      continue;

    GpuAdapter info;
    info.index = static_cast<int>(i);
    info.description = TrimAscii(ToUtf8(desc.Description));
    info.vendor_id = desc.VendorId;
    info.device_id = desc.DeviceId;
    info.subsys_id = desc.SubSysId;
    info.revision = desc.Revision;
    info.vendor_name = GpuVendorName(desc.VendorId);
    info.dedicated_video_memory = desc.DedicatedVideoMemory;
    info.dedicated_system_memory = desc.DedicatedSystemMemory;
    info.shared_system_memory = desc.SharedSystemMemory;
    info.luid = PackLuid(desc.AdapterLuid);
    info.luid_high = desc.AdapterLuid.HighPart;
    info.luid_low = desc.AdapterLuid.LowPart;
    info.is_software =
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
        (desc.VendorId == kVendorMicrosoft && desc.DedicatedVideoMemory == 0);

    for (UINT o = 0;; ++o) {
      ComPtr<IDXGIOutput> output;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
        break;
      if (!output)
        break;
      ++info.output_count;
    }
    info.has_output = info.output_count > 0;

    LARGE_INTEGER umd{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
      info.driver_version = FormatDriverVersion(umd);

    ComPtr<IDXGIAdapter3> adapter3;
    adapter.As(&adapter3);
    impl_->adapters3.push_back(adapter3);

    info_.adapters.push_back(std::move(info));
  }

  if (info_.adapters.empty()) {
    if (info_.error.empty())
      info_.error = "DXGI reported no adapters";
    return false;
  }

  // What Windows itself would pick for a high-performance request. Worth
  // recording separately: when it disagrees with ours, the GPU steering in
  // gpu_preference.cc is the thing that has to bridge the gap.
  ComPtr<IDXGIFactory6> factory6;
  if (SUCCEEDED(factory.As(&factory6)) && factory6) {
    ComPtr<IDXGIAdapter1> high_perf;
    if (SUCCEEDED(factory6->EnumAdapterByGpuPreference(
            0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&high_perf)))) {
      DXGI_ADAPTER_DESC1 desc{};
      if (SUCCEEDED(high_perf->GetDesc1(&desc))) {
        const int64_t luid = PackLuid(desc.AdapterLuid);
        for (size_t i = 0; i < info_.adapters.size(); ++i) {
          if (info_.adapters[i].luid == luid) {
            info_.os_high_performance_index = static_cast<int>(i);
            break;
          }
        }
      }
    }
  }

  info_.preferred_index = ChoosePreferredAdapter(info_.adapters);
  SampleLiveMemory();
  return true;
}

void GpuMonitor::SampleLiveMemory() {
  for (size_t i = 0; i < info_.adapters.size(); ++i) {
    GpuLiveMemory live;
    if (i < impl_->adapters3.size() && impl_->adapters3[i]) {
      DXGI_QUERY_VIDEO_MEMORY_INFO vmi{};
      if (SUCCEEDED(impl_->adapters3[i]->QueryVideoMemoryInfo(
              0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi))) {
        live.valid = true;
        live.budget_bytes = vmi.Budget;
        live.process_usage_bytes = vmi.CurrentUsage;
        live.available_for_reservation_bytes = vmi.AvailableForReservation;
        live.current_reservation_bytes = vmi.CurrentReservation;
      }
    }
    info_.adapters[i].live = live;
  }
}

GpuInfo QueryGpuInfo() {
  GpuMonitor monitor;
  monitor.Initialize();
  return monitor.info();
}

}  // namespace cad::sys
