#pragma once

namespace shim::versions::v115::detail {

bool InstallRuntimePatches() noexcept;
bool InstallRenderPatches() noexcept;
bool InstallUiHooks() noexcept;
bool InstallVideoOptionHooks() noexcept;
void EnsureVideoOptionOverrides() noexcept;
bool InstallLifecycleHooks() noexcept;

}  // namespace shim::versions::v115::detail
