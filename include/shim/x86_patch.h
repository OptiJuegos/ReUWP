#pragma once

#include "shim/common.h"

namespace shim::hooks::x86 {

enum class RelativeBranch : unsigned char {
  kCall = 0xE8,
  kJump = 0xE9,
};

struct RelativeBranchPatch {
  uint32_t rva;
  const void* expected;
  uint32_t size;
  const void* destination;
  RelativeBranch branch;
  const char* failure_message;
};

bool BuildRelativeBranch(void* source, const void* destination,
                         RelativeBranch branch, unsigned char* output,
                         size_t size) noexcept;

bool WriteRelativeBranch(void* target, const void* expected, size_t size,
                         const void* destination, RelativeBranch branch,
                         const char* failure_message) noexcept;

bool ApplyRelativeBranchPatches(uintptr_t module_base,
                                const RelativeBranchPatch* patches,
                                size_t count) noexcept;

}  // namespace shim::hooks::x86
