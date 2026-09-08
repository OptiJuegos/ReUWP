#pragma once

#include "shim/common.h"

namespace shim::object_access {

inline unsigned char* Field(void* object, size_t offset) noexcept {
  return static_cast<unsigned char*>(object) + offset;
}

inline void* VtableEntry(void* object, size_t byte_offset) noexcept {
  if (object == nullptr || ::IsBadReadPtr(object, sizeof(void*))) {
    return nullptr;
  }

  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable == nullptr ||
      ::IsBadReadPtr(vtable, byte_offset + sizeof(void*))) {
    return nullptr;
  }

  return *reinterpret_cast<void**>(
      reinterpret_cast<unsigned char*>(vtable) + byte_offset);
}

}  // namespace shim::object_access
