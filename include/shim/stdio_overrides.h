#pragma once

#include "shim/common.h"

namespace shim::stdio_overrides {

// Installs the stdio hooks described by the active version profile. Profiles
// without a stdio patch catalog are left unchanged.
bool InstallStdioHooks() noexcept;

}  // namespace shim::stdio_overrides
