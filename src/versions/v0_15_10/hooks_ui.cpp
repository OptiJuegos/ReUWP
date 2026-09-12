#include "patches_internal.h"

#include "shim/app_window.h"
#include "shim/common.h"
#include "shim/custom_skin.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::detail {
namespace {

constexpr DWORD k01510CustomSkinRva = 0x00600110;
constexpr DWORD k01510FullscreenRva = 0x00601310;

constexpr unsigned char k01510CustomSkinSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xFB, 0x54, 0xD0, 0x00};
constexpr unsigned char k01510FullscreenSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x3F, 0x59, 0xD0, 0x00};

// sub_62950980. Igual que el 0.13.2 picker, el callback se conserva en
// this+0x1B0 y se notifica por vtable+8 al terminar el dialogo Win32.
void __fastcall CustomSkinHook01510(void* self, void* /*edx*/,
                                    void* callback) noexcept {
  if (self != nullptr &&
      !::IsBadWritePtr(object_access::Field(self, 0x1B0), sizeof(void*))) {
    *reinterpret_cast<void**>(object_access::Field(self, 0x1B0)) = callback;
  }

  custom_skin::ShowAndInstall();

  void* const finish_entry = object_access::VtableEntry(callback, 8);
  if (finish_entry != nullptr) {
    using FinishFn = void(__thiscall*)(void* callback);
    reinterpret_cast<FinishFn>(finish_entry)(callback);
  }
}

// sub_629512B0 es __stdcall(int): el valor ya llega como argumento, no dentro
// de un objeto C++/CX como ocurre en el wrapper de 1.1.5.
void SHIM_COM FullscreenHook01510(int enabled) noexcept {
  app_window::SetBorderlessFullscreen(enabled != 0);
}

}  // namespace

bool InstallUiHooks() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {k01510CustomSkinRva, k01510CustomSkinSignature,
       sizeof(k01510CustomSkinSignature),
       reinterpret_cast<const void*>(&CustomSkinHook01510),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 custom-skin hook failed"},
      {k01510FullscreenRva, k01510FullscreenSignature,
       sizeof(k01510FullscreenSignature),
       reinterpret_cast<const void*>(&FullscreenHook01510),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 fullscreen hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "0.15.10 custom-skin/fullscreen hooks installed"
                : "0.15.10 custom-skin/fullscreen hooks unavailable");
  return ok;
}


}  // namespace shim::versions::v01510::detail
