#pragma once

namespace shim::versions::v0132 {

bool InstallFrameSleepHook() noexcept;
bool InstallActivationHook() noexcept;
bool InstallServiceHooks() noexcept;
bool InstallHelpHook() noexcept;
bool InstallCustomSkinHook() noexcept;
bool InstallFullscreenHook() noexcept;

}  // namespace shim::versions::v0132

namespace shim::versions::v01510 {

bool InstallCompatibilityPatches() noexcept;
void InstallPostWindowPatches() noexcept;

}  // namespace shim::versions::v01510

namespace shim::versions::v115 {

bool InstallCompatibilityPatches() noexcept;
bool InstallPreD3DPatches() noexcept;
void DumpAdapterSnapshotForced() noexcept;
void InstallPostWindowPatches() noexcept;

}  // namespace shim::versions::v115
