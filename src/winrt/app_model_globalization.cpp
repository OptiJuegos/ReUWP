#include "app_model_internal.h"
#include "shim/winrt_string_compat.h"

#include "shim/log.h"

#include <winstring.h>

namespace shim::winrt {
namespace {

// --- Globalizacion --------------------------------------------------------

// PrimaryLanguageOverride (sub_6294C3E0).
//
// Devuelve "USD" y no "en-US" cuando quien pregunta es el objeto de moneda: el
// original comparte este metodo entre el idioma y la divisa, y los separa por el
// `kind` del objeto que llama.
HRESULT SHIM_COM GetPrimaryLanguage(Object* self, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  constexpr int kCurrencyKind = 36;
  const wchar_t* value =
      (self != nullptr && self->kind == kCurrencyKind) ? L"USD" : L"en-US";
  return shim::winrt_string::Create(value, static_cast<UINT32>(::lstrlenW(value)),
                               out);
}

// put_PrimaryLanguageOverride (sub_6294C430): se traga el valor y dice que si.
//
// Devolver un error haria que el juego lo tratara como fallo de configuracion;
// aceptarlo y no hacer nada lo deja en ingles, que es lo unico que hay.
HRESULT SHIM_COM SetPrimaryLanguage(Object* self, HSTRING value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}


// IVectorView<HSTRING> shared by ApplicationLanguages (kind 33) and the
// GeographicRegion currencies vector (kind 36).  The original uses the same
// vtable at 0x62993510 and selects en-US vs USD from Object::kind.
HRESULT SHIM_COM HStringVectorGetAt(Object* self, unsigned int index,
                                    HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (index != 0) {
    return static_cast<HRESULT>(0x8000000BL);  // E_BOUNDS
  }
  const wchar_t* value =
      (self != nullptr && self->kind == kCurrencyVector) ? L"USD" : L"en-US";
  return shim::winrt_string::Create(value, static_cast<UINT32>(::lstrlenW(value)),
                               out);
}

HRESULT SHIM_COM HStringVectorGetSize(Object* self,
                                      unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

HRESULT SHIM_COM HStringVectorIndexOf(Object* self, HSTRING value,
                                      unsigned int* index,
                                      unsigned char* found) noexcept {
  (void)self;
  (void)value;
  if (index == nullptr || found == nullptr) {
    return E_POINTER;
  }
  // 0x6294C550 does not compare the HSTRING: the one-element fake vector
  // always reports its sole item at index zero.
  *index = 0;
  *found = 1;
  return S_OK;
}

HRESULT SHIM_COM HStringVectorGetMany(Object* self, unsigned int start,
                                      unsigned int capacity, HSTRING* items,
                                      unsigned int* written) noexcept {
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  if (capacity != 0 && items == nullptr) {
    return E_POINTER;
  }
  if (start != 0 || capacity == 0) {
    return S_OK;
  }

  const wchar_t* value =
      (self != nullptr && self->kind == kCurrencyVector) ? L"USD" : L"en-US";
  const HRESULT hr = shim::winrt_string::Create(
      value, static_cast<UINT32>(::lstrlenW(value)), &items[0]);
  if (SUCCEEDED(hr)) {
    *written = 1;
  }
  return hr;
}

HRESULT SHIM_COM GetRegionTextUS(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"US", 2, out);
}

HRESULT SHIM_COM GetRegionTextUSA(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"USA", 3, out);
}

HRESULT SHIM_COM GetRegionText840(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"840", 3, out);
}

HRESULT SHIM_COM GetRegionDisplayName(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"United States", 13, out);
}



#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

// 0x62993510: IVectorView<HSTRING>, 10 slots.  The exact same table is used
// by kind 33 (en-US) and kind 36 (USD).
Method g_language_vector_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&HStringVectorGetAt),
    reinterpret_cast<Method>(&HStringVectorGetSize),
    reinterpret_cast<Method>(&HStringVectorIndexOf),
    reinterpret_cast<Method>(&HStringVectorGetMany),
};
static_assert(CountOf(g_language_vector_vtable) == 10,
              "language vector must match 0x62993510");

Object g_language_vector = {g_language_vector_vtable, 1, kLanguageVector};
Object g_currency_vector = {g_language_vector_vtable, 1, kCurrencyVector};

HRESULT SHIM_COM GetLanguageVector(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_language_vector, out);
  if (SUCCEEDED(result)) {
    log::Write("ApplicationLanguages returned en-US vector");
  }
  return result;
}

// IApplicationLanguagesStatics (0x629934E8).
Method g_application_languages_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetPrimaryLanguage),
    reinterpret_cast<Method>(&SetPrimaryLanguage),
    reinterpret_cast<Method>(&GetLanguageVector),  // Languages
    reinterpret_cast<Method>(&GetLanguageVector),  // ManifestLanguages
};

// 0x62993554: IGeographicRegion, 13 slots.  IDA made this object look almost
// empty, but the raw table contains seven properties after IInspectable.
HRESULT SHIM_COM GetRegionCurrencies(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_currency_vector, out);
}

Method g_geographic_region_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetRegionTextUS),           //  6
    reinterpret_cast<Method>(&GetRegionTextUS),           //  7
    reinterpret_cast<Method>(&GetRegionTextUSA),          //  8
    reinterpret_cast<Method>(&GetRegionText840),          //  9
    reinterpret_cast<Method>(&GetRegionDisplayName),      // 10
    reinterpret_cast<Method>(&GetRegionDisplayName),      // 11
    reinterpret_cast<Method>(&GetRegionCurrencies),       // 12
};
static_assert(CountOf(g_geographic_region_vtable) == 13,
              "GeographicRegion must match 0x62993554");

Object g_geographic_region = {g_geographic_region_vtable, 1,
                              kGeographicRegion};

HRESULT SHIM_COM GetCurrentRegion(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_geographic_region, out);
  if (SUCCEEDED(result)) {
    log::Write("GeographicRegion activated as en-US/US");
  }
  return result;
}

Method g_geographic_region_statics_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetCurrentRegion),
};

Object g_application_languages = {g_application_languages_vtable, 1,
                                  kApplicationLanguages};
Object g_geographic_region_statics = {g_geographic_region_statics_vtable, 1,
                                      kGeographicRegionStatics};

}  // namespace

Object* LanguageVectorSingleton() noexcept { return &g_language_vector; }
Object* ApplicationLanguagesStatics() noexcept { return &g_application_languages; }
Object* GeographicRegionStatics() noexcept { return &g_geographic_region_statics; }

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
