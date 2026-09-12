#include "swapchain_internal.h"

#include "shim/log.h"

namespace shim::d3d::detail {
namespace {

bool CreateModernSwapChain(CreateSwapChainFn create_swap_chain, void* factory,
                           void* device, HWND window,
                           void** swap_chain) noexcept {
  if (create_swap_chain == nullptr || swap_chain == nullptr) {
    return false;
  }

  SwapChainDesc desc = DescribeSwapChain(window, kFormatBgra);
  desc.buffer_count = 2;
  const HRESULT result =
      create_swap_chain(factory, device, &desc, swap_chain);
  log::WriteHResult("IDXGIFactory::CreateSwapChain(HWND BGRA)", result);
  return SUCCEEDED(result) && *swap_chain != nullptr;
}

}  // namespace

const SwapChainPlatformBackend& ModernBackend() noexcept {
  static const SwapChainPlatformBackend backend = [] {
    SwapChainPlatformBackend value = {};
    value.default_sync_interval = 1;
    value.create_swap_chain = &CreateModernSwapChain;
    return value;
  }();
  return backend;
}

}  // namespace shim::d3d::detail
