#include "shim/version_runtime.h"

#include "shim/app_model.h"
#include "shim/app_window.h"
#include "shim/core_window.h"
#include "shim/crash_report.h"
#include "shim/game_layout.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/modern_runtime_core.h"
#include "shim/patch.h"
#include "shim/swapchain.h"
#include "shim/watchdog.h"
#include "shim/x86_abi.h"

namespace shim::versions::v115 {
namespace {

struct EmptyStringArgument115 {
  unsigned char storage[32];
};
static_assert(sizeof(EmptyStringArgument115) == 0x20,
              "1.1.5 MSVC string argument must stay 32 bytes");

EmptyStringArgument115 MakeEmptyStringArgument115() noexcept {
  EmptyStringArgument115 argument = {};
  *reinterpret_cast<unsigned int*>(argument.storage + 0x1C) = 15;
  return argument;
}

bool CallAppMainResize115() noexcept {
  if (modern::State().app_main == nullptr) {
    return false;
  }

  void* const resize_entry = modern::Context().app_main_resize;
  constexpr unsigned char kResizePrologue[3] = {0x55, 0x8B, 0xEC};
  if (resize_entry == nullptr ||
      !hooks::MatchesSignature(resize_entry, kResizePrologue,
                               sizeof(kResizePrologue))) {
    log::Write("Minecraft 1.1.5 AppMain resize entry has invalid prologue");
    return false;
  }

  void* const size_source = winrt::XamlSizeSourceSingleton();
  using ResizeFn = void(__thiscall*)(void* app_main, void* size_source,
                                     int trailing_zero);
  const auto resize = reinterpret_cast<ResizeFn>(resize_entry);
  resize(modern::State().app_main, size_source, 0);
  return true;
}

bool ActivateAppMain115() noexcept {
  if (modern::State().app_main == nullptr ||
      ::IsBadReadPtr(modern::State().app_main, 0x14)) {
    return false;
  }

  auto* const app = static_cast<unsigned char*>(modern::State().app_main);
  void* const app_platform = *reinterpret_cast<void**>(app + 0x04);
  if (app_platform == nullptr || ::IsBadWritePtr(
                                   static_cast<unsigned char*>(app_platform) +
                                       0x08,
                                   1) ||
      ::IsBadWritePtr(app + 0x10, 1)) {
    log::Write("1.1.5 activation: AppPlatform/AppMain unavailable");
    return false;
  }

  const DWORD input_sink_offset = modern::Context().input_to_event_sink;

  *reinterpret_cast<unsigned char*>(
      static_cast<unsigned char*>(app_platform) + 0x08) = 1;
  *(app + 0x10) = 1;

  log::Write("Minecraft 1.1.5 initial AppMain resize begin");
  if (!CallAppMainResize115()) {
    return false;
  }
  log::Write("Minecraft 1.1.5 initial AppMain resize returned");

  // sub_62950800(AppPlatform) lleva al InputHandler; el original considera
  // valida la inyeccion solo si InputHandler+0x10 contiene un sink legible de
  // al menos 0x1D0 bytes.
  input::SetAppMain(modern::State().app_main);
  void* const handler = input::Handler();
  bool input_ready = false;
  if (handler != nullptr &&
      !::IsBadReadPtr(static_cast<unsigned char*>(handler) +
                          input_sink_offset,
                      sizeof(void*))) {
    void* const sink = *reinterpret_cast<void**>(
        static_cast<unsigned char*>(handler) + input_sink_offset);
    input_ready = sink != nullptr && !::IsBadReadPtr(sink, 0x1D0);
  }
  input::SetEnabled(input_ready);
  log::Write(input_ready
                 ? "Win32 input injection enabled for Minecraft 1.1.5 ABI"
                 : "Minecraft 1.1.5 HID/InputHandler is unavailable");
  log::Write("Minecraft 1.1.5 HWND frame callback enabled");
  return true;
}

bool CreateAppMain115() noexcept {
  void* const entry = modern::Context().make_app_main;
  if (entry == nullptr) {
    return false;
  }

  EmptyStringArgument115 argument = MakeEmptyStringArgument115();
  // 629458E7..629458FE: el primer holder auxiliar contiene el CoreWindow
  // singleton del shim; XAML panel y pointer source empiezan nulos.
  void* aux0 = winrt::CoreWindowSingleton();
  void* aux1 = nullptr;
  void* aux2 = nullptr;

  // FUN_629550F0 carga &dword_62998EF8 en ECX. Por tanto el primer parametro
  // es un void** de salida; el objeto resultante no esta inline en ese buffer.
  //
  // 62955102..62955105: that host pushes the three auxiliary holders, loads the
  // register pair and then releases the tail itself with `add esp, 0Ch`. The
  // factory at game+0xAC6C0 ends in a bare `ret`, so the tail belongs to the
  // caller and this call must not go through a __fastcall declaration.
  const void* const stack_arguments[] = {&aux0, &aux1, &aux2};
  modern::State().app_main = nullptr;
  diag::SetWatchedThread(::GetCurrentThreadId());
  diag::SetConstructorActive(true);
  diag::ArmConstructorWatchdog();
  const unsigned int string_capacity =
      *reinterpret_cast<const unsigned int*>(argument.storage + 0x1C);
  log::Writef("1.1.5 AppMain ABI entry=%p out=%p str=%p cap=%lu",
              entry, &modern::State().app_main, &argument,
              static_cast<unsigned long>(string_capacity));
  log::Writef("1.1.5 AppMain ABI aux slots=%p/%p/%p",
              &aux0, &aux1, &aux2);
  log::Writef("1.1.5 AppMain ABI aux values=%p/%p/%p",
              aux0, aux1, aux2);
  diag::Set115AppMainAbiSnapshot(
      reinterpret_cast<uintptr_t>(entry),
      reinterpret_cast<uintptr_t>(&modern::State().app_main),
      reinterpret_cast<uintptr_t>(&argument), string_capacity,
      reinterpret_cast<uintptr_t>(&aux0), reinterpret_cast<uintptr_t>(&aux1),
      reinterpret_cast<uintptr_t>(&aux2), reinterpret_cast<uintptr_t>(aux0),
      reinterpret_cast<uintptr_t>(aux1), reinterpret_cast<uintptr_t>(aux2));
  const bool copy_probe_installed = diag::Install115MemoryCopyProbe();
  log::Write(copy_probe_installed
                 ? "1.1.5 memory-copy attribution probe installed"
                 : "1.1.5 memory-copy attribution probe unavailable");
  log::Write("1.1.5 AppMain factory begin");
  diag::Set115MemoryCopyProbeActive(true);
  const void* result = hooks::x86::CallRegisterCallerClean(
      entry, &modern::State().app_main, &argument, stack_arguments,
      CountOf(stack_arguments));
  diag::Set115MemoryCopyProbeActive(false);
  diag::SetConstructorActive(false);
  log::WritePointer("1.1.5 AppMain factory result", result);
  log::WritePointer("1.1.5 AppMain pointer", modern::State().app_main);
  if (modern::State().app_main != nullptr) {
    winrt::SetXamlAppMain(modern::State().app_main);
  }
  return modern::State().app_main != nullptr;
}

bool Resize115(int width, int height) noexcept {
  if (!modern::ResizeGraphics(width, height,
                              "1.1.5 IDXGISwapChain::ResizeBuffers")) {
    return false;
  }
  if (!CallAppMainResize115()) {
    return false;
  }
  input::InvalidateCursorClip();
  log::Write("Minecraft 1.1.5 HWND target and UI size synchronized");
  return true;
}

bool ValidateFrameBindings115() noexcept {
  auto& state = modern::State();
  auto& context = modern::Context();
  state.frame_bindings_valid = false;
  if (state.app_main == nullptr || ::IsBadReadPtr(state.app_main, 0x14)) {
    return false;
  }

  constexpr unsigned char kFramePrologue[3] = {0x55, 0x8B, 0xEC};
  if (context.app_main_frame == nullptr ||
      context.platform_frame_entry == nullptr ||
      ::IsBadReadPtr(context.app_main_frame, sizeof(kFramePrologue)) ||
      ::IsBadReadPtr(context.platform_frame_entry, sizeof(kFramePrologue)) ||
      !hooks::MatchesSignature(context.app_main_frame, kFramePrologue,
                               sizeof(kFramePrologue)) ||
      !hooks::MatchesSignature(context.platform_frame_entry, kFramePrologue,
                               sizeof(kFramePrologue))) {
    log::Write("Minecraft 1.1.5 frame entry validation failed");
    return false;
  }
  state.frame_bindings_valid = true;
  return true;
}

bool HasAppPlatform115(void* app_main) noexcept {
  auto* const app = static_cast<unsigned char*>(app_main);
  return *reinterpret_cast<void**>(app + 0x04) != nullptr;
}

void RunPlatformFrame115(void* app_main) noexcept {
  auto* const app = static_cast<unsigned char*>(app_main);
  void* const app_platform = *reinterpret_cast<void**>(app + 0x04);
  void* const platform = modern::Context().platform_frame_entry;
  if (app_platform == nullptr || platform == nullptr) {
    return;
  }

  using PlatformFrameFn = void(__fastcall*)(void*);
  reinterpret_cast<PlatformFrameFn>(platform)(app_platform);
}

void PresentFallback115() noexcept {
  d3d::PresentPlatformDefault();
}

}  // namespace

bool BringUp() noexcept {
  modern::ResetState();
  if (!modern::InitializeRuntimeContext()) {
    log::Write(
        "startup stopped: 1.1.5 runtime context initialization failed");
    return false;
  }

  void* owner = modern::CreateGraphicsOwner("1.1.5 graphics owner");
  if (owner == nullptr) {
    log::Write("startup stopped: 1.1.5 graphics owner construction failed");
    return false;
  }
  log::Write("1.1.5 graphics owner constructed");

  if (!modern::InitializeGraphics(owner)) {
    return false;
  }

  if (modern::CreateDeviceResources(
          "Minecraft 1.1.5 DeviceResources holder result",
          "Minecraft 1.1.5 DeviceResources pointer") == nullptr) {
    log::Write("startup stopped: 1.1.5 DX::DeviceResources construction failed");
    return false;
  }

  if (!CreateAppMain115()) {
    log::Write("startup stopped: 1.1.5 AppMain construction returned null");
    return false;
  }
  input::SetAppMain(modern::State().app_main);

  if (!ActivateAppMain115()) {
    log::Write("startup stopped: 1.1.5 AppMain activation failed");
    return false;
  }
  if (!ValidateFrameBindings115()) {
    log::Write("1.1.5 frame binding validation failed");
  }

  app_window::SetResizeHandler(&Resize115);
  return true;
}

void RunFrame() noexcept {
  static const modern::FramePlan plan = {
      "Minecraft 1.1.5 first frame begin",
      nullptr,
      &HasAppPlatform115,
      nullptr,
      &RunPlatformFrame115,
      nullptr,
      &PresentFallback115,
  };
  modern::RunModernFrame(plan);
}

}  // namespace shim::versions::v115
