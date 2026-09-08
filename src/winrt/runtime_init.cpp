#include "shim/winrt_runtime.h"

#include <roapi.h>

#include "shim/log.h"

namespace shim::winrt {

HRESULT InitializeRuntime() noexcept {
  using RoInitializeFn = HRESULT(WINAPI*)(RO_INIT_TYPE);

  HMODULE combase = ::GetModuleHandleW(L"combase.dll");
  if (combase == nullptr) {
    combase = ::LoadLibraryW(L"combase.dll");
  }
  if (combase == nullptr) {
    log::Write("RoInitialize unavailable; continuing with ReUWP projections");
    return S_FALSE;
  }

  const auto initialize = reinterpret_cast<RoInitializeFn>(
      ::GetProcAddress(combase, "RoInitialize"));
  if (initialize == nullptr) {
    log::Write("RoInitialize export unavailable; continuing with ReUWP projections");
    return S_FALSE;
  }

  const HRESULT result = initialize(RO_INIT_MULTITHREADED);
  log::WriteHResult("RoInitialize(RO_INIT_MULTITHREADED)", result);
  return result;
}

}  // namespace shim::winrt
