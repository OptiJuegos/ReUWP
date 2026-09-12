#include "patches_internal.h"

#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/memory.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/render_distance_policy.h"
#include "shim/runtime_config.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::detail {
namespace {

constexpr DWORD kDecreaseActionRva = 0x001DD190;
constexpr DWORD kIncreaseActionRva = 0x001DD150;
constexpr DWORD kOriginalDecreaseRva = 0x00037E40;
constexpr DWORD kOriginalIncreaseRva = 0x00037F60;
constexpr DWORD kGetOptionRva = 0x00210830;
constexpr DWORD kSetOptionRva = 0x00211210;
constexpr DWORD kRenderDistanceKeyRva = 0x00C64F5C;

// ServerPlayer::setClientChunkRadius(unsigned int) in Minecraft 0.15.10
// clamps the requested radius to at least four chunks before forwarding the
// accepted value to the PlayerChunkSource updater. The updater later adds two
// chunks as a separate streaming margin; keep that margin stock.
//
// 797BF8: mov edx,[ebp+08h]         ; requested radius
// 797BFB: mov eax,[esi+0EF4h]       ; server maximum
// 797C01: mov [esi+0EF0h],edx       ; requested client radius
// 797C07: cmp edx,eax
// 797C09: jg  797C15
// 797C0B: mov eax,4                 ; stock minimum (patch immediate only)
// 797C10: cmp edx,eax
// 797C12: cmovg eax,edx
// 797C15: push eax
// 797C16: call 886930               ; accepted radius / streaming update
constexpr DWORD kServerChunkRadiusClampRva = 0x00397BF8;
constexpr DWORD kServerChunkRadiusMinimumImmediateRva = 0x00397C0C;
constexpr DWORD kServerChunkRadiusClampTailRva = 0x00397C10;

constexpr unsigned char kServerChunkRadiusClampPrefix[] = {
    0x8B, 0x55, 0x08,
    0x8B, 0x86, 0xF4, 0x0E, 0x00, 0x00,
    0x89, 0x96, 0xF0, 0x0E, 0x00, 0x00,
    0x3B, 0xD0,
    0x7F, 0x0A, 0xB8};

constexpr unsigned char kServerChunkRadiusClampTail[] = {
    0x3B, 0xD0, 0x0F, 0x4F, 0xC2,
    0x50, 0xE8, 0x15, 0xED, 0x0E, 0x00,
    0x8B, 0x86, 0xB4, 0x0E, 0x00, 0x00};

constexpr unsigned char kDecreaseActionSignature[] = {
    0x8B, 0x49, 0x04, 0x8B, 0x49, 0x34, 0xE9, 0xA5, 0xAC, 0xE5, 0xFF};
constexpr unsigned char kIncreaseActionSignature[] = {
    0x8B, 0x49, 0x04, 0x8B, 0x49, 0x34, 0xE9, 0x05, 0xAE, 0xE5, 0xFF};

using ActionFn = void(__thiscall*)(void* screen);
using GetOptionFn = int(__thiscall*)(void* options, const void* key);
using SetOptionFn = void(__thiscall*)(void* options, const void* key, int value);

bool PatchServerChunkRadiusMinimum() noexcept {
  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (base == 0) {
    return false;
  }

  const void* const prefix =
      reinterpret_cast<const void*>(base + kServerChunkRadiusClampRva);
  const void* const tail =
      reinterpret_cast<const void*>(base + kServerChunkRadiusClampTailRva);
  auto* const immediate = reinterpret_cast<DWORD*>(
      base + kServerChunkRadiusMinimumImmediateRva);

  if (!hooks::MatchesSignature(prefix, kServerChunkRadiusClampPrefix,
                               sizeof(kServerChunkRadiusClampPrefix)) ||
      !hooks::MatchesSignature(tail, kServerChunkRadiusClampTail,
                               sizeof(kServerChunkRadiusClampTail)) ||
      !memory::IsReadable(immediate, sizeof(*immediate))) {
    log::Write(
        "0.15.10 server chunk-radius minimum patch refused: signature mismatch");
    return false;
  }

  DWORD desired_minimum = runtime_config::Get().min_render_distance;
  if (desired_minimum < 1u) {
    desired_minimum = 1u;
  } else if (desired_minimum > 4u) {
    desired_minimum = 4u;
  }

  const DWORD current_minimum = *immediate;
  if (current_minimum == desired_minimum) {
    return true;
  }
  if (current_minimum != 4u) {
    log::Writef(
        "0.15.10 server chunk-radius minimum patch refused: current=%lu",
        static_cast<unsigned long>(current_minimum));
    return false;
  }

  if (!hooks::WriteCode(immediate, &desired_minimum,
                        sizeof(desired_minimum))) {
    log::Write("0.15.10 server chunk-radius minimum patch write failed");
    return false;
  }

  log::Writef("0.15.10 server chunk-radius minimum: %lu -> %lu chunks",
              static_cast<unsigned long>(current_minimum),
              static_cast<unsigned long>(desired_minimum));
  return true;
}

void* ScreenFromAction(void* action) noexcept {
  if (action == nullptr ||
      !memory::IsReadable(object_access::Field(action, 4), sizeof(void*))) {
    return nullptr;
  }
  void* const owner =
      *reinterpret_cast<void**>(object_access::Field(action, 4));
  if (owner == nullptr ||
      !memory::IsReadable(object_access::Field(owner, 0x34), sizeof(void*))) {
    return nullptr;
  }
  return *reinterpret_cast<void**>(object_access::Field(owner, 0x34));
}

bool HandleLowRenderDistance(void* screen, bool increase) noexcept {
  if (screen == nullptr ||
      !memory::IsReadable(object_access::Field(screen, 0x1C4), sizeof(void*))) {
    return false;
  }

  void* const options =
      *reinterpret_cast<void**>(object_access::Field(screen, 0x1C4));
  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (options == nullptr || base == 0) {
    return false;
  }

  auto get_option = reinterpret_cast<GetOptionFn>(base + kGetOptionRva);
  auto set_option = reinterpret_cast<SetOptionFn>(base + kSetOptionRva);
  const void* const key = reinterpret_cast<const void*>(base + kRenderDistanceKeyRva);
  if (!memory::IsExecutable(reinterpret_cast<const void*>(get_option)) ||
      !memory::IsExecutable(reinterpret_cast<const void*>(set_option)) ||
      !memory::IsReadable(key, sizeof(void*))) {
    return false;
  }

  const int current = get_option(options, key);
  int next = current;
  if (!render_distance_policy::TryResolveLowStep(
          current, static_cast<int>(runtime_config::Get().min_render_distance),
          increase, &next)) {
    return false;
  }

  if (next != current) {
    set_option(options, key, next);
  }
  return true;
}

void CallOriginal(void* screen, DWORD rva) noexcept {
  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (screen == nullptr || base == 0) {
    return;
  }
  auto original = reinterpret_cast<ActionFn>(base + rva);
  if (memory::IsExecutable(reinterpret_cast<const void*>(original))) {
    original(screen);
  }
}

void __fastcall DecreaseRenderDistanceHook(void* action, void* /*edx*/) noexcept {
  void* const screen = ScreenFromAction(action);
  if (!HandleLowRenderDistance(screen, false)) {
    CallOriginal(screen, kOriginalDecreaseRva);
  }
}

void __fastcall IncreaseRenderDistanceHook(void* action, void* /*edx*/) noexcept {
  void* const screen = ScreenFromAction(action);
  if (!HandleLowRenderDistance(screen, true)) {
    CallOriginal(screen, kOriginalIncreaseRva);
  }
}

}  // namespace

bool InstallVideoOptionHooks() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  const bool server_radius_ok = PatchServerChunkRadiusMinimum();

  const hooks::x86::RelativeBranchPatch patches[] = {
      {kDecreaseActionRva, kDecreaseActionSignature,
       sizeof(kDecreaseActionSignature),
       reinterpret_cast<const void*>(&DecreaseRenderDistanceHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 render-distance decrease hook failed"},
      {kIncreaseActionRva, kIncreaseActionSignature,
       sizeof(kIncreaseActionSignature),
       reinterpret_cast<const void*>(&IncreaseRenderDistanceHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 render-distance increase hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "0.15.10 low render-distance hooks installed"
                : "0.15.10 low render-distance hooks unavailable");
  return ok && server_radius_ok;
}

}  // namespace shim::versions::v01510::detail
