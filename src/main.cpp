#include "shim/bootstrap.h"
#include "shim/common.h"

extern "C" BOOL SHIM_COM DllMain(HINSTANCE module, DWORD reason,
                                 LPVOID reserved) noexcept {
  (void)module;
  (void)reason;
  (void)reserved;
  return TRUE;
}

extern "C" WPARAM SHIM_COM Win32Bootstrap() noexcept {
  return shim::bootstrap::Run();
}

// A stdcall function with no arguments is decorated as _Win32Bootstrap@0 on
// MSVC x86. Older launchers may resolve that exact
// export name, so ReUWP keeps it as a compatibility alias at ordinal 57.
#pragma comment(linker, "/EXPORT:Win32Bootstrap@0=_Win32Bootstrap@0,@57")
