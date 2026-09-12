#include "patches_internal.h"

#include "shim/app_window.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::detail {
namespace {

// StartMenuScreenController keeps Minecraft's native exit warning. This
// callback runs only after the user responds to that warning, immediately
// before the original UWP lifecycle backend is asked to terminate the app.
constexpr DWORD kExitConfirmationActionRva = 0x000ED190;

constexpr unsigned char kExitConfirmationActionSignature[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x80, 0x38,
    0x00, 0x74, 0x1D, 0x8B, 0x41, 0x04, 0x8B, 0x80,
    0xB8, 0x01, 0x00, 0x00, 0x8B, 0x48, 0x0C, 0x8B,
};

void __fastcall ExitConfirmationHook(void* /*action*/, void* /*edx*/,
                                     const unsigned char* accepted) noexcept {
  // Leave Save & Quit and the native confirmation UI untouched. Only replace
  // the final UWP process-exit request with the Win32 host close path.
  if (accepted != nullptr && *accepted != 0) {
    app_window::RequestClose();
  }
}

}  // namespace

bool InstallLifecycleHooks() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {kExitConfirmationActionRva, kExitConfirmationActionSignature,
       sizeof(kExitConfirmationActionSignature),
       reinterpret_cast<const void*>(&ExitConfirmationHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 exit confirmation hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "0.15.10 exit confirmation hook installed"
                : "0.15.10 exit confirmation hook unavailable");
  return ok;
}

}  // namespace shim::versions::v01510::detail
