#include "compat_internal.h"

#include "shim/compat_hooks.h"
#include "shim/config.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/patch.h"

namespace shim::hooks {
namespace {

using CreateDeviceFn = HRESULT(SHIM_COM*)(void*, UINT, HMODULE, UINT,
                                          const UINT*, UINT, UINT, void**,
                                          UINT*, void**);
CreateDeviceFn g_original_create_device = nullptr;

const char* DriverTypeName(UINT driver_type) noexcept {
  switch (driver_type) {
    case kDriverTypeHardware:
      return "HARDWARE";
    case kDriverTypeWarp:
      return "WARP";
    default:
      return "OTHER";
  }
}

void* g_retained_d3d_device = nullptr;
void* g_retained_d3d_context = nullptr;

using ComRefFn = ULONG(SHIM_COM*)(void* object);

ULONG ComAddRef(void* object) noexcept {
  if (object == nullptr || ::IsBadReadPtr(object, sizeof(void*))) {
    return 0;
  }
  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable == nullptr || ::IsBadReadPtr(vtable + 1, sizeof(void*))) {
    return 0;
  }
  return reinterpret_cast<ComRefFn>(vtable[1])(object);
}

ULONG ComRelease(void* object) noexcept {
  if (object == nullptr || ::IsBadReadPtr(object, sizeof(void*))) {
    return 0;
  }
  void** vtable = *reinterpret_cast<void***>(object);
  if (vtable == nullptr || ::IsBadReadPtr(vtable + 2, sizeof(void*))) {
    return 0;
  }
  return reinterpret_cast<ComRefFn>(vtable[2])(object);
}

void ReleaseRetainedD3DDevicePair() noexcept {
  if (g_retained_d3d_device != nullptr) {
    ComRelease(g_retained_d3d_device);
    g_retained_d3d_device = nullptr;
  }
  if (g_retained_d3d_context != nullptr) {
    ComRelease(g_retained_d3d_context);
    g_retained_d3d_context = nullptr;
  }
}

void RetainD3DDevicePair(void* device, void* context) noexcept {
  ReleaseRetainedD3DDevicePair();
  if (device == nullptr || context == nullptr) {
    return;
  }

  g_retained_d3d_device = device;
  g_retained_d3d_context = context;
  ComAddRef(g_retained_d3d_device);
  ComAddRef(g_retained_d3d_context);
}

#if REUWP_ENABLE_WARP_FALLBACK
void ResetCreateDeviceOutputs(void** out_device, UINT* out_feature_level,
                              void** out_context) noexcept {
  if (out_device != nullptr) {
    *out_device = nullptr;
  }
  if (out_context != nullptr) {
    *out_context = nullptr;
  }
  if (out_feature_level != nullptr) {
    *out_feature_level = 0;
  }
}
#endif

HRESULT SHIM_COM CreateCompatibleD3D11Device(
    void* adapter, UINT driver_type, HMODULE software, UINT flags,
    const UINT* feature_levels, UINT feature_level_count, UINT sdk_version,
    void** out_device, UINT* out_feature_level, void** out_context) noexcept {
  flags |= kCreateDeviceBgraSupport;

  UINT filtered[16] = {};
#if REUWP_WIN7_FILTER_FEATURE_LEVEL_11_1
  if (game::IsWindows7() && feature_levels != nullptr &&
      feature_level_count != 0) {
    UINT kept = 0;
    for (UINT i = 0; i < feature_level_count && kept < CountOf(filtered); ++i) {
      if (feature_levels[i] != kFeatureLevel11_1) {
        filtered[kept++] = feature_levels[i];
      }
    }
    if (kept != 0) {
      feature_levels = filtered;
      feature_level_count = kept;
    }
  }
#endif

  if (g_original_create_device == nullptr) {
    return E_NOTIMPL;
  }

  UINT selected_driver_type = driver_type;
  HRESULT result = g_original_create_device(
      adapter, driver_type, software, flags, feature_levels,
      feature_level_count, sdk_version, out_device, out_feature_level,
      out_context);

  log::Writef(
      "D3D11CreateDevice compat: driver=%u flags=0x%X levels=%u result=0x%08lX",
      driver_type, flags, feature_level_count, result);

#if REUWP_ENABLE_WARP_FALLBACK
  if (FAILED(result) && driver_type == kDriverTypeHardware) {
    ResetCreateDeviceOutputs(out_device, out_feature_level, out_context);
    selected_driver_type = kDriverTypeWarp;
    result = g_original_create_device(
        nullptr, kDriverTypeWarp, nullptr, flags, feature_levels,
        feature_level_count, sdk_version, out_device, out_feature_level,
        out_context);
    log::WriteHResult("D3D11CreateDevice WARP fallback", result);
  }
#endif

  if (SUCCEEDED(result)) {
    const UINT selected_feature_level =
        out_feature_level != nullptr ? *out_feature_level : 0;
    log::Writef("D3D11 device selected: %s feature_level=0x%04X",
                DriverTypeName(selected_driver_type), selected_feature_level);
  }

#if REUWP_WIN7_RETAIN_BASE_D3D_INTERFACES
  if (game::HasCapability(game::VersionCapability::kRetainD3DDevicePair) &&
      SUCCEEDED(result) && out_device != nullptr && *out_device != nullptr &&
      out_context != nullptr && *out_context != nullptr) {
    RetainD3DDevicePair(*out_device, *out_context);
  }
#endif

  return result;
}

constexpr GUID kIidD3D11Device = {
    0xDB6F6DDB, 0xAC77, 0x4E88,
    {0x82, 0x53, 0x81, 0x9D, 0xF9, 0xBB, 0xF1, 0x40}};
constexpr GUID kIidD3D11DeviceContext = {
    0xC0BFA96C, 0xE089, 0x44FB,
    {0x8E, 0xAF, 0x26, 0xF8, 0x79, 0x61, 0x90, 0xDA}};
constexpr GUID kIidDxgiDevice = {
    0x54EC77FA, 0x1377, 0x44E6,
    {0x8C, 0x32, 0x88, 0xFD, 0x5F, 0x44, 0xC8, 0x4C}};
constexpr GUID kIidDxgiDevice3 = {
    0x6007896C, 0x3244, 0x4AFD,
    {0xBF, 0x18, 0xA6, 0xD3, 0xBE, 0xDA, 0x50, 0x23}};

bool WriteIid(DWORD rva, const GUID& replacement) noexcept {
  void* address = game::Resolve(rva);
  return address != nullptr && WriteCode(address, &replacement, sizeof(GUID));
}

void InstallWindows7IidPatches(DWORD device2_rva, DWORD context2_rva,
                               DWORD dxgi3_rva) noexcept {
  const bool device_patched = WriteIid(device2_rva, kIidD3D11Device);
  const bool context_patched = WriteIid(context2_rva, kIidD3D11DeviceContext);

  log::Write(device_patched && context_patched
                 ? "Windows 7 D3D: Device2/Context2 QueryInterface IIDs replaced with base interfaces"
                 : "Windows 7 D3D base-IID patch protection failed; retained-pointer fallback remains active");

  if (dxgi3_rva == 0) {
    return;
  }

  void* address = game::Resolve(dxgi3_rva);
  if (address == nullptr) {
    return;
  }
  if (!MatchesSignature(address, &kIidDxgiDevice3, sizeof(GUID)) &&
      !MatchesSignature(address, &kIidDxgiDevice, sizeof(GUID))) {
    log::Write("Windows 7 DXGI Device3 IID patch refused: unexpected bytes");
    return;
  }

  log::Write(WriteCode(address, &kIidDxgiDevice, sizeof(GUID))
                 ? "Windows 7 DXGI: IDXGIDevice3 query downgraded to IDXGIDevice"
                 : "Windows 7 DXGI Device3 IID patch protection failed");
}

}  // namespace

namespace compat_internal {

void InstallD3DDeviceCompatibility(DWORD create_device_rva,
                                   DWORD device2_iid_rva,
                                   DWORD context2_iid_rva,
                                   DWORD dxgi3_iid_rva) noexcept {
  if (HookSlot(create_device_rva,
               reinterpret_cast<const void*>(&CreateCompatibleD3D11Device),
               reinterpret_cast<void**>(&g_original_create_device),
               "D3D11CreateDevice")) {
    log::Write("installed D3D11CreateDevice compatibility hook");
  } else {
    log::Write("D3D11CreateDevice IAT hook protection failed");
  }

#if REUWP_WIN7_DOWNGRADE_D3D_IIDS
  if (game::IsWindows7()) {
    InstallWindows7IidPatches(device2_iid_rva, context2_iid_rva,
                              dxgi3_iid_rva);
  }
#else
  (void)device2_iid_rva;
  (void)context2_iid_rva;
  (void)dxgi3_iid_rva;
#endif
}

}  // namespace compat_internal

void FinalizeRetainedD3DDeviceFallback(void* owner, DWORD device_offset,
                                       void* renderer,
                                       DWORD context_offset) noexcept {
#if REUWP_WIN7_RETAIN_BASE_D3D_INTERFACES
  if (owner == nullptr || renderer == nullptr || device_offset == 0 ||
      context_offset == 0) {
    ReleaseRetainedD3DDevicePair();
    return;
  }

  auto** device_slot = reinterpret_cast<void**>(
      static_cast<unsigned char*>(owner) + device_offset);
  auto** context_slot = reinterpret_cast<void**>(
      static_cast<unsigned char*>(renderer) + context_offset);
  if (::IsBadReadPtr(device_slot, sizeof(void*)) ||
      ::IsBadWritePtr(device_slot, sizeof(void*)) ||
      ::IsBadReadPtr(context_slot, sizeof(void*)) ||
      ::IsBadWritePtr(context_slot, sizeof(void*))) {
    ReleaseRetainedD3DDevicePair();
    return;
  }

  if (game::IsWindows7() &&
      (*device_slot == nullptr || *context_slot == nullptr) &&
      g_retained_d3d_device != nullptr && g_retained_d3d_context != nullptr) {
    if (*device_slot == nullptr) {
      *device_slot = g_retained_d3d_device;
      g_retained_d3d_device = nullptr;
      log::Write("Windows 7 fallback: base ID3D11Device stored in game owner");
    }
    if (*context_slot == nullptr) {
      *context_slot = g_retained_d3d_context;
      g_retained_d3d_context = nullptr;
      log::Write("Windows 7 fallback: base ID3D11DeviceContext stored in renderer");
    }
  }
  ReleaseRetainedD3DDevicePair();
#else
  (void)owner;
  (void)device_offset;
  (void)renderer;
  (void)context_offset;
  ReleaseRetainedD3DDevicePair();
#endif
}

}  // namespace shim::hooks
