#include "shim/compat_hooks.h"

#include "compat_internal.h"

#include "shim/game_layout.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::hooks {
namespace compat_internal {

bool HookSlot(DWORD rva, const void* replacement, void** previous,
              const char* name) noexcept {
  void* slot = game::Resolve(rva);
  if (slot == nullptr) {
    return false;
  }
  return ReplacePointer(static_cast<void**>(slot), replacement, previous, name);
}

}  // namespace compat_internal

void InstallCompatibility(const game::PatchLayout& layout) noexcept {
  const bool vccorlib_ok = compat_internal::InstallVccorlibCompatibility(layout);
  log::Write(vccorlib_ok
                 ? "installed native Platform::Object/IBox/allocator compatibility shims"
                 : "vccorlib compatibility IAT protection failed");

  compat_internal::InstallD3DCompileCompatibility(layout.iat_d3d_compile);
  compat_internal::InstallD3DDeviceCompatibility(
      layout.iat_d3d11_create_device, layout.iid_d3d11_device2,
      layout.iid_d3d11_context2, layout.iid_dxgi_device3);
}

void InstallV0132Compatibility() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  compat_internal::InstallD3DCompileCompatibility(layout.iat_d3d_compile);
  compat_internal::InstallD3DDeviceCompatibility(
      layout.iat_d3d11_create_device, layout.iid_d3d11_device2,
      layout.iid_d3d11_context2, 0);
}

}  // namespace shim::hooks
