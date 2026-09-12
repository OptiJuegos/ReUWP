#pragma once

#include "shim/common.h"
#include "shim/game_layout.h"

namespace shim::hooks::compat_internal {

bool HookSlot(DWORD rva, const void* replacement, void** previous,
              const char* name) noexcept;

void InstallD3DCompileCompatibility(DWORD compile_rva) noexcept;
void InstallD3DDeviceCompatibility(DWORD create_device_rva,
                                   DWORD device2_iid_rva,
                                   DWORD context2_iid_rva,
                                   DWORD dxgi3_iid_rva) noexcept;
bool InstallVccorlibCompatibility(const game::PatchLayout& layout) noexcept;

}  // namespace shim::hooks::compat_internal
