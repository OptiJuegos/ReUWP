#include "shim/version_runtime.h"

#include <cstring>

#include "shim/activation.h"
#include "shim/app_window.h"
#include "shim/bootstrap.h"
#include "shim/branding.h"
#include "shim/compat_hooks.h"
#include "shim/core_window.h"
#include "shim/crash_report.h"
#include "shim/fmod_install.h"
#include "shim/game_layout.h"
#include "shim/input.h"
#include "shim/local_folder.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/swapchain.h"
#include "shim/version_patches.h"
#include "shim/watchdog.h"
#include "shim/winrt_runtime.h"

namespace shim::versions::v0132 {
namespace {

// ---------------------------------------------------------------------------
// Minecraft 0.13.2
// ---------------------------------------------------------------------------
//
// Esta rama no comparte la ABI de BringUpGame(). FUN_629557e0 reserva primero
// un owner diminuto (0x1c bytes), deja que el juego cree dentro su renderer y
// dispositivo, monta la swap chain HWND sobre esos objetos y SOLO DESPUES crea
// DX::DeviceResources y MCPE_Host::AppMain. Mantenerlo separado evita que un
// offset correcto se use con una convencion de llamada equivocada.

struct V0132RenderTargetDesc {
  DWORD width;
  DWORD height;
  DWORD reserved0;
  DWORD reserved1;
  DWORD reserved2;
  void* swap_chain;
};

static_assert(sizeof(V0132RenderTargetDesc) == 0x18,
              "FUN_629557e0 pasa un descriptor de render de 24 bytes");
static_assert(offsetof(V0132RenderTargetDesc, width) == 0x00,
              "0.13.2 render descriptor width must stay at +0x00");
static_assert(offsetof(V0132RenderTargetDesc, height) == 0x04,
              "0.13.2 render descriptor height must stay at +0x04");
static_assert(offsetof(V0132RenderTargetDesc, swap_chain) == 0x14,
              "0.13.2 render descriptor swap chain must stay at +0x14");

// D3D11_VIEWPORT sin depender de d3d11.h. FUN_6295CF60 construye exactamente
// estos seis floats y copia los 24 bytes a renderer+0x94.
struct V0132Viewport {
  float top_left_x;
  float top_left_y;
  float width;
  float height;
  float min_depth;
  float max_depth;
};
static_assert(sizeof(V0132Viewport) == 0x18,
              "0.13.2 viewport must stay 24 bytes");


struct V0132RendererExtent {
  float width;
  float height;
  float scale_x;
  float scale_y;
};
static_assert(sizeof(V0132RendererExtent) == 0x10,
              "0.13.2 renderer extent must stay 16 bytes");

struct V0132State {
  void* resource_owner;
  void* renderer;
  void* device_resources;
  void* app_main;
  bool activated;
};

V0132State g_v0132 = {};

void* Read0132Pointer(void* object, DWORD offset, const char* what) noexcept {
  if (object == nullptr || ::IsBadReadPtr(object, offset + sizeof(void*))) {
    return nullptr;
  }
  void* value = *reinterpret_cast<void**>(object_access::Field(object, offset));
  if (value == nullptr || ::IsBadReadPtr(value, sizeof(void*))) {
    if (what != nullptr) {
      log::Writef("0.13.2 %s unavailable at +0x%X", what, offset);
    }
    return nullptr;
  }
  if (what != nullptr) {
    log::WritePointer(what, value);
  }
  return value;
}

bool CreateV0132ResourceOwner(HWND window) noexcept {
  const game::Version0132Layout& layout = game::Layout0132();

  // El RVA apunta a una ranura de IAT, no al malloc directamente.
  void* malloc_slot = game::Resolve(layout.game_malloc_iat);
  if (malloc_slot == nullptr || ::IsBadReadPtr(malloc_slot, sizeof(void*))) {
    log::Write("0.13.2 game malloc IAT unavailable");
    return false;
  }
  using MallocFn = void*(__cdecl*)(size_t);
  const auto game_malloc =
      reinterpret_cast<MallocFn>(*static_cast<void**>(malloc_slot));
  if (game_malloc == nullptr) {
    return false;
  }

  void* owner = game_malloc(0x1c);
  log::WritePointer("0.13.2 game owner allocation", owner);
  if (owner == nullptr) {
    return false;
  }
  memset(owner, 0, 0x1c);

  // 6295685f: ECX=owner; call base+0x5366e0; no stack arguments.
  using OwnerCtorFn = void*(__thiscall*)(void* owner);
  const auto owner_ctor = reinterpret_cast<OwnerCtorFn>(
      game::Resolve(layout.fn_resource_owner_ctor));
  if (owner_ctor == nullptr) {
    return false;
  }
  owner = owner_ctor(owner);
  if (owner == nullptr) {
    log::Write("0.13.2 resource-owner constructor returned null");
    return false;
  }
  g_v0132.resource_owner = owner;
  if (void* slot = game::Resolve(layout.resource_owner_slot)) {
    *static_cast<void**>(slot) = owner;
  }

  void* renderer =
      Read0132Pointer(owner, layout.owner_renderer_offset, "game renderer");
  if (renderer == nullptr) {
    return false;
  }
  g_v0132.renderer = renderer;

  // 62956b43: ECX=owner, push renderer, call base+0x5391d0.
  using GameD3DInitFn = void(__thiscall*)(void* owner, void* renderer);
  const auto d3d_init = reinterpret_cast<GameD3DInitFn>(
      game::Resolve(layout.fn_game_d3d_init));
  if (d3d_init == nullptr) {
    return false;
  }
  log::Write("0.13.2 calling game D3D11 device initializer");
  d3d_init(owner, renderer);
  log::Write("0.13.2 game D3D11 device initializer returned");

  void* device =
      Read0132Pointer(owner, layout.owner_device_offset, "game D3D11 device");
  void* context = Read0132Pointer(renderer, layout.renderer_context_offset,
                                    "game D3D11 context");
  if (device == nullptr || context == nullptr) {
    log::Write("0.13.2 game D3D device/context is null");
    return false;
  }

  if (!d3d::CreateForWindow(device, window)) {
    log::Write("0.13.2 HWND swap-chain creation failed");
    return false;
  }
  d3d::SetDeviceContext(context);

  int width = 0;
  int height = 0;
  app_window::ClientSize(&width, &height);
  V0132RenderTargetDesc desc = {};
  desc.width = static_cast<DWORD>(width);
  desc.height = static_cast<DWORD>(height);
  desc.swap_chain = d3d::SwapChain();

  // 62957061: ECX=owner, push renderer, push &desc, call base+0x538f90.
  // Es un __thiscall(owner, desc, renderer): usar fastcall aqui pondria
  // renderer en EDX y corromperia la ABI.
  using InitRenderTargetsFn = void(__thiscall*)(
      void* owner, V0132RenderTargetDesc* desc, void* renderer);
  const auto init_targets = reinterpret_cast<InitRenderTargetsFn>(
      game::Resolve(layout.fn_init_render_targets));
  if (init_targets == nullptr) {
    return false;
  }
  log::Write("0.13.2 calling game render-target initializer");
  init_targets(owner, &desc, renderer);
  log::Write("0.13.2 game render-target initializer returned");

  if (!d3d::InjectIntoRenderer(renderer)) {
    log::Write("0.13.2 failed to attach HWND swap chain to renderer");
    return false;
  }
  if (!d3d::InstallGamePresentHook()) {
    // El original tampoco convierte un fallo de VirtualProtect aqui en fallo
    // de D3D: simplemente pierde el gate de doble Present.
    log::Write("0.13.2 Present hook installation failed");
  }
  return d3d::RenderTargetView() != nullptr;
}

bool CreateV0132AppMain() noexcept {
  const game::Version0132Layout& layout = game::Layout0132();

  // 62957c68: xor ecx,ecx; call base+0x3ea440. Declarar un argumento
  // fastcall obliga al compilador a reproducir ese ECX=0.
  using DeviceResourcesCtorFn = void*(__thiscall*)(void* null_this);
  const auto make_resources = reinterpret_cast<DeviceResourcesCtorFn>(
      game::Resolve(layout.fn_device_resources_ctor));
  if (make_resources == nullptr) {
    return false;
  }
  log::Write("0.13.2 calling DX::DeviceResources constructor");
  g_v0132.device_resources = make_resources(nullptr);
  log::WritePointer("0.13.2 DX::DeviceResources",
                    g_v0132.device_resources);
  if (g_v0132.device_resources == nullptr) {
    return false;
  }

  // 62957d1c: ECX=&global_app_main; call base+0x3ea3b0. El constructor
  // escribe el AppMain real en esa ranura y devuelve un holder auxiliar.
  using AppMainCtorFn = void*(__thiscall*)(void** out_app_main);
  const auto make_app_main = reinterpret_cast<AppMainCtorFn>(
      game::Resolve(layout.fn_app_main_ctor));
  if (make_app_main == nullptr) {
    return false;
  }
  g_v0132.app_main = nullptr;
  diag::SetWatchedThread(::GetCurrentThreadId());
  diag::ArmConstructorWatchdog();
  log::Write("0.13.2 calling MCPE_Host::AppMain constructor");
  void* holder = make_app_main(&g_v0132.app_main);
  log::WritePointer("0.13.2 AppMain holder result", holder);
  log::WritePointer("0.13.2 MCPE_Host::AppMain", g_v0132.app_main);
  if (g_v0132.app_main == nullptr) {
    return false;
  }
  input::SetAppMain(g_v0132.app_main);
  return true;
}

unsigned int StartV0132Listeners(void* platform) noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  if (platform == nullptr) {
    return 0;
  }

  void* head = Read0132Pointer(platform, layout.platform_listener_list_offset,
                                 nullptr);
  if (head == nullptr) {
    return 0;
  }

  void* node = head;
  unsigned int started = 0;
  // El original limita a 64 listeners ARRANCADOS, no a 64 nodos visitados.
  // Se conserva ademas un fusible mas amplio por si una lista corrupta forma
  // un ciclo que no vuelve al head y contiene nodos sin listener.
  for (unsigned int visited = 0; started < 64 && visited < 256; ++visited) {
    if (::IsBadReadPtr(node, sizeof(void*))) {
      break;
    }
    node = *reinterpret_cast<void**>(node);
    if (node == nullptr || node == head || ::IsBadReadPtr(node, 0x18)) {
      break;
    }

    void* listener = *reinterpret_cast<void**>(object_access::Field(node, 0x14));
    void* start_entry = object_access::VtableEntry(listener, 0x14);
    if (listener == nullptr || start_entry == nullptr) {
      // Un nodo sin listener no invalida toda la lista.
      continue;
    }
    using ListenerStartFn = void(__thiscall*)(void* listener);
    reinterpret_cast<ListenerStartFn>(start_entry)(listener);
    ++started;
  }
  return started;
}

bool ActivateV0132App(HWND window) noexcept {
  const game::Version0132Layout& layout = game::Layout0132();
  void* app_main = g_v0132.app_main;
  if (app_main == nullptr || ::IsBadReadPtr(app_main, 0x14)) {
    return false;
  }

  void* platform =
      Read0132Pointer(app_main, layout.app_main_platform_offset,
                        "AppMain platform");
  void* game_object =
      Read0132Pointer(app_main, layout.app_main_game_offset, "AppMain game");
  if (platform == nullptr || game_object == nullptr) {
    return false;
  }

  // FUN_629557e0 toma tambien el AppPlatform desde un slot global y llama a su
  // virtual +0x28 antes de arrancar listeners.
  void* app_platform = nullptr;
  if (void* slot = game::Resolve(layout.app_platform_slot)) {
    if (!::IsBadReadPtr(slot, sizeof(void*))) {
      app_platform = *static_cast<void**>(slot);
    }
  }
  // 62958147..629582A5 comprueba tambien AppPlatform+0x13c antes de llamar la
  // virtual +0x28. No es un detalle de logging: si ese estado aun no existe el
  // original omite por completo la activacion.
  void* app_platform_state =
      Read0132Pointer(app_platform, layout.platform_state_offset, nullptr);
  if (app_platform_state != nullptr) {
    if (void* activate_entry = object_access::VtableEntry(app_platform, 0x28)) {
      using ActivateFn = void(__thiscall*)(void* app_platform);
      reinterpret_cast<ActivateFn>(activate_entry)(app_platform);
      log::Write("0.13.2 AppPlatform activated");
    }
  }

  RECT client = {};
  ::GetClientRect(window, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;

  if (void* resize_entry = object_access::VtableEntry(game_object, 0x4c)) {
    using ResizeFn = void(__thiscall*)(void* game, int width, int height,
                                      int reason);
    reinterpret_cast<ResizeFn>(resize_entry)(game_object, width, height, 0);
    log::Write("0.13.2 MinecraftClient Win32 size initialized");
  }

  if (!::IsBadWritePtr(platform, layout.platform_height_offset + sizeof(DWORD))) {
    *reinterpret_cast<DWORD*>(object_access::Field(platform, layout.platform_width_offset)) =
        static_cast<DWORD>(width);
    *reinterpret_cast<DWORD*>(object_access::Field(platform, layout.platform_height_offset)) =
        static_cast<DWORD>(height);
  }

  const unsigned int listeners = StartV0132Listeners(platform);
  if (!::IsBadWritePtr(object_access::Field(app_main, layout.app_main_frame_enabled_offset),
                       1)) {
    *object_access::Field(app_main, layout.app_main_frame_enabled_offset) = 1;
  }

  // Estado HID inicial observado despues de activar AppMain: absoluto, orden 0.
  void* state = Read0132Pointer(platform, layout.platform_state_offset, nullptr);
  if (state != nullptr && !::IsBadWritePtr(state, 0x4c)) {
    *(object_access::Field(state, 0x45)) = 0;
    *reinterpret_cast<DWORD*>(object_access::Field(state, 0x48)) = 0;
  }

  g_v0132.activated = true;
  log::Writef("0.13.2 AppMain activated; %u listener(s) started", listeners);
  return true;
}

bool ResizeV0132(int width, int height) noexcept {
  if (width <= 0 || height <= 0 || g_v0132.resource_owner == nullptr ||
      g_v0132.renderer == nullptr || d3d::SwapChain() == nullptr ||
      d3d::DeviceContext() == nullptr) {
    return false;
  }

  const game::Version0132Layout& layout = game::Layout0132();

  using ReleaseRenderTargetsFn = void(__thiscall*)(void* renderer);
  const auto release_targets = reinterpret_cast<ReleaseRenderTargetsFn>(
      game::Resolve(layout.fn_release_render_targets));
  using InitRenderTargetsFn = void(__thiscall*)(
      void* owner, V0132RenderTargetDesc* desc, void* renderer);
  const auto init_targets = reinterpret_cast<InitRenderTargetsFn>(
      game::Resolve(layout.fn_init_render_targets));
  if (release_targets == nullptr || init_targets == nullptr) {
    log::Write("0.13.2 resize target functions unavailable");
    return false;
  }

  void* const context = d3d::DeviceContext();
  void* const swap_chain = d3d::SwapChain();

  // 6295D042..6295D083: desengancha OM, Flush y deja que el renderer del
  // juego libere sus targets antes de tocar los backbuffers de DXGI.
  d3d::PrepareForResize();
  release_targets(g_v0132.renderer);

  // 6295D085..6295D098: el original llama directamente a
  // IDXGISwapChain::ResizeBuffers (vtable +0x34), con UN backbuffer y formato/
  // flags cero. No crea un RTV temporal del shim entre medias.
  using ResizeBuffersFn = HRESULT(SHIM_COM*)(void* swap_chain,
                                              UINT buffer_count, UINT width,
                                              UINT height, DWORD new_format,
                                              UINT flags);
  const auto resize_buffers = reinterpret_cast<ResizeBuffersFn>(
      object_access::VtableEntry(d3d::SwapChain(), 0x34));
  if (resize_buffers == nullptr) {
    log::Write("0.13.2 IDXGISwapChain::ResizeBuffers entry unavailable");
    return false;
  }
  const HRESULT resized =
      resize_buffers(swap_chain, 1, static_cast<UINT>(width),
                     static_cast<UINT>(height), 0, 0);
  log::WriteHResult("0.13.2 IDXGISwapChain::ResizeBuffers", resized);
  if (FAILED(resized)) {
    return false;
  }

  V0132RenderTargetDesc desc = {};
  desc.width = static_cast<DWORD>(width);
  desc.height = static_cast<DWORD>(height);
  desc.swap_chain = swap_chain;

  // 6295D0F7..6295D12C: __thiscall(owner, &desc, renderer).
  init_targets(g_v0132.resource_owner, &desc, g_v0132.renderer);

  // 6295D12E..6295D187: vuelve a sincronizar renderer+0x74/+0x88 con las
  // referencias que conserva el shim.
  if (!d3d::InjectIntoRenderer(g_v0132.renderer)) {
    log::Write("0.13.2 resize could not re-adopt renderer RTV");
    return false;
  }

  auto* renderer = static_cast<unsigned char*>(g_v0132.renderer);
  if (::IsBadWritePtr(renderer, layout.renderer_viewport_offset + 0x18)) {
    log::Write("0.13.2 renderer resize fields are not writable");
    return false;
  }

  *reinterpret_cast<DWORD*>(renderer + layout.renderer_width_offset) =
      static_cast<DWORD>(width);
  *reinterpret_cast<DWORD*>(renderer + layout.renderer_height_offset) =
      static_cast<DWORD>(height);

  // 6295D18A..6295D22C: viewport exacto de seis floats y copia literal a
  // renderer+0x94.
  V0132Viewport viewport = {};
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
  viewport.min_depth = 0.0f;
  viewport.max_depth = 1.0f;
  *reinterpret_cast<V0132Viewport*>(
      renderer + layout.renderer_viewport_offset) = viewport;

  using RSSetViewportsFn = void(SHIM_COM*)(void* context, UINT count,
                                           const V0132Viewport* viewport);
  const auto set_viewports = reinterpret_cast<RSSetViewportsFn>(
      object_access::VtableEntry(context, 0xb0));
  if (set_viewports != nullptr) {
    set_viewports(context, 1, &viewport);
  }

  void* render_target = d3d::RenderTargetView();
  void* depth_stencil = *reinterpret_cast<void**>(
      renderer + layout.renderer_depth_stencil_offset);
  using OMSetRenderTargetsFn = void(SHIM_COM*)(void* context, UINT count,
                                               void* const* targets,
                                               void* depth_stencil);
  const auto set_targets = reinterpret_cast<OMSetRenderTargetsFn>(
      object_access::VtableEntry(context, 0x84));
  if (set_targets != nullptr) {
    set_targets(context, 1, &render_target, depth_stencil);
  }

  // 6295D256..6295D262: el resize original presenta inmediatamente despues de
  // rebindear OM. El Present ya esta hookeado en 0.13.2, por lo que esta llamada
  // atraviesa la misma ruta DO_NOT_WAIT del DLL.
  using PresentFn = HRESULT(SHIM_COM*)(void* swap_chain, UINT sync_interval,
                                       UINT flags);
  const auto present = reinterpret_cast<PresentFn>(
      object_access::VtableEntry(d3d::SwapChain(), 0x20));
  if (present != nullptr) {
    const HRESULT result = present(swap_chain, 0, 0);
    log::WriteHResult("0.13.2 resize Present", result);
    if (FAILED(result)) {
      return false;
    }
  }

  if (g_v0132.app_main != nullptr) {
    void* platform = Read0132Pointer(
        g_v0132.app_main, layout.app_main_platform_offset, nullptr);
    if (platform != nullptr &&
        !::IsBadWritePtr(object_access::Field(platform, layout.platform_width_offset),
                         sizeof(DWORD)) &&
        !::IsBadWritePtr(object_access::Field(platform, layout.platform_height_offset),
                         sizeof(DWORD))) {
      *reinterpret_cast<DWORD*>(
          object_access::Field(platform, layout.platform_width_offset)) =
          static_cast<DWORD>(width);
      *reinterpret_cast<DWORD*>(
          object_access::Field(platform, layout.platform_height_offset)) =
          static_cast<DWORD>(height);
    }

    void* game_object = Read0132Pointer(
        g_v0132.app_main, layout.app_main_game_offset, nullptr);
    if (void* resize_entry = object_access::VtableEntry(game_object, 0x4c)) {
      using ClientResizeFn = void(__thiscall*)(void* game, int width,
                                               int height, int reason);
      reinterpret_cast<ClientResizeFn>(resize_entry)(game_object, width, height,
                                                       0);
    }
  }

  log::Writef("0.13.2 resize completed: %dx%d", width, height);
  return true;
}

bool BringUpV0132(HWND window) noexcept {
  if (!CreateV0132ResourceOwner(window)) {
    log::Write("0.13.2 startup stopped: initialize_game_d3d failed");
    return false;
  }
  if (!CreateV0132AppMain()) {
    log::Write("0.13.2 startup stopped: initialize_game_app failed");
    return false;
  }

  // El WndProc original ya puede entregar WM_SIZE en cuanto AppMain existe.
  // Registrar la 0.13.2 path antes de activar la plataforma evita caer en el
  // ResizeBuffers generico, que no libera/recrea los targets del renderer.
  app_window::SetResizeHandler(&ResizeV0132);

  // El DLL instala estos stubs despues de crear AppMain y antes de activar la
  // plataforma. Son compatibilidad opcional: un fallo de proteccion se registra
  // pero no invalida los objetos D3D/AppMain que ya fueron construidos.
  InstallServiceHooks();

  if (!ActivateV0132App(window)) {
    log::Write("0.13.2 startup stopped: activate_game_app failed");
    return false;
  }
  return true;
}

void RunV0132Frame() noexcept {
  // FUN_629557e0 llama a 0x62958C80 en cada pasada sin mensajes, antes incluso
  // de comprobar si D3D/AppMain estan listos. Esto hace efectivos inmediatamente
  // los cambios de modo de raton solicitados por el juego.
  input::SyncCursorMode(app_window::Handle());

  const game::Version0132Layout& layout = game::Layout0132();
  if (!g_v0132.activated || g_v0132.app_main == nullptr ||
      g_v0132.renderer == nullptr || d3d::DeviceContext() == nullptr ||
      d3d::RenderTargetView() == nullptr || d3d::SwapChain() == nullptr) {
    d3d::PresentV0132LoadingFrame();
    return;
  }

  // 629576F4: AppMain+0x10 es un gate real del frame loop. Aunque los objetos
  // D3D existan, mientras este byte sea cero el original sigue presentando el
  // fondo de carga y NO llama a AppMain::frame.
  unsigned char* frame_enabled =
      object_access::Field(g_v0132.app_main, layout.app_main_frame_enabled_offset);
  if (::IsBadReadPtr(frame_enabled, 1) || *frame_enabled == 0) {
    d3d::PresentV0132LoadingFrame();
    return;
  }

  // No se llama BeginFrame aqui. En la rama activa el original deja intactos
  // viewport/depth/OM state del renderer y entra directamente en AppMain::frame.
  void* game_object = Read0132Pointer(
      g_v0132.app_main, layout.app_main_game_offset, nullptr);
  if (game_object != nullptr && !::IsBadWritePtr(game_object, 0x1d5)) {
    *object_access::Field(game_object, 0x1d4) = 1;
  }

  using FrameFn = void(__thiscall*)(void* app_main);
  const auto frame =
      reinterpret_cast<FrameFn>(game::Resolve(layout.fn_app_main_frame));
  if (frame == nullptr) {
    d3d::PresentV0132LoadingFrame();
    return;
  }

  const unsigned long present_generation_before = d3d::PresentGeneration();
  frame(g_v0132.app_main);

  // 62957713..62957749: si AppMain ya llamo a Present, el hook cambio la
  // generacion y el host no vuelve a presentar. Si no cambio, se hace el
  // Present de reserva exactamente como en el bucle original.
  if (d3d::PresentGeneration() == present_generation_before) {
    d3d::PresentPlatformDefault();
  }
}

WPARAM RunInternal() noexcept {
  const HMODULE game_module = game::Base();
  log::Write(SHIM_DISPLAY_NAME_A " 0.13.2 Win32Bootstrap entered");

  diag::SetGameModuleBase(reinterpret_cast<uintptr_t>(game_module));
  diag::InstallHandlers();
  input::SetEnabled(false);

  winrt::InitializeRuntime();
  local_folder::Prepare0132();

  InstallFrameSleepHook();
  hooks::InstallV0132Compatibility();
  fmod::Install();
  if (!input::InstallHooks()) {
    log::Write("0.13.2 Win32 input hooks were not installed");
  }

  const HWND window =
      app_window::Create(reinterpret_cast<HINSTANCE>(game_module));
  if (window == nullptr) {
    return static_cast<WPARAM>(
        ::GetLastError() == ERROR_CANNOT_FIND_WND_CLASS
            ? game::BootstrapResult::kRegisterClassFailed
            : game::BootstrapResult::kCreateWindowFailed);
  }

  winrt::Initialize();
  winrt::InitializeActivation();
  InstallActivationHook();
  InstallHelpHook();
  InstallCustomSkinHook();
  InstallFullscreenHook();

  if (!BringUpV0132(window)) {
    ::SetWindowTextW(window, SHIM_DISPLAY_NAME_W L" 0.13.2 - startup failed");
  } else {
    input::SetEnabled(true);
    log::Write("0.13.2 Win32 input injection enabled");
  }

  return bootstrap::RunMessageLoop(&RunV0132Frame);
}


}  // namespace

WPARAM Run() noexcept {
  return RunInternal();
}

}  // namespace shim::versions::v0132
