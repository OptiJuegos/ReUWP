// Win8+/UWP imports that have no direct Windows 7 export.
//
// This file deliberately targets the Win7 declarations so the SDK does not
// mark the compatibility function names as dllimport. The ABI is still the
// stock x86 Win32 ABI expected by Minecraft's import table.
#undef _WIN32_WINNT
#undef WINVER
#undef NTDDI_VERSION
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define NTDDI_VERSION 0x06010000

#include "shim/common.h"

#include <hstring.h>
#include <objbase.h>

namespace {

using GameUICompletionRoutine = void(WINAPI*)(HRESULT result, void* context);

}  // namespace

extern "C" {

// Win8's CoCreateInstanceFromApp is intentionally close to CoCreateInstanceEx.
// On desktop Win7 we use the normal COM class registration rules; callers still
// receive per-interface HRESULTs through MULTI_QI exactly as expected.
HRESULT SHIM_COM CoCreateInstanceFromApp(REFCLSID clsid, IUnknown* outer,
                                         DWORD cls_context, void* reserved,
                                         DWORD count, MULTI_QI* results) {
  if (reserved != nullptr) {
    return E_INVALIDARG;
  }
  return ::CoCreateInstanceEx(clsid, outer, cls_context, nullptr, count,
                              results);
}

// EventSetInformation itself was introduced in Windows 8. TraceLogging treats
// ERROR_NOT_SUPPORTED as the normal downlevel result, while the other ETW
// imports are redirected directly to ADVAPI32 by the patcher.
ULONG SHIM_COM EventSetInformation(ULONGLONG /*registration_handle*/,
                                   ULONG /*information_class*/,
                                   void* /*event_information*/,
                                   ULONG /*information_length*/) {
  return ERROR_NOT_SUPPORTED;
}

// Xbox title-callable UI is optional integration. On Win7 there is no profile
// card surface, so complete the request synchronously as a harmless no-op.
HRESULT SHIM_COM ProcessPendingGameUI(BOOL /*wait_for_completion*/) {
  return S_OK;
}

HRESULT SHIM_COM ShowProfileCardUI(HSTRING /*target_user_xuid*/,
                                   GameUICompletionRoutine /*completion*/,
                                   void* /*context*/) {
  // Preserve the historical ReUWP hook semantics: the surface does not exist
  // on the desktop host and callers receive the same explicit unsupported
  // result before and after bootstrap replaces the IAT slot.
  return E_NOTIMPL;
}

// ReUWP supplies the WinRT projections used by the game, so the OS WinRT
// apartment is not a prerequisite on the Win7 path.
HRESULT SHIM_COM RoInitialize(int /*init_type*/) {
  return S_OK;
}

// Restricted error info is diagnostic state for language projections. ReUWP's
// crash reporting already records unhandled failures; no system restricted
// error object exists on Win7.
HRESULT SHIM_COM SetRestrictedErrorInfo(IUnknown* /*restricted_error*/) {
  return S_OK;
}

HRESULT SHIM_COM RoReportUnhandledError(IUnknown* /*restricted_error*/) {
  return S_OK;
}

void SHIM_COM RoFailFastWithErrorContext(HRESULT error) {
  // Match fail-fast semantics: a debugger can observe the non-continuable
  // exception, but the process cannot resume into undefined state.
  ::RaiseException(static_cast<DWORD>(error), EXCEPTION_NONCONTINUABLE, 0,
                   nullptr);
  ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(error));
}

}  // extern "C"
