#pragma once

#include "shim/common.h"

namespace shim::memory {

bool IsReadable(const void* address, size_t size) noexcept;
bool IsWritable(void* address, size_t size) noexcept;
bool IsExecutable(const void* address) noexcept;

}  // namespace shim::memory
