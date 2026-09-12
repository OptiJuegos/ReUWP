#include "shim/winrt_runtime.h"

#include <objbase.h>

namespace {

volatile LONG g_shutdown_cookie = 0;

UINT64 NextShutdownCookie() noexcept {
  LONG value = ::InterlockedIncrement(&g_shutdown_cookie);
  if (value == 0) {
    value = ::InterlockedIncrement(&g_shutdown_cookie);
  }
  return static_cast<UINT64>(static_cast<ULONG>(value));
}

}  // namespace

extern "C" {

HRESULT SHIM_COM RoGetApartmentIdentifier(UINT64* apartment_identifier) {
  if (apartment_identifier == nullptr) {
    return E_POINTER;
  }
  *apartment_identifier = 0;

  ULONG_PTR token = 0;
  const HRESULT result = ::CoGetContextToken(&token);
  if (FAILED(result)) {
    return result;
  }
  *apartment_identifier = static_cast<UINT64>(token);
  return S_OK;
}

HRESULT SHIM_COM RoRegisterForApartmentShutdown(
    IUnknown* callback_object, UINT64* apartment_identifier,
    UINT64* registration_cookie) {
  (void)callback_object;
  if (apartment_identifier == nullptr || registration_cookie == nullptr) {
    return E_POINTER;
  }
  *apartment_identifier = 0;
  *registration_cookie = 0;

  const HRESULT result = RoGetApartmentIdentifier(apartment_identifier);
  if (FAILED(result)) {
    return result;
  }
  *registration_cookie = NextShutdownCookie();
  return S_OK;
}

HRESULT SHIM_COM RoUnregisterForApartmentShutdown(UINT64 registration_cookie) {
  (void)registration_cookie;
  return S_OK;
}

}  // extern "C"
