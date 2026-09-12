#include "shim/memory.h"

#include <cstdint>
#include <limits>

namespace shim::memory {
namespace {

enum class Access {
  kRead,
  kWrite,
  kExecute,
};

bool ProtectionAllows(DWORD protection, Access access) noexcept {
  if ((protection & PAGE_GUARD) != 0 ||
      (protection & 0xFFu) == PAGE_NOACCESS) {
    return false;
  }

  const DWORD base = protection & 0xFFu;
  switch (access) {
    case Access::kRead:
      return base == PAGE_READONLY || base == PAGE_READWRITE ||
             base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
             base == PAGE_EXECUTE_READWRITE ||
             base == PAGE_EXECUTE_WRITECOPY;
    case Access::kWrite:
      return base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
             base == PAGE_EXECUTE_READWRITE ||
             base == PAGE_EXECUTE_WRITECOPY;
    case Access::kExecute:
      return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
             base == PAGE_EXECUTE_READWRITE ||
             base == PAGE_EXECUTE_WRITECOPY;
  }
  return false;
}

bool IsAccessibleRange(const void* address, size_t size,
                       Access access) noexcept {
  if (address == nullptr || size == 0) {
    return false;
  }

  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  if (size - 1 > std::numeric_limits<uintptr_t>::max() - start) {
    return false;
  }
  const uintptr_t last = start + size - 1;
  uintptr_t current = start;

  while (current <= last) {
    MEMORY_BASIC_INFORMATION info = {};
    if (::VirtualQuery(reinterpret_cast<const void*>(current), &info,
                       sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || !ProtectionAllows(info.Protect, access)) {
      return false;
    }

    const uintptr_t region_base =
        reinterpret_cast<uintptr_t>(info.BaseAddress);
    if (info.RegionSize == 0 ||
        info.RegionSize >
            std::numeric_limits<uintptr_t>::max() - region_base) {
      return false;
    }
    const uintptr_t region_end = region_base + info.RegionSize;
    if (current < region_base || current >= region_end) {
      return false;
    }
    if (last < region_end) {
      return true;
    }
    current = region_end;
  }

  return true;
}

}  // namespace

bool IsReadable(const void* address, size_t size) noexcept {
  return IsAccessibleRange(address, size, Access::kRead);
}

bool IsWritable(void* address, size_t size) noexcept {
  return IsAccessibleRange(address, size, Access::kWrite);
}

bool IsExecutable(const void* address) noexcept {
  return IsAccessibleRange(address, 1, Access::kExecute);
}

}  // namespace shim::memory
