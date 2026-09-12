#include "patches_internal.h"

#include "shim/common.h"
#include "shim/game_layout.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::versions::v01510 {
namespace {

// Cuatro callbacks Xbox equivalentes en Minecraft 0.15.10. La tabla
// embedded in the reference shim uses the same HRESULT=8 / ret 4 stub as 1.1.5,
// pero con firmas y RVAs propios de esta version.
constexpr unsigned char k01510XboxCallbackNoOp[] = {
    0xB8, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00};
constexpr unsigned char k01510XboxMenuSignInExpected[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0xEC, 0x28, 0x8B};
constexpr unsigned char k01510XboxSignInAExpected[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0xEC, 0x28, 0x8B};
constexpr unsigned char k01510XboxSignInBExpected[] = {
    0x83, 0xC1, 0x04, 0xE9, 0x48, 0xFB, 0xFF, 0xFF};
constexpr unsigned char k01510XboxSignInCExpected[] = {
    0x83, 0xC1, 0x08, 0xE9, 0xD8, 0xF9, 0xFF, 0xFF};

constexpr hooks::PatchSpec k01510XboxCallbackPatches[] = {
    {0x000F3520, k01510XboxMenuSignInExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxMenuSignInExpected),
     "Minecraft 0.15.10 menu Xbox sign-in no-op patch failed"},
    {0x00127A70, k01510XboxSignInAExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInAExpected),
     "Minecraft 0.15.10 Xbox sign-in callback A no-op patch failed"},
    {0x001316C0, k01510XboxSignInBExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInBExpected),
     "Minecraft 0.15.10 Xbox sign-in callback B no-op patch failed"},
    {0x001331C0, k01510XboxSignInCExpected, k01510XboxCallbackNoOp,
     sizeof(k01510XboxSignInCExpected),
     "Minecraft 0.15.10 Xbox sign-in callback C no-op patch failed"},
};

bool InstallXboxCallbackPatches() noexcept {
  const hooks::PatchTransaction transaction(
      reinterpret_cast<uintptr_t>(game::Base()));
  const bool ok = transaction.Apply(k01510XboxCallbackPatches,
                                    CountOf(k01510XboxCallbackPatches));
  log::Write(ok ? "Minecraft 0.15.10 Xbox callbacks disabled for HWND host"
                : "Minecraft 0.15.10 Xbox callback patches incomplete");
  return ok;
}

// The executables shipped so far carry the next two patches baked in, so a
// signature mismatch there would be ambiguous: unsupported build, or an image
// that was patched ahead of time. Accepting the replacement bytes as success
// keeps a pre-patched and a stock executable on the same path.
bool InstallIdempotentPatch(const hooks::PatchSpec& patch,
                            const char* installed, const char* already,
                            const char* failed) noexcept {
  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), patch);
  if (result == hooks::PatchApplyResult::kAlreadyApplied) {
    log::Write(already);
    return true;
  }
  if (result == hooks::PatchApplyResult::kApplied) {
    log::Write(installed);
    return true;
  }
  log::Write(patch.failure_message);
  log::Write(failed);
  return false;
}

// ---------------------------------------------------------------------------
// Minecraft 0.15.10 - lost-focus path
// ---------------------------------------------------------------------------
//
// sub_00788C70 is the UWP suspend pump. Its only caller is the lost-focus
// handler at 0x005FFC10 (the one that reports "LostFocus.Rift"), which parks
// the game in a GetTickCount64-bounded wait loop and drives the pump on every
// turn of it:
//
//   005FFDCF  mov   dword ptr [esi+0x3C4], 2   ; suspended
//   005FFDE0  call  <spin>                     ; loops while it returns true
//   005FFDEB  mov   ecx, [esi+0x258]
//   005FFDF1  call  0x00788C70                 ; neutralised here
//   005FFDF6  call  [KERNEL32!GetTickCount64]  ; deadline in [esi+0x3A0]
//
// The pump walks the GameControllerHandler_Windows vector at [this+0x74..0x78]
// and drives AppPlatform_Winrt at [this+0x10]; both classes come from the
// vtable RTTI at 0x00F3B258 and 0x00F6179C. Neither is reachable outside the
// app container: the WinRT gamepad stack is absent (the host ships an
// XInputGetState stub) and AppPlatform_Winrt is what the shim replaces.
//
// Under UWP this path runs only on a real suspend. On a Win32 window it runs on
// every alt-tab, so it has to become a no-op. The wait loop around it is left
// alone.
//
// The function takes no stack arguments - it ends in pop edi/esi/ebx, mov esp,
// ebp, pop ebp, ret - so returning immediately keeps the stack balanced. Only
// the first byte changes; the rest of the signature is there because a lone
// 0x55 matches every prologue in the image.
constexpr unsigned char k01510SuspendPumpExpected[9] = {
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xD9};
constexpr unsigned char k01510SuspendPumpReplacement[9] = {
    0xC3, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x8B, 0xD9};

constexpr hooks::PatchSpec k01510SuspendPumpPatch = {
    0x00388C70, k01510SuspendPumpExpected, k01510SuspendPumpReplacement,
    sizeof(k01510SuspendPumpExpected),
    "Minecraft 0.15.10 UWP suspend pump no-op patch failed"};

// Null guard in the teardown callback at 0x00438900, reached through the
// _Do_call slot of a std::function wrapping a lambda:
//
//   001DC7A0  mov ecx, [ecx+4]      ; captured object
//   001DC7A3  mov ecx, [ecx+0x34]
//   001DC7A6  jmp 0x00438900
//
// The original guards the outer pointer and only then dereferences it:
//
//   mov ecx,[esi+0xA8] / test ecx,ecx / je +8 / mov ecx,[ecx+0x20] / call
//
// Under the HWND host the outer object is always constructed but its 0x20
// member can be null, so the call landed with a null `this`. Moving the test
// one level in fixes that; the branch target is unchanged, so both forms skip
// the same call.
//
// This DROPS the test on [esi+0xA8] itself - seven bytes cannot hold both - so
// a null outer pointer now faults where it used to be tolerated. That is the
// trade the shipped executables already make, and it is the reason this patch
// gets its own table: if [esi+0xA8] ever comes back null, this is the entry to
// revisit.
constexpr unsigned char k01510TeardownGuardExpected[13] = {
    0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
    0x85, 0xC9, 0x74, 0x08, 0x8B, 0x49, 0x20};
constexpr unsigned char k01510TeardownGuardReplacement[13] = {
    0x8B, 0x8E, 0xA8, 0x00, 0x00, 0x00,
    0x8B, 0x49, 0x20, 0x85, 0xC9, 0x74, 0x05};

constexpr hooks::PatchSpec k01510TeardownGuardPatch = {
    0x00038D01, k01510TeardownGuardExpected, k01510TeardownGuardReplacement,
    sizeof(k01510TeardownGuardExpected),
    "Minecraft 0.15.10 teardown null-guard patch failed"};

bool InstallSuspendPumpPatch() noexcept {
  return InstallIdempotentPatch(
      k01510SuspendPumpPatch,
      "Minecraft 0.15.10 UWP suspend pump disabled",
      "Minecraft 0.15.10 UWP suspend pump already disabled",
      "Minecraft 0.15.10 UWP suspend pump patch unavailable");
}

bool InstallTeardownGuardPatch() noexcept {
  return InstallIdempotentPatch(
      k01510TeardownGuardPatch,
      "Minecraft 0.15.10 teardown null guard installed",
      "Minecraft 0.15.10 teardown null guard already installed",
      "Minecraft 0.15.10 teardown null guard unavailable");
}

}  // namespace

namespace detail {

bool InstallRuntimePatches() noexcept {
  const bool xbox = InstallXboxCallbackPatches();
  const bool suspend_pump = InstallSuspendPumpPatch();
  const bool teardown_guard = InstallTeardownGuardPatch();
  return xbox && suspend_pump && teardown_guard;
}

}  // namespace detail
}  // namespace shim::versions::v01510
