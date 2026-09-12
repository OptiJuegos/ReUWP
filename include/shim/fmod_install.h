#pragma once

#include "shim/common.h"

namespace shim::game {
struct FmodLayout;
}

namespace shim::fmod {

void BindLayout(const game::FmodLayout* layout) noexcept;
const game::FmodLayout* BoundLayout() noexcept;

bool Install() noexcept;
bool PreloadBridgeRuntime() noexcept;

bool InstallBridgeArray(DWORD name_rva, DWORD iat_rva, DWORD expected_count,
                        const void* const* bridge_slots, size_t bridge_count,
                        const char* label) noexcept;

bool ResolveDesktopDelayImports(DWORD descriptor_rva, DWORD expected_iat_rva,
                                DWORD expected_int_rva,
                                DWORD expected_import_count) noexcept;

}  // namespace shim::fmod
