#include "shim/version_runtime.h"

#include "shim/app_window.h"
#include "shim/game_layout.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/modern_runtime_core.h"
#include "shim/object_access.h"
#include "shim/swapchain.h"

namespace shim::versions::v01510 {
namespace {

unsigned char* g_app_frame_flag = nullptr;

// La cadena vacia que ambos constructores reciben como argumento.
//
// Es un objeto de cadena del CRT de MSVC con la optimizacion de cadena corta:
// todo a cero salvo la capacidad, que vale 15 y significa "los datos estan aqui
// dentro". El juego la usa como ruta de contenido adicional y una vacia le dice
// que no hay ninguna.
struct EmptyStringArgument {
  unsigned char storage[28];
};
static_assert(sizeof(EmptyStringArgument) == 0x1C,
              "MSVC string argument must stay 28 bytes");

EmptyStringArgument MakeEmptyStringArgument() noexcept {
  EmptyStringArgument argument = {};
  *reinterpret_cast<unsigned int*>(argument.storage + 0x18) = 15;
  return argument;
}

struct MaterialCompileArgs01510 {
  void* context;
  void* group;
  DWORD token;
};
static_assert(sizeof(MaterialCompileArgs01510) == 0x0C,
              "0.15.10 material compile args must stay 12 bytes");

bool CompileMaterialGroup01510(DWORD group_rva, const char* name) noexcept {
  auto* base = reinterpret_cast<unsigned char*>(game::Base());
  if (base == nullptr) {
    return false;
  }

  unsigned char* group = base + group_rva;
  auto** payload = reinterpret_cast<void**>(group + 0x0C);
  if (::IsBadReadPtr(payload, sizeof(void*)) || *payload == nullptr ||
      ::IsBadReadPtr(*payload, sizeof(DWORD))) {
    log::Writef("0.15.10 %s material group is unavailable", name);
    return false;
  }

  MaterialCompileArgs01510 args = {};
  args.context = base + 0x00B3A334;
  args.group = group;
  args.token = **reinterpret_cast<DWORD**>(payload);

  using CompileStepFn = unsigned char(__thiscall*)(MaterialCompileArgs01510*);
  const auto step = reinterpret_cast<CompileStepFn>(base + 0x002CD9B0);
  for (unsigned int attempt = 1; attempt <= 0x2000; ++attempt) {
    if (step(&args) != 0) {
      log::Writef("0.15.10 %s material group compiled in %u steps", name,
                  attempt);
      return true;
    }
  }

  log::Writef("0.15.10 %s material group compile hit iteration limit", name);
  return false;
}

void CompileMaterialGroups01510() noexcept {
  CompileMaterialGroup01510(0x00C67CC8, "entity/UI");
  CompileMaterialGroup01510(0x00C67D28, "terrain");
}

bool ResetInputState01510() noexcept {
  auto* sink = static_cast<unsigned char*>(input::Handler());
  if (sink == nullptr || ::IsBadWritePtr(sink + 0x44, sizeof(DWORD)) ||
      ::IsBadWritePtr(sink + 0x88, 1)) {
    return false;
  }

  *(sink + 0x88) = 0;
  *reinterpret_cast<DWORD*>(sink + 0x44) = 0;
  return true;
}

bool WritePlatformSize01510(void* platform, int width, int height) noexcept {
  if (platform == nullptr) {
    return false;
  }

  auto* bytes = static_cast<unsigned char*>(platform);
  if (::IsBadWritePtr(bytes + 0x25C, 8)) {
    return false;
  }

  *reinterpret_cast<DWORD*>(bytes + 0x25C) = static_cast<DWORD>(width);
  *reinterpret_cast<DWORD*>(bytes + 0x260) = static_cast<DWORD>(height);
  return true;
}

bool CallPrimarySize01510(void* game_object, int width, int height) noexcept {
  void* const entry = object_access::VtableEntry(game_object, 0x50);
  if (entry == nullptr) {
    return false;
  }

  using SizeFn = void(__thiscall*)(void*, int, int, int);
  reinterpret_cast<SizeFn>(entry)(game_object, width, height, 0);
  return true;
}

bool CallSecondarySize01510(void* game_object, int width, int height) noexcept {
  void* const entry = object_access::VtableEntry(game_object, 0x54);
  if (entry == nullptr) {
    return false;
  }

  using SizeFn = void(__thiscall*)(void*, int, int);
  reinterpret_cast<SizeFn>(entry)(game_object, width, height);
  return true;
}

bool SynchronizeWindowSize01510(int width, int height) noexcept {
  void* const app_main = modern::State().app_main;
  if (app_main == nullptr || ::IsBadReadPtr(app_main, 0x0C)) {
    return false;
  }

  auto* app = static_cast<unsigned char*>(app_main);
  void* const platform = *reinterpret_cast<void**>(app + 0x04);
  void* const game_object = *reinterpret_cast<void**>(app + 0x08);
  if (platform == nullptr || game_object == nullptr ||
      ::IsBadReadPtr(game_object, sizeof(void*))) {
    return false;
  }

  if (!WritePlatformSize01510(platform, width, height)) {
    return false;
  }

  const bool primary = CallPrimarySize01510(game_object, width, height);
  const bool secondary = CallSecondarySize01510(game_object, width, height);
  if (!primary || !secondary) {
    log::Write("0.15.10 resize: MinecraftGame size callback unavailable");
    return false;
  }

  log::Write(
      "0.15.10 HWND renderer targets, viewport, UI and HID size synchronized");
  return true;
}

bool ActivateAppMain01510() noexcept {
  if (modern::State().app_main == nullptr ||
      ::IsBadReadPtr(modern::State().app_main, 0x14)) {
    return false;
  }

  auto* app = static_cast<unsigned char*>(modern::State().app_main);
  void* const platform = *reinterpret_cast<void**>(app + 0x04);
  void* const game_object = *reinterpret_cast<void**>(app + 0x08);
  if (platform == nullptr || game_object == nullptr ||
      ::IsBadReadPtr(game_object, sizeof(void*))) {
    log::Write("0.15.10 activation: platform/game unavailable");
    return false;
  }

  int width = 0;
  int height = 0;
  app_window::ClientSize(&width, &height);
  if (width <= 0 || height <= 0) {
    log::Write("0.15.10 activation: invalid client size");
    return false;
  }

  // 6294706F..62947097: primer size callback sobre MinecraftGame antes de
  // reanudar el AppPlatform.
  CallPrimarySize01510(game_object, width, height);

  // 62946F29..62946F40: el tamano vive en AppPlatform+0x25C/+0x260, no en
  // AppMain. Esta diferencia quedaba oculta por el antiguo holder desplazado.
  auto* platform_bytes = static_cast<unsigned char*>(platform);
  if (!WritePlatformSize01510(platform, width, height)) {
    log::Write("0.15.10 activation: AppPlatform size fields are not writable");
    return false;
  }

  // 629470E0..6294714D: AppPlatform::resume() (vtable +0x48), seguido del
  // visible/active byte en AppPlatform+8.
  void* const resume_entry = object_access::VtableEntry(platform, 0x48);
  if (resume_entry == nullptr) {
    log::Write("0.15.10 activation: AppPlatform vtable+0x48 unavailable");
    return false;
  }
  using ResumeFn = void(__thiscall*)(void*);
  reinterpret_cast<ResumeFn>(resume_entry)(platform);
  if (::IsBadWritePtr(platform_bytes + 0x08, 1)) {
    return false;
  }
  *(platform_bytes + 0x08) = 1;

  // 62947198..62947253: MinecraftGame listener start (vtable +0x14).
  void* const listener_start = object_access::VtableEntry(game_object, 0x14);
  if (listener_start == nullptr) {
    log::Write("0.15.10 activation: MinecraftGame vtable+0x14 unavailable");
    return false;
  }
  using ListenerStartFn = void(__thiscall*)(void*);
  reinterpret_cast<ListenerStartFn>(listener_start)(game_object);
  log::Write("AppMain MinecraftClient listener started");

  // 62947299..6294735A: sincronizacion posterior de size/pass-list.
  CallPrimarySize01510(game_object, width, height);
  CallSecondarySize01510(game_object, width, height);

  // 62946FB7: el byte de frame pertenece al AppMain real.
  if (::IsBadWritePtr(app + 0x10, 1)) {
    return false;
  }
  *(app + 0x10) = 1;

  // Desde aqui input::Handler() recorre AppMain -> AppPlatform -> InputHandler.
  input::SetAppMain(modern::State().app_main);
  void* const handler = input::Handler();
  bool input_ready = false;
  if (handler != nullptr && !::IsBadReadPtr(handler, 0x14)) {
    void* const sink = *reinterpret_cast<void**>(
        static_cast<unsigned char*>(handler) + 0x10);
    input_ready = sink != nullptr && !::IsBadReadPtr(sink, 0x2AC);
  }

  // 629473C2..62947534: empieza en cursor absoluto, resetea el estado HID y
  // sincroniza inmediatamente contra el HWND.
  input::RequestCursorMode(false);
  input::InvalidateCursorClip();
  const bool input_state_reset = ResetInputState01510();
  input::SetEnabled(input_ready && input_state_reset);
  input::SyncCursorMode(app_window::Handle());
  log::Writef("0.15.10 Win32 input injection %s",
              input_ready && input_state_reset
                  ? "enabled"
                  : "disabled (handler/reset unavailable)");
  return true;
}

bool CreateAppMain01510() noexcept {
  void* const entry = modern::Context().make_app_main;
  if (entry == nullptr) {
    return false;
  }

  EmptyStringArgument argument = MakeEmptyStringArgument();
  using ConstructFn = void*(__fastcall*)(void** out_app_main,
                                         EmptyStringArgument* argument);
  const auto construct = reinterpret_cast<ConstructFn>(entry);
  modern::State().app_main = nullptr;
  log::Write("calling AppMain constructor");
  const void* result = construct(&modern::State().app_main, &argument);
  log::WritePointer("AppMain holder result", result);
  log::WritePointer("AppMain pointer", modern::State().app_main);
  return modern::State().app_main != nullptr;
}

bool Resize01510(int width, int height) noexcept {
  if (!modern::ResizeGraphics(width, height,
                              "0.15.10 IDXGISwapChain::ResizeBuffers")) {
    return false;
  }

  return SynchronizeWindowSize01510(width, height);
}

bool ValidateFrameBindings01510() noexcept {
  auto& state = modern::State();
  state.frame_bindings_valid = false;
  if (modern::Context().app_main_frame == nullptr || state.app_main == nullptr ||
      ::IsBadReadPtr(state.app_main, 0x14)) {
    log::Write("0.15.10 frame bindings are unavailable");
    return false;
  }

  g_app_frame_flag = nullptr;
  if (::IsBadReadPtr(state.app_main, 0x0C)) {
    return false;
  }

  auto* const app = static_cast<unsigned char*>(state.app_main);
  void* const game_object = *reinterpret_cast<void**>(app + 0x08);
  if (game_object == nullptr || ::IsBadReadPtr(game_object, sizeof(void*))) {
    return false;
  }

  auto* const frame_flag = static_cast<unsigned char*>(game_object) + 0x230;
  if (::IsBadWritePtr(frame_flag, 1)) {
    return false;
  }

  g_app_frame_flag = frame_flag;
  state.frame_bindings_valid = true;
  return true;
}

void ResetAppFrameFlag01510() noexcept {
  if (g_app_frame_flag != nullptr && !::IsBadWritePtr(g_app_frame_flag, 1)) {
    *g_app_frame_flag = 0;
  }
}

void PumpInputSubmit01510() noexcept {
  input::PumpSubmit();
}

void PresentFallback01510() noexcept {
  d3d::Present(false);
}

}  // namespace

bool BringUp() noexcept {
  modern::ResetState();
  g_app_frame_flag = nullptr;
  if (!modern::InitializeRuntimeContext()) {
    log::Write(
        "startup stopped: 0.15.10 runtime context initialization failed");
    return false;
  }

  void* owner = modern::CreateGraphicsOwner("0.15.10 graphics owner");
  if (owner == nullptr) {
    log::Write("startup stopped: 0.15.10 graphics owner construction failed");
    return false;
  }
  log::Write("0.15.10 graphics owner constructed");

  if (!modern::InitializeGraphics(owner)) {
    return false;
  }

  if (!CreateAppMain01510()) {
    log::Write("startup stopped: 0.15.10 AppMain construction returned null");
    return false;
  }
  input::SetAppMain(modern::State().app_main);

  if (modern::CreateDeviceResources(
          "0.15.10 DeviceResources holder result",
          "0.15.10 DX::DeviceResources") == nullptr) {
    log::Write("startup stopped: 0.15.10 DX::DeviceResources construction failed");
    return false;
  }

  CompileMaterialGroups01510();
  if (!ActivateAppMain01510()) {
    log::Write("startup stopped: 0.15.10 AppMain activation failed");
    return false;
  }
  if (!ValidateFrameBindings01510()) {
    log::Write("0.15.10 frame binding validation failed");
  }

  app_window::SetResizeHandler(&Resize01510);
  if (!input::InstallLifecycleHooks()) {
    log::Write("0.15.10 ChatScreen hooks unavailable");
  }
  return true;
}

void RunFrame() noexcept {
  static const modern::FramePlan plan = {
      "first 0.15.10 AppMain update/render begin",
      &d3d::Present01510LoadingFrame,
      nullptr,
      &ResetAppFrameFlag01510,
      nullptr,
      &PumpInputSubmit01510,
      &PresentFallback01510,
  };
  modern::RunModernFrame(plan);
}

}  // namespace shim::versions::v01510
