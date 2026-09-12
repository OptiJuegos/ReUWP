#include "shim/stdio_overrides.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::stdio_overrides {
namespace {

using FopenFn = FILE*(__cdecl*)(const char* path, const char* mode);
using WfopenFn = FILE*(__cdecl*)(const wchar_t* path, const wchar_t* mode);
using WfopenSFn = errno_t(__cdecl*)(FILE** stream, const wchar_t* path,
                                   const wchar_t* mode);
using VfscanfFn = int(__cdecl*)(unsigned long long options, FILE* stream,
                                const char* format, void* locale, va_list args);
using VsscanfFn = int(__cdecl*)(unsigned long long options, const char* buffer,
                                size_t buffer_count, const char* format,
                                void* locale, va_list args);

FopenFn g_original_fopen_115 = nullptr;
WfopenFn g_original_wfopen_115 = nullptr;
WfopenSFn g_original_wfopen_s_115 = nullptr;
VfscanfFn g_original_vfscanf_115 = nullptr;
VsscanfFn g_original_vsscanf_115 = nullptr;
LONG g_open_sequence_115 = 0;
LONG g_scan_sequence_115 = 0;

FILE* __cdecl Fopen115(const char* path, const char* mode) noexcept {
  if (g_original_fopen_115 == nullptr) {
    return nullptr;
  }
  FILE* const result = g_original_fopen_115(path, mode);
  const LONG sequence = ::InterlockedIncrement(&g_open_sequence_115);
  log::Writef("1.1.5 fopen #%ld path='%s' mode='%s' -> %p", sequence,
              path != nullptr ? path : "(null)",
              mode != nullptr ? mode : "(null)", result);
  return result;
}

FILE* __cdecl Wfopen115(const wchar_t* path, const wchar_t* mode) noexcept {
  if (g_original_wfopen_115 == nullptr) {
    return nullptr;
  }
  FILE* const result = g_original_wfopen_115(path, mode);
  const LONG sequence = ::InterlockedIncrement(&g_open_sequence_115);
  log::Writef("1.1.5 _wfopen #%ld -> %p", sequence, result);
  return result;
}

errno_t __cdecl WfopenS115(FILE** stream, const wchar_t* path,
                           const wchar_t* mode) noexcept {
  if (g_original_wfopen_s_115 == nullptr) {
    return EINVAL;
  }
  const errno_t result = g_original_wfopen_s_115(stream, path, mode);
  const LONG sequence = ::InterlockedIncrement(&g_open_sequence_115);
  log::Writef("1.1.5 _wfopen_s #%ld result=%d stream_out=%p", sequence,
              static_cast<int>(result), static_cast<void*>(stream));
  return result;
}

int __cdecl Vfscanf115(unsigned long long options, FILE* stream,
                       const char* format, void* locale, va_list args) noexcept {
  if (g_original_vfscanf_115 == nullptr) {
    return -1;
  }
  const int result =
      g_original_vfscanf_115(options, stream, format, locale, args);
  log::Writef("1.1.5 vfscanf stream=%p result=%d format='%s'", stream, result,
              format != nullptr ? format : "(null)");
  return result;
}

int __cdecl Vsscanf115(unsigned long long options, const char* buffer,
                       size_t buffer_count, const char* format, void* locale,
                       va_list args) noexcept {
  if (g_original_vsscanf_115 == nullptr) {
    return -1;
  }
  const int result = g_original_vsscanf_115(options, buffer, buffer_count,
                                             format, locale, args);
  const LONG sequence = ::InterlockedIncrement(&g_scan_sequence_115);
  log::Writef("1.1.5 sscanf #%ld result=%d input='%s' format='%s'", sequence,
              result, buffer != nullptr ? buffer : "(null)",
              format != nullptr ? format : "(null)");
  return result;
}

template <typename Fn>
bool InstallSlot(DWORD rva, const void* replacement, Fn* original,
                 const char* name) noexcept {
  void* slot = game::Resolve(rva);
  if (slot == nullptr) {
    return false;
  }
  void* previous = nullptr;
  if (!hooks::ReplacePointer(static_cast<void**>(slot), replacement, &previous,
                             name)) {
    return false;
  }
  *original = reinterpret_cast<Fn>(previous);
  return true;
}

void RestoreSlot(DWORD rva, const void* original, const char* name) noexcept {
  if (original == nullptr) {
    return;
  }
  if (void* slot = game::Resolve(rva)) {
    void* ignored = nullptr;
    hooks::ReplacePointer(static_cast<void**>(slot), original, &ignored, name);
  }
}

}  // namespace

bool InstallStdioHooks() noexcept {
  if (!game::HasCapability(game::VersionCapability::kStdioOverrides)) {
    return true;
  }
  if (!log::IsEnabled()) {
    return true;
  }
  const game::VersionProfile* const profile = game::CurrentProfile();
  const game::StdioPatchCatalog* const patches =
      profile != nullptr ? profile->stdio_patches : nullptr;
  if (patches == nullptr) {
    log::Write("stdio hooks enabled without a patch catalog");
    return false;
  }

  if (!InstallSlot(patches->fopen_iat_rva,
                   reinterpret_cast<const void*>(&Fopen115),
                   &g_original_fopen_115, "1.1.5 fopen")) {
    return false;
  }
  if (!InstallSlot(patches->wfopen_s_iat_rva,
                   reinterpret_cast<const void*>(&WfopenS115),
                   &g_original_wfopen_s_115, "1.1.5 _wfopen_s")) {
    RestoreSlot(patches->fopen_iat_rva,
                reinterpret_cast<const void*>(g_original_fopen_115),
                "1.1.5 fopen rollback");
    g_original_fopen_115 = nullptr;
    return false;
  }
  if (!InstallSlot(patches->wfopen_iat_rva,
                   reinterpret_cast<const void*>(&Wfopen115),
                   &g_original_wfopen_115, "1.1.5 _wfopen")) {
    RestoreSlot(patches->wfopen_s_iat_rva,
                reinterpret_cast<const void*>(g_original_wfopen_s_115),
                "1.1.5 _wfopen_s rollback");
    RestoreSlot(patches->fopen_iat_rva,
                reinterpret_cast<const void*>(g_original_fopen_115),
                "1.1.5 fopen rollback");
    g_original_wfopen_s_115 = nullptr;
    g_original_fopen_115 = nullptr;
    return false;
  }
  if (!InstallSlot(patches->vfscanf_iat_rva,
                   reinterpret_cast<const void*>(&Vfscanf115),
                   &g_original_vfscanf_115, "1.1.5 __stdio_common_vfscanf")) {
    RestoreSlot(patches->wfopen_iat_rva,
                reinterpret_cast<const void*>(g_original_wfopen_115),
                "1.1.5 _wfopen rollback");
    RestoreSlot(patches->wfopen_s_iat_rva,
                reinterpret_cast<const void*>(g_original_wfopen_s_115),
                "1.1.5 _wfopen_s rollback");
    RestoreSlot(patches->fopen_iat_rva,
                reinterpret_cast<const void*>(g_original_fopen_115),
                "1.1.5 fopen rollback");
    g_original_wfopen_115 = nullptr;
    g_original_wfopen_s_115 = nullptr;
    g_original_fopen_115 = nullptr;
    return false;
  }
  if (!InstallSlot(patches->vsscanf_iat_rva,
                   reinterpret_cast<const void*>(&Vsscanf115),
                   &g_original_vsscanf_115, "1.1.5 __stdio_common_vsscanf")) {
    RestoreSlot(patches->vfscanf_iat_rva,
                reinterpret_cast<const void*>(g_original_vfscanf_115),
                "1.1.5 vfscanf rollback");
    RestoreSlot(patches->wfopen_iat_rva,
                reinterpret_cast<const void*>(g_original_wfopen_115),
                "1.1.5 _wfopen rollback");
    RestoreSlot(patches->wfopen_s_iat_rva,
                reinterpret_cast<const void*>(g_original_wfopen_s_115),
                "1.1.5 _wfopen_s rollback");
    RestoreSlot(patches->fopen_iat_rva,
                reinterpret_cast<const void*>(g_original_fopen_115),
                "1.1.5 fopen rollback");
    g_original_vfscanf_115 = nullptr;
    g_original_wfopen_115 = nullptr;
    g_original_wfopen_s_115 = nullptr;
    g_original_fopen_115 = nullptr;
    return false;
  }

  log::Write("Minecraft 1.1.5 CRT stdio/JSON trace installed");
  return true;
}

}  // namespace shim::stdio_overrides
