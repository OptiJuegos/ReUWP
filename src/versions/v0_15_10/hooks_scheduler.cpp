#include "patches_internal.h"

#include "shim/common.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/patch.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::detail {
namespace {

void* g_original_material_scheduler_01510 = nullptr;
volatile LONG g_material_shader_sync_count_01510 = 0;

void __cdecl DrainMaterialShaderTask01510(void* callback) noexcept {
  if (callback == nullptr || ::IsBadReadPtr(callback, sizeof(void*))) {
    return;
  }

  void* const vtable = *reinterpret_cast<void**>(callback);
  void* const expected_vtable = game::Resolve(0x00B3A334);
  if (vtable != expected_vtable || vtable == nullptr ||
      ::IsBadReadPtr(vtable, 3 * sizeof(void*))) {
    return;
  }

  void* const step_entry = reinterpret_cast<void**>(vtable)[2];
  if (step_entry == nullptr) {
    return;
  }

  using StepFn = unsigned char(__thiscall*)(void* callback);
  const auto step = reinterpret_cast<StepFn>(step_entry);
  while (step(callback) == 0) {
  }

  const LONG count = ::InterlockedIncrement(&g_material_shader_sync_count_01510);
  if (count <= 4) {
    log::Write("material shader task executed synchronously");
  }
}


}  // namespace

bool InstallMaterialSchedulerHook() noexcept {
  if (game::Current() != game::Version::kV0_15_10) {
    return true;
  }

  constexpr DWORD kCallsiteRva = 0x002CCA0B;
  constexpr DWORD kOriginalSchedulerRva = 0x00390670;
  constexpr unsigned char kExpected[5] = {
      0xE8, 0x60, 0x3C, 0x0C, 0x00};

  auto* const target = static_cast<unsigned char*>(game::Resolve(kCallsiteRva));
  g_original_material_scheduler_01510 = game::Resolve(kOriginalSchedulerRva);
  if (target == nullptr || g_original_material_scheduler_01510 == nullptr ||
      !hooks::MatchesSignature(target, kExpected, sizeof(kExpected))) {
    log::Write("0.15.10 material scheduler hook signature mismatch");
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }

  const void* const entry = asm_hooks::MaterialSchedulerEntry();
  if (entry == nullptr) {
    log::Write("0.15.10 material scheduler hook requires MSVC x86 assembly support");
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }
  asm_hooks::ConfigureMaterialScheduler(&DrainMaterialShaderTask01510,
                                        g_original_material_scheduler_01510);
  const hooks::x86::RelativeBranchPatch patch = {
      kCallsiteRva, kExpected, sizeof(kExpected), entry,
      hooks::x86::RelativeBranch::kCall,
      "0.15.10 material scheduler compatibility patch failed"};
  if (!hooks::x86::ApplyRelativeBranchPatches(
          reinterpret_cast<uintptr_t>(game::Base()), &patch, 1)) {
    asm_hooks::ConfigureMaterialScheduler(nullptr, nullptr);
    g_original_material_scheduler_01510 = nullptr;
    return false;
  }

  log::Write("0.15.10 material scheduler compatibility patch installed");
  return true;
}


}  // namespace shim::versions::v01510::detail
