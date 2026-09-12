#include "shim/winrt_object.h"
#include "shim/winrt_string_compat.h"

#include <winstring.h>

namespace shim::winrt {

ULONG SHIM_COM AddRef(Object* self) noexcept {
  return static_cast<ULONG>(::InterlockedIncrement(&self->ref_count));
}

ULONG SHIM_COM Release(Object* self) noexcept {
  const LONG remaining = ::InterlockedDecrement(&self->ref_count);
  if (remaining <= 0) {
    // Singleton estatico: en vez de destruirlo se fija el contador en 1.
    // Asi un juego que desbalancee AddRef/Release no puede provocar un
    // use-after-free ni que el contador se vaya a negativo.
    ::InterlockedExchange(&self->ref_count, 1);
    return 1;
  }
  return static_cast<ULONG>(remaining);
}

HRESULT SHIM_COM GetIids(Object* self, ULONG* iid_count, IID** iids) noexcept {
  (void)self;
  if (iid_count != nullptr) {
    *iid_count = 0;
  }
  if (iids != nullptr) {
    *iids = nullptr;
  }
  return S_OK;
}

HRESULT SHIM_COM GetRuntimeClassName(Object* self, HSTRING* class_name) noexcept {
  (void)self;
  if (class_name == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(kRuntimeClassName, kRuntimeClassNameLength,
                               class_name);
}

HRESULT SHIM_COM GetTrustLevel(Object* self, int* trust_level) noexcept {
  (void)self;
  if (trust_level == nullptr) {
    return E_POINTER;
  }
  *trust_level = 0;  // BaseTrust
  return S_OK;
}

HRESULT ReturnSingleton(Object* singleton, void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = singleton;
  AddRef(singleton);
  return S_OK;
}

}  // namespace shim::winrt
