#include "shim/swapchain.h"

#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "swapchain_internal.h"

namespace shim::d3d::detail {
namespace {
SwapChainState g_state = {};
}  // namespace

SwapChainState& State() noexcept {
  return g_state;
}

}  // namespace shim::d3d::detail

namespace shim::d3d {

void BindLayout(const game::D3DLayout* layout) noexcept {
  detail::State().layout = layout;
}

bool CreateForWindow(void* device, HWND window) noexcept {
  return detail::CreateForWindowInternal(device, window);
}

void* SwapChain() noexcept {
  return detail::State().swap_chain;
}

void* RenderTargetView() noexcept {
  return detail::State().render_target_view;
}

bool InjectIntoRenderer(void* renderer) noexcept {
  detail::SwapChainState& state = detail::State();
  if (renderer == nullptr || state.swap_chain == nullptr) {
    return false;
  }

  DWORD swap_chain_offset = 0;
  DWORD render_target_offset = 0;
  if (game::HasCapability(game::VersionCapability::kRenderer0132Layout)) {
    const game::Version0132Layout& layout0132 = game::Layout0132();
    swap_chain_offset = layout0132.renderer_swap_chain_offset;
    render_target_offset = layout0132.renderer_rtv_offset;
  } else {
    if (state.layout == nullptr) {
      return false;
    }
    swap_chain_offset = state.layout->renderer_to_swap_chain;
    render_target_offset = state.layout->renderer_to_render_target;
  }

  if (swap_chain_offset == 0) {
    return false;
  }

  auto** slot = reinterpret_cast<void**>(
      static_cast<unsigned char*>(renderer) + swap_chain_offset);

  if (*slot != state.swap_chain) {
    detail::SafeRelease(*slot);
    if (const auto add_ref = detail::Slot<detail::AddRefFn>(
            state.swap_chain, detail::kSlotAddRef)) {
      add_ref(state.swap_chain);
    }
    *slot = state.swap_chain;
    log::Writef("HWND swap chain stored at renderer+0x%X", swap_chain_offset);
  }

  if (render_target_offset != 0) {
    auto** render_target_slot = reinterpret_cast<void**>(
        static_cast<unsigned char*>(renderer) + render_target_offset);
    void* game_view = *render_target_slot;
    log::WritePointer("game render target", game_view);
    if (game_view != nullptr) {
      detail::SafeRelease(state.render_target_view);
      if (const auto add_ref = detail::Slot<detail::AddRefFn>(
              game_view, detail::kSlotAddRef)) {
        add_ref(game_view);
      }
      state.render_target_view = game_view;
    } else {
      if (state.render_target_view == nullptr) {
        log::Write(
            "game render target creation returned null; trying direct RTV fallback");
        detail::CreateRenderTargetViewFromBackBuffer();
      }

      if (state.render_target_view == nullptr) {
        detail::SafeRelease(state.swap_chain);
        return false;
      }

      if (const auto add_ref = detail::Slot<detail::AddRefFn>(
              state.render_target_view, detail::kSlotAddRef)) {
        add_ref(state.render_target_view);
        *render_target_slot = state.render_target_view;
        log::Writef("direct RTV fallback stored at renderer+0x%X",
                    render_target_offset);
      }
    }
  }
  return state.render_target_view != nullptr;
}

}  // namespace shim::d3d
