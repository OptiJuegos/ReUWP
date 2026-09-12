#include "patches_internal.h"

#include "shim/app_window.h"
#include "shim/common.h"
#include "shim/custom_skin.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/memory.h"
#include "shim/object_access.h"
#include "shim/x86_patch.h"

namespace shim::versions::v115::detail {
namespace {

// ---------------------------------------------------------------------------
// Minecraft 1.1.5 - hooks de UI persistentes
// ---------------------------------------------------------------------------
//
// El bootstrap original reemplaza estos dos metodos C++/CX despues de crear el
// HWND y antes de construir D3D/AppMain. Ambos parches son saltos de 10 bytes
// guarded by the exact signature embedded in the reference shim.

constexpr DWORD k115CustomSkinRva = 0x006D1740;
constexpr DWORD k115FullscreenRva = 0x006D55B0;

constexpr unsigned char k115CustomSkinSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xD8, 0xF2, 0x2A, 0x01};
constexpr unsigned char k115FullscreenSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xEF, 0x0C, 0x2C, 0x01};

// 62950870 -> 629509A0. El wrapper original conserva el callback en
// *(self+0x278)+0xA8 antes de abrir el selector Win32. El picker termina con
// `ret 4`, por eso una funcion __fastcall con un unico argumento de pila
// mantiene la ABI del metodo thiscall sustituido.
void __fastcall CustomSkinHook115(void* self, void* /*edx*/,
                                  void* callback) noexcept {
  if (self != nullptr &&
      memory::IsReadable(object_access::Field(self, 0x278), sizeof(void*))) {
    void* owner = *reinterpret_cast<void**>(object_access::Field(self, 0x278));
    if (owner != nullptr &&
        memory::IsWritable(object_access::Field(owner, 0xA8), sizeof(void*))) {
      *reinterpret_cast<void**>(object_access::Field(owner, 0xA8)) = callback;
    }
  }

  custom_skin::ShowAndInstall();

  // El epilogo del picker original notifica la finalizacion mediante vtable+8.
  void* const finish_entry = object_access::VtableEntry(callback, 8);
  if (finish_entry != nullptr) {
    using FinishFn = void(__thiscall*)(void* callback);
    reinterpret_cast<FinishFn>(finish_entry)(callback);
  }
}

// 62950970: lee el booleano/estado en this+4 y lo entrega al helper Win32
// 629512D0. SetBorderlessFullscreen() ya reproduce ese helper compartido.
void __fastcall FullscreenHook115(void* self, void* /*edx*/) noexcept {
  bool enabled = false;
  if (self != nullptr &&
      memory::IsReadable(object_access::Field(self, 4), sizeof(DWORD))) {
    enabled = *reinterpret_cast<const DWORD*>(object_access::Field(self, 4)) != 0;
  }
  app_window::SetBorderlessFullscreen(enabled);
}

}  // namespace

bool InstallUiHooks() noexcept {
  if (game::Current() != game::Version::kV1_1_5) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {k115CustomSkinRva, k115CustomSkinSignature,
       sizeof(k115CustomSkinSignature),
       reinterpret_cast<const void*>(&CustomSkinHook115),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 custom-skin hook failed"},
      {k115FullscreenRva, k115FullscreenSignature,
       sizeof(k115FullscreenSignature),
       reinterpret_cast<const void*>(&FullscreenHook115),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 fullscreen hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "1.1.5 custom-skin/fullscreen hooks installed"
                : "1.1.5 custom-skin/fullscreen hooks unavailable");
  return ok;
}


}  // namespace shim::versions::v115::detail
