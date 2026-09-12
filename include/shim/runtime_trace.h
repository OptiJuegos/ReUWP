// Debug-only runtime boundary trace.
//
// This is intentionally tiny and allocation-free so the access-violation VEH
// can dump it without touching the heap or dereferencing game/FM0D objects.

#pragma once

#include "shim/common.h"

namespace shim::runtime_trace {

enum class BoundaryKind : uint8_t {
  Async = 1,
  Fmod = 2,
  Storage = 3,
};

// Uses the same REUWP_DEBUG switch as logging.
bool Enabled() noexcept;

// `name` must be a process-lifetime string literal. a/b are opaque pointer or
// integer values; Record never dereferences them.
void Record(BoundaryKind kind, const char* name, uintptr_t a = 0,
            uintptr_t b = 0, long result = 0) noexcept;

// Dumps newest-to-oldest records through shim::log. No heap allocation.
void DumpRecent() noexcept;

// Same as DumpRecent, but emits even when neither debug environment variable is set.
// Only the VEH calls this path while the process is being debugged.
void DumpRecentForced() noexcept;

}  // namespace shim::runtime_trace
