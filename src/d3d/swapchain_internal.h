#pragma once

#include "shim/swapchain.h"

namespace shim::d3d::detail {

using ReleaseFn = ULONG(SHIM_COM*)(void*);
using QueryInterfaceFn = HRESULT(SHIM_COM*)(void*, const GUID*, void**);
using AddRefFn = ULONG(SHIM_COM*)(void*);
using PresentFn = HRESULT(SHIM_COM*)(void*, UINT, UINT);
using SetRenderTargetsFn =
    void(SHIM_COM*)(void*, UINT, void* const*, void*);
using SetViewportsFn = void(SHIM_COM*)(void*, UINT, const float*);
using ClearRenderTargetViewFn = void(SHIM_COM*)(void*, void*, const float*);

constexpr size_t kSlotQueryInterface = 0;
constexpr size_t kSlotAddRef = 1;
constexpr size_t kSlotRelease = 2;
constexpr size_t kDxgiDeviceGetAdapter = 7;
constexpr size_t kDxgiObjectGetParent = 6;
constexpr size_t kDxgiFactoryCreateSwapChain = 10;
constexpr size_t kSwapChainPresent = 8;
constexpr size_t kSwapChainGetBuffer = 9;
constexpr size_t kSwapChainResizeBuffers = 13;
constexpr size_t kDeviceCreateRenderTargetView = 9;
constexpr size_t kContextOMSetRenderTargets = 33;
constexpr size_t kContextRSSetViewports = 44;
constexpr size_t kContextRSSetScissorRects = 45;
constexpr size_t kContextClearRenderTargetView = 50;
constexpr size_t kContextFlush = 111;

inline constexpr GUID kIidDxgiDevice = {
    0x54EC77FA, 0x1377, 0x44E6,
    {0x8C, 0x32, 0x88, 0xFD, 0x5F, 0x44, 0xC8, 0x4C}};
inline constexpr GUID kIidDxgiFactory = {
    0x7B7166EC, 0x21C7, 0x44AE,
    {0xB2, 0x1A, 0xC9, 0xAE, 0x32, 0x1A, 0xE3, 0x69}};
inline constexpr GUID kIidTexture2D = {
    0x6F15AAF2, 0xD208, 0x4E89,
    {0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C}};

struct SwapChainDesc {
  DWORD width;
  DWORD height;
  DWORD refresh_numerator;
  DWORD refresh_denominator;
  DWORD format;
  DWORD scanline_ordering;
  DWORD scaling;
  DWORD sample_count;
  DWORD sample_quality;
  DWORD buffer_usage;
  DWORD buffer_count;
  HWND output_window;
  BOOL windowed;
  DWORD swap_effect;
  DWORD flags;
};
static_assert(sizeof(SwapChainDesc) == 60,
              "DXGI_SWAP_CHAIN_DESC must stay 60 bytes on Win32");

using CreateSwapChainFn =
    HRESULT(SHIM_COM*)(void*, void*, SwapChainDesc*, void**);
using PlatformCreateSwapChainFn = bool (*)(CreateSwapChainFn create_swap_chain,
                                           void* factory,
                                           void* device,
                                           HWND window,
                                           void** swap_chain) noexcept;

struct SwapChainPlatformBackend {
  UINT default_sync_interval;
  PlatformCreateSwapChainFn create_swap_chain;
};

struct SwapChainState {
  const game::D3DLayout* layout = nullptr;
  void* swap_chain = nullptr;
  void* render_target_view = nullptr;
  void* context = nullptr;
  bool context_retained = false;
  void* device = nullptr;
  float fallback_color = 0.0f;
  unsigned int present_count = 0;
  volatile LONG present_generation = 0;
  unsigned int game_present_calls = 0;
  unsigned int game_present_would_block = 0;
  PresentFn host_present = nullptr;
  PresentFn original_game_present = nullptr;
  SetRenderTargetsFn set_targets = nullptr;
  SetViewportsFn set_viewports = nullptr;
  ClearRenderTargetViewFn clear_render_target = nullptr;
  void* original_scissor_rects = nullptr;
  HWND scissor_window = nullptr;
  unsigned int scissor_fix_count = 0;
  const SwapChainPlatformBackend* backend = nullptr;
};

SwapChainState& State() noexcept;

inline void* const* VtableOf(void* object) noexcept {
  if (object == nullptr || ::IsBadReadPtr(object, sizeof(void*))) {
    return nullptr;
  }
  return *static_cast<void* const* const*>(object);
}

template <typename Fn>
Fn Slot(void* object, size_t index) noexcept {
  void* const* vtable = VtableOf(object);
  if (vtable == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<Fn>(vtable[index]);
}

inline void SafeRelease(void*& object) noexcept {
  if (object == nullptr) {
    return;
  }
  if (const auto release = Slot<ReleaseFn>(object, kSlotRelease)) {
    release(object);
  }
  object = nullptr;
}

SwapChainDesc DescribeSwapChain(HWND window, DWORD format) noexcept;
const SwapChainPlatformBackend& Win7Backend() noexcept;
const SwapChainPlatformBackend& ModernBackend() noexcept;
const SwapChainPlatformBackend& SelectPlatformBackend() noexcept;

bool CreateForWindowInternal(void* device, HWND window) noexcept;
bool CreateRenderTargetViewFromBackBuffer() noexcept;
void PrepareForResizeInternal() noexcept;
bool ResizePreparedInternal(int width, int height) noexcept;

}  // namespace shim::d3d::detail
