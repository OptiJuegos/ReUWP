#include "compat_internal.h"

#include <stdio.h>
#include <string.h>

#include "shim/compat_hooks.h"
#include "shim/log.h"

namespace shim::hooks::compat_internal {
namespace {

void* const kStandardFileInclude = reinterpret_cast<void*>(1);
char g_include_directory[520] = {};

struct IncludeResolver {
  const void* vtable;
};

HRESULT SHIM_COM IncludeOpen(IncludeResolver* self, int include_type,
                             LPCSTR file_name, LPCVOID parent_data,
                             LPCVOID* out_data, UINT* out_size) noexcept {
  (void)self;
  (void)include_type;
  (void)parent_data;
  if (out_data == nullptr || out_size == nullptr) {
    return E_POINTER;
  }
  *out_data = nullptr;
  *out_size = 0;
  if (file_name == nullptr) {
    return E_POINTER;
  }

  char path[520] = {};
  ::wsprintfA(path, "%s%s", g_include_directory, file_name);

  FILE* file = ::fopen(path, "rb");
  if (file == nullptr) {
    log::Writef("D3DCompile include not found: %s", file_name);
    return E_FAIL;
  }

  ::fseek(file, 0, SEEK_END);
  const long size = ::ftell(file);
  ::fseek(file, 0, SEEK_SET);
  if (size < 0) {
    ::fclose(file);
    return E_FAIL;
  }

  void* buffer =
      ::HeapAlloc(::GetProcessHeap(), 0, static_cast<SIZE_T>(size) + 1);
  if (buffer == nullptr) {
    ::fclose(file);
    return E_OUTOFMEMORY;
  }

  const size_t read = ::fread(buffer, 1, static_cast<size_t>(size), file);
  ::fclose(file);
  if (read != static_cast<size_t>(size)) {
    ::HeapFree(::GetProcessHeap(), 0, buffer);
    return E_FAIL;
  }

  auto* bytes = static_cast<unsigned char*>(buffer);
  bytes[size] = 0;
  *out_data = buffer;
  *out_size = static_cast<UINT>(size);
  return S_OK;
}

HRESULT SHIM_COM IncludeClose(IncludeResolver* self, LPCVOID data) noexcept {
  (void)self;
  if (data != nullptr) {
    ::HeapFree(::GetProcessHeap(), 0, const_cast<void*>(data));
  }
  return S_OK;
}

const void* g_include_vtable[] = {
    reinterpret_cast<const void*>(&IncludeOpen),
    reinterpret_cast<const void*>(&IncludeClose),
};

IncludeResolver g_include_resolver = {g_include_vtable};

void RememberSourceDirectory(const char* source_name) noexcept {
  g_include_directory[0] = '\0';
  if (source_name == nullptr) {
    return;
  }

  const char* last = nullptr;
  for (const char* cursor = source_name; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      last = cursor;
    }
  }
  if (last == nullptr) {
    return;
  }

  const size_t length = static_cast<size_t>(last - source_name) + 1;
  if (length >= sizeof(g_include_directory)) {
    return;
  }

  memcpy(g_include_directory, source_name, length);
  g_include_directory[length] = '\0';
}

using CompileFn = HRESULT(SHIM_COM*)(LPCVOID, SIZE_T, LPCSTR, const void*,
                                     void*, LPCSTR, LPCSTR, UINT, UINT,
                                     void**, void**);
CompileFn g_original_compile = nullptr;

HRESULT SHIM_COM CompileWithWin32Includes(LPCVOID source, SIZE_T source_size,
                                          LPCSTR source_name,
                                          const void* defines, void* include,
                                          LPCSTR entry_point, LPCSTR target,
                                          UINT flags1, UINT flags2,
                                          void** out_code,
                                          void** out_errors) noexcept {
  if (include == kStandardFileInclude) {
    RememberSourceDirectory(source_name);
    include = &g_include_resolver;
  }

  if (g_original_compile == nullptr) {
    return E_NOTIMPL;
  }

  const HRESULT result =
      g_original_compile(source, source_size, source_name, defines, include,
                         entry_point, target, flags1, flags2, out_code,
                         out_errors);
  if (FAILED(result)) {
    log::Writef(
        "D3DCompile failed entry=%s target=%s bytes=%u: HRESULT 0x%08lX",
        entry_point != nullptr ? entry_point : "<null>",
        target != nullptr ? target : "<null>",
        static_cast<unsigned int>(source_size), result);

    FILE* dump = ::fopen("shader_failure.hlsl", "wb");
    if (dump != nullptr) {
      ::fwrite(source, 1, source_size, dump);
      ::fclose(dump);
    }
  }
  return result;
}

}  // namespace

void InstallD3DCompileCompatibility(DWORD compile_rva) noexcept {
  if (HookSlot(compile_rva,
               reinterpret_cast<const void*>(&CompileWithWin32Includes),
               reinterpret_cast<void**>(&g_original_compile), "D3DCompile")) {
    log::Write("D3DCompile Win32 include compatibility installed");
  } else {
    log::Write("VirtualProtect D3DCompile IAT failed");
  }
}

}  // namespace shim::hooks::compat_internal
