#include "swapchain_internal.h"

#include "shim/game_profile.h"
#include "shim/log.h"

namespace shim::d3d::detail {
namespace {

bool TryCreateWin7SwapChain(CreateSwapChainFn create_swap_chain, void* factory,
                            void* device, HWND window, DWORD format,
                            DWORD buffer_count, const char* log_name,
                            void** swap_chain) noexcept {
  SwapChainDesc desc = DescribeSwapChain(window, format);
  desc.buffer_count = buffer_count;
  const HRESULT result =
      create_swap_chain(factory, device, &desc, swap_chain);
  log::WriteHResult(log_name, result);
  return SUCCEEDED(result) && *swap_chain != nullptr;
}

bool CreateWin7SwapChain(CreateSwapChainFn create_swap_chain, void* factory,
                         void* device, HWND window,
                         void** swap_chain) noexcept {
  if (create_swap_chain == nullptr || swap_chain == nullptr) {
    return false;
  }

  *swap_chain = nullptr;
  if (TryCreateWin7SwapChain(create_swap_chain, factory, device, window,
                             kFormatBgra, 1,
                             "IDXGIFactory::CreateSwapChain(HWND BGRA x1)",
                             swap_chain)) {
    return true;
  }

  *swap_chain = nullptr;
  return TryCreateWin7SwapChain(
      create_swap_chain, factory, device, window, kFormatRgba, 1,
      "IDXGIFactory::CreateSwapChain(HWND RGBA x1 fallback)", swap_chain);
}

}  // namespace

const SwapChainPlatformBackend& Win7Backend() noexcept {
  static const SwapChainPlatformBackend backend = [] {
    SwapChainPlatformBackend value = {};
    value.default_sync_interval = 0;
    value.create_swap_chain = &CreateWin7SwapChain;
    return value;
  }();
  return backend;
}

const SwapChainPlatformBackend& SelectPlatformBackend() noexcept {
  return game::IsWindows7() ? Win7Backend() : ModernBackend();
}

}  // namespace shim::d3d::detail
