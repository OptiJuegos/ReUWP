#include "shim/modern_runtime_core.h"

#include <cstddef>
#include <cstring>

#include "shim/app_window.h"
#include "shim/compat_hooks.h"
#include "shim/game_layout.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/swapchain.h"

namespace shim::versions::modern {
namespace {

RuntimeState g_state = {};

void* ReadComPointer(void* object, DWORD offset, const char* what) noexcept {
  if (object == nullptr || offset == 0) {
    return nullptr;
  }

  void* value = *reinterpret_cast<void**>(
      static_cast<unsigned char*>(object) + offset);
  if (value == nullptr || ::IsBadReadPtr(value, sizeof(void*))) {
    log::Writef("%s: not a readable pointer at +0x%X", what, offset);
    return nullptr;
  }

  void* const* vtable = *static_cast<void* const* const*>(value);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 3 * sizeof(void*))) {
    log::Writef("%s: pointer at +0x%X has no readable vtable", what, offset);
    return nullptr;
  }

  log::WritePointer(what, value);
  return value;
}

struct ModernRenderTargetDesc {
  DWORD width;
  DWORD height;
  DWORD reserved[5];
  void* swap_chain;
};

static_assert(sizeof(ModernRenderTargetDesc) == 0x20,
              "modern render-target descriptor must stay 32 bytes");
static_assert(offsetof(ModernRenderTargetDesc, swap_chain) == 0x1C,
              "modern swap-chain field must stay at +0x1C");

}  // namespace

RuntimeState& State() noexcept {
  return g_state;
}

const RuntimeContext& Context() noexcept {
  return g_state.context;
}

void ResetState() noexcept {
  const game::RuntimeLayout* const runtime_layout = g_state.runtime_layout;
  const game::D3DLayout* const d3d_layout = g_state.d3d_layout;
  const game::InputLayout* const input_layout = g_state.input_layout;
  g_state = {};
  g_state.runtime_layout = runtime_layout;
  g_state.d3d_layout = d3d_layout;
  g_state.input_layout = input_layout;
}

void BindRuntimeLayouts(const game::RuntimeLayout* runtime_layout,
                        const game::D3DLayout* d3d_layout,
                        const game::InputLayout* input_layout) noexcept {
  g_state.runtime_layout = runtime_layout;
  g_state.d3d_layout = d3d_layout;
  g_state.input_layout = input_layout;
}

bool InitializeRuntimeContext() noexcept {
  RuntimeContext& context = g_state.context;
  context = {};
  if (g_state.runtime_layout == nullptr || g_state.d3d_layout == nullptr ||
      g_state.input_layout == nullptr) {
    log::Write("modern runtime layouts are not bound");
    return false;
  }

  context.resource_owner_size = g_state.d3d_layout->resource_owner_size;
  context.alloc_resource_owner =
      game::Resolve(g_state.d3d_layout->fn_alloc_resource_owner);
  context.construct_resource_owner =
      game::Resolve(g_state.d3d_layout->fn_construct_resource_owner);
  context.resource_owner_slot =
      static_cast<void**>(game::Resolve(g_state.d3d_layout->resource_owner_slot));
  context.init_d3d_device = game::Resolve(g_state.d3d_layout->fn_init_d3d_device);
  context.make_device_resources =
      game::Resolve(g_state.d3d_layout->fn_make_device_resources);
  context.device_resources_slot =
      static_cast<void**>(game::Resolve(g_state.d3d_layout->device_resources_slot));
  context.make_app_main = game::Resolve(g_state.runtime_layout->fn_make_app_main);
  context.init_render_targets =
      game::Resolve(g_state.d3d_layout->fn_init_render_targets);
  context.release_render_targets =
      game::Resolve(g_state.d3d_layout->fn_release_render_targets);
  context.app_main_frame = game::Resolve(g_state.runtime_layout->fn_app_main_frame);
  context.platform_frame_entry =
      game::Resolve(g_state.runtime_layout->fn_platform_frame_entry);
  context.app_main_resize = game::Resolve(g_state.runtime_layout->fn_app_main_resize);
  context.owner_to_renderer = g_state.d3d_layout->owner_to_renderer;
  context.owner_to_device = g_state.d3d_layout->owner_to_device;
  context.renderer_to_context = g_state.d3d_layout->renderer_to_context;
  context.renderer_to_depth_target = g_state.d3d_layout->renderer_to_depth_target;
  context.renderer_to_back_buffer = g_state.d3d_layout->renderer_to_back_buffer;
  context.input_to_event_sink = g_state.input_layout->input_to_event_sink;

  const bool valid =
      context.resource_owner_size != 0 &&
      context.alloc_resource_owner != nullptr &&
      context.construct_resource_owner != nullptr &&
      context.init_d3d_device != nullptr &&
      context.make_device_resources != nullptr &&
      context.make_app_main != nullptr &&
      context.init_render_targets != nullptr &&
      context.release_render_targets != nullptr &&
      context.app_main_frame != nullptr && context.owner_to_renderer != 0 &&
      context.owner_to_device != 0 && context.renderer_to_context != 0 &&
      context.renderer_to_depth_target != 0;
  if (!valid) {
    log::Write("modern runtime context is incomplete for the active profile");
  }
  return valid;
}

bool BindRendererRuntimeContext(void* renderer) noexcept {
  RuntimeContext& context = g_state.context;
  context.depth_target_slot = nullptr;
  context.back_buffer_slot = nullptr;
  if (renderer == nullptr || context.renderer_to_depth_target == 0) {
    return false;
  }

  auto** const depth_target_slot = reinterpret_cast<void**>(
      static_cast<unsigned char*>(renderer) + context.renderer_to_depth_target);
  if (::IsBadReadPtr(depth_target_slot, sizeof(void*))) {
    return false;
  }
  context.depth_target_slot = depth_target_slot;

  if (context.renderer_to_back_buffer != 0) {
    auto** const back_buffer_slot = reinterpret_cast<void**>(
        static_cast<unsigned char*>(renderer) + context.renderer_to_back_buffer);
    if (::IsBadReadPtr(back_buffer_slot, sizeof(void*)) ||
        ::IsBadWritePtr(back_buffer_slot, sizeof(void*))) {
      context.depth_target_slot = nullptr;
      return false;
    }
    context.back_buffer_slot = back_buffer_slot;
  }
  return true;
}

void* GameRenderer(void* owner) noexcept {
  if (owner == nullptr || g_state.context.owner_to_renderer == 0) {
    return nullptr;
  }

  void* renderer = *reinterpret_cast<void**>(
      static_cast<unsigned char*>(owner) + g_state.context.owner_to_renderer);
  log::WritePointer("game renderer", renderer);
  return renderer;
}

void* GameDevice(void* owner) noexcept {
  return ReadComPointer(owner, g_state.context.owner_to_device,
                        "game D3D11 device");
}

void* GameContext(void* renderer) noexcept {
  return ReadComPointer(renderer, g_state.context.renderer_to_context,
                        "game D3D11 context");
}

void* DepthTarget() noexcept {
  return g_state.context.depth_target_slot != nullptr
             ? *g_state.context.depth_target_slot
             : nullptr;
}

void* CreateGraphicsOwner(const char* log_name) noexcept {
  RuntimeContext& context = g_state.context;
  if (context.resource_owner_size == 0 ||
      context.alloc_resource_owner == nullptr ||
      context.construct_resource_owner == nullptr ||
      context.init_d3d_device == nullptr) {
    return nullptr;
  }

  using AllocFn = void*(__cdecl*)(unsigned int size);
  const auto alloc = reinterpret_cast<AllocFn>(context.alloc_resource_owner);
  void* memory = alloc(context.resource_owner_size);
  if (memory == nullptr) {
    return nullptr;
  }
  memset(memory, 0, context.resource_owner_size);

  using ConstructFn = void*(__fastcall*)(void* owner);
  const auto construct =
      reinterpret_cast<ConstructFn>(context.construct_resource_owner);
  void* owner = construct(memory);
  if (owner == nullptr) {
    return nullptr;
  }

  if (context.resource_owner_slot != nullptr) {
    *context.resource_owner_slot = owner;
  }

  void* renderer = GameRenderer(owner);
  if (renderer == nullptr) {
    return nullptr;
  }

  using InitFn = void(__fastcall*)(void* owner, void* unused_edx,
                                    void* renderer);
  const auto init = reinterpret_cast<InitFn>(context.init_d3d_device);
  init(owner, nullptr, renderer);

  hooks::FinalizeRetainedD3DDeviceFallback(
      owner, context.owner_to_device, renderer, context.renderer_to_context);

  if (log_name != nullptr) {
    log::WritePointer(log_name, owner);
  }
  return owner;
}

void* CreateDeviceResources(const char* holder_log_name,
                            const char* pointer_log_name) noexcept {
  RuntimeContext& context = g_state.context;
  using MakeFn = void*(__fastcall*)(void** out);
  const auto make = reinterpret_cast<MakeFn>(context.make_device_resources);
  if (make == nullptr) {
    return nullptr;
  }

  void* resources = nullptr;
  const void* holder = make(&resources);
  if (holder_log_name != nullptr) {
    log::WritePointer(holder_log_name, holder);
  }
  if (pointer_log_name != nullptr) {
    log::WritePointer(pointer_log_name, resources);
  }
  if (resources == nullptr) {
    return nullptr;
  }

  if (context.device_resources_slot != nullptr) {
    *context.device_resources_slot = resources;
  }
  return resources;
}

bool InitRenderTargets(void* owner, void* renderer, int width,
                       int height) noexcept {
  if (owner == nullptr || renderer == nullptr || width <= 0 || height <= 0 ||
      d3d::SwapChain() == nullptr) {
    return false;
  }

  using InitFn = void(__fastcall*)(void* owner, void* unused_edx,
                                    ModernRenderTargetDesc* desc,
                                    void* renderer);
  const auto init =
      reinterpret_cast<InitFn>(g_state.context.init_render_targets);
  if (init == nullptr) {
    return false;
  }

  ModernRenderTargetDesc desc = {};
  desc.width = static_cast<DWORD>(width);
  desc.height = static_cast<DWORD>(height);
  desc.swap_chain = d3d::SwapChain();

  log::Write("calling game render-target initializer");
  init(owner, nullptr, &desc, renderer);
  log::Write("game render-target initializer returned");
  return d3d::InjectIntoRenderer(renderer);
}

bool InitializeGraphics(void* owner) noexcept {
  if (owner == nullptr) {
    return false;
  }

  void* renderer = GameRenderer(owner);
  if (renderer == nullptr) {
    log::Write("startup stopped: game renderer creation returned null");
    return false;
  }

  g_state.graphics_owner = owner;
  g_state.game_renderer = renderer;
  if (!BindRendererRuntimeContext(renderer)) {
    log::Write("startup stopped: renderer runtime slots are unavailable");
    return false;
  }

  const HWND window = app_window::Handle();
  if (window == nullptr) {
    log::Write("startup stopped: HWND is unavailable during graphics bring-up");
    return false;
  }

  void* device = GameDevice(owner);
  void* context = GameContext(renderer);
  if (device == nullptr || context == nullptr) {
    log::Write("startup stopped: game D3D11 device/context is unavailable");
    return false;
  }

  d3d::SetDeviceContext(context);
  if (!d3d::InstallModernContextHooks(window)) {
    log::Write("D3D11 HWND context hooks unavailable");
  }

  if (!d3d::CreateForWindow(device, window)) {
    log::Write("startup stopped: HWND swap chain creation failed");
    return false;
  }

  int width = 0;
  int height = 0;
  app_window::ClientSize(&width, &height);
  if (!InitRenderTargets(owner, renderer, width, height)) {
    log::Write("startup stopped: game render-target initialization failed");
    return false;
  }

  if (!d3d::RetainDeviceContext()) {
    log::Write("startup stopped: failed to retain game D3D11 context");
    return false;
  }

  if (!d3d::InstallGamePresentHook()) {
    log::Write("Present hook installation failed");
  }
  d3d::Present(true);
  return true;
}

bool ResizeGraphics(int width, int height, const char* resize_log_name) noexcept {
  if (width <= 0 || height <= 0 || g_state.graphics_owner == nullptr ||
      g_state.game_renderer == nullptr || d3d::SwapChain() == nullptr ||
      d3d::DeviceContext() == nullptr) {
    return false;
  }

  d3d::PrepareForResize();

  using ReleaseTargetsFn = void(__fastcall*)(void* renderer);
  const auto release_targets = reinterpret_cast<ReleaseTargetsFn>(
      g_state.context.release_render_targets);
  if (release_targets == nullptr) {
    return false;
  }
  release_targets(g_state.game_renderer);

  void** const back_buffer = g_state.context.back_buffer_slot;
  if (back_buffer != nullptr && *back_buffer != nullptr) {
    using ReleaseFn = unsigned long(SHIM_COM*)(void*);
    const auto release = reinterpret_cast<ReleaseFn>(
        object_access::VtableEntry(*back_buffer, 0x08));
    if (release != nullptr) {
      release(*back_buffer);
    }
    *back_buffer = nullptr;
  }

  using ResizeBuffersFn = HRESULT(SHIM_COM*)(void* swap_chain,
                                              UINT buffer_count, UINT width,
                                              UINT height, DWORD format,
                                              UINT flags);
  const auto resize_buffers = reinterpret_cast<ResizeBuffersFn>(
      object_access::VtableEntry(d3d::SwapChain(), 0x34));
  if (resize_buffers == nullptr) {
    return false;
  }

  const HRESULT resized = resize_buffers(
      d3d::SwapChain(), 0, static_cast<UINT>(width),
      static_cast<UINT>(height), 0, 0);
  if (resize_log_name != nullptr) {
    log::WriteHResult(resize_log_name, resized);
  }
  if (FAILED(resized)) {
    return false;
  }

  if (!InitRenderTargets(g_state.graphics_owner, g_state.game_renderer, width,
                         height)) {
    return false;
  }
  return SUCCEEDED(d3d::Present(true));
}

void RunModernFrame(const FramePlan& plan) noexcept {
  RuntimeState& state = State();
  void* const frame = Context().app_main_frame;
  if (!state.frame_bindings_valid || frame == nullptr ||
      state.app_main == nullptr) {
    if (plan.not_ready != nullptr) {
      plan.not_ready();
    }
    return;
  }

  auto* const app = static_cast<unsigned char*>(state.app_main);
  if (*(app + 0x10) == 0 || state.game_renderer == nullptr ||
      d3d::DeviceContext() == nullptr || d3d::RenderTargetView() == nullptr ||
      d3d::SwapChain() == nullptr) {
    if (plan.not_ready != nullptr) {
      plan.not_ready();
    }
    return;
  }

  if (plan.validate_app_frame != nullptr &&
      !plan.validate_app_frame(state.app_main)) {
    return;
  }

  int width = 0;
  int height = 0;
  app_window::ClientSize(&width, &height);
  d3d::BeginFrame(width, height, DepthTarget());

  if (plan.before_present_capture != nullptr) {
    plan.before_present_capture();
  }

  const unsigned long present_generation_before = d3d::PresentGeneration();
  if (plan.before_game_frame != nullptr) {
    plan.before_game_frame(state.app_main);
  }

  if (!state.game_is_rendering && plan.first_frame_log != nullptr) {
    log::Write(plan.first_frame_log);
  }

  using FrameFn = void(__fastcall*)(void*);
  reinterpret_cast<FrameFn>(frame)(state.app_main);
  const bool game_presented =
      d3d::PresentGeneration() != present_generation_before;

  if (plan.after_game_frame != nullptr) {
    plan.after_game_frame();
  }

  if (!state.game_is_rendering) {
    state.game_is_rendering = true;
    log::Write("first AppMain update/render returned");
  }
  ++state.frame_count;

  if (!game_presented && plan.fallback_present != nullptr) {
    plan.fallback_present();
  }
}

}  // namespace shim::versions::modern
