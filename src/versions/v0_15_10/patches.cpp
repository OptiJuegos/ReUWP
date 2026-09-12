#include "shim/version_patches.h"

#include "patches_internal.h"

#include "shim/input.h"

namespace shim::versions::v01510 {

bool InstallCompatibilityPatches() noexcept {
  const bool runtime = detail::InstallRuntimePatches();
  const bool render = detail::InstallRenderPatches();
  return runtime && render;
}

void InstallPostWindowPatches() noexcept {
  input::InstallHooks();
  detail::InstallUiHooks();
  detail::InstallLifecycleHooks();
  detail::InstallVideoOptionHooks();
  detail::InstallFileBrowserHook();
  detail::InstallMaterialSchedulerHook();
}

}  // namespace shim::versions::v01510
