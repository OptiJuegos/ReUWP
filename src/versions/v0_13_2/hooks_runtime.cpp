#include "shim/version_patches.h"

#include "shim/activation.h"
#include "shim/common.h"
#include "shim/game_layout.h"
#include "shim/log.h"
#include "shim/patch.h"
#include "shim/version_asm.h"

namespace shim::versions::v0132 {
namespace {

// FUN_629557e0 stores the bootstrap thread id and replaces the frame-sleep
// import. The register/stack-preserving tail trampoline lives in asm_hooks.cpp.
bool InstallV0132FrameSleepHook() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  const DWORD main_thread_id = ::GetCurrentThreadId();
  const void* const entry = asm_hooks::FrameSleepEntry();
  if (entry == nullptr) {
    log::Write("0.13.2 frame-sleep hook requires MSVC x86 assembly support");
    return false;
  }

  void* original = nullptr;
  if (!hooks::ReplaceImportRva(game::Base(), layout.iat_frame_sleep, entry,
                               &original, "frame sleep (0.13.2)")) {
    log::Write("0.13.2 frame-sleep hook failed");
    return false;
  }
  asm_hooks::ConfigureFrameSleep(main_thread_id, original);
  log::WritePointer("0.13.2 original frame-sleep import", original);
  log::Writef("0.13.2 frame-sleep hook installed for thread %lu",
              static_cast<unsigned long>(main_thread_id));
  return true;
}


bool InstallV0132ActivationHook() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  void* original = nullptr;
  if (!hooks::ReplaceImportRva(
          game::Base(), layout.iat_activation_factory,
          reinterpret_cast<const void*>(&winrt::GetActivationFactoryByName),
          &original, "GetActivationFactoryByPCWSTR (0.13.2)")) {
    log::Write("0.13.2 WinRT activation redirect failed");
    return false;
  }
  winrt::SetActivationFallback(
      reinterpret_cast<winrt::ActivationFactoryFn>(original));
  log::Write("0.13.2 WinRT activation redirect installed");
  return true;
}

// 6295C9F0 / 6295CA40. El bootstrap 0.13.2 sustituye las dos entradas de
// servicios Xbox por stubs stdcall. El primero termina en `ret 0x0c` y por
// tanto recibe tres argumentos; el segundo termina en `ret 0x18` y recibe seis.
// Ambos devuelven E_NOTIMPL sin tocar salidas. Mantener la aridad exacta es
// importante en x86: con __stdcall el llamado limpia la pila.
HRESULT SHIM_COM V0132AchievementsNoOp(void* arg0, void* arg1,
                                            void* arg2) noexcept {
  (void)arg0;
  (void)arg1;
  (void)arg2;
  return E_NOTIMPL;
}

HRESULT SHIM_COM V0132InviteNoOp(void* arg0, void* arg1, void* arg2,
                                      void* arg3, void* arg4,
                                      void* arg5) noexcept {
  (void)arg0;
  (void)arg1;
  (void)arg2;
  (void)arg3;
  (void)arg4;
  (void)arg5;
  return E_NOTIMPL;
}

bool InstallV0132ServiceHooks() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  const bool achievements =
      hooks::ReplaceImportRva(
          game::Base(), layout.achievements_iat,
          reinterpret_cast<const void*>(&V0132AchievementsNoOp), nullptr,
          "achievements service (0.13.2)");
  const bool invite =
      hooks::ReplaceImportRva(
          game::Base(), layout.invite_player_iat,
          reinterpret_cast<const void*>(&V0132InviteNoOp), nullptr,
          "invite-player service (0.13.2)");

  log::Write(achievements && invite
                 ? "0.13.2 Xbox service stubs installed"
                 : "0.13.2 Xbox service stub patch incomplete");
  return achievements && invite;
}

}  // namespace

bool InstallFrameSleepHook() noexcept {
  return InstallV0132FrameSleepHook();
}

bool InstallActivationHook() noexcept {
  return InstallV0132ActivationHook();
}

bool InstallServiceHooks() noexcept {
  return InstallV0132ServiceHooks();
}

}  // namespace shim::versions::v0132
