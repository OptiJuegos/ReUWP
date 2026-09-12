#include "shim/version_patches.h"

#include <cstring>

#include "shim/activation.h"
#include "shim/app_window.h"
#include "shim/common.h"
#include "shim/custom_skin.h"
#include "shim/game_layout.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

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


constexpr unsigned char k0132HelpSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xF8, 0xB7, 0x98, 0x00};

// 6295C8E0. UWP abre este enlace mediante Launcher; el shim original lo baja a
// ShellExecuteW para Win32. Carga shell32 de forma dinamica para no convertirla
// en una dependencia dura del DLL.
void __cdecl V0132OpenHelpUrl() noexcept {
  HMODULE shell = ::LoadLibraryW(L"shell32.dll");
  if (shell == nullptr) {
    log::Write("0.13.2 help URL failed: shell32.dll unavailable");
    return;
  }

  using ShellExecuteWFn = HINSTANCE(WINAPI*)(HWND, LPCWSTR, LPCWSTR, LPCWSTR,
                                             LPCWSTR, INT);
  const auto shell_execute = reinterpret_cast<ShellExecuteWFn>(
      ::GetProcAddress(shell, "ShellExecuteW"));
  if (shell_execute == nullptr) {
    log::Write("0.13.2 help URL failed: ShellExecuteW unavailable");
    return;
  }

  const HINSTANCE result =
      shell_execute(app_window::Handle(), L"open",
                    L"http://aka.ms/minecraftfb", nullptr, nullptr, 1);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    log::Write("0.13.2 help URL ShellExecuteW failed");
    return;
  }
  log::Write("0.13.2 help URL opened through Win32 ShellExecuteW");
}

bool InstallV0132HelpHook() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  void* const target = game::Resolve(layout.help_url_hook);
  if (!hooks::x86::WriteRelativeBranch(
          target, k0132HelpSignature, sizeof(k0132HelpSignature),
          reinterpret_cast<const void*>(&V0132OpenHelpUrl),
          hooks::x86::RelativeBranch::kJump,
          "0.13.2 help URL hook failed")) {
    return false;
  }
  log::Write("0.13.2 Win32 help URL hook installed");
  return true;
}

constexpr unsigned char k0132CustomSkinSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x88, 0xCF, 0x9C, 0x00};

// 6295BE50. La funcion reemplazada es un metodo thiscall con un unico
// argumento en pila. Un free function __fastcall preserva esa ABI en x86:
// ECX=self, EDX queda como hueco de fastcall y callback sigue siendo el unico
// argumento de pila, por lo que el retorno limpia exactamente cuatro bytes.
void __fastcall V0132CustomSkinHook(void* self, void* /*edx*/,
                                         void* callback) noexcept {
  // El original recuerda el objeto de finalizacion en this+0x1B0 antes de abrir
  // el dialogo. Se mantiene ese estado porque el codigo del juego puede leerlo
  // incluso aunque el usuario cancele.
  if (self != nullptr &&
      !::IsBadWritePtr(object_access::Field(self, 0x1B0), sizeof(void*))) {
    *reinterpret_cast<void**>(object_access::Field(self, 0x1B0)) = callback;
  }

  custom_skin::ShowAndInstall();

  // Epilogo exacto de FUN_6295BE50: si existe callback, invoca su vtable + 8
  // con callback en ECX. No se le asigna un nombre COM porque el tipo concreto
  // aun no esta demostrado; solo reproducimos la ABI observada.
  void* const finish_entry = object_access::VtableEntry(callback, 8);
  if (finish_entry != nullptr) {
    using FinishFn = void(__thiscall*)(void* callback);
    reinterpret_cast<FinishFn>(finish_entry)(callback);
  }
}

bool InstallV0132CustomSkinHook() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  void* const target = game::Resolve(layout.custom_skin_hook);
  if (!hooks::x86::WriteRelativeBranch(
          target, k0132CustomSkinSignature, sizeof(k0132CustomSkinSignature),
          reinterpret_cast<const void*>(&V0132CustomSkinHook),
          hooks::x86::RelativeBranch::kJump,
          "0.13.2 custom-skin hook failed")) {
    return false;
  }
  log::Write("0.13.2 Win32 custom-skin hook installed");
  return true;
}

constexpr unsigned char k0132FullscreenSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xDF, 0xD3, 0x9C, 0x00};

// 6295C620 termina en `ret 4`: es una funcion __stdcall con un unico entero.
// No toca objetos del juego; traduce directamente el toggle UWP a una ventana
// Win32 borderless y vuelve a sincronizar el cursor.
void SHIM_COM V0132FullscreenHook(int enabled) noexcept {
  app_window::SetBorderlessFullscreen(enabled != 0);
}

bool InstallV0132FullscreenHook() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  void* const target = game::Resolve(layout.fullscreen_hook);
  if (!hooks::x86::WriteRelativeBranch(
          target, k0132FullscreenSignature, sizeof(k0132FullscreenSignature),
          reinterpret_cast<const void*>(&V0132FullscreenHook),
          hooks::x86::RelativeBranch::kJump,
          "0.13.2 fullscreen hook failed")) {
    return false;
  }
  log::Write("0.13.2 Win32 fullscreen hook installed");
  return true;
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

bool InstallHelpHook() noexcept {
  return InstallV0132HelpHook();
}

bool InstallCustomSkinHook() noexcept {
  return InstallV0132CustomSkinHook();
}

bool InstallFullscreenHook() noexcept {
  return InstallV0132FullscreenHook();
}

}  // namespace shim::versions::v0132
