#include "swapchain_internal.h"

#include "shim/app_window.h"
#include "shim/game_profile.h"
#include "shim/log.h"

namespace shim::d3d {
namespace {

struct ScissorRect {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};
static_assert(sizeof(ScissorRect) == 16,
              "D3D11_RECT-compatible scissor must stay 16 bytes");

using ScissorRectsFn = void(SHIM_COM*)(void*, UINT, const ScissorRect*);

bool NeedsScissorAdjustment(const ScissorRect& rect, int width,
                            int height) noexcept {
  return rect.left < 0 || rect.top < 0 || rect.right > width ||
         rect.bottom > height;
}

void AdjustScissorRect(ScissorRect& rect, int width, int height) noexcept {
  if (rect.top < 0) {
    if (rect.bottom <= 0) {
      rect.top += height;
      rect.bottom += height;
    }
    if (rect.top < 0) {
      rect.top = 0;
    }
  }
  if (rect.left < 0) {
    rect.left = 0;
  }
  if (rect.right > width) {
    rect.right = width;
  }
  if (rect.bottom > height) {
    rect.bottom = height;
  }
}

void SHIM_COM GameScissorRectsHook(void* context, UINT count,
                                   const ScissorRect* rects) noexcept {
  detail::SwapChainState& state = detail::State();
  const auto original =
      reinterpret_cast<ScissorRectsFn>(state.original_scissor_rects);
  if (original == nullptr) {
    return;
  }

  if (rects == nullptr || count == 0 || count > 16) {
    original(context, count, rects);
    return;
  }

  int width = 0;
  int height = 0;
  app_window::ClientSize(&width, &height);
  if (width <= 0 || height <= 0) {
    original(context, count, rects);
    return;
  }

  bool changed = false;
  for (UINT i = 0; i < count; ++i) {
    if (NeedsScissorAdjustment(rects[i], width, height)) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    original(context, count, rects);
    return;
  }

  ScissorRect adjusted[16] = {};
  memcpy(adjusted, rects, count * sizeof(ScissorRect));
  for (UINT i = 0; i < count; ++i) {
    AdjustScissorRect(adjusted[i], width, height);
  }

  if (state.scissor_fix_count < 8) {
    ++state.scissor_fix_count;
    log::Write("D3D11 HWND scissor rect adjusted");
  }
  original(context, count, adjusted);
}

}  // namespace

void SetDeviceContext(void* context) noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == context) {
    return;
  }

  if (state.context_retained) {
    detail::SafeRelease(state.context);
    state.context_retained = false;
  } else {
    state.context = nullptr;
  }

  state.context = context;
  state.set_viewports = nullptr;
  state.set_targets = nullptr;
  state.clear_render_target = nullptr;
  if (context != nullptr) {
    state.set_viewports = detail::Slot<detail::SetViewportsFn>(
        context, detail::kContextRSSetViewports);
    state.set_targets = detail::Slot<detail::SetRenderTargetsFn>(
        context, detail::kContextOMSetRenderTargets);
    state.clear_render_target = detail::Slot<detail::ClearRenderTargetViewFn>(
        context, detail::kContextClearRenderTargetView);
  }
}

bool RetainDeviceContext() noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr) {
    return false;
  }
  if (state.context_retained) {
    return true;
  }

  const auto add_ref =
      detail::Slot<detail::AddRefFn>(state.context, detail::kSlotAddRef);
  if (add_ref == nullptr) {
    return false;
  }
  add_ref(state.context);
  state.context_retained = true;
  log::Write("retained game D3D11 immediate context");
  return true;
}

bool InstallModernContextHooks(HWND window) noexcept {
  if (game::HasCapability(game::VersionCapability::kRenderer0132Layout)) {
    return true;
  }

  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr || window == nullptr) {
    return false;
  }

  void* const* vtable = detail::VtableOf(state.context);
  if (vtable == nullptr) {
    return false;
  }

  auto** slot =
      const_cast<void**>(&vtable[detail::kContextRSSetScissorRects]);
  if (*slot == reinterpret_cast<void*>(&GameScissorRectsHook)) {
    state.scissor_window = window;
    return true;
  }

  DWORD old_protect = 0;
  if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
    return false;
  }

  state.original_scissor_rects = *slot;
  *slot = reinterpret_cast<void*>(&GameScissorRectsHook);

  DWORD ignored = 0;
  ::VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
  ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));

  state.scissor_window = window;
  state.scissor_fix_count = 0;
  log::Write("installed D3D11 HWND scissor fix");
  return state.original_scissor_rects != nullptr;
}

void* DeviceContext() noexcept {
  return detail::State().context;
}

void BeginFrame(int width, int height, void* depth_target) noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr || state.render_target_view == nullptr ||
      state.set_viewports == nullptr || state.set_targets == nullptr) {
    return;
  }

  const float viewport[6] = {
      0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
      0.0f, 1.0f};
  state.set_viewports(state.context, 1, viewport);
  state.set_targets(state.context, 1, &state.render_target_view, depth_target);
}

}  // namespace shim::d3d
