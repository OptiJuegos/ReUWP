#include "swapchain_internal.h"

#include "shim/log.h"

namespace shim::d3d::detail {

bool CreateRenderTargetViewFromBackBuffer() noexcept {
  SwapChainState& state = State();
  const auto get_buffer =
      Slot<HRESULT(SHIM_COM*)(void*, UINT, const GUID*, void**)>(
          state.swap_chain, kSwapChainGetBuffer);
  const auto create_view =
      Slot<HRESULT(SHIM_COM*)(void*, void*, const void*, void**)>(
          state.device, kDeviceCreateRenderTargetView);
  if (get_buffer == nullptr || create_view == nullptr) {
    return false;
  }

  void* back_buffer = nullptr;
  HRESULT result =
      get_buffer(state.swap_chain, 0, &kIidTexture2D, &back_buffer);
  log::WriteHResult("IDXGISwapChain::GetBuffer", result);
  if (FAILED(result) || back_buffer == nullptr) {
    return false;
  }

  result = create_view(state.device, back_buffer, nullptr,
                       &state.render_target_view);
  log::WriteHResult("ID3D11Device::CreateRenderTargetView", result);
  SafeRelease(back_buffer);
  return SUCCEEDED(result) && state.render_target_view != nullptr;
}

void PrepareForResizeInternal() noexcept {
  SwapChainState& state = State();
  if (state.context != nullptr) {
    const auto set_targets =
        Slot<void(SHIM_COM*)(void*, UINT, void* const*, void*)>(
            state.context, kContextOMSetRenderTargets);
    if (set_targets != nullptr) {
      set_targets(state.context, 0, nullptr, nullptr);
    }

    const auto flush =
        Slot<void(SHIM_COM*)(void*)>(state.context, kContextFlush);
    if (flush != nullptr) {
      flush(state.context);
    }
  }

  SafeRelease(state.render_target_view);
}

bool ResizePreparedInternal(int width, int height) noexcept {
  SwapChainState& state = State();
  if (state.swap_chain == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  const auto resize_buffers =
      Slot<HRESULT(SHIM_COM*)(void*, UINT, UINT, UINT, UINT, UINT)>(
          state.swap_chain, kSwapChainResizeBuffers);
  if (resize_buffers == nullptr) {
    return false;
  }

  const HRESULT result =
      resize_buffers(state.swap_chain, 0, static_cast<UINT>(width),
                     static_cast<UINT>(height), 0, 0);
  log::WriteHResult("IDXGISwapChain::ResizeBuffers", result);
  if (FAILED(result)) {
    return false;
  }

  return CreateRenderTargetViewFromBackBuffer();
}

}  // namespace shim::d3d::detail

namespace shim::d3d {

void PrepareForResize() noexcept {
  detail::PrepareForResizeInternal();
}

bool Resize(int width, int height) noexcept {
  detail::PrepareForResizeInternal();
  return detail::ResizePreparedInternal(width, height);
}

}  // namespace shim::d3d
