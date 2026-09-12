#include "shim/winrt_runtime.h"

#include <objbase.h>

#include "shim/log.h"

namespace shim::winrt {

HRESULT InitializeApartment(RuntimeInitType type) noexcept {
  DWORD flags = 0;
  switch (type) {
    case RuntimeInitType::kSingleThreaded:
      flags = COINIT_APARTMENTTHREADED;
      break;
    case RuntimeInitType::kMultiThreaded:
      flags = COINIT_MULTITHREADED;
      break;
    default:
      return E_INVALIDARG;
  }

  return ::CoInitializeEx(nullptr, flags);
}

void UninitializeApartment() noexcept {
  ULONG_PTR token = 0;
  if (SUCCEEDED(::CoGetContextToken(&token))) {
    ::CoUninitialize();
  }
}

HRESULT CheckApartmentInitialized() noexcept {
  ULONG_PTR token = 0;
  return ::CoGetContextToken(&token);
}

HRESULT InitializeRuntime() noexcept {
  const HRESULT result =
      InitializeApartment(RuntimeInitType::kMultiThreaded);
  log::WriteHResult("ReUWP RoInitialize(RO_INIT_MULTITHREADED)", result);
  return result;
}

}  // namespace shim::winrt
