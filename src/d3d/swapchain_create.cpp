#include "swapchain_internal.h"

#include "shim/log.h"

namespace shim::d3d::detail {

SwapChainDesc DescribeSwapChain(HWND window, DWORD format) noexcept {
  RECT client = {};
  ::GetClientRect(window, &client);

  SwapChainDesc desc = {};
  desc.width = static_cast<DWORD>(client.right - client.left);
  desc.height = static_cast<DWORD>(client.bottom - client.top);
  desc.format = format;
  desc.sample_count = 1;
  desc.buffer_usage = 0x20;
  desc.output_window = window;
  desc.windowed = TRUE;
  desc.swap_effect = 0;
  return desc;
}

bool CreateForWindowInternal(void* device, HWND window) noexcept {
  if (device == nullptr || window == nullptr) {
    return false;
  }

  SwapChainState& state = State();
  state.device = device;
  state.backend = &SelectPlatformBackend();

  const auto query = Slot<QueryInterfaceFn>(device, kSlotQueryInterface);
  if (query == nullptr) {
    return false;
  }

  void* dxgi_device = nullptr;
  HRESULT result = query(device, &kIidDxgiDevice, &dxgi_device);
  log::WriteHResult("ID3D11Device::QueryInterface(IDXGIDevice)", result);
  if (FAILED(result) || dxgi_device == nullptr) {
    return false;
  }

  void* adapter = nullptr;
  void* factory = nullptr;
  bool created = false;

  do {
    const auto get_adapter =
        Slot<HRESULT(SHIM_COM*)(void*, void**)>(dxgi_device,
                                                kDxgiDeviceGetAdapter);
    if (get_adapter == nullptr) {
      break;
    }
    result = get_adapter(dxgi_device, &adapter);
    log::WriteHResult("IDXGIDevice::GetAdapter", result);
    if (FAILED(result) || adapter == nullptr) {
      break;
    }

    const auto get_parent =
        Slot<HRESULT(SHIM_COM*)(void*, const GUID*, void**)>(
            adapter, kDxgiObjectGetParent);
    if (get_parent == nullptr) {
      break;
    }
    result = get_parent(adapter, &kIidDxgiFactory, &factory);
    log::WriteHResult("IDXGIAdapter::GetParent(IDXGIFactory)", result);
    if (FAILED(result) || factory == nullptr) {
      break;
    }

    const auto create_swap_chain =
        Slot<CreateSwapChainFn>(factory, kDxgiFactoryCreateSwapChain);
    if (create_swap_chain == nullptr || state.backend == nullptr ||
        state.backend->create_swap_chain == nullptr) {
      break;
    }

    log::Write("creating HWND swap chain for game device");
    created = state.backend->create_swap_chain(
        create_swap_chain, factory, device, window, &state.swap_chain);
  } while (false);

  SafeRelease(factory);
  SafeRelease(adapter);
  SafeRelease(dxgi_device);

  if (!created) {
    SafeRelease(state.swap_chain);
    state.host_present = nullptr;
    return false;
  }

  state.host_present = Slot<PresentFn>(state.swap_chain, kSwapChainPresent);
  if (state.host_present == nullptr) {
    SafeRelease(state.swap_chain);
    return false;
  }

  return true;
}

}  // namespace shim::d3d::detail
