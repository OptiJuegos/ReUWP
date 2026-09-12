#pragma once

namespace shim::versions::v01510::detail {

bool InstallRuntimePatches() noexcept;
bool InstallRenderPatches() noexcept;
bool InstallUiHooks() noexcept;
bool InstallVideoOptionHooks() noexcept;
bool InstallLifecycleHooks() noexcept;
bool InstallFileBrowserHook() noexcept;
bool InstallMaterialSchedulerHook() noexcept;

}  // namespace shim::versions::v01510::detail
