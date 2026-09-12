// D3D11CreateDevice: reenvio perezoso a d3d11.dll.
//
// Original: sub_62949330, exportada como D3D11CreateDevice (ordinal 7).
//
// El shim exporta este simbolo porque se hace pasar por la DLL de la que el
// juego lo importa. La funcion en si no intercepta nada: resuelve la de verdad
// la primera vez y a partir de ahi reenvia.
//
// La inyeccion del swapchain sobre HWND NO ocurre aqui, sino mas tarde, cuando
// el bootstrap sustituye el swapchain del DX::DeviceResources ya construido.

#include "shim/common.h"

#include <d3d11.h>

namespace {

using D3D11CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE,
                                             HMODULE, UINT,
                                             const D3D_FEATURE_LEVEL*, UINT,
                                             UINT, ID3D11Device**,
                                             D3D_FEATURE_LEVEL*,
                                             ID3D11DeviceContext**);

// dword_62996250 en el original: la funcion real, cacheada tras el primer uso.
D3D11CreateDeviceFn g_real_create_device = nullptr;

// Resuelve la funcion real. Devuelve nullptr si d3d11.dll no esta disponible.
D3D11CreateDeviceFn Resolve() noexcept {
  if (g_real_create_device != nullptr) {
    return g_real_create_device;
  }

  const HMODULE module = ::LoadLibraryW(L"d3d11.dll");
  if (module == nullptr) {
    return nullptr;
  }

  g_real_create_device = reinterpret_cast<D3D11CreateDeviceFn>(
      ::GetProcAddress(module, "D3D11CreateDevice"));
  return g_real_create_device;
}

}  // namespace

extern "C" {

HRESULT SHIM_COM D3D11CreateDevice(IDXGIAdapter* adapter,
                                   D3D_DRIVER_TYPE driver_type,
                                   HMODULE software, UINT flags,
                                   const D3D_FEATURE_LEVEL* feature_levels,
                                   UINT feature_level_count, UINT sdk_version,
                                   ID3D11Device** device,
                                   D3D_FEATURE_LEVEL* feature_level,
                                   ID3D11DeviceContext** immediate_context) {
  const D3D11CreateDeviceFn real = Resolve();
  if (real == nullptr) {
    return E_FAIL;
  }
  return real(adapter, driver_type, software, flags, feature_levels,
              feature_level_count, sdk_version, device, feature_level,
              immediate_context);
}

}  // extern "C"
