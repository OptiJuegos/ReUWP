#include "shim/bootstrap.h"

#include <cstddef>
#include <cstring>

#include "shim/activation.h"
#include "shim/apiset.h"
#include "shim/app_model.h"
#include "shim/app_window.h"
#include "shim/branding.h"
#include "shim/compat_hooks.h"
#include "shim/config.h"
#include "shim/core_window.h"
#include "shim/crash_report.h"
#include "shim/fmod_install.h"
#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/local_folder.h"
#include "shim/modern_input.h"
#include "shim/modern_runtime_core.h"
#include "shim/version_runtime.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/swapchain.h"
#include "shim/watchdog.h"
#include "shim/winrt_runtime.h"

namespace shim::bootstrap {
namespace {

// --- Ganchos que instala el arranque --------------------------------------

using ThrowFn = int(SHIM_COM*)(void*, void*);
ThrowFn g_original_throw = nullptr;  // dword_62997364

// Traza de excepciones de C++ del juego (sub_62949C90).
//
// _CxxThrowException es el ultimo punto en el que la pila todavia esta intacta:
// para cuando la excepcion llega a un manejador, el desenrollado ya se ha
// llevado por delante los marcos que interesan. Por eso se captura aqui y no en
// el filtro de excepciones.
int SHIM_COM CxxThrowExceptionTrace(void* object, void* type) noexcept {
  log::Write("game raised a C++ exception; stack follows");

  void* frames[24] = {};
  const USHORT captured =
      ::RtlCaptureStackBackTrace(0, CountOf(frames), frames, nullptr);
  for (USHORT i = 0; i < captured; ++i) {
    log::Writef("  frame %u: %p", static_cast<unsigned int>(i), frames[i]);
  }

  // Se encadena al original: esto observa, no intercepta. Devolver sin lanzar
  // dejaria al juego creyendo que su excepcion se propago.
  return g_original_throw != nullptr ? g_original_throw(object, type) : 0;
}

// Sustitutos sin efecto para las dos entradas TCUI de Xbox Live.
//
// No se pueden compartir: el primer hueco tiene ABI __stdcall de UN argumento
// (0x6294F660, `ret 4`) y devuelve S_OK; el segundo recibe TRES argumentos
// (0x6294F6B0, `ret 12`) y devuelve E_NOTIMPL. Apuntar ambos al mismo stub
// desbalancea la pila x86 aunque sus cuerpos parezcan equivalentes.
HRESULT SHIM_COM XboxTcuiPendingNoOp(void*) noexcept {
  log::Write("Xbox TCUI pending request ignored by Win32 host");
  return S_OK;
}

HRESULT SHIM_COM XboxProfileCardNoOp(void*, void*, void*) noexcept {
  log::Write("Xbox profile-card request ignored by Win32 host");
  return E_NOTIMPL;
}

// --- Fases del arranque ---------------------------------------------------

// Ganchos que hacen falta antes de que el juego ejecute nada suyo.
void InstallCoreImportHooks(const game::PatchLayout& patches) noexcept {
#if REUWP_ENABLE_LOGGING
  if (hooks::ReplaceImportRva(
          game::Base(), patches.iat_cxx_throw_exception,
          reinterpret_cast<const void*>(&CxxThrowExceptionTrace),
          reinterpret_cast<void**>(&g_original_throw), "_CxxThrowException")) {
    log::Write("C++ exception trace installed");
  }
#endif

  // CreateFile2 no existe en Windows 7 y el CRT del juego la importa
  // directamente. Sin esto el proceso ni siquiera llega a su punto de entrada.
  if (hooks::ReplaceImportRva(game::Base(), patches.iat_create_file2,
                              apiset::CreateFile2Address(), nullptr,
                              "CreateFile2")) {
    log::Write("CreateFile2 Windows 7 compatibility installed");
  } else {
    log::Write("VirtualProtect CreateFile2 IAT failed");
  }

  log::Write(game::IsWindows7() ? "Windows 7 DXGI present compatibility enabled"
                                : "standard DXGI present path enabled");

  // Los dos huecos son contiguos, pero NO comparten ABI. Mantenerlos
  // separados es obligatorio en x86 porque __stdcall limpia la pila en el
  // llamado: 0x6294F660 hace `ret 4` y 0x6294F6B0 hace `ret 12`.
  const bool tcui_pending_patched =
      hooks::ReplaceImportRva(
          game::Base(), patches.iat_xbox_tcui[0],
          reinterpret_cast<const void*>(&XboxTcuiPendingNoOp), nullptr,
          "Xbox TCUI pending");
  const bool tcui_profile_patched =
      hooks::ReplaceImportRva(
          game::Base(), patches.iat_xbox_tcui[1],
          reinterpret_cast<const void*>(&XboxProfileCardNoOp), nullptr,
          "Xbox TCUI profile card");
  log::Write(tcui_pending_patched && tcui_profile_patched
                 ? "Xbox TCUI profile-card hooks disabled"
                 : "VirtualProtect Xbox TCUI IAT failed");
}

// Instala los ganchos de compatibilidad reconstruidos y, a continuacion, la
// ruta de audio correspondiente a la version detectada. 0.13.2 no pasa por
// aqui: su bootstrap y su tabla FMOD de 25 ranuras son independientes.
void InstallCompatibilityHooks(
    const game::StartupOperations* startup,
    const game::PatchLayout& patches) noexcept {
  if (startup != nullptr &&
      startup->install_before_common_compatibility != nullptr) {
    startup->install_before_common_compatibility();
  }

  hooks::InstallCompatibility(patches);

  if (startup != nullptr &&
      startup->install_after_common_compatibility != nullptr) {
    startup->install_after_common_compatibility();
  }

  // Audio installation stays between compatibility hooks and game bring-up.
  fmod::Install();
}

}  // namespace

WPARAM RunMessageLoop(void (*run_frame)() noexcept) noexcept {
  constexpr unsigned kMaxMessagesPerFrame = 64;
  constexpr unsigned kDispatcherHandlersPerFrame = 16;

  log::Write("Win32 message/frame loop entered");

  MSG message = {};
  bool quit = false;
  for (;;) {
    unsigned message_count = 0;
    while (message_count < kMaxMessagesPerFrame &&
           ::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        quit = true;
        break;
      }
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
      ++message_count;
    }
    if (quit) {
      break;
    }

    // 62946099..6294625D: only the 1.1.5 host drains the reconstructed
    // CoreDispatcher queue here. 0.15.10 branches directly to 0x62946274 and
    // must not consume these entries from the Win32 idle loop.
    if (game::HasCapability(game::VersionCapability::kDrainCoreDispatcher)) {
      winrt::ProcessQueuedHandlers(kDispatcherHandlersPerFrame);
    }

    // 62946274..6294627A: el host original resincroniza el modo/clip del
    // cursor en cada iteracion idle antes de entrar al frame moderno. No basta
    // con hacerlo en WM_SIZE/WM_ACTIVATE: el juego puede cambiar entre modo
    // absoluto y relativo sin generar un mensaje Win32 nuevo.
    input::SyncCursorMode(app_window::Handle());

    run_frame();

    // Sleep(0) cede el resto del cuanto solo si hay otro hilo listo. Sin esto
    // el bucle come un nucleo entero mientras el juego no dibuja; con un Sleep
    // mayor se perderian fotogramas.
    ::Sleep(0);
  }

  log::Write("message loop ended");
  // El host original retorna directamente en WM_QUIT. No restaura hooks ni
  // libera el grafo D3D aqui: ese estado pertenece al proceso del juego y la
  // salida normal de Win32Bootstrap deja que termine con el proceso.
  return message.wParam;
}

WPARAM Run() noexcept {
  const HMODULE game_module = ::GetModuleHandleW(nullptr);
  const game::Version version = game::Initialize(game_module);

  if (version == game::Version::kUnknown) {
    return static_cast<WPARAM>(game::BootstrapResult::kUnsupportedVersion);
  }
  const game::VersionProfile* const profile = game::CurrentProfile();
  if (profile == nullptr || profile->runtime == nullptr) {
    return static_cast<WPARAM>(game::BootstrapResult::kUnsupportedVersion);
  }
  if (profile->runtime->run != nullptr) {
    return profile->runtime->run();
  }
  if (profile->layout == nullptr) {
    return static_cast<WPARAM>(game::BootstrapResult::kUnsupportedVersion);
  }
  const game::Layout& layout = *profile->layout;
  versions::modern::input_backend::BindLayout(&layout.input);
  versions::modern::BindRuntimeLayouts(&layout.runtime, &layout.d3d,
                                       &layout.input);
  d3d::BindLayout(&layout.d3d);
  fmod::BindLayout(&layout.fmod);

  if (profile->runtime->bring_up == nullptr ||
      profile->runtime->run_frame == nullptr) {
    return static_cast<WPARAM>(game::BootstrapResult::kUnsupportedVersion);
  }

  log::Writef(SHIM_DISPLAY_NAME_A " %s Win32Bootstrap entered",
              game::VersionName());

  // Los manejadores de excepciones van lo primero: a partir de aqui todo lo que
  // hace el arranque puede fallar dentro de codigo ajeno, y sin traza un fallo
  // ahi es un cierre sin mensaje.
  diag::SetGameModuleBase(reinterpret_cast<uintptr_t>(game_module));
  diag::InstallHandlers();

  InstallCoreImportHooks(layout.patches);

  // RO_INIT_MULTITHREADED, no single-threaded: el juego crea objetos WinRT
  // desde sus hilos de carga, y con apartamento de un solo hilo cada uno de
  // esos accesos tendria que hacer marshalling.
  winrt::InitializeRuntime();

  local_folder::Prepare();

  InstallCompatibilityHooks(profile->startup, layout.patches);

  const HWND window = app_window::Create(
      reinterpret_cast<HINSTANCE>(game_module));
  if (window == nullptr) {
    // Se distingue que fallo por el ultimo error: si la clase no llego a
    // registrarse, CreateWindowExW falla con ERROR_CANNOT_FIND_WND_CLASS.
    return static_cast<WPARAM>(
        ::GetLastError() == ERROR_CANNOT_FIND_WND_CLASS
            ? game::BootstrapResult::kRegisterClassFailed
            : game::BootstrapResult::kCreateWindowFailed);
  }

  // El CoreWindow falso es una fachada sobre el HWND, asi que no puede
  // inicializarse antes de que el HWND exista.
  winrt::Initialize();
  winrt::InitializeActivation();

  // Version-specific startup work is registered by the active profile.
  // Critical pre-D3D work must finish before non-critical window hooks.
  if (profile->startup != nullptr &&
      profile->startup->install_pre_d3d != nullptr &&
      !profile->startup->install_pre_d3d()) {
    log::Write("startup stopped: critical pre-D3D compatibility patch failed");
    ::SetWindowTextW(
        window, SHIM_DISPLAY_NAME_W L" - compatibility patch failed");
    return static_cast<WPARAM>(
        game::BootstrapResult::kCompatibilityPatchFailed);
  }

  if (profile->startup != nullptr &&
      profile->startup->install_post_window != nullptr) {
    profile->startup->install_post_window();
  }

  // El gancho de activacion va DESPUES de tener los objetos en pie: desde el
  // instante en que se instala, el juego puede pedir cualquiera de ellos.
  //
  // Es lo que hace alcanzable todo lo demas. El juego esta compilado con
  // C++/CX, y cada `ref new` de un tipo de Windows pasa por esta unica funcion
  // de vccorlib; enganchar aqui cubre el modulo entero.
  {
    void* original = nullptr;
    if (hooks::ReplaceImportRva(
            game::Base(), layout.patches.iat_activation_factory,
            reinterpret_cast<const void*>(&winrt::GetActivationFactoryByName),
            &original, "GetActivationFactoryByPCWSTR")) {
      winrt::SetActivationFallback(
          reinterpret_cast<winrt::ActivationFactoryFn>(original));
      log::Write("WinRT activation redirect installed");
    } else {
      log::Write("VirtualProtect activation IAT failed");
    }
  }

  if (!profile->runtime->bring_up()) {
    log::Write("startup stopped: game bring-up unavailable");
    // Se entra igualmente al bucle de mensajes. La ventana existe y responde,
    // asi que el proceso se comporta como una aplicacion que no pinta nada en
    // vez de cerrarse de golpe, y se puede cerrar desde la barra de titulo.
  }

  return RunMessageLoop(profile->runtime->run_frame);
}

}  // namespace shim::bootstrap
