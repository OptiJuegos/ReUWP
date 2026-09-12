#include "patches_internal.h"

#include "shim/common.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::versions::v115::detail {

// Minecraft 1.1.5 usa ID3D11DeviceContext1::DiscardView en una ruta que no
// is unavailable on Windows 7. The reference shim does not emulate it: it turns the
// funcion del juego que agrupa esas llamadas en un `ret` de un byte. Para 1.1.5
// solo acepta el prologo 0xA1 esperado o 0xC3 si ya fue parcheada.
constexpr DWORD k115DiscardViewRva = 0x006D0DA0;
constexpr unsigned char k115DiscardViewExpected[] = {0xA1};
constexpr unsigned char k115DiscardViewReplacement[] = {0xC3};
constexpr hooks::PatchSpec k115DiscardViewPatch = {
    k115DiscardViewRva, k115DiscardViewExpected, k115DiscardViewReplacement,
    sizeof(k115DiscardViewExpected),
    "Minecraft 1.1.5 Windows 7 DiscardView patch refused"};

bool InstallRenderPatches() noexcept {
  if (!game::HasCapability(game::VersionCapability::kPreD3DCompatibility) ||
      !game::IsWindows7()) {
    return true;
  }

  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), k115DiscardViewPatch);
  if (result == hooks::PatchApplyResult::kAlreadyApplied) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 D3D11.1 DiscardView calls already disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kApplied) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 D3D11.1 DiscardView calls disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kUnavailable) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 DiscardView patch target unavailable");
    return false;
  }
  if (result == hooks::PatchApplyResult::kUnexpectedBytes) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 DiscardView patch refused: unexpected function prologue");
    return false;
  }

  log::Write("Windows 7 DiscardView compatibility patch failed");
  return false;
}


}  // namespace shim::versions::v115::detail
