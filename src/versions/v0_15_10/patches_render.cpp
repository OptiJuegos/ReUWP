#include "patches_internal.h"

#include "shim/common.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::versions::v01510 {
namespace {

// Minecraft 0.15.10 can reach an ID3D11DeviceContext1-only discard path that
// is unavailable on Windows 7. The original compatibility DLL disables the
// entire helper by replacing its first byte with a one-byte RET.
constexpr DWORD k01510DiscardViewRva = 0x005FF7D0;
constexpr unsigned char k01510DiscardViewExpected[] = {0xA1};
constexpr unsigned char k01510DiscardViewReplacement[] = {0xC3};
constexpr hooks::PatchSpec k01510DiscardViewPatch = {
    k01510DiscardViewRva, k01510DiscardViewExpected,
    k01510DiscardViewReplacement, sizeof(k01510DiscardViewExpected),
    "Minecraft 0.15.10 Windows 7 DiscardView patch refused"};

bool InstallDiscardViewPatch() noexcept {
  if (!game::IsWindows7()) {
    return true;
  }

  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), k01510DiscardViewPatch);
  if (result == hooks::PatchApplyResult::kAlreadyApplied) {
    log::Write(
        "Minecraft 0.15.10 Windows 7 D3D11.1 DiscardView calls already disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kApplied) {
    log::Write(
        "Minecraft 0.15.10 Windows 7 D3D11.1 DiscardView calls disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kUnavailable) {
    log::Write(
        "Minecraft 0.15.10 Windows 7 DiscardView patch target unavailable");
    return false;
  }
  if (result == hooks::PatchApplyResult::kUnexpectedBytes) {
    log::Write(
        "Minecraft 0.15.10 Windows 7 DiscardView patch refused: unexpected function prologue");
    return false;
  }

  log::Write("Minecraft 0.15.10 Windows 7 DiscardView patch write failed");
  return false;
}


}  // namespace

namespace detail {

bool InstallRenderPatches() noexcept {
  return InstallDiscardViewPatch();
}

}  // namespace detail
}  // namespace shim::versions::v01510
