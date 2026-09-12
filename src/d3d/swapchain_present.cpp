#include "swapchain_internal.h"

#include "shim/game_profile.h"
#include "shim/log.h"

namespace shim::d3d {
namespace {

UINT EffectiveSyncInterval(UINT sync_interval) noexcept {
  return game::IsWindows7() ? 0u : sync_interval;
}

HRESULT SHIM_COM GamePresentHook(void* swap_chain, UINT sync_interval,
                                 UINT flags) noexcept {
  detail::SwapChainState& state = detail::State();
  ++state.game_present_calls;
  if (state.original_game_present == nullptr) {
    return E_FAIL;
  }

  const HRESULT result =
      state.original_game_present(swap_chain, EffectiveSyncInterval(sync_interval),
                                  flags);
  if (SUCCEEDED(result)) {
    ::InterlockedIncrement(&state.present_generation);
  }
  return result;
}

HRESULT PresentWithSyncInterval(UINT sync_interval) noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.swap_chain == nullptr || state.host_present == nullptr) {
    return E_POINTER;
  }

  const HRESULT result =
      state.host_present(state.swap_chain, EffectiveSyncInterval(sync_interval),
                         0u);
  ++state.present_count;
  if (state.present_count <= 10 || state.present_count == 60 ||
      FAILED(result)) {
    log::Writef("host Present %u: HRESULT 0x%08lX", state.present_count,
                result);
  }
  return result;
}

void PresentLoadingColor(const float color[4], bool platform_default) noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr || state.render_target_view == nullptr ||
      state.swap_chain == nullptr || state.set_targets == nullptr ||
      state.clear_render_target == nullptr) {
    return;
  }

  state.set_targets(state.context, 1, &state.render_target_view, nullptr);
  state.clear_render_target(state.context, state.render_target_view, color);
  if (platform_default) {
    PresentPlatformDefault();
  } else {
    Present(false);
  }
}

}  // namespace

bool InstallGamePresentHook() noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.swap_chain == nullptr) {
    return false;
  }

  void* const* vtable = detail::VtableOf(state.swap_chain);
  if (vtable == nullptr) {
    return false;
  }

  auto** slot = const_cast<void**>(&vtable[detail::kSwapChainPresent]);
  if (*slot == reinterpret_cast<void*>(&GamePresentHook)) {
    return true;
  }

  DWORD old_protect = 0;
  if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
    return false;
  }

  state.original_game_present = state.host_present != nullptr
                                    ? state.host_present
                                    : reinterpret_cast<detail::PresentFn>(*slot);
  state.host_present = state.original_game_present;
  *slot = reinterpret_cast<void*>(&GamePresentHook);

  DWORD ignored = 0;
  ::VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
  ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));
  return state.original_game_present != nullptr;
}

HRESULT Present(bool allow_tearing) noexcept {
  return PresentWithSyncInterval(allow_tearing ? 0u : 1u);
}

HRESULT PresentPlatformDefault() noexcept {
  detail::SwapChainState& state = detail::State();
  const detail::SwapChainPlatformBackend* backend = state.backend;
  if (backend == nullptr) {
    backend = &detail::SelectPlatformBackend();
  }
  return PresentWithSyncInterval(backend->default_sync_interval);
}

unsigned long PresentGeneration() noexcept {
  return static_cast<unsigned long>(::InterlockedCompareExchange(
      &detail::State().present_generation, 0, 0));
}

void PresentV0132LoadingFrame() noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr || state.render_target_view == nullptr ||
      state.swap_chain == nullptr) {
    return;
  }

  float next = state.fallback_color + 0.0025f;
  if (next > 0.06f) {
    next = 0.0f;
  }
  state.fallback_color = next;
  const float color[4] = {0.055f, 0.085f, next + 0.12f, 1.0f};
  PresentLoadingColor(color, true);
}

void Present01510LoadingFrame() noexcept {
  detail::SwapChainState& state = detail::State();
  if (state.context == nullptr || state.render_target_view == nullptr ||
      state.swap_chain == nullptr) {
    return;
  }

  float next = state.fallback_color + 0.0025f;
  if (next > 0.06f) {
    next = 0.0f;
  }
  state.fallback_color = next;
  const float color[4] = {0.055f, 0.085f, 0.12f + next, 1.0f};
  PresentLoadingColor(color, false);
}

}  // namespace shim::d3d
