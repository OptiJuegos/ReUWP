#include "shim/version_patches.h"

#include "patches_internal.h"

#include "shim/input.h"
#include "shim/stdio_overrides.h"

namespace shim::versions::v115 {

bool InstallCompatibilityPatches() noexcept {
  return detail::InstallRenderPatches();
}

bool InstallPreD3DPatches() noexcept {
  return detail::InstallRuntimePatches();
}

void InstallPostWindowPatches() noexcept {
  stdio_overrides::InstallStdioHooks();
  input::InstallHooks();
  detail::InstallUiHooks();
  detail::InstallLifecycleHooks();
  detail::InstallVideoOptionHooks();
}

}  // namespace shim::versions::v115
