// windows.h must precede the COM headers; see the note in process_tree.cc.
#include "sys/win_util.h"

#include "sys/wmi_memory.h"

#include <comdef.h>
#include <wbemidl.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace cad::sys {
namespace {

WmiMemoryResult RunQuery() {
  WmiMemoryResult result;

  // This runs on its own thread, so it owns its own COM apartment.
  HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_initialized = SUCCEEDED(hr);
  if (!com_initialized && hr != RPC_E_CHANGED_MODE) {
    result.error = "CoInitializeEx failed: " + Win32ErrorString(static_cast<DWORD>(hr));
    return result;
  }

  struct ComScope {
    bool active;
    ~ComScope() {
      if (active)
        ::CoUninitialize();
    }
  } com_scope{com_initialized};

  IWbemLocator* locator = nullptr;
  hr = ::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator));
  if (FAILED(hr) || !locator) {
    result.error = "CoCreateInstance(WbemLocator) failed";
    return result;
  }

  IWbemServices* services = nullptr;
  BSTR ns = ::SysAllocString(L"ROOT\\CIMV2");
  hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr,
                              nullptr, &services);
  ::SysFreeString(ns);
  locator->Release();
  if (FAILED(hr) || !services) {
    result.error = "IWbemLocator::ConnectServer failed";
    return result;
  }

  hr = ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                           nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  if (FAILED(hr)) {
    services->Release();
    result.error = "CoSetProxyBlanket failed";
    return result;
  }

  BSTR language = ::SysAllocString(L"WQL");
  BSTR query = ::SysAllocString(L"SELECT Capacity FROM Win32_PhysicalMemory");
  IEnumWbemClassObject* enumerator = nullptr;
  hr = services->ExecQuery(
      language, query,
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);
  ::SysFreeString(language);
  ::SysFreeString(query);
  if (FAILED(hr) || !enumerator) {
    services->Release();
    result.error = "WQL query for Win32_PhysicalMemory failed";
    return result;
  }

  for (;;) {
    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    hr = enumerator->Next(2000, 1, &object, &returned);
    if (hr != WBEM_S_NO_ERROR || returned == 0 || !object)
      break;

    VARIANT value;
    ::VariantInit(&value);
    if (SUCCEEDED(object->Get(L"Capacity", 0, &value, nullptr, nullptr))) {
      uint64_t capacity = 0;
      if (value.vt == VT_BSTR && value.bstrVal) {
        // Win32_PhysicalMemory.Capacity is a uint64 exposed as a string.
        capacity = ::_wcstoui64(value.bstrVal, nullptr, 10);
      } else if (value.vt == VT_UI8) {
        capacity = value.ullVal;
      } else if (value.vt == VT_I4) {
        capacity = static_cast<uint64_t>(value.lVal);
      }
      if (capacity > 0) {
        result.total_capacity_bytes += capacity;
        result.module_capacities.push_back(capacity);
        ++result.module_count;
      }
    }
    ::VariantClear(&value);
    object->Release();
  }

  enumerator->Release();
  services->Release();

  result.ok = result.module_count > 0;
  if (!result.ok && result.error.empty())
    result.error = "Win32_PhysicalMemory returned no rows";
  return result;
}

}  // namespace

WmiMemoryResult QueryWmiPhysicalMemory(int timeout_ms) {
  // A promise held by a shared_ptr: if the worker outlives our wait, it writes
  // into storage that stays alive and we simply stop caring about the answer.
  auto promise = std::make_shared<std::promise<WmiMemoryResult>>();
  std::future<WmiMemoryResult> future = promise->get_future();

  std::thread worker([promise]() { promise->set_value(RunQuery()); });
  worker.detach();

  if (future.wait_for(std::chrono::milliseconds(timeout_ms)) ==
      std::future_status::ready) {
    return future.get();
  }

  WmiMemoryResult timed_out;
  timed_out.error =
      "WMI did not answer within " + std::to_string(timeout_ms) + " ms";
  return timed_out;
}

}  // namespace cad::sys
