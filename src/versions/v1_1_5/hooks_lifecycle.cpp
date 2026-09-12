#include "patches_internal.h"

#include "shim/app_window.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/x86_patch.h"

namespace shim::versions::v115::detail {
namespace {

constexpr DWORD kExitConfirmationActionRva = 0x0024CB10;

constexpr unsigned char kExitConfirmationActionSignature[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x80, 0x38, 0x00, 0x74,
    0x11, 0x8B, 0x41, 0x04, 0x8B, 0x80, 0x20, 0x02, 0x00, 0x00};

void __fastcall ExitConfirmationHook(void* /*action*/, void* /*edx*/,
                                     const unsigned char* accepted) noexcept {
  // StartMenuScreenController has already presented the native confirmation
  // dialog at this point. Only replace the final UWP process-exit request.
  if (accepted != nullptr && *accepted != 0) {
    app_window::RequestClose();
  }
}

}  // namespace

bool InstallLifecycleHooks() noexcept {
  if (game::Current() != game::Version::kV1_1_5) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {kExitConfirmationActionRva, kExitConfirmationActionSignature,
       sizeof(kExitConfirmationActionSignature),
       reinterpret_cast<const void*>(&ExitConfirmationHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 exit confirmation hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "1.1.5 exit confirmation hook installed"
                : "1.1.5 exit confirmation hook unavailable");
  return ok;
}

}  // namespace shim::versions::v115::detail
