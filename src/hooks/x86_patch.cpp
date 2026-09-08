#include "shim/x86_patch.h"

#include <cstring>

#include "shim/log.h"
#include "shim/patch.h"

namespace shim::hooks::x86 {
namespace {

constexpr size_t kRelativeBranchSize = 5;

void RollBack(uintptr_t module_base, const RelativeBranchPatch* patches,
              size_t applied) noexcept {
  while (applied != 0) {
    --applied;
    const RelativeBranchPatch& patch = patches[applied];
    void* const target = reinterpret_cast<void*>(module_base + patch.rva);
    if (!hooks::WriteCode(target, patch.expected, patch.size)) {
      log::Writef("x86 branch rollback failed at rva=%08lx",
                  static_cast<unsigned long>(patch.rva));
    }
  }
}

}  // namespace

bool BuildRelativeBranch(void* source, const void* destination,
                         RelativeBranch branch, unsigned char* output,
                         size_t size) noexcept {
  if (source == nullptr || destination == nullptr || output == nullptr ||
      size < kRelativeBranchSize) {
    return false;
  }

  memset(output, 0x90, size);
  output[0] = static_cast<unsigned char>(branch);

  const intptr_t delta = reinterpret_cast<intptr_t>(destination) -
                         (reinterpret_cast<intptr_t>(source) +
                          kRelativeBranchSize);
  const int32_t relative = static_cast<int32_t>(delta);
  memcpy(output + 1, &relative, sizeof(relative));
  return true;
}

bool WriteRelativeBranch(void* target, const void* expected, size_t size,
                         const void* destination, RelativeBranch branch,
                         const char* failure_message) noexcept {
  if (target == nullptr || expected == nullptr ||
      !hooks::MatchesSignature(target, expected, size)) {
    if (failure_message != nullptr) {
      log::Write(failure_message);
    }
    return false;
  }

  unsigned char branch_bytes[32] = {};
  if (size > sizeof(branch_bytes) ||
      !BuildRelativeBranch(target, destination, branch, branch_bytes, size) ||
      !hooks::WriteCode(target, branch_bytes, size)) {
    if (failure_message != nullptr) {
      log::Write(failure_message);
    }
    return false;
  }
  return true;
}

bool ApplyRelativeBranchPatches(uintptr_t module_base,
                                const RelativeBranchPatch* patches,
                                size_t count) noexcept {
  if (module_base == 0 || patches == nullptr) {
    return count == 0;
  }

  for (size_t index = 0; index < count; ++index) {
    const RelativeBranchPatch& patch = patches[index];
    void* const target = reinterpret_cast<void*>(module_base + patch.rva);
    if (patch.expected == nullptr || patch.size < kRelativeBranchSize ||
        patch.size > 32 ||
        !hooks::MatchesSignature(target, patch.expected, patch.size)) {
      if (patch.failure_message != nullptr) {
        log::Write(patch.failure_message);
      }
      return false;
    }
  }

  size_t applied = 0;
  for (; applied < count; ++applied) {
    const RelativeBranchPatch& patch = patches[applied];
    void* const target = reinterpret_cast<void*>(module_base + patch.rva);
    unsigned char branch_bytes[32] = {};
    if (!BuildRelativeBranch(target, patch.destination, patch.branch,
                             branch_bytes, patch.size) ||
        !hooks::WriteCode(target, branch_bytes, patch.size)) {
      if (patch.failure_message != nullptr) {
        log::Write(patch.failure_message);
      }
      RollBack(module_base, patches, applied);
      return false;
    }
  }
  return true;
}

}  // namespace shim::hooks::x86
