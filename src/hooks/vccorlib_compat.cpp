#include "compat_internal.h"

#include <string.h>

#include "shim/log.h"

namespace shim::hooks::compat_internal {
namespace {

using AllocateWithFlagsFn = void*(__cdecl*)(size_t size, int flags);
using AllocateFn = void*(__cdecl*)(size_t size);

AllocateWithFlagsFn g_original_allocate_with_flags = nullptr;
AllocateFn g_original_allocate = nullptr;

void* __cdecl ZeroingAllocateWithFlags(size_t size, int flags) noexcept {
  void* block = g_original_allocate_with_flags != nullptr
                    ? g_original_allocate_with_flags(size, flags)
                    : nullptr;
  if (block != nullptr) {
    memset(block, 0, size);
  }
  return block;
}

void* __cdecl ZeroingAllocate(size_t size) noexcept {
  void* block =
      g_original_allocate != nullptr ? g_original_allocate(size) : nullptr;
  if (block != nullptr) {
    memset(block, 0, size);
  }
  return block;
}

int SHIM_COM ReadBoxedValue(void* object) noexcept {
  if (object == nullptr) {
    return 0;
  }
  return *reinterpret_cast<int*>(static_cast<unsigned char*>(object) +
                                 sizeof(void*));
}

int SHIM_COM VccorlibNoOp() noexcept {
  return 0;
}

}  // namespace

bool InstallVccorlibCompatibility(const game::PatchLayout& layout) noexcept {
  void** slots[5] = {};
  uintptr_t min_address = static_cast<uintptr_t>(-1);
  uintptr_t max_address = 0;
  for (size_t i = 0; i < CountOf(slots); ++i) {
    slots[i] = static_cast<void**>(game::Resolve(layout.iat_vccorlib[i]));
    if (slots[i] == nullptr) {
      return false;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(slots[i]);
    if (address < min_address) {
      min_address = address;
    }
    if (address > max_address) {
      max_address = address;
    }
  }

  const SIZE_T patch_size =
      static_cast<SIZE_T>((max_address - min_address) + sizeof(void*));
  DWORD old_protect = 0;
  if (!::VirtualProtect(reinterpret_cast<void*>(min_address), patch_size,
                        PAGE_READWRITE, &old_protect)) {
    return false;
  }

  g_original_allocate_with_flags =
      reinterpret_cast<AllocateWithFlagsFn>(*slots[3]);
  g_original_allocate = reinterpret_cast<AllocateFn>(*slots[2]);

  *slots[0] = reinterpret_cast<void*>(&ReadBoxedValue);
  *slots[1] = reinterpret_cast<void*>(&ReadBoxedValue);
  *slots[2] = reinterpret_cast<void*>(&ZeroingAllocate);
  *slots[3] = reinterpret_cast<void*>(&ZeroingAllocateWithFlags);
  *slots[4] = reinterpret_cast<void*>(&VccorlibNoOp);

  DWORD ignored = 0;
  ::VirtualProtect(reinterpret_cast<void*>(min_address), patch_size, old_protect,
                   &ignored);
  ::FlushInstructionCache(::GetCurrentProcess(),
                          reinterpret_cast<void*>(min_address), patch_size);
  return true;
}

}  // namespace shim::hooks::compat_internal
