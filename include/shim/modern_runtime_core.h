#pragma once

#include "shim/common.h"

namespace shim::game {
struct RuntimeLayout;
struct D3DLayout;
struct InputLayout;
}

namespace shim::versions::modern {

struct RuntimeContext {
  unsigned int resource_owner_size;
  void* alloc_resource_owner;
  void* construct_resource_owner;
  void** resource_owner_slot;
  void* init_d3d_device;
  void* make_device_resources;
  void** device_resources_slot;
  void* make_app_main;
  void* init_render_targets;
  void* release_render_targets;
  void* app_main_frame;
  void* platform_frame_entry;
  void* app_main_resize;
  DWORD owner_to_renderer;
  DWORD owner_to_device;
  DWORD renderer_to_context;
  DWORD renderer_to_depth_target;
  DWORD renderer_to_back_buffer;
  DWORD input_to_event_sink;
  void** depth_target_slot;
  void** back_buffer_slot;
};

struct RuntimeState {
  RuntimeContext context;
  const game::RuntimeLayout* runtime_layout;
  const game::D3DLayout* d3d_layout;
  const game::InputLayout* input_layout;
  void* app_main;
  void* graphics_owner;
  void* game_renderer;
  bool frame_bindings_valid;
  bool game_is_rendering;
  unsigned int frame_count;
};

using FrameAction = void (*)() noexcept;
using FrameAppPredicate = bool (*)(void* app_main) noexcept;
using FrameAppAction = void (*)(void* app_main) noexcept;

struct FramePlan {
  const char* first_frame_log;
  FrameAction not_ready;
  FrameAppPredicate validate_app_frame;
  FrameAction before_present_capture;
  FrameAppAction before_game_frame;
  FrameAction after_game_frame;
  FrameAction fallback_present;
};

RuntimeState& State() noexcept;
const RuntimeContext& Context() noexcept;
void ResetState() noexcept;
void BindRuntimeLayouts(const game::RuntimeLayout* runtime_layout,
                        const game::D3DLayout* d3d_layout,
                        const game::InputLayout* input_layout) noexcept;
bool InitializeRuntimeContext() noexcept;
bool BindRendererRuntimeContext(void* renderer) noexcept;

void* GameRenderer(void* owner) noexcept;
void* GameDevice(void* owner) noexcept;
void* GameContext(void* renderer) noexcept;
void* DepthTarget() noexcept;

void* CreateGraphicsOwner(const char* log_name) noexcept;
void* CreateDeviceResources(const char* holder_log_name,
                            const char* pointer_log_name) noexcept;

bool InitRenderTargets(void* owner, void* renderer, int width,
                       int height) noexcept;
bool InitializeGraphics(void* owner) noexcept;
bool ResizeGraphics(int width, int height, const char* resize_log_name) noexcept;

void RunModernFrame(const FramePlan& plan) noexcept;

}  // namespace shim::versions::modern
